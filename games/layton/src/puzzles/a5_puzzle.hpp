#pragma once
// The fifth adventure's puzzle session (lt5CLoopNazo, loop 3000) and its
// standard play loop (lt5CLoopNazoCommon, loop 3100).
//
// How a puzzle runs
// -----------------
// The session's start (0x18394c) loads the puzzle frame's scripts
// (seq/prog_hint, prog_memo, prog_nazo, nazo_intro, nazo_quit, nazo_judge),
// the puzzle's own script (res/naz/seq/n<id>.xq) and its texts
// ("nazo_text": txt/@/50/50_<id>.xs), then runs the block "Init" (0x183fac;
// prog_nazo's). Block lookups search the newest script first, so a puzzle
// script's blocks override the frame's.
// Its update (0x18320c) then asks for the play loop by the puzzle's type
// (types without an entry: 3100), giving it the puzzle id, number, kind and
// the session. The play loop runs "Main" (0x29707c; prog_nazo's): the title
// card (nazo_intro), the puzzle's own blocks (NazoRes, NazoObj, NazoInp,
// NazoJdg ...), the verdict through the variable "ナゾ判定" and the
// judgement scene (nazo_judge). When the play loop ends, the session
// updates the progress variables from the verdict ("ノーヒント正解数",
// "連続ナゾ解き中", "あとまわし回数" ...) and ends.
#include "system/a5_game.hpp"
#include <memory>

namespace picarat::adventure5 {

class PuzzleSession {
public:
    // `standalone`: a puzzle played on its own (the puzzle index): a new
    // game's state first. In the story the state is the story's.
    PuzzleSession(Game& game, int puzzleId, bool standalone = true);
    // Whether the port plays a puzzle of this catalogue type: the types on
    // the shared play loop 3100 (0x18320c's switch: 1..8, 20, 44, 45, 48)
    // and the special loops reconstructed so far (a5_special.hpp). The
    // sixth adventure: the types on its standard loop (a6_puzzle.hpp).
    static bool playable(int type, Edition edition = Edition::Fifth);

    // One frame; false once the session has ended.
    bool frame(const Input& input);
    // The session's part of a frame (its update and scripts), for a loop
    // stack that runs the frame around it; false once it has ended.
    bool update();

    bool ended() const { return state_ == State::Ended; }

    const PuzzleRecord& record() const { return *record_; }

    xq::Thread& thread() { return *thread_; }

private:
    enum class State { Init, Play, Ended };
    Game& game_;
    const PuzzleRecord* record_ = nullptr;
    std::unique_ptr<xq::Thread> thread_, update_;
    State state_ = State::Init;
};

} // namespace picarat::adventure5
