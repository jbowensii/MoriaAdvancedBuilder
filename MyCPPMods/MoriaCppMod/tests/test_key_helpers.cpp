// Unit tests for keyName, modifierName, nextModifier

#include <gtest/gtest.h>
#include "moria_testable.h"

using namespace MoriaMods;

class KeyHelperTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Loc::clear();
        Loc::initDefaults();
    }
    void TearDown() override { Loc::clear(); }
};

// ── keyName tests ──

TEST_F(KeyHelperTest, FKeys)
{
    EXPECT_EQ(keyName(0x70), L"F1");
    EXPECT_EQ(keyName(0x71), L"F2");
    EXPECT_EQ(keyName(0x72), L"F3");
    EXPECT_EQ(keyName(0x73), L"F4");
    EXPECT_EQ(keyName(0x74), L"F5");
    EXPECT_EQ(keyName(0x75), L"F6");
    EXPECT_EQ(keyName(0x76), L"F7");
    EXPECT_EQ(keyName(0x77), L"F8");
    EXPECT_EQ(keyName(0x78), L"F9");
    EXPECT_EQ(keyName(0x79), L"F10");
    EXPECT_EQ(keyName(0x7A), L"F11");
    EXPECT_EQ(keyName(0x7B), L"F12");
}

TEST_F(KeyHelperTest, NumpadDigits)
{
    EXPECT_EQ(keyName(0x60), L"Num0");
    EXPECT_EQ(keyName(0x61), L"Num1");
    EXPECT_EQ(keyName(0x62), L"Num2");
    EXPECT_EQ(keyName(0x63), L"Num3");
    EXPECT_EQ(keyName(0x64), L"Num4");
    EXPECT_EQ(keyName(0x65), L"Num5");
    EXPECT_EQ(keyName(0x66), L"Num6");
    EXPECT_EQ(keyName(0x67), L"Num7");
    EXPECT_EQ(keyName(0x68), L"Num8");
    EXPECT_EQ(keyName(0x69), L"Num9");
}

TEST_F(KeyHelperTest, NumpadOps)
{
    EXPECT_EQ(keyName(0x6A), L"Num*");
    EXPECT_EQ(keyName(0x6B), L"Num+");
    EXPECT_EQ(keyName(0x6C), L"NumSep");
    EXPECT_EQ(keyName(0x6D), L"Num-");
    EXPECT_EQ(keyName(0x6E), L"Num.");
    EXPECT_EQ(keyName(0x6F), L"Num/");
}

TEST_F(KeyHelperTest, Letters)
{
    EXPECT_EQ(keyName(0x41), L"A");
    EXPECT_EQ(keyName(0x42), L"B");
    EXPECT_EQ(keyName(0x4D), L"M");
    EXPECT_EQ(keyName(0x5A), L"Z");
}

TEST_F(KeyHelperTest, Digits)
{
    EXPECT_EQ(keyName(0x30), L"0");
    EXPECT_EQ(keyName(0x31), L"1");
    EXPECT_EQ(keyName(0x39), L"9");
}

TEST_F(KeyHelperTest, Symbols)
{
    EXPECT_EQ(keyName(0xDC), L"\\");
    EXPECT_EQ(keyName(0xC0), L"`");
    EXPECT_EQ(keyName(0xBA), L";");
    EXPECT_EQ(keyName(0xBB), L"=");
    EXPECT_EQ(keyName(0xBC), L",");
    EXPECT_EQ(keyName(0xBD), L"-");
    EXPECT_EQ(keyName(0xBE), L".");
    EXPECT_EQ(keyName(0xBF), L"/");
    EXPECT_EQ(keyName(0xDB), L"[");
    EXPECT_EQ(keyName(0xDD), L"]");
    EXPECT_EQ(keyName(0xDE), L"'");
}

