// tools/swf_diagnostic/main.cpp
//
// Real-game test corpus phase (2026-08-19). A permanent, repo-checked-in
// analysis tool — NOT a throwaway script — for producing a deterministic,
// evidence-based compatibility report for any real SWF file (the Hobo
// family, Extreme Pamplona, or any future compatibility target), without
// modifying or copying the file itself. Reuses this project's existing
// parsers exclusively (SwfLoader/TagDispatcher/CharacterDictionary/
// DefineButtonTag/DefineSoundTag/DefineShapeTag/ActionCode) — no new
// parsing logic beyond a generic recursive tag-stream walker (needed
// because CharacterDictionary's own recursive walker is anonymous-
// namespace/private) and a generic AVM1 opcode walker (needed because
// avm1::Interpreter only executes bytecode, it doesn't summarize it).
//
// Usage: swf_diagnostic <path/to/file.swf>
//
// Prints a plain-text, greppable report to stdout: header stats, a
// recursive tag histogram (including nested DefineSprite tag streams),
// AVM2/DoABC detection, an AVM1 opcode histogram (recursively including
// DefineFunction/DefineFunction2/With bodies, which live OUTSIDE their
// defining action's own declared operand length per the SWF spec — see
// walkActionStream() below), a button profile (definitions + placed
// instances via a real MovieClipInstance tree, frame 1 only), a sound
// profile, a rendering-feature tag-presence summary, and a best-effort AS2
// identifier substring scan over the decompressed body.
//
// ANALYSIS ONLY: this tool never mutates the input file and never wires up
// any new runtime feature — see docs/real-game-compatibility.md for the
// methodology notes this header summarizes.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "avm1/ActionCode.h"
#include "runtime/ButtonInstance.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/DefineButtonTag.h"
#include "swf/DefineShapeTag.h"
#include "swf/DefineSoundTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"
#include "swf/TagDispatcher.h"

using namespace flash3ds;

namespace {

// --- recursive tag-stream walker (mirrors CharacterDictionary's own
// private scanTagsForCharacters()'s recursion pattern, but collects every
// tag record rather than only character-defining ones) -------------------

struct CollectedTag {
    swf::TagRecord tag;
    int depth;  // 0 = top-level movie tag stream; 1+ = nested inside that many DefineSprite levels
};

void collectTagsRecursive(const runtime::Movie& movie, const std::vector<swf::TagRecord>& tags,
                           int depth, std::vector<CollectedTag>& out) {
    for (const auto& tag : tags) {
        out.push_back({tag, depth});
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::DefineSprite) {
            if (tag.bodyLength < 4) continue;
            swf::SwfReader header = movie.tagBodyReader(tag);
            header.readU16();  // characterId, unused here
            header.readU16();  // frameCount, unused here
            if (header.failed()) continue;

            std::vector<swf::TagRecord> nested;
            swf::SwfReader full(movie.data.data(), movie.data.size());
            full.seek(tag.bodyOffset + 4);
            size_t endOffset = tag.bodyOffset + tag.bodyLength;
            while (full.position() < endOffset && !full.failed()) {
                swf::TagRecord nestedTag;
                if (!swf::TagDispatcher::readTagHeader(full, nestedTag)) break;
                nested.push_back(nestedTag);
                if (static_cast<swf::TagCode>(nestedTag.code) == swf::TagCode::End) break;
                full.skip(nestedTag.bodyLength);
            }
            collectTagsRecursive(movie, nested, depth + 1, out);
        }
    }
}

// --- generic AVM1 opcode walker -------------------------------------------
//
// Counts opcodes only -- does not execute anything. Must replicate
// avm1::Interpreter's two documented exceptions to "an action's true
// extent is exactly its own declared Length" (see Interpreter.cpp's own
// comments on these two cases):
//   - DefineFunction(0x9B)/DefineFunction2(0x8E): the function BODY (its
//     own nested action stream, CodeSize bytes) follows immediately in the
//     OUTER stream, after this action's own (signature-only) operand.
//   - With(0x94): the block body (WithSize bytes) likewise follows
//     immediately in the OUTER stream after this action's own (2-byte)
//     operand.
// Without this, opcode counts after the first DefineFunction/With in a
// buffer would be silently wrong (misaligned reads into the function
// body's raw bytes, interpreted as bogus opcodes).

