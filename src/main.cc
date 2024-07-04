//main.cc
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <boost/asio.hpp>
#include "chatserver.h"
#include "chatclient.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "format.hh"
#include "signal.hh"


void run_server(short port, std::atomic<bool>& stop_flag) {
    try {
        dbgln("[MAIN] Starting server thread");
        boost::asio::io_context io_context;
        ChatServer server(io_context, port);
        dbgln("[MAIN] Server thread started");

        std::thread io_thread([&io_context]() {
            dbgln("[MAIN] Server IO thread started");
            io_context.run();
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
        : name_(name), host_(host), port_(port), stop_flag_(false) {}

    void start() {
        thread_ = std::thread(&ClientRunner::run, this);
    }

    void stop() {
        stop_flag_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    Signal<const std::string&> on_message_received;
    Signal<> on_disconnected;
    Signal<> on_connected;
    Signal<> on_login_success;
    Signal<> on_login_failure;
    Signal<> on_account_created;
    Signal<const std::string&> on_send_message;

    void send_message(const std::string& message) {
        on_send_message.emit(message);
    }

private:
    void run() {
        try {
            bool needs_account = true;
            while (!stop_flag_) {
                dbgln("[CLIENT] {}: Attempting to connect to server...", name_);
                boost::asio::io_context io_context;
                on_send_message.connect([this](const std::string& message) {
                    message_queue_.push(message);
                }, &io_context);
                ChatClient client(io_context, name_);

                client.set_disconnected_callback([this]() {
                    on_disconnected.emit();
                    dbgln("[CLIENT] {}: Disconnected from server", name_);
                });


                if (!client.connect(host_, port_)) {
                    dbgln("[CLIENT] {}: Failed to connect. Retrying in 2 seconds...", name_);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }

                on_connected.emit();
                dbgln("[CLIENT] {}: Connected to server.", name_);

                std::thread io_thread([&io_context, this]() {
                    dbgln("[CLIENT] {}: IO thread started", name_);
                    io_context.run();
                });

                std::string username = name_;
                std::string password = "password123";

                if (needs_account) {
                    dbgln("[CLIENT] {}: Attempting to create a new account.", name_);
                    bool create_success = client.create_user(username, password);
                    if (create_success) {
                        on_account_created.emit();
                        dbgln("[CLIENT] {}: Account created. Disconnecting to log in.", name_);
                        needs_account = false;
                    } else {
                        dbgln("[CLIENT] {}: Failed to create an account. Assuming it already exists.", name_);
                        needs_account = false;
                    }
                } else {
                    dbgln("[CLIENT] {}: Attempting to log in.", name_);
                    bool login_success = client.login(username, password);
                    if (login_success) {
                        on_login_success.emit();
                        dbgln("[CLIENT] {}: Successfully logged in.", name_);

                        while (!stop_flag_ && client.is_open()) {
                            std::string message;
                            if (try_pop_message(message)) {
                                dbgln("[CLIENT] {}: Sending message: {}", name_, message);
                                client.queue_message(message);
                            }
                            client.process_write_queue();
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    } else {
                        on_login_failure.emit();
                        dbgln("[CLIENT] {}: Login failed.", name_);
                    }
                }

                client.close();
                io_context.stop();
                io_thread.join();

                if (!stop_flag_) {
                    dbgln("[CLIENT] {}: Disconnected. Reconnecting in 2 seconds...", name_);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }
        }
        catch (std::exception& e) {
            dbgln("[CLIENT] {} exception: {}", name_, e.what());
        }
        dbgln("[CLIENT] {} thread ended", name_);
    }

    bool try_pop_message(std::string& message) {
        if (message_queue_.empty()) {
            return false;
        }
        message = std::move(message_queue_.front());
        message_queue_.pop();
        return true;
    }

    std::string name_;
    std::string host_;
    std::string port_;
    std::atomic<bool> stop_flag_;
    std::thread thread_;
    std::queue<std::string> message_queue_;
};

int main() {
    std::atomic<bool> stop_flag(false);
    dbgln("[MAIN] Starting chat application");
    std::thread server_thread(run_server, 12345, std::ref(stop_flag));

    // Allow some time for the server to start
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ClientRunner alice("Alice", "localhost", "12345");
    ClientRunner bob("Bob", "localhost", "12345");
    ClientRunner charlie("Charlie", "localhost", "12345");

    // Set up signal handlers
    alice.on_message_received.connect([](const std::string& msg) {
        dbgln("[MAIN] Alice received: {}", msg);
    });
    bob.on_message_received.connect([](const std::string& msg) {
        dbgln("[MAIN] Bob received: {}", msg);
    });
    charlie.on_message_received.connect([](const std::string& msg) {
        dbgln("[MAIN] Charlie received: {}", msg);
    });

    // Start clients
    alice.start();
    bob.start();
    charlie.start();

    // Wait for clients to connect and log in
    dbgln("[MAIN] Waiting for clients to connect and log in");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Send messages
    dbgln("[MAIN] Sending messages");
    alice.send_message("Hello, everyone!\n");
    bob.send_message("Hi Alice!\n");
    charlie.send_message("Hey there!\n");

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Send more messages
    alice.send_message("How are you all doing?\n");
    bob.send_message("Great, thanks!\n");
    charlie.send_message("Doing well, thanks for asking!\n");

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Clean up
    dbgln("[MAIN] Closing clients and server");
    stop_flag = true;

    alice.stop();
    bob.stop();
    charlie.stop();

    server_thread.join();

    dbgln("[MAIN] Main function ending");
    return 0;
}
