#include "data/game_files.hpp"

#include "data/mach_o.hpp"
#include "data/obfuscated_file.hpp"
#include "data/program_layout.hpp"

#include <fstream>

namespace doc::data {
std::optional<Bytes> readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<GameFiles> GameFiles::open(const std::filesystem::path& bundleDir, std::string& error) {
    GameFiles files;
    files.bundle_ = bundleDir;
    auto exe = readWholeFile(bundleDir / "SpyMouse");
    if (!exe) {
        error = "no game executable in " + bundleDir.string();
        return std::nullopt;
    }
    return openWith(std::move(files), std::move(exe), error);
}

std::optional<GameFiles> GameFiles::open(std::shared_ptr<const std::map<std::string, Bytes>> memory, std::string& error) {
    GameFiles files;
    files.memory_ = std::move(memory);
    std::optional<Bytes> exe;
    if (auto it = files.memory_->find("SpyMouse"); it != files.memory_->end()) exe = it->second;
    if (!exe) {
        error = "no game executable (Payload/*.app/SpyMouse)";
        return std::nullopt;
    }
    return openWith(std::move(files), std::move(exe), error);
}

std::optional<GameFiles> GameFiles::openWith(GameFiles files, std::optional<Bytes> exe, std::string& error) {
    auto image = ExecutableImage::parse(std::move(*exe), error);
    if (!image) return std::nullopt;
    auto containerKey = image->readCString(layout::kContainerKey, 128);
    auto chunkKey = image->read(layout::kChunkTreeKey, layout::kChunkTreeKeyLength);
    auto guiKey = image->readCString(layout::kGuiKey, 128);
    if (!containerKey || !chunkKey || !guiKey || containerKey->size() < 8 || guiKey->size() < 8) {
        error = "the program is not Spy Mouse 1.1.1: its file keys were not found";
        return std::nullopt;
    }
    files.keys_ = {std::move(*containerKey), std::move(*chunkKey), std::move(*guiKey)};
    files.executable_ = std::make_shared<const ExecutableImage>(std::move(*image));
    // Both keys must open a file whose integrity the original checks.
    auto levels = files.readResource("level_data.xml.brd");
    auto map = files.readResource("map801/map801.mbn");
    if (!levels || !decodeChunkTree(*levels, files.keys_.chunkTreeKey) || !map ||
        !decodeContainer(*map, files.keys_.containerKey)) {
        error = "the data files do not match Spy Mouse 1.1.1";
        return std::nullopt;
    }
    return files;
}

std::filesystem::path GameFiles::resourcePath(const std::string& relative) const {
    if (!override_.empty()) {
        std::error_code ec;
        auto replaced = override_ / relative;
        if (std::filesystem::is_regular_file(replaced, ec)) return replaced;
    }
    return bundle_ / "res" / relative;
}

std::optional<Bytes> GameFiles::readResource(const std::string& relative) const {
    if (memory_) {
        auto it = memory_->find("res/" + relative);
        if (it == memory_->end()) return std::nullopt;
        return it->second;
    }
    return readWholeFile(resourcePath(relative));
}

}  // namespace doc::data
