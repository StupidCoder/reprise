#include "bruce_lee_c64/import/disk_import.hpp"
#include "c64/disk_image.hpp"
#include "c64/program.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <stdexcept>
#include <string_view>

// The loader's rules, recovered from the disk's own code (addresses are
// where the loader runs them). Every key and table is read from the user's
// disk; the constants below are the loader's own code constants.
namespace bruce_lee::import {
namespace {

// What the import produces, and the release it was recovered from
// (Bruce_Lee_(Original).g64, MD5 8cbfd1595cdc18a8e68fe4d43eec6aa8). Hashes
// are of the bytes without the load address.
struct ExpectedFile {
    std::string_view name;
    std::uint16_t load_address;
    std::size_t size;
    std::string_view sha256;
};
constexpr std::string_view release_name = "Datasoft disk (1984)";
constexpr ExpectedFile release[] = {
    {"game.prg", 0x0400, 0xA000, "2b482503521cae7fbfb608f0e2c1038dd4dd514981586dc7156072206df01813"},
    {"high.prg", 0xE000, 0x2000, "497207b110fe2d353607b24056da198c4ff7495c0b9c97239a7177d493ffc0a0"},
    {"loading.prg", 0x6000, 0x2000, "1aadacee324f7f9404186d49224779cbe75f5d8911d62d0cbc895c352864bfee"},
};

// The C64's memory as far as the loader builds it.
struct Memory {
    std::array<std::uint8_t, 0x10000> bytes{};
    std::uint8_t& operator[](unsigned address) { return bytes[address & 0xFFFF]; }
    std::uint8_t operator[](unsigned address) const { return bytes[address & 0xFFFF]; }
    void put(unsigned address, std::span<const std::uint8_t> data) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            (*this)[address + unsigned(i)] = data[i];
        }
    }
    std::vector<std::uint8_t> range(unsigned from, std::size_t count) const {
        return {bytes.begin() + from, bytes.begin() + std::ptrdiff_t(from + count)};
    }
};

// The KERNAL's LOAD loop: store at ($AE),Y, increment $AE/$AF. The boot file
// "2" loads at $00AC and overwrites that very pointer, so the rest of the
// file lands somewhere else ($02B0-$0313, over the BASIC idle vector).
void kernal_load(Memory& m, std::span<const std::uint8_t> prg) {
    unsigned pointer = prg[0] | prg[1] << 8;
    m[0xAE] = std::uint8_t(pointer);
    m[0xAF] = std::uint8_t(pointer >> 8);
    for (std::size_t i = 2; i < prg.size(); ++i) {
        pointer = m[0xAE] | m[0xAF] << 8;
        m[pointer] = prg[i];
        if (++m[0xAE] == 0) {
            ++m[0xAF];
        }
    }
}

// The loader's sector order ($0D21/$0D53): from sector 0, add 10 modulo 21
// after each sector, one track down after every 21 sectors. Each sector's
// 256 bytes go to the next page.
void load_sectors(Memory& m, const c64::DiskImage& disk, int track, int count, unsigned to) {
    int sector = 0;
    for (int i = 0; i < count; ++i) {
        const auto* block = disk.sector(track, sector);
        if (!block) {
            throw std::runtime_error("The disk is damaged: sector " + std::to_string(track) + "/" +
                                     std::to_string(sector) + " cannot be read.");
        }
        m.put(to + unsigned(i) * 256, *block);
        sector = (sector + 10) % 21;
        if ((i + 1) % 21 == 0) {
            --track;
        }
    }
}

