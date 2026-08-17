// Stack.h
//
// The AVM1 operand stack. Every operation is safe on an empty stack (pop()/
// peek() return Value::undefined() rather than reading out of bounds or
// asserting) — malformed/adversarial bytecode that pops more than it
// pushed must degrade gracefully, matching the project's "never crash on
// untrusted input" principle (see docs/architecture.md).

#pragma once

#include <utility>
#include <vector>

#include "avm1/Value.h"

namespace flash3ds::avm1 {

class Stack {
public:
    void push(Value v) { values_.push_back(std::move(v)); }

    Value pop() {
        if (values_.empty()) return Value::undefined();
        Value v = std::move(values_.back());
        values_.pop_back();
        return v;
    }

    Value peek() const { return values_.empty() ? Value::undefined() : values_.back(); }

    size_t size() const { return values_.size(); }
    bool empty() const { return values_.empty(); }
    void clear() { values_.clear(); }

    // ActionPushDuplicate: duplicates the top value. No-op on an empty stack.
    void duplicateTop() {
        if (!values_.empty()) values_.push_back(values_.back());
    }

    // ActionStackSwap: swaps the top two values. No-op if fewer than 2.
    void swapTop() {
        if (values_.size() >= 2) {
            std::swap(values_[values_.size() - 1], values_[values_.size() - 2]);
        }
    }

private:
    std::vector<Value> values_;
};

}  // namespace flash3ds::avm1
