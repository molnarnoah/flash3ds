#include <cmath>
#include <limits>
#include <memory>

#include "TestFramework.h"
#include "avm1/Value.h"

using namespace flash3ds::avm1;

TEST_CASE(Value_ToBoolean_Coercions) {
    CHECK(!Value::undefined().toBoolean());
    CHECK(!Value::null().toBoolean());
    CHECK(!Value::number(0).toBoolean());
    CHECK(!Value::number(std::nan("")).toBoolean());
    CHECK(Value::number(1).toBoolean());
    CHECK(!Value::string("").toBoolean());
    CHECK(Value::string("x").toBoolean());
    CHECK(Value::object(std::make_shared<Object>()).toBoolean());
}

TEST_CASE(Value_ToNumber_Coercions) {
    CHECK(std::isnan(Value::undefined().toNumber()));
    CHECK_EQ(Value::null().toNumber(), 0.0);
    CHECK_EQ(Value::boolean(true).toNumber(), 1.0);
    CHECK_EQ(Value::boolean(false).toNumber(), 0.0);
    CHECK_EQ(Value::string("42").toNumber(), 42.0);
    CHECK_EQ(Value::string("  3.5  ").toNumber(), 3.5);
    CHECK_EQ(Value::string("").toNumber(), 0.0);
    CHECK(std::isnan(Value::string("abc").toNumber()));
    CHECK(std::isnan(Value::string("12abc").toNumber()));
    CHECK(std::isnan(Value::object(std::make_shared<Object>()).toNumber()));
}

TEST_CASE(Value_ToInt32_WrapsAndTruncates) {
    CHECK_EQ(Value::number(3.9).toInt32(), 3);
    CHECK_EQ(Value::number(-3.9).toInt32(), -3);
    CHECK_EQ(Value::number(std::nan("")).toInt32(), 0);
    CHECK_EQ(Value::number(std::numeric_limits<double>::infinity()).toInt32(), 0);
}

TEST_CASE(Value_ToString_Coercions) {
    CHECK_EQ(Value::undefined().toString(), "undefined");
    CHECK_EQ(Value::null().toString(), "null");
    CHECK_EQ(Value::boolean(true).toString(), "true");
    CHECK_EQ(Value::boolean(false).toString(), "false");
    CHECK_EQ(Value::number(42).toString(), "42");
    CHECK_EQ(Value::number(0).toString(), "0");
    CHECK_EQ(Value::string("hi").toString(), "hi");
}

TEST_CASE(NumberToAs2String_SpecialValues) {
    CHECK_EQ(numberToAs2String(std::nan("")), "NaN");
    CHECK_EQ(numberToAs2String(std::numeric_limits<double>::infinity()), "Infinity");
    CHECK_EQ(numberToAs2String(-std::numeric_limits<double>::infinity()), "-Infinity");
    CHECK_EQ(numberToAs2String(0.0), "0");
    CHECK_EQ(numberToAs2String(-0.0), "0");
    CHECK_EQ(numberToAs2String(3.5), "3.5");
}

TEST_CASE(Object_OwnProperty_SetGetHasDelete) {
    auto obj = std::make_shared<Object>();
    CHECK(!obj->hasOwnProperty("x"));
    obj->setOwnProperty("x", Value::number(5));
    CHECK(obj->hasOwnProperty("x"));
    CHECK_EQ(obj->getOwnProperty("x").toNumber(), 5.0);
    obj->deleteOwnProperty("x");
    CHECK(!obj->hasOwnProperty("x"));
}

TEST_CASE(Object_GetMember_PrototypeChainFallback) {
    auto proto = std::make_shared<Object>();
    proto->setOwnProperty("greeting", Value::string("hi"));
    auto obj = std::make_shared<Object>();
    obj->prototype = proto;

    CHECK_EQ(obj->getMember("greeting").toString(), "hi");
    CHECK(obj->getMember("missing").isUndefined());

    // Own property shadows the prototype's.
    obj->setOwnProperty("greeting", Value::string("own"));
    CHECK_EQ(obj->getMember("greeting").toString(), "own");
}

TEST_CASE(Object_GetMember_CyclicPrototypeDoesNotHang) {
    auto a = std::make_shared<Object>();
    auto b = std::make_shared<Object>();
    a->prototype = b;
    b->prototype = a;  // cycle
    // Should terminate (bounded depth) rather than looping forever.
    CHECK(a->getMember("nonexistent").isUndefined());
}

TEST_CASE(Object_Array_LengthAndIndexAccess) {
    auto arr = std::make_shared<Object>(Object::Kind::kArray);
    CHECK_EQ(arr->getMember("length").toNumber(), 0.0);

    arr->setMember("0", Value::string("a"));
    arr->setMember("2", Value::string("c"));
    CHECK_EQ(arr->getMember("length").toNumber(), 3.0);
    CHECK_EQ(arr->getMember("0").toString(), "a");
    CHECK(arr->getMember("1").isUndefined());
    CHECK_EQ(arr->getMember("2").toString(), "c");

    arr->setMember("length", Value::number(1));
    CHECK_EQ(arr->elements.size(), static_cast<size_t>(1));
}

TEST_CASE(Value_ArrayToString_JoinsWithCommas) {
    auto arr = std::make_shared<Object>(Object::Kind::kArray);
    arr->elements = {Value::number(1), Value::string("a"), Value::undefined(), Value::number(3)};
    Value v = Value::object(arr);
    CHECK_EQ(v.toString(), "1,a,,3");
}
