// The act results screen and the special stage results screen.

#include "sonic1_gg/frontend/results.hpp"
#include <algorithm>

namespace sonic1_gg {
Results::Results(const FlowAssets& assets, unsigned act, int points, int ring_count, int life_count,
                 int elapsed, int emeralds, int value, int credits, unsigned threshold)
    : score(points), rings(ring_count), lives(life_count), ring_value(value), continues(credits),
      special(act >= 28) {
    next_life_score_bcd = threshold;
    screen.patterns = assets.screens[special ? 5 : 4].patterns;
    screen.tiles = assets.screens[special ? 5 : 4].tiles;
    screen.palette = assets.palette(10, 11);
    // 44FF: the ACT number is two tiles wide and four rows high.
    unsigned offset = special ? 0 : assets.byte(0x4aac + act);
    for (unsigned row = 0; !special && row < 4; ++row) {
        for (unsigned col = 0; col < 2; ++col) {
            screen.tiles[((6 + row) * 32 + 23 + col) * 2] =
                assets.byte(0x4a94 + offset + row * 2 + col);
        }
    }
    // Time bonus: the table at 4481 lists 14 time limits (BCD minutes and
    // seconds); finishing under a limit awards the matching entry of the bonus
    // table at 4457 (three BCD bytes per entry). Slower than all limits: none.
    // 460F repeats a two-row emerald marker, centred using the collection count.
    for (int i = 0; i < emeralds; ++i) {
        for (unsigned row = 0; row < 2; ++row) {
            for (unsigned col = 0; col < 2; ++col) {
                unsigned y = assets.byte(0x4645 + row * 4), x = 19 - emeralds + i * 2;
                screen.tiles[(y * 32 + x + col) * 2] = assets.byte(0x4646 + row * 4 + col);
            }
        }
    }
    if (special) {
        return; // 459C uses the zero bonus at 489B.
    }
    auto decimal = [](unsigned b) {
        return int((b >> 4) * 10 + (b & 15));
    };
    unsigned bonus = 0x4457;
    const int seconds = elapsed / 60;
    for (unsigned i = 0; i < 14; ++i) {
        int threshold =
            decimal(assets.byte(0x4481 + i * 2)) * 60 + decimal(assets.byte(0x4482 + i * 2));
        if (seconds >= threshold) {
            break;
        }
        bonus += 3;
        if (i == 13) {
            bonus = 0x4457;
        }
    }
    time_bonus = decimal(assets.byte(bonus)) * 10000 + decimal(assets.byte(bonus + 1)) * 100 +
                 decimal(assets.byte(bonus + 2));
}

void Results::number(int value, unsigned digits, int x, int y, bool leading) {
    std::vector<unsigned> values(digits);
    for (unsigned i = digits; i > 0; --i) {
        values[i - 1] = value % 10;
        value /= 10;
    }
    bool visible = !leading;
    for (unsigned i = 0; i < digits; ++i) {
        visible |= values[i] != 0 || i == digits - 1;
        if (visible) {
            screen.sprites.push_back({x + int(i) * 8, y, 128 + 2 * values[i]});
        }
    }
}

void Results::numbers() {
    // 489F builds the sprite list before changing the counters. Consequently
    // displayed values trail the arithmetic state by one update during counting.
    screen.sprites.clear();
    number(score, 8, 128, special ? 80 : 88);
    number(rings, 2, special ? 128 : 136, special ? 104 : 128);
    number(ring_value, 4, 160, special ? 104 : 128);
    number(lives, 2, 80, 151, false);
    if (special) {
        number(continues, 2, 152, 128);
    } else {
        number(time_bonus, 8, 128, 112);
    }
}

void Results::tick(bool skip) {
    sound_events.clear();
    if (phase == Phase::Done) {
        return;
    }
    ++ticks;
    ++phase_ticks;
    numbers();
    if (phase == Phase::Hold) {
        if (phase_ticks == 120) {
            phase = Phase::Count;
            phase_ticks = 0;
        }
        return;
    }
    if (phase == Phase::Count) {
        if (rings) {
            --rings;
            award_points(score, lives, next_life_score_bcd, ring_value, sound_events);
        }
        if (time_bonus) {
            time_bonus -= 100;
            award_points(score, lives, next_life_score_bcd, 100, sound_events);
        }
        if ((++sound_counter & 3) == 0) {
            sound_events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::Ring)});
        }
        if (!rings && !time_bonus) {
            phase = Phase::After;
            phase_ticks = 0;
        }
        return;
    }
    if (phase_ticks == 180 || skip) {
        phase = Phase::Done;
    }
}
} // namespace sonic1_gg
