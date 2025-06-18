/*
 *  reactor.hxx
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

#include <chrono>
#include <cstdint>
#include "fd.hxx"
#include "result.hxx"

#include <functional>
#include <optional>
#include <vector>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace fastipc::io {

enum class Direction : std::uint8_t {
    Read,
    Write,
};

class Reactor final {
  public:
    struct Registration {
        int fd;
        std::function<void()> read_cb;
        std::function<void()> write_cb;
    };

    Reactor(const Reactor&) noexcept = delete;
    Reactor& operator=(const Reactor&) noexcept = delete;

    Reactor(Reactor&&) noexcept = default;
    Reactor& operator=(Reactor&&) noexcept = default;

    ~Reactor() = default;

    static expected<Reactor> create() noexcept;
    expected<void> react(std::optional<std::chrono::milliseconds> timeout) noexcept;

    expected<void> interrupt() noexcept;

    expected<Registration*> registerFd(const Fd& fd) noexcept;

    expected<void> unregister(Registration* registration) noexcept;

  private:
    explicit Reactor(Fd event_fd, Fd epoll_fd);

    expected<std::span<::epoll_event>> wait(std::optional<std::chrono::milliseconds> timeout) noexcept;
    void process(std::span<::epoll_event> events) noexcept;

    static constexpr std::uint64_t kEventFdData = 1;
    Fd m_event_fd_;

    Fd m_epoll_fd_;
    std::vector<::epoll_event> m_events_buf_;

    std::unordered_map<int, Registration> m_registered;
};

} // namespace fastipc::io
