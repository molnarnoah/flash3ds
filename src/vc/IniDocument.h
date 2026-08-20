// IniDocument.h
//
// Virtual Console resource layer: a deliberately tiny, dependency-free INI
// parser. This is the ONLY thing that understands "[section]" / "key=value"
// syntax — GameConfig.h builds on top of this to interpret specific known
// keys; nothing else in the project needs to know INI syntax exists.
//
// Grammar (kept intentionally small, per the spec this was written against):
//   - `[section]` starts a new section. Everything before the first
//     section header belongs to an implicit unnamed section ("").
//   - `key=value` sets a key within the current section. Leading/trailing
//     whitespace around both key and value is trimmed. A later `key=` in
//     the SAME section overwrites an earlier one (last one wins).
//   - `;` or `#` at the start of a trimmed line marks a comment; blank
//     lines are ignored.
//   - Any line that isn't a comment, blank, `[section]`, or `key=value`
//     (no '=' found) is a malformed line — recorded in malformedLines()
//     (0-based line index) and otherwise skipped, never fatal. This is
//     what backs "invalid config entry: ignore it and use its default" —
//     IniDocument doesn't know what a "default" is, it just never lets a
//     bad line stop parsing or corrupt an unrelated key.
//   - Unknown sections/keys are simply stored; it's the CALLER's job (see
//     GameConfig) to only read the keys it recognizes and ignore the rest
//     — IniDocument itself has no concept of "known" vs "unknown".

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flash3ds::vc {

class IniDocument {
public:
    // Parses `text` (the full contents of a .ini file, or "" for an empty/
    // missing file — both produce an IniDocument with zero sections/keys,
    // which is exactly what "use documented defaults" needs downstream).
    static IniDocument parse(const std::string& text);

    // Returns the raw string value of `section`/`key`, or std::nullopt if
    // that section or key was never set. Section/key matching is exact
    // (case-sensitive), matching the documented config.ini examples.
    std::optional<std::string> getString(const std::string& section,
                                          const std::string& key) const;

    // True if `section` appeared at least once (even with zero keys).
    bool hasSection(const std::string& section) const;

    // 0-based line indices IniDocument couldn't parse as a comment, blank
    // line, section header, or key=value pair. Never affects parsing of
    // any other line. Exposed mainly for tests/diagnostics.
    const std::vector<size_t>& malformedLines() const { return malformedLines_; }

private:
    // section name -> (key -> value). The implicit unnamed section (before
    // any "[...]" header) is stored under "".
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections_;
    std::vector<size_t> malformedLines_;
};

}  // namespace flash3ds::vc
