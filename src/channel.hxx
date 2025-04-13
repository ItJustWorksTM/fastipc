#pragma once

#include <array>
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
    unsigned char payload[0];
};

struct ChannelPage final {
    std::size_t max_payload_size{0U};
    std::atomic_size_t next_seq_id{0U};
    std::atomic_uint64_t occupancy{0U};
    std::atomic_size_t latest_sample_index{0U};
    alignas(ChannelSample) unsigned char samples_storage[0];

    std::size_t sample_size() const { return sizeof(ChannelSample) + max_payload_size; }
    std::size_t index_of(ChannelSample const& sample) const {
        return (reinterpret_cast<const unsigned char*>(&sample) - samples_storage) / sample_size();
    }

    ChannelSample const& operator[](std::size_t index) const {
        return *reinterpret_cast<ChannelSample const*>(&samples_storage[index * sample_size()]);
    }
    ChannelSample& operator[](std::size_t index) {
        return *reinterpret_cast<ChannelSample*>(&samples_storage[index * sample_size()]);
    }
};

} // namespace impl
} // namespace fastipc
