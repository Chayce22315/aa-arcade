#pragma once

// Minimal self-contained test harness (no third-party dependency).

#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct RequireFailed {};

void fail(const char* file, int line, const std::string& message);

template <class T>
std::string show(const T& v) {
    if constexpr (std::is_arithmetic_v<T> || std::is_convertible_v<const T&, std::string_view>) {
        return std::format("{}", v);
    } else if constexpr (requires { v.begin(); v.end(); std::format("{}", *v.begin()); }) {
        std::string s = "{";
        for (const auto& x : v) s += (s.size() > 1 ? ", " : "") + std::format("{}", x);
        return s + "}";
    } else {
        return "<value>";
    }
}

} // namespace test

#define SPINDLE_CAT2(a, b) a##b
#define SPINDLE_CAT(a, b) SPINDLE_CAT2(a, b)

#define TEST_CASE(name)                                                         \
    static void name();                                                         \
    static const test::Registrar SPINDLE_CAT(name, _registrar)(#name, &name);   \
    static void name()

#define CHECK(expr)                                                             \
    do {                                                                        \
        if (!(expr)) test::fail(__FILE__, __LINE__, "CHECK(" #expr ")");       \
    } while (0)

#define REQUIRE(expr)                                                           \
    do {                                                                        \
        if (!(expr)) {                                                          \
            test::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")");              \
            throw test::RequireFailed{};                                        \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                                                         \
    do {                                                                                                       \
        const auto spindle_a_ = (a);                                                                           \
        const auto spindle_b_ = (b);                                                                           \
        if (!(spindle_a_ == spindle_b_))                                                                       \
            test::fail(__FILE__, __LINE__,                                                                     \
                       std::string("CHECK_EQ(" #a ", " #b "): ") + test::show(spindle_a_) + " != " + test::show(spindle_b_)); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                  \
    do {                                                                                                       \
        const double spindle_a_ = static_cast<double>(a);                                                      \
        const double spindle_b_ = static_cast<double>(b);                                                      \
        if (!(std::fabs(spindle_a_ - spindle_b_) <= (eps)))                                                    \
            test::fail(__FILE__, __LINE__,                                                                     \
                       std::format("CHECK_NEAR(" #a ", " #b "): {} vs {} (eps {})", spindle_a_, spindle_b_, static_cast<double>(eps))); \
    } while (0)
