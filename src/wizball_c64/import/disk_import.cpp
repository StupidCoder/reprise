#include "wizball_c64/import/disk_import.hpp"
#include "c64/disk_image.hpp"
#include "c64/program.hpp"
#include "platform/sha256.hpp"
#include <string_view>

namespace wizball::import {
namespace {

// The release this was recovered from (Wizball.g64, MD5
// 73e1a136da4b6972d39f00817b86983e). The hash is of the bytes without the
// load address.
constexpr std::string_view release_name = "Ocean disk (1987)";
constexpr std::string_view file_name = "WIZBALL";
constexpr std::uint16_t load_address = 0x0400;
constexpr std::size_t file_size = 0xF901; // $0400-$FD00
constexpr std::string_view file_sha256 = "eca9b29e2774c9a57f1780bb2d8102f6938968920f7f74b8a2b679ace323fe23";

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!c64::is_g64(image)) {
        throw app::NotThisGame("This is not a G64 disk image.");
    }
    const auto disk = c64::read_g64(image);
    for (const auto& entry : c64::read_directory(disk)) {
        if (entry.type != 2 || entry.name != file_name) {
            continue;
        }
        const auto program = c64::Program::from_prg(c64::read_file(disk, entry.track, entry.sector));
        if (program.load_address != load_address || program.bytes.size() != file_size ||
            platform::sha256(program.bytes) != file_sha256) {
            throw std::runtime_error("This disk holds a different release of Wizball, which is not supported yet.");
        }
        app::ImportResult result;
        result.release = std::string(release_name);
        result.files.push_back({"wizball.prg", program.to_prg()});
        return result;
    }
    throw app::NotThisGame("This disk is not Wizball (it has no WIZBALL file).");
}

} // namespace wizball::import
