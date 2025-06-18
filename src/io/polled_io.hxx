/*
 *  polled_io.hxx
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

#include <coroutine>
#include "io/io_env.hxx"
#include "fd.hxx"
#include "reactor.hxx"
#include "result.hxx"

namespace fastipc::io {

class PolledFd final {
  public:
    PolledFd(const PolledFd&) noexcept = delete;
    PolledFd& operator=(const PolledFd&) noexcept = delete;

    PolledFd(PolledFd&& it) noexcept = default;
    PolledFd& operator=(PolledFd&& rhs) noexcept = default;

    ~PolledFd() noexcept {
        if (m_fd.fd() != -1) {
            const auto res = expected(m_reactor->unregister(m_registration));

            static_cast<void>(res);
        }
    }

    static Co<expected<PolledFd>> create(Fd fd) {
        auto& reactor = *(co_await co::GetEnv{}).reactor;

        co_return setBlocking(fd, false)
            .and_then([&]() { return reactor.registerFd(fd); })
            .transform([&](auto* registration) { return PolledFd{std::move(fd), registration, reactor}; });
    }

    [[nodiscard]] constexpr const int& fd() const noexcept { return m_fd.fd(); }

  private:
    template <class>
    friend class TryIoAwaiter;

    PolledFd(Fd fd, Reactor::Registration* registration, Reactor& reactor) noexcept
        : m_fd{std::move(fd)}, m_registration{registration}, m_reactor{&reactor} {}

    Fd m_fd;
    Reactor::Registration* m_registration;
    Reactor* m_reactor;
};

template <class F>
class TryIoAwaiter final {
  public:
    using value_type = std::invoke_result_t<F>;

    explicit TryIoAwaiter(const io::PolledFd& fd, io::Direction direction, F io)
        : m_fd{&fd}, m_direction{direction}, m_io{std::move(io)} {}

    bool await_ready() noexcept { return false; }

    template <class T>
    void await_suspend(std::coroutine_handle<Promise<T>> cont) {
        m_cont = cont;
        m_env = &cont.promise().env();

        m_env->scheduler->schedule([this]() { poll(); });
    }

    value_type await_resume() noexcept { return std::move(m_value).value(); }

  private:
    void poll() {
        auto res = m_io();

        if (!res.has_value()) {
            if (res.error() == std::errc::operation_would_block ||
                res.error() == std::errc::resource_unavailable_try_again) {

                auto& registration = *m_fd->m_registration;
                auto& cb = m_direction == io::Direction::Read ? registration.read_cb : registration.write_cb;

                cb = [this]() { m_env->scheduler->schedule([&]() { m_cont.resume(); }); };

                return;
            }
        }

        m_value = std::move(res);

        m_env->scheduler->schedule([&]() { m_cont.resume(); });
    }

    const io::PolledFd* m_fd;
    io::Direction m_direction;
    F m_io;

    Env* m_env = nullptr;
    std::optional<value_type> m_value = {};
    std::coroutine_handle<> m_cont;
};

} // namespace fastipc::io
