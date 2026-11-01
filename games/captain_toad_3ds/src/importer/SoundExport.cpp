// =============================================================================================
// SoundExport.cpp - the front end's sound package (SoundExport.hpp)
// =============================================================================================
#include "SoundExport.hpp"

#include "Archive.hpp"
#include "Binary.hpp"
#include "Importer.hpp"

#include <fstream>
#include <sstream>
#include <unistd.h>

namespace captain_toad {

namespace fs = std::filesystem;
using json::Value;

namespace {

// The SeDataBase entries of the front-end actors (layout actors and sequences by their sound
// user names).
const char* const kOwners[] = {"CourseBook",         "CourseBookCover",      "CoursePageL",
                               "CoursePageR",        "ProductSequence",      "WipeStart",
                               "ButtonBack",         "ButtonBackParts",      "CourseSelectLayout",
                               "CourseInfoItemDemo", "CourseSelectItemDemo", "CourseBookBonusDemo",
                               "WipeFadeBlack"};

// Situations of BgmSituationInfoList the front end sends (book scene, course-open sequence).
const char* const kSituations[] = {"VolumeNormal", "VolumeNormalFast", "VolumeDownJingle", "VolumeDown",
                                   "VolumeDownBonus"};

}   // namespace

void exportFrontEndSound(Inputs& in, const fs::path& out) {
    for (const char* f : {"sound_data.bcsar", "SNDTBLS.DAT", "SFXSCRIPTFILES.DAT", "STREAMFILEINFO.DAT",
                          "SND3DLIST.DAT", "ADSRINFO.DAT"}) {
        writeFile(out / f, in.romfs(std::string("SoundData/") + f));
    }
    // BgmPlayInfoList: the CourseSelect line's entries name the stream of each season's book.
    const auto& bgmDb = in.archive("SoundData/BgmDataBase.szs");
    const Value playInfo = byml(member(bgmDb, "BgmPlayInfoList.byml"));
    Value courseSelect = Value::object();
    Value streamList = Value::array();
    for (const auto& p : playInfo.at("PlayInfoList").items()) {
        if (p.find("LineName") && p.at("LineName").str() == "CourseSelect") {
            const std::string stream = p.at("DefaultResourceName").str();
            courseSelect[p.at("Name").str()] = stream;
            const std::string file = stream + ".dspadpcm.bcstm";
            writeFile(out / "stream" / file, in.romfs("SoundData/stream/" + file));
            streamList.push(stream);
        }
    }
    const Value situationList = byml(member(bgmDb, "BgmSituationInfoList.byml"));
    Value situations = Value::object();
    for (const auto& s : situationList.at("SituationInfoList").items()) {
        for (const char* n : kSituations) {
            if (s.at("Name").str() == n) {
                situations[n] = s;
            }
        }
    }
    Value bgm = Value::object();
    bgm["course_select"] = courseSelect;
    bgm["situations"] = situations;
    writeFile(out / "bgm.json", json::dump(bgm, 1));

    const auto& seDb = in.archive("SoundData/SeDataBase.szs");
    Value owners = Value::object();
    for (const char* o : kOwners) {
        owners[o] = byml(member(seDb, std::string(o) + ".byml"));
    }
    Value sedb = Value::object();
    sedb["owners"] = owners;
    sedb["properties"] = Value::object();
    Value stage = Value::object();
    stage["AtmosphereSeName"] = "";   // no ambience in the front end
    sedb["stage"] = stage;
    sedb["streams"] = streamList;
    sedb["schema"] = kFrontEndSoundSchema;
    writeFile(out / "sedb.json", json::dump(sedb, 1));
}

fs::path buildFrontEndSound(const fs::path& dataDirectory) {
    const fs::path target = dataDirectory / "Sound" / "FrontEnd";
    {
        std::ifstream f(target / "sedb.json");
        std::stringstream s;
        s << f.rdbuf();
        if (f && s.str().find("\"schema\": " + std::to_string(kFrontEndSoundSchema)) != std::string::npos) {
            return target;
        }
    }
    const fs::path staging = dataDirectory / "Sound" / (".build-" + std::to_string(getpid()));
    fs::remove_all(staging);
    try {
        Inputs in(romDirectory(dataDirectory));
        exportFrontEndSound(in, staging);
        fs::remove_all(target);
        fs::rename(staging, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
    return target;
}

}   // namespace captain_toad
