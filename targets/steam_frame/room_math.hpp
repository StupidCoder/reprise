#pragma once
#include <openxr/openxr.h>
#include <cmath>
#include <optional>
namespace frame_room {
inline XrVector3f add(XrVector3f a, XrVector3f b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline XrVector3f sub(XrVector3f a, XrVector3f b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline XrVector3f mul(XrVector3f a, float v) { return {a.x*v,a.y*v,a.z*v}; }
inline float dot(XrVector3f a, XrVector3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline XrVector3f cross(XrVector3f a, XrVector3f b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    const XrVector3f axis{q.x,q.y,q.z};
    const auto twice=mul(cross(axis,v),2);
    return add(v,add(mul(twice,q.w),cross(axis,twice)));
}
struct Hit { float distance, u, v; };
// Orthonormal billboard frame: local Y runs along the beam, Z faces the eye.
inline XrQuaternionf ribbon_orientation(XrVector3f up, XrVector3f to_eye) {
    const auto unit=[](XrVector3f v) { return mul(v,1/std::sqrt(dot(v,v))); };
    up=unit(up);
    auto right=cross(up,to_eye);
    if(dot(right,right)<1e-8f)
        right=cross(up,std::abs(up.y)<.9f ? XrVector3f{0,1,0} : XrVector3f{1,0,0});
    right=unit(right);
    const auto normal=cross(right,up);
    const float m[3][3]={{right.x,up.x,normal.x},{right.y,up.y,normal.y},{right.z,up.z,normal.z}};
    const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {
        const float s=2*std::sqrt(trace+1);
        return {(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4};
    }
    int i=0;
    if(m[1][1]>m[i][i]) i=1;
    if(m[2][2]>m[i][i]) i=2;
    const int j=(i+1)%3,k=(i+2)%3;
    const float s=2*std::sqrt(1+m[i][i]-m[j][j]-m[k][k]);
    float q[4]{};
    q[i]=s/4; q[j]=(m[j][i]+m[i][j])/s; q[k]=(m[k][i]+m[i][k])/s;
    q[3]=(m[k][j]-m[j][k])/s;
    return {q[0],q[1],q[2],q[3]};
}
inline std::optional<Hit> hit(XrPosef ray, XrPosef panel, float width, float height) {
    const auto right = rotate(panel.orientation,{1,0,0});
    const auto up = rotate(panel.orientation,{0,1,0});
    const auto normal = rotate(panel.orientation,{0,0,1});
    const auto direction = rotate(ray.orientation,{0,0,-1});
    const float denominator = dot(direction,normal);
    if (denominator >= -1e-5f) return {}; // front face only, including parallel rays
    const float distance = dot(sub(panel.position,ray.position),normal)/denominator;
    if (distance <= 0) return {};
    const auto offset = sub(add(ray.position,mul(direction,distance)),panel.position);
    const float u = dot(offset,right)/width+.5f, v = .5f-dot(offset,up)/height;
    if (u < 0 || u > 1 || v < 0 || v > 1) return {};
    return Hit{distance,u,v};
}
}
