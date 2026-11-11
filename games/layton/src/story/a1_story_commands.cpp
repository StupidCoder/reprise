// The first adventure's script commands that work the same on every script
// object: screen fades, backgrounds, waits, sound, story flags and the
// choice of the next scene. Commands that hand their arguments to the
// running object (a room, an event) are bound by that scene; on the base
// object (logo.gds, room<N>_in.gds) those functions are empty.
//
// Handlers are cited by their ARM9 address (the table at 0220d08c, filled
// by 020171b0). See a1_script.cpp for the argument layouts.
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_script.hpp"
#include <algorithm>
#include <cstring>

namespace picarat::adventure1 {

namespace {

// The fades the commands use whose frame order differs from the shared
// helpers (system/a1_ui.hpp).
// 0200f6d4 (0x01): poll and present, then both screens step toward 0; ends
// once both are 0 (at most 17 steps).
void fadeInBothAfterFrame(Story& story, int step) {
    auto& kit = story.kit;
    for (int i = -16; i <= 0; ++i) {
        story.frame([] {});
        kit.setLowerBrightness(std::min(0, kit.lowerBrightness() + step));
        kit.setUpperBrightness(std::min(0, kit.upperBrightness() + step));
        if (kit.lowerBrightness() == 0 && kit.upperBrightness() == 0) {
            return;
        }
    }
}

// 0200f8a0 (0x03): the upper screen toward 0 over VBlank waits (no frame).
void fadeInUpperWaiting(Story& story, int step) {
    auto& kit = story.kit;
    for (int i = -16; i <= 0; ++i) {
        story.stall(1);
        kit.setUpperBrightness(std::min(0, kit.upperBrightness() + step));
        if (kit.upperBrightness() == 0) {
            return;
        }
    }
}

// 0200fc24 (0x05): poll and present, then both screens step toward -16; 17
// steps whatever the start.
void fadeOutBothAfterFrame(Story& story, int step) {
    auto& kit = story.kit;
    for (int i = 0; i > -17; i -= step) {
        story.frame([] {});
        kit.setLowerBrightness(std::max(-16, kit.lowerBrightness() - step));
        kit.setUpperBrightness(std::max(-16, kit.upperBrightness() - step));
    }
}

// 0x10 (020109f0): sound effects by a script's index (a switch in the
// handler; 0 plays nothing, 6 loads voice group 2 and plays its line).
constexpr int kScriptEffects[] = {
    -1,    0,     0xb,   9,     0x36,  0x37,  -2,    0x2c8, 0x2c9,
    0x2ca, 0x2cc, 0x2cb, 0x2cd, 0x2d0, 0x2ce, 0x2cf, 0x32,  0x22,
};
} // namespace

ScriptContext Story::commonCommands() {
    ScriptContext c;
    // --- Fades (each one blocks until done).
    c.bind(0x01, [this](CommandArguments&) { fadeInBothAfterFrame(*this, 1); });
    c.bind(0x02, [this](CommandArguments&) { fadeIn(kit, Screens::Both); });   // 0200f7b4
    c.bind(0x03, [this](CommandArguments&) { fadeInUpperWaiting(*this, 1); }); // 0200f8a0
    c.bind(0x04, [this](CommandArguments&) { fadeIn(kit, Screens::Lower); });  // 0200fb90
    c.bind(0x05, [this](CommandArguments&) { fadeOutBothAfterFrame(*this, 1); });
    c.bind(0x06, [this](CommandArguments&) { fadeOut(kit, Screens::Both); });  // 0200fce8
    c.bind(0x07, [this](CommandArguments&) { fadeOut(kit, Screens::Upper); }); // 0200fe78
    c.bind(0x08, [this](CommandArguments&) { fadeOut(kit, Screens::Lower); }); // 0200ff00
    // --- Waits.
    c.bind(0x09, [this](CommandArguments&) { waitForTouch(); }); // 02003e44 (touch only)
    c.bind(0x0a, [this](CommandArguments&) { waitForTouch(); }); // 02003de0 (touch or A)
    c.bind(0x0d, [this](CommandArguments& a) { stallUntilRelease(a.integer()); }); // 02003d94
    c.bind(0x6b, [this](CommandArguments& a) { stall(a.integer()); });             // 02003d64
    // 0x7a (02013e98): n frames (poll and present).
    c.bind(0x7a, [this](CommandArguments& a) {
        for (int n = a.integer(), i = 0; i < n; ++i) {
            frame([] {});
        }
    });
    // --- Backgrounds and screens.
    // 0x0b (02010864): lower background (the object's virtual +0x60 first:
    // empty on the base object).
    c.bind(command::MainBackground, [this](CommandArguments& a) {
        const std::string name = a.string();
        a.integer(); // layer
        setPicture(kit.lowerBackground, name);
    });
    // 0x0c (020108cc): upper background.
    c.bind(command::SubBackground, [this](CommandArguments& a) {
        const std::string name = a.string();
        a.integer(); // layer
        setPicture(kit.upperBackground, name);
    });
    // 0x0e (02010964): both text layers and all background maps cleared,
    // then a frame presented.
    c.bind(0x0e, [this](CommandArguments&) {
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        kit.lowerOverlay.clear();
        kit.upperOverlay.clear();
        present();
    });
    // 0x0f (020109c4): present a frame.
    c.bind(0x0f, [this](CommandArguments&) { present(); });
    // 0x5f (020134c0): both text layers cleared, a frame presented.
    c.bind(0x5f, [this](CommandArguments&) {
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        present();
    });
    // 0xde (02016818 -> 02005698): marks the sprite memory used so far as
    // kept (the loader's VRAM watermark); nothing to do natively.
    c.bind(0xde, [](CommandArguments&) {});
    // --- Sound.
    c.bind(0x10, [this](CommandArguments& a) {
        const int index = a.integer();
        if (!sound || index < 0 || index >= int(std::size(kScriptEffects))) {
            return;
        }
        if (kScriptEffects[index] == -2) {
            sound->loadVoice(2, [this] { present(); });
            sound->effect(0);
        } else if (kScriptEffects[index] >= 0) {
            sound->effect(kScriptEffects[index]);
        }
    });
    c.bind(0x11, [this](CommandArguments& a) { // 02010ee8: 0200530c
        const int music = a.integer();
        if (sound) {
            sound->playMusic(music);
        }
    });
    c.bind(0x59, [this](CommandArguments&) { // 020131f8: effect 700
        if (sound) {
            sound->effect(700);
        }
    });
    // --- The story state.
    c.bind(0x48, [this](CommandArguments& a) { state.setPuzzle(a.integer()); }); // 02012ac0
    // 0x49 (02012b20): the current puzzle has been seen.
    c.bindCondition(
        0x49, [this](CommandArguments&) { return (state.progress(state.puzzleNumber) & 1) != 0; });
    // 0x4a / 0x4b (02012b9c / 02012be0): the event runs before / after its
    // puzzle (+0x128).
    c.bindCondition(0x4a, [this](CommandArguments&) { return state.afterPuzzle == 0; });
    c.bindCondition(0x4b, [this](CommandArguments&) { return state.afterPuzzle == 1; });
    // 0x4c / 0x4d (02012c24 / 02012c68): the last answer was wrong / right.
    c.bindCondition(0x4c, [this](CommandArguments&) { return !state.puzzle.solved; });
    c.bindCondition(0x4d, [this](CommandArguments&) { return state.puzzle.solved; });
    // 0x4e (02012ca0): the current puzzle has bit 4 (its event has run after
    // it was solved).
    c.bindCondition(
        0x4e, [this](CommandArguments&) { return (state.progress(state.puzzleNumber) & 4) != 0; });
    // 0x54 (0201301c): puzzle n is solved.
    c.bindCondition(0x54, [this](CommandArguments& a) {
        const int n = a.integer();
        return (n >= 1 && n <= 0xfe) || n < 0 ? (state.progress(n) & 2) != 0 : false;
    });
    // 0x55 (020130b8): the current event gets mark 2.
    c.bind(0x55, [this](CommandArguments&) {
        if (state.event < state.events.size()) {
            state.events[state.event] |= 2;
        }
    });
    // 0x58 (02013178): event n has run.
    c.bindCondition(0x58, [this](CommandArguments& a) {
        const int n = a.integer();
        return n >= 0 && n < int(state.events.size()) && (state.events[std::size_t(n)] & 1);
    });
    // 0x60 (02013504): the event to run (0..0x1df, else 0).
    c.bind(0x60, [this](CommandArguments& a) {
        int n = a.integer();
        if (n < 0 || n > 0x1df) {
            n = 0;
        }
        state.event = std::uint32_t(n);
    });
    // 0x62 / 0x63 (02013594 / 020135d4): a 16-bit story number (+0x1e).
    c.bind(0x62, [this](CommandArguments& a) { state.value1e = std::uint16_t(a.integer()); });
    c.bindCondition(
        0x63, [this](CommandArguments& a) { return state.value1e == std::uint16_t(a.integer()); });
    // 0x51 (02012de0): the next scene, by name.
    c.bind(0x51, [this](CommandArguments& a) {
        const std::string name = a.string();
        static const std::pair<const char*, std::uint8_t> names[] = {
            {"Question", scene::Question},
            {"Room", scene::Room},
            {"Event", scene::Event},
            {"Movie", scene::Movie},
            {"Event Movie", scene::EventMovie},
            {"Reset", scene::Reset},
            {"Taiken", scene::Taiken},
            {"Challenge Mode", scene::Challenge},
            {"Staff Roll", scene::StaffRoll},
        };
        for (const auto& [n, value] : names) {
            // 0200c08c: case-insensitive comparison.
            if (n == name ||
                std::equal(name.begin(), name.end(), n, n + std::strlen(n), [](char x, char y) {
                    return std::tolower(std::uint8_t(x)) == std::tolower(std::uint8_t(y));
                })) {
                state.scene = value;
                return;
            }
        }
    });
    // 0x52 (02012f24): the scene a menu page returns to (+0x11), by name.
    c.bind(0x52, [this](CommandArguments& a) {
        const std::string name = a.string();
        static const std::pair<const char*, std::uint8_t> names[] = {
            {"question", scene::Question},
            {"room", scene::Room},
            {"event", scene::Event},
            {"challenge mode", scene::Challenge},
        };
        std::string lower;
        for (char ch : name) {
            lower += char(std::tolower(std::uint8_t(ch)));
        }
        for (const auto& [n, value] : names) {
            if (lower == n) {
                state.returnScene = value;
                return;
            }
        }
    });
    // 0x53 (02012fdc): the room to enter.
    c.bind(0x53, [this](CommandArguments& a) { state.room = std::uint8_t(a.integer()); });
    // 0x8d / 0x8e (02014570 / 020145c0): story flags.
    c.bindCondition(0x8d, [this](CommandArguments& a) { return state.flag(a.integer()); });
    c.bind(0x8e, [this](CommandArguments& a) {
        const int n = a.integer();
        const bool on = a.truth();
        state.setFlag(n, on);
    });
    // 0xb7 (02015774): puzzle n seen, solved and done.
    c.bind(0xb7, [this](CommandArguments& a) { state.progress(a.integer()) |= 1 | 2 | 4; });
    // 0xb8 (02015860): event n has run.
    c.bind(0xb8, [this](CommandArguments& a) {
        const int n = a.integer();
        if (n >= 0 && n < int(state.events.size())) {
            state.events[std::size_t(n)] |= 1;
        }
    });
    // 0x66 / 0x67 (0201369c / 020136d0): which set of judgement pictures the
    // next result shows (+0x4f0).
    c.bind(0x66, [this](CommandArguments&) { state.puzzle.secondJudge = false; });
    c.bind(0x67, [this](CommandArguments&) { state.puzzle.secondJudge = true; });
    // 0xc0 (02015c2c): the picture after a wrong answer (+0xd).
    c.bind(0xc0,
           [this](CommandArguments& a) { state.puzzle.wrongPicture = std::uint8_t(a.integer()); });
    // 0xd3 (02016430): the movie number (+0x70c).
    c.bind(0xd3, [this](CommandArguments& a) { state.movie = a.integer(); });
    // 0xda (020166b4): the last choice was n.
    c.bindCondition(0xda, [this](CommandArguments& a) { return a.integer() == state.choice; });
    // 0xdd (020167cc): movie n, then the dual-screen movie scene (0x1b).
    c.bind(0xdd, [this](CommandArguments& a) {
        state.movie = a.integer();
        state.scene = scene::DualMovie;
    });
    // 0xe9 (02016ac8): the event after the coming scene (+0x74c).
    c.bind(0xe9, [this](CommandArguments& a) { state.nextEvent = a.integer(); });
    // 0xea (02016b08): go to that event, or back to the room when none.
    c.bind(0xea, [this](CommandArguments&) {
        int n = state.nextEvent;
        if (n == -1) {
            state.scene = scene::Room;
            return;
        }
        if (n < 0 || n > 0x1df) {
            n = 0;
        }
        state.event = std::uint32_t(n);
        state.scene = scene::Event;
    });
    // 0x85 / 0x86 / 0xce (02019afc / 02019bb0 / 02019b24): the items
    // carried (8 places).
    c.bind(0x85, [this](CommandArguments& a) {
        const auto item = std::uint8_t(a.integer());
        for (auto& slot : state.items) {
            if (slot == 0) {
                slot = item;
                return;
            }
        }
    });
    c.bindCondition(0x86, [this](CommandArguments& a) {
        const auto item = std::uint8_t(a.integer());
        return std::find(state.items.begin(), state.items.end(), item) != state.items.end();
    });
    c.bind(0xce, [this](CommandArguments& a) {
        const auto item = std::uint8_t(a.integer());
        for (auto& slot : state.items) {
            if (slot == item) {
                slot = 0;
            }
        }
    });
    // 0x9e (02014c20, 0201bb88): the chapter only rises.
    c.bind(0x9e, [this](CommandArguments& a) {
        // 0201bb88: each chapter passed adds its story memo entry.
        const int chapter = a.integer();
        if (state.chapter < chapter) {
            for (int c = state.chapter + 1; c <= chapter; ++c) {
                state.addMemo(c);
            }
            state.chapter = std::uint8_t(chapter);
        }
    });
    // 0x77 (02013d54): at least n puzzles solved.
    c.bindCondition(0x77, [this](CommandArguments& a) {
        const int n = a.integer();
        return state.solvedCount() >= n;
    });
    // 0x7b / 0x7c (02013f08 / 02013f88): the lower / upper pictures' colours
    // move toward (r, g, b) by factor / 256 (02008e70 / 02009074).
    c.bind(0x7b, [this](CommandArguments& a) {
        const int r = a.integer(), g = a.integer(), b = a.integer(), f = a.integer();
        kit.lowerTint = {r, g, b, f};
    });
    c.bind(0x7c, [this](CommandArguments& a) {
        const int r = a.integer(), g = a.integer(), b = a.integer(), f = a.integer();
        kit.upperTint = {r, g, b, f};
    });
    // 0x7d (02014008 -> 020056bc on 020e0640): restores a saved setting of
    // that object (its meaning is not established; no visible effect found).
    c.bind(0x7d, [](CommandArguments&) {});
    // --- Music and effects.
    c.bind(0x69, [this](CommandArguments& a) { // 020137a8: 020051a4
        a.integer();
        if (sound) {
            sound->fadeOutMusicSlowly();
        }
    });
    c.bind(0x6a, [this](CommandArguments& a) { // 020137ec: 0200527c
        a.integer();
        if (sound) {
            sound->restoreMusicVolume();
        }
    });
    c.bind(0xec, [this](CommandArguments& a) { // 02016bb8: 020052f4
        const int volume = a.integer(), frames = a.integer();
        if (sound) {
            sound->moveMusicVolume(volume, frames);
        }
    });
    c.bind(0xed, [this](CommandArguments& a) { // 02016c0c: 02004d94
        const int music = a.integer();
        a.integer();
        if (sound) {
            sound->loadAndPlayMusic(music, [this] { stall(1); });
        }
    });
    c.bind(0xee, [this](CommandArguments& a) { // 02016c60: 02004b08
        const int kind = a.integer();
        if (sound) {
            sound->setupScene(kind, [this] { stall(1); });
        }
    });
    c.bind(0xf3, [this](CommandArguments& a) { // 02016e20: 020053ac
        const int id = a.integer();
        if (sound) {
            sound->effect(id);
        }
    });
    c.bind(0xf4, [this](CommandArguments& a) { // 02016e64: event n's sounds
        int n = a.integer();
        if (n < 0 || n > 0x1df) {
            n = 0;
        }
        if (sound) {
            sound->setupEvent(n, [this] { stall(1); });
        }
    });
    c.bind(0xf7, [this](CommandArguments&) { // 02016f58: 0200520c
        if (sound) {
            sound->fadeOutMusicSlowly();
        }
    });
    c.bind(0xf8, [this](CommandArguments&) { // 02016f84: 020052b4
        if (sound) {
            sound->fadeInMusicSlowly();
        }
    });
    c.bind(0xf9, [this](CommandArguments&) { // 02016fb0: 02005244
        if (sound) {
            sound->fadeOutMusicQuickly();
        }
    });
    // --- A movie's description (the movie scripts; 020dedf8).
    c.bind(0xef, [this](CommandArguments& a) { // 02016ca4: 02019580
        MovieCues::Music m;
        m.frame = a.integer();
        m.frames = a.integer();
        m.volume = a.integer();
        m.sequence = a.integer();
        movieCues.music.push_back(m);
    });
    c.bind(0xf0, [this](CommandArguments& a) { // 02016d24: 020195c8
        MovieCues::Music m;
        m.frame = a.integer();
        m.frames = a.integer();
        m.volume = a.integer();
        movieCues.music.push_back(m);
    });
    c.bind(0xfd, [this](CommandArguments& a) { // 02017100: 020195fc
        MovieCues::Caption cap;
        cap.text = a.integer();
        cap.start = a.real();
        cap.end = cap.start + a.real();
        movieCues.captions.push_back(cap);
    });
    // --- Input conditions.
    c.bindCondition(0xc1, [this](CommandArguments&) { return touch.released; }); // 02015c6c
    c.bindCondition(0xeb,
                    [](CommandArguments&) { return false; }); // 02016b78: a key held (02003b34)
    // 0x87 / 0x88 (02014398 / 020143d4): words +0x68 / +0x6c of the object
    // at 021045a8 (meaning not established).
    c.bind(0x87, [](CommandArguments& a) { a.integer(); });
    c.bind(0x88, [](CommandArguments& a) { a.integer(); });
    c.bind(0xf1, [](CommandArguments& a) { // 02016d88: the same object's +0x64 / +0x68
        a.integer();
        a.integer();
    });
    // --- Base-object virtual functions: empty unless a scene binds them.
    for (std::uint16_t command :
         {0x22, 0x43, 0x50, 0x56, 0x57, 0x5a, 0x5b, 0x5c, 0x61, 0x65, 0x68, 0x6c, 0x6d, 0x6e, 0x6f,
          0x70, 0x71, 0x72, 0x78, 0x79, 0x7e, 0x8f, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0xab, 0xaf,
          0xb3, 0xbc, 0xbd, 0xc2, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xcf,
          0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xdb, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xfe}) {
        c.bind(command, [](CommandArguments& a) {
            while (!a.consumed()) {
                a.skip();
            }
        });
    }
    return c;
}

} // namespace picarat::adventure1
