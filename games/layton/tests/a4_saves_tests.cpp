// The fourth adventure's save slots: a record written to a folder reads back
// the same, the table's flags persist, a damaged file reads as an empty
// slot, and erasing removes the file.
#include "system/a4_saves.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace picarat::adventure4;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

SaveRecord sample() {
    SaveRecord r;
    r.header = {12, 34, 56, 78, 2, "pica", "Seilbr{:u}cke"};
    r.flags = {{"flag#0", 1}, {"other#3", 1}};
    r.variables = {{"step#0", 920}, {"negative#0", -5}};
    r.marks = {{"mark#0", 2}};
    r.puzzles[1] = {2, 3, 1, true, false};
    r.puzzles[228] = {1, 0, 0};
    r.names = {{0, "pica"}, {1, "second"}};
    r.playFrames = 0x123456789ull;
    r.memo = {0x8000, 0x7fff, 0x1234};
    return r;
}

bool same(const SaveRecord& a, const SaveRecord& b) {
    const SaveHeader &x = a.header, &y = b.header;
    const auto progress = [](const auto& p, const auto& q) {
        return p.state == q.state && p.failures == q.failures && p.hints == q.hints &&
               p.favourite == q.favourite && p.flag100 == q.flag100;
    };
    bool puzzles = a.puzzles.size() == b.puzzles.size();
    for (const auto& [n, p] : a.puzzles) {
        puzzles = puzzles && b.puzzles.contains(n) && progress(p, b.puzzles.at(n));
    }
    return x.hours == y.hours && x.minutes == y.minutes && x.found == y.found &&
           x.solved == y.solved && x.cleared == y.cleared && x.name == y.name &&
           x.place == y.place && a.flags == b.flags && a.variables == b.variables &&
           a.marks == b.marks && puzzles && a.names == b.names && a.playFrames == b.playFrames &&
           a.memo == b.memo;
}
} // namespace

int main(int argc, char** argv) try {
    const std::filesystem::path folder =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::temp_directory_path() / "a4-saves-test";
    std::filesystem::remove_all(folder);
    {
        SaveSlots slots(folder);
        check(!slots.slot(0) && !slots.slot(2), "a new folder has empty slots");
        check(slots.write(1, sample()), "writing slot 1");
        check(!slots.write(3, sample()), "only three slots");
        slots.setFlag(0, true);
    }
    {
        SaveSlots slots(folder);
        check(slots.slot(1) && same(*slots.slot(1), sample()), "slot 1 reads back the same");
        check(!slots.slot(0), "slot 0 stays empty");
        check(slots.flag(0) && !slots.flag(1), "the table's flags persist");
        GameData data;
        showSlots(data, slots);
        check(data.slots[1].used && data.slots[1].first == "pica" && data.slots[1].numbers[4] == 2,
              "the slot table the scripts read");
        restore(data, *slots.slot(1));
        check(data.variable("step") == 920 && data.flag("other", 3) &&
                  data.puzzles.at(1).failures == 3,
              "restoring the game data");
    }
    {
        std::ofstream(folder / "slot2.sav", std::ios::binary) << "PCR4SAV1\x05";
        SaveSlots slots(folder);
        check(!slots.slot(2), "a damaged file reads as an empty slot");
        check(slots.erase(1) && !std::filesystem::exists(folder / "slot1.sav"),
              "erasing removes the file");
    }
    std::filesystem::remove_all(folder);
    std::cout << "a4 saves: ok\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "a4 saves: " << e.what() << '\n';
    return 1;
}
