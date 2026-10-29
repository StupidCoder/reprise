// The 3DS adventures' sound archive. See a56_sound_archive.hpp.
#include "sound/a56_sound_archive.hpp"
#include <cstring>
#include <stdexcept>

namespace picarat::adventure56 {

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
    check(at + 4 <= b.size(), "sound file too short");
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 |
           std::uint32_t(b[at + 3]) << 24;
}

std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t at) {
    check(at + 2 <= b.size(), "sound file too short");
    return std::uint16_t(b[at] | b[at + 1] << 8);
}

// A NintendoWare file's block of `type` (0 when missing).
std::size_t block(std::span<const std::uint8_t> b, std::uint16_t type) {
    for (unsigned i = 0, n = u16(b, 0x10); i < n; ++i) {
        if (u16(b, 0x14 + i * 12) == type) {
            return u32(b, 0x14 + i * 12 + 4);
        }
    }
    return 0;
}

// Item ids carry their kind in the top byte.
std::uint32_t itemIndex(std::uint32_t id) {
    return id & 0xffffff;
}

// The file table's entries: 0x220c in the archive, 0x220d external (a path).
constexpr std::uint16_t kInternalFile = 0x220c, kExternalFile = 0x220d;
constexpr std::uint16_t kSequenceSound = 0x2203;

enum Table { Sounds = 0, Banks = 2, WaveArchives = 3, Files = 6 };

} // namespace

SoundArchive::SoundArchive(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    const auto& b = bytes_;
    auto u32 = [&](std::size_t at) {
        check(at + 4 <= b.size(), "BCSAR too short");
        return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 |
               std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
    };
    auto u16 = [&](std::size_t at) {
        check(at + 2 <= b.size(), "BCSAR too short");
        return std::uint16_t(b[at] | b[at + 1] << 8);
    };
    check(b.size() >= 0x40 && std::memcmp(b.data(), "CSAR", 4) == 0, "not a BCSAR archive");
    std::size_t strg = 0, info = 0, fileBlock = 0;
    const unsigned blocks = u32(0x10);
    for (unsigned i = 0; i < blocks; ++i) {
        const std::uint16_t type = u16(0x14 + i * 12);
        const std::size_t offset = u32(0x14 + i * 12 + 4);
        if (type == 0x2000) {
            strg = offset;
        } else if (type == 0x2001) {
            info = offset;
        } else if (type == 0x2002) {
            fileBlock = offset;
        }
    }
    check(strg && info, "BCSAR without names or info");
    // Names: the string table after the block's 8-byte header; offsets
    // count from 16 bytes past the table reference's base.
    const std::size_t st = strg + 8;
    const std::size_t table = st + u32(st + 4);
    const unsigned strings = u32(table);
    std::vector<std::string> names(strings);
    for (unsigned i = 0; i < strings; ++i) {
        const std::size_t off = u32(table + 4 + i * 12 + 4), size = u32(table + 4 + i * 12 + 8);
        const std::size_t at = st + 16 + off;
        check(at + size <= b.size(), "BCSAR name outside the file");
        names[i] = std::string(reinterpret_cast<const char*>(b.data() + at),
                               strnlen(reinterpret_cast<const char*>(b.data() + at), size));
    }
    // Sounds: the first INFO table.
    const std::size_t ib = info + 8;
    infoBase_ = ib;
    fileBlock_ = fileBlock;
    const std::size_t soundTable = ib + u32(ib + 4);
    const unsigned count = u32(soundTable);
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t e = soundTable + u32(soundTable + 4 + i * 8 + 4);
        Sound s;
        s.index = int(i);
        s.file = int(u32(e));
        s.volume = b[e + 8];
        s.kind = u16(e + 0x0c);
        s.info = e;
        const std::uint32_t flags = u32(e + 0x14);
        const std::uint32_t nameId = (flags & 1) ? u32(e + 0x18) : i;
        if (nameId < names.size() && !names[nameId].empty()) {
            sounds_[names[nameId]] = s;
        }
    }
}

