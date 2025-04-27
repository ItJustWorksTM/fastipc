/*
 *  intraprocess.cxx
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

#include <cassert>
#include <cstddef>
#include <thread>

#include "fastipc.hxx"
#include "tower.hxx"

int main() {

    auto tower = fastipc::Tower::create("fastipcd");
    const std::jthread tower_thread{[&] { tower.run(); }};

    constexpr std::string_view channel_name{"Hallowed are the Ori"};
    constexpr std::size_t max_payload_size{sizeof(int)};

    fastipc::Writer writer{channel_name, max_payload_size};
    fastipc::Reader reader{channel_name, max_payload_size};

    {
        auto sample = reader.acquire();
        assert(sample.getSequenceId() == 0);
        reader.release(sample);
    }

    {
        auto sample = writer.prepare();
        assert(sample.getSequenceId() == 1);
        *static_cast<int*>(sample.getPayload()) = 5; // NOLINT(*-magic-numbers)
        writer.submit(sample);
    }

    {
        auto sample = reader.acquire();
        assert(sample.getSequenceId() == 1);
        assert(*static_cast<const int*>(sample.getPayload()) == 5);
        reader.release(sample);
    }

    tower.shutdown();
}
