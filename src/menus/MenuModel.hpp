#pragma once

#include <functional>
#include <string>
#include <vector>

namespace spindle::menus {

// One row of a service-menu page. adjust(dir) handles left/right (dir = -1 / +1), activate() handles
// select. value() renders the current value; either callback may be empty.
struct MenuItem {
    std::string label;
    std::function<std::string()> value;
    std::function<void(int)> adjust;
    std::function<void()> activate;
    bool enabled = true;
    bool separatorBefore = false;
};

class MenuPage {
public:
    MenuPage() = default;
    MenuPage(std::string title, std::vector<MenuItem> items);

    const std::string& title() const { return title_; }
    const std::vector<MenuItem>& items() const { return items_; }
    std::vector<MenuItem>& items() { return items_; }
    int selected() const { return selected_; }

    // Moves the cursor with wrap-around, skipping disabled rows. Returns false if nothing is selectable.
    bool move(int delta);
    void select(int index);
    bool adjust(int dir);
    bool activate();

private:
    void clampSelection();

    std::string title_;
    std::vector<MenuItem> items_;
    int selected_ = 0;
};

} // namespace spindle::menus
