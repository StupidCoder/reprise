#include "cannon_fodder_amiga/sound/board.hpp"

namespace fodder::sound {

void SoundBoard::play(const game::World& world, const game::GeometryTables& geometry, const game::SoundRequest& request,
                      Rjp1& player) {
    if (silenced) return;
    const int channel = ((last_channel + 1) & 3) | 2;
    last_channel = channel;
    auto& p = priority[std::size_t(channel)];
    if (request.priority < p && busy[std::size_t(channel)] != 0) return; // the channel's sound is more important
    p = request.priority & 0xff;
    busy[std::size_t(channel)] = 12;
    int volume = 0x40;
    if (request.slot >= 0) {
        const auto& o = world.objects.slots[std::size_t(request.slot)];
        if (!o.on_screen) {
            const int dx = world.camera.x + 0x98 - std::int16_t(std::uint32_t(o.x) >> 16);
            const int dy = world.camera.y + 0x70 - std::int16_t(std::uint32_t(o.y) >> 16);
            const int far = geometry.distance(dx, dy, 0x280, 0x280) / 16;
            volume = 0x28 > far ? 0x28 - far : 4;
        }
    }
    player.set_channel_volume_raw(channel, volume);
    player.start_effect(channel, request.effect);
}

void SoundBoard::play_all(game::World& world, const game::GeometryTables& geometry, Rjp1& player) {
    for (const auto& r : world.sounds) play(world, geometry, r, player);
    world.sounds.clear();
}

void SoundBoard::tick() {
    for (auto& b : busy) {
        if (b != 0) --b;
    }
}

} // namespace fodder::sound
