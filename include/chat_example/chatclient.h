// chatclient.h
#pragma once

#include <boost/asio.hpp>
#include <string>
#include <deque>
#include <atomic>

#include "packet.hh"
#include "signal.hh"

class ChatClient {
public:
    ChatClient(const std::string& name);
    void start();
    void stop();

    Signal<const std::string&, const std::string&> Connect;
    Signal<const std::string&> SendMessage;
    Signal<const std::string&, const std::string&> CreateUser;
    Signal<const std::string&, const std::string&> Login;
    Signal<> Close;

    Signal<> on_disconnected;
    Signal<> on_connected;
    Signal<bool> on_login_response;
    Signal<bool> on_create_account_response;
    Signal<const std::string&, const std::string&> on_message_received;

    bool is_logged_in() const {return logged_in_;}
private:
    void connect(const std::string& host, const std::string& port);
    void write(const std::string& msg);
    void close();
    bool is_open() const { return !closed_; }
    void login(const std::string& username, const std::string& password);
    void create_user(const std::string& username, const std::string& password);
    void send_message(const std::string& message);

    void do_read();
    void do_write();
    void do_read_header();
    void do_read_body();
    void write(const Packet& packet);

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread io_thread_;
    std::string name_;
    std::vector<uint8_t> read_msg_;
    std::deque<std::vector<uint8_t>> write_msgs_;
    std::atomic<bool> closed_{false};
    std::atomic<bool> logged_in_ = false;

    void handle_packet(const std::vector<uint8_t>& packet_data);


    uint32_t next_packet_size_ = 0;
    bool account_created_ = false;
};
