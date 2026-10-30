#include "data/gui_layout.hpp"

#include "data/obfuscated_file.hpp"

#include <cstring>

namespace doc::data {
namespace {

constexpr int kGuiMagic = 0x00101c0f;

// Kinds the app makes (App::loadUserItem @0x183708, kind - 101 indexes its table).
constexpr int kResultsBounceFrame = 101;  // a GuiBounceFrame
constexpr int kPlusFrame = 105;
constexpr int kPosedMouse = 106;
constexpr int kLevelGoalButton = 107;     // a GuiButton
constexpr int kPopupGoalInfo = 108;
constexpr int kDots = 109;
constexpr int kLabelSM = 114;             // GuiLabelSM (1.1.1): a label with two more fields
constexpr int kScaleButton = 115;         // GuiScaleButton (1.1.1): a button with two scales
constexpr int kNewsWidget = 118;          // GuiFMWidget (1.1.1, Firemint's news): item fields only
constexpr int kSnapScroll = 110;          // GuiSnapScroll (1.1.1): item fields only
constexpr int kOriginInfo = 113;          // GuiOriginInfo (1.1.1): item fields only
constexpr int kHudElement = 116;          // GuiHudElement (1.1.1): item fields and a float
constexpr int kScrollFrameSM = 117;       // GuiScrollFrameSM (1.1.1): a GuiScrollFrame
constexpr int kProgressFrame = 119;       // GuiProgressFrame (1.1.1)
constexpr int kFlipItem = 120;            // GuiFlipItem (1.1.1): item fields and two flags
constexpr int kSpinningImage = 121;       // GuiSpinningImage::createItem (1.1.1): item fields only
constexpr int kBillboard = 104;           // GuiBillboard (the map's title bar)

class Reader {
public:
    explicit Reader(const Bytes& body) : body_(body) {}
    bool ok() const { return ok_; }
    std::size_t position() const { return at_; }

