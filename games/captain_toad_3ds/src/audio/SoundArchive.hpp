// =============================================================================================
// SoundArchive.hpp - the game's sound data and the middleware's lookup tables
// =============================================================================================
//
// The game's sounds live in the CSAR archive (sound_data.bcsar: sounds, wave-sound data, wave
// archives, references to external stream files). On top of it the game uses a sound
// middleware with its own tables, which address sounds by a hash of their name. This file reads
// both.
//
// Recovered from code.bin:
//   * Name hash 0x270d9c: a bitwise CRC (initial value 0x87654321, polynomial 0x04c11db7). Per
//     character it feeds bits 6, 4, 3, 2, 1, 0 and one zero bit, so letter case is ignored; the
//     final carry ORs 1.
//   * .DAT tables (loader 0x30a96c): u32 size + hash table (u32 count, hashes), then type data.
//     Lookup vt+0x54 (0x37a0f0) returns prefix | index (prefixes at 0x48e260).
//   * SNDTBLS.DAT, type 0: wave-sound name hashes in CSAR wave-sound order.
//   * SFXSCRIPTFILES.DAT, type 2: script name hashes; the directory (u32 count | divisor << 28,
//     then (u32 offset, u16 size, u8 volume, u8 ?) per script); the scripts themselves
//     (u8 mode, u8 ?, u16 n, n x 0x18-byte entries; see ScriptEntry and SoundScript.hpp).
//   * STREAMFILEINFO.DAT, type 4: stream name hashes (BCSAR "STREAMS_" names without the prefix).
// CSAR / CWSD / CWAR parsing follows the general NW4C layouts (typed references, option
// parameters).
// =============================================================================================
#pragma once
#include "audio/AudioFormats.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace captain_toad::audio {

// 0x270d9c: the middleware's sound name hash.
inline uint32_t soundNameHash(const char* s) {
    uint32_t r = 0x87654321u;
    const uint32_t polynomial = 0x04c11db7u;
    for (size_t i = 0; s[i]; ++i) {
        const int ch = int8_t(s[i]);
        for (int bit : {6, 4, 3, 2, 1, 0}) {
            const uint32_t carry = r >> 31;
            r = (r << 1) | ((ch >> bit) & 1);
            if (carry) {
                r ^= polynomial;
            }
        }
        const uint32_t carry = r >> 31;
        r <<= 1;
        if (s[i + 1]) {
            if (carry) {
                r ^= polynomial;
            }
        } else if (carry) {
            r |= 1;
        }
    }
    return r;
}

// Id prefixes of lookup vt+0x54 (table 0x48e260).
enum : uint32_t {
    kIdWave = 0,
    kIdScript = 0x200000,
    kIdStream = 0x20000,
    kIdNone = 0xffffffffu,
};

inline std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// One 0x18-byte script entry (recovered from 0x11f624, 0x111cc8, 0x11881c).
struct ScriptEntry {
    uint32_t sound;          // name hash; resolved at load (0x111cc8) with mask 0x3f to an id
    uint32_t adsr;           // ADSR name hash (mask 0x200), or a time when flags & 0x100
    uint32_t pitch;          // Q12 multiplier
    uint8_t pitchRandSemi;   // random pitch raise, semitones
    uint8_t pitchRandCent;   // random pitch raise, cents
    uint16_t delay;          // scaled at load: delay * rate / divisor (update ticks)
    uint16_t flags;          // 0x8 stop-by-pan-byte, 0x40 loop script, 0x80 stop, 0x100 timed
    uint8_t volMin;
    uint8_t volMax;
    int8_t panMin;   // -1: instance pan
    int8_t panMax;
    uint8_t sendA;
    uint8_t sendB;
    uint32_t id = kIdNone;   // resolved sound id
};

struct Script {
    uint8_t mode = 0;    // 0 timeline, 1 play one entry (random unless forced)
    uint8_t mode1 = 0;   // non-zero: no random choice in mode 1
    uint8_t volume = 127;
    uint8_t dirByte = 0;
    std::vector<ScriptEntry> entries;
};

// Wave sound data (CWSD) for one wave sound.
struct WaveSoundInfo {
    uint8_t pan = 64;
    uint8_t surroundPan = 0;
    float pitch = 1.f;
    uint8_t mainSend = 127;
    uint8_t adsr[5] = {127, 127, 127, 127, 127};   // attack, decay, sustain, hold, release
    uint32_t archive = 0;                          // CSAR wave archive id
    uint32_t wave = 0;                             // index in the CWAR
};

struct SoundEntry {
    std::string name;
    enum Kind { Stream, Wave, Sequence, Other } kind = Other;
    uint32_t file = 0;
    uint32_t player = 0;
    uint8_t volume = 127;
    uint32_t waveIndex = 0;   // index within the CWSD (wave sounds)
};

