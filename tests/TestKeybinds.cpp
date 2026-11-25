#include "Test.hpp"
#include "input/Keybinds.hpp"

using namespace spindle::input;

TEST_CASE(keybind_aliases_expand_to_sdl_names) {
    CHECK_EQ(canonicalKeyName("LCtrl"), std::string("Left Ctrl"));
    CHECK_EQ(canonicalKeyName(" rctrl "), std::string("Right Ctrl"));
    CHECK_EQ(canonicalKeyName("Esc"), std::string("Escape"));
    CHECK_EQ(canonicalKeyName("Enter"), std::string("Return"));
    CHECK_EQ(canonicalKeyName("F2"), std::string("F2"));
    CHECK_EQ(canonicalKeyName("Left Ctrl"), std::string("Left Ctrl"));
}

TEST_CASE(keybind_lists_and_buttons) {
    auto keys = keyNames("LCtrl, Space,  A");
    CHECK_EQ(keys.size(), std::size_t{3});
    CHECK_EQ(keys[0], std::string("Left Ctrl"));
    CHECK_EQ(keys[1], std::string("Space"));
    CHECK_EQ(joinNames(keys), std::string("Left Ctrl, Space, A"));
    auto buttons = buttonNames("A, B, LeftShoulder");
    CHECK_EQ(buttons.size(), std::size_t{3});
    CHECK_EQ(buttons[2], std::string("leftshoulder"));
    CHECK(buttonNames("none").empty());
    CHECK(isStationAction(Action::Fire));
    CHECK(!isStationAction(Action::Coin));
}
