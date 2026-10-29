#pragma once
#include <coroutine>
#include <exception>
#include <utility>

// The front end's control flow, written as the original's straight-line
// code is: routines that wait for the next vertical blank in the middle of
// their loops ($1261A, the menus' wait for a click). Each such routine is a
// coroutine returning Flow; waiting for the next frame is
// `co_await runner.next_frame()`, calling a routine that may wait is
// `co_await routine()`. The module resumes the innermost waiting routine once
// per vertical blank.
namespace babel::front {

class FlowRunner;

class [[nodiscard]] Flow {
public:
    struct promise_type {
        std::coroutine_handle<> continuation;
        std::exception_ptr error;
        Flow get_return_object() { return Flow(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto next = h.promise().continuation;
                return next ? next : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { error = std::current_exception(); }
    };

    Flow() = default;
    explicit Flow(std::coroutine_handle<promise_type> h) : handle_(h) {}
    Flow(Flow&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Flow& operator=(Flow&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Flow() {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation = caller;
        return handle_;
    }
    void await_resume() const {
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
    }
    bool done() const { return !handle_ || handle_.done(); }

private:
    friend class FlowRunner;
    std::coroutine_handle<promise_type> handle_;
};

class FlowRunner {
public:
    void start(Flow flow) {
        root_ = std::move(flow);
        resume_point_ = root_.handle_;
    }
    // Runs until the program next waits for a frame (or finishes).
    void resume() {
        if (!resume_point_ || root_.done()) return;
        auto h = std::exchange(resume_point_, {});
        h.resume();
        if (root_.done() && root_.handle_.promise().error) std::rethrow_exception(root_.handle_.promise().error);
    }
    bool running() const { return !root_.done(); }

    auto next_frame() {
        struct Awaiter {
            FlowRunner& runner;
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h) noexcept { runner.resume_point_ = h; }
            void await_resume() const noexcept {}
        };
        return Awaiter{*this};
    }

private:
    Flow root_;
    std::coroutine_handle<> resume_point_;
};

} // namespace babel::front
