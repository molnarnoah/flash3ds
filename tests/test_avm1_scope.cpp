#include "TestFramework.h"
#include "avm1/Scope.h"

using namespace flash3ds::avm1;

TEST_CASE(Scope_TopLevel_GetSetOnSingleObject) {
    auto global = std::make_shared<Object>();
    Scope scope = Scope::topLevel(global);

    CHECK(scope.getVariable("x").isUndefined());
    scope.setVariable("x", Value::number(5));
    CHECK_EQ(scope.getVariable("x").toNumber(), 5.0);
    CHECK(global->hasOwnProperty("x"));
}

TEST_CASE(Scope_Pushed_InnerShadowsOuter) {
    auto global = std::make_shared<Object>();
    global->setOwnProperty("x", Value::number(1));
    Scope outer = Scope::topLevel(global);

    auto frame = std::make_shared<Object>();
    Scope inner = outer.pushed(frame);

    // Not shadowed yet — falls through to the outer object.
    CHECK_EQ(inner.getVariable("x").toNumber(), 1.0);

    // DefineLocal always creates on the innermost frame, shadowing outer.
    inner.defineLocal("x", Value::number(2));
    CHECK_EQ(inner.getVariable("x").toNumber(), 2.0);
    CHECK_EQ(global->getOwnProperty("x").toNumber(), 1.0);  // outer untouched
}

TEST_CASE(Scope_SetVariable_UpdatesExistingBindingWherever) {
    auto global = std::make_shared<Object>();
    global->setOwnProperty("x", Value::number(1));
    Scope outer = Scope::topLevel(global);
    auto frame = std::make_shared<Object>();
    Scope inner = outer.pushed(frame);

    // x isn't local yet — SetVariable should update the OUTER binding, not
    // create a new local one.
    inner.setVariable("x", Value::number(99));
    CHECK_EQ(global->getOwnProperty("x").toNumber(), 99.0);
    CHECK(!frame->hasOwnProperty("x"));
}

TEST_CASE(Scope_SetVariable_CreatesLocalIfNotFoundAnywhere) {
    auto global = std::make_shared<Object>();
    Scope outer = Scope::topLevel(global);
    auto frame = std::make_shared<Object>();
    Scope inner = outer.pushed(frame);

    inner.setVariable("newVar", Value::string("hi"));
    CHECK(frame->hasOwnProperty("newVar"));
    CHECK(!global->hasOwnProperty("newVar"));
}

TEST_CASE(Scope_DeleteVariable_RemovesFromOwningScope) {
    auto global = std::make_shared<Object>();
    global->setOwnProperty("x", Value::number(1));
    Scope outer = Scope::topLevel(global);
    auto frame = std::make_shared<Object>();
    Scope inner = outer.pushed(frame);

    CHECK(inner.deleteVariable("x"));
    CHECK(!global->hasOwnProperty("x"));
    CHECK(!inner.deleteVariable("neverExisted"));
}

TEST_CASE(Scope_Innermost_ReturnsFrontOfChain) {
    auto global = std::make_shared<Object>();
    Scope outer = Scope::topLevel(global);
    auto frame = std::make_shared<Object>();
    Scope inner = outer.pushed(frame);
    CHECK(inner.innermost() == frame);
    CHECK(outer.innermost() == global);
}
