// signal.hh
#pragma once

#include <boost/asio.hpp>
#include <boost/signals2.hpp>

template <typename... Args>
class Signal {
public:
    using SignalType = boost::signals2::signal<void(Args...)>;
    using SlotType = typename SignalType::slot_type;

    void connect(const SlotType& slot, boost::asio::io_service* io_service = nullptr) {
        if (io_service) {
            m_signal.connect([io_service, slot](Args... args) {
                io_service->post([slot, args...]() {
                    slot(args...);
                });
            });
        } else {
            m_signal.connect(slot);
        }
    }

    void connect(const SlotType& slot, std::shared_ptr<boost::asio::io_service> io_service) {
        if (io_service) {
            std::weak_ptr<boost::asio::io_service> weak_io_service = io_service;
            m_signal.connect([weak_io_service, slot](Args... args) {
                if (auto io_service = weak_io_service.lock()) {
                    io_service->post([slot, args...]() {
                        slot(args...);
                    });
                }
            });
        } else {
            m_signal.connect(slot);
        }
    }

    void emit(Args... args) {
        m_signal(args...);
    }

private:
    SignalType m_signal;
};
