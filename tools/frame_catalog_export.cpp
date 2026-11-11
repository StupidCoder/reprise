#include "app/catalog.hpp"
#include "ui/shelf.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: frame_catalog_export OUTPUT_DIRECTORY");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        const auto entries = app::catalog();
        constexpr int width = 512, height = 352, columns = 4;
        const int rows = int((entries.size()+columns-1)/columns);
        platform::Image atlas(columns*width, rows*height);
        std::fill(atlas.pixels.begin(), atlas.pixels.end(), platform::Color{20,22,25,255});
        std::ofstream manifest(destination/"catalog.tsv");
        manifest << "REPRISE_XR_CATALOG_V1\t" << width << '\t' << height << '\t' << columns << '\n';
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& game = entries[i];
            const int x = int(i%columns)*width, y = int(i/columns)*height;
            ui::shelf::cover(atlas, game.id, x+6, y+6, width-12, 282);
            ui::shelf::clipped(atlas, game.title, x+18, y+299, width-36, 27,
                               {240,236,224,255}, true);
            manifest << game.id << '\t' << game.title << '\n';
        }
        manifest.close();
        if (!manifest) throw std::runtime_error("Cannot write catalog manifest");
        SDL_Surface* surface = SDL_CreateSurfaceFrom(atlas.width, atlas.height,
            SDL_PIXELFORMAT_RGBA32, atlas.pixels.data(), atlas.width*4);
        if (!surface) throw std::runtime_error(SDL_GetError());
        const bool saved = SDL_SavePNG(surface, (destination/"cards.png").c_str());
        SDL_DestroySurface(surface);
        if (!saved) throw std::runtime_error(SDL_GetError());
        std::printf("Exported %zu real catalog cards (%dx%d atlas); no game data\n",
                    entries.size(), atlas.width, atlas.height);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
