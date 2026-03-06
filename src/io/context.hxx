#pragma once
#include <exception>
#include <memory>
#include <print>
#include <utility>
#include "co/received.hxx"
#include "co/scheduler.hxx"
#include "co/task.hxx"
#include "io/result.hxx"
#include "reactor.hxx"

namespace fastipc::io {

class Runtime final {
    explicit Runtime(Reactor reactor)
        : m_reactor{std::move(reactor)}, m_scheduler{std::make_unique<co::Scheduler>(&m_reactor)} {}

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

        auto task = co::spawn(func());

        while (!task.completed()) {
            while (m_scheduler->can_run()) {
                m_scheduler->run();
                expect(m_reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
            }

            expect(m_reactor.react({}), "failed to react to io events");
        }

        return task.get();
    }

    Reactor& reactor() noexcept { return m_reactor; }
    co::Scheduler& scheduler() noexcept { return *m_scheduler; }

  private:
    static Runtime*& active_singleton() {
        static Runtime* runtime{};

        return runtime;
    }

    Reactor m_reactor;
    std::unique_ptr<co::Scheduler> m_scheduler;
};

} // namespace fastipc::io