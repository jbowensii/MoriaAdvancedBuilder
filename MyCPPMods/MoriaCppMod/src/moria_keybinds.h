


#pragma once

#include "moria_common.h"

namespace MoriaMods
{


    static constexpr int MC_BIND_BASE = 8;
    static constexpr int BIND_ROTATION  = 8;
    static constexpr int BIND_SNAP      = 9;
    static constexpr int BIND_STABILITY = 10;
    static constexpr int BIND_TARGET    = 12;
    static constexpr int BIND_CONFIG    = 13;
    static constexpr int BIND_AB_OPEN   = 17;
    static constexpr int BIND_TRASH_ITEM     = 19;
    static constexpr int BIND_REPLENISH_ITEM = 20;
    static constexpr int BIND_REMOVE_ATTRS   = 21;

    struct KeyBind
    {
        const wchar_t* label;
        const wchar_t* section;
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
            {L"Reserved", L"Diagnostics", 0},
            {L"Trash Item", L"Game Options", VK_DELETE},
            {L"Replenish Item", L"Game Options", VK_INSERT},
            {L"Remove Attributes", L"Game Options", VK_END},
    };

    inline std::atomic<int> s_capturingBind{-1};


    inline std::atomic<uint8_t> s_modifierVK{VK_SHIFT};


    inline const char* INI_PATH = "Mods/MoriaCppMod/MoriaCppMod.ini";
    inline const char* OLD_KEYBIND_PATH = "Mods/MoriaCppMod/keybindings.txt";


    inline bool isModifierDown()
    {
        return (GetAsyncKeyState(s_modifierVK.load()) & 0x8000) != 0;
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
