// The movie lists and their thumbnails. See movies.hpp.
#include "shell/movies.hpp"
#include "graphics/a3_assets.hpp"
#include "graphics/graphics.hpp"
#include "system/resources.hpp"

namespace picarat::shell {

namespace {

// Second adventure: overlay 23 (loaded at 020efda0), the movies' events.
constexpr std::uint32_t kBonusOverlayLoad = 0x020efda0, kMovieEvents2 = 0x020f3e3c;
constexpr int kMovies2 = 23;

// Third adventure: overlay 29 (loaded at 0211df00), its button -> movie table.
constexpr int kTheatreOverlay = 29;
constexpr std::uint32_t kTheatreOverlayLoad = 0x0211df00, kMovieTable3 = 0x0211e130;
constexpr int kMovies3 = 37; // btn_movie1..37 (lt3_scrt_movie, CREATE_CHARA)

// An RGB555 picture with its coverage bit as an RGBA image.
platform::Image toImage(const Surface& s) {
    platform::Image out(int(s.width), int(s.height));
    for (std::size_t i = 0; i < s.pixels.size(); ++i) {
        const std::uint16_t p = s.pixels[i];
        auto channel = [&](int shift) {
            const int v = (p >> shift) & 31;
            return std::uint8_t(v << 3 | v >> 2);
        };
        out.pixels[i] = {channel(0), channel(5), channel(10), std::uint8_t(p & 0x8000 ? 255 : 0)};
    }
    return out;
}

} // namespace

std::vector<MovieEntry> adventure2Movies(const std::filesystem::path& pack) {
    const auto overlay = pack / "code" / "overlay23.bin";
    if (!fileExists(overlay) ||
        !fileExists(pack / "ani" / "menu" / "secret" / "movie_icon.arc.l2sp")) {
        return {};
    }
    const Bytes code = readFile(overlay);
    auto word = [&](std::uint32_t address) {
        const std::size_t i = address - kBonusOverlayLoad;
        return i + 4 <= code.size()
                   ? std::uint32_t(code[i]) | std::uint32_t(code[i + 1]) << 8 |
                         std::uint32_t(code[i + 2]) << 16 | std::uint32_t(code[i + 3]) << 24
                   : 0u;
    };
    GraphicsAssets assets(pack);
    const Sprite& icons = assets.sprite("menu/secret/movie_icon.arc");
    std::vector<MovieEntry> movies;
    for (int i = 1; i <= kMovies2; ++i) {
        MovieEntry movie;
        movie.id = i;
        movie.event = word(kMovieEvents2 + 4 * std::uint32_t(i - 1));
        // 020f2c44: the button's animation is named by its number.
        const auto name = icons.animationNames.find(std::to_string(i));
        if (name != icons.animationNames.end() && name->second < icons.animations.size() &&
            !icons.animations[name->second].empty()) {
            const unsigned image = icons.animations[name->second].front().image;
            if (image < icons.images.size()) {
                movie.thumbnail = toImage(icons.images[image]);
            }
        }
        movies.push_back(std::move(movie));
    }
    return movies;
}

std::vector<MovieEntry> adventure3Movies(const std::filesystem::path& pack) {
    adventure3::Assets assets(pack);
    const std::string icons = "menu/?/s_movie.cani";
    const adventure3::Animation* animation =
        assets.exists(icons) ? &assets.animation(icons) : nullptr;
    std::vector<MovieEntry> movies;
    for (int i = 0; i < kMovies3; ++i) {
        MovieEntry movie;
        movie.id = int(assets.codeWord(kTheatreOverlay, kTheatreOverlayLoad, kMovieTable3 + 4 * i));
        // BTN_MOVIE_N: the button shows "i<nn>_off" (nn = button 1..37).
        char name[16];
        std::snprintf(name, sizeof name, "i%02d_off", i + 1);
        const auto* sequence = animation ? animation->sequence(name) : nullptr;
        if (sequence && !sequence->steps.empty()) {
            const unsigned frame = sequence->steps.front().frame;
            const auto& bounds = animation->frames[frame].bounds;
            Surface picture{
                unsigned(bounds.width), unsigned(bounds.height),
                std::vector<std::uint16_t>(std::size_t(bounds.width) * bounds.height, 0)};
            animation->drawFrame(picture, frame, 0, 0);
            movie.thumbnail = toImage(picture);
        }
        movies.push_back(std::move(movie));
    }
    return movies;
}

std::vector<MovieEntry> adventure1Movies(const std::filesystem::path& pack) {
    if (!fileExists(pack / "ani" / "movie_file1.arc.l2sp")) {
        return {};
    }
    GraphicsAssets assets(pack);
    const Sprite& icons = assets.sprite("movie_file1.arc");
    std::vector<MovieEntry> movies;
    for (int i = 1; i <= 16; ++i) {
        MovieEntry movie;
        movie.id = i;
        // 0202f944: button n shows animation n.
        if (std::size_t(i) < icons.animations.size() && !icons.animations[std::size_t(i)].empty()) {
            const unsigned image = icons.animations[std::size_t(i)].front().image;
            if (image < icons.images.size()) {
                movie.thumbnail = toImage(icons.images[image]);
            }
        }
        movies.push_back(std::move(movie));
    }
    return movies;
}

} // namespace picarat::shell
