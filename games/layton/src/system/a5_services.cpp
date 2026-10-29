// The fifth adventure's script services: resources, objects, texts, flags
// and variables (library opcodes 1000..6988, game opcodes 2104..16571).
// Each comment names the handler (from the service tables the game builds
// at run time, .analysis/adventure56/research/l5-handler-tables.tsv) and the
// object method it calls. See .analysis/adventure56/docs/scene.md.
#include "graphics/a56_camera.hpp"
#include "graphics/a56_font.hpp"
#include "graphics/a56_model.hpp"
#include "graphics/a56_text.hpp"
#include "import/asset_codecs.hpp"
#include "system/a56_movie.hpp"
#include "system/a5_game.hpp"
#include "system/a5_saves.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace picarat::adventure5 {

namespace fs = std::filesystem;
using adventure56::Object;
using adventure56::Resource;
using xq::Call;
using xq::Value;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

using adventure56::wrapAngle;

// 0x2d4b94: an object or resource reference: a name (hashed) or the hash.
std::uint32_t reference(const Call& c, std::size_t i) {
    const Value& v = c.at(i);
    if (v.isText()) {
        return xq::hash(v.text);
    }
    return v.toUint();
}

std::string textOf(const Call& c, std::size_t i) {
    const std::string* t = c.text(i);
    return t ? *t : std::string();
}

// A loader's file (3402 .xi, 3502 .xa, 3602 .xp, 3702 .xc): the loader
// decides the extension by its kind, so a path naming another one (puzzle
// 008 loads animation sets as ".xc") gets the loader's own.
fs::path findOfKind(const adventure56::Pack& pack, const std::string& path,
                    const std::string& extension) {
    fs::path file = pack.find(path, extension);
    if (file.empty() && !fs::path(path).extension().empty()) {
        file = pack.find(fs::path(path).replace_extension(extension).string(), extension);
    }
    return file;
}

// A block named by operand i: its name, or (the sixth adventure's scripts)
// its hash, resolved through the loaded scripts from the newest one.
std::string blockName(const xq::Program* newest, const Call& c, std::size_t i) {
    if (i >= c.count() || c.at(i).isText()) {
        return textOf(c, i);
    }
    const std::uint32_t h = reference(c, i);
    for (const xq::Program* p = newest; p && h; p = p->next) {
        if (const auto* b = p->block(h)) {
            return b->name;
        }
    }
    return {};
}

std::array<float, 3> vec(const Call& c, std::size_t i, std::size_t n, float rest = 0) {
    std::array<float, 3> v{rest, rest, rest};
    for (std::size_t k = 0; k < n && k < 3; ++k) {
        v[k] = c.real(i + k);
    }
    return v;
}

bool done(Call& c, std::int32_t v = 1) {
    c.result = Value::ofInt(v);
    return true;
}

} // namespace

