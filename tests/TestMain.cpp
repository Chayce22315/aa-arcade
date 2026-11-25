#include <cstdio>
#include <cstring>
#include <exception>

#include "Test.hpp"
#include "engine/Log.hpp"

namespace test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

namespace {
int g_failures = 0;
}

void fail(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::fprintf(stderr, "    FAIL %s:%d: %s\n", file, line, message.c_str());
}

} // namespace test

int main(int argc, char** argv) {
    spindle::log::setLevel(spindle::log::Level::Error);
    spindle::log::setEchoToStderr(false);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    int failedCases = 0;
    for (const test::Case& c : test::registry()) {
        if (filter && std::strstr(c.name, filter) == nullptr) continue;
        const int before = test::g_failures;
        std::printf("[ RUN  ] %s\n", c.name);
        try {
            c.fn();
        } catch (const test::RequireFailed&) {
        } catch (const std::exception& e) {
            test::fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        ++ran;
        if (test::g_failures != before) {
            ++failedCases;
            std::printf("[ FAIL ] %s\n", c.name);
        } else {
            std::printf("[  OK  ] %s\n", c.name);
        }
    }
    std::printf("\n%d test case(s) run, %d failed, %d failed check(s)\n", ran, failedCases, test::g_failures);
    return failedCases == 0 && ran > 0 ? 0 : 1;
}
