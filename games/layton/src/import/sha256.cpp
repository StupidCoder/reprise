// SHA-256, expressed with fixed-width unsigned arithmetic. No platform crypto API.
#include "import/sha256.hpp"
#include <bit>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace picarat::import {

namespace {
constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
} // namespace

void Sha256::block(const std::uint8_t* data) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 4; ++j) {
            w[i] = (w[i] << 8) | data[i * 4 + j];
        }
    }
    for (int i = 16; i < 64; ++i) {
        auto a = w[i - 15], b = w[i - 2];
        w[i] = w[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
               (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10));
    }
    auto a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], v = h_[7];
    for (int i = 0; i < 64; ++i) {
        auto t = v + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                 ((e & f) ^ (~e & g)) + k[i] + w[i];
        auto u =
            (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        v = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += v;
}

void Sha256::update(std::span<const std::uint8_t> data) {
    length_ += data.size();
    std::size_t at = 0;
    if (pendingSize_ > 0) {
        const std::size_t take = std::min(data.size(), 64 - pendingSize_);
        std::memcpy(pending_.data() + pendingSize_, data.data(), take);
        pendingSize_ += take;
        at = take;
        if (pendingSize_ < 64) {
            return;
        }
        block(pending_.data());
        pendingSize_ = 0;
    }
    for (; at + 64 <= data.size(); at += 64) {
        block(data.data() + at);
    }
    pendingSize_ = data.size() - at;
    std::memcpy(pending_.data(), data.data() + at, pendingSize_);
}

std::string Sha256::finish() {
    const std::uint64_t bits = length_ * 8;
    std::uint8_t tail[72] = {128};
    const std::size_t padding = (pendingSize_ < 56 ? 56 : 120) - pendingSize_;
    for (int i = 0; i < 8; ++i) {
        tail[padding + std::size_t(i)] = std::uint8_t(bits >> ((7 - i) * 8));
    }
    update(std::span(tail, padding + 8));
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (auto v : h_) {
        result << std::setw(8) << v;
    }
    return result.str();
}

std::string sha256(std::span<const std::uint8_t> input) {
    Sha256 hash;
    hash.update(input);
    return hash.finish();
}

} // namespace picarat::import
