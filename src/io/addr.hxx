/*
 *  addr.hxx
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

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <print>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>
#include "endian.hxx"

#include "result.hxx"

namespace fastipc::io {

struct Ipv4Addr final {
    constexpr static std::size_t kSegments8 = 4;
    using value_type = std::array<std::uint8_t, kSegments8>;

    value_type value;

    constexpr static Ipv4Addr from(value_type value) noexcept { return {.value = value}; }
    constexpr static Ipv4Addr from(::in_addr addr) noexcept { return from(std::bit_cast<value_type>(addr.s_addr)); }
};

struct SocketAddrV4 final {
    Ipv4Addr addr;
    std::uint16_t port;

    constexpr static SocketAddrV4 from(const ::sockaddr_in& sockaddr) noexcept {
        return {.addr = Ipv4Addr::from(sockaddr.sin_addr), .port = sockaddr.sin_port};
    }
};

struct Ipv6Addr final {
    constexpr static std::size_t kSegments8 = 16;
    constexpr static std::size_t kSegments16 = 8;
    using value_type = std::array<std::uint8_t, kSegments8>;

    value_type value;

    constexpr static Ipv6Addr from(value_type value) noexcept { return {.value = value}; }
    constexpr static Ipv6Addr from(::in6_addr addr) noexcept { return from(std::bit_cast<value_type>(addr.s6_addr)); }

    [[nodiscard]] constexpr std::array<std::uint16_t, kSegments16> segments() const noexcept {
        auto vs = std::bit_cast<std::array<std::uint16_t, kSegments16>>(value);

        for (auto& v : vs)
            v = fromBe(v);

        return vs;
    }
};

struct SocketAddrV6 final {
    Ipv6Addr addr;
    std::uint16_t port;
    std::uint32_t flowinfo;
    std::uint32_t scope_id;

    constexpr static SocketAddrV6 from(const ::sockaddr_in6& sockaddr) noexcept {
        return {.addr = Ipv6Addr::from(sockaddr.sin6_addr),
                .port = sockaddr.sin6_port,
                .flowinfo = sockaddr.sin6_flowinfo,
                .scope_id = sockaddr.sin6_scope_id};
    }
};

using IpAddr = std::variant<Ipv4Addr, Ipv6Addr>;
using SocketAddr = std::variant<SocketAddrV4, SocketAddrV6>;

expected<std::unordered_map<std::string, std::vector<SocketAddr>>> getInterfaceAddresses() noexcept;

} // namespace fastipc::io

template <>
class std::formatter<fastipc::io::Ipv4Addr> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::Ipv4Addr& self, auto& ctx) const {
        return std::format_to(ctx.out(), "{}.{}.{}.{}", self.value[0], self.value[1], self.value[2], self.value[3]);
    }
};

template <>
class std::formatter<fastipc::io::Ipv6Addr> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::Ipv6Addr& self, auto& ctx) const {
        const auto segments = self.segments();

        const auto zeroes = parts(segments);

        auto out = ctx.out();

        if (zeroes.size > 1) {
            out = formatChunk(out, std::span{segments}.first(zeroes.start));
            out = std::format_to(out, "::");
            out = formatChunk(out, std::span{segments}.subspan(zeroes.start + zeroes.size));

        } else {
            out = formatChunk(out, segments);
        }

        return out;
    }

  private:
    auto formatChunk(auto out, std::span<const std::uint16_t> chunk) const {
        if (!chunk.empty()) {
            out = std::format_to(out, "{:x}", chunk[0]);

            for (const auto segment : chunk.subspan(1)) {
                out = std::format_to(out, ":{:x}", segment);
            }
        }

        return out;
    };

    struct Span {
        std::size_t start;
        std::size_t size;
    };

    [[nodiscard]] constexpr static Span parts(std::span<const std::uint16_t> segments) noexcept {

        Span longest{};
        Span current{};

        for (std::size_t i = 0; i < segments.size(); ++i) {
            if (segments[i] == 0) {
                if (current.size == 0) {
                    current.start = i;
                }

                current.size += 1;

                if (current.size > longest.size) {
                    longest = current;
                }
            } else {
                current = Span{};
            }
        }

        return longest;
    }
};

template <>
class std::formatter<fastipc::io::IpAddr> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::IpAddr& self, auto& ctx) const {
        return std::visit([&](const auto& v) { return std::format_to(ctx.out(), "{}", v); }, self);
    }
};

template <>
class std::formatter<fastipc::io::SocketAddrV4> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::SocketAddrV4& self, auto& ctx) const {
        return std::format_to(ctx.out(), "{}:{}", self.addr, self.port);
    }
};

template <>
class std::formatter<fastipc::io::SocketAddrV6> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::SocketAddrV6& self, auto& ctx) const {
        if (self.scope_id == 0) {
            return std::format_to(ctx.out(), "[{}]:{}", self.addr, self.scope_id, self.port);
        } else {
            return std::format_to(ctx.out(), "[{}%{}]:{}", self.addr, self.scope_id, self.port);
        }
    }
};

template <>
class std::formatter<fastipc::io::SocketAddr> {
  public:
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const fastipc::io::SocketAddr& self, auto& ctx) const {
        return std::visit([&](const auto& v) { return std::format_to(ctx.out(), "{}", v); }, self);
    }
};
