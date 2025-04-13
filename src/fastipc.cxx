
#include "fastipc.hxx"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>

#include <cstdlib>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "channel.hxx"

using namespace fastipc::impl;

namespace fastipc {

namespace {

ChannelPage& connect(std::string_view name) {
    const int sockfd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    ::sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    constexpr char kPath[] = "fastipcd";
    static_assert(sizeof(kPath) <= sizeof(addr.sun_path));
    std::memcpy(addr.sun_path, kPath, sizeof(kPath));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto conn_res = ::connect(sockfd, reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr));
    if (conn_res < 0) {
        ::perror("connect failed: ");
        std::abort();
    }

    std::array<std::uint8_t, 128u> buf{};
    buf[0] = static_cast<std::uint8_t>(name.length());
    std::memcpy(&buf[1], name.data(), name.size());
    const auto write_res = ::write(sockfd, buf.data(), buf.size());
    if (!write_res) {
        ::perror("write failed: ");
        std::abort();
    }

    std::size_t total_size{0U};
    int memfd{-1};
    ::msghdr msg{};
    ::iovec iov{&total_size, sizeof(total_size)};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    alignas(::cmsghdr) std::array<char, CMSG_SPACE(sizeof(memfd))> data{};
    msg.msg_control = &data;
    msg.msg_controllen = sizeof(data);
    const auto recv_res = ::recvmsg(sockfd, &msg, 0);
    if (recv_res < 0) {
        ::perror("recvmsg failed: ");
        std::abort();
    }

    const auto* const cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg != nullptr);
    std::memcpy(&memfd, CMSG_DATA(cmsg), sizeof(memfd));

    void* ptr = ::mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (ptr == nullptr) {
        ::perror("mmap failed: ");
        std::abort();
    }

    return *static_cast<ChannelPage*>(ptr);
}

} // namespace

auto Reader::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<ChannelSample const*>(shadow_)->sequence_id;
}

auto Reader::Sample::getTimestamp() const -> std::chrono::system_clock::time_point {
    return static_cast<ChannelSample const*>(shadow_)->timestamp;
}

auto Reader::Sample::getPayload() const -> void const* { return +static_cast<ChannelSample const*>(shadow_)->payload; }

Reader::Reader(std::string_view channel_name, std::size_t max_payload_size)
    : shadow_{[=]() {
          auto& channel = connect(channel_name);
          assert(channel.max_payload_size == max_payload_size);
          return static_cast<void*>(&channel);
      }()} {}

bool Reader::hasNewData(std::uint64_t sequence_id) const {
    auto const& channel_page = *static_cast<ChannelPage const*>(shadow_);
    auto const index = channel_page.latest_sample_index.load(std::memory_order_relaxed);
    auto const& sample = channel_page[index];

    return sample.sequence_id > sequence_id;
}

auto Reader::acquire() -> Sample {
    auto& channel_page = *static_cast<ChannelPage*>(shadow_);
    auto const index = channel_page.latest_sample_index.load(std::memory_order_relaxed);
    auto& sample = channel_page[index];

    // Bump up sample refcount.
    sample.ref_count.fetch_add(1U, std::memory_order_acquire);

    // Hint that the sample is being used.
    channel_page.occupancy.fetch_or(1U << index, std::memory_order_relaxed);

    return Sample{static_cast<void*>(&sample)};
}

void Reader::release(Sample sample_handle) {
    auto& channel_page = *static_cast<ChannelPage*>(shadow_);
    auto& sample = *static_cast<ChannelSample*>(sample_handle.shadow_);

    // Bump down refcount.
    auto const count = sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the sample is not being used.
    if (count == 1U) {
        auto const index = channel_page.index_of(sample);
        channel_page.occupancy.fetch_xor(1U << index, std::memory_order_relaxed);
    }
}

auto Writer::Sample::getSequenceId() const -> std::uint64_t {
    return static_cast<ChannelSample const*>(shadow_)->sequence_id;
}

auto Writer::Sample::getPayload() -> void* { return +static_cast<ChannelSample*>(shadow_)->payload; }

Writer::Writer(std::string_view channel_name, [[maybe_unused]] std::size_t max_payload_size)
    : shadow_{[=]() {
          auto& channel = connect(channel_name);
          assert(channel.max_payload_size == max_payload_size);
          return static_cast<void*>(&channel);
      }()} {}

auto Writer::prepare() -> Sample {
    auto& channel_page = *static_cast<ChannelPage*>(shadow_);
    for (;; std::this_thread::yield()) {
        // Read occupancy hints.
        auto occupancy = channel_page.occupancy.load(std::memory_order_relaxed);
        if (~occupancy == 0U) {
            // Everything is occupied, which is very unlikely.
            continue;
        }

        for (std::size_t index{0U}; (index = __builtin_ctzll(~occupancy)) < std::numeric_limits<std::uint64_t>::digits;
             occupancy |= (1U << index)) {
            auto& sample = channel_page[index];
            std::uint64_t expected_count{0U};
            constexpr std::uint64_t kDesiredCount{1U};
            if (sample.ref_count.compare_exchange_strong(expected_count, kDesiredCount, std::memory_order_relaxed)) {
                // The hint for this sample was racy.
                continue;
            }

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
    auto& channel_page = *static_cast<ChannelPage*>(shadow_);
    auto& sample = *static_cast<ChannelSample*>(sample_handle.shadow_);

    // Timestamp the sample
    sample.timestamp = std::chrono::system_clock::now();

    // Update latest sample index
    auto const index = channel_page.index_of(sample);
    auto const previous_index = channel_page.latest_sample_index.exchange(index, std::memory_order_release);

    // Bump down previous sample's refcount.
    auto& previous_sample = channel_page[previous_index];
    auto const count = previous_sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the previous latest sample is not being
    // used.
    if (count == 1U) {
        channel_page.occupancy.fetch_xor(1U << previous_index, std::memory_order_relaxed);
    }
}

} // namespace fastipc
