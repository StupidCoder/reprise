#pragma once
#include "../src/session/Progress.hpp"
#include "state/archive.hpp"

namespace captain_toad::reprise {
// Coarse checkpoints deliberately restart a scene. Transient actors, camera, animation,
// audio and GPU resources are reconstructed; there is no arbitrary-moment save contract.
struct Checkpoint {
    std::string magic = "Reprise Captain Toad checkpoint";
    int version = 1;
    std::string release = "00040000001cb200-eu";
    std::string scene = "Opening";
    session::Progress progress;
    template <class A>
    void persist(A& a) {
        a(magic, version, release, scene, progress.cleared, progress.gems, progress.season, progress.lives,
          progress.page, progress.revealed, progress.hasRevealed, progress.played, progress.lastPlayed,
          progress.returnedFromClear, progress.returnedFromStage);
    }
    static bool supported(const std::string& stage) {
        return stage == "PullUpStage" || stage == "RevolverRuinsStage" || stage == "TouchPuzzleMiniStage" ||
               stage == "FreshRuinsStage";
    }
    std::vector<uint8_t> encode() const {
        auto copy = *this;
        ::state::Writer a;
        a(copy);
        return a.bytes;
    }
    static Checkpoint decode(std::span<const uint8_t> bytes) {
        if (bytes.size() > 65536) {
            ::state::Reader::fail();
        }
        Checkpoint c;
        ::state::Reader a(bytes);
        a(c);
        a.finish();
        if (c.magic != "Reprise Captain Toad checkpoint" || c.version != 1 || c.release != "00040000001cb200-eu" ||
            (c.scene != "Opening" && c.scene != "Book" && !supported(c.scene)) || c.progress.season != 1 ||
            c.progress.page < 0 || c.progress.page > 100 || c.progress.lives < 0 || c.progress.lives > 999) {
            ::state::Reader::fail();
        }
        auto name = [](const std::string& s) {
            return s.size() <= 96 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
                       return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                   });
        };
        auto names = [&](const auto& v) {
            if (v.size() > 256) {
                ::state::Reader::fail();
            }
            for (const auto& s : v) {
                if (s.empty() || !name(s)) {
                    ::state::Reader::fail();
                }
            }
        };
        names(c.progress.cleared);
        names(c.progress.revealed);
        names(c.progress.played);
        if (!name(c.progress.lastPlayed) || c.progress.gems.size() > 256) {
            ::state::Reader::fail();
        }
        for (const auto& [s, bits] : c.progress.gems) {
            if (s.empty() || !name(s) || bits < 0 || bits > 7) {
                ::state::Reader::fail();
            }
        }
        return c;
    }
};
}   // namespace captain_toad::reprise
