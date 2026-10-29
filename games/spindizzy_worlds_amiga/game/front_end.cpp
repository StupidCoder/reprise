#include "spindizzy_worlds_amiga/game/front_end.hpp"
#include "spindizzy_worlds_amiga/view/text.hpp"
#include <string>

namespace spindizzy::game {
namespace {

constexpr int page_frames = 0x1F4 + 1;   // $17738 / $17918: dbra over $1F4
constexpr std::uint32_t title_line = 0x17889; // "Copyright Paul Shirley 1990" ($17714)
// The attract pages in their order ($17756): text, block pictures, sprites.
struct Page { std::uint32_t text, blocks, sprites; };
constexpr Page pages[] = {
    {0x17942, 0, 0x17B10},       // $17904: GERALD
    {0x17312, 0x172DE, 0x17ABA}, // $1777C: hazards
    {0x177DF, 0, 0},             // $178AE: credits
    {0x173DC, 0x17300, 0x17B98}, // $1779C: switches
    {0x1736E, 0, 0x17B5A},       // $177B6
};
constexpr std::uint32_t map_palette = 0x11666;
constexpr std::uint32_t easy_cluster = 0x1195E, hard_cluster = 0x11876; // $11E3A: Easy has 9 worlds, Hard 23
constexpr std::uint32_t cluster_box = 0x11615;  // the box in the hidden buffer, bank 2
constexpr std::uint32_t cluster_labels = 0x1161E;
constexpr std::uint32_t world_names = 0x119EE;
constexpr int easy_thumbnail = 0x57A, hard_thumbnail = 0x58E;

void title_picture(view::Screen& s, const data::Assets& assets) {
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) s.put(x, y, assets.title.pixels[std::size_t(y * 320 + x)]);
}

} // namespace

void draw_statistics(view::Screen& s, const data::Assets& assets, const std::vector<WorldRecord>& done) {
    const auto& main = assets.main;
    view::TextPrinter text(main);
    if (done.empty()) {
        text.print(s, 0x16DB4);
        return;
    }
    text.print(s, 0x16DD3); // " You have completed "
    const int count = int(done.size());
    // $187F4: the count from the cursor on.
    const std::string digits = std::to_string(count);
    for (char c : digits) text.put(s, std::uint8_t(c));
    text.print(s, count == 1 ? 0x16DFD : 0x16DF2);
    // $16E56: the box around the list, its height by the count.
    const int height = count < 16 ? count * 9 + 2 : main.u16(0x16E2A);
    view::TextPrinter::box(s, main.u16(0x16E30), main.u16(0x16E2C), main.u16(0x16E2E) - 1, main.u16(0x16E28), height);
    text.column = main.u16(0x16E2C);
    text.row = main.u16(0x16E2E);
    const std::size_t first = done.size() > 16 ? done.size() - 16 : 0;
    for (std::size_t i = first; i < done.size(); ++i) {
        const auto& r = done[i];
        text.column = 2;
        text.print(s, 0x119EE + std::uint32_t(r.world & 31) * 13);             // $119D8
        text.column = 0x12;
        text.put(s, '%');                                                       // $18818
        text.column -= 2;
        text.number(s, r.fuel * 100 / 256);
        text.column = 0x14;
        for (char c : std::to_string(r.taken)) text.put(s, std::uint8_t(c));
        text.print(s, 0x16F06);                                                 // " of "
        for (char c : std::to_string(r.found)) text.put(s, std::uint8_t(c));
        text.print(s, 0x16F0B);                                                 // " jewels."
        text.put(s, 0x0D);
    }
    text.print(s, 0x16E07); // "Reserve fuel level"
    const int reserve = done.back().reserve;
    text.put(s, '%');
    text.column -= 2;
    text.number(s, reserve * 100 / 256);
}

void set_title_palette(view::Screen& s, const data::Assets& assets) {
    for (std::size_t i = 0; i < 16; ++i) s.palette[i] = assets.title.palette[i];
}

void set_map_palette(view::Screen& s, const data::Memory& main) {
    for (std::size_t i = 0; i < 16; ++i) s.palette[i] = data::Colour(main.u16(map_palette + std::uint32_t(i) * 2) * 2);
}

Attract::Attract(const data::Assets& assets) : assets_(assets) { start_page(); }

void Attract::start_page() {
    // $176FC: the title into the hidden buffer and its copyright line.
    picture_.fill(0);
    set_title_palette(picture_, assets_);
    title_picture(picture_, assets_);
    view::TextPrinter text(assets_.main);
    text.print(picture_, title_line);
    sprites_.clear();
    if (page_ >= 0) {
        const auto& page = pages[page_];
        text.print(picture_, page.text);
        if (page.blocks) // $1741C: (buffer offset, block word) pairs to a negative offset
            for (std::uint32_t at = page.blocks; std::int16_t(assets_.main.u16(at)) >= 0; at += 4)
                view::draw_block_picture(picture_, assets_, assets_.main.u16(at), assets_.main.u16(at + 2));
        if (page.sprites) sprites_ = view::read_attract_sprites(assets_.main, page.sprites);
    }
    frames_ = 0;
}

