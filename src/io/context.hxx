#pragma once
#include "co/scheduler.hxx"
#include "co/task.hxx"
#include "io_env.hxx"
#include "reactor.hxx"

namespace fastipc::io {


struct Receiver {
    template <class T>
    void set_value(T value) {
        std::println("complete: {}", value);
    }
    void set_exception(std::exception_ptr) { std::println("oops exception"); }
    void set_stopped() { std::println("oops stopped"); }

    Env& env() { return *m_env; }

    Env* m_env;
};

template <class F>
void context(F func) {
    auto scheduler = co::Scheduler{};
    auto reactor = expect(Reactor::create());

    Env env{.scheduler = &scheduler, .reactor = &reactor, .stop_token = {}};

    auto op = func().connect(Receiver{&env});
    
    op.start();

    while (scheduler.can_run()) {
        while (scheduler.can_run()) {
            scheduler.run();
            expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }

        expect(reactor.react({}), "failed to react to io events");
    }

    static_cast<void>(op);
}

} // namespace fastipc::io