#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "chat_example/packet.hh"

TEST_CASE("Packet serialization and deserialization") {
    SUBCASE("LoginPacket") {
        LoginPacket original("testuser", "testpass");
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        REQUIRE(packet->getType() == PacketType::Login);

        auto login_packet = static_cast<LoginPacket*>(packet.get());
        CHECK(login_packet->getUsername() == "testuser");
        CHECK(login_packet->getPassword() == "testpass");
    }

    SUBCASE("ChatMessagePacket") {
        ChatMessagePacket original("sender", "Hello, world!");
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        REQUIRE(packet->getType() == PacketType::ChatMessage);

        auto chat_packet = static_cast<ChatMessagePacket*>(packet.get());
        CHECK(chat_packet->getSender() == "sender");
        CHECK(chat_packet->getMessage() == "Hello, world!");
    }

    SUBCASE("CreateUserPacket") {
        CreateUserPacket original("newuser", "newpass");
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        REQUIRE(packet->getType() == PacketType::CreateUser);

        auto create_packet = static_cast<CreateUserPacket*>(packet.get());
        CHECK(create_packet->getUsername() == "newuser");
        CHECK(create_packet->getPassword() == "newpass");
    }
}

TEST_CASE("Status packets") {
    SUBCASE("LoginSuccessPacket") {
        LoginSuccessPacket original;
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        CHECK(packet->getType() == PacketType::LoginSuccess);
    }

    SUBCASE("LoginFailedPacket") {
        LoginFailedPacket original;
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        CHECK(packet->getType() == PacketType::LoginFailed);
    }

    SUBCASE("AccountCreatedPacket") {
        AccountCreatedPacket original;
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);
        CHECK(packet->getType() == PacketType::AccountCreated);
    }
}

TEST_CASE("Edge cases") {
    SUBCASE("Empty strings") {
        ChatMessagePacket original("", "");
        std::vector<uint8_t> buffer = Packet::preparePacketForSending(original);

        auto packet = createPacketFromData(std::vector<uint8_t>(buffer.begin() + 4, buffer.end()));
        REQUIRE(packet != nullptr);

        auto chat_packet = static_cast<ChatMessagePacket*>(packet.get());
        CHECK(chat_packet->getSender().empty());
        CHECK(chat_packet->getMessage().empty());
    }

    SUBCASE("Invalid packet data") {
        std::vector<uint8_t> invalid_data{255};  // Invalid packet type
        auto packet = createPacketFromData(invalid_data);
        CHECK(packet == nullptr);
    }

    SUBCASE("Empty packet data") {
        std::vector<uint8_t> empty_data;
        auto packet = createPacketFromData(empty_data);
        CHECK(packet == nullptr);
    }
}
