
#include <array>
#include <cstdio>
#include <print>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "xnt_ip.hxx"

namespace fastipc::xnt {

void IPTransport::run_data() {

    const int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::perror("socket");
        return;
    }

    ::sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = ::htons(m_port_number);
    recv_addr.sin_addr.s_addr = ::htonl(INADDR_ANY);

    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&recv_addr), sizeof(recv_addr)) < 0) {
        std::perror("bind");
        ::close(sockfd);
        return;
    }

    constexpr auto kMaxMessageCount = 64uz;
    constexpr auto kMaxMessageSize = 9000uz;
    std::array<::mmsghdr, kMaxMessageCount> msgs{};
    std::array<::sockaddr_in, kMaxMessageCount> src_addrs{};
    std::array<::iovec, kMaxMessageCount> iovecs{};
    std::array<std::array<std::byte, kMaxMessageSize>, kMaxMessageCount> buffers{};

    std::println("Listening for UDP messages on port {}...", m_port_number);

    for (auto i = 0uz; i < kMaxMessageCount; ++i) {
        iovecs[i].iov_base = buffers[i].data();
        iovecs[i].iov_len = kMaxMessageSize;

        msgs[i].msg_hdr.msg_name = &src_addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(::sockaddr_in);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = nullptr;
        msgs[i].msg_hdr.msg_controllen = 0uz;
        msgs[i].msg_hdr.msg_flags = 0;
    }

    for (;;) {
        const int received = ::recvmmsg(sockfd, msgs.data(), kMaxMessageCount, MSG_WAITFORONE, nullptr);
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue; // Try again

            if (errno == EBADF || errno == EINVAL)
                std::println("[INFO] Socket closed. Exiting loop.");
            else
                std::perror("recvmmsg");

            break;
        }

        for (auto i = 0z; i < received; ++i) {
            buffers[i][msgs[i].msg_len] = static_cast<std::byte>('\0');
            std::println("Received [{}]: {}", i, reinterpret_cast<const char*>(buffers[i].data()));
        }
    }
}

} // namespace fastipc::xnt
