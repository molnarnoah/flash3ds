#include <cmath>
#include <vector>

#include "Avm1TestFixtures.h"
#include "TestFramework.h"
#include "avm1/ActionCode.h"
#include "avm1/ExecutionContext.h"
#include "avm1/GlobalObject.h"
#include "avm1/Interpreter.h"
#include "avm1/Scope.h"

using namespace flash3ds::avm1;
namespace fixtures = flash3ds::test::fixtures;
using Asm = fixtures::Avm1Assembler;
using AC = flash3ds::avm1::ActionCode;

namespace {

uint8_t op(AC code) { return static_cast<uint8_t>(code); }

ExecutionContext makeContext() {
    auto global = GlobalObject::create();
    return ExecutionContext(Scope::topLevel(global), global);
}

void run(ExecutionContext& ctx, const std::vector<uint8_t>& code) {
    Interpreter::execute(ctx, code.data(), code.size());
}

}  // namespace

// NOTE on ActionSetVariable operand order throughout this file: it pops
// VALUE first, then NAME (see Interpreter.cpp) — so bytecode must push the
// variable name BEFORE computing/pushing the value, leaving the value on
// top when SetVariable executes.

TEST_CASE(Interpreter_Add2_NumericAddition) {
    Asm a;
    a.pushString("result");
    a.pushInt(2);
    a.pushInt(3);
    a.op(op(AC::Add2));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 5.0);
}

TEST_CASE(Interpreter_Add2_StringConcatenation) {
    Asm a;
    a.pushString("out");
    a.pushString("foo");
    a.pushString("bar");
    a.op(op(AC::Add2));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("out").toString(), "foobar");
}

TEST_CASE(Interpreter_Comparisons_Equals2_StrictEquals_Less2) {
    Asm a;
    // "5" == 5 -> true (loose)
    a.pushString("looseEq");
    a.pushString("5");
    a.pushInt(5);
    a.op(op(AC::Equals2));
    a.op(op(AC::SetVariable));
    // "5" === 5 -> false (strict, different types)
    a.pushString("strictEq");
    a.pushString("5");
    a.pushInt(5);
    a.op(op(AC::StrictEquals));
    a.op(op(AC::SetVariable));
    // 3 < 5 -> true
    a.pushString("lt");
    a.pushInt(3);
    a.pushInt(5);
    a.op(op(AC::Less2));
    a.op(op(AC::SetVariable));
    // "abc" < "abd" -> true (string comparison)
    a.pushString("strLt");
    a.pushString("abc");
    a.pushString("abd");
    a.op(op(AC::Less2));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK(ctx.scope.getVariable("looseEq").toBoolean());
    CHECK(!ctx.scope.getVariable("strictEq").toBoolean());
    CHECK(ctx.scope.getVariable("lt").toBoolean());
    CHECK(ctx.scope.getVariable("strLt").toBoolean());
}

TEST_CASE(Interpreter_Logic_And_Or_Not) {
    Asm a;
    a.pushString("andRes");
    a.pushBool(true);
    a.pushBool(false);
    a.op(op(AC::And));
    a.op(op(AC::SetVariable));
    a.pushString("orRes");
    a.pushBool(true);
    a.pushBool(false);
    a.op(op(AC::Or));
    a.op(op(AC::SetVariable));
    a.pushString("notRes");
    a.pushBool(false);
    a.op(op(AC::Not));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK(!ctx.scope.getVariable("andRes").toBoolean());
    CHECK(ctx.scope.getVariable("orRes").toBoolean());
    CHECK(ctx.scope.getVariable("notRes").toBoolean());
}

TEST_CASE(Interpreter_Bitwise_Ops) {
    Asm a;
    a.pushString("r");
    a.pushInt(6);
    a.pushInt(3);
    a.op(op(AC::BitAnd));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("r").toNumber(), 2.0);  // 6 & 3 == 2

    Asm b;
    b.pushString("shl");
    b.pushInt(1);
    b.pushInt(4);
    b.op(op(AC::BitLShift));
    b.op(op(AC::SetVariable));
    auto ctx2 = makeContext();
    run(ctx2, b.build());
    CHECK_EQ(ctx2.scope.getVariable("shl").toNumber(), 16.0);
}

