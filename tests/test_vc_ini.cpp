// test_vc_ini.cpp — Virtual Console resource layer: IniDocument parser.

#include "TestFramework.h"
#include "vc/IniDocument.h"

using flash3ds::vc::IniDocument;

TEST_CASE(Ini_Valid_ParsesSectionsAndKeys) {
    auto doc = IniDocument::parse(
        "[game]\n"
        "swf=custom.swf\n"
        "\n"
        "[input]\n"
        "A=SPACE\n");

    CHECK_EQ(doc.getString("game", "swf").value(), std::string("custom.swf"));
    CHECK_EQ(doc.getString("input", "A").value(), std::string("SPACE"));
}

TEST_CASE(Ini_Missing_ProducesEmptyDocument) {
    // "Missing file" is represented by the caller passing "" -- see
    // GamePackage.cpp -- so this is really the same as Ini_Empty below,
    // tested explicitly under its own name for clarity/traceability.
    auto doc = IniDocument::parse("");
    CHECK(!doc.getString("game", "swf").has_value());
    CHECK(doc.malformedLines().empty());
}

TEST_CASE(Ini_Empty_ProducesEmptyDocument) {
    auto doc = IniDocument::parse("");
    CHECK(!doc.hasSection("game"));
    CHECK(doc.malformedLines().empty());
}

TEST_CASE(Ini_Comments_AreIgnored) {
    auto doc = IniDocument::parse(
        "; this is a comment\n"
        "# so is this\n"
        "[game]\n"
        "; swf=should_not_appear.swf\n"
        "swf=real.swf\n");

    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
    CHECK(doc.malformedLines().empty());
}

TEST_CASE(Ini_UnknownSection_IsStoredButHarmless) {
    auto doc = IniDocument::parse(
        "[totally_unknown_section]\n"
        "whatever=1\n"
        "[game]\n"
        "swf=real.swf\n");

    CHECK(doc.hasSection("totally_unknown_section"));
    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
}

TEST_CASE(Ini_UnknownKey_IsStoredButHarmless) {
    auto doc = IniDocument::parse(
        "[game]\n"
        "swf=real.swf\n"
        "some_future_key=123\n");

    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
    CHECK_EQ(doc.getString("game", "some_future_key").value(), std::string("123"));
}

TEST_CASE(Ini_DuplicateSetting_LastOneWins) {
    auto doc = IniDocument::parse(
        "[game]\n"
        "swf=first.swf\n"
        "swf=second.swf\n");

    CHECK_EQ(doc.getString("game", "swf").value(), std::string("second.swf"));
}

TEST_CASE(Ini_MalformedLine_IsRecordedAndSkipped_ParsingContinues) {
    auto doc = IniDocument::parse(
        "[game]\n"
        "this line has no equals sign\n"
        "swf=real.swf\n");

    CHECK_EQ(doc.malformedLines().size(), static_cast<size_t>(1));
    CHECK_EQ(doc.malformedLines()[0], static_cast<size_t>(1));  // 0-based: line 2
    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
}

TEST_CASE(Ini_KeyBeforeAnySectionHeader_GoesToImplicitUnnamedSection) {
    auto doc = IniDocument::parse("loose=value\n[game]\nswf=real.swf\n");

    CHECK_EQ(doc.getString("", "loose").value(), std::string("value"));
    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
}

TEST_CASE(Ini_WhitespaceAroundKeysAndValues_IsTrimmed) {
    auto doc = IniDocument::parse("[game]\n   swf   =   real.swf   \n");
    CHECK_EQ(doc.getString("game", "swf").value(), std::string("real.swf"));
}
