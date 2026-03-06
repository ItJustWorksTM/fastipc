#pragma once
#include <exception>
#include <memory>
#include <print>
#include <utility>
#include "co/received.hxx"
#include "co/scheduler.hxx"
#include "co/task.hxx"
#include "reactor.hxx"

namespace fastipc::io {

template <class F>
auto block_on(F func) {
    auto reactor = expect(Reactor::create());
    auto scheduler = co::Scheduler{&reactor};

    auto task = co::spawn(func());

    while (!task.completed()) {
        while (scheduler.can_run()) {
            scheduler.run();
            expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }
        
        expect(reactor.react({}), "failed to react to io events");
    }

    return task.get();
}

} // namespace fastipc::io