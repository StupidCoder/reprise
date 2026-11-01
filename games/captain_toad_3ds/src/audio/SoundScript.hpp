// =============================================================================================
// SoundScript.hpp - the sound-effect script engine
// =============================================================================================
//
// Most sound effects in the game are not single samples but small scripts (SFXSCRIPTFILES.DAT in
// the sound archive). A script is a list of entries; each entry names a wave (sample), or another
// script, together with a start delay and random ranges for volume, pan and pitch. That is how
// one "footstep" sound can vary from step to step, or how a bird chirp is a little sequence of
// tweets.
//
// Script modes:
//   * mode 0 - a timeline: every entry plays once the script time reaches its delay;
//   * mode 1 - a choice: one entry is picked at random (or forced) and played once.
// An entry flag 0x40 (or the loop bit of the start call) restarts the script after its last
// entry. Otherwise the instance ends when none of its sounds play and no child script remains.
//
// Translated functions of the sound middleware (code.bin):
//   * start 0x37bb24 -> 0x124a48: instance initialisation, random entry choice for mode-1 scripts;
//   * update 0x11881c: once per game frame (delta = system rate 1);
//   * entry playback 0x11f624: sub-scripts (id & 0x200000) inherit volume / pitch / pan / sends;
//     waves play with the instance volume, a random volume byte in [min, max], sends scaled by
//     the instance sends, pan (instance or random [min, max]) and pitch = entry * instance >> 12
//     times 2^(cents / 1200) for a random upward cent offset; flag 0x80 stops the sound;
//   * random numbers: LCG state 0x4f8128 (x * 0x41c64e6d + 12345), 0x301318 / 0x1287dc.
// Voice playback itself is delegated to a backend (the middleware's play / stop / is-playing).
// =============================================================================================
#pragma once
#include "audio/SoundArchive.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <list>

namespace captain_toad::audio {

// The middleware's random generator. Every random choice of every script draws from this one
// generator, so the order of the draws across all playing scripts matters for exact replay.
struct SoundRng {
    uint32_t state = 0;
    long* clock = nullptr;   // research: frame stamp for DIORAMA_SOUND_RNG logging
    bool log = getenv("DIORAMA_SOUND_RNG") != nullptr;

    // 0x1287dc: a 15-bit random number.
    uint32_t next15() {
        if (log) {
            std::fprintf(stderr, "rng: %ld choice\n", clock ? *clock : -1);
        }
        state = state * 0x41c64e6du + 0x3039u;
        return (state >> 16) & 0x7fff;
    }

