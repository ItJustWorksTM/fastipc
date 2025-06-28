/*
 *  tower.hxx
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

#include <unordered_map>

#include "io/fd.hxx"
#include "io/io_env.hxx"
#include "channel.hxx"

namespace fastipc {

class Tower final {
  public:
    [[nodiscard]] static io::Co<Tower> create(std::string_view path);

    io::Co<void> run();
    void shutdown();

  private:
    // NOLINTNEXTLINE(altera-struct-pack-align)
    struct ChannelDescriptor final {
        io::Fd memfd;
        std::size_t total_size{0U};
        impl::ChannelPage* page{nullptr};
    };

    explicit Tower(io::Fd sockfd) noexcept : m_sockfd{std::move(sockfd)} {}

    io::Co<void> serve(io::Fd clientfd);

    io::Fd m_sockfd;
    std::unordered_map<std::string, ChannelDescriptor> m_channels;
};

} // namespace fastipc
