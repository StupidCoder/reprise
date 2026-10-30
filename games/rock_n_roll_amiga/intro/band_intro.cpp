#include "rock_n_roll_amiga/intro/band_intro.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include <algorithm>

namespace rocknroll::intro {
namespace {

// Where the band intro keeps what it shows, as it runs from $1087E.
constexpr std::uint32_t screen_at = 0x10938;     // the curtain ILBM's BODY, shown in place
constexpr std::uint32_t screen_end = 0x1ABDC;    // the copies reach a few lines below it (to the player)
constexpr std::uint32_t colour_map = 0x1087E + 0x30; // the ILBM's CMAP, as $7C8F2 reads it
constexpr std::uint32_t stage_at = 0x44D2E, credits_at = 0x5D176, frames_at = 0x4EA36;
constexpr std::uint32_t song_at = 0x1BE50, samples_at = 0x1EB08, periods_at = 0x1BDCA;
constexpr std::uint32_t song_number = 0x7C736;
constexpr std::uint32_t fellow_table = 0x7CD1A;  // 16 entries of 16 bytes, then the copy entry
constexpr std::uint32_t sign_table = 0x7CC0A;    // 17 entries
constexpr std::uint32_t copy_entry = 0x7CE1A;    // the entry the curtain rewrites
constexpr std::uint32_t pass_end_at = 0x7C446;
constexpr std::uint32_t second_sign = 0x59F76;
constexpr std::uint32_t wipe_at = 0x7C1F0;
constexpr std::uint32_t slots_at = 0x7C018;      // 11 x (position, end)
constexpr std::uint32_t ending_count_at = 0x7BC40;

// The program's own constants.
constexpr int pause_frames = 52;                 // $7C6E2: 51 waits with a delay, then one more
constexpr std::uint32_t line_bytes = 200;
constexpr int last_frame = 0x3E07;               // $7BC7C
constexpr int ending_frames = 0x12C;             // $7BCEC
// The credits stay through $7CEB6's delay (17 x 65536 DBRA passes) until the
// loader clears the screen: 85 frames, measured in FS-UAE.
constexpr int credits_frames = 85;

// $7C048: the order the animations are drawn in, one handler a turn; each
// names an animation (its descriptor: screen offset, height in plane rows,
// width in words, frame offsets) and its slot.
struct Animation {
    std::uint32_t descriptor;
    std::size_t slot;
};
constexpr std::array<Animation, 11> animations = {{
    {0x7CA26, 0}, {0x7CA52, 1}, {0x7CA7E, 2}, {0x7CAAA, 4}, {0x7CAD6, 5}, {0x7CB02, 6},
    {0x7CB2E, 7}, {0x7CB5A, 8}, {0x7CB86, 9}, {0x7CBB2, 10}, {0x7CBDE, 3},
}};

constexpr std::uint16_t stopped = 0xFFFF;

} // namespace

BandIntro::BandIntro(const data::Memory& intro) : intro_(&intro) {
    const auto screen = intro.bytes(screen_at, screen_end - screen_at);
    screen_.assign(screen.begin(), screen.end());
    const auto stage = intro.bytes(stage_at, screen_bytes);
    stage_picture_.assign(stage.begin(), stage.end());
    const auto credits = intro.bytes(credits_at, screen_bytes);
    credits_picture_.assign(credits.begin(), credits.end());
    for (std::size_t i = 0; i < colours_.size(); ++i) { // $7C8F2
        const std::uint32_t at = colour_map + std::uint32_t(i) * 3;
        colours_[i] = std::uint16_t(intro.u8(at) << 4 | intro.u8(at + 1) | intro.u8(at + 2) >> 4);
    }
    for (std::size_t s = 0; s < slots_.size(); ++s)
        slots_[s] = {intro.u16(slots_at + std::uint32_t(s) * 4), intro.u16(slots_at + std::uint32_t(s) * 4 + 2)};
    copy_to_ = intro.u32(copy_entry);
    copy_from_ = intro.u32(copy_entry + 4);
    pass_end_ = int(intro.u32(pass_end_at));
    sign_picture_ = intro.u32(sign_table + 4);
    wipe_at_ = intro.u32(wipe_at);
    ending_frames_ = int(intro.u32(ending_count_at));

    // $7C742/$7C7BC: the player set to the song; it starts at the first frame
    // the band's loop runs.
    std::vector<std::uint16_t> periods;
    for (std::uint32_t i = 0; i < 256; ++i) periods.push_back(intro.u16(periods_at + i * 2));
    player_.set_periods(periods);
    player_.set_sample_base(samples_at);
    const auto song = intro.bytes(song_at, samples_at - song_at);
    player_.init(std::vector<std::uint8_t>(song.begin(), song.end()));
    player_.start_song(int(intro.u32(song_number)));
    const auto samples = intro.bytes(samples_at, intro.end() - samples_at);
    sound_memory_.assign(samples.begin(), samples.end());
    paula_.set_memory(sound_memory_);
}

std::uint16_t BandIntro::word(std::uint32_t address) const {
    if (address >= screen_at && address + 1 < screen_at + screen_.size()) {
        const std::size_t at = address - screen_at;
        return std::uint16_t(screen_[at] << 8 | screen_[at + 1]);
    }
    return intro_->contains(address, 2) ? intro_->u16(address) : 0;
}

void BandIntro::set_word(std::uint32_t address, std::uint16_t value) {
    if (address < screen_at || address + 1 >= screen_at + screen_.size()) return;
    const std::size_t at = address - screen_at;
    screen_[at] = std::uint8_t(value >> 8);
    screen_[at + 1] = std::uint8_t(value);
}

// The blitter in area mode, ascending: per row, `width` words of A, B and C
// (A masked by the first/last word masks, A and B shifted right through the
// word before), combined by the minterm into D, then the modulos added.
void BandIntro::blit(const Blit& b) {
    const int rows = (b.size >> 6) != 0 ? b.size >> 6 : 1024;
    const int words = (b.size & 0x3F) != 0 ? b.size & 0x3F : 64;
    const int use = b.con0 >> 8 & 0xF, minterm = b.con0 & 0xFF;
    const int a_shift = b.con0 >> 12, b_shift = b.con1 >> 12;
    std::uint32_t a = b.a, src = b.b, c = b.c, d = b.d;
    for (int r = 0; r < rows; ++r) {
        std::uint16_t a_before = 0, b_before = 0;
        for (int w = 0; w < words; ++w) {
            std::uint16_t a_word = 0xFFFF, b_word = 0, c_word = 0;
            if (use & 8) a_word = word(a), a += 2;
            if (w == 0) a_word &= b.first_mask;
            if (w == words - 1) a_word &= b.last_mask;
            if (use & 4) b_word = word(src), src += 2;
            if (use & 2) c_word = word(c), c += 2;
            const auto as = std::uint16_t((std::uint32_t(a_before) << 16 | a_word) >> a_shift);
            const auto bs = std::uint16_t((std::uint32_t(b_before) << 16 | b_word) >> b_shift);
            a_before = a_word;
            b_before = b_word;
            std::uint16_t out = 0;
            for (int term = 0; term < 8; ++term) {
                if (!(minterm >> term & 1)) continue;
                out |= std::uint16_t((term & 4 ? as : ~as) & (term & 2 ? bs : ~bs) & (term & 1 ? c_word : ~c_word));
            }
            if (use & 1) set_word(d, out), d += 2;
        }
        if (use & 8) a += std::uint32_t(b.amod);
        if (use & 4) src += std::uint32_t(b.bmod);
        if (use & 2) c += std::uint32_t(b.cmod);
        if (use & 1) d += std::uint32_t(b.dmod);
    }
}

// $7C5E2: the blit an entry of a table gives (index / 4 picks the entry):
// D and C its screen address less the crossing's byte position, B its
// picture, A the mask $1900 bytes after it. It also sets the copy entry's
// source to the line above the sign's place.
void BandIntro::table_blit(std::uint32_t table, int index) {
    const std::uint32_t entry = table + (std::uint32_t(index << 2) & 0x0FFFFFF0u);
    const auto entry_long = [&](std::uint32_t at) {
        if (at == copy_entry) return copy_to_;
        if (at == copy_entry + 4) return copy_from_;
        if (table == sign_table && at == entry + 4) return sign_picture_;
        return intro_->u32(at);
    };
    blitter_.b = entry_long(entry + 4);
    blitter_.a = blitter_.b + 0x1900;
    blitter_.c = blitter_.d = entry_long(entry) - std::uint32_t(shift_);
    copy_from_ = 0x17848 - std::uint32_t(shift_);
    blitter_.con0 = intro_->u16(entry + 8);
    blitter_.con1 = blitter_.con0 & 0xF000;
    blitter_.first_mask = intro_->u16(entry + 12);
    blitter_.last_mask = intro_->u16(entry + 14);
    blitter_.size = entry == copy_entry ? std::uint16_t(0x5000 | copy_words_) : intro_->u16(entry + 10);
    blit(blitter_);
}

// $7C466-$7C52A: a pass of the crossing, up to the frame's wait. The fellow
// and the sign are cut in; then a copy smears the line above them down over
// 64 lines on the far side of the screen (the curtain's stripes are
// vertical), wiping what of them wrapped round the screen's edge.
void BandIntro::curtain_draw() {
    blitter_.amod = blitter_.bmod = blitter_.cmod = blitter_.dmod = 0x22;
    table_blit(fellow_table, pass_);
    blitter_.amod = blitter_.bmod = blitter_.cmod = blitter_.dmod = 0x1C;
    copy_to_kept_ = copy_to_;
    copy_from_kept_ = copy_from_;
    pass_kept_ = pass_;
    shift_kept_ = shift_;
    table_blit(sign_table, pass_);
    pass_ = 0x40;
    shift_ = shift_kept_ <= 0x12 ? 0x20 : 0;
    copy_from_ = 0x17848 - std::uint32_t(shift_);
    table_blit(fellow_table, pass_);
    drawn_ = true;
}

// $7C532-$7C5DE, after the wait: the same smear, 7 words wide, over the
// sign's own place takes fellow and sign off again; then the next pass, or
// (the crossing's 32 passes done) the next word to the left. A click ends
// the crossing.
void BandIntro::curtain_restore(bool click) {
    shift_ = shift_kept_;
    copy_from_ = copy_from_kept_;
    copy_to_ = copy_to_kept_;
    blitter_.bmod = blitter_.dmod = 0x1A;
    copy_words_ = 7;
    table_blit(fellow_table, pass_);
    copy_words_ = 6;
    blitter_.bmod = blitter_.dmod = 0x1C;
    pass_ = pass_kept_ + 2;
    if (click) {
        pass_ = pass_end_;
        shift_ = 0x28;
        sign_picture_ = second_sign;
        second_sign_ = true;
    }
    drawn_ = false;
}

void BandIntro::swap_line(std::uint32_t picture, std::uint32_t offset) {
    auto& other = picture == stage_at ? stage_picture_ : credits_picture_;
    std::swap_ranges(screen_.begin() + std::ptrdiff_t(offset), screen_.begin() + std::ptrdiff_t(offset + line_bytes),
                     other.begin() + std::ptrdiff_t(offset));
}

// $7BD14: the frame count and what starts or stops at it.
void BandIntro::step_timeline() {
    const int n = ++timeline_;
    auto start = [&](std::size_t s) { slots_[s].position = 0; };
    auto stop = [&](std::size_t s) { slots_[s].position = stopped; };
    if (n == 0x140) start(9), start(10);
    if (n == 0x26D) start(3), start(2);
    if (n == 0x51A) start(1);
    if (n == 0xC60) start(6);
    if (n == 0x644) start(4), slots_[4].end = 0x0C;
    if (n == 0x734) stop(4), slots_[4].end = 0x10;
    if (n == 0xDEA) start(5);
    if (n == 0x12BB) stop(5), start(4);
    if (n == 0x179A) start(5), stop(4);
    if (n == 0x18D6) start(5), start(4);
    if (n == 0x200) slots_[7].end = 0x20;
    switch (n & 0x7FF) {
    case 0x21E: slots_[7].end = 0x14; break;
    case 0x400: slots_[7].end = 0x20; break;
    case 0x41E: slots_[7].end = 0x14; break;
    case 0x700: slots_[7].end = 0x24; break;
    case 0x73C: slots_[7].end = 0x14; break;
    default: break;
    }
    if (n == last_frame) {
        for (std::size_t s : {5, 4, 6, 9, 10, 8, 3, 2, 1, 7}) stop(s);
        start(0);
    }
}

// $7C150/$7C184: the next animation in turn moves a frame on; at its end it
// rests a turn and starts again. A frame is a plain copy from the store.
void BandIntro::animate() {
    turn_ = (turn_ + 1) % int(animations.size());
    const Animation& anim = animations[std::size_t(turn_)];
    Slot& slot = slots_[anim.slot];
    if (slot.position == stopped) return;
    slot.position = std::uint16_t(slot.position + 4);
    if (slot.position >= slot.end) {
        slot.position = 0;
        return;
    }
    const std::uint32_t d = anim.descriptor;
    const std::uint32_t rows = intro_->u32(d + 4), words = intro_->u32(d + 8);
    Blit b;
    b.con0 = 0x05CC; // $7BFB4: B to D
    b.d = screen_at + intro_->u32(d);
    b.b = frames_at + intro_->u32(d + 12 + ((slot.position - 4u) & ~3u));
    b.bmod = b.dmod = std::int16_t(40 - 2 * int(words));
    b.size = std::uint16_t(rows << 6 | words);
    blit(b);
}

bool BandIntro::tick(bool click) {
    switch (stage_) {
    case Stage::curtain:
        if (drawn_) {
            curtain_restore(click);
            if (pass_ == pass_end_) {
                shift_ += 2;
                if (shift_ == 0x2A) { // $7C67E: the crossing is over
                    shift_ = 0;
                    stage_ = Stage::pause;
                    pause_left_ = pause_frames;
                    return false;
                }
                pass_ = 0;
            }
        }
        curtain_draw();
        return false;

    case Stage::pause:
        if (--pause_left_ > 0) return false;
        if (!second_sign_) { // $7C69C: the second sign crosses
            second_sign_ = true;
            sign_picture_ = second_sign;
            pass_ = 0;
            stage_ = Stage::curtain;
            curtain_draw();
            return false;
        }
        stage_ = Stage::wipe_in;
        [[fallthrough]];

    case Stage::wipe_in:
        // $7C384: a line a frame from the bottom; line 0 is never swapped.
        wipe_at_ -= line_bytes;
        swap_line(stage_at, wipe_at_);
        if (wipe_at_ == line_bytes) {
            wipe_at_ = 0;
            stage_ = Stage::band;
            music_frame(); // $7BC64
            timeline_ = 0;
            step_timeline();
            music_frame();
        }
        return false;

    case Stage::band:
        animate();
        animate();
        if (click) { // $7C796: the music fades out
            player_.fade_to(0, 0x10);
            stage_ = Stage::ending;
        } else {
            step_timeline();
            if (timeline_ == last_frame) stage_ = Stage::ending;
            else {
                music_frame();
                return false;
            }
        }
        step_timeline();
        music_frame();
        return false;

    case Stage::ending:
        animate();
        animate();
        if (++ending_frames_ == ending_frames) {
            player_.stop(paula_); // $7C7AC
            stage_ = Stage::wipe_out;
            wipe_at_ += line_bytes;
            music_frame();
            return false;
        }
        step_timeline();
        music_frame();
        return false;

    case Stage::wipe_out:
        // $7C3DA: the credits a line a frame from line 1 down.
        swap_line(credits_at, wipe_at_);
        wipe_at_ += line_bytes;
        if (wipe_at_ == screen_bytes) {
            stage_ = Stage::credits;
            credits_left_ = credits_frames;
            return false;
        }
        music_frame();
        return false;

    case Stage::credits:
        if (--credits_left_ <= 0) stage_ = Stage::done;
        return stage_ == Stage::done;

    case Stage::done:
        return true;
    }
    return false;
}

platform::Image BandIntro::picture() const {
    platform::Image image(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::size_t at = std::size_t(y) * line_bytes + std::size_t(x / 8);
            const int bit = 7 - (x & 7);
            int index = 0;
            for (int p = 0; p < 5; ++p) index |= ((screen_[at + std::size_t(p) * 40] >> bit) & 1) << p;
            image.pixels[std::size_t(y * width + x)] = data::to_color(colours_[std::size_t(index)]);
        }
    return image;
}

} // namespace rocknroll::intro
