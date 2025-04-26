#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "addr.hxx"
#include "result.hxx"

namespace fastipc::io {

expected<std::unordered_map<std::string, std::vector<SocketAddr>>> getInterfaceAddresses() noexcept {
    ::ifaddrs* ifap{};

    return io::sysCheck(::getifaddrs(&ifap))
        .transform(
            [&]() { return std::unique_ptr<::ifaddrs, decltype([](::ifaddrs* ptr) { ::freeifaddrs(ptr); })>{ifap}; })
        .transform([](auto owned_ifa) {
            // NOLINTNEXTLINE(misc-const-correctness)
            std::unordered_map<std::string, std::vector<SocketAddr>> interfaces{};

            for (const auto* ifa = owned_ifa.get(); ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr)
                    continue;

                const auto family = ifa->ifa_addr->sa_family;
                const auto name = std::string{ifa->ifa_name};

                if (family == AF_INET) {
                    const auto& sockaddr = *reinterpret_cast<const ::sockaddr_in*>(ifa->ifa_addr);

                    interfaces[name].emplace_back(SocketAddrV4::from(sockaddr));

                } else if (family == AF_INET6) {
                    const auto& sockaddr = *reinterpret_cast<const ::sockaddr_in6*>(ifa->ifa_addr);

                    interfaces[name].emplace_back(SocketAddrV6::from(sockaddr));
                }
            }

            return interfaces;
        });
}

} // namespace fastipc::io
