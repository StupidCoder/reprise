// The third adventure's story tables. See a3_data.hpp.
#include "story/a3_data.hpp"
#include <cstring>

namespace picarat::adventure3 {
namespace {
using import::adventure3::Lscr;
using import::adventure3::LscrCommand;

int integer(const LscrCommand& c, std::size_t i) {
    return i < c.operands.size() ? int(std::int32_t(c.operands[i].value)) : 0;
}

std::string text(const Lscr& s, const LscrCommand& c, std::size_t i) {
    return i < c.operands.size() && c.operands[i].type == 2 ? s.text(c.operands[i]) : std::string{};
}

// A row operand as a script value: texts stay texts, floats floats.
lscr::Value value(const Lscr& s, const import::adventure3::LscrOperand& o) {
    switch (o.type) {
    case 1: {
        float f;
        std::memcpy(&f, &o.value, 4);
        return lscr::Value::ofFloat(f);
    }
    case 2:
        return lscr::Value::ofText(s.text(o));
    default:
        return lscr::Value::of(int(std::int32_t(o.value)));
    }
}

TableCommand command(const Lscr& s, const LscrCommand& c) {
    TableCommand t;
    t.opcode = integer(c, 0);
    for (std::size_t i = 1; i < c.operands.size(); ++i) {
        t.operands.push_back(value(s, c.operands[i]));
    }
    return t;
}
} // namespace

void StoryData::load(Assets& assets) {
    // lt3_evt 5401 (020dccb4): {group, id, name, program, sound set, puzzle,
    // memo, ...}.
    const auto evt = assets.script("rc/ge/lt3_rc/lt3_evt.lbin");
    for (const auto& c : evt.commands) {
        if (c.opcode == 5401 && c.operands.size() >= 8) {
            EventInfo e;
            e.group = integer(c, 0);
            e.id = integer(c, 1);
            e.name = text(evt, c, 2);
            e.program = text(evt, c, 3);
            e.soundSet = integer(c, 4);
            e.puzzle = text(evt, c, 5);
            e.memo = text(evt, c, 6);
            e.extra = text(evt, c, 7);
            eventsByName_.emplace(e.name, e.id);
            events_[e.id] = std::move(e);
        }
    }
    // lt3_prg: 5910 starts a program, 5912 a variant {base, label}, 5920
    // its conditions, 5921 / 5922 its actions before / after, 5911 ends the
    // program (020dcb24,
    // 020dcb80, 020dc940, 020dc998, 020dca28); 5810 starts a phase {id,
    // name, save label, goal}, 5812 actions, 5813 conditions, 5811 ends it
    // (020dc574 .. 020dc808).
    const auto prg = assets.script("rc/ge/lt3_rc/lt3_prg.lbin");
    std::vector<ProgramVariant>* program = nullptr;
    Phase* phase = nullptr;
    for (const auto& c : prg.commands) {
        switch (c.opcode) {
        case 5910:
            program = &programs_[text(prg, c, 0)];
            break;
        case 5911:
            program = nullptr;
            break;
        case 5912:
            if (program) {
                program->push_back({text(prg, c, 0), text(prg, c, 1), {}, {}, {}});
            }
            break;
        case 5920:
            if (program && !program->empty()) {
                program->back().conditions.push_back(command(prg, c));
            }
            break;
        case 5921:
            if (program && !program->empty()) {
                program->back().before.push_back(command(prg, c));
            }
            break;
        case 5922:
            if (program && !program->empty()) {
                program->back().after.push_back(command(prg, c));
            }
            break;
        case 5810:
            phases_.push_back(
                {integer(c, 0), text(prg, c, 1), text(prg, c, 2), text(prg, c, 3), {}, {}});
            phase = &phases_.back();
            break;
        case 5811:
            phase = nullptr;
            break;
        case 5812:
            if (phase) {
                phase->actions.push_back(command(prg, c));
            }
            break;
        case 5813:
            if (phase) {
                phase->conditions.push_back(command(prg, c));
            }
            break;
        default:
            break;
        }
    }
    // lt3_plc 5101 (020db990): {id, name, title, sound set, map, guide}.
    const auto plc = assets.script("rc/ge/lt3_rc/lt3_plc.lbin");
    for (const auto& c : plc.commands) {
        if (c.opcode == 5101 && c.operands.size() >= 6) {
            PlaceInfo p;
            p.id = integer(c, 0);
            p.name = text(plc, c, 1);
            p.title = text(plc, c, 2);
            p.soundSet = integer(c, 3);
            p.map = text(plc, c, 4);
            p.guide = text(plc, c, 5);
            placesByName_.emplace(p.name, p.id);
            places_[p.id] = std::move(p);
        }
    }
    // lt3_map 5201 (020dbac4): {id, name, picture}.
    const auto map = assets.script("rc/ge/lt3_rc/lt3_map.lbin");
    for (const auto& c : map.commands) {
        if (c.opcode == 5201 && c.operands.size() >= 3) {
            MapInfo m{integer(c, 0), text(map, c, 1), text(map, c, 2)};
            mapsByName_.emplace(m.name, m.id);
            maps_[m.id] = std::move(m);
        }
    }
    const auto gmp = assets.script("rc/ge/lt3_rc/lt3_gmp.lbin");
    GuideInfo* guide = nullptr;
    for (const auto& c : gmp.commands) {
        if (c.opcode == 5241 && c.operands.size() >= 2) {
            guides_[text(gmp, c, 1)] = integer(c, 0);
            guide = &guideInfos_[integer(c, 0)];
            guide->id = integer(c, 0);
            guide->name = text(gmp, c, 1);
        } else if (c.opcode == 5242) {
            guide = nullptr;
        } else if (c.opcode == 5243 && guide) {
            for (std::size_t i = 0; i + 2 < c.operands.size(); i += 3) {
                guide->places[text(gmp, c, i)] = {integer(c, i + 1), integer(c, i + 2)};
            }
        } else if (c.opcode == 5244 && guide) {
            for (std::size_t i = 0; i + 3 < c.operands.size(); i += 4) {
                guide->goals.emplace(
                    text(gmp, c, i),
                    std::array<int, 3>{integer(c, i + 1), integer(c, i + 2), integer(c, i + 3)});
            }
        }
    }
    for (const auto& c : prg.commands) {
        if (c.opcode == 5701 && c.operands.size() >= 4) {
            timings_[text(prg, c, 0)] = {integer(c, 1), integer(c, 2), integer(c, 3)};
        }
    }
    const auto chr = assets.script("rc/ge/lt3_rc/lt3_chr.lbin");
    for (const auto& c : chr.commands) {
        if (c.opcode == 5001 && c.operands.size() >= 5) {
            CharacterInfo ch{integer(c, 0), text(chr, c, 1), text(chr, c, 2), text(chr, c, 3),
                             integer(c, 4)};
            characters_[ch.name] = ch;
        }
    }
    // lt3_gmd 5521 (020dc144): items; the kind from the name's prefix.
    static const std::pair<std::uint32_t, int> kPrefixes[] = {
        {0x020f6ac0, 4},    {0x020f6ad4, 2},    {0x020f6ae8, 8},   {0x020f6af4, 0x10},
        {0x020f6b08, 0x20}, {0x020f6b14, 0x40}, {0x020f6b24, 0x80}};
    itemPrefixes.clear();
    for (auto [address, mask] : kPrefixes) {
        itemPrefixes.emplace_back(assets.codeText(6, 0x020a7980, address), mask);
    }
    const auto gmd = assets.script("rc/ge/lt3_rc/lt3_gmd.lbin");
    std::vector<std::pair<int, std::vector<std::string>>> memberNames;
    for (const auto& c : gmd.commands) {
        if (c.opcode == 5521 && c.operands.size() >= 4) {
            ItemInfo it;
            it.id = integer(c, 0);
            it.name = text(gmd, c, 1);
            it.value = integer(c, 2);
            it.title = text(gmd, c, 3);
            for (const auto& [prefix, mask] : itemPrefixes) {
                if (it.name.rfind(prefix, 0) == 0) {
                    it.mask = mask;
                    break;
                }
            }
            std::vector<std::string> members;
            for (std::size_t i = 4; i < c.operands.size(); ++i) {
                members.push_back(text(gmd, c, i));
            }
            memberNames.emplace_back(it.id, members);
            itemsByName_[it.name] = it.id;
            items_[it.id] = std::move(it);
        }
    }
    for (const auto& [id, names] : memberNames) {
        for (const auto& n : names) {
            if (auto it = itemsByName_.find(n); it != itemsByName_.end()) {
                items_[id].members.push_back(it->second);
            }
        }
    }
    const auto nazo = assets.script("rc/ge/lt3_rc/lt3_nazo.lbin");
    for (const auto& c : nazo.commands) {
        if (c.opcode == 3001 && c.operands.size() >= 10) {
            puzzles_[text(nazo, c, 1)] = integer(c, 0);
            PuzzleInfo p;
            p.number = integer(c, 0);
            p.label = text(nazo, c, 1);
            p.title = text(nazo, c, 2);
            p.record = text(nazo, c, 3);
            p.type = integer(c, 4);
            p.typeLabel = text(nazo, c, 5);
            p.picarats = {integer(c, 6), integer(c, 7), integer(c, 8)};
            p.titlePicture = text(nazo, c, 9);
            puzzleInfos_[p.number] = p;
        }
    }
    for (const auto& c : nazo.commands) {
        if (c.opcode == 3011 && c.operands.size() >= 4) {
            auto it = puzzles_.find(text(nazo, c, 0));
            slots_.push_back({it == puzzles_.end() ? -1 : it->second, text(nazo, c, 1),
                              text(nazo, c, 2), text(nazo, c, 3)});
        }
    }
}

const ItemInfo* StoryData::item(const std::string& name) const {
    auto it = itemsByName_.find(name);
    return it == itemsByName_.end() ? nullptr : item(it->second);
}

const ItemInfo* StoryData::item(int id) const {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

const CharacterInfo* StoryData::character(const std::string& name) const {
    auto it = characters_.find(name);
    return it == characters_.end() ? nullptr : &it->second;
}

const GuideInfo* StoryData::guideInfo(int id) const {
    auto it = guideInfos_.find(id);
    return it == guideInfos_.end() ? nullptr : &it->second;
}

const PuzzleInfo* StoryData::puzzleInfo(int number) const {
    auto it = puzzleInfos_.find(number);
    return it == puzzleInfos_.end() ? nullptr : &it->second;
}

int StoryData::slotOf(int number) const {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].number == number) {
            return int(i);
        }
    }
    return -1;
}

