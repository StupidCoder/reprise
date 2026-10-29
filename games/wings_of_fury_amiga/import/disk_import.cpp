#include "wings_of_fury_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <string>

namespace wings::import {
namespace {

constexpr std::string_view volume_name = "Wings of Fury";
constexpr std::string_view game_folder = "Wings_of_Fury/";

struct Release {
    std::string_view name;
    std::string_view wings_sha256;
};

// The release this was recovered from
// ("Wings_of_Fury_v1.0_1990_Broderbund_cr_Hysteria.adf", SHA-256
// 638d9a9d…4620044). It is a cracked copy: its `wings` no longer calls the
// manual-word check ($10F41A), and s/startup-sequence types the cracker's
// banner first (disk.md). Everything else is the game's own data.
constexpr std::array releases = {
    Release{"Broderbund v1.0 (1990), cracked copy", "923b364017f5b2547f0475517dc62e9260854bf3800d56d459721210bc004c27"},
};

struct DiskFile {
    std::string_view path; // below the game folder unless it starts with "fonts/"
    std::string_view sha256;
};

constexpr std::array programs = {
    DiskFile{"songplay", "0a4187cdff0bd14e11f184e116a2b29804019d778a702757714b34422e01b0ed"},
    DiskFile{"wofsongs", "e7662e8e2ff1cfd4ff6219fe1f6935841140424227bc688758cf722c73bcaf86"},
};

constexpr std::array data_files = {
    DiskFile{"shapes/8thscale.shp", "cd2fdd6fc7dc46b3d7607f9a448137447f23651a2686bb03cbbd1578a3eadc2b"},
    DiskFile{"shapes/battleship.shp", "4b21d17d57ac0c4e5c5ae2084e3edf9a26149781c830cc3d72ce48b7ec947ae0"},
    DiskFile{"shapes/broderbund", "1d83e351f5d2587c195acd6d6c05193319d384949128e74231de9d7b224ca63f"},
    DiskFile{"shapes/creditscreen", "6984197988c1c6e3bf66b0a28ee3961569298f6283b4d955f1343d29b4b03cf1"},
    DiskFile{"shapes/cruiseship.shp", "fe1b50be077a9e8f4757e414447a5757467a2e2205945b2501f915e5ac95fdf3"},
    DiskFile{"shapes/dash.shp", "bf4fcb83a16b99e54f402016c526c7ba4d42ffe8eb9013ae2d2c44a5875524e7"},
    DiskFile{"shapes/destroyer.shp", "39e657ceac7f7123174f228aef683b65e0c7deb9b80bf38f51e2728a8a49cb7d"},
    DiskFile{"shapes/hellcat.shp", "dacce39af7d1dd627eff940bd1d12b3453bd0604c910c450f69e4514705d40e1"},
    DiskFile{"shapes/hiscore.iff", "9bac3ce5c302c364789b35f3f03847da1b4c0f5a4b5c65507b7aa0e4ef294809"},
    DiskFile{"shapes/hiscoreslab", "299b36a2bf1f5f6d6c0c96e87f9e7304087ce6bfb41c2277aed3433138bfe045"},
    DiskFile{"shapes/iff-dash", "30fb832f8fd3457b988b61c7c0a10ea39676957a405b485bbb0f0d9f00a7cc4f"},
    DiskFile{"shapes/japcarrier.shp", "8275fbbfc528068be9a50b377ae0ee127941960e92de4ce88af81ccd4ae229a7"},
    DiskFile{"shapes/japplane.shp", "3c2bf7a137a09efb3b8486c2c8ecea877a5018a1e26d2ba3847b6b1cadd7aed9"},
    DiskFile{"shapes/night.p", "579c86f4120f6f4a2b75d7c23dae61ccf7b036bcc2e536c54b1574a3d9aed427"},
    DiskFile{"shapes/nightdash", "7de170d93feba1ce51f7fb237f4f736e879ecadfcd221582ec03f9659331f9e5"},
    DiskFile{"shapes/nightdash.shp", "67da3401dbaa12de2e16cb7627668dbff1d5933fe7d9c2940f2b9489f1729cc7"},
    DiskFile{"shapes/nightocean.p", "ad582c56d15b53fb483d1ebc00504af044c56f64eb51d670c3f373d801c12573"},
    DiskFile{"shapes/ocean.p", "68455215efbb9e36f45952b6af1445c4c1f47c97276c81a3fb78ffd1b56b0c13"},
    DiskFile{"shapes/ocean.palette", "701a351ccfbae78c8285f9d7ffab0d532dcaf6716395930b7055e2bfc4d70902"},
    DiskFile{"shapes/palette", "c6d874c14c935299e4416bf22a1e0400f00f21bc55e38d2dc2f32ef7e735c65a"},
    DiskFile{"shapes/rank.iff", "c8b18c17129288aab2d612aeb9b47dd26e51695cce0c2244de4e4dbcf2746c46"},
    DiskFile{"shapes/selectrank", "27f0c6149714afaf191f89da14a99df0690d4c4226080be35678d186c3891441"},
    DiskFile{"shapes/selectrank.shp", "ceecfbdba1c44e066c8f6023b57a06d76ceeb7a421da61285541ee3984679e9d"},
    DiskFile{"shapes/torpedo.shp", "05f156491c236eb0dbf1f103fb2703982602a922f0b7787e4fc83573900589f4"},
    DiskFile{"shapes/wingspalette", "1a7fff7d32d80e0628d1e34c4c4b90b59c31396e20204385336c0eedee365ca8"},
    DiskFile{"shapes/wingstitle", "b564fa3494e8ef8762fb4781f2ed002d72a8566fe50522dc8ceb6440c36b8fd6"},
    DiskFile{"shapes/world.shp", "04571427c438d26ce0da109177b013358b5318b0f4fb5f8c4abc16b67f65a841"},
    DiskFile{"sounds/boom", "ab32756387cadde199b4137fa79174658f20e86af46da5da1540001f6824b9fe"},
    DiskFile{"sounds/engine", "302921457c438d0d18578c21dc6fa3973c62990001525273e1ef176f37b75d15"},
    DiskFile{"sounds/grind.1", "e87f27fa600c0bb69f836727b8ee5c07549e8a4222639f5615f088cd12721d2f"},
    DiskFile{"sounds/machinegun", "ed1b8a0f1eff3b9204cf796b7246fd3b9584d7ddfc7c92d43e35f595ba3281fb"},
    DiskFile{"sounds/metal.clang.1", "8b3724318efe6e33b462af49660d2594f81aba3d0de643056e4283ee898968c6"},
    DiskFile{"sounds/scream", "b23f599401858f50b4395301b34b753e9a7e148faaa13263e0883578dc56c6ed"},
    DiskFile{"sounds/screech", "7889b9b6543fbd0784a40562fab91db2a8e75857a45e8d8c781975e351c3744f"},
    DiskFile{"sounds/splash", "f71e6218b4bd586c91053bc0ae5d8a6592ecb9ab89a256e36bbe3c1bebefe3f2"},
    DiskFile{"maps/a.map", "dc23457f290a15076d07e6acf1eda5e1c25d5946a2f1c284bd7472979e148196"},
    DiskFile{"maps/b.map", "a291d9064871e91549c9dd368205826a431839e28f21f667936c73e88cdb7923"},
    DiskFile{"maps/c.map", "c7ebda40e4652ad5ac9a3134ed1136feaef7c7693365c0bb9ecf25583fe5057e"},
    DiskFile{"maps/d.map", "dca3fd46f21d05b6d5210d69e1cd85e8d40ec5e978c6238c80023fad490b5c7a"},
    DiskFile{"maps/e.map", "57648b6e965270dfe3bee014b2b80a74567f2de633f8092eb99cf0da036e07b2"},
    DiskFile{"maps/f.map", "7a2e189e2edf10cbf8e8516d183327196e5925541101ce7fcc9054da7cef3076"},
    DiskFile{"maps/g.map", "623459868a10e9b3da9578aeb7c9819d04c3bdd1f945a7d6b0e50359011426dd"},
    DiskFile{"maps/h.map", "18b68e01ce111e0455c9859bcd7af2514d5befcea9c54933799fc5dbec0e9dde"},
    DiskFile{"maps/i.map", "e335aebc75aa01f4338b4c5954df5a2fd9038dc8a1c16f1f443ec2917644143c"},
    DiskFile{"maps/j.map", "96ad8e515ba652af8a453e3d28d85533b0f8a7da02abe1ea527c87c3f3aef7a9"},
    DiskFile{"maps/k.map", "9cfef384ab2cb80ae528b96409a849ac89b43dba7a103faf6cd51d9b6fc961df"},
    DiskFile{"maps/l.map", "7593af72a7addac4ff7eba6262f67c554cb2e50f61123ae242087ae40b405962"},
    DiskFile{"maps/m.map", "1e61ca9fc168d4f5a2b483da833f8d514429762706c2dc47e79b9bdbc9e22a8b"},
    DiskFile{"maps/n.map", "a96cf46cecd22997304686f08b616fb5fe95ffa3e6621088dc40560f077283ec"},
    DiskFile{"maps/o.map", "d909d07dccd5a20660f6e44aec8d72cdf75259ea872f3139033ac370a11a5f7b"},
    DiskFile{"newarmyfont", "babd81282df3baba7764fd4a47ebcf9721cffd16e187204c6532df56a9669ab7"},
    DiskFile{"fonts/army/12", "5f4d6dc00b71075ed5c89c1e109ef8cb1aba31e55a41e2329dd08eba3db2def2"},
    DiskFile{"fonts/army.font", "0c19b65af8b048e590efeae8840c07f30fa17fc95adcadaaa85f3f783fed4b62"},
};

std::string disk_path(std::string_view path) {
    return path.starts_with("fonts/") ? std::string(path) : std::string(game_folder) + std::string(path);
}

} // namespace

std::string_view installed_name(std::string_view path) {
    if (path.ends_with("fonts/army/12")) {
        return "army-12";
    }
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const amiga::Volume volume(image);
    if (volume.name() != volume_name || volume.find(disk_path("wings")) == nullptr ||
        volume.find(disk_path("wofsongs")) == nullptr) {
        throw app::NotThisGame("This disk is not Wings of Fury.");
    }
    app::ImportResult result;
    auto wings = volume.read(disk_path("wings"));
    const auto wings_sha = platform::sha256(wings);
    for (const auto& release : releases) {
        if (release.wings_sha256 == wings_sha) {
            result.release = std::string(release.name);
        }
    }
    if (result.release.empty()) {
        throw std::runtime_error("This disk holds a different release of Wings of Fury, which is not supported yet.");
    }
    result.files.push_back({"wings", std::move(wings)});
    const auto add = [&](const DiskFile& file, const char* problem) {
        auto bytes = volume.read(disk_path(file.path));
        if (platform::sha256(bytes) != file.sha256) {
            throw std::runtime_error("The disk's " + std::string(file.path) + problem);
        }
        result.files.push_back({std::string(installed_name(file.path)), std::move(bytes)});
    };
    for (const auto& program : programs) {
        add(program, " belongs to a different release of Wings of Fury, which is not supported yet.");
    }
    for (const auto& file : data_files) {
        add(file, " is damaged or from a different release.");
    }
    result.files.push_back({"highscore", volume.read(disk_path("highscore"))});
    return result;
}

} // namespace wings::import
