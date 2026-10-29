// See movie.hpp.
#include "video/movie.hpp"
#include <stdexcept>

namespace picarat {
MovieFrames::MovieFrames(const std::filesystem::path& assets, const std::string& name)
    : MovieFrames(assets / "movie" / (name + ".mods"),
                  mobiclip::Tables::fromOverlay(readFile(assets / "code" / "overlay1.bin"))) {}

MovieFrames::MovieFrames(const std::filesystem::path& file,
                         std::shared_ptr<const mobiclip::Tables> tables)
    : data_(readFile(file)), tables_(std::move(tables)) {
    container_ = std::make_unique<mobiclip::Container>(data_);
    const auto& header = container_->header();
    if (header.width != 256 || header.height != 192 || !header.frames || header.frames > 100000 ||
        !header.rate) {
        throw std::runtime_error("movie dimensions/timing");
    }
    count = header.frames;
    numerator = header.rate;
    decoder_ = std::make_unique<mobiclip::Decoder>(header.width, header.height, tables_);
    image_.width = header.width;
    image_.height = header.height;
    image_.pixels.resize(header.width * header.height);
}

const Surface& MovieFrames::frame(unsigned index) {
    if (index == cached_) {
        return image_;
    }
    if (index >= count) {
        throw std::out_of_range("movie frame");
    }
    if (index + 1 < decoded_) {
        container_->rewind();
        decoder_ = std::make_unique<mobiclip::Decoder>(image_.width, image_.height, tables_);
        decoded_ = 0;
    }
    mobiclip::Packet packet;
    while (decoded_ <= index) {
        if (!container_->next(packet)) {
            throw std::runtime_error("movie ended before its declared frame count");
        }
        decoder_->decode(packet.data);
        ++decoded_;
    }
    // The movie scene shows the 0x0205b178 RGB555 conversion (bit 15 set).
    decoder_->convert(image_.pixels.data(), image_.width);
    cached_ = index;
    return image_;
}
} // namespace picarat
