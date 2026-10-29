// See console_id.hpp.
#include "system/console_id.hpp"
#include <fstream>
#include <iterator>
#include <random>

namespace picarat {

std::array<std::uint8_t, 6> consoleAddress(const std::filesystem::path& directory) {
    std::array<std::uint8_t, 6> mac{};
    const auto file = directory / "console.id";
    if (std::ifstream in{file, std::ios::binary}) {
        const std::string b{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (b.size() == mac.size()) {
            for (std::size_t i = 0; i < mac.size(); ++i) {
                mac[i] = std::uint8_t(b[i]);
            }
            return mac;
        }
    }
    std::random_device random;
    for (auto& m : mac) {
        m = std::uint8_t(random());
    }
    if (!directory.empty()) {
        std::filesystem::create_directories(directory);
    }
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(mac.data()), std::streamsize(mac.size()));
    return mac;
}

std::string consoleCode(const std::array<std::uint8_t, 6>& m, unsigned seed) {
    const unsigned k = seed;
    const unsigned r0 = (m[0] + m[3]) & 255u;
    const unsigned r3 = (m[1] + m[3] - m[4]) & 255u;
    const unsigned r9 = (m[2] + m[3] + m[4] + m[5]) & 255u;
    const std::array<unsigned, 8> b{r0 * k,
                                    r3 + k,
                                    r9 * k,
                                    m[3] - k,
                                    m[4] * k,
                                    m[5] + k,
                                    ((r0 + r3 + r9) & 255u) * k,
                                    ((m[3] - m[4] - m[5]) & 255u) - k};
    std::string code(8, '0');
    for (std::size_t i = 0; i < 8; ++i) {
        const unsigned v = b[i] & 255u;
        code[i] = i % 2 == 0 ? char('0' + v % 9) : char('A' + v % 7);
    }
    return code;
}

} // namespace picarat
