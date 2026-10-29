// The character profiles of the secrets (loop 5210, lt5CLoopScrtChara: creator
// 0x37ee08, constructor 0x387674, vtable 0x403080: reset 0x2f2988, commands
// 0x2f1e0c; the rest is the script loop's). The script scrt_chara shows a
// grid of characters and, in its 3D profiles, the chosen character's model;
// the loop answers its 2110 commands from res/scrt/lt5_scrt_chr.xs.
#include "story/a5_secrets.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace picarat::adventure5 {

using xq::Value;

namespace {

// The data file (0x2f29a0, parsers by command: 7000 0x158380, 7001
// 0x158304, 7002 0x1583f0, 7003 0x151fac, 7004 0x14f35c).
constexpr const char* kProfilesData = "res/scrt/lt5_scrt_chr";

class SecretProfiles : public ScriptLoop {
public:
    SecretProfiles(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "scrt_chara") {}

    bool command(int number, xq::Call& c, std::size_t first) override {
        const std::size_t n = c.count() > first ? c.count() - first : 0;
        auto integer = [&](std::size_t i) { return c.integer(first + i); };
        auto put = [&](std::size_t i, Value v) {
            if (Value* r = first + i < c.count() ? c.reg(first + i) : nullptr) {
                *r = std::move(v);
            }
        };
        switch (number) {
        case 0: // 0x2f29a0: the tables from the data file
            load();
            return result(c, 1);
        case 1: { // {page, -> columns, -> rows}
            const Page p = page(integer(0));
            put(1, Value::ofInt(p.columns));
            put(2, Value::ofInt(p.rows));
            return result(c, 1);
        }
        case 2: { // 0x2f220c {page, column, row}: the button's character into
                  // the first operand's register
            const int id = buttonAt(integer(0), integer(1), integer(2));
            if (id < 0) {
                return false;
            }
            put(0, Value::ofInt(id));
            return result(c, 1);
        }
        case 3: // 0x2f235c {character, object, blend frames}: a pose
            if (n < 3) {
                return false;
            }
            pose(integer(0), c.text(first + 1) ? *c.text(first + 1) : std::string());
            return result(c, 1);
        case 4: // the pose's character is forgotten (+0x1708 = -1)
            current_ = -1;
            return result(c, 1);
        case 5: // {character}: its turn (radians)
        case 6: { // {character}: its scale
            const Character* ch = n ? character(integer(0)) : nullptr;
            c.result = Value::ofFloat(ch ? (number == 5 ? ch->turn : ch->scale) : 0.0f);
            return true;
        }
        case 7: { // 0x2f20b8 {character, -> x, -> y, -> z}: its place
            if (n < 4) {
                return false;
            }
            const Character* ch = character(integer(0));
            const std::array<float, 3> at = ch ? ch->place : std::array<float, 3>{};
            for (std::size_t i = 0; i < 3; ++i) {
                put(1 + i, Value::ofFloat(at[i]));
            }
            c.result = Value::ofInt(1);
            return true;
        }
        case 8: // 0x2f2558 {kind, index, -> x, -> y, -> w, -> h}: a rectangle
            return rectangle(c, first, n);
        default:
            return ScriptLoop::command(number, c, first);
        }
    }

private:
    struct Page {
        int columns = 0, rows = 0;
    };
    // A character (0x58 bytes from +0x168, 60 at most): its id, scale (+0x170),
    // turn (+0x174, the data's degrees in radians), place (+0x178) and up to
    // ten poses (+0x184: three motion numbers, 6850's).
    struct Character {
        int id = -1;
        float scale = 1, turn = 0;
        std::array<float, 3> place{};
        std::vector<std::array<int, 3>> poses;
        int pose = 0; // +0x16e: the pose shown
    };

    static bool result(xq::Call& c, std::int32_t v) {
        c.result = Value::ofInt(v);
        return true;
    }

    int characterId(const std::string& name) const {
        const auto it = game_.defs.characterIds.find(name);
        return it == game_.defs.characterIds.end() ? -1 : it->second;
    }