void Game::registerLibraryServices() {
    auto& m = machine;
    auto object = [this](Call& c) -> Object* {
        return scene.object(reference(c, 0));
    };

    // ------------------------------------------------- sprite batches
    // The sixth adventure's sprite batch on an animation object (content 9;
    // fifth adventure: the same numbers): the object draws copies of its
    // set's cells at local positions, kept as Object::pieces.
    // 3570 (0x18a9b0 -> 0x1c9e04 -> 0x245e58): start a batch (object [,
    // capacity, default 72]); 3571 (0x18190c -> 0x259a58 / 0x259a3c): add
    // the cell named (hash) at x, y (object, cell, x, y, index [, z]) -
    // refused when the set has no such cell or the batch is full (all
    // cells must share one material; the sets used have one); 3572
    // (0x17df34 -> 0x25a0c8 -> 0x1deb2c): build the batch (the original
    // draws the built list; the scripts build right after adding, so the
    // pieces are drawn as added); 3573 (0x191dbc -> 0x2563cc -> 0x24cfac):
    // the built entry [index]'s offset x, y from where it was added
    // (object, index, x, y [, z]; puzzle 073 shows its pieces with 0, 0 and
    // hides them with 0, -1000, as the reference shows), entries counted in
    // the order added. Hypothesis: the build keeps that
    // order (the index operand of 3571 equals it in every script).
    auto batchObject = [this](Call& c) -> Object* {
        Object* o = scene.object(reference(c, 0));
        return o && o->animation && o->content == 9 ? o : nullptr;
    };
    m.service(3570, [batchObject](Call& c) {
        Object* o = c.count() > 0 ? batchObject(c) : nullptr;
        if (!o) {
            return done(c, 0);
        }
        o->pieces.clear();
        o->pieceCapacity = c.count() > 1 ? c.integer(1) : 72;
        return done(c, 1);
    });
    m.service(3571, [batchObject](Call& c) {
        Object* o = c.count() >= 5 ? batchObject(c) : nullptr;
        if (!o || o->pieceCapacity <= 0 || int(o->pieces.size()) >= o->pieceCapacity) {
            return done(c, 0);
        }
        const std::uint32_t h = reference(c, 1);
        for (std::size_t i = 0; i < o->animation->cells.size(); ++i) {
            if (o->animation->cells[i].hash == h) {
                o->pieces.push_back({int(i), c.real(2), c.real(3)});
                return done(c, 1);
            }
        }
        return done(c, 0);
    });
    m.service(3572,
              [batchObject](Call& c) { return done(c, c.count() > 0 && batchObject(c) ? 1 : 0); });
    m.service(3573, [batchObject](Call& c) {
        Object* o = c.count() >= 4 ? batchObject(c) : nullptr;
        const int i = c.count() >= 4 ? c.integer(1) : -1;
        if (!o || i < 0 || i >= int(o->pieces.size())) {
            return done(c, 0);
        }
        o->pieces[std::size_t(i)].dx = c.real(2);
        o->pieces[std::size_t(i)].dy = c.real(3);
        return done(c, 1);
    });
    // ---------------------------------------------------------- line sets
    // The sixth adventure's line set (Object::LineSet; fifth adventure: the
    // same numbers, unused by its puzzles). Operand forms by count, as the
    // handlers read them (sixth adventure addresses).
    auto lineSet = [this](Call& c) -> Object::LineSet* {
        Object* o = scene.object(reference(c, 0));
        return o && o->lines ? o->lines.get() : nullptr;
    };
    // 5970 / 5971 (0x1aef7c / 0x1af060 -> 0x24eca8 / 0x1c970c -> 0x1c2c7c):
    // the object becomes a line set for `capacity` segments [, a switch];
    // 5971's segments repeat the texture along their length.
    for (const bool tiled : {false, true}) {
        m.service(tiled ? 5971 : 5970, [this, tiled](Call& c) {
            Object* o = scene.object(reference(c, 0));
            if (!o || c.count() < 2) {
                return done(c, 0);
            }
            o->lines = std::make_shared<Object::LineSet>();
            o->lines->tiled = tiled;
            o->lines->capacity = c.integer(1);
            return done(c, 1);
        });
    }
    // 5972 (0x199e60 -> 0x19a4dc -> 0x1cb7e4 -> 0x1c29ac): the build starts
    // (once): [x, y [, z]], [width], [picture, u0, v0, u1, v1]. The parts
    // present depend on the count of operands after the object n: x, y for
    // n >= 3 except 5; z for 4 and 9; the width for n >= 3 except 5; the
    // picture with its rect for 5, 8 and 9. The width must be positive.
    m.service(5972, [this, lineSet](Call& c) {
        auto* set = lineSet(c);
        const std::size_t n = c.count() - 1;
        if (!set || set->building) {
            return done(c, 0);
        }
        std::size_t i = 1;
        float x = 0, y = 0, z = 0, width = 0;
        if (n >= 3 && n != 5) {
            x = c.real(i), y = c.real(i + 1);
            i += 2;
        }
        if (n == 4 || n == 9) {
            z = c.real(i++);
        }
        if (n >= 3 && n != 5) {
            width = c.real(i++);
        }
        std::shared_ptr<ctr3d::Texture> picture;
        float uv[4] = {0, 0, 1, 1};
        if (n == 5 || n == 8 || n == 9) {
            if (Resource* r = scene.resource(reference(c, i)); r && r->picture) {
                picture = r->picture;
            }
            for (int k = 0; k < 4; ++k) {
                uv[k] = c.real(i + 1 + std::size_t(k));
            }
        }
        // Without operands (sixth adventure 070's road lines) the build
        // starts at 0, 0 with no width; the segments (5973's long form)
        // bring their own.
        if ((n > 0 && width <= 0) || (set->tiled && !picture)) {
            return done(c, 0);
        }
        set->last[0] = x, set->last[1] = y, set->last[2] = z;
        set->width = width;
        set->picture = picture;
        std::copy(uv, uv + 4, set->uv);
        set->building = true;
        return done(c, 1);
    });
    // 5973 (0x199e04 -> 0x19a25c -> 0x25236c -> 0x1c2620): a segment from
    // the last point to x, y [, z] while building: (x, y [, z], r, g, b, a)
    // for under nine operands, else a segment between two points (below).
    // The end point is the next segment's start.
    m.service(5973, [lineSet](Call& c) {
        auto* set = lineSet(c);
        const std::size_t n = c.count() - 1;
        if (!set || n < 5 || !set->building ||
            int(set->segments.size()) >= std::max(set->capacity, 0)) {
            return done(c, 0);
        }
        Object::LineSet::Segment seg;
        float p[3] = {c.real(1), c.real(2), 0};
        std::size_t i = 3;
        float width = 0;
        if (n < 9) {
            if (n == 7) {
                p[2] = c.real(i++);
            }
        } else {
            // 0x19a25c -> 0x25236c: (x0, y0 [, z0], x1, y1 [, z1], width,
            // r, g, b, a), a segment from the first point (where a
            // positive width restarts the line) to the second.
            if (n == 11) {
                p[2] = c.real(i++);
            }
            float to[3] = {c.real(i), c.real(i + 1), 0};
            i += 2;
            if (n == 11) {
                to[2] = c.real(i++);
            }
            width = c.real(i++);
            if (width > 0) {
                std::copy(p, p + 3, set->last);
                set->width = width;
            }
            std::copy(to, to + 3, p);
            width = 0; // the restart is done
        }
        for (int k = 0; k < 4; ++k) {
            seg.colour[k] = c.real(i + std::size_t(k));
        }
        if (width > 0) {
            std::copy(p, p + 3, set->last);
            set->width = width;
        }
        std::copy(set->last, set->last + 3, seg.p[0]);
        std::copy(p, p + 3, seg.p[1]);
        std::copy(&seg.p[0][0], &seg.p[0][0] + 6, &seg.shown[0][0]);
        seg.width = set->width;
        set->segments.push_back(seg);
        std::copy(p, p + 3, set->last);
        return done(c, 1);
    });
    // 5980 (sixth adventure 0x1a9708 -> 0x1c912c): the segment under a
    // screen point (object, x, y; Scene::lineSegmentAt), -1 for none.
    m.service(5980, [this, object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return done(c, 0);
        }
        return done(c, scene.lineSegmentAt(*o, c.real(1), c.real(2)));
    });
    // 5974 (0x199fcc -> 0x2523dc): the build ends.
    m.service(5974, [lineSet](Call& c) {
        auto* set = lineSet(c);
        if (!set || !set->building) {
            return done(c, 0);
        }
        set->building = false;
        return done(c, 1);
    });
    // 5976 (0x1a98b4 -> 0x1a9e04 -> 0x24f9b8): a segment's end (segment,
    // end 0 / 1, x, y [, z]); drawn there from the next update.
    m.service(5976, [lineSet](Call& c) {
        auto* set = lineSet(c);
        if (!set || c.count() < 5) {
            return false;
        }
        const int i = c.integer(1), e = c.integer(2);
        if (i < 0 || i >= int(set->segments.size()) || e < 0 || e > 1) {
            return done(c, 0);
        }
        auto& p = set->segments[std::size_t(i)].p[e];
        p[0] = c.real(3), p[1] = c.real(4);
        if (c.count() > 5) {
            p[2] = c.real(5);
        }
        if (set->autoUpdate) {
            std::copy(p, p + 3, set->segments[std::size_t(i)].shown[e]);
        }
        return done(c, 1);
    });
    // 5977 (0x1a9614 -> 0x24f9d4 -> 0x247cbc): a segment's colour (segment,
    // r, g, b, a) for all four corners, at once.
    m.service(5977, [lineSet](Call& c) {
        auto* set = lineSet(c);
        if (!set || c.count() < 6) {
            return false;
        }
        const int i = c.integer(1);
        if (i < 0 || i >= int(set->segments.size())) {
            return done(c, 0);
        }
        for (int k = 0; k < 4; ++k) {
            set->segments[std::size_t(i)].colour[k] = c.real(2 + std::size_t(k));
        }
        return done(c, 1);
    });
    // 5986 (0x1b4020 -> 0x1ca420): the segments follow their ends every
    // frame (+0xcd).
    m.service(5986, [lineSet](Call& c) {
        auto* set = lineSet(c);
        if (!set || c.count() < 2) {
            return done(c, 0);
        }
        set->autoUpdate = c.at(1).truthy();
        return done(c, 1);
    });
    // 5988 (0x1b1dac -> 0x1ca73c / 0x1c9ee8): update every segment (or the
    // one given) to its ends now.
    m.service(5988, [lineSet](Call& c) {
        auto* set = lineSet(c);
        if (!set) {
            return done(c, 0);
        }
        for (std::size_t i = 0; i < set->segments.size(); ++i) {
            if (c.count() > 1 && int(i) != c.integer(1)) {
                continue;
            }
            auto& seg = set->segments[i];
            std::copy(&seg.p[0][0], &seg.p[0][0] + 6, &seg.shown[0][0]);
        }
        return done(c, 1);
    });
    // ---------------------------------------------------------- resources
    // 3201 / 3401 / 3501 (sixth adventure 0x175ca0 / 0x1751f0 / 0x174ac8
    // -> 0x25c9e8 / 0x25ced4 / 0x25d46c): a text list / picture / animation
    // set by name or hash is released (a binary search of that kind's list,
    // then the record's release under the list's lock); result 1 when it
    // was there.
    for (const auto& [op, kind] : {std::pair<int, Resource::Kind>{3201, Resource::Kind::Text},
                                   {3401, Resource::Kind::Picture},
                                   {3501, Resource::Kind::Animation}}) {
        m.service(op, [this, kind](Call& c) {
            if (c.count() < 1) {
                return false;
            }
            const std::uint32_t h = reference(c, 0);
            if (!scene.resource(h, kind)) {
                return done(c, 0);
            }
            scene.removeResource(h, kind);
            return done(c, 1);
        });
    }
    // 3402 (0x12a1a0): a picture resource "name", "path" (.xi).
    m.service(3402, [this](Call& c) {
        const fs::path file = findOfKind(pack, textOf(c, 1), ".xi");
        if (file.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Picture;
        r.path = textOf(c, 1);
        r.picture = adventure56::loadTexture(pack, file);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3414 (0x18f430 -> 0x197608): a picture "name" that is a render
    // target: screen, width, height [, flag] (puzzle 017: "img_frame" 0 400
    // 240 1); refused when the name exists. Starts transparent. 3410
    // (0x17b994 -> 0x183564, the same steps; puzzle 068: "img_frame" 0 320
    // 240) likewise; the creators differ in the flag (hypothesis).
    auto renderTarget = [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        const int w = c.integer(2), h = c.integer(3);
        if (scene.resource(xq::hash(textOf(c, 0))) || w <= 0 || h <= 0 || w > 1024 || h > 1024) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Picture;
        r.path = textOf(c, 0);
        r.renderTarget = true;
        r.picture = std::make_shared<ctr3d::Texture>();
        r.picture->width = w;
        r.picture->height = h;
        r.picture->pixels.assign(std::size_t(w) * std::size_t(h), platform::Color{0, 0, 0, 0});
        r.picture->linear = false;
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    };
    m.service(3410, renderTarget);
    m.service(3414, renderTarget);
    // 5440 (0x17c940 -> 0x25a5a0 -> 0x1cb214 / 0x2956a8): the object
    // becomes a capture of its screen into a render-target picture (object,
    // picture [, count, default 1]); refused for other pictures (+0x8d7) and
    // count 0. 5441 (0x190364 -> 0x36e6b4): its state (1 once captured).
    m.service(5440, [this](Call& c) {
        Object* o = c.count() > 1 ? scene.object(reference(c, 0)) : nullptr;
        Resource* r = o ? scene.resource(reference(c, 1)) : nullptr;
        const int count = c.count() > 2 ? c.integer(2) : 1;
        if (!o || !r || r->kind != Resource::Kind::Picture || !r->renderTarget || !r->picture ||
            count == 0) {
            return done(c, 0);
        }
        o->capture = std::make_shared<adventure56::Capture>();
        o->capture->picture = r->picture;
        o->capture->count = count;
        return done(c, 1);
    });
    m.service(5441, [this](Call& c) {
        const Object* o = c.count() ? scene.object(reference(c, 0)) : nullptr;
        return done(c, o && o->capture ? o->capture->state : 0);
    });
    // 3412 (0x121748 -> 0x2d5ba0): a drawing canvas "name", format,
    // width, height (the memo's 312 x 204 sheet); it starts transparent.
    m.service(3412, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        const int w = c.integer(2), h = c.integer(3);
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Picture;
        r.path = textOf(c, 0);
        r.picture = std::make_shared<ctr3d::Texture>();
        r.picture->width = w;
        r.picture->height = h;
        r.picture->pixels.assign(std::size_t(w) * std::size_t(h), platform::Color{0, 0, 0, 0});
        r.picture->linear = false;
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3502 (0x129364): an animation set "name", "path" (.xa).
    m.service(3502, [this](Call& c) {
        const fs::path folder = findOfKind(pack, textOf(c, 1), ".xa");
        if (folder.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Animation;
        r.path = textOf(c, 1);
        r.animation = adventure56::loadAnimationSet(pack, folder);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3202 (0x12afb0 -> 0x2ce84c): a text list "name", folder, number:
    // txt/@/<folder>/<folder>_<number>.xs.
    m.service(3202, [this](Call& c) {
        char path[96];
        std::snprintf(path, sizeof path, "txt/@/%02d/%02d_%06d.xs", c.integer(1), c.integer(1),
                      c.integer(2));
        const fs::path file = pack.find(path, "");
        if (file.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Text;
        r.path = path;
        r.text = adventure56::loadTextList(pack, file);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3210 (0x12b1c4 -> 0x352340): the pages of a text list's key.
    m.service(3210, [this](Call& c) {
        Resource* r = scene.resource(reference(c, 0));
        if (!r || !r->text || c.count() < 2) {
            return false;
        }
        return done(c, int(r->text->pages(c.integer(1))));
    });
    // 3211 (0x12b238 -> 0x2ce6a0): a string of a text list's row (list,
    // key, page, field: 0 the speaker's name, 1 the text).
    m.service(3211, [this](Call& c) {
        Resource* r = scene.resource(reference(c, 0));
        if (!r || !r->text) {
            return false;
        }
        const std::string* t =
            r->text->field(c.integer(1), c.count() > 2 ? unsigned(std::max(0, c.integer(2))) : 0u,
                           c.count() > 3 ? c.integer(3) : 1);
        c.result = Value::ofText(t ? *t : std::string());
        return true;
    });
    // 3430 (0x13efcc -> 0x2c926c): a picture's blend mode (byte +0x46c of
    // its render state). Mode 7 multiplies (the title card's iris mask:
    // white shows the scene, black hides it); others are drawn as usual.
    m.service(3430, [this](Call& c) {
        Resource* r = scene.resource(reference(c, 0));
        if (!r || !r->picture) {
            return false;
        }
        r->blend = c.integer(1);
        return done(c);
    });
    // 3432 (sixth adventure 0x18cacc -> 0x2574e8): a picture's render
    // state, the record at +0x85c whose byte 0 is 3430's blend mode: byte
    // 1 = the second operand, with five or six operands also two or three
    // floats. The scripts pass (picture, 5). Accepted; what byte 1 changes
    // in drawing is not known yet.
    m.service(3432, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const Resource* r = scene.resource(reference(c, 0));
        return done(c, r && r->picture ? 1 : 0);
    });
    // 3302 (0x12ad64 -> 0x2d2f48): a sky "name", "sky/x" (.xk); only its
    // lights are read (LGT: count, then per light 28 floats: ambient 0..2,
    // diffuse 4..6, specular 8..10, the direction 12..14).
    m.service(3302, [this](Call& c) {
        const fs::path folder = c.count() > 1 ? pack.find(textOf(c, 1), ".xk") : fs::path{};
        if (folder.empty()) {
            return done(c, 0);
        }
        auto sky = std::make_shared<adventure56::Sky>();
        for (unsigned i = 0;; ++i) {
            char name[16];
            std::snprintf(name, sizeof name, "%03u.lgt", i);
            if (!fs::exists(folder / name)) {
                break;
            }
            const Bytes d = import::decompressMethodWord(pack.read(folder / name));
            auto f = [&](std::size_t k) {
                float v = 0;
                if (4 + k * 4 + 4 <= d.size()) {
                    std::memcpy(&v, d.data() + 4 + k * 4, 4);
                }
                return v;
            };
            const std::uint32_t count = d.size() >= 4 ? std::uint32_t(d[0] | d[1] << 8) : 0;
            for (std::uint32_t l = 0; l < count; ++l) {
                adventure56::Sky::Light light;
                const std::size_t b = l * 28;
                for (int k = 0; k < 3; ++k) {
                    light.ambient[k] = f(b + std::size_t(k));
                    light.diffuse[k] = f(b + 4 + std::size_t(k));
                    light.specular[k] = f(b + 8 + std::size_t(k));
                    light.direction[k] = f(b + 12 + std::size_t(k));
                }
                sky->lights.push_back(light);
            }
        }
        Resource r;
        r.path = textOf(c, 1);
        r.sky = std::move(sky);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3322 (0x12a3d8): a light resource "name", "path" (.xl: LGT.lgt,
    // a spot's angle table). Kept by name for the loops that read it (the
    // rabbit show, 799); its contents are not decoded yet.
    m.service(3322, [this](Call& c) {
        const fs::path folder = c.count() > 1 ? pack.find(textOf(c, 1), ".xl") : fs::path{};
        if (folder.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.path = textOf(c, 1);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3310 (0x124208 -> 0x32bb10): a screen uses a sky (screen, name).
    m.service(3310, [this](Call& c) {
        const int screen = c.integer(0);
        Resource* r = c.count() > 1 ? scene.resource(reference(c, 1)) : nullptr;
        if (screen < 0 || screen >= adventure56::Scene::kScreens || !r || !r->sky) {
            return done(c, 0);
        }
        scene.screenSky[screen] = r->sky;
        return done(c);
    });
    // 3030 (0x1225ac -> 0x32e504 / 0x32e5d4): an offscreen screen on/off.
    m.service(3030, [this](Call& c) {
        const int screen = c.integer(0);
        if (screen < 0 || screen >= adventure56::Scene::kScreens || c.count() < 2) {
            return done(c, 0);
        }
        scene.screenEnabled[screen] = c.at(1).truthy();
        return done(c);
    });
    // 3051 (0x12abf0 -> 0x2cedcc) {screen, on [, kind]}: the top / bottom
    // screen keeps its last frame for 5430 (Scene::screenKept). The third
    // operand picks the target's kind (0x2d381c or 0x1a1010; not modelled).
    m.service(3051, [this](Call& c) {
        const int screen = c.integer(0);
        if (c.count() < 2) {
            return false;
        }
        if (screen == 0 || screen == 1) {
            scene.screenKept[screen] = c.at(1).truthy();
        }
        return done(c);
    });
    // 2400 (0x12342c -> 0x2d3af8 / 0x2d3a84): the least number of display
    // frames per game frame (1..6; the verdict runs at 2). The engine
    // raises it to 3 when a frame takes too long (0x1c5a38); 2120 reports
    // the ratio, 1 here.
    m.service(2400, [this](Call& c) {
        displayFramesPerFrame = std::clamp(c.integer(0), 1, 6);
        return done(c);
    });
    // 3408 (0x12fb6c): whether a resource is loaded.
    m.service(3408, [this](Call& c) { return done(c, scene.resource(reference(c, 0)) ? 1 : 0); });
    // 3510 (sixth adventure 0x17b430 -> 0x260250): whether an animation set
    // of that name is loaded (the animation sets' list).
    m.service(3510, [this](Call& c) {
        if (c.count() < 1) {
            return false;
        }
        return done(c, scene.resource(reference(c, 0), Resource::Kind::Animation) ? 1 : 0);
    });
    // 5629 (sixth adventure 0x18fe88 -> 0x36e690): an animation object's
    // current frame (+0xac, as an unsigned integer); 0 for other objects.
    m.service(5629, [this](Call& c) {
        if (c.count() < 1) {
            return false;
        }
        const Object* o = scene.object(reference(c, 0));
        c.result = Value::ofUint(o && o->animation ? o->frame : 0u);
        return true;
    });
    // 3433 (0x12f6f8): whether a file exists.
    m.service(3433,
              [this](Call& c) { return done(c, pack.find(textOf(c, 0), "").empty() ? 0 : 1); });

    // ------------------------------------------------------------ objects
    // 5002 (0x125004 -> 0x2d1c34): create "name", screen, priority, kind
    // [, more]; the result is the object's hash.
    m.service(5002, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        // 0x2d1d1c: up to sixteen operands become the object's values.
        std::vector<Value> values;
        for (std::size_t i = 0; i < c.count() && i < 16; ++i) {
            values.push_back(c.at(i));
        }
        Object& o = createObject(textOf(c, 0), c.integer(1), c.integer(2), c.integer(3), values);
        c.result = Value::ofUint(o.hash);
        return true;
    });
    // 5003 (0x131c3c -> 0x2d1c34): 5002 for objects that do not belong to
    // a scope level of their own (second argument 0); the result is the
    // object's hash (0 when it failed).
    m.service(5003, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        std::vector<Value> values;
        for (std::size_t i = 0; i < c.count() && i < 16; ++i) {
            values.push_back(c.at(i));
        }
        Object& o = createObject(textOf(c, 0), c.integer(1), c.integer(2), c.integer(3), values, 0);
        c.result = Value::ofUint(o.hash);
        return true;
    });
    // 5022 (0x12be00): an object's priority (+0x434; under a parent, the
    // parent's list order, 0x2ce198).
    m.service(5022, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->priority) : false;
    });
    // 5050 (0x12d9e8 -> 0x2cd770): the object's touch areas go.
    m.service(5050, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        o->areas.clear();
        return done(c);
    });
    // 5001 (0x12512c): delete an object.
    m.service(5001, [this](Call& c) {
        scene.remove(reference(c, 0));
        return done(c);
    });
    // 5005 (0x1321d0, vtable +0x0c): show / hide.
    m.service(5005, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->visible = c.at(1).truthy();
        return done(c);
    });
    // 5006 (0x132250, vtable +0x10 = 0x177444): the touch switch.
    m.service(5006, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->touchOn = c.at(1).truthy();
        return done(c);
    });
    // 5009 (0x12bd48): whether all named objects exist.
    m.service(5009, [this](Call& c) {
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (!scene.object(reference(c, i))) {
                return done(c, 0);
            }
        }
        return done(c, 1);
    });
    // Tweened properties: set at once / over frames along a curve.
    // 5242 position (0x34c, 0x2aeda4), 5243 position tween (x, y, frames,
    // curve), 5244 tween relative to the current position; 5247 offset
    // (0x37c), 5248 offset tween; 5202 scale (0x1d0), 5203 scale tween;
    // 5227 rotation (0x2b4), 5228 rotation tween; 5102 alpha (0x94), 5103
    // alpha tween; 5112 colour (0xb8), 5113 colour tween.
    m.service(5242, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        auto v = o->position.value();
        v[0] = c.real(1);
        v[1] = c.real(2);
        o->position.set(v);
        return done(c);
    });
    m.service(5243, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        auto v = o->position.value();
        v[0] = c.real(1);
        v[1] = c.real(2);
        o->position.start(v, c.real(3), c.integer(4));
        return done(c);
    });
    m.service(5244, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        auto v = o->position.target();
        v[0] += c.real(1);
        v[1] += c.real(2);
        o->position.start(v, c.real(3), c.integer(4));
        return done(c);
    });
    // 5246: the offset into two registers.
    m.service(5246, [object](Call& c) {
        Object* o = object(c);
        for (std::size_t i = 0; i < 2; ++i) {
            if (Value* r = c.count() > 1 + i ? c.reg(1 + i) : nullptr) {
                *r = Value::ofFloat(o ? o->offset[int(i)] : 0.0f);
            }
        }
        return done(c, o != nullptr);
    });
    m.service(5247, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->offset.set({c.real(1), c.real(2), 0});
        return done(c);
    });
    m.service(5248, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->offset.start({c.real(1), c.real(2), 0}, c.real(3), c.integer(4));
        return done(c);
    });
    m.service(5202, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->scale.set({c.real(1), c.real(2), c.count() > 3 ? c.real(3) : o->scale[2]});
        return done(c);
    });
    m.service(5203, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        o->scale.start({c.real(1), c.real(2), o->scale[2]}, c.real(3),
                       c.count() > 4 ? c.integer(4) : 0);
        return done(c);
    });
    m.service(5227, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->rotation.set({c.real(1)});
        return done(c);
    });
    m.service(5228, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        o->rotation.start({c.real(1)}, c.real(2), c.integer(3));
        return done(c);
    });
    // Stepped rotation (the tween at 0x284 with its steps at +0x24..+0x2c).
    // 5235 (0x144470 -> 0x175c8c): steps and snap frames (0 steps: off).
    m.service(5235, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->steps = std::max(0, c.integer(1));
        o->stepFrames = o->steps ? c.real(2) : 0.0f;
        o->stepIndex = 0;
        return done(c);
    });
    // The step nearest an angle (0x2af734: rounded half away from zero,
    // wrapped into 0..steps-1).
    auto stepOf = [](const Object& o, float angle) {
        const float size = kTwoPi / float(o.steps);
        const float a = angle >= 0 ? angle + size * 0.5f : angle - size * 0.5f;
        const int i = int(a / size) % o.steps;
        return i < 0 ? i + o.steps : i;
    };
    // 5236 (0x148d8c): snap to the step nearest the given angle, or the
    // current one (0x2af6e4); the turn takes the short way round (0x2af580)
    // with curve 2 over the step frames.
    m.service(5236, [object, stepOf](Call& c) {
        Object* o = object(c);
        if (!o || o->steps == 0) {
            return done(c, 0);
        }
        const int i = stepOf(*o, c.count() > 1 ? c.real(1) : o->stepAngle[0]);
        o->stepIndex = i;
        const float to = wrapAngle(kTwoPi * float(i) / float(o->steps));
        float from = wrapAngle(o->stepAngle[0]);
        if (std::fabs(to - from) > kPi) {
            from += to - from >= 0 ? kTwoPi : -kTwoPi;
        }
        o->stepAngle.set({from});
        o->stepAngle.start({to}, o->stepFrames, 2);
        return done(c);
    });
    // 5237 (0x138c3c -> 0x33d77c): the step an angle falls on.
    m.service(5237, [object, stepOf](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        c.result = Value::ofUint(o->steps ? std::uint32_t(stepOf(*o, c.real(1))) : 0u);
        return true;
    });
    // 5238 (0x138cb0 -> 0x2ca1a4): the current step.
    m.service(5238, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        c.result = Value::ofUint(std::uint32_t(o->stepIndex));
        return true;
    });
    // 5239 (0x138d08 -> 0x175bac): turn to a step at once.
    m.service(5239, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2 || o->steps == 0) {
            return done(c, 0);
        }
        o->stepIndex = int(c.unsignedInt(1) % std::uint32_t(o->steps));
        o->stepAngle.set({wrapAngle(kTwoPi * float(o->stepIndex) / float(o->steps))});
        return done(c);
    });
    // The rotation's base (0x284, also the stepped rotation's; 5225..5229
    // address 0x2b4, its offset): 5222 (0x139110 -> 0x26378c) at once,
    // 5221 (0x138fd4) into a register, 5220 (0x15735c) still turning.
    m.service(5222, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->stepAngle.set({wrapAngle(c.real(1))});
        return done(c);
    });
    // 5223 / 5224 (0x139058 -> 0x2af580): to an angle / by a difference,
    // over frames along a curve, the short way round.
    auto turn = [object](Call& c, bool relative) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        const float to = wrapAngle(c.real(1) + (relative ? o->stepAngle.target()[0] : 0.0f));
        float from = wrapAngle(o->stepAngle[0]);
        if (std::fabs(to - from) > kPi) {
            from += to - from >= 0 ? kTwoPi : -kTwoPi;
        }
        o->stepAngle.set({from});
        o->stepAngle.start({to}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    };
    m.service(5223, [turn](Call& c) { return turn(c, false); });
    m.service(5224, [turn](Call& c) { return turn(c, true); });
    m.service(5221, [object](Call& c) {
        Object* o = object(c);
        if (Value* r = c.count() > 1 ? c.reg(1) : nullptr) {
            *r = Value::ofFloat(o ? o->stepAngle[0] : 0.0f);
        }
        return done(c, o != nullptr);
    });
    // 5226 (sixth adventure 0x1867c4 -> 0x380364): the rotation (0x2b4)
    // into a register, as 5221 the base.
    m.service(5226, [object](Call& c) {
        Object* o = object(c);
        if (Value* r = c.count() > 1 ? c.reg(1) : nullptr) {
            *r = Value::ofFloat(o ? o->rotation[0] : 0.0f);
        }
        return done(c, o != nullptr);
    });
    m.service(5220, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->stepAngle.moving() ? 1 : 0) : false;
    });
    // 5207 / 5208 (0x139498 / 0x13953c): the scale offset (x, y) at once /
    // over frames along a curve.
    m.service(5207, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->scaleOffset.set({c.real(1), c.real(2)});
        return done(c);
    });
    m.service(5208, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->scaleOffset.start({c.real(1), c.real(2)}, c.real(3), c.integer(4));
        return done(c);
    });
    // 5262 / 5263 (0x1372dc / 0x137350): the depth at once / over frames
    // [, curve]; 5267 / 5268 / 5269 (0x137074 / 0x1370e8 / 0x136fbc): the
    // depth offset at once / over frames with a curve.
    m.service(5262, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->depth.set({c.real(1)});
        return done(c);
    });
    m.service(5263, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->depth.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    m.service(5267, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->depthOffset.set({c.real(1)});
        return done(c);
    });
    auto depthOffsetTween = [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        o->depthOffset.start({c.real(1)}, c.real(2), c.integer(3));
        return done(c);
    };
    m.service(5268, depthOffsetTween);
    m.service(5269, depthOffsetTween);
    // "Still changing" queries (the curve byte below 0x20): 5240 position
    // (0x2bec84), 5245 offset (0x2bec9c), 5250 either (0x2becb4), 5225
    // rotation (0x3525e8).
    m.service(5240, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->position.moving()) : false;
    });
    // 5200 (0x157464 -> 0x352650): the scale still changing (+0x1d4, the
    // scale tween's curve byte, below 0x20).
    m.service(5200, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->scale.moving()) : false;
    });
    m.service(5245, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->offset.moving()) : false;
    });
    m.service(5250, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->position.moving() || o->offset.moving()) : false;
    });
    m.service(5225, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->rotation.moving()) : false;
    });
    // 5230 (0x1572ac -> 0x3525c8): the stepped rotation (0x284) or the
    // rotation (0x2b8) still moving.
    m.service(5230, [object](Call& c) {
        Object* o = object(c);
        return o ? done(c, o->rotation.moving() || o->stepAngle.moving()) : false;
    });

    // ------------------------------------------------------------ content
    // 5410 (0x12de9c -> 0x2cd3d4): the object shows a movie resource's
    // picture (the left eye; drawn like a picture, content 3).
    m.service(5410, [this, object](Call& c) {
        Object* o = object(c);
        adventure56::Resource* r =
            c.count() > 1 ? scene.resource(reference(c, 1), adventure56::Resource::Kind::Movie)
                          : nullptr;
        if (!o || !r || !r->movie) {
            return done(c, 0);
        }
        o->picture = r->movie->picture();
        o->pictureBlend = 0;
        o->pictureCellList = false;
        o->content = 3;
        return done(c, 1);
    });
    // 5411 (0x12de08 -> 0x2cd454): the object shows a resource (a movie's
    // picture or a picture) as a list of rectangles; 5413 (0x127aa8 ->
    // 0x2cff40) adds one (object, x, y, w, h, u, v); 5412 (0x127bd4 ->
    // 0x2cff30) ends the list. 5400 (0x12e21c: the object's vtable +0x28)
    // follows hiding a button in the secrets' videos: *hypothesis*, its
    // content is dropped.
    m.service(5411, [this, object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return done(c, 0);
        }
        // Movies are kept in their own list (see 5410).
        adventure56::Resource* r =
            scene.resource(reference(c, 1), adventure56::Resource::Kind::Movie);
        if (!r || !r->movie) {
            r = scene.resource(reference(c, 1));
        }
        std::shared_ptr<ctr3d::Texture> picture =
            r && r->movie ? r->movie->picture() : r ? r->picture : nullptr;
        if (!picture) {
            return done(c, 0);
        }
        o->picture = std::move(picture);
        o->pictureBlend = 0;
        o->pictureCells.clear();
        o->pictureCellList = true;
        o->content = 3;
        return done(c, 1);
    });
    m.service(5413, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 7 || !o->pictureCellList) {
            return done(c, 0);
        }
        o->pictureCells.push_back(
            {c.real(1), c.real(2), c.real(3), c.real(4), c.real(5), c.real(6)});
        return done(c, 1);
    });
    m.service(5412, [object](Call& c) {
        Object* o = object(c);
        return done(c, o && o->pictureCellList ? 1 : 0);
    });
    m.service(5400, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return done(c, 0);
        }
        o->picture.reset();
        o->pictureCells.clear();
        o->pictureCellList = false;
        o->content = 0;
        return done(c, 1);
    });
    // 5430 (0x12dd08 -> 0x36e5d8): the object shows a picture resource.
    m.service(5430, [this, object](Call& c) { return showPicture(c, object(c)); });
    // 5500 (0x12d878 -> 0x2cd790): the object shows a canvas (object,
    // canvas [, mode [, curve]]; hypothesis: the extra operands choose the
    // filtering).
    m.service(5500, [this, object](Call& c) {
        Object* o = object(c);
        Resource* r = c.count() > 1 ? scene.resource(reference(c, 1)) : nullptr;
        if (!o || !r || !r->picture) {
            return done(c, 0);
        }
        o->picture = r->picture;
        o->pictureBlend = 0;
        o->content = 8;
        return done(c);
    });
    // 5501 (0x159f38 -> 0x33de84): the box around what was drawn on a
    // canvas into up to four registers: x, y, width, height (zeros for
    // other objects; the circle puzzles test which object it encloses).
    m.service(5501, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Object* o = scene.object(reference(c, 0));
        const auto& b = o ? o->drawnBox : Object::DrawnBox{};
        const int v[4] = {b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0};
        for (std::size_t i = 1; i < c.count() && i <= 4; ++i) {
            if (Value* r = c.reg(i)) {
                *r = Value::ofInt(o && o->content == 8 && b.any ? v[i - 1] : 0);
            }
        }
        return done(c, o != nullptr);
    });
    // 5510 (0x12d818 -> 0x1a83ec): a canvas is wiped (transparent, no
    // drawn box); 1 when it was a canvas.
    m.service(5510, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        if (o->content != 8 || !o->picture) {
            return done(c, 0);
        }
        std::fill(o->picture->pixels.begin(), o->picture->pixels.end(),
                  platform::Color{0, 0, 0, 0});
        o->drawnBox = {};
        o->pen.down = false;
        // 0x1a8504: the recorded strokes and the recognised character go.
        o->pen.strokes.clear();
        o->pen.recognised.clear();
        return done(c, 1);
    });
    // 5511 (0x15a094 -> 0x177a94): a canvas's pen mode (0 off, 1 draw, 16
    // draw and record strokes for the handwriting recogniser).
    m.service(5511, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        if (o->content == 8) {
            o->pen.mode = c.integer(1) & 0xff;
        }
        return done(c);
    });
    // 5512 (0x1433f4 -> 0x2c49dc): the pen width in pixels.
    m.service(5512, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        if (o->content == 8) {
            o->pen.width = c.real(1);
        }
        return done(c);
    });
    // 5521 (0x143274): the pen colour, a text palette entry (0x177d2c:
    // low nibble column, high nibble row of fix.xr/000.xi) or red, green,
    // blue (0x177648; hypothesis: 0..1, larger values taken as 0..255).
    m.service(5521, [this, object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        if (o->content == 8) {
            if (c.count() < 4) {
                o->pen.colour = scene.paletteColour(c.integer(1) & 0xff);
            } else {
                float rgb[3] = {c.real(1), c.real(2), c.real(3)};
                const bool bytes = rgb[0] > 1 || rgb[1] > 1 || rgb[2] > 1;
                for (int i = 0; i < 3; ++i) {
                    o->pen.colour[std::size_t(i)] = bytes ? rgb[i] / 255.0f : rgb[i];
                }
                o->pen.colour[3] = 1;
            }
        }
        return done(c);
    });
    // 5534 (0x136bf8): the canvas's recognised character (canvas content
    // +0x19c), "" for none.
    m.service(5534, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        c.result = Value::ofText(o->content == 8 ? o->pen.recognised : std::string());
        return true;
    });
    // 5532 / 5533 (0x150d34 / 0x150cd4): whether a canvas can redo / undo
    // a stroke (0x343a60 / 0x33df60; only canvases, content 8). The
    // reconstruction keeps no stroke history yet.
    m.service(5532, [object](Call& c) { return object(c) ? done(c, 0) : false; });
    m.service(5533, [object](Call& c) { return object(c) ? done(c, 0) : false; });
    // 5420 (0x12d6c8 -> 0x2cd848): the object is a solid rectangle
    // ([x, y,] width, height from its position).
    m.service(5420, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->rectangle = true;
        o->content = 13;
        if (c.count() >= 5) {
            o->shape[0] = c.real(1);
            o->shape[1] = c.real(2);
            o->shape[2] = c.real(3);
            o->shape[3] = c.real(4);
        } else {
            o->shape[0] = o->shape[1] = 0;
            o->shape[2] = c.real(1);
            o->shape[3] = c.real(2);
        }
        return done(c);
    });
    // 5900 (0x12e010 -> 0x12fee4): the object gets a text component:
    // capacity [, font] [, window animation set]; the default font is
    // "_FNT_NRM" ("#FNT_SUB" names the second font).
    m.service(5900, [this, object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        Resource* font = scene.resource("_FNT_NRM");
        for (std::size_t i = 2; i < c.count(); ++i) {
            const std::string name = textOf(c, i);
            if (name == "#FNT_SUB") {
                if (Resource* sub = scene.resource("_FNT_SML")) {
                    font = sub;
                }
                continue;
            }
            Resource* r = scene.resource(reference(c, i));
            if (r && r->font) {
                font = r;
            } else if (r && r->animation) {
                o->window = r->animation;
            }
        }
        if (font && font->font) {
            o->font = font->font;
            o->content = 15;
        }
        return done(c);
    });
    // 5911 (0x148d2c -> 0x14a500 -> 0x2c1bac): the text [, show at once,
    // ?, speed, window width, height, text offset x, y].
    m.service(5911, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->text = textOf(c, 1);
        if (c.count() >= 7) {
            o->windowSize[0] = c.real(5);
            o->windowSize[1] = c.real(6);
        }
        if (c.count() >= 9) {
            o->textOffset[0] = c.real(7);
            o->textOffset[1] = c.real(8);
        }
        return done(c);
    });
    // 5910: the text cleared (hypothesis).
    m.service(5910, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        o->text.clear();
        return done(c);
    });
    // 5932 / 5933 (0x15aa94 / 0x15aaf4 -> 0x172a60 / 0x172b40): the text's
    // colour and second colour, palette indices (or r, g, b [, a]).
    m.service(5932, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->textColour = c.integer(1);
        o->textRgba.reset();
        return done(c);
    });
    m.service(5933, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->textColour2 = c.integer(1);
        return done(c);
    });
    // 5942 / 5943 (0x151280 / 0x151304: bytes +0x167 / +0x168 of the text
    // component): centred across / down.
    m.service(5942, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->centreH = c.at(1).truthy();
        return done(c);
    });
    m.service(5943, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->centreV = c.at(1).truthy();
        return done(c);
    });
    // 5950 (0x144128 -> 0x2c4308; sixth adventure 0x1af43c -> 0x24e618 ->
    // 0x256408): three text spacing values (-999 keeps the default: 0, 2,
    // -1 when 0x287fa0 holds, else 1, 1, 1). The first is added to every
    // character's advance (the sixth adventure's question line, -1: the
    // reference's width is native's minus one pixel per gap). The second
    // is the line spacing (Object::lineSpacing); the third (the first
    // line's offset, +0x4ae: font +0x16 plus it when not negative) is not
    // modelled.
    m.service(5950, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        if (c.integer(1) != -999) {
            o->letterSpacing = c.integer(1);
        }
        // The second value, -999 as 2 (the fifth adventure's Note texts
        // reach 0x170578 with 0, 2, -1 for -999 each: console exec trace;
        // the condition choosing 2 over 1, 0x287fa0, is not decoded).
        o->lineSpacing = c.integer(2) != -999 ? c.integer(2) : 2;
        return done(c, 1);
    });
    // 5956 (0x143ef0 -> 0x14583c -> 0x172a10): a text's edge: width
    // (clamped to 0..8), red, green, blue [, alpha (1)]; texts only.
    m.service(5956, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->edgeWidth = std::clamp(c.real(1), 0.0f, 8.0f);
        o->edgeColour[0] = c.real(2);
        o->edgeColour[1] = c.real(3);
        o->edgeColour[2] = c.real(4);
        o->edgeColour[3] = c.count() > 5 ? c.real(5) : 1.0f;
        return done(c);
    });
    // 5960 (0x156ec8 -> 0x33dde8): the text's size into two registers.
    m.service(5960, [this, object](Call& c) {
        Object* o = object(c);
        const auto size = o ? scene.measure(*o) : std::array<float, 2>{0, 0};
        for (std::size_t i = 0; i < 2; ++i) {
            if (Value* r = c.reg(1 + i)) {
                *r = Value::ofInt(int(size[i]));
            }
        }
        return done(c, o != nullptr);
    });
    // 5626 (0x148574 -> 0x14a0a8 -> 0x177214 -> 0x187ea0): the animation
    // set draws a string (text, advance [, line advance]).
    m.service(5626, [object](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->glyphText = textOf(c, 1);
        o->glyphAdvance = c.count() > 2 ? c.real(2) : 0.0f;
        o->glyphLineAdvance = c.count() > 3 ? c.real(3) : 0.0f;
        return done(c);
    });
}

