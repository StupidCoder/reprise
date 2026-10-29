#pragma once
#include "gg_engine/data/assets.hpp"
#include "gg_engine/types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

// Object scripts: for each object type and state, a list of steps that a
// game's object engine works through. Both cartridges use the same bytecode
// family (Sonic 2: read by 5F47, commands at 6539; Triple Trouble: 5A86/5AAF,
// commands at 5C19):
//
//   ticks, frame, routine (4 bytes)   show `frame` for `ticks` updates,
//                                     running `routine` on each
//   FE, ticks, frame                  the same, keeping the routine (TT)
//   FF, command, arguments            a command
//
// The commands share their argument layouts but are numbered differently,
// and each revision has some the other lacks; ScriptFormat maps a
// cartridge's command numbers to the step kinds. A type's state table (bank
// 1) gives each state's script address in the type's bank; the cartridge
// keeps a pointer into the script. Here each (type, state) script is decoded
// once into steps, with jump targets resolved to step numbers; an object's
// position is the step number in the script it was started for. Running the
// steps is each game's own (its object engine).
namespace gg_engine {

enum class ScriptKind : u8 {
    frame,         // show `frame` for `ticks` updates, running `routine`
    frame_keep,    // FE: the same, keeping the routine
    restart,       // start the state's script again
    remove,        // remove the object
    call,          // run `routine`, go on
    hold,          // stop here (until the current frame runs out again)
    velocity,      // set the speed (vx mirrored with the object)
    request_state, // ask for state `value`
    spawn,         // an object of `type` at (dx, dy), parameter `value`
    call_and_show, // run `routine`, then show the current frame with `next_routine`
    object_tiles,  // ask for object tiles `value`
    sound,         // play effect `value`
    music,         // play song `value`
    jump,          // continue at step `target`
    branch_if,     // run `routine`; if it says so, continue at step `target`
    set_field,     // the object's byte `offset` = `value`
    poke,          // the work-RAM byte `address` = `value`
    and_field,     // the object's byte `offset` &= `value`
    or_field,      // the object's byte `offset` |= `value`
    show,          // `ticks` updates of `frame` (`frame_mirrored`), running `routine`
    set_loop,      // loop counter = `value`
    loop,          // count down; while not zero continue at step `target`
    spawn_turned,  // as spawn, with `value` added to the object's angle
    nothing,       // a command number with no effect
    invalid,       // past the decodable data: running it is an error
};

template <class Routine>
struct ScriptStep {
    using Kind = ScriptKind;
    Kind kind{Kind::invalid};
    u8 ticks{}, frame{}, frame_mirrored{};
    Routine routine{Routine::none}, next_routine{Routine::none};
    std::uint16_t vx{}, vy{};
    u8 value{}, type{}, offset{};
    std::uint16_t dx{}, dy{}, address{};
    std::uint16_t target{};
    std::uint16_t source{}; // the step's address in the cartridge
    // Where `routine` and `next_routine` lie (for the error when a routine
    // that is not reconstructed yet is reached).
    std::uint16_t routine_address{}, next_routine_address{};
};

// How one cartridge encodes its scripts.
struct ScriptFormat {
    Table state_tables;                // bank 1: per type a word: its state table
    std::array<ScriptKind, 256> commands; // by command number (after FF)
    bool frame_keep;                   // FE starts a frame step keeping the routine
    // The bank of a type's scripts and routines.
    unsigned (*type_bank)(const Assets& rom, u8 type);
};

template <class Routine>
class Scripts {
public:
    using Step = ScriptStep<Routine>;
    // Which routine a script names by its address (in `bank`).
    using RoutineAt = std::function<Routine(unsigned bank, unsigned address)>;

    Scripts(const Assets& rom, const ScriptFormat& format, RoutineAt routine_at)
        : rom_(rom), format_(format), routine_at_(std::move(routine_at)) {}

    // The bank of a type's scripts.
    unsigned bank(u8 type) const { return format_.type_bank(rom_, type); }

    // The script of object `type`'s `state`, in `bank` (0: the type's own).
    // Decoded on first use.
    const std::vector<Step>& script(u8 type, u8 state, unsigned bank = 0) const {
        const auto [b, entry] = locate(type, state, bank);
        const std::uint32_t key = (b << 16) | entry;
        if (const auto found = decoded_.find(key); found != decoded_.end()) {
            return found->second;
        }
        auto& steps = decoded_[key];
        decode(b, entry, steps, indexes_[key]);
        return steps;
    }

    // The step at `address` within the script of (`type`, `state`): routines
    // that set an object's script position directly may point past a loop to
    // a part the script's own steps never reach; it is decoded on first use.
    std::uint16_t step_at(u8 type, u8 state, unsigned address, unsigned bank = 0) const {
        const auto [b, entry] = locate(type, state, bank);
        const std::uint32_t key = (b << 16) | entry;
        script(type, state, bank);
        auto& index = indexes_[key];
        if (const auto at = index.find(address); at != index.end()) {
            return at->second;
        }
        decode(b, address, decoded_[key], index);
        return index.at(address);
    }

private:
    // Longest script decoded: real scripts are a few dozen steps; a state
    // whose table entry points elsewhere ends in an invalid step instead.
    static constexpr std::size_t longest_script = 512;

    const Assets& rom_;
    const ScriptFormat& format_;
    RoutineAt routine_at_;
    mutable std::map<std::uint32_t, std::map<unsigned, std::uint16_t>>
        indexes_;                                                // by bank and address
    mutable std::map<std::uint32_t, std::vector<Step>> decoded_; // by bank and address

