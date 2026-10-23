#include "fort_apocalypse_c64/import/tape_import.hpp"
#include "c64/kernal_tape.hpp"
#include "c64/program.hpp"
#include "fort_apocalypse_c64/import/novaload.hpp"
#include "platform/sha256.hpp"
#include <map>
#include <stdexcept>
#include <string_view>

namespace fort_apocalypse::import {
namespace {

struct ExpectedProgram {
    std::string_view file;
    std::uint16_t load_address;
    std::size_t size;
    std::string_view sha256;
};

// The release the reconstruction is based on: the tape image with MD5
// bec7409816865f3ad160af9984f127cd (title screen "COPYRIGHT SYNSOFT").
// Hashes are of the decoded program bytes, without the load address.
constexpr std::string_view release_name = "Novaload tape release";
constexpr ExpectedProgram release[] = {
    {"boot.prg", 0x0801, 175, "c107f06ff4a20705758ee874d7c7dc221264033248d55c91a03b0319710423f4"},
    {"loader-e000.prg", 0xE000, 0x0700, "358dc28c9af8ab63c88b28a32afaddf54fbc90b938c3235168f27248017f5853"},
    {"loader-ee00.prg", 0xEE00, 0x0400, "f0c2c9ae653e220f281bcd524d4f60a1867269f564ae6d6e7766ffd161a94f4a"},
    {"game.prg", 0x7000, 0x4900, "1856e5d8d83df0e49478bb011f20b0ebe923115a5ef16a4bb123f1512ac6347a"},
};

// Joins the loaded pages into contiguous programs, as the loader leaves them
// in memory. A page loaded twice keeps its last copy, as in the original.
std::vector<c64::Program> programs_from_pages(const std::vector<PageRecord>& records) {
    std::map<std::uint8_t, const PageRecord*> pages;
    for (const auto& record : records) {
        pages[record.page] = &record;
    }
    std::vector<c64::Program> programs;
    int previous = -2;
    for (const auto& [page, record] : pages) {
        if (page != previous + 1) {
            programs.push_back({std::uint16_t(page << 8), {}});
        }
        programs.back().bytes.insert(programs.back().bytes.end(), record->bytes.begin(),
                                     record->bytes.end());
        previous = page;
    }
    return programs;
}
} // namespace

app::ImportResult import_tape(std::span<const std::uint8_t> image) {
    if (!c64::is_tap(image)) {
        throw app::NotThisGame("This is not a TAP tape image.");
    }
    const auto tape = c64::read_tap(image);
    const auto files = c64::read_kernal_files(tape);
    if (files.size() != 1 || files[0].name != "FORT" ||
        files[0].type != c64::KernalFile::Type::basic) {
        throw app::NotThisGame("This tape is not Fort Apocalypse (its boot program is not "
                                 "the expected one).");
    }
    const auto& boot = files[0];
    const auto stream = read_novaload(tape, boot.end_pulse);

    std::vector<c64::Program> programs{{boot.load_address(), boot.data}};
    for (auto& program : programs_from_pages(stream.records)) {
        programs.push_back(std::move(program));
    }
    // Order as in the release table: boot, then by address with the game last.
    std::vector<c64::Program> ordered;
    for (const auto& expected : release) {
        for (const auto& program : programs) {
            if (program.load_address == expected.load_address) {
                ordered.push_back(program);
            }
        }
    }
    if (ordered.size() != std::size(release) || programs.size() != std::size(release)) {
        throw std::runtime_error("This tape loads a different set of programs than the "
                                 "supported Fort Apocalypse release.");
    }

    app::ImportResult result;
    result.release = std::string(release_name);
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto& expected = release[i];
        const auto& program = ordered[i];
        if (program.bytes.size() != expected.size ||
            platform::sha256(program.bytes) != expected.sha256) {
            throw std::runtime_error("This tape decodes correctly but is a different release "
                                     "of Fort Apocalypse, which is not supported yet.");
        }
        result.files.push_back({std::string(expected.file), program.to_prg()});
    }
    return result;
}

} // namespace fort_apocalypse::import
