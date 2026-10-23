// Recovered from the loader's pulse handler, which the KERNAL loads with the
// tape header to $0351-$03F5 (header bytes 21..191), and its setup code in the
// boot program at $080D. Evidence: .analysis/fort_apocalypse_c64/tape.md.

#include "fort_apocalypse_c64/import/novaload.hpp"
#include <stdexcept>
#include <string>

namespace fort_apocalypse::import {
namespace {

// Bit decision ($0354-$035F). CIA 1 timer A is latched to $03F4 and restarted
// in continuous mode on every pulse; the handler reads the timer's high byte
// and takes bit 1 of (high byte EOR 2) as the data bit. The timer counts
// $03F4 down to 0 and reloads, so a pulse is a 1 when $03F4 minus its length
// (modulo the reload period) has a high byte of 0 or 1: a length between 501
// and 1012 cycles. Shorter pulses, and longer ones that wrapped the timer,
// read as 0. Interrupt latency (a few cycles) is not modelled; the tape's
// pulses (296 and 672 cycles) are far from both edges.
int pulse_bit(std::uint32_t cycles) {
    constexpr std::uint32_t latch = 0x03F4;
    const std::uint32_t timer = latch - cycles % (latch + 1);
    return (((timer >> 8) ^ 2) >> 1) & 1;
}

// Byte assembly ($0360-$0364, $037D). Bits are rotated in from the top, so
// the first bit of a byte is its least significant bit. The register starts
// at $7F; the 0 that drops out of the bottom after eight bits marks a
// complete byte. Every state resets the register except the pilot search on
// a mismatch, which lets the register slide one bit at a time until a run of
// 0 bits and a 1 bit read as $80. (The boot code does not initialise the
// register; starting at $7F is our assumption and only shifts where the
// pilot search begins.)
class ByteAssembler {
public:
    // Adds one bit; returns true when a byte is complete.
    bool push(int bit) {
        const bool complete = (shift_ & 1) == 0;
        shift_ = std::uint8_t(shift_ >> 1 | bit << 7);
        return complete;
    }
    std::uint8_t value() const { return shift_; }
    void restart() { shift_ = 0x7F; }

private:
    std::uint8_t shift_{0x7F};
};

// The handler's states; the original keeps them as a self-modified branch
// offset at $0365 (and, for end mode, $03A2).
enum class State { pilot, sync, checksum, page, page_after_end_page, data };

constexpr std::uint8_t pilot_byte = 0x80, sync_byte = 0xAA, key_byte = 0x55, end_page = 0xF0;

std::string hex(int value) {
    constexpr char digits[] = "0123456789ABCDEF";
    return std::string{'$', digits[value >> 4 & 15], digits[value & 15]};
}
} // namespace

NovaloadStream read_novaload(const c64::TapeImage& tape, std::size_t from) {
    NovaloadStream stream;
    ByteAssembler assembler;
    State state = State::pilot;
    std::uint8_t checksum = key_byte; // $AA, set to $55 by the boot code
    PageRecord record;
    int offset = 0;        // $FD, the position in the current page
    bool end_mode = false; // page $F0 was announced

    for (std::size_t i = from; i < tape.pulses.size(); ++i) {
        const auto& pulse = tape.pulses[i];
        if (pulse.pause) {
            // Silence produces no pulses for the handler; once a stream has
            // started it means the recording broke off.
            if (state != State::pilot) {
                break;
            }
            continue;
        }
        if (!assembler.push(pulse_bit(pulse.cycles))) {
            continue;
        }
        const std::uint8_t value = assembler.value();
        switch (state) {
        case State::pilot: // $0368
            if (value != pilot_byte) {
                continue; // no register restart: keep sliding
            }
            state = State::sync;
            break;
        case State::sync: // $0370
            state = value == sync_byte ? State::checksum : State::pilot;
            break;
        case State::checksum: // $039D: the key byte first, then each record's sum
            if (value != checksum) {
                throw std::runtime_error(
                    stream.records.empty()
                        ? "The fast loader's key byte is wrong; this is not the expected tape."
                        : "The tape is damaged: page " + hex(record.page) +
                              " has a wrong checksum.");
            }
            state = end_mode ? State::page_after_end_page : State::page;
            break;
        case State::page_after_end_page: // $03B1
            if (value == 0) {
                stream.complete = true;
                stream.end_pulse = i + 1;
                return stream;
            }
            [[fallthrough]];
        case State::page: // $03A5, $03D0
            if (state == State::page && value == end_page) {
                end_mode = true; // $03AA: the next page states become $03B1
            }
            record.page = value;
            checksum = value;
            state = State::data;
            break;
        case State::data: // $0388
            record.bytes[std::size_t(offset)] = value;
            checksum = std::uint8_t(checksum + value); // ADC with carry cleared, carry dropped
            offset = (offset + 1) & 0xFF;
            if (offset == 0) {
                stream.records.push_back(record);
                state = State::checksum;
            }
            break;
        }
        assembler.restart();
    }
    if (stream.records.empty()) {
        throw std::runtime_error("No fast loader data was found after the boot program.");
    }
    throw std::runtime_error("The tape ends before the fast loader finished loading.");
}

} // namespace fort_apocalypse::import
