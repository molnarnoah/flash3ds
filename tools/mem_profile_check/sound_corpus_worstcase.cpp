// sound_corpus_worstcase.cpp
//
// Roadmap Phase 3 memory measurement, part 2. audio_mem_check.cpp measures
// the REAL RSS cost of the decode-on-demand-and-cache path as actually
// exercised by a real corpus file's first ~13-20 frames (title/menu
// screen only, per docs/compatibility.md's known "only title screens
// tested" scope) -- but ScriptEnvironment::decodedSoundCache_ never evicts
// (see MovieClipInstance.h/.cpp's design comment), so a longer play
// session that eventually triggers every distinct StartSound in a game
// would grow the cache further. This tool answers the WORST CASE question
// directly and cheaply, without needing to actually play through an entire
// game: sum sampleCount x channels x 2 bytes (PCM16) over every kMp3
// SoundDef character resolved by CharacterDictionary::build(), i.e. "what
// would decodedSoundCache_ + a real backend's own copy (see
// Nintendo3DSAudioBackend::loadedSounds_) cost if literally every distinct
// sound in this file were eventually triggered once."
//
// This is a byte-counted UPPER BOUND from header fields (SoundDef::
// sampleCount, already parsed structurally since Phase 6), not a real
// decode -- some SoundDef entries may fail to decode in practice (corrupt
// stream, unsupported edge case) and would then never be cached, so real
// usage is <= this bound, never more.
//
// Usage: sound_corpus_worstcase <path-to-swf> [<path-to-swf> ...]

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "swf/DefineSoundTag.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-swf> [<path-to-swf> ...]\n", argv[0]);
        return 1;
    }

    for (int argi = 1; argi < argc; ++argi) {
        const std::string path = argv[argi];
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "could not open %s\n", path.c_str());
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string raw = ss.str();
        auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
        if (!movie || !movie->valid) {
            std::fprintf(stderr, "%s: load failed\n", path.c_str());
            continue;
        }
        auto dict = runtime::CharacterDictionary::build(*movie);

        size_t soundCount = 0, mp3Count = 0, otherCodecCount = 0;
        uint64_t mp3WorstCaseBytes = 0;
        for (uint32_t id = 1; id < 65536; ++id) {
            const auto* def = dict.find(static_cast<uint16_t>(id));
            if (!def) continue;
            const auto* s = std::get_if<swf::SoundDef>(def);
            if (!s) continue;
            ++soundCount;
            if (s->format == swf::SoundFormat::kMp3) {
                ++mp3Count;
                const int channels = s->stereo ? 2 : 1;
                // Worst-case PCM16 byte count if this soundId is ever
                // triggered and decoded: sampleCount is per the SWF spec's
                // "number of samples" field (per channel), matching
                // audio::DecodedAudio::frameCount()'s convention used
                // throughout this project's decode path.
                mp3WorstCaseBytes +=
                    static_cast<uint64_t>(s->sampleCount) * channels * sizeof(int16_t);
            } else {
                ++otherCodecCount;  // not decoded by this project yet (see docs/known-limitations.md L1)
            }
        }

        std::printf("=== %s ===\n", path.c_str());
        std::printf("  DefineSound characters total: %zu (mp3=%zu, other-codec-undecoded=%zu)\n",
                    soundCount, mp3Count, otherCodecCount);
        std::printf("  Worst-case decoded-PCM cache size if EVERY distinct mp3 soundId in this "
                    "file is eventually triggered once: %llu bytes (%.2f KB / %.4f MB)\n\n",
                    static_cast<unsigned long long>(mp3WorstCaseBytes),
                    mp3WorstCaseBytes / 1024.0, mp3WorstCaseBytes / (1024.0 * 1024.0));
    }
    return 0;
}
