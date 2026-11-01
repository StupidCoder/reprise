// Reprise's large-image import adapter; all reads use the shared document-aware reader.
#pragma once
#include "app/game_entry.hpp"
namespace captain_toad::reprise {
std::string import_into(::platform::FileSource& image, const std::filesystem::path& folder,
                        const ::app::ImportProgress& progress);
}
