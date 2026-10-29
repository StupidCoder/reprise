// The adventures embedded in another application. See layton_embed.hpp.
#include "layton_embed.hpp"
#include "graphics/graphics.hpp"
#include "import/rom_image.hpp"
#include "import/rom_import.hpp"
#include "import/rom_source.hpp"
#include "shell/game_session.hpp"
#include "shell/library.hpp"
#include "shell/screen_layout.hpp"
#include "system/a5_saves.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace picarat::embed {
namespace fs = std::filesystem;

namespace {

import::Game gameOf(Title title) {
    switch (title) {
    case Title::CuriousVillage:
        return import::Game::Adventure1;
    case Title::PandorasBox:
        return import::Game::Adventure2;
    case Title::LostFuture:
        return import::Game::Adventure3;
    case Title::SpectresCall:
        return import::Game::Adventure4;
    case Title::MiracleMask:
        return import::Game::Adventure5;
    case Title::AzranLegacy:
        return import::Game::Adventure6;
    }
    return import::Game::Adventure2;
}

bool ctr(Title title) { return title == Title::MiracleMask || title == Title::AzranLegacy; }

// The pack layout each game's player reads (import::k...PackSchema) and a
// file every complete pack of it holds.
int schemaOf(Title title) {
    switch (title) {
    case Title::CuriousVillage:
        return import::kAdventure1PackSchema;
    case Title::PandorasBox:
        return import::kPackSchema;
    case Title::LostFuture:
        return import::kAdventure3PackSchema;
    case Title::SpectresCall:
        return import::kAdventure4PackSchema;
    case Title::MiracleMask:
        return import::kAdventure5PackSchema;
    case Title::AzranLegacy:
        return import::kAdventure6PackSchema;
    }
    return 0;
}

const char* requiredFile(Title title) {
    switch (title) {
    case Title::CuriousVillage:
        return "code/arm9.bin";
    case Title::PandorasBox:
        return "sound/SY_999.SWD";
    case Title::LostFuture:
        return "provenance.json";
    case Title::SpectresCall:
        return "main/fnt/main.lf";
    case Title::MiracleMask:
    case Title::AzranLegacy:
        return "code/code.bin";
    }
    return "import.json";
}

// Where each recognised cartridge comes from (kKnownRoms: the European
// multi-language first adventure, the German editions of the others).
const char* regionOf(Title title) {
    return title == Title::CuriousVillage ? "Europe" : "Germany";
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The value after "key": in the small import.json the importer writes.
std::string jsonValue(const std::string& json, const std::string& key) {
    const auto at = json.find("\"" + key + "\"");
    if (at == std::string::npos) {
        return {};
    }
    auto start = json.find(':', at);
    if (start == std::string::npos) {
        return {};
    }
    start = json.find_first_not_of(" \t\r\n\"", start + 1);
    if (start == std::string::npos) {
        return {};
    }
    const auto end = json.find_first_of(",\"\r\n}", start);
    return json.substr(start, end - start);
}

// The engine's random-access reader over the host's image.
class Source final : public import::RomSource {
public:
    explicit Source(ImageSource& image) : image_(image) {}

    std::uint64_t size() const override { return image_.size(); }

    void read(std::uint64_t offset, std::span<std::uint8_t> out) override { image_.read(offset, out); }

private:
    ImageSource& image_;
};

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}

// RGB555 with coverage in bit 15 to RGBA.
Picture picture(std::string title, const Surface& surface) {
    Picture out{std::move(title), int(surface.width), int(surface.height), {}};
    out.rgba.resize(std::size_t(out.width) * std::size_t(out.height) * 4);
    for (std::size_t i = 0; i < surface.pixels.size(); ++i) {
        const std::uint16_t c = surface.pixels[i];
        auto channel = [](unsigned v) { return std::uint8_t((v << 3) | (v >> 2)); };
        out.rgba[i * 4 + 0] = channel(c & 31u);
        out.rgba[i * 4 + 1] = channel((c >> 5) & 31u);
        out.rgba[i * 4 + 2] = channel((c >> 10) & 31u);
        out.rgba[i * 4 + 3] = 255;
    }
    return out;
}

// The session's own files in the progress folder.
fs::path checkpointFile(Title title, const fs::path& progress) {
    switch (title) {
    case Title::CuriousVillage:
    case Title::LostFuture:  // (the slots' checkpoint record, adventure3::SaveSlots::kCheckpoint)
    case Title::MiracleMask: // (adventure5::writeCheckpoint)
        return progress / "checkpoint.sav";
    case Title::PandorasBox:
        return progress / "checkpoint.l2cp";
    default:
        return {};
    }
}

std::vector<std::uint8_t> readBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + file.filename().string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& file, std::span<const std::uint8_t> bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) {
        throw std::runtime_error("cannot write " + file.filename().string());
    }
}
} // namespace

