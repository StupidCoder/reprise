#include "app/library.hpp"
#include "platform/files.hpp"
#include "platform/sha256.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    const auto root = fs::temp_directory_path() / ("reprise-validation-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        app::GameEntry game;
        game.id = "fixture";
        game.import = [](auto) { return app::ImportResult{}; };
        const auto folder = root / "games" / game.id;
        const auto file = folder / "asset.bin";
        const auto manifest = folder / "release.txt";
        const auto cache = root / "validation" / "fixture.txt";
        fs::create_directories(folder);
        const std::vector<std::uint8_t> good{1,2,3,4}, bad{4,3,2,1};
        const auto description = "release fixture\nfile " + platform::sha256(good) + " asset.bin\nfile " +
            platform::sha256(good) + " other.bin\n";
        const auto other = folder / "other.bin";
        platform::write_file(other, good);
        platform::write_file(file, good);
        platform::write_text(manifest, description);
        const auto installed = [&] { return app::Library(root, {game}).installed(0); };
        require(installed() && fs::exists(cache), "First validation creates cache");
        const auto stamp = fs::last_write_time(file);
        const auto cached = platform::read_file(cache);
        require(installed() && platform::read_file(cache) == cached, "Unchanged import reuses cache");
        // Deliberately retain both metadata values to prove content is not read again.
        // This is a startup optimization, not protection against metadata-preserving edits.
        platform::write_file(file, bad); fs::last_write_time(file, stamp);
        require(installed(), "Matching metadata skips hashing");
        fs::last_write_time(other, fs::last_write_time(other) + std::chrono::seconds(2));
        require(installed(), "Changing one file still reuses other files’ receipts");
        fs::last_write_time(file, stamp + std::chrono::seconds(2));
        require(!installed(), "Changed timestamp rehashes and detects corruption");
        platform::write_file(file, good);
        require(installed(), "Valid changed content is checked and cached");
        const auto second_stamp = fs::last_write_time(file);
        platform::write_text(file, "longer"); fs::last_write_time(file, second_stamp);
        require(!installed(), "Size change invalidates even with same timestamp");
        platform::write_file(file, good); require(installed(), "Restored file validates");
        platform::write_text(manifest, "release changed\nfile " + platform::sha256(bad) + " asset.bin\n");
        require(!installed(), "Manifest edit invalidates cache");
        platform::write_text(manifest, description);
        platform::write_text(cache, "truncated");
        platform::write_file(file, bad);
        require(!installed(), "Malformed cache falls back to hashing");
        platform::write_file(file, good); require(installed(), "Cache can be rebuilt");
        fs::remove(file); require(!installed(), "Missing files remain unavailable");
        platform::write_file(file, good);
        fs::remove(cache); fs::create_directory(cache); // Cannot write the cache as a file.
        require(installed(), "Cache write failure does not hide a valid game");
        platform::write_text(manifest, description + "size 4 asset.bin\ncheck sizes\n");
        platform::write_file(file, bad);
        require(installed(), "Large-import size-only policy is preserved");
        fs::remove_all(root);
        std::cout << "Library validation cache checks passed\n";
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n'; return 1;
    }
}
