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
#include <print>
#include <stop_token>
#include <type_traits>
#include <utility>
#include "received.hxx"

namespace fastipc::co {

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
    virtual void set_stopped() noexcept = 0;
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
    virtual void set_stopped() noexcept = 0;
};

class Handler {
  public:
    Handler() = default;

    Handler(Handler&&) noexcept = delete;
    Handler& operator=(Handler&&) noexcept = delete;

    Handler(const Handler&) noexcept = delete;
    Handler& operator=(const Handler&) noexcept = delete;

    virtual ~Handler() = default;

    virtual void unhandled_exception() noexcept = 0;
    virtual void unhandled_stopped() noexcept = 0;
};

template <class T>
concept valueless = std::is_void_v<T>;

template <class T, class Env>
class Promise;

template <class T, class Env>
class CoAwaiter;

template <class T, class Env>
class Task;

template <class Env>
class EnvAwaiter {
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

struct GetEnv {};

constexpr GetEnv getEnv() noexcept { return {}; }

template <class T, class Env>
class [[nodiscard]] Co {
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

template <class T, class Env>
class FinalAwaiter {
  public:
    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T, Env>> completed) {
        assert(completed.promise().m_received.has_value());
        auto& promise = completed.promise();

        std::move(promise.m_received).forward(*promise.m_cont);

        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <class T, class Env>
class Promise : public Handler {
  public:
    Promise() = default;

    Promise(Promise&&) noexcept = delete;
    Promise& operator=(Promise&&) noexcept = delete;

    Promise(const Promise&) noexcept = delete;
    Promise& operator=(const Promise&) noexcept = delete;

    ~Promise() noexcept override = default;

    Co<T, Env> get_return_object() { return Co<T, Env>{std::coroutine_handle<Promise>::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }

    FinalAwaiter<T, Env> final_suspend() noexcept {
        assert(m_received.has_value());
        return {};
    }

    void return_value(T value) noexcept { m_received.set_value(std::move(value)); }
    void unhandled_exception() noexcept override { m_received.set_exception(std::current_exception()); }
    void unhandled_stopped() noexcept override { m_received.set_stopped(); }

    template <class A>
    decltype(auto) await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }

    EnvAwaiter<Env> await_transform(GetEnv) { return EnvAwaiter<Env>{}; }

    const Env& env() const { return *m_env; }
    std::stop_token stop_token() { return m_env->stop_token; }

  private:
    friend class Co<T, Env>;
    friend class CoAwaiter<T, Env>;
    friend class FinalAwaiter<T, Env>;
    friend class Task<T, Env>;

    Received<T> m_received;

    Receiver<T>* m_cont = nullptr;

    const Env* m_env;
};

template <class Env>
class Promise<void, Env> : public Handler {
  public:
    Promise() = default;

    Promise(Promise&&) noexcept = delete;
    Promise& operator=(Promise&&) noexcept = delete;

    Promise(const Promise&) noexcept = delete;
    Promise& operator=(const Promise&) noexcept = delete;

    ~Promise() noexcept override = default;

    Co<void, Env> get_return_object() { return Co<void, Env>{std::coroutine_handle<Promise>::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }

    FinalAwaiter<void, Env> final_suspend() noexcept {
        assert(m_received.has_value());
        return {};
    }

    // So what if we call the receiver directly? why cache it?
    void return_void() noexcept { m_received.set_value(); }
    void unhandled_exception() noexcept override { m_received.set_exception(std::current_exception()); }
    void unhandled_stopped() noexcept override { 
      // this does not trigger final_suspend, so that means we cant rely on it...
      m_received.set_stopped(); }

    template <class A>
    decltype(auto) await_transform(A&& awaitable) noexcept {
        return std::forward<A>(awaitable);
    }

    EnvAwaiter<Env> await_transform(GetEnv) { return EnvAwaiter<Env>{}; }

    const Env& env() const { return *m_env; }
    std::stop_token stop_token() { return m_env->stop_token; }

  private:
    friend class Co<void, Env>;
    friend class CoAwaiter<void, Env>;
    friend class FinalAwaiter<void, Env>;
    friend class Task<void, Env>;

    Received<void> m_received;
    Receiver<void>* m_cont = nullptr;

    const Env* m_env;
};

template <class T, class Env>
class CoReceiver : public Receiver<T> {
  public:
    CoReceiver() : Receiver<T>{} {}
    CoReceiver(std::coroutine_handle<> cont, const Env* env) : m_cont{cont}, m_env{env} {}

    void set_value(T value) noexcept override {
        m_received.set_value(std::move(value));

        m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
    };

    void set_exception(std::exception_ptr error) noexcept override {
        m_received.set_exception(error);

        m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
    };

    // these also need to be scheduled...
    void set_stopped() noexcept override { m_received.set_stopped(); };

    T resume() {
        assert(m_received.has_value());

        return std::move(m_received).consume();
    }

  private:
    Received<T> m_received;
    std::coroutine_handle<> m_cont;
    const Env* m_env;
};

template <class Env>
class CoReceiver<void, Env> : public Receiver<void> {
  public:
    CoReceiver() : Receiver<void>{} {}
    CoReceiver(std::coroutine_handle<> cont, const Env* env) : m_cont{cont}, m_env{env} {}

    void set_value() noexcept override {
        m_received.set_value();

        m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
    };

    void set_exception(std::exception_ptr error) noexcept override {
        m_received.set_exception(error);

        m_env->scheduler->schedule([cont = m_cont]() { cont.resume(); });
    };

    void set_stopped() noexcept override { 
      m_received.set_stopped();
      
      // TODO: also needs to be scheduled...
      assert(false);
    };

    void resume() {
        assert(m_received.has_value());
        std::move(m_received).consume();
    }

  private:
    Received<void> m_received;
    std::coroutine_handle<> m_cont;
    const Env* m_env;
};

template <class T, class Env>
class [[nodiscard]] CoAwaiter {
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
        const auto env = &cont.promise().env();

        m_co.env(env);

        m_receiver = {cont, env};
        m_co.m_handle.promise().m_cont = &m_receiver;

        // TODO: move to scheduler
        return m_co.m_handle;
    }

    T await_resume() {
        auto _ = std::move(m_co);

        return m_receiver.resume();
    }

  private:
    Co<T, Env> m_co;
    CoReceiver<T, Env> m_receiver;
};

template <class T, class Env>
CoAwaiter<T, Env> Co<T, Env>::operator co_await() && noexcept {
    return CoAwaiter<T, Env>{std::move(*this)};
}

} // namespace fastipc::co
