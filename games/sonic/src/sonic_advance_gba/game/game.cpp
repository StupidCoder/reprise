#include "sonic_advance_gba/game/game.hpp"
#include "gba/bios.hpp"
#include "sonic_advance_gba/game/demo.hpp"

namespace sonic_advance_gba {

Game::Game(const Assets& assets, std::optional<SaveData> save) : assets_(assets) {
    sound_.attach(assets_);
    // 080003EC: the display starts with sprites in 1D tile order and nothing
    // shown (DISPCNT 0080 is forced blank, set by the hardware init as 0x80;
    // the screens set their own before the first picture).
    video_.settings.set_control(0x80);
    // 0803CB90: a valid save starts with the SEGA logo, otherwise with the
    // language select.
    if (save) {
        save_ = *save;
        start_sega_logo();
    } else {
        throw Unrecovered("the language select (0806A700) for a cartridge without a save");
    }
}

void Game::clear_screen_processes() {
    scheduler_.destroy_range(0, 0xFFFF);
    backgrounds_.drop_pending();
}

namespace {

// 0804CD28: holds the demo buttons; its end (0804CD2C) ends the demo mode.
class DemoInput final : public Process {
public:
    explicit DemoInput(Controller& c) : Process(0x2000), c_(c) {}

    void on_destroy() override {
        c_.demo = DemoMode::off;
        c_.demo_read = c_.demo_written = 0;
        c_.demo_input.clear();
    }

    void update(Game&) override {}

private:
    Controller& c_;
};

} // namespace

void Game::restart_demo_input() {
    const DemoMode mode = controller_.demo;
    controller_.demo = DemoMode::off;
    controller_.demo_input.assign(0x800, 0);
    const auto bytes = gba::lz77_decompress([&](u32 at) { return assets_.u8(at); }, demo_data_);
    for (std::size_t i = 0; i + 1 < bytes.size() && i / 2 < 0x800; i += 2) {
        controller_.demo_input[i / 2] = u16(bytes[i] | bytes[i + 1] << 8);
    }
    scheduler_.create<DemoInput>(controller_);
    controller_.demo_read = controller_.demo_written = 0;
    controller_.demo = mode;
}

void Game::start_demo() {
    sonic_advance_gba::start_demo(*this);
}

void Game::frame(u16 buttons) {
    // 0800089C: sound engine, buttons, processes.
    sound_.main();
    controller_.read(buttons);
    scheduler_.run(*this, paused_);
    vertical_blank();
}

void Game::vertical_blank() {
    // 08000F44: the interrupt counts frames.
    ++frame_counter_;
    sound_.vertical_blank();
    // 08000980: the frame's picture goes to the display; then the
    // background workers copy tiles, clear and draw maps (0809B3B0) -
    // what they put in the settings shows one frame later.
    const SpriteHold hold = paused_           ? SpriteHold::held
                            : pause_requested ? SpriteHold::starting
                                              : SpriteHold::none;
    commit_video(video_, display_, hold, !pause_requested);
    backgrounds_.run(assets_, video_.settings, display_);
    paused_ = pause_requested;
}

} // namespace sonic_advance_gba