class SoundArchive {
public:
    explicit SoundArchive(const std::string& soundDataDir) : dir_(soundDataDir) {
        csar_ = readFile(dir_ + "/sound_data.bcsar");
        parseCsar();
        loadTables();
    }

    const std::vector<SoundEntry>& sounds() const { return sounds_; }

    uint32_t waveSoundCount() const { return uint32_t(waveSounds_.size()); }

    const Script* script(uint32_t index) const { return index < scripts_.size() ? &scripts_[index] : nullptr; }

    uint32_t scriptDivisor() const { return scriptDivisor_; }

    // Lookup vt+0x54 for the mask bits used by the opening (1 wave, 4 script, 0x10 stream).
    uint32_t lookup(uint32_t hash, uint32_t mask) const {
        auto find = [&](const std::vector<uint32_t>& table) -> int {
            for (size_t i = 0; i < table.size(); ++i) {
                if (table[i] == hash) {
                    return int(i);
                }
            }
            return -1;
        };
        int i;
        if ((mask & 1) && (i = find(waveHashes_)) >= 0) {
            return kIdWave | uint32_t(i);
        }
        if ((mask & 4) && (i = find(scriptHashes_)) >= 0) {
            return kIdScript | uint32_t(i);
        }
        if ((mask & 0x10) && (i = find(streamHashes_)) >= 0) {
            return kIdStream | uint32_t(i);
        }
        return kIdNone;
    }

    uint32_t lookup(const char* name, uint32_t mask) const { return lookup(soundNameHash(name), mask); }

    // Wave sound i (SNDTBLS order = CSAR wave-sound order) -> its CWSD record.
    const WaveSoundInfo& waveSound(uint32_t i) const { return waveSounds_.at(i); }

    const SoundEntry& waveSoundEntry(uint32_t i) const { return sounds_.at(waveSoundIndex_.at(i)); }

    // Decoded wave (cached).
    const Pcm& wave(uint32_t archive, uint32_t index) {
        uint64_t key = uint64_t(archive) << 32 | index;
        auto it = waves_.find(key);
        if (it != waves_.end()) {
            return *it->second;
        }
        const auto& f = files_.at(archives_.at(archive));
        Cwar w = parseCwar(csar_.data() + f.first, f.second);
        auto& e = w.waves.at(index);
        auto p = std::make_unique<Pcm>(decodeCwav(csar_.data() + f.first + e.first, e.second));
        return *(waves_[key] = std::move(p));
    }

    // Every wave of every wave archive: (archive, index, channel-0 DSP coefficients, length).
    struct WaveKey {
        uint32_t archive;
        uint32_t index;
        std::array<int16_t, 16> coef;
        uint32_t length;
    };

    std::vector<WaveKey> waveKeys() const {
        std::vector<WaveKey> out;
        for (uint32_t a = 0; a < archives_.size(); ++a) {
            const auto& f = files_.at(archives_[a]);
            if (!f.second) {
                continue;
            }
            Cwar w = parseCwar(csar_.data() + f.first, f.second);
            for (uint32_t i = 0; i < w.waves.size(); ++i) {
                const uint8_t* c = csar_.data() + f.first + w.waves[i].first;
                const uint8_t* infoBody = findBlock(c, w.waves[i].second, 0x7000) + 8;
                if (infoBody[0] != 2) {
                    continue;
                }
                const uint8_t* channelTable = infoBody + 0x14;
                const uint8_t* channelInfo = channelTable + rd32(channelTable + 8);
                DspAdpcmInfo d = readDspInfo(channelInfo + rd32(channelInfo + 0xc));
                WaveKey k{a, i, {}, rd32(infoBody + 0xc)};
                std::copy(d.coef, d.coef + 16, k.coef.begin());
                out.push_back(k);
            }
        }
        return out;
    }

    // Stream by stream-table index (STREAMFILEINFO order = CSAR stream-sound order).
    std::string streamPath(uint32_t i) const {
        const SoundEntry& s = sounds_.at(streamSoundIndex_.at(i));
        auto it = externalPaths_.find(s.file);
        return it == externalPaths_.end() ? std::string() : dir_ + "/" + it->second;
    }

    const SoundEntry& streamEntry(uint32_t i) const { return sounds_.at(streamSoundIndex_.at(i)); }

private:
    std::string dir_;
    std::vector<uint8_t> csar_;
    std::vector<std::string> strings_;
    std::vector<SoundEntry> sounds_;
    std::vector<std::pair<uint32_t, uint32_t>> files_;   // internal (offset, size)
    std::map<uint32_t, std::string> externalPaths_;
    std::vector<uint32_t> archives_;           // wave archive id -> file id
    std::vector<uint32_t> waveSoundIndex_;     // wave sound -> CSAR sound index
    std::vector<uint32_t> streamSoundIndex_;   // stream sound -> CSAR sound index
    std::vector<WaveSoundInfo> waveSounds_;
    std::vector<uint32_t> waveHashes_;
    std::vector<uint32_t> scriptHashes_;
    std::vector<uint32_t> streamHashes_;
    std::vector<Script> scripts_;
    uint32_t scriptDivisor_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<Pcm>> waves_;

