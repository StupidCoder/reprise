#include "system/a2_checkpoint.hpp"
#include "system/resources.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace picarat;

void check(bool yes, const char* why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

void write(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

int main(int argc, char** argv) try {
    if (argc != 2) {
        throw std::runtime_error("checkpoint test output required");
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    const auto path = dir / "save.l2cp";
    std::filesystem::remove(path);
    CheckpointStore store(path);
    check(!store.load(), "missing checkpoint");
    Checkpoint c;
    c.name.insert(0xe4);
    c.name.insert('x');
    c.progress.setPlace(91);
    c.progress.setStoryStep(10);
    c.progress.puzzleByte(0) = 0x11;
    c.progress.setHintCoins(9);
    c.progress.setEventViewed(131, true);
    c.progress.setPendingEvent(-1);
    c.scene = scene::Room;
    store.save(c);
    auto loaded = store.load();
    check(loaded && loaded->name.value() == c.name.value() && loaded->name.cursor == 2 &&
              loaded->progress.bytes == c.progress.bytes && loaded->scene == scene::Room,
          "round trip room checkpoint");
    c.scene = scene::DramaEvent;
    c.eventId = 20001;
    c.puzzle = 1;
    c.retryVariant = true;
    c.randomState = 0xdeadbeef;
    c.following = scene::PuzzleResult;
    store.save(c);
    loaded = store.load();
    check(loaded && loaded->eventId == 20001 && loaded->puzzle == 1 && loaded->retryVariant &&
              !loaded->revisitVariant && loaded->randomState == 0xdeadbeef &&
              loaded->following == scene::PuzzleResult,
          "replace event checkpoint");
    const auto valid = readFile(path);
    write(path.string() + ".tmp", Bytes{1, 2, 3});
    check(store.load()->eventId == 20001, "interrupted temporary write leaves prior save readable");
    for (unsigned which = 0; which < 3; ++which) {
        auto corrupt = valid;
        if (which == 0) {
            corrupt[200] ^= 1;
        } else if (which == 1) {
            corrupt.resize(20);
        } else {
            corrupt[4] = 3;
        }
        write(path, corrupt);
        bool rejected = false;
        try {
            store.load();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected && readFile(path) == corrupt,
              "reject corrupt/truncated/incompatible without mutation");
    }
    // A format-1 checkpoint (before the story router) is refused, not migrated.
    auto old = valid;
    old[4] = 1;
    write(path, old);
    bool rejected = false;
    try {
        store.load();
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("format 1") != std::string::npos;
    }
    check(rejected && readFile(path) == old, "format-1 checkpoint is refused without mutation");
    Checkpoint bad = c;
    bad.scene = scene::Menu;
    rejected = false;
    try {
        store.save(bad);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "unsupported resume scene rejected");
    write(path, valid);
    std::cout << "Checkpoint round trips, atomic replacement and rejection passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
