#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <algorithm>

namespace spindizzy::view {
namespace {

// Per jewel type (the first parameter byte & $E, halved): rows ($14C24),
// and the offset of its animation list ($14C44); frames from $25AC0.
constexpr std::uint32_t jewel_frames = 0x25AC0;
constexpr std::uint32_t jewel_rows = 0x14C24;
constexpr std::uint32_t jewel_lists = 0x14C44;
constexpr std::uint32_t exit_frames = 0x2B6A0;     // $15016
constexpr std::uint32_t exit_list = 0x15140;       // $15138
constexpr std::uint32_t button_frames = 0x2C000;   // $1605C: $208 per type, pressed at +$104
constexpr std::uint32_t switch_frames = 0x2D040;   // $1618E: $DC per colour and facing, off at +$370
constexpr std::uint32_t lift_frames = 0x285E0;     // $14CE2: $140 per type
constexpr std::uint32_t tall_lift_frames = 0x2A7A0; // $14CEE: $1E0 per type
constexpr std::uint32_t lift_shaft = 0x280E0;      // $14FA6: the column below a platform
constexpr std::uint32_t head_frames = 0x28EA0;     // $1597E
constexpr std::uint32_t head_offsets = 0x159A4;    // per type × 4 + facing
constexpr std::uint32_t spark_frames = 0x276E0;    // $15C46; +$500 when the direction's bit 0 is clear
constexpr std::uint32_t spark_list = 0x15CBC;      // $15CB2
// Creatures (Pac aliens, pyramids, boinks) draw through one table ($146FC):
// per graphic set (+$2A & $1C), 24 bytes: draw mode, offset of a (dx, dy)
// pair (from $146F8) that the animation list follows, frame base, and a
// frame offset per heading.
constexpr std::uint32_t creature_sets = 0x146FC;
constexpr std::uint32_t creature_pairs = 0x146F8;

WorldPosition position(const world::PlacedObject& object) {
    return {object.x * 256, object.y * 256, object.z * 256}; // $127F8: cells × 256, no centring
}

std::uint8_t parameter(const world::PlacedObject& object, std::size_t i) {
    return i < object.parameters.size() ? object.parameters[i] : 0;
}

// A sprite place whose mask was made from map row `mask_top` (rounded down
// to its band, $14ABA asr #3) and is read from mask row `row` on.
SpritePlace banded(SpritePlace place, int mask_top, int row = 0) {
    place.mask_dy = (mask_top & ~7) + row - place.y;
    return place;
}

// The shared placement of buttons, switches, exits and sparks ($1607E →
// $14AAE): 16 pixels left, and 8 up when the record's word +$2C happens to
// be 2 (a test meant for jewels of type 2; kept).
SpritePlace placed(const SpritePlace& at, int word_2c) {
    SpritePlace place{at.x - 16, at.y, at.key};
    if (word_2c == 2) place.y -= 8;
    return banded(place, place.y);
}

// The mask row a lift or head starts reading at its height ($14F56,
// $15AD4: ($900 − z) / 16 bytes, even).
int mask_row(int z) {
    return ((((0x900 - z) & 0xFFFF) >> 4) & 0xFE) / 2;
}

// A frame with more rows appended below (the lift's shaft).
data::SpriteFrame stacked(data::SpriteFrame top, const data::SpriteFrame& below) {
    top.pixels.insert(top.pixels.end(), below.pixels.begin(), below.pixels.end());
    top.opaque.insert(top.opaque.end(), below.opaque.begin(), below.opaque.end());
    top.height += below.height;
    return top;
}

} // namespace

// An animation list ($10058): entries (ticks, frame offset); a negative
// tick count ends it and the step wraps to the first entry.
std::uint32_t animation_offset(const data::Memory& main, std::uint32_t list, int step) {
    int length = 0;
    while (std::int16_t(main.u16(list + std::uint32_t(length) * 4)) >= 0) ++length;
    const auto entry = std::uint32_t(step % std::max(length, 1));
    return main.u16(list + entry * 4 + 2);
}

std::uint32_t jewel_frame(const data::Memory& main, int type, int step) {
    const std::uint32_t list = jewel_lists + std::uint32_t(std::int16_t(main.u16(jewel_lists + std::uint32_t(type))));
    return jewel_frames + animation_offset(main, list, step);
}

namespace {

// A creature's sprite ($146D2 picks the frame, $1467E draws): modes 1
// (16 × 16) and 0 (16 × 12) work out their occlusion every frame like the
// explorer; mode −1 (32 × 24, $10124) keys one outer column nearer.
ObjectSprite creature(const data::Memory& main, int kind, int set_byte, int heading, const WorldPosition& where,
                      int step, int order_offset) {
    const std::uint32_t set = creature_sets + std::uint32_t((set_byte & 0x1C) / 4) * 24;
    const int mode = std::int16_t(main.u16(set));
    const std::uint32_t pair = creature_pairs + std::uint32_t(std::int16_t(main.u16(set + 2)));
    const int dx = std::int16_t(main.u16(pair)), dy = std::int16_t(main.u16(pair + 2));
    const std::uint32_t frame = main.u32(set + 4) + main.u16(set + 8 + std::uint32_t(heading & 3) * 2) +
                                animation_offset(main, pair + 4, step);
    const auto at = project(where);
    SpritePlace place{at.x + dx, at.y + dy, at.key};
    data::SpriteFrame image;
    if (mode < 0) {
        image = data::read_sprite_frame(main, frame, 2, 24);
        if (heading & 1) place.column_key[0] = -0x200;
        else place.column_key[2] = -0x200;
        // Their mask is made per map column and then ANDed with the frame
        // before the frame is shifted into place ($101FE): the occlusion
        // lands x & 15 pixels left of the pixels it belongs to. Kept.
        place.mask_dx = -(place.x & 15);
    } else {
        image = data::read_sprite_frame(main, frame, 1, mode > 0 ? 16 : 12);
    }
    return {kind, std::move(image), place, sort_value(where) + order_offset};
}

} // namespace

std::vector<ObjectSprite> start_sprites(const world::Level& level, const world::Switches& switches,
                                        const data::Memory& main, const std::function<ObjectState(std::size_t)>& state) {
    std::vector<ObjectSprite> sprites;
    for (std::size_t index = 0; index < level.objects.size(); ++index) {
        const auto& object = level.objects[index];
        const ObjectState now = state ? state(index) : ObjectState{};
        auto where = position(object);
        if (now.z >= 0) where.z = now.z;
        const auto at = project(where);
        const int order = sort_value(where);
        switch (world::ObjectKind(object.kind)) {
        case world::ObjectKind::jewel: { // $14A18
            const int type = parameter(object, 0) & 0x0E;
            const int rows = std::int16_t(main.u16(jewel_rows + std::uint32_t(type))) + 1;
            SpritePlace place{at.x - 16, at.y, at.key};
            if (type == 4 || type == 14) place.y -= 16;  // the tall ones
            else if (type == 2) place.y -= 8;
            sprites.push_back({object.kind, data::read_sprite_frame(main, jewel_frame(main, type, now.step), 2, rows),
                               banded(place, place.y), order});
            break;
        }
        case world::ObjectKind::exit_point: { // $14FC2, $15128
            // Drawn only once switched on (+$20, cleared at $14FC4) and
            // unless invisible (flags bit 7): 15 rows, 8 frames of 4 ticks.
            if (!now.shown || (parameter(object, 0) & 0x80)) break;
            const std::uint32_t frame = exit_frames + animation_offset(main, exit_list, now.step);
            sprites.push_back({object.kind, data::read_sprite_frame(main, frame, 2, 15),
                               placed(at, parameter(object, 0) << 8 | parameter(object, 1)), order});
            break;
        }
        case world::ObjectKind::push_button: { // $16048, $16112
            const int type = parameter(object, 0) & 7;
            bool pressed;
            if (type == 7) pressed = switches.primary == 0xFF && switches.secondary == 0xFF; // word $3616 = $FFFF
            else if (type & 4) pressed = ((type & 3) + 1) == switches.primary;
            else pressed = type == switches.secondary;
            const std::uint32_t frame = button_frames + std::uint32_t(type) * 0x208 + (pressed ? 0x104 : 0);
            sprites.push_back({object.kind, data::read_sprite_frame(main, frame, 2, 13), placed(at, type), order});
            break;
        }
        case world::ObjectKind::binary_switch: { // $16154, $16240
            const int colour = parameter(object, 0) & 3;
            const int facing = parameter(object, 1) & 3; // minus the level's rotation, 0 for levels as built
            const bool on = switches.binary & (8 >> colour);
            const std::uint32_t frame = switch_frames + std::uint32_t(colour * 8 + facing) * 0xDC + (on ? 0 : 0x370);
            sprites.push_back({object.kind, data::read_sprite_frame(main, frame, 2, 11), placed(at, 8 >> colour), order});
            break;
        }
        case world::ObjectKind::spark_generator: { // $15C10, $15CAA: hidden once it has fired (+$20)
            if (now.shown) break;
            const int period = parameter(object, 2);
            const int direction = parameter(object, 3);
            const std::uint32_t frame = spark_frames + ((direction & 1) ? 0 : 0x500) +
                                        animation_offset(main, spark_list, now.step);
            sprites.push_back({object.kind, data::read_sprite_frame(main, frame, 2, 16), placed(at, period), order});
            break;
        }
        case world::ObjectKind::lift: { // $14CBC, $14EF6
            const int flags = parameter(object, 6);
            const int base = (parameter(object, 7) & 7) << 8;
            const bool tall = flags & 0x40, column = flags & 0x80;
            const int type = flags & 7;
            // The mask is made once at the top height $7FF, key + $100,
            // in 8-row bands each $100 further back, from 8 rows above.
            const auto top = project({object.x * 256, object.y * 256, 0x7FF});
            SpritePlace place{at.x - 16, at.y, std::uint16_t(top.key + 0x100), -0x100, (top.y - 8) & ~7};
            place = banded(place, top.y - 8, mask_row(where.z));
            data::SpriteFrame frame;
            if (tall) {
                frame = data::read_sprite_frame(main, tall_lift_frames + std::uint32_t(type) * 0x1E0, 2, 24);
            } else {
                frame = data::read_sprite_frame(main, lift_frames + std::uint32_t(type) * 0x140, 2, 16);
                const int shaft = column ? std::max(0, ((where.z - base) >> 5) - 15) : 0;
                if (shaft > 0) frame = stacked(frame, data::read_sprite_frame(main, lift_shaft, 2, std::min(shaft, 64)));
            }
            sprites.push_back({object.kind, std::move(frame), place, order});
            break;
        }
        case world::ObjectKind::heads: { // $1594A, $15A98: 40 rows; occlusion as the lift's
            const int facing = parameter(object, 0) & 3; // minus the level's rotation
            const int type = parameter(object, 1) & 3;
            const std::uint32_t frame = head_frames + main.u16(head_offsets + std::uint32_t(type * 4 + facing) * 2);
            const auto top = project({object.x * 256, object.y * 256, 0x7FF});
            SpritePlace place{at.x - 16, at.y, std::uint16_t(top.key + 0x100), -0x100, (top.y - 8) & ~7};
            place = banded(place, top.y - 8, mask_row(where.z));
            sprites.push_back({object.kind, data::read_sprite_frame(main, frame, 2, 40), place, order});
            break;
        }
        case world::ObjectKind::pac:      // $1544A: set = 4th parameter byte; on the cell's centre
        case world::ObjectKind::pyramid:  // $157F6: set = 2nd parameter byte
        case world::ObjectKind::boink: {  // $151C4: set = low byte of the word in bytes 2-3
            const auto kind = world::ObjectKind(object.kind);
            const int set = kind == world::ObjectKind::pac       ? parameter(object, 3)
                            : kind == world::ObjectKind::pyramid ? parameter(object, 1)
                                                                 : parameter(object, 3);
            WorldPosition centre{object.x * 256 + 0x80, object.y * 256 + 0x80, where.z};
            if (now.x >= 0) centre.x = now.x;
            if (now.y >= 0) centre.y = now.y;
            // Pac aliens also settle on the block's surface ($12F62; flat
            // blocks add nothing, slopes are not rebuilt yet).
            const int heading = now.heading >= 0 ? now.heading : set; // the set byte + level rotation (0)
            const int offset = kind == world::ObjectKind::pac ? 0x10 : kind == world::ObjectKind::pyramid ? 0xC : 0;
            sprites.push_back(creature(main, object.kind, set, heading, centre, now.step, offset)); // $156BE, $15918, $152CE
            break;
        }
        default:
            break; // timers, secret passages, clues, messages, palettes: nothing to draw
        }
    }
    // $1440E: ascending sort value; equal values keep their slot order.
    std::stable_sort(sprites.begin(), sprites.end(),
                     [](const ObjectSprite& a, const ObjectSprite& b) { return std::uint16_t(a.order) < std::uint16_t(b.order); });
    return sprites;
}

void draw_with_explorer(Canvas& canvas, std::vector<ObjectSprite> sprites, const ObjectSprite& explorer,
                        const TileMap& map, const data::Graphics& graphics, int left, int top) {
    // The explorer has the lowest slot, so it goes first among equals.
    const auto at = std::find_if(sprites.begin(), sprites.end(), [&](const ObjectSprite& s) {
        return std::uint16_t(s.order) >= std::uint16_t(explorer.order);
    });
    sprites.insert(at, explorer);
    for (const auto& sprite : sprites) draw_sprite(canvas, sprite.frame, sprite.place, map, graphics, left, top);
}

} // namespace spindizzy::view