Screens screens(Title title) {
    if (ctr(title)) {
        return {{400, 240}, {320, 240}};
    }
    return {{256, 192}, {256, 192}};
}

Layout layout(double width, double height, const Screens& screens) {
    const shell::ScreenGeometry g{screens.top.width, screens.top.height, screens.bottom.width,
                                  screens.bottom.height};
    const auto l = shell::screenLayout(width, height, g);
    return {{l.top.x, l.top.y, l.top.width, l.top.height},
            {l.bottom.x, l.bottom.y, l.bottom.width, l.bottom.height},
            l.horizontal,
            l.scale};
}

// --- Import ------------------------------------------------------------------

std::optional<Title> identify(ImageSource& image) {
    if (image.size() < 0x200) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 0x200> head{};
    image.read(0, head);
    std::string code;
    if (std::equal(head.begin() + 0x100, head.begin() + 0x104, std::string_view("NCSD").begin())) {
        // A 3DS cartridge image: the first partition's product code
        // ("CTR-P-AKKD"), whose last four letters are the game code.
        const std::uint64_t at = std::uint64_t(le32(head.data() + 0x120)) * 0x200;
        if (at + 0x200 > image.size()) {
            return std::nullopt;
        }
        std::array<std::uint8_t, 0x200> ncch{};
        image.read(at, ncch);
        if (!std::equal(ncch.begin() + 0x100, ncch.begin() + 0x104, std::string_view("NCCH").begin())) {
            return std::nullopt;
        }
        const std::string product(reinterpret_cast<const char*>(ncch.data() + 0x150), 16);
        const auto end = product.find('\0');
        const std::string trimmed = product.substr(0, end);
        if (trimmed.size() < 4) {
            return std::nullopt;
        }
        code = trimmed.substr(trimmed.size() - 4);
    } else {
        code.assign(reinterpret_cast<const char*>(head.data() + 0x0c), 4); // the DS header's
    }
    for (const Title t : {Title::CuriousVillage, Title::PandorasBox, Title::LostFuture,
                          Title::SpectresCall, Title::MiracleMask, Title::AzranLegacy}) {
        if (import::knownRom(gameOf(t)).gameCode == code) {
            return t;
        }
    }
    return std::nullopt;
}

std::string importImage(Title title, ImageSource& image, const fs::path& pack,
                        const std::function<void(double)>& progress) {
    Source source(image);
    const auto& known = import::knownRom(gameOf(title));
    try {
        import::importRom(
            source,
            [&](import::Game game) {
                if (game != gameOf(title)) {
                    throw std::runtime_error(std::string("This is ") + import::knownRom(game).title +
                                             ", not " + known.title + ".");
                }
                return pack;
            },
            [&](double fraction, const std::string&) {
                if (progress) {
                    progress(fraction);
                }
            });
    } catch (const import::ImportCancelled&) {
        throw std::runtime_error("The import was cancelled.");
    }
    const std::string problem = checkPack(title, pack);
    if (!problem.empty()) {
        throw std::runtime_error(problem);
    }
    return packRelease(title, pack);
}

std::string checkPack(Title title, const fs::path& pack) {
    const auto& known = import::knownRom(gameOf(title));
    const std::string meta = readText(pack / "import.json");
    if (meta.empty()) {
        return std::string(known.title) + ": nothing imported.";
    }
    if (jsonValue(meta, "rom_sha256") != known.sha256) {
        return std::string(known.title) + ": the import comes from another release; import the cartridge again.";
    }
    if (jsonValue(meta, "schema") != std::to_string(schemaOf(title))) {
        return std::string(known.title) +
               ": the import has an older layout this version cannot read; import the cartridge again.";
    }
    std::error_code error;
    if (!fs::exists(pack / requiredFile(title), error) || !fs::exists(pack / "provenance.json", error)) {
        return std::string(known.title) + ": the import is incomplete; import the cartridge again.";
    }
    if (title == Title::CuriousVillage && languages(title, pack).empty()) {
        return std::string(known.title) + ": the import has no languages; import the cartridge again.";
    }
    return {};
}

