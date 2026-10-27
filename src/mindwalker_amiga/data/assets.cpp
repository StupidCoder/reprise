#include "mindwalker_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <stdexcept>

namespace mindwalker::data {
namespace {

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t address, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = program.u8(address + std::uint32_t(i));
    }
    return out;
}

Palette palette_at(const amiga::LoadedImage& program, std::uint32_t address, int colours) {
    Palette palette;
    for (int i = 0; i < colours; ++i) {
        palette.push_back(program.u16(address + std::uint32_t(i * 2)) & 0x0FFF);
    }
    return palette;
}

std::vector<std::uint8_t> read_sized(const std::filesystem::path& file, std::size_t size) {
    auto bytes = platform::read_file(file);
    if (bytes.size() < size) {
        throw std::runtime_error("The installed Mind Walker data is damaged (" + file.filename().string() +
                                 "); please import the disk again.");
    }
    return bytes;
}

// MindB: five 320 x 160 planes, one after the other. T $602DD8 reads them
// into the screen's bitplanes listed at $603350 ({0, 1, 2, 3, 5}: data bits
// and the HAM "modify red" control bit), from row 15; T $602C70 then loads
// the title palette at $6032B0.
HamPicture decode_title(std::span<const std::uint8_t> file, const amiga::LoadedImage& title_program) {
    constexpr int width = 320, height = 160, plane_bytes = width / 8 * height, file_planes = 5;
    constexpr std::uint32_t plane_table = 0x603350, palette = 0x6032B0;
    HamPicture picture;
    picture.palette = palette_at(title_program, palette, 16);
    picture.codes = {width, height, 6, std::vector<std::uint8_t>(std::size_t(width * height))};
    for (int p = 0; p < file_planes; ++p) {
        const int bitplane = title_program.s32(plane_table + std::uint32_t(p * 4));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int bit = file[std::size_t(p * plane_bytes + y * (width / 8) + x / 8)] >> (7 - x % 8) & 1;
                picture.codes.pixels[std::size_t(y * width + x)] |= std::uint8_t(bit << bitplane);
            }
        }
    }
    return picture;
}

// S $7005A8 copies five sets of 16-pixel, 2-plane frames into chip memory and
// files pointers to them in the shared block in the order of each set's
// 1-based index list. The frame counts and sizes are the copy loops' bounds.
struct SetLayout {
    const char* name;
    std::uint32_t index_list; // 0: frames in stored order
    int sequence_length;
    std::uint32_t frames;
    int frame_count;
    int rows;
};

constexpr SetLayout set_layouts[] = {
    {"Shared images +$76", 0x700A94, 28, 0x700B04, 27, 22},
    {"Shared images +$E6", 0x701464, 28, 0x7014D4, 27, 19},
    {"Shared images +$156", 0x701CF0, 28, 0x701D60, 19, 22},
    {"Shared images +$1C6", 0x702400, 60, 0x702540, 19, 20},
    {"Shared images +$2D0", 0, 10, 0x702E48, 10, 14},
};
// The +$1C6 set continues with 32 frames of 6 rows ($702B30), numbered 20-51
// in its index list.
constexpr std::uint32_t small_frames = 0x702B30;
constexpr int small_frame_count = 32, small_frame_rows = 6;

std::vector<ImageSet> decode_figures(const amiga::LoadedImage& setup) {
    std::vector<ImageSet> sets;
    for (const auto& layout : set_layouts) {
        ImageSet set{layout.name, {}, {}};
        const auto add_frames = [&](std::uint32_t at, int count, int rows) {
            const std::uint32_t frame_bytes = std::uint32_t(rows * 2 * 2);
            for (int f = 0; f < count; ++f) {
                set.frames.push_back(planar_bitmap(bytes_at(setup, at + std::uint32_t(f) * frame_bytes, frame_bytes), 16,
                                                   rows, 2));
            }
        };
        add_frames(layout.frames, layout.frame_count, layout.rows);
        if (layout.frames == 0x702540) {
            add_frames(small_frames, small_frame_count, small_frame_rows);
        }
        for (int i = 0; i < layout.sequence_length; ++i) {
            set.sequence.push_back(layout.index_list ? setup.s32(layout.index_list + std::uint32_t(i * 4)) - 1 : i);
        }
        sets.push_back(std::move(set));
    }
    return sets;
}

// S's one-plane icons (h6; their byte counts are the table at $700A6C), kept
// in the shared block at +$2F8, +$2FC, +$300 and +$304.
std::vector<Bitmap> decode_icons(const amiga::LoadedImage& setup) {
    struct Icon {
        std::uint32_t at;
        int width, height;
    };
    constexpr Icon icons[] = {{0x7009DC, 32, 20}, {0x700A2C, 16, 10}, {0x700A40, 16, 10}, {0x700A54, 16, 12}};
    std::vector<Bitmap> out;
    for (const auto& icon : icons) {
        out.push_back(planar_bitmap(bytes_at(setup, icon.at, std::size_t(icon.width / 8 * icon.height)), icon.width,
                                    icon.height, 1));
    }
    return out;
}

// A sample copied with each byte shifted left by `shift` in 8 bits (B $3032A8
// shifts by 3, P $5011xx/$5012xx by 2).
Sample sample_at(const amiga::LoadedImage& program, const char* name, std::uint32_t at, std::size_t length, int shift) {
    Sample sample{name, {}};
    for (std::size_t i = 0; i < length; ++i) {
        sample.data.push_back(std::int8_t(std::uint8_t(program.u8(at + std::uint32_t(i)) << shift)));
    }
    return sample;
}

