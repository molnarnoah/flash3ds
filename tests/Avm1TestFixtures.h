// Avm1TestFixtures.h
//
// A tiny AVM1 bytecode assembler for building test fixtures, independently
// implemented from the same public SWF "Actions" spec the production
// Interpreter reads (src/avm1/Interpreter.cpp) — not sharing code with it,
// so a test passing doesn't just mean "the encoder and decoder agree with
// each other," it means both independently match the documented format
// (mirrors the SwfTestFixtures.h pattern used for the SWF/shape tests).
//
// Supports labels for ActionJump/ActionIf so tests can write real loops
// and conditionals without hand-computing branch offsets.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace flash3ds::test::fixtures {

class Avm1Assembler {
public:
    // --- ActionPush variants (each call emits its own standalone
    // ActionPush record — valid per spec, simpler than batching) ----------
    void pushString(const std::string& s);
    void pushFloat(float f);
    void pushNull();
    void pushUndefined();
    void pushRegisterValue(uint8_t reg);
    void pushBool(bool b);
    void pushDouble(double v);
    void pushInt(int32_t v);
    void pushConstant8(uint8_t idx);
    void pushConstant16(uint16_t idx);

    // A no-operand action (code < 0x80), e.g. op(0x0A) for ActionAdd.
    void op(uint8_t code);

    // Length-prefixed actions with hand-built data (code >= 0x80).
    void storeRegister(uint8_t reg);
    void constantPoolAction(const std::vector<std::string>& pool);
    void gotoFrameAction(uint16_t frame);
    void gotoLabelAction(const std::string& label);
    void setTargetAction(const std::string& target);
    void getUrlAction(const std::string& url, const std::string& target);

    void defineFunctionV1(const std::string& name, const std::vector<std::string>& params,
                           const std::vector<uint8_t>& body);

    struct RegParam {
        uint8_t reg;
        std::string name;
    };
    void defineFunction2(const std::string& name, uint8_t registerCount, uint16_t flags,
                          const std::vector<RegParam>& params, const std::vector<uint8_t>& body);

    void withAction(const std::vector<uint8_t>& block);

    // --- control flow ------------------------------------------------------
    void label(const std::string& name);
    void jump(const std::string& targetLabel);
    void ifJump(const std::string& targetLabel);

    // Resolves all pending branch offsets, appends a trailing ActionEnd
    // (0x00), and returns the finished bytecode buffer.
    std::vector<uint8_t> build();

private:
    void emitInstr(uint8_t code, const std::vector<uint8_t>& data, bool hasLength);
    void emitBranch(uint8_t code, const std::string& target);
    void appendRaw(const std::vector<uint8_t>& raw);

    std::vector<uint8_t> bytes_;
    std::unordered_map<std::string, size_t> labels_;
    struct PendingBranch {
        size_t offsetFieldPos;
        std::string targetLabel;
    };
    std::vector<PendingBranch> pendingBranches_;
};

}  // namespace flash3ds::test::fixtures
