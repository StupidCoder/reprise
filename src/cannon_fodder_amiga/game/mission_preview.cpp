#include "cannon_fodder_amiga/game/mission_preview.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include <string>

namespace fodder::game {
namespace {

struct Mission {
    data::MissionMap map;
    MapPicture picture;
    data::Palette palette;
    MissionObjects objects;
    ObjectSheets sheets;
    FrameTables frames;
};

Mission load(const data::Content& content, int number) {
    const int disk = data::map_disk(number);
    auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
    MapPicture picture(map, data::read_tile_set(content.read(map.base_set, disk)),
                       data::read_tile_set(content.read(map.sub_set, disk)));
    const auto land = map.base_set.substr(0, 3);
    auto palette = data::read_palette(content.read(land + "base.pal", disk));
    auto objects = spawn_objects(data::read_placements(content.read("mapm" + std::to_string(number) + ".spt", disk)));
    for (auto& o : objects.slots) {
        if (o.used) {
            if (const auto graphic = fixed_first_graphic(o.kind)) {
                o.graphic = *graphic;
            }
        }
    }
    ObjectSheets sheets{amiga::read_ilbm(content.read(land + "army.lbm", disk)),
                        amiga::read_ilbm(content.read(land + "copt.lbm", disk))};
    return {std::move(map), std::move(picture), std::move(palette), std::move(objects), std::move(sheets),
            FrameTables(content.read("fodderc", 1))};
}

// Every slot whose look is known, in the draw list's layering.
std::vector<int> drawable(const MissionObjects& objects) {
    std::vector<int> order;
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = objects.slots[std::size_t(i)];
        if (o.used && fixed_first_graphic(o.kind)) {
            order.push_back(i);
        }
    }
    return order;
}

void mark(platform::Image& image, const GameObject& o, int origin_x, int origin_y, int first_column) {
    if (!o.used || fixed_first_graphic(o.kind)) {
        return;
    }
    const platform::Color colour = o.kind == 0 ? platform::Color{255, 255, 255, 255}
                                   : (o.kind == 5 || o.kind == 0x24) ? platform::Color{255, 64, 64, 255}
                                                                      : platform::Color{255, 220, 0, 255};
    const int left = (o.x >> 16) - origin_x, bottom = (o.y >> 16) - origin_y, top = bottom - 13;
    for (int x = left; x < left + 16; ++x) {
        if (x >= first_column) {
            image.put(x, top, colour);
            image.put(x, bottom, colour);
        }
    }
    for (int y = top; y <= bottom; ++y) {
        if (left >= first_column) image.put(left, y, colour);
        if (left + 15 >= first_column) image.put(left + 15, y, colour);
    }
}

} // namespace

std::optional<int> fixed_first_graphic(int kind) {
    switch (kind) {
    case 13: return 0x8f;
    case 14: return 0x90;
    case 15: return 0x91;
    default: return std::nullopt;
    }
}

platform::Image map_objects_image(const data::Content& content, int number) {
    auto m = load(content, number);
    std::vector<std::uint8_t> pixels(std::size_t(m.picture.width() * m.picture.height()));
    for (int y = 0; y < m.picture.height(); ++y) {
        for (int x = 0; x < m.picture.width(); ++x) {
            pixels[std::size_t(y * m.picture.width() + x)] = m.picture.at(x, y);
        }
    }
    draw_objects({pixels, m.picture.width(), m.picture.height(), 0, 0, 0}, m.objects, drawable(m.objects), m.frames,
                 m.sheets);
    platform::Image image(m.picture.width(), m.picture.height());
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        image.pixels[i] = data::to_color(m.palette[pixels[i] & 15]);
    }
    for (const auto& o : m.objects.slots) {
        mark(image, o, 0, 0, 0);
    }
    return image;
}

std::optional<platform::Image> mission_start_image(const data::Content& content, int number) {
    auto m = load(content, number);
    if (!map_fits_screen(m.map)) {
        return std::nullopt;
    }
    const Camera camera{0, 0};
    auto view = view_indices(m.picture, camera);
    draw_objects(view, camera, m.objects, drawable(m.objects), m.frames, m.sheets);
    platform::Image image(view_width, view_height);
    for (std::size_t i = 0; i < view.size(); ++i) {
        image.pixels[i] = data::to_color(m.palette[view[i] & 15]);
    }
    for (const auto& o : m.objects.slots) {
        mark(image, o, camera.x - view_column_of_camera, camera.y, view_column_of_camera);
    }
    // The panel, from a new game's first recruits.
    const auto pstuff = amiga::read_ilbm(content.read("pstuff.lbm", 1));
    const PanelArt art(content.read("fodderc", 1), pstuff);
    PanelState state;
    int soldiers = 0;
    for (const auto& o : m.objects.slots) {
        soldiers += o.used && o.kind == 0;
    }
    state.soldiers = {std::min(soldiers, 8), 0, 0};
    state.icon = {soldiers > 1 ? 1 : 0, 0, 0};
    for (int i = 0; i < std::min(soldiers, 8); ++i) {
        state.roster.push_back({i, 0, 0, 0, true});
    }
    const auto panel = compose_panel(art, state);
    for (int y = 0; y < panel_height; ++y) {
        for (int x = 0; x < panel_width; ++x) {
            // The sprites start one pixel left of the play window (HSTART $80 vs DIW $81).
            const auto c = panel[std::size_t(y * panel_width + x)];
            if (c != 0) {
                image.put(x - 1, y, data::to_color(pstuff.palette[c]));
            }
        }
    }
    return image;
}

} // namespace fodder::game
