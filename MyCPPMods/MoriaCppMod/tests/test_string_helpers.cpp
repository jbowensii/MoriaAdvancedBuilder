// Unit tests for string helper functions:
// extractFriendlyName, componentNameToMeshId, wrapText, trimStr, strEqualCI, wstrEqualCI

#include <gtest/gtest.h>
#include "moria_testable.h"

using namespace MoriaMods;

// ── extractFriendlyName tests ──

TEST(ExtractFriendlyName, WithDash)
{
    EXPECT_EQ(extractFriendlyName("PWM_Quarry_2x2-Large_Section"), L"PWM_Quarry_2x2");
}

TEST(ExtractFriendlyName, NoDash)
{
    EXPECT_EQ(extractFriendlyName("SimpleMeshName"), L"SimpleMeshName");
}

TEST(ExtractFriendlyName, MultipleDashes)
{
    // Should split at FIRST dash
    EXPECT_EQ(extractFriendlyName("Part_A-Part_B-Part_C"), L"Part_A");
}

TEST(ExtractFriendlyName, Empty)
{
    EXPECT_EQ(extractFriendlyName(""), L"");
}

TEST(ExtractFriendlyName, DashAtStart)
{
    // Dash at position 0 means empty prefix
    EXPECT_EQ(extractFriendlyName("-SomeStuff"), L"");
}

TEST(ExtractFriendlyName, DashAtEnd)
{
    EXPECT_EQ(extractFriendlyName("Name-"), L"Name");
}

TEST(ExtractFriendlyName, RealWorldMeshName)
{
    EXPECT_EQ(extractFriendlyName("PWM_Quarry_2x2x2_A-Wall_Stone_Half_2x1x1_A_C_2147478223"), L"PWM_Quarry_2x2x2_A");
}

// ── componentNameToMeshId tests ──

TEST(ComponentNameToMeshId, NumericSuffix)
{
    EXPECT_EQ(componentNameToMeshId(L"PWM_Quarry_2x2_2147476295"), "PWM_Quarry_2x2");
}

TEST(ComponentNameToMeshId, NoNumericSuffix)
{
    // "Large" is not all digits, so the full string is preserved
    EXPECT_EQ(componentNameToMeshId(L"PWM_Quarry_2x2_Large"), "PWM_Quarry_2x2_Large");
}

TEST(ComponentNameToMeshId, MixedSuffix)
{
    // "A2B3" has letters, not all digits
    EXPECT_EQ(componentNameToMeshId(L"Name_A2B3"), "Name_A2B3");
}

TEST(ComponentNameToMeshId, TrailingUnderscore)
{
    // Empty string after underscore = "all digits" (vacuously true)
    EXPECT_EQ(componentNameToMeshId(L"Name_"), "Name");
}

TEST(ComponentNameToMeshId, NoUnderscore)
{
    EXPECT_EQ(componentNameToMeshId(L"SingleWord"), "SingleWord");
}

TEST(ComponentNameToMeshId, Empty)
{
    EXPECT_EQ(componentNameToMeshId(L""), "");
}

TEST(ComponentNameToMeshId, MultipleUnderscores_LastNumeric)
{
    EXPECT_EQ(componentNameToMeshId(L"A_B_C_12345"), "A_B_C");
}

TEST(ComponentNameToMeshId, SingleDigitSuffix)
{
    EXPECT_EQ(componentNameToMeshId(L"Mesh_0"), "Mesh");
}

TEST(ComponentNameToMeshId, RealWorldComponentName)
{
    EXPECT_EQ(componentNameToMeshId(L"PWM_Quarry_2x2x2_A-Wall_Stone_Half_2x1x1_A_C_2147478223"),
              "PWM_Quarry_2x2x2_A-Wall_Stone_Half_2x1x1_A_C");
}

// ── wrapText tests ──

TEST(WrapText, ShortTextNoWrap)
{
    // prefix + value fits in maxLine → returned as-is
    EXPECT_EQ(wrapText(L"Name: ", L"ShortVal"), L"Name: ShortVal");
}

TEST(WrapText, ExactlyMaxLine)
{
    // Exactly 70 chars → no wrap
    std::wstring prefix = L"P: ";
    std::wstring value(67, L'x'); // 3 + 67 = 70
    EXPECT_EQ(wrapText(prefix, value), prefix + value);
}

