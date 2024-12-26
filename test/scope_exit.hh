#pragma once

#include <functional>

class ScopeExitGuard {
public:
    ScopeExitGuard(std::function<void()> onExit) : onExit_(onExit), active_(true) {}

    ~ScopeExitGuard() {
        if (active_) {
            onExit_();
        }
    }

    // Disable copy
    ScopeExitGuard(const ScopeExitGuard&) = delete;
    ScopeExitGuard& operator=(const ScopeExitGuard&) = delete;

    // Enable move
    ScopeExitGuard(ScopeExitGuard&& other) noexcept : onExit_(std::move(other.onExit_)), active_(other.active_) {
        other.active_ = false;
    }

    ScopeExitGuard& operator=(ScopeExitGuard&& other) noexcept {
        if (this != &other) {
            onExit_ = std::move(other.onExit_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    void dismiss() noexcept {
        active_ = false;
    }

private:
    std::function<void()> onExit_;
    bool active_;
};

// Helper function to create a ScopeExitGuard
ScopeExitGuard makeScopeExitGuard(std::function<void()> onExit) {
    return ScopeExitGuard(onExit);
}

// Define the SCOPE_EXIT macro
#define CONCATENATE_DETAIL(x, y) x##y
#define CONCATENATE(x, y) CONCATENATE_DETAIL(x, y)
#define SCOPE_EXIT(code) auto CONCATENATE(scope_exit_, __COUNTER__) = makeScopeExitGuard([&]() { code; })

