// Unit tests for file format parse helpers (removal, slot, keybind)

#include <gtest/gtest.h>
#include "moria_testable.h"

using namespace MoriaMods;

// ════════════════════════════════════════════════════════════════════════════
// parseRemovalLine tests
// ════════════════════════════════════════════════════════════════════════════

TEST(ParseRemovalLine, PositionEntry)
{
    auto result = parseRemovalLine("PWM_Quarry_2x2|1.5|2.5|3.5");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->meshName, "PWM_Quarry_2x2");
    EXPECT_FLOAT_EQ(pos->posX, 1.5f);
    EXPECT_FLOAT_EQ(pos->posY, 2.5f);
    EXPECT_FLOAT_EQ(pos->posZ, 3.5f);
}

TEST(ParseRemovalLine, TypeRule)
{
    auto result = parseRemovalLine("@PWM_Quarry_2x2");
    auto* tr = std::get_if<ParsedRemovalTypeRule>(&result);
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->meshName, "PWM_Quarry_2x2");
}

TEST(ParseRemovalLine, Comment)
{
    auto result = parseRemovalLine("# This is a comment");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, EmptyLine)
{
    auto result = parseRemovalLine("");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, MissingFields)
{
    auto result = parseRemovalLine("mesh|1.0");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, InvalidFloat)
{
    auto result = parseRemovalLine("mesh|abc|2.0|3.0");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, NegativeCoords)
{
    auto result = parseRemovalLine("mesh|-100.5|0|999.9");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, -100.5f);
    EXPECT_FLOAT_EQ(pos->posY, 0.0f);
    EXPECT_FLOAT_EQ(pos->posZ, 999.9f);
}

TEST(ParseRemovalLine, TypeRuleEmptyName)
{
    auto result = parseRemovalLine("@");
    auto* tr = std::get_if<ParsedRemovalTypeRule>(&result);
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->meshName, "");
}

TEST(ParseRemovalLine, PipeOnlyLine)
{
    // "|||" — empty mesh name, then empty fields that fail float parse
    auto result = parseRemovalLine("|||");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, LargeCoordinates)
{
    auto result = parseRemovalLine("mesh|999999.5|-888888.25|777777.75");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, 999999.5f);
    EXPECT_FLOAT_EQ(pos->posY, -888888.25f);
    EXPECT_FLOAT_EQ(pos->posZ, 777777.75f);
}

TEST(ParseRemovalLine, WhitespaceOnly)
{
    auto result = parseRemovalLine("   ");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, ExtraFields)
{
    // Extra fields after the 4 expected — should still parse the first 4
    auto result = parseRemovalLine("mesh|1.0|2.0|3.0|extra");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, 1.0f);
}

