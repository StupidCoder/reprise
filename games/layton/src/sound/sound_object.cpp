// Recovered overlay-2 sound object. See sound_object.hpp; every function names
// its overlay-2 entry point. Float arithmetic uses host IEEE single precision:
// the SDK soft-float routines it replaces (0x0203afd4 multiply, 0x0203aa94
// divide, 0x0203af44 int->float, 0x0203aed0/0x0203af04 truncating
// conversions) matched host results in 1,000,003 checks (local research harness).
#include "sound/sound_object.hpp"
#include "system/resources.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace picarat {
namespace {
std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(resolvePath(path), std::ios::binary);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

std::int32_t f2iz(float v) { // 0x0203aed0
    if (!(v > -2147483648.0f)) {
        return INT32_MIN;
    }
    if (!(v < 2147483648.0f)) {
        return INT32_MAX;
    }
    return std::int32_t(v);
}

std::uint32_t f2uiz(float v) { // 0x0203af04 (negative and NaN -> 0)
    if (!(v > 0.0f)) {
        return 0;
    }
    if (!(v < 4294967296.0f)) {
        return 0xffffffffu;
    }
    return std::uint32_t(v);
}

// Binary search shared by 0x02084728 (room table, 8-byte rows) and 0x0207d30c
// (time table, 4-byte rows): keys are u16 at the row start.
const std::uint8_t* tableFind(const std::vector<std::uint8_t>& table, std::uint16_t count,
                              std::uint16_t step, unsigned row, std::uint32_t key) {
    if (table.empty() || count == 0) {
        return nullptr;
    }
    const std::uint8_t* rows = table.data();
    if (key > le16(rows + (count - 1) * row)) {
        return nullptr;
    }
    int found = -1, low = 0;
    for (int span = step; span > 0;) {
        span >>= 1;
        int middle = low + span;
        if (middle < count) {
            std::int32_t delta = std::int32_t(key) - le16(rows + middle * row);
            if (delta == 0) {
                found = middle;
            } else if (delta > 0) {
                low = middle;
            }
        }
    }
    return found < 0 ? nullptr : rows + found * row;
}

std::uint16_t searchStep(std::uint16_t count) { // power of two >= count, as the loaders compute it
    std::uint16_t step = 1;
    while (step < count) {
        step = std::uint16_t(step << 1);
    }
    return step;
}

std::string romPath(const std::string& pattern, int value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, pattern.c_str(), value);
    return buffer;
}
} // namespace

int effectGroup(int id) { // 0x0207f850
    if (id < 0) {
        return -1;
    }
    if (id <= 0x31) {
        return 0;
    }
    if (id <= 199) {
        return 1;
    }
    return id < 1000 ? 2 : -1;
}

// Two-ended pool: front allocations 0x0207d4a8, back 0x0207d508, rounded to 4.
struct SoundObject::Pool {
    std::vector<std::uint8_t> memory;
    std::uint32_t front = 0, back = 0;

    explicit Pool(std::uint32_t size) : memory(size & ~3u, 0) {}

    std::uint32_t capacity() const { return std::uint32_t(memory.size()); }

    std::uint8_t* allocate(std::uint32_t size, bool fromBack) {
        if (memory.empty() || capacity() < size) {
            return nullptr;
        }
        std::uint32_t rounded = (size + 3) & ~3u;
        if (!rounded || rounded + front + back > capacity()) {
            return nullptr;
        }
        if (fromBack) {
            back += rounded;
            return memory.data() + capacity() - back;
        }
        std::uint8_t* at = memory.data() + front;
        front += rounded;
        return at;
    }

    std::uint8_t* current(bool fromBack) {
        return fromBack ? memory.data() + capacity() - back : memory.data() + front;
    } // 0x0207d658

    void reset(bool fromBack) { (fromBack ? back : front) = 0; } // 0x0207d5f4
};

// Owner layout +0x00..+0x6f shared by effect groups and the BGM object.
struct SoundObject::Bank {
    std::int32_t dataHandle = -0x40;    // +0x0c SED/SMD wrapper handle
    std::int32_t programHandle = -0x40; // +0x1c SWD wrapper handle
    std::int32_t mainHandle = -0x40;    // main-bank wrapper (+0x20 object, +0x0c handle)
    bool mainOpen = false;
    Pool* pool = nullptr;               // +0x24
    bool back = false;                  // +0x28
    std::string bankFormat, dataFormat; // +0x29 / +0x49
    std::string mainPath;               // the main sample bank (number 999 in the second game)
    std::int16_t current = 0;           // +0x6a
    std::uint32_t flags = 0;            // +0x70
};

struct SoundObject::Group : Bank {};

struct SoundObject::Bgm : Bank {
    std::int16_t pending = -1;                 // +0x6c
    float requested = 0;                       // +0x78 last requested volume
    float startVolume = 0, startSeconds = 0;   // +0x7c/+0x80 (flag 0x40)
    float fadeAux = 0, fadeSeconds = 0;        // +0x84/+0x88 (flag 0x80)
    float resumeVolume = 0, resumeSeconds = 0; // +0x8c/+0x90 (flag 0x100)
    float volume = 0, volumeSeconds = 0;       // +0x94/+0x98 (flag 0x200)
    float tempo = 0, tempoSeconds = 0;         // +0x9c/+0xa0 (flag 0x400)
};

namespace {
// Main-bank FS reader for 0x02002354 / 0x02002814 (file kept open while the bank is open).
struct DiskBank final : dse::BankFile {
    std::ifstream file;

    explicit DiskBank(const std::filesystem::path& path)
        : file(resolvePath(path), std::ios::binary) {}

    bool read(std::uint32_t offset, std::uint8_t* destination, std::uint32_t length) override {
        file.clear();
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(destination), length);
        std::streamsize got = file.gcount();
        if (got < std::streamsize(length)) {
            std::memset(destination + got, 0, length - std::size_t(got));
        }
        return got == std::streamsize(length);
    }
};

} // namespace

