#include "app/quick_save.hpp"
#include "platform/host.hpp"
#include <chrono>
#include <fstream>

namespace shell {
namespace fs = std::filesystem;

fs::path quick_save_path(const fs::path& root, const char* name) {
    return root / "saves" / name;
}

bool has_quick_save(const fs::path& root, const char* name) {
    std::error_code ec;
    return fs::is_regular_file(quick_save_path(root, name), ec);
}

void write_quick_save(const fs::path& root, const char* name, std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 32 * 1024 * 1024) {
        throw std::runtime_error("Invalid quick-save size");
    }
    const auto target = quick_save_path(root, name);
    fs::create_directories(target.parent_path());
    const auto stage =
        target.parent_path() /
        (".save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(stage)) {
        throw std::runtime_error("Could not create quick-save workspace");
    }

    struct Cleanup {
        fs::path p;

        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{stage};

    const auto temporary = stage / "new", backup = stage / "previous";
    std::ofstream out(temporary, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("Could not write quick save. Previous save is unchanged");
    }
    if (fs::exists(target) && !fs::is_regular_file(target)) {
        throw std::runtime_error("Quick-save path is not a regular file");
    }
#ifndef _WIN32
    // POSIX replacement is atomic: readers see either complete version.
    fs::rename(temporary, target);
#else
    bool moved = false;
    try {
        if (fs::exists(target)) {
            fs::rename(target, backup);
            moved = true;
        }
        fs::rename(temporary, target);
    } catch (...) {
        if (moved) {
            std::error_code ec;
            fs::rename(backup, target, ec);
            // Preserve recoverable data if even rollback fails.
            if (ec) {
                cleanup.p.clear();
                throw std::runtime_error("Could not replace quick save. Previous save remains at " +
                                         backup.string());
            }
        }
        throw;
    }
#endif
}

std::vector<std::uint8_t> read_quick_save(const fs::path& root, const char* name) {
    const auto path = quick_save_path(root, name);
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("No quick save yet. Use " +
                                 std::string(platform::shortcut_modifier) +
                                 "+S or Quick save first");
    }
    if (!size || size > 32 * 1024 * 1024) {
        throw std::runtime_error("Quick save has an invalid size");
    }
    std::vector<std::uint8_t> bytes(size);
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
    if (in.gcount() != std::streamsize(size) || in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Could not read a complete quick save");
    }
    return bytes;
}
} // namespace shell
