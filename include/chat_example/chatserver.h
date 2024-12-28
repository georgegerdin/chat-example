// chatserver.h
#pragma once

#include <boost/asio.hpp>
#include <string>
#include <unordered_map>
#include <set>
#include <memory>
#include <deque>
#include <atomic>
#include "packet.hh"
#include "databaseadapter.hh"

class ChatServer;

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(boost::asio::ip::tcp::socket socket, ChatServer& server);

    void start();
    void deliver(const Packet& packet);
    void stop();
    void set_username(const std::string& username);
    const std::string& get_username() const;

private:
    void do_read_header();
    void do_read_body();
    void do_write();

    boost::asio::ip::tcp::socket socket_;
    ChatServer& server_;
    std::string username_;
    uint32_t next_packet_size_;
    std::vector<uint8_t> read_msg_;
    std::deque<std::vector<uint8_t>> write_msgs_;
};

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, short port,
               std::shared_ptr<DatabaseAdapter> db_adapter);

    void stop();
    void handle_packet(std::shared_ptr<ChatSession> sender, const std::vector<uint8_t>& packet_data);
    void broadcast(const Packet& packet, std::shared_ptr<ChatSession> sender);
    void leave(std::shared_ptr<ChatSession> participant);

private:
    void do_accept();
    void authenticate_user(const std::string& username,
                           const std::string& password,
                           std::function<void(bool)> callback);
    void create_user(const std::string& username,
                     const std::string& password,
                     std::function<void(bool)> callback);
    void load_recent_messages(std::shared_ptr<ChatSession> session);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::set<std::shared_ptr<ChatSession>> participants_;
    std::atomic<bool> stop_flag_;
    std::shared_ptr<DatabaseAdapter> db_adapter_;
};
