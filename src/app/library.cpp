#include "app/library.hpp"
#include "platform/files.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace app {
namespace fs = std::filesystem;

namespace {
constexpr std::uintmax_t largest_image = 64u << 20; // larger than any C64/Amiga image

// The manifest: "release <name>", "source <file name>", then one
// "file <sha256> <name>" line per installed file ("extra" instead of "file"
// for the files of the game's optional second file).
std::string manifest(const ImportResult& result, const std::string& source) {
    std::ostringstream out;
    out << "release " << result.release << '\n' << "source " << source << '\n';
    for (const auto& file : result.files) {
        out << (file.extra ? "extra " : "file ") << platform::sha256(file.bytes) << ' ' << file.name << '\n';
    }
    return out.str();
}
} // namespace

Library::Library(fs::path data_directory, std::vector<GameEntry> games)
    : root_(std::move(data_directory)), games_(std::move(games)) {
    for (const auto& game : games_) {
        installs_.push_back(check_install(game));
    }
}

fs::path Library::content_directory(std::size_t game) const {
    return root_ / "games" / games_[game].id;
}

std::string Library::release(std::size_t game) const {
    return installs_[game] ? installs_[game]->release : std::string();
}

std::vector<std::pair<std::string, std::uintmax_t>> Library::installed_files(
    std::size_t game) const {
    return installs_[game] ? installs_[game]->files
                           : std::vector<std::pair<std::string, std::uintmax_t>>{};
}

std::optional<Library::Install> Library::check_install(const GameEntry& game) const {
    const auto folder = root_ / "games" / game.id;
    std::error_code error;
    if (!game.import || !fs::exists(folder / "release.txt", error)) {
        return std::nullopt;
    }
    try {
        const auto text = platform::read_file(folder / "release.txt");
        std::istringstream in(std::string(text.begin(), text.end()));
        Install install;
        std::string line;
        while (std::getline(in, line)) {
            if (line.starts_with("release ")) {
                install.release = line.substr(8);
            } else if (line.starts_with("source ")) {
                install.source = line.substr(7);
            } else if (line.starts_with("file ") || line.starts_with("extra ")) {
                const std::size_t at = line.find(' ') + 1;
                const auto hash = line.substr(at, 64);
                const auto name = line.substr(at + 65);
                if (line.starts_with("extra ")) {
                    install.extras.push_back(name);
                }
                const auto bytes = platform::read_file(folder / name);
                if (platform::sha256(bytes) != hash) {
                    return std::nullopt;
                }
                install.files.emplace_back(name, bytes.size());
            }
        }
        if (install.files.empty()) {
            return std::nullopt;
        }
        return install;
    } catch (const std::exception&) {
        return std::nullopt; // damaged: the game asks for a new import
    }
}

void Library::install(const GameEntry& game, const ImportResult& result, const std::string& source) {
    const auto staging = root_ / "staging" / game.id;
    const auto target = root_ / "games" / game.id;
    fs::remove_all(staging);
    fs::create_directories(staging);
    for (const auto& file : result.files) {
        platform::write_file(staging / file.name, file.bytes);
    }
    platform::write_text(staging / "release.txt", manifest(result, source));
    // Read the staged files back before replacing anything.
    for (const auto& file : result.files) {
        if (platform::read_file(staging / file.name) != file.bytes) {
            fs::remove_all(staging);
            throw std::runtime_error("Writing the imported files failed; please try again.");
        }
    }
    fs::create_directories(target.parent_path());
    fs::remove_all(target);
    fs::rename(staging, target);
    std::error_code ignored;
    fs::remove(root_ / "staging", ignored); // only if no other import is staged
}

Library::Outcome Library::import(const fs::path& image, std::optional<std::size_t> preferred) {
    std::vector<std::uint8_t> bytes;
    try {
        std::error_code error;
        if (fs::file_size(image, error) > largest_image) {
            return {false, std::nullopt, "This file is too large to be a C64 or Amiga image."};
        }
        bytes = platform::read_file(image);
    } catch (const std::exception& e) {
        return {false, std::nullopt, e.what()};
    }

    std::vector<std::size_t> order;
    if (preferred && games_[*preferred].import) {
        order.push_back(*preferred);
    }
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (games_[i].import && (!preferred || i != *preferred)) {
            order.push_back(i);
        }
    }

    std::string other_game, this_game; // first message of each kind
    for (const auto index : order) {
        const auto& game = games_[index];
        ImportResult result;
        try {
            result = game.import(bytes);
        } catch (const NotThisGame& e) {
            if (game.import_extra) {
                try {
                    return install_extra(index, game.import_extra(bytes));
                } catch (const NotThisGame&) {
                } catch (const std::exception& extra_error) {
                    if (this_game.empty()) {
                        this_game = extra_error.what();
                    }
                    continue;
                }
            }
            if (other_game.empty()) {
                other_game = e.what();
            }
            continue;
        } catch (const std::exception& e) {
            if (this_game.empty()) {
                this_game = e.what();
            }
            continue;
        }
        try {
            // Keep what the optional second file added.
            if (installs_[index]) {
                for (const auto& name : installs_[index]->extras) {
                    result.files.push_back({name, platform::read_file(content_directory(index) / name), true});
                }
            }
            install(game, result, image.filename().string());
        } catch (const std::exception& e) {
            return {false, index, std::string("Import failed: ") + e.what()};
        }
        installs_[index] = check_install(game);
        if (!installs_[index]) {
            return {false, index, "The imported files could not be verified."};
        }
        return {true, index, game.title + " imported (" + result.release + ")."};
    }
    if (!this_game.empty()) {
        return {false, std::nullopt, this_game};
    }
    return {false, std::nullopt,
            "This image is not one of the supported games" +
                (other_game.empty() ? std::string(".") : " (" + other_game + ")")};
}

Library::Outcome Library::install_extra(std::size_t index, const ImportResult& extra) {
    const auto& game = games_[index];
    if (!installs_[index]) {
        return {false, index, "Import " + game.media + " first, then add " + game.extra_media + "."};
    }
    // The installed files, with the second file's replaced.
    ImportResult combined;
    combined.release = installs_[index]->release;
    const auto& extras = installs_[index]->extras;
    for (const auto& [name, size] : installs_[index]->files) {
        const bool replaced = std::ranges::any_of(extra.files, [&](const auto& f) { return f.name == name; });
        if (!replaced) {
            const bool is_extra = std::ranges::find(extras, name) != extras.end();
            combined.files.push_back({name, platform::read_file(content_directory(index) / name), is_extra});
        }
    }
    for (auto file : extra.files) {
        file.extra = true;
        combined.files.push_back(std::move(file));
    }
    try {
        install(game, combined, installs_[index]->source);
    } catch (const std::exception& e) {
        return {false, index, std::string("Import failed: ") + e.what()};
    }
    installs_[index] = check_install(game);
    if (!installs_[index]) {
        return {false, index, "The imported files could not be verified."};
    }
    return {true, index, game.title + ": " + extra.release + " added."};
}

void Library::remove(std::size_t game) {
    fs::remove_all(content_directory(game));
    installs_[game].reset();
}

} // namespace app
