// =============================================================================================
// Package.hpp - reading the imported scene package
// =============================================================================================
//
// The importer writes the opening's assets as a folder of JSON documents and binary buffers
// (see importer/SceneExport.cpp). This header gives the runtime read access to them:
//   * readBytes / readJson load files of the package;
//   * JsonView navigates a parsed document without copying it.
//
// JsonView deliberately behaves like the Foundation JSON objects the earlier macOS viewer used,
// so the reconstruction reads exactly the same values on every platform:
//   * a missing key or index-less lookup gives an empty view, and every number read from an
//     empty view is 0 (false, "" for strings is an error);
//   * JSON null is "present" (it tests true) but reports isNull();
//   * numbers convert like NSNumber: integers by truncation, doubles to float by rounding, and
//     strings like NSString's intValue / floatValue / boolValue.
// =============================================================================================
#pragma once
#include "importer/Json.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace captain_toad::data {

using Bytes = std::vector<uint8_t>;
using captain_toad::json::Value;

inline Bytes readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}

inline Value readJson(const std::filesystem::path& path) {
    const Bytes bytes = readBytes(path);
    return captain_toad::json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// A read-only view of a JSON value (or of nothing, for a missing key).
class JsonView {
public:
    JsonView() = default;
    JsonView(const Value& v) : v_(&v) {}

    // True when the value exists (JSON null included), like a non-nil Foundation object.
    explicit operator bool() const { return v_ != nullptr; }

    bool isNull() const { return v_ && v_->isNull(); }
    bool isArray() const { return v_ && v_->isArray(); }
    bool isObject() const { return v_ && v_->isObject(); }
    bool isString() const { return v_ && v_->isString(); }

    // Member lookup; empty view when missing or when this is not an object.
    JsonView operator[](std::string_view key) const {
        if (!v_ || !v_->isObject()) {
            return {};
        }
        if (const Value* m = v_->find(std::string(key))) {
            return JsonView(*m);
        }
        return {};
    }
    JsonView operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    JsonView operator[](const std::string& key) const { return (*this)[std::string_view(key)]; }

    // Element lookup (arrays only; out of range throws like NSArray).
    JsonView operator[](size_t i) const {
        if (!v_) {
            return {};
        }
        if (!v_->isArray()) {
            throw std::runtime_error("JSON value is not an array");
        }
        return JsonView(v_->items().at(i));
    }
    JsonView operator[](int i) const { return (*this)[size_t(i)]; }
    JsonView operator[](unsigned i) const { return (*this)[size_t(i)]; }

    // Element count of an array or object (0 when empty).
    size_t count() const {
        if (!v_ || !(v_->isArray() || v_->isObject())) {
            return 0;
        }
        return v_->size();
    }

    // Array elements (none when empty or not an array).
    std::vector<JsonView> items() const {
        std::vector<JsonView> out;
        if (v_ && v_->isArray()) {
            for (const Value& x : v_->items()) {
                out.push_back(JsonView(x));
            }
        }
        return out;
    }

    // Object keys in document order (none when empty or not an object).
    std::vector<std::string> keys() const {
        std::vector<std::string> out;
        if (v_ && v_->isObject()) {
            for (const auto& m : v_->members()) {
                out.push_back(m.first);
            }
        }
        return out;
    }

    // Numbers, NSNumber / NSString style (see the file header).
    double d() const {
        if (!v_) {
            return 0;
        }
        switch (v_->type()) {
        case Value::Type::Int:
        case Value::Type::Bool:
            return double(raw());
        case Value::Type::Double:
            return v_->num();
        case Value::Type::String:
            return std::strtod(skipSpace(v_->str()).c_str(), nullptr);
        default:
            throw std::runtime_error("JSON value is not a number");
        }
    }
    float f() const {
        if (!v_) {
            return 0;
        }
        switch (v_->type()) {
        case Value::Type::Int:
        case Value::Type::Bool:
            return float(raw());
        case Value::Type::Double:
            return float(v_->num());
        case Value::Type::String:
            return std::strtof(skipSpace(v_->str()).c_str(), nullptr);
        default:
            throw std::runtime_error("JSON value is not a number");
        }
    }
    int i() const {
        if (!v_) {
            return 0;
        }
        switch (v_->type()) {
        case Value::Type::Int:
        case Value::Type::Bool:
            return int(raw());
        case Value::Type::Double:
            return int(v_->num());
        case Value::Type::String:
            return int(std::strtol(skipSpace(v_->str()).c_str(), nullptr, 10));
        default:
            throw std::runtime_error("JSON value is not a number");
        }
    }
    unsigned u() const {
        if (!v_) {
            return 0;
        }
        if (v_->type() == Value::Type::Double) {
            return unsigned(int64_t(v_->num()));
        }
        return unsigned(int64_t(i64()));
    }
    long long i64() const {
        if (!v_) {
            return 0;
        }
        switch (v_->type()) {
        case Value::Type::Int:
        case Value::Type::Bool:
            return raw();
        case Value::Type::Double:
            return (long long)v_->num();
        case Value::Type::String:
            return std::strtoll(skipSpace(v_->str()).c_str(), nullptr, 10);
        default:
            throw std::runtime_error("JSON value is not a number");
        }
    }
    bool b() const {
        if (!v_) {
            return false;
        }
        switch (v_->type()) {
        case Value::Type::Int:
        case Value::Type::Bool:
            return raw() != 0;
        case Value::Type::Double:
            return v_->num() != 0;
        case Value::Type::String: {
            // NSString boolValue: optional whitespace, sign and zeros, then Y/y/T/t or a digit 1-9.
            const std::string s = skipSpace(v_->str());
            size_t k = 0;
            if (k < s.size() && (s[k] == '+' || s[k] == '-')) {
                ++k;
            }
            while (k < s.size() && s[k] == '0') {
                ++k;
            }
            return k < s.size() &&
                   (s[k] == 'Y' || s[k] == 'y' || s[k] == 'T' || s[k] == 't' || (s[k] >= '1' && s[k] <= '9'));
        }
        default:
            throw std::runtime_error("JSON value is not a boolean");
        }
    }

    // String value (throws when missing or not a string).
    const std::string& str() const {
        if (!v_) {
            throw std::runtime_error("JSON string missing");
        }
        return v_->str();
    }

    // Equality with a string, like isEqual: (false for non-strings and missing values).
    bool is(std::string_view s) const { return v_ && v_->isString() && v_->str() == s; }

    const Value* value() const { return v_; }

private:
    const Value* v_ = nullptr;

    long long raw() const {
        return v_->type() == Value::Type::Bool ? (v_->boolean() ? 1 : 0) : (long long)v_->integer();
    }

    static std::string skipSpace(const std::string& s) {
        size_t k = 0;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '\n' || s[k] == '\r')) {
            ++k;
        }
        return s.substr(k);
    }
};

}   // namespace captain_toad::data
