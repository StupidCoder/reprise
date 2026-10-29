#include "platform/resources.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <stdexcept>
#include <string>

namespace platform {
std::vector<std::uint8_t> resource(std::string_view relative) {
    const char* raw_base = SDL_GetBasePath();
    const std::string base = raw_base ? raw_base : "";
    const std::string name(relative);
    // macOS: Resources is SDL's base. Loose builds: resources beside binary.
    // Android: SDL_LoadFile opens relative paths from packaged assets.
    for (const auto& path :
         std::array{base + name, base + "resources/" + name, name, "resources/" + name}) {
        std::size_t size{};
        if (void* data = SDL_LoadFile(path.c_str(), &size)) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            std::vector<std::uint8_t> result(bytes, bytes + size);
            SDL_free(data);
            return result;
        }
    }
    throw std::runtime_error("Cannot load BlueStreak resource: " + name);
}
} // namespace platform
