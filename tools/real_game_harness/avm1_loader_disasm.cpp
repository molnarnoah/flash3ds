// avm1_loader_disasm.cpp
//
// Roadmap Phase 4 (2026-08-21), Step 1: "targeted disassembly of Extreme
// Pamplona's loader functions to identify the actual loading API used
// (likely MovieClipLoader or a custom wrapper -- not loadMovie itself per
// the corpus string scan)". `docs/known-limitations.md` L6 confirms zero
// hits for the literal strings "loadMovie"/"_level" anywhere in the file,
// so the actual mechanism has to be found by reading real bytecode, not
// guessed at from string presence alone.
//
// This is a STATIC, best-effort AVM1 symbolic disassembler -- NOT the real
// avm1::Interpreter (which requires a live ExecutionContext/HostBindings
// and only executes, doesn't report). It walks every DoAction/
// DoInitAction action-byte-stream in the file (recursively into nested
// DefineSprite tag streams, and recursively into DefineFunction/
// DefineFunction2 bodies -- reusing tools/swf_diagnostic's documented
// extent-handling pattern for both), and does a SINGLE LINEAR PASS (no
// control-flow simulation -- branches/loops are not followed, so the
// symbolic stack can desync after a jump; this is a discovery tool, not a
// verifier) tracking a symbolic value stack well enough to resolve
// string-literal operands of ActionCode::{CallFunction,CallMethod,
// NewMethod,NewObject,SetMember,GetURL,GetURL2} -- the actions that
// reveal *what API surface real content actually calls*, since those
// names live on AVM1's stack (pushed via ActionPush/ActionConstantPool),
// not as fixed bytecode operands.
//
// Usage: avm1_loader_disasm <path.swf> [keyword ...]
// With no keywords, prints every resolved call/assignment event. With
// keywords, prints only events whose text contains at least one keyword
// (case-insensitive) -- e.g. `avm1_loader_disasm extreme-pamplona.swf
// load Loader level attach export`.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "avm1/ActionCode.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "swf/DefineButtonTag.h"
#include "swf/PlaceObjectTag.h"
#include "swf/SwfLoader.h"
#include "swf/SwfReader.h"
#include "swf/TagCode.h"
#include "swf/TagDispatcher.h"

using namespace flash3ds;

namespace {

// --- recursive tag collection (same pattern as tools/swf_diagnostic) -----

struct CollectedTag {
    swf::TagRecord tag;
    int depth;
    // 2026-08-27 (Hobo1 "disassemble root frame 2's unidentified clips"
    // follow-up): the characterId of the innermost enclosing DefineSprite
    // this tag lives inside (0 = top-level movie tag stream). Previously
    // read and immediately discarded ("characterId, unused here") -- now
    // threaded through so a DoAction/DoInitAction frame script found deep
    // inside a specific sprite can be attributed to that sprite's
    // characterId directly, instead of only the tree-nesting `depth`
    // (which says "how many DefineSprite levels deep" but not "which
    // sprite"). This is what Step 1 of the disassembly prompt needs: find
    // every DoAction that belongs to characterId 114/118/192 specifically.
    uint16_t spriteCharacterId;
};

void collectTagsRecursive(const runtime::Movie& movie, const std::vector<swf::TagRecord>& tags,
                           int depth, uint16_t spriteCharacterId, std::vector<CollectedTag>& out) {
    for (const auto& tag : tags) {
        out.push_back({tag, depth, spriteCharacterId});
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::DefineSprite) {
            if (tag.bodyLength < 4) continue;
            swf::SwfReader header = movie.tagBodyReader(tag);
            uint16_t thisSpriteCharacterId = header.readU16();
            header.readU16();  // frameCount, unused here
            if (header.failed()) continue;
            std::vector<swf::TagRecord> nested;
            swf::SwfReader full(movie.data.data(), movie.data.size());
            full.seek(tag.bodyOffset + 4);
            size_t endOffset = tag.bodyOffset + tag.bodyLength;
            while (full.position() < endOffset && !full.failed()) {
                swf::TagRecord nestedTag;
                if (!swf::TagDispatcher::readTagHeader(full, nestedTag)) break;
                nested.push_back(nestedTag);
                if (static_cast<swf::TagCode>(nestedTag.code) == swf::TagCode::End) break;
                full.skip(nestedTag.bodyLength);
            }
            collectTagsRecursive(movie, nested, depth + 1, thisSpriteCharacterId, out);
        }
    }
}

// --- symbolic value ---------------------------------------------------

struct Sym {
    std::string text;         // display text, always set
    bool isString = false;    // literal string constant known
    std::string strVal;
    bool isNumber = false;    // literal number constant known
    double numVal = 0;
};

Sym symString(const std::string& s) {
    Sym v;
    v.isString = true;
    v.strVal = s;
    v.text = "\"" + s + "\"";
    return v;
}
Sym symNumber(double n) {
    Sym v;
    v.isNumber = true;
    v.numVal = n;
    std::ostringstream ss;
    ss << n;
    v.text = ss.str();
    return v;
}
Sym symUnknown(const std::string& label) {
    Sym v;
    v.text = label;
    return v;
}

// --- symbolic stack machine --------------------------------------------

