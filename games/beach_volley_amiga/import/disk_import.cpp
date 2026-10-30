#include "beach_volley_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "amiga/backward_cruncher.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <stdexcept>
#include <string_view>

namespace beach_volley::import {
namespace {

constexpr std::string_view volume_name = "Beach Volley";

// "Beach_Volley_1989_Ocean_cr.adf", SHA-256 b5c1ce38…41156a6f27a: a cracked
// copy (the cracker's installer archive sits beside the game's files, which
// look untouched).
constexpr std::string_view release_name = "Ocean (1989), cracked copy";
constexpr std::string_view volley_code_sha256 = "e85f65b79d796a14d261b3597740d0fc697fe2864521e81c28da98bff9cc3e31";

struct GameFile {
    std::string_view disk_name;
    std::string_view name; // as installed
    bool crunched;
    std::string_view sha256; // of the installed bytes
};

constexpr std::array<GameFile, 52> game_files = {{
    {"intro.code", "intro.code", false,
     "6b476b2a854131bfe8845ba0609c7eee5992482dc29554b66f336d453ea7e9ea"},
    {"volley.code", "volley.code", false,
     "e85f65b79d796a14d261b3597740d0fc697fe2864521e81c28da98bff9cc3e31"},
    {"animation.code", "animation.code", false,
     "1c516687e47ad73175ab0dc9d046280ff20ca3990248f21c22225db6ef3746fb"},
    {"score.code", "score.code", false,
     "9b0a22b967abefcff08ddbcaf842dfd1d663093b329203a6c64c4f703c81be5d"},
    {"final.code", "final.code", false,
     "75b73466921db9e7ee25b0ca9a8b311e117bef50e5e40b9d85d47192982ad9bb"},
    {"instruments", "instruments", false,
     "e9a31f65c279c4c95d52cb79563a851e3cc7125dc89ff3e1981de2957a3473d2"},
    {"anim0.cru", "anim0", true,
     "7fd7ec1e9b30229756974a173193c0360cd62b38e78f6d7c79de6602c7f02d7d"},
    {"anim1.cru", "anim1", true,
     "cb7384cd17916e8beadafb10d7243d63f2c8551697192dbec739bcc7b23e9c33"},
    {"anim2.cru", "anim2", true,
     "2b60cfccae20c2d6c9a339d2c3b754ba2aa5c57476f47f80541e5ed83a5eea95"},
    {"anim3.cru", "anim3", true,
     "0d9c21584d6229511db60b6a260dad97f8a7e97a51bb6cbd807292b5832b7321"},
    {"anim4.cru", "anim4", true,
     "da8cea232adcc2598c295e26d3172a2c73bd9f2f21111bd7ad4d048ee41cfd64"},
    {"anim5.cru", "anim5", true,
     "cd7001f4dabafa05589a9cb84c09b250ff1ce815075c284aa1a124df49afa11e"},
    {"anim6.cru", "anim6", true,
     "560df46c2a4a3b437343aadeed1a951a71309b602bbb8b13936baf60e5eab4bb"},
    {"anim7.cru", "anim7", true,
     "a18cc923cb2326b3a325cdc36adb031dc1791fd2278ef05b1dceee2f33b21f8d"},
    {"animation1.cru", "animation1", true,
     "3fb6029bdf643fc8230ea926466f0048d2271e0982ca68c3e710237f20c0f2e4"},
    {"animation2.cru", "animation2", true,
     "7b16f4c7655347e8e06cea3501e04a2d3fe8488d06f6cb1970d4f59675192b39"},
    {"carte.cru", "carte", true,
     "44f827bd65f857a4e36e9163a248bbdd3f0616682f2e328c5cea030a5aa9b70e"},
    {"dnv0", "dnv0", true,
     "4f8470ef5d022a9e9b09323652e6f855c8addd35d9f1d63d218c80e34279a83b"},
    {"dnv1", "dnv1", true,
     "492fa0a383290c78a97badf89ce6a4c00662f9abf302bdfc8d99e14b2a0434da"},
    {"dnv2", "dnv2", true,
     "fec33e30f08a84a7d6f1f44869457f01085b274f9fe34f54e3c3572bc561bed1"},
    {"dnv3", "dnv3", true,
     "14365e900b72d82b0a19900b54c8c7f6f3fc55b579b102350a6e75d6dd49da89"},
    {"dnv4", "dnv4", true,
     "df45cf62ce8238a116ac5c8879dda75a4aaedc60add35075b7c6e9a3604bedef"},
    {"dnv5", "dnv5", true,
     "fd0653401b999082ebb60d66bb215c6c968b3ddc19f916d5b3271002f69d61b1"},
    {"dnv6", "dnv6", true,
     "09e8d7cd9dd5b740fc1a5938725d1ba638622e00f08917de55b35a4643b5bd4e"},
    {"dnv7", "dnv7", true,
     "451ca31d3d248d74669c7af6932481173b5985b374210a5264c4c59c69a0a753"},
    {"final.cru", "final", true,
     "f8aea6540845bb26e901c87989d45434bd6ae86a4b4d29021a09df648c427c1e"},
    {"introp1.cru", "introp1", true,
     "3ddbb4e31e7e629fb4b5aa16aa69a656f954821265e48acb53aabd3fa5e55d4b"},
    {"introp2.cru", "introp2", true,
     "6c99b4dfee136a09836e72e4d2db0659de6cf8a6853cbab395edf5f68f7081d2"},
    {"music1.cru", "music1", true,
     "109638afbf298b2b9a408d82d7e83766f73ad4cc47334fb9a65c62d45bdbddc8"},
    {"music2.cru", "music2", true,
     "a630224b891da3114c48f65d95f91186b766a3c5bdcac99c90807c7003e62814"},
    {"music3.cru", "music3", true,
     "50ca2b3ade0b329bc566c2e59df199ed849845471db61d029d8dce40b40252d8"},
    {"music4.cru", "music4", true,
     "67dc473b776c6b29bf3cdf5c58e3ed630e65b4f82ebf7a048cd344f46eca9deb"},
    {"music5.cru", "music5", true,
     "00a0a8ff280328db849a4a28e517f46f538c32bd4510c7117d8c3009d732a857"},
    {"music6.cru", "music6", true,
     "a84c6b2bb5ad73314037267ec59d3357a4e9a299c5890542d133aea5087f9fc6"},
    {"music7.cru", "music7", true,
     "75c677e5f9bbcd0ac22e23e157ffa8a88da1dd509beb301ad374dcf9abe28196"},
    {"music8.cru", "music8", true,
     "3de5faa996c0b867159bddfc43cade1756063bddbbf3542aec79fb73be93658d"},
    {"music9.cru", "music9", true,
     "f42c1cceb52ca11d01ef6b4f60e2b8835e7db567d61ac5131afeb2b65e77bd6a"},
    {"music10.cru", "music10", true,
     "ddb722e6545d981ca116a6443dec32c201e5d38be9fac86fa58a7b9c0594caf0"},
    {"music11.cru", "music11", true,
     "abe41be33f5ea5e506e18e70d5df64af6ac12efd06735942b0fd3e190664f25a"},
    {"music12.cru", "music12", true,
     "2d33f74ca5478c0de3b00b57b46ed7eaac3bccd73fd59d7257818887a7cafe0a"},
    {"music13.cru", "music13", true,
     "dd2d184aa486960c334f4828753bbcf26335bfada07f614650f9ef3a9ef7194d"},
    {"musicfinal.cru", "musicfinal", true,
     "f5c7e715321dd45323a934b0f4b542d597853a874f34f18a9cdb5ba77fe691db"},
    {"score.cru", "score", true,
     "44e55941c2be873513e7441d1f2ac2018429c3278bf6c4b17e600cfa83f235c0"},
    {"terrain1.cru", "terrain1", true,
     "6b4b02e894d096a2cfe69123f6cf6feaa04ce6446cc2de44d8e35c953169f5ce"},
    {"terrain2.cru", "terrain2", true,
     "86a131a21164b962635649cdd3fe550ef34a8b3170bc8adc5b491566a7fe7b34"},
    {"terrain3.cru", "terrain3", true,
     "49e30ee19647efcfdcc0677c5e2e2a20cb2fb11d05ae70c4755acc57863a091e"},
    {"terrain4.cru", "terrain4", true,
     "45683898e8ad5e5bf0ff83b6ee3f3102d9a1fa8ab99ccc6449df9ba35b707a4e"},
    {"terrain5.cru", "terrain5", true,
     "a6dbe3555c3cc33a49fc581aa4b62b077810e74b181e841671a9ed85ea2069f2"},
    {"terrain6.cru", "terrain6", true,
     "85bf67f85f318ee2b8a04829c414b3ac32b02edc3c7d51fe6e529876841d332d"},
    {"terrain7.cru", "terrain7", true,
     "eca8a198d24776016f2b2fb4bc9ade4ce8972a7d702975561f5e07c723f492f3"},
    {"terrain8.cru", "terrain8", true,
     "932770718c0ba2cedc60d8345d2416c54054521a0f357fac07ff11e14d23db76"},
    {"trophe.cru", "trophe", true,
     "0869d8062b121d4e1883cecfbe0d59dad4da2e806b763a2cced743ce1c965b06"},
}};

// A crunched file is the bit stream followed by two longs, the checksum and
// the unpacked length; the stream's last long is the first one read
// (volley.code $31B9C: the decruncher is called with A0 = end of file).
std::vector<std::uint8_t> uncrunch(std::span<const std::uint8_t> file, std::string_view name) {
    constexpr std::size_t trailer = 8;
    if (file.size() < trailer + 4 || file.size() % 4 != 0) {
        throw std::runtime_error("The disk's file " + std::string(name) + " is not in the game's crunched form.");
    }
    const std::size_t unpacked = amiga::read_be32(file, file.size() - 4);
    if (unpacked > 0x80000) {
        throw std::runtime_error("The disk's file " + std::string(name) + " is damaged.");
    }
    return amiga::uncrunch_backward(file.first(file.size() - trailer), unpacked,
                                    amiga::read_be32(file, file.size() - trailer));
}

// The volume name as the root block (880) stores it, a BCPL string at +$1B0,
// checked before the file system is parsed: other games' disks often have no
// file system at all and must not be reported as a damaged Beach Volley.
bool named_beach_volley(std::span<const std::uint8_t> image) {
    if (image[0] != 'D' || image[1] != 'O' || image[2] != 'S') return false;
    const auto root = image.subspan(880 * amiga::block_size, amiga::block_size);
    const std::size_t length = root[0x1B0];
    return length == volume_name.size() &&
           std::string_view(reinterpret_cast<const char*>(root.data() + 0x1B1), length) == volume_name;
}

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    if (!named_beach_volley(image)) {
        throw app::NotThisGame("This disk is not Beach Volley.");
    }
    const amiga::Volume volume(image); // from here on, errors mean a damaged disk
    if (volume.name() != volume_name || volume.find("volley.code") == nullptr) {
        throw app::NotThisGame("This disk is not Beach Volley.");
    }
    if (platform::sha256(volume.read("volley.code")) != volley_code_sha256) {
        throw std::runtime_error("This disk holds a different release of Beach Volley, which is not supported yet.");
    }
    app::ImportResult result;
    result.release = std::string(release_name);
    for (const auto& file : game_files) {
        if (volume.find(file.disk_name) == nullptr) {
            throw std::runtime_error("The disk's file " + std::string(file.disk_name) + " is missing.");
        }
        const auto stored = volume.read(file.disk_name);
        auto bytes = file.crunched ? uncrunch(stored, file.disk_name) : stored;
        if (platform::sha256(bytes) != file.sha256) {
            throw std::runtime_error("The disk's file " + std::string(file.disk_name) +
                                     " is damaged or from a different release.");
        }
        result.files.push_back({std::string(file.name), std::move(bytes)});
    }
    return result;
}

} // namespace beach_volley::import
