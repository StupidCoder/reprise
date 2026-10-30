#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/data/preview.hpp"
#include <algorithm>
#include <stdexcept>

namespace beach_volley::front {
namespace {

// intro.code's table of pictures: records of $14 bytes (x, y, picture,
// mask buffer, screen address, width, height); the program sets x and y
// itself before drawing.
constexpr std::uint32_t picture_table = 0x23B5C;
constexpr std::uint32_t record_size = 0x14;
enum Record { small_ocean, beach, volley, present, credits, page_1, page_3, page_2, page_4, page_5, page_6, page_7,
              one_player, two_players };

constexpr std::uint32_t gold_blue = 0x24008, gold_red = 0x24028, gold_white = 0x24048, comic = 0x23FE8;

// The comic ($23332): where each page goes, in the order drawn.
constexpr struct {
    int x, y;
} page_places[7] = {{48, 0}, {0, 20}, {160, 10}, {30, 80}, {120, 20}, {80, 50}, {156, 130}};
constexpr int choice_y = 200;

// The voices: their address, length in words and period, as the comic
// writes them to channel 3 (pages 3-7, then with the menu).
constexpr struct {
    std::uint32_t address;
    std::uint16_t length, period;
} voice_list[6] = {{0x777EA, 0x8E1, 0x2F8}, {0x71782, 0xA61, 0x1AC}, {0x72C44, 0xBC1, 0x2D0},
                   {0x743C6, 0xD91, 0x328}, {0x75EE8, 0xC81, 0x1FC}, {0x6FFE0, 0xBD1, 0x5A0}};

// The titles' loop ($23186) draws a frame and waits for the third vertical
// blank since the last ($24068). (On the reference the drawing takes
// longer, 3.6 blanks a frame on average; the port keeps the program's pace,
// game.md "Intro".)
constexpr int blanks_per_step = 3;
// The comic's pause between pages: a delay loop (101 x 15,001 dbra,
// $23634) that took 146 vertical blanks on the reference, plus the page's
// drawing: 147 from page to page (observed: 146 and 148 in turn).
constexpr int comic_page_blanks = 147;
constexpr int comic_first_page = 4; // after the comic's flip (3 blanks) and its first blit

constexpr int clear_rows = 255;     // the screen's clear and the pictures stop above the last row

data::Picture table_picture(const data::IntroFiles& files, int record) {
    const std::uint32_t at = picture_table + std::uint32_t(record) * record_size;
    const std::uint32_t address = files.code.u32(at + 4);
    const int width = files.code.u16(at + 0x10), height = files.code.u16(at + 0x12);
    if (address < data::intro_block_address) throw std::runtime_error("The intro's pictures are not where expected.");
    const std::size_t offset = address - data::intro_block_address, size = std::size_t(width) * std::size_t(height) / 2;
    if (offset + size > files.block.size()) throw std::runtime_error("The intro's pictures are shorter than expected.");
    return data::planar_picture(std::span(files.block).subspan(offset, size), width, height, 4);
}

data::Palette read_palette(const data::CodeFile& code, std::uint32_t at) {
    data::Palette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) palette[i] = code.u16(at + std::uint32_t(i) * 2);
    return palette;
}

} // namespace

IntroArt decode_intro(const data::IntroFiles& files) {
    IntroArt art;
    art.ocean = data::planar_picture(files.block, 304, 83, 4);
    art.trophy = data::planar_picture(files.trophy, 224, 399, 4);
    // At the end of its scroll the trophy is copied from one row before its
    // first: plane 0 from the zeros before the picture, planes 1-3 from the
    // last row of the plane before. As colour indices: the last row's
    // colours shifted up one bit.
    art.above_trophy.resize(224);
    for (int x = 0; x < 224; ++x)
        art.above_trophy[std::size_t(x)] = std::uint8_t(art.trophy.pixels[398u * 224u + std::size_t(x)] << 1 & 0x0E);
    art.small_ocean = table_picture(files, small_ocean);
    art.beach = table_picture(files, beach);
    art.volley = table_picture(files, volley);
    art.present = table_picture(files, present);
    art.credits = table_picture(files, credits);
    const int pages[7] = {page_1, page_2, page_3, page_4, page_5, page_6, page_7};
    for (std::size_t i = 0; i < 7; ++i) art.pages[i] = table_picture(files, pages[i]);
    art.choices = {table_picture(files, one_player), table_picture(files, two_players)};
    art.gold_blue = read_palette(files.code, gold_blue);
    art.gold_red = read_palette(files.code, gold_red);
    art.gold_white = read_palette(files.code, gold_white);
    art.comic = read_palette(files.code, comic);
    for (std::size_t i = 0; i < 6; ++i) {
        const auto& v = voice_list[i];
        art.voices[i] = {v.address - data::intro_block_address, v.length, v.period};
        if (art.voices[i].offset + v.length * 2u > files.block.size())
            throw std::runtime_error("The intro's voices are shorter than expected.");
    }
    return art;
}