SoundLayout SoundLayout::adventure2(const std::filesystem::path& pack) {
    SoundLayout l;
    l.file = [pack](const std::string& romPath) {
        const std::string stream = "/data_lt2/stream/";
        if (romPath.rfind(stream, 0) == 0) {
            return pack / "stream" / romPath.substr(stream.size());
        }
        return pack / "sound" / romPath.substr(romPath.rfind('/') + 1);
    };
    l.driverProgram = pack / "code" / "arm9.bin";
    l.driverLoad = 0x02000000;
    l.tables = {0x02043ee0, 0x02043ba8, 0x02043aa8, 0x02043e60};
    const char* names[3] = {"SY", "GE", "SI"};
    for (int i = 0; i < 3; ++i) { // 0x0207d86c
        l.groupBank[size_t(i)] = std::string("/data_lt2/sound/") + names[i] + "_%03d.SWD";
        l.groupData[size_t(i)] = std::string("/data_lt2/sound/") + names[i] + "_%03d.SED";
        l.groupMain[size_t(i)] = std::string("/data_lt2/sound/") + names[i] + "_999.SWD";
    }
    l.bgmBank = "/data_lt2/sound/BG_%03d.SWD"; // 0x0207dc3c
    l.bgmData = "/data_lt2/sound/BG_%03d.SMD";
    l.bgmMain = "/data_lt2/sound/BG_999.SWD";
    l.roomTable = pack / "sound" / "snd_fix.dlz";
    l.timeTable = pack / "time.dlz";
    l.stream = streamPath;
    return l;
}

SoundLayout SoundLayout::adventure3(const std::filesystem::path& pack) {
    SoundLayout l;
    l.file = [pack](const std::string& romPath) {
        return pack / romPath;
    };
    // Overlay 1 (loaded at 0x02072660) holds the driver; its tables are
    // byte for byte the second adventure's.
    l.driverProgram = pack / "code" / "overlay1.bin";
    l.driverLoad = 0x02072660;
    l.tables = {0x02072660 + 0xe1d8, 0x02072660 + 0xdd94, 0x02072660 + 0xdc94, 0x02072660 + 0xe0d4};
    // ov6 0x020d034c..0x020d05b4 name snd/<G>_%03d.csnd: SED (or SMD) and
    // SWD, unpacked into snd/<G>_<n>/.
    const char* names[3] = {"SY", "GE", "SI"};
    for (int i = 0; i < 3; ++i) {
        const std::string g = names[i];
        l.groupBank[size_t(i)] = "snd/" + g + "_%1$03d/" + g + "_%1$03d.SWD";
        l.groupData[size_t(i)] = "snd/" + g + "_%1$03d/" + g + "_%1$03d.SED";
        l.groupMain[size_t(i)] = "snd/WAV_" + g + ".SWD";
    }
    l.bgmBank = "snd/BG_%1$03d/BG_%1$03d.SWD";
    l.bgmData = "snd/BG_%1$03d/BG_%1$03d.SMD";
    l.bgmMain = "snd/WAV_BG.SWD";
    // ov6 0x020d0618..0x020d0744: the music, ordinary (language folder for
    // 110..150), film and event streams; the language is "ge".
    l.stream = [](int route, int id, int part) {
        char path[96] = {};
        switch (route) {
        case 0:
            std::snprintf(path, sizeof path, "str/bgm/BG_%03d.SAD", id);
            break;
        case 1:
            std::snprintf(path, sizeof path,
                          id < 110 || id > 150 ? "str/nrm/%03d.SAD" : "str/nrm/ge/%03d.SAD", id);
            break;
        case 2:
            std::snprintf(path, sizeof path, "str/mov/ge/%03d.SAD", id);
            break;
        case 3:
            std::snprintf(path, sizeof path, "str/evt/ge/%02d_%06d_%04d.SAD", id >> 16, id & 0xffff,
                          part);
            break;
        default:
            break;
        }
        return std::string(path);
    };
    return l;
}

SoundLayout SoundLayout::adventure4(const std::filesystem::path& pack) {
    SoundLayout l;
    // The importer keeps the archives' members under main/ and the loose
    // folders (wav/, str/) at the top.
    l.file = [pack](const std::string& romPath) {
        if (romPath.rfind("snd/", 0) == 0) {
            return pack / "main" / romPath;
        }
        return pack / romPath;
    };
    // Overlay 0 (loaded at 0x02038080) holds the driver; its tables are the
    // same bytes as the third adventure's.
    l.driverProgram = pack / "code" / "overlay0.bin";
    l.driverLoad = 0x02038080;
    l.tables = {0x02038080 + 0x147f8, 0x02038080 + 0x143b4, 0x02038080 + 0x142b4,
                0x02038080 + 0x146f4};
    // ov7 0x020d8428 / 0x020d8468: snd/<G>_%03d.cse and snd/BG_%03d.cse.
    const char* names[3] = {"SY", "GE", "SI"};
    for (int i = 0; i < 3; ++i) {
        const std::string g = names[i];
        l.groupBank[size_t(i)] = "snd/" + g + "_%1$03d/SWD";
        l.groupData[size_t(i)] = "snd/" + g + "_%1$03d/SED";
        l.groupMain[size_t(i)] = "wav/" + g + ".SWD";
    }
    l.bgmBank = "snd/BG_%1$03d/SWD";
    l.bgmData = "snd/BG_%1$03d/SMD";
    l.bgmMain = "wav/BG.SWD";
    // ov7 0x020dacfc: the language ("@" = "ge") for jingles 100..138 and
    // from 600, film sound and event voices.
    l.stream = [](int route, int id, int part) {
        char path[96] = {};
        switch (route) {
        case 0:
            std::snprintf(path, sizeof path,
                          (id >= 100 && id <= 138) || id >= 600 ? "str/jng/ge/%03d.SAD"
                                                                : "str/jng/%03d.SAD",
                          id);
            break;
        case 1:
            std::snprintf(path, sizeof path, "str/bgm/%03d.SAD", id);
            break;
        case 2:
            std::snprintf(path, sizeof path, "str/mov/ge/%03d.SAD", id);
            break;
        case 3:
            std::snprintf(path, sizeof path, "str/evt/ge/%02d_%06d_%04d.SAD", id >> 20,
                          id & 0xfffff, part);
            break;
        default:
            break;
        }
        return std::string(path);
    };
    return l;
}

SoundObject::SoundObject(std::filesystem::path assets, dse::SoundCommands& hardware)
    : SoundObject(SoundLayout::adventure2(assets), hardware) {}

