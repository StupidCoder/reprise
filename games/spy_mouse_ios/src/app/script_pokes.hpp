// Research tools only (doc-verify, the app's --replay): reference scripts set the original's
// profile with pokes before the level (`FRAME poke 5fbee0*+OFF HEXVALUE`, g_pProfile in 1.1.1).
// Those that concern the power-ups are mapped onto the port's records: +0x120 + 0x18 t is
// power-up t's record (+8 count, +0xc bought, +0x10 used, +0x14 active and +0x15 introduced
// bytes), +8 and +9 the "met Digger" and "button tutorial done" bytes.
#pragma once

#include "game/profile.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace doc::app {

struct ScriptProfile {
    std::array<game::PowerupRecord, game::kPowerupCount> powerups = game::Profile{}.powerups;
    bool metDigger = false;
    bool tutorialDone = false;
};

inline ScriptProfile readScriptProfile(const std::string& path) {
    ScriptProfile p;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        int frame = 0;
        std::string kind, expr, value;
        if (!(ss >> frame >> kind >> expr >> value) || kind != "poke") continue;
        const std::string base = "5fbee0*+";
        if (expr.rfind(base, 0) != 0) continue;
        const int off = int(std::stoul(expr.substr(base.size()), nullptr, 16));
        const std::uint32_t v = std::uint32_t(std::stoul(value, nullptr, 16));
        if (off == 8) {
            p.metDigger = (v & 0xff) != 0;
            p.tutorialDone = ((v >> 8) & 0xff) != 0;
            continue;
        }
        if (off < 0x120 || off >= 0x120 + 0x18 * game::kPowerupCount) continue;
        game::PowerupRecord& r = p.powerups[std::size_t((off - 0x120) / 0x18)];
        switch ((off - 0x120) % 0x18) {
        case 0x8: r.count = int(v); break;
        case 0xc: r.bought = int(v); break;
        case 0x10: r.used = int(v); break;
        case 0x14:
            r.active = (v & 0xff) != 0;
            r.introduced = ((v >> 8) & 0xff) != 0;
            break;
        default: break;
        }
    }
    return p;
}

}  // namespace doc::app
