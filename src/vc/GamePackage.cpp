#include "vc/GamePackage.h"

#include "swf/SwfLoader.h"

namespace flash3ds::vc {

GamePackage buildGamePackage(const ResourceFetcher& fetch) {
    GamePackage package;

    std::vector<uint8_t> configBytes;
    bool haveConfig = fetch("config.ini", configBytes);
    std::string configText =
        haveConfig ? std::string(configBytes.begin(), configBytes.end()) : std::string();
    // haveConfig == false and haveConfig == true with an empty file both
    // collapse to the same fromIniText("") defaults path here -- see
    // GameConfig.h's own doc comment for why that equivalence is
    // deliberate ("config.ini missing" and "config.ini empty" are meant to
    // behave identically).
    package.config = GameConfig::fromIniText(configText);

    std::vector<uint8_t> swfBytes;
    bool haveSwf = fetch(package.config.swfFilename, swfBytes);
    if (haveSwf) {
        // Reuses the EXISTING SwfLoader unchanged -- this is the
        // "existing SWF loader" node in docs/virtual-console.md's
        // architecture diagram. Its own well-established contract (see
        // swf/SwfLoader.h) already covers "Invalid SWF: produce a clear
        // runtime error" (movie->valid == false, movie->errorMessage set)
        // without any new logic here.
        package.movie = swf::SwfLoader::loadSwf(swfBytes.data(), swfBytes.size());
    } else {
        // Distinct from SwfLoader's own generic parse-failure messages:
        // the resource genuinely doesn't exist, which SwfLoader (handed
        // nullptr/0) would otherwise report as "too small to contain a
        // valid header" -- accurate but not the sharpest message for this
        // specific, common case.
        package.movie = swf::SwfLoader::loadSwf(nullptr, 0);
        package.movie->errorMessage = "Resource not found: " + package.config.swfFilename;
    }

    return package;
}

}  // namespace flash3ds::vc
