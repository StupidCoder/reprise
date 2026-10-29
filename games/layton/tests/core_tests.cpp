#include "graphics/graphics.hpp"
#include "sound/dse_driver.hpp"
#include "sound/sad_audio.hpp"
#include "sound/sad_stream.hpp"
#include "story/a2_narration.hpp"
#include "system/interaction.hpp"
#include "system/resources.hpp"
#include "system/script.hpp"
#include "system/a2_timing.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace picarat;

static void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class F>
static void rejects(F call) {
    bool threw = false;
    try {
        call();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "invalid input was accepted");
}

static void word(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(value >> (i * 8));
    }
}

static Bytes testStream(unsigned channels, bool loop) {
    Bytes file(256 + 49 * 16 * channels, 0);
    auto put = [&](unsigned offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            file[offset + i] = value >> (i * 8);
        }
    };
    put(0, 0x6c646173);
    put(8, file.size());
    put(12, 0x415);
    put(0x40, file.size());
    put(0x48, 256);
    put(0x54, 256);
    put(0x58, file.size());
    put(0x68, 3000);
    file[0x31] = loop;
    file[0x32] = channels;
    file[0x33] = 0xb2;
    file[0x3a] = 16;
    file[0x65] = 1;
    for (unsigned i = 256; i < file.size(); ++i) {
        file[i] = ((i - 256) % 16 == 15) ? 0x84 : 0x99;
    }
    return file;
}

struct Clock final : FrameHost {
    std::string events;
    int frames = 0, polls = 0, release = 0, brightnessWrites = 0;

    void prepareFrame() override { events += 'P'; }

    void submitFrame() override { events += 'S'; }

    void waitVBlank() override {
        events += 'V';
        ++frames;
    }

    void afterVBlank() override { events += 'A'; }

    bool pollTouchRelease() override {
        events += 'T';
        return ++polls == release;
    }

    void brightness(bool, std::int32_t value) override {
        check(value >= -16 && value <= 0, "brightness range");
        ++brightnessWrites;
    }
};

struct NoCommands final : OpeningHost {
    void background(bool, const std::string&, std::int32_t) override {
        throw std::runtime_error("unexpected host call");
    }

    void fadeIn() override { throw std::runtime_error("unexpected host call"); }

    void fadeOut() override { throw std::runtime_error("unexpected host call"); }

    void wait(std::int32_t, bool) override { throw std::runtime_error("unexpected host call"); }

    void audioRequest(std::int32_t, std::uint32_t, std::uint32_t, std::int32_t) override {
        throw std::runtime_error("unexpected host call");
    }
};

struct PageServices final : NarrationHost {
    int frames = 0, skips = 0, aborts = 0, skipAt = 0, abortAt = 0, busyCalls = 0;
    int draws = 0, voices = 0, stops = 0, music = 0;

    void update() override {}

    void waitOneFrame() override { ++frames; }

    void present() override { ++frames; }

    void draw(int, int alpha) override {
        check(alpha >= 1 && alpha <= 31, "page alpha range");
        ++draws;
    }

    bool skipRequested() override { return ++skips == skipAt; }

    bool abortRequested() override { return ++aborts == abortAt; }

    void startVoice(int category, int id, int sub, float volume, float fade) override {
        check(category == 2 && id == 1 && sub == 0 && volume == 1 && fade == 0,
              "page voice arguments");
        ++voices;
    }

    bool voicePlaying() override { return busyCalls-- > 0; }

    void stopVoice(float fade) override {
        check(fade == 0, "page voice stop");
        ++stops;
    }

    void fadeMusic(int channel, float seconds) override {
        check(channel == 0 && seconds == 5, "page music fade");
        ++music;
    }
};

