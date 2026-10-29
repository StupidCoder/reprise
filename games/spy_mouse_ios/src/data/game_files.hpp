// Access to the user's copy of the game: the unpacked app bundle (SpyMouse.app from the IPA).
// Nothing from the game is part of this source tree; every file, including the keys that
// unlock the obfuscated data files, is read from the user's bundle at run time.
#pragma once

#include <cstdint>
#include <memory>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc::data {

using Bytes = std::vector<std::uint8_t>;

// Keys the original program uses for its two file obfuscations. They are read from the
// user's executable (see GameFiles::open), never stored here.
struct FileKeys {
    Bytes containerKey;  // PFile key incl. trailing NUL (Map::loadBinVersion @0x13de30)
    Bytes chunkTreeKey;  // 29-byte BRD key (BrdReader::loadFile @0x21b388)
    Bytes guiKey;        // PFile key of the GUI layouts incl. NUL (GuiItem::loadLayout @0x1d9b28)
};

class ExecutableImage;

class GameFiles {
public:
    // bundleDir: the unpacked "SpyMouse.app" directory. Fails with a message if the
    // directory or the executable is missing or is not the expected build.
    static std::optional<GameFiles> open(const std::filesystem::path& bundleDir, std::string& error);
    // The same checks on files held in memory (bundle-relative names: "SpyMouse", "res/..."),
    // for an import that is checked before anything is written.
    static std::optional<GameFiles> open(std::shared_ptr<const std::map<std::string, Bytes>> files, std::string& error);

    // Reads "res/<relative>" (the 480x320 resource set the game uses on iPhone 3GS-class
    // devices). Returns nullopt if the file does not exist.
    std::optional<Bytes> readResource(const std::string& relative) const;
    std::filesystem::path resourcePath(const std::string& relative) const;
    // Like the original, which looks in Library/Caches/res/ before the bundle's res/
    // (ManagerFiles::open), files in this directory replace the bundle's. Used for test levels.
    void setOverrideDir(std::filesystem::path dir) { override_ = std::move(dir); }

    const FileKeys& keys() const { return keys_; }
    // The game's program image, for data tables the original keeps there.
    const ExecutableImage& executable() const { return *executable_; }

private:
    static std::optional<GameFiles> openWith(GameFiles files, std::optional<Bytes> exe, std::string& error);

    std::filesystem::path bundle_;
    std::filesystem::path override_;
    std::shared_ptr<const std::map<std::string, Bytes>> memory_;  // in-memory bundle, if any
    FileKeys keys_;
    std::shared_ptr<const ExecutableImage> executable_;
};

std::optional<Bytes> readWholeFile(const std::filesystem::path& path);

}  // namespace doc::data
