/*
 *  task.hxx
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

#include <cassert>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include "co/received.hxx"

namespace fastipc::co {

struct Unit final {};

class Listener {
  public:
    Listener() = default;

    Listener(Listener&&) noexcept = default;
    Listener& operator=(Listener&&) noexcept = default;

    Listener(const Listener&) noexcept = default;
    Listener& operator=(const Listener&) noexcept = default;

    virtual ~Listener() = default;

    virtual void notify() = 0;
};

template <class T>
struct State {
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    virtual ~State() = default;

    Received<T> received{};
    Listener* listener{};
};

template <class T>
struct JoinHandle final {
    [[nodiscard]] bool completed() const noexcept { return state->received.has_value(); }

    using value_type = T;

    template <class R>
    auto connect(R&& receiver) && {
        struct OperationState final : public Listener {
            R receiver;
            std::shared_ptr<State<T>> state;

            OperationState(R&& receiver, std::shared_ptr<State<T>> state)
                : receiver{std::move(receiver)}, state{std::move(state)} {}

            OperationState(OperationState&&) noexcept = default;
            OperationState& operator=(OperationState&&) noexcept = default;

            OperationState(const OperationState&) noexcept = default;
            OperationState& operator=(const OperationState&) noexcept = default;

            ~OperationState() override = default;

            void notify() override {
                if (state->received.has_value()) {
                    state->listener = nullptr;
                    std::move(state->received).forward(receiver);
                }
            }

            void start() {
                if (state->received.has_value()) {
                    std::move(state->received).forward(receiver);
                } else {
                    state->listener = this;
                }
            }
        };

        return OperationState{std::forward<R>(receiver), std::move(state)};
    }

    // dtor slice?
    std::shared_ptr<State<T>> state;
};

template <class S>
JoinHandle<typename S::value_type> spawn(S&& sender) {
    struct StateImpl : State<typename S::value_type> {
        explicit StateImpl() : State<typename S::value_type>{} {}

        StateImpl(const StateImpl&) = delete;
        StateImpl& operator=(const StateImpl&) = delete;
        StateImpl(StateImpl&&) = delete;
        StateImpl& operator=(StateImpl&&) = delete;

        ~StateImpl() override = default;

        struct Receiver {
            std::shared_ptr<StateImpl> state;

            void set_value(typename S::value_type value) {
                state->received.set_value(std::move(value));

                if (state->listener) {
                    state->listener->notify();
                }
            }
            void set_exception(std::exception_ptr exc) {
                state->received.set_exception(std::move(exc));

                if (state->listener) {
                    state->listener->notify();
                }
            }
        };

        using operation_state_type = decltype(std::declval<S>().connect(std::declval<Receiver>()));

        std::optional<operation_state_type> operation_state{};
    };

    auto state = std::make_shared<StateImpl>();
    state->operation_state.emplace(std::forward<S>(sender).connect(typename StateImpl::Receiver{state})).start();

    return JoinHandle<typename S::value_type>{std::move(state)};
}

} // namespace fastipc::co