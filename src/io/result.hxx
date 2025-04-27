#pragma once

#include <concepts>
#include <expected>
#include <iostream>
#include <system_error>

namespace fastipc {
namespace io {

template <typename T>
using expected = std::expected<T, std::error_code>;
using unexpected = std::unexpected<std::error_code>;

[[nodiscard]] inline std::error_code errnoCode() noexcept { return std::error_code{errno, std::system_category()}; }

template <std::integral T>
    requires std::is_signed_v<T>
[[nodiscard]] io::expected<int> sysVal(T val) noexcept {
    if (val < 0) {
        return io::unexpected{errnoCode()};
    }

    return val;
}

[[nodiscard]] inline io::expected<void*> sysVal(void* val) noexcept {
    if (val == nullptr) {
        return io::unexpected{errnoCode()};
    }

    return val;
}

template <std::integral T>
    requires std::is_signed_v<T>
[[nodiscard]] io::expected<void> sysCheck(T val) noexcept {
    if (val < 0) {
        return io::unexpected{errnoCode()};
    }

    return {};
}

} // namespace io

template <typename T>
[[nodiscard]] T expect(std::expected<T, std::error_code> expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return std::move(expected.value());
    }

    std::cerr << message << ": " << expected.error().message() << "\n" << std::flush;
    std::abort();
}

inline void expect(std::expected<void, std::error_code> expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return;
    }

    std::cerr << message << ": " << expected.error().message() << "\n" << std::flush;
    std::abort();
}

} // namespace fastipc
