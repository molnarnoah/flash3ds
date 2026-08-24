#include "runtime/CharacterDictionary.h"

#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

namespace {

SpriteDef parseSpriteNestedTags(const Movie& movie, const swf::TagRecord& spriteTag,
                                 uint16_t characterId, uint16_t frameCount) {
    SpriteDef def;
    def.characterId = characterId;
    def.frameCount = frameCount;

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
        def.tags.push_back(tag);
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::End) break;
        full.skip(tag.bodyLength);
        if (full.failed()) {
            LOG_WARN("CHARDICT", "DefineSprite %u: nested tag stream truncated after '%s'",
                      characterId, tag.name.c_str());
        }
    }
    return def;
}

// ExportAssets (tag 56) — per the public SWF spec: Count (UI16), then
// Count records of {Tag1 (UI16 characterId), Name1 (STRING)}. Declares a
// linkage name for an already-defined character, resolved later by AS2's
// attachMovie(linkageName, ...)/Sound.attachSound(linkageName) — see
// CharacterDictionary::findByLinkageName()'s own doc comment.
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

// Every character-defining tag this dictionary can lazily index begins its
// body with a UI16 CharacterId/FontId/SoundId/ButtonId field (per the
// public SWF spec) — confirmed by direct source read of each tag's own
// parser (DefineShapeTag.cpp/DefineSoundTag.cpp/DefineFontTag.cpp/
// DefineTextTag.cpp/DefineButtonTag.cpp/DefineEditTextTag.cpp all read that
// field first, before anything else). Peeking it costs a 2-byte read, not
// a full parse, which is exactly what makes lazy indexing possible: build()
// can learn "this tag defines character N" without paying for N's geometry/
// glyphs/text runs until something actually references N.
bool peekLeadingCharacterId(const Movie& movie, const swf::TagRecord& tag, uint16_t& outId) {
    if (tag.bodyLength < 2) return false;
    swf::SwfReader reader = movie.tagBodyReader(tag);
    outId = reader.readU16();
    return !reader.failed();
}

