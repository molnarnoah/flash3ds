#include <variant>

#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "runtime/CharacterDictionary.h"
#include "swf/DefineShapeTag.h"
#include "swf/DefineSoundTag.h"
#include "swf/PlaceObjectTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace fixtures = flash3ds::test::fixtures;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::SpriteDef;
using flash3ds::swf::parsePlaceObject;
using flash3ds::swf::ShapeDef;
using flash3ds::swf::SoundDef;
using flash3ds::swf::SwfLoader;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;

namespace {

// A movie with two characters: a DefineShape rectangle (id=10) and a
// DefineSprite (id=20) whose nested timeline itself places the rectangle at
// depth 1. Both characters are placed on the main timeline (shape at depth
// 1, sprite at depth 2) so the movie also exercises CharacterDictionary
// alongside a normal top-level DisplayList/Timeline build.
std::vector<uint8_t> buildMovieWithShapeAndSprite() {
    auto shapeBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/10, 100 * 20, 80 * 20,
                                                        0xE0, 0x10, 0x10, 0xFF);

    std::vector<fixtures::FixtureTag> nestedTags = {
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 10, fixtures::buildMatrixBytes(0, 0))},
        {1 /* ShowFrame */, {}},
    };
    auto spriteBody = fixtures::buildDefineSpriteBytes(/*characterId=*/20, /*frameCount=*/1,
                                                          nestedTags);

    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 10, fixtures::buildMatrixBytes(0, 0))},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(2, false, 20, fixtures::buildMatrixBytes(500, 500))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(400 * 20, 400 * 20, 12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

// A movie whose shape character (id=11) is defined NESTED INSIDE the
// sprite's own tag stream (id=20) rather than at the top level — legal per
// the public SWF spec (the character ID dictionary is global across the
// whole file, not scoped per-sprite), and something some authoring
// tools/obfuscators produce even though the standard Flash IDE publish
// pipeline puts nearly everything at the top level. Regression coverage for
// a real Phase 5 bug: CharacterDictionary::build() originally only scanned
// movie.tags (the top level), silently failing to resolve any
// character defined only inside a sprite's nested stream.
std::vector<uint8_t> buildMovieWithShapeNestedInsideSprite() {
    auto shapeBody = fixtures::buildDefineShapeBytes(2, /*characterId=*/11, 20 * 20, 20 * 20, 0x00,
                                                        0xFF, 0x00, 0xFF);
    std::vector<fixtures::FixtureTag> nestedTags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 11, fixtures::buildMatrixBytes(0, 0))},
        {1 /* ShowFrame */, {}},
    };
    auto spriteBody = fixtures::buildDefineSpriteBytes(/*characterId=*/20, /*frameCount=*/1,
                                                          nestedTags);
    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 20, fixtures::buildMatrixBytes(0, 0))},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return fixtures::wrapFws(6, body);
}

}  // namespace

TEST_CASE(CharacterDictionary_Build_ResolvesShapeNestedInsideSprite) {
    auto bytes = buildMovieWithShapeNestedInsideSprite();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto dict = CharacterDictionary::build(*movie);
    CHECK_EQ(dict.size(), static_cast<size_t>(2));  // sprite 20 + nested shape 11

    const auto* shapeCharacter = dict.find(11);
    CHECK(shapeCharacter != nullptr);
    CHECK(std::holds_alternative<ShapeDef>(*shapeCharacter));

    const auto* spriteCharacter = dict.find(20);
    CHECK(spriteCharacter != nullptr);
    CHECK(std::holds_alternative<SpriteDef>(*spriteCharacter));
}

TEST_CASE(CharacterDictionary_Build_ResolvesShapeAndSprite) {
    auto bytes = buildMovieWithShapeAndSprite();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto dict = CharacterDictionary::build(*movie);
    CHECK_EQ(dict.size(), static_cast<size_t>(2));

    const auto* shapeCharacter = dict.find(10);
    CHECK(shapeCharacter != nullptr);
    CHECK(std::holds_alternative<ShapeDef>(*shapeCharacter));
    CHECK_EQ(std::get<ShapeDef>(*shapeCharacter).characterId, static_cast<uint16_t>(10));

    const auto* spriteCharacter = dict.find(20);
    CHECK(spriteCharacter != nullptr);
    CHECK(std::holds_alternative<SpriteDef>(*spriteCharacter));
    const auto& sprite = std::get<SpriteDef>(*spriteCharacter);
    CHECK_EQ(sprite.characterId, static_cast<uint16_t>(20));
    CHECK_EQ(sprite.frameCount, static_cast<uint16_t>(1));
}

TEST_CASE(CharacterDictionary_MissingCharacter_ReturnsNull) {
    auto bytes = buildMovieWithShapeAndSprite();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto dict = CharacterDictionary::build(*movie);
    CHECK(dict.find(999) == nullptr);
}

TEST_CASE(CharacterDictionary_SpriteNestedTags_HaveAbsoluteOffsetsIntoMovieData) {
    // The critical invariant documented in CharacterDictionary.h: nested
    // sprite tags' bodyOffset must be usable directly with
    // Movie::tagBodyReader, exactly like a top-level tag.
    auto bytes = buildMovieWithShapeAndSprite();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto dict = CharacterDictionary::build(*movie);

    const auto* spriteCharacter = dict.find(20);
    CHECK(spriteCharacter != nullptr);
    const auto& sprite = std::get<SpriteDef>(*spriteCharacter);

    // Expect: one PlaceObject2 tag, one ShowFrame tag (End is consumed as
    // the loop-terminating tag but still recorded — see
    // CharacterDictionary.cpp's parseSpriteNestedTags).
    CHECK(sprite.tags.size() >= static_cast<size_t>(2));

    const auto& placeTag = sprite.tags[0];
    CHECK_EQ(placeTag.code, static_cast<uint16_t>(TagCode::PlaceObject2));

    SwfReader reader = movie->tagBodyReader(placeTag);
    auto record = parsePlaceObject(reader, placeTag.code);
    CHECK(record.has_value());
    CHECK(!reader.failed());
    CHECK_EQ(record->depth, static_cast<int32_t>(1));
    CHECK(record->characterId.has_value());
    CHECK_EQ(*record->characterId, static_cast<uint16_t>(10));
}

// --- Phase 6: DefineSound character resolution --------------------------

TEST_CASE(CharacterDictionary_Build_ResolvesDefineSound) {
    auto soundBody = fixtures::buildDefineSoundBytes(/*soundId=*/5, /*format=*/1 /*ADPCM*/,
                                                       /*rate=*/2 /*22050Hz*/, /*is16Bit=*/true,
                                                       /*stereo=*/false, /*sampleCount=*/22050);
    std::vector<fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSound), soundBody},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto dict = CharacterDictionary::build(*movie);

    const auto* soundCharacter = dict.find(5);
    CHECK(soundCharacter != nullptr);
    CHECK(std::holds_alternative<SoundDef>(*soundCharacter));
    const auto& sound = std::get<SoundDef>(*soundCharacter);
    CHECK_EQ(sound.soundId, static_cast<uint16_t>(5));
    CHECK(sound.is16Bit);
    CHECK(!sound.stereo);
    CHECK_EQ(sound.sampleCount, 22050u);
}