const std::array<int, 3>* StoryData::programTimings(const std::string& name) const {
    auto it = timings_.find(name);
    return it == timings_.end() ? nullptr : &it->second;
}

int StoryData::guide(const std::string& name) const {
    auto it = guides_.find(name);
    return it == guides_.end() ? -1 : it->second;
}

int StoryData::puzzle(const lscr::Value& id) const {
    if (!id.isText()) {
        return id.toInt();
    }
    auto it = puzzles_.find(id.toText());
    return it == puzzles_.end() ? -1 : it->second;
}

const EventInfo* StoryData::event(int id) const {
    auto it = events_.find(id);
    return it == events_.end() ? nullptr : &it->second;
}

const EventInfo* StoryData::event(const std::string& name) const {
    auto it = eventsByName_.find(name);
    return it == eventsByName_.end() ? nullptr : event(it->second);
}

const std::vector<ProgramVariant>* StoryData::program(const std::string& name) const {
    auto it = programs_.find(name);
    return it == programs_.end() ? nullptr : &it->second;
}

const PlaceInfo* StoryData::place(int id) const {
    auto it = places_.find(id);
    return it == places_.end() ? nullptr : &it->second;
}

const PlaceInfo* StoryData::place(const std::string& name) const {
    auto it = placesByName_.find(name);
    return it == placesByName_.end() ? nullptr : place(it->second);
}

const MapInfo* StoryData::map(int id) const {
    auto it = maps_.find(id);
    return it == maps_.end() ? nullptr : &it->second;
}

const MapInfo* StoryData::map(const std::string& name) const {
    auto it = mapsByName_.find(name);
    return it == mapsByName_.end() ? nullptr : map(it->second);
}

const Phase* StoryData::phase(int id) const {
    for (const auto& p : phases_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const Phase* StoryData::phase(const std::string& name) const {
    for (const auto& p : phases_) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace picarat::adventure3
