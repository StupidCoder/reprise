#pragma once

// The first adventure's WiFi corner (from the secret menu):
//   0x1d menu          02036424 / run 020367a8 / 02036668
//   0x1e puzzles       020368ec / run 02037b0c / 02036f6c (route 02002690)
//   0x20 settings      0200274c: the console's WiFi utility (overlay code of
//                      the system library, not the game's)
//   0x21 download      02037d64 / run 02038f38 / 020381e8
//
// How it plays
// ------------
// New puzzles came over Nintendo's WiFi service: the download screen
// connected, received the list of puzzles to unlock (they are on the
// cartridge already) and kept it in the backup. The list page shows the
// puzzles received with their dates, to play like the index's.
//
// The service has been gone since 2014 and the port has no WiFi: the
// download screen runs its connection screens and ends as the cartridge
// does when no access point answers (error 52000, message err9, after about
// 1500 frames: measured on the reference emulator), and the settings
// utility is not there (scene 0x20 returns at once). Puzzles a cartridge received before are in its
// backup and show.
namespace picarat::adventure1 {
class Story;

void wifiMenu(Story& story);     // 0x1d
void wifiPuzzles(Story& story);  // 0x1e
void wifiSettings(Story& story); // 0x20
void wifiDownload(Story& story); // 0x21

} // namespace picarat::adventure1
