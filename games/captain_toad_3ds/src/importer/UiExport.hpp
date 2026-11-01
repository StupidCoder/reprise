// UI packages: a layout archive of the game (UIX2/Eu/<name>.arc with its <name>.json screen
// definitions) converted for the layout runtime, plus the English (EUen) message files. Built from
// the verified ROM copy on first use and cached in DATA/Ui/<name>, like the stages.
//
// Package contents:
//   ui.json             the archive's screen definitions (each UI "screen" names its top and bottom
//                       layout, perspective field of view, layer and buttons) and the export index;
//   layouts/<L>.json    every BFLYT with its BFLAN animations, texture and font references;
//   layouts/textures/   decoded BFLIM rasters (RGBA, stored orientation);
//   layouts/fonts/      BFFNT glyph sheets and metrics;
//   messages/<G>/<M>.json  every MSBT of LayoutMessage and SystemMessage (G), as readMsbt;
//   packs/<P>/<T>.rgba  (CourseSelection) the illustration and screenshot texture packs
//                       UIX2_Textures/<P>_textures.ctpk, indexed in ui.json "packs".
#pragma once
#include "Inputs.hpp"

#include <filesystem>
#include <string>

namespace captain_toad {

constexpr int kUiSchema = 7;

void exportUi(Inputs& in, const std::string& archive, const std::filesystem::path& out);

std::filesystem::path uiDirectory(const std::filesystem::path& dataDirectory, const std::string& archive);
// Builds the UI package unless it is ready; returns its directory.
std::filesystem::path buildUi(const std::filesystem::path& dataDirectory, const std::string& archive);

}   // namespace captain_toad
