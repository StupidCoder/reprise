// The first adventure's bonus section. See a1_bonus.hpp.
#include "menus/a1_bonus.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_ui.hpp"
#include "system/console_id.hpp"

namespace picarat::adventure1 {

namespace {

// The pages' common frame: the display set afresh (0200432c, 02004254, a
// frame), the lower picture (02009d80) and the upper one (0200a4e0), a
// frame; at the end (the destructors) faded out to black and cleared the
// same way.
struct Page {
    Story& story;
    SceneKit& kit;

    explicit Page(Story& s) : story(s), kit(s.kit) {}

    std::string picture(const std::string& romName) const {
        return kit.localized(pictureName(romName));
    }

    void open(const std::string& lower, const std::string& upper) {
        clear();
        story.present();
        kit.lowerBackground = picture(lower);
        kit.upperBackground = picture(upper);
        story.present();
    }

    void clear() {
        kit.lowerPriority.reset();
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        kit.lowerOverlay.clear();
        kit.upperOverlay.clear();
    }

    // 0200fce8(1, 1) and the display cleared (0200432c, 02004254, a frame).
    void close() {
        fadeOut(kit, Screens::Both);
        clear();
        story.present();
    }

    // The pages' first frame and fade (0200f7b4(1, 1)).
    template <class Draw>
    void enter(Draw draw) {
        story.frame(draw);
        fadeIn(kit, Screens::Both);
    }

    void setupSounds(int kind) {
        if (story.sound) {
            story.sound->setupScene(kind, [this] { story.stall(1); });
        }
    }

    // A text of a pack archive with the original's fallback when missing.
    std::string text(const std::string& folder, const std::string& name,
                     const std::string& missing) const {
        std::string t = story.text(folder, folder, name);
        return t.empty() ? missing : t;
    }
};

// 02050f1c with the press offset (1, 1), shown while idle (+0x1b): the
// pages' picture buttons.
void pictureButton(SceneKit& kit, Button& b, int x, int y, const std::string& sprite,
                   const std::string& animation, int sound) {
    b.setSprite(kit, x, y, sprite, animation);
    b.visibleIdle = true;
    b.pressDx = b.pressDy = 1;
    b.sound = sound;
}

// The same with a numbered animation (0200c6d0 after 02050f1c).
void numberedButton(SceneKit& kit, Button& b, int x, int y, const std::string& sprite,
                    unsigned animation, int sound) {
    pictureButton(kit, b, x, y, sprite, {}, sound);
    kit.select(b.sprite, animation);
}

// Two digits of `sprite` (frames 1..10 for 0..9) at (x, y) and (x + 6, y):
// the art gallery's and the WiFi list's numbers (02030530).
void drawTwoDigits(SceneKit& kit, SceneKit::SpriteObject& sprite, int x, int y, int value) {
    const int v = value % 100;
    sprite.x = x;
    sprite.y = y;
    kit.select(sprite, unsigned(v / 10 + 1));
    kit.draw(sprite);
    sprite.x = x + 6;
    sprite.y = y;
    kit.select(sprite, unsigned(value % 10 + 1));
    kit.draw(sprite);
}

// --------------------------------------------------------------------------
// 0x24: the secret menu.

struct SecretMenu : Page {
    SpriteButton back; // +0
    Button choices[4]; // +4..+0x10: WiFi, puzzle index, challenges, extras

