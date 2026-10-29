// This cartridge's revision of the shared sound driver; see sound_driver.hpp.

#include "triple_trouble_gg/audio/sound_driver.hpp"

namespace triple_trouble_gg {

const gg_engine::DriverProfile sound_profile{
    .slot2_bank = 0x03,
    .silence_writes = 0x509F,
    .end_command = 0x4DD7,
    .music_channel_ids = 0x4EE7,
    .note_periods = 0x50D1,
    .envelope_tables = 0x4151,
    .priorities = 0x4B39,
    .song_headers = 0x4B9C,
    .effect_headers = 0x4BE2,
    .song_tempos = 0,
    .modulation_tables = 0x4000, // 4F89
    .request_slots = 3,          // DE04-DE06 (4CC0)
    .default_slot = 1,           // effects DE05; songs DE06 (slot 2)
    .first_effect = 0xB0,        // 81-AF songs, B0-DF effects
    .first_command = 0xE0,
    .tempo = gg_engine::DriverProfile::Tempo::accumulate, // 4CA8
    .fade_step = 2,                                       // 4CF1
    .fade_ends_drums = true,
    .note_base = 0x81, // 50A9: (note - 81 + transpose) & 7F
    .note_mask = 0x7F,
    .first_event_command = 0xE1, // 4FCC; D8-E0 set the volume (4FEC)
    .volume_events = true,
    .high_envelopes = gg_engine::DriverProfile::HighEnvelopes::note_off_level, // 5255
    // 52F8: drums 81, 82, 84, 88, 90, A0 exactly; volumes as given.
    .drum_match = gg_engine::DriverProfile::DrumMatch::exact,
    .drum_volume_added = false,
    .drums = {{{0x10, 3, 0xE4},
               {0x03, 3, 0xE4},
               {0x18, 2, 0xE6},
               {0x19, 2, 0xE5},
               {0x1A, 2, 0xE5},
               {0x11, 3, 0xE4}}},
    .env_base_command = false,
    .volume_command = true,    // E9 (53EA)
    .loop_exit_command = true, // FC (552A)
    .unused_commands_throw = false,
    .priority_waits_for_effects = false, // 54CD clears it always
};

} // namespace triple_trouble_gg
