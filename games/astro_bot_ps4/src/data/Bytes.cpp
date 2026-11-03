#include "data/Bytes.hpp"

#include <fstream>

namespace beacon::data {

bool loadFile(const std::filesystem::path& path, Bytes& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    out.resize(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())));
}

}   // namespace data
