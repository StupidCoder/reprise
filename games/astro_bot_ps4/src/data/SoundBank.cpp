#include "data/SoundBank.hpp"

#include <algorithm>
#include <cstring>

namespace beacon::data {

namespace {

constexpr std::uint32_t kNoLoop = 0xffffffffu;
constexpr std::uint32_t kAtrac9Rates[16] = {11025, 12000, 16000, 22050, 24000, 32000,  44100,  48000,
                                            44100, 48000, 64000, 88200, 96000, 128000, 176400, 192000};
constexpr std::uint32_t kAtrac9Channels[8] = {1, 2, 2, 6, 8, 4, 0, 0};

// Finds the ATRAC9 configuration among an entry's u32 fields: top byte 0xFE, and its sample rate and
// channel layout must match the entry.
bool findAtrac9Config(ByteView entry, Wave& w) {
    for (std::size_t at = 40; at + 4 <= entry.size(); at += 4) {
        std::uint32_t v = u32(entry, at);
        if ((v >> 24) != 0xfe) {
            continue;
        }
        std::array<std::uint8_t, 4> cfg = {0xfe, std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v)};
        std::uint32_t rate = kAtrac9Rates[cfg[1] >> 4];
        std::uint32_t channels = kAtrac9Channels[(cfg[1] >> 1) & 7];
        if (rate == w.sampleRate && channels == w.channels) {
            w.atrac9Config = cfg;
            return true;
        }
    }
    return false;
}

// Self-relative offset table of a chunk payload: u32 0, u32 count, count x u32.
std::vector<std::size_t> offsetTable(ByteView d, std::size_t payload) {
    std::vector<std::size_t> out;
    const std::uint32_t count = u32(d, payload + 4);
    for (std::uint32_t i = 0; i < count && payload + 12 + i * 4 <= d.size(); ++i) {
        const std::size_t slot = payload + 8 + i * 4;
        out.push_back(slot + u32(d, slot));
    }
    return out;
}

std::string cString(ByteView d, std::size_t at) {
    std::string s;
    while (at < d.size() && d[at] && s.size() < 128) {
        s += char(d[at++]);
    }
    return s;
}

// Sequence operands: type byte (high nibble 0/1 = +/- BCD number of low-nibble bytes, others are
// variable references), until the next opcode byte (>= 0xa0).
std::vector<std::int64_t> operands(ByteView d, std::size_t& p, std::size_t end) {
    std::vector<std::int64_t> out;
    while (p < end && d[p] < 0xa0) {
        const std::uint8_t b = d[p++];
        const int type = b >> 4, n = b & 15;
        std::int64_t v = 0;
        for (int k = 0; k < n && p < end; ++k, ++p) {
            v = v * 100 + (d[p] >> 4) * 10 + (d[p] & 15);
        }
        out.push_back(type == 0 ? v : type == 1 ? -v : -1);
    }
    return out;
}

struct Chunks {
    std::size_t tone = 0, reqd = 0, name = 0, seqd = 0, trns = 0, lvar = 0;
};

void resolveUses(ByteView d, const Chunks& c, SoundBank& bank) {
    bank.waveUses.assign(bank.waves.size(), {});
    if (!c.reqd || !c.seqd) {
        return;
    }
    // Note -> wave from the tone keymaps.
    std::vector<int> noteWave(256, -1);
    if (c.tone) {
        const std::size_t header = c.tone + 8;   // first header: {u32, u16, u16, u32 count, u32 rel}
        const std::uint32_t keymaps = u32(d, header + 8);
        const std::size_t first = header + 12 + u32(d, header + 12);
        for (std::uint32_t k = 0; k < keymaps; ++k) {
            const std::size_t e = first + k * 16;
            const std::uint32_t regions = u16(d, e + 10);
            const std::size_t r0 = e + 12 + u32(d, e + 12);
            for (std::uint32_t r = 0; r < regions && r0 + r * 12 + 12 <= d.size(); ++r) {
                const std::uint16_t note = u16(d, r0 + r * 12 + 2), wave = u16(d, r0 + r * 12 + 4);
                if (note < 256) {
                    noteWave[note] = wave;
                }
            }
        }
    }
    // Sequence -> waves.
    const auto seqs = offsetTable(d, c.seqd);
    auto seqWaves = [&](std::size_t s) {
        std::vector<int> out;
        if (s >= seqs.size()) {
            return out;
        }
        const std::size_t e = seqs[s];
        std::size_t p = e + 16 + u32(d, e + 16);
        const std::size_t end = std::min(d.size(), p + u32(d, e + 12));
        while (p < end) {
            const std::uint8_t op = d[p++];
            auto args = operands(d, p, end);
            if (op == 0xa4 && args.size() >= 4 && args[3] >= 0 && args[3] < 256 && noteWave[args[3]] >= 0) {
                out.push_back(noteWave[args[3]]);
            } else if (op == 0xa2 && !args.empty() && args[0] >= 0) {
                out.push_back(int(args[0]));
            } else if (op == 0xff) {
                break;
            }
        }
        return out;
    };
    // Variable labels (LVAR entries are inline, 32 bytes each).
    std::vector<std::pair<std::string, std::vector<std::string>>> variables;
    if (c.lvar) {
        const std::uint32_t count = u32(d, c.lvar + 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t e = c.lvar + 8 + i * 32;
            if (e + 32 > d.size()) {
                break;
            }
            std::string var = cString(d, e + 8 + u32(d, e + 8));
            const std::size_t list = e + 16 + u32(d, e + 16);
            std::vector<std::string> labels;
            if (list + 12 <= d.size()) {
                const std::uint32_t n = u32(d, list + 4);
                std::size_t at = list + 8 + u32(d, list + 8);
                for (std::uint32_t k = 0; k < n && k < 64 && at < d.size(); ++k) {
                    labels.push_back(cString(d, at));
                    at += labels.back().size() + 1;
                }
            }
            variables.push_back({var, labels});
        }
    }
    auto stateLabel = [&](const std::string& var, int value) {
        for (const auto& [name, labels] : variables) {
            if (name == var && value >= 0 && std::size_t(value) < labels.size()) {
                return labels[value];
            }
        }
        return var + "=" + std::to_string(value);
    };
    const auto trns = c.trns ? offsetTable(d, c.trns) : std::vector<std::size_t>{};
    auto add = [&](int wave, const std::string& name, const std::string& state) {
        if (wave >= 0 && std::size_t(wave) < bank.waveUses.size()) {
            bank.waveUses[wave].push_back({name, state});
        }
    };
    const auto requests = offsetTable(d, c.reqd);
    for (const auto& n : bank.names) {
        if (n.request >= requests.size()) {
            continue;
        }
        const std::size_t r = requests[n.request];
        const std::uint32_t kind = u32(d, r + 4) & 0xff, index = u32(d, r + 8);
        if (kind == 1) {
            for (int w : seqWaves(index)) {
                add(w, n.name, "");
            }
        } else if (kind == 2 && index < trns.size()) {
            const std::size_t e = trns[index];
            const std::uint32_t tracks = u32(d, e + 0x1c);
            for (std::uint32_t k = 0; k < tracks && k < 64; ++k) {
                const std::size_t slot = e + 0x20 + k * 4;
                const std::size_t t = slot + u32(d, slot);
                if (t + 64 > d.size()) {
                    break;
                }
                // Condition: "... f1 0f <type> <length incl. 0> <variable> 0 <value>".
                std::string state, variable;
                for (int j : {4, 5}) {
                    const std::uint32_t rel = u32(d, t + j * 4);
                    if (!rel || t + j * 4 + rel + 8 > d.size()) {
                        continue;
                    }
                    const std::size_t a = t + j * 4 + rel;
                    for (std::size_t k2 = a; k2 < a + 8 && k2 + 4 < d.size(); ++k2) {
                        if (d[k2] == 0xf1 && d[k2 + 1] == 0x0f) {
                            const std::size_t len = d[k2 + 3];
                            if (k2 + 4 + len < d.size()) {
                                variable = cString(d, k2 + 4);
                                state = stateLabel(variable, d[k2 + 4 + len]);
                            }
                            break;
                        }
                    }
                    if (!state.empty()) {
                        break;
                    }
                }
                // Action: instruction list {operand count | 0x80 last, opcode} + operands {length,
                // little-endian bytes}; opcode 0x31 plays sequence operand[1] on slot operand[0]
                // (pieces with several layers start several sequences).
                const std::uint32_t rel = u32(d, t + 28);
                std::size_t a = t + 28 + rel;
                for (int i = 0; rel && i < 64 && a + 2 <= d.size(); ++i) {
                    const std::uint8_t head = d[a], op = d[a + 1];
                    a += 2;
                    std::vector<std::uint64_t> args;
                    for (int k = 0; k < (head & 0x7f) && a < d.size(); ++k) {
                        const std::size_t len = d[a++];
                        std::uint64_t v = 0;
                        for (std::size_t b2 = 0; b2 < len && b2 < 8 && a + b2 < d.size(); ++b2) {
                            v |= std::uint64_t(d[a + b2]) << (8 * b2);
                        }
                        a += len;
                        args.push_back(v);
                    }
                    if (op == 0x31 && args.size() >= 2) {
                        for (int w : seqWaves(std::size_t(args[1]))) {
                            add(w, n.name, state);
                        }
                    }
                    // Opcode 0x42 (interpreter 0xd56a00): the level of the piece's slot operand[0]
                    // becomes operand[2]; operands 3 and 4 are read and not used for it.
                    if (op == 0x42 && args.size() >= 3 && !variable.empty()) {
                        bank.levelRules.push_back({n.name, variable, state, float(args[2])});
                    }
                    if (head & 0x80) {
                        break;
                    }
                }
            }
        }
    }
}

}   // namespace

