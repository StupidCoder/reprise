#pragma once
// The first adventure's script commands. Its scripts use the same file and
// token format as the second adventure's (system/script.hpp) and run on the
// same interpreter, but with their own command numbers and with structured
// flow control: if / else-if / else chains, while and repeat loops, and
// condition commands combined with "not", "and" and "or" tokens.
//
// In the game (ARM9, monolithic program): one table of 256 handlers at
// 0220d08c, filled once by 020171b0, serves every script; each handler calls
// the running object's virtual +8, reads its own arguments with the token
// reader 02017e7c, and usually passes them on to a virtual function of the
// object (a room, an event, a puzzle engine ...). The main loop is 02018050.
#include "system/script.hpp"

namespace picarat::adventure1 {

// The first adventure's command table and flow control.
const ScriptDialect& scripts();

namespace command {
// Command numbers named in code.
inline constexpr std::uint16_t MainBackground = 0x0b;
inline constexpr std::uint16_t SubBackground = 0x0c;
inline constexpr std::uint16_t Engine = 0x1b;
inline constexpr std::uint16_t Stop = 0x4f;
inline constexpr std::uint16_t PuzzleTitle = 0xba;
inline constexpr std::uint16_t PuzzleInfo = 0xdc;
} // namespace command

} // namespace picarat::adventure1
