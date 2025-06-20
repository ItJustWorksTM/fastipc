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
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <print>
#include <span>
#include <string_view>
#include <thread>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "io/cursor.hxx"
#include "io/fd.hxx"
#include "io/result.hxx"
#include "channel.hxx"
#include "local_proto.hxx"

namespace fastipc {

using namespace impl;

namespace {

void writeClientRequest(std::span<std::byte>& buf, const ClientRequest& request) noexcept {
    const auto topic_name_buf = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(request.topic_name.data()), request.topic_name.size()};

    io::putBuf(buf, request.type);
    io::putBuf(buf, request.max_payload_size);
    io::putBuf(buf, static_cast<std::uint8_t>(topic_name_buf.size()));
    io::putBuf(buf, topic_name_buf);
}

[[nodiscard]] ChannelPage& connect(const ClientRequest& request) {
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
        expect(io::write(sockfd, std::span{buf}.first(buf.size() - sndbuf.size())), "failed to write to tower");
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

    return *static_cast<ChannelPage*>(ptr);
}

void disconnect(ChannelPage& channel_page) {
    expect(io::sysCheck(::munmap(&channel_page, ChannelPage::total_size(channel_page.max_payload_size))),
           "Failed to munmap channel memory");
}

} // namespace

auto Reader::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<const ChannelSample*>(m_shadow)->sequence_id;
}

auto Reader::Sample::getTimestamp() const -> std::chrono::system_clock::time_point {
    return static_cast<const ChannelSample*>(m_shadow)->timestamp;
}

auto Reader::Sample::getPayload() const -> const void* { return +static_cast<const ChannelSample*>(m_shadow)->payload; }

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

    disconnect(*static_cast<ChannelPage*>(m_shadow));
    m_shadow = nullptr;
}

bool Reader::hasNewData(std::uint64_t sequence_id) const {
    const auto& channel_page = *static_cast<const ChannelPage*>(m_shadow);
    const auto index = channel_page.latest_sample_index.load(std::memory_order_relaxed);
    const auto& sample = channel_page[index];

    return sample.sequence_id > sequence_id;
}

auto Reader::acquire() -> Sample {
    auto& channel_page = *static_cast<ChannelPage*>(m_shadow);
    const auto index = channel_page.latest_sample_index.load(std::memory_order_relaxed);
    auto& sample = channel_page[index];

    // Bump up sample refcount.
    sample.ref_count.fetch_add(1U, std::memory_order_acquire);

    // Hint that the sample is being used.
    channel_page.occupancy.fetch_or(1U << index, std::memory_order_relaxed);

    return Sample{static_cast<void*>(&sample)};
}

void Reader::release(Sample sample_handle) {
    auto& channel_page = *static_cast<ChannelPage*>(m_shadow);
    auto& sample = *static_cast<ChannelSample*>(sample_handle.m_shadow);

    // Bump down refcount.
    const auto count = sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the sample is not being used.
    if (count == 1U) {
        const auto index = channel_page.index_of(sample);
        channel_page.occupancy.fetch_xor(1U << index, std::memory_order_relaxed);
    }
}

auto Writer::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<const ChannelSample*>(m_shadow)->sequence_id;
}

auto Writer::Sample::getPayload() -> void* { return +static_cast<ChannelSample*>(m_shadow)->payload; }

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

    disconnect(*static_cast<ChannelPage*>(m_shadow));
    m_shadow = nullptr;
}

auto Writer::prepare() -> Sample {
    auto& channel_page = *static_cast<ChannelPage*>(m_shadow);
    for (;; std::this_thread::yield()) {
        // Read occupancy hints.
        auto occupancy = channel_page.occupancy.load(std::memory_order_relaxed);
        if (~occupancy == 0U)
            // Everything is occupied, which is very unlikely.
            continue;

        // NOLINTNEXTLINE(altera-id-dependent-backward-branch,altera-unroll-loops) Let's benchmark first
        for (std::size_t index{0U}; (index = std::countr_one(occupancy)) < std::numeric_limits<std::uint64_t>::digits;
             occupancy |= (1U << index)) {
            auto& sample = channel_page[index];
            std::uint64_t expected_count{0U};
            constexpr std::uint64_t kDesiredCount{1U};
            if (sample.ref_count.compare_exchange_strong(expected_count, kDesiredCount, std::memory_order_relaxed))
                // The hint for this sample was racy.
                continue;

            // The sample is ours now.
            // Bump the seq id now but do not stamp,
            // thus making writer races visible from logs.
            sample.sequence_id = channel_page.next_seq_id.fetch_add(1U, std::memory_order_relaxed);
            return Sample{static_cast<void*>(&sample)};
        }
        // Everything is occupied and all hints were racy,
        // which is very much unlikely.
    }
}

void Writer::submit(Sample sample_handle) {
    auto& channel_page = *static_cast<ChannelPage*>(m_shadow);
    auto& sample = *static_cast<ChannelSample*>(sample_handle.m_shadow);

    // Timestamp the sample
    sample.timestamp = std::chrono::system_clock::now();

    // Update latest sample index
    const auto index = channel_page.index_of(sample);
    const auto previous_index = channel_page.latest_sample_index.exchange(index, std::memory_order_release);

    // Bump down previous sample's refcount.
    auto& previous_sample = channel_page[previous_index];
    const auto count = previous_sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the previous latest sample is not being
    // used.
    if (count == 1U)
        channel_page.occupancy.fetch_xor(1U << previous_index, std::memory_order_relaxed);
}

} // namespace fastipc
