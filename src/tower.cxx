#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "io/fd.hxx"
#include "io/result.hxx"
#include "channel.hxx"
#include "tower.hxx"

namespace fastipc {
namespace {

class Tower final {
  public:
    static Tower create(std::string_view path) {
        auto sockfd = expect(io::adoptSysFd(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)),
                             "failed to create tower socket");

        ::sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        assert(path.size() < sizeof(addr.sun_path));

        std::memcpy(addr.sun_path, path.data(), path.size());

        auto unlink_res = io::sysCheck(::unlink(addr.sun_path));
        static_cast<void>(unlink_res);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        expect(io::sysCheck(::bind(sockfd.fd(), reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr))),
               "failed to bind tower socket");

        constexpr int kListenQueueSize{128};
        expect(io::sysCheck(::listen(sockfd.fd(), kListenQueueSize)), "failed to listen to tower socket");

        return Tower{std::move(sockfd)};
    }

    void run() {
        for (;;) {
            auto clientfd = expect(io::adoptSysFd(::accept(m_sockfd.fd(), nullptr, nullptr)),
                                   "failed to accept incoming connection");

            serve(std::move(clientfd));
        }
    }

  private:
    struct ChannelDescriptor final {
        io::Fd memfd;
        std::size_t total_size{0U};
        impl::ChannelPage* page{nullptr};
    };

    explicit Tower(io::Fd sockfd) noexcept : m_sockfd{std::move(sockfd)} {}

    void serve(io::Fd clientfd) {
        std::array<std::uint8_t, 128u> buf{};
        const auto bytes_read =
            expect(io::sysVal(::read(clientfd.fd(), buf.data(), buf.size())), "failed to read from client");

        auto recvbuf = std::span<const std::uint8_t>{buf.data(), static_cast<std::size_t>(bytes_read)};
        const auto request = readClientRequest(recvbuf);

        std::print("{} request for topic '{}' with max payload size of {} bytes.\n",
                   (request.type == RequesterType::Reader ? "reader" : "writer"), request.topic_name,
                   request.max_payload_size);

        const auto topic_name = std::string{request.topic_name};
        auto& channel = m_channels[topic_name];

        if (channel.page == nullptr) {
            channel.memfd =
                expect(io::adoptSysFd(::memfd_create(topic_name.c_str(), MFD_CLOEXEC)), "failed to create memfd");

            channel.total_size =
                sizeof(impl::ChannelPage) +
                std::numeric_limits<std::uint64_t>::digits * (sizeof(impl::ChannelSample) + request.max_payload_size);

            expect(io::sysCheck(::ftruncate(channel.memfd.fd(), channel.total_size)),
                   "failed to truncate channel memory");

            void* ptr = expect(io::sysVal(::mmap(nullptr, channel.total_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                                 channel.memfd.fd(), 0)),
                               "failed to mmap channel memory");

            channel.page = ::new (ptr) impl::ChannelPage;
            channel.page->max_payload_size = request.max_payload_size;
            // Weakly-reserve the first sample as default latest
            channel.page->next_seq_id.store(1U, std::memory_order_relaxed);
            channel.page->occupancy.store(1U << 0U, std::memory_order_relaxed);

            for (std::size_t i{0U}; i < std::numeric_limits<std::uint64_t>::digits; ++i) {
                ::new (channel.page->samples_storage + i * (sizeof(impl::ChannelSample) + request.max_payload_size))
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

        expect(io::sysVal(::sendmsg(clientfd.fd(), &msg, 0)), "failed to send reply to client");
    }

    io::Fd m_sockfd;
    std::unordered_map<std::string, ChannelDescriptor> m_channels;
};

} // namespace
} // namespace fastipc

int main() {
    using namespace std::literals;

    auto tower = fastipc::Tower::create("fastipcd"sv);
    tower.run();
}
