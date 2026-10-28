#pragma once
#include "amiga/iff.hpp"
#include "app/game_module.hpp"
#include "cannon_fodder_amiga/data/assets.hpp"
#include "cannon_fodder_amiga/game/campaign.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/game/tables.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The screens around the missions (.analysis/cannon_fodder_amiga/game.md
// "Front end"): the intro, the hill between phases, the briefing and the
// debrief. The original draws them into a 320 x 256 screen with the
// blitter from frame tables of its own ($8CFCE, $A6DC8, $8D3F6, …) whose
// frames lie in pictures it loaded at fixed addresses; colours fade in and
// out in 16 steps. Here: an indexed screen, the same tables and pictures,
// the same timings.
namespace fodder::game {

// A frame table of the program and the pictures its frames lie in, by the
// address the original loads each picture to.
class FrontFrames {
public:
    FrontFrames(std::span<const std::uint8_t> program, std::uint32_t table);
    struct Frame {
        std::uint32_t address{};
        int width_words{}, height{}, offset_x{};
    };
    std::optional<Frame> frame(int group, int index) const;

private:
    std::span<const std::uint8_t> program_;
    std::uint32_t table_{};
};

// 320 x 256, 32 colours (0-15 and 16-31 fade separately). The screens
// that change colours down the screen (the parallax's bands) or show
// sprites in colours of their own use the entries from 32 on.
struct FrontScreen {
    static constexpr int width = 320, height = 256;
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(std::size_t(width * height), 0);
    std::array<amiga::Rgb4, 64> palette{};
    int fade_low{15}, fade_high{15}; // $817C0, $817C4: 15 full, 0 black ($8CE34); 16-63 follow high
    void clear() { std::fill(pixels.begin(), pixels.end(), std::uint8_t(0)); }
    platform::Image image() const;
    template <class A> void persist(A& a) {
        a(pixels, palette, fade_low, fade_high);
        if (pixels.size() != std::size_t(width * height)) A::fail();
    }
};

class FrontEnd {
public:
    FrontEnd(const data::Content& content, std::span<const std::uint8_t> program, const PanelArt& panel);

    enum class Screen { none, intro, hill, parallax, briefing, debrief, ending, saves };
    Screen screen() const { return screen_; }
    // The hill's answer ($A58F8): go on, or the LOAD or SAVE icon.
    enum class HillChoice { go_on, load, save };
    HillChoice hill_choice() const { return hill_choice_; }
    bool active() const { return screen_ != Screen::none; }

    // $9CBB0: the presentation once at the start (a click skips it).
    void start_intro();
    // $A56B0: the hill between phases (the score, the fallen and the squad);
    // a click on the hill goes on.
    void start_hill(const Campaign& campaign);
    // $A766C: the mission's landscape scrolling by, a helicopter flying
    // over it, the mission's name coming in (jon song 7); a click (or the
    // helicopter reaching the left) ends it.
    void start_parallax(const Campaign& campaign, const std::string& landscape);
    // $A9E56 (LOAD): SELECT FILE, UP, five saved games, DOWN, CONTINUE
    // GAME; a click on a game picks it (picked_save()), CONTINUE GAME goes
    // back (-1).
    void start_save_list(const std::vector<std::string>& names);
    int picked_save() const { return picked_; }
    // $A6614: the briefing of the phase.
    void start_briefing(const Campaign& campaign, const MissionTables& tables);

    // $A8AF6: after a mission (or when the game is over): the fallen of the
    // mission ("LOST IN SERVICE", poppies) or the survivors ("HEROES IN
    // VICTORY", medals), a name every 88 frames.
    void start_debrief(const Campaign& campaign);
    // $9CAB2: the end of the game: WON.RAW and the title music (the
    // original stops there).
    void start_ending();

    // One frame (50 a second).
    void tick(const app::GameInput& input);
    platform::Image picture() const;
    const std::vector<std::uint8_t>& indices() const { return screen_buffer_.pixels; } // (tools)