Intro::Intro(const IntroArt& art, std::uint32_t voices_at)
    : art_(&art), voices_at_(voices_at), shown_(std::size_t(width) * height), back_(std::size_t(width) * height) {
    // $23046: gold and blue above line $84 and between it and $FF, below that
    // gold and red.
    copper_.palettes = {art.gold_blue, art.gold_blue, art.gold_red};
    copper_.first_line = 0x84;
    copper_.second_line = 0xFF;
}

void Intro::tick(int players, amiga::PaulaChannels& paula) {
    if (phase_ == Phase::chosen) return;
    if (phase_ == Phase::comic) {
        comic_tick(paula);
        return;
    }
    // The titles: a step every third blank, after showing the last one.
    if (phase_ == Phase::titles) {
        if (wait_ > 0 && --wait_ > 0) return;
        if (steps_ > 0) std::swap(shown_, back_);
        wait_ = blanks_per_step;
    }
    // The keys are read at the top of the loop, and continually once the
    // menu is shown: F1 one player, F2 two ($23186, $232DC). The program
    // then turns off the screen and the sound and loads the match
    // (volley.code).
    if (players == 1 || players == 2) {
        players_ = players;
        phase_ = Phase::chosen;
        paula.write_dmacon(0x000F);
        return;
    }
    if (phase_ == Phase::menu) return;
    if (steps_ >= 1000) { // $2376A: the comic follows the titles' 1000th step
        phase_ = Phase::comic;
        comic_time_ = 0;
        comic_tick(paula);
        return;
    }
    titles_step();
}

// One step of the titles ($23186-$232AE).
void Intro::titles_step() {
    std::fill(back_.begin(), back_.begin() + clear_rows * width, std::uint8_t(0));
    const int s = ++steps_;

    // The trophy from step 120: copied from its last row down to the
    // screen's top, one more row every second step; once 255 rows show, it
    // scrolls one row a step until step 772 ($23776, $237CE).
    if (s >= 120) {
        const auto& t = art_->trophy;
        for (int row = 0; row < trophy_.rows; ++row) {
            const int from = trophy_.top + row;
            const std::uint8_t* source = from < 0 ? art_->above_trophy.data() : &t.pixels[std::size_t(from) * 224u];
            std::copy_n(source, 224, &back_[std::size_t(row) * width + 64]);
        }
        if (s < 773) {
            if (trophy_.rows < 255) {
                trophy_.half = !trophy_.half;
                if (!trophy_.half) {
                    ++trophy_.rows;
                    --trophy_.top;
                }
            } else {
                --trophy_.top;
            }
        }
    }
    // The Ocean logo rises out of line 150, a row a step, from step 20 to
    // step 410; its rows are copied whole (black around it), 7 pixels in
    // ($23812). "PRESENT" follows under it once it is complete.
    if (s >= 20 && s <= 410) {
        const auto& o = art_->ocean;
        const int top = 151 - ocean_rows_;
        for (int row = 0; row < ocean_rows_; ++row) {
            auto* line = &back_[std::size_t(top + row) * width];
            std::fill_n(line, width, std::uint8_t(0));
            std::copy_n(&o.pixels[std::size_t(row) * 304u], 304, line + 7);
        }
        ocean_rows_ = std::min(ocean_rows_ + 1, 83);
        if (ocean_rows_ >= 83) draw(back_, art_->present, 130, 160);
    }
    if (s == 412) copper_.palettes = {art_->gold_red, art_->gold_white, art_->gold_white};
    // The titles: the small Ocean logo comes down and "Volley" up into
    // place; "Beach" joins them from step 569 ($23650).
    if (s >= 406) {
        draw(back_, art_->small_ocean, 7, small_ocean_y_);
        draw(back_, art_->volley, 1, volley_y_);
        if (s >= 569) draw(back_, art_->beach, 102, beach_y_);
        if (s <= 599) {
            if (volley_y_ >= 95) --volley_y_;
            if (small_ocean_y_ < 46) ++small_ocean_y_;
        }
    }
    if (s == 601) copper_.palettes = {art_->gold_red, art_->gold_white, art_->gold_blue};
    // Then everything scrolls up with the credits, while the two palette
    // splits rise; at the 220th such step all three palettes are the same
    // ($2368A).
    if (s >= 600 && s < 990) {
        --small_ocean_y_;
        --beach_y_;
        --volley_y_;
        draw(back_, art_->credits, 96, credits_y_);
        --credits_y_;
        if (copper_.first_line >= 0x1A) --copper_.first_line;
        if (const int k = s - 599; k < 300) {
            if (k == 220) copper_.palettes = {art_->gold_blue, art_->gold_blue, art_->gold_blue};
            --copper_.second_line; // (an 8-bit line: it comes round past 0)
        }
    }
}