bool Attract::frame(std::uint8_t joystick) {
    if (joystick) return true; // $17C52 / $109E2: fire or a direction
    for (auto& sprite : sprites_) view::step_attract_sprite(picture_, assets_.main, sprite); // $14390
    if (++frames_ < page_frames) return false;
    page_ = page_ + 1 >= int(std::size(pages)) ? -1 : page_ + 1;
    start_page();
    return false;
}

void Attract::draw(view::Screen& s) const { s = picture_; }

Menu::Menu(const data::Assets& assets, const view::Screen& behind, std::uint32_t items, std::uint32_t header, int timeout)
    : assets_(assets), behind_(behind), items_(items), header_(header), timeout_(timeout) {
    for (std::uint32_t at = items_; assets_.main.u16(at) != 0; at += assets_.main.u16(at)) item_at_.push_back(at);
    view::TextPrinter text(assets_.main);
    if (header_) text.print(behind_, header_);
}

int Menu::frame(std::uint8_t joystick) {
    // $18086: the joystick byte is taken once; here a press is a change.
    const std::uint8_t pressed = joystick & ~last_;
    last_ = joystick;
    if (pressed & 0x80) return chosen_;
    if (pressed & 1) chosen_ = std::max(chosen_ - 1, 0);
    if (pressed & 2) chosen_ = std::min(chosen_ + 1, int(item_at_.size()) - 1);
    if (pressed & 0x0C) {
        const std::uint32_t next = item_at_[std::size_t(chosen_)] + assets_.main.u16(item_at_[std::size_t(chosen_)]);
        if (assets_.main.u16(next - 2) == 0x4000) slow_mode = !slow_mode; // a bit item: $17EA4
    }
    if (timeout_ > 0 && --timeout_ == 0) return -1;
    return -2;
}

void Menu::draw(view::Screen& s) const {
    s = behind_;
    view::TextPrinter text(assets_.main);
    text.put(s, 0x10);
    text.put(s, 0x13);
    int counter = counter_;
    for (std::size_t i = 0; i < item_at_.size(); ++i) {
        if (int(i) == chosen_ && (counter & 0x40)) text.put(s, 0x02); // $17FB6: orange
        text.print(s, item_at_[i] + 2);
        const std::uint32_t next = item_at_[i] + assets_.main.u16(item_at_[i]);
        if (assets_.main.u16(next - 2) == 0x4000) { // $17FF6: "Yes" / "No "
            const char* value = slow_mode ? "Yes" : "No ";
            for (const char* c = value; *c; ++c) text.put(s, std::uint8_t(*c));
        }
        text.put(s, 0x0D);
        text.put(s, 0x01);
        ++counter;
    }
    // $37B2 moves on with every item drawn and once more per pass.
    const_cast<Menu*>(this)->counter_ = counter + 1;
}

ClusterChoice::ClusterChoice(const data::Assets& assets, const view::ClusterView::Turn& turn) : assets_(assets) {
    // $11E52: the labels and their boxes first ($11E7A), then each cluster
    // drawn in the hidden buffer and copied small into its box.
    view::TextPrinter labels(assets_.main);
    labels.put(picture_, 0x10);
    labels.print(picture_, cluster_labels);
    view::Screen hidden;
    for (const auto& [list, offset] : {std::pair{easy_cluster, easy_thumbnail}, std::pair{hard_cluster, hard_thumbnail}}) {
        hidden.fill(0);
        view::TextPrinter text(assets_.main);
        text.print(hidden, cluster_box);
        const auto planets = view::read_cluster(assets_.main, list);
        view::ClusterView cluster;
        cluster.set_turn(turn);
        cluster.update(assets_.main, planets, std::vector<bool>(planets.size(), true));
        cluster.draw(hidden, assets_, assets_.main, planets);
        view::shrink_cluster(picture_, hidden, offset);
    }
    set_map_palette(picture_, assets_.main);
}

int ClusterChoice::frame(std::uint8_t joystick) {
    const bool fire = joystick & 0x80;
    const int held = (joystick & 4) ? 1 : (joystick & 8) ? 2 : 0;
    if (held != lit_) { lit_ = held; fire_down_ = false; return 0; }
    if (lit_ == 0) return 0;
    if (fire) fire_down_ = true;
    else if (fire_down_) return lit_; // released with the direction still held
    return 0;
}

void ClusterChoice::draw(view::Screen& s) const {
    s = picture_;
    if (lit_ == 0) return;
    // $11F3A: planes 2 and 3 of the thumbnail inverted (128 rows × 128 pixels).
    const int offset = lit_ == 1 ? easy_thumbnail : hard_thumbnail;
    const int x0 = (offset % 40) * 8, y0 = offset / 40;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            auto& p = s.pixels[std::size_t((y0 + y) * s.width + x0 + x)];
            p = std::uint8_t(p ^ 0x0C);
        }
}

