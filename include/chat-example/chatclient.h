// chatclient.h
#pragma once

#include <boost/asio.hpp>
#include <string>
#include <deque>
#include <mutex>
#include <atomic>
#include <future>

class ChatClient {
public:
    ChatClient(boost::asio::io_context& io_context, const std::string& name);

    bool connect(const std::string& host, const std::string& port);
    void write(const std::string& msg);
    void close();
    bool is_open() const { return !closed_; }
    void queue_message(const std::string& msg);
    void process_write_queue();
    bool login(const std::string& username, const std::string& password);
    bool create_user(const std::string& username, const std::string& password);
    bool is_logged_in() const { return logged_in_; }
    void set_disconnected_callback(std::function<void()> callback);
    void send_message(const std::string& message);
private:
    void do_read();
    void do_write();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    std::string name_;
    std::string read_msg_;
    std::deque<std::string> write_msgs_;
    std::atomic<bool> closed_{false};
    bool logged_in_ = false;
    std::promise<bool> login_promise_;
    std::promise<bool> create_user_promise_;
    std::function<void()> disconnected_callback_;

    std::mutex queue_mutex_;
    std::deque<std::string> message_queue_;
};
