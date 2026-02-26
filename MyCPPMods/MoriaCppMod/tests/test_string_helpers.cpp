// Unit tests for extractFriendlyName and componentNameToMeshId

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
