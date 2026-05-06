


#pragma once

#include "moria_common.h"

namespace MoriaMods
{


    static constexpr int MC_BIND_BASE = 8;
    static constexpr int BIND_ROTATION  = 8;
    static constexpr int BIND_SNAP      = 9;
    // BIND_STABILITY (10) removed — unused
    static constexpr int BIND_TARGET    = 12;
    static constexpr int BIND_CONFIG    = 13;
    static constexpr int BIND_AB_OPEN   = 17;
    // slot 18 repurposed from "Reserved/Diagnostics" to Save Game
    // (defaults to F12, freed up after the legacy F12 menu dispatcher was
    // disabled in this version). Live keybind: triggers triggerSaveGame().
    static constexpr int BIND_SAVE_GAME = 18;
    static constexpr int BIND_TRASH_ITEM     = 19;
    static constexpr int BIND_REPLENISH_ITEM = 20;
    static constexpr int BIND_REMOVE_ATTRS   = 21;
    static constexpr int BIND_PITCH_ROTATE   = 22;
    static constexpr int BIND_ROLL_ROTATE    = 23;
    // HUD reposition mode toggle. Default F10. When pressed,
    // forces the inspect window + 4-circle rotation display visible so
    // the user can drag them. ESC or another F10 press exits the mode.
    static constexpr int BIND_REPOSITION_HUD = 24;

    struct KeyBind
    {
        std::wstring label;
        std::wstring section;
        uint8_t key;
        bool enabled = true;
    };

    inline KeyBind s_bindings[BIND_COUNT] = {
            {L"Quick Build 1", L"Quick Building", Input::Key::F1},
            {L"Quick Build 2", L"Quick Building", Input::Key::F2},
            {L"Quick Build 3", L"Quick Building", Input::Key::F3},
            {L"Quick Build 4", L"Quick Building", Input::Key::F4},
            {L"Quick Build 5", L"Quick Building", Input::Key::F5},
            {L"Quick Build 6", L"Quick Building", Input::Key::F6},
            {L"Quick Build 7", L"Quick Building", Input::Key::F7},
            {L"Quick Build 8", L"Quick Building", Input::Key::F8},
            {L"Rotation", L"Mod Controller", Input::Key::F9},
            {L"Snap Toggle", L"Mod Controller", Input::Key::OEM_FOUR},
            {L"Integrity Check", L"Mod Controller", Input::Key::DIVIDE},
            {L"Super Dwarf", L"Mod Controller", Input::Key::OEM_FIVE},
            {L"Target", L"Mod Controller", Input::Key::OEM_SIX},
            {L"Configuration", L"Mod Controller", Input::Key::F12},
            {L"Remove Single", L"Mod Controller", Input::Key::NUM_ONE},
            {L"Undo Last", L"Mod Controller", Input::Key::NUM_TWO},
            {L"Remove All", L"Mod Controller", Input::Key::NUM_THREE},
            {L"Advanced Builder Open", L"Advanced Builder", Input::Key::ADD},
            {L"Save Game", L"General", Input::Key::F12},
            {L"Trash Item", L"Game Options", VK_DELETE},
            {L"Replenish Item", L"Game Options", VK_INSERT},
            {L"Remove Attributes", L"Game Options", VK_END},
            {L"Pitch Rotate", L"Game Options", Input::Key::OEM_PERIOD},
            {L"Roll Rotate", L"Game Options", Input::Key::OEM_COMMA},
            {L"Reposition HUD", L"Mod Controller", Input::Key::F10},
    };

    inline std::atomic<int> s_capturingBind{-1};


    inline std::atomic<uint8_t> s_modifierVK{VK_SHIFT};

    // v6.8.0 CP3 — per-slot chord-aware storage for the Quick Build "Set"
    // action (the chord that saves the current recipe to slot N). Defaults
    // mirror the legacy "Shift + use chord" model so dispatch keeps working
    // until the user rebinds a SET row in Settings → Key Mapping.
    //   vk        — primary key VK code
    //   modBits   — bShift bit0, bCtrl bit1, bAlt bit2, bCmd bit3
    struct SetBind { uint8_t vk; uint8_t modBits; };
    inline SetBind s_setBindings[8] = {
        { Input::Key::F1, 0x01 }, { Input::Key::F2, 0x01 },
        { Input::Key::F3, 0x01 }, { Input::Key::F4, 0x01 },
        { Input::Key::F5, 0x01 }, { Input::Key::F6, 0x01 },
        { Input::Key::F7, 0x01 }, { Input::Key::F8, 0x01 },
    };


    inline std::string iniPath() { return modPath("Mods/MoriaCppMod/MoriaCppMod.ini"); }
    inline std::string oldKeybindPath() { return modPath("Mods/MoriaCppMod/keybindings.txt"); }


    inline bool isModifierDown()
    {
        return (GetAsyncKeyState(s_modifierVK.load()) & 0x8000) != 0;
    }

    // v6.8.0 CP3 — chord-held check: VK + modifier-bits chord matches the
    // current keyboard state. modBits: bit0=Shift, bit1=Ctrl, bit2=Alt.
    // The primary key uses & 1 (just-pressed edge) when checked via the
    // caller's edge-detector; this helper only reads the held state.
    inline bool isChordHeld(uint8_t vk, uint8_t modBits)
    {
        if (!vk) return false;
        if ((GetAsyncKeyState(vk) & 0x8000) == 0) return false;
        bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        if (((modBits & 0x01) != 0) != shift) return false;
        if (((modBits & 0x02) != 0) != ctrl)  return false;
        if (((modBits & 0x04) != 0) != alt)   return false;
        return true;
    }


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


    inline HWND findGameWindow()
    {

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

}
