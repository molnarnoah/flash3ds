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

}  // namespace

CharacterDictionary CharacterDictionary::build(const Movie& movie) {
    CharacterDictionary dict;

    for (const auto& tag : movie.tags) {
        auto code = static_cast<swf::TagCode>(tag.code);

        if (code == swf::TagCode::DefineShape || code == swf::TagCode::DefineShape2 ||
            code == swf::TagCode::DefineShape3) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto shapeDef = swf::parseDefineShape(reader, tag.code);
            if (shapeDef) {
                dict.characters_[shapeDef->characterId] = *shapeDef;
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
            dict.characters_[characterId] =
                parseSpriteNestedTags(movie, tag, characterId, frameCount);
        }
        // Other character-defining tags (DefineBits*, DefineText*,
        // DefineFont*, DefineButton*) are recognized by TagCode elsewhere
        // but not resolved into the dictionary yet — Phase 8+.
    }

    return dict;
}

const CharacterDef* CharacterDictionary::find(uint16_t characterId) const {
    auto it = characters_.find(characterId);
    return it == characters_.end() ? nullptr : &it->second;
}

}  // namespace flash3ds::runtime
