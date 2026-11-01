#include "captain_toad_3ds/reprise/checkpoint.hpp"
#include <iostream>
using captain_toad::reprise::Checkpoint;
int main() {
    try {
        Checkpoint c;
        c.scene = "PullUpStage";
        c.progress.cleared.insert("Season1OpeningStage");
        c.progress.gems["PullUpStage"] = 5;
        c.progress.played.insert("PullUpStage");
        c.progress.lastPlayed = "PullUpStage";
        c.progress.returnedFromStage = true;
        auto bytes = c.encode();
        if (Checkpoint::decode(bytes).encode() != bytes) {
            throw std::runtime_error("Checkpoint round trip");
        }
        auto reject = [](auto b) {
            try {
                Checkpoint::decode(b);
            } catch (const std::exception&) {
                return;
            }
            throw std::runtime_error("Malformed checkpoint accepted");
        };
        for (size_t n = 0; n < bytes.size(); ++n) {
            reject(std::span(bytes).first(n));
        }
        auto tail = bytes;
        tail.push_back(0);
        reject(tail);
        for (int i = 0; i < 7; ++i) {
            auto bad = c;
            if (i == 0) {
                bad.version = 2;
            }
            if (i == 1) {
                bad.scene = "DoubleCherryStage";
            }
            if (i == 2) {
                bad.progress.gems["PullUpStage"] = 8;
            }
            if (i == 3) {
                bad.progress.lives = -1;
            }
            if (i == 4) {
                bad.progress.lastPlayed = "../Opening";
            }
            if (i == 5) {
                bad.release = "other";
            }
            if (i == 6) {
                bad.progress.season = 4;
            }
            reject(bad.encode());
        }
        std::cout << "Checkpoint values, truncation, schema, release, range and course validation passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
