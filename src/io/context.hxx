#pragma once
#include <print>
#include "co/scheduler.hxx"
#include "co/task.hxx"
#include "io_env.hxx"
#include "reactor.hxx"

namespace fastipc::io {

template <class F>
void context(F func) {
    auto scheduler = co::Scheduler{};
    auto reactor = expect(Reactor::create());

    Env env{.scheduler = &scheduler, .reactor = &reactor, .stop_token = {}};

    auto task = co::spawn(func(), env);

    while (scheduler.can_run()) {
        while (scheduler.can_run()) {
            scheduler.run();
            std::println("inner loop");
            expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
            std::println("inner loop wake");
        }

        std::println("outer loop");
        expect(reactor.react({}), "failed to react to io events");
        std::println("outer loop wake");

    }

    static_cast<void>(task);
}

} // namespace fastipc::io