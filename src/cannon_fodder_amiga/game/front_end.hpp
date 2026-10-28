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

// 320 x 256, 32 colours (0-15 and 16-31 fade separately).
struct FrontScreen {
    static constexpr int width = 320, height = 256;
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(std::size_t(width * height), 0);
    std::array<amiga::Rgb4, 48> palette{}; // (32-47: a sprite's own colours)
    int fade_low{15}, fade_high{15}; // $817C0, $817C4: 15 full, 0 black ($8CE34); 16-47 follow high
    void clear() { std::fill(pixels.begin(), pixels.end(), std::uint8_t(0)); }
    platform::Image image() const;
};

class FrontEnd {
public:
    FrontEnd(const data::Content& content, std::span<const std::uint8_t> program, const PanelArt& panel);

    enum class Screen { none, intro, hill, parallax, briefing, debrief, ending };
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
    platform::Image picture() const { return screen_buffer_.image(); }

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
    // The music fades out ($A3D8A) while this is set.
    bool music_fading() const { return music_fade_; }

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
    int tables_sine(int direction) const;

    const data::Content& content_;
    const PanelArt& panel_;
    std::span<const std::uint8_t> program_;
    FrontScreen screen_buffer_;
    std::map<std::uint32_t, amiga::IlbmPicture> loaded_; // pictures by the original's load address
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
};

} // namespace fodder::game
