#include "app/event_script.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace app {
namespace {

SDL_Window* window() {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* first = count > 0 ? windows[0] : nullptr;
    SDL_free(windows);
    return first;
}

void push_button(Uint32 type, float x, float y) {
    SDL_Event e{};
    e.type = type;
    e.button.windowID = window() ? SDL_GetWindowID(window()) : 0;
    e.button.which = 0;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

void push_motion(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = window() ? SDL_GetWindowID(window()) : 0;
    e.motion.state = SDL_BUTTON_LMASK;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
}

// Movement alone, as a captured mouse reports it (Host::set_mouse_captured).
void push_relative(float dx, float dy) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = window() ? SDL_GetWindowID(window()) : 0;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    SDL_PushEvent(&e);
}

void push_key(const std::string& name) {
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mod = SDL_KMOD_NONE;
    std::string base = name;
    if (base.rfind("cmd+", 0) == 0) {
        mod = SDL_KMOD_LGUI;
        base = base.substr(4);
    }
    if (base == "escape") key = SDLK_ESCAPE;
    else if (base == "return") key = SDLK_RETURN;
    else if (base == "left") key = SDLK_LEFT;
    else if (base == "right") key = SDLK_RIGHT;
    else if (base == "up") key = SDLK_UP;
    else if (base == "down") key = SDLK_DOWN;
    else if (base.size() == 1) key = SDL_Keycode(base[0]);
    for (const bool down : {true, false}) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = window() ? SDL_GetWindowID(window()) : 0;
        e.key.key = key;
        e.key.mod = mod;
        e.key.down = down;
        SDL_PushEvent(&e);
    }
}

} // namespace

EventScript::EventScript(const std::filesystem::path& file, std::function<void(const std::filesystem::path&)> shot)
    : shot_(std::move(shot)) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("Cannot read the event script " + file.string() + ".");
    for (std::string text; std::getline(in, text);) {
        std::istringstream ss(text);
        Line line;
        if (!(ss >> line.at >> line.kind) || line.kind.empty() || line.kind[0] == '#') continue;
        if (line.kind == "key" || line.kind == "shot" || line.kind == "drop") {
            std::getline(ss >> std::ws, line.text);
        } else {
            for (float v; ss >> v;) line.numbers.push_back(v);
        }
        lines_.push_back(std::move(line));
    }
}

void EventScript::pump(double seconds) {
    if (drag_) {
        const auto& n = dragging_.numbers;
        const double t = std::min(1.0, (seconds - drag_start_) / std::max(0.01, double(n[4])));
        push_motion(float(n[0] + (n[2] - n[0]) * t), float(n[1] + (n[3] - n[1]) * t));
        if (t >= 1.0) {
            push_button(SDL_EVENT_MOUSE_BUTTON_UP, n[2], n[3]);
            drag_ = false;
        }
        return;  // (nothing else while a drag lasts)
    }
    while (next_ < lines_.size() && lines_[next_].at <= seconds) {
        const Line& line = lines_[next_++];
        const auto& n = line.numbers;
        if (line.kind == "down" && n.size() >= 2) push_button(SDL_EVENT_MOUSE_BUTTON_DOWN, n[0], n[1]);
        else if (line.kind == "up" && n.size() >= 2) push_button(SDL_EVENT_MOUSE_BUTTON_UP, n[0], n[1]);
        else if (line.kind == "move" && n.size() >= 2) push_motion(n[0], n[1]);
        else if (line.kind == "rel" && n.size() >= 2) push_relative(n[0], n[1]);
        else if (line.kind == "drag" && n.size() >= 5) {
            push_button(SDL_EVENT_MOUSE_BUTTON_DOWN, n[0], n[1]);
            dragging_ = line;
            drag_ = true;
            drag_start_ = seconds;
            return;
        } else if (line.kind == "key") push_key(line.text);
        else if (line.kind == "shot" && shot_) shot_(line.text);
        else if (line.kind == "drop") {
            // (The line keeps the name alive while the host reads the event.)
            SDL_Event e{};
            e.type = SDL_EVENT_DROP_FILE;
            e.drop.windowID = window() ? SDL_GetWindowID(window()) : 0;
            e.drop.data = line.text.c_str();
            SDL_PushEvent(&e);
        }
        else if (line.kind == "quit") {
            SDL_Event e{};
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
        }
    }
}

} // namespace app
