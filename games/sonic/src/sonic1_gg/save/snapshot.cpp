// Quick-save serialization.
//
// A save is a text header (format name, schema version, game id, ROM SHA-256)
// followed by a payload and its SHA-256. The payload is every field listed in
// snapshot_fields.hpp, in that order, as little-endian integers (booleans one
// byte, enums four, other integers eight); containers are prefixed with their
// size. No pointers, padding or host state are written, so saves are portable
// between platforms. Older schemas load with defaults for the newer fields.

#include "sonic1_gg/save/snapshot.hpp"
#include "platform/sha256.hpp"
#include "sonic1_gg/data/content_store.hpp"
#include "sonic1_gg/save/snapshot_fields.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace sonic1_gg {
namespace {
constexpr std::size_t maximum = 32 * 1024 * 1024;
// Schema 2 adds invincibility-star and ring-sparkle state, 3 widescreen staging,
// 4 the death fade, game over and continue. Schema 1-3 files still load.
constexpr int schema = 4;

std::string header_for(int version) {
    return "BLUESTREAK-QUICKSAVE\n" + std::to_string(version) + "\nsonic-gg\n" + rom_sha256 + "\n";
}

struct Writer {
    static constexpr bool reading = false;
    Bytes bytes;

    void integer(std::uint64_t n, unsigned size) {
        for (unsigned i = 0; i < size; ++i) {
            bytes.push_back(std::uint8_t(n >> (i * 8)));
        }
        if (bytes.size() > maximum) {
            throw std::runtime_error("Quick save exceeds size limit");
        }
    }

    template <class... T>
    void operator()(T&... v) {
        (one(v), ...);
    }

    template <class T>
    void one(T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            integer(v, 1);
        } else if constexpr (std::is_enum_v<T>) {
            integer(static_cast<std::uint32_t>(v), 4);
        } else if constexpr (std::is_integral_v<T>) {
            integer(static_cast<std::uint64_t>(v), 8);
        } else if constexpr (std::is_same_v<T, float>) {
            integer(std::bit_cast<std::uint32_t>(v), 4);
        } else {
            StateAccess::visit(*this, v);
        }
    }

    template <class T, std::size_t N>
    void one(std::array<T, N>& v) {
        for (auto& x : v) {
            one(x);
        }
    }

    template <class T>
    void one(std::vector<T>& v) {
        integer(v.size(), 8);
        for (auto& x : v) {
            one(x);
        }
    }

    // Byte payloads stay compact, unlike numeric state fields.
    void one(Bytes& v) {
        integer(v.size(), 8);
        bytes.insert(bytes.end(), v.begin(), v.end());
    }

    template <class K, class V>
    void one(std::map<K, V>& v) {
        integer(v.size(), 8);
        for (auto& [key, value] : v) {
            auto k = key;
            one(k);
            one(value);
        }
    }
};

struct Reader {
    static constexpr bool reading = true;
    std::span<const std::uint8_t> bytes;
    int version{schema};
    std::size_t pos{}, elements{};

    std::uint64_t integer(unsigned size) {
        if (size > bytes.size() - pos) {
            throw std::runtime_error("Quick save is truncated");
        }
        std::uint64_t n = 0;
        for (unsigned i = 0; i < size; ++i) {
            n |= std::uint64_t(bytes[pos++]) << (i * 8);
        }
        return n;
    }

    std::size_t count() {
        auto n = integer(8);
        if (n > bytes.size() - pos || n > maximum || elements + n > maximum) {
            throw std::runtime_error("Invalid quick-save collection size");
        }
        elements += n;
        return std::size_t(n);
    }

    template <class... T>
    void operator()(T&... v) {
        (one(v), ...);
    }

