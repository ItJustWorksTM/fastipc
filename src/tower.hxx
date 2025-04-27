#pragma once

#include <utility>

#include <unordered_map>

#include "io/fd.hxx"
#include "channel.hxx"

namespace fastipc {

class Tower final {
  public:
    [[nodiscard]] static Tower create(std::string_view path);

    void run();

    void shutdown();

  private:
    // NOLINTNEXTLINE(altera-struct-pack-align)
    struct ChannelDescriptor final {
        io::Fd memfd;
        std::size_t total_size{0U};
        impl::ChannelPage* page{nullptr};
    };

    explicit Tower(io::Fd sockfd) noexcept : m_sockfd{std::move(sockfd)} {}

    void serve(io::Fd clientfd);

    io::Fd m_sockfd;
    std::unordered_map<std::string, ChannelDescriptor> m_channels;
};

} // namespace fastipc
