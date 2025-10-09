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
#include <memory>
#include <stop_token>
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
        auto& reactor = *(co_await co::EnvSender{}).reactor;

        co_return setBlocking(fd, false)
            .and_then([&]() { return reactor.registerFd(fd); })
            .transform([&](auto* registration) { return PolledFd{std::move(fd), registration, reactor}; });
    }

    [[nodiscard]] constexpr const int& fd() const noexcept { return m_fd.fd(); }

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

    template <class>
    using value_type = result_type;

    explicit TryIoSender(const io::PolledFd& fd, io::Direction direction, F io)
        : m_fd{&fd}, m_direction{direction}, m_io{std::move(io)} {}

    template <class R>
    class OperationState {
      public:
        OperationState(R&& receiver, const io::PolledFd& fd, io::Direction direction, F io)
            : m_fd{&fd}, m_direction{direction}, m_io{std::move(io)}, m_receiver{std::move(receiver)} {}

        OperationState(const OperationState&) noexcept = delete;
        OperationState& operator=(const OperationState&) noexcept = delete;

        OperationState(OperationState&& it) noexcept = default;
        OperationState& operator=(OperationState&& rhs) noexcept = default;

        ~OperationState() = default;

        void start() {
            std::stop_token st = m_receiver.env().stop_token;

            if (st.stop_requested()) {
                set_stopped();
                return;
            }

            m_stop_fn = std::make_unique<std::stop_callback<StopFn>>(std::move(st), StopFn{this});

            schedule_poll();
        }

      private:
        void poll() {
            if (stop_requested) {
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

        void set_value(result_type res) {
            m_stop_fn.reset();

            state = State::Done;
            m_receiver.env().scheduler->schedule(
                [&, value = std::move(res)]() mutable { m_receiver.set_value(std::move(value)); });
        }

        void set_stopped() {
            m_stop_fn.reset();

            state = State::Done;

            m_receiver.env().scheduler->schedule([&]() { m_receiver.set_stopped(); });
        }

        void schedule_poll() noexcept {
            if (state != State::Blocked) {
                return;
            }

            state = State::Flight;
            m_receiver.env().scheduler->schedule([this]() { poll(); });
        }

        const io::PolledFd* m_fd;
        io::Direction m_direction;
        F m_io;

        R m_receiver;

        enum class State : std::uint8_t {
            Blocked,
            Flight,
            Done,
        };

        State state = State::Blocked;
        bool stop_requested = false;

        struct StopFn final {
            OperationState* self;

            void operator()() noexcept {
                self->stop_requested = true;
                self->m_fd->m_registration->callback(self->m_direction, {});
            }
        };

        // cb is not moveable..
        std::unique_ptr<std::stop_callback<StopFn>> m_stop_fn{};
    };

    template <class R>
    OperationState<R> connect(R&& receiver) && {
        return OperationState{std::forward<R>(receiver), *m_fd, m_direction, std::move(m_io)};
    }

  private:
    const io::PolledFd* m_fd;
    io::Direction m_direction;
    F m_io;
};

inline Co<expected<PolledFd>> accept(PolledFd& fd) {
    auto accepted_fd_res = co_await io::TryIoSender{fd, io::Direction::Read,
                                                    [&]() { return adoptSysFd(::accept(fd.fd(), nullptr, nullptr)); }};

    if (!accepted_fd_res.has_value()) {
        co_return unexpected{accepted_fd_res.error()};
    }

    co_return co_await PolledFd::create(std::move(accepted_fd_res).value());
}

[[nodiscard]] inline Co<expected<std::size_t>> aread(PolledFd& fd, std::span<std::byte> buf) noexcept {
    co_return co_await io::TryIoSender{fd, io::Direction::Read, [&]() { return read(fd, buf); }};
}

} // namespace fastipc::io
