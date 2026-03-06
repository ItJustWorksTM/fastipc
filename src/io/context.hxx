#pragma once
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include "co/received.hxx"
#include "co/scheduler.hxx"
#include "co/task.hxx"
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

} // namespace fastipc::io