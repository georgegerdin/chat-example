// packet.hh
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

enum class PacketType : uint8_t {
    Login,
    CreateUser,
    ChatMessage,
    LoginSuccess,
    LoginFailed,
    AccountCreated,
    AccountExists
};

class Packet {
public:
    virtual ~Packet() = default;

    virtual PacketType getType() const = 0;
    virtual size_t serialize(std::vector<uint8_t>& buffer) const = 0;
    virtual void deserialize(const std::vector<uint8_t>& data) = 0;

    static inline std::vector<uint8_t> preparePacketForSending(const Packet& packet) {
        std::vector<uint8_t> result;
        result.reserve(1024);  // Reserve some space to avoid frequent reallocations

        // Placeholder for packet size
        writeToBuffer(result, static_cast<uint32_t>(0));

        // Serialize the packet
        size_t packetSize = packet.serialize(result);

        // Write the actual packet size at the beginning
        uint32_t size = static_cast<uint32_t>(packetSize);
        std::memcpy(result.data(), &size, sizeof(uint32_t));

        return result;
    }

protected:
    template<typename T>
    static void writeToBuffer(std::vector<uint8_t>& buffer, const T& value) {
        size_t size = sizeof(T);
        size_t currentSize = buffer.size();
        buffer.resize(currentSize + size);
        std::memcpy(buffer.data() + currentSize, &value, size);
    }

    template<typename T>
    static T readFromBuffer(const std::vector<uint8_t>& buffer, size_t& offset) {
        T value;
        std::memcpy(&value, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }

    static void writeString(std::vector<uint8_t>& buffer, const std::string& str) {
        uint32_t length = static_cast<uint32_t>(str.length());
        writeToBuffer(buffer, length);
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    static std::string readString(const std::vector<uint8_t>& buffer, size_t& offset) {
        uint32_t length = readFromBuffer<uint32_t>(buffer, offset);
        std::string str(reinterpret_cast<const char*>(buffer.data() + offset), length);
        offset += length;
        return str;
    }
};

class LoginPacket : public Packet {
public:
    LoginPacket() = default;
    LoginPacket(const std::string& username, const std::string& password)
        : username_(username), password_(password) {}

    PacketType getType() const override { return PacketType::Login; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        writeString(buffer, username_);
        writeString(buffer, password_);
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        size_t offset = 1; // Skip the packet type
        username_ = readString(data, offset);
        password_ = readString(data, offset);
    }

    const std::string& getUsername() const { return username_; }
    const std::string& getPassword() const { return password_; }

private:
    std::string username_;
    std::string password_;
};

class CreateUserPacket : public Packet {
public:
    CreateUserPacket() = default;
    CreateUserPacket(const std::string& username, const std::string& password)
        : username_(username), password_(password) {}

    PacketType getType() const override { return PacketType::CreateUser; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        writeString(buffer, username_);
        writeString(buffer, password_);
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        size_t offset = 1; // Skip the packet type
        username_ = readString(data, offset);
        password_ = readString(data, offset);
    }

    const std::string& getUsername() const { return username_; }
    const std::string& getPassword() const { return password_; }

private:
    std::string username_;
    std::string password_;
};

class ChatMessagePacket : public Packet {
public:
    ChatMessagePacket() = default;
    ChatMessagePacket(const std::string& sender, const std::string& message)
        : sender_(sender), message_(message) {}

    PacketType getType() const override { return PacketType::ChatMessage; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        writeString(buffer, sender_);
        writeString(buffer, message_);
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        size_t offset = 1; // Skip the packet type
        sender_ = readString(data, offset);
        message_ = readString(data, offset);
    }

    const std::string& getSender() const { return sender_; }
    const std::string& getMessage() const { return message_; }

private:
    std::string sender_;
    std::string message_;
};

class LoginSuccessPacket : public Packet {
public:
    LoginSuccessPacket() = default;

    PacketType getType() const override { return PacketType::LoginSuccess; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        // No additional data to deserialize
    }
};

class LoginFailedPacket : public Packet {
public:
    LoginFailedPacket() = default;

    PacketType getType() const override { return PacketType::LoginFailed; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        // No additional data to deserialize
    }
};

class AccountCreatedPacket : public Packet {
public:
    AccountCreatedPacket() = default;

    PacketType getType() const override { return PacketType::AccountCreated; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        // No additional data to deserialize
    }
};

class AccountExistsPacket : public Packet {
public:
    AccountExistsPacket() = default;

    PacketType getType() const override { return PacketType::AccountExists; }

    size_t serialize(std::vector<uint8_t>& buffer) const override {
        size_t initialSize = buffer.size();
        writeToBuffer(buffer, getType());
        return buffer.size() - initialSize;
    }

    void deserialize(const std::vector<uint8_t>& data) override {
        // No additional data to deserialize
    }
};

// Helper function to create a packet from raw data
inline std::unique_ptr<Packet> createPacketFromData(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return nullptr;
    }

    PacketType type = static_cast<PacketType>(data[0]);
    std::unique_ptr<Packet> packet;

    switch (type) {
    case PacketType::Login:
        packet = std::make_unique<LoginPacket>();
        break;
    case PacketType::CreateUser:
        packet = std::make_unique<CreateUserPacket>();
        break;
    case PacketType::ChatMessage:
        packet = std::make_unique<ChatMessagePacket>();
        break;
    case PacketType::LoginSuccess:
        packet = std::make_unique<LoginSuccessPacket>();
        break;
    case PacketType::LoginFailed:
        packet = std::make_unique<LoginFailedPacket>();
        break;
    case PacketType::AccountCreated:
        packet = std::make_unique<AccountCreatedPacket>();
        break;
    case PacketType::AccountExists:
        packet = std::make_unique<AccountExistsPacket>();
        break;
    default:
        return nullptr;
    }

    packet->deserialize(data);
    return packet;
}
