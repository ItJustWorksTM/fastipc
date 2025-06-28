/*
 *  coroutine.hxx
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

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <exception>
#include <memory>
#include <print>
#include <type_traits>
#include <utility>
#include <variant>
#include "received.hxx"
#include "visitor.hxx"

namespace fastipc::co {

template <class T>
concept valueless = std::is_void_v<T>;

template <class T, class Env>
class Promise;

template <class T, class Env>
class CoAwaiter;

template <class T, class Env>
class Task;

template <class Env>
class EnvAwaiter final {
  public:
    bool await_ready() noexcept { return false; }

    template <class T>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T, Env>> cont) {
        m_env = &cont.promise().env();

        return cont;
    }

    const Env& await_resume() const noexcept { return *m_env; }

  private:
    const Env* m_env = nullptr;
};

struct GetEnv final {};

constexpr GetEnv getEnv() noexcept { return {}; }

template <class T, class Env>
class [[nodiscard]] Co final {
  public:
    using promise_type = Promise<T, Env>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Co(handle_type handle) : m_handle{std::move(handle)} {}

    Co(Co&) noexcept = delete;
    Co& operator=(Co&) noexcept = delete;

    Co(Co&& it) noexcept : m_handle{std::exchange(it.m_handle, {})} {}
    Co& operator=(Co&& rhs) noexcept {
        auto other = Co{std::move(rhs)};

        std::swap(m_handle, other.m_handle);

        return *this;
    }

    ~Co() noexcept {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    void env(const Env* env) { m_handle.promise().m_env = env; }

    CoAwaiter<T, Env> operator co_await() && noexcept;

  private:
    friend class CoAwaiter<T, Env>;
    friend class Promise<T, Env>;
    friend class Task<T, Env>;

    handle_type m_handle;
};

template <class T>
class Receiver {
  public:
    Receiver() = default;

    Receiver(Receiver&&) noexcept = default;
    Receiver& operator=(Receiver&&) noexcept = default;

    Receiver(const Receiver&) noexcept = default;
    Receiver& operator=(const Receiver&) noexcept = default;

    virtual ~Receiver() = default;

    virtual void set_value(T value) noexcept = 0;
    virtual void set_exception(std::exception_ptr error) noexcept = 0;
};

template <>
class Receiver<void> {
  public:
    Receiver() = default;

    Receiver(Receiver&&) noexcept = default;
    Receiver& operator=(Receiver&&) noexcept = default;

    Receiver(const Receiver&) noexcept = default;
    Receiver& operator=(const Receiver&) noexcept = default;

    virtual ~Receiver() = default;

    virtual void set_value() noexcept = 0;
    virtual void set_exception(std::exception_ptr error) noexcept = 0;
};

template <class T, class Env>
class FinalAwaiter final {
  public:
    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T, Env>> completed) {
        assert(completed.promise().m_received.has_value());
        auto& promise = completed.promise();

        return match(
            std::move(promise.m_cont), [](std::coroutine_handle<> cont) -> std::coroutine_handle<> { return cont; },
            [&promise](std::shared_ptr<Receiver<T>> receiver) -> std::coroutine_handle<> {
                std::move(promise.m_received).forward(*receiver);

                return std::noop_coroutine();
            });
    }

    void await_resume() noexcept {}
};

template <class T, class Env>
class Promise final {
  public:
    Promise() = default;

    Promise(Promise&&) noexcept = delete;
    Promise& operator=(Promise&&) noexcept = delete;

    Promise(const Promise&) noexcept = delete;
    Promise& operator=(const Promise&) noexcept = delete;

    ~Promise() noexcept = default;

    Co<T, Env> get_return_object() { return Co<T, Env>{std::coroutine_handle<Promise>::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }

    FinalAwaiter<T, Env> final_suspend() noexcept {
        assert(m_received.has_value());
        return {};
    }

    void return_value(T value) noexcept { m_received.set_value(std::move(value)); }
    void unhandled_exception() noexcept { m_received.set_exception(std::current_exception()); }

    template <class A>
    decltype(auto) await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }

    EnvAwaiter<Env> await_transform(GetEnv) { return EnvAwaiter<Env>{}; }

    const Env& env() const { return *m_env; }

  private:
    friend class Co<T, Env>;
    friend class CoAwaiter<T, Env>;
    friend class FinalAwaiter<T, Env>;
    friend class Task<T, Env>;

    Received<T> m_received;

    std::variant<std::coroutine_handle<>, std::shared_ptr<Receiver<T>>> m_cont = std::noop_coroutine();

    const Env* m_env;
};

template <class Env>
class Promise<void, Env> final {
  public:
    Promise() = default;

    Promise(Promise&&) noexcept = delete;
    Promise& operator=(Promise&&) noexcept = delete;

    Promise(const Promise&) noexcept = delete;
    Promise& operator=(const Promise&) noexcept = delete;

    ~Promise() noexcept = default;

    Co<void, Env> get_return_object() { return Co<void, Env>{std::coroutine_handle<Promise>::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }

    FinalAwaiter<void, Env> final_suspend() noexcept {
        assert(m_received.has_value());
        return {};
    }

    void return_void() noexcept { m_received.set_value(); }

    void unhandled_exception() noexcept { m_received.set_exception(std::current_exception()); }

    template <class A>
    decltype(auto) await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }

    EnvAwaiter<Env> await_transform(GetEnv) { return EnvAwaiter<Env>{}; }

    const Env& env() const { return *m_env; }

  private:
    friend class Co<void, Env>;
    friend class CoAwaiter<void, Env>;
    friend class FinalAwaiter<void, Env>;
    friend class Task<void, Env>;

    Received<void> m_received;
    std::variant<std::coroutine_handle<>, std::shared_ptr<Receiver<void>>> m_cont = std::noop_coroutine();

    const Env* m_env;
};

template <class T, class Env>
class [[nodiscard]] CoAwaiter final {
  public:
    explicit CoAwaiter(Co<T, Env> co) noexcept : m_co{std::move(co)} {}

    CoAwaiter(CoAwaiter&&) noexcept = delete;
    CoAwaiter& operator=(CoAwaiter&&) noexcept = delete;

    CoAwaiter(const CoAwaiter&) noexcept = delete;
    CoAwaiter& operator=(const CoAwaiter&) noexcept = delete;

    ~CoAwaiter() noexcept = default;

    bool await_ready() noexcept { return false; }

    template <class S>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<S, Env>> cont) {
        m_co.m_handle.promise().m_env = &cont.promise().env();
        m_co.m_handle.promise().m_cont = cont;

        return m_co.m_handle;
    }

    T await_resume() {
        auto co = std::move(m_co);

        auto& promise = co.m_handle.promise();

        assert(promise.m_received.has_value());
        return std::move(promise.m_received).consume();
    }

  private:
    Co<T, Env> m_co;
};

template <class T, class Env>
CoAwaiter<T, Env> Co<T, Env>::operator co_await() && noexcept {
    return CoAwaiter<T, Env>{std::move(*this)};
}

} // namespace fastipc::co
