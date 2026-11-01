// Minimal JSON document model for the asset importer: ordered objects, a distinct integer type
// (the package keeps Python's int/float distinction), and Python-compatible float formatting.
// formatFloat() reproduces Python's repr(float), which the generated shader sources depend on.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace captain_toad::json {

class Value {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), int_(b) {}
    Value(int v) : type_(Type::Int), int_(v) {}
    Value(unsigned v) : type_(Type::Int), int_(v) {}
    Value(long v) : type_(Type::Int), int_(v) {}
    Value(long long v) : type_(Type::Int), int_(v) {}
    Value(unsigned long v) : type_(Type::Int), int_(int64_t(v)) {}
    Value(unsigned long long v) : type_(Type::Int), int_(int64_t(v)) {}
    Value(double v) : type_(Type::Double), double_(v) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Value array() {
        Value v;
        v.type_ = Type::Array;
        return v;
    }
    static Value object() {
        Value v;
        v.type_ = Type::Object;
        return v;
    }
    template <class T>
    static Value list(const std::vector<T>& items) {
        Value v = array();
        for (const auto& x : items) {
            v.push(Value(x));
        }
        return v;
    }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Int || type_ == Type::Double; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Numbers: both integer and float values convert, like Python arithmetic.
    double num() const;
    int64_t integer() const;
    bool boolean() const;
    const std::string& str() const;
    // Python truthiness (used where the exporters test a value with `if x`).
    bool truthy() const;

    // Arrays.
    Value& push(Value v);
    size_t size() const;
    Value& operator[](size_t i);
    const Value& operator[](size_t i) const;
    Value& operator[](int i) { return (*this)[size_t(i)]; }
    const Value& operator[](int i) const { return (*this)[size_t(i)]; }
    std::vector<Value>& items();
    const std::vector<Value>& items() const;

    // Objects (insertion ordered). operator[] inserts a null member when missing.
    Value& operator[](const std::string& key);
    Value& operator[](const char* key) { return (*this)[std::string(key)]; }
    const Value& at(const std::string& key) const;
    const Value* find(const std::string& key) const;
    Value* find(const std::string& key);
    bool has(const std::string& key) const { return find(key) != nullptr; }
    void erase(const std::string& key);
    std::vector<Member>& members();
    const std::vector<Member>& members() const;

private:
    void need(Type t) const;
    Type type_ = Type::Null;
    int64_t int_ = 0;
    double double_ = 0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<Member> object_;
};

// Python repr(float): shortest round-trip digits, fixed notation for exponents -5 < e < 16.
std::string formatFloat(double v);
// Python str() of a JSON number (integers without a decimal point).
std::string formatNumber(const Value& v);
// Serialises a document; indent < 0 writes compact JSON.
std::string dump(const Value& v, int indent = -1);
// Parses a JSON document (also accepts NaN, Infinity and -Infinity, which dump() writes). Numbers
// without a fraction or exponent become integers; duplicate keys keep the last value. Throws
// std::runtime_error with the byte offset on malformed input.
Value parse(std::string_view text);

}   // namespace captain_toad::json