    // 0x301318: a random integer in [lo, hi], inclusive. No draw happens when lo == hi.
    int range(int lo, int hi) {
        if (lo == hi) {
            return lo;
        }
        if (log) {
            std::fprintf(stderr, "rng: %ld range %d %d\n", clock ? *clock : -1, lo, hi);
        }
        state = state * 0x41c64e6du + 0x3039u;
        return lo + int(((state >> 16) & 0xffff7fffu) * uint32_t(hi - lo + 1) >> 15);
    }
};

// Arguments of the middleware's play call (vt+0x68) for a wave id, as 0x11f624 fills them.
struct WavePlay {
    uint32_t id;
    int handle;
    uint8_t volume;        // instance volume
    uint8_t entryVolume;   // packed byte 0: the entry's random volume
    uint8_t sendA;         // packed byte 1: effect send A
    uint8_t sendB;         // packed byte 2: effect send B
    uint8_t pan;
    uint32_t pitch;   // Q12 (0x1000 = original pitch)
    uint32_t adsr;    // ADSR envelope id or 0xffffffff
    int entry = -1;   // the script entry that plays it (its index)
};

// The voice side of the middleware, implemented by SoundSystem (OpeningSound.hpp).
class SoundBackend {
public:
    virtual ~SoundBackend() = default;
    virtual void playWave(const WavePlay& p) = 0;
    virtual void stopWave(uint32_t id, int handle) = 0;   // vt+0xa4
    // vt+0x80 for one entry's wave: its volume (the entry volume folded in).
    virtual void setEntryVolume(int, int, uint8_t) {}
    // vt+0x7c (0x37a804 -> 0x37cea8): the playing waves (id, handle) move their entry volume, pan
    // and pitch to new values over `frames` 60 Hz frames (0xff / -1: unchanged).
    virtual void sweepWave(uint32_t, int, int, int, int, int) {}
    virtual bool handlePlaying(int handle) const = 0;   // vt+0xa8
    virtual void setHandleVolume(int handle, uint8_t volume) = 0;
    virtual void setWaveVolume(uint32_t id, int handle, uint8_t volume) = 0;   // vt+0x80 wave branch
};

// One running script.
struct ScriptInstance {
    uint32_t script = 0;   // script index
    const Script* data = nullptr;
    int time = 0;   // frames since start (mode 0)
    int prevTime = -1;
    uint16_t index = 0;   // next entry to play
    uint8_t volume = 127;
    uint8_t flags = 0;         // 2: entries held (not played), 4: loop
    uint16_t pitch = 0x1000;   // Q12
    uint8_t pan = 0x40;        // 0x40 = centre
    uint8_t sendA = 0;
    uint8_t sendB = 0;
    uint8_t x1d = 0;   // instance byte +0x1d (meaning not recovered)
    int handle = -1;
    int parent = -1;   // handle of the script that started this one as a sub-script
};

class SoundScriptEngine {
public:
    SoundScriptEngine(const SoundArchive& a, SoundBackend& b, SoundRng& rng) : archive_(a), backend_(b), rng_(rng) {}

    std::list<ScriptInstance> instances;
    int nextHandle = 1;

    // 0x37bb24 + 0x124a48: start a script and return its handle. forced < 0 picks a random
    // entry for mode-1 scripts (unless the script disables random choice).
    int start(uint32_t scriptIndex, int forced, bool loop, uint8_t x1d = 0) {
        const Script* s = archive_.script(scriptIndex);
        if (!s) {
            return -1;
        }
        ScriptInstance in;
        in.script = scriptIndex;
        in.data = s;
        uint16_t entryCount = uint16_t(s->entries.size());
        if (s->mode == 1 && entryCount) {
            if (forced < 0) {
                if (s->mode1 == 0) {
                    in.index = uint16_t(rng_.next15() % entryCount);
                }
            } else {
                in.index = uint16_t(std::min<int>(forced, entryCount - 1));
            }
        }
        bool anyLoop = false;
        for (auto& e : s->entries) {
            anyLoop |= (e.flags & 0x40) != 0;
        }
        in.flags = (loop || anyLoop) ? 4 : 0;
        in.volume = s->volume;
        in.x1d = x1d;
        in.handle = nextHandle++;
        instances.push_back(in);
        return in.handle;
    }

    ScriptInstance* find(int handle) {
        for (auto& i : instances) {
            if (i.handle == handle) {
                return &i;
            }
        }
        return nullptr;
    }

    // 0x270f00: change instance parameters (0xff / 0xffffffff = unchanged).
    void setParams(int handle, int volume, int pan, int pitch, int sendA, int sendB, uint8_t x1d) {
        ScriptInstance* in = find(handle);
        if (!in) {
            return;
        }
        if (volume != 0xff) {
            in->volume = uint8_t(volume);
            backend_.setHandleVolume(handle, uint8_t(volume));
            propagateVolume(*in, uint8_t(volume));
        }
        if (pan != 0xff) {
            in->pan = uint8_t(pan);
        }
        if (pitch != -1) {
            in->pitch = uint16_t(pitch);
        }
        if (sendA != 0xff) {
            in->sendA = uint8_t(sendA);
        }
        if (sendB != 0xff) {
            in->sendB = uint8_t(sendB);
        }
        in->x1d = x1d;
    }

