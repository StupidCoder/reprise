// Asset-free integration checks. Original-data coverage lives in the handoff report.
#include "data/Xml.hpp"
#include "game/LevelRuntime.hpp"
#include "game/PadStick.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        const auto centre = beacon::game::padStick(128, 128);
        require(centre.x == 0 && centre.y == 0, "pad centre leaves the dead zone");
        const auto left = beacon::game::padStick(0, 128);
        const auto right = beacon::game::padStick(255, 127);
        require(left.x < -0.99f && right.x > 0.99f &&
                std::abs(left.x + right.x) < 1e-6f, "pad direction symmetry changed");
        std::string error;
        auto xml = beacon::data::parseXml("<Rules><Value key=\"a&amp;b\">1.5</Value></Rules>", &error);
        require(xml && xml->child("Value") && xml->child("Value")->text == "1.5", "XML data not read");
        require(*xml->child("Value")->attribute("key") == "a&b", "XML entity not decoded");
        require(!beacon::data::parseXml("<Rules><Value></Rules>", &error), "bad XML accepted");
        beacon::game::Level level;
        level.objects.resize(2);
        level.objects[0].uuid = "first";
        level.objects[1].uuid = "second";
        level.byUuid = {{"first", 0}, {"second", 1}};
        beacon::game::LevelObject source;
        source.attributes.push_back({"Link", "first,missing,second,:1", true});
        const auto link = beacon::game::resolveLink(level, source, "Link");
        require(link.targets == std::vector<std::size_t>({0, 1}) && link.requireAll,
                "data-driven link resolution changed");
        std::cout << "Beacon core integration checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
