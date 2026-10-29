#pragma once
// The 3DS adventures' message engine: how a text object's text appears
// (the text component, vtable 0x3ff2d8 in the fifth adventure).
//
// How a message plays
// -------------------
// Setting a text (0x1703dc -> 0x172c20) splits it into runs of characters
// and commands in angle brackets ("<T>", "<W>", "<M2/1/1/45>": a name of
// letters, then values separated by '/'; "</T>" closes; 0x174934) and turns
// them into a timeline of items (0x172190 for the plain commands and the
// characters, 0x16db60 for the talk commands). Each frame (0x170154 ->
// 0x175330) the timeline moves on: a character costs one step of the
// speed (at speed 1 one character a frame, never more than one unless the
// text is being skipped), "<W n>" waits n frames, a page mark waits for a
// tap, "<T>"..."</T>" is a stretch a tap shows at once, the talk commands
// ask the game to play sounds, voices and the speaker's poses and motions.
// While characters appear a typing sound plays every few frames.
//
// The script talks to it through 5911 (set), 5913 (tap), 5920 (all shown),
// 5922 (waiting at a mark), 5951 (speed), 5953 (typing sound), 5955 (the
// speaker).
#include "system/a56_xq.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picarat::adventure56 {

// Who receives the message's events (the game: its vtable +0xa8, 0x33336c
// in the fifth adventure). Codes: 1 play effect, 3 / 4 / 5 voice queries,
// 7 a stream on player 2, 8 the event's voice, 10 stop a voice, 0xc,
// 0xf a script call, 0x10 the speaker's typing sound, 0x13 pose, 0x14
// face, 0x15, 0x16 3D motion, 0x17, 0x18 lock, 0x19 a character's name.
class MessageHost {
public:
    virtual ~MessageHost() = default;
    virtual xq::Value messageEvent(int code, std::span<const xq::Value> args) = 0;
};

class MessageFlow {
public:
    // A glyph of the text as it is drawn.
    struct Glyph {
        std::uint32_t code = 0;
        int colour = -1; // palette index, -1 the text's own
        int line = 0;
        bool ruby = false; // a reading above the line (not drawn yet)
    };

    // 0x1703dc: a new text. `instant`: all at once (+0x166); `waitAtEnd`: a
    // tap mark after the last item (+0x163).
    void set(const std::string& text, bool instant, bool waitAtEnd, MessageHost* host);
    // 0x1720ac: no text.
    void clear();
    // 0x170154: one frame (`step` frames of time).
    void update(float step);
    // 5913 (0x17742c): a tap (+0x162).
    void tap() { tapped_ = true; }

    // 5920 (vtable +0x78, 0x33d6e0): everything has played.
    bool done() const;
    // 5922 (+0x80, 0x33d438): waiting at a page mark for a tap.
    bool waitingMark() const;
    // 5921 (+0x7c, 0x33cd10): the speaker's mouth moves: not held by <K>;
    // while a voice plays (*hypothesis*: whenever it plays - the original
    // compares its loudness with 0.125), else while characters appear.
    bool talking() const;
    // 5951 (+0x74, 0x170604): characters per frame; the typing sound's
    // interval follows (1 / speed rounded, at least 4).
    void setSpeed(float speed);
    // 5953 (0x2c43b8): the typing sound and its interval (frames; 5).
    void setTypingSound(int sound, int interval);

    int speaker = 0; // +0x170 (5955): the speaking character's id

    const std::vector<Glyph>& glyphs() const { return glyphs_; }

    // Glyphs shown (+0x15c).
    int shown() const { return shown_; }

    bool empty() const { return items_.empty(); }

private:
    struct Item {
        int type = 0;
        std::uint32_t glyph = 0; // types 0..2, 4: the glyph's index in glyphs_
        std::vector<xq::Value> values;
        bool close = false;
    };

    void command(const std::string& name, bool close, std::vector<xq::Value> values);
    void plain(const std::string& text);
    bool play(const Item& item); // 0x16e678 (false: blocked)
    bool voiceBusy(int which) const;
    xq::Value event(int code, std::vector<xq::Value> args) const;
    float number(const Item& item, std::size_t i, float fallback = 0) const;

    MessageHost* host_ = nullptr;
    std::vector<Item> items_;
    std::vector<Glyph> glyphs_;
    int line_ = 0, colour_ = -1;
    std::size_t index_ = 0; // +0x15a
    int shown_ = 0;         // +0x15c
    float wait_ = 0;        // +0x100
    float progress_ = 0;    // +0x104
    float speed_ = 1;       // +0x108
    float savedSpeed_ = 1;  // +0x10c
    float clock_ = 0;       // +0x478
    float delay_ = 0;       // +0x474
    int typingSound_ = -1;  // +0x466
    int typingEvery_ = 0;   // +0x468
    int typingCount_ = 0;   // +0x46a
    bool printing_ = false; // +0x160
    bool atMark_ = false;   // +0x161
    bool tapped_ = false;   // +0x162
    bool waitAtEnd_ = false; // +0x163
    bool stretch_ = false;  // +0x164: inside <T>, a tap skips
    bool skipping_ = false; // +0x165
    bool instant_ = false;  // +0x166
    bool keep_ = false;     // +0x461 (K)
    bool quiet_ = false;    // +0x462 (Q, voices)
    bool voiceWait_ = false; // +0x463
    bool jingle_ = false;   // +0x465
};

} // namespace picarat::adventure56
