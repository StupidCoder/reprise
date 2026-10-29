#pragma once

// The first adventure's bonus section: the title's "Bonus" button loads a
// saved game (0202e050) and opens the secret menu (scene 0x24). Every page
// is an object the router builds and runs (02000d28); each sets the next
// scene when it closes.
//
// How it plays
// ------------
// The secret menu offers the WiFi corner, the puzzle index (all solved
// puzzles to play again), the challenges (once unlocked in the story) and
// the extras (once the story is finished). The extras hold the character
// files, the art gallery, the music and voice rooms and the movie theatre,
// each unlocked by enough picarats, and the passcode page: the code shown
// to the console's owner elsewhere unlocks a last event.
//
// Scenes and objects (ARM9):
//   0x24 secret menu        0203b86c / run 0203bc20 / 0203badc
//   0x0d extras             0202ea30 / 0202eff4 / 0202ee10
//   0x0e character files    0202f188 / 0202f7f4 / 0202f498
//   0x0f movie theatre      0202f944 / 0202fca0 / 0202faec
//   0x11 art gallery        02030270 / 02030814 / 02030460
//   0x12 music room         0203090c / 02031194 / 02030ccc
//   0x13 voice room         02031250 / 02031af0 / 020315cc
//   0x14 passcode notes     02031cdc / 02031e44 / 02031dd0 (no way leads here)
//   0x25 passcode           0203a000 / 0203a8bc / 0203a350 (route 02002aac)
#include <cstdint>

namespace picarat::adventure1 {
class Story;

void secretMenu(Story& story);     // 0x24
void extrasMenu(Story& story);     // 0x0d
void characterFiles(Story& story); // 0x0e
void movieTheatre(Story& story);   // 0x0f
void artGallery(Story& story);     // 0x11
void musicRoom(Story& story);      // 0x12
void voiceRoom(Story& story);      // 0x13
void passcodeNotes(Story& story);  // 0x14
void passcodePage(Story& story);   // 0x25's page (0203a000)

// The theatre's replay of movie n (1..16) outside the page (the shell's
// movie player): its script (event/m<n>.gds), then the movie as scene 0x22
// with the theatre as the scene to return to, so a touch skips it.
void replayMovie(Story& story, int movie);

// The music room's pieces (0x12, table 020d4da4): piece 1 streams
// BGM_09.SAD, the others play these sequences.
inline constexpr std::uint8_t kMusicRoomSequences[19] = {9,  17, 1, 2,  3,  4,  5,  14, 6, 8,
                                                         13, 10, 7, 12, 15, 11, 16, 18, 19};

} // namespace picarat::adventure1
