#pragma once

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>
#include "co/received.hxx"

namespace fastipc::co {

class StoppedException final : public std::exception {
  public:
    [[nodiscard]] const char* what() const noexcept override { return "operation stopped"; }
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
class [[nodiscard]] Promise {
  public:
    class State {

      public:
        Promise get_return_object() { return Promise{std::coroutine_handle<State>::from_promise(*this)}; }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalSuspend {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<State> h) noexcept { h.promise().complete(); }
            void await_resume() noexcept {}
        };

        FinalSuspend final_suspend() noexcept { return {}; }

        void return_value(T value) { received.set_value(std::move(value)); }
        void unhandled_exception() { received.set_exception(std::current_exception()); }

        template <class A>
        AwaitedBy<A, typename Promise<T>::State> await_transform(A&& awaitable) {
            return {std::forward<A>(awaitable)};
        }

        Receiver<T>* receiver;

      private:
        // sadly we need to buffer the received value to allow final_suspend to run
        void complete() { std::move(received).forward(*receiver); }

        Received<T> received;
    };

    explicit Promise(std::coroutine_handle<State> handle) : m_handle{std::move(handle)} {}

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

    std::coroutine_handle<State> handle() { return m_handle; }
    std::coroutine_handle<State> handle() const { return m_handle; }

  private:
    std::coroutine_handle<State> m_handle;
};

template <class S, class P>
class SenderAwaiter {

  public:
    using sender_type = S;
    using value_type = sender_type::value_type;

    explicit SenderAwaiter(sender_type sender) : state{std::move(sender)} {}

    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> cont) {
        // TODO: somehow operation state needs to be moveable..
        auto& operation_state = state.template emplace<operation_state_type>(
            std::get<sender_type>(std::move(state)).connect(AwaiterReceiver{this->received, cont}));

        operation_state.start();

        return std::noop_coroutine();
    }

    [[nodiscard]] value_type await_resume() noexcept { return std::move(received).consume(); }

  private:
    class AwaiterReceiver {
      public:
        AwaiterReceiver(Received<value_type>& received, std::coroutine_handle<P> awaiter)
            : m_received{&received}, m_awaiter{awaiter} {}

        void set_value(value_type value) {
            m_received->set_value(std::move(value));

            m_awaiter.resume();
        }

        void set_exception(std::exception_ptr ptr) {
            m_received->set_exception(ptr);

            m_awaiter.resume();
        }

      private:
        Received<value_type>* m_received;
        std::coroutine_handle<P> m_awaiter;
    };

    using operation_state_type = decltype(std::declval<S>().connect(std::declval<AwaiterReceiver>()));

    std::variant<std::monostate, sender_type, operation_state_type> state;
    Received<value_type> received;
};

// todo sender concept
template <class A, class P>
SenderAwaiter<A, P> operator co_await(AwaitedBy<A, P>&& awaited_by) {
    return SenderAwaiter<A, P>{std::move(awaited_by).value};
}

template <class T>
class [[nodiscard]] Co {

  public:
    using promise_type = Promise<T>::State;

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

                // TODO: schedule it
                m_promise.handle().resume();
            }

          private:
            class PromiseReceiver : public Receiver<T> {

              public:
                explicit PromiseReceiver(R receiver) : Receiver<T>{}, m_receiver{std::move(receiver)} {}

                void set_value(T value) override { m_receiver.set_value(std::move(value)); }
                void set_exception(std::exception_ptr exc) override { m_receiver.set_exception(exc); }

              private:
                R m_receiver;
            };

            PromiseReceiver m_receiver;
            Promise<T> m_promise;
        };

        return OperationState{std::move(*this).m_promise, std::forward<R>(receiver)};
    }

  private:
    Promise<T> m_promise;
};

} // namespace fastipc::co
