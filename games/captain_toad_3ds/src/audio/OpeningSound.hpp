// =============================================================================================
// OpeningSound.hpp - the port's sound system
// =============================================================================================
//
// Four layers, from the game down to the speakers:
//   1. game-side SOUND OBJECTS: a sound attached to a position in the world. Each game tick its
//      volume and pan are recomputed from the distance to the listener and the direction in
//      view space (a 3D "control block");
//   2. the middleware's SCRIPT ENGINE (SoundScript.hpp), which turns a sound request into one
//      or more waves with random variations;
//   3. VOICES: the waves actually playing, each on a DSP source with per-mix gains;
//   4. the DSP MIXER (DspMixer.hpp), plus the BGM streams that play on two dedicated sources.
//
// Recovered parts (code.bin) and observed parts (the emulator's DSP log, evidence/runs/sound-dsp-a)
// are marked; see docs/sound.md.
//   * 3D control block 0x3794e0 (recovered): volume 127 inside `near`, falling linearly to 0 at
//     `far` (distance to the listener = camera position 0x5445c4); pan = 64 + 64 * k * x / -z of
//     the emitter in view space (view matrix 0x544594, k = 1.0 at 0x4f8598), clamped to
//     [0, 127]. (near, far) come from SND3DLIST.DAT by attenuation preset (names: 0x2390a0).
//   * Sound object update 0x37c738 (recovered): instance volume = script volume
//     * ((object volume * control volume) >> 7) / 127 (x * 0x4081 + 0x4081 >> 21), pan = control.
//   * Voice gains (observed, exact on 126 of 157 paired starts; code location not found):
//     q = v * e * 124 / (127 * 128) (instance volume v, entry volume e); main mix
//     L = 1.2 * q / 127 * sqrt((128 - pan) / 128), R = 1.2 * q / 127 * sqrt(pan / 128); third mix
//     = main * sendA * 5 / (127 * 127). Rate = wave rate / 32728.498 * pitch / 4096 * CWSD pitch.
//   * Streams (observed): per-channel gain 1.2 * (v / 127) * (x / 127) with v the stream's volume
//     in the sound archive and x = round(0.96 v): Tutorial 88 -> 84, Clear01 83 -> 80, Demo_Op01
//     150 -> 144 (sound-dsp-a), Grass 84 -> 81 (dspB); the stream volume enters twice. The stream
//     track data are neutral (volume 127, pan 64); the code of this gain stage is not found (as
//     for the voices' 124 / 128); left / right channels on separate sources.
// =============================================================================================
#pragma once
#include "audio/DspMixer.hpp"
#include "audio/SoundScript.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace captain_toad::audio {

struct Vec3f {
    float x = 0;
    float y = 0;
    float z = 0;
};

// Row-major view matrix (camera space = m * p).
struct View34 {
    float m[3][4];
};

// Output of the 3D control block.
struct Control {
    uint8_t volume = 0x7f;
    uint8_t pan = 0x40;
    uint16_t pitch = 0x1000;
};

// 0x3794e0 with explicit near / far distances (the caller does the preset lookup).
inline Control control3D(const Vec3f& p, const Vec3f& listener, const View34& view, float nearD, float farD,
                         float panScale = 1.0f) {
    Control c;

    // Volume by distance.
    float dx = p.x - listener.x;
    float dy = p.y - listener.y;
    float dz = p.z - listener.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    if (farD * farD <= d2) {
        c.volume = 0;
        return c;
    }
    if (0.f < d2 && nearD * nearD < d2) {
        c.volume = uint8_t(uint32_t(127.f - ((std::sqrt(d2) - nearD) * 127.f) / (farD - nearD)));
    }

    // Pan by direction: x / -z in view space is the tangent of the angle off the view axis.
    float x = view.m[0][0] * p.x + view.m[0][1] * p.y + view.m[0][2] * p.z + view.m[0][3];
    float z = view.m[2][0] * p.x + view.m[2][1] * p.y + view.m[2][2] * p.z + view.m[2][3];
    if (z == 0.f) {
        z = 1.f;
    }
    float pan = 64.f + (x / -z) * panScale * 64.f;
    float v = 0.f;
    if (0.f <= pan) {
        v = pan;
        if (127 < int(pan)) {
            v = 127.f;
        }
    }
    c.pan = uint8_t(uint32_t(v));
    return c;
}

