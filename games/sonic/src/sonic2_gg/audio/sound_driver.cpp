// This cartridge's revision of the shared sound driver; see sound_driver.hpp.

#include "sonic2_gg/audio/sound_driver.hpp"

namespace sonic2_gg {

const gg_engine::DriverProfile sound_profile{
    .slot2_bank = 0x1B,
    .silence_writes = 0x442E,
    .end_command = 0x4187,
    .music_channel_ids = 0x428C,
    .note_periods = 0x4459,
    .envelope_tables = 0x492C,
    .priorities = 0x4A92,
    .song_headers = 0x4AE6,
    .effect_headers = 0x4B10,
    .song_tempos = 0x4AD1,
    .modulation_tables = 0, // table modulation is never used
    .request_slots = 2,     // DD04, DD05 (409F)
    .default_slot = 0,
    .first_effect = 0x9A, // 81-99 songs, 9A-BF effects
    .first_command = 0xC0,
    .tempo = gg_engine::DriverProfile::Tempo::countdown, // 4086
    .fade_step = 1,                                      // 40D0
    .fade_ends_drums = false,
    .note_base = 0x80, // 4438: (note & 7F) + transpose
    .note_mask = 0xFF,
    .first_event_command = 0xE0, // 436A
    .volume_events = false,
    .high_envelopes = gg_engine::DriverProfile::HighEnvelopes::adsr, // 4571
    // 4697: the lowest bit set picks the drum; volumes add to DD16.
    .drum_match = gg_engine::DriverProfile::DrumMatch::lowest_bit,
    .drum_volume_added = true,
    .drums = {{{1, 2, 0xE4}, {3, 2, 0xE4}, {2, 2, 0xE5}, {3, 2, 0xE4}, {3, 3, 0xE6}, {4, 4, 0xE4}}},
    .env_base_command = true, // E5 (4923)
    .volume_command = false,
    .loop_exit_command = false,
    .unused_commands_throw = true, // EF, F1: table entries into data at 492C
    .priority_waits_for_effects = true, // 4867
};

} // namespace sonic2_gg
