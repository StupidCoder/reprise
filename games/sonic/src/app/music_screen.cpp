// The sound test screen.

#include "app/shell_frame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shell {

// --- Music player ----------------------------------------------------------
namespace {
constexpr int visible_rows = 17;

// The first track shown: the list scrolls to keep the selection near the
// middle once a game has more tracks than fit.
int first_row(int selection, int count) {
    return std::clamp(selection - visible_rows / 2, 0, std::max(0, count - visible_rows));
}
} // namespace

int music_hit(const gg::MusicPlayer& player, int selection, int x, int y) {
    const int count = int(player.tracks().size());
    if (x >= 32 && x < 448 && y >= 138 && y < 580) {
        const int n = (y - 138) / 26 + first_row(selection, count);
        if (n < count && (y - 138) % 26 < 24) {
            return n;
        }
    }
    for (int n = 0; n < 4; ++n) {
        if (x >= 476 + n * 116 && x < 580 + n * 116 && y >= 460 && y < 506) {
            return 100 + n;
        }
    }
    if (x >= 476 && x < 760 && y >= 548 && y < 586) {
        return 104;
    }
    if (x >= 784 && x < 928 && y >= 548 && y < 586) {
        return 105;
    }
    return -1;
}

platform::Image music_image(const gg::MusicPlayer& player, std::string_view game, int selection,
                            int focus, bool muted, double time) {
    Image im(960, 640);
    const auto& t = theme(0);
    backdrop(im, t, 0, 0, 960, 640, int(time * 18));
    blend_rect(im, 0, 92, 960, 512, navy, 70);
    const auto& tracks = player.tracks();
    const int count = int(tracks.size()), first = first_row(selection, count);
    zone_header(im, 0, "Sound test", std::string(game) + ", " + std::to_string(count) + " tracks");
    text(im, "Select a track", 34, 112, 2, t.accent, Ink::outline, navy);
    if (first > 0) {
        text_right(im, std::string(1, icon_up), 446, 112, 2, t.accent, Ink::outline, navy);
    }
    if (first + visible_rows < count) {
        text_right(im, std::string(1, icon_down), 446, 583, 2, t.accent, Ink::outline, navy);
    }
    const bool active_player = player.mode() != gg::MusicPlayer::Mode::Stopped;
    for (int n = first; n < std::min(count, first + visible_rows); ++n) {
        const int y = 138 + (n - first) * 26;
        const bool chosen = n == selection, lit = chosen && focus == 0;
        rect(im, 32, y, 416, 24, lit ? t.accent : chosen ? mix(t.card, t.accent, 90) : t.card);
        const Color c = lit ? t.ink : t.card_ink;
        auto number = std::to_string(n + 1);
        if (number.size() == 1) {
            number = "0" + number;
        }
        text(im, number, 42, y + 5, 2, lit ? t.ink : t.muted);
        text(im, tracks[std::size_t(n)].title, 78, y + 5, 2, c);
        if (unsigned(n) == player.track() && active_player) {
            const int lift =
                player.playing() && !reduce_motion ? int(std::lround(std::sin(time * 10) * 2)) : 0;
            text(im, std::string(1, icon_note), 422, y + 4 - lift, 2, lit ? t.ink : t.bg);
        }
    }
    const auto& track = tracks[player.track()];
    panel(im, t, 470, 108, 464, 106, t.card, t.accent);
    text(im, "Now selected", 488, 120, 2, t.muted);
    text(im, track.title, 488, 142, 3, t.card_ink);
    wrapped(im, track.description, 488, 176, 430, 2, 2, t.muted);
    constexpr Color screen{9, 22, 38}, grid{25, 46, 65};
    const Color wave = t.accent;
    panel(im, t, 476, 226, 452, 204, screen, mix(t.accent, black, 120));
    for (int x = 492; x < 920; x += 20) {
        rect(im, x, 238, 1, 180, grid);
    }
    for (int y = 246; y < 420; y += 20) {
        rect(im, 488, y, 428, 1, grid);
    }
    auto signal = player.waveform();
    for (int x = 0; x < 100; ++x) {
        int top = 330 - int(std::clamp(signal[std::size_t(x)][1] * 300.0f, -78.0f, 78.0f));
        int bottom = 330 - int(std::clamp(signal[std::size_t(x)][0] * 300.0f, -78.0f, 78.0f));
        top = (top / 3) * 3;
        bottom = (bottom / 3) * 3;
        rect(im, 502 + x * 4, top, 3, std::max(3, bottom - top + 3), wave);
    }
    const char* state = player.mode() == gg::MusicPlayer::Mode::Playing    ? "Playing"
                        : player.mode() == gg::MusicPlayer::Mode::Paused   ? "Paused"
                        : player.mode() == gg::MusicPlayer::Mode::Finished ? "Finished"
                                                                           : "Stopped";
    text(im, state, 494, 242, 2, wave);
    auto seconds = unsigned(player.elapsed());
    auto sec = std::to_string(seconds % 60);
    if (sec.size() == 1) {
        sec = "0" + sec;
    }
    text_right(im, std::to_string(seconds / 60) + ":" + sec, 912, 404, 2, t.accent2);
    const std::array<std::string, 4> labels{std::string(1, icon_left) + " Prev",
                                            player.playing() ? "Pause"
                                                             : std::string(1, icon_play) + " Play",
                                            "Next " + std::string(1, icon_right), "Stop"};
    for (int n = 0; n < 4; ++n) {
        button(im, t, 476 + n * 116, 460, 104, 46, labels[std::size_t(n)], focus == n + 1);
    }
    text(im, "Original score timing and loops.", 478, 520, 2, white, Ink::outline, navy);
    button(im, t, 476, 548, 284, 38, "Back to library", focus == 5);
    button(im, t, 784, 548, 144, 38, muted ? "Unmute  M" : "Mute  M", focus == 6);
    blend_rect(im, 0, 604, 960, 36, hint_bar, 200);
    int x = 24;
    for (auto [key, action] : {std::pair<std::string_view, std::string_view>{"\x03\x04", "Track"},
                               {"Enter", "Play"},
                               {"Space", "Pause"},
                               {"\x01\x02", "Skip"},
                               {"Tab", "Controls"},
                               {"Esc", "Back"}}) {
        x = key_hint(im, key, action, x, 611);
    }
    return im;
}

} // namespace shell
