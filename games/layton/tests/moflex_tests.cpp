#include "test_assets.hpp"
// Movies of the 3DS adventures (src/video/moflex.*): demuxes and decodes
// original moflex files from the fifth adventure's pack (and the sixth's,
// when given) and compares plane digests recorded from the games' own frame
// routine and post filter, run in a local research oracle
// (.analysis/adventure56/tools/moflex_oracle.cpp, --digest).
#include "system/resources.hpp"
#include "video/moflex.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using namespace picarat;

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::uint64_t fnv(const std::uint8_t* plane, unsigned stride, unsigned width, unsigned height) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            h ^= plane[std::size_t(y) * stride + x];
            h *= 1099511628211ull;
        }
    }
    return h;
}

struct Expected {
    unsigned quantizer;
    std::uint64_t decoded[3], filtered[3]; // luma, U, V
};

using Digests = std::map<unsigned, Expected>;

// FNV-1a 64 over the visible planes, as printed by the ROM oracle.
const Digests kMovie1 = {
    {0,
     {27,
      {0x5e512667d3b80068, 0x089063e5f9f0e5ca, 0xe3c9807e6d58300c},
      {0xd5bc1ddde1644763, 0x2769f30769787a16, 0xcdc18a3f89bab670}}},
    {1,
     {31,
      {0xcc1c528cd0fccd76, 0x97b66c4a67855f2f, 0x9a221d2dd79c3b56},
      {0x7cc038618e604c4b, 0xae7bc42c7edf3fbe, 0x73c8c436919917bc}}},
    {101,
     {31,
      {0x7a35fe4f739dbe9c, 0x2b2b96804709f40e, 0xf5b21f056bb4c245},
      {0x16b19817ea687477, 0x0c5b500678d5ff0d, 0xba20d5a67ea4ffa1}}},
    {362,
     {31,
      {0x60a42bed2becf0d8, 0xb197ff8e2840791b, 0xc75882a008b47ad7},
      {0x714d73eddaba4799, 0xd44f47cda29082e9, 0xc054cc762222d164}}},
    {1355,
     {32,
      {0xdc4ffb4df710140d, 0xef262075a962fc3f, 0xfb4c9334f1ded093},
      {0xee08de8d2f83b5ce, 0x9b960b9418e766ad, 0x0408420201e64a9c}}},
};

// m070: the one mono movie (12 pictures per second); quantizer 18 leaves the
// post filter off (alpha 0).
const Digests kMovie70 = {
    {0,
     {18,
      {0x4c3e9a251e3ccf5a, 0x0c10cf377a83c4f4, 0x37c24751c3f93d95},
      {0x4c3e9a251e3ccf5a, 0x0c10cf377a83c4f4, 0x37c24751c3f93d95}}},
    {60,
     {24,
      {0x33d5c51c8248ae32, 0xe3dd5cf163aa4105, 0xa275dec4d521bb2a},
      {0x70e18ea0a6c326ae, 0xf1ebcb1405968ca9, 0x4f4a9b0a555a2f62}}},
    {120,
     {25,
      {0xc5d54013655b825a, 0x9fe500ef13cb9af8, 0x62015c8a0724aa5b},
      {0xfbebf466d3c00506, 0xd3699d2271aadff9, 0x52c83b45f65f3e93}}},
};

// Sixth adventure, m999.
const Digests kSixth999 = {
    {200,
     {19,
      {0x7d3887b2e512dc42, 0x93a0a3896ab693ea, 0xe875c81c3f467ce3},
      {0x7d3887b2e512dc42, 0x93a0a3896ab693ea, 0xe875c81c3f467ce3}}},
};

