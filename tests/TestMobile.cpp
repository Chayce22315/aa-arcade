// Phones and tablets: touch zones, the notch-safe layout, mobile defaults, pausing in the background.

#include <cmath>
#include <limits>
#include <memory>

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
#include "config/IniDocument.hpp"
#include "config/Settings.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"
#include "input/TouchZones.hpp"
#include "render/Layout.hpp"

using namespace spindle;
using input::Action;
using input::TouchContext;
using input::TouchScreen;

namespace {

TouchContext playing(std::array<bool, 4> humans) {
    TouchContext c;
    c.screen = TouchScreen::Playing;
    c.humans = humans;
    return c;
}

} // namespace

TEST_CASE(touch_lobby_tap_is_that_columns_start) {
    TouchContext c; // lobby, four equal columns
    for (int s = 0; s < 4; ++s) {
        const auto hit = input::touchHit(c, 0.125f + 0.25f * static_cast<float>(s), 0.6f);
        REQUIRE(hit.has_value());
        CHECK(hit->action == Action::Start);
        CHECK_EQ(hit->station, s);
    }
    // Pillarboxed screen: taps beside the columns count for the nearest outer column.
    c.columnEdges = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    CHECK_EQ(input::touchHit(c, 0.05f, 0.5f)->station, 0);
    CHECK_EQ(input::touchHit(c, 0.95f, 0.5f)->station, 3);
    CHECK_EQ(input::touchHit(c, 0.31f, 0.5f)->station, 1);
}

TEST_CASE(touch_one_human_fires_from_anywhere) {
    const TouchContext c = playing({false, false, true, false});
    for (float x : {0.1f, 0.4f, 0.6f, 0.99f}) {
        const auto hit = input::touchHit(c, x, 0.7f);
        REQUIRE(hit.has_value());
        CHECK(hit->action == Action::Fire);
        CHECK_EQ(hit->station, 2);
    }
}

TEST_CASE(touch_several_humans_fire_in_their_own_column_only) {
    const TouchContext c = playing({true, false, true, true});
    CHECK_EQ(input::touchHit(c, 0.1f, 0.5f)->station, 0);
    CHECK(!input::touchHit(c, 0.3f, 0.5f).has_value()); // a bot's column
    CHECK_EQ(input::touchHit(c, 0.6f, 0.5f)->station, 2);
    CHECK_EQ(input::touchHit(c, 0.9f, 0.5f)->station, 3);
    CHECK(input::touchHit(c, 0.9f, 0.5f)->action == Action::Fire);
}

TEST_CASE(touch_corner_ignored_and_bad_positions_rejected) {
    TouchContext lobby;
    CHECK(input::inServiceCorner(0.02f, 0.03f));
    CHECK(!input::touchHit(lobby, 0.02f, 0.03f).has_value()); // reserved for the hidden service hold
    CHECK(!input::touchHit(playing({true, false, false, false}), 0.02f, 0.03f).has_value());
    CHECK(input::touchHit(lobby, 0.02f, 0.5f).has_value()); // same column, below the corner
    CHECK(!input::touchHit(lobby, -0.1f, 0.5f).has_value());
    CHECK(!input::touchHit(lobby, 0.5f, 1.5f).has_value());
    CHECK(!input::touchHit(lobby, std::numeric_limits<float>::quiet_NaN(), 0.5f).has_value());
    TouchContext ignored;
    ignored.screen = TouchScreen::Ignore;
    CHECK(!input::touchHit(ignored, 0.5f, 0.5f).has_value());
    CHECK(!input::touchHit(playing({false, false, false, false}), 0.5f, 0.5f).has_value()); // no humans
}

