// The first adventure's native story screens around the new game, and its
// movies. See a1_opening.hpp.
#include "story/a1_opening.hpp"
#include "graphics/text.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "video/movie.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {
// The fonts' line feed (NFTR FINF byte +1 of data/font/fontevent.NFTR; the
// text routines step lines by it).
constexpr int kFontEventLineFeed = 12;

// The talk text of 0200e1d4: characters [from, to) of `text` from (x0, y0)
// on the lower text layer with fontevent; a newline steps line feed + 4,
// "#X" sets the colour, "@c" clears the layer from y0 down and restarts,
// other "@" codes take no room. Returns nothing; the layer keeps the text.
void drawTalk(Story& story, int x0, int y0, const std::string& text, std::size_t from,
              std::size_t to) {
    SceneKit& kit = story.kit;
    const Font& font = kit.assets.font("fontevent.NFTR");
    Surface& layer = kit.textLayer(SceneKit::Lower);
    to = std::min(to, text.size());
    int x = x0, y = y0, colour = 0xff;
    for (std::size_t i = 0; i < to; ++i) {
        const auto c = std::uint8_t(text[i]);
        if (c == '\n') {
            y += kFontEventLineFeed + 4;
            x = x0;
        } else if (c == '#') {
            ++i;
            if (i < text.size()) {
                const char k = text[i];
                colour = k == 'R' ? 0xfc : k == 'G' ? 0xfb : k == 'W' ? 0xfe : 0xff;
            }
        } else if (c == '@') {
            ++i;
            if (i < text.size() && text[i] == 'c') {
                if (i == from) {
                    for (int py = y0; py < 192; ++py) {
                        for (int px = 0; px < 256; ++px) {
                            layer.pixels[std::size_t(py) * 256 + std::size_t(px)] = 0;
                        }
                    }
                }
                while (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) {
                    ++i;
                }
                x = x0;
                y = y0;
            } else if (i < text.size() && text[i] == 'W' && i + 2 == to && story.sound) {
                story.stall(5);
            }
        } else {
            if (i >= from) {
                textGlyph(layer, font, c, x, y, textColour(colour));
            }
            x += textAdvance(font, c) + 1;
        }
    }
}

// A sprite of the talk screens (020072b0 objects: position +0x30, offset
// +0x34): drawn with its alpha (020081d4).
struct Picture {
    SceneKit::SpriteObject sprite;
    int x = 0, y = 0;

    void load(SceneKit& kit, const std::string& name) { sprite = kit.load(name, 0, 0, 1); }

    void draw(SceneKit& kit, int alpha) {
        sprite.x = x;
        sprite.y = y;
        kit.draw(sprite, alpha);
    }
};
} // namespace

// ---------------------------------------------------------------------------
// The opening text.

