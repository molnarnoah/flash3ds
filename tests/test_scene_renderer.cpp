#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "renderer/SceneRenderer.h"
#include "renderer/SoftwareRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace fixtures = flash3ds::test::fixtures;
using flash3ds::renderer::SceneRenderer;
using flash3ds::renderer::SoftwareRenderer;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::swf::SwfLoader;
using flash3ds::swf::TagCode;

namespace {

// 100x100px (2000x2000 twips) stage. A single red 40x40px rectangle
// character (id=10), placed at a (10px, 10px) offset so it occupies device
// pixels roughly x:[10,50) y:[10,50).
std::vector<uint8_t> buildSingleShapeMovie() {
    auto shapeBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/10, 40 * 20, 40 * 20,
                                                        0xFF, 0x00, 0x00, 0xFF);
    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 10, fixtures::buildMatrixBytes(10 * 20, 10 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

// Same 100x100px stage. A DefineSprite (id=20) whose nested timeline places
// a blue 20x20px rectangle character (id=21) at a local (5px, 5px) offset;
// the sprite instance itself is placed on the main timeline at a
// (30px, 30px) offset — so the composed world position should be
// (35px, 35px) to (55px, 55px).
std::vector<uint8_t> buildNestedSpriteMovie() {
    auto innerShapeBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/21, 20 * 20, 20 * 20,
                                                             0x00, 0x00, 0xFF, 0xFF);

    std::vector<fixtures::FixtureTag> nestedTags = {
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 21, fixtures::buildMatrixBytes(5 * 20, 5 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto spriteBody = fixtures::buildDefineSpriteBytes(/*characterId=*/20, /*frameCount=*/1,
                                                          nestedTags);

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), innerShapeBody},
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 20,
                                            fixtures::buildMatrixBytes(30 * 20, 30 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

}  // namespace

TEST_CASE(SceneRenderer_SingleShape_FillsExpectedRegionOnly) {
    auto bytes = buildSingleShapeMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    CHECK_EQ(width, 100);
    CHECK_EQ(height, 100);

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // Well inside the rectangle [10,50)x[10,50).
    auto inside = renderer.pixelAt(30, 30);
    CHECK_EQ(inside.r, 255);
    CHECK_EQ(inside.g, 0);
    CHECK_EQ(inside.b, 0);

    // Outside the rectangle, background should still be white.
    auto outside = renderer.pixelAt(80, 80);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}

TEST_CASE(SceneRenderer_NestedSprite_ComposesWorldTransform) {
    auto bytes = buildNestedSpriteMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // Composed offset: sprite placed at (30,30) + inner shape local offset
    // (5,5) = (35,35) world px, rectangle spans [35,55)x[35,55).
    auto inside = renderer.pixelAt(45, 45);
    CHECK_EQ(inside.r, 0);
    CHECK_EQ(inside.g, 0);
    CHECK_EQ(inside.b, 255);

    // Outside the composed rectangle but still on stage.
    auto outside = renderer.pixelAt(5, 5);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);

    // Where the shape would be if the sprite's own placement offset were
    // ignored (i.e. only the inner (5,5) offset applied) — should NOT be
    // filled, proving the parent transform was actually composed in.
    auto notComposed = renderer.pixelAt(10, 10);
    CHECK_EQ(notComposed.r, 255);
    CHECK_EQ(notComposed.g, 255);
    CHECK_EQ(notComposed.b, 255);
}

// --- Compatibility-audit phase (2026-08-18): ColorTransform/_alpha was
// parsed/stored/script-mutable but never applied to a single rendered
// pixel (see docs/known-limitations.md, priority #1 finding). Regression
// coverage for the fix: swf::concatColorTransform + swf::applyColorTransform,
// threaded through every SceneRenderer leaf-render call. -------------------

TEST_CASE(SceneRenderer_MovieClipInstanceAlpha_BlendsRenderedColorWithBackground) {
    auto bytes = buildNestedSpriteMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // The sprite (character id=20) is placed at depth=1 on the root's
    // display list — find its live MovieClipInstance child and set its
    // AVM1-visible `_alpha` to 50% via the exact same C++ setter
    // `SetProperty`/`.member` assignment on `_alpha` compiles down to (see
    // MovieClipInstance::setAlpha(), which writes colorTransform_.alphaMult
    // directly — this is deliberately exercised at the same layer real AS2
    // `_alpha = 50;` would hit, not a lower-level shortcut).
    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK(childIt->second != nullptr);
    childIt->second->setAlpha(50.0);
    CHECK_EQ(childIt->second->alpha(), 50.0);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // Before this fix: _alpha had zero effect on rendering, so this pixel
    // would come out exactly (0,0,255) — full opaque blue — identical to
    // SceneRenderer_NestedSprite_ComposesWorldTransform's un-alpha'd case.
    // With a real 50%-alpha composite over the white background (standard
    // "over" blend, see SoftwareRenderer::blendChannel): the shape's
    // r/g channels (0 in the source color, 255 in the white background)
    // must land partway between the two, not at either extreme.
    auto blended = renderer.pixelAt(45, 45);
    CHECK(blended.r > 0);
    CHECK(blended.r < 255);
    CHECK(blended.g > 0);
    CHECK(blended.g < 255);
    // Blue channel is 255 in BOTH the source color and the white
    // background, so it must stay saturated at 255 regardless of alpha —
    // confirms the blend is real per-channel math, not an accidental flat
    // dimming of every channel.
    CHECK_EQ(blended.b, 255);

    // Fully outside the shape's footprint: still untouched background.
    auto outside = renderer.pixelAt(5, 5);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}

TEST_CASE(SceneRenderer_MovieClipInstanceAlphaZero_RendersFullyTransparent) {
    // alpha=0 is the sharpest possible signal: the shape must become
    // completely invisible (pixel identical to background), not merely
    // "dimmer" — this catches an off-by-one/inverted-mult bug that a
    // middling 50% test alone might not.
    auto bytes = buildNestedSpriteMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    childIt->second->setAlpha(0.0);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    auto invisible = renderer.pixelAt(45, 45);
    CHECK_EQ(invisible.r, 255);
    CHECK_EQ(invisible.g, 255);
    CHECK_EQ(invisible.b, 255);
}

// --- Phase 8: text / button / edit-text rendering ------------------------

TEST_CASE(SceneRenderer_DefineText_DrawsGlyphAtScaledPosition) {
    // Font (id=1): one glyph, a 700x700-unit rectangle in the 1024-per-em
    // space. Text (id=2): that glyph at textHeight=1024 twips (scale=1.0,
    // so the glyph's raw units map 1:1 to twips), green, at TextRecord
    // offset (0,0). Text character placed at (10px,10px) — so the glyph
    // rectangle should land at world twips [200,900]x[200,900], i.e.
    // device px [10,45)x[10,45).
    auto glyph = fixtures::buildGlyphShapeBytes(700, 700);
    auto fontBody = fixtures::buildDefineFont2Bytes(1, "T", {glyph}, {'A'}, false, 0, 0, 0, {});

    fixtures::TextRecordFixture rec;
    rec.fontId = 1;
    rec.textHeightTwips = 1024;
    rec.colorRgba = std::array<uint8_t, 4>{0, 255, 0, 255};
    rec.xOffsetTwips = 0;
    rec.yOffsetTwips = 0;
    rec.glyphs = {{0, 0}};
    auto textBody = fixtures::buildDefineTextBytes(2, fixtures::buildMatrixBytes(0, 0), 8, 16,
                                                      {rec}, false);

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineFont2), fontBody},
        {static_cast<uint16_t>(TagCode::DefineText), textBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 2, fixtures::buildMatrixBytes(10 * 20, 10 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    auto inside = renderer.pixelAt(20, 20);
    CHECK_EQ(inside.r, 0);
    CHECK_EQ(inside.g, 255);
    CHECK_EQ(inside.b, 0);

    auto outside = renderer.pixelAt(80, 80);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}

TEST_CASE(SceneRenderer_DefineButton_DrawsOnlyUpStateRecord) {
    // A 20x20px shape (id=10) referenced by a button's (id=2) Up-state
    // record at its own placement matrix (identity — coincides with the
    // button's own placement), and by a Down-state record offset far away
    // (which must NOT render, since there's no interactive state machine —
    // Up is always what's drawn).
    auto shapeBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/10, 20 * 20, 20 * 20, 0xFF,
                                                        0x00, 0x00, 0xFF);

    fixtures::ButtonRecordV1Fixture upRec;
    upRec.up = true;
    upRec.characterId = 10;
    upRec.depth = 1;
    upRec.matrixBytes = fixtures::buildMatrixBytes(0, 0);

    fixtures::ButtonRecordV1Fixture downRec;
    downRec.down = true;
    downRec.characterId = 10;
    downRec.depth = 2;
    downRec.matrixBytes = fixtures::buildMatrixBytes(50 * 20, 50 * 20);

    auto buttonBody = fixtures::buildDefineButtonV1Bytes(2, {upRec, downRec}, {0x00});

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {static_cast<uint16_t>(TagCode::DefineButton), buttonBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 2, fixtures::buildMatrixBytes(10 * 20, 10 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // Up state: button placed at (10,10) + record's identity matrix ->
    // shape occupies device px [10,30)x[10,30).
    auto upInside = renderer.pixelAt(15, 15);
    CHECK_EQ(upInside.r, 255);
    CHECK_EQ(upInside.g, 0);
    CHECK_EQ(upInside.b, 0);

    // Down state would occupy [60,80)x[60,80) if (incorrectly) rendered —
    // must stay background white.
    auto downOutside = renderer.pixelAt(65, 65);
    CHECK_EQ(downOutside.r, 255);
    CHECK_EQ(downOutside.g, 255);
    CHECK_EQ(downOutside.b, 255);
}

TEST_CASE(SceneRenderer_DefineEditText_DrawsInitialTextGlyph) {
    // Font (id=1) with a code-table entry for 'A' -> glyph 0 (a 700x700
    // rectangle). EditText (id=2): fontHeight=1024 twips (scale=1.0),
    // initialText="A", blue. Baseline starts at bounds.yMin + fontHeight;
    // bounds.yMin is always 0 (see buildDefineEditTextBytes), so the glyph
    // lands at LOCAL twips x:[0,700] y:[1024,1724] — placed at (10px,10px)
    // -> world twips x:[200,900] y:[1224,1924], i.e. device px roughly
    // [10,45)x[61,96).
    auto glyph = fixtures::buildGlyphShapeBytes(700, 700);
    auto fontBody = fixtures::buildDefineFont2Bytes(1, "E", {glyph}, {'A'}, false, 0, 0, 0, {});

    auto editTextBody = fixtures::buildDefineEditTextBytes(
        2, 100 * 20, 100 * 20, /*fontId=*/1, /*fontHeight=*/1024,
        std::array<uint8_t, 4>{0, 0, 255, 255}, "", std::string("A"));

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineFont2), fontBody},
        {static_cast<uint16_t>(TagCode::DefineEditText), editTextBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 2, fixtures::buildMatrixBytes(10 * 20, 10 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    auto inside = renderer.pixelAt(20, 70);
    CHECK_EQ(inside.r, 0);
    CHECK_EQ(inside.g, 0);
    CHECK_EQ(inside.b, 255);

    auto outside = renderer.pixelAt(5, 5);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}

// Roadmap Phase 9 (2026-08-25): a DefineMorphShape character whose START
// side is a green 40x40px rectangle and whose END side is a much larger
// (80x80px) red rectangle — deliberately different sizes/colors so a
// pixel-level check can distinguish "rendered the start side" (this
// phase's approved simplification, matching every real corpus placement's
// ratio=0) from "rendered the end side" or "rendered nothing at all".
TEST_CASE(SceneRenderer_DefineMorphShape_RendersStartSideGeometryAndColorOnly) {
    auto morphBody = fixtures::buildDefineMorphShapeBytes(
        /*characterId=*/40, /*startW=*/40 * 20, /*startH=*/40 * 20, /*endW=*/80 * 20,
        /*endH=*/80 * 20, /*r1=*/0, /*g1=*/255, /*b1=*/0, /*a1=*/255, /*r2=*/255, /*g2=*/0,
        /*b2=*/0, /*a2=*/255);
    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineMorphShape), morphBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 40,
                                            fixtures::buildMatrixBytes(10 * 20, 10 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // Well inside the START rectangle [10,50)x[10,50) — should be green.
    auto insideStart = renderer.pixelAt(30, 30);
    CHECK_EQ(insideStart.r, 0);
    CHECK_EQ(insideStart.g, 255);
    CHECK_EQ(insideStart.b, 0);

    // Inside the (much larger) END rectangle's footprint [10,90)x[10,90)
    // but OUTSIDE the start rectangle — must stay background white, not
    // red, proving the end-side geometry was not rendered.
    auto insideEndOnly = renderer.pixelAt(70, 70);
    CHECK_EQ(insideEndOnly.r, 255);
    CHECK_EQ(insideEndOnly.g, 255);
    CHECK_EQ(insideEndOnly.b, 255);
}

// --- Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task):
// renderShapeCharacter() used to call tessellateShape() fresh on every
// single call, even though a swf::ShapeDef's geometry is immutable and
// tessellation is done in shape-local space (independent of the world
// transform applied afterward) -- so the same shapeDef always produces the
// same TessellatedShape. Now cached per-SceneRenderer, keyed by the
// shapeDef's swf::Shape address (see SceneRenderer.h's
// shapeTessellationCache_ doc comment for why that's safe). Regression
// coverage: two DISTINCT shape characters, both placed and rendered
// TWICE through the same SceneRenderer instance (the second render()
// call is a pure cache-hit for both), confirming (a) the cache doesn't
// confuse two different shapes' geometry with each other and (b) a
// repeated render produces exactly the same correct pixels as the first,
// not stale/wrong geometry from a cache bug. ---------------------------

namespace {

// 100x100px stage, TWO distinct shape characters at two different depths:
// a 30x30px RED rectangle (id=10) at (5px,5px), and a 30x30px GREEN
// rectangle (id=11) at (60px,60px) -- non-overlapping, distinguishable by
// both position and color, so a cache keying bug (e.g. two different
// shapes accidentally sharing one cache entry) would show up as one
// region rendering the WRONG color, not just "some region is unfilled".
std::vector<uint8_t> buildTwoDistinctShapesMovie() {
    auto redBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/10, 30 * 20, 30 * 20, 0xFF,
                                                     0x00, 0x00, 0xFF);
    auto greenBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/11, 30 * 20, 30 * 20, 0x00,
                                                       0xFF, 0x00, 0xFF);
    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), redBody},
        {static_cast<uint16_t>(TagCode::DefineShape2), greenBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 10, fixtures::buildMatrixBytes(5 * 20, 5 * 20))},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(2, false, 11,
                                            fixtures::buildMatrixBytes(60 * 20, 60 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

}  // namespace

TEST_CASE(SceneRenderer_TessellationCache_RepeatedRenderStaysCorrectAndDistinguishesShapes) {
    auto bytes = buildTwoDistinctShapesMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);

    auto checkBothShapes = [&]() {
        // Inside the red rectangle [5,35)x[5,35).
        auto red = renderer.pixelAt(20, 20);
        CHECK_EQ(red.r, 255);
        CHECK_EQ(red.g, 0);
        CHECK_EQ(red.b, 0);
        // Inside the green rectangle [60,90)x[60,90).
        auto green = renderer.pixelAt(75, 75);
        CHECK_EQ(green.r, 0);
        CHECK_EQ(green.g, 255);
        CHECK_EQ(green.b, 0);
        // Background elsewhere, between the two shapes.
        auto bg = renderer.pixelAt(45, 45);
        CHECK_EQ(bg.r, 255);
        CHECK_EQ(bg.g, 255);
        CHECK_EQ(bg.b, 255);
    };

    // First render: both shapes' tessellations are cache MISSES (populate
    // the cache for the first time).
    scene.render(*root, renderer, width, height);
    checkBothShapes();

    // Second render through the SAME SceneRenderer instance: both shapes'
    // tessellations are now cache HITS. If the cache were keyed wrong (or
    // returned stale/uninitialized data on a hit) this would diverge from
    // the first render's correct output.
    scene.render(*root, renderer, width, height);
    checkBothShapes();
}

