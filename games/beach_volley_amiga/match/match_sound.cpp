#include "beach_volley_amiga/match/match_sound.hpp"
#include "beach_volley_amiga/match/match_data.hpp"

namespace beach_volley::match {
namespace {

// What an effect routine writes: the channel, the sample (an address in
// the sprite block), its length in words, the period, and whether the
// routine switches the channel's DMA off first (then the sample starts at
// once; otherwise a playing channel takes it at the end of its block) and
// sets the length to one word after the start (the sample plays once).
struct EffectSample {
    int channel;
    std::uint32_t sample;
    std::uint16_t length, period;
    bool restart, once;
};

constexpr EffectSample effect_samples[] = {
    {3, 0x73B92, 0x1191, 0x1F4, false, false}, // crowd
    {3, 0x736B0, 0x0271, 0x0C8, false, true},  // serve
    {3, 0x73200, 0x0258, 0x244, true, true},   // whistle
    {3, 0x73200, 0x0258, 0x1E0, true, true},   // net whistle
    {1, 0x75EB4, 0x0411, 0x258, true, true},   // spike
    {3, 0x736B0, 0x0271, 0x0A0, true, true},   // hit
    {3, 0x766D6, 0x03B7, 0x118, true, true},   // landing
    {3, 0x736B0, 0x0271, 0x050, true, true},   // smash
};

} // namespace

void MatchSound::bind(const MatchData& data) {
    data_ = &data;
    memory_.clear();
    memory_.reserve(data.instruments.size() + data.sprites.size());
    for (const auto b : data.instruments) memory_.push_back(std::int8_t(b));
    for (const auto b : data.sprites) memory_.push_back(std::int8_t(b));
    paula_->set_memory(memory_);
    player_.set_tables(data.player_tables);
    if (player_.playing()) player_.bind(current());
}

const sound::Song* MatchSound::current() const {
    if (!data_) return nullptr;
    switch (song_) {
    case Song::venue: return &data_->venue_song;
    case Song::hurry: return &data_->hurry_song;
    case Song::a_won: return &data_->a_won_song;
    case Song::b_won: return &data_->b_won_song;
    case Song::none: break;
    }
    return nullptr;
}

std::uint32_t MatchSound::at(std::uint32_t address) const {
    return std::uint32_t(data_->instruments.size()) + (address - sprite_block_address);
}

void MatchSound::play(Song song, bool once) {
    song_ = song;
    heard_.push_back(100 + int(song));
    if (const auto* s = current()) player_.start(*s, *paula_, once);
}

void MatchSound::vblank(bool hold_1) {
    if (!music_on_) return;
    player_.hold(hold_1 ? 0x2 : 0x0);
    player_.tick(*paula_);
    // $284EE: the venue song's sample volumes and the three music channels
    // are set to the music's volume after every tick, so neither a sample's
    // own volume nor command C is heard.
    if (!player_.playing()) music_on_ = false; // $31172: a song played once has ended
    paula_->set_volume(0, std::uint16_t(volume_));
    if (!hold_1) paula_->set_volume(1, std::uint16_t(volume_));
    paula_->set_volume(2, std::uint16_t(volume_));
}

void MatchSound::effect(Effect e) {
    heard_.push_back(int(e));
    const auto& s = effect_samples[std::size_t(e)];
    const auto bit = std::uint16_t(1u << s.channel);
    if (s.restart) paula_->write_dmacon(bit);
    paula_->set_location(s.channel, at(s.sample));
    paula_->set_length(s.channel, s.length);
    paula_->set_period(s.channel, s.period);
    paula_->set_volume(s.channel, 64);
    paula_->write_dmacon(std::uint16_t(0x8000 | bit));
    if (s.once) paula_->set_length(s.channel, 1);
}

} // namespace beach_volley::match