void Game::registerHandlerServices() {
    auto& m = machine;
    // The object services the 2D (5xxx) and 3D (6xxx) objects share: the
    // same handlers at base + n (3D: 0x2d1a40 lookups instead of 0x2d375c).
    for (const bool solid : {false, true}) {
        const int base = solid ? 6000 : 5000;
        auto object = [this, solid](Call& c) -> Object* {
            return scene.find(solid, reference(c, 0));
        };
        // 5600 (0x12d538 -> 0x2cd9b0): the object uses an animation set.
        m.service(base + 600, [this, object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2 || !setAnimationSet(*o, reference(c, 1))) {
                return done(c, 0);
            }
            return done(c);
        });
        // 5622 (0x1278dc -> 0x39b1ec): play the set's animation "name".
        // 5621 (0x127744 -> 0x2d021c): the name of the animation the object
        // plays (nothing without an animation set); the switch puzzles judge
        // by it ("B_on").
        m.service(base + 621, [object](Call& c) {
            Object* o = object(c);
            if (!o) {
                return false;
            }
            if (o->animation && o->playing >= 0 &&
                o->playing < int(o->animation->animations.size())) {
                c.result = Value::ofText(o->animation->animations[std::size_t(o->playing)].name);
            } else {
                c.result = Value{};
            }
            return true;
        });
        m.service(base + 622, [this, object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return done(c, 0);
            }
            const std::uint32_t flags = c.count() > 2 ? c.unsignedInt(2) : 0;
            return done(c, playAnimation(*o, c.at(1), flags) ? 1 : 0);
        });
        // 5044 / 6044 (0x12bed0 -> 0x2b8a04): start a block (its name) on
        // the object's handler thread with up to eight arguments; 1 when it
        // exists (puzzle 027's targets run their crash sequences so).
        // 5043 / 6043 (sixth adventure 0x17af28 -> 0x1bad70 -> 0x273cac): the
        // object's handler thread drops its running block and runs the
        // named one (a name or its hash) at once, without arguments.
        // Fifth adventure 0x12bf9c (not checked against the sixth's).
        m.service(base + 43, [this, object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            return done(c, startObjectBlock(*o, blockName(newestScript(), c, 1), {}) ? 1 : 0);
        });
        m.service(base + 44, [this, object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            std::vector<Value> arguments;
            for (std::size_t i = 2; i < c.count() && i < 10; ++i) {
                arguments.push_back(c.at(i));
            }
            return done(c,
                        startObjectBlock(*o, blockName(newestScript(), c, 1), arguments) ? 1 : 0);
        });
        // 5042 / 6042 (0x13306c -> 0x2cd1d0): switch the handler to a state
        // (0 normal .. 6 click), as a touch would.
        m.service(base + 42, [this, object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            startHandler(*o, std::clamp(c.integer(1), 0, 6));
            return done(c);
        });
        m.service(base + 102, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            o->alpha.set({c.real(1)});
            return done(c);
        });
        m.service(base + 103, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 3) {
                return false;
            }
            o->alpha.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
            return done(c);
        });
        // 5104 (0x1364b4): an alpha tween after stopping the running one.
        m.service(base + 104, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 3) {
                return false;
            }
            o->alpha.set(o->alpha.value());
            o->alpha.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
            return done(c);
        });
        m.service(base + 112, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 4) {
                return false;
            }
            o->colour.set(vec(c, 1, 3));
            return done(c);
        });
        m.service(base + 113, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 5) {
                return false;
            }
            o->colour.start(vec(c, 1, 3), c.real(4), c.count() > 5 ? c.integer(5) : 0);
            return done(c);
        });
        // 5100 (0x2bece8): the alpha still changing.
        m.service(base + 100, [object](Call& c) {
            Object* o = object(c);
            return o ? done(c, o->alpha.moving()) : false;
        });
        // 5040 / 6040 (0x148fc8 / 0x1496e0): 1 while the object's handler
        // thread runs a block (+0x17c; the robot game waits on its pieces
        // so).
        m.service(base + 40, [this, object](Call& c) {
            Object* o = object(c);
            return o ? done(c, handlerRunning(*o) ? 1 : 0) : false;
        });
        // 5041 / 6041 (0x12ef38 -> 0x2cd1d0): the handler back to its normal
        // block.
        m.service(base + 41, [this, object](Call& c) {
            Object* o = object(c);
            if (!o) {
                return false;
            }
            startHandler(*o, 0);
            return done(c);
        });
        // 5007 / 6007 (0x132350 / 0x133260, vtable +0x14): a further switch
        // of the object (seen turning shadows and background parts off);
        // kept, its effect is not known yet.
        m.service(base + 7, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            o->switch14 = c.at(1).truthy();
            return done(c);
        });
        // 5045 / 5046 (0x1281c8 / 0x12824c): an object's value by index.
        m.service(base + 45, [this, object](Call& c) {
            Object* o = object(c);
            const std::uint32_t i = c.unsignedInt(1);
            if (!o || i >= o->values.size()) {
                return false;
            }
            c.result = objectValue(*o, i);
            return true;
        });
        m.service(base + 46, [this, object](Call& c) {
            Object* o = object(c);
            const std::uint32_t i = c.unsignedInt(1);
            if (!o || i >= o->values.size() || c.count() < 3) {
                return false;
            }
            setObjectValue(*o, i, c.at(2));
            return done(c);
        });
        // 5051 (0x1279ac -> 0x2cff84): a touch rectangle (kind, x, y, w, h;
        // negative sizes allowed). 6051 / 6052 are the 3D objects' touch
        // sphere and capsule (registered with 6053).
        if (base == 5000) {
            m.service(base + 51, [object](Call& c) {
                Object* o = object(c);
                if (!o || c.count() < 6 || o->areas.size() >= 16 || c.integer(1) == 0) {
                    return done(c, 0);
                }
                float x = c.real(2), y = c.real(3), w = c.real(4), h = c.real(5);
                if (w < 0) {
                    x += w;
                    w = -w;
                }
                if (h < 0) {
                    y += h;
                    h = -h;
                }
                o->areas.push_back({c.integer(1), x, y, w, h});
                return done(c);
            });
        }
        // 5052 (0x12e170): the union of the touch rectangles (x, y, w, h)
        // into four registers (kept by 0x2cff84 as areas are added).
        if (base == 5000) {
            m.service(base + 52, [object](Call& c) {
                Object* o = object(c);
                if (!o || c.count() < 5) {
                    return false;
                }
                float box[4] = {0, 0, 0, 0};
                for (std::size_t i = 0; i < o->areas.size(); ++i) {
                    const auto& a = o->areas[i];
                    if (i == 0) {
                        box[0] = a.x, box[1] = a.y, box[2] = a.x + a.w, box[3] = a.y + a.h;
                    } else {
                        box[0] = std::min(box[0], a.x);
                        box[1] = std::min(box[1], a.y);
                        box[2] = std::max(box[2], a.x + a.w);
                        box[3] = std::max(box[3], a.y + a.h);
                    }
                }
                const float out[4] = {box[0], box[1], box[2] - box[0], box[3] - box[1]};
                for (std::size_t i = 0; i < 4; ++i) {
                    if (Value* r = c.reg(1 + i)) {
                        *r = Value::ofFloat(out[i]);
                    }
                }
                return done(c);
            });
        }
        // Handler blocks (0x131aa8 5032: normal, touch [, slot 1, slot 2];
        // 0x1318f4 5033: normal, select, click [, slot 1, slot 2]; 0x136c5c
        // 5034: normal [, slide, rotate]); the normal block starts at once.
        auto handlers = [this, object](Call& c, int mode) {
            Object* o = object(c);
            if (!o) {
                return false;
            }
            std::vector<std::string> names;
            for (std::size_t i = 1; i < c.count(); ++i) {
                names.push_back(blockName(newestScript(), c, i));
            }
            setHandlers(*o, mode, names);
            return done(c);
        };
        // 5031 (0x136d88): blocks without touch (mode 0: normal [, 1, 2]).
        m.service(base + 31, [handlers](Call& c) { return handlers(c, 0); });
        m.service(base + 32, [handlers](Call& c) { return handlers(c, 1); });
        m.service(base + 33, [handlers](Call& c) { return handlers(c, 2); });
        m.service(base + 34, [handlers](Call& c) { return handlers(c, 3); });
        // 5008 (0x1322d0, vtable +0x18): drawn or not (see Object::drawn).
        m.service(base + 8, [object](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return false;
            }
            o->drawn = c.at(1).truthy();
            return done(c);
        });
        // 5013 (0x141658 -> 0x1c2ba0): attach to a parent (child, parent).
        m.service(base + 13, [object, solid](Call& c) {
            Object* o = object(c);
            if (!o || c.count() < 2) {
                return done(c, 0);
            }
            o->parent = reference(c, 1);
            // 0x141c00 -> 0x2c5574: 3D children may hang from a bone.
            o->parentBone = solid && c.count() > 2 ? reference(c, 2) : 0;
            return done(c);
        });
        // 5060 (0x149020): whether the object is enabled (+0x114 bit 0 clear;
        // hypothesis: shown).
        m.service(base + 60, [this, object](Call& c) {
            Object* o = object(c);
            return o ? done(c, scene.shown(*o) ? 1 : 0) : false;
        });
        // 5061 / 5062 / 5063 (0x149078 / 0x149128 / 0x1490d0): the object's
        // own touch (+0x118), update (+0x11c) and drawn (+0x120) switches
        // (bit 0 clear: on).
        m.service(base + 61, [object](Call& c) {
            Object* o = object(c);
            return o ? done(c, o->touchOn ? 1 : 0) : false;
        });
        m.service(base + 62, [object](Call& c) {
            Object* o = object(c);
            return o ? done(c, o->switch14 ? 1 : 0) : false;
        });
        m.service(base + 63, [object](Call& c) {
            Object* o = object(c);
            return o ? done(c, o->drawn ? 1 : 0) : false;
        });
        // 5010 (0x14133c -> 0x3464c8): the name of an object's n-th child
        // (from 0, in the screen list's order); nothing without one.
        m.service(base + 10, [this, object, solid](Call& c) {
            Object* parent = object(c);
            const int n = c.count() > 1 ? c.integer(1) : 0;
            std::vector<const Object*> found;
            if (parent) {
                for (const auto& [hash, o] : scene.objects(solid)) {
                    if (o->parent == parent->hash) {
                        found.push_back(o.get());
                    }
                }
            }
            std::sort(found.begin(), found.end(), adventure56::screenListBefore);
            c.result = n >= 0 && n < int(found.size()) ? Value::ofText(found[std::size_t(n)]->name)
                                                       : Value{};
            return true;
        });
        // 5012 (0x14160c -> 0x29500c): detach from the parent (the scripts
        // then place the object where the parent was).
        m.service(base + 12, [object](Call& c) {
            Object* o = object(c);
            if (!o || !o->parent) {
                return done(c, 0);
            }
            o->parent = 0;
            return done(c);
        });
        // 5014 (0x156fa0 -> 0x29500c with 1): detach from the parent keeping
        // the place on the screen (0x3384b0: the parents' transform is folded
        // into position, scale and rotation). Puzzle 027 fires its stones so.
        m.service(base + 14, [this, object, solid](Call& c) {
            Object* o = object(c);
            if (!o || !o->parent) {
                return done(c, 0);
            }
            if (solid) {
                scene.detach3D(*o);
                return done(c, 1);
            }
            const auto t = scene.screenTransform(*o);
            o->position.set({t.x - o->offset[0], t.y - o->offset[1], o->position[2]});
            o->scale.set({t.sx - o->scaleOffset[0], t.sy - o->scaleOffset[1], o->scale[2]});
            o->rotation.set({t.angle - o->stepAngle[0]});
            o->parent = 0;
            return done(c, 1);
        });
    }

    auto object = [this](Call& c) -> Object* {
        return scene.object(reference(c, 0));
    };
    // 5303 / 5304 (0x14d0bc -> 0x346588, 0x1514bc -> 0x346690): the
    // objects of a content kind on a screen whose position lies in a box
    // (screen, kind, x, y, width, height; edges included), walked in the
    // screen's list order: 5303 counts them, 5304 names the n-th (from 0;
    // it skips objects with +0x114 bit 0, as 5302 does).
    auto inBox = [this](Call& c, bool skipHidden) {
        std::vector<const Object*> found;
        if (c.count() < 6) {
            return found;
        }
        const int screen = c.integer(0), kind = c.integer(1) & 0xff;
        const float x = c.real(2), y = c.real(3), w = c.real(4), h = c.real(5);
        for (const auto& [hash, o] : scene.objects()) {
            if (o->screen != screen || o->content != kind || (skipHidden && !scene.shown(*o))) {
                continue;
            }
            const auto at = scene.screenPosition(*o);
            if (at[0] >= x && at[0] <= x + w && at[1] >= y && at[1] <= y + h) {
                found.push_back(o.get());
            }
        }
        std::sort(found.begin(), found.end(), adventure56::screenListBefore);
        return found;
    };
    m.service(5303, [inBox](Call& c) {
        if (c.count() < 6) {
            return false;
        }
        return done(c, int(inBox(c, false).size()));
    });
    m.service(5304, [inBox](Call& c) {
        if (c.count() < 7) {
            return false;
        }
        const auto found = inBox(c, true);
        const int n = c.integer(6);
        c.result =
            n >= 0 && n < int(found.size()) ? Value::ofText(found[std::size_t(n)]->name) : Value{};
        return true;
    });
    // 5311 (0x1316f4 -> 0x3461f0): the name of the n-th object (from 0, in
    // the screen list's order: priority, then insertion) of a group (5002's fourth operand,
    // +0x436; a child counts with its parent's group; 0xff any) on a screen
    // (screen, group, n); nothing past the last.
    m.service(5311, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const int screen = c.integer(0), group = c.integer(1) & 0xff, n = c.integer(2);
        std::vector<const Object*> found;
        for (const auto& [hash, o] : scene.objects()) {
            if (o->screen != screen) {
                continue;
            }
            const Object* g = o.get();
            for (int depth = 0; g->parent && depth < 16; ++depth) {
                const Object* p = scene.object(g->parent);
                if (!p) {
                    break;
                }
                g = p;
            }
            if (group == 0xff || g->kind == group) {
                found.push_back(o.get());
            }
        }
        std::sort(found.begin(), found.end(), adventure56::screenListBefore);
        c.result =
            n >= 0 && n < int(found.size()) ? Value::ofText(found[std::size_t(n)]->name) : Value{};
        return true;
    });
    // 5302 (0x138b08 -> 0x3462c4): the name of the object of a content kind
    // on a screen nearest a point (screen, kind, x, y), within 1000 pixels;
    // later objects win ties. Objects with +0x114 bit 0 are skipped
    // (hypothesis: hidden ones).
    m.service(5302, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        const int screen = c.integer(0), kind = c.integer(1) & 0xff;
        const float x = c.real(2), y = c.real(3);
        std::vector<const Object*> list;
        for (const auto& [hash, o] : scene.objects()) {
            if (o->screen == screen && o->content == kind && scene.shown(*o)) {
                list.push_back(o.get());
            }
        }
        std::sort(list.begin(), list.end(), adventure56::screenListBefore);
        const Object* best = nullptr;
        float bestDistance = 1000000.0f;
        for (const Object* o : list) {
            const auto at = scene.screenPosition(*o);
            const float dx = at[0] - x, dy = at[1] - y, d = dx * dx + dy * dy;
            if (d <= bestDistance) {
                best = o;
                bestDistance = d;
            }
        }
        c.result = best ? Value::ofText(best->name) : Value{};
        return true;
    });
    // 5301 (0x131ec0 -> 0x34614c): the name of the n-th object (from 0) of
    // a kind on a screen (screen, kind [, n]); nothing when there is none.
    // The game walks the screen's object list (sorted by priority, an
    // object inserted after its equals: Object::listOrder).
    m.service(5301, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int screen = c.integer(0), kind = c.integer(1) & 0xff;
        const int n = c.count() > 2 ? c.integer(2) : 0;
        std::vector<const Object*> found;
        for (const auto& [hash, o] : scene.objects()) {
            if (o->screen == screen && o->content == kind) {
                found.push_back(o.get());
            }
        }
        std::sort(found.begin(), found.end(), adventure56::screenListBefore);
        if (screen < 0 || screen > 4 || n < 0 || n >= int(found.size())) {
            c.result = Value{};
            return true;
        }
        c.result = Value::ofText(found[std::size_t(n)]->name);
        return true;
    });
    for (const bool solid : {false, true}) {
        const int base = solid ? 6000 : 5000;
        auto find = [this, solid](Call& c) -> Object* {
            return scene.find(solid, reference(c, 0));
        };
        // 5011 (0x131e24 -> 0x32cf30): the parent's name.
        m.service(base + 11, [this, find, solid](Call& c) {
            Object* o = find(c);
            Object* p = o && o->parent ? scene.find(solid, o->parent) : nullptr;
            // Nothing (not an empty text: texts are always true) without one.
            c.result = p ? Value::ofText(p->name) : Value{};
            return true;
        });
        // 5023 (0x12be6c -> 0x1c2b1c): a new priority (2D: the object
        // moves after its equals in the screen list).
        m.service(base + 23, [this, find, solid](Call& c) {
            Object* o = find(c);
            if (!o || c.count() < 2) {
                return done(c, 0);
            }
            if (solid) {
                o->priority = c.integer(1);
            } else {
                scene.setPriority(*o, c.integer(1));
            }
            return done(c);
        });
    }
    // 5241 (0x1387e0): the position into two registers.
    m.service(5241, [this](Call& c) {
        Object* o = scene.object(reference(c, 0));
        Value* x = c.reg(1);
        Value* y = c.reg(2);
        if (x) {
            *x = Value::ofFloat(o ? o->position[0] : 0.0f);
        }
        if (y) {
            *y = Value::ofFloat(o ? o->position[1] : 0.0f);
        }
        return done(c, o != nullptr);
    });
    // 5251 (0x13839c -> 0x28d358): the position plus the offset (the
    // drawn place, cached at +0x3ac) into two registers.
    m.service(5251, [this](Call& c) {
        Object* o = scene.object(reference(c, 0));
        Value* x = c.reg(1);
        Value* y = c.reg(2);
        if (x) {
            *x = Value::ofFloat(o ? o->position[0] + o->offset[0] : 0.0f);
        }
        if (y) {
            *y = Value::ofFloat(o ? o->position[1] + o->offset[1] : 0.0f);
        }
        return done(c, o != nullptr);
    });
    // 5252 (0x13877c -> 0x2d01e8): the position becomes the playing
    // animation's origin (410's origin: where the designer placed it on the
    // screen); objects without an animation go to (0, 0).
    m.service(5252, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        std::array<float, 3> p{0, 0, 0};
        if (o->animation && o->playing >= 0 && o->playing < int(o->animation->animations.size())) {
            const auto& a = o->animation->animations[std::size_t(o->playing)];
            p = {a.origin[0], a.origin[1], a.origin[2]};
        }
        o->position.set(p);
        return done(c);
    });
    // 5253 (sixth adventure 0x1986b0 -> 0x25c8b4): the vector 5252 moves
    // the object to (the playing animation's origin; (0, 0) without one)
    // into two registers; result 1 when the object exists (registers 0, 0
    // otherwise).
    m.service(5253, [object](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const Object* o = object(c);
        std::array<float, 3> p{0, 0, 0};
        if (o && o->animation && o->playing >= 0 &&
            o->playing < int(o->animation->animations.size())) {
            const auto& a = o->animation->animations[std::size_t(o->playing)];
            p = {a.origin[0], a.origin[1], a.origin[2]};
        }
        for (std::size_t i = 0; i < 2; ++i) {
            if (Value* r = c.reg(1 + i)) {
                *r = Value::ofFloat(p[i]);
            }
        }
        return done(c, o != nullptr);
    });
}

