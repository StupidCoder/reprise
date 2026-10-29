// Sonic 2's script format (scripts.hpp).

#include "sonic2_gg/game/scripts.hpp"
#include "sonic2_gg/game/rom_tables.hpp"

namespace sonic2_gg {

namespace {

using Kind = gg_engine::ScriptKind;

// 6539: the commands; other numbers do nothing (656F).
constexpr std::array<Kind, 256> commands() {
    std::array<Kind, 256> c{};
    c.fill(Kind::nothing);
    c[0] = Kind::restart;       // 6572
    c[1] = Kind::remove;        // 6575
    c[2] = Kind::call;          // 6578
    c[3] = Kind::hold;          // 658E
    c[4] = Kind::velocity;      // 658F
    c[5] = Kind::request_state; // 65BF
    c[6] = Kind::spawn;         // 65D3
    c[7] = Kind::call_and_show; // 6655
    c[8] = Kind::object_tiles;  // 6675
    c[9] = Kind::sound;         // 6689
    return c;
}

const gg_engine::ScriptFormat format{
    .state_tables = rom::object_states,
    .commands = commands(),
    .frame_keep = false,
    .type_bank = [](const Assets&, u8 type) { return object_bank(type); },
};

} // namespace

unsigned object_bank(unsigned type) {
    return type < 0x20 ? 0x1F : type < 0x50 ? 0x1C : 0x1E;
}

// A routine address of 0 names none; an address not reconstructed ends the
// decoded script (running into it fails).
Scripts::Scripts(const Assets& rom)
    : gg_engine::Scripts<Routine>(rom, format, [](unsigned bank, unsigned address) {
          return address ? routine_at(bank, address) : Routine::none;
      }) {}

} // namespace sonic2_gg
