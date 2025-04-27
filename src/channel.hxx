/*
 *  channel.hxx
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

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>

namespace fastipc::impl {

// NOLINTNEXTLINE(altera-struct-pack-align)
struct ChannelSample final {
    std::atomic_size_t ref_count{0U};
    std::size_t sequence_id{0U};
    std::size_t size{0U};
    std::chrono::system_clock::time_point timestamp;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    std::byte payload[0]; // NOLINT(*-c-arrays)
#pragma GCC diagnostic pop
};

// NOLINTNEXTLINE(altera-struct-pack-align)
struct ChannelPage final {
    std::size_t max_payload_size{0U};
    std::atomic_size_t next_seq_id{0U};
    std::atomic_uint64_t occupancy{0U};
    std::atomic_size_t latest_sample_index{0U};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    alignas(ChannelSample) std::byte samples_storage[0]; // NOLINT(*-c-arrays)
#pragma GCC diagnostic pop

    [[nodiscard]] std::size_t sample_size() const { return sizeof(ChannelSample) + max_payload_size; }
    [[nodiscard]] std::size_t index_of(const ChannelSample& sample) const {
        return (reinterpret_cast<const std::byte*>(&sample) - samples_storage) / sample_size();
    }

    [[nodiscard]] const ChannelSample& operator[](std::size_t index) const {
        return *reinterpret_cast<const ChannelSample*>(&samples_storage[index * sample_size()]);
    }
    [[nodiscard]] ChannelSample& operator[](std::size_t index) {
        return *reinterpret_cast<ChannelSample*>(&samples_storage[index * sample_size()]);
    }

    [[nodiscard]] constexpr static std::size_t total_size(std::size_t max_payload_size) noexcept {
        return sizeof(ChannelPage) +
               (std::numeric_limits<std::uint64_t>::digits * (sizeof(ChannelSample) + max_payload_size));
    }
};

} // namespace fastipc::impl
