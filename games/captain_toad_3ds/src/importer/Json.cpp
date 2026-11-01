// See Json.hpp.
#include "Json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace captain_toad::json {

void Value::need(Type t) const {
    if (type_ != t) {
        throw std::runtime_error("JSON value has an unexpected type");
    }
}

double Value::num() const {
    if (type_ == Type::Int || type_ == Type::Bool) {
        return double(int_);
    }
    need(Type::Double);
    return double_;
}

int64_t Value::integer() const {
    if (type_ == Type::Int || type_ == Type::Bool) {
        return int_;
    }
    throw std::runtime_error("JSON value is not an integer");
}

bool Value::boolean() const {
    need(Type::Bool);
    return int_ != 0;
}
const std::string& Value::str() const {
    need(Type::String);
    return string_;
}

bool Value::truthy() const {
    switch (type_) {
    case Type::Null:
        return false;
    case Type::Bool:
    case Type::Int:
        return int_ != 0;
    case Type::Double:
        return double_ != 0;
    case Type::String:
        return !string_.empty();
    case Type::Array:
        return !array_.empty();
    case Type::Object:
        return !object_.empty();
    }
    return false;
}

Value& Value::push(Value v) {
    need(Type::Array);
    array_.push_back(std::move(v));
    return array_.back();
}
size_t Value::size() const {
    if (type_ == Type::Object) {
        return object_.size();
    }
    need(Type::Array);
    return array_.size();
}
Value& Value::operator[](size_t i) {
    need(Type::Array);
    return array_.at(i);
}
const Value& Value::operator[](size_t i) const {
    need(Type::Array);
    return array_.at(i);
}
std::vector<Value>& Value::items() {
    need(Type::Array);
    return array_;
}
const std::vector<Value>& Value::items() const {
    need(Type::Array);
    return array_;
}

Value& Value::operator[](const std::string& key) {
    if (type_ == Type::Null) {
        type_ = Type::Object;
    }
    need(Type::Object);
    for (auto& m : object_) {
        if (m.first == key) {
            return m.second;
        }
    }
    object_.emplace_back(key, Value());
    return object_.back().second;
}

const Value& Value::at(const std::string& key) const {
    if (const Value* v = find(key)) {
        return *v;
    }
    throw std::runtime_error("JSON object lacks key " + key);
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& m : object_) {
        if (m.first == key) {
            return &m.second;
        }
    }
    return nullptr;
}

Value* Value::find(const std::string& key) {
    return const_cast<Value*>(static_cast<const Value*>(this)->find(key));
}

void Value::erase(const std::string& key) {
    need(Type::Object);
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return;
        }
    }
}

std::vector<Value::Member>& Value::members() {
    need(Type::Object);
    return object_;
}
const std::vector<Value::Member>& Value::members() const {
    need(Type::Object);
    return object_;
}

std::string formatFloat(double v) {
    if (std::isnan(v)) {
        return "nan";
    }
    if (std::isinf(v)) {
        return v < 0 ? "-inf" : "inf";
    }
    if (v == 0) {
        return std::signbit(v) ? "-0.0" : "0.0";
    }
    // Shortest round-trip digits in scientific form: [-]d[.ddd]e[+-]xx.
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    std::string sign;
    if (s[0] == '-') {
        sign = "-";
        s.erase(0, 1);
    }
    const size_t e = s.find('e');
    std::string digits = s.substr(0, e);
    const int exponent = std::stoi(s.substr(e + 1));
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int point = exponent + 1;   // decimal point position relative to the digits
    std::string out;
    if (point > -4 && point <= 16) {
        if (point <= 0) {
            out = "0." + std::string(size_t(-point), '0') + digits;
        } else if (size_t(point) >= digits.size()) {
            out = digits + std::string(size_t(point) - digits.size(), '0') + ".0";
        } else {
            out = digits.substr(0, size_t(point)) + "." + digits.substr(size_t(point));
        }
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1) {
            out += "." + digits.substr(1);
        }
        char ex[16];
        std::snprintf(ex, sizeof ex, "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        out += ex;
    }
    return sign + out;
}

std::string formatNumber(const Value& v) {
    if (v.type() == Value::Type::Int) {
        return std::to_string(v.integer());
    }
    return formatFloat(v.num());
}

namespace {

void quote(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}

void write(std::string& out, const Value& v, int indent, int depth) {
    auto newline = [&](int d) {
        if (indent < 0) {
            return;
        }
        out += '\n';
        out.append(size_t(indent * d), ' ');
    };
    switch (v.type()) {
    case Value::Type::Null:
        out += "null";
        break;
    case Value::Type::Bool:
        out += v.boolean() ? "true" : "false";
        break;
    case Value::Type::Int:
        out += std::to_string(v.integer());
        break;
    case Value::Type::Double: {
        const double d = v.num();
        out += std::isfinite(d) ? formatFloat(d) : (std::isnan(d) ? "NaN" : (d < 0 ? "-Infinity" : "Infinity"));
        break;
    }
    case Value::Type::String:
        quote(out, v.str());
        break;
    case Value::Type::Array: {
        if (v.items().empty()) {
            out += "[]";
            break;
        }
        out += '[';
        bool first = true;
        for (const auto& x : v.items()) {
            if (!first) {
                out += indent < 0 ? ", " : ",";
            }
            first = false;
            newline(depth + 1);
            write(out, x, indent, depth + 1);
        }
        newline(depth);
        out += ']';
        break;
    }
    case Value::Type::Object: {
        if (v.members().empty()) {
            out += "{}";
            break;
        }
        out += '{';
        bool first = true;
        for (const auto& m : v.members()) {
            if (!first) {
                out += indent < 0 ? ", " : ",";
            }
            first = false;
            newline(depth + 1);
            quote(out, m.first);
            out += ": ";
            write(out, m.second, indent, depth + 1);
        }
        newline(depth);
        out += '}';
        break;
    }
    }
}

}   // namespace

