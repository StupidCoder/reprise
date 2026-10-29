#pragma once

// The first adventure's challenges (scene 0x1c, from the secret menu; ARM9
// 02034c60 / run 02035b8c / 0203509c, the challenge's page 02035680).
//
// How it plays
// ------------
// Five challenges, opened by the story (flags 507, 508, 509 and 512) and,
// the last, by solving every puzzle of the story. Each holds three puzzles
// (121..135): touching one runs its event (350 + 3 * challenge + row), which
// plays the puzzle and comes back here, the same challenge still open. A
// solved puzzle carries the challenge's mark.
namespace picarat::adventure1 {
class Story;

void challengeMenu(Story& story);

} // namespace picarat::adventure1