std::string packRelease(Title title, const fs::path& pack) {
    const auto& known = import::knownRom(gameOf(title));
    std::string release = std::string(known.gameCode) + ", " + regionOf(title);
    if (title == Title::CuriousVillage) {
        std::string names;
        for (const auto& [code, name] : languages(title, pack)) {
            names += (names.empty() ? "" : ", ") + name;
        }
        if (!names.empty()) {
            release += " (" + names + ")";
        }
    }
    return release;
}

fs::path legacyDataDirectory() {
    // SDL's preference folder of the standalone app ("Picarat").
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / "Library" / "Application Support" / "Picarat";
    }
#elif defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA")) {
        return fs::path(appData) / "Picarat";
    }
#elif !defined(__ANDROID__)
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "Picarat";
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".local" / "share" / "Picarat";
    }
#endif
    return {}; // (Android: another app's private storage, out of reach)
}

std::string legacyPackFolder(Title title) {
    switch (title) {
    case Title::CuriousVillage:
        return shell::kAdventure1PackFolder;
    case Title::PandorasBox:
        return shell::kPackFolder;
    case Title::LostFuture:
        return shell::kAdventure3PackFolder;
    case Title::SpectresCall:
        return shell::kAdventure4PackFolder;
    case Title::MiracleMask:
        return shell::kAdventure5PackFolder;
    case Title::AzranLegacy:
        return shell::kAdventure6PackFolder;
    }
    return {};
}

std::vector<std::pair<fs::path, std::string>> legacyProgress(Title title) {
    const fs::path root = legacyDataDirectory();
    std::vector<std::pair<fs::path, std::string>> files;
    if (root.empty()) {
        return files;
    }
    std::error_code error;
    // The console's identity (codes the games derive from it) goes with the
    // cartridge saves of the DS games that read it.
    auto add = [&](const fs::path& from, const std::string& to) {
        if (fs::is_regular_file(from, error)) {
            files.emplace_back(from, to);
        }
    };
    auto addSlots = [&](const fs::path& folder) {
        if (!fs::is_directory(folder, error)) {
            return;
        }
        for (const auto& entry : fs::directory_iterator(folder, error)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_regular_file() && name != "checkpoint.sav") {
                files.emplace_back(entry.path(), name);
            }
        }
    };
    switch (title) {
    case Title::CuriousVillage:
        add(root / "a5fp-backup.sav", "backup.sav");
        add(root / "console.id", "console.id");
        break;
    case Title::PandorasBox:
        add(root / "backup.sav", "backup.sav");
        add(root / "console.id", "console.id");
        break;
    case Title::LostFuture:
        addSlots(root / "c3jd-saves");
        break;
    case Title::SpectresCall:
        addSlots(root / "blfd-saves");
        break;
    case Title::MiracleMask:
        addSlots(root / "akkd-saves");
        break;
    case Title::AzranLegacy:
        break;
    }
    return files;
}

// --- Pictures ----------------------------------------------------------------

