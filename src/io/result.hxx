#pragma once

#include <expected>
#include <iostream>
#include <system_error>

namespace fastipc {
namespace io {

template <typename T>
using expected = std::expected<T, std::error_code>;
using unexpected = std::unexpected<std::error_code>;

inline std::error_code errnoCode() noexcept { return std::error_code{errno, std::system_category()}; }

inline io::expected<int> sysVal(int val) noexcept {
    if (val < 0) {
        return io::unexpected{errnoCode()};
    }

    return val;
}

inline io::expected<void*> sysVal(void* val) noexcept {
    if (val == nullptr) {
        return io::unexpected{errnoCode()};
    }

    return val;
}

inline io::expected<void> sysCheck(int val) noexcept {
    if (val < 0) {
        return io::unexpected{errnoCode()};
    }

    return {};
}

} // namespace io

template <typename T>
T expect(std::expected<T, std::error_code>&& expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return std::move(expected.value());
    }

    std::cerr << message << ": " << expected.error().message() << "\n" << std::flush;
    std::abort();
}

inline void expect(std::expected<void, std::error_code>&& expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return;
    }

    std::cerr << message << ": " << expected.error().message() << "\n" << std::flush;
    std::abort();
}

} // namespace fastipc
