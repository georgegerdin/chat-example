#include "chatclient.h"
#include "format.hh"
#include <iostream>

ChatClient::ChatClient(boost::asio::io_context& io_context, const std::string& name)
    : io_context_(io_context), socket_(io_context), name_(name) {
    dbgln("[CLIENT {}] Initializing", name_);
}

bool ChatClient::connect(const std::string& host, const std::string& port) {
    try {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, port);
        boost::asio::connect(socket_, endpoints);
        dbgln("[CLIENT {}] Connected to server at {}:{}", name_, host, port);
        do_read();
        write("NAME:" + name_ + "\n");
        return true;
    } catch (std::exception& e) {
        dbgln("[CLIENT {}] Connection failed: {}", name_, e.what());
        return false;
    }
}

void ChatClient::write(const std::string& msg) {
    boost::asio::post(socket_.get_executor(),
                      [this, msg]() {
                          bool write_in_progress = !write_msgs_.empty();
                          write_msgs_.push_back(msg);
                          dbgln("[CLIENT {}] Queued message for sending: {}", name_, msg);
                          if (!write_in_progress) {
                              do_write();
                          }
                      });
}

void ChatClient::close() {
    boost::asio::post(socket_.get_executor(), [this]() {
        if (!closed_) {
            closed_ = true;
            boost::system::error_code ec;
            socket_.close(ec);
            dbgln("[CLIENT {}] Closed connection", name_);
        }
    });
}

void ChatClient::queue_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.push_back(msg);
    dbgln("[CLIENT {}] Queued message: {}", name_, msg);
}

void ChatClient::process_write_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!message_queue_.empty()) {
        write(message_queue_.front());
        message_queue_.pop_front();
    }
}

void ChatClient::do_read() {
    boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_msg_), "\n",
                                  [this](boost::system::error_code ec, std::size_t /*length*/) {
                                      if (!ec) {
                                          std::string msg = read_msg_.substr(0, read_msg_.find("\n"));
                                          read_msg_.erase(0, read_msg_.find("\n") + 1);

                                          if (!msg.empty()) {
                                              dbgln("[CLIENT {}] Received: {}", name_, msg);

                                              if (msg == "LOGIN_SUCCESS") {
                                                  logged_in_ = true;
                                                  login_promise_.set_value(true);
                                                  dbgln("[CLIENT {}] Login successful", name_);
                                              } else if (msg == "LOGIN_FAILED") {
                                                  login_promise_.set_value(false);
                                                  dbgln("[CLIENT {}] Login failed", name_);
                                              } else if (msg == "ACCOUNT_CREATED") {
                                                  create_user_promise_.set_value(true);
                                                  dbgln("[CLIENT {}] Account created successfully", name_);
                                              } else if (msg == "ACCOUNT_EXISTS") {
                                                  create_user_promise_.set_value(false);
                                                  dbgln("[CLIENT {}] Account creation failed: Username already exists", name_);
                                              } else {
                                                  // Regular chat message
                                                  dbgln("{}", msg);
                                              }
                                          }

                                          do_read();
                                      } else if (!closed_) {
                                          dbgln("[CLIENT {}] Read error: {}", name_, ec.message());
                                          close();
                                          if (disconnected_callback_) {
                                              disconnected_callback_();
                                          }
                                      }
                                  });
}

void ChatClient::do_write() {
    boost::asio::async_write(socket_,
                             boost::asio::buffer(write_msgs_.front()),
                             [this](boost::system::error_code ec, std::size_t /*length*/) {
                                 if (!ec) {
                                     dbgln("[CLIENT {}] Message sent successfully: {}", name_, write_msgs_.front());
                                     write_msgs_.pop_front();
                                     if (!write_msgs_.empty()) {
                                         do_write();
                                     }
                                 } else if (!closed_) {
                                     dbgln("[CLIENT {}] Write error: {}", name_, ec.message());
                                     close();
                                 }
                             });
}

bool ChatClient::login(const std::string& username, const std::string& password) {
    dbgln("[CLIENT {}] Attempting login for user: {}", name_, username);
    login_promise_ = std::promise<bool>();
    std::future<bool> login_future = login_promise_.get_future();
    write("LOGIN:" + username + ":" + password + "\n");
    return login_future.get();
}

bool ChatClient::create_user(const std::string& username, const std::string& password) {
    dbgln("[CLIENT {}] Attempting to create user: {}", name_, username);
    create_user_promise_ = std::promise<bool>();
    std::future<bool> create_user_future = create_user_promise_.get_future();
    write("CREATE:" + username + ":" + password + "\n");
    return create_user_future.get();
}

void ChatClient::set_disconnected_callback(std::function<void()> callback) {
    disconnected_callback_ = std::move(callback);
}

void ChatClient::send_message(const std::string& message) {
    write(message + "\n");
    dbgln("[CLIENT {}] Sent message: {}", name_, message);
}
