#include "data/program_tables.hpp"

#include "data/mach_o.hpp"
#include "data/program_layout.hpp"

#include <cstring>

namespace doc::data {
namespace {

using namespace layout;

std::uint32_t word(const Bytes& b, std::size_t at) {
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 |
           std::uint32_t(b[at + 3]) << 24;
}

float asFloat(std::uint32_t w) {
    float f;
    std::memcpy(&f, &w, sizeof f);
    return f;
}

std::string string(const ExecutableImage& exe, std::uint32_t address) {
    if (address == 0) return {};
    auto s = exe.readCString(address, 256);
    return s && !s->empty() ? std::string(s->begin(), s->end() - 1) : std::string();
}

}  // namespace

std::vector<ParticleKindData> readParticleKinds(const GameFiles& files) {
    std::vector<ParticleKindData> kinds;
    const ExecutableImage& exe = files.executable();
    auto table = exe.read(kParticleTable, std::size_t(kParticleKinds) * 20);
    if (!table) return kinds;
    for (int i = 0; i < kParticleKinds; ++i) {
        const std::size_t at = std::size_t(i) * 20;
        ParticleKindData k;
        // Entry: sheet name, frame width, image flags, texture (-1 until loaded), priority
        // (Particles::addParticle @0x138d00 reads +4 and +0xc).
        k.sheet = string(exe, word(*table, at));
        k.frameWidth = asFloat(word(*table, at + 4));
        k.layer = int(word(*table, at + 8));
        k.priority = int(word(*table, at + 16));
        kinds.push_back(std::move(k));
    }
    return kinds;
}

std::vector<DossierData> readDossiers(const GameFiles& files) {
    std::vector<DossierData> dossiers;
    const ExecutableImage& exe = files.executable();
    auto names = exe.read(kDossierNames, std::size_t(kDossiers) * 4);
    auto types = exe.read(kDossierTypes, std::size_t(kDossiers) * 4);
    auto tips = exe.read(kDossierTips, std::size_t(kDossiers) * 20);
    auto seen = exe.read(kDossierLastSeen, std::size_t(kDossiers) * 4);
    if (!names || !types || !tips || !seen) return dossiers;
    for (int i = 0; i < kDossiers; ++i) {
        const std::size_t at = std::size_t(i) * 4;
        DossierData d;
        d.name = string(exe, word(*names, at));
        d.title = int(word(*types, at));
        for (std::size_t k = 0; k < 5; ++k) d.tips[k] = int(word(*tips, std::size_t(i) * 20 + k * 4));
        d.lastSeen = int(word(*seen, at));
        dossiers.push_back(std::move(d));
    }
    return dossiers;
}

std::vector<FluffKindData> readFluffKinds(const GameFiles& files) {
    std::vector<FluffKindData> kinds;
    const ExecutableImage& exe = files.executable();
    auto table = exe.read(kFluffTable, std::size_t(kFluffKinds) * 24);
    if (!table) return kinds;
    for (int i = 0; i < kFluffKinds; ++i) {
        const std::size_t at = std::size_t(i) * 24;
        FluffKindData k;
        k.sheet = string(exe, word(*table, at));
        k.frameWidth = asFloat(word(*table, at + 4));
        k.imageFlags = int(word(*table, at + 8));
        k.frameMs = int(word(*table, at + 12));
        k.mode = int(word(*table, at + 16));
        k.autoplay = (*table)[at + 20] != 0;
        kinds.push_back(std::move(k));
    }
    return kinds;
}

std::vector<std::uint8_t> readEyeMapping(const GameFiles& files) {
    auto table = files.executable().read(kEyeMapping, std::size_t(kMousePoses) * 5);
    return table ? std::vector<std::uint8_t>(table->begin(), table->end()) : std::vector<std::uint8_t>{};
}

std::vector<std::pair<float, float>> readMouseEyeOffsets(const GameFiles& files) {
    std::vector<std::pair<float, float>> offsets;
    auto table = files.executable().read(kMouseEyeOffsets, std::size_t(kMousePoses) * 8);
    if (!table) return offsets;
    for (std::size_t i = 0; i < std::size_t(kMousePoses); ++i)
        offsets.emplace_back(asFloat(word(*table, i * 8)), asFloat(word(*table, i * 8 + 4)));
    return offsets;
}

std::vector<std::string> readLevelGoalNames(const GameFiles& files) {
    std::vector<std::string> names;
    const ExecutableImage& exe = files.executable();
    auto table = exe.read(kLevelGoalNames, std::size_t(kGoalKinds) * 4);
    if (!table) return names;
    for (std::size_t i = 0; i < std::size_t(kGoalKinds); ++i) names.push_back(string(exe, word(*table, i * 4)));
    return names;
}

std::vector<std::pair<std::string, std::string>> readGoalStarImages(const GameFiles& files) {
    std::vector<std::pair<std::string, std::string>> images;
    const ExecutableImage& exe = files.executable();
    auto table = exe.read(kGoalStarImages, std::size_t(kGoalKinds) * 8);
    if (!table) return images;
    for (std::size_t i = 0; i < std::size_t(kGoalKinds); ++i)
        images.emplace_back(string(exe, word(*table, i * 8)), string(exe, word(*table, i * 8 + 4)));
    return images;
}

std::vector<std::string> readSoundTable(const GameFiles& files) {
    std::vector<std::string> names;
    const ExecutableImage& exe = files.executable();
    auto table = exe.read(kSoundFiles, std::size_t(kSounds) * 4);
    if (!table) return names;
    for (std::size_t i = 0; i < std::size_t(kSounds); ++i) names.push_back(string(exe, word(*table, i * 4)));
    return names;
}

std::vector<int> readScoreNotes(const GameFiles& files) {
    std::vector<int> notes;
    auto table = files.executable().read(kScoreNotes, 6 * 4);
    if (!table) return notes;
    for (std::size_t i = 0; i < 6; ++i) notes.push_back(int(word(*table, i * 4)));
    return notes;
}

}  // namespace doc::data
