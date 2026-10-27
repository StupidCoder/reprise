#include "mindwalker_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <string>
#include <string_view>

namespace mindwalker::import {
namespace {

// The release this was recovered from ("Mind Walker (1986)(Commodore).adf",
// SHA-256 2e83a67a…a8969c).
constexpr std::string_view release_name = "Commodore disk (1986)";
constexpr std::string_view volume_name = "MWDisk";

struct Program {
    const char* name;
    std::string_view sha256;
};

constexpr std::array programs = {
    Program{"MindWlkS", "78b315fa3f4d019a3abbb53ed9f7e35a9ba14702a86508bafdd163c576b9b939"},
    Program{"MindWlkT", "1d42ddd2525b2a15d9816bc94dce8106d6149cfa911c546503d744a901c8b7a5"},
    Program{"MindWlkA", "d15335a1488f590e9e8abbb25dc6210f85d21c5b4a2a4b6901178631b5d15f50"},
    Program{"MindWlkB", "a9eda8997f270e1a817af69140e6cd4d18e3cf722521512ac057148ac503cf95"},
    Program{"MindWlkC", "9c3cb8ffe97567b53bff20f0ce17eda724d9472b2965d83fa281e422ead2bf30"},
    Program{"MindWlkP", "51920792fe37e659e0f071bf3463838c01334718dabd05082e7e021cb285fa63"},
    Program{"MindWlkE", "346f659426aed9a7d03e53cc72f9041e5a9db58c17b31fc160b0af2bf9d25f4c"},
};

struct DataFile {
    const char* name;
    std::size_t size;
};

// The data files the stages open by name ("MWDisk:<name>"), with the sizes
// they read (T $602DD8, P $50574C, T $602196).
constexpr std::array data_files = {
    DataFile{"MindB", 32000},  DataFile{"MindF1", 8000}, DataFile{"MindF2", 8000},   DataFile{"MindF3", 8000},
    DataFile{"MindF4", 8000},  DataFile{"MindF5", 8000}, DataFile{"MindF6", 8000},   DataFile{"MindHighs", 256},
};

// Workbench's preferences on the disk: Intuition draws its mouse pointer from
// them (PointerMatrix, offsets and colours 17-19).
constexpr std::string_view preferences_file = "devs/system-configuration";
constexpr std::size_t preferences_size = 232;

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const amiga::Volume volume(image);
    if (volume.name() != volume_name || volume.find("Mind Walker") == nullptr || volume.find("MindWlkA") == nullptr) {
        throw app::NotThisGame("This disk is not Mind Walker.");
    }
    app::ImportResult result;
    result.release = std::string(release_name);
    for (const auto& program : programs) {
        auto bytes = volume.read(program.name);
        if (platform::sha256(bytes) != program.sha256) {
            throw std::runtime_error("This disk holds a different release of Mind Walker, which is not supported yet.");
        }
        result.files.push_back({program.name, std::move(bytes)});
    }
    for (const auto& file : data_files) {
        auto bytes = volume.read(file.name);
        if (bytes.size() != file.size) {
            throw std::runtime_error(std::string("The disk's ") + file.name + " is damaged.");
        }
        result.files.push_back({file.name, std::move(bytes)});
    }
    auto preferences = volume.read(preferences_file);
    if (preferences.size() != preferences_size) {
        throw std::runtime_error("The disk's devs/system-configuration is damaged.");
    }
    result.files.push_back({"system-configuration", std::move(preferences)});
    return result;
}

} // namespace mindwalker::import