    int integer() {  // PFile::getInt: big-endian
        if (!need(4)) return 0;
        const std::uint32_t v = (std::uint32_t(body_[at_]) << 24) | (std::uint32_t(body_[at_ + 1]) << 16) |
                                (std::uint32_t(body_[at_ + 2]) << 8) | std::uint32_t(body_[at_ + 3]);
        at_ += 4;
        return int(v);
    }
    float real() {  // PFile::getFloat: as stored (little-endian)
        if (!need(4)) return 0.0f;
        const std::uint32_t v = std::uint32_t(body_[at_]) | (std::uint32_t(body_[at_ + 1]) << 8) |
                                (std::uint32_t(body_[at_ + 2]) << 16) | (std::uint32_t(body_[at_ + 3]) << 24);
        at_ += 4;
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    bool flag() {  // PFile::getBool: one byte
        if (!need(1)) return false;
        return body_[at_++] != 0;
    }
    std::string text() {  // PFile::getStr
        std::string s;
        while (ok_ && at_ < body_.size() && body_[at_] != 0) s.push_back(char(body_[at_++]));
        if (!need(1)) return s;
        ++at_;
        return s;
    }
    GuiVec vec() {
        GuiVec v;
        v.x = real();
        v.y = real();
        return v;
    }
    GuiColour colour() {  // PFile::getColour: four floats
        GuiColour c;
        c.r = real();
        c.g = real();
        c.b = real();
        c.a = real();
        return c;
    }

private:
    bool need(std::size_t n) {
        if (at_ + n > body_.size()) ok_ = false;
        return ok_;
    }
    const Bytes& body_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// GuiItem::GuiItem @0x1d9398.
void readItem(Reader& in, int version, GuiNode& node) {
    node.id = in.integer();
    node.name = in.text();
    node.image = in.text();
    node.position = in.vec();
    node.altPosition = in.vec();
    node.size = in.vec();
    node.altSize = node.size;
    if (version >= 6) node.altSize = in.vec();
    if (version >= 21) node.sizeToImage = in.flag();
    node.colour = in.colour();
    if (version >= 13) node.blend = in.integer();
    else if (version >= 10 && in.flag()) node.blend = 2;
    node.relative = in.flag();
    if (version > 6) {
        node.alignChildren = in.flag();
        if (version >= 9) in.integer();
        else if (version >= 8) in.flag();
        if (version >= 12) {
            in.flag();  // stretched picture (drawImageStretch) and its two margins
            in.real();
            in.real();
        }
    }
}

// GuiLabel::GuiLabel @0x1da484.
void readLabel(Reader& in, int version, GuiLabelFields& label) {
    in.integer();
    label.text = in.text();
    label.key = version >= 5 ? in.text() : label.text;
    label.fontClass = version >= 20 ? in.integer() : -1;
    label.font = in.integer();
    label.align = in.integer();
    label.wrap = in.flag();
    label.scale = in.real();
    if (version >= 11) {
        label.outline = in.integer();
        label.shadow = in.flag();
        label.outlineColour = in.colour();
        label.outlineWidth = in.real();
        label.shadowColour = in.colour();
        label.shadowOffset = in.vec();
    }
}

bool readChildren(Reader& in, int version, GuiNode& parent, int depth, std::string& error) {
    const int count = in.integer();
    if (!in.ok() || count < 0 || count > 4096 || depth > 32) {
        error = "corrupt gui layout";
        return false;
    }
    for (int i = 0; i < count; ++i) {
        GuiNode node;
        node.kind = in.integer();
        readItem(in, version, node);
        switch (node.kind) {
        case int(GuiKind::Item):
        case int(GuiKind::Rect):
        case int(GuiKind::Group):
        case kPlusFrame:
        case kPosedMouse:
        case kPopupGoalInfo:
        case kDots:
        case kNewsWidget:
        case kSnapScroll:   // GuiSnapScroll::GuiSnapScroll @0x1855bc
        case kOriginInfo:   // GuiOriginInfo @0x28409c
        case kSpinningImage:  // @0x18568c
            break;
        case kHudElement:  // GuiHudElement @0x28e818
            in.real();
            break;
        case kFlipItem:  // GuiFlipItem @0x29ad30
            in.flag();
            in.flag();
            break;
        case int(GuiKind::Label):
        case kLabelSM:
            node.label.emplace();
            readLabel(in, version, *node.label);
            if (node.kind == kLabelSM) {
                // GuiLabelSM::GuiLabelSM @0x29aa68: from version 26 a scale and a flag.
                if (version >= 26) {
                    in.real();
                    in.flag();
                }
                node.kind = int(GuiKind::Label);
            }
            break;
        case int(GuiKind::Button):  // GuiButton::GuiButton @0x1d7818: the pressed picture
        case int(GuiKind::Checkbox):
        case kLevelGoalButton:
            node.pressedImage = in.text();
            break;
        case kScaleButton:  // GuiScaleButton::GuiScaleButton @0x28a660: a button, then two scales
            node.pressedImage = in.text();
            in.real();
            in.real();
            node.kind = int(GuiKind::Button);
            break;
        case int(GuiKind::BounceFrame):
        case int(GuiKind::FadeFrame):
        case int(GuiKind::SlideFrame):
        case kResultsBounceFrame:
        case kProgressFrame:
            // GuiProgressFrame::GuiProgressFrame @0x20e11c: how long its animation runs.
            if (version >= 14) node.duration = in.real();
            if (version >= 18) in.integer();
            if (version >= 19) in.integer();
            if ((node.kind == int(GuiKind::BounceFrame) || node.kind == kResultsBounceFrame) && version >= 15) {
                node.bounceIn = in.flag();  // GuiBounceFrame @0x1d7200
                node.bounceOut = in.flag();
            }
            if (node.kind == int(GuiKind::SlideFrame) && version >= 15) {
                // GuiSlideFrame @0x20e818: its slide as two whole numbers, from version 22 as
                // two points.
                if (version > 21) {
                    in.vec();
                    in.vec();
                } else {
                    in.integer();
                    in.integer();
                }
            }
            break;
        case kBillboard: {  // GuiBillboard::GuiBillboard @0x20f6dc
            GuiBillboardFields& b = node.billboard.emplace();
            b.mode = in.integer();
            b.scale = in.real();
            b.spacing = in.real();
            b.font = in.integer();
            b.text = in.text();
            b.alpha = in.real();
            break;
        }
        case int(GuiKind::ScrollFrame):  // GuiScrollFrame::GuiScrollFrame @0x1dcb88
        case kScrollFrameSM:             // GuiScrollFrameSM @0x297574: the same fields
            node.scrollIndicator = in.text();
            if (version >= 25) {
                in.flag();
                in.real();
            }
            break;
        default:
            error = "gui item kind " + std::to_string(node.kind) + " not supported yet";
            return false;
        }
        if (!readChildren(in, version, node, depth + 1, error)) return false;
        parent.children.push_back(std::move(node));
    }
    return in.ok();
}

}  // namespace

const GuiNode* GuiNode::find(const std::string& itemName) const {
    if (name == itemName) return this;
    for (const GuiNode& child : children)
        if (const GuiNode* found = child.find(itemName)) return found;
    return nullptr;
}

std::optional<GuiLayout> loadGuiLayout(const GameFiles& files, const std::string& name, std::string& error) {
    auto file = files.readResource("guis/" + name + ".gui");
    if (!file) {
        error = "missing guis/" + name + ".gui";
        return std::nullopt;
    }
    auto body = decodeContainer(*file, files.keys().guiKey);
    if (!body) {
        error = "guis/" + name + ".gui does not decode";
        return std::nullopt;
    }
    Reader in(*body);
    GuiLayout layout;
    if (in.integer() != kGuiMagic) {
        error = "guis/" + name + ".gui is not a layout";
        return std::nullopt;
    }
    layout.version = in.integer();
    if (layout.version < 4) {
        error = "guis/" + name + ".gui is too old";
        return std::nullopt;
    }
    if (layout.version >= 20) in.integer();
    if (!readChildren(in, layout.version, layout.root, 0, error)) {
        error = "guis/" + name + ".gui: " + error;
        return std::nullopt;
    }
    return layout;
}

}  // namespace doc::data
