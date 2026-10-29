// The puzzle index (overlay 19). See a2_puzzle_index.hpp.
#include "menus/a2_puzzle_index.hpp"
#include "menus/a2_file_dialog.hpp"
#include <cstdlib>

namespace picarat {
namespace {
// ov2 puzzle-byte accessors by number (02083d28.. 02083e24): index n-1,
// numbers above 153 read 0. Bits: 0-1 status, 2-3 failures, 4-5 hints,
// 6 passed to the shack, 7 favourite.
unsigned puzzleBits(const GameState& s, int number, int shift, unsigned mask) {
    if (number < 1 || number > 153) {
        return 0;
    }
    return (s.progress.puzzleByte(std::uint32_t(number - 1)) >> shift) & mask;
}
} // namespace

PuzzleIndex::PuzzleIndex(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                         GameState& state)
    : kit_(assets, std::move(present), audio, SceneKind::PuzzleIndex), state_(state) {
    // 020efda0 (scenes 19 and 31): display reset (0206dabc, 0206d9c0, 0206e144(1),
    // 0206ddec); the WiFi-return branch (following 22) is not reconstructed.
    kit_.resetDisplay();
    scene_ = state_.scene; // +0x564
    digits_ = kit_.load("menu/jiten/?/jiten_num.spr", 0, 0, 1);
    auto fixed = [&](SpriteButton& b, int w, int h) {
        b.button.bounds.width = w;
        b.button.bounds.height = h;
    }; // +0x34: size kept
    start_ = kit_.button("menu/jiten/?/jiten_btn.spr", 179, 3, -1);
    kit_.setButtonAnimations(start_, "pazzle_on", "pazzle_off", "pazzle_off");
    fixed(start_, 56, 50);
    up_ = kit_.button("menu/jiten/?/jiten_btn.spr", 107, 30, -1);
    kit_.setButtonAnimations(up_, "up_on", "up_off", "up_on");
    fixed(up_, 30, 24);
    upFast_ = kit_.button("menu/jiten/?/jiten_btn.spr", 139, 30, -1);
    kit_.setButtonAnimations(upFast_, "upfast_on", "upfast_off", "upfast_on");
    fixed(upFast_, 30, 24);
    down_ = kit_.button("menu/jiten/?/jiten_btn.spr", 107, 154, -1);
    kit_.setButtonAnimations(down_, "down_on", "down_off", "down_on");
    fixed(down_, 30, 24);
    downFast_ = kit_.button("menu/jiten/?/jiten_btn.spr", 139, 154, -1);
    kit_.setButtonAnimations(downFast_, "downfast_on", "downfast_off", "downfast_on");
    fixed(downFast_, 30, 24);
    // Spriteless rows (0208003c without a path): 180x17 at (38, 56 + 20 i), sound 3.
    for (int i = 0; i < 5; ++i) {
        auto& r = rows_[std::size_t(i)];
        r.exists = true;
        r.x = 38;
        r.y = 56 + 20 * i;
        r.button = Button({38, 56 + 20 * i, 180, 17}, 3);
    }
    // Scroll bar 020814e0(x 227, pos 55, top 55, bottom 153, index 0), count 0.
    bar_.x = 227;
    bar_.pos = 55;
    bar_.top = 55;
    bar_.bottom = 153;
    bar_.index = 0;
    bar_.sprite = kit_.load("menu/jiten/jiten_scroll.spr", 0, 0, 1);
    kit_.select(bar_.sprite, 1u);
    bar_.sprite.x = bar_.x;
    bar_.sprite.y = bar_.pos;
    barCount(0);
    barIndex(0);
    kit_.lowerBackground = "menu/bag/jiten_bg";
    kit_.upperBackground = kit_.localized("menu/bag/?/jiten_sub");
    title_ = kit_.load("menu/jiten/?/jiten_title.spr", 14, 8, 1);
    tag_ = kit_.load("menu/jiten/?/tag.spr", 0, 0, 1);
    if (state_.indexMode == 0) {
        kit_.select(tag_, "all");
    } else if (state_.indexMode == 1) {
        kit_.select(tag_, "favorite");
    }
    favIcon_ = kit_.load("menu/jiten/?/jiten_btn.spr", 0, 0, 1);
    guard_ = kit_.load("menu/jiten/jiten_guard.spr", 0, 0, 1);
    prize_ = kit_.load("menu/jiten/jiten_prize.sbj", 0, 0, 1);
    prize_.x = 13;
    prize_.y = 6;
    numbers_ = kit_.load("menu/jiten/?/jiten_num.sbj", 0, 0, 1);
    hintBox_ = kit_.load("menu/jiten/jiten_hintbox.sbj", 0, 0, 1);
    allTab_.exists = true;
    allTab_.x = 14;
    allTab_.y = 38;
    allTab_.button = Button({14, 38, 32, 16}, -1);
    favTab_.exists = true;
    favTab_.x = 47;
    favTab_.y = 38;
    favTab_.button = Button({47, 38, 48, 16}, -1);
    for (int i = 0; i < 5; ++i) {
        auto& f = favorites_[std::size_t(i)];
        f.exists = true;
        f.x = 19;
        f.y = 58 + 20 * i;
        f.button = Button({19, 58 + 20 * i, 16, 12}, 3);
    }
    close_ = kit_.button("menu/jiten/?/jiten_btn.spr", 187, 163, 121);
    kit_.setButtonAnimations(close_, "close_on", "close_off", "close_click");
    fixed(close_, 52, 20);
    question_ = kit_.load("menu/jiten/jiten_q1.sbj", 80, 48, 1);
    state_.indexSelected = state_.progress.bytes[0x319]; // +0x8204 = +0x3e1
    initLists();
    // Room sound 31 with music (0207f780), then wait for the BGM (0207fa78);
    // from the bonus menu (scene 31, 020f0800) room sound 19 and the
    // following scene 28.
    if (scene_ == 31) {
        state_.following = 28;
    }
    if (auto s = kit_.sound()) {
        s->roomSound(scene_ == 31 ? 19 : 31, true);
        while (!s->bgmSettled()) {
            kit_.clearSubmissions();
            kit_.wait(1);
        }
    }
}

// ---------------------------------------------------------------------------
// Scroll bar (ov2 0208159c..02081b28).

void PuzzleIndex::barRecompute() {
    // 0208159c: thumb = f2i((bottom - top) * (5 / n)), n = range + 5 (at least
    // 1), no smaller than the sprite and no larger than the track.
    const int n = bar_.range + 5 > 0 ? bar_.range + 5 : 1;
    const int track = bar_.bottom - bar_.top;
    bar_.thumb = int(float(track) * (5.0f / float(n)));
    kit_.select(bar_.sprite, 1u);
    if (bar_.thumb < kit_.frameHeight(bar_.sprite)) {
        bar_.thumb = kit_.frameHeight(bar_.sprite);
    }
    if (bar_.thumb > track) {
        bar_.thumb = track;
    }
    bar_.maxPos = bar_.bottom - bar_.thumb;
}

void PuzzleIndex::barCount(int count) {
    bar_.range = count - 5;
    barRecompute();
} // 02081878

void PuzzleIndex::barIndex(int index) {
    // 02081b28: pos = top + f2i((maxPos - top) * (index / range)). With no
    // range (five entries or fewer) the ROM divides by zero; the bar is not
    // shown then, and the native thumb stays at the top.
    barRecompute();
    bar_.index = index;
    bar_.pos =
        bar_.top + (bar_.range > 0
                        ? int(float(bar_.maxPos - bar_.top) * (float(index) / float(bar_.range)))
                        : 0);
}

void PuzzleIndex::barUpdate() {
    // 0208188c: a press on the thumb (18 px wide) starts dragging; a release
    // stops it (and sequenced effect 126); while held the thumb follows the
    // touch while it stays within 5 px of the last sample, and the index is
    // f2i(range * (pos - top) / (maxPos - top) + 0.5).
    bar_.changed = false;
    if (kit_.pressed()) {
        int tx = 0, ty = 0;
        kit_.position(tx, ty);
        if (Rect{bar_.x, bar_.pos, 18, bar_.thumb}.contains(tx, ty)) {
            bar_.dragging = true;
            bar_.lastY = ty;
        }
    }
    if (kit_.released() && bar_.dragging) {
        if (auto s = kit_.sound()) {
            s->stopEffect(126, 0.0f);
        }
        bar_.dragging = false;
    }
    if (kit_.held() && bar_.dragging) {
        int tx = bar_.x, ty = bar_.pos;
        kit_.position(tx, ty);
        // 0203a54c / bls: the thumb follows only touches within 5 px of the
        // previous sample (measured: it tracks a 1 px/frame drag exactly,
        // ref-index2); larger jumps are ignored.
        if (std::abs(ty - bar_.lastY) <= 5) {
            bar_.pos += ty - bar_.lastY;
            bar_.lastY = ty;
        }
        if (bar_.pos > bar_.maxPos) {
            bar_.pos = bar_.maxPos;
        }
        if (bar_.pos < bar_.top) {
            bar_.pos = bar_.top;
        }
        bar_.index =
            int(float(bar_.range) * (float(bar_.pos - bar_.top) / float(bar_.maxPos - bar_.top)) +
                0.5f);
        bar_.changed = true;
    }
    if (bar_.range + 5 <= 5) {
        return;
    }
    // 0208165c: the thumb from opaque 3D rectangles and the top/bottom caps.
    const int x = bar_.x, y = bar_.pos, h = bar_.thumb;
    kit_.select(bar_.sprite, 1u);
    const int w = kit_.frameWidth(bar_.sprite);
    kit_.rectangle(x + 2, y + 3, w - 4, h - 6,
                   bar_.dragging ? rgb(194, 84, 18) : rgb(156, 147, 121), 31);
    kit_.rectangle(x, y + 3, 2, h - 6, rgb(49, 28, 16), 31);
    kit_.rectangle(x + w - 2, y + 3, 2, h - 6, rgb(49, 28, 16), 31);
    kit_.rectangle(x + 2, y + 3, 1, h - 6, rgb(254, 251, 227), 31);
    kit_.rectangle(x + w - 3, y + 3, 1, h - 6, rgb(254, 251, 227), 31);
    kit_.select(bar_.sprite, "top");
    bar_.sprite.x = x;
    bar_.sprite.y = y;
    kit_.draw(bar_.sprite);
    kit_.select(bar_.sprite, "bottom");
    bar_.sprite.x = x;
    bar_.sprite.y = y + h - 3;
    kit_.draw(bar_.sprite);
    kit_.select(bar_.sprite, 1u);
}

// ---------------------------------------------------------------------------
// Lists.

void PuzzleIndex::initLists() {
    // 020f2130.
    top_ = 0;
    selected_ = state_.indexSelected;
    favTop_ = 0;
    favSelected_ = state_.indexFavorite;
    mode_ = state_.indexMode;
    buildAll();
    buildFavorites();
    selectChanged();
    if (mode_ == 0) {
        barCount(count_);
        barIndex(top_);
    } else if (mode_ == 1) {
        barCount(favCount_);
        barIndex(favTop_);
    }
}

// The "all" tab: every puzzle met so far, by number.
void PuzzleIndex::buildAll() {
    // 020f2208: numbers 1..153 with a nonzero status; the selected one's
    // row becomes the top (at most count - 5); without a valid selection
    // the first entry is selected.
    int k = 0;
    for (int n = 1; n <= 153; ++n) {
        if (state_.puzzleStatus(n) == 0) {
            continue;
        }
        all_[std::size_t(k)] = n;
        if (selected_ == n) {
            top_ = k;
        }
        ++k;
    }
    count_ = k;
    if (k <= 5) {
        top_ = 0;
    } else if (top_ > k - 5) {
        top_ = k - 5;
    }
    for (; k < 153; ++k) {
        all_[std::size_t(k)] = 0;
    }
    if (selected_ == 0 || selected_ > 153) {
        selected_ = all_[0];
    }
}

// The favourites tab.
void PuzzleIndex::buildFavorites() {
    // 020f22bc: the favourites among the listed numbers.
    int j = 0;
    bool found = false;
    for (int k = 0; k < count_; ++k) {
        const int n = all_[std::size_t(k)];
        if (!puzzleBits(state_, n, 7, 1)) {
            continue;
        }
        favoriteList_[std::size_t(j)] = n;
        if (favSelected_ == n) {
            favTop_ = j;
            found = true;
        }
        ++j;
    }
    favCount_ = j;
    if (j <= 5) {
        favTop_ = 0;
    } else if (favTop_ > j - 5) {
        favTop_ = j - 5;
    }
    for (; j < 153; ++j) {
        favoriteList_[std::size_t(j)] = 0;
    }
    if (!found) {
        favTop_ = 0;
        favSelected_ = favoriteList_[0];
    }
}

// Whether any puzzle is marked as a favourite.
bool PuzzleIndex::anyFavorite() const {
    for (int n = 1; n <= 153; ++n) {
        if (puzzleBits(state_, n, 7, 1)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Drawing.

void PuzzleIndex::numberDigits(SpriteObject& s, int x, int y, int value, int digits) {
    // 020f24d8 / 020f3258: `digits` digits right to left from x by 6, each
    // the animation named "%i".
    for (int i = 0; i < digits; ++i) {
        kit_.select(s, std::to_string(value % 10));
        s.x = x;
        s.y = y;
        kit_.draw(s);
        x -= 6;
        value /= 10;
    }
}

// The favourite star of a row.
void PuzzleIndex::favoriteIcon(int x, int y, bool on) {
    kit_.select(favIcon_, on ? "fav_on" : "fav_off");
    favIcon_.x = x;
    favIcon_.y = y;
    kit_.draw(favIcon_);
}

// The icons of a row (solved, favourite).
void PuzzleIndex::entryIcons(int y, int number) {
    // 020f2588: "nazo" at (53, y + 1), three digits from x 83, then the
    // status mark at (38, y): o solved, x seen, New.
    kit_.select(digits_, "nazo");
    digits_.x = 53;
    digits_.y = y + 1;
    kit_.draw(digits_);
    numberDigits(digits_, 83, y + 1, number, 3);
    digits_.x = 38;
    digits_.y = y;
    const auto status = state_.puzzleStatus(number);
    if (status == 2) {
        kit_.select(digits_, "o");
        kit_.draw(digits_);
    } else if (status == 1) {
        kit_.select(digits_, "x");
        kit_.draw(digits_);
    } else if (status == 0) {
        kit_.select(digits_, "New");
        kit_.draw(digits_);
    }
}

// The highlight of the selected row.
void PuzzleIndex::highlight(int y) {
    // 020f27b0: a translucent 208x16 bar (rgb 0,0,64, alpha 5) behind the
    // row, clipped to the list area 56..152.
    int top = y - 2, h = 16;
    const int bottom = top + 16;
    if (bottom < 56 || top >= 152) {
        return;
    }
    if (top < 56) {
        h -= 56 - top;
        top = 56;
    }
    if (bottom >= 152) {
        h = 152 - top;
    }
    kit_.rectangle(16, top, 208, h, rgb(0, 0, 64), 5);
}

// Draws the five visible rows.
void PuzzleIndex::drawList(int offset, int textOffset) {
    // 020f2834: rows -1..5 from y 38 - offset (row -1 only while scrolling
    // up, row 5 only while scrolling down); titles (fontq, x 101) only when
    // the text needs redrawing, partial rows' text cleared at the edges.
    const int count = mode_ == 0 ? count_ : favCount_,
              selected = mode_ == 0 ? selected_ : favSelected_, top = mode_ == 0 ? top_ : favTop_;
    const auto& list = mode_ == 0 ? all_ : favoriteList_;
    int y = 38 - offset;
    if (count <= 0) {
        return;
    }
    for (int r = -1; r <= 5; ++r) {
        if (r == -1 && offset >= 0) {
            y += 20;
            continue;
        }
        if (r == 5 && offset <= 0) {
            break;
        }
        const int k = top + r;
        const int n = k >= 0 && k < 153 ? list[std::size_t(k)] : 0;
        if (n == 0) {
            return;
        }
        if (selected == n) {
            highlight(y);
        }
        const auto record = state_.puzzleByNumber(n);
        entryIcons(y, record ? record->number : 0);
        if (mode_ == 0) {
            favoriteIcon(21, y, puzzleBits(state_, n, 7, 1));
        } else {
            favoriteIcon(21, y, true);
        }
        if (redraw_) {
            if (record) {
                kit_.textQuestion(101, y + textOffset, kit_.text.expand(record->title),
                                  SceneKit::Lower);
            }
            if (r == -1 || (r == 0 && offset > 0)) {
                kit_.clearTextRect(91, 38, 136, 20);
            }
            if (r == 5 || (r == 4 && offset < 0)) {
                kit_.clearTextRect(91, 151, 136, 20);
            }
        }
        y += 20;
    }
    redraw_ = false;
}

// Scrolls the list (animated).
void PuzzleIndex::scrollUp(int step) {
    // 020f2b70: one row up in frames of `step` pixels; a fresh press plays SE 125.
    int& top = mode_ == 0 ? top_ : favTop_;
    if (top <= 0) {
        return;
    }
    if (kit_.pressed()) {
        kit_.effect(125);
    }
    for (int o = step; o <= 20; o += step) {
        redraw_ = true;
        kit_.poll();
        clearList();
        drawList(-o, -step);
        draw();
        kit_.present();
    }
    redraw_ = true;
    clearList();
    --top;
    barIndex(top);
}

void PuzzleIndex::scrollDown(int step) {
    // 020f2cc8.
    int& top = mode_ == 0 ? top_ : favTop_;
    const int count = mode_ == 0 ? count_ : favCount_;
    if (top + 6 > count) {
        return;
    }
    if (kit_.pressed()) {
        kit_.effect(125);
    }
    for (int o = step; o <= 20; o += step) {
        redraw_ = true;
        kit_.poll();
        clearList();
        drawList(o, step);
        draw();
        kit_.present();
    }
    clearList();
    redraw_ = true;
    ++top;
    barIndex(top);
}

// Switches between the "all" and favourites tabs.
void PuzzleIndex::switchMode(int mode) {
    // 020f2e2c.
    mode_ = mode;
    if (mode == 0) {
        kit_.select(tag_, "all");
        buildAll();
        barCount(count_);
        barIndex(top_);
        selectChanged();
    } else if (mode == 1) {
        kit_.select(tag_, "favorite");
        buildFavorites();
        barCount(favCount_);
        barIndex(favTop_);
        if (anyFavorite()) {
            selectChanged();
        }
    }
    redraw_ = true;
    kit_.effect(120);
    clearList();
}

// Updates the upper screen after the selection changed.
void PuzzleIndex::selectChanged() {
    // 020f2efc: the puzzle's data (ov27 object 020f5924 on n%i.dat), its
    // type (type_%u, 35 shown as 22) and place (p_%u, or p_63 once passed to
    // the shack) from jiten.plz, one frame, the upper texts, then the
    // picture jiten_q%i (by internal id) at (80, 48).
    const int n = mode_ == 0 ? selected_ : (mode_ == 1 ? favSelected_ : 0);
    if (n == 0) {
        return;
    }
    const auto record = state_.puzzleByNumber(n);
    if (!record) {
        return;
    }
    data_.emplace(
        readFile(kit_.assets.root / "puzzle" / ("n" + std::to_string(record->internal) + ".dat")),
        kit_.text);
    const unsigned type = data_->type == 35 ? 22 : data_->type;
    type_ = kit_.text.expand(kit_.member("jiten", "type_" + std::to_string(type) + ".txt"));
    place_ = kit_.text.expand(kit_.member(
        "jiten", puzzleBits(state_, n, 6, 1) ? std::string("p_63.txt")
                                             : "p_" + std::to_string(data_->place) + ".txt"));
    kit_.wait(1);
    upperText();
    question_ =
        kit_.load("menu/jiten/jiten_q" + std::to_string(record->internal) + ".sbj", 80, 48, 1);
}

// Upper screen: the puzzle's prize icon.
void PuzzleIndex::prizeIcon(int number) {
    // 020f319c: gold (solved, no hints, no failures), silver (solved, no
    // failures), bronze (solved), normal.
    unsigned a = 4;
    if (state_.puzzleStatus(number) == 2 && !puzzleBits(state_, number, 4, 3) &&
        !puzzleBits(state_, number, 2, 3)) {
        a = 1;
    } else if (state_.puzzleStatus(number) == 2 && !puzzleBits(state_, number, 2, 3)) {
        a = 2;
    } else if (state_.puzzleStatus(number) == 2) {
        a = 3;
    }
    kit_.select(prize_, a);
    kit_.draw(prize_);
}

// Upper screen: how many hints were used.
void PuzzleIndex::hintBoxes(int number) {
    // 020f3304: hint box i (animation i) at (131 + 17 (i - 1), 126) per used hint.
    const int used = int(puzzleBits(state_, number, 4, 3));
    for (int i = 1; i <= used; ++i) {
        kit_.select(hintBox_, unsigned(i));
        hintBox_.x = 131 + 17 * (i - 1);
        hintBox_.y = 126;
        kit_.draw(hintBox_);
    }
}

// Upper screen: picarats won.
void PuzzleIndex::picarats(int number) {
    // 020f3368: the full value (+0x35) at x 225, the current one by failures
    // (0 +0x35, 1 +0x36, 2 or more +0x37) at x 205, two digits each.
    if (!data_) {
        return;
    }
    numberDigits(numbers_, 225, 22, int(data_->picarats[0]), 2);
    const int f = int(puzzleBits(state_, number, 2, 3));
    numberDigits(numbers_, 205, 22,
                 int(f <= 0   ? data_->picarats[0]
                     : f >= 2 ? data_->picarats[2]
                              : data_->picarats[std::size_t(f)]),
                 2);
}

// Upper screen: the puzzle's type and where it was found.
void PuzzleIndex::typeAndPlace() {
    // 020f33f4: centred on 158 at y 150 and 168 (event font).
    if (!data_) {
        return;
    }
    kit_.textTitle(150, type_, 0, 158, SceneKit::Upper);
    kit_.textTitle(168, place_, 0, 158, SceneKit::Upper);
}

// Upper screen: the selected puzzle's picture and details.
void PuzzleIndex::upperInfo() {
    // 020f346c: prize, number (three digits at (60, 15)), picture, hints,
    // picarats of the selection (favourites fall back to the list's).
    int n = 0;
    if (mode_ == 0) {
        n = selected_;
        if (n == 0) {
            return;
        }
    } else if (mode_ == 1) {
        n = favSelected_;
        if (n == 0) {
            n = selected_;
        }
    }
    prizeIcon(n);
    numberDigits(numbers_, 60, 15, n, 3);
    if (question_.loaded()) {
        kit_.draw(question_);
    }
    hintBoxes(n);
    picarats(n);
}

// Upper screen: the selected puzzle's texts.
void PuzzleIndex::upperText() {
    // 020f34f4: clear (65,15,146,20) and (80,150,160,60), title (fontq)
    // centred on 128 at y 15, then type and place.
    int n = 0;
    if (mode_ == 0) {
        n = selected_;
    } else if (mode_ == 1) {
        n = favSelected_;
    }
    if (n == 0) {
        return;
    }
    kit_.clearTextRect(65, 15, 146, 20, SceneKit::Upper);
    kit_.clearTextRect(80, 150, 160, 60, SceneKit::Upper);
    if (const auto record = state_.puzzleByNumber(n)) {
        kit_.textTitle(15, kit_.text.expand(record->title), 0, 128, SceneKit::Upper, true);
    }
    typeAndPlace();
}

// Draws the index.
void PuzzleIndex::draw() {
    // 020f12f4 (scene 19): tag, title, guard; the other tab; close; the
    // favourite marks of the visible rows; start (solved selection only)
    // and the scroll bar (more than five entries); the upper info; the
    // scroll buttons; the rows.
    auto live = [&](SpriteButton& b) {
        kit_.updateButton(b);
        kit_.drawButton(b);
    };
    kit_.draw(tag_);
    kit_.draw(title_);
    kit_.draw(guard_);
    if (mode_ == 1) {
        live(allTab_);
    }
    if (mode_ == 0) {
        live(favTab_);
    }
    live(close_);
    const int count = mode_ == 0 ? count_ : favCount_;
    for (int i = 0; i < 5 && i < count; ++i) {
        live(favorites_[std::size_t(i)]);
    }
    const int sel = mode_ == 0 ? selected_ : favSelected_;
    if (sel && state_.puzzleStatus(sel) == 2) {
        live(start_);
    }
    if (count > 5) {
        barUpdate();
    }
    upperInfo();
    live(up_);
    live(upFast_);
    live(down_);
    live(downFast_);
    for (int i = 0; i < 5 && i < count; ++i) {
        live(rows_[std::size_t(i)]);
    }
}

// Replays a solved puzzle (returns to the index afterwards).
bool PuzzleIndex::startPuzzle(int number, int mode) {
    // Start: SE 111, fade out, the selection saved (+0x8204, +0x3e1,
    // +0x8208, tab +0x820c, +0x8214 = 0), following = this scene, the
    // puzzle loaded by number (02083cb0), scene 10.
    if (number == 0) {
        return false;
    }
    kit_.effect(111);
    kit_.fadeOut();
    state_.indexSelected = selected_;
    state_.progress.bytes[0x319] = std::uint8_t(selected_);
    state_.indexFavorite = favSelected_;
    state_.indexMode = mode;
    state_.indexReplay = 0;
    state_.following = std::uint8_t(scene_);
    if (const auto record = state_.puzzleByNumber(number)) {
        state_.loadPuzzle(record->internal);
    }
    state_.scene = scene::Puzzle;
    return true;
}

// Runs the index until the player leaves or replays a puzzle.
void PuzzleIndex::run() {
    int previous = bar_.pos;
    bool dragSound = false;
    kit_.poll();
    drawList(0, 0);
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        drawList(0, 0);
        draw();
        kit_.present();
        auto resetStart = [&] {
            start_.button.state = Button::State::Idle;
        }; // 02080204(start, 0)
        if (mode_ == 0) {
            if (kit_.clicked(favTab_)) {
                resetStart();
                switchMode(1);
            }
            for (int i = 0; i < 5; ++i) {
                if (!kit_.clicked(favorites_[std::size_t(i)])) {
                    continue;
                }
                resetStart();
                // Toggle the favourite bit (02083e24); the icon drawn here is
                // submitted after the frame (see docs).
                const int n = all_[std::size_t(top_ + i)];
                const bool on = puzzleBits(state_, n, 7, 1);
                if (n >= 1 && n <= 153) {
                    auto& b = state_.progress.puzzleByte(std::uint32_t(n - 1));
                    b = std::uint8_t(on ? (b & 0x7f) : (b | 0x80));
                }
                favoriteIcon(21, 58 + 20 * i, !on);
            }
            if (kit_.clicked(start_)) {
                if (startPuzzle(selected_, 0)) {
                    return;
                }
                continue;
            }
        }
        if (mode_ == 1) {
            if (kit_.clicked(allTab_)) {
                resetStart();
                switchMode(0);
            }
            for (int i = 0; i < 5; ++i) {
                if (!kit_.clicked(rows_[std::size_t(i)]) || favCount_ <= favTop_ + i) {
                    continue;
                }
                resetStart();
                highlight(58 + 20 * i);
                const int n = favoriteList_[std::size_t(favTop_ + i)];
                if (favSelected_ != n) {
                    favSelected_ = n;
                    selectChanged();
                }
            }
            for (int i = 0; i < 5; ++i) {
                if (!kit_.clicked(favorites_[std::size_t(i)]) || favCount_ <= favTop_ + i) {
                    continue;
                }
                resetStart();
                highlight(58 + 20 * i);
                favSelected_ = favoriteList_[std::size_t(favTop_ + i)];
                selectChanged();
            }
            if (kit_.clicked(start_)) {
                if (startPuzzle(favSelected_, 1)) {
                    return;
                }
                continue;
            }
            if (bar_.changed) {
                favTop_ = bar_.index;
                clearList();
                redraw_ = true;
            }
        }
        if (kit_.clicked(close_)) {
            kit_.fadeOut();
            state_.progress.bytes[0x319] = std::uint8_t(selected_);
            state_.indexSelected = 0;
            state_.indexFavorite = 0;
            state_.indexMode = 0;
            state_.scene = scene::Menu; // scene 31: the runner makes it 28
            // Destructor 020f096c: opened from the bonus menu (following
            // 28), the lower text is cleared and the slot saved.
            if (state_.following == 28) {
                kit_.clearText(SceneKit::Lower);
                FileDialog::quietSave(kit_, state_, saveFile);
            }
            return;
        }
        // Scroll buttons while close is idle (020802f8: pressed inside).
        if (close_.button.state == Button::State::Idle) {
            auto down = [](const SpriteButton& b) {
                return b.button.state == Button::State::Armed && b.button.inside;
            };
            if (down(up_)) {
                scrollUp(2);
            }
            if (down(upFast_)) {
                scrollUp(5);
            }
            if (down(down_)) {
                scrollDown(2);
            }
            if (down(downFast_)) {
                scrollDown(5);
            }
        }
        // Dragging sound: SE 126 while the thumb moves, stopped when it rests.
        {
            const int d = std::abs(previous - bar_.pos);
            previous = bar_.pos;
            if (!dragSound && d && bar_.dragging) {
                kit_.effect(126);
                dragSound = true;
            } else if (dragSound && d == 0) {
                if (auto s = kit_.sound()) {
                    s->stopEffect(126, 0.0f);
                }
                dragSound = false;
            }
        }
        if (mode_ == 0) {
            for (int i = 0; i < 5; ++i) {
                if (!kit_.clicked(rows_[std::size_t(i)]) || count_ <= top_ + i) {
                    continue;
                }
                state_.indexRow = std::uint8_t(top_ + i + 1); // +0x81d0
                resetStart();
                highlight(58 + 20 * i);
                const int n = all_[std::size_t(top_ + i)];
                if (selected_ != n) {
                    selected_ = n;
                    selectChanged();
                }
            }
            if (bar_.changed) {
                top_ = bar_.index;
                clearList();
                redraw_ = true;
            }
        }
    }
}
} // namespace picarat
