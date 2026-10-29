#pragma once
// The handwriting recogniser behind the written-answer puzzles (overlay 42).
//
// How it works, in outline:
//   1. Each stroke (at most six) is resampled to 32 evenly spaced points.
//   2. The strokes are normalised: moved, scaled and, if requested, rotated
//      and de-slanted, relative to the writing guides (baseline and help
//      line of the box).
//   3. The database holds prototypes of every character the recogniser
//      knows, grouped by number of strokes, each stroke stored as a shape.
//      The input is matched against the prototypes with the same stroke
//      count (and in different stroke orders and directions); every match
//      gets a distance score.
//   4. The best candidates are kept in a ranked list, rescored, filtered by
//      the requested character classes (digits, letters...) and returned,
//      best first, with upper- or lower-case variants where needed.
//
//
// ROM identity: ARM9 overlay 0 (FAT 0, ROM 0x34a00+0x7afc, load 0205a120),
// a third-party pattern recognizer linked into the game together with its
// prototype database /data_lt2/decuma/_databas_le.bin. The game reaches it
// through ov2 02077c50 (database load) and 02077c9c (recognize). Everything in
// this module is recovered from that code; guest addresses are cited per
// function. Guest pointers inside work structures become typed native fields.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picarat::handwriting {

struct Point {
    std::int16_t x = 0, y = 0;
};

using Stroke = std::vector<Point>;

// Settings passed by ov42 (box object +0x18, filled by 020f9510/020f9694).
struct Settings {
    std::int32_t categories = 0x10;  // +0x00 category mask (digits 0x10, letters 0x0c...).
    std::int32_t categories2 = -1;   // +0x04 secondary mask.
    std::int32_t rotation = 0;       // +0x08 degrees, 0..359.
    std::int32_t slant = 0;          // +0x0c 0..1024.
    std::int32_t offsetX = 0;        // +0x10 added before the coordinate shift.
    std::int32_t offsetY = 0;        // +0x14
    std::int32_t shift = 0;          // +0x18 coordinate shift.
    std::int32_t baseline = 0x50;    // +0x1c writing guide (lower).
    std::int32_t helpline = 0x28;    // +0x20 writing guide (upper).
    std::int32_t userDatabase = 0;   // +0x28 optional second database; none in this game.
    std::int32_t characterTable = 0; // +0x2c optional replacement table; none in this game.
};

struct Result {
    std::u16string text;    // Recognized symbol string (case folded when requested).
    std::int32_t score = 0; // Clamped sum of the candidate's two scores (+0x20,+0x22).
};

// Native view of the 0x4c-byte candidate record, for diagnostics/validation.
struct CandidateDebug {
    std::int16_t index = 0;
    std::uint8_t strokes = 0;
    std::int16_t score = 0, penalty = 0;
    std::u16string text;
    std::int32_t caseKind = 0;
};

class Recognizer {
public:
    explicit Recognizer(std::span<const std::uint8_t> database);
    // 0205a12c: returns the recognizer status (0 = success) and up to
    // `capacity` results. More than six strokes produce no candidates.
    int recognize(std::span<const Stroke> strokes, const Settings& settings, unsigned capacity,
                  std::vector<Result>& out);
    std::vector<CandidateDebug> lastCandidates; // Final ws+0x20 records.
private:
    std::vector<std::uint8_t> database_;
};

} // namespace picarat::handwriting
