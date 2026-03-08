// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_keybinds.h — Keybinding system & window discovery                  ║
// ║  25 rebindable keys (8 quickbuild + 12 MC slots + 5 extra),              ║
// ║  VK code ↔ string conversion, findGameWindow() for overlay               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once

#include "moria_common.h"

namespace MoriaMods
{
    // ════════════════════════════════════════════════════════════════════════════
    // Keybinding Constants & Data
    // ════════════════════════════════════════════════════════════════════════════

    // BIND_COUNT defined in moria_testable.h
    static constexpr int MC_BIND_BASE = 8;     // s_bindings[8..19] = MC slots 0..11
    static constexpr int BIND_ROTATION = 8;    // "Rotation" — MC slot 0
    static constexpr int BIND_TARGET   = 9;    // "Target" — MC slot 1
    static constexpr int BIND_CONFIG   = 19;   // "Configuration" — MC slot 11
    static constexpr int BIND_AB_OPEN  = 20;   // "Advanced Builder Open"
    static constexpr int BIND_STABILITY = 10;  // "Stability Check" — MC slot 2
    static constexpr int BIND_SNAP      = 13;  // "Snap Toggle" — MC slot 5
    static constexpr int BIND_TRASH_ITEM     = 22;  // "Trash Item"
    static constexpr int BIND_REPLENISH_ITEM = 23;  // "Replenish Item"
    static constexpr int BIND_REMOVE_ATTRS   = 24;  // "Remove Attributes"

    struct KeyBind
    {
        const wchar_t* label;
        const wchar_t* section;
        uint8_t key; // Input::Key value (same as VK code)
        bool enabled = true; // false = keybind suppressed even if key is set
    };

    inline KeyBind s_bindings[BIND_COUNT] = {
            {L"Quick Build 1", L"Quick Building", Input::Key::F1},                     // 0
            {L"Quick Build 2", L"Quick Building", Input::Key::F2},                     // 1
            {L"Quick Build 3", L"Quick Building", Input::Key::F3},                     // 2
            {L"Quick Build 4", L"Quick Building", Input::Key::F4},                     // 3
            {L"Quick Build 5", L"Quick Building", Input::Key::F5},                     // 4
            {L"Quick Build 6", L"Quick Building", Input::Key::F6},                     // 5
            {L"Quick Build 7", L"Quick Building", Input::Key::F7},                     // 6
            {L"Quick Build 8", L"Quick Building", Input::Key::F8},                     // 7
            {L"Rotation", L"Mod Controller", Input::Key::F9},                          // 8  (BIND_ROTATION, MC slot 0)
            {L"Target", L"Mod Controller", Input::Key::OEM_SIX},                       // 9  (BIND_TARGET, MC slot 1)
            {L"Integrity Check", L"Mod Controller", Input::Key::DIVIDE},               // 10 (BIND_STABILITY, MC slot 2)
            {L"Super Dwarf", L"Mod Controller", Input::Key::OEM_FIVE},                 // 11 (MC slot 3)
            {L"Empty", L"Mod Controller", 0},                                           // 12 (MC slot 4 — empty)
            {L"Snap Toggle", L"Mod Controller", Input::Key::OEM_FOUR},                    // 13 (BIND_SNAP, MC slot 5)
            {L"Empty", L"Mod Controller", 0},                                           // 14 (MC slot 6 — empty)
            {L"Empty", L"Mod Controller", 0},                                           // 15 (MC slot 7 — empty)
            {L"Remove Single", L"Mod Controller", Input::Key::NUM_ONE},                // 16 (MC slot 8)
            {L"Undo Last", L"Mod Controller", Input::Key::NUM_TWO},                    // 17 (MC slot 9)
            {L"Remove All", L"Mod Controller", Input::Key::NUM_THREE},                 // 18 (MC slot 10)
            {L"Configuration", L"Mod Controller", Input::Key::F12},                    // 19 (BIND_CONFIG, MC slot 11)
            {L"Advanced Builder Open", L"Advanced Builder", Input::Key::ADD},           // 20 (BIND_AB_OPEN)
            {L"Reserved", L"Diagnostics", 0},                                          // 21 (placeholder — no key, no action)
            {L"Trash Item", L"Game Options", VK_DELETE},                                  // 22 (BIND_TRASH_ITEM)
            {L"Replenish Item", L"Game Options", VK_INSERT},                              // 23 (BIND_REPLENISH_ITEM)
            {L"Remove Attributes", L"Game Options", VK_END},                              // 24 (BIND_REMOVE_ATTRS)
    };

    inline std::atomic<int> s_capturingBind{-1};

    // Active modifier key (SHIFT/CTRL/ALT/RALT)
    inline std::atomic<uint8_t> s_modifierVK{VK_SHIFT};

    // Config file paths
    inline const char* INI_PATH = "Mods/MoriaCppMod/MoriaCppMod.ini";
    inline const char* OLD_KEYBIND_PATH = "Mods/MoriaCppMod/keybindings.txt";

    // ════════════════════════════════════════════════════════════════════════════
    // Keybinding Helper Functions
    // ════════════════════════════════════════════════════════════════════════════

    inline bool isModifierDown()
    {
        return (GetAsyncKeyState(s_modifierVK.load()) & 0x8000) != 0;
    }

    // SHIFT+NumLock reverses numpad keys to navigation equivalents; returns alternate VK or 0
    inline uint8_t numpadShiftAlternate(uint8_t vk)
    {
        switch (vk)
        {
        case VK_NUMPAD0: return VK_INSERT;
        case VK_NUMPAD1: return VK_END;
        case VK_NUMPAD2: return VK_DOWN;
        case VK_NUMPAD3: return VK_NEXT;
        case VK_NUMPAD4: return VK_LEFT;
        case VK_NUMPAD5: return VK_CLEAR;
        case VK_NUMPAD6: return VK_RIGHT;
        case VK_NUMPAD7: return VK_HOME;
        case VK_NUMPAD8: return VK_UP;
        case VK_NUMPAD9: return VK_PRIOR;
        default: return 0;
        }
    }

    // modifierName, nextModifier, keyName — defined in moria_testable.h

    inline HWND findGameWindow()
    {
        // Find largest visible UnrealWindow
        struct FindData
        {
            HWND best;
            int bestArea;
        };
        FindData fd{nullptr, 0};
        EnumWindows(
                [](HWND hwnd, LPARAM lp) -> BOOL {
                    wchar_t cls[64]{};
                    GetClassNameW(hwnd, cls, 64);
                    if (wcscmp(cls, L"UnrealWindow") != 0) return TRUE;
                    if (!IsWindowVisible(hwnd)) return TRUE;
                    RECT r;
                    GetWindowRect(hwnd, &r);
                    int area = (r.right - r.left) * (r.bottom - r.top);
                    auto* fd = reinterpret_cast<FindData*>(lp);
                    if (area > fd->bestArea)
                    {
                        fd->best = hwnd;
                        fd->bestArea = area;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&fd));
        return fd.best;
    }

} // namespace MoriaMods
