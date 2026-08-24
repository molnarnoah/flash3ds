// CharacterDictionary.h
//
// Resolves SWF character IDs (as referenced by DisplayListEntry::characterId)
// to their definitions. Phase 3 added two character kinds: shapes
// (DefineShape/2/3) and sprites (DefineSprite — a nested, independently
// timed mini-movie). Phase 6 added a third: sounds (DefineSound — see
// swf/DefineSoundTag.h; structural header fields only, no codec decode).
// Phase 8 added four more: fonts (DefineFont/DefineFont2 — swf/
// DefineFontTag.h), static text (DefineText/DefineText2 — swf/
// DefineTextTag.h), buttons (DefineButton/DefineButton2 — swf/
// DefineButtonTag.h), and dynamic/input text fields (DefineEditText — swf/
// DefineEditTextTag.h; parsed structurally only, see that header). Bitmap
// characters (DefineBits*) are still recognized by tag but not parsed
// (later phase; see docs/swf-support.md).
//
// DefineSprite's body is CharacterId, FrameCount, then a *nested* tag
// stream (its own ShowFrame/PlaceObject*/RemoveObject*/DoAction/... control
// tags, terminated by an End tag) living inside that same DefineSprite tag's
// body bytes. Critically, that nested stream is scanned here using
// *absolute* offsets into Movie::data (by seeking a reader into the shared
// buffer rather than constructing an isolated sub-reader), so the resulting
// TagRecords are directly usable with Movie::tagBodyReader — and so
// Timeline::build() doesn't need to know or care whether it's building a
// movie's main timeline or a sprite's nested one.
//
// --- Lazy/on-demand parsing (Roadmap Phase 5, "RAM Option B", 2026-08-24) ---
//
// build() used to eagerly parse every character-defining tag's full body
// (geometry, glyph outlines, TEXTRECORD runs, ...) up front. Measured cost
// (docs/memory-audit.md §5-5c): CharacterDictionary::build() is the
// dominant contributor to this runtime's peak RSS (~90%+ of growth for
// every real corpus file measured), and the overwhelming majority of that
// is std::vector<ShapeRecord> geometry that a typical session may never
// actually touch (e.g. off-screen/rarely-placed sprite children).
//
// build() now only PEEKS each character-defining tag's leading CharacterId
// field (every one of DefineShape/2/3, DefineSound, DefineFont, DefineFont2,
// DefineText/2, DefineButton/2, and DefineEditText begins with that field
// as its very first UI16 per the public SWF spec — confirmed by direct
// source read of each tag's own parser) and stores a `pending_` entry
// {code, bodyOffset, bodyLength} — no geometry/outline/text-run parsing
// happens at this point. The real parse (calling into
// swf::parseDefineShape/parseDefineFont/etc.) happens lazily, on first
// find(), and the result is cached in `parsed_` so a second find() for the
// same character is a plain hash-map lookup, not a re-parse.
//
// DefineSprite is the one exception, kept eager: constructing a SpriteDef
// is already cheap (it stores TagRecord offset/length for its nested
// control tags, not a byte copy — see SpriteDef's own comment below) and
// build() must recurse into a sprite's nested tag stream regardless, to
// discover any character-defining tags nested inside it (legal per spec,
// see scanTagsForCharacters()'s own comment) — so there is no laziness win
// available for sprites themselves, only for what they might reference.
//
// find() remains a `const` method — every call site in this codebase holds
// a `const CharacterDictionary&`/`const CharacterDictionary*` (SceneRenderer,
// MovieClipInstance, ButtonInstance, CharacterBounds — grepped and
// confirmed before this change) and was written assuming find() is
// synchronous/cheap. Lazy-parse-and-cache-on-first-call preserves that
// call-site contract exactly (the mutation is an internal cache fill, not
// an observable state change) via `mutable parsed_`/`mutable pending_`.
//
// A linkage-name lookup (findByLinkageName(), used by attachMovie()/
// attachSound()) does NOT force a parse — it only resolves a name to a
// character ID, exactly as before; the actual parse still waits for a real
// find() call (i.e. actual placement/render/attachSound use), per this
// phase's own design goal of not eagerly materializing anything a session
// doesn't end up touching.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

