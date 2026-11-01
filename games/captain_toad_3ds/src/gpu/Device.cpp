// See Device.hpp.
#include "gpu/Device.hpp"

#include <stdexcept>

namespace captain_toad::gpu {

namespace {
Device* currentDevice = nullptr;
}   // namespace

Device& device() {
    if (!currentDevice) {
        throw std::runtime_error("No graphics device");
    }
    return *currentDevice;
}

void setDevice(Device* d) {
    currentDevice = d;
}

}   // namespace captain_toad::gpu
