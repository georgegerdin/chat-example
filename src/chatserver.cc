#include "chatserver.h"
#include "packet.hh"
#include "format.hh"

ChatServer::ChatServer(boost::asio::io_context& io_context,
                       short port,
                       std::shared_ptr<DatabaseAdapter> db_adapter)
    : io_context_(io_context),
    acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    stop_flag_(false),
    db_adapter_(std::move(db_adapter)) {
    //dbgln("[SERVER] ChatServer constructor");
    do_accept();
    //dbgln("[SERVER] Server started on port {}", port);
}

void ChatServer::stop() {
    //dbgln("[SERVER] Stopping server...");
    stop_flag_ = true;
    acceptor_.close();
    //dbgln("[SERVER] Acceptor closed");

    // Move this to protect us from lifetime problems
    std::set<std::shared_ptr<ChatSession>> participants = std::move(participants_);
    participants_.clear();

    //dbgln("[SERVER] Stopping participant sessions");
    for (auto& participant : participants) {
        boost::asio::post(io_context_, [&participant] {
            //dbgln("[SERVER] Stopping participant {}", participant->get_username());
            participant->stop();
        });
    }

    io_context_.run();

    //dbgln("[SERVER] All participant sessions stopped");
    participants.clear();
    //dbgln("[SERVER] Participants cleared");
    //dbgln("[SERVER] Server stop completed");
}

void ChatServer::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec && !stop_flag_) {
                //dbgln("[SERVER] New connection accepted");
                auto session = std::make_shared<ChatSession>(std::move(socket), *this);
                participants_.insert(session);
                session->start();
                do_accept();
            } else if (ec) {
                //dbgln("[SERVER] Accept error: {}", ec.message());
            }
        });
}

void ChatServer::handle_packet(std::shared_ptr<ChatSession> sender, const std::vector<uint8_t>& packet_data) {
    //dbgln("[SERVER] Handling packet of size: {}", packet_data.size());
    auto packet = createPacketFromData(packet_data);
    if (!packet) {
        //dbgln("[SERVER] Received invalid packet from client");
        return;
    }

    //dbgln("[SERVER] Received packet of type: {}", static_cast<int>(packet->getType()));

    switch (packet->getType()) {
    case PacketType::Login: {
        auto login_packet = static_cast<LoginPacket*>(packet.get());
        authenticate_user(
            login_packet->getUsername(),
            login_packet->getPassword(),
            [this, sender, username = login_packet->getUsername()](bool success) {
                if (success) {
                    sender->set_username(username);
                    sender->deliver(LoginSuccessPacket());

                    // Send recent messages to newly logged-in user
                    load_recent_messages(sender);

                    // Notify others
                    ChatMessagePacket system_msg("System", username + " has joined the chat.");
                    broadcast(system_msg, sender);
                } else {
                    sender->deliver(LoginFailedPacket());
                }
            });
        break;
    }
    case PacketType::CreateUser: {
        auto create_user_packet = static_cast<CreateUserPacket*>(packet.get());
        create_user(
            create_user_packet->getUsername(),
            create_user_packet->getPassword(),
            [this, sender](bool success) {
                if (success) {
                    sender->deliver(AccountCreatedPacket());
                } else {
                    sender->deliver(AccountExistsPacket());
                }
            });
        break;
    }
    case PacketType::ChatMessage: {
        auto chat_message_packet = static_cast<ChatMessagePacket*>(packet.get());
        std::string sender_name = sender->get_username();

        if (sender_name.empty()) {
            sender->deliver(LoginFailedPacket());
            return;
        }

        // Store message in database
        ChatMessage msg(sender_name, chat_message_packet->getMessage());
        db_adapter_->storeMessage(msg, [this, sender, msg](bool success) {
            if (success) {
                // Create a new packet with the sender's name and message from msg
                ChatMessagePacket broadcast_packet(msg.sender, msg.content);
                broadcast(broadcast_packet, sender);
            }
        });
        break;
    }
    case PacketType::LoginSuccess:
    case PacketType::LoginFailed:
    case PacketType::AccountCreated:
    case PacketType::AccountExists:
        //dbgln("[SERVER] Received unexpected packet type from client: {}", static_cast<int>(packet->getType()));
        break;
    default:
        //dbgln("[SERVER] Received unknown packet type from client: {}", static_cast<int>(packet->getType()));
        break;
    }
}

void ChatServer::broadcast(const Packet& packet, std::shared_ptr<ChatSession> sender) {
    for (auto& participant : participants_) {
        if (participant != sender) {
            participant->deliver(packet);
        }
    }
}

void ChatServer::authenticate_user(const std::string& username,
                                   const std::string& password,
                                   std::function<void(bool)> callback) {
    db_adapter_->authenticateUser(username, password, callback);
}

void ChatServer::create_user(const std::string& username,
                             const std::string& password,
                             std::function<void(bool)> callback) {
    db_adapter_->createUser(username, password, callback);
}

void ChatServer::load_recent_messages(std::shared_ptr<ChatSession> session) {
    db_adapter_->getRecentMessages(50, [this, session](const std::vector<ChatMessage>& messages) {
        for (const auto& msg : messages) {
            ChatMessagePacket packet(msg.sender, msg.content);
            session->deliver(packet);
        }
    });
}

void ChatServer::leave(std::shared_ptr<ChatSession> participant) {
    participants_.erase(participant);
    std::string username = participant->get_username();
    if (!username.empty()) {
        ChatMessagePacket system_msg("System", username + " has left the chat.");
        broadcast(system_msg, participant);
    }
}

ChatSession::ChatSession(boost::asio::ip::tcp::socket socket, ChatServer& server)
    : socket_(std::move(socket)), server_(server) {
    //dbgln("[SERVER] New chat session created");
}

void ChatSession::start() {
    //dbgln("[SERVER] Starting chat session");
    do_read_header();
}

void ChatSession::deliver(const Packet& packet) {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(Packet::preparePacketForSending(packet));
    if (!write_in_progress) {
        do_write();
    }
}

void ChatSession::stop() {
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    socket_.close();
}

void ChatSession::do_read_header() {
    auto self(shared_from_this());
    boost::asio::async_read(socket_, boost::asio::buffer(&next_packet_size_, sizeof(uint32_t)),
                            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec) {
                                    if (next_packet_size_ > 1024 * 1024) {  // Limit packet size to 1MB
                                        //dbgln("[SERVER] Received packet size too large: {}", next_packet_size_);
                                        server_.leave(self);
                                        return;
                                    }
                                    read_msg_.resize(next_packet_size_);
                                    do_read_body();
                                } else {
                                    server_.leave(self);
                                }
                            });
}

void ChatSession::do_read_body() {
    auto self(shared_from_this());
    boost::asio::async_read(socket_, boost::asio::buffer(read_msg_),
                            [this, self](boost::system::error_code ec, std::size_t length) {
                                if (!ec) {
                                    //dbgln("[SERVER] Received packet body of size: {}", length);
                                    server_.handle_packet(self, read_msg_);
                                    do_read_header();
                                } else {
                                    server_.leave(self);
                                }
                            });
}

void ChatSession::do_write() {
    auto self(shared_from_this());
    boost::asio::async_write(socket_,
                             boost::asio::buffer(write_msgs_.front()),
                             [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                 if (!ec) {
                                     write_msgs_.pop_front();
                                     if (!write_msgs_.empty()) {
                                         do_write();
                                     }
                                 } else {
                                     server_.leave(self);
                                 }
                             });
}

void ChatSession::set_username(const std::string& username) {
    username_ = username;
}

const std::string& ChatSession::get_username() const {
    return username_;
}
