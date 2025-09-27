/*
 *  polled_fd.hxx
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

#include <cassert>
#include <chrono>
#include <coroutine>
#include <optional>
#include <print>
#include <stop_token>
#include <thread>
#include "co/coroutine.hxx"
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

    static Co<expected<PolledFd>> create(Fd fd) noexcept {
        auto& reactor = *(co_await co::getEnv()).reactor;

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
        std::stop_token st = cont.promise().stop_token();

        if (st.stop_requested()) {

            set_stopped();
            return;
        }

        m_stop_fn.emplace(std::move(st), StopFn{this});

        schedule_poll();
    }

    value_type await_resume() noexcept { return std::move(m_value).value(); }

  private:
    void poll() {
        if (stop_requested) {
            std::println("stop request so we set_stopped!");
            set_stopped();
            return;
        }

        // POSSIBLE RACE CONDITION
        // stop_request set to true, but in flight, set new reactor callback, ignore stop..

        auto res = m_io();

        if (!res.has_value()) {
            if (res.error() == std::errc::operation_would_block ||
                res.error() == std::errc::resource_unavailable_try_again) {

                state = State::Blocked;
                m_fd->m_registration->callback(m_direction, [this]() { schedule_poll(); });

                return;
            }
        }

        set_value(std::move(res));
    }

    void set_value(value_type res) {
        m_stop_fn.reset();

        m_value = std::move(res);

        state = State::Done;
        m_env->scheduler->schedule([&]() { m_cont.resume(); });
    }

    void set_stopped() {
        m_stop_fn.reset();

        state = State::Done;

        // TODO: propagate, use adapter for that
        assert(false);
    }

    void schedule_poll() noexcept {
        if (state != State::Blocked) {
            return;
        }
        
        state = State::Flight;
        m_env->scheduler->schedule([this]() { poll(); });
    }

    const io::PolledFd* m_fd;
    io::Direction m_direction;
    F m_io;

    enum class State : std::uint8_t {
        Blocked,
        Flight,
        Done,
    };

    State state = State::Blocked;
    bool stop_requested = false;

    const Env* m_env = nullptr;
    std::optional<value_type> m_value = {};
    std::coroutine_handle<> m_cont;

    struct StopFn final {
        TryIoAwaiter* self;

        void operator()() noexcept {

            std::println("TryIoAwaiter::StopFn");

            self->stop_requested = true;
            self->m_fd->m_registration->callback(self->m_direction, {});
            // need to interrupt the reactor...
            expect(self->m_fd->m_reactor->interrupt(), "fuck");
        }
    };

    std::optional<std::stop_callback<StopFn>> m_stop_fn;
};

inline Co<expected<PolledFd>> accept(PolledFd& fd) {
    auto accepted_fd_res = co_await io::TryIoAwaiter{fd, io::Direction::Read,
                                                     [&]() { return adoptSysFd(::accept(fd.fd(), nullptr, nullptr)); }};

    if (!accepted_fd_res.has_value()) {
        co_return unexpected{accepted_fd_res.error()};
    }

    co_return co_await PolledFd::create(std::move(accepted_fd_res).value());
}

} // namespace fastipc::io
