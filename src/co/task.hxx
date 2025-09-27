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
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <stop_token>
#include "coroutine.hxx"

namespace fastipc::co {

struct Unit final {};

template <class T, class Env>
class Task : public std::enable_shared_from_this<Task<T, Env>> {
  public:
    Task(Co<T, Env> co, const Env& env) : m_stop_cb{m_stop_source.get_token(), Canary{}}, m_co_run{std::move(co)}, m_env{env} {
        // think about if we want to propagate the stop request from parent env
        m_env.stop_token = m_stop_source.get_token();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

    [[nodiscard]] bool completed() const noexcept { return m_done; }

    void abort() {
        m_stop_source.request_stop();
    }

    ~Task() noexcept = default;

    void start() {
        auto& promise = m_co_run.m_handle.promise();

        promise.m_env = &m_env;

        m_op = Op{std::enable_shared_from_this<Task>::shared_from_this()};
        promise.m_cont = &m_op.value();

        m_env.scheduler->schedule([this]() { m_co_run.m_handle.resume(); });
    }

  private:
    template <class, class>
    friend class JoinHandle;

    struct Op : public Receiver<T> {
        std::shared_ptr<Task> m_self;

        explicit Op(std::shared_ptr<Task> self) : Receiver<T>{}, m_self{self} {}

        void set_value(T value) noexcept override {
            assert(m_self);

            auto self = std::move(m_self);
            self->m_done = true;
            self->m_value = std::move(value);

            if (self->m_cont) {
                self->m_env.scheduler->schedule([cont = self->m_cont]() { cont.resume(); });
            }
        }

        void set_exception(std::exception_ptr exc) noexcept override {
            auto self = std::move(m_self);
            self->m_done = true;

            std::rethrow_exception(std::move(exc)); // wrong
        }

        void set_stopped() noexcept override {
            auto self = std::move(m_self);
            self->m_done = true;

            std::terminate();
        }
    };

    struct Canary {
        void operator()() {
            std::println("task abort requested");
        }
    };

    std::stop_source m_stop_source;
    std::stop_callback<Canary> m_stop_cb;
    bool m_done = false;
    std::optional<T> m_value{};

    std::optional<Op> m_op;

    Co<T, Env> m_co_run;
    Env m_env;


    // I think this also needs to become a Receiver..
    std::coroutine_handle<> m_cont;
};

// template <class Env>
// class Task<void, Env> final : public Receiver<void>, public std::enable_shared_from_this<Task<void, Env>> {
//   public:
//     Task(Co<void, Env> co, const Env& env) : m_co_run{std::move(co)}, m_env{&env} {}

//     Task(const Task&) = delete;
//     Task& operator=(const Task&) = delete;

//     Task(Task&&) = default;
//     Task& operator=(Task&&) = default;

//     [[nodiscard]] bool completed() const noexcept { return m_done; }

//     ~Task() noexcept override = default;

//     void start() {
//         auto& promise = m_co_run.m_handle.promise();

//         promise.m_env = m_env;
//         promise.m_cont = this;

//         m_env->scheduler->schedule([this]() { m_co_run.m_handle.resume(); });

//         m_lifetime = std::enable_shared_from_this<Task>::shared_from_this();
//     }

//     void set_value() noexcept override {
//         auto _ = std::move(m_lifetime);
//         m_done = true;

//         if (m_cont) {
//             m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
//         }
//     }

//     void set_exception(std::exception_ptr exc) noexcept override {
//         auto _ = std::move(m_lifetime);
//         m_done = true;
//         std::rethrow_exception(std::move(exc));
//     }

//     void set_stopped() noexcept override {
//         auto _ = std::move(m_lifetime);
//         m_done = true;
//         if (m_cont) {
//             m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
//         }
//     }

//   private:
//     template <class, class>
//     friend class JoinHandle;

//     Co<void, Env> m_co_run;
//     const Env* m_env;

//     bool m_done = false;

//     std::coroutine_handle<> m_cont;
//     std::shared_ptr<Task> m_lifetime;
// };

template <class T, class Env>
class JoinHandle final {

  public:
    explicit JoinHandle(std::shared_ptr<Task<T, Env>> task) noexcept : m_task{std::move(task)} {}

    [[nodiscard]] bool completed() const noexcept { return m_task->completed(); }
    void abort() noexcept { m_task->abort(); }

    bool await_ready() noexcept { return completed(); }

    void await_suspend(std::coroutine_handle<> cont) { m_task->m_cont = cont; }

    T await_resume() { return std::move(m_task->m_value).value(); }

  private:
    std::shared_ptr<Task<T, Env>> m_task;
};

template <class T, class Env>
JoinHandle<T, Env> spawn(Co<T, Env> co, const Env& env) {
    auto task = std::make_shared<Task<T, Env>>(std::move(co), env);
    task->start();

    return JoinHandle{std::move(task)};
}

template <class T, class Env>
Co<JoinHandle<T, Env>, Env> spawn(Co<T, Env> co) {
    co_return spawn(std::move(co), co_await getEnv());
}

} // namespace fastipc::co