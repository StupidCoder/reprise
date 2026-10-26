#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The disk's encrypted load files (c/xxx and c/MarbleMadness!.dat) and the
// decryptor c/zzz that the launcher runs on them.
//
// c/zzz is a LoadSeg replacement. It reads an AmigaDOS load file whose block
// type markers (and the raw-skipped name and debug bytes) are stored as they
// are, while every other long - sizes, relocation tables, hunk contents - is
// XORed with a keystream, one keystream long per stored long in file order
// (zzz $2C0 reads a long, $F04 makes the next keystream long).
//
// The keystream (zzz $C2C, $D46, $DEA, $F04): a table of 55 longs starts at
// $57319753 with t[i] = t[i-1] * 31 + i; the caller's key array is XORed
// into its first entries; then bytes of the machine's state are XORed in (the
// copy protection, see MachineKey); then each step adds t[q] to t[p], moves p
// on by one and q by two (both round 55, q starting at 27) and returns t[p]
// at its new place.
namespace marble_madness::import {

// The machine state the protection (zzz $DEA) folds into the table: bits
// 16-23 of the CPU's exception vectors $08-$20 (into entries 10-16), $28-$38
// (10-14) and the TRAP vectors $80-$BC (32-47), of the launcher task's
// tc_ExceptCode (entry 30) and of its tc_TrapCode shifted right by 4 (entry
// 31). On Kickstart 1.2/1.3 every one of those points into the ROM at
// $FC0000-$FFFFFF: the vectors and tc_ExceptCode into page $FC, tc_TrapCode
// into page $FF. The game decrypts only on such a machine.
struct MachineKey {
    std::array<std::uint8_t, 0x100 / 4> vector_pages{}; // per vector number 0-63
    std::uint8_t except_code_page{};
    std::uint8_t trap_code_page{};

    static MachineKey kickstart_1x();
};

class KeyStream {
public:
    KeyStream(std::span<const std::uint32_t> key, const MachineKey&);
    std::uint32_t next();

private:
    std::array<std::uint32_t, 55> table_{};
    int p_{0};
    int q_{27};
};

// Decrypts a load file the way c/zzz reads it, writing the plain load file
// (markers, decrypted longs and raw bytes in their order). Throws
// std::runtime_error when the stream does not decode to valid blocks.
std::vector<std::uint8_t> decrypt_load_file(std::span<const std::uint8_t> file, std::span<const std::uint32_t> key,
                                            const MachineKey&);

// The launcher's integrity value (MarbleMadness! $574-$58A): the XOR of all
// words of c/xxx's loaded hunks (checksum_seglist, $DA2) XORed with zzz's
// relocation checksum of the same load. The relocated longs' words appear in
// both, so the value is the XOR of every word of the plain hunks that is not
// part of a relocated long, independent of the load addresses.
std::uint16_t relocation_free_checksum(std::span<const std::uint8_t> plain_load_file);

} // namespace marble_madness::import