std::vector<Picture> previews(Title title, const fs::path& pack) {
    std::vector<Picture> out;
    if (title != Title::CuriousVillage && title != Title::PandorasBox) {
        return out; // (no picture decoder exposed here yet for the later games)
    }
    // Backgrounds of the pack's picture folders (title, maps, menus, events).
    std::vector<std::string> names;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(pack / "bg", error);
         it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) {
            break;
        }
        if (it->path().extension() == ".l2bg") {
            auto name = fs::relative(it->path(), pack / "bg").generic_string();
            names.push_back(name.substr(0, name.size() - 5));
        }
    }
    std::sort(names.begin(), names.end());
    GraphicsAssets assets(pack);
    const std::size_t count = std::min<std::size_t>(names.size(), 24);
    for (std::size_t i = 0; i < count; ++i) {
        // Spread over the whole list, not just its first folder.
        const auto& name = names[i * names.size() / count];
        try {
            out.push_back(picture("Background " + name, assets.background(name)));
        } catch (const std::exception&) {
            // (one picture the decoder does not take: the others still show)
        }
    }
    if (out.empty() && !names.empty()) {
        throw std::runtime_error("The pictures of this import could not be decoded.");
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> languages(Title title, const fs::path& pack) {
    std::vector<std::pair<std::string, std::string>> out;
    if (title != Title::CuriousVillage) {
        return out;
    }
    const auto library = shell::loadAdventure1(pack);
    for (const auto& language : library.languages) {
        out.emplace_back(language.code, language.name);
    }
    return out;
}

std::string systemLanguage(Title title, const fs::path& pack, const std::string& iso) {
    if (title != Title::CuriousVillage) {
        return {};
    }
    const auto library = shell::loadAdventure1(pack);
    if (library.languages.empty()) {
        return {};
    }
    const int index = shell::usedLanguage(library.languages, -1, iso);
    return library.languages[std::size_t(std::clamp<int>(index, 0, int(library.languages.size()) - 1))].code;
}

// --- A running game ------------------------------------------------------------

bool storyPlayable(Title title) { return title != Title::AzranLegacy; }

bool hasCheckpoints(Title title) {
    return title == Title::CuriousVillage || title == Title::PandorasBox || title == Title::LostFuture ||
           title == Title::MiracleMask;
}

struct Session::Implementation {
    // The checkpoint file as last seen (write time, size): a change counts
    // as a new checkpoint (checkpointGeneration).
    std::optional<std::pair<fs::file_time_type, std::uintmax_t>> seen;
    std::uint64_t generation = 0;
    Title title;
    fs::path pack, progress;
    std::unique_ptr<shell::GameSession> game;
    bool resumed = false; // continued from a checkpoint
};

Session::Session(std::unique_ptr<Implementation> implementation) : impl_(std::move(implementation)) {}

Session::~Session() = default;

std::unique_ptr<Session> Session::start(Title title, const fs::path& pack, const fs::path& progress,
                                        const StartOptions& options) {
    if (!storyPlayable(title)) {
        throw std::runtime_error("This game's story is not reconstructed yet.");
    }
    if (const std::string problem = checkPack(title, pack); !problem.empty()) {
        throw std::runtime_error(problem);
    }
    fs::create_directories(progress);
    auto impl = std::make_unique<Implementation>();
    impl->title = title;
    impl->pack = pack;
    impl->progress = progress;
    // The checkpoint file is the session's own: continuing writes the one to
    // continue from, a new start removes an old one (the title opens).
    const fs::path checkpoint = checkpointFile(title, progress);
    if (!checkpoint.empty()) {
        std::error_code error;
        if (options.checkpoint) {
            const std::string problem = checkCheckpoint(title, pack, progress, *options.checkpoint);
            if (!problem.empty()) {
                throw std::runtime_error(problem);
            }
            writeBytes(checkpoint, *options.checkpoint);
            impl->resumed = true;
        } else {
            fs::remove(checkpoint, error);
        }
    } else if (options.checkpoint) {
        throw std::runtime_error("This game has no checkpoints to continue from.");
    }
    if (!checkpoint.empty()) {
        // (A checkpoint written for continuing is not a new one.)
        std::error_code error;
        const auto time = fs::last_write_time(checkpoint, error);
        const auto size = error ? 0 : fs::file_size(checkpoint, error);
        if (!error) {
            impl->seen.emplace(time, size);
        }
    }
    const bool resume = options.checkpoint.has_value();
    switch (title) {
    case Title::CuriousVillage: {
        std::string language = options.language;
        const auto codes = languages(title, pack);
        if (std::none_of(codes.begin(), codes.end(), [&](const auto& c) { return c.first == language; })) {
            language = codes.empty() ? std::string() : codes.front().first;
        }
        impl->game = shell::GameSession::adventure1Story(pack, progress / "backup.sav", checkpoint, language,
                                                         resume);
        break;
    }
    case Title::PandorasBox:
        impl->game = resume ? shell::GameSession::adventure2Continue(pack, checkpoint)
                            : std::make_unique<shell::GameSession>(pack, checkpoint, 0);
        break;
    case Title::LostFuture:
        impl->game = shell::GameSession::adventure3Story(pack, progress, resume);
        break;
    case Title::SpectresCall:
        impl->game = shell::GameSession::adventure4Story(pack, progress);
        break;
    case Title::MiracleMask:
        impl->game = shell::GameSession::adventure5Story(pack, progress, checkpoint, resume);
        break;
    case Title::AzranLegacy:
        break;
    }
    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

bool Session::step(const Input& input, std::span<float> stereo, int rate) {
    auto& game = *impl_->game;
    for (const auto& touch : input.touches) {
        game.touch(touch.kind != Touch::Kind::Up, touch.x, touch.y, touch.kind == Touch::Kind::Move);
    }
    const unsigned pad = (input.left ? 1u : 0u) | (input.right ? 2u : 0u) | (input.up ? 4u : 0u) |
                         (input.down ? 8u : 0u);
    game.pad(pad);
    game.tilt(input.tiltX, input.tiltY);
    game.shake(input.shake);
    game.motion(input.motion);
    game.blow(input.blow);
    for (const auto& key : input.name) {
        NameInput name;
        name.action = key.kind == NameKey::Kind::Character ? NameInput::Action::Insert
                      : key.kind == NameKey::Kind::Erase   ? NameInput::Action::Erase
                                                           : NameInput::Action::Confirm;
        name.character = key.character;
        game.nameInput(name);
    }
    if (input.skip) {
        game.skip();
    }
    const bool stepped = game.step(stereo, rate);
    // A checkpoint written in this frame (the game writes it while the frame
    // is made, so the frame shows the moment it keeps).
    if (const fs::path file = checkpointFile(impl_->title, impl_->progress); stepped && !file.empty()) {
        std::error_code error;
        const auto time = fs::last_write_time(file, error);
        const auto size = error ? 0 : fs::file_size(file, error);
        std::optional<std::pair<fs::file_time_type, std::uintmax_t>> now;
        if (!error) {
            now.emplace(time, size);
        }
        if (now && now != impl_->seen) {
            ++impl_->generation;
        }
        impl_->seen = now;
    }
    return stepped;
}

std::uint64_t Session::checkpointGeneration() const { return impl_->generation; }

Screens Session::screens() const { return embed::screens(impl_->title); }

bool Session::frame(std::vector<std::uint8_t>& rgba) const { return impl_->game->frame(rgba); }

Status Session::status() const {
    const auto state = impl_->game->state();
    Status status;
    status.skippable = state.skippable;
    status.movie = state.scene == SceneKind::Movie;
    status.naming = state.scene == SceneKind::Name;
    status.puzzle = state.scene == SceneKind::Puzzle;
    // The one puzzle that listens to the microphone (the second adventure's
    // 203); the others never do.
    status.listening = impl_->title == Title::PandorasBox && state.scene == SceneKind::Puzzle &&
                       state.detail == 203;
    for (const auto& area : state.touchAreas) {
        status.areas.push_back({area.x, area.y, area.w, area.h, int(area.kind)});
    }
    return status;
}

std::string Session::error() const { return impl_->game->error(); }

bool Session::ended() const { return impl_->game->ended(); }

std::optional<std::vector<std::uint8_t>> Session::checkpoint() const {
    const fs::path file = checkpointFile(impl_->title, impl_->progress);
    if (file.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    if (!fs::exists(file, error)) {
        return std::nullopt;
    }
    try {
        return readBytes(file);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string Session::checkCheckpoint(Title title, const fs::path& pack, const fs::path& scratch,
                                     std::span<const std::uint8_t> bytes) {
    if (!hasCheckpoints(title)) {
        return "This game has no checkpoints to continue from.";
    }
    if (bytes.empty() || bytes.size() > (4u << 20)) {
        return "The checkpoint is damaged.";
    }
    // Read with the game's own reader, from a scratch folder beside the
    // progress (never the session's own files).
    static std::mutex scratchMutex;
    std::lock_guard lock(scratchMutex);
    std::mt19937 random(std::random_device{}());
    const fs::path folder = scratch / (".check-" + std::to_string(random()));
    std::error_code error;
    fs::remove_all(folder, error);
    struct Cleanup {
        fs::path folder;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(folder, ignored);
        }
    } cleanup{folder};
    try {
        const fs::path file = checkpointFile(title, folder);
        writeBytes(file, bytes);
        const bool usable = title == Title::CuriousVillage ? shell::adventure1Checkpoint(file).has_value()
                            : title == Title::PandorasBox  ? shell::adventure2Checkpoint(pack, file).has_value()
                            : title == Title::MiracleMask  ? adventure5::readCheckpoint(file).has_value()
                                                           : shell::adventure3Checkpoint(folder).has_value();
        return usable ? std::string() : "The checkpoint cannot be continued (damaged or from another version).";
    } catch (const std::exception& e) {
        return std::string("The checkpoint cannot be read (") + e.what() + ").";
    }
}

} // namespace picarat::embed
