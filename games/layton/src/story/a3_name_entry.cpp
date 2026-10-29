// The third adventure's name entry. See a3_name_entry.hpp.
#include "story/a3_name_entry.hpp"
#include "sound/a3_sound.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace picarat::adventure3 {
namespace {
using lscr::Value;
constexpr int kOverlay = 33;
constexpr std::uint32_t kLoad = 0x0211dae0;
// 0211e7d0: the layouts' row tables (four pointers each) and the accented
// letters (0211e9d4).
constexpr std::uint32_t kRowTables[4] = {0, 0x0211f61c, 0x0211f62c, 0x0211f60c};
constexpr std::uint32_t kAccents = 0x0211f5b0;
// 0211f100: the name's character cells (floats, x on the lower screen),
// for the two name modes.
constexpr std::uint32_t kCells = 0x0211f560;
// Key rows: top at 49, 19 pixels high, keys 19 pixels wide starting at
// these x positions (0211e7d0, 0211efc8).
constexpr int kRowTop = 49, kKey = 19;
constexpr int kRowStart[4] = {15, 24, 34, 43};
constexpr int kLockout = 9; // 0211f1c4

// The game variables the keyboard shares with the script (the overlay's
// names: ワーク[0] layout, [1] a button's request, [2] OK refused).
constexpr std::uint32_t kWork[3] = {0x0211f668, 0x0211f65c, 0x0211f650};
constexpr std::uint32_t kFirstPhase = 0x0211f914; // the new game's phase

int work(Story& s, int i) {
    return s.state.variable(s.codeName(kWork[i], kOverlay));
}

void setWork(Story& s, int i, int v) {
    s.state.setVariable(s.codeName(kWork[i], kOverlay), v, 0, true);
}

void playEffect(Story& s, int id) {
    if (s.sound) {
        s.sound->playEffect(id);
    }
}
} // namespace

int NameEntryLoop::cellX(int i) const {
    // A float in the overlay's table.
    const std::uint32_t bits = story_.assets.codeWord(kOverlay, kLoad, kCells + 4u * unsigned(i));
    float f;
    std::memcpy(&f, &bits, 4);
    return int(f);
}

std::string NameEntryLoop::name() const {
    // 0211eed8: the characters up to the first empty one.
    std::string n;
    for (char c : characters_) {
        if (!c) {
            break;
        }
        n += c;
    }
    return n;
}

void NameEntryLoop::activate() {
    // 0211db30.
    mode_ = args_[0];
    if (mode_ == 2) {
        throw std::runtime_error("the secret door's password entry is not reconstructed");
    }
    maxLength_ = 10;
    for (int layout = 1; layout <= 3; ++layout) {
        for (int row = 0; row < 4; ++row) {
            const auto p =
                story_.assets.codeWord(kOverlay, kLoad, kRowTables[layout] + 4u * unsigned(row));
            rows_[size_t(layout)][size_t(row)] = story_.assets.codeText(kOverlay, kLoad, p);
        }
    }
    accents_ = story_.assets.codeText(kOverlay, kLoad, kAccents);
    characters_.assign(size_t(maxLength_), '\0');
    cursor_ = -1;
    lockout_ = 0;
    errorShown_ = highlighted_ = finishing_ = false;
    layout_ = 0;

    scene_ = makeScene();
    scene_->load("evt/prg/lt3_name_resist.cbin");
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    scene_->loadAnimation("anm_name", 0, "menu/?/name_btn.cani");
    scene_->loadAnimation("anm_wnd", 0, "ani/win_tobj.cani");
    scene_->loadTextTable("text", 40, 3000);
    static const char* kTop[] = {"menu/?/name_b1.cimg", "menu/?/name_ob1.cimg"};
    scene_->setScreenPicture(1, &story_.assets.picture(kTop[mode_ == 1]));
    if (mode_ == 0) {
        // A new game starts here: its data, and the first phase.
        story_.resetGame();
        if (const Phase* p = story_.data.phase(story_.codeName(kFirstPhase, kOverlay).text)) {
            story_.enterPhase(*p);
        }
    }
    runToEnd("NAME_RESIST_BEGIN");
    // One message per character cell, centred six pixels right of the
    // cell's left edge, 18 down (020232ec adds 6.0; flag bit 0 of +0xf0
    // centres - matches the ROM's picture).
    for (int i = 0; i < maxLength_; ++i) {
        if (lscr::Object* m =
                scene_->createMessage("msg_n" + std::to_string(i), lscr::Screen::Lower, 0x18, 0)) {
            m->position.set({(cellX(i) + 6) * lscr::fx::kOne, 18 * lscr::fx::kOne});
            m->text.style.centerX = true;
        }
    }
    if (lscr::Object* cur = scene_->find("anm_a_cur")) {
        cur->position.set({cellX(0) * lscr::fx::kOne, 18 * lscr::fx::kOne});
    }
    scene_->pushScope();
    setLayout(1);
    scene_->setAllInput(false);
    fade_ = 1;
}

