// SceneRenderer.h
//
// Walks a MovieClipInstance tree (see runtime/MovieClipInstance.h, Phase 5)
// and draws it into an IRenderer: resolves each display-list entry's
// characterId via CharacterDictionary, composes world transforms
// (concatMatrix, parent-then-child per SWF's nested coordinate space
// convention), tessellates shape characters on the fly, and recurses into
// child MovieClipInstances at THEIR OWN (possibly script-mutated)
// localMatrix()/colorTransform(), skipping any that are !visible().
//
// Phase 3 rendered sprites by walking a shared, per-CHARACTER cached
// Timeline — every placed instance of a given sprite character rendered
// identically, with no independent playhead. Phase 5 replaces that with the
// actual MovieClipInstance tree, so two instances of the same sprite
// character can be on different frames, at different _x/_y/_alpha, etc.,
// and the renderer reflects it correctly.
//
// Phase 8 added three more leaf character kinds, all resolved through the
// same `renderCharacter()` entry point `renderClip()` already used for
// shapes: static text (swf::TextDef — glyph runs drawn by looking up each
// glyph's outline in its swf::FontDef and reusing ShapeTessellator, scaled
// by textHeight/1024 — see renderGlyph()), buttons (swf::ButtonDef — draws
// only the "Up" state's records, since there's no mouse hit-testing/state
// machine yet to pick a different state; see docs/avm1-support.md's Known
// Phase 8 limitations), and dynamic/input text fields (swf::EditTextDef —
// only rendered when it embeds a font AND that font has a code table, i.e.
// a DefineFont2 font; no word-wrap/scrolling/alignment, see
// renderEditTextCharacter()).
//
// Compatibility-audit phase (2026-08-18): every render entry point now also
// threads an EFFECTIVE (world-composed, via swf::concatColorTransform)
// swf::ColorTransform alongside worldMatrix, applied (via
// swf::applyColorTransform) to each leaf's actual fill/stroke/glyph color
// immediately before handing it to IRenderer. Before this, ColorTransform
// (including AVM1 `_alpha`) was parsed, stored per-MovieClipInstance, and
// script-mutable, but never once read by the renderer — a real, confirmed
// gap found by tracing execution (not assuming from prior docs); see
// docs/known-limitations.md.

#pragma once

#include <unordered_map>

#include "renderer/IRenderer.h"
#include "renderer/ShapeTessellator.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
#include "runtime/MovieClipInstance.h"
#include "runtime/Movie.h"
#include "swf/DefineButtonTag.h"
#include "swf/DefineEditTextTag.h"
#include "swf/DefineFontTag.h"
#include "swf/DefineMorphShapeTag.h"
#include "swf/DefineTextTag.h"

namespace flash3ds::renderer {

class SceneRenderer {
public:
    SceneRenderer(const runtime::Movie& movie, const runtime::CharacterDictionary& characters);

    // Renders `root`'s current state into `target`, mapping the movie's
    // stage bounds (Movie::frameSize, in twips) onto a `outputWidthPixels`
    // x `outputHeightPixels` device viewport. Calls target.beginFrame()/
    // endFrame() itself — the caller should not.
    void render(const runtime::MovieClipInstance& root, IRenderer& target, int outputWidthPixels,
                int outputHeightPixels);

private:
    void renderClip(const runtime::MovieClipInstance& clip, const swf::Matrix& worldMatrix,
                     const swf::ColorTransform& worldColorTransform, IRenderer& target,
                     double pixelsPerTwipX, double pixelsPerTwipY, int depth);

    // Resolves `characterId` and dispatches to the right leaf renderer
    // below based on which CharacterDef alternative it is; a SpriteDef here
    // means renderClip() should have handled it via a MovieClipInstance
    // child instead (see renderClip()'s doc comment), so it's a no-op, same
    // as an unresolved/still-unsupported (bitmap) character ID.
    void renderCharacter(uint16_t characterId, const swf::Matrix& worldMatrix,
                          const swf::ColorTransform& worldColorTransform, IRenderer& target,
                          double pixelsPerTwipX, double pixelsPerTwipY, int depth);

    void renderShapeCharacter(const swf::ShapeDef& shapeDef, const swf::Matrix& worldMatrix,
                               const swf::ColorTransform& worldColorTransform, IRenderer& target,
                               double pixelsPerTwipX, double pixelsPerTwipY);