void openingText(Story& story) {
    SceneKit& kit = story.kit;
    // Timed pictures and text over the narrator: the host's skip
    // fast-forwards them (the stream is closed at the end, 02035eb4).
    SceneKit::Skippable skippable(kit);
    story.marker("opening-ctor");
    const auto start = story.kit.frames;
    // 02035d90: the letter (lower), the title picture (upper); the five
    // lines of text as sprites; the text layer blends over the letter.
    story.present();
    kit.lowerBackground = "ibg_20";
    kit.lowerOverlay.clear();
    kit.upperBackground = kit.localized("?/ara_top");
    story.present();
    SceneKit::SpriteObject lines[5];
    for (int i = 0, y = 0x27; i < 5; ++i, y += 0x17) {
        if (i == 4) {
            y = 0x8f;
        }
        lines[i] = kit.load("?/op_message.spr", 0x10, y, unsigned(i + 1));
        lines[i].x = 0x10;
        lines[i].y = y;
    }
    // Measured (new game, ROM frames 1365..1373).
    story.loadingPause(start, 8);
    // 02035f30.
    story.marker("opening-run");
    story.frame([] {});
    fadeIn(kit, Screens::Both);
    // The letter darkens in 16 steps (the picture reloaded, then tinted
    // toward black by 10, 20, ... 160 / 256), five VBlanks apart.
    for (int i = 0; i < 16; ++i) {
        kit.lowerTint = {0, 0, 0, 10 + 10 * i};
        kit.hold(2); // the picture reloaded from the cartridge (measured)
        story.frame([] {});
        story.stall(5);
    }
    for (int i = 0; i < 25; ++i) {
        story.poll();
        story.stall(1);
    }
    // The line with the player's name (itext t_0.txt, "%s" the name) in
    // white, centred on y 0x52; it fades in over the letter by blending
    // (one step every four frames up to 15), stays a second, fades out.
    std::string line = story.text("itext", "itext", "t_0.txt");
    if (const auto at = line.find("%s"); at != std::string::npos) {
        line.replace(at, 2, story.state.playerName);
    }
    drawCentredText(kit, SceneKit::Lower, 0x52, line, false, 0, 0x80, 0xfe);
    int blend = 0;
    auto textFrame = [&] {
        kit.lowerTextAlpha = blend;
        story.frame([] {});
    };
    for (int countdown = 4;;) {
        if (--countdown == 0) {
            countdown = 4;
            if (blend > 14) {
                break;
            }
            ++blend;
        }
        textFrame();
    }
    for (int i = 0; i < 0x3c; ++i) {
        story.poll();
        story.stall(1);
    }
    for (int countdown = 4;;) {
        if (--countdown == 0) {
            countdown = 4;
            if (blend < 1) {
                break;
            }
            --blend;
        }
        textFrame();
    }
    kit.clearText(SceneKit::Lower);
    kit.lowerTextAlpha = -1;
    story.frame([] {});
    for (int i = 0; i < 0x3c; ++i) {
        story.poll();
        story.stall(1);
    }
    // The five lines appear one by one (alpha 2..31, a step a frame; the
    // first line after three frames, the third after two); the narrator's
    // stream starts with the first.
    int shown = 0, alpha = 0;
    for (int countdown = 4;;) {
        if (--countdown == 0) {
            countdown = shown == 0 ? 3 : shown == 2 ? 2 : 4;
            if (alpha == 0 && shown == 0 && story.sound) {
                story.sound->playStream("SP_V_EP_001.SAD", 10, 11, 0x7f);
            }
            if (alpha < 0x1f) {
                ++alpha;
            } else {
                alpha = 0;
                ++shown;
            }
        }
        story.frame([&] {
            for (int i = 0; i < 5; ++i) {
                if (i == shown) {
                    if (alpha > 1) {
                        kit.draw(lines[i], alpha);
                    }
                    break;
                }
                kit.draw(lines[i], 0x1f);
            }
        });
        if (shown > 4) {
            for (int i = 0; i < 0x78; ++i) {
                story.poll();
                story.stall(1);
            }
            break;
        }
    }
    // 02035eb4.
    if (story.sound) {
        story.sound->closeStream();
    }
    fadeOut(kit, Screens::Both);
    kit.lowerTint = {};
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
    story.present();
}

// ---------------------------------------------------------------------------
// Movies.