void Game::register3DServices() {
    using adventure56::Object3D;
    auto& m = machine;
    // The sixth adventure's 58xx services are the 68xx ones on 2D objects
    // (the same handlers with the 2D lookup 0x27e7f8 instead of 0x28bc54):
    // registered below as the 68xx handler run with the lookup switched.
    static thread_local bool on2D = false;
    auto object = [this](Call& c) -> Object3D* {
        return on2D ? scene.object(reference(c, 0)) : scene.object3D(reference(c, 0));
    };
    // 6021 (sixth adventure 0x181534 -> 0x2c24f8): a 3D object moves to
    // another screen (object, screen below 5; +0x12d).
    m.service(6021, [this](Call& c) {
        Object3D* o = c.count() >= 2 ? scene.object3D(reference(c, 0)) : nullptr;
        const int screen = c.count() >= 2 ? c.integer(1) : -1;
        if (!o || screen < 0 || screen >= adventure56::Scene::kScreens) {
            return done(c, 0);
        }
        o->screen = screen;
        return done(c, 1);
    });
    // 3602 (0x12aa50 -> 0x2cef64): a pop-up resource "name", "path" (.xp).
    m.service(3602, [this](Call& c) {
        const fs::path folder = findOfKind(pack, textOf(c, 1), ".xp");
        if (folder.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Popup;
        r.path = textOf(c, 1);
        r.animation = adventure56::loadAnimationSet(pack, folder);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3591 (0x129598): Resource::animationValue.
    m.service(3591, [this](Call& c) {
        Resource* r = c.count() > 1 ? scene.resource(reference(c, 0)) : nullptr;
        if (!r) {
            return false;
        }
        r->animationValue = c.real(1);
        return done(c, 1);
    });
    // 3725 / 3726 (0x14de94 / 0x14dcc4): a model resource's motion / face
    // motion (by name; the resource's own list, then its eight parts) as
    // its length in frames: last - first (the MINF words at 0x4c / 0x50,
    // +0x30 / +0x34 once loaded).
    auto motionLength = [this](Call& c, bool face) {
        Resource* r = c.count() > 1 ? scene.resource(reference(c, 0)) : nullptr;
        if (!r || !r->model) {
            return false;
        }
        const std::string name = textOf(c, 1);
        const adventure56::Motion* mo = face ? r->model->faceMotion(name) : r->model->motion(name);
        if (!mo) {
            return false;
        }
        c.result = Value::ofInt(mo->last - mo->first);
        return true;
    };
    m.service(3725, [motionLength](Call& c) { return motionLength(c, false); });
    m.service(3726, [motionLength](Call& c) { return motionLength(c, true); });
    // 3691 (0x12ab84): a pop-up's scale (+0x1a8; see Resource::popScale).
    m.service(3691, [this](Call& c) {
        Resource* r = c.count() > 1 ? scene.resource(reference(c, 0)) : nullptr;
        if (!r) {
            return done(c, 0);
        }
        r->popScale = c.real(1);
        return done(c, 1);
    });
    // Movies (prog_common MovieRun; docs/movies.md). 3802 (0x125d04 ->
    // 0x32f938): a movie resource "name" from "path" (.moflex) [, two
    // switches not modelled]; 3801 (0x125de8 -> 0x32c8c8) deletes it; 3803
    // (0x1226e4) starts its clock; 3812 (0x125e30 -> 0x343278) 1 when there
    // is none or it has ended; 3813 (0x134238 -> 0x34334c) 1 while it is
    // still loading (natively never); 3820 (0x12a6a4 -> 0x343480) its clock in
    // display ticks, as a float.
    auto movieOf = [this](const Call& c) -> adventure56::MoviePlayer* {
        adventure56::Resource* r =
            c.count() ? scene.resource(reference(c, 0), adventure56::Resource::Kind::Movie)
                      : nullptr;
        return r ? r->movie.get() : nullptr;
    };
    m.service(3802, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        // The movies sit in the romfs root (mov/mNNN.moflex), outside the
        // archives.
        fs::path file = pack.find(textOf(c, 1), ".moflex");
        if (file.empty() && fs::exists(pack.root() / (textOf(c, 1) + ".moflex"))) {
            file = pack.root() / (textOf(c, 1) + ".moflex");
        }
        if (file.empty()) {
            return done(c, 0);
        }
        if (!movieTables_) {
            movieTables_ = std::make_shared<moflex::Tables>(
                moflex::Tables::fromCode(code(), moflex::Tables::Game::Adventure5));
        }
        const Bytes bytes = pack.read(file);
        adventure56::Resource r;
        r.kind = adventure56::Resource::Kind::Movie;
        r.path = textOf(c, 1);
        r.movie = std::make_shared<adventure56::MoviePlayer>(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()), *movieTables_);
        r.movie->setLooping(c.count() > 2 && c.integer(2) != 0);
        movies.push_back(r.movie);
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c, 1);
    });
    m.service(3801, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        scene.removeResource(reference(c, 0), adventure56::Resource::Kind::Movie);
        return done(c, 1);
    });
    m.service(3803, [movieOf](Call& c) {
        adventure56::MoviePlayer* mv = movieOf(c);
        if (!mv) {
            return false;
        }
        mv->start();
        return done(c, 1);
    });
    m.service(3812, [movieOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        adventure56::MoviePlayer* mv = movieOf(c);
        return done(c, !mv || mv->ended() ? 1 : 0);
    });
    m.service(3813, [](Call& c) {
        // 1 while the movie still loads (0x34334c); natively it is loaded
        // when 3802 returns.
        return c.count() ? done(c, 0) : false;
    });
    m.service(3820, [movieOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        adventure56::MoviePlayer* mv = movieOf(c);
        c.result = Value::ofFloat(mv ? mv->ticks() : 0.0f);
        return true;
    });
    // 3702 (0x129708 -> 0x2cfb68): a model resource "name", "path" (.xc).
    m.service(3702, [this](Call& c) {
        const fs::path folder = findOfKind(pack, textOf(c, 1), ".xc");
        if (folder.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Model;
        r.path = textOf(c, 1);
        r.model = adventure56::loadModel(pack, folder);
        r.model->path = r.path;
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3704 (0x13cf64 -> 0x2d40c4 / 0x2cfb68): 3702 with an optional third
    // operand (the scope level the resource belongs to; *not modelled*:
    // the current scope).
    m.service(3704, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        if (scene.resource(textOf(c, 0))) {
            return done(c, 1);
        }
        const fs::path folder = findOfKind(pack, textOf(c, 1), ".xc");
        if (folder.empty()) {
            return done(c, 0);
        }
        Resource r;
        r.kind = Resource::Kind::Model;
        r.path = textOf(c, 1);
        r.model = adventure56::loadModel(pack, folder);
        r.model->path = r.path;
        scene.addResource(textOf(c, 0), std::move(r));
        return done(c);
    });
    // 3710 (0x12c780 -> 0x2d3ef4): a model resource of that name exists.
    m.service(3710, [this](Call& c) {
        Resource* r = c.count() ? scene.resource(reference(c, 0)) : nullptr;
        return done(c, r && r->model ? 1 : 0);
    });
    // 3730 (0x13ce64 -> 0x2c9c98): {model, motion file [, slot]}: the motion
    // file's motions play on the model. The model is a model resource, else
    // a 3D object showing one (content 13); the motion file a model resource
    // (.xc). Without a slot the empty one filled longest ago.
    m.service(3730, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        std::shared_ptr<adventure56::Model> model;
        if (Resource* r = scene.resource(reference(c, 0)); r && r->model) {
            model = r->model;
        } else if (Object3D* o = scene.object3D(reference(c, 0)); o && o->model) {
            model = o->model;
        }
        Resource* file = scene.resource(reference(c, 1));
        if (!model || !file || !file->model) {
            return false;
        }
        const int slot = c.count() > 2 ? c.integer(2) : -1;
        return done(c, model->attachMotions(file->model, slot) ? 1 : 0);
    });
    // 3208 (0x130bb4 -> 0x346078): a text resource of that name exists.
    m.service(3208, [this](Call& c) {
        Resource* r = c.count() ? scene.resource(reference(c, 0)) : nullptr;
        return done(c, r && r->kind == Resource::Kind::Text ? 1 : 0);
    });
    // 6002 (0x1251c0 -> 0x2d1b00): create a 3D object "name", screen,
    // priority, kind [, values].
    m.service(6430, [this, object](Call& c) { return showPicture(c, object(c)); });
    m.service(6002, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        Object3D& o = scene.create3D(textOf(c, 0), c.integer(1), c.integer(2), c.integer(3));
        o.values.assign(16, Value{});
        for (std::size_t i = 0; i < c.count() && i < 16; ++i) {
            o.values[i] = c.at(i);
        }
        c.result = Value::ofUint(o.hash);
        return true;
    });
    // 6001: delete a 3D object (hypothesis by symmetry with 5001).
    m.service(6001, [this](Call& c) {
        scene.remove3D(reference(c, 0));
        return done(c);
    });
    // 6005 / 6006 (vtable +0x0c / +0x10): show; the second switch.
    m.service(6005, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->visible = c.at(1).truthy();
        return done(c);
    });
    m.service(6006, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->touchOn = c.at(1).truthy();
        return done(c);
    });
    // 6820 (0x14d3f0 -> 0x18d72c): a model's face motion by name (.imminf
    // / .imm2); it restarts.
    m.service(6820, [object](Call& c) {
        Object3D* o = object(c);
        if (!o) {
            return false;
        }
        // [flags, layer (0)]
        const int layer = c.count() > 3 ? c.integer(3) : 0;
        if (layer < 0 || layer > 2) {
            return done(c, 0);
        }
        // The motion is a reference (sixth adventure 0x198a80 -> 0x2527e8:
        // 0x2606a4, a name or its hash).
        std::string name = textOf(c, 1);
        if (!c.at(1).isText() && o->model) {
            const std::uint32_t h = reference(c, 1);
            for (const auto& mo : o->model->faceMotions) {
                if (mo.hash == h || xq::hash(mo.name) == h) {
                    name = mo.name;
                }
            }
        }
        o->faces[std::size_t(layer)].motion = name;
        o->faces[std::size_t(layer)].time = 0;
        return done(c);
    });
    // 6821 (sixth adventure 0x198bbc -> 0x2527cc -> 0x1e43e4): a face
    // motion layer's speed (object, speed [, layer (0)]; the layer's +0x3c);
    // result 1 when the layer exists.
    m.service(6821, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const int layer = c.count() > 2 ? c.integer(2) : 0;
        if (layer < 0 || layer > 2) {
            return done(c, 0);
        }
        o->faces[std::size_t(layer)].speed = c.real(1);
        return done(c, 1);
    });
    // 6831 (sixth adventure 0x19905c -> 0x1ca948 -> 0x1e45cc): the material
    // motion layer's speed (object, speed; the layer's +0x3c).
    m.service(6831, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->materialMotionSpeed = c.real(1);
        return done(c, o->model ? 1 : 0);
    });
    // 6817 (sixth adventure 0x1b31bc -> 0x24d588 -> 0x371078): the hash of
    // the motion playing on a layer [layer, 0] (the layer's motion record's
    // first word: its name hash); 0 without one.
    m.service(6817, [object](Call& c) {
        const Object3D* o = object(c);
        if (!o) {
            return false;
        }
        const int layer = c.count() > 1 ? c.integer(1) : 0;
        std::string name;
        if (layer == 0) {
            name = o->motion;
        } else if (layer >= 1 && layer <= int(o->upperMotions.size())) {
            name = o->upperMotions[std::size_t(layer - 1)].motion;
        }
        const adventure56::Motion* mo =
            o->model && !name.empty() ? o->model->motion(name) : nullptr;
        c.result = Value::ofUint(mo ? mo->hash : 0u);
        return true;
    });
    // 6830 (sixth adventure 0x198f20 -> 0x25275c -> 0x1e27f0; fifth
    // 0x14d8b4): a model's material motion by name (XMTM .mtm2 with its
    // .mtminf span; model +0x148, layers of 0x120 bytes at +0x14c, count
    // +0x150) [, flags [, layer]]; result 1 when the layer exists and the
    // motion was found. The motion is a reference (name or hash). Flags
    // taken as 6810's (4 restart, 8 once; hypothesis: the intro plays its
    // appearing and disappearing motions with 12 and 8 and waits for their
    // end with 6833). The XMTM tracks (Model::MaterialClip) drive the
    // materials' alpha (Scene::draw3D).
    m.service(6830, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const std::uint32_t h = reference(c, 1);
        const adventure56::Motion* found = nullptr;
        if (o->model) {
            for (const auto& mo : o->model->materialMotions) {
                if (mo.hash == h || xq::hash(mo.name) == h) {
                    found = &mo;
                }
            }
        }
        if (!found) {
            return done(c, 0);
        }
        const std::uint32_t flags = c.count() > 2 ? c.unsignedInt(2) : 0;
        if (o->materialMotion != found->name || (flags & 4) || o->materialMotionDone) {
            o->materialMotionTime = 0;
        }
        o->materialMotion = found->name;
        o->materialMotionOnce = (flags & 8) != 0;
        o->materialMotionDone = false;
        return done(c, 1);
    });
    // 6833 (sixth adventure 0x1a64ec -> 0x250730 -> 0x370c88; fifth
    // 0x15ae4c): a model's material motion layer [layer] has run to its end
    // (the layer's flag 0x2 at +0x5d; layers past the count: 0).
    m.service(6833, [object](Call& c) {
        const Object3D* o = object(c);
        if (!o) {
            return false;
        }
        return done(c, o->model && !o->materialMotion.empty() && o->materialMotionDone ? 1 : 0);
    });
    // 6865 (sixth adventure 0x1ad3d8 -> 0x27e770 -> 0x1239dc; fifth
    // 0x160120): a model's mesh by name (or hash) shown or not; the first
    // of the model's five parts that has it (0x28a938, a binary search of
    // its mesh hashes) takes the switch (+0xa6). Result 1 when found.
    m.service(6865, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        const std::uint32_t h = reference(c, 1);
        bool found = false;
        if (o->model) {
            for (const auto& mesh : o->model->meshes) {
                found = found || xq::hash(mesh.name) == h;
            }
        }
        if (!found) {
            return done(c, 0);
        }
        if (c.at(2).truthy()) {
            o->hiddenMeshes.erase(h);
        } else {
            o->hiddenMeshes.insert(h);
        }
        return done(c, 1);
    });
    // 6053 (0x13a718 -> 0x3389b4): a touch box (object, kind, corner a
    // x y z, corner b x y z) in the object's own space.
    m.service(6053, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 8) {
            return false;
        }
        if (o->shapes.size() >= 8 || c.integer(1) == 0) {
            return done(c, 0);
        }
        Object3D::Shape sh;
        sh.kind = c.integer(1);
        for (int i = 0; i < 3; ++i) {
            sh.a[i] = c.real(2 + std::size_t(i));
            sh.b[i] = c.real(5 + std::size_t(i));
        }
        o->shapes.push_back(sh);
        return done(c);
    });
    // 6051 (0x13a964 -> 0x338d88): a touch sphere (object, kind, centre x y
    // z, radius); 6052 (0x13a834 -> 0x338df0): a touch capsule (object,
    // kind, end a x y z, end b x y z, radius). In the object's own space;
    // at most 8 shapes, kind 0 adds none (result 0).
    m.service(6051, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 6) {
            return false;
        }
        if (o->shapes.size() >= 8 || c.integer(1) == 0) {
            return done(c, 0);
        }
        Object3D::Shape sh;
        sh.kind = c.integer(1);
        sh.type = 0;
        sh.radius = c.real(5);
        for (int i = 0; i < 3; ++i) {
            sh.a[i] = c.real(2 + std::size_t(i));
        }
        o->shapes.push_back(sh);
        return done(c);
    });
    m.service(6052, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 9) {
            return false;
        }
        if (o->shapes.size() >= 8 || c.integer(1) == 0) {
            return done(c, 0);
        }
        Object3D::Shape sh;
        sh.kind = c.integer(1);
        sh.type = 1;
        sh.radius = c.real(8);
        for (int i = 0; i < 3; ++i) {
            sh.a[i] = c.real(2 + std::size_t(i));
            sh.b[i] = c.real(5 + std::size_t(i));
        }
        o->shapes.push_back(sh);
        return done(c);
    });
    // 6311 (0x1326a4 -> 0x346888): the name of the n-th 3D object (from 0,
    // in creation order) of a kind (6002's fourth operand, +0x76a; 0xff any)
    // on a screen (screen, kind, n); nothing when there is none.
    m.service(6311, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const int screen = c.integer(0), kind = c.integer(1) & 0xff, n = c.integer(2);
        std::vector<const Object3D*> found;
        for (const auto& [hash, o] : scene.objects(true)) {
            if ((screen == -1 || o->screen == screen) && (kind == 0xff || o->kind == kind)) {
                found.push_back(o.get());
            }
        }
        std::sort(found.begin(), found.end(),
                  [](const Object3D* a, const Object3D* b) { return a->order < b->order; });
        c.result =
            n >= 0 && n < int(found.size()) ? Value::ofText(found[std::size_t(n)]->name) : Value{};
        return true;
    });
    // 6009 (0x12c248): whether all named 3D objects exist.
    m.service(6009, [this](Call& c) {
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (!scene.object3D(reference(c, i))) {
                return done(c, 0);
            }
        }
        return done(c, 1);
    });
    // 6700 (0x12edd0 -> 0x2cd240): the object shows a pop-up resource.
    m.service(6700, [this, object](Call& c) {
        Object3D* o = object(c);
        // The pop-up list (a same-named animation set lives in its own).
        Resource* r = nullptr;
        if (c.count() > 1) {
            r = scene.resource(reference(c, 1), Resource::Kind::Popup);
            if (!r) {
                r = scene.resource(reference(c, 1));
            }
        }
        if (!o || !r || !r->animation) {
            return done(c, 0);
        }
        o->popup = r->animation;
        o->popupResource = reference(c, 1);
        o->layerFrames.assign(r->animation->animations.size(), 0);
        return done(c);
    });
    // 6800 (0x12e374 -> 0x2cd8cc): the object shows a model resource.
    m.service(6800, [this, object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return done(c, 0);
        }
        Resource* r = scene.resource(reference(c, 1));
        o->model = r ? r->model : nullptr;
        return done(c, r && r->model ? 1 : 0);
    });
    // Transforms of 3D objects (layout from the handlers' targets): scale
    // 0x1d0, rotation 0x254 (radians) and its offset 0x290, position 0x2d8
    // and its offset 0x314; drawn as position + offset and rotation +
    // offset. Per property: set at once, tween (x, y, z, frames, curve),
    // relative tween, get into three registers, still moving.
    auto set3 = [object](Call& c, adventure56::Tween<3> Object3D::* field) {
        Object3D* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        (o->*field).set(vec(c, 1, 3));
        return done(c);
    };
    auto tween3 = [object](Call& c, adventure56::Tween<3> Object3D::* field, bool relative) {
        Object3D* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        auto v = vec(c, 1, 3);
        if (relative) {
            // 0x13b974 / 0x13b5f8: added to the current value (0x2c9ebc,
            // 0x2c9f14), not to a pending target.
            const auto& t = (o->*field).value();
            for (int i = 0; i < 3; ++i) {
                v[std::size_t(i)] += t[std::size_t(i)];
            }
        }
        (o->*field).start(v, c.real(4), c.count() > 5 ? c.integer(5) : 0);
        return done(c);
    };
    // Rotations (0x254, and the offset at 0x290): 0x2631d8 sets each angle
    // wrapped (0x2af518); 0x26303c tweens from the wrapped current angle to
    // the wrapped target, first moving the current one by 2pi where the two
    // lie more than pi apart (the short way round); the relative tweens
    // (0x13c1b0 / 0x13be34) add to the current value.
    auto setAngles = [object](Call& c, adventure56::Tween<3> Object3D::* field) {
        Object3D* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        auto v = vec(c, 1, 3);
        for (auto& a : v) {
            a = wrapAngle(a);
        }
        (o->*field).set(v);
        return done(c);
    };
    auto turnAngles = [object](Call& c, adventure56::Tween<3> Object3D::* field, bool relative) {
        Object3D* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        adventure56::Tween<3>& t = o->*field;
        auto to = vec(c, 1, 3);
        auto from = t.value();
        for (std::size_t i = 0; i < 3; ++i) {
            if (relative) {
                to[i] += from[i];
            }
            from[i] = wrapAngle(from[i]);
            to[i] = wrapAngle(to[i]);
            const float d = to[i] - from[i];
            if (std::fabs(d) > 3.14159274f) { // 0x40490fdb
                from[i] = d >= 0.0f ? from[i] + 6.28318548f : from[i] - 6.28318548f;
            }
        }
        t.set(from);
        t.start(to, c.real(4), c.count() > 5 ? c.integer(5) : 0);
        return done(c);
    };
    auto get3 = [object](Call& c, adventure56::Tween<3> Object3D::* field,
                         adventure56::Tween<3> Object3D::* plus) {
        Object3D* o = object(c);
        for (std::size_t i = 0; i < 3; ++i) {
            if (Value* r = c.reg(1 + i)) {
                float v = o ? (o->*field)[int(i)] : 0.0f;
                if (o && plus) {
                    v += (o->*plus)[int(i)];
                }
                *r = Value::ofFloat(v);
            }
        }
        return done(c, o != nullptr);
    };
    auto moving3 = [object](Call& c, adventure56::Tween<3> Object3D::* field,
                            adventure56::Tween<3> Object3D::* also) {
        Object3D* o = object(c);
        return o ? done(c, (o->*field).moving() || (also && (o->*also).moving())) : false;
    };
    using adventure56::Tween;
    Tween<3> Object3D::* none = nullptr;
    // Each property's services follow one pattern (base + 0 still moving,
    // + 1 into registers, + 2 at once, + 3 tween, + 4 tween by a
    // difference; the offset the same at + 5 .. + 9): scale 6200 (0x1d0),
    // rotation 6220 (0x254, radians; offset 0x290), position 6240 (0x2d8;
    // offset 0x314).
    m.service(6200, [moving3, none](Call& c) { return moving3(c, &Object3D::scale, none); });
    m.service(6201, [get3, none](Call& c) { return get3(c, &Object3D::scale, none); });
    m.service(6202, [set3](Call& c) { return set3(c, &Object3D::scale); });
    m.service(6203, [tween3](Call& c) { return tween3(c, &Object3D::scale, false); });
    m.service(6204, [tween3](Call& c) { return tween3(c, &Object3D::scale, true); });
    // 6205..6209 (0x157c40, 0x13c52c, 0x13c70c, 0x13c7c4, 0x13c604): the
    // scale's offset (+0x20c), as the rotation's (the robot game's
    // Chr_ToDead sets it).
    m.service(6205, [moving3, none](Call& c) { return moving3(c, &Object3D::scale3Offset, none); });
    m.service(6206, [get3, none](Call& c) { return get3(c, &Object3D::scale3Offset, none); });
    m.service(6207, [set3](Call& c) { return set3(c, &Object3D::scale3Offset); });
    m.service(6208, [tween3](Call& c) { return tween3(c, &Object3D::scale3Offset, false); });
    m.service(6209, [tween3](Call& c) { return tween3(c, &Object3D::scale3Offset, true); });
    m.service(6220, [moving3, none](Call& c) { return moving3(c, &Object3D::rotation3, none); });
    // 6230 (0x157ae0 -> 0x2bec08): the rotation or its offset still turning.
    m.service(6230, [moving3](Call& c) {
        return moving3(c, &Object3D::rotation3, &Object3D::rotation3Offset);
    });
    m.service(6221, [get3, none](Call& c) { return get3(c, &Object3D::rotation3, none); });
    m.service(6222, [setAngles](Call& c) { return setAngles(c, &Object3D::rotation3); });
    m.service(6223, [turnAngles](Call& c) { return turnAngles(c, &Object3D::rotation3, false); });
    m.service(6224, [turnAngles](Call& c) { return turnAngles(c, &Object3D::rotation3, true); });
    m.service(6225,
              [moving3, none](Call& c) { return moving3(c, &Object3D::rotation3Offset, none); });
    m.service(6226, [get3, none](Call& c) { return get3(c, &Object3D::rotation3Offset, none); });
    m.service(6227, [setAngles](Call& c) { return setAngles(c, &Object3D::rotation3Offset); });
    m.service(6228,
              [turnAngles](Call& c) { return turnAngles(c, &Object3D::rotation3Offset, false); });
    m.service(6229,
              [turnAngles](Call& c) { return turnAngles(c, &Object3D::rotation3Offset, true); });
    // 6242 / 6243 / 6244 (0x2c9e9c / 0x2c9eac / 0x13b974) position, 6241
    // get, 6240 moving; 6247 / 6248 / 6249 (0x2c9ef4 / 0x2c9f04 /
    // 0x13b5f8) its offset, 6246 get, 6245 moving; 6250 (0x2bec64) either
    // moving; 6251 (0x2c9f4c -> 0x2cba0c) position + offset.
    m.service(6242, [set3](Call& c) { return set3(c, &Object3D::position); });
    m.service(6243, [tween3](Call& c) { return tween3(c, &Object3D::position, false); });
    m.service(6244, [tween3](Call& c) { return tween3(c, &Object3D::position, true); });
    m.service(6241, [get3, none](Call& c) { return get3(c, &Object3D::position, none); });
    m.service(6240, [moving3, none](Call& c) { return moving3(c, &Object3D::position, none); });
    m.service(6247, [set3](Call& c) { return set3(c, &Object3D::offset); });
    m.service(6248, [tween3](Call& c) { return tween3(c, &Object3D::offset, false); });
    m.service(6249, [tween3](Call& c) { return tween3(c, &Object3D::offset, true); });
    m.service(6246, [get3, none](Call& c) { return get3(c, &Object3D::offset, none); });
    m.service(6245, [moving3, none](Call& c) { return moving3(c, &Object3D::offset, none); });
    m.service(6250,
              [moving3](Call& c) { return moving3(c, &Object3D::position, &Object3D::offset); });
    m.service(6251, [get3](Call& c) { return get3(c, &Object3D::position, &Object3D::offset); });
    // 6875 (0x15d154 -> 0x18e6b8): a model's material alpha factor (only on
    // model content, 13).
    m.service(6875, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        if (!o->model) {
            return done(c, 0);
        }
        o->materialAlpha = c.real(1);
        return done(c, 1);
    });
    // Merged objects (content 0x10): 6890 (0x12ec90 -> 0x2cd2d0) makes the
    // object one (its model content goes); 6891 (0x12a700 -> 0x2cf200 /
    // 0x22434c) the model resource and the number of instances [, positions
    // only]; 6892 (0x128ca4 -> 0x222ca0 by mesh name / 0x222d98 by number)
    // gives the next instance its mesh; 6893 (0x128d88 -> 0x223ac0) bakes
    // them (fails when a mesh's vertex layout differs from the first's);
    // 6894 / 6895 / 6896 (0x223994 / 0x2239f8 / 0x223a5c) an instance's
    // position / rotation / scale.
    m.service(6890, [object](Call& c) {
        Object3D* o = object(c);
        if (!o) {
            return done(c, 0);
        }
        o->model = nullptr;
        o->merged = std::make_shared<Object3D::Merged>();
        return done(c, 1);
    });
    m.service(6891, [this, object](Call& c) {
        Object3D* o = object(c);
        if (!o || !o->merged || c.count() < 3) {
            return done(c, 0);
        }
        Resource* r = scene.resource(reference(c, 1));
        if (!r || !r->model) {
            return done(c, 0);
        }
        auto& g = *o->merged;
        g.model = r->model;
        g.instances.assign(std::size_t(std::max(0, c.integer(2))), {});
        g.added = 0;
        g.built = false;
        g.positionsOnly = c.count() > 3 && (c.integer(3) & 0xff) != 0;
        return done(c, 1);
    });
    m.service(6892, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || !o->merged || !o->merged->model || c.count() < 2) {
            return done(c, 0);
        }
        auto& g = *o->merged;
        const auto& meshes = g.model->meshes;
        int mesh = -1;
        if (const std::string* name = c.text(1)) {
            const std::uint32_t h = xq::hash(*name);
            for (std::size_t i = 0; i < meshes.size(); ++i) {
                const auto dot = meshes[i].name.rfind('.');
                if (xq::hash(meshes[i].name) == h ||
                    (dot != std::string::npos && xq::hash(meshes[i].name.substr(dot + 1)) == h)) {
                    mesh = int(i);
                    break;
                }
            }
        } else {
            mesh = c.integer(1);
        }
        if (mesh < 0 || mesh >= int(meshes.size()) || g.added >= g.instances.size()) {
            return done(c, 0);
        }
        g.instances[g.added++].mesh = mesh;
        return done(c, 1);
    });
    m.service(6893, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || !o->merged || !o->merged->model) {
            return done(c, 0);
        }
        auto& g = *o->merged;
        bool ok = g.added > 0 && !g.model->meshes.empty();
        const auto& first = g.model->meshes.front().vertices;
        for (std::size_t i = 0; ok && i < g.added; ++i) {
            const auto& vb = g.model->meshes[std::size_t(g.instances[i].mesh)].vertices;
            ok = std::equal(std::begin(vb.components), std::end(vb.components),
                            std::begin(first.components));
        }
        g.built = ok;
        return done(c, ok ? 1 : 0);
    });
    auto instanceSet = [object](Call& c, int which) {
        Object3D* o = object(c);
        if (!o || !o->merged || c.count() < 5) {
            return done(c, 0);
        }
        auto& g = *o->merged;
        const auto i = std::size_t(c.integer(1) & 0xff);
        if (i >= g.instances.size()) {
            return done(c, 0);
        }
        auto& in = g.instances[i];
        float* v = which == 0 ? in.position : which == 1 ? in.rotation : in.scale;
        for (int k = 0; k < 3; ++k) {
            v[k] = c.real(std::size_t(2 + k));
        }
        return done(c, 1);
    };
    m.service(6894, [instanceSet](Call& c) { return instanceSet(c, 0); });
    // 6898 (sixth adventure 0x1a67ac): merged instance [i] of the first
    // object goes where a bone of the second object's model is (first,
    // second, i, bone): the bone's matrix applied to the origin (0x370ae8,
    // the bone record's +0x78) is stored as the instance's position as it
    // is (0x25826c -> 0x2f22f4, instance +0x10). Result 1 when both exist.
    m.service(6898, [this](Call& c) {
        Object3D* a = c.count() >= 4 ? scene.object3D(reference(c, 0)) : nullptr;
        const Object3D* b = c.count() >= 4 ? scene.object3D(reference(c, 1)) : nullptr;
        if (!a || !b || !a->merged || !b->model) {
            return done(c, 0);
        }
        const int bone = b->model->bone(reference(c, 3));
        const auto i = std::size_t(c.integer(2));
        if (bone < 0 || i >= a->merged->instances.size()) {
            return done(c, 0);
        }
        const auto pose = scene.posedModel(*b, *b->model);
        adventure56::Affine m = scene.worldMatrix(*b, 1.0f, true);
        if (std::size_t(bone) < pose.size()) {
            m = m * pose[std::size_t(bone)];
        }
        const float origin[3] = {0, 0, 0};
        const auto p = m.apply(origin);
        std::copy(p.begin(), p.end(), a->merged->instances[i].position);
        return done(c, 1);
    });
    m.service(6895, [instanceSet](Call& c) { return instanceSet(c, 1); });
    m.service(6896, [instanceSet](Call& c) { return instanceSet(c, 2); });
    // 6810 / 6811: the model's motion by name / its speed (kept; motions
    // are not played yet).
    m.service(6810, [object](Call& c) {
        Object3D* o = object(c);
        // 0x14d6c4 -> 0x14e5ac: without a motion operand it fails (result
        // 0; the ruins' events loop on it while a part's motion plays).
        if (!o || c.count() < 2) {
            return false;
        }
        // Flags (third operand, as 5622's): 4 restarts a motion already
        // playing, 8 plays it once and holds (the verdict motions, u8).
        const std::uint32_t flags = c.count() > 2 ? c.unsignedInt(2) : 0;
        // The motion is a reference (0x2606a4 in the sixth adventure's
        // 0x199c08): a name, or its hash (the sixth adventure's intro).
        std::string name = textOf(c, 1);
        if (!c.at(1).isText() && o->model) {
            const std::uint32_t h = reference(c, 1);
            for (const auto& mo : o->model->motions) {
                if (mo.hash == h || xq::hash(mo.name) == h) {
                    name = mo.name;
                }
            }
        }
        // [layer] (the sixth adventure's fourth operand, 0x199c08): layers
        // above 0 play over layer 0 (Object::upperMotions).
        const int layer = c.count() > 3 ? c.integer(3) : 0;
        if (layer >= 1 && layer <= int(o->upperMotions.size())) {
            auto& l = o->upperMotions[std::size_t(layer - 1)];
            if (l.motion != name || (flags & 4) || l.done) {
                l.time = 0;
            }
            l.motion = name;
            l.once = (flags & 8) != 0;
            l.done = false;
            l.stops = false;
            return done(c);
        }
        if (o->motion != name || (flags & 4) || o->motionDone) {
            o->motionTime = 0;
        }
        o->motion = name;
        o->motionStops = false;
        o->motionOnce = (flags & 8) != 0;
        o->motionDone = false;
        return done(c);
    });
    // The model's motion layer (content 13: layers at +0x10c, 0x1dc bytes
    // each; the reconstruction keeps one): 6813 (0x15ad18 -> 0x3403f0)
    // whether it ran to its end (+0x59 bit 1), 6812 (0x14d5fc -> 0x18f76c) its frame
    // (clamped to the motion), 6814 (0x160090 -> 0x2bc954) how far the
    // named motion has come (0..1) into a register, 6815 (0x15ffdc ->
    // 0x340770) the motion's name into a register.
    auto motionOf = [](const Object3D* o) -> const adventure56::Motion* {
        return o && o->model && !o->motion.empty() ? o->model->motion(o->motion) : nullptr;
    };
    // 6813 (0x15ad18 -> 0x3403f0): a model's motion layer [layer] has run
    // to its end (the layer's flag 0x2 at +0x59; a played-once motion that
    // finished). Puzzle 069 waits on it while a slice turns.
    m.service(6813, [object, motionOf](Call& c) {
        const Object3D* o = object(c);
        if (!o) {
            return false;
        }
        // [layer]: the upper layers' own end flags.
        const int layer = c.count() > 1 ? c.integer(1) : 0;
        if (layer >= 1 && layer <= int(o->upperMotions.size())) {
            const auto& l = o->upperMotions[std::size_t(layer - 1)];
            return done(c, !l.motion.empty() && l.done ? 1 : 0);
        }
        return done(c, motionOf(o) && o->motionDone ? 1 : 0);
    });
    m.service(6812, [object, motionOf](Call& c) {
        Object3D* o = object(c);
        const auto* mo = motionOf(o);
        if (!o || c.count() < 2) {
            return false;
        }
        if (!mo) {
            return done(c, 0);
        }
        const float length = float(std::max(1, mo->last - mo->first + 1));
        o->motionTime = std::clamp(c.real(1), 0.0f, length - 1);
        return done(c, 1);
    });
    m.service(6814, [object, motionOf](Call& c) {
        const Object3D* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        const auto* mo = motionOf(o);
        float progress = 0;
        // The motion as a name or its hash (6810's reference; the sixth
        // adventure's TearBomb passes the hash).
        const bool named = c.at(1).isText() ? o->motion == textOf(c, 1)
                                            : mo && (mo->hash == reference(c, 1) ||
                                                     xq::hash(o->motion) == reference(c, 1));
        if (mo && named) {
            progress = o->motionTime / float(std::max(1, mo->last - mo->first));
        }
        if (Value* r = c.reg(2)) {
            *r = Value::ofFloat(progress);
        }
        return done(c, 1);
    });
    m.service(6815, [object](Call& c) {
        const Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        if (Value* r = c.reg(1)) {
            *r = o->motion.empty() ? Value{} : Value::ofText(o->motion);
        }
        return done(c, 1);
    });
    m.service(6811, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->motionSpeed = c.real(1);
        return done(c);
    });
    // 6818 / 5818 (0x1b324c / 0x1b2fac -> 0x1ca9bc -> 0x1e4784): a motion
    // layer's stop frame (frame [, layer]); see Object::motionStop.
    m.service(6818, [object](Call& c) {
        Object3D* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const int layer = c.count() > 2 ? c.integer(2) : 0;
        if (layer >= 1 && layer <= int(o->upperMotions.size())) {
            auto& l = o->upperMotions[std::size_t(layer - 1)];
            l.stops = true;
            l.stop = c.real(1);
        } else if (layer == 0) {
            o->motionStops = true;
            o->motionStop = c.real(1);
        } else {
            return false;
        }
        return done(c);
    });
    // Cameras (per screen; vtables of 0x4007e0). Eye, target and the field
    // of view are each a base plus an offset, both tweened (0x344 + 0x380,
    // 0x3c8 + 0x404, 0x44c + 0x470). 4200 stereo distance (0x498), 4220
    // stereo strength (0x4e0/0x528).
    auto camera = [this](Call& c) -> adventure56::Camera* {
        const int s = c.integer(0);
        return s >= 0 && s < adventure56::Scene::kScreens ? &scene.cameras[s] : nullptr;
    };
    m.service(4200, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 2) {
            return false;
        }
        cam->stereoDistance.set({c.real(1)});
        return done(c);
    });
    m.service(4220, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 2) {
            return false;
        }
        cam->stereoStrength.set({c.real(1)});
        return done(c);
    });
    // 4202 / 4222 (0x13d280 / 0x149ea8): the same over frames (screen,
    // value, frames[, curve]).
    m.service(4202, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 3) {
            return false;
        }
        cam->stereoDistance.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    m.service(4222, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 3) {
            return false;
        }
        cam->stereoStrength.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    // 4105 (0x15801c): the camera still moving (an offset tween runs).
    m.service(4105, [camera](Call& c) {
        auto* cam = camera(c);
        return cam ? done(c, cam->eyeOffset.moving() || cam->targetOffset.moving()) : false;
    });
    // Setters at once: 4102 eye base (vtable +0x58), 4107 eye offset (+0x5c),
    // 4122 target base (+0x74), 4127 target offset (+0x78); field of view:
    // 4142 base (+0x90), 4147 offset (+0x94).
    auto setVector = [camera](Call& c, adventure56::Tween<3> adventure56::Camera::* field) {
        auto* cam = camera(c);
        if (!cam || c.count() < 4) {
            return false;
        }
        (cam->*field).set({c.real(1), c.real(2), c.real(3)});
        return done(c);
    };
    m.service(4102, [setVector](Call& c) { return setVector(c, &adventure56::Camera::eye); });
    m.service(4107, [setVector](Call& c) { return setVector(c, &adventure56::Camera::eyeOffset); });
    m.service(4122, [setVector](Call& c) { return setVector(c, &adventure56::Camera::target); });
    m.service(4127,
              [setVector](Call& c) { return setVector(c, &adventure56::Camera::targetOffset); });
    m.service(4142, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 2) {
            return false;
        }
        cam->fov.set({c.real(1)});
        return done(c);
    });
    // 4143 (0x13d7a0 -> 0x2c99d4): the field of view's base to a value over
    // frames [, curve] (the horse ride's alleys widen the view).
    m.service(4143, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 3) {
            return false;
        }
        cam->fov.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    // 4148 (0x13d644 -> 0x2c9b68, vtable +0x9c): the field of view's
    // offset to a value over frames [, curve] (as 4143 is the base's, +0x98).
    m.service(4148, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 3) {
            return false;
        }
        cam->fovOffset.start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    m.service(4147, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 2) {
            return false;
        }
        cam->fovOffset.set({c.real(1)});
        return done(c);
    });
    // Tweens: 4108 (0x13e050 -> +0x64) the eye offset to (x, y, z) over
    // frames [, curve]; 4109 (0x13ddbc) the same by a difference; 4128
    // (0x13e954 -> +0x80) the target offset.
    auto tweenVector = [camera](Call& c, adventure56::Tween<3> adventure56::Camera::* field,
                                bool relative) {
        auto* cam = camera(c);
        if (!cam || c.count() < 5) {
            return false;
        }
        std::array<float, 3> v{c.real(1), c.real(2), c.real(3)};
        if (relative) {
            for (int i = 0; i < 3; ++i) {
                v[std::size_t(i)] += (cam->*field)[i];
            }
        }
        (cam->*field).start(v, c.real(4), c.count() > 5 ? c.integer(5) : 0);
        return done(c);
    };
    m.service(4108, [tweenVector](Call& c) {
        return tweenVector(c, &adventure56::Camera::eyeOffset, false);
    });
    m.service(4109, [tweenVector](Call& c) {
        return tweenVector(c, &adventure56::Camera::eyeOffset, true);
    });
    m.service(4128, [tweenVector](Call& c) {
        return tweenVector(c, &adventure56::Camera::targetOffset, false);
    });
    // Getters into three registers: 4101 (0x13e148) the eye base, 4121
    // (0x13ea4c) the target base, 4126 (0x13e608) the target offset.
    auto getVector = [camera](Call& c, adventure56::Tween<3> adventure56::Camera::* field) {
        auto* cam = camera(c);
        if (!cam) {
            return false;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (Value* r = c.reg(1 + i)) {
                *r = Value::ofFloat((cam->*field)[int(i)]);
            }
        }
        return done(c);
    };
    m.service(4101, [getVector](Call& c) { return getVector(c, &adventure56::Camera::eye); });
    m.service(4121, [getVector](Call& c) { return getVector(c, &adventure56::Camera::target); });
    // 4106 (0x13dd10): the eye offset (+0x380, brought up to date when a
    // tween changed it). prog_place's Place_CopyCameraParam copies it.
    m.service(4106, [getVector](Call& c) { return getVector(c, &adventure56::Camera::eyeOffset); });
    m.service(4126,
              [getVector](Call& c) { return getVector(c, &adventure56::Camera::targetOffset); });
    // 4190 (0x1498e8): an orbit (screen, target x, y, z, distance [, yaw
    // [, pitch]] in degrees): the target base is set (0x2c92d8, as 4122) and
    // the eye base at target + R(q(Y, yaw) * q(X, pitch)) (0, 0, distance)
    // (0x2c1d30 axis-angle quaternions, 0x2d1124 product, 0x2b8258 matrix,
    // 0x2b8394 transform; 0x2c93a4, as 4102): the pitch turns the offset
    // about x first (y = -d sin(pitch)), then the yaw about y.
    m.service(4190, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 5) {
            return false;
        }
        const float kRad = 3.14159265f / 180.0f;
        const float tx = c.real(1), ty = c.real(2), tz = c.real(3), d = c.real(4);
        const float yaw = c.count() > 5 ? c.real(5) * kRad : 0.0f,
                    pitch = c.count() > 6 ? c.real(6) * kRad : 0.0f;
        cam->target.set({tx, ty, tz});
        cam->eye.set({tx + d * std::cos(pitch) * std::sin(yaw), ty - d * std::sin(pitch),
                      tz + d * std::cos(pitch) * std::cos(yaw)});
        return done(c);
    });
    // 4005 (0x133df8 -> 0x32da10): a screen camera's method +0x30 with a
    // switch (before the verdict's result: "4005 0 1"); accepted.
    m.service(4005, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam) {
            return false;
        }
        if (c.count() > 1) {
            cam->viewShown = c.at(1).truthy();
        }
        return done(c);
    });
    // 4006 / 4007 (sixth adventure 0x182448 / 0x1824b0 -> the screen's
    // view, vtable +0x34 / +0x38: clear or set bit 0 of +0x2e0 / +0x2e4,
    // the bits 3011 / 3012 set for all screens; screen, switch). The
    // nazo intro switches every view off (3012 0) and screens 0 and 4 back
    // on. Kept in Camera (read by 4022).
    for (const int op : {4006, 4007}) {
        m.service(op, [camera, op](Call& c) {
            auto* cam = camera(c);
            if (!cam || c.count() < 2) {
                return false;
            }
            (op == 4006 ? cam->viewTouch : cam->viewActive) = c.at(1).truthy();
            return done(c);
        });
    }
    // 4404 (0x13d870): a value for a world point seen from a screen's
    // camera (screen, x, y, z); the pointer puzzles feed it to the cursor's
    // stereo depth (5263) only. Hypothesis: the point's depth for the 3D
    // effect; 0 in a single image.
    m.service(4404, [camera](Call& c) {
        if (!camera(c)) {
            return false;
        }
        c.result = Value::ofFloat(0);
        return true;
    });
    // 4300 (0x129bc0 -> 0x2c115c): the camera's +0x570 value (screen, x,
    // y) at once; 4302 (0x129c60 -> 0x2c11dc) over frames [, curve];
    // 4304 (0x14572c -> 0x2c42a0): still changing.
    m.service(4300, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 3) {
            return false;
        }
        cam->shake.set({c.real(1), c.real(2)});
        return done(c);
    });
    m.service(4302, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 4) {
            return false;
        }
        cam->shake.start({c.real(1), c.real(2)}, c.real(3), c.count() > 4 ? c.integer(4) : 0);
        return done(c);
    });
    m.service(4304, [camera](Call& c) {
        auto* cam = camera(c);
        return cam ? done(c, cam->shake.moving() ? 1 : 0) : false;
    });
    // 4003 (0x13357c -> 0x2d1484): a camera object "name", kind; 4010
    // (0x13348c -> 0x2cbdc4): a screen uses it. The screens' cameras are
    // fixed here (the puzzle camera, a RAM dump's values: see Camera).
    // 4002 (0x125428) / 4003 (0x13357c): a camera object "name", kind
    // (4002 the verdict's, 4003 the puzzle's); 4010: a screen uses it.
    auto createRig = [this](Call& c) {
        if (c.count() < 2) {
            return done(c, 0);
        }
        adventure56::CameraRig rig;
        rig.kind = c.integer(1);
        rig.scope = scene.scopeDepth();
        if (rig.kind == 4) {
            rig.nearZ = 1.0f;
            rig.farZ = 10241.0f;
        }
        scene.rigs[reference(c, 0)] = rig;
        return done(c, 1);
    };
    m.service(4002, createRig);
    m.service(4003, createRig);
    // 4010 (0x13348c -> 0x2cbdc4 -> 0x202bc4): a screen shows a camera
    // object (0: its own); with a third operand set the new one takes the
    // old one's values (vtable +0x10), else it shows its own.
    m.service(4010, [this, camera](Call& c) {
        if (!camera(c) || c.count() < 2) {
            return false;
        }
        const int s = c.integer(0);
        const std::uint32_t from = scene.screenRig[s], to = reference(c, 1);
        if (from == to) {
            return done(c);
        }
        if (const auto it = scene.rigs.find(from); from && it != scene.rigs.end()) {
            it->second.values = scene.cameras[s];
        } else {
            scene.screenDefault[s] = scene.cameras[s];
            scene.screenDefaultKept[s] = true;
        }
        const bool copy = c.count() > 2 && c.at(2).truthy();
        if (!copy) {
            if (const auto it = scene.rigs.find(to); to && it != scene.rigs.end()) {
                scene.cameras[s] = it->second.values;
            } else if (scene.screenDefaultKept[s]) {
                scene.cameras[s] = scene.screenDefault[s];
            }
        }
        scene.screenRig[s] = to;
        return done(c);
    });
    // 4500 (0x1298f4 -> 0x2cf93c): a camera object's motions (camera,
    // "evt/cmr/x" folder .xv [, flags]).
    m.service(4500, [this](Call& c) {
        auto it = scene.rigs.find(reference(c, 0));
        const fs::path folder = c.count() > 1 ? pack.find(textOf(c, 1), ".xv") : fs::path{};
        if (it == scene.rigs.end() || folder.empty()) {
            return done(c, 0);
        }
        it->second.set = adventure56::loadCameraSet(pack, folder);
        return done(c);
    });
    // 4602 (0x13dbac -> 0x2c9470): play a clip by name (camera, "01"
    // [, flags [, ...]]); 4600 (0x149f6c): +0x824, the clip has ended (the
    // scripts wait while it is 0).
    m.service(4602, [this](Call& c) {
        auto it = scene.rigs.find(reference(c, 0));
        if (it == scene.rigs.end() || !it->second.set || c.count() < 2) {
            return done(c, 0);
        }
        auto& rig = it->second;
        rig.clip = rig.set->clip(reference(c, 1));
        rig.time = 0;
        rig.finished = rig.clip == nullptr;
        return done(c, rig.clip ? 1 : 0);
    });
    m.service(4600, [this](Call& c) {
        auto it = scene.rigs.find(reference(c, 0));
        return done(c, it == scene.rigs.end() || it->second.finished ? 1 : 0);
    });
    // 4210 (0x149bf8 -> camera +0xc4), 4215 / 4225 (0x15d3ac / 0x15d604 ->
    // +0xd0 / +0xe8): more stereoscopic parameters (seen together with
    // 4200 / 4220 in CreateNazoCamera). Kept, not used by a single image.
    for (int op : {4210, 4215, 4225}) {
        m.service(op, [camera, op](Call& c) {
            auto* cam = camera(c);
            if (!cam || c.count() < 2) {
                return false;
            }
            cam->stereoExtra[op == 4210 ? 0 : op == 4215 ? 1 : 2] = c.real(1);
            if (op == 4210) {
                cam->stereoBase.set({c.real(1)});
            }
            return done(c);
        });
    }
    // The sixth adventure's depth slots (see Camera): 4201 / 4211 / 4221
    // (0x1957f0 / 0x1955d0 / 0x195a10 -> 0x253290 / 0x25319c) read the
    // current value of 4200's / 4210's / 4220's slot into a register
    // (screen, register; 0 without a view); 4230 (0x1954d0 -> 0x2531fc)
    // sets its own slot, 4212 / 4232 (0x195740 / 0x195520) tween 4210's /
    // 4230's (screen, value, frames, curve) as 4202 / 4222 do.
    using Slot = adventure56::Tween<1> adventure56::Camera::*;
    auto readSlot = [camera](Call& c, Slot slot) {
        auto* cam = camera(c);
        if (c.count() < 2) {
            return false;
        }
        if (Value* r = c.reg(1)) {
            *r = Value::ofFloat(cam ? (cam->*slot)[0] : 0.0f);
        }
        return done(c);
    };
    m.service(4201,
              [readSlot](Call& c) { return readSlot(c, &adventure56::Camera::stereoDistance); });
    m.service(4211, [readSlot](Call& c) { return readSlot(c, &adventure56::Camera::stereoBase); });
    m.service(4221,
              [readSlot](Call& c) { return readSlot(c, &adventure56::Camera::stereoStrength); });
    m.service(4230, [camera](Call& c) {
        auto* cam = camera(c);
        if (!cam || c.count() < 2) {
            return false;
        }
        cam->stereoNear.set({c.real(1)});
        return done(c);
    });
    for (const auto& [op, slot] : {std::pair<int, Slot>{4212, &adventure56::Camera::stereoBase},
                                   {4232, &adventure56::Camera::stereoNear}}) {
        m.service(op, [camera, slot](Call& c) {
            auto* cam = camera(c);
            if (!cam || c.count() < 3) {
                return false;
            }
            (cam->*slot).start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
            return done(c);
        });
    }
    // 4161 / 4181 (sixth adventure 0x18f2d4 / 0x18adf8 -> view vtable +0x50
    // / +0x44, slots +0x350 / +0x32c; fifth 0x142190 / 0x13d398 -> +0x330 /
    // +0x32c, which its camera kinds set to 10 / 10250, 0x1aed78): the near
    // / far clip distance (screen, value). With a third operand the named
    // camera clip's instead (not modelled: result 0).
    for (const bool far : {false, true}) {
        m.service(far ? 4181 : 4161, [this, camera, far](Call& c) {
            if (c.count() < 2) {
                return false;
            }
            auto* cam = c.count() > 2 ? nullptr : camera(c);
            if (!cam) {
                return done(c, 0);
            }
            (far ? cam->farZ : cam->nearZ) = c.real(1);
            // The screen's camera is the camera object it shows (4010), so
            // the distance is that object's too (the rabbit's room: its
            // "cmr_move" with near 1).
            const int s = c.integer(0);
            if (const auto it = scene.rigs.find(scene.screenRig[s]);
                scene.screenRig[s] && it != scene.rigs.end()) {
                (far ? it->second.farZ : it->second.nearZ) = c.real(1);
            }
            return done(c, 1);
        });
    }
    // 4022 (0x195d0c -> 0x37f440): the view is active (4007's switch; 0
    // without a view).
    m.service(4022, [camera](Call& c) {
        auto* cam = camera(c);
        return done(c, cam && cam->viewActive ? 1 : 0);
    });
    // 4405 (0x18f160 -> 0x35d14c -> 0x36bce8): a depth (value [, screen])
    // turned into a stereo value from the view's projection words (+0x18,
    // +0x2c, +0x3c), 4200's slot, the slot at +0x544 and a global 0x3d973c
    // (10.0 in the reference), 0 when the view is not shown. Its words are
    // not mapped yet: result 0 (open item; the result only feeds 4200's
    // stereo slot, which a single image does not draw).
    m.service(4405, [](Call& c) { return c.count() > 0 ? done(c, 0) : false; });
    // 5800..5899: the models on 2D objects (5800 model, 5810.. motions,
    // 5820.. face motions, 5830.. material motions, 5865 mesh switch ...),
    // where no 2D service of that number exists.
    for (int op = 6800; op < 6900; ++op) {
        if (!m.hasService(op) || m.hasService(op - 1000)) {
            continue;
        }
        m.service(op - 1000, [handler = m.handler(op)](Call& c) {
            on2D = true;
            const bool ok = handler(c);
            on2D = false;
            return ok;
        });
    }
}

