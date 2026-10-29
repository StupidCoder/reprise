// The fifth adventure's runtime. See a5_game.hpp.
#include "system/a5_game.hpp"
#include "graphics/a56_font.hpp"
#include "graphics/a56_model.hpp"
#include "graphics/a56_text.hpp"
#include "sound/a56_sound_archive.hpp"
#include "system/a56_movie.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace picarat::adventure5 {

namespace fs = std::filesystem;
using xq::Value;

const PuzzleRecord* Definitions::puzzle(int id) const {
    for (const auto& p : puzzles) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string definitionsPath(Edition edition) {
    return edition == Edition::Sixth ? "res/@/lt6_def" : "res/@/lt5_def";
}

std::string puzzleParametersPath(Edition edition) {
    return edition == Edition::Sixth ? "res/lt6_naz.xb" : "res/lt5_naz.xb";
}

Definitions loadDefinitions(const adventure56::Pack& pack, Edition edition) {
    Definitions d;
    const fs::path file = pack.find(definitionsPath(edition), ".xs");
    if (file.empty()) {
        throw std::runtime_error(definitionsPath(edition) + " not found");
    }
    const auto list = import::adventure56::parseXscr(pack.read(file));
    auto text = [&](const import::adventure56::Operand& o) -> std::string {
        if ((o.type & 15) != 8 || o.value >= list.strings.size()) {
            return {};
        }
        const auto* p = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return std::string(p, strnlen(p, list.strings.size() - o.value));
    };
    auto integer = [&](const import::adventure56::Operand& o) {
        return std::int32_t(o.value);
    };
    int event = -1; // the 2201 record the 2203 entries belong to
    for (const auto& c : list.commands) {
        const auto* op = list.operands.data() + c.first;
        if (c.opcode == 2201 && c.count >= 1) {
            event = integer(op[0]);
        } else if (c.opcode == 2202) {
            event = -1;
        } else if (c.opcode == 2203 && event == 0 && c.count >= 2) {
            d.initialState.push_back(
                {integer(op[0]), text(op[1]), c.count > 2 ? integer(op[2]) : 1});
        }
        if (c.opcode == 2021 && c.count >= 4) {
            Definitions::Item item;
            item.id = integer(op[0]);
            item.name = text(op[1]);
            item.kind = integer(op[2]);
            item.mask = integer(op[3]);
            item.text = c.count > 4 ? text(op[4]) : std::string();
            for (unsigned i = 5; i < c.count; ++i) {
                if (!text(op[i]).empty()) {
                    item.set.push_back(text(op[i]));
                }
            }
            d.itemByName[item.name] = item.id;
            d.items[item.id] = std::move(item);
        }
        if (c.opcode == 2001 && c.count >= 2) {
            d.flagIds[text(op[1])] = integer(op[0]);
        } else if (c.opcode == 2011 && c.count >= 3) {
            Definitions::Variable v{integer(op[0]), integer(op[1])};
            d.variables[text(op[2])] = v;
            d.variablesByOffset[v.offset] = v;
        } else if (c.opcode == 2111 && c.count >= 2) {
            d.characterIds[text(op[1])] = integer(op[0]);
        } else if (c.opcode == 2101 && c.count >= 4) {
            d.soundScenes[integer(op[0])] = {integer(op[1]), integer(op[2]), integer(op[3])};
        } else if (c.opcode == 2301 && c.count >= 8) {
            PuzzleRecord p;
            p.place = c.count > 8 ? text(op[8]) : std::string();
            p.lostStep = c.count > 9 ? text(op[9]) : std::string();
            p.unlockItem = c.count > 10 ? text(op[10]) : std::string();
            p.id = integer(op[0]);
            p.number = int(d.puzzles.size()) + 1;
            p.name = text(op[1]);
            p.title = text(op[2]);
            p.type = integer(op[3]);
            p.typeName = text(op[4]);
            for (int i = 0; i < 3; ++i) {
                p.picarats[i] = integer(op[5 + i]);
            }
            d.puzzles.push_back(std::move(p));
        }
    }
    return d;
}

std::string displayText(const std::string& text) {
    std::string out;
    for (const std::uint32_t c : adventure56::decodeText(text)) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        } else {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        }
    }
    return out;
}

namespace {

// lt5_def 2011's size code: the variable's bytes, negative when signed.
int sizeBytes(int code) {
    const int a = code < 0 ? -code : code;
    return std::clamp(a, 1, 4);
}

} // namespace

Game::Game(const std::string& packRoot, Edition edition_)
    : edition(edition_), pack(packRoot), defs(loadDefinitions(pack, edition_)), scene(pack),
      story(loadStoryTables(pack, edition_)) {
    int end = 0;
    for (const auto& [off, v] : defs.variablesByOffset) {
        end = std::max(end, off + sizeBytes(v.size) * 64);
    }
    variableBytes_.assign(std::size_t(end) + 64, 0);
    machine.missing = [this](int opcode, const xq::Program&, int) {
        ++missing[opcode];
    };
    // The text colour palette: fix.xr's 16x16 picture.
    if (const auto fix = pack.find("fix", ".xr"); !fix.empty() && fs::exists(fix / "000.xi")) {
        scene.palette = adventure56::loadTexture(pack, fix / "000.xi");
    }
    registerLibraryServices();
    registerGameServices();
    register3DServices();
    registerHandlerServices(); // after the 3D services: the shared 6xxx forms win
    registerStoryServices();
    registerMessageServices();
    registerStageServices();
    if (edition == Edition::Sixth) {
        registerSixthServices();
    }
}

Game::~Game() = default;

bool Game::flag(int id) const {
    return flags_.contains(id);
}