void playMovie(Story& story, const MovieCues& cues, bool bothScreens) {
    SceneKit& kit = story.kit;
    const int number = story.state.movie;
    story.marker("movie");
    // The decoder's tables are data in the program (the same code as the
    // second adventure's movie overlay, its tables 0x60998 bytes lower: the
    // second game's 0x0205a120 is 0x020baab8 here).
    const Bytes arm9 = readFile(story.pack / "code/arm9.bin");
    constexpr std::size_t kTablesOrigin = 0x020baab8 - 0x02000000;
    const auto tables =
        mobiclip::Tables::fromOverlay(std::span<const std::uint8_t>(arm9).subspan(kTablesOrigin));
    const auto video = story.pack / "video";
    std::unique_ptr<MovieFrames> lower, upper;
    if (bothScreens) {
        upper = std::make_unique<MovieFrames>(video / ("emt" + std::to_string(number) + ".mods"),
                                              tables);
        lower = std::make_unique<MovieFrames>(video / ("emb" + std::to_string(number) + ".mods"),
                                              tables);
    } else {
        lower =
            std::make_unique<MovieFrames>(video / ("m" + std::to_string(number) + ".mods"), tables);
    }
    // 0201813c: the movie's sound (stream M<N>.SAD) when there is one.
    const std::string streamName = "M" + std::to_string(number) + ".SAD";
    if (story.sound && fileExists(story.pack / "stream" / streamName)) {
        story.sound->playStream(streamName, 10, 11, 0x7f);
    }
    // The captions (etext m<N>_<i>.txt), centred on y 0xab over the film.
    std::vector<std::string> captionText(cues.captions.size());
    for (std::size_t i = 0; i < cues.captions.size(); ++i) {
        captionText[i] = story.text("etext", "etext",
                                    "m" + std::to_string(number) + "_" +
                                        std::to_string(cues.captions[i].text) + ".txt");
    }
    const bool onUpper = cues.upper;
    // 02018fa4: the screens cleared and faded in (0200f7b4) before the film.
    kit.setBrightness(-16);
    kit.painter = [](Surface& image) {
        std::fill(image.pixels.begin(), image.pixels.end(), std::uint16_t(0x8000));
    };
    fadeIn(kit, Screens::Both);
    std::vector<bool> musicDone(cues.music.size(), false);
    std::uint64_t clock = 0;
    unsigned shown = 0;
    const unsigned count = lower->count;
    kit.painter = [&](Surface& image) {
        const Surface& frame = lower->frame(shown);
        auto copy = [&](const Surface& f, int top) {
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    image.pixels[std::size_t(y + top) * 256 + std::size_t(x)] =
                        f.pixels[std::size_t(y) * f.width + std::size_t(x)] | 0x8000;
                }
            }
        };
        copy(frame, onUpper ? 0 : 192);
        if (upper) {
            copy(upper->frame(std::min(shown, upper->count - 1)), 0);
        }
        // The caption due at this frame.
        const float seconds = float(shown) * float(lower->denominator) / float(lower->numerator);
        for (std::size_t i = 0; i < cues.captions.size(); ++i) {
            if (cues.captions[i].start <= seconds && seconds <= cues.captions[i].end) {
                Surface layer{256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
                drawCentredText(kit, layer, 0xab, captionText[i], false, 0, 0x80, 0xfe);
                for (std::size_t p = 0; p < layer.pixels.size(); ++p) {
                    if (layer.pixels[p] & 0x8000) {
                        image.pixels[p + (onUpper ? 0 : 256 * 192)] = layer.pixels[p];
                    }
                }
            }
        }
    };
    // The host's skip (system/game_types.hpp) ends any film at any time,
    // beside the game's own rules below.
    kit.skippable = true;

    struct NotSkippable {
        SceneKit& kit;

        ~NotSkippable() { kit.skippable = false; }
    } notSkippable{kit};

    for (;;) {
        // The film's alarm (its rate) picks the frame to show.
        clock += lower->numerator;
        const unsigned due = unsigned(clock / (std::uint64_t(60) << 24));
        // The player decodes five frames ahead and stops when the last one
        // is decoded (02018420), so the film's last five frames are not shown.
        if (due + 5 >= count) {
            break;
        }
        shown = due;
        // Music cues at their frame (0200530c, then 020052f4).
        for (std::size_t i = 0; i < cues.music.size(); ++i) {
            if (!musicDone[i] && cues.music[i].frame >= 0 && int(shown) >= cues.music[i].frame) {
                musicDone[i] = true;
                if (story.sound) {
                    if (cues.music[i].sequence >= 0) {
                        story.sound->playMusic(cues.music[i].sequence);
                    }
                    story.sound->moveMusicVolume(cues.music[i].volume, cues.music[i].frames);
                }
            }
        }
        story.poll();
        story.present();
        // 02018420: movie 0 can be skipped with a touch after 300 frames;
        // the theatre's replays (+0x11 == 0xf) with a touch (or A / B) at
        // any time, unless a save is being written (021ebb28; never here).
        if (number == 0 && shown > 300 && story.touch.released) {
            break;
        }
        if (story.state.returnScene == 0xf && story.touch.released) {
            break;
        }
        if (story.kit.hostInput().skip) {
            break;
        }
    }
    // 0200fdc8: both screens out over the last frame.
    fadeOut(kit, Screens::Both);
    kit.painter = nullptr;
    if (story.sound) {
        story.sound->closeStream();
    }
    kit.setBrightness(-16);
}

