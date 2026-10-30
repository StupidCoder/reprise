// The puzzle adventures through Reprise's interfaces (games/layton/reprise).
//
// Always: the adaptive two-screen layout (DS and 3DS geometry, both arrangements, the switch
// between them as the frame's shape changes), the stylus mapping through the shown layout and
// the zoom, presses outside the lower screen, a layout change and a pinch during a press, the
// filter regions, and the importers' rejection of other files.
//
// With LAYTON_PACKS naming a folder of imported packs (the standalone Picarat app's data folder,
// whose pack folders are yltd-v11, a5fp-v3, ...): each playable game starts from its title and
// runs with sound; with LAYTON_CHECKPOINTS naming a folder holding Picarat checkpoints
// (checkpoint.l2cp, a5fp-checkpoint.sav) the quick save is checked through Reprise's persistent
// save wrapper: continue, save, a fresh load, a damaged save and another game's save. Nothing is
// written outside a temporary data folder.
#include "app/catalog.hpp"
#include "app/quick_save.hpp"
#include "layton/reprise/dual_screen.hpp"
#include "layton/reprise/entry.hpp"
#include "layton/reprise/saved_checkpoint.hpp"
#include "layton_embed.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include "platform/sha256.hpp"
#include "state/archive.hpp"
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

namespace {
namespace fs = std::filesystem;
namespace embed = picarat::embed;
using layton_reprise::DualScreen;
using layton_reprise::StylusRouter;
using Kind = platform::PointerEvent::Kind;

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

bool close(double a, double b) { return std::abs(a - b) < 1e-6; }

void layouts() {
    const auto ds = embed::screens(embed::Title::PandorasBox);
    const auto ctr = embed::screens(embed::Title::MiracleMask);
    check(ds.top.width == 256 && ds.top.height == 192 && ds.bottom.width == 256 && ds.bottom.height == 192,
          "DS: two 256x192 screens");
    check(ctr.top.width == 400 && ctr.top.height == 240 && ctr.bottom.width == 320 && ctr.bottom.height == 240,
          "3DS: 400x240 upper and 320x240 lower screen");
    for (const auto& s : {ds, ctr}) {
        for (const auto& [w, h] : {std::pair{1920, 1080}, std::pair{1080, 1920}, std::pair{2560, 1080},
                                   std::pair{900, 900}, std::pair{400, 1600}}) {
            DualScreen screen(s);
            screen.show(w, h);
            const auto& p = screen.shown();
            check(close(p.top.width / p.top.height, double(s.top.width) / s.top.height) &&
                      close(p.bottom.width / p.bottom.height, double(s.bottom.width) / s.bottom.height),
                  "Each screen keeps its shape");
            check(close(p.top.width / s.top.width, p.bottom.width / s.bottom.width), "Both screens at one scale");
            check(p.top.x >= -1e-6 && p.top.y >= -1e-6 && p.top.x + p.top.width <= w + 1e-6 &&
                      p.bottom.y + p.bottom.height <= h + 1e-6 && p.bottom.x + p.bottom.width <= w + 1e-6,
                  "The screens fit the frame");
            if (p.side_by_side) {
                check(p.top.x + p.top.width <= p.bottom.x + 1e-6, "Side by side: the upper screen at the left");
            } else {
                check(p.top.y + p.top.height <= p.bottom.y + 1e-6, "Stacked: the upper screen on top");
            }
            // The arrangement chosen is the one with the larger scale.
            const double stacked = std::min(double(w) / std::max(s.top.width, s.bottom.width),
                                            double(h) / (s.top.height + s.bottom.height));
            const double side = std::min(double(w) / (s.top.width + s.bottom.width),
                                         double(h) / std::max(s.top.height, s.bottom.height));
            check(p.side_by_side == (side > stacked), "The larger arrangement is chosen");
            check(close(p.top.width / s.top.width, std::max(side, stacked)), "At the larger scale");
        }
        // Across the switch: a landscape frame shows them side by side, a portrait one stacked,
        // at once (a rotation).
        DualScreen screen(s);
        screen.show(2400, 1000);
        const auto before = screen.generation();
        check(screen.shown().side_by_side, "Wide frame: side by side");
        screen.show(1000, 2400);
        check(!screen.shown().side_by_side && screen.generation() != before, "Rotated: stacked, a new layout");
    }
}

// A 3DS frame 1000 x 1200 (stacked): where a lower-screen pixel shows.
std::array<float, 2> at_lower(const DualScreen& screen, int x, int y) {
    const auto& b = screen.shown().bottom;
    const auto s = screen.screens();
    return {float(b.x + (x + 0.5) * b.width / s.bottom.width), float(b.y + (y + 0.5) * b.height / s.bottom.height)};
}

void stylus() {
    const auto ctr = embed::screens(embed::Title::MiracleMask);
    for (const auto& [w, h] : {std::pair{1000, 1200}, std::pair{2000, 600}}) {
        DualScreen screen(ctr);
        screen.show(w, h);
        StylusRouter router;
        // Mouse: press, drag, release, mapped through the letterbox and the scale.
        const auto a = at_lower(screen, 10, 20), b = at_lower(screen, 300, 230);
        auto f = router.route({{Kind::down, a[0], a[1]}, {Kind::move, b[0], b[1]}, {Kind::up, b[0], b[1]}}, {},
                              screen, false, std::nullopt);
        check(f.touches.size() == 3 && f.touches[0].kind == embed::Touch::Kind::Down && f.touches[0].x == 10 &&
                  f.touches[0].y == 20 && f.touches[1].x == 300 && f.touches[1].y == 230 &&
                  f.touches[2].kind == embed::Touch::Kind::Up,
              "Press, move and release in order, in lower-screen pixels");
        // The upper screen and the margins are not the touch screen.
        const auto& t = screen.shown().top;
        f = router.route({{Kind::down, float(t.x + 5), float(t.y + 5)}, {Kind::up, float(t.x + 5), float(t.y + 5)}},
                         {}, screen, false, std::nullopt);
        check(f.touches.empty(), "Upper-screen presses are not stylus contacts");
        f = router.route({{Kind::down, 1, 1}, {Kind::up, 1, 1}}, {}, screen, false, std::nullopt);
        check(f.touches.empty() || w == 2000, "Presses beside the screens are not stylus contacts");
        // A press that began on the lower screen goes on off it.
        f = router.route({{Kind::down, a[0], a[1]}, {Kind::move, float(t.x + 5), float(t.y + 5)}}, {}, screen, false,
                         std::nullopt);
        check(f.touches.size() == 2 && (f.touches[1].x < 0 || f.touches[1].y < 0 || f.touches[1].x >= 320 ||
                                        f.touches[1].y >= 240),
              "A drag off the lower screen stays down, off the screen");
        // The frame rotates during the press: it slides off and lifts, and the rest is ignored.
        screen.show(h, w);
        f = router.route({{Kind::move, a[0], a[1]}}, {}, screen, false, std::nullopt);
        check(f.touches.size() == 2 && f.touches[0].kind == embed::Touch::Kind::Move && f.touches[0].x >= 100000 &&
                  f.touches[1].kind == embed::Touch::Kind::Up,
              "A layout change during a press lifts the stylus off the screen");
        f = router.route({{Kind::move, a[0], a[1]}, {Kind::up, a[0], a[1]}}, {}, screen, false, std::nullopt);
        check(f.touches.empty() && !router.stylus_down(), "The rest of that press is ignored");
        screen.show(w, h);
        // A click during a movie never becomes a stylus contact.
        f = router.route({{Kind::down, a[0], a[1]}}, {}, screen, true, std::nullopt);
        f = router.route({{Kind::up, a[0], a[1]}}, {}, screen, false, std::nullopt);
        check(f.touches.empty(), "A movie's click is not a contact in the next scene");
    }
    // Touch: a tap is held back until it lifts, then keeps its own length; a second finger makes a
    // pinch the game never sees, and zooms with correct mapping afterwards.
    const auto ds = embed::screens(embed::Title::PandorasBox);
    DualScreen screen(ds);
    screen.show(1080, 1920);
    StylusRouter router;
    const auto a = at_lower(screen, 100, 100);
    auto f = router.route({{Kind::down, a[0], a[1], true}}, {{1, a[0], a[1]}}, screen, false, std::nullopt);
    check(f.touches.empty(), "A finger's press waits for a possible second finger");
    f = router.route({{Kind::up, a[0], a[1], true}}, {}, screen, false, std::nullopt);
    check(f.touches.size() == 1 && f.touches[0].kind == embed::Touch::Kind::Down, "A tap: the press at its release");
    f = router.route({}, {}, screen, false, std::nullopt);
    f = router.route({}, {}, screen, false, std::nullopt);
    check(f.touches.size() == 1 && f.touches[0].kind == embed::Touch::Kind::Up, "and the release a tap's length later");
    const auto b = at_lower(screen, 120, 100);
    f = router.route({{Kind::down, a[0], a[1], true}}, {{1, a[0], a[1]}}, screen, false, std::nullopt);
    f = router.route({}, {{1, a[0], a[1]}, {2, b[0], b[1]}}, screen, false, std::nullopt);
    check(f.touches.empty(), "A pinch's first finger is never a contact");
    const double before = screen.zoom();
    f = router.route({{Kind::move, a[0] - 100, a[1], true}}, {{1, a[0] - 100, a[1]}, {2, b[0] + 100, b[1]}}, screen,
                     false, std::nullopt);
    check(f.touches.empty() && screen.zoom() > before, "Spreading the fingers zooms in");
    f = router.route({{Kind::up, a[0] - 100, a[1], true}}, {}, screen, false, std::nullopt);
    screen.show(1080, 1920);
    check(f.touches.empty() && screen.zoom() > 1, "Lifting keeps the zoom");
    // Mapped through the zoom: the pixel shown under a point is the one touched.
    const auto c = at_lower(screen, 128, 96);
    f = router.route({{Kind::down, c[0], c[1]}, {Kind::up, c[0], c[1]}}, {}, screen, false, std::nullopt);
    check(f.touches.size() == 2 && f.touches[0].x == 128 && f.touches[0].y == 96, "Mapping through the zoom");
}

void importer_rejects() {
    const auto games = layton_reprise::entries();
    check(games.size() == 6, "Six adventures");
    const fs::path dir = fs::temp_directory_path() / ("layton-import-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path junk = dir / "junk.bin";
    std::vector<std::uint8_t> bytes(4096, 0x5a);
    platform::write_file(junk, bytes);
    for (const auto& game : games) {
        bool not_this = false;
        try {
            platform::FileSource file(junk);
            (void)game.import_into(file, dir / "out", {});
        } catch (const app::NotThisGame&) {
            not_this = true;
        } catch (const std::exception&) {
        }
        check(not_this, "Another file is not this game");
    }
    check(!games[5].start, "Azran Legacy cannot be started (no story yet)");
    for (int i = 0; i < 5; ++i) check(bool(games[std::size_t(i)].start), "The other five start their stories");
    fs::remove_all(dir);
}

// A temporary data folder with games/<id> showing an existing pack's files (linked, never changed)
// and a release.txt of its own for the quick-save wrapper's fingerprint.
fs::path stage(const fs::path& data, const std::string& id, const fs::path& pack) {
    const fs::path content = data / "games" / id;
    fs::create_directories(content);
    for (const auto& entry : fs::directory_iterator(pack)) {
        fs::create_symlink(entry.path(), content / entry.path().filename());
    }
    const auto marker = platform::read_file(pack / "import.json");
    platform::write_text(content / "release.txt",
                         "release test\nfile " + platform::sha256(marker) + " import.json\n");
    return content;
}

int lit_pixels(const app::GameModule& game) {
    int lit = 0;
    for (const auto& c : game.picture().pixels) lit += (c.r | c.g | c.b) != 0;
    return lit;
}

// Ticks as the launcher does while the game loads its first frame (a load never blocks the
// window: the module skips ticks until then), up to 30 seconds.
void wait_until_shown(app::GameModule& game) {
    game.set_canvas(1280, 800);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (game.stopped().empty() && lit_pixels(game) == 0 && std::chrono::steady_clock::now() < until) {
        game.tick({});
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

int run_frames(app::GameModule& game, int frames, double& loudness) {
    std::vector<float> sound(1600);
    wait_until_shown(game);
    int lit = 0;
    for (int i = 0; i < frames && game.stopped().empty(); ++i) {
        game.tick({});
        game.audio(sound, 48000);
        for (const float v : sound) loudness = std::max(loudness, double(std::abs(v)));
    }
    const auto picture = game.picture();
    for (const auto& c : picture.pixels) lit += (c.r | c.g | c.b) != 0;
    return lit;
}

void with_packs(const fs::path& packs, const fs::path& checkpoints) {
    const auto games = layton_reprise::entries();
    const char* folders[] = {"a5fp-v3", "yltd-v11", "c3jd-v2", "blfd-v1", "akkd-v1", "al6d-v1"};
    const fs::path data = fs::temp_directory_path() / ("layton-data-" + std::to_string(std::random_device{}()));
    for (std::size_t i = 0; i < games.size(); ++i) {
        const fs::path pack = packs / folders[i];
        if (!fs::exists(pack / "import.json")) {
            std::printf("%s: no pack, skipped\n", games[i].id.c_str());
            continue;
        }
        const auto problem = embed::checkPack(embed::Title(i), pack);
        check(problem.empty(), "The existing pack is accepted");
        const fs::path content = stage(data, games[i].id, pack);
        check(!games[i].previews || !games[i].previews(content).empty(), "Previews decode");
        if (!games[i].start) continue;
        auto game = games[i].start(content);
        double loud = 0;
        const int lit = run_frames(*game, 900, loud);
        if (const char* shots = std::getenv("LAYTON_SHOTS")) {
            platform::save_png(game->picture(), fs::path(shots) / (games[i].id + "-start.png"));
        }
        std::printf("%s: title after 900 frames: %d lit pixels, peak %.3f, %s\n", games[i].id.c_str(), lit, loud,
                    game->stopped().c_str());
        check(game->stopped().empty() && lit > 1000, "The story starts and shows its title");
        check(loud > 0.001, "with sound");
        game.reset();
    }
    // Quick saves (checkpoint saves) through the persistent save wrapper.
    const std::pair<std::size_t, const char*> saved[] = {
        {0, "a5fp-checkpoint.sav"}, {1, "checkpoint.l2cp"},     {2, "c3jd-checkpoint.sav"},
        {3, "blfd-checkpoint.sav"}, {4, "akkd-checkpoint.sav"}};
    for (const auto& [index, file] : saved) {
        const auto& entry = games[index];
        if (checkpoints.empty() || !fs::exists(checkpoints / file) || !fs::exists(data / "games" / entry.id)) continue;
        const fs::path content = data / "games" / entry.id;
        layton_reprise::SavedCheckpoint record;
        record.game = entry.id;
        record.checkpoint = platform::read_file(checkpoints / file);
        state::Writer w;
        record.persist(w);
        auto game = app::with_quick_save(entry, content);
        check(!game->save(), "No quick save before the first room");
        game = entry.start(content);
        try {
            game->read_state(w.bytes);
        } catch (const std::exception& e) {
            std::printf("%s: %s cannot be continued (%s), skipped\n", entry.id.c_str(), file, e.what());
            continue;
        }
        double loud = 0;
        int lit = run_frames(*game, 600, loud);
        check(game->stopped().empty() && lit > 1000, "Continues from a checkpoint");
        // Save through the wrapper, then a fresh process-like load.
        // (A session continued from the checkpoint, under the wrapper.)
        app::GameEntry seeded = entry;
        seeded.start = [&](const fs::path& c) {
            auto m = entry.start(c);
            m->read_state(w.bytes);
            return m;
        };
        auto wrapped = app::with_quick_save(seeded, content);
        run_frames(*wrapped, 300, loud);
        check(wrapped->save(), "Quick save after continuing");
        {
            // The save's picture is the frame the checkpoint was written with (the room ready).
            int lit_save = 0;
            for (const auto& c : wrapped->save_picture().pixels) lit_save += (c.r | c.g | c.b) != 0;
            check(lit_save > 1000, "The save picture shows the checkpoint's room");
            if (const char* shots = std::getenv("LAYTON_SHOTS")) {
                platform::save_png(wrapped->save_picture(), fs::path(shots) / (entry.id + "-save-picture.png"));
            }
        }
        std::printf("%s: saved (%s)\n", entry.id.c_str(), wrapped->save_error().c_str());
        auto fresh = app::with_quick_save(entry, content);
        check(fresh->has_save() && fresh->load(), "Continue from the saved checkpoint");
        lit = run_frames(*fresh, 300, loud);
        check(fresh->stopped().empty() && lit > 1000, "and plays on");
        if (const char* shots = std::getenv("LAYTON_SHOTS")) {
            platform::save_png(fresh->picture(), fs::path(shots) / (entry.id + "-continued.png"));
        }
        const auto preview = app::save_preview(content, entry.id);
        check(preview.available && preview.image, "The save has its picture");
        // Damage the newest save: loading fails, the running game is unchanged.
        std::vector<fs::path> generations;
        for (const auto& e : fs::directory_iterator(data / "saves" / entry.id)) generations.push_back(e.path());
        std::sort(generations.begin(), generations.end());
        auto bytes = platform::read_file(generations.back() / "state.bin");
        bytes[bytes.size() / 2] ^= 0x55;
        platform::write_file(generations.back() / "state.bin", bytes);
        check(!fresh->load() && !fresh->save_error().empty(), "A damaged save is refused");
        run_frames(*fresh, 60, loud);
        check(fresh->stopped().empty(), "and the running game goes on");
        // Another game's save is refused by the module itself.
        const std::size_t another = index == 0 ? 1 : 0;
        auto other = games[another].start(data / "games" / games[another].id);
        bool refused = false;
        try {
            other->read_state(w.bytes);
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused, "Another game's checkpoint is refused");
    }
    std::error_code ignored;
    fs::remove_all(data, ignored);
}

} // namespace

int main() {
    layouts();
    stylus();
    importer_rejects();
    const char* packs = std::getenv("LAYTON_PACKS");
    const char* checkpoints = std::getenv("LAYTON_CHECKPOINTS");
    if (packs) {
        with_packs(packs, checkpoints ? fs::path(checkpoints) : fs::path());
    } else {
        std::printf("LAYTON_PACKS not set: the games themselves were not run\n");
    }
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("layton checks passed\n");
    return 0;
}
