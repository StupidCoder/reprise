#pragma once
#include <coroutine>
#include <exception>
#include <utility>

// Blocking control flow of the original program, kept as written.
//
// Much of the cartridges' frontend and level sequencing is straight-line code
// that waits for the next video frame in the middle of loops ("EI; HALT", or
// the frame-wait routine: Sonic 2 05C5, Triple Trouble 084C). Each such
// routine is a coroutine returning Flow; a wait for the next frame is
// `co_await next_frame` and a call to a routine that may wait is
// `co_await routine()`. The game module resumes the innermost waiting routine
// once per frame, after the frame interrupt work.
namespace gg_engine {

class FlowRunner;

class [[nodiscard]] Flow {
public:
    struct promise_type {
        std::coroutine_handle<> continuation;
        std::exception_ptr error;

        Flow get_return_object() {
            return Flow(std::coroutine_handle<promise_type>::from_promise(*this));
        }

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
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~Flow() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // Awaiting a Flow runs it as a subroutine of the awaiting one.
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation = caller;
        return handle_;
    }

    void await_resume() const {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
    }

    bool done() const { return !handle_ || handle_.done(); }

private:
    friend class FlowRunner;
    std::coroutine_handle<promise_type> handle_;
};

// Owns the top-level routine and remembers where it is waiting.
class FlowRunner {
public:
    void start(Flow flow) {
        root_ = std::move(flow);
        resume_point_ = root_.handle_;
    }

    // Runs until the program next waits for a frame (or finishes).
    void resume() {
        if (!resume_point_ || root_.done()) {
            return;
        }
        auto h = std::exchange(resume_point_, {});
        h.resume();
        if (root_.done() && root_.handle_.promise().error) {
            std::rethrow_exception(root_.handle_.promise().error);
        }
    }

    bool running() const { return !root_.done(); }

    // Marks `h` as the point to resume at the next resume().
    void suspend_at(std::coroutine_handle<> h) { resume_point_ = h; }

    // `co_await runner.next_frame()`: suspends the whole chain of routines
    // until the next resume().
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

} // namespace gg_engine
