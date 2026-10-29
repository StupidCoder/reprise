// The third adventure's talk window. See a3_talk.hpp.
#include "story/a3_talk.hpp"
#include "sound/a3_sound.hpp"
#include "story/a3_story.hpp"
#include <cstdio>

namespace picarat::adventure3 {
namespace {
using lscr::fx::kOne;
constexpr int kFade = 15 * kOne; // the window's fades (020c497c: 0xf000)
// 020c497c's tail sequences by direction (ov6 0x020f5af8).
const char* const kTails[7] = {"win_m", "win_ll", "win_lm", "win_lr", "win_rr", "win_rm", "win_rl"};

int alpha5(int alpha) {
    return std::clamp((alpha * 31 + 2048) >> 12, 0, 31);
}

// Draws a sprite's current frame at its place with its alpha.
void drawSprite(Surface& screen, const lscr::Object& o) {
    if (!o.animation || !o.sequence || o.sequence->steps.empty()) {
        return;
    }
    const int a = alpha5(o.alpha.value[0]);
    if (a <= 0) {
        return;
    }
    int t = o.time >> 12, acc = 0;
    const auto* st = &o.sequence->steps.back();
    for (const auto& s : o.sequence->steps) {
        acc += int(s.duration);
        if (t < acc) {
            st = &s;
            break;
        }
    }
    const int x = (o.position.value[0] >> 12) + st->x - o.sequence->originX;
    const int y = (o.position.value[1] >> 12) + st->y - o.sequence->originY;
    if (a >= 31) {
        o.animation->drawFrame(screen, st->frame, x, y);
        return;
    }
    Surface layer{256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
    o.animation->drawFrame(layer, st->frame, x, y);
    blit(screen, layer, 0, 0, a / 2, 16 - a / 2);
}

void stepSprite(lscr::Object& o) {
    o.alpha.step(kOne);
    if (o.sequence && !o.paused) {
        int length = 0;
        for (const auto& s : o.sequence->steps) {
            length += int(s.duration);
        }
        o.time += kOne;
        if (length > 0 && o.time >= length << 12) {
            if (o.once) {
                o.time = length << 12;
            } else {
                o.time %= length << 12;
            }
        }
    }
}
} // namespace

TalkWindow::TalkWindow(Story& story, int group, int id) : story_(story), group_(group), id_(id) {
    // 020f2ce4: txt/?/GG.cpck, member GG_IIIIII.
    char path[64];
    std::snprintf(path, sizeof path, "txt/?/%02d/%02d_%06d.lbin", group, group, id);
    if (!story.assets.exists(path)) {
        return;
    }
    const auto s = story.assets.script(path);
    for (const auto& c : s.commands) {
        if (c.opcode == 6001 && c.operands.size() >= 3) {
            lines_.push_back({int(std::int32_t(c.operands[0].value)), s.text(c.operands[1]),
                              s.text(c.operands[2])});
        }
    }
}

std::optional<std::pair<std::string, std::string>> TalkWindow::line(int number, int index) const {
    for (const auto& l : lines_) {
        if (l.number == number && index-- == 0) {
            return std::pair{l.speaker, l.text};
        }
    }
    return std::nullopt;
}

bool TalkWindow::show(lscr::Scene& scene, lscr::Object& message, int number) {
    // 020c41fc: the lines with that number, in order; 020c47cc: the window
    // and the pen (ani/win_talk.cani, ani/icon_pen.cani), hidden.
    int first = -1;
    last_ = -1;
    for (int i = 0; i < int(lines_.size()); ++i) {
        if (lines_[size_t(i)].number == number) {
            if (first < 0) {
                first = i;
            }
            last_ = i;
        } else if (first >= 0) {
            break;
        }
    }
    if (first < 0) {
        state_ = 0;
        return false;
    }
    current_ = first;
    window_ = {};
    window_.animation = &scene.assets.animation("ani/win_talk.cani");
    window_.alpha.set({0});
    pen_ = {};
    pen_.animation = &scene.assets.animation("ani/icon_pen.cani");
    pen_.alpha.set({0});
    for (auto [o, name] :
         {std::pair{&window_, "MSG_TALK_MAIN"}, std::pair{&pen_, "MSG_TALK_WAIT"}}) {
        if (auto p = scene.assets.position(name)) {
            o->position.set({p->x * kOne, p->y * kOne});
        }
    }
    if (auto p = scene.assets.position("MSG_TALK_FONT")) {
        message.position.set({p->x * kOne, p->y * kOne});
    }
    message.text.clear();
    speaker_.clear();
    plate_ = nullptr;
    direction_ = 0;
    state_ = 1;
    return true;
}

lscr::Object* TalkWindow::speakerObject(lscr::Scene& scene) const {
    return speaker_.empty() ? nullptr : scene.find(speaker_);
}

bool TalkWindow::speaking(const lscr::Object& message) const {
    // 020c4588: in the text state, something shown, not thinking - with a
    // voice while it plays (the game also follows its loudness), else while
    // glyphs still come.
    const auto& t = message.text;
    if (state_ != 4 || t.shown() == 0 || t.thinking()) {
        return false;
    }
    if (voicePlaying_) {
        return story_.sound && story_.sound->streamPlaying();
    }
    return !t.waitingForTap() && !t.finished();
}

bool TalkWindow::onStep(lscr::Scene& scene, lscr::Object& message, const MessageStep& s) {
    // 020c3e9c.
    Adventure3Sound* snd = story_.sound;
    switch (s.kind) {
    case MessageStep::Effect:
        if (s.closing) {
            return !(snd && snd->effectPlaying(s.a));
        }
        if (snd) {
            snd->playEffect(s.a);
        }
        return true;
    case MessageStep::Jingle:
        // 020d07b4: an ordinary stream (hypothesis); </J#> waits for it.
        if (s.closing) {
            return !(snd && (snd->musicPlaying() || snd->streamPlaying()));
        }
        if (snd) {
            snd->openStream(Adventure3Sound::Ordinary, s.a);
            snd->startStream(1.0f, 0, 0);
        }
        return true;
    case MessageStep::Voice:
        // 020d07e8: the event's voice line; while it plays there is no
        // typing sound (+0xf2 bit 9).
        if (!s.closing) {
            voicePlaying_ = true;
            if (snd) {
                snd->openStream(Adventure3Sound::Event, group_ << 16 | id_, s.a);
                snd->startStream(1.0f, 0, 0);
            }
        } else {
            voiceEnded_ = true;
        }
        return true;
    case MessageStep::Expression: {
        // 020c4650: the event script's section named by the number runs on
        // the message's own thread.
        const std::string name = std::to_string(s.a);
        if (scene.machine.find(name)) {
            sectionThread_ = std::make_unique<lscr::Thread>(scene.machine);
            sectionThread_->call(name, {});
        }
        return true;
    }
    case MessageStep::Pose:
    case MessageStep::Face:
    case MessageStep::Animation: {
        // 02066534 / 0206656c / 02066510: the speaker's sequence
        // "aa_bb_..." (pose a, face b; P and F keep the current face - as
        // the game does, F also puts the current face first).
        lscr::Object* o = speakerObject(scene);
        if (!o || !o->animation) {
            return true;
        }
        int face = -1;
        if (o->sequence && o->sequence->name.size() > 5 && o->sequence->name[2] == '_' &&
            o->sequence->name[5] == '_') {
            const auto& n = o->sequence->name;
            face = (std::isdigit(static_cast<unsigned char>(n[3])) ? n[3] - '0' : 0) * 10 +
                   (std::isdigit(static_cast<unsigned char>(n[4])) ? n[4] - '0' : 0);
        }
        int a = s.a, b = s.b;
        if (s.kind == MessageStep::Pose) {
            b = face;
        } else if (s.kind == MessageStep::Face) {
            a = face;
            b = s.a;
        }
        if (a < 0 || b < 0) {
            return true;
        }
        char prefix[16];
        std::snprintf(prefix, sizeof prefix, "%02d_%02d_", a, b);
        for (const auto& seq : o->animation->sequences) {
            if (seq.name.rfind(prefix, 0) == 0) {
                lscr::play(*o, &seq, 0);
                break;
            }
        }
        return true;
    }
    default:
        (void)message;
        return true;
    }
}

void TalkWindow::update(lscr::Scene& scene, lscr::Object& message) {
    if (sectionThread_ && sectionThread_->running()) {
        sectionThread_->run();
    }
    auto& text = message.text;
    if (!text.hooks.step) {
        text.style.speed = 0x1000; // 020c4414
        text.style.waitAtEnd = true;
        text.hooks.step = [this, &scene, &message](const MessageStep& s) {
            return onStep(scene, message, s);
        };
        text.hooks.insert = [this](char tag, int n) -> std::string {
            // 020c3dd8: <N0> / <N1> the two names.
            if (tag == 'N' && n >= 0 && n < 2) {
                return story_.names[size_t(n)];
            }
            return {};
        };
    }
    const bool tapped = scene.input().pressed || (scene.input().keysPressed & 1u);
    if (state_ >= 1 && state_ <= 7 && current_ <= last_) {
        const Line& line = lines_[size_t(current_)];
        if (state_ == 1) {
            // A new speaker: the tail towards them, their typing sound and
            // name plate.
            speaker_.clear();
            plate_ = nullptr;
            direction_ = 0;
            text.setTyping(-1, 0);
            if (const CharacterInfo* ch = story_.data.character(line.speaker)) {
                speaker_ = ch->name;
                text.setTyping(ch->typingSound, 5);
                const std::string plate = "chr/?/" + ch->plate + ".cimg";
                if (story_.assets.exists(plate)) {
                    plate_ = &story_.assets.picture(plate);
                }
            }
            if (lscr::Object* o = speakerObject(scene)) {
                if (o->alpha.value[0] > 0 && o->shown()) {
                    const int x = o->position.value[0] >> 12;
                    const int d = x - 128;
                    if (std::abs(d) > 32) {
                        if (d < 0) {
                            direction_ = x < 40 ? 1 : x > 64 ? 3 : 2;
                        } else {
                            direction_ = x > 216 ? 4 : x < 192 ? 6 : 5;
                        }
                    }
                }
                o->talking = false;
            }
            voicePlaying_ = voiceEnded_ = false;
            window_.alpha.to(1, {kOne}, kFade);
            state_ = 3;
        }
        lscr::play(window_, window_.animation->sequence(kTails[direction_]), 4);
        if (state_ == 3 && window_.alpha.value[0] >= kOne) {
            text.set(line.text, story_.text);
            state_ = 4;
        }
        if (state_ == 4) {
            if (lscr::Object* o = speakerObject(scene)) {
                o->talking = speaking(message);
            }
            if (!voicePlaying_ && text.waitingForTap()) {
                if (!pen_.alpha.active() && pen_.alpha.value[0] < kOne) {
                    pen_.alpha.to(1, {kOne}, kFade);
                    lscr::play(pen_, pen_.animation->sequence("w_pen"), 0);
                }
            } else {
                pen_.alpha.set({0});
            }
        }
    }
    // The message reveals (0206b5b4); a tap ends a line's wait.
    const bool wasWaiting = text.waitingForTap();
    text.update(tapped);
    if (wasWaiting && !text.waitingForTap() && voicePlaying_ && story_.sound) {
        story_.sound->stopStream(6); // 020603a4(6): the tap cuts the voice
    }
    if (state_ == 4 && text.finished()) {
        if (lscr::Object* o = speakerObject(scene)) {
            o->talking = false;
        }
        const std::string speaker = lines_[size_t(current_)].speaker;
        ++current_;
        if (current_ > last_) {
            state_ = 7;
        } else if (lines_[size_t(current_)].speaker == speaker) {
            voicePlaying_ = voiceEnded_ = false;
            text.set(lines_[size_t(current_)].text, story_.text);
        } else {
            state_ = 6;
        }
        if (state_ != 4) {
            text.clear();
            window_.alpha.to(1, {0}, kFade);
            pen_.alpha.to(1, {0}, kFade);
        }
    }
    if (state_ == 6 && window_.alpha.value[0] <= 0) {
        plate_ = nullptr;
        speaker_.clear();
        state_ = 1;
    }
    if (state_ == 7 && window_.alpha.value[0] <= 0) {
        speaker_.clear();
        plate_ = nullptr;
        state_ = 8;
    }
    stepSprite(window_);
    stepSprite(pen_);
}

void TalkWindow::draw(const lscr::Scene& scene, Surface& screen, lscr::Screen which,
                      const lscr::Object& message) const {
    if (which != lscr::Screen::Lower || state_ == 0 || state_ == 8) {
        return;
    }
    drawSprite(screen, window_);
    // 020c4fc4: the speaker's name plate at MSG_TALK_NAME, as visible as
    // the window.
    if (plate_ && window_.alpha.value[0] > 0) {
        if (auto p = scene.assets.position("MSG_TALK_NAME")) {
            const int a = alpha5(window_.alpha.value[0]);
            if (a >= 31) {
                blit(screen, plate_->image, p->x, p->y);
            } else {
                Surface layer{256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
                blit(layer, plate_->image, p->x, p->y);
                blit(screen, layer, 0, 0, a / 2, 16 - a / 2);
            }
        }
    }
    message.text.draw(screen, message.position.value[0] >> 12, message.position.value[1] >> 12,
                      scene.messagePalette);
    drawSprite(screen, pen_);
}

} // namespace picarat::adventure3
