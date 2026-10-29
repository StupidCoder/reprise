// The judgment sequence (overlay 2, 0206b4fc). See judgment.hpp.
#include "puzzles/judgment.hpp"
#include <algorithm>
#include <stdexcept>

namespace picarat {

std::uint32_t gameRandom(std::uint32_t& state) {
    state = state * 0x41c64e6du + 0x3039u;
    return (state >> 16) & 0x7fff;
}

void playJudgmentPictures(std::span<const std::uint8_t> table, unsigned mode,
                          const JudgmentPictures& pictures, JudgmentHost& host) {
    if (table.size() != 114) {
        throw std::runtime_error("judgment table size");
    }
    const unsigned wrong = mode & 1;
    const char character = (mode & 2) ? 'r' : 'l'; // picture set: judge_l*/judge_r*
    // Show the pictures of the table, each fading in from the current level.
    // Row layout: {picture if correct, picture if wrong, fade frames}.
    int level = -16;
    std::size_t row = 0;
    for (;;) {
        if (host.aborted()) {
            return;
        }
        const unsigned index = read16(table, row + wrong * 2);
        const bool final = index == 14 || index == 114;
        host.brightness(level);
        host.image(pictures.path(character, index, final), final);
        host.present();
        // Pictures 10 / 110 come with the "correct" / "wrong" sound.
        if (index == 10 || index == 110) {
            host.sound(wrong ? pictures.wrongSound : pictures.correctSound);
        }
        // Fade in: 7 brightness steps per frame (2 for the verdict picture).
        const int step = final ? 2 : 7;
        for (int i = 0; i < int(read16(table, row + 4)); ++i) {
            level = std::min(0, level + step);
            host.brightness(level);
            host.wait(1);
        }
        row += 6;
        if (row + 6 > table.size()) {
            throw std::runtime_error("judgment table missing sentinel");
        }
        const unsigned next = read16(table, row + wrong * 2);
        if (next == 0xffff) {
            host.upperBrightness(0);
            break;
        }
        if (next == 0) {
            // A pause row: wait 6 frames, fade to black over 17 frames
            // (overlay 2, 02072260; ARM9 0200ff00 in the first adventure),
            // hold black for the row's frame count, and continue with the
            // row after it.
            host.wait(6);
            for (int i = 0; i <= 16; ++i) {
                host.wait(1);
                level = std::clamp(level - 1, -16, 0);
                host.brightness(level);
            }
            host.brightness(-16);
            host.wait(read16(table, row + 4));
            row += 6;
            level = -16;
        }
    }
}

void runJudgment(std::span<const std::uint8_t> table, unsigned mode, bool special,
                 unsigned randomValue, JudgmentHost& host) {
    if (table.size() != 114) {
        throw std::runtime_error("judgment table size");
    }
    const unsigned wrong = mode & 1;
    host.effectBank(wrong ? 5 : 4);
    host.sound(0xc9);
    host.openWindows();
    host.slide(0, -191); // the upper picture starts above the screen
    JudgmentPictures pictures;
    pictures.path = [](char set, unsigned index, bool verdict) {
        return std::string("nazo/hantei/") + (verdict ? "ge/" : "") + "judge_" + set +
               std::to_string(index) + "_bg";
    };
    pictures.correctSound = 0xca;
    pictures.wrongSound = 0xcb;
    playJudgmentPictures(table, mode, pictures, host);
    if (host.aborted()) {
        return;
    }

    // After 22 frames both screens slide by 16 pixels per frame until the
    // upper picture is in place.
    host.wait(22);
    int bottom = 0;
    int top = -191;
    for (; bottom < 192; bottom += 16, top += 16) {
        host.slide(bottom, top);
        host.clipWindows(bottom, top);
        host.present();
    }
    host.slide(bottom, top);
    host.presentWithoutPoll();
    host.clearWindows();
    host.brightness(-16);
    host.slide(0, 0);
    host.present();
    host.windowsOff();
    host.wait(37);

    // The voice line: 110..112 (correct) or 120..122 (wrong), +5 with the
    // "r" picture set, chosen at random. Special puzzles always use 112 / 120.
    unsigned voice = (wrong ? 120u : 110u) + ((mode & 2) ? 5u : 0u) + randomValue % 3;
    if (special) {
        voice = wrong ? 120 : 112;
    }
    host.voice(voice);
}

} // namespace picarat
