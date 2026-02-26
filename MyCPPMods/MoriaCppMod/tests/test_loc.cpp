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
