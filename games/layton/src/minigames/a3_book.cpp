// The third adventure's picture book (see a3_book.hpp).
#include "minigames/a3_book.hpp"
#include <algorithm>
#include <cstdio>
#include <map>

namespace picarat::adventure3 {

namespace {
constexpr int kOverlay = 19;
constexpr std::uint32_t kDataFile = 0x02121388; // rc/mode/?/lt3_mini_book.cbin
constexpr std::uint32_t kProgram = 0x021213a8;  // evt/prg/lt3_mini_book_main.cbin
constexpr std::uint32_t kIconFormat = 0x02121378, kImageFormat = 0x02121380; // i_%s, s_%s
constexpr std::uint32_t kCommon = 0x021213c8, kCommonFile = 0x021213d4;      // book_anm
constexpr std::uint32_t kItems = 0x021213fc, kItemsFile = 0x021213e8;        // book_item, b_spr%d
constexpr std::uint32_t kUpper = 0x02121408;                                 // mini/b_%db.cimg
constexpr std::uint32_t kTexts = 0x02121418;                                 // book_text
constexpr int kTextFile = 40, kTextPart = 7100;                              // 0211eddc
constexpr std::uint32_t kSeal = 0x02121424;                                  // seal%d
constexpr std::uint32_t kReleased = 0x0212142c, kTouched = 0x02121434;       // MOV_N, MOV_T
constexpr std::uint32_t kSolvedFlag = 0x0212143c;   // ＜絵本クリアー＞初級 [book]
constexpr std::uint32_t kAllModesFlag = 0x02121454; // ＜３つのモードクリアー＞絵本
constexpr std::uint32_t kMain = 0x02121474;
constexpr std::uint32_t kCover = 0x0212147c, kPicture = 0x02121494; // b%d_%02da (cover localised)
constexpr std::uint32_t kMarker = 0x021214a8, kMarkerSequence = 0x021214b0; // mark%d, marker
constexpr std::uint32_t kViewButton = 0x021214b8;
constexpr std::uint32_t kWordFormat = 0x021214c4,
                        kHintFormat = 0x021214d0; // <CS>%s</CS>, <CR>[%s]</CR>
constexpr std::uint32_t kNext = 0x021214e0, kPrevious = 0x021214ec;
constexpr std::uint32_t kNextOn = 0x021214f8, kNextOff = 0x02121504, kPreviousOn = 0x02121510,
                        kPreviousOff = 0x0212151c;
constexpr std::uint32_t kPicker = 0x02121528; // MINI_BOOK_SELECT
// ワーク[0..10] (0x021210b8.. as the overlay's table 0x02121060 names them).
constexpr std::array<std::uint32_t, 11> kWork{0x021210e8, 0x021210f4, 0x021210b8, 0x021210c4,
                                              0x02121100, 0x021210d0, 0x021210dc, 0x02121130,
                                              0x0212113c, 0x0212110c, 0x02121160};

enum Work {
    kMode = 0,      // 1 set up, 2 browsing, 3 / 4 stickers out, 5 looking, 6 closing
    kPicked = 1,    // the book the picker chose (1..3)
    kPage = 2,      // the page shown (1..)
    kLastPage = 3,  //
    kDrop = 4,      // 1 a sticker is carried, 2 dropped
    kDropX = 5,     //
    kDropY = 6,     //
    kDropped = 7,   // which sticker
    kScroll = 8,    // the tray's first place
    kTakeOff = 9,   // 1: every sticker back into the tray
    kTraySize = 10, //
};

constexpr int kTrayPlaces = 6;
// 0203f7b4 is 5 for this ROM (the German build): the text's line spacing
// 5 and its first line 8 pixels down (7 and 12 when it is 1).
constexpr int kLineSpacing = 5, kTextTop = 8, kTextLeft = 14;
} // namespace

// --- The book data --------------------------------------------------------------------

const BookData::Spot* BookData::Page::spot(int gap) const {
    for (const auto& s : spots) {
        if (s.gap == gap) {
            return &s;
        }
    }
    return nullptr;
}

const BookData& BookData::get(const Story& story) {
    static std::map<const Assets*, BookData> cache;
    if (auto it = cache.find(&story.assets); it != cache.end()) {
        return it->second;
    }
    BookData d;
    const auto file = story.assets.script(story.codeName(kDataFile, kOverlay).text);
    const std::string iconFormat = story.codeName(kIconFormat, kOverlay).text;
    const std::string imageFormat = story.codeName(kImageFormat, kOverlay).text;
    Book* book = nullptr;
    Page* page = nullptr;
    Sticker* sticker = nullptr;
    std::size_t form = 0;
    for (const auto& c : file.commands) {
        auto text = [&](std::size_t i) {
            return i < c.operands.size() && c.operands[i].type == 2 ? file.text(c.operands[i])
                                                                    : std::string();
        };
        auto num = [&](std::size_t i) {
            return i < c.operands.size() ? int(std::int32_t(c.operands[i].value)) : 0;
        };
        switch (c.opcode) {
        case 4010:
            d.books.emplace_back();
            book = &d.books.back();
            break;
        case 4011:
            book = nullptr;
            break;
        case 4020:
            if (book) {
                book->pages.emplace_back();
                page = &book->pages.back();
            }
            break;
        case 4021:
            page = nullptr;
            break;
        case 4030: // 0211dff8
            if (page && c.operands.size() >= 5) {
                page->lines.push_back({text(0), text(2), text(3), num(4), num(1), {}});
            }
            break;
        case 4031: // 0211e100
            if (page && c.operands.size() >= 3) {
                page->spots.push_back({num(0), num(1), num(2)});
            }
            break;
        case 4032: // 0211e1ec: to the last line of that name
            if (page) {
                for (auto l = page->lines.rbegin(); l != page->lines.rend(); ++l) {
                    if (l->message == text(0)) {
                        for (std::size_t i = 1; i + 2 < c.operands.size(); i += 3) {
                            l->choices.push_back({text(i), text(i + 1), num(i + 2)});
                        }
                        break;
                    }
                }
            }
            break;
        case 4041: { // 0211e48c
            if (c.operands.size() < 6) {
                break;
            }
            Sticker s;
            s.found = text(0);
            s.placed = text(1);
            s.book = num(2);
            s.gap = num(3);
            char icon[48], image[48];
            std::snprintf(icon, sizeof icon, iconFormat.c_str(), text(4).c_str());
            std::snprintf(image, sizeof image, imageFormat.c_str(), text(4).c_str());
            s.icon = icon;
            s.image = image;
            s.choice = num(5);
            d.stickers.push_back(std::move(s));
            sticker = &d.stickers.back();
            break;
        }
        case 4042: // 0211e5e8: four forms (the counter runs over the twenty)
            for (std::size_t i = 0; i < 4 && sticker; ++i) {
                sticker->forms[form] = text(i);
                form = (form + 1) % sticker->forms.size();
            }
            break;
        default:
            break;
        }
    }
    return cache.emplace(&story.assets, std::move(d)).first->second;
}

// --- The loop ----------------------------------------------------------------------

std::string BookLoop::name(std::uint32_t address) const {
    return story_.codeName(address, kOverlay).text;
}

std::string BookLoop::format(std::uint32_t address, int a, int b) const {
    char out[64];
    std::snprintf(out, sizeof out, name(address).c_str(), a, b);
    return out;
}

int BookLoop::work(int i) const {
    return story_.state.variable(story_.codeName(kWork[size_t(i)], kOverlay));
}

void BookLoop::setWork(int i, int value) {
    story_.state.setVariable(story_.codeName(kWork[size_t(i)], kOverlay), value, 0, true);
}

const BookData::Page* BookLoop::page(int number) const { // 0211e78c
    const auto& d = BookData::get(story_);
    if (book_ < 0 || book_ >= int(d.books.size())) {
        return nullptr;
    }
    const auto& pages = d.books[size_t(book_)].pages;
    return number >= 1 && number <= int(pages.size()) ? &pages[size_t(number - 1)] : nullptr;
}

void BookLoop::activate() {
    // 0211eccc: first the picker; back from it, the book it chose, or the
    // end when it was closed.
    if (!picked_ && state_ == State::Idle) {
        pickAgain();
        return;
    }
    if (state_ == State::Picking) {
        const int chosen = work(kPicked);
        if (chosen < 1 || chosen > 3) {
            state_ = State::Idle;
            story_.request(Story::Request::Pop);
            return;
        }
        book_ = chosen - 1;
        open();
        return;
    }
    if (picked_) {
        open(); // uncovered while open: the book again (hypothesis)
    }
}

void BookLoop::pickAgain() {
    // 02120be4: everything back, the page variable 0, the picker pushed.
    picked_ = true;
    book_ = page_ = trayAt_ = bookStickers_ = trayCount_ = 0;
    seals_.clear();
    tray_.fill(-1);
    setWork(kPage, 0);
    state_ = State::Picking;
    story_.request(Story::Request::Push, name(kPicker), args_);
}

void BookLoop::open() {
    // The book's sprites, the stickers, its cover on the upper screen, its
    // texts, the data and the program (0211e7d0 / 0211eccc).
    scene_ = makeScene();
    scene_->load(name(kProgram));
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    scene_->loadAnimation(name(kCommon), 0, name(kCommonFile));
    scene_->loadAnimation(name(kItems), 0, format(kItemsFile, book_ + 1));
    const std::string upper = format(kUpper, book_ + 1);
    scene_->setScreenPicture(1,
                             story_.assets.exists(upper) ? &story_.assets.picture(upper) : nullptr);
    scene_->loadTextTable(name(kTexts), kTextFile, kTextPart);
    page_ = trayAt_ = 0;
    made_.clear();
    state_ = State::Loading;
}

void BookLoop::deactivate() {
    if (state_ == State::Running || state_ == State::Loading) {
        keepSeals();
    }
    scene_.reset();
}

bool BookLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void BookLoop::loadSeals() {
    // 02120ab4: the book's stickers that have been found, where each was
    // left; those on no gap make the tray.
    const auto& d = BookData::get(story_);
    seals_.clear();
    bookStickers_ = 0;
    trayCount_ = 0;
    tray_.fill(-1);
    for (std::size_t k = 0; k < d.stickers.size(); ++k) {
        const auto& s = d.stickers[k];
        if (s.book != book_) {
            continue;
        }
        ++bookStickers_;
        if (!story_.state.flag(lscr::Value::ofText(s.found))) {
            continue;
        }
        Seal seal{int(k), story_.state.variable(lscr::Value::ofText(s.placed))};
        if (seal.gap == 0 && trayCount_ < int(tray_.size())) {
            tray_[size_t(trayCount_++)] = seal.sticker;
        }
        seals_.push_back(seal);
    }
}

void BookLoop::keepSeals() {
    // 02120a2c: each found sticker's gap into its variable.
    const auto& d = BookData::get(story_);
    for (const Seal& s : seals_) {
        story_.state.setVariable(lscr::Value::ofText(d.stickers[size_t(s.sticker)].placed), s.gap,
                                 0, true);
    }
}

bool BookLoop::solved() const {
    // 02120134: the book's stickers in order, each on its own gap (all of
    // them found: the game walks its found ones alongside).
    const auto& d = BookData::get(story_);
    std::size_t i = 0;
    for (std::size_t k = 0; k < d.stickers.size(); ++k) {
        if (d.stickers[k].book != book_) {
            continue;
        }
        if (i >= seals_.size() || seals_[i].sticker != int(k) ||
            seals_[i].gap != d.stickers[k].gap) {
            return false;
        }
        ++i;
    }
    return true;
}

void BookLoop::viewButton() {
    // 02120194: "look at the book" once it was solved and every sticker is out.
    bool on = story_.state.flag(story_.codeName(kSolvedFlag, kOverlay), book_) && !seals_.empty();
    for (const Seal& s : seals_) {
        on = on && s.gap != 0;
    }
    if (lscr::Object* b = scene_->find(name(kViewButton))) {
        b->switches[0].on = on;
    }
}

void BookLoop::layers() { // 021200a8
    for (std::size_t i = 0; i < seals_.size(); ++i) {
        if (lscr::Object* o = sealObject(int(i))) {
            scene_->setLayer(*o, seals_[i].gap == 0 ? 1 : 3);
        }
    }
}

lscr::Object* BookLoop::sealObject(int seal) {
    return scene_->find(format(kSeal, seal));
}

int BookLoop::sealOnGap(int gap) const {
    for (std::size_t i = 0; i < seals_.size(); ++i) {
        if (seals_[i].gap == gap) {
            return int(i);
        }
    }
    return -1;
}

void BookLoop::setUp(lscr::Object& o, int seal) {
    // 02120220: the sticker's values for the script - {sequence now, its
    // picture, which sticker, its gap, 1 in the tray / 2 out} - its look
    // and layer.
    const auto& d = BookData::get(story_);
    const Seal& s = seals_[size_t(seal)];
    const auto& st = d.stickers[size_t(s.sticker)];
    const bool out = s.gap != 0;
    if (out) {
        o.switches[0].on = true;
    }
    const std::string& now = out ? st.image : st.icon;
    lscr::play(o, lscr::sequenceOf(o, lscr::Value::ofText(now)), 0);
    scene_->setLayer(o, out ? 3 : 1);
    if (o.thread) {
        auto& v = o.thread->variables;
        v[0] = lscr::Value::ofText(now);
        v[1] = lscr::Value::ofText(st.image);
        v[2] = lscr::Value::of(s.sticker);
        v[3] = lscr::Value::of(s.gap);
        v[4] = lscr::Value::of(out ? 2 : 1);
    }
}

void BookLoop::makeSeal(int seal) {
    // 020f29d8: a piece (as 4021) with the script's MOV_N / MOV_T.
    lscr::Object* o =
        scene_->create(lscr::Object::Kind::Button, format(kSeal, seal), name(kItems), false);
    if (!o) {
        return;
    }
    scene_->objectThread(*o, {});
    o->style = lscr::Object::Style::Piece;
    o->touch = 0;
    o->releasedSection = name(kReleased);
    o->touchedSection = name(kTouched);
    o->clickedSection = name(kTouched);
}

void BookLoop::arrows(int scroll) {
    // 021206d4: the tray's arrows, live when there is more that way.
    auto set = [&](std::uint32_t object, bool live, std::uint32_t on, std::uint32_t off) {
        if (lscr::Object* o = scene_->find(name(object))) {
            lscr::play(*o, lscr::sequenceOf(*o, lscr::Value::ofText(name(live ? on : off))), 0);
            o->switches[1].on = live;
        }
    };
    set(kNext, scroll + kTrayPlaces < int(tray_.size()) && tray_[size_t(scroll + kTrayPlaces)] >= 0,
        kNextOn, kNextOff);
    set(kPrevious, scroll > 0, kPreviousOn, kPreviousOff);
}

void BookLoop::showTray(int scroll) {
    // 021207ec: the tray's six places from `scroll`, 30 pixels apart from
    // x 52 at y 175 (32 lower, out of sight, unless the stickers are out).
    const bool out = work(kMode) == 4;
    for (std::size_t i = 0; i < seals_.size(); ++i) {
        if (seals_[i].gap != 0) {
            continue;
        }
        lscr::Object* o = sealObject(int(i));
        if (!o) {
            return;
        }
        bool shown = false;
        for (int j = 0; j < kTrayPlaces; ++j) {
            const int at = scroll + j;
            if (at >= 0 && at < int(tray_.size()) && tray_[size_t(at)] == seals_[i].sticker) {
                o->position.set(
                    {(52 + 30 * j) * lscr::fx::kOne, (out ? 175 : 207) * lscr::fx::kOne});
                const auto& st = BookData::get(story_).stickers[size_t(seals_[i].sticker)];
                lscr::play(*o, lscr::sequenceOf(*o, lscr::Value::ofText(st.icon)), 0);
                scene_->setLayer(*o, 1);
                if (o->thread) {
                    o->thread->variables[4] = lscr::Value::of(1);
                }
                shown = true;
                break;
            }
        }
        o->switches[0].on = shown;
    }
    arrows(scroll);
    trayAt_ = scroll;
    setWork(kTraySize, trayCount_);
}

bool BookLoop::move(int sticker, int gap, int x) {
    // 0211f4ec: a sticker put on a gap (taking the place of one there,
    // which goes where it came from) or into the tray at the place nearest
    // x (a gap of 0).
    const int scroll = work(kScroll);
    int i = -1;
    for (std::size_t k = 0; k < seals_.size(); ++k) {
        if (seals_[k].sticker == sticker) {
            i = int(k);
        }
    }
    if (i < 0) {
        showTray(scroll);
        return true;
    }
    if (gap > 0 && gap == seals_[size_t(i)].gap) {
        showTray(scroll); // put back where it was
        return false;
    }
    int other = -1;
    if (gap != 0) {
        other = sealOnGap(gap);
    }
    auto removeFromTray = [&](int t) {
        for (int k = t; k < trayCount_; ++k) {
            tray_[size_t(k)] = k + 1 < int(tray_.size()) ? tray_[size_t(k + 1)] : -1;
        }
        if (trayCount_ > 0) {
            tray_[size_t(trayCount_ - 1)] = -1;
            --trayCount_;
        }
    };
    auto trayIndex = [&](int s) {
        for (int k = 0; k < trayCount_; ++k) {
            if (tray_[size_t(k)] == s) {
                return k;
            }
        }
        return -1;
    };
    if (gap == 0) {
        if (seals_[size_t(i)].gap == 0) {
            if (const int t = trayIndex(sticker); t >= 0) {
                removeFromTray(t);
            }
        }
        const int place = x < 0x43 ? 0 : std::min((x - 0x43) / 30 + 1, 5);
        int at = std::min(trayCount_, std::max(0, int(tray_.size()) - 1));
        for (; place + scroll < at; --at) {
            tray_[size_t(at)] = tray_[size_t(at - 1)];
        }
        tray_[size_t(at)] = sticker;
        ++trayCount_;
        seals_[size_t(i)].gap = 0;
    } else {
        if (seals_[size_t(i)].gap == 0) {
            if (const int t = trayIndex(sticker); t >= 0) {
                if (other >= 0) {
                    tray_[size_t(t)] = seals_[size_t(other)].sticker; // the one it replaces
                } else {
                    removeFromTray(t);
                    if (scroll > 0 &&
                        (scroll + 5 >= int(tray_.size()) || tray_[size_t(scroll + 5)] < 0)) {
                        setWork(kScroll, scroll - 1);
                    }
                }
            }
        }
        if (other >= 0) {
            std::swap(seals_[size_t(i)].gap, seals_[size_t(other)].gap);
        } else {
            seals_[size_t(i)].gap = gap;
        }
    }
    showTray(work(kScroll));
    return true;
}

void BookLoop::writeLine(lscr::Object& message, const BookData::Line& line, int seal) {
    // 02120414 / 02120538: the line with its gap - the hint in brackets, or
    // the sticker's word in the line's form - and its '#'s each the word
    // the sticker on that gap takes.
    const auto& d = BookData::get(story_);
    char word[160];
    if (seal < 0) {
        std::snprintf(word, sizeof word, name(kHintFormat).c_str(), line.hint.c_str());
    } else {
        const auto& st = d.stickers[size_t(seals_[size_t(seal)].sticker)];
        const int f = std::clamp(line.form, 0, int(st.forms.size()) - 1);
        std::snprintf(word, sizeof word, name(kWordFormat).c_str(), st.forms[size_t(f)].c_str());
    }
    char filled[512];
    std::snprintf(filled, sizeof filled, line.text.c_str(), word);
    std::string out, in = filled;
    std::size_t choice = 0;
    for (std::size_t p = 0; p < in.size(); ++p) {
        if (in[p] != '#' || choice >= line.choices.size()) {
            out += in[p];
            continue;
        }
        const auto& c = line.choices[choice++];
        const int s = sealOnGap(c.gap);
        if (s < 0) {
            out += '#';
            continue;
        }
        out += d.stickers[size_t(seals_[size_t(s)].sticker)].choice == 0 ? c.first : c.second;
    }
    message.text.set(out, story_.text, true);
}

void BookLoop::showPage(int number) {
    // 0211f8ac: the page's picture; its markers, points and lines afresh;
    // the stickers on its gaps there.
    if (number == 0) {
        const std::string cover = format(kCover, book_ + 1, 0);
        scene_->setScreenPicture(0, story_.assets.exists(cover) ? &story_.assets.picture(cover)
                                                                : nullptr);
        return;
    }
    const std::string picture = format(kPicture, book_ + 1, number);
    scene_->setScreenPicture(0, story_.assets.exists(picture) ? &story_.assets.picture(picture)
                                                              : nullptr);
    for (const std::string& m : made_) {
        scene_->removeMessage(m);
        scene_->points.erase(m);
    }
    made_.clear();
    for (int i = 0; i < 12; ++i) {
        scene_->remove(format(kMarker, i));
    }
    for (std::size_t i = 0; i < seals_.size(); ++i) {
        lscr::Object* o = sealObject(int(i));
        if (!o) {
            makeSeal(int(i));
            if ((o = sealObject(int(i)))) {
                o->switches[0].on = false;
                o->switches[1].on = false;
            }
        } else if (seals_[i].gap < 1) {
            if (o->thread) {
                o->thread->variables[5] = lscr::Value::of(1);
            }
        } else {
            o->switches[0].on = false;
            if (o->thread) {
                o->thread->variables[5] = lscr::Value::of(0);
            }
        }
    }
    const auto* p = page(number);
    if (!p) {
        return;
    }
    const bool looking = work(kMode) == 5;
    std::vector<int> done;
    int y = kTextTop;
    for (std::size_t li = 0; li < p->lines.size(); ++li) {
        const auto& line = p->lines[li];
        int seal = -1;
        if (!line.hint.empty()) {
            seal = sealOnGap(line.gap);
            const bool repeated = std::find(done.begin(), done.end(), line.gap) != done.end();
            done.push_back(line.gap);
            const auto* spot = p->spot(line.gap);
            if (spot && !repeated && spot->x > 0 && spot->y > 0) {
                scene_->point(line.message).local = {spot->x * lscr::fx::kOne,
                                                     spot->y * lscr::fx::kOne};
                made_.push_back(line.message);
                if (seal >= 0) {
                    if (lscr::Object* o = sealObject(seal)) {
                        setUp(*o, seal);
                        o->position.set({spot->x * lscr::fx::kOne, spot->y * lscr::fx::kOne});
                        if (o->thread) {
                            o->thread->variables[5] = lscr::Value::of(1);
                        }
                    }
                }
                if (!looking) {
                    // A marker where the gap's sticker goes (020f27cc).
                    const std::string marker = format(kMarker, int(li));
                    if (lscr::Object* m =
                            scene_->create(lscr::Object::Kind::Sprite, marker, name(kCommon))) {
                        lscr::play(*m,
                                   lscr::sequenceOf(*m, lscr::Value::ofText(name(kMarkerSequence))),
                                   0);
                        m->position.set({(spot->x - 1) * lscr::fx::kOne, spot->y * lscr::fx::kOne});
                        scene_->setLayer(*m, 3);
                        m->switches[0].on = seal < 0;
                    }
                }
            }
        }
        if (lscr::Object* m = scene_->createMessage(line.message, lscr::Screen::Upper, 0x80, 1)) {
            made_.push_back(line.message);
            m->position.set({kTextLeft * lscr::fx::kOne, y * lscr::fx::kOne});
            m->text.style.characterSpacing = 1; // 02069c74
            m->text.style.lineSpacing = kLineSpacing;
            writeLine(*m, line, seal);
            y += m->text.style.fontHeight + kLineSpacing; // +0xef (hypothesis: one line's pitch)
        }
    }
}

void BookLoop::retext() {
    // 0211fe80: the page's lines and markers as the stickers now are.
    if (const auto* p = page(page_)) {
        std::vector<int> done;
        for (std::size_t li = 0; li < p->lines.size(); ++li) {
            const auto& line = p->lines[li];
            lscr::Object* m = scene_->message(line.message);
            if (line.hint.empty()) {
                if (m && line.text.find('#') != std::string::npos) {
                    writeLine(*m, line, -1);
                }
                continue;
            }
            const int seal = sealOnGap(line.gap);
            if (m) {
                writeLine(*m, line, seal);
            }
            const bool repeated = std::find(done.begin(), done.end(), line.gap) != done.end();
            done.push_back(line.gap);
            const auto* spot = p->spot(line.gap);
            if (repeated || (spot && spot->x == 0 && spot->y == 0)) {
                continue;
            }
            if (lscr::Object* marker = scene_->find(format(kMarker, int(li)))) {
                marker->switches[0].on = seal < 0;
            }
        }
    }
    layers();
    viewButton();
}

void BookLoop::dropped() {
    // 0211edf0: a sticker let go (ワーク[4] 2) at (ワーク[5], ワーク[6]).
    const int x = work(kDropX), y = work(kDropY), sticker = work(kDropped);
    const auto* p = page(work(kPage));
    if (!p) {
        return;
    }
    if (work(kMode) == 4 && y >= 0xa0) { // the tray
        if (move(sticker, 0, x)) {
            retext();
        }
        setWork(kDrop, 0);
        return;
    }
    bool gaps = false;
    for (const auto& line : p->lines) {
        if (line.gap < 0) {
            continue;
        }
        gaps = true;
        const auto* spot = p->spot(line.gap);
        if (!spot || spot->x != x || spot->y != y) {
            continue;
        }
        if (!move(sticker, line.gap, 0)) {
            layers();
        } else {
            retext();
            const lscr::Value solvedFlag = story_.codeName(kSolvedFlag, kOverlay);
            if (solved() && !story_.state.flag(solvedFlag, book_)) {
                keepSeals();
                story_.state.setFlag(solvedFlag, true, book_);
                if (book_ == 2) {
                    story_.state.setFlag(story_.codeName(kAllModesFlag, kOverlay), true);
                }
                setWork(kPicked, 10); // the script's "solved"
            }
        }
        setWork(kDrop, 0);
        return;
    }
    if (gaps) {
        return; // let go off every gap: nothing (as the game)
    }
    if (move(sticker, 0, 0)) {
        retext();
    }
    setWork(kDrop, 0);
}

void BookLoop::frame(const lscr::SceneInput& input) {
    if (!scene_ || state_ == State::Idle || state_ == State::Picking) {
        return;
    }
    if (state_ == State::Loading) {
        // 0211f434: the last page's number, page 1, MAIN.
        const auto& d = BookData::get(story_);
        if (book_ < int(d.books.size())) {
            setWork(kLastPage, int(d.books[size_t(book_)].pages.size()) - 1);
        }
        showPage(1);
        scene_->start(name(kMain));
        state_ = State::Running;
    }
    scene_->frame(input);
    // 0211edf0 (vtable 0xe4).
    const int mode = work(kMode);
    if (mode == 0) {
        return;
    }
    const int scroll = work(kScroll);
    switch (mode) {
    case 1: // the stickers made
        loadSeals();
        for (std::size_t i = 0; i < seals_.size(); ++i) {
            makeSeal(int(i));
            if (lscr::Object* o = sealObject(int(i))) {
                setUp(*o, int(i));
            }
        }
        viewButton();
        showTray(scroll);
        setWork(kMode, 2);
        break;
    case 2:
        if (work(kTakeOff) == 1) {
            for (std::size_t i = 0; i < seals_.size(); ++i) {
                if (seals_[i].gap > 0) {
                    seals_[i].gap = 0;
                    if (trayCount_ < int(tray_.size())) {
                        tray_[size_t(trayCount_++)] = seals_[i].sticker;
                    }
                }
                if (lscr::Object* o = sealObject(int(i))) {
                    setUp(*o, int(i));
                    if (o->thread) {
                        o->thread->variables[5] = lscr::Value::of(1);
                    }
                    o->position.set({0, 0});
                    o->switches[0].on = false;
                }
            }
            retext();
            showTray(scroll);
        }
        break;
    case 3:
        arrows(scroll);
        setWork(kMode, 4);
        break;
    case 4:
        if (scroll != trayAt_) {
            showTray(scroll);
        }
        break;
    case 6: // closing: once the screens are dark, back to the picker
        if (!scene_->brightness[0].active() && !scene_->brightness[1].active()) {
            keepSeals();
            scene_.reset();
            pickAgain();
            setWork(kMode, 1);
            return;
        }
        break;
    default:
        break;
    }
    if (const int pageNow = work(kPage); pageNow != page_) {
        showPage(pageNow);
        page_ = pageNow;
    }
    if (work(kDrop) == 2) {
        dropped();
    }
}

// --- Free play ---------------------------------------------------------------------

void findAllStickers(Story& story) {
    for (const auto& s : BookData::get(story).stickers) {
        story.state.setFlag(lscr::Value::ofText(s.found), true);
    }
}

int bookStickerCount(const Story& story) {
    return int(BookData::get(story).stickers.size());
}

std::vector<std::uint8_t> bookProgress(const Story& story) {
    const auto& stickers = BookData::get(story).stickers;
    std::vector<std::uint8_t> out(std::size_t(kBooks) + stickers.size());
    for (int b = 0; b < kBooks; ++b) {
        out[std::size_t(b)] = story.state.flag(story.codeName(kSolvedFlag, kOverlay), b) ? 1 : 0;
    }
    for (std::size_t i = 0; i < stickers.size(); ++i) {
        out[kBooks + i] =
            std::uint8_t(story.state.variable(lscr::Value::ofText(stickers[i].placed)));
    }
    return out;
}

void setBookProgress(Story& story, const std::vector<std::uint8_t>& progress) {
    const auto& stickers = BookData::get(story).stickers;
    for (int b = 0; b < kBooks && b < int(progress.size()); ++b) {
        story.state.setFlag(story.codeName(kSolvedFlag, kOverlay), progress[std::size_t(b)] != 0,
                            b);
    }
    for (std::size_t i = 0; i < stickers.size() && kBooks + i < progress.size(); ++i) {
        story.state.setVariable(lscr::Value::ofText(stickers[i].placed), progress[kBooks + i], 0,
                                true);
    }
}

} // namespace picarat::adventure3
