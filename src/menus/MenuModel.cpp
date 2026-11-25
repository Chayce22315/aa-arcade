#include "menus/MenuModel.hpp"

namespace spindle::menus {

MenuPage::MenuPage(std::string title, std::vector<MenuItem> items) : title_(std::move(title)), items_(std::move(items)) {
    clampSelection();
}

void MenuPage::clampSelection() {
    const int n = static_cast<int>(items_.size());
    if (n == 0) {
        selected_ = 0;
        return;
    }
    if (selected_ < 0 || selected_ >= n) selected_ = 0;
    if (!items_[static_cast<std::size_t>(selected_)].enabled) move(1);
}

bool MenuPage::move(int delta) {
    const int n = static_cast<int>(items_.size());
    if (n == 0) return false;
    const int step = delta < 0 ? -1 : 1;
    int count = delta == 0 ? 0 : (delta < 0 ? -delta : delta);
    int i = selected_;
    if (count == 0 && items_[static_cast<std::size_t>(i)].enabled) return true;
    if (count == 0) count = 1;
    for (int moved = 0; moved < count; ++moved) {
        int tries = 0;
        do {
            i = ((i + step) % n + n) % n;
            ++tries;
        } while (!items_[static_cast<std::size_t>(i)].enabled && tries <= n);
        if (!items_[static_cast<std::size_t>(i)].enabled) return false;
    }
    selected_ = i;
    return true;
}

void MenuPage::select(int index) {
    selected_ = index;
    clampSelection();
}

bool MenuPage::adjust(int dir) {
    if (items_.empty()) return false;
    MenuItem& item = items_[static_cast<std::size_t>(selected_)];
    if (!item.enabled || !item.adjust) return false;
    item.adjust(dir);
    return true;
}

bool MenuPage::activate() {
    if (items_.empty()) return false;
    MenuItem& item = items_[static_cast<std::size_t>(selected_)];
    if (!item.enabled) return false;
    if (item.activate) {
        item.activate();
        return true;
    }
    if (item.adjust) {
        item.adjust(1);
        return true;
    }
    return false;
}

} // namespace spindle::menus