TEST(ParseRemovalLine, ZeroCoordinates)
{
    auto result = parseRemovalLine("mesh|0|0|0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, 0.0f);
    EXPECT_FLOAT_EQ(pos->posY, 0.0f);
    EXPECT_FLOAT_EQ(pos->posZ, 0.0f);
}

TEST(ParseRemovalLine, ScientificNotation)
{
    // std::stof supports scientific notation
    auto result = parseRemovalLine("mesh|1.5e3|-2.0e1|3.14e0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, 1500.0f);
    EXPECT_FLOAT_EQ(pos->posY, -20.0f);
    EXPECT_FLOAT_EQ(pos->posZ, 3.14f);
}

TEST(ParseRemovalLine, TypeRuleLongName)
{
    // Very long mesh name in type rule
    std::string longName(500, 'X');
    auto result = parseRemovalLine("@" + longName);
    auto* tr = std::get_if<ParsedRemovalTypeRule>(&result);
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->meshName.size(), 500u);
}

TEST(ParseRemovalLine, MeshNameWithSpecialChars)
{
    // Mesh names can contain underscores, numbers, dots
    auto result = parseRemovalLine("SM_Wall.Stone_2x1x1_A|1.0|2.0|3.0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->meshName, "SM_Wall.Stone_2x1x1_A");
}

TEST(ParseRemovalLine, MeshNameWithSpaces)
{
    // Mesh names with spaces (rare but possible)
    auto result = parseRemovalLine("SM Wall Stone|1.0|2.0|3.0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->meshName, "SM Wall Stone");
}

TEST(ParseRemovalLine, FieldsWithWhitespace)
{
    // stof handles leading whitespace but our parser doesn't trim
    // fields come from pipe-splitting so "1.0 " has trailing space
    auto result = parseRemovalLine("mesh| 1.0| 2.0| 3.0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->posX, 1.0f);
}

TEST(ParseRemovalLine, EmptyMeshName)
{
    // Empty mesh name but valid coordinates
    auto result = parseRemovalLine("|1.0|2.0|3.0");
    auto* pos = std::get_if<ParsedRemovalPosition>(&result);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->meshName, "");
}

TEST(ParseRemovalLine, OnlyCommentHash)
{
    auto result = parseRemovalLine("#");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseRemovalLine, TypeRuleWithPipe)
{
    // "@mesh|extra" — type rule only takes substr(1), so includes the pipe
    auto result = parseRemovalLine("@mesh|extra");
    auto* tr = std::get_if<ParsedRemovalTypeRule>(&result);
    ASSERT_NE(tr, nullptr);
    EXPECT_EQ(tr->meshName, "mesh|extra");
}

// ════════════════════════════════════════════════════════════════════════════
// parseSlotLine tests
// ════════════════════════════════════════════════════════════════════════════

TEST(ParseSlotLine, ValidSlot)
{
    auto result = parseSlotLine("3|Adorned Door|T_UI_BuildIcon_AdornedDoor");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->slotIndex, 3);
    EXPECT_EQ(slot->displayName, "Adorned Door");
    EXPECT_EQ(slot->textureName, "T_UI_BuildIcon_AdornedDoor");
}

TEST(ParseSlotLine, NoTexture)
{
    // Backward compatibility: no texture name
    auto result = parseSlotLine("0|Wall Stone");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->slotIndex, 0);
    EXPECT_EQ(slot->displayName, "Wall Stone");
    EXPECT_EQ(slot->textureName, "");
}

TEST(ParseSlotLine, RotationKey)
{
    auto result = parseSlotLine("rotation|15");
    auto* rot = std::get_if<ParsedRotation>(&result);
    ASSERT_NE(rot, nullptr);
    EXPECT_EQ(rot->step, 15);
}

TEST(ParseSlotLine, RotationZero)
{
    auto result = parseSlotLine("rotation|0");
    auto* rot = std::get_if<ParsedRotation>(&result);
    ASSERT_NE(rot, nullptr);
    EXPECT_EQ(rot->step, 0);
}

TEST(ParseSlotLine, RotationMax)
{
    auto result = parseSlotLine("rotation|90");
    auto* rot = std::get_if<ParsedRotation>(&result);
    ASSERT_NE(rot, nullptr);
    EXPECT_EQ(rot->step, 90);
}

TEST(ParseSlotLine, RotationOutOfRange)
{
    auto result = parseSlotLine("rotation|999");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, RotationNegative)
{
    auto result = parseSlotLine("rotation|-5");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, InvalidSlot)
{
    // Slot 99 >= OVERLAY_BUILD_SLOTS (8) → rejected
    auto result = parseSlotLine("99|name|tex");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, NegativeSlot)
{
    auto result = parseSlotLine("-1|name|tex");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, Comment)
{
    auto result = parseSlotLine("# MoriaCppMod quick-build slots");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, EmptyLine)
{
    auto result = parseSlotLine("");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, NoPipe)
{
    auto result = parseSlotLine("invalidline");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, AllSlotsValid)
{
    // Slots 0-7 are valid
    for (int i = 0; i < 8; i++)
    {
        auto result = parseSlotLine(std::to_string(i) + "|Recipe|Tex");
        auto* slot = std::get_if<ParsedSlot>(&result);
        ASSERT_NE(slot, nullptr) << "Slot " << i << " should be valid";
        EXPECT_EQ(slot->slotIndex, i);
    }
}

TEST(ParseSlotLine, SlotBoundary)
{
    // Slot 7 = last valid, slot 8 = first invalid
    auto valid = parseSlotLine("7|name|tex");
    EXPECT_NE(std::get_if<ParsedSlot>(&valid), nullptr);

    auto invalid = parseSlotLine("8|name|tex");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(invalid));
}