WorldMap::WorldMap(const data::Assets& assets, bool easy, std::uint32_t worlds_left, const view::ClusterView::Turn& turn, int breaking)
    : assets_(assets), planets_(view::read_cluster(assets.main, easy ? easy_cluster : hard_cluster)), size_(easy ? 0x12 : 0x2E) {
    cluster_.set_turn(turn);
    // $16990: a world done ($24AE bit clear) has no planet; $119B4: the
    // one just done ($24AC) is made kind −7.
    for (std::size_t i = 0; i < planets_.size(); ++i) {
        const int world = planets_[i].world;
        shown_.push_back((worlds_left >> (world & 31)) & 1);
        if (world == breaking) {
            shown_.back() = false;
            cluster_.break_up(int(i));
        }
    }
    if (breaking >= 0) {
        break_frames_ = 0x40 - size_ + 1; // $11850
        cluster_.update(assets_.main, planets_, shown_, false);
    } else {
        chosen_ = cluster_.update(assets_.main, planets_, shown_);
    }
}

bool WorldMap::break_frame(std::uint8_t joystick, bool slow) {
    // $11858: $11686 (the planets, the break-up stepped as it is drawn),
    // the objects' frames ($143B6), the joystick ($168BE).
    cluster_.update(assets_.main, planets_, shown_, false);
    cluster_.step_break_up(assets_.main, 1 + (size_ >= 0x10) + (size_ >= 0x20), slow);
    cluster_.steer(joystick);
    return --break_frames_ <= 0;
}

int WorldMap::frame(std::uint8_t joystick) {
    // $11686 each frame: the planets placed, then the joystick ($168BE).
    const bool fire = joystick & 0x80;
    if (refused_) { // $109CC: until fire
        if (fire && released_) { refused_ = false; released_ = false; }
        if (!fire) released_ = true;
        return -1;
    }
    chosen_ = cluster_.update(assets_.main, planets_, shown_);
    cluster_.steer(joystick);
    if (!fire) { released_ = true; return -1; }
    if (!released_ || chosen_ < 0) return -1;
    return planets_[std::size_t(chosen_)].world;
}

Ending::Ending(const data::Assets& assets, const view::Screen& shown, bool easy)
    : assets_(assets), screen_(shown), easy_(easy) {
    view::TextPrinter text(assets_.main);
    text.print(screen_, easy ? 0x17D74 : 0x17DA7);
    effects_on_three();
}

void Ending::effects_on_three() {
    for (int channel = 0; channel < 3; ++channel) sounds.push_back({channel, 1, 0x40});
}

bool Ending::frame(std::uint8_t joystick) {
    constexpr int shown_frames = 0x96 + 1; // $17CD4: $10A1E waits d0 + 1 frames
    constexpr int zoom_frames = 0x2F + 1, last_frames = 8 + 1; // $17CEC, $17D3A
    constexpr std::uint32_t frame_effects = 0x17C62;
    const int n = frames_++;
    if (easy_) {
        if (n < shown_frames + 0x32 + 1) return false; // $17D52: 51 more
        const bool fire = joystick & 0x80;             // $109CC: then fire
        const bool pressed = fire && !fire_held_;
        fire_held_ = fire;
        return pressed;
    }
    if (n < shown_frames) return false;
    const int k = n - shown_frames;
    if (k == 0) effects_on_three(); // $17CE8
    if (k < zoom_frames) {
        // $17CF4: a channel for effect 1 this frame (bit 7: none).
        const int channel = assets_.main.u8(frame_effects + std::uint32_t(k));
        if (!(channel & 0x80)) sounds.push_back({channel & 3, 1, 0x40});
        view::draw_break_frame(screen_, assets_, picture_ + 4, 144, 84); // $16B62 with d0 + 3: at $D32
        picture_ = (picture_ + 1) & 7;
    } else if (k >= zoom_frames + last_frames) {
        return true;
    }
    // $17DFC: each byte of the centre (80-239, 50-149) through the doubling
    // table $40FC ($17DB8) into both rows of the other buffer, which is
    // then shown ($112E0).
    const view::Screen from = screen_;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            screen_.pixels[std::size_t(y * screen_.width + x)] = from.pixels[std::size_t((50 + y / 2) * from.width + 80 + x / 2)];
    return false;
}

void WorldMap::draw(view::Screen& s) const {
    s.fill(0);
    set_map_palette(s, assets_.main);
    view::TextPrinter text(assets_.main);
    text.print(s, cluster_box);
    cluster_.draw(s, assets_, assets_.main, planets_);
    if (chosen_ >= 0) { // $119CC: the name at column 14, row 8
        text.put(s, 0x10);
        text.column = 14;
        text.row = 8;
        text.print(s, world_names + std::uint32_t(planets_[std::size_t(chosen_)].world & 31) * 13);
    }
    if (refused_) text.print(s, 0x1180A);
}

} // namespace spindizzy::game
