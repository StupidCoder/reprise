#pragma once
// Writes pack files under a root folder and remembers each file's SHA-256,
// so the import can verify what it wrote and record its provenance.
#include "import/rom_image.hpp"
#include "import/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <map>

namespace picarat::import {

class PackWriter {
public:
    explicit PackWriter(std::filesystem::path root) : root_(std::move(root)) {
        std::filesystem::create_directories(root_);
    }

    void save(const std::string& name, std::span<const std::uint8_t> data) {
        const auto target = root_ / safeName(name);
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) {
            throw std::runtime_error("could not write " + target.string());
        }
        outputs_[name] = sha256(data);
    }

    void save(const std::string& name, std::string_view text) {
        save(name, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    bool contains(const std::string& name) const { return outputs_.count(name) != 0; }

    const std::map<std::string, std::string>& outputs() const { return outputs_; }

    // provenance.json: the ROM's SHA-256 and every output's digest.
    void writeProvenance(std::string_view romSha256) const {
        std::string text = "{\n  \"version\": 2,\n  \"rom_sha256\": \"" + std::string(romSha256) +
                           "\",\n  \"outputs\": {";
        bool first = true;
        for (const auto& [name, digest] : outputs_) {
            text += (first ? "\n" : ",\n") + ("    \"" + name + "\": \"" + digest + "\"");
            first = false;
        }
        text += "\n  }\n}\n";
        std::ofstream(root_ / "provenance.json", std::ios::binary) << text;
    }

private:
    std::filesystem::path root_;
    std::map<std::string, std::string> outputs_;
};

} // namespace picarat::import
