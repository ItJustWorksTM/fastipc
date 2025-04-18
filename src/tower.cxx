#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "channel.hxx"
#include "fastipc.hxx"

namespace fastipc {
namespace {

class Tower final {
  public:
    static Tower create(std::string_view path) {
        const int sockfd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);

        ::sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        assert(path.size() < sizeof(addr.sun_path));
        std::memcpy(addr.sun_path, path.data(), path.size());
        ::unlink(addr.sun_path);
        const int bind_res =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            ::bind(sockfd, reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr));
        if (bind_res < 0) {
            ::perror("bind failed: ");
            std::abort();
        }

        constexpr int kListenQueueSize{128};
        const auto listen_res = ::listen(sockfd, kListenQueueSize);
        if (listen_res < 0) {
            ::perror("listen failed: ");
            std::abort();
        }

        return Tower{sockfd};
    }

    void run() {
        for (;;) {
            const int client_fd = ::accept(m_sockfd, nullptr, nullptr);
            if (client_fd < 0) {
                ::perror("accept failed: ");
                continue;
            }

            serve(client_fd);
            ::close(client_fd);
        }
    }

  private:
    struct ChannelDescriptor final {
        int memfd{-1};
        std::size_t total_size{0U};
        impl::ChannelPage* page{nullptr};
    };

    explicit Tower(int sockfd) noexcept : m_sockfd{sockfd} {}

    void serve(int client_fd) {
        std::array<std::uint8_t, 128u> buf{};
        const auto read_res = ::read(client_fd, buf.data(), buf.size());
        if (read_res < 0) {
            ::perror("read failed: ");
            return;
        }

        const std::string name{reinterpret_cast<const char*>(&buf[1]), static_cast<std::size_t>(buf[0])};
        auto& channel = m_channels[name];
        if (channel.page == nullptr) {
            channel.memfd = ::memfd_create(name.c_str(), MFD_CLOEXEC);
            if (channel.memfd < 0) {
                ::perror("memfd_create failed: ");
                return;
            }

            constexpr std::size_t kMaxSamplePayloadSize = 256U; // FIXME get that information from the request buffer
            channel.total_size = sizeof(impl::ChannelPage) + std::numeric_limits<std::uint64_t>::digits *
                                                                 (sizeof(impl::ChannelSample) + kMaxSamplePayloadSize);
            const auto ftruncate_res = ::ftruncate(channel.memfd, channel.total_size);
            if (ftruncate_res < 0) {
                ::perror("ftruncate failed: ");
                return;
            }

            void* ptr = ::mmap(nullptr, channel.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, channel.memfd, 0);
            if (ptr == nullptr) {
                ::perror("mmap failed: ");
                return;
            }

            channel.page = ::new (ptr) impl::ChannelPage;
            channel.page->max_payload_size = kMaxSamplePayloadSize;
            // Weakly-reserve the first sample as default latest
            channel.page->next_seq_id.store(1U, std::memory_order_relaxed);
            channel.page->occupancy.store(1U << 0U, std::memory_order_relaxed);

            for (std::size_t i{0U}; i < std::numeric_limits<std::uint64_t>::digits; ++i) {
                ::new (channel.page->samples_storage + i * (sizeof(impl::ChannelSample) + kMaxSamplePayloadSize))
                    impl::ChannelSample;
            }
        }

        ::msghdr msg{};
        ::iovec iov{static_cast<void*>(&channel.total_size), sizeof(channel.total_size)};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        alignas(::cmsghdr) std::array<char, CMSG_SPACE(sizeof(channel.memfd))> ctrl{};
        msg.msg_control = ctrl.data();
        msg.msg_controllen = ctrl.size();
        auto* const cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(channel.memfd));
        std::memcpy(CMSG_DATA(cmsg), &channel.memfd, sizeof(channel.memfd));
        msg.msg_controllen = cmsg->cmsg_len;
        const auto send_res = ::sendmsg(client_fd, &msg, 0);
        if (send_res < 0) {
            ::perror("send faild: ");
            return;
        }
    }

    int m_sockfd;
    std::unordered_map<std::string, ChannelDescriptor> m_channels;
};

} // namespace
} // namespace fastipc

int main() {
    using namespace std::literals;

    auto tower = fastipc::Tower::create("fastipcd"sv);
    tower.run();
}
