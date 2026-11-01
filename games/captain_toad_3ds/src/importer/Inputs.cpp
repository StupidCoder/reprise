// See Inputs.hpp. The paths below are the ROM's own file names (data literals).
#include "Inputs.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace captain_toad {

namespace {
// SHA-256 of the decompressed code segment and of every RomFS file of the supported release
// (Europe, program 00040000001CB200), sorted by path.
const char* const kCodeDigest = "f0b29dabd93125f1427a46d0918a6ba8ce1ebe6bef8498613a49c7e50cb42edd";
const struct Digest {
    const char* path;
    const char* sha256;
} kDigests[] = {
#include "RomDigests.inc"
};

const char* const kMismatch = "The ROM contents do not match the supported European release.";
}   // namespace

const char* expectedDigest(const std::string& path) {
    if (path == "exefs/code.bin") {
        return kCodeDigest;
    }
    const auto* end = std::end(kDigests);
    const auto* it = std::lower_bound(std::begin(kDigests), end, path,
                                      [](const Digest& d, const std::string& p) { return p.compare(d.path) > 0; });
    return it != end && path == it->path ? it->sha256 : nullptr;
}

std::vector<std::string> romfsPaths() {
    std::vector<std::string> paths;
    for (const auto& d : kDigests) {
        paths.push_back(d.path);
    }
    return paths;
}

Inputs::Inputs(RomImage& rom)
    : readCode_([&rom] { return rom.exefsFile(".code"); }),
      readFile_([&rom](const std::string& path) { return rom.romfsFile(path); }) {}

Inputs::Inputs(const std::filesystem::path& extracted)
    : readCode_([extracted] { return readFile(extracted / "exefs" / "code.bin"); }),
      readFile_([extracted](const std::string& path) { return readFile(extracted / "romfs" / path); }) {}

const Bytes& Inputs::code() {
    if (code_.empty()) {
        Bytes c = readCode_();
        if (sha256Hex(c) != expectedDigest("exefs/code.bin")) {
            throw ImportError(kMismatch);
        }
        code_ = std::move(c);
        if (onFile) {
            onFile("exefs/code.bin");
        }
    }
    return code_;
}

const Bytes& Inputs::romfs(const std::string& path) {
    auto it = files_.find(path);
    if (it != files_.end()) {
        return it->second;
    }
    const char* digest = expectedDigest(path);
    if (!digest) {
        throw ImportError("Internal error: unlisted ROM input " + path);
    }
    Bytes data = readFile_(path);
    if (sha256Hex(data) != digest) {
        throw ImportError(kMismatch);
    }
    if (onFile) {
        onFile(path);
    }
    return files_.emplace(path, std::move(data)).first->second;
}

Bytes Inputs::copy(const std::string& path) {
    const char* digest = expectedDigest(path);
    if (!digest) {
        throw ImportError("Internal error: unlisted ROM input " + path);
    }
    Bytes data = readFile_(path);
    if (sha256Hex(data) != digest) {
        throw ImportError(kMismatch);
    }
    return data;
}

bool Inputs::has(const std::string& path) {
    return expectedDigest(path) != nullptr;
}

const std::vector<SarcMember>& Inputs::archive(const std::string& path) {
    auto it = archives_.find(path);
    if (it != archives_.end()) {
        return it->second;
    }
    return archives_.emplace(path, sarc(yaz0(romfs(path)))).first->second;
}

const Bytes& Inputs::file(const std::string& archivePath, const std::string& name) {
    return member(archive(archivePath), name);
}

json::Value Inputs::sources() const {
    json::Value list = json::Value::array();
    auto add = [&](const std::string& path) {
        json::Value s = json::Value::object();
        s["path"] = "rom:" + path;
        s["sha256"] = expectedDigest(path);
        list.push(s);
    };
    if (!code_.empty()) {
        add("exefs/code.bin");
    }
    for (const auto& f : files_) {
        add(f.first);
    }
    return list;
}

}   // namespace captain_toad