    explicit SecretMenu(Story& s) : Page(s) {
        // 0203b86c.
        open("secretmenu_bg.bgx", "?/secretmenu_sub.bgx");
        back.load(kit, "?/load_btn.spr", 4);
        const int xs[] = {10, 0x84, 10, 0x84}, ys[] = {0x22, 0x22, 0x56, 0x56};
        for (int i = 0; i < 4; ++i) {
            pictureButton(kit, choices[i], xs[i], ys[i], "?/secretmenu_btn.spr",
                          std::to_string(i + 1), 3);
        }
        setupSounds(3);
        if (story.sound) {
            story.sound->loadMusic(2, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
    }

    // 0203bbb0: the challenges once flag 506 is set, the extras with 512.
    void draw() {
        back.update(kit, story.touch);
        choices[0].update(kit, story.touch);
        choices[1].update(kit, story.touch);
        if (story.state.flag(506)) {
            choices[2].update(kit, story.touch);
        }
        if (story.state.flag(512)) {
            choices[3].update(kit, story.touch);
        }
    }

    void run() {
        // 0203bc20.
        enter([this] { draw(); });
        GameState& state = story.state;
        for (;;) {
            story.frame([this] { draw(); });
            if (back.clicked()) {
                state.scene = scene::TitleScreen;
                return;
            }
            if (choices[0].clicked(kit, story.touch)) {
                state.scene = 0x1d;
                return;
            }
            if (choices[1].clicked(kit, story.touch)) {
                state.scene = scene::EventThenQuestion; // 0x27: the index
                return;
            }
            if (state.flag(506) && choices[2].clicked(kit, story.touch)) {
                state.scene = scene::Challenge;
                return;
            }
            if (state.flag(512) && choices[3].clicked(kit, story.touch)) {
                state.scene = 0xd;
                return;
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x0d: the extras.

struct Extras : Page {
    // +0..+0x14: character files, art, music, voices, movies, passcode;
    // +0x18 back.
    Button pages[6];
    SpriteButton back;

    // 0202ef1c: the first five open with enough picarats (+0x640).
    static constexpr int kPicarats[5] = {2500, 2700, 2900, 3100, 3500};

    bool open_(int i) const { return int(story.state.puzzle.score) >= kPicarats[i]; }

    explicit Extras(Story& s) : Page(s) {
        // 0202ea30.
        open("omake_mode.bgx", "?/omake_mode_sub.bgx");
        const int xs[] = {10, 0x5c, 0xad, 10, 0x5c, 0xad}, ys[] = {6, 6, 6, 0x59, 0x59, 0x59};
        for (int i = 0; i < 6; ++i) {
            // The passcode's button changes once its event was seen.
            const unsigned animation =
                i == 5 && (story.state.events[0x103] & 1) ? 7u : unsigned(i + 1);
            numberedButton(kit, pages[i], xs[i], ys[i], "?/omake_mode.spr", animation, 0xb);
        }
        back.load(kit, "?/load_btn.spr", 4);
        setupSounds(3);
        if (story.sound) {
            story.sound->loadMusic(2, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
    }

    void draw() {
        for (int i = 0; i < 5; ++i) {
            if (open_(i)) {
                pages[i].update(kit, story.touch);
            }
        }
        pages[5].update(kit, story.touch);
        back.update(kit, story.touch);
    }

    void run() {
        // 0202eff4: character files 0xe, art 0x11, music 0x12, voices 0x13,
        // movies 0xf, passcode 0x25; back to the secret menu.
        enter([this] { draw(); });
        static constexpr std::uint8_t kScenes[6] = {0xe, 0x11, 0x12, 0x13, 0xf, 0x25};
        for (;;) {
            story.frame([this] { draw(); });
            for (int i = 0; i < 6; ++i) {
                if ((i == 5 || open_(i)) && pages[i].clicked(kit, story.touch)) {
                    story.state.scene = kScenes[i];
                    return;
                }
            }
            if (back.clicked()) {
                story.state.scene = scene::Continue;
                return;
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x0e: the character files: 40 characters on two pages of 20.

struct CharacterFiles : Page {
    Button people[2][20];  // +0 / +0x50: chr_file1.spr / chr_file2.spr
    Button previous, next; // +0xa4 / +0xa0
    SpriteButton back;     // +0xa8
    int page = 0;          // +0xac

    explicit CharacterFiles(Story& s) : Page(s) {
        // 0202f188.
        open("omake_chrmode.bgx", "?/omake_chrmode_sub.bgx");
        auto arrow = [&](Button& b, int x, const char* idle, const char* pressed) {
            b.setSprite(kit, x, 0xae, "?/omake_btn.spr", idle);
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 0;
            b.setPressedAnimation(kit, pressed);
            b.sound = 0x14;
        };
        arrow(previous, 5, "mae_off", "mae_on");
        arrow(next, 0x66, "next_off", "next_on");
        back.load(kit, "?/load_btn.spr", 4);
        for (int i = 0; i < 20; ++i) {
            const int x = 4 + 0x32 * (i % 5), y = 0x18 + 0x25 * (i / 5);
            numberedButton(kit, people[0][i], x, y, "chr_file1.spr", unsigned(i + 1), 3);
            numberedButton(kit, people[1][i], x, y, "chr_file2.spr", unsigned(i + 1), 3);
        }
        setupSounds(3);
    }

    // 0202f778.
    void draw() {
        (page == 0 ? next : previous).update(kit, story.touch);
        back.update(kit, story.touch);
        for (auto& b : people[page]) {
            b.update(kit, story.touch);
        }
    }

    // 0202f588: character n (1..40): the portrait (chr_data<n>, upper BG2)
    // over chr_sub, the name (otext n_<n-1>.txt) centred on (0x49, 0xe)
    // and the notes (chr<n>.txt) from (6, 0x24).
    void show(int n) {
        kit.upperBackground = picture("chr_sub.bgx");
        kit.upperOverlay = picture("chr_data" + std::to_string(n) + ".bgx");
        const std::string name = text("otext", "n_" + std::to_string(n - 1) + ".txt",
                                      "missing n_" + std::to_string(n) + ".txt");
        const std::string notes = text("otext", "chr" + std::to_string(n) + ".txt",
                                       "missing chr" + std::to_string(n) + ".txt");
        kit.clearText(SceneKit::Upper);
        drawCentredLine(kit, SceneKit::Upper, 0xe, name, 0x49);
        drawTextBox(kit, SceneKit::Upper, 6, 0x24, notes);
    }

    void run() {
        // 0202f7f4.
        enter([this] { draw(); });
        for (;;) {
            story.frame([this] { draw(); });
            if (page == 0 && next.clicked(kit, story.touch)) {
                page = 1;
            } else if (page == 1 && previous.clicked(kit, story.touch)) {
                page = 0;
            }
            for (int i = 0; i < 20; ++i) {
                if (people[page][i].clicked(kit, story.touch)) {
                    show(page * 20 + i + 1);
                }
            }
            if (back.clicked()) {
                story.state.scene = 0xd; // the router's 02002104
                return;
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x0f: the movie theatre: 16 movies.

struct MovieTheatre : Page {
    SpriteButton back; // +0
    Button movies[16]; // +4..

    explicit MovieTheatre(Story& s) : Page(s) {
        // 0202f944.
        open("omake_moviemode.bgx", "?/omake_moviemode_sub.bgx");
        back.load(kit, "?/load_btn.spr", 4);
        for (int i = 0; i < 16; ++i) {
            numberedButton(kit, movies[i], 8 + 0x3e * (i % 4), 0x14 + 0x27 * (i / 4),
                           "movie_file1.spr", unsigned(i + 1), 3);
        }
        setupSounds(3);
        if (story.sound) {
            story.sound->loadMusic(2, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
    }

    // 0202fc6c.
    void draw() {
        back.update(kit, story.touch);
        for (auto& b : movies) {
            b.update(kit, story.touch);
        }
    }

    // 0202fb80: movie n's script (event/m<n>.gds) describes it; it plays
    // as an event movie (scene 0x22) and returns here.
    void play(int n) {
        story.movieCues = MovieCues{};
        const std::string script = "script/event/m" + std::to_string(n) + ".gds";
        if (fileExists(story.pack / script)) {
            story.runScript(script, story.commonCommands());
        }
        story.state.movie = n;
        story.state.scene = scene::EventMovie;
        story.state.returnScene = 0xf;
        if (story.sound) {
            story.sound->fadeOutMusicSlowly(); // 020051a4 (its count 10 is not used)
        }
        story.stall(10);
        if (story.sound) {
            story.sound->stopMusicSoftly();   // 02005354
            story.sound->forgetLoadedMusic(); // 02004d80
        }
    }

    void run() {
        // 0202fca0.
        enter([this] { draw(); });
        for (;;) {
            story.frame([this] { draw(); });
            for (int i = 0; i < 16; ++i) {
                if (movies[i].clicked(kit, story.touch)) {
                    play(i + 1);
                    return;
                }
            }
            if (back.clicked()) {
                story.state.scene = 0xd;
                return;
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x11: the art gallery: 20 pictures of the story, turned with arrows.

struct ArtGallery : Page {
    SceneKit::SpriteObject digits; // +0: artmode_num.spr
    SpriteButton back;             // +4
    Button previous, next;         // +8 / +0xc
    int shown = 1;                 // +0x10

    explicit ArtGallery(Story& s) : Page(s) {
        // 02030270.
        clear();
        story.present();
        kit.lowerBackground = picture("artmode_bg.bgx");
        showPicture(shown);
        story.present();
        back.load(kit, "?/load_btn.spr", 4);
        pictureButton(kit, previous, 0x37, 0x91, "?/artmode_btn.spr", "mae", 0x14);
        pictureButton(kit, next, 0x89, 0x91, "?/artmode_btn.spr", "next", 0x14);
        digits = kit.load("artmode_num.spr", 0, 0, 1);
        setupSounds(3);
    }

    // 020305e4: picture n (1..20) on the upper screen (table 020d4da4 of
    // event pictures; 120 is the language's own), its title (art_t<n>,
    // centred at row 0x24) and notes (art_d<n>, from (0xe, 0x40)) below.
    void showPicture(int n) {
        static constexpr int kPictures[20] = {114, 115, 180, 147, 113, 117, 116, 143, 154, 132,
                                              120, 121, 131, 188, 125, 163, 123, 186, 181, 182};
        const int number = kPictures[n - 1];
        kit.upperBackground = number == 0x78 ? picture("?/ebg_120.bgx")
                                             : picture("ebg_" + std::to_string(number) + ".bgx");
        const std::string title =
            text("otext", "art_t" + std::to_string(n) + ".txt", "NO TITLE : " + std::to_string(n));
        const std::string notes =
            text("otext", "art_d" + std::to_string(n) + ".txt", "NO DATA : " + std::to_string(n));
        kit.clearText(SceneKit::Lower);
        drawCentredLine(kit, SceneKit::Lower, 0x24, title, 0x80);
        drawEventText(kit, SceneKit::Lower, 0xe, 0x40, notes);
    }

    // 020307dc: the buttons and the page number at (9, 0x1f).
    void draw() {
        back.update(kit, story.touch);
        previous.update(kit, story.touch);
        next.update(kit, story.touch);
        drawTwoDigits(kit, digits, 9, 0x1f, shown);
    }

    void run() {
        // 02030814.
        enter([this] { draw(); });
        for (;;) {
            story.frame([this] { draw(); });
            if (back.clicked()) {
                story.state.scene = 0xd;
                return;
            }
            if (previous.clicked(kit, story.touch)) {
                shown = shown - 1 < 1 ? 20 : shown - 1;
                showPicture(shown);
            }
            if (next.clicked(kit, story.touch)) {
                shown = shown + 1 > 20 ? 1 : shown + 1;
                showPicture(shown);
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x12: the music room: 19 pieces; the first is the ending theme's stream.

struct MusicRoom : Page {
    SpriteButton back;             // +0
    SceneKit::SpriteObject number; // +4: omake_music_num.sbj (upper)
    Button pieces[19];             // +8..
    int playing = -1;              // +0x54

    static constexpr const std::uint8_t* kSequences = kMusicRoomSequences; // 020d4da4

    explicit MusicRoom(Story& s) : Page(s) {
        // 0203090c: the music fades and stops.
        // (020051a4 ignores its count: the slow fade, 100 frames.)
        if (story.sound) {
            story.sound->fadeOutMusicSlowly();
        }
        story.stall(0x1e);
        if (story.sound) {
            story.sound->stopMusicSoftly();
        }
        open("musicmode_bg.bgx", "?/omake_musicmode_sub.bgx");
        back.load(kit, "?/load_btn.spr", 4);
        number = kit.load("omake_music_num.sbj", 6, 6, 1);
        number.x = number.y = 6;
        for (int i = 0; i < 19; ++i) {
            const int row = i / 5, column = i % 5;
            const int x = row == 3 ? 0x1d + 0x32 * column : 4 + 0x32 * column;
            numberedButton(kit, pieces[i], x, 0x16 + 0x26 * row, "omake_music.spr", unsigned(i + 1),
                           0x17);
        }
        setupSounds(3);
        if (story.sound) {
            story.sound->loadMusic(1, [this] { story.stall(1); });
        }
    }

    // 0203113c.
    void draw() {
        if (playing != -1) {
            kit.select(number, unsigned(playing));
            kit.draw(number);
        }
        back.update(kit, story.touch);
        for (auto& b : pieces) {
            b.update(kit, story.touch);
        }
    }

    // 02030e38: the ending theme's stream fades out over 27 frames (its
    // volume 0x68 down by 4 a frame) and closes.
    void closeStream() {
        if (playing != 1 || !story.sound) {
            return;
        }
        for (int volume = 0x68; volume >= 0; volume -= 4) {
            story.sound->setStreamVolume(volume);
            story.frame([this] { draw(); });
        }
        story.sound->closeStream();
    }

    // 02030f3c: piece n (1..19): its title (music<n>.txt) centred at row
    // 0x97 of the upper screen over musicmode_sub; piece 1 streams
    // BGM_09.SAD, the others play their sequence.
    void play(int n) {
        if (playing == 1) {
            closeStream();
            story.stall(0x3c);
        } else {
            if (story.sound) {
                story.sound->fadeOutMusic30(); // 020051dc
            }
            story.stall(0x20);
        }
        if (n == 1) {
            if (playing != 1) {
                if (story.sound) {
                    story.sound->stopMusic(1);
                }
                story.stall(2);
            }
            story.stall(0x14);
            if (story.sound) {
                story.sound->playStream("BGM_09.SAD", 10, 0xb, 0x7f);
            }
        } else {
            if (story.sound) {
                story.sound->stopMusic(1);
            }
            story.stall(2);
            if (story.sound) {
                story.sound->forgetLoadedMusic();
                story.sound->loadMusic(kSequences[n - 1], [this] { story.stall(1); });
                story.sound->setupScene(3, [this] { story.stall(1); });
                story.sound->playMusic(kSequences[n - 1]);
            }
        }
        // The picture and title are set before the waits above in the
        // original, but its waits present nothing: they show with the next
        // frame, as here.
        kit.upperBackground = picture("musicmode_sub.bgx");
        const std::string title = text("otext", "music" + std::to_string(n) + ".txt",
                                       "missing music" + std::to_string(n) + ".txt");
        kit.clearText(SceneKit::Upper);
        drawCentredLine(kit, SceneKit::Upper, 0x97, title, 0x80);
        playing = n;
    }

    void run() {
        // 02031194.
        enter([this] { draw(); });
        for (;;) {
            story.frame([this] { draw(); });
            if (back.clicked()) {
                story.state.scene = 0xd;
                closeStream(); // 02030ccc
                return;
            }
            for (int i = 0; i < 19; ++i) {
                if (pieces[i].clicked(kit, story.touch)) {
                    play(i + 1);
                }
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x13: the voice room: 14 lines of the young assistant, 27 of the professor.

struct VoiceRoom : Page {
    SpriteButton back;           // +0
    Button assistant, professor; // +4 / +8: play the line chosen
    Button professorPrevious, assistantPrevious, professorNext, assistantNext; // +0xc..+0x18
    int assistantLine = 1, professorLine = 1;                                  // +0x1c / +0x20
    bool secondPicture = false;                                                // +0x2c

    // 020d4e84 / 020d4ebc: the lines' stream numbers (SP_V_OP_1<n - 20>).
    static constexpr int kAssistant[14] = {21, 32, 35, 36, 39, 41, 42, 46, 48, 50, 51, 55, 57, 59};
    static constexpr int kProfessor[27] = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 37, 38,
                                           40, 43, 44, 45, 47, 49, 52, 53, 54, 56, 58, 60, 61};

    explicit VoiceRoom(Story& s) : Page(s) {
        // 02031250: the music fades (020051a4: the slow fade whatever the
        // count), then its memory goes.
        if (story.sound) {
            story.sound->fadeOutMusicSlowly();
        }
        story.stall(0x1e);
        if (story.sound) {
            story.sound->stopMusicSoftly();
            story.sound->forgetLoadedMusic();
            story.sound->freeLevel(0);
            story.sound->saveLevel(2);
        }
        open("voicemode_bg.bgx", "?/voicemode_sub.bgx");
        back.load(kit, "?/load_btn.spr", 4);
        pictureButton(kit, assistant, 0xa3, 0x5a, "?/voicemode_btn.spr", "luke", 0);
        pictureButton(kit, professor, 0x24, 0x5a, "?/voicemode_btn.spr", "layton", 0);
        pictureButton(kit, professorPrevious, 0xb, 0x98, "?/voicemode_btn.spr", "mae", 0x14);
        pictureButton(kit, assistantPrevious, 0x8a, 0x98, "?/voicemode_btn.spr", "mae", 0x14);
        pictureButton(kit, professorNext, 0x43, 0x98, "?/voicemode_btn.spr", "next", 0x14);
        pictureButton(kit, assistantNext, 0xc2, 0x98, "?/voicemode_btn.spr", "next", 0x14);
        setupSounds(3);
    }

    // 020316f4: the lines' names (laytonv<n> from (0x10, 0x22), lukev<n>
    // from (0x8f, 0x22)).
    void writeNames() {
        kit.clearText(SceneKit::Lower);
        drawEventText(kit, SceneKit::Lower, 0x10, 0x22,
                      text("otext", "laytonv" + std::to_string(professorLine) + ".txt",
                           "missing laytonv" + std::to_string(professorLine) + ".txt"));
        drawEventText(kit, SceneKit::Lower, 0x8f, 0x22,
                      text("otext", "lukev" + std::to_string(assistantLine) + ".txt",
                           "missing lukev" + std::to_string(assistantLine) + ".txt"));
    }

    // 0203188c: the line's words centred on row 0x56 of the upper screen.
    void writeWords(bool professorsLine) {
        const std::string name = professorsLine ? "laytonvs" + std::to_string(professorLine)
                                                : "lukevs" + std::to_string(assistantLine);
        kit.clearText(SceneKit::Upper);
        drawCentredText(kit, SceneKit::Upper, 0x56,
                        text("otext", name + ".txt", "missing " + name + ".txt"));
    }

    // 02031a00 / 02031a78: the stream on channels 10 and 11 (its volume:
    // the program's stream volume, 0x7f here: hypothesis).
    void speak(int stream) {
        if (!story.sound) {
            return;
        }
        const int n = stream - 0x14;
        story.sound->closeStream();
        story.sound->playStream(std::string("SP_V_OP_1") + (n < 10 ? "0" : "") + std::to_string(n) +
                                    ".SAD",
                                10, 0xb, 0x7f);
    }

    // 020319ac.
    void draw() {
        back.update(kit, story.touch);
        assistant.update(kit, story.touch);
        professor.update(kit, story.touch);
        professorPrevious.update(kit, story.touch);
        assistantPrevious.update(kit, story.touch);
        professorNext.update(kit, story.touch);
        assistantNext.update(kit, story.touch);
    }

    void run() {
        // 02031af0.
        writeNames();
        enter([this] { draw(); });
        for (;;) {
            story.frame([this] { draw(); });
            if (back.clicked()) {
                story.state.scene = 0xd;
                if (story.sound) {
                    story.sound->closeStream(); // 020315cc
                }
                return;
            }
            bool spoke = false;
            if (assistant.clicked(kit, story.touch)) {
                speak(kAssistant[assistantLine - 1]);
                writeWords(false);
                spoke = true;
            }
            if (professor.clicked(kit, story.touch)) {
                speak(kProfessor[professorLine - 1]);
                writeWords(true);
                spoke = true;
            }
            if (professorPrevious.clicked(kit, story.touch)) {
                professorLine = professorLine - 1 == 0 ? 27 : professorLine - 1;
                writeNames();
            }
            if (assistantPrevious.clicked(kit, story.touch)) {
                assistantLine = assistantLine - 1 == 0 ? 14 : assistantLine - 1;
                writeNames();
            }
            if (professorNext.clicked(kit, story.touch)) {
                professorLine = professorLine + 1 > 27 ? 1 : professorLine + 1;
                writeNames();
            }
            if (assistantNext.clicked(kit, story.touch)) {
                assistantLine = assistantLine + 1 > 14 ? 1 : assistantLine + 1;
                writeNames();
            }
            // The upper picture changes with the first line heard.
            if (spoke && !secondPicture) {
                kit.upperBackground = picture("voicemode_sub2.bgx");
                secondPicture = true;
            }
        }
    }
};

// --------------------------------------------------------------------------
// 0x14: the passcode's notes (passcode_bg / passcode_sub, a "back" button).

struct PasscodeNotes : Page {
    Button back; // +0

    explicit PasscodeNotes(Story& s) : Page(s) {
        // 02031cdc.
        open("passcode_bg.bgx", "passcode_sub.bgx");
        pictureButton(kit, back, 0xd6, 0xaf, "?/omake_btn.spr", "modoru", 4);
    }

    void run() {
        // 02031e44 (02002284: then the extras).
        enter([this] { back.update(kit, story.touch); });
        do {
            story.frame([this] { back.update(kit, story.touch); });
        } while (!back.clicked(kit, story.touch));
        story.state.scene = 0xd;
    }
};

// --------------------------------------------------------------------------
// 0x25: the passcode: eight characters from 36 keys.

struct Passcode : Page {
    int cursor = 0;                          // +0
    std::string code = std::string(8, '\0'); // +4..+0xb
    std::string keys;                        // +0xc: otext/keys/secret.txt
    SceneKit::SpriteObject marker;           // +0x30: name_btn.spr, the cursor
    SpriteButton back;                       // +0x34
    Button ok, erase;                        // +0x38 / +0x3c
    Button keyButtons[36];                   // +0x40..
    SceneKit::SpriteObject badCode;          // +0xd0: ?/bad_code.spr

    std::string expected() const {
        // 0200b618(code, 20057): the seed passes as a byte (0x59).
        return consoleCode(consoleAddress(story.saveFile.parent_path()), 0x59);
    }

    std::string typed() const { return std::string(code.c_str()); }

    explicit Passcode(Story& s) : Page(s) {
        // 0203a000.
        open("codeinput_bg.bgx", "?/name_sub2.bgx");
        // 0203a660: the keys' characters.
        const auto path = story.pack / "otext" / "keys" / "secret.txt";
        if (fileExists(path)) {
            const Bytes raw = readFile(path);
            keys.assign(raw.begin(),
                        raw.begin() + std::ptrdiff_t(std::min<std::size_t>(raw.size(), 0x24)));
        }
        keys.resize(0x24, ' ');
        marker = kit.load("name_btn.spr", 0, 0, 1);
        pictureButton(kit, ok, 0x60, 0xa1, "?/wifi_download_btn.spr", "ok", 0xb);
        back.load(kit, "?/load_btn.spr", 4);
        int x = 0xd, y = 0x34;
        for (int i = 0; i < 36; ++i) {
            keyButtons[i].setRect(x, y, 0x13, 0x13);
            keyButtons[i].sound = 0x29;
            x += 0x14;
            if (i == 9) {
                x = 0x17;
                y = 0x49;
            } else if (i == 0x13) {
                x = 0x21;
                y = 0x5e;
            } else if (i == 0x1c) {
                x = 0x2b;
                y = 0x73;
            }
        }
        pictureButton(kit, erase, 0xc0, 0x75, "name_btn.spr", "erase", 10);
        badCode = kit.load("?/bad_code.spr", 0, 0x3b, 1);
        kit.select(badCode, 1u);
        badCode.x = (0x100 - kit.frameWidth(badCode)) >> 1;
        badCode.y = 0x3b;
        setupSounds(3);
    }

    // 0203a5e0 / 0203a600 / 0203a5b4.
    void add(char c) {
        code[std::size_t(cursor)] = c;
        if (cursor < 7) {
            ++cursor;
        }
    }

    void removeLast() {
        if (code[std::size_t(cursor)] == '\0') {
            if (cursor > 0) {
                --cursor;
            }
            code[std::size_t(cursor)] = '\0';
            return;
        }
        code[std::size_t(cursor)] = '\0';
        if (cursor > 0 && cursor < 7) {
            --cursor;
        }
    }

    void reset() {
        cursor = 0;
        code.assign(8, '\0');
    }

    // 0203a6e4 + 0203a750: the code's characters 0x10 apart from (0x45,
    // 0x13), the cursor under the next one.
    void drawCode() {
        kit.clearTextRect(0x45, 0x13, 0x8c, 0xc, SceneKit::Lower);
        const Font& font = kit.assets.font("fontevent.NFTR");
        Surface& layer = kit.textLayer(SceneKit::Lower);
        for (int i = 0, x = 0x45; i < 8; ++i, x += 0x10) {
            const auto c = std::uint8_t(code[std::size_t(i)]);
            if (c != 0 && c != ' ') {
                textGlyph(layer, font, c, x, 0x13, textColour(0xff));
            }
        }
        marker.x = cursor * 0x10 + 0x44;
        marker.y = 0x1d;
        kit.select(marker, 1u);
        kit.draw(marker);
    }

    // 0203a790: buttons, then the keys (a held key shows a red square).
    void input() {
        back.update(kit, story.touch);
        ok.update(kit, story.touch);
        erase.update(kit, story.touch);
        if (erase.clicked(kit, story.touch)) {
            removeLast();
        }
        for (int i = 0; i < 36; ++i) {
            Button& key = keyButtons[i];
            key.update(kit, story.touch);
            if (key.held(story.touch)) {
                kit.rectangle(key.x, key.y, 0x13, 0x13, 0x001f, 15);
                return;
            }
            if (key.clicked(kit, story.touch)) {
                add(keys[std::size_t(i)]);
                return;
            }
        }
    }

    // 0203a4dc: a wrong code shows its message until the stylus is lifted,
    // then starts again.
    bool accepted() {
        if (typed() == expected()) {
            return true;
        }
        do {
            story.frame([this] {
                drawCode();
                ok.draw(kit);
                erase.draw(kit);
                back.draw(kit);
                kit.draw(badCode);
            });
        } while (!story.touch.released);
        reset();
        return false;
    }

    void run() {
        // 0203a8bc.
        enter([this] {
            input();
            drawCode();
        });
        for (;;) {
            story.frame([this] {
                input();
                drawCode();
            });
            if (!typed().empty() && ok.clicked(kit, story.touch) && accepted()) {
                break;
            }
            if (back.clicked()) {
                break;
            }
        }
        // 0203a350: the right code runs event 0x103, else back to the extras.
        if (typed() == expected()) {
            story.state.scene = scene::Event;
            story.state.event = 0x103;
        } else {
            story.state.scene = 0xd;
        }
    }
};

} // namespace

void replayMovie(Story& story, int movie) {
    story.movieCues = MovieCues{};
    const std::string script = "script/event/m" + std::to_string(movie) + ".gds";
    if (fileExists(story.pack / script)) {
        story.runScript(script, story.commonCommands());
    }
    story.state.movie = movie;
    story.state.returnScene = 0xf;
    playMovie(story, story.movieCues, false);
    story.movieCues = MovieCues{};
}

void secretMenu(Story& story) {
    SecretMenu page(story);
    page.run();
    page.close();
}

void extrasMenu(Story& story) {
    Extras page(story);
    page.run();
    page.close();
}

void characterFiles(Story& story) {
    CharacterFiles page(story);
    page.run();
    page.close();
}

void movieTheatre(Story& story) {
    MovieTheatre page(story);
    page.run();
    page.close();
}

void artGallery(Story& story) {
    ArtGallery page(story);
    page.run();
    page.close();
}

void musicRoom(Story& story) {
    MusicRoom page(story);
    page.run();
    page.close();
}

void voiceRoom(Story& story) {
    VoiceRoom page(story);
    page.run();
    page.close();
}

void passcodeNotes(Story& story) {
    PasscodeNotes page(story);
    page.run();
    page.close();
}

void passcodePage(Story& story) {
    Passcode page(story);
    page.run();
    page.close();
}

} // namespace picarat::adventure1
