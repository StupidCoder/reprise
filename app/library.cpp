#include "app/library.hpp"
#include "platform/files.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace app {
namespace fs = std::filesystem;

namespace {

// The manifest: "release <name>", "source <file name>", then one
// "file <sha256> <name>" line per installed file.
std::string manifest(const ImportResult& result, const fs::path& source) {
    std::ostringstream out;
    out << "release " << result.release << '\n' << "source " << source.filename().string() << '\n';
    for (const auto& file : result.files) {
        out << "file " << platform::sha256(file.bytes) << ' ' << file.name << '\n';
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
    if (!game.import) {
        return std::nullopt;
    }
    return check_folder(root_ / "games" / game.id);
}

std::optional<Library::Install> Library::check_folder(const fs::path& folder) const {
    std::error_code error;
    if (!fs::exists(folder / "release.txt", error)) {
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
            } else if (line.starts_with("file ")) {
                const auto hash = line.substr(5, 64);
                const auto name = line.substr(70);
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

void Library::install(const GameEntry& game, const ImportResult& result, const fs::path& source) {
    write_verified(root_ / "games" / game.id, result, source);
}

fs::path Library::parts_directory(const GameEntry& game) const {
    return root_ / "parts" / game.id;
}

void Library::write_verified(const fs::path& target, const ImportResult& result, const fs::path& source) {
    auto key = fs::relative(target, root_).generic_string(); // "games/<id>" or "parts/<id>/<n>"
    std::replace(key.begin(), key.end(), '/', '-');
    const auto staging = root_ / "staging" / key;
    fs::remove_all(staging);
    fs::create_directories(staging);
    for (const auto& file : result.files) {
        fs::create_directories((staging / file.name).parent_path());
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
    // Each importer has its own size limit (GameEntry::largest_image); only those that accept
    // a file of this size see it.
    std::error_code size_error;
    const auto size = fs::file_size(image, size_error);
    if (size_error) {
        return {false, std::nullopt, "The file could not be read (" + size_error.message() + ")."};
    }
    auto accepts = [&](std::size_t i) { return games_[i].import && size <= games_[i].largest_image; };
    std::vector<std::size_t> order;
    if (preferred && accepts(*preferred)) {
        order.push_back(*preferred);
    }
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (accepts(i) && (!preferred || i != *preferred)) {
            order.push_back(i);
        }
    }
    if (order.empty()) {
        return {false, std::nullopt, "This file is too large to be one of the supported games' images."};
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes = platform::read_file(image);
    } catch (const std::exception& e) {
        return {false, std::nullopt, e.what()};
    }

    std::string other_game, this_game; // first message of each kind
    for (const auto index : order) {
        const auto& game = games_[index];
        ImportResult result;
        try {
            result = game.import(bytes);
        } catch (const NotThisGame& e) {
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
        if (result.parts > 1) {
            try {
                return import_part(index, result, image);
            } catch (const std::exception& e) {
                return {false, index, std::string("Import failed: ") + e.what()};
            }
        }
        try {
            install(game, result, image);
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

std::string Library::imported_parts(std::size_t game) const {
    const auto folder = parts_directory(games_[game]);
    std::error_code error;
    if (installs_[game] || !fs::is_directory(folder, error)) {
        return {};
    }
    std::string parts;
    int count = 0;
    for (int part = 1; part <= 9; ++part) {
        if (fs::exists(folder / std::to_string(part) / "release.txt", error)) {
            parts += (count++ ? ", " : "") + std::to_string(part);
        }
    }
    return count == 0 ? std::string() : (count == 1 ? "disk " : "disks ") + parts;
}

Library::Outcome Library::import_part(std::size_t index, const ImportResult& result, const fs::path& source) {
    const auto& game = games_[index];
    const auto parts = parts_directory(game);
    write_verified(parts / std::to_string(result.part), result, source);

    // Every disk present, all of the same release?
    std::vector<int> missing;
    for (int part = 1; part <= result.parts; ++part) {
        const auto kept = check_folder(parts / std::to_string(part));
        if (!kept || kept->release != result.release) {
            missing.push_back(part);
        }
    }
    const auto label = game.title + " disk " + std::to_string(result.part) + " of " + std::to_string(result.parts);
    if (!missing.empty()) {
        std::string list;
        for (std::size_t i = 0; i < missing.size(); ++i) {
            list += (i == 0 ? "" : i + 1 == missing.size() ? " and " : ", ") + std::to_string(missing[i]);
        }
        return {true, index, label + " imported. Now import disk" + (missing.size() > 1 ? "s " : " ") + list + ".",
                true};
    }
    ImportResult combined;
    combined.release = result.release;
    for (int part = 1; part <= result.parts; ++part) {
        const auto folder = parts / std::to_string(part);
        const auto kept = check_folder(folder);
        for (const auto& [name, size] : kept->files) {
            combined.files.push_back({name, platform::read_file(folder / name)});
        }
    }
    install(game, combined, source);
    installs_[index] = check_install(game);
    if (!installs_[index]) {
        return {false, index, "The imported files could not be verified."};
    }
    fs::remove_all(parts);
    std::error_code ignored;
    fs::remove(root_ / "parts", ignored); // only if no other game waits for disks
    return {true, index, game.title + " imported (" + result.release + ", all " + std::to_string(result.parts) +
                             " disks)."};
}

std::vector<std::string> Library::adopt_legacy() {
    std::vector<std::string> messages;
    for (std::size_t index = 0; index < games_.size(); ++index) {
        const auto& game = games_[index];
        if (!game.legacy) {
            continue;
        }
        const auto marker = root_ / "migrations" / (game.id + ".txt");
        std::error_code error;
        if (fs::exists(marker, error)) {
            continue; // taken over (or found unusable) before
        }
        std::optional<LegacyCopy> legacy;
        try {
            legacy = game.legacy(root_);
        } catch (const std::exception& e) {
            messages.push_back(game.title + ": the earlier copy could not be read (" + e.what() + ").");
            continue;
        }
        if (!legacy) {
            continue;
        }
        std::vector<std::string> done = legacy->notes;
        bool failed = false;
        if (legacy->import) {
            if (installs_[index]) {
                done.push_back("Reprise already has an import of " + game.title + "; the earlier one was left as it is.");
            } else {
                try {
                    install(game, *legacy->import, "earlier application");
                    installs_[index] = check_install(game);
                    if (!installs_[index]) {
                        throw std::runtime_error("the copied files could not be verified");
                    }
                    done.push_back(game.title + ": the earlier import (" + legacy->import->release + ") was copied into Reprise.");
                } catch (const std::exception& e) {
                    failed = true;
                    messages.push_back(game.title + ": the earlier import could not be copied (" + e.what() + ").");
                }
            }
        }
        for (const auto& [source, target] : legacy->copies) {
            if (fs::exists(target, error)) {
                done.push_back(game.title + ": Reprise's own " + target.filename().string() + " was kept; the earlier one was not copied.");
                continue;
            }
            try {
                fs::create_directories(target.parent_path());
                fs::copy_file(source, target);
                done.push_back(game.title + ": the earlier " + source.filename().string() + " was copied.");
            } catch (const std::exception& e) {
                failed = true;
                messages.push_back(game.title + ": " + source.filename().string() + " could not be copied (" + e.what() + ").");
            }
        }
        messages.insert(messages.end(), done.begin(), done.end());
        if (!failed) {
            std::string text;
            for (const auto& line : done) {
                text += line + '\n';
            }
            try {
                fs::create_directories(marker.parent_path());
                platform::write_text(marker, text);
            } catch (const std::exception&) {
                // (Not recorded: the takeover is looked at again next time; nothing is overwritten.)
            }
        }
    }
    return messages;
}

void Library::remove(std::size_t game) {
    fs::remove_all(parts_directory(games_[game]));
    fs::remove_all(content_directory(game));
    installs_[game].reset();
}

} // namespace app
