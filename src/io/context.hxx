/*
 *  context.hxx
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

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include "co/received.hxx"
#include "co/scheduler.hxx"
#include "io/result.hxx"
#include "reactor.hxx"

namespace fastipc::io {

class Runtime final {
    explicit Runtime(Reactor reactor)
        : m_reactor{std::make_unique<Reactor>(std::move(reactor))},
          m_scheduler{std::make_unique<co::Scheduler>(m_reactor.get())} {}

  public:
    static expected<Runtime> create() noexcept {
        auto reactor_res = Reactor::create();

        if (!reactor_res.has_value()) {
            return unexpected{std::move(reactor_res).error()};
        }

        return expected<Runtime>{Runtime{std::move(reactor_res).value()}};
    }

    static Runtime& singleton() noexcept { return *active_singleton(); }

    template <class F>
    auto block_on(F func) noexcept {
        active_singleton() = this;

        using S = std::remove_cvref_t<decltype(func())>;

        co::Received<typename S::value_type> received{};

        struct Receiver {
            co::Received<typename S::value_type>* received;
            Reactor* reactor;

            void set_value(typename S::value_type value) {
                received->set_value(std::move(value));
                expect(reactor->interrupt());
            }
            void set_exception(std::exception_ptr exc) {
                received->set_exception(std::move(exc));
                expect(reactor->interrupt());
            }
        };

        auto op = func().connect(Receiver{&received, m_reactor.get()});
        op.start();

        for (;;) {
            while (m_scheduler->can_run()) {
                m_scheduler->run();
                expect(m_reactor->react(std::chrono::milliseconds{0}), "failed to react to io events");

                if (received.has_value()) {
                    return std::move(received).consume();
                }
            }

            if (received.has_value()) {
                return std::move(received).consume();
            }

            expect(m_reactor->react({}), "failed to react to io events");
        }
    }

    Reactor& reactor() noexcept { return *m_reactor; }
    co::Scheduler& scheduler() noexcept { return *m_scheduler; }

  private:
    static Runtime*& active_singleton() {
        static Runtime* runtime{};

        return runtime;
    }

    std::unique_ptr<Reactor> m_reactor;
    std::unique_ptr<co::Scheduler> m_scheduler;
};

class YieldSender final {
  public:
    using value_type = void;

    explicit YieldSender(co::Scheduler& scheduler) : m_scheduler{&scheduler} {}

    template <class R>
    auto connect(R&& receiver) {
        struct OperationState final {
            R receiver;
            co::Scheduler* scheduler;

            void start() {
                scheduler->schedule([this]() { receiver.set_value(); });
            }
        };

        return OperationState{std::forward<R>(receiver), m_scheduler};
    }

  private:
    co::Scheduler* m_scheduler;
};

inline YieldSender yield() { return YieldSender{Runtime::singleton().scheduler()}; }

} // namespace fastipc::io
