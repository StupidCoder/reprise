// =============================================================================================
// Parallel.hpp - spreading a loop of independent items over the CPU's cores
// =============================================================================================
//
// The skinning of the animated models writes each vertex from its own inputs only; drawn at the
// display's full rate (frames between ticks, session Presentation.hpp) it runs every frame. A
// small pool of worker threads takes slices of such loops; the caller works on one slice itself
// and waits for the rest. Results do not depend on the split (each item is computed alike).
// =============================================================================================
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace captain_toad::parallel {

class Pool {
public:
    static Pool& shared() {
        static Pool pool;
        return pool;
    }

    // Runs f(begin, end) over [0, n) in slices of at least `grain` items.
    void forRange(size_t n, size_t grain, const std::function<void(size_t, size_t)>& f) {
        const size_t slices = std::min(workers_.size() + 1, std::max<size_t>(1, n / std::max<size_t>(grain, 1)));
        if (slices <= 1) {
            f(0, n);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = &f;
            n_ = n;
            slices_ = slices;
            next_ = 1;   // slice 0 is the caller's
            pending_ = slices - 1;
            ++generation_;
        }
        wake_.notify_all();
        f(0, n / slices);
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
        job_ = nullptr;
    }

    ~Pool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        for (auto& t : workers_) {
            t.join();
        }
    }

private:
    Pool() {
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        // Up to five workers (the Quest 3 offers six cores; results do not depend on the split).
        const unsigned count = std::min(5u, cores > 1 ? cores - 1 : 0u);
        for (unsigned i = 0; i < count; i++) {
            workers_.emplace_back([this] { run(); });
        }
    }

    void run() {
        unsigned long long seen = 0;
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            wake_.wait(lock, [&] { return stop_ || (generation_ != seen && job_ && next_ < slices_); });
            if (stop_) {
                return;
            }
            const size_t slice = next_++;
            if (next_ >= slices_) {
                seen = generation_;
            }
            const auto* job = job_;
            const size_t n = n_, slices = slices_;
            lock.unlock();
            (*job)(n* slice / slices, n * (slice + 1) / slices);
            lock.lock();
            if (--pending_ == 0) {
                done_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(size_t, size_t)>* job_ = nullptr;
    size_t n_ = 0, slices_ = 0, next_ = 0, pending_ = 0;
    unsigned long long generation_ = 0;
    bool stop_ = false;
};

}   // namespace captain_toad::parallel
