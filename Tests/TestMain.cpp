// Minimal self-contained test runner (no external dependencies).
#include "TestFramework.h"
#include <cstdio>

int main() {
    int failed = 0, passed = 0;
    for (auto& t : el::test::registry()) {
        el::test::currentFailures() = 0;
        t.fn();
        if (el::test::currentFailures() == 0) { ++passed; }
        else { ++failed; std::printf ("FAILED: %s\n", t.name); }
    }
    std::printf ("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