SoundObject::SoundObject(SoundLayout layout, dse::SoundCommands& hardware) // 0x0207f384
    : layout_(std::move(layout)),
      driver_(hardware, dse::SoundTables::read(readFile(layout_.driverProgram), layout_.driverLoad,
                                               layout_.tables[0], layout_.tables[1],
                                               layout_.tables[2], layout_.tables[3])) {
    hardware.setMasterVolume(std::uint32_t(f2iz(127.0f * 1.0f))); // 0x0207f82c
    timeTable_ =
        layout_.timeTable.empty() ? std::vector<std::uint8_t>{} : readAll(layout_.timeTable);
    if (timeTable_.size() >= 8) {
        timeCount_ = le16(timeTable_.data());
        timeStep_ = searchStep(timeCount_);
    }
    roomTable_ =
        layout_.roomTable.empty() ? std::vector<std::uint8_t>{} : readAll(layout_.roomTable);
    if (roomTable_.size() >= 8) {
        roomCount_ = le16(roomTable_.data());
        std::uint16_t header = le16(roomTable_.data() + 2), row = le16(roomTable_.data() + 4);
        if (row != 8) {
            throw std::runtime_error("snd_fix.dlz row size");
        }
        roomTable_.erase(roomTable_.begin(), roomTable_.begin() + header);
        roomStep_ = searchStep(roomCount_);
    }
    if (timeTable_.size() >= 8) {
        std::uint16_t header = le16(timeTable_.data() + 2), row = le16(timeTable_.data() + 4);
        if (row != 4) {
            throw std::runtime_error("time.dlz row size");
        }
        timeTable_.erase(timeTable_.begin(), timeTable_.begin() + header);
    }
    driver_.initialize(0x02108760, 0x20000, 4); // 0x02000dac with the overlay-2 heap block
    driver_.configureEffectPlayers(4, 4);       // 0x02003df8(4, 4) after initialisation
    bgPool_ = std::make_unique<Pool>(0x82000);
    sePool_ = std::make_unique<Pool>(0x3c000);
    syPool_ = std::make_unique<Pool>(0x9000); // fixed region overlay-4 base + 0x58000 (0x0207d470)
    Pool* pools[3] = {syPool_.get(), sePool_.get(), sePool_.get()};
    for (int i = 0; i < 3; ++i) { // 0x0207d86c
        groups_[i] = std::make_unique<Group>();
        auto& g = *groups_[i];
        g.bankFormat = layout_.groupBank[size_t(i)];
        g.dataFormat = layout_.groupData[size_t(i)];
        g.mainPath = layout_.groupMain[size_t(i)];
        g.pool = pools[i];
        g.back = i == 2;
        g.current = -1;
        g.flags = 1;
    }
    bgm_ = std::make_unique<Bgm>(); // 0x0207dc3c
    bgm_->bankFormat = layout_.bgmBank;
    bgm_->dataFormat = layout_.bgmData;
    bgm_->mainPath = layout_.bgmMain;
    bgm_->pool = bgPool_.get();
    bgm_->back = false;
    bgm_->current = 0;
    bgm_->flags = 1;
    // 0x0207e540: one stream, two voices from voice 12, 0x2000-byte ring;
    // mono files use both voices (0x02004a48(1)).
    dse::StreamConfig config;
    config.threadPriority = 6;
    config.streams = 1;
    config.voices = 2;
    config.firstVoice = 12;
    config.bufferBytes = 0x2000;
    driver_.streamInit(config);
    driver_.streamMonoOnTwoVoices(true);
    loadBank(0, 0); // 0x0207f884(obj, 0, 0)
}

SoundObject::~SoundObject() = default;

const std::uint8_t* SoundObject::memoryLimit(const std::uint8_t* p) const {
    for (const Pool* pool : {bgPool_.get(), sePool_.get(), syPool_.get()}) {
        const std::uint8_t* begin = pool->memory.data();
        if (p >= begin && p < begin + pool->memory.size()) {
            return begin + pool->memory.size();
        }
    }
    if (const std::uint8_t* end = driver_.streamMemoryLimit(p)) {
        return end;
    }
    return p;
}

float SoundObject::durationSeconds(int id) const { // 0x0207d3c0 / 0x0207d30c
    std::uint16_t frames = 0;
    if (id >= 0) {
        if (const std::uint8_t* row =
                tableFind(timeTable_, timeCount_, timeStep_, 4, std::uint32_t(id))) {
            frames = le16(row + 2);
        }
    }
    return float(std::int32_t(frames)) / 60.0f;
}

// ---- effect banks -------------------------------------------------------------
bool SoundObject::loadGroupBank(Group& g, int number) { // 0x0207d8c0
    if (!(g.flags & 1) || number == -1 || number == g.current) {
        return true; // 0x0207d800
    }
    unloadGroupBank(g);
    std::vector<std::uint8_t> temporary; // 0x2000-byte overlay heap buffer
    bool ok = false;
    auto openMain = [&](Bank& b) { // 0x0207d704
        if (b.mainOpen) {
            driver_.closeMainBank(b.mainHandle);
            b.mainOpen = false;
        }
        b.mainHandle = driver_.openMainBank(std::make_unique<DiskBank>(layout_.file(b.mainPath)));
        if (b.mainHandle < 0) {
            b.mainHandle = -0x40;
            return false;
        }
        b.mainOpen = true;
        return true;
    };
    auto closeMain = [&](Bank& b) { // 0x0207d790
        if (b.mainOpen) {
            driver_.closeMainBank(b.mainHandle);
        }
        b.mainOpen = false;
        b.mainHandle = -0x40;
    };
    auto readInto =
        [&](const std::string& path) -> std::uint8_t* { // 0x0206c10c + 0x0207d624 + memcpy
        temporary = readAll(layout_.file(path));
        if (temporary.empty()) {
            return nullptr;
        }
        if (temporary.size() > 0x2000) {
            throw std::runtime_error("effect file exceeds the 0x2000-byte load buffer");
        }
        std::uint8_t* at = g.pool->allocate(std::uint32_t(temporary.size()), g.back);
        if (at) {
            std::memcpy(at, temporary.data(), temporary.size());
        }
        return at;
    };
    if (openMain(g)) {
        std::uint8_t* sed = readInto(romPath(g.dataFormat, number));
        std::uint8_t* swd = sed ? readInto(romPath(g.bankFormat, number)) : nullptr;
        if (swd) {
            g.programHandle = driver_.registerProgramBank(swd, temporary.size());
            if (g.programHandle >= 0) {
                std::uint8_t* samples =
                    g.pool->allocate(std::uint32_t(driver_.sampleBytes(g.programHandle)), g.back);
                if (samples) {
                    driver_.loadSamples(g.programHandle, samples);
                    g.dataHandle = driver_.loadEffects(sed, 0); // 0x02003e4c
                    if (g.dataHandle < 0) {
                        driver_.unregisterProgramBank(g.programHandle);
                        g.programHandle = -0x40;
                    } else {
                        ok = true;
                        g.flags |= 2;
                        g.current = std::int16_t(number);
                    }
                }
            } else {
                g.programHandle = -0x40;
            }
        }
    }
    closeMain(g);
    if (!ok) {
        g.pool->reset(g.back);
    }
    return ok;
}

