#include "room_math.hpp"
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
    std::puts("room ray/quad coordinate checks passed");
}
