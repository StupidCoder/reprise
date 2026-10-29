#include "game/ambience.hpp"

#include "data/brd.hpp"
#include "game/random.hpp"

#include <cstdio>
#include <cstring>

namespace doc::game {

namespace {
// Length of a PCM WAV file in ms (data bytes over the byte rate of its "fmt " chunk).
int wavLengthMs(const data::GameFiles& files, const std::string& path) {
    auto bytes = files.readResource(path);
    if (!bytes || bytes->size() < 12) return 0;
    const auto& b = *bytes;
    auto u32 = [&](std::size_t at) {
        return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 |
               std::uint32_t(b[at + 3]) << 24;
    };
    std::uint32_t byteRate = 0, dataSize = 0;
    for (std::size_t at = 12; at + 8 <= b.size();) {
        const std::uint32_t size = u32(at + 4);
        if (!std::memcmp(&b[at], "fmt ", 4) && at + 20 <= b.size()) byteRate = u32(at + 16);
        if (!std::memcmp(&b[at], "data", 4)) dataSize = size;
        at += 8 + size + (size & 1);
    }
    return byteRate ? int(std::uint64_t(dataSize) * 1000 / byteRate) : 0;
}
}  // namespace

AmbienceScheme AmbienceScheme::load(const data::GameFiles& files, int mapNumber, int floor) {
    AmbienceScheme scheme;
    // SceneGame::loadAmbientSchemes @0xfc6b0: <floor id=".." scheme=".."/> per map floor.
    char path[64];
    std::snprintf(path, sizeof path, "map%02d/amb_schemes.xml.brd", mapNumber);
    auto schemes = data::loadChunkTree(files, path);
    if (!schemes) return scheme;
    std::string name;
    for (const data::BrdNode* f : schemes->childrenNamed("floor"))
        if (f->getInt("id").value_or(-1) == floor) name = f->getString("scheme").value_or("");
    if (name.empty()) return scheme;
    return loadNamed(files, name);
}

// SceneGame::playAmbientScheme @0xfbd54 and AmbiencePlayer::load @0x21098c.
AmbienceScheme AmbienceScheme::loadNamed(const data::GameFiles& files, const std::string& name) {
    AmbienceScheme scheme;
    const std::string folder = "sound/sound/amb/" + name + "/";
    auto root = data::loadChunkTree(files, folder + "amb.xml.brd");
    if (!root) return scheme;
    for (const data::BrdNode* loop : root->childrenNamed("loop"))
        scheme.loops.push_back(folder + loop->getString("file").value_or(""));
    for (const data::BrdNode* a : root->childrenNamed("ambience")) {
        Group g;
        g.minDelay = a->getFloat("min_delay").value_or(0.0f);
        g.maxDelay = a->getFloat("max_delay").value_or(0.0f);
        for (const data::BrdNode* sound : a->childrenNamed("sound")) {
            g.sounds.push_back(folder + sound->getString("file").value_or(""));
            g.lengthsMs.push_back(wavLengthMs(files, g.sounds.back()));
        }
        scheme.groups.push_back(std::move(g));
    }
    return scheme;
}

void Ambience::start(const AmbienceScheme& scheme, GameRandom& random) {
    scheme_ = scheme;
    timersMs_.clear();
    for (const AmbienceScheme::Group& g : scheme_.groups)
        timersMs_.push_back(int(random.within(g.minDelay, g.maxDelay) * 1000.0f));
}

std::vector<std::string> Ambience::update(int dtMs, GameRandom& random) {
    // AmbiencePlayer::update @0x210650.
    std::vector<std::string> started;
    for (Playing& p : playing_) p.leftMs -= dtMs;
    std::erase_if(playing_, [](const Playing& p) { return p.leftMs <= 0; });
    for (std::size_t i = 0; i < timersMs_.size(); ++i) {
        const AmbienceScheme::Group& g = scheme_.groups[i];
        int& timer = timersMs_[i];
        timer -= dtMs;
        if (timer > -1) continue;
        const unsigned draw = unsigned(random.next());
        if (!g.sounds.empty()) {
            const std::size_t k = draw % unsigned(g.sounds.size());
            started.push_back(g.sounds[k]);
            playing_.push_back({g.sounds[k], k < g.lengthsMs.size() ? g.lengthsMs[k] : 0});
        }
        timer = int(float(timer) + random.within(g.minDelay, g.maxDelay) * 1000.0f);
    }
    return started;
}

}  // namespace doc::game

namespace doc::game {

std::vector<std::string> Ambience::sounding(int laterMs) const {
    std::vector<std::string> files;
    for (const Playing& p : playing_)
        if (p.leftMs - laterMs > 0) files.push_back(p.file);
    return files;
}

}  // namespace doc::game
