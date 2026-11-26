#include "menus/Screens.hpp"

#include <algorithm>
#include <cmath>

#include "input/TouchZones.hpp"
#include "menus/screens/Common.hpp"

// Each screen lives in src/menus/screens/; this only picks the one for the arcade state.
namespace spindle::menus {
namespace {
constexpr double kIrisSeconds = 0.35;

// Phones: the top-right corner holds a button (the gear on attract, pause in a solo match).
void cornerButton(const ScreenContext& c, bool gear) {
    using namespace screens;
    const Vec2 o{c.layout.width * (1.0f - input::kButtonCornerWidth * 0.5f), c.layout.height * input::kButtonCornerHeight * 0.5f};
    const float r = std::min(c.layout.width * input::kButtonCornerWidth, c.layout.height * input::kButtonCornerHeight) * 0.28f;
    const float t = std::max(2.0f, r * 0.14f);
    if (gear) {
        c.r.ring(o, r * 0.62f, t, colors::kDim);
        c.r.ring(o, r * 0.22f, t, colors::kDim);
        for (int i = 0; i < 8; ++i) {
            const float a = kTau * static_cast<float>(i) / 8.0f;
            c.r.line({o.x + std::cos(a) * r * 0.62f, o.y + std::sin(a) * r * 0.62f}, {o.x + std::cos(a) * r, o.y + std::sin(a) * r}, t * 1.6f, colors::kDim);
        }
    } else {
        c.r.ring(o, r, t, colors::kDim);
        c.r.fillRect({o.x - r * 0.38f, o.y - r * 0.45f, r * 0.25f, r * 0.9f}, colors::kDim);
        c.r.fillRect({o.x + r * 0.13f, o.y - r * 0.45f, r * 0.25f, r * 0.9f}, colors::kDim);
    }
}

bool soloPauseAvailable(const arcade::ArcadeMachine& a) {
    const gameplay::Match* m = a.match();
    if (a.state() != arcade::ArcadeState::Playing || a.suspended() || !m) return false;
    int humans = 0;
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (a.lineup().seats[static_cast<std::size_t>(s)] == players::SeatKind::Human && m->participant(s) && !m->participant(s)->eliminated) ++humans;
    return humans == 1;
}
} // namespace

void drawArcade(const ScreenContext& c) {
    using namespace screens;
    const auto& a = c.arcade;
    switch (a.state()) {
    case arcade::ArcadeState::Attract:
        switch (a.attractPage()) {
        case arcade::AttractPage::Title: drawTitle(c); break;
        case arcade::AttractPage::WhatsNew: drawWhatsNew(c); break;
        case arcade::AttractPage::HowTo: drawHowTo(c); break;
        case arcade::AttractPage::Demo:
            dividers(c);
            if (const gameplay::Match* m = a.match()) drawMatch(c, *m, true);
            break;
        case arcade::AttractPage::HighScores: drawHighScores(c); break;
        case arcade::AttractPage::Stats: drawStats(c); break;
        }
        break;
    case arcade::ArcadeState::InsertCoin: drawInsertCoin(c); break;
    case arcade::ArcadeState::Vote: drawVote(c); break;
    case arcade::ArcadeState::Playing:
        dividers(c);
        if (const gameplay::Match* m = a.match()) drawMatch(c, *m, false);
        if (a.pausedForDisconnect())
            drawDisconnectOverlay(c);
        else if (a.suspended())
            drawSuspendedOverlay(c);
        break;
    case arcade::ArcadeState::Initials:
        dividers(c);
        drawInitialsState(c);
        break;
    case arcade::ArcadeState::GameOver: drawGameOver(c); break;
    }
    drawAchievementToasts(c);
    creditLine(c);
    exitHint(c);
    if (c.touchUi) {
        if (a.state() == arcade::ArcadeState::Attract || a.state() == arcade::ArcadeState::InsertCoin) cornerButton(c, true);
        if (soloPauseAvailable(a)) cornerButton(c, false);
    }
    // Every new screen opens with a quick iris out of black.
    const double since = a.state() == arcade::ArcadeState::Attract ? a.pageTime() : a.stateTime();
    if (since < kIrisSeconds) drawIris(c, static_cast<float>(since / kIrisSeconds));
}

} // namespace spindle::menus