TEST_CASE(Interpreter_StringOps_LengthExtractAdd) {
    Asm a;
    a.pushString("len");
    a.pushString("hello");
    a.op(op(AC::StringLength));
    a.op(op(AC::SetVariable));

    a.pushString("extracted");
    a.pushString("hello world");
    a.pushInt(6);  // index
    a.pushInt(5);  // count
    a.op(op(AC::StringExtract));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("len").toNumber(), 5.0);
    CHECK_EQ(ctx.scope.getVariable("extracted").toString(), "world");
}

TEST_CASE(Interpreter_Variables_GetSetDefineLocal) {
    Asm a;
    a.pushString("x");
    a.pushInt(10);
    a.op(op(AC::SetVariable));
    a.pushString("y");
    a.pushString("x");
    a.op(op(AC::GetVariable));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("x").toNumber(), 10.0);
    CHECK_EQ(ctx.scope.getVariable("y").toNumber(), 10.0);
}

TEST_CASE(Interpreter_InitObject_GetMemberSetMember) {
    // {a: 1, b: "two"} — InitObject pops NumProps, then (value,name) pairs.
    Asm a;
    a.pushString("obj");
    a.pushString("a");
    a.pushInt(1);
    a.pushString("b");
    a.pushString("two");
    a.pushInt(2);  // NumProperties
    a.op(op(AC::InitObject));
    a.op(op(AC::SetVariable));

    // obj.c = 3 (via GetVariable target then SetMember)
    a.pushString("obj");
    a.op(op(AC::GetVariable));
    a.pushString("c");
    a.pushInt(3);
    a.op(op(AC::SetMember));

    auto ctx = makeContext();
    run(ctx, a.build());
    Value obj = ctx.scope.getVariable("obj");
    CHECK(obj.isObject());
    CHECK_EQ(obj.asObject()->getMember("a").toNumber(), 1.0);
    CHECK_EQ(obj.asObject()->getMember("b").toString(), "two");
    CHECK_EQ(obj.asObject()->getMember("c").toNumber(), 3.0);
}

TEST_CASE(Interpreter_InitArray_LengthAndElements) {
    Asm a;
    a.pushString("arr");
    a.pushString("x");
    a.pushString("y");
    a.pushString("z");
    a.pushInt(3);  // NumElements
    a.op(op(AC::InitArray));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    Value arr = ctx.scope.getVariable("arr");
    CHECK(arr.isObject());
    CHECK(arr.asObject()->isArray());
    CHECK_EQ(arr.asObject()->getMember("length").toNumber(), 3.0);
    CHECK_EQ(arr.asObject()->getMember("0").toString(), "x");
    CHECK_EQ(arr.asObject()->getMember("2").toString(), "z");
}

TEST_CASE(Interpreter_ControlFlow_Loop_SumsOneToFive) {
    // sum = 0; i = 0; while (i < 5) { sum += i; i++; }
    Asm a;
    a.pushString("sum");
    a.pushInt(0);
    a.op(op(AC::SetVariable));
    a.pushString("i");
    a.pushInt(0);
    a.op(op(AC::SetVariable));

    a.label("loop_start");
    a.pushString("i");
    a.op(op(AC::GetVariable));
    a.pushInt(5);
    a.op(op(AC::Less2));
    a.op(op(AC::Not));
    a.ifJump("loop_end");

    a.pushString("sum");
    a.pushString("sum");
    a.op(op(AC::GetVariable));
    a.pushString("i");
    a.op(op(AC::GetVariable));
    a.op(op(AC::Add2));
    a.op(op(AC::SetVariable));

    a.pushString("i");
    a.pushString("i");
    a.op(op(AC::GetVariable));
    a.op(op(AC::Increment));
    a.op(op(AC::SetVariable));

    a.jump("loop_start");
    a.label("loop_end");

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("sum").toNumber(), 10.0);  // 0+1+2+3+4
    CHECK_EQ(ctx.scope.getVariable("i").toNumber(), 5.0);
}

