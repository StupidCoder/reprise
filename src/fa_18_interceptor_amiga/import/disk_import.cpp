#include "fa_18_interceptor_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <string>
#include <string_view>

namespace interceptor::import {
namespace {

constexpr std::string_view volume_name = "F-18 Interceptor";
constexpr std::string_view program_path = "F-18 Interceptor";

struct Release {
    std::string_view name;
    std::string_view program_sha256;
};

// "FA-18 Interceptor (1988)(Electronic Arts)[cr A-Ha].adf", SHA-256
// e28e9469…6f93bfa2. A cracked copy: the program's security check screen
// shows the crackers' line ($1342AC). The data files look untouched.
constexpr std::array releases = {
    Release{"Electronic Arts (1988), cracked copy",
            "d7301e20958548f9b5f1ba744f8ff91fef476306c220eb886e7c90bac2802ffa"},
};

struct DiskFile {
    std::string_view path;
    std::string_view sha256;
};

constexpr std::array data_files = {
    DiskFile{"pix/splsh", "d64e541d3e91808dd10a9e934216f1f28601f7e2c7c7a8db8b9d4845a47e1c70"},
    DiskFile{"pix/inst5", "870329e081934311588fe90d28897a82c775a39e22f6f4525f163502a008889c"},
    DiskFile{"pix/frnt5", "7cf5300d75a10f0f7727506a91f2eb46968a625fdbbc9f13e13c88b26a52d012"},
    DiskFile{"text/text201", "f1ea05f80960ff2d7006fd6210f51dde104c78d1f10c4ecbb2776687e931f47f"},
    DiskFile{"text/textcpt", "ca7a3d917c8c4a7b9145f8c98bcbee3741127a04470c481eb586f1d94e668ad7"},
    DiskFile{"text/textegn", "894561be833fb4e4c03a4461574a87d7d4f5c9a54d6a5cafab8807f4bdfa97a0"},
    DiskFile{"text/textegn2", "4cbd5a5cd77d55290916afe8b6924f21f5c147e906e39052106fd346844f3fe8"},
    DiskFile{"text/textger", "ab4352391a8f282bc93e9a6b1ae267e27d253b545c4e5daab99992484d7166b8"},
    DiskFile{"text/texti0a", "e22ee6afadb6c7825456db3a8ea2640a7ddfae796779298a45b86b9b0f282565"},
    DiskFile{"text/texti0b", "775bc74d2647b2ea8445ece027d199d926b0b484f2be89747aaf5241cc93816b"},
    DiskFile{"text/texti1", "4cb22150b61d37935cb3a0c7ad0688b3a82fbc2cd59ffe143fa4e24290223a1e"},
    DiskFile{"text/texti2", "9b90b44e6b185554f19b33cc3a6d60f34f4d984578f978d92b7a10c10a89f6a2"},
    DiskFile{"text/texti3", "7e1cb735642c0f75c3fdc35625e05fe4590ed05539241f686f20e161f4126777"},
    DiskFile{"text/texti4", "bdbfc8d95be0694f900f693148170e800d62139a38db244566415a5dc074653c"},
    DiskFile{"text/texti5", "8e97c62afa76e4ae66c6f2e631d2efb49e774f45213d996349d28f880aeb5b1b"},
    DiskFile{"text/texttre", "86e1ce8814647948d4a6b245a46eb0eab35bf73cf68bf2c6ad9041219e0c7c0a"},
    DiskFile{"text/textwnd", "518bf5f40c1228eb6ea1b9b98ac00836682682ffad0d4d49e1c0ca5ab213111f"},
    DiskFile{"text/textply", "a4c8b947d4ba0efa71d260f17045b3496fffb340909c5df133136c40a44a6745"},
    DiskFile{"text/textctl", "0b7b6fdcf833113fc2ca02a89263a23b7eb890f414bb724414a15f143f5c2003"},
    DiskFile{"config", "f268dc8e925a020dadce6bae12b6158fed9020ec6d82729bbe816a0cd7d3d063"},
};

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const amiga::Volume volume(image);
    if (volume.name() != volume_name || volume.find(program_path) == nullptr) {
        throw app::NotThisGame("This disk is not F/A-18 Interceptor.");
    }
    app::ImportResult result;
    auto program = volume.read(program_path);
    const auto program_sha = platform::sha256(program);
    for (const auto& release : releases) {
        if (release.program_sha256 == program_sha) result.release = std::string(release.name);
    }
    if (result.release.empty()) {
        throw std::runtime_error("This disk holds a different release of F/A-18 Interceptor, which is not supported yet.");
    }
    result.files.push_back({"program", std::move(program)});
    for (const auto& file : data_files) {
        if (volume.find(file.path) == nullptr) {
            throw std::runtime_error("The disk has no " + std::string(file.path) + "; it may be damaged.");
        }
        auto bytes = volume.read(file.path);
        if (platform::sha256(bytes) != file.sha256) {
            throw std::runtime_error("The disk's " + std::string(file.path) +
                                     " is damaged or from a different release.");
        }
        result.files.push_back({std::string(file.path), std::move(bytes)});
    }
    return result;
}

} // namespace interceptor::import
