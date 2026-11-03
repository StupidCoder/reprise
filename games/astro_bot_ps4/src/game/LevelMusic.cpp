#include "game/LevelMusic.hpp"

#include "game/LevelDocument.hpp"

namespace beacon::game {

void LevelMusic::load(const Level& level) {
    players_.clear();
    cues_.clear();
    for (const LevelObject& o : level.objects) {
        if (o.className != "bgm_player" || o.asTemplate) {
            continue;
        }
        Player p;
        p.requests = {o.text("Request"), o.text("RequestHero"), o.text("RequestController")};
        p.link = resolveLink(level, o, "TriggerBox");
        // TriggerType is an enum property; the level files hold its name.
        const std::string type = o.text("TriggerType", "Normal");
        p.normal = type == "Normal" || type == "0";
        p.fadeIn = o.number("FadeInTime").value_or(0.0f);
        p.fadeOut = o.number("FadeOutTime").value_or(0.5f);
        players_.push_back(std::move(p));
    }
}

void LevelMusic::start() {
    for (Player& p : players_) {
        if (p.normal && p.link.empty()) {
            play(p);
            p.state = 1;
        }
    }
}

void LevelMusic::update(const LevelRuntime& runtime) {
    for (Player& p : players_) {
        switch (p.state) {
        case 2:
            if (p.normal && p.link.empty()) {
                stopAll();
                p.state = 0;
            }
            break;
        case 1:
            if (!p.link.empty() && runtime.linkFired(p.link)) {
                play(p);
                p.state = 2;
            }
            break;
        case 0:
            if (!p.normal) {
                break;
            }
            if (p.link.empty()) {
                play(p);
                p.state = 1;
            } else if (runtime.linkFired(p.link)) {
                play(p);
                p.state = 2;
            }
            break;
        default:
            break;
        }
    }
}

void LevelMusic::heroSpawnLanded() {
    for (Player& p : players_) {
        if (!p.normal) {
            p.state = 0;
            play(p);
            p.state = 2;
        }
    }
}

void LevelMusic::play(Player& p) {
    // 0x30c9b0 only acts in state 0, in state 1 without a link, or in state 2.
    if (p.state == 1 && !p.link.empty()) {
        return;
    }
    for (int slot = 0; slot < kSlots; ++slot) {
        const std::string& request = p.requests[std::size_t(slot)];
        cues_.push_back({slot, request, request.empty() ? p.fadeOut : p.fadeIn});
    }
}

void LevelMusic::stopAll() {
    for (int slot = 0; slot < kSlots; ++slot) {
        cues_.push_back({slot, {}, 0});
    }
}

}   // namespace game