// The comic ($23332): a clean screen and the comic's colours, then a page
// every pause, the later ones with a voice on channel 3, and the menu.
void Intro::comic_tick(amiga::PaulaChannels& paula) {
    const int t = comic_time_++;
    if (t == 0) {
        std::fill(back_.begin(), back_.begin() + clear_rows * width, std::uint8_t(0));
        copper_.first_line = 0x12;
        copper_.second_line = 0x05;
        copper_.palettes = {art_->comic, art_->comic, art_->comic};
        return;
    }
    if (t == 3) {
        std::swap(shown_, back_); // and from here on it draws on the screen shown
        return;
    }
    if (t < comic_first_page || (t - comic_first_page) % comic_page_blanks != 0) return;
    const int page = (t - comic_first_page) / comic_page_blanks;
    if (page < 7) {
        draw(shown_, art_->pages[std::size_t(page)], page_places[page].x, page_places[page].y);
        if (page >= 2) play_voice(page - 2, paula);
        return;
    }
    play_voice(5, paula);
    draw(shown_, art_->choices[0], choice_areas[0][0], choice_y);
    draw(shown_, art_->choices[1], choice_areas[1][0], choice_y);
    phase_ = Phase::menu;
}

// $23942: a picture with colour 0 left out, clipped above the screen and
// at row 255. Rows run on in memory: a picture wider than the space to the
// screen's right edge continues at the left of the next row.
void Intro::draw(Screen& screen, const data::Picture& picture, int x, int y) const {
    if (y >= clear_rows) return;
    int skip = 0, rows = picture.height;
    if (y < 0) {
        if (y + picture.height <= 0) return;
        skip = -y;
        rows = picture.height + y;
        y = 0;
    } else if (y + rows >= clear_rows) {
        rows = clear_rows - y;
    }
    for (int row = 0; row < rows; ++row) {
        const auto* source = &picture.pixels[std::size_t(skip + row) * std::size_t(picture.width)];
        const std::size_t at = std::size_t(y + row) * width + std::size_t(x);
        for (int i = 0; i < picture.width; ++i) {
            if (source[i] != 0 && at + std::size_t(i) < screen.size()) screen[at + std::size_t(i)] = source[i];
        }
    }
}

// A voice on channel 3, played once ($233D6): as the music's samples, its
// length is set to one word after the start.
void Intro::play_voice(int voice, amiga::PaulaChannels& paula) const {
    const auto& v = art_->voices[std::size_t(voice)];
    paula.write_dmacon(0x0008);
    paula.set_location(3, voices_at_ + v.offset);
    paula.set_length(3, v.length);
    paula.set_period(3, v.period);
    paula.set_volume(3, 64);
    paula.write_dmacon(0x8008);
    paula.set_length(3, 1);
}

// The screen as the display shows it: display line 38 is the screen's
// first row ($23EDE); above the copper's first line the first palette,
// then the second, below the later of the two lines the third.
platform::Image Intro::picture() const {
    platform::Image image(width, height);
    if (phase_ == Phase::chosen) {
        std::fill(image.pixels.begin(), image.pixels.end(), platform::Color{0, 0, 0, 255});
        return image;
    }
    const int first = copper_.first_line, second = std::max<int>(copper_.first_line, copper_.second_line);
    for (int y = 0; y < height; ++y) {
        const int line = 38 + y;
        const auto& palette = copper_.palettes[line < first ? 0 : line < second ? 1 : 2];
        for (int x = 0; x < width; ++x)
            image.pixels[std::size_t(y) * width + std::size_t(x)] =
                data::to_color(palette[shown_[std::size_t(y) * width + std::size_t(x)] & 15]);
    }
    return image;
}

} // namespace beach_volley::front