struct OpcodeStats {
    std::map<std::string, int> counts;
    int totalOpcodes = 0;
    int nestedFunctionBodies = 0;
    int withBlocks = 0;
    bool truncated = false;  // hit end-of-buffer before an ActionEnd
};

void walkActionStream(const uint8_t* data, size_t size, OpcodeStats& stats) {
    size_t pos = 0;
    bool sawEnd = false;
    while (pos < size) {
        uint8_t opcode = data[pos];
        pos += 1;
        if (opcode == 0x00) {  // ActionEnd
            sawEnd = true;
            break;
        }
        auto code = static_cast<avm1::ActionCode>(opcode);
        stats.counts[avm1::actionCodeName(opcode)]++;
        stats.totalOpcodes++;

        if (opcode < 0x80) continue;  // no operand

        if (pos + 2 > size) {
            stats.truncated = true;
            break;
        }
        uint16_t length = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        if (pos + length > size) {
            stats.truncated = true;
            break;
        }
        const uint8_t* operand = data + pos;
        pos += length;

        if (code == avm1::ActionCode::DefineFunction || code == avm1::ActionCode::DefineFunction2) {
            swf::SwfReader header(operand, length);
            header.readCString();  // function name
            uint16_t numParams = header.readU16();
            uint16_t codeSize = 0;
            if (code == avm1::ActionCode::DefineFunction) {
                for (uint16_t i = 0; i < numParams && !header.failed(); ++i) header.readCString();
            } else {
                header.readU8();   // register count
                header.readU16();  // flags
                for (uint16_t i = 0; i < numParams && !header.failed(); ++i) {
                    header.readU8();
                    header.readCString();
                }
            }
            codeSize = header.readU16();
            if (header.failed() || pos + codeSize > size) {
                stats.truncated = true;
                break;
            }
            stats.nestedFunctionBodies++;
            walkActionStream(data + pos, codeSize, stats);
            pos += codeSize;
        } else if (code == avm1::ActionCode::With) {
            swf::SwfReader header(operand, length);
            uint16_t blockSize = header.readU16();
            if (header.failed() || pos + blockSize > size) {
                stats.truncated = true;
                break;
            }
            stats.withBlocks++;
            walkActionStream(data + pos, blockSize, stats);
            pos += blockSize;
        }
    }
    if (!sawEnd && pos >= size) {
        // Ran off the end without an explicit ActionEnd byte -- some real
        // content (including button actionsV1, which per spec has no
        // length prefix on the tag itself) is like this; not necessarily
        // an error, just noted.
    }
}

void mergeStats(OpcodeStats& into, const OpcodeStats& from) {
    for (const auto& [name, count] : from.counts) into.counts[name] += count;
    into.totalOpcodes += from.totalOpcodes;
    into.nestedFunctionBodies += from.nestedFunctionBodies;
    into.withBlocks += from.withBlocks;
    into.truncated = into.truncated || from.truncated;
}

// Every ActionCode this runtime's Interpreter has a real (or explicitly
// documented parse-only-stub, see below) case for. Confirmed by direct
// diff against avm1::Interpreter.cpp's switch statement (2026-08-19): all
// 100 ActionCode enum values have a `case` -- so "supported" here means
// "not silently ignored"; the one confirmed PARSE-ONLY exception is Try
// (0x8F), which Interpreter.cpp's own comment documents as "parsed and
// skipped (block execution not yet implemented)" -- flagged specially
// below rather than reported as fully working.
bool isParseOnlyStub(const std::string& opcodeName) { return opcodeName == "Try"; }

// --- AS2 identifier substring scan (best-effort; see docs/
// real-game-compatibility.md's methodology note) --------------------------

const char* kAs2Identifiers[] = {
    "Key",       "Mouse",       "MovieClip",       "Sound",         "_root",
    "_parent",   "_global",     "gotoAndPlay",     "gotoAndStop",   "play",
    "stop",      "removeMovieClip", "createEmptyMovieClip", "duplicateMovieClip",
    "attachMovie", "loadMovie", "_x",              "_y",            "_xscale",
    "_yscale",   "_rotation",   "_alpha",          "_visible",      "_width",
    "_height",   "onClipEvent", "onPress",         "onRelease",     "onRollOver",
    "onRollOut", "onMouseDown", "onMouseUp",       "onMouseMove",   "ExternalInterface",
};

