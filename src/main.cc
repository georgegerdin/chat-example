#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <boost/asio.hpp>
#include "chatserver.h"
#include "chatclient.h"
#include "format.hh"

void run_server(short port, std::atomic<bool>& stop_flag) {
    try {
        dbgln("[MAIN] Starting server thread");
        boost::asio::io_context io_context;
        ChatServer server(io_context, port);
        dbgln("[MAIN] Server thread started");

        std::thread io_thread([&io_context]() {
            dbgln("[MAIN] Server IO thread started");
            io_context.run();
            dbgln("[MAIN] Server IO thread ended");
        });

        while (!stop_flag) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        dbgln("[MAIN] Stopping server");
        server.stop();
        io_context.stop();
        io_thread.join();
        dbgln("[MAIN] Server thread ended");
    }
    catch (std::exception& e) {
        dbgln("[MAIN] Server exception: {}", e.what());
    }
}

class ClientRunner {
public:
    ClientRunner(const std::string& name, const std::string& host, const std::string& port)
        : name_(name), host_(host), port_(port)
    {
        SendMessage.connect([this](auto message) {
            send_message(message);
        }, &io_context_);
        Stop.connect([this] {
            io_context_.stop();
        }, &io_context_);
    }

    void start() {
        thread_ = std::thread(&ClientRunner::run, this);
    }

    void stop() {
        Stop.emit();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    Signal<const std::string&> SendMessage;
    Signal<> Stop;

    std::string name() const {return name_;}
private:
    void send_message(const std::string& message) {
        if (client_ && client_->is_logged_in()) {
            client_->SendMessage.emit(message);
        }
    }

    void run() {
        try {
            auto work_guard = boost::asio::make_work_guard(io_context_);

            client_ = std::make_unique<ChatClient>(name_);
            client_->start();

            client_->Connect.emit(host_, port_);

            std::string username = name_;
            std::string password = "password123";

            client_->on_connected.connect([this, &username, &password] {
                dbgln("[CLIENT {}] Attempting to create user", name_);
                client_->CreateUser.emit(username, password);
            }, &io_context_);

            client_->on_create_account_response.connect([this, &username, &password](bool response)
            {
                dbgln("[CLIENT {}] Attempting to log in", name_);
                client_->Login.emit(username, password);
            }, &io_context_);

            client_->on_login_response.connect([this](bool response)
            {
                if(response) {
                    dbgln("[CLIENT {}] Logged in successfully", name_);
                } else {
                    dbgln("[CLIENT {}] Failed to log in", name_);
                }
            }, &io_context_);

            client_->on_message_received.connect([this](const std::string& sender, const std::string& message) {
                dbgln("[CLIENT {}] Received: {} - {}", name_, sender, message);
            }, &io_context_);

            client_->on_disconnected.connect([this] {
                dbgln("[CLIENT {}] Disconnected", name_);
                io_context_.stop();
            }, &io_context_);

            io_context_.run();

            dbgln("[CLIENT {}] Stopping client", name_);
            client_->stop();
            dbgln("[CLIENT {}] Client stopped", name_);
        }
        catch (std::exception& e) {
            dbgln("[CLIENT {}] Exception: {}", name_, e.what());
        }
    }

    std::string name_;
    std::string host_;
    std::string port_;
    boost::asio::io_context io_context_;
    std::thread thread_;
    std::unique_ptr<ChatClient> client_;
};

int main() {
    std::atomic<bool> stop_flag(false);
    dbgln("[MAIN] Starting chat application");

    short server_port = 12345;
    std::thread server_thread(run_server, server_port, std::ref(stop_flag));

    // Allow some time for the server to start
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::vector<std::unique_ptr<ClientRunner>> clients;
    clients.push_back(std::make_unique<ClientRunner>("Alice", "localhost", std::to_string(server_port)));
    clients.push_back(std::make_unique<ClientRunner>("Bob", "localhost", std::to_string(server_port)));
    clients.push_back(std::make_unique<ClientRunner>("Charlie", "localhost", std::to_string(server_port)));

    // Start clients
    for (auto& client : clients) {
        dbgln("[MAIN] Starting client: {}", client->name());
        client->start();
    }

    // Wait for clients to connect and log in
    dbgln("[MAIN] Waiting for clients to connect and log in");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Send messages
    dbgln("[MAIN] Sending messages");
    for (auto& client : clients) {
        dbgln("[MAIN] Sending message from client: {}", client->name());
        client->SendMessage.emit("Hello, everyone!");
    }

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Send more messages
    clients[0]->SendMessage.emit("How are you all doing?");
    clients[1]->SendMessage.emit("Great, thanks!");
    clients[2]->SendMessage.emit("Doing well, thanks for asking!");

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Clean up
    dbgln("[MAIN] Closing clients and server");
    stop_flag = true;

    for (auto& client : clients) {
        client->stop();
    }

    server_thread.join();

    dbgln("[MAIN] Main function ending");
    return 0;
}
