#include "sonic1_gg/frontend/game_over.hpp"
#include <stdexcept>

namespace sonic1_gg {
GameOver::GameOver(const FlowAssets& assets, int count) : continues(count), assets_(assets) {
    // 11E1-11ED: the caller already faded to black and cleared sprites; the
    // display is switched off for the first wait while patterns are loaded.
    screen.patterns = assets.screens.at(9).patterns;
    screen.tiles = assets.screens.at(9).tiles;
    screen.palette = assets.palette(0x16, 0x16);
    screen.enabled = false;
}

void GameOver::patch(unsigned address, int column) {
    // 0612: column, row, tile IDs, FF terminator, attribute D20F=0. 460F
    // stores its computed column in front of a table that starts at the row.
    // The VRAM address is 3800+row*64+column*2 in sixteen bits, so a column
    // past 31 continues on the following row, as in the original.
    if (column < 0) {
        column = assets_.byte(address++);
    }
    unsigned x = unsigned(column) & 255, y = assets_.byte(address++);
    unsigned offset = (y * 64 + x * 2) & 0xffff;
    for (unsigned count = 0; count < 256; ++count, offset += 2) {
        unsigned value = assets_.byte(address++);
        if (value == 255) {
            return;
        }
        if (offset + 1 < screen.tiles.size()) {
            screen.tiles[offset] = value;
            screen.tiles[offset + 1] = 0;
        }
    }
    throw std::runtime_error("Unterminated continue tile row");
}

void GameOver::tick(bool start, bool button) {
    sound_events.clear();
    ++phase_ticks;
    switch (phase) {
    case Phase::Off:
        // 11F0-121C: GAME OVER patterns/rows, scroll 0, palettes 0F/10.
        screen.palette = assets_.palette(0x0f, 0x10);
        screen.scroll_y = 0;
        screen.enabled = true;
        phase = Phase::Hold;
        phase_ticks = 0;
        break;
    case Phase::Hold:
        // 121D: 120 waits with the display on.
        if (phase_ticks < 120) {
            break;
        }
        phase_ticks = 0;
        phase = continues ? Phase::Scroll : Phase::Wait;
        break;
    case Phase::Wait:
        // 1236-124E: up to 180 waits. Button 1/2 ends early, except after
        // the last wait, whose zero count returns before testing input.
        if (phase_ticks == 180 || button) {
            phase = Phase::Done;
            continued = false;
        }
        break;
    case Phase::Scroll:
        // 1250-125D: GAME OVER moves up one line after each of 16 waits.
        ++screen.scroll_y;
        if (phase_ticks < 16) {
            break;
        }
        // 125F-1271: one 2x2 icon per credit (460F), centred on column 16,
        // then the two CONTINUE ? rows. 1274: the countdown starts at 9.
        for (int i = 0; i < continues; ++i) {
            int column = (16 - continues + 2 * i) & 255;
            patch(0x12c7, column);
            patch(0x12cb, column);
        }
        patch(0x12cf);
        patch(0x12dc);
        digit = 9;
        phase = Phase::Countdown;
        phase_ticks = 0;
        break;
    case Phase::Countdown:
        // 127B-12A7: redraw the digit after every wait (49B2/2FA8: a blank
        // cell at X=74, then the digit at X=7C, Y=67). Start continues.
        screen.sprites.clear();
        screen.sprites.push_back({0x7c, 0x67, unsigned(0x80 + 2 * digit)});
        if (start) {
            phase = Phase::Done;
            continued = true;
            --continues;
            break;
        }
        if (phase_ticks < 60) {
            break;
        }
        // 12A9-12B3: tick sound each second; after 0 is shown, give up.
        sound_events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::Chime)});
        phase_ticks = 0;
        if (!digit) {
            phase = Phase::Done;
            continued = false;
            break;
        }
        --digit;
        break;
    case Phase::Done:
        break;
    }
}
} // namespace sonic1_gg
