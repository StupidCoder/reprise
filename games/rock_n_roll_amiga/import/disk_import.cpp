#include "rock_n_roll_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "amiga/hunk.hpp"
#include "platform/sha256.hpp"
#include "rock_n_roll_amiga/import/cruncher.hpp"
#include "rock_n_roll_amiga/import/packers.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace rocknroll::import {
namespace {

// "Rock'n Roll (1991)(Rainbow Arts)[compilation 5th Anniversary].adf",
// SHA-256 09ca427b…ed22d4. Recognised by its boot block (the first two
// blocks), then every unpacked part is checked by its SHA-256.
constexpr std::string_view release_name = "Rainbow Arts 5th Anniversary compilation (1991)";
constexpr std::string_view boot_sha256 = "84dd97caa70f27c6fd4aa3caa4155b1cf05c585d05830919deb43fb45482ea4a";
constexpr std::string_view logo_sha256 = "67375cfb846dab329781513746d83f39bb499d388e1eb64f87f476a5ae805a55";
constexpr std::string_view loader_sha256 = "993f5c24539deffc30a3b534d30690ff8288b1ef5891943f06e2effd499e0204";
constexpr std::string_view intro_sha256 = "fd28a2a57a90afb3a517f0b8536b3d01b7ca55a34494d5921007509378308172";
constexpr std::string_view main_sha256 = "fe997f4ae487ebf122e8cbb12487f725c9ba1d817670620e7a59638c936a31b7";

// The boot block's two trackdisk reads: $2A00 bytes from disk offset $200 and
// $2400 from $D9C00, together the memory from $50000. The logo's packer header
// lies at $50196, its packed data at $505BA.
constexpr std::size_t boot_part1 = 0x200, boot_part1_size = 0x2A00;
constexpr std::size_t boot_part2 = 0xD9C00, boot_part2_size = 0x2400;
constexpr std::size_t logo_header = 0x196, logo_data = 0x5BA;

// The track loader: a load file at disk offset $DB600 whose code (at $60024)
// unpacks the loader to $A000. Its packed stream ends at $609CE; the file
// table lies at $A65C, 16 bytes a file: load address, length, track, offset
// in the track, crunched flag. A track is $1600 bytes of the image.
constexpr std::size_t loader_file = 0xDB600;
constexpr std::size_t loader_stream_end = 0x609CE - 0x60024;
constexpr std::size_t file_table = 0xA65C - 0xA000;
constexpr std::size_t track_size = 0x1600;
constexpr int game_program = 0x43; // a 8 KB front part, then Main
constexpr int band_intro = 0x44;
constexpr std::size_t main_offset = 0x2000;

// Main's unpacking stub (code hunk +$0): LEA $E8(PC),A0 / LEA $1A578,A1 —
// the crunched program follows at +$E8 and unpacks to $1A578.
constexpr std::array<std::uint8_t, 10> main_stub = {0x41, 0xFA, 0x00, 0xE6, 0x43, 0xF9, 0x00, 0x01, 0xA5, 0x78};
constexpr std::size_t main_stream = 0xE8;

// The band intro's load file (code at $3A020 once loaded at $3A000): DefJam's
// code is copied from $3A092 to $7F000; it reads the packed length at
// $7F042, the literal table at $7F186 and the data from $3A318.
constexpr std::size_t defjam_length = 0x3A092 - 0x3A020 + 0x42;
constexpr std::size_t defjam_literals = 0x3A092 - 0x3A020 + 0x186;
constexpr std::size_t defjam_data = 0x3A318 - 0x3A020;

struct GameFile {
    int number;
    std::string_view sha256; // of the unpacked bytes
};

// Every game file the program's load table ($78136) names, except $3F
// (Main). File $35 is stored as it is (a sample bank); all others are
// crunched. $3D and $3E are copies of $33 and $39.
constexpr std::array<GameFile, 65> game_files = {{
    {0x00, "b49107242759c7f6e6e5618a0033b6875173e79f964b7a1412e479218d3a35c5"},
    {0x01, "d442a0df59c12399d022536d2eaa3e867fbc72c3ea540d9794140e095c80d8b8"},
    {0x02, "c9126477dec2c918c7e9b008870260fcd192ae37a3eb7c31c701af13ff123ae8"},
    {0x03, "4111c2a69355cb488ee1f1a9c4fd47f6807f4ef1fdcbe3fa1402f66eb6b640ca"},
    {0x04, "af897ee608bded1f564a72f3b8aad5ba3dae43293dad84b19ca3fbe347315f6f"},
    {0x05, "7b049b3e2c69d1d470ce16adbcb78e2ca9f2e747efb4a59ecf4789068767f119"},
    {0x06, "1c0195c89ca7f86ee3a790c34e7e874c3dd1189a9db80948af6c9a868cbdbefb"},
    {0x07, "60084f9e9d73b7371173bcf91571776ba24dda0bdb448af18826e6912c0bae88"},
    {0x08, "26a849cd3e36471c3d3bb8cdecb893d4151009ee748efd66226239516b07a83d"},
    {0x09, "54686fbc3dfb341e0145a11ca0142a861d88c69ed7ce31fe621534a60dd69228"},
    {0x0A, "915bf48d45bba706ec487f5ba920124d6a8503af12d38b798bba45a567443b9c"},
    {0x0B, "bb1e8a71731bd11e8ca862465e87daae4e59ab59e8343d9e28f910d88add2dd7"},
    {0x0C, "0be8db425de18a8b6ad95dede08f1a63e3ca6851d1887fb15d5151a13d463348"},
    {0x0D, "d255eec2fcdde23370a77667682d29667b2511e9eefb5aa1f568aa2709167376"},
    {0x0E, "abbca40092bcad810040967f35d50e69fb01b498064888d5c7f9dc9cf3878ba0"},
    {0x0F, "c808094162ead2c8941c0ed2ecace2bd83e5c1f2193c46fa40d1cc15247946fd"},
    {0x10, "8b441426463f0db8f0c0f839531e47368d2459462c7b8c1c1af99ebad53f062e"},
    {0x11, "60632f761a123cd5592dc77fde1f1067793c22618213e755c3fb858229cb0e6c"},
    {0x12, "d7d52a3628a74b989ae331eb4f19bf6037f3ef75789632d4df27465c50cfa300"},
    {0x13, "7af6ee7f8e53f412bc23b920d9bf081783783b8f255aca2e8c275e700abf1696"},
    {0x14, "fce0dae8769111d5ab4d5726e6f7cc13179f8ceed5db793d487eeed1ffc0a633"},
    {0x15, "e8912d63cc725edb03362dd8fa613c6d3f866f23c723f8733296db022a518595"},
    {0x16, "05d5124f9d266b6f1167a8bf9909dc408dee56336fbc682ce2daaa3ba4c1cca2"},
    {0x17, "449801cbb8dc30cf405ba7e8440d103861754742f61ce7e5715069fbf5a0ddaf"},
    {0x18, "d5ab1fd041e8996c89ccbb0419a43771ed28309d0de980eb0df19c8712c83ea1"},
    {0x19, "6133ff8b8dd24d244b716c12b60e1467b8e9dc74339fa2420d7de40b4e66998e"},
    {0x1A, "1ec0e13ee6a3ac6fed87e640ca1c7193253112436d56a359012dde9fcd35ab06"},
    {0x1B, "e698bb3683e8f1850be36ea281e4447b955ff03b6d2d71962ee1f434f2d852ed"},
    {0x1C, "69bcbecadb34245f13887651f8a1ab9566467f07375015f3784c8746c9c290e5"},
    {0x1D, "ed5b66daff4b3487649630ee36f761b9edb09be7628507579d2b248452cfecf3"},
    {0x1E, "b1dcd1765dbfbf3a4d02441f364e0e5063024fdba7d49140316e1f3a888156d2"},
    {0x1F, "e30842ad0445c4069834c8af32ada750ca17390d0023f3a08c037da91a4e63b7"},
    {0x20, "968c4d330ee0fe44f93db98e8b34de72e8793e2f3adae7eb58dd2405a27474a0"},
    {0x21, "108cb1cf920d2541bedaf876af9a8e0fd0ce967278263dc0e91aaab34137a93f"},
    {0x22, "90f8ca291a6c140067bad5a08a29888a37e677de67593f70efeedf88af4fdd5d"},
    {0x23, "ee1746fbda2b5b0e49de519719ad6363b0022cba4dd4b0dc934a53960ebbf0e3"},
    {0x24, "935f88d448a94e5981e0f5a84fac569b4baa136d33fe2c095fd78972a3bcec34"},
    {0x25, "f80d4b16a4afd864c16587444a67e169784448bfd0a6973d72915b6ddd7ac4a2"},
    {0x26, "0ea447403c6dae90edf7057c40dfa91181a28ca2a98fb5d95e0299dcaf29a706"},
    {0x27, "7fb2d43d826bae9a332433f3b21874e1cd249dec79df21c16f9982f758c0cd21"},
    {0x28, "05c2ea467d6fce51480de62b22cfd230a094b9280c34306553176126ad7d794f"},
    {0x29, "1db19d22b0b08bc9d5316c5b723fbd3228ff681b6e7e642b49ef1aa6dbab8eda"},
    {0x2A, "45318d553e1810eb44970721e34a46418c938c2d46bcb444c38d86d0c7768889"},
    {0x2B, "294ab5e942c63c8b68ca5aee1bf635b1fa09a446f237dce246dbe4d80eed6fb2"},
    {0x2C, "cec931ab383fa66f767197b702f7d2e7818628a41d4b8a3eb64617ce5d4c01f2"},
    {0x2D, "ba45680278100e08478df5fc8f641c50e0a76ec54c5308ceee5b49a6d6c69b4c"},
    {0x2E, "2a81a5f91bf1ec27abd2f5c03de1fca35c10e12a805b9e5823ca10466490e4b0"},
    {0x2F, "63d220b9aca132d710451d3f3654058263b3d5ca46ad253d09b7a5960c39df4f"},
    {0x30, "ef8524c59fba4c046ff69736faf0c81d0017521eff569a4ccf04cdeac845beb6"},
    {0x31, "4146b09ccc0ae6f7c08767de98271d90b64981c6788425e7cc2ccc9d862e747d"},
    {0x32, "2bddef773859b0f0f387c829a12ccc9a9e0dacb0fc3b6d1fed842e678ec20072"},
    {0x33, "dcd35cadd0e07d86792cc65ea9a2abbd32b2a1c2bcd219cfb70f5f34a0bb9e0b"},
    {0x34, "08f2ab5dff0bdcd0d5aac3f99c9931de3cc829e62c8e910a2e964b9234d1c805"},
    {0x35, "932100865f5c38dd9358f94ddd0e643fb293bd7c1ffd2f2e7930b5bf7318a375"},
    {0x36, "e734928c7987bbe7e04345933f78006a45d17432a3995dac29069951274e06bd"},
    {0x37, "df46c04e11371f4e7aae158e49721a128be54a0c0cb331f37295e468b3c38655"},
    {0x38, "19dd796c72fc56a31836767677a8d55b3cced8f4fd27e3004f5224671e6fc3d1"},
    {0x39, "3bf2be9c81eba27f5018dfcd1f14b42eb2919431e2a702f399fe1e729e36a77e"},
    {0x3A, "cd5859f58142ca212e64e761d338a28e8ccebdda3421282e8c106621425ef2f1"},
    {0x3B, "051009ecd9a84e13db8b834e596df801de96912854bb2178be1e553cb4d6ecf7"},
    {0x3C, "3d875c599056ad4e956058c4be759a111e1c528074af5969566ab7265fe706fb"},
    {0x3D, "dcd35cadd0e07d86792cc65ea9a2abbd32b2a1c2bcd219cfb70f5f34a0bb9e0b"},
    {0x3E, "3bf2be9c81eba27f5018dfcd1f14b42eb2919431e2a702f399fe1e729e36a77e"},
    {0x40, "24352e2dcf744e1dbe82c3cf437d4e5d5dc2811dbfdd0d97af5cf85450777960"},
    {0x41, "f2d59ec897031b61331f889afc518f0f40db793ccd09b4cd02e390626ab615b1"},
}};

std::span<const std::uint8_t> part(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t size) {
    if (at > bytes.size() || size > bytes.size() - at) {
        throw std::runtime_error("The disk's data is cut short.");
    }
    return bytes.subspan(at, size);
}

void check(std::span<const std::uint8_t> bytes, std::string_view sha256, std::string_view what) {
    if (platform::sha256(bytes) != sha256) {
        throw std::runtime_error("The disk's " + std::string(what) + " is damaged or from a different release.");
    }
}

std::vector<std::uint8_t> code_hunk(std::span<const std::uint8_t> load_file, std::string_view what) {
    const auto file = amiga::read_load_file(load_file);
    if (file.hunks.empty() || file.hunks[0].kind != amiga::HunkKind::code) {
        throw std::runtime_error("The disk's " + std::string(what) + " has no program code.");
    }
    return file.hunks[0].bytes;
}

std::vector<std::uint8_t> unpack_main(std::span<const std::uint8_t> load_file) {
    const auto code = code_hunk(load_file, "game program");
    if (code.size() <= main_stream || !std::equal(main_stub.begin(), main_stub.end(), code.begin())) {
        throw std::runtime_error("The disk's game program belongs to a different release of Rock 'n Roll.");
    }
    // The stream's own length decides where it ends (the hunk is padded to longs).
    const std::span<const std::uint8_t> crunched(code.data() + main_stream, code.size() - main_stream);
    const std::size_t length = 12 + amiga::read_be32(crunched, 0);
    return uncrunch(part(crunched, 0, length));
}

std::vector<std::uint8_t> unpack_intro(std::span<const std::uint8_t> load_file) {
    const auto code = code_hunk(load_file, "band intro");
    const std::size_t length = amiga::read_be32(part(code, defjam_length, 4), 0);
    return unpack_defjam(part(code, defjam_data, length), part(code, defjam_literals, 264));
}

struct FileEntry {
    std::uint32_t length;
    std::size_t at; // in the image
    bool crunched;
};

FileEntry file_entry(std::span<const std::uint8_t> loader, int number) {
    const auto e = part(loader, file_table + std::size_t(number) * 16, 16);
    return {amiga::read_be32(e, 4), amiga::read_be16(e, 8) * track_size + amiga::read_be32(e, 10),
            amiga::read_be16(e, 14) != 0};
}

} // namespace

