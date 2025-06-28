#pragma once
#include "co/scheduler.hxx"
#include "io_env.hxx"
#include "reactor.hxx"

namespace fastipc::io {

template <class F>
void context(F func) {
    auto scheduler = co::Scheduler{};
    auto reactor = expect(Reactor::create());

    Env env{.scheduler = &scheduler, .reactor = &reactor};

    auto task = co::spawn(func(), env);

    while (scheduler.can_run()) {
        while (scheduler.can_run()) {
            scheduler.run();
            expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }

        expect(reactor.react({}), "failed to react to io events");
    }

    static_cast<void>(task);
}

} // namespace fastipc::io