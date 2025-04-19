#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>

namespace fastipc {

namespace impl {

struct ChannelSample final {
    std::atomic_size_t ref_count{0U};
    std::size_t sequence_id{0U};
    std::size_t size{0U};
    std::chrono::system_clock::time_point timestamp{};
    std::byte payload[0];
};

struct ChannelPage final {
    std::size_t max_payload_size{0U};
    std::atomic_size_t next_seq_id{0U};
    std::atomic_uint64_t occupancy{0U};
    std::atomic_size_t latest_sample_index{0U};
    alignas(ChannelSample) std::byte samples_storage[0];

    std::size_t sample_size() const { return sizeof(ChannelSample) + max_payload_size; }
    std::size_t index_of(const ChannelSample& sample) const {
        return (reinterpret_cast<const std::byte*>(&sample) - samples_storage) / sample_size();
    }

    const ChannelSample& operator[](std::size_t index) const {
        return *reinterpret_cast<const ChannelSample*>(&samples_storage[index * sample_size()]);
    }
    ChannelSample& operator[](std::size_t index) {
        return *reinterpret_cast<ChannelSample*>(&samples_storage[index * sample_size()]);
    }
};

} // namespace impl
} // namespace fastipc
