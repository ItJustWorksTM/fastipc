/*
 *  endian.hxx
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
