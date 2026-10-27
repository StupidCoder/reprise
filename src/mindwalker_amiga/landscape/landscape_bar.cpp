#include "mindwalker_amiga/landscape/landscape_bar.hpp"
#include <vector>

namespace mindwalker::landscape {
namespace {

constexpr std::uint32_t new_window = 0x204B1E;
// S's icons ($7009DC +$2F8 emblem 32 x 10 x 2, $700A2C +$2FC life figure,
// $700A40 +$300 pause box, $700A54 +$304 slider knob) and the Image data
// pointers A sets to them ($200040).
constexpr std::uint32_t emblem = 0x7009DC, life = 0x700A2C, pause_box = 0x700A40, knob = 0x700A54;
constexpr std::uint32_t emblem_data = 0x204836, pause_box_data = 0x204A3A, knob_data = 0x204A26;
constexpr std::uint32_t first_life_data = 0x2048A6, image_size = 20; // five Images, 20 bytes apart
constexpr int lives_gadget = 64, slider_gadget = 32, compass_first = 0; // compass ids 0-3: N E S W

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t at, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = program.u8(at + std::uint32_t(i));
    return out;
}

} // namespace

LandscapeBar::LandscapeBar(const data::GameFiles& files) {
    const auto& s = files.program(data::Stage::setup);
    std::map<std::uint32_t, std::vector<std::uint8_t>> data = {{emblem_data, bytes_at(s, emblem, 80)},
                                                               {pause_box_data, bytes_at(s, pause_box, 20)},
                                                               {knob_data, bytes_at(s, knob, 24)}};
    for (std::uint32_t i = 0; i < 5; ++i) data[first_life_data + i * image_size] = bytes_at(s, life, 20);
    bar_ = screen::GadgetBar(files.program(data::Stage::landscape), new_window, std::move(data));
}

void LandscapeBar::set(int lives, int direction, int stage) {
    auto& figures = bar_.find(lives_gadget)->images;
    for (std::size_t i = 0; i < figures.size(); ++i) {
        const bool shown = int(i) + 1 < lives;
        // Depth (Image +8) and PlanePick (+14); PlaneOnOff stays $12 (pen 18).
        figures[i].depth = shown ? 1 : 0;
        figures[i].plane_pick = shown ? 4 : 0;
    }
    for (int d = 0; d < 4; ++d) bar_.find(compass_first + d)->texts.front().front = d == (direction & 3) ? 0x1F : 0;
    bar_.find(slider_gadget)->horizontal_pot = std::uint16_t(stage * 0x618);
}

} // namespace mindwalker::landscape
