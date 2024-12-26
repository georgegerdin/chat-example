#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "scope_exit.hh"
#include "chat_example/chatclient.h"
#include "chat_example/chatserver.h"
#include "chat_example/format.hh"
#include <chrono>

enum class WaitFlag {
    Connected,
    LoginResult,
    AccountCreated,
    MessageReceived
};

class TestChatClient {
private:
    boost::asio::io_context io_context_;
    std::unique_ptr<ChatClient> client_;
    bool connected_ = false;
    bool login_result_ = false;
    bool account_created_ = false;
    std::string received_message_;
    std::string message_sender_;

public:
    TestChatClient() : client_(std::make_unique<ChatClient>("TestUser")) {
        // Connect signals with proper io_context binding
        client_->on_connected.connect([this]() {
            connected_ = true;
        }, &io_context_);

        client_->on_login_response.connect([this](bool success) {
            login_result_ = success;
        }, &io_context_);

        client_->on_create_account_response.connect([this](bool success) {
            account_created_ = success;
        }, &io_context_);

        client_->on_message_received.connect([this](const std::string& sender, const std::string& message) {
            message_sender_ = sender;
            received_message_ = message;
        }, &io_context_);

        client_->start();
    }

    ~TestChatClient() {
        client_->stop();
    }

    void reset_flags() {
        connected_ = false;
        login_result_ = false;
        account_created_ = false;
        received_message_.clear();
        message_sender_.clear();
    }

    bool wait_for_flag(WaitFlag flag, int timeout_ms = 1000) {
        auto start = std::chrono::steady_clock::now();

        while (!check_flag(flag)) {
            // Run the io_context for a short duration to process events
            io_context_.run_for(std::chrono::milliseconds(10));

            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count() > timeout_ms) {
                return false;
            }

            // Reset the io_context if it has no more work
            if (io_context_.stopped()) {
                io_context_.restart();
            }
        }

        return check_flag(flag);
    }

    bool check_flag(WaitFlag flag) const {
        switch (flag) {
        case WaitFlag::Connected:
            return connected_;
        case WaitFlag::LoginResult:
            return login_result_;
        case WaitFlag::AccountCreated:
            return account_created_;
        case WaitFlag::MessageReceived:
            return !received_message_.empty();
        default:
            return false;
        }
    }

    ChatClient* get_client() { return client_.get(); }
    bool is_connected() const { return connected_; }
    bool get_login_result() const { return login_result_; }
    bool get_account_created() const { return account_created_; }
    const std::string& get_received_message() const { return received_message_; }
    const std::string& get_message_sender() const { return message_sender_; }
};

TEST_CASE("ChatClient basic functionality") {
    const short TEST_PORT = 12345;
    boost::asio::io_context io_context;
    ChatServer server(io_context, TEST_PORT);

    std::thread server_thread([&io_context]() {
        io_context.run();
    });

    SCOPE_EXIT ({
        io_context.stop();
        server_thread.join();
    });

    TestChatClient test_client;

    SUBCASE("Client initialization") {
        CHECK_FALSE(test_client.get_client()->is_logged_in());
    }

    SUBCASE("Connection handling") {
        test_client.get_client()->Connect.emit("localhost", "12345");
        CHECK(test_client.wait_for_flag(WaitFlag::Connected));
    }

    SUBCASE("Account creation") {
        test_client.reset_flags();
        test_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(test_client.wait_for_flag(WaitFlag::Connected));

        test_client.get_client()->CreateUser.emit("newuser", "password");
        CHECK(test_client.wait_for_flag(WaitFlag::AccountCreated));
    }

    SUBCASE("Login handling") {
        test_client.reset_flags();
        test_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(test_client.wait_for_flag(WaitFlag::Connected));

        test_client.get_client()->CreateUser.emit("testuser", "password");
        CHECK(test_client.wait_for_flag(WaitFlag::AccountCreated));

        test_client.get_client()->Login.emit("testuser", "password");
        CHECK(test_client.wait_for_flag(WaitFlag::LoginResult));
        CHECK(test_client.get_client()->is_logged_in());
    }


    SUBCASE("Message sending and receiving") {
        // Set up first client (sender)
        test_client.reset_flags();
        test_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(test_client.wait_for_flag(WaitFlag::Connected));

        test_client.get_client()->CreateUser.emit("sender", "password");
        CHECK(test_client.wait_for_flag(WaitFlag::AccountCreated));

        test_client.get_client()->Login.emit("sender", "password");
        REQUIRE(test_client.wait_for_flag(WaitFlag::LoginResult));

        // Set up second client (receiver)
        TestChatClient receiver_client;
        receiver_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(receiver_client.wait_for_flag(WaitFlag::Connected));

        receiver_client.get_client()->CreateUser.emit("receiver", "password");
        CHECK(receiver_client.wait_for_flag(WaitFlag::AccountCreated));

        receiver_client.get_client()->Login.emit("receiver", "password");
        REQUIRE(receiver_client.wait_for_flag(WaitFlag::LoginResult));

        // Wait for join messages to be processed
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Send and verify message
        const std::string test_message = "Hello, receiver!";
        test_client.get_client()->SendMessage.emit(test_message);

        CHECK(receiver_client.wait_for_flag(WaitFlag::MessageReceived));
        CHECK(receiver_client.get_received_message() == test_message);
        CHECK(receiver_client.get_message_sender() == "sender");
    }
}

TEST_CASE("ChatClient error handling") {
    const short TEST_PORT = 12345;
    boost::asio::io_context io_context;
    ChatServer server(io_context, TEST_PORT);

    std::thread server_thread([&io_context]() {
        io_context.run();
    });

    SCOPE_EXIT ({
        io_context.stop();
        server_thread.join();
    });

    TestChatClient test_client;

    SUBCASE("Invalid login") {
        test_client.reset_flags();
        test_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(test_client.wait_for_flag(WaitFlag::Connected));

        test_client.get_client()->Login.emit("invalid", "wrong");
        CHECK_FALSE(test_client.wait_for_flag(WaitFlag::LoginResult));
        CHECK_FALSE(test_client.get_client()->is_logged_in());
    }

    SUBCASE("Message sending when not logged in") {
        test_client.reset_flags();
        test_client.get_client()->Connect.emit("localhost", "12345");
        REQUIRE(test_client.wait_for_flag(WaitFlag::Connected));

        test_client.get_client()->SendMessage.emit("Should not send");
        CHECK_FALSE(test_client.wait_for_flag(WaitFlag::MessageReceived));
        CHECK(test_client.get_received_message().empty());
    }
}