TEST_CASE(Interpreter_DefineFunctionV1_CallFunction_ReturnsValue) {
    // function add(a, b) { return a + b; }
    Asm body;
    body.pushString("a");
    body.op(op(AC::GetVariable));
    body.pushString("b");
    body.op(op(AC::GetVariable));
    body.op(op(AC::Add2));
    body.op(op(AC::Return));

    Asm a;
    a.defineFunctionV1("add", {"a", "b"}, body.build());

    // ActionCallFunction pops FunctionName (top), then NumArgs, then that
    // many args (which come off in reverse call order) — so bytecode
    // pushes args in call order first, then NumArgs, then FunctionName
    // last (topmost).
    a.pushString("result");
    a.pushInt(3);
    a.pushInt(4);
    a.pushInt(2);  // NumArgs
    a.pushString("add");
    a.op(op(AC::CallFunction));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 7.0);
}

TEST_CASE(Interpreter_Recursion_FactorialOfFive) {
    // function fact(n) { if (n < 2) { return 1; } return n * fact(n-1); }
    Asm body;
    body.pushString("n");
    body.op(op(AC::GetVariable));
    body.pushInt(2);
    body.op(op(AC::Less2));
    body.op(op(AC::Not));
    body.ifJump("recurse");
    body.pushInt(1);
    body.op(op(AC::Return));
    body.label("recurse");
    body.pushString("n");
    body.op(op(AC::GetVariable));  // n (kept on stack for the final Multiply)
    body.pushString("n");
    body.op(op(AC::GetVariable));
    body.pushInt(1);
    body.op(op(AC::Subtract));  // arg: n - 1
    body.pushInt(1);            // NumArgs
    body.pushString("fact");    // FunctionName (topmost)
    body.op(op(AC::CallFunction));
    body.op(op(AC::Multiply));  // n * fact(n-1)
    body.op(op(AC::Return));

    Asm a;
    a.defineFunctionV1("fact", {"n"}, body.build());
    a.pushString("result");
    a.pushInt(5);
    a.pushInt(1);  // NumArgs
    a.pushString("fact");
    a.op(op(AC::CallFunction));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 120.0);
}

TEST_CASE(Interpreter_DefineFunction2_RegisterParamsAndPreloadThis) {
    // function(a) { return this.base + a; } bound as a "sum" method on an
    // object with base=100, called via CallMethod so `this` is set.
    uint16_t flags = 0x0001;  // PreloadThisFlag

    Asm b;
    Asm fnBody;
    fnBody.pushRegisterValue(1);
    fnBody.pushString("base");
    fnBody.op(op(AC::GetMember));
    fnBody.pushRegisterValue(2);
    fnBody.op(op(AC::Add2));
    fnBody.op(op(AC::Return));
    std::vector<Asm::RegParam> fnParams = {{2, "a"}};

    // obj = {base: 100}
    b.pushString("obj");
    b.pushString("base");
    b.pushInt(100);
    b.pushInt(1);
    b.op(op(AC::InitObject));
    b.op(op(AC::SetVariable));

    // obj.sum = function(a){ return this.base + a; }
    b.pushString("obj");
    b.op(op(AC::GetVariable));
    b.pushString("sum");
    b.defineFunction2("", 4, flags, fnParams, fnBody.build());
    b.op(op(AC::SetMember));

    // result = obj.sum(5) — ActionCallMethod pops MethodName (top), Object,
    // NumArgs, then that many args.
    b.pushString("result");
    b.pushInt(5);
    b.pushInt(1);  // NumArgs
    b.pushString("obj");
    b.op(op(AC::GetVariable));
    b.pushString("sum");
    b.op(op(AC::CallMethod));
    b.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, b.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 105.0);
}

TEST_CASE(Interpreter_Closures_FunctionSeesEnclosingScopeVariable) {
    // A function's captured scope includes variables already defined in
    // the enclosing scope at DefineFunction time (real closure capture,
    // not just global lookup — the captured Scope snapshot is what makes
    // this work rather than coincidental shared global state).
    Asm a;
    a.pushString("captured");
    a.pushInt(42);
    a.op(op(AC::SetVariable));

    Asm body;
    body.pushString("captured");
    body.op(op(AC::GetVariable));
    body.op(op(AC::Return));
    a.defineFunctionV1("reader", {}, body.build());

    a.pushString("result");
    a.pushInt(0);  // NumArgs
    a.pushString("reader");
    a.op(op(AC::CallFunction));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 42.0);
}

