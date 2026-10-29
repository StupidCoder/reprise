#include "importer/ipa_import.hpp"

#include "data/game_files.hpp"
#include "data/level_table.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

namespace doc::importer {
namespace fs = std::filesystem;
namespace {

constexpr const char* kMarker = "imported.txt";
constexpr const char* kExecutable = "SpyMouse";
constexpr int kImportVersion = 1;  // raise when the imported layout changes

std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24); }

// The archive's bytes: a file being read, or a copy in memory.
class Archive {
public:
    virtual ~Archive() = default;
    virtual std::uint64_t size() const = 0;
    // Reads `n` bytes at `offset`; false past the end.
    virtual bool read(std::uint64_t offset, std::size_t n, std::uint8_t* out) const = 0;
};

class FileArchive final : public Archive {
public:
    explicit FileArchive(std::ifstream& in) : in_(in) {
        in_.seekg(0, std::ios::end);
        size_ = std::uint64_t(in_.tellg());
    }
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::size_t n, std::uint8_t* out) const override {
        if (offset > size_ || size_ - offset < n) return false;
        in_.clear();
        in_.seekg(std::streamoff(offset));
        in_.read(reinterpret_cast<char*>(out), std::streamsize(n));
        return bool(in_);
    }

private:
    std::ifstream& in_;
    std::uint64_t size_ = 0;
};

class MemoryArchive final : public Archive {
public:
    explicit MemoryArchive(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint64_t size() const override { return bytes_.size(); }
    bool read(std::uint64_t offset, std::size_t n, std::uint8_t* out) const override {
        if (offset > bytes_.size() || bytes_.size() - offset < n) return false;
        std::memcpy(out, bytes_.data() + offset, n);
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
};

// One file of the archive, from its central directory.
struct Entry {
    std::string name;
    std::uint16_t method = 0;  // 0 stored, 8 deflated
    std::uint32_t crc = 0, compressedSize = 0, size = 0, localHeader = 0;
};

// Reads the central directory of a ZIP archive (no ZIP64: app archives stay below 4 GB).
bool readDirectory(const Archive& in, std::vector<Entry>& entries, std::string& error) {
    const std::uint64_t fileSize = in.size();
    const std::uint64_t tail = std::min<std::uint64_t>(fileSize, 65536 + 22);
    std::vector<std::uint8_t> end(tail);
    if (!in.read(fileSize - tail, std::size_t(tail), end.data())) {
        error = "The file could not be read.";
        return false;
    }
    std::int64_t eocd = -1;
    for (std::int64_t i = std::int64_t(tail) - 22; i >= 0; --i)
        if (le32(&end[std::size_t(i)]) == 0x06054B50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        error = "The file is not an app archive (no ZIP directory).";
        return false;
    }
    const std::uint8_t* e = &end[std::size_t(eocd)];
    const std::uint32_t count = le16(e + 10), size = le32(e + 12), offset = le32(e + 16);
    if (offset == 0xFFFFFFFFu || std::uint64_t(offset) + size > fileSize) {
        error = "The archive's directory is damaged or uses ZIP64.";
        return false;
    }
    std::vector<std::uint8_t> dir(size);
    if (!in.read(offset, size, dir.data())) {
        error = "The archive's directory is damaged.";
        return false;
    }
    std::size_t at = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + 46 > dir.size() || le32(&dir[at]) != 0x02014B50) {
            error = "The archive's directory is damaged.";
            return false;
        }
        const std::uint8_t* h = &dir[at];
        Entry entry;
        entry.method = le16(h + 10);
        entry.crc = le32(h + 16);
        entry.compressedSize = le32(h + 20);
        entry.size = le32(h + 24);
        const std::size_t nameLength = le16(h + 28), extra = le16(h + 30), comment = le16(h + 32);
        entry.localHeader = le32(h + 42);
        if (at + 46 + nameLength > dir.size()) {
            error = "The archive's directory is damaged.";
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(h + 46), nameLength);
        entries.push_back(std::move(entry));
        at += 46 + nameLength + extra + comment;
    }
    return true;
}

bool extract(const Archive& in, const Entry& entry, std::vector<std::uint8_t>& out, std::string& error) {
    std::uint8_t local[30];
    if (!in.read(entry.localHeader, 30, local) || le32(local) != 0x04034B50) {
        error = "Damaged archive entry: " + entry.name;
        return false;
    }
    std::vector<std::uint8_t> packed(entry.compressedSize);
    if (!in.read(std::uint64_t(entry.localHeader) + 30 + le16(local + 26) + le16(local + 28), packed.size(), packed.data())) {
        error = "The archive ends early: " + entry.name;
        return false;
    }
    out.assign(entry.size, 0);
    if (entry.method == 0) {
        if (entry.compressedSize != entry.size) {
            error = "Damaged archive entry: " + entry.name;
            return false;
        }
        out = std::move(packed);
    } else if (entry.size == 0) {
        out.clear();  // (an empty file: a deflated empty stream has nothing to inflate into)
    } else if (entry.method == 8) {
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
            error = "zlib could not start";
            return false;
        }
        z.next_in = packed.data();
        z.avail_in = uInt(packed.size());
        z.next_out = out.data();
        z.avail_out = uInt(out.size());
        const int result = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        if (result != Z_STREAM_END || z.total_out != entry.size) {
            error = "Damaged archive entry: " + entry.name;
            return false;
        }
    } else {
        error = "Unsupported compression in the archive: " + entry.name;
        return false;
    }
    if (std::uint32_t(crc32(0, out.data(), uInt(out.size()))) != entry.crc) {
        error = "Checksum mismatch in the archive: " + entry.name;
        return false;
    }
    return true;
}

