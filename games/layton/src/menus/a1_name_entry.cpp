// The first adventure's name entry. See a1_name_entry.hpp.
#include "menus/a1_name_entry.hpp"
#include "graphics/text.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include <algorithm>

namespace picarat::adventure1 {

namespace {
constexpr int kMaxLength = 10;

struct NameEntry {
    Story& story;
    SceneKit& kit;
    // The name (0203b144 / 0203b164): characters and the cursor (0..9).
    std::string name = std::string(kMaxLength, '\0');
    int cursor = 0;
    // +0x248: the key set (0 small, 1 capitals, 2 symbols, 3 accented).
    int set = 0;
    std::string keys[4]; // otext/keys: komoji, oomoji, kigou, tokushu
    Button ok, erase, space, normal, special, shift, symbols;
    Button keyButtons[52];
    SceneKit::SpriteObject cursorSprite, badName;
    std::string reserved[2];

    explicit NameEntry(Story& s) : story(s), kit(s.kit) {
        // 0203a994.
        story.marker("name-ctor");
        const auto start = kit.frames;
        story.present();
        // BG priorities (0203a994 writes BGxCNT): BG2 0, 3D 1, text 2, BG3 3.
        kit.lowerPriority = std::array<int, 4>{1, 2, 0, 3};
        kit.lowerBackground = "name_bg1";
        kit.lowerOverlay.clear();
        kit.upperBackground =
            kit.localized(story.state.scene == 0x28 ? "?/name_sub3" : "?/name_sub");
        story.present();
        // 0203b254: the key sets (45 characters, the accented set 52 with its
        // escapes expanded).
        const auto file = [&](const char* n) {
            const Bytes raw = readFile(story.pack / "otext/keys" / n);
            return std::string(raw.begin(), raw.end());
        };
        keys[0] = file("komoji.txt").substr(0, 45);
        keys[1] = file("oomoji.txt").substr(0, 45);
        keys[2] = file("kigou.txt").substr(0, 45);
        keys[3] = kit.text.expand(file("tokushu.txt")).substr(0, 52);
        cursorSprite = kit.load("name_btn.spr", 0, 0, 1);
        auto button = [&](Button& b, int x, int y, const char* sprite, const char* animation,
                          int sound) {
            b.setSprite(kit, x, y, sprite, animation);
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 1;
            b.sound = sound;
        };
        button(ok, 0x60, 0xa1, "?/name_ok.spr", "gfx", 0xb);
        for (auto& key : keyButtons) {
            key.sound = 0x29;
        }
        layoutKeys();
        button(erase, 0xc2, 0x81, "name_btn.spr", "erase", 10);
        shift.setRect(5, 0x57, 0x1c, 0x12);
        shift.sound = 3;
        symbols.setRect(5, 0x6a, 0x25, 0x12);
        symbols.sound = 3;
        button(normal, 3, 0x81, "name_btn.spr", "normal", 3);
        button(special, 0x22, 0x81, "name_btn.spr", "tokushu", 3);
        button(space, 0x44, 0x81, "name_btn.spr", "space", 0x29);
        badName = kit.load("?/bad_name.spr", 0, 0x3b, 1);
        badName.x = (0x100 - kit.frameWidth(badName)) >> 1;
        badName.y = 0x3b;
        selectSet(0);
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
        // Measured (new game, ROM frames 855..993): 138 frames of loading.
        story.loadingPause(start, 138);
        // The two names refused (0203b02c compares with literals at
        // 0203b100 / 0203b104; read from the program).
        reserved[0] = story.programString(0x0203b100);
        reserved[1] = story.programString(0x0203b104);
    }

    // 0203b1c4: the key set and its picture.
    void selectSet(int which) {
        set = which;
        static const char* const pictures[] = {"name_bg1", "name_bg2", "name_bg3", "name_bg4"};
        kit.lowerBackground = pictures[which];
    }

    // 0203b780: 45 keys of 19x19 in four rows (12, 12, 11, 10 keys).
    void layoutKeys() {
        int x = 0xf, y = 0x31;
        for (int i = 0; i < 45; ++i) {
            keyButtons[i].setRect(x, y, 0x13, 0x13);
            x += 0x13;
            if (i == 0xb) {
                x = 0x18;
                y = 0x44;
            } else if (i == 0x17) {
                x = 0x22;
                y = 0x57;
            } else if (i == 0x22) {
                x = 0x2b;
                y = 0x6a;
            }
        }
    }

    // 0203b800: the accented set, 52 keys in four rows of 13.
    void layoutSpecialKeys() {
        int i = 0;
        for (int row = 0, y = 0x31; row < 4; ++row, y += 0x13) {
            for (int column = 0, x = 5; column < 13; ++column, x += 0x13) {
                keyButtons[i++].setRect(x, y, 0x13, 0x13);
            }
        }
    }

    // 0203b144 / 0203b164.
    void add(char c) {
        name[std::size_t(cursor)] = c;
        if (cursor < kMaxLength - 1) {
            ++cursor;
        }
    }

    void removeLast() {
        if (name[std::size_t(cursor)] == '\0') {
            if (cursor > 0) {
                --cursor;
            }
            name[std::size_t(cursor)] = '\0';
            return;
        }
        name[std::size_t(cursor)] = '\0';
        if (cursor > 0 && cursor < kMaxLength - 1) {
            --cursor;
        }
    }

    std::string text() const { return std::string(name.c_str()); }

