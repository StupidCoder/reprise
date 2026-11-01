// =============================================================================================
// BookSound.cpp - the course book's music and sound effects (BookSound.hpp)
// =============================================================================================
#include "session/book/BookSound.hpp"

#include "data/Package.hpp"

#include <cstdlib>

namespace book {

Sound::Sound(const std::filesystem::path& dir) {
    sys_ = std::make_unique<captain_toad::audio::SoundSystem>(dir.string());
    sys_->envelopes = true;   // the front end's waves use ADSR releases and timed entries
    sys_->scripts.perEntryVolumes = true;
    sys_->commandLatency = std::getenv("DIORAMA_SFX_LATENCY") ? std::atoi(std::getenv("DIORAMA_SFX_LATENCY")) : 18;
    keeper_ = std::make_unique<captain_toad::OpeningSoundKeeper::Keeper>(*sys_);
    keeper_->load(dir);
    const captain_toad::data::Value document = captain_toad::data::readJson(dir / "bgm.json");
    captain_toad::data::JsonView bgm = document;
    for (const std::string& k : bgm["course_select"].keys()) {
        courseSelect_[k] = bgm["course_select"][k].str();
    }
    for (const std::string& k : bgm["situations"].keys()) {
        for (captain_toad::data::JsonView sub : bgm["situations"][k]["SubSituationInfoList"].items()) {
            for (captain_toad::data::JsonView proc : sub["ProcInfoList"].items()) {
                if (proc["ProcInfoName"].str() == "ChangeVolume") {
                    situations_[k] = {proc["TargetVolume"].f(), proc["VolumeDiff"].f()};
                }
            }
        }
    }
}

captain_toad::OpeningSoundKeeper::Owner& Sound::owner(const std::string& name, const std::string& db) {
    for (auto& o : keeper_->owners) {
        if (o.name == name) {
            return o;
        }
    }
    captain_toad::OpeningSoundKeeper::Owner o;
    o.name = name;
    o.db = db;
    keeper_->owners.push_back(o);
    return keeper_->owners.back();
}

void Sound::action(const std::string& actor, const std::string& db, const std::string& tag) {
    // 0x297ff0 only follows the tags of the user's ActionInfoList; each runs its own frame clock.
    auto d = keeper_->dbs.find(db);
    if (d == keeper_->dbs.end()) {
        return;
    }
    auto a = d->second.actions.find(tag);
    if (a == d->second.actions.end()) {
        return;
    }
    Clock& c = clocks_[actor + "/" + tag];
    c.db = db;
    c.tag = tag;
    c.frame = 0;
    c.started = false;
    c.end = 0;
    for (const auto& e : a->second) {
        c.end = std::max(c.end, e.start);
    }
}

void Sound::play(const std::string& db, const std::string& playInfo, float param) {
    // The PlayInfo's Volume curve (In 0..2 -> Out 0..2): the object's volume byte times the input
    // (book-turn-sound-a: the turns at 1.00 / 1.03 / 1.06 give instance volumes 27 / 28 / 29). The
    // Pitch curve leaves the pitch at 0x1000 in every traced turn.
    if (param >= 0) {
        sys_->nextObjectScale = param;
    }
    keeper_->play(owner(db, db), playInfo);
    sys_->nextObjectScale = 1.f;
}

void Sound::startMusic(int season) {
    auto it = courseSelect_.find("CourseSelect" + std::to_string(std::min(std::max(season, 1), 4)));
    if (it == courseSelect_.end()) {
        return;
    }
    music_ = it->second;
    if (keeper_->debug) {
        std::fprintf(stderr, "sound: %ld music %s\n", keeper_->stamp, music_.c_str());
    }
    volume_ = target_ = 1;
    step_ = 0;
    fadeFrames_ = 0;
    sys_->requestStream(music_, kMusicLevel);
}

void Sound::situation(const std::string& name) {
    if (keeper_->debug) {
        std::fprintf(stderr, "sound: %ld situation %s\n", keeper_->stamp, name.c_str());
    }
    auto it = situations_.find(name);
    if (it != situations_.end()) {
        target_ = it->second.target;
        step_ = it->second.step;
    }
}

void Sound::stopMusic(int fadeFrames) {
    if (music_.empty()) {
        return;
    }
    if (keeper_->debug) {
        std::fprintf(stderr, "sound: %ld stop music, %d frames\n", keeper_->stamp, fadeFrames);
    }
    fadeFrames_ = std::max(fadeFrames, 1);
    fadeAge_ = 0;
}

void Sound::tick() {
    ++keeper_->stamp;
    // Actions: the keeper sees each started action's frame (2 per tick, from 0 on the tick it
    // started).
    for (auto& [actor, c] : clocks_) {
        captain_toad::OpeningSoundKeeper::Owner& o = owner(actor, c.db);
        if (!c.started) {
            o.action.clear();   // a restarted action is new to the keeper
            c.started = true;
        }
        keeper_->update(o, c.tag, c.frame, 0);
        c.frame += 2;
    }
    for (auto it = clocks_.begin(); it != clocks_.end();) {
        it = it->second.frame > it->second.end + 2 ? clocks_.erase(it) : std::next(it);
    }
    // ChangeVolume: one step per tick towards the target.
    if (volume_ < target_) {
        volume_ = std::min(target_, volume_ + step_);
    } else if (volume_ > target_) {
        volume_ = std::max(target_, volume_ - step_);
    }
    sys_->tickObjects();
}

void Sound::frame() {
    float fade = 1;
    if (fadeFrames_ > 0) {
        ++fadeAge_;
        fade = std::max(0.f, 1.f - float(fadeAge_) / float(fadeFrames_));
    }
    if (!music_.empty()) {
        sys_->setStreamScale(music_, volume_ * fade);
        if (fade <= 0) {
            music_.clear();
        }
    }
    sys_->frameUpdate();
}

void Sound::render(int16_t* stereo, int dspFrames) {
    sys_->render(stereo, dspFrames);
}

}   // namespace book
