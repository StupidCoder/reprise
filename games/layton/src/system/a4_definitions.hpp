#pragma once
// The fourth adventure's definitions: res/@/lt4_def.cb, a data list the
// game loop reads at its start into the game data's tables (020b8c68):
// puzzles (2301), places (2151), rooms (2131), map areas (2141), events
// (2321) and their kinds (2211), sound sets (2111), flags (2001),
// variables (2011), story steps (2201), characters (2121) and more. Rows
// are kept as values (numbers or texts) by their record number; the
// tables' meanings are documented where the services read them.
//
// Field meanings were checked against the game's records in RAM (harness
// snapshot of the title, game +0x18c): see .analysis/adventure4/docs/story.md.
#include "graphics/a4_assets.hpp"
#include "system/a4_gss.hpp"
#include <map>
#include <string>
#include <vector>

namespace picarat::adventure4 {

class Definitions {
public:
    using Row = std::vector<gss::Value>;

    explicit Definitions(Assets& assets);

    // Every row of record `opcode`, in file order.
    const std::vector<Row>& rows(int opcode) const;

    // The first row of `opcode` whose field `field` equals `key` (a number
    // or a text); null when there is none.
    const Row* find(int opcode, std::size_t field, const gss::Value& key) const;

    // A row's number or text field (0 / empty when missing).
    static int number(const Row& row, std::size_t field);
    static std::string text(const Row& row, std::size_t field);

private:
    std::map<int, std::vector<Row>> rows_;
};

// The definitions of an asset set, read once.
const Definitions& definitions(Assets& assets);

} // namespace picarat::adventure4
