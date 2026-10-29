#pragma once
// The third adventure's messages: a text with markup, laid out once and then
// revealed step by step (the game's message object, overlay 0 0206xxxx).
//
// How a message works
// -------------------
// Setting the text lays it out into steps (0206ad94, 02069f5c): one per
// line (its place and width), per glyph (code, colour, place) and per
// markup tag. Revealing then walks the steps at a speed (steps per frame,
// 4096 = one): only glyphs and waits take time, everything else happens
// as it is reached. The markup:
//
//   \n            a new line
//   {xy}          an accented letter (the game's escape table)
//   <W> <W#>      wait 60 / # frames; <WT> wait for a tap (unless a voice
//                 is open)
//   <T> ... </T>  a part a tap skips to the end of
//   <C?> </C>     colour (a stack of seven): B 1, C 0, F 6, G 5, O 7,
//                 R 3, S 4, T 8, W 2, others 0
//   <K> </K>      the speaker does not move its mouth (thinking)
//   <Q> </Q>      no typing sound
//   <S#> </S#>    play effect # / wait for it to end
//   <J#> </J#>    a jingle / wait for the music
//   <V#> </V>     a voice line starts / ends
//   <E#> <P#> <F#> <A#/#>  speaker expression, pose, face, animation
//   <N#> <I#>     insert a name (the player's, the second one) / an item
//
// What effects, voices and the speaker's animations do is up to the owner
// (the talk window); a plain message ignores them. Tapping while a <T> part
// is being revealed jumps to its end; a wait for a tap ends with a tap.
//
// Layout: glyph advance + character spacing; line height = font height +
// line spacing; centring moves a line left by half its width rounded down
// (an arithmetic shift) and the text up by half its height rounded up.
#include "graphics/graphics.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace picarat {
class GameText;
}

namespace picarat::adventure3 {

struct MessageStep {
    enum Kind : std::uint8_t {
        Line = 1,
        Glyph = 2,
        Inline = 3, // takes neither time nor space (unused by the scripts)
        Wait = 4,
        PartBegin = 5,
        PartEnd = 6,
        Effect = 7,
        Jingle = 8,
        Voice = 9,
        Expression = 10,
        Quiet = 11,
        Thinking = 12,
        Pose = 13,
        Face = 14,
        Animation = 15,
    };

    Kind kind = Line;
    int a = 0, b = 0;     // numbers (Wait frames, -1 for a tap; PartBegin's end)
    bool closing = false; // </S#>, </J#>, </V>, </K>, </Q>
    unsigned code = 0;    // glyphs
    int colour = 0;       // glyphs: an index into the message palette
    int x = 0, y = 0;     // glyphs and lines (a line: x its left, a its width)
};

class MessageText {
public:
    struct Style {
        const Font* font = nullptr;
        int fontHeight = 10;
        int characterSpacing = 1, lineSpacing = 2; // 4641
        int baseline = 0;                          // +0xee
        bool centerX = false, centerY = false;     // +0xf0 bits 0 / 1 (4643 / 4644)
        bool waitAtEnd = false;                    // +0xf0 bit 4: end with a wait for a tap
        int speed = 0x2000;                        // steps per frame, 4096 = one (0206b488)
        int defaultColour = 0;
    };

    // What the owner does with the markup (talk windows): an effect step
    // returns false to hold the reveal there (waiting for a sound); `insert`
    // gives the text of <N#> / <I#>.
    struct Hooks {
        std::function<bool(const MessageStep&)> step;
        std::function<std::string(char tag, int number)> insert;
        std::function<void(int sound)> typing;
    };

    Style style;
    Hooks hooks;

    // 0206ad94: lays the text out; `instant` shows it all at once.
    void set(const std::string& text, const GameText& escapes, bool instant = false);
    void clear();

    // 0206b5b4: one frame of `elapsed` (4096 = a frame). `tapped`: the
    // player tapped or pressed A this frame; `instant`: reveal everything
    // (the owner's skip).
    void update(bool tapped, bool instant = false, int elapsed = 0x1000);

    // Waiting for a tap (+0xf2 bit 4).
    bool waitingForTap() const { return waitingTap_; }

    // All steps revealed and not waiting (02069bcc).
    bool finished() const { return !waitingTap_ && shown_ >= steps_.size(); }

    bool empty() const { return steps_.empty(); }

    // Quiet (<Q>, +0xf2 bit 9) and thinking (<K>, bit 8).
    bool quiet() const { return quiet_; }

    bool thinking() const { return thinking_; }

    // Typing sound (0206b4e0): `sound` every `interval` frames while
    // glyphs appear.
    void setTyping(int sound, int interval) {
        typingSound_ = sound;
        typingInterval_ = interval;
        typingCount_ = 10000;
    }

    // The glyphs revealed so far, drawn with their top-left at (x, y);
    // colours from `palette` (index -> RGB555).
    void draw(Surface& image, int x, int y, const std::vector<std::uint16_t>& palette,
              int alpha = 31) const;

    const std::vector<MessageStep>& steps() const { return steps_; }

    std::size_t shown() const { return shown_; }

private:
    std::vector<MessageStep> steps_;
    std::size_t shown_ = 0;
    int progress_ = 0;  // fixed point, steps (+0xe8)
    int countdown_ = 0; // a wait's frames left (+0xf4)
    int partEnd_ = 0;   // the open part's end (+0xe0)
    bool inPart_ = false, waitingTap_ = false, quiet_ = false, thinking_ = false;
    int typingSound_ = -1, typingInterval_ = 0, typingCount_ = 10000;
};

} // namespace picarat::adventure3