#include "runtime/Movie.h"
#include "swf/DefineButtonTag.h"
#include "swf/DefineEditTextTag.h"
#include "swf/DefineFontTag.h"
#include "swf/DefineShapeTag.h"
#include "swf/DefineSoundTag.h"
#include "swf/DefineTextTag.h"
#include "swf/TagDispatcher.h"

namespace flash3ds::runtime {

struct SpriteDef {
    uint16_t characterId = 0;
    uint16_t frameCount = 0;
    std::vector<swf::TagRecord> tags;  // nested control tags; bodyOffset is absolute into Movie::data
};

using CharacterDef = std::variant<swf::ShapeDef, SpriteDef, swf::SoundDef, swf::FontDef,
                                   swf::TextDef, swf::ButtonDef, swf::EditTextDef>;

class CharacterDictionary {
public:
    // Scans `movie`'s top-level tags (recursing into nested DefineSprite
    // tag streams) for character-defining tags. DefineSprite bodies are
    // parsed immediately (cheap — see file header); every other kind is
    // only indexed by {tag code, offset, length} here and actually parsed
    // on first find() — see this file's header comment for the full
    // rationale. Tags whose leading CharacterId can't even be peeked
    // (truncated body) are logged and skipped, same as a full parse
    // failure was handled before this phase.
    static CharacterDictionary build(const Movie& movie);

    // Resolves `characterId`. First checks the already-parsed cache; on a
    // miss, checks the pending index and — if found — parses the tag body
    // now, caches the result, and returns it. Returns nullptr if
    // `characterId` was never seen by build() at all, or if its tag failed
    // to parse (logged when that happens, exactly as it was pre-Phase-5).
    const CharacterDef* find(uint16_t characterId) const;

    // Total distinct character IDs build() discovered (parsed + still
    // pending) — NOT the count of characters actually materialized so far.
    // Matches this method's pre-Phase-5 meaning (existing tests assert on
    // it as "how many characters did build() find", not "how many are
    // resident right now") — see test_character_dictionary.cpp.
    size_t size() const { return pending_.size() + parsed_.size(); }

    // Roadmap Phase 4 (2026-08-21, ExportAssets/dynamic instantiation —
    // see docs/known-limitations.md L4): resolves a library symbol's
    // linkage name (as set in the Flash IDE's "Linkage" properties, and
    // referenced by AS2's MovieClip.attachMovie(linkageName, ...)/
    // Sound.attachSound(linkageName)) to the character ID an
    // `ExportAssets` tag associated it with. Returns nullptr if `name`
    // has no linkage entry (not exported, or this file exports nothing —
    // the overwhelmingly common case; every Hobo file exports zero
    // symbols, so this is a no-cost lookup for them). Populated by
    // build() scanning for ExportAssets tags exactly like every other
    // character-defining tag. Does NOT force-parse the target character —
    // see this file's header comment.
    const uint16_t* findByLinkageName(const std::string& name) const;

private:
    const Movie* movie_ = nullptr;
    // {tag code, offset, length} for every discovered-but-not-yet-parsed
    // character. `mutable` because find() erases an entry here the moment
    // it promotes that character into `parsed_`, and find() is const (see
    // this file's header comment on why that contract is preserved).
    mutable std::unordered_map<uint16_t, swf::TagRecord> pending_;
    mutable std::unordered_map<uint16_t, CharacterDef> parsed_;
    std::unordered_map<std::string, uint16_t> linkageNameToId_;

    // Actually parses `tag` (whose code determines which swf::parseDefineX
    // function to call) into a CharacterDef, logs and returns nullptr on
    // failure. Factored out of find() for readability only.
    const CharacterDef* parseAndCache(uint16_t characterId, const swf::TagRecord& tag) const;
};

}  // namespace flash3ds::runtime
