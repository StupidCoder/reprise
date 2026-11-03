#pragma once
#include "app/game_entry.hpp"
namespace beacon::reprise {
std::string import_assets(platform::FileSource&, const std::filesystem::path&, const app::ImportProgress&);
}