TEST_F(KeyHelperTest, SpecialKeys)
{
    EXPECT_EQ(keyName(0x20), L"Space");
    EXPECT_EQ(keyName(0x09), L"Tab");
    EXPECT_EQ(keyName(0x0D), L"Enter");
    EXPECT_EQ(keyName(0x2D), L"Ins");
    EXPECT_EQ(keyName(0x2E), L"Del");
    EXPECT_EQ(keyName(0x24), L"Home");
    EXPECT_EQ(keyName(0x23), L"End");
    EXPECT_EQ(keyName(0x21), L"PgUp");
    EXPECT_EQ(keyName(0x22), L"PgDn");
}

TEST_F(KeyHelperTest, Unknown)
{
    EXPECT_EQ(keyName(0xFF), L"0xFF");
    EXPECT_EQ(keyName(0x01), L"0x01"); // VK_LBUTTON
}

// ── modifierName tests ──

TEST_F(KeyHelperTest, ModifierName_AllValues)
{
    EXPECT_STREQ(modifierName(VK_SHIFT), L"SHIFT");
    EXPECT_STREQ(modifierName(VK_CONTROL), L"CTRL");
    EXPECT_STREQ(modifierName(VK_MENU), L"ALT");
    EXPECT_STREQ(modifierName(VK_RMENU), L"RALT");
}

TEST_F(KeyHelperTest, ModifierName_Default)
{
    // Unknown modifier defaults to SHIFT
    EXPECT_STREQ(modifierName(0x00), L"SHIFT");
    EXPECT_STREQ(modifierName(0xFF), L"SHIFT");
}

// ── nextModifier tests ──

TEST_F(KeyHelperTest, NextModifier_FullCycle)
{
    EXPECT_EQ(nextModifier(VK_SHIFT), VK_CONTROL);
    EXPECT_EQ(nextModifier(VK_CONTROL), VK_MENU);
    EXPECT_EQ(nextModifier(VK_MENU), VK_RMENU);
    EXPECT_EQ(nextModifier(VK_RMENU), VK_SHIFT);
}

TEST_F(KeyHelperTest, NextModifier_Default)
{
    // Unknown modifier returns SHIFT
    EXPECT_EQ(nextModifier(0x00), VK_SHIFT);
    EXPECT_EQ(nextModifier(0xFF), VK_SHIFT);
}

// ── modifierToIniName tests ──

TEST_F(KeyHelperTest, ModifierToIniName_AllValues)
{
    EXPECT_EQ(modifierToIniName(VK_SHIFT), "SHIFT");
    EXPECT_EQ(modifierToIniName(VK_CONTROL), "CTRL");
    EXPECT_EQ(modifierToIniName(VK_MENU), "ALT");
    EXPECT_EQ(modifierToIniName(VK_RMENU), "RALT");
}

TEST_F(KeyHelperTest, ModifierToIniName_UnknownDefaultsToShift)
{
    EXPECT_EQ(modifierToIniName(0x00), "SHIFT");
    EXPECT_EQ(modifierToIniName(0xFF), "SHIFT");
    EXPECT_EQ(modifierToIniName(0x41), "SHIFT"); // 'A' key
}

// ── modifierNameToVK tests ──

TEST_F(KeyHelperTest, ModifierNameToVK_AllValues)
{
    EXPECT_EQ(modifierNameToVK(L"SHIFT"), VK_SHIFT);
    EXPECT_EQ(modifierNameToVK(L"CTRL"), VK_CONTROL);
    EXPECT_EQ(modifierNameToVK(L"ALT"), VK_MENU);
    EXPECT_EQ(modifierNameToVK(L"RALT"), VK_RMENU);
}

TEST_F(KeyHelperTest, ModifierNameToVK_CaseInsensitive)
{
    EXPECT_EQ(modifierNameToVK(L"shift"), VK_SHIFT);
    EXPECT_EQ(modifierNameToVK(L"Ctrl"), VK_CONTROL);
    EXPECT_EQ(modifierNameToVK(L"alt"), VK_MENU);
    EXPECT_EQ(modifierNameToVK(L"rAlt"), VK_RMENU);
}

