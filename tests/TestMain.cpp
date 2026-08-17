#include <cstdio>

#include "TestFramework.h"
#include "platform/Log.h"

int main() {
    // Keep test output focused on PASS/FAIL lines; individual tests can
    // still call flash3ds::Log::setLevel(...) locally if they want to
    // assert on log output.
    flash3ds::Log::setLevel(flash3ds::LogLevel::kNone);

    int passed = 0;
    int failed = 0;

    for (auto& tc : flash3ds::test::registry()) {
        try {
            tc.fn();
            std::printf("[PASS] %s\n", tc.name.c_str());
            ++passed;
        } catch (const flash3ds::test::TestFailure& f) {
            std::printf("[FAIL] %s -- %s\n", tc.name.c_str(), f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s -- unexpected exception: %s\n", tc.name.c_str(), e.what());
            ++failed;
        } catch (...) {
            std::printf("[FAIL] %s -- unknown exception\n", tc.name.c_str());
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}
