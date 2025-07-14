/*
 *  channel.cxx
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

#include "channel.hxx"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

namespace fastipc::impl {

bool hasNewData(const ChannelPage& page, std::uint64_t sequence_id) {
    const auto index = page.latest_sample_index.load(std::memory_order_relaxed);
    const auto& sample = page[index];

    return sample.sequence_id > sequence_id;
}

auto acquire(ChannelPage& page) -> ChannelSample& {
    const auto index = page.latest_sample_index.load(std::memory_order_relaxed);
    auto& sample = page[index];

    // Bump up sample refcount.
    sample.ref_count.fetch_add(1U, std::memory_order_acquire);

    // Hint that the sample is being used.
    page.occupancy.fetch_or(1U << index, std::memory_order_relaxed);

    return sample;
}

void release(ChannelPage& page, ChannelSample& sample) {
    // Bump down refcount.
    const auto count = sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the sample is not being used.
    if (count == 1U) {
        const auto index = page.index_of(sample);
        page.occupancy.fetch_xor(1U << index, std::memory_order_relaxed);
    }
}

auto prepare(ChannelPage& page) -> ChannelSample& {
    for (;; std::this_thread::yield()) {
        // Read occupancy hints.
        auto occupancy = page.occupancy.load(std::memory_order_relaxed);
        if (~occupancy == 0U)
            // Everything is occupied, which is very unlikely.
            continue;

        // NOLINTNEXTLINE(altera-id-dependent-backward-branch,altera-unroll-loops) Let's benchmark first
        for (std::size_t index{0U}; (index = std::countr_one(occupancy)) < std::numeric_limits<std::uint64_t>::digits;
             occupancy |= (1U << index)) {
            auto& sample = page[index];
            std::uint64_t expected_count{0U};
            constexpr std::uint64_t kDesiredCount{1U};
            if (sample.ref_count.compare_exchange_strong(expected_count, kDesiredCount, std::memory_order_relaxed))
                // The hint for this sample was racy.
                continue;

            return sample;
        }

        // Everything is occupied and all hints were racy, which is very much unlikely.
    }
}

void submit(ChannelPage& page, ChannelSample& sample) {
    // Update latest sample index
    const auto index = page.index_of(sample);
    const auto previous_index = page.latest_sample_index.exchange(index, std::memory_order_release);

    // Bump down previous sample's refcount.
    auto& previous_sample = page[previous_index];
    const auto count = previous_sample.ref_count.fetch_sub(1U, std::memory_order_relaxed);

    // If refcount is zero, hint that the previous latest sample is not being used.
    if (count == 1U)
        page.occupancy.fetch_xor(1U << previous_index, std::memory_order_relaxed);
}

} // namespace fastipc::impl