// ---------------------------------------------------------------------------
// The intro conversation.

namespace {
struct IntroTalk {
    Story& story;
    SceneKit& kit;
    Picture window, faceRight, faceLeft; // event_window_1, luke_face_new (+4), layton_face_new (+8)
    Picture pictures[8];                 // intro<set>_wani<k>_new
    SceneKit::SpriteObject cursor, plateLeft, plateRight;
    int set = 0, speaker = 0; // 0220e8e8 +0x10 / +4
    int previousLine = 0, previousSet = 0;
    bool faceRightShown = false, faceLeftShown = false;   // +0x61 / +0x62
    bool plateLeftShown = false, plateRightShown = false; // +0x54 / +0x55
    int voice = 0;                                        // +0x64: the next voice line
    std::string text;

    explicit IntroTalk(Story& s) : story(s), kit(s.kit) {
        // 0202b1d0.
        story.marker("intro-ctor");
        const auto start = kit.frames;
        story.poll();
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        kit.lowerOverlay.clear();
        story.present();
        // BG priorities (0202b1d0 writes BGxCNT): text 0, 3D 1, BG2 2, BG3 3.
        kit.lowerPriority = std::array<int, 4>{1, 0, 2, 3};
        window.load(kit, "event_window_1.spr");
        kit.select(window.sprite, "gfx");
        window.x = 2;
        window.y = 0x7f;
        faceLeft.load(kit, "layton_face_new.spr");
        faceRight.load(kit, "luke_face_new.spr");
        cursor = kit.load("cursor_wait.spr", 0xea, 0xad, 1);
        kit.select(cursor, "touch");
        cursor.x = 0xea;
        cursor.y = 0xad;
        static const char* const names[8] = {"intro1_wani1_new.spr", "intro2_wani1_new.spr",
                                             "intro2_wani2_new.spr", "intro3_wani1_new.spr",
                                             "intro3_wani2_new.spr", "intro3_wani3_new.spr",
                                             "intro4_wani1_new.spr", "intro4_wani2_new.spr"};
        static const int positions[8][2] = {{0x28, 0x14}, {0xce, 0},    {0x65, 0x12}, {0, 0xf},
                                            {0xd3, 0xf},  {0x66, 0x2b}, {0, 0},       {0x84, 6}};
        for (int i = 0; i < 8; ++i) {
            pictures[i].load(kit, names[i]);
            pictures[i].x = positions[i][0];
            pictures[i].y = positions[i][1];
        }
        plateRight = kit.load("?/luke_n.spr", 4, 0x7c, 1);
        plateRight.x = 4;
        plateRight.y = 0x7c;
        plateLeft = kit.load("?/layton_n.spr", 4, 0x7c, 1);
        plateLeft.x = 4;
        plateLeft.y = 0x7c;
        kit.select(window.sprite, 1u); // 0202bc60
        if (story.sound) {
            story.sound->stopMusicSoftly();
        }
        story.stall(0x1e);
        if (story.sound) {
            story.sound->freeLevel(0);
            story.sound->saveLevel(2);
            story.sound->setupScene(6, [this] { story.stall(1); });
        }
        // Measured (new game, ROM frames 3968..4029): 61 frames.
        story.loadingPause(start, 61);
    }

    // 0202b6c0 (the destructor; called explicitly: it presents a frame).
    void finish() {
        if (story.sound) {
            story.sound->closeStream();
            story.sound->setTypingEffect(0x29);
        }
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        kit.lowerOverlay.clear();
        kit.upperOverlay.clear();
        story.present();
    }

