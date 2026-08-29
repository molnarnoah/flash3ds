// test_vc_game_package.cpp — Virtual Console resource layer: GamePackage
// (config.ini -> GameConfig -> the EXISTING SwfLoader, see
// src/vc/GamePackage.h). The ResourceFetcher callback is backed here by a
// plain in-memory map, which is exactly what makes GamePackage testable
// on desktop with zero 3DS/RomFS dependency -- see that header's own doc
// comment for why.

#include <unordered_map>

#include "TestFramework.h"
#include "SwfTestFixtures.h"
#include "vc/GamePackage.h"

using flash3ds::vc::buildGamePackage;
using flash3ds::vc::GamePackage;
using flash3ds::vc::ResourceFetcher;

namespace {

// A ResourceFetcher backed by a fixed in-memory {name -> bytes} map --
// resources not present in the map are reported as "not found", exactly
// matching Nintendo3DSRomfs::readFile()'s own contract (see that class).
ResourceFetcher fakeFetcher(std::unordered_map<std::string, std::vector<uint8_t>> files) {
    return [files](const std::string& name, std::vector<uint8_t>& outBytes) {
        auto it = files.find(name);
        if (it == files.end()) return false;
        outBytes = it->second;
        return true;
    };
}

std::vector<uint8_t> textBytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

}  // namespace

TEST_CASE(GamePackage_DefaultSwf_LoadsSuccessfully) {
    auto fetch = fakeFetcher({
        {"config.ini", textBytes("[game]\nswf=game.swf\n")},
        {"game.swf", flash3ds::test::fixtures::minimalFwsMovie()},
    });

    GamePackage package = buildGamePackage(fetch);

    CHECK_EQ(package.config.swfFilename, std::string("game.swf"));
    CHECK(package.movie != nullptr);
    CHECK(package.movie->valid);
}

TEST_CASE(GamePackage_CustomSwfFilename_FetchesThatExactName) {
    auto fetch = fakeFetcher({
        {"config.ini", textBytes("[game]\nswf=hobo3.swf\n")},
        {"hobo3.swf", flash3ds::test::fixtures::minimalFwsMovie()},
        // A red herring under the DEFAULT name -- must NOT be picked up.
        {"game.swf", flash3ds::test::fixtures::movieWithActionScript()},
    });

    GamePackage package = buildGamePackage(fetch);

    CHECK_EQ(package.config.swfFilename, std::string("hobo3.swf"));
    CHECK(package.movie->valid);
    CHECK(!package.movie->hasActionScript);  // proves hobo3.swf was fetched, not game.swf
}

TEST_CASE(GamePackage_MissingConfigIni_UsesDefaultsAndDefaultSwf) {
    auto fetch = fakeFetcher({
        {"game.swf", flash3ds::test::fixtures::minimalFwsMovie()},
        // no "config.ini" entry at all
    });

    GamePackage package = buildGamePackage(fetch);

    CHECK_EQ(package.config.swfFilename, std::string("game.swf"));
    CHECK(package.movie->valid);
}

TEST_CASE(GamePackage_MissingSwf_ProducesInvalidMovieWithClearError) {
    auto fetch = fakeFetcher({
        {"config.ini", textBytes("[game]\nswf=nonexistent.swf\n")},
        // "nonexistent.swf" deliberately absent
    });

    GamePackage package = buildGamePackage(fetch);

    CHECK(package.movie != nullptr);  // always non-null, per contract
    CHECK(!package.movie->valid);
    CHECK(package.movie->errorMessage.find("nonexistent.swf") != std::string::npos);
    CHECK(package.movie->errorMessage.find("not found") != std::string::npos);
    // swfResourceFound distinguishes this case (fetch() itself failed)
    // from GamePackage_MalformedSwf_ProducesInvalidMovie below (fetch()
    // succeeded, SwfLoader rejected the bytes) -- see GamePackage.h's doc
    // comment. This is what nintendo3ds_main.cpp's kInvalidMovie subCode
    // is built from, so a real device/emulator run can distinguish these
    // two cases from the on-screen square count alone, with no log
    // access needed.
    CHECK(!package.swfResourceFound);
}

TEST_CASE(GamePackage_MalformedSwf_ProducesInvalidMovie) {
    auto fetch = fakeFetcher({
        {"config.ini", textBytes("[game]\nswf=broken.swf\n")},
        {"broken.swf", std::vector<uint8_t>{0x00, 0x01, 0x02}},  // not a real SWF
    });

    GamePackage package = buildGamePackage(fetch);

    CHECK(package.movie != nullptr);
    CHECK(!package.movie->valid);
    CHECK(!package.movie->errorMessage.empty());
    // The resource WAS found (fetch() succeeded) -- SwfLoader rejected
    // the bytes themselves. See swfResourceFound's doc comment.
    CHECK(package.swfResourceFound);
}

TEST_CASE(GamePackage_EmptyConfigIni_BehavesLikeMissing) {
    auto withEmptyFile = fakeFetcher({
        {"config.ini", {}},
        {"game.swf", flash3ds::test::fixtures::minimalFwsMovie()},
    });
    auto withoutFile = fakeFetcher({
        {"game.swf", flash3ds::test::fixtures::minimalFwsMovie()},
    });

    GamePackage a = buildGamePackage(withEmptyFile);
    GamePackage b = buildGamePackage(withoutFile);

    CHECK(a.config == b.config);
}
