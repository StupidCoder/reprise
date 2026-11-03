// Headless handoff probe extracted from Beacon's heroTest (source-handoff.json).
// Uses the supplied level files; does not open a window or install game data.
#include "data/GameFiles.hpp"
#include "data/ObjectDefinition.hpp"
#include "game/LevelSchema.hpp"
#include "game/LevelDocument.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelRuntime.hpp"
#include "game/Hero.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef REPRISE_BEACON_CORE
using namespace beacon;
#endif
struct Context {
    std::filesystem::path root;
    data::GameFiles files;
    data::ObjectDefinitionLibrary defs;
    game::LevelSchemaLibrary schemas;

    bool open(const char* dir) {
        root = dir;
        std::string error;
        if (!files.open(root, &error) || !defs.open(root, &error) || !schemas.open(root, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return false;
        }
        return true;
    }
};

void hideHiddenCollision(const game::Level& level, const std::filesystem::path& root,
                         const game::LevelCollisionStats& stats, physics::PhysicsWorld& world) {
    game::LevelRuntime runtime;
    runtime.load(level, root);
    std::map<int, std::pair<std::uint32_t, std::uint32_t>> saved;
    game::syncHiddenCollision(runtime, stats, world, saved);
}

int heroTest(const char* dir, const char* levelName) {
    Context ctx;
    if (!ctx.open(dir)) {
        return 1;
    }
    std::string error;
    auto level = game::loadLevel(ctx.root, levelName, &error);
    auto tuning = game::loadHeroTuning(ctx.root, &error);
    if (!level || !tuning) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    game::PhysicsLibrary physics(ctx.files);
    physics::CollisionWorld triangles;
    physics::PhysicsWorld world;
    const auto hiddenStats = game::buildLevelCollision(*level, ctx.root, ctx.defs, physics, triangles, nullptr, &world);
    hideHiddenCollision(*level, ctx.root, hiddenStats, world);
    const game::LevelObject* start = nullptr;
    for (const auto& o : level->objects) {
        if (o.className == "hero_locator") {
            start = &o;
            break;
        }
    }
    if (!start) {
        std::fprintf(stderr, "no hero_locator\n");
        return 1;
    }
    const auto& t = start->transform;
    game::Hero hero(*tuning, game::loadHeroClipLengths(ctx.defs, ctx.files));
    hero.spawn({t.translate[0], t.translate[1] + 0.05f, t.translate[2]}, t.rotate[1] * 3.14159265f / 180);
    // Research aid: BEACON_SLIDE_STEPS=FILE with lines "k x y z dx dy dz": one collide-and-slide each,
    // printing the end (compare with the original's next position).
    if (const char* steps = std::getenv("BEACON_SLIDE_STEPS")) {
        if (std::FILE* f = std::fopen(steps, "r")) {
            int k;
            float v[6];
            while (std::fscanf(f, "%d %f %f %f %f %f %f", &k, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 7) {
                const math::Vec3 e = hero.collideAndSlide(world, {v[0], v[1], v[2]}, {v[3], v[4], v[5]});
                std::printf("%d %.5f %.5f %.5f\n", k, e.x, e.y, e.z);
            }
            std::fclose(f);
        }
        return 0;
    }
    // Research aid: BEACON_HERO_AT=x,y,z,yawdeg drops the hero there and prints 240 updates
    // without input (frame, state, feet, velocity).
    if (const char* at = std::getenv("BEACON_HERO_AT")) {
        float x = 0, y = 0, z = 0, yaw = 0;
        std::sscanf(at, "%f,%f,%f,%f", &x, &y, &z, &yaw);
        hero.spawn({x, y, z}, yaw * 3.14159265f / 180);
        for (int i = 0; i < 240; ++i) {
            hero.update({}, world);
            const auto& p = hero.position();
            const auto& v = hero.velocity();
            std::printf("%3d %-6s %s pos %.4f %.4f %.4f vel %.3f %.3f %.3f yaw %.1f\n", i, game::heroStateName(hero.state()),
                        hero.slideClip(), p.x, p.y, p.z, v.x, v.y, v.z, hero.yaw() * 180 / 3.14159265f);
        }
        return 0;
    }
    auto run = [&](const char* label, int steps, game::HeroInput in, bool pressFirst) {
        float y0 = hero.position().y, top = y0;
        game::HeroState last = hero.state();
        std::string trace;
        for (int i = 0; i < steps; ++i) {
            game::HeroInput step = in;
            step.jumpPressed = pressFirst && i == 0;
            hero.update(step, world);
            top = std::max(top, hero.position().y);
            if (hero.state() != last) {
                trace += std::string(" ") + game::heroStateName(hero.state()) + "@" + std::to_string(i);
                last = hero.state();
            }
        }
        const auto& p = hero.position();
        std::printf("%-22s pos (%.3f %.3f %.3f) speed %.3f apex +%.3f grounded %d states:%s\n", label, p.x, p.y, p.z,
                    hero.horizontalSpeed(), top - y0, hero.grounded(), trace.c_str());
    };
    run("settle", 60, {}, false);
    run("tap jump", 90, {0, 0, false, false}, true);
    run("held jump", 90, {0, 0, false, true}, true);
    game::HeroInput fwd;
    fwd.stickX = std::sin(hero.yaw());
    fwd.stickZ = std::cos(hero.yaw());
    run("walk forward 1 s", 60, fwd, false);
    run("release 0.5 s", 30, {}, false);
    game::HeroInput hover;
    hover.jumpHeld = true;
    run("jump", 12, hover, true);
    run("hover (press, hold)", 120, hover, true);
    run("after hover", 60, {}, false);
    return 0;
}


int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: beacon_hero_probe GAME_DIR LEVEL\n");
        return 2;
    }
    return heroTest(argv[1], argv[2]);
}
