// signal.hh
#pragma once

#include <boost/asio.hpp>
#include <functional>
#include <optional>
#include <mutex>

template <typename... Args>
class Signal {
public:
    using SlotType = std::function<void(Args...)>;

    void connect(const SlotType& slot, boost::asio::io_service* io_service = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (io_service) {
            m_slot = [io_service, slot](Args... args) {
                io_service->post([slot, args...]() {
                    slot(args...);
                });
            };
        } else {
            m_slot = slot;
        }
    }

    void connect(const SlotType& slot, std::shared_ptr<boost::asio::io_service> io_service) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (io_service) {
            std::weak_ptr<boost::asio::io_service> weak_io_service = io_service;
            m_slot = [weak_io_service, slot](Args... args) {
                if (auto io_service = weak_io_service.lock()) {
                    io_service->post([slot, args...]() {
                        slot(args...);
                    });
                }
            };
        } else {
            m_slot = slot;
        }
    }

    void emit(Args... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_slot) {
            (*m_slot)(args...);
        }
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slot.reset();
    }

    bool connected() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slot.has_value();
    }

private:
    std::optional<SlotType> m_slot;
    mutable std::mutex m_mutex;
};
