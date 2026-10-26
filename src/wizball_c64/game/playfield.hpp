#pragma once
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/random_source.hpp"
#include <array>
#include <cstdint>
#include <set>

// The scrolling landscape of one place: which map columns are on screen,
// the fine scroll, the star sky, and the screens the game prepares ahead of
// the scroll.
//
// The original keeps three screens and shows one; the other two hold the
// picture shifted one character left and one right, rebuilt over four passes
// after every coarse step ($77F7). A coarse step then only switches screens
// ($7A98). The sky is made of 90 star characters ($01-$5A): an 18-column
// pattern per character row of a tile row, laid into empty map cells as a
// column enters the screen ($79C2); which of the 18 columns a new screen
// column gets comes from a running counter ($B27A) that moves when a column
// is prepared, so the sky does not follow the map exactly. The characters
// themselves hold one fixed dot per row band and one drifting star per band
// ($7540, $747E).
namespace wizball::game {
using Glyph = data::Glyph;

class Playfield {
public:
    static constexpr int columns = 40, rows = 20; // screen rows 3-22
    static constexpr int sky_bands = 5;  // one per character row of a tile row
    static constexpr int sky_codes = 90; // $01-$5A, 18 per band

    explicit Playfield(const data::Assets& assets, const data::GameImage& image);

    // Level start ($7540 sky, $7586 start column, $75EA first screen and
    // the two prepared ones). `bonus` selects the bonus stage's wrap ($75DF).
    void clear_sky(RandomSource& random);
    void start(int place, RandomSource& random);
    void draw_first_screen(int place, int wrap_tiles, bool blank = false);
    void prepare_step(); // $77F7: one chunk of the next prepared screen

    // $7A98: one pass of scrolling at speed index `speed` (-6..6).
    void scroll(int speed);
    // $747E: the drifting stars.
    void animate_stars(int speed, RandomSource& random);

    // What the shown screen holds (0 = empty cell), shots included.
    std::uint8_t code(int column, int row) const;
    // Shots are characters written into the shown screen and taken out
    // again before the next pass scrolls ($6F21, $7090, $9A14, $9AD6).
    void put(int screen, int column, int row, std::uint8_t code);
    std::uint8_t code_on(int screen, int column, int row) const;
    // Widescreen (an enhancement): what a screen column left (< 0) or right
    // (>= 40) of the window would hold: the map and the sky as the game lays
    // them in, only as far as the game's own view ever goes at the place's
    // ends (empty beyond); all sky in a cleared place.
    std::array<std::uint8_t, rows> side_column(int column) const;
    // Carnage mode (not the original game): a landscape cell's place in the
    // map (stays the same while the view scrolls), for a screen column (may
    // be a side column) and row; and opening a cell: it is sky from now on,
    // in the screens and whenever the map is read again.
    std::uint32_t cell_key(int column, int row) const;
    void open_cell(int column, int row);
    bool opened(std::uint32_t key) const { return opened_.count(key) != 0; }
    // The 90 sky characters ($01-$5A) as the charset holds them.
    Glyph sky_glyph(int code) const;

    // State the rest of the game reads.
    std::uint8_t view_column() const { return view_column_; }   // $B1F1
    std::uint8_t fine() const { return fine_; }                 // $B1F2
    std::uint8_t fine_scroll() const { return fine_scroll_; }   // $B1FC
    std::int8_t wiz_offset() const { return wiz_offset_; }      // $B1F9
    void set_wiz_offset(std::int8_t v) { wiz_offset_ = v; }
    void halt() { step_ = 0; }
    // $7F0B: once a place is cleared the view stands; $7BCB moves $B1F2
    // alone, which then places the wiz on the screen.
    void nudge(int speed) { step_fine(speed); }
    void set_fine(std::uint8_t v) { fine_ = v; }
    void set_fraction(std::uint8_t v) { fraction_ = v; }
    void set_view_column(std::uint8_t v) { view_column_ = v; }
    void set_fine_scroll(std::uint8_t v) { fine_scroll_ = v; }
    // $646F: the scroll stopped for the end of a place.
    void stop_for_clear() {
        fine_ = 0;
        pixel_delta_ = 0;
        step_ = 0;
        wiz_offset_ = 0;
    }                                   // $820B
    void align_fine_scroll() { fine_scroll_ = fine_ & 7; }       // $825C
    std::int8_t step() const { return step_; }                  // $B1FF: 2 right, -1 left, 0
    std::int8_t pixel_delta() const { return pixel_delta_; }    // $B1FB
    bool at_end() const { return at_end_; }                     // $B25D
    int shown_screen() const { return shown_; }                 // $B1FD >> 4

    // For checks against the original.
    struct Probe {
        std::uint8_t tile_column, tile_char, fraction, star_counter;
        std::array<std::uint8_t, 3> progress, role;
    };
    Probe probe() const;
    // A drifting star ($B1B4 + 5 * band).
    struct Star {
        std::uint8_t offset{};   // byte within its band's characters (from $F80A + $90 * band)
        std::uint8_t position{}; // sub-character position (bits 5-6: the pixel pair)
        std::uint8_t speed_row{}; // row of the drift table ($B8A1): 0, 8, $10, $18
        std::uint8_t clock{};
        std::uint8_t twinkle{};  // 0, 4, 8, 12: brightness pattern row
    };
    const std::array<Star, sky_bands>& stars() const { return stars_; }

private:
    struct Screen {
        int left = 0;                        // map character column of screen column 0
        std::array<std::uint8_t, columns * rows> codes{};
    };
    std::uint8_t map_tile(int row, std::uint8_t column) const;
    std::array<std::uint8_t, rows> map_column(std::uint8_t tile_column, int tile_char) const;
    int step_fine(int speed);
    void fill_sky(std::array<std::uint8_t, rows>& column, std::uint8_t counter) const;
    void step_counter(int direction);
    void build_prepared(int screen);

    const data::GameImage& image_;
    std::uint16_t tile_base_{};
    std::array<Screen, 3> screens_{};
    std::array<std::uint8_t, 3> progress_{}; // $B200-$B202: chunks done (4 = ready)
    std::array<std::uint8_t, 3> role_{};     // $B203-$B205: 0 shown, 1 left, 2 right
    int shown_{};                            // $B1FD >> 4
    std::uint8_t tile_column_{}, map_row_{}, tile_char_{}; // $B209, $B20A, $B20B
    std::uint8_t wrap_left_{0xFF};           // $75DF
    std::uint8_t view_column_{}, fine_{}, fraction_{}, last_fine_{}, fine_scroll_{};
    std::int8_t wiz_offset_{}, step_{}, pixel_delta_{};
    bool at_end_{};
    bool blank_{};               // the first screen was drawn without landscape
    std::uint8_t screen_view_{}; // the view column the shown screen belongs to
    int sky_counter_at(int column) const; // the sky pattern's counter of a shown column
    std::set<std::uint32_t> opened_;      // carnage mode's opened cells (map keys)
    std::uint8_t star_counter_{0x51}; // $B27A
    std::array<std::uint8_t, sky_codes * 8> sky_{}; // the sky characters' bytes, from code $01
    std::array<Star, sky_bands> stars_{};
    void sky_put(int charset_offset, std::uint8_t value); // offset from $F800
};

} // namespace wizball::game
