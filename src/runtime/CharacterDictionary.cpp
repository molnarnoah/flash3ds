#include "runtime/CharacterDictionary.h"

#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

namespace {

// ExportAssets (tag 56) — per the public SWF spec: Count (UI16), then
// Count records of {Tag1 (UI16 characterId), Name1 (STRING)}. Declares a
// linkage name for an already-defined character, resolved later by AS2's
// attachMovie(linkageName, ...)/Sound.attachSound(linkageName) — see
// CharacterDictionary::findByLinkageName()'s own doc comment. Stays a
// plain free function (unlike scanTagsForCharacters/parseOneCharacter):
// it only ever writes into the `linkageNameToId` map that's handed to it
// by reference, never touches a CharacterDictionary's private members
// directly, so it doesn't need member/static-member access.
void parseExportAssets(const Movie& movie, const swf::TagRecord& tag,
                        std::unordered_map<std::string, uint16_t>& linkageNameToId) {
    swf::SwfReader reader = movie.tagBodyReader(tag);
    uint16_t count = reader.readU16();
    for (uint16_t i = 0; i < count && !reader.failed(); ++i) {
        uint16_t characterId = reader.readU16();
        std::string name = reader.readCString();
        if (reader.failed()) break;
        linkageNameToId[name] = characterId;
    }
    if (reader.failed()) {
        LOG_WARN("CHARDICT", "ExportAssets at offset=%zu truncated after %zu of %u entries",
                  tag.bodyOffset, linkageNameToId.size(), count);
    }
}

}  // namespace

std::vector<swf::TagRecord> CharacterDictionary::walkSpriteTagStream(const Movie& movie,
                                                                       const swf::TagRecord& spriteTag,
                                                                       uint16_t characterId) {
    std::vector<swf::TagRecord> tags;

    // Scan the nested tag stream using a reader over the *full* Movie::data
    // buffer (seeked to just past CharacterId+FrameCount), not an isolated
    // sub-reader — that way every TagRecord::bodyOffset recorded below is
    // an absolute offset into Movie::data, exactly like a top-level tag,
    // so Movie::tagBodyReader() and Timeline both work on it unmodified.
    swf::SwfReader full(movie.data.data(), movie.data.size());
    full.seek(spriteTag.bodyOffset + 4);
    size_t endOffset = spriteTag.bodyOffset + spriteTag.bodyLength;

    while (full.position() < endOffset && !full.failed()) {
        swf::TagRecord tag;
        if (!swf::TagDispatcher::readTagHeader(full, tag)) break;
        tags.push_back(tag);
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::End) break;
        full.skip(tag.bodyLength);
        if (full.failed()) {
            LOG_WARN("CHARDICT", "DefineSprite %u: nested tag stream truncated after '%s'",
                      characterId, tag.name.c_str());
        }
    }
    return tags;
}

