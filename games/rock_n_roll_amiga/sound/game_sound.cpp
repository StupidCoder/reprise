#include "rock_n_roll_amiga/sound/game_sound.hpp"
#include <algorithm>

namespace rocknroll::sound {
namespace {

constexpr std::uint32_t load_table = 0x78136;  // per file number: a long, then the load address
constexpr std::uint32_t song_data_at = 0x5740E;
constexpr std::uint32_t song_data_end = 0x5A40E; // 12,288 bytes, the song files' size
constexpr std::uint32_t periods = 0x390A6;
constexpr int song_sets_file = 0x40;
constexpr std::uint32_t world_table = 0x70E94;  // per world: graphics, extra, songs, samples

// $70E40: the song sets in file $40 (where they lie once it is loaded, and
// how many bytes are copied: DBRA counts plus one).
struct SongSet {
    int number;
    std::uint32_t at, bytes;
};
constexpr SongSet song_sets[] = {{1, 0x10360, 0x2AB1}, {8, 0x13240, 0x1BC9}, {9, 0x16120, 0x2C11}};

} // namespace

GameSound::GameSound(const data::Assets& assets) : assets_(&assets) {
    const auto& program = assets.program;
    const std::uint32_t end = data::Program::base + 0x61800; // $7BD78
    memory_.resize(end - Tfmx::sample_base);
    for (std::uint32_t a = Tfmx::sample_base; a < end; ++a)
        memory_[a - Tfmx::sample_base] = std::int8_t(program.contains(a) ? program.u8(a) : 0);
    std::vector<std::uint16_t> table;
    for (std::uint32_t i = 0; i < 256; ++i) table.push_back(program.contains(periods + i * 2 + 1) ? program.u16(periods + i * 2) : 0);
    player_.set_periods(table);
    paula_.set_memory(memory_);
}

void GameSound::place(std::uint32_t address, std::span<const std::uint8_t> bytes) {
    if (address < Tfmx::sample_base) return;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t at = address - Tfmx::sample_base + i;
        if (at >= memory_.size()) break;
        memory_[at] = std::int8_t(bytes[i]);
    }
}

void GameSound::load_file(int file) {
    const auto* music = assets_->music_file(file);
    if (!music) return;
    place(assets_->program.u32(load_table + std::uint32_t(file) * 8 + 4), music->bytes);
}

void GameSound::copy_song_set(int set) {
    const auto* file = assets_->music_file(song_sets_file);
    if (!file) return;
    const std::uint32_t loaded = assets_->program.u32(load_table + std::uint32_t(song_sets_file) * 8 + 4);
    const SongSet* chosen = &song_sets[2]; // any other number: the third set
    for (const auto& s : song_sets)
        if (s.number == set) chosen = &s;
    const std::size_t from = chosen->at - loaded;
    if (from >= file->bytes.size()) return;
    const std::size_t count = std::min<std::size_t>(chosen->bytes, file->bytes.size() - from);
    place(song_data_at, std::span(file->bytes).subspan(from, count));
}

void GameSound::load_world(int world) {
    const auto& program = assets_->program;
    const std::uint32_t entry = world_table + std::uint32_t(world - 1) * 16;
    if (world == 1) { // $70FC8: the samples file $3B and song set 1
        load_file(0x3B);
        copy_song_set(1);
        return;
    }
    load_file(int(program.u32(entry + 8)));
    load_file(int(program.u32(entry + 12)));
}

void GameSound::load_title(bool after_result) {
    copy_song_set(after_result ? 8 : 9);
    load_world(8);
}

void GameSound::load_jukebox(int title) {
    if (title == 8 || title == 9) {
        copy_song_set(title);
        load_world(8);
    } else {
        load_world(title);
    }
}

std::vector<std::uint8_t> GameSound::song_data() const {
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t a = song_data_at; a < song_data_end; ++a) bytes.push_back(std::uint8_t(memory_[a - Tfmx::sample_base]));
    return bytes;
}

} // namespace rocknroll::sound