std::string dump(const Value& v, int indent) {
    std::string out;
    write(out, v, indent, 0);
    return out;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Value document() {
        Value v = value();
        space();
        if (p_ != s_.size()) {
            fail("trailing characters");
        }
        return v;
    }

private:
    std::string_view s_;
    size_t p_ = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("JSON parse error at byte ") + std::to_string(p_) + ": " + what);
    }

    void space() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\n' || s_[p_] == '\r' || s_[p_] == '\t')) {
            ++p_;
        }
    }

    bool literal(std::string_view word) {
        if (s_.substr(p_, word.size()) == word) {
            p_ += word.size();
            return true;
        }
        return false;
    }

    Value value() {
        space();
        if (p_ >= s_.size()) {
            fail("unexpected end");
        }
        const char c = s_[p_];
        if (c == '{') {
            return object();
        }
        if (c == '[') {
            return array();
        }
        if (c == '"') {
            return Value(string());
        }
        if (literal("true")) {
            return Value(true);
        }
        if (literal("false")) {
            return Value(false);
        }
        if (literal("null")) {
            return Value(nullptr);
        }
        if (literal("NaN")) {
            return Value(std::nan(""));
        }
        if (literal("Infinity")) {
            return Value(HUGE_VAL);
        }
        if (literal("-Infinity")) {
            return Value(-HUGE_VAL);
        }
        return number();
    }

    Value object() {
        ++p_;
        Value v = Value::object();
        space();
        if (p_ < s_.size() && s_[p_] == '}') {
            ++p_;
            return v;
        }
        while (true) {
            space();
            if (p_ >= s_.size() || s_[p_] != '"') {
                fail("expected a key");
            }
            std::string key = string();
            space();
            if (p_ >= s_.size() || s_[p_] != ':') {
                fail("expected ':'");
            }
            ++p_;
            v[key] = value();
            space();
            if (p_ < s_.size() && s_[p_] == ',') {
                ++p_;
                continue;
            }
            if (p_ < s_.size() && s_[p_] == '}') {
                ++p_;
                return v;
            }
            fail("expected ',' or '}'");
        }
    }

    Value array() {
        ++p_;
        Value v = Value::array();
        space();
        if (p_ < s_.size() && s_[p_] == ']') {
            ++p_;
            return v;
        }
        while (true) {
            v.push(value());
            space();
            if (p_ < s_.size() && s_[p_] == ',') {
                ++p_;
                continue;
            }
            if (p_ < s_.size() && s_[p_] == ']') {
                ++p_;
                return v;
            }
            fail("expected ',' or ']'");
        }
    }

    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        } else {
            out += char(0xf0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3f));
            out += char(0x80 | ((c >> 6) & 0x3f));
            out += char(0x80 | (c & 0x3f));
        }
    }

    uint32_t hex4() {
        if (p_ + 4 > s_.size()) {
            fail("short \\u escape");
        }
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= uint32_t(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= uint32_t(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= uint32_t(c - 'A' + 10);
            } else {
                fail("bad \\u escape");
            }
        }
        return v;
    }

    std::string string() {
        ++p_;
        std::string out;
        while (true) {
            if (p_ >= s_.size()) {
                fail("unterminated string");
            }
            const char c = s_[p_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= s_.size()) {
                fail("unterminated escape");
            }
            const char e = s_[p_++];
            switch (e) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                uint32_t c1 = hex4();
                if (c1 >= 0xd800 && c1 < 0xdc00 && s_.substr(p_, 2) == "\\u") {
                    p_ += 2;
                    const uint32_t c2 = hex4();
                    c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                }
                utf8(out, c1);
                break;
            }
            default:
                fail("bad escape");
            }
        }
    }

    Value number() {
        const size_t start = p_;
        bool integral = true;
        if (p_ < s_.size() && (s_[p_] == '-' || s_[p_] == '+')) {
            ++p_;
        }
        while (p_ < s_.size()) {
            const char c = s_[p_];
            if (c >= '0' && c <= '9') {
                ++p_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                integral = false;
                ++p_;
            } else {
                break;
            }
        }
        if (p_ == start) {
            fail("unexpected character");
        }
        const std::string text(s_.substr(start, p_ - start));
        if (integral) {
            int64_t v = 0;
            auto r = std::from_chars(text.data(), text.data() + text.size(), v);
            if (r.ec == std::errc() && r.ptr == text.data() + text.size()) {
                return Value((long long)v);
            }
        }
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size()) {
            fail("bad number");
        }
        return Value(d);
    }
};

}   // namespace

Value parse(std::string_view text) {
    return Parser(text).document();
}

}   // namespace captain_toad::json
