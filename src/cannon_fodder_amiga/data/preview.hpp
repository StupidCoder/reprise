#pragma once
#include "app/game_entry.hpp"
#include "cannon_fodder_amiga/data/assets.hpp"
#include "platform/image.hpp"
#include <filesystem>
#include <vector>

// Pictures of the decoded data for the launcher's viewer and the asset tool.
namespace fodder::data {

platform::Color to_color(amiga::Rgb4 colour);

platform::Image picture_image(const amiga::IlbmPicture& picture);
platform::Image palette_image(const Palette& palette);
// A tile set as a sheet, 20 tiles per row.
platform::Image tile_sheet(const std::vector<Tile>& tiles, const Palette& palette);

// A whole map drawn with its tile sets and the base set's palette.
platform::Image map_image(const Content& content, int number);

// Shrinks by averaging so the picture fits in width x height (no change when
// it already fits).
platform::Image fit(const platform::Image& image, int width, int height);

// Every preview of an installed content folder; `full_size_maps` keeps maps
// at one pixel per pixel (the export tool), otherwise they are fitted to the
// viewer.
std::vector<app::Preview> previews(const std::filesystem::path& content, bool full_size_maps = false);

} // namespace fodder::data
