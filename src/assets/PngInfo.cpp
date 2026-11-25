#include "assets/PngInfo.hpp"

#include <array>

namespace spindle::assets {

namespace {

constexpr std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}

constexpr auto kCrcTable = makeCrcTable();

std::uint32_t readBe32(std::span<const unsigned char> d, std::size_t at) {
    return (static_cast<std::uint32_t>(d[at]) << 24) | (static_cast<std::uint32_t>(d[at + 1]) << 16) |
           (static_cast<std::uint32_t>(d[at + 2]) << 8) | static_cast<std::uint32_t>(d[at + 3]);
}

} // namespace

std::uint32_t crc32(std::span<const unsigned char> data) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = kCrcTable[(c ^ b) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::optional<PngInfo> readPngInfo(std::span<const unsigned char> data) {
    static constexpr unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    // signature (8) + IHDR length (4) + type (4) + body (13) + CRC (4)
    constexpr std::size_t kHeaderBytes = 8 + 4 + 4 + 13 + 4;
    if (data.size() < kHeaderBytes) return std::nullopt;
    for (std::size_t i = 0; i < 8; ++i)
        if (data[i] != kSignature[i]) return std::nullopt;
    if (readBe32(data, 8) != 13) return std::nullopt;
    if (data[12] != 'I' || data[13] != 'H' || data[14] != 'D' || data[15] != 'R') return std::nullopt;
    if (crc32(data.subspan(12, 4 + 13)) != readBe32(data, 29)) return std::nullopt;
    PngInfo info;
    info.width = readBe32(data, 16);
    info.height = readBe32(data, 20);
    info.bitDepth = data[24];
    info.colorType = data[25];
    if (info.width == 0 || info.height == 0 || info.width > 16384 || info.height > 16384) return std::nullopt;
    return info;
}

} // namespace spindle::assets
