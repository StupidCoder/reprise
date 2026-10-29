// Stepping-stones engine (ARM9 overlay 53). See a2_bridge_puzzle.hpp for the rules.
#include "puzzles/a2_bridge_puzzle.hpp"
#include <algorithm>
#include <cmath>

namespace picarat {

namespace {
// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}

// Float to int as the game's helper (0203aed0) does for these small values.
int f2i(float v) {
    return std::isnan(v) ? 0 : int(v);
}

// Row and foot to pixels (020f7aac / 020f7ac0).
int pixelX(int row) {
    return (0x11 - row) * 0xc + 0xb;
}

int pixelY(int foot) {
    return foot * 0x10 + 0x72;
}

// Pixels to row and foot (020f7acc / 020f7af4). The added constants keep the
// divisions positive so positions outside the path give invalid values.
int row(int x) {
    return 0x11 - ((x + 0x175) / 12 - 0x20);
}

int foot(int y) {
    return (y + 0x18e) / 16 - 0x20;
}
} // namespace

void BridgePuzzle::reset() {
    state_ = kWalking;
    timer_ = 0;
    for (auto& r : cells_) {
        r = {kStone, kStone};
    }
    // The walk starts with both feet on row 0.
    row_ = 0;
    cells_[0] = {kFootprint, kFootprint};
    // The missing stones, at these byte offsets of the original object
    // (+0x6e is row 0, right foot; each row is two bytes).
    for (int offset : {0x75, 0x76, 0x7d, 0x80, 0x81, 0x82, 0x8a, 0x8d, 0x90}) {
        const int i = offset - 0x6e;
        cells_[std::size_t(i / 2)][std::size_t(i % 2)] = kHole;
    }
    markNextSteps();
}

void BridgePuzzle::markNextSteps() {
    // Remove footprints beyond the current row (after going back) and all
    // previous candidates...
    for (int r = 0; r < 18; ++r) {
        for (int f = 0; f < 2; ++f) {
            auto& c = cells_[std::size_t(r)][std::size_t(f)];
            if (c == kFootprint) {
                if (row_ < r) {
                    c = kStone;
                }
            } else if (c == kNextStep) {
                c = kStone;
            }
        }
    }
    // ...then, from each footprint on the current row, the other foot may go
    // one or three rows further, onto a stone.
    for (int f = 0; f < 2; ++f) {
        if (cells_[std::size_t(row_)][std::size_t(f)] != kFootprint) {
            continue;
        }
        for (int ahead : {1, 3}) {
            const int r = row_ + ahead;
            if (r < 18 && cells_[std::size_t(r)][std::size_t(1 - f)] == kStone) {
                cells_[std::size_t(r)][std::size_t(1 - f)] = kNextStep;
            }
        }
    }
    // Restart the pulse so the new candidates fade in from full brightness.
    phase_ = 0;
}

void BridgePuzzle::update(const TouchFilter& t) {
    if (state_ != kWalking) {
        if (state_ == kAcross) {
            timer_ = s16(timer_ + 1);
        }
        return;
    }
    // Only new presses count, at the press position (0206e288).
    if (t.state != 1) {
        return;
    }
    const auto r = unsigned(row(t.x));
    const auto f = unsigned(foot(t.y));
    if (r >= 18 || f >= 2) {
        return;
    }
    auto& c = cells_[r][f];
    if (c == kFootprint) {
        // Back to an earlier footprint.
        sounds.play(0xf8);
        row_ = s16(int(r));
    } else if (c == kNextStep) {
        sounds.play(0xf7);
        c = kFootprint;
        row_ = s16(int(r));
        // A footprint on the last row (17) completes the crossing.
        if (row_ > 0x10) {
            timer_ = 0;
            state_ = kAcross;
        }
    }
    markNextSteps();
}

std::vector<BridgePuzzle::Draw> BridgePuzzle::draws() {
    // The candidates' brightness follows 16·cos(phase) + 16, clamped to
    // 0..31, with the phase growing by 0.1 per frame (overlay 2, 0206b370;
    // the cosine is computed in double precision, 02038110). After every
    // step the phase restarts at 0, i.e. full brightness.
    int pulse = f2i(16.0f * float(std::cos(double(phase_)))) + 0x10;
    pulse = std::clamp(pulse, 0, 0x1f);
    phase_ = 0.1f + phase_;

    // Footprints: "r"/"l" for the right/left foot; frame 0 for the latest
    // footprint, 2 for older ones, 1 for a candidate stone. Row 0 (the
    // starting position) is not drawn.
    std::vector<Draw> out;
    for (int r = 1; r <= 0x11; ++r) {
        for (int f = 0; f < 2; ++f) {
            const auto c = cells_[std::size_t(r)][std::size_t(f)];
            if (c == kFootprint) {
                const char* animation = r == row_ ? (f ? "l0" : "r0") : (f ? "l2" : "r2");
                out.push_back({animation, pixelX(r), pixelY(f), 0x1f});
            } else if (c == kNextStep && pulse > 0) {
                out.push_back({f ? "l1" : "r1", pixelX(r), pixelY(f), pulse});
            }
        }
    }
    return out;
}

} // namespace picarat
