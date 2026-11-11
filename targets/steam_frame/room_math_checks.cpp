#include "room_math.hpp"
#include "window_follow.hpp"
#include <cstdio>
#include <stdexcept>
using namespace frame_room;
int main() {
    const auto check=[](bool ok,const char* message){if(!ok) throw std::runtime_error(message);};
    const XrQuaternionf identity{0,0,0,1};
    const XrPosef ray{identity,{0,0,0}}, panel{identity,{0,0,-2}};
    auto hit=frame_room::hit(ray,panel,2,1);
    check(hit && std::abs(hit->distance-2)<1e-6f && hit->u==.5f && hit->v==.5f,"center hit");
    auto edge=ray; edge.position={1,.5f,0};
    hit=frame_room::hit(edge,panel,2,1);
    check(hit && hit->u==1 && hit->v==0,"upper-right coordinate mapping");
    edge.position.x=1.01f;
    check(!frame_room::hit(edge,panel,2,1),"outside width misses");
    auto behind=ray; behind.position.z=-3;
    check(!frame_room::hit(behind,panel,2,1),"negative distance misses");
    auto back=panel; back.orientation={0,1,0,0};
    check(!frame_room::hit(ray,back,2,1),"back face misses");
    const float half=std::sqrt(.5f);
    auto side=panel; side.orientation={0,half,0,half};
    check(!frame_room::hit(ray,side,2,1),"parallel plane misses");
    const XrQuaternionf quarter{0,half,0,half};
    auto rotated_ray=ray; rotated_ray.orientation=quarter;
    auto rotated_panel=panel; rotated_panel.orientation=quarter;
    rotated_panel.position=rotate(quarter,panel.position);
    hit=frame_room::hit(rotated_ray,rotated_panel,2,1);
    check(hit && std::abs(hit->distance-2)<1e-5f,"rotated ring card hit");
    for(const auto direction : {XrVector3f{0,0,-1},XrVector3f{1,0,0},
                               XrVector3f{0,1,0},XrVector3f{0,-1,0}}) {
        for(const auto eye : {XrVector3f{.4f,.2f,1},direction}) {
            const auto q=ribbon_orientation(direction,eye);
            const auto axis=rotate(q,{0,1,0});
            const auto face=rotate(q,{0,0,1});
            check(dot(sub(axis,direction),sub(axis,direction))<1e-8f,"beam follows ray, including axial fallback");
            check(std::abs(dot(face,face)-1)<1e-5f && dot(face,eye)>-1e-5f,"beam faces wearer and remains finite");
        }
    }
    WindowFollow follow;
    auto head=ray; head.position.y=1.7f;
    auto window=follow.update(head,0,false);
    check(std::abs(window.position.y-1.7f)<1e-5f,"window begins at eye height");
    head.position.y=1.2f;
    for(int i=0;i<40;++i) window=follow.update(head,.01f,false);
    check(std::abs(window.position.y-1.7f)<1e-5f,"follow waits through brief movement");
    float previous=window.position.y;
    for(int i=0;i<400;++i) {
        window=follow.update(head,.01f,false);
        check(window.position.y<=previous+1e-6f && window.position.y>=1.2f-1e-6f,"follow eases without overshoot");
        previous=window.position.y;
    }
    check(std::abs(window.position.y-1.2f)<.011f,"follow settles at new seated eye height");
    head.position.y=2;
    for(int i=0;i<200;++i) window=follow.update(head,.01f,true);
    check(std::abs(window.position.y-1.2f)<.011f,"reduced motion keeps anchor");
    follow.reset(); window=follow.update(head,0,true);
    check(window.position.y==2,"recenter immediately restores eye height");
    head.orientation=quarter;
    for(int i=0;i<450;++i) window=follow.update(head,.01f,false);
    check(std::abs(window.position.x+2)<.02f && std::abs(window.position.z)<.02f,
          "window follows horizontal facing at two metres");
    const auto normal=rotate(window.orientation,{0,0,1});
    check(dot(normal,sub(head.position,window.position))>1.99f,"following window faces user");
    head.orientation={half,0,0,half};
    follow.reset();window=follow.update(head,0,false);
    check(window.position.y==head.position.y && window.orientation.x==0 && window.orientation.z==0,
          "looking vertically keeps an upright eye-level window");
    follow.reset();head=ray;
    window=follow.update(head,0,false);
    const float glance=20*std::numbers::pi_v<float>/180;
    head.orientation={0,std::sin(-glance/2),0,std::cos(-glance/2)};
    for(int i=0;i<150;++i) window=follow.update(head,.01f,false);
    check(window.position.x==0,"looking toward frame edge does not trigger follow");
    head.orientation=identity;head.position.x=.5f;
    for(int i=0;i<150;++i) window=follow.update(head,.01f,false);
    check(std::abs(window.position.x-.5f)<.011f,"follow catches up within one second after dwell");
    const auto settled=window.position;
    head.position.x+=.05f;
    for(int i=0;i<100;++i) window=follow.update(head,.01f,false);
    check(window.position.x==settled.x,"arrival ends tracking despite small subsequent motion");
    head.position.x+=.4f;
    for(int i=0;i<40;++i) window=follow.update(head,.01f,false);
    check(window.position.x==settled.x,"arrival requires a fresh delay before following again");
    for(int i=0;i<30;++i) window=follow.update(head,.01f,false);
    check(window.position.x>settled.x,"fresh sustained movement starts another follow");
    for(float y:{-.4f,0.f,.4f}) {
        auto pointer=ray;pointer.position.y=y;
        const auto logical=frame_room::hit(pointer,panel,1.5f,1.f);
        const auto native=frame_room::hit(pointer,panel,1.5f,.9375f);
        check(logical && native && std::abs((logical->v*640-20)/600-native->v)<1e-6f,
              "native cockpit preserves letterboxed pointer coordinates");
    }
    std::puts("room ray/quad coordinate checks passed");
}
