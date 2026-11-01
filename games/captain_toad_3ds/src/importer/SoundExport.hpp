// Front-end sound package: what the course book, its touch screen and the course title card play,
// built from the verified ROM copy on first use and cached in DATA/Sound/FrontEnd.
//
// Package contents (files copied unmodified unless stated):
//   sound_data.bcsar, SNDTBLS.DAT, SFXSCRIPTFILES.DAT, STREAMFILEINFO.DAT, SND3DLIST.DAT,
//   ADSRINFO.DAT          the sound archive and the middleware tables (SoundData/);
//   stream/<S>.dspadpcm.bcstm  the book's music, one per season (Stm_Kp_bgm_Book01..04);
//   sedb.json             the SeDataBase entries of the front-end actors (CourseBook, its pages,
//                         the cover, the buttons, ProductSequence, WipeStart, ...), in the format
//                         of the opening's sedb.json (audio/OpeningSoundKeeper.hpp);
//   bgm.json              BgmDataBase: the "CourseSelect" play entries (season -> stream) and the
//                         situations (ChangeVolume target and step) the front end uses.
#pragma once
#include "Inputs.hpp"

#include <filesystem>

namespace captain_toad {

constexpr int kFrontEndSoundSchema = 2;

void exportFrontEndSound(Inputs& in, const std::filesystem::path& out);
// Builds the package unless it is ready; returns its directory.
std::filesystem::path buildFrontEndSound(const std::filesystem::path& dataDirectory,
                                         const std::filesystem::path& cacheDirectory);

}   // namespace captain_toad