// Scans one tag list (either the movie's own top-level tags, or a
// DefineSprite's nested control-tag stream) for character-defining tags and
// registers each as a PendingCharacter, recursing into any nested
// DefineSprite it finds. SWF's character ID dictionary is GLOBAL across the
// whole file — per the public spec, a character-defining tag may legally
// appear nested inside a DefineSprite's own tag stream rather than only at
// the top level (uncommon from the standard Flash IDE publish pipeline,
// which puts nearly everything at the top level, but legal and seen from
// some authoring tools/obfuscators) — so this must be recursive, not just a
// single pass over movie.tags, or such a character's ID would silently
// never be registered at all. The same reasoning applies to ExportAssets,
// so `linkageNameToId` is threaded through and populated alongside
// `pending`.
//
// Phase 5: unlike the old eager version of this function, this only reads
// each character-defining tag's leading UI16 ID field (every one of them
// per the public spec — ShapeId/SpriteID/SoundID/FontID/CharacterId — see
// this file's header comment) and records a PendingCharacter; it does NOT
// call any of the parseDefineShape/parseDefineSound/etc. per-type parsers.
// That work is deferred to parseOneCharacter(), invoked lazily from
// find(). A DefineSprite still needs its CharacterId+FrameCount header
// read here (4 bytes, not just the ID) purely so this function can seek
// past it to walk the nested stream for further nested IDs — the
// FrameCount value itself is discarded here and re-read later by
// parseOneCharacter() when the sprite is actually parsed.
void CharacterDictionary::scanTagsForCharacters(
    const Movie& movie, const std::vector<swf::TagRecord>& tags,
    std::unordered_map<uint16_t, PendingCharacter>& pending,
    std::unordered_map<std::string, uint16_t>& linkageNameToId) {
    for (const auto& tag : tags) {
        auto code = static_cast<swf::TagCode>(tag.code);

        if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
            code == swf::TagCode::DefineShape3) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t shapeId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for %s at offset=%zu",
                          tag.name.c_str(), tag.bodyOffset);
                continue;
            }
            pending[shapeId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineSprite) {
            if (tag.bodyLength < 4) {
                LOG_WARN("CHARDICT", "DefineSprite at offset=%zu is too short for its header",
                          tag.bodyOffset);
                continue;
            }
            swf::SwfReader header = movie.tagBodyReader(tag);
            uint16_t characterId = header.readU16();
            header.readU16();  // frameCount — re-read later by parseOneCharacter(); see comment above
            if (header.failed()) {
                LOG_WARN("CHARDICT", "Failed to parse DefineSprite header at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            pending[characterId] = PendingCharacter{tag, code};
            std::vector<swf::TagRecord> nestedTags = walkSpriteTagStream(movie, tag, characterId);
            scanTagsForCharacters(movie, nestedTags, pending, linkageNameToId);
        } else if (code == swf::TagCode::DefineSound) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t soundId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for DefineSound at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            pending[soundId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineFont) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t fontId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for DefineFont at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            pending[fontId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineFont2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t fontId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for DefineFont2 at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            pending[fontId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineText || code == swf::TagCode::DefineText2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t characterId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for %s at offset=%zu",
                          tag.name.c_str(), tag.bodyOffset);
                continue;
            }
            pending[characterId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t characterId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for %s at offset=%zu",
                          tag.name.c_str(), tag.bodyOffset);
                continue;
            }
            pending[characterId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::DefineEditText) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            uint16_t characterId = reader.readU16();
            if (reader.failed()) {
                LOG_WARN("CHARDICT", "Failed to read character ID for DefineEditText at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            pending[characterId] = PendingCharacter{tag, code};
        } else if (code == swf::TagCode::ExportAssets) {
            parseExportAssets(movie, tag, linkageNameToId);
        }
        // Other character-defining tags (DefineBits*/bitmaps,
        // DefineMorphShape/2 — confirmed present in real hobo.swf content
        // via Phase 9 testing, 19 occurrences) are recognized by TagCode
        // elsewhere but not resolved into the dictionary yet — later
        // phase. A PlaceObject2 referencing one of these silently places
        // nothing (SceneRenderer::renderCharacter finds no CharacterDef
        // and skips it), matching how unresolved characters are already
        // handled.
    }
}

