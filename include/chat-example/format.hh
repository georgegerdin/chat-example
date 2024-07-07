// format.hh
#pragma once
#include <string>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <mutex>

template<typename T>
std::string to_string_custom(const T& t) {
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

// Base case for recursion
inline void format_helper(std::string& result, const std::string& fmt, size_t& fmt_index) {
    while (fmt_index < fmt.size()) {
        if (fmt[fmt_index] == '{' && fmt_index + 1 < fmt.size() && fmt[fmt_index + 1] == '}') {
            throw std::runtime_error("Too few arguments provided to format string");
        }
        result += fmt[fmt_index++];
    }
}

// Recursive variadic template function
template<typename T, typename... Args>
void format_helper(std::string& result, const std::string& fmt, size_t& fmt_index, const T& value, const Args&... args) {
    while (fmt_index < fmt.size()) {
        if (fmt[fmt_index] == '{' && fmt_index + 1 < fmt.size() && fmt[fmt_index + 1] == '}') {
            result += to_string_custom(value);
            fmt_index += 2;
            format_helper(result, fmt, fmt_index, args...);
            return;
        }
        result += fmt[fmt_index++];
    }
    throw std::runtime_error("Too many arguments provided to format string");
}

template<typename... Args>
std::string format(const char* fmt, const Args&... args) {
    std::string result;
    size_t fmt_index = 0;
    format_helper(result, fmt, fmt_index, args...);
    return result;
}

// Global mutex for synchronizing cout access
extern std::mutex cout_mutex;

template<typename... Args>
void dbgln(const char* fmt, const Args&... args) {
    // Format the string
    std::string formatted_str = format(fmt, args...);

    // Lock the mutex before accessing cout
    std::lock_guard<std::mutex> lock(cout_mutex);

    // Print the formatted string and add a newline
    std::cout << formatted_str << std::endl;
}
