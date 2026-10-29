#pragma once
#include "gba/display.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <deque>

// Copies of cartridge data into video memory, done in the vertical blank
// (worker 0800116C). A copy is a record (source, destination, bytes left)
// kept by its owner: a background's tile set, a sprite's tiles, a loaded
// picture. The queue (03002040, 32 entries) holds references to records, so
// the copy uses the record's values at vertical blank time, and a record
// queued twice is copied once (the first copy counts its bytes down to 0).
namespace sonic_advance_gba {

class TileCopyQueue;

struct TileCopy {
    // A destination outside video memory: the original's address 0 (the
    // BIOS, which ignores writes).
    static constexpr u32 nowhere = 0xFFFFFFFF;

    u32 from{};  // cartridge address
    u32 to{};    // video memory offset (or nowhere)
    u16 bytes{}; // bytes still to copy

    TileCopy() = default;
    TileCopy(const TileCopy&) = delete;
    TileCopy& operator=(const TileCopy&) = delete;
    ~TileCopy();

private:
    friend class TileCopyQueue;
    TileCopyQueue* queue_{};
};

class TileCopyQueue {
public:
    void push(TileCopy& c) {
        if (copies_.size() >= 32) {
            throw Unrecovered("tile copy queue overflow (32 entries)");
        }
        copies_.push_back({&c, 0, 0, 0});
        c.queue_ = this;
    }

    // The owner of a queued record went away before the copy. The original
    // still copies from the freed memory, which holds the record as it was
    // (the reconstructed parts reuse no such memory before the vertical
    // blank): the copy keeps the record's values.
    void forget(TileCopy& c) {
        for (Entry& e : copies_) {
            if (e.live == &c) {
                e = {nullptr, c.from, c.to, c.bytes};
            }
        }
    }

    void clear() {
        for (Entry& e : copies_) {
            if (e.live) {
                e.live->queue_ = nullptr;
            }
        }
        copies_.clear();
    }

    void run(const Assets& assets, gba::Display& d) {
        while (!copies_.empty()) {
            Entry e = copies_.front();
            copies_.pop_front();
            const u32 from = e.live ? e.live->from : e.from;
            const u32 to0 = e.live ? e.live->to : e.to;
            const u16 bytes = to0 == TileCopy::nowhere ? 0 : (e.live ? e.live->bytes : e.bytes);
            for (u32 i = 0; i < bytes; ++i) {
                const u32 to = to0 + i;
                if (to >= d.memory.size()) {
                    throw Unrecovered("tile copy beyond video memory");
                }
                d.memory[to] = assets.u8(from + i);
            }
            if (e.live) {
                e.live->bytes = 0; // (queued twice, it is copied once)
            }
        }
    }

private:
    struct Entry {
        TileCopy* live; // the owner's record, or null: the snapshot below
        u32 from, to;
        u16 bytes;
    };

    std::deque<Entry> copies_;
};

inline TileCopy::~TileCopy() {
    if (queue_) {
        queue_->forget(*this);
    }
}

} // namespace sonic_advance_gba