SoundArchive::~SoundArchive() = default;

int SoundArchive::volume(const std::string& name) const {
    const auto it = sounds_.find(name);
    return it == sounds_.end() ? -1 : it->second.volume;
}

// An entry of one of INFO's tables.
static std::size_t tableEntry(std::span<const std::uint8_t> b, std::size_t infoBase, int table,
                              std::uint32_t i) {
    const std::size_t t = infoBase + u32(b, infoBase + std::size_t(table) * 8 + 4);
    check(i < u32(b, t), "sound archive item out of range");
    return t + u32(b, t + 4 + std::size_t(i) * 8 + 4);
}

std::string SoundArchive::externalFile(const std::string& name) const {
    const auto it = sounds_.find(name);
    if (it == sounds_.end()) {
        return {};
    }
    const std::span<const std::uint8_t> b = bytes_;
    const std::size_t e = tableEntry(b, infoBase_, Files, std::uint32_t(it->second.file));
    if (u16(b, e) != kExternalFile) {
        return {};
    }
    const std::size_t at = e + u32(b, e + 4);
    check(at < b.size(), "sound archive path outside the archive");
    return std::string(reinterpret_cast<const char*>(b.data() + at),
                       strnlen(reinterpret_cast<const char*>(b.data() + at), b.size() - at));
}

std::span<const std::uint8_t> SoundArchive::file(std::uint32_t id) const {
    const std::span<const std::uint8_t> b = bytes_;
    const std::size_t e = tableEntry(b, infoBase_, Files, id);
    if (u16(b, e) != kInternalFile) {
        return {};
    }
    const std::size_t at = e + u32(b, e + 4);
    const std::size_t from = fileBlock_ + 8 + u32(b, at + 4), size = u32(b, at + 8);
    check(from + size <= b.size(), "sound archive file outside the archive");
    return b.subspan(from, size);
}

std::shared_ptr<const std::vector<std::uint8_t>> SoundArchive::sequenceData(std::uint32_t id) {
    if (const auto it = sequences_.find(id); it != sequences_.end()) {
        return it->second;
    }
    const auto f = file(id);
    check(f.size() >= 0x20 && std::memcmp(f.data(), "CSEQ", 4) == 0, "not a BCSEQ sequence");
    const std::size_t data = block(f, 0x5000);
    check(data != 0, "BCSEQ without DATA");
    const std::size_t size = u32(f, data + 4);
    check(data + size <= f.size() && size >= 8, "BCSEQ DATA outside the file");
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(
        f.begin() + std::ptrdiff_t(data + 8), f.begin() + std::ptrdiff_t(data + size));
    sequences_[id] = bytes;
    return bytes;
}

std::shared_ptr<const Pcm> SoundArchive::wave(std::uint32_t archive, std::uint32_t index) {
    const auto key = std::pair(archive, index);
    if (const auto it = waves_.find(key); it != waves_.end()) {
        return it->second;
    }
    const std::span<const std::uint8_t> b = bytes_;
    const auto f = file(u32(b, tableEntry(b, infoBase_, WaveArchives, archive)));
    check(f.size() >= 0x20 && std::memcmp(f.data(), "CWAR", 4) == 0, "not a BCWAR wave archive");
    const std::size_t info = block(f, 0x6800), data = block(f, 0x6801);
    check(info && data, "BCWAR without INFO or FILE");
    check(index < u32(f, info + 8), "wave out of range");
    const std::size_t entry = info + 12 + std::size_t(index) * 12;
    const std::size_t from = data + 8 + u32(f, entry + 4), size = u32(f, entry + 8);
    check(from + size <= f.size(), "wave outside its archive");
    auto pcm = std::make_shared<const Pcm>(decodeWave(f.subspan(from, size)));
    waves_[key] = pcm;
    return pcm;
}

