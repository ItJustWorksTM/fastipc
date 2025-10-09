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
#include <stop_token>
#include <type_traits>
#include <utility>
#include "co/received.hxx"

namespace fastipc::co {

struct Unit final {};

template <class T>
struct State {

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    virtual ~State() = default;

    Received<T> received{};
};

template <class T>
struct JoinHandle {
    [[nodiscard]] bool completed() const noexcept { return state->received.has_value(); }

    void abort() noexcept {
        // TODO
    }

    template <class>
    using value_type = T;

    template <class R>
    auto connect(R&& receiver) && {
        struct OperationState {
            R receiver;
            std::shared_ptr<State<T>> state;

            void start() {
                // TODO: put receiver on shared state
                // OR check if received is already populated and immediately forward it
            }
        };

        return OperationState{std::forward<R>(receiver), std::move(state)};
    }

    // dtor slice?
    std::shared_ptr<State<T>> state;
};

template <class S>
struct SpawnSender {

    template <class Env>
    using task_value_type = typename S::template value_type<Env>;

    template <class Env>
    using value_type = JoinHandle<task_value_type<Env>>;

    template <class R>
    struct OperationState {
        R receiver;
        S sender;

        using Env = std::remove_cvref_t<decltype(std::declval<R>().env())>;
        using T = task_value_type<Env>;

        void start() {

            struct StateImpl : State<T> {
                explicit StateImpl(Env env) : State<T>{}, env{env} {}

                StateImpl(const StateImpl&) = delete;
                StateImpl& operator=(const StateImpl&) = delete;
                StateImpl(StateImpl&&) = delete;
                StateImpl& operator=(StateImpl&&) = delete;

                ~StateImpl() override = default;

                struct Receiver {
                    std::shared_ptr<StateImpl> state;

                    void set_value(T value) { state->received.set_value(std::move(value)); }
                    void set_exception(std::exception_ptr exc) { state->received.set_exception(std::move(exc)); }
                    void set_stopped() { state->received.set_stopped(); };

                    Env& env() { return state->env; }
                };

                using operation_state_type = decltype(std::declval<S>().connect(std::declval<Receiver>()));

                Env env;
                std::optional<operation_state_type> operation_state{};
            };

            // TODO: add stop_source
            auto state = std::make_shared<StateImpl>(receiver.env());
            state->operation_state.emplace(std::move(sender).connect(typename StateImpl::Receiver{state})).start();

            receiver.set_value(JoinHandle<T>{std::move(state)});
        }
    };

    template <class R>
    auto connect(R&& receiver) && {
        return OperationState{std::forward<R>(receiver), std::move(sender)};
    }

    S sender;
};

template <class S>
SpawnSender<S> spawn(S&& sender) {
    return SpawnSender<S>{std::forward<S>(sender)};
}

} // namespace fastipc::co