// Scans one tag list (either the movie's own top-level tags, or a
// DefineSprite's nested control-tag stream) for character-defining tags and
// registers them into `pending`/`parsed`, recursing into any nested
// DefineSprite it finds. SWF's character ID dictionary is GLOBAL across the
// whole file — per the public spec, a character-defining tag may legally
// appear nested inside a DefineSprite's own tag stream rather than only at
// the top level (uncommon from the standard Flash IDE publish pipeline,
// which puts nearly everything at the top level, but legal and seen from
// some authoring tools/obfuscators) — so this must be recursive, not just a
// single pass over movie.tags, or such a character's ID would silently
// never resolve. The same reasoning applies to ExportAssets, so
// `linkageNameToId` is threaded through and populated alongside the
// character indexes.
//
// DefineSprite is parsed eagerly into `parsed` (see this file's header
// comment for why); every other character-defining tag is only indexed
// into `pending` — the real parse happens lazily on first
// CharacterDictionary::find().
void scanTagsForCharacters(const Movie& movie, const std::vector<swf::TagRecord>& tags,
                            std::unordered_map<uint16_t, swf::TagRecord>& pending,
                            std::unordered_map<uint16_t, CharacterDef>& parsed,
                            std::unordered_map<std::string, uint16_t>& linkageNameToId) {
    for (const auto& tag : tags) {
        auto code = static_cast<swf::TagCode>(tag.code);

        if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
            code == swf::TagCode::DefineShape3 || code == swf::TagCode::DefineSound ||
            code == swf::TagCode::DefineFont || code == swf::TagCode::DefineFont2 ||
            code == swf::TagCode::DefineText || code == swf::TagCode::DefineText2 ||
            code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2 ||
            code == swf::TagCode::DefineEditText) {
            uint16_t characterId = 0;
            if (!peekLeadingCharacterId(movie, tag, characterId)) {
                LOG_WARN("CHARDICT", "Failed to peek leading CharacterId of %s at offset=%zu",
                          tag.name.c_str(), tag.bodyOffset);
                continue;
            }
            pending[characterId] = tag;
        } else if (code == swf::TagCode::DefineSprite) {
            if (tag.bodyLength < 4) {
                LOG_WARN("CHARDICT", "DefineSprite at offset=%zu is too short for its header",
                          tag.bodyOffset);
                continue;
            }
            swf::SwfReader header = movie.tagBodyReader(tag);
            uint16_t characterId = header.readU16();
            uint16_t frameCount = header.readU16();
            if (header.failed()) {
                LOG_WARN("CHARDICT", "Failed to parse DefineSprite header at offset=%zu",
                          tag.bodyOffset);
                continue;
            }
            SpriteDef spriteDef = parseSpriteNestedTags(movie, tag, characterId, frameCount);
            scanTagsForCharacters(movie, spriteDef.tags, pending, parsed, linkageNameToId);
            parsed[characterId] = std::move(spriteDef);
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

}  // namespace

CharacterDictionary CharacterDictionary::build(const Movie& movie) {
    CharacterDictionary dict;
    dict.movie_ = &movie;
    scanTagsForCharacters(movie, movie.tags, dict.pending_, dict.parsed_, dict.linkageNameToId_);
    return dict;
}

const CharacterDef* CharacterDictionary::parseAndCache(uint16_t characterId,
                                                         const swf::TagRecord& tag) const {
    auto code = static_cast<swf::TagCode>(tag.code);
    swf::SwfReader reader = movie_->tagBodyReader(tag);

    if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
        code == swf::TagCode::DefineShape3) {
        auto shapeDef = swf::parseDefineShape(reader, tag.code);
        if (!shapeDef) {
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu (lazy)", tag.name.c_str(),
                      tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*shapeDef));
    } else if (code == swf::TagCode::DefineSound) {
        auto soundDef = swf::parseDefineSound(reader, tag.bodyOffset);
        if (!soundDef) {
            LOG_WARN("CHARDICT", "Failed to parse DefineSound at offset=%zu (lazy)", tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*soundDef));
    } else if (code == swf::TagCode::DefineFont) {
        auto fontDef = swf::parseDefineFont(reader);
        if (!fontDef) {
            LOG_WARN("CHARDICT", "Failed to parse DefineFont at offset=%zu (lazy)", tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*fontDef));
    } else if (code == swf::TagCode::DefineFont2) {
        auto fontDef = swf::parseDefineFont2(reader, tag.code);
        if (!fontDef) {
            LOG_WARN("CHARDICT", "Failed to parse DefineFont2 at offset=%zu (lazy)", tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*fontDef));
    } else if (code == swf::TagCode::DefineText || code == swf::TagCode::DefineText2) {
        auto textDef = swf::parseDefineText(reader, tag.code);
        if (!textDef) {
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu (lazy)", tag.name.c_str(),
                      tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*textDef));
    } else if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
        auto buttonDef = swf::parseDefineButton(reader, tag.code);
        if (!buttonDef) {
            LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu (lazy)", tag.name.c_str(),
                      tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*buttonDef));
    } else if (code == swf::TagCode::DefineEditText) {
        auto editTextDef = swf::parseDefineEditText(reader);
        if (!editTextDef) {
            LOG_WARN("CHARDICT", "Failed to parse DefineEditText at offset=%zu (lazy)",
                      tag.bodyOffset);
            return nullptr;
        }
        return &(parsed_[characterId] = std::move(*editTextDef));
    }

    // Unreachable in practice: only tag codes scanTagsForCharacters() put
    // into `pending_` in the first place ever reach here.
    LOG_WARN("CHARDICT", "parseAndCache: unexpected pending tag code %u for character %u",
              tag.code, characterId);
    return nullptr;
}

const CharacterDef* CharacterDictionary::find(uint16_t characterId) const {
    auto parsedIt = parsed_.find(characterId);
    if (parsedIt != parsed_.end()) return &parsedIt->second;

    auto pendingIt = pending_.find(characterId);
    if (pendingIt == pending_.end()) return nullptr;

    // Copy the TagRecord before erasing — parseAndCache() may insert into
    // `parsed_`, which could rehash and invalidate iterators, but never
    // touches `pending_`, so erasing first (by value, not iterator) is
    // safe either order; copying first just avoids any dependence on that.
    swf::TagRecord tag = pendingIt->second;
    pending_.erase(pendingIt);
    return parseAndCache(characterId, tag);
}

const uint16_t* CharacterDictionary::findByLinkageName(const std::string& name) const {
    auto it = linkageNameToId_.find(name);
    return it == linkageNameToId_.end() ? nullptr : &it->second;
}

}  // namespace flash3ds::runtime