    // Roadmap Phase 9 (2026-08-25): synthesizes a swf::Shape from
    // `morphDef`'s START-side geometry/styles only (ratio=0) and renders it
    // exactly like renderShapeCharacter() — a deliberate, roadmap-approved
    // simplification, not full morph-tween support. Real-corpus evidence
    // (tools/real_game_harness/morph_ratio_scan.cpp) confirmed every morph
    // placement in this corpus uses ratio=0 (explicit or absent), so this
    // is exactly correct for the games this runtime targets, not just an
    // approximation of convenience. See swf/DefineMorphShapeTag.h.
    void renderMorphShapeCharacter(const swf::MorphShapeDef& morphDef, const swf::Matrix& worldMatrix,
                                    const swf::ColorTransform& worldColorTransform, IRenderer& target,
                                    double pixelsPerTwipX, double pixelsPerTwipY);

    void renderTextCharacter(const swf::TextDef& textDef, const swf::Matrix& worldMatrix,
                              const swf::ColorTransform& worldColorTransform, IRenderer& target,
                              double pixelsPerTwipX, double pixelsPerTwipY);

    void renderEditTextCharacter(const swf::EditTextDef& editTextDef,
                                  const swf::Matrix& worldMatrix,
                                  const swf::ColorTransform& worldColorTransform, IRenderer& target,
                                  double pixelsPerTwipX, double pixelsPerTwipY);

    // Draws one glyph outline (`glyphShape`, in the font's raw 1024-
    // units-per-em space — see swf/DefineFontTag.h) filled with `color`
    // (after `worldColorTransform` is applied to it), scaled by `scale`
    // (== textHeightTwips / 1024.0), translated by (offsetXTwips,
    // offsetYTwips) in the SAME local space `worldMatrix` expects (i.e.
    // already-scaled glyph units, not yet transformed).
    void renderGlyph(const swf::Shape& glyphShape, const swf::RgbaColor& color, double scale,
                      int32_t offsetXTwips, int32_t offsetYTwips, const swf::Matrix& worldMatrix,
                      const swf::ColorTransform& worldColorTransform, IRenderer& target,
                      double pixelsPerTwipX, double pixelsPerTwipY);

    const runtime::Movie* movie_;
    const runtime::CharacterDictionary* characters_;

    // Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task --
    // see docs/performance-pacing.md for the measured evidence this was
    // built on): renderShapeCharacter() used to call tessellateShape()
    // fresh on every single render() call for every placed shape, even
    // though a swf::ShapeDef's geometry is immutable once parsed (no
    // drawing API exists anywhere in this runtime that could mutate a
    // Shape's edges/styles post-parse) and tessellation is done entirely
    // in the shape's own local twip space (world transform is applied
    // separately, AFTER tessellation, in toDevicePolyline) -- so the same
    // shapeDef always tessellates to the exact same TessellatedShape,
    // frame after frame, whether or not anything on screen actually
    // changed. On-device phase timing showed this was the dominant cost
    // by far (roughly 60% of every real frame, even on a static screen).
    // Cached here, keyed by the address of the shapeDef's own swf::Shape
    // (stable for the CharacterDictionary's lifetime: parsedCharacters_
    // is a std::unordered_map, which never invalidates existing elements'
    // addresses on insertion -- only iterators -- and entries are never
    // erased, per the M2/Roadmap-Phase-6 "no eviction" decision already
    // documented in docs/memory-audit.md). `mutable` because caching is
    // an internal render-time optimization, not user-visible state --
    // render() itself stays logically const-compatible with its callers'
    // expectations (it already wasn't const, but this keeps the cache
    // from forcing every call site to be non-const for a reason unrelated
    // to what render() actually does).
    mutable std::unordered_map<const swf::Shape*, TessellatedShape> shapeTessellationCache_;

    // Guards against a malformed/cyclic sprite OR button reference (a
    // sprite/button that directly or indirectly places itself) recursing
    // forever. Shared between clip nesting and button-record nesting —
    // conservative (one combined budget rather than two separate ones) but
    // simple, and pathological input either way just stops rendering a
    // branch early rather than crashing.
    static constexpr int kMaxRecursionDepth = 64;
};

}  // namespace flash3ds::renderer
