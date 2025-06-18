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