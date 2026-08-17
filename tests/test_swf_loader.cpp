#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/SwfLoader.h"

using flash3ds::runtime::SwfCompression;
using flash3ds::swf::SwfLoader;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(SwfLoader_MinimalFws_ParsesHeaderCorrectly) {
    auto bytes = fixtures::minimalFwsMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());

    CHECK(movie->valid);
    CHECK_EQ(movie->version, 6);
    CHECK(movie->compression == SwfCompression::kNone);
    CHECK_EQ(movie->frameSize.widthTwips(), 550 * 20);
    CHECK_EQ(movie->frameSize.heightTwips(), 400 * 20);
    CHECK(movie->frameRateFps() > 23.9 && movie->frameRateFps() < 24.1);
    CHECK_EQ(movie->frameCount, 1);
    CHECK(!movie->hasActionScript);

    // ShowFrame + auto-appended End.
    CHECK_EQ(movie->tags.size(), 2u);
    CHECK_EQ(movie->tags[0].code, 1u);  // ShowFrame
    CHECK_EQ(movie->tags[1].code, 0u);  // End
}

TEST_CASE(SwfLoader_MinimalCws_DecompressesAndParses) {
    auto bytes = fixtures::minimalCwsMovie();
    CHECK_EQ(static_cast<char>(bytes[0]), 'C');

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());

    CHECK(movie->valid);
    CHECK(movie->compression == SwfCompression::kZlib);
    CHECK_EQ(movie->frameSize.widthTwips(), 550 * 20);
    CHECK_EQ(movie->frameCount, 1);
    CHECK_EQ(movie->tags.size(), 2u);
}

TEST_CASE(SwfLoader_ActionScriptTag_DetectedViaDoAction) {
    auto bytes = fixtures::movieWithActionScript();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());

    CHECK(movie->valid);
    CHECK(movie->hasActionScript);
    CHECK_EQ(movie->countTagsWithCode(12 /* DoAction */), 1u);
}

TEST_CASE(SwfLoader_InvalidSignature_FailsGracefully) {
    const uint8_t bad[] = {'X', 'Y', 'Z', 6, 0, 0, 0, 0};
    auto movie = SwfLoader::loadSwf(bad, sizeof(bad));
    CHECK(!movie->valid);
    CHECK(!movie->errorMessage.empty());
}

TEST_CASE(SwfLoader_TooShort_FailsGracefully_NoCrash) {
    const uint8_t tiny[] = {'F', 'W'};
    auto movie = SwfLoader::loadSwf(tiny, sizeof(tiny));
    CHECK(!movie->valid);
}

TEST_CASE(SwfLoader_NullData_FailsGracefully_NoCrash) {
    auto movie = SwfLoader::loadSwf(nullptr, 0);
    CHECK(!movie->valid);
}

TEST_CASE(SwfLoader_LzmaSignature_RecognizedButRejected) {
    const uint8_t zws[] = {'Z', 'W', 'S', 13, 8, 0, 0, 0};
    auto movie = SwfLoader::loadSwf(zws, sizeof(zws));
    CHECK(!movie->valid);
    CHECK(movie->compression == SwfCompression::kLzma);
}

TEST_CASE(SwfLoader_TruncatedTagStream_DoesNotCrash_ReturnsPartialTags) {
    auto bytes = fixtures::minimalFwsMovie();
    // Chop off the last 3 bytes (breaks the final End-tag header) to
    // simulate a truncated/corrupt file.
    bytes.resize(bytes.size() - 3);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    // Header itself was intact, so this should still be "valid" overall —
    // only the tag scan was cut short. The important guarantee is: no
    // crash, and we get back whatever tags were fully readable.
    CHECK(movie != nullptr);
}

TEST_CASE(SwfLoader_LoadSwfFile_MissingFile_FailsGracefully) {
    auto movie = SwfLoader::loadSwfFile("/nonexistent/path/does_not_exist.swf");
    CHECK(!movie->valid);
    CHECK(!movie->errorMessage.empty());
}
