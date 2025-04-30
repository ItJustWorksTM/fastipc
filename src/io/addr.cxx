/*
 *  addr.cxx
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
