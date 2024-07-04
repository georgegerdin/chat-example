// chatserver.h
#pragma once

#include <boost/asio.hpp>
#include <string>
#include <map>
#include <memory>
#include <atomic>

class ChatSession;

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, short port);
    void stop();

    void dump_accounts() const;
private:
    void do_accept();
    void handle_message(std::shared_ptr<ChatSession> sender, const std::string& message);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::map<std::shared_ptr<ChatSession>, std::string> clients_;
    std::atomic<bool> stop_flag_{false};

    friend class ChatSession;

    std::unordered_map<std::string, std::string> user_credentials_;

    bool authenticate_user(const std::string& username, const std::string& password);
    bool create_user(const std::string& username, const std::string& password);
    void disconnect_client(std::shared_ptr<ChatSession> client, const std::string& reason);
};
