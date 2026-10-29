// The cartridge save image with its three slots. See a2_save_file.hpp.
#include "system/a2_save_file.hpp"
#include "system/console_id.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

namespace picarat {
namespace {
// 0206c3f4 compares and writes this 16-byte card identification (ov2 02087988).
constexpr char kIdentity[16] = "ATAMFIREBELLNY";
constexpr std::size_t kHeader = 0x10, kSlots = 0x110, kSlotStride = 0x370, kShared = 0xb60;

std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}

void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = std::uint8_t(v >> (8 * i));
    }
}
} // namespace

std::uint32_t SaveFile::checksum(const std::uint8_t* data, std::size_t bytes) {
    // 0206b1b4: two 32-bit sums over halfwords, both starting at 0xffff and
    // folded to 16 bits (high half added to the low half) every 360 words.
    std::uint32_t a = 0xffff, b = 0xffff;
    std::size_t words = bytes / 2;
    while (words) {
        const std::size_t n = std::min<std::size_t>(words, 360);
        words -= n;
        for (std::size_t i = 0; i < n; ++i) {
            a += std::uint32_t(data[0] | data[1] << 8);
            b += a;
            data += 2;
        }
        a = (a >> 16) + (a & 0xffff);
        b = (b >> 16) + (b & 0xffff);
    }
    a = (a >> 16) + (a & 0xffff);
    b = (b >> 16) + (b & 0xffff);
    return a | b << 16;
}

SaveFile::SaveFile(std::filesystem::path path) : path_(std::move(path)) {
    if (fileExists(path_)) {
        image_ = readFile(path_);
    }
    // A DS image of another size is used as far as it goes; blank cells read 0xff.
    image_.resize(std::max(image_.size(), imageSize), 0xff);
}

void SaveFile::write(std::size_t offset, const std::uint8_t* data, std::size_t bytes) {
    std::copy_n(data, bytes, image_.begin() + std::ptrdiff_t(offset));
    flush();
}

void SaveFile::flush() const {
    // Written to a temporary name and renamed, so a crash keeps the old image.
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    auto temp = path_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(image_.data()), std::streamsize(image_.size()));
        if (!out) {
            throw std::runtime_error("cannot write the save file");
        }
    }
    std::filesystem::rename(temp, path_);
}

unsigned SaveFile::boot() {
    // 0206c3f4: a card without the identification is formatted: an empty
    // header (02082224) and shared block (0208308c), then the identification.
    if (!std::equal(kIdentity, kIdentity + 16, image_.begin())) {
        header_.fill(0);
        commitHeader();
        shared_.fill(0);
        writeShared();
        write(0, reinterpret_cast<const std::uint8_t*>(kIdentity), 16);
    }
    // 02082204 reads the header into state +0x000; 0206bbac checks it.
    std::copy_n(image_.begin() + kHeader, headerSize, header_.begin());
    if (checksum(header_.data() + 4, headerSize - 4) != get32(header_.data())) {
        return 7; // 02082244 fails: every slot counts as damaged
    }
    // 02083874: each used slot's block must match its checksum.
    unsigned damaged = 0;
    for (int s = 0; s < slots; ++s) {
        if (!used(s)) {
            continue;
        }
        const auto block = readSlot(s);
        if (checksum(block.data() + 4, slotSize - 4) != get32(block.data())) {
            clearSlot(s);
            damaged |= 1u << s;
        }
    }
    // 02083134: the shared block, copied to state +0x3e8 when it checks out.
    const std::uint8_t* shared = image_.data() + kShared;
    if (checksum(shared + 4, globalSize - 4) == get32(shared)) {
        std::copy_n(shared, globalSize, shared_.begin());
    } else {
        damaged |= 8;
    }
    return damaged;
}

void SaveFile::repair(unsigned damaged) {
    // 0206bcf0..0206bd34: a bad header is emptied; the header is written
    // again; a bad shared block is rewritten from its cleared RAM copy.
    if ((damaged & 7) == 7) {
        header_.fill(0);
    }
    commitHeader();
    if (damaged & 8) {
        shared_.fill(0);
        put32(shared_.data(), checksum(shared_.data() + 4, globalSize - 4)); // 0208308c
        writeShared();
    }
}

bool SaveFile::used(int s) const {
    return s >= 0 && s < slots && (get32(header_.data() + 4) >> s & 1);
}

bool SaveFile::anyUsed() const {
    return used(0) || used(1) || used(2);
}

std::string SaveFile::name(int s) const {
    if (s < 0 || s >= slots) {
        return {};
    }
    const char* p = reinterpret_cast<const char*>(summary(s));
    return std::string(p, strnlen(p, 20));
}

int SaveFile::place(int s) const {
    return s >= 0 && s < slots ? header_[std::size_t(0x1c + 64 * s)] : 0;
}

std::uint32_t SaveFile::seconds(int s) const {
    return s >= 0 && s < slots ? get32(header_.data() + 0x34 + 64 * s) : 0;
}

