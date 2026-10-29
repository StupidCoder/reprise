// Triple Trouble's script format (scripts.hpp).

#include "triple_trouble_gg/game/scripts.hpp"
#include <stdexcept>
#include <string>

namespace triple_trouble_gg {

namespace {

using Kind = gg_engine::ScriptKind;

// 502C: per object type (00-6F) the bank of its scripts and routines.
constexpr Table type_banks{0x01, 0x502C};
constexpr unsigned type_bank_count = 0x70;

// 5C19: the commands; other numbers are not commands (the table ends).
constexpr std::array<Kind, 256> commands() {
    std::array<Kind, 256> c{};
    c.fill(Kind::invalid);
    c[0] = Kind::restart;        // 5C40
    c[1] = Kind::call;           // 5C4C
    c[2] = Kind::velocity;       // 5C62
    c[3] = Kind::request_state;  // 5C92
    c[4] = Kind::spawn;          // 5CA6
    c[5] = Kind::call_and_show;  // 5D28
    c[6] = Kind::sound;          // 5D5C
    c[7] = Kind::jump;           // 5D84
    c[8] = Kind::branch_if;      // 5D97
    c[9] = Kind::set_field;      // 5DBF
    c[10] = Kind::poke;          // 5DD9
    c[11] = Kind::and_field;     // 5DEF
    c[12] = Kind::or_field;      // 5E0B
    c[13] = Kind::show;          // 5E27
    c[14] = Kind::set_loop;      // 5E53
    c[15] = Kind::loop;          // 5E67
    c[16] = Kind::spawn_turned;  // 5EA3
    c[17] = Kind::music;         // 5D70
    return c;
}

const gg_engine::ScriptFormat format{
    .state_tables = {0x01, 0x5B2D},
    .commands = commands(),
    .frame_keep = true,
    .type_bank =
        [](const Assets& rom, u8 type) {
            if (type >= type_bank_count) {
                throw std::runtime_error("Object type " + std::to_string(type) + " has no bank");
            }
            return unsigned(rom.byte(type_banks, type));
        },
};

} // namespace

// Routines not reconstructed decode as Routine::unknown; running them fails.
Scripts::Scripts(const Assets& rom) : gg_engine::Scripts<Routine>(rom, format, routine_at) {}

} // namespace triple_trouble_gg
