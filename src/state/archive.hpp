#pragma once
#include <array>
#include <algorithm>
#include <unordered_map>
#include <bit>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <locale>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Portable value archive, never an object-memory dump. Integers have an
// explicit little-endian representation (64 bits, except bytes/bools).
// Game schemas name every field and reconstruct resource bindings separately.
namespace state {
template <bool Reading> class Archive {
  public:
    static constexpr bool reading = Reading;
    std::vector<std::uint8_t> bytes;
    explicit Archive(std::span<const std::uint8_t> input = {}) : input_(input) {}
    template <class... T> void operator()(T &...values) { (value(values), ...); }
    void finish() {
        if constexpr (Reading)
            if (at_ != input_.size())
                fail();
    }
    static void fail() { throw std::runtime_error("The save data is invalid or incompatible."); }

  private:
    std::span<const std::uint8_t> input_;
    std::size_t at_{};
    std::uint64_t word(std::uint64_t n, int count) {
        if constexpr (Reading) {
            if (input_.size() - at_ < std::size_t(count))
                fail();
            n = 0;
            for (int i = 0; i < count; ++i)
                n |= std::uint64_t(input_[at_++]) << (8 * i);
        } else {
            if (bytes.size() + count > 64 * 1024 * 1024)
                fail();
            for (int i = 0; i < count; ++i)
                bytes.push_back(std::uint8_t(n >> (8 * i)));
        }
        return n;
    }
    template <class T> void value(T &v) {
        if constexpr (std::is_same_v<T, bool>) {
            auto n = word(v ? 1 : 0, 1);
            if (n > 1)
                fail();
            v = n != 0;
        } else if constexpr (std::is_enum_v<T>) {
            auto n = static_cast<std::underlying_type_t<T>>(v);
            value(n);
            v = static_cast<T>(n);
        } else if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(T) == 1)
                v = std::bit_cast<T>(std::uint8_t(word(std::uint8_t(v), 1)));
            else if constexpr (std::is_signed_v<T>) {
                const auto n =
                    std::bit_cast<std::int64_t>(word(std::bit_cast<std::uint64_t>(std::int64_t(v)), 8));
                if (n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
                    fail();
                v = T(n);
            } else {
                const auto n = word(v, 8);
                if (n > std::numeric_limits<T>::max())
                    fail();
                v = T(n);
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            double n = std::bit_cast<double>(word(std::bit_cast<std::uint64_t>(double(v)), 8));
            if (!std::isfinite(n))
                fail();
            v = T(n);
        } else
            v.persist(*this);
    }
    std::size_t count(std::size_t n, std::uint64_t limit = 4 * 1024 * 1024) {
        auto size = word(n, 8);
        if (size > limit)
            fail();
        if constexpr (Reading)
            if (size > input_.size() - at_)
                fail();
        return std::size_t(size);
    }
    template <class T, std::size_t N> void value(std::array<T, N> &v) {
        for (auto &x : v)
            value(x);
    }
    template <class T, std::size_t N> void value(T (&v)[N]) {
        for (auto &x : v)
            value(x);
    }
    template <class T> void value(std::vector<T> &v) {
        auto n = count(v.size(), sizeof(T) == 1 ? 64 * 1024 * 1024 : 4 * 1024 * 1024);
        if constexpr (Reading)
            v.resize(n);
        for (auto &x : v)
            value(x);
    }
    void value(std::vector<bool> &v) {
        auto n = count(v.size());
        if constexpr (Reading)
            v.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            bool x = v[i];
            value(x);
            v[i] = x;
        }
    }
    template <class T> void value(std::deque<T> &v) {
        auto n = count(v.size());
        if constexpr (Reading)
            v.resize(n);
        for (auto &x : v)
            value(x);
    }
    void value(std::string &v) {
        auto n = count(v.size());
        if constexpr (Reading)
            v.resize(n);
        for (auto &x : v)
            value(x);
    }
    template <class T> void value(std::optional<T> &v) {
        bool exists = v.has_value();
        value(exists);
        if constexpr (Reading) {
            if (!exists)
                v.reset();
            else if (!v)
                v.emplace();
        }
        if (exists)
            value(*v);
    }
    template <class K, class V> void value(std::map<K, V> &v) {
        auto n = count(v.size());
        if constexpr (Reading) {
            v.clear();
            for (std::size_t i = 0; i < n; ++i) {
                K k{};
                V x{};
                value(k);
                value(x);
                if (!v.emplace(k, std::move(x)).second)
                    fail();
            }
        } else
            for (auto &[key, x] : v) {
                auto k = key;
                value(k);
                value(x);
            }
    }
    void value(std::mt19937 &v) { random_value(v); }
    void value(std::minstd_rand &v) { random_value(v); }
    template<class R> void random_value(R &v) {
        std::string encoded;
        if constexpr (!Reading) {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << v;
            encoded = out.str();
        }
        value(encoded);
        if constexpr (Reading) {
            std::istringstream in(encoded);
            in.imbue(std::locale::classic());
            if (!(in >> v))
                fail();
            in >> std::ws;
            if (!in.eof())
                fail();
        }
    }
    template <class T> void value(std::set<T> &v) {
        auto n = count(v.size());
        if constexpr (Reading) {
            v.clear();
            for (std::size_t i = 0; i < n; ++i) {
                T x{};
                value(x);
                if (!v.insert(x).second)
                    fail();
            }
        } else
            for (auto x : v)
                value(x);
    }
    template <class K, class V> void value(std::unordered_map<K, V> &v) {
        auto n = count(v.size());
        if constexpr (Reading) {
            v.clear();
            for (std::size_t i = 0; i < n; ++i) {
                K k{};
                V x{};
                value(k);
                value(x);
                if (!v.emplace(k, std::move(x)).second)
                    fail();
            }
        } else {
            std::vector<K> keys;
            for (const auto &item : v)
                keys.push_back(item.first);
            std::sort(keys.begin(), keys.end());
            for (auto k : keys) {
                value(k);
                value(v.at(k));
            }
        }
    }
    template <class K, class V> void value(std::pair<K, V> &v) {
        value(v.first);
        value(v.second);
    }
};
using Writer = Archive<false>;
using Reader = Archive<true>;
} // namespace state