void SoundObject::unloadGroupBank(Group& g) { // 0x0207da74
    if (g.flags & 2) {
        if (g.dataHandle >= 0) {
            driver_.unloadEffects(g.dataHandle);
            g.dataHandle = -0x44;
        } // 0x02003e68
        if (g.programHandle >= 0) {
            driver_.unregisterProgramBank(g.programHandle);
            g.programHandle = -0x40;
        }
    }
    g.flags &= ~2u;
    g.current = -1;
    g.pool->reset(g.back);
}

bool SoundObject::requestEffect(Group& g, int id) { // 0x0207dab4
    if (!(g.flags & 1) || !(g.flags & 2)) {
        return false;
    }
    dse::RequestBlock request; // stack block of 0x0207dab4 (unused fields left zero)
    request.half[0x02 / 2] = 0;
    request.half[0x00 / 2] = 0;
    request.half[0x04 / 2] = 2; // pitch 0 over 0 ms
    request.half[0x12 / 2] = 2;
    request.half[0x08 / 2] = 0x7f;
    request.half[0x10 / 2] = 0; // volume 0x7f
    request.half[0x26 / 2] = 2;
    request.half[0x14 / 2] = 0x40;
    request.half[0x24 / 2] = 0; // pan 0x40
    request.half[0x2e / 2] = 0; // no tempo
    dse::PlayParameters q = dse::buildParameters(request, 0);
    std::uint32_t handle = std::uint32_t(g.dataHandle) + (std::uint32_t(std::int16_t(id)) << 16);
    return driver_.playEffect(handle, 0, q) >= 0; // 0x02003eac
}

bool SoundObject::stopGroupEffect(Group& g, int id, float seconds) { // 0x0207db3c
    if (!(g.flags & 1) || !(g.flags & 2)) {
        return false;
    }
    std::uint32_t handle = std::uint32_t(g.dataHandle) + (std::uint32_t(std::int16_t(id)) << 16);
    driver_.stopEffect(handle, 0, std::int32_t(f2uiz(1000.0f * seconds))); // 0x02003eec
    return true;
}

void SoundObject::loadBank(unsigned group, int number) {
    if (group < 3) {
        loadGroupBank(*groups_[group], number);
    }
} // 0x0207f884

void SoundObject::unloadBank(unsigned group) { // 0x0207f8ac
    if (group >= 3) {
        return;
    }
    if (unsigned(effectGroup(uniqueId_)) == group) {
        stopUniqueEffect(0);
    }
    unloadGroupBank(*groups_[group]);
}

void SoundObject::playEffect(int id) { // 0x0207f8f0 after 0x0207f850
    int group = effectGroup(id);
    if (unsigned(group) < 3) {
        requestEffect(*groups_[group], id);
    }
}

void SoundObject::stopEffect(int id, float seconds) { // 0x0207f918
    int group = effectGroup(id);
    if (unsigned(group) < 3) {
        stopGroupEffect(*groups_[group], id, seconds);
    }
}

bool SoundObject::playTrackedEffect(int id) { // 0x0207f96c
    trackedId_ = -1;
    trackedPart_ = -1;
    trackedFrames_ = 0;
    trackedRoute_ = -1;
    const int group = effectGroup(id);
    if (unsigned(group) > 2) {
        return false;
    }
    const bool ok = requestEffect(*groups_[group], id);
    if (ok) {
        trackedId_ = id;
    }
    return ok;
}

void SoundObject::trackedUpdate() { // 0x0207fca8
    auto reset = [&] {
        trackedId_ = -1;
        trackedPart_ = -1;
        trackedFrames_ = 0;
        trackedRoute_ = -1;
    };
    if (trackedRoute_ != -1) { // tracked stream: busy while the controller plays that file
        if (trackedRoute_ == stream_.route && trackedId_ == stream_.id &&
            trackedPart_ == stream_.part) {
            if (!streamBusy()) {
                reset();
            } else {
                ++trackedFrames_;
            }
        } else if (trackedId_ < 0) {
            ++trackedFrames_;
        } else {
            reset();
        }
        return;
    }
    if (trackedId_ < 0) {
        ++trackedFrames_;
        return;
    }
    if (!effectPlaying(trackedId_)) {
        reset();
        return;
    }
    ++trackedFrames_;
}

bool SoundObject::effectPlaying(int id) { // 0x0207f944 -> 0x0207d834
    int group = effectGroup(id);
    if (unsigned(group) >= 3) {
        return false;
    }
    Group& g = *groups_[group];
    if (!(g.flags & 1)) {
        return false;
    }
    return driver_.effectQuery(
               std::uint32_t(g.dataHandle) + (std::uint32_t(std::int16_t(id)) << 16), 0) != 0;
}

bool SoundObject::playUniqueEffect(int id) { // 0x0207f9e4
    int group = effectGroup(id);
    if (uniqueGroup_ == group && uniqueId_ == id) {
        return true;
    }
    stopUniqueEffect(0);
    if (unsigned(group) >= 3) {
        return false;
    }
    if (!requestEffect(*groups_[group], id)) {
        return false;
    }
    uniqueGroup_ = group;
    uniqueId_ = id;
    return true;
}

void SoundObject::stopUniqueEffect(float seconds) { // 0x0207fa50
    if (unsigned(uniqueGroup_) < 3) {
        stopGroupEffect(*groups_[uniqueGroup_], uniqueId_, seconds);
    }
    uniqueGroup_ = -1;
    uniqueId_ = -1;
}

