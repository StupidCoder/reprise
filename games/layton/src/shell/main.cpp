// Picarat's entry point.
//
//   picarat                          the app (library, import, the game)
//   picarat --library-root DIR       keep the imported game in DIR (for tests)
//   picarat --self-test OUT [--assets PACK]
//                                    headless check of the opening and the
//                                    first puzzles (see self_test.cpp)
#include "platform/host.hpp"
#include "shell/app.hpp"
#include "shell/library.hpp"
#include "shell/self_test.hpp"
#include <iostream>
#include <string>

// Desktops start at main(). On Android the Java activity loads the app as a
// library and calls SDL_main, which SDL_main.h makes of main().
#include <SDL3/SDL_platform_defines.h>
#ifndef SDL_PLATFORM_ANDROID
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
    using namespace picarat;
    std::filesystem::path libraryRoot, selfTestOutput, assets;
    bool testRoot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--library-root" && hasValue) {
            libraryRoot = argv[++i];
            testRoot = true;
        } else if (arg == "--self-test" && hasValue) {
            selfTestOutput = argv[++i];
        } else if (arg == "--assets" && hasValue) {
            assets = argv[++i];
        } else if (arg.rfind("-psn_", 0) == 0) {
            // macOS Finder may pass a process serial number; ignore it.
        } else {
            std::cerr << "usage: picarat [--library-root DIR] [--self-test OUT [--assets PACK]]\n";
            return 2;
        }
    }
    try {
        if (libraryRoot.empty()) {
            libraryRoot = platform::Host::dataDirectory("Picarat");
        }
        if (!selfTestOutput.empty()) {
            return shell::selfTest(selfTestOutput,
                                   assets.empty() ? libraryRoot / shell::kPackFolder : assets);
        }
        platform::Host host("Picarat", 1080, 720, 720, 540);
        shell::App app(host, libraryRoot, testRoot);
        app.run();
    } catch (const std::exception& error) {
        std::cerr << "Picarat: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