    // The script's bank and address: the type's state table, then the
    // state's entry. Both table indexes are 8-bit (they wrap).
    std::pair<unsigned, unsigned> locate(u8 type, u8 state, unsigned bank) const {
        const unsigned b = bank ? bank : this->bank(type);
        const unsigned table = rom_.word(format_.state_tables, u8((type - 1) * 2));
        return {b, rom_.word(b, table + u8(state * 2))};
    }

    // Decodes from `entry` on into `steps`, recording addresses in `index`;
    // jump and loop targets not decoded yet are decoded after.
    void decode(unsigned b, unsigned entry, std::vector<Step>& steps,
                std::map<unsigned, std::uint16_t>& index) const {
        std::vector<std::pair<std::size_t, unsigned>> targets; // step -> target address
        std::vector<unsigned> work{entry};
        auto word = [&](unsigned a) {
            return std::uint16_t(rom_.word(b, a & 0xFFFF));
        };
        auto byte = [&](unsigned a) {
            return rom_.byte(b, a & 0xFFFF);
        };
        auto routine = [&](unsigned address, std::uint16_t& at) {
            at = std::uint16_t(address);
            return routine_at_(b, address);
        };
        try {
            while (!work.empty() && steps.size() < longest_script) {
                unsigned p = work.back();
                work.pop_back();
                if (index.count(p)) {
                    continue;
                }
                while (steps.size() < longest_script) {
                    if (const auto at = index.find(p); at != index.end()) {
                        // Runs into a part decoded before: continue there.
                        Step j;
                        j.kind = ScriptKind::jump;
                        j.target = at->second;
                        j.source = std::uint16_t(p);
                        steps.push_back(j);
                        break;
                    }
                    index[p] = std::uint16_t(steps.size());
                    Step s;
                    s.source = std::uint16_t(p);
                    const u8 ticks = byte(p);
                    if (ticks == 0xFE && format_.frame_keep) {
                        s.kind = ScriptKind::frame_keep;
                        s.ticks = byte(p + 1);
                        s.frame = byte(p + 2);
                        steps.push_back(s);
                        p += 3;
                        continue;
                    }
                    if (ticks != 0xFF) {
                        s.kind = ScriptKind::frame;
                        s.ticks = ticks;
                        s.frame = byte(p + 1);
                        s.routine = routine(word(p + 2), s.routine_address);
                        steps.push_back(s);
                        p += 4;
                        continue;
                    }
                    s.kind = format_.commands[byte(p + 1)];
                    p += 2;
                    bool ends = false;
                    switch (s.kind) {
                    case ScriptKind::restart:
                    case ScriptKind::remove:
                    case ScriptKind::invalid:
                        ends = true; // nothing after these is ever read
                        break;
                    case ScriptKind::call:
                        s.routine = routine(word(p), s.routine_address);
                        p += 2;
                        break;
                    case ScriptKind::velocity:
                        s.vx = word(p);
                        s.vy = word(p + 2);
                        p += 4;
                        break;
                    case ScriptKind::request_state:
                    case ScriptKind::object_tiles:
                    case ScriptKind::sound:
                    case ScriptKind::music:
                    case ScriptKind::set_loop:
                        s.value = byte(p);
                        p += 1;
                        break;
                    case ScriptKind::spawn:
                    case ScriptKind::spawn_turned:
                        s.type = byte(p);
                        s.dx = word(p + 1);
                        s.dy = word(p + 3);
                        s.value = byte(p + 5);
                        p += 6;
                        break;
                    case ScriptKind::call_and_show:
                        s.routine = routine(word(p), s.routine_address);
                        s.next_routine = routine(word(p + 2), s.next_routine_address);
                        p += 4;
                        break;
                    case ScriptKind::jump:
                        targets.emplace_back(steps.size(), word(p));
                        p += 2;
                        ends = true;
                        break;
                    case ScriptKind::branch_if:
                        s.routine = routine(word(p), s.routine_address);
                        targets.emplace_back(steps.size(), word(p + 2));
                        p += 4;
                        break;
                    case ScriptKind::set_field:
                    case ScriptKind::and_field:
                    case ScriptKind::or_field:
                        s.offset = byte(p);
                        s.value = byte(p + 1);
                        p += 2;
                        break;
                    case ScriptKind::poke:
                        s.address = word(p);
                        s.value = byte(p + 2);
                        p += 3;
                        break;
                    case ScriptKind::show:
                        s.ticks = byte(p);
                        s.frame = byte(p + 1);
                        s.frame_mirrored = byte(p + 2);
                        s.routine = routine(word(p + 3), s.routine_address);
                        p += 5;
                        break;
                    case ScriptKind::loop:
                        targets.emplace_back(steps.size(), word(p));
                        p += 2;
                        break;
                    case ScriptKind::hold:
                    case ScriptKind::nothing:
                    case ScriptKind::frame:
                    case ScriptKind::frame_keep:
                        break;
                    }
                    steps.push_back(s);
                    if (ends) {
                        break;
                    }
                }
                // Targets not decoded yet start new parts.
                for (const auto& [step, address] : targets) {
                    if (!index.count(address)) {
                        work.push_back(address);
                    }
                }
            }
            for (const auto& [step, address] : targets) {
                const auto at = index.find(address);
                if (at == index.end()) {
                    throw std::runtime_error("script jump target not decoded");
                }
                steps[step].target = at->second;
            }
            if (steps.size() >= longest_script) {
                steps.push_back(Step{});
            }
        } catch (const std::exception&) {
            // Not script data (or, in Sonic 2, a routine not reconstructed):
            // running into it fails.
            index[entry] = std::uint16_t(steps.size());
            steps.push_back(Step{});
        }
    }
};

} // namespace gg_engine