std::string file_name(int number) {
    char name[8];
    std::snprintf(name, sizeof name, "%02x", number);
    return name;
}

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    if (platform::sha256(image.first(2 * amiga::block_size)) != boot_sha256) {
        const bool cracked = image.size() > 0x6E000 + 0x1B0 &&
                             std::string_view(reinterpret_cast<const char*>(image.data()) + 0x6E000 + 0x1B1, 9) == "Rock&Roll";
        if (cracked) {
            throw std::runtime_error("This is a cracked one-disk copy of Rock 'n Roll, which lacks the band intro. "
                                     "Reprise supports the 1991 Rainbow Arts 5th Anniversary compilation disk.");
        }
        throw app::NotThisGame("This disk is not Rock 'n Roll.");
    }

    app::ImportResult result;
    result.release = std::string(release_name);

    std::vector<std::uint8_t> boot(boot_part1_size + boot_part2_size);
    std::ranges::copy(part(image, boot_part1, boot_part1_size), boot.begin());
    std::ranges::copy(part(image, boot_part2, boot_part2_size), boot.begin() + boot_part1_size);
    const auto header = part(boot, logo_header, 0x24 + 0x400);
    const std::size_t logo_packed = amiga::read_be32(header, 0x10);
    auto logo = unpack_boot(header, part(boot, logo_data - 4, logo_packed + 4));
    check(logo, logo_sha256, "Rainbow Arts logo");

    const auto loader_code = code_hunk(image.subspan(loader_file), "track loader");
    const auto loader = unpack_loader(part(loader_code, 0, loader_stream_end));
    check(loader, loader_sha256, "track loader");
    auto read = [&](int number) {
        const auto e = file_entry(loader, number);
        const auto stored = part(image, e.at, e.length);
        return e.crunched ? uncrunch(stored) : std::vector<std::uint8_t>(stored.begin(), stored.end());
    };

    // The game program: an 8 KB front part (the compilation's trainer, whose
    // options the loader clears before the game starts) and then Main.
    const auto program = read(game_program);
    if (program.size() <= main_offset) {
        throw std::runtime_error("The disk's game program is cut short.");
    }
    auto main = unpack_main(std::span(program).subspan(main_offset));
    check(main, main_sha256, "game program");
    result.files.push_back({"main", std::move(main)});
    for (const auto& file : game_files) {
        auto bytes = read(file.number);
        check(bytes, file.sha256, "file " + file_name(file.number));
        result.files.push_back({file_name(file.number), std::move(bytes)});
    }

    auto intro = unpack_intro(read(band_intro));
    check(intro, intro_sha256, "band intro");
    result.files.push_back({"logo", std::move(logo)});
    result.files.push_back({"intro", std::move(intro)});
    return result;
}

} // namespace rocknroll::import