void SoundObject::pauseSound() { // 0x0207f6f4
    // The looping unique effect stops at once and is restarted on resume.
    if (unsigned(uniqueGroup_) < 3) {
        stopGroupEffect(*groups_[uniqueGroup_], uniqueId_, 0);
    }
    stream_.flags |= 0x20; // the stream controller does not auto-stop the paused stream
    driver_.pause(0);      // 0x02000fcc
}

void SoundObject::resumeSound() { // 0x0207f724
    driver_.resume(0);            // 0x02001078
    const int id = uniqueId_;
    uniqueGroup_ = -1;
    uniqueId_ = -1;
    playUniqueEffect(id); // 0x0207f9e4 with the saved group/id
    stream_.flags &= ~0x20u;
}

void SoundObject::roomSound(int id, bool play) { // 0x0207f780
    const std::uint8_t* row = tableFind(roomTable_, roomCount_, roomStep_, 8, std::uint32_t(id));
    if (!row) {
        return;
    }
    room_ = id;
    if (play) {
        playBgm(std::int16_t(le16(row + 2)), 1.0f, 0.0f);
    }
    const int ge = std::int16_t(le16(row + 4)), si = std::int16_t(le16(row + 6));
    auto changes = [](Group& g, int n) {
        return (g.flags & 1) && n != -1 && n != g.current;
    }; // 0x0207d800
    if (changes(*groups_[1], ge)) {
        unloadBank(1);
    }
    if (changes(*groups_[2], si)) {
        unloadBank(2);
    }
    loadBank(1, ge);
    loadBank(2, si);
}

// ---- BGM controller -----------------------------------------------------------
void SoundObject::frame() {
    streamFrame();
    bgmUpdate();
    trackedUpdate();
} // main loop 0206cb00: 0207fc98, 0207faf4, 0207fca8

void SoundObject::playBgm(int track, float volume, float seconds) {
    bgmPlay(track, volume, seconds);
} // 0x0207faa4

void SoundObject::stopBgm(float seconds) {
    bgmStop(seconds);
} // 0x0207fa94

void SoundObject::switchBgm(int track) {
    bgmSwitch(track);
} // 0x0207fab4

int SoundObject::currentBgm() {
    return bgm_->current;
} // 0x0207fa88

void SoundObject::fadeOutBgm(float seconds) {
    bgmFadeOut(seconds);
} // 0x0207fad4

void SoundObject::bgmVolume(float volume, float seconds) {
    bgmSetVolume(volume, seconds);
} // 0x0207fae4

bool SoundObject::bgmPlaying() { // 0x0207dc00
    return (bgm_->flags & 2) && driver_.sequencePlaying(bgm_->dataHandle) > 0;
}

bool SoundObject::bgmSwitch(int track) { // 0x0207dc90
    Bgm& b = *bgm_;
    if (!(b.flags & 1)) {
        return false;
    }
    b.flags &= ~0x20u;
    b.pending = -1;
    if (track == -1 || track == b.current) {
        return true;
    }
    bgmStop(durationSeconds(scene_ == 6 ? 0x3f2 : 1000));
    if (track > 0) {
        b.pending = std::int16_t(track);
        b.flags |= 0x20;
    }
    bgmUpdate();
    return true;
}

bool SoundObject::bgmPlay(int track, float volume, float seconds) { // 0x0207dd34
    Bgm& b = *bgm_;
    if (!(b.flags & 1)) {
        return false;
    }
    if ((b.flags & 2) && track == b.current) {
        if ((b.flags & 4) && !(b.flags & 8)) {
            return bgmSetVolume(volume, seconds);
        }
        return bgmRestart(volume, seconds);
    }
    if (!bgmSwitch(track)) {
        return false;
    }
    if (track >= 0) {
        b.requested = volume;
        b.startVolume = volume;
        b.startSeconds = seconds;
        b.flags |= 0x40;
    }
    bgmUpdate();
    return true;
}

bool SoundObject::bgmStop(float seconds) { // 0x0207ddf4
    Bgm& b = *bgm_;
    if (!(b.flags & 1) || (b.flags & 0x10)) {
        return false;
    }
    b.requested = 0;
    b.startVolume = 0;
    b.startSeconds = 0;
    b.flags &= ~0x40u;
    b.resumeVolume = 0;
    b.resumeSeconds = 0;
    b.flags &= ~0x100u;
    b.volume = 0;
    b.volumeSeconds = 0;
    b.flags &= ~0x200u;
    b.tempo = 0;
    b.tempoSeconds = 0;
    const std::uint32_t before = b.flags;
    b.flags &= ~0x400u;
    if (!(before & 8) && !(before & 0x80)) {
        bgmFadeOut(seconds);
    }
    b.flags |= 0x10;
    bgmUpdate();
    return true;
}

bool SoundObject::bgmRestart(float volume, float seconds) { // 0x0207dea0
    Bgm& b = *bgm_;
    if (!(b.flags & 1) || (b.flags & 0x10)) {
        return false;
    }
    b.fadeAux = 0;
    b.fadeSeconds = 0;
    b.flags &= ~0x80u;
    b.resumeVolume = 0;
    b.resumeSeconds = 0;
    b.flags &= ~0x100u;
    b.volume = 0;
    b.volumeSeconds = 0;
    b.flags &= ~0x200u;
    b.requested = volume;
    b.tempo = 1.0f;
    b.tempoSeconds = 0;
    b.flags |= 0x400;
    b.startVolume = volume;
    b.startSeconds = seconds;
    b.flags |= 0x40;
    bgmUpdate();
    return true;
}

bool SoundObject::bgmFadeOut(float seconds) { // 0x0207df4c
    Bgm& b = *bgm_;
    if (!(b.flags & 1) || (b.flags & 0x10) || !(b.flags & 4) || (b.flags & 8)) {
        return false;
    }
    b.requested = 0;
    b.resumeVolume = 0;
    b.resumeSeconds = 0;
    b.flags &= ~0x100u;
    b.fadeAux = 0;
    b.fadeSeconds = seconds;
    b.flags |= 0x80;
    bgmUpdate();
    return true;
}

