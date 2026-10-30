// doc-verify: compares the reconstruction with reference runs of the original (touchHLE
// harness traces, see .analysis/docs/reference-runs.md). Research tool, not part of the game.
//
//   doc-verify <SpyMouse.app> <trace.log> <script.txt> [first-frame]
//
// 1. Follower check: for every traced frame, the original's player state of the previous frame
//    (position, heading, path index, path points, whether cheese is carried) is loaded into
//    the port's Mouse, one 17 ms tick is run, and the result must equal the next frame bit
//    for bit. This isolates Player::update from input handling.
// 2. Session replay: the touch script is replayed into a port Session from `first-frame`;
//    mouse position and path are compared every frame until the first difference.
#include <cctype>
#include "game/float_mode.hpp"
#include "data/game_files.hpp"
#include "data/gui_layout.hpp"
#include "data/strings.hpp"
#include "data/level_table.hpp"
#include "game/session.hpp"
#include "game/state_io.hpp"
#include "app/script_pokes.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <zlib.h>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace doc;

namespace {

struct PlayerDump {
    int state, pathCount, resampled, smoothed;
    float x, y, reach, heading, acceleration, lastStep, distance;
    int pathIndex;
    std::uint32_t carried;
    bool drawing;  // path being drawn
    std::vector<game::PathPoint> points;
};

// Where the original (Spy Mouse 1.1.1) keeps the compared fields. Player and object offsets
// were found by aligning the same functions of 1.0.5 and 1.1.1 and checked against the 1.1.1
// level 1-1 run (v111_l11_obj); fields not yet seen in a 1.1.1 run are marked "(hypothesis)".
namespace orig {
// Player (SceneGame + 0x12e2c, 0x868 bytes; Player::update @0x1252bc). Its Path: points
// pointer +0x308, count +0x310, resampled +0x314, smoothed +0x318, drawing +0x330,
// snapped +0x331, snap target +0x334.
constexpr std::size_t kPlayerSize = 0x868;
constexpr std::size_t kState = 0x2b0, kCarried = 0x2f4;
constexpr std::size_t kPathCount = 0x310, kResampled = 0x314, kSmoothed = 0x318;
constexpr std::size_t kDrawing = 0x330, kSnapped = 0x331, kSnapTarget = 0x334;
constexpr std::size_t kX = 0x33c, kY = 0x340, kReach = 0x358, kHeading = 0x35c, kPathIndex = 0x360;
constexpr std::size_t kAcceleration = 0x3b0, kIdleStep = 0x3b4, kIdleMs = 0x3b8, kLastStep = 0x3bc;
constexpr std::size_t kDistance = 0x3c0, kScale = 0x3c4;
constexpr std::size_t kPredictedCheese = 0xc8, kPredictedEnough = 0xd0, kPredictedSnapped = 0xd1;
constexpr std::size_t kSlideX = 0x3d0, kSlideY = 0x3d4, kSlideMs = 0x3d8;  // (hypothesis)
// Objects (SceneGame + 0x139d8, 0x884 bytes each): unchanged up to +0x110, 8 bytes later after
// (tools/v111/fieldvote.py over the Object and controller functions).
// +6 is the snap target as in 1.0.5. 1.0.5's "open" byte (+7) is 0 for every object in the 1.1.1
// runs and is not compared (HYPOTHESIS: the field is gone; ObjControllerHideable::init no longer
// writes it).
constexpr std::size_t kObjCarried = 0x180, kObjSnappable = 6;
// Cats and the bosses' controllers (ObjControllerCat / ObjControllerBoss; hypothesis beyond
// the heading fields until the cat runs are recorded on 1.1.1).
constexpr std::size_t kRouteIndex = 0x174, kRouteDir = 0x17c, kVelocity = 0x1b4, kSeen = 0x1d0;
constexpr std::size_t kStep = 0x1dc, kTarget = 0x1e0, kTimer = 0x1e4, kCalm = 0x1ec, kLook = 0x1f0;
constexpr std::size_t kStepTimer = 0x1f4, kStreak = 0x1f8, kBackCount = 0x14c, kBackIndex = 0x178;
constexpr std::size_t kWindUp = 0x200, kDue = 0x204, kThrowing = 0x20c;
constexpr std::size_t kIdleFrame = 0x308 + 0x64, kFacing = 0x82c, kAnimated = 0x87c;  // (idle sprite +0x308: stun @0x1a2fd0 sets its frame +0x36c)
// LevelState (g_LevelState 0x5fa290): 1.1.1 adds a byte at +0x1b; results requested +0x1d.
constexpr std::size_t kResultsRequested = 0x1d;
// Scene vtables (the top scene's first word).
constexpr std::uint32_t kDossierVtable = 0x5f6af8, kMsgBoxVtable = 0x5f5138;  // (1.1.1)
constexpr std::uint32_t kIntroVtable = 0x5f39b8, kSkipVtable = 0x5f5b68, kHandVtable = 0x5f5aa8,
                        kResultsVtable = 0x5f5958;
// GameBoot::bootLevel's draw (its return address): bootLevel @0x1f4fb8 + 0x159.
constexpr unsigned long kBootDrawCaller = 0x1F5111;
}  // namespace orig

template <typename T>
T at(const std::vector<std::uint8_t>& b, std::size_t o) {
    T v;
    std::memcpy(&v, &b[o], sizeof v);
    return v;
}

std::vector<std::uint8_t> hexBytes(const std::string& hex) {
    std::vector<std::uint8_t> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint8_t(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    return out;
}

// Reference traces are kept gzip-compressed (trace.log.gz, disk budget); zlib reads plain files
// as they are, so either form works.
class TraceReader {
public:
    explicit TraceReader(const std::string& path) {
        std::string p = path;
        if (!std::filesystem::exists(p) && std::filesystem::exists(p + ".gz")) p += ".gz";
        file_ = gzopen(p.c_str(), "rb");
        if (file_) gzbuffer(file_, 1 << 20);
    }
    ~TraceReader() {
        if (file_) gzclose(file_);
    }
    TraceReader(const TraceReader&) = delete;
    TraceReader& operator=(const TraceReader&) = delete;
    bool getline(std::string& line) {
        line.clear();
        if (!file_) return false;
        char buffer[1 << 16];
        while (gzgets(file_, buffer, sizeof buffer)) {
            line += buffer;
            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
                return true;
            }
        }
        return !line.empty();
    }

private:
    gzFile file_ = nullptr;
};

std::map<int, PlayerDump> readTrace(const std::string& path) {
    std::map<int, PlayerDump> frames;
    std::map<int, std::vector<std::uint8_t>> points;
    TraceReader in(path);
    std::string line;
    while (in.getline(line)) {
        std::istringstream ss(line);
        int frame;
        std::string kind, name, addr, len, hex;
        if (!(ss >> frame >> kind >> name) || kind != "watch") continue;
        if (!(ss >> addr >> len >> hex)) continue;
        auto b = hexBytes(hex);
        if (name == "player" && b.size() >= orig::kPlayerSize) {
            using namespace orig;
            PlayerDump d{};
            d.state = at<int>(b, kState);
            d.pathCount = at<int>(b, kPathCount);
            d.resampled = at<int>(b, kResampled);
            d.smoothed = at<int>(b, kSmoothed);
            d.x = at<float>(b, kX);
            d.y = at<float>(b, kY);
            d.reach = at<float>(b, kReach);
            d.heading = at<float>(b, kHeading);
            d.pathIndex = at<int>(b, kPathIndex);
            d.acceleration = at<float>(b, kAcceleration);
            d.lastStep = at<float>(b, kLastStep);
            d.distance = at<float>(b, kDistance);
            d.carried = at<std::uint32_t>(b, kCarried);
            d.drawing = b[kDrawing] != 0;
            frames[frame] = d;
        } else if (name == "pathpts") {
            points[frame] = b;
        }
    }
    for (auto& [frame, d] : frames) {
        auto it = points.find(frame);
        if (it == points.end()) continue;
        for (int i = 0; i < d.pathCount && std::size_t(i * 12 + 12) <= it->second.size(); ++i)
            d.points.push_back({at<float>(it->second, i * 12), at<float>(it->second, i * 12 + 4),
                                at<std::uint16_t>(it->second, i * 12 + 8)});
    }
    return frames;
}

bool same(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

void loadPath(game::DrawnPath& path, const PlayerDump& d) {
    path.clear();
    for (const auto& p : d.points) {
        game::PathPoint& q = path.append({p.x, p.y});
        q.flag = p.flag;
    }
    path.resampledEnd = d.resampled;
    path.smoothedEnd = d.smoothed;
}

// Level 1-1's only cheese (map type 8) weighs 11.5, truncated to 11 (Player::updateTrail).
float weightFor(const PlayerDump& d, const game::Tuning& tuning) {
    if (!d.carried) return 0.0f;
    int type = 8;
    return game::trailWeight(&type, 1, tuning);
}

int followerCheck(const std::map<int, PlayerDump>& frames) {
    game::Tuning tuning;
    int checked = 0, failed = 0;
    for (auto it = frames.begin(); std::next(it) != frames.end(); ++it) {
        const PlayerDump& before = it->second;
        const PlayerDump& after = std::next(it)->second;
        if (std::next(it)->first != it->first + 1) continue;
        if (before.points.size() != std::size_t(before.pathCount)) continue;
        if (before.state != 0 || after.state != 0) continue;
        // Only ticks without drawing: input runs before the mouse and reshapes the path.
        if (before.drawing || after.drawing) continue;
        game::Mouse mouse;
        mouse.position = {before.x, before.y};
        mouse.heading = before.heading;
        mouse.pathIndex = before.pathIndex;
        mouse.reachDistance = before.reach;
        mouse.acceleration = before.acceleration;
        game::DrawnPath path;
        loadPath(path, before);
        mouse.tick(game::kTickMs, path, weightFor(before, tuning), tuning);
        mouse.normaliseHeadingForFrame();
        ++checked;
        bool ok = same(mouse.position.x, after.x) && same(mouse.position.y, after.y) &&
                  same(mouse.heading, after.heading) && mouse.pathIndex == after.pathIndex &&
                  path.size() == after.pathCount;
        if (!ok) {
            if (failed < 8)
                std::printf("  follower frame %d: port (%.9g, %.9g) h=%.9g idx=%d n=%d | original (%.9g, %.9g) h=%.9g idx=%d n=%d\n",
                            std::next(it)->first, mouse.position.x, mouse.position.y, mouse.heading, mouse.pathIndex,
                            path.size(), after.x, after.y, after.heading, after.pathIndex, after.pathCount);
            ++failed;
        }
    }
    std::printf("follower check: %d ticks compared, %d differ\n", checked, failed);
    return failed;
}

struct ScriptEvent {
    int frame;
    std::string kind;
    float x, y;
};

std::vector<ScriptEvent> readScript(const std::string& path) {
    std::vector<ScriptEvent> events;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        ScriptEvent e{};
        if (!(ss >> e.frame >> e.kind)) continue;
        if (e.kind != "down" && e.kind != "move" && e.kind != "up") continue;
        ss >> e.x >> e.y;
        events.push_back(e);
    }
    return events;
}

// The profile the script set with pokes (the power-ups; see app/script_pokes.hpp).
app::ScriptProfile gScriptProfile;

// rand() calls of the original by frame: value and caller (return address).
struct RandCall {
    long value;
    unsigned long caller;
};
struct RandTrace {
    std::map<int, std::vector<RandCall>> byFrame;
    int bootFrame = -1;       // frame of GameBoot::bootLevel's draw
    long stateBeforeBoot = 1; // generator state just before it
};

RandTrace readRandTrace(const std::string& path) {
    RandTrace t;
    TraceReader in(path);
    std::string line;
    long previous = 1;
    while (in.getline(line)) {
        std::istringstream ss(line);
        int frame;
        std::string kind, value, lr;
        if (!(ss >> frame >> kind) || kind != "rand") continue;
        if (!(ss >> value >> lr)) continue;
        RandCall c{std::stol(value), std::stoul(lr.substr(3), nullptr, 16)};
        if (c.caller == orig::kBootDrawCaller && t.bootFrame < 0) {
            t.bootFrame = frame;
            t.stateBeforeBoot = previous;
        }
        if (t.bootFrame >= 0) t.byFrame[frame].push_back(c);
        previous = c.value;
    }
    return t;
}

// Raw watch dumps by name and frame, for the object and level-state comparisons.
using Watches = std::map<std::string, std::map<int, std::vector<std::uint8_t>>>;

Watches readWatches(const std::string& path) {
    Watches w;
    TraceReader in(path);
    std::string line;
    while (in.getline(line)) {
        std::istringstream ss(line);
        int frame;
        std::string kind, name, addr, len, hex;
        if (!(ss >> frame >> kind >> name) || kind != "watch") continue;
        if (!(ss >> addr >> len >> hex)) continue;
        if (name == "player" || name == "levelstate" || name.rfind("obj", 0) == 0 || name == "hudA" || name == "hudB" ||
            name == "pscore" || name == "top" || name == "ripple" || name == "height" || name == "proj")
            w[name][frame] = hexBytes(hex);
    }
    return w;
}

// A layout item's box in screen points (y down), the way the app's hit test finds it
// (render/results_view.cpp findBox).
bool layoutBox(const data::GuiNode& node, const std::string& name, float px, float py, float box[4]) {
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += px;
        y += py;
    }
    if (node.name == name) {
        box[0] = x;
        box[1] = 320.0f - (y + node.size.y);
        box[2] = node.size.x;
        box[3] = node.size.y;
        return true;
    }
    for (const data::GuiNode& child : node.children)
        if (layoutBox(child, name, x, y, box)) return true;
    return false;
}