TEST_CASE(layout_fits_the_safe_area) {
    using render::computeLayout;
    // A 2400 x 1080 phone with a 120-pixel notch on the left and 60 pixels of rounded corner on the right.
    const render::ScreenLayout l = computeLayout(2400, 1080, {120.0f, 0.0f, 2220.0f, 1080.0f});
    CHECK_NEAR(l.scale, 1.0, 1e-6);
    CHECK(l.area.x >= 120.0f);
    CHECK(l.area.x + l.area.w <= 2340.0f + 1e-3f);
    CHECK_NEAR(l.area.x + l.area.w * 0.5f, 120.0 + 2220.0 / 2.0, 1e-3); // centred in the safe area
    CHECK_NEAR(l.width, 2400.0, 1e-6);                                  // backgrounds still use the whole screen
    // A safe area that shrinks the height scales the columns down.
    const render::ScreenLayout s = computeLayout(1920, 1080, {0.0f, 40.0f, 1920.0f, 1000.0f});
    CHECK_NEAR(s.scale, 1000.0 / 1080.0, 1e-5);
    CHECK(s.area.y >= 40.0f);
    // Empty, inverted or off-screen safe areas fall back to the whole screen.
    const render::Rect none{}, offScreen{5000.0f, 0.0f, 10.0f, 10.0f}, inverted{0.0f, 0.0f, -5.0f, 100.0f};
    const float noneScale = computeLayout(1920, 1080, none).scale;
    const float offScale = computeLayout(1920, 1080, offScreen).scale;
    const float invScale = computeLayout(1920, 1080, inverted).scale;
    CHECK_NEAR(noneScale, 1.0, 1e-6);
    CHECK_NEAR(offScale, 1.0, 1e-6);
    CHECK_NEAR(invScale, 1.0, 1e-6);
}

TEST_CASE(settings_mobile_defaults_are_free_play) {
    const config::Settings m = config::Settings::mobileDefaults();
    CHECK(m.arcade.freePlay);
    config::Settings d = config::Settings::defaults();
    CHECK(!d.arcade.freePlay);
    d.arcade.freePlay = true;
    CHECK(d == m); // nothing else differs
    // The default file written on a phone loads back as the phone defaults, with no invalid values.
    const auto loaded = config::loadSettings(config::IniDocument::parse(config::defaultSettingsText(m)), m);
    CHECK(loaded.settings == m);
    CHECK_EQ(loaded.invalidCount(), std::size_t{0});
    // A missing key falls back to the platform's default.
    const auto sparse = config::loadSettings(config::IniDocument::parse("[Arcade]\ncredits_per_coin = 2\n"), m);
    CHECK(sparse.settings.arcade.freePlay);
    CHECK_EQ(sparse.settings.arcade.creditsPerCoin, 2);
}

namespace {

arcade::BotFactory factory() {
    return [](config::Difficulty d, std::uint64_t seed) -> std::unique_ptr<bots::BotController> {
        std::string err;
        auto brain = bots::BotBrain::create(bots::BotBrain::embeddedSource(), d, seed, &err);
        if (!brain) return nullptr;
        return std::make_unique<bots::BotController>(std::move(brain));
    };
}

constexpr int kSecond = gameplay::geom::kTickRate;

} // namespace

TEST_CASE(arcade_background_suspends_the_match_until_a_player_presses) {
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::mobileDefaults();
    s.gameplay.seed = 77;
    s.arcade.joinWindow = 3;
    s.bots.singleplayerBots = 3;
    arcade::ArcadeMachine m(s, levels, factory(), {}, 1);
    m.suspend(); // nothing to suspend in attract
    CHECK(!m.suspended());
    arcade::ArcadeInput start;
    start.stations[1].startPressed = true; // free play: a tap joins
    m.tick(start);
    REQUIRE(m.state() == arcade::ArcadeState::Join);
    m.tick(start); // tapping again starts at once
    REQUIRE(m.state() == arcade::ArcadeState::Playing);
    for (int i = 0; i < 3 * kSecond; ++i) m.tick({});
    m.suspend();
    REQUIRE(m.suspended());
    const std::uint64_t frozen = m.match()->tickCount();
    for (int i = 0; i < 2 * kSecond; ++i) m.tick({});
    CHECK_EQ(m.match()->tickCount(), frozen);
    // A bot's station pressing does not wake it; the human's fire does, without launching a needle.
    arcade::ArcadeInput botPress;
    botPress.stations[0].firePresses = 1;
    m.tick(botPress);
    CHECK(m.suspended());
    const int launchedBefore = m.match()->field(1).needlesLeft();
    arcade::ArcadeInput fire;
    fire.stations[1].firePresses = 1;
    m.tick(fire);
    CHECK(!m.suspended());
    CHECK_EQ(m.match()->field(1).needlesLeft(), launchedBefore);
    for (int i = 0; i < kSecond; ++i) m.tick({});
    CHECK(m.match()->tickCount() > frozen);
}
