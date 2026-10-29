// The embedding interface over BlueStreak's game modules (app/game_module.hpp), their asset
// packs and sound tests. Everything game-specific stays in the modules; this file only maps
// between them and the plain types of bluestreak_embed.hpp.

#include "bluestreak_embed.hpp"
#include "app/game_module.hpp"
#include "gamegear/lcd_filter.hpp"
#include "platform/sha256.hpp"
#include "sonic1_gg/catalog.hpp"
#include "sonic1_gg/data/content_store.hpp"
#include "sonic1_gg/module.hpp"
#include "sonic1_gg/music_player.hpp"
#include "sonic2_gg/catalog.hpp"
#include "sonic2_gg/data/content_store.hpp"
#include "sonic2_gg/module.hpp"
#include "sonic2_gg/music_player.hpp"
#include "sonic_advance_gba/data/content_store.hpp"
#include "sonic_advance_gba/module.hpp"
#include "triple_trouble_gg/catalog.hpp"
#include "triple_trouble_gg/data/content_store.hpp"
#include "triple_trouble_gg/module.hpp"
#include "triple_trouble_gg/music_player.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace bluestreak {
namespace {

using Bytes = std::vector<std::uint8_t>;

// The cartridge header's identity. Game Gear: "TMR SEGA" at 7FF0 and the BCD product code at
// 7FFC-7FFE (observed in the user's dumps: Sonic 1 2408, Sonic 2 2507, Triple Trouble 2530).
// Game Boy Advance: the game code at 0AC ("ASOE" in the user's Sonic Advance, "ASO" + region).
bool game_gear_product(std::span<const std::uint8_t> rom, unsigned product) {
    if (rom.size() < 0x8000 || std::memcmp(rom.data() + 0x7ff0, "TMR SEGA", 8) != 0) {
        return false;
    }
    const unsigned code = (rom[0x7ffd] >> 4) * 1000 + (rom[0x7ffd] & 15) * 100 +
                          (rom[0x7ffc] >> 4) * 10 + (rom[0x7ffc] & 15) + (rom[0x7ffe] >> 4) * 10000;
    return code == product;
}

bool sonic_advance_header(std::span<const std::uint8_t> rom) {
    return rom.size() >= 0xc0 && std::memcmp(rom.data() + 0xac, "ASO", 3) == 0;
}

std::vector<PackFile> to_files(std::vector<std::pair<std::string, Bytes>> files) {
    std::vector<PackFile> out;
    out.reserve(files.size());
    for (auto& [name, bytes] : files) {
        out.push_back({std::move(name), std::move(bytes)});
    }
    return out;
}

std::unique_ptr<shell::GameModule> make(Title title, const std::filesystem::path& pack,
                                        std::span<const std::uint8_t> state) {
    switch (title) {
    case Title::sonic1_gg:
        return sonic1_gg::make_module(pack, state);
    case Title::sonic2_gg:
        return sonic2_gg::make_module(pack, state);
    case Title::triple_trouble_gg:
        return triple_trouble_gg::make_module(pack, state);
    case Title::sonic_advance_gba:
        return sonic_advance_gba::make_module(pack, state);
    }
    throw std::runtime_error("Unknown game");
}

std::unique_ptr<shell::GameModule> make_level(Title title, const std::filesystem::path& pack,
                                              int level) {
    switch (title) {
    case Title::sonic1_gg:
        return sonic1_gg::make_level_module(pack, unsigned(level));
    case Title::sonic2_gg:
        return sonic2_gg::make_level_module(pack, unsigned(level));
    case Title::triple_trouble_gg:
        return triple_trouble_gg::make_level_module(pack, unsigned(level));
    case Title::sonic_advance_gba:
        break;
    }
    throw std::runtime_error("This game has no level selector.");
}

class ModuleSession final : public Session {
public:
    explicit ModuleSession(std::unique_ptr<shell::GameModule> module) : module_(std::move(module)) {}

    double step_seconds() const override { return module_->step_seconds(); }

    void set_view(bool widescreen, int canvas_width, int canvas_height) override {
        canvas_ = {canvas_width, canvas_height};
        module_->set_view(widescreen, canvas_width, canvas_height);
    }

    std::vector<float> tick(const Buttons& b, const Enhancements& e) override {
        return module_->tick({b.left, b.right, b.jump, b.down, b.up, b.start, b.action}, shell(e));
    }

    platform::Image render(const Enhancements& e) const override {
        return module_->render(shell(e), canvas_[0], canvas_[1]);
    }

    bool gameplay() const override { return module_->gameplay(); }
    std::string status() const override { return module_->status(); }
    std::string stopped() const override { return module_->stopped(); }
    std::vector<std::uint8_t> save() override { return module_->save_state(); }

private:
    static shell::Enhancements shell(const Enhancements& e) {
        // (The LCD look is drawn by the embedder: LcdLook.)
        return {e.widescreen, e.shield_transparency, e.water, e.rings, false};
    }