// Fully parses one pending character, dispatching on `code` to the exact
// same per-type parser (and, on failure, the exact same LOG_WARN message)
// the old eager scanTagsForCharacters() used to call inline. This is the
// function find() invokes on a character ID's first reference.
std::optional<CharacterDef> CharacterDictionary::parseOneCharacter(const Movie& movie,
                                                                     const swf::TagRecord& tag,
                                                                     swf::TagCode code) {
    switch (code) {
        case swf::TagCode::DefineShape:
        case swf::TagCode::DefineShape2:
        case swf::TagCode::DefineShape3: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto shapeDef = swf::parseDefineShape(reader, tag.code);
            if (shapeDef) return CharacterDef(*shapeDef);
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(), tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineSprite: {
            if (tag.bodyLength < 4) {
                LOG_WARN("CHARDICT", "DefineSprite at offset=%zu is too short for its header",
                          tag.bodyOffset);
                return std::nullopt;
            }
            swf::SwfReader header = movie.tagBodyReader(tag);
            uint16_t characterId = header.readU16();
            uint16_t frameCount = header.readU16();
            if (header.failed()) {
                LOG_WARN("CHARDICT", "Failed to parse DefineSprite header at offset=%zu",
                          tag.bodyOffset);
                return std::nullopt;
            }
            SpriteDef def;
            def.characterId = characterId;
            def.frameCount = frameCount;
            def.tags = walkSpriteTagStream(movie, tag, characterId);
            return CharacterDef(std::move(def));
        }
        case swf::TagCode::DefineSound: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto soundDef = swf::parseDefineSound(reader, tag.bodyOffset);
            if (soundDef) return CharacterDef(*soundDef);
            LOG_WARN("CHARDICT", "Failed to parse DefineSound at offset=%zu", tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineFont: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto fontDef = swf::parseDefineFont(reader);
            if (fontDef) return CharacterDef(*fontDef);
            LOG_WARN("CHARDICT", "Failed to parse DefineFont at offset=%zu", tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineFont2: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto fontDef = swf::parseDefineFont2(reader, tag.code);
            if (fontDef) return CharacterDef(*fontDef);
            LOG_WARN("CHARDICT", "Failed to parse DefineFont2 at offset=%zu", tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineText:
        case swf::TagCode::DefineText2: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto textDef = swf::parseDefineText(reader, tag.code);
            if (textDef) return CharacterDef(*textDef);
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(), tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineButton:
        case swf::TagCode::DefineButton2: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto buttonDef = swf::parseDefineButton(reader, tag.code);
            if (buttonDef) return CharacterDef(*buttonDef);
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(), tag.bodyOffset);
            return std::nullopt;
        }
        case swf::TagCode::DefineEditText: {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto editTextDef = swf::parseDefineEditText(reader);
            if (editTextDef) return CharacterDef(*editTextDef);
            LOG_WARN("CHARDICT", "Failed to parse DefineEditText at offset=%zu", tag.bodyOffset);
            return std::nullopt;
        }
        default:
            // Shouldn't happen — scanTagsForCharacters() only ever
            // registers a PendingCharacter for a code this switch handles
            // above — but fail safe rather than silently returning garbage.
            LOG_WARN("CHARDICT", "parseOneCharacter: unexpected tag code %d at offset=%zu",
                      static_cast<int>(code), tag.bodyOffset);
            return std::nullopt;
    }
}

CharacterDictionary CharacterDictionary::build(const Movie& movie) {
    CharacterDictionary dict;
    scanTagsForCharacters(movie, movie.tags, dict.pendingCharacters_, dict.linkageNameToId_);
    dict.movie_ = &movie;
    return dict;
}

const CharacterDef* CharacterDictionary::find(uint16_t characterId) const {
    auto parsedIt = parsedCharacters_.find(characterId);
    if (parsedIt != parsedCharacters_.end()) {
        return &parsedIt->second;
    }

    auto pendingIt = pendingCharacters_.find(characterId);
    if (pendingIt == pendingCharacters_.end()) {
        return nullptr;
    }

    // Copy out before erasing — parseOneCharacter() only needs the tag +
    // code by value, and erasing first keeps `size()` (pending + parsed)
    // correct even if parseOneCharacter() below fails and never reaches
    // the parsedCharacters_.emplace() that would otherwise account for it.
    PendingCharacter pending = pendingIt->second;
    pendingCharacters_.erase(pendingIt);

    std::optional<CharacterDef> parsed = parseOneCharacter(*movie_, pending.tag, pending.code);
    if (!parsed) {
        // A genuinely malformed character: same external behavior as the
        // old eager design's parse failure (skipped, so find() misses it
        // forever) — it's already gone from pendingCharacters_ above, so
        // this doesn't cost a repeated parse attempt on a later find().
        return nullptr;
    }

    auto [insertedIt, inserted] = parsedCharacters_.emplace(characterId, std::move(*parsed));
    return &insertedIt->second;
}

const uint16_t* CharacterDictionary::findByLinkageName(const std::string& name) const {
    auto it = linkageNameToId_.find(name);
    return it == linkageNameToId_.end() ? nullptr : &it->second;
}

}  // namespace flash3ds::runtime
