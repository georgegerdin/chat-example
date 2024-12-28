#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>

// Forward declarations
class ChatMessage;
class UserCredentials;

// Callback types for async operations
using AuthCallback = std::function<void(bool)>;
using StoreMessageCallback = std::function<void(bool)>;
using GetMessagesCallback = std::function<void(const std::vector<ChatMessage>&)>;

struct ChatMessage {
    std::string sender;
    std::string content;
    std::chrono::system_clock::time_point timestamp;

    ChatMessage(const std::string& s, const std::string& c)
        : sender(s), content(c), timestamp(std::chrono::system_clock::now()) {}
};

class DatabaseAdapter {
public:
    virtual ~DatabaseAdapter() = default;

    // User authentication and management
    virtual void authenticateUser(const std::string& username,
                                  const std::string& password,
                                  AuthCallback callback) = 0;

    virtual void createUser(const std::string& username,
                            const std::string& password,
                            AuthCallback callback) = 0;

    // Message operations
    virtual void storeMessage(const ChatMessage& message,
                              StoreMessageCallback callback) = 0;

    virtual void getRecentMessages(size_t limit,
                                   GetMessagesCallback callback) = 0;

    // Optional: Get messages within a time range
    virtual void getMessagesByTimeRange(std::chrono::system_clock::time_point start,
                                        std::chrono::system_clock::time_point end,
                                        GetMessagesCallback callback) = 0;

protected:
    // Utility method to post callbacks to io_context
    template<typename Callback, typename... Args>
    void postCallback(boost::asio::io_context& io_context,
                      Callback callback,
                      Args... args) {
        io_context.post([callback, args...]() {
            callback(args...);
        });
    }
};

// In-memory implementation for testing
class InMemoryDatabaseAdapter : public DatabaseAdapter {
public:
    InMemoryDatabaseAdapter(boost::asio::io_context& io_context)
        : io_context_(io_context) {}

    void authenticateUser(const std::string& username,
                          const std::string& password,
                          AuthCallback callback) override {
        auto it = users_.find(username);
        bool success = (it != users_.end() && it->second == password);
        postCallback(io_context_, callback, success);
    }

    void createUser(const std::string& username,
                    const std::string& password,
                    AuthCallback callback) override {
        bool success = users_.find(username) == users_.end();
        if (success) {
            users_[username] = password;
        }
        postCallback(io_context_, callback, success);
    }

    void storeMessage(const ChatMessage& message,
                      StoreMessageCallback callback) override {
        messages_.push_back(message);
        postCallback(io_context_, callback, true);
    }

    void getRecentMessages(size_t limit,
                           GetMessagesCallback callback) override {
        std::vector<ChatMessage> recent;
        auto start = messages_.size() > limit ?
                         messages_.end() - limit :
                         messages_.begin();
        recent.assign(start, messages_.end());
        postCallback(io_context_, callback, recent);
    }

    void getMessagesByTimeRange(std::chrono::system_clock::time_point start,
                                std::chrono::system_clock::time_point end,
                                GetMessagesCallback callback) override {
        std::vector<ChatMessage> filtered;
        std::copy_if(messages_.begin(), messages_.end(),
                     std::back_inserter(filtered),
                     [&](const ChatMessage& msg) {
                         return msg.timestamp >= start && msg.timestamp <= end;
                     });
        postCallback(io_context_, callback, filtered);
    }

private:
    boost::asio::io_context& io_context_;
    std::unordered_map<std::string, std::string> users_;
    std::vector<ChatMessage> messages_;
};
