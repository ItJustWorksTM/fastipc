/*
 *  reactor.cxx
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "fd.hxx"
#include "reactor.hxx"
#include "result.hxx"

namespace fastipc::io {

Reactor::Reactor(Fd event_fd, Fd epoll_fd) : m_event_fd_{std::move(event_fd)}, m_epoll_fd_{std::move(epoll_fd)} {}

expected<Reactor> Reactor::create() noexcept {
    return adoptSysFd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
        .and_then([](Fd event_fd) {
            return adoptSysFd(::epoll_create1(0))
                .and_then([&](Fd epoll_fd) {
                    ::epoll_event event{.events = EPOLLIN | EPOLLET, .data{.u64 = kEventFdData}};

                    return sysCheck(::epoll_ctl(epoll_fd.fd(), EPOLL_CTL_ADD, event_fd.fd(), &event)).transform([&]() {
                        return std::move(epoll_fd);
                    });
                })
                .transform([&](Fd epoll_fd) { return std::pair{std::move(event_fd), std::move(epoll_fd)}; });
        })
        .transform([](std::pair<Fd, Fd> fds) { return Reactor{std::move(fds.first), std::move(fds.second)}; });
}

expected<void> Reactor::react(std::optional<std::chrono::milliseconds> timeout) noexcept {
    return wait(timeout).transform([this](auto events) { process(events); });
}

expected<std::span<::epoll_event>> Reactor::wait(std::optional<std::chrono::milliseconds> timeout) noexcept {
    if (m_registered.empty()) {
        return {};
    }

    const auto timeout_ms = timeout.transform([](auto ms) { return static_cast<int>(ms.count()); }).value_or(-1);

    const auto wait_res = sysVal(
        ::epoll_wait(m_epoll_fd_.fd(), m_events_buf_.data(), static_cast<int>(m_events_buf_.size()), timeout_ms));

    return wait_res.transform([this](int n) { return std::span{m_events_buf_}.first(static_cast<std::size_t>(n)); });
}

void Reactor::process(std::span<::epoll_event> events) noexcept {
    for (const auto& event : events) {
        if (event.data.u64 == kEventFdData) {
            std::uint64_t value{};

            const auto res =
                read(m_event_fd_, std::span<std::byte>{reinterpret_cast<std::byte*>(&value), sizeof(value)});

            static_cast<void>(res);

            continue;
        }

        auto& registered_io = *reinterpret_cast<Registration*>(event.data.ptr);

        const auto readable = event.events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
        if (readable && registered_io.read_cb) {
            auto cb = std::move(registered_io.read_cb);

            cb();
        }

        const auto writable = event.events & (EPOLLOUT | EPOLLHUP | EPOLLERR);
        if (writable && registered_io.write_cb) {
            auto cb = std::move(registered_io.write_cb);

            cb();
        }
    }
}

expected<void> Reactor::interrupt() noexcept {
    std::uint64_t value{};

    return write(m_event_fd_, std::span<const std::byte>{reinterpret_cast<const std::byte*>(&value), sizeof(value)})
        .transform([](std::size_t) {});
}

expected<Reactor::Registration*> Reactor::registerFd(const Fd& fd) noexcept {
    auto* registered_io =
        &m_registered.emplace(fd.fd(), Registration{.fd = fd.fd(), .read_cb = {}, .write_cb = {}}).first->second;

    // TODO: make this configurable
    const auto interests = EPOLLIN | EPOLLOUT;

    ::epoll_event event{.events = interests | EPOLLRDHUP | EPOLLET, .data = {.ptr = registered_io}};

    return sysCheck(::epoll_ctl(m_epoll_fd_.fd(), EPOLL_CTL_ADD, fd.fd(), &event)).transform([&]() {
        // TODO: perhaps just use a fixed buffer
        m_events_buf_.resize(m_events_buf_.size() + 1);

        return registered_io;
    });
}

expected<void> Reactor::unregister(Registration* registration) noexcept {
    // TODO: suboptimal
    const auto res = sysCheck(::epoll_ctl(m_epoll_fd_.fd(), EPOLL_CTL_DEL, registration->fd, nullptr));

    const auto it = m_registered.find(registration->fd);
    assert(it != m_registered.end());
    m_registered.erase(it);

    return res;
}

} // namespace fastipc::io
