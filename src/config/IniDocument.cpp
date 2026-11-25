#include "config/IniDocument.hpp"

#include <format>

namespace spindle::config {

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

IniDocument IniDocument::parse(std::string_view text, std::vector<IniIssue>* issues) {
    IniDocument doc;
    std::string current;
    int lineNo = 0;
    // Strip a UTF-8 BOM written by some Windows editors.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view rawLine = text.substr(pos, end - pos);
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.remove_suffix(1);
        ++lineNo;
        const bool lastLine = end >= text.size();
        if (lastLine && rawLine.empty()) break;

        Line line;
        line.raw = std::string(rawLine);
        const std::string_view t = trim(rawLine);
        if (t.empty()) {
            line.kind = Kind::Blank;
        } else if (t.front() == ';' || t.front() == '#') {
            line.kind = Kind::Comment;
        } else if (t.front() == '[') {
            if (t.back() == ']' && t.size() > 2) {
                line.kind = Kind::Section;
                current = std::string(trim(t.substr(1, t.size() - 2)));
                line.section = current;
            } else {
                line.kind = Kind::Invalid;
                if (issues) issues->push_back({lineNo, std::string(t), "malformed section header"});
            }
        } else {
            const std::size_t eq = t.find('=');
            if (eq == std::string_view::npos || trim(t.substr(0, eq)).empty()) {
                line.kind = Kind::Invalid;
                if (issues) issues->push_back({lineNo, std::string(t), "expected key = value"});
            } else {
                line.kind = Kind::Pair;
                line.section = current;
                line.key = std::string(trim(t.substr(0, eq)));
                line.value = std::string(trim(t.substr(eq + 1)));
                if (issues && doc.findPair(line.section, line.key)) {
                    issues->push_back({lineNo, std::string(t),
                                       std::format("duplicate key '{}' in [{}]; the last one wins", line.key, line.section)});
                }
            }
        }
        doc.lines_.push_back(std::move(line));
        if (lastLine) break;
        pos = end + 1;
    }
    return doc;
}

std::optional<std::size_t> IniDocument::findPair(std::string_view section, std::string_view key) const {
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        if (l.kind == Kind::Pair && equalsIgnoreCase(l.section, section) && equalsIgnoreCase(l.key, key)) found = i;
    }
    return found;
}

std::optional<std::string> IniDocument::get(std::string_view section, std::string_view key) const {
    if (auto i = findPair(section, key)) return lines_[*i].value;
    return std::nullopt;
}

void IniDocument::set(std::string_view section, std::string_view key, std::string_view value,
                      std::string_view commentIfNew) {
    if (auto i = findPair(section, key)) {
        Line& l = lines_[*i];
        l.value = std::string(value);
        l.raw = std::format("{} = {}", l.key, l.value);
        return;
    }
    Line pair;
    pair.kind = Kind::Pair;
    pair.section = std::string(section);
    pair.key = std::string(key);
    pair.value = std::string(value);
    pair.raw = std::format("{} = {}", key, value);

    std::vector<Line> insert;
    if (!commentIfNew.empty()) {
        std::size_t p = 0;
        while (p <= commentIfNew.size()) {
            std::size_t e = commentIfNew.find('\n', p);
            if (e == std::string_view::npos) e = commentIfNew.size();
            Line c;
            c.kind = Kind::Comment;
            c.raw = std::format("; {}", commentIfNew.substr(p, e - p));
            insert.push_back(std::move(c));
            if (e >= commentIfNew.size()) break;
            p = e + 1;
        }
    }
    insert.push_back(std::move(pair));

    // Find the section header; the block ends at the next header.
    std::optional<std::size_t> header;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind == Kind::Section && equalsIgnoreCase(lines_[i].section, section)) {
            header = i;
            break;
        }
    }
    if (!header && !section.empty()) {
        if (!lines_.empty() && lines_.back().kind != Kind::Blank) lines_.push_back(Line{});
        Line h;
        h.kind = Kind::Section;
        h.section = std::string(section);
        h.raw = std::format("[{}]", section);
        lines_.push_back(std::move(h));
        for (Line& l : insert) lines_.push_back(std::move(l));
        return;
    }
    std::size_t blockStart = header ? *header + 1 : 0;
    std::size_t blockEnd = blockStart;
    while (blockEnd < lines_.size() && lines_[blockEnd].kind != Kind::Section) ++blockEnd;
    // Insert after the last non-blank line of the block so trailing spacing is kept.
    std::size_t at = blockEnd;
    while (at > blockStart && lines_[at - 1].kind == Kind::Blank) --at;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(insert.begin()),
                  std::make_move_iterator(insert.end()));
}

std::string IniDocument::serialize() const {
    std::string out;
    for (const Line& l : lines_) {
        out += l.raw;
        out += '\n';
    }
    return out;
}

} // namespace spindle::config