struct DisasmState {
    std::vector<Sym> stack;
    std::vector<std::string> pool;      // current ConstantPool
    std::vector<Sym> registers = std::vector<Sym>(256);
    std::vector<bool> registerSet = std::vector<bool>(256, false);
    std::string context;                // human label: where this stream lives
    std::vector<std::string> funcStack; // nested DefineFunction name trail
    std::vector<std::string>* eventsOut = nullptr;
};

Sym pop(DisasmState& st) {
    if (st.stack.empty()) return symUnknown("<underflow>");
    Sym v = st.stack.back();
    st.stack.pop_back();
    return v;
}

std::string currentLabel(const DisasmState& st) {
    std::string label = st.context;
    for (const auto& f : st.funcStack) label += " > function " + f;
    return label;
}

void emit(DisasmState& st, const std::string& text) {
    if (st.eventsOut) st.eventsOut->push_back("[" + currentLabel(st) + "] " + text);
}

std::vector<Sym> popArgs(DisasmState& st, const Sym& numArgs) {
    std::vector<Sym> args;
    if (!numArgs.isNumber) {
        // Unknown arg count -- can't safely pop a bounded number without
        // risking desyncing the rest of the stream's stack tracking even
        // further than a linear (no-control-flow) pass already does, so
        // stop here. This is flagged in the event text, not hidden.
        return args;
    }
    int n = static_cast<int>(numArgs.numVal);
    for (int i = 0; i < n; ++i) args.push_back(pop(st));
    std::reverse(args.begin(), args.end());
    return args;
}

std::string joinArgs(const std::vector<Sym>& args) {
    std::string s;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) s += ", ";
        s += args[i].text;
    }
    return s;
}

void walkActionStream(const uint8_t* data, size_t size, DisasmState& st);

void handleDefineFunction(avm1::ActionCode code, const uint8_t* operand, uint16_t length,
                           const uint8_t* streamBase, size_t streamSize, size_t& pos,
                           DisasmState& st) {
    swf::SwfReader header(operand, length);
    std::string fname = header.readCString();
    uint16_t numParams = header.readU16();
    std::vector<std::string> paramNames;
    if (code == avm1::ActionCode::DefineFunction) {
        for (uint16_t i = 0; i < numParams && !header.failed(); ++i) paramNames.push_back(header.readCString());
    } else {
        header.readU8();
        header.readU16();
        for (uint16_t i = 0; i < numParams && !header.failed(); ++i) {
            header.readU8();
            paramNames.push_back(header.readCString());
        }
    }
    uint16_t codeSize = header.readU16();
    if (header.failed() || pos + codeSize > streamSize) return;

    std::string label = fname.empty() ? ("<anon@" + std::to_string(pos) + ">") : fname;
    label += "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (i) label += ", ";
        label += paramNames[i];
    }
    label += ")";
    emit(st, "DefineFunction " + label);

    // Per the SWF spec (mirrored exactly in Interpreter.cpp's own
    // DefineFunction/2 case): a NAMED function defines a local variable and
    // leaves the stack untouched; an ANONYMOUS one pushes the new function
    // object onto the stack instead (the common "immediately-invoked
    // function expression"/dispatcher-table compile pattern -- without
    // this push, every anonymous function immediately followed by a
    // CallFunction/CallMethod/StoreRegister desyncs the symbolic stack
    // from here on, which is exactly what was observed before this was
    // added: a real anonymous-IIFE pattern showing up as "CallFunction ()"
    // instead of resolving the actual call target).
    if (fname.empty()) {
        st.stack.push_back(symUnknown("<function:" + label + ">"));
    }

    DisasmState nested = st;  // inherit constant pool + label context (registers reset per real semantics, but kept here for best-effort tracing)
    nested.registers.assign(256, Sym{});
    nested.registerSet.assign(256, false);
    nested.stack.clear();
    nested.funcStack.push_back(label);
    walkActionStream(streamBase + pos, codeSize, nested);
    pos += codeSize;
}

