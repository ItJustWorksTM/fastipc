/*
 *  result.hxx
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

#include <concepts>
#include <expected>
#include <print>
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

    std::println(stderr, "{}: {}", message, expected.error().message());
    std::abort();
}

inline void expect(std::expected<void, std::error_code> expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return;
    }

    std::println(stderr, "{}: {}", message, expected.error().message());
    std::abort();
}

template <typename T>
[[nodiscard]] T expect(std::optional<T> expected, std::string_view message = "unexpected") noexcept {
    if (expected.has_value()) {
        return std::move(expected.value());
    }

    std::println(stderr, "{}", message);
    std::abort();
}

} // namespace fastipc
