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
