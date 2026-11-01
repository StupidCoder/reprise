// Exercise the real SDL event path: launcher-only tests already receive mapped coordinates.
#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void finger(SDL_EventType type, SDL_FingerID id, float x, float y) {
    SDL_Event event{};
    event.type=type; event.tfinger.touchID=1; event.tfinger.fingerID=id;
    event.tfinger.x=x; event.tfinger.y=y;
    check(SDL_PushEvent(&event), "Queue touch event");
}
int main() {
    try {
        platform::Host host("Touch coordinate check");
        int count=0; auto** windows=SDL_GetWindows(&count);
        check(count==1,"One host window");
        int ww=0,wh=0; SDL_GetWindowSize(windows[0],&ww,&wh); SDL_free(windows);
        for (const auto size : {std::array{960,640},std::array{640,1138},std::array{1422,640}}) {
            host.present(platform::Image(size[0],size[1])); host.poll();
            const float scale=std::min(float(ww)/size[0],float(wh)/size[1]);
            auto expect=[&](const platform::Controls& input, float x, float y) {
                check(!input.ui_fingers.empty(),"UI receives active touch");
                const auto& at=input.ui_fingers.front();
                const float ex=(x*ww-(ww-size[0]*scale)/2)/scale;
                const float ey=(y*wh-(wh-size[1]*scale)/2)/scale;
                // SDL rounds the letterboxed viewport to whole pixels.
                check(std::abs(at.x-ex)<1.f && std::abs(at.y-ey)<1.f,"Touch mapped once to logical UI coordinates");
                check(input.fingers.empty(),"UI contacts do not leak into gameplay");
            };
            finger(SDL_EVENT_FINGER_DOWN,1,.5f,.5f); expect(host.poll(),.5f,.5f);
            finger(SDL_EVENT_FINGER_MOTION,1,.53f,.56f); expect(host.poll(),.53f,.56f);
            finger(SDL_EVENT_FINGER_DOWN,2,.6f,.5f);
            auto two=host.poll(); check(two.ui_fingers.size()==2,"Both pinch contacts survive polling");
            finger(SDL_EVENT_FINGER_MOTION,2,.65f,.5f);
            auto moved=host.poll();
            check(moved.ui_fingers[1].x>two.ui_fingers[1].x,"Second pinch contact moves in logical space");
            expect(host.poll(),.53f,.56f); // Stationary first finger survives another frame.
            finger(SDL_EVENT_FINGER_UP,1,.53f,.56f);finger(SDL_EVENT_FINGER_CANCELED,2,.65f,.5f);
            check(host.poll().ui_fingers.empty(),"Release and cancel clear contacts");
        }
        host.present_game(platform::Image(320,200),1);host.poll();
        finger(SDL_EVENT_FINGER_DOWN,1,.5f,.5f);
        const auto game=host.poll();
        check(game.ui_fingers.empty() && game.fingers.size()==1,"Game receives only frame contacts");
        check(std::abs(game.fingers[0].x-160)<.05f && std::abs(game.fingers[0].y-100)<.05f,
              "Game contact mapping is preserved");
        SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;SDL_PushEvent(&lost);
        const auto cancelled=host.poll();check(cancelled.input_cancelled && cancelled.fingers.empty(),"Focus loss clears contacts");
        std::cout << "SDL touch coordinates passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
