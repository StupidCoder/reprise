#include "import.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>

namespace beacon::reprise {
namespace {
constexpr std::string_view magic{"BEACON-ASSETS-1\n\0", 16};
constexpr std::string_view identity = "b8198d82c595d41f092424cc14234bf1b26e82c0427970fe0c70ac5876af2d9c";
struct Reader {
    platform::FileSource& file;
    std::uint64_t offset{};
    std::vector<std::uint8_t> read(std::size_t n) {
        if (offset > file.size() || n > file.size() - offset)
            throw std::runtime_error("The Beacon asset bundle is truncated.");
        std::vector<std::uint8_t> bytes(n);
        file.read(offset, bytes); offset += n; return bytes;
    }
    std::uint64_t number(int n) {
        const auto b = read(n); std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= std::uint64_t(b[i]) << (8 * i);
        return v;
    }
};
bool safe_path(const std::string& s) {
    if (s.empty() || s.size() > 512 || s.front() == '/' || s.back() == '/') return false;
    if (s != "sce_sys/param.sfo" && !s.starts_with("data/")) return false;
    std::size_t at = 0;
    while (at < s.size()) {
        const auto end = s.find('/', at);
        const auto part = s.substr(at, end == std::string::npos ? end : end-at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        for (unsigned char c : part) if (c < 32 || c > 126 || std::string_view("\\:*?\"<>|").find(c) != std::string_view::npos) return false;
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return true;
}
std::string hex(const std::vector<std::uint8_t>& b) {
    std::string s; for (auto v : b) { s += "0123456789abcdef"[v >> 4]; s += "0123456789abcdef"[v & 15]; } return s;
}
}
std::string import_assets(platform::FileSource& file, const std::filesystem::path& folder,
                          const app::ImportProgress& progress) {
    Reader r{file};
    if (file.size() < 20) throw app::NotThisGame("Not a Beacon asset bundle.");
    auto header = r.read(16);
    if (!std::equal(header.begin(), header.end(), magic.begin())) {
        if (header[0] == 0x7f && header[1] == 'C' && header[2] == 'N' && header[3] == 'T')
            {
            std::array<std::uint8_t,48> id{};
            if(file.size()<0x70) throw app::NotThisGame("Not this game's package");
            file.read(0x40,id);
            if(std::string_view(reinterpret_cast<const char*>(id.data()),std::string_view("EP9000-CUSA12392_00-PLATFORMERVR00EU").size())!="EP9000-CUSA12392_00-PLATFORMERVR00EU")
                throw app::NotThisGame("Not this game's package");
            throw std::runtime_error("Direct PS4 package import is not connected yet. Export Beacon's extracted game folder with export_assets.py, then import the .beacon file.");
        }
        throw app::NotThisGame("Not a Beacon asset bundle.");
    }
    if (file.size() > (2ull << 30)) throw std::runtime_error("Beacon viewer bundle exceeds 2 GiB.");
    const auto count = r.number(4);
    if (!count || count > 12000) throw std::runtime_error("Invalid Beacon asset count.");
    std::set<std::string> names;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto length = r.number(2), size = r.number(8);
        const auto digest = hex(r.read(32));
        if (!length || length > 512 || size > (64u << 20)) throw std::runtime_error("Invalid Beacon asset size.");
        auto path = r.read(length);
        std::string name(path.begin(), path.end());
        if (!safe_path(name)) throw std::runtime_error("Unsafe path in Beacon asset bundle.");
        std::string folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (!names.insert(folded).second) throw std::runtime_error("Duplicate path in Beacon asset bundle.");
        if (i == 0 && name != "sce_sys/param.sfo") throw std::runtime_error("Beacon release identity must be first.");
        if (name != "sce_sys/param.sfo") {
            const auto ext = std::filesystem::path(name).extension().string();
            if (ext != ".jxm" && ext != ".gnf" && ext != ".skel" && ext != ".anim" && !(ext == ".sxd" && std::filesystem::path(name).stem().string().ends_with("_bgm")))
                throw std::runtime_error("Unsupported file in Beacon viewer bundle.");
        }
        auto bytes = r.read(size);
        if (platform::sha256(bytes) != digest) throw std::runtime_error("Damaged Beacon asset: " + name);
        if (i == 0 && digest != identity) throw std::runtime_error("Unsupported Beacon release; expected the supplied CUSA12392 merged package.");
        const auto dest = folder / name;
        std::filesystem::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("Could not write Beacon asset: " + name);
        if (progress) progress("Importing character assets", double(r.offset) / double(file.size()));
    }
    if (r.offset != file.size()) throw std::runtime_error("Unexpected trailing data in Beacon bundle.");
    for (const auto* required : {"data/multi_platformer/gfx/mup_hero.jxm", "data/multi_platformer/anim/mup_hero.skel"})
        if (!names.contains(required)) throw std::runtime_error("The Beacon viewer bundle is missing the hero model or skeleton.");
    return "CUSA12392 — Beacon character assets (merged package 1.04; metadata 01.03)";
}
}