void Game::applyInitialState() {
    for (const auto& e : defs.initialState) {
        if (e.code == 1521) {
            setVariable(e.name, e.value);
        } else if (e.code == 1512) {
            if (const auto it = defs.flagIds.find(e.name); it != defs.flagIds.end()) {
                setFlag(it->second, true);
            }
        }
    }
}

void Game::setFlag(int id, bool on) {
    if (on) {
        flags_.insert(id);
    } else {
        flags_.erase(id);
    }
}

std::int32_t Game::variable(const Definitions::Variable& v, int slot) const {
    const int bytes = sizeBytes(v.size);
    const std::size_t at = std::size_t(v.offset + slot * bytes);
    if (slot < 0 || at + std::size_t(bytes) > variableBytes_.size()) {
        return 0;
    }
    std::uint32_t raw = 0;
    for (int i = 0; i < bytes; ++i) {
        raw |= std::uint32_t(variableBytes_[at + std::size_t(i)]) << (8 * i);
    }
    if (v.size < 0 && bytes < 4 && (raw >> (bytes * 8 - 1)) & 1) {
        raw |= ~0u << (bytes * 8);
    }
    return std::int32_t(raw);
}

void Game::setVariable(const Definitions::Variable& v, std::int32_t value, int slot) {
    const int bytes = sizeBytes(v.size);
    const std::size_t at = std::size_t(v.offset + slot * bytes);
    if (slot < 0 || at + std::size_t(bytes) > variableBytes_.size()) {
        return;
    }
    for (int i = 0; i < bytes; ++i) {
        variableBytes_[at + std::size_t(i)] = std::uint8_t(std::uint32_t(value) >> (8 * i));
    }
}

std::int32_t Game::variable(const std::string& name, int slot) const {
    const auto it = defs.variables.find(name);
    return it == defs.variables.end() ? 0 : variable(it->second, slot);
}

void Game::setVariable(const std::string& name, std::int32_t value, int slot) {
    const auto it = defs.variables.find(name);
    if (it != defs.variables.end()) {
        setVariable(it->second, value, slot);
    }
}

const xq::Program* Game::loadScript(const std::string& path) {
    for (const auto& [s, scope] : liveScripts_) {
        if (s->name() == path) {
            return s;
        }
    }
    fs::path file = pack.find(path, ".xq");
    if (file.empty()) {
        if (log) {
            log("script not found: " + path);
        }
        return nullptr;
    }
    auto program =
        std::make_unique<xq::Program>(path, import::adventure56::parseXq32(pack.read(file)));
    program->next = newest_;
    newest_ = program.get();
    liveScripts_.push_back({program.get(), scene.scopeDepth()});
    scripts_.push_back(std::move(program));
    return newest_;
}

void Game::openScope(bool snapshot) {
    scene.openScope(snapshot);
}

void Game::closeScope() {
    // Scripts loaded in the scope go with its objects and resources (the
    // programs stay in memory: a thread may still point into one).
    const int depth = scene.scopeDepth();
    scene.closeScope();
    if (depth <= 0 || scene.scopeDepth() == depth) {
        return;
    }
    std::erase_if(liveScripts_, [depth](const auto& s) { return s.second >= depth; });
    const xq::Program* previous = nullptr;
    for (auto& [s, scope] : liveScripts_) {
        s->next = previous;
        previous = s;
    }
    newest_ = previous;
}

void Game::closeScopesTo(int depth) {
    while (scene.scopeDepth() > depth) {
        const int before = scene.scopeDepth();
        closeScope();
        if (scene.scopeDepth() == before) {
            break;
        }
    }
}

