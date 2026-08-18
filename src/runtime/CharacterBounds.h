// CharacterBounds.h
//
// Small, shared bounding-box helpers originally written inline (anonymous
// namespace) inside MovieClipInstance.cpp for the interactivity-audit
// phase's _width/_height fix. Extracted into their own translation unit
// (ButtonInstance phase, 2026-08-19) so BOTH MovieClipInstance.cpp and the
// new ButtonInstance.cpp can call characterOwnBoundsRect() — in particular
// its existing, already-correct ButtonDef handling (unions the HitTest-
// state records' geometry, falling back to Up-state records if no explicit
// HitTest state exists) — WITHOUT duplicating that logic. See
// docs/hit-testing.md and docs/buttons.md.
//
// Pure geometry, no MovieClipInstance/ButtonInstance dependency — these
// functions only need a CharacterDef + CharacterDictionary (to resolve a
// button record's nested characterId).

#pragma once

#include "runtime/CharacterDictionary.h"
#include "swf/SwfRecords.h"

namespace flash3ds::runtime {

// The neutral/"nothing here" element for unionBoundsRect() — detected via
// the classic "inverted" empty-rect convention (xMin > xMax) rather than a
// separate bool flag, analogous to swf::ColorTransform::identity() being
// the neutral element for concatColorTransform.
swf::Rect emptyBoundsRect();

bool isEmptyBoundsRect(const swf::Rect& r);

swf::Rect unionBoundsRect(const swf::Rect& a, const swf::Rect& b);

// Resolves a LEAF character's own (untransformed) bounds — the same "which
// CharacterDef alternative is this" dispatch renderer::SceneRenderer uses,
// but returning a bounding Rect instead of drawing:
//   - ShapeDef/TextDef/EditTextDef: use the tag's own already-parsed
//     bounds RECT directly.
//   - ButtonDef: unions the HitTest-state records' underlying character
//     bounds (falling back to Up-state records if the button defines no
//     explicit HitTest state at all — matches real Flash, which requires
//     an author-supplied hit area but tolerates its absence by using
//     Up-state geometry). Only resolves ONE level deep (a button record
//     referencing a Shape/Text/EditText) — a button record referencing a
//     nested Sprite for its hit area is skipped rather than recursed into
//     (see docs/hit-testing.md).
//   - SpriteDef/SoundDef/FontDef: empty (sprites resolve to a
//     MovieClipInstance child instead; Sound/Font are never placeable).
//   - Bitmap/DefineMorphShape characters don't resolve into
//     CharacterDictionary at all yet, so `def` itself would be null for
//     those — never reaches this function.
swf::Rect characterOwnBoundsRect(const CharacterDef& def, const CharacterDictionary& characters);

}  // namespace flash3ds::runtime
