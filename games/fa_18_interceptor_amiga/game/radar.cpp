#include "fa_18_interceptor_amiga/game/radar.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <algorithm>
#include <cstdlib>

namespace interceptor::game {
namespace {

using i32 = std::int32_t;

// An arithmetic shift right rounded up on the last bit shifted out.
i32 shift_rounded(i32 v, int shift) {
    if (shift <= 0) return v;
    const i32 r = v >> shift;
    return ((v >> (shift - 1)) & 1) ? r + 1 : r;
}

// Within the radar's window: x across ±$1B, z from −$15 to $10 ahead.
bool in_window(i32 x, i32 z) { return -x <= 0x1B && -x >= -0x1B && -z < 0x16 && -z >= -0x10; }

} // namespace

void Radar::reset() {
    entries_.clear();
    scan_due_ = false;
    target_request_ = false;
    locked_ = -1;
    confirm_ = 0;
    redraw_ = -1;
}

void Radar::after_scene(Records& records, std::uint16_t frame, bool outside, const FlightTables& tables) {
    scan_due_ = false;
    if (outside) return;
    if (redraw_ <= 1) {
        if ((frame & 2) == 0) scan_due_ = true;
        if (frame & 3) return;
    } else {
        scan_due_ = true;
    }
    // The viewed aircraft's heading turns the others' places ($13AEEC).
    const auto& me = *records.player;
    const auto sc = sine_cosine(tables, std::uint16_t(me.attitude.heading) >> 3);
    const i32 s = sc.sine >> 6, c = sc.cosine >> 6;
    entries_.clear();
    for (int r = 1; r < 16; ++r) {
        const Aircraft* a = records.craft(r);
        if (!a || !(a->state & 0x0040)) continue;
        i32 dx = a->x - me.x, dy = a->y - me.y, dz = a->z - me.z;
        i32 largest = std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz)));
        int shift = 0;
        while (largest > 0x7FFFFF) {
            shift += 2;
            largest >>= 2;
        }
        dx >>= shift + 8;
        dz >>= shift + 8;
        const i32 x = ((c * std::int16_t(dx) + s * std::int16_t(dz)) >> 8) << shift;
        const i32 z = ((-s * std::int16_t(dx) + c * std::int16_t(dz)) >> 8) << shift;
        if (x == 0 && z == 0) break; // (an entry of zeros ends the list)
        entries_.push_back({x, z, r, false});
    }
}

void Radar::panel(Records& records, std::uint16_t frame, int look_shift, int range_shift, std::uint8_t mode) {
    // $125CDE: only while the radar is on screen.
    if (look_shift <= -12 || look_shift >= 12) return;
    (void)frame; // (which part of the radar's frame is drawn: `$13ABCA` & $E)
    scan(records, mode, range_shift);
    // While the panel is redrawn both parts are drawn, each followed by
    // the pass.
    if (redraw_ >= 1) scan(records, mode, range_shift);
}

void Radar::end_panel(std::uint16_t frame) {
    if (redraw_ >= 0) --redraw_;
    if ((frame & 0x1F) == 0x10) redraw_panel(); // ($101492)
}

void Radar::scan(Records& records, std::uint8_t mode, int range_shift) {
    if (!scan_due_) return;
    ++passes_;
    blips_.clear();
    for (auto& e : entries_) {
        e.on_scope = false;
        Aircraft* a = records.craft(e.record);
        if (!a) continue;
        if (!(a->state & 0x0040) || (a->motion & flag::on_ground) ||
            !in_window(shift_rounded(e.x, 13), shift_rounded(e.z, 13))) {
            a->state &= ~0x4000;
            continue;
        }
        const std::uint8_t family = a->type & 0xF0;
        if (mode != 0x7D && mode != 2) {
            if (std::abs(e.x) <= 0x10000 && std::abs(e.z) <= 0x10000) {
                a->handling |= 0x40; // seen
                // Mission 3: an aircraft this near while something is
                // locked confirms it after 30 passes.
                if (family != 0x20 && locked_ >= 0 && family == 0x10) {
                    if (confirm_ == 0) confirm_ = 0x1E;
                    else if (confirm_ > 0 && --confirm_ == 0) confirm_ = -1;
                }
                // (The threat lamps' bits, `$13AB5D`, are not built.)
            } else {
                a->handling &= ~0x40;
            }
        }
        a->state |= 0x4000; // on the radar
        const i32 sx = shift_rounded(e.x, range_shift), sz = shift_rounded(e.z, range_shift);
        e.on_scope = in_window(sx, sz);
        if (!e.on_scope || (e.record == locked_ && !(passes_ & 1))) continue;
        int colour = 1;
        if (family == 0x20 || family == 0x30) colour = 5;
        else if (!(a->handling & 0x40) || (a->handling & 0x02)) colour = 8;
        else if (a->state & 0x0008) colour = 4;
        else if (family == 0) colour = 2;
        blips_.push_back({0x9E - int(sx), 0xA7 - int(sz), colour, (records.player->y >> 8) <= (a->y >> 8)});
    }
    if (target_request_) {
        target_request_ = false;
        pick_target(records);
    }
}

// $126408: the next entry on the scope after the locked one that can be
// locked (an aircraft, not spinning); past the last, nothing.
void Radar::pick_target(Records& records) {
    std::size_t at = 0;
    if (locked_ >= 0) {
        const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.record == locked_; });
        at = it == entries_.end() ? 0 : std::size_t(it - entries_.begin()) + 1; // (not found: message $30)
    }
    for (; at < entries_.size(); ++at) {
        const auto& e = entries_[at];
        const Aircraft* a = records.craft(e.record);
        if (!e.on_scope || !a) continue;
        const std::uint8_t family = a->type & 0xF0;
        if (family == 0 || family == 0x20 || family == 0x30 || (a->handling & 0x02)) continue;
        locked_ = e.record;
        return;
    }
    locked_ = -1;
}

} // namespace interceptor::game