std::map<std::string, int> scanAs2Identifiers(const std::vector<uint8_t>& data) {
    std::map<std::string, int> found;
    // Reconstruct every NUL-terminated ASCII run (matches how AVM1 Push
    // string constants / ConstantPool entries / DefineFunction param names
    // are actually encoded) and test each against the identifier list --
    // avoids false positives from binary matches inside compressed shape/
    // sound data that happen to contain the same bytes.
    std::string current;
    std::vector<std::string> strings;
    for (uint8_t b : data) {
        if (b >= 0x20 && b < 0x7F) {
            current.push_back(static_cast<char>(b));
        } else {
            if (current.size() >= 2) strings.push_back(current);
            current.clear();
        }
    }
    if (current.size() >= 2) strings.push_back(current);

    for (const char* id : kAs2Identifiers) {
        int count = 0;
        std::string needle(id);
        for (const auto& s : strings) {
            // Exact match OR needle appears as a whole "word" inside a
            // longer string (e.g. "_root.mc" contains "_root" as a
            // prefix followed by a non-identifier char) -- a plain
            // substring search would also match unrelated identifiers
            // that happen to contain the same characters (e.g. "play"
            // inside "playerName"), so require a non-identifier boundary
          // on both sides.
            size_t pos = 0;
            while ((pos = s.find(needle, pos)) != std::string::npos) {
                bool leftOk = (pos == 0) || !(isalnum((unsigned char)s[pos - 1]) || s[pos - 1] == '_');
                size_t endPos = pos + needle.size();
                bool rightOk = (endPos >= s.size()) ||
                               !(isalnum((unsigned char)s[endPos]) || s[endPos] == '_');
                if (leftOk && rightOk) { count++; break; }  // count once per string, not per repeat
                pos += 1;
            }
        }
        if (count > 0) found[id] = count;
    }
    return found;
}

// --- placed-button diagnostic (frame 1, root timeline only, mirrors the
// ButtonInstance-phase hobo_button_diag.cpp tool but factored for reuse
// across every corpus file) ------------------------------------------------

struct PlacedButtonRecord {
    std::string parentPath;
    std::string name;
    int32_t depth;
    uint16_t characterId;
    double worldX, worldY;
};