TEST(WrapText, WrapsAtSpace)
{
    // Force wrap by exceeding maxLine=70, break should occur at a space
    std::wstring prefix = L"Path: ";
    // 6 (prefix) + 65 (word) + 1 (space) + 10 (word) = 82 chars
    std::wstring value = std::wstring(65, L'A') + L" " + std::wstring(10, L'B');
    std::wstring result = wrapText(prefix, value);
    // Should break after the space at position 71 (within search window)
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // No newline in first 70 chars
    EXPECT_EQ(result.substr(0, 70).find(L'\n'), std::wstring::npos);
}

TEST(WrapText, WrapsAtUnderscore)
{
    std::wstring prefix = L"Class: ";
    // Build a string with underscores as break points
    // "Class: AAAA...(50)_BBBBB...(30)" = 7+50+1+30 = 88
    std::wstring value = std::wstring(50, L'A') + L"_" + std::wstring(30, L'B');
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
}

TEST(WrapText, WrapsAtSlash)
{
    std::wstring prefix = L"Path: ";
    // "Path: /Game/SomeLong...Path/MoreStuff" — slash as break point
    std::wstring value = L"/Game/" + std::wstring(55, L'X') + L"/" + std::wstring(20, L'Y');
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
}

TEST(WrapText, ForcedBreakNoWordBoundary)
{
    // A single 100-char unbreakable token → must still break (hard break at maxLine)
    std::wstring prefix = L"";
    std::wstring value(100, L'Z');
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // First line should be exactly 70 chars
    auto nl = result.find(L'\n');
    EXPECT_EQ(nl, 70u);
}

TEST(WrapText, MultipleWraps)
{
    // 210 chars with no break points → should produce 3 lines of 70
    std::wstring prefix = L"";
    std::wstring value(210, L'Q');
    std::wstring result = wrapText(prefix, value);
    // Count newlines: should be exactly 2
    int nlCount = 0;
    for (wchar_t c : result) if (c == L'\n') nlCount++;
    EXPECT_EQ(nlCount, 2);
}

TEST(WrapText, EmptyValue)
{
    EXPECT_EQ(wrapText(L"Prefix: ", L""), L"Prefix: ");
}

TEST(WrapText, EmptyPrefixAndValue)
{
    EXPECT_EQ(wrapText(L"", L""), L"");
}

TEST(WrapText, CustomMaxLine)
{
    // Use smaller maxLine
    std::wstring result = wrapText(L"", L"ABCDEFGHIJ", 5);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // First chunk is 5 chars
    EXPECT_EQ(result.substr(0, 5), L"ABCDE");
}

TEST(WrapText, PreservesAllContent)
{
    // After removing newlines, content should be identical to prefix+value
    std::wstring prefix = L"Recipe: ";
    std::wstring value = L"DT_Master_Recipes_Building.PWM_Quarry_2x2x2_A-Wall_Stone_Half_2x1x1_A_Row";
    std::wstring result = wrapText(prefix, value);
    std::wstring stripped;
    for (wchar_t c : result)
        if (c != L'\n') stripped += c;
    EXPECT_EQ(stripped, prefix + value);
}

TEST(WrapText, RealWorldPath)
{
    // Typical long UE path that should wrap
    std::wstring prefix = L"Path: ";
    std::wstring value = L"/Game/Blueprints/BuildableObjects/Quarry/PWM_Quarry_2x2x2_A-Wall_Stone_Half_2x1x1_A_C";
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // Verify content preserved
    std::wstring stripped;
    for (wchar_t c : result)
        if (c != L'\n') stripped += c;
    EXPECT_EQ(stripped, prefix + value);
}

TEST(WrapText, PrefixLongerThanMaxLine)
{
    // Prefix alone exceeds maxLine — should still work, just produces long first line
    std::wstring prefix(80, L'P');
    std::wstring value = L"short";
    std::wstring result = wrapText(prefix, value, 70);
    // Content preserved
    std::wstring stripped;
    for (wchar_t c : result) if (c != L'\n') stripped += c;
    EXPECT_EQ(stripped, prefix + value);
}

