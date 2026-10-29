// The cartridge save image: formatting a blank card, the block checksum,
// writing and reading a slot, damage detection and erasing a slot.
#include "system/resources.hpp"
#include "system/a2_save_file.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace picarat;

void check(bool yes, const char* why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

int main(int argc, char** argv) try {
    if (argc != 2) {
        throw std::runtime_error("save file test output required");
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    const auto path = dir / "backup.sav";
    std::filesystem::remove(path);

    // 0206b1b4 over zeroed bytes: both sums stay 0xffff (the ROM's own
    // shared block after formatting carries ffffffff).
    const Bytes zeros(140, 0);
    check(SaveFile::checksum(zeros.data(), zeros.size()) == 0xffffffffu, "checksum of zeros");

    {
        SaveFile file(path);
        check(file.boot() == 0, "a blank card formats cleanly");
        check(!file.anyUsed(), "no slot after formatting");
        Bytes block(SaveFile::slotSize, 0);
        for (std::size_t i = 4; i < block.size(); ++i) {
            block[i] = std::uint8_t(i * 7);
        }
        file.invalidateHeader();
        file.writeSlot(2, block);
        file.setUsed(2, true);
        file.setName(2, "Name");
        file.setPlace(2, 82);
        file.setSeconds(2, 5338);
        file.setFound(2, 119);
        file.setSolved(2, 118);
        file.setClearLevel(2, 2);
        file.commitHeader();
    }
    const auto image = readFile(path);
    check(image.size() == SaveFile::imageSize, "image size");
    check(std::string(reinterpret_cast<const char*>(image.data()), 14) == "ATAMFIREBELLNY",
          "card identification");
    {
        SaveFile file(path);
        check(file.boot() == 0, "a written slot checks out");
        check(file.used(2) && !file.used(0) && file.name(2) == "Name" && file.place(2) == 82 &&
                  file.seconds(2) == 5338 && file.found(2) == 119 && file.solved(2) == 118 &&
                  file.clearLevel(2) == 2,
              "summary round trip");
        const auto block = file.readSlot(2);
        check(block[100] == std::uint8_t(100 * 7), "slot block round trip");
    }

    // A damaged block is reported (bit 2) and the slot is cleared.
    {
        auto damaged = image;
        damaged[0x7f0 + 200] ^= 1;
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(damaged.data()), std::streamsize(damaged.size()));
        SaveFile file(path);
        check(file.boot() == 4, "damaged slot detected");
        check(!file.used(2), "damaged slot cleared");
        file.repair(4);
        check(SaveFile(path).boot() == 0, "repaired header checks out");
    }

    // Erasing a slot (the app's save management).
    {
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
        SaveFile file(path);
        check(file.erase(2), "erase");
        check(!file.erase(3), "erase out of range");
        SaveFile again(path);
        check(again.boot() == 0 && !again.used(2) && again.clearLevel(2) == 0, "erased slot");
    }
    std::cout << "save file tests passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
