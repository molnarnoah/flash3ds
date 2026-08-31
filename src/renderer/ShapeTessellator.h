// ShapeTessellator.h
//
// Converts a parsed swf::Shape (fill/line styles + SHAPERECORD stream) into
// flat, renderable geometry: closed polygons (for fills) and polylines
// (for strokes), in the shape's own local twip coordinate space.
//
// IMPORTANT — run-scoped, order-preserving fill reconstruction
// (2026-08-30), with one still-open limitation:
//
//   This tessellator splits each pen-drawn run (the edges between one
//   MoveTo and the next) into one polygon PER FILL STYLE actually used
//   within that run, in authored edge order, instead of the earlier,
//   deliberately-simplified "one polygon per pen-drawn subpath" scheme
//   (Phase 3, 2026-08-21) that used only whichever FillStyle was live when
//   the subpath STARTED. That older scheme silently dropped every fill
//   region past the first whenever a style switched mid-run with no
//   explicit MoveTo — confirmed as the root cause of real hobo.swf
//   title/menu content rendering only its first fill layer. The fix: a
//   `fillSubpath`/`fillSubpathStyle` accumulator is flushed into a closed
//   polygon whenever the resolved style (fillStyle1, falling back to
//   fillStyle0) changes mid-run, in addition to the existing MoveTo/end-
//   of-shape flush points — see docs/renderer.md for the full writeup,
//   including a real before/after render comparison.
//
//   Two earlier, more "textbook-correct" designs were tried and REJECTED
//   after real-content regressions, and are recorded here so a future
//   change doesn't reintroduce either without re-verifying against real
//   rendered output, not just unit tests, first:
//
//   1. Whole-shape edge grouping: record every edge against its resolved
//      FillStyle (including a second, reversed contribution to the OTHER
//      side's style, matching the spec's every-edge-borders-two-regions
//      model), group ALL of a shape's edges by FillStyle identity
//      regardless of which run they came from, then chain each group into
//      contours by matching shared endpoints. On paper more spec-correct,
//      but the endpoint-matching step incorrectly welded together edges
//      from separate, unrelated MoveTo-bounded contours whenever they
//      happened to touch the same coordinate — confirmed via hobo.swf's
//      mute-button icon (characterId=89), whose black speaker-cutout
//      contour got scrambled into a garbled ~65-point polygon and its
//      white circle came out solid (losing the cutout) from the spurious
//      reversed contribution.
//   2. The same whole-shape chaining, PLUS filtering contours by winding
//      sign (shoelace-formula sign, keeping only contours matching the
//      dominant contour's sign) to try to distinguish "real region" from
//      "hole". This didn't fix the mute icon (the scrambling happens
//      during chaining, before any sign check ever runs) and introduced a
//      SECOND regression: hobo.swf's "Armor Games" caption, whose separate
//      letters share one fill style and are legitimately wound in mixed
//      directions, got its non-dominant-sign letters discarded, garbling
//      the text into "Amrae".
//
//   The current run-scoped design sidesteps both failure modes at once:
//   edges within one MoveTo-to-MoveTo run are already in the correct
//   sequential authored order, so no endpoint search/matching is ever
//   needed, and grouping never crosses a MoveTo boundary, so unrelated
//   contours can never be welded together no matter how their coordinates
//   happen to line up. Re-verified against hobo.swf: characterId=89's mute
//   icon now tessellates into exactly 6 polygons (matching its 6 MoveTo
//   subpaths 1:1, one style each) with clean monotonic contours — no
//   scrambling — and renders correctly (white circle, black speaker
//   cutout intact); "Armor Games" and the HOBO/CONTROLS title art all
//   render correctly in the same frame.
//
//   Hole/counter rendering (2026-08-31, Priority Fix List item #1): the
//   narrower remaining gap described in the paragraph above — a single
//   FillStyle's edges forming multiple disjoint contours where one is
//   genuinely a hole in another (e.g. the letter "O") — is now handled,
//   WITHOUT reintroducing either rejected design's failure mode. The key
//   realization: this run-scoped tessellator already never welds edges
//   across a MoveTo boundary (that's exactly why it sidesteps the mute-
//   icon/"Armor Games" regressions above), so every contour it emits is
//   already a correct, independently-closed polygon — the only thing
//   missing was a way to tell the RENDERER that two or more of those
//   already-correct contours belong to the same original fill style and
//   must be filled TOGETHER with one combined even-odd rule, rather than
//   each being painted as its own fully-opaque solid region. `fillGroupId`
//   below is exactly that: every TessellatedPolygon carries the identity
//   of the resolved swf::FillStyle it came from (assigned in first-seen
//   order per tessellateShape() call — see the .cpp), so the caller
//   (SceneRenderer) can group same-style contours and hand them to
//   IRenderer::fillPolygonGroup()/fillPolygonGradientGroup() for a
//   combined fill instead of independent per-contour fillPolygon() calls.
//   No edge welding, endpoint matching, or winding-sign filtering is
//   involved — the fix is purely "which already-correct contours get
//   painted in one call vs. separately", so it carries none of the risk
//   that sank the two rejected designs above.
//
//   One real regression WAS found and fixed during this same 2026-08-31
//   change, worth recording here since it's a subtlety of exactly this
//   fillGroupId field, not of the tessellator's own contour extraction:
//   `fillGroupId` only says "these contours share a resolved FillStyle",
//   NOT "these contours are a boundary+hole pair meant to be combined".
//   Real content (hobo.swf characterId=89, the same mute-button icon
//   already called out above) emits two same-fillGroupId white contours
//   with three differently-styled black contours authored BETWEEN them —
//   the second white contour is a deliberate painter's-algorithm overpaint
//   (repaint white on top of the black detail), not a hole in the first
//   one. SceneRenderer.cpp's fillTessellatedPolygons() therefore only
//   combines same-fillGroupId contours when they're CONTIGUOUS in
//   `polygons` (nothing of a different fillGroupId between them) — see
//   that function's own doc comment for the full writeup. A genuine
//   boundary+hole pair (this field's actual target) is unaffected, since
//   nothing of a different fill style is ever tessellated between two
//   contours of the very same fill run in that case.
//
// Gradient and bitmap fills are reduced to a single representative flat
// color (see FillStyle::toFlatColor below) — proper gradient/bitmap
// rendering is future work.
//
// Graphics/gradients task (2026-08-28): "future work" above is now half
// done, by real-corpus evidence — see IRenderer.h's DeviceGradientFill doc
// comment for the full reasoning. FillStyleType::kLinearGradient fills are
// now tessellated with a real 256-stop color ramp (GradientPaint below,
// PaintKind::kLinearGradient) instead of being flattened by toFlatColor();
// radial/focal-radial/bitmap fills still go through the original
// toFlatColor() path (PaintKind::kFlat) — zero corpus evidence for those,
// so they're deliberately left alone rather than implemented speculatively.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "swf/ShapeRecords.h"

