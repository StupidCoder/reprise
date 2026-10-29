#pragma once
// The third adventure's talk window (the event's "lt3_msg_talk" message;
// overlay 6, class at 020c3dd8..020c4fc4): a conversation from the event's
// text file shown line by line on the lower screen.
//
// How it plays: 4620 {message, number} picks the lines with that number from
// txt/?/GG.cpck/GG_IIIIII (6001 {number, speaker, text}). For each speaker
// in turn the window fades in (15 frames) with its tail towards the speaker
// standing on the upper screen and the speaker's name plate on top; the
// lines reveal one step a frame with the speaker's typing sound and mouth
// moving, each waiting for a tap (a pen blinks at the corner). The next line
// of the same speaker follows at once; another speaker means fading out and
// in again. After the last line the window fades out and the message is done
// (condition 4310).
//
// Markup handled here (see graphics/a3_message.hpp): <V#> plays the
// event's voice line # (str/evt/?/GG_IIIIII_####), <S#> an effect, <J#> a
// jingle, <A#/#> sets the speaker's animation ("aa_bb_..."), <E#> runs the
// event script's section named "#" alongside, <N#> inserts a name.
//
// Reconstructed from 020c47cc (start), 020c497c (each frame), 020c3e9c (the
// markup), 020c4588 (speaking), 020c4fc4 (name plate), 020c41fc (lines).
#include "system/a3_lscr_scene.hpp"
#include <optional>

namespace picarat::adventure3 {

class Story;

class TalkWindow : public lscr::MessageBehaviour {
public:
    // The lines of event (group, id) (020f2ce4).
    TalkWindow(Story& story, int group, int id);

    // 4620: shows the lines numbered `number`.
    bool show(lscr::Scene& scene, lscr::Object& message, int number);

    void update(lscr::Scene& scene, lscr::Object& message) override;
    void draw(const lscr::Scene& scene, Surface& screen, lscr::Screen which,
              const lscr::Object& message) const override;

    bool done(const lscr::Object&) const override { return state_ == 0 || state_ == 8; }

    // The `index`-th line numbered `number`: speaker and text (4661).
    std::optional<std::pair<std::string, std::string>> line(int number, int index) const;

private:
    struct Line {
        int number = 0;
        std::string speaker, text;
    };

    lscr::Object* speakerObject(lscr::Scene& scene) const;
    bool speaking(const lscr::Object& message) const;
    bool onStep(lscr::Scene& scene, lscr::Object& message, const MessageStep& step);

    Story& story_;
    int group_ = 0, id_ = 0;
    std::vector<Line> lines_;
    int state_ =
        0; // +0x140: 1 speaker, 2 window in, 3 opening, 4 text, 6 next speaker, 7 closing, 8 done
    int current_ = 0;     // +0x150
    int last_ = -1;       // +0x148
    int direction_ = 0;   // +0x70c: the tail (0 middle, 1..3 left, 4..6 right)
    std::string speaker_; // the speaker's character name, "" none
    const Picture* plate_ = nullptr;
    lscr::Object window_, pen_;
    bool voicePlaying_ = false, voiceEnded_ = false;
    std::unique_ptr<lscr::Thread> sectionThread_; // <E#> (+0x154)
};

} // namespace picarat::adventure3
