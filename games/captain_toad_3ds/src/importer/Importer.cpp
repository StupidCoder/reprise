// See Importer.hpp. Writes into Library-owned staging; only Reprise installs or replaces content.
#include "Importer.hpp"

#include "SceneExport.hpp"

#include <fstream>
#include <sstream>

namespace captain_toad {

namespace fs = std::filesystem;

fs::path packageDirectory(const fs::path& dataDirectory) {
    return dataDirectory / "Opening";
}

fs::path romDirectory(const fs::path& dataDirectory) {
    return dataDirectory / "Rom";
}

bool packageReady(const fs::path& dataDirectory) {
    if (!fs::exists(romDirectory(dataDirectory) / "complete")) {
        return false;
    }
    std::ifstream f(packageDirectory(dataDirectory) / "manifest.json");
    if (!f) {
        return false;
    }
    std::stringstream s;
    s << f.rdbuf();
    // The manifest is written last; its schema line decides validity.
    return s.str().find("\"schema\": " + std::to_string(kPackageSchema) + ",") != std::string::npos &&
           fs::exists(packageDirectory(dataDirectory) / "scene.json");
}

void importInto(RomImage& image, const fs::path& folder,
                const std::function<void(double, const std::string&)>& progress) {
    if (image.programId() != kSupportedProgramId) {
        throw ImportError("Unsupported release.");
    }
    if (!fs::is_directory(folder) || !fs::is_empty(folder)) {
        throw ImportError("Import requires an empty staging directory.");
    }
    auto report = [&](double f, const std::string& phase) {
        if (progress) {
            progress(f, phase);
        }
    };
    report(0, "Verifying the supported release");
    Inputs inputs(image);
    inputs.code();
    exportPackage(inputs, packageDirectory(folder), [&](double f, const std::string& phase) { report(f * .5, phase); });
    const auto rom = romDirectory(folder);
    writeFile(rom / "exefs" / "code.bin", inputs.code());
    const auto paths = romfsPaths();
    for (size_t i = 0; i < paths.size(); ++i) {
        writeFile(rom / "romfs" / paths[i], inputs.copy(paths[i]));
        if (i % 64 == 0 || i + 1 == paths.size()) {
            report(.5 + .49 * double(i + 1) / double(paths.size()), "Verifying and copying game files");
        }
    }
    writeFile(rom / "complete", std::string("verified\n"));
    json::Value manifest = json::Value::object();
    manifest["schema"] = kPackageSchema;
    manifest["program_id"] = "00040000001CB200";
    manifest["sources"] = inputs.sources();
    writeFile(packageDirectory(folder) / "manifest.json", json::dump(manifest, 2) + "\n");
    report(1, "Extraction complete");
}

}   // namespace captain_toad
