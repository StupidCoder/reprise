#include "marble_madness_amiga/import/loader_cipher.hpp"
#include "amiga/adf.hpp"
#include "amiga/hunk.hpp"
#include <stdexcept>
#include <string>

namespace marble_madness::import {
namespace {

constexpr std::uint32_t table_seed = 0x57319753;

// Block types c/zzz's dispatcher ($406) knows, as offsets from HUNK_UNIT.
enum Block : std::uint32_t {
    unit = 0x3E7,
    name = 0x3E8,
    code = 0x3E9,
    data = 0x3EA,
    bss = 0x3EB,
    reloc32 = 0x3EC,
    reloc16 = 0x3ED,
    reloc8 = 0x3EE,
    ext = 0x3EF,
    symbol = 0x3F0,
    debug = 0x3F1,
    end = 0x3F2,
    header = 0x3F3,
};

[[noreturn]] void bad(const char* what) {
    throw std::runtime_error(std::string("An encrypted program on the disk does not decode (") + what +
                             "); the disk may be damaged or a different release.");
}

// Reads the encrypted file as zzz does and writes the plain file alongside.
class Decoder {
public:
    Decoder(std::span<const std::uint8_t> file, KeyStream& stream) : in_(file), stream_(stream) {}

    bool at_end() const { return at_ >= in_.size(); }

    // zzz $230 with 4 bytes: a long as stored.
    std::uint32_t raw() {
        const auto value = take_long();
        put(value);
        return value;
    }
    // zzz $2C0: a long XORed with the next keystream long.
    std::uint32_t decrypted() {
        const auto value = take_long() ^ stream_.next();
        put(value);
        return value;
    }
    // zzz $A92: bytes skipped as stored (names, debug data).
    void skip(std::uint64_t count) {
        if (at_ + count > in_.size()) {
            bad("cut short");
        }
        out_.insert(out_.end(), in_.begin() + std::ptrdiff_t(at_), in_.begin() + std::ptrdiff_t(at_ + count));
        at_ += std::size_t(count);
    }

    std::vector<std::uint8_t> take_output() { return std::move(out_); }

private:
    std::uint32_t take_long() {
        if (at_ + 4 > in_.size()) {
            bad("cut short");
        }
        const auto value = amiga::read_be32(in_, at_);
        at_ += 4;
        return value;
    }
    void put(std::uint32_t value) {
        out_.push_back(std::uint8_t(value >> 24));
        out_.push_back(std::uint8_t(value >> 16));
        out_.push_back(std::uint8_t(value >> 8));
        out_.push_back(std::uint8_t(value));
    }

    std::span<const std::uint8_t> in_;
    std::size_t at_{};
    KeyStream& stream_;
    std::vector<std::uint8_t> out_;
};

} // namespace

MachineKey MachineKey::kickstart_1x() {
    MachineKey key;
    key.vector_pages.fill(0xFC);
    key.except_code_page = 0xFC;
    key.trap_code_page = 0xFF;
    return key;
}

KeyStream::KeyStream(std::span<const std::uint32_t> key, const MachineKey& machine) {
    table_[0] = table_seed; // zzz $C2C
    for (std::uint32_t i = 1; i < table_.size(); ++i) {
        table_[i] = table_[i - 1] * 31 + i;
    }
    for (std::size_t i = 0; i < key.size() && i < table_.size(); ++i) { // zzz $D46
        table_[i] ^= key[i];
    }
    // zzz $DEA: vectors $08-$20 into 10-16, $28-$38 into 10-14, $80-$BC
    // into 32-47 (entry i reads the vector at i*4 - $20, then i*4).
    for (int i = 10; i < 17; ++i) {
        table_[std::size_t(i)] ^= machine.vector_pages[std::size_t(i - 8)];
    }
    for (int i = 10; i < 15; ++i) {
        table_[std::size_t(i)] ^= machine.vector_pages[std::size_t(i)];
    }
    for (int i = 32; i < 48; ++i) {
        table_[std::size_t(i)] ^= machine.vector_pages[std::size_t(i)];
    }
    table_[30] ^= machine.except_code_page;
    table_[31] ^= std::uint32_t(machine.trap_code_page) >> 4; // ASR #4 of a value 0-255
}

std::uint32_t KeyStream::next() { // zzz $F04
    table_[std::size_t(p_)] += table_[std::size_t(q_)];
    p_ = (p_ + 1) % 55;
    q_ += 2;
    if (q_ >= 55) {
        q_ -= 55;
    }
    return table_[std::size_t(p_)];
}

std::vector<std::uint8_t> decrypt_load_file(std::span<const std::uint8_t> file, std::span<const std::uint32_t> key,
                                            const MachineKey& machine) {
    KeyStream stream(key, machine);
    Decoder in(file, stream);
    while (!in.at_end()) { // zzz $3C4
        const auto type = in.raw() & 0x3FFFFFFF;
        switch (type) {
        case unit:
        case name:
        case debug:
            in.skip(std::uint64_t(in.decrypted()) * 4);
            break;
        case code:
        case data:
            for (auto count = in.decrypted() & 0x3FFFFFFF; count > 0; --count) {
                in.decrypted();
            }
            break;
        case bss:
            in.decrypted();
            break;
        case reloc32: // zzz $78C: count, target, offsets ... 0
            for (auto count = in.decrypted(); count != 0; count = in.decrypted()) {
                in.decrypted();
                for (std::uint32_t i = 0; i < count; ++i) {
                    in.decrypted();
                }
            }
            break;
        case reloc16:
        case reloc8: // zzz $844: count, then count + 1 longs as stored
            for (auto count = in.decrypted(); count != 0; count = in.decrypted()) {
                in.skip((std::uint64_t(count) + 1) * 4);
            }
            break;
        case symbol: // zzz $8D2: length, name as stored, value ... 0
            for (auto length = in.decrypted(); length != 0;) {
                in.skip(std::uint64_t(length) * 4);
                in.decrypted();
                length = in.decrypted();
            }
            break;
        case end:
            break;
        case header: { // zzz $94C, $AE8
            for (auto names = in.decrypted(); names != 0;) {
                names = in.decrypted();
                in.skip(std::uint64_t(names) * 4);
            }
            in.decrypted(); // table size
            const auto first = in.decrypted();
            const auto last = in.decrypted();
            if (last < first || last - first > 4096) {
                bad("hunk table");
            }
            for (auto i = first; i <= last; ++i) {
                in.decrypted();
            }
            break;
        }
        default:
            bad("block type");
        }
    }
    auto plain = in.take_output();
    amiga::read_load_file(plain); // throws when the structure is not valid
    return plain;
}

std::uint16_t relocation_free_checksum(std::span<const std::uint8_t> plain_load_file) {
    const auto file = amiga::read_load_file(plain_load_file);
    std::uint16_t sum = 0;
    for (const auto& hunk : file.hunks) {
        std::vector<bool> relocated(hunk.size / 2, false);
        for (const auto& relocation : hunk.relocations) {
            for (const auto offset : relocation.offsets) {
                // A long relocated twice would count twice in zzz's sum; the
                // disk's files have none.
                relocated[offset / 2] = !relocated[offset / 2];
                relocated[offset / 2 + 1] = !relocated[offset / 2 + 1];
            }
        }
        for (std::size_t word = 0; word < relocated.size(); ++word) {
            if (!relocated[word] && word * 2 + 1 < hunk.bytes.size()) {
                sum ^= amiga::read_be16(hunk.bytes, word * 2);
            }
        }
    }
    return sum;
}

} // namespace marble_madness::import