    // 0202bc88: faces, the set's pictures (the stream is fed elsewhere).
    void draw() {
        if (faceRightShown) {
            faceRight.draw(kit, 0x1f);
        }
        if (faceLeftShown) {
            faceLeft.draw(kit, 0x1f);
        }
        switch (set) {
        case 4:
            pictures[6].draw(kit, 0x1f);
            pictures[7].draw(kit, 0x1f);
            break;
        case 1:
            pictures[0].draw(kit, 0x1f);
            break;
        case 2:
            pictures[1].draw(kit, 0x1f);
            pictures[2].draw(kit, 0x1f);
            break;
        case 3:
            pictures[3].draw(kit, 0x1f);
            pictures[4].draw(kit, 0x1f);
            pictures[5].draw(kit, 0x1f);
            break;
        }
    }

    void plate(int alpha) {
        if (plateRightShown) {
            kit.draw(plateRight, alpha);
        } else if (plateLeftShown) {
            kit.draw(plateLeft, alpha);
        }
    }

    bool voicePlaying() const { return story.sound && story.sound->streamPlaying(); }

    // 0202b900: both faces quiet.
    void quiet() {
        const std::string name = "bg" + std::to_string(set) + "notalk";
        kit.select(faceRight.sprite, name);
        kit.select(faceLeft.sprite, name);
    }

    // 0202b98c: the faces' places for a set.
    void placeFaces() {
        faceLeftShown = set == 1 || set == 2 || set == 4;
        faceRightShown = set == 1 || set == 2;
        if (set == 1) {
            faceLeft.x = 0x39, faceLeft.y = 0x2b;
            faceRight.x = 0xb0, faceRight.y = 0x47;
        } else if (set == 2) {
            faceLeft.x = 0x2b, faceLeft.y = 0x19;
            faceRight.x = 0xa7, faceRight.y = 0x26;
        } else if (set == 4) {
            faceLeft.x = 0xa9, faceLeft.y = 0x3f;
        }
        quiet();
    }

    // 0202baa8: who speaks: the talking face, the name plate and the typing
    // tick (0x29 for one speaker, 0x2a for the other).
    void speak() {
        plateRightShown = plateLeftShown = false;
        int tick = 0x29;
        const std::string talk = "bg" + std::to_string(set) + "talk";
        const std::string quietName = "bg" + std::to_string(set) + "notalk";
        switch (set) {
        case 1:
        case 2:
            if (speaker != 'L') {
                kit.select(faceLeft.sprite, talk);
                kit.select(faceRight.sprite, quietName);
                plateLeftShown = true;
            } else {
                kit.select(faceRight.sprite, talk);
                kit.select(faceLeft.sprite, quietName);
                plateRightShown = true;
                tick = 0x2a;
            }
            break;
        case 3:
            if (speaker != 'R') {
                plateLeftShown = true;
            } else {
                plateRightShown = true;
                tick = 0x2a;
            }
            break;
        case 4:
            kit.select(faceLeft.sprite, talk);
            plateLeftShown = true;
            break;
        case 5:
            plateRightShown = true;
            tick = 0x2a;
            break;
        case 0x11:
        case 0x12:
            plateLeftShown = true;
            break;
        default:
            return;
        }
        if (story.sound) {
            story.sound->setTypingEffect(tick);
        }
    }

    // 0202b96c / 0202b054: the next voice line (SP_V_OP_1nn.SAD, nn from 01).
    void nextVoice() {
        char name[32];
        std::snprintf(name, sizeof name, "SP_V_OP_1%02d.SAD", voice + 1);
        ++voice;
        if (story.sound && fileExists(story.pack / "stream" / name)) {
            story.sound->playStream(name, 10, 11, 0x7f);
        }
    }

    bool touched() { return story.touch.released; }

