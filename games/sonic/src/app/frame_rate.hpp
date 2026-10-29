#pragma once
#include <optional>

namespace shell {
// Sample completed presentation intervals, including work and vsync waits.
// Independent of emulation/game ticks and refreshed twice a second.
class FrameRate {
public:
    explicit FrameRate(double now) : start_(now) {}

    void presented(double now) {
        ++frames_;
        const double elapsed = now - start_;
        if (elapsed >= 0.5) {
            fps_ = frames_ / elapsed;
            frames_ = 0;
            start_ = now;
        }
    }

    std::optional<double> fps() const { return fps_; }

private:
    double start_;
    unsigned frames_{};
    std::optional<double> fps_;
};
} // namespace shell
