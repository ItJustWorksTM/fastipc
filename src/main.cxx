/*
 *  main.cxx
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

#include "co/task.hxx"
#include "io/io_env.hxx"
#include "tower.hxx"

namespace fastipc {
namespace {

io::Co<int> main() {
    auto tower = co_await fastipc::Tower::create("fastipcd");
    co_await tower.run();

    co_return 0;
}

} // namespace
} // namespace fastipc

int main() {
    auto scheduler = fastipc::co::Scheduler{};
    auto reactor = fastipc::expect(fastipc::io::Reactor::create());

    fastipc::io::Env env{.scheduler = &scheduler, .reactor = &reactor};

    // block on?
    fastipc::co::spawn(fastipc::main(), env);

    while (scheduler.can_run()) {
        while (scheduler.can_run()) {
            scheduler.run();
            fastipc::expect(reactor.react(std::chrono::milliseconds{0}), "failed to react to io events");
        }

        fastipc::expect(reactor.react({}), "failed to react to io events");
    }
}