    // 0202bdf0: one line, typed out with its voice; "@p" waits for a touch.
    void typeLine(int line) {
        text = story.text("itext", "itext", "t_" + std::to_string(line) + ".txt");
        if (story.sound) {
            story.sound->retryTyping();
        }
        speak();
        for (int i = 0; i < 3; ++i) {
            story.frame([&] { draw(); });
        }
        for (int a = 1; a < 0x1f; a += 2) {
            story.frame([&] {
                draw();
                window.draw(kit, a);
                plate(a);
            });
        }
        const std::size_t length = text.size();
        story.stall(1);
        nextVoice();
        story.stall(1);
        std::size_t i = 0;
        while (i < length) {
            story.poll();
            speak();
            draw();
            window.draw(kit, 0x1f);
            plate(0x1f);
            drawTalk(story, 0xe, 0x8d, text, i, i + 1);
            story.present();
            story.stall(1); // a VBlank more: two frames a character
            std::size_t j = i;
            if (touched()) {
                // The rest up to the next "@" at once.
                while (j < length && text[j] != '@') {
                    ++j;
                }
                story.frame([&] {
                    draw();
                    window.draw(kit, 0x1f);
                    plate(0x1f);
                    drawTalk(story, 0xe, 0x8d, text, i, j + 1);
                });
                i = j;
                if (j == length) {
                    break;
                }
            }
            if (j + 1 < length && text[j] == '@' && text[j + 1] == 'p') {
                if (!voicePlaying()) {
                    quiet();
                }
                if (story.sound) {
                    story.sound->retryTyping();
                }
                for (int k = 2;;) {
                    story.frame([&] {
                        draw();
                        window.draw(kit, 0x1f);
                        kit.draw(cursor, k);
                        plate(0x1f);
                    });
                    if (touched()) {
                        break;
                    }
                    if (k < 0x1f) {
                        ++k;
                    }
                    if (!voicePlaying()) {
                        quiet();
                    }
                }
                speak();
                if (story.sound) {
                    story.sound->effect(9);
                }
                story.stall(4);
                nextVoice();
                story.stall(1);
            }
            i = j + 1;
        }
        if (story.sound) {
            story.sound->retryTyping();
        }
        if (!voicePlaying()) {
            quiet();
        }
        for (int k = 2;;) {
            story.frame([&] {
                draw();
                window.draw(kit, 0x1f);
                kit.draw(cursor, k);
                plate(0x1f);
            });
            if (touched()) {
                break;
            }
            if (k < 0x1f) {
                ++k;
            }
            if (!voicePlaying()) {
                quiet();
            }
        }
        if (story.sound) {
            story.sound->effect(9);
        }
        quiet();
        kit.clearText(SceneKit::Lower);
        for (int a = 0x1f; a > 1; a -= 2) {
            story.frame([&] {
                draw();
                window.draw(kit, a);
                plate(a);
            });
        }
        story.frame([&] { draw(); });
        story.stall(4);
    }

    // 0202c49c: one step: the picture set, then the line (0: the picture
    // alone for 1.5 x `frames` frames or until a touch).
    void step(int newSet, int line, int who, int frames) {
        set = newSet;
        speaker = who;
        placeFaces();
        const int shownFrames = int(1.5f * float(frames));
        const std::string picture = "ibg_" + std::to_string(newSet);
        if (line == 0) {
            if (previousLine != 0) {
                fadeOut(kit, Screens::Lower);
            }
            story.poll();
            kit.lowerBackground = picture;
            draw();
            story.present();
            fadeIn(kit, Screens::Lower);
            for (int i = 0; i < shownFrames; ++i) {
                story.frame([&] { draw(); });
                if (touched()) {
                    break;
                }
            }
            fadeOut(kit, Screens::Lower);
        } else {
            story.poll();
            kit.lowerBackground = picture;
            draw();
            story.present();
            kit.select(window.sprite, who == 'R' ? 2u : who == 'L' ? 1u : 3u);
            if (newSet == 0x11 && story.sound) {
                story.sound->stopRoomLoop(0x1e);
            }
            if (previousLine == 0) {
                story.frame([&] { draw(); });
                fadeIn(kit, Screens::Lower);
            }
            if ((newSet == 0x11 || newSet == 0x12) && story.sound) {
                story.sound->effect(0x2d0);
            }
            typeLine(line);
        }
        previousLine = line;
        previousSet = newSet;
    }

