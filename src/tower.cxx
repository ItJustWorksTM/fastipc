/*
 *  tower.cxx
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

#include "tower.hxx"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "co/task.hxx"

#include <stop_token>
#include "co/coroutine.hxx"
#include "io/context.hxx"
#include "io/cursor.hxx"
#include "io/fd.hxx"
#include "io/polled_fd.hxx"
#include "io/result.hxx"
#include "channel.hxx"
#include "local_proto.hxx"

namespace fastipc {
namespace {

[[nodiscard]] io::expected<std::optional<ClientRequest>> readClientRequest(std::span<const std::byte>& obuf) {
    constexpr static auto kMinSize = 10u;

    auto buf = obuf;

    if (kMinSize > buf.size()) {
        return {};
    }

    const auto requester_type = io::getBuf<std::underlying_type_t<RequesterType>>(buf);

    if (requester_type >= 2) {
        return io::unexpected{std::make_error_code(std::errc::protocol_error)};
    }

    const auto max_payload_size = io::getBuf<std::size_t>(buf);
    const auto topic_name_size = io::getBuf<std::uint8_t>(buf);

    if (topic_name_size > buf.size()) {
        return {};
    }

    const auto topic_name_buf = io::takeBuf(buf, topic_name_size);

    obuf = buf;

    return ClientRequest{
        .type = static_cast<RequesterType>(requester_type),
        .max_payload_size = max_payload_size,
        .topic_name = {reinterpret_cast<const char*>(topic_name_buf.data()), topic_name_buf.size()},
    };
}

} // namespace

[[nodiscard]] co::Co<Tower> Tower::create(std::string_view path) {

    auto sockfd =
        expect(io::adoptSysFd(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)), "failed to create tower socket");

    ::sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    assert(path.size() < sizeof(addr.sun_path));

    std::memcpy(addr.sun_path, path.data(), path.size());

    auto unlink_res = io::sysCheck(::unlink(addr.sun_path));
    static_cast<void>(unlink_res);

    // TODO: This technically has to be async as well
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    expect(io::sysCheck(::bind(sockfd.fd(), reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr))),
           "failed to bind tower socket");

    constexpr int kListenQueueSize{128};
    expect(io::sysCheck(::listen(sockfd.fd(), kListenQueueSize)), "failed to listen to tower socket");

    co_return Tower{expect(io::PolledFd::create(std::move(sockfd)), "failed to created polled fd")};
}

co::Co<void> Tower::run(std::stop_token stop_token) {

    // NOLINTNEXTLINE(altera-unroll-loops) Service loops should not be unrolled
    for (; !stop_token.stop_requested();) {
        try {
            auto expected_clientfd = co_await accept(m_sockfd, stop_token);

            if (!expected_clientfd.has_value()) {
                if (expected_clientfd.error() == std::errc::bad_file_descriptor)
                    break;
                if (expected_clientfd.error() == std::errc::connection_aborted)
                    continue;
            }

            auto clientfd = expect(std::move(expected_clientfd), "failed to accept incoming connection");

            static_cast<void>(co::spawn(serve(std::move(clientfd), stop_token)));

        } catch (const fastipc::io::StoppedException&) {
            break;
        }
    }

    co_return;
}

void Tower::shutdown() {
    // expect(io::sysCheck(::shutdown(m_sockfd.fd(), SHUT_RD)), "Failed to shutdown tower socket");
}

co::Co<void> Tower::serve(io::PolledFd clientfd, std::stop_token stop_token) {
    std::array<std::byte, 128u> buf{}; // NOLINT(*-magic-numbers)
    const auto bytes_read =
        expect(co_await io::aread(clientfd, std::span{buf}, stop_token), "failed to read from client");

    co_await io::yield();

    auto recvbuf = std::span<const std::byte>{buf}.first(bytes_read);
    const auto request = expect(expect(readClientRequest(recvbuf), "invalid request"), "incomplete message");

    std::println("{} request for topic '{}' with max payload size of {} bytes.",
                 (request.type == RequesterType::Reader ? "reader" : "writer"), request.topic_name,
                 request.max_payload_size);

    const auto topic_name = std::string{request.topic_name};
    auto& channel = m_channels[topic_name];

    if (channel.page == nullptr) {
        channel.memfd =
            expect(io::adoptSysFd(::memfd_create(topic_name.c_str(), MFD_CLOEXEC)), "failed to create memfd");

        channel.total_size = impl::ChannelPage::total_size(request.max_payload_size);

        // NOLINTNEXTLINE(*-narrowing-conversions)
        expect(io::sysCheck(::ftruncate(channel.memfd.fd(), channel.total_size)), "failed to truncate channel memory");

        // NOLINTNEXTLINE(misc-const-correctness)
        void* ptr = expect(
            io::sysVal(::mmap(nullptr, channel.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, channel.memfd.fd(), 0)),
            "failed to mmap channel memory");

        channel.page = ::new (ptr) impl::ChannelPage;
        channel.page->max_payload_size = request.max_payload_size;
        // Weakly-reserve the first sample as default latest
        channel.page->next_seq_id.store(1U, std::memory_order_relaxed);
        channel.page->occupancy.store(1U << 0U, std::memory_order_relaxed);

        // NOLINTNEXTLINE(altera-unroll-loops) This shouldn't be unrolled as much as optimized away
        for (std::size_t i{0U}; i < std::numeric_limits<std::uint64_t>::digits; ++i) {
            ::new (channel.page->samples_storage + (i * (sizeof(impl::ChannelSample) + request.max_payload_size)))
                impl::ChannelSample;
        }
    }

    ::msghdr msg{};

    ::iovec iov{.iov_base = static_cast<void*>(&channel.total_size), .iov_len = sizeof(channel.total_size)};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(channel.memfd))> ctrl{};
    msg.msg_control = ctrl.data();
    msg.msg_controllen = ctrl.size();

    auto* const cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET; // NOLINT(misc-include-cleaner) false-positive
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(channel.memfd.fd()));
    std::memcpy(CMSG_DATA(cmsg), &channel.memfd.fd(), sizeof(channel.memfd));
    msg.msg_controllen = cmsg->cmsg_len;

    auto const send_n = expect(co_await io::asendmsg(clientfd, msg, 0), "failed to send reply to client");
    static_cast<void>(send_n);

    co_return;
}

} // namespace fastipc
