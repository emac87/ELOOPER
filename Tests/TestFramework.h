#pragma once
#include <cstdio>
#include <cmath>
#include <vector>

namespace el::test {
struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& currentFailures() { static int f = 0; return f; }
struct Registrar { Registrar (const char* n, void (*f)()) { registry().push_back ({ n, f }); } };
}

#define EL_TEST(name) \
    static void name(); \
    static el::test::Registrar name##_registrar (#name, &name); \
    static void name()

#define EL_CHECK(cond) do { if (!(cond)) { std::printf ("  %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); ++el::test::currentFailures(); } } while (0)
#define EL_CHECK_NEAR(a, b, eps) do { const double _a = (a), _b = (b); if (std::abs (_a - _b) > (eps)) { std::printf ("  %s:%d CHECK_NEAR(%s, %s): %g vs %g\n", __FILE__, __LINE__, #a, #b, _a, _b); ++el::test::currentFailures(); } } while (0)
