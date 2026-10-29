#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"

namespace spindizzy::play {

namespace {

constexpr std::uint32_t arrival_list = 0x12AF2; // kind −3's frames ($12B04)
constexpr std::uint32_t lost_list = 0x12DB8;    // kind −2's frames ($12DB0)
constexpr int arrival_time = 0x0B;              // $12B48: +$2C, counted below 0
constexpr int lost_time = 0x18;                 // $12D8E
constexpr int arrival_flash = 0x13;             // $12B34: $24F2 afterwards

std::int16_t with_low(std::int16_t value, int low) { return std::int16_t((value & 0xFF00) | low); }

} // namespace

std::uint32_t ListStepper::advance(const data::Memory& main, std::uint32_t list) {
    if (--count == 0) {
        ++entry;
        if (std::int16_t(main.u16(list + std::uint32_t(entry) * 4)) < 0) entry = 0;
        count = std::int16_t(main.u16(list + std::uint32_t(entry) * 4)) & 0xFF;
    }
    return main.u16(list + std::uint32_t(entry) * 4 + 2);
}

LevelPlay::LevelPlay(const data::Assets& assets, const SurfaceTable& surfaces, int world, int level)
    : surfaces_(surfaces) {
    const auto& file = assets.worlds.at(std::size_t(world));
    level_ = world::build_level(file, level, assets.block_attributes, switches_);
    const auto header = file.header(level);
    level_flags_ = header.flags;
    // $12720: the start cell's centre; z from the header word's low three
    // bits as the level (hypothesis, see LevelHeader), the explorer falls
    // from there.
    explorer_.respawn_x = std::int16_t(header.start_x() << 8 | 0x80);
    explorer_.respawn_y = std::int16_t(header.start_y() << 8 | 0x80);
    explorer_.respawn_z = std::int16_t((header.first & 7) << 8 | 0x80);
    arrive();
}

// Kind −3's create routine ($12B0C): back at the respawn point (the cell's
// centre, $40 into it), at rest; then its frames for 12 ticks.
void LevelPlay::arrive() {
    explorer_.x = with_low(explorer_.respawn_x, 0x80);
    explorer_.y = with_low(explorer_.respawn_y, 0x80);
    explorer_.z = with_low(explorer_.respawn_z, 0x40);
    explorer_.vx = explorer_.vy = explorer_.vz = 0;
    explorer_.timer_a = explorer_.timer_b = 0;
    explorer_.arrival_ticks = arrival_flash;
    phase_ = ExplorerPhase::arriving;
    phase_ticks_ = arrival_time;
    stepper_ = {};
}

void LevelPlay::tick(std::uint8_t joystick) {
    switch (phase_) {
    case ExplorerPhase::arriving:
        // $12AD2
        if (--phase_ticks_ < 0) {
            phase_ = ExplorerPhase::playing;
            spin_ = 0x100;
        }
        return;
    case ExplorerPhase::lost:
        // $12DA2, then kind −3
        if (--phase_ticks_ < 0) arrive();
        return;
    case ExplorerPhase::playing: break;
    }
    Ground ground{level_.grid, surfaces_, level_flags_, switches_};
    TickEvents events;
    move_explorer(explorer_, joystick, ground, events);
    settle_explorer(explorer_, events);
    if (events.deaths.empty()) return;
    // The last kind change wins (each death replaces the slot's routines).
    const auto& death = events.deaths.back();
    explorer_.arrival_ticks = arrival_flash; // $12B2E, also run for kind −2
    explorer_.timer_a = explorer_.timer_b = 0;
    stepper_ = {};
    if (death.becomes == -3) {
        arrive();
    } else {
        phase_ = ExplorerPhase::lost;
        phase_ticks_ = lost_time;
    }
}

void LevelPlay::brake() {
    if (explorer_.brake_lock == 0 && (explorer_.contact & 0xFF) != 0) explorer_.brake_requested = true;
}

std::uint32_t LevelPlay::explorer_frame(const data::Memory& main) {
    switch (phase_) {
    case ExplorerPhase::arriving: return view::explorer_frames + stepper_.advance(main, arrival_list);
    case ExplorerPhase::lost: return view::explorer_frames + stepper_.advance(main, lost_list);
    case ExplorerPhase::playing: break;
    }
    // $1490A (slow mode): the spin counter steps once a frame; while a
    // timer runs or after arriving the special set shows; on a top every
    // other frame uses the flash set.
    ++spin_;
    const bool special = (explorer_.timer_a & 1) != 0 || explorer_.arrival_ticks > 0;
    bool flash = false;
    if (!special && explorer_.contact != 0) {
        flash = flash_; // bchg: the bit before the change picks the set
        flash_ = !flash_;
    }
    return view::explorer_frame(spin_, flash, special);
}

} // namespace spindizzy::play