// The part of an archive path below the app folder, if the file is one the game needs.
bool wanted(const std::string& name, std::string& relative) {
    if (name.rfind("Payload/", 0) != 0) return false;
    const std::size_t app = name.find(".app/", 8);
    if (app == std::string::npos || name.find('/', 8) != app + 4) return false;
    relative = name.substr(app + 5);
    if (relative.empty() || relative.back() == '/') return false;
    // No absolute paths or parent references from the archive.
    if (relative.find("..") != std::string::npos || relative.front() == '/') return false;
    return relative == kExecutable || relative.rfind("res/", 0) == 0;
}

}  // namespace

fs::path gameDirectory(const fs::path& dataDirectory) { return dataDirectory / "game"; }

bool isAppArchive(std::span<const std::uint8_t> bytes) {
    const MemoryArchive archive(bytes);
    std::vector<Entry> entries;
    std::string ignored;
    if (!readDirectory(archive, entries, ignored)) return false;
    return std::any_of(entries.begin(), entries.end(), [](const Entry& e) {
        return e.name.rfind("Payload/", 0) == 0 && e.name.find(".app/", 8) != std::string::npos;
    });
}

bool readIpa(std::span<const std::uint8_t> ipa, ImportedFiles& out, std::string& error) {
    const MemoryArchive archive(ipa);
    std::vector<Entry> entries;
    if (!readDirectory(archive, entries, error)) return false;
    auto files = std::make_shared<std::map<std::string, data::Bytes>>();
    bool haveExecutable = false;
    std::vector<std::uint8_t> data;
    for (const Entry& entry : entries) {
        std::string relative;
        if (!wanted(entry.name, relative)) continue;
        if (!extract(archive, entry, data, error)) return false;
        haveExecutable |= relative == kExecutable;
        (*files)[relative] = std::move(data);
        data.clear();
    }
    if (!haveExecutable) {
        error = "This archive does not contain the game (no Payload/*.app/SpyMouse).";
        return false;
    }
    std::string why;
    auto game = data::GameFiles::open(files, why);
    auto table = game ? data::LevelTable::load(*game, why) : std::nullopt;
    if (!game || !table || !table->byNumber("1-1")) {
        error = "This app is not a supported version of the game (" + why + ").";
        return false;
    }
    out.files.clear();
    for (auto& [name, bytes] : *files) out.files.emplace_back(name, std::move(bytes));
    return true;
}

bool gameReady(const fs::path& dataDirectory) {
    std::ifstream marker(gameDirectory(dataDirectory) / kMarker);
    std::string word;
    int version = 0;
    return marker >> word >> version && word == "import" && version == kImportVersion;
}

bool importIpa(const fs::path& ipa, const fs::path& dataDirectory,
               const std::function<void(double, const std::string&)>& progress, const std::atomic<bool>* cancel,
               std::string& error) {
    std::ifstream in(ipa, std::ios::binary);
    if (!in) {
        error = "Cannot open " + ipa.filename().string() + ".";
        return false;
    }
    const FileArchive archive(in);
    std::vector<Entry> entries;
    if (!readDirectory(archive, entries, error)) return false;

    std::vector<std::pair<const Entry*, std::string>> files;
    std::uint64_t totalBytes = 0;
    bool haveExecutable = false;
    for (const Entry& entry : entries) {
        std::string relative;
        if (!wanted(entry.name, relative)) continue;
        haveExecutable |= relative == kExecutable;
        totalBytes += entry.size;
        files.emplace_back(&entry, relative);
    }
    if (!haveExecutable) {
        error = "This archive does not contain the game (no Payload/*.app/SpyMouse).";
        return false;
    }

    // Extract into a fresh folder next to the current import, then swap.
    std::error_code ec;
    fs::create_directories(dataDirectory, ec);
    const fs::path staging = dataDirectory / "game-import";
    fs::remove_all(staging, ec);
    std::uint64_t doneBytes = 0;
    std::vector<std::uint8_t> data;
    for (const auto& [entry, relative] : files) {
        if (cancel && cancel->load()) {
            fs::remove_all(staging, ec);
            error = "cancelled";
            return false;
        }
        if (!extract(archive, *entry, data, error)) {
            fs::remove_all(staging, ec);
            return false;
        }
        const fs::path target = staging / fs::path(reinterpret_cast<const char8_t*>(relative.c_str()));
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) {
            fs::remove_all(staging, ec);
            error = "Cannot write " + target.string() + ".";
            return false;
        }
        doneBytes += entry->size;
        if (progress) progress(0.9 * double(doneBytes) / double(totalBytes ? totalBytes : 1), "Copying the game files");
    }

    if (progress) progress(0.92, "Checking the game files");
    std::string why;
    auto game = data::GameFiles::open(staging, why);
    auto table = game ? data::LevelTable::load(*game, why) : std::nullopt;
    if (!game || !table || !table->byNumber("1-1")) {
        fs::remove_all(staging, ec);
        error = "This app is not a supported version of the game (" + why + ").";
        return false;
    }
    {
        std::ofstream marker(staging / kMarker);
        marker << "import " << kImportVersion << "\nsource " << ipa.filename().string() << "\nfiles " << files.size()
               << "\n";
    }
    const fs::path current = gameDirectory(dataDirectory);
    fs::remove_all(current, ec);
    fs::rename(staging, current, ec);
    if (ec) {
        error = "Cannot move the imported files into place: " + ec.message();
        return false;
    }
    if (progress) progress(1.0, "Done");
    return true;
}

}  // namespace doc::importer
