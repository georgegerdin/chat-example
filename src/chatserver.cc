#include "chatserver.h"
#include "packet.hh"
#include "format.hh"
#include <iostream>
#include <algorithm>

ChatServer::ChatServer(boost::asio::io_context& io_context, short port)
    : io_context_(io_context),
    acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    stop_flag_(false) {
    dbgln("[SERVER] ChatServer constructor");
    do_accept();
    dbgln("[SERVER] Server started on port {}", port);
}

void ChatServer::stop() {
    dbgln("[SERVER] Stopping server...");
    stop_flag_ = true;
    acceptor_.close();
    dbgln("[SERVER] Acceptor closed");

    // Move this to protect us from lifetime problems
    std::set<std::shared_ptr<ChatSession>> participants = std::move(participants_);
    participants_.clear();

    dbgln("[SERVER] Stopping participant sessions");
    for (auto& participant : participants) {
        boost::asio::post(io_context_, [&participant] {
            dbgln("[SERVER] Stopping participant {}", participant->get_username());
            participant->stop();
        });
    }

    io_context_.run();

    dbgln("[SERVER] All participant sessions stopped");
    participants.clear();
    dbgln("[SERVER] Participants cleared");
    dump_accounts();
    dbgln("[SERVER] Server stop completed");
}

void ChatServer::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec && !stop_flag_) {
                dbgln("[SERVER] New connection accepted");
                auto session = std::make_shared<ChatSession>(std::move(socket), *this);
                participants_.insert(session);
                session->start();
                do_accept();
            } else if (ec) {
                dbgln("[SERVER] Accept error: {}", ec.message());
            }
        });
}

void ChatServer::handle_packet(std::shared_ptr<ChatSession> sender, const std::vector<uint8_t>& packet_data) {
    dbgln("[SERVER] Handling packet of size: {}", packet_data.size());
    auto packet = createPacketFromData(packet_data);
    if (!packet) {
        dbgln("[SERVER] Received invalid packet from client");
        return;
    }

    dbgln("[SERVER] Received packet of type: {}", static_cast<int>(packet->getType()));

    switch (packet->getType()) {
    case PacketType::Login: {
        auto login_packet = static_cast<LoginPacket*>(packet.get());
        dbgln("[SERVER] Login attempt from user: {}", login_packet->getUsername());

        bool success = authenticate_user(login_packet->getUsername(), login_packet->getPassword());
        if (success) {
            dbgln("[SERVER] Login successful for user: {}", login_packet->getUsername());
            sender->set_username(login_packet->getUsername());
            sender->deliver(LoginSuccessPacket());

            // Notify other users about the new user
            ChatMessagePacket system_msg("System", login_packet->getUsername() + " has joined the chat.");
            broadcast(system_msg, sender);
        } else {
            dbgln("[SERVER] Login failed for user: {}", login_packet->getUsername());
            sender->deliver(LoginFailedPacket());
        }
        break;
    }
    case PacketType::CreateUser: {
        auto create_user_packet = static_cast<CreateUserPacket*>(packet.get());
        dbgln("[SERVER] Account creation attempt for username: {}", create_user_packet->getUsername());

        bool success = create_user(create_user_packet->getUsername(), create_user_packet->getPassword());
        if (success) {
            dbgln("[SERVER] Account created successfully for user: {}", create_user_packet->getUsername());
            sender->deliver(AccountCreatedPacket());
        } else {
            dbgln("[SERVER] Account creation failed for user: {} (username already exists)", create_user_packet->getUsername());
            sender->deliver(AccountExistsPacket());
        }
        break;
    }
    case PacketType::ChatMessage: {
        auto chat_message_packet = static_cast<ChatMessagePacket*>(packet.get());
        std::string sender_name = sender->get_username();

        if (sender_name.empty()) {
            dbgln("[SERVER] Received chat message from unauthenticated user");
            sender->deliver(LoginFailedPacket());
            return;
        }

        dbgln("[SERVER] Received chat message from {}: {}", sender_name, chat_message_packet->getMessage());

        // Create a new packet with the sender's name
        ChatMessagePacket broadcast_packet(sender_name, chat_message_packet->getMessage());
        broadcast(broadcast_packet, sender);
        break;
    }
    case PacketType::LoginSuccess:
    case PacketType::LoginFailed:
    case PacketType::AccountCreated:
    case PacketType::AccountExists:
        dbgln("[SERVER] Received unexpected packet type from client: {}", static_cast<int>(packet->getType()));
        break;
    default:
        dbgln("[SERVER] Received unknown packet type from client: {}", static_cast<int>(packet->getType()));
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

bool ChatServer::authenticate_user(const std::string& username, const std::string& password) {
    dbgln("[SERVER] Attempting to authenticate user: {}", username);
    auto it = user_credentials_.find(username);
    if (it != user_credentials_.end() && it->second == password) {
        dbgln("[SERVER] Authentication successful for user: {}", username);
        return true;
    }
    dbgln("[SERVER] Authentication failed for user: {}", username);
    return false;
}

bool ChatServer::create_user(const std::string& username, const std::string& password) {
    dbgln("[SERVER] Attempting to create user: {}", username);
    if (user_credentials_.find(username) == user_credentials_.end()) {
        user_credentials_[username] = password;
        dbgln("[SERVER] User created successfully: {}", username);
        return true;
    }
    dbgln("[SERVER] User creation failed: username already exists: {}", username);
    return false;
}


void ChatServer::dump_accounts() const {
    dbgln("[SERVER] Available accounts:");
    for (const auto& account : user_credentials_) {
        dbgln("[SERVER] Username: {}, Password: {}", account.first, account.second);
    }
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
    dbgln("[SERVER] New chat session created");
}

void ChatSession::start() {
    dbgln("[SERVER] Starting chat session");
    do_read_header();
}

void ChatSession::deliver(const Packet& packet) {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(preparePacketForSending(packet));
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
                                        dbgln("[SERVER] Received packet size too large: {}", next_packet_size_);
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
                                    dbgln("[SERVER] Received packet body of size: {}", length);
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
