// =============================================================================================
// PicaShader.hpp - an interpreter for the console GPU's vertex shaders
// =============================================================================================
//
// This is a model of the graphics hardware, not game logic. The particle engine (Eft.hpp) draws
// its particles with 30 vertex programs stored in code.bin (DVLB blobs at 0x49da80.., pointer
// table 0x4f8f54; the program choice is 0x351b40). Running those ROM programs on the documented
// PICA200 instruction set keeps the quad construction, billboarding, texture-coordinate wrapping
// and colour clamping exactly as authored, instead of re-deriving every variant by hand.
//
// Written from the public shader binary and instruction-set documentation. The arithmetic is
// float32 (the hardware uses float24).
// =============================================================================================
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace captain_toad::pica {

using Vec4 = std::array<float, 4>;

// A parsed shader program (one DVLE of a DVLB).
struct Program {
    std::vector<uint32_t> code;   // instruction words
    std::vector<uint32_t> desc;   // operand descriptors (swizzles, negation, write masks)
    uint32_t mainPc = 0;
    uint32_t endPc = 0;
    std::vector<std::pair<int, Vec4>> constants;   // DVLE float constants (override uniforms)
    int outputs[16][4];                            // output register component -> semantic slot
    std::string uniformNames;

    // Semantic slots: 0..3 position, 4..7 colour, 8..9 texcoord0, 10..11 texcoord1, 12..13 texcoord2.
    static int slot(int type, int component) {
        switch (type) {
        case 0:
            return component;   // position
        case 2:
            return 4 + component;   // colour
        case 3:
            return component < 2 ? 8 + component : -1;   // texcoord0
        case 5:
            return component < 2 ? 10 + component : -1;   // texcoord1
        case 6:
            return component < 2 ? 12 + component : -1;   // texcoord2
        default:
            return -1;
        }
    }

    // float24 (1 sign, 7 exponent, 16 mantissa bits) -> float32.
    static float f24(uint32_t v) {
        if ((v & 0x7fffff) == 0) {
            return (v >> 23) ? -0.0f : 0.0f;
        }
        uint32_t sign = v >> 23;
        uint32_t e = (v >> 16) & 0x7f;
        uint32_t m = v & 0xffff;
        if (e == 0) {
            return 0.0f;
        }
        uint32_t bits = (sign << 31) | ((e - 63 + 127) << 23) | (m << 7);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }

    // Parse a DVLB (one DVLE is used).
    explicit Program(const uint8_t* raw, size_t size, int dvle = 0) {
        auto u32 = [&](size_t o) {
            if (o + 4 > size) {
                throw std::runtime_error("DVLB truncated");
            }
            uint32_t v;
            std::memcpy(&v, raw + o, 4);
            return v;
        };
        auto u16 = [&](size_t o) {
            return uint16_t(u32(o) & 0xffff);
        };
        if (std::memcmp(raw, "DVLB", 4)) {
            throw std::runtime_error("not a DVLB");
        }

        // DVLP: the shared code and operand descriptors.
        uint32_t n = u32(4);
        size_t p = 8 + 4 * n;
        if (std::memcmp(raw + p, "DVLP", 4)) {
            throw std::runtime_error("no DVLP");
        }
        uint32_t progOff = u32(p + 8);
        uint32_t progN = u32(p + 12);
        uint32_t descOff = u32(p + 16);
        uint32_t descN = u32(p + 20);
        for (uint32_t i = 0; i < progN; i++) {
            code.push_back(u32(p + progOff + 4 * i));
        }
        for (uint32_t i = 0; i < descN; i++) {
            desc.push_back(u32(p + descOff + 8 * i));
        }

        // DVLE: entry point, constants and output map of one program.
        size_t e = u32(8 + 4 * dvle);
        if (std::memcmp(raw + e, "DVLE", 4)) {
            throw std::runtime_error("no DVLE");
        }
        mainPc = u32(e + 8);
        endPc = u32(e + 12);
        uint32_t cOff = u32(e + 0x18);
        uint32_t cN = u32(e + 0x1c);
        uint32_t oOff = u32(e + 0x28);
        uint32_t oN = u32(e + 0x2c);
        for (uint32_t i = 0; i < cN; i++) {
            size_t c = e + cOff + 20 * i;
            if ((u32(c) & 0xff) != 2) {
                continue;   // bool / int constants are unused by eft
            }
            int reg = (u32(c) >> 16) & 0xff;
            constants.push_back({reg, {f24(u32(c + 4)), f24(u32(c + 8)), f24(u32(c + 12)), f24(u32(c + 16))}});
        }
        for (auto& o : outputs) {
            for (int& k : o) {
                k = -1;
            }
        }
        for (uint32_t i = 0; i < oN; i++) {
            size_t o = e + oOff + 8 * i;
            int type = u16(o);
            int reg = u16(o + 2);
            int mask = u16(o + 4);
            for (int k = 0; k < 4; k++) {
                if (mask & (1 << k)) {
                    outputs[reg & 15][k] = slot(type, k);
                }
            }
        }
    }
};

// The shader unit: uniforms in, one vertex through a program, outputs by semantic slot.
struct Machine {
    Vec4 c[96]{};                    // float uniforms
    bool b[16]{};                    // bool uniforms
    std::array<uint8_t, 4> i[4]{};   // int uniforms (loop count, start, increment)
    Vec4 v[16]{};                    // vertex inputs
    float out[16]{};                 // semantic slots, see Program::slot

    // PICA multiplication: 0 * inf = 0.
    static float mul(float a, float b) {
        float r = a * b;
        if (std::isnan(r) && !std::isnan(a) && !std::isnan(b)) {
            return 0.0f;
        }
        return r;
    }

    // Runs the program for the current inputs and fills `out`.
    void run(const Program& p) { Execution(*this, p).execute(); }

private:
    // The state of one program run: temporary and output registers, the address registers, the
    // condition flags and the nested control blocks.
    class Execution {
    public:
        Execution(Machine& m, const Program& p) : m_(m), p_(p) {}

        void execute() {
            uint32_t pc = p_.mainPc;
            for (int budget = 100000; budget > 0; budget--) {
                // Reaching the end of the innermost block: repeat a loop body, or resume after it.
                if (!blocks_.empty() && pc == blocks_.back().end) {
                    Block& b = blocks_.back();
                    loopCounter_ += b.counterStep;
                    if (b.kind == Block::Kind::Loop && b.repeats > 0) {
                        b.repeats--;
                        pc = b.start;
                    } else {
                        pc = b.resume;
                        blocks_.pop_back();
                    }
                    continue;
                }
                if (pc >= p_.code.size()) {
                    throw std::runtime_error("shader pc out of range");
                }
                const uint32_t word = p_.code[pc];
                const uint32_t op = word >> 26;
                if (op >= 0x30) {
                    multiplyAdd(word, op);
                } else if (op == 0x2e || op == 0x2f) {
                    compare(word);
                } else if (op < 0x20) {
                    arithmetic(word, op);
                } else {
                    const std::optional<uint32_t> next = flow(word, op, pc);
                    if (!next) {
                        writeOutputs();
                        return;
                    }
                    pc = *next;
                    continue;
                }
                pc++;
            }
            throw std::runtime_error("shader did not terminate");
        }

    private:
        // A called subroutine, a conditional body or a loop body: instructions [start, end),
        // then execution resumes at `resume`. A loop body runs `repeats` more times, and every
        // pass adds `counterStep` to the loop counter.
        struct Block {
            enum class Kind { Call, If, Loop };
            Kind kind;
            uint32_t start;
            uint32_t end;
            uint32_t resume;
            int repeats = 0;
            int counterStep = 0;
        };

        Machine& m_;
        const Program& p_;
        Vec4 temp_[16]{};
        Vec4 output_[16]{};
        int address_[2] = {0, 0};
        int loopCounter_ = 0;
        bool flag_[2] = {false, false};
        std::vector<Block> blocks_;

        // A source register: an input, a temporary, or a uniform (optionally indexed by an
        // address register or the loop counter; out-of-range uniforms read as ones, unverified).
        Vec4 source(uint32_t reg, int index) const {
            if (reg < 0x10) {
                return m_.v[reg];
            }
            if (reg < 0x20) {
                return temp_[reg - 0x10];
            }
            const int offsets[4] = {0, address_[0], address_[1], loopCounter_};
            const int k = int(reg - 0x20) + offsets[index & 3];
            return k >= 0 && k < 96 ? m_.c[k] : Vec4{1, 1, 1, 1};
        }

        // Source operand 0, 1 or 2 with the swizzle and negation of the operand descriptor.
        Vec4 operand(uint32_t reg, int index, uint32_t desc, int which) const {
            static const int negateBit[3] = {4, 13, 22};
            static const int swizzleShift[3] = {5, 14, 23};
            const Vec4 s = source(reg, index);
            const bool negate = (desc >> negateBit[which]) & 1;
            const uint32_t swizzle = (desc >> swizzleShift[which]) & 0xff;
            Vec4 result;
            for (int k = 0; k < 4; k++) {
                const float value = s[(swizzle >> ((3 - k) * 2)) & 3];
                result[k] = negate ? -value : value;
            }
            return result;
        }

        // Writes the components selected by the descriptor's mask to an output or temporary.
        void store(uint32_t reg, uint32_t desc, const Vec4& value) {
            Vec4& target = reg < 0x10 ? output_[reg] : temp_[reg - 0x10];
            for (int k = 0; k < 4; k++) {
                if (desc & (1 << (3 - k))) {
                    target[k] = value[k];
                }
            }
        }

        // MAD / MADI: d = a * b + c (the I form swaps which source may use relative addressing).
        void multiplyAdd(uint32_t word, uint32_t op) {
            const bool wideC = op < 0x38;
            const uint32_t desc = p_.desc[word & 0x1f];
            const uint32_t c = (word >> 5) & (wideC ? 0x7f : 0x1f);
            const uint32_t b = (word >> (wideC ? 12 : 10)) & (wideC ? 0x1f : 0x7f);
            const uint32_t a = (word >> 17) & 0x1f;
            const int index = (word >> 22) & 3;
            const Vec4 A = operand(a, 0, desc, 0);
            const Vec4 B = operand(b, wideC ? 0 : index, desc, 1);
            const Vec4 C = operand(c, wideC ? index : 0, desc, 2);
            Vec4 result;
            for (int k = 0; k < 4; k++) {
                result[k] = mul(A[k], B[k]) + C[k];
            }
            store((word >> 24) & 0x1f, desc, result);
        }

        // CMP: compares x and y components into the two condition flags.
        void compare(uint32_t word) {
            const uint32_t desc = p_.desc[word & 0x7f];
            const Vec4 A = operand((word >> 12) & 0x7f, (word >> 19) & 3, desc, 0);
            const Vec4 B = operand((word >> 7) & 0x1f, 0, desc, 1);
            const int modes[2] = {int((word >> 24) & 7), int((word >> 21) & 7)};
            for (int k = 0; k < 2; k++) {
                const float x = A[k], y = B[k];
                switch (modes[k]) {
                case 0:
                    flag_[k] = x == y;
                    break;
                case 1:
                    flag_[k] = x != y;
                    break;
                case 2:
                    flag_[k] = x < y;
                    break;
                case 3:
                    flag_[k] = x <= y;
                    break;
                case 4:
                    flag_[k] = x > y;
                    break;
                case 5:
                    flag_[k] = x >= y;
                    break;
                default:
                    flag_[k] = true;
                }
            }
        }

        // Instructions with one or two sources (the I forms widen the second source instead).
        void arithmetic(uint32_t word, uint32_t op) {
            const uint32_t desc = p_.desc[word & 0x7f];
            const bool wideB = op >= 0x18;
            const uint32_t a = (word >> (wideB ? 14 : 12)) & (wideB ? 0x1f : 0x7f);
            const uint32_t b = (word >> 7) & (wideB ? 0x7f : 0x1f);
            const int index = (word >> 19) & 3;
            const Vec4 A = operand(a, wideB ? 0 : index, desc, 0);
            const Vec4 B = operand(b, wideB ? index : 0, desc, 1);
            auto dot = [&](int n, bool homogeneous) {
                float sum = 0;
                for (int k = 0; k < n; k++) {
                    sum = sum + mul(homogeneous && k == 3 ? 1.0f : A[k], B[k]);
                }
                return sum;
            };
            auto each = [&](auto f) {
                Vec4 r;
                for (int k = 0; k < 4; k++) {
                    r[k] = f(A[k], B[k]);
                }
                return r;
            };
            Vec4 result{};
            switch (op) {
            case 0x00:   // ADD
                result = each([](float x, float y) { return x + y; });
                break;
            case 0x01:   // DP3
                result.fill(dot(3, false));
                break;
            case 0x02:   // DP4
                result.fill(dot(4, false));
                break;
            case 0x03:   // DPH
            case 0x18:   // DPHI
                result.fill(dot(4, true));
                break;
            case 0x05:   // EX2
                result.fill(std::exp2(A[0]));
                break;
            case 0x06:   // LG2
                result.fill(std::log2(A[0]));
                break;
            case 0x08:   // MUL
                result = each([](float x, float y) { return mul(x, y); });
                break;
            case 0x09:   // SGE
            case 0x1a:   // SGEI
                result = each([](float x, float y) { return x >= y ? 1.0f : 0.0f; });
                break;
            case 0x0a:   // SLT
            case 0x1b:   // SLTI
                result = each([](float x, float y) { return x < y ? 1.0f : 0.0f; });
                break;
            case 0x0b:   // FLR
                result = each([](float x, float) { return std::floor(x); });
                break;
            case 0x0c:   // MAX
                result = each([](float x, float y) { return x > y ? x : y; });
                break;
            case 0x0d:   // MIN
                result = each([](float x, float y) { return x < y ? x : y; });
                break;
            case 0x0e:   // RCP
                result.fill(1.0f / A[0]);
                break;
            case 0x0f:   // RSQ
                result.fill(1.0f / std::sqrt(A[0]));
                break;
            case 0x12:   // MOVA: x and y to the address registers, by the write mask
                if (desc & 8) {
                    address_[0] = int(A[0]);
                }
                if (desc & 4) {
                    address_[1] = int(A[1]);
                }
                return;
            case 0x13:   // MOV
                result = A;
                break;
            default:
                throw std::runtime_error("unsupported shader opcode " + std::to_string(op));
            }
            store((word >> 21) & 0x1f, desc, result);
        }

        // The condition of a flow instruction: the reference bits against the flags, combined by
        // OR, AND, or one flag alone.
        bool condition(uint32_t word) const {
            const bool refX = (word >> 25) & 1;
            const bool refY = (word >> 24) & 1;
            switch ((word >> 22) & 3) {
            case 0:
                return refX == flag_[0] || refY == flag_[1];
            case 1:
                return refX == flag_[0] && refY == flag_[1];
            case 2:
                return refX == flag_[0];
            default:
                return refY == flag_[1];
            }
        }

        // Enters a block and returns its first instruction.
        uint32_t enter(Block block) {
            blocks_.push_back(block);
            return block.start;
        }

        // Flow control at `pc`; returns the next instruction, or nothing at END.
        std::optional<uint32_t> flow(uint32_t word, uint32_t op, uint32_t pc) {
            const uint32_t count = word & 0xff;
            const uint32_t target = (word >> 10) & 0xfff;
            const bool uniform = m_.b[(word >> 22) & 15];
            using Kind = Block::Kind;
            switch (op) {
            case 0x21:   // NOP
                return pc + 1;
            case 0x22:   // END
                return std::nullopt;
            case 0x24:   // CALL: count instructions at target, then return
                return enter({Kind::Call, target, target + count, pc + 1});
            case 0x25:   // CALLC
                return condition(word) ? enter({Kind::Call, target, target + count, pc + 1}) : pc + 1;
            case 0x26:   // CALLU
                return uniform ? enter({Kind::Call, target, target + count, pc + 1}) : pc + 1;
            case 0x27:     // IFU
            case 0x28: {   // IFC: the "then" part runs up to target, the "else" part is count
                           // instructions at target; both continue after the else part
                const bool taken = op == 0x27 ? uniform : condition(word);
                return taken ? enter({Kind::If, pc + 1, target, target + count})
                             : enter({Kind::If, target, target + count, target + count});
            }
            case 0x29: {   // LOOP: the body up to target (inclusive) runs (count + 1) times
                const auto& setup = m_.i[(word >> 22) & 3];
                loopCounter_ = setup[1];
                return enter({Kind::Loop, pc + 1, target + 1, target + 1, setup[0], setup[2]});
            }
            case 0x2c:   // JMPC
                return condition(word) ? target : pc + 1;
            case 0x2d:   // JMPU (bit 0 inverts the test)
                return uniform == !(word & 1) ? target : pc + 1;
            default:
                throw std::runtime_error("unsupported shader flow opcode " + std::to_string(op));
            }
        }

        // Copies the output registers to their semantic slots.
        void writeOutputs() {
            for (int reg = 0; reg < 16; reg++) {
                for (int k = 0; k < 4; k++) {
                    if (p_.outputs[reg][k] >= 0) {
                        m_.out[p_.outputs[reg][k]] = output_[reg][k];
                    }
                }
            }
        }
    };

public:
    // Load a program's constants over the uniform file (the DVLE constants win, as uploaded last).
    void constants(const Program& p) {
        for (auto& [reg, value] : p.constants) {
            c[reg] = value;
        }
    }
};

}   // namespace captain_toad::pica
