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
#include <queue>

namespace fastipc::co {

class Scheduler final {
  public:
    // TODO: use std::function_ref
    void schedule(std::function<void()> fn) { m_queue.push(std::move(fn)); }

    [[nodiscard]] bool can_run() const noexcept { return !m_queue.empty(); }

    void run() noexcept {
        const auto queued = m_queue.size();

        for (std::size_t i = 0; i < queued; ++i) {
            std::move(m_queue.front())();
            m_queue.pop();
        }
    }

  private:
    std::queue<std::function<void()>> m_queue;
};

} // namespace fastipc::io