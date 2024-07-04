#include "chatserver.h"
#include "format.hh"
#include <iostream>
#include <deque>

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(boost::asio::ip::tcp::socket socket, ChatServer& server)
        : socket_(std::move(socket)), server_(server) {
        dbgln("[SERVER] New chat session created");
    }

    void start() {
        dbgln("[SERVER] Starting chat session");
        do_read();
    }

    void deliver(const std::string& message) {
        bool write_in_progress = !write_msgs_.empty();
        write_msgs_.push_back(message);
        dbgln("[SERVER] Message queued for delivery: {}", message);
        if (!write_in_progress) {
            do_write();
        }
    }

    void stop() {
        dbgln("[SERVER] Stopping chat session");
        socket_.close();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_msg_), "\n",
                                      [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                          if (!ec) {
                                              std::string msg(read_msg_.substr(0, read_msg_.find("\n")));
                                              read_msg_.erase(0, read_msg_.find("\n") + 1);
                                              dbgln("[SERVER] Raw message received in ChatSession: {}", msg);
                                              server_.handle_message(self, msg);
                                              do_read();
                                          } else {
                                              dbgln("[SERVER] Read error in ChatSession: {}", ec.message());
                                          }
                                      });
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
                                 boost::asio::buffer(write_msgs_.front()),
                                 [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                     if (!ec) {
                                         dbgln("[SERVER] Message sent successfully: {}", write_msgs_.front());
                                         write_msgs_.pop_front();
                                         if (!write_msgs_.empty()) {
                                             do_write();
                                         }
                                     } else {
                                         dbgln("[SERVER] Write error: {}", ec.message());
                                         socket_.close();
                                     }
                                 });
    }

    boost::asio::ip::tcp::socket socket_;
    ChatServer& server_;
    std::string read_msg_;
    std::deque<std::string> write_msgs_;
};

ChatServer::ChatServer(boost::asio::io_context& io_context, short port)
    : io_context_(io_context),
    acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    user_credentials_["Alice"] = "password123";
    dbgln("[SERVER] ChatServer constructor");
    do_accept();
    dbgln("[SERVER] Server started on port {}", port);
}

void ChatServer::stop() {
    dbgln("[SERVER] Stopping server...");
    stop_flag_ = true;
    acceptor_.close();
    for (auto& client : clients_) {
        client.first->stop();
    }
    clients_.clear();
    dump_accounts();
}

void ChatServer::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec && !stop_flag_) {
                dbgln("[SERVER] New connection accepted");
                auto session = std::make_shared<ChatSession>(std::move(socket), *this);
                clients_[session] = "Anonymous";
                session->start();
                do_accept();
            } else if (ec) {
                dbgln("[SERVER] Accept error: {}", ec.message());
            }
        });
}

bool ChatServer::authenticate_user(const std::string& username, const std::string& password) {
    dbgln("[SERVER] Authenticating user: {}", username);
    auto it = user_credentials_.find(username);
    if (it != user_credentials_.end()) {
        return it->second == password;
    }
    return false;
}

bool ChatServer::create_user(const std::string& username, const std::string& password) {
    dbgln("[SERVER] Creating user: {}", username);
    if (user_credentials_.find(username) == user_credentials_.end()) {
        user_credentials_[username] = password;
        return true;
    }
    return false;
}

void ChatServer::dump_accounts() const {
    dbgln("[SERVER] Available accounts:");
    for (const auto& account : user_credentials_) {
        dbgln("[SERVER] Username: {}, Password: {}", account.first, account.second);
    }
}

void ChatServer::disconnect_client(std::shared_ptr<ChatSession> client, const std::string& reason) {
    dbgln("[SERVER] Disconnecting client. Reason: {}", reason);
    client->deliver(reason + "\n");
    client->stop();
    clients_.erase(client);
}

void ChatServer::handle_message(std::shared_ptr<ChatSession> sender, const std::string& message) {
    dbgln("[SERVER] Handling message: '{}' from client", message);

    if (message.empty()) {
        dbgln("[SERVER] Ignored empty message");
        return;
    }

    if (message.substr(0, 6) == "LOGIN:") {
        std::string credentials = message.substr(6);
        size_t delimiter = credentials.find(':');
        if (delimiter != std::string::npos) {
            std::string username = credentials.substr(0, delimiter);
            std::string password = credentials.substr(delimiter + 1);
            if (authenticate_user(username, password)) {
                clients_[sender] = username;
                sender->deliver("LOGIN_SUCCESS\n");
                dbgln("[SERVER] Login successful for user: {}", username);
            } else {
                disconnect_client(sender, "LOGIN_FAILED");
                dbgln("[SERVER] Login failed for user: {}", username);
            }
        }
    } else if (message.substr(0, 7) == "CREATE:") {
        std::string credentials = message.substr(7);
        size_t delimiter = credentials.find(':');
        if (delimiter != std::string::npos) {
            std::string username = credentials.substr(0, delimiter);
            std::string password = credentials.substr(delimiter + 1);
            if (create_user(username, password)) {
                disconnect_client(sender, "ACCOUNT_CREATED");
                dbgln("[SERVER] Account created successfully for user: {}", username);
            } else {
                disconnect_client(sender, "ACCOUNT_EXISTS");
                dbgln("[SERVER] Account creation failed for user: {} (already exists)", username);
            }
        }
    } else if (message.substr(0, 5) == "NAME:") {
        std::string new_name = message.substr(5);
        clients_[sender] = new_name;
        dbgln("[SERVER] Client set name to: {}", new_name);
    } else {
        // This is a regular chat message
        dbgln("[SERVER] Received a regular chat message");
        std::string sender_name = clients_[sender];
        dbgln("[SERVER] Sender: {}", sender_name);
        std::string full_message = sender_name + ": " + message;
        dbgln("[SERVER] Full message to broadcast: {}", full_message);

        int broadcast_count = 0;
        for (auto& client : clients_) {
            if (client.first != sender) {
                dbgln("[SERVER] Attempting to deliver message to client: {}", client.second);
                client.first->deliver(full_message + "\n");
                broadcast_count++;
            }
        }

        dbgln("[SERVER] Broadcasted message to {} clients", broadcast_count);
    }
}