    // 0x37c0c0 -> vt+0x80 (0x37a8ec): an instance volume reaches its entries. For a sub-script
    // entry, every child instance of that script started by this instance re-draws, per child
    // entry, rand(volMin, volMax) and sets that entry's waves to (rand * volume & 0x7fff) >> 7
    // (traced: two LCG draws per bird per tick while a ChirpOnce plays, sound-lcg-a 0x37ba5c).
    // Per-entry propagation (on for the front end, BookSound.hpp): each wave entry's own voice gets
    // (rand * volume & 0x7fff) >> 7 with the entry volume folded in (the start's v x e gives the
    // same gain); stop and timed entries carry no wave. Observed in book-dsp-a: the new-page
    // sub-script's waves keep their start gain (q 32) while the instance volume is re-sent every
    // tick. The opening keeps the wave-id form below (validated with it).
    bool perEntryVolumes = false;

    void propagateVolume(const ScriptInstance& in, uint8_t volume) {
        if (perEntryVolumes) {
            for (const auto& e : in.data->entries) {
                if (e.id == kIdNone || !(e.id & kIdScript)) {
                    continue;
                }
                const uint32_t sub = e.id & 0xffff;
                for (auto& c : instances) {
                    if (c.parent != in.handle || c.script != sub) {
                        continue;
                    }
                    for (size_t k = 0; k < c.data->entries.size(); ++k) {
                        const auto& ce = c.data->entries[k];
                        int r = rng_.range(ce.volMin, ce.volMax);
                        if (ce.id != kIdNone && !(ce.id & 0x370000) && !(ce.flags & 0x180)) {
                            backend_.setEntryVolume(c.handle, int(k), uint8_t((uint32_t(r) * volume & 0x7fff) >> 7));
                        }
                    }
                }
            }
            return;
        }
        for (const auto& e : in.data->entries) {
            if (e.id == kIdNone || !(e.id & kIdScript)) {
                continue;
            }
            const uint32_t sub = e.id & 0xffff;
            for (auto& c : instances) {
                if (c.parent != in.handle || c.script != sub) {
                    continue;
                }
                for (const auto& ce : c.data->entries) {
                    int r = rng_.range(ce.volMin, ce.volMax);
                    if (ce.id != kIdNone && !(ce.id & 0x370000)) {
                        backend_.setWaveVolume(ce.id, c.handle, uint8_t((uint32_t(r) * volume & 0x7fff) >> 7));
                    }
                }
            }
        }
    }

    // 0x11881c: advance every instance by `delta` frames (1 per game frame).
    void update(int delta = 1) {
        for (auto it = instances.begin(); it != instances.end();) {
            ScriptInstance& in = *it;
            const auto& entries = in.data->entries;
            uint16_t entryCount = uint16_t(entries.size());

            // Entries left to play.
            if (in.index < entryCount) {
                if (!(in.flags & 2)) {
                    if (in.data->mode == 1) {
                        // Choice: play the chosen entry, done.
                        playEntry(in, entries[in.index]);
                        in.index = entryCount;
                    } else {
                        // Timeline: when the time crosses the next delay, play every entry
                        // whose delay has been reached.
                        int delay = entries[in.index].delay;
                        if (in.prevTime < delay && delay <= in.time) {
                            uint16_t k = in.index;
                            for (; k < entryCount && in.time >= entries[k].delay; ++k) {
                                playEntry(in, entries[k]);
                            }
                            in.index = k;
                        }
                        int t = in.time;
                        in.time = t + delta;
                        in.prevTime = t;
                    }
                }
                ++it;
                continue;
            }

            // All entries played: loop, keep waiting for sounds / children, or end.
            if (in.flags & 4) {
                in.index = 0;
                in.time = 0;
                in.prevTime = -1;
                ++it;
                continue;
            }
            if (backend_.handlePlaying(in.handle)) {
                ++it;
                continue;
            }
            bool child = false;
            for (auto& o : instances) {
                child |= o.parent == in.handle;
            }
            if (child) {
                ++it;
                continue;
            }
            it = instances.erase(it);
        }
    }

private:
    const SoundArchive& archive_;
    SoundBackend& backend_;
    SoundRng& rng_;