int main() try {
    {
        // 0x02002094 float modes, against the ROM function run in a local
        // research harness (its output block): volume modes
        // 3/5 and pan mode 3.
        struct Case {
            std::uint16_t volumeMode;
            float a, c;
            float pan;
            int volume, aux, panByte;
        };

        const Case cases[] = {
            {3, 0.5f, 0, -1.0f, 0x3f, 0, 0},    {5, 0.5f, 0.25f, 0.0f, 0x3f, 0x1f, 0x3f},
            {3, 1.0f, 0, 0.3f, 0x7f, 0, 0x52},  {5, -0.2f, 0.999f, 1.0f, 0xe7, 0x7e, 0x7f},
            {3, 0.0078f, 0, -0.5f, 0, 0, 0x1f}, {5, 2.0f, -1.0f, 0.75f, 0xfe, 0x81, 0x6f}};
        for (const auto& t : cases) {
            dse::RequestBlock r;
            auto put = [&](int offset, float v) {
                auto u = std::bit_cast<std::uint32_t>(v);
                r.half[offset / 2] = std::uint16_t(u);
                r.half[offset / 2 + 1] = std::uint16_t(u >> 16);
            };
            put(8, t.a);
            put(0xc, t.c);
            put(0x14, t.pan);
            r.half[0x12 / 2] = t.volumeMode;
            r.half[0x26 / 2] = 3;
            const auto q = dse::buildParameters(r, 0);
            check(std::uint8_t(q.volume) == t.volume && std::uint8_t(q.volumeAux) == t.aux &&
                      std::uint8_t(q.pan) == t.panByte,
                  "0x02002094 float mode differs from the ROM");
        }
    }
    {
        // Driver streams (0x020052b4 .. 0x02006bdc): the ring halves hold the
        // recovered decoder's PCM unchanged and in order; alarm-0 passes
        // refill the half that has just played.
        struct Silent final : dse::SoundCommands {
            unsigned alarmStarts = 0;
            std::uint32_t period = 0;

            void startTimers(std::uint32_t, std::uint32_t, std::uint32_t a,
                             std::uint32_t) override {
                alarmStarts += a & 1;
            }

            void stopTimers(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}

            void setupPcm(unsigned, unsigned, const std::uint8_t*, unsigned, unsigned, unsigned,
                          unsigned, unsigned, unsigned, unsigned) override {}

            void setupPsg(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned) override {}

            void setupNoise(unsigned, unsigned, unsigned, unsigned, unsigned) override {}

            void setTimer(std::uint32_t, unsigned) override {}

            void setVolume(std::uint32_t, unsigned, unsigned) override {}

            void setPan(std::uint32_t, unsigned) override {}

            void setMasterVolume(unsigned) override {}

            std::uint16_t channelStatus() override { return 0; }

            void setupAlarm(unsigned, std::uint32_t, std::uint32_t p, std::uint8_t) override {
                period = p;
            }
        };

        struct Memory final : dse::StreamFile {
            Bytes data;

            bool read(std::uint32_t o, std::uint8_t* d, std::uint32_t n) override {
                if (o > data.size() || n > data.size() - o) {
                    return false;
                }
                std::copy_n(data.begin() + o, n, d);
                return true;
            }

            std::uint32_t size() const override { return std::uint32_t(data.size()); }
        };

        for (unsigned channels : {1u, 2u}) {
            Silent hw;
            dse::Driver driver(hw, dse::SoundTables{}); // streams need no tables
            driver.initialize(0x02108760, 0x20000, 4);
            check(driver.streamInit(dse::StreamConfig{}) == 0, "stream init");
            driver.streamMonoOnTwoVoices(true);
            auto file = std::make_unique<Memory>();
            file->data = testStream(channels, false);
            SadStream reference;
            reference.open(file->data);
            const int id = driver.streamCreate(std::move(file), 1);
            check(id > 0, "stream create");
            const auto ring = driver.streamRing(std::uint32_t(id), 0);
            const std::size_t half = ring.size() / 2;
            auto expectHalf = [&](unsigned h, const char* what) {
                auto pcm = reference.refill();
                for (unsigned c = 0; c < channels; ++c) {
                    auto r = driver.streamRing(std::uint32_t(id), c);
                    for (std::size_t i = 0; i < half / 2; ++i) {
                        const std::int16_t want =
                            i < pcm.size() / channels ? pcm[i * channels + c] : 0;
                        const std::int16_t got =
                            std::int16_t(r[h * half + i * 2] | r[h * half + i * 2 + 1] << 8);
                        if (got != want) {
                            check(false, what);
                            return;
                        }
                    }
                }
            };
            expectHalf(0, "pre-filled first half differs from decoder");
            expectHalf(1, "pre-filled second half differs from decoder");
            check(driver.streamStart(std::uint32_t(id), 127, 0, 1) == 0 &&
                      driver.streamPlaying(std::uint32_t(id)) == 1,
                  "stream start");
            driver.tick();
            check(hw.alarmStarts == 1 && hw.period == std::uint32_t((512u * half) >> 6),
                  "alarm 0 start/period");
            driver.streamAlarm();
            expectHalf(0, "alarm refill of half 0");
            driver.streamAlarm();
            expectHalf(1, "alarm refill of half 1");
            check(driver.streamClose(std::uint32_t(id)) == 0 &&
                      driver.streamQuery(std::uint32_t(id)) == -0xb1,
                  "stream close");
        }
    }
    {
        Surface dst{2, 1, {0x8000, 0x800a}}, src{2, 1, {0x801f, 0x7fff}};
        blitNarrationPolygon(dst, src, 0, 0, 31);
        check(dst.pixels[0] == 0x801f && dst.pixels[1] == 0x800a,
              "polygon opacity/transparent coverage");
        rejects([&] { blitNarrationPolygon(dst, src, 0, 0, 0); });
    }
    {
        PageServices host;
        NarrationPage page{7, 0, false};
        host.skipAt = 1;
        check(page.presentPages(host, 0, 3, 1, 2) && host.frames == 1 && host.voices == 0 &&
                  page.fadeLevel == 7,
              "skip before voice preserves level");
        host = {};
        page = {};
        host.skipAt = 62;
        check(page.presentPages(host, 0, 3, 1, 2) && host.frames == 62 && host.draws == 0 &&
                  host.stops == 0,
              "fade-in skip does not clean up voice");
        host = {};
        page = {};
        host.busyCalls = 20;
        host.skipAt = 122;
        check(page.presentPages(host, 0, 3, 1, 2) && host.frames == 122 && host.stops == 0,
              "voice-wait skip returns before stop");
        host = {};
        page = {};
        host.skipAt = 122;
        check(page.presentPages(host, 0, 3, 1, 2) && host.frames == 122 && host.stops == 1 &&
                  page.fadeLevel == 31,
              "fade-out skip retains context level");
        host = {};
        page = {0, 3, false};
        host.skipAt = 1;
        host.abortAt = 1;
        check(page.presentPages(host, 0, 3, 1, 2) && host.skips == 0 && host.frames == 61,
              "mode 3 disables skip but still checks abort");
        host = {};
        page = {0, 3, true};
        check(!page.presentPages(host, 0, 3, 1, -1) && host.frames == 308 && host.music == 1 &&
                  host.skips == 0,
              "music flag overrides fade-repeat count");
        check(host.aborts == 61 && page.fadeLevel == 31,
              "abort checks confined to fade-in; fade-out uses local alpha");
        host = {};
        page = {};
        check(!page.presentPages(host, 0, 0, 1, -1) && host.frames == 122 && host.draws == 0,
              "empty actors and nonpositive fade repeats");
    }
    {
        auto f = sadFilter(3000);
        check(f.current == 147 && f.previous == 108, "ROM filter coefficients at 3000");
        f = sadFilter(5000);
        check(f.current == 245 && f.previous == 10, "ROM filter coefficients at 5000");
        Bytes block(16, 0x88);
        block[15] = 0x8c;
        SadChannelState state{0, 0, 123, 456};
        auto pcm = decodeSadBlock(block, 0xb4, {}, state);
        // -32768*64 + 32, divided toward zero, is -32767; the ROM's
        // subsequent ten-bit quantization makes that -32704, not -32768.
        check(pcm.size() == 30 && pcm[0] == -32704 && pcm[1] == 0,
              "b4 signed rounding and low-nibble-first order");
        check(state.previousSample == 123 && state.previousFiltered == 456,
              "b4 leaves interpolation state untouched");
        block.assign(16, 0x80);
        state = {0, 0, 0, 1024};
        pcm = decodeSadBlock(block, 0xb2, {128, 128}, state);
        check(pcm.size() == 60 && pcm[0] == 512 && pcm[1] == 256 && pcm[2] == 128 && pcm[3] == 64,
              "filter tail is explicit input state");
        // Encoded shifts 13..15 become ARM register ASR counts 255..253,
        // not a C++ negative shift. An out-of-range predictor falls back to 0.
        block.assign(16, 0x88);
        block[15] = 0xfd;
        state = {};
        pcm = decodeSadBlock(block, 0xb4, {}, state);
        check(pcm[0] == 0 && pcm[1] == 0 && state.olderHistory == -64,
              "out-of-range header preserves ARM shift/fallback");
        block.pop_back();
        rejects([&] { decodeSadBlock(block, 0xb2, {}, state); });
        rejects([&] { readSadHeader(block); });
        rejects([] { sadFilter(6001); });
    }
    {
        Clock clock;
        OpeningTiming timing(clock);
        timing.mainBrightness = -3;
        timing.subBrightness = -7;
        timing.fadeOverFrames(true, 30);
        check(clock.frames == 30 && timing.mainBrightness == 0 && timing.subBrightness == 0,
              "duration fade starts at fixed endpoint");
        timing.fadeOverFrames(false, 30);
        check(clock.frames == 60 && clock.brightnessWrites == 120 && timing.mainBrightness == -16,
              "duration fade extent");
        rejects([&] { timing.fadeOverFrames(true, 0); });
    }
    {
        Button b({10, 20, 19, 19});
        b.update({true, true, false, 10, 20});
        check(b.state == Button::State::Armed && b.inside, "button press");
        b.update({true, false, false, 50, 50});
        b.update({false, false, true, 50, 50});
        check(!b.consumeClick() && b.state == Button::State::Idle, "drag out cancels");
        b.update({true, true, false, 29, 39});
        b.update({false, false, true, 29, 39});
        for (int i = 0; i < 5; ++i) {
            check(!b.consumeClick(), "click too early");
            b.update({});
        }
        check(b.consumeClick() && !b.consumeClick(), "one delayed click");
        NameEditor n;
        for (int i = 0; i < 12; ++i) {
            n.insert('a' + i);
        }
        check(n.value() == "abcdefghil" && n.cursor == 9, "full name replaces final cell");
        n.erase();
        check(n.cursor == 9 && n.value() == "abcdefghi", "full-cell erase");
        n.erase();
        check(n.cursor == 8 && n.value() == "abcdefgh", "empty-cell erase");
        const std::array<std::string, 2> lead{"Hero", "Aide"};
        n.clear();
        for (char c : std::string("Hero")) {
            n.insert(c);
        }
        check(n.reserved(lead), "reserved name");
        n.characters[0] = 'h';
        check(!n.reserved(lead), "reserved comparison is exact");
        AnimationPlayer p;
        std::vector<std::vector<AnimationStep>> a{{}, {{1, 6, 0}, {0, 6, 1}}};
        p.select(1, 100, a);
        p.advance(106, a);
        check(p.step == 0, "strict animation deadline");
        p.advance(107, a);
        check(p.step == 1 && p.lastTick == 107, "animation follows next index");
        p.lastTick = 0xfffffffc;
        p.advance(3, a);
        check(p.step == 0, "tick counter wraps unsigned");
    }
    {
        Clock clock;
        OpeningTiming timing(clock);
        timing.wait(0);
        check(clock.events == "SVA", "zero-count wait");
        clock.events.clear();
        timing.wait(3);
        check(clock.events == "SVAPSVAPSVA", "ordinary wait order");
        clock.events.clear();
        timing.waitForTouchRelease(0);
        check(clock.events.empty(), "zero interruptible wait");
        timing.waitForTouchRelease(-10);
        check(clock.events.empty(), "negative interruptible wait");
        clock.release = 3;
        timing.waitForTouchRelease(10);
        check(clock.events == "TPSVATPSVAT", "release should return before another frame");
    }
    {
        Clock clock;
        OpeningTiming timing(clock);
        timing.fadeIn();
        check(clock.frames == 16 && clock.brightnessWrites == 32 && timing.mainBrightness == 0,
              "fade-in extent");
        timing.fadeOut();
        check(clock.frames == 33 && clock.brightnessWrites == 66 && timing.subBrightness == -16,
              "inclusive fade-out extent");
        check(timing.fadeSteps == 33, "fade counter");
        timing.mainBrightness = -3;
        timing.subBrightness = -1;
        int before = clock.frames;
        timing.fadeIn();
        check(clock.frames - before == 3, "unequal levels early termination");
    }
    {
        Bytes file;
        word(file, 12);
        file.insert(file.end(), {2, 0, 0, 0, 0, 128, 1, 0, 255, 255, 255, 255});
        ScriptReader reader(file);
        auto a = reader.next();
        auto b = reader.next();
        check(a.next == 6 && b.next == 12 && reader.values().floatBits == 0x80000000 &&
                  reader.values().integerBits == 0xffffffff,
              "exact float/sign bits");
        rejects([&] { reader.next(); });
        file.pop_back();
        rejects([&] { ScriptReader bad(file); });
        Bytes truncated{4, 0, 0, 0, 3, 0, 255, 255};
        ScriptReader bad(truncated);
        rejects([&] { bad.next(); });
        Bytes unknown{2, 0, 0, 0, 13, 0};
        ScriptReader unknownReader(unknown);
        rejects([&] { unknownReader.next(); });
        Bytes stop{6, 0, 0, 0, 0, 0, 1, 0, 12, 0};
        ScriptInterpreter stopped(stop);
        NoCommands host;
        stopped.run(openingContext(host));
        check(stopped.reader.position() == 6, "command 1 returns body end");
        stop[6] = 255;
        ScriptInterpreter unsupported(stop);
        rejects([&] { unsupported.run(openingContext(host)); });
    }
    {
        Bytes archive;
        word(archive, 16);
        word(archive, 40);
        word(archive, 0x324b4350);
        word(archive, 0);
        word(archive, 20);
        word(archive, 24);
        word(archive, 0);
        word(archive, 3);
        archive.insert(archive.end(), {'x', 0, 0, 0, 11, 22, 33, 0});
        auto retained = [&] {
            PckArchive owner{Resource(archive)};
            check(!owner.find("X"), "case-sensitive lookup");
            return owner.find("x").value();
        }();
        check(retained.bytes().size() == 3 && retained.bytes()[2] == 33,
              "member lifetime independent of archive");
        archive[20] = 0;
        rejects([&] { PckArchive bad{Resource(archive)}; });
        rejects([&] { retained.slice(2, 2); });
    }
    check(resolveLanguagePath("/data_lt2/bg/name/?/name_sub.arc", "ge") ==
              "/data_lt2/bg/name/ge/name_sub.arc",
          "language substitution");
    check(backgroundPath("logo/level5.bgx") == "/data_lt2/bg/logo/level5.arc",
          "background extension");
    rejects([] { resolveLanguagePath("?/??", "ge"); });
    std::cout << "Core edge cases passed. ROM differential coverage is a separate validation.\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