TEST(ParseSlotLine, WhitespaceOnly)
{
    auto result = parseSlotLine("   ");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, DisplayNameWithPipes)
{
    // Display name with embedded pipe — third field is texture
    auto result = parseSlotLine("0|Name With Spaces|TextureName");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->displayName, "Name With Spaces");
    EXPECT_EQ(slot->textureName, "TextureName");
}

TEST(ParseSlotLine, FourFieldFormat)
{
    // Full 4-field format: slot|displayName|textureName|rowName
    auto result = parseSlotLine("2|Wall Stone|T_UI_BuildIcon_WallStone|DT_Recipes.WallStone_Row");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->slotIndex, 2);
    EXPECT_EQ(slot->displayName, "Wall Stone");
    EXPECT_EQ(slot->textureName, "T_UI_BuildIcon_WallStone");
    EXPECT_EQ(slot->rowName, "DT_Recipes.WallStone_Row");
}

TEST(ParseSlotLine, FourFieldEmptyRowName)
{
    // 4-field with empty row name
    auto result = parseSlotLine("0|Name|Tex|");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->textureName, "Tex");
    EXPECT_EQ(slot->rowName, "");
}

TEST(ParseSlotLine, RotationNonNumeric)
{
    // rotation with non-numeric value
    auto result = parseSlotLine("rotation|abc");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, NonNumericSlotKey)
{
    // A non-numeric, non-rotation key — "abc" fails stoi
    auto result = parseSlotLine("abc|name|tex");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, RotationBoundary91)
{
    // rotation=91 is just over the max of 90
    auto result = parseSlotLine("rotation|91");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseSlotLine, EmptyDisplayName)
{
    // slot with empty display name
    auto result = parseSlotLine("0||tex");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->displayName, "");
    EXPECT_EQ(slot->textureName, "tex");
}

TEST(ParseSlotLine, LongMeshName)
{
    // Very long display name (stress test)
    std::string longName(500, 'X');
    auto result = parseSlotLine("0|" + longName + "|tex");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->displayName.size(), 500u);
}

TEST(ParseSlotLine, SlotWithSpacesInTexture)
{
    auto result = parseSlotLine("5|Door|T UI BuildIcon Door");
    auto* slot = std::get_if<ParsedSlot>(&result);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->textureName, "T UI BuildIcon Door");
}

// ════════════════════════════════════════════════════════════════════════════
// parseKeybindLine tests
// ════════════════════════════════════════════════════════════════════════════

TEST(ParseKeybindLine, ValidBind)
{
    auto result = parseKeybindLine("0|112");
    auto* kb = std::get_if<ParsedKeybind>(&result);
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->bindIndex, 0);
    EXPECT_EQ(kb->vkCode, 112); // VK_F1
}

TEST(ParseKeybindLine, LastValidBind)
{
    auto result = parseKeybindLine("16|13"); // BIND_COUNT-1, VK_RETURN
    auto* kb = std::get_if<ParsedKeybind>(&result);
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->bindIndex, 16);
    EXPECT_EQ(kb->vkCode, 13);
}

TEST(ParseKeybindLine, ModifierValid_Shift)
{
    auto result = parseKeybindLine("mod|16"); // VK_SHIFT
    auto* mod = std::get_if<ParsedModifier>(&result);
    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(mod->vkCode, VK_SHIFT);
}

TEST(ParseKeybindLine, ModifierValid_Ctrl)
{
    auto result = parseKeybindLine("mod|17"); // VK_CONTROL
    auto* mod = std::get_if<ParsedModifier>(&result);
    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(mod->vkCode, VK_CONTROL);
}

TEST(ParseKeybindLine, ModifierValid_Alt)
{
    auto result = parseKeybindLine("mod|18"); // VK_MENU
    auto* mod = std::get_if<ParsedModifier>(&result);
    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(mod->vkCode, VK_MENU);
}

TEST(ParseKeybindLine, ModifierValid_RAlt)
{
    auto result = parseKeybindLine("mod|165"); // VK_RMENU (0xA5)
    auto* mod = std::get_if<ParsedModifier>(&result);
    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(mod->vkCode, VK_RMENU);
}

