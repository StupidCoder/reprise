#include "platform/file_source.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>

namespace platform {

FileSource::FileSource(const std::filesystem::path& path) {
    const auto name = path.u8string();
    auto* io = SDL_IOFromFile(reinterpret_cast<const char*>(name.c_str()), "rb");
    if (!io) {
        throw std::runtime_error("Cannot open " + path.filename().string() + " (" + SDL_GetError() + ").");
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        SDL_CloseIO(io);
        throw std::runtime_error("Cannot read the size of " + path.filename().string() + ".");
    }
    io_ = io;
    size_ = std::uint64_t(size);
}

FileSource::~FileSource() {
    SDL_CloseIO(static_cast<SDL_IOStream*>(io_));
}

void FileSource::read(std::uint64_t offset, std::span<std::uint8_t> out) {
    auto* io = static_cast<SDL_IOStream*>(io_);
    if (offset > size_ || out.size() > size_ - offset) {
        throw std::runtime_error("The image is shorter than its contents say.");
    }
    if (SDL_SeekIO(io, Sint64(offset), SDL_IO_SEEK_SET) < 0) {
        throw std::runtime_error(std::string("Cannot read the image (") + SDL_GetError() + ").");
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const std::size_t count = SDL_ReadIO(io, out.data() + done, out.size() - done);
        if (count == 0) {
            throw std::runtime_error(std::string("Cannot read the image (") + SDL_GetError() + ").");
        }
        done += count;
    }
}

} // namespace platform
