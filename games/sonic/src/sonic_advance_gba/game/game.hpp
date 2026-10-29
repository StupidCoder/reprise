#pragma once
#include "gba/display.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/background.hpp"
#include "sonic_advance_gba/game/controller.hpp"
#include "sonic_advance_gba/game/scheduler.hpp"
#include "sonic_advance_gba/game/sound.hpp"
#include "sonic_advance_gba/game/sprite_tiles.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include <array>
#include <memory>
#include <optional>

// Sonic Advance (Game Boy Advance): the game.
//
// Each frame has two parts, as in the cartridge's main loop (0800089C): the
// game's processes run once (scheduler.hpp: screens, objects, effects) after
// the sound engine and the buttons, then the vertical blank hands the
// prepared picture to the display (video.hpp, background.hpp).
//
// Hexadecimal numbers in comments are ARM addresses: routines in the
// cartridge (0800xxxx, Thumb unless noted), state in internal work RAM
// (03000000-03007FFF) or external work RAM (02000000-0203FFFF), and
// offsets into a task's data (+xx). They link the re-implementation to the
// code it reproduces; nothing here depends on that layout.
namespace sonic_advance_gba {

// The cartridge's save record (03005160; 0x430 bytes in Flash, magic "PIRO").
// Only the fields the reconstructed parts use are here so far.
struct SaveData {
    // 03005168 (+08): per character, the acts cleared: an act's results
    // raise it to the act's number + 1, reaching act 13 sets 0Eh, the true
    // ending 0Fh. The character select goes to the act select when nonzero;
    // the true ending's way needs every character above 0Ch.
    std::array<u16, 4> zones_reached{};
    u8 unknown_5178{};   // 03005178 (+18)
    u8 language = 1;     // 03005179 (+19): 0 Japanese, 1 English
    u8 time_limit_off{}; // 0300517A (+1A): no time over after ten minutes
    u8 button_layout{};  // 0300517B (+1B): 0 A jumps, 1 B jumps
    u8 emeralds{};       // 0300517C (+1C): bit n the emerald of special stage n (7F: all seven)
    u8 unknown_5584{};   // 03005584 (+424): set to 1 by the true ending
};

// The view some effects are drawn through (08003D84): a turn, scales, a
// depth factor and the point the view centres on (03001204, 030010B0,
// 03004C34, 03004C38, 0300120C, 030020C0; 0300232C/030045F4 kept with
// them). An act's start makes the depth a half and centres the screen.
struct SceneTransform {
    u16 angle{};
    s16 scale_x = 0x100, scale_y = 0x100;
    s16 depth = 0x100;
    s16 focus_x{}, focus_y{};
    s16 centre_x{}, centre_y{};
};

class Game {
public:
    // Power-on. With `save`, the cartridge holds a valid save record;
    // otherwise the game starts with the language select.
    Game(const Assets& assets, std::optional<SaveData> save);

    // The processes go first: their ends use the rest of the game.
    ~Game() { scheduler_.destroy_range(0, 0xFFFF); }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // One video frame. `buttons` uses the button:: bits (held).
    void frame(u16 buttons);

    const gba::Display& display() const { return display_; }

    // Video memory written at once (not in the vertical blank), as the
    // screens' set-up code does with direct copies and fills. On the
    // hardware such a write shows from whatever line is being drawn at that
    // moment; here it shows from the next picture. (Counted for the
    // lockstep harness, which then does not compare the picture.)
    gba::Display& video_memory_now() {
        ++immediate_writes;
        return display_;
    }

    unsigned immediate_writes{};

    u32 frames() const { return frame_counter_; }

    // Services for the processes.
    const Assets& assets() const { return assets_; }

    VideoState& video() { return video_; }

    BackgroundQueues& backgrounds() { return backgrounds_; }

    Scheduler& scheduler() { return scheduler_; }

    const Controller& controller() const { return controller_; }

    // The auto-repeat delays, which some screens change.
    Controller& controller_settings() { return controller_; }

    Sound& sound() { return sound_; }

    SaveData& save() { return save_; }

    // The act being played (or last played).
    StageState& stage() { return *stage_; }

    // Demos: the recorded buttons replace the controller's.
    DemoMode demo_mode() const { return controller_.demo; }

    // 0800DF88: plays the LZ77-compressed buttons at `data` once the
    // player's process starts them.
    void start_demo_playback(u32 data) {
        controller_.demo = DemoMode::play;
        demo_data_ = data;
    }

    // 0804CC78: the recording or playback starts over (a process holds the
    // buttons; when it goes, the demo mode ends).
    void restart_demo_input();

    SceneTransform scene;
    // 03001100 bit 15: the button reset (A+B+Start+Select) is off.
    bool reset_disabled = false;
    // 03001100 bit 10: the game is paused (set and cleared by the pause
    // process). From the next frame on only the processes marked to run in
    // a pause run (bit 11) and the sprite table keeps the paused picture.
    bool pause_requested = false;
    // 030010B4/B6: subtracted from the position of sprites drawn relative
    // to the view (sprite_flag::view_relative).
    s16 view_offset_x{}, view_offset_y{};
    // 0300505C: the game mode (0 the story, 1 time attack, 2 and up
    // multiplayer modes; 6 a multiplayer mode without special moves).
    u8 game_mode{};
    // 03005018: continues left (cleared by the main menu; the game over
    // offers the continue screen while positive).
    s8 continues{};
    // Sprite tile memory handed out to screens (sprite_tiles.hpp).
    SpriteTiles sprite_tiles;
    // 03006240: the process holding the screens' pictures (graphics.hpp).
    class ScreenGraphics* screen_graphics{};

    // Between screens (08001954(0, FFFF) and the queue resets that follow
    // it): every process ends and the pending background work is dropped.
    void clear_screen_processes();

    // The screens (frontend/).
    void start_sega_logo();                    // 0800CFB8
    void start_sonic_team_logo();              // 0800D198
    void start_intro();                        // 080638E4
    void start_title(bool with_music);         // 0800D4E0
    void start_main_menu(bool load);           // 0800D8FC
    void start_character_select(u8 character); // 080597A8
    void start_demo();                         // 0800DF88

private:
    void vertical_blank();

    const Assets& assets_;
    gba::Display display_;
    VideoState video_;
    BackgroundQueues backgrounds_;
    Scheduler scheduler_;
    Controller controller_;
    Sound sound_;
    SaveData save_;
    std::unique_ptr<StageState> stage_ = std::make_unique<StageState>();
    u32 demo_data_{};     // 03004C50
    u32 frame_counter_{}; // 03001B24, counted by the vertical blank interrupt
    bool paused_{};       // 03001100 bit 11
};

} // namespace sonic_advance_gba
