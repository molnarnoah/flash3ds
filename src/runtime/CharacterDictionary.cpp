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

// Scans one tag list (either the movie's own top-level tags, or a
// DefineSprite's nested control-tag stream) for character-defining tags and
// registers them into `dict`, recursing into any nested DefineSprite it
// finds. SWF's character ID dictionary is GLOBAL across the whole file —
// per the public spec, a character-defining tag may legally appear nested
// inside a DefineSprite's own tag stream rather than only at the top level
// (uncommon from the standard Flash IDE publish pipeline, which puts nearly
// everything at the top level, but legal and seen from some authoring
// tools/obfuscators) — so this must be recursive, not just a single pass
// over movie.tags, or such a character's ID would silently never resolve.
void scanTagsForCharacters(const Movie& movie, const std::vector<swf::TagRecord>& tags,
                            std::unordered_map<uint16_t, CharacterDef>& characters) {
    for (const auto& tag : tags) {
        auto code = static_cast<swf::TagCode>(tag.code);

        if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
            code == swf::TagCode::DefineShape3) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto shapeDef = swf::parseDefineShape(reader, tag.code);
            if (shapeDef) {
                characters[shapeDef->characterId] = *shapeDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(),
                          tag.bodyOffset);
            }
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
            scanTagsForCharacters(movie, spriteDef.tags, characters);
            characters[characterId] = std::move(spriteDef);
        } else if (code == swf::TagCode::DefineSound) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto soundDef = swf::parseDefineSound(reader, tag.bodyOffset);
            if (soundDef) {
                characters[soundDef->soundId] = *soundDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse DefineSound at offset=%zu", tag.bodyOffset);
            }
        } else if (code == swf::TagCode::DefineFont) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto fontDef = swf::parseDefineFont(reader);
            if (fontDef) {
                characters[fontDef->fontId] = *fontDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse DefineFont at offset=%zu", tag.bodyOffset);
            }
        } else if (code == swf::TagCode::DefineFont2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto fontDef = swf::parseDefineFont2(reader, tag.code);
            if (fontDef) {
                characters[fontDef->fontId] = *fontDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse DefineFont2 at offset=%zu", tag.bodyOffset);
            }
        } else if (code == swf::TagCode::DefineText || code == swf::TagCode::DefineText2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto textDef = swf::parseDefineText(reader, tag.code);
            if (textDef) {
                characters[textDef->characterId] = *textDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(),
                          tag.bodyOffset);
            }
        } else if (code == swf::TagCode::DefineButton || code == swf::TagCode::DefineButton2) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto buttonDef = swf::parseDefineButton(reader, tag.code);
            if (buttonDef) {
                characters[buttonDef->characterId] = *buttonDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse %s at offset=%zu", tag.name.c_str(),
                          tag.bodyOffset);
            }
        } else if (code == swf::TagCode::DefineEditText) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto editTextDef = swf::parseDefineEditText(reader);
            if (editTextDef) {
                characters[editTextDef->characterId] = *editTextDef;
            } else {
                LOG_WARN("CHARDICT", "Failed to parse DefineEditText at offset=%zu",
                          tag.bodyOffset);
            }
        }
        // Other character-defining tags (DefineBits*/bitmaps) are
        // recognized by TagCode elsewhere but not resolved into the
        // dictionary yet — later phase.
    }
}

}  // namespace

CharacterDictionary CharacterDictionary::build(const Movie& movie) {
    CharacterDictionary dict;
    scanTagsForCharacters(movie, movie.tags, dict.characters_);
    return dict;
}

const CharacterDef* CharacterDictionary::find(uint16_t characterId) const {
    auto it = characters_.find(characterId);
    return it == characters_.end() ? nullptr : &it->second;
}

}  // namespace flash3ds::runtime
