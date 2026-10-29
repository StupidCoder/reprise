// The library screen's music player: a game's track list, the track playing
// with a waveform, and play controls. See library_screen.hpp; the music
// itself is played by the app's MusicPlayer (music_player.hpp).
//
// Landscape: the list on the left; on the right the track's title, the
// waveform on a dark "scope", the time and the controls. Portrait: the same
// parts above the list, which scrolls.
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::shell {

using platform::Event;
using platform::Key;
using platform::Rect;
using namespace ui;

namespace {

constexpr float kRowHeight = 30;

std::string twoDigits(int n) {
    return (n < 10 ? "0" : "") + std::to_string(n);
}

} // namespace

void LibraryScreen::showMusic(int card) {
    const int slot = slotOf(card);
    if (!games_[slot].library.ready || games_[slot].musicTracks == 0) {
        return;
    }
    game_ = slot;
    view_ = View::Music;
    musicRow_ = std::clamp(music_.track, 0, games_[slot].musicTracks - 1);
    musicScroll_ = 0;
}

LibraryScreen::Action LibraryScreen::playTrack(int index) const {
    Action action{Request::PlayTrack};
    action.card = cardOf(game_);
    action.slot = index;
    return action;
}

float LibraryScreen::musicListMax() const {
    return std::max(0.0f, music_.titles.size() * kRowHeight - musicList_.h);
}

void LibraryScreen::scrollMusicTo(int row) {
    const float top = row * kRowHeight;
    if (top < musicScroll_) {
        musicScroll_ = top;
    } else if (top + kRowHeight > musicScroll_ + musicList_.h) {
        musicScroll_ = top + kRowHeight - musicList_.h;
    }
    musicScroll_ = std::clamp(musicScroll_, 0.0f, musicListMax());
}

LibraryScreen::Action LibraryScreen::handleMusic(const Event& e) {
    const int count = int(music_.titles.size());
    const int current = music_.track < 0 ? musicRow_ : music_.track;
    auto step = [&](int direction) {
        return count ? playTrack((current + direction + count) % count) : Action{};
    };
    switch (e.type) {
    case Event::Type::MouseDown:
        if (e.button != 1) {
            break;
        }
        if (musicBack_.contains(e.x, e.y)) {
            return {Request::CloseMusic};
        }
        if (transport_[0].contains(e.x, e.y)) {
            return step(-1);
        }
        if (transport_[1].contains(e.x, e.y)) {
            return music_.track < 0 ? playTrack(musicRow_) : Action{Request::PauseMusic};
        }
        if (transport_[2].contains(e.x, e.y)) {
            return step(1);
        }
        if (transport_[3].contains(e.x, e.y)) {
            return {Request::StopMusic};
        }
        if (musicList_.contains(e.x, e.y)) {
            musicPressed_ = true;
            musicPanning_ = false;
            musicPressY_ = e.y;
            musicPressScroll_ = musicScroll_;
        }
        break;
    case Event::Type::MouseMove:
        // A press in the list that moves scrolls it (touch scrolling).
        if (musicPressed_ && e.button == 1 && (musicPanning_ || std::abs(e.y - musicPressY_) > 8)) {
            musicPanning_ = true;
            musicScroll_ =
                std::clamp(musicPressScroll_ - (e.y - musicPressY_), 0.0f, musicListMax());
        }
        break;
    case Event::Type::MouseUp:
        if (e.button == 1 && musicPressed_) {
            musicPressed_ = false;
            if (!musicPanning_) {
                const int row = int((musicPressY_ - musicList_.y + musicScroll_) / kRowHeight);
                if (row >= 0 && row < count) {
                    musicRow_ = row;
                    return playTrack(row);
                }
            }
            musicPanning_ = false;
        }
        break;
    case Event::Type::Wheel:
        musicScroll_ = std::clamp(musicScroll_ - e.wheel * kRowHeight, 0.0f, musicListMax());
        break;
    case Event::Type::KeyDown:
        if (e.command) {
            break;
        }
        switch (e.key) {
        case Key::Up:
            musicRow_ = std::max(0, musicRow_ - 1);
            scrollMusicTo(musicRow_);
            break;
        case Key::Down:
            musicRow_ = std::min(count - 1, musicRow_ + 1);
            scrollMusicTo(musicRow_);
            break;
        case Key::Return:
            return playTrack(musicRow_);
        case Key::Left:
            return step(-1);
        case Key::Right:
            return step(1);
        case Key::Escape:
            return {Request::CloseMusic};
        case Key::Character:
            if (e.symbol == ' ') {
                return music_.track < 0 ? playTrack(musicRow_) : Action{Request::PauseMusic};
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return {};
}

void LibraryScreen::drawMusic(style::Canvas& c) {
    const bool tall = portrait();
    const float m = margin(), contentW = width_ - 2 * m;
    const Card& card = cards_[cardOf(game_)];
    const int count = int(music_.titles.size());
    using State = MusicStatus::State;

    // The bar: back to the hub, "Music" and the game.
    musicBack_ = {m, 24, 150, 36};
    button(c, musicBack_, std::string(style::kIconLeft) + " Back", ButtonKind::Plain, true,
           musicBack_.contains(mouseX_, mouseY_));
    const float barText = 24 + (36 - c.capHeight(1.5f)) / 2;
    c.textRight(card.title, width_ - m, barText, 1.5f, kMuted);
    if (tall) {
        c.text("Music", m, 80, 3, kInk);
    } else {
        c.textCentred("Music", width_ / 2, 24 + (36 - c.capHeight(3)) / 2, 3, kInk);
        c.rect(m, 76, contentW, 1, kGold);
    }

    // Where the parts go.
    const float top = tall ? 120 : 96;
    const float listW = tall ? contentW : 430;
    const float panelX = tall ? m : m + listW + 24,
                panelW = tall ? contentW : contentW - listW - 24;
    const Rect info{panelX, top, panelW, tall ? 96.0f : 118.0f};
    const Rect scope{panelX, info.y + info.h + 12, panelW, tall ? 140.0f : 236.0f};
    const float buttonsY = scope.y + scope.h + 16, buttonH = tall ? 44.0f : 48.0f;
    musicList_ =
        tall ? Rect{m, buttonsY + buttonH + 20, contentW, height_ - 24 - (buttonsY + buttonH + 20)}
             : Rect{m, top, listW, height_ - 48 - top};
    musicList_.h = std::max(kRowHeight, musicList_.h);
    musicScroll_ = std::clamp(musicScroll_, 0.0f, musicListMax());

    // The track list; rows that scroll out are covered below.
    c.rounded(musicList_.x, musicList_.y, musicList_.w, musicList_.h, 6, kCard);
    const int first = std::max(0, int(musicScroll_ / kRowHeight));
    for (int i = first; i < count && i * kRowHeight < musicScroll_ + musicList_.h; ++i) {
        const float y = musicList_.y + i * kRowHeight - musicScroll_;
        const bool playing = i == music_.track;
        const bool hover =
            Rect{musicList_.x, y, musicList_.w, kRowHeight}.contains(mouseX_, mouseY_) &&
            musicList_.contains(mouseX_, mouseY_) && !musicPanning_;
        Color background = i % 2 ? kRowAlternate : kCard;
        if (playing) {
            background = kGold;
        } else if (hover || i == musicRow_) {
            background = style::mix(background, kGold, i == musicRow_ ? 60 : 35);
        }
        c.rect(musicList_.x, y, musicList_.w, kRowHeight, background);
        const float ty = y + (kRowHeight - c.capHeight(2)) / 2;
        c.text(twoDigits(i + 1), musicList_.x + 12, ty, 2, playing ? kWhite : kMuted);
        const float titleX = musicList_.x + 52;
        c.text(c.ellipsize(music_.titles[std::size_t(i)], musicList_.w - 52 - 40, 2), titleX, ty, 2,
               playing ? kWhite : kInk);
        if (playing && music_.state != State::Stopped) {
            // A note that bobs while the track plays.
            const float lift = music_.state == State::Playing
                                   ? float(std::round(std::sin(music_.elapsed * 8) * 2))
                                   : 0;
            c.textRight(style::kIconMusic, musicList_.x + musicList_.w - 12, ty - lift, 2, kWhite);
        }
    }
    c.rect(0, musicList_.y - kRowHeight, width_, kRowHeight, kPaper);
    c.rect(0, musicList_.y + musicList_.h, width_, height_, kPaper);
    const Color edge = style::mix(kLine, kInk, 60);
    c.rect(musicList_.x, musicList_.y, musicList_.w, 1, edge);
    c.rect(musicList_.x, musicList_.y + musicList_.h, musicList_.w, 1, edge);
    // A thin bar shows where a long list is scrolled to.
    if (musicListMax() > 0) {
        const float total = count * kRowHeight;
        const float barH = std::max(24.0f, musicList_.h * musicList_.h / total);
        const float barY = musicList_.y + (musicList_.h - barH) * musicScroll_ / musicListMax();
        c.rounded(musicList_.x + musicList_.w - 5, barY + 2, 3, barH - 4, 1,
                  style::mix(kLine, kInk, 90));
    }

    // The track: number, title and state.
    c.frame(info.x, info.y, info.w, info.h, 8, 2, kGold, kCard);
    const int shown = music_.track >= 0 ? music_.track : musicRow_;
    const char* state = music_.state == State::Playing    ? "Playing"
                        : music_.state == State::Starting ? "Starting"
                        : music_.state == State::Paused   ? "Paused"
                                                          : "Stopped";
    c.text("Track " + twoDigits(shown + 1) + " of " + std::to_string(count), info.x + 18,
           info.y + 16, 1.5f, kMuted);
    c.textRight(state, info.x + info.w - 18, info.y + 16, 1.5f, kGold);
    if (shown >= 0 && shown < count) {
        c.text(c.ellipsize(music_.titles[std::size_t(shown)], info.w - 36, 3), info.x + 18,
               info.y + (tall ? 44 : 50), 3, kInk);
    }

    drawScope(c, scope);

    // Controls: previous, play or pause, next, stop.
    const bool playing = music_.state == State::Playing || music_.state == State::Starting;
    const std::string labels[4] = {std::string(style::kIconLeft) + " Prev",
                                   playing ? "Pause" : "Play",
                                   "Next " + std::string(style::kIconRight), "Stop"};
    const float gap = 12, w = (panelW - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i) {
        transport_[i] = {panelX + i * (w + gap), buttonsY, w, buttonH};
        const bool enabled = i != 3 || music_.state != State::Stopped;
        button(c, transport_[i], labels[i], i == 1 ? ButtonKind::Primary : ButtonKind::Plain,
               enabled, enabled && transport_[i].contains(mouseX_, mouseY_));
    }
    if (!tall) {
        std::string keys(style::kIconUp);
        keys.append(style::kIconDown).append(" choose  Return play  Space pause  ");
        keys.append(style::kIconLeft).append(style::kIconRight).append(" skip  Esc back");
        c.text(keys, panelX, height_ - 48 - c.capHeight(1.5f), 1.5f, kMuted);
    }
}

// The waveform on a dark screen with a faint grid, drawn as blocky bars
// (each column spans the lowest to the highest sample of its slice), with
// the state and the time played.
void LibraryScreen::drawScope(style::Canvas& c, const Rect& r) {
    const Color screen = style::mix(kToolbar, Color{0, 0, 0}, 60);
    const Color grid = style::mix(screen, kGold, 40);
    c.rounded(r.x, r.y, r.w, r.h, 8, screen);
    const float inner = 14, x0 = r.x + inner, y0 = r.y + inner;
    const float w = r.w - 2 * inner, h = r.h - 2 * inner - 20;
    for (float x = x0; x <= x0 + w; x += 24) {
        c.rect(x, y0, 1, h, grid);
    }
    for (float y = y0; y <= y0 + h; y += 24) {
        c.rect(x0, y, w, 1, grid);
    }
    const float mid = y0 + h / 2;
    c.rect(x0, mid, w, 1, style::mix(screen, kGold, 80));

    // Columns of 4 units with 2 between; heights snap to 3-unit steps.
    const int columns = int(w / 6);
    const auto& wave = music_.wave;
    const bool live = music_.state == MusicStatus::State::Playing && !wave.empty();
    for (int i = 0; i < columns; ++i) {
        float low = 0, high = 0;
        if (live) {
            const std::size_t a = wave.size() * i / columns, b = wave.size() * (i + 1) / columns;
            for (std::size_t k = a; k < std::max(b, a + 1) && k < wave.size(); ++k) {
                low = std::min(low, wave[k]);
                high = std::max(high, wave[k]);
            }
        }
        const float gain = h * 1.4f;
        const float top = std::max(y0, mid - std::round(high * gain / 3) * 3);
        const float bottom = std::min(y0 + h, mid - std::round(low * gain / 3) * 3);
        c.rect(x0 + i * 6, top - 1.5f, 4, std::max(3.0f, bottom - top + 3), kGold);
    }

    // The time played, mm:ss.
    const int seconds = int(music_.elapsed);
    c.textRight(std::to_string(seconds / 60) + ":" + twoDigits(seconds % 60), r.x + r.w - inner,
                r.y + r.h - inner - c.capHeight(2), 2, kGold);
}

} // namespace picarat::shell