void Game::registerGameServices() {
    auto& m = machine;
    // Flags (0x14f588 get, 0x14f6d4 set, 0x14bab0 on, 0x14f62c off): by
    // name (lt5_def 2001) or number.
    auto flagId = [this](const Call& c) -> int {
        const Value& v = c.at(0);
        if (v.isText()) {
            const auto it = defs.flagIds.find(v.text);
            return it == defs.flagIds.end() ? -1 : it->second;
        }
        return v.toInt();
    };
    // A flag's bit: the named flag's first bit (its record's +4) plus the
    // index operand, when there is one (0x2d5214: below 0x880).
    auto flagBit = [flagId](const Call& c, std::size_t index) -> int {
        const int id = flagId(c);
        const int bit = id < 0 ? -1 : id + (c.count() > index ? c.integer(index) : 0);
        return bit >= 0 && bit < 0x880 ? bit : -1;
    };
    // 16200 {flag [, index]}.
    m.service(16200, [this, flagBit](Call& c) {
        const int id = flagBit(c, 1);
        return done(c, id >= 0 && flag(id) ? 1 : 0);
    });
    // 16201 {flag, on} or {flag, index, on}.
    m.service(16201, [this, flagBit](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int id = c.count() > 2 ? flagBit(c, 1) : flagBit(c, c.count());
        if (id >= 0) {
            setFlag(id, c.at(c.count() > 2 ? 2 : 1).truthy());
        }
        return done(c, id >= 0);
    });
    // 16202 / 16203 {flag [, index]}: on / off.
    m.service(16202, [this, flagBit](Call& c) {
        const int id = flagBit(c, 1);
        if (id >= 0) {
            setFlag(id, true);
        }
        return done(c, id >= 0);
    });
    m.service(16203, [this, flagBit](Call& c) {
        const int id = flagBit(c, 1);
        if (id >= 0) {
            setFlag(id, false);
        }
        return done(c, id >= 0);
    });
    // Variables (0x14fa04 get, 0x14fb60 set): by name (lt5_def 2011) or
    // byte offset; [slot] for arrays.
    auto variableOf = [this](const Call& c) -> const Definitions::Variable* {
        const Value& v = c.at(0);
        if (v.isText()) {
            const auto it = defs.variables.find(v.text);
            return it == defs.variables.end() ? nullptr : &it->second;
        }
        const auto it = defs.variablesByOffset.find(v.toInt());
        return it == defs.variablesByOffset.end() ? nullptr : &it->second;
    };
    // 15102 (0x15be28 -> 0x33ef70): a sound scene's effect set (-1 for an
    // unknown scene); the verdict reloads the puzzle's set with it (11023
    // 1 n).
    m.service(15102, [this](Call& c) {
        const auto it = defs.soundScenes.find(c.integer(0));
        return done(c, it == defs.soundScenes.end() ? -1 : it->second.effects);
    });
    // 16524 (0x165048 -> 0x185308): one more picarat reduction for the
    // current puzzle (at most 3); 0 in the special mode (+0x12fcc).
    m.service(16524, [this](Call& c) {
        if (!puzzle) {
            return done(c, 0);
        }
        auto& p = progress[puzzle->id];
        p.reductions = std::min(3, p.reductions + 1);
        return done(c, 1);
    });
    // 16520 (0x16576c -> 0x2d4f24): the current puzzle's saved state.
    m.service(16520, [this](Call& c) {
        const auto it = puzzle ? progress.find(puzzle->id) : progress.end();
        return done(c, it == progress.end() ? 0 : it->second.state);
    });
    // 16522 (0x164fa4 -> 0x33f3bc): its picarat reductions.
    m.service(16522, [this](Call& c) {
        const auto it = puzzle ? progress.find(puzzle->id) : progress.end();
        return done(c, it == progress.end() ? 0 : it->second.reductions);
    });
    // 16560 (0x15ebf0 -> 0x33f2c8): the play time in seconds (the puzzle
    // frame adds each puzzle's time to a variable, capped at 360000).
    m.service(16560, [this](Call& c) {
        c.result = Value::ofUint(std::uint32_t((std::int64_t(frame) - playStart) / 60));
        return true;
    });
    m.service(16300, [this, variableOf](Call& c) {
        const auto* v = variableOf(c);
        const std::int32_t value = v ? variable(*v, c.count() > 1 ? c.integer(1) : 0) : 0;
        if (traceHandlers && log && c.at(0).isText() && c.at(0).text == names::verdict &&
            value != -1) {
            log("verdict read " + std::to_string(value) + " at " + c.frame.program->name() + ":" +
                c.frame.block->name);
        }
        return done(c, value);
    });
    m.service(16301, [this, variableOf](Call& c) {
        const auto* v = variableOf(c);
        if (v && c.count() >= 2) {
            const bool slotted = c.count() > 2;
            setVariable(*v, c.integer(slotted ? 2 : 1), slotted ? c.integer(1) : 0);
            if (traceHandlers && log) {
                log("variable " + c.at(0).text + " = " +
                    std::to_string(c.integer(slotted ? 2 : 1)) + " at " + c.frame.program->name() +
                    ":" + c.frame.block->name + "#" + std::to_string(c.frame.pc - 1));
            }
        }
        return done(c, v != nullptr);
    });
    // 2500 (0x129604 -> 0x2cfbf0): a screen's brightness as it is now (its
    // change stops).
    m.service(2500, [this](Call& c) {
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            return false;
        }
        scene.brightness[screen].set(scene.brightness[screen].value());
        c.result = Value::ofFloat(scene.brightness[screen][0]);
        return true;
    });
    // 2509 (0x1454e0 -> 0x351b10 -> 0x25f7dc): a screen's brightness is
    // still changing.
    m.service(2509, [this](Call& c) {
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            return false;
        }
        return done(c, scene.brightness[screen].moving() ? 1 : 0);
    });
    // 2501 / 2502 (0x129650 / 0x12c560): a screen's brightness at once /
    // over frames (screen, level [, frames [, curve]]): -1 black, 0 normal.
    m.service(2501, [this](Call& c) {
        const int s = c.integer(0);
        if (s < 0 || s > 1) {
            return false;
        }
        scene.brightness[s].set({c.real(1)});
        return done(c);
    });
    m.service(2502, [this](Call& c) {
        const int s = c.integer(0);
        if (s < 0 || s > 1 || c.count() < 3) {
            return false;
        }
        scene.brightness[s].start({c.real(1)}, c.real(2), c.count() > 3 ? c.integer(3) : 0);
        return done(c);
    });
    // 2220 (0x12ce4c -> 0x334b28): start a block on the loop's second
    // thread (a per-frame update; "" stops it).
    // Touch queries (1200..1215; the optional operand names the input
    // source, 0 the touch screen - others read nothing here).
    // 1200 (0x126470): not touching; 1201 (0x123de8): touching (0x2d3388:
    // a new press or held, input bits 6); 1202 (0x123e84 -> 0x2d335c, bit
    // 8): released; 1203 (0x12a9b4 -> 0x2cf128, bit 2): a new press. All
    // answer 0 while the loop's input byte (+0x1a, 2109) is set.
    auto touchSource = [this](Call& c) {
        return touchFrom(c.count() == 0 ? 0 : c.integer(0));
    };
    m.service(1200, [this, touchSource](Call& c) {
        return done(c, touchSource(c) && !input.touching ? 1 : 0);
    });
    m.service(1201, [this, touchSource](Call& c) {
        return done(c, touchSource(c) && input.touching ? 1 : 0);
    });
    m.service(1202, [this, touchSource](Call& c) {
        return done(c, touchSource(c) && released() ? 1 : 0);
    });
    m.service(1203, [this, touchSource](Call& c) {
        return done(c, touchSource(c) && pressed() ? 1 : 0);
    });
    // 1210 / 1211 (0x126048 / 0x1260e0): the touch position; 1212 / 1213
    // (0x126340 / 0x1263d8): where the press began; 1214 / 1215 (0x126178 /
    // 0x12625c): this frame's movement.
    // With operand 1 the cursor's point (game +0x520 + 4 * source).
    auto cursorSource = [](Call& c) {
        return c.count() > 0 && c.integer(0) == 1;
    };
    m.service(1210, [this, cursorSource](Call& c) {
        return done(c, cursorSource(c) ? int(std::floor(padCursor.x + 0.5f)) : touchX);
    });
    m.service(1211, [this, cursorSource](Call& c) {
        return done(c, cursorSource(c) ? int(std::floor(padCursor.y + 0.5f)) : touchY);
    });
    m.service(1212, [this, cursorSource](Call& c) {
        return done(c, cursorSource(c) ? padCursor.pressX : pressX);
    });
    m.service(1213, [this, cursorSource](Call& c) {
        return done(c, cursorSource(c) ? padCursor.pressY : pressY);
    });
    m.service(1214, [this, touchSource](Call& c) { return done(c, touchSource(c) ? moveX : 0); });
    m.service(1215, [this, touchSource](Call& c) { return done(c, touchSource(c) ? moveY : 0); });
    // 3021 (0x12c838 -> 0x3467c0): pick a 2D object at (screen, x, y
    // [, one to skip]): the screen's objects from the top down, each by its
    // touch rectangles (the last first) or else its picture's or current
    // animation's bounds (0x27233c); 3025 then names it.
    m.service(3021, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const std::uint32_t skip = c.count() > 3 ? reference(c, 3) : 0;
        scene.pick = scene.pick2DAt(c.integer(0), c.real(1), c.real(2), skip);
        return done(c, scene.pick.hit ? 1 : 0);
    });
    // Picking (the pointer puzzles' cursor): 3022 (0x12c94c -> 0x350a74)
    // casts a ray through (screen, x, y) against the 3D objects' touch
    // boxes and keeps the nearest hit; 3025 (0x129df0) its object's name,
    // 3026 (0x129d70) the hit point into up to three registers, 3020
    // (0x12323c) forgets it.
    m.service(3022, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        scene.pick = scene.pickAt(c.integer(0), c.real(1), c.real(2));
        return done(c, scene.pick.hit ? 1 : 0);
    });
    m.service(3025, [this](Call& c) {
        Object* o = scene.pick.hit ? scene.find(scene.pick.solid, scene.pick.hash) : nullptr;
        c.result = o ? Value::ofText(o->name) : Value{};
        return true;
    });
    m.service(3026, [this](Call& c) {
        for (std::size_t i = 0; i < 3 && i < c.count(); ++i) {
            if (Value* r = c.reg(i)) {
                *r = Value::ofFloat(scene.pick.point[i]);
            }
        }
        return done(c);
    });
    m.service(3020, [this](Call& c) {
        scene.pick = {};
        return done(c);
    });
    // 1205 (0x12650c): a tap (the game's input word +0x534 bit 0x40, set by
    // 0x3a61f0 at a release: Game::beginFrame has the rule), while the
    // loop's input switch (2109) is off.
    // 1204 (0x130238): bit 0 of the touch word +0x534 with the touch
    // screen as the source (bit 8 clear). *Hypothesis*: bit 0 is the held
    // touch's repeat pulse (Slct_Touch asks it while an arrow is held; the
    // tracker's only repeat is its 0x400 pulse, Game::repeatPulse).
    m.service(1204, [this, touchSource](Call& c) {
        return done(c,
                    touchSource(c) && input.touching && touchSourceId == 0 && repeatPulse ? 1 : 0);
    });
    // 1300 (0x13fa34 -> 0x142be4 -> 0x2c4d18): a password for another
    // game's secret door from a key ("LT5TOLT4"): h = crc32 over the key
    // and the console's address (0x2c4df0, 0x250d34); two characters a
    // round from the alphabet in the code (0x2c4dcc, 32 entries), nibble k
    // of byte i at ((h >> (8i + 4k)) & 15) + 22i + 11k, wrapped at 32.
    // Console check: h 0xbf7f9cc1 gives the reference's "BA9QTYL3".
    m.service(1300, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const std::string key = textOf(c, 0);
        std::string input = key;
        input.append(reinterpret_cast<const char*>(consoleAddress.data()), consoleAddress.size());
        const std::uint32_t h = xq::hash(input);
        const auto& code = this->code();
        constexpr std::uint32_t kAlphabet = 0x2c4dcc - 0x100000;
        std::string password;
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 2; ++k) {
                const std::uint32_t at =
                    ((h >> (8 * i + 4 * k)) & 15u) + std::uint32_t(22 * i + 11 * k);
                const std::size_t a = kAlphabet + (at & 31u);
                password += a < code.size() ? char(code[a]) : '?';
            }
        }
        c.result = Value::ofText(password);
        return true;
    });
    m.service(1205,
              [this, touchSource](Call& c) { return done(c, touchSource(c) && tapped ? 1 : 0); });
    // 2211 (0x1307c8 -> 0x2cdc08): calls a block (with up to eight
    // arguments) as a callback in the loop's main thread; the interrupted
    // block resumes when it returns.
    m.service(2211, [this](Call& c) {
        xq::Thread* t = loopThreads[0];
        if (!t || c.count() == 0 || !t->running()) {
            return done(c, 0);
        }
        std::vector<Value> args;
        for (std::size_t i = 1; i < c.count() && i <= 8; ++i) {
            args.push_back(c.at(i));
        }
        return done(c, t->callback(blockName(newest_, c, 0), args) ? 1 : 0);
    });
    m.service(2220, [this](Call& c) {
        xq::Thread* t = loopThreads[1];
        if (!t || !newest_) {
            return done(c, 0);
        }
        const std::string name = blockName(newest_, c, 0);
        if (name.empty()) {
            t->stop();
            return done(c, 0);
        }
        return done(c, t->start(*newest_, name) ? 1 : 0);
    });
    // 2221 (0x12cda4 -> 0x2cdc08): as 2220 with up to eight arguments for
    // the block (the shop's CallSeCount).
    m.service(2221, [this](Call& c) {
        xq::Thread* t = loopThreads[1];
        if (!t || !newest_ || c.count() == 0) {
            return done(c, 0);
        }
        const std::string name = blockName(newest_, c, 0);
        if (name.empty()) {
            t->stop();
            return done(c, 0);
        }
        std::vector<Value> args;
        for (std::size_t i = 1; i < c.count() && i <= 8; ++i) {
            args.push_back(c.at(i));
        }
        return done(c, t->start(*newest_, name, {}, args) ? 1 : 0);
    });
    // 1000 (0x12f6a8 -> 0x352ad4): a tap (a new touch) when the loop takes
    // input.
    m.service(1000, [this](Call& c) { return done(c, pressed() ? 1 : 0); });
    // 1010 (0x145788): always 0 (a disabled debug skip). 2120 (0x133f98):
    // the scene's frame step (+0x10b4: display frames per game frame,
    // 0x378900; the port keeps no slow frames, so the ratio). 2302..2304 (0x145490...): files still
    // loading.
    m.service(1010, [](Call& c) { return done(c, 0); });
    m.service(2120, [this](Call& c) {
        c.result = Value::ofFloat(float(std::max(1, displayFramesPerFrame)));
        return true;
    });
    for (int op : {2302, 2303, 2304}) {
        m.service(op, [](Call& c) { return done(c, 0); });
    }
    // 2511 (0x1296c0 -> 0x2cfbb0): every screen's brightness at once.
    m.service(2511, [this](Call& c) {
        if (c.count() == 0) {
            return done(c, 0);
        }
        for (auto& b : scene.brightness) {
            b.set({c.real(0)});
        }
        return done(c);
    });
    // 2512 / 2519: both screens' brightness over frames / still changing.
    m.service(2512, [this](Call& c) {
        for (auto& b : scene.brightness) {
            b.start({c.real(0)}, c.real(1), 0);
        }
        return done(c);
    });
    m.service(2519, [this](Call& c) {
        return done(c, scene.brightness[0].moving() || scene.brightness[1].moving());
    });
    // 2110 (0x12cb68): a command to the puzzle's play loop (its +0x4c
    // handler): the first operand is the command, the rest its operands.
    // The standard loop has none.
    m.service(2110, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        if (special) {
            return special->command(c.integer(0), c, 1);
        }
        return loops && loops->loopCommand(c.integer(0), c, 1);
    });
    // The loop requests (the game's loop manager, requests at +0x1d3c,
    // carried out at the next frame). 2104 (0x1425b0 -> 0x2c592c): a loop
    // by number on top [value 0, value 1]; 2102 (0x122694 -> 0x2d4c98): the
    // top loop ends; 2103 (0x1226bc -> 0x2d4c04): it starts again; 2100
    // (0x134168 -> 0x2cb770): requests are waiting.
    m.service(2104, [this](Call& c) {
        if (c.count() == 0 || !loops) {
            return done(c, 0);
        }
        std::vector<Value> values;
        for (std::size_t i = 1; i < 3; ++i) {
            values.push_back(i < c.count() ? Value::ofInt(c.integer(i)) : Value::ofInt(-1));
        }
        return done(c, loops->push(c.integer(0), values) ? 1 : 0);
    });
    m.service(2102, [this](Call& c) {
        if (loops) {
            loops->pop();
        }
        return done(c, 1);
    });
    m.service(2103, [this](Call& c) {
        if (loops) {
            loops->restart();
        }
        return done(c, 1);
    });
    m.service(2100, [this](Call& c) { return done(c, loops && loops->pending() ? 1 : 0); });
    // 10000 (0x15be70): an event by name or number on top [branch]: loop
    // 1200 {number, branch}.
    m.service(10000, [this](Call& c) {
        if (c.count() == 0 || !loops) {
            return done(c, 0);
        }
        return done(c, loops->requestEvent(c.at(0), c.count() > 1 ? c.integer(1) : -1) ? 1 : 0);
    });
    // 10001 (0x15bf20): the place loop 1300 {mode [, event by name or
    // number]}.
    m.service(10001, [this](Call& c) {
        if (c.count() == 0 || !loops) {
            return done(c, 0);
        }
        int event = -1;
        if (c.count() > 1) {
            const Value& v = c.at(1);
            const EventRecord* e = v.isText() ? story.event(v.text) : story.event(v.toInt());
            event = e ? e->number : -1;
        }
        return done(c,
                    loops->push(1300, {Value::ofInt(c.integer(0)), Value::ofInt(event)}) ? 1 : 0);
    });
    // 10002 (0x158530): a puzzle by name or id: loop 3000 {id}.
    m.service(10002, [this](Call& c) {
        if (c.count() == 0 || !loops) {
            return done(c, 0);
        }
        const Value& v = c.at(0);
        const PuzzleRecord* p = v.isText() ? defs.puzzleByName(v.text) : defs.puzzle(v.toInt());
        if (!p) {
            return done(c, 0);
        }
        return done(c, loops->push(3000, {Value::ofInt(p->id)}) ? 1 : 0);
    });
    // 2109 (0x134190): the loop's input switch (byte +0x1a); while it is
    // set the frame drops the touch event before the objects see it
    // (0x37889c -> 0x265360): the intro, messages and the verdict lock
    // the puzzle's objects so.
    m.service(2109, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        objectTouchLocked = c.at(0).truthy();
        return done(c, 1);
    });
    // 2190 / 2191 (0x123ac8 / 0x123a98: the game's vtable +0xd0 with 1,
    // +0xd4): open / close a scope (Scene::openScope). The intro, the
    // question text and the verdict run in their own; switching every
    // object's touch off inside one (3011 0) lasts until it closes.
    m.service(2190, [this](Call& c) {
        openScope(true);
        return done(c, 1);
    });
    m.service(2191, [this](Call& c) {
        closeScope();
        return done(c, 1);
    });
    // 2920 (0x1265e8 -> 0x135b90): byte 0 of the settings at 0x411d78
    // (the German reference reads 2; hypothesis: the region, 0 Japan);
    // scripts lay out by it.
    m.service(2920, [](Call& c) { return done(c, 2); });
    // 2930 (0x12cafc -> 0x2cdd08): byte 1 of the settings at 0x411d78, the
    // language (hypothesis: 0 Japanese; the German reference reads 4, byte
    // 0 is 2). Scripts lay out titles by it (the intro's digits move 31
    // pixels left when it is not 0); 0x2d4f24 sizes the puzzle record
    // table by it (165 for 0, else 150).
    m.service(2930, [this](Call& c) { return done(c, language); });
    // 3010 / 3011 (0x1348e8 -> 0x32b16c / 0x134960 -> 0x2ca8ec): every
    // object of a screen (or of all screens: no screen or one past 4)
    // shown / hidden (vtable +0x0c, as 5005) or touch switched (vtable
    // +0x10, as 5006), 2D and 3D alike. The verdict's NazoJudgeEvent hides
    // all ("3010 0") before it builds its own; JudgeMain switches touch
    // off ("3011 0"). Not modelled: the same call's per-screen layer
    // switch (0x26741c / 0x267404 on the five screen records) and the
    // +0x41e8 list's vtable +0x30 / +0x34.
    auto everyObject = [this](Call& c, bool touch) {
        if (c.count() == 0) {
            return false;
        }
        const unsigned screen = c.count() > 1 ? unsigned(c.integer(1)) : ~0u;
        setEveryObject(touch, c.at(0).truthy(), screen);
        // 3010 also switches the views (sixth adventure 0x359e8c: vtable
        // +0x30, as 4005).
        for (int s = 0; !touch && s < adventure56::Scene::kScreens; ++s) {
            if (screen >= 5 || unsigned(s) == screen) {
                scene.cameras[std::size_t(s)].viewShown = c.at(0).truthy();
            }
        }
        return done(c, 1);
    };
    // 3041 (0x123520): the handwriting recogniser (width, height,
    // category [, score limit]).
    m.service(3041, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const int limit = c.count() > 3 ? c.integer(3) : 0;
        return done(c, setupRecogniser(c.integer(0), c.integer(1), c.integer(2) & 0xff, limit) ? 1
                                                                                               : 0);
    });
    m.service(3010, [everyObject](Call& c) { return everyObject(c, false); });
    m.service(3011, [everyObject](Call& c) { return everyObject(c, true); });
    // 3012 / 3013 (sixth adventure 0x182f98 -> 0x35a298 / 0x182f28 ->
    // 0x35a0fc; switch [, screen]): every 2D and 3D object of a screen (all
    // for none or one past 4) gets 5007's switch (vtable +0x14) / 5008's
    // drawn switch (+0x18), as 3010 / 3011 do for shown / touch. 3012 also
    // switches the screens' views (+0x2e4, as 4007) and with all screens a
    // global bit (+0x2694 bit 0; not modelled).
    for (const bool drawnSwitch : {false, true}) {
        m.service(drawnSwitch ? 3013 : 3012, [this, drawnSwitch](Call& c) {
            if (c.count() < 1) {
                return false;
            }
            const bool on = c.at(0).truthy();
            const unsigned screen = c.count() > 1 ? unsigned(c.integer(1)) : ~0u;
            for (const bool solid : {false, true}) {
                for (const auto& [hash, o] : scene.objects(solid)) {
                    if (screen < 5 && unsigned(o->screen) != screen) {
                        continue;
                    }
                    (drawnSwitch ? o->drawn : o->switch14) = on;
                }
            }
            if (!drawnSwitch) {
                for (int s = 0; s < adventure56::Scene::kScreens; ++s) {
                    if (screen >= 5 || unsigned(s) == screen) {
                        scene.cameras[std::size_t(s)].viewActive = on;
                    }
                }
            }
            return done(c, 1);
        });
    }
    // 3542 (0x14525c -> 0x2c54d8): the size of an animation set's cell
    // (section 200, binary-searched by name hash in the set's cell table
    // +0x138, comparator 0x36e258; its box, max minus min) into two
    // registers; 0 and result 0 when there is none. The title sizes its
    // buttons' touch areas by it (its names are cells and composites
    // alike); the shop's "b3" is a cell only.
    m.service(3542, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        Resource* r = scene.resource(reference(c, 0));
        const adventure56::AnimationSet::Cell* found = nullptr;
        if (r && r->animation) {
            const std::uint32_t h = reference(c, 1);
            for (const auto& cell : r->animation->cells) {
                if (cell.hash == h) {
                    found = &cell;
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < 2; ++i) {
            if (Value* reg = c.reg(2 + i)) {
                *reg = Value::ofFloat(found ? found->box[3 + i] - found->box[i] : 0.0f);
            }
        }
        return done(c, found ? 1 : 0);
    });
    // Boot checks (the game object's bytes +0x26c / +0x26d, set by the
    // boot's data checks; *hypothesis*: both 0 on a normal start - the
    // reference boots to the title without the reset page): 2729
    // (0x14e73c) +0x26d; 2728 (0x13fe9c) a pending check or +0x26c; 2722
    // (0x12af48 -> 0x336bf8) a check of the extra data, 0 when neither is
    // set.
    m.service(2729, [](Call& c) { return done(c, 0); });
    m.service(2728, [](Call& c) { return done(c, 0); });
    m.service(2722, [](Call& c) { return done(c, 0); });
    // 2600 (0x12af70): the game's vtable +0xc0 (*not identified*; the
    // title calls it once before its buttons), result 1.
    m.service(2600, [](Call& c) { return done(c, 1); });
    // Master volume (the sound manager's settings record, 0x34853c): 11016
    // (0x1649f0) reads it (-1 without sound), 11017 (0x16580c -> 0x16b1cc)
    // sets it, 11019 (0x165228) stores it into "ゲーム音量".
    m.service(11016, [this](Call& c) { return done(c, masterVolume); });
    m.service(11017, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        masterVolume = c.integer(0);
        return done(c);
    });
    m.service(11019, [](Call& c) { return done(c); });
    // Save memory (the save object at game +0x67f8; system/a5_saves.hpp).
    // 16000 (0x145bb8 -> 0x185bf8): the header is read. 16010 (0x1585ac):
    // a slot record's field (slot 0..2: records of 0x60 bytes at +0x6e90;
    // field 0 the name, 1 "in use", 2.. the stored values, a5_saves.hpp),
    // or with field 32 the saved master volume. 10101 (0x145c58 ->
    // 0x2c41d8: a background job, 0x332710) saves the game into a slot
    // {slot, ..}; 10100 (0x145c10 -> 0x332df4) loads one {slot}; a failure
    // sets game +0x197e0 bit 0, which 16001 (0x152384) reads.
    m.service(16000, [](Call& c) { return done(c, 1); });
    m.service(16010, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int field = c.integer(1);
        if (field == 32) {
            return done(c, masterVolume);
        }
        if (field > 31) {
            return done(c, 1);
        }
        const int slot = c.integer(0);
        if (slot < 0 || slot >= SaveSlots::kCount) {
            return false;
        }
        const SaveRecord* r = saveSlots().slot(slot);
        if (field == 0) {
            c.result = Value::ofText(r ? r->header.name : std::string());
            return true;
        }
        return done(c, r ? headerField(r->header, field) : 0);
    });
    m.service(10101, [this](Call& c) {
        if (c.count() < 2) {
            return done(c, 0);
        }
        saveFailed = !saveSlots().write(c.integer(0), saveRecord(*this));
        return done(c, 1);
    });
    m.service(10100, [this](Call& c) {
        if (c.count() < 1) {
            return done(c, 0);
        }
        const SaveRecord* r = saveSlots().slot(c.integer(0));
        saveFailed = r == nullptr;
        if (r) {
            restore(*this, *r);
        }
        return done(c, 1);
    });
    m.service(16001, [this](Call& c) { return done(c, saveFailed ? 1 : 0); });
    // 3422 / 3423 (0x134038 / 0x133fe0 -> 0x2cdd18): a picture resource's
    // width / height (+0x428 / +0x42a).
    for (const int op : {3422, 3423}) {
        m.service(op, [this, op](Call& c) {
            Resource* r = c.count() ? scene.resource(reference(c, 0)) : nullptr;
            if (!r || !r->picture) {
                return false;
            }
            return done(c, op == 3422 ? r->picture->width : r->picture->height);
        });
    }
    // 3407 (0x12cab0): whether a picture resource exists.
    m.service(3407, [this](Call& c) {
        Resource* r = scene.resource(reference(c, 0));
        return done(c, r && r->picture ? 1 : 0);
    });
    // 16540 (0x164870 -> 0x33f560): one of the current puzzle's six
    // parameter values (index 0..5; nothing past them).
    m.service(16540, [this](Call& c) {
        const int i = c.integer(0);
        c.result = i >= 0 && i < int(puzzleValues.size()) ? puzzleValues[std::size_t(i)] : Value{};
        return true;
    });
    // 16542 (0x165028): the current puzzle's title.
    m.service(16542, [this](Call& c) {
        c.result = Value::ofText(puzzle ? puzzle->title : std::string());
        return true;
    });
    // Sound (11xxx; the game's sound manager, vtable of the game object
    // +0x10c..+0x114). Names are built as "%s_%03d" with the players'
    // prefixes (0x411ed0: BGM EVT JNG EVS MOV SUR) or the effect set.
    // Play (0x2bc7a0 / 0x2bc84c -> game vtable +0x114 -> 0x201d9c): stream
    // `number` on a script player [fade, auto-start]; number 0 stops the
    // player (+0x110, over `fade`), below 0 leaves it alone. Names are
    // "<prefix>_%03d" by the script player (0x411ed0).
    auto stream = [this](int player, int number, int fade, bool autoStart) {
        return playStream(player, number, fade, autoStart);
    };
    // 11000 (0x14bc38 -> 0x2c158c): a sound scene (lt5_def 2101 by id
    // [, fade, ...]): its music on player 0, its ambience on player 5, its
    // effect set.
    m.service(11000, [this](Call& c) {
        // [fade (60), auto-start (1)]
        const int fade = c.count() > 1 ? c.integer(1) : 60;
        const bool autoStart = c.count() > 2 ? c.at(2).truthy() : true;
        return done(c, selectSoundScene(c.integer(0), fade, autoStart) ? 1 : 0);
    });
    // 11002 / 11003 / 11004 (0x15c078 / 0x15c0d0 / 0x1587b0): a scene's
    // music, ambience, effect set.
    auto sceneField = [this](Call& c, int Definitions::SoundScene::* field) {
        const auto it = defs.soundScenes.find(c.integer(0));
        return it == defs.soundScenes.end() ? false : done(c, it->second.*field);
    };
    m.service(11002,
              [sceneField](Call& c) { return sceneField(c, &Definitions::SoundScene::music); });
    m.service(11003,
              [sceneField](Call& c) { return sceneField(c, &Definitions::SoundScene::ambience); });
    m.service(11004,
              [sceneField](Call& c) { return sceneField(c, &Definitions::SoundScene::effects); });
    // 11023 (0x15ec70 -> 0x2bcefc): a sound group ("SY" / "SI" / ... by
    // kind, number); kind 1 makes it the effect set.
    m.service(11023, [this](Call& c) {
        if (c.count() >= 2 && c.integer(0) == 1) {
            effectSet = c.integer(1);
        }
        return done(c);
    });
    // 11024 (0x1523a8 -> 0x2bf9fc): an effect by number; 11025 (0x1523f0 ->
    // 0x2bf988) stops it [over frames]; 11021 (0x163414): with a number,
    // whether anything sounds on that number's effect player (0x33b570 ->
    // the player's handles, 0x1bfe50), without, on any (0x33b7f8); 11026
    // (0x160e54) stops all [over frames]. Game::activeEffects keeps the
    // timing; the audio follows.
    m.service(11024, [this](Call& c) { return done(c, playEffect(c.integer(0)) ? 1 : 0); });
    m.service(11025, [this](Call& c) {
        stopEffect(c.integer(0), c.count() > 1 ? c.integer(1) : 0);
        return done(c);
    });
    m.service(11021, [this](Call& c) {
        return done(c, effectsSounding(c.count() > 0 ? c.integer(0) : -1) ? 1 : 0);
    });
    m.service(11026, [this](Call& c) {
        const int frames = c.count() > 0 ? c.integer(0) : 0;
        for (auto& e : activeEffects) {
            e.remaining = std::min(e.remaining, double(frames) * adventure56::SoundPlayer::kRate /
                                                    adventure56::StreamPlayers::kFrameRate);
        }
        if (sound) {
            sound->stopEffects(frames);
        }
        return done(c);
    });
    // 11027 (0x16269c -> 0x2bb95c -> 0x2017c4): every sound of one effect
    // player (the list 11021 asks, sound system +0x6b4; method +0x30) stops
    // [over frames]: player, frames. The verdict fades the scene's effects
    // so (prog_nazo Main: 11027 1 30, then 11021 waits).
    m.service(11027, [this](Call& c) {
        const int player = c.integer(0);
        const int frames = c.count() > 1 ? c.integer(1) : 0;
        std::vector<std::string> names;
        for (const auto& e : activeEffects) {
            if (e.player == player) {
                names.push_back(e.name);
            }
        }
        for (const auto& name : names) {
            const double samples = double(frames) * adventure56::SoundPlayer::kRate /
                                   adventure56::StreamPlayers::kFrameRate;
            for (auto& e : activeEffects) {
                if (e.name == name) {
                    e.remaining = std::min(e.remaining, samples);
                }
            }
            if (sound) {
                sound->stopEffect(name, frames);
            }
        }
        return done(c, 1);
    });
    // Streams (a56_stream_players.hpp has the model): 11046 music
    // (0x160eec), 11045 jingle (0x161220), 11047 movie sound (0x161344),
    // 11051 voice (0x161004): number [, auto-start [, fade]]. Without
    // auto-start the stream is only prepared; 11049 starts it.
    auto playService = [stream](int player) {
        return [stream, player](Call& c) {
            const bool autoStart = c.count() > 1 && c.at(1).truthy();
            const int fade = c.count() > 2 ? c.integer(2) : 0;
            return done(c, stream(player, c.integer(0), fade, autoStart) ? 1 : 0);
        };
    };
    m.service(11046, playService(0));
    m.service(11045, playService(2));
    m.service(11047, playService(4));
    m.service(11051, playService(3));
    // 11048 (0x1610a8 -> 0x2bc724): an event voice on player 1, named
    // "EVT_%02d_%06d_%04d" from the current event and the number
    // [, auto-start [, fade]].
    m.service(11048, [this](Call& c) {
        const bool autoStart = c.count() > 1 && c.at(1).truthy();
        const int fade = c.count() > 2 ? c.integer(2) : 0;
        return done(c, c.count() && playEventVoice(c.integer(0), fade, autoStart) ? 1 : 0);
    });
    // The players by script number, or every player (0xffff).
    auto eachPlayer = [](Call& c, int at, auto&& fn) {
        const int p = c.count() > std::size_t(at) ? c.integer(at) : 0xffff;
        if (p == 0xffff) {
            for (const int sp : {0, 5, 2}) { // one script number per internal player
                fn(sp);
            }
        } else {
            fn(p);
        }
    };
    // 11049 (0x152560 -> 0x32f604, command 1 0x1c0088): start what is
    // prepared or resume a pause [, fade].
    m.service(11049, [this, eachPlayer](Call& c) {
        const int fade = c.count() > 1 ? c.integer(1) : 0;
        eachPlayer(c, 0, [&](int p) {
            const int internal = adventure56::StreamPlayers::internal(p);
            if (internal < 0) {
                return;
            }
            const bool resuming = streams.paused(p);
            streams.start(p, fade);
            if (sound) {
                if (!preparedStream[internal].empty()) {
                    sound->playStream(internal, preparedStream[internal], fade);
                } else if (resuming) {
                    sound->resumeStream(internal, fade);
                }
            }
            preparedStream[internal].clear();
        });
        return done(c, 1);
    });
    // 11053 (0x16366c -> 0x32f534 -> 0x265664 -> 0x201d10, command 3):
    // a player's volume [0xffff: every player] to a value over frames
    // (MovieSkip fades the movie's sound so). *Hypothesis*: a linear move.
    m.service(11053, [this, eachPlayer](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const float to = c.real(1);
        const int frames = c.count() > 2 ? c.integer(2) : 0;
        eachPlayer(c, 0, [&](int p) {
            const int internal = adventure56::StreamPlayers::internal(p);
            if (internal < 0 || internal >= int(streamVolume.size())) {
                return;
            }
            volumeMoves[internal] = {streamVolume[std::size_t(internal)], to, frames, 0};
        });
        return done(c, 1);
    });
    // 11050 (0x152604 -> 0x2bf914, command 2 0x1c014c): pause over frames.
    m.service(11050, [this, eachPlayer](Call& c) {
        const int frames = c.count() > 1 ? c.integer(1) : 0;
        eachPlayer(c, 0, [&](int p) {
            streams.pause(p, frames);
            if (sound && adventure56::StreamPlayers::internal(p) >= 0) {
                sound->pauseStream(adventure56::StreamPlayers::internal(p), frames);
            }
        });
        return done(c, 1);
    });
    // 11044 (0x158898, +0x110) stop [over frames]; 11043 (0x15893c,
    // +0x10c) stop at once (both command 7, 0x1c01a4).
    auto stopService = [this, eachPlayer](Call& c, int frames) {
        eachPlayer(c, 0, [&](int p) {
            const int internal = adventure56::StreamPlayers::internal(p);
            streams.stop(p, frames);
            if (internal >= 0) {
                preparedStream[internal].clear();
                if (sound) {
                    sound->stopStream(internal, frames);
                }
            }
        });
        return done(c, 1);
    };
    m.service(11044,
              [stopService](Call& c) { return stopService(c, c.count() > 1 ? c.integer(1) : 0); });
    m.service(11043, [stopService](Call& c) { return stopService(c, 0); });
    // Queries (the game's vtable +0xf8 / +0xfc / +0x100 / +0x104 / +0x108
    // -> the player's 0x345f88 / 0x345fc4 / 0x345e60 / 0x345ed0 / 0x345f54;
    // with no player, any player): 11041 preparing, 11042 playing (started
    // and not paused - a pause's fade still counts), 11056 / 11058 a fade
    // running, 11057 a stop's fade running.
    auto query = [this, eachPlayer](bool (adventure56::StreamPlayers::*q)(int) const) {
        return [this, eachPlayer, q](Call& c) {
            bool any = false;
            eachPlayer(c, 0, [&](int p) { any = any || (streams.*q)(p); });
            return done(c, any ? 1 : 0);
        };
    };
    m.service(11041, query(&adventure56::StreamPlayers::preparing));
    m.service(11042, query(&adventure56::StreamPlayers::playing));
    m.service(11056, query(&adventure56::StreamPlayers::fading));
    m.service(11058, query(&adventure56::StreamPlayers::fading));
    m.service(11057, query(&adventure56::StreamPlayers::stopFading));
    // 16541 (0x165004): the current puzzle's type.
    m.service(16541, [this](Call& c) { return done(c, puzzle ? puzzle->type : 0); });
    // 15703 (0x160690): a puzzle's picarats after 0, 1, 2+ wrong answers.
    m.service(15703, [this](Call& c) {
        const PuzzleRecord* p = defs.puzzle(c.integer(0));
        if (!p) {
            return false;
        }
        const int i = std::clamp(c.integer(1), 0, 2);
        return done(c, p->picarats[i]);
    });
}

