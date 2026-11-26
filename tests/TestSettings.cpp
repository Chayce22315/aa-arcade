#include "Test.hpp"
#include "config/Settings.hpp"

using namespace spindle::config;

TEST_CASE(settings_default_file_loads_cleanly_and_equals_defaults) {
    const std::string text = defaultSettingsText();
    auto doc = IniDocument::parse(text);
    auto loaded = loadSettings(doc);
    CHECK_EQ(loaded.issues.size(), std::size_t{0});
    CHECK(loaded.settings == Settings::defaults());
    CHECK(text.find("[Keys.Player4]") != std::string::npos);
    CHECK(text.find("tries = 3") != std::string::npos);
}

TEST_CASE(settings_missing_file_means_defaults_with_missing_issues) {
    auto loaded = loadSettings(IniDocument::parse(""));
    CHECK(loaded.settings == Settings::defaults());
    CHECK(loaded.issues.size() > 40);
    CHECK_EQ(loaded.invalidCount(), std::size_t{0});
}

TEST_CASE(settings_invalid_values_fall_back_and_are_reported) {
    auto doc = IniDocument::parse(
        "[Display]\nresolution = huge\nfullscreen = 1\nvsync = maybe\n"
        "[Gameplay]\ntries = 0\nrotation_speed_scale = 1.5\nneedle_speed_scale = nan\nseed = -4\n"
        "[Bots]\nsingleplayer_bots = 7\ndifficulty = Expert\n"
        "[Arcade]\ncredits_per_coin = 2\nfree_play = yes\n"
        "[Keys.Player1]\nfire = \n");
    auto loaded = loadSettings(doc);
    const Settings& s = loaded.settings;
    CHECK(s.display.resolution.automatic);                       // invalid -> default
    CHECK(s.display.fullscreen == FullscreenMode::Borderless);   // alias "1"
    CHECK(s.display.vsync);                                      // invalid -> default
    CHECK_EQ(s.gameplay.tries, 3);                               // out of range -> default
    CHECK_NEAR(s.gameplay.rotationSpeedScale, 1.5, 1e-12);
    CHECK_NEAR(s.gameplay.needleSpeedScale, 1.0, 1e-12);         // nan rejected
    CHECK_EQ(s.gameplay.seed, 0u);
    CHECK_EQ(s.bots.singleplayerBots, 3);
    CHECK(s.bots.difficulty == Difficulty::Expert);              // case-insensitive
    CHECK_EQ(s.arcade.creditsPerCoin, 2);
    CHECK(s.arcade.freePlay);
    CHECK_EQ(s.stationKeys[0].fire, std::string("LCtrl"));        // empty -> default
    CHECK_EQ(loaded.invalidCount(), std::size_t{7});
}

TEST_CASE(settings_store_keeps_comments_and_round_trips) {
    auto doc = IniDocument::parse("; operator note\n[Gameplay]\n; my tries\ntries = 5\n");
    auto loaded = loadSettings(doc);
    CHECK_EQ(loaded.settings.gameplay.tries, 5);
    Settings s = loaded.settings;
    s.gameplay.tries = 2;
    s.display.resolution = Resolution{false, 1280, 720};
    s.bots.difficulty = Difficulty::Hard;
    s.input.gamepadGuid[2] = "03000000de2800000112000001000000";
    storeSettings(s, doc);
    const std::string out = doc.serialize();
    CHECK(out.rfind("; operator note\n[Gameplay]\n; my tries\ntries = 2\n", 0) == 0);
    auto reloaded = loadSettings(IniDocument::parse(out));
    CHECK(reloaded.settings == s);
    CHECK_EQ(reloaded.invalidCount(), std::size_t{0});
}

TEST_CASE(settings_resolution_and_list_parsing) {
    CHECK(parseResolution("auto").value().automatic);
    auto r = parseResolution(" 2560X1440 ");
    CHECK(r && !r->automatic && r->width == 2560 && r->height == 1440);
    CHECK(!parseResolution("100x100"));
    CHECK(!parseResolution("1920x"));
    CHECK_EQ(formatResolution(Resolution{false, 1920, 1080}), std::string("1920x1080"));
    auto parts = splitList(" a, b ,,c ");
    CHECK_EQ(parts.size(), std::size_t{3});
    CHECK_EQ(parts[1], std::string("b"));
}

TEST_CASE(settings_powerup_types_validated_and_normalised) {
    auto ok = loadSettings(IniDocument::parse("[Gameplay]\npowerups = 1\npowerup_types = Mend,shield\n"));
    CHECK_EQ(ok.settings.gameplay.powerupTypes, std::string("shield, mend"));
    CHECK_EQ(ok.invalidCount(), std::size_t{0});
    auto bad = loadSettings(IniDocument::parse("[Gameplay]\npowerup_types = shield, rocket\n"));
    CHECK_EQ(bad.settings.gameplay.powerupTypes, Settings::defaults().gameplay.powerupTypes);
    CHECK_EQ(bad.invalidCount(), std::size_t{1});
    auto none = loadSettings(IniDocument::parse("[Gameplay]\npowerups = 0\npowerup_types = none\n"));
    CHECK(!none.settings.gameplay.powerups);
    CHECK_EQ(none.settings.gameplay.powerupTypes, std::string("none"));
}

TEST_CASE(settings_real_numbers_are_plain_decimals_in_any_locale) {
    const auto scale = [](const char* text) {
        auto loaded = loadSettings(IniDocument::parse(std::string("[Gameplay]\nrotation_speed_scale = ") + text + "\n"));
        return std::pair<double, std::size_t>{loaded.settings.gameplay.rotationSpeedScale, loaded.invalidCount()};
    };
    CHECK_NEAR(scale("1.25").first, 1.25, 1e-12);
    CHECK_NEAR(scale(" +1.5 ").first, 1.5, 1e-12);
    CHECK_NEAR(scale(".5").first, 0.5, 1e-12);
    CHECK_NEAR(scale("2.").first, 2.0, 1e-12);
    CHECK_NEAR(scale("15e-1").first, 1.5, 1e-12);
    CHECK_EQ(scale("1.25").second, std::size_t{0});
    for (const char* bad : {"1,5", "inf", "nan", "0x1p0", "-", ".", "1.2.3", "1e", "1.5x", "e5"}) {
        const auto [value, invalid] = scale(bad);
        CHECK_NEAR(value, 1.0, 1e-12); // the default
        CHECK_EQ(invalid, std::size_t{1});
    }
}
