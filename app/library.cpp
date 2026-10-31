#include "app/library.hpp"
#include "platform/files.hpp"
#include "platform/file_source.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <sstream>
#include <map>
#include <set>
#include <iomanip>
#include <chrono>
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
// The manifest of a folder of installed files (large imports, GameEntry::import_into):
// "release", "source", the files' checksums as above, then "size <bytes> <name>" per file and
// "check sizes", which tells check_folder to compare sizes at start-up instead of checksums.
std::string folder_manifest(const fs::path& folder, const std::string& release, const fs::path& source,
                            const ImportProgress& progress) {
    std::vector<std::string> names;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            names.push_back(fs::relative(entry.path(), folder).generic_string());
        }
    }
    std::sort(names.begin(), names.end());
    if (names.empty()) {
        throw std::runtime_error("The import wrote no files.");
    }
    std::ostringstream out, sizes;
    out << "release " << release << '\n' << "source " << source.filename().string() << '\n';
    std::uintmax_t total = 0, verified = 0;
    for (const auto& name : names) total += fs::file_size(folder / name);
    if (progress) progress("Verifying imported files", 0);
    for (const auto& name : names) {
        if (name == "release.txt" || name.find('\n') != std::string::npos) {
            throw std::runtime_error("The import wrote an unexpected file name.");
        }
        // (Read back from disk: the checksum is of what was written.)
        const auto bytes = platform::read_file(folder / name);
        out << "file " << platform::sha256(bytes) << ' ' << name << '\n';
        sizes << "size " << bytes.size() << ' ' << name << '\n';
        verified += bytes.size();
        if (progress) progress("Verifying imported files", total ? double(verified) / total : 1);
    }
    out << sizes.str() << "check sizes\n";
    return out.str();
}
} // namespace

Library::Library(fs::path data_directory, std::vector<GameEntry> games, StartupValidation validation)
    : root_(std::move(data_directory)), games_(std::move(games)) {
    for (const auto& game : games_) {
        installs_.push_back(check_install(game, validation == StartupValidation::defer_large_imports));
    }
}

bool Library::verify(std::size_t game) {
    if (installs_.at(game) && installs_[game]->needs_verification)
        installs_[game] = check_install(games_.at(game));
    return installs_[game].has_value();
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

std::optional<Library::Install> Library::check_install(const GameEntry& game, bool defer_large) const {
    if (!game.import && !game.import_into) {
        return std::nullopt;
    }
    return check_folder(root_ / "games" / game.id, root_ / "validation" / (game.id + ".txt"), defer_large);
}

std::optional<Library::Install> Library::check_folder(const fs::path& folder, const fs::path& cache, bool defer_large) const {
    std::error_code error;
    if (!fs::exists(folder / "release.txt", error)) {
        return std::nullopt;
    }
    try {
        const auto text = platform::read_file(folder / "release.txt");
        std::istringstream in(std::string(text.begin(), text.end()));
        Install install;
        std::string line;
        std::vector<std::string> lines;
        bool sizes_only = false; // a large import (folder_manifest): sizes, not checksums
        while (std::getline(in, line)) {
            sizes_only |= line == "check sizes";
            lines.push_back(line);
        }
        // Large folder imports already have a verified inventory written during
        // import. Reading it is enough to show the shelf; do the file-size scan
        // for the chosen game before play, not for every game at each startup.
        install.needs_verification = sizes_only && defer_large;
        // The cache is disposable, local to this data directory and outside imported game
        // assets. Read the small manifest every time; changing it invalidates all receipts.
        // Still stat every file: directory timestamps do not track file-content edits.
        std::map<std::string, std::uintmax_t> file_sizes;
        std::map<std::string, std::string> file_receipts;
        std::set<std::string> verified_files;
        const auto fingerprint = [&] {
            std::ostringstream out;
            out << "Reprise validation 1\n" << platform::sha256(text) << '\n';
            for (const auto& entry : lines) {
                if (!entry.starts_with("file ")) continue;
                const auto name = entry.substr(70);
                const auto path = folder / name;
                const auto size = fs::file_size(path);
                const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    fs::last_write_time(path).time_since_epoch()).count();
                file_sizes[name] = size;
                std::ostringstream record;
                record << std::quoted(name) << ' ' << size << ' ' << stamp;
                file_receipts[name] = record.str();
                out << record.str() << '\n';
            }
            return out.str();
        };
        std::string receipt;
        bool previously_verified = false;
        if (!sizes_only && !cache.empty()) {
            receipt = fingerprint();
            try {
                const auto saved = platform::read_file(cache);
                const std::string saved_text(saved.begin(), saved.end());
                previously_verified = saved_text == receipt;
                std::istringstream records(saved_text);
                std::string version, digest, record;
                std::getline(records, version); std::getline(records, digest);
                if (version == "Reprise validation 1" && digest == platform::sha256(text))
                    while (std::getline(records, record)) verified_files.insert(record);
            } catch (const std::exception&) { /* absent or unreadable cache: verify normally */ }
        }
        for (const auto& entry : lines) {
            if (entry.starts_with("release ")) {
                install.release = entry.substr(8);
            } else if (sizes_only && entry.starts_with("size ")) {
                const auto space = entry.find(' ', 5);
                if (space == std::string::npos) {
                    return std::nullopt;
                }
                const auto expected = std::stoull(entry.substr(5, space - 5));
                const auto name = entry.substr(space + 1);
                std::error_code error;
                const auto size = install.needs_verification ? expected : fs::file_size(folder / name, error);
                if (error || size != expected) {
                    return std::nullopt;
                }
                install.files.emplace_back(name, size);
            } else if (!sizes_only && entry.starts_with("file ")) {
                const auto hash = entry.substr(5, 64);
                const auto name = entry.substr(70);
                if (!cache.empty() && verified_files.contains(file_receipts.at(name))) {
                    install.files.emplace_back(name, file_sizes.at(name));
                } else {
                    const auto bytes = platform::read_file(folder / name);
                    if (platform::sha256(bytes) != hash) return std::nullopt;
                    install.files.emplace_back(name, bytes.size());
                }
            }
        }
        if (install.files.empty()) {
            return std::nullopt;
        }
        if (!receipt.empty() && !previously_verified) {
            // Do not cache a result if a file changed while it was being read.
            if (fingerprint() != receipt) return std::nullopt;
            try {
                fs::create_directories(cache.parent_path());
                // A partial write can only cause a cache miss on the next launch.
                platform::write_text(cache, receipt);
            } catch (const std::exception&) { /* caching must not make a valid game unavailable */ }
        }
        return install;
    } catch (const std::exception&) {
        return std::nullopt; // damaged: the game asks for a new import
    }
}

