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
#include <print>
#include <thread>

#include "fastipc.hxx"
#include "tower.hxx"

#include "co/coroutine.hxx"
#include "co/task.hxx"
#include "io/context.hxx"

namespace {

fastipc::co::Co<int> co_main() {
    auto tower = co_await fastipc::Tower::create("fastipcd");

    std::stop_source stop_source{};
    auto handle = fastipc::co::spawn(tower.run(stop_source.get_token()));

    auto test = std::jthread{[&] {
        std::println("starting test in thead");
        constexpr std::string_view channel_name{"Hallowed are the Ori"};
        constexpr std::size_t max_payload_size{sizeof(int)};

        fastipc::Writer writer{channel_name, max_payload_size};
        fastipc::Reader reader{channel_name, max_payload_size};

        {
            std::println("reading sample");
            auto sample = reader.acquire();
            assert(sample.getSequenceId() == 0);
            reader.release(sample);
        }

        {
            std::println("writing sample");
            auto sample = writer.prepare();
            assert(sample.getSequenceId() == 1);
            *static_cast<int*>(sample.getPayload()) = 5; // NOLINT(*-magic-numbers)
            writer.submit(sample);
        }

        {
            std::println("reading sample");
            auto sample = reader.acquire();
            assert(sample.getSequenceId() == 1);
            assert(*static_cast<const int*>(sample.getPayload()) == 5);
            reader.release(sample);
        }

        // tower.shutdown();
        std::println("test done. stopping handle");

        fastipc::io::Runtime::singleton().scheduler().schedule([&]() { stop_source.request_stop(); });
    }};

    static_cast<void>(co_await std::move(handle));

    std::println("run done!");

    co_return 0; // too lazy for void
}

} // namespace

int main() {
    auto runtime = fastipc::expect(fastipc::io::Runtime::create());

    return runtime.block_on(co_main);
}