    template <class T>
    void one(T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            auto n = integer(1);
            if (n > 1) {
                throw std::runtime_error("Invalid saved flag");
            }
            v = n;
        } else if constexpr (std::is_enum_v<T>) {
            auto n = integer(4);
            unsigned last = 0;
            if constexpr (std::is_same_v<T, FrontEnd::Scene>) {
                last = unsigned(version < 4 ? FrontEnd::Scene::Results : FrontEnd::Scene::GameOver);
            } else if constexpr (std::is_same_v<T, GameOver::Phase>) {
                last = unsigned(GameOver::Phase::Done);
            } else if constexpr (std::is_same_v<T, Results::Phase>) {
                last = unsigned(Results::Phase::Done);
            } else if constexpr (std::is_same_v<T, Ending::Phase>) {
                last = unsigned(Ending::Phase::Done);
            } else if constexpr (std::is_same_v<T, SoundEvent::Kind>) {
                last = unsigned(SoundEvent::Kind::Fade);
            } else {
                static_assert(sizeof(T) == 0, "Specify saved enum bounds");
            }
            if (n > last) {
                throw std::runtime_error("Invalid saved enum");
            }
            v = static_cast<T>(n);
        } else if constexpr (std::is_integral_v<T>) {
            auto n = integer(8);
            if constexpr (std::is_signed_v<T>) {
                auto signed_n = std::bit_cast<std::int64_t>(n);
                if (signed_n < std::numeric_limits<T>::min() ||
                    signed_n > std::numeric_limits<T>::max()) {
                    throw std::runtime_error("Invalid saved integer");
                }
                v = static_cast<T>(signed_n);
            } else {
                if (n > std::numeric_limits<T>::max()) {
                    throw std::runtime_error("Invalid saved integer");
                }
                v = static_cast<T>(n);
            }
        } else if constexpr (std::is_same_v<T, float>) {
            v = std::bit_cast<float>(std::uint32_t(integer(4)));
            if (!std::isfinite(v)) {
                throw std::runtime_error("Invalid saved audio sample");
            }
        } else {
            StateAccess::visit(*this, v);
        }
    }

    template <class T, std::size_t N>
    void one(std::array<T, N>& v) {
        for (auto& x : v) {
            one(x);
        }
    }

    template <class T>
    void one(std::vector<T>& v) {
        v.resize(count());
        for (auto& x : v) {
            one(x);
        }
    }

    void one(Bytes& v) {
        auto n = count();
        v.assign(bytes.begin() + pos, bytes.begin() + pos + n);
        pos += n;
    }

    template <class K, class V>
    void one(std::map<K, V>& v) {
        auto n = count();
        v.clear();
        for (std::size_t i = 0; i < n; ++i) {
            K k{};
            V value{};
            one(k);
            one(value);
            if (!v.emplace(k, std::move(value)).second) {
                throw std::runtime_error("Duplicate saved key");
            }
        }
    }
};
} // namespace

Bytes save_snapshot(Session& session, SoundDriver& sound, gg::Psg& psg, LogoVoice& voice,
                    std::uint64_t ticks) {
    Writer a;
    a(session, sound, psg, voice, ticks);
    if (a.bytes.size() > maximum) {
        throw std::runtime_error("Quick save exceeds size limit");
    }
    auto digest = platform::sha256(a.bytes);
    const auto header = header_for(schema);
    Bytes out(header.begin(), header.end());
    out.insert(out.end(), digest.begin(), digest.end());
    out.insert(out.end(), a.bytes.begin(), a.bytes.end());
    return out;
}

void load_snapshot(std::span<const std::uint8_t> bytes, Session& session, SoundDriver& sound,
                   gg::Psg& psg, LogoVoice& voice, std::uint64_t& ticks) {
    int version = 0;
    std::string header;
    for (int candidate = 1; candidate <= schema && !version; ++candidate) {
        header = header_for(candidate);
        if (bytes.size() <= maximum && bytes.size() >= header.size() + 64 &&
            std::equal(header.begin(), header.end(), bytes.begin())) {
            version = candidate;
        }
    }
    if (!version) {
        throw std::runtime_error("Quick save belongs to an unsupported game or save version");
    }
    auto payload = bytes.subspan(header.size() + 64);
    auto expected = std::string(bytes.begin() + header.size(), bytes.begin() + header.size() + 64);
    if (platform::sha256(payload) != expected) {
        throw std::runtime_error("Quick save is damaged (checksum mismatch)");
    }
    Reader a{payload, version};
    a(session, sound, psg, voice, ticks);
    if (a.pos != payload.size()) {
        throw std::runtime_error("Unexpected quick-save data");
    }
}
} // namespace sonic1_gg
