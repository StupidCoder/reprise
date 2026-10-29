// File and data access. See resources.hpp.
#include "system/resources.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

namespace picarat {
static void requireRange(std::size_t size, std::size_t offset, std::size_t length) {
    if (offset > size || length > size - offset) {
        throw std::runtime_error("resource range outside payload");
    }
}

std::uint16_t read16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    requireRange(bytes.size(), offset, 2);
    return bytes[offset] | (std::uint16_t(bytes[offset + 1]) << 8);
}

std::uint32_t read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    requireRange(bytes.size(), offset, 4);
    return read16(bytes, offset) | (std::uint32_t(read16(bytes, offset + 2)) << 16);
}

namespace {
std::string folded(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = char(c - 'A' + 'a');
        }
    }
    return s;
}

// One directory's entries by their case-folded names, and when it was read.
struct Listing {
    std::filesystem::file_time_type written;
    std::map<std::string, std::string> names;
};

std::mutex listingsMutex;
std::map<std::filesystem::path, Listing> listings;

// The entry of `directory` named `name` ignoring case; empty when none.
std::string matchIn(const std::filesystem::path& directory, const std::string& name) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(directory, error);
    if (error) {
        return {};
    }
    std::lock_guard lock(listingsMutex);
    Listing& listing = listings[directory];
    if (listing.names.empty() || listing.written != written) {
        listing.names.clear();
        listing.written = written;
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
             it.increment(error)) {
            const std::string entry = it->path().filename().string();
            listing.names.emplace(folded(entry), entry);
        }
    }
    const auto it = listing.names.find(folded(name));
    return it == listing.names.end() ? std::string() : it->second;
}
} // namespace

std::filesystem::path resolvePath(const std::filesystem::path& path) {
    std::error_code error;
    if (path.empty() || std::filesystem::exists(path, error)) {
        return path;
    }
    // From the deepest part that exists as written, the rest name by name.
    std::filesystem::path base = path, rest;
    while (base.has_relative_path() && !std::filesystem::exists(base, error)) {
        rest = rest.empty() ? base.filename() : base.filename() / rest;
        base = base.parent_path();
    }
    for (const auto& part : rest) {
        const std::string name = matchIn(base, part.string());
        if (name.empty()) {
            return path;
        }
        base /= name;
    }
    return base;
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(resolvePath(path), error);
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(resolvePath(path), std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const auto size = file.tellg();
    if (size < 0 || size > 512 * 1024 * 1024) { // the largest DS cartridge
        throw std::runtime_error("invalid resource size");
    }
    Bytes result(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!result.empty() && !file.read(reinterpret_cast<char*>(result.data()), size)) {
        throw std::runtime_error("short read: " + path.string());
    }
    return result;
}

Resource::Resource(Bytes bytes)
    : storage_(std::make_shared<const Bytes>(std::move(bytes))), length_(storage_->size()) {}

std::span<const std::uint8_t> Resource::bytes() const {
    return std::span(*storage_).subspan(offset_, length_);
}

Resource Resource::slice(std::size_t offset, std::size_t length) const {
    requireRange(length_, offset, length);
    auto result = *this;
    result.offset_ += offset;
    result.length_ = length;
    return result;
}

std::string resolveLanguagePath(std::string_view path, std::string_view language) {
    if (language.size() != 2 ||
        language.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string_view::npos) {
        throw std::runtime_error("language must be a two-letter resource code");
    }
    // Every path the game uses has at most one '?'. The original's way of
    // replacing it (copying through a scratch buffer) is not a general
    // replace, so paths with several markers are rejected rather than guessed.
    if (std::count(path.begin(), path.end(), '?') > 1) {
        throw std::runtime_error("multiple language markers are unverified");
    }
    std::string result;
    for (char c : path) {
        if (c == '?') {
            result += language;
        } else {
            result += c;
        }
    }
    if (result.size() >= 64 || result.find('\0') != std::string::npos) {
        throw std::runtime_error("resource path exceeds original buffer");
    }
    return result;
}

std::string backgroundPath(std::string_view logicalName) {
    if (logicalName.size() < 3) {
        throw std::runtime_error("background name too short");
    }
    return "/data_lt2/bg/" + std::string(logicalName.substr(0, logicalName.size() - 3)) + "arc";
}

AssetStore::AssetStore(std::filesystem::path root) : root_(std::filesystem::canonical(root)) {}

Resource AssetStore::load(std::string_view directory, std::string_view romPath) const {
    if (romPath.empty() || romPath.find('\0') != std::string_view::npos ||
        romPath.find('\\') != std::string_view::npos) {
        throw std::runtime_error("invalid resource path");
    }
    if (romPath.front() == '/') {
        romPath.remove_prefix(1);
    }
    std::filesystem::path relative{romPath};
    if (relative.empty() || relative.is_absolute()) {
        throw std::runtime_error("invalid relative resource path");
    }
    for (const auto& part : relative) {
        if (part == "." || part == "..") {
            throw std::runtime_error("resource path traversal");
        }
    }
    auto base = std::filesystem::canonical(root_ / directory);
    auto target = std::filesystem::canonical(base / relative);
    auto contained = target.lexically_relative(base);
    if (contained.empty() || *contained.begin() == "..") {
        throw std::runtime_error("resource symlink escapes store");
    }
    return Resource(readFile(target));
}

Resource AssetStore::raw(std::string_view path) const {
    return load("raw/fs", path);
}

Resource AssetStore::expanded(std::string_view path) const {
    return load("expanded", path);
}

// PCK2 layout: header size, total size, "PCK2", padding; then members, each
// {header size, member size, flags, data length, NUL-terminated name, data}.
PckArchive::PckArchive(Resource resource) : resource_(std::move(resource)) {
    auto bytes = resource_.bytes();
    auto header = read32(bytes, 0), total = read32(bytes, 4);
    if (read32(bytes, 8) != 0x324b4350 || (total & 3) || header < 16 || total < header ||
        total != bytes.size()) {
        throw std::runtime_error("invalid PCK2 header");
    }
    for (std::size_t cursor = header; cursor < total;) {
        requireRange(total, cursor, 16);
        auto head = read32(bytes, cursor), size = read32(bytes, cursor + 4),
             length = read32(bytes, cursor + 12);
        if (head < 17 || size < head || length > size - head) {
            throw std::runtime_error("invalid PCK2 member lengths");
        }
        requireRange(total, cursor, size);
        auto name = bytes.subspan(cursor + 16, head - 16);
        auto end = std::find(name.begin(), name.end(), 0);
        if (end == name.end()) {
            throw std::runtime_error("unterminated PCK2 name");
        }
        members_.push_back({std::string(name.begin(), end), cursor + head, length});
        cursor += size;
    }
}

std::optional<Resource> PckArchive::find(std::string_view name) const {
    for (const auto& member : members_) {
        if (member.name == name) {
            return resource_.slice(member.offset, member.length);
        }
    }
    return std::nullopt;
}
} // namespace picarat
