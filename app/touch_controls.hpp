#pragma once
#include "platform/host.hpp"
namespace app {
// Owns cached text tiles; destroy before the host renderer.
class TouchPainter {
public:
    TouchPainter();
    ~TouchPainter();
    void draw(const platform::TouchDeck&, const platform::GpuFrame&);
private:
    struct Cache;
    std::unique_ptr<Cache> cache_;
};
}