// 5430 / 6430 (0x12dd08 / 0x12eab0 -> 0x36e5d8): the object shows a
// picture resource [with its pivot]; a number instead of a name shows an
// offscreen screen's image (the verdict: "5430 obj 4" shows screen 4;
// hypothesis from its use). 6430 puts a picture on a 3D object (puzzle
// 044's card on the top screen).
bool Game::showPicture(xq::Call& c, adventure56::Object* o) {
    if (o && c.count() > 1 && c.at(1).kind == Value::Kind::Int) {
        o->screenPicture = c.integer(1);
        o->picture.reset();
        o->content = 3;
        return done(c);
    }
    adventure56::Resource* r = c.count() > 1 ? scene.resource(reference(c, 1)) : nullptr;
    if (!o || !r || !r->picture) {
        return done(c, 0);
    }
    o->picture = r->picture;
    o->pictureCellList = false;
    o->pictureBlend = r->blend;
    o->content = 3;
    if (c.count() >= 4) {
        o->pivot[0] = c.real(2);
        o->pivot[1] = c.real(3);
    }
    return done(c);
}

// Play (0x2bc7a0 / 0x2bc84c -> game vtable +0x114 -> 0x201d9c): stream
// `number` on a script player [fade, auto-start]; number 0 stops the player
// (+0x110, over `fade`), below 0 leaves it alone. Names are
// "<prefix>_%03d" by the script player (0x411ed0).
bool Game::playStream(int player, int number, int fade, bool autoStart) {
    static const char* kPrefix[6] = {"BGM", "EVT", "JNG", "EVS", "MOV", "SUR"};
    const int internal = adventure56::StreamPlayers::internal(player);
    if (internal < 0 || number < 0) {
        return false;
    }
    if (number == 0) {
        streams.stop(player, fade);
        preparedStream[internal].clear();
        if (sound) {
            sound->stopStream(internal, fade);
        }
        return true;
    }
    char name[32];
    std::snprintf(name, sizeof name, "%s_%03d", kPrefix[player], number);
    return playNamedStream(player, name, fade, autoStart);
}