    const uint8_t* at(size_t o) const { return csar_.data() + o; }
    uint32_t u32(size_t o) const { return rd32(at(o)); }
    int32_t s32(size_t o) const { return int32_t(rd32(at(o))); }
    uint16_t u16(size_t o) const { return rd16(at(o)); }

    // Option parameter: u32 flags, then one u32 per set bit.
    std::map<int, uint32_t> options(size_t o) const {
        std::map<int, uint32_t> v;
        uint32_t flags = u32(o);
        size_t p = o + 4;
        for (int b = 0; b < 32; ++b) {
            if (flags >> b & 1) {
                v[b] = u32(p);
                p += 4;
            }
        }
        return v;
    }

    void parseCsar() {
        const uint8_t* f = csar_.data();
        if (std::memcmp(f, "CSAR", 4)) {
            throw std::runtime_error("not CSAR");
        }
        uint32_t strg = 0;
        uint32_t info = 0;
        uint32_t file = 0;
        for (int i = 0, n = rd16(f + 0x10); i < n; ++i) {
            uint16_t id = u16(0x14 + i * 12);
            uint32_t off = u32(0x14 + i * 12 + 4);
            if (id == 0x2000) {
                strg = off;
            }
            if (id == 0x2001) {
                info = off;
            }
            if (id == 0x2002) {
                file = off;
            }
        }

        // Strings (0x2400 table: (type, offset, size) per string, relative to the table).
        size_t stringBlock = strg + 8;
        size_t table = stringBlock + s32(stringBlock + 4);
        for (uint32_t i = 0, n = u32(table); i < n; ++i) {
            size_t e = table + 4 + i * 12;
            strings_.emplace_back(reinterpret_cast<const char*>(at(table + u32(e + 4))));
        }

        // INFO: eight references; tables are classified by their entries' type ids.
        size_t infoBody = info + 8;
        std::map<uint16_t, size_t> tables;
        for (int i = 0; i < 8; ++i) {
            int32_t off = s32(infoBody + 8 * i + 4);
            if (off < 0) {
                continue;
            }
            size_t t = infoBody + off;
            if (u32(t) == 0) {
                continue;
            }
            tables[u16(t + 4)] = t;
        }
        auto each = [&](uint16_t type, auto fn) {
            auto it = tables.find(type);
            if (it == tables.end()) {
                return;
            }
            size_t t = it->second;
            for (uint32_t i = 0, n = u32(t); i < n; ++i) {
                fn(i, t + s32(t + 4 + 8 * i + 4));
            }
        };

        // Files: internal (inside the FILE block) or external (a path).
        each(0x220a, [&](uint32_t, size_t o) {
            uint16_t type = u16(o);
            size_t p = o + s32(o + 4);
            if (type == 0x220c) {
                int32_t fileOffset = s32(p + 4);
                files_.push_back({fileOffset >= 0 ? uint32_t(file + 8 + fileOffset) : 0u, u32(p + 8)});
            } else {
                if (type == 0x220d) {
                    externalPaths_[uint32_t(files_.size())] = reinterpret_cast<const char*>(at(p));
                }
                files_.push_back({0, 0});
            }
        });

        // Wave archives.
        each(0x2207, [&](uint32_t, size_t o) { archives_.push_back(u32(o)); });

        // Sounds.
        each(0x2200, [&](uint32_t i, size_t o) {
            SoundEntry s;
            s.file = u32(o);
            s.player = u32(o + 4) & 0xffffff;
            s.volume = at(o + 8)[0];
            uint16_t detailType = u16(o + 0xc);
            size_t detail = o + s32(o + 0x10);
            auto opt = options(o + 0x14);
            if (opt.count(0) && opt[0] != 0xffffffffu) {
                s.name = strings_.at(opt[0] & 0xffffff);
            }
            if (detailType == 0x2201) {
                s.kind = SoundEntry::Stream;
                streamSoundIndex_.push_back(i);
            }
            if (detailType == 0x2203) {
                s.kind = SoundEntry::Sequence;
            }
            if (detailType == 0x2202) {
                s.kind = SoundEntry::Wave;
                s.waveIndex = u32(detail);
                waveSoundIndex_.push_back(i);
            }
            sounds_.push_back(s);
        });

        for (uint32_t i : waveSoundIndex_) {
            waveSounds_.push_back(parseWaveSound(sounds_[i]));
        }
    }

