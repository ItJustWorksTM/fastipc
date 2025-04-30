/*
 *  cursor.hxx
 *  Copyright 2025 ItJustWorksTM
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>

namespace fastipc::io {

template <std::size_t n, std::size_t extent>
[[nodiscard]] constexpr std::span<const std::byte, n> takeBuf(std::span<const std::byte, extent>& self) noexcept {
    const auto taken = self.template first<n>();

    self = self.subspan(n);

    return taken;
}

template <std::size_t extent>
[[nodiscard]] constexpr std::span<const std::byte> takeBuf(std::span<const std::byte, extent>& self,
                                                           std::size_t n) noexcept {
    const auto taken = self.first(n);

    self = self.subspan(n);

    return taken;
}

template <typename T, std::size_t extent>
[[nodiscard]] constexpr T getBuf(std::span<const std::byte, extent>& self) noexcept {
    const auto taken = takeBuf<sizeof(T)>(self);

    T value;
    std::memcpy(&value, taken.data(), taken.size());

    return value;
}

template <std::size_t n, std::size_t extent>
constexpr void putBuf(std::span<std::byte, extent>& self, std::span<const std::byte, n> buf) noexcept {
    const auto written = self.first(buf.size());

    std::memcpy(written.data(), buf.data(), buf.size());

    self = self.subspan(buf.size());
}

template <typename T, std::size_t extent>
constexpr void putBuf(std::span<std::byte, extent>& self, T value) noexcept {
    const auto buf = std::span<const std::byte, sizeof(T)>{reinterpret_cast<const std::byte*>(&value), sizeof(T)};

    putBuf(self, buf);
}

} // namespace fastipc::io
