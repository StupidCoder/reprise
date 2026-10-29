#pragma once
// Host audio service handed to the scenes. Sequenced music, effects and SAD
// streams all go through the recovered overlay-2 sound object (GameSound);
// the host owns device output, locking and restarts.
#include "sound/sound_object.hpp"

namespace picarat {
struct AudioService {
    virtual ~AudioService() = default;
    // A native restart (title, collection, checkpoint load) rebuilds the sound system.
    virtual void reset() = 0;

    // Null when the host has no sound service (render/input-only research probes).
    virtual GameSound* sequenced() { return nullptr; }
};
} // namespace picarat
