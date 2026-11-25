#include <string>

#include "Test.hpp"
#include "assets/Sha256.hpp"

using spindle::assets::Sha256;
using spindle::assets::sha256Hex;

TEST_CASE(sha256_known_vectors) {
    CHECK_EQ(sha256Hex(std::string_view("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256Hex(std::string_view("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST_CASE(sha256_million_a_streamed_in_odd_chunks) {
    Sha256 h;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left > 0) {
        const std::size_t n = std::min(left, chunk.size());
        h.update(std::string_view(chunk.data(), n));
        left -= n;
    }
    CHECK_EQ(spindle::assets::toHex(h.finish()), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST_CASE(sha256_padding_boundaries) {
    // 55, 56 and 64 byte messages straddle the length-field boundary.
    CHECK_EQ(sha256Hex(std::string(55, 'x')).size(), std::size_t{64});
    CHECK(sha256Hex(std::string(55, 'x')) != sha256Hex(std::string(56, 'x')));
    CHECK_EQ(sha256Hex(std::string(64, 'a')), std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
}

#include "assets/ControllerDbPin.hpp"

TEST_CASE(controller_db_pin_rejects_anything_else) {
    namespace pin = spindle::assets::controller_db_pin;
    CHECK(!pin::matches(""));
    CHECK(!pin::matches(std::string(static_cast<std::size_t>(pin::kSizeBytes), 'x')));
    CHECK_EQ(pin::kSha256.size(), std::size_t{64});
    CHECK_EQ(pin::kCommit.size(), std::size_t{40});
    CHECK(pin::kUrl.find(pin::kCommit) != std::string_view::npos);
    CHECK(pin::kUrl.substr(0, 8) == "https://");
}
