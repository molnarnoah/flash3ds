#include "Avm1TestFixtures.h"

#include <cstring>

namespace flash3ds::test::fixtures {

namespace {

void appendU16(std::vector<uint8_t>& d, uint16_t v) {
    d.push_back(static_cast<uint8_t>(v & 0xFF));
    d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& d, uint32_t v) {
    d.push_back(static_cast<uint8_t>(v & 0xFF));
    d.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    d.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    d.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendCString(std::vector<uint8_t>& d, const std::string& s) {
    d.insert(d.end(), s.begin(), s.end());
    d.push_back(0);
}

}  // namespace

void Avm1Assembler::emitInstr(uint8_t code, const std::vector<uint8_t>& data, bool hasLength) {
    bytes_.push_back(code);
    if (hasLength) {
        appendU16(bytes_, static_cast<uint16_t>(data.size()));
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
}

void Avm1Assembler::appendRaw(const std::vector<uint8_t>& raw) {
    bytes_.insert(bytes_.end(), raw.begin(), raw.end());
}

void Avm1Assembler::emitBranch(uint8_t code, const std::string& target) {
    bytes_.push_back(code);
    appendU16(bytes_, 2);  // branch offset field is always exactly 2 bytes
    size_t offsetFieldPos = bytes_.size();
    bytes_.push_back(0);
    bytes_.push_back(0);
    pendingBranches_.push_back({offsetFieldPos, target});
}

void Avm1Assembler::pushString(const std::string& s) {
    std::vector<uint8_t> d{0};
    appendCString(d, s);
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushFloat(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    std::vector<uint8_t> d{1};
    appendU32(d, bits);
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushNull() {
    std::vector<uint8_t> d{2};
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushUndefined() {
    std::vector<uint8_t> d{3};
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushRegisterValue(uint8_t reg) {
    std::vector<uint8_t> d{4, reg};
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushBool(bool b) {
    std::vector<uint8_t> d{5, static_cast<uint8_t>(b ? 1 : 0)};
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushDouble(double v) {
    // Matches Interpreter.cpp's readAvm1Double exactly (both independently
    // written from the same documented word-swap convention) so tests
    // round-trip regardless of whether that convention matches real Flash
    // output — see docs/avm1-support.md's confidence note.
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    uint32_t hi = static_cast<uint32_t>(bits >> 32);
    uint32_t lo = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
    std::vector<uint8_t> d{6};
    appendU32(d, hi);
    appendU32(d, lo);
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushInt(int32_t v) {
    std::vector<uint8_t> d{7};
    appendU32(d, static_cast<uint32_t>(v));
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushConstant8(uint8_t idx) {
    std::vector<uint8_t> d{8, idx};
    emitInstr(0x96, d, true);
}

void Avm1Assembler::pushConstant16(uint16_t idx) {
    std::vector<uint8_t> d{9};
    appendU16(d, idx);
    emitInstr(0x96, d, true);
}

void Avm1Assembler::op(uint8_t code) { emitInstr(code, {}, false); }

void Avm1Assembler::storeRegister(uint8_t reg) {
    std::vector<uint8_t> d{reg};
    emitInstr(0x87, d, true);
}

void Avm1Assembler::constantPoolAction(const std::vector<std::string>& pool) {
    std::vector<uint8_t> d;
    appendU16(d, static_cast<uint16_t>(pool.size()));
    for (const auto& s : pool) appendCString(d, s);
    emitInstr(0x88, d, true);
}

void Avm1Assembler::gotoFrameAction(uint16_t frame) {
    std::vector<uint8_t> d;
    appendU16(d, frame);
    emitInstr(0x81, d, true);
}

void Avm1Assembler::gotoLabelAction(const std::string& label) {
    std::vector<uint8_t> d;
    appendCString(d, label);
    emitInstr(0x8C, d, true);
}

void Avm1Assembler::setTargetAction(const std::string& target) {
    std::vector<uint8_t> d;
    appendCString(d, target);
    emitInstr(0x8B, d, true);
}

void Avm1Assembler::getUrlAction(const std::string& url, const std::string& target) {
    std::vector<uint8_t> d;
    appendCString(d, url);
    appendCString(d, target);
    emitInstr(0x83, d, true);
}

void Avm1Assembler::defineFunctionV1(const std::string& name,
                                      const std::vector<std::string>& params,
                                      const std::vector<uint8_t>& body) {
    std::vector<uint8_t> header;
    appendCString(header, name);
    appendU16(header, static_cast<uint16_t>(params.size()));
    for (const auto& p : params) appendCString(header, p);
    appendU16(header, static_cast<uint16_t>(body.size()));
    emitInstr(0x9B, header, true);
    appendRaw(body);
}

void Avm1Assembler::defineFunction2(const std::string& name, uint8_t registerCount,
                                     uint16_t flags, const std::vector<RegParam>& params,
                                     const std::vector<uint8_t>& body) {
    std::vector<uint8_t> header;
    appendCString(header, name);
    appendU16(header, static_cast<uint16_t>(params.size()));
    header.push_back(registerCount);
    appendU16(header, flags);
    for (const auto& p : params) {
        header.push_back(p.reg);
        appendCString(header, p.name);
    }
    appendU16(header, static_cast<uint16_t>(body.size()));
    emitInstr(0x8E, header, true);
    appendRaw(body);
}

void Avm1Assembler::withAction(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> header;
    appendU16(header, static_cast<uint16_t>(block.size()));
    emitInstr(0x94, header, true);
    appendRaw(block);
}

void Avm1Assembler::label(const std::string& name) { labels_[name] = bytes_.size(); }

void Avm1Assembler::jump(const std::string& targetLabel) { emitBranch(0x99, targetLabel); }

void Avm1Assembler::ifJump(const std::string& targetLabel) { emitBranch(0x9D, targetLabel); }

std::vector<uint8_t> Avm1Assembler::build() {
    for (const auto& p : pendingBranches_) {
        auto it = labels_.find(p.targetLabel);
        int32_t targetPos =
            it != labels_.end() ? static_cast<int32_t>(it->second) : static_cast<int32_t>(bytes_.size());
        int32_t afterOffsetFieldPos = static_cast<int32_t>(p.offsetFieldPos + 2);
        int16_t branchOffset = static_cast<int16_t>(targetPos - afterOffsetFieldPos);
        bytes_[p.offsetFieldPos] = static_cast<uint8_t>(branchOffset & 0xFF);
        bytes_[p.offsetFieldPos + 1] = static_cast<uint8_t>((branchOffset >> 8) & 0xFF);
    }
    std::vector<uint8_t> out = bytes_;
    out.push_back(0x00);  // ActionEnd
    return out;
}

}  // namespace flash3ds::test::fixtures
