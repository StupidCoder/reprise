// Screen layouts the original builds its menus and popups from (res/guis/<name>.gui, read by
// GuiItem::loadLayout @0x1d9b28). A layout is a container file (PFile) with its own key; its
// body is a tree of items:
//   header   int magic 0x00101c0f, int file version, int (version >= 20) unused here
//   children int count, then per child: int kind, the item's fields, its own children
// Ints are big-endian (PFile::getInt @0x150cc8 byte-swaps), floats little-endian, strings
// NUL-terminated. Which fields exist depends on the file version (GuiItem::GuiItem @0x1d9398).
//
// Every item carries two placements, one for the phone screen and one for larger screens;
// the game picks the first on the 480x320 screen (GuiItem::render @0x1d85ec). Positions are
// in screen points with y up, relative to the parent when `relative` is set
// (GuiItem::updateScreenPos @0x1d8c10).
#pragma once

#include "data/game_files.hpp"

#include <optional>
#include <string>
#include <vector>

namespace doc::data {

struct GuiVec {
    float x = 0, y = 0;
};

struct GuiColour {
    float r = 1, g = 1, b = 1, a = 1;
};

// Item kinds (GuiItem::createChildren @0x1d9750). Kinds from 101 on are made by the app
// (App::loadUserItem @0x183708): 101 bounce frame, 105 plus frame, 106 posed mouse, 107 level
// goal button, 108 goal info popup, 109 dots.
enum class GuiKind : int {
    Item = 0,
    Label = 2,
    Button = 3,
    SliderA = 4,
    SliderB = 5,
    ScrollFrame = 6,
    BounceFrame = 7,
    FadeFrame = 8,
    Checkbox = 9,
    Rect = 10,
    SlideFrame = 11,
    ListBox = 12,
    Group = 13,
};

// GuiLabel's fields (GuiLabel::GuiLabel @0x1da484).
struct GuiLabelFields {
    std::string text;      // literal text
    std::string key;       // string name; the text when the string table knows it
    int fontClass = -1;    // shared style (ManagerFont::getClass), -1 none
    int font = 0;          // 0 FNT_TXT, 1 FNT_MEDIUM, 2 FNT_HEADING (App::loadFonts)
    int align = 0;         // renderStr flags: 1 left, 2 centre, 4 right; 0x10 top, 0x20 middle
    bool wrap = false;     // wrapped to the label's width (renderStrWrap)
    float scale = 1.0f;
    int outline = 0;       // FontOutline
    bool shadow = false;
    GuiColour outlineColour;
    float outlineWidth = 0.0f;
    GuiColour shadowColour;
    GuiVec shadowOffset;   // points, y up
};

// GuiBillboard's fields (kind 104): a caption that fades in when it changes.
struct GuiBillboardFields {
    int mode = 0;
    float scale = 1.0f;
    float spacing = 0.0f;
    int font = 0;
    std::string text;
    float alpha = 1.0f;
};

struct GuiNode {
    int kind = 0;
    int id = -1;             // hash of the name; scenes look items up by it (GuiItem::getItem)
    std::string name;
    std::string image;       // picture under res/, stretched over the item's box
    GuiVec position, size;   // phone placement (+0x08, +0x38)
    GuiVec altPosition, altSize;
    bool sizeToImage = false;
    GuiColour colour;        // tint and alpha (+0x4c)
    int blend = 1;           // setBlend mode (+0x5c)
    bool relative = true;    // positioned from the parent (+0x05)
    bool alignChildren = false;  // (+0x77)
    std::optional<GuiLabelFields> label;
    std::optional<GuiBillboardFields> billboard;
    std::string pressedImage;  // buttons
    std::string scrollIndicator;  // scroll frames
    float duration = 0.0f;        // animated frames: length of the animation
    bool bounceIn = false, bounceOut = false;  // bounce frames
    std::vector<GuiNode> children;

    const GuiNode* find(const std::string& itemName) const;
};

struct GuiLayout {
    int version = 0;
    GuiNode root;  // a nameless item holding the layout's top-level items
};

// Reads res/guis/<name>.gui. nullopt with a message if missing, corrupt or of an unknown
// version.
std::optional<GuiLayout> loadGuiLayout(const GameFiles& files, const std::string& name, std::string& error);

}  // namespace doc::data
