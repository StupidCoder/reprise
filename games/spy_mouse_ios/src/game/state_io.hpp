// Quick saves keep the whole state of the scene being played, so loading one continues at once
// (no re-run of the scene). Every class whose fields change in play lists them once:
//
//     template <class Io> void io(Io& io) { io(position, heading, ...); }
//
// and the same list writes (StateWriter) and reads (StateReader). Loading builds the scene from
// the game's files as at its start, then reads the saved fields over it, so what comes from the
// files and never changes (the level's layout, tables, constants) is left out; each io() names
// what it leaves out and why. tools/check_quick_save.py checks that every field of a class with
// an io() is in one list or the other.
//
// Encoding: little-endian, floats by their bits, sizes as 32-bit counts.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace doc::game {

struct StateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct StateWriter {
    static constexpr bool reading = false;
    std::vector<std::uint8_t> bytes;
    // For tests: where each class's fields lie in `bytes` (its type name), when set.
    struct Mark {
        std::size_t begin = 0, end = 0;
        int depth = 0;
        const char* type = "";
    };
    std::vector<Mark>* marks = nullptr;

    template <class... T>
    void operator()(T&... values) {
        (put(values), ...);
    }
    // For callers holding the state as const: writing changes nothing.
    template <class T>
    void save(const T& value) {
        put(const_cast<T&>(value));
    }

    void count(std::size_t n) { word(std::uint64_t(n), 4); }

private:
    void word(std::uint64_t v, int size) {
        for (int i = 0; i < size; ++i) bytes.push_back(std::uint8_t(v >> (8 * i)));
    }
    template <class T>
    void put(T& v) {
        if constexpr (std::is_same_v<T, bool>) word(v ? 1 : 0, 1);
        else if constexpr (std::is_enum_v<T>) word(std::uint64_t(std::underlying_type_t<T>(v)), sizeof(T));
        else if constexpr (std::is_same_v<T, float>) word(std::bit_cast<std::uint32_t>(v), 4);
        else if constexpr (std::is_same_v<T, double>) word(std::bit_cast<std::uint64_t>(v), 8);
        else if constexpr (std::is_integral_v<T>) word(std::uint64_t(v), sizeof(T));
        else if constexpr (std::is_array_v<T>) {
            for (auto& e : v) put(e);
        } else if (marks) {
            const std::size_t mark = marks->size();
            marks->push_back({bytes.size(), 0, depth_++, typeid(T).name()});
            v.io(*this);
            --depth_;
            (*marks)[mark].end = bytes.size();
        } else {
            v.io(*this);
        }
    }
    int depth_ = 0;
    void put(std::string& s) {
        count(s.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    template <class T, std::size_t N>
    void put(std::array<T, N>& a) {
        for (T& e : a) put(e);
    }
    template <class T>
    void put(std::vector<T>& v) {
        count(v.size());
        if constexpr (std::is_same_v<T, bool>) {
            for (bool e : v) word(e ? 1 : 0, 1);
        } else {
            for (T& e : v) put(e);
        }
    }
    template <class T>
    void put(std::deque<T>& v) {
        count(v.size());
        for (T& e : v) put(e);
    }
    template <class K, class V>
    void put(std::map<K, V>& m) {
        count(m.size());
        for (auto& [k, v] : m) {
            put(const_cast<K&>(k));
            put(v);
        }
    }
    template <class K>
    void put(std::set<K>& s) {
        count(s.size());
        for (const K& k : s) put(const_cast<K&>(k));
    }
    template <class T>
    void put(std::optional<T>& o) {
        word(o ? 1 : 0, 1);
        if (o) put(*o);
    }
    template <class A, class B>
    void put(std::pair<A, B>& p) {
        put(p.first);
        put(p.second);
    }
};

struct StateReader {
    static constexpr bool reading = true;
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;

    template <class... T>
    void operator()(T&... values) {
        (get(values), ...);
    }
    template <class T>
    void load(T& value) {
        get(value);
    }

    std::size_t count() {
        const std::size_t n = std::size_t(word(4));
        // (Every element takes at least a byte: a larger count is a damaged save.)
        if (n > bytes.size() - at) throw StateError("The quick save is damaged.");
        return n;
    }

private:
    std::uint64_t word(int size) {
        if (bytes.size() - at < std::size_t(size)) throw StateError("The quick save is incomplete.");
        std::uint64_t v = 0;
        for (int i = 0; i < size; ++i) v |= std::uint64_t(bytes[at++]) << (8 * i);
        return v;
    }
    template <class T>
    void get(T& v) {
        if constexpr (std::is_same_v<T, bool>) v = word(1) != 0;
        else if constexpr (std::is_enum_v<T>) v = T(std::underlying_type_t<T>(word(sizeof(T))));
        else if constexpr (std::is_same_v<T, float>) v = std::bit_cast<float>(std::uint32_t(word(4)));
        else if constexpr (std::is_same_v<T, double>) v = std::bit_cast<double>(word(8));
        else if constexpr (std::is_integral_v<T>) v = T(word(sizeof(T)));
        else if constexpr (std::is_array_v<T>) {
            for (auto& e : v) get(e);
        } else v.io(*this);
    }
    void get(std::string& s) {
        const std::size_t n = count();
        s.assign(reinterpret_cast<const char*>(bytes.data() + at), n);
        at += n;
    }
    template <class T, std::size_t N>
    void get(std::array<T, N>& a) {
        for (T& e : a) get(e);
    }
    // Elements already there keep what their io() leaves out (built from the files with the
    // scene); new ones start from their defaults. Things that only the scene's files can make
    // (cats, bosses: no default) are as many as the scene built.
    template <class T>
    void get(std::vector<T>& v) {
        const std::size_t n = count();
        if constexpr (std::is_default_constructible_v<T>) v.resize(n);
        else if (n != v.size()) throw StateError("The quick save does not match the scene.");
        if constexpr (std::is_same_v<T, bool>) {
            for (std::size_t i = 0; i < v.size(); ++i) v[i] = word(1) != 0;
        } else {
            for (T& e : v) get(e);
        }
    }
    template <class T>
    void get(std::deque<T>& v) {
        v.resize(count());
        for (T& e : v) get(e);
    }
    template <class K, class V>
    void get(std::map<K, V>& m) {
        m.clear();
        for (std::size_t n = count(); n > 0; --n) {
            K k{};
            get(k);
            get(m[k]);
        }
    }
    template <class K>
    void get(std::set<K>& s) {
        s.clear();
        for (std::size_t n = count(); n > 0; --n) {
            K k{};
            get(k);
            s.insert(std::move(k));
        }
    }
    template <class T>
    void get(std::optional<T>& o) {
        if (word(1) == 0) {
            o.reset();
            return;
        }
        if (!o) o.emplace();
        get(*o);
    }
    template <class A, class B>
    void get(std::pair<A, B>& p) {
        get(p.first);
        get(p.second);
    }
};

}  // namespace doc::game
