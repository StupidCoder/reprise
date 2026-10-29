// Reads the circle-the-answer puzzle data from its parameter script.
#include "puzzles/circle_parameters.hpp"

namespace picarat {

CircleParameters::CircleParameters(std::span<const std::uint8_t> bytes) {
    ScriptContext context;
    // InkColour(r, g, b), 8-bit components. Overlay 2 shifts each right by 3
    // (02078550..02078568) and combines them by addition with 16-bit
    // truncation (02072664..02072678), not by clamping or masking.
    context.bind(0x0c, [&](CommandArguments& a) {
        const auto r = std::uint32_t(a.integer() >> 3);
        const auto g = std::uint32_t(a.integer() >> 3);
        const auto b = std::uint32_t(a.integer() >> 3);
        drawingColor = std::uint16_t(r + (g << 5) + (b << 10));
    });
    // Region(x, y, extent, answer): added to the current group, at most 25
    // per group (overlay 33, 020f78b0..020f78f8).
    context.bind(0x18, [&](CommandArguments& a) {
        CircleRegion region{a.integer(), a.integer(), a.real(), a.truth()};
        auto& active = extraGroups.empty() ? regions : extraGroups.back();
        if (active.size() < 25) {
            active.push_back(region);
        }
    });
    // NewRegionGroup(): the following regions belong to the next picture,
    // at most four groups (020f8468).
    context.bind(0x3e, [&](CommandArguments&) {
        if (groupCount() < 4) {
            extraGroups.emplace_back();
        }
    });
    // MaximumCenters(n): how many regions one stroke may enclose (020f84ac).
    context.bind(0x86, [&](CommandArguments& a) { maximumCenters = a.integer(); });
    ScriptInterpreter interpreter(bytes);
    interpreter.run(context);
}

} // namespace picarat
