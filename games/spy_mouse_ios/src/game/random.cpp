#include "game/random.hpp"

#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <execinfo.h>
#define DOC_RANDOM_TRACE 1
#endif

namespace doc::game {

namespace {
// Research aid: DOC_RAND_TRACE=1 prints every draw with the port functions that asked for it,
// to line up with the reference traces' callers (lr=...). Off unless the variable is set.
void traceDraw(std::uint64_t index, std::uint32_t value) {
#ifdef DOC_RANDOM_TRACE
    static const bool on = std::getenv("DOC_RAND_TRACE") != nullptr;
    if (!on) return;
    void* frames[6];
    const int n = backtrace(frames, 6);
    char** names = backtrace_symbols(frames, n);
    std::printf("rand %llu %u", static_cast<unsigned long long>(index), value);
    for (int i = 2; i < n && names; ++i) {
        const char* s = names[i];
        // ("index  module  address  symbol + offset": keep the symbol.)
        int field = 0;
        for (const char* p = s; *p; ++p)
            if (*p == ' ' && p[1] != ' ' && ++field == 3) { s = p + 1; break; }
        std::printf(" | %.60s", s);
    }
    std::printf("\n");
    std::free(names);
#else
    (void)index;
    (void)value;
#endif
}
}  // namespace

int GameRandom::next() {
    ++draws_;
    if (state_ == 0) state_ = 123459876;
    std::int32_t hi = std::int32_t(state_ / 127773);
    std::int32_t lo = std::int32_t(state_ % 127773);
    std::int32_t x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7fffffff;
    state_ = std::uint32_t(x);
    traceDraw(draws_, state_);
    return int(state_ % 0x80000000u);
}

}  // namespace doc::game