    // 0203b6b4: the name (0203b388: cleared, then each character centred in
    // a 9-pixel cell every 16 pixels) and the cursor (0203b3f4).
    void drawName() {
        const Font& font = kit.assets.font("fontevent.NFTR");
        Surface& layer = kit.textLayer(SceneKit::Lower);
        for (int y = 0x13; y < 0x13 + 0xc; ++y) {
            for (int x = 0x34; x < 0x34 + 0xc0; ++x) {
                layer.pixels[std::size_t(y) * 256 + std::size_t(x)] = 0;
            }
        }
        for (int i = 0, x = 0x34; i < kMaxLength; ++i, x += 0x10) {
            const auto c = std::uint8_t(name[std::size_t(i)]);
            if (c != 0 && c != ' ') {
                textGlyph(layer, font, c, x + (9 - textAdvance(font, c)) / 2, 0x13,
                          textColour(0xff));
            }
        }
        cursorSprite.x = cursor * 0x10 + 0x34;
        cursorSprite.y = 0x1d;
        kit.select(cursorSprite, 1u);
        kit.draw(cursorSprite, 31);
    }

    void updateButtons() {
        ok.update(kit, story.touch);
        erase.update(kit, story.touch);
        space.update(kit, story.touch);
        normal.update(kit, story.touch);
        special.update(kit, story.touch);
        if (set != 3) {
            shift.update(kit, story.touch);
            symbols.update(kit, story.touch);
        }
    }

    // 0203b434: buttons and keys; a key held shows a red square (0200f600,
    // colour 0x001f, alpha 15), a key clicked adds its character.
    void input() {
        updateButtons();
        if (shift.clicked(kit, story.touch)) {
            selectSet(set == 1 ? 0 : 1);
            layoutKeys();
        }
        if (symbols.clicked(kit, story.touch)) {
            selectSet(2);
            layoutKeys();
        }
        if (normal.clicked(kit, story.touch)) {
            selectSet(0);
            layoutKeys();
        }
        if (special.clicked(kit, story.touch)) {
            selectSet(3);
            layoutSpecialKeys();
        }
        if (erase.clicked(kit, story.touch)) {
            removeLast();
        }
        if (space.clicked(kit, story.touch)) {
            add(' ');
        }
        const int count = set == 3 ? 52 : 45;
        for (int i = 0; i < count; ++i) {
            Button& key = keyButtons[i];
            key.update(kit, story.touch);
            if (key.held(story.touch)) {
                kit.rectangle(key.x, key.y, 0x13, 0x13, 0x001f, 15);
                return;
            }
            if (key.clicked(kit, story.touch)) {
                add(keys[set][std::size_t(i)]);
                if (set == 2) {
                    selectSet(0); // a symbol returns to the small letters
                }
                return;
            }
        }
    }

    // Native addition (as in the other two games): the host's keyboard.
    // A character on any of the four key sets is typed as by its key,
    // Backspace erases as "erase" does, Return accepts as "OK" does.
    // Returns true when the name was accepted.
    bool keyboard() {
        const NameInput key = kit.hostInput().name;
        switch (key.action) {
        case NameInput::Action::Insert: {
            const char c = char(key.character);
            const bool known = c == ' ' || std::any_of(std::begin(keys), std::end(keys),
                                                       [&](const std::string& k) {
                                                           return k.find(c) != std::string::npos;
                                                       });
            if (known) {
                add(c);
                if (story.sound) {
                    story.sound->effect(0x29);
                }
            }
            return false;
        }
        case NameInput::Action::Erase:
            removeLast();
            if (story.sound) {
                story.sound->effect(10);
            }
            return false;
        case NameInput::Action::Confirm:
            if (text().empty()) {
                return false;
            }
            if (story.sound) {
                story.sound->effect(0xb);
            }
            return !refused();
        default:
            return false;
        }
    }

    // 0203b02c: a reserved name shows the message until the stylus is
    // lifted, then the name is cleared.
    bool refused() {
        const auto same = [](const std::string& a, const std::string& b) {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(std::uint8_t(x)) == std::tolower(std::uint8_t(y));
                   });
        };
        if (!same(text(), reserved[0]) && !same(text(), reserved[1])) {
            return false;
        }
        do {
            story.poll();
            drawName();
            updateButtons();
            kit.draw(badName, 31);
            story.present();
        } while (!story.touch.released && kit.hostInput().name.action == NameInput::Action::None);
        name.assign(kMaxLength, '\0');
        cursor = 0;
        return true;
    }

    std::string run() {
        const SceneKind previousScene = kit.scene();
        // 0203b6cc.
        story.marker("name-run");
        story.frame([&] {
            input();
            drawName();
        });
        fadeIn(kit, Screens::Both);
        kit.setScene(SceneKind::Name); // from now on the host sends typed keys
        for (;;) {
            story.frame([&] {
                input();
                drawName();
            });
            if (!text().empty() && ok.clicked(kit, story.touch) && !refused()) {
                break;
            }
            if (keyboard()) {
                break;
            }
        }
        kit.setScene(previousScene);
        // 0203ae68: fade out; the name goes to the state.
        fadeOut(kit, Screens::Both);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
        return text();
    }
};
} // namespace

std::string nameEntry(Story& story) {
    NameEntry entry(story);
    std::string name = entry.run();
    if (story.state.scene == 0x28) {
        // 0201b71c: the robot dog's name (+0x626, 20 bytes).
        auto& dog = story.state.dogName;
        dog.fill(0);
        std::copy_n(name.begin(), std::min(name.size(), dog.size() - 1), dog.begin());
    } else if (story.state.scene == 0) {
        story.state.playerName = name; // 0201b6ec: state +0x738
    }
    return name;
}

} // namespace picarat::adventure1
