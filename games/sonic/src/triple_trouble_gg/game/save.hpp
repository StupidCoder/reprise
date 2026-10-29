// Quick saves: plain bytes (host order: saves stay on this machine) of the
// game's state groups, the picture and the time model (game/save.cpp); the
// module adds the arena force field (widescreen), the PSG and its tick count.
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace triple_trouble_gg::save {

template <class T>
struct is_vector : std::false_type {};

template <class T>
struct is_vector<std::vector<T>> : std::true_type {};

struct Writer {
    std::vector<std::uint8_t> out;

    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }

    template <class T>
    void value(const T& v) {
        bytes(&v, sizeof v);
    }

    template <class... T>
    void operator()(const T&... v) {
        (field(v), ...);
    }

    template <class T>
    void field(const T& v) {
        if constexpr (requires {
                          v.size();
                          v.data();
                      }) {
            for (const auto& e : v) {
                field(e);
            }
        } else {
            value(v);
        }
    }
};

struct Reader {
    std::span<const std::uint8_t> in;
    std::size_t pos{};

    void bytes(void* p, std::size_t n) {
        if (pos + n > in.size()) {
            throw std::runtime_error("The quick save is incomplete");
        }
        std::memcpy(p, in.data() + pos, n);
        pos += n;
    }

    template <class T>
    void value(T& v) {
        bytes(&v, sizeof v);
    }

    template <class... T>
    void operator()(T&... v) {
        (field(v), ...);
    }

    template <class T>
    void field(T& v) {
        if constexpr (requires {
                          v.size();
                          v.data();
                      }) {
            for (auto& e : v) {
                field(e);
            }
        } else {
            value(v);
        }
    }
};

template <class T>
inline void write_part(Writer& w, const T& part) {
    if constexpr (is_vector<T>::value) {
        w.value(std::uint32_t(part.size()));
        w.bytes(part.data(), part.size() * sizeof(typename T::value_type));
    } else {
        static_assert(std::is_trivially_copyable_v<T>);
        w.bytes(&part, sizeof part);
    }
}

template <class T>
inline void read_part(Reader& r, T& part) {
    if constexpr (is_vector<T>::value) {
        std::uint32_t n{};
        r.value(n);
        if (n > 4096) {
            throw std::runtime_error("The quick save is damaged");
        }
        part.resize(n);
        r.bytes(part.data(), n * sizeof(typename T::value_type));
    } else {
        r.bytes(&part, sizeof part);
    }
}

} // namespace triple_trouble_gg::save
