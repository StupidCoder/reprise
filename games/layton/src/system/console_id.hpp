#pragma once
// The console's identity the bonus passcodes of the first two adventures are
// computed from: the DS's MAC address. Natively it is a random stand-in made
// once per installation and kept in the library folder (console.id), shared
// by both games so that a code shown by one works in the other.
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace picarat {

// The stand-in MAC address kept in `directory`/console.id (made on first use).
std::array<std::uint8_t, 6> consoleAddress(const std::filesystem::path& directory);

// An eight-character code from the MAC address and a seed (the same routine
// in both programs: first adventure 0200b618, second 0206ba48): sums of the
// address bytes and the seed, each kept to 8 bits; even positions become
// '0' + b % 9, odd positions 'A' + b % 7.
std::string consoleCode(const std::array<std::uint8_t, 6>& mac, unsigned seed);

} // namespace picarat
