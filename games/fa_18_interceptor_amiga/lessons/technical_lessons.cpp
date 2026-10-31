#include "fa_18_interceptor_amiga/lessons/technical_lessons.hpp"
#include "fa_18_interceptor_amiga/game/draw_order.hpp"
#include "fa_18_interceptor_amiga/game/carrier.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "platform/resources.hpp"
#include "ui/shelf.hpp"
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace interceptor::lessons {
namespace {
namespace s = ui::shelf;
using platform::Color;
using platform::Image;
using P = data::ShapePoint;
constexpr Color orange{185, 83, 39};
void line(Image &im, int x, int y, int xx, int yy, Color c) {
    const int n = std::max(std::abs(xx - x), std::abs(yy - y));
    for (int i = 0; i <= n; ++i) {
        const double t = n ? double(i) / n : 0;
        ui::rect(im, int(x + (xx - x) * t), int(y + (yy - y) * t), 2, 2, c);
    }
}
void dot(Image &im, int x, int y, Color c) { ui::ring(im, x, y, 5, 5, c, 0, 0, im.width, im.height); }
void label(Image &im, std::string_view text, int x, int y, Color c = s::muted) {
    s::text(im, text, x, y, 15, c);
}
std::string number(double v) { return std::to_string(int(std::lround(v))); }
P sub(P a, P b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P cross(P a, P b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dotp(P a, P b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool facing(const std::vector<P> &p, P eye) {
    return p.size() < 3 || dotp(cross(sub(p[1], p[0]), sub(p[2], p[0])), sub(p[0], eye)) < 0;
}
void polygon(Image &im, const std::vector<std::array<int, 2>> &p, Color c) {
    if (p.size() < 3)
        return;
    int low = im.height, high = 0;
    for (auto v : p) {
        low = std::min(low, v[1]);
        high = std::max(high, v[1]);
    }
    for (int y = std::max(0, low); y <= std::min(im.height - 1, high); ++y) {
        std::vector<int> xs;
        for (std::size_t i = 0; i < p.size(); ++i) {
            auto a = p[i], b = p[(i + 1) % p.size()];
            if ((a[1] <= y && b[1] > y) || (b[1] <= y && a[1] > y))
                xs.push_back(a[0] + int(double(y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])));
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 1; i < xs.size(); i += 2)
            ui::rect(im, xs[i - 1], y, xs[i] - xs[i - 1] + 1, 1, c);
    }
}
Color palette(const data::Palette &p, int index) {
    const auto c = p.at(std::size_t(index) & 31);
    return {std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17), std::uint8_t((c & 15) * 17)};
}
struct Block {
    std::string text;
    int y{}, height{}, size{20};
    bool figure{};
};
} // namespace

LandingProbe landing_probe(const data::Assets& assets, const game::FlightTables& tables, bool inverted, int setting) {
    game::Carrier carrier(assets.program);
    auto a=carrier.start_on_deck(assets.program,tables);
    a.attitude={0,0,std::int16_t(inverted ? game::full_turn/2 : 0)};
    a.body=game::body_matrix(tables,a.attitude);
    a.view=game::view_matrix(tables,a.attitude);
    a.gear=setting==3 ? 0xE0 : 0;
    a.motion=0; a.state=game::flag::moves; a.surface=0;
    a.speed=a.airspeed=1200; a.vx=a.vy=a.vz=0;
    const int rates[]={384,575,576,384};
    a.sink=std::int16_t(rates[std::clamp(setting,0,3)]);
    const int clearance=setting==3 ? 0x2EE : 0x708;
    a.y=carrier.deck_height*256+clearance+1;
    // Establish deck recognition at this controlled pose, then reproduce the
    // movement/contact order. coast uses the very same move() as fly().
    const bool hull_before=carrier.touch(a,a);
    const bool deck=(a.surface&0xC0)!=0;
    const auto before=a;
    game::coast(a,{},tables);
    const bool hull=carrier.touch(a,before) || hull_before;
    return {rates[std::clamp(setting,0,3)], int(a.y-carrier.deck_height*256),deck,hull,
            bool(a.motion&game::flag::on_ground),bool(a.state&game::flag::crashed)||hull,
            bool(a.motion&game::flag::touched_down_fast)};
}

TechnicalLesson::TechnicalLesson(const std::filesystem::path &content, int chapter)
    : assets_(data::load_assets(content)), chapter_(chapter) {
    if (chapter < 0 || chapter > 3)
        throw std::runtime_error("Unknown Interceptor lesson");
    tables_ = game::read_flight_tables(assets_.program);
    plane_ = data::read_shape(assets_.program, 4); // F/A-18, not the F-16 type 3.
    const auto built = game::road_points(assets_.program);
    for (auto it = built.begin(); it != built.end();) {
        const auto at = it->first - 8;
        Road road{};
        road.a = {assets_.program.s16(at), assets_.program.s16(at + 2)};
        road.b = {assets_.program.s16(at + 4), assets_.program.s16(at + 6)};
        for (int j = 0; j < 3; ++j, ++it) {
            if (it == built.end() || it->first != at + 8 + 4 * j)
                throw std::runtime_error("Incomplete road record");
            road.after[j] = it->second;
            road.before[j] = {assets_.program.s16(it->first), assets_.program.s16(it->first + 2)};
        }
        roads_.push_back(road);
    }
    if (roads_.empty() || plane_.faces.empty())
        throw std::runtime_error("Lesson geometry is missing");
    const char *names[] = {"painting", "roads", "flight", "landing"};
    const auto bytes = platform::resource(std::string("articles/interceptor_") + names[chapter] + ".txt");
    article_.assign(bytes.begin(), bytes.end());
    if (chapter_ == 0)
        stage_ = std::max(0, plane_.lists - 1);
    simulate();
    compose();
}
void TechnicalLesson::simulate() {
    if (chapter_ != 2)
        return;
    const auto run = [&](bool selected) {
        auto a = game::start_at_place(assets_.program, tables_, 0);
        // A documented laboratory state, not a captured historical flight or a save.
        a.y = 0x200000;
        a.motion = 0;
        a.handling = 0;
        a.gear = 0xE0;
        a.attitude = {0, 0, 0};
        a.body = game::body_matrix(tables_, a.attitude);
        a.view = game::view_matrix(tables_, a.attitude);
        a.throttle = stage_ % 2 ? 120 : 60;
        a.throttle_target = 0;
        a.speed = a.airspeed = 0x1000;
        a.vx = a.vy = 0;
        a.vz = 4 * a.airspeed;
        std::vector<Sample> samples;
        for (int tick = 0; tick <= 120; ++tick) {
            const auto angle = [](int v) {
                return double(v > game::full_turn / 2 ? v - game::full_turn : v) / 80;
            };
            samples.push_back(
                {double(a.airspeed), -angle(a.attitude.pitch), angle(a.attitude.roll), double(a.y) / 256});
            game::FlightControls c;
            if (selected && tick < 30) {
                c.nose_up = selection_ == 1;
                c.right = selection_ == 2;
            }
            game::fly(a, c, game::FlightRules{}, tables_);
        }
        return samples;
    };
    neutral_ = run(false);
    flight_ = run(true);
}
void TechnicalLesson::set_canvas(int w, int h) {
    w = std::max(480, w);
    h = std::max(480, h);
    if (w == width_ && h == height_)
        return;
    const double ratio = scroll_limit() ? scroll_ / scroll_limit() : 0;
    width_ = w;
    height_ = h;
    compose();
    scroll_ = ratio * scroll_limit();
    drag_y_.reset();
}
void TechnicalLesson::compose() {
    const int w = std::min(880, width_ - 64), inner = w - 48;
    std::vector<Block> blocks;
    std::istringstream input(article_);
    std::string text;
    int y = 24;
    Image measure(1, 1);
    while (std::getline(input, text)) {
        if (text.empty())
            continue;
        Block b;
        b.text = text;
        b.y = y;
        if (text.starts_with("!figure ")) {
            b.figure = true;
            b.text = text.substr(8);
            b.height = figure(b.text, inner).height;
        } else {
            if (text.starts_with("# ")) {
                b.text = text.substr(2);
                b.size = 32;
            } else if (text.starts_with("## ")) {
                b.text = text.substr(3);
                b.size = 25;
                y += 16;
                b.y = y;
            }
            b.height = s::prose(measure, b.text, 0, 0, inner, b.size, s::ink);
        }
        y += b.height + 24;
        blocks.push_back(b);
    }
    page_ = Image(w, y + 12);
    ui::fill(page_, s::card);
    for (auto &b : blocks) {
        if (b.figure) {
            auto image = figure(b.text, inner);
            for (int row = 0; row < image.height; ++row)
                std::copy_n(image.pixels.begin() + row * inner, inner,
                            page_.pixels.begin() + (b.y + row) * w + 24);
        } else
            s::prose(page_, b.text, 24, b.y, inner, b.size, s::ink);
    }
}
Image TechnicalLesson::figure(const std::string &kind, int w) const {
    Image im(w, 360);
    ui::fill(im, s::pale);
    if (kind == "landing_pose") {
        label(im,"CONTACT LAB / a schematic, not a flight replay",16,16,s::ink);
        const auto result=landing_probe(assets_,tables_,selection_!=0,stage_);
        const int cx=w/2,cy=145,sign=selection_ ? -1 : 1;
        line(im,32,240,w-32,240,s::ink);
        label(im,"carrier deck",32,250);
        line(im,cx-100,cy,cx+100,cy,s::cobalt);
        line(im,cx-24,cy,cx,cy-sign*18,s::cobalt);
        line(im,cx,cy-sign*18,cx+24,cy,s::cobalt);
        if(stage_!=3) for(int dx:{-35,35}) {
            line(im,cx+dx,cy,cx+dx,cy+sign*24,s::cobalt);
            dot(im,cx+dx,cy+sign*30,s::ink);
        }
        dot(im,cx,cy,orange);
        line(im,cx+120,cy,cx+120,240,orange);
        label(im,"reference height",cx-64,205,orange);
        label(im,selection_?"INVERTED / wheels above":"UPRIGHT / wheels below",16,48,s::cobalt);
        label(im,"Height clamp: deck + "+number(result.clearance)+" position units",16,292);
        label(im,"Gear changes this offset. Roll does not rotate it.",16,324);
    } else if(kind=="landing_probe") {
        label(im,"ONE CONTACT STEP / production movement + deck checks",16,16,s::ink);
        const auto selected=landing_probe(assets_,tables_,selection_!=0,stage_);
        label(im,"Downward rate: "+number(selected.rate)+" / crash cutoff: 576",16,50);
        label(im,"Airspeed fixed at 1200 (100 kt); pitch and heading level.",16,76);
        const char* names[]={"Upright","Inverted"};
        for(int i=0;i<2;++i) {
            const auto p=landing_probe(assets_,tables_,i!=0,stage_);
            const int y=119+i*76;
            ui::rounded(im,16,y,w-32,64,6,i==selection_?s::paper:s::card);
            label(im,names[i],28,y+10,s::ink);
            label(im,p.crashed?"CRASH":"SURVIVES CONTACT",w/2,y+10,p.crashed?orange:s::green);
            label(im,p.deck?"Deck recognised":"Deck not recognised",28,y+36);
            label(im,p.fast_contact?"Fast belly contact":"No fast-contact flag",w/2,y+36);
        }
        label(im,"575 survives; 576 crashes at either roll angle.",16,292);
        label(im,"Controlled state: no approach, wire catch or mission result.",16,324);
    } else if(kind=="landing_rules") {
        label(im,"SEPARATE QUESTIONS / simplified contact sequence",16,16,s::ink);
        const char* rows[]={"Deck or ground? Select a reference height.",
                            "Cross that height? Clamp position to the surface.",
                            "First contact? Compare vertical rate with 576.",
                            "Carrier probes? Check deck, wire zone and hull.",
                            "Water, other objects, mission rules? Further checks."};
        for(int i=0;i<5;++i) {
            const int y=56+i*51;
            ui::rounded(im,16,y,w-32,42,5,s::paper);
            label(im,std::to_string(i+1)+" / "+rows[i],28,y+13,i==2?orange:s::ink);
        }
        label(im,"No universal rule that says: inverted means crashed.",16,324);
    } else if (kind == "plane" || kind == "order") {
        const double angle = (35 + selection_ * 45) * std::numbers::pi / 180;
        P eye{400 * std::sin(angle), selection_ % 2 ? -180.0 : 180.0, 400 * std::cos(angle)};
        const auto order =
            game::shape_draw_order(plane_, [&](const auto &p) { return facing({p[0], p[1], p[2]}, eye); },
                                   {0, 0, 0}, {eye.x, eye.y, eye.z});
        const int count = std::min(int(order.size()), stage_ + 1);
        if (kind == "order") {
            const int cols = std::max(1, (w - 32) / 94);
            const int rows = (int(order.size()) + cols - 1) / cols;
            im = Image(w, 200 + rows * 52);
            ui::fill(im, s::pale);
            label(im, "CAMERA TESTS SELECT A SEQUENCE", 16, 16, s::ink);
            s::wrap(im,
                    "View " + std::to_string(selection_ + 1) + ": " + std::to_string(plane_.tree.size()) +
                        " imported tree nodes; " + std::to_string(order.size()) + " lists in this walk.",
                    16, 48, w - 32, 3, 17, s::ink);
            for (std::size_t i = 0; i < order.size(); ++i) {
                int x = 16 + int(i) % cols * 94, y = 110 + int(i) / cols * 52;
                ui::rounded(im, x, y, 84, 36, 6, int(i) < count ? s::cobalt : s::line);
                s::text(im, "List " + std::to_string(order[i] + 1), x + 8, y + 8, 15,
                        int(i) < count ? s::white : s::ink);
            }
            s::wrap(im,
                    "Read left to right, then down. A list can contain several faces, lines or dots. A "
                    "hidden one-sided face can stop its list.",
                    16, 126 + rows * 52, w - 32, 3, 16, s::muted);
        } else {
            const double len = std::sqrt(dotp(eye, eye));
            P f{eye.x / len, eye.y / len, eye.z / len};
            P right{std::cos(angle), 0, -std::sin(angle)};
            P up = cross(f, right);
            double extent = 1;
            for (const auto &face : plane_.faces)
                for (auto p : face.corners)
                    extent = std::max({extent, std::abs(dotp(p, right)), std::abs(dotp(p, up))});
            const double scale = std::min(w / 4.0 - 26, 108.0) / extent;
            const auto draw_plane = [&](int centre, int limit) {
                int painted = 0;
                const auto project = [&](P p) {
                    return std::array{centre + int(dotp(p, right) * scale), 174 - int(dotp(p, up) * scale)};
                };
                for (int j = 0; j < limit; ++j) {
                    const int list = order[j];
                    std::vector<const data::ShapeFace *> faces;
                    for (const auto &face : plane_.faces)
                        if (face.list == list)
                            faces.push_back(&face);
                    std::sort(faces.begin(), faces.end(), [](auto a, auto b) { return a->order < b->order; });
                    for (auto face : faces) {
                        int colour = face->colour;
                        if (face->facing != data::Facing::always && !facing(face->corners, eye)) {
                            if (face->facing == data::Facing::one_sided)
                                break;
                            colour = face->back_colour;
                        }
                        std::vector<std::array<int, 2>> p;
                        for (auto v : face->corners)
                            p.push_back(project(v));
                        polygon(im, p, palette(assets_.palettes.at(0), colour));
                        ++painted;
                        for (std::size_t k = 0; k < p.size(); ++k)
                            line(im, p[k][0], p[k][1], p[(k + 1) % p.size()][0], p[(k + 1) % p.size()][1],
                                 s::muted);
                    }
                }
                if (!painted)
                    s::wrap(im, "No visible faces in these lists yet", centre - w / 4 + 16, 150, w / 2 - 32,
                            3, 16, s::muted);
            };
            label(im, "Complete face order", 16, 16, s::ink);
            label(im, "Build up: " + std::to_string(count) + " / " + std::to_string(order.size()), w / 2 + 12,
                  16, s::ink);
            draw_plane(w / 4, int(order.size()));
            draw_plane(3 * w / 4, count);
            s::wrap(im,
                    "Imported Hornet faces; shared ordering interpreter. Orthographic teaching view with "
                    "added outlines; lines/dots omitted.",
                    16, 292, w - 32, 3, 16, s::muted);
        }
    } else if (kind == "road" || kind == "records") {
        const auto &r = roads_[std::size_t(selection_) % roads_.size()];
        if (kind == "records") {
            label(im, "STORED POINTS -> STARTUP RESULTS", 16, 16, s::ink);
            label(im,
                  std::to_string(roads_.size()) + " road records / " + std::to_string(roads_.size() * 3) +
                      " generated points",
                  16, 48);
            for (int j = 0; j < 3; ++j) {
                const int y = 112 + j * 62;
                label(im, "P" + std::to_string(j + 1), 16, y, s::ink);
                label(im, "(" + std::to_string(r.before[j][0]) + ", " + std::to_string(r.before[j][1]) + ")",
                      80, y);
                label(im, "->", w / 2 - 20, y);
                label(im, "(" + std::to_string(r.after[j][0]) + ", " + std::to_string(r.after[j][1]) + ")",
                      w / 2 + 24, y, s::cobalt);
            }
            label(im, "Coordinates read from this import; x, z in model units.", 16, 318);
        } else {
            label(im,
                  "ROAD " + std::to_string(selection_ + 1) + " / STEP " + std::to_string(stage_ % 4) +
                      " OF 3",
                  16, 16, s::ink);
            std::vector<std::array<int, 2>> points{r.a, r.b};
            for (auto p : r.after)
                points.push_back(p);
            int xmin = points[0][0], xmax = xmin, zmin = points[0][1], zmax = zmin;
            for (auto p : points) {
                xmin = std::min(xmin, p[0]);
                xmax = std::max(xmax, p[0]);
                zmin = std::min(zmin, p[1]);
                zmax = std::max(zmax, p[1]);
            }
            const double scale =
                std::min(double(w / 2 - 90) / std::max(1, xmax - xmin), 170.0 / std::max(1, zmax - zmin));
            const auto pt = [&](auto p) {
                return std::array{w / 4 + int((p[0] - (xmin + xmax) / 2.0) * scale),
                                  180 - int((p[1] - (zmin + zmax) / 2.0) * scale)};
            };
            auto a = pt(r.a), b = pt(r.b);
            line(im, a[0], a[1], b[0], b[1], s::muted);
            dot(im, a[0], a[1], s::ink);
            dot(im, b[0], b[1], s::ink);
            label(im, "A", a[0] + 7, a[1] + 8);
            label(im, "B", b[0] + 7, b[1] + 8);
            for (int j = 0; j < stage_ % 4; ++j) {
                auto p = pt(r.after[j]);
                auto base = j == 2 ? b : a;
                line(im, base[0], base[1], p[0], p[1], s::cobalt);
                dot(im, p[0], p[1], orange);
                if (j == 2) // The A-end labels are separated in the magnified inset.
                    label(im, "P" + std::to_string(j + 1), p[0] + 7, p[1] - 22, orange);
            }
            label(im, "Full segment", 16, 48);
            label(im, "A-end enlarged", w / 2 + 16, 48);
            double extent = 1;
            for (int j = 0; j < 2; ++j)
                extent = std::max({extent, double(std::abs(r.after[j][0] - r.a[0])),
                                   double(std::abs(r.after[j][1] - r.a[1]))});
            const double zoom = std::min(w / 4.0 - 42, 72.0) / extent;
            const int cx = 3 * w / 4, cy = 180;
            dot(im, cx, cy, s::ink);
            label(im, "A", cx + 8, cy + 8);
            for (int j = 0; j < std::min(2, stage_ % 4); ++j) {
                int x = cx + int((r.after[j][0] - r.a[0]) * zoom),
                    y = cy - int((r.after[j][1] - r.a[1]) * zoom);
                line(im, cx, cy, x, y, s::cobalt);
                dot(im, x, y, orange);
                label(im, "P" + std::to_string(j + 1), x + 7, y - 22, orange);
            }
            s::wrap(im,
                    "Top view: x right, z up. Grey: stored endpoints. Blue: computed offsets. Orange: "
                    "generated points (not a road-surface polygon).",
                    16, 294, w - 32, 3, 16, s::muted);
        }
    } else if (kind == "response" || kind == "sink" || kind == "flight" || kind == "bank") {
        std::vector<double> a, b;
        std::string title, units, xlabel;
        if (kind == "response") {
            title = "IMPORTED CONTROL RESPONSE";
            units = "rate-table units";
            xlabel = "input magnitude 0 .. 20";
            for (int i = 0; i <= 20; ++i)
                a.push_back(tables_.player_response.at(i));
        } else if (kind == "sink") {
            title = "IMPORTED SINK TABLE";
            units = "sink-table units";
            xlabel = "airspeed bucket 0 .. 30 (128 units each)";
            for (auto v : tables_.sink_by_speed)
                a.push_back(v);
        } else {
            title = kind == "flight" ? "EIGHT SECONDS / AIRSPEED" : "EIGHT SECONDS / BANK ANGLE";
            units = kind == "flight" ? "internal speed units" : "degrees (signed)";
            xlabel = "seconds: 0                          4                          8";
            for (auto v : flight_)
                a.push_back(kind == "flight" ? v.speed : v.bank);
            for (auto v : neutral_)
                b.push_back(kind == "flight" ? v.speed : v.bank);
        }
        label(im, title, 16, 16, s::ink);
        label(im, units, 16, 45);
        double lo = *std::min_element(a.begin(), a.end()), hi = *std::max_element(a.begin(), a.end());
        for (auto v : b) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (hi - lo < 1) {
            lo -= 1;
            hi += 1;
        }
        const int left = 76, right = w - 24, top = 90, bottom = 265;
        line(im, left, top, left, bottom, s::line);
        line(im, left, bottom, right, bottom, s::line);
        label(im, number(hi), 16, top - 8);
        label(im, number(lo), 16, bottom - 10);
        const auto plot = [&](const auto &v, Color c) {
            for (std::size_t i = 1; i < v.size(); ++i)
                line(im, left + int((right - left) * (i - 1) / double(v.size() - 1)),
                     bottom - int((v[i - 1] - lo) / (hi - lo) * (bottom - top)),
                     left + int((right - left) * i / double(v.size() - 1)),
                     bottom - int((v[i] - lo) / (hi - lo) * (bottom - top)), c);
        };
        plot(b, s::muted);
        plot(a, s::cobalt);
        if (b.empty())
            label(im, xlabel, 16, 282);
        else {
            label(im, "0 s", left, 282);
            label(im, "4 s", (left + right) / 2 - 10, 282);
            label(im, "8 s", right - 20, 282);
        }
        if (!b.empty())
            s::wrap(im,
                    "Blue: selected input for 2 s, then release. Grey: neutral. Same starting state and "
                    "throttle; actual reconstructed physics, 15 ticks/s.",
                    16, 310, w - 32, 3, 15, s::muted);
        else
            label(im, "Imported samples, joined for readability; not a continuous formula.", 16, 320);
    } else
        throw std::runtime_error("Unknown Interceptor figure: " + kind);
    return im;
}
void TechnicalLesson::update(const platform::Controls &in, double) {
    const auto hit = [&](int x, int w) {
        return in.click && in.mouse_x >= x && in.mouse_x < x + w && in.mouse_y >= 84 && in.mouse_y < 122;
    };
    bool changed = false;
    if (hit(24, 210) || in.nav_right || in.nav_left) {
        const int count = chapter_ == 0 ? 8 : chapter_ == 1 ? int(roads_.size()) : chapter_ == 3 ? 2 : 3;
        selection_ = (selection_ + (in.nav_left ? count - 1 : 1)) % count;
        changed = true;
    }
    if (hit(250, 210) || (in.fire && !fire_was_)) {
        const int count = chapter_ == 0 ? std::max(1, plane_.lists) : chapter_ == 1 || chapter_ == 3 ? 4 : 2;
        stage_ = (stage_ + 1) % count;
        changed = true;
    }
    fire_was_ = in.fire;
    if (changed) {
        simulate();
        compose();
    }
    scroll_ += (in.wheel_precise != 0 ? in.wheel_precise : double(in.wheel)) * 56 +
               (int(in.nav_down) - int(in.nav_up)) * 44 +
               (int(in.page_down) - int(in.page_up)) * (height_ - 220);
    const int x = (width_ - page_.width) / 2;
    if (in.click && in.mouse_x >= x && in.mouse_x < x + page_.width && in.mouse_y >= 140 &&
        in.mouse_y < height_ - 44)
        drag_y_ = in.mouse_y;
    if (drag_y_ && in.ui_pointer && in.mouse_held) {
        scroll_ += *drag_y_ - (*in.ui_pointer)[1];
        drag_y_ = (*in.ui_pointer)[1];
    }
    if (!in.mouse_held)
        drag_y_.reset();
    scroll_ = std::clamp(scroll_, 0.0, double(scroll_limit()));
}
Image TechnicalLesson::draw() const {
    Image im(width_, height_);
    ui::fill(im, s::paper);
    s::button(im, "< Lessons", 24, 20, 104, 34);
    s::text(im, "F/A-18 Interceptor", 148, 22, 23, s::ink, true);
    const char *headings[] = {"01 / PAINTING THE HORNET", "02 / BUILDING THE ROADS", "03 / THE FLIGHT MODEL", "04 / LANDING OR CRASHING"};
    label(im, headings[chapter_], 24, 62);
    const char *inputs[] = {"Neutral", "Pull up", "Bank right"};
    const auto first =
        chapter_ == 0 ? "View " + std::to_string(selection_ + 1) + (selection_ % 2 ? " / below" : " / above")
        : chapter_ == 1 ? "Road " + std::to_string(selection_ + 1) + " / " + std::to_string(roads_.size())
                        : chapter_ == 3 ? std::string(selection_ ? "Inverted" : "Upright")
                        : std::string("Input: ") + inputs[selection_];
    const char* contact_settings[]={"Soft: 384", "Edge: 575", "Hard: 576", "Gear up: 384"};
    const auto second = chapter_ == 0   ? "Next layer"
                        : chapter_ == 1 ? "Step " + std::to_string(stage_ % 4) + " / 3"
                                        : chapter_ == 3 ? std::string(contact_settings[stage_])
                                        : "Throttle: " + std::to_string(stage_ % 2 ? 120 : 60);
    s::button(im, first, 24, 84, 210, 38);
    s::button(im, second, 250, 84, 210, 38);
    const int x = (width_ - page_.width) / 2, visible = height_ - 184;
    for (int y = 0; y < visible && y + int(scroll_) < page_.height; ++y)
        std::copy_n(page_.pixels.begin() + (y + int(scroll_)) * page_.width, page_.width,
                    im.pixels.begin() + (140 + y) * width_ + x);
    if (scroll_limit()) {
        int thumb = std::max(24, visible * visible / page_.height);
        ui::rect(im, x + page_.width + 8, 140, 4, visible, s::line);
        ui::rect(im, x + page_.width + 8, 140 + int((visible - thumb) * scroll_ / scroll_limit()), 4, thumb,
                 s::muted);
    }
    s::clipped(im, "Swipe / Up-Down: read   Left-Right: select   Fire: next setting", 24, height_ - 28,
               width_ - 48, 14, s::muted);
    return im;
}
} // namespace interceptor::lessons
