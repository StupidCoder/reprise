// The fourth adventure's loop base. See a4_loop.hpp.
#include "system/a4_loop.hpp"

namespace picarat::adventure4 {

Loop::Loop(Scene& scene, Services& services, std::span<const Value> arguments)
    : scene_(scene), services_(services) {
    // 020e2004: a scope level of the loop's own; the pen counts from its
    // next press.
    level_ = scene.level++;
    upper_ = scene.upper;
    services.touch.state = -1;
    for (std::size_t i = 0; i < arguments.size() && i < values_.size(); ++i) {
        values_[i] = arguments[i];
    }
    // 020e2004: both threads get the loop's eight values.
    for (auto& t : threads) {
        t = std::make_unique<gss::Thread>(scene.machine, 16);
        for (std::size_t i = 0; i < values_.size(); ++i) {
            t->values[i] = values_[i];
        }
    }
}

Loop::~Loop() {
    // 020e20c8: everything the loop made goes (020ce7a0).
    while (scene_.level > level_) {
        scene_.freeLevel();
    }
    // The screen on top goes back to what it was (+0x38, 0209ee30).
    scene_.upper = upper_;
    services_.touch.state = -1;
    if (services_.loop == this) {
        services_.loop = nullptr;
    }
}

void Loop::resume() {
    // 020e2128: a level for what the loop shows while on top; the pen
    // counts from its next press; the loop takes input again (+0x48).
    // Both screens show their objects fully again (0209f4c4).
    topLevel_ = scene_.level++;
    for (auto& s : scene_.screens) {
        s.blend.set(kOne); // 0209f4c4
    }
    services_.touch.state = -1;
    busy_ = false;
    services_.loop = this;
}

void Loop::suspend() {
    // 020e21a0: input off, the on-top level goes (020ce7a0). (020cd4ac
    // also stops the screens' background films.)
    busy_ = false;
    services_.touch.state = -1;
    if (topLevel_ >= 0) {
        while (scene_.level > topLevel_) {
            scene_.freeLevel();
        }
        topLevel_ = -1;
    }
}

void Loop::begin() {
    if (beginsWithMain) {
        if (const gss::Program* p = scene_.newestScript()) {
            start(*p, "Main");
        }
    }
}

bool Loop::start(const gss::Program& program, std::string_view block, int thread,
                 std::string_view label) {
    if (thread < 0 || thread > 1) {
        return false;
    }
    program_ = &program;
    services_.loop = this;
    return threads[std::size_t(thread)]->start(program, block, label);
}

bool Loop::running() const {
    return threads[0]->running() || threads[1]->running();
}

bool Loop::running(int t) const {
    return t >= 0 && t < 2 && threads[std::size_t(t)]->running();
}

bool Loop::command(gss::Call& call, int id) {
    return commands && commands(call, id);
}

bool Loop::callback(int thread, const std::string& block, std::span<const gss::Value> args) {
    if (thread < 0 || thread > 1) {
        return false;
    }
    return threads[std::size_t(thread)]->callback(block, args);
}

bool Loop::startThread(int thread, const std::string& block) {
    // 020e1d80 -> ov7 020ce8c0: the thread starts in the newest script
    // loaded (0208c788), so blocks of scripts loaded since the loop began
    // (4502) are found from there on.
    const gss::Program* newest = scene_.newestScript();
    if (thread < 0 || thread > 1 || !newest) {
        return false;
    }
    return threads[std::size_t(thread)]->start(*newest, block);
}

Loop::Target Loop::dispatch(const Target& previous) const {
    // ov7 020dd108: an object still held keeps the pen while its state
    // asks for it; otherwise a new press goes to the topmost object hit.
    const auto& touch = services_.touch;
    const bool down = touch.state == 1 || touch.state == 2;
    Target current;
    if (previous.object) {
        if (const Chara* o = scene_.find(previous.hash); o && o->touchable()) {
            auto onOrOff = [&] {
                // On the object: area 0; dragged off it: area 3.
                current = {o->hash, true, unsigned(o->hit(touch.x, touch.y)) < 3 ? 0 : 3};
            };
            switch (o->state) {
            case 1: // sliding
            case 2:
                if (down) {
                    current = {o->hash, true, previous.area};
                }
                break;
            case 4: // pressed on a button
                if (touch.state == 3) {
                    current = {o->hash, true, -1};
                } else if (down) {
                    onOrOff();
                }
                break;
            case 5: // pressed, pen off the button
                if (down) {
                    onOrOff();
                }
                break;
            default:
                break;
            }
        }
    }
    if (touch.state == 1 && !current.object) {
        int area = -1;
        if (const Chara* o = scene_.objectAt(scene_.touchScreen(), touch.x, touch.y, area)) {
            current = {o->hash, true, area};
        }
    }
    return current;
}

void Loop::transition(Target& current, const Target& previous) {
    // ov7 020dd4d0: the objects' states follow the pen.
    if (!previous.object) {
        if (!current.object) {
            return;
        }
        Chara* o = scene_.find(current.hash);
        if (!o) {
            return;
        }
        if (!o->touchable()) {
            current = {};
            return;
        }
        int state = -1;
        if (current.area == 0) {
            // A new touch: touch (5112), select (5113) or slide (5114).
            state = o->touchKind == 1 ? 3 : o->touchKind == 2 ? 4 : o->touchKind == 3 ? 1 : -1;
        } else if (current.area == 1 || current.area == 2) {
            state = current.area;
        }
        if (state >= 0) {
            o->enterState(state);
        }
        return;
    }
    if (current.object && current.hash == previous.hash && current.area == previous.area) {
        return;
    }
    Chara* o = scene_.find(previous.hash);
    if (!o) {
        return;
    }
    const bool same = o->touchable() && current.object && current.hash == previous.hash;
    switch (o->state) {
    case 1:
    case 2: // the slide ends
        o->enterState(0);
        break;
    case 4: // released on the button: click; dragged off: state 5
        o->enterState(!same ? 0 : current.area == -1 ? 6 : current.area == 3 ? 5 : 0);
        break;
    case 5: // back on the button
        if (same && current.area == 0) {
            o->enterState(4);
        }
        break;
    default:
        break;
    }
}

void Loop::paintTouch(Target& current, const Target& previous) {
    // ov7 020e0ab8: a held paint keeps the pen while it is down; a new
    // press goes to the topmost paint whose area holds it.
    const auto& touch = services_.touch;
    if (previous.paint) {
        if (Paint* p = scene_.paint(previous.hash);
            p && p->touchable() && (touch.state == 1 || touch.state == 2)) {
            current = {p->hash, false, previous.area, true};
        }
    }
    if (touch.state == 1 && !current.paint) {
        if (Paint* p = scene_.paintAt(scene_.touchScreen(), touch.x, touch.y)) {
            current = {p->hash, false, 0, true};
        }
    }
}

void Loop::paintTransition(Target& current, const Target& previous) {
    // ov7 020e0d9c: press, move and release follow the pen.
    const auto& touch = services_.touch;
    if (!previous.paint) {
        if (!current.paint) {
            return;
        }
        Paint* p = scene_.paint(current.hash);
        if (!p) {
            return;
        }
        if (!p->touchable()) {
            current = {};
            return;
        }
        if (p->clearOnPress) {
            scene_.clearPaint(*p);
        }
        p->press(touch.x, touch.y);
        return;
    }
    if (current.paint && current.hash == previous.hash && current.area == previous.area) {
        if (Paint* p = scene_.paint(current.hash)) {
            p->move(touch.x, touch.y);
        }
        return;
    }
    if (Paint* p = scene_.paint(previous.hash)) {
        // lt4CPaint 020dfd1c: a handwriting box reads its strokes.
        p->release();
        if (p->mode & 0x10) {
            scene_.readHandwriting(*p);
        }
    }
}

void Loop::frame(bool touching, int x, int y) {
    const std::int32_t elapsed = kOne;
    services_.loop = this;
    // 020e22e4: the pen, the touch target (unless the loop is busy), the
    // threads, the objects' touch states, game data, screens, resources,
    // then the objects and the messages.
    const Target previous = target_;
    target_ = {};
    services_.touch.update(touching, x, y);
    if (!busy_) {
        // Paints first; objects only get a pen no paint took.
        paintTouch(target_, previous);
        if (!target_.paint) {
            target_ = dispatch(previous);
        }
        afterPen();
    }
    for (auto& t : threads) {
        t->run();
    }
    paintTransition(target_, previous);
    if (!target_.paint) {
        transition(target_, previous.paint ? Target{} : previous);
    }
    scene_.updateScreens(elapsed);
    scene_.updateObjects(elapsed);
    scene_.updateMessages(elapsed);
    scene_.updatePaints();
    if (endsWithMain && begun && services_.loops && !services_.loops->pending() && !running(0)) {
        services_.loops->pop();
    }
    afterUpdate();
    endFrame();
}

} // namespace picarat::adventure4