unsigned SaveFile::found(int s) const {
    return s >= 0 && s < slots ? unsigned(header_[std::size_t(0x38 + 64 * s)] |
                                          header_[std::size_t(0x39 + 64 * s)] << 8)
                               : 0;
}

unsigned SaveFile::solved(int s) const {
    return s >= 0 && s < slots ? unsigned(header_[std::size_t(0x3a + 64 * s)] |
                                          header_[std::size_t(0x3b + 64 * s)] << 8)
                               : 0;
}

unsigned SaveFile::clearLevel(int s) const {
    return s >= 0 && s < slots ? header_[std::size_t(0x3c + 64 * s)] : 0;
}

void SaveFile::setUsed(int s, bool on) {
    if (s < 0 || s >= slots) {
        return;
    }
    std::uint32_t mask = get32(header_.data() + 4);
    mask = on ? mask | 1u << s : mask & ~(1u << s);
    put32(header_.data() + 4, mask);
}

void SaveFile::setName(int s, const std::string& name) {
    // 02082154: the first byte is cleared, then the name copied (strcpy).
    if (s < 0 || s >= slots) {
        return;
    }
    auto* p = summary(s);
    p[0] = 0;
    std::copy_n(name.begin(), std::min<std::size_t>(name.size(), 19), p);
    p[std::min<std::size_t>(name.size(), 19)] = 0;
}

void SaveFile::setPlace(int s, int place) {
    if (s >= 0 && s < slots) {
        header_[std::size_t(0x1c + 64 * s)] = std::uint8_t(place);
    }
}

void SaveFile::setSeconds(int s, std::uint32_t v) {
    if (s >= 0 && s < slots) {
        put32(header_.data() + 0x34 + 64 * s, v);
    }
}

void SaveFile::setFound(int s, unsigned v) {
    if (s >= 0 && s < slots) {
        header_[std::size_t(0x38 + 64 * s)] = std::uint8_t(v);
        header_[std::size_t(0x39 + 64 * s)] = std::uint8_t(v >> 8);
    }
}

void SaveFile::setSolved(int s, unsigned v) {
    if (s >= 0 && s < slots) {
        header_[std::size_t(0x3a + 64 * s)] = std::uint8_t(v);
        header_[std::size_t(0x3b + 64 * s)] = std::uint8_t(v >> 8);
    }
}

void SaveFile::setClearLevel(int s, unsigned level) {
    if (s >= 0 && s < slots) {
        header_[std::size_t(0x3c + 64 * s)] = std::uint8_t(level);
    }
}

void SaveFile::clearSlot(int s) {
    // 02083924: the summary emptied and the slot marked unused (in RAM).
    setUsed(s, false);
    setName(s, {});
    setPlace(s, 0);
    setSeconds(s, 0);
    setFound(s, 0);
    setSolved(s, 0);
}

bool SaveFile::erase(int s) {
    if (s < 0 || s >= slots) {
        return false;
    }
    // Header as on the card first (a blank or damaged card is formatted).
    if (!std::equal(kIdentity, kIdentity + 16, image_.begin()) ||
        checksum(image_.data() + kHeader + 4, headerSize - 4) != get32(image_.data() + kHeader)) {
        boot();
    } else {
        std::copy_n(image_.begin() + kHeader, headerSize, header_.begin());
    }
    clearSlot(s);
    setClearLevel(s, 0);
    const Bytes blank(slotSize, 0xff);
    write(kSlots + kSlotStride * std::size_t(s), blank.data(), slotSize);
    commitHeader();
    return true;
}

void SaveFile::invalidateHeader() {
    // 020821a8: the header word is zeroed and the header written, so an
    // interrupted save leaves a header that fails its check.
    put32(header_.data(), 0);
    write(kHeader, header_.data(), headerSize);
}

void SaveFile::commitHeader() {
    put32(header_.data(), checksum(header_.data() + 4, headerSize - 4));
    write(kHeader, header_.data(), headerSize);
}

void SaveFile::writeSlot(int s, Bytes progress) {
    // 02083668: the first word is the checksum of the other 796 bytes.
    if (s < 0 || s >= slots || progress.size() != slotSize) {
        throw std::runtime_error("save slot");
    }
    put32(progress.data(), checksum(progress.data() + 4, slotSize - 4));
    write(kSlots + kSlotStride * std::size_t(s), progress.data(), slotSize);
}

Bytes SaveFile::readSlot(int s) const {
    if (s < 0 || s >= slots) {
        throw std::runtime_error("save slot");
    }
    const auto begin = image_.begin() + std::ptrdiff_t(kSlots + kSlotStride * std::size_t(s));
    return Bytes(begin, begin + slotSize);
}

void SaveFile::writeShared() {
    put32(shared_.data(), checksum(shared_.data() + 4, globalSize - 4));
    write(kShared, shared_.data(), globalSize);
}

std::array<std::uint8_t, 6> SaveFile::consoleAddress() const {
    return picarat::consoleAddress(path_.parent_path());
}
} // namespace picarat
