// TestFramework.h — tiny dependency-free unit test harness.
//
// Deliberately minimal (no GoogleTest/Catch2 dependency) so Phase 1 has zero
// external test dependencies beyond zlib. If the project grows enough to
// need fixtures/mocking/parameterized tests, swapping in GoogleTest later is
// straightforward since TEST_CASE/CHECK* map naturally onto TEST/EXPECT*.

#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace flash3ds::test {

struct TestFailure {
    std::string message;
};

struct TestCaseEntry {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCaseEntry>& registry() {
    static std::vector<TestCaseEntry> r;
    return r;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

}  // namespace flash3ds::test

#define FLASH3DS_CONCAT_(a, b) a##b
#define FLASH3DS_CONCAT(a, b) FLASH3DS_CONCAT_(a, b)

#define TEST_CASE(name)                                                       \
    static void name();                                                      \
    static ::flash3ds::test::Registrar FLASH3DS_CONCAT(registrar_, name)(     \
        #name, name);                                                        \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            throw ::flash3ds::test::TestFailure{                              \
                std::string("CHECK failed: ") + #cond + " (" + __FILE__ +     \
                ":" + std::to_string(__LINE__) + ")"};                        \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        if (!((a) == (b))) {                                                  \
            throw ::flash3ds::test::TestFailure{                              \
                std::string("CHECK_EQ failed: ") + #a + " != " + #b + " (" +  \
                __FILE__ + ":" + std::to_string(__LINE__) + ")"};             \
        }                                                                     \
    } while (0)
