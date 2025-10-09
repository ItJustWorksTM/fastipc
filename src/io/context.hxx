#pragma once
#include <exception>
#include <memory>
#include <print>
#include <utility>
#include "co/received.hxx"
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
    void set_exception(std::exception_ptr exc) noexcept { std::rethrow_exception(std::move(exc)); }
    void set_stopped() { std::println("oops stopped"); }

    Env& env() { return m_env; }

    Env m_env;
};


// so in p2300 it seems block_on will supply an in-place scheduler
// which we should probably use for the main function, but then we can introduce this i/o env with a simple receiver?

template <class F>
void context(F func) {
    auto reactor = expect(Reactor::create());
    auto scheduler = co::Scheduler{&reactor};

    Env env{.scheduler = &scheduler, .reactor = &reactor, .stop_token = {}};

    auto op = func().connect(Receiver{env});
    op.start();

    while (true) {
        while (scheduler.can_run()) {
            scheduler.run();
            expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }
        
        expect(reactor.react({}), "failed to react to io events");
    }

    static_cast<void>(op);
}

} // namespace fastipc::io