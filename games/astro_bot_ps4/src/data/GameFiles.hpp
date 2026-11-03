// Index of the user's extracted game folder (the CUSA12392 directory with eboot.bin and data/).
// Models and materials refer to textures and other models by file stem only, so assets are found
// by stem. Low-detail variants ("<stem>__low") are kept apart.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

class GameFiles {
public:
    bool open(const std::filesystem::path& root, std::string* error);

    const std::filesystem::path& root() const { return root_; }
    // Full path of a file by stem and extension (".gnf", ".jxm", ...); prefers the full-detail file.
    std::optional<std::filesystem::path> find(const std::string& stem, const std::string& extension) const;
    // All files with the extension, sorted by path relative to data/.
    std::vector<std::filesystem::path> list(const std::string& extension) const;

private:
    std::filesystem::path root_;
    std::map<std::string, std::filesystem::path> byKey_;   // "stem.ext"
};

}   // namespace data
