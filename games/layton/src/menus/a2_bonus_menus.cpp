// The bonus section's pages (overlays 23 and 11). See a2_bonus_menus.hpp.
#include "menus/a2_bonus_menus.hpp"
#include "menus/a2_file_dialog.hpp"

namespace picarat {
namespace {
// 0206b114(255, 0, 0): the held key's highlight colour (RGB15 red).
constexpr std::uint16_t kRed = 0x801f;
} // namespace

BonusPage::BonusPage(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                     GameState& state, SaveFile* file)
    : kit_(assets, std::move(present), audio, SceneKind::Bonus), state_(state), file_(file) {
    kit_.resetDisplay(); // 0206ddec
}

BonusPage::SpriteButton BonusPage::picture(const std::string& path, int x, int y, int sound,
                                           const std::string& animation) {
    auto b = kit_.button(kit_.localized(path), x, y, sound, "");
    kit_.setButtonAnimations(b, animation, animation, animation);
    b.pressX = b.pressY = 1;
    return b;
}

void BonusPage::waitForMusic() {
    // 0207fa78 loop: 0206d950 and one frame until the BGM controller settles.
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.clearSubmissions();
            kit_.wait(1);
        }
    }
}

void BonusPage::leave(std::uint8_t scene, int following) {
    kit_.fadeOut(); // 02071e84
    if (following >= 0) {
        state_.following = std::uint8_t(following);
    }
    state_.scene = scene;
}

// ---------------------------------------------------------------------------
// Scene 28: the secret menu.

void SecretMenu::build() {
    // 020efda0: WiFi (10, 54) always; the puzzle index (132, 54) once a
    // puzzle was found (02083e40); the challenges (10, 106) once all photos
    // are complete (0208592c), all teas brewed (02085970), the hamster is
    // at level 6 (020859f4) or the slot's game is finished (020823c8);
    // the extras (132, 106) always; back (secret_modoru "pos", sound 4).
    const std::string path = "menu/secret/%s/secretmenu_btn.spr";
    wifi_ = picture(path, 10, 54, 3, "1");
    if (state_.foundCount() > 0) {
        index_ = picture(path, 132, 54, 3, "2");
    }
    bool photos = true;
    for (unsigned i = 0; i < 9; ++i) {
        photos &= state_.progress.photoComplete(i);
    }
    const bool finished = file_ ? file_->clearLevel(state_.saveSlot) != 0 : state_.cleared;
    if (photos || state_.teaComplete() || state_.progress.bytes[0x283] == 6 || finished) {
        challenge_ = picture(path, 10, 106, 3, "3");
    }
    extras_ = picture(path, 132, 106, 3, "4");
    back_ = kit_.button(kit_.localized("menu/secret/%s/secret_modoru.spr"), 0, 0, 4);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/secretmenu_bg");
    kit_.upperBackground = kit_.localized("menu/secret/%s/secretmenu_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(16, true);
    }
}