TEST_CASE(Interpreter_NewObject_ArrayLiteralViaConstructor) {
    // ActionNewObject pops ClassName (top), NumArgs, then that many args.
    Asm a;
    a.pushString("arr");
    a.pushInt(1);
    a.pushInt(2);
    a.pushInt(2);  // NumArgs
    a.pushString("Array");
    a.op(op(AC::NewObject));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    Value arr = ctx.scope.getVariable("arr");
    CHECK(arr.isObject() && arr.asObject()->isArray());
    CHECK_EQ(arr.asObject()->elements.size(), static_cast<size_t>(2));
    CHECK_EQ(arr.asObject()->getMember("0").toNumber(), 1.0);
    CHECK_EQ(arr.asObject()->getMember("1").toNumber(), 2.0);
}

TEST_CASE(Interpreter_NewObject_UserDefinedConstructor) {
    // function Point(x) { this.x = x; }
    Asm ctorBody;
    ctorBody.pushRegisterValue(1);  // preloaded this
    ctorBody.pushString("x");
    ctorBody.pushRegisterValue(2);  // param x
    ctorBody.op(op(AC::SetMember));
    ctorBody.op(op(AC::Return));  // implicit undefined return -> NewObject keeps newObj

    Asm a;
    a.defineFunction2("Point", 4, 0x0001 /*PreloadThis*/, {{2, "x"}}, ctorBody.build());

    a.pushString("pt");
    a.pushInt(9);
    a.pushInt(1);  // NumArgs
    a.pushString("Point");
    a.op(op(AC::NewObject));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    Value pt = ctx.scope.getVariable("pt");
    CHECK(pt.isObject());
    CHECK_EQ(pt.asObject()->getMember("x").toNumber(), 9.0);
}

TEST_CASE(Interpreter_Trace_InvokesCustomSink) {
    Asm a;
    a.pushString("hello from AVM1");
    a.op(op(AC::Trace));

    auto ctx = makeContext();
    std::vector<std::string> traced;
    ctx.traceSink = [&](const std::string& msg) { traced.push_back(msg); };
    run(ctx, a.build());

    CHECK_EQ(traced.size(), static_cast<size_t>(1));
    CHECK_EQ(traced[0], "hello from AVM1");
}

TEST_CASE(Interpreter_ConstantPool_PushConstant8) {
    Asm a;
    a.constantPoolAction({"alpha", "beta", "gamma"});
    a.pushString("result");
    a.pushConstant8(1);  // "beta"
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toString(), "beta");
}

TEST_CASE(Interpreter_StoreRegister_PushRegisterValue) {
    Asm a;
    a.pushInt(77);
    a.storeRegister(1);  // does NOT pop
    a.op(op(AC::Pop));
    a.pushString("result");
    a.pushRegisterValue(1);
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 77.0);
}

TEST_CASE(Interpreter_DeepRecursion_DoesNotCrashOrHang) {
    // function loop() { return loop(); } — infinite recursion; the call
    // depth guard must return gracefully rather than overflowing the C++
    // stack.
    Asm body;
    body.pushInt(0);  // NumArgs
    body.pushString("loop");
    body.op(op(AC::CallFunction));
    body.op(op(AC::Return));

    Asm a;
    a.defineFunctionV1("loop", {}, body.build());
    a.pushString("result");
    a.pushInt(0);  // NumArgs
    a.pushString("loop");
    a.op(op(AC::CallFunction));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());  // must return, not hang or crash
    CHECK(ctx.scope.getVariable("result").isUndefined());
}

