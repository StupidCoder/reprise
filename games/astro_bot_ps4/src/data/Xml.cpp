#include "data/Xml.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace beacon::data {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isSpace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

void appendDecoded(std::string& out, std::string_view raw) {
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out += raw[i];
            continue;
        }
        std::size_t end = raw.find(';', i);
        if (end == std::string_view::npos) {
            out += raw[i];
            continue;
        }
        std::string_view e = raw.substr(i + 1, end - i - 1);
        if (e == "amp") {
            out += '&';
        } else if (e == "lt") {
            out += '<';
        } else if (e == "gt") {
            out += '>';
        } else if (e == "quot") {
            out += '"';
        } else if (e == "apos") {
            out += '\'';
        } else if (!e.empty() && e[0] == '#') {
            long code = e.size() > 1 && (e[1] == 'x' || e[1] == 'X')
                            ? std::strtol(std::string(e.substr(2)).c_str(), nullptr, 16)
                            : std::strtol(std::string(e.substr(1)).c_str(), nullptr, 10);
            out += code > 0 && code < 128 ? char(code) : '?';
        } else {
            out.append(raw.substr(i, end - i + 1));
        }
        i = end;
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<XmlNode> document(std::string* error) {
        skipMisc();
        XmlNode root;
        if (!element(root)) {
            if (error) {
                *error = error_.empty() ? "no root element" : error_ + " at offset " + std::to_string(at_);
            }
            return std::nullopt;
        }
        return root;
    }

private:
    bool starts(std::string_view p) const { return s_.substr(at_, p.size()) == p; }
    bool skipPast(std::string_view p) {
        std::size_t e = s_.find(p, at_);
        if (e == std::string_view::npos) {
            at_ = s_.size();
            return false;
        }
        at_ = e + p.size();
        return true;
    }
    // Whitespace, comments, processing instructions and doctype between elements.
    void skipMisc() {
        for (;;) {
            while (at_ < s_.size() && isSpace(s_[at_])) {
                ++at_;
            }
            if (starts("<!--")) {
                skipPast("-->");
            } else if (starts("<?") || starts("<!")) {
                skipPast(">");
            } else {
                return;
            }
        }
    }
    std::string_view name() {
        std::size_t b = at_;
        while (at_ < s_.size() && !isSpace(s_[at_]) && s_[at_] != '>' && s_[at_] != '/' && s_[at_] != '=') {
            ++at_;
        }
        return s_.substr(b, at_ - b);
    }
    bool fail(const char* message) {
        error_ = message;
        return false;
    }

    bool element(XmlNode& node) {
        if (at_ >= s_.size() || s_[at_] != '<') {
            return fail("expected '<'");
        }
        ++at_;
        node.name = std::string(name());
        if (node.name.empty()) {
            return fail("empty element name");
        }
        // Attributes.
        for (;;) {
            while (at_ < s_.size() && isSpace(s_[at_])) {
                ++at_;
            }
            if (at_ >= s_.size()) {
                return fail("unterminated start tag");
            }
            if (starts("/>")) {
                at_ += 2;
                return true;
            }
            if (s_[at_] == '>') {
                ++at_;
                break;
            }
            std::string key(name());
            while (at_ < s_.size() && isSpace(s_[at_])) {
                ++at_;
            }
            if (at_ >= s_.size() || s_[at_] != '=') {
                return fail("expected '=' in attribute");
            }
            ++at_;
            while (at_ < s_.size() && isSpace(s_[at_])) {
                ++at_;
            }
            if (at_ >= s_.size() || (s_[at_] != '"' && s_[at_] != '\'')) {
                return fail("expected quoted attribute value");
            }
            char q = s_[at_++];
            std::size_t e = s_.find(q, at_);
            if (e == std::string_view::npos) {
                return fail("unterminated attribute value");
            }
            std::string value;
            appendDecoded(value, s_.substr(at_, e - at_));
            node.attributes.emplace_back(std::move(key), std::move(value));
            at_ = e + 1;
        }
        // Content.
        std::string text;
        for (;;) {
            std::size_t lt = s_.find('<', at_);
            if (lt == std::string_view::npos) {
                return fail("unterminated element");
            }
            appendDecoded(text, s_.substr(at_, lt - at_));
            at_ = lt;
            if (starts("</")) {
                at_ += 2;
                std::string_view close = name();
                if (close != node.name) {
                    return fail("mismatched end tag");
                }
                skipPast(">");
                node.text = std::string(trim(text));
                return true;
            }
            if (starts("<!--")) {
                skipPast("-->");
            } else if (starts("<![CDATA[")) {
                at_ += 9;
                std::size_t e = s_.find("]]>", at_);
                if (e == std::string_view::npos) {
                    return fail("unterminated CDATA");
                }
                text.append(s_.substr(at_, e - at_));
                at_ = e + 3;
            } else if (starts("<?")) {
                skipPast("?>");
            } else {
                node.children.emplace_back();
                if (!element(node.children.back())) {
                    return false;
                }
            }
        }
    }

    std::string_view s_;
    std::size_t at_ = 0;
    std::string error_;
};

}   // namespace

const XmlNode* XmlNode::child(std::string_view childName) const {
    for (const auto& c : children) {
        if (c.name == childName) {
            return &c;
        }
    }
    return nullptr;
}

std::optional<std::string> XmlNode::childText(std::string_view childName) const {
    const XmlNode* c = child(childName);
    if (!c) {
        return std::nullopt;
    }
    return c->text;
}

std::string XmlNode::childText(std::string_view childName, std::string_view fallback) const {
    const XmlNode* c = child(childName);
    return c ? c->text : std::string(fallback);
}

const std::string* XmlNode::attribute(std::string_view attributeName) const {
    for (const auto& [k, v] : attributes) {
        if (k == attributeName) {
            return &v;
        }
    }
    return nullptr;
}

std::optional<XmlNode> parseXml(std::string_view text, std::string* error) {
    if (text.size() >= 3 && std::uint8_t(text[0]) == 0xef && std::uint8_t(text[1]) == 0xbb &&
        std::uint8_t(text[2]) == 0xbf) {
        text.remove_prefix(3);   // UTF-8 byte order mark
    }
    return Parser(text).document(error);
}

bool parseFloatList(std::string_view text, float* out, std::size_t count) {
    std::string buf(text);
    const char* p = buf.c_str();
    for (std::size_t i = 0; i < count; ++i) {
        char* end = nullptr;
        float v = std::strtof(p, &end);
        if (end == p) {
            return false;
        }
        out[i] = v;
        p = end;
    }
    return true;
}

std::optional<bool> parseBool(std::string_view text) {
    text = trim(text);
    std::string t;
    for (char c : text) {
        t += char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (t == "true" || t == "1") {
        return true;
    }
    if (t == "false" || t == "0") {
        return false;
    }
    return std::nullopt;
}

}   // namespace data