void Library::install(const GameEntry& game, const ImportResult& result, const fs::path& source) {
    write_verified(root_ / "games" / game.id, result, source);
}

void Library::install_folder(const GameEntry& game, const fs::path& source,
                             const std::function<std::string(const fs::path&)>& fill,
                             const ImportProgress& progress) {
    const auto target = root_ / "games" / game.id;
    const auto staging = root_ / "staging" / ("games-" + game.id);
    fs::remove_all(staging);
    fs::create_directories(staging);
    try {
        const auto release = fill(staging);
        platform::write_text(staging / "release.txt", folder_manifest(staging, release, source, progress));
        if (!check_folder(staging)) {
            throw std::runtime_error("Writing the imported files failed; please try again.");
        }
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
    if (progress) progress("Finishing installation", -1);
    fs::create_directories(target.parent_path());
    fs::remove_all(target);
    fs::rename(staging, target);
    std::error_code ignored;
    fs::remove(root_ / "staging", ignored); // only if no other import is staged
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

Library::Outcome Library::import(const fs::path& image, std::optional<std::size_t> preferred,
                                 const ImportProgress& progress) {
    if (progress) progress("Identifying game image", -1);
    // Each importer has its own size limit (GameEntry::largest_image); only those that accept
    // a file of this size see it.
    // (Through the platform's reader, which also opens the file picker's documents.)
    std::unique_ptr<platform::FileSource> source;
    try {
        source = std::make_unique<platform::FileSource>(image);
    } catch (const std::exception& e) {
        return {false, std::nullopt, std::string("The file could not be read (") + e.what() + ")."};
    }
    const auto size = source->size();
    auto accepts = [&](std::size_t i) {
        return (games_[i].import || games_[i].import_into) && size <= games_[i].largest_image;
    };
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
    // Read whole only for the importers that take the image in memory.
    std::optional<std::vector<std::uint8_t>> bytes;

    std::string other_game, this_game; // first message of each kind
    for (const auto index : order) {
        const auto& game = games_[index];
        if (game.import_into) {
            // A large image: the importer streams it and writes the files itself.
            try {
                install_folder(game, image, [&](const fs::path& folder) {
                    return game.import_into(*source, folder, progress);
                }, progress);
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
            installs_[index] = check_install(game);
            if (!installs_[index]) {
                return {false, index, "The imported files could not be verified."};
            }
            return {true, index, game.title + " imported (" + installs_[index]->release + ")."};
        }
        if (!bytes) {
            try {
                bytes.emplace(std::size_t(size));
                if (size > 0) {
                    source->read(0, *bytes);
                }
            } catch (const std::exception& e) {
                return {false, std::nullopt, e.what()};
            }
        }
        ImportResult result;
        try {
            if (progress) progress("Reading game assets", -1);
            result = game.import(*bytes);
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
            if (progress) progress("Installing and verifying files", -1);
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
        if (legacy->folder) {
            if (installs_[index]) {
                done.push_back("Reprise already has an import of " + game.title + "; the earlier one was left as it is.");
            } else {
                try {
                    install_folder(game, "earlier application", [&](const fs::path& folder) {
                        fs::copy(*legacy->folder, folder, fs::copy_options::recursive);
                        return legacy->folder_release;
                    });
                    installs_[index] = check_install(game);
                    if (!installs_[index]) {
                        throw std::runtime_error("the copied files could not be verified");
                    }
                    done.push_back(game.title + ": the earlier import (" + legacy->folder_release +
                                   ") was copied into Reprise.");
                } catch (const std::exception& e) {
                    failed = true;
                    messages.push_back(game.title + ": the earlier import could not be copied (" + e.what() + ").");
                }
            }
        }
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
