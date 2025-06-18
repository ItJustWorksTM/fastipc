/*
 *  scheduler.hxx
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
#include <exception>
#include <print>
#include <utility>
#include <variant>
#include "visitor.hxx"

namespace fastipc::co {

template <class T>
struct Received final {

    void set_value(T value) { m_value = std::move(value); }
    void set_exception(std::exception_ptr exc) { m_value = std::move(exc); }

    [[nodiscard]] bool has_value() const { return !std::holds_alternative<std::monostate>(m_value); }

    [[nodiscard]] T consume() && {
        return match(
            std::move(m_value), [](std::exception_ptr exc) -> T { std::rethrow_exception(std::move(exc)); },
            [](T&& value) { return std::move(value); },
            [](std::monostate) -> T {
                assert(false);
                std::unreachable();
            });
    }

    template <class R>
    void forward(R& receiver) && {
        match(
            std::move(m_value), [&receiver](std::exception_ptr exc) { receiver.set_exception(std::move(exc)); },
            [&receiver](T&& value) { receiver.set_value(std::move(value)); },
            [](std::monostate) {
                assert(false);
                std::unreachable();
            });
    }

    std::variant<std::monostate, T, std::exception_ptr> m_value;
};

template <>
struct Received<void> final {

    void set_value() { m_value = has_value_tag{}; }
    void set_exception(std::exception_ptr exc) { m_value = std::move(exc); }

    [[nodiscard]] bool has_value() const { return !std::holds_alternative<std::monostate>(m_value); }

    void consume() && {
        match(
            std::move(m_value), [](std::exception_ptr exc) -> void { std::rethrow_exception(std::move(exc)); },
            [](has_value_tag) {},
            [](std::monostate) {
                assert(false);
                std::unreachable();
            });
    }

    template <class R>
    void forward(R& receiver) && {
        return match(
            std::move(m_value), [&](std::exception_ptr exc) { receiver.set_exception(std::move(exc)); },
            [&](has_value_tag) { receiver.set_value(); },
            [](std::monostate) {
                assert(false);
                std::unreachable();
            });
    }

    struct has_value_tag final {};

    std::variant<std::monostate, has_value_tag, std::exception_ptr> m_value;
};

} // namespace fastipc::co