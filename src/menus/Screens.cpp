#include "menus/Screens.hpp"

#include "menus/screens/Common.hpp"

// Each screen lives in src/menus/screens/; this only picks the one for the arcade state.
namespace spindle::menus {

void drawArcade(const ScreenContext& c) {
    using namespace screens;
    const auto& a = c.arcade;
    switch (a.state()) {
    case arcade::ArcadeState::Attract:
        switch (a.attractPage()) {
        case arcade::AttractPage::Title: drawTitle(c); break;
        case arcade::AttractPage::HowTo: drawHowTo(c); break;
        case arcade::AttractPage::Demo:
            dividers(c);
            if (const gameplay::Match* m = a.match()) drawMatch(c, *m, true);
            break;
        case arcade::AttractPage::HighScores: drawHighScores(c); break;
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
    creditLine(c);
    exitHint(c);
}

} // namespace spindle::menus