// Finds the two files the boot uses: the first PRG of the directory (what
// LOAD"*",8,1 loads; it starts at $00AC) and the PRG that loads at $0400.
std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> boot_files(
    const c64::DiskImage& disk) {
    std::vector<std::uint8_t> boot, stage2;
    for (const auto& entry : c64::read_directory(disk)) {
        if (entry.type != 2) {
            continue;
        }
        std::vector<std::uint8_t> bytes;
        try {
            bytes = c64::read_file(disk, entry.track, entry.sector);
        } catch (const std::runtime_error&) {
            continue; // the directory holds decoy entries
        }
        if (bytes.size() < 2) {
            continue;
        }
        const unsigned load = bytes[0] | bytes[1] << 8;
        if (boot.empty() && load == 0x00AC) {
            boot = std::move(bytes);
        } else if (stage2.empty() && load == 0x0400) {
            stage2 = std::move(bytes);
        }
    }
    if (boot.empty() || stage2.empty()) {
        throw app::NotThisGame("This disk is not Bruce Lee (its boot files are not there).");
    }
    return {boot, stage2};
}

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!c64::is_g64(image)) {
        throw app::NotThisGame("This is not a G64 disk image.");
    }
    const auto disk = c64::read_g64(image);
    const auto [boot, stage2] = boot_files(disk);
    if (stage2.size() < 2 + 0x0A8A) {
        throw std::runtime_error("This disk's loader is not the supported Bruce Lee release.");
    }

    Memory m;
    kernal_load(m, boot);
    const std::uint8_t main_key = m[0x0313]; // pushed at $0BA6, pulled at $02AE
    m.put(0x0400, std::span(stage2).subspan(2));

    // $0C69-$0C9B: three loads (the loading picture, the block under the
    // KERNAL, the main block); the copy-protection test between the last two
    // only decides whether the main block is loaded at all.
    load_sectors(m, disk, 17, 0x20, 0x6000);
    const auto loading = m.range(0x6000, 0x2000);
    load_sectors(m, disk, 15, 0x20, 0xE000);
    load_sectors(m, disk, 13, 0xA0, 0x2F00);

    // $0CB2: $E000-$FFFF as one XOR chain (a ^= byte; byte = a), column by
    // column: offset Y outer, pages $E0-$FF inner. Key: $054D.
    std::uint8_t a = m[0x054D];
    for (unsigned y = 0; y < 256; ++y) {
        for (unsigned page = 0xE0; page <= 0xFF; ++page) {
            a ^= m[page << 8 | y];
            m[page << 8 | y] = a;
        }
    }
    // $054E: copies $0545-$054D to $FFFA-$FFFF and, wrapping, $0000-$0002.
    for (unsigned i = 0; i < 6; ++i) {
        m[0xFFFA + i] = m[0x0545 + i];
    }
    const std::uint8_t port_key = m[0x054B]; // the CPU port's direction register, read back at $0CEB

    // $0CD9: the last stage's code, 256 bytes from $0D8A to $02A7 (from
    // offset $D0 round), chained with the CPU port value as key.
    a = port_key;
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned y = (0xD0 + i) & 0xFF;
        a ^= m[0x0D8A + y];
        m[0x02A7 + y] = a;
    }
    const unsigned expected_sum = m[0x0392] | m[0x0393] << 8;

    // $02AF: the main block as one XOR chain from $CE00 down to $2F00 (in
    // each page offset 0 first, then $FF down to $01). The loader branches on
    // the chain value after every 32 pages: it must be the next page number.
    a = main_key;
    for (unsigned page = 0xCE; page >= 0x2F; --page) {
        for (unsigned i = 0; i < 256; ++i) {
            const unsigned offset = i == 0 ? 0 : 256 - i;
            a ^= m[page << 8 | offset];
            m[page << 8 | offset] = a;
        }
        if ((0xCF - page) % 0x20 == 0 && a != page - 1) {
            throw std::runtime_error("This disk decodes to different data than the supported "
                                     "Bruce Lee release (loader check at $036D fails).");
        }
    }
    // $02C8: five bytes of the stage's code patched into the block; the
    // store's address drops by $2000 each time.
    for (unsigned x = 0; x < 5; ++x) {
        m[0xAF01 - 0x2000 * (4 - x)] = m[0x03A2 + x];
    }
    // $02DC-$039F: the block moves down from $2F00 to $0400, page by page in
    // place (a three-way XOR per byte); the old page takes the XOR of both.
    for (unsigned k = 0; k < 0xA0; ++k) {
        const unsigned low = (0x04 + k) << 8, high = (0x2F + k) << 8;
        for (unsigned y = 0; y < 256; ++y) {
            const std::uint8_t p = m[low + y], q = m[high + y];
            m[low + y] = q;
            m[high + y] = std::uint8_t(p ^ q);
        }
    }
    // $0344: 16-bit sum of $0400-$A3FF; the carry out of the high byte goes
    // into the next byte's addition.
    unsigned lo = 0, hi = 0, carry = 0;
    for (unsigned address = 0x0400; address < 0xA400; ++address) {
        lo += m[address] + carry;
        carry = lo >> 8;
        lo &= 0xFF;
        hi += carry;
        carry = hi >> 8;
        hi &= 0xFF;
    }
    if ((hi << 8 | lo) != expected_sum) {
        throw std::runtime_error("This disk decodes to different data than the supported "
                                 "Bruce Lee release (loader checksum at $0344 fails).");
    }

    const std::vector<std::vector<std::uint8_t>> decoded = {m.range(0x0400, 0xA000),
                                                            m.range(0xE000, 0x2000), loading};
    app::ImportResult result;
    result.release = std::string(release_name);
    for (std::size_t i = 0; i < std::size(release); ++i) {
        const auto& expected = release[i];
        if (decoded[i].size() != expected.size || platform::sha256(decoded[i]) != expected.sha256) {
            throw std::runtime_error("This disk decodes correctly but is a different release of "
                                     "Bruce Lee, which is not supported yet.");
        }
        result.files.push_back(
            {std::string(expected.name), c64::Program{expected.load_address, decoded[i]}.to_prg()});
    }
    return result;
}

} // namespace bruce_lee::import