std::string c_string(const amiga::LoadedImage& program, std::uint32_t at) {
    std::string text;
    for (char c; (c = char(program.u8(at))) != 0; ++at) {
        text += c;
    }
    return text;
}

} // namespace

Bitmap planar_bitmap(std::span<const std::uint8_t> bytes, int width, int height, int planes) {
    const int row_bytes = (width + 15) / 16 * 2;
    const int plane_bytes = row_bytes * height;
    if (bytes.size() < std::size_t(plane_bytes * planes)) {
        throw std::runtime_error("A Mind Walker image is cut short.");
    }
    Bitmap bitmap{width, height, planes, std::vector<std::uint8_t>(std::size_t(width * height))};
    for (int p = 0; p < planes; ++p) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int bit = bytes[std::size_t(p * plane_bytes + y * row_bytes + x / 8)] >> (7 - x % 8) & 1;
                bitmap.pixels[std::size_t(y * width + x)] |= std::uint8_t(bit << p);
            }
        }
    }
    return bitmap;
}

Assets load_assets(const std::filesystem::path& content) {
    const GameFiles files(content);
    const auto& setup = files.program(Stage::setup);
    const auto& title = files.program(Stage::title);
    const auto& landscape = files.program(Stage::landscape);
    const auto& tunnel = files.program(Stage::tunnel);
    const auto& arena = files.program(Stage::arena);
    const auto& psyche = files.program(Stage::psyche);

    Assets assets;
    assets.title = decode_title(read_sized(content / "MindB", 32000), title);
    for (int i = 0; i < 6; ++i) {
        // P $50574C shows rows 0-187 of a portrait; the files hold 200.
        const auto file = read_sized(content / ("MindF" + std::to_string(i + 1)), 8000);
        assets.portraits[std::size_t(i)] = planar_bitmap(file, 320, 200, 1);
    }
    assets.figures = decode_figures(setup);
    assets.icons = decode_icons(setup);

    // B h18 $30273C: two Images, 70 x 70 x 3, data in the chip hunk h15.
    constexpr std::uint32_t hands = 0x301494, hand_bytes = 10 * 70 * 3;
    for (std::uint32_t h = 0; h < 2; ++h) {
        assets.hands[h] = planar_bitmap(bytes_at(tunnel, hands + h * hand_bytes, hand_bytes), 70, 70, 3);
    }
    // C h48: the arena's Images ($4078C4, 20 bytes apart) get chip copies of
    // these blocks (C $406D28): four 32 x 32 x 4 cells (also drawn with their
    // first three planes), a 32 x 10 x 4, a 32 x 15 x 4 and a 16 x 14 x 4.
    struct ArenaImage {
        std::uint32_t at;
        int width, height;
    };
    constexpr ArenaImage arena_images[] = {{0x406F34, 32, 32}, {0x407134, 32, 32}, {0x407334, 32, 32},
                                           {0x407534, 32, 32}, {0x407734, 32, 10}, {0x4077D4, 32, 15},
                                           {0x406EC4, 16, 14}};
    for (const auto& image : arena_images) {
        const auto size = std::size_t((image.width + 15) / 16 * 2 * image.height * 4);
        assets.arena_images.push_back(planar_bitmap(bytes_at(arena, image.at, size), image.width, image.height, 4));
    }

    assets.landscape_palette = palette_at(landscape, 0x2004AC, 32);
    assets.tunnel_palette = palette_at(tunnel, 0x3002E8, 32);
    assets.arena_palette = palette_at(arena, 0x40042C, 32);
    assets.psyche_palette = palette_at(psyche, 0x5002F4, 32);
    assets.title_palette = palette_at(title, 0x600AD8, 16);

    // P's word table at $501CD2 (32 pointers; the screen picks by & 31).
    for (std::uint32_t w = 0; w < 32; ++w) {
        assets.words.push_back(c_string(psyche, psyche.u32(0x501CD2 + w * 4)));
    }

    assets.samples.push_back(sample_at(tunnel, "Tunnel (B h33)", 0x3034DC, 2000, 3));
    assets.samples.push_back(sample_at(arena, "Arena (C h33)", 0x4050DC, 4000, 0));
    assets.samples.push_back(sample_at(arena, "Arena (C h36)", 0x406094, 1000, 0));
    assets.samples.push_back(sample_at(psyche, "Psyche (P h24)", 0x5044BC, 2048, 2));
    assets.samples.push_back(sample_at(psyche, "Psyche (P h27)", 0x504CD4, 2048, 2));
    for (std::uint32_t w = 0; w < 6; ++w) {
        assets.samples.push_back(
            sample_at(psyche, ("Psyche wave " + std::to_string(w + 1)).c_str(), 0x5054EC + w * 64, 64, 0));
    }

    // MindHighs: 32 entries of a score long and four initials (T $602196).
    const auto highs = read_sized(content / "MindHighs", 256);
    for (std::size_t e = 0; e < 32; ++e) {
        const auto* entry = &highs[e * 8];
        HighScore score{std::uint32_t(entry[0]) << 24 | std::uint32_t(entry[1]) << 16 | std::uint32_t(entry[2]) << 8 |
                            entry[3],
                        {}};
        for (int c = 4; c < 8 && entry[c] != 0; ++c) {
            score.initials += char(entry[c]);
        }
        if (score.score != 0 || !score.initials.empty()) {
            assets.high_scores.push_back(score);
        }
    }
    return assets;
}

} // namespace mindwalker::data