    // Product of two 0..127 send levels, rescaled to 0..127 (fixed-point divide by 127).
    static uint8_t scaleSend(uint8_t a, uint8_t b) {
        return uint8_t((uint32_t(int16_t(a) * int16_t(b)) * 0x4081u + 0x4081u) >> 21);
    }

    // Raise `base` by a random amount up to `semi` semitones and `cent` cents.
    uint32_t randomPitch(uint32_t base, uint8_t semi, uint8_t cent) {
        if (!semi && !cent) {
            return base;
        }
        int cents = cent ? rng_.range(0, cent + semi * 100) : rng_.range(0, semi) * 100;
        float factor = std::pow(2.0f, float(uint32_t(cents)) * 0.0008333333535f);   // 2^(cents / 1200)
        return uint32_t(float(base) * factor);
    }

    // 0x11f624: play one entry. Returns true when a sub-script was started or a stop was issued.
    // The random draws (volume, pitch, pan) happen in this order for exact replay.
    bool playEntry(ScriptInstance& in, const ScriptEntry& e) {
        uint32_t id = e.id;
        if (id == kIdNone) {
            return false;
        }
        if (e.flags & 0x8) {
            return false;   // vt+0x104 (pan byte): not used by the opening
        }
        if (e.flags & 0x80) {
            // Stop entry.
            if ((id & 0x70000) == 0) {
                backend_.stopWave(id, in.handle);
            }
            return true;
        }
        if (id & 0x70000) {
            return false;   // streams / inter-music from scripts: not used here
        }
        if (id & kIdScript) {
            // Sub-script: inherits this instance's parameters, scaled by the entry's.
            int h = start(id & 0xffff, -1, false, 0);
            if (h < 0) {
                return true;
            }
            ScriptInstance* sub = find(h);
            sub->parent = in.handle;
            sub->volume = uint8_t(uint32_t(rng_.range(e.volMin, e.volMax)) * in.volume >> 7);
            uint32_t pitch = randomPitch(e.pitch * in.pitch >> 12, e.pitchRandSemi, e.pitchRandCent);
            sub->pitch = uint16_t(pitch);
            sub->pan = e.panMin == -1 ? in.pan : uint8_t(rng_.range(uint8_t(e.panMin), uint8_t(e.panMax)));
            sub->sendA = scaleSend(in.sendA, e.sendA);
            sub->sendB = scaleSend(in.sendB, e.sendB);
            return true;
        }
        if (id & 0x100000) {
            return false;   // DGN
        }
        if (e.flags & 0x100) {
            // Timed entry (0x11f624 -> vt+0x7c): the wave already playing for this instance
            // moves to the entry's pitch, pan and volume over the entry's time (its ADSR field).
            // Draw order as in the code: pitch, pan, volume.
            const uint32_t pitch = randomPitch(e.pitch * in.pitch >> 12, e.pitchRandSemi, e.pitchRandCent);
            const int pan = rng_.range(uint8_t(e.panMin), uint8_t(e.panMax));
            const int volume = rng_.range(e.volMin, e.volMax);
            // The time is scaled like the delays (the script directory's divisor; book-dsp-a: 19
            // gives a 6-frame pitch glide).
            backend_.sweepWave(id, in.handle, volume, pan, int(pitch), int(e.adsr / archive_.scriptDivisor()));
            return false;
        }

        // A wave.
        WavePlay p;
        p.id = id;
        p.handle = in.handle;
        p.pan = e.panMin == -1 ? in.pan : uint8_t(rng_.range(uint8_t(e.panMin), uint8_t(e.panMax)));
        p.entryVolume = uint8_t(rng_.range(e.volMin, e.volMax));
        p.sendA = scaleSend(in.sendA, e.sendA);
        p.sendB = scaleSend(in.sendB, e.sendB);
        p.pitch = randomPitch(e.pitch * in.pitch >> 12, e.pitchRandSemi, e.pitchRandCent);
        p.volume = in.volume;
        p.adsr = e.adsr;
        p.entry = int(&e - in.data->entries.data());
        backend_.playWave(p);
        return false;
    }
};

}   // namespace captain_toad::audio