// Decodes every video frame of a movie and checks the listed ones; returns
// the number of video frames.
unsigned checkMovie(const std::filesystem::path& file, const moflex::Tables& tables,
                    const Digests& digests) {
    const auto data = readFile(file);
    moflex::Demuxer demuxer(data);
    const moflex::Stream* video = nullptr;
    for (const auto& s : demuxer.streams()) {
        if (s.kind == moflex::Stream::Kind::Video) {
            video = &s;
        }
    }
    require(video, file.string() + ": no video stream");
    moflex::VideoDecoder decoder(video->width, video->height, tables.video);
    moflex::Frame frame;
    unsigned index = 0, checked = 0;
    while (demuxer.next(frame)) {
        if (frame.stream != video->index) {
            continue;
        }
        decoder.decode(frame.data);
        if (auto it = digests.find(index); it != digests.end()) {
            const auto& e = it->second;
            const auto p = decoder.planes();
            const unsigned cw = p.width / 2, ch = p.height / 2;
            const std::string where = file.filename().string() + " frame " + std::to_string(index);
            require(decoder.quantizer() == e.quantizer, where + ": quantizer");
            require(fnv(p.luma, p.stride, p.width, p.height) == e.decoded[0], where + ": luma");
            require(fnv(p.u, p.stride, cw, ch) == e.decoded[1], where + ": U plane");
            require(fnv(p.v, p.stride, cw, ch) == e.decoded[2], where + ": V plane");
            const auto filtered = moflex::deblock(p, decoder.quantizer(), *tables.video);
            require(fnv(filtered.luma.data(), p.width, p.width, p.height) == e.filtered[0],
                    where + ": filtered luma");
            require(fnv(filtered.u.data(), cw, cw, ch) == e.filtered[1], where + ": filtered U");
            require(fnv(filtered.v.data(), cw, cw, ch) == e.filtered[2], where + ": filtered V");
            ++checked;
        }
        ++index;
    }
    require(checked == digests.size(), file.string() + ": frames missing");
    return index;
}
} // namespace

int main(int argc, char** argv) try {
    requireTestPack(argc, argv);
    const std::filesystem::path fifth = argv[1];
    const auto tables = moflex::Tables::fromCode(readFile(fifth / "code" / "code.bin"),
                                                 moflex::Tables::Game::Adventure5);

    // Container: movie 1 is a stereo pair stream (400x240, 48000/1001
    // pictures per second = 23.976 pairs) with a timeline stream.
    {
        const auto data = readFile(fifth / "mov" / "m001.moflex");
        moflex::Demuxer demuxer(data);
        require(demuxer.streams().size() == 2, "m001 streams");
        const auto& video = demuxer.streams()[0];
        require(video.kind == moflex::Stream::Kind::Video && video.width == 400 &&
                    video.height == 240 && video.rateNumerator == 48000 &&
                    video.rateDenominator == 1001 && video.layout == 0,
                "m001 video descriptor");
        require(demuxer.streams()[1].kind == moflex::Stream::Kind::Timeline, "m001 timeline");
        moflex::Frame frame;
        std::int64_t last = -1;
        unsigned pictures = 0;
        while (demuxer.next(frame)) {
            if (frame.stream == 0) {
                require(frame.time >= last, "m001 times go backwards");
                last = frame.time;
                ++pictures;
            }
        }
        require(pictures == 1356 && last == 28236542, "m001 frame count / last time");
    }
    require(checkMovie(fifth / "mov" / "m001.moflex", tables, kMovie1) == 1356, "m001 frames");
    require(checkMovie(fifth / "mov" / "m070.moflex", tables, kMovie70) == 121, "m070 frames");

    // The convenience player: pairs, rate and RGBA output (native Y2R model).
    {
        moflex::Movie movie(readFile(fifth / "mov" / "m001.moflex"), tables);
        require(movie.stereo() && movie.frameCount() == 678, "m001 pairs");
        require(movie.rate() > 23.97 && movie.rate() < 23.98, "m001 rate");
        std::vector<std::uint8_t> rgba;
        unsigned eye = 9;
        std::int64_t time = 0;
        require(movie.next(rgba, eye, time) && eye == 0 && rgba.size() == 400 * 240 * 4,
                "m001 first picture");
        require(movie.next(rgba, eye, time) && eye == 1, "m001 right eye");
        require(movie.audio() == nullptr, "m001 has no audio stream");
    }

    unsigned movies = 2;
    if (argc > 2 && std::filesystem::exists(std::filesystem::path(argv[2]) / "provenance.json")) {
        const std::filesystem::path sixth = argv[2];
        const auto tables6 = moflex::Tables::fromCode(readFile(sixth / "code" / "code.bin"),
                                                      moflex::Tables::Game::Adventure6);
        require(checkMovie(sixth / "mov" / "m999.moflex", tables6, kSixth999) == 520,
                "sixth m999 frames");
        ++movies;
    }
    std::cout << "moflex: " << movies << " movies decoded, ROM digests match\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "moflex_tests: " << e.what() << '\n';
    return 1;
}
