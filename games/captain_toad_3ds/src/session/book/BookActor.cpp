// See BookActor.hpp.
#include "session/book/BookActor.hpp"

#include "session/book/BookSound.hpp"

namespace book {

const char* msgName(Msg m) {
    switch (m) {
    case Msg::Appear:
        return "Appear";
    case Msg::Appeared:
        return "Appeared";
    case Msg::BottomIntro:
        return "BottomIntro";
    case Msg::Leave:
        return "Leave";
    case Msg::Close:
        return "Close";
    case Msg::Closed:
        return "Closed";
    case Msg::Open:
        return "Open";
    case Msg::Opened:
        return "Opened";
    case Msg::BottomReady:
        return "BottomReady";
    case Msg::Chosen:
        return "Chosen";
    case Msg::ChosenDone:
        return "ChosenDone";
    case Msg::EnableInput:
        return "EnableInput";
    case Msg::DisableInput:
        return "DisableInput";
    case Msg::CourseOpenDone:
        return "CourseOpenDone";
    case Msg::ShowOpen:
        return "ShowOpen";
    case Msg::CourseOpen:
        return "CourseOpen";
    case Msg::MarkLocked:
        return "MarkLocked";
    case Msg::TurnDone:
        return "TurnDone";
    case Msg::NewLoop:
        return "NewLoop";
    case Msg::CloseWipeFilter:
        return "CloseWipeFilter";
    case Msg::SpotlightAppear:
        return "SpotlightAppear";
    case Msg::SpotlightEnd:
        return "SpotlightEnd";
    }
    return "?";
}

Journal::Journal(const char* path) {
    if (path && *path) {
        f_ = std::fopen(path, "w");
    }
}

Journal::~Journal() {
    if (f_) {
        std::fclose(f_);
    }
}

void Journal::action(const std::string& actor, const std::string& tag, const std::string& slot, float frame) {
    if (!f_) {
        return;
    }
    std::fprintf(f_, "%d\tACT\t%s\t%s\t%s", tick, actor.c_str(), tag.c_str(), slot.empty() ? "-" : slot.c_str());
    if (frame != 0) {
        std::fprintf(f_, "\t%g", frame);
    }
    std::fputc('\n', f_);
}

void Journal::message(const std::string& from, const std::string& to, Msg m, int arg) {
    if (f_) {
        std::fprintf(f_, "%d\tMSG\t%s\t%s\t%#x\t%d\t%s\n", tick, from.c_str(), to.c_str(), int(m), arg, msgName(m));
    }
}

bool LayoutActor::start(const std::string& tag, const std::string& slot, float frame) {
    if (journal_.on()) {
        journal_.action(name_, tag, slot, frame);
    }
    if (sound_) {
        sound_->action(name_, soundDb_, tag);   // 0x297ff0, also for tags the layout lacks
    }
    return layout_ && layout_->start(tag, slot, frame);
}

void LayoutActor::playSound(const std::string& playInfo, float param) {
    if (sound_) {
        sound_->play(soundDb_, playInfo, param);
    }
}

bool LayoutActor::hold(const std::string& tag, const std::string& slot, float frame) {
    return layout_ && layout_->hold(tag, slot, frame);
}

}   // namespace book
