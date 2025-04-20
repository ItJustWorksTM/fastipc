#include "tower.hxx"

#include <array>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "io/cursor.hxx"
#include "io/fd.hxx"
#include "io/result.hxx"
#include "channel.hxx"
#include "local_proto.hxx"

namespace fastipc {
namespace {

[[nodiscard]] ClientRequest readClientRequest(std::span<const std::byte>& buf) noexcept {
    const auto requester_type = io::getBuf<std::underlying_type_t<RequesterType>>(buf);
    const auto max_payload_size = io::getBuf<std::size_t>(buf);
    const auto topic_name_buf = io::takeBuf(buf, io::getBuf<std::uint8_t>(buf));

    assert(requester_type < 2);

    return {
        static_cast<RequesterType>(requester_type),
        max_payload_size,
        {reinterpret_cast<const char*>(topic_name_buf.data()), topic_name_buf.size()},
    };
}
} // namespace

[[nodiscard]] Tower Tower::create(std::string_view path) {
    auto sockfd =
        expect(io::adoptSysFd(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)), "failed to create tower socket");

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

void Tower::run() {
    for (;;) {
        auto expected_clientfd = io::adoptSysFd(::accept(m_sockfd.fd(), nullptr, nullptr));
        if (!expected_clientfd.has_value()) {
            if (expected_clientfd.error() == std::errc::invalid_argument)
                break;
            if (expected_clientfd.error() == std::errc::connection_aborted)
                continue;
        }

        auto clientfd = expect(std::move(expected_clientfd), "failed to accept incoming connection");
        serve(std::move(clientfd));
    }
}

void Tower::shutdown() { expect(io::sysCheck(::shutdown(m_sockfd.fd(), SHUT_RD)), "Failed to shutdown tower socket"); }

void Tower::serve(io::Fd clientfd) {
    std::array<std::byte, 128u> buf{};
    const auto bytes_read =
        expect(io::sysVal(::read(clientfd.fd(), buf.data(), buf.size())), "failed to read from client");

    auto recvbuf = std::span<const std::byte>{buf.data(), static_cast<std::size_t>(bytes_read)};
    const auto request = readClientRequest(recvbuf);

    std::println("{} request for topic '{}' with max payload size of {} bytes.",
                 (request.type == RequesterType::Reader ? "reader" : "writer"), request.topic_name,
                 request.max_payload_size);

    const auto topic_name = std::string{request.topic_name};
    auto& channel = m_channels[topic_name];

    if (channel.page == nullptr) {
        channel.memfd =
            expect(io::adoptSysFd(::memfd_create(topic_name.c_str(), MFD_CLOEXEC)), "failed to create memfd");

        channel.total_size = impl::ChannelPage::total_size(request.max_payload_size);

        expect(io::sysCheck(::ftruncate(channel.memfd.fd(), channel.total_size)), "failed to truncate channel memory");

        void* ptr = expect(
            io::sysVal(::mmap(nullptr, channel.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, channel.memfd.fd(), 0)),
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

    alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(channel.memfd))> ctrl{};
    msg.msg_control = ctrl.data();
    msg.msg_controllen = ctrl.size();

    auto* const cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(channel.memfd));
    std::memcpy(CMSG_DATA(cmsg), &channel.memfd, sizeof(channel.memfd));
    msg.msg_controllen = cmsg->cmsg_len;

    static_cast<void>(expect(io::sysVal(::sendmsg(clientfd.fd(), &msg, 0)), "failed to send reply to client"));
}

} // namespace fastipc
