#include "fa_18_interceptor_amiga/game/draw_order.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/lessons/technical_lessons.hpp"
#include "platform/host.hpp"
#include "platform/resources.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
void check(bool v, const char *text) {
    if (!v)
        throw std::runtime_error(text);
}
bool same(const platform::Image &a, const platform::Image &b) {
    return a.width == b.width && a.height == b.height &&
           std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(platform::Color)) == 0;
}
int main(int argc, char **argv) {
    try {
        const std::set<std::string> known{"plane",  "order", "road",     "records",
                                          "flight", "bank",  "response", "sink", "landing_pose", "landing_probe", "landing_rules"};
        for (auto name : {"painting", "roads", "flight", "landing"}) {
            const auto bytes = platform::resource(std::string("articles/interceptor_") + name + ".txt");
            std::string text(bytes.begin(), bytes.end());
            check(text.starts_with("# "), "Article title");
            check(text.find("## Evidence and scope") != std::string::npos, "Evidence section");
            std::istringstream lines(text);
            std::string line;
            int figures = 0;
            while (std::getline(lines, line))
                if (line.starts_with("!figure ")) {
                    check(known.contains(line.substr(8)), "Known figure");
                    ++figures;
                }
            check(figures >= 2, "Explanatory figures");
        }
        if (argc > 1) {
            const auto assets = interceptor::data::load_assets(argv[1]);
            const auto shape = interceptor::data::read_shape(assets.program, 4);
            std::set<std::vector<int>> orders;
            for (int view = 0; view < 8; ++view) {
                const double angle = (35 + view * 45) * std::numbers::pi / 180;
                const std::array<double, 3> eye{400 * std::sin(angle), view % 2 ? -180.0 : 180.0,
                                                400 * std::cos(angle)};
                orders.insert(interceptor::game::shape_draw_order(
                    shape,
                    [&](const auto &p) {
                        const auto a = p[0], b = p[1], c = p[2];
                        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x,
                                     vy = c.y - a.y, vz = c.z - a.z;
                        return (uy * vz - uz * vy) * (a.x - eye[0]) + (uz * vx - ux * vz) * (a.y - eye[1]) +
                                   (ux * vy - uy * vx) * (a.z - eye[2]) <
                               0;
                    },
                    {0, 0, 0}, eye));
            }
            std::cout << "Distinct Hornet draw orders in the eight views: " << orders.size() << '\n';
            check(orders.size() > 1, "Viewpoints demonstrate different imported draw orders");
            const auto built = interceptor::game::road_points(assets.program);
            check(built.size() == 120, "Examined import produces 120 points");
            int zeros = 0;
            for (auto &[at, p] : built)
                zeros += assets.program.s16(at) == 0 && assets.program.s16(at + 2) == 0;
            std::cout << "Road points: " << built.size() << "; stored zero pairs: " << zeros << '\n';
            check(zeros == 120, "Stored points really start at zero");
            const auto tables=interceptor::game::read_flight_tables(assets.program);
            for(int setting=0;setting<4;++setting) for(bool inverted:{false,true}) {
                const auto p=interceptor::lessons::landing_probe(assets,tables,inverted,setting);
                check(p.deck && !p.hull && p.grounded,"Laboratory contact is on the carrier, clear of hull");
                check(p.crashed==(setting==2),"576 crashes; softer contacts survive at either roll");
                check(p.fast_contact==(setting==3),"Gear-up contact sets the fast-contact flag");
                check(p.clearance==(setting==3?750:1800),"Reference clearance depends on gear, not roll");
            }
            for (int chapter = 0; chapter < 4; ++chapter) {
                interceptor::lessons::TechnicalLesson lesson(argv[1], chapter);
                for (auto size : {std::pair{960, 640}, std::pair{640, 1138}, std::pair{1422, 640}}) {
                    lesson.set_canvas(size.first, size.second);
                    platform::Controls in;
                    in.wheel = -10000;
                    lesson.update(in, 0);
                    check(lesson.scroll() == 0, "Top bound");
                    auto first = lesson.draw();
                    check(first.width == size.first && first.height == size.second, "Canvas dimensions");
                    in = {};
                    in.nav_right = true;
                    const int old = lesson.selection();
                    lesson.update(in, 0);
                    check(lesson.selection() != old, "Directional experiment selection");
                    check(!same(first, lesson.draw()), "Selection updates diagram or controls");
                    in = {};
                    in.fire = true;
                    lesson.update(in, 0);
                    const int stage = lesson.stage();
                    lesson.update(in, 0);
                    check(lesson.stage() == stage, "Held fire changes once");
                    lesson.update({}, 0);
                    in = {};
                    in.wheel_precise = .5f;
                    lesson.update(in, 0);
                    check(lesson.scroll() == 28, "Fractional wheel");
                    in = {};
                    in.click = true;
                    in.mouse_held = true;
                    in.mouse_x = size.first / 2;
                    in.mouse_y = 300;
                    in.ui_pointer = std::array{size.first / 2, 300};
                    lesson.update(in, 0);
                    in.click = false;
                    in.ui_pointer = std::array{size.first / 2, 200};
                    lesson.update(in, 0);
                    check(lesson.scroll() == 128, "Touch drag");
                    lesson.update({}, 0);
                    in = {};
                    in.wheel = -10000;
                    lesson.update(in, 0);
                    int page = 0;
                    do {
                        if (argc > 2) {
                            std::filesystem::create_directories(argv[2]);
                            platform::save_png(lesson.draw(), std::filesystem::path(argv[2]) /
                                                                  (std::to_string(chapter) + "-" +
                                                                   std::to_string(size.first) + "-" +
                                                                   std::to_string(page++) + ".png"));
                        }
                        if (lesson.scroll() == lesson.scroll_limit())
                            break;
                        in = {};
                        in.page_down = true;
                        lesson.update(in, 0);
                    } while (true);
                    const auto last = lesson.draw();
                    in = {};
                    in.wheel = 10000;
                    lesson.update(in, 0);
                    check(same(last, lesson.draw()), "End clamp");
                }
                lesson.set_canvas(640, 1138);
                check(lesson.scroll() == lesson.scroll_limit(), "Resize preserves reading position");
            }
        }
        std::cout << "Interceptor lessons passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
