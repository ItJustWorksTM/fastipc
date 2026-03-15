/*
 *  visitor.hxx
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

#include <utility>
#include <variant>

namespace fastipc {

template <class... Ts>
struct Visitor final : Ts... {
    using Ts::operator()...;
};

template <class V, class... Ts>
decltype(auto) match(V&& variant, Ts&&... arms) {
    return std::visit(Visitor{std::forward<Ts>(arms)...}, std::forward<V>(variant));
}

} // namespace fastipc