TEST(ParseKeybindLine, ModifierInvalid)
{
    auto result = parseKeybindLine("mod|99");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, OutOfRange)
{
    // Index 99 >= BIND_COUNT (17) → rejected
    auto result = parseKeybindLine("99|112");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, NegativeIndex)
{
    auto result = parseKeybindLine("-1|112");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, VKZero)
{
    // VK 0 is invalid (must be > 0)
    auto result = parseKeybindLine("0|0");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, VK256)
{
    // VK 256 is out of range (must be < 256)
    auto result = parseKeybindLine("0|256");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, Comment)
{
    auto result = parseKeybindLine("# keybindings header");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, EmptyLine)
{
    auto result = parseKeybindLine("");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, NoPipe)
{
    auto result = parseKeybindLine("invalid");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(ParseKeybindLine, VKBoundaryMin)
{
    // VK 1 = minimum valid
    auto result = parseKeybindLine("0|1");
    auto* kb = std::get_if<ParsedKeybind>(&result);
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->vkCode, 1);
}

TEST(ParseKeybindLine, VKBoundaryMax)
{
    // VK 255 = maximum valid
    auto result = parseKeybindLine("0|255");
    auto* kb = std::get_if<ParsedKeybind>(&result);
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->vkCode, 255);
}

TEST(ParseKeybindLine, WhitespaceOnly)
{
    auto result = parseKeybindLine("   ");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

// ════════════════════════════════════════════════════════════════════════════
// nameToVK tests (reverse of keyName)
// ════════════════════════════════════════════════════════════════════════════

class NameToVKTest : public ::testing::Test
{
  protected:
    void SetUp() override { Loc::clear(); Loc::initDefaults(); }
    void TearDown() override { Loc::clear(); }
};

TEST_F(NameToVKTest, FKeys)
{
    EXPECT_EQ(nameToVK(L"F1"), 0x70);
    EXPECT_EQ(nameToVK(L"F12"), 0x7B);
    EXPECT_EQ(nameToVK(L"F24"), 0x87);
}

TEST_F(NameToVKTest, CaseInsensitive)
{
    EXPECT_EQ(nameToVK(L"f1"), 0x70);
    EXPECT_EQ(nameToVK(L"pgdn"), 0x22);
    EXPECT_EQ(nameToVK(L"SPACE"), 0x20);
    EXPECT_EQ(nameToVK(L"num+"), 0x6B);
}

TEST_F(NameToVKTest, NumpadDigits)
{
    EXPECT_EQ(nameToVK(L"Num0"), 0x60);
    EXPECT_EQ(nameToVK(L"Num9"), 0x69);
}

TEST_F(NameToVKTest, NumpadOps)
{
    EXPECT_EQ(nameToVK(L"Num+"), 0x6B);
    EXPECT_EQ(nameToVK(L"Num-"), 0x6D);
    EXPECT_EQ(nameToVK(L"Num*"), 0x6A);
    EXPECT_EQ(nameToVK(L"Num/"), 0x6F);
    EXPECT_EQ(nameToVK(L"Num."), 0x6E);
    EXPECT_EQ(nameToVK(L"NumSep"), 0x6C);
}

TEST_F(NameToVKTest, Letters)
{
    EXPECT_EQ(nameToVK(L"A"), 0x41);
    EXPECT_EQ(nameToVK(L"Z"), 0x5A);
    EXPECT_EQ(nameToVK(L"a"), 0x41); // lowercase -> uppercase VK
}

TEST_F(NameToVKTest, Digits)
{
    EXPECT_EQ(nameToVK(L"0"), 0x30);
    EXPECT_EQ(nameToVK(L"9"), 0x39);
}

TEST_F(NameToVKTest, Symbols)
{
    EXPECT_EQ(nameToVK(L"]"), 0xDD);
    EXPECT_EQ(nameToVK(L"["), 0xDB);
    EXPECT_EQ(nameToVK(L"\\"), 0xDC);
    EXPECT_EQ(nameToVK(L";"), 0xBA);
    EXPECT_EQ(nameToVK(L"'"), 0xDE);
    EXPECT_EQ(nameToVK(L"`"), 0xC0);
    EXPECT_EQ(nameToVK(L","), 0xBC);
    EXPECT_EQ(nameToVK(L"-"), 0xBD);
    EXPECT_EQ(nameToVK(L"."), 0xBE);
    EXPECT_EQ(nameToVK(L"/"), 0xBF);
    EXPECT_EQ(nameToVK(L"="), 0xBB);
}

TEST_F(NameToVKTest, SpecialKeys)
{
    EXPECT_EQ(nameToVK(L"PgDn"), 0x22);
    EXPECT_EQ(nameToVK(L"PgUp"), 0x21);
    EXPECT_EQ(nameToVK(L"Home"), 0x24);
    EXPECT_EQ(nameToVK(L"End"), 0x23);
    EXPECT_EQ(nameToVK(L"Space"), 0x20);
    EXPECT_EQ(nameToVK(L"Tab"), 0x09);
    EXPECT_EQ(nameToVK(L"Enter"), 0x0D);
    EXPECT_EQ(nameToVK(L"Ins"), 0x2D);
    EXPECT_EQ(nameToVK(L"Del"), 0x2E);
}

TEST_F(NameToVKTest, HexFallback)
{
    EXPECT_EQ(nameToVK(L"0xFF"), 0xFF);
    EXPECT_EQ(nameToVK(L"0x01"), 0x01);
}

TEST_F(NameToVKTest, Invalid)
{
    EXPECT_EQ(nameToVK(L"INVALID"), std::nullopt);
    EXPECT_EQ(nameToVK(L""), std::nullopt);
    EXPECT_EQ(nameToVK(L"F0"), std::nullopt);
    EXPECT_EQ(nameToVK(L"F25"), std::nullopt);
}

TEST_F(NameToVKTest, FKeysExtended)
{
    // nameToVK supports F1-F24 (wider than keyName's F1-F12)
    EXPECT_EQ(nameToVK(L"F13"), 0x7C);
    EXPECT_EQ(nameToVK(L"F16"), 0x7F);
    EXPECT_EQ(nameToVK(L"F24"), 0x87);
    // F0 and F25 are out of range
    EXPECT_EQ(nameToVK(L"F0"), std::nullopt);
    EXPECT_EQ(nameToVK(L"F25"), std::nullopt);
}

TEST_F(NameToVKTest, HexCaseSensitivity)
{
    // Both uppercase and lowercase hex should work
    EXPECT_EQ(nameToVK(L"0xFF"), 0xFF);
    EXPECT_EQ(nameToVK(L"0xff"), 0xFF);
    EXPECT_EQ(nameToVK(L"0xAB"), 0xAB);
    EXPECT_EQ(nameToVK(L"0xab"), 0xAB);
    // Invalid hex strings
    EXPECT_EQ(nameToVK(L"0xGG"), std::nullopt);
    EXPECT_EQ(nameToVK(L"0x00"), std::nullopt); // VK 0 rejected (val > 0)
}

TEST_F(NameToVKTest, FKeyCaseVariations)
{
    EXPECT_EQ(nameToVK(L"f12"), 0x7B);
    EXPECT_EQ(nameToVK(L"F1"), 0x70);
    EXPECT_EQ(nameToVK(L"f24"), 0x87);
}

TEST_F(NameToVKTest, NumpadCaseVariations)
{
    EXPECT_EQ(nameToVK(L"num0"), 0x60);
    EXPECT_EQ(nameToVK(L"NUM9"), 0x69);
    EXPECT_EQ(nameToVK(L"num*"), 0x6A);
    EXPECT_EQ(nameToVK(L"numsep"), 0x6C);
}

TEST_F(NameToVKTest, EmptyAndWhitespace)
{
    EXPECT_EQ(nameToVK(L""), std::nullopt);
    EXPECT_EQ(nameToVK(L" "), std::nullopt); // space char is not "Space"
}

TEST_F(NameToVKTest, RoundTrip)
{
    // For all VK codes that keyName produces a recognizable name, verify round-trip
    for (int vk = 1; vk < 256; vk++)
    {
        std::wstring name = keyName(static_cast<uint8_t>(vk));
        if (name.substr(0, 2) == L"0x") continue; // hex fallback = unknown key, skip
        auto result = nameToVK(name);
        std::string narrow;
        for (auto c : name) narrow += static_cast<char>(c);
        EXPECT_TRUE(result.has_value())
            << "nameToVK failed for keyName(" << vk << ") = '" << narrow << "'";
        if (result)
            EXPECT_EQ(*result, static_cast<uint8_t>(vk))
                << "Round-trip failed for VK " << vk;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// modifierNameToVK / modifierToIniName tests
// ════════════════════════════════════════════════════════════════════════════

TEST(ModifierMapping, NameToVK)
{
    EXPECT_EQ(modifierNameToVK(L"SHIFT"), VK_SHIFT);
    EXPECT_EQ(modifierNameToVK(L"CTRL"), VK_CONTROL);
    EXPECT_EQ(modifierNameToVK(L"ALT"), VK_MENU);
    EXPECT_EQ(modifierNameToVK(L"RALT"), VK_RMENU);
    EXPECT_EQ(modifierNameToVK(L"shift"), VK_SHIFT);
    EXPECT_EQ(modifierNameToVK(L"invalid"), std::nullopt);
}

TEST(ModifierMapping, ToIniName)
{
    EXPECT_EQ(modifierToIniName(VK_SHIFT), "SHIFT");
    EXPECT_EQ(modifierToIniName(VK_CONTROL), "CTRL");
    EXPECT_EQ(modifierToIniName(VK_MENU), "ALT");
    EXPECT_EQ(modifierToIniName(VK_RMENU), "RALT");
}

TEST(ModifierMapping, RoundTrip)
{
    for (uint8_t vk : {(uint8_t)VK_SHIFT, (uint8_t)VK_CONTROL, (uint8_t)VK_MENU, (uint8_t)VK_RMENU})
    {
        std::string name = modifierToIniName(vk);
        std::wstring wname(name.begin(), name.end());
        auto result = modifierNameToVK(wname);
        EXPECT_TRUE(result.has_value());
        if (result) EXPECT_EQ(*result, vk);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// parseIniLine tests
// ════════════════════════════════════════════════════════════════════════════

TEST(ParseIniLine, SectionHeader)
{
    auto r = parseIniLine("[Keybindings]");
    auto* sec = std::get_if<ParsedIniSection>(&r);
    ASSERT_NE(sec, nullptr);
    EXPECT_EQ(sec->name, "Keybindings");
}

TEST(ParseIniLine, SectionWithSpaces)
{
    auto r = parseIniLine("  [ Preferences ]");
    auto* sec = std::get_if<ParsedIniSection>(&r);
    ASSERT_NE(sec, nullptr);
    EXPECT_EQ(sec->name, "Preferences");
}

TEST(ParseIniLine, KeyValueSimple)
{
    auto r = parseIniLine("QuickBuild1 = F1");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "QuickBuild1");
    EXPECT_EQ(kv->value, "F1");
}

TEST(ParseIniLine, KeyValueNoSpaces)
{
    auto r = parseIniLine("Rotation=F9");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Rotation");
    EXPECT_EQ(kv->value, "F9");
}

TEST(ParseIniLine, InlineComment)
{
    auto r = parseIniLine("Verbose = false ; enable for debugging");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->value, "false");
}

TEST(ParseIniLine, SemicolonValue)
{
    // Bare semicolon as value should NOT be stripped as comment
    auto r = parseIniLine("Target = ;");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->value, ";");
}

TEST(ParseIniLine, SemicolonComment)
{
    auto r = parseIniLine("; This is a comment");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, HashComment)
{
    auto r = parseIniLine("# This is a comment");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, EmptyLine)
{
    EXPECT_TRUE(std::holds_alternative<std::monostate>(parseIniLine("")));
}

TEST(ParseIniLine, WhitespaceOnly)
{
    EXPECT_TRUE(std::holds_alternative<std::monostate>(parseIniLine("   ")));
}

TEST(ParseIniLine, EmptySectionBrackets)
{
    // [] — empty section name should be rejected
    auto r = parseIniLine("[]");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, KeyNoValue)
{
    // "Key=" — empty value is valid
    auto r = parseIniLine("Key=");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Key");
    EXPECT_EQ(kv->value, "");
}

TEST(ParseIniLine, MultipleEquals)
{
    // "Key=Value=Extra" — only splits at first '='
    auto r = parseIniLine("Key=Value=Extra");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Key");
    EXPECT_EQ(kv->value, "Value=Extra");
}

TEST(ParseIniLine, TabIndented)
{
    auto r = parseIniLine("\tKey = Value");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Key");
    EXPECT_EQ(kv->value, "Value");
}

TEST(ParseIniLine, NoKey)
{
    // "= value" — empty key should be rejected
    auto r = parseIniLine("= value");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

// ════════════════════════════════════════════════════════════════════════════
// bindIndexToIniKey / iniKeyToBindIndex tests
// ════════════════════════════════════════════════════════════════════════════

TEST(IniKeyMapping, AllIndicesHaveKeys)
{
    for (int i = 0; i < BIND_COUNT; i++)
        EXPECT_NE(bindIndexToIniKey(i), nullptr) << "Missing INI key for index " << i;
}

TEST(IniKeyMapping, OutOfRange)
{
    EXPECT_EQ(bindIndexToIniKey(-1), nullptr);
    EXPECT_EQ(bindIndexToIniKey(BIND_COUNT), nullptr);
}

TEST(IniKeyMapping, RoundTrip)
{
    for (int i = 0; i < BIND_COUNT; i++)
    {
        const char* key = bindIndexToIniKey(i);
        EXPECT_EQ(iniKeyToBindIndex(key), i) << "Round-trip failed for index " << i;
    }
}

TEST(IniKeyMapping, CaseInsensitive)
{
    EXPECT_EQ(iniKeyToBindIndex("quickbuild1"), 0);
    EXPECT_EQ(iniKeyToBindIndex("QUICKBUILD1"), 0);
    EXPECT_EQ(iniKeyToBindIndex("ROTATION"), 8);
}

TEST(IniKeyMapping, Unknown)
{
    EXPECT_EQ(iniKeyToBindIndex("nonexistent"), -1);
}

// ── parseIniLine edge cases ──

TEST(ParseIniLine, MalformedSectionNoClose)
{
    // "[Section" without closing bracket — not a section header
    auto r = parseIniLine("[Section");
    // The front is '[' but the back is not ']', so it should NOT be parsed as a section.
    // It will try key=value parse, fail (no '='), and return monostate
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, KeyWithSpecialChars)
{
    // Keys with dots, dashes, underscores are common in INI files
    auto r = parseIniLine("Some-Key.Name_Here = Value");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Some-Key.Name_Here");
    EXPECT_EQ(kv->value, "Value");
}

TEST(ParseIniLine, ValueWithSpaces)
{
    auto r = parseIniLine("Key = Value With Spaces");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->value, "Value With Spaces");
}

TEST(ParseIniLine, ConsecutiveEquals)
{
    // "Key===Value" — splits at first '=', rest is value
    auto r = parseIniLine("Key===Value");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Key");
    EXPECT_EQ(kv->value, "==Value");
}

TEST(ParseIniLine, SectionWithTrailingContent)
{
    // "[Section] extra" — back is not ']', so not parsed as section
    auto r = parseIniLine("[Section] extra");
    // trimmed string is "[Section] extra" — back is not ']'
    // so it falls through to key=value parse, which fails (no '=')
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, OnlyEquals)
{
    // "=" — empty key, should be rejected
    auto r = parseIniLine("=");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r));
}

TEST(ParseIniLine, BooleanValue)
{
    auto r = parseIniLine("Verbose = true");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "Verbose");
    EXPECT_EQ(kv->value, "true");
}

TEST(ParseIniLine, NumericValue)
{
    auto r = parseIniLine("RotationStep = 5");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->key, "RotationStep");
    EXPECT_EQ(kv->value, "5");
}

TEST(ParseIniLine, SectionWithNumbers)
{
    auto r = parseIniLine("[Section2]");
    auto* sec = std::get_if<ParsedIniSection>(&r);
    ASSERT_NE(sec, nullptr);
    EXPECT_EQ(sec->name, "Section2");
}

TEST(ParseIniLine, InlineCommentNoSpace)
{
    // ";comment" without space before semicolon should NOT strip (only " ;" triggers)
    auto r = parseIniLine("Key = Value;comment");
    auto* kv = std::get_if<ParsedIniKeyValue>(&r);
    ASSERT_NE(kv, nullptr);
    EXPECT_EQ(kv->value, "Value;comment");
}