    // Music the screen wants once: a song of jon.sng/jon.ins (or of
    // warx4.sng/warx1.ins, `$86FDA`), -1 none.
    struct Song {
        int number{-1};
        bool warx{};
    };
    Song take_song() {
        const Song s{song_, warx_};
        song_ = -1;
        return s;
    }
    // The parallax ($A766C): the layers' scroll, the names sliding in, the
    // helicopter's flight and the fade.
    struct ParallaxState {
        std::array<std::int32_t, 4> scroll{};       // $826B4…$826C0: the layers' scroll (16.16), slower to faster
        int name_x{0x138};                          // $826B0 (and $826C4 = $158 - it)
        std::int32_t heli_x{0x150 << 16}, heli_y{0x1e << 16}; // $826CC, $826D0
        int speed{}, wanted_speed{};                // $826D4, $826D8
        int heading{}, wanted{};                    // $826DC, $826DE
        int leg{}, leg_left{};                      // $826E0 (in entries), $826E2
        int fade{};                                 // $826E4
        int rotor{}, rotor_shown{};                 // $826E6: the rotor picture ($CD + it)
        int sprite_x{}, sprite_y{};                 // $8CABA/$8CABC: where the sprites were put
        bool clicked{};                             // $81484
        bool ended{};                               // $826E8
        int vblank{};                               // (the parity of $80618)
        template <class A> void persist(A& a) {
            a(scroll, name_x, heli_x, heli_y, speed, wanted_speed, heading, wanted, leg, leg_left, fade, rotor,
              rotor_shown, sprite_x, sprite_y, clicked, ended, vblank);
        }
    };
    const ParallaxState& parallax_state() const { return parallax_; } // (tools)

    // The debrief ($A8AF6): rounds of two frames; the names' column
    // scrolls up a line a round; a line every 44 rounds.
    struct DebriefState {
        enum class Mode { fade_in, rounds, fade_out_middle, fade_in_middle, fade_out_end, done };
        Mode mode{Mode::fade_in};
        int frames{};                 // within a fade, or of the round (0, 1)
        int round{};                  // (tools)
        int fade_wait{};              // $82160
        bool fade_due{};              // $82162
        bool fallen_banner{};         // $82164: the fallen first, then the heroes
        int line_timer{};             // $82166
        bool listing_fallen{};        // $82168
        int roster_next{};            // $8216A (a place)
        int roster_left{};            // $8216E
        int line_rank{};              // $82170
        int end_timer{};              // $82172
        bool nobody_back{};           // $82174
        int banner_timer{};           // $8061A
        int banner_step{};            // $8215E
        int fallen_next{};            // $80CE8 (in the mission's fallen)
        int promoted_next{};          // $8219E (in `promoted`)
        std::vector<int> promoted;    // $8218C: the new ranks of the recruits back
        template <class A> void persist(A& a) {
            a(mode, frames, round, fade_wait, fade_due, fallen_banner, line_timer, listing_fallen, roster_next,
              roster_left, line_rank, end_timer, nobody_back, banner_timer, banner_step, fallen_next, promoted_next,
              promoted);
        }
    };
    const DebriefState& debrief_state() const { return debrief_; } // (tools)

    // The load screen's state.
    struct SaveListState {
        enum class Mode { fade_in, wait, fade_out, done };
        Mode mode{Mode::fade_in};
        int level{};  // $817C0
        int first{};  // $828D6: the first game shown
        template <class A> void persist(A& a) { a(mode, level, first); }
    };
    // The screens with the mouse pointer (the arrow, $81FDE): the hill and
    // the load screen.
    bool shows_pointer() const { return screen_ == Screen::hill || screen_ == Screen::saves; }
    // (tools: the save-disk request $AA1CE drawn on a cleared screen, to
    // check the load screen's text and boxes against the reference)
    std::vector<std::array<int, 4>> draw_disk_request();

    // The music fades out ($A3D8A) while this is set.
    bool music_fading() const { return music_fade_; }

    // For the quick saves: which screen was started (and with what), and how
    // far it has got. Loading starts that screen again (its steps are code,
    // not data) and then puts its progress back.
    template <class A> void persist(A& a, const MissionTables& tables) {
        a(started_);
        if constexpr (A::reading) restart(tables);
        a(screen_, step_, counter_, skipped_, left_was_down_, song_, warx_, music_fade_, hill_choice_, save_allowed_,
          picked_, screen_buffer_, hill_, parallax_, debrief_, promotion_timer_, save_list_, pointer_x_, pointer_y_);
        if (step_ > steps_.size()) A::fail();
    }

private:
    // A step of a screen: runs once a frame until it returns true.
    using Step = std::function<bool(const app::GameInput&)>;
    void run(Screen screen, std::vector<Step> steps);
    Step fade_in();
    Step fade_out(int frames_per_step = 3);
    Step hold(int frames, bool click_ends = true);

