/*
 *  fastipc.cxx
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

#include "fastipc.hxx"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>
#include <string_view>

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

void writeClientRequest(std::span<std::byte>& buf, const ClientRequest& request) noexcept {
    const auto topic_name_buf = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(request.topic_name.data()), request.topic_name.size()};

    io::putBuf(buf, request.type);
    io::putBuf(buf, request.max_payload_size);
    io::putBuf(buf, static_cast<std::uint8_t>(topic_name_buf.size()));
    io::putBuf(buf, topic_name_buf);
}

[[nodiscard]] impl::ChannelPage& connect(const ClientRequest& request) {
    const auto sockfd =
        expect(io::adoptSysFd(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)), "failed to create client socket");

    ::sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    constexpr char kPath[] = "fastipcd"; // NOLINT(*-c-arrays)
    static_assert(sizeof(kPath) <= sizeof(addr.sun_path));
    std::memcpy(addr.sun_path, kPath, sizeof(kPath));

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    expect(io::sysCheck(::connect(sockfd.fd(), reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr))),
           "failed to connect to tower");

    std::array<std::byte, 128u> buf{}; // NOLINT(*-magic-numbers)

    std::span<std::byte> sndbuf{buf};
    writeClientRequest(sndbuf, request);

    const auto bytes_written =
        expect(io::sysVal(::write(sockfd.fd(), buf.data(), buf.size() - sndbuf.size())), "failed to write to tower");
    static_cast<void>(bytes_written); // seq packet

    std::size_t total_size{0U};
    int memfd{-1};
    ::msghdr msg{};

    ::iovec iov{.iov_base = &total_size, .iov_len = sizeof(total_size)};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    alignas(::cmsghdr) std::array<char, CMSG_SPACE(sizeof(memfd))> data{};
    msg.msg_control = &data;
    msg.msg_controllen = sizeof(data);

    expect(io::sysCheck(::recvmsg(sockfd.fd(), &msg, 0)), "failed to receive reply from tower");

    const auto* const cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg != nullptr);
    std::memcpy(&memfd, CMSG_DATA(cmsg), sizeof(memfd));

    void* ptr = expect(io::sysVal(::mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0)),
                       "failed to mmap channel memory");

    return *static_cast<impl::ChannelPage*>(ptr);
}

void disconnect(impl::ChannelPage& channel_page) {
    expect(io::sysCheck(::munmap(&channel_page, impl::ChannelPage::total_size(channel_page.max_payload_size))),
           "Failed to munmap channel memory");
}

} // namespace

auto Reader::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<const impl::ChannelSample*>(m_shadow)->sequence_id;
}

auto Reader::Sample::getTimestamp() const -> std::chrono::system_clock::time_point {
    return static_cast<const impl::ChannelSample*>(m_shadow)->timestamp;
}

auto Reader::Sample::getPayload() const -> const void* {
    return +static_cast<const impl::ChannelSample*>(m_shadow)->payload;
}

Reader::Reader(std::string_view channel_name, std::size_t max_payload_size)
    : m_shadow{[=]() {
          auto& channel = connect(
              {.type = RequesterType::Reader, .max_payload_size = max_payload_size, .topic_name = channel_name});
          assert(channel.max_payload_size == max_payload_size);
          return static_cast<void*>(&channel);
      }()} {}

Reader::~Reader() noexcept {
    if (m_shadow == nullptr)
        return;

    disconnect(*static_cast<impl::ChannelPage*>(m_shadow));
    m_shadow = nullptr;
}

bool Reader::hasNewData(std::uint64_t sequence_id) const {
    const auto& page = *static_cast<const impl::ChannelPage*>(m_shadow);
    return impl::hasNewData(page, sequence_id);
}

auto Reader::acquire() -> Sample {
    auto& page = *static_cast<impl::ChannelPage*>(m_shadow);
    auto& sample = impl::acquire(page);

    return Sample{static_cast<void*>(&sample)};
}

void Reader::release(Sample sample_handle) {
    auto& page = *static_cast<impl::ChannelPage*>(m_shadow);
    auto& sample = *static_cast<impl::ChannelSample*>(sample_handle.m_shadow);

    impl::release(page, sample);
}

auto Writer::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<const impl::ChannelSample*>(m_shadow)->sequence_id;
}

auto Writer::Sample::getPayload() -> void* { return +static_cast<impl::ChannelSample*>(m_shadow)->payload; }

Writer::Writer(std::string_view channel_name, std::size_t max_payload_size)
    : m_shadow{[=]() {
          auto& channel = connect(
              {.type = RequesterType::Writer, .max_payload_size = max_payload_size, .topic_name = channel_name});
          std::println("channel sample size: {}", channel.max_payload_size);

          assert(channel.max_payload_size == max_payload_size);
          return static_cast<void*>(&channel);
      }()} {}

Writer::~Writer() noexcept {
    if (m_shadow == nullptr)
        return;

    disconnect(*static_cast<impl::ChannelPage*>(m_shadow));
    m_shadow = nullptr;
}

auto Writer::prepare() -> Sample {
    auto& page = *static_cast<impl::ChannelPage*>(m_shadow);
    auto& sample = impl::prepare(page);

    // Bump the seq id now but do not stamp,
    // thus making writer races visible from logs.
    sample.sequence_id = page.next_seq_id.fetch_add(1U, std::memory_order_relaxed);

    return Sample{static_cast<void*>(&sample)};
}

void Writer::submit(Sample sample_handle) {
    auto& page = *static_cast<impl::ChannelPage*>(m_shadow);
    auto& sample = *static_cast<impl::ChannelSample*>(sample_handle.m_shadow);

    // Timestamp the sample
    sample.timestamp = std::chrono::system_clock::now();

    impl::submit(page, sample);
}

} // namespace fastipc
