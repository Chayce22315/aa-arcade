#include <filesystem>

#include "Test.hpp"
#include "config/AtomicFile.hpp"
#include "config/IniDocument.hpp"

using spindle::config::IniDocument;
using spindle::config::IniIssue;

TEST_CASE(ini_parse_get_case_insensitive) {
    std::vector<IniIssue> issues;
    auto doc = IniDocument::parse("; top comment\n[Display]\nVsync = 1\nresolution=1920x1080  \n\n[Audio]\nmaster_volume = 50\n", &issues);
    CHECK(issues.empty());
    CHECK_EQ(doc.get("display", "VSYNC").value_or(""), std::string("1"));
    CHECK_EQ(doc.get("Display", "resolution").value_or(""), std::string("1920x1080"));
    CHECK_EQ(doc.get("Audio", "master_volume").value_or(""), std::string("50"));
    CHECK(!doc.get("Audio", "missing").has_value());
}

TEST_CASE(ini_reports_malformed_and_duplicates) {
    std::vector<IniIssue> issues;
    auto doc = IniDocument::parse("[A]\nkey = 1\nnot a pair\n[broken\nkey = 2\n", &issues);
    CHECK_EQ(issues.size(), std::size_t{3});
    CHECK_EQ(doc.get("A", "key").value_or(""), std::string("2")); // last wins
}

TEST_CASE(ini_round_trip_preserves_comments_and_order) {
    const std::string text = "; keep me\n[Display]\n; and me\nvsync = 1\n\n[Audio]\nmaster_volume = 50\n";
    auto doc = IniDocument::parse(text);
    CHECK_EQ(doc.serialize(), text);
    doc.set("Display", "vsync", "0");
    CHECK_EQ(doc.serialize(), std::string("; keep me\n[Display]\n; and me\nvsync = 0\n\n[Audio]\nmaster_volume = 50\n"));
}

TEST_CASE(ini_set_inserts_into_section_and_creates_sections) {
    auto doc = IniDocument::parse("[Display]\nvsync = 1\n\n[Audio]\nmaster_volume = 50\n");
    doc.set("Display", "max_fps", "144", "frame cap");
    doc.set("Bots", "difficulty", "hard");
    const std::string out = doc.serialize();
    CHECK_EQ(out, std::string("[Display]\nvsync = 1\n; frame cap\nmax_fps = 144\n\n[Audio]\nmaster_volume = 50\n\n[Bots]\ndifficulty = hard\n"));
    auto again = IniDocument::parse(out);
    CHECK_EQ(again.get("Display", "max_fps").value_or(""), std::string("144"));
}

TEST_CASE(ini_handles_bom_and_crlf) {
    auto doc = IniDocument::parse("\xEF\xBB\xBF[Display]\r\nvsync = 0\r\n");
    CHECK_EQ(doc.get("Display", "vsync").value_or(""), std::string("0"));
}

TEST_CASE(atomic_file_write_and_replace) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "spindle_test_atomic";
    fs::remove_all(dir);
    const fs::path file = dir / "settings.ini";
    std::string err;
    CHECK(spindle::config::writeFileAtomic(file, "first", &err));
    CHECK(spindle::config::writeFileAtomic(file, "second", &err));
    CHECK_EQ(spindle::config::readFile(file).value_or(""), std::string("second"));
    CHECK(!fs::exists(dir / "settings.ini.tmp"));
    CHECK(spindle::config::isDirectoryWritable(dir));
    fs::remove_all(dir);
}