TEST(WrapText, MaxLineOne)
{
    // Extreme: maxLine=1 forces break after every character
    std::wstring result = wrapText(L"", L"ABC", 1);
    // Each character on its own line
    EXPECT_EQ(result, L"A\nB\nC");
}

// ── trimStr tests ──

TEST(TrimStr, Empty)
{
    EXPECT_EQ(trimStr(""), "");
}

TEST(TrimStr, WhitespaceOnly)
{
    EXPECT_EQ(trimStr("   "), "");
    EXPECT_EQ(trimStr("\t\t"), "");
    EXPECT_EQ(trimStr("\r\n"), "");
}

TEST(TrimStr, NoWhitespace)
{
    EXPECT_EQ(trimStr("hello"), "hello");
}

TEST(TrimStr, LeadingWhitespace)
{
    EXPECT_EQ(trimStr("  hello"), "hello");
}

TEST(TrimStr, TrailingWhitespace)
{
    EXPECT_EQ(trimStr("hello  "), "hello");
}

TEST(TrimStr, BothEnds)
{
    EXPECT_EQ(trimStr("  hello  "), "hello");
}

TEST(TrimStr, InternalWhitespacePreserved)
{
    EXPECT_EQ(trimStr("  hello world  "), "hello world");
}

TEST(TrimStr, MixedWhitespace)
{
    EXPECT_EQ(trimStr("\t\r\n hello \t\r\n"), "hello");
}

TEST(TrimStr, SingleChar)
{
    EXPECT_EQ(trimStr(" x "), "x");
}

// ── strEqualCI tests ──

TEST(StrEqualCI, Equal)
{
    EXPECT_TRUE(strEqualCI("hello", "hello"));
}

TEST(StrEqualCI, DifferentCase)
{
    EXPECT_TRUE(strEqualCI("Hello", "hELLO"));
    EXPECT_TRUE(strEqualCI("SHIFT", "shift"));
    EXPECT_TRUE(strEqualCI("QuickBuild1", "quickbuild1"));
}

TEST(StrEqualCI, NotEqual)
{
    EXPECT_FALSE(strEqualCI("hello", "world"));
}

TEST(StrEqualCI, DifferentLength)
{
    EXPECT_FALSE(strEqualCI("hello", "hell"));
    EXPECT_FALSE(strEqualCI("hi", "his"));
}

TEST(StrEqualCI, BothEmpty)
{
    EXPECT_TRUE(strEqualCI("", ""));
}

TEST(StrEqualCI, OneEmpty)
{
    EXPECT_FALSE(strEqualCI("a", ""));
    EXPECT_FALSE(strEqualCI("", "b"));
}

TEST(StrEqualCI, Digits)
{
    // Digits are case-insensitive by default (no change)
    EXPECT_TRUE(strEqualCI("abc123", "ABC123"));
}

// ── wstrEqualCI tests ──

TEST(WstrEqualCI, Equal)
{
    EXPECT_TRUE(wstrEqualCI(L"hello", L"hello"));
}

TEST(WstrEqualCI, DifferentCase)
{
    EXPECT_TRUE(wstrEqualCI(L"Hello", L"hELLO"));
    EXPECT_TRUE(wstrEqualCI(L"SPACE", L"space"));
    EXPECT_TRUE(wstrEqualCI(L"Num+", L"NUM+"));
}

TEST(WstrEqualCI, NotEqual)
{
    EXPECT_FALSE(wstrEqualCI(L"hello", L"world"));
}

TEST(WstrEqualCI, DifferentLength)
{
    EXPECT_FALSE(wstrEqualCI(L"hello", L"hell"));
}

TEST(WstrEqualCI, BothEmpty)
{
    EXPECT_TRUE(wstrEqualCI(L"", L""));
}

TEST(WstrEqualCI, OneEmpty)
{
    EXPECT_FALSE(wstrEqualCI(L"a", L""));
    EXPECT_FALSE(wstrEqualCI(L"", L"b"));
}

TEST(WstrEqualCI, Digits)
{
    EXPECT_TRUE(wstrEqualCI(L"abc123", L"ABC123"));
    EXPECT_TRUE(wstrEqualCI(L"Num+", L"num+"));
}

// ── wrapText additional edge cases ──