// Collects differences between port and original for one frame.
struct Diff {
    std::vector<std::string> lines;
    // DOC_VERIFY_SKIP=name,name: fields left out (known, documented differences).
    static bool skipped(const char* what) {
        static const std::string skip = std::getenv("DOC_VERIFY_SKIP") ? "," + std::string(std::getenv("DOC_VERIFY_SKIP")) + "," : "";
        if (skip.empty()) return false;
        // ("cat12.heading" is skipped by "cat.heading" too.)
        std::string name = what;
        if (name.rfind("cat", 0) == 0) {
            std::size_t digits = 3;
            while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))) ++digits;
            if (digits > 3 && skip.find("," + ("cat" + name.substr(digits)) + ",") != std::string::npos) return true;
        }
        return skip.find("," + name + ",") != std::string::npos;
    }
    void f(const char* what, float port, float orig) {
        if (!same(port, orig) && !skipped(what)) add(what, port, orig);
    }
    void i(const char* what, long port, long orig) {
        if (port != orig && !skipped(what)) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "  %-22s port %ld original %ld", what, port, orig);
            lines.push_back(buf);
        }
    }
    void add(const char* what, double port, double orig) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "  %-22s port %.9g original %.9g", what, port, orig);
        lines.push_back(buf);
    }
};

// Original object layout: +4 active, +5 updating, +6 snap target, +7 open, +0x1c state,
// +0x2c pulse, +0xf4 position, +0x104 radius, carried (orig::kObjCarried).
void compareObject(Diff& diff, const game::LevelObject& o, const std::vector<std::uint8_t>& b) {
    std::string p = "obj" + std::to_string(o.id) + ".";
    auto name = [&](const char* field) {
        static std::string keep[16];
        static int slot = 0;
        std::string& s = keep[slot++ & 15];
        s = p + field;
        return s.c_str();
    };
    diff.i(name("active"), o.active, b[4]);
    diff.i(name("updating"), o.updating, b[5]);
    diff.i(name("snappable"), o.snappable, b[orig::kObjSnappable]);
    diff.i(name("state"), int(o.state), at<int>(b, 0x1c));
    diff.f(name("pulse"), o.pulseMs, at<float>(b, 0x2c));
    diff.f(name("x"), o.position.x, at<float>(b, 0xf4));
    diff.f(name("y"), o.position.y, at<float>(b, 0xf8));
    diff.f(name("radius"), o.radius, at<float>(b, 0x104));
    diff.i(name("carried"), o.inTrail, b[orig::kObjCarried]);
    // (Sprite +0x38, frame +0x64; an object scrolled away in a chase is freed and cleared, run
    // lb2_win: its picture is not compared any more.)
    if (o.frames > 1 && b[4]) diff.i(name("frame"), o.anim.frame(), at<int>(b, 0x9c));
}

