#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fastipc {
namespace io {

template <std::size_t n, std::size_t extent>
constexpr std::span<const std::uint8_t, n> takeBuf(std::span<const std::uint8_t, extent>& self) noexcept {
    const auto taken = self.template first<n>();

    self = self.subspan(n);

    return taken;
}

template <std::size_t extent>
constexpr std::span<const std::uint8_t> takeBuf(std::span<const std::uint8_t, extent>& self, std::size_t n) noexcept {
    const auto taken = self.first(n);

    self = self.subspan(n);

    return taken;
}

template <typename T, std::size_t extent>
constexpr T getBuf(std::span<const std::uint8_t, extent>& self) noexcept {
    const auto taken = takeBuf<sizeof(T)>(self);

    T value;
    std::memcpy(&value, taken.data(), taken.size());

    return value;
}

template <std::size_t n, std::size_t extent>
constexpr void putBuf(std::span<std::uint8_t, extent>& self, std::span<const std::uint8_t, n> buf) noexcept {
    const auto written = self.first(buf.size());

    std::memcpy(written.data(), buf.data(), buf.size());

    self = self.subspan(buf.size());
}

template <typename T, std::size_t extent>
constexpr void putBuf(std::span<std::uint8_t, extent>& self, T value) noexcept {
    const auto buf = std::span<const std::uint8_t, sizeof(T)>{reinterpret_cast<const std::uint8_t*>(&value), sizeof(T)};

    putBuf(self, buf);
}

} // namespace io
} // namespace fastipc
