#include "vc/IniDocument.h"

#include <sstream>

namespace flash3ds::vc {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

}  // namespace

IniDocument IniDocument::parse(const std::string& text) {
    IniDocument doc;

    std::istringstream stream(text);
    std::string rawLine;
    std::string currentSection;  // "" == implicit unnamed section
    doc.sections_[currentSection];  // always present, even if empty

    size_t lineIndex = 0;
    while (std::getline(stream, rawLine)) {
        std::string line = trim(rawLine);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            ++lineIndex;
            continue;
        }

        if (line.front() == '[' && line.back() == ']' && line.size() >= 2) {
            currentSection = trim(line.substr(1, line.size() - 2));
            doc.sections_[currentSection];  // ensure it exists even if empty
            ++lineIndex;
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            // Not a comment, not a section header, no '=' -- malformed.
            // Recorded, never fatal; parsing continues with the next line.
            doc.malformedLines_.push_back(lineIndex);
            ++lineIndex;
            continue;
        }

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty()) {
            // "=value" with no key on the left -- also malformed.
            doc.malformedLines_.push_back(lineIndex);
            ++lineIndex;
            continue;
        }

        // Last one wins on a duplicate key within the same section --
        // plain map assignment already gives us that for free.
        doc.sections_[currentSection][key] = value;
        ++lineIndex;
    }

    return doc;
}

std::optional<std::string> IniDocument::getString(const std::string& section,
                                                    const std::string& key) const {
    auto sectionIt = sections_.find(section);
    if (sectionIt == sections_.end()) return std::nullopt;

    auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end()) return std::nullopt;

    return keyIt->second;
}

bool IniDocument::hasSection(const std::string& section) const {
    return sections_.count(section) != 0;
}

}  // namespace flash3ds::vc