// --- Graphics/gradients task (2026-08-28): end-to-end integration test for
// the affine matrix inversion in SceneRenderer.cpp's resolveGradientFill()
// -- the actual bug fix behind this task (see IRenderer.h's
// DeviceGradientFill doc comment). Every other gradient test in this
// codebase (ShapeTessellator/SoftwareRenderer level) exercises one half of
// the pipeline in isolation with hand-picked coefficients; this is the only
// test that goes through the REAL byte-level DefineShape parse ->
// CharacterDictionary -> SceneRenderer's own matrix composition/inversion
// -> SoftwareRenderer's rasterizer, so a wiring bug in any of those seams
// (e.g. a transposed matrix term, a sign error in the inversion, an
// unapplied ColorTransform) would show up here even if each unit's own
// tests still passed individually. -----------------------------------

namespace {

// A 1638.4px (32768-twip) wide, 100px tall stage-filling rectangle (id=50)
// whose FillStyle is a linear gradient (red at ratio 0, blue at ratio 255)
// with a translate-only (scale=1) gradientMatrix offset by exactly
// +16384 twips in X. Per the SWF gradient-square convention (spans
// -16384..16384 twips in its own space), that offset lines up the
// gradient's t=0 point with this shape's local x=0 and t=1 with local
// x=32768 -- i.e. the gradient sweeps red-to-blue across EXACTLY this
// rectangle's width, nothing more/less, making the expected rendered
// output independently hand-verifiable (see the comment above
// buildDefineShapeWithLinearGradientBytes for why a translate-only matrix
// is sufficient/deliberate for this test rather than needing a scaling one
// too).
std::vector<uint8_t> buildGradientRectMovie() {
    auto gradientMatrix = fixtures::buildMatrixBytes(/*translateXTwips=*/16384,
                                                       /*translateYTwips=*/0);
    std::vector<fixtures::GradientStopFixture> stops = {
        {0, 255, 0, 0, 255},    // ratio 0 -> opaque red
        {255, 0, 0, 255, 255},  // ratio 255 -> opaque blue
    };
    auto shapeBody = fixtures::buildDefineShapeWithLinearGradientBytes(
        /*shapeVersion=*/3, /*characterId=*/50, /*widthTwips=*/32768, /*heightTwips=*/2000,
        gradientMatrix, stops);

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape3), shapeBody},
        // Placed at the origin (no matrix -> identity), so world space ==
        // shape-local space and the hand-verified math above applies
        // directly to device pixels (after the stage's pixelsPerTwip scale,
        // which this test picks to be an easy 1/20 -- see stage size below).
        {26 /* PlaceObject2 */, fixtures::buildPlaceObject2Bytes(1, false, 50, std::nullopt)},
        {1 /* ShowFrame */, {}},
    };
    // Stage sized so pixelsPerTwipX/Y == 1/20 exactly (outputWidthPixels ==
    // stageWidthTwips/20), matching the twips-per-pixel convention used
    // throughout this codebase -- keeps the expected device-pixel gradient
    // boundaries (x=0 and x=1638) exact round numbers, not rounding-
    // dependent ones.
    auto body = fixtures::buildMovieBody(/*stageWidthTwips=*/40000, /*stageHeightTwips=*/4000,
                                          12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

}  // namespace

