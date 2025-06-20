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
#include <optional>
#include <print>
#include <utility>

namespace fastipc::co {

template <class T, class Env>
class Promise;

template <class T, class Env>
class CoAwaiter;

template <class Env>
class EnvAwaiter final {
  public:
    bool await_ready() noexcept { return false; }

    template <class T>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T, Env>> cont) {
        m_env = &cont.promise().env();

        return cont;
    }

    Env& await_resume() noexcept { return *m_env; }

  private:
    Env* m_env = nullptr;
};

struct GetEnv final {};

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

    ~Co() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    void resume() { m_handle.resume(); }

    void env(Env* env) {
        m_handle.promise().m_env = env;
    }

    CoAwaiter<T, Env> operator co_await() && noexcept;

  private:
    friend class CoAwaiter<T, Env>;
    friend class Promise<T, Env>;

    handle_type m_handle;
};

template <class T, class Env>
class FinalAwaiter final {
  public:
    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T, Env>> cont) { return cont.promise().m_cont; }

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

    FinalAwaiter<T, Env> final_suspend() noexcept { return {}; }

    void return_value(T&& value) noexcept { m_value = std::move(value); }
    // void return_void() {}

    void unhandled_exception() noexcept { m_exception = std::current_exception(); }

    // void unhandled_stopped() noexcept { m_stopped = true; }

    Env& env() {
        assert(m_env != nullptr);
        return *m_env;
    }

    EnvAwaiter<Env> await_transform(GetEnv) { return EnvAwaiter<Env>{}; }

    template <class A>
    decltype(auto) await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }

  private:
    friend class Co<T, Env>;
    friend class CoAwaiter<T, Env>;
    friend class FinalAwaiter<T, Env>;

    Env* m_env;
    std::optional<T> m_value;
    std::exception_ptr m_exception;
    std::coroutine_handle<> m_cont = std::noop_coroutine();
    // bool m_stopped = false;
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

        if (promise.m_value.has_value()) {
            return std::move(promise.m_value).value();
        }

        if (promise.m_exception) {
            std::rethrow_exception(std::move(promise.m_exception));
        }

        assert(false);
    }

  private:
    Co<T, Env> m_co;
};

template <class T, class Env>
CoAwaiter<T, Env> Co<T, Env>::operator co_await() && noexcept {
    return CoAwaiter<T, Env>{std::move(*this)};
}

} // namespace fastipc