void findPlacedButtonsRecursive(runtime::MovieClipInstance& clip, const std::string& pathPrefix,
                                 std::vector<PlacedButtonRecord>& out) {
    for (const auto& [depth, button] : clip.buttonInstances()) {
        PlacedButtonRecord rec;
        rec.parentPath = pathPrefix.empty() ? "/" : pathPrefix;
        rec.name = button->name();
        rec.depth = depth;
        rec.characterId = button->characterId();
        const auto& wm = button->worldMatrix();
        rec.worldX = wm.translateXPixels();
        rec.worldY = wm.translateYPixels();
        out.push_back(rec);
    }
    for (const auto& [depth, child] : clip.children()) {
        (void)depth;
        findPlacedButtonsRecursive(*child, pathPrefix + "/" + child->name(), out);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: swf_diagnostic <path/to/file.swf>\n");
        return 2;
    }
    std::string path = argv[1];

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        printf("PARSE_RESULT: FAILED\nERROR: could not open file: %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t actualFileSize = bytes.size();

    auto movie = swf::SwfLoader::loadSwf(bytes.data(), bytes.size());
    if (!movie || !movie->valid) {
        printf("PARSE_RESULT: FAILED\nERROR: %s\n",
               movie ? movie->errorMessage.c_str() : "(null movie)");
        return 1;
    }
    printf("PARSE_RESULT: OK\n");

    // --- 1. Header stats ---------------------------------------------------
    printf("\n=== HEADER ===\n");
    printf("file: %s\n", path.c_str());
    const char* compressionName = movie->compression == runtime::SwfCompression::kNone ? "none (FWS)"
                                    : movie->compression == runtime::SwfCompression::kZlib ? "zlib (CWS)"
                                                                                             : "lzma (ZWS)";
    printf("swf_version: %u\n", movie->version);
    printf("compression: %s\n", compressionName);
    printf("declared_length: %u\n", movie->declaredFileLength);
    printf("actual_file_size: %zu\n", actualFileSize);
    printf("decompressed_body_size: %zu\n", movie->data.size());
    printf("stage_width_px: %.1f\n", movie->frameSize.widthPixels());
    printf("stage_height_px: %.1f\n", movie->frameSize.heightPixels());
    printf("fps: %.2f\n", movie->frameRateFps());
    printf("declared_frame_count: %u\n", movie->frameCount);
    printf("has_actionscript_tag: %s\n", movie->hasActionScript ? "yes" : "no");

    // --- 2. Recursive tag collection + AVM2 detection -----------------------
    std::vector<CollectedTag> allTags;
    collectTagsRecursive(*movie, movie->tags, 0, allTags);

    bool avm2Detected = false;
    int doActionCount = 0, doInitActionCount = 0, doAbcCount = 0;
    std::map<std::string, int> tagHistogram;
    std::map<uint16_t, int> unknownTagHistogram;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        std::string name = ct.tag.name;
        if (name == "Unknown") {
            unknownTagHistogram[ct.tag.code]++;
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown(%u)", ct.tag.code);
            name = buf;
        }
        tagHistogram[name]++;
        if (code == swf::TagCode::DoAction) doActionCount++;
        if (code == swf::TagCode::DoInitAction) doInitActionCount++;
        if (code == swf::TagCode::DoABC || code == swf::TagCode::DoABC2) {
            doAbcCount++;
            avm2Detected = true;
        }
    }

    printf("\n=== ACTIONSCRIPT VERSION ===\n");
    printf("avm1_present: %s\n", (doActionCount > 0 || doInitActionCount > 0) ? "yes" : "no");
    printf("avm2_present (DoABC/DoABC2 tags): %s\n", avm2Detected ? "yes" : "no");
    printf("DoAction_tag_count: %d\n", doActionCount);
    printf("DoInitAction_tag_count: %d\n", doInitActionCount);
    printf("DoABC_tag_count: %d\n", doAbcCount);

    printf("\n=== TAG HISTOGRAM (recursive incl. nested DefineSprite streams; %zu total tag occurrences) ===\n",
           allTags.size());
    {
        std::vector<std::pair<std::string, int>> sorted(tagHistogram.begin(), tagHistogram.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](auto& a, auto& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
        for (auto& [name, count] : sorted) printf("  %-32s %d\n", name.c_str(), count);
    }

    // --- 3. AVM1 opcode histogram (top-level + nested DoAction/DoInitAction
    // + button actionsV1/condActionsV2) --------------------------------------
    OpcodeStats totalOps;
    int bytecodeBuffersScanned = 0;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        if (code == swf::TagCode::DoAction) {
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            std::vector<uint8_t> body = r.readBytes(ct.tag.bodyLength);
            OpcodeStats s;
            walkActionStream(body.data(), body.size(), s);
            mergeStats(totalOps, s);
            bytecodeBuffersScanned++;
        } else if (code == swf::TagCode::DoInitAction) {
            if (ct.tag.bodyLength < 2) continue;
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            r.readU16();  // SpriteId
            std::vector<uint8_t> body = r.readBytes(ct.tag.bodyLength - 2);
            OpcodeStats s;
            walkActionStream(body.data(), body.size(), s);
            mergeStats(totalOps, s);
            bytecodeBuffersScanned++;
        } else if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto def = swf::parseDefineButton(r, ct.tag.code);
            if (!def) continue;
            if (!def->actionsV1.empty()) {
                OpcodeStats s;
                walkActionStream(def->actionsV1.data(), def->actionsV1.size(), s);
                mergeStats(totalOps, s);
                bytecodeBuffersScanned++;
            }
            for (const auto& cond : def->condActionsV2) {
                OpcodeStats s;
                walkActionStream(cond.actionBytes.data(), cond.actionBytes.size(), s);
                mergeStats(totalOps, s);
                bytecodeBuffersScanned++;
            }
        }
    }

    printf("\n=== AVM1 OPCODE PROFILE (%d bytecode buffers scanned, %d total opcodes, "
           "%d nested function bodies, %d With blocks%s) ===\n",
           bytecodeBuffersScanned, totalOps.totalOpcodes, totalOps.nestedFunctionBodies,
           totalOps.withBlocks, totalOps.truncated ? ", TRUNCATION DETECTED" : "");
    {
        std::vector<std::pair<std::string, int>> sorted(totalOps.counts.begin(), totalOps.counts.end());
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (auto& [name, count] : sorted) {
            printf("  %-20s %6d  %s\n", name.c_str(), count,
                   isParseOnlyStub(name) ? "[PARSED-ONLY, not executed]" : "[supported]");
        }
    }

    // --- 4. AS2 identifier substring scan -----------------------------------
    printf("\n=== AS2 API/IDENTIFIER SCAN (best-effort substring match over decompressed body; "
           "presence only, not proof of semantic usage) ===\n");
    auto idHits = scanAs2Identifiers(movie->data);
    for (const char* id : kAs2Identifiers) {
        auto it = idHits.find(id);
        printf("  %-20s %s\n", id, it != idHits.end() ? "FOUND" : "not found");
    }

    // --- 5. Button profile ---------------------------------------------------
    auto characters = runtime::CharacterDictionary::build(*movie);
    int defineButtonV1Count = 0, defineButtonV2Count = 0;
    int totalButtonRecords = 0, buttonsWithExplicitHitState = 0;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        if (code == swf::TagCode::DefineButton) defineButtonV1Count++;
        if (code == swf::TagCode::DefineButton2) defineButtonV2Count++;
        if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto def = swf::parseDefineButton(r, ct.tag.code);
            if (!def) continue;
            totalButtonRecords += static_cast<int>(def->records.size());
            bool anyHit = false;
            for (auto& rec : def->records) {
                if (rec.stateHitTest) { anyHit = true; break; }
            }
            if (anyHit) buttonsWithExplicitHitState++;
        }
    }

    printf("\n=== BUTTON PROFILE ===\n");
    printf("DefineButton_v1_count: %d\n", defineButtonV1Count);
    printf("DefineButton2_v2_count: %d\n", defineButtonV2Count);
    printf("total_button_records: %d\n", totalButtonRecords);
    printf("buttons_with_explicit_hittest_state: %d\n", buttonsWithExplicitHitState);
    printf("buttons_falling_back_to_upstate_hit_area: %d\n",
           (defineButtonV1Count + defineButtonV2Count) - buttonsWithExplicitHitState);

    if (defineButtonV1Count + defineButtonV2Count > 0) {
        runtime::ScriptEnvironment env;
        auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
        if (root) {
            std::vector<PlacedButtonRecord> placed;
            findPlacedButtonsRecursive(*root, "", placed);
            printf("placed_button_instances_frame1: %zu\n", placed.size());
            for (const auto& rec : placed) {
                printf("  - parent=%s name=%s depth=%d characterId=%u world=(%.2fpx,%.2fpx)\n",
                       rec.parentPath.c_str(), rec.name.empty() ? "(unnamed)" : rec.name.c_str(),
                       rec.depth, rec.characterId, rec.worldX, rec.worldY);
            }
        } else {
            printf("placed_button_instances_frame1: ERROR (createRoot() failed)\n");
        }
    } else {
        printf("placed_button_instances_frame1: 0 (no DefineButton/2 characters in this file)\n");
    }

    // --- 6. Sound profile ------------------------------------------------------
    int defineSoundCount = 0, startSoundCount = 0;
    std::map<std::string, int> soundFormatHistogram;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        if (code == swf::TagCode::StartSound) startSoundCount++;
        if (code == swf::TagCode::DefineSound) {
            defineSoundCount++;
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto def = swf::parseDefineSound(r, ct.tag.bodyOffset);
            if (!def) continue;
            const char* fmt = "unknown";
            switch (def->format) {
                case swf::SoundFormat::kUncompressedNative: fmt = "uncompressed-native"; break;
                case swf::SoundFormat::kAdpcm: fmt = "ADPCM"; break;
                case swf::SoundFormat::kMp3: fmt = "MP3"; break;
                case swf::SoundFormat::kUncompressedLittleEndian: fmt = "uncompressed-LE"; break;
                case swf::SoundFormat::kNellymoser16k: fmt = "Nellymoser-16k"; break;
                case swf::SoundFormat::kNellymoser8k: fmt = "Nellymoser-8k"; break;
                case swf::SoundFormat::kNellymoser: fmt = "Nellymoser"; break;
                case swf::SoundFormat::kSpeex: fmt = "Speex"; break;
            }
            soundFormatHistogram[fmt]++;
        }
    }
    printf("\n=== SOUND PROFILE ===\n");
    printf("DefineSound_count: %d\n", defineSoundCount);
    printf("StartSound_count: %d\n", startSoundCount);
    printf("codec decode implemented: no (this runtime never decodes any sound codec -- see docs/known-limitations.md priority #5)\n");
    for (auto& [fmt, count] : soundFormatHistogram) printf("  format=%-20s count=%d\n", fmt.c_str(), count);

    // --- 7. Rendering feature profile (tag-presence proxy) ----------------------
    auto tagCount = [&](swf::TagCode c) {
        auto it = tagHistogram.find(swf::tagCodeName(static_cast<uint16_t>(c)));
        return it != tagHistogram.end() ? it->second : 0;
    };
    int shapeCount = tagCount(swf::TagCode::DefineShape) + tagCount(swf::TagCode::DefineShape2) +
                      tagCount(swf::TagCode::DefineShape3) + tagCount(swf::TagCode::DefineShape4);
    int gradientFillCount = 0, bitmapFillCount = 0, shapesParsed = 0, shapesUnparseable = 0;
    for (const auto& ct : allTags) {
        auto code = static_cast<swf::TagCode>(ct.tag.code);
        if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
            code == swf::TagCode::DefineShape3) {
            swf::SwfReader r = movie->tagBodyReader(ct.tag);
            auto def = swf::parseDefineShape(r, ct.tag.code);
            if (!def) continue;
            shapesParsed++;
            for (auto& fs : def->shape.fillStyles) {
                if (fs.isGradient()) gradientFillCount++;
                if (fs.isBitmap()) bitmapFillCount++;
            }
        } else if (code == swf::TagCode::DefineShape4) {
            shapesUnparseable++;  // parser doesn't support DefineShape4 at all
        }
    }
    printf("\n=== RENDERING FEATURE PROFILE (tag-presence proxy; DefineShape/2/3 fill styles "
           "inspected directly, DefineShape4 tag-count only -- parser doesn't support it) ===\n");
    printf("shape_tag_count (all versions): %d\n", shapeCount);
    printf("  DefineShape4_count (unparsed by this runtime): %d\n", shapesUnparseable);
    printf("gradient_fill_styles_found: %d (in %d parsed DefineShape/2/3 tags)\n", gradientFillCount, shapesParsed);
    printf("bitmap_fill_styles_found: %d\n", bitmapFillCount);
    printf("sprite_tag_count: %d\n", tagCount(swf::TagCode::DefineSprite));
    printf("morphshape_tag_count: %d\n", tagCount(swf::TagCode::DefineMorphShape) + tagCount(swf::TagCode::DefineMorphShape2));
    printf("bitmap_tag_count (DefineBits/JPEG2/3/4/Lossless/2): %d\n",
           tagCount(swf::TagCode::DefineBits) + tagCount(swf::TagCode::DefineBitsJpeg2) +
               tagCount(swf::TagCode::DefineBitsJpeg3) + tagCount(swf::TagCode::DefineBitsJpeg4) +
               tagCount(swf::TagCode::DefineBitsLossless) + tagCount(swf::TagCode::DefineBitsLossless2));
    printf("text_tag_count (DefineText/2/DefineEditText): %d\n",
           tagCount(swf::TagCode::DefineText) + tagCount(swf::TagCode::DefineText2) +
               tagCount(swf::TagCode::DefineEditText));
    printf("font_tag_count (DefineFont/2/3): %d\n",
           tagCount(swf::TagCode::DefineFont) + tagCount(swf::TagCode::DefineFont2) +
               tagCount(swf::TagCode::DefineFont3));
    printf("PlaceObject3_count (blend modes/filters carrier -- neither is applied by this runtime): %d\n",
           tagCount(swf::TagCode::PlaceObject3));

    // --- 8. Interactivity profile summary ----------------------------------
    printf("\n=== INTERACTIVITY PROFILE SUMMARY ===\n");
    printf("Button2_present: %s\n", (defineButtonV2Count + defineButtonV1Count) > 0 ? "yes" : "no");
    printf("onPress_string_found: %s\n", idHits.count("onPress") ? "yes" : "no");
    printf("onRelease_string_found: %s\n", idHits.count("onRelease") ? "yes" : "no");
    printf("onRollOver_string_found: %s\n", idHits.count("onRollOver") ? "yes" : "no");
    printf("onRollOut_string_found: %s\n", idHits.count("onRollOut") ? "yes" : "no");
    printf("onClipEvent_string_found: %s\n", idHits.count("onClipEvent") ? "yes" : "no");
    printf("Key_isDown_context (Key string found): %s\n", idHits.count("Key") ? "yes" : "no");
    printf("Mouse_string_found: %s\n", idHits.count("Mouse") ? "yes" : "no");
    printf("Sound_string_found: %s\n", idHits.count("Sound") ? "yes" : "no");
    printf("ExternalInterface_string_found: %s\n", idHits.count("ExternalInterface") ? "yes" : "no");

    return 0;
}
