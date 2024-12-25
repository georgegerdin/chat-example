#include "chatclient.h"
#include "packet.hh"
#include "format.hh"
#include <iostream>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>

ChatClient::ChatClient(const std::string& name)
    : name_(name), io_context_(), work_guard_(boost::asio::make_work_guard(io_context_)),
    socket_(io_context_), closed_(false), logged_in_(false) {
    //dbgln("[CLIENT {}] Initializing", name_);

    Connect.connect([this] (auto host, auto port)
    {
        connect(host, port);
    }, &io_context_);

    SendMessage.connect([this] (auto message)
    {
        send_message(message);
    }, &io_context_);
    CreateUser.connect([this] (auto username, auto password)
    {
        create_user(username, password);
    }, &io_context_);
    Close.connect([this]
    {
        close();
    }, &io_context_);
    Login.connect([this] (auto username, auto password)
    {
        login(username, password);
    }, &io_context_);
}

void ChatClient::start() {
    io_thread_ = std::thread([this]() {
        //dbgln("[CLIENT {}] IO thread started", name_);
        io_context_.run();
        //dbgln("[CLIENT {}] IO thread ended", name_);
    });
}

void ChatClient::stop() {
    //dbgln("[CLIENT {}] Stopping client", name_);
    Close.emit();
    if (io_thread_.joinable()) {
        io_context_.stop();
        io_thread_.join();
    }
    //dbgln("[CLIENT {}] Client stopped", name_);
}

void ChatClient::connect(const std::string& host, const std::string& port) {
    try {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, port);

        boost::asio::async_connect(socket_, endpoints,
                                   [this](const boost::system::error_code& ec, const boost::asio::ip::tcp::endpoint&) {
                                       if (!ec) {
                                           ////dbgln("[CLIENT {}] Connected to server", name_);
                                           on_connected.emit();
                                           do_read_header();
                                       } else {
                                           //dbgln("[CLIENT {}] Connection failed: {}", name_, ec.message());
                                           on_disconnected.emit();
                                       }
                                   });
    } catch (std::exception& e) {
        //dbgln("[CLIENT {}] Connection failed: {}", name_, e.what());
        on_disconnected.emit();
        return;
    }
}

void ChatClient::close() {
    if (!closed_) {
        closed_ = true;
        boost::system::error_code ec;
        socket_.close(ec);
        //dbgln("[CLIENT {}] Closed connection. Error code: {}", name_, ec.value());
        on_disconnected.emit();
    }
}

void ChatClient::write(const Packet& packet) {
    auto prepared_packet = Packet::preparePacketForSending(packet);
    //dbgln("[CLIENT {}] Sending packet of type: {}", name_, static_cast<int>(packet.getType()));
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(std::move(prepared_packet));
    if (!write_in_progress) {
        do_write();
    }
}

void ChatClient::do_read_header() {
    boost::asio::async_read(socket_, boost::asio::buffer(&next_packet_size_, sizeof(uint32_t)),
                            [this](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec) {
                                    if (next_packet_size_ > 1024 * 1024) {  // Limit packet size to 1MB
                                        //dbgln("[CLIENT {}] Received packet size too large: {}", name_, next_packet_size_);
                                        close();
                                        return;
                                    }
                                    read_msg_.resize(next_packet_size_);
                                    do_read_body();
                                } else if (ec != boost::asio::error::operation_aborted) {
                                    //dbgln("[CLIENT {}] Read header error: {}", name_, ec.message());
                                    close();
                                }
                            });
}

void ChatClient::do_read_body() {
    boost::asio::async_read(socket_, boost::asio::buffer(read_msg_),
                            [this](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec) {
                                    handle_packet(read_msg_);
                                    do_read_header();
                                } else if (ec != boost::asio::error::operation_aborted) {
                                    //dbgln("[CLIENT {}] Read body error: {}", name_, ec.message());
                                    close();
                                }
                            });
}

void ChatClient::do_write() {
    boost::asio::async_write(socket_,
                             boost::asio::buffer(write_msgs_.front()),
                             [this](boost::system::error_code ec, std::size_t /*length*/) {
                                 if (!ec) {
                                     write_msgs_.pop_front();
                                     if (!write_msgs_.empty()) {
                                         do_write();
                                     }
                                 } else {
                                     //dbgln("[CLIENT {}] Write error: {}", name_, ec.message());
                                     close();
                                 }
                             });
}

void ChatClient::login(const std::string& username, const std::string& password) {
    //dbgln("[CLIENT {}] Attempting login for user: {}", name_, username);
    write(LoginPacket(username, password));
}

void ChatClient::create_user(const std::string& username, const std::string& password) {
    //dbgln("[CLIENT {}] Attempting to create user: {}", name_, username);
    write(CreateUserPacket(username, password));
}

void ChatClient::send_message(const std::string& message) {
    if (logged_in_) {
        write(ChatMessagePacket(name_, message));
        //dbgln("[CLIENT {}] Sent message: {}", name_, message);
    } else {
        //dbgln("[CLIENT {}] Cannot send message: not logged in", name_);
    }
}

void ChatClient::handle_packet(const std::vector<uint8_t>& packet_data) {
    //dbgln("[CLIENT {}] Handling packet of size: {}", name_, packet_data.size());
    auto packet = createPacketFromData(packet_data);
    if (!packet) {
        //dbgln("[CLIENT {}] Received invalid packet from server", name_);
        return;
    }

    //dbgln("[CLIENT {}] Received packet of type: {}", name_, static_cast<int>(packet->getType()));

    switch (packet->getType()) {
    case PacketType::LoginSuccess: {
        //dbgln("[CLIENT {}] Login successful", name_);
        logged_in_ = true;
        on_login_response.emit(true);
        break;
    }
    case PacketType::LoginFailed: {
        //dbgln("[CLIENT {}] Login failed", name_);
        logged_in_ = false;
        on_login_response.emit(false);
        break;
    }
    case PacketType::AccountCreated: {
        //dbgln("[CLIENT {}] Account created successfully", name_);
        account_created_ = true;
        on_create_account_response.emit(true);
        break;
    }
    case PacketType::AccountExists: {
        //dbgln("[CLIENT {}] Account creation failed: username already exists", name_);
        account_created_ = false;
        on_create_account_response.emit(false);
        break;
    }
    case PacketType::ChatMessage: {
        auto chat_message = static_cast<ChatMessagePacket*>(packet.get());
        //dbgln("[CLIENT {}] Received message from {}: {}", name_, chat_message->getSender(), chat_message->getMessage());
        on_message_received.emit(chat_message->getSender(), chat_message->getMessage());
        break;
    }
    case PacketType::Login:
    case PacketType::CreateUser:
        //dbgln("[CLIENT {}] Received unexpected packet type from server: {}", name_, static_cast<int>(packet->getType()));
        break;
    default:
        //dbgln("[CLIENT {}] Received unknown packet type from server: {}", name_, static_cast<int>(packet->getType()));
        break;
    }
}

