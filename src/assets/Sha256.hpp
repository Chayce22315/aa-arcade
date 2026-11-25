#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace spindle::assets {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> data);
    void update(std::string_view text);
    Sha256Digest finish();

private:
    void block(const std::uint8_t* chunk);

    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferLen_ = 0;
    std::uint64_t totalBytes_ = 0;
    bool finished_ = false;
};

std::string toHex(const Sha256Digest& digest);
std::string sha256Hex(std::span<const std::uint8_t> data);
std::string sha256Hex(std::string_view text);

} // namespace spindle::assets
