#pragma once
#include "room_math.hpp"
#include <algorithm>
#include <numbers>

namespace frame_room {
// Upright gameplay window. Head pitch/roll never tilt the horizon. A dwell
// rejects brief glances; a critically damped spring eases translation and yaw.
class WindowFollow {
public:
    void reset() { ready_=moving_=false; dwell_=0; velocity_={}; yaw_velocity_=0; }
    XrPosef update(const XrPosef& head,float dt,bool reduced) {
        const auto forward=rotate(head.orientation,{0,0,-1});
        const float yaw=forward.x*forward.x+forward.z*forward.z>1e-6f ?
            std::atan2(forward.x,-forward.z) : yaw_;
        if(!ready_) { position_=head.position; yaw_=yaw; ready_=true; }
        const auto delta=sub(head.position,position_);
        const float angle=std::remainder(yaw-yaw_,2*std::numbers::pi_v<float>);
        if(reduced) { moving_=false; dwell_=0; velocity_={}; yaw_velocity_=0; }
        else {
            const bool displaced=dot(delta,delta)>.20f*.20f || std::abs(angle)>25*std::numbers::pi_v<float>/180;
            if(!moving_) {
                dwell_=displaced ? dwell_+dt : 0;
                moving_=dwell_>=.5f;
            }
            if(moving_) {
                dt=std::clamp(dt,0.f,.05f);
                spring(position_.x,velocity_.x,head.position.x,dt);
                spring(position_.y,velocity_.y,head.position.y,dt);
                spring(position_.z,velocity_.z,head.position.z,dt);
                spring(yaw_,yaw_velocity_,yaw_+angle,dt);
                // Stop on perceptual arrival, not asymptotic zero velocity.
                // Freeze here rather than snapping the final small distance;
                // subsequent movement must qualify for a fresh dwell.
                const auto remaining=sub(head.position,position_);
                const float remaining_yaw=std::remainder(yaw-yaw_,2*std::numbers::pi_v<float>);
                if(dot(remaining,remaining)<.01f*.01f &&
                   std::abs(remaining_yaw)<.5f*std::numbers::pi_v<float>/180) {
                    moving_=false; dwell_=0; velocity_={}; yaw_velocity_=0;
                }
            }
        }
        return {{0,std::sin(-yaw_/2),0,std::cos(-yaw_/2)},
                add(position_,{2*std::sin(yaw_),0,-2*std::cos(yaw_)})};
    }
private:
    static void spring(float& value,float& velocity,float target,float dt) {
        constexpr float rate=8;
        const float offset=value-target, impulse=velocity+rate*offset;
        const float decay=std::exp(-rate*dt);
        value=target+(offset+impulse*dt)*decay;
        velocity=(velocity-rate*impulse*dt)*decay;
    }
    bool ready_{},moving_{};
    float dwell_{},yaw_{},yaw_velocity_{};
    XrVector3f position_{},velocity_{};
};
}
