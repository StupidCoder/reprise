#include "tower_of_babel_amiga/front/play.hpp"
#include "tower_of_babel_amiga/view/overlay.hpp"

namespace babel::front {
namespace {

constexpr std::uint32_t click_areas = 0x31DE;   // 27 x (x0, y0, x1, y1), low-res pixels
constexpr std::uint32_t panel_images = 0x303E;  // per image: offset in the panel set, width (words), modulo, rows
constexpr std::uint32_t panel_places = 0x3008;  // per button: byte offset in a bitplane
constexpr std::uint32_t view_distances = 0x9F7C; // $FBA by option $111A
constexpr std::uint32_t sound_no_effect = var(0x31AA); // a shot that does nothing
constexpr std::uint32_t sound_time_low = var(0x314E), sound_time_out = var(0x317C); // under 30 / 10 seconds
constexpr std::uint32_t default_name = var(0x52BD); // a tower without a name
constexpr view::Window view_window{16, 303, 16, 127}; // $F9E-$FA4
constexpr int panel_top = 0x84;                  // below this line a click is in the view

} // namespace

Play::Play(const data::Assets& assets, Screen& screen, const data::Tower& tower, const Settings& settings, int game_type)
    : assets_(assets), screen_(screen), tower_(tower), settings_(settings), game_type_(game_type), rules_(assets.program),
      state_(game::TowerState::unpack(tower, assets.program)), pens_(view::read_pens(assets.program)),
      sky_settings_(view::sky_settings(tower)) {
    panel_.reset(state_);
    strip_ = state_.programs[std::size_t(state_.active)];
    apply_view_settings();
}

void Play::apply_view_settings() {
    view_offset_ = (settings_.view_side - 1) * 0x30;
    view_height_ = settings_.view_height * 0x30 + 0x58;
    view_distance_ = std::int16_t(assets_.program.u16(view_distances + std::uint32_t(settings_.view_distance) * 2));
    state_.backwards_allowed = settings_.backwards_allowed;
}

void Play::draw_screen() {
    screen_.clear();
    screen_.copper = CopperMode::game;
    screen_.sky_type = tower_.settings[3];
    // $AC44: every button in its state.
    strip_ = state_.programs[std::size_t(state_.active)];
    draw_panel();
    screen_.frame(15, 15, 304, 128, 15);
    screen_.title_bar();
    draw_title();
    draw_view({}, !running_);
}

void Play::draw_title() {
    screen_.cursor.y = 2;
    screen_.cursor.paper = 0;
    screen_.cursor.ink = 15;
    const std::string name = tower_.name.empty() ? assets_.program.text(default_name) : tower_.name;
    screen_.centre(name);
    screen_.text(name);
}

int Play::survivors() const {
    int n = 0;
    for (int s = 0; s < 3; ++s)
        if (tower_.spider_available(s) && state_.objects[std::size_t(4 + s)].type == 0) ++n;
    return n;
}

int Play::button_at(int x, int y) const {
    for (int i = 0; i < game::buttons; ++i) {
        const std::uint32_t a = click_areas + std::uint32_t(i) * 8;
        const int x0 = assets_.program.u16(a), y0 = assets_.program.u16(a + 2);
        const int x1 = assets_.program.u16(a + 4), y1 = assets_.program.u16(a + 6);
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return i;
    }
    return -1;
}

Play::Outcome Play::frame(const Pointer& pointer) {
    sounds_.clear();
    if (!running_) {
        if (--state_.blink < 0) state_.blink = 6;
        one_frame(pointer, true);
        if (panel_.pause_requested()) return Outcome::paused;
        if (state_.started) {
            ++state_.clock; // $99BE: the clock runs from now
            running_ = true;
        }
        return Outcome::playing;
    }
    ++state_.frame;
    if (--state_.blink < 0) {
        state_.blink = 6;
        // A second of the timer: warnings under 30 and 10, the end at 0.
        if (state_.time_left != 0 && state_.end_countdown == 0) {
            if (state_.time_left < 0x30) sounds_.push_back(state_.time_left < 0x10 ? sound_time_out : sound_time_low);
            int t = state_.time_left;
            // One BCD second off.
            for (int digit = 0; digit < 4; ++digit) {
                const int shift = digit * 4;
                const int d = t >> shift & 15;
                if (d > 0) {
                    t -= 1 << shift;
                    break;
                }
                t |= 9 << shift; // borrow: this digit becomes 9
            }
            state_.time_left = t & 0xFFFF;
            if (state_.time_left == 0 && state_.end_countdown == 0) {
                state_.end_countdown = state_.blink + 0x15;
                state_.end_reason = 2;
            }
        }
    }
    one_frame(pointer, false);
    if (panel_.pause_requested()) return Outcome::paused;
    if (state_.end_countdown == 0 || --state_.end_countdown != 0) return Outcome::playing;
    return Outcome::ended;
}

void Play::one_frame(const Pointer& pointer, bool ready) {
    // $AB44: pens 23 and 24 change every frame (blinking objects).
    screen_.invert_blink_pens();
    for (int n = 23; n <= 24; ++n)
        for (auto& row : pens_.object[std::size_t(n)].rows)
            for (auto& c : row) c = std::uint8_t(15 - c);

    game::Panel::Input input;
    if (pointer.left) {
        if (pointer.y >= panel_top) {
            input.button = button_at(pointer.x, pointer.y);
        } else if (pointer.x >= view_window.left && pointer.x < view_window.right && pointer.y >= view_window.top &&
                   pointer.y < view_window.bottom) {
            input.view_click = true;
        }
    }
    state_.backwards_allowed = settings_.backwards_allowed;
    panel_.poll(state_, rules_, {settings_.link_cameras, tower_.flags[4]}, input, picked_);
    if (panel_.fire_failed()) sounds_.push_back(sound_no_effect);
    rules_.step(state_);
    rules_.update(state_);
    draw_view(pointer, ready);
    draw_panel();
    for (const int s : state_.sounds) sounds_.push_back(std::uint32_t(s));
    state_.sounds.clear();
    even_frame_ = !even_frame_;
}

std::array<data::Colour, 48> Play::target_palette() const {
    return view::tower_palette(tower_, assets_.program, settings_.panel_style);
}

void Play::start_demo(int index) {
    demo_ = index;
    demo_step_ = 0;
    demo_byte_ = 0;
    demo_height_ = 0x120;
    state_.started = true; // $158D6: running from the first frame
    state_.clock = 0;
    running_ = true;
}

void Play::draw_demo_screen() {
    screen_.clear();
    screen_.copper = CopperMode::game;
    screen_.sky_type = tower_.settings[3];
    screen_.frame(15, 15, 304, 128, 14);
}

void Play::demo_frame() {
    sounds_.clear();
    ++state_.frame;
    if (--state_.blink < 0) state_.blink = 6;
    screen_.invert_blink_pens();
    for (int n = 23; n <= 24; ++n)
        for (auto& row : pens_.object[std::size_t(n)].rows)
            for (auto& c : row) c = std::uint8_t(15 - c);
    panel_.clear_command_waits();
    auto& spider = state_.objects[std::size_t(state_.active)];
    if (spider.progress == 0) {
        const int b = assets_.program.u8(var(0xFE4) + std::uint32_t(demo_ * 16 + demo_step_));
        demo_byte_ = b;
        if (b != 0) {
            ++demo_step_;
            const int command = b & 7;
            if (command == 6) { // a jump on the spot
                spider.progress = 16;
                spider.state = 12;
            } else if (command != 0) {
                panel_.press(state_, rules_, {settings_.link_cameras, tower_.flags[4]}, game::forward + command - 1);
            }
        }
    }
    rules_.step(state_);
    rules_.update(state_);
    // The camera: over the spider (x 0.8, y 1.25 times its position), rising
    // or sinking by the script byte's top two bits each frame, and as far
    // behind it as it is high; always facing north.
    const auto& o = state_.objects[std::size_t(state_.active)];
    demo_height_ = std::int16_t(demo_height_ + (std::int8_t(demo_byte_) >> 6) * 2);
    const view::Camera camera{std::int16_t((o.x * 0xCD) >> 8), std::int16_t(((o.y * 0x140) >> 8) + demo_height_),
                              std::int16_t(o.z - demo_height_ * 8), 0};
    draw_view({}, false, &camera);
    for (const int s : state_.sounds) sounds_.push_back(std::uint32_t(s));
    state_.sounds.clear();
    even_frame_ = !even_frame_;
}

void Play::draw_view(const Pointer& pointer, bool ready, const view::Camera* fixed) {
    const auto& t = state_;
    const auto& active = t.objects[std::size_t(t.active)];
    if (!fixed && ((t.active < 4 && tower_.flags[4]) || (t.active >= 4 && active.type >= 0x1A))) {
        draw_noise();
        return;
    }
    const auto camera = fixed ? *fixed
                        : t.active >= 4
                            ? view::spider_camera(active.x, active.y, active.z, active.heading, view_offset_, view_height_, view_distance_)
                            : view::spider_camera(active.x, active.y, active.z, active.heading, 0, 0, 0);
    view::SkyEffects::advance(sky_, sky_settings_, camera.heading, even_frame_);
    screen_.flash = sky_.flash;

    // The objects as the view draws them.
    std::vector<view::SceneObject> objects(game::TowerState::objects_max);
    for (int n = 0; n < game::TowerState::objects_max; ++n) {
        const auto& o = t.objects[std::size_t(n)];
        auto& s = objects[std::size_t(n)];
        s.slot = n;
        s.type = o.type;
        s.x = o.x;
        s.y = o.y;
        s.z = o.z;
        s.moving = o.progress != 0;
        s.particle_count = o.counter_1a;
        s.second_cloud = (o.flags & 2) != 0;
        if (o.type >= 0 && o.type < 0x1A && assets_.main.contains(o.shape, 2)) {
            auto it = shapes_.find(o.shape);
            if (it == shapes_.end()) {
                try {
                    it = shapes_.emplace(o.shape, data::decode_shape(assets_.main, o.shape)).first;
                } catch (const std::exception&) {
                    it = shapes_.end();
                }
            }
            if (it != shapes_.end()) s.shape = &it->second;
        }
    }
    const view::ObjectAt object_at = [&](int f, int x, int z) -> const view::SceneObject* {
        const int n = t.object_at(game::cell_index(f, x, z));
        return n < 0 || n >= int(objects.size()) ? nullptr : &objects[std::size_t(n)];
    };
    const view::LiftHeight lift_height = [&](int f, int x, int z) -> std::optional<int> {
        const int n = t.object_at(game::cell_index(f, x, z));
        if (n < 0) return std::nullopt;
        return t.objects[std::size_t(n)].y;
    };
    const auto draw_sky = [&](view::Canvas& c) {
        view::draw_sky(c, sky_settings_, sky_, camera.heading, assets_.program, assets_.main, pens_.object);
    };
    view::Pick pick{pointer.x, pointer.y};
    const bool picking = panel_.pick_requested();
    const auto map = t.map();
    view::TowerView(pens_, assets_.program, assets_.main)
        .draw(screen_.canvas(), view_window, 160, 72, map, camera, lift_height, object_at, draw_sky, picking ? &pick : nullptr);
    if (picking) picked_ = pick.floor >= 0 ? t.object_at(game::cell_index(pick.floor, pick.cell_x, pick.cell_z)) : -1;

    if (game_type_ < 4) { // $D7EC: no overlays in demonstrations
        view::OverlayState o;
        o.ready = ready;
        o.blink = t.blink;
        o.info = settings_.info == 0 ? view::Corner::off : settings_.info == 1 ? view::Corner::bottom : view::Corner::top;
        o.timer = settings_.timer == 0 ? view::Corner::off : settings_.timer == 2 ? view::Corner::top : view::Corner::bottom;
        o.sights = settings_.sights;
        o.spider = t.active - 4;
        o.heading = camera.heading;
        o.klondikes = t.klondikes;
        o.klondikes_needed = t.klondikes_needed;
        o.destroyed = t.destroyed;
        o.destroy_needed = t.destroy_needed;
        o.time_limit = tower_.has_time_limit();
        o.time_left = t.time_left;
        view::draw_overlay(screen_.canvas(), o, assets_.program, assets_.main);
    }
}

void Play::draw_noise() {
    // $D804: the view black, then white dots: x steps on by the third
    // counter's low byte, y from a rotating sum; more dots while the panel's
    // choosing lockout runs.
    for (int y = 16; y < 128; ++y)
        for (int x = 16; x < 304; ++x) screen_.canvas().set(x, y, 0);
    int x = noise_x_;
    std::uint32_t d7 = std::uint16_t(noise_step_);
    int d6 = noise_y_;
    for (int d4 = (panel_.select_wait() + 2) * 0x360; d4 >= 0; --d4) {
        x += (int(d7) & 0xFF) + 3;
        if (x >= 0x120) x -= 0x120;
        d7 = (d7 >> 16) | (d7 << 16);
        d7 += 0x1F81;
        d7 = (d7 << 3) | (d7 >> 29);
        d6 = std::uint16_t((d6 + 1) ^ d4);
        const int y = (int(d7 & 0xFFFF) + d6) & 0x7F;
        d7 = (d7 >> 16) | (d7 << 16);
        const std::uint16_t low = std::uint16_t(d7);
        d7 = (d7 & 0xFFFF0000u) | std::uint16_t((low >> 3) | (low << 13));
        if (y < 0x70) screen_.canvas().set(x + 16, y + 16, 15);
    }
    noise_x_ += 0x8F;
    if (noise_x_ >= 0x120) noise_x_ -= 0x120;
    noise_y_ = std::uint16_t(noise_y_ + 0x3D);
    noise_step_ = std::uint16_t(noise_step_ + 0xB);
}

void Play::draw_panel() {
    const auto& t = state_;
    if (t.panel.program_record >= 0) {
        strip_ = t.programs[std::size_t(t.panel.program_record)];
        state_.panel.program_record = -1;
    }
    const auto& set = assets_.panels.at(std::size_t(settings_.panel_style));
    const auto blit = [&](int button, int image) {
        const std::uint32_t entry = panel_images + std::uint32_t(image) * 8;
        const std::uint32_t offset = assets_.program.u16(entry);
        const int words = assets_.program.u16(entry + 2), rows = assets_.program.u16(entry + 6);
        const int place = assets_.program.u16(panel_places + std::uint32_t(button) * 2);
        if (offset >= set.size()) return;
        screen_.copy_block(std::span<const std::uint8_t>(set).subspan(offset), (place % 40) * 8, place / 40, words, rows);
    };
    const int selected = panel_.selected_light();
    for (int b = 0; b < 7; ++b) blit(b, 2 * b + (b == selected ? 1 : 0));
    for (int b = 7; b < 9; ++b) blit(b, 2 * b + (t.panel.move == b - 7 ? 1 : 0));
    for (int b = 9; b < 13; ++b) blit(b, 2 * b + (t.panel.turn == b - 9 ? 1 : 0));
    for (int b = 13; b < 19; ++b) blit(b, 2 * b + (t.panel.program == b - 13 ? 1 : 0));
    for (int i = 0; i < 8; ++i) {
        const int command = std::int8_t(strip_.commands[std::size_t(i)]);
        blit(game::strip_first + i, (command + 19) * 2 + (i == strip_.position ? 1 : 0));
    }
}

} // namespace babel::front