    std::unique_ptr<shell::GameModule> module_;
    std::array<int, 2> canvas_{};
};

std::unique_ptr<gg::MusicPlayer> music_player(Title title, const std::filesystem::path& pack) {
    switch (title) {
    case Title::sonic1_gg:
        return std::make_unique<sonic1_gg::MusicPlayer>(pack);
    case Title::sonic2_gg:
        return sonic2_gg::make_music_player(pack);
    case Title::triple_trouble_gg:
        return triple_trouble_gg::make_music_player(pack);
    case Title::sonic_advance_gba:
        break;
    }
    return nullptr;
}

} // namespace

std::vector<Enhancement> offered(Title title) {
    switch (title) {
    case Title::sonic1_gg:
        return {Enhancement::shield_transparency, Enhancement::widescreen, Enhancement::water,
                Enhancement::rings, Enhancement::lcd_screen};
    case Title::sonic2_gg:
        return {Enhancement::widescreen, Enhancement::water, Enhancement::lcd_screen};
    case Title::triple_trouble_gg:
        return {Enhancement::widescreen, Enhancement::lcd_screen};
    case Title::sonic_advance_gba:
        break;
    }
    return {};
}

// The level selector's start points, as BlueStreak's shell lists them (app/titles.cpp).
std::vector<StartPoint> start_points(Title title) {
    std::vector<StartPoint> out;
    switch (title) {
    case Title::sonic1_gg:
        for (const auto& l : sonic1_gg::levels) {
            const bool special = l.id >= 28 && l.id < sonic1_gg::ending_start;
            std::string hint =
                special ? "Plays the special stage with 3 lives, then goes on to Green Hill Act 2."
                : l.id == sonic1_gg::ending_start
                    ? "The finale after the escape and the ending, without the emeralds."
                : l.id == sonic1_gg::good_ending_start
                    ? "The finale after the escape and the ending, with all six emeralds."
                    : "";
            out.push_back({int(l.id), special ? "Special stages" : l.world,
                           special ? std::string("Stage ") + l.label : l.label, hint});
        }
        break;
    case Title::sonic2_gg:
        for (std::size_t i = 0; i < sonic2_gg::levels.size(); ++i) {
            out.push_back({int(i), sonic2_gg::levels[i].world, sonic2_gg::levels[i].label, ""});
        }
        out.push_back({int(sonic2_gg::ending_start), "Ending", "Standard",
                       "The ending and credits, without all six emeralds."});
        out.push_back({int(sonic2_gg::good_ending_start), "Ending", "All emeralds",
                       "The ending and credits, with all six emeralds."});
        break;
    case Title::triple_trouble_gg:
        for (std::size_t i = 0; i < triple_trouble_gg::levels.size(); ++i) {
            out.push_back({int(i), triple_trouble_gg::levels[i].world,
                           triple_trouble_gg::levels[i].label, ""});
        }
        for (unsigned i = 0; i < triple_trouble_gg::special_stage_count; ++i) {
            out.push_back({int(triple_trouble_gg::special_stage_start + i), "Special stages",
                           "Stage " + std::to_string(i + 1),
                           "Plays the special stage with 3 lives, then Great Turquoise Act 1."});
        }
        out.push_back({int(triple_trouble_gg::ending_start), "Ending", "Standard",
                       "The rival's short scene, as without all six emeralds."});
        out.push_back({int(triple_trouble_gg::good_ending_start), "Ending", "All emeralds",
                       "The ending and credits, with all six emeralds."});
        break;
    case Title::sonic_advance_gba:
        break;
    }
    return out;
}

std::string release(Title title) {
    switch (title) {
    case Title::sonic1_gg:
        return "Game Gear, Japan/USA (CRC32 3E31CB8C)";
    case Title::sonic2_gg:
        return "Game Gear, World (CRC32 95A18EC7)";
    case Title::triple_trouble_gg:
        return "Game Gear, USA/Europe/Brazil (CRC32 D23A2A93)";
    case Title::sonic_advance_gba:
        break;
    }
    return "Game Boy Advance, USA (CRC32 63F70FD8)";
}

std::vector<PackFile> extract(Title title, std::span<const std::uint8_t> rom,
                              std::string& release) {
    Bytes bytes(rom.begin(), rom.end());
    switch (title) {
    case Title::sonic1_gg:
        if (!game_gear_product(rom, 2408)) {
            throw NotThisTitle("not the Sonic 1 Game Gear cartridge");
        }
        release = bluestreak::release(title);
        return to_files(sonic1_gg::extract_pack(std::move(bytes)));
    case Title::sonic2_gg:
        if (!game_gear_product(rom, 2507)) {
            throw NotThisTitle("not the Sonic 2 Game Gear cartridge");
        }
        release = bluestreak::release(title);
        return to_files(gg_engine::extract_pack(sonic2_gg::cartridge, std::move(bytes)));
    case Title::triple_trouble_gg:
        if (!game_gear_product(rom, 2530)) {
            throw NotThisTitle("not the Triple Trouble Game Gear cartridge");
        }
        release = bluestreak::release(title);
        return to_files(gg_engine::extract_pack(triple_trouble_gg::cartridge, std::move(bytes)));
    case Title::sonic_advance_gba:
        if (!sonic_advance_header(rom)) {
            throw NotThisTitle("not the Sonic Advance cartridge");
        }
        release = bluestreak::release(title);
        return to_files(sonic_advance_gba::extract_pack(std::move(bytes)));
    }
    throw NotThisTitle("unknown game");
}

