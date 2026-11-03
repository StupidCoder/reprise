#pragma once
#include "app/game_entry.hpp"
namespace beacon::reprise {
std::string import_package(platform::FileSource&, const std::filesystem::path&, const app::ImportProgress&);
}