TEST_F(KeyHelperTest, ModifierNameToVK_Unknown)
{
    EXPECT_EQ(modifierNameToVK(L""), std::nullopt);
    EXPECT_EQ(modifierNameToVK(L"SPACE"), std::nullopt);
    EXPECT_EQ(modifierNameToVK(L"LALT"), std::nullopt);
}

// ── iniKeyToBindIndex tests ──

TEST_F(KeyHelperTest, IniKeyToBindIndex_AllKeys)
{
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild1"), 0);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild2"), 1);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild3"), 2);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild4"), 3);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild5"), 4);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild6"), 5);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild7"), 6);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild8"), 7);
    EXPECT_EQ(iniKeyToBindIndex("Rotation"), 8);
    EXPECT_EQ(iniKeyToBindIndex("SnapToggle"), 9);
    EXPECT_EQ(iniKeyToBindIndex("StabilityCheck"), 10);
    EXPECT_EQ(iniKeyToBindIndex("SuperDwarf"), 11);
    EXPECT_EQ(iniKeyToBindIndex("Target"), 12);
    EXPECT_EQ(iniKeyToBindIndex("Configuration"), 13);
    EXPECT_EQ(iniKeyToBindIndex("RemoveTarget"), 14);
    EXPECT_EQ(iniKeyToBindIndex("UndoLast"), 15);
    EXPECT_EQ(iniKeyToBindIndex("RemoveAll"), 16);
    EXPECT_EQ(iniKeyToBindIndex("AdvancedBuilderOpen"), 17);
    EXPECT_EQ(iniKeyToBindIndex("Reserved1"), 18);
    EXPECT_EQ(iniKeyToBindIndex("TrashItem"), 19);
    EXPECT_EQ(iniKeyToBindIndex("ReplenishItem"), 20);
    EXPECT_EQ(iniKeyToBindIndex("RemoveAttributes"), 21);
}

TEST_F(KeyHelperTest, IniKeyToBindIndex_CaseInsensitive)
{
    EXPECT_EQ(iniKeyToBindIndex("quickbuild1"), 0);
    EXPECT_EQ(iniKeyToBindIndex("ROTATION"), 8);
    EXPECT_EQ(iniKeyToBindIndex("trashitem"), 19);
}

TEST_F(KeyHelperTest, IniKeyToBindIndex_NotFound)
{
    EXPECT_EQ(iniKeyToBindIndex(""), -1);
    EXPECT_EQ(iniKeyToBindIndex("UnknownKey"), -1);
    EXPECT_EQ(iniKeyToBindIndex("QuickBuild9"), -1);
}

// ── bindIndexToIniKey tests ──

TEST_F(KeyHelperTest, BindIndexToIniKey_ValidRange)
{
    EXPECT_STREQ(bindIndexToIniKey(0), "QuickBuild1");
    EXPECT_STREQ(bindIndexToIniKey(7), "QuickBuild8");
    EXPECT_STREQ(bindIndexToIniKey(8), "Rotation");
    EXPECT_STREQ(bindIndexToIniKey(10), "StabilityCheck");
    EXPECT_STREQ(bindIndexToIniKey(12), "Target");
    EXPECT_STREQ(bindIndexToIniKey(16), "RemoveAll");
    EXPECT_STREQ(bindIndexToIniKey(19), "TrashItem");
    EXPECT_STREQ(bindIndexToIniKey(20), "ReplenishItem");
    EXPECT_STREQ(bindIndexToIniKey(21), "RemoveAttributes");
}

TEST_F(KeyHelperTest, BindIndexToIniKey_OutOfRange)
{
    EXPECT_EQ(bindIndexToIniKey(-1), nullptr);
    EXPECT_EQ(bindIndexToIniKey(BIND_COUNT), nullptr);
    EXPECT_EQ(bindIndexToIniKey(100), nullptr);
}

// ── keyName edge cases ──

TEST_F(KeyHelperTest, KeyName_ZeroVK)
{
    // VK 0x00 should fall through to hex format
    EXPECT_EQ(keyName(0x00), L"0x00");
}
