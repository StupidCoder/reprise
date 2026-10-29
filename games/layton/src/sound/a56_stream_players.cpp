// The stream players as the scripts see them. See a56_stream_players.hpp.
#include "sound/a56_stream_players.hpp"

namespace picarat::adventure56 {

bool StreamPlayers::play(int player, const std::string& name, const StreamInfo& info, int fade,
                         bool autoStart) {
    Player* pp = at(player);
    if (!pp) {
        return false;
    }
    Player& p = *pp;
    // 0x201d9c: nothing to do when the same stream is on (vtable +100),
    // a start asked for is already playing (+0x24) and no stop fade runs
    // (+0x38).
    if (p.handle && p.name == name && !(autoStart && !playing(player)) && !stopFading(player)) {
        return false;
    }
    stop(player, fade); // command 7
    // Command 6: a new handle, prepared.
    p = Player{};
    p.handle = true;
    p.name = name;
    p.info = info;
    if (autoStart) {
        start(player, 0); // command 1
    }
    return true;
}

void StreamPlayers::start(int player, int fade) {
    Player* pp = at(player);
    if (!pp) {
        return;
    }
    Player& p = *pp;
    if (!p.handle) {
        return;
    }
    if (p.pauseState == 1 || p.pauseState == 2) {
        // Resuming (0x295934 with 0): back to full volume over `fade`.
        p.pauseState = 3;
        p.pauseFade = {fade > 0 ? fade : 1, 0};
        p.stopping = false;
        return;
    }
    // 0x31a8f0: the sound starts, fading in over `fade`.
    p.started = true;
    if (fade > 0) {
        p.volumeFade = {fade, 0};
        p.fadeEnds = false;
    }
}

void StreamPlayers::pause(int player, int frames) {
    Player* pp = at(player);
    if (!pp) {
        return;
    }
    Player& p = *pp;
    if (!p.handle || p.stopping) {
        return;
    }
    p.pauseState = 1;
    p.pauseFade = {frames > 0 ? frames : 1, 0};
    p.stopping = true;
}

void StreamPlayers::stop(int player, int frames) {
    Player* pp = at(player);
    if (!pp) {
        return;
    }
    Player& p = *pp;
    if (!p.handle) {
        return;
    }
    p.stopping = true;
    if (frames <= 0) {
        p.handle = false;
        return;
    }
    p.volumeFade = {frames, 0};
    p.fadeEnds = true;
}

void StreamPlayers::tick() {
    for (Player& p : players_) {
        if (!p.handle) {
            continue;
        }
        if (p.volumeFade.remaining() > 0 && ++p.volumeFade.done >= p.volumeFade.total &&
            p.fadeEnds) {
            p.handle = false; // a stop's fade ran out: the sound ends
            continue;
        }
        if ((p.pauseState == 1 || p.pauseState == 3) && p.pauseFade.remaining() > 0) {
            if (++p.pauseFade.done >= p.pauseFade.total) {
                p.pauseState = p.pauseState == 1 ? 2 : 0;
            }
        }
        if (p.started && p.pauseState != 2) {
            p.position += double(p.info.rate) / kFrameRate;
            if (p.position >= double(p.info.frames)) {
                if (!p.info.loop || p.info.frames <= p.info.loopStart) {
                    p.handle = false; // played to its end
                } else {
                    const double span = double(p.info.frames - p.info.loopStart);
                    while (p.position >= double(p.info.frames)) {
                        p.position -= span;
                    }
                }
            }
        }
    }
}

bool StreamPlayers::preparing(int player) const {
    // A handle not yet prepared (vtable +0x14 of the handle) or a
    // preparation pending (+0x28): never, streams are prepared at once.
    (void)player;
    return false;
}

bool StreamPlayers::playing(int player) const {
    const Player* pp = at(player);
    if (!pp) {
        return false;
    }
    const Player& p = *pp;
    return p.handle && p.started &&
           (!(p.pauseState == 1 || p.pauseState == 2) || p.pauseFade.remaining() > 0);
}

bool StreamPlayers::fading(int player) const {
    const Player* pp = at(player);
    if (!pp) {
        return false;
    }
    const Player& p = *pp;
    return p.started && p.handle && (p.pauseFade.remaining() > 0 || p.volumeFade.remaining() > 0);
}

bool StreamPlayers::stopFading(int player) const {
    const Player* pp = at(player);
    if (!pp) {
        return false;
    }
    const Player& p = *pp;
    return p.stopping && p.handle && p.volumeFade.remaining() > 0;
}

bool StreamPlayers::paused(int player) const {
    const Player* p = at(player);
    return p && p->handle && (p->pauseState == 1 || p->pauseState == 2);
}

} // namespace picarat::adventure56
