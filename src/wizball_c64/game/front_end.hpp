#pragma once
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/colour_runs.hpp"
#include "wizball_c64/game/display.hpp"
#include "wizball_c64/game/keyboard.hpp"
#include "wizball_c64/game/play.hpp"
#include "wizball_c64/game/random_source.hpp"
#include "wizball_c64/game/scores.hpp"
#include "wizball_c64/game/sound.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Wizball as a whole: the front end (the title with the top scores $8F75,
// the menu of modes $9017, three pages of wiztips $944D), and the game it
// starts ($6392): per life the player screen ($6893), play (Play), the
// game-over message ($69F2), and at the end the high-score entry ($6B92).
// One tick() is one frame; the screens are drawn by picture() from the
// state here and the screens' raster handlers (front_picture.cpp).
namespace wizball::game {

struct Controls {
    Ports ports;     // the two joysticks
    KeyMatrix keys{}; // the keyboard
};

class FrontEnd {
public:
    FrontEnd(const data::Assets& assets, const data::GameImage& image, RandomSource& random);
    // A copy of the whole state (a quick save); the game in it plays on the
    // copy's sound. The random source stays the caller's.
    std::unique_ptr<FrontEnd> copy() const;

    void tick(const Controls& controls);
    // `extension`: widescreen side columns (an enhancement; 0 = as the C64).
    Display picture(int extension = 0, int blackout_fade = 0) const;
    Sound& sound() { return sound_; }

    enum class Screen { title, menu, tips, loading, player, play, game_over, scores };
    Screen screen() const { return screen_; }
    const Play* play() const { return play_ ? &*play_ : nullptr; }
    Play* play() { return play_ ? &*play_ : nullptr; }
    const ColourRuns& colours() const { return colours_; }

    // The launcher's cheats (options): the game's own invincibility cheat
    // without its key sequence, lives that are not counted, and every start
    // place on the title's keys 0-5.
    struct Cheats {
        bool invincible{}, infinite_lives{}, any_start_place{};
        bool carnage{}; // carnage mode (not the original game)
        bool catellite_ai{}; // the automatic catellite (not the original game)
    };
    void set_cheats(const Cheats& cheats);

    // The flying stars of the menu and the player screen ($90BD), as their
    // sprites show them (for the effects): positions, shape and colour;
    // empty on the other screens.
    struct StarSprites {
        std::vector<std::array<int, 3>> sprites; // x, y, 1 for the top half's mirror (shown above line 136)
        SpriteShape shape{};
        std::uint8_t colour{};
    };
    StarSprites star_sprites() const;

private:
    // The screens.
    void start_title(bool with_music); // $8F61 / $8F75
    void title_frame(const Controls& c); // $8FEE
    void start_menu();                 // $9017
    void menu_frame(const Controls& c);  // $906C
    void start_tips();                 // $944D
    void tips_frame(const Controls& c);
    void tips_pass(const Controls& c);   // $94BE
    void start_game();                 // $6392
    void drop_game();                  // back to the front end
    void start_life(int setup_frames, bool after_play); // $639B
    void loading_frame();
    void start_player_screen();        // $6893
    void player_frame(const Controls& c); // $691C
    void play_frame(const Controls& c);
    void end_life();                   // $63BB-$63C7
    void game_over_frame(const Controls& c); // $6A94
    void finish_life();                // $6AC0 on
    void game_ended();                 // $6B5B
    void start_scores();               // $6B92
    void next_name();                  // $6BFB
    void scores_frame(const Controls& c); // $6CC5
    void begin_typing(int column, int row, int length); // $6CF6, $6CBC
    void show_typing();                // $6D44
    void after_names();                // $6C8D
    void finish_name();                // $6CB6

    // Shared work.
    void read_keys(const Controls& c, bool all);   // $73D4 / $7429
    std::uint8_t stick_change(const Controls& c);  // $91FA: $FF when unchanged
    void set_timer(std::uint8_t t);               // $9215
    bool timer_runs();                           // $921E: false once out
    void move_stars();                           // $90BD
    void title_keys();                           // $AF85: level select and the cheat

