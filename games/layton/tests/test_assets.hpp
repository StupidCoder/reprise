#pragma once
// Tests that read game data take a pack made by `picarat_import --pack`.
// Without one they exit with 77, which CTest reports as skipped.
#include <cstdlib>
#include <filesystem>
#include <iostream>

inline void requireTestPack(int argc, char** argv) {
    if (argc < 2 || !std::filesystem::exists(std::filesystem::path(argv[1]) / "provenance.json")) {
        std::cout
            << "skipped: no asset pack (run picarat_import --pack ROM <build>/native-assets)\n";
        std::exit(77);
    }
}