TEST(WrapText, WrapsAtDash)
{
    std::wstring prefix = L"Name: ";
    // Build string that breaks at a dash
    std::wstring value = std::wstring(55, L'A') + L"-" + std::wstring(20, L'B');
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // Content preserved
    std::wstring stripped;
    for (wchar_t c : result) if (c != L'\n') stripped += c;
    EXPECT_EQ(stripped, prefix + value);
}

TEST(WrapText, WrapsAtBackslash)
{
    std::wstring prefix = L"Path: ";
    // Windows-style path with backslashes as break points
    std::wstring value = L"C:\\" + std::wstring(55, L'X') + L"\\" + std::wstring(20, L'Y');
    std::wstring result = wrapText(prefix, value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
}

TEST(WrapText, SingleCharValue)
{
    EXPECT_EQ(wrapText(L"", L"X"), L"X");
    EXPECT_EQ(wrapText(L"P: ", L"X"), L"P: X");
}

TEST(WrapText, ExactlyOneOverMaxLine)
{
    // 71 chars = exactly 1 over default maxLine of 70
    std::wstring value(71, L'Q');
    std::wstring result = wrapText(L"", value);
    EXPECT_NE(result.find(L'\n'), std::wstring::npos);
    // First line should be 70 chars (hard break)
    auto nl = result.find(L'\n');
    EXPECT_EQ(nl, 70u);
}

// ── extractFriendlyName additional edge cases ──

TEST(ExtractFriendlyName, OnlyDash)
{
    EXPECT_EQ(extractFriendlyName("-"), L"");
}

TEST(ExtractFriendlyName, ConsecutiveDashes)
{
    EXPECT_EQ(extractFriendlyName("A--B"), L"A");
}

// ── componentNameToMeshId additional edge cases ──

TEST(ComponentNameToMeshId, OnlyUnderscore)
{
    // Single underscore — last underscore at pos 0, empty suffix = vacuously all digits
    // But lastUnderscore > 0 check fails (pos == 0), so full string returned
    EXPECT_EQ(componentNameToMeshId(L"_"), "_");
}

TEST(ComponentNameToMeshId, OnlyDigits)
{
    // "12345" — no underscore, so returned as-is
    EXPECT_EQ(componentNameToMeshId(L"12345"), "12345");
}

TEST(ComponentNameToMeshId, ConsecutiveUnderscores)
{
    // "A__123" — last underscore is at pos 2, suffix "123" is all digits → strip
    EXPECT_EQ(componentNameToMeshId(L"A__123"), "A_");
}

TEST(ComponentNameToMeshId, UnderscoreAndLettersAtEnd)
{
    // "Mesh_ABC_DEF" — suffix "DEF" has letters → not stripped
    EXPECT_EQ(componentNameToMeshId(L"Mesh_ABC_DEF"), "Mesh_ABC_DEF");
}

// ── trimStr edge cases ──

TEST(TrimStr, OnlyNewlines)
{
    EXPECT_EQ(trimStr("\n\n\n"), "");
}

TEST(TrimStr, InternalNewlines)
{
    EXPECT_EQ(trimStr("  hello\nworld  "), "hello\nworld");
}

// ── strEqualCI edge cases ──

TEST(StrEqualCI, SingleChar)
{
    EXPECT_TRUE(strEqualCI("A", "a"));
    EXPECT_TRUE(strEqualCI("z", "Z"));
    EXPECT_FALSE(strEqualCI("a", "b"));
}

TEST(StrEqualCI, SpecialChars)
{
    // Non-alpha characters are unchanged by tolower
    EXPECT_TRUE(strEqualCI("key=val", "KEY=VAL"));
    EXPECT_TRUE(strEqualCI("foo.bar", "FOO.BAR"));
}

// ── wstrEqualCI edge cases ──

TEST(WstrEqualCI, SpecialChars)
{
    EXPECT_TRUE(wstrEqualCI(L"Num+", L"num+"));
    EXPECT_TRUE(wstrEqualCI(L"Key=Val", L"key=val"));
}

// ── extractFriendlyName edge cases ──

TEST(ExtractFriendlyName, LongNameNoDash)
{
    std::string longName(500, 'A');
    auto result = extractFriendlyName(longName);
    EXPECT_EQ(result.size(), 500u);
}

TEST(ExtractFriendlyName, DashFollowedByDash)
{
    EXPECT_EQ(extractFriendlyName("A--"), L"A");
}