void SecretMenu::run() {
    build();
    // 020f0234; drawing 020f01c0 (update and draw every button).
    auto draw = [this] {
        for (auto* b : {&wifi_, &index_, &challenge_, &extras_, &back_}) {
            live(*b);
        }
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (kit_.clicked(wifi_)) {
            return leave(29, 28);
        }
        if (kit_.clicked(index_)) {
            return leave(31, 28);
        }
        if (kit_.clicked(challenge_)) {
            return leave(scene::Challenge, 28);
        }
        if (kit_.clicked(extras_)) {
            return leave(30, 28);
        }
        if (kit_.clicked(back_)) {
            // Back to the title: bonus mode ends (+0x8221), the music stops
            // over 1.6 s, one second passes.
            kit_.fadeOut();
            state_.scene = scene::Title;
            state_.chainBlocked = false;
            if (auto s = kit_.sound()) {
                s->stopBgm(1.6f);
            }
            kit_.wait(60);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 29: the WiFi menu.

void WifiMenu::run() {
    // 020f0420: the downloaded puzzles (48, 32) only when some were
    // downloaded (state +0x3ec); download (48, 80) and settings (48, 128);
    // cancel. The "new" badge (41, 28) marks a downloaded puzzle not yet
    // seen (its internal number at +0x3f4 + 4 i, status 0 by 02083d28).
    const std::string path = "menu/secret/?/wifimenu_btn.spr";
    const auto count = std::int32_t(state_.shared[4] | state_.shared[5] << 8 |
                                    state_.shared[6] << 16 | state_.shared[7] << 24);
    if (count > 0) {
        puzzles_ = picture(path, 48, 32, 3, "1");
    }
    download_ = picture(path, 48, 80, 3, "2");
    settings_ = picture(path, 48, 128, 3, "3");
    cancel_ = kit_.button("system/btn/" + kit_.language + "/cancel.spr", 0, 0, 4);
    for (std::int32_t i = 0; i < count && 0xc + 4 * i < std::int32_t(state_.shared.size()); ++i) {
        const int number = state_.puzzleNumber(state_.shared[std::size_t(0xc + 4 * i)]);
        if (state_.puzzleStatus(number) == 0) {
            fresh_ = true;
            break;
        }
    }
    if (fresh_) {
        badge_ = kit_.load(kit_.localized("system/btn/?/new_button.spr"), 0, 0, 1);
        badge_.x = 41;
        badge_.y = 28;
    }
    kit_.lowerBackground = kit_.localized("menu/secret/?/wifimenu_bg");
    kit_.upperBackground = kit_.localized("menu/secret/?/wifimenu_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(18, true);
    }
    // 020f07c4; drawing 020f075c.
    auto draw = [this] {
        for (auto* b : {&puzzles_, &download_, &settings_, &cancel_}) {
            live(*b);
        }
        if (badge_.loaded()) {
            kit_.draw(badge_);
        }
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (kit_.clicked(puzzles_)) {
            // The WiFi puzzle index (scene 22) in its download mode (+0x81d0).
            state_.indexRow = 1;
            return leave(22, 29);
        }
        if (kit_.clicked(download_)) {
            return leave(39);
        }
        if (kit_.clicked(settings_)) {
            return leave(38);
        }
        if (kit_.clicked(cancel_)) {
            return leave(28);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 30: the extras.

void ExtrasMenu::draw() {
    // 020f0f44: the buttons, then the picarats (020f123c): four digits from
    // x 104 leftwards in steps of 6 at y 175, digit d is animation "d".
    for (auto& p : pages_) {
        live(p);
    }
    live(back_);
    unsigned value = state_.progress.picarats();
    for (int i = 0, x = 104; i < 4; ++i, x -= 6) {
        kit_.select(digits_, std::to_string(value % 10));
        digits_.x = x;
        digits_.y = 175;
        kit_.draw(digits_);
        value /= 10;
    }
}

void ExtrasMenu::run() {
    // 020f0928: once the slot's game is finished (020823c8) the five rooms
    // open at 2500, 3000, 3500, 4000 and 4500 picarats (table 020f3724);
    // the passcode page is always there.
    static constexpr int kNeeded[5] = {2500, 3000, 3500, 4000, 4500};
    static constexpr int kX[6] = {4, 88, 172, 4, 88, 172}, kY[6] = {4, 4, 4, 88, 88, 88};
    const std::string path = "menu/secret/%s/omake_mode.spr";
    const bool finished = file_ ? file_->clearLevel(state_.saveSlot) != 0 : state_.cleared;
    for (int i = 0; i < 5; ++i) {
        if (finished && state_.progress.picarats() >= kNeeded[i]) {
            pages_[std::size_t(i)] = picture(path, kX[i], kY[i], 3, std::to_string(i + 1));
        }
    }
    pages_[5] = picture(path, kX[5], kY[5], 3, "6");
    back_ = kit_.button(kit_.localized("menu/secret/%s/secret_modoru.spr"), 0, 0, 4, "pos_o");
    digits_ = kit_.load("menu/secret/artmode_num.spr", 0, 0, 1);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/omake_mode");
    kit_.upperBackground = kit_.localized("menu/secret/%s/omake_mode_sub");
    if (auto s = kit_.sound()) {
        // Room sound 21; when its music is still changing, wait for it and
        // start BGM 36 (coming back from a room that played its own).
        s->roomSound(21, true);
        if (!s->bgmSettled()) {
            waitForMusic();
            s->playBgm(36, 1.0f, 0.0f);
        }
    }
    // 020f0ff0: characters 33, art 32, music 34, voices 35, movies 36,
    // passcode 40 (all with following scene 30); back to the secret menu.
    static constexpr std::uint8_t kScenes[6] = {33, 32, 34, 35, 36, scene::Passcode};
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        for (int i = 0; i < 6; ++i) {
            if (kit_.clicked(pages_[std::size_t(i)])) {
                return leave(kScenes[i], 30);
            }
        }
        if (kit_.clicked(back_)) {
            return leave(28);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 32: the art gallery.

void ArtGallery::draw() {
    // 020f15a8: buttons, then the picture number (020f1898): two digits from
    // x 18 leftwards in steps of 6 at y 7.
    live(previous_);
    live(next_);
    live(back_);
    unsigned value = unsigned(picture_ + 1);
    for (int i = 0, x = 18; i < 2; ++i, x -= 6) {
        kit_.select(digits_, std::to_string(value % 10));
        digits_.x = x;
        digits_.y = 7;
        kit_.draw(digits_);
        value /= 10;
    }
}

void ArtGallery::show() {
    // 020f16f4: the title (art_t_%u, centred at y 36) and the caption
    // (art_%u from (18, 64)) on the lower text layer; the picture is an
    // event background (ebg_%i, table 020f394c) on the upper screen.
    static constexpr int kPictures[10] = {2, 22, 59, 18, 45, 41, 38, 68, 104, 39};
    const auto n = std::to_string(picture_);
    kit_.upperBackground = "ebg/ebg_" + std::to_string(kPictures[picture_]);
    kit_.clearText(SceneKit::Lower);
    kit_.textTitle(36, kit_.text.expand(kit_.member("", "art_t_" + n + ".txt")), 0, 128,
                   SceneKit::Lower);
    kit_.textAt(18, 64, kit_.text.expand(kit_.member("", "art_" + n + ".txt")), SceneKit::Lower);
}

void ArtGallery::run() {
    // 020f12ec: previous (55, 145) and next (137, 145) with sound 120; back.
    const std::string path = "menu/secret/%s/artmode_btn.spr";
    previous_ = picture(path, 55, 145, 120, "mae");
    next_ = picture(path, 137, 145, 120, "next");
    back_ = kit_.button(kit_.localized("menu/secret/%s/modoru_btn.spr"), 0, 0, 4);
    digits_ = kit_.load("menu/secret/artmode_num.spr", 0, 0, 1);
    kit_.lowerBackground = "menu/secret/artmode_bg";
    if (auto s = kit_.sound()) {
        s->roomSound(23, true);
    }
    waitForMusic();
    // 020f15ec: ten pictures, wrapping around.
    kit_.poll();
    draw();
    show();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (kit_.clicked(previous_)) {
            if (--picture_ < 0) {
                picture_ = 9;
            }
            show();
        }
        if (kit_.clicked(next_)) {
            if (++picture_ >= 10) {
                picture_ = 0;
            }
            show();
        }
        if (kit_.clicked(back_)) {
            return leave(30);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 33: the character files.

void CharacterFiles::draw(int page) {
    // 020f1de8: the page buttons and back, then the page's fifteen files.
    live(previous_);
    live(next_);
    live(back_);
    for (int i = 0; i < 15; ++i) {
        live(files_[std::size_t((page - 1) * 15 + i)]);
    }
}

void CharacterFiles::show(int page, int file) {
    // 020f1f9c: unless already shown, the portrait (chr_sub%i on upper BG2,
    // colour 0 transparent, 0207090c), the name typed from (8, 15) and the
    // description in the question font from (8, 36), both on the upper
    // text layer (line step 12, set by the constructor).
    const int n = (page - 1) * 15 + file + 1;
    if (shown_ == n) {
        return;
    }
    kit_.upperOverlay = "menu/secret/chr_sub" + std::to_string(n);
    const auto index = std::to_string(n - 1);
    kit_.clearText(SceneKit::Upper);
    kit_.textTyped(8, 15, kit_.text.expand(kit_.member("", "name_" + index + ".txt")), -1);
    kit_.textQuestion(8, 36, kit_.text.expand(kit_.member("", "chr_" + index + ".txt")),
                      SceneKit::Upper);
    shown_ = n;
}

void CharacterFiles::run() {
    // 020f1944: five pages, six once all 153 puzzles are solved (02085a1c);
    // page buttons (pos_b / pos_n, sound 120), back; then the 90 file
    // buttons (chr_file<page>.spr, animation "<i + 1>", sound 3) at
    // (10 + 48 (i % 5), 35 + 43 (i / 5)). The display setup (BG2 for the
    // portrait, blending on the lower screen) is native composition.
    if (state_.allPuzzlesSolved(153)) {
        ++pages_;
    }
    const auto next = kit_.localized("menu/secret/%s/next_btn.spr");
    previous_ = kit_.button(next, 0, 0, 120, "pos_b");
    kit_.setButtonAnimations(previous_, "back2", "back1", "back1");
    next_ = kit_.button(next, 0, 0, 120, "pos_n");
    kit_.setButtonAnimations(next_, "next2", "next1", "next1");
    back_ = kit_.button(kit_.localized("menu/secret/%s/modoru_btn.spr"), 0, 0, 4);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/chrmode_bg");
    kit_.upperBackground = "menu/secret/chrmode_sub";
    if (auto s = kit_.sound()) {
        s->roomSound(22, true);
    }
    waitForMusic();
    for (int page = 0; page < 6; ++page) {
        const auto path = "menu/secret/chr_file" + std::to_string(page + 1) + ".spr";
        for (int i = 0; i < 15; ++i) {
            files_[std::size_t(page * 15 + i)] =
                picture(path, 10 + 48 * (i % 5), 35 + 43 * (i / 5), 3, std::to_string(i + 1));
        }
    }
    kit_.questionLineStep = 12; // text layer +0x51
    // 020f1e5c: page 1 with its first file shown.
    int page = 1;
    kit_.poll();
    draw(page);
    show(page, 0);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw(page);
        kit_.present();
        for (int i = 0; i < 15; ++i) {
            if (kit_.clicked(files_[std::size_t((page - 1) * 15 + i)])) {
                show(page, i);
            }
        }
        if (kit_.clicked(previous_) && --page < 1) {
            page = pages_;
        }
        if (kit_.clicked(next_) && ++page > pages_) {
            page = 1;
        }
        if (kit_.clicked(back_)) {
            return leave(30);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 34: the music room.

void MusicRoom::draw(bool active) {
    // 020f2418: the track buttons and back (updated only while active), the
    // track number once one was chosen.
    for (auto& t : tracks_) {
        if (active) {
            kit_.updateButton(t);
        }
        kit_.drawButton(t);
    }
    if (active) {
        kit_.updateButton(back_);
    }
    kit_.drawButton(back_);
    if (!idle_) {
        kit_.draw(number_);
    }
}

void MusicRoom::play(int track) {
    // 020f2550: everything stops; the first time the upper picture becomes
    // musicmode_sub; the track number (animation track + 1); three frames;
    // the title (music_%i, upper, centred at y 151); SE 480; 25 frames
    // without input; then the track (table 020f3b80): BGM, or the streamed
    // pieces 1 and 29..31.
    static constexpr int kTracks[19] = {1,  35, 4,  6,  23, 9,  11, 12, 7, 36,
                                        27, 14, 15, 25, 18, 29, 30, 31, 24};
    auto s = kit_.sound();
    if (s) {
        s->stopBgm(0.0f);
        s->stopStream(0.0f);
    }
    if (idle_) {
        kit_.upperBackground = "menu/secret/musicmode_sub";
        idle_ = false;
    }
    kit_.select(number_, unsigned(track + 1));
    for (int i = 0; i < 3; ++i) {
        kit_.poll();
        draw(true);
        kit_.present();
    }
    kit_.clearText(SceneKit::Upper);
    kit_.textTitle(151,
                   kit_.text.expand(kit_.member("", "music_" + std::to_string(track) + ".txt")), 0,
                   128, SceneKit::Upper);
    kit_.effect(480);
    for (int i = 0; i < 25; ++i) {
        kit_.poll();
        draw(false);
        kit_.present();
    }
    const int id = kTracks[track];
    if (!s) {
        return;
    }
    if (id == 1 || (id >= 29 && id <= 31)) {
        s->playStream(1, id, -1, 1.0f, 0.0f, false);
    } else {
        s->playBgm(id, 1.0f, 0.0f);
    }
}

void MusicRoom::run() {
    // 020f2134: 19 track buttons (no sound), 15 on a 5x3 grid from (10, 20)
    // stepping 48 / 38, the last four moved to (33 + 48 k, 134)
    // (02080184); back; the number (2D, (6, 6)).
    for (int i = 0; i < 19; ++i) {
        tracks_[std::size_t(i)] = picture("menu/secret/omake_music.spr", 10 + 48 * (i % 5),
                                          20 + 38 * (i / 5), -1, std::to_string(i + 1));
    }
    for (int k = 0; k < 4; ++k) {
        auto& t = tracks_[std::size_t(15 + k)];
        t.x = t.sprite.x = t.button.bounds.x = 33 + 48 * k;
        t.y = t.sprite.y = t.button.bounds.y = 134;
    }
    back_ = kit_.button(kit_.localized("menu/secret/?/modoru_btn.spr"), 0, 0, 4);
    number_ = kit_.load("menu/secret/omake_music_num.sbj", 6, 6, 1);
    number_.x = number_.y = 6;
    kit_.lowerBackground = kit_.localized("menu/secret/?/musicmode_bg");
    kit_.upperBackground = kit_.localized("menu/secret/?/omake_musicmode_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(24, true);
    }
    waitForMusic();
    // 020f2470.
    kit_.poll();
    draw(true);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw(true);
        kit_.present();
        for (int i = 0; i < 19; ++i) {
            if (kit_.clicked(tracks_[std::size_t(i)])) {
                play(i);
            }
        }
        if (kit_.clicked(back_)) {
            leave(30);
            break;
        }
    }
    // Destructor 020f2360: music and stream stop (time 0x514).
    if (auto s = kit_.sound()) {
        s->stopBgm(s->duration(0x514));
        s->stopStream(s->duration(0x514));
    }
}

// ---------------------------------------------------------------------------
// Scene 35: the voice room.

void VoiceRoom::play(int voice) {
    // 020f2b44: the stream stops (time 1012), then table 020f3cc4 (pairs):
    // a first value of -1 plays stream (route 0, id) whole; otherwise a
    // voice part (route 2, event, part) with the level meter.
    static constexpr int kVoices[30][2] = {
        {-1, 101}, {-1, 110}, {17, 1},  {21, 0},  {401, 0}, {-1, 105}, {-1, 115}, {132, 0},
        {200, 0},  {233, 0},  {231, 0}, {234, 1}, {279, 0}, {281, 0},  {294, 0},  {275, 0},
        {280, 1},  {338, 0},  {282, 0}, {284, 0}, {800, 0}, {900, 0},  {60, 1},   {83, 2},
        {88, 0},   {88, 1},   {166, 0}, {205, 1}, {378, 0}, {411, 1}};
    auto s = kit_.sound();
    if (!s) {
        return;
    }
    s->stopStream(s->duration(1012));
    const auto& v = kVoices[voice];
    if (v[0] == -1) {
        s->playStream(0, v[1], -1, 1.0f, 0.0f, false);
    } else {
        s->playStream(2, v[0], v[1], 1.0f, 0.0f, true);
    }
}

void VoiceRoom::run() {
    // 020f27a0: 30 icons (no sound) on a 6x5 grid from (13, 18) stepping
    // 39 / 31; back.
    for (int i = 0; i < 30; ++i) {
        voices_[std::size_t(i)] = picture("menu/secret/voice_icon.spr", 13 + 39 * (i % 6),
                                          18 + 31 * (i / 6), -1, std::to_string(i + 1));
    }
    back_ = kit_.button(kit_.localized("menu/secret/%s/modoru_btn.spr"), 0, 0, 4);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/voicemode_bg");
    kit_.upperBackground = kit_.localized("menu/secret/%s/voicemode_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(25, true);
    }
    waitForMusic();
    // 020f2a70; drawing 020f2a30.
    auto draw = [this] {
        for (auto& v : voices_) {
            live(v);
        }
        live(back_);
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        for (int i = 0; i < 30; ++i) {
            if (kit_.clicked(voices_[std::size_t(i)])) {
                play(i);
            }
        }
        if (kit_.clicked(back_)) {
            return leave(30);
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 36: the movie theatre.

void MovieTheatre::run() {
    // 020f2bdc: 23 icons (sound 3), 18 on a 6x3 grid from (3, 19) stepping
    // 42 / 38 and five at (24 + 42 k, 133); back.
    for (int i = 0; i < 23; ++i) {
        const int x = i < 18 ? 3 + 42 * (i % 6) : 24 + 42 * (i - 18);
        const int y = i < 18 ? 19 + 38 * (i / 6) : 133;
        movies_[std::size_t(i)] =
            picture("menu/secret/movie_icon.spr", x, y, 3, std::to_string(i + 1));
    }
    back_ = kit_.button(kit_.localized("menu/secret/%s/modoru_btn.spr"), 0, 0, 4);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/omake_moviemode");
    kit_.upperBackground = kit_.localized("menu/secret/%s/omake_moviemode_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(26, true);
    }
    waitForMusic();
    // 020f2f1c; a movie plays through its event (table 020f3e3c) with the
    // replay flag (+0x821c) set, so the movie scene returns here.
    static constexpr std::uint16_t kEvents[23] = {
        10010, 10040, 10110, 10130, 11000, 11300, 12000, 12250, 13060, 13080, 13100, 14020,
        16030, 16090, 17020, 17030, 17090, 17100, 17120, 17130, 17170, 17200, 17220};
    auto draw = [this] {
        for (auto& m : movies_) {
            live(m);
        }
        live(back_);
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        for (int i = 0; i < 23; ++i) {
            if (kit_.clicked(movies_[std::size_t(i)])) {
                kit_.fadeOut();
                state_.movieReplay = true;
                state_.eventId = kEvents[i];
                state_.scene = scene::DramaEvent;
                state_.following = 36;
                return;
            }
        }
        if (kit_.clicked(back_)) {
            kit_.fadeOut();
            state_.movieReplay = false;
            state_.scene = 30;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Scene 40: the passcode page.

void PasscodePage::run() {
    // 020f306c: coming back from an unlocked event (following scene 40),
    // the slot is saved first and the following scene becomes 28.
    if (state_.following == scene::Passcode) {
        FileDialog::quietSave(kit_, state_, file_);
        state_.following = 28;
    }
    // The two code buttons (36, 56) / (140, 56): "3" / "4" once unlocked
    // (progress +0x31a / +0x31b), else "1" / "2"; back.
    const std::string path = "menu/secret/%s/secretcode_btn.spr";
    first_ = picture(path, 36, 56, 3, state_.progress.bytes[0x31a] ? "3" : "1");
    second_ = picture(path, 140, 56, 3, state_.progress.bytes[0x31b] ? "4" : "2");
    back_ = kit_.button(kit_.localized("menu/secret/%s/modoru_btn.spr"), 0, 0, 4);
    kit_.lowerBackground = kit_.localized("menu/secret/%s/passcord_bg");
    kit_.upperBackground = kit_.localized("menu/secret/%s/passcord_sub");
    // The console's own code (seed 0x4e59), one character each 16 px from
    // (73, 155) on the upper text layer (02073324, spaces skipped).
    const auto code = consoleCode(state_.consoleMac, 0x4e59);
    kit_.clearText(SceneKit::Upper);
    for (int i = 0; i < 8; ++i) {
        kit_.textTyped(73 + 16 * i, 155, std::string(1, code[std::size_t(i)]), -1);
    }
    if (auto s = kit_.sound()) {
        s->roomSound(27, true);
    }
    waitForMusic();
    // 020f35b0: an unlocked code replays its event (26500 / 18440) and comes
    // back here; otherwise the code entry (41 / 42).
    auto draw = [this] {
        live(first_);
        live(second_);
        live(back_);
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        for (int k = 0; k < 2; ++k) {
            if (!kit_.clicked(k == 0 ? first_ : second_)) {
                continue;
            }
            kit_.fadeOut();
            if (!state_.progress.bytes[0x31a + std::size_t(k)]) {
                state_.scene = std::uint8_t(41 + k);
                return;
            }
            state_.eventId = k == 0 ? 26500 : 18440;
            state_.scene = scene::DramaEvent;
            state_.following = scene::Passcode;
            return;
        }
        if (kit_.clicked(back_)) {
            return leave(30);
        }
    }
}

// ---------------------------------------------------------------------------
// Scenes 41 / 42: entering a code.

void CodeEntry::clear() {
    position_ = 0;
    input_.fill(0);
}

void CodeEntry::type(char c) {
    // 020f02ac: the character goes to the cursor, which stops at the last place.
    input_[std::size_t(position_)] = c;
    if (position_ < 7) {
        ++position_;
    }
}

void CodeEntry::erase() {
    // 020f02cc: the character under the cursor goes; on an empty place the
    // cursor first steps back and removes the one before.
    if (input_[std::size_t(position_)] != 0) {
        input_[std::size_t(position_)] = 0;
        if (position_ > 0 && position_ < 7) {
            --position_;
        }
        return;
    }
    if (position_ > 0) {
        --position_;
    }
    input_[std::size_t(position_)] = 0;
}

void CodeEntry::keyboard() {
    // 020f046c: OK and erase, then the 36 keys; a held key gets a red
    // 19x19 highlight (alpha 15) and ends the pass, a clicked key types.
    live(ok_);
    live(erase_);
    if (kit_.clicked(erase_)) {
        erase();
    }
    for (int i = 0; i < 36; ++i) {
        auto& k = keys_[std::size_t(i)];
        live(k);
        if (k.button.state == Button::State::Armed && k.button.inside) {
            kit_.rectangle(k.x, k.y, 19, 19, kRed, 15);
            return;
        }
        if (kit_.clicked(k)) {
            if (std::size_t(i) < keyChars_.size()) {
                type(keyChars_[std::size_t(i)]);
            }
            return;
        }
    }
}

void CodeEntry::showInput() {
    // 020f056c: the typed characters (020f03c0: rectangle (69, 18) 140x12
    // cleared, one character each 16 px from (69, 18), lower layer), the
    // cursor (020f042c: animation 2 at (68 + 16 cursor, 29)), back.
    kit_.clearTextRect(69, 18, 140, 12);
    for (int i = 0; i < 8; ++i) {
        const char c = input_[std::size_t(i)];
        if (c != 0 && c != ' ') {
            kit_.textAt(69 + 16 * i, 18, std::string(1, c), SceneKit::Lower);
        }
    }
    cursor_.x = 68 + 16 * position_;
    cursor_.y = 29;
    kit_.select(cursor_, 2u);
    kit_.draw(cursor_);
    live(back_);
}

bool CodeEntry::wrong() {
    // 020f0160: compare with the scene's code (41: seed 0x4e4b, 42: 0x4b55);
    // on a mismatch "wrong code" stays until the touch is released, then the
    // input is cleared.
    const auto code = consoleCode(state_.consoleMac, state_.scene == 41 ? 0x4e4b : 0x4b55);
    if (std::equal(code.begin(), code.end(), input_.begin())) {
        return false;
    }
    do {
        kit_.poll();
        showInput();
        live(ok_);
        live(erase_);
        kit_.draw(badCode_);
        kit_.present();
    } while (!kit_.released());
    clear();
    return true;
}

void CodeEntry::run() {
    // ov11 020efda0: 36 spriteless 19x19 keys (sound 41) in four staggered
    // rows of 10, 10, 9 and 7 from (13, 52), (23, 73), (33, 94), (43, 115)
    // every 20 px; OK (kettei, no sound), erase (kesu, sound 4), back
    // (secret_modoru "pos_o"); the cursor; "wrong code" centred at y 59;
    // the key characters (secret.txt).
    static constexpr int kRows[4] = {10, 10, 9, 7};
    int index = 0;
    for (int row = 0; row < 4; ++row) {
        for (int i = 0; i < kRows[row]; ++i, ++index) {
            auto& k = keys_[std::size_t(index)];
            k.exists = true;
            k.x = 13 + 10 * row + 20 * i;
            k.y = 52 + 21 * row;
            k.button = Button({k.x, k.y, 19, 19}, 41);
        }
    }
    ok_ = kit_.button(kit_.localized("name/?/kettei.spr"), 0, 0, -1);
    erase_ = kit_.button(kit_.localized("name/?/kesu.spr"), 0, 0, 4);
    back_ = kit_.button(kit_.localized("menu/secret/%s/secret_modoru.spr"), 0, 0, 4, "pos_o");
    cursor_ = kit_.load("name/cursor.spr", 0, 0, 1);
    badCode_ = kit_.load(kit_.localized("name/?/bad_code.spr"), 0, 0, 1);
    badCode_.x = (256 - kit_.frameWidth(badCode_)) / 2;
    badCode_.y = 59;
    keyChars_ = kit_.member("input", "secret.txt").substr(0, 36);
    clear();
    kit_.lowerBackground = "name/codeinput_bg";
    kit_.upperBackground = kit_.localized(state_.scene == 41 ? "menu/secret/%s/passcord_sub2"
                                                             : "menu/secret/%s/passcord_sub3");
    if (auto s = kit_.sound()) {
        s->roomSound(27, true);
    }
    // 020f0594.
    kit_.poll();
    keyboard();
    showInput();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        keyboard();
        showInput();
        kit_.present();
        if (kit_.clicked(ok_)) {
            if (input_[0] == 0) {
                kit_.effect(4); // nothing typed
            } else {
                kit_.effect(111);
                if (!wrong()) {
                    // The right code: its flag (progress +0x31a / +0x31b) and
                    // event (26500 / 18440), then back to the passcode page.
                    kit_.fadeOut();
                    const bool first = state_.scene == 41;
                    state_.progress.bytes[first ? 0x31a : 0x31b] = 1;
                    state_.eventId = first ? 26500 : 18440;
                    state_.scene = scene::DramaEvent;
                    state_.following = scene::Passcode;
                    return;
                }
            }
        }
        if (kit_.clicked(back_)) {
            return leave(scene::Passcode);
        }
    }
}
} // namespace picarat