const PuzzleRecord* Definitions::puzzleByName(const std::string& name) const {
    for (const auto& p : puzzles) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

void Game::setPuzzleState(int id, int state) {
    // 0x2d1fcc: the puzzle's slot (game +0x89a4, 165: 150 puzzles, then
    // Layton's 15 challenges) takes the state; a change counts. Seen for
    // the first time: found (みつけたナゾ数 / 挑戦状みつけたナゾ数) and
    // みつけたナゾ総数 go up. Solved: unless the demo flag (体験版) is set,
    // the item "＜ナゾ＞追加" with the puzzle's index and the puzzle's own
    // item (2301 field 10) are given; solved (とけたナゾ数 / 挑戦状とけた
    // ナゾ数) goes up, with "＜ナゾ＞コンプリート" at 150 and "＜ゲーム＞
    // コンプリート" past 14 challenges, "＜ナゾ＞最後部屋以外コンプ" when
    // both make 162; とけたナゾ総数 goes up and 総取得ピカラット gains the
    // puzzle's picarats for its reductions (at most 2).
    const PuzzleRecord* rec = defs.puzzle(id);
    if (!rec) {
        return;
    }
    // The slot: the record's place in lt5_def 2301 (number - 1).
    const int slot = rec->number - 1;
    if (slot < 0 || slot >= 165) {
        return;
    }
    auto& p = progress[id];
    const int old = p.state & 3;
    state &= 3;
    p.state = state;
    if (old == state) {
        return;
    }
    constexpr int kMain = 150;
    auto bump = [this](const char* name) {
        setVariable(name, variable(name) + 1);
    };
    auto giveNamed = [this](const std::string& name, int argument) {
        const auto it = defs.itemByName.find(name);
        if (it != defs.itemByName.end()) {
            giveItem(it->second, argument);
        }
    };
    if (old == 0) {
        if (slot < kMain) {
            bump("\x82\xdd\x82\xc2\x82\xaf\x82\xbd\x83\x69\x83\x5d\x90\x94"); // みつけたナゾ数
        } else {
            bump("\x92\xa7\x90\xed\x8f\xf3\x82\xdd\x82\xc2\x82\xaf\x82\xbd\x83\x69\x83\x5d"
                 "\x90\x94"); // 挑戦状みつけたナゾ数
        }
        bump(
            "\x82\xdd\x82\xc2\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94"); // みつけたナゾ総数
    }
    if (state != 2) {
        return;
    }
    const char* added = "\x81\x83\x83\x69\x83\x5d\x81\x84\x92\xc7\x89\xc1";  // ＜ナゾ＞追加
    const char* solved = "\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x90\x94"; // とけたナゾ数
    const char* solvedChallenges = "\x92\xa7\x90\xed\x8f\xf3\x82\xc6\x82\xaf\x82\xbd\x83\x69"
                                   "\x83\x5d\x90\x94"; // 挑戦状とけたナゾ数
    if (slot < kMain) {
        const int demo = flagId("\x91\xcc\x8c\xb1\x94\xc5"); // 体験版
        if (demo < 0 || !flag(demo)) {
            giveNamed(added, slot);
            giveNamed(rec->unlockItem, 0);
        }
        bump(solved);
        if (variable(solved) >= kMain) {
            giveNamed("\x81\x83\x83\x69\x83\x5d\x81\x84\x83\x52\x83\x93\x83\x76\x83\x8a\x81\x5b"
                      "\x83\x67",
                      0); // ＜ナゾ＞コンプリート
        }
    } else {
        giveNamed(added, slot);
        giveNamed(rec->unlockItem, 0);
        bump(solvedChallenges);
        if (variable(solvedChallenges) > 14) {
            giveNamed("\x81\x83\x83\x51\x81\x5b\x83\x80\x81\x84\x83\x52\x83\x93\x83\x76\x83\x8a"
                      "\x81\x5b\x83\x67",
                      0); // ＜ゲーム＞コンプリート
        }
    }
    if (variable(solved) + variable(solvedChallenges) - kMain == 12) {
        giveNamed("\x81\x83\x83\x69\x83\x5d\x81\x84\x8d\xc5\x8c\xe3\x95\x94\x89\xae\x88\xc8"
                  "\x8a\x4f\x83\x52\x83\x93\x83\x76",
                  0); // ＜ナゾ＞最後部屋以外コンプ
    }
    bump("\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94"); // とけたナゾ総数
    const int r = std::min(p.reductions, 2);
    const char* total =
        "\x91\x8d\x8e\xe6\x93\xbe\x83\x73\x83\x4a\x83\x89\x83\x62\x83\x67"; // 総取得ピカラット
    setVariable(total, variable(total) + rec->picarats[r]);
}

void Game::giveItem(const std::string& name) {
    const auto it = defs.itemByName.find(name);
    if (it != defs.itemByName.end()) {
        giveItem(it->second);
    }
}

void Game::giveItem(int id, int argument) {
    // 0x2abd4c's effect part (in 0x124870, code 1591).
    const auto it = defs.items.find(id);
    if (it == defs.items.end()) {
        return;
    }
    const auto& item = it->second;
    if (id >= 0x1e0) {
        // A set gives its items.
        for (const auto& n : item.set) {
            giveItem(n);
        }
        return;
    }
    if (itemNotices.size() >= 16) {
        // A full queue drops its oldest notice that has a mask.
        for (std::size_t i = 0; i < itemNotices.size(); ++i) {
            if (itemNotices[i].mask != 0) {
                itemNotices.erase(itemNotices.begin() + std::ptrdiff_t(i));
                break;
            }
        }
        if (itemNotices.size() >= 16) {
            return;
        }
    }
    auto setFlagNamed = [this](const char* n) {
        const int f = flagId(n);
        if (f >= 0) {
            setFlag(f, true);
        }
    };
    if (id >= 0x20 && id < 0x1c0) {
        // Items 32..447 are the flags of the same numbers (base flag 32
        // "＜アイテム＞" plus the index); three ranges also mark the
        // modes' "NEW" (robot, shop, rabbit).
        setFlag(id, true);
        if (unsigned(id - 0x4a) < 10) {
            setFlagNamed("\x81\x83\x82\x52\x82\xc2\x82\xcc\x83\x82\x81\x5b\x83\x68\x82\x6d"
                         "\x82\x64\x82\x76\x81\x84\x83\x8d\x83\x7b\x83\x62\x83\x67");
        } else if (unsigned(id - 0x55) < 11) {
            setFlagNamed("\x81\x83\x82\x52\x82\xc2\x82\xcc\x83\x82\x81\x5b\x83\x68\x82\x6d"
                         "\x82\x64\x82\x76\x81\x84\x83\x56\x83\x87\x83\x62\x83\x76");
        } else if (unsigned(id - 0x61) < 10) {
            setFlagNamed("\x81\x83\x82\x52\x82\xc2\x82\xcc\x83\x82\x81\x5b\x83\x68\x82\x6d"
                         "\x82\x64\x82\x76\x81\x84\x83\x45\x83\x54\x83\x4d");
        }
    } else if (id < 0x10) {
        // Counted items: byte `id` of the variable at offset 0.
        const auto v = defs.variablesByOffset.find(0);
        if (v != defs.variablesByOffset.end()) {
            setVariable(v->second, variable(v->second, id) + 1, id);
        }
    } else if (id == 0x1d1) {
        setFlagNamed(
            "\x83\x69\x83\x5d\x83\x52\x83\x93\x83\x76\x83\x8a\x81\x5b\x83\x67"); // ナゾコンプリート
    } else if (id == 0x1d2) {
        setFlagNamed("\x83\x51\x81\x5b\x83\x80\x83\x52\x83\x93\x83\x76\x83\x8a\x81\x5b"
                     "\x83\x67"); // ゲームコンプリート
    }
    itemNotices.push_back({id, argument, item.kind, item.mask});
}

void Game::beginFrame(const Input& in) {
    // The frame's input is read after the objects (0x37889c: the objects,
    // then 0x265360 / 0x257d6c, then the touch): the scripts and handlers
    // of a frame see the input of the frame before - the same reading that
    // started a handler's block.
    pendingInput_ = in;
}

void Game::absolutePad() {
    // 0x3a61f0 with the mapping of 1222 (0x339bbc): an affine map of the
    // press (+0x14 .. +0x2c: scales width / box width, height / box
    // height, offsets x0 - bx, y0 - by).
    auto& p = padCursor;
    const float sx = p.bw ? float(p.x1 - p.x0) / float(p.bw) : 1.0f;
    const float sy = p.bh ? float(p.y1 - p.y0) / float(p.bh) : 1.0f;
    p.x = float(p.x0 - p.bx) + float(input.x) * sx;
    p.y = float(p.y0 - p.by) + float(input.y) * sy;
}

void Game::readInput(const Input& in) {
    previousInput = input;
    input = in;
    moveX = moveY = 0;
    tapped = false;
    repeatPulse = false;
    // 0x3a61f0 counts in display frames (it adds the frames since its last
    // call): at 30 Hz a game frame is two.
    const int step = std::max(1, displayFramesPerFrame);
    if (input.touching) {
        if (previousInput.touching) {
            moveX = input.x - touchX;
            moveY = input.y - touchY;
            heldFrames += step;
            // The held touch's repeat (flag 0x400): 20 frames after the
            // press, then every 5.
            repeatCountdown -= step;
            if (repeatCountdown < 1) {
                while (repeatCountdown < 1) {
                    repeatCountdown += 5;
                }
                repeatPulse = true;
            }
            if (touchSourceId == 1) {
                auto& p = padCursor;
                if (p.absolute) {
                    absolutePad();
                } else {
                    p.x = std::clamp(p.x + float(moveX) * p.speed, float(p.x0), float(p.x1 - 1));
                    p.y = std::clamp(p.y + float(moveY) * p.speed, float(p.y0), float(p.y1 - 1));
                }
            }
        } else {
            pressX = input.x;
            pressY = input.y;
            heldFrames = 0;
            repeatCountdown = 20;
            repeatPulse = true;
            const auto& p = padCursor;
            touchSourceId = p.on && unsigned(input.x - p.bx) < unsigned(p.bw) &&
                                    unsigned(input.y - p.by) < unsigned(p.bh)
                                ? 1
                                : 0;
            if (touchSourceId == 1) {
                if (padCursor.absolute) {
                    absolutePad();
                }
                padCursor.pressX = int(std::floor(padCursor.x + 0.5f));
                padCursor.pressY = int(std::floor(padCursor.y + 0.5f));
            }
        }
        touchX = input.x;
        touchY = input.y;
    } else if (previousInput.touching) {
        // 0x3a61f0 at the release: a tap when the touch was held under 15
        // frames (counted from 0 at the press) and its last place is
        // within 8 pixels of the press (dx^2 + dy^2 < 64; flag 0x2000 ->
        // the input word's 0x40, read by 1205).
        const int dx = touchX - pressX, dy = touchY - pressY;
        tapped = heldFrames < 15 && dx * dx + dy * dy < 64;
    }
}

void Game::cameraJitter() {
    // 0x378dd4 -> 0x37ce58 after the objects: each camera in the scene's
    // list (+0x6c) updates (vtable +0x18, 0x221944); an active one (bit 0
    // of +0x2dc and +0x2e4 clear, 0x349b68: shown and active) draws two
    // random floats (0x2bcea8) for an offset (r x 2a - a, r x 2b - b, 0)
    // from its shake (+0x588 / +0x58c, 4300 / 4302) and stores it at +0x338
    // (vtable +0x50). Console trace in the rabbit room: the camera object
    // first, then the four screen views, two draws each per game frame.
    // *Not modelled*: the offset's use (+0x338's reader).
    auto jitter = [this](float a, float b) {
        const float x = machine.randomFloat(2.0f * a) - a;
        const float y = machine.randomFloat(2.0f * b) - b;
        return std::array<float, 2>{x, y};
    };
    // *Hypothesis* for which camera objects are in the list: those a screen
    // uses (4010); the rabbit room has cmr_move on screen 0 and one camera
    // object in the console's list, while an older one left unused is not.
    for (auto& [hash, rig] : scene.rigs) {
        if (std::find(std::begin(scene.screenRig), std::end(scene.screenRig), hash) !=
            std::end(scene.screenRig)) {
            rig.jitter = jitter(0, 0);
        }
    }
    // The four views the console lists are fixed members of the game
    // (0x650 apart from 0x429888); *hypothesis*: screens 0..3.
    for (int s = 0; s < 4; ++s) {
        auto& cam = scene.cameras[s];
        if (cam.viewShown && cam.viewActive) {
            cam.jitter = jitter(cam.shake[0], cam.shake[1]);
        }
    }
}

void Game::endFrame() {
    // 0x37889c after the loop's scripts: the shared tweens, each 2D object
    // (0x37b0cc), each 3D object (0x37b438) - its handler (0x263578), then
    // its tweens and animation, only while it updates - and then the touch
    // (0x37a728 / 0x37aaa4), which starts handler blocks that run from the
    // next frame on.
    scene.frameStep = float(std::max(1, displayFramesPerFrame));
    scene.updateShared();
    updateObjects();
    cameraJitter();
    readInput(pendingInput_);
    processTouch();
    // 0x37889c: the loop's look at the input (+0x50) after the touch.
    if (special) {
        special->input();
    }
    for (int i = 0; i < std::max(1, displayFramesPerFrame); ++i) {
        streams.tick();
    }
    for (auto it = volumeMoves.begin(); it != volumeMoves.end();) {
        VolumeMove& v = it->second;
        ++v.elapsed;
        const float t = v.frames > 0 ? std::min(1.0f, float(v.elapsed) / float(v.frames)) : 1.0f;
        streamVolume[std::size_t(it->first)] = v.from + (v.to - v.from) * t;
        if (sound) {
            sound->setStreamVolume(it->first, streamVolume[std::size_t(it->first)]);
        }
        it = t >= 1.0f ? volumeMoves.erase(it) : std::next(it);
    }
    // 0x381dfc: the movie loop's clock.
    std::erase_if(movies, [this](const std::weak_ptr<adventure56::MoviePlayer>& w) {
        const auto m = w.lock();
        if (m) {
            m->tick(float(displayFramesPerFrame));
        }
        return !m;
    });
    for (auto* list : {&shakes, &shakes2D}) {
        for (auto& [h, s] : *list) {
            s.step();
        }
    }
    dramaClock += float(displayFramesPerFrame);
    for (auto& e : activeEffects) {
        e.remaining -=
            double(adventure56::SoundPlayer::kRate) / adventure56::StreamPlayers::kFrameRate;
    }
    std::erase_if(activeEffects, [](const ActiveEffect& e) { return e.remaining <= 0; });
    ++frame;
}

adventure56::SoundArchive* Game::soundArchive() {
    if (!soundArchiveTried_) {
        soundArchiveTried_ = true;
        const fs::path file = pack.root() / "snd" / "product.bcsar";
        if (fs::exists(file)) {
            try {
                const Bytes bytes = pack.read(file);
                soundArchive_ = std::make_unique<adventure56::SoundArchive>(
                    std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
            } catch (const std::exception&) {
            }
        }
    }
    return soundArchive_.get();
}

double Game::effectDuration(const std::string& name) {
    if (const auto it = effectDurations_.find(name); it != effectDurations_.end()) {
        return it->second;
    }
    adventure56::SoundArchive* archive = soundArchive();
    double samples = 0;
    if (archive) {
        if (auto player = archive->openSequence(name)) {
            // Run it silently to its end (a looping effect stops counting
            // at 30 seconds and is taken as lasting).
            constexpr int kChunk = 1024;
            const int limit = adventure56::SoundPlayer::kRate * 30;
            std::vector<float> scratch(std::size_t(kChunk) * 2);
            bool going = true;
            while (going && samples < limit) {
                std::fill(scratch.begin(), scratch.end(), 0.0f);
                going =
                    player->render(scratch.data(), kChunk, adventure56::SoundPlayer::kRate, 0.0f);
                samples += kChunk;
            }
            if (going) {
                samples = 1e18;
            }
        }
    }
    effectDurations_[name] = samples;
    return samples;
}

const adventure56::StreamInfo* Game::streamInfo(const std::string& name) {
    auto [it, fresh] = streamInfo_.try_emplace(name);
    if (fresh) {
        // The archive's file table names the file (the sixth adventure's
        // carry a title: "JNG_415_Failure.dspadpcm.bcstm").
        const adventure56::SoundArchive* archive = soundArchive();
        const std::string path = archive ? archive->externalFile(name) : std::string();
        const fs::path file = path.empty() ? pack.root() / "snd" / "stream" / (name + ".bcstm")
                                           : pack.root() / "snd" / path;
        if (fs::exists(file)) {
            try {
                it->second = std::make_unique<adventure56::StreamInfo>(
                    adventure56::readStreamInfo(pack.read(file)));
            } catch (const std::exception&) {
            }
        }
    }
    return it->second.get();
}

void Game::startHandler(adventure56::Object& o, int state) {
    // 0x2cd1d0: the state's block (table 0x3e92f0 maps states 0..6 to the
    // name slots 0, 1, 2, 3, 4, 0, 5); a missing block falls back to state 0.
    static const int kSlot[7] = {0, 1, 2, 3, 4, 0, 5};
    auto& thread = handlerThreads_[{o.is3D, o.hash}];
    if (!thread) {
        // Its registers are the object's values, also when no block starts
        // now (a later 5043/6043 start runs on this thread).
        thread = std::make_unique<xq::Thread>(machine, 16);
        for (std::size_t i = 0; i < 16; ++i) {
            thread->values[i] = i < o.values.size() ? o.values[i] : xq::Value{};
        }
    }
    for (int s : {state, 0}) {
        const std::string& name = o.handlers[kSlot[s]];
        if (!name.empty() && newest_) {
            for (std::size_t i = 0; i < 16; ++i) {
                thread->values[i] = i < o.values.size() ? o.values[i] : xq::Value{};
            }
            if (thread->start(*newest_, name)) {
                o.handlerState = s;
                if (traceHandlers && log) {
                    log("handler " + o.name + " state " + std::to_string(s) + " " + name);
                }
                return;
            }
        }
        if (s == 0) {
            break;
        }
    }
    o.handlerState = 0;
}

bool Game::startObjectBlock(adventure56::Object& o, const std::string& block,
                            std::span<const xq::Value> arguments) {
    // 0x2b8a04 (= 0x290f5c): a call from the handler thread's innermost
    // running block (its root when none runs), so a block started while
    // another waits runs first and the other goes on after it.
    auto& thread = handlerThreads_[{o.is3D, o.hash}];
    if (!thread) {
        thread = std::make_unique<xq::Thread>(machine, 16);
        for (std::size_t i = 0; i < 16; ++i) {
            thread->values[i] = i < o.values.size() ? o.values[i] : xq::Value{};
        }
    }
    xq::Thread& t = *thread;
    return t.program() ? t.callback(block, arguments)
                       : newest_ && t.start(*newest_, block, {}, arguments);
}

bool Game::callObjectBlock(adventure56::Object& o, const std::string& block,
                           std::span<const xq::Value> arguments) {
    o.switch14 = true;
    if (!startObjectBlock(o, block, arguments)) {
        return false;
    }
    xq::Thread& t = *handlerThreads_[{o.is3D, o.hash}];
    if (t.executing()) {
        return true;
    }
    const bool solid = o.is3D;
    const std::uint32_t hash = o.hash;
    t.run();
    if (adventure56::Object* now = scene.find(solid, hash)) {
        for (std::size_t i = 0; i < now->values.size() && i < 16; ++i) {
            now->values[i] = t.values[i];
        }
    }
    return true;
}

void Game::processTouch() {
    using adventure56::Object;
    // 0x37889c: with the loop's input switch set (2109) the touch event is
    // dropped: no object is pressed or clicked.
    const bool locked = objectTouchLocked;
    if (locked) {
        pressedObject_ = 0;
    }
    // 0x37aaa4: a new press picks the topmost touchable object under the pen.
    if (locked) {
    } else if (pressed()) {
        const Object* best = nullptr;
        int bestKind = 0;
        for (const auto& [hash, o] : scene.objects()) {
            if (o->screen != 1 || !o->touchable()) {
                continue;
            }
            // 0x37a728: the topmost object the pen touches (0x27233c).
            // Blocks without touch (5031, mode 0) still take a press on a
            // touch area of kind 2 or 3 (0x37acf0: states 1 and 2 by kind
            // alone; only kind 1 depends on the mode).
            if (const int kind = scene.hitTest(*o, float(input.x), float(input.y));
                kind > 0 && (o->handlerMode || kind == 2 || kind == 3)) {
                // Ties: later in the screen list (Object::listOrder).
                if (!best || o->priority > best->priority ||
                    (o->priority == best->priority && o->listOrder > best->listOrder)) {
                    best = o.get();
                    bestKind = kind;
                }
            }
        }
        if (best) {
            Object* o = scene.object(best->hash);
            const int kind = bestKind;
            int state = 0;
            if (kind == 2) {
                state = 1;
            } else if (kind == 3) {
                state = 2;
            } else if (o->handlerMode == 1) {
                state = 3;
            } else if (o->handlerMode == 2) {
                state = 4;
            } else {
                state = 1;
            }
            pressedObject_ = o->hash;
            startHandler(*o, state);
        }
    } else if (released() && pressedObject_) {
        if (Object* o = scene.object(pressedObject_)) {
            const bool inside =
                scene.hitTest(*o, float(previousInput.x), float(previousInput.y)) > 0;
            // 0x37ab98 (L6 0x1120cc), by the handler's state: 1 and 2 go
            // back to normal, 4 (select) clicks inside or goes normal; 0,
            // 3, 5 and 6 are left alone (a touch block, 3, runs to its end
            // and 0x263578 returns it to normal).
            if (o->handlerState == 1 || o->handlerState == 2) {
                startHandler(*o, 0);
            } else if (o->handlerState == 4) {
                startHandler(*o, inside ? 6 : 0);
            }
            if (o->content == 8) {
                penUp(*o);
            }
        }
        pressedObject_ = 0;
    }
    if (input.touching && pressedObject_) {
        if (Object* o = scene.object(pressedObject_)) {
            penTo(*o, float(input.x), float(input.y), pressed());
        }
    }
}

void Game::updateObjects() {
    using adventure56::Object;
    std::erase_if(handlerThreads_, [this](const auto& e) {
        return scene.find(e.first.first, e.first.second) == nullptr;
    });
    // The lists as they are now: objects made during the walk wait for
    // the next frame, deleted ones are skipped.
    std::vector<std::pair<bool, std::uint32_t>> order;
    for (const bool solid : {false, true}) {
        for (const Object* o : scene.updateOrder(solid)) {
            order.emplace_back(solid, o->hash);
        }
    }
    for (const auto& [solid, hash] : order) {
        Object* o = scene.find(solid, hash);
        if (!o || !o->updating()) {
            continue;
        }
        // The handler thread; its registers 0..15 are the object's values.
        if (const auto it = handlerThreads_.find({solid, hash}); it != handlerThreads_.end()) {
            xq::Thread& thread = *it->second;
            if (thread.running()) {
                thread.run();
                o = scene.find(solid, hash);
                if (o) {
                    for (std::size_t i = 0; i < o->values.size() && i < 16; ++i) {
                        o->values[i] = thread.values[i];
                    }
                }
            }
            // 0x263578: a finished touch (3) or click (6) block returns the
            // object to its normal state.
            if (o && !thread.running() && (o->handlerState == 3 || o->handlerState == 6)) {
                startHandler(*o, 0);
            }
        }
        if (o) {
            // The text component's frame (vtable +0x90, 0x170154), given the
            // scene's frame step (display frames per game frame, 0x378900).
            if (o->message && !o->message->empty()) {
                o->message->update(float(std::max(1, displayFramesPerFrame)));
            }
            scene.updateObject(*o);
        }
    }
}

const std::vector<std::uint8_t>& Game::code() {
    if (codeSegment_.empty()) {
        const Bytes c = pack.read(pack.root() / "code" / "code.bin");
        codeSegment_.assign(c.begin(), c.end());
    }
    return codeSegment_;
}

std::uint32_t Game::codeWord(std::uint32_t address) {
    const auto& c = code();
    const std::size_t at = address - kCodeBase;
    if (address < kCodeBase || at + 4 > c.size()) {
        return 0;
    }
    return std::uint32_t(c[at]) | std::uint32_t(c[at + 1]) << 8 | std::uint32_t(c[at + 2]) << 16 |
           std::uint32_t(c[at + 3]) << 24;
}

std::string Game::codeText(std::uint32_t address, std::size_t limit) {
    const auto& c = code();
    std::string s;
    for (std::size_t at = address - kCodeBase;
         address >= kCodeBase && at < c.size() && c[at] && s.size() < limit; ++at) {
        s += char(c[at]);
    }
    return s;
}

bool Game::setupRecogniser(int width, int height, int category, int limit) {
    // 0x1a7ea8: the database by category (file names at 0x412658: 0
    // alnum_u, 3 hira_u, 4 kata_u) and the class mask.
    recogniser_.reset();
    const char* file = category == 5 ? "hira_u.bin" : category == 6 ? "kata_u.bin" : "alnum_u.bin";
    static constexpr unsigned kMasks[7] = {0x1fff, 2, 4, 8, 0xc, 0x10, 0x20};
    const unsigned mask = category >= 1 && category <= 6 ? kMasks[category] : 0x1fff;
    try {
        code();
        const Bytes db = pack.read(pack.root() / "arc" / "a" / "res" / "hwr" / file);
        recogniser_ = std::make_unique<adventure56::hwr::Recognizer>(
            std::vector<std::uint8_t>(db.begin(), db.end()), codeSegment_, width, height, mask);
        recogniserLimit_ = limit;
    } catch (const std::exception&) {
        recogniser_.reset();
        return false;
    }
    return true;
}

void Game::penUp(adventure56::Object& o) {
    if (o.pen.mode != 16) {
        return;
    }
    auto& list = o.pen.strokes;
    const std::size_t points = list.size() / 2;
    std::vector<std::uint8_t> bytes = list;
    if (points != 0 && points < 0x3ff) {
        list[list.size() - 2] &= 0x7f;
        bytes = list;
        bytes.push_back(0xff);
        bytes.push_back(0xff);
    }
    o.pen.recognised.clear();
    if (!recogniser_) {
        return;
    }
    // 0x3a283c: the first result's character (UTF-16 to Shift-JIS by
    // 0x250e2c; digits and ASCII map to themselves); with a limit set, a
    // best score above it or no result gives "?". The canvas keeps the
    // first result only when there were candidates.
    const auto results = recogniser_->recognize(bytes);
    if (results.empty()) {
        return;
    }
    if (recogniserLimit_ != 0 && unsigned(recogniserLimit_) < results[0].score) {
        o.pen.recognised = "?";
        return;
    }
    const std::uint16_t code = results[0].code;
    if (code < 0x80) {
        o.pen.recognised = std::string(1, char(code));
    }
}

void Game::penTo(adventure56::Object& o, float x, float y, bool start) {
    // Hypothesis for the brush: discs of the pen width stamped along the
    // segment between the frames' pen positions, in the pen colour; the
    // drawn box grows by the stamped pixels.
    if (o.content != 8 || o.pen.mode == 0 || !o.picture) {
        return;
    }
    auto& canvas = *o.picture;
    const auto local = scene.toLocal(o, x, y);
    const float px = local[0] + o.pivot[0], py = local[1] + o.pivot[1];
    if (start || !o.pen.down) {
        o.pen.down = true;
        o.pen.lastX = px;
        o.pen.lastY = py;
    } else if (o.pen.mode == 16 && o.pen.strokes.size() / 2 < 0x3ff) {
        // 0x391a20: each continuing frame's point (not the press frame's),
        // the coordinates cut to unsigned bytes, x with bit 7 set.
        auto cut = [](float v) {
            return std::uint8_t(v <= 0 ? 0u : unsigned(v));
        };
        o.pen.strokes.push_back(std::uint8_t(cut(px) | 0x80));
        o.pen.strokes.push_back(cut(py));
    }
    const platform::Color colour{
        std::uint8_t(std::clamp(o.pen.colour[0], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(o.pen.colour[1], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(o.pen.colour[2], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(o.pen.colour[3], 0.0f, 1.0f) * 255.0f + 0.5f)};
    const float radius = std::max(0.5f, o.pen.width * 0.5f);
    const float dx = px - o.pen.lastX, dy = py - o.pen.lastY;
    const int steps = std::max(1, int(std::ceil(std::sqrt(dx * dx + dy * dy))));
    auto& box = o.drawnBox;
    for (int i = 0; i <= steps; ++i) {
        const float cx = o.pen.lastX + dx * float(i) / float(steps),
                    cy = o.pen.lastY + dy * float(i) / float(steps);
        const int x0 = std::max(0, int(std::floor(cx - radius))),
                  x1 = std::min(canvas.width - 1, int(std::ceil(cx + radius)));
        const int y0 = std::max(0, int(std::floor(cy - radius))),
                  y1 = std::min(canvas.height - 1, int(std::ceil(cy + radius)));
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                const float ex = float(xx) + 0.5f - cx, ey = float(yy) + 0.5f - cy;
                if (ex * ex + ey * ey > radius * radius) {
                    continue;
                }
                canvas.pixels[std::size_t(yy) * std::size_t(canvas.width) + std::size_t(xx)] =
                    colour;
                if (!box.any) {
                    box = {true, xx, yy, xx + 1, yy + 1};
                } else {
                    box.x0 = std::min(box.x0, xx);
                    box.y0 = std::min(box.y0, yy);
                    box.x1 = std::max(box.x1, xx + 1);
                    box.y1 = std::max(box.y1, yy + 1);
                }
            }
        }
    }
    o.pen.lastX = px;
    o.pen.lastY = py;
}

xq::Value Game::objectValue(const adventure56::Object& o, std::size_t i) const {
    if (const auto it = handlerThreads_.find({o.is3D, o.hash});
        it != handlerThreads_.end() && i < 16) {
        return it->second->values[i];
    }
    return i < o.values.size() ? o.values[i] : xq::Value{};
}

void Game::setObjectValue(adventure56::Object& o, std::size_t i, const xq::Value& v) {
    if (i < o.values.size()) {
        o.values[i] = v;
    }
    if (const auto it = handlerThreads_.find({o.is3D, o.hash});
        it != handlerThreads_.end() && i < 16) {
        it->second->values[i] = v;
    }
}

void Game::render(ctr3d::Target& top, ctr3d::Target& bottom) const {
    top.clear({0, 0, 0, 255});
    bottom.clear({0, 0, 0, 255});
    scene.render(top, bottom);
}

} // namespace picarat::adventure5

namespace picarat::adventure5 {

// Engine operations the script services and the special puzzle loops share.

adventure56::Object& Game::createObject(const std::string& name, int screen, int priority, int kind,
                                        std::span<const xq::Value> values, int level) {
    adventure56::Object& o = scene.create(name, screen, priority, kind, level);
    // 0x2d1d1c: up to sixteen values.
    o.values.assign(16, xq::Value{});
    for (std::size_t i = 0; i < values.size() && i < 16; ++i) {
        o.values[i] = values[i];
    }
    return o;
}

bool Game::setAnimationSet(adventure56::Object& o, std::uint32_t resource) {
    // 0x2cd9b0: the animation sets' list (a pop-up of the same name is
    // another list's); other kinds that carry a set (pop-ups) as before.
    adventure56::Resource* r = scene.resource(resource, adventure56::Resource::Kind::Animation);
    if (!r) {
        r = scene.resource(resource);
    }
    if (!r || !r->animation) {
        return false;
    }
    o.animation = r->animation;
    o.playing = -1;
    o.content = 9;
    return true;
}

bool Game::playAnimation(adventure56::Object& o, const xq::Value& v, std::uint32_t flags) {
    // 0x39b1ec
    if (!o.animation) {
        return false;
    }
    int index = -1;
    for (std::size_t i = 0; i < o.animation->animations.size(); ++i) {
        const auto& a = o.animation->animations[i];
        if ((v.isText() && a.name == v.text) || (!v.isText() && a.hash == v.toUint())) {
            index = int(i);
        }
    }
    if (index < 0) {
        return false;
    }
    // Flags (0x2aef34, kept at +0xa8): 4 restarts an animation already
    // playing, 8 plays it once and holds the last frame (puzzle 027's
    // slingshots and stone hits); 0x10 skips the follow-up animation (not
    // modelled).
    if (o.playing != index || (flags & 4) || o.finished) {
        o.frame = 0;
        o.passes = 0;
    }
    o.playing = index;
    o.looping = (flags & 8) == 0;
    o.finished = false;
    return true;
}

void Game::setHandlers(adventure56::Object& o, int mode, std::span<const std::string> names) {
    for (auto& h : o.handlers) {
        h.clear();
    }
    auto name = [&](std::size_t i) {
        return i < names.size() ? names[i] : std::string();
    };
    o.handlerMode = mode;
    o.handlers[0] = name(0);
    if (mode == 1) {
        o.handlers[3] = name(1);
        o.handlers[1] = name(2);
        o.handlers[2] = name(3);
    } else if (mode == 2) {
        o.handlers[4] = name(1);
        o.handlers[5] = name(2);
        o.handlers[1] = name(3);
        o.handlers[2] = name(4);
    } else {
        o.handlers[1] = name(1);
        o.handlers[2] = name(2);
    }
    startHandler(o, 0);
}

std::string Game::effectName(int number) const {
    // 0x2bf9fc / 0x2bf988: the player by number (0x350954: below 200 the
    // system player, set "SY_000"; from 200 the scene's, "SI_<set>" - 11023
    // 1 / 11000 set it) and "%s_%03d".
    char name[32];
    if (number < 200) {
        std::snprintf(name, sizeof name, "SY_000_%03d", number);
    } else {
        std::snprintf(name, sizeof name, "SI_%03d_%03d", effectSet, number);
    }
    return name;
}

bool Game::playEffect(int number) {
    const std::string name = effectName(number);
    const double samples = effectDuration(name);
    if (samples > 0) {
        activeEffects.push_back({number >= 200 ? 1 : 0, name, samples});
    }
    if (sound) {
        sound->playEffect(name);
    }
    return samples > 0;
}

bool Game::effectPlaying(int number) {
    const std::string name = effectName(number);
    for (const auto& e : activeEffects) {
        if (e.name == name && e.remaining > 0) {
            return true;
        }
    }
    return false;
}

void Game::stopEffect(int number, int frames) {
    const std::string name = effectName(number);
    const double samples =
        double(frames) * adventure56::SoundPlayer::kRate / adventure56::StreamPlayers::kFrameRate;
    for (auto& e : activeEffects) {
        if (e.name == name) {
            e.remaining = std::min(e.remaining, samples);
        }
    }
    if (sound) {
        sound->stopEffect(name, frames);
    }
}

bool Game::effectsSounding(int number) {
    // 0x33b570 (that number's player) / 0x33b7f8 (any, number < 0)
    const int player = number < 0 ? -1 : (number >= 200 ? 1 : 0);
    return std::any_of(activeEffects.begin(), activeEffects.end(),
                       [&](const auto& e) { return player < 0 || e.player == player; });
}

} // namespace picarat::adventure5

namespace picarat::adventure5 {

int Game::flagId(const std::string& name) const {
    const auto it = defs.flagIds.find(name);
    return it == defs.flagIds.end() ? -1 : it->second;
}

} // namespace picarat::adventure5

namespace picarat::adventure5 {

void Game::setEveryObject(bool touch, bool on, unsigned screen) {
    for (const bool solid : {false, true}) {
        for (const auto& [hash, o] : scene.objects(solid)) {
            if (screen < 5 && unsigned(o->screen) != screen) {
                continue;
            }
            (touch ? o->touchOn : o->visible) = on;
        }
    }
}

} // namespace picarat::adventure5
