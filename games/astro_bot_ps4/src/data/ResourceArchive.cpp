#include "data/ResourceArchive.hpp"

#include "data/Hash.hpp"

#include <cctype>
#include <cstring>

namespace beacon::data {

std::uint32_t ResourceArchive::pathHash(std::string_view path) {
    std::string lower(path);
    for (char& c : lower) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return fnv1a(lower);
}

bool ResourceArchive::open(const std::filesystem::path& file, std::string* error) {
    entries_.clear();
    auto fail = [&](const char* message) {
        if (error) {
            *error = file.string() + ": " + message;
        }
        entries_.clear();
        return false;
    };
    if (!loadFile(file, bytes_)) {
        return fail("cannot read");
    }
    if (bytes_.size() < 8 || std::memcmp(bytes_.data(), "@MRA", 4) != 0) {
        return fail("not an @MRA archive");
    }
    const std::uint32_t count = u32(bytes_, 4);
    if (8 + std::size_t(count) * 24 > bytes_.size()) {
        return fail("entry table out of range");
    }
    entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 8 + std::size_t(i) * 24;
        Entry e;
        e.hash = u32(bytes_, at);
        std::uint32_t nameOffset = u32(bytes_, at + 4), nameLength = u32(bytes_, at + 8);
        e.offset = u32(bytes_, at + 12);
        e.size = u32(bytes_, at + 16);
        if (std::size_t(nameOffset) + nameLength > bytes_.size() || std::size_t(e.offset) + e.size > bytes_.size()) {
            return fail("entry out of range");
        }
        e.path.assign(reinterpret_cast<const char*>(bytes_.data()) + nameOffset, nameLength);
        entries_.push_back(std::move(e));
    }
    return true;
}

std::optional<ByteView> ResourceArchive::find(std::string_view path) const {
    const std::uint32_t h = pathHash(path);
    for (const auto& e : entries_) {
        if (e.hash == h) {
            return contents(e);
        }
    }
    return std::nullopt;
}

}   // namespace data
