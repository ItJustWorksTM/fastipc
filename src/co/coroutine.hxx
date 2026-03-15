#pragma once

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>
#include "co/received.hxx"

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

    virtual void set_value(T value) = 0;
    virtual void set_exception(std::exception_ptr exc) = 0;
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

    virtual void set_value() = 0;
    virtual void set_exception(std::exception_ptr exc) = 0;
};

template <class A, class P>
struct AwaitedBy {
    A value;
};

template <class T>
class PromiseState;

template <class T>
class [[nodiscard]] Promise final {
  public:
    explicit Promise(std::coroutine_handle<PromiseState<T>> handle) : m_handle{std::move(handle)} {}

    Promise(Promise&) noexcept = delete;
    Promise& operator=(Promise&) noexcept = delete;

    Promise(Promise&& it) noexcept : m_handle{std::exchange(it.m_handle, {})} {}
    Promise& operator=(Promise&& rhs) noexcept {
        auto other = Promise{std::move(rhs)};

        std::swap(m_handle, other.m_handle);

        return *this;
    }

    ~Promise() noexcept {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    [[nodiscard]] std::coroutine_handle<PromiseState<T>> handle() const { return m_handle; }

  private:
    std::coroutine_handle<PromiseState<T>> m_handle;
};

template <class T>
class PromiseState final {
  public:
    Promise<T> get_return_object() { return Promise<T>{std::coroutine_handle<PromiseState>::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalSuspend final {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<PromiseState> h) noexcept { h.promise().complete(); }
        void await_resume() noexcept {}
    };

    FinalSuspend final_suspend() noexcept { return {}; }

    void return_value(T value) { received.set_value(std::move(value)); }
    void unhandled_exception() { received.set_exception(std::current_exception()); }

    template <class A>
    AwaitedBy<A, PromiseState> await_transform(A&& awaitable) {
        return {std::forward<A>(awaitable)};
    }

    Receiver<T>* receiver{};

  private:
    void complete() { std::move(received).forward(*receiver); }

    Received<T> received;
};

template <>
class PromiseState<void> final {
  public:
    Promise<void> get_return_object() {
        return Promise<void>{std::coroutine_handle<PromiseState>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalSuspend final {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<PromiseState> h) noexcept { h.promise().complete(); }
        void await_resume() noexcept {}
    };

    FinalSuspend final_suspend() noexcept { return {}; }

    void return_void() { received.set_value(); }
    void unhandled_exception() { received.set_exception(std::current_exception()); }

    template <class A>
    AwaitedBy<A, PromiseState> await_transform(A&& awaitable) {
        return {std::forward<A>(awaitable)};
    }

    Receiver<void>* receiver{};

  private:
    void complete() { std::move(received).forward(*receiver); }

    Received<void> received;
};

template <class T, class P>
class AwaiterReceiver final {
  public:
    AwaiterReceiver(Received<T>& received, std::coroutine_handle<P> awaiter)
        : m_received{&received}, m_awaiter{awaiter} {}

    void set_value(T value) {
        m_received->set_value(std::move(value));

        m_awaiter.resume();
    }

    void set_exception(std::exception_ptr ptr) {
        m_received->set_exception(std::move(ptr));

        m_awaiter.resume();
    }

  private:
    Received<T>* m_received;
    std::coroutine_handle<P> m_awaiter;
};

template <class P>
class AwaiterReceiver<void, P> final {
  public:
    AwaiterReceiver(Received<void>& received, std::coroutine_handle<P> awaiter)
        : m_received{&received}, m_awaiter{awaiter} {}

    void set_value() {
        m_received->set_value();

        m_awaiter.resume();
    }

    void set_exception(std::exception_ptr ptr) {
        m_received->set_exception(std::move(ptr));

        m_awaiter.resume();
    }

  private:
    Received<void>* m_received;
    std::coroutine_handle<P> m_awaiter;
};

template <class S, class P>
class SenderAwaiter final {

  public:
    using sender_type = S;
    using value_type = sender_type::value_type;

    explicit SenderAwaiter(sender_type sender) : state{std::move(sender)} {}

    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> cont) {
        auto& operation_state = state.template emplace<operation_state_type>(
            std::get<sender_type>(std::move(state)).connect(AwaiterReceiver<value_type, P>{this->received, cont}));

        operation_state.start();

        return std::noop_coroutine();
    }

    [[nodiscard]] value_type await_resume() { return std::move(received).consume(); }

  private:
    using operation_state_type = decltype(std::declval<S>().connect(std::declval<AwaiterReceiver<value_type, P>>()));

    std::variant<std::monostate, sender_type, operation_state_type> state;
    Received<value_type> received;
};

// todo sender concept
template <class A, class P>
SenderAwaiter<A, P> operator co_await(AwaitedBy<A, P>&& awaited_by) {
    return SenderAwaiter<A, P>{std::move(awaited_by).value};
}

template <class T, class R>
class PromiseReceiver final : public Receiver<T> {

  public:
    explicit PromiseReceiver(R receiver) : Receiver<T>{}, m_receiver{std::move(receiver)} {}

    void set_value(T value) override { m_receiver.set_value(std::move(value)); }
    void set_exception(std::exception_ptr exc) override { m_receiver.set_exception(exc); }

  private:
    R m_receiver;
};

template <class R>
class PromiseReceiver<void, R> final : public Receiver<void> {

  public:
    explicit PromiseReceiver(R receiver) : Receiver<void>{}, m_receiver{std::move(receiver)} {}

    void set_value() override { m_receiver.set_value(); }
    void set_exception(std::exception_ptr exc) override { m_receiver.set_exception(exc); }

  private:
    R m_receiver;
};

template <class T>
class [[nodiscard]] Co final {

  public:
    using promise_type = PromiseState<T>;
    using value_type = T;

    explicit(false) Co(Promise<T> promise) : m_promise{std::move(promise)} {}

    template <class R>
    auto connect(R&& receiver) && {
        class OperationState {

          public:
            OperationState(Promise<T> promise, R receiver)
                : m_receiver{std::move(receiver)}, m_promise{std::move(promise)} {}

            void start() {
                m_promise.handle().promise().receiver = &m_receiver;
                m_promise.handle().resume();
            }

          private:
            PromiseReceiver<T, R> m_receiver;
            Promise<T> m_promise;
        };

        return OperationState{std::move(*this).m_promise, std::forward<R>(receiver)};
    }

  private:
    Promise<T> m_promise;
};

} // namespace fastipc::co
