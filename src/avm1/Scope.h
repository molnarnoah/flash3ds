// Scope.h
//
// AVM1's variable scope chain: an ordered list of Objects (innermost
// activation record first, outermost `_global`-equivalent object last).
// `var x = ...` (DefineLocal/DefineLocal2) always creates a property on the
// innermost object; plain assignment (SetVariable) walks the chain to
// update an existing binding if one exists, otherwise falls back to
// creating it on the innermost object — matching real AS2 lexical scoping
// for functions/closures.
//
// NOTE (Phase 4 scope, pun intended): this models the *function-local*
// scope chain only. Phase 4 has no MovieClip/timeline object yet, so the
// outermost object here is just a plain Object standing in for what will
// become the movie's/clip's variable bag once Phase 5 wires AVM1 into the
// scene graph (see docs/avm1-support.md).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "avm1/Value.h"

namespace flash3ds::avm1 {

class Scope {
public:
    // `outer` is the enclosing scope chain (e.g. a closure's captured
    // scope, or empty for the top-level script). A single fresh object is
    // pushed as the new innermost frame.
    explicit Scope(std::vector<std::shared_ptr<Object>> chain) : chain_(std::move(chain)) {}

    // Convenience: a scope containing just one object (e.g. the top-level
    // global object for a fresh script).
    static Scope topLevel(std::shared_ptr<Object> global) {
        return Scope(std::vector<std::shared_ptr<Object>>{std::move(global)});
    }

    // Returns a NEW Scope with `frame` pushed as the innermost object,
    // ahead of everything in this chain (used when calling into a function:
    // its activation record becomes the new innermost frame, in front of
    // its captured closure chain).
    Scope pushed(std::shared_ptr<Object> frame) const {
        std::vector<std::shared_ptr<Object>> newChain;
        newChain.reserve(chain_.size() + 1);
        newChain.push_back(std::move(frame));
        newChain.insert(newChain.end(), chain_.begin(), chain_.end());
        return Scope(std::move(newChain));
    }

    // Never UB on an empty chain (shouldn't happen via the public
    // constructors above, but defensive per the project's "never crash"
    // principle) — returns a shared null pointer instead of dereferencing
    // past the end of an empty vector.
    const std::shared_ptr<Object>& innermost() const {
        static const std::shared_ptr<Object> kNone;
        return chain_.empty() ? kNone : chain_.front();
    }
    const std::vector<std::shared_ptr<Object>>& chain() const { return chain_; }

    // GetVariable semantics: search innermost-to-outermost for an existing
    // own property named `name`; return the first match, or undefined if
    // never found anywhere in the chain.
    Value getVariable(const std::string& name) const;

    // SetVariable semantics: if `name` already exists as an own property
    // somewhere in the chain, update it there; otherwise create it as a new
    // own property on the innermost object.
    void setVariable(const std::string& name, Value value);

    // DefineLocal semantics ("var x = value"): always creates/overwrites an
    // own property on the innermost object, regardless of whether the name
    // exists further up the chain.
    void defineLocal(const std::string& name, Value value);

    // Delete2 semantics: removes `name` from whichever scope object (if
    // any) actually owns it. Returns true if something was deleted.
    bool deleteVariable(const std::string& name);

private:
    std::vector<std::shared_ptr<Object>> chain_;
};

}  // namespace flash3ds::avm1