namespace flash3ds::renderer {

struct PointTwips {
    int32_t x = 0;
    int32_t y = 0;
};

// A gradient fill resolved to a 256-stop ramp, still in the shape's own
// local twips space (device-space resolution — the affine matrix inversion
// — happens later, in SceneRenderer, since that's where the
// shape's-local-space -> device-pixel-space transform chain is assembled;
// see IRenderer.h's DeviceGradientFill). `matrix` is FillStyle::
// gradientMatrix unchanged: it maps the SWF gradient square's own
// -16384..16384 space into this shape's local twips space.
struct GradientPaint {
    swf::Matrix matrix;
    swf::GradientSpreadMode spreadMode = swf::GradientSpreadMode::kPad;
    std::array<swf::RgbaColor, 256> ramp{};
};

enum class PaintKind { kFlat, kLinearGradient };

struct TessellatedPolygon {
    swf::RgbaColor color;  // flat color (see toFlatColor); the fallback/degenerate-matrix
                            // color even when paintKind == kLinearGradient
    PaintKind paintKind = PaintKind::kFlat;
    GradientPaint gradient;  // valid iff paintKind == kLinearGradient
    std::vector<PointTwips> points;  // closed polygon; edge back to points[0] is implicit

    // Identifies which resolved swf::FillStyle this contour came from,
    // assigned in first-seen order within one tessellateShape() call (see
    // the .cpp) — NOT a global/stable id across different shapes or calls.
    // Two polygons from the same tessellateShape() call with the same
    // fillGroupId are different contours of the SAME original fill region
    // (e.g. an outer boundary and an inner counter/hole) and must be
    // combined-filled together via IRenderer::fillPolygonGroup()/
    // fillPolygonGradientGroup() rather than painted independently — see
    // this file's header comment ("Hole/counter rendering") for why. -1
    // never occurs in practice (every flushed polygon has a resolved style
    // by construction) — it exists only as an explicit "not assigned"
    // sentinel for defensive/default-constructed instances.
    int fillGroupId = -1;
};

struct TessellatedStroke {
    swf::RgbaColor color;
    uint16_t widthTwips = 0;
    std::vector<PointTwips> points;  // open polyline
};

struct TessellatedShape {
    std::vector<TessellatedPolygon> polygons;
    std::vector<TessellatedStroke> strokes;
};

// Reduces a FillStyle to a single flat color: the solid color as-is, the
// average of a gradient's stop colors for gradient fills, or a neutral gray
// placeholder for bitmap fills (bitmap decoding isn't implemented yet).
swf::RgbaColor toFlatColor(const swf::FillStyle& fill);

// Builds a 256-entry color ramp by linearly interpolating between a
// Gradient's GradientRecord stops (each stop's `ratio` is already in the
// same 0-255 space as the ramp index). Mirrors toFlatColor()'s edge-case
// handling: an empty gradient returns 256 copies of the same neutral gray
// placeholder toFlatColor() uses for that case. Exposed (not file-local)
// so tests can verify ramp contents directly against known stop data.
std::array<swf::RgbaColor, 256> buildGradientRamp(const swf::Gradient& gradient);

// `curveSubdivisions` controls how many line segments approximate each
// quadratic-bezier CurvedEdgeRecord (higher = smoother, more points).
TessellatedShape tessellateShape(const swf::Shape& shape, int curveSubdivisions = 8);

}  // namespace flash3ds::renderer
