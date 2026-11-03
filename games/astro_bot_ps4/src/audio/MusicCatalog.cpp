#include "audio/MusicCatalog.hpp"

#include <algorithm>

namespace beacon::audio {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}   // namespace

void MusicCatalog::scan(const data::GameFiles& files, double minSeconds, double minNamedSeconds) {
    scan(files.list(".sxd"), minSeconds, minNamedSeconds);
}

void MusicCatalog::scan(const std::vector<std::filesystem::path>& paths, double minSeconds, double minNamedSeconds) {
    banks_.clear();
    tracks_.clear();
    levelRules_.clear();
    for (const auto& path : paths) {
        std::string stem = path.stem().string();
        if (!endsWith(stem, "_bgm")) {
            continue;
        }
        data::Bytes bytes;
        if (!data::loadFile(path, bytes)) {
            continue;
        }
        auto bank = data::loadSoundBank(bytes);
        if (!bank) {
            continue;
        }
        Bank b{stem, path, {}};
        for (const auto& n : bank->names) {
            if (!endsWith(n.name, "_tv")) {
                b.names.push_back(n.name);
            }
        }
        std::sort(b.names.begin(), b.names.end());
        for (std::size_t i = 0; i < bank->waves.size(); ++i) {
            const data::Wave& w = bank->waves[i];
            MusicTrack t{stem, i, {}, w.channels, w.sampleRate, w.samples, w.loopStart.has_value()};
            bool tvOnly = true;
            for (const auto& u : bank->waveUses[i]) {
                const std::string label = u.name + (u.state.empty() ? "" : " [" + u.state + "]");
                if (std::find(t.uses.begin(), t.uses.end(), label) == t.uses.end()) {
                    t.uses.push_back(label);
                }
                tvOnly = tvOnly && endsWith(u.name, "_tv");
            }
            t.tvOnly = !t.uses.empty() && tvOnly;
            // Title: the first non-TV use, else the first use, else bank and wave number.
            for (const auto& u : t.uses) {
                if (u.find("_tv") == std::string::npos) {
                    t.title = u;
                    break;
                }
            }
            if (t.title.empty()) {
                t.title = t.uses.empty() ? stem + " #" + std::to_string(i) : t.uses.front();
            }
            const bool namedMusic = !t.uses.empty() && t.title.find("bgm") != std::string::npos;   // jingles, stings
            if (t.seconds() >= minSeconds || (namedMusic && t.seconds() >= minNamedSeconds)) {
                // Layers: a piece whose sequences start several equally long waves together.
                auto same = std::find_if(tracks_.begin(), tracks_.end(), [&](const MusicTrack& o) {
                    return o.bank == t.bank && !t.uses.empty() && o.uses == t.uses && o.samples == t.samples;
                });
                if (same != tracks_.end()) {
                    same->layers.push_back(i);
                    continue;
                }
                t.layers = {i};
                tracks_.push_back(std::move(t));
            }
        }
        for (const auto& rule : bank->levelRules) {
            if (!endsWith(rule.name, "_tv")) {
                levelRules_.push_back(rule);
            }
        }
        banks_.push_back(std::move(b));
    }
    std::sort(tracks_.begin(), tracks_.end(), [](const MusicTrack& a, const MusicTrack& b) {
        if (a.uses.empty() != b.uses.empty()) {
            return b.uses.empty();   // named tracks first, unnamed (cinematic stems) last
        }
        return a.title != b.title ? a.title < b.title : a.wave < b.wave;
    });
}

const std::vector<std::string>& MusicCatalog::bankNames(const std::string& bank) const {
    static const std::vector<std::string> none;
    for (const auto& b : banks_) {
        if (b.stem == bank) {
            return b.names;
        }
    }
    return none;
}

std::vector<data::LevelRule> MusicCatalog::levelRules(const std::string& request) const {
    std::vector<data::LevelRule> out;
    for (const auto& r : levelRules_) {
        if (r.name == request) {
            out.push_back(r);
        }
    }
    return out;
}

int MusicCatalog::findRequest(const std::string& request) const {
    if (request.empty()) {
        return -1;
    }
    int first = -1;
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        for (const auto& u : tracks_[i].uses) {
            if (u == request) {
                return int(i);
            }
            if (first < 0 && u.size() > request.size() && u.compare(0, request.size() + 2, request + " [") == 0) {
                first = int(i);
            }
        }
    }
    return first;
}

bool MusicCatalog::decode(const MusicTrack& track, Pcm& out, std::size_t& loopStart, std::size_t& loopEnd,
                          std::string* error) const {
    auto it = std::find_if(banks_.begin(), banks_.end(), [&](const Bank& b) { return b.stem == track.bank; });
    data::Bytes bytes;
    if (it == banks_.end() || !data::loadFile(it->path, bytes)) {
        if (error) {
            *error = "cannot read the sound bank";
        }
        return false;
    }
    auto bank = data::loadSoundBank(bytes, error);
    if (!bank || track.wave >= bank->waves.size()) {
        return false;
    }
    const data::Wave& w = bank->waves[track.wave];
    if (!decodeWave(w, out, error)) {
        return false;
    }
    // Mix further layers in (same length and format by construction).
    for (std::size_t k = 1; k < track.layers.size(); ++k) {
        Pcm layer;
        if (track.layers[k] < bank->waves.size() && decodeWave(bank->waves[track.layers[k]], layer, error) &&
            layer.channels == out.channels) {
            const std::size_t n = std::min(out.samples.size(), layer.samples.size());
            for (std::size_t s2 = 0; s2 < n; ++s2) {
                out.samples[s2] =
                    std::int16_t(std::clamp(int(out.samples[s2]) + int(layer.samples[s2]), -32768, 32767));
            }
        }
    }
    loopStart = w.loopStart.value_or(0);
    loopEnd = std::min<std::size_t>(w.loopEnd ? *w.loopEnd + 1 : out.frames(), out.frames());
    if (loopStart >= loopEnd) {
        loopStart = 0;
        loopEnd = out.frames();
    }
    return true;
}

}   // namespace audio