// 0x2bc724: a voice line of the current event on player 1,
// "EVT_%02d_%06d_%04d" (the event's group and number, the line).
bool Game::playEventVoice(int line, int fade, bool autoStart) {
    if (!currentEvent || line <= 0) {
        return false;
    }
    char name[40];
    std::snprintf(name, sizeof name, "EVT_%02d_%06d_%04d", currentEvent->group,
                  currentEvent->number, line);
    const bool ok = playNamedStream(1, name, fade, autoStart);
    if (!ok && log) {
        log(std::string("voice ") + name + " not found");
    }
    return ok;
}

bool Game::playNamedStream(int player, const std::string& name, int fade, bool autoStart) {
    const int internal = adventure56::StreamPlayers::internal(player);
    if (internal < 0) {
        return false;
    }
    const adventure56::StreamInfo* info = streamInfo(name);
    if (!info || !streams.play(player, name, *info, fade, autoStart)) {
        return false;
    }
    preparedStream[internal] = name;
    if (sound) {
        sound->stopStream(internal, fade);
        if (autoStart) {
            sound->playStream(internal, name, 0);
            preparedStream[internal].clear();
        }
    }
    return true;
}

// 0x2c158c: a sound scene (lt5_def 2101 by id): its music on player 0, its
// ambience on player 5, its effect set.
bool Game::selectSoundScene(int id, int fade, bool autoStart) {
    const auto it = defs.soundScenes.find(id);
    if (it == defs.soundScenes.end()) {
        return false;
    }
    playStream(0, it->second.music, fade, autoStart);
    playStream(5, it->second.ambience, fade, autoStart);
    effectSet = it->second.effects;
    return true;
}

} // namespace picarat::adventure5
