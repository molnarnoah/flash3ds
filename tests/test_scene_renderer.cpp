#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "renderer/SceneRenderer.h"
#include "renderer/SoftwareRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/Timeline.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace fixtures = flash3ds::test::fixtures;
using flash3ds::renderer::SceneRenderer;
using flash3ds::renderer::SoftwareRenderer;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::Timeline;
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

    auto timeline = Timeline::build(*movie);
    CHECK(timeline != nullptr);
    auto characters = CharacterDictionary::build(*movie);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());
    CHECK_EQ(width, 100);
    CHECK_EQ(height, 100);

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*timeline, renderer, width, height);

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

    auto timeline = Timeline::build(*movie);
    auto characters = CharacterDictionary::build(*movie);

    int width = static_cast<int>(movie->frameSize.widthPixels());
    int height = static_cast<int>(movie->frameSize.heightPixels());

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(*movie, characters);
    scene.render(*timeline, renderer, width, height);

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
