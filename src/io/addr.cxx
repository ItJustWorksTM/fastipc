#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "addr.hxx"
#include "result.hxx"

namespace fastipc::io {

std::string getAddrName(const Ipv4Addr& addr) noexcept {
    ::sockaddr_in sockaddr{};
    sockaddr.sin_family = AF_INET;
    std::memcpy(&sockaddr.sin_addr, addr.value.data(), addr.value.size());

    std::array<char, NI_MAXHOST> name_buf{};

    const auto res = ::getnameinfo(reinterpret_cast<const ::sockaddr*>(&sockaddr), sizeof(sockaddr), name_buf.data(),
                                   NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);

    assert(res == 0); // TODO(ruthgerd): return std::expected

    return std::string{name_buf.data()};
}

std::string getAddrName(const Ipv6Addr& addr) noexcept {
    ::sockaddr_in6 sockaddr{};
    sockaddr.sin6_family = AF_INET6;
    std::memcpy(sockaddr.sin6_addr.s6_addr16, addr.value.data(), addr.value.size());

    std::array<char, NI_MAXHOST> name_buf{};

    const auto res = ::getnameinfo(reinterpret_cast<const ::sockaddr*>(&sockaddr), sizeof(sockaddr), name_buf.data(),
                                   NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);

    assert(res == 0); // TODO(ruthgerd): return std::expected

    return std::string{name_buf.data()};
}

expected<std::vector<SocketAddr>> getInterfaceAddresses() noexcept {
    ::ifaddrs* ifap{};

    return io::sysCheck(::getifaddrs(&ifap))
        .transform(
            [&]() { return std::unique_ptr<::ifaddrs, decltype([](::ifaddrs* ptr) { ::freeifaddrs(ptr); })>{ifap}; })
        .transform([](auto owned_ifa) {
            // NOLINTNEXTLINE(misc-const-correctness)
            std::vector<SocketAddr> sockaddrs{};

            // NOLINTNEXTLINE(altera-unroll-loops,altera-id-dependent-backward-branch)
            for (const auto* ifa = owned_ifa.get(); ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr)
                    continue;

                const auto family = ifa->ifa_addr->sa_family;
                if (family == AF_INET) {
                    const auto& sockaddr = *reinterpret_cast<const ::sockaddr_in*>(ifa->ifa_addr);

                    sockaddrs.emplace_back(SocketAddrV4::from(sockaddr));

                } else if (family == AF_INET6) {
                    const auto& sockaddr = *reinterpret_cast<const ::sockaddr_in6*>(ifa->ifa_addr);

                    sockaddrs.emplace_back(SocketAddrV6::from(sockaddr));
                }
            }

            return sockaddrs;
        });
}

} // namespace fastipc::io
