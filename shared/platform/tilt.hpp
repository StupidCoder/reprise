#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace platform {
struct TiltConfig {
    float full_scale{.35f}; // gravity difference that produces full input
    float dead_zone{.05f}; // fraction of full-scale input
    float smoothing_seconds{.08f};
};
// Input motion has already been rotated into the window axes by the host.
// Neutral is the first valid sample after reset; positive output y points down.
class TiltAdapter {
public:
    void reset() { neutral_.reset(); filtered_={}; }
    std::optional<std::array<float,2>> update(std::optional<std::array<float,3>> sample,
                                            TiltConfig config, double dt) {
        if (!sample || !std::isfinite(config.full_scale) || !std::isfinite(config.dead_zone) ||
            !std::isfinite(config.smoothing_seconds) || !std::isfinite(dt)) { reset(); return std::nullopt; }
        for (float v:*sample) if (!std::isfinite(v)) { reset(); return std::nullopt; }
        if (!neutral_) neutral_=*sample;
        const float scale=std::max(config.full_scale,.01f);
        std::array<float,2> raw{((*sample)[0]-(*neutral_)[0])/scale,
                               -((*sample)[1]-(*neutral_)[1])/scale};
        const float a=config.smoothing_seconds<=0 ? 1.f : float(1-std::exp(-std::clamp(dt,0.,.25)/config.smoothing_seconds));
        for(int k=0;k<2;++k) filtered_[k]+=(raw[k]-filtered_[k])*a;
        const float length=std::hypot(filtered_[0],filtered_[1]);
        const float dead=std::clamp(config.dead_zone,0.f,.95f);
        const float magnitude=std::clamp((length-dead)/(1-dead),0.f,1.f);
        return std::array<float,2>{length>0?filtered_[0]/length*magnitude:0,
                                   length>0?filtered_[1]/length*magnitude:0};
    }
private:
    std::optional<std::array<float,3>> neutral_;
    std::array<float,2> filtered_{};
};
}