    // Drawing.
    void draw_frame(const FrontFrames& frames, int group, int index, int x, int y);
    int text_width(const std::string& text, std::uint32_t widths) const;
    void draw_text(const FrontFrames& frames, int group, std::uint32_t widths, const std::string& text, int x, int y,
                   int space_glyph = 0);
    void draw_centred(const FrontFrames& frames, int group, std::uint32_t widths, const std::string& text, int y,
                      int space_glyph = 0);
    std::string program_text(std::uint32_t address) const; // a string ended by a byte with bit 7 set
    std::uint32_t program_long(std::uint32_t address) const;
    // Intro pieces.
    void show_text_page(std::uint32_t page);
    void show_picture(const std::string& file, int disk);
    void blit_block(int index, int x, int y); // $A8EF8: a block of the table $A918A
    bool debrief_frame(bool held);
    void rectangle(int x, int y, int w, int h, int colour); // $A0D70
    // $AAC94: a line of text centred with the hill's header font, boxed
    // ($A0D22); its click area ($AAB56).
    struct Area {
        int x, w, y, h;
    };
    Area text_area(const std::string& text, int y) const;
    void boxed_text(const std::string& text, int y, int outer = 0xf, int inner = 0xc);
    void draw_save_names();
    bool save_list_frame(const app::GameInput& input);
    void debrief_round();
    void debrief_line();     // $A8FC4
    void debrief_after_round(bool held);
    int tables_sine(int direction) const;

    void restart(const MissionTables& tables);

    // What the screen was started with.
    struct Started {
        Screen screen{Screen::none};
        Campaign campaign;
        std::string landscape;
        std::vector<std::string> names;
        template <class A> void persist(A& a) { a(screen, campaign, landscape, names); }
    };
    // The hill's walk down the road ($A5D50 …) and its column.
    struct HillState {
        FrontScreen background;
        HillColumn column;
        std::vector<HillColumn::Entry> arriving;
        std::vector<std::array<int, 3>> path;  // $92FC8: x, y, state
        std::array<int, 15> walkers{};         // $8275C: path offsets (entries), < 0 not out
        int joining{};                         // $8277E
        bool queued{};                         // $8277C: the walkers have closed up
        std::uint32_t toggle{};                // $82758 (its high word the walking frame)
        int queue{};                           // $80652: recruits waiting from before
        int grave_variety{};                   // $80A0E - $8073C
        // The door at the road's end ($A5A24): its pictures in order
        // ($A5972, $BB29A), the step, the picture shown and the next.
        std::vector<int> door;
        int door_step{}, door_frame{}, door_shown{}; // $8205C, $8205A
        template <class A> void persist(A& a) {
            a(background, column, arriving, path, walkers, joining, queued, toggle, queue, grave_variety, door, door_step,
              door_frame, door_shown);
        }
    };
    bool hill_walk();
    void hill_door();
    void draw_hill();
    void helicopter_leg(); // $A7924
    void parallax_frame(bool click);
    void draw_parallax();

    const data::Content& content_;
    const PanelArt& panel_;
    std::span<const std::uint8_t> program_;
    FrontScreen screen_buffer_;
    std::map<std::uint32_t, amiga::IlbmPicture> loaded_; // pictures by the original's load address
    amiga::IlbmPicture sheet_; // the hill's sheet of scaled graves and recruits ($A5C3A)
    Screen screen_{Screen::none};
    std::vector<Step> steps_;
    std::size_t step_{};
    int counter_{};
    bool skipped_{};
    bool left_was_down_{true};
    int song_{-1};
    bool warx_{};
    bool music_fade_{};
    HillChoice hill_choice_{HillChoice::go_on};
    bool save_allowed_{};
    int picked_{-1};
    Started started_;
    HillState hill_;
    ParallaxState parallax_;
    DebriefState debrief_;
    SaveListState save_list_;
    int pointer_x_{}, pointer_y_{}; // $8CABA/$8CABC
    int promotion_timer_{}; // $821A2: not reset by a debrief that lists the fallen first
    // The parallax's two playfields ($71000 and $62000: 640 x 256 each, the
    // picture twice side by side; colours 0-7).
    std::vector<std::uint8_t> playfield1_, playfield2_;
};

} // namespace fodder::game
