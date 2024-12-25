#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "chat_example/chatserver.h"
#include "chat_example/packet.hh"
#include "chat_example/format.hh"
#include <thread>
#include <future>

class TestClient {
public:
    TestClient(boost::asio::io_context& io_context, short port)
        : socket_(io_context), responses_() {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::address::from_string("127.0.0.1"), port);
        socket_.connect(endpoint);
    }

    void send(const Packet& packet) {
        auto data = Packet::preparePacketForSending(packet);
        boost::asio::write(socket_, boost::asio::buffer(data));
    }

    std::unique_ptr<Packet> receive() {
        uint32_t size;
        boost::asio::read(socket_, boost::asio::buffer(&size, sizeof(uint32_t)));

        std::vector<uint8_t> data(size);
        boost::asio::read(socket_, boost::asio::buffer(data));

        return createPacketFromData(data);
    }

private:
    boost::asio::ip::tcp::socket socket_;
    std::vector<std::unique_ptr<Packet>> responses_;
};

inline const char* packetTypeToString(PacketType type) {
    switch (type) {
    case PacketType::Login:         return "Login";
    case PacketType::CreateUser:    return "CreateUser";
    case PacketType::ChatMessage:   return "ChatMessage";
    case PacketType::LoginSuccess:  return "LoginSuccess";
    case PacketType::LoginFailed:   return "LoginFailed";
    case PacketType::AccountCreated: return "AccountCreated";
    case PacketType::AccountExists: return "AccountExists";
    default:                        return "Unknown";
    }
}

TEST_CASE("ChatServer tests") {
    const short TEST_PORT = 12346;
    boost::asio::io_context io_context;
    ChatServer server(io_context, TEST_PORT);

    std::thread server_thread([&io_context]() {
        io_context.run();
    });

    SUBCASE("User registration and authentication") {
        TestClient client(io_context, TEST_PORT);

        // Test user creation
        client.send(CreateUserPacket("testuser", "testpass"));
        auto response = client.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::AccountCreated);

        // Test duplicate user creation
        client.send(CreateUserPacket("testuser", "testpass"));
        response = client.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::AccountExists);

        // Test login with correct credentials
        client.send(LoginPacket("testuser", "testpass"));
        response = client.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::LoginSuccess);

        // Test login with incorrect password
        client.send(LoginPacket("testuser", "wrongpass"));
        response = client.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::LoginFailed);
    }

    SUBCASE("Message broadcasting") {
        TestClient client1(io_context, TEST_PORT);
        TestClient client2(io_context, TEST_PORT);

        // Register and login users
        client1.send(CreateUserPacket("user1", "pass1"));
        client1.receive(); // Account created
        client1.send(LoginPacket("user1", "pass1"));
        client1.receive(); // Login successful

        client2.receive(); // User 1 has already joined the chat
        client2.send(CreateUserPacket("user2", "pass2"));
        client2.receive();
        client2.send(LoginPacket("user2", "pass2"));
        client2.receive();

        // Test message broadcasting
        client1.send(ChatMessagePacket("user1", "Hello everyone!"));

        auto response = client2.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::ChatMessage);

        auto chat_msg = static_cast<ChatMessagePacket*>(response.get());
        CHECK(chat_msg->getSender() == "user1");
        CHECK(chat_msg->getMessage() == "Hello everyone!");
    }

    SUBCASE("Multiple client handling") {
        std::vector<std::unique_ptr<TestClient>> clients;
        const int NUM_CLIENTS = 5;

        // Create and connect multiple clients
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            clients.push_back(std::make_unique<TestClient>(io_context, TEST_PORT));
            std::string username = "user" + std::to_string(i);
            std::string password = "pass" + std::to_string(i);

            clients[i]->send(CreateUserPacket(username, password));
            clients[i]->receive();

            clients[i]->send(LoginPacket(username, password));
            clients[i]->receive();
        }

        // Send message from first client
        clients[0]->send(ChatMessagePacket("user0", "Broadcast test"));

        // Verify all other clients receive the message
        for (int i = 1; i < NUM_CLIENTS; ++i) {
            auto response = clients[i]->receive();
            REQUIRE(response != nullptr);
            CHECK(response->getType() == PacketType::ChatMessage);

            auto chat_msg = static_cast<ChatMessagePacket*>(response.get());
            auto sender_name = chat_msg->getSender();
            if(sender_name != "System") {
                CHECK(chat_msg->getSender() == "user0");
                CHECK(chat_msg->getMessage() == "Broadcast test");
            }
        }
    }

    // Clean up
    io_context.stop();
    server_thread.join();
}

TEST_CASE("ChatServer error handling") {
    const short TEST_PORT = 12347;
    boost::asio::io_context io_context;
    ChatServer server(io_context, TEST_PORT);

    std::thread server_thread([&io_context]() {
        io_context.run();
    });

    SUBCASE("Invalid packet handling") {
        TestClient client(io_context, TEST_PORT);

        // Test sending chat message without login
        client.send(ChatMessagePacket("unknown", "test message"));
        auto response = client.receive();
        REQUIRE(response != nullptr);
        CHECK(response->getType() == PacketType::LoginFailed);
    }

    // Clean up
    io_context.stop();
    server_thread.join();
}