std::optional<SoundBank> loadSoundBank(ByteView d, std::string* error) {
    auto fail = [&](const std::string& message) -> std::optional<SoundBank> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (d.size() < 0x60 || std::memcmp(d.data(), "SXDF", 4) != 0) {
        return fail("not a sound bank (SXDF)");
    }
    SoundBank bank;
    const char* name = reinterpret_cast<const char*>(d.data() + 0x20);
    bank.name.assign(name, strnlen(name, 0x40));

    Chunks found;
    for (std::size_t at = 0x60; at + 8 <= d.size();) {
        const std::uint32_t size = u32(d, at + 4);
        const std::size_t payload = at + 8, end = payload + size;
        if (end > d.size()) {
            return fail("chunk exceeds the file");
        }
        const std::string tag(reinterpret_cast<const char*>(d.data() + at), 4);
        if (tag == "TONE") {
            found.tone = payload;
        } else if (tag == "REQD") {
            found.reqd = payload;
        } else if (tag == "SEQD") {
            found.seqd = payload;
        } else if (tag == "TRNS") {
            found.trns = payload;
        } else if (tag == "LVAR") {
            found.lvar = payload;
        }
        if (std::memcmp(d.data() + at, "WAVE", 4) == 0) {
            const std::uint32_t count = u32(d, payload + 4);
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::size_t slot = payload + 8 + i * 4;
                const std::size_t entry = slot + u32(d, slot);
                const std::size_t next =
                    i + 1 < count ? payload + 8 + (i + 1) * 4 + u32(d, payload + 8 + (i + 1) * 4) : end;
                if (entry + 40 > end || next > end || next < entry) {
                    return fail("wave entry out of range");
                }
                ByteView e = d.subspan(entry, next - entry);
                Wave w;
                w.codec = WaveCodec(e[4]);
                w.channels = e[5];
                w.sampleRate = u32(e, 8);
                w.samples = u32(e, 20);
                if (u32(e, 24) != kNoLoop && u32(e, 28) != kNoLoop) {
                    w.loopStart = u32(e, 24);
                    w.loopEnd = u32(e, 28);
                }
                const std::size_t dataAt = entry + 36 + u32(e, 36), dataSize = u32(e, 32);
                if (dataAt + dataSize > d.size()) {
                    return fail("wave data out of range");
                }
                w.data = d.subspan(dataAt, dataSize);
                if (w.codec == WaveCodec::Atrac9 && !findAtrac9Config(e, w)) {
                    return fail("ATRAC9 wave without a matching configuration");
                }
                if (w.codec != WaveCodec::Atrac9 && w.codec != WaveCodec::HeVag) {
                    return fail("unknown wave codec " + std::to_string(int(e[4])));
                }
                bank.waves.push_back(w);
            }
        } else if (std::memcmp(d.data() + at, "NAME", 4) == 0) {
            const std::uint32_t count = u32(d, payload + 4);
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::size_t e = payload + 8 + i * 12;
                const std::size_t s = e + u32(d, e);
                if (s >= d.size()) {
                    return fail("sound name out of range");
                }
                const char* str = reinterpret_cast<const char*>(d.data() + s);
                bank.names.push_back({std::string(str, strnlen(str, d.size() - s)), u32(d, e + 8)});
            }
        }
        if (size == 0) {
            break;
        }
        at = (end + 3) / 4 * 4;
    }
    resolveUses(d, found, bank);
    return bank;
}

}   // namespace data
