#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace spindle::config {

struct IniIssue {
    int line = 0;
    std::string text;
    std::string message;
};

// Line-preserving INI model: comments, blank lines and key order survive a load/modify/save cycle.
// Section and key lookups are ASCII case-insensitive. Comments start with ';' or '#' at line start.
class IniDocument {
public:
    static IniDocument parse(std::string_view text, std::vector<IniIssue>* issues = nullptr);

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    bool has(std::string_view section, std::string_view key) const { return get(section, key).has_value(); }

    // Updates the last occurrence of the key, or inserts it at the end of the section (creating the
    // section at the end of the document if needed). commentIfNew is written above a newly added key.
    void set(std::string_view section, std::string_view key, std::string_view value,
             std::string_view commentIfNew = {});

    std::string serialize() const;

private:
    enum class Kind { Blank, Comment, Section, Pair, Invalid };
    struct Line {
        Kind kind = Kind::Blank;
        std::string raw;
        std::string section; // section this line belongs to (for pairs), or the header name
        std::string key;
        std::string value;
    };

    std::optional<std::size_t> findPair(std::string_view section, std::string_view key) const;

    std::vector<Line> lines_;
};

std::string toLowerAscii(std::string_view s);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
std::string_view trim(std::string_view s);

} // namespace spindle::config