const std::vector<Instrument>* SoundArchive::bank(std::uint32_t index) {
    if (const auto it = banks_.find(index); it != banks_.end()) {
        return &it->second;
    }
    const std::span<const std::uint8_t> b = bytes_;
    const auto f = file(u32(b, tableEntry(b, infoBase_, Banks, index)));
    check(f.size() >= 0x20 && std::memcmp(f.data(), "CBNK", 4) == 0, "not a BCBNK bank");
    const std::size_t info = block(f, 0x5800);
    check(info != 0, "BCBNK without INFO");
    const std::size_t base = info + 8;
    const std::size_t waveList = base + u32(f, base + 4), programs = base + u32(f, base + 12);
    std::vector<Instrument> out(u32(f, programs));
    for (std::size_t i = 0; i < out.size(); ++i) {
        // Follow the region tree down to a velocity region; tables of key
        // or velocity ranges (not used by the effects) take their first
        // region.
        std::size_t ref = programs + 4 + i * 8, refBase = programs;
        std::size_t region = 0;
        for (int depth = 0; depth < 8 && !region; ++depth) {
            const std::uint16_t type = u16(f, ref);
            const std::size_t at = refBase + u32(f, ref + 4);
            switch (type) {
            case 0x5900: // instrument
            case 0x5901: // key region
            case 0x6000: // direct: one reference counted from itself
                ref = refBase = at;
                break;
            case 0x6001: // ranges: count, borders, aligned references (unused)
                ref = (at + 4 + u32(f, at) + 3) & ~std::size_t(3);
                refBase = at;
                break;
            case 0x6002: // index table: min, max, references
                ref = at + 4;
                refBase = at;
                break;
            case 0x5902:
                region = at;
                break;
            default:
                depth = 8; // empty program (0x5903)
                break;
            }
        }
        if (!region) {
            continue;
        }
        Instrument& in = out[i];
        const std::uint32_t waveIndex = u32(f, region), flags = u32(f, region + 4);
        std::size_t p = region + 8;
        for (int bit = 0; bit < 32; ++bit) {
            if (!(flags >> bit & 1)) {
                continue;
            }
            const std::uint32_t v = u32(f, p);
            switch (bit) {
            case 0:
                in.originalKey = int(v & 0x7f);
                break;
            case 1:
                in.volume = int(v & 0xff);
                break;
            case 2:
                in.pan = int(v & 0xff);
                break;
            case 3: {
                float pitch;
                std::memcpy(&pitch, &v, 4);
                in.pitch = pitch > 0 && pitch < 16 ? pitch : 1.0f;
                break;
            }
            case 9: { // a reference (counted from the region) to the envelope
                const std::size_t env = region + v;
                const std::size_t e = env + u32(f, env + 4);
                in.attack = f[e];
                in.decay = f[e + 1];
                in.sustain = f[e + 2];
                in.hold = f[e + 3];
                in.release = f[e + 4];
                break;
            }
            default:
                break;
            }
            p += 4;
        }
        if (waveIndex < u32(f, waveList)) {
            const std::size_t w = waveList + 4 + std::size_t(waveIndex) * 8;
            in.wave = wave(itemIndex(u32(f, w)), u32(f, w + 4));
        }
    }
    return &(banks_[index] = std::move(out));
}

std::unique_ptr<SequencePlayer> SoundArchive::openSequence(const std::string& name) {
    const auto it = sounds_.find(name);
    if (it == sounds_.end() || it->second.kind != kSequenceSound) {
        return nullptr;
    }
    try {
        const std::span<const std::uint8_t> b = bytes_;
        const std::size_t e = it->second.info;
        const std::size_t detail = e + u32(b, e + 0x10);
        const std::size_t banks = detail + u32(b, detail + 4);
        const std::size_t start = u32(b, detail + 0x10);
        const std::vector<Instrument>* instruments =
            u32(b, banks) ? bank(itemIndex(u32(b, banks + 4))) : nullptr;
        auto data = sequenceData(std::uint32_t(it->second.file));
        if (start >= data->size()) {
            return nullptr;
        }
        return std::make_unique<SequencePlayer>(
            std::move(data), start, instruments ? *instruments : std::vector<Instrument>{});
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace picarat::adventure56
