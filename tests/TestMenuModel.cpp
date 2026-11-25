#include "Test.hpp"
#include "menus/MenuModel.hpp"

using namespace spindle::menus;

TEST_CASE(menu_navigation_wraps_and_skips_disabled) {
    int value = 0;
    int activated = 0;
    std::vector<MenuItem> items(4);
    items[0].label = "a";
    items[1].label = "b";
    items[1].enabled = false;
    items[2].label = "c";
    items[2].adjust = [&](int d) { value += d; };
    items[3].label = "d";
    items[3].activate = [&] { ++activated; };
    MenuPage page("test", std::move(items));
    CHECK_EQ(page.selected(), 0);
    page.move(1);
    CHECK_EQ(page.selected(), 2); // skipped disabled b
    CHECK(page.adjust(-1));
    CHECK_EQ(value, -1);
    CHECK(page.activate()); // activate falls back to adjust(+1)
    CHECK_EQ(value, 0);
    page.move(1);
    CHECK(page.activate());
    CHECK_EQ(activated, 1);
    page.move(1);
    CHECK_EQ(page.selected(), 0); // wrapped
    page.move(-1);
    CHECK_EQ(page.selected(), 3);
    CHECK(!page.adjust(1)); // d has no adjust
}

TEST_CASE(menu_all_disabled_and_empty_pages_are_safe) {
    std::vector<MenuItem> items(2);
    items[0].enabled = false;
    items[1].enabled = false;
    MenuPage page("x", std::move(items));
    CHECK(!page.move(1));
    CHECK(!page.activate());
    MenuPage empty;
    CHECK(!empty.move(1));
    CHECK(!empty.activate());
    CHECK(!empty.adjust(1));
}