bool SoundObject::bgmSetVolume(float volume, float seconds) { // 0x0207dfc0
    Bgm& b = *bgm_;
    if (!(b.flags & 1) || (b.flags & 0x10)) {
        return false;
    }
    if (b.requested == volume) {
        return false; // 0x0203a9b8 float equality
    }
    b.requested = volume;
    b.volume = volume;
    b.volumeSeconds = seconds;
    b.flags |= 0x200;
    bgmUpdate();
    return true;
}

void SoundObject::bgmUpdate() { // 0x0207e038
    Bgm& b = *bgm_;
    if (!(b.flags & 1)) {
        return;
    }
    if ((b.flags & 0x10) && !bgmPlaying()) {
        if (b.dataHandle >= 0) {
            driver_.unloadSequence(b.dataHandle);
            b.dataHandle = -0x40;
        } // 0x02002d34
        if (b.programHandle >= 0) {
            driver_.unregisterProgramBank(b.programHandle);
            b.programHandle = -0x40;
        }
        b.flags &= ~0x1eu;
        b.current = 0;
        b.pool->reset(b.back);
    }
    if ((b.flags & 0x20) && !(b.flags & 0x10)) {
        b.current = 0;
        if (b.mainOpen) {
            driver_.closeMainBank(b.mainHandle);
        }
        b.mainHandle = driver_.openMainBank(std::make_unique<DiskBank>(layout_.file(b.mainPath)));
        b.mainOpen = b.mainHandle >= 0;
        if (b.mainOpen) {
            auto readInto = [&](const std::string& path) -> std::pair<std::uint8_t*, std::size_t> {
                std::uint8_t* at = b.pool->current(b.back);
                auto bytes = readAll(layout_.file(path));
                if (bytes.empty()) {
                    return {nullptr, 0};
                }
                // The original reads before allocating (an oversized file
                // overruns the pool); natively a file that cannot fit fails.
                if (!b.pool->allocate(std::uint32_t(bytes.size()), b.back)) {
                    return {nullptr, 0};
                }
                std::memcpy(at, bytes.data(), bytes.size());
                return {at, bytes.size()};
            };
            auto smd = readInto(romPath(b.dataFormat, b.pending));
            if (smd.first) {
                auto swd = readInto(romPath(b.bankFormat, b.pending));
                if (swd.first) {
                    b.programHandle = driver_.registerProgramBank(swd.first, swd.second);
                    if (b.programHandle >= 0) {
                        std::uint8_t* samples = b.pool->allocate(
                            std::uint32_t(driver_.sampleBytes(b.programHandle)), b.back);
                        if (samples) {
                            driver_.loadSamples(b.programHandle, samples);
                            b.dataHandle = driver_.loadSequence(
                                smd.first, smd.second, nullptr); // 0x02002cfc (callback returns 0)
                            if (b.dataHandle < 0) {
                                driver_.unregisterProgramBank(b.programHandle);
                                b.programHandle = -0x40;
                                b.dataHandle = -0x40;
                            } else {
                                b.current = b.pending;
                                b.flags = (b.flags | 2) & ~0xcu;
                            }
                        }
                    } else {
                        b.programHandle = -0x40;
                    }
                }
            }
            driver_.closeMainBank(b.mainHandle);
        }
        b.mainOpen = false;
        b.mainHandle = -0x40;
        b.flags &= ~0x20u;
        b.pending = -1;
    }
    if (!(b.flags & 2)) {
        return;
    }
    if (b.flags & 0x80) {
        driver_.stopSequence(b.dataHandle, std::int32_t(f2uiz(1000.0f * b.fadeSeconds)));
        b.fadeAux = 0;
        b.fadeSeconds = 0;
        b.flags = (b.flags & ~0x80u) | 8;
    }
    if (b.flags & 0x10) {
        return;
    }
    if (b.flags & 0x40) {
        std::int32_t volume = f2iz(127.0f * b.startVolume);
        std::uint32_t ms = f2uiz(1000.0f * b.startSeconds);
        driver_.playSequence(b.dataHandle, std::uint8_t(volume), std::uint16_t(ms));
        driver_.sequenceTempo(b.dataHandle, 0, 0);
        b.startVolume = 0;
        b.startSeconds = 0;
        b.flags = (b.flags & ~0x48u) | 4;
    }
    if (b.flags & 0x100) {
        std::int32_t volume = f2iz(127.0f * b.resumeVolume);
        std::uint32_t ms = f2uiz(1000.0f * b.resumeSeconds);
        driver_.resumeSequence(b.dataHandle, std::uint8_t(volume), std::uint16_t(ms));
        b.resumeVolume = 0;
        b.resumeSeconds = 0;
        b.flags &= ~0x108u;
    }
    if (b.flags & 0x200) {
        std::int32_t volume = f2iz(127.0f * b.volume);
        std::uint32_t ms = f2uiz(1000.0f * b.volumeSeconds);
        driver_.sequenceVolume(b.dataHandle, volume, std::int32_t(ms));
        b.volume = 0;
        b.volumeSeconds = 0;
        b.flags &= ~0x200u;
    }
    if (b.flags & 0x400) {
        std::uint32_t scale = f2uiz(256.0f * b.tempo);
        std::uint32_t ms = f2uiz(1000.0f * b.tempoSeconds);
        driver_.sequenceTempo(b.dataHandle, std::int32_t(scale), std::int32_t(ms));
        b.tempo = 0;
        b.tempoSeconds = 0;
        b.flags &= ~0x400u;
    }
}

// ---- Stream controller (0x0207e41c..0x0207ee1b) --------------------------------
namespace {
struct DiskStream final : dse::StreamFile {
    std::vector<std::uint8_t> data;

    bool read(std::uint32_t offset, std::uint8_t* destination, std::uint32_t length) override {
        if (offset > data.size() || length > data.size() - offset) {
            return false;
        }
        std::copy(data.begin() + offset, data.begin() + offset + length, destination);
        return true;
    }

    std::uint32_t size() const override { return std::uint32_t(data.size()); }
};

std::int32_t seconds1000(float seconds) {
    return std::int32_t(f2uiz(1000.0f * seconds));
} // 0x0203afd4, 0x0203af04
} // namespace