// Distance attenuation preset (SND3DLIST.DAT).
struct Attenuation {
    float nearD;
    float farD;
};

class SoundSystem : public SoundBackend {
public:
    explicit SoundSystem(const std::string& soundDataDir)
        : archive(soundDataDir), scripts(archive, *this, rng), mixer(24) {
        // ADSRINFO.DAT: a hash table of envelope names, then a u32 and one {attack, decay,
        // sustain, release} record per name.
        if (FILE* f = std::fopen((soundDataDir + "/ADSRINFO.DAT").c_str(), "rb")) {
            std::fclose(f);
            auto a = readFile(soundDataDir + "/ADSRINFO.DAT");
            const uint32_t size = rd32(a.data()), n = rd32(a.data() + 4);
            for (uint32_t i = 0; i < n && size + 8 + 4 * i <= a.size(); ++i) {
                const uint8_t* r = a.data() + size + 4 + 4 * i;
                adsrTable[rd32(a.data() + 8 + 4 * i)] = {r[0], r[1], r[2], r[3]};
            }
        }
        auto t = readFile(soundDataDir + "/SND3DLIST.DAT");
        uint32_t n = rd32(t.data());
        for (uint32_t i = 0; i < n; ++i) {
            presets.push_back({float(int32_t(rd32(t.data() + 4 + i * 8))), float(int32_t(rd32(t.data() + 8 + i * 8)))});
        }
    }

    SoundArchive archive;
    SoundRng rng;
    SoundScriptEngine scripts;
    DspMixer mixer;
    std::vector<Attenuation> presets;
    std::recursive_mutex lock;   // the audio thread renders while the game thread updates

    // -----------------------------------------------------------------------------------------
    // Voices (the middleware's wave playback)
    // -----------------------------------------------------------------------------------------

    struct Voice {
        int source = -1;
        uint32_t id = 0;
        int handle = -1;
        uint8_t volume = 0;
        uint8_t entryVolume = 127;
        uint8_t sendA = 0;
        uint8_t sendB = 0;
        uint8_t pan = 64;
        bool stopping = false;
        int channel = 0;
        int channels = 1;
        int entry = -1;   // script entry index
        // Envelopes (when SoundSystem::envelopes): the ADSR record (attack, decay, sustain,
        // release), the envelope level, the release, and the timed sweeps of pitch, entry volume
        // and pan (Q12 steps, frames left).
        std::array<uint8_t, 4> adsr{127, 127, 127, 127};
        float env = 1.f, attackStep = 0.f;
        bool releasing = false;
        float baseRate = 0.f;   // rate at pitch 0x1000
        int pitch = 0x1000, pitchTarget = 0, pitchStep = 0, pitchFrames = 0;
        int volQ = 0, volTarget = 0, volStep = 0, volFrames = 0;   // entry volume << 12
        int panQ = 0, panTarget = 0, panStep = 0, panFrames = 0;   // pan << 12
    };
    std::vector<Voice> voices;

    bool debugWaves = getenv("DIORAMA_SOUND_WAVES") != nullptr;
    // ADSR envelopes and timed sweeps: on for the front end. Off for the opening, whose render is
    // validated without them (turning them on there is a separate, re-validated change).
    bool envelopes = false;
    std::map<uint32_t, std::array<uint8_t, 4>> adsrTable;

