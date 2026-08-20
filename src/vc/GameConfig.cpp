#include "vc/GameConfig.h"

#include <cctype>
#include <unordered_map>

#include "platform/Log.h"
#include "vc/IniDocument.h"

namespace flash3ds::vc {

using runtime::InputState;

namespace {

// Named key tokens config.ini may use in [input] -- deliberately reuses
// InputState::KeyCode's own values (see that enum) rather than defining a
// second table of magic numbers. Only the non-printable/named keys need an
// entry here; single printable characters ("L", "R", "X", "Y", "A", ...)
// are handled generically below without needing to be listed.
const std::unordered_map<std::string, int>& namedKeyTokens() {
    static const std::unordered_map<std::string, int> table = {
        {"BACKSPACE", InputState::kBackspace}, {"TAB", InputState::kTab},
        {"ENTER", InputState::kEnter},         {"SHIFT", InputState::kShift},
        {"CONTROL", InputState::kControl},     {"ALT", InputState::kAlt},
        {"CAPSLOCK", InputState::kCapsLock},   {"ESCAPE", InputState::kEscape},
        {"SPACE", InputState::kSpace},         {"PAGEUP", InputState::kPageUp},
        {"PAGEDOWN", InputState::kPageDown},   {"END", InputState::kEnd},
        {"HOME", InputState::kHome},           {"LEFT", InputState::kLeft},
        {"UP", InputState::kUp},               {"RIGHT", InputState::kRight},
        {"DOWN", InputState::kDown},           {"INSERT", InputState::kInsert},
        {"DELETE", InputState::kDelete},
    };
    return table;
}

// Applies `parseKeyToken(rawValue)` to `outKeyCode` if present and valid;
// otherwise logs and leaves `outKeyCode` at whatever it already was (the
// caller seeds it with the field's default beforehand) -- this is the
// "invalid entry: ignore it, keep the default" rule, applied per-field.
void applyKeyMapping(const IniDocument& doc, const char* section, const char* key,
                      int& outKeyCode) {
    auto raw = doc.getString(section, key);
    if (!raw) return;  // key not present -- keep existing default, no warning needed

    auto parsed = parseKeyToken(*raw);
    if (!parsed) {
        LOG_WARN("VC", "config.ini [%s] %s='%s' is not a recognized key token -- keeping default",
                 section, key, raw->c_str());
        return;
    }
    outKeyCode = *parsed;
}

// Parses "true"/"false" (case-insensitive), "1"/"0" -- anything else is
// invalid and leaves `outValue` at its existing default.
void applyBool(const IniDocument& doc, const char* section, const char* key, bool& outValue) {
    auto raw = doc.getString(section, key);
    if (!raw) return;

    std::string v;
    for (char c : *raw) v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (v == "true" || v == "1") {
        outValue = true;
    } else if (v == "false" || v == "0") {
        outValue = false;
    } else {
        LOG_WARN("VC", "config.ini [%s] %s='%s' is not a valid boolean -- keeping default",
                 section, key, raw->c_str());
    }
}

}  // namespace

std::optional<int> parseKeyToken(const std::string& token) {
    if (token.empty()) return std::nullopt;

    if (token.size() == 1) {
        // Single printable ASCII character -- direct pass-through, same
        // convention platform::Nintendo3DSInput already used for L/R/X/Y
        // before this layer existed (see that file's header comment).
        unsigned char c = static_cast<unsigned char>(token[0]);
        if (c >= 0x20 && c < 0x7F) return static_cast<int>(c);
        return std::nullopt;
    }

    const auto& table = namedKeyTokens();
    auto it = table.find(token);
    if (it != table.end()) return it->second;

    return std::nullopt;
}

GameConfig GameConfig::fromIniText(const std::string& iniText) {
    GameConfig config;  // seeded with documented defaults (see GameConfig.h)

    IniDocument doc = IniDocument::parse(iniText);

    if (auto swf = doc.getString("game", "swf")) {
        if (!swf->empty()) {
            config.swfFilename = *swf;
        } else {
            LOG_WARN("VC", "config.ini [game] swf='' is empty -- keeping default '%s'",
                     config.swfFilename.c_str());
        }
    }

    applyKeyMapping(doc, "input", "A", config.input.aKeyCode);
    applyKeyMapping(doc, "input", "B", config.input.bKeyCode);
    applyKeyMapping(doc, "input", "X", config.input.xKeyCode);
    applyKeyMapping(doc, "input", "Y", config.input.yKeyCode);
    applyKeyMapping(doc, "input", "L", config.input.lKeyCode);
    applyKeyMapping(doc, "input", "R", config.input.rKeyCode);
    applyKeyMapping(doc, "input", "START", config.input.startKeyCode);
    applyKeyMapping(doc, "input", "SELECT", config.input.selectKeyCode);

    applyBool(doc, "touch", "enabled", config.input.touchEnabled);
    if (auto screen = doc.getString("touch", "screen")) {
        if (*screen == "bottom") {
            config.input.touchUsesBottomScreen = true;
        } else if (*screen == "top") {
            config.input.touchUsesBottomScreen = false;
        } else {
            LOG_WARN("VC",
                     "config.ini [touch] screen='%s' is not 'bottom' or 'top' -- keeping default",
                     screen->c_str());
        }
    }

    applyBool(doc, "mouse", "enabled", config.input.mouseEnabled);

    // Unknown sections/keys (anything not read above -- e.g. a future
    // typo'd key, or a whole unrecognized [section]) are never visited by
    // any of the applyX() calls above, so they're implicitly ignored, per
    // spec, with no special-case code needed for them here.

    for (size_t lineIndex : doc.malformedLines()) {
        LOG_WARN("VC", "config.ini line %zu could not be parsed (not a comment, section header, "
                       "or key=value pair) -- ignored",
                 lineIndex + 1);
    }

    return config;
}

GameConfig GameConfig::defaults() { return fromIniText(""); }

}  // namespace flash3ds::vc