std::string streamPath(int route, int id, int part) { // 0x0207eaf4 switch, 0x0208398c language "ge"
    char path[96] = {};
    switch (route) {
    case 0:
        if (id < 100) {
            std::snprintf(path, sizeof path, "/data_lt2/stream/ST_%03d.SAD", id);
        } else {
            std::snprintf(path, sizeof path, "/data_lt2/stream/nazo/%s/ST_%04d.SAD", "ge", id);
        }
        break;
    case 1:
        std::snprintf(path, sizeof path, "/data_lt2/stream/bgm/BG_%03d.SAD", id);
        break;
    case 2:
        std::snprintf(path, sizeof path, "/data_lt2/stream/event/%s/%03d_%d.SAD", "ge", id, part);
        break;
    case 3:
        std::snprintf(path, sizeof path, "/data_lt2/stream/movie/%s/M%d.SAD", "ge", id);
        break;
    default:
        break; // empty name: the open fails
    }
    return path;
}

bool SoundObject::streamBusy() { // 0x0207e494 (0x02004b24 -> 0x020057e0)
    if (!(stream_.flags & 1)) {
        return false;
    }
    const int playing =
        stream_.handle < 0 ? -0x40 : driver_.streamPlaying(std::uint32_t(stream_.handle));
    return playing > 0;
}

bool SoundObject::streamExists() { // 0x0207e4bc (0x02004a5c -> 0x02005270)
    if (!(stream_.flags & 1)) {
        return false;
    }
    if (stream_.handle < 0) {
        return false;
    }
    const int r = driver_.streamQuery(std::uint32_t(stream_.handle));
    if (r == -0xb1) {
        stream_.handle = -0x47;
    }
    return r >= 0;
}

bool SoundObject::streamStopFade(float seconds, bool update) { // 0x0207e854
    auto& c = stream_;
    if (c.flags & 0x80) {
        return false;
    }
    if (!(c.flags & 2)) {
        return false;
    }
    c.restartVolume = c.restartSeconds = 0;
    c.flags &= ~0x400u;
    c.stopSeconds = seconds;
    c.flags |= 0x200;
    if (update) {
        streamFrame();
    }
    return true;
}

bool SoundObject::streamStopRequest(float seconds) { // 0x0207e73c
    auto& c = stream_;
    if (c.flags & 0x80) {
        return false;
    }
    c.startVolume = c.startSeconds = 0;
    c.flags &= ~0x100u;
    c.restartVolume = c.restartSeconds = 0;
    c.flags &= ~0x400u;
    c.volume = c.volumeSeconds = 0;
    c.flags &= ~0x800u;
    c.seekSeconds = 0;
    c.flags &= ~0x1000u;
    if (!(c.flags & 4) && !(c.flags & 0x200)) {
        streamStopFade(seconds, false);
    }
    c.flags |= 0x80;
    streamFrame();
    return true;
}

bool SoundObject::streamRequest(int route, int id, int part, bool meter) { // 0x0207e5e0
    auto& c = stream_;
    c.flags &= ~0x40u;
    c.reqRoute = -1;
    c.reqId = -1;
    c.reqPart = -1;
    if (unsigned(route) > 3) {
        return false;
    }
    c.meter = meter ? 1 : 0;
    if (id == -1) {
        return true;
    }
    streamStopRequest(0);
    c.reqRoute = route;
    c.reqId = id;
    c.reqPart = std::int16_t(part);
    c.flags |= 0x40;
    streamFrame();
    return true;
}

bool SoundObject::playStream(int route, int id, int part, float volume, float seconds,
                             bool meter) { // 0x0207fb84 -> 0x0207e660
    trackedId_ = -1;
    trackedPart_ = -1;
    trackedFrames_ = 0;
    trackedRoute_ = -1;
    auto& c = stream_;
    streamStopRequest(0);
    // The same file is resumed or restarted only while flag 0x80 is clear,
    // which the stop request above has just set; kept as in 0x0207e660.
    if (!(c.flags & 0x80) && (c.flags & 1) && route == c.route && id == c.id && part == c.part) {
        if (c.flags & 2) {
            restartStream(volume, seconds);
        } else {
            startStream(volume, seconds);
        }
        return true;
    }
    if (!streamRequest(route, id, part, meter)) {
        return false;
    }
    c.startVolume = volume;
    c.startSeconds = seconds;
    c.flags |= 0x100;
    streamFrame();
    return true;
}

bool SoundObject::playTrackedStream(int route, int id, int part, float volume,
                                    float seconds) { // 0x0207fc3c
    if (!playStream(route, id, part, volume, seconds, false)) {
        return false;
    }
    trackedRoute_ = route;
    trackedId_ = id;
    trackedPart_ = part;
    return true;
}

void SoundObject::stopStream(float seconds) {
    streamStopRequest(seconds);
} // 0x0207fbcc

bool SoundObject::prepareStream(int route, int id, int part,
                                bool meter) { // 0x0207fb54 -> 0x0207e5e0
    trackedId_ = -1;
    trackedPart_ = -1;
    trackedFrames_ = 0;
    trackedRoute_ = -1;
    return streamRequest(route, id, part, meter);
}

void SoundObject::startStream(float volume, float seconds) { // 0x0207e7d8
    auto& c = stream_;
    if (c.flags & 0x80) {
        return;
    }
    c.stopSeconds = 0;
    c.flags &= ~0x200u;
    c.restartVolume = c.restartSeconds = 0;
    c.flags &= ~0x400u;
    c.volume = c.volumeSeconds = 0;
    c.flags &= ~0x800u;
    c.startVolume = volume;
    c.startSeconds = seconds;
    c.flags |= 0x100;
    streamFrame();
}

void SoundObject::restartStream(float volume, float seconds) { // 0x0207e8b4
    auto& c = stream_;
    if ((c.flags & 0x80) || !(c.flags & 2)) {
        return;
    }
    c.stopSeconds = 0;
    c.flags &= ~0x200u;
    c.restartVolume = volume;
    c.restartSeconds = seconds;
    c.flags |= 0x400;
    streamFrame();
}

void SoundObject::streamVolume(float volume, float seconds) { // 0x0207e91c
    auto& c = stream_;
    if (c.flags & 0x80) {
        return;
    }
    c.volume = volume;
    c.volumeSeconds = seconds;
    c.flags |= 0x800;
    streamFrame();
}