namespace {
bool current(Title title, const std::filesystem::path& pack) {
    switch (title) {
    case Title::sonic1_gg:
        return sonic1_gg::pack_current(pack);
    case Title::sonic2_gg:
        return sonic2_gg::pack_current(pack);
    case Title::triple_trouble_gg:
        return triple_trouble_gg::pack_current(pack);
    case Title::sonic_advance_gba:
        break;
    }
    return sonic_advance_gba::pack_current(pack);
}

// An import made by an older extractor may lack data the game now reads.
void require_current(Title title, const std::filesystem::path& pack) {
    if (!current(title, pack)) {
        throw std::runtime_error("This import was made by an older version of the game's importer. "
                                 "Delete the game files and import the ROM again.");
    }
}
} // namespace

void validate(Title title, const std::filesystem::path& pack) {
    require_current(title, pack);
    switch (title) {
    case Title::sonic1_gg:
        return sonic1_gg::validate_pack(pack);
    case Title::sonic2_gg:
        return sonic2_gg::validate_pack(pack);
    case Title::triple_trouble_gg:
        return triple_trouble_gg::validate_pack(pack);
    case Title::sonic_advance_gba:
        return sonic_advance_gba::validate_pack(pack);
    }
}

std::filesystem::path standalone_pack(Title title, const std::filesystem::path& root) {
    switch (title) {
    case Title::sonic1_gg:
        return sonic1_gg::pack_path(root);
    case Title::sonic2_gg:
        return sonic2_gg::pack_path(root);
    case Title::triple_trouble_gg:
        return triple_trouble_gg::pack_path(root);
    case Title::sonic_advance_gba:
        break;
    }
    return sonic_advance_gba::pack_path(root);
}

bool standalone_pack_current(Title title, const std::filesystem::path& root) {
    return current(title, standalone_pack(title, root));
}

// (The names BlueStreak's shell gives them, app/titles.cpp.)
std::filesystem::path standalone_quick_save(Title title, const std::filesystem::path& root) {
    const char* name = title == Title::sonic1_gg           ? "sonic-gg.quick"
                       : title == Title::sonic2_gg         ? "sonic2-gg.quick"
                       : title == Title::triple_trouble_gg ? "triple-trouble-gg.quick"
                                                           : "sonic-advance-gba.quick";
    return root / "saves" / name;
}

std::unique_ptr<Session> Session::start(Title title, const std::filesystem::path& pack,
                                        int start_point) {
    require_current(title, pack);
    return std::make_unique<ModuleSession>(start_point < 0 ? make(title, pack, {})
                                                           : make_level(title, pack, start_point));
}

std::unique_ptr<Session> Session::restore(Title title, const std::filesystem::path& pack,
                                          std::span<const std::uint8_t> state) {
    if (state.empty()) {
        throw std::runtime_error("The quick save has no game state.");
    }
    require_current(title, pack);
    return std::make_unique<ModuleSession>(make(title, pack, state));
}

std::vector<Track> sound_test(Title title, const std::filesystem::path& pack, double longest) {
    std::vector<Track> out;
    auto player = music_player(title, pack);
    if (!player) {
        return out;
    }
    const auto frames = std::uint64_t(longest / gg::MusicPlayer::step_seconds());
    for (unsigned i = 0; i < player->tracks().size(); ++i) {
        Track track{player->tracks()[i].title, player->tracks()[i].description, {}};
        player->play(i);
        for (std::uint64_t f = 0; f < frames && player->playing(); ++f) {
            const auto samples = player->tick();
            track.stereo.insert(track.stereo.end(), samples.begin(), samples.end());
        }
        player->stop();
        out.push_back(std::move(track));
    }
    return out;
}

struct LcdLook::State {
    gg::LcdFilter filter;
};

LcdLook::LcdLook() : state_(std::make_unique<State>()) {}
LcdLook::~LcdLook() = default;

void LcdLook::reset() { state_->filter.reset(); }

const platform::Image& LcdLook::apply(const platform::Image& frame, double elapsed) {
    return state_->filter.apply(frame, elapsed);
}

platform::Image LcdLook::mask(int width, int height, double cell_width, double cell_height,
                              double origin_x, double origin_y) {
    return gg::LcdFilter::mask(width, height, cell_width, cell_height, origin_x, origin_y);
}

int LcdLook::glow() { return gg::LcdFilter::glow; }

} // namespace bluestreak