    // Observed envelope laws (DSP log book-dsp-a; the middleware code was not found):
    //   release: the level falls by d(R) = 32.6 (127 - R)^-1.17 dB per 60 Hz frame (fits R = 100
    //   / 110 / 122 with the voice's volume byte floored: 0.69 / 1.21 / 4.82 dB), R 127 at once;
    //   attack: a linear rise of 0.318 per frame for A = 117 (10 below 127), the first frame two
    //   steps in; hypothesis for the other values: the step scales as 3.18 / (127 - A).
    static float releaseFactor(uint8_t r) {
        if (r >= 127) {
            return 0.f;
        }
        const float db = 32.6f * std::pow(float(127 - r), -1.17f);
        return std::pow(10.f, -db / 20.f);
    }
    static float attackStep(uint8_t a) { return a >= 127 ? 1.f : 3.18f / float(127 - a); }
    long debugClock = 0;   // DSP frames rendered (set by render)

    // Voice gains for one DSP source. A stereo wave plays each channel on its own source, the
    // left at pan - 64 and the right at pan + 64 (clamped; GOAL_ITEM_GOT1 in sound-dsp-a: left
    // source [A, 0], right source [0.088 A, A]).
    static std::array<std::array<float, 4>, 3> gains(uint8_t v, uint8_t e, int pan, uint8_t sendA, float env = 1.f) {
        pan = std::min(127, std::max(0, pan));
        int q = env >= 1.f ? int(uint32_t(v) * e * 124 / (127 * 128))
                           : int(float(uint32_t(v) * e * 124) * env / float(127 * 128));
        float a = 1.2f * float(q) / 127.f;
        float l = a * std::sqrt(float(128 - pan) / 128.f);
        float r = a * std::sqrt(float(pan) / 128.f);
        float s = float(sendA) * 5.f / (127.f * 127.f);
        return {{{l, r, 0, 0}, {0, 0, 0, 0}, {l * s, r * s, 0, 0}}};
    }

    static int channelPan(int pan, int channel, size_t channels) {
        if (channels < 2) {
            return pan;
        }
        return channel == 0 ? pan - 64 : pan + 64;
    }

    // First free DSP source (sources below 4 are kept for the BGM streams, which use 0 and 1).
    int freeSource() {
        for (size_t i = 4; i < mixer.sources.size(); ++i) {
            bool used = mixer.sources[i].enabled;
            for (auto& v : voices) {
                used |= v.source == int(i);
            }
            if (!used) {
                return int(i);
            }
        }
        return -1;
    }

    void playWave(const WavePlay& p) override {
        if (p.id >= archive.waveSoundCount()) {
            return;
        }
        const WaveSoundInfo& w = archive.waveSound(p.id);
        const Pcm& pcm = archive.wave(w.archive, w.wave);
        for (size_t c = 0; c < std::min<size_t>(pcm.ch.size(), 2); ++c) {
            int src = freeSource();
            if (src < 0) {
                return;
            }
            DspVoice& s = mixer.sources[size_t(src)];
            s.start(&pcm);
            s.channel = int(c);
            s.loop = false;   // one-shot waves of the opening
            s.rate = float(pcm.sampleRate) / float(kRateBase) * (float(p.pitch) / 4096.f) * w.pitch;
            // A voice starts at its gain without the DSP's per-frame ramp (observed: the emulator's
            // capture has the full onset; tools/validate_dsp_mix with NO_START_RAMP gains 4 dB).
            Voice v;
            if (envelopes) {
                auto a = adsrTable.find(p.adsr);
                if (a != adsrTable.end()) {
                    v.adsr = a->second;
                }
                v.attackStep = attackStep(v.adsr[0]);
                v.env = std::min(1.f, 2.f * v.attackStep);
                v.baseRate = s.rate / (float(p.pitch) / 4096.f);
                v.pitch = int(p.pitch);
                v.volQ = int(p.entryVolume) << 12;
                v.panQ = int(p.pan) << 12;
            }
            s.snapGains(gains(p.volume, p.entryVolume, channelPan(p.pan, int(c), pcm.ch.size()), p.sendA, v.env));

            v.source = src;
            v.id = p.id;
            v.handle = p.handle;
            v.volume = p.volume;
            v.entryVolume = p.entryVolume;
            v.sendA = p.sendA;
            v.sendB = p.sendB;
            v.pan = p.pan;
            v.channel = int(c);
            v.channels = int(pcm.ch.size());
            v.entry = p.entry;
            voices.push_back(v);
            if (debugWaves) {
                std::fprintf(stderr,
                             "wave: %ld id %u handle %d ch %zu archive %u/%u v %d e %d pan %d pitch %x rate %.4f adsr "
                             "%d/%d/%d/%d\n",
                             debugClock, p.id, p.handle, c, w.archive, w.wave, p.volume, p.entryVolume, p.pan, p.pitch,
                             double(s.rate), v.adsr[0], v.adsr[1], v.adsr[2], v.adsr[3]);
            }
        }
    }

