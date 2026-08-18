// test_define_sound_tag.cpp
//
// Phase 6: structural parsing tests for DefineSound (14) and StartSound
// (15)/SOUNDINFO — see swf/DefineSoundTag.h and swf/StartSoundTag.h for
// exactly what "structural" means here (header fields only, no codec
// decode).

#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineSoundTag.h"
#include "swf/StartSoundTag.h"

using flash3ds::swf::parseDefineSound;
using flash3ds::swf::parseStartSound;
using flash3ds::swf::SoundFormat;
using flash3ds::swf::SoundRate;
using flash3ds::swf::SwfReader;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineSound_ParsesHeaderFields) {
    std::vector<uint8_t> sampleData = {0xAA, 0xBB, 0xCC, 0xDD};
    auto bytes = fixtures::buildDefineSoundBytes(/*soundId=*/9, /*format=*/2 /*MP3*/,
                                                  /*rate=*/3 /*44.1kHz*/, /*is16Bit=*/true,
                                                  /*stereo=*/true, /*sampleCount=*/44100,
                                                  sampleData);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineSound(r, /*bodyAbsoluteOffset=*/100);
    CHECK(def.has_value());
    CHECK_EQ(def->soundId, 9);
    CHECK(def->format == SoundFormat::kMp3);
    CHECK(def->rate == SoundRate::k44100Hz);
    CHECK(def->is16Bit);
    CHECK(def->stereo);
    CHECK_EQ(def->sampleCount, 44100u);
    CHECK_EQ(def->dataLength, sampleData.size());
    // Header is SoundId(2) + flags(1) + SampleCount(4) = 7 bytes, so the
    // sample data starts at bodyAbsoluteOffset + 7.
    CHECK_EQ(def->dataOffset, static_cast<size_t>(107));
}

TEST_CASE(DefineSound_MonoUncompressed8Bit_FlagsAllFalse) {
    auto bytes = fixtures::buildDefineSoundBytes(/*soundId=*/1, /*format=*/0, /*rate=*/0,
                                                  /*is16Bit=*/false, /*stereo=*/false,
                                                  /*sampleCount=*/100);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineSound(r, 0);
    CHECK(def.has_value());
    CHECK(def->format == SoundFormat::kUncompressedNative);
    CHECK(def->rate == SoundRate::k5512Hz);
    CHECK(!def->is16Bit);
    CHECK(!def->stereo);
    CHECK_EQ(def->dataLength, static_cast<size_t>(0));
}

TEST_CASE(SoundInfo_NoOptionalFields_AllAbsent) {
    auto bytes = fixtures::buildSoundInfoBytes(/*syncStop=*/false, /*syncNoMultiple=*/false,
                                                std::nullopt, std::nullopt, std::nullopt);
    SwfReader r(bytes.data(), bytes.size());
    auto info = flash3ds::swf::readSoundInfo(r);
    CHECK(!info.syncStop);
    CHECK(!info.syncNoMultiple);
    CHECK(!info.hasInPoint);
    CHECK(!info.hasOutPoint);
    CHECK(!info.hasLoops);
    CHECK(!info.inPointSamples.has_value());
    CHECK(!info.loopCount.has_value());
}

TEST_CASE(SoundInfo_WithLoopsAndInOutPoints_ParsesEachField) {
    auto bytes = fixtures::buildSoundInfoBytes(/*syncStop=*/false, /*syncNoMultiple=*/true,
                                                /*inPoint=*/1000u, /*outPoint=*/5000u,
                                                /*loopCount=*/uint16_t{3});
    SwfReader r(bytes.data(), bytes.size());
    auto info = flash3ds::swf::readSoundInfo(r);
    CHECK(info.syncNoMultiple);
    CHECK(info.hasInPoint);
    CHECK(info.hasOutPoint);
    CHECK(info.hasLoops);
    CHECK(info.inPointSamples.has_value());
    CHECK_EQ(*info.inPointSamples, 1000u);
    CHECK(info.outPointSamples.has_value());
    CHECK_EQ(*info.outPointSamples, 5000u);
    CHECK(info.loopCount.has_value());
    CHECK_EQ(*info.loopCount, static_cast<uint16_t>(3));
}

TEST_CASE(SoundInfo_SyncStop_FlagParsed) {
    auto bytes = fixtures::buildSoundInfoBytes(/*syncStop=*/true, /*syncNoMultiple=*/false,
                                                std::nullopt, std::nullopt, std::nullopt);
    SwfReader r(bytes.data(), bytes.size());
    auto info = flash3ds::swf::readSoundInfo(r);
    CHECK(info.syncStop);
}

TEST_CASE(StartSound_ParsesSoundIdAndSoundInfo) {
    auto info = fixtures::buildSoundInfoBytes(false, false, std::nullopt, std::nullopt,
                                               uint16_t{7});
    auto bytes = fixtures::buildStartSoundBytes(/*soundId=*/42, info);
    SwfReader r(bytes.data(), bytes.size());
    auto rec = parseStartSound(r);
    CHECK(rec.has_value());
    CHECK_EQ(rec->soundId, 42);
    CHECK(rec->info.hasLoops);
    CHECK(rec->info.loopCount.has_value());
    CHECK_EQ(*rec->info.loopCount, static_cast<uint16_t>(7));
}