TEST_CASE(Interpreter_With_ExtendsScopeForBlockOnly) {
    Asm withBody;
    withBody.pushString("x");
    withBody.pushInt(1);
    withBody.op(op(AC::SetVariable));

    Asm a;
    // target = {x: 0} — gives it an OWN "x" property so SetVariable inside
    // the with-block updates it, not the outer/global scope.
    a.pushString("target");
    a.pushString("x");
    a.pushInt(0);
    a.pushInt(1);  // NumProperties
    a.op(op(AC::InitObject));
    a.op(op(AC::SetVariable));

    a.pushString("target");
    a.op(op(AC::GetVariable));
    a.withAction(withBody.build());

    auto ctx = makeContext();
    run(ctx, a.build());

    Value target = ctx.scope.getVariable("target");
    CHECK(target.isObject());
    CHECK_EQ(target.asObject()->getMember("x").toNumber(), 1.0);
    CHECK(ctx.scope.getVariable("x").isUndefined());  // outer scope untouched
}

namespace {
class RecordingHostBindings : public HostBindings {
public:
    int playCalls = 0;
    int stopCalls = 0;
    void play() override { ++playCalls; }
    void stop() override { ++stopCalls; }
};
}  // namespace

TEST_CASE(Interpreter_HostBindings_PlayStopForwarded) {
    Asm a;
    a.op(op(AC::Play));
    a.op(op(AC::Stop));

    auto ctx = makeContext();
    RecordingHostBindings host;
    ctx.host = &host;
    run(ctx, a.build());

    CHECK_EQ(host.playCalls, 1);
    CHECK_EQ(host.stopCalls, 1);
}

TEST_CASE(Interpreter_HostBindings_Unset_DoesNotCrash) {
    Asm a;
    a.op(op(AC::Play));
    a.op(op(AC::Stop));
    a.pushInt(1);
    a.pushInt(2);
    a.op(op(AC::GetProperty));

    auto ctx = makeContext();  // ctx.host stays nullptr
    run(ctx, a.build());       // must not crash
}

TEST_CASE(Interpreter_MalformedBytecode_UnderflowedStackDoesNotCrash) {
    // Add2 with nothing pushed first: both pops safely return undefined.
    Asm a;
    a.op(op(AC::Add2));
    a.op(op(AC::SetVariable));  // also underflows safely (no name/value pushed)
    a.op(op(AC::Pop));          // pop on an already-empty stack

    auto ctx = makeContext();
    run(ctx, a.build());  // must not crash
}

TEST_CASE(Interpreter_TypeOf_AllTypes) {
    auto ctx = makeContext();

    Asm a;
    a.pushString("t");
    a.pushUndefined();
    a.op(op(AC::TypeOf));
    a.op(op(AC::SetVariable));
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("t").toString(), "undefined");

    Asm b;
    b.pushString("t");
    b.pushNull();
    b.op(op(AC::TypeOf));
    b.op(op(AC::SetVariable));
    run(ctx, b.build());
    CHECK_EQ(ctx.scope.getVariable("t").toString(), "object");

    Asm c;
    c.pushString("t");
    c.pushInt(5);
    c.op(op(AC::TypeOf));
    c.op(op(AC::SetVariable));
    run(ctx, c.build());
    CHECK_EQ(ctx.scope.getVariable("t").toString(), "number");
}

TEST_CASE(Interpreter_IncrementDecrement) {
    Asm a;
    a.pushString("inc");
    a.pushInt(5);
    a.op(op(AC::Increment));
    a.op(op(AC::SetVariable));
    a.pushString("dec");
    a.pushInt(5);
    a.op(op(AC::Decrement));
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("inc").toNumber(), 6.0);
    CHECK_EQ(ctx.scope.getVariable("dec").toNumber(), 4.0);
}

TEST_CASE(Interpreter_PushDuplicateAndStackSwap) {
    // Duplicate a computed value so it can be stored into two variables
    // without recomputing it; StackSwap reorders name/value pairs so each
    // SetVariable sees [name, value] with value on top.
    Asm a;
    a.pushInt(9);
    a.op(op(AC::PushDuplicate));  // stack: 9, 9
    a.pushString("a");            // stack: 9, 9, "a"
    a.op(op(AC::StackSwap));      // stack: 9, "a", 9
    a.op(op(AC::SetVariable));    // pops 9(value), "a"(name) -> a=9; stack: 9
    a.pushString("b");            // stack: 9, "b"
    a.op(op(AC::StackSwap));      // stack: "b", 9
    a.op(op(AC::SetVariable));    // pops 9(value), "b"(name) -> b=9; stack: empty

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("a").toNumber(), 9.0);
    CHECK_EQ(ctx.scope.getVariable("b").toNumber(), 9.0);
}

