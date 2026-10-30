#include "platform/touch.hpp"
#include "platform/tilt.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace platform;
void check(bool ok) { if (!ok) throw std::runtime_error("input contract check failed"); }
bool near(float a,float b) { return std::abs(a-b)<.001f; }
int main() {
    TouchDeck deck;
    std::vector<TouchControl> layout{
        {"steer","",TouchControl::Kind::stick,0,0,.4f,1,0,0,{}},
        {"throttle","",TouchControl::Kind::slider,.5f,0,.2f,1,.2f,0,{}},
        {"fire","",TouchControl::Kind::button,.8f,0,.2f,1,0,0,{}}};
    deck.configure(layout,{10,20,1000,500},true);
    deck.event({TouchEvent::Kind::down,1,100,200});
    deck.event({TouchEvent::Kind::move,1,240,200});
    deck.event({TouchEvent::Kind::down,2,600,145});
    deck.event({TouchEvent::Kind::down,3,900,300});
    auto v=deck.consume(); check(near(v[0].x,1) && near(v[1].y,.75f) && v[2].pressed && v[2].held);
    check(!deck.consume()[2].pressed);
    // Interceptor reports its changing engine throttle as both initial and feedback.
    // These are values, not layout changes: preserve every finger throughout the ramp.
    for (int step=0;step<40;++step) {
        layout[1].initial=float(step)/40;
        layout[1].displayed_value=layout[1].initial;
        deck.configure(layout,{10,20,1000,500},true);
        v=deck.consume();
        check(v[0].held && near(v[0].x,1) && v[1].held && near(v[1].y,.75f) && v[2].held);
        check(!v[0].released && !v[1].released && !v[2].released && !v[2].pressed);
        deck.event({TouchEvent::Kind::move,2,600,146});
        v=deck.consume(); check(v[1].held && v[1].changed && near(v[1].y,.748f));
        deck.event({TouchEvent::Kind::move,2,600,145});
        check(near(deck.consume()[1].y,.75f));
    }
    // A second finger cannot steal the stick; a captured finger can move outside its box.
    deck.event({TouchEvent::Kind::down,4,120,200});
    deck.event({TouchEvent::Kind::move,4,300,200}); check(near(deck.consume()[0].x,1));
    deck.event({TouchEvent::Kind::move,1,-500,200}); check(near(deck.consume()[0].x,-1));
    deck.event({TouchEvent::Kind::up,2,600,145}); v=deck.consume(); check(v[1].released && near(v[1].y,.75f));
    // Feedback from a load/keyboard action changes the display but creates no input action.
    layout[1].displayed_value=.4f; deck.configure(layout,{10,20,1000,500},true);
    check(near(deck.consume()[1].y,.4f)); check(!deck.consume()[1].changed);
    deck.cancel(); v=deck.consume(); check(v[0].released && !v[0].held && near(v[0].x,0) && !v[2].pressed);
    deck.event({TouchEvent::Kind::move,4,900,300}); check(!deck.consume()[2].held);
    deck.event({TouchEvent::Kind::down,5,900,300}); deck.event({TouchEvent::Kind::up,5,900,300});
    v=deck.consume(); check(v[2].pressed && v[2].released && !v[2].held); // tap between ticks
    deck.event({TouchEvent::Kind::down,6,100,200});
    deck.configure(layout,{0,0,500,1000},true); check(!deck.consume()[0].held); // rotation
    deck.configure(layout,{0,0,500,1000},false);
    deck.event({TouchEvent::Kind::down,7,100,200}); check(!deck.consume()[0].held);
    TiltAdapter tilt; TiltConfig config{.5f,.1f,0};
    auto t=tilt.update(std::array<float,3>{.1f,.2f,.9f},config,.016); check(t && near((*t)[0],0));
    t=tilt.update(std::array<float,3>{.6f,.2f,.9f},config,.016); check(t && near((*t)[0],1));
    t=tilt.update(std::array<float,3>{.1f,-.3f,.9f},config,.016); check(t && near((*t)[1],1));
    t=tilt.update(std::array<float,3>{.11f,.21f,.9f},config,.016); check(t && near((*t)[0],0) && near((*t)[1],0));
    tilt.reset(); config.smoothing_seconds=.1f;
    tilt.update(std::array<float,3>{0,0,1},config,.016);
    t=tilt.update(std::array<float,3>{.5f,0,1},config,.01); check(t && (*t)[0]<.1f);
    t=tilt.update(std::array<float,3>{5,5,1},config,1); check(t && std::hypot((*t)[0],(*t)[1])<=1.001f);
    check(!tilt.update(std::nullopt,config,.1));
    t=tilt.update(std::array<float,3>{.5f,0,1},config,.1); check(t && near((*t)[0],0));
    check(!tilt.update(std::array<float,3>{std::numeric_limits<float>::quiet_NaN(),0,1},config,.1));
    std::cout << "Touch ownership, multi-touch, edges, feedback, cancellation and tilt checks passed\n";
}