    void stopWave(uint32_t id, int handle) override {
        if (debugWaves) {
            int n = 0;
            for (auto& v : voices) {
                n += v.id == id && v.handle == handle && !v.stopping;
            }
            std::fprintf(stderr, "stop: %ld id %u handle %d voices %d\n", debugClock, id, handle, n);
        }
        for (auto& v : voices) {
            if (v.id == id && v.handle == handle && !v.stopping) {
                if (envelopes && v.adsr[3] < 127) {
                    v.releasing = true;   // vt+0xa4 with release: the envelope's release runs
                    continue;
                }
                v.stopping = true;
                mixer.sources[size_t(v.source)].setGains({});
            }
        }
    }

    // A script instance is alive (started, possibly before its first voice).
    bool handleAlive(int handle) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        return scripts.find(handle) != nullptr;
    }

    // Stop a script instance, its sub-scripts and their voices (a level sound whose action ended).
    void stopHandle(int handle) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        std::vector<int> handles{handle};
        for (size_t k = 0; k < handles.size(); k++) {
            for (const auto& i : scripts.instances) {
                if (i.parent == handles[k]) {
                    handles.push_back(i.handle);
                }
            }
        }
        auto stopped = [&](int h) {
            return std::find(handles.begin(), handles.end(), h) != handles.end();
        };
        for (auto& v : voices) {
            if (stopped(v.handle) && !v.stopping) {
                if (envelopes && v.adsr[3] < 127) {
                    v.releasing = true;
                    continue;
                }
                v.stopping = true;
                mixer.sources[size_t(v.source)].setGains({});
            }
        }
        scripts.instances.remove_if([&](const ScriptInstance& i) { return stopped(i.handle); });
        for (auto& o : objects) {
            if (stopped(o.handle)) {
                o.done = true;
            }
        }
    }

    // 0x37cea8: a target and a per-frame step (an integer division of the difference by the
    // frames) for each changed parameter.
    void sweepWave(uint32_t id, int handle, int volume, int pan, int pitch, int frames) override {
        if (!envelopes) {
            return;
        }
        if (debugWaves) {
            std::fprintf(stderr, "sweep: %ld id %u handle %d vol %d pan %d pitch %x frames %d\n", debugClock, id,
                         handle, volume, pan, pitch, frames);
        }
        for (auto& v : voices) {
            if (v.id != id || v.handle != handle || v.stopping) {
                continue;
            }
            if (volume != 0xff) {
                v.volTarget = volume << 12;
                v.volFrames = std::max(frames, 0);
                v.volStep = frames > 0 ? (v.volTarget - v.volQ) / frames : 0;
                if (frames < 1) {
                    v.volQ = v.volTarget;
                }
            }
            if (pan != 0xff) {
                v.panTarget = pan << 12;
                v.panFrames = std::max(frames, 0);
                v.panStep = frames > 0 ? (v.panTarget - v.panQ) / frames : 0;
                if (frames < 1) {
                    v.panQ = v.panTarget;
                }
            }
            if (pitch != -1 && pitch != 0xfffff) {
                v.pitchTarget = pitch;
                v.pitchFrames = std::max(frames, 0);
                v.pitchStep = frames > 0 ? (pitch - v.pitch) / frames : 0;
                if (frames < 1) {
                    v.pitch = pitch;
                }
            }
        }
    }

    // One 60 Hz frame of the envelopes and sweeps.
    void envelopeFrame() {
        for (auto& v : voices) {
            if (v.stopping) {
                continue;
            }
            DspVoice& s = mixer.sources[size_t(v.source)];
            bool gainsDirty = false;
            if (v.env < 1.f && !v.releasing) {
                v.env = std::min(1.f, v.env + v.attackStep);
                gainsDirty = true;
            }
            if (v.releasing) {
                v.env *= releaseFactor(v.adsr[3]);
                gainsDirty = true;
                if (int(float(uint32_t(v.volume) * (v.volQ >> 12) * 124) * v.env / float(127 * 128)) <= 0) {
                    v.stopping = true;
                    s.setGains({});
                    continue;
                }
            }
            auto stepTo = [](int& cur, int target, int step, int& frames) {
                if (frames <= 0) {
                    return false;
                }
                cur += step;
                if (--frames == 0) {
                    cur = target;
                }
                return true;
            };
            if (stepTo(v.pitch, v.pitchTarget, v.pitchStep, v.pitchFrames)) {
                s.rate = v.baseRate * float(v.pitch) / 4096.f;
            }
            if (stepTo(v.volQ, v.volTarget, v.volStep, v.volFrames)) {
                v.entryVolume = uint8_t(std::clamp(v.volQ >> 12, 0, 255));
                gainsDirty = true;
            }
            if (stepTo(v.panQ, v.panTarget, v.panStep, v.panFrames)) {
                v.pan = uint8_t(std::clamp(v.panQ >> 12, 0, 127));
                gainsDirty = true;
            }
            if (gainsDirty) {
                applyVoiceGains(v);
            }
        }
    }

    bool handlePlaying(int handle) const override {
        for (auto& v : voices) {
            if (v.handle == handle) {
                return true;
            }
        }
        return false;
    }

    void setHandleVolume(int handle, uint8_t volume) override {
        for (auto& v : voices) {
            if (v.handle == handle && !v.stopping && v.volume != volume) {
                v.volume = volume;
                applyVoiceGains(v);
            }
        }
    }

    void setWaveVolume(uint32_t id, int handle, uint8_t volume) override {
        for (auto& v : voices) {
            if (v.id == id && v.handle == handle && !v.stopping && v.volume != volume) {
                v.volume = volume;
                applyVoiceGains(v);
            }
        }
    }

    void setEntryVolume(int handle, int entry, uint8_t volume) override {
        for (auto& v : voices) {
            if (v.handle == handle && v.entry == entry && !v.stopping &&
                (v.volume != volume || v.entryVolume != 127 || (v.volQ >> 12) != 127)) {
                v.volume = volume;
                v.entryVolume = 127;
                v.volQ = 127 << 12;
                applyVoiceGains(v);
            }
        }
    }

    void setHandlePan(int handle, uint8_t pan) {
        for (auto& v : voices) {
            if (v.handle == handle && !v.stopping && v.pan != pan) {
                v.pan = pan;
                applyVoiceGains(v);
            }
        }
    }

    // -----------------------------------------------------------------------------------------
    // Game-side sound objects (0x37c738)
    // -----------------------------------------------------------------------------------------

    struct Object {
        uint32_t script = 0;
        uint8_t scriptVolume = 127;
        uint8_t objectVolume = 127;
        int forced = -1;
        int handle = -1;
        std::function<Vec3f()> where;   // empty: 2D (control 0x7f, 0x40, 0x1000)
        int preset = 10;
        float volumeScale = 1.f;   // game-side scale (VolumeAfterGoal, atmosphere mute)
        bool done = false;
    };
    std::vector<Object> objects;
    Vec3f listener;
    View34 view{};

    // a * b / 127 in the middleware's fixed-point form.
    static uint8_t mul127(uint32_t a, uint32_t b) {
        return uint8_t((uint32_t(int16_t(a) * int16_t(b)) * 0x4081u + 0x4081u) >> 21);
    }

    // The middleware's command latency for script starts, in DSP frames (0: at once, as the
    // opening's research renders, which model it in the render). The front end sets 16 (observed:
    // book-sound-a's start voice is the port's render delayed by 15.8 DSP frames, residual 4e-8).
    int commandLatency = 0;
    // Object volume factor for the next play() (a PlayInfo's Volume curve).
    float nextObjectScale = 1.f;
    struct PendingPlay {
        uint32_t script;
        std::function<Vec3f()> where;
        int preset, forced, frames;
        float scale;
        std::function<void(int)> started;
    };
    std::vector<PendingPlay> pendingPlays;

    // Start a script by name for an emitter. The object's volume byte is the script volume
    // times the control volume at creation (observed in sound-object-a: e.g. bird 45 * 97 -> 34).
    // `started` receives the object's script handle when it starts (at once, or after the command
    // latency).
    bool play(const std::string& name, std::function<Vec3f()> where, int preset, int forced = -1,
              std::function<void(int)> started = {}) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        uint32_t id = archive.lookup(name.c_str(), 4);
        if (id == kIdNone) {
            return false;
        }
        const float scale = nextObjectScale;
        nextObjectScale = 1.f;
        if (commandLatency > 0) {
            pendingPlays.push_back(
                {id & 0xffff, std::move(where), preset, forced, commandLatency, scale, std::move(started)});
            return true;
        }
        startScript(id & 0xffff, std::move(where), preset, forced, scale);
        if (started) {
            started(objects.back().handle);
        }
        return true;
    }

    void startScript(uint32_t script, std::function<Vec3f()> where, int preset, int forced, float scale) {
        Object o;
        o.script = script;
        o.scriptVolume = archive.script(o.script)->volume;
        o.forced = forced;
        o.where = std::move(where);
        o.preset = preset;
        o.objectVolume = mul127(o.scriptVolume, controlOf(o).volume);
        if (scale != 1.f) {
            o.objectVolume = uint8_t(std::min(255.f, float(o.objectVolume) * scale));
        }
        o.handle = scripts.start(o.script, forced, false);
        objects.push_back(o);
        update(objects.back());
    }

    Control controlOf(const Object& o) const {
        if (!o.where) {
            return Control{};
        }
        const Attenuation& a = presets.at(size_t(o.preset));
        return control3D(o.where(), listener, view, a.nearD, a.farD);
    }

    // 0x37c738: refresh an object's script instance from its current 3D control.
    void update(Object& o) {
        Control c = controlOf(o);
        uint32_t x = (uint32_t(o.objectVolume) * c.volume & 0x7fff) >> 7;
        uint32_t volume = mul127(o.scriptVolume, x);
        volume = uint32_t(float(volume) * o.volumeScale);
        scripts.setParams(o.handle, int(volume), c.pan, -1, 0xff, 0xff, 0);
        setHandlePan(o.handle, c.pan);
    }

    // -----------------------------------------------------------------------------------------
    // BGM streams
    // -----------------------------------------------------------------------------------------

    struct Stream {
        std::unique_ptr<Pcm> pcm;
        int source = 0;   // one stereo source (the original uses a mono source per channel)
        float base = 0;   // 1.2 * stream volume / 127
        int level = 0;    // x / 127 (x = round(0.96 * stream volume), observed)
        int fadeFrom = 0;
        int fadeTicks = 0;
        int fadeAge = 0;
        float scale = 1.f;   // game-side volume (a BGM line's situation volume and fade), 0..1
        bool active = false;
        bool paused = false;   // a lower BGM line under a higher one (0x32a3d8): position kept
        int fadeInTicks = 0;   // resume fade-in (0x32a63c -> 0x378578)
        int fadeInAge = 0;
    };
    std::map<std::string, Stream> streams;
    int nextStreamPair = 0;

    // Streams start after their first blocks are loaded: a request starts playing kStreamDelay
    // DSP frames later (observed: Clear01 and Demo_Op01 in sound-dsp-a, 11-12 frames after the
    // command latency of the SFX; Demo_Op01 one more).
    static constexpr int kStreamDelay = 12;

    struct PendingStream {
        std::string name;
        int level;
        uint32_t position;
        int frames;
    };
    std::vector<PendingStream> pendingStreams;

    // level -1: x from the stream's volume (see the header).
    void requestStream(const std::string& name, int level = -1, int delay = kStreamDelay) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        pendingStreams.push_back({name, level, 0, delay});
    }

    void playStream(const std::string& name, int level = -1, uint32_t position = 0) {
        uint32_t id = archive.lookup(name.c_str(), 0x10);
        if (id == kIdNone) {
            return;
        }
        Stream& s = streams[name];
        if (!s.pcm) {
            // Runs on the audio thread: a stream the package lacks is skipped (an exception here
            // would end the device's thread with its lock held).
            try {
                s.pcm = std::make_unique<Pcm>(decodeCstm(readFile(archive.streamPath(id & 0xffff))));
            } catch (const std::exception& e) {
                fprintf(stderr, "sound: stream %s not playable (%s)\n", name.c_str(), e.what());
                streams.erase(name);
                return;
            }
        }
        s.source = nextStreamPair;
        nextStreamPair ^= 1;
        const int volume = archive.streamEntry(id & 0xffff).volume;
        s.base = 1.2f * float(volume) / 127.f;
        s.level = level >= 0 ? level : int(std::lround(0.96 * volume));
        s.scale = 1.f;
        s.fadeTicks = 0;
        s.active = true;
        // Stream sample rate -> output: the DSP rate multiplier resamples the decoded PCM.
        DspVoice& d = mixer.sources[size_t(s.source)];
        d.start(s.pcm.get(), position);
        d.rate = float(s.pcm->sampleRate) / float(kRateBase);
        d.snapGains({});
        applyStream(s);
    }

    // True while a stream is requested or still playing (a non-looping stream ends when its
    // source runs out of samples).
    bool streamPlaying(const std::string& name) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        for (const auto& p : pendingStreams) {
            if (p.name == name) {
                return true;
            }
        }
        auto it = streams.find(name);
        return it != streams.end() && it->second.active && mixer.sources[size_t(it->second.source)].enabled;
    }

    // The game-side volume of a playing stream (0 stops it).
    void setStreamScale(const std::string& name, float scale) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        auto it = streams.find(name);
        if (it == streams.end() || !it->second.active) {
            return;
        }
        it->second.scale = scale;
        if (scale <= 0.f) {
            it->second.active = false;
            mixer.sources[size_t(it->second.source)].enabled = false;
        } else {
            applyStream(it->second);
        }
    }

    // Pause a playing stream where it is (a lower BGM line while a higher one plays, 0x32a3d8).
    void pauseStream(const std::string& name) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        auto it = streams.find(name);
        if (it == streams.end() || !it->second.active || it->second.paused) {
            return;
        }
        it->second.paused = true;
        mixer.sources[size_t(it->second.source)].enabled = false;
    }
    // Resume a paused stream from its position, fading in over `ticks` game ticks (0x32a63c:
    // the unpause with fade 0x14 frames).
    void resumeStream(const std::string& name, int ticks) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        auto it = streams.find(name);
        if (it == streams.end() || !it->second.paused) {
            return;
        }
        Stream& st = it->second;
        st.paused = false;
        mixer.sources[size_t(st.source)].enabled = true;
        st.fadeInTicks = ticks;
        st.fadeInAge = 0;
        st.scale = ticks > 0 ? 0.f : 1.f;
        applyStream(st);
    }

    // Linear fade to silence over `ticks` game ticks.
    void fadeOutStream(const std::string& name, int ticks) {
        auto it = streams.find(name);
        if (it == streams.end() || !it->second.active) {
            return;
        }
        it->second.fadeFrom = it->second.level;
        it->second.fadeTicks = ticks;
        it->second.fadeAge = 0;
    }

    // -----------------------------------------------------------------------------------------
    // Clocks
    // -----------------------------------------------------------------------------------------

    // Per game tick: sound objects (0x37c738) and BGM fades.
    void tickObjects() {
        std::lock_guard<std::recursive_mutex> guard(lock);
        for (auto& o : objects) {
            if (!o.done) {
                update(o);
            }
        }
        for (auto& [n, s] : streams) {
            if (s.active && !s.paused && s.fadeInTicks) {
                ++s.fadeInAge;
                s.scale = std::min(1.f, float(s.fadeInAge) / float(s.fadeInTicks));
                if (s.fadeInAge >= s.fadeInTicks) {
                    s.fadeInTicks = 0;
                }
                applyStream(s);
            }
            if (!s.active || !s.fadeTicks) {
                continue;
            }
            ++s.fadeAge;
            s.level = s.fadeFrom - s.fadeFrom * s.fadeAge / s.fadeTicks;
            if (s.level <= 0) {
                s.active = false;
                mixer.sources[size_t(s.source)].enabled = false;
            } else {
                applyStream(s);
            }
        }
    }

    // Per 60 Hz frame: the middleware's script update (0x11881c, delta 1), then the voices and
    // objects that have finished are dropped.
    void frameUpdate() {
        std::lock_guard<std::recursive_mutex> guard(lock);
        scripts.update(1);
        if (envelopes) {
            envelopeFrame();
        }
        for (size_t i = 0; i < voices.size();) {
            if (!mixer.sources[size_t(voices[i].source)].enabled) {
                voices.erase(voices.begin() + long(i));
            } else {
                ++i;
            }
        }
        for (auto& o : objects) {
            if (!o.done && !scripts.find(o.handle)) {
                o.done = true;
            }
        }
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const Object& o) { return o.done; }),
                      objects.end());
    }

    // Render `frames` DSP frames (160 stereo samples each) into `out`.
    void render(int16_t* out, int frames) {
        std::lock_guard<std::recursive_mutex> guard(lock);
        for (int f = 0; f < frames; ++f) {
            for (size_t i = 0; i < pendingPlays.size();) {
                if (--pendingPlays[i].frames <= 0) {
                    PendingPlay p = std::move(pendingPlays[i]);
                    pendingPlays.erase(pendingPlays.begin() + long(i));
                    startScript(p.script, std::move(p.where), p.preset, p.forced, p.scale);
                    if (p.started) {
                        p.started(objects.back().handle);
                    }
                } else {
                    ++i;
                }
            }
            for (size_t i = 0; i < pendingStreams.size();) {
                if (--pendingStreams[i].frames <= 0) {
                    playStream(pendingStreams[i].name, pendingStreams[i].level, pendingStreams[i].position);
                    pendingStreams.erase(pendingStreams.begin() + long(i));
                } else {
                    ++i;
                }
            }
            mixer.frame(out + f * kFrameSamples * 2);
            ++debugClock;
        }
    }

private:
    void applyVoiceGains(Voice& v) {
        if (debugWaves && envelopes) {
            auto g = gains(v.volume, v.entryVolume, channelPan(v.pan, v.channel, size_t(v.channels)), v.sendA, v.env);
            std::fprintf(stderr, "gain: %ld src %d id %u %.4f env %.3f rate %.4f\n", debugClock, v.source, v.id,
                         double(g[0][0]), double(v.env), double(mixer.sources[size_t(v.source)].rate));
        }
        mixer.sources[size_t(v.source)].setGains(
            gains(v.volume, v.entryVolume, channelPan(v.pan, v.channel, size_t(v.channels)), v.sendA, v.env));
    }

    void applyStream(Stream& s) {
        float g = s.base * float(s.level) / 127.f * s.scale;
        std::array<std::array<float, 4>, 3> m{};
        m[0][0] = m[0][1] = g;
        mixer.sources[size_t(s.source)].setGains(m);
    }
};

}   // namespace captain_toad::audio
