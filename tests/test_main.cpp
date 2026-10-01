#include "test_framework.h"
int main() {
    int failedCases = 0;
    for (auto& c : osmptest::cases()) {
        int before = osmptest::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (osmptest::failures() != before) { failedCases++; std::printf("[ FAIL ] %s\n", c.name); }
        else std::printf("[  OK  ] %s\n", c.name);
    }
    std::printf("%zu cases, %d checks, %d failed checks, %d failed cases\n",
                osmptest::cases().size(), osmptest::checks(), osmptest::failures(), failedCases);
    return failedCases ? 1 : 0;
}
