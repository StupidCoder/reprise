// Lists an Amiga disk image's files and their hunk structure, and extracts
// them for research.
//
//   amiga_disk list <image.adf>
//   amiga_disk extract <image.adf> <out dir>
//   amiga_disk hunks <file>            hunk table of a load file
//   amiga_disk flat <file> <base> <out>  relocated memory image of a load file
#include "amiga/adf.hpp"
#include "amiga/hunk.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_all(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

const char* kind_name(amiga::HunkKind kind) {
    return kind == amiga::HunkKind::code ? "CODE" : kind == amiga::HunkKind::data ? "DATA" : "BSS";
}

void print_hunks(const amiga::LoadFile& file) {
    std::uint32_t base = 0;
    std::vector<std::uint32_t> addresses;
    amiga::relocate(file, base, addresses);
    for (std::size_t i = 0; i < file.hunks.size(); ++i) {
        const auto& hunk = file.hunks[i];
        std::size_t relocs = 0;
        for (const auto& r : hunk.relocations) {
            relocs += r.offsets.size();
        }
        std::printf("  %3zu %-4s %06x size %6u flags %08x relocs %4zu syms %zu", i, kind_name(hunk.kind),
                    addresses[i], hunk.size, hunk.memory_flags, relocs, hunk.symbols.size());
        for (const auto& s : hunk.symbols) {
            std::printf(" %s@%x", s.name.c_str(), s.offset);
        }
        std::printf("\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if ((command == "list" || command == "extract") && argc >= 3) {
            const auto image = read_all(argv[2]);
            const amiga::Volume volume(image);
            std::printf("volume \"%s\" %s\n", volume.name().c_str(), volume.fast_file_system() ? "FFS" : "OFS");
            for (const auto& entry : volume.entries()) {
                if (entry.directory) {
                    std::printf("%-28s <dir> header %u\n", entry.path.c_str(), entry.header_block);
                    continue;
                }
                std::vector<std::uint8_t> bytes;
                try {
                    bytes = volume.read(entry);
                } catch (const std::exception& error) {
                    std::printf("%-28s %7u header %4u: %s\n", entry.path.c_str(), entry.size, entry.header_block,
                                error.what());
                    continue;
                }
                std::printf("%-28s %7u header %4u blocks %4u..%4u first %02x%02x%02x%02x\n", entry.path.c_str(),
                            entry.size, entry.header_block, entry.data_blocks.empty() ? 0 : entry.data_blocks.front(),
                            entry.data_blocks.empty() ? 0 : entry.data_blocks.back(), bytes.size() > 0 ? bytes[0] : 0,
                            bytes.size() > 1 ? bytes[1] : 0, bytes.size() > 2 ? bytes[2] : 0,
                            bytes.size() > 3 ? bytes[3] : 0);
                if (command == "extract") {
                    write_all(std::filesystem::path(argv[3]) / entry.path, bytes);
                }
            }
            return 0;
        }
        if (command == "hunks" && argc >= 3) {
            print_hunks(amiga::read_load_file(read_all(argv[2])));
            return 0;
        }
        if (command == "flat" && argc >= 5) {
            std::vector<std::uint32_t> addresses;
            const auto image =
                amiga::relocate(amiga::read_load_file(read_all(argv[2])), std::stoul(argv[3], nullptr, 0), addresses);
            write_all(argv[4], image);
            return 0;
        }
        std::fprintf(stderr, "usage: amiga_disk list|extract <adf> [out] | hunks <file> | flat <file> <base> <out>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