void NameEntryLoop::deactivate() {
    scene_.reset();
}

void NameEntryLoop::runToEnd(const std::string& section) {
    // 020f19d0 then the thread run until it ends (vtable 0xb8), within the
    // frame.
    scene_->start(section);
    for (int guard = 0; scene_->running(); ++guard) {
        if (guard > 10000) {
            throw std::runtime_error(section + " does not end");
        }
        scene_->mainThread()->run();
    }
}

void NameEntryLoop::setLayout(int layout) {
    // 0211e500: the keyboard picture and the caps / shift lamps.
    if (layout == layout_) {
        return;
    }
    scene_->popScope();
    scene_->pushScope();
    auto lamp = [&](const char* n, bool on) {
        if (lscr::Object* o = scene_->find(n)) {
            o->alpha.set({on ? lscr::fx::kOne : 0});
        }
    };
    if (layout >= 1 && layout <= 3) {
        lamp("anm_a_caps_on", layout == 2);
        lamp("anm_a_shift_on", layout == 3);
    }
    if (layout >= 1 && layout <= 4) {
        scene_->setScreenPicture(
            0, &story_.assets.picture("menu/name_bg" + std::to_string(layout) + ".cimg"));
    }
    layout_ = layout;
}

void NameEntryLoop::moveCursor() {
    // 0211ecf8: the next cell (or the last one again) and the cursor there.
    if (cursor_ < maxLength_ - 1) {
        ++cursor_;
    }
    if (lscr::Object* cur = scene_->find("anm_a_cur")) {
        const int cell = cursor_ < maxLength_ - 1 ? cursor_ + 1 : cursor_;
        cur->position.set({cellX(cell) * lscr::fx::kOne, 18 * lscr::fx::kOne});
        lscr::play(*cur, lscr::sequenceOf(*cur, Value::ofText("here")), 0);
    }
}

void NameEntryLoop::type(char c) {
    moveCursor();
    characters_[size_t(cursor_)] = c;
    // 0211eccc: the cell's message shows the character.
    if (lscr::Object* m = scene_->message("msg_n" + std::to_string(cursor_))) {
        m->text.set(std::string(1, c), story_.text, true);
    }
}

void NameEntryLoop::erase() {
    // 0211ee08.
    if (cursor_ < 0) {
        return;
    }
    if (lscr::Object* m = scene_->message("msg_n" + std::to_string(cursor_))) {
        m->text.clear();
    }
    characters_[size_t(cursor_)] = '\0';
    if (lscr::Object* cur = scene_->find("anm_a_cur")) {
        cur->position.set({cellX(cursor_) * lscr::fx::kOne, 18 * lscr::fx::kOne});
        lscr::play(*cur, lscr::sequenceOf(*cur, Value::ofText("here")), 0);
    }
    --cursor_;
}

void NameEntryLoop::press(int x, int y) {
    auto light = [&](int hx, int hy) {
        // 0211efc8: the key under the pen lights up.
        highlighted_ = true;
        highlight_ = {hx, hy};
        if (lscr::Object* t = scene_->find("anm_a_touching")) {
            t->alpha.set({lscr::fx::kOne});
            t->position.set({hx * lscr::fx::kOne, hy * lscr::fx::kOne});
        }
    };
    const int row = (y - kRowTop) / kKey;
    if (layout_ == 4) {
        // 0211e9d4: thirteen accented letters a row.
        if (x > 4 && x < 247 && y > 48 && y < 125) {
            const int col = (x - 5) / kKey;
            light(col * kKey + 5, row * kKey + kRowTop);
            const int i = row * 13 + col;
            if (i < int(accents_.size())) {
                type(accents_[size_t(i)]);
            }
            playEffect(story_, 0x29);
            lockout_ = kLockout;
        }
        return;
    }
    // 0211eaa0: the four rows' extents.
    const bool onKeys =
        (x > 14 && x < 243 && y > 48 && y < 68) || (x > 23 && x < 252 && y > 67 && y < 87) ||
        (x > 4 && x < 243 && y > 86 && y < 106) || (x > 4 && x < 233 && y > 105 && y < 125);
    if (!onKeys) {
        return;
    }
    if (x > 4 && x < 34 && y > 86 && y < 106) { // 0211eb64: caps
        playEffect(story_, 0x29);
        lockout_ = kLockout;
        setWork(story_, 0, layout_ == 2 ? 1 : 2);
        return;
    }
    if (x > 4 && x < 43 && y > 105 && y < 125) { // 0211ec18: shift
        playEffect(story_, 0x29);
        lockout_ = kLockout;
        setWork(story_, 0, layout_ == 3 ? 1 : 3);
        return;
    }
    const int col = (x - kRowStart[row]) / kKey;
    light(col * kKey + kRowStart[row], row * kKey + kRowTop);
    const std::string& keys = rows_[size_t(layout_)][size_t(row)];
    type(col < int(keys.size()) ? keys[size_t(col)] : ' ');
    if (layout_ == 3) {
        setWork(story_, 0, 1); // shift lasts one key
    }
    playEffect(story_, 0x29);
    lockout_ = kLockout;
}

