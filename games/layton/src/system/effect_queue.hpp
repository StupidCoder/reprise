#pragma once
// Sequenced-effect requests made by recovered puzzle engines. In the ROM the
// engines call the overlay-2 sound object directly (0207f850 group lookup,
// then 0207f8f0 play / 0207f918 stop); natively an engine records the call in
// order and the scene forwards it to its GameSound after the engine update.
#include <utility>
#include <vector>

namespace picarat {

struct EffectRequest {
    int id = -1; // -1 with volume >= 0: BGM volume request (0207fae4)
    bool stop = false;
    float seconds = 0; // stop fade / volume ramp
    float volume = -1;
};

class EffectQueue {
public:
    void play(int id) { requests_.push_back({id, false, 0}); }

    void stop(int id, float seconds = 0) { requests_.push_back({id, true, seconds}); }

    void bgmVolume(float volume, float seconds) {
        requests_.push_back({-1, false, seconds, volume});
    }

    std::vector<EffectRequest> take() { return std::exchange(requests_, {}); }

private:
    std::vector<EffectRequest> requests_;
};

} // namespace picarat
