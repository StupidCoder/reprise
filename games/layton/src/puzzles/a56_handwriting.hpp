#pragma once
// The 3DS adventures' handwriting recogniser behind the written answers.
//
// How it works, in outline:
//   1. The strokes come as canvas-local points (0..127). Each stroke is cut
//      into straight segments between its corners, each with one of 16
//      directions and a length; segments of nearly the same direction are
//      merged and tiny ones dropped, and the strokes are linked by pen-up
//      segments.
//   2. A 4 x 4 grid of direction histograms over the whole trace picks the
//      character classes worth trying.
//   3. Every prototype of those classes with a fitting stroke count is
//      aligned with the segment directions (dynamic time warping with
//      pruning); a small rule program per prototype then checks geometric
//      relations between the aligned segments (which is higher, longer,
//      to the left ...), adding penalties.
//   4. The best candidates, filtered by the requested character classes,
//      come back best first with their scores.
//
// ROM identity: the fifth adventure's code segment, a third-party library
// at 0x120d94..0x121620, 0x23dd84..0x2410xx and 0x3c1eac..0x3d37b8 with the
// databases res/hwr/*.bin; the game reaches it through 0x1a7ea8 (setup)
// and 0x3a283c (recognise). Its constant tables are read from the code
// segment (the pack's code/code.bin). Research notes:
// .analysis/adventure56/docs/hwr.md.
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace picarat::adventure56::hwr {

struct Candidate {
    std::uint16_t code = 0;  // UTF-16
    std::uint16_t score = 0; // lower is better
};

class Recognizer {
public:
    // `database`: a res/hwr/*.bin file; `code`: the code segment (loaded at
    // 0x00100000) for the constant tables; the box size in pixels (clamped
    // to 50..127 like 0x1a7ea8) and the class mask of 0x120df0.
    Recognizer(std::vector<std::uint8_t> database, std::span<const std::uint8_t> code, int width,
               int height, unsigned classMask);
    ~Recognizer();

    // 0x3b7a54: the strokes as (x | 0x80, y) byte pairs, bit 7 clear on a
    // stroke's last point (0xff 0xff may end the list); up to 32 results.
    std::vector<Candidate> recognize(std::span<const std::uint8_t> strokes);

    // Research: the work structure after the strokes went in, in the
    // original's layout (for comparison with console memory dumps).
    std::vector<std::uint8_t> workImage() const;
    // Research: other state rendered at its console address (0x478b08
    // histogram, 0x457710 classes, 0x476a20 / 0x476e20 / 0x476f20
    // candidates, 0x477120 packed segments, 0x423608 bins); record
    // pointers as `dbBase` + file offset.
    std::vector<std::uint8_t> stateImage(std::uint32_t address, std::uint32_t dbBase) const;

private:
    struct Engine;
    std::unique_ptr<Engine> engine_;
};

} // namespace picarat::adventure56::hwr
