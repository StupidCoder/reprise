#pragma once
#include "gba/display.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>
#include <functional>
#include <utility>
#include <vector>

// The picture as the game prepares it during a frame, and its hand-over to
// the display in the vertical blank (08000980 and 08000C54).
//
// The game never writes the display directly while it runs: it changes a
// copy of the display settings and colours, queues the sprites it wants
// drawn and fills a table of per-line scroll offsets; all of it reaches the
// display at the next vertical blank.
namespace sonic_advance_gba {

// A sprite entry queued for the sprite table, in one of 32 layers (layer 0
// is drawn in front). Within a layer the first queued entry is in front.
struct QueuedSprite {
    gba::Sprite sprite;
};

struct VideoState {
    // The next picture's settings: display control 030020E0, layer controls
    // 03001B70, scroll 030020D0, rotation 030011E0, windows 03001B30,
    // blending 030011A8.
    gba::Settings settings{};
    // Colours: background 03002120, sprites 03001920; copied to the display
    // when marked changed (flags 03001100 bits 0 and 1).
    std::array<u16, 256> bg_colors{}, sprite_colors{};
    bool bg_colors_changed{}, sprite_colors_changed{};

    // Per-line values (flag 4): the horizontal blank copies `line_bytes`
    // bytes per line from a table into display registers: a layer's
    // scroll (x, y; 4 bytes), one window's edges (2 bytes) or both
    // windows' edges (4 bytes). Entry n is for picture line n. The
    // original keeps two tables (03001420, 030016A0, 0x280 bytes each)
    // and alternates between them every frame (03001144 is the one being
    // filled); both are kept here because a frame that fills only some
    // lines shows the others from two frames ago.
    enum class LineTarget : u8 { scroll, window0, window1, windows };

    struct LineTable {
        std::array<u8, 0x280> bytes{};

        u16 halfword(unsigned offset) const {
            return u16(bytes[offset] | (bytes[offset + 1] << 8));
        }

        void put_halfword(unsigned offset, u16 v) {
            bytes.at(offset) = u8(v);
            bytes.at(offset + 1) = u8(v >> 8);
        }
    };

    std::array<LineTable, 2> line_tables{};
    u8 line_table{};          // the one being filled (03001144)
    bool line_effect_on{};    // flag 4, cleared after each frame
    LineTarget line_target{}; // 03002118
    u8 line_layer{};          // scroll: the layer
    u8 line_bytes{};          // 03002320

    LineTable& filling() { return line_tables[line_table]; }

    // Horizontal blank handlers asked for this frame (the list 03001F80,
    // flag 03001100 bit 3): each runs after picture line `line` (0-9Eh) is
    // drawn and may change the display settings from the next line on
    // (after that line's per-line values are in).
    std::vector<std::function<void(int line, gba::Settings&)>> hblank;

    // The water's split (0804C634, its vertical blank part 0804C92C and its
    // line interrupt 0804C9A0), asked for again every frame: from `line`
    // the colours are the underwater ones (background colours 0-E7h and
    // all sprite colours), layer 0 is off and layer 3's scroll comes from
    // the line table's entries from 0 on; at line 0 the whole picture is
    // under water (the ordinary line effect then still applies).
    // (On the act's first frame the original's loading overruns the frame:
    // the vertical blank's callback then copies the underwater colours into
    // colour memory a frame late. The pictures are the same.)
    struct WaterSplit {
        bool on{};
        u8 line = 0xFF;
        std::array<u16, 256> bg{}, sprites{};
        bool late{}; // the colour memory keeps the ordinary colours this frame
    };

    WaterSplit water;

    // Sprites queued this frame, by layer (03001110 heads, 03001B80 entries,
    // 030011B0 count; at most 128).
    std::array<std::vector<gba::Sprite>, 32> layers{};
    unsigned queued{};
    // The entries in the order they were queued (03001B80): some objects
    // copy an entry queued earlier in the frame and move it.
    std::array<gba::Sprite, 128> queued_entries{};
    // Sprite rotation/scaling parameter sets as set this frame (inside the
    // sprite table buffer 03004630, which starts and is reset after each
    // frame filled with the halfword 0200: every parameter 2.0).
    std::array<gba::Rotation, 32> rotations = blank_rotations();
    // 03002380: while paused, the sprite table's entries from here on are
    // the picture of the frame the pause began in.
    u8 kept_from{};
    // Attributes of sprite table entries cleared to 0 by stray writes into
    // the buffer this frame (entry, attribute 0-2); they show where the
    // frame's entries do not reach.
    std::vector<std::pair<u8, u8>> cleared_attributes;

    static std::array<gba::Rotation, 32> blank_rotations() {
        std::array<gba::Rotation, 32> r;
        r.fill({0x200, 0x200, 0x200, 0x200});
        return r;
    }
};

// How the frame's sprites reach the sprite table around a pause
// (03001100 bits 10 and 11).
enum class SpriteHold : u8 {
    none,     // all of the table is the frame's
    starting, // the pause began this frame: the frame's entries move to the
              // end of the table and stay there while paused
    held,     // paused: the frame's entries (the pause's) go in front of the
              // ones kept
};

// 08000980 + 08000C54: hands the frame's picture to the display and resets
// what is renewed every frame (not while a pause is set: the line table,
// line effect and rotation sets then stay as they are).
void commit_video(VideoState& v, gba::Display& d, SpriteHold hold = SpriteHold::none,
                  bool renew = true);

// Fills lines [first, last) of the per-line table with scroll (x, y) for
// background `layer` and switches the effect on for this frame (0800681C).
void line_scroll_fill(VideoState& v, u8 layer, u8 first, u8 last, u16 x, u16 y);
// 080067A0: lines [first, last) all show the same background line: the
// vertical offset starts at y - first and goes down by one per line.
void line_scroll_stretch(VideoState& v, u8 layer, u8 first, u8 last, u16 x, u16 y);
// 080068A0: alternate lines shifted by +offset and -offset, the offset
// changing by `step` each line (a "venetian blind" slide).
void line_scroll_interleave(VideoState& v, u8 layer, u8 first, u8 last, s16 offset, s16 step, s16 x,
                            u16 y);

// 08002E58: sets a rotated layer's matrix for `angle` (0-3FF) and scale
// (8.8, x and y), with picture point (sx, sy) showing map point (cx, cy).
void rotate_layer(gba::Background& bg, const Assets& a, u16 angle, s16 scale_x, s16 scale_y, s32 cx,
                  s32 cy, s32 sx, s32 sy);

// 08003710: sprite rotation set `set` for `angle` and the scales (8.8).
void set_rotation(VideoState& v, const Assets& a, u16 angle, s16 scale_x, s16 scale_y,
                  unsigned set);

// 080047FC + the entry copy of 08004158: queues a sprite table entry in
// `layer` (0-31). Returns false when the table is full (128 entries).
bool queue_sprite(VideoState& v, unsigned layer, const gba::Sprite& s);

} // namespace sonic_advance_gba