void NameEntryLoop::closeError() {
    // 0211e03c: the next touch closes the error window.
    if (lscr::Object* w = scene_->find("anm_a_wnd")) {
        w->alpha.set({0});
    }
    if (lscr::Object* w = scene_->find("anm_a_wnd_pass")) {
        w->alpha.set({0});
    }
    if (lscr::Object* m = scene_->message("msg_a_ps_err")) {
        m->switches[3].on = false;
    }
    errorShown_ = false;
}

bool NameEntryLoop::nameAccepted() {
    // 0211f320: the two names the game keeps for its own characters are
    // refused, in any case (the overlay's strings).
    std::string upper = name();
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') {
            c = char(c - 0x20);
        }
    }
    const std::string refused1 = story_.assets.codeText(kOverlay, kLoad, 0x0211fabc);
    const std::string refused2 = story_.assets.codeText(kOverlay, kLoad, 0x0211fac4);
    if (upper == refused1 || upper == refused2) {
        playEffect(story_, 3);
        lockout_ = kLockout;
        // 0211f4e0: the name window (its text is part of the picture); the
        // password message (msg_a_ps_err) is the secret door's (mode 2).
        if (lscr::Object* w = scene_->find("anm_a_wnd")) {
            w->alpha.set({lscr::fx::kOne});
        }
        errorShown_ = true;
        setWork(story_, 2, 1);
        return false;
    }
    return true;
}

void NameEntryLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    // 0211e42c: the layout follows ワーク[0]; the screens fade in once.
    if (const int w = work(story_, 0); w != layout_) {
        setLayout(w);
    } else if (fade_ == 1) {
        fade_ = 2;
    } else if (fade_ == 2) {
        for (auto& b : scene_->brightness) {
            b.to(1, {0}, 30 * lscr::fx::kOne);
        }
        fade_ = 3;
    } else if (fade_ == 3 && !scene_->brightness[0].active() && !scene_->brightness[1].active()) {
        scene_->setAllInput(true);
        fade_ = 0;
    }
    scene_->frame(input);
    if (finishing_) {
        return;
    }
    // 0211e03c: touches, then the script buttons' requests.
    if (lockout_ > 0) {
        --lockout_;
    }
    if (!input.pressed && fade_ == 0 && !typed_.empty()) {
        const NameInput key = typed_.front();
        typed_.pop_front();
        if (errorShown_) {
            closeError();
            return;
        }
        switch (key.action) {
        case NameInput::Action::Insert:
            if (key.character == ' ' ||
                std::any_of(rows_.begin(), rows_.end(),
                            [&](const auto& rows) {
                                return std::any_of(
                                    rows.begin(), rows.end(), [&](const std::string& r) {
                                        return r.find(char(key.character)) != std::string::npos;
                                    });
                            }) ||
                accents_.find(char(key.character)) != std::string::npos) {
                type(char(key.character));
                playEffect(story_, 0x29);
            }
            break;
        case NameInput::Action::Erase:
            setWork(story_, 1, 4); // as the delete button
            break;
        case NameInput::Action::Confirm:
            setWork(story_, 1, 2); // as OK
            break;
        default:
            break;
        }
    }
    if (input.pressed) {
        if (lockout_ > 0) {
            return;
        }
        if (errorShown_) {
            closeError();
            return;
        }
        press(input.x, input.y);
        return;
    }
    if (highlighted_ && (input.held || input.released)) {
        const bool outside = input.x < highlight_[0] || input.x >= highlight_[0] + kKey ||
                             input.y < highlight_[1] || input.y >= highlight_[1] + kKey;
        if (input.released || outside) {
            highlighted_ = false;
            if (lscr::Object* t = scene_->find("anm_a_touching")) {
                t->alpha.set({0});
            }
            if (input.released) {
                return;
            }
        }
    }
    auto fadeOut = [&] {
        for (auto& b : scene_->brightness) {
            b.to(1, {-lscr::fx::kOne}, 30 * lscr::fx::kOne, true);
        }
    };
    switch (work(story_, 1)) {
    case 1: // back
        finishing_ = true;
        fadeOut();
        runToEnd("NAME_RESIST_BACK");
        return;
    case 2: // OK
        if (characters_[0] == '\0') {
            setWork(story_, 2, 1);
            playEffect(story_, 4);
        } else if (nameAccepted()) {
            playEffect(story_, 0x6f);
            finishing_ = true;
            fadeOut();
            story_.names[size_t(mode_)] = name(); // 0211eed8
            if (mode_ == 0) {
                runToEnd("NAME_RESIST_ENTRY");
            } else {
                story_.request(Story::Request::Pop);
            }
            return;
        }
        break;
    case 3:
        type(' '); // 0211edc8
        break;
    case 4:
        erase();
        break;
    default:
        break;
    }
    setWork(story_, 1, 0);
}

} // namespace picarat::adventure3
