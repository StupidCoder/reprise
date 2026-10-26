// A live illustrated chapter. Range bands describe decision eligibility, not
// hitboxes; the decision record is sampled inside the reconstructed think rule.
#include "bruce_lee_c64/game/chapter.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include "ui/shelf.hpp"
#include <algorithm>
#include <cmath>
namespace bruce_lee::game {
namespace s = ui::shelf;
namespace {
bool hit(const platform::Controls &c, int x, int y, int w, int h) {
    return c.click && c.mouse_x >= x && c.mouse_x < x + w && c.mouse_y >= y && c.mouse_y < y + h;
}
const char *motion(const Actor &a) {
    switch (a.move) {
    case move::stand:
        return "Resting";
    case move::enemy_think:
        return "Deciding / walking";
    case move::enemy_walk:
        return "Repositioning";
    case move::chop:
        return "Chopping";
    case move::low_strike:
    case move::strike:
        return "Striking";
    case move::kick_left:
    case move::kick_right:
        return "Kicking";
    case move::fall:
        return "Falling";
    case move::climb_up:
    case move::climb_down:
        return "Climbing";
    default:
        return "Move in progress";
    }
}
} // namespace
EnemyChapter::EnemyChapter(const std::filesystem::path &content) : assets_(data::load_assets(content)) {
    reset();
}
void EnemyChapter::reset() {
    world_ = std::make_unique<World>(assets_);
    world_->new_game();
    world_->playing = true;
    world_->cheats = {true, true};
    world_->enter_room(0, 88, 198, false);
    auto &actors = world_->actors.slots;
    actors[0].x = 88;
    actors[0].y = 198;
    actors[1] = actors[0];
    actors[1].flags = 0x81;
    actors[1].x = 60;
    actors[2] = actors[0];
    actors[2].flags = 0x41;
    actors[2].x = 120;
    for (int i = 1; i < 3; ++i) {
        auto &a = actors[std::size_t(i)];
        a.sprite_codes = assets_.actor_sprites[std::size_t(a.kind())];
        for (auto code : {a.sprite_codes.first, a.sprite_codes.second})
            if (code)
                world_->sprite_owner[assets_.sprite_slot_of_code[code - 3]] = std::int8_t(i);
        a.move = move::stand;
        a.step = 0;
        a.delay = 0;
        a.d1 = 36;
        a.facing = i == 1 ? 1 : 0;
    }
    auto &selected = actors[std::size_t(enemy_)];
    if (scenario_ == 1)
        selected.x = std::uint8_t(88 + (enemy_ == 1 ? -8 : 8));
    if (scenario_ == 2)
        actors[0].y = 166;
    passes_ = 0;
    credit_ = 0;
    boundary_ = false;
    step({});
}
void EnemyChapter::step(const platform::Controls &c) {
    auto &b = world_->actors.slots[0];
    b.stick = c.up      ? (c.left    ? stick::up_left
                           : c.right ? stick::up_right
                                     : stick::up)
              : c.down  ? stick::down
              : c.left  ? stick::left
              : c.right ? stick::right
                        : stick::none;
    b.fire = 1; // A movement experiment: attacking the observers is deliberately
                // disabled.
    for (int i = 0; i < 3; ++i)
        world_->frame();
    world_->ninja_timer = 255;
    world_->yamo_timer = 255;
    const auto event = world_->pass();
    if (event.room_change || event.bruce_died || event.back_to_start) {
        paused_ = true;
        boundary_ = true;
    }
    c64::Collisions collisions;
    picture_ = world_frame(*world_).rasterise(&collisions).to_image(0, 0);
    world_->collision_latch |= collisions.sprite_sprite;
    ++passes_;
}
void EnemyChapter::update(const platform::Controls &c, double now) {
    const double elapsed = last_ < 0 ? 0 : std::clamp(now - last_, 0.0, 0.1);
    last_ = now;
    if (c.accept || hit(c, 24, 82, 100, 32)) {
        if (boundary_)
            reset();
        paused_ = !paused_;
    }
    if (hit(c, 132, 82, 100, 32)) {
        paused_ = true;
        step(c);
    }
    if (hit(c, 240, 82, 100, 32))
        reset();
    if (hit(c, 348, 82, 108, 32))
        slow_ = !slow_;
    if (hit(c, 464, 82, 120, 32)) {
        enemy_ = 3 - enemy_;
        reset();
    }
    for (int i = 0; i < 3; ++i)
        if (hit(c, 24 + i * 190, 126, 182, 30)) {
            scenario_ = i;
            paused_ = true;
            reset();
        }
    for (int i = 0; i < 4; ++i)
        if (hit(c, 620, 184 + i * 64, 312, 48))
            topic_ = i;
    if (!paused_) {
        credit_ += elapsed;
        const double interval = slow_ ? .24 : .06;
        while (credit_ >= interval) {
            credit_ -= interval;
            step(c);
            if (paused_)
                break;
        }
    } else
        credit_ = 0;
}
platform::Image EnemyChapter::draw() const {
    platform::Image im(960, 640);
    ui::fill(im, s::paper);
    s::button(im, "< Lessons", 24, 20, 104, 34);
    s::text(im, "Bruce Lee / Behind the decisions", 148, 22, 24, s::ink, true);
    s::text(im, "An experiment in Joyport's reconstruction", 148, 54, 14, s::muted);
    s::button(im, paused_ ? "Run" : "Pause", 24, 82, 100, 32, true);
    s::button(im, "Step", 132, 82, 100, 32);
    s::button(im, "Reset", 240, 82, 100, 32);
    s::button(im, slow_ ? "1/4 speed" : "Full speed", 348, 82, 108, 32);
    s::button(im, enemy_ == 2 ? "Ninja" : "Yamo", 464, 82, 120, 32, true);
    const char *presets[] = {"1  Approach", "2  Close encounter", "3  Height matters"};
    for (int i = 0; i < 3; ++i)
        s::button(im, presets[i], 24 + i * 190, 126, 182, 30, scenario_ == i);
    // An explicit transform shared by the game picture and all annotations.
    constexpr float zoom = 1.6f;
    constexpr int stageX = 40, stageY = 174;
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 512; ++x)
            im.put(stageX + x, stageY + y,
                   picture_.pixels[std::size_t(int(y / zoom) * picture_.width + int(x / zoom))]);
    const auto &a = world_->actors.slots[std::size_t(enemy_)];
    const auto &d = world_->actors.decisions[std::size_t(enemy_)];
    // VIC sprite origin maps to picture x=2*x-24, y=y+29-50.
    const int ax = stageX + int((2 * a.x - 24) * zoom), ay = stageY + int((a.y - 20) * zoom);
    const int sign = a.facing ? 1 : -1;
    const bool attacking = a.move == move::chop || a.move == move::low_strike || a.move == move::kick_left ||
                           a.move == move::kick_right;
    const auto tint = attacking ? s::red : s::cobalt;
    const int reach = int((enemy_ == 2 ? 20 : 36) * zoom);
    ui::blend_rect(im, ax + (sign > 0 ? 16 : -reach), ay - 24, reach - 14, 49, tint, 60);
    if (enemy_ == 1)
        ui::rect(im, ax + sign * 38, ay - 24, 1, 49, s::white);
    ui::rect(im, ax, ay, 2, 30, s::white);
    for (int slot = 1; slot < 3; ++slot) {
        const auto &actor = world_->actors.slots[std::size_t(slot)];
        if (!actor.present())
            continue;
        const int x = std::clamp(stageX + int((2 * actor.x - 24) * zoom) - 30, stageX, stageX + 350);
        const int y = std::clamp(stageY + int((actor.y - 20) * zoom) - 28, stageY, stageY + 280);
        ui::rounded(im, x, y, 156, 23, 4, slot == enemy_ ? s::cobalt : s::ink);
        s::clipped(im, std::string(slot == 2 ? "N: " : "Y: ") + motion(actor), x + 5, y + 5, 146, 11,
                   s::white, true);
    }
    s::text(im,
            boundary_ ? "Room boundary reached. Reset to repeat the experiment."
                      : "Arrows / WASD: move and jump. Enter: run / pause.",
            24, 506, 14);
    s::text(im,
            std::string(motion(a)) + "  |  Delay " + std::to_string(a.delay) + "  |  Steps " +
                std::to_string(a.step),
            24, 532, 15, s::ink, true);
    s::wrap(im,
            "Tint: facing attack-decision band. Red: attack underway, not a hit. "
            "White line: actor origin. The diagram holds the last sampled decision.",
            24, 558, 560, 2, 12);
    s::text(im, "LAST DECISION  /  click a gate", 620, 142, 16, s::ink, true);
    s::text(im, std::string("Result: ") + std::string(d.outcome), 620, 165, 12, s::cobalt, true);
    const char *gates[] = {"1  Is there ground support?", "2  Within 16 vertical pixels?",
                           "3  Facing the target?", "4  Which horizontal band?"};
    const bool active[] = {d.sampled, d.sampled && d.supported, d.sampled && d.supported && d.level,
                           d.sampled && d.supported && d.level && d.facing};
    const std::string values[] = {
        d.supported ? "Ground" : "Ladder / air", d.level ? "Same height band" : "Different height band",
        d.facing ? "Facing target" : "Facing away", std::to_string(d.horizontal) + " pixels apart"};
    for (int i = 0; i < 4; ++i) {
        ui::rounded(im, 620, 184 + i * 64, 312, 48, 5, active[i] ? s::pale : s::card);
        if (topic_ == i)
            ui::rect(im, 620, 184 + i * 64, 3, 48, s::cobalt);
        s::text(im, gates[i], 632, 191 + i * 64, 14, s::ink, true);
        s::text(im,
                !d.sampled  ? "Not sampled yet"
                : active[i] ? values[i]
                            : "Not reached this decision",
                632, 211 + i * 64, 12, s::muted);
        if (i < 3) {
            ui::rect(im, 774, 232 + i * 64, 2, 16, s::muted);
        }
    }
    const char *explanations[] = {"No ground: continue across climbable support or fall. Other moves and "
                                  "their delays can finish before another decision is made.",
                                  "At 16 pixels or more apart vertically, an enemy above the target walks "
                                  "on. One below turns toward the target and waits.",
                                  "Facing away: at 66 horizontal pixels or more, turn and walk. Closer "
                                  "than that, reposition toward a point beside the target.",
                                  "Horizontal gaps (screen pixels): Ninja strikes at 10-20. Yamo chops at "
                                  "10-22, kicks at 24-36. Below 10: reposition. Beyond reach: walk. Yamo "
                                  "will not chop on climbable support."};
    s::wrap(im, explanations[topic_], 620, 448, 312, 7, 14);
    s::text(im, "At decision: dx " + std::to_string(d.horizontal) + " / dy " + std::to_string(d.vertical),
            620, 566, 13, s::muted);
    ui::rect(im, 24, 594, 912, 1, s::line);
    s::wrap(im,
            "Reconstructed mechanics, not a proof of original accuracy. Practice "
            "setup: harmless enemies; no saves; silent. Labels describe moves, "
            "bands are not hitboxes.",
            24, 606, 912, 2, 12);
    return im;
}
} // namespace bruce_lee::game
