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

enum class FileKind { tower = 1, group = 2, position = 8, scores = 0x20 };

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
    bool remove(FileKind, const std::string& name) const;
    // Prepare Disk: every file of the game's kinds goes (false if one cannot be removed).
    bool erase() const;

    // Quick saves: every answer the disk gave, in order, since `record`
    // started afresh; `replay` gives those answers again (and touches no
    // file) until they run out, so that a game played again from its
    // journal sees the disk as it was.
    using Answers = std::vector<std::vector<std::uint8_t>>;
    void record() { answers_.clear(); replay_ = false; }
    const Answers& answers() const { return answers_; }
    void replay(Answers a) {
        answers_ = std::move(a);
        replay_ = true;
        next_ = 0;
    }
    bool replay_done() const { return !replay_ || next_ == answers_.size(); }
    void end_replay() { replay_ = false; }

private:
    std::filesystem::path file(FileKind, const std::string& name) const;
    std::vector<std::uint8_t> answer(std::vector<std::uint8_t> now) const; // recorded, or the replayed one
    std::vector<std::string> list(FileKind) const;
    std::optional<std::vector<std::uint8_t>> load(FileKind, const std::string& name) const;
    std::filesystem::path folder_;
    mutable Answers answers_;
    bool replay_{};
    mutable std::size_t next_{};
};

} // namespace babel::front
