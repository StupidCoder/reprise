#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"

namespace spindizzy::play {

namespace {

constexpr std::uint32_t arrival_list = 0x12AF2; // kind −3's frames ($12B04)
constexpr std::uint32_t lost_list = 0x12DB8;    // kind −2's frames ($12DB0)
constexpr std::uint32_t fuel_cost_tables = 0x10DFE; // $10E5E: difficulty 0, < 6, < 16, more
constexpr int arrival_time = 0x0B;              // $12B48: +$2C, counted below 0
constexpr int lost_time = 0x18;                 // $12D8E
constexpr int arrival_flash = 0x13;             // $12B34: $24F2 afterwards
constexpr int dying_time = 0xC8;                // $12CB8: out of fuel
constexpr std::int16_t respawn_cost = 0x10;     // $12B18

std::int16_t with_low(std::int16_t value, int low) { return std::int16_t((value & 0xFF00) | low); }

// Adds whole units to a 16.16 long's high word (the original's add.w /
// sub.w on the word at the long's address).
void add_whole(std::int32_t& value, int units) {
    value = std::int32_t(std::uint32_t(std::uint16_t(std::int16_t(value >> 16) + units)) << 16 |
                         (std::uint32_t(value) & 0xFFFF));
}

} // namespace

std::uint32_t ListStepper::advance(const data::Memory& main, std::uint32_t list, bool slow) {
    bool next = false;
    if (!slow) next = (count = (count - 1) & 0xFF) == 0;
    if (!next) next = (count = (count - 1) & 0xFF) == 0;
    if (next) {
        ++entry;
        if (std::int16_t(main.u16(list + std::uint32_t(entry) * 4)) < 0) entry = 0;
        count = main.u16(list + std::uint32_t(entry) * 4) & 0xFF;
    }
    return main.u16(list + std::uint32_t(entry) * 4 + 2);
}

void Progress::start_world(const data::Memory& main, int number, int difficulty_steps) {
    world = number;
    level = 1;
    fuel_shown = 0; // $11044, the pillars built empty ($10FC6 from $14980)
    clue_word = 0xFF00; // $149A0
    level_jewels.fill(0x80000000); // $138E0: no level visited
    jewels_found = jewels_taken = 0;
    arrival_place = 0;
    switches.primary = switches.secondary = 0xFF; // word $3616 = $FFFF
    switches.binary = 0;
    rotation = 0;
    // $10E1E: per difficulty step the fuel drain each frame and the costs.
    difficulty = difficulty_steps;
    std::uint32_t table = fuel_cost_tables;
    if (difficulty == 0) {
        drain = 0;
    } else {
        drain = difficulty * 0x11 + 0x280;
        table += difficulty < 6 ? 8 : difficulty < 16 ? 16 : 24;
    }
    costs = {std::int16_t(main.u16(table)), std::int16_t(main.u16(table + 2)), std::int16_t(main.u16(table + 4)),
             std::int16_t(main.u16(table + 6))};
}

LevelPlay::LevelPlay(const data::Assets& assets, const SurfaceTable& surfaces, Progress& progress)
    : assets_(assets), surfaces_(surfaces), progress_(progress) {
    auto& p = progress_;
    const auto& file = assets.worlds.at(std::size_t(p.world));
    header_ = file.header(p.level);
    level_flags_ = header_.flags;
    // The explorer (kind −4, $12B2E): its timers cleared, the arrival
    // flash, the spin counter at $100.
    auto& e = p.explorer;
    e.timer_a = e.timer_b = 0;
    e.arrival_ticks = arrival_flash;
    // $138F6: the level's jewels; bit 31 = the first visit, worth fuel.
    p.jewels = p.level_jewels.at(std::size_t(p.level - 1));
    const bool first_visit = p.jewels & 0x80000000u;
    if (first_visit) add_whole(p.fuel_change, p.costs.first_visit);
    if (level_flags_ & 0x8000) p.rotation = 0; // $12028: a level that does not turn
    level_ = world::build_level(file, p.level, assets.block_attributes, p.switches, p.rotation);
    for (auto condition : level_.conditions) condition_results_.push_back(p.switches.holds(condition));
    // $12714: from an exit's place word, or the header's start cell.
    const std::uint16_t place = p.arrival_place;
    p.arrival_place = 0;
    if (place & 0x8000) place_explorer(place);
    else if (!(place & 0x4000)) place_explorer(header_.first);
    auto w = object_world();
    objects_.emplace(level_, assets.main, surfaces_, w, first_visit, p.jewels_found);
    view::StarRandom running(p.random);
    for (const auto& object : level_.objects)
        if (object.kind == int(world::ObjectKind::starfield)) stars_ = view::make_stars(assets.main, object, running);
    p.random = running.state();
    p.jewels &= ~0x80000000u; // $1236A
    camera_ = view::snap_camera(view::explorer_place({e.x, e.y, e.z}), level_flags_); // $11FB4
    p.reserve_at_start = p.reserve;
    p.jewel_fuel = 0;
    notice_ = (level_flags_ & 0x1000) != 0; // $11FE4: kind −9
}

// $12724: a place word (cells: x bits 8-12, y 3-7, z 0-2) turned with the
// level, the cell's centre; bit 13 puts z on the level itself, bit 14 keeps
// the explorer's speed.
void LevelPlay::place_explorer(std::uint16_t place) {
    auto& e = progress_.explorer;
    int x = place >> 8 & 31, y = place >> 3 & 31;
    for (int turn = 0; turn < (progress_.rotation & 3); ++turn) {
        const int old_x = x;
        x = y;
        y = -old_x & 31;
    }
    e.x = std::int16_t(x << 8 | 0x80);
    e.y = std::int16_t(y << 8 | 0x80);
    e.z = std::int16_t((place & 7) << 8 | ((place & 0x2000) ? 0 : 0x80));
    if (place & 0x4000) return;
    e.vx = e.vy = e.vz = 0;
}

ObjectWorld LevelPlay::object_world() {
    auto& p = progress_;
    ObjectWorld w{p.explorer, level_.grid, p.switches, p.jewels, p.level_jewels.data(), p.jewels_taken,
                  p.fuel_change, p.jewel_fuel, p.timer, p.timer_expired};
    w.level = p.level;
    w.level_flags = level_flags_;
    w.rotation = p.rotation;
    w.random = &p.random;
    w.turn_side = &p.turn_side;
    w.alien_cost = p.costs.alien;
    w.running = running_;
    return w;
}

// Kind −3's create routine ($12B0C): back at the respawn point (the cell's
// centre, $40 into it), at rest, $10 fuel; then its frames for 12 ticks.
void LevelPlay::arrive() {
    auto& e = progress_.explorer;
    e.x = with_low(e.respawn_x, 0x80);
    e.y = with_low(e.respawn_y, 0x80);
    e.z = with_low(e.respawn_z, 0x40);
    e.vx = e.vy = e.vz = 0;
    add_whole(progress_.fuel_change, -respawn_cost);
    progress_.sounds.push_back({1, 3, 0x40}); // $12B1E
    e.timer_a = e.timer_b = 0;
    e.arrival_ticks = arrival_flash;
    phase_ = ExplorerPhase::arriving;
    phase_ticks_ = arrival_time;
    stepper_ = {};
}

void LevelPlay::show_clue(int number, bool always) {
    // $15FC0: once per game ($3610 bit n); messages with bit 7 every time.
    const std::uint32_t bit = 1u << (number & 31);
    const bool unseen = progress_.clues_left & bit;
    progress_.clues_left &= ~bit;
    if (unseen || always) clue_ = ShownClue{number};
}

void LevelPlay::die(const Death& death) {
    // $131F4 took the cost and showed the clue; the explorer's kind changes.
    if (death.costs_energy) add_whole(progress_.fuel_change, -progress_.costs.death);
    if (death.hint >= 0) show_clue(death.hint, false);
    auto& e = progress_.explorer;
    e.arrival_ticks = arrival_flash; // $12B2E, also run for kind −2
    e.timer_a = e.timer_b = 0;
    stepper_ = {};
    if (death.becomes == -3) {
        arrive();
    } else {
        phase_ = ExplorerPhase::lost;
        phase_ticks_ = lost_time;
        progress_.sounds.push_back({1, 2, 0x40}); // $12D7A
    }
}

void LevelPlay::tick(std::uint8_t joystick) {
    auto& p = progress_;
    auto& e = p.explorer;
    switch (phase_) {
    case ExplorerPhase::arriving: // $12AD2
        if (--phase_ticks_ < 0) {
            phase_ = ExplorerPhase::playing;
            spin_ = 0x100;
        }
        break;
    case ExplorerPhase::lost: // $12DA2, then kind −3
        if (--phase_ticks_ < 0) arrive();
        break;
    case ExplorerPhase::out_of_fuel: // $12CE8: a pause, fire ends it early
        if (--phase_ticks_ < 0 || (phase_ticks_ < 0xA0 && (joystick & stick::fire))) {
            p.game_over = true;
            running_ = false;
        }
        break;
    case ExplorerPhase::playing: break;
    }
    TickEvents events;
    if (phase_ == ExplorerPhase::playing) {
        // $12B7C: out of fuel (or the timer killed it): the dying kind.
        if (p.timer_expired || std::int16_t(p.fuel >> 16) <= 0) {
            if (!p.timer_expired) {
                show_clue(1, false);
                p.sounds.push_back({1, 4, 0x40}); // $12CA0
            }
            p.timer_expired = true;
            phase_ = ExplorerPhase::out_of_fuel;
            phase_ticks_ = dying_time;
        } else {
            Ground ground{level_.grid, surfaces_, level_flags_, p.switches};
            move_explorer(e, joystick, ground, events);
        }
    }
    // The objects' slots ($14390), then the explorer's second half.
    auto w = object_world();
    ObjectEvents happened;
    objects_->tick(w, happened);
    if (phase_ == ExplorerPhase::playing) settle_explorer(e, events);
    if (happened.clue >= 0) show_clue(happened.clue, happened.clue_always);
    if (happened.fuel_cost) add_whole(p.fuel_change, -happened.fuel_cost);
    p.sounds.insert(p.sounds.end(), happened.sounds.begin(), happened.sounds.end());
    // $11208: channel 2 for the loudest object heard; its volume each
    // tick, the effect when the object or effect changed; none: silent.
    if (happened.heard_volume < 0) {
        p.sounds.push_back({2, -1, 0});
    } else {
        p.sounds.push_back({2, -1, happened.heard_volume});
        const int heard = int(happened.heard_object) * 256 + happened.heard_effect;
        if (heard != p.heard_last) {
            p.heard_last = heard;
            p.sounds.push_back({2, happened.heard_effect, -1});
        }
    }
    if (events.landing_volume) p.sounds.push_back({0, 6, events.landing_volume}); // $12F52
    if (events.bounced) p.sounds.push_back({0, 0x24, 0x40});                      // $1352C
    if (events.switch_sound) p.sounds.push_back({1, events.switch_sound, -1});    // $1325A
    if (happened.back_to_respawn) { // $12C3E
        e.x = with_low(e.respawn_x, 0x80);
        e.y = with_low(e.respawn_y, 0x80);
        e.z = with_low(e.respawn_z, 0x40);
        e.vx = e.vy = e.vz = 0;
    }
    if (happened.travel) {
        if (happened.travel->level == p.level) {
            place_explorer(happened.travel->place); // $1510C, then $14202
            moved_ = true;
        } else {
            p.level_jewels.at(std::size_t(p.level - 1)) = p.jewels; // $150E8
            p.level = happened.travel->level;
            p.arrival_place = happened.travel->place;
            running_ = false;
        }
    }
    if (!events.deaths.empty() && phase_ == ExplorerPhase::playing) die(events.deaths.back());
}

bool LevelPlay::turn(int direction) {
    if (level_flags_ & 0x8000) return false;
    auto& p = progress_;
    p.rotation = (p.rotation + direction) & 0xFF;
    rebuild(false); // $11FF0; the passages open again from their rotate handlers
    const int turns = -direction & 3; // $143DE
    auto w = object_world();
    objects_->rotate(turns, w);
    auto& e = p.explorer; // $12C64: position and respawn (^ $1FFF), speed
    for (int i = 0; i < turns; ++i) {
        std::int16_t x = e.x;
        e.x = std::int16_t(e.y ^ 0x1FFF);
        e.y = x;
        x = e.respawn_x;
        e.respawn_x = std::int16_t(e.respawn_y ^ 0x1FFF);
        e.respawn_y = x;
        const std::int32_t vx = e.vx;
        e.vx = -e.vy;
        e.vy = vx;
    }
    camera_ = view::snap_camera(view::explorer_place({e.x, e.y, e.z}), level_flags_); // $14202
    return true;
}

void LevelPlay::revive() {
    auto& p = progress_;
    p.fuel = p.reserve;
    p.reserve = 0;
    p.reserve_at_start = 0;
    p.game_over = false;
    p.timer_expired = false;
    auto& e = p.explorer; // $12B2E
    e.timer_a = e.timer_b = 0;
    e.arrival_ticks = arrival_flash;
    phase_ = ExplorerPhase::playing;
    spin_ = 0x100;
    running_ = true;
}

void LevelPlay::centre() {
    const auto& e = progress_.explorer;
    camera_ = view::snap_camera(view::explorer_place({e.x, e.y, e.z}), level_flags_);
}

void LevelPlay::brake() {
    const auto& e = progress_.explorer;
    if (e.brake_lock == 0 && (e.contact & 0xFF) != 0) progress_.explorer.brake_requested = true;
}

void LevelPlay::frame(bool slow) {
    auto& p = progress_;
    const auto& e = p.explorer;
    const auto place = view::explorer_place({e.x, e.y, e.z});
    if (moved_) camera_ = view::snap_camera(place, level_flags_); // a same-level exit: $14202
    else view::follow(camera_, place, e.z, level_flags_);
    moved_ = false;
    // $10E96: the reserve gets an eighth of the jewels' fuel ("Add reserve
    // fuel", level flag bit 12); the fuel takes the changes less the drain.
    if (level_flags_ & 0x1000) {
        std::uint32_t reserve = std::uint32_t(p.reserve) + std::uint32_t(std::int32_t(std::uint32_t(std::uint16_t(p.jewel_fuel)) << 16) >> 3);
        if (reserve > 0xFFFFFF) reserve = 0x1000000;
        p.reserve = std::int32_t(reserve);
    }
    p.jewel_fuel = 0;
    std::int32_t fuel = p.fuel + p.fuel_change - ((level_flags_ & 0x2000) ? 0 : p.drain);
    p.fuel_change = 0;
    if (fuel < 0) {
        fuel = 0;
        p.game_over = true; // $10E8C: $2490 set; the explorer's tick then dies
    } else if (std::uint32_t(fuel) > 0xFFFFFF) {
        fuel = 0x1000000;
    }
    p.fuel = fuel;
    const int whole = fuel >> 16;
    if (p.fuel_shown < whole) ++p.fuel_shown;
    else if (p.fuel_shown > whole) --p.fuel_shown;
    // $1635E every 16 frames: the first object with a condition word within
    // $380 of the explorer whose word is not the one shown shows it; none:
    // the boxes are cleared (so a clue blinks while the explorer is near).
    p.clue_counter = (p.clue_counter - 1) & 15;
    if (p.clue_counter == 0) {
        std::uint16_t found = 0;
        for (const auto& o : objects_->objects()) {
            using K = world::ObjectKind;
            if (!o.alive || !o.word26) continue;
            if (o.kind != K::lift && o.kind != K::exit_point && o.kind != K::heads && o.kind != K::spark_generator &&
                o.kind != K::clue) continue;
            if (std::uint16_t(o.x - e.x + 0x380) >= 0x700 || std::uint16_t(o.y - e.y + 0x380) >= 0x700) continue;
            if (o.word26 == p.clue_word) continue;
            found = o.word26;
            break;
        }
        p.clue_word = found;
    }
    objects_->frame(assets_.main, slow, camera_);
    // $102F6 / $101FE: a sprite drawn through a mask buffer at $2098 (the
    // explorer and every creature) whose corner is on a 16-pixel boundary
    // starts its blit one word early; with its y on an 8-row band and its
    // top not clipped that word is $2096, the generator's state word,
    // which becomes 0. An original bug, kept: it steers the Pacs' turns.
    auto clears = [&](const view::SpritePlace& at) {
        const bool wide = at.key == 1;
        const int across = at.x - camera_.scroll_x + (wide ? 0x20 : 0x10); // $10138 / $100A8
        const int down = at.y - camera_.scroll_y;                          // $1072A
        return across > 0 && across < (wide ? 0x140 : 0x130) && down >= 0 && down < 0xA7 && (at.x & 15) == 0 &&
               (at.y & 7) == 0;
    };
    auto explorer_at = view::explorer_place({e.x, e.y, e.z});
    explorer_at.key = 0;
    bool cleared = clears(explorer_at);
    for (const auto& at : objects_->masked_creature_places(assets_.main)) cleared = cleared || clears(at);
    if (cleared) p.random &= 0xFFFF0000u;
    // $162FE: a condition met while building changed: build again where
    // the explorer is ($11FF0 with $24C0 = $4000).
    for (std::size_t i = 0; i < level_.conditions.size(); ++i) {
        if (p.switches.holds(level_.conditions[i]) != condition_results_[i]) {
            rebuild();
            break;
        }
    }
}

void LevelPlay::rebuild(bool reopen) {
    auto& p = progress_;
    const auto& file = assets_.worlds.at(std::size_t(p.world));
    if (level_flags_ & 0x8000) p.rotation = 0;
    auto objects = level_.objects;
    level_ = world::build_level(file, p.level, assets_.block_attributes, p.switches, p.rotation);
    level_.objects = std::move(objects);
    condition_results_.clear();
    for (auto condition : level_.conditions) condition_results_.push_back(p.switches.holds(condition));
    auto w = object_world();
    if (reopen) objects_->reapply(w); // $143DE: the objects' rotate handlers (secret passages)
    ++builds_;
}

std::optional<ShownClue> LevelPlay::take_clue() {
    auto clue = clue_;
    clue_.reset();
    return clue;
}

std::uint32_t LevelPlay::explorer_frame(const data::Memory& main, bool slow) {
    const auto& e = progress_.explorer;
    switch (phase_) {
    case ExplorerPhase::arriving: return view::explorer_frames + stepper_.advance(main, arrival_list, slow);
    case ExplorerPhase::lost: return view::explorer_frames + stepper_.advance(main, lost_list, slow);
    case ExplorerPhase::out_of_fuel: return view::explorer_frames; // $12D14's list (not rebuilt yet)
    case ExplorerPhase::playing: break;
    }
    // $1490A: the spin counter steps once a frame (twice without slow
    // mode); while a timer runs or after arriving the special set shows;
    // on a top every other frame uses the flash set.
    spin_ += slow ? 1 : 2;
    const bool special = (e.timer_a & (slow ? 1 : 2)) != 0 || e.arrival_ticks > 0;
    bool flash = false;
    if (!special && e.contact != 0) {
        flash = flash_; // bchg: the bit before the change picks the set
        flash_ = !flash_;
    }
    return view::explorer_frame(spin_, flash, special);
}

} // namespace spindizzy::play
