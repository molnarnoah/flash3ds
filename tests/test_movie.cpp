#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/SwfLoader.h"

namespace fixtures = flash3ds::test::fixtures;
using flash3ds::swf::SwfLoader;

TEST_CASE(Movie_CountTagsWithCode) {
    std::vector<fixtures::FixtureTag> tags = {
        {1, {}}, {1, {}}, {12, {0x00}}, {1, {}},
    };
    auto body = fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 3, tags);
    auto bytes = fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    CHECK_EQ(movie->countTagsWithCode(1), 3u);   // ShowFrame x3
    CHECK_EQ(movie->countTagsWithCode(12), 1u);  // DoAction x1
    CHECK_EQ(movie->countTagsWithCode(0), 1u);   // auto-appended End
}
