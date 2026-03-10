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
#include <exception>
#include <memory>
#include <stop_token>
#include <system_error>
#include "co/coroutine.hxx"
#include "io/context.hxx"
#include "fd.hxx"
#include "reactor.hxx"
#include "result.hxx"

namespace fastipc::io {

constexpr bool is_error_blocking(std::error_code error) noexcept {
    return error == std::errc::operation_would_block || error == std::errc::resource_unavailable_try_again;
}

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

    static expected<PolledFd> create(Fd fd) noexcept {
        return create(std::move(fd), Runtime::singleton().reactor());
    }

    static expected<PolledFd> create(Fd fd, Reactor& reactor) noexcept {
        return setBlocking(fd, false)
            .and_then([&]() { return reactor.registerFd(fd); })
            .transform([&](auto* registration) { return PolledFd{std::move(fd), registration, reactor}; });
    }

    [[nodiscard]] constexpr const int& fd() const noexcept { return m_fd.fd(); }
    [[nodiscard]] Reactor& reactor() const noexcept { return *m_reactor; }

  private:
    template <class>
    friend class TryIoSender;

    PolledFd(Fd fd, Reactor::Registration* registration, Reactor& reactor) noexcept
        : m_fd{std::move(fd)}, m_registration{registration}, m_reactor{&reactor} {}

    Fd m_fd;
    Reactor::Registration* m_registration;
    Reactor* m_reactor;
};

template <class F>
class TryIoSender final {

  public:
    using result_type = std::invoke_result_t<F>;

    using value_type = result_type;

    explicit TryIoSender(const io::PolledFd& fd, io::Direction direction, std::stop_token stop_token, F io)
        : m_fd{&fd}, m_direction{direction}, m_stop_token{std::move(stop_token)}, m_io{std::move(io)} {}

    template <class R>
    class OperationState {
      public:
        OperationState(R&& receiver, const io::PolledFd& fd, io::Direction direction, std::stop_token stop_token, F io)
            : m_fd{&fd}, m_direction{direction}, m_stop_token{std::move(stop_token)}, m_io{std::move(io)},
              m_receiver{std::move(receiver)} {}

        OperationState(const OperationState&) noexcept = delete;
        OperationState& operator=(const OperationState&) noexcept = delete;

        OperationState(OperationState&& it) noexcept = default;
        OperationState& operator=(OperationState&& rhs) noexcept = default;

        ~OperationState() = default;

        void start() {
            if (m_stop_token.stop_requested()) {
                set_stopped();
                return;
            }

            m_stop_fn = std::make_unique<std::stop_callback<StopFn>>(m_stop_token, StopFn{this});
            poll();
        }

      private:
        void poll() {
            if (m_state != State::Blocked) {
                return;
            }

            if (m_stop_token.stop_requested()) {
                set_stopped();
                return;
            }

            auto res = m_io();

            if (res.has_value()) {
                set_value(std::move(res));
                return;
            }

            if (!is_error_blocking(res.error())) {
                set_value(std::move(res));
                return;
            }

            m_state = State::Blocked;
            m_fd->m_registration->callback(m_direction, [this]() { poll(); });
        }

        void set_value(result_type value) {
            m_stop_fn.reset();

            m_state = State::Done;
            m_receiver.set_value(std::move(value));
        }

        void set_stopped() {
            m_stop_fn.reset();

            m_state = State::Stopped;
            m_receiver.set_exception(std::make_exception_ptr(co::StoppedException{}));
        }

        const io::PolledFd* m_fd;
        io::Direction m_direction;
        std::stop_token m_stop_token;
        F m_io;

        R m_receiver;

        enum class State : std::uint8_t {
            Blocked,
            Done,
            Stopped,
        };

        State m_state = State::Blocked;

        struct StopFn final {
            OperationState* self;

            void operator()() noexcept { self->m_fd->m_registration->callback(self->m_direction, {}); }
        };

        // cb is not moveable..
        std::unique_ptr<std::stop_callback<StopFn>> m_stop_fn{};
    };

    template <class R>
    OperationState<R> connect(R&& receiver) && {
        return OperationState{std::forward<R>(receiver), *m_fd, m_direction, std::move(m_stop_token), std::move(m_io)};
    }

  private:
    const io::PolledFd* m_fd;
    io::Direction m_direction;
    std::stop_token m_stop_token;
    F m_io;
};

inline co::Co<expected<PolledFd>> accept(PolledFd& fd, std::stop_token stop_token = {}) {
    auto accepted_fd_res = co_await io::TryIoSender{fd, io::Direction::Read, std::move(stop_token),
                                                    [&]() { return adoptSysFd(::accept(fd.fd(), nullptr, nullptr)); }};

    if (!accepted_fd_res.has_value()) {
        co_return unexpected{accepted_fd_res.error()};
    }

    co_return PolledFd::create(std::move(accepted_fd_res).value(), fd.reactor());
}

[[nodiscard]] inline co::Co<expected<std::size_t>> aread(PolledFd& fd, std::span<std::byte> buf,
                                                         std::stop_token stop_token = {}) {
    co_return co_await io::TryIoSender{fd, io::Direction::Read, std::move(stop_token), [&]() { return read(fd, buf); }};
}

[[nodiscard]] inline co::Co<expected<std::size_t>> asendmsg(PolledFd& fd, ::msghdr& buf, int flags,
                                                            std::stop_token stop_token = {}) {
    co_return co_await io::TryIoSender{fd, io::Direction::Read, std::move(stop_token),
                                       [&]() { return sysVal(::sendmsg(fd.fd(), &buf, flags)); }};
}

} // namespace fastipc::io
