/*
 *  scheduler.hxx
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

#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include "io/reactor.hxx"

namespace fastipc::co {

class Scheduler final {
  public:
    explicit Scheduler(io::Reactor* reactor = nullptr) : m_reactor(reactor) {}

    // optimize by making a custom callable interface
    void schedule(std::function<void()> fn) {
        auto lock = std::scoped_lock{m_child_lock};

        m_queue.push(std::move(fn));

        // make this smart..
        if (m_reactor != nullptr) {
            expect(m_reactor->interrupt(), "failed to interrupt reactor");
        }
    }

    [[nodiscard]] bool can_run() const noexcept {
        auto lock = std::scoped_lock{m_child_lock};

        return !m_queue.empty();
    }

    void run() noexcept {
        auto lock = std::scoped_lock{m_child_lock};

        const auto queued = m_queue.size();

        for (std::size_t i = 0; i < queued; ++i) {
            std::move(m_queue.front())();
            m_queue.pop();
        }
    }

  private:
    mutable std::recursive_mutex m_child_lock;
    std::queue<std::function<void()>> m_queue;

    io::Reactor* m_reactor;
};

} // namespace fastipc::co