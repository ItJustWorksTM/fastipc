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

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include "coroutine.hxx"

namespace fastipc::co {

struct Unit final {};

template <class T, class Env>
class Task final : public Receiver<T>, public std::enable_shared_from_this<Task<T, Env>> {
  public:
    Task(Co<T, Env> co, const Env& env) : m_co_run{std::move(co)}, m_env{&env} { std::println("Task created"); }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

    [[nodiscard]] bool completed() const noexcept { return m_done; }

    ~Task() noexcept override = default;

    void start() {
        auto& promise = m_co_run.m_handle.promise();

        promise.m_env = m_env;
        promise.m_cont = std::enable_shared_from_this<Task<T, Env>>::shared_from_this();

        m_env->scheduler->schedule([this]() { m_co_run.m_handle.resume(); });
    }

    void set_value(T value) noexcept override {
        m_value = std::move(value);
        m_done = true;

        if (m_cont) {
            m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
        }
    }

    void set_exception(std::exception_ptr) noexcept override { std::terminate(); }

  private:
    template <class, class>
    friend class JoinHandle;

    Co<T, Env> m_co_run;
    const Env* m_env;

    bool m_done = false;
    std::optional<T> m_value{};

    std::coroutine_handle<> m_cont;
};

template <class Env>
class Task<void, Env> final : public Receiver<void>, public std::enable_shared_from_this<Task<void, Env>> {
  public:
    Task(Co<void, Env> co, const Env& env) : m_co_run{std::move(co)}, m_env{&env} {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

    [[nodiscard]] bool completed() const noexcept { return m_done; }

    ~Task() noexcept override = default;

    void start() {
        auto& promise = m_co_run.m_handle.promise();

        promise.m_env = m_env;
        promise.m_cont = std::enable_shared_from_this<Task<void, Env>>::shared_from_this();

        m_env->scheduler->schedule([this]() { m_co_run.m_handle.resume(); });
    }

    void set_value() noexcept override {
        m_done = true;

        if (m_cont) {
            m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
        }
    }

    void set_exception(std::exception_ptr exc) noexcept override {
        m_done = true;
        std::rethrow_exception(std::move(exc));
    }

  private:
    template <class, class>
    friend class JoinHandle;

    Co<void, Env> m_co_run;
    const Env* m_env;

    bool m_done = false;

    std::coroutine_handle<> m_cont;
};

template <class T, class Env>
class JoinHandle final {

  public:
    explicit JoinHandle(std::shared_ptr<Task<T, Env>> task) noexcept : m_task{std::move(task)} {}

    [[nodiscard]] bool completed() const noexcept { return m_task->completed(); }

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