TEST_CASE(SceneRenderer_LinearGradientFill_SweepsRedToBlueAcrossDeviceXAxis) {
    auto bytes = buildGradientRectMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    CHECK_EQ(width, 2000);
    CHECK_EQ(height, 200);

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*root, renderer, width, height);

    // The rectangle spans device x in [0, 1638) (32768 twips / 20) and
    // device y in [0, 100) (2000 twips / 20) -- sample at y=50, safely
    // inside that row range (NOT y=100: this scanline rasterizer's
    // half-open [yLo, yHi) active-edge convention, shared with the
    // existing flat-fill fillPolygon(), deliberately excludes the exact
    // bottom-boundary row — see SoftwareRenderer.cpp's fillPolygon()/
    // fillPolygonGradient() active-edge-table loop). Near the rectangle's
    // left edge should be near-pure red, near its right edge near-pure
    // blue, and the middle neither -- a real per-pixel gradient sweep
    // driven entirely by the real parse->tessellate->matrix-invert->
    // rasterize pipeline, not a flat average (which toFlatColor() would
    // have produced as a uniform mid-purple everywhere before this task).
    auto nearLeft = renderer.pixelAt(20, 50);
    CHECK(nearLeft.r > 200);
    CHECK(nearLeft.b < 50);

    auto nearRight = renderer.pixelAt(1600, 50);
    CHECK(nearRight.b > 200);
    CHECK(nearRight.r < 50);

    auto middle = renderer.pixelAt(819, 50);
    CHECK(middle.r > 50 && middle.r < 200);
    CHECK(middle.b > 50 && middle.b < 200);

    // Outside the rectangle entirely: untouched background.
    auto outside = renderer.pixelAt(1900, 50);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}
