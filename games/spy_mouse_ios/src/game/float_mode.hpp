// The original runs on ARMv7 under iOS, whose floating point flushes denormal results to zero
// (a cat's steering offset that decays tick by tick reaches exactly 0 there, run l23_win). The
// game's arithmetic runs in the same mode: call once at start-up, on the thread that ticks.
#pragma once

#if defined(__aarch64__)
#include <cstdint>
#elif defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace doc::game {

inline void flushDenormalsToZero() {
#if defined(__aarch64__)
    std::uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= std::uint64_t(1) << 24;  // FZ
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

// The same mode for a stretch of code only, the earlier mode restored at the end: for the game
// inside an application whose other code (other games) must keep the default mode.
class ScopedFlushToZero {
public:
    ScopedFlushToZero() {
#if defined(__aarch64__)
        __asm__ volatile("mrs %0, fpcr" : "=r"(saved_));
#elif defined(__x86_64__) || defined(_M_X64)
        saved_ = _mm_getcsr();
#endif
        flushDenormalsToZero();
    }
    ~ScopedFlushToZero() {
#if defined(__aarch64__)
        __asm__ volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(_M_X64)
        _mm_setcsr(saved_);
#endif
    }
    ScopedFlushToZero(const ScopedFlushToZero&) = delete;
    ScopedFlushToZero& operator=(const ScopedFlushToZero&) = delete;

private:
#if defined(__aarch64__)
    std::uint64_t saved_ = 0;
#else
    unsigned int saved_ = 0;
#endif
};

}  // namespace doc::game
