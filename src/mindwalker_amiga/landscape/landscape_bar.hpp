#pragma once
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"

// The landscape stage's top bar (A's window, NewWindow $204B1E): the pause
// box, the progress slider (the stage), the life figures, the compass letters
// (the facing one in pen 31) and the emblem, on the window's title bar in its
// BlockPen (18). The images take S's shared icons as A's set-up ($200040)
// puts them in. Intuition's window close gadget sits at the left (x 0-13).
namespace mindwalker::landscape {

class LandscapeBar {
public:
    explicit LandscapeBar(const data::GameFiles&);

    // $203E74: figure i shows while i + 1 < lives; $203EF0: the facing
    // direction's letter in pen 31; $204312: the slider's pot = stage * $618.
    void set(int lives, int direction, int stage);
    // OffGadget / OnGadget: the emblem (id 4) is off while the stage is
    // paused ($204106) and ghosted.
    void enable(int id, bool on) { bar_.find(id)->enabled = on; }
    void draw(screen::Canvas&) const;
    // The gadget under (x, y), or -1; -2 for Intuition's close gadget.
    int gadget_at(int x, int y) const;

private:
    screen::GadgetBar bar_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(bar_); }
};

} // namespace mindwalker::landscape