    // CWSD: INFO references (wave id table, wave sound info table); each wave sound info has
    // references to its details, tracks and notes.
    WaveSoundInfo parseWaveSound(const SoundEntry& s) const {
        WaveSoundInfo w;
        const auto& f = files_.at(s.file);
        const uint8_t* c = csar_.data() + f.first;
        if (!f.second || std::memcmp(c, "CWSD", 4)) {
            return w;
        }
        const uint8_t* info = findBlock(c, f.second, 0x6800);
        const uint8_t* infoBody = info + 8;
        const uint8_t* ids = infoBody + rd32(infoBody + 4);
        const uint8_t* table = infoBody + rd32(infoBody + 0xc);
        const uint8_t* waveSoundInfo = table + rd32(table + 4 + s.waveIndex * 8 + 4);
        const uint8_t* detail = waveSoundInfo + rd32(waveSoundInfo + 4);

        uint32_t flags = rd32(detail);
        const uint8_t* v = detail + 4;
        std::map<int, uint32_t> opt;
        for (int b = 0; b < 32; ++b) {
            if (flags >> b & 1) {
                opt[b] = rd32(v);
                v += 4;
            }
        }
        if (opt.count(0)) {
            w.pan = opt[0] & 0xff;
            w.surroundPan = (opt[0] >> 8) & 0xff;
        }
        if (opt.count(1)) {
            std::memcpy(&w.pitch, &opt[1], 4);
        }
        if (opt.count(8)) {
            w.mainSend = detail[opt[8]];
        }
        if (opt.count(9)) {
            const uint8_t* r = detail + opt[9];
            std::memcpy(w.adsr, r + rd32(r + 4), 5);
        }

        const uint8_t* notes = waveSoundInfo + rd32(waveSoundInfo + 0x14);
        if (rd32(notes)) {
            const uint8_t* note = notes + rd32(notes + 8);
            uint32_t waveId = rd32(note);
            const uint8_t* e = ids + 4 + waveId * 8;
            w.archive = rd32(e) & 0xffffff;
            w.wave = rd32(e + 4);
        }
        return w;
    }

    // A .DAT hash table at `off`; `end` receives the offset of the type data that follows.
    static std::vector<uint32_t> hashTable(const std::vector<uint8_t>& t, size_t off, size_t* end) {
        uint32_t size = rd32(t.data() + off);
        uint32_t n = rd32(t.data() + off + 4);
        std::vector<uint32_t> h(n);
        for (uint32_t i = 0; i < n; ++i) {
            h[i] = rd32(t.data() + off + 8 + i * 4);
        }
        if (end) {
            *end = off + size;
        }
        return h;
    }

    void loadTables() {
        auto sndTables = readFile(dir_ + "/SNDTBLS.DAT");
        waveHashes_ = hashTable(sndTables, 0, nullptr);
        auto streamInfo = readFile(dir_ + "/STREAMFILEINFO.DAT");
        streamHashes_ = hashTable(streamInfo, 0, nullptr);
        auto scriptFile = readFile(dir_ + "/SFXSCRIPTFILES.DAT");
        size_t base;
        scriptHashes_ = hashTable(scriptFile, 0, &base);

        // Script directory: count in the low 28 bits, delay divisor in the top 4.
        uint32_t word = rd32(scriptFile.data() + base);
        uint32_t n = word & 0xfffffff;
        scriptDivisor_ = (word >> 28) ? (word >> 28) : 1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t* dir = scriptFile.data() + base + 4 + i * 8;
            const uint8_t* p = scriptFile.data() + base + rd32(dir);
            Script s;
            s.volume = dir[6];
            s.dirByte = dir[7];
            s.mode = p[0];
            s.mode1 = p[1];
            for (uint32_t k = 0, m = rd16(p + 2); k < m; ++k) {
                const uint8_t* e = p + 4 + k * 0x18;
                ScriptEntry x;
                x.sound = rd32(e);
                x.adsr = rd32(e + 4);
                x.pitch = rd32(e + 8);
                x.pitchRandSemi = e[0xc];
                x.pitchRandCent = e[0xd];
                x.delay = rd16(e + 0xe);
                x.flags = rd16(e + 0x10);
                x.volMin = e[0x12];
                x.volMax = e[0x13];
                x.panMin = int8_t(e[0x14]);
                x.panMax = int8_t(e[0x15]);
                x.sendA = e[0x16];
                x.sendB = e[0x17];
                s.entries.push_back(x);
            }
            scripts_.push_back(std::move(s));
        }

        // Load-time resolution (0x111cc8): sound with mask 0x3f; delay scaled by rate (1) / divisor.
        for (auto& s : scripts_) {
            for (auto& e : s.entries) {
                e.id = lookup(e.sound, 0x3f);
                e.delay = uint16_t(e.delay / scriptDivisor_);
            }
        }
    }
};

}   // namespace captain_toad::audio
