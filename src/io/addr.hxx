#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <print>
#include <variant>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>

#include "result.hxx"

namespace fastipc::io {

struct Ipv4Addr final {
    using value_type = std::array<std::uint8_t, 4>;

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
    constexpr static std::size_t kSegments = 8;
    using value_type = std::array<std::uint16_t, kSegments>;

    value_type value;

    constexpr static Ipv6Addr from(value_type value) noexcept { return {.value = value}; }
    constexpr static Ipv6Addr from(::in6_addr addr) noexcept {
        return from(*reinterpret_cast<const value_type*>(&addr.s6_addr32));
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

std::string getAddrName(const Ipv4Addr& addr) noexcept;
std::string getAddrName(const Ipv6Addr& addr) noexcept;

expected<std::vector<SocketAddr>> getInterfaceAddresses() noexcept;

} // namespace fastipc::io

template <>
class std::formatter<fastipc::io::Ipv4Addr> : public std::formatter<std::string_view> {
  public:
    template <class FmtContext>
    FmtContext::iterator format(const fastipc::io::Ipv4Addr& self, FmtContext& ctx) const {
        const auto name = fastipc::io::getAddrName(self);

        return std::formatter<string_view>::format(name, ctx);
    }
};

template <>
class std::formatter<fastipc::io::SocketAddrV4> : public std::formatter<std::string_view> {
  public:
    template <class FmtContext>
    FmtContext::iterator format(const fastipc::io::SocketAddrV4& self, FmtContext& ctx) const {
        return std::format_to(ctx.out(), "{}:{}", self.addr, self.port);
    }
};

template <>
class std::formatter<fastipc::io::Ipv6Addr> : public std::formatter<std::string_view> {
  public:
    template <class FmtContext>
    FmtContext::iterator format(const fastipc::io::Ipv6Addr& self, FmtContext& ctx) const {
        const auto name = fastipc::io::getAddrName(self);

        return std::formatter<string_view>::format(name, ctx);
    }
};

template <>
class std::formatter<fastipc::io::SocketAddrV6> : public std::formatter<std::string_view> {
  public:
    template <class FmtContext>
    FmtContext::iterator format(const fastipc::io::SocketAddrV6& self, FmtContext& ctx) const {
        return std::format_to(ctx.out(), "[{}%{}]:{}", self.addr, self.scope_id, self.port);
    }
};
