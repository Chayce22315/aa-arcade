#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace spindle::assets {

struct PngInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0; // 6 = RGBA
};

// Checks the PNG signature and the IHDR chunk (length, type and CRC) and returns the header.
// Pure: used at boot to confirm an embedded image is intact before the display exists. It does not
// decode the pixel data; SDL_LoadPNG_IO does that when the texture is created.
std::optional<PngInfo> readPngInfo(std::span<const unsigned char> data);

// CRC-32 (ISO-HDLC, the one PNG and zlib use).
std::uint32_t crc32(std::span<const unsigned char> data);

} // namespace spindle::assets
