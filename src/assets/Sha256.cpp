#include "assets/Sha256.hpp"

#include <cstring>

namespace spindle::assets {
namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

} // namespace

Sha256::Sha256() {
    h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
}

void Sha256::block(const std::uint8_t* chunk) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t{chunk[i * 4]} << 24) | (std::uint32_t{chunk[i * 4 + 1]} << 16) |
               (std::uint32_t{chunk[i * 4 + 2]} << 8) | std::uint32_t{chunk[i * 4 + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) {
    if (finished_) return;
    totalBytes_ += data.size();
    std::size_t offset = 0;
    if (bufferLen_ > 0) {
        const std::size_t take = std::min(data.size(), buffer_.size() - bufferLen_);
        std::memcpy(buffer_.data() + bufferLen_, data.data(), take);
        bufferLen_ += take;
        offset = take;
        if (bufferLen_ == buffer_.size()) {
            block(buffer_.data());
            bufferLen_ = 0;
        }
    }
    while (data.size() - offset >= 64) {
        block(data.data() + offset);
        offset += 64;
    }
    if (offset < data.size()) {
        std::memcpy(buffer_.data(), data.data() + offset, data.size() - offset);
        bufferLen_ = data.size() - offset;
    }
}

void Sha256::update(std::string_view text) {
    update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

Sha256Digest Sha256::finish() {
    if (!finished_) {
        const std::uint64_t bitLen = totalBytes_ * 8u;
        std::array<std::uint8_t, 72> pad{};
        pad[0] = 0x80;
        const std::size_t padLen = (bufferLen_ < 56) ? (56 - bufferLen_) : (120 - bufferLen_);
        std::array<std::uint8_t, 8> len{};
        for (std::size_t i = 0; i < 8; ++i) len[7 - i] = static_cast<std::uint8_t>(bitLen >> (8 * i));
        const std::uint64_t savedTotal = totalBytes_;
        update(std::span<const std::uint8_t>(pad.data(), padLen));
        update(std::span<const std::uint8_t>(len.data(), len.size()));
        totalBytes_ = savedTotal;
        finished_ = true;
    }
    Sha256Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h_[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h_[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h_[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h_[i]);
    }
    return out;
}

std::string toHex(const Sha256Digest& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (std::uint8_t b : digest) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0f]);
    }
    return s;
}

std::string sha256Hex(std::span<const std::uint8_t> data) {
    Sha256 h;
    h.update(data);
    return toHex(h.finish());
}

std::string sha256Hex(std::string_view text) {
    Sha256 h;
    h.update(text);
    return toHex(h.finish());
}

} // namespace spindle::assets