void walkActionStream(const uint8_t* data, size_t size, DisasmState& st) {
    size_t pos = 0;
    while (pos < size) {
        uint8_t opcode = data[pos];
        pos += 1;
        if (opcode == 0x00) break;  // ActionEnd
        auto code = static_cast<avm1::ActionCode>(opcode);

        if (opcode < 0x80) {
            switch (code) {
                case avm1::ActionCode::Pop:
                    pop(st);
                    break;
                case avm1::ActionCode::PushDuplicate:
                    if (!st.stack.empty()) st.stack.push_back(st.stack.back());
                    break;
                case avm1::ActionCode::StackSwap:
                    if (st.stack.size() >= 2) std::swap(st.stack[st.stack.size() - 1], st.stack[st.stack.size() - 2]);
                    break;
                case avm1::ActionCode::GetVariable: {
                    Sym name = pop(st);
                    st.stack.push_back(symUnknown(name.isString ? ("$" + name.strVal) : "<dynvar>"));
                    break;
                }
                case avm1::ActionCode::ToInteger: {
                    pop(st);
                    st.stack.push_back(symUnknown("<int>"));
                    break;
                }
                case avm1::ActionCode::TargetPath: {
                    pop(st);
                    st.stack.push_back(symString(""));
                    break;
                }
                case avm1::ActionCode::SetVariable: {
                    Sym value = pop(st);
                    Sym name = pop(st);
                    if (name.isString) emit(st, "SetVariable " + name.strVal + " = " + value.text);
                    break;
                }
                case avm1::ActionCode::DefineLocal: {
                    Sym value = pop(st);
                    Sym name = pop(st);
                    if (name.isString) emit(st, "var " + name.strVal + " = " + value.text);
                    break;
                }
                case avm1::ActionCode::DefineLocal2: {
                    pop(st);
                    break;
                }
                case avm1::ActionCode::GetMember: {
                    Sym member = pop(st);
                    Sym obj = pop(st);
                    std::string m = member.isString ? member.strVal : "<dyn>";
                    st.stack.push_back(symUnknown(obj.text + "." + m));
                    break;
                }
                case avm1::ActionCode::SetMember: {
                    Sym value = pop(st);
                    Sym member = pop(st);
                    Sym obj = pop(st);
                    std::string m = member.isString ? member.strVal : "<dyn>";
                    emit(st, "SetMember " + obj.text + "." + m + " = " + value.text);
                    break;
                }
                case avm1::ActionCode::CallFunction: {
                    Sym name = pop(st);
                    Sym numArgs = pop(st);
                    std::vector<Sym> args = popArgs(st, numArgs);
                    emit(st, "CallFunction " + (name.isString ? name.strVal : name.text) + "(" + joinArgs(args) +
                                 (numArgs.isNumber ? "" : ", <unknown-argc>") + ")");
                    st.stack.push_back(symUnknown("<callresult>"));
                    break;
                }
                case avm1::ActionCode::CallMethod: {
                    Sym method = pop(st);
                    Sym obj = pop(st);
                    Sym numArgs = pop(st);
                    std::vector<Sym> args = popArgs(st, numArgs);
                    std::string m = method.isString ? method.strVal : (method.text == "<undef>" || !method.isString ? "<self>" : method.text);
                    emit(st, "CallMethod " + obj.text + "." + m + "(" + joinArgs(args) +
                                 (numArgs.isNumber ? "" : ", <unknown-argc>") + ")");
                    st.stack.push_back(symUnknown("<callresult>"));
                    break;
                }
                case avm1::ActionCode::NewMethod: {
                    Sym method = pop(st);
                    Sym obj = pop(st);
                    Sym numArgs = pop(st);
                    std::vector<Sym> args = popArgs(st, numArgs);
                    std::string m = method.isString ? method.strVal : "<self>";
                    emit(st, "NewMethod new " + obj.text + "." + m + "(" + joinArgs(args) +
                                 (numArgs.isNumber ? "" : ", <unknown-argc>") + ")");
                    st.stack.push_back(symUnknown("<newresult>"));
                    break;
                }
                case avm1::ActionCode::NewObject: {
                    Sym cls = pop(st);
                    Sym numArgs = pop(st);
                    std::vector<Sym> args = popArgs(st, numArgs);
                    emit(st, "NewObject new " + (cls.isString ? cls.strVal : cls.text) + "(" + joinArgs(args) +
                                 (numArgs.isNumber ? "" : ", <unknown-argc>") + ")");
                    st.stack.push_back(symUnknown("<newresult>"));
                    break;
                }
                case avm1::ActionCode::Increment:
                case avm1::ActionCode::Decrement:
                case avm1::ActionCode::TypeOf:
                case avm1::ActionCode::ToNumber:
                case avm1::ActionCode::ToString:
                case avm1::ActionCode::Not:
                case avm1::ActionCode::StringLength:
                case avm1::ActionCode::MBStringLength:
                    pop(st);
                    st.stack.push_back(symUnknown("<unop>"));
                    break;
                case avm1::ActionCode::Add:
                case avm1::ActionCode::Add2:
                case avm1::ActionCode::Subtract:
                case avm1::ActionCode::Multiply:
                case avm1::ActionCode::Divide:
                case avm1::ActionCode::Modulo:
                case avm1::ActionCode::Equals:
                case avm1::ActionCode::Equals2:
                case avm1::ActionCode::StrictEquals:
                case avm1::ActionCode::Less:
                case avm1::ActionCode::Less2:
                case avm1::ActionCode::Greater:
                case avm1::ActionCode::And:
                case avm1::ActionCode::Or:
                case avm1::ActionCode::StringAdd:
                case avm1::ActionCode::StringEquals:
                case avm1::ActionCode::StringLess:
                case avm1::ActionCode::StringGreater:
                case avm1::ActionCode::BitAnd:
                case avm1::ActionCode::BitOr:
                case avm1::ActionCode::BitXor:
                case avm1::ActionCode::BitLShift:
                case avm1::ActionCode::BitRShift:
                case avm1::ActionCode::BitURShift:
                case avm1::ActionCode::InstanceOf:
                case avm1::ActionCode::CastOp: {
                    // "StringAdd" is a special, common case worth resolving
                    // when both sides are known literals -- real content
                    // concatenates path/class-name strings this way
                    // constantly (e.g. "_level" + levelNum).
                    Sym b = pop(st);
                    Sym a = pop(st);
                    if (code == avm1::ActionCode::StringAdd && a.isString && b.isString) {
                        st.stack.push_back(symString(a.strVal + b.strVal));
                    } else {
                        st.stack.push_back(symUnknown("<binop>"));
                    }
                    break;
                }
                case avm1::ActionCode::InitObject: {
                    Sym numProps = pop(st);
                    if (numProps.isNumber) {
                        for (int i = 0; i < static_cast<int>(numProps.numVal); ++i) { pop(st); pop(st); }
                    }
                    st.stack.push_back(symUnknown("<object>"));
                    break;
                }
                case avm1::ActionCode::InitArray: {
                    Sym numElems = pop(st);
                    if (numElems.isNumber) {
                        for (int i = 0; i < static_cast<int>(numElems.numVal); ++i) pop(st);
                    }
                    st.stack.push_back(symUnknown("<array>"));
                    break;
                }
                // --- everything below this point has NO literal-resolution
                // value on its own but DOES have a real stack effect per
                // Interpreter.cpp -- modeled here purely to keep this
                // best-effort linear-pass symbolic stack from desyncing
                // (an unmodeled pop/push here would silently corrupt every
                // subsequent event in the same action stream, which is
                // exactly what happened before these cases were added: see
                // this tool's own investigation notes for how GetProperty/
                // AsciiToChar-family gaps produced garbage output on real
                // content). Pop/push counts cross-checked directly against
                // Interpreter.cpp's own implementation of each, not
                // guessed.
                case avm1::ActionCode::SetTarget2:
                    pop(st);
                    break;
                case avm1::ActionCode::GetProperty: {
                    pop(st);  // index
                    pop(st);  // target
                    st.stack.push_back(symUnknown("<property>"));
                    break;
                }
                case avm1::ActionCode::SetProperty:
                    pop(st);  // value
                    pop(st);  // index
                    pop(st);  // target
                    break;
                case avm1::ActionCode::CloneSprite: {
                    Sym depth = pop(st);
                    Sym newName = pop(st);
                    Sym target = pop(st);
                    emit(st, "CloneSprite target=" + target.text + " newName=" + newName.text +
                                 " depth=" + depth.text);
                    break;
                }
                case avm1::ActionCode::RemoveSprite: {
                    Sym target = pop(st);
                    emit(st, "RemoveSprite target=" + target.text);
                    break;
                }
                case avm1::ActionCode::StartDrag: {
                    Sym target = pop(st);
                    Sym lockCenter = pop(st);
                    Sym constrain = pop(st);
                    // Real semantics test constrain.toBoolean() at runtime
                    // to decide whether 4 more values (L,T,R,B) follow --
                    // only resolvable here when the pushed value was a
                    // literal boolean this tool actually tracked.
                    if (constrain.text == "true") { pop(st); pop(st); pop(st); pop(st); }
                    emit(st, "StartDrag target=" + target.text + " lockCenter=" + lockCenter.text);
                    break;
                }
                case avm1::ActionCode::Return: {
                    Sym v = pop(st);
                    emit(st, "Return " + v.text);
                    break;
                }
                case avm1::ActionCode::Extends:
                    pop(st);  // subclass ctor
                    pop(st);  // superclass ctor
                    break;
                case avm1::ActionCode::ImplementsOp: {
                    Sym count = pop(st);
                    pop(st);  // class ctor
                    if (count.isNumber) {
                        for (int i = 0; i < static_cast<int>(count.numVal); ++i) pop(st);
                    }
                    break;
                }
                case avm1::ActionCode::Trace: {
                    Sym msg = pop(st);
                    emit(st, "Trace " + msg.text);
                    break;
                }
                case avm1::ActionCode::RandomNumber:
                    pop(st);
                    st.stack.push_back(symUnknown("<random>"));
                    break;
                case avm1::ActionCode::GetTime:
                    st.stack.push_back(symUnknown("<time>"));
                    break;
                case avm1::ActionCode::Throw: {
                    Sym v = pop(st);
                    emit(st, "Throw " + v.text);
                    break;
                }
                case avm1::ActionCode::StringExtract:
                case avm1::ActionCode::MBStringExtract:
                    pop(st);  // count
                    pop(st);  // index
                    pop(st);  // string
                    st.stack.push_back(symUnknown("<substr>"));
                    break;
                case avm1::ActionCode::CharToAscii:
                case avm1::ActionCode::MBCharToAscii: {
                    Sym s = pop(st);
                    if (s.isString && !s.strVal.empty()) {
                        st.stack.push_back(symNumber(static_cast<unsigned char>(s.strVal[0])));
                    } else {
                        st.stack.push_back(symUnknown("<charcode>"));
                    }
                    break;
                }
                case avm1::ActionCode::AsciiToChar:
                case avm1::ActionCode::MBAsciiToChar: {
                    // Real content commonly builds an "obfuscated" string
                    // literal one character at a time via AsciiToChar +
                    // repeated StringAdd specifically to defeat naive
                    // string scans (see this file's own header comment,
                    // and docs/known-limitations.md L6's "zero hits for
                    // the literal string loadMovie" finding) -- resolving
                    // this for real, known numeric codes is exactly what
                    // lets a *linear* symbolic pass see through that
                    // trick, unlike a plain strings(1)-style scan.
                    Sym code = pop(st);
                    if (code.isNumber) {
                        char c = static_cast<char>(static_cast<int>(code.numVal) & 0xFF);
                        st.stack.push_back(symString(std::string(1, c)));
                    } else {
                        st.stack.push_back(symUnknown("<char>"));
                    }
                    break;
                }
                case avm1::ActionCode::Delete: {
                    pop(st);  // name
                    pop(st);  // obj
                    st.stack.push_back(symUnknown("<bool>"));
                    break;
                }
                case avm1::ActionCode::Delete2:
                    pop(st);  // name
                    st.stack.push_back(symUnknown("<bool>"));
                    break;
                case avm1::ActionCode::Enumerate: {
                    pop(st);  // name
                    st.stack.push_back(symUnknown("null"));  // real op pushes a variable-length
                                                              // property list after this -- not
                                                              // statically knowable, undercounted
                    break;
                }
                case avm1::ActionCode::Enumerate2: {
                    pop(st);  // obj
                    st.stack.push_back(symUnknown("null"));  // same caveat as Enumerate
                    break;
                }
                // 2026-08-27 (Track A follow-up, frame-progression trace):
                // these bare (un-OOP, no-operand) timeline-control actions
                // were previously silently dropped by the `default: break`
                // below -- fine for the symbolic stack (none of them push/
                // pop anything, so dropping them never desynced later
                // events, unlike If/Jump), but it meant this tool was
                // structurally unable to show a `stop();`/`play();`/
                // `nextFrame();`/`prevFrame();` call at all, even though
                // these are exactly the classic compiled form of simple,
                // untargeted button/frame scripts (e.g. `on(release) {
                // play(); }`) -- precisely the kind of "what advances the
                // root timeline" evidence needed once the isDown()
                // question itself was answered (see docs/hobo-playability-
                // verification.md's frame-progression addendum). No stack
                // effect per Interpreter.cpp's own handling of these four.
                case avm1::ActionCode::Play:
                    emit(st, "Play()");
                    break;
                case avm1::ActionCode::Stop:
                    emit(st, "Stop()");
                    break;
                case avm1::ActionCode::NextFrame:
                    emit(st, "NextFrame()");
                    break;
                case avm1::ActionCode::PreviousFrame:
                    emit(st, "PrevFrame()");
                    break;
                default:
                    break;  // no stack effect this tool needs to model
            }
            continue;
        }

        // length-prefixed actions
        if (pos + 2 > size) break;
        uint16_t length = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        if (pos + length > size) break;
        const uint8_t* operand = data + pos;
        pos += length;

        switch (code) {
            case avm1::ActionCode::Push: {
                swf::SwfReader r(operand, length);
                while (!r.atEnd() && !r.failed()) {
                    uint8_t type = r.readU8();
                    if (r.failed()) break;
                    switch (type) {
                        case 0: st.stack.push_back(symString(r.readCString())); break;
                        case 1: {
                            uint32_t bits = r.readU32();
                            float f;
                            std::memcpy(&f, &bits, sizeof(f));
                            st.stack.push_back(symNumber(f));
                            break;
                        }
                        case 2: st.stack.push_back(symUnknown("null")); break;
                        case 3: st.stack.push_back(symUnknown("<undef>")); break;
                        case 4: {
                            uint8_t reg = r.readU8();
                            st.stack.push_back(st.registerSet[reg] ? st.registers[reg] : symUnknown("R" + std::to_string(reg)));
                            break;
                        }
                        case 5: { uint8_t b = r.readU8(); st.stack.push_back(symUnknown(b ? "true" : "false")); break; }
                        case 6: {
                            // SWF's DOUBLE push operand stores the two
                            // 32-bit words swapped relative to normal
                            // little-endian layout -- see Interpreter.cpp's
                            // readAvm1Double() for the full note; mirrored
                            // here (hi word first, then lo word).
                            uint32_t hi = r.readU32();
                            uint32_t lo = r.readU32();
                            uint64_t bits = (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
                            double d;
                            std::memcpy(&d, &bits, sizeof(d));
                            st.stack.push_back(symNumber(d));
                            break;
                        }
                        case 7: st.stack.push_back(symNumber(static_cast<double>(r.readS32()))); break;
                        case 8: {
                            uint8_t idx = r.readU8();
                            st.stack.push_back(idx < st.pool.size() ? symString(st.pool[idx]) : symUnknown("<pool?>"));
                            break;
                        }
                        case 9: {
                            uint16_t idx = r.readU16();
                            st.stack.push_back(idx < st.pool.size() ? symString(st.pool[idx]) : symUnknown("<pool?>"));
                            break;
                        }
                        default:
                            r.seek(r.size());
                            break;
                    }
                }
                break;
            }
            case avm1::ActionCode::StoreRegister: {
                swf::SwfReader r(operand, length);
                uint8_t reg = r.readU8();
                if (!st.stack.empty()) {
                    st.registers[reg] = st.stack.back();
                    st.registerSet[reg] = true;
                }
                break;
            }
            case avm1::ActionCode::ConstantPool: {
                swf::SwfReader r(operand, length);
                uint16_t count = r.readU16();
                std::vector<std::string> newPool;
                for (uint16_t i = 0; i < count && !r.failed(); ++i) newPool.push_back(r.readCString());
                st.pool = std::move(newPool);
                break;
            }
            case avm1::ActionCode::GetURL: {
                swf::SwfReader r(operand, length);
                std::string url = r.readCString();
                std::string target = r.readCString();
                emit(st, "GetURL url=\"" + url + "\" target=\"" + target + "\"");
                break;
            }
            case avm1::ActionCode::GetURL2: {
                Sym target = pop(st);
                Sym url = pop(st);
                emit(st, "GetURL2 url=" + url.text + " target=" + target.text);
                break;
            }
            // GotoFrame2/WaitForFrame2/Call are length-prefixed (operand
            // bytes carry flags/skip-count) but ALSO pop one value from the
            // AVM1 stack per Interpreter.cpp -- both effects, not just the
            // operand-byte one, or the symbolic stack desyncs from here on.
            case avm1::ActionCode::GotoFrame2: {
                Sym frame = pop(st);
                emit(st, "GotoFrame2 " + frame.text);
                break;
            }
            case avm1::ActionCode::WaitForFrame2:
                pop(st);
                break;
            case avm1::ActionCode::Call: {
                Sym frame = pop(st);
                emit(st, "Call " + frame.text);
                break;
            }
            // 2026-08-27 (Track A follow-up, docs/hobo-playability-
            // verification.md): If pops ONE value (the branch condition)
            // per Interpreter.cpp -- not modeling that pop here silently
            // desynced the symbolic stack for the REST of every action
            // stream containing an If (which is nearly every real
            // condition-gated script, e.g. `if (Key.isDown(...)) { ... }`
            // -- exactly the code this tool exists to read). This is still
            // a LINEAR pass (the branch itself is not followed/simulated,
            // so both arms print in sequence as if unconditional -- read
            // the surrounding emitted events as "what CAN happen here",
            // not "what happens in what order"), but the stack no longer
            // corrupts past this point. Jump has no AVM1-stack effect at
            // all (it only affects the byte-stream cursor, which a linear
            // pass ignores by design) -- explicitly a no-op here, not an
            // oversight.
            case avm1::ActionCode::If: {
                Sym cond = pop(st);
                emit(st, "If (" + cond.text + ")");
                break;
            }
            case avm1::ActionCode::Jump:
                break;
            case avm1::ActionCode::DefineFunction:
            case avm1::ActionCode::DefineFunction2:
                handleDefineFunction(code, operand, length, data, size, pos, st);
                break;
            // 2026-08-27 (Track A follow-up, frame-progression trace):
            // GotoFrame (0x81, the plain "gotoAndPlay(literalFrame)"/
            // "gotoAndStop(literalFrame)" compiled form -- a 2-byte UI16
            // frame-number operand baked directly into the bytecode, no
            // stack involvement at all) and SetTarget (0x8B, a static
            // string-operand target scope for whichever bare Play/Stop/
            // GotoFrame/NextFrame/PrevFrame follow it, as opposed to
            // SetTarget2's already-handled dynamic/stack form) were both
            // previously unhandled -- falling to this switch's own
            // `default: break` -- meaning this tool could not show root-
            // timeline (or any target's) frame navigation driven by the
            // classic untargeted/SetTarget-scoped compiled form at all,
            // only the OOP `_root.gotoAndStop(n)`-style CallMethod form
            // already handled above. Read GotoFrame's emitted frame number
            // together with the nearest preceding SetTarget in the SAME
            // event stream to know which timeline it addresses (this tool
            // does not track a persistent "current target" across
            // opcodes, so, exactly like the If/Jump caveat above, treat
            // this as "what CAN run here", not fully resolved).
            case avm1::ActionCode::GotoFrame: {
                if (length < 2) break;
                uint16_t frame = static_cast<uint16_t>(operand[0]) | (static_cast<uint16_t>(operand[1]) << 8);
                emit(st, "GotoFrame " + std::to_string(frame));
                break;
            }
            case avm1::ActionCode::SetTarget: {
                swf::SwfReader r(operand, length);
                std::string name = r.readCString();
                emit(st, "SetTarget \"" + name + "\"");
                break;
            }
            case avm1::ActionCode::With: {
                swf::SwfReader r(operand, length);
                uint16_t blockSize = r.readU16();
                if (pos + blockSize > size) break;
                Sym withObj = pop(st);
                DisasmState nested = st;
                nested.stack.clear();
                emit(st, "With (" + withObj.text + ")");
                walkActionStream(data + pos, blockSize, nested);
                pos += blockSize;
                break;
            }
            default:
                break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [keyword ...]\n", argv[0]);
        return 2;
    }
    std::vector<std::string> keywords;
    for (int i = 2; i < argc; ++i) {
        std::string k = argv[i];
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
        keywords.push_back(k);
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed: %s\n", movie ? movie->errorMessage.c_str() : "(null)");
        return 1;
    }

    std::vector<CollectedTag> allTags;
    collectTagsRecursive(*movie, movie->tags, 0, /*spriteCharacterId=*/0, allTags);

    std::vector<std::string> events;
    int doActionCount = 0, doInitActionCount = 0, clipActionCount = 0, buttonActionCount = 0;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
            // 2026-08-27 (Track A follow-up, frame-progression trace): a
            // THIRD kind of embedded-bytecode container this tool had
            // never scanned, alongside DoAction/DoInitAction (fixed
            // originally) and PlaceObject2's ClipActionRecord (fixed
            // above this segment). DefineButton/DefineButton2 carry their
            // click-handler script INSIDE THE CHARACTER DEFINITION ITSELF
            // (ButtonCondAction records for v2, one implicit OverDown->
            // OverUp block for v1) -- not in any DoAction tag or
            // PlaceObject2 clip-action record, so a real "PLAY button"
            // click handler (the plausible trigger for the root-timeline
            // frame1->4->10 gate found via the new GotoFrame/Play/Stop
            // emission above -- see docs/hobo-playability-verification.md)
            // would be completely invisible to every scan this tool has
            // done until now.
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto parsed = swf::parseDefineButton(r, ct.tag.code);
            if (!parsed) continue;
            if (!parsed->actionsV1.empty()) {
                DisasmState st;
                st.context = "DefineButton(v1) characterId=" + std::to_string(parsed->characterId) +
                              " (OverDown->OverUp)";
                st.eventsOut = &events;
                walkActionStream(parsed->actionsV1.data(), parsed->actionsV1.size(), st);
                buttonActionCount++;
            }
            for (size_t i = 0; i < parsed->condActionsV2.size(); ++i) {
                const auto& ca = parsed->condActionsV2[i];
                if (ca.actionBytes.empty()) continue;
                std::vector<std::string> condNames;
                auto condBit = [&](swf::ButtonCondition c, const char* name) {
                    if (ca.conditions & static_cast<uint16_t>(c)) condNames.push_back(name);
                };
                condBit(swf::ButtonCondition::kIdleToOverUp, "IdleToOverUp");
                condBit(swf::ButtonCondition::kOverUpToIdle, "OverUpToIdle");
                condBit(swf::ButtonCondition::kOverUpToOverDown, "OverUpToOverDown");
                condBit(swf::ButtonCondition::kOverDownToOverUp, "OverDownToOverUp(click/release)");
                condBit(swf::ButtonCondition::kOverDownToOutDown, "OverDownToOutDown");
                condBit(swf::ButtonCondition::kOutDownToOverDown, "OutDownToOverDown");
                condBit(swf::ButtonCondition::kOutDownToIdle, "OutDownToIdle");
                condBit(swf::ButtonCondition::kIdleToOverDown, "IdleToOverDown");
                condBit(swf::ButtonCondition::kOverDownToIdle, "OverDownToIdle");
                std::string condText;
                for (size_t c = 0; c < condNames.size(); ++c) {
                    if (c) condText += "|";
                    condText += condNames[c];
                }
                DisasmState st;
                st.context = "DefineButton2 characterId=" + std::to_string(parsed->characterId) +
                              " cond#" + std::to_string(i) + " (" + condText + ")" +
                              (ca.keyCode ? " key=" + std::to_string(*ca.keyCode) : "");
                st.eventsOut = &events;
                walkActionStream(ca.actionBytes.data(), ca.actionBytes.size(), st);
                buttonActionCount++;
            }
            continue;
        }
        if (code == swf::TagCode::PlaceObject2) {
            // 2026-08-27 (Track A follow-up, docs/hobo-playability-
            // verification.md): this static tool previously only walked
            // DoAction/DoInitAction tag bodies -- it never looked inside
            // PlaceObject2's own optional CLIPACTIONRECORD section, which
            // is exactly where an `onClipEvent(enterFrame) { ... }`
            // handler's bytecode lives (confirmed live/dispatching in the
            // real interpreter per Phase 6 -- see CLAUDE.md). That gap is
            // why a prior run of this tool found zero "isDown"/"Key"
            // occurrences in hobo.swf despite the dynamic call trace
            // showing Key.isDown() firing every tick: the polling code was
            // never being scanned at all, not merely mis-disassembled.
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto parsed = swf::parsePlaceObject(r, ct.tag.code, movie->version);
            if (!parsed) continue;
            // 2026-08-27 (Hobo1 "disassemble root frame 2's unidentified
            // clips" follow-up, Step 1): unconditionally log every
            // placement's characterId/depth/name/enclosing sprite, even
            // with no clip actions -- needed to answer "what characters
            // does sprite X actually place inside itself", which the
            // clip-action-only scan above can't answer (most placements
            // carry no onClipEvent at all, but still matter for finding
            // e.g. a button nested inside a menu sprite).
            if (parsed->characterId) {
                events.push_back("[placement enclosingSpriteCharId=" + std::to_string(ct.spriteCharacterId) +
                                  "] depth=" + std::to_string(parsed->depth) + " characterId=" +
                                  std::to_string(*parsed->characterId) +
                                  (parsed->name ? " name=\"" + *parsed->name + "\"" : ""));
            }
            if (parsed->clipActions.empty()) continue;
            for (size_t i = 0; i < parsed->clipActions.size(); ++i) {
                const auto& rec = parsed->clipActions[i];
                if (rec.actionBytes.empty()) continue;
                std::vector<std::string> flagNames;
                auto flagBit = [&](swf::ClipEventFlag f, const char* name) {
                    if (rec.eventFlags & static_cast<uint32_t>(f)) flagNames.push_back(name);
                };
                flagBit(swf::ClipEventFlag::kLoad, "Load");
                flagBit(swf::ClipEventFlag::kEnterFrame, "EnterFrame");
                flagBit(swf::ClipEventFlag::kUnload, "Unload");
                flagBit(swf::ClipEventFlag::kMouseMove, "MouseMove");
                flagBit(swf::ClipEventFlag::kMouseDown, "MouseDown");
                flagBit(swf::ClipEventFlag::kMouseUp, "MouseUp");
                flagBit(swf::ClipEventFlag::kKeyDown, "KeyDown");
                flagBit(swf::ClipEventFlag::kKeyUp, "KeyUp");
                flagBit(swf::ClipEventFlag::kData, "Data");
                flagBit(swf::ClipEventFlag::kInitialize, "Initialize");
                flagBit(swf::ClipEventFlag::kPress, "Press");
                flagBit(swf::ClipEventFlag::kRelease, "Release");
                flagBit(swf::ClipEventFlag::kReleaseOutside, "ReleaseOutside");
                flagBit(swf::ClipEventFlag::kRollOver, "RollOver");
                flagBit(swf::ClipEventFlag::kRollOut, "RollOut");
                flagBit(swf::ClipEventFlag::kDragOver, "DragOver");
                flagBit(swf::ClipEventFlag::kDragOut, "DragOut");
                flagBit(swf::ClipEventFlag::kKeyPress, "KeyPress");
                flagBit(swf::ClipEventFlag::kConstruct, "Construct");
                std::string flagsText;
                for (size_t f = 0; f < flagNames.size(); ++f) {
                    if (f) flagsText += "|";
                    flagsText += flagNames[f];
                }
                std::string label = "onClipEvent(" + flagsText + ") depth=" +
                                     std::to_string(parsed->depth) +
                                     (parsed->name ? " name=\"" + *parsed->name + "\"" : "") +
                                     " charId=" +
                                     (parsed->characterId ? std::to_string(*parsed->characterId) : "?") +
                                     " record#" + std::to_string(i) + " (treeDepth=" +
                                     std::to_string(ct.depth) + ", enclosingSpriteCharId=" +
                                     std::to_string(ct.spriteCharacterId) + ")";
                DisasmState st;
                st.context = label;
                st.eventsOut = &events;
                walkActionStream(rec.actionBytes.data(), rec.actionBytes.size(), st);
                clipActionCount++;
            }
            continue;
        }
        if (code != swf::TagCode::DoAction && code != swf::TagCode::DoInitAction) continue;
        swf::SwfReader r = movie->tagBodyReader(ct.tag);
        std::string label;
        if (code == swf::TagCode::DoInitAction) {
            uint16_t characterId = r.readU16();
            label = "DoInitAction(characterId=" + std::to_string(characterId) + ", depth=" +
                    std::to_string(ct.depth) + ", enclosingSpriteCharId=" +
                    std::to_string(ct.spriteCharacterId) + ")";
            doInitActionCount++;
        } else {
            label = "DoAction(depth=" + std::to_string(ct.depth) + ", tagOffset=" +
                     std::to_string(ct.tag.bodyOffset) + ", enclosingSpriteCharId=" +
                     std::to_string(ct.spriteCharacterId) + ")";
            doActionCount++;
        }
        DisasmState st;
        st.context = label;
        st.eventsOut = &events;
        // tagBodyReader() returns a reader scoped to [bodyOffset,
        // bodyOffset+bodyLength) with position() relative to that span (0
        // at the tag body's own start), so the action stream's real base
        // address is bodyOffset + whatever header fields were already
        // consumed (2 bytes of characterId for DoInitAction, nothing for
        // DoAction), not r.position() alone (which would silently
        // underflow into the movie's absolute-offset-0 bytes instead).
        const uint8_t* base = movie->data.data() + ct.tag.bodyOffset + r.position();
        size_t remaining = ct.tag.bodyLength - r.position();
        walkActionStream(base, remaining, st);
    }

    std::printf("=== %s ===\n", argv[1]);
    std::printf(
        "DoAction tags: %d, DoInitAction tags: %d, ClipActionRecords: %d, ButtonCondActions: %d, "
        "total events: %zu\n\n",
        doActionCount, doInitActionCount, clipActionCount, buttonActionCount, events.size());

    int printedCount = 0;
    for (const auto& e : events) {
        bool match = keywords.empty();
        if (!match) {
            std::string lower = e;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            for (const auto& k : keywords) {
                if (lower.find(k) != std::string::npos) { match = true; break; }
            }
        }
        if (match) {
            std::printf("%s\n", e.c_str());
            printedCount++;
        }
    }
    std::printf("\n(printed %d of %zu events%s)\n", printedCount, events.size(),
                keywords.empty() ? "" : ", filtered by keyword");
    return 0;
}