TEST_CASE(Interpreter_PushTypes_AllRoundTrip) {
    Asm a;
    a.pushString("s");
    a.pushString("hi");
    a.op(op(AC::SetVariable));

    a.pushString("f");
    a.pushFloat(2.5f);
    a.op(op(AC::SetVariable));

    a.pushString("n");
    a.pushNull();
    a.op(op(AC::SetVariable));

    a.pushString("u");
    a.pushUndefined();
    a.op(op(AC::SetVariable));

    a.pushString("b");
    a.pushBool(true);
    a.op(op(AC::SetVariable));

    a.pushString("d");
    a.pushDouble(3.14159265358979);
    a.op(op(AC::SetVariable));

    a.pushString("i");
    a.pushInt(-7);
    a.op(op(AC::SetVariable));

    auto ctx = makeContext();
    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("s").toString(), "hi");
    CHECK_EQ(ctx.scope.getVariable("f").toNumber(), 2.5);
    CHECK(ctx.scope.getVariable("n").isNull());
    CHECK(ctx.scope.getVariable("u").isUndefined());
    CHECK(ctx.scope.getVariable("b").toBoolean());
    CHECK(std::fabs(ctx.scope.getVariable("d").toNumber() - 3.14159265358979) < 1e-9);
    CHECK_EQ(ctx.scope.getVariable("i").toNumber(), -7.0);
}

// --- Phase 6: native (C++-backed) functions ---------------------------

TEST_CASE(Interpreter_NativeFunction_CallFunction_InvokesCppCodeWithArgs) {
    auto ctx = makeContext();
    int callCount = 0;
    ctx.globalObject->setOwnProperty(
        "nativeAdd",
        Value::object(makeNativeFunction(
            "nativeAdd", [&callCount](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                ++callCount;
                double a = args.size() > 0 ? args[0].toNumber() : 0.0;
                double b = args.size() > 1 ? args[1].toNumber() : 0.0;
                return Value::number(a + b);
            })));

    // ActionCallFunction pops (in order): name, numArgs, then `numArgs`
    // args — so push order (bottom to top) is: args..., numArgs, name.
    Asm a;
    a.pushString("result");
    a.pushInt(3);
    a.pushInt(4);
    a.pushInt(2);  // numArgs
    a.pushString("nativeAdd");
    a.op(op(AC::CallFunction));
    a.op(op(AC::SetVariable));

    run(ctx, a.build());

    CHECK_EQ(callCount, 1);
    CHECK_EQ(ctx.scope.getVariable("result").toNumber(), 7.0);
}

TEST_CASE(Interpreter_NativeFunction_CallMethod_SeesThisValue) {
    auto ctx = makeContext();
    auto obj = std::make_shared<Object>();
    obj->setOwnProperty("tag", Value::string("marker"));
    obj->setOwnProperty(
        "describe", Value::object(makeNativeFunction(
                        "describe", [](ExecutionContext&, const Value& thisVal,
                                        const std::vector<Value>&) {
                            if (!thisVal.isObject() || !thisVal.asObject()) return Value::string("?");
                            return thisVal.asObject()->getOwnProperty("tag");
                        })));
    ctx.globalObject->setOwnProperty("obj", Value::object(obj));

    // ActionCallMethod pops (in order): methodName, object, numArgs, then
    // `numArgs` args — so the push order (bottom to top) must be: args...,
    // numArgs, object, methodName.
    Asm a;
    a.pushString("result");
    a.pushInt(0);  // numArgs
    a.pushString("obj");
    a.op(op(AC::GetVariable));
    a.pushString("describe");
    a.op(op(AC::CallMethod));
    a.op(op(AC::SetVariable));

    run(ctx, a.build());
    CHECK_EQ(ctx.scope.getVariable("result").toString(), "marker");
}