    void load() {
        pages_.clear();
        buttons_.clear();
        characters_.clear();
        bool open = false; // 7002 opens a character, 7004 closes it
        for (const auto& d : readDataList(game_, kProfilesData)) {
            switch (d.opcode) {
            case 7000: { // {page from 1, columns, rows}
                const int p = d.number(0) - 1;
                if (p >= 0 && p < 16) {
                    if (int(pages_.size()) <= p) {
                        pages_.resize(std::size_t(p + 1));
                    }
                    pages_[std::size_t(p)] = {d.number(1), d.number(2)};
                }
                break;
            }
            case 7001: // the buttons' characters, in order
                for (std::size_t i = 0; i < d.operands.size() && buttons_.size() < 60; ++i) {
                    buttons_.push_back(characterId(d.text(i)));
                }
                break;
            case 7002: // {name, turn (degrees), scale, x, y, z}
                if (characters_.size() >= 60) {
                    break;
                }
                characters_.push_back({});
                characters_.back().id = characterId(d.text(0));
                characters_.back().turn = float(d.number(1)) * 0.0174532924f; // 0x15852c
                characters_.back().scale = d.real(2);
                characters_.back().place = {d.real(3), d.real(4), d.real(5)};
                open = true;
                break;
            case 7003: { // {number from 1, three motion numbers}: the next pose
                if (!open || characters_.empty()) {
                    break;
                }
                auto& poses = characters_.back().poses;
                if (d.number(0) > 9) {
                    open = false; // 0x151fac: a tenth pose closes the list
                }
                if (poses.size() < 10 && int(poses.size()) + 1 == d.number(0)) {
                    poses.push_back({d.number(1), d.number(2), d.number(3)});
                }
                break;
            }
            case 7004:
                open = false;
                break;
            default:
                break;
            }
        }
    }

    Page page(int p) const {
        return p >= 0 && p < int(pages_.size()) ? pages_[std::size_t(p)] : Page{};
    }

    int characterIndex(int id) const {
        for (std::size_t i = 0; i < characters_.size(); ++i) {
            if (characters_[i].id == id) {
                return int(i);
            }
        }
        return -1;
    }
    Character* character(int id) {
        const int i = characterIndex(id);
        return i < 0 ? nullptr : &characters_[std::size_t(i)];
    }

    int buttonAt(int p, int column, int row) const {
        // The pages before count in full; the button is row x columns +
        // column into its page. -1 outside the page or past the list.
        const Page here = page(p);
        if (column < 0 || column >= here.columns || row < 0 || row >= here.rows) {
            return -1;
        }
        int index = 0;
        for (int q = 0; q < p; ++q) {
            index += page(q).columns * page(q).rows;
        }
        index += row * here.columns + column;
        if (index > int(buttons_.size())) {
            return -1;
        }
        return index < int(buttons_.size()) ? buttons_[std::size_t(index)] : 0;
    }

    void pose(int id, const std::string& objectName) {
        // 0x2f235c: the same character again takes another of its poses at
        // random (not the one shown, nor one starting with the same body
        // motion); a new character starts with its first pose and the last
        // one's choice is reset. The poses play on the object as 6850 with
        // its lip flag on (0x2c5604). *Not modelled*: the blend frames.
        const int i = characterIndex(id);
        if (i < 0) {
            return;
        }
        Character& ch = characters_[std::size_t(i)];
        const int count = int(ch.poses.size());
        int choice = 0;
        if (current_ == i) {
            if (count < 2) {
                return;
            }
            do {
                choice = int(game_.machine.random(std::uint32_t(count)));
            } while (choice == ch.pose ||
                     ch.poses[std::size_t(choice)][0] == ch.poses[std::size_t(ch.pose)][0]);
        } else if (current_ >= 0 && current_ < int(characters_.size())) {
            characters_[std::size_t(current_)].pose = 0;
        }
        ch.pose = choice;
        adventure56::Object* o = game_.scene.object3D(xq::hash(objectName));
        if (!o || count == 0) {
            return;
        }
        game_.setCharacterMotion(*o, ch.poses[std::size_t(choice)]);
        o->lipSync = true;
        current_ = i;
    }

    bool rectangle(xq::Call& c, std::size_t first, std::size_t n) {
        // Tables in the code: kinds 0..6 at 0x3ecd4e + 16 x kind, kind 9 at
        // 0x3ecdbe; two entries of four 16-bit values each.
        if (n < 6) {
            return false;
        }
        const int kind = c.integer(first), index = c.integer(first + 1);
        if (index < 0 || index > 1 || !((kind >= 0 && kind <= 6) || kind == 9)) {
            return false;
        }
        const std::uint32_t table = 0x3ecd4e + 16u * std::uint32_t(kind == 9 ? 7 : kind);
        const std::uint32_t at = table + 8u * std::uint32_t(index);
        const auto& code = game_.code();
        for (std::uint32_t k = 0; k < 4; ++k) {
            const std::uint32_t a = at + 2 * k - 0x100000;
            const int v = a + 1 < code.size() ? code[a] | code[a + 1] << 8 : 0;
            if (Value* r = c.reg(first + 2 + k)) {
                *r = Value::ofInt(v);
            }
        }
        c.result = Value::ofInt(1);
        return true;
    }

    std::vector<Page> pages_;
    std::vector<int> buttons_;           // +0x1618: the buttons' characters
    std::vector<Character> characters_;  // +0x168
    int current_ = -1;                   // +0x1708: the posed character's index
};

} // namespace

std::unique_ptr<Loop> makeSecretProfilesLoop(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<SecretProfiles>(story, id, std::move(values));
}

} // namespace picarat::adventure5