    // The text screen ($EC00) and its colour RAM.
    void clear_screen(std::uint8_t code, std::uint8_t colour);
    void print(std::uint16_t address, std::uint8_t colour, bool big); // $8CFA / $8CF4
    void put_big(int& at, std::uint8_t ch, std::uint8_t colour);      // $8D99
    std::uint8_t text_byte(std::uint16_t address) const;

    // Sprites $80-$8F drawn at run time ($E000).
    std::uint8_t& sprite_byte(int pointer, int offset);
    void fill_sprites(int first, int count, std::uint8_t value); // $8F15
    void sprite_text(int pointer, std::uint16_t address, int first_row, bool big); // $8DE4 / $8E32
    SpriteShape sprite_shape(int pointer) const;
    void tips_picture(Display& d) const;
    Display screen_picture() const;
    void tips_line1();                 // $9681 / $9762: the page's animations

    const data::Assets& assets_;
    const data::GameImage& image_;
    RandomSource& random_;
    Sound sound_;
    ColourRuns colours_;
    Scores scores_;
    std::optional<Play> play_;
    Screen screen_{Screen::title};

    std::array<std::uint8_t, 1000> codes_{}, cells_{};       // $EC00, colour RAM
    std::array<std::uint8_t, 16 * 64> sprites_{};            // $E000: sprites $80-$8F
    bool dot_sprite_{};                                      // $9038: sprite 5 is a dot
    bool letters_inverted_{}, big_inverted_{};               // $F100, $F6F0 as the font is in memory
    KeyLatch keys_{};                                        // $B254/$B255
    std::uint8_t stick_last_{};                              // $B270
    std::uint8_t timer_{}, timer_clock_{};                   // $B25F, $B260
    std::uint8_t mode_{};                                    // $B272: the menu's choice
    std::uint8_t tips_page_{};                               // $944C
    std::uint8_t shot_volume_{};                             // $BA81, kept between games
    int tips_wait_{};                                        // frames the page takes to print
    bool tips_double_{};                                     // two passes in the page's first frame
    struct Star {
        std::uint8_t x{}, x_fraction{}, y{}, y_fraction{}, dx{}, dy{};
    };
    std::array<Star, 4> stars_{};                            // $EB9A
    std::uint8_t start_place_{}, start_limit_{};             // $AF7E, $AF7D
    std::uint8_t cheat_step_{};                              // $AFCD
    bool cheat_{};                                           // $BB07
    Cheats cheats_{};
    std::uint8_t border_{};                                  // $D020 on the title (the start place)
    int loading_wait_{};                                     // frames the place takes to set up
    bool loading_in_play_{};                                 // the play's raster list still runs
    std::uint8_t sprite_flash_clock_{}, sprite_flash_{};     // $B88D, $B88E
    std::uint8_t sprite_colour_{1};                          // $D027-$D02E on the player screen
    std::uint8_t player_background_{0x0A}, player_background_top_{0x0A}; // $D021: set mid-frame (about line 155)
    std::array<std::uint8_t, 3> tips_clock_{}, tips_phase_{}; // $96EE-$96F3
    std::uint8_t tips_cat_clock_{}, tips_cat_phase_{};       // $97C0, $97C1
    std::uint8_t tips_token_colour_{};                       // $95EE ($B4D6)
    std::uint8_t token_clock_{}, token_flash_{};             // $B25A, $B25B (shared with play in the original)
    // High-score entry.
    int entry_player_{};                                     // $6B21
    std::uint8_t entry_marker_{}, entry_rank_{};             // $6B22, $6B23
    enum class EntryStep { short_name, wizard, feline, words, done } entry_step_{};
    std::array<std::uint8_t, 12> typed_{};                   // $6D76 on
    std::uint8_t typed_length_{}, typed_limit_{};            // $6D73, $6D72
    std::uint8_t typed_column_{}, typed_row_{};
    int scores_wait_{};                                      // frames blanked before the entry runs
    std::array<std::uint8_t, 2> ranks_{};                    // $BFF2, $BFF3
    std::array<std::uint8_t, 10> name_{};                    // $BFF4: the name being made
};

} // namespace wizball::game
