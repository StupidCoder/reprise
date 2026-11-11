#include "fort_apocalypse_c64/game/module.hpp"
#include "fort_apocalypse_c64/enhanced/world.hpp"
#include "c64/palette.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(const platform::Image& a, const platform::Image& b) {
    return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
        std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(platform::Color)) == 0;
}
}
int main(int argc, char** argv) {
    try {
        using namespace fort_apocalypse;
        enhanced::WorldScene synthetic;
        synthetic.colours = {0, 8, 1, 5};
        synthetic.sprites.push_back({1719, 3, 3, 1, 1, {true, true, true}});
        auto image = enhanced::world_image(synthetic);
        require(image.pixels[3 * 1720 + 1719].r == 255 && image.pixels[3 * 1720].r == 255 &&
                image.pixels[3 * 1720 + 1].r == 255, "sprite must cross wrap without clipping");
        synthetic.sprites[0].x = -1;
        require(same(image, enhanced::world_image(synthetic)), "negative wrap must match positive wrap");
        synthetic.cells[0].glyph[3] = 0x80; // foreground pair 10 occludes sprite
        synthetic.colours[2] = 2;
        require(enhanced::world_image(synthetic).pixels[3 * 1720].r == c64::colour(2).r,
                "foreground priority must survive panorama rendering");
        std::puts("PASS synthetic wrap and foreground priority");
        if (argc < 2) return 0;
        auto baseline = game::start(argv[1]), drawn = game::start(argv[1]);
        drawn->set_world_surface_supported(true);
        drawn->set_option(11, 1);
        std::array<float, 1920> audio_a{}, audio_b{};
        int samples = 0, maximum_aliases = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            app::GameInput input;
            input.f1 = frame == 2;
            input.fire = frame > 300 && frame % 60 < 30;
            input.up = frame > 300 && frame % 240 < 120;
            input.right = frame > 400 && frame < 850;
            baseline->tick(input); drawn->tick(input);
            if (frame % 20 == 0) {
                const auto before = drawn->write_state();
                auto first = drawn->world_surface();
                auto second = drawn->world_surface();
                require(bool(first) == bool(second), "surface availability changed while drawing");
                if (first) {
                    require(same(first->image, second->image), "repeated drawing changed pixels");
                    require(first->image.width == 1720 && first->image.height == 320, "wrong world dimensions");
                    require(first->focus_x >= 0 && first->focus_x < 1, "focus outside horizontal period");
                    const auto scene = static_cast<game::Module&>(*drawn).game().world_scene();
                    maximum_aliases = std::max(maximum_aliases, scene->alias_disagreements);
                    if (samples++ == 0 && argc > 2) platform::save_png(first->image, argv[2]);
                }
                require(before == drawn->write_state(), "drawing mutated persisted state");
                require(before == baseline->write_state(), "drawing changed simulation trajectory");
                require(same(baseline->picture(), drawn->picture()), "flat rendering changed");
            }
            baseline->audio(audio_a, 48000); drawn->audio(audio_b, 48000);
            require(audio_a == audio_b, "drawing changed audio");
        }
        require(samples > 0, "run never reached a world surface");
        const auto save = drawn->write_state();
        drawn->read_state(save);
        require(drawn->write_state() == save, "save round trip changed state");
        std::printf("PASS 1200 ticks, %d panorama samples; repeated rendering/state/audio/flat/save checks; max alias disagreements=%d\n",
                    samples, maximum_aliases);
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
