#pragma once
#include "graphics/graphics.hpp"
#include "video/mobiclip.hpp"
#include <memory>

namespace picarat {
// Plays an original MODS movie through the reconstructed Mobiclip decoder
// (mobiclip.hpp). Frames are decoded in order, as the movie scene does; a
// request for an earlier frame restarts from the first packet.
class MovieFrames {
public:
    // Opens movie/<name>.mods of the second adventure's pack in `assets`.
    MovieFrames(const std::filesystem::path& assets, const std::string& name);
    // Opens a MODS file with the decoder tables given.
    MovieFrames(const std::filesystem::path& file, std::shared_ptr<const mobiclip::Tables> tables);
    const Surface& frame(unsigned index);
    // Header rate is frames per second in 8.24 fixed point.
    unsigned count = 0, numerator = 0, denominator = 1u << 24;

private:
    Bytes data_;
    std::shared_ptr<const mobiclip::Tables> tables_;
    std::unique_ptr<mobiclip::Container> container_;
    std::unique_ptr<mobiclip::Decoder> decoder_;
    Surface image_;
    unsigned decoded_ = 0; // frames decoded so far
    unsigned cached_ = ~0u;
};
} // namespace picarat
