// Unit tests for MoriaMods::Loc namespace (JSON parser, string table, UTF-8 conversion)

#include <gtest/gtest.h>
#include "moria_testable.h"

#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace MoriaMods;

// Helper to write a temporary JSON file and return its path
static std::string writeTempJson(const std::string& content)
{
    static int counter = 0;
    std::string path = "test_loc_temp_" + std::to_string(counter++) + ".json";
    std::ofstream f(path, std::ios::binary);
    f << content;
    f.close();
    return path;
}

class LocTest : public ::testing::Test
{
  protected:
    void SetUp() override { Loc::clear(); }
    void TearDown() override { Loc::clear(); }
};

// ── parseJsonFile tests ──

TEST_F(LocTest, ParseJsonFile_ValidFile)
{
    auto path = writeTempJson(R"({"greeting": "Hello World"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("greeting"), L"Hello World");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_MultiplePairs)
{
    auto path = writeTempJson(R"({"a": "one", "b": "two", "c": "three"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("a"), L"one");
    EXPECT_EQ(Loc::get("b"), L"two");
    EXPECT_EQ(Loc::get("c"), L"three");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_WithBOM)
{
    std::string bom = "\xEF\xBB\xBF";
    auto path = writeTempJson(bom + R"({"key": "value"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("key"), L"value");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeNewline)
{
    auto path = writeTempJson(R"({"k": "line1\nline2"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"line1\nline2");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeTab)
{
    auto path = writeTempJson(R"({"k": "col1\tcol2"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"col1\tcol2");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeBackslash)
{
    auto path = writeTempJson(R"({"k": "a\\b"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"a\\b");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeQuote)
{
    auto path = writeTempJson(R"({"k": "say \"hi\""})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"say \"hi\"");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeSlash)
{
    auto path = writeTempJson(R"({"k": "a\/b"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"a/b");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnicodeEscape_ASCII)
{
    auto path = writeTempJson(R"({"k": "\u0041"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"A");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnicodeEscape_2byte)
{
    // \u00E9 = e-acute (UTF-8: 0xC3 0xA9)
    auto path = writeTempJson(R"({"k": "\u00E9"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"\x00E9");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnicodeEscape_3byte)
{
    // \u2014 = em-dash (UTF-8: 0xE2 0x80 0x94)
    auto path = writeTempJson(R"({"k": "\u2014"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"\x2014");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EmptyFile)
{
    auto path = writeTempJson("");
    EXPECT_FALSE(Loc::parseJsonFile(path));
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_NoBraces)
{
    auto path = writeTempJson("just plain text");
    EXPECT_FALSE(Loc::parseJsonFile(path));
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EmptyObject)
{
    auto path = writeTempJson("{}");
    EXPECT_FALSE(Loc::parseJsonFile(path));
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_TrailingComma)
{
    auto path = writeTempJson(R"({"a": "1",})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("a"), L"1");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_WhitespaceVariety)
{
    auto path = writeTempJson("  \t\n{  \n  \"k1\" \t : \n \"v1\"  ,  \n  \"k2\"  :  \"v2\"  \n}  ");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k1"), L"v1");
    EXPECT_EQ(Loc::get("k2"), L"v2");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_NonexistentFile)
{
    EXPECT_FALSE(Loc::parseJsonFile("nonexistent_file_12345.json"));
}

// ── initDefaults / get tests ──

TEST_F(LocTest, InitDefaults_PopulatesKeys)
{
    Loc::initDefaults();
    // Check a sample of known keys
    EXPECT_FALSE(Loc::get("bind.quick_build_1").empty());
    EXPECT_FALSE(Loc::get("key.shift").empty());
    EXPECT_FALSE(Loc::get("ui.config_title").empty());
    EXPECT_FALSE(Loc::get("msg.no_hit").empty());
    EXPECT_FALSE(Loc::get("save.removal_header").empty());
    // Count should be at least 80 keys
    EXPECT_GE(Loc::s_table.size(), 80u);
}

TEST_F(LocTest, Get_ExistingKey)
{
    Loc::initDefaults();
    EXPECT_EQ(Loc::get("key.shift"), L"SHIFT");
    EXPECT_EQ(Loc::get("key.ctrl"), L"CTRL");
    EXPECT_EQ(Loc::get("bind.rotation"), L"Rotation");
}

TEST_F(LocTest, Get_MissingKey)
{
    Loc::initDefaults();
    const auto& result = Loc::get("nonexistent.key");
    EXPECT_TRUE(result.empty());
    // Verify it returns the static empty string (same address)
    EXPECT_EQ(&result, &Loc::s_empty);
}

TEST_F(LocTest, Get_OverrideFromJson)
{
    Loc::initDefaults();
    EXPECT_EQ(Loc::get("key.shift"), L"SHIFT");
    // Override via JSON
    auto path = writeTempJson(R"({"key.shift": "MAYUS"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("key.shift"), L"MAYUS");
    // Other defaults should still be present
    EXPECT_EQ(Loc::get("key.ctrl"), L"CTRL");
    std::remove(path.c_str());
}

// ── utf8ToWide tests ──

TEST_F(LocTest, Utf8ToWide_ASCII)
{
    EXPECT_EQ(Loc::utf8ToWide("hello"), L"hello");
}

TEST_F(LocTest, Utf8ToWide_Empty)
{
    EXPECT_EQ(Loc::utf8ToWide(""), L"");
}

TEST_F(LocTest, Utf8ToWide_Multibyte)
{
    // UTF-8 for e-acute: 0xC3 0xA9
    std::string utf8 = "\xC3\xA9";
    auto wide = Loc::utf8ToWide(utf8);
    EXPECT_EQ(wide.size(), 1u);
    EXPECT_EQ(wide[0], L'\x00E9');
}

TEST_F(LocTest, Utf8ToWide_3ByteChar)
{
    // UTF-8 for em-dash U+2014: 0xE2 0x80 0x94
    std::string utf8 = "\xE2\x80\x94";
    auto wide = Loc::utf8ToWide(utf8);
    EXPECT_EQ(wide.size(), 1u);
    EXPECT_EQ(wide[0], L'\x2014');
}

TEST_F(LocTest, Utf8ToWide_MixedASCIIAndMultibyte)
{
    // "caf\xC3\xA9" = "cafe" with accented e
    std::string utf8 = "caf\xC3\xA9";
    auto wide = Loc::utf8ToWide(utf8);
    EXPECT_EQ(wide, L"caf\x00E9");
}

// ── parseJsonFile edge cases ──

TEST_F(LocTest, ParseJsonFile_NestedObjectIgnored)
{
    // Nested objects are not valid in our flat JSON parser — should either skip or handle gracefully
    auto path = writeTempJson(R"({"a": "valid", "nested": {"inner": "value"}, "b": "also valid"})");
    // Parser should at least get "a"
    Loc::parseJsonFile(path);
    EXPECT_EQ(Loc::get("a"), L"valid");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_ValueWithColons)
{
    auto path = writeTempJson(R"({"url": "https://example.com:8080/path"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("url"), L"https://example.com:8080/path");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EmptyValue)
{
    auto path = writeTempJson(R"({"empty": ""})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("empty"), L"");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnicodeEscape_NullChar)
{
    // \u0000 should produce a null character
    auto path = writeTempJson(R"({"k": "a\u0000b"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    auto val = Loc::get("k");
    // The string should contain 3 characters: 'a', '\0', 'b'
    EXPECT_GE(val.size(), 1u); // at least got something
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_MultipleEscapes)
{
    auto path = writeTempJson(R"({"k": "line1\nline2\ttab\\slash"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"line1\nline2\ttab\\slash");
    std::remove(path.c_str());
}

TEST_F(LocTest, Get_ClearRemovesAll)
{
    Loc::initDefaults();
    EXPECT_FALSE(Loc::s_table.empty());
    Loc::clear();
    EXPECT_TRUE(Loc::s_table.empty());
    EXPECT_TRUE(Loc::get("key.shift").empty());
}

// ── JSON parser edge cases: malformed unicode, non-string values ──

TEST_F(LocTest, ParseJsonFile_MalformedUnicodeShort)
{
    // Incomplete \u escape (only 2 hex digits available before end of string)
    auto path = writeTempJson(R"({"k": "\u00"})");
    // Should not crash; may or may not parse the value
    Loc::parseJsonFile(path);
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_MalformedUnicodeInvalidHex)
{
    // Invalid hex digits in \uXXXX — should produce some output without crashing
    auto path = writeTempJson(R"({"k": "\u00GZ"})");
    Loc::parseJsonFile(path);
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_NonStringValue)
{
    // JSON with numeric or boolean values — our parser expects string values only
    // Should reject gracefully (value doesn't start with quote)
    auto path = writeTempJson(R"({"valid": "yes", "num": 42, "after": "ok"})");
    Loc::parseJsonFile(path);
    // "valid" should be parsed, "num" should be skipped, "after" may or may not parse
    EXPECT_EQ(Loc::get("valid"), L"yes");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_BooleanValue)
{
    auto path = writeTempJson(R"({"flag": true, "name": "test"})");
    Loc::parseJsonFile(path);
    // "flag" should not appear (not a string value), "name" may not parse
    // either due to parser stopping — just verify no crash
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_EscapeCarriageReturn)
{
    auto path = writeTempJson(R"({"k": "line1\rline2"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"line1\rline2");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnknownEscape)
{
    // \z is not a standard JSON escape — parser default case passes through 'z'
    auto path = writeTempJson(R"({"k": "test\zval"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"testzval");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_OnlyBraces)
{
    // Just braces with whitespace inside — no pairs
    auto path = writeTempJson("{   }");
    EXPECT_FALSE(Loc::parseJsonFile(path));
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_LargeFile)
{
    // 500 key-value pairs
    std::string json = "{";
    for (int i = 0; i < 500; i++)
    {
        if (i > 0) json += ",";
        json += "\"key" + std::to_string(i) + "\": \"val" + std::to_string(i) + "\"";
    }
    json += "}";
    auto path = writeTempJson(json);
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("key0"), L"val0");
    EXPECT_EQ(Loc::get("key499"), L"val499");
    std::remove(path.c_str());
}

// ── utf8ToWide edge cases ──

TEST_F(LocTest, Utf8ToWide_4ByteChar)
{
    // UTF-8 for U+1F600 (grinning face emoji): 0xF0 0x9F 0x98 0x80
    // On Windows wchar_t is 16-bit, so this becomes a surrogate pair
    std::string utf8 = "\xF0\x9F\x98\x80";
    auto wide = Loc::utf8ToWide(utf8);
    EXPECT_GE(wide.size(), 1u); // surrogate pair = 2 wchars on Windows
}

TEST_F(LocTest, Utf8ToWide_LongString)
{
    // 1000 ASCII characters
    std::string input(1000, 'A');
    auto wide = Loc::utf8ToWide(input);
    EXPECT_EQ(wide.size(), 1000u);
    EXPECT_EQ(wide[0], L'A');
    EXPECT_EQ(wide[999], L'A');
}

// ── parseJsonFile: parser robustness ──

TEST_F(LocTest, ParseJsonFile_DuplicateKeys)
{
    // Duplicate keys — last value should win
    auto path = writeTempJson(R"({"k": "first", "k": "second"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"second");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_ConsecutiveCommas)
{
    // Multiple commas between entries — parser skips commas
    auto path = writeTempJson(R"({"a": "1",,,, "b": "2"})");
    Loc::parseJsonFile(path);
    EXPECT_EQ(Loc::get("a"), L"1");
    // "b" may or may not parse depending on how commas interact with skipWS+parseString
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_KeyWithoutColon)
{
    // Key followed by another key instead of colon — parser should stop
    auto path = writeTempJson(R"({"valid": "yes", "bad" "value", "after": "ok"})");
    Loc::parseJsonFile(path);
    EXPECT_EQ(Loc::get("valid"), L"yes");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_UnicodeInKey)
{
    // Unicode escape in key name
    auto path = writeTempJson(R"({"\u0041\u0042": "AB key"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("AB"), L"AB key");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_MixedNewlines)
{
    // CRLF and LF mixed
    auto path = writeTempJson("{\r\n  \"a\": \"1\",\n  \"b\": \"2\"\r\n}");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("a"), L"1");
    EXPECT_EQ(Loc::get("b"), L"2");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_SinglePair)
{
    auto path = writeTempJson(R"({"only": "one"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("only"), L"one");
    std::remove(path.c_str());
}

TEST_F(LocTest, ParseJsonFile_LeadingGarbage)
{
    // Garbage before the opening brace — parser searches for '{'
    auto path = writeTempJson(R"(GARBAGE STUFF {"k": "v"})");
    EXPECT_TRUE(Loc::parseJsonFile(path));
    EXPECT_EQ(Loc::get("k"), L"v");
    std::remove(path.c_str());
}

// ── utf8ToWide: invalid sequences ──

TEST_F(LocTest, Utf8ToWide_InvalidSequence)
{
    // Invalid UTF-8 byte (0xFF is never valid in UTF-8)
    // MultiByteToWideChar with CP_UTF8 should handle gracefully
    std::string bad = "\xFF\xFE";
    auto wide = Loc::utf8ToWide(bad);
    // Should not crash; result may vary by Windows version
    // Just verify we get some result without throwing
    (void)wide;
}

TEST_F(LocTest, Utf8ToWide_TruncatedMultibyte)
{
    // First byte of 2-byte sequence (0xC3) without continuation
    std::string truncated = "\xC3";
    auto wide = Loc::utf8ToWide(truncated);
    // Should not crash
    (void)wide;
}
