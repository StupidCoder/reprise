// Tests of the first adventure's sound: the BIOS curves, and - when a pack
// is available - a puzzle's title card on the sound object clocked by the
// frames: the puzzle music sounds, an effect's end lets the result's wait
// finish, and two runs give the same samples; a voice line as a stream.
//
// usage: a1_sound_tests [PACK]   (PACK defaults to $PICARAT_A1_PACK)
#include "sound/a1_sound.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace picarat;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

void curves() {
    // The BIOS pitch curve: one octave is 768 steps, the last step just below
    // doubling; the volume curve ends at full volume, steps down by the
    // divider's ranges.
    check(sdat::pitchCurve(0) == 0, "pitch curve start");
    check(sdat::pitchCurve(767) > 0xfe00, "pitch curve end");
    check(sdat::volumeCurve(723) == 127, "volume curve end");
    check(sdat::volumeCurve(0) == 0, "volume curve start");
    check(sdat::volumeCurve(483) == 32 && sdat::volumeCurve(482) == 127, "volume curve ranges");
}

// The calls of a title card (0203c85c / 0203db80) and the result's wait;
// returns a checksum of the samples and the frames the wait took.
std::pair<std::uint64_t, int> titleCard(const std::filesystem::path& pack) {
    Adventure1Sound sound(pack);
    sound.clockWithFrames(true);
    std::uint64_t sum = 0, energy = 0;
    int frames = 0;
    std::vector<std::int16_t> pcm;
    auto frame = [&] {
        sound.frame();
        ++frames;
    };
    sound.rememberMusic();
    sound.stopMusic(0x3c);
    sound.setupScene(5, frame);
    sound.loadVoice(1, frame);
    sound.effect(0); // the voice
    for (int i = 0; i < 120; ++i) {
        frame();
    }
    sound.playMusic(3);
    for (int i = 0; i < 300; ++i) {
        frame();
    }
    // A puzzle's effects, an effect, then the result's sounds: the wait ends.
    sound.setupPuzzle(23, 0, frame);
    sound.effect(0xb);
    frame();
    const int before = frames;
    sound.setupScene(0xa, frame);
    const int waited = frames - before;
    check(waited > 30 && waited < 400, "the result's wait");
    // The music still plays: render a second (outside the frame clock,
    // so last) and look at it.
    pcm.resize(32728 * 2);
    sound.render(pcm);
    for (const auto v : pcm) {
        sum = sum * 31 + std::uint16_t(v);
        energy += std::uint64_t(std::abs(int(v)));
    }
    check(energy > pcm.size() * 4, "the puzzle music is silent");

    return {sum, waited};
}

// A voice line as a stream (020045f8): silent for the first two refills
// (the ring starts cleared, the first refill fills the half just left),
// then the voice; the channels stop on their own after the data.
void voiceStream(const std::filesystem::path& pack) {
    if (!std::filesystem::exists(pack / "stream/SP_V_OP_101.SAD")) {
        return;
    }
    Adventure1Sound sound(pack);
    sound.clockWithFrames(true);
    sound.frame();
    sound.playStream("SP_V_OP_101.SAD", 10, 11, 127);
    check(sound.streamPlaying(), "the stream does not start");
    std::vector<std::int16_t> pcm(2);
    std::uint64_t early = 0, later = 0;
    for (int i = 0; i < 2048; ++i) { // the frame clock is not advanced here
        sound.render(pcm);
        early += std::uint64_t(std::abs(int(pcm[0])) + std::abs(int(pcm[1])));
    }
    for (int i = 0; i < 32728 * 6; ++i) { // the line is five seconds long
        sound.render(pcm);
        later += std::uint64_t(std::abs(int(pcm[0])) + std::abs(int(pcm[1])));
    }
    check(early == 0, "the stream sounds before its first refill");
    check(later > 32728 * 8, "the voice is silent");
    check(!sound.streamPlaying(), "the stream does not end");
}
} // namespace

int main(int argc, char** argv) try {
    curves();
    const char* env = std::getenv("PICARAT_A1_PACK");
    const std::filesystem::path pack = argc > 1 ? argv[1] : env ? env : "";
    if (pack.empty() || !std::filesystem::exists(pack / "sound/sound_data.sdat") ||
        !std::filesystem::exists(pack / "code/arm7.bin")) {
        std::cout << "a1_sound_tests: curves passed; no pack, sound skipped\n";
        return 77;
    }
    voiceStream(pack);
    const auto a = titleCard(pack), b = titleCard(pack);
    check(a == b, "two runs differ");
    std::cout << "a1_sound_tests passed (result wait " << a.second << " frames)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "a1_sound_tests: " << e.what() << "\n";
    return 1;
}