    // 0202c780: the car shakes (the lower BG moved by -1..1 for 20 frames).
    void shake() {
        fadeOut(kit, Screens::Lower);
        story.poll();
        kit.lowerBackground = "ibg_5";
        story.present();
        fadeIn(kit, Screens::Lower, 4);
        if (story.sound) {
            story.sound->effect(0x2bf);
        }
        kit.lowerShake = 20;
        story.stall(20);
        kit.lowerShake = 0;
    }

    void firstPart() {
        // 0202c910.
        story.marker("intro-run");
        if (story.sound) {
            story.sound->startRoomLoop();
        }
        story.stall(0x28);
        kit.upperOverlay.clear();
        kit.upperBackground = "i_car4";
        story.present();
        fadeIn(kit, Screens::Upper);
        story.stall(0x78);
        step(2, 1, 'L', 0x32);
        step(2, 2, 'R', 0x32);
        step(0x11, 3, '1', 0x46);
        step(0x12, 4, '1', 0x78);
        if (story.sound) {
            story.sound->startRoomLoop();
        }
        fadeOut(kit, Screens::Upper);
        kit.upperBackground = "i_car5";
        story.present();
        fadeIn(kit, Screens::Upper);
        story.stall(0x14);
        static const int lines[][3] = {
            {4, 5, 'L'},    {3, 6, 'L'},    {3, 7, 'R'},   {3, 8, 'L'},    {2, 9, 'L'},
            {2, 10, 'R'},   {2, 0xb, 'L'},  {2, 0xc, 'R'}, {3, 0xd, 'R'},  {3, 0xe, 'L'},
            {3, 0x26, 'R'}, {3, 0x27, 'L'}, {3, 0xf, 'R'}, {1, 0x10, 'R'}, {1, 0x11, 'L'},
        };
        for (std::size_t i = 0; i < std::size(lines); ++i) {
            step(lines[i][0], lines[i][1], lines[i][2], i == 0 ? 100 : 0x3c);
        }
        shake();
        step(5, 0x25, '1', 0x3c);
        step(4, 0x12, 'L', 0x3c);
        step(2, 0x13, 'L', 0x3c);
        step(2, 0x14, 'R', 0x3c);
        fadeOut(kit, Screens::Both);
        if (story.sound) {
            story.sound->stopRoomLoop(0x69);
        }
    }

    void secondPart() {
        // 0202ccb4.
        story.marker("intro-run");
        voice = 0x24;
        if (story.sound) {
            story.sound->startRoomLoop();
        }
        story.stall(0x2d);
        kit.upperOverlay.clear();
        kit.upperBackground = "i_car4";
        story.present();
        fadeInUpperWaiting(story);
        story.stall(0x14);
        step(2, 0x15, 'L', 0x3c);
        step(2, 0x16, 'R', 100);
        step(1, 0x17, 'L', 100);
        step(1, 0x18, 'R', 0x32);
        step(2, 0x19, 'R', 100);
        if (story.sound) {
            story.sound->fadeOutMusic();
        }
        fadeOut(kit, Screens::Both);
        if (story.sound) {
            story.sound->stopRoomLoop(0x69);
        }
        story.stall(0x2d);
    }

    // 0200f8a0: the upper screen in over VBlank waits.
    void fadeInUpperWaiting(Story& s) {
        for (int i = -16; i <= 0; ++i) {
            s.stall(1);
            kit.setUpperBrightness(std::min(0, kit.upperBrightness() + 1));
            if (kit.upperBrightness() == 0) {
                return;
            }
        }
    }
};
} // namespace

void introTalk(Story& story, int part) {
    IntroTalk talk(story);
    if (part == 0) {
        talk.firstPart();
    } else {
        talk.secondPart();
    }
    talk.finish();
}

// ---------------------------------------------------------------------------
// The title logo.

