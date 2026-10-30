#pragma once
#include "platform/control_icon.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

namespace platform {
enum class Orientation { flexible, landscape, portrait };
struct InputCapabilities {
    bool mobile{}, touchscreen{}, gamepad{}, accelerometer{};
    bool operator==(const InputCapabilities&) const = default;
};
// Touch layouts use fractions of the safe drawing area. IDs belong to the game.
struct TouchControl {
    enum class Kind { stick, slider, button };
    std::string id, label;
    Kind kind{Kind::button};
    float x{}, y{}, width{}, height{};
    float initial{}; // slider only: 0 bottom, 1 top; sampled on a new layout
    float dead_zone{.12f};
    std::optional<float> displayed_value; // optional game-owned slider feedback; ignored during a drag
    ControlIcon icon{ControlIcon::none}; // label remains a textual fallback
    // Initial values seed a new layout; changing them must not release active fingers.
    bool operator==(const TouchControl& other) const {
        return std::tie(id,label,kind,x,y,width,height,dead_zone,icon)==
               std::tie(other.id,other.label,other.kind,other.x,other.y,other.width,other.height,other.dead_zone,other.icon);
    }
};
struct TouchValue {
    std::string id;
    float x{}, y{}; // stick [-1,1], positive y down; slider value in y [0,1]
    bool held{}, pressed{}, released{}, changed{};
};
struct TouchEvent {
    enum class Kind { down, move, up, cancel };
    Kind kind{};
    std::uint64_t id{};
    float x{}, y{}; // device pixels; independent of the game's picture
};
struct TouchArea {
    float x{}, y{}, width{1}, height{1};
    bool operator==(const TouchArea&) const = default;
};
// Input-only controller: deterministic and independent of SDL and game state.
// Edges remain pending until consume(), even if no simulation tick ran.
class TouchDeck {
public:
    void configure(const std::vector<TouchControl>& controls, TouchArea area, bool active) {
        if (controls != controls_) {
            cancel(); controls_ = controls; values_.clear();
            for (const auto& c : controls_) values_.push_back({c.id, 0, c.kind == TouchControl::Kind::slider ? std::clamp(c.initial, 0.f, 1.f) : 0});
        }
        if (area != area_ || active != active_) cancel();
        area_ = area; active_ = active;
        for (std::size_t k=0;k<controls.size();++k) {
            const auto feedback=controls[k].displayed_value;
            if (controls[k].kind==TouchControl::Kind::slider && !values_[k].held && !values_[k].changed &&
                feedback && std::isfinite(*feedback)) values_[k].y=std::clamp(*feedback,0.f,1.f);
        }
    }
    void clear() { cancel(); controls_.clear(); values_.clear(); active_ = false; }
    void cancel() {
        for (std::size_t k = 0; k < values_.size(); ++k) {
            auto& v = values_[k];
            v.released |= v.held; v.held = false; v.pressed = false; v.changed = false;
            if (controls_[k].kind == TouchControl::Kind::stick) v.x = v.y = 0;
        }
        captures_.clear();
    }
    TouchArea bounds(std::size_t k) const {
        const auto& c = controls_[k];
        return {area_.x + c.x * area_.width, area_.y + c.y * area_.height,
                c.width * area_.width, c.height * area_.height};
    }
    void event(const TouchEvent& e) {
        if (!active_ || !std::isfinite(e.x) || !std::isfinite(e.y)) return;
        auto it = captures_.find(e.id);
        if (e.kind == TouchEvent::Kind::down) {
            if (it != captures_.end()) return;
            for (std::size_t k = 0; k < controls_.size(); ++k) {
                const auto b = bounds(k);
                if (values_[k].held || b.width <= 0 || b.height <= 0 || e.x < b.x || e.y < b.y || e.x >= b.x+b.width || e.y >= b.y+b.height) continue;
                captures_[e.id] = {k, e.x, e.y};
                values_[k].held = values_[k].pressed = true;
                it = captures_.find(e.id); break;
            }
        }
        if (it == captures_.end()) return; // never acquire a control by sliding onto it
        const auto capture = it->second;
        auto& v = values_[capture.index]; const auto& c = controls_[capture.index];
        const auto b = bounds(capture.index);
        if (e.kind == TouchEvent::Kind::up || e.kind == TouchEvent::Kind::cancel) {
            v.held = false; v.released = true; v.changed = true;
            if (e.kind == TouchEvent::Kind::cancel) v.pressed = false;
            if (c.kind == TouchControl::Kind::stick) v.x = v.y = 0;
            captures_.erase(it); return;
        }
        v.changed = true;
        if (c.kind == TouchControl::Kind::slider) v.y = std::clamp(1.f-(e.y-b.y)/b.height, 0.f, 1.f);
        if (c.kind == TouchControl::Kind::stick) {
            const float radius = std::max(1.f, std::min(b.width,b.height)*.35f);
            float x = (e.x-capture.x)/radius, y = (e.y-capture.y)/radius;
            const float length = std::hypot(x,y), dead = std::clamp(c.dead_zone,0.f,.95f);
            const float magnitude = std::clamp((length-dead)/(1.f-dead),0.f,1.f);
            v.x = length > 0 ? x/length*magnitude : 0;
            v.y = length > 0 ? y/length*magnitude : 0;
        }
    }
    std::vector<TouchValue> consume() {
        auto result = values_;
        for (auto& v : values_) v.pressed = v.released = v.changed = false;
        return result;
    }
    const auto& values() const { return values_; }
    const auto& controls() const { return controls_; }
    bool active() const { return active_; }
    std::pair<float,float> centre(std::size_t k) const {
        for (const auto& [id,c] : captures_) if(c.index==k) return {c.x,c.y};
        auto b=bounds(k); return {b.x+b.width/2,b.y+b.height/2};
    }
private:
    struct Capture { std::size_t index; float x,y; };
    std::vector<TouchControl> controls_;
    std::vector<TouchValue> values_;
    std::map<std::uint64_t,Capture> captures_;
    TouchArea area_;
    bool active_{};
};
} // namespace platform
