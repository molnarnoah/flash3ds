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
// Phase 5 (RAM Option B — lazy/on-demand parsing, 2026-08-24): build() no
// longer parses every character's shape/font/etc. payload up front. It
// only scans for character-defining tags (cheap — a single UI16 read per
// tag, since every character-defining tag's body begins with its own
// CharacterId/SpriteID/FontID/etc. as the first field, per the public SWF
// spec) and records each one's TagRecord in `pendingCharacters_`. The
// actual type-specific parse (parseDefineShape/parseDefineSound/etc.) only
// runs the FIRST time find() is called for that character ID — see
// find()'s own doc comment and docs/memory-audit.md §10 for the measured
// RAM impact. This is a behavior-preserving change from every EXTERNAL
// caller's point of view: find() still returns a fully-parsed
// `const CharacterDef*` (or nullptr for an unknown ID) synchronously, with
// identical content to what the old eager build() would have produced —
// only WHEN the parse work happens changed, not what it produces. No call
// site (SceneRenderer.cpp, MovieClipInstance.cpp, ButtonInstance.cpp,
// CharacterBounds.cpp — every find() call site as of this phase) needed
// any change.
//
// DefineSprite's body is CharacterId, FrameCount, then a *nested* tag
// stream (its own ShowFrame/PlaceObject*/RemoveObject*/DoAction/... control
// tags, terminated by an End tag) living inside that same DefineSprite tag's
// body bytes. Critically, that nested stream is scanned using *absolute*
// offsets into Movie::data (by seeking a reader into the shared buffer
// rather than constructing an isolated sub-reader), so the resulting
// TagRecords are directly usable with Movie::tagBodyReader — and so
// Timeline::build() doesn't need to know or care whether it's building a
// movie's main timeline or a sprite's nested one. SWF's character ID
// dictionary is GLOBAL across the whole file, so a character defined nested
// inside a DefineSprite's own tag stream must still be independently
// find()-able — build()'s scan recurses into every DefineSprite's nested
// tags to register those IDs too, even though (Phase 5) it does not build
// their full CharacterDef payload at scan time either. This means a
// DefineSprite's own nested-tag walk runs twice in the worst case (once
// during build()'s scan, purely to discover further nested character IDs;
// once more on the sprite's own first find() call, to build its real,
// cached SpriteDef) — a small, bounded, deliberate cost, not an oversight;
// see CharacterDictionary.cpp's scanTagsForCharacters()/parseOneCharacter()
// for exactly where each walk happens.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
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
#include "swf/TagCode.h"
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
    // Scans `movie`'s top-level tags (recursing into every DefineSprite's
    // own nested tag stream) for character-defining tags and records each
    // one's TagRecord + resolved tag code, keyed by character ID — see this
    // file's header comment (Phase 5) for why this does NOT parse each
    // character's actual payload. Tags whose character-ID field can't even
    // be read (truncated/malformed) are logged and skipped, exactly like
    // the old eager design did for a full-parse failure.
    static CharacterDictionary build(const Movie& movie);

    // Returns the fully-parsed definition for `characterId`, parsing it
    // (and caching the result) on this call if this is the first
    // reference — see this file's header comment. `const` is preserved
    // (every existing caller treats CharacterDictionary as read-only) via
    // `mutable` cache/pending members; this is the standard "logically
    // const, physically caches" pattern, not a design compromise. Returns
    // nullptr for a character ID that was never registered by build(), or
    // whose lazy parse just failed (logged once, at that point — see
    // CharacterDictionary.cpp) — identical external behavior to the old
    // eager design either way.
    const CharacterDef* find(uint16_t characterId) const;

    // Total DISTINCT character IDs build() discovered, whether or not
    // find() has actually been called for each one yet — i.e. pending +
    // already-parsed. Existing callers/tests that asserted this against
    // the old eager design's character count are unaffected (a
    // registered-but-never-yet-parsed character still counts).
    size_t size() const { return pendingCharacters_.size() + parsedCharacters_.size(); }

    // Phase 5 diagnostic (2026-08-24): how many of `size()`'s characters
    // have actually been parsed-on-demand so far via find() — i.e. how
    // many were genuinely referenced (placed or looked up) this session.
    // Exists so RAM-measurement tooling (tools/mem_profile_check) can
    // report "N of M characters ever touched" directly, instead of that
    // number only being inferable indirectly — see docs/memory-audit.md
    // §10 for how this phase used it.
    size_t parsedCount() const { return parsedCharacters_.size(); }

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
    // character-defining tag. Deliberately does NOT force-parse the
    // target character (Phase 5) — resolving a name to an ID is not by
    // itself a "reference" in the placement/render sense; the caller's
    // own subsequent find(id) call is what triggers the lazy parse, same
    // as any other character ID it might have obtained another way.
    const uint16_t* findByLinkageName(const std::string& name) const;