void titleLogo(Story& story) {
    SceneKit& kit = story.kit;
    // 0201bd50: lower and upper pictures; the "touch" prompt (title.spr),
    // the logo as a 2D sprite on the upper screen and as a 3D sprite on the
    // lower one, all centred.
    kit.clearText(SceneKit::Upper);
    kit.lowerOverlay.clear();
    kit.upperOverlay.clear();
    kit.lowerBackground = "title_bottom";
    kit.upperBackground = "title_top";
    auto prompt = kit.load("?/title.spr", 0, 0, 1);
    prompt.x = (0x100 - kit.frameWidth(prompt)) >> 1;
    prompt.y = (0xc0 - kit.frameHeight(prompt)) >> 1;
    auto upperLogo = kit.load("?/title_logo.sbj", 0, 0, 1);
    kit.select(upperLogo, "logo");
    upperLogo.x = (0x100 - kit.frameWidth(upperLogo)) >> 1;
    upperLogo.y = (0xc0 - kit.frameHeight(upperLogo)) >> 1;
    auto lowerLogo = kit.load("?/title_logo.spr", 0, 0, 1);
    kit.select(lowerLogo, "logo");
    lowerLogo.x = (0x100 - kit.frameWidth(lowerLogo)) >> 1;
    lowerLogo.y = (0xc0 - kit.frameHeight(lowerLogo)) >> 1;
    // 0201c108.
    story.marker("titlelogo-run");
    story.stall(0x3c);
    if (story.sound) {
        story.sound->setupScene(7, [&] { story.stall(1); });
    }
    story.frame([] {});
    if (story.sound) {
        story.sound->playStream("BGM_09.SAD", 10, 11, 0x7f); // 0201bf9c
    }
    fadeIn(kit, Screens::Both); // 0200f7b4(1, 2)
    const int finalY = upperLogo.y;
    const int lowerStartY = lowerLogo.y;
    // The upper logo shows only below its final top (WIN0 from there down,
    // the sprites only inside).
    auto& win = kit.windows[SceneKit::Upper];
    win.enabled[0] = true;
    win.x1[0] = 0;
    win.x2[0] = 0xff;
    win.y1[0] = finalY;
    win.y2[0] = 0xbf;
    win.inside[0] = 0x3c;
    win.outside = 0x2c;
    int alpha = 0, delay = 10, arrived = 0;
    int upperY = finalY + 0xbf, lowerY = lowerStartY;
    unsigned counter = 0;
    for (;;) {
        story.poll();
        ++counter;
        if (counter > 0x3c && alpha < 0x1f && counter % 5 == 1) {
            ++alpha;
        }
        if (upperY == finalY) {
            upperLogo.y = upperY;
            kit.draw(upperLogo);
        } else {
            if (upperY < 0xc0) {
                upperLogo.y = upperY;
                kit.draw(upperLogo);
            }
            if (lowerY > -kit.frameHeight(lowerLogo) && alpha > 0) {
                lowerLogo.y = lowerY;
                kit.draw(lowerLogo, alpha);
            }
        }
        if (alpha == 0x1f && delay > 0) {
            --delay;
        }
        if (delay == 0) {
            upperY = std::max(finalY, upperY - 3);
            lowerY = std::max(-100, lowerY - 3);
        }
        if (upperY == finalY && arrived < 1000) {
            ++arrived;
        }
        // The prompt blinks once the logo has been in place a second.
        if (arrived > 0x3c && (counter * 2 - 1) % 60 > 0x14) {
            kit.draw(prompt, 0x1f);
        }
        ++counter;
        story.present();
        if (story.touch.released) {
            break;
        }
    }
    if (story.sound) {
        story.sound->effect(2);
    }
    upperLogo.y = finalY;
    for (int i = 0; i < 10; ++i) {
        story.frame([&] { kit.draw(upperLogo); });
    }
    // The stream's volume falls from 0x68 to 0 while the screens darken.
    for (int volume = 0x68, level = 0; volume >= 0; --volume) {
        if (story.sound) {
            story.sound->setStreamVolume(volume);
        }
        kit.setBrightness(level);
        story.stall(1);
        if (level > -16) {
            --level;
        }
    }
    story.stall(5);
    if (story.sound) {
        story.sound->closeStream();
    }
    fadeOut(kit, Screens::Both);
    win = SceneKit::Windows{};
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
    kit.lowerOverlay.clear();
    kit.upperOverlay.clear();
    kit.clearText(SceneKit::Upper);
}

} // namespace picarat::adventure1
