#pragma once
#include "platform/control_icon.hpp"
#include "platform/image.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ui {
// Original 24-unit line artwork, rasterized with one-pixel edge coverage at the
// requested output size. Cache the returned tile; no fixed-resolution source art.
inline platform::Image control_icon(platform::ControlIcon icon, int size,
                                    platform::Color color = {235,242,250,255}) {
    size = std::max(size, 1);
    platform::Image image(size, size);
    const float scale = float(size) / 24.f;
    for (int y=0; y<size; ++y) for (int x=0; x<size; ++x) {
        const float px=(float(x)+.5f)/scale, py=(float(y)+.5f)/scale;
        float distance=std::numeric_limits<float>::infinity();
        // Round caps and joins share the same stroke width throughout the icon set.
        auto line = [&](float ax,float ay,float bx,float by) {
            const float dx=bx-ax, dy=by-ay;
            const float t=std::clamp(((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
            distance=std::min(distance,std::hypot(px-ax-t*dx,py-ay-t*dy)-.8f);
        };
        auto ring = [&](float cx,float cy,float radius) {
            distance=std::min(distance,std::abs(std::hypot(px-cx,py-cy)-radius)-.8f);
        };
        auto wheel = [&](float cx) {
            // Rounded vertical tire outline: 4 x 6 design units.
            const float qx=std::abs(px-cx)-.9f, qy=std::abs(py-18.f)-1.9f;
            const float box=std::hypot(std::max(qx,0.f),std::max(qy,0.f))+
                std::min(std::max(qx,qy),0.f)-1.1f;
            distance=std::min(distance,std::abs(box)-.8f);
        };
        switch (icon) {
        case platform::ControlIcon::landing_gear:
            line(9,3,15,3); line(12,3,12,10);
            line(8,10,16,10); line(8,10,6,15); line(16,10,18,15);
            line(8,18,16,18); wheel(6); wheel(18);
            break;
        case platform::ControlIcon::camera:
            line(4,7,8,7); line(8,7,9.5f,4); line(9.5f,4,14.5f,4);
            line(14.5f,4,16,7); line(16,7,20,7);
            line(20,7,21,8); line(21,8,21,19); line(21,19,20,20);
            line(20,20,4,20); line(4,20,3,19); line(3,19,3,8); line(3,8,4,7);
            ring(12,13.5f,3.7f);
            distance=std::min(distance,std::hypot(px-18,py-10)-.85f);
            break;
        case platform::ControlIcon::arrow_up:
        case platform::ControlIcon::arrow_down:
        case platform::ControlIcon::arrow_left:
        case platform::ControlIcon::arrow_right: {
            // Rotate one master arrow so every direction has identical weight.
            auto point = [&](float x,float y) {
                switch(icon) {
                case platform::ControlIcon::arrow_down: return std::pair{24.f-x,24.f-y};
                case platform::ControlIcon::arrow_left: return std::pair{y,24.f-x};
                case platform::ControlIcon::arrow_right: return std::pair{24.f-y,x};
                default: return std::pair{x,y};
                }
            };
            auto stroke = [&](float ax,float ay,float bx,float by) {
                auto a=point(ax,ay), b=point(bx,by);line(a.first,a.second,b.first,b.second);
            };
            stroke(12,20,12,4);stroke(5,11,12,4);stroke(12,4,19,11);
            break;
        }
        case platform::ControlIcon::accelerator:
            // Tall accelerator pedal with three tread grooves.
            line(10,3,17,5);line(17,5,14,21);line(14,21,7,19);line(7,19,10,3);
            line(11,7,14,8);line(10,11,13,12);line(9,15,12,16);
            break;
        case platform::ControlIcon::brake:
            // Wide brake pedal and its short supporting arm.
            line(6,6,18,6);line(18,6,20,8);line(20,8,20,14);
            line(20,14,18,16);line(18,16,6,16);line(6,16,4,14);
            line(4,14,4,8);line(4,8,6,6);line(12,16,12,21);
            line(8,9,8,13);line(12,9,12,13);line(16,9,16,13);
            break;
        case platform::ControlIcon::turbo:
            // Lightning bolt: a distinct burst of power, readable at small sizes.
            line(13,3,6,13);line(6,13,11,13);line(11,13,9,21);
            line(9,21,19,10);line(19,10,13,10);line(13,10,16,3);line(16,3,13,3);
            break;
        case platform::ControlIcon::none: break;
        }
        const float coverage=std::clamp(.5f-distance*scale,0.f,1.f);
        auto pixel=color; pixel.a=std::uint8_t(std::lround(float(color.a)*coverage));
        image.put(x,y,pixel);
    }
    return image;
}
} // namespace ui
