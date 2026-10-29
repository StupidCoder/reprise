#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"

namespace interceptor::game {
namespace {

constexpr std::uint32_t records_at = 0x1321C8;
constexpr int record_count = 54, record_size = 28;

} // namespace

Messages::Messages(const amiga::LoadedImage& program) {
    for (int n = 0; n < record_count; ++n) {
        const auto at = records_at + std::uint32_t(n * record_size);
        records_.push_back({program.u8(at), program.u8(at + record_size - 1)});
    }
}

void Messages::post(std::uint16_t code) {
    code_ = code;
    posted_ = true;
    const auto kind = code & 0xFF00;
    if (code & 0x2000) started_ = true;
    else if (kind == 0x4000 || kind == 0x4800) started_ = false;
}

// Eight frames: ticks $86, 0, 4, 0, 6, 0, 4, 0.
void Messages::clock() {
    const std::int8_t c = std::int8_t(clock_ - 1);
    clock_ = std::uint8_t(c);
    if (c <= 0) {
        clock_ = 8;
        ticks_ = 0x86;
    } else if (c == 4) {
        ticks_ = 6;
    } else if ((c & 3) == 2) {
        ticks_ = 4;
    } else if (c > 8) {
        clock_ = 8;
        ticks_ = 0;
    }
}

std::uint32_t Messages::update(bool stall_warning) {
    std::uint32_t beep = 0;
    std::uint16_t code = code_;
    if (!posted_) {
        // The warnings choose ($13AE40: only the stall's is rebuilt).
        if (!stall_warning) {
            if (!kept_) code = 0;
        } else {
            code = 0xC001; // STALL, blinking with blank
        }
    } else if (code_ == 0) {
        code = 0;
        posted_ = false;
    }
    if ((chosen_ & 0xFF) != (code & 0xFF)) {
        // Another record: its attribute, time and beep.
        code_ = chosen_ = code;
        started_ = false;
        kept_ = false;
        const auto& r = records_[std::size_t(code & 0xFF) % records_.size()];
        attribute_ = r.attribute;
        beeps_ = r.beeps;
        duration_ = r.attribute & 0x3F;
    }
    if (!(code_ & 0x8000)) {
        if (!(code_ & 0x4000)) {
            shown_ = code_;
        } else {
            // Shown for its time, counted on every fourth frame.
            if (!started_) {
                started_ = true;
                count_ = std::int8_t(duration_);
                shown_ = code_;
            }
            if ((ticks_ & 2) && --count_ < 0) {
                code_ = shown_ = 0;
                posted_ = false;
                started_ = false;
            }
        }
    } else {
        if (!started_) {
            started_ = true;
            count_ = std::int8_t(duration_);
        }
        const std::uint8_t tick = (code_ & 0x1000) ? 4 : 2;
        if (ticks_ & tick) {
            if (!lit_) {
                // The first half: the record itself.
                shown_ = code_;
                if (beeps_) beep |= warning::message;
                lit_ = true;
                if (duration_ != 0 && --count_ < 0) {
                    // Its blinks are over: it stays, plain.
                    shown_ = code_ & 0xFF;
                    code_ = chosen_ = shown_;
                    posted_ = false;
                    started_ = false;
                }
            } else {
                // The second half: the next record, or blank.
                shown_ = (code_ & 0xC000) == 0xC000 ? 0 : std::uint16_t(code_ + 1);
                lit_ = false;
                if (beeps_) beep |= warning::message_off;
            }
        }
    }
    ticks_ = 0;
    return beep;
}

// The line's colour by the attribute's top bits (taken from the original's
// screen: $4x messages in colour 4, $8x in colour 1).
int Messages::colour() const { return (attribute_ & 0x80) ? 1 : 4; }

void ground_warnings(const Aircraft& a, Messages& messages, Sounds& sounds) {
    const auto crash = [&] {
        if ((a.motion & flag::on_ground) || messages.code() == 0x9C06) return;
        sounds.warn(warning::crash);
        messages.post(0x9C06);
    };
    if (a.vy < 0) {
        const std::int32_t sink = -a.vy;
        if (a.handling & 2) {
            if ((a.gear & 0x0F) == 0 && (sink << 6) >= a.y && messages.code() != 0xD02A) {
                sounds.warn(warning::spin);
                messages.post(0xD02A);
            }
            return;
        }
        if (a.attitude.pitch >= 0x1E0 && a.attitude.pitch <= 0x3840 && (sink << 4) >= a.y) {
            crash();
            return;
        }
    }
    const int roll = a.attitude.roll;
    const bool banked = roll > 0x3840 ? roll <= 0x6BD0 : roll >= 0x4B0;
    if (banked && a.y <= 0x1800) crash();
}

} // namespace interceptor::game