private:
    // A character-defining tag build()'s scan has seen but not yet
    // parsed. Deliberately just the TagRecord + its already-resolved
    // TagCode (so find()'s lazy parse doesn't need to re-derive the type
    // from tag.code) — no payload bytes, matching how SoundDef/SpriteDef
    // already only stored offset/length before this phase.
    struct PendingCharacter {
        swf::TagRecord tag;
        swf::TagCode code = swf::TagCode::End;
    };

    // These are private *static* helpers (not free functions in an
    // anonymous namespace, unlike the old eager .cpp's style) because they
    // need to write directly into a CharacterDictionary's private
    // pendingCharacters_/linkageNameToId_ maps (scanTagsForCharacters) or
    // name the private PendingCharacter type (parseOneCharacter) — a
    // non-member function has no access to either, regardless of what
    // reference/pointer type it's handed. See CharacterDictionary.cpp for
    // the implementations and per-tag-type dispatch details.

    // Scans one tag list (either the movie's own top-level tags, or a
    // DefineSprite's nested control-tag stream, via recursion) for
    // character-defining tags, recording each as a PendingCharacter keyed
    // by character ID — see this file's header comment (Phase 5) for why
    // this only reads each tag's leading UI16 ID field rather than fully
    // parsing it. Also populates `linkageNameToId` from any ExportAssets
    // tags found, exactly as build() always has.
    static void scanTagsForCharacters(const Movie& movie, const std::vector<swf::TagRecord>& tags,
                                       std::unordered_map<uint16_t, PendingCharacter>& pending,
                                       std::unordered_map<std::string, uint16_t>& linkageNameToId);

    // Walks a DefineSprite tag's nested control-tag stream (ShowFrame/
    // PlaceObject*/RemoveObject*/DoAction/.../End), starting just past its
    // CharacterId+FrameCount header, and returns the resulting TagRecords.
    // Uses *absolute* offsets into Movie::data (see this file's header
    // comment), so the result is directly usable with
    // Movie::tagBodyReader()/Timeline either way. Shared by
    // scanTagsForCharacters() (to recurse into nested character IDs at
    // scan time — the walk's result is discarded once IDs are registered)
    // and parseOneCharacter() (to build a DefineSprite's real, cached
    // SpriteDef::tags on its first find() call) so the walk logic itself
    // is written exactly once.
    static std::vector<swf::TagRecord> walkSpriteTagStream(const Movie& movie,
                                                             const swf::TagRecord& spriteTag,
                                                             uint16_t characterId);

    // Fully parses one pending character's payload, dispatching on `code`
    // to the same per-type parser the old eager build() called
    // (parseDefineShape/parseDefineSound/.../walkSpriteTagStream for
    // DefineSprite). Returns std::nullopt (after logging, same message
    // text as the old eager code) if the parse fails — find() treats that
    // identically to an unregistered character ID.
    static std::optional<CharacterDef> parseOneCharacter(const Movie& movie, const swf::TagRecord& tag,
                                                           swf::TagCode code);

    mutable std::unordered_map<uint16_t, PendingCharacter> pendingCharacters_;
    mutable std::unordered_map<uint16_t, CharacterDef> parsedCharacters_;
    std::unordered_map<std::string, uint16_t> linkageNameToId_;
    // Non-owning — see MovieClipInstance.h/SceneRenderer.h's own
    // movie_/characters_ pointer pair for the same, already-established
    // "these two co-live for the whole session" convention this reuses.
    // Needed here (new in Phase 5) so find()'s lazy parse can read a
    // pending character's body via movie_->tagBodyReader(tag) without
    // every call site having to additionally pass the Movie back in.
    const Movie* movie_ = nullptr;
};

}  // namespace flash3ds::runtime