void SoundObject::seekStream(float seconds) { // 0x0207e964
    auto& c = stream_;
    if (c.flags & 0x80) {
        return;
    }
    c.seekSeconds = seconds;
    c.flags |= 0x1000;
    streamFrame();
}

float SoundObject::streamPosition() { // 0x0207e5a0
    const auto& c = stream_;
    if (!(c.flags & 1) || !(c.flags & 2)) {
        return 0;
    }
    const std::int32_t ms =
        driver_.streamBytesToMs(std::uint32_t(c.handle), c.position); // 0x02004d90
    return float(ms) / 1000.0f;                                       // 0x0203af44, 0x0203aa94
}

bool SoundObject::bgmSettled() {
    return !(bgm_->flags & 0x10);
} // 0x0207dc28

bool SoundObject::streamLoud() { // 0x0207e500
    return streamBusy() && int(stream_.levelMax) - int(stream_.levelMin) > 0x1770;
}

void SoundObject::streamFrame() { // 0x0207e9b0
    auto& c = stream_;
    if (streamBusy()) {
        // 0x02004b60 copies the stream status to +0x70; its position (+0x78)
        // anchors restart and seek. With metering on, 0x02004c04 samples the
        // levels to +0x88 every 11th frame (read by 0x0207e500).
        c.position = driver_.streamPosition(std::uint32_t(c.handle));
        if (c.meter && ++c.meterFrames > 10) {
            std::array<std::int16_t, 4> levels{};
            if (driver_.streamLevels(std::uint32_t(c.handle), levels) > 1) {
                // A second channel would land in the BGM object at +0x100; every
                // metered file in this game (route 2, 293 voices) is mono.
                throw std::runtime_error("metered stereo stream");
            }
            c.levelMin = levels[0];
            c.levelMax = levels[1];
            c.meterFrames = 0;
        }
    } else if (!(c.flags & 0x80) && !(c.flags & 0x20) && (c.flags & 8) && !(c.flags & 4)) {
        streamStopRequest(0); // finished on its own
    }
    if ((c.flags & 0x80) && !streamBusy()) {
        if (!(c.flags & 0x10)) {
            if (c.flags & 1) { // 0x02004b08
                driver_.streamClose(std::uint32_t(c.handle));
                c.handle = -0x47;
            }
            c.flags = (c.flags & ~0xfu) | 0x10;
        } else if (!streamExists()) {
            c.flags &= ~(0x10u | 0x80u | 8u);
            c.route = -1;
            c.id = -1;
            c.part = -1;
        }
    }
    if ((c.flags & 0x40) && !(c.flags & 0x80)) {
        c.route = -1;
        c.id = -1;
        c.part = -1;
        const std::string path =
            layout_.stream ? layout_.stream(c.reqRoute, c.reqId, c.reqPart) : std::string();
        int r; // 0x02004a94(ctrl, path, 0, 0)
        if (c.handle >= 0 && driver_.streamQuery(std::uint32_t(c.handle)) < 0) {
            c.handle = -0x40;
        }
        if (c.handle >= 0) {
            r = driver_.streamQuery(std::uint32_t(c.handle));
        } else {
            std::unique_ptr<dse::StreamFile> file;
            if (!path.empty()) {
                const auto local = layout_.file(path);
                if (std::filesystem::is_regular_file(resolvePath(local))) {
                    auto f = std::make_unique<DiskStream>();
                    f->data = readAll(local);
                    file = std::move(f);
                }
            }
            r = driver_.streamCreate(std::move(file), 0);
            c.handle =
                r; // the 0x02004e40 event callback reaches a no-op handler (vtable 0x02087ee0 +8)
        }
        if (r < 0) {
            c.flags &= ~0x100u;
        } else {
            c.flags |= 1;
            c.route = c.reqRoute;
            c.id = c.reqId;
            c.part = c.reqPart;
            driver_.streamSetMeter(std::uint32_t(c.handle), c.meter); // 0x02004bd8
            c.meterFrames = 0;
        }
        c.flags &= ~(4u | 2u | 0x40u);
        c.reqRoute = -1;
        c.reqId = -1;
        c.reqPart = -1;
    }
    if (!(c.flags & 1) || !streamExists()) {
        return;
    }
    if (c.flags & 0x200) {
        driver_.streamStop(std::uint32_t(c.handle), seconds1000(c.stopSeconds));
        c.stopSeconds = 0;
        c.flags = (c.flags & ~0x200u) | 4;
    }
    if (c.flags & 0x80) {
        return;
    }
    if (c.flags & 0x100) {
        const auto volume = std::uint8_t(f2iz(127.0f * c.startVolume)); // 0x0203afd4, 0x0203aed0
        driver_.streamStart(std::uint32_t(c.handle), volume, seconds1000(c.startSeconds), 1);
        c.startVolume = c.startSeconds = 0;
        c.flags = (c.flags & ~0x104u) | 0xa;
    }
    if (c.flags & 0x400) { // 0x02004cc0 -> 0x02005b34, then 0x02004b40
        driver_.streamSeek(std::uint32_t(c.handle), c.position);
        driver_.streamStart(std::uint32_t(c.handle), std::uint8_t(f2iz(127.0f * c.restartVolume)),
                            seconds1000(c.restartSeconds), 1);
        c.restartVolume = c.restartSeconds = 0;
        c.flags &= ~0x404u;
    }
    if (c.flags & 0x800) { // 0x02004cb0 -> 0x02005a14
        driver_.streamVolume(std::uint32_t(c.handle), std::uint8_t(f2iz(127.0f * c.volume)),
                             seconds1000(c.volumeSeconds));
        c.volume = c.volumeSeconds = 0;
        c.flags &= ~0x800u;
    }
    if (c.flags & 0x1000) { // 0x02004d90 + offset, 0x02004cd0 (0x02004cf8 -> 0x02005b34)
        const std::int32_t ms = driver_.streamBytesToMs(std::uint32_t(c.handle), c.position) +
                                f2iz(1000.0f * c.seekSeconds);
        const std::int32_t bytes = driver_.streamMsToBytes(std::uint32_t(c.handle), ms);
        if (bytes >= 0) {
            driver_.streamSeek(std::uint32_t(c.handle), bytes);
        }
        c.seekSeconds = 0;
        c.flags &= ~0x1000u;
    }
}

} // namespace picarat
