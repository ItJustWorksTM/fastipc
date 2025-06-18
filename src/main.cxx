/*
 *  main.cxx
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

#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <optional>
#include <print>
#include <string_view>
#include <utility>
#include "co/scheduler.hxx"
#include "io/fd.hxx"
#include "io/io_env.hxx"
#include "io/polled_io.hxx"
#include "io/reactor.hxx"
#include "io/result.hxx"

namespace fastipc {
namespace {

// NOLINTNEXTLINE (cppcoreguidelines-avoid-reference-coroutine-parameters)
io::Co<io::expected<std::size_t>> readAsync(const io::PolledFd& fd, std::span<std::byte> buf) noexcept {
    auto res = co_await io::TryIoAwaiter{fd, io::Direction::Read, [&]() { return read(fd, buf); }};

    co_return res;
}

// NOLINTNEXTLINE (cppcoreguidelines-avoid-reference-coroutine-parameters)
io::Co<io::expected<std::size_t>> writeAsync(const io::PolledFd& fd, std::span<const std::byte> buf) noexcept {
    auto res = co_await io::TryIoAwaiter{fd, io::Direction::Write, [&]() { return write(fd, buf); }};

    co_return res;
}

io::Co<int> main() {
    std::println("hello world");

    constexpr auto kWBufSize = 32;
    std::array<std::byte, kWBufSize> wbuf{};

    std::print("Enter value: ");
    std::cin.getline(reinterpret_cast<char*>(wbuf.data()), wbuf.size());

    auto [read_fd, write_fd] = expect(io::makePipe());

    auto aread_fd = expect(co_await io::PolledFd::create(std::move(read_fd)));
    auto awrite_fd = expect(co_await io::PolledFd::create(std::move(write_fd)));

    const auto written = expect(co_await writeAsync(awrite_fd, wbuf), "failed to write");

    std::println("written {} bytes", written);

    std::array<std::byte, wbuf.size()> rbuf{};
    const auto read = expect(co_await readAsync(aread_fd, rbuf), "failed to read");

    std::println("read {} bytes", read);
    std::println("loop back: {}", reinterpret_cast<char*>(rbuf.data()));

    // std::println("reading more");
    // const auto read_more = expect(co_await readAsync(read_fd, rbuf), "failed to read");
    // std::println("done: {}", read_more);

    std::println("returning!");
    co_return 0;
}
} // namespace

} // namespace fastipc

// WE NEED SOME KIND OF ROOT LIFETIME HANDLE

int main() {
    auto task = fastipc::main();
    auto reactor = fastipc::expect(fastipc::io::Reactor::create());
    auto scheduler = fastipc::co::Scheduler{};

    fastipc::io::Env env{.scheduler = &scheduler, .reactor = &reactor};
    task.env(&env);
    task.resume();

    while (scheduler.can_run()) {
        while (scheduler.can_run()) {
            scheduler.run();
            fastipc::expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }

        fastipc::expect(reactor.react({}), "failed to react to io events");
    }

    std::println("nothing more to run");
}
