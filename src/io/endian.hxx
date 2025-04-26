#pragma once

#include <bit>

namespace fastipc::io {

template <std::integral T>
[[nodiscard]] constexpr T fromBe(T value) noexcept {
    return std::endian::native == std::endian::little ? std::byteswap(value) : value;
}

template <std::integral T>
[[nodiscard]] constexpr T toBe(T value) noexcept {
    return std::endian::native == std::endian::little ? std::byteswap(value) : value;
}

template <std::integral T>
[[nodiscard]] constexpr T fromLe(T value) noexcept {
    return std::endian::native == std::endian::big ? std::byteswap(value) : value;
}

template <std::integral T>
[[nodiscard]] constexpr T toLe(T value) noexcept {
    return std::endian::native == std::endian::big ? std::byteswap(value) : value;
}

} // namespace fastipc::io