int sessionReplay(const data::GameFiles& files, const std::map<int, PlayerDump>& frames, const Watches& watches,
                  const RandTrace& rands, const std::vector<ScriptEvent>& script) {
    std::string error;
    auto table = data::LevelTable::load(files, error);
    static const char* levelNumber = std::getenv("DOC_LEVEL") ? std::getenv("DOC_LEVEL") : "1-1";
    const data::LevelInfo* info = table ? table->byNumber(levelNumber) : nullptr;
    auto level = info ? game::Level::load(files, *info, error) : std::nullopt;
    const std::optional<game::Level> levelFiles = level;  // (for quick-save tests)
    if (!level) {
        std::printf("session replay: %s\n", error.c_str());
        return 1;
    }
    if (rands.bootFrame < 0) {
        std::printf("session replay: no level start in the trace\n");
        return 1;
    }
    // The session starts where the original booted the level (in the frame after the draws
    // labelled bootFrame were logged), with its generator state.
    const int firstFrame = rands.bootFrame + 1;
    // A restart (retry, or the level lost) replaces the session, as in the app.
    auto makeSession = [&]() {
        auto session = std::make_unique<game::Session>(game::Level(*levelFiles), std::uint32_t(rands.stateBeforeBoot));
        // A profile that has not seen Kiska's offer (the reference states have not), by the clock.
        session->setKiskaOffers({}, std::int64_t(std::time(nullptr)));
        // (Reference runs play through level 1-1's or 1-2's slot: too early for Digger by itself.)
        session->setPowerups(gScriptProfile.powerups, gScriptProfile.tutorialDone, gScriptProfile.metDigger);
        // The reference states play later levels through another level's slot, whose saved best
        // the trace does not show: a run whose level state ends without the new-best flag
        // (LevelState +0x1d, Profile::completeLevel) is given a best it cannot beat.
        if (const auto ls = watches.find("levelstate"); ls != watches.end() && !ls->second.empty()) {
            const std::vector<std::uint8_t>& last = ls->second.rbegin()->second;
            if (last.size() > 0x1d && last[0x1d] == 0) {
                game::LevelRecord record;
                record.best = std::numeric_limits<int>::max();
                session->setRecord(record);
            }
        }
        return session;
    };
    auto owner = makeSession();
    // DOC_QUICKSAVE=K: every K frames the session is quick-saved and loaded into a new one built
    // from the level's files (game/state_io.hpp), which plays on; an uninterrupted twin plays the
    // same touches, and the two states must stay the same byte for byte.
    const int quickSaveEvery = std::getenv("DOC_QUICKSAVE") ? std::atoi(std::getenv("DOC_QUICKSAVE")) : 0;
    std::unique_ptr<game::Session> twin = quickSaveEvery > 0 ? makeSession() : nullptr;
    int quickSaves = 0;
    auto saved = [](const game::Session& s, std::vector<game::StateWriter::Mark>* marks = nullptr) {
        game::StateWriter w;
        w.marks = marks;
        w.save(s);
        return std::move(w.bytes);
    };
    const auto kiskaLayout = data::loadGuiLayout(files, "level_skip", error);  // ("levelSkip" in 1.0.5)
    // (1.1.1) The dossier page's back button and the message box's "Okay".
    float backBox[4] = {0, 0, -1, -1}, okBox[4] = {0, 0, -1, -1};
    if (const auto layout = data::loadGuiLayout(files, "dossier", error))
        for (const data::GuiNode& top : layout->root.children)
            if (layoutBox(top, "BTN_BACK", 0.0f, 0.0f, backBox)) break;
    if (const auto layout = data::loadGuiLayout(files, "msgbox", error))
        for (const data::GuiNode& top : layout->root.children)
            if (layoutBox(top, "BTN_OK", 0.0f, 0.0f, okBox)) break;
    auto inBox = [](const float box[4], float x, float y) {
        return x >= box[0] && x < box[0] + box[2] && y >= box[1] && y < box[1] + box[3];
    };
    // The tutorial popup's continue button: a release over it closes the popup (GuiButton answers
    // on release; DOC_NOTICE_ANY=1 takes any release, as the 1.0.5 scripts assumed).
    float continueBox[4] = {0, 0, -1, -1};
    if (const auto popup = data::loadGuiLayout(files, "popup_tutorial", error))
        for (const data::GuiNode& top : popup->root.children)
            if (layoutBox(top, "BTN_CONTINUE", 0.0f, 0.0f, continueBox)) break;
    static const bool anyRelease = std::getenv("DOC_NOTICE_ANY") != nullptr;
    auto onContinue = [&](float x, float y) {
        return anyRelease || (x >= continueBox[0] && x < continueBox[0] + continueBox[2] && y >= continueBox[1] &&
                              y < continueBox[1] + continueBox[3]);
    };
    if (std::getenv("DOC_NOTICE_BOX"))
        std::printf("popup continue button: %.1f,%.1f %.1fx%.1f\n", continueBox[0], continueBox[1], continueBox[2],
                    continueBox[3]);
    int lastFrame = frames.empty() ? 0 : frames.rbegin()->first;
    if (auto w = watches.find("top"); w != watches.end() && !w->second.empty())
        lastFrame = std::max(lastFrame, w->second.rbegin()->first);
    int randFrames = 0;
    std::uint64_t drawsBefore = 0;
    auto checkDraws = [&](int frame) -> bool {
        // Same number of draws in the tick shown as `frame` and the same generator state after
        // them. The harness labels a draw with the number of frames presented so far, so the
        // tick shown as frame N logs its draws under N - 1.
        auto it = rands.byFrame.find(frame - 1);
        const std::size_t expected = it == rands.byFrame.end() ? 0 : it->second.size();
        const std::uint64_t drawn = owner->random().draws() - drawsBefore;
        drawsBefore = owner->random().draws();
        const bool stateOk = expected == 0 || long(owner->random().state()) == it->second.back().value;
        if (drawn == expected && stateOk) {
            ++randFrames;
            return true;
        }
        std::printf("rand: first difference at frame %d: port drew %llu, original %zu\n", frame,
                    (unsigned long long)drawn, expected);
        if (it != rands.byFrame.end())
            for (const RandCall& c : it->second) std::printf("  original rand %ld from %#lx\n", c.value, c.caller);
        return false;
    };
    if (!checkDraws(firstFrame)) return 1;
    int compared = 0, resultsFrame = -1, originalResultsFrame = -1;
    int objectChecks = 0, levelChecks = 0, playerChecks = 0, handChecks = 0, resultsChecks = 0;
    auto lookup = [&](const char* name, int frame) -> const std::vector<std::uint8_t>* {
        auto w = watches.find(name);
        if (w == watches.end()) return nullptr;
        auto it = w->second.find(frame);
        return it == w->second.end() ? nullptr : &it->second;
    };
    // A script event into a session.
    auto feed = [&](game::Session& s, const ScriptEvent& e) {
        // A popup over the level takes the touches; the scripts tap only its button.
        static const char* noticeKind = std::getenv("DOC_NOTICE_ON") ? std::getenv("DOC_NOTICE_ON") : "up";
        if (s.notice().active() && e.kind == noticeKind && onContinue(e.x, e.y)) s.pressNoticeButton();
        if (s.kiska().active() && e.kind == "up" && kiskaLayout)
            s.pressKiskaButton(game::KiskaPopup::buttonAt(kiskaLayout->root, {e.x, e.y}));
        if (s.dossierScreen().active() && e.kind == "up") {
            if (s.dossierScreen().note().active) {
                if (inBox(okBox, e.x, e.y)) s.pressDossierNote();
            } else if (inBox(backBox, e.x, e.y)) {
                s.pressDossierBack();
            }
        }
        if (e.kind == "down") s.fingerDown({e.x, e.y});
        if (e.kind == "move") s.fingerMove({e.x, e.y});
        if (e.kind == "up") s.fingerUp({e.x, e.y});
    };
    for (int frame = firstFrame + 1; frame <= lastFrame; ++frame) {
        // The harness injects events after presenting frame N; the game handles them in the
        // next tick, whose result is presented as frame N + 1.
        for (const ScriptEvent& e : script) {
            if (e.frame != frame - 1) continue;
            feed(*owner, e);
            if (twin) feed(*twin, e);
        }
        owner->tick(game::kTickMs);
        // The level restarts (lost, or a retry): a new session takes over in the same tick.
        if (owner->retryRequested()) owner = owner->retry();
        owner->prepareFrame();
        if (twin) {
            twin->tick(game::kTickMs);
            if (twin->retryRequested()) twin = twin->retry();
            twin->prepareFrame();
            if ((frame - firstFrame) % quickSaveEvery == 0) {
                const std::vector<std::uint8_t> bytes = saved(*owner);
                auto loaded = makeSession();
                game::StateReader r{bytes};
                try {
                    r.load(*loaded);
                    if (r.at != bytes.size()) throw game::StateError("bytes left over");
                } catch (const std::exception& e) {
                    std::printf("quick save: loading at frame %d failed: %s\n", frame, e.what());
                    return 1;
                }
                owner = std::move(loaded);
                ++quickSaves;
            }
            std::vector<game::StateWriter::Mark> marks;
            const std::vector<std::uint8_t> a = saved(*owner), b = saved(*twin, &marks);
            if (a != b) {
                std::size_t at = 0;
                while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
                std::printf("quick save: first difference at frame %d (after %d loads), byte %zu of %zu/%zu, in:\n",
                            frame, quickSaves, at, a.size(), b.size());
                for (const game::StateWriter::Mark& m : marks)
                    if (m.begin <= at && at < m.end)
                        std::printf("  %*s%s +%zu\n", 2 * m.depth, "", m.type, at - m.begin);
                return 1;
            }
        }
        // DOC_DUMP_PARTICLES=frame: the port's live particles at that frame.
        if (const char* dump = std::getenv("DOC_DUMP_PARTICLES"); dump && std::atoi(dump) == frame)
            for (std::size_t i = 0; i < owner->particles().slots().size(); ++i)
                if (const game::Particle& p = owner->particles().slots()[i]; p.active)
                    std::printf("  particle %zu type %#x floor %d pos (%.1f,%.1f) scale %.3f alpha %.3f frame %d age %d delay %d\n", i,
                                p.type, p.floor, p.position.x, p.position.y, p.scale, p.alpha, p.sprite.frame(), p.ageMs, p.delayMs);
        // The results screen gone (continued, or by its time-out): the original goes on to the
        // world map, which a level replay does not follow.
        if (owner->results().done()) {
            std::printf("results left at frame %d\n", frame);
            lastFrame = frame - 1;
            break;
        }
        // DOC_VERIFY_RAND_SOFT=1: report a rand difference but go on to the fields of that
        // frame (the cause often shows there), stopping after it.
        static const bool randSoft = std::getenv("DOC_VERIFY_RAND_SOFT") != nullptr;
        bool randStop = false;
        if (!checkDraws(frame)) {
            if (!randSoft) return 1;
            randStop = true;
        }
        if (owner->progress().finished && owner->progress().won && resultsFrame < 0) resultsFrame = frame;
        // The top scene (watch "top": [[g_pEng]+0x48]-4 → scene): the level intro, the
        // demonstrating finger or the level itself.
        if (const auto* b = lookup("top", frame)) {
            const std::uint32_t vtable = at<std::uint32_t>(*b, 0);
            const game::HandTutorial& h = owner->hand();
            Diff diff;
            // (A tutorial popup goes on top of the intro: ScenePopupTutorial.)
            const bool popup = owner->notice().active() || owner->kiska().active() || owner->dossierScreen().active();
            diff.i("top is dossier", owner->dossierScreen().active() && !owner->dossierScreen().note().active,
                   vtable == orig::kDossierVtable);
            diff.i("top is note", owner->dossierScreen().note().active, vtable == orig::kMsgBoxVtable);
            diff.i("top is intro", owner->intro().active && !popup, vtable == orig::kIntroVtable);
            diff.i("top is kiska", owner->kiska().active(), vtable == orig::kSkipVtable);  // SceneLevelSkip
            diff.i("top is finger", !popup && !owner->intro().active && h.active(), vtable == orig::kHandVtable);
            if (vtable == orig::kHandVtable) {
                ++handChecks;
                // SceneHandTutorial: +0xb8 delay, +0xbc wait, +0xc8 leg, +0xe8 fingertip,
                // +0xf0 velocity, +0xf8 state (2 playing); statics @0x5fc4dc ripple, @0x5f1bfc lift.
                diff.i("finger.delay", h.delayMs(), at<int>(*b, 0xb8));
                diff.i("finger.playing", h.playing(), at<int>(*b, 0xf8) == 2);
                diff.i("finger.leg", h.index(), at<int>(*b, 0xc8));
                diff.i("finger.wait", h.waitMs(), at<int>(*b, 0xbc));
                diff.f("finger.x", h.position().x, at<float>(*b, 0xe8));
                diff.f("finger.y", h.position().y, at<float>(*b, 0xec));
                diff.f("finger.vx", h.velocity().x, at<float>(*b, 0xf0));
                diff.f("finger.vy", h.velocity().y, at<float>(*b, 0xf4));
                if (const auto* r = lookup("ripple", frame)) diff.i("finger.ripple", h.rippleState(), at<int>(*r, 0));
                if (const auto* l = lookup("height", frame)) diff.f("finger.lift", h.lift(), at<float>(*l, 0));
            }
            if (vtable == orig::kResultsVtable && b->size() >= 0x170) {  // (shorter watches miss its fields)
                ++resultsChecks;
                // SceneGameResults (1.1.1, each 8 after 1.0.5's): +0x124 state, +0x128 timer,
                // +0x12c points, +0x134 bonus, +0x13c total, +0x140 alpha, +0x150 cheese badge, +0x154 best badge, +0x168 ring.
                const game::ResultsScreen& r = owner->results();
                diff.i("results.active", r.active() || owner->progress().finished, 1);  // pushed, runs next tick
                diff.i("results.state", r.state(), at<int>(*b, 0x124));
                diff.i("results.timer", r.timerMs(), at<int>(*b, 0x128));
                diff.f("results.points", r.points(), at<float>(*b, 0x12c));
                diff.f("results.bonus", r.bonusShown(), at<float>(*b, 0x134));
                diff.f("results.total", r.total(), at<float>(*b, 0x13c));
                diff.f("results.alpha", r.alpha(), at<float>(*b, 0x140));
                diff.f("results.cheeseBadge", r.cheeseScale(), at<float>(*b, 0x150));
                diff.f("results.badge", r.badgeScale(), at<float>(*b, 0x154));
                diff.f("results.ring", r.outline(), at<float>(*b, 0x168));
            }
            if (!diff.lines.empty()) {
                std::printf("session replay: first top-scene difference at frame %d\n", frame);
                for (const std::string& l : diff.lines) std::printf("%s\n", l.c_str());
                return 1;
            }
        }
        auto it = frames.find(frame);
        if (it == frames.end()) continue;
        const PlayerDump& d = it->second;
        const game::Mouse& m = owner->mouse();
        const game::DrawnPath& p = owner->path();
        Diff diff;
        // DOC_TRACE_MOUSE=from,to: the mouse in both, frame by frame.
        if (const char* t = std::getenv("DOC_TRACE_MOUSE")) {
            int from = 0, to = 0;
            if (std::sscanf(t, "%d,%d", &from, &to) == 2 && frame >= from && frame <= to)
                std::printf("  mouse f%d port (%.4f,%.4f) s%d idx%d n%d view %.4f | original (%.4f,%.4f) s%d idx%d n%d\n", frame,
                            m.position.x, m.position.y, m.state, m.pathIndex, p.size(), owner->viewLeft(), d.x, d.y, d.state, d.pathIndex,
                            d.pathCount);
        }
        diff.f("mouse.x", m.position.x, d.x);
        diff.f("mouse.y", m.position.y, d.y);
        // (Player::render wraps the heading into 0..2pi; the trace sometimes catches it before the
        // frame's render, the same angle unwrapped: compared as an angle, run l110_win.)
        {
            float orig = d.heading;
            if (orig < 0.0f && m.heading >= 0.0f) orig = std::fmod(orig, 6.28318548f) + 6.28318548f;
            diff.f("mouse.heading", m.heading, orig);
        }
        diff.i("mouse.state", m.state, d.state);
        diff.i("path.index", m.pathIndex, d.pathIndex);
        diff.i("path.count", p.size(), d.pathCount);
        diff.i("carrying", !owner->carried().empty(), d.carried != 0);
        // DOC_TRACE_PATH=frame: both paths' points at that frame.
        if (const char* f = std::getenv("DOC_TRACE_PATH"); f && std::atoi(f) == frame)
            for (int i = 0; i < std::max(p.size(), int(d.points.size())); ++i)
                std::printf("  path %d port (%.3f,%.3f) original (%.3f,%.3f)\n", i, i < p.size() ? p[i].x : 0.0f,
                            i < p.size() ? p[i].y : 0.0f, i < int(d.points.size()) ? d.points[i].x : 0.0f,
                            i < int(d.points.size()) ? d.points[i].y : 0.0f);
        int pointDiff = -1;
        for (int i = 0; i < p.size() && i < int(d.points.size()); ++i)
            if (!same(p[i].x, d.points[i].x) || !same(p[i].y, d.points[i].y)) {
                pointDiff = i;
                diff.add(("path point " + std::to_string(i) + ".x").c_str(), p[i].x, d.points[i].x);
                diff.add(("path point " + std::to_string(i) + ".y").c_str(), p[i].y, d.points[i].y);
                break;
            }
        if (const auto* b = lookup("player", frame)) {
            ++playerChecks;
            using namespace orig;
            diff.i("path.drawing", p.drawing, (*b)[kDrawing]);
            diff.i("path.snapped", p.snapped, (*b)[kSnapped]);
            diff.i("path.has-target", p.snapTarget >= 0, at<std::uint32_t>(*b, kSnapTarget) != 0);
            // (Player::playerPredictivePathUpdate: 1.0.5's +4/+0xc/+0xd moved to +0xc8/+0xd0/+0xd1.)
            diff.i("predicted cheese", owner->prediction().cheeseAhead, at<int>(*b, kPredictedCheese));
            diff.i("predicted enough", owner->prediction().enoughCheese, (*b)[kPredictedEnough]);
            diff.i("predicted snapped", owner->prediction().snappedToCheese, (*b)[kPredictedSnapped]);
            diff.f("slide.x", m.slidePosition.x, at<float>(*b, kSlideX));
            diff.f("slide.y", m.slidePosition.y, at<float>(*b, kSlideY));
            diff.i("slide.remaining", m.slideRemainingMs, at<int>(*b, kSlideMs));
            diff.f("mouse.scale", m.drawScale, at<float>(*b, kScale));
            diff.f("mouse.distance", m.distanceWalked, at<float>(*b, kDistance));
            // The idle animations' step and time.
            diff.i("mouse.idleStep", m.idleStep, at<int>(*b, kIdleStep));
            diff.i("mouse.idleMs", m.idleMs, at<int>(*b, kIdleMs));
        }
        for (const game::LevelObject& o : owner->objects())
            if (const auto* b = lookup(("obj" + std::to_string(o.id)).c_str(), frame)) {
                // DOC_TRACE_OBJ=id: print that object's port and original state every frame.
                static const char* traced = std::getenv("DOC_TRACE_OBJ");
                if (traced && (std::atoi(traced) == o.id || (std::atoi(traced) == -1 && o.inTrail)))
                    std::printf("  obj%d t%x f%d mouse (%.3f,%.3f) m%d port (%.9g,%.9g) s%d c%d | original (%.9g,%.9g) s%d c%d\n", o.id, o.type, frame, m.position.x, m.position.y, m.state,
                                o.position.x, o.position.y, int(o.state), int(o.inTrail), at<float>(*b, 0xf4),
                                at<float>(*b, 0xf8), at<int>(*b, 0x1c), int((*b)[orig::kObjCarried]));
                compareObject(diff, o, *b);
                for (const game::Cat& cat : owner->cats())
                    if (cat.objectId() == o.id && (*b)[4]) {  // (a freed cat's memory is cleared)
                        // DOC_TRACE_CAT=frame: the cat's route and way back at that frame.
                        static const char* catFrame = std::getenv("DOC_TRACE_CAT");
                        if (catFrame && std::atoi(catFrame) == frame) {
                            for (const auto& pt : cat.route().points) std::printf("  route (%.4f,%.4f)\n", pt.position.x, pt.position.y);
                            for (const auto& pt : cat.returnPath()) std::printf("  back (%.4f,%.4f)\n", pt.x, pt.y);
                        }
                        // Cat (ObjControllerCat): +0x1c state, +0x108 heading, +0x10c aim,
                        // +0x110 steering, +0x16c route point, +0x1ac velocity, +0x1dc
                        // awareness timer, +0x1f0 collision streak.
                        const std::string catField = "cat" + std::to_string(o.id) + ".";
                        if (traced && std::atoi(traced) == o.id)
                            std::printf("  cat%d f%d s%d/%d h %.5f/%.5f aim %.5f/%.5f steer %.5f/%.5f step %d/%d v %.4f,%.4f/%.4f,%.4f seen %d/%d trail %d/%d awr %d/%d\n", o.id, frame,
                                        cat.state(), at<int>(*b, 0x1c), cat.heading(), at<float>(*b, 0x108), cat.targetHeading(),
                                        at<float>(*b, 0x10c), cat.steerOffset(), at<float>(*b, 0x110), cat.chaseStep(), at<int>(*b, orig::kStep),
                                        cat.velocity().x, cat.velocity().y, at<float>(*b, orig::kVelocity), at<float>(*b, orig::kVelocity + 4),
                                        int(cat.sawMouse()), int((*b)[0x1d8]), 0, int((*b)[0x1d9]), cat.awarenessMs(), at<int>(*b, orig::kTimer));
                        diff.i((catField + "state").c_str(), cat.state(), at<int>(*b, 0x1c));
                        diff.f((catField + "heading").c_str(), cat.heading(), at<float>(*b, 0x108));
                        diff.f((catField + "aim").c_str(), cat.targetHeading(), at<float>(*b, 0x10c));
                        diff.f((catField + "steer").c_str(), cat.steerOffset(), at<float>(*b, 0x110));
                        diff.i((catField + "routeIndex").c_str(), cat.routeIndex(), at<int>(*b, orig::kRouteIndex));
                        diff.f((catField + "vx").c_str(), cat.velocity().x, at<float>(*b, orig::kVelocity));
                        diff.f((catField + "vy").c_str(), cat.velocity().y, at<float>(*b, orig::kVelocity + 4));
                        diff.i((catField + "awareness").c_str(), cat.awarenessMs(), at<int>(*b, orig::kTimer));
                        diff.i((catField + "streak").c_str(), cat.collisionStreak(), at<int>(*b, orig::kStreak));
                        diff.i((catField + "step").c_str(), cat.chaseStep(), at<int>(*b, orig::kStep));
                        diff.i((catField + "target").c_str(), cat.targetId(), at<int>(*b, orig::kTarget));
                        diff.i((catField + "calm").c_str(), cat.calmMs(), at<int>(*b, orig::kCalm));
                        diff.i((catField + "look").c_str(), cat.lookMs(), at<int>(*b, orig::kLook));
                        diff.i((catField + "stepTimer").c_str(), cat.chaseTimerMs(), at<int>(*b, orig::kStepTimer));
                        diff.f((catField + "seenX").c_str(), cat.seenAt().x, at<float>(*b, orig::kSeen));
                        diff.f((catField + "seenY").c_str(), cat.seenAt().y, at<float>(*b, orig::kSeen + 4));
                        diff.i((catField + "backCount").c_str(), int(cat.returnPath().size()), at<int>(*b, orig::kBackCount));
                        diff.i((catField + "backIndex").c_str(), cat.returnIndex(), at<int>(*b, orig::kBackIndex));
                        diff.i("cat.idleFrame", cat.idle.frame(), at<int>(*b, orig::kIdleFrame));
                        diff.i("cat.facing", cat.facing(), at<int>(*b, orig::kFacing));
                        diff.i("cat.animated", cat.animatedMs, at<int>(*b, orig::kAnimated));
                    }
                for (const game::Spitter& sp : owner->spitters())
                    if (sp.objectId() == o.id) {
                        // Spitter (ObjControllerBoss, updateSpitter): +0x108 heading, +0x10c aim,
                        // +0x16c route point, +0x174 direction, +0x1ac velocity, +0x1dc
                        // cooldown, +0x1f8 wind-up, +0x1fc throws due, +0x204 throwing.
                        diff.f("spitter.heading", sp.heading(), at<float>(*b, 0x108));
                        diff.f("spitter.aim", sp.aim(), at<float>(*b, 0x10c));
                        diff.i("spitter.routeIndex", sp.routeIndex(), at<int>(*b, orig::kRouteIndex));
                        diff.i("spitter.routeDir", sp.routeDirection(), at<int>(*b, orig::kRouteDir));
                        diff.f("spitter.vx", sp.velocity().x, at<float>(*b, orig::kVelocity));
                        diff.f("spitter.vy", sp.velocity().y, at<float>(*b, orig::kVelocity + 4));
                        diff.i("spitter.cooldown", sp.cooldownMs(), at<int>(*b, orig::kTimer));
                        diff.i("spitter.windUp", sp.windUpMs(), at<int>(*b, orig::kWindUp));
                        diff.i("spitter.due", sp.throwsDue(), at<int>(*b, orig::kDue));
                        diff.i("spitter.throwing", sp.throwing(), at<int>(*b, orig::kThrowing));
                    }
                for (const game::Vacuum& v : owner->vacuums())
                    if (v.objectId() == o.id) {
                        // Vacuum (ObjControllerBoss, updateVaccuum): +0x108 heading, +0x10c aim,
                        // +0x16c route point, +0x174 direction, +0x1ac velocity, +0x1d4 suction
                        // stage, +0x1dc timer.
                        diff.f("vacuum.heading", v.heading(), at<float>(*b, 0x108));
                        diff.f("vacuum.aim", v.aim(), at<float>(*b, 0x10c));
                        diff.i("vacuum.routeIndex", v.routeIndex(), at<int>(*b, orig::kRouteIndex));
                        diff.i("vacuum.routeDir", v.routeDirection(), at<int>(*b, orig::kRouteDir));
                        diff.f("vacuum.vx", v.velocity().x, at<float>(*b, orig::kVelocity));
                        diff.f("vacuum.vy", v.velocity().y, at<float>(*b, orig::kVelocity + 4));
                        diff.i("vacuum.stage", v.stage(), at<int>(*b, orig::kStep));
                        diff.i("vacuum.timer", v.timerMs(), at<int>(*b, orig::kTimer));
                    }
                for (const game::RailTarget& t : owner->railTargets())
                    if (t.objectId == o.id) {
                        diff.f("sphere.heading", t.heading, at<float>(*b, 0x108));
                        diff.f("sphere.aim", t.aim, at<float>(*b, 0x10c));
                        diff.i("sphere.routeIndex", t.walk.index, at<int>(*b, orig::kRouteIndex));
                        diff.i("sphere.routeDir", t.walk.direction, at<int>(*b, orig::kRouteDir));
                        // (+0x1dc holds 500 from Object's constructor until the first stop.)
                        if (t.state == 0x1c || at<int>(*b, orig::kTimer) != 500) diff.i("sphere.stop", t.stopMs, at<int>(*b, orig::kTimer));
                    }
                for (const game::BeamEmitter& e : owner->emitters())
                    if (e.objectId == o.id) {
                        diff.i("device.step", e.step, at<int>(*b, orig::kStep));
                        diff.i("device.timer", e.timerMs, at<int>(*b, orig::kTimer));
                        diff.i("device.partner", e.partner, at<int>(*b, orig::kTarget));
                    }
                for (const game::Droid& droid : owner->droids())
                    if (droid.objectId() == o.id) {
                        // Droid (ObjControllerBoss): +0x108 heading, +0x10c aim, +0x16c rail
                        // point, +0x174 rail direction, +0x1ac velocity, +0x1d4 step.
                        diff.i("droid.step", droid.step(), at<int>(*b, orig::kStep));
                        diff.f("droid.heading", droid.heading(), at<float>(*b, 0x108));
                        diff.f("droid.aim", droid.aim(), at<float>(*b, 0x10c));
                        diff.i("droid.railIndex", droid.railIndex(), at<int>(*b, orig::kRouteIndex));
                        diff.i("droid.railDir", droid.railDirection(), at<int>(*b, orig::kRouteDir));
                        diff.f("droid.vx", droid.velocity().x, at<float>(*b, orig::kVelocity));
                        diff.f("droid.vy", droid.velocity().y, at<float>(*b, orig::kVelocity + 4));
                    }
                ++objectChecks;
            }
        // The projectiles (watch "proj": SceneGame +0x11664, 16 slots of 0x11c bytes): +0 slot,
        // +4 status, +0x14 picture, +0xdc position, +0xe8 angle, +0xf0 velocity, +0x104 timer,
        // +0x10c stuck to.
        if (const auto* pw = lookup("proj", frame); pw && pw->size() >= 16 * 0x11c) {
            for (int k = 0; k < game::Projectiles::kSlots; ++k) {
                const game::Projectile& q = owner->projectiles().slots()[std::size_t(k)];
                const std::size_t base = std::size_t(k) * 0x11c;
                auto pi = [&](std::size_t off) { return at<int>(*pw, base + off); };
                auto pf = [&](std::size_t off) { return at<float>(*pw, base + off); };
                const std::string n = "proj" + std::to_string(k) + ".";
                diff.i((n + "slot").c_str(), q.slot, pi(0));
                if (q.slot < 0 && pi(0) < 0) continue;
                diff.i((n + "status").c_str(), int(q.status), pi(4));
                diff.i((n + "picture").c_str(), q.picture, pi(0x14));
                diff.f((n + "x").c_str(), q.position.x, pf(0xdc));
                diff.f((n + "y").c_str(), q.position.y, pf(0xe0));
                diff.f((n + "angle").c_str(), q.angle, pf(0xe8));
                diff.f((n + "vx").c_str(), q.velocity.x, pf(0xf0));
                diff.f((n + "vy").c_str(), q.velocity.y, pf(0xf4));
                diff.f((n + "timer").c_str(), q.timerMs, pf(0x104));
                diff.i((n + "stuck").c_str(), q.stuckTo, pi(0x10c));
                diff.i((n + "frame").c_str(), q.anim.frame(), pi(0x1c + 0x64));
            }
        }
        // HUD state (SceneGame +0x137a4.., +0xb8.., Player +0x650..), when the run watched it.
        // (1.1.1: Player +0x670 shown score, +0x674 last score (scoreCheese @0x12131c), +0x6a0
        // exit bonus.)
        if (const auto* a = lookup("hudA", frame)) {
            const game::Hud& h = owner->hud();
            diff.f("hud.handBounce", h.handBounce, at<float>(*a, 0x10));
            diff.f("hud.handWidth", h.handWidth, at<float>(*a, 0x14));
            diff.i("hud.scoreBounce", h.scoreBounceMs, at<int>(*a, 0x1b0));
            diff.i("hud.glow", h.glowMs, at<int>(*a, 0x1bc));
            diff.i("hud.fade", h.fadeMs, at<int>(*a, 0x1c4));
            if (const auto* b = lookup("hudB", frame))
                for (int k = 0; k < 3; ++k) {
                    diff.f("hud.item.x", h.items[k].position.x, at<float>(*b, 8 * k));
                    diff.f("hud.item.y", h.items[k].position.y, at<float>(*b, 8 * k + 4));
                    diff.i("hud.item.timer", h.items[k].timerMs, at<int>(*b, 0x60 + 4 * k));
                }
            if (const auto* p = lookup("pscore", frame)) {
                diff.f("hud.shownScore", h.shownScore, at<float>(*p, 0x20));
                diff.i("hud.lastScore", owner->progress().lastScore, at<int>(*p, 0x24));
                diff.i("hud.exitBonus", h.exitBonus, (*p)[0x50]);
            }
        }
        if (const auto* b = lookup("levelstate", frame)) {
            ++levelChecks;
            // LevelState (g_LevelState): +0x18 over, +0x19 won, results requested (see orig),
            // +0x20 clock, +0x24 delivered, +0x28 cheese total, +0x2c score, +0x48 total,
            // +0x4c distance.
            const game::LevelProgress& g = owner->progress();
            diff.i("level.over", g.over, (*b)[0x18]);
            diff.i("level.won", g.won, (*b)[0x19]);
            diff.i("level.clock", g.clock, at<int>(*b, 0x20));
            diff.i("level.delivered", g.delivered, at<int>(*b, 0x24));
            diff.i("level.cheese", g.cheeseTotal, at<int>(*b, 0x28));
            diff.i("level.score", g.score, at<int>(*b, 0x2c));
            diff.i("level.powerupUsed", owner->powerupUsed(), (*b)[0x5c]);  // (1.1.1) Profile::usePowerup
            // (The port fills them in when the level's numbers are taken: at the finish, or earlier
            // when the boss cat has seen the mouse.)
            const bool taken = g.finished || g.distance != 0;
            diff.i("level.total", taken ? g.total : 0, at<int>(*b, 0x48));
            diff.i("level.distance", taken ? g.distance : 0, at<int>(*b, 0x4c));
            if ((*b)[orig::kResultsRequested] && originalResultsFrame < 0) originalResultsFrame = frame;
        }
        ++compared;
        if (!diff.lines.empty()) {
            std::printf("session replay: first difference at frame %d after %d frames\n", frame, compared - 1);
            for (const std::string& l : diff.lines) std::printf("%s\n", l.c_str());
            if (pointDiff >= 0)
                std::printf("  (port path n=%d res=%d smo=%d, original n=%d res=%d smo=%d)\n", p.size(), p.resampledEnd,
                            p.smoothedEnd, d.pathCount, d.resampled, d.smoothed);
            return 1;
        }
        if (randStop) {
            std::printf("(no field differences at frame %d)\n", frame);
            return 1;
        }
    }
    std::printf("session replay from level start (frame %d): %d frames identical (%d player, %d object, %d level-state "
                "comparisons), rand() identical in %d frames\n",
                firstFrame, compared, playerChecks, objectChecks, levelChecks, randFrames);
    if (handChecks) std::printf("demonstrating finger identical in %d frames\n", handChecks);
    if (resultsChecks) std::printf("results screen identical in %d frames\n", resultsChecks);
    std::printf("results requested: port frame %d, original frame %d (port score %d, last hand %d, total %d)\n",
                resultsFrame, originalResultsFrame, owner->progress().score, int(owner->progress().lastHand),
                owner->progress().total);
    // (Without level-state watches the original's request frame is unknown.)
    return resultsFrame == originalResultsFrame || (levelChecks == 0 && originalResultsFrame < 0) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    doc::game::flushDenormalsToZero();
    // --res DIR: replacement files, as the reference run's sandbox Library/Caches/res.
    std::string overrideDir;
    if (argc > 2 && std::string(argv[1]) == "--res") {
        overrideDir = argv[2];
        argv += 2;
        argc -= 2;
    }
    if (argc < 4) {
        std::fprintf(stderr, "usage: doc-verify [--res DIR] <SpyMouse.app> <trace.log> <script.txt> [first-frame]\n");
        return 2;
    }
    std::string error;
    auto files = data::GameFiles::open(argv[1], error);
    if (!files) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    if (!overrideDir.empty()) files->setOverrideDir(overrideDir);
    // doc-verify APP --strings NAME...: the text table and string names (research check).
    if (std::string(argv[2]) == "--strings") {
        auto table = data::StringTable::load(*files, "EN", error);
        if (!table) return 2;
        std::printf("%d strings\n", table->size());
        for (int i = 3; i < argc; ++i) {
            const bool number = argv[i][0] >= '0' && argv[i][0] <= '9';
            const int id = number ? std::atoi(argv[i]) : table->id(argv[i]);
            std::printf("%s -> %d \"%s\"\n", argv[i], id, table->text(id).c_str());
        }
        return 0;
    }
    auto frames = readTrace(argv[2]);
    const Watches watches = readWatches(argv[2]);
    if (frames.empty() && !watches.count("top")) {
        std::fprintf(stderr, "no player or top-scene watches in %s\n", argv[2]);
        return 2;
    }
    auto script = readScript(argv[3]);
    gScriptProfile = app::readScriptProfile(argv[3]);
    int first = argc > 4 ? std::atoi(argv[4]) : frames.empty() ? 0 : frames.begin()->first;
    // The follower check assumes the shipped level's single cheese; replaced levels are checked
    // by the session replay alone.
    int failures = 0;
    if (frames.empty()) std::printf("follower check: skipped (no player watches)\n");
    else if (overrideDir.empty()) failures += followerCheck(frames);
    else std::printf("follower check: skipped (replaced level files)\n");
    (void)first;
    failures += sessionReplay(*files, frames, watches, readRandTrace(argv[2]), script);
    return failures ? 1 : 0;
}
