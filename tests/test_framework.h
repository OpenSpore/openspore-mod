#pragma once
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

namespace osmptest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, f}); } };
}
#define TEST(name) static void name(); static osmptest::Reg name##_reg(#name, name); static void name()
#define CHECK(cond) do { osmptest::checks()++; if (!(cond)) { osmptest::failures()++; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { osmptest::checks()++; if (!((a) == (b))) { osmptest::failures()++; std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { osmptest::checks()++; if (std::fabs((double)(a) - (double)(b)) > (eps)) { osmptest::failures()++; std::printf("  FAIL %s:%d: %s ~= %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); } } while (0)
