#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The player's data disk: saved positions, high score tables and tower
// groups. The original keeps them on a disk of its own format (a
// directory of 200 entries: kind, an 8-character name, blocks; $12A10);
// here each is a file "<kind>.<name>" in a folder beside the installed
// content, and the requester ($126C0) shows them as the original does.
namespace babel::front {

enum class FileKind { group = 2, position = 8, scores = 0x20 };

class DataDisk {
public:
    static constexpr int entries = 200; // the directory's size ("Room for")
    static constexpr int name_length = 8;

    explicit DataDisk(std::filesystem::path folder) : folder_(std::move(folder)) {}

    // The names of one kind, sorted as the requester sorts them ($12AFA).
    std::vector<std::string> names(FileKind) const;
    int files() const; // all kinds
    std::optional<std::vector<std::uint8_t>> read(FileKind, const std::string& name) const;
    bool exists(FileKind k, const std::string& name) const { return read(k, name).has_value(); }
    // False when the file cannot be written (the original's "Unable To Write to Disk").
    bool write(FileKind, const std::string& name, const std::vector<std::uint8_t>&) const;

private:
    std::filesystem::path file(FileKind, const std::string& name) const;
    std::filesystem::path folder_;
};

} // namespace babel::front
