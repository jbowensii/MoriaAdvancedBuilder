// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  MoriaCppMod v1.9 — Advanced Builder & HISM Removal for Return to Moria   ║
// ║                                                                            ║
// ║  A UE4SS C++ mod for Return to Moria (UE4.27) providing:                  ║
// ║    - HISM instance hiding with persistence across sessions/worlds          ║
// ║    - Quick-build hotbar (F1-F8) with recipe capture & icon overlay         ║
// ║    - Dual-toolbar swap system (PageDown) with name-matching resolve        ║
// ║    - Rotation step control (F9) with ProcessEvent hook integration         ║
// ║    - Debug cheats (free build/craft, instant craft, unlock recipes)        ║
// ║    - Win32 GDI+ overlay, config window, and target info popup             ║
// ║                                                                            ║
// ║  Build:  cmake --build build --config Game__Shipping__Win64                ║
// ║          --target MoriaCppMod                                              ║
// ║  Deploy: Copy MoriaCppMod.dll -> <game>/Mods/MoriaCppMod/dlls/main.dll    ║
// ║                                                                            ║
// ║  Source: github.com/jbowensii/MoriaAdvancedBuilder                        ║
// ║  Date:   2026-02-24                                                        ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ════════════════════════════════════════════════════════════════════════════════
// Section 1: Includes & Forward Declarations
// ════════════════════════════════════════════════════════════════════════════════
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>

namespace MoriaMods
{
    using namespace RC;
    using namespace RC::Unreal;

    // ════════════════════════════════════════════════════════════════════════════
    // Section 2: Geometry Structs, Data Types & Constants
    //   FVec3f, FQuat4f, FRotator3f, FTransformRaw — raw UE4.27 float types
    //   FHitResultLocal (136B) — matches Engine.hpp:2742
    //   FItemHandleLocal (20B) — matches FGK.hpp:1715
    //   ProcessEvent param structs, LineTrace offsets, HISM constants
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr float MY_PI = 3.14159265358979323846f;
    static constexpr float DEG2RAD = MY_PI / 180.0f;
    static constexpr float TRACE_DIST = 5000.0f;   // 50m (was 500m — way too far)
    static constexpr float POS_TOLERANCE = 100.0f; // 1 meter — game scale is huge (walls = 2000 units)
    static constexpr int STREAM_CHECK_INTERVAL = 180;

    // ── Raw UE4.27 types (floats, not doubles) ──
    struct FVec3f
    {
        float X, Y, Z;
    };
    struct FQuat4f
    {
        float X, Y, Z, W;
    };
    struct FRotator3f
    {
        float Pitch, Yaw, Roll;
    };

    struct FTransformRaw
    {
        FQuat4f Rotation;
        FVec3f Translation;
        float _pad1{0};
        FVec3f Scale3D;
        float _pad2{0};
    };
    static_assert(sizeof(FTransformRaw) == 48, "FTransformRaw must be 48 bytes");

    // ── ProcessEvent param structs (layouts confirmed by probe) ──
    struct GetInstanceCount_Params
    {
        int32_t ReturnValue{0};
    };

#pragma pack(push, 1)
    struct GetInstanceTransform_Params
    {
        int32_t InstanceIndex{0};
        uint8_t _pad0[12]{};
        FTransformRaw OutTransform{};
        uint8_t bWorldSpace{1};
        uint8_t ReturnValue{0};
    };
#pragma pack(pop)
    static_assert(sizeof(GetInstanceTransform_Params) == 66, "Must be 66 bytes");

// FHitResult — matches Engine.hpp:2742 (Size: 0x88 = 136 bytes)
#pragma pack(push, 1)
    struct FHitResultLocal
    {
        int32_t FaceIndex;                       // 0x00
        float Time;                              // 0x04
        float Distance;                          // 0x08
        FVec3f Location;                         // 0x0C
        FVec3f ImpactPoint;                      // 0x18
        FVec3f Normal;                           // 0x24
        FVec3f ImpactNormal;                     // 0x30
        FVec3f TraceStart;                       // 0x3C
        FVec3f TraceEnd;                         // 0x48
        float PenetrationDepth;                  // 0x54
        int32_t Item;                            // 0x58
        uint8_t ElementIndex;                    // 0x5C
        uint8_t bBlockingHit;                    // 0x5D
        uint8_t bStartPenetrating;               // 0x5E
        uint8_t _pad5F;                          // 0x5F
        RC::Unreal::FWeakObjectPtr PhysMaterial; // 0x60
        RC::Unreal::FWeakObjectPtr Actor;        // 0x68
        RC::Unreal::FWeakObjectPtr Component;    // 0x70
        uint8_t BoneName[8];                     // 0x78 (FName)
        uint8_t MyBoneName[8];                   // 0x80 (FName)
    }; // 0x88
#pragma pack(pop)
    static_assert(sizeof(FHitResultLocal) == 0x88, "FHitResult must be 136 bytes");

    // FItemHandle — matches FGK.hpp:1715 (Size: 0x14 = 20 bytes)
    struct FItemHandleLocal
    {
        int32_t ID;        // 0x00
        int32_t Payload;   // 0x04
        uint8_t Owner[12]; // 0x08 (TWeakObjectPtr + padding to 0x14)
    }; // 0x14
    static_assert(sizeof(FItemHandleLocal) == 0x14, "FItemHandle must be 20 bytes");

    // LineTraceSingle param offsets (237 bytes, probed)
    namespace LTOff
    {
        constexpr int WorldContextObject = 0;
        constexpr int Start = 8;
        constexpr int End = 20;
        constexpr int TraceChannel = 32;
        constexpr int bTraceComplex = 33;
        constexpr int ActorsToIgnore = 40;
        constexpr int DrawDebugType = 56;
        constexpr int OutHit = 60;
        constexpr int bIgnoreSelf = 196;
        constexpr int TraceColor = 200;
        constexpr int TraceHitColor = 216;
        constexpr int DrawTime = 232;
        constexpr int ReturnValue = 236;
        constexpr int ParmsSize = 237;
    } // namespace LTOff

    // ── Saved removal record ──
    struct SavedRemoval
    {
        std::string meshName;
        float posX, posY, posZ;
    };

    struct RemovedInstance
    {
        UObject* component;
        int32_t instanceIndex{-1};
        FTransformRaw transform;
        std::wstring componentName;
        bool isTypeRule{false};
        std::string typeRuleMeshId;
    };

    // ── Display entry for removal list UI (config window tab 0) ──
    struct RemovalEntry
    {
        bool isTypeRule{false};
        std::string meshName;
        float posX{0}, posY{0}, posZ{0};
        std::wstring friendlyName; // first segment before '-'
        std::wstring fullPathW;    // wide copy of meshName
        std::wstring coordsW;      // formatted coords or "TYPE RULE (all instances)"
    };

    static std::wstring extractFriendlyName(const std::string& meshName)
    {
        auto dash = meshName.find('-');
        std::string shortName = (dash != std::string::npos) ? meshName.substr(0, dash) : meshName;
        return std::wstring(shortName.begin(), shortName.end());
    }

    // PrintString param offsets (discovered at runtime via probe)
    struct PSOffsets
    {
        int worldContext{-1};
        int inString{-1};
        int printToScreen{-1};
        int printToLog{-1};
        int textColor{-1};
        int duration{-1};
        int parmsSize{0};
        bool valid{false};
    };

    // ══════════════════════════════════════════════════════════════
    // Win32 Overlay — transparent top-center bar for hotbar display
    // Uses GDI+ with per-pixel alpha (UpdateLayeredWindow)
    // ══════════════════════════════════════════════════════════════

    static constexpr int OVERLAY_BUILD_SLOTS = 8; // F1-F8: programmable build recipe slots
    static constexpr int OVERLAY_SLOTS = 12;      // F1-F12 total displayed in overlay

    struct OverlaySlot
    {
        std::wstring displayName;                   // recipe name (short)
        std::wstring textureName;                   // e.g. "T_UI_BuildIcon_AdornedDoor"
        std::shared_ptr<Gdiplus::Image> icon;       // loaded PNG icon (ref-counted for thread safety)
        bool used{false};
    };

    struct OverlayState
    {
        HWND overlayHwnd{nullptr};
        HWND gameHwnd{nullptr};
        HANDLE thread{nullptr};
        std::atomic<bool> running{false};
        std::atomic<bool> needsUpdate{true};
        std::atomic<bool> visible{true};
        CRITICAL_SECTION slotCS;
        OverlaySlot slots[OVERLAY_SLOTS]{};
        std::atomic<bool> csInit{false};
        ULONG_PTR gdipToken{0};
        std::wstring iconFolder;                    // path to icon PNGs
        std::atomic<int> rotationStep{5};           // current build rotation step in degrees (shown in F9)
        std::atomic<int> totalRotation{0};          // cumulative build rotation 0-359° (shown in F9)
        std::atomic<int> activeToolbar{0};          // which toolbar is visible (0/1/2) — shown in F12 slot
    };
    static OverlayState s_overlay;

    // ════════════════════════════════════════════════════════════════════════════
    // Section 3: Keybinding System & Window Discovery
    //   Rebindable F-key assignments, VK code → string conversion,
    //   findGameWindow() for overlay positioning
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr int BIND_COUNT = 17;
    static constexpr int MC_BIND_BASE = 8;     // s_bindings[8..15] = MC slots 0..7
    static constexpr int BIND_ROTATION = 8;    // "Rotation" — MC slot 0
    static constexpr int BIND_TARGET   = 9;    // "Target" — MC slot 1
    static constexpr int BIND_SWAP     = 10;   // "Toolbar Swap" — MC slot 2
    static constexpr int BIND_CONFIG   = 15;   // "Configuration" — MC slot 7
    static constexpr int BIND_AB_OPEN  = 16;   // "Advanced Builder Open"
    struct KeyBind
    {
        const wchar_t* label;
        const wchar_t* section;
        uint8_t key; // Input::Key value (same as VK code)
    };
    static KeyBind s_bindings[BIND_COUNT] = {
            {L"Quick Build 1", L"Quick Building", Input::Key::F1},                     // 0
            {L"Quick Build 2", L"Quick Building", Input::Key::F2},                     // 1
            {L"Quick Build 3", L"Quick Building", Input::Key::F3},                     // 2
            {L"Quick Build 4", L"Quick Building", Input::Key::F4},                     // 3
            {L"Quick Build 5", L"Quick Building", Input::Key::F5},                     // 4
            {L"Quick Build 6", L"Quick Building", Input::Key::F6},                     // 5
            {L"Quick Build 7", L"Quick Building", Input::Key::F7},                     // 6
            {L"Quick Build 8", L"Quick Building", Input::Key::F8},                     // 7
            {L"Rotation", L"Mod Controller", Input::Key::F10},                         // 8  (BIND_ROTATION, MC slot 0)
            {L"Target", L"Mod Controller", Input::Key::F9},                            // 9  (BIND_TARGET, MC slot 1)
            {L"Toolbar Swap", L"Mod Controller", Input::Key::PAGE_DOWN},               // 10 (BIND_SWAP, MC slot 2)
            {L"ModMenu 4", L"Mod Controller", Input::Key::NUM_FOUR},                   // 11 (MC slot 3)
            {L"Remove Target", L"Mod Controller", Input::Key::NUM_ONE},                // 12 (MC slot 4)
            {L"Undo Last", L"Mod Controller", Input::Key::NUM_TWO},                    // 13 (MC slot 5)
            {L"Remove All", L"Mod Controller", Input::Key::NUM_THREE},                 // 14 (MC slot 6)
            {L"Configuration", L"Mod Controller", Input::Key::F12},                    // 15 (BIND_CONFIG, MC slot 7)
            {L"Advanced Builder Open", L"Advanced Builder", Input::Key::RETURN},         // 16 (BIND_AB_OPEN)
    };
    static std::atomic<int> s_capturingBind{-1};

    // Modifier key choice: VK_SHIFT (0x10), VK_CONTROL (0x11), VK_MENU (0x12 = ALT), or VK_RMENU (0xA5 = RALT)
    static std::atomic<uint8_t> s_modifierVK{VK_SHIFT};

    static bool isModifierDown()
    {
        return (GetAsyncKeyState(s_modifierVK.load()) & 0x8000) != 0;
    }

    static const wchar_t* modifierName(uint8_t vk)
    {
        switch (vk)
        {
        case VK_SHIFT:
            return L"SHIFT";
        case VK_CONTROL:
            return L"CTRL";
        case VK_MENU:
            return L"ALT";
        case VK_RMENU:
            return L"RALT";
        default:
            return L"SHIFT";
        }
    }

    static uint8_t nextModifier(uint8_t vk)
    {
        switch (vk)
        {
        case VK_SHIFT:
            return VK_CONTROL;
        case VK_CONTROL:
            return VK_MENU;
        case VK_MENU:
            return VK_RMENU;
        case VK_RMENU:
            return VK_SHIFT;
        default:
            return VK_SHIFT;
        }
    }

    static std::wstring keyName(uint8_t vk)
    {
        if (vk >= 0x70 && vk <= 0x7B)
        { // F1-F12
            wchar_t buf[8];
            swprintf_s(buf, L"F%d", vk - 0x70 + 1);
            return buf;
        }
        if (vk >= 0x60 && vk <= 0x69)
        { // Numpad 0-9
            wchar_t buf[8];
            swprintf_s(buf, L"Num%d", vk - 0x60);
            return buf;
        }
        switch (vk)
        {
        case 0x6A:
            return L"Num*";
        case 0x6B:
            return L"Num+";
        case 0x6C:
            return L"NumSep";
        case 0x6D:
            return L"Num-";
        case 0x6E:
            return L"Num.";
        case 0x6F:
            return L"Num/";
        case 0xDC:
            return L"\\";
        case 0xC0:
            return L"`";
        case 0xBA:
            return L";";
        case 0xBB:
            return L"=";
        case 0xBC:
            return L",";
        case 0xBD:
            return L"-";
        case 0xBE:
            return L".";
        case 0xBF:
            return L"/";
        case 0xDB:
            return L"[";
        case 0xDD:
            return L"]";
        case 0xDE:
            return L"'";
        case 0x20:
            return L"Space";
        case 0x09:
            return L"Tab";
        case 0x0D:
            return L"Enter";
        case 0x2D:
            return L"Ins";
        case 0x2E:
            return L"Del";
        case 0x24:
            return L"Home";
        case 0x23:
            return L"End";
        case 0x21:
            return L"PgUp";
        case 0x22:
            return L"PgDn";
        default: {
            wchar_t buf[16];
            if (vk >= 0x30 && vk <= 0x39)
            {
                swprintf_s(buf, L"%c", (wchar_t)vk);
                return buf;
            }
            if (vk >= 0x41 && vk <= 0x5A)
            {
                swprintf_s(buf, L"%c", (wchar_t)vk);
                return buf;
            }
            swprintf_s(buf, L"0x%02X", vk);
            return buf;
        }
        }
    }

    static HWND findGameWindow()
    {
        // UE4 games use "UnrealWindow" window class
        // Pick the LARGEST one in case there are multiple
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

    // ════════════════════════════════════════════════════════════════════════════
    // Section 4: Overlay Rendering & Thread
    //   renderOverlay() — GDI+ rendering of 12-slot hotbar bar
    //   overlayWndProc() — Win32 message handler
    //   overlayThreadProc() — dedicated rendering thread
    // ════════════════════════════════════════════════════════════════════════════

    // Renders the 12-slot hotbar overlay. Called from overlay thread on WM_TIMER.
    // Layout: [F1..F8] | [F9..F12] — build recipes with icons | utility slots
    // Scales relative to 1080p baseline. Uses UpdateLayeredWindow for per-pixel alpha.
    static void renderOverlay(HWND hwnd)
    {
        if (!s_overlay.gameHwnd || !IsWindow(s_overlay.gameHwnd))
        {
            s_overlay.gameHwnd = findGameWindow();
            if (!s_overlay.gameHwnd) return;
        }

        RECT clientRect;
        GetClientRect(s_overlay.gameHwnd, &clientRect);
        POINT origin = {0, 0};
        ClientToScreen(s_overlay.gameHwnd, &origin);
        int gameW = clientRect.right;
        int gameH = clientRect.bottom;
        if (gameW < 100 || gameH < 100) return;

        float scale = gameH / 1080.0f;
        if (scale < 0.5f) scale = 0.5f;

        // Each slot: icon box (slotSize x slotSize) with gap between them
        int slotSize = static_cast<int>(48 * scale);
        int gap = static_cast<int>(4 * scale);
        int padding = static_cast<int>(6 * scale);
        int labelH = static_cast<int>(14 * scale);    // height for F-key label below icon
        int separatorW = static_cast<int>(8 * scale); // white vertical bar between F8 and F9
        // Width = padding + 8 build slots + separator + 4 extra slots + padding
        int overlayW = padding * 2 + OVERLAY_SLOTS * slotSize + (OVERLAY_SLOTS - 1) * gap + separatorW;
        int overlayH = padding * 2 + slotSize + labelH;
        int overlayX = origin.x + (gameW - overlayW) / 2;
        int overlayY = origin.y + static_cast<int>(4 * scale);

        if (!s_overlay.visible)
        {
            ShowWindow(hwnd, SW_HIDE);
            return;
        }
        if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        // Create 32-bit ARGB bitmap
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = overlayW;
        bmi.bmiHeader.biHeight = -overlayH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp)
        {
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

        // Thread-safe slot copy
        OverlaySlot localSlots[OVERLAY_SLOTS]{};
        if (s_overlay.csInit)
        {
            EnterCriticalSection(&s_overlay.slotCS);
            for (int i = 0; i < OVERLAY_SLOTS; i++)
                localSlots[i] = s_overlay.slots[i];
            LeaveCriticalSection(&s_overlay.slotCS);
        }

        {
            Gdiplus::Graphics gfx(memDC);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // Overall background
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(100, 5, 8, 18));
            int radius = static_cast<int>(6 * scale);
            Gdiplus::GraphicsPath bgPath;
            bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
            bgPath.AddArc(overlayW - radius * 2 - 1, 0, radius * 2, radius * 2, 270, 90);
            bgPath.AddArc(overlayW - radius * 2 - 1, overlayH - radius * 2 - 1, radius * 2, radius * 2, 0, 90);
            bgPath.AddArc(0, overlayH - radius * 2 - 1, radius * 2, radius * 2, 90, 90);
            bgPath.CloseFigure();
            gfx.FillPath(&bgBrush, &bgPath);

            // Brushes and fonts
            Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(51, 235, 235, 230)); // off-white 20% opacity
            Gdiplus::SolidBrush usedBrush(Gdiplus::Color(51, 245, 245, 240));  // off-white 20% opacity (used)
            Gdiplus::Pen slotBorder(Gdiplus::Color(180, 100, 160, 230), 3.0f); // light blue outline 2x thick
            Gdiplus::Pen usedBorder(Gdiplus::Color(220, 120, 180, 255), 3.0f); // light blue outline 2x thick (used)
            float labelFontSz = 10.0f * scale;
            if (labelFontSz < 9.0f) labelFontSz = 9.0f;
            Gdiplus::FontFamily fontFamily(L"Consolas");
            Gdiplus::Font labelFont(&fontFamily, labelFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            float nameFontSz = 9.0f * scale;
            if (nameFontSz < 8.0f) nameFontSz = 8.0f;
            Gdiplus::Font nameFont(&fontFamily, nameFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush labelBrush(Gdiplus::Color(200, 140, 170, 210));
            Gdiplus::SolidBrush nameBrush(Gdiplus::Color(180, 180, 200, 230));
            Gdiplus::SolidBrush letterBrush(Gdiplus::Color(140, 100, 140, 200));
            Gdiplus::Font letterFont(&fontFamily, slotSize * 0.45f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat centerFmt;
            centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            // Vertical separator pen (white, thin)
            Gdiplus::Pen separatorPen(Gdiplus::Color(180, 200, 210, 230), 1.0f);

            for (int i = 0; i < OVERLAY_SLOTS; i++)
            {
                // X offset: after F8 (index 8+), add separator width
                int sx = padding + i * (slotSize + gap) + (i >= OVERLAY_BUILD_SLOTS ? separatorW : 0);
                int sy = padding;

                // Draw vertical separator before F9
                if (i == OVERLAY_BUILD_SLOTS)
                {
                    int sepX = padding + OVERLAY_BUILD_SLOTS * (slotSize + gap) - gap / 2 + separatorW / 2;
                    gfx.DrawLine(&separatorPen, sepX, sy + 2, sepX, sy + slotSize - 2);
                }

                // Slot background — F9-F12 always draw as empty
                Gdiplus::Rect slotRect(sx, sy, slotSize, slotSize);
                if (i < OVERLAY_BUILD_SLOTS && localSlots[i].used)
                {
                    gfx.FillRectangle(&usedBrush, slotRect);
                    gfx.DrawRectangle(&usedBorder, slotRect);
                }
                else
                {
                    gfx.FillRectangle(&emptyBrush, slotRect);
                    gfx.DrawRectangle(&slotBorder, slotRect);
                }

                // Icon or letter placeholder (F1-F8 only)
                if (i < OVERLAY_BUILD_SLOTS)
                {
                    if (localSlots[i].used && localSlots[i].icon)
                    {
                        int iconPad = static_cast<int>(3 * scale);
                        gfx.DrawImage(localSlots[i].icon.get(), Gdiplus::Rect(sx + iconPad, sy + iconPad, slotSize - iconPad * 2, slotSize - iconPad * 2));
                    }
                    else if (localSlots[i].used && !localSlots[i].displayName.empty())
                    {
                        wchar_t letter[2] = {localSlots[i].displayName[0], 0};
                        Gdiplus::RectF letterRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                        gfx.DrawString(letter, 1, &letterFont, letterRect, &centerFmt, &letterBrush);
                    }
                }

                // Slot 8 (Target): archery target icon + TGT text
                if (i == 8)
                {
                    float bcx = (float)(sx + slotSize / 2);
                    float bcy = (float)(sy + slotSize / 2);
                    // Concentric rings: white, black, blue, red, gold (outside-in)
                    float r5 = slotSize * 0.42f; // outermost white
                    float r4 = slotSize * 0.35f; // black
                    float r3 = slotSize * 0.28f; // blue
                    float r2 = slotSize * 0.20f; // red
                    float r1 = slotSize * 0.12f; // gold center
                    Gdiplus::SolidBrush bWhite(Gdiplus::Color(160, 240, 240, 235));
                    Gdiplus::SolidBrush bBlack(Gdiplus::Color(160, 40, 40, 40));
                    Gdiplus::SolidBrush bBlue(Gdiplus::Color(160, 50, 120, 200));
                    Gdiplus::SolidBrush bRed(Gdiplus::Color(160, 210, 50, 40));
                    Gdiplus::SolidBrush bGold(Gdiplus::Color(160, 240, 200, 50));
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    gfx.FillEllipse(&bWhite, bcx - r5, bcy - r5, r5 * 2, r5 * 2);
                    gfx.FillEllipse(&bBlack, bcx - r4, bcy - r4, r4 * 2, r4 * 2);
                    gfx.FillEllipse(&bBlue, bcx - r3, bcy - r3, r3 * 2, r3 * 2);
                    gfx.FillEllipse(&bRed, bcx - r2, bcy - r2, r2 * 2, r2 * 2);
                    gfx.FillEllipse(&bGold, bcx - r1, bcy - r1, r1 * 2, r1 * 2);
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeDefault);

                    float tgtFontSz = slotSize * 0.28f;
                    Gdiplus::Font tgtFont(&fontFamily, tgtFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush tgtBrush(Gdiplus::Color(240, 10, 15, 70));
                    Gdiplus::RectF tgtRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(L"TGT", -1, &tgtFont, tgtRect, &centerFmt, &tgtBrush);
                }

                // Slot 9 (Rotation): step degrees (top, bold) | separator line | T+total (bottom)
                if (i == 9)
                {
                    int stepVal = s_overlay.rotationStep;
                    int totalVal = s_overlay.totalRotation;

                    // Top line: step value with degree symbol (bold)
                    std::wstring stepStr = std::to_wstring(stepVal) + L"\xB0";
                    float stepFontSz = slotSize * 0.28f;
                    Gdiplus::Font stepFont(&fontFamily, stepFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush stepBrush(Gdiplus::Color(220, 180, 210, 255));
                    Gdiplus::StringFormat topFmt;
                    topFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    topFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF topRect((float)sx, (float)sy + slotSize * 0.02f, (float)slotSize, (float)slotSize * 0.45f);
                    gfx.DrawString(stepStr.c_str(), -1, &stepFont, topRect, &topFmt, &stepBrush);

                    // Horizontal separator line
                    float lineY = (float)sy + slotSize * 0.48f;
                    float lineMargin = slotSize * 0.15f;
                    Gdiplus::Pen linePen(Gdiplus::Color(120, 180, 180, 200), 1.0f);
                    gfx.DrawLine(&linePen, (float)sx + lineMargin, lineY, (float)sx + slotSize - lineMargin, lineY);

                    // Bottom line: T+total (no degree symbol)
                    std::wstring totalStr = L"T" + std::to_wstring(totalVal);
                    float totalFontSz = slotSize * 0.28f;
                    Gdiplus::Font totalFont(&fontFamily, totalFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush totalBrush(Gdiplus::Color(255, 200, 230, 255));
                    Gdiplus::StringFormat botFmt;
                    botFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    botFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF botRect((float)sx, (float)sy + slotSize * 0.50f, (float)slotSize, (float)slotSize * 0.48f);
                    gfx.DrawString(totalStr.c_str(), -1, &totalFont, botRect, &botFmt, &totalBrush);
                }

                // Slot 10 (Toolbar Swap): show active toolbar number
                if (i == 10)
                {
                    int tb = s_overlay.activeToolbar;
                    std::wstring tbStr = L"T" + std::to_wstring(tb + 1);
                    float tbFontSz = slotSize * 0.35f;
                    Gdiplus::Font tbFont(&fontFamily, tbFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush tbBrush(Gdiplus::Color(220, 180, 210, 255));
                    Gdiplus::RectF tbRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(tbStr.c_str(), -1, &tbFont, tbRect, &centerFmt, &tbBrush);
                }

                // Slot 11 (Configuration): gear/cog icon + CFG text
                if (i == 11)
                {
                    float gcx = (float)(sx + slotSize / 2);
                    float gcy = (float)(sy + slotSize / 2);
                    constexpr int nTeeth = 8;
                    constexpr float kPI = 3.14159265f;
                    float tipR = slotSize * 0.40f;  // outer radius (tooth tips)
                    float rootR = slotSize * 0.28f; // inner radius (between teeth)
                    float holeR = slotSize * 0.10f; // center hole

                    // Build gear profile: each tooth = flat top, each gap = flat valley
                    float segAngle = 2.0f * kPI / nTeeth;
                    float halfTip = segAngle * 0.22f;  // half angular width of tooth top
                    float halfRoot = segAngle * 0.28f; // half angular width of root valley

                    std::vector<Gdiplus::PointF> gearPts;
                    for (int t = 0; t < nTeeth; t++)
                    {
                        float ctrAngle = t * segAngle - kPI / 2.0f;
                        float gapCtr = ctrAngle + segAngle * 0.5f;
                        // Flat tooth top (outer)
                        gearPts.push_back({gcx + tipR * cosf(ctrAngle - halfTip), gcy + tipR * sinf(ctrAngle - halfTip)});
                        gearPts.push_back({gcx + tipR * cosf(ctrAngle + halfTip), gcy + tipR * sinf(ctrAngle + halfTip)});
                        // Flat root valley (inner)
                        gearPts.push_back({gcx + rootR * cosf(gapCtr - halfRoot), gcy + rootR * sinf(gapCtr - halfRoot)});
                        gearPts.push_back({gcx + rootR * cosf(gapCtr + halfRoot), gcy + rootR * sinf(gapCtr + halfRoot)});
                    }

                    Gdiplus::GraphicsPath gearPath;
                    gearPath.AddPolygon(gearPts.data(), (int)gearPts.size());

                    Gdiplus::SolidBrush gearBrush(Gdiplus::Color(150, 150, 165, 185));
                    Gdiplus::Pen gearOutline(Gdiplus::Color(140, 100, 115, 140), 1.2f * (slotSize / 48.0f));
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    gfx.FillPath(&gearBrush, &gearPath);
                    gfx.DrawPath(&gearOutline, &gearPath);
                    // Center hole
                    Gdiplus::SolidBrush holeBrush(Gdiplus::Color(150, 25, 30, 45));
                    gfx.FillEllipse(&holeBrush, gcx - holeR, gcy - holeR, holeR * 2, holeR * 2);
                    Gdiplus::Pen holeRing(Gdiplus::Color(140, 100, 115, 140), 1.2f * (slotSize / 48.0f));
                    gfx.DrawEllipse(&holeRing, gcx - holeR, gcy - holeR, holeR * 2, holeR * 2);
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeDefault);

                    float cfgFontSz = slotSize * 0.28f;
                    Gdiplus::Font cfgFont(&fontFamily, cfgFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush cfgBrush(Gdiplus::Color(240, 10, 15, 70));
                    Gdiplus::RectF cfgRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(L"CFG", -1, &cfgFont, cfgRect, &centerFmt, &cfgBrush);
                }

                // Key label below slot — pulled from s_bindings via named constants
                // Slot mapping: 0-7=QB bindings, 8=Target, 9=Rotation, 10=Swap, 11=Config
                std::wstring fLabel;
                if (i <= 7)
                {
                    fLabel = keyName(s_bindings[i].key); // Quick Build 1-8
                }
                else if (i == 8)
                {
                    fLabel = keyName(s_bindings[BIND_TARGET].key); // Target
                }
                else if (i == 9)
                {
                    fLabel = keyName(s_bindings[BIND_ROTATION].key); // Rotation
                }
                else if (i == 10)
                {
                    fLabel = keyName(s_bindings[BIND_SWAP].key); // Toolbar Swap
                }
                else if (i == 11)
                {
                    fLabel = keyName(s_bindings[BIND_CONFIG].key); // Configuration
                }
                Gdiplus::RectF labelRect((float)sx, (float)(sy + slotSize + 1), (float)slotSize, (float)labelH);
                gfx.DrawString(fLabel.c_str(), -1, &labelFont, labelRect, &centerFmt, &labelBrush);
            }
        }

        POINT ptSrc = {0, 0};
        SIZE sz = {overlayW, overlayH};
        POINT ptDst = {overlayX, overlayY};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    static LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_TIMER:
            renderOverlay(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static DWORD WINAPI overlayThreadProc(LPVOID /*param*/)
    {
        // GDI+ already initialized in startOverlay() — ensure it's ready
        if (!s_overlay.gdipToken)
        {
            Gdiplus::GdiplusStartupInput gdipInput;
            Gdiplus::GdiplusStartup(&s_overlay.gdipToken, &gdipInput, nullptr);
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlayWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MoriaCppModOverlay";
        UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
        if (!RegisterClassExW(&wc))
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            return 1;
        }

        for (int i = 0; i < 60 && s_overlay.running; i++)
        {
            s_overlay.gameHwnd = findGameWindow();
            if (s_overlay.gameHwnd) break;
            Sleep(500);
        }
        if (!s_overlay.running || !s_overlay.gameHwnd)
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
            return 0;
        }

        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                    L"MoriaCppModOverlay",
                                    L"",
                                    WS_POPUP,
                                    0,
                                    0,
                                    1,
                                    1,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandle(nullptr),
                                    nullptr);
        if (!hwnd)
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
            return 1;
        }

        s_overlay.overlayHwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        renderOverlay(hwnd);

        SetTimer(hwnd, 1, 200, nullptr); // 5Hz refresh

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            if (!s_overlay.running) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        s_overlay.overlayHwnd = nullptr;
        Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
        UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
        return 0;
    }

    // ══════════════════════════════════════════════════════════════
    // Config Window — F12 centered panel with tabs (GDI+ overlay)
    // ══════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════════════════════
    // Section 5: Config Window System
    //   3-tab config panel (Building Options, Optional Mods, Key Mapping)
    //   GDI+ rendered, independent thread, scrollbar support
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr int CONFIG_TAB_COUNT = 3;
    static const wchar_t* CONFIG_TAB_NAMES[CONFIG_TAB_COUNT] = {L"Optional Mods", L"Key Mapping", L"Hide Environment"};

    struct ConfigState
    {
        HWND configHwnd{nullptr};
        HWND gameHwnd{nullptr};
        HANDLE thread{nullptr};
        std::atomic<bool> running{false};
        std::atomic<bool> visible{false};
        std::atomic<int> activeTab{0};
        ULONG_PTR gdipToken{0};

        // Cheat toggle states (read from debug menu actor on game thread)
        std::atomic<bool> freeBuild{false};
        std::atomic<bool> freeCraft{false};
        std::atomic<bool> instantCraft{false};

        // Pending actions (set by config UI thread, consumed by game thread in on_update)
        std::atomic<bool> pendingToggleFreeBuild{false};
        std::atomic<bool> pendingToggleFreeCraft{false};
        std::atomic<bool> pendingToggleInstantCraft{false};
        std::atomic<bool> pendingUnlockAllRecipes{false};
        std::atomic<bool> pendingCompleteTips{false};  // "Mark All Read" button
        std::atomic<bool> pendingUnlockAll{false};    // "Unlock All Content" button
        std::atomic<bool> pendingMarkTutorialsRead{false}; // "Mark Tutorials Read" button
        std::atomic<bool> suppressTutorials{false};   // when true, block tutorial/tip/lore calls in pre-hook
        std::atomic<bool> internalTutorialCall{false}; // bypass trigger suppression for our own calls
        std::atomic<bool> trackTutorials{true};       // when true, log all tutorial-related ProcessEvent calls
        std::atomic<bool> hideTutorialHUD{false};     // when true, block tutorial HUD display permanently
        std::atomic<bool> markRecipesRead{false};     // when true, auto-mark crafting recipes as read when screen opens
        void* savedTutorialTable{nullptr};            // saved before completeTutorials nulls TutorialTable

        // Scrollbar state
        int scrollY{0};
        int contentHeight{0}; // total logical content height
        int visibleHeight{0}; // visible content area height

        // Removal list (Building Options tab)
        CRITICAL_SECTION removalCS;
        std::atomic<bool> removalCSInit{false};
        std::vector<RemovalEntry> removalEntries; // display snapshot, protected by removalCS
        std::atomic<int> removalCount{0};         // quick count without lock

        // Delete button hit-test rects (written/read on config thread only)
        struct DeleteRect
        {
            int x, y, w, h; // y includes scroll offset for logicalMy comparison
            int entryIndex;
        };
        std::vector<DeleteRect> deleteRects;

        // Pending removal (set by UI thread, consumed by game thread)
        std::atomic<int> pendingRemoveIndex{-1};
    };
    static ConfigState s_config{};

    // ══════════════════════════════════════════════════════════════
    // Target Info Popup — right-side panel showing aimed actor info
    // ══════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════════════════════
    // Section 6: Target Info Popup
    //   Right-side panel showing aimed actor details (class, name, path, recipe)
    //   Copy-to-clipboard support, independent GDI+ thread
    // ════════════════════════════════════════════════════════════════════════════
#if 0 // DISABLED: Win32 GDI+ Target Info — replaced by UMG widget (createTargetInfoWidget)
    struct TargetInfoState
    {
        HWND hwnd{nullptr};
        HWND gameHwnd{nullptr};
        HANDLE thread{nullptr};
        std::atomic<bool> running{false};
        std::atomic<bool> visible{false};
        ULONG_PTR gdipToken{0};

        // Data fields (set by game thread, read by UI thread)
        std::wstring actorName;
        std::wstring displayName;
        std::wstring assetPath;
        std::wstring actorClass;
        bool buildable{false};
        std::wstring recipeRef; // BP name without _C suffix (for bLock matching)
        std::wstring rowName;   // DT_Constructions row name (e.g. "Beorn_Wall_4x3_A")
        CRITICAL_SECTION dataCS;
        std::atomic<bool> csInit{false};

        // Auto-copy and auto-close (set by game thread, consumed by UI thread)
        std::atomic<bool> pendingAutoCopy{false};

        // Hit rects (set during render, checked on click)
        Gdiplus::RectF copyBtnRect;
        Gdiplus::RectF closeBtnRect;
    };
    static TargetInfoState s_targetInfo;

    static void copyTargetInfoToClipboard(HWND hwnd)
    {
        if (!s_targetInfo.csInit) return;
        std::wstring copyText;
        EnterCriticalSection(&s_targetInfo.dataCS);
        copyText = L"Class: " + s_targetInfo.actorClass + L"\r\n" + L"Name: " + s_targetInfo.actorName + L"\r\n" + L"Display: " + s_targetInfo.displayName +
                   L"\r\n" + L"Path: " + s_targetInfo.assetPath + L"\r\n" + L"Buildable: " + (s_targetInfo.buildable ? L"Yes" : L"No");
        std::wstring recDisp = !s_targetInfo.rowName.empty() ? s_targetInfo.rowName : s_targetInfo.recipeRef;
        if (!recDisp.empty()) copyText += L"\r\nRecipe: " + recDisp;
        LeaveCriticalSection(&s_targetInfo.dataCS);

        if (OpenClipboard(hwnd))
        {
            EmptyClipboard();
            size_t sz = (copyText.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
            if (hMem)
            {
                memcpy(GlobalLock(hMem), copyText.c_str(), sz);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
            CloseClipboard();
        }
    }

    static void renderTargetInfo(HWND hwnd)
    {
        if (!s_targetInfo.csInit) return;

        // Auto-copy to clipboard + start 10-second auto-close timer
        if (s_targetInfo.pendingAutoCopy.exchange(false))
        {
            copyTargetInfoToClipboard(hwnd);
            SetTimer(hwnd, 2, 10000, nullptr); // Timer ID 2: auto-close after 10s
        }

        if (!s_targetInfo.gameHwnd || !IsWindow(s_targetInfo.gameHwnd))
        {
            s_targetInfo.gameHwnd = findGameWindow();
            if (!s_targetInfo.gameHwnd) return;
        }

        RECT clientRect;
        GetClientRect(s_targetInfo.gameHwnd, &clientRect);
        POINT origin = {0, 0};
        ClientToScreen(s_targetInfo.gameHwnd, &origin);
        int gameW = clientRect.right;
        int gameH = clientRect.bottom;
        if (gameW < 100 || gameH < 100) return;

        float scale = gameH / 1080.0f;
        if (scale < 0.5f) scale = 0.5f;

        int panelW = static_cast<int>(440 * scale);
        int margin = static_cast<int>(20 * scale);
        int pad = static_cast<int>(12 * scale);
        int lineH = static_cast<int>(20 * scale);
        float labelW = 80.0f * scale;
        float valueW = (float)(panelW - pad * 2) - labelW;

        if (!s_targetInfo.visible)
        {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            return;
        }

        // Read data under lock
        std::wstring dClass, dName, dDisplay, dPath, dRecipe, dRowName;
        bool dBuildable = false;
        EnterCriticalSection(&s_targetInfo.dataCS);
        dClass = s_targetInfo.actorClass;
        dName = s_targetInfo.actorName;
        dDisplay = s_targetInfo.displayName;
        dPath = s_targetInfo.assetPath;
        dBuildable = s_targetInfo.buildable;
        dRecipe = s_targetInfo.recipeRef;
        dRowName = s_targetInfo.rowName;
        LeaveCriticalSection(&s_targetInfo.dataCS);

        // Measure path text height for word-wrap
        Gdiplus::FontFamily fontFamily(L"Consolas");
        float valueFontSz = 11.0f * scale;
        Gdiplus::Font valueFont(&fontFamily, valueFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        // Measure wrapped path height
        Gdiplus::Bitmap measBmp(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics measGfx(&measBmp);
        Gdiplus::RectF measLayout(0, 0, valueW, 999.0f);
        Gdiplus::RectF measBound;
        measGfx.MeasureString(dPath.c_str(), -1, &valueFont, measLayout, &measBound);
        int pathH = (int)(measBound.Height + 0.5f);
        if (pathH < lineH) pathH = lineH;

        // Calculate dynamic panel height
        int headerH = lineH + static_cast<int>(4 * scale) + static_cast<int>(6 * scale) + 1; // title + gap + divider + gap
        int rowsH = lineH * 3 + pathH;                                                       // class, name, display, path(wrapped)
        int buildableH = lineH;                                                              // buildable row
        std::wstring recipeDisplay_m = !dRowName.empty() ? dRowName : dRecipe;
        int recipeH = !recipeDisplay_m.empty() ? lineH : 0;
        int panelH = headerH + rowsH + buildableH + recipeH + pad * 2;

        // Position: right side of game window, vertically centered
        int panelX = origin.x + gameW - panelW - margin;
        int panelY = origin.y + (gameH - panelH) / 2;

        // Create bitmap for layered window
        Gdiplus::Bitmap bmp(panelW, panelH, PixelFormat32bppPARGB);
        Gdiplus::Graphics gfx(&bmp);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Background with rounded corners
        int radius = static_cast<int>(10 * scale);
        Gdiplus::GraphicsPath bgPath;
        bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
        bgPath.AddArc(panelW - radius * 2 - 1, 0, radius * 2, radius * 2, 270, 90);
        bgPath.AddArc(panelW - radius * 2 - 1, panelH - radius * 2 - 1, radius * 2, radius * 2, 0, 90);
        bgPath.AddArc(0, panelH - radius * 2 - 1, radius * 2, radius * 2, 90, 90);
        bgPath.CloseFigure();
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(220, 25, 30, 45));
        gfx.FillPath(&bgBrush, &bgPath);
        Gdiplus::Pen borderPen(Gdiplus::Color(180, 80, 130, 200), 2.0f * scale);
        gfx.DrawPath(&borderPen, &bgPath);

        float titleFontSz = 14.0f * scale;
        float labelFontSz = 11.0f * scale;
        Gdiplus::Font titleFont(&fontFamily, titleFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font labelFont(&fontFamily, labelFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(240, 200, 220, 255));
        Gdiplus::SolidBrush labelBrush(Gdiplus::Color(200, 140, 180, 220));
        Gdiplus::SolidBrush valueBrush(Gdiplus::Color(230, 220, 230, 245));

        int cy = pad;

        // Header: "Target Info" + copy icon + close X
        float btnSize = 24.0f * scale;

        // Close button (red X) — rightmost
        float closeX = panelW - pad - btnSize;
        float closeY = (float)cy + (lineH - btnSize) / 2.0f;
        s_targetInfo.closeBtnRect = {closeX, closeY, btnSize, btnSize};
        // Red circle background
        Gdiplus::SolidBrush closeBg(Gdiplus::Color(200, 180, 50, 50));
        gfx.FillEllipse(&closeBg, closeX, closeY, btnSize, btnSize);
        // X lines
        Gdiplus::Pen xPen(Gdiplus::Color(240, 255, 255, 255), 2.0f * scale);
        float xp = 6.0f * scale;
        gfx.DrawLine(&xPen, closeX + xp, closeY + xp, closeX + btnSize - xp, closeY + btnSize - xp);
        gfx.DrawLine(&xPen, closeX + btnSize - xp, closeY + xp, closeX + xp, closeY + btnSize - xp);

        // Copy button — to the left of close
        float copyX = closeX - btnSize - 4 * scale;
        float copyY = closeY;
        s_targetInfo.copyBtnRect = {copyX, copyY, btnSize, btnSize};
        // Draw copy icon: small pages with lines
        Gdiplus::Pen iconPen(Gdiplus::Color(200, 180, 200, 240), 1.5f * scale);
        float ix = copyX + 3 * scale, iy = copyY + 2 * scale;
        float iw = btnSize - 6 * scale, ih = btnSize - 4 * scale;
        gfx.DrawRectangle(&iconPen, ix + 3 * scale, iy, iw - 3 * scale, ih - 3 * scale);
        Gdiplus::SolidBrush pageBrush(Gdiplus::Color(180, 40, 50, 70));
        gfx.FillRectangle(&pageBrush, ix, iy + 3 * scale, iw - 3 * scale, ih - 3 * scale);
        gfx.DrawRectangle(&iconPen, ix, iy + 3 * scale, iw - 3 * scale, ih - 3 * scale);
        float lx = ix + 3 * scale, ly = iy + 6 * scale;
        float lw = iw - 9 * scale;
        for (int l = 0; l < 3; l++)
            gfx.DrawLine(&iconPen, lx, ly + l * 4 * scale, lx + lw, ly + l * 4 * scale);

        // Title text
        Gdiplus::RectF titleRect((float)pad, (float)cy, copyX - pad - 4 * scale, (float)lineH);
        gfx.DrawString(L"Target Info", -1, &titleFont, titleRect, nullptr, &titleBrush);

        cy += lineH + static_cast<int>(4 * scale);
        Gdiplus::Pen divPen(Gdiplus::Color(100, 80, 130, 200), 1.0f);
        gfx.DrawLine(&divPen, pad, cy, panelW - pad, cy);
        cy += static_cast<int>(6 * scale);

        // Draw info rows (single-line)
        Gdiplus::StringFormat noWrap;
        noWrap.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        noWrap.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

        struct
        {
            const wchar_t* label;
            const std::wstring& value;
        } singleRows[] = {
                {L"Class:  ", dClass},
                {L"Name:   ", dName},
                {L"Display:", dDisplay},
        };
        for (auto& row : singleRows)
        {
            Gdiplus::RectF lRect((float)pad, (float)cy, labelW, (float)lineH);
            gfx.DrawString(row.label, -1, &labelFont, lRect, nullptr, &labelBrush);
            Gdiplus::RectF vRect(pad + labelW, (float)cy, valueW, (float)lineH);
            gfx.DrawString(row.value.c_str(), -1, &valueFont, vRect, &noWrap, &valueBrush);
            cy += lineH;
        }

        // Path row — word-wrapped
        Gdiplus::RectF pathLRect((float)pad, (float)cy, labelW, (float)lineH);
        gfx.DrawString(L"Path:   ", -1, &labelFont, pathLRect, nullptr, &labelBrush);
        Gdiplus::RectF pathVRect(pad + labelW, (float)cy, valueW, (float)pathH);
        gfx.DrawString(dPath.c_str(), -1, &valueFont, pathVRect, nullptr, &valueBrush);
        cy += pathH;

        // Buildable row
        Gdiplus::RectF bldLRect((float)pad, (float)cy, labelW, (float)lineH);
        gfx.DrawString(L"Build:  ", -1, &labelFont, bldLRect, nullptr, &labelBrush);
        std::wstring buildStr = dBuildable ? L"Yes" : L"No";
        Gdiplus::SolidBrush buildBrush(dBuildable ? Gdiplus::Color(230, 80, 220, 80)     // green for yes
                                                  : Gdiplus::Color(200, 180, 100, 100)); // dim for no
        Gdiplus::RectF bldVRect(pad + labelW, (float)cy, valueW, (float)lineH);
        gfx.DrawString(buildStr.c_str(), -1, &valueFont, bldVRect, &noWrap, &buildBrush);
        cy += lineH;

        // Recipe row: show DT_Constructions row name if available, else recipeRef
        std::wstring recipeDisplay = !dRowName.empty() ? dRowName : dRecipe;
        if (!recipeDisplay.empty())
        {
            Gdiplus::RectF recLRect((float)pad, (float)cy, labelW, (float)lineH);
            gfx.DrawString(L"Recipe: ", -1, &labelFont, recLRect, nullptr, &labelBrush);
            Gdiplus::RectF recVRect(pad + labelW, (float)cy, valueW, (float)lineH);
            gfx.DrawString(recipeDisplay.c_str(), -1, &valueFont, recVRect, &noWrap, &valueBrush);
            cy += lineH;
        }

        // Blit to layered window
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        HBITMAP hBmp;
        bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBmp);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

        POINT ptPos = {panelX, panelY};
        SIZE sz = {panelW, panelH};
        POINT ptSrc = {0, 0};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

        SetWindowPos(hwnd, HWND_TOPMOST, panelX, panelY, panelW, panelH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateLayeredWindow(hwnd, screenDC, &ptPos, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    static LRESULT CALLBACK targetInfoWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            // Hit-test close button (red X)
            if (mx >= s_targetInfo.closeBtnRect.X && mx <= s_targetInfo.closeBtnRect.X + s_targetInfo.closeBtnRect.Width && my >= s_targetInfo.closeBtnRect.Y &&
                my <= s_targetInfo.closeBtnRect.Y + s_targetInfo.closeBtnRect.Height)
            {
                s_targetInfo.visible = false;
                KillTimer(hwnd, 2); // cancel auto-close
                return 0;
            }
            // Hit-test copy button
            if (mx >= s_targetInfo.copyBtnRect.X && mx <= s_targetInfo.copyBtnRect.X + s_targetInfo.copyBtnRect.Width && my >= s_targetInfo.copyBtnRect.Y &&
                my <= s_targetInfo.copyBtnRect.Y + s_targetInfo.copyBtnRect.Height)
            {
                copyTargetInfoToClipboard(hwnd);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Target info copied to clipboard\n"));
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE)
            {
                s_targetInfo.visible = false;
                KillTimer(hwnd, 2); // cancel auto-close
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == 2)
            {
                // Auto-close timer fired (10 seconds after show)
                s_targetInfo.visible = false;
                KillTimer(hwnd, 2);
                return 0;
            }
            renderTargetInfo(hwnd); // Timer ID 1: render refresh
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static DWORD WINAPI targetInfoThreadProc(LPVOID)
    {
        if (!s_targetInfo.gdipToken)
        {
            Gdiplus::GdiplusStartupInput gdipInput;
            Gdiplus::GdiplusStartup(&s_targetInfo.gdipToken, &gdipInput, nullptr);
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = targetInfoWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MoriaCppModTargetInfo";
        UnregisterClassW(L"MoriaCppModTargetInfo", GetModuleHandle(nullptr));
        if (!RegisterClassExW(&wc))
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Failed to register TargetInfo window class\n"));
            return 1;
        }

        for (int i = 0; i < 60 && s_targetInfo.running; i++)
        {
            s_targetInfo.gameHwnd = findGameWindow();
            if (s_targetInfo.gameHwnd) break;
            Sleep(500);
        }

        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                    L"MoriaCppModTargetInfo",
                                    L"",
                                    WS_POPUP,
                                    0,
                                    0,
                                    1,
                                    1,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandle(nullptr),
                                    nullptr);

        if (!hwnd)
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Failed to create TargetInfo window\n"));
            return 1;
        }

        s_targetInfo.hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        renderTargetInfo(hwnd);
        SetTimer(hwnd, 1, 200, nullptr);

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            if (!s_targetInfo.running) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        s_targetInfo.hwnd = nullptr;
        Gdiplus::GdiplusShutdown(s_targetInfo.gdipToken);
        s_targetInfo.gdipToken = 0;
        UnregisterClassW(L"MoriaCppModTargetInfo", GetModuleHandle(nullptr));
        return 0;
    }
#endif // Win32 Target Info disabled

    static inline std::atomic<bool> s_pendingKeyLabelRefresh{false}; // cross-thread flag: config→game thread

#if 0 // DISABLED: Win32 Config Menu rendering — replaced by UMG
    static void renderConfig(HWND hwnd)
    {
        if (!s_config.gameHwnd || !IsWindow(s_config.gameHwnd))
        {
            s_config.gameHwnd = findGameWindow();
            if (!s_config.gameHwnd) return;
        }

        RECT clientRect;
        GetClientRect(s_config.gameHwnd, &clientRect);
        POINT origin = {0, 0};
        ClientToScreen(s_config.gameHwnd, &origin);
        int gameW = clientRect.right;
        int gameH = clientRect.bottom;
        if (gameW < 100 || gameH < 100) return;

        float scale = gameH / 1080.0f;
        if (scale < 0.5f) scale = 0.5f;

        int configW = static_cast<int>(600 * scale);
        int configH = static_cast<int>(400 * scale);
        int configX = origin.x + (gameW - configW) / 2;
        int configY = origin.y + (gameH - configH) / 2;

        if (!s_config.visible)
        {
            ShowWindow(hwnd, SW_HIDE);
            return;
        }
        if (!IsWindowVisible(hwnd))
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
        }

        // Create 32-bit ARGB bitmap
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = configW;
        bmi.bmiHeader.biHeight = -configH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp)
        {
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

        {
            Gdiplus::Graphics gfx(memDC);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            // Background with rounded corners
            int pad = static_cast<int>(8 * scale);
            int radius = static_cast<int>(10 * scale);
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(230, 15, 18, 28));
            Gdiplus::GraphicsPath bgPath;
            bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
            bgPath.AddArc(configW - radius * 2 - 1, 0, radius * 2, radius * 2, 270, 90);
            bgPath.AddArc(configW - radius * 2 - 1, configH - radius * 2 - 1, radius * 2, radius * 2, 0, 90);
            bgPath.AddArc(0, configH - radius * 2 - 1, radius * 2, radius * 2, 90, 90);
            bgPath.CloseFigure();
            gfx.FillPath(&bgBrush, &bgPath);

            // Border
            Gdiplus::Pen borderPen(Gdiplus::Color(180, 60, 80, 120), 1.5f);
            gfx.DrawPath(&borderPen, &bgPath);

            // Title
            Gdiplus::FontFamily fontFamily(L"Consolas");
            float titleSz = 16.0f * scale;
            Gdiplus::Font titleFont(&fontFamily, titleSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(240, 200, 220, 255));
            Gdiplus::StringFormat leftFmt;
            leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);
            leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF titleRect((float)pad, (float)pad, (float)(configW - pad * 2), titleSz * 1.5f);
            gfx.DrawString(L"Building Mod Configuration Menu", -1, &titleFont, titleRect, &leftFmt, &titleBrush);

            // Tab bar
            int tabY = pad + static_cast<int>(titleSz * 1.8f);
            int tabH = static_cast<int>(28 * scale);
            int tabW = static_cast<int>((configW - pad * 2) / CONFIG_TAB_COUNT);

            Gdiplus::SolidBrush activeTabBrush(Gdiplus::Color(200, 40, 60, 100));
            Gdiplus::SolidBrush inactiveTabBrush(Gdiplus::Color(100, 25, 35, 55));
            Gdiplus::Pen tabBorderPen(Gdiplus::Color(150, 50, 80, 140), 1.0f);
            float tabFontSz = 12.0f * scale;
            Gdiplus::Font tabFont(&fontFamily, tabFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush tabTextBrush(Gdiplus::Color(220, 180, 200, 240));
            Gdiplus::SolidBrush tabTextDimBrush(Gdiplus::Color(140, 120, 140, 180));
            Gdiplus::StringFormat centerFmt;
            centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            for (int t = 0; t < CONFIG_TAB_COUNT; t++)
            {
                Gdiplus::Rect tabRect(pad + t * tabW, tabY, tabW, tabH);
                if (t == s_config.activeTab)
                {
                    gfx.FillRectangle(&activeTabBrush, tabRect);
                }
                else
                {
                    gfx.FillRectangle(&inactiveTabBrush, tabRect);
                }
                gfx.DrawRectangle(&tabBorderPen, tabRect);

                Gdiplus::RectF tabTextRect((float)(pad + t * tabW), (float)tabY, (float)tabW, (float)tabH);
                gfx.DrawString(CONFIG_TAB_NAMES[t], -1, &tabFont, tabTextRect, &centerFmt, t == s_config.activeTab ? &tabTextBrush : &tabTextDimBrush);
            }

            // Content area setup with scrollbar support
            int contentY = tabY + tabH + static_cast<int>(6 * scale);
            Gdiplus::Pen sepPen(Gdiplus::Color(120, 50, 80, 140), 1.0f);
            gfx.DrawLine(&sepPen, pad, contentY, configW - pad, contentY);

            int scrollbarW = static_cast<int>(10 * scale);
            int contentBottomY = configH - pad;
            int visibleH = contentBottomY - contentY;
            s_config.visibleHeight = visibleH;

            // Clamp scroll
            int maxScroll = s_config.contentHeight - visibleH;
            if (maxScroll < 0) maxScroll = 0;
            if (s_config.scrollY > maxScroll) s_config.scrollY = maxScroll;
            if (s_config.scrollY < 0) s_config.scrollY = 0;

            // Clip content area (leave room for scrollbar)
            gfx.SetClip(Gdiplus::Rect(0, contentY + 1, configW - scrollbarW - 2, visibleH - 1));

            // Tab content
            float rowFontSz = 12.0f * scale;
            Gdiplus::Font rowFont(&fontFamily, rowFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::Font sectionFont(&fontFamily, rowFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush rowBrush(Gdiplus::Color(200, 180, 200, 240));
            Gdiplus::SolidBrush sectionBrush(Gdiplus::Color(240, 200, 220, 255));
            Gdiplus::SolidBrush keyBrush(Gdiplus::Color(220, 120, 200, 255));
            Gdiplus::SolidBrush captureBrush(Gdiplus::Color(255, 255, 180, 80));
            Gdiplus::SolidBrush dimBrush(Gdiplus::Color(100, 120, 140, 180));
            Gdiplus::Pen keyBoxPen(Gdiplus::Color(160, 80, 120, 200), 1.0f);
            Gdiplus::SolidBrush keyBoxBg(Gdiplus::Color(80, 30, 50, 90));

            int rowH = static_cast<int>(22 * scale);
            int keyBoxW = static_cast<int>(80 * scale);
            int contentStartY = contentY + static_cast<int>(10 * scale);
            int cy = contentStartY - s_config.scrollY; // apply scroll offset

            if (s_config.activeTab == 1)
            {
                // ── Key Mapping tab ──
                const wchar_t* lastSection = nullptr;
                for (int b = 0; b < BIND_COUNT; b++)
                {
                    // Section header
                    if (!lastSection || wcscmp(lastSection, s_bindings[b].section) != 0)
                    {
                        if (lastSection) cy += static_cast<int>(6 * scale); // gap between sections
                        lastSection = s_bindings[b].section;
                        Gdiplus::RectF secRect((float)pad, (float)cy, (float)(configW - pad * 2 - scrollbarW), (float)rowH);
                        gfx.DrawString(lastSection, -1, &sectionFont, secRect, &leftFmt, &sectionBrush);
                        cy += rowH;
                        gfx.DrawLine(&sepPen, pad, cy - static_cast<int>(4 * scale), configW - pad - scrollbarW, cy - static_cast<int>(4 * scale));
                    }

                    // Label
                    Gdiplus::RectF labelRect((float)(pad + static_cast<int>(12 * scale)),
                                             (float)cy,
                                             (float)(configW - pad * 2 - keyBoxW - scrollbarW - static_cast<int>(24 * scale)),
                                             (float)rowH);
                    gfx.DrawString(s_bindings[b].label, -1, &rowFont, labelRect, &leftFmt, &rowBrush);

                    // Key box
                    int kx = configW - pad - keyBoxW - scrollbarW;
                    Gdiplus::Rect keyRect(kx, cy, keyBoxW, rowH - static_cast<int>(2 * scale));
                    gfx.FillRectangle(&keyBoxBg, keyRect);
                    gfx.DrawRectangle(&keyBoxPen, keyRect);

                    Gdiplus::RectF keyTextRect((float)kx, (float)cy, (float)keyBoxW, (float)(rowH - static_cast<int>(2 * scale)));
                    if (s_capturingBind == b)
                    {
                        gfx.DrawString(L"Press key...", -1, &rowFont, keyTextRect, &centerFmt, &captureBrush);
                    }
                    else
                    {
                        std::wstring kn = keyName(s_bindings[b].key);
                        gfx.DrawString(kn.c_str(), -1, &rowFont, keyTextRect, &centerFmt, &keyBrush);
                    }

                    cy += rowH;
                }

                // Modifier key row — no extra gap, part of Misc section
                Gdiplus::RectF modLabelRect((float)(pad + static_cast<int>(12 * scale)),
                                            (float)cy,
                                            (float)(configW - pad * 2 - keyBoxW - scrollbarW - static_cast<int>(24 * scale)),
                                            (float)rowH);
                gfx.DrawString(L"Set Modifier Key", -1, &rowFont, modLabelRect, &leftFmt, &rowBrush);
                int mkx = configW - pad - keyBoxW - scrollbarW;
                Gdiplus::Rect modKeyRect(mkx, cy, keyBoxW, rowH - static_cast<int>(2 * scale));
                gfx.FillRectangle(&keyBoxBg, modKeyRect);
                gfx.DrawRectangle(&keyBoxPen, modKeyRect);
                Gdiplus::RectF modKeyTextRect((float)mkx, (float)cy, (float)keyBoxW, (float)(rowH - static_cast<int>(2 * scale)));
                gfx.DrawString(modifierName(s_modifierVK), -1, &rowFont, modKeyTextRect, &centerFmt, &keyBrush);
                cy += rowH;
            }
            else if (s_config.activeTab == 0)
            {
                // ── Optional Mods tab ──
                Gdiplus::SolidBrush toggleOnBrush(Gdiplus::Color(220, 40, 180, 80));
                Gdiplus::SolidBrush toggleOffBrush(Gdiplus::Color(150, 60, 60, 60));
                Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 240, 240, 240));
                Gdiplus::Pen toggleBorderPen(Gdiplus::Color(180, 80, 120, 200), 1.0f);
                Gdiplus::SolidBrush statusOnBrush(Gdiplus::Color(200, 80, 220, 120));
                Gdiplus::SolidBrush statusOffBrush(Gdiplus::Color(140, 140, 140, 140));

                int toggleW = static_cast<int>(44 * scale);
                int toggleH = static_cast<int>(20 * scale);
                int knobR = static_cast<int>(8 * scale);
                int toggleX = configW - pad - toggleW - scrollbarW;

                struct ToggleItem
                {
                    const wchar_t* label;
                    const wchar_t* desc;
                    bool state;
                };
                ToggleItem toggles[] = {
                        {L"Free Build", L"Build without materials", s_config.freeBuild},
                        // Free Crafting / Instant Crafting removed — game's debug flags are non-functional
                };

                // Section header
                Gdiplus::RectF secRect((float)pad, (float)cy, (float)(configW - pad * 2 - scrollbarW), (float)rowH);
                gfx.DrawString(L"Cheat Toggles", -1, &sectionFont, secRect, &leftFmt, &sectionBrush);
                cy += rowH;
                gfx.DrawLine(&sepPen, pad, cy - static_cast<int>(4 * scale), configW - pad - scrollbarW, cy - static_cast<int>(4 * scale));

                float descFontSz = 10.0f * scale;
                Gdiplus::Font descFont(&fontFamily, descFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

                for (int ti = 0; ti < 1; ti++)
                {
                    Gdiplus::RectF lblRect((float)(pad + static_cast<int>(12 * scale)),
                                           (float)cy,
                                           (float)(configW - pad * 2 - toggleW - scrollbarW - static_cast<int>(24 * scale)),
                                           (float)rowH);
                    gfx.DrawString(toggles[ti].label, -1, &rowFont, lblRect, &leftFmt, &rowBrush);

                    int ty = cy + (rowH - toggleH) / 2;
                    Gdiplus::GraphicsPath trackPath;
                    int tr = toggleH / 2;
                    trackPath.AddArc(toggleX, ty, tr * 2, toggleH - 1, 90, 180);
                    trackPath.AddArc(toggleX + toggleW - tr * 2, ty, tr * 2, toggleH - 1, 270, 180);
                    trackPath.CloseFigure();
                    gfx.FillPath(toggles[ti].state ? &toggleOnBrush : &toggleOffBrush, &trackPath);
                    gfx.DrawPath(&toggleBorderPen, &trackPath);

                    int knobX = toggles[ti].state ? (toggleX + toggleW - knobR * 2 - 3) : (toggleX + 3);
                    int knobY = ty + (toggleH - knobR * 2) / 2;
                    gfx.FillEllipse(&knobBrush, knobX, knobY, knobR * 2, knobR * 2);

                    Gdiplus::RectF statusRect((float)(toggleX - static_cast<int>(36 * scale)), (float)cy, (float)(static_cast<int>(32 * scale)), (float)rowH);
                    Gdiplus::StringFormat rightFmt;
                    rightFmt.SetAlignment(Gdiplus::StringAlignmentFar);
                    rightFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    gfx.DrawString(toggles[ti].state ? L"ON" : L"OFF", -1, &rowFont, statusRect, &rightFmt, toggles[ti].state ? &statusOnBrush : &statusOffBrush);

                    cy += rowH;

                    Gdiplus::RectF descRect((float)(pad + static_cast<int>(24 * scale)),
                                            (float)cy,
                                            (float)(configW - pad * 2 - scrollbarW - static_cast<int>(24 * scale)),
                                            (float)rowH);
                    gfx.DrawString(toggles[ti].desc, -1, &descFont, descRect, &leftFmt, &dimBrush);
                    cy += static_cast<int>(rowH * 0.8f);
                }

                // ── Unlock All Recipes button ──
                cy += static_cast<int>(12 * scale);
                gfx.DrawLine(&sepPen, pad, cy, configW - pad - scrollbarW, cy);
                cy += static_cast<int>(8 * scale);

                int btnW = static_cast<int>(200 * scale);
                int btnH = static_cast<int>(28 * scale);
                int btnX = pad + static_cast<int>(12 * scale);
                int btnY = cy;

                Gdiplus::SolidBrush btnBrush(Gdiplus::Color(200, 60, 40, 100));
                Gdiplus::Pen btnBorderPen(Gdiplus::Color(200, 100, 160, 230), 1.5f);
                Gdiplus::SolidBrush btnTextBrush(Gdiplus::Color(240, 220, 230, 255));

                Gdiplus::GraphicsPath btnPath;
                int br = static_cast<int>(6 * scale);
                btnPath.AddArc(btnX, btnY, br * 2, br * 2, 180, 90);
                btnPath.AddArc(btnX + btnW - br * 2, btnY, br * 2, br * 2, 270, 90);
                btnPath.AddArc(btnX + btnW - br * 2, btnY + btnH - br * 2, br * 2, br * 2, 0, 90);
                btnPath.AddArc(btnX, btnY + btnH - br * 2, br * 2, br * 2, 90, 90);
                btnPath.CloseFigure();
                gfx.FillPath(&btnBrush, &btnPath);
                gfx.DrawPath(&btnBorderPen, &btnPath);

                float btnFontSz = 13.0f * scale;
                Gdiplus::Font btnFont(&fontFamily, btnFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::RectF btnTextRect((float)btnX, (float)btnY, (float)btnW, (float)btnH);
                gfx.DrawString(L"Unlock All Recipes", -1, &btnFont, btnTextRect, &centerFmt, &btnTextBrush);
                cy += btnH;

#if 0 // DISABLED: Mark All Read + Complete All Tutorials buttons — breadcrumb/tutorial systems don't persist reliably
                // ── Mark All Read button ──
                cy += static_cast<int>(8 * scale);
                int btn3Y = cy;
                Gdiplus::GraphicsPath btn3Path;
                btn3Path.AddArc(btnX, btn3Y, br * 2, br * 2, 180, 90);
                btn3Path.AddArc(btnX + btnW - br * 2, btn3Y, br * 2, br * 2, 270, 90);
                btn3Path.AddArc(btnX + btnW - br * 2, btn3Y + btnH - br * 2, br * 2, br * 2, 0, 90);
                btn3Path.AddArc(btnX, btn3Y + btnH - br * 2, br * 2, br * 2, 90, 90);
                btn3Path.CloseFigure();
                gfx.FillPath(&btnBrush, &btn3Path);
                gfx.DrawPath(&btnBorderPen, &btn3Path);
                Gdiplus::RectF btn3TextRect((float)btnX, (float)btn3Y, (float)btnW, (float)btnH);
                gfx.DrawString(L"Mark All Read", -1, &btnFont, btn3TextRect, &centerFmt, &btnTextBrush);
                cy += btnH;

                // ── Mark Tutorials Read button ──
                cy += static_cast<int>(8 * scale);
                int btn4Y = cy;
                Gdiplus::GraphicsPath btn4Path;
                btn4Path.AddArc(btnX, btn4Y, br * 2, br * 2, 180, 90);
                btn4Path.AddArc(btnX + btnW - br * 2, btn4Y, br * 2, br * 2, 270, 90);
                btn4Path.AddArc(btnX + btnW - br * 2, btn4Y + btnH - br * 2, br * 2, br * 2, 0, 90);
                btn4Path.AddArc(btnX, btn4Y + btnH - br * 2, br * 2, br * 2, 90, 90);
                btn4Path.CloseFigure();
                gfx.FillPath(&btnBrush, &btn4Path);
                gfx.DrawPath(&btnBorderPen, &btn4Path);
                Gdiplus::RectF btn4TextRect((float)btnX, (float)btn4Y, (float)btnW, (float)btnH);
                gfx.DrawString(L"Complete All Tutorials", -1, &btnFont, btn4TextRect, &centerFmt, &btnTextBrush);
                cy += btnH;
#endif
            }
            else if (s_config.activeTab == 2)
            {
                // ── Hide Environment tab: Saved Removals list ──
                int entryCount = s_config.removalCount.load();
                std::wstring header = L"Saved Removals (" + std::to_wstring(entryCount) + L" entries)";
                Gdiplus::RectF secRect((float)pad, (float)cy, (float)(configW - pad * 2 - scrollbarW), (float)rowH);
                gfx.DrawString(header.c_str(), -1, &sectionFont, secRect, &leftFmt, &sectionBrush);
                cy += rowH;
                gfx.DrawLine(&sepPen, pad, cy - static_cast<int>(4 * scale), configW - pad - scrollbarW, cy - static_cast<int>(4 * scale));

                float descFontSz = 10.0f * scale;
                Gdiplus::Font descFont(&fontFamily, descFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                int smallRowH = static_cast<int>(18 * scale);
                int deleteSize = static_cast<int>(20 * scale);
                int indent = static_cast<int>(12 * scale);
                int entryGap = static_cast<int>(4 * scale);

                Gdiplus::SolidBrush nameBrush(Gdiplus::Color(240, 230, 230, 240));
                Gdiplus::SolidBrush pathBrush(Gdiplus::Color(140, 150, 160, 180));
                Gdiplus::SolidBrush coordBrush(Gdiplus::Color(180, 120, 180, 220));
                Gdiplus::SolidBrush typeRuleBrush(Gdiplus::Color(200, 255, 140, 80));
                Gdiplus::SolidBrush deleteBtnBrush(Gdiplus::Color(200, 180, 40, 40));
                Gdiplus::SolidBrush deleteXBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::Pen deleteBorderPen(Gdiplus::Color(200, 220, 80, 80), 1.0f);
                Gdiplus::Pen entrySepPen(Gdiplus::Color(60, 80, 100, 140), 0.5f);

                Gdiplus::StringFormat ellipsisFmt;
                ellipsisFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                ellipsisFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                ellipsisFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                ellipsisFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

                // Take local copy of entries under CS to minimize lock time
                std::vector<RemovalEntry> localEntries;
                if (s_config.removalCSInit)
                {
                    EnterCriticalSection(&s_config.removalCS);
                    localEntries = s_config.removalEntries;
                    LeaveCriticalSection(&s_config.removalCS);
                }

                s_config.deleteRects.clear();

                if (localEntries.empty())
                {
                    Gdiplus::RectF emptyRect((float)(pad + indent), (float)cy, (float)(configW - pad * 2 - scrollbarW), (float)rowH);
                    gfx.DrawString(L"No removed instances.", -1, &rowFont, emptyRect, &leftFmt, &dimBrush);
                    cy += rowH;
                }
                else
                {
                    int contentRight = configW - pad - scrollbarW;
                    for (size_t i = 0; i < localEntries.size(); i++)
                    {
                        auto& e = localEntries[i];
                        int textRight = contentRight - deleteSize - static_cast<int>(8 * scale);

                        // Line 1: Friendly name (bold)
                        Gdiplus::RectF nameRect((float)(pad + indent), (float)cy, (float)(textRight - pad - indent), (float)rowH);
                        gfx.DrawString(e.friendlyName.c_str(), -1, &sectionFont, nameRect, &ellipsisFmt, &nameBrush);

                        // Delete button (vertically centered across the 3-line entry)
                        int entryTotalH = rowH + smallRowH * 2;
                        int delX = contentRight - deleteSize;
                        int delY = cy + (entryTotalH - deleteSize) / 2;
                        Gdiplus::Rect delRect(delX, delY, deleteSize, deleteSize);
                        gfx.FillRectangle(&deleteBtnBrush, delRect);
                        gfx.DrawRectangle(&deleteBorderPen, delRect);
                        float xFontSz = 12.0f * scale;
                        Gdiplus::Font xFont(&fontFamily, xFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                        Gdiplus::RectF xRect((float)delX, (float)delY, (float)deleteSize, (float)deleteSize);
                        gfx.DrawString(L"X", -1, &xFont, xRect, &centerFmt, &deleteXBrush);

                        // Store hit rect (y with scroll offset for logicalMy comparison in click handler)
                        s_config.deleteRects.push_back({delX, delY + s_config.scrollY, deleteSize, deleteSize, (int)i});

                        cy += rowH;

                        // Line 2: Full mesh path (smaller, gray, with ellipsis)
                        Gdiplus::RectF pathRect((float)(pad + indent * 2), (float)cy, (float)(textRight - pad - indent * 2), (float)smallRowH);
                        gfx.DrawString(e.fullPathW.c_str(), -1, &descFont, pathRect, &ellipsisFmt, &pathBrush);
                        cy += smallRowH;

                        // Line 3: Coordinates or TYPE RULE
                        Gdiplus::RectF coordRect((float)(pad + indent * 2), (float)cy, (float)(textRight - pad - indent * 2), (float)smallRowH);
                        gfx.DrawString(e.coordsW.c_str(), -1, &descFont, coordRect, &ellipsisFmt, e.isTypeRule ? &typeRuleBrush : &coordBrush);
                        cy += smallRowH;

                        // Separator between entries
                        cy += entryGap;
                        gfx.DrawLine(&entrySepPen, pad + indent, cy, contentRight, cy);
                        cy += entryGap;
                    }
                }
            }

            // Calculate total content height for scrollbar
            int totalContentH = (cy + s_config.scrollY) - contentStartY + static_cast<int>(10 * scale);
            s_config.contentHeight = totalContentH;

            // Reset clip for scrollbar drawing
            gfx.ResetClip();

            // ── Scrollbar ──
            int sbX = configW - scrollbarW - 1;
            int sbY = contentY + 1;
            int sbH = visibleH - 2;

            // Track background
            Gdiplus::SolidBrush sbTrackBrush(Gdiplus::Color(60, 40, 50, 80));
            gfx.FillRectangle(&sbTrackBrush, sbX, sbY, scrollbarW, sbH);

            if (totalContentH > visibleH)
            {
                // Thumb
                float thumbRatio = (float)visibleH / (float)totalContentH;
                int thumbH = (int)(sbH * thumbRatio);
                if (thumbH < static_cast<int>(20 * scale)) thumbH = static_cast<int>(20 * scale);
                int thumbMaxTravel = sbH - thumbH;
                int thumbY = sbY + (maxScroll > 0 ? (int)((float)s_config.scrollY / maxScroll * thumbMaxTravel) : 0);

                Gdiplus::SolidBrush sbThumbBrush(Gdiplus::Color(180, 80, 110, 170));
                Gdiplus::GraphicsPath thumbPath;
                int tr = scrollbarW / 2;
                thumbPath.AddArc(sbX, thumbY, tr * 2, tr * 2, 180, 90);
                thumbPath.AddArc(sbX + scrollbarW - tr * 2, thumbY, tr * 2, tr * 2, 270, 90);
                thumbPath.AddArc(sbX + scrollbarW - tr * 2, thumbY + thumbH - tr * 2, tr * 2, tr * 2, 0, 90);
                thumbPath.AddArc(sbX, thumbY + thumbH - tr * 2, tr * 2, tr * 2, 90, 90);
                thumbPath.CloseFigure();
                gfx.FillPath(&sbThumbBrush, &thumbPath);
            }
        }

        POINT ptSrc = {0, 0};
        SIZE sz = {configW, configH};
        POINT ptDst = {configX, configY};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    static inline std::atomic<bool> s_pendingKeyLabelRefresh{false}; // cross-thread flag: config→game thread

    static LRESULT CALLBACK configWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN: {
            // Activate window for keyboard input
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);

            if (!s_config.gameHwnd) break;
            RECT clientRect;
            GetClientRect(s_config.gameHwnd, &clientRect);
            int gameH = clientRect.bottom;
            float scale = gameH / 1080.0f;
            if (scale < 0.5f) scale = 0.5f;

            int configW = static_cast<int>(600 * scale);
            int pad = static_cast<int>(8 * scale);
            float titleSz = 16.0f * scale;
            int tabY = pad + static_cast<int>(titleSz * 1.8f);
            int tabH = static_cast<int>(28 * scale);
            int tabW = static_cast<int>((configW - pad * 2) / CONFIG_TAB_COUNT);

            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);

            // Scroll offset for hit testing — convert visual Y to logical Y
            int scrollbarW = static_cast<int>(10 * scale);
            int contentY = tabY + tabH + static_cast<int>(6 * scale);
            int logicalMy = my + s_config.scrollY; // adjust for scroll

            // Hit-test tabs (not scrolled)
            if (my >= tabY && my <= tabY + tabH)
            {
                int tabIdx = (mx - pad) / tabW;
                if (tabIdx >= 0 && tabIdx < CONFIG_TAB_COUNT)
                {
                    s_config.activeTab = tabIdx;
                    s_config.scrollY = 0; // reset scroll on tab change
                    s_capturingBind = -1;
                    renderConfig(hwnd);
                }
                return 0;
            }

            // Hit-test key boxes on Key Mapping tab (index 1)
            if (s_config.activeTab == 1 && my > contentY)
            {
                int rowH = static_cast<int>(22 * scale);
                int keyBoxW = static_cast<int>(80 * scale);
                int kx = configW - pad - keyBoxW - scrollbarW;

                int cy = contentY + static_cast<int>(10 * scale);
                const wchar_t* lastSection = nullptr;
                for (int b = 0; b < BIND_COUNT; b++)
                {
                    if (!lastSection || wcscmp(lastSection, s_bindings[b].section) != 0)
                    {
                        if (lastSection) cy += static_cast<int>(6 * scale);
                        lastSection = s_bindings[b].section;
                        cy += rowH;
                    }
                    if (mx >= kx && mx <= kx + keyBoxW && logicalMy >= cy && logicalMy < cy + rowH)
                    {
                        s_capturingBind = b;
                        renderConfig(hwnd);
                        return 0;
                    }
                    cy += rowH;
                }
                // Hit-test modifier key box (immediately after last binding row)
                if (mx >= kx && mx <= kx + keyBoxW && logicalMy >= cy && logicalMy < cy + rowH)
                {
                    s_capturingBind = -1;
                    s_modifierVK = nextModifier(s_modifierVK);
                    renderConfig(hwnd);
                    s_overlay.needsUpdate = true;
                    s_pendingKeyLabelRefresh = true;
                    // Persist to disk
                    {
                        std::ofstream kf("Mods/MoriaCppMod/keybindings.txt", std::ios::trunc);
                        if (kf.is_open())
                        {
                            kf << "# MoriaCppMod keybindings (index|VK_code)\n";
                            for (int bi = 0; bi < BIND_COUNT; bi++)
                                kf << bi << "|" << (int)s_bindings[bi].key << "\n";
                            kf << "mod|" << (int)s_modifierVK.load() << "\n";
                        }
                    }
                    return 0;
                }
                s_capturingBind = -1;
                renderConfig(hwnd);
            }

            // Hit-test toggles and button on Optional Mods tab (index 0)
            if (s_config.activeTab == 0 && my > contentY)
            {
                int rowH = static_cast<int>(22 * scale);
                int toggleW = static_cast<int>(44 * scale);
                int toggleH = static_cast<int>(20 * scale);
                int toggleX = configW - pad - toggleW - scrollbarW;

                int cy = contentY + static_cast<int>(10 * scale);
                cy += rowH; // section header

                for (int ti = 0; ti < 1; ti++)
                {
                    int toggleY = cy + (rowH - toggleH) / 2;
                    if (mx >= toggleX && mx <= toggleX + toggleW && logicalMy >= toggleY && logicalMy <= toggleY + toggleH)
                    {
                        if (ti == 0)
                            s_config.pendingToggleFreeBuild = true;
                        renderConfig(hwnd);
                        return 0;
                    }
                    cy += rowH;
                    cy += static_cast<int>(rowH * 0.8f);
                }

                cy += static_cast<int>(12 * scale);
                cy += static_cast<int>(8 * scale);
                int btnW = static_cast<int>(200 * scale);
                int btnH = static_cast<int>(28 * scale);
                int btnX = pad + static_cast<int>(12 * scale);
                if (mx >= btnX && mx <= btnX + btnW && logicalMy >= cy && logicalMy <= cy + btnH)
                {
                    s_config.pendingUnlockAllRecipes = true;
                    renderConfig(hwnd);
                    return 0;
                }
                cy += btnH;
#if 0 // DISABLED: click handlers for Mark All Read + Complete All Tutorials
                // Mark All Read button
                cy += static_cast<int>(8 * scale);
                if (mx >= btnX && mx <= btnX + btnW && logicalMy >= cy && logicalMy <= cy + btnH)
                {
                    s_config.pendingCompleteTips = true;
                    renderConfig(hwnd);
                    return 0;
                }
                cy += btnH;
                // Mark Tutorials Read button
                cy += static_cast<int>(8 * scale);
                if (mx >= btnX && mx <= btnX + btnW && logicalMy >= cy && logicalMy <= cy + btnH)
                {
                    s_config.pendingMarkTutorialsRead = true;
                    renderConfig(hwnd);
                    return 0;
                }
#endif
            }

            // Hit-test delete buttons on Hide Environment tab (index 2)
            if (s_config.activeTab == 2 && my > contentY)
            {
                for (auto& dr : s_config.deleteRects)
                {
                    if (mx >= dr.x && mx <= dr.x + dr.w && logicalMy >= dr.y && logicalMy <= dr.y + dr.h)
                    {
                        int expected = -1;
                        s_config.pendingRemoveIndex.compare_exchange_strong(expected, dr.entryIndex);
                        renderConfig(hwnd);
                        return 0;
                    }
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            uint8_t vk = static_cast<uint8_t>(wp);

            // Escape: cancel capture if active, otherwise close config
            if (vk == VK_ESCAPE)
            {
                if (s_capturingBind >= 0)
                {
                    s_capturingBind = -1;
                    renderConfig(hwnd);
                }
                else
                {
                    s_config.visible = false;
                    renderConfig(hwnd);
                }
                return 0;
            }

            // F12: close config
            if (vk == VK_F12)
            {
                s_config.visible = false;
                renderConfig(hwnd);
                return 0;
            }

            // Capture key for rebinding
            if (s_capturingBind >= 0 && s_capturingBind < BIND_COUNT)
            {
                // Ignore modifier keys alone
                if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                    vk == VK_LMENU || vk == VK_RMENU)
                    return 0;
                // Update binding
                s_bindings[s_capturingBind].key = vk;
                s_capturingBind = -1;
                renderConfig(hwnd);
                // Update overlay labels
                s_overlay.needsUpdate = true;
                s_pendingKeyLabelRefresh = true;
                // Persist to disk
                {
                    std::ofstream kf("Mods/MoriaCppMod/keybindings.txt", std::ios::trunc);
                    if (kf.is_open())
                    {
                        kf << "# MoriaCppMod keybindings (index|VK_code)\n";
                        for (int bi = 0; bi < BIND_COUNT; bi++)
                            kf << bi << "|" << (int)s_bindings[bi].key << "\n";
                        kf << "mod|" << (int)s_modifierVK.load() << "\n";
                    }
                }
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            // Modal: reclaim focus when config is visible
            if (s_config.visible && hwnd == s_config.configHwnd)
            {
                SetTimer(hwnd, 2, 100, nullptr); // brief delay to avoid focus fight
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            int scrollStep = 40; // pixels per notch
            s_config.scrollY -= (delta / WHEEL_DELTA) * scrollStep;
            int maxScroll = s_config.contentHeight - s_config.visibleHeight;
            if (maxScroll < 0) maxScroll = 0;
            if (s_config.scrollY < 0) s_config.scrollY = 0;
            if (s_config.scrollY > maxScroll) s_config.scrollY = maxScroll;
            renderConfig(hwnd);
            return 0;
        }
        case WM_TIMER:
            if (wp == 2)
            {
                // Focus reclaim timer — modal behavior
                KillTimer(hwnd, 2);
                if (s_config.visible)
                {
                    SetForegroundWindow(hwnd);
                    SetFocus(hwnd);
                }
                return 0;
            }
            renderConfig(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
#endif // Win32 Config Menu rendering disabled

#if 0 // DISABLED: Win32 Config thread — replaced by UMG
    static DWORD WINAPI configThreadProc(LPVOID)
    {
        // Initialize GDI+ for this thread
        if (!s_config.gdipToken)
        {
            Gdiplus::GdiplusStartupInput gdipInput;
            Gdiplus::GdiplusStartup(&s_config.gdipToken, &gdipInput, nullptr);
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = configWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MoriaCppModConfig";
        UnregisterClassW(L"MoriaCppModConfig", GetModuleHandle(nullptr));
        if (!RegisterClassExW(&wc))
        {
            Gdiplus::GdiplusShutdown(s_config.gdipToken);
            s_config.gdipToken = 0;
            return 1;
        }

        // Wait for game window
        for (int i = 0; i < 60 && s_config.running; i++)
        {
            s_config.gameHwnd = findGameWindow();
            if (s_config.gameHwnd) break;
            Sleep(500);
        }
        if (!s_config.running || !s_config.gameHwnd)
        {
            Gdiplus::GdiplusShutdown(s_config.gdipToken);
            s_config.gdipToken = 0;
            UnregisterClassW(L"MoriaCppModConfig", GetModuleHandle(nullptr));
            return 0;
        }

        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                    L"MoriaCppModConfig",
                                    L"",
                                    WS_POPUP,
                                    0,
                                    0,
                                    1,
                                    1,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandle(nullptr),
                                    nullptr);
        if (!hwnd)
        {
            Gdiplus::GdiplusShutdown(s_config.gdipToken);
            s_config.gdipToken = 0;
            UnregisterClassW(L"MoriaCppModConfig", GetModuleHandle(nullptr));
            return 1;
        }

        s_config.configHwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        renderConfig(hwnd);
        SetTimer(hwnd, 1, 200, nullptr); // 5Hz refresh

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            if (!s_config.running) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        s_config.configHwnd = nullptr;
        Gdiplus::GdiplusShutdown(s_config.gdipToken);
        s_config.gdipToken = 0;
        UnregisterClassW(L"MoriaCppModConfig", GetModuleHandle(nullptr));
        return 0;
    }
#endif // Win32 Config thread disabled

    // ════════════════════════════════════════════════════════════════════════════
    // Section 7: MoriaCppMod Class — Main Mod Implementation
    //   All game logic: HISM removal, inventory, quick-build, toolbar swap,
    //   icon extraction, rotation control, debug cheats, keybinds, hooks
    // ════════════════════════════════════════════════════════════════════════════
    class MoriaCppMod : public RC::CppUserModBase
    {
      private:
        std::vector<RemovedInstance> m_undoStack;
        std::vector<SavedRemoval> m_savedRemovals;
        std::set<std::string> m_typeRemovals; // mesh IDs to remove ALL of (Num6)
        std::set<UObject*> m_processedComps;
        int m_frameCounter{0};
        bool m_replayActive{false};
        bool m_characterLoaded{false};
        int m_charLoadFrame{0}; // frame when character was first detected
        bool m_initialReplayDone{false};
        int m_stuckLogCount{0}; // only log stuck entries once
        std::string m_saveFilePath;
        PSOffsets m_ps;
        UObject* m_chatWidget{nullptr};
        UObject* m_sysMessages{nullptr};
        std::vector<bool> m_appliedRemovals; // parallel to m_savedRemovals: true = already removed
        int m_rescanCounter{0};              // frames since last full rescan

        // Throttled replay: spread UpdateInstanceTransform across frames to avoid
        // crashing the render thread (FStaticMeshInstanceBuffer::UpdateFromCommandBuffer_Concurrent)
        struct ReplayState
        {
            std::vector<UObject*> compQueue;
            size_t compIdx{0};
            int instanceIdx{0}; // resume position within current component
            bool active{false};
            int totalHidden{0};
        };
        ReplayState m_replay;
        static constexpr int MAX_HIDES_PER_FRAME = 3; // conservative limit

        // ── Tracking helpers ──

        bool hasPendingRemovals() const
        {
            for (size_t i = 0; i < m_appliedRemovals.size(); i++)
            {
                if (!m_appliedRemovals[i]) return true;
            }
            return false;
        }

        int pendingCount() const
        {
            int n = 0;
            for (size_t i = 0; i < m_appliedRemovals.size(); i++)
            {
                if (!m_appliedRemovals[i]) n++;
            }
            return n;
        }

        // ── File I/O ──

        // ── 7A: File I/O & Persistence ────────────────────────────────────────
        // Save/load HISM removal data (removed_instances.txt)
        // Format: meshName|posX|posY|posZ (single instance) or @meshName (type rule)

        // Strips numeric suffix from UE4 component name to get stable mesh ID.
        // e.g. "PWM_Quarry_2x2x2_A-..._2147476295" → "PWM_Quarry_2x2x2_A-..."
        static std::string componentNameToMeshId(const std::wstring& name)
        {
            std::string narrow;
            narrow.reserve(name.size());
            for (wchar_t c : name)
                narrow.push_back(static_cast<char>(c));
            auto lastUnderscore = narrow.rfind('_');
            if (lastUnderscore != std::string::npos)
            {
                bool allDigits = true;
                for (size_t i = lastUnderscore + 1; i < narrow.size(); i++)
                {
                    if (!std::isdigit(narrow[i]))
                    {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits && lastUnderscore > 0)
                {
                    return narrow.substr(0, lastUnderscore);
                }
            }
            return narrow;
        }

        void loadSaveFile()
        {
            m_savedRemovals.clear();
            m_typeRemovals.clear();
            std::ifstream file(m_saveFilePath);
            if (!file.is_open())
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No save file found (first run)\n"));
                return;
            }
            std::string line;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;
                // @meshName = type rule (remove ALL of this mesh)
                if (line[0] == '@')
                {
                    m_typeRemovals.insert(line.substr(1));
                    continue;
                }
                std::istringstream ss(line);
                SavedRemoval sr;
                std::string token;
                if (!std::getline(ss, sr.meshName, '|')) continue;
                if (!std::getline(ss, token, '|')) continue;
                try
                {
                    sr.posX = std::stof(token);
                    if (!std::getline(ss, token, '|')) continue;
                    sr.posY = std::stof(token);
                    if (!std::getline(ss, token, '|')) continue;
                    sr.posZ = std::stof(token);
                }
                catch (...)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Skipping malformed save line: {}\n"), std::wstring(line.begin(), line.end()));
                    continue;
                }
                m_savedRemovals.push_back(sr);
            }

            // Remove position entries that are redundant with type rules
            {
                size_t before = m_savedRemovals.size();
                std::erase_if(m_savedRemovals, [this](const SavedRemoval& sr) {
                    return m_typeRemovals.count(sr.meshName) > 0;
                });
                size_t redundant = before - m_savedRemovals.size();
                if (redundant > 0)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Removed {} position entries redundant with type rules\n"), redundant);
                }
            }

            // No dedup — stacked instances share the same position,
            // and each entry matches a different stacked instance on replay

            // Initialize tracking: all pending (not yet applied)
            m_appliedRemovals.assign(m_savedRemovals.size(), false);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Loaded {} position removals + {} type rules\n"), m_savedRemovals.size(), m_typeRemovals.size());
        }

        void appendToSaveFile(const SavedRemoval& sr)
        {
            std::ofstream file(m_saveFilePath, std::ios::app);
            if (!file.is_open()) return;
            file << sr.meshName << "|" << sr.posX << "|" << sr.posY << "|" << sr.posZ << "\n";
        }

        void rewriteSaveFile()
        {
            std::ofstream file(m_saveFilePath, std::ios::trunc);
            if (!file.is_open()) return;
            file << "# MoriaCppMod removed instances\n";
            file << "# meshName|posX|posY|posZ = single instance\n";
            file << "# @meshName = remove ALL of this type\n";
            for (auto& type : m_typeRemovals)
                file << "@" << type << "\n";
            for (auto& sr : m_savedRemovals)
                file << sr.meshName << "|" << sr.posX << "|" << sr.posY << "|" << sr.posZ << "\n";
        }

        // Re-read removed_instances.txt in file order and build display entries for config UI.
        // Called after every loadSaveFile() and rewriteSaveFile() that changes the list.
        void buildRemovalEntries()
        {
            std::vector<RemovalEntry> entries;
            std::ifstream file(m_saveFilePath);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    if (line.empty() || line[0] == '#') continue;
                    RemovalEntry entry{};
                    if (line[0] == '@')
                    {
                        entry.isTypeRule = true;
                        entry.meshName = line.substr(1);
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        entry.coordsW = L"TYPE RULE (all instances)";
                    }
                    else
                    {
                        entry.isTypeRule = false;
                        std::istringstream ss(line);
                        std::string token;
                        if (!std::getline(ss, entry.meshName, '|')) continue;
                        try
                        {
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posX = std::stof(token);
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posY = std::stof(token);
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posZ = std::stof(token);
                        }
                        catch (...)
                        {
                            continue;
                        }
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        entry.coordsW = std::format(L"X: {:.1f}   Y: {:.1f}   Z: {:.1f}", entry.posX, entry.posY, entry.posZ);
                    }
                    entries.push_back(std::move(entry));
                }
            }
            if (s_config.removalCSInit)
            {
                EnterCriticalSection(&s_config.removalCS);
                s_config.removalEntries = std::move(entries);
                s_config.removalCount = static_cast<int>(s_config.removalEntries.size());
                LeaveCriticalSection(&s_config.removalCS);
            }
        }

        // ── Helpers ──

        // ── 7B: Player & World Helpers ─────────────────────────────────────────
        // Find player controller, pawn, location, camera ray

        // Returns the first PlayerController found via FindAllOf.
        UObject* findPlayerController()
        {
            std::vector<UObject*> pcs;
            UObjectGlobals::FindAllOf(STR("PlayerController"), pcs);
            return pcs.empty() ? nullptr : pcs[0];
        }

        UObject* getPawn()
        {
            auto* pc = findPlayerController();
            if (!pc) return nullptr;
            auto* fn = pc->GetFunctionByNameInChain(STR("K2_GetPawn"));
            if (!fn) return nullptr;
            struct
            {
                UObject* Ret{nullptr};
            } p{};
            pc->ProcessEvent(fn, &p);
            return p.Ret;
        }

        FVec3f getPawnLocation()
        {
            FVec3f loc{0, 0, 0};
            auto* pawn = getPawn();
            if (!pawn) return loc;
            auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
            if (!fn) return loc;
            pawn->ProcessEvent(fn, &loc);
            return loc;
        }

        // ── PrintString support ──

        // ── 7C: Display & UI Helpers ──────────────────────────────────────────
        // PrintString, on-screen text, chat widget, system messages

        // Discovers KismetSystemLibrary::PrintString param offsets at runtime.
        // Uses ForEachProperty() to locate params by name — safe across game updates.
        void probePrintString()
        {
            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:PrintString"));
            if (!fn)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] PrintString NOT FOUND\n"));
                return;
            }
            m_ps.parmsSize = fn->GetParmsSize();
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] PrintString ParmsSize={}\n"), m_ps.parmsSize);

            for (auto* prop : fn->ForEachProperty())
            {
                auto name = prop->GetName();
                int offset = prop->GetOffset_Internal();
                int size = prop->GetSize();
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   PS: {} @{} size={}\n"), name, offset, size);

                if (name == STR("WorldContextObject"))
                    m_ps.worldContext = offset;
                else if (name == STR("inString"))
                    m_ps.inString = offset;
                else if (name == STR("bPrintToScreen"))
                    m_ps.printToScreen = offset;
                else if (name == STR("bPrintToLog"))
                    m_ps.printToLog = offset;
                else if (name == STR("TextColor"))
                    m_ps.textColor = offset;
                else if (name == STR("Duration"))
                    m_ps.duration = offset;
            }

            m_ps.valid = (m_ps.worldContext >= 0 && m_ps.inString >= 0 && m_ps.printToScreen >= 0 && m_ps.duration >= 0);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] PrintString valid={}\n"), m_ps.valid);
        }

        // Displays text on screen via KismetSystemLibrary::PrintString.
        // Requires probePrintString() to have discovered param offsets first.
        // Color is RGB (0.0-1.0), duration in seconds.
        void showOnScreen(const std::wstring& text, float duration = 5.0f, float r = 0.0f, float g = 1.0f, float b = 0.5f)
        {
            if (!m_ps.valid) return;

            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:PrintString"));
            auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!fn || !cdo || !pc) return;

            std::vector<uint8_t> buf(m_ps.parmsSize, 0);

            // WorldContextObject
            std::memcpy(buf.data() + m_ps.worldContext, &pc, 8);

            // FString in param buffer: Data ptr (8) + ArrayNum (4) + ArrayMax (4)
            const wchar_t* textPtr = text.c_str();
            int32_t len = static_cast<int32_t>(text.size() + 1);
            uintptr_t ptrVal = reinterpret_cast<uintptr_t>(textPtr);
            std::memcpy(buf.data() + m_ps.inString, &ptrVal, 8);
            std::memcpy(buf.data() + m_ps.inString + 8, &len, 4);
            std::memcpy(buf.data() + m_ps.inString + 12, &len, 4);

            // bPrintToScreen = true
            buf[m_ps.printToScreen] = 1;

            // bPrintToLog = false (don't spam log)
            if (m_ps.printToLog >= 0) buf[m_ps.printToLog] = 0;

            // TextColor (FLinearColor: R, G, B, A)
            if (m_ps.textColor >= 0)
            {
                float color[4] = {r, g, b, 1.0f};
                std::memcpy(buf.data() + m_ps.textColor, color, 16);
            }

            // Duration
            std::memcpy(buf.data() + m_ps.duration, &duration, 4);

            cdo->ProcessEvent(fn, buf.data());
        }

        // ── Chat/Widget display ──

        void findWidgets()
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            m_chatWidget = nullptr;
            m_sysMessages = nullptr;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring clsName = safeClassName(w);
                if (clsName.empty()) continue;
                if (clsName == STR("WBP_UI_ChatWidget_C") && !m_chatWidget) m_chatWidget = w;
                if (clsName == STR("WBP_UI_Console_SystemMessages_C") && !m_sysMessages) m_sysMessages = w;
            }
        }

        void showGameMessage(const std::wstring& text)
        {
            if (!m_chatWidget) findWidgets();
            if (!m_chatWidget) return;

            // AddToShortChat(FText) — the floating chat overlay
            auto* func = m_chatWidget->GetFunctionByNameInChain(STR("AddToShortChat"));
            if (!func)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] AddToShortChat not found\n"));
                return;
            }

            FText ftext(text.c_str());
            uint8_t buf[sizeof(FText) + 16]{};
            std::memcpy(buf, &ftext, sizeof(FText));
            m_chatWidget->ProcessEvent(func, buf);
        }

        // ── Test all display methods (Num5) — DISABLED: keybind removed ──
#if 0
        void testAllDisplayMethods()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === Testing display methods ===\n"));
            findWidgets();

            if (m_chatWidget)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ChatWidget found\n"));

                // Test 1: AddToShortChat
                auto* f1 = m_chatWidget->GetFunctionByNameInChain(STR("AddToShortChat"));
                if (f1)
                {
                    FText t1(STR("[Mod] Test 1: AddToShortChat"));
                    uint8_t buf[sizeof(FText) + 16]{};
                    std::memcpy(buf, &t1, sizeof(FText));
                    m_chatWidget->ProcessEvent(f1, buf);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called AddToShortChat\n"));
                }

                // Test 2: SystemMessageEvent
                auto* f2 = m_chatWidget->GetFunctionByNameInChain(STR("SystemMessageEvent"));
                if (f2)
                {
                    FText t2(STR("[Mod] Test 2: SystemMessageEvent"));
                    uint8_t buf[sizeof(FText) + 16]{};
                    std::memcpy(buf, &t2, sizeof(FText));
                    m_chatWidget->ProcessEvent(f2, buf);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called SystemMessageEvent\n"));
                }

                // Test 3: AddFormattedMessage
                auto* f3 = m_chatWidget->GetFunctionByNameInChain(STR("AddFormattedMessage"));
                if (f3)
                {
                    FText t3(STR("[Mod] Test 3: AddFormattedMessage"));
                    uint8_t buf[sizeof(FText) + 16]{};
                    std::memcpy(buf, &t3, sizeof(FText));
                    m_chatWidget->ProcessEvent(f3, buf);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called AddFormattedMessage\n"));
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ChatWidget NOT found\n"));
            }

            if (m_sysMessages)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] SystemMessages found\n"));

                // Test 4: AppendMessage
                auto* f4 = m_sysMessages->GetFunctionByNameInChain(STR("AppendMessage"));
                if (f4)
                {
                    FText t4(STR("[Mod] Test 4: AppendMessage"));
                    uint8_t buf[sizeof(FText) + 16]{};
                    std::memcpy(buf, &t4, sizeof(FText));
                    m_sysMessages->ProcessEvent(f4, buf);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called AppendMessage\n"));
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] SystemMessages NOT found\n"));
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === Display test done ===\n"));
        }
#endif

        // ── Camera & Trace ──

        // ── 7D: HISM Removal System ──────────────────────────────────────────
        // Line trace, instance hiding (UpdateInstanceTransform), undo, replay
        // CRITICAL: Max 3 hides/frame to avoid render thread crash

        // Constructs camera ray from viewport center, starting past the player
        // character to avoid hitting objects between camera and pawn.
        // Uses DeprojectScreenPositionToWorld → pawn distance → offset start.
        bool getCameraRay(FVec3f& outStart, FVec3f& outEnd)
        {
            auto* pc = findPlayerController();
            if (!pc) return false;

            // Get viewport size
            auto* vpFunc = pc->GetFunctionByNameInChain(STR("GetViewportSize"));
            if (!vpFunc) return false;
            struct
            {
                int32_t SizeX{0}, SizeY{0};
            } vpParams{};
            pc->ProcessEvent(vpFunc, &vpParams);
            float centerX = vpParams.SizeX / 2.0f;
            float centerY = vpParams.SizeY / 2.0f;

            // Deproject screen center to world ray
            auto* deprojFunc = pc->GetFunctionByNameInChain(STR("DeprojectScreenPositionToWorld"));
            if (!deprojFunc) return false;
            int parmsSize = deprojFunc->GetParmsSize();
            std::vector<uint8_t> buf(parmsSize, 0);
            std::memcpy(buf.data() + 0, &centerX, 4);
            std::memcpy(buf.data() + 4, &centerY, 4);
            pc->ProcessEvent(deprojFunc, buf.data());

            FVec3f cameraLoc{}, worldDir{};
            std::memcpy(&cameraLoc, buf.data() + 8, 12);
            std::memcpy(&worldDir, buf.data() + 20, 12);

            // 3rd-person fix: start trace PAST the character to avoid hitting
            // objects between the camera and the player (the "behind me" problem)
            FVec3f pawnLoc = getPawnLocation();
            float dx = pawnLoc.X - cameraLoc.X;
            float dy = pawnLoc.Y - cameraLoc.Y;
            float dz = pawnLoc.Z - cameraLoc.Z;
            float camToChar = std::sqrt(dx * dx + dy * dy + dz * dz);
            float startOffset = camToChar + 50.0f; // 50 units past the character

            outStart = {cameraLoc.X + worldDir.X * startOffset, cameraLoc.Y + worldDir.Y * startOffset, cameraLoc.Z + worldDir.Z * startOffset};
            outEnd = {cameraLoc.X + worldDir.X * TRACE_DIST, cameraLoc.Y + worldDir.Y * TRACE_DIST, cameraLoc.Z + worldDir.Z * TRACE_DIST};
            return true;
        }

        // Extracts the hit UObject* directly from FHitResult's Component FWeakObjectPtr.
        // Faster and more accurate than searching all components by name.
        UObject* resolveHitComponent(const uint8_t* hitBuf)
        {
            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            return hit->Component.Get();
        }

        bool isHISMComponent(UObject* comp)
        {
            if (!comp) return false;
            auto* cls = comp->GetClassPrivate();
            if (!cls) return false;
            std::wstring clsName(cls->GetName());
            return clsName.find(STR("InstancedStaticMeshComponent")) != std::wstring::npos;
        }

        // Hide instance by moving underground + tiny scale (safe — no crash unlike RemoveInstance)
        bool hideInstance(UObject* comp, int32_t instanceIndex)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            // Get current transform first
            auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!transFunc) return false;
            GetInstanceTransform_Params gtp{};
            gtp.InstanceIndex = instanceIndex;
            gtp.bWorldSpace = 1;
            comp->ProcessEvent(transFunc, &gtp);
            if (!gtp.ReturnValue) return false;

            // Move deep underground, scale to near-zero
            FTransformRaw hidden = gtp.OutTransform;
            hidden.Translation.Z -= 50000.0f;
            hidden.Scale3D = {0.001f, 0.001f, 0.001f};

            // UpdateInstanceTransform(int32 Index, FTransform NewTrans, bool bWorldSpace,
            //                         bool bMarkRenderStateDirty, bool bTeleport) -> bool
            // Layout matches GetInstanceTransform + 2 extra bools
            uint8_t params[72]{};
            int32_t idx = instanceIndex;
            std::memcpy(params + 0, &idx, 4);      // InstanceIndex
            std::memcpy(params + 16, &hidden, 48); // NewInstanceTransform (aligned)
            params[64] = 1;                        // bWorldSpace
            params[65] = 1;                        // bMarkRenderStateDirty
            params[66] = 1;                        // bTeleport
            comp->ProcessEvent(updateFunc, params);
            return params[67] != 0; // ReturnValue
        }

        // Restore instance to original transform (undo a hide)
        bool restoreInstance(UObject* comp, int32_t instanceIndex, const FTransformRaw& original)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            uint8_t params[72]{};
            std::memcpy(params + 0, &instanceIndex, 4);
            std::memcpy(params + 16, &original, 48);
            params[64] = 1; // bWorldSpace
            params[65] = 1; // bMarkRenderStateDirty
            params[66] = 1; // bTeleport
            comp->ProcessEvent(updateFunc, params);
            return params[67] != 0;
        }

        // Performs KismetSystemLibrary::LineTraceSingle via ProcessEvent.
        // Returns true if hit. Fills hitBuf (136 bytes = FHitResultLocal).
        // debugDraw=true shows red/green trace line in-game for 5 seconds.
        // Param buffer: 237 bytes (see LTOff namespace for layout).
        bool doLineTrace(const FVec3f& start, const FVec3f& end, uint8_t* hitBuf, bool debugDraw = false)
        {
            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return false;

            uint8_t params[LTOff::ParmsSize]{};
            std::memcpy(params + LTOff::WorldContextObject, &pc, 8);
            std::memcpy(params + LTOff::Start, &start, 12);
            std::memcpy(params + LTOff::End, &end, 12);
            params[LTOff::TraceChannel] = 0;  // Visibility
            params[LTOff::bTraceComplex] = 1; // Per-triangle for accuracy
            params[LTOff::bIgnoreSelf] = 1;

            // Add player pawn to ActorsToIgnore so trace doesn't hit the character
            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params + LTOff::ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params + LTOff::ActorsToIgnore + 8, &one, 4);
                std::memcpy(params + LTOff::ActorsToIgnore + 12, &one, 4);
            }

            if (debugDraw)
            {
                params[LTOff::DrawDebugType] = 2; // ForDuration
                float greenColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                float redColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float drawTime = 5.0f;
                std::memcpy(params + LTOff::TraceColor, greenColor, 16);
                std::memcpy(params + LTOff::TraceHitColor, redColor, 16);
                std::memcpy(params + LTOff::DrawTime, &drawTime, 4);
            }
            else
            {
                params[LTOff::DrawDebugType] = 0; // None
            }

            kslCDO->ProcessEvent(ltFunc, params);

            bool bHit = params[LTOff::ReturnValue] != 0;
            if (bHit)
            {
                std::memcpy(hitBuf, params + LTOff::OutHit, 136);
            }
            return bHit;
        }

        // ── Throttled Replay ──
        // Spreads UpdateInstanceTransform calls across frames to avoid crashing
        // the render thread (FStaticMeshInstanceBuffer::UpdateFromCommandBuffer_Concurrent).

        void startReplay()
        {
            if (m_replay.active) return; // don't interrupt active replay
            m_replay = {};
            if (m_savedRemovals.empty() && m_typeRemovals.empty()) return;

            UObjectGlobals::FindAllOf(STR("GlobalHierarchicalInstancedStaticMeshComponent"), m_replay.compQueue);
            if (m_replay.compQueue.empty()) UObjectGlobals::FindAllOf(STR("HierarchicalInstancedStaticMeshComponent"), m_replay.compQueue);

            m_replay.active = !m_replay.compQueue.empty();
            if (m_replay.active)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Starting throttled replay ({} comps, max {} hides/frame)\n"),
                                                m_replay.compQueue.size(),
                                                MAX_HIDES_PER_FRAME);
            }
        }

        // Process up to MAX_HIDES_PER_FRAME instances per frame. Returns true if more work remains.
        bool processReplayBatch()
        {
            if (!m_replay.active) return false;

            int hidesThisBatch = 0;

            while (m_replay.compIdx < m_replay.compQueue.size())
            {
                UObject* comp = m_replay.compQueue[m_replay.compIdx];

                // Validity check: can we access this component's functions?
                auto* countFunc = comp ? comp->GetFunctionByNameInChain(STR("GetInstanceCount")) : nullptr;
                if (!countFunc)
                {
                    m_replay.compIdx++;
                    m_replay.instanceIdx = 0;
                    continue;
                }

                std::string meshId = componentNameToMeshId(std::wstring(comp->GetName()));
                bool isTypeRule = m_typeRemovals.count(meshId) > 0;

                // For position-based, check if this mesh has any pending matches
                if (!isTypeRule)
                {
                    bool hasPending = false;
                    for (size_t si = 0; si < m_savedRemovals.size(); si++)
                    {
                        if (!m_appliedRemovals[si] && m_savedRemovals[si].meshName == meshId)
                        {
                            hasPending = true;
                            break;
                        }
                    }
                    if (!hasPending)
                    {
                        m_processedComps.insert(comp);
                        m_replay.compIdx++;
                        m_replay.instanceIdx = 0;
                        continue;
                    }
                }

                auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));

                // Get current instance count
                GetInstanceCount_Params cp{};
                comp->ProcessEvent(countFunc, &cp);
                int count = cp.ReturnValue;

                if (count == 0 || m_replay.instanceIdx >= count)
                {
                    m_processedComps.insert(comp);
                    m_replay.compIdx++;
                    m_replay.instanceIdx = 0;
                    continue;
                }

                // Process instances from where we left off
                while (m_replay.instanceIdx < count)
                {
                    if (hidesThisBatch >= MAX_HIDES_PER_FRAME)
                    {
                        return true; // Budget exhausted, continue next frame
                    }

                    int i = m_replay.instanceIdx++;

                    if (isTypeRule)
                    {
                        // For type rules, skip already-hidden instances
                        if (transFunc)
                        {
                            GetInstanceTransform_Params tp{};
                            tp.InstanceIndex = i;
                            tp.bWorldSpace = 1;
                            comp->ProcessEvent(transFunc, &tp);
                            if (tp.ReturnValue && tp.OutTransform.Translation.Z < -40000.0f) continue; // already hidden
                        }
                        if (hideInstance(comp, i))
                        {
                            hidesThisBatch++;
                            m_replay.totalHidden++;
                        }
                    }
                    else if (transFunc)
                    {
                        GetInstanceTransform_Params tp{};
                        tp.InstanceIndex = i;
                        tp.bWorldSpace = 1;
                        comp->ProcessEvent(transFunc, &tp);
                        if (!tp.ReturnValue) continue;

                        float px = tp.OutTransform.Translation.X;
                        float py = tp.OutTransform.Translation.Y;
                        float pz = tp.OutTransform.Translation.Z;
                        if (pz < -40000.0f) continue; // already hidden

                        for (size_t si = 0; si < m_savedRemovals.size(); si++)
                        {
                            if (m_appliedRemovals[si]) continue;
                            if (m_savedRemovals[si].meshName != meshId) continue;
                            float ddx = px - m_savedRemovals[si].posX;
                            float ddy = py - m_savedRemovals[si].posY;
                            float ddz = pz - m_savedRemovals[si].posZ;
                            if (ddx * ddx + ddy * ddy + ddz * ddz < POS_TOLERANCE * POS_TOLERANCE)
                            {
                                hideInstance(comp, i);
                                m_appliedRemovals[si] = true;
                                hidesThisBatch++;
                                m_replay.totalHidden++;
                                break;
                            }
                        }
                    }
                }

                // Finished all instances in this component
                m_processedComps.insert(comp);
                m_replay.compIdx++;
                m_replay.instanceIdx = 0;
            }

            // All components processed — replay complete
            m_replay.active = false;
            int pending = pendingCount();
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Replay done: {} hidden, {} pending\n"), m_replay.totalHidden, pending);
            return false;
        }

        void checkForNewComponents()
        {
            if (m_savedRemovals.empty() && m_typeRemovals.empty()) return;
            if (m_replay.active) return; // don't interfere with active replay

            std::vector<UObject*> comps;
            UObjectGlobals::FindAllOf(STR("GlobalHierarchicalInstancedStaticMeshComponent"), comps);

            // Collect new (unprocessed) components
            std::vector<UObject*> newComps;
            for (auto* comp : comps)
            {
                if (!m_processedComps.count(comp)) newComps.push_back(comp);
            }
            if (newComps.empty()) return;

            // Queue them as a new replay batch
            m_replay = {};
            m_replay.compQueue = std::move(newComps);
            m_replay.active = true;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Streaming: {} new components queued for replay\n"), m_replay.compQueue.size());
        }

        // ── Actions ──

        void inspectAimed()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] --- Inspect ---\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Ray: ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f})\n"), start.X, start.Y, start.Z, end.X, end.Y, end.Z);

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf, true))
            { // debugDraw=true
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(L"[Inspect] No hit", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            FVec3f impactPoint = hit->ImpactPoint;
            int32_t item = hit->Item;

            // Resolve component directly via FWeakObjectPtr (fast, accurate)
            UObject* hitComp = resolveHitComponent(hitBuf);

            std::wstring compName = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
            std::wstring fullName = hitComp ? std::wstring(hitComp->GetFullName()) : L"(null)";
            std::wstring className = L"(unknown)";
            if (hitComp)
            {
                auto* cls = hitComp->GetClassPrivate();
                if (cls) className = std::wstring(cls->GetName());
            }
            bool isHISM = isHISMComponent(hitComp);
            std::string meshId = hitComp ? componentNameToMeshId(compName) : "(null)";
            std::wstring meshIdW(meshId.begin(), meshId.end());

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Component: {} | Class: {} | Item: {} | HISM: {}\n"), compName, className, item, isHISM);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] FullPath: {}\n"), fullName);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] MeshID: {} | Impact: ({:.1f},{:.1f},{:.1f})\n"),
                                            meshIdW,
                                            impactPoint.X,
                                            impactPoint.Y,
                                            impactPoint.Z);

            // Show instance transform if it's an HISM
            if (isHISM && item >= 0 && hitComp)
            {
                auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
                if (transFunc)
                {
                    GetInstanceTransform_Params tp{};
                    tp.InstanceIndex = item;
                    tp.bWorldSpace = 1;
                    hitComp->ProcessEvent(transFunc, &tp);
                    if (tp.ReturnValue)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Instance #{} pos: ({:.1f},{:.1f},{:.1f})\n"),
                                                        item,
                                                        tp.OutTransform.Translation.X,
                                                        tp.OutTransform.Translation.Y,
                                                        tp.OutTransform.Translation.Z);
                    }
                }
            }

            // On-screen display
            std::wstring screenText = fullName + L"\nClass: " + className;
            if (isHISM)
            {
                screenText += L"\nItem: " + std::to_wstring(item) + L" | MeshID: " + meshIdW;
            }
            float screenR = isHISM ? 0.0f : 1.0f;
            float screenG = isHISM ? 1.0f : 0.5f;
            showOnScreen(screenText, 8.0f, screenR, screenG, 0.5f);
        }

        // LINT NOTE (#18 — removeAimed throttle): Analyzed and intentionally skipped. This function is
        // only called on Num1 keypress (not per-frame), so call frequency is naturally limited by keyboard
        // repeat rate (~20 Hz). Adding a throttle risks partial stack removal (e.g., hiding 2 of 5 stacked
        // instances, corrupting the undo stack). The automated replay path already has MAX_HIDES_PER_FRAME.
        void removeAimed()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No hit\n"));
                showInfoBox(L"Remove", L"No hit", 1.0f, 0.3f, 0.3f);
                return;
            }

            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            FVec3f impactPoint = hit->ImpactPoint;
            int32_t item = hit->Item;

            // Resolve component directly
            UObject* hitComp = resolveHitComponent(hitBuf);

            if (!hitComp || !isHISMComponent(hitComp))
            {
                std::wstring name = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
                std::wstring cls = L"";
                if (hitComp)
                {
                    auto* c = hitComp->GetClassPrivate();
                    if (c) cls = std::wstring(c->GetName());
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Not HISM: {} ({})\n"), name, cls);
                showOnScreen(L"Not HISM: " + name, 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            if (item < 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No instance index (Item=-1)\n"));
                showOnScreen(L"No instance index", 2.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            // Get transform of the aimed instance
            auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            auto* countFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceCount"));
            if (!transFunc || !countFunc) return;
            GetInstanceTransform_Params tp{};
            tp.InstanceIndex = item;
            tp.bWorldSpace = 1;
            hitComp->ProcessEvent(transFunc, &tp);
            if (!tp.ReturnValue) return;

            float targetX = tp.OutTransform.Translation.X;
            float targetY = tp.OutTransform.Translation.Y;
            float targetZ = tp.OutTransform.Translation.Z;
            std::wstring compName(hitComp->GetName());
            std::string meshId = componentNameToMeshId(compName);

            // Find ALL instances at the same position (stacked instances)
            GetInstanceCount_Params cp{};
            hitComp->ProcessEvent(countFunc, &cp);
            int count = cp.ReturnValue;

            int hiddenCount = 0;
            for (int i = 0; i < count; i++)
            {
                GetInstanceTransform_Params itp{};
                itp.InstanceIndex = i;
                itp.bWorldSpace = 1;
                hitComp->ProcessEvent(transFunc, &itp);
                if (!itp.ReturnValue) continue;

                float px = itp.OutTransform.Translation.X;
                float py = itp.OutTransform.Translation.Y;
                float pz = itp.OutTransform.Translation.Z;

                // Skip already-hidden
                if (pz < -40000.0f) continue;

                float ddx = px - targetX;
                float ddy = py - targetY;
                float ddz = pz - targetZ;
                if (ddx * ddx + ddy * ddy + ddz * ddz < POS_TOLERANCE * POS_TOLERANCE)
                {
                    // Save for undo
                    m_undoStack.push_back({hitComp, i, itp.OutTransform, compName});

                    // Save to persistence file
                    SavedRemoval sr;
                    sr.meshName = meshId;
                    sr.posX = px;
                    sr.posY = py;
                    sr.posZ = pz;
                    m_savedRemovals.push_back(sr);
                    m_appliedRemovals.push_back(true);
                    appendToSaveFile(sr);
                    buildRemovalEntries();

                    hideInstance(hitComp, i);
                    hiddenCount++;
                }
            }

            std::wstring meshIdW(meshId.begin(), meshId.end());
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] REMOVED {} stacked at ({:.0f},{:.0f},{:.0f}) from {} | Total: {}\n"),
                                            hiddenCount,
                                            targetX,
                                            targetY,
                                            targetZ,
                                            compName,
                                            m_savedRemovals.size());
            showInfoBox(L"Removed", std::to_wstring(hiddenCount) + L"x: " + meshIdW, 0.0f, 1.0f, 0.0f);
            showGameMessage(L"[Mod] Removed " + std::to_wstring(hiddenCount) + L"x: " + meshIdW);
        }

        void removeAllOfType()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No hit\n"));
                showInfoBox(L"Remove All", L"No hit", 1.0f, 0.3f, 0.3f);
                return;
            }

            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp || !isHISMComponent(hitComp))
            {
                std::wstring name = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Not HISM: {}\n"), name);
                showOnScreen(L"Not HISM: " + name, 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            auto* countFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceCount"));
            auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!countFunc) return;

            std::wstring compName(hitComp->GetName());
            std::string meshId = componentNameToMeshId(compName);

            // Save as type rule — removes ALL of this mesh on every world
            if (!m_typeRemovals.count(meshId))
            {
                m_typeRemovals.insert(meshId);
                std::ofstream file(m_saveFilePath, std::ios::app);
                if (file.is_open()) file << "@" << meshId << "\n";
                buildRemovalEntries();
            }

            // Get instance count
            GetInstanceCount_Params cp{};
            hitComp->ProcessEvent(countFunc, &cp);
            int count = cp.ReturnValue;

            // Save all transforms for undo, then hide each instance
            int hidden = 0;
            for (int i = 0; i < count; i++)
            {
                if (transFunc)
                {
                    GetInstanceTransform_Params tp{};
                    tp.InstanceIndex = i;
                    tp.bWorldSpace = 1;
                    hitComp->ProcessEvent(transFunc, &tp);
                    if (tp.ReturnValue)
                    {
                        // Skip already-hidden instances
                        if (tp.OutTransform.Translation.Z < -40000.0f) continue;
                        m_undoStack.push_back({hitComp, i, tp.OutTransform, compName, true, meshId});
                    }
                }
                if (hideInstance(hitComp, i)) hidden++;
            }

            std::wstring meshIdW(meshId.begin(), meshId.end());
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] TYPE RULE: @{} — hidden {} instances (persists across all worlds)\n"), meshIdW, hidden);
            showInfoBox(L"Type Rule", meshIdW + L" (" + std::to_wstring(hidden) + L" hidden)", 1.0f, 0.5f, 0.0f);
            showGameMessage(L"[Mod] Type rule: " + meshIdW + L" (" + std::to_wstring(hidden) + L" hidden)");
        }

        // ── Building / UI Exploration (Num7/Num8/Num9) — DISABLED: keybinds removed ──
#if 0
        void dumpAllWidgets()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === WIDGET DUMP ===\n"));

            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {} UserWidgets\n"), widgets.size());

            int idx = 0;
            for (auto* w : widgets)
            {
                if (!w) continue;
                auto* cls = w->GetClassPrivate();
                if (!cls) continue;
                std::wstring clsName(cls->GetName());
                std::wstring objName(w->GetName());

                // Check visibility via IsVisible or IsInViewport
                bool visible = false;
                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    w->ProcessEvent(visFunc, &vp);
                    visible = vp.Ret;
                }

                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   [{}] {} | obj={} | visible={}\n"), idx, clsName, objName, visible);

                // For build/craft/recipe-related widgets, dump functions too
                bool interesting = false;
                std::string narrow;
                for (wchar_t c : clsName)
                    narrow.push_back(static_cast<char>(c));
                for (auto& ch : narrow)
                    ch = static_cast<char>(std::tolower(ch));
                if (narrow.find("build") != std::string::npos || narrow.find("craft") != std::string::npos || narrow.find("recipe") != std::string::npos ||
                    narrow.find("construct") != std::string::npos || narrow.find("place") != std::string::npos || narrow.find("inventory") != std::string::npos ||
                    narrow.find("radial") != std::string::npos || narrow.find("wheel") != std::string::npos || narrow.find("menu") != std::string::npos)
                {
                    interesting = true;
                }

                if (interesting || visible)
                {
                    // Dump functions
                    auto* ustruct = static_cast<UStruct*>(cls);
                    int funcCount = 0;
                    for (auto* func : ustruct->ForEachFunctionInChain())
                    {
                        if (!func) continue;
                        std::wstring funcName(func->GetName());
                        int parmsSize = func->GetParmsSize();

                        // Skip common inherited UE functions to reduce noise
                        if (funcName.find(STR("Construct")) == 0 && funcName.find(STR("Construction")) == std::wstring::npos) continue;
                        if (funcName == STR("Destruct")) continue;
                        if (funcName.find(STR("ExecuteUbergraph")) == 0) continue;

                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     fn: {} ({}B)\n"), funcName, parmsSize);
                        funcCount++;
                        if (funcCount > 30)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     ... (truncated at 30)\n"));
                            break;
                        }
                    }
                }

                idx++;
            }

            showOnScreen(L"Widget dump: " + std::to_wstring(widgets.size()) + L" widgets (see log)", 5.0f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END WIDGET DUMP ===\n"));
        }
#endif

        void dumpAimedActor()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === AIMED ACTOR DUMP ===\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }

            // Use a wider trace — we want to hit actors, not just HISM instances
            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return;

            uint8_t params[LTOff::ParmsSize]{};
            std::memcpy(params + LTOff::WorldContextObject, &pc, 8);
            std::memcpy(params + LTOff::Start, &start, 12);
            std::memcpy(params + LTOff::End, &end, 12);
            params[LTOff::TraceChannel] = 0;  // Visibility
            params[LTOff::bTraceComplex] = 0; // Simple trace to hit actor bounds
            params[LTOff::bIgnoreSelf] = 1;
            params[LTOff::DrawDebugType] = 2; // ForDuration
            float greenColor[4] = {0.0f, 1.0f, 1.0f, 1.0f};
            float redColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
            float drawTime = 5.0f;
            std::memcpy(params + LTOff::TraceColor, greenColor, 16);
            std::memcpy(params + LTOff::TraceHitColor, redColor, 16);
            std::memcpy(params + LTOff::DrawTime, &drawTime, 4);

            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params + LTOff::ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params + LTOff::ActorsToIgnore + 8, &one, 4);
                std::memcpy(params + LTOff::ActorsToIgnore + 12, &one, 4);
            }

            kslCDO->ProcessEvent(ltFunc, params);

            bool bHit = params[LTOff::ReturnValue] != 0;
            if (!bHit)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(L"[ActorDump] No hit", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            uint8_t hitBuf[136]{};
            std::memcpy(hitBuf, params + LTOff::OutHit, 136);

            // Get the hit component and its owning actor
            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Hit but null component\n"));
                return;
            }

            std::wstring compName(hitComp->GetName());
            std::wstring compClass = safeClassName(hitComp);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Hit component: {} ({})\n"), compName, compClass);

            // Get the owning actor via GetOwner
            auto* ownerFunc = hitComp->GetFunctionByNameInChain(STR("GetOwner"));
            UObject* actor = nullptr;
            if (ownerFunc)
            {
                struct
                {
                    UObject* Ret{nullptr};
                } op{};
                hitComp->ProcessEvent(ownerFunc, &op);
                actor = op.Ret;
            }

            if (!actor)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No owning actor found\n"));
                showOnScreen(L"[ActorDump] Component: " + compName + L" (" + compClass + L")\nNo owning actor", 5.0f);
                return;
            }

            std::wstring actorName(actor->GetName());
            std::wstring actorClassName = safeClassName(actor);

            // Get asset path via GetPathName()
            std::wstring assetPath(actor->GetPathName());

            // Try to get display name via GetDisplayName() (returns FText)
            // Available on: UFGKCharacterBase, AInventoryItem, AMorInteractable, and others
            // NOTE: AMorBreakable (building pieces) does NOT have this — they need DataTable lookup
            std::wstring displayName;
            auto* getDispFn = actor->GetFunctionByNameInChain(STR("GetDisplayName"));
            if (getDispFn)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetDisplayName found, parmsSize={}\n"), getDispFn->GetParmsSize());
                if (getDispFn->GetParmsSize() == sizeof(FText))
                {
                    FText txt{};
                    actor->ProcessEvent(getDispFn, &txt);
                    if (txt.Data) displayName = txt.ToString();
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetDisplayName NOT found on this actor\n"));
            }
            if (displayName.empty())
            {
                // Fallback: generate readable name from class name
                // BP_Suburbs_Wall_Thick_4x1m_A_C → Suburbs Wall Thick 4x1m A
                std::wstring cleaned = actorClassName;
                if (cleaned.size() > 3 && cleaned.substr(0, 3) == L"BP_") cleaned = cleaned.substr(3);
                if (cleaned.size() > 2 && cleaned.substr(cleaned.size() - 2) == L"_C") cleaned = cleaned.substr(0, cleaned.size() - 2);
                for (auto& c : cleaned)
                {
                    if (c == L'_') c = L' ';
                }
                displayName = cleaned;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Display name fallback: '{}'\n"), displayName);
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Actor: {} | Class: {}\n"), actorName, actorClassName);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Display: {}\n"), displayName);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Path: {}\n"), assetPath);

            // Detect if player-buildable by checking class hierarchy for construction components
            bool isBuildable = false;
            std::wstring recipeRef;
            auto* actorCls = actor->GetClassPrivate();
            if (actorCls)
            {
                auto* actorStruct = static_cast<UStruct*>(actorCls);
                // Check properties for construction-related components
                for (auto* prop : actorStruct->ForEachPropertyInChain())
                {
                    if (!prop) continue;
                    std::wstring propName(prop->GetName());
                    if (propName.find(L"MorConstructionSnap") != std::wstring::npos || propName.find(L"MorConstructionStability") != std::wstring::npos ||
                        propName.find(L"MorConstructionPermit") != std::wstring::npos)
                    {
                        isBuildable = true;
                        break;
                    }
                }
                // Walk super class chain for construction base classes
                if (!isBuildable)
                {
                    for (auto* super = actorCls->GetSuperStruct(); super; super = super->GetSuperStruct())
                    {
                        std::wstring superName(super->GetName());
                        if (superName.find(L"BaseArchitectureBreakable") != std::wstring::npos || superName.find(L"CraftingStation") != std::wstring::npos ||
                            superName.find(L"FueledCraftingStation") != std::wstring::npos)
                        {
                            isBuildable = true;
                            break;
                        }
                    }
                }
                // Build recipe reference from class name
                if (isBuildable)
                {
                    // Strip _C suffix to get blueprint asset name
                    recipeRef = actorClassName;
                    if (recipeRef.size() > 2 && recipeRef.substr(recipeRef.size() - 2) == L"_C") recipeRef = recipeRef.substr(0, recipeRef.size() - 2);
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Buildable: {} Recipe: {}\n"), isBuildable ? STR("Yes") : STR("No"), recipeRef);

            // ── DT_Constructions lookup: find real display name for this actor ──
            std::wstring dtDisplayName;
            std::wstring dtRowName;
            {
                std::wofstream dumpFile("Mods/MoriaCppMod/actor_dump.txt", std::ios::trunc);
                if (dumpFile.is_open())
                {
                    dumpFile << L"=== DT_Constructions SCAN for: " << actorClassName << L" ===\n";
                    dumpFile << L"Actor path: " << assetPath << L"\n";

                    // Get actor class path (the blueprint path, not the instance path)
                    std::wstring classPath;
                    if (actorCls)
                    {
                        classPath = std::wstring(actorCls->GetPathName());
                        dumpFile << L"Class path: " << classPath << L"\n";
                    }
                    dumpFile << L"\n";
                    dumpFile.flush();

                    // Find DT_Constructions
                    std::vector<UObject*> dataTables;
                    UObjectGlobals::FindAllOf(STR("DataTable"), dataTables);
                    UObject* dtConst = nullptr;
                    for (auto* dt : dataTables)
                    {
                        if (!dt) continue;
                        try
                        {
                            std::wstring name(dt->GetName());
                            if (name == STR("DT_Constructions"))
                            {
                                dtConst = dt;
                                break;
                            }
                        }
                        catch (...)
                        {
                        }
                    }

                    if (!dtConst)
                    {
                        dumpFile << L"DT_Constructions NOT FOUND\n";
                    }
                    else
                    {
                        dumpFile << L"Found DT_Constructions at " << dtConst << L"\n\n";

                        // Read RowMap: offset 0x30, TSet<TPair<FName, uint8*>>
                        uint8_t* dtBase = reinterpret_cast<uint8_t*>(dtConst);
                        constexpr int ROWMAP_OFFSET = 0x30;
                        constexpr int SET_ELEMENT_SIZE = 24;
                        constexpr int FNAME_SIZE = 8;

                        struct
                        {
                            uint8_t* Data;
                            int32_t Num;
                            int32_t Max;
                        } elemArray{};
                        std::memcpy(&elemArray, dtBase + ROWMAP_OFFSET, 16);

                        dumpFile << L"RowMap: " << elemArray.Num << L" rows\n\n";
                        dumpFile.flush();

                        // TSoftClassPtr = TPersistentObjectPtr<FSoftObjectPath> layout:
                        //   +0x00 (row+0x50): TWeakObjectPtr (8 bytes) — cached resolved ptr
                        //   +0x08 (row+0x58): int32 TagAtLastTest (4 bytes) + 4 bytes padding
                        //   +0x10 (row+0x60): FName AssetPathName (8 bytes) — the asset path
                        //   +0x18 (row+0x68): FString SubPathString (16 bytes) — usually empty
                        constexpr int ACTOR_FNAME_OFFSET = 0x60; // FSoftObjectPath.AssetPathName within row

                        int matchCount = 0;

                        for (int i = 0; i < elemArray.Num; i++)
                        {
                            uint8_t* elem = elemArray.Data + i * SET_ELEMENT_SIZE;
                            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;

                            uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + FNAME_SIZE);
                            if (!rowData || !isReadableMemory(rowData, 0x78)) continue;

                            // Read Actor AssetPathName FName at row+0x60
                            FName assetFName;
                            std::memcpy(&assetFName, rowData + ACTOR_FNAME_OFFSET, FNAME_SIZE);
                            std::wstring rowAssetPath;
                            try
                            {
                                rowAssetPath = assetFName.ToString();
                            }
                            catch (...)
                            {
                                continue;
                            }

                            // Match: classPath contains assetPath or assetPath contains recipeRef
                            bool isMatch = false;
                            if (!rowAssetPath.empty())
                            {
                                if (!classPath.empty() && classPath.find(rowAssetPath) != std::wstring::npos)
                                {
                                    isMatch = true;
                                }
                                if (!isMatch && !recipeRef.empty() && rowAssetPath.find(recipeRef) != std::wstring::npos)
                                {
                                    isMatch = true;
                                }
                            }

                            // Dump first 3 rows + any matches for diagnostics
                            if (i < 3 || isMatch)
                            {
                                FName rowName;
                                std::memcpy(&rowName, elem, FNAME_SIZE);
                                std::wstring rowNameStr;
                                try
                                {
                                    rowNameStr = rowName.ToString();
                                }
                                catch (...)
                                {
                                    rowNameStr = L"(err)";
                                }

                                std::wstring dispName;
                                try
                                {
                                    FText* txt = reinterpret_cast<FText*>(rowData + 0x18);
                                    if (txt && txt->Data && isReadableMemory(txt->Data, 8)) dispName = txt->ToString();
                                }
                                catch (...)
                                {
                                    dispName = L"(err)";
                                }

                                dumpFile << (isMatch ? L">>> MATCH" : L"   ") << L" [" << i << L"] " << rowNameStr << L"  Display=\"" << dispName
                                         << L"\"  ActorPath=\"" << rowAssetPath << L"\"\n";
                                dumpFile.flush();

                                if (isMatch && dtDisplayName.empty())
                                {
                                    dtDisplayName = dispName;
                                    dtRowName = rowNameStr;
                                }
                            }

                            if (isMatch) matchCount++;
                        }

                        dumpFile << L"\n=== RESULTS: " << elemArray.Num << L" rows scanned, " << matchCount << L" matches ===\n";
                        if (!dtDisplayName.empty())
                        {
                            dumpFile << L"MATCHED: row='" << dtRowName << L"' display='" << dtDisplayName << L"'\n";
                        }
                        else
                        {
                            dumpFile << L"NO MATCH FOUND for classPath='" << classPath << L"'\n";
                        }
                    }
                    dumpFile.close();

                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] DT_Constructions scan -> actor_dump.txt\n"));
                }
            }

            // Use DT_Constructions display name if found, overriding the fallback
            if (!dtDisplayName.empty())
            {
                displayName = dtDisplayName;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] DT display name: '{}' (row '{}')\n"), displayName, dtRowName);
            }

            // Store for Shift+F10 build-from-target
            m_lastTargetBuildable = isBuildable;
            m_targetBuildRecipeRef = recipeRef;
            m_targetBuildRowName = dtRowName; // DT_Constructions row name (also key for DT_ConstructionRecipes)
            if (isBuildable && !displayName.empty())
            {
                m_targetBuildName = displayName;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Stored target: name='{}' recipeRef='{}' row='{}'\n"),
                                                m_targetBuildName,
                                                m_targetBuildRecipeRef,
                                                m_targetBuildRowName);
            }
            else
            {
                m_targetBuildName.clear();
            }

            // Non-buildable: do a second HISM-aware trace and show component/mesh info instead
            if (!isBuildable)
            {
                uint8_t inspectHitBuf[136]{};
                if (doLineTrace(start, end, inspectHitBuf, false))
                {
                    auto* inspHit = reinterpret_cast<const FHitResultLocal*>(inspectHitBuf);
                    FVec3f impactPt = inspHit->ImpactPoint;
                    int32_t instItem = inspHit->Item;
                    UObject* inspComp = resolveHitComponent(inspectHitBuf);
                    if (inspComp)
                    {
                        std::wstring inspCompName(inspComp->GetName());
                        std::wstring inspFullName(inspComp->GetFullName());
                        std::wstring inspClassName = safeClassName(inspComp);
                        bool inspIsHISM = isHISMComponent(inspComp);
                        std::string inspMeshId = componentNameToMeshId(inspCompName);
                        std::wstring inspMeshIdW(inspMeshId.begin(), inspMeshId.end());

                        // Extract friendly name from mesh ID (first segment before '-')
                        std::wstring friendlyName = extractFriendlyName(inspMeshId);

                        // Get instance position if HISM
                        std::wstring posInfo;
                        if (inspIsHISM && instItem >= 0)
                        {
                            auto* transFunc = inspComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
                            if (transFunc)
                            {
                                GetInstanceTransform_Params tp{};
                                tp.InstanceIndex = instItem;
                                tp.bWorldSpace = 1;
                                inspComp->ProcessEvent(transFunc, &tp);
                                if (tp.ReturnValue)
                                {
                                    posInfo = std::format(L"Pos: ({:.0f}, {:.0f}, {:.0f}) Item #{}",
                                                          tp.OutTransform.Translation.X,
                                                          tp.OutTransform.Translation.Y,
                                                          tp.OutTransform.Translation.Z,
                                                          instItem);
                                }
                            }
                        }
                        if (posInfo.empty())
                        {
                            posInfo = std::format(L"Impact: ({:.0f}, {:.0f}, {:.0f})", impactPt.X, impactPt.Y, impactPt.Z);
                        }

                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [F10] Non-buildable inspect: {} | HISM={} | MeshID={}\n"),
                                                        inspCompName, inspIsHISM, inspMeshIdW);

                        // Show inspect data in target info popup instead of actor data
                        showTargetInfo(inspCompName, friendlyName, inspMeshIdW, inspClassName, false, posInfo, L"");
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
                        return;
                    }
                }
            }

            // Show target info popup window (buildable objects or inspect fallback failed)
            showTargetInfo(actorName, displayName, assetPath, actorClassName, isBuildable, recipeRef, dtRowName);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
        }

        // ── Debug probes (Num9, Alt+Num7/8/9) — DISABLED: keybinds removed ──
#if 0
        void dumpBuildCraftClasses()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === BUILD/CRAFT CLASS SEARCH ===\n"));

            // Search patterns for build/craft related classes
            const wchar_t* searchPatterns[] = {
                    STR("Build"),
                    STR("Craft"),
                    STR("Recipe"),
                    STR("Construct"),
                    STR("Place"),
                    STR("Structure"),
                    STR("Buildable"),
                    STR("Blueprint"),
            };

            // Dump all UObject subclasses that match our search terms
            // Strategy: Search common game class hierarchies
            const wchar_t* baseClasses[] = {
                    STR("Actor"),
                    STR("UserWidget"),
                    STR("DataAsset"),
                    STR("Object"),
            };

            int totalFound = 0;
            std::set<std::wstring> seenClasses;

            for (auto* baseClass : baseClasses)
            {
                std::vector<UObject*> objects;
                UObjectGlobals::FindAllOf(baseClass, objects);

                for (auto* obj : objects)
                {
                    if (!obj) continue;
                    auto* cls = obj->GetClassPrivate();
                    if (!cls) continue;
                    std::wstring clsName(cls->GetName());

                    // Skip if already seen this class
                    if (seenClasses.count(clsName)) continue;

                    // Check if class name matches any search pattern
                    std::string narrow;
                    for (wchar_t c : clsName)
                        narrow.push_back(static_cast<char>(c));
                    std::string lower = narrow;
                    for (auto& ch : lower)
                        ch = static_cast<char>(std::tolower(ch));

                    bool matches = false;
                    for (auto* pattern : searchPatterns)
                    {
                        std::string patNarrow;
                        for (const wchar_t* p = pattern; *p; p++)
                            patNarrow.push_back(static_cast<char>(*p));
                        std::string patLower = patNarrow;
                        for (auto& ch : patLower)
                            ch = static_cast<char>(std::tolower(ch));
                        if (lower.find(patLower) != std::string::npos)
                        {
                            matches = true;
                            break;
                        }
                    }

                    if (!matches) continue;
                    seenClasses.insert(clsName);
                    totalFound++;

                    std::wstring objName(obj->GetName());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [{}] Class: {} | Base: {} | Obj: {}\n"), totalFound, clsName, baseClass, objName);

                    // Dump functions for this class
                    auto* ustruct = static_cast<UStruct*>(cls);
                    int fc = 0;
                    for (auto* func : ustruct->ForEachFunction())
                    {
                        if (!func) continue;
                        std::wstring funcName(func->GetName());
                        if (funcName.find(STR("ExecuteUbergraph")) == 0) continue;
                        int parmsSize = func->GetParmsSize();
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     fn: {} ({}B)\n"), funcName, parmsSize);
                        fc++;
                        if (fc > 40)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     ... truncated\n"));
                            break;
                        }
                    }

                    // Dump properties (own only, not inherited)
                    int pc = 0;
                    for (auto* prop : ustruct->ForEachProperty())
                    {
                        if (!prop) continue;
                        std::wstring propName(prop->GetName());
                        int offset = prop->GetOffset_Internal();
                        int size = prop->GetSize();
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     prop: {} @{} size={}\n"), propName, offset, size);
                        pc++;
                        if (pc > 40)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     ... truncated\n"));
                            break;
                        }
                    }
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {} unique build/craft related classes\n"), totalFound);
            showOnScreen(L"Build/Craft search: " + std::to_wstring(totalFound) + L" classes (see log)", 5.0f, 1.0f, 0.8f, 0.0f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END BUILD/CRAFT SEARCH ===\n"));
        }

        // ── Deep Probes (Ctrl+Numpad keys) ──

        void probeBuildTabRecipe()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === PROBE: Build_Tab selectedRecipe ===\n"));

            // Find the Build_Tab widget
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            UObject* buildTab = nullptr;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring clsName = safeClassName(w);
                if (clsName == STR("UI_WBP_Build_Tab_C"))
                {
                    buildTab = w;
                    break;
                }
            }
            if (!buildTab)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Build_Tab widget NOT FOUND\n"));
                showOnScreen(L"Build_Tab NOT FOUND", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Build_Tab found: {}\n"), std::wstring(buildTab->GetName()));

            // Read selectedRecipe at offset 1120, size 120
            uint8_t* objPtr = reinterpret_cast<uint8_t*>(buildTab);
            uint8_t* recipePtr = objPtr + 1120;

            // Dump raw bytes in hex (8 bytes per line)
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] selectedRecipe @1120 (120 bytes):\n"));
            for (int row = 0; row < 120; row += 8)
            {
                std::wstring hexLine;
                std::wstring asciiLine;
                for (int col = 0; col < 8 && (row + col) < 120; col++)
                {
                    uint8_t b = recipePtr[row + col];
                    wchar_t hex[8]{};
                    swprintf(hex, 8, L"%02X ", b);
                    hexLine += hex;
                    asciiLine += (b >= 32 && b < 127) ? static_cast<wchar_t>(b) : L'.';
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   +{:3d}: {} | {}\n"), row, hexLine, asciiLine);
            }

            // Also read selectedName at offset 1536 (FName, 8 bytes)
            uint8_t* namePtr = objPtr + 1536;
            // FName: ComparisonIndex(4) + Number(4) — try to interpret
            int32_t nameIdx = *reinterpret_cast<int32_t*>(namePtr);
            int32_t nameNum = *reinterpret_cast<int32_t*>(namePtr + 4);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] selectedName @1536: index={} number={}\n"), nameIdx, nameNum);

            // Try to read recipesDataTable pointer at 1544
            UObject** dtPtr = reinterpret_cast<UObject**>(objPtr + 1544);
            if (*dtPtr)
            {
                std::wstring dtName((*dtPtr)->GetName());
                std::wstring dtClass = safeClassName(*dtPtr);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] recipesDataTable @1544: {} ({})\n"), dtName, dtClass);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] recipesDataTable @1544: nullptr\n"));
            }

            // Interpret potential FName values within the recipe struct
            // FName indices are typically small positive integers
            for (int off = 0; off < 120; off += 4)
            {
                int32_t val = *reinterpret_cast<int32_t*>(recipePtr + off);
                // Check if it could be a small int (index, count, enum)
                if (val > 0 && val < 100000)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   recipe+{}: int32={}\n"), off, val);
                }
            }

            // Also dump the ShowThisRecipe function params layout
            auto* showFunc = buildTab->GetFunctionByNameInChain(STR("ShowThisRecipe"));
            if (showFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ShowThisRecipe params ({}B):\n"), showFunc->GetParmsSize());
                for (auto* prop : showFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            // And blockSelectedEvent params
            auto* blockFunc = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (blockFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] blockSelectedEvent params ({}B):\n"), blockFunc->GetParmsSize());
                for (auto* prop : blockFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            showOnScreen(L"Build_Tab recipe probe done (see log)", 5.0f, 0.0f, 1.0f, 0.5f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END PROBE: Build_Tab ===\n"));
        }

        void probeBuildConstruction()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === PROBE: BuildNewConstruction ===\n"));

            // Find MorBuildingComponent
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);

            UObject* buildingComp = nullptr;
            for (auto* actor : actors)
            {
                if (!actor) continue;
                std::wstring clsName = safeClassName(actor);
                if (clsName.find(STR("MorBuildingComponent")) != std::wstring::npos)
                {
                    buildingComp = actor;
                    break;
                }
            }

            // Also try finding via component search
            if (!buildingComp)
            {
                std::vector<UObject*> comps;
                UObjectGlobals::FindAllOf(STR("ActorComponent"), comps);
                for (auto* c : comps)
                {
                    if (!c) continue;
                    std::wstring clsName = safeClassName(c);
                    if (clsName.find(STR("MorBuildingComponent")) != std::wstring::npos)
                    {
                        buildingComp = c;
                        break;
                    }
                }
            }

            if (!buildingComp)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] MorBuildingComponent NOT FOUND\n"));
                showOnScreen(L"MorBuildingComponent NOT FOUND", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            std::wstring compName(buildingComp->GetName());
            std::wstring compClass = safeClassName(buildingComp);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found: {} ({})\n"), compName, compClass);

            // Probe BuildNewConstruction param layout
            auto* buildFunc = buildingComp->GetFunctionByNameInChain(STR("BuildNewConstruction"));
            if (buildFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BuildNewConstruction params ({}B):\n"), buildFunc->GetParmsSize());
                for (auto* prop : buildFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BuildNewConstruction function NOT FOUND\n"));
            }

            // Probe CanBuild
            auto* canBuildFunc = buildingComp->GetFunctionByNameInChain(STR("CanBuild"));
            if (canBuildFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] CanBuild params ({}B):\n"), canBuildFunc->GetParmsSize());
                for (auto* prop : canBuildFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            // Probe GetBuildTargetTransform
            auto* transformFunc = buildingComp->GetFunctionByNameInChain(STR("GetBuildTargetTransform"));
            if (transformFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetBuildTargetTransform params ({}B):\n"), transformFunc->GetParmsSize());
                for (auto* prop : transformFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            // Probe GetActiveBuildingWidget
            auto* widgetFunc = buildingComp->GetFunctionByNameInChain(STR("GetActiveBuildingWidget"));
            if (widgetFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetActiveBuildingWidget params ({}B):\n"), widgetFunc->GetParmsSize());
                for (auto* prop : widgetFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            // Dump all properties on the component
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] --- MorBuildingComponent Properties ---\n"));
            auto* ustruct = static_cast<UStruct*>(buildingComp->GetClassPrivate());
            for (auto* prop : ustruct->ForEachPropertyInChain())
            {
                if (!prop) continue;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   prop: {} @{} size={}\n"),
                                                std::wstring(prop->GetName()),
                                                prop->GetOffset_Internal(),
                                                prop->GetSize());
            }

            // Read LastSelectedRecipe raw bytes (offset 208, size 16)
            uint8_t* objPtr = reinterpret_cast<uint8_t*>(buildingComp);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] LastSelectedRecipe @208 (16 bytes):\n"));
            for (int row = 0; row < 16; row += 8)
            {
                std::wstring hexLine;
                for (int col = 0; col < 8 && (row + col) < 16; col++)
                {
                    uint8_t b = objPtr[208 + row + col];
                    wchar_t hex[8]{};
                    swprintf(hex, 8, L"%02X ", b);
                    hexLine += hex;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   +{}: {}\n"), row, hexLine);
            }

            showOnScreen(L"BuildNewConstruction probe done (see log)", 5.0f, 0.0f, 1.0f, 0.5f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END PROBE: BuildNewConstruction ===\n"));
        }

        void dumpDebugMenus()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === PROBE: Debug Menus ===\n"));

            const wchar_t* debugMenuClasses[] = {
                    STR("BP_DebugMenu_Recipes_C"),
                    STR("BP_DebugMenu_CraftingAndConstruction_C"),
            };

            for (auto* menuClass : debugMenuClasses)
            {
                std::vector<UObject*> objects;
                UObjectGlobals::FindAllOf(menuClass, objects);

                if (objects.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {} — no instances found, trying CDO...\n"), menuClass);

                    // Try to find the class itself and dump its CDO
                    std::vector<UObject*> allActors;
                    UObjectGlobals::FindAllOf(STR("Actor"), allActors);
                    for (auto* a : allActors)
                    {
                        if (!a) continue;
                        std::wstring clsName = safeClassName(a);
                        if (clsName == menuClass)
                        {
                            objects.push_back(a);
                            break;
                        }
                    }
                }

                for (auto* obj : objects)
                {
                    if (!obj) continue;
                    auto* cls = obj->GetClassPrivate();
                    std::wstring clsName(cls->GetName());
                    std::wstring objName(obj->GetName());

                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Debug Menu: {} | Obj: {}\n"), clsName, objName);

                    // Dump all functions
                    auto* ustruct = static_cast<UStruct*>(cls);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   Functions:\n"));
                    for (auto* func : ustruct->ForEachFunctionInChain())
                    {
                        if (!func) continue;
                        std::wstring funcName(func->GetName());
                        if (funcName.find(STR("ExecuteUbergraph")) == 0) continue;
                        int parmsSize = func->GetParmsSize();
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     fn: {} ({}B)\n"), funcName, parmsSize);
                    }

                    // Dump properties
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   Properties:\n"));
                    for (auto* prop : ustruct->ForEachPropertyInChain())
                    {
                        if (!prop) continue;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     prop: {} @{} size={}\n"),
                                                        std::wstring(prop->GetName()),
                                                        prop->GetOffset_Internal(),
                                                        prop->GetSize());
                    }
                }
            }

            showOnScreen(L"Debug menu probe done (see log)", 5.0f, 0.0f, 1.0f, 0.5f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END PROBE: Debug Menus ===\n"));
        }
#endif

        // Find the MorInventoryComponent on a character
        // ── 7E: Inventory & Toolbar System ────────────────────────────────────
        // Inventory component discovery, toolbar swap (PageDown), clear hotbar
        // BodyInventory stash containers, name-matching resolve phase

        // Finds the MorInventoryComponent on the player character.
        // Searches character's components for class containing "InventoryComponent".
        UObject* findPlayerInventoryComponent(UObject* playerChar)
        {
            if (!playerChar) return nullptr;
            std::vector<UObject*> allComps;
            UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
            for (auto* c : allComps)
            {
                if (!c) continue;
                auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                if (!ownerFunc) continue;
                struct
                {
                    UObject* Ret{nullptr};
                } op{};
                c->ProcessEvent(ownerFunc, &op);
                if (op.Ret != playerChar) continue;
                std::wstring cls = safeClassName(c);
                if (cls == STR("MorInventoryComponent")) return c;
            }
            return nullptr;
        }

        // Discover the EpicPack bag container handle — shared by clearHotbar and swapToolbar
        bool discoverBagHandle(UObject* invComp)
        {
            auto* getContainersFunc = invComp->GetFunctionByNameInChain(STR("GetContainers"));
            if (!getContainersFunc)
            {
                showOnScreen(L"GetContainers not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return false;
            }

            struct ParamInfo
            {
                int offset{-1};
                int size{0};
            };
            auto findParam = [](UFunction* func, const wchar_t* name) -> ParamInfo {
                for (auto* prop : func->ForEachProperty())
                {
                    if (std::wstring(prop->GetName()) == name) return {prop->GetOffset_Internal(), prop->GetSize()};
                }
                return {};
            };

            // Get FItemHandle size from GetItemForHotbarSlot return
            auto* getSlotFunc = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
            int handleSize = getSlotFunc ? findParam(getSlotFunc, L"ReturnValue").size : 0;
            if (handleSize <= 0) handleSize = 20; // fallback

            // Call GetContainers() to find the EpicPack bag
            auto contRet = findParam(getContainersFunc, L"ReturnValue");
            std::vector<uint8_t> contBuf(std::max(getContainersFunc->GetParmsSize() + 32, 64), 0);
            invComp->ProcessEvent(getContainersFunc, contBuf.data());

            uint8_t* contData = *reinterpret_cast<uint8_t**>(contBuf.data() + contRet.offset);
            int32_t contNum = *reinterpret_cast<int32_t*>(contBuf.data() + contRet.offset + 8);

            // Build ID→className map from Items.List
            constexpr int ITEMINSTANCE_SIZE = 0x30;
            constexpr int ITEM_CLASS_OFF = 0x10;
            constexpr int ITEM_ID_OFF = 0x20;
            constexpr int ITEMS_LIST_OFFSET = 0x0330;

            uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
            struct
            {
                uint8_t* Data;
                int32_t Num;
                int32_t Max;
            } itemsList{};
            if (isReadableMemory(invBase + ITEMS_LIST_OFFSET, 16)) std::memcpy(&itemsList, invBase + ITEMS_LIST_OFFSET, 16);

            std::unordered_map<int32_t, std::wstring> idToClass;
            UObject* bodyInvClass = nullptr; // save BodyInventory UClass* for creating stash containers
            if (itemsList.Data && itemsList.Num > 0 && itemsList.Num < 500 && isReadableMemory(itemsList.Data, itemsList.Num * ITEMINSTANCE_SIZE))
            {
                for (int i = 0; i < itemsList.Num; i++)
                {
                    uint8_t* entry = itemsList.Data + i * ITEMINSTANCE_SIZE;
                    int32_t id = *reinterpret_cast<int32_t*>(entry + ITEM_ID_OFF);
                    UObject* cls = *reinterpret_cast<UObject**>(entry + ITEM_CLASS_OFF);
                    if (cls && isReadableMemory(cls, 64))
                    {
                        try
                        {
                            std::wstring name = std::wstring(cls->GetName());
                            idToClass[id] = name;
                            if (name.find(STR("BodyInventory")) != std::wstring::npos) bodyInvClass = cls;
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }

            // Find IHF CDO for GetStorageMaxSlots diagnostic
            if (!m_ihfCDO)
            {
                m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
            }
            UFunction* ihfMaxSlots = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("GetStorageMaxSlots")) : nullptr;
            ParamInfo msItem{}, msRet{};
            if (ihfMaxSlots)
            {
                for (auto* prop : ihfMaxSlots->ForEachProperty())
                {
                    std::wstring pn(prop->GetName());
                    if (pn == L"Item") msItem = {prop->GetOffset_Internal(), prop->GetSize()};
                    if (pn == L"ReturnValue") msRet = {prop->GetOffset_Internal(), prop->GetSize()};
                }
            }

            // Find the EpicPack bag AND BodyInventory container handles
            m_bagHandle.clear();
            m_bodyInvHandle.clear();
            m_bodyInvHandles.clear();
            if (contData && contNum > 0 && contNum < 32 && isReadableMemory(contData, contNum * handleSize))
            {
                for (int i = 0; i < contNum; i++)
                {
                    uint8_t* hPtr = contData + i * handleSize;
                    int32_t cId = *reinterpret_cast<int32_t*>(hPtr);
                    auto it = idToClass.find(cId);
                    std::wstring cName = (it != idToClass.end()) ? it->second : L"(unknown)";

                    // Query max slots via IHF::GetStorageMaxSlots
                    int32_t maxSlots = -1;
                    if (ihfMaxSlots && msItem.offset >= 0 && msRet.offset >= 0)
                    {
                        std::vector<uint8_t> msBuf(std::max(ihfMaxSlots->GetParmsSize() + 32, 64), 0);
                        std::memcpy(msBuf.data() + msItem.offset, hPtr, handleSize);
                        m_ihfCDO->ProcessEvent(ihfMaxSlots, msBuf.data());
                        maxSlots = *reinterpret_cast<int32_t*>(msBuf.data() + msRet.offset);
                    }

                    if (cName.find(STR("EpicPack")) != std::wstring::npos)
                    {
                        m_bagHandle.assign(hPtr, hPtr + handleSize);
                    }
                    if (cName.find(STR("BodyInventory")) != std::wstring::npos)
                    {
                        m_bodyInvHandles.push_back(std::vector<uint8_t>(hPtr, hPtr + handleSize));
                    }
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Container[{}] ID={} slots={} class='{}'\n"), i, cId, maxSlots, cName);
                }
            }

            if (!m_bodyInvHandles.empty())
            {
                m_bodyInvHandle = m_bodyInvHandles[0]; // first = hotbar
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {} BodyInventory containers\n"), m_bodyInvHandles.size());

            // If only 1 BodyInventory (hotbar), create 2 more for toolbar swap stash
            if (m_bodyInvHandles.size() == 1 && bodyInvClass)
            {
                auto* addItemFunc = invComp->GetFunctionByNameInChain(STR("AddItem"));
                if (addItemFunc)
                {
                    auto aiItem = findParam(addItemFunc, L"Item");
                    auto aiCount = findParam(addItemFunc, L"Count");
                    auto aiMethod = findParam(addItemFunc, L"Method");
                    auto aiRet = findParam(addItemFunc, L"ReturnValue");

                    if (aiItem.offset >= 0 && aiCount.offset >= 0 && aiRet.offset >= 0)
                    {
                        for (int i = 0; i < 2; i++)
                        {
                            std::vector<uint8_t> aiBuf(std::max(addItemFunc->GetParmsSize() + 32, 128), 0);
                            *reinterpret_cast<UObject**>(aiBuf.data() + aiItem.offset) = bodyInvClass;
                            *reinterpret_cast<int32_t*>(aiBuf.data() + aiCount.offset) = 1;
                            if (aiMethod.offset >= 0) aiBuf[aiMethod.offset] = 1; // EAddItem::Create
                            invComp->ProcessEvent(addItemFunc, aiBuf.data());

                            // Extract returned FItemHandle
                            std::vector<uint8_t> newHandle(aiBuf.data() + aiRet.offset, aiBuf.data() + aiRet.offset + handleSize);
                            int32_t newId = reinterpret_cast<const FItemHandleLocal*>(newHandle.data())->ID;
                            m_bodyInvHandles.push_back(newHandle);

                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Created stash BodyInventory #{} — ID={}\n"), i + 1, newId);
                        }
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Now have {} BodyInventory containers (1 hotbar + 2 stash)\n"), m_bodyInvHandles.size());
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] AddItem param offsets not found (Item={} Count={} Ret={})\n"),
                                                        aiItem.offset,
                                                        aiCount.offset,
                                                        aiRet.offset);
                    }
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] AddItem function not found on inventory component\n"));
                }
            }
            else if (m_bodyInvHandles.size() == 1)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Only 1 BodyInventory but class not found — cannot create stash\n"));
            }

            if (m_bagHandle.empty())
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Note: EpicPack bag not equipped\n"));
            }
            if (m_bodyInvHandle.empty())
            {
                showOnScreen(L"BodyInventory not found!", 3.0f, 1.0f, 0.3f, 0.3f);
                return false;
            }
            return true;
        }

        // ── Inventory/Hotbar Probe + Clear Hotbar — DISABLED: keybinds removed ──
#if 0
        void probeInventoryHotbar()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === PROBE: Inventory Hotbar ===\n"));

            // --- Step 1: Find the player character (BP_FGKDwarf_C) ---
            UObject* playerChar = nullptr;
            {
                std::vector<UObject*> actors;
                UObjectGlobals::FindAllOf(STR("Character"), actors);
                for (auto* a : actors)
                {
                    if (!a) continue;
                    std::wstring cls = safeClassName(a);
                    if (cls.find(STR("Dwarf")) != std::wstring::npos || cls.find(STR("FGK")) != std::wstring::npos)
                    {
                        playerChar = a;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Found player char: {} '{}'\n"), cls, std::wstring(a->GetName()));
                        break;
                    }
                }
            }
            if (!playerChar)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Player character NOT FOUND\n"));
                showOnScreen(L"Player character not found!", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // --- Step 2: Dump all components on the player character ---
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] --- Player Character Components ---\n"));
            {
                auto* getCompsFunc = playerChar->GetFunctionByNameInChain(STR("K2_GetComponentsByClass"));
                // Also try GetComponents
                if (!getCompsFunc) getCompsFunc = playerChar->GetFunctionByNameInChain(STR("GetComponents"));

                // Direct approach: search all ActorComponents and filter by owner
                std::vector<UObject*> allComps;
                UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
                int compIdx = 0;
                for (auto* c : allComps)
                {
                    if (!c) continue;
                    // Check if this component belongs to our player character
                    auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                    if (!ownerFunc) continue;
                    struct
                    {
                        UObject* Ret{nullptr};
                    } op{};
                    c->ProcessEvent(ownerFunc, &op);
                    if (op.Ret != playerChar) continue;

                    std::wstring compCls = safeClassName(c);
                    std::wstring compName(c->GetName());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   comp[{}]: {} '{}'\n"), compIdx++, compCls, compName);

                    // Check for inventory-related components
                    std::string narrow;
                    for (wchar_t ch : compCls)
                        narrow.push_back(static_cast<char>(std::tolower(ch)));
                    bool isInventory = (narrow.find("inventor") != std::string::npos || narrow.find("storage") != std::string::npos ||
                                        narrow.find("hotbar") != std::string::npos || narrow.find("equip") != std::string::npos ||
                                        narrow.find("item") != std::string::npos || narrow.find("container") != std::string::npos);

                    if (isInventory)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   *** INVENTORY COMPONENT FOUND: {} ***\n"), compCls);

                        // Dump all functions
                        auto* cls = c->GetClassPrivate();
                        if (cls)
                        {
                            auto* ustruct = static_cast<UStruct*>(cls);
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   Functions:\n"));
                            for (auto* func : ustruct->ForEachFunctionInChain())
                            {
                                if (!func) continue;
                                std::wstring fn(func->GetName());
                                if (fn.find(STR("ExecuteUbergraph")) == 0) continue;
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]     fn: {} ({}B)\n"), fn, func->GetParmsSize());
                            }

                            // Dump properties
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   Properties:\n"));
                            for (auto* prop : ustruct->ForEachPropertyInChain())
                            {
                                if (!prop) continue;
                                std::wstring pn(prop->GetName());
                                int off = prop->GetOffset_Internal();
                                int sz = prop->GetSize();

                                // For pointer-sized properties, try to read and identify the value
                                std::wstring extra;
                                if (sz == 8)
                                {
                                    uint8_t* base = reinterpret_cast<uint8_t*>(c);
                                    if (isReadableMemory(base + off, 8))
                                    {
                                        UObject* ptr = *reinterpret_cast<UObject**>(base + off);
                                        if (ptr && isReadableMemory(ptr, 64))
                                        {
                                            try
                                            {
                                                extra = L" -> " + safeClassName(ptr) + L" '" + std::wstring(ptr->GetName()) + L"'";
                                            }
                                            catch (...)
                                            {
                                                extra = L" -> (err)";
                                            }
                                        }
                                        else if (ptr)
                                        {
                                            extra = L" -> (ptr, unreadable)";
                                        }
                                    }
                                }

                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]       prop: {} @{} size={}{}\n"), pn, off, sz, extra);
                            }
                        }
                    }
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Total components on player: {}\n"), compIdx);
            }

            // --- Step 2b: Read hotbar items via GetItemForHotbarSlot ---
            {
                auto* invComp = findPlayerInventoryComponent(playerChar);
                if (invComp)
                {
                    // Read HotbarSize
                    auto* hotbarSizeFunc = invComp->GetFunctionByNameInChain(STR("GetHotbarSize"));
                    int hotbarSize = 8;
                    if (hotbarSizeFunc)
                    {
                        struct
                        {
                            int32_t Ret{0};
                        } hs{};
                        invComp->ProcessEvent(hotbarSizeFunc, &hs);
                        hotbarSize = hs.Ret;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] HotbarSize = {}\n"), hotbarSize);
                    }

                    // Read HotbarEpicItemIndex
                    auto* epicIdxFunc = invComp->GetFunctionByNameInChain(STR("GetHotbarEpicItemIndex"));
                    int epicIdx = -1;
                    if (epicIdxFunc)
                    {
                        struct
                        {
                            int32_t Ret{0};
                        } ei{};
                        invComp->ProcessEvent(epicIdxFunc, &ei);
                        epicIdx = ei.Ret;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] HotbarEpicItemIndex = {}\n"), epicIdx);
                    }

                    // Probe GetItemForHotbarSlot param layout
                    auto* getItemFunc = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
                    if (getItemFunc)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] GetItemForHotbarSlot ({}B) params:\n"), getItemFunc->GetParmsSize());
                        for (auto* prop : getItemFunc->ForEachProperty())
                        {
                            if (!prop) continue;
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   param: {} @{} size={}\n"),
                                                            std::wstring(prop->GetName()),
                                                            prop->GetOffset_Internal(),
                                                            prop->GetSize());
                        }

                        // ── Build ID→name map from ClientItems TArray ──
                        // FItemHandle (from CXXHeaderDump/FGK.hpp):
                        //   int32 ID @0x00, int32 Payload @0x04, TWeakObjectPtr Owner @0x08
                        // FItemInstance (from CXXHeaderDump/FGK.hpp):
                        //   TSubclassOf<AInventoryItem> Item @0x10 (UClass*, 8B)
                        //   int32 Count @0x18, int32 Slot @0x1C, int32 ID @0x20
                        //   float Durability @0x24, int32 RepairCount @0x28
                        // ClientItems TArray<FItemInstance> at UInventoryComponent+0xB8
                        // Each FItemInstance = 0x30 bytes (48)
                        // FItemInstance layout (from CXXHeaderDump/FGK.hpp):
                        //   @0x10: TSubclassOf<AInventoryItem> Item (UClass*, 8B)
                        //   @0x18: int32 Count, @0x1C: int32 Slot, @0x20: int32 ID
                        //   @0x24: float Durability, @0x28: int32 RepairCount
                        // Total: 0x30 = 48 bytes per instance
                        constexpr int ITEMINSTANCE_SIZE = 0x30;
                        constexpr int ITEM_CLASS_OFF = 0x10;
                        constexpr int ITEM_COUNT_OFF = 0x18;
                        constexpr int ITEM_SLOT_OFF = 0x1C;
                        constexpr int ITEM_ID_OFF = 0x20;
                        constexpr int ITEM_DURABILITY_OFF = 0x24;
                        // Items.List TArray<FItemInstance> at UInventoryComponent+0x0220+0x0110 = +0x0330
                        constexpr int ITEMS_LIST_OFFSET = 0x0330;

                        uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);

                        // Read Items.List TArray header (server-side authoritative array)
                        struct
                        {
                            uint8_t* Data;
                            int32_t Num;
                            int32_t Max;
                        } itemsList{};
                        if (isReadableMemory(invBase + ITEMS_LIST_OFFSET, 16))
                        {
                            std::memcpy(&itemsList, invBase + ITEMS_LIST_OFFSET, 16);
                        }
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Items.List TArray @0x330: data={:p} num={} max={}\n"),
                                                        static_cast<void*>(itemsList.Data),
                                                        itemsList.Num,
                                                        itemsList.Max);

                        // Build ID → {className, count, slot, durability} map
                        struct ItemInfo
                        {
                            std::wstring className;
                            int32_t count{0};
                            int32_t slot{0};
                            int32_t id{0};
                            float durability{0.f};
                        };
                        std::unordered_map<int32_t, ItemInfo> idMap;

                        if (itemsList.Data && itemsList.Num > 0 && itemsList.Num < 500)
                        {
                            int totalBytes = itemsList.Num * ITEMINSTANCE_SIZE;
                            if (isReadableMemory(itemsList.Data, totalBytes))
                            {
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Reading {} item instances...\n"), itemsList.Num);
                                for (int i = 0; i < itemsList.Num; i++)
                                {
                                    uint8_t* entry = itemsList.Data + i * ITEMINSTANCE_SIZE;
                                    UObject* itemClass = *reinterpret_cast<UObject**>(entry + ITEM_CLASS_OFF);
                                    int32_t count = *reinterpret_cast<int32_t*>(entry + ITEM_COUNT_OFF);
                                    int32_t slot = *reinterpret_cast<int32_t*>(entry + ITEM_SLOT_OFF);
                                    int32_t id = *reinterpret_cast<int32_t*>(entry + ITEM_ID_OFF);
                                    float durability = *reinterpret_cast<float*>(entry + ITEM_DURABILITY_OFF);

                                    std::wstring clsName = L"(null)";
                                    if (itemClass && isReadableMemory(itemClass, 64))
                                    {
                                        try
                                        {
                                            clsName = std::wstring(itemClass->GetName());
                                        }
                                        catch (...)
                                        {
                                            clsName = L"(err)";
                                        }
                                    }

                                    idMap[id] = {clsName, count, slot, id, durability};
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv]   item[{}]: id={} slot={} count={} dur={:.1f} class='{}'\n"),
                                                                    i,
                                                                    id,
                                                                    slot,
                                                                    count,
                                                                    durability,
                                                                    clsName);
                                }
                            }
                        }

                        // ── Now read each hotbar slot and resolve via the ID map ──
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] === HOTBAR CONTENTS ===\n"));
                        for (int slot = 0; slot <= hotbarSize; slot++)
                        {
                            int querySlot = slot;
                            bool isEpic = false;
                            if (slot == hotbarSize)
                            {
                                if (epicIdx < 0) break;
                                querySlot = epicIdx;
                                isEpic = true;
                            }

                            uint8_t buf[64]{};
                            *reinterpret_cast<int32_t*>(buf) = querySlot;
                            invComp->ProcessEvent(getItemFunc, buf);

                            // FItemHandle ReturnValue @4: {ID, Payload, Owner}
                            auto* itemHandle = reinterpret_cast<const FItemHandleLocal*>(buf + 4);
                            int32_t itemId = itemHandle->ID;
                            int32_t payload = itemHandle->Payload;

                            std::wstring label = isEpic ? std::format(L"EpicSlot[{}]", querySlot) : std::format(L"Slot[{}]", querySlot);

                            if (itemId == 0 && payload == 0)
                            {
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] {}: (empty)\n"), label);
                            }
                            else
                            {
                                auto it = idMap.find(itemId);
                                if (it != idMap.end())
                                {
                                    const auto& info = it->second;
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] {}: '{}' x{} (dur={:.0f}, id={})\n"),
                                                                    label,
                                                                    info.className,
                                                                    info.count,
                                                                    info.durability,
                                                                    itemId);
                                }
                                else
                                {
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] {}: id={} payload={} (NOT in ClientItems)\n"), label, itemId, payload);
                                }
                            }
                        }
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] GetItemForHotbarSlot NOT FOUND\n"));
                    }
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Inv] Could not find MorInventoryComponent\n"));
                }
            }

            showOnScreen(L"Inventory probe done (see UE4SS log)", 5.0f, 0.0f, 1.0f, 0.5f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END PROBE: Inventory Hotbar ===\n"));
        }

        // ── Clear Hotbar: move slots 0-7 to body inventory (one per frame) ──
        void clearHotbar()
        {
            if (m_clearingHotbar)
            {
                showOnScreen(L"Already clearing hotbar...", 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }
            if (m_swap.active)
            {
                showOnScreen(L"Wait for toolbar swap to finish", 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            // Find player + inventory to discover bag container once
            UObject* playerChar = nullptr;
            {
                std::vector<UObject*> actors;
                UObjectGlobals::FindAllOf(STR("Character"), actors);
                for (auto* a : actors)
                {
                    if (!a) continue;
                    if (safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                    {
                        playerChar = a;
                        break;
                    }
                }
            }
            if (!playerChar)
            {
                showOnScreen(L"Player not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            auto* invComp = findPlayerInventoryComponent(playerChar);
            if (!invComp)
            {
                showOnScreen(L"Inventory not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            if (!discoverBagHandle(invComp)) return;
            if (m_bagHandle.empty())
            {
                showOnScreen(L"Equip a bag first!", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            int32_t bagId = reinterpret_cast<const FItemHandleLocal*>(m_bagHandle.data())->ID;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === CLEAR HOTBAR → bag id={} ===\n"), bagId);
            m_clearingHotbar = true;
            m_clearHotbarCount = 0;
            m_clearHotbarDropped = 0;
            m_clearHotbarWait = 0;
            showOnScreen(L"Clearing hotbar...", 3.0f, 0.0f, 1.0f, 0.5f);
        }

        // Called from on_update() — moves one hotbar item per tick to the EpicPack bag
        void clearHotbarTick()
        {
            if (!m_clearingHotbar) return;
            if (m_clearHotbarWait > 0)
            {
                m_clearHotbarWait--;
                return;
            }
            if (m_clearHotbarCount + m_clearHotbarDropped >= 8)
            {
                std::wstring msg = std::format(L"Hotbar cleared: {} to bag, {} dropped", m_clearHotbarCount, m_clearHotbarDropped);
                showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
                m_clearingHotbar = false;
                return;
            }

            // Find player + inventory (lightweight per-tick lookup)
            UObject* playerChar = nullptr;
            {
                std::vector<UObject*> actors;
                UObjectGlobals::FindAllOf(STR("Character"), actors);
                for (auto* a : actors)
                {
                    if (!a) continue;
                    if (safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                    {
                        playerChar = a;
                        break;
                    }
                }
            }
            if (!playerChar)
            {
                m_clearingHotbar = false;
                return;
            }

            auto* invComp = findPlayerInventoryComponent(playerChar);
            if (!invComp)
            {
                m_clearingHotbar = false;
                return;
            }

            auto* getSlotFunc = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
            auto* canMoveFunc = invComp->GetFunctionByNameInChain(STR("CanMoveItem"));
            auto* moveFunc = invComp->GetFunctionByNameInChain(STR("MoveItem"));
            auto* dropFunc = invComp->GetFunctionByNameInChain(STR("DropItem"));
            if (!getSlotFunc || !moveFunc || !dropFunc)
            {
                m_clearingHotbar = false;
                return;
            }

            struct ParamInfo
            {
                int offset{-1};
                int size{0};
            };
            auto findParam = [](UFunction* func, const wchar_t* name) -> ParamInfo {
                for (auto* prop : func->ForEachProperty())
                {
                    if (std::wstring(prop->GetName()) == name) return {prop->GetOffset_Internal(), prop->GetSize()};
                }
                return {};
            };

            auto slotInput = findParam(getSlotFunc, L"HotbarIndex");
            auto slotRet = findParam(getSlotFunc, L"ReturnValue");
            auto mItem = findParam(moveFunc, L"Item");
            auto mDest = findParam(moveFunc, L"Destination");
            int handleSize = slotRet.size;

            // Scan for first non-empty hotbar slot
            for (int slot = 0; slot < 8; slot++)
            {
                std::vector<uint8_t> slotBuf(std::max(getSlotFunc->GetParmsSize() + 32, 64), 0);
                *reinterpret_cast<int32_t*>(slotBuf.data() + slotInput.offset) = slot;
                invComp->ProcessEvent(getSlotFunc, slotBuf.data());

                int32_t itemId = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + slotRet.offset)->ID;
                if (itemId == 0) continue;

                // Check if bag has room (CanMoveItem)
                bool canMove = true;
                if (canMoveFunc)
                {
                    auto ci = findParam(canMoveFunc, L"Item");
                    auto cd = findParam(canMoveFunc, L"Destination");
                    auto cr = findParam(canMoveFunc, L"ReturnValue");
                    if (ci.offset >= 0 && cd.offset >= 0 && cr.offset >= 0)
                    {
                        std::vector<uint8_t> canBuf(std::max(canMoveFunc->GetParmsSize() + 32, 128), 0);
                        std::memcpy(canBuf.data() + ci.offset, slotBuf.data() + slotRet.offset, handleSize);
                        std::memcpy(canBuf.data() + cd.offset, m_bagHandle.data(), handleSize);
                        invComp->ProcessEvent(canMoveFunc, canBuf.data());
                        canMove = canBuf[cr.offset] != 0;
                    }
                }

                if (canMove)
                {
                    // Move to bag
                    std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                    std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + slotRet.offset, handleSize);
                    std::memcpy(moveBuf.data() + mDest.offset, m_bagHandle.data(), handleSize);
                    invComp->ProcessEvent(moveFunc, moveBuf.data());
                    m_clearHotbarCount++;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Slot {} (id={}) → bag\n"), slot, itemId);
                }
                else
                {
                    // Bag full — drop on ground
                    auto di = findParam(dropFunc, L"Item");
                    auto dc = findParam(dropFunc, L"Count");
                    std::vector<uint8_t> dropBuf(std::max(dropFunc->GetParmsSize() + 32, 128), 0);
                    std::memcpy(dropBuf.data() + di.offset, slotBuf.data() + slotRet.offset, handleSize);
                    *reinterpret_cast<int32_t*>(dropBuf.data() + dc.offset) = 999999;
                    invComp->ProcessEvent(dropFunc, dropBuf.data());
                    m_clearHotbarDropped++;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Slot {} (id={}) → dropped (bag full)\n"), slot, itemId);
                }

                m_clearHotbarWait = 3;
                return; // one item per tick
            }

            // All slots empty — done
            std::wstring msg = std::format(L"Hotbar cleared: {} to bag, {} dropped", m_clearHotbarCount, m_clearHotbarDropped);
            showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {}\n"), msg);
            m_clearingHotbar = false;
        }
#endif

        // ── Patch DT_Storage: expand BodyInventory from 8x1 to 8x3 ──
        void patchBodyInventoryStorage()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Patching DT_Storage Dwarf.BodyInventory...\n"));

            // Find all DataTable objects and look for DT_Storage
            std::vector<UObject*> dataTables;
            UObjectGlobals::FindAllOf(STR("DataTable"), dataTables);

            UObject* dtStorage = nullptr;
            for (auto* dt : dataTables)
            {
                if (!dt) continue;
                try
                {
                    std::wstring name(dt->GetName());
                    if (name.find(STR("DT_Storage")) != std::wstring::npos)
                    {
                        dtStorage = dt;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found DataTable: '{}'\n"), name);
                        break;
                    }
                }
                catch (...)
                {
                    continue;
                }
            }

            if (!dtStorage)
            {
                // Only log once to avoid spam
                static bool logged = false;
                if (!logged)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] DT_Storage not found yet (will retry)\n"));
                    logged = true;
                }
                return;
            }

            // DataTable RowMap is at offset 0x30: TSet<TPair<FName, uint8*>>
            // TSet starts with TSparseArray which starts with TArray
            // TArray: { Data*, Num, Max }
            // Each element: FSetElement<TPair<FName, uint8*>> = FName(8) + uint8*(8) + HashNextId(4) + HashIndex(4) = 24 bytes
            // LINT NOTE (#11 — TSet iteration safety): Analyzed and intentionally skipped. These DataTable
            // RowMaps are static (loaded once at startup, never modified at runtime). All iteration is
            // read-only on the game thread. Copying to a local vector would require raw FSetElement
            // deserialization — adding risk (FName safety) with zero benefit.
            uint8_t* dtBase = reinterpret_cast<uint8_t*>(dtStorage);
            constexpr int ROWMAP_OFFSET = 0x30;
            constexpr int SET_ELEMENT_SIZE = 24; // FName(8) + ptr(8) + hash(4) + hash(4)
            constexpr int FNAME_SIZE = 8;

            struct
            {
                uint8_t* Data;
                int32_t Num;
                int32_t Max;
            } elemArray{};
            if (!isReadableMemory(dtBase + ROWMAP_OFFSET, 16))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] RowMap not readable at 0x30\n"));
                return;
            }
            std::memcpy(&elemArray, dtBase + ROWMAP_OFFSET, 16);

            if (!elemArray.Data || elemArray.Num <= 0 || elemArray.Num > 1000)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] RowMap TArray invalid: Num={}, Max={}\n"), elemArray.Num, elemArray.Max);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] RowMap has {} entries\n"), elemArray.Num);

            // Construct target FName for comparison
            FName targetName(STR("Dwarf.BodyInventory"));

            // Iterate TSet elements to find the matching row
            uint8_t* foundRow = nullptr;
            for (int i = 0; i < elemArray.Num; i++)
            {
                uint8_t* elem = elemArray.Data + i * SET_ELEMENT_SIZE;
                if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;

                // First 8 bytes = FName (ComparisonIndex:4 + Number:4)
                FName entryName;
                std::memcpy(&entryName, elem, FNAME_SIZE);

                if (entryName == targetName)
                {
                    // Found it! Next 8 bytes = uint8_t* row data pointer
                    uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + FNAME_SIZE);
                    if (rowData && isReadableMemory(rowData, 0x58))
                    {
                        foundRow = rowData;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found Dwarf.BodyInventory row at entry {}\n"), i);
                    }
                    break;
                }
            }

            if (!foundRow)
            {
                // Fallback: log all row names for debugging
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Dwarf.BodyInventory not found. Logging row names:\n"));
                for (int i = 0; i < std::min(elemArray.Num, 30); i++)
                {
                    uint8_t* elem = elemArray.Data + i * SET_ELEMENT_SIZE;
                    if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;
                    FName entryName;
                    std::memcpy(&entryName, elem, FNAME_SIZE);
                    try
                    {
                        auto nameStr = entryName.ToString();
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   Row[{}]: '{}'\n"), i, nameStr);
                    }
                    catch (...)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   Row[{}]: (FName unreadable)\n"), i);
                    }
                }
                return;
            }

            // FMorStorageDefinition layout:
            //   0x004C: InventoryWidth (int32)
            //   0x0050: InventoryHeight (int32)
            int32_t* widthPtr = reinterpret_cast<int32_t*>(foundRow + 0x4C);
            int32_t* heightPtr = reinterpret_cast<int32_t*>(foundRow + 0x50);

            int32_t oldWidth = *widthPtr;
            int32_t oldHeight = *heightPtr;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Current: InventoryWidth={}, InventoryHeight={}\n"), oldWidth, oldHeight);

            // Patch to 8x3 so hidden toolbar slots exist without disturbing slot 8 (epic).
            // Row 0 = hotbar (0-7), rows 1-2 = extra space for the game.
            // Sentinel slots 100-107 / 200-207 live in Items.List regardless of grid dims.
            int32_t newWidth = 8;
            int32_t newHeight = 3;
            if (oldHeight < newHeight)
            {
                *widthPtr = newWidth;
                *heightPtr = newHeight;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Patched BodyInventory: {}x{} → {}x{}\n"), oldWidth, oldHeight, newWidth, newHeight);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BodyInventory already {}x{} (h >= {}), no patch\n"), oldWidth, oldHeight, newHeight);
            }
            m_storagePatched = true;
        }

        // ── Toolbar Swap: F12 — 2 toolbars via BodyInventory containers ──
        // m_bodyInvHandles[0] = hotbar, [1] = T1 stash, [2] = T2 stash
        // Phase 0: MoveItem(hotbar items → stash container) using GetItemForHotbarSlot
        // Phase 1: MoveItem(stash items → hotbar) using IHF::GetItemForSlot on stash
        static constexpr int NUM_TOOLBARS = 2;
        static constexpr int TOOLBAR_SLOTS = 8;
        int m_activeToolbar{0}; // 0 or 1 — which toolbar is currently visible

        // Swap state machine (multi-frame, one move per tick)
        struct SwapState
        {
            bool active{false};
            bool resolved{false}; // true after name-matching resolve phase
            bool cleared{false};  // true after EmptyContainer on stash destination
            int phase{0};         // 0 = stash hotbar→container, 1 = restore container→hotbar
            int slot{0};          // current slot being processed (0-7)
            int moved{0};         // items successfully moved
            int wait{0};          // frame delay between operations
            int nextTB{0};        // which toolbar we're switching TO
            int curTB{0};         // which toolbar we're switching FROM
            int stashIdx{-1};     // resolved container index for stashing (1 or 2)
            int restoreIdx{-1};   // resolved container index for restoring (1 or 2)
        };
        SwapState m_swap{};

        std::vector<uint8_t> m_bodyInvHandle;               // hotbar container handle
        std::vector<std::vector<uint8_t>> m_bodyInvHandles; // all BodyInventory handles
        UObject* m_ihfCDO{nullptr};                         // UItemHandleFunctions CDO
        UObject* m_dropItemMgr{nullptr};                    // BP_DropItemManager for GetNameForItemHandle

        // Initiates toolbar swap (PageDown). Uses 2 BodyInventory containers as
        // stash buffers. Name-matching resolve phase determines which container
        // holds stale copies vs. the other toolbar's items. 3-phase state machine:
        //   Resolve → Phase 0 (stash hotbar→container) → Phase 1 (restore container→hotbar)
        void swapToolbar()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] === swapToolbar() called ===\n"));
            try
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] State: active={} clearing={} bodyInvHandle.empty={} handles.size={} charLoaded={}\n"),
                                                m_swap.active,
                                                m_clearingHotbar,
                                                m_bodyInvHandle.empty(),
                                                m_bodyInvHandles.size(),
                                                m_characterLoaded);

                if (m_swap.active)
                {
                    showOnScreen(L"Swap already in progress...", 2.0f, 1.0f, 1.0f, 0.0f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] BLOCKED: swap already active\n"));
                    return;
                }
                if (m_clearingHotbar)
                {
                    showOnScreen(L"Wait for hotbar clear to finish", 2.0f, 1.0f, 1.0f, 0.0f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] BLOCKED: hotbar clear in progress\n"));
                    return;
                }

                // Discover container handles if not cached
                if (m_bodyInvHandle.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] No cached handles, running discovery...\n"));
                    UObject* playerChar = nullptr;
                    {
                        std::vector<UObject*> actors;
                        UObjectGlobals::FindAllOf(STR("Character"), actors);
                        for (auto* a : actors)
                        {
                            if (a && safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                            {
                                playerChar = a;
                                break;
                            }
                        }
                    }
                    if (playerChar)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Player found, discovering containers...\n"));
                        auto* invComp = findPlayerInventoryComponent(playerChar);
                        if (invComp)
                        {
                            discoverBagHandle(invComp);
                        }
                        else
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] FAIL: findPlayerInventoryComponent returned null\n"));
                        }
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] FAIL: Dwarf character not found in actors\n"));
                    }
                }
                if (m_bodyInvHandle.empty())
                {
                    showOnScreen(L"BodyInventory not found!", 3.0f, 1.0f, 0.3f, 0.3f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] FAIL: m_bodyInvHandle still empty after discovery\n"));
                    return;
                }
                // Need at least 3 BodyInventory containers: [0]=hotbar, [1]=T1 stash, [2]=T2 stash
                if (m_bodyInvHandles.size() < 3)
                {
                    showOnScreen(std::format(L"Need 3 BodyInventory containers, found {}", m_bodyInvHandles.size()), 3.0f, 1.0f, 0.3f, 0.3f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] FAIL: only {} BodyInventory containers (need 3)\n"), m_bodyInvHandles.size());
                    return;
                }

                // Log handle IDs for debugging
                for (size_t hi = 0; hi < m_bodyInvHandles.size(); hi++)
                {
                    int32_t hid = reinterpret_cast<const FItemHandleLocal*>(m_bodyInvHandles[hi].data())->ID;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Handle[{}] ID={} size={}\n"), hi, hid, m_bodyInvHandles[hi].size());
                }

                int curTB = m_activeToolbar;
                int nextTB = 1 - curTB; // toggle 0↔1

                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] toolbar swap: T{} -> T{} (stash->container[{}], restore<-container[{}])\n"),
                                                curTB + 1,
                                                nextTB + 1,
                                                curTB + 1,
                                                nextTB + 1);

                m_swap = {};
                m_swap.active = true;
                m_swap.resolved = false;
                m_swap.phase = 0;
                m_swap.slot = 0;
                m_swap.moved = 0;
                m_swap.wait = 0;
                m_swap.curTB = curTB;
                m_swap.nextTB = nextTB;
                m_swap.stashIdx = curTB + 1;    // default, may be overridden by resolve
                m_swap.restoreIdx = nextTB + 1; // default, may be overridden by resolve

                showOnScreen(std::format(L"Swapping to Toolbar {}...", nextTB + 1), 2.0f, 0.0f, 1.0f, 0.5f);
            }
            catch (const std::exception&)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] EXCEPTION in swapToolbar()\n"));
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] UNKNOWN EXCEPTION in swapToolbar()\n"));
            }
        }

        // Called from on_update() — processes one move per tick.
        // Phase 0: Move hotbar items → stash BodyInventory container[curTB+1]
        // Phase 1: Move stash items from BodyInventory container[nextTB+1] → hotbar
        void swapToolbarTick()
        {
            if (!m_swap.active) return;
            if (m_swap.wait > 0)
            {
                m_swap.wait--;
                return;
            }

            try
            {
                // Find player + inventory each tick
                UObject* playerChar = nullptr;
                {
                    std::vector<UObject*> actors;
                    UObjectGlobals::FindAllOf(STR("Character"), actors);
                    for (auto* a : actors)
                    {
                        if (!a) continue;
                        if (safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                        {
                            playerChar = a;
                            break;
                        }
                    }
                }
                if (!playerChar)
                {
                    m_swap.active = false;
                    return;
                }
                auto* invComp = findPlayerInventoryComponent(playerChar);
                if (!invComp)
                {
                    m_swap.active = false;
                    return;
                }

                // Look up functions
                auto* getSlotFunc = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
                auto* moveFunc = invComp->GetFunctionByNameInChain(STR("MoveItem"));

                if (!getSlotFunc || !moveFunc)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Swap: functions missing (GetItemForHotbarSlot={} MoveItem={})\n"),
                                                    getSlotFunc != nullptr,
                                                    moveFunc != nullptr);
                    m_swap.active = false;
                    return;
                }

                // Find IHF CDO for phase 1 (scanning stash container)
                if (!m_ihfCDO)
                {
                    m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
                    if (m_ihfCDO)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found IHF CDO: '{}'\n"), std::wstring(m_ihfCDO->GetName()));
                    }
                }

                struct ParamInfo
                {
                    int offset{-1};
                    int size{0};
                };
                auto findParam = [](UFunction* func, const wchar_t* name) -> ParamInfo {
                    for (auto* prop : func->ForEachProperty())
                    {
                        if (std::wstring(prop->GetName()) == name) return {prop->GetOffset_Internal(), prop->GetSize()};
                    }
                    return {};
                };

                auto slotInput = findParam(getSlotFunc, L"HotbarIndex");
                auto slotRet = findParam(getSlotFunc, L"ReturnValue");
                auto mItem = findParam(moveFunc, L"Item");
                auto mDest = findParam(moveFunc, L"Destination");
                int handleSize = slotRet.size;
                if (handleSize <= 0) handleSize = 20;

                auto& hotbarContainer = m_bodyInvHandles[0];

                // ── Resolve Phase: determine stash/restore containers via item name matching ──
                if (!m_swap.resolved)
                {
                    m_swap.resolved = true;

                    // Find DropItemManager for GetNameForItemHandle
                    if (!m_dropItemMgr)
                    {
                        std::vector<UObject*> actors;
                        UObjectGlobals::FindAllOf(STR("BP_DropItemManager_C"), actors);
                        if (!actors.empty()) m_dropItemMgr = actors[0];
                    }
                    // Find IHF CDO if needed
                    if (!m_ihfCDO)
                    {
                        m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
                    }

                    if (!m_dropItemMgr || !m_ihfCDO)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Resolve: DropItemMgr={} IHF={} — using default mapping stash=[{}] restore=[{}]\n"),
                                                        m_dropItemMgr != nullptr,
                                                        m_ihfCDO != nullptr,
                                                        m_swap.stashIdx,
                                                        m_swap.restoreIdx);
                        m_swap.wait = 1;
                    }
                    else
                    {
                        auto* getNameFunc = m_dropItemMgr->GetFunctionByNameInChain(STR("GetNameForItemHandle"));
                        auto* ihfGetSlot = m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot"));
                        auto* ihfIsValid = m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem"));

                        if (!getNameFunc || !ihfGetSlot || !ihfIsValid)
                        {
                            Output::send<LogLevel::Warning>(
                                    STR("[MoriaCppMod] Resolve: missing functions (getName={} getSlot={} isValid={}) — using default\n"),
                                    getNameFunc != nullptr,
                                    ihfGetSlot != nullptr,
                                    ihfIsValid != nullptr);
                            m_swap.wait = 1;
                        }
                        else
                        {
                            auto gnItem = findParam(getNameFunc, L"Item");
                            auto gnRet = findParam(getNameFunc, L"ReturnValue");

                            auto rgsItem = findParam(ihfGetSlot, L"Item");
                            auto rgsSlot = findParam(ihfGetSlot, L"Slot");
                            auto rgsRet = findParam(ihfGetSlot, L"ReturnValue");
                            auto rivItem = findParam(ihfIsValid, L"Item");
                            auto rivRet = findParam(ihfIsValid, L"ReturnValue");

                            // Helper: get FName ComparisonIndex for an item handle
                            auto getNameCI = [&](const uint8_t* handleData) -> int32_t {
                                std::vector<uint8_t> buf(std::max(getNameFunc->GetParmsSize() + 32, 64), 0);
                                std::memcpy(buf.data() + gnItem.offset, handleData, handleSize);
                                m_dropItemMgr->ProcessEvent(getNameFunc, buf.data());
                                return *reinterpret_cast<int32_t*>(buf.data() + gnRet.offset);
                            };

                            // Helper: try to get FName string for logging (best-effort)
                            auto getNameStr = [&](const uint8_t* handleData) -> std::wstring {
                                try
                                {
                                    std::vector<uint8_t> buf(std::max(getNameFunc->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(buf.data() + gnItem.offset, handleData, handleSize);
                                    m_dropItemMgr->ProcessEvent(getNameFunc, buf.data());
                                    FName* fn = reinterpret_cast<FName*>(buf.data() + gnRet.offset);
                                    return fn->ToString();
                                }
                                catch (...)
                                {
                                    return L"???";
                                }
                            };

                            // Collect hotbar item names
                            std::vector<int32_t> hotbarCIs;
                            for (int s = 0; s < TOOLBAR_SLOTS; s++)
                            {
                                std::vector<uint8_t> sb(std::max(getSlotFunc->GetParmsSize() + 32, 64), 0);
                                *reinterpret_cast<int32_t*>(sb.data() + slotInput.offset) = s;
                                invComp->ProcessEvent(getSlotFunc, sb.data());
                                auto* slotHandle = reinterpret_cast<const FItemHandleLocal*>(sb.data() + slotRet.offset);
                                if (slotHandle->ID != 0)
                                {
                                    int32_t ci = getNameCI(sb.data() + slotRet.offset);
                                    hotbarCIs.push_back(ci);
                                    auto nameStr = getNameStr(sb.data() + slotRet.offset);
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Resolve: hotbar[{}] id={} name={}\n"), s, slotHandle->ID, nameStr);
                                }
                            }

                            // Helper: collect names from a stash container
                            auto collectContainerCIs = [&](int cIdx) -> std::vector<int32_t> {
                                std::vector<int32_t> cis;
                                for (int s = 0; s < TOOLBAR_SLOTS; s++)
                                {
                                    std::vector<uint8_t> gb(std::max(ihfGetSlot->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(gb.data() + rgsItem.offset, m_bodyInvHandles[cIdx].data(), handleSize);
                                    *reinterpret_cast<int32_t*>(gb.data() + rgsSlot.offset) = s;
                                    m_ihfCDO->ProcessEvent(ihfGetSlot, gb.data());

                                    std::vector<uint8_t> vb(std::max(ihfIsValid->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(vb.data() + rivItem.offset, gb.data() + rgsRet.offset, handleSize);
                                    m_ihfCDO->ProcessEvent(ihfIsValid, vb.data());
                                    if (vb[rivRet.offset] != 0)
                                    {
                                        int32_t ci = getNameCI(gb.data() + rgsRet.offset);
                                        cis.push_back(ci);
                                        auto* cHandle = reinterpret_cast<const FItemHandleLocal*>(gb.data() + rgsRet.offset);
                                        auto nameStr = getNameStr(gb.data() + rgsRet.offset);
                                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Resolve: container[{}][{}] id={} name={}\n"), cIdx, s, cHandle->ID, nameStr);
                                    }
                                }
                                return cis;
                            };

                            auto c1CIs = collectContainerCIs(1);
                            auto c2CIs = collectContainerCIs(2);

                            // Count matches: how many hotbar names appear in each container
                            int c1Match = 0, c2Match = 0;
                            for (auto ci : hotbarCIs)
                            {
                                for (auto n : c1CIs)
                                    if (n == ci)
                                    {
                                        c1Match++;
                                        break;
                                    }
                                for (auto n : c2CIs)
                                    if (n == ci)
                                    {
                                        c2Match++;
                                        break;
                                    }
                            }

                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Resolve: hotbar={} items, c[1]={} items ({} match), c[2]={} items ({} match)\n"),
                                                            hotbarCIs.size(),
                                                            c1CIs.size(),
                                                            c1Match,
                                                            c2CIs.size(),
                                                            c2Match);

                            // Determine mapping based on matches
                            if (c1Match > c2Match && c1Match > 0)
                            {
                                // Container[1] matches hotbar → stale copies → stash to [1], restore from [2]
                                m_swap.stashIdx = 1;
                                m_swap.restoreIdx = 2;
                            }
                            else if (c2Match > c1Match && c2Match > 0)
                            {
                                // Container[2] matches hotbar → stale copies → stash to [2], restore from [1]
                                m_swap.stashIdx = 2;
                                m_swap.restoreIdx = 1;
                            }
                            else if (c1CIs.empty() && !c2CIs.empty())
                            {
                                // Container[1] empty, [2] has items → stash to [1], restore from [2]
                                m_swap.stashIdx = 1;
                                m_swap.restoreIdx = 2;
                            }
                            else if (c2CIs.empty() && !c1CIs.empty())
                            {
                                // Container[2] empty, [1] has items → stash to [2], restore from [1]
                                m_swap.stashIdx = 2;
                                m_swap.restoreIdx = 1;
                            }
                            // else: both empty or tied — keep default mapping

                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Resolve: RESULT stashIdx={} restoreIdx={}\n"), m_swap.stashIdx, m_swap.restoreIdx);
                            m_swap.wait = 1;
                        }
                    }
                    return;
                }

                auto& stashContainer = m_bodyInvHandles[m_swap.stashIdx];
                auto& restoreContainer = m_bodyInvHandles[m_swap.restoreIdx];

                // ── Phase 0: Move hotbar items → stash container ──
                if (m_swap.phase == 0)
                {
                    // Clear stash destination if it has stale items (persisted from previous session)
                    if (!m_swap.cleared)
                    {
                        m_swap.cleared = true;
                        auto* emptyFunc = invComp->GetFunctionByNameInChain(STR("EmptyContainer"));
                        if (emptyFunc)
                        {
                            auto emptyItem = findParam(emptyFunc, L"Item");
                            if (emptyItem.offset >= 0)
                            {
                                std::vector<uint8_t> emptyBuf(std::max(emptyFunc->GetParmsSize() + 32, 64), 0);
                                std::memcpy(emptyBuf.data() + emptyItem.offset, stashContainer.data(), handleSize);
                                invComp->ProcessEvent(emptyFunc, emptyBuf.data());
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: EmptyContainer([{}]) — clearing stale items before stash\n"),
                                                                m_swap.stashIdx);
                            }
                        }
                        m_swap.wait = 3;
                        return; // give a tick for empty to process
                    }

                    for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                    {
                        std::vector<uint8_t> slotBuf(std::max(getSlotFunc->GetParmsSize() + 32, 64), 0);
                        *reinterpret_cast<int32_t*>(slotBuf.data() + slotInput.offset) = slot;
                        invComp->ProcessEvent(getSlotFunc, slotBuf.data());

                        auto* stashHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + slotRet.offset);
                        if (stashHandle->ID == 0)
                        {
                            m_swap.slot = slot + 1;
                            continue;
                        }

                        std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                        std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + slotRet.offset, handleSize);
                        std::memcpy(moveBuf.data() + mDest.offset, stashContainer.data(), handleSize);
                        invComp->ProcessEvent(moveFunc, moveBuf.data());

                        m_swap.moved++;
                        m_swap.slot = slot + 1;

                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: hotbar[{}] id={} -> container[{}]\n"), slot, stashHandle->ID, m_swap.stashIdx);

                        m_swap.wait = 3;
                        return;
                    }

                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0 done: stashed {} items. Restoring from container[{}]\n"),
                                                    m_swap.moved,
                                                    m_swap.restoreIdx);

                    m_swap.phase = 1;
                    m_swap.slot = 0;
                    m_swap.wait = 3;
                    return;
                }

                // ── Phase 1: Move items from restore container → hotbar ──
                if (m_swap.phase == 1)
                {
                    if (!m_ihfCDO)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: IHF CDO not found\n"));
                        m_swap.active = false;
                        return;
                    }

                    auto* ihfGetSlot = m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot"));
                    auto* ihfIsValid = m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem"));
                    if (!ihfGetSlot || !ihfIsValid)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: IHF functions missing\n"));
                        m_swap.active = false;
                        return;
                    }

                    auto gsItem = findParam(ihfGetSlot, L"Item");
                    auto gsSlot = findParam(ihfGetSlot, L"Slot");
                    auto gsRet = findParam(ihfGetSlot, L"ReturnValue");
                    auto ivItem = findParam(ihfIsValid, L"Item");
                    auto ivRet = findParam(ihfIsValid, L"ReturnValue");

                    for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                    {
                        std::vector<uint8_t> gsBuf(std::max(ihfGetSlot->GetParmsSize() + 32, 64), 0);
                        std::memcpy(gsBuf.data() + gsItem.offset, restoreContainer.data(), handleSize);
                        *reinterpret_cast<int32_t*>(gsBuf.data() + gsSlot.offset) = slot;
                        m_ihfCDO->ProcessEvent(ihfGetSlot, gsBuf.data());

                        std::vector<uint8_t> ivBuf(std::max(ihfIsValid->GetParmsSize() + 32, 64), 0);
                        std::memcpy(ivBuf.data() + ivItem.offset, gsBuf.data() + gsRet.offset, handleSize);
                        m_ihfCDO->ProcessEvent(ihfIsValid, ivBuf.data());
                        bool isItem = ivBuf[ivRet.offset] != 0;

                        if (!isItem)
                        {
                            m_swap.slot = slot + 1;
                            continue;
                        }

                        auto* restHandle = reinterpret_cast<const FItemHandleLocal*>(gsBuf.data() + gsRet.offset);

                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: container[{}][{}] id={} -> hotbar\n"), m_swap.restoreIdx, slot, restHandle->ID);

                        std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                        std::memcpy(moveBuf.data() + mItem.offset, gsBuf.data() + gsRet.offset, handleSize);
                        std::memcpy(moveBuf.data() + mDest.offset, hotbarContainer.data(), handleSize);
                        invComp->ProcessEvent(moveFunc, moveBuf.data());
                        m_swap.moved++;

                        m_swap.slot = slot + 1;
                        m_swap.wait = 3;
                        return;
                    }

                    // Phase 1 complete — empty the restore container for cleanup
                    auto* emptyFunc = invComp->GetFunctionByNameInChain(STR("EmptyContainer"));
                    if (emptyFunc)
                    {
                        auto emptyItem = findParam(emptyFunc, L"Item");
                        if (emptyItem.offset >= 0)
                        {
                            std::vector<uint8_t> emptyBuf(std::max(emptyFunc->GetParmsSize() + 32, 64), 0);
                            std::memcpy(emptyBuf.data() + emptyItem.offset, restoreContainer.data(), handleSize);
                            invComp->ProcessEvent(emptyFunc, emptyBuf.data());
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: EmptyContainer([{}]) — cleanup after restore\n"), m_swap.restoreIdx);
                        }
                    }

                    // Swap done
                    m_activeToolbar = m_swap.nextTB;
                    s_overlay.activeToolbar = m_swap.nextTB;
                    s_overlay.needsUpdate = true;

                    std::wstring msg = std::format(L"Toolbar {} active ({} items moved)", m_swap.nextTB + 1, m_swap.moved);
                    showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {}\n"), msg);
                    m_swap.active = false;
                    refreshActionBar();
                    return;
                }
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] EXCEPTION in swapToolbarTick()\n"));
                m_swap.active = false;
            }
        }

        void toggleBuildHUD()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === Toggle Build HUD ===\n"));

            // Find WBP_MoriaHUD_C
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            UObject* moriaHUD = nullptr;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring clsName = safeClassName(w);
                if (clsName == STR("WBP_MoriaHUD_C"))
                {
                    // Check if visible
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        w->ProcessEvent(visFunc, &vp);
                        if (vp.Ret)
                        {
                            moriaHUD = w;
                            break;
                        }
                    }
                }
            }

            if (!moriaHUD)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] WBP_MoriaHUD_C NOT FOUND (visible)\n"));
                showOnScreen(L"MoriaHUD NOT FOUND", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // First probe BuildHUDShow params
            auto* showFunc = moriaHUD->GetFunctionByNameInChain(STR("BuildHUDShow"));
            auto* hideFunc = moriaHUD->GetFunctionByNameInChain(STR("BuildHUDHide"));

            if (showFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BuildHUDShow params ({}B):\n"), showFunc->GetParmsSize());
                for (auto* prop : showFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }

                // Try calling it with zeroed params
                int parmsSize = showFunc->GetParmsSize();
                std::vector<uint8_t> buf(parmsSize, 0);
                moriaHUD->ProcessEvent(showFunc, buf.data());
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called BuildHUDShow!\n"));
                showOnScreen(L"BuildHUDShow called!", 3.0f, 0.0f, 1.0f, 0.0f);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BuildHUDShow NOT FOUND\n"));
            }

            if (hideFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BuildHUDHide params ({}B):\n"), hideFunc->GetParmsSize());
                for (auto* prop : hideFunc->ForEachProperty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END Toggle Build HUD ===\n"));
        }

        // ── Debug Cheat Functions ──
        // Calls a named 0-param function on a debug menu actor.
        // Also tries direct class search (faster than scanning all Actor instances).
        bool callDebugFunc(const wchar_t* actorClass, const wchar_t* funcName)
        {
            // Try direct class search first (faster and more reliable)
            {
                std::vector<UObject*> directObjs;
                UObjectGlobals::FindAllOf(actorClass, directObjs);
                if (!directObjs.empty())
                {
                    for (auto* a : directObjs)
                    {
                        if (!a) continue;
                        auto* fn = a->GetFunctionByNameInChain(funcName);
                        if (fn)
                        {
                            a->ProcessEvent(fn, nullptr);
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called {}::{} (direct find)\n"),
                                                            std::wstring(actorClass), std::wstring(funcName));
                            return true;
                        }
                    }
                }
            }

            // Fallback: scan all actors by class name
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);
            for (auto* a : actors)
            {
                if (!a) continue;
                std::wstring cls = safeClassName(a);
                if (cls == actorClass)
                {
                    auto* fn = a->GetFunctionByNameInChain(funcName);
                    if (fn)
                    {
                        a->ProcessEvent(fn, nullptr);
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called {}::{} (actor scan)\n"), cls, std::wstring(funcName));
                        return true;
                    }
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Function {} not found on {}\n"), std::wstring(funcName), cls);
                    return false;
                }
            }
            // Only log on first retry (not every frame)
            static int s_debugNotFoundCount = 0;
            if (++s_debugNotFoundCount <= 3)
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Actor {} not found (attempt {})\n"), std::wstring(actorClass), s_debugNotFoundCount);
            return false;
        }

        // ── Complete Tips & Tutorials ──
        // Reads the PlayerUnlockedTips array on the TipComponent to check status.
        // If any tips are missing, adds them using the correct DataTable pointer
        // from existing entries. Safe — no calls with mismatched pointers.
        void completeTips()
        {
            // TipComponent lives on AMorPlayerController at offset 0x0B00
            UObject* controller = nullptr;
            {
                std::vector<UObject*> controllers;
                UObjectGlobals::FindAllOf(STR("BP_FGKMoriaPlayerController_C"), controllers);
                if (!controllers.empty()) controller = controllers[0];
            }

            if (!controller)
            {
                showOnScreen(L"Tips: PlayerController not found", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            uint8_t* ctrlBase = reinterpret_cast<uint8_t*>(controller);
            UObject* tipComp = *reinterpret_cast<UObject**>(ctrlBase + 0x0B00);

            if (!tipComp || !isReadableMemory(tipComp, 64))
            {
                showOnScreen(L"Tips: TipComponent not found", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            // Read PlayerUnlockedTips TArray at TipComponent+0x00C0
            constexpr int TIPS_ARRAY_OFFSET = 0x00C0;
            constexpr int TIP_HANDLE_SIZE = 0x18; // FMorTipRowHandle: {ptr(8)+FName(8)+bool+pad(8)}
            uint8_t* tipCompBase = reinterpret_cast<uint8_t*>(tipComp);

            struct { uint8_t* Data; int32_t Num; int32_t Max; } tipsArray{};
            std::memcpy(&tipsArray, tipCompBase + TIPS_ARRAY_OFFSET, 16);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] PlayerUnlockedTips: Num={}, Max={}\n"),
                                            tipsArray.Num, tipsArray.Max);

            // Get the DT_Tips row count for comparison
            UObject* tipTableWrapper = nullptr;
            {
                std::vector<UObject*> tables;
                UObjectGlobals::FindAllOf(STR("MorTipTable"), tables);
                if (!tables.empty()) tipTableWrapper = tables[0];
            }

            int totalTipRows = 0;
            if (tipTableWrapper)
            {
                uint8_t* wrapperBase = reinterpret_cast<uint8_t*>(tipTableWrapper);
                UObject* tipTable = *reinterpret_cast<UObject**>(wrapperBase + 0x0028);
                if (tipTable && isReadableMemory(tipTable, 64))
                {
                    uint8_t* dtBase = reinterpret_cast<uint8_t*>(tipTable);
                    struct { uint8_t* Data; int32_t Num; int32_t Max; } rowMap{};
                    if (isReadableMemory(dtBase + 0x30, 16))
                    {
                        std::memcpy(&rowMap, dtBase + 0x30, 16);
                        if (rowMap.Data && rowMap.Num > 0 && rowMap.Num < 500)
                            totalTipRows = rowMap.Num;
                    }
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] DT_Tips has {} rows, player has {} unlocked\n"),
                                            totalTipRows, tipsArray.Num);

            if (tipsArray.Num >= totalTipRows && totalTipRows > 0)
            {
                // All tips already completed
                std::wstring msg = L"All " + std::to_wstring(tipsArray.Num) + L"/" +
                                   std::to_wstring(totalTipRows) + L" tips already completed!";
                showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
                showGameMessage(L"[Mod] " + msg);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {}\n"), msg);
                return;
            }

            // Some tips are missing — need to add them via UnlockTip
            // First, get the correct DataTable pointer from an existing entry
            void* correctDTPtr = nullptr;
            if (tipsArray.Num > 0 && tipsArray.Data && isReadableMemory(tipsArray.Data, TIP_HANDLE_SIZE))
            {
                correctDTPtr = *reinterpret_cast<void**>(tipsArray.Data);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Correct DT ptr from existing entry: {}\n"),
                                                reinterpret_cast<uintptr_t>(correctDTPtr));
            }

            if (!correctDTPtr)
            {
                // No existing entries to copy pointer from — cannot safely call UnlockTip
                showOnScreen(L"Tips: No existing entries to reference", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            auto* unlockTipFunc = tipComp->GetFunctionByNameInChain(STR("UnlockTip"));
            if (!unlockTipFunc)
            {
                showOnScreen(L"Tips: UnlockTip function not found", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            // Build set of already-unlocked FNames for dedup
            std::set<uint64_t> existingNames;
            for (int i = 0; i < tipsArray.Num; i++)
            {
                uint8_t* entry = tipsArray.Data + i * TIP_HANDLE_SIZE;
                if (!isReadableMemory(entry, TIP_HANDLE_SIZE)) continue;
                uint64_t nameVal = 0;
                std::memcpy(&nameVal, entry + 8, 8); // FName at offset 8
                existingNames.insert(nameVal);
            }

            // Iterate DT_Tips RowMap and call UnlockTip for missing tips
            uint8_t* wrapperBase = reinterpret_cast<uint8_t*>(tipTableWrapper);
            UObject* tipTable = *reinterpret_cast<UObject**>(wrapperBase + 0x0028);
            uint8_t* dtBase = reinterpret_cast<uint8_t*>(tipTable);
            struct { uint8_t* Data; int32_t Num; int32_t Max; } rowMap{};
            std::memcpy(&rowMap, dtBase + 0x30, 16);

            int added = 0;
            for (int i = 0; i < rowMap.Num; i++)
            {
                uint8_t* elem = rowMap.Data + i * 24;
                if (!isReadableMemory(elem, 24)) continue;

                uint64_t nameVal = 0;
                std::memcpy(&nameVal, elem, 8); // FName from RowMap

                if (existingNames.count(nameVal)) continue; // Already unlocked

                // Build FMorTipRowHandle with the correct DataTable pointer
                uint8_t tipHandle[0x18]{};
                std::memcpy(tipHandle, &correctDTPtr, 8);    // DataTable ptr at 0x00
                std::memcpy(tipHandle + 8, elem, 8);         // RowName FName at 0x08
                // bWasRestoredFromSaveData = false at 0x10 (already zeroed)

                tipComp->ProcessEvent(unlockTipFunc, tipHandle);
                added++;
            }

            // Read array count after
            struct { uint8_t* Data; int32_t Num; int32_t Max; } tipsAfter{};
            std::memcpy(&tipsAfter, tipCompBase + TIPS_ARRAY_OFFSET, 16);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Added {} tips (was {}, now {})\n"),
                                            added, tipsArray.Num, tipsAfter.Num);

            std::wstring msg = L"Tips: added " + std::to_wstring(added) + L" new (" +
                               std::to_wstring(tipsAfter.Num) + L"/" +
                               std::to_wstring(totalTipRows) + L" total)";
            showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
            showGameMessage(L"[Mod] " + msg);
        }

        // ── Complete Tutorials ──
        // Finds TutorialManager, reads ServerAllTrackedTutorials, and sets
        // CompletedListItems=0xFFFF on every tracked tutorial entry.
        void completeTutorials()
        {
            // Find TutorialManager
            UObject* tutMgr = nullptr;
            {
                std::vector<UObject*> mgrs;
                UObjectGlobals::FindAllOf(STR("MorTutorialManager"), mgrs);
                if (!mgrs.empty()) tutMgr = mgrs[0];
            }

            if (!tutMgr)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] TutorialManager not found\n"));
                showOnScreen(L"Tutorials: Manager not found", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found TutorialManager: '{}'\n"),
                                            std::wstring(tutMgr->GetName()));

            uint8_t* tmBase = reinterpret_cast<uint8_t*>(tutMgr);

            // ServerAllTrackedTutorials at TutMgr+0x0398
            // Inside FMorTrackedTutorials (FFastArraySerializer), TArray<FMorTutorialState> at +0x0110
            constexpr int ALL_TRACKED_OFFSET = 0x0398;
            constexpr int TUTORIALS_ARRAY_OFFSET = 0x0110;
            constexpr int TUTORIAL_STATE_SIZE = 0x38;
            constexpr int COMPLETED_ITEMS_OFFSET = 0x0030; // uint16 CompletedListItems within FMorTutorialState
            constexpr int COUNTS_OFFSET = 0x0020;          // uint8 Counts[16]

            uint8_t* arrayBase = tmBase + ALL_TRACKED_OFFSET + TUTORIALS_ARRAY_OFFSET;

            if (!isReadableMemory(arrayBase, 16))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ServerAllTrackedTutorials array unreadable\n"));
                showOnScreen(L"Tutorials: array unreadable", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            struct { uint8_t* Data; int32_t Num; int32_t Max; } tutArray{};
            std::memcpy(&tutArray, arrayBase, 16);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ServerAllTrackedTutorials: Num={}, Max={}\n"),
                                            tutArray.Num, tutArray.Max);

            if (!tutArray.Data || tutArray.Num <= 0 || tutArray.Num > 100)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Tutorial array invalid\n"));
                showOnScreen(L"Tutorials: invalid array", 5.0f, 1.0f, 0.0f, 0.0f);
                return;
            }

            int completed = 0;
            int alreadyDone = 0;

            for (int i = 0; i < tutArray.Num; i++)
            {
                uint8_t* entry = tutArray.Data + i * TUTORIAL_STATE_SIZE;
                if (!isReadableMemory(entry, TUTORIAL_STATE_SIZE)) continue;

                // Read current CompletedListItems
                uint16_t currentBits = 0;
                std::memcpy(&currentBits, entry + COMPLETED_ITEMS_OFFSET, 2);

                if (currentBits == 0xFFFF)
                {
                    alreadyDone++;
                    continue;
                }

                // Set CompletedListItems to 0xFFFF (all steps complete)
                uint16_t allComplete = 0xFFFF;
                std::memcpy(entry + COMPLETED_ITEMS_OFFSET, &allComplete, 2);

                // Set all Counts to 0xFF (max completion count per step)
                std::memset(entry + COUNTS_OFFSET, 0xFF, 16);

                completed++;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Tutorials: {} completed, {} already done (of {} tracked)\n"),
                                            completed, alreadyDone, tutArray.Num);

            // NULL out TutorialTable pointer so TriggerTutorial/CanTriggerTutorial
            // can't look up any tutorial definitions — prevents ALL triggers regardless of source
            constexpr int TUTORIAL_TABLE_OFFSET = 0x0260;
            uint8_t* tableAddr = tmBase + TUTORIAL_TABLE_OFFSET;
            if (isReadableMemory(tableAddr, 8))
            {
                void* oldTable = nullptr;
                std::memcpy(&oldTable, tableAddr, 8);
                s_config.savedTutorialTable = oldTable; // save for markTutorialsRead
                std::memset(tableAddr, 0, 8);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Nulled TutorialTable at TutMgr+0x0260 (was {})\n"),
                                                reinterpret_cast<uintptr_t>(oldTable));
            }

            // Zero out ShowTutorialDisplay delegate invocation list
            constexpr int SHOW_TUTORIAL_DELEGATE_OFFSET = 0x0230;
            uint8_t* delegateAddr = tmBase + SHOW_TUTORIAL_DELEGATE_OFFSET;
            if (isReadableMemory(delegateAddr, 0x10))
            {
                std::memset(delegateAddr, 0, 0x10);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Zeroed ShowTutorialDisplay delegate at TutMgr+0x0230\n"));
            }

            // Zero out TutorialComplete delegate too
            constexpr int TUTORIAL_COMPLETE_DELEGATE_OFFSET = 0x0250;
            uint8_t* completeDelegateAddr = tmBase + TUTORIAL_COMPLETE_DELEGATE_OFFSET;
            if (isReadableMemory(completeDelegateAddr, 0x10))
            {
                std::memset(completeDelegateAddr, 0, 0x10);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Zeroed TutorialComplete delegate at TutMgr+0x0250\n"));
            }

            // Also mark tutorials on the PlayerController's AllTrackedTutorials (client side)
            // AMorPlayerController + 0x0868 + 0x0110 = controller + 0x0978
            UObject* controller = nullptr;
            {
                std::vector<UObject*> controllers;
                UObjectGlobals::FindAllOf(STR("BP_FGKMoriaPlayerController_C"), controllers);
                if (!controllers.empty()) controller = controllers[0];
            }

            int clientCompleted = 0;
            if (controller)
            {
                uint8_t* ctrlBase = reinterpret_cast<uint8_t*>(controller);
                uint8_t* ctrlArrayBase = ctrlBase + 0x0868 + TUTORIALS_ARRAY_OFFSET;

                if (isReadableMemory(ctrlArrayBase, 16))
                {
                    struct { uint8_t* Data; int32_t Num; int32_t Max; } ctrlArray{};
                    std::memcpy(&ctrlArray, ctrlArrayBase, 16);

                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Controller AllTrackedTutorials: Num={}\n"),
                                                    ctrlArray.Num);

                    if (ctrlArray.Data && ctrlArray.Num > 0 && ctrlArray.Num <= 100)
                    {
                        for (int i = 0; i < ctrlArray.Num; i++)
                        {
                            uint8_t* entry = ctrlArray.Data + i * TUTORIAL_STATE_SIZE;
                            if (!isReadableMemory(entry, TUTORIAL_STATE_SIZE)) continue;

                            uint16_t currentBits = 0;
                            std::memcpy(&currentBits, entry + COMPLETED_ITEMS_OFFSET, 2);

                            if (currentBits == 0xFFFF) continue;

                            uint16_t allComplete = 0xFFFF;
                            std::memcpy(entry + COMPLETED_ITEMS_OFFSET, &allComplete, 2);
                            std::memset(entry + COUNTS_OFFSET, 0xFF, 16);
                            clientCompleted++;
                        }
                    }
                }
            }

            std::wstring msg = L"Tutorials: " + std::to_wstring(completed) + L" completed";
            if (alreadyDone > 0)
                msg += L" (" + std::to_wstring(alreadyDone) + L" already done)";
            if (clientCompleted > 0)
                msg += L" + " + std::to_wstring(clientCompleted) + L" client";
            showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
            showGameMessage(L"[Mod] " + msg);
        }

        // ── Mark All As Read ──
        // Finds all open lore/crafting/build screen widgets and calls their
        // MarkAllAsRead/MarkAllRead functions to clear "new" indicators.
        bool markAllAsRead()
        {
            // Each screen type: { FindAllOf class name, function name }
            struct ScreenInfo
            {
                const wchar_t* className;
                const wchar_t* funcName;
            };
            ScreenInfo screens[] = {
                {STR("WBP_GoalsScreen_C"), STR("MarkAllAsRead")},           // Goals/Tutorials/Tips
                {STR("WBP_LoreScreen_v2_C"), STR("MarkAllRead")},          // Appendices/Mysteries & Lore
                {STR("UI_WBP_Crafting_Screen_C"), STR("MarkAllAsRead")},    // Crafting recipes
                {STR("UI_WBP_Build_Tab_C"), STR("MarkAllAsRead")},          // Build recipes
            };

            int found = 0;
            int marked = 0;

            for (const auto& si : screens)
            {
                std::vector<UObject*> objs;
                UObjectGlobals::FindAllOf(si.className, objs);
                if (objs.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {} not found\n"), si.className);
                    continue;
                }

                UObject* widget = objs[0];
                found++;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {}: '{}'\n"),
                                                si.className, std::wstring(widget->GetName()));

                UFunction* func = widget->GetFunctionByNameInChain(si.funcName);
                if (!func)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {} not found on {}\n"),
                                                    si.funcName, si.className);
                    continue;
                }

                widget->ProcessEvent(func, nullptr);
                marked++;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called {}::{} OK\n"),
                                                si.className, si.funcName);
            }

            if (found == 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No screen widgets found — open the Lore/Goals menu first\n"));
                return false;
            }

            std::wstring msg = std::to_wstring(marked) + L" of " + std::to_wstring(4) + L" screens marked as read";
            showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
            showGameMessage(L"[Mod] " + msg);
            return marked > 0;
        }

        // ── Complete All Tutorials ──
        // Marks existing tracked tutorials complete, nulls TutorialTable to prevent
        // new triggers, zeros delegates, and enables pre-hook suppression of all
        // tutorial display/query functions (hideTutorialHUD flag).
        // Also zeros out the tracked tutorial arrays so GetClientTutorials returns empty.
        bool completeAllTutorials()
        {
            constexpr int TUTORIALS_ARRAY_OFFSET = 0x0110;
            constexpr int TUTORIAL_STATE_SIZE = 0x38;
            constexpr int COMPLETED_ITEMS_OFFSET = 0x0030;
            constexpr int COUNTS_OFFSET = 0x0020;
            constexpr int FLAGS_OFFSET = 0x0036;

            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };

            // ── Find TutorialManager ──
            UObject* tutMgr = nullptr;
            {
                std::vector<UObject*> mgrs;
                UObjectGlobals::FindAllOf(STR("MorTutorialManager"), mgrs);
                if (!mgrs.empty()) tutMgr = mgrs[0];
            }
            if (!tutMgr)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] completeAllTutorials: TutorialManager not found\n"));
                return false;
            }

            uint8_t* tmBase = reinterpret_cast<uint8_t*>(tutMgr);

            // Save TutorialTable pointer before nulling
            UObject* tutTable = nullptr;
            if (isReadableMemory(tmBase + 0x0260, 8))
                tutTable = *reinterpret_cast<UObject**>(tmBase + 0x0260);
            if (!tutTable && s_config.savedTutorialTable)
                tutTable = reinterpret_cast<UObject*>(s_config.savedTutorialTable);

            // ── Record breadcrumbs FIRST (before nulling table) ──
            int breadcrumbs = 0;
            if (tutTable)
            {
                s_config.savedTutorialTable = tutTable;
                uint8_t* dtBase = reinterpret_cast<uint8_t*>(tutTable);
                if (isReadableMemory(dtBase + 0x30, 16))
                {
                    TArrayHeader rowMap{};
                    std::memcpy(&rowMap, dtBase + 0x30, 16);
                    UFunction* recordFunc = tutMgr->GetFunctionByNameInChain(STR("RecordBreadcrumb"));
                    if (recordFunc && rowMap.Data && rowMap.Num > 0 && rowMap.Num <= 100)
                    {
                        void* dtPtr = tutTable;
                        for (int i = 0; i < rowMap.Num; i++)
                        {
                            uint8_t* elem = rowMap.Data + i * 24;
                            if (!isReadableMemory(elem, 24)) continue;
                            uint64_t nameVal = 0;
                            std::memcpy(&nameVal, elem, 8);
                            if (nameVal == 0) continue;
                            uint8_t params[0x18]{};
                            std::memcpy(params + 0x00, &dtPtr, 8);
                            std::memcpy(params + 0x08, &nameVal, 8);
                            tutMgr->ProcessEvent(recordFunc, params);
                            breadcrumbs++;
                        }
                    }
                }
            }

            // ── Mark existing tracked entries complete ──
            auto markComplete = [&](uint8_t* arrayBase, const wchar_t* label) -> int {
                if (!isReadableMemory(arrayBase, 16)) return 0;
                TArrayHeader arr{};
                std::memcpy(&arr, arrayBase, 16);
                int completed = 0;
                if (arr.Data && arr.Num > 0 && arr.Num <= 100)
                {
                    for (int i = 0; i < arr.Num; i++)
                    {
                        uint8_t* entry = arr.Data + i * TUTORIAL_STATE_SIZE;
                        if (!isReadableMemory(entry, TUTORIAL_STATE_SIZE)) continue;
                        uint16_t allDone = 0xFFFF;
                        std::memcpy(entry + COMPLETED_ITEMS_OFFSET, &allDone, 2);
                        std::memset(entry + COUNTS_OFFSET, 0xFF, 16);
                        uint8_t flags = 0x07;
                        std::memcpy(entry + FLAGS_OFFSET, &flags, 1);
                        completed++;
                    }
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] completeAllTutorials: {} completed {}/{}\n"),
                                                label, completed, arr.Num);
                return completed;
            };

            markComplete(tmBase + 0x0270 + TUTORIALS_ARRAY_OFFSET, L"ServerTracked");
            markComplete(tmBase + 0x0398 + TUTORIALS_ARRAY_OFFSET, L"ServerAllTracked");
            {
                std::vector<UObject*> controllers;
                UObjectGlobals::FindAllOf(STR("BP_FGKMoriaPlayerController_C"), controllers);
                if (!controllers.empty())
                {
                    uint8_t* ctrlBase = reinterpret_cast<uint8_t*>(controllers[0]);
                    markComplete(ctrlBase + 0x0740 + TUTORIALS_ARRAY_OFFSET, L"CtrlTracked");
                    markComplete(ctrlBase + 0x0868 + TUTORIALS_ARRAY_OFFSET, L"CtrlAllTracked");
                }
            }

            // ── Null TutorialTable (prevents all tutorial lookups) ──
            if (isReadableMemory(tmBase + 0x0260, 8))
            {
                std::memset(tmBase + 0x0260, 0, 8);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] completeAllTutorials: nulled TutorialTable\n"));
            }

            // ── Zero delegates ──
            if (isReadableMemory(tmBase + 0x0230, 0x10))
                std::memset(tmBase + 0x0230, 0, 0x10); // ShowTutorialDisplay
            if (isReadableMemory(tmBase + 0x0250, 0x10))
                std::memset(tmBase + 0x0250, 0, 0x10); // TutorialComplete

            // ── Zero out ALL tracked tutorial arrays (Num=0) so GetClientTutorials returns empty ──
            // This prevents GetAllTutorialRowHandles from iterating stale data
            auto zeroArray = [&](uint8_t* arrayBase, const wchar_t* label) {
                if (!isReadableMemory(arrayBase, 16)) return;
                int32_t zero = 0;
                std::memcpy(arrayBase + 8, &zero, 4); // Num = 0
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] completeAllTutorials: zeroed {} Num\n"), label);
            };

            zeroArray(tmBase + 0x0270 + TUTORIALS_ARRAY_OFFSET, L"ServerTracked");
            zeroArray(tmBase + 0x0398 + TUTORIALS_ARRAY_OFFSET, L"ServerAllTracked");
            {
                std::vector<UObject*> controllers;
                UObjectGlobals::FindAllOf(STR("BP_FGKMoriaPlayerController_C"), controllers);
                if (!controllers.empty())
                {
                    uint8_t* ctrlBase = reinterpret_cast<uint8_t*>(controllers[0]);
                    zeroArray(ctrlBase + 0x0740 + TUTORIALS_ARRAY_OFFSET, L"CtrlTracked");
                    zeroArray(ctrlBase + 0x0868 + TUTORIALS_ARRAY_OFFSET, L"CtrlAllTracked");
                }
            }

            // ── Enable permanent pre-hook suppression ──
            s_config.hideTutorialHUD = true;

            // ── Dismiss tutorial HUD widgets ──
            int dismissed = 0;
            {
                std::vector<UObject*> tutDisplays;
                UObjectGlobals::FindAllOf(STR("MorTutorialDisplay"), tutDisplays);
                for (auto* disp : tutDisplays)
                {
                    if (!disp || safeClassName(disp) != STR("WBP_TutorialDisplay_C")) continue;
                    UFunction* visFunc = disp->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFunc)
                    {
                        uint8_t visParms[8]{};
                        visParms[0] = 1; // Collapsed
                        disp->ProcessEvent(visFunc, visParms);
                        dismissed++;
                    }
                }
                std::vector<UObject*> overlays;
                UObjectGlobals::FindAllOf(STR("MoriaHUDWidget"), overlays);
                for (auto* ov : overlays)
                {
                    if (!ov || safeClassName(ov) != STR("UI_WBP_TutorialOverlay_C")) continue;
                    UFunction* visFunc = ov->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFunc)
                    {
                        uint8_t visParms[8]{};
                        visParms[0] = 1; // Collapsed
                        ov->ProcessEvent(visFunc, visParms);
                        dismissed++;
                    }
                }
            }

            Output::send<LogLevel::Warning>(
                STR("[MoriaCppMod] completeAllTutorials: breadcrumbs={}, dismissed={}\n"),
                breadcrumbs, dismissed);
            showOnScreen(L"TUTORIALS DISABLED", 5.0f, 0.0f, 1.0f, 0.0f);
            return true;
        }

        // Dump breadcrumb subsystem contents and record recipe breadcrumbs
        void dumpAndRecordBreadcrumbs()
        {
            // ── Find the BreadcrumbsSubsystem ──
            UObject* bcSub = nullptr;
            {
                std::vector<UObject*> subs;
                UObjectGlobals::FindAllOf(STR("MorBreadcrumbsSubsystem"), subs);
                if (subs.empty())
                    UObjectGlobals::FindAllOf(STR("MorBreadcrumbsSubsystemBase"), subs);
                if (!subs.empty()) bcSub = subs[0];
            }
            if (!bcSub)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: subsystem not found\n"));
                showOnScreen(L"BreadcrumbsSubsystem not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: found subsystem {}\n"),
                                            safeClassName(bcSub));

            // Breadcrumbs TArray at offset 0x0030 (FMorBreadcrumb = 0x18 each: FGameplayTag + FName + FName)
            uint8_t* subBase = reinterpret_cast<uint8_t*>(bcSub);
            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };
            TArrayHeader bcArr{};
            if (!isReadableMemory(subBase + 0x30, 16))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: array unreadable\n"));
                return;
            }
            std::memcpy(&bcArr, subBase + 0x30, 16);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: {} breadcrumbs (max={})\n"),
                                            bcArr.Num, bcArr.Max);

            // Dump first 200 entries to log
            int dumpCount = std::min(bcArr.Num, 200);
            for (int i = 0; i < dumpCount; i++)
            {
                uint8_t* entry = bcArr.Data + i * 0x18;
                if (!isReadableMemory(entry, 0x18)) continue;
                try
                {
                    auto* tag = reinterpret_cast<FName*>(entry + 0x00);
                    auto* catName = reinterpret_cast<FName*>(entry + 0x08);
                    auto* uniqName = reinterpret_cast<FName*>(entry + 0x10);
                    std::wstring tagStr = tag->ToString();
                    std::wstring catStr = catName->ToString();
                    std::wstring uniqStr = uniqName->ToString();
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BC[{}]: tag={}, cat={}, unique={}\n"),
                                                    i, tagStr, catStr, uniqStr);
                }
                catch (...) { Output::send<LogLevel::Warning>(STR("[MoriaCppMod] BC[{}]: read error\n"), i); }
            }

            // ── Record breadcrumbs for crafting recipes ──
            // Call RecordBreadcrumb(CategoryTag, None, UniqueName) directly on the subsystem.
            // UI_WBP_Craft_List_Item_C widgets have:
            //   CategoryTag at 0x049C (FGameplayTag = FName, 8B)
            //   RecipeRef.ResultItemHandle.RowName at 0x0348+0xD8+0x08 = 0x0428 (FName, 8B)
            // Breadcrumb pattern: tag=UI.*, cat=None, unique=Consumable.*
            int recipesBc = 0;
            {
                UFunction* recordFunc = bcSub->GetFunctionByNameInChain(STR("RecordBreadcrumb"));
                if (recordFunc)
                {
                    // Find all craft list item widgets (crafting screen must be open)
                    std::vector<UObject*> widgets;
                    UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
                    for (auto* w : widgets)
                    {
                        if (!w) continue;
                        if (safeClassName(w) != L"UI_WBP_Craft_List_Item_C") continue;
                        uint8_t* wBase = reinterpret_cast<uint8_t*>(w);

                        // Read CategoryTag (FGameplayTag = FName at 0x049C)
                        if (!isReadableMemory(wBase + 0x049C, 8)) continue;
                        uint64_t catTag = 0;
                        std::memcpy(&catTag, wBase + 0x049C, 8);
                        if (catTag == 0) continue; // skip items with no tag

                        // Read ResultItemHandle.RowName (FName at 0x0428)
                        if (!isReadableMemory(wBase + 0x0428, 8)) continue;
                        uint64_t uniqueName = 0;
                        std::memcpy(&uniqueName, wBase + 0x0428, 8);
                        if (uniqueName == 0) continue; // skip items with no name

                        // RecordBreadcrumb(FGameplayTag CategoryTag, FName CategoryName, FName UniqueName)
                        // ParmsSize: FGameplayTag(8) + FName(8) + FName(8) + bool return(1) = ~25, round to 32
                        uint8_t params[32]{};
                        std::memcpy(params + 0x00, &catTag, 8);     // CategoryTag (FGameplayTag = FName)
                        // params+0x08 = CategoryName = None = 0 (already zeroed)
                        std::memcpy(params + 0x10, &uniqueName, 8); // UniqueName
                        bcSub->ProcessEvent(recordFunc, params);
                        recipesBc++;
                    }
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: recorded {} recipe breadcrumbs via RecordBreadcrumb\n"), recipesBc);
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] dumpBreadcrumbs: RecordBreadcrumb not found on subsystem\n"));
                }
            }

            std::wstring msg = L"Breadcrumbs: " + std::to_wstring(bcArr.Num) + L" total, " +
                               std::to_wstring(recipesBc) + L" recipes recorded";
            showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
            showGameMessage(L"[Mod] " + msg);
        }

        // ── Mark All Crafting Recipes as Read ──
        // Finds the open crafting screen, calls MarkAllAsRead for immediate UI update,
        // then iterates AllRecipes and calls SetRecipeViewed for each recipe to record
        // persistent breadcrumbs. Returns count of recipes marked, or -1 if screen not found.
        int markAllCraftingRecipesRead()
        {
            // Find the crafting screen widget
            UObject* craftScreen = nullptr;
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("MorCraftingScreen"), screens);
                for (auto* s : screens)
                {
                    if (!s) continue;
                    std::wstring cls = safeClassName(s);
                    if (cls.find(L"UI_WBP_Crafting_Screen") != std::wstring::npos)
                    {
                        craftScreen = s;
                        break;
                    }
                }
            }
            if (!craftScreen)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: crafting screen not open\n"));
                return -1;
            }

            // Call MarkAllAsRead for immediate UI update
            UFunction* markAllFunc = craftScreen->GetFunctionByNameInChain(STR("MarkAllAsRead"));
            if (markAllFunc)
            {
                craftScreen->ProcessEvent(markAllFunc, nullptr);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: called MarkAllAsRead OK\n"));
            }

            // Read AllRecipes TArray at offset 0x03F0 on UMorCraftingScreen
            uint8_t* screenBase = reinterpret_cast<uint8_t*>(craftScreen);
            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };
            TArrayHeader recipes{};
            if (!isReadableMemory(screenBase + 0x03F0, 16))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: AllRecipes unreadable\n"));
                return 0;
            }
            std::memcpy(&recipes, screenBase + 0x03F0, 16);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: AllRecipes Num={}, Max={}\n"),
                                            recipes.Num, recipes.Max);

            if (!recipes.Data || recipes.Num <= 0 || recipes.Num > 500)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: AllRecipes invalid (Num={})\n"),
                                                recipes.Num);
                return 0;
            }

            // Call SetRecipeViewed for each recipe (records breadcrumbs for persistence)
            UFunction* setViewedFunc = craftScreen->GetFunctionByNameInChain(STR("SetRecipeViewed"));
            if (!setViewedFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: SetRecipeViewed not found\n"));
                return 0;
            }

            int viewed = 0;
            constexpr int RECIPE_SIZE = 0x138; // FMorItemRecipeDefinition size
            for (int i = 0; i < recipes.Num; i++)
            {
                uint8_t* recipeData = recipes.Data + i * RECIPE_SIZE;
                if (!isReadableMemory(recipeData, RECIPE_SIZE)) continue;

                // SetRecipeViewed(const FMorItemRecipeDefinition& Recipe)
                // In ProcessEvent, const-ref struct params are copied inline
                uint8_t params[RECIPE_SIZE]{};
                std::memcpy(params, recipeData, RECIPE_SIZE);
                craftScreen->ProcessEvent(setViewedFunc, params);
                viewed++;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: viewed {}/{} recipes\n"),
                                            viewed, recipes.Num);

            // Also clear "new" indicators on individual recipe widgets
            // SetBreadcrumb(false) hides the breadcrumb dot on each craft list item
            int cleared = 0;
            {
                std::vector<UObject*> widgets;
                UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
                for (auto* w : widgets)
                {
                    if (!w) continue;
                    if (safeClassName(w) != L"UI_WBP_Craft_List_Item_C") continue;

                    UFunction* setBcFunc = w->GetFunctionByNameInChain(STR("SetBreadcrumb"));
                    if (setBcFunc)
                    {
                        uint8_t bcParams[8]{};
                        bcParams[0] = 0; // On = false → hide breadcrumb
                        w->ProcessEvent(setBcFunc, bcParams);
                        cleared++;
                    }
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] markAllCraftingRecipesRead: cleared {} widget breadcrumbs\n"),
                                            cleared);

            return viewed;
        }

        // Read debug menu bool properties to show current state
        // ── 7H: Debug & Cheat Commands ────────────────────────────────────────
        // Debug menu toggles, rotation control, actor inspection, widget dumps

        // Reads bool properties from BP_DebugMenu_CraftingAndConstruction_C
        // using runtime property discovery (GetValuePtrByPropertyNameInChain).
        void showDebugMenuState()
        {
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);
            for (auto* a : actors)
            {
                if (!a) continue;
                std::wstring cls = safeClassName(a);
                if (cls == STR("BP_DebugMenu_CraftingAndConstruction_C"))
                {
                    auto readBool = [&](const TCHAR* name) -> bool {
                        void* ptr = a->GetValuePtrByPropertyNameInChain(name);
                        return ptr && *static_cast<uint8_t*>(ptr) != 0;
                    };
                    bool freeCon = readBool(STR("free_construction"));
                    bool freeCraft = readBool(STR("free_crafting"));
                    bool prereqs = readBool(STR("construction_prereqs"));
                    bool stability = readBool(STR("construction_stability"));
                    bool instant = readBool(STR("instant_crafting"));
                    std::wstring msg = L"[Cheats] ";
                    msg += freeCon ? L"FreeBuild:ON " : L"FreeBuild:OFF ";
                    msg += freeCraft ? L"FreeCraft:ON " : L"FreeCraft:OFF ";
                    msg += instant ? L"InstantCraft:ON " : L"InstantCraft:OFF ";
                    msg += prereqs ? L"Prereqs:OFF " : L"Prereqs:ON ";
                    msg += stability ? L"Stability:OFF" : L"Stability:ON";
                    showOnScreen(msg, 5.0f, 0.0f, 1.0f, 1.0f);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] {}\n"), msg);
                    return;
                }
            }
            showOnScreen(L"Debug menu actor not found", 3.0f, 1.0f, 0.3f, 0.3f);
        }

        // Reads the actual debug menu toggle state and syncs s_config flags.
        // Called on character load so the UI toggles match the game's real state.
        bool syncDebugToggleState()
        {
            // Try direct class search first
            std::vector<UObject*> objs;
            UObjectGlobals::FindAllOf(STR("BP_DebugMenu_CraftingAndConstruction_C"), objs);
            if (objs.empty())
            {
                // Fallback: scan all actors
                std::vector<UObject*> actors;
                UObjectGlobals::FindAllOf(STR("Actor"), actors);
                for (auto* a : actors)
                {
                    if (a && safeClassName(a) == STR("BP_DebugMenu_CraftingAndConstruction_C"))
                    {
                        objs.push_back(a);
                        break;
                    }
                }
            }
            if (objs.empty())
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] syncDebugToggleState: debug actor not found\n"));
                return false;
            }

            UObject* debugActor = objs[0];
            auto readBool = [&](const TCHAR* name) -> bool {
                void* ptr = debugActor->GetValuePtrByPropertyNameInChain(name);
                return ptr && *static_cast<uint8_t*>(ptr) != 0;
            };

            s_config.freeBuild = readBool(STR("free_construction"));
            s_config.freeCraft = readBool(STR("free_crafting"));
            s_config.instantCraft = readBool(STR("instant_crafting"));

            Output::send<LogLevel::Warning>(
                STR("[MoriaCppMod] syncDebugToggleState: freeBuild={}, freeCraft={}, instantCraft={}\n"),
                s_config.freeBuild ? 1 : 0, s_config.freeCraft ? 1 : 0, s_config.instantCraft ? 1 : 0);
            return true;
        }

        // ── Toggle/cheat wrapper functions — DISABLED: keybinds removed, config window calls callDebugFunc directly ──
#if 0
        void toggleFreeConstruction()
        {
            callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Construction"));
            showDebugMenuState();
        }

        void toggleFreeCrafting()
        {
            callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Crafting"));
            showDebugMenuState();
        }

        void toggleInstantCrafting()
        {
            callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Instant Crafting"));
            showDebugMenuState();
        }

        void toggleConstructionPrereqs()
        {
            callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Construction Prereqs"));
            showDebugMenuState();
        }

        void toggleConstructionStability()
        {
            callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Construction Stability"));
            showDebugMenuState();
        }

        void unlockAllRecipes()
        {
            if (callDebugFunc(STR("BP_DebugMenu_Recipes_C"), STR("All Recipes"))) showOnScreen(L"ALL RECIPES UNLOCKED!", 5.0f, 0.0f, 1.0f, 0.0f);
        }

        void restoreAllConstructions()
        {
            if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Restore All Constructions")))
                showOnScreen(L"All constructions restored!", 5.0f, 0.0f, 1.0f, 0.0f);
        }
#endif

        // Unlock only construction/building recipes (B menu), not weapons/armor
        // Uses DiscoverRecipe(FName) on DiscoveryManager for each row in ConstructionRecipesTable
        void unlockAllBuildingRecipes()
        {
            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };

            // ── Discover all construction recipes ──
            UObject* discMgr = nullptr;
            {
                std::vector<UObject*> mgrs;
                UObjectGlobals::FindAllOf(STR("MorDiscoveryManager"), mgrs);
                if (!mgrs.empty()) discMgr = mgrs[0];
            }
            if (!discMgr)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: DiscoveryManager not found\n"));
                showOnScreen(L"DiscoveryManager not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            UFunction* discoverFunc = discMgr->GetFunctionByNameInChain(STR("DiscoverRecipe"));
            if (!discoverFunc)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: DiscoverRecipe function not found\n"));
                showOnScreen(L"DiscoverRecipe not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // Find ConstructionRecipesTable (UMorConstructionRecipesTable extends UFGKDataTableBase)
            UObject* constTable = nullptr;
            {
                std::vector<UObject*> tables;
                UObjectGlobals::FindAllOf(STR("MorConstructionRecipesTable"), tables);
                if (!tables.empty()) constTable = tables[0];
            }
            // Fallback: search DataTable objects by name
            if (!constTable)
            {
                std::vector<UObject*> tables;
                UObjectGlobals::FindAllOf(STR("DataTable"), tables);
                for (auto* t : tables)
                {
                    if (!t) continue;
                    std::wstring name(t->GetName());
                    if (name.find(STR("Construction")) != std::wstring::npos &&
                        name.find(STR("Recipe")) != std::wstring::npos)
                    {
                        constTable = t;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: found table via name: {}\n"), name);
                        break;
                    }
                }
            }
            if (!constTable)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: ConstructionRecipesTable not found\n"));
                showOnScreen(L"Construction recipes table not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: found wrapper table: {}\n"),
                                            std::wstring(constTable->GetName()));

            // UFGKDataTableBase wraps a UDataTable* at offset 0x28 (TableAsset)
            uint8_t* wrapperBase = reinterpret_cast<uint8_t*>(constTable);
            UObject* actualDT = nullptr;
            if (isReadableMemory(wrapperBase + 0x28, 8))
                actualDT = *reinterpret_cast<UObject**>(wrapperBase + 0x28);
            if (!actualDT)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: TableAsset at 0x28 is null\n"));
                showOnScreen(L"TableAsset is null", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: actual DataTable: {}\n"),
                                            std::wstring(actualDT->GetName()));

            // Iterate RowMap (offset 0x30 from actual UDataTable)
            uint8_t* dtBase = reinterpret_cast<uint8_t*>(actualDT);
            TArrayHeader rowMap{};
            if (!isReadableMemory(dtBase + 0x30, 16))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: RowMap unreadable\n"));
                showOnScreen(L"RowMap unreadable", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            std::memcpy(&rowMap, dtBase + 0x30, 16);
            if (!rowMap.Data || rowMap.Num <= 0 || rowMap.Num > 2000)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: RowMap invalid Num={}\n"), rowMap.Num);
                showOnScreen(L"RowMap invalid", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: DataTable has {} rows\n"), rowMap.Num);

            // Call DiscoverRecipe(FName) for each construction recipe
            int discovered = 0;
            for (int i = 0; i < rowMap.Num; i++)
            {
                uint8_t* elem = rowMap.Data + i * 24; // TSet element: FName(8B) + ptr(8B) + hash(8B)
                if (!isReadableMemory(elem, 24)) continue;
                uint64_t nameVal = 0;
                std::memcpy(&nameVal, elem, 8); // FName from RowMap key
                if (nameVal == 0) continue;

                // DiscoverRecipe param: const FName& (8 bytes in ProcessEvent buffer)
                uint8_t params[16]{};
                std::memcpy(params, &nameVal, 8);
                discMgr->ProcessEvent(discoverFunc, params);
                discovered++;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] unlockBuilding: discovered {} construction recipes\n"), discovered);
            std::wstring msg = L"BUILDING RECIPES UNLOCKED: " + std::to_wstring(discovered) + L" recipes";
            showOnScreen(msg.c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
        }

        // Read debug menu actor memory to sync config window toggle states
        void refreshCheatStates()
        {
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);
            for (auto* a : actors)
            {
                if (!a) continue;
                std::wstring cls = safeClassName(a);
                if (cls == STR("BP_DebugMenu_CraftingAndConstruction_C"))
                {
                    auto readBool = [&](const TCHAR* name) -> bool {
                        void* ptr = a->GetValuePtrByPropertyNameInChain(name);
                        return ptr && *static_cast<uint8_t*>(ptr) != 0;
                    };
                    s_config.freeBuild = readBool(STR("free_construction"));
                    s_config.freeCraft = readBool(STR("free_crafting"));
                    s_config.instantCraft = readBool(STR("instant_crafting"));
                    return;
                }
            }
        }

        // ── Rotate Aimed Building: set mobility then rotate + raw memory fallback ──
        // Find BuildHUDv2 widget by searching all UserWidgets
        UObject* findBuildHUD()
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos)
                {
                    // Check if visible
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        w->ProcessEvent(visFunc, &vp);
                        if (vp.Ret) return w;
                    }
                }
            }
            // Also return first match even if not visible
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos) return w;
            }
            return nullptr;
        }

        UObject* resolveGATA()
        {
            auto* hud = findBuildHUD();
            if (!hud) return nullptr;
            uint8_t* hudBase = reinterpret_cast<uint8_t*>(hud);
            if (!isReadableMemory(hudBase + 1000, sizeof(RC::Unreal::FWeakObjectPtr))) return nullptr;
            RC::Unreal::FWeakObjectPtr weakPtr{};
            std::memcpy(&weakPtr, hudBase + 1000, sizeof(RC::Unreal::FWeakObjectPtr));
            return weakPtr.Get();
        }

        // Sets SnapRotateIncrement + FreePlaceRotateIncrement on GATA actor
        // via runtime property discovery (replaces hard-coded +1616/+1620 offsets).
        bool setGATARotation(UObject* gata, float step)
        {
            float* snap = gata->GetValuePtrByPropertyNameInChain<float>(STR("SnapRotateIncrement"));
            float* free = gata->GetValuePtrByPropertyNameInChain<float>(STR("FreePlaceRotateIncrement"));
            if (!snap || !free) return false;
            *snap = step;
            *free = step;
            return true;
        }

        // Read current SnapRotateIncrement from GATA
        float getGATARotation(UObject* gata)
        {
            float* snap = gata->GetValuePtrByPropertyNameInChain<float>(STR("SnapRotateIncrement"));
            return snap ? *snap : 45.0f;
        }

        // ── Manual rotation keybind wrappers — DISABLED: keybinds removed ──
#if 0
        void rotateBuildPlacement()
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                showOnScreen(L"Not in build mode", 2.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            const float step = static_cast<float>(s_overlay.rotationStep);
            if (!setGATARotation(gata, step)) return;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Set SnapRotateIncrement={:.0f} FreePlaceRotateIncrement={:.0f}\n"), step, step);
            std::wstring msg = L"Rotation step: " + std::to_wstring((int)step) + L"\xB0 \x2014 press R!";
            showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
        }

        void rotateBuildPlacementCcw()
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                showOnScreen(L"Not in build mode", 2.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            // Cycle: 5 → 15 → 45 → 90 → 5
            float current = getGATARotation(gata);
            float newStep;
            if (current < 10.0f)
                newStep = 15.0f;
            else if (current < 30.0f)
                newStep = 45.0f;
            else if (current < 60.0f)
                newStep = 90.0f;
            else
                newStep = 5.0f;
            setGATARotation(gata, newStep);
            s_overlay.rotationStep = static_cast<int>(newStep);
            s_overlay.needsUpdate = true;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Toggled rotation step to {:.0f}\n"), newStep);
            std::wstring msg = L"Rotation step: " + std::to_wstring((int)newStep) + L"\xB0";
            showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
        }
#endif

        // ProcessEvent spy — captures all rotation-related calls for 5 seconds
        bool m_spyActive{false};
        bool m_spyAll{false};
        int m_spyFrameCount{0};
        int m_spyAllFrameCount{0};
        float m_buildRotation{0.0f};
        static inline MoriaCppMod* s_instance{nullptr};

        // Quick-build hotbar: F1-F12 recipe slots
        static constexpr int QUICK_BUILD_SLOTS = 12;

        // Per-slot: save display name + recipe struct for activation
        static constexpr int BLOCK_DATA_SIZE = 120; // bLock struct size in blockSelectedEvent
        struct RecipeSlot
        {
            std::wstring displayName;             // display name from blockName TextBlock
            std::wstring textureName;             // e.g. "T_UI_BuildIcon_AdornedDoor" (for PNG lookup)
            uint8_t bLockData[BLOCK_DATA_SIZE]{}; // captured recipe struct (session-only, not saved to disk)
            bool hasBLockData{false};             // true if bLockData was captured this session
            bool used{false};
        };
        RecipeSlot m_recipeSlots[QUICK_BUILD_SLOTS]{};

        // Auto-capture from post-hook
        std::wstring m_lastCapturedName;
        uint8_t m_lastCapturedBLock[BLOCK_DATA_SIZE]{}; // last captured bLock from manual click
        bool m_hasLastCapture{false};
        bool m_isAutoSelecting{false}; // suppress post-hook capture during automated quickbuild

        // Offset where bLock recipe data lives in Build_Item_Medium widget memory
        // Discovered via runtime scan: widget base + 616 (0x268) = 120-byte recipe struct
        static constexpr int BLOCK_WIDGET_OFFSET = 616;
        int m_bLockWidgetOffset{BLOCK_WIDGET_OFFSET}; // hardcoded, verified via scan
        bool m_bLockIsIndirect{false};                // direct memory, not a pointer

        // Pending quick-build: set when we simulate B key, waiting for build menu to open
        int m_pendingQuickBuildSlot{-1};
        int m_pendingBuildFrames{0};

        // Target-to-build: Shift+F10 — build the last targeted buildable object
        std::wstring m_targetBuildName;      // display name from last F10 target
        std::wstring m_targetBuildRecipeRef; // class name sans BP_ prefix (for bLock matching)
        std::wstring m_targetBuildRowName;   // DT_Constructions row name (also key for DT_ConstructionRecipes)
        bool m_lastTargetBuildable{false};   // was the last target buildable?
        bool m_pendingTargetBuild{false};    // pending build-from-target state machine
        bool m_buildMenuWasOpen{false};      // tracks build menu open/close for ActionBar refresh
        int m_pendingTargetBuildFrames{0};   // frame counter for state machine

        // Clear-hotbar state machine: move one item per frame
        bool m_clearingHotbar{false};
        int m_clearHotbarCount{0};        // items moved so far
        int m_clearHotbarDropped{0};      // items dropped (bag full)
        int m_clearHotbarWait{0};         // frames to wait before next move
        std::vector<uint8_t> m_bagHandle; // cached EpicPack bag FItemHandle

        // Toolbar swap system (F12) — row swap within 8x3 BodyInventory
        static constexpr int HOTBAR_SLOTS = 8;
        bool m_storagePatched{false}; // Whether DT_Storage was successfully patched

        // Hotbar overlay: Win32 transparent bar at top-center of screen
        bool m_showHotbar{true}; // ON by default

        // Experimental UMG toolbar (Num5 toggle)
        UObject* m_umgBarWidget{nullptr};           // root UUserWidget
        UObject* m_umgStateImages[8]{};             // state icon UImage per slot
        UObject* m_umgIconImages[8]{};              // recipe icon UImage per slot (overlaid on state)
        UObject* m_umgIconTextures[8]{};            // cached UTexture2D* per slot (recipe icon)
        std::wstring m_umgIconNames[8];             // texture name currently displayed per slot
        UObject* m_umgTexEmpty{nullptr};            // cached Empty state texture
        UObject* m_umgTexInactive{nullptr};         // cached Inactive state texture
        UObject* m_umgTexActive{nullptr};           // cached Active state texture
        enum class UmgSlotState : uint8_t { Empty, Inactive, Active };
        UmgSlotState m_umgSlotStates[8]{};
        int m_activeBuilderSlot{-1};               // which slot is currently Active (-1 = none)
        UFunction* m_umgSetBrushFn{nullptr};       // cached SetBrushFromTexture function

        // Mod Controller toolbar (Num7 toggle) — 4x2 grid, lower-right of screen
        static constexpr int MC_SLOTS = 8;
        UObject* m_mcBarWidget{nullptr};               // root UUserWidget
        UObject* m_mcStateImages[MC_SLOTS]{};          // state icon UImage per slot
        UObject* m_mcIconImages[MC_SLOTS]{};           // icon UImage per slot (overlaid on state)
        UmgSlotState m_mcSlotStates[MC_SLOTS]{};

        // Key label overlays — UTextBlock + background UImage per slot
        UObject* m_umgKeyLabels[8]{};              // UTextBlock per builders bar slot
        UObject* m_umgKeyBgImages[8]{};            // Blank_Rect UImage per builders bar slot
        UObject* m_mcKeyLabels[MC_SLOTS]{};        // UTextBlock per MC slot
        UObject* m_mcKeyBgImages[MC_SLOTS]{};      // Blank_Rect UImage per MC slot
        UObject* m_umgTexBlankRect{nullptr};       // cached T_UI_Icon_Input_Blank_Rect texture
        UObject* m_mcRotationLabel{nullptr};       // UTextBlock overlaid on MC slot 0 — "5°\nT0"
        UObject* m_mcSlot0Overlay{nullptr};        // Overlay containing state+icon for MC slot 0
        UObject* m_mcSlot4Overlay{nullptr};        // Overlay for MC slot 4 (Remove Target)
        UObject* m_mcSlot6Overlay{nullptr};        // Overlay for MC slot 6 (Remove All)
        // Advanced Builder toolbar (single toggle button, lower-right corner)
        UObject* m_abBarWidget{nullptr};           // root UUserWidget for Advanced Builder toggle
        UObject* m_abKeyLabel{nullptr};            // UTextBlock showing key name on AB toolbar
        bool m_toolbarsVisible{false};             // toggle state: are builders bar + MC bar visible?
        // UMG Target Info popup (replaces Win32 GDI+ overlay)
        UObject* m_targetInfoWidget{nullptr};      // root UUserWidget
        UObject* m_tiTitleLabel{nullptr};           // "Target Info" title
        UObject* m_tiClassLabel{nullptr};           // Class value
        UObject* m_tiNameLabel{nullptr};            // Name value
        UObject* m_tiDisplayLabel{nullptr};         // Display value
        UObject* m_tiPathLabel{nullptr};            // Path value
        UObject* m_tiBuildLabel{nullptr};           // Buildable value
        UObject* m_tiRecipeLabel{nullptr};          // Recipe value
        ULONGLONG m_tiShowTick{0};                  // GetTickCount64() when shown; 0 = hidden
        // UMG Info Box popup (removal operation messages)
        UObject* m_infoBoxWidget{nullptr};          // root UUserWidget
        UObject* m_ibTitleLabel{nullptr};            // title (e.g. "Removed", "Undo")
        UObject* m_ibMessageLabel{nullptr};          // message body
        ULONGLONG m_ibShowTick{0};                   // GetTickCount64() when shown; 0 = hidden
        // UMG Config Menu
        UObject* m_configWidget{nullptr};              // root UUserWidget
        UObject* m_cfgTabLabels[3]{};                  // tab header TextBlocks
        UObject* m_cfgTabContent[3]{};                 // VBox per tab (content)
        UObject* m_cfgTabImages[3]{};                  // UImage per tab (background texture)
        UObject* m_cfgTabActiveTexture{nullptr};       // T_UI_Btn_P1_Up (active tab)
        UObject* m_cfgTabInactiveTexture{nullptr};     // T_UI_Btn_P2_Up (inactive tab)
        UObject* m_cfgVignetteImage{nullptr};          // UImage for vignette border frame
        UObject* m_cfgScrollBoxes[3]{};                // UScrollBox wrappers per tab
        int m_cfgActiveTab{0};
        bool m_cfgVisible{false};
        // Tab 0: Optional Mods
        UObject* m_cfgFreeBuildLabel{nullptr};
        UObject* m_cfgFreeBuildCheckImg{nullptr};  // check mark image (shown when ON)
        UObject* m_cfgUnlockBtnImg{nullptr};       // Unlock All Recipes button bg image
        // Tab 1: Key Mapping
        UObject* m_cfgKeyValueLabels[BIND_COUNT]{};    // old text labels (kept for compat)
        UObject* m_cfgKeyBoxLabels[BIND_COUNT]{};      // key box TextBlocks (new)
        UObject* m_cfgModifierLabel{nullptr};
        UObject* m_cfgModBoxLabel{nullptr};            // modifier key box TextBlock
        // Tab 2: Hide Environment
        UObject* m_cfgRemovalHeader{nullptr};
        UObject* m_cfgRemovalVBox{nullptr};            // VBox holding removal entry rows
        int m_cfgLastRemovalCount{-1};
        // s_pendingKeyLabelRefresh moved to static section near s_overlay (before configWndProc)

        // Safe memory read helper (uses VirtualQuery to avoid access violations)
        // Checks both the start and end of the requested range to handle page boundaries.
        static bool isReadableMemory(const void* ptr, size_t size = 8)
        {
            if (!ptr) return false;
            auto checkPage = [](const void* p) -> bool {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
                if (mbi.State != MEM_COMMIT) return false;
                DWORD protect = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
                return (protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE);
            };
            if (!checkPage(ptr)) return false;
            // Also check the last byte of the range to catch cross-page boundary reads
            if (size > 1)
            {
                const void* end = static_cast<const uint8_t*>(ptr) + size - 1;
                if (!checkPage(end)) return false;
            }
            return true;
        }

        // Safe wrapper for GetClassPrivate()->GetName() — returns empty string on null
        static std::wstring safeClassName(UObject* obj)
        {
            if (!obj) return L"";
            auto* cls = obj->GetClassPrivate();
            if (!cls) return L"";
            return std::wstring(cls->GetName());
        }

        // ── 7F: Quick-Build System ────────────────────────────────────────────
        // F1-F8 recipe slots: capture bLock from build menu, replay via state machine
        // Key discovery: bLock at widget+616 is THE recipe identifier
        // State machine: open menu → wait for build tab → find widget → select recipe

        UObject* findWidgetByClass(const wchar_t* className, bool requireVisible = false)
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls == className)
                {
                    if (!requireVisible) return w;
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        w->ProcessEvent(visFunc, &vp);
                        if (vp.Ret) return w;
                    }
                }
            }
            return nullptr;
        }

        // Force the game's action bar (hotbar UI) to refresh its display
        void refreshActionBar()
        {
            UObject* actionBar = findWidgetByClass(L"WBP_UI_ActionBar_C", true);
            if (!actionBar) return;
            auto* refreshFunc = actionBar->GetFunctionByNameInChain(STR("Set All Action Bar Items"));
            if (refreshFunc)
            {
                actionBar->ProcessEvent(refreshFunc, nullptr);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] ActionBar: Set All Action Bar Items called\n"));
            }
        }

        void saveQuickBuildSlots()
        {
            std::ofstream file("Mods/MoriaCppMod/quickbuild_slots.txt", std::ios::trunc);
            if (!file.is_open()) return;
            file << "# MoriaCppMod quick-build slots (F1-F8)\n";
            file << "# slot|displayName|textureName\n";
            for (int i = 0; i < OVERLAY_BUILD_SLOTS; i++)
            {
                if (!m_recipeSlots[i].used) continue;
                std::string narrowName, narrowTex;
                for (wchar_t c : m_recipeSlots[i].displayName)
                    narrowName.push_back(static_cast<char>(c));
                for (wchar_t c : m_recipeSlots[i].textureName)
                    narrowTex.push_back(static_cast<char>(c));
                file << i << "|" << narrowName << "|" << narrowTex << "\n";
            }
            // Persist rotation step
            file << "rotation|" << s_overlay.rotationStep << "\n";
        }

        void loadQuickBuildSlots()
        {
            std::ifstream file("Mods/MoriaCppMod/quickbuild_slots.txt");
            if (!file.is_open()) return;
            std::string line;
            int loaded = 0;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;
                auto sep1 = line.find('|');
                if (sep1 == std::string::npos) continue;
                std::string key = line.substr(0, sep1);
                // Rotation step persistence
                if (key == "rotation")
                {
                    try
                    {
                        int val = std::stoi(line.substr(sep1 + 1));
                        if (val >= 0 && val <= 90) s_overlay.rotationStep = val;
                    }
                    catch (...) {}
                    continue;
                }
                int slot;
                try
                {
                    slot = std::stoi(key);
                }
                catch (...)
                {
                    continue;
                }
                if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) continue;
                // Parse: displayName|textureName (textureName optional for backward compat)
                auto sep2 = line.find('|', sep1 + 1);
                std::string name, tex;
                if (sep2 != std::string::npos)
                {
                    name = line.substr(sep1 + 1, sep2 - sep1 - 1);
                    tex = line.substr(sep2 + 1);
                }
                else
                {
                    name = line.substr(sep1 + 1);
                }
                m_recipeSlots[slot].displayName = std::wstring(name.begin(), name.end());
                m_recipeSlots[slot].textureName = std::wstring(tex.begin(), tex.end());
                m_recipeSlots[slot].used = true;
                loaded++;
            }
            if (loaded > 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Loaded {} quick-build slots from disk\n"), loaded);
                updateOverlayText();
                updateBuildersBar();
            }
        }

        void saveKeybindings()
        {
            std::ofstream file("Mods/MoriaCppMod/keybindings.txt", std::ios::trunc);
            if (!file.is_open()) return;
            file << "# MoriaCppMod keybindings (index|VK_code)\n";
            for (int i = 0; i < BIND_COUNT; i++)
            {
                file << i << "|" << (int)s_bindings[i].key << "\n";
            }
            file << "mod|" << (int)s_modifierVK.load() << "\n";
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Saved {} keybindings + modifier to disk\n"), BIND_COUNT);
        }

        void loadKeybindings()
        {
            std::ifstream file("Mods/MoriaCppMod/keybindings.txt");
            if (!file.is_open()) return;
            std::string line;
            int loaded = 0;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;
                // Check for modifier line: "mod|VK_code"
                if (line.size() > 4 && line.substr(0, 4) == "mod|")
                {
                    try
                    {
                        int mvk = std::stoi(line.substr(4));
                        if (mvk == VK_SHIFT || mvk == VK_CONTROL || mvk == VK_MENU || mvk == VK_RMENU)
                            s_modifierVK = static_cast<uint8_t>(mvk);
                    }
                    catch (...)
                    {
                    }
                    continue;
                }
                auto sep = line.find('|');
                if (sep == std::string::npos) continue;
                int idx, vk;
                try
                {
                    idx = std::stoi(line.substr(0, sep));
                    vk = std::stoi(line.substr(sep + 1));
                }
                catch (...)
                {
                    continue;
                }
                if (idx >= 0 && idx < BIND_COUNT && vk > 0 && vk < 256)
                {
                    s_bindings[idx].key = static_cast<uint8_t>(vk);
                    loaded++;
                }
            }
            if (loaded > 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Loaded {} keybindings from disk\n"), loaded);
            }
        }

        // NOTE: DXT5 decoder, saveBGRAAsPng, isRangeReadable, looksLikeDXT5, findBulkDataPtr
        // were removed in v1.11 — dead code from the CPU texture extraction attempt.
        // The Canvas render target pipeline (extractAndSaveIcon) replaced all of these.

        // Helper: find a UFunction param property by name (case-insensitive)
        static FProperty* findParam(UFunction* fn, const wchar_t* name)
        {
            std::wstring target(name);
            for (auto* prop : fn->ForEachProperty())
            {
                std::wstring pn(prop->GetName());
                if (pn.size() != target.size()) continue;
                bool match = true;
                for (size_t i = 0; i < pn.size(); i++)
                {
                    if (towlower(pn[i]) != towlower(target[i]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match) return prop;
            }
            return nullptr;
        }

        // Extract icon via render target: draw UTexture2D to Canvas on a render target, then export
        // Uses: CreateRenderTarget2D, BeginDrawCanvasToRenderTarget, K2_DrawTexture, EndDrawCanvasToRenderTarget, ExportRenderTarget
        bool extractAndSaveIcon(UObject* widget, const std::wstring& textureName, const std::wstring& outPath)
        {
            if (!widget || textureName.empty()) return false;
            try
            {
                // --- Get UTexture2D from the widget chain ---
                // widget+1104 → Image → Image+264+72 → MID → MID+256 → TextureParamValues[0]+16 → UTexture2D*
                UObject* texture = nullptr;
                {
                    uint8_t* base = reinterpret_cast<uint8_t*>(widget);
                    UObject* iconImg = *reinterpret_cast<UObject**>(base + 1104);
                    if (iconImg && isReadableMemory(iconImg, 400))
                    {
                        uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImg);
                        UObject* mid = *reinterpret_cast<UObject**>(imgBase + 264 + 72);
                        if (mid && isReadableMemory(mid, 280))
                        {
                            uint8_t* midBase = reinterpret_cast<uint8_t*>(mid);
                            uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + 256);
                            int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + 256 + 8);
                            if (arrNum >= 1 && arrNum <= 32 && arrData && isReadableMemory(arrData, 40))
                            {
                                texture = *reinterpret_cast<UObject**>(arrData + 16);
                                if (texture && !isReadableMemory(texture, 64)) texture = nullptr;
                            }
                        }
                    }
                }
                if (!texture)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] UTexture2D not found from widget chain\n"));
                    return false;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] UTexture2D: {} '{}'\n"), safeClassName(texture), std::wstring(texture->GetName()));

                // --- Find required UFunctions ---
                auto* createRTFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:CreateRenderTarget2D"));
                auto* beginDrawFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:BeginDrawCanvasToRenderTarget"));
                auto* endDrawFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:EndDrawCanvasToRenderTarget"));
                auto* exportRTFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:ExportRenderTarget"));
                auto* drawTexFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Canvas:K2_DrawTexture"));

                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] CreateRT={} BeginDraw={} EndDraw={} ExportRT={} K2_DrawTex={}\n"),
                                                createRTFn ? STR("YES") : STR("no"),
                                                beginDrawFn ? STR("YES") : STR("no"),
                                                endDrawFn ? STR("YES") : STR("no"),
                                                exportRTFn ? STR("YES") : STR("no"),
                                                drawTexFn ? STR("YES") : STR("no"));

                if (!createRTFn || !beginDrawFn || !endDrawFn || !exportRTFn || !drawTexFn)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Missing required UFunctions\n"));
                    return false;
                }

                // Log parameter layouts for all functions (first time only)
                static bool s_loggedParams = false;
                if (!s_loggedParams)
                {
                    s_loggedParams = true;
                    for (auto* fn : {createRTFn, beginDrawFn, endDrawFn, drawTexFn, exportRTFn})
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] {} ParmsSize={}:\n"), std::wstring(fn->GetName()), fn->GetParmsSize());
                        for (auto* prop : fn->ForEachProperty())
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon]   {} @{} sz={} {}\n"),
                                                            std::wstring(prop->GetName()),
                                                            prop->GetOffset_Internal(),
                                                            prop->GetSize(),
                                                            std::wstring(prop->GetClass().GetName()));
                        }
                    }
                }

                // --- Get world context ---
                UObject* worldCtx = nullptr;
                {
                    std::vector<UObject*> pcs;
                    UObjectGlobals::FindAllOf(STR("PlayerController"), pcs);
                    for (auto* pc : pcs)
                    {
                        if (pc)
                        {
                            worldCtx = pc;
                            break;
                        }
                    }
                }
                if (!worldCtx)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] No PlayerController\n"));
                    return false;
                }

                // KRL CDO for static function calls
                auto* krlClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary"));
                if (!krlClass) return false;
                UObject* krlCDO = krlClass->GetClassDefaultObject();
                if (!krlCDO) return false;

                // === Step 1: CreateRenderTarget2D (128x128 RGBA8) ===
                UObject* renderTarget = nullptr;
                {
                    int pSz = createRTFn->GetParmsSize();
                    std::vector<uint8_t> params(pSz, 0);
                    auto* pWC = findParam(createRTFn, STR("WorldContextObject"));
                    auto* pW = findParam(createRTFn, STR("Width"));
                    auto* pH = findParam(createRTFn, STR("Height"));
                    auto* pF = findParam(createRTFn, STR("Format"));
                    auto* pRV = findParam(createRTFn, STR("ReturnValue"));
                    if (pWC) *reinterpret_cast<UObject**>(params.data() + pWC->GetOffset_Internal()) = worldCtx;
                    if (pW) *reinterpret_cast<int32_t*>(params.data() + pW->GetOffset_Internal()) = 128;
                    if (pH) *reinterpret_cast<int32_t*>(params.data() + pH->GetOffset_Internal()) = 128;
                    if (pF) params[pF->GetOffset_Internal()] = 2; // RTF_RGBA8
                    krlCDO->ProcessEvent(createRTFn, params.data());
                    renderTarget = pRV ? *reinterpret_cast<UObject**>(params.data() + pRV->GetOffset_Internal()) : nullptr;
                }
                if (!renderTarget)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] CreateRenderTarget2D returned null\n"));
                    return false;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Created render target OK\n"));

                // === Step 2: BeginDrawCanvasToRenderTarget ===
                UObject* canvas = nullptr;
                std::vector<uint8_t> beginParams;
                {
                    int pSz = beginDrawFn->GetParmsSize();
                    beginParams.resize(pSz, 0);
                    auto* bWC = findParam(beginDrawFn, STR("WorldContextObject"));
                    auto* bRT = findParam(beginDrawFn, STR("TextureRenderTarget"));
                    auto* bCanvas = findParam(beginDrawFn, STR("Canvas"));
                    if (bWC) *reinterpret_cast<UObject**>(beginParams.data() + bWC->GetOffset_Internal()) = worldCtx;
                    if (bRT) *reinterpret_cast<UObject**>(beginParams.data() + bRT->GetOffset_Internal()) = renderTarget;
                    krlCDO->ProcessEvent(beginDrawFn, beginParams.data());
                    canvas = bCanvas ? *reinterpret_cast<UObject**>(beginParams.data() + bCanvas->GetOffset_Internal()) : nullptr;
                }
                if (!canvas)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] BeginDrawCanvasToRenderTarget returned no Canvas\n"));
                    return false;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Got Canvas: {} '{}'\n"), safeClassName(canvas), std::wstring(canvas->GetName()));

                // === Step 3: K2_DrawTexture on Canvas ===
                {
                    int pSz = drawTexFn->GetParmsSize();
                    std::vector<uint8_t> dtParams(pSz, 0);
                    auto* dTex = findParam(drawTexFn, STR("RenderTexture"));
                    auto* dPos = findParam(drawTexFn, STR("ScreenPosition"));
                    auto* dSize = findParam(drawTexFn, STR("ScreenSize"));
                    auto* dCoordPos = findParam(drawTexFn, STR("CoordinatePosition"));
                    auto* dCoordSize = findParam(drawTexFn, STR("CoordinateSize"));
                    auto* dColor = findParam(drawTexFn, STR("RenderColor"));
                    auto* dBlend = findParam(drawTexFn, STR("BlendMode"));
                    auto* dRotation = findParam(drawTexFn, STR("Rotation"));
                    auto* dPivot = findParam(drawTexFn, STR("PivotPoint"));

                    if (dTex) *reinterpret_cast<UObject**>(dtParams.data() + dTex->GetOffset_Internal()) = texture;
                    if (dPos)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dPos->GetOffset_Internal());
                        v[0] = 0.0f;
                        v[1] = 0.0f;
                    }
                    if (dSize)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dSize->GetOffset_Internal());
                        v[0] = 128.0f;
                        v[1] = 128.0f;
                    }
                    if (dCoordPos)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dCoordPos->GetOffset_Internal());
                        v[0] = 0.0f;
                        v[1] = 0.0f;
                    }
                    if (dCoordSize)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dCoordSize->GetOffset_Internal());
                        v[0] = 1.0f;
                        v[1] = 1.0f;
                    }
                    if (dColor)
                    {
                        auto* c = reinterpret_cast<float*>(dtParams.data() + dColor->GetOffset_Internal());
                        c[0] = 1.0f;
                        c[1] = 1.0f;
                        c[2] = 1.0f;
                        c[3] = 1.0f;
                    }
                    if (dBlend) *reinterpret_cast<uint8_t*>(dtParams.data() + dBlend->GetOffset_Internal()) = 0; // BLEND_Opaque
                    if (dRotation) *reinterpret_cast<float*>(dtParams.data() + dRotation->GetOffset_Internal()) = 0.0f;
                    if (dPivot)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dPivot->GetOffset_Internal());
                        v[0] = 0.5f;
                        v[1] = 0.5f;
                    }

                    canvas->ProcessEvent(drawTexFn, dtParams.data());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Drew texture to canvas\n"));
                }

                // === Step 4: EndDrawCanvasToRenderTarget ===
                {
                    int pSz = endDrawFn->GetParmsSize();
                    std::vector<uint8_t> eParams(pSz, 0);
                    auto* eWC = findParam(endDrawFn, STR("WorldContextObject"));
                    auto* eCtx = findParam(endDrawFn, STR("Context"));
                    if (eWC) *reinterpret_cast<UObject**>(eParams.data() + eWC->GetOffset_Internal()) = worldCtx;
                    // Copy the Context struct from BeginDraw output to EndDraw input
                    if (eCtx)
                    {
                        auto* bCtx = findParam(beginDrawFn, STR("Context"));
                        if (bCtx && bCtx->GetSize() <= eCtx->GetSize())
                        {
                            memcpy(eParams.data() + eCtx->GetOffset_Internal(), beginParams.data() + bCtx->GetOffset_Internal(), bCtx->GetSize());
                        }
                    }
                    krlCDO->ProcessEvent(endDrawFn, eParams.data());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] EndDrawCanvasToRenderTarget OK\n"));
                }

                // === Step 5: ExportRenderTarget ===
                {
                    int eSz = exportRTFn->GetParmsSize();
                    std::vector<uint8_t> eParams(eSz, 0);
                    auto* eWC = findParam(exportRTFn, STR("WorldContextObject"));
                    auto* eRT = findParam(exportRTFn, STR("TextureRenderTarget"));
                    auto* eFP = findParam(exportRTFn, STR("FilePath"));
                    auto* eFN = findParam(exportRTFn, STR("FileName"));

                    if (eWC) *reinterpret_cast<UObject**>(eParams.data() + eWC->GetOffset_Internal()) = worldCtx;
                    if (eRT) *reinterpret_cast<UObject**>(eParams.data() + eRT->GetOffset_Internal()) = renderTarget;

                    std::wstring folder, fileName;
                    auto lastSlash = outPath.rfind(L'\\');
                    if (lastSlash != std::wstring::npos)
                    {
                        folder = outPath.substr(0, lastSlash);
                        fileName = outPath.substr(lastSlash + 1);
                        auto dotPos = fileName.rfind(L'.');
                        if (dotPos != std::wstring::npos) fileName = fileName.substr(0, dotPos);
                    }
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Exporting to folder='{}' file='{}'\n"), folder, fileName);

                    if (eFP)
                    {
                        auto* fstr = reinterpret_cast<FString*>(eParams.data() + eFP->GetOffset_Internal());
                        *fstr = FString(folder.c_str());
                    }
                    if (eFN)
                    {
                        auto* fstr = reinterpret_cast<FString*>(eParams.data() + eFN->GetOffset_Internal());
                        *fstr = FString(fileName.c_str());
                    }
                    krlCDO->ProcessEvent(exportRTFn, eParams.data());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] ExportRenderTarget called\n"));
                }

                // --- Check for exported file and convert to PNG ---
                std::wstring baseName = outPath.substr(0, outPath.rfind(L'.'));
                std::wstring exportedPath;
                for (const wchar_t* ext : {L".hdr", L".bmp", L".png", L".exr", L""})
                {
                    std::wstring candidate = baseName + ext;
                    DWORD attr = GetFileAttributesW(candidate.c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES)
                    {
                        WIN32_FILE_ATTRIBUTE_DATA fad{};
                        GetFileAttributesExW(candidate.c_str(), GetFileExInfoStandard, &fad);
                        int64_t fsize = ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Found file: {} ({} bytes)\n"), candidate, fsize);
                        if (fsize > 0)
                        {
                            exportedPath = candidate;
                            break;
                        }
                    }
                }

                if (exportedPath.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] No exported file found (or 0 bytes)\n"));
                    for (const wchar_t* ext : {L".hdr", L".bmp", L".png", L".exr", L""})
                    {
                        std::wstring candidate = baseName + ext;
                        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(candidate.c_str());
                    }
                    return false;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Found exported file: {}\n"), exportedPath);

                if (exportedPath == outPath) return true;

                // Convert to PNG using GDI+
                Gdiplus::GdiplusStartupInput gdipInput;
                ULONG_PTR token = 0;
                Gdiplus::GdiplusStartup(&token, &gdipInput, nullptr);
                {
                    Gdiplus::Image* img = Gdiplus::Image::FromFile(exportedPath.c_str());
                    if (img && img->GetLastStatus() == Gdiplus::Ok)
                    {
                        UINT num = 0, sz = 0;
                        Gdiplus::GetImageEncodersSize(&num, &sz);
                        if (sz > 0)
                        {
                            std::vector<uint8_t> buf(sz);
                            auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
                            Gdiplus::GetImageEncoders(num, sz, encoders);
                            for (UINT i = 0; i < num; i++)
                            {
                                if (wcscmp(encoders[i].MimeType, L"image/png") == 0)
                                {
                                    if (img->Save(outPath.c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok)
                                    {
                                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Converted to PNG: {}\n"), outPath);
                                        delete img;
                                        DeleteFileW(exportedPath.c_str());
                                        Gdiplus::GdiplusShutdown(token);
                                        return true;
                                    }
                                    break;
                                }
                            }
                        }
                        delete img;
                    }
                }
                Gdiplus::GdiplusShutdown(token);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] PNG conversion failed\n"));
                return false;
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] Exception during extraction\n"));
                return false;
            }
        }

        // ── 7G: Icon Extraction ───────────────────────────────────────────────
        // Extracts UTexture2D from build menu widgets via Canvas render target.
        // Chain: widget+1104 → Image+336 → MID+256 → TextureParamValues[0]+16
        // Pipeline: CreateRenderTarget2D → BeginDraw → K2_DrawTexture → EndDraw
        //           → ExportRenderTarget → GDI+ PNG conversion
        // Icons cached to <game>/Mods/MoriaCppMod/icons/*.png

        // Extract UTexture2D name from a Build_Item_Medium widget
        // Chain: widget+1104 (Icon Image) → Image+264+72 (MID) → MID+256 (TextureParamValues TArray) → entry+16 (UTexture2D*)
        std::wstring extractIconTextureName(UObject* widget)
        {
            if (!widget) return L"";
            try
            {
                uint8_t* base = reinterpret_cast<uint8_t*>(widget);
                // Icon Image at widget+1104
                UObject* iconImg = *reinterpret_cast<UObject**>(base + 1104);
                if (!iconImg || !isReadableMemory(iconImg, 400)) return L"";
                // MID at Image+264+72 = Image+336
                uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImg);
                UObject* mid = *reinterpret_cast<UObject**>(imgBase + 264 + 72);
                if (!mid || !isReadableMemory(mid, 280)) return L"";
                // TextureParameterValues TArray at MID+256
                uint8_t* midBase = reinterpret_cast<uint8_t*>(mid);
                uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + 256);
                int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + 256 + 8);
                if (arrNum < 1 || arrNum > 32 || !arrData || !isReadableMemory(arrData, 40)) return L"";
                // UTexture2D* at entry+16
                UObject* texPtr = *reinterpret_cast<UObject**>(arrData + 16);
                if (!texPtr || !isReadableMemory(texPtr, 64)) return L"";
                UObject** cs = reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(texPtr) + 0x10);
                if (!isReadableMemory(cs, 8) || !*cs || !isReadableMemory(*cs, 64)) return L"";
                return std::wstring(texPtr->GetName());
            }
            catch (...)
            {
                return L"";
            }
        }

        // Find the Build_Item_Medium widget matching a recipe display name
        UObject* findBuildItemWidget(const std::wstring& recipeName)
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;
                std::wstring name = readWidgetDisplayName(w);
                if (name == recipeName) return w;
            }
            return nullptr;
        }

        void assignRecipeSlot(int slot)
        {
            if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return; // F1-F8 only

            // Check if build menu is open — if not, treat as "no build object selected"
            UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
            bool hasBuildObject = m_hasLastCapture && !m_lastCapturedName.empty() && buildTab;

            // No build object selected → clear the slot
            if (!hasBuildObject)
            {
                if (m_recipeSlots[slot].used)
                {
                    m_recipeSlots[slot].displayName.clear();
                    m_recipeSlots[slot].textureName.clear();
                    std::memset(m_recipeSlots[slot].bLockData, 0, BLOCK_DATA_SIZE);
                    m_recipeSlots[slot].hasBLockData = false;
                    m_recipeSlots[slot].used = false;
                    if (m_activeBuilderSlot == slot) m_activeBuilderSlot = -1;
                    saveQuickBuildSlots();
                    updateOverlayText();
                    updateBuildersBar();
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] CLEARED F{}\n"), slot + 1);
                    std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" cleared";
                    showOnScreen(msg.c_str(), 2.0f, 1.0f, 0.5f, 0.0f);
                }
                return;
            }

            m_recipeSlots[slot].displayName = m_lastCapturedName;
            std::memcpy(m_recipeSlots[slot].bLockData, m_lastCapturedBLock, BLOCK_DATA_SIZE);
            m_recipeSlots[slot].hasBLockData = true;
            m_recipeSlots[slot].used = true;

            // Try to extract the icon texture name and save as PNG
            UObject* itemWidget = findBuildItemWidget(m_lastCapturedName);
            if (itemWidget)
            {
                m_recipeSlots[slot].textureName = extractIconTextureName(itemWidget);
                if (!m_recipeSlots[slot].textureName.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] F{} icon: '{}'\n"), slot + 1, m_recipeSlots[slot].textureName);

                    // Extract and save the icon as PNG for the overlay
                    if (!s_overlay.iconFolder.empty())
                    {
                        std::wstring pngPath = s_overlay.iconFolder + L"\\" + m_recipeSlots[slot].textureName + L".png";
                        // Check if PNG already exists (skip extraction)
                        DWORD attr = GetFileAttributesW(pngPath.c_str());
                        if (attr == INVALID_FILE_ATTRIBUTES)
                        {
                            extractAndSaveIcon(itemWidget, m_recipeSlots[slot].textureName, pngPath);
                        }
                        else
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Icon] PNG already exists: {}\n"), pngPath);
                        }
                    }
                }
            }

            saveQuickBuildSlots();
            updateOverlayText();
            updateBuildersBar();

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] ASSIGN F{} = '{}'\n"), slot + 1, m_lastCapturedName);

            std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" = " + m_lastCapturedName;
            showOnScreen(msg.c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
        }

        // ── Deep probe: dump Icon image data from selected Build_Item_Medium widget ──
#if 0 // Disabled: debug probe functions (dumpObjectProperties, probeImageWidget, probeSelectedRecipe)
        void dumpObjectProperties(UObject* obj, const std::wstring& label, int maxProps = 200)
        {
            if (!obj) return;
            uint8_t* base = reinterpret_cast<uint8_t*>(obj);
            int count = 0;
            for (auto* prop : obj->GetClassPrivate()->ForEachPropertyInChain())
            {
                if (count >= maxProps) break;
                std::wstring pname(prop->GetName());
                int offset = prop->GetOffset_Internal();
                int size = prop->GetSize();
                std::wstring typeName(prop->GetClass().GetName());

                std::wstring valueInfo;
                if (size == 8 && typeName.find(STR("Object")) != std::wstring::npos)
                {
                    UObject* ptr = *reinterpret_cast<UObject**>(base + offset);
                    if (ptr && isReadableMemory(ptr, 64))
                    {
                        try
                        {
                            valueInfo = L" -> " + safeClassName(ptr) + L" '" + std::wstring(ptr->GetName()) + L"'";
                        }
                        catch (...)
                        {
                            valueInfo = L" -> (err)";
                        }
                    }
                    else
                    {
                        valueInfo = ptr ? L" -> (unreadable)" : L" -> null";
                    }
                }
                else if (size == 1 && typeName.find(STR("Bool")) != std::wstring::npos)
                {
                    valueInfo = (*(base + offset)) ? L" = true" : L" = false";
                }
                else if (size == 4 && typeName.find(STR("Int")) != std::wstring::npos)
                {
                    valueInfo = L" = " + std::to_wstring(*reinterpret_cast<int32_t*>(base + offset));
                }
                else if (size == 4 && typeName.find(STR("Float")) != std::wstring::npos)
                {
                    wchar_t buf[32]{};
                    swprintf(buf, 32, L" = %.3f", *reinterpret_cast<float*>(base + offset));
                    valueInfo = buf;
                }

                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   {}: {} @{} sz={} type={}{}\n"), label, pname, offset, size, typeName, valueInfo);
                count++;
            }
        }

        // Probe an Image widget to find its texture/brush
        void probeImageWidget(UObject* imageWidget, const std::wstring& name)
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] --- IMAGE '{}' DEEP DIVE ---\n"), name);
            uint8_t* imgBase = reinterpret_cast<uint8_t*>(imageWidget);

            // Step 1: Hex dump the Brush struct (known to be at offset 264, size 136)
            const int BRUSH_OFF = 264;
            const int BRUSH_SZ = 136;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Brush @{} hex dump ({}B):\n"), BRUSH_OFF, BRUSH_SZ);
            for (int row = 0; row < BRUSH_SZ; row += 16)
            {
                std::wstring hex;
                for (int col = 0; col < 16 && (row + col) < BRUSH_SZ; col++)
                {
                    wchar_t h[4]{};
                    swprintf(h, 4, L"%02X ", imgBase[BRUSH_OFF + row + col]);
                    hex += h;
                }
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   +{:3d}: {}\n"), row, hex);
            }

            // Step 2: Read ResourceObject at brush+72 (confirmed MID location)
            // brush+112/120 are TSharedPtr ResourceHandle internals (NOT UObjects — crash on GetName)
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Brush ResourceObject at brush+72:\n"));
            {
                uint64_t val = *reinterpret_cast<uint64_t*>(imgBase + BRUSH_OFF + 72);
                if (val >= 0x10000 && val < 0x00007FF000000000)
                {
                    UObject* ptr = reinterpret_cast<UObject*>(val);
                    if (isReadableMemory(ptr, 64))
                    {
                        try
                        {
                            auto* cls = ptr->GetClassPrivate();
                            std::wstring objCls(cls->GetName());
                            std::wstring objName(ptr->GetName());
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   brush+72: {} '{}' @ 0x{:016X}\n"), objCls, objName, val);
                        }
                        catch (...)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   brush+72: 0x{:016X} (exception)\n"), val);
                        }
                    }
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   brush+72: null or out of range\n"));
                }
            }

            // Step 3: Look for SetBrushFromTexture UFunction (useful for later setting icons)
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Image UFunctions scan:\n"));
            const wchar_t* funcNames[] = {
                    STR("SetBrushFromTexture"),
                    STR("SetBrushResourceObject"),
                    STR("SetBrush"),
                    STR("SetBrushFromMaterial"),
                    STR("SetBrushFromAtlasInterface"),
                    STR("SetBrushFromSoftTexture"),
                    STR("SetColorAndOpacity"),
                    STR("SetOpacity"),
                    STR("GetBrush"),
                    STR("SetBrushSize"),
                    STR("SetBrushTintColor"),
                    STR("SetDesiredSizeOverride"),
            };
            for (auto* fn : funcNames)
            {
                auto* ufn = imageWidget->GetFunctionByNameInChain(fn);
                if (ufn)
                {
                    int pSz = ufn->GetParmsSize();
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   FOUND: {}() ParmsSize={}\n"), fn, pSz);
                    // Dump params
                    for (auto* prop : ufn->ForEachProperty())
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     param: {} @{} sz={} type={}\n"),
                                                        std::wstring(prop->GetName()),
                                                        prop->GetOffset_Internal(),
                                                        prop->GetSize(),
                                                        std::wstring(prop->GetClass().GetName()));
                    }
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] --- IMAGE '{}' PROBE DONE ---\n"), name);
        }

        void probeSelectedRecipe()
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === ICON PROBE: Selected Recipe ===\n"));

            if (!m_hasLastCapture)
            {
                showOnScreen(L"No recipe selected! Click one in Build menu first.", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // Find the Build_Item_Medium widget that matches the last captured name
            UObject* widget = nullptr;
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;
                std::wstring name = readWidgetDisplayName(w);
                if (name == m_lastCapturedName)
                {
                    widget = w;
                    break;
                }
            }
            if (!widget)
            {
                for (auto* w : widgets)
                {
                    if (!w) continue;
                    std::wstring cls = safeClassName(w);
                    if (cls != L"UI_WBP_Build_Item_Medium_C") continue;
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        w->ProcessEvent(visFunc, &vp);
                        if (vp.Ret)
                        {
                            widget = w;
                            break;
                        }
                    }
                }
            }
            if (!widget)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No Build_Item_Medium widget found!\n"));
                showOnScreen(L"Build menu not open or no widget found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            std::wstring displayName = readWidgetDisplayName(widget);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Widget: {} (name: '{}')\n"), std::wstring(widget->GetName()), displayName);

            // Get the Icon Image widget at offset 1104
            uint8_t* base = reinterpret_cast<uint8_t*>(widget);
            UObject* iconImage = *reinterpret_cast<UObject**>(base + 1104);
            if (iconImage && isReadableMemory(iconImage, 64))
            {
                std::wstring iconCls = safeClassName(iconImage);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Icon widget: {} '{}' @ {:p}\n"),
                                                iconCls,
                                                std::wstring(iconImage->GetName()),
                                                static_cast<void*>(iconImage));

                probeImageWidget(iconImage, L"Icon");

                // Step: Probe the MaterialInstanceDynamic at brush+72
                uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImage);
                const int BRUSH_OFF = 264;
                UObject* mid = *reinterpret_cast<UObject**>(imgBase + BRUSH_OFF + 72);
                if (mid && isReadableMemory(mid, 64))
                {
                    try
                    {
                        std::wstring midCls = safeClassName(mid);
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === MID PROBE: {} '{}' ===\n"), midCls, std::wstring(mid->GetName()));

                        // Dump MID properties (looking for TextureParameterValues, etc.)
                        dumpObjectProperties(mid, L"mid", 60);

                        // Try GetTextureParameterValue with common parameter names
                        auto* getTexParam = mid->GetFunctionByNameInChain(STR("GetTextureParameterValue"));
                        if (getTexParam)
                        {
                            int pSz = getTexParam->GetParmsSize();
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetTextureParameterValue() ParmsSize={}\n"), pSz);
                            for (auto* prop : getTexParam->ForEachProperty())
                            {
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   param: {} @{} sz={} type={}\n"),
                                                                std::wstring(prop->GetName()),
                                                                prop->GetOffset_Internal(),
                                                                prop->GetSize(),
                                                                std::wstring(prop->GetClass().GetName()));
                            }

                            // Try common texture parameter names
                            const wchar_t* texParamNames[] = {
                                    STR("Texture"),
                                    STR("BaseColor"),
                                    STR("Icon"),
                                    STR("DiffuseTexture"),
                                    STR("BaseColorTexture"),
                                    STR("MainTexture"),
                                    STR("Image"),
                                    STR("T_Icon"),
                                    STR("Param"),
                                    STR("TextureParam"),
                            };
                            for (auto* paramName : texParamNames)
                            {
                                // GetTextureParameterValue(FName ParameterName, UTexture*& OutValue) -> bool
                                // Layout: FName(8B) + UTexture*(8B) + bool ReturnValue(1B) = 17B typically
                                // But ParmsSize tells us the actual layout
                                struct GetTexParamArgs
                                {
                                    FName ParamName;
                                    UObject* OutValue{nullptr};
                                    bool ReturnValue{false};
                                    uint8_t pad[7]{}; // safety padding
                                };
                                GetTexParamArgs args{};
                                args.ParamName = FName(paramName, FNAME_Add);
                                mid->ProcessEvent(getTexParam, &args);

                                if (args.ReturnValue && args.OutValue)
                                {
                                    std::wstring texCls = safeClassName(args.OutValue);
                                    std::wstring texName(args.OutValue->GetName());
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] >>> GetTextureParameterValue('{}') = {} '{}' <<<\n"), paramName, texCls, texName);
                                    // Walk outer chain for full path
                                    UObject* cur = args.OutValue;
                                    for (int d = 0; d < 5 && cur; d++)
                                    {
                                        auto* o = cur->GetOuterPrivate();
                                        if (!o) break;
                                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   outer[{}]: {} '{}'\n"), d, safeClassName(o), std::wstring(o->GetName()));
                                        cur = o;
                                    }
                                    dumpObjectProperties(args.OutValue, L"tex", 30);
                                }
                            }
                        }
                        else
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] GetTextureParameterValue NOT found as UFunction\n"));
                        }

                        // Read TextureParameterValues TArray directly from MID+256
                        // TArray layout: { void* Data (8B), int32 Num (4B), int32 Max (4B) } = 16B
                        // FTextureParameterValue layout (UE4.27):
                        //   FMaterialParameterInfo { FName(8B) + int32 Association(4B) + int32 Index(4B) } = 16B
                        //   UTexture* ParameterValue = 8B
                        //   FGuid ExpressionGUID = 16B
                        //   Total = 40B per entry
                        uint8_t* midBase = reinterpret_cast<uint8_t*>(mid);
                        const int TEX_PARAMS_OFF = 256;
                        uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + TEX_PARAMS_OFF);
                        int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + TEX_PARAMS_OFF + 8);
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] TextureParameterValues TArray: Data={:p} Num={}\n"), static_cast<void*>(arrData), arrNum);

                        if (arrNum > 0 && arrNum < 32 && arrData && isReadableMemory(arrData, arrNum * 40))
                        {
                            // Try multiple entry sizes in case layout differs
                            const int ENTRY_SIZES[] = {40, 48, 56, 32};
                            for (int entrySz : ENTRY_SIZES)
                            {
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Trying entry size {}B:\n"), entrySz);
                                bool foundAny = false;

                                for (int i = 0; i < arrNum && i < 8; i++)
                                {
                                    uint8_t* entry = arrData + i * entrySz;
                                    if (!isReadableMemory(entry, entrySz)) break;

                                    // Hex dump entry
                                    std::wstring hex;
                                    for (int b = 0; b < entrySz && b < 56; b++)
                                    {
                                        wchar_t h[4]{};
                                        swprintf(h, 4, L"%02X ", entry[b]);
                                        hex += h;
                                    }
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   entry[{}]: {}\n"), i, hex);

                                    // Try reading UTexture* at offset 16 (after FMaterialParameterInfo)
                                    for (int texOff : {16, 8, 24, 32})
                                    {
                                        if (texOff + 8 > entrySz) continue;
                                        uint64_t val = *reinterpret_cast<uint64_t*>(entry + texOff);
                                        if (val < 0x10000 || val > 0x00007FF000000000) continue;
                                        UObject* texPtr = reinterpret_cast<UObject*>(val);
                                        if (!isReadableMemory(texPtr, 64)) continue;
                                        UObject** cs = reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(texPtr) + 0x10);
                                        if (!isReadableMemory(cs, 8) || !*cs || !isReadableMemory(*cs, 64)) continue;
                                        try
                                        {
                                            std::wstring cls = safeClassName(texPtr);
                                            std::wstring nm(texPtr->GetName());
                                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   >>> entry[{}]+{}: {} '{}' <<<\n"), i, texOff, cls, nm);
                                            foundAny = true;
                                            // Walk outer chain
                                            UObject* cur = texPtr;
                                            for (int d = 0; d < 5 && cur; d++)
                                            {
                                                auto* o = cur->GetOuterPrivate();
                                                if (!o) break;
                                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     outer[{}]: {} '{}'\n"),
                                                                                d,
                                                                                safeClassName(o),
                                                                                std::wstring(o->GetName()));
                                                cur = o;
                                            }
                                            // Dump Texture2D properties
                                            dumpObjectProperties(texPtr, L"tex", 40);

                                            // Scan texture memory for PlatformData pointer
                                            // PlatformData contains SizeX, SizeY as consecutive int32 at its start
                                            // FTexturePlatformData layout: int32 SizeX, int32 SizeY, int32 NumSlices, EPixelFormat PixelFormat, ...
                                            // We look for a pointer to a struct where first 8 bytes = {SizeX, SizeY} with plausible icon sizes
                                            uint8_t* texBase = reinterpret_cast<uint8_t*>(texPtr);
                                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Texture raw scan for PlatformData (offsets 8-800):\n"));
                                            for (int toff = 8; toff < 800; toff += 8)
                                            {
                                                if (!isReadableMemory(texBase + toff, 8)) break;
                                                uint64_t tval = *reinterpret_cast<uint64_t*>(texBase + toff);
                                                if (tval < 0x10000 || tval > 0x00007FF000000000) continue;
                                                uint8_t* candidate = reinterpret_cast<uint8_t*>(tval);
                                                if (!isReadableMemory(candidate, 64)) continue;
                                                // Read first two int32s as potential SizeX, SizeY
                                                int32_t sx = *reinterpret_cast<int32_t*>(candidate);
                                                int32_t sy = *reinterpret_cast<int32_t*>(candidate + 4);
                                                // Accept any reasonable texture size (removed power-of-2 and square requirements)
                                                if (sx > 0 && sx <= 4096 && sy > 0 && sy <= 4096)
                                                {
                                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   tex+{}: PlatformData? {}x{} @ 0x{:016X}\n"), toff, sx, sy, tval);
                                                    // Dump first 80 bytes of PlatformData
                                                    std::wstring pdHex;
                                                    for (int pb = 0; pb < 80; pb++)
                                                    {
                                                        wchar_t h[4]{};
                                                        swprintf(h, 4, L"%02X ", candidate[pb]);
                                                        pdHex += h;
                                                        if ((pb + 1) % 16 == 0)
                                                        {
                                                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     +{:3d}: {}\n"), pb - 15, pdHex);
                                                            pdHex.clear();
                                                        }
                                                    }
                                                    // Check for Mips TIndirectArray at PlatformData+16 or +20
                                                    // TIndirectArray: void** Data, int32 Num, int32 Max
                                                    for (int mipOff : {16, 20, 24, 32})
                                                    {
                                                        if (!isReadableMemory(candidate + mipOff, 16)) continue;
                                                        uint8_t* mipArrData = *reinterpret_cast<uint8_t**>(candidate + mipOff);
                                                        int32_t mipNum = *reinterpret_cast<int32_t*>(candidate + mipOff + 8);
                                                        if (mipNum > 0 && mipNum <= 16 && mipArrData && isReadableMemory(mipArrData, mipNum * 8))
                                                        {
                                                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod]     PD+{}: Mips? Num={} Data={:p}\n"),
                                                                                            mipOff,
                                                                                            mipNum,
                                                                                            static_cast<void*>(mipArrData));
                                                            // Read first mip pointer
                                                            uint8_t* mip0 = *reinterpret_cast<uint8_t**>(mipArrData);
                                                            if (mip0 && isReadableMemory(mip0, 64))
                                                            {
                                                                // FTexture2DMipMap: int32 SizeX, SizeY, SizeZ, then FByteBulkData
                                                                int32_t m0sx = *reinterpret_cast<int32_t*>(mip0);
                                                                int32_t m0sy = *reinterpret_cast<int32_t*>(mip0 + 4);
                                                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]       Mip0: {}x{} @ {:p}\n"),
                                                                                                m0sx,
                                                                                                m0sy,
                                                                                                static_cast<void*>(mip0));
                                                                // Hex dump first 128 bytes of mip0
                                                                if (isReadableMemory(mip0, 128))
                                                                {
                                                                    for (int mr = 0; mr < 128; mr += 16)
                                                                    {
                                                                        std::wstring mhex;
                                                                        for (int mc = 0; mc < 16; mc++)
                                                                        {
                                                                            wchar_t h[4]{};
                                                                            swprintf(h, 4, L"%02X ", mip0[mr + mc]);
                                                                            mhex += h;
                                                                        }
                                                                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]         +{:3d}: {}\n"), mr, mhex);
                                                                    }
                                                                }
                                                                // Scan mip0 for heap pointers (bulk data pointer)
                                                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod]       Mip0 pointer scan:\n"));
                                                                for (int mp = 8; mp < 120; mp += 8)
                                                                {
                                                                    if (!isReadableMemory(mip0 + mp, 8)) break;
                                                                    uint64_t mpv = *reinterpret_cast<uint64_t*>(mip0 + mp);
                                                                    if (mpv < 0x10000 || mpv > 0x00007FF000000000) continue;
                                                                    void* mpPtr = reinterpret_cast<void*>(mpv);
                                                                    if (!isReadableMemory(mpPtr, 16)) continue;
                                                                    // Check allocation size with VirtualQuery
                                                                    MEMORY_BASIC_INFORMATION mbi{};
                                                                    if (VirtualQuery(mpPtr, &mbi, sizeof(mbi)))
                                                                    {
                                                                        size_t regionSz = mbi.RegionSize;
                                                                        Output::send<LogLevel::Warning>(
                                                                                STR("[MoriaCppMod]         mip0+{}: 0x{:016X} regionSz={} state={}\n"),
                                                                                mp,
                                                                                mpv,
                                                                                regionSz,
                                                                                mbi.State);
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        catch (...)
                                        {
                                        }
                                    }
                                }
                                if (foundAny) break; // found textures with this entry size
                            }
                        }
                        else if (arrNum == 0)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] TextureParameterValues is empty!\n"));
                        }

                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END MID PROBE ===\n"));
                    }
                    catch (...)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] MID probe exception\n"));
                    }
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No MID at brush+72\n"));
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Icon @1104 is null or unreadable!\n"));
            }

            // Also probe blockName text for completeness
            UObject* blockName = *reinterpret_cast<UObject**>(base + 1040);
            if (blockName && isReadableMemory(blockName, 64))
            {
                auto* getTextFn = blockName->GetFunctionByNameInChain(STR("GetText"));
                if (getTextFn && getTextFn->GetParmsSize() == sizeof(FText))
                {
                    FText txt{};
                    blockName->ProcessEvent(getTextFn, &txt);
                    if (txt.Data) Output::send<LogLevel::Warning>(STR("[MoriaCppMod] blockName text = \"{}\"\n"), txt.ToString());
                }
            }

            // Also probe StackCount text
            UObject* stackCount = *reinterpret_cast<UObject**>(base + 1176);
            if (stackCount && isReadableMemory(stackCount, 64))
            {
                auto* getTextFn = stackCount->GetFunctionByNameInChain(STR("GetText"));
                if (getTextFn && getTextFn->GetParmsSize() == sizeof(FText))
                {
                    FText txt{};
                    stackCount->ProcessEvent(getTextFn, &txt);
                    if (txt.Data) Output::send<LogLevel::Warning>(STR("[MoriaCppMod] StackCount text = \"{}\"\n"), txt.ToString());
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END ICON PROBE ===\n"));
            showOnScreen(L"Icon probe done (see UE4SS log)", 5.0f, 0.0f, 1.0f, 0.5f);
        }
#endif // Disabled: debug probe functions

        // Read display name from a Build_Item_Medium widget's blockName TextBlock
        std::wstring readWidgetDisplayName(UObject* widget)
        {
            uint8_t* base = reinterpret_cast<uint8_t*>(widget);
            UObject* blockNameWidget = *reinterpret_cast<UObject**>(base + 1040);
            if (!blockNameWidget) return L"";

            auto* getTextFunc = blockNameWidget->GetFunctionByNameInChain(STR("GetText"));
            if (!getTextFunc || getTextFunc->GetParmsSize() != sizeof(FText)) return L"";

            FText textResult{};
            blockNameWidget->ProcessEvent(getTextFunc, &textResult);
            if (!textResult.Data) return L"";
            return textResult.ToString();
        }

        // Find a Build_Item_Medium widget whose display name matches, then trigger blockSelectedEvent
        void selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] SELECT: searching for '{}' (slot F{})\n"), targetName, slot + 1);

            // Find all Build_Item_Medium widgets and match by display name
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            int visibleCount = 0;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                // Only use VISIBLE widgets — stale/recycled widgets have invalid internal state
                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    w->ProcessEvent(visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                if (!name.empty() && name == targetName)
                {
                    matchedWidget = w;
                    break;
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   checked {} visible widgets, match: {}\n"), visibleCount, matchedWidget ? L"YES" : L"NO");

            if (!matchedWidget)
            {
                showOnScreen((L"Recipe '" + targetName + L"' not found in menu!").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return;

            // blockSelectedEvent params: bLock@0(120B) + selfRef@120(8B) + Index@128(4B)
            uint8_t params[132]{};
            bool gotFreshBLock = false;

            // BEST: read fresh bLock directly from the matched widget (offset discovered at runtime)
            if (m_bLockWidgetOffset >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                if (m_bLockIsIndirect)
                {
                    uint8_t* ptr = *reinterpret_cast<uint8_t**>(widgetBase + m_bLockWidgetOffset);
                    if (ptr && isReadableMemory(ptr, BLOCK_DATA_SIZE))
                    {
                        std::memcpy(params, ptr, BLOCK_DATA_SIZE);
                        gotFreshBLock = true;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (indirect @{})\n"), m_bLockWidgetOffset);
                    }
                }
                else
                {
                    std::memcpy(params, widgetBase + m_bLockWidgetOffset, BLOCK_DATA_SIZE);
                    gotFreshBLock = true;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (direct @{})\n"), m_bLockWidgetOffset);
                }
            }

            // FALLBACK: use captured bLock from assignment (may have stale pointer at +104)
            if (!gotFreshBLock && m_recipeSlots[slot].hasBLockData)
            {
                std::memcpy(params, m_recipeSlots[slot].bLockData, BLOCK_DATA_SIZE);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   using SAVED bLock (may be stale)\n"));
            }
            else if (!gotFreshBLock)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   WARNING: no bLock data at all, using zeros\n"));
            }

            *reinterpret_cast<UObject**>(params + 120) = matchedWidget;
            *reinterpret_cast<int32_t*>(params + 128) = 0;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild]   calling blockSelectedEvent with selfRef={:p}\n"), static_cast<void*>(matchedWidget));

            // Suppress post-hook capture during automated selection (RAII guard ensures reset on exception)
            m_isAutoSelecting = true;
            struct AutoSelectGuard
            {
                bool& flag;
                ~AutoSelectGuard() { flag = false; }
            } guard{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params);
            m_isAutoSelecting = false;

            showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true; // track menu so we refresh ActionBar when it closes
            refreshActionBar();        // also refresh immediately after recipe selection

            // Set this slot as Active on the builders bar, all others become Inactive/Empty
            m_activeBuilderSlot = slot;
            updateBuildersBar();
        }

        void quickBuildSlot(int slot)
        {
            if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return; // F1-F8 only

            if (!m_recipeSlots[slot].used)
            {
                std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" empty \x2014 select recipe in B menu, then " + modifierName(s_modifierVK) + L"+F" + std::to_wstring(slot + 1);
                showOnScreen(msg.c_str(), 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            // Guard: if a previous quickbuild is still pending, skip
            if (m_pendingQuickBuildSlot >= 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] F{} pressed but slot {} already pending (frame {}), ignoring\n"),
                                                slot + 1,
                                                m_pendingQuickBuildSlot + 1,
                                                m_pendingBuildFrames);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] ACTIVATE F{} -> '{}' (charLoaded={} frameCounter={})\n"),
                                            slot + 1,
                                            m_recipeSlots[slot].displayName,
                                            m_characterLoaded,
                                            m_frameCounter);

            // Always close the build menu first if it's open, then reopen fresh.
            // Reusing a stale menu session causes widget recycling issues.
            UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
            if (buildTab)
            {
                // Close existing menu first
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] Build tab already open — closing first (pendingFrames=-15)\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_pendingQuickBuildSlot = slot;
                m_pendingBuildFrames = -15; // negative = wait for close, then reopen at frame 0
            }
            else
            {
                // Menu not open — just open it
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] Build tab not open — sending B key (pendingFrames=0)\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_pendingQuickBuildSlot = slot;
                m_pendingBuildFrames = 0;
            }
        }

        // ── Build from Target: Shift+F10 — build the last targeted actor ──

        void buildFromTarget()
        {
            Output::send<LogLevel::Warning>(
                    STR("[MoriaCppMod] [TargetBuild] buildFromTarget() called: buildable={} name='{}' recipeRef='{}' charLoaded={} frame={}\n"),
                    m_lastTargetBuildable,
                    m_targetBuildName,
                    m_targetBuildRecipeRef,
                    m_characterLoaded,
                    m_frameCounter);

            if (m_pendingTargetBuild)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Already pending (frame {}), ignoring\n"), m_pendingTargetBuildFrames);
                return;
            }

            if (!m_lastTargetBuildable || (m_targetBuildName.empty() && m_targetBuildRecipeRef.empty()))
            {
                showOnScreen(L"No buildable target \x2014 aim at a building and press F10 first", 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            // Same pattern as quickBuildSlot: open/reopen build menu, then select by name
            UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
            if (buildTab)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Build tab already open — closing first\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_pendingTargetBuild = true;
                m_pendingTargetBuildFrames = -15;
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Build tab not open — sending B key\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_pendingTargetBuild = true;
                m_pendingTargetBuildFrames = 0;
            }
        }

        // Normalize a string for fuzzy matching: lowercase, keep only alphanumeric
        static std::wstring normalizeForMatch(const std::wstring& s)
        {
            std::wstring out;
            out.reserve(s.size());
            for (wchar_t c : s)
            {
                if (std::iswalnum(c)) out += std::towlower(c);
            }
            return out;
        }

        void selectRecipeByTargetName(UObject* buildTab)
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Searching: name='{}' recipeRef='{}' bLockOffset={} indirect={}\n"),
                                            m_targetBuildName,
                                            m_targetBuildRecipeRef,
                                            m_bLockWidgetOffset,
                                            m_bLockIsIndirect);

            // Build FName from our recipeRef for ComparisonIndex matching
            // Try several forms the DataTable row name might use
            std::vector<std::pair<std::wstring, uint32_t>> targetFNames;
            {
                // Full ref: "BP_Suburbs_Wall_Thick_4x1m_A"
                FName fn1(m_targetBuildRecipeRef.c_str());
                uint32_t ci1 = *reinterpret_cast<uint32_t*>(&fn1);
                targetFNames.push_back({m_targetBuildRecipeRef, ci1});

                // Without BP_ prefix: "Suburbs_Wall_Thick_4x1m_A"
                std::wstring noBP = m_targetBuildRecipeRef;
                if (noBP.size() > 3 && noBP.substr(0, 3) == L"BP_") noBP = noBP.substr(3);
                FName fn2(noBP.c_str());
                uint32_t ci2 = *reinterpret_cast<uint32_t*>(&fn2);
                targetFNames.push_back({noBP, ci2});

                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] FName CIs: full='{}' CI={}, short='{}' CI={}\n"),
                                                m_targetBuildRecipeRef,
                                                ci1,
                                                noBP,
                                                ci2);
            }

            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            std::wstring matchedName;
            int visibleCount = 0;
            int bLockNullCount = 0, bLockMemFailCount = 0, variantsEmptyCount = 0;

            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    w->ProcessEvent(visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                bool isFirstFew = (visibleCount <= 5);

                // Log first 5 widgets in detail for diagnostics
                if (isFirstFew)
                {
                    std::wstring objName(w->GetName());
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]   W[{}] obj='{}' display='{}'\n"), visibleCount, objName, name);
                }

                // Strategy 1: bLock-based matching via Variants->ResultConstructionHandle RowName
                if (!matchedWidget && m_bLockWidgetOffset >= 0 && !m_targetBuildRecipeRef.empty())
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(w);
                    uint8_t* bLock = nullptr;

                    if (m_bLockIsIndirect)
                    {
                        uint8_t* ptr = *reinterpret_cast<uint8_t**>(widgetBase + m_bLockWidgetOffset);
                        if (ptr && isReadableMemory(ptr, BLOCK_DATA_SIZE)) bLock = ptr;
                    }
                    else
                    {
                        bLock = widgetBase + m_bLockWidgetOffset;
                    }

                    if (!bLock)
                    {
                        bLockNullCount++;
                        if (isFirstFew) Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]     bLock=NULL\n"));
                    }
                    else if (!isReadableMemory(bLock + 104, 16))
                    {
                        bLockMemFailCount++;
                        if (isFirstFew) Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]     bLock+104 not readable\n"));
                    }
                    else
                    {
                        uint8_t* variantsPtr = *reinterpret_cast<uint8_t**>(bLock + 104);
                        int32_t variantsCount = *reinterpret_cast<int32_t*>(bLock + 112);

                        if (isFirstFew)
                        {
                            // Log first 8 bytes of bLock (FGameplayTag FName)
                            uint32_t tagCI = *reinterpret_cast<uint32_t*>(bLock);
                            int32_t tagNum = *reinterpret_cast<int32_t*>(bLock + 4);
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]     bLock tag CI={} Num={} | variants={} ptr={:p}\n"),
                                                            tagCI,
                                                            tagNum,
                                                            variantsCount,
                                                            (void*)variantsPtr);
                        }

                        if (variantsCount <= 0 || !variantsPtr)
                        {
                            variantsEmptyCount++;
                        }
                        else if (isReadableMemory(variantsPtr, 0xE8))
                        {
                            // Read ResultConstructionHandle.RowName at variant+0xE0
                            uint32_t rowCI = *reinterpret_cast<uint32_t*>(variantsPtr + 0xE0);
                            int32_t rowNum = *reinterpret_cast<int32_t*>(variantsPtr + 0xE4);

                            if (isFirstFew)
                            {
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]     RowName CI={} Num={}\n"), rowCI, rowNum);
                            }

                            // Check if RowName CI matches any of our target FName CIs
                            for (auto& [tName, tCI] : targetFNames)
                            {
                                if (tCI == rowCI)
                                {
                                    matchedWidget = w;
                                    matchedName = name.empty() ? tName : name;
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] MATCH (bLock RowName CI={}) on '{}' target='{}'\n"),
                                                                    rowCI,
                                                                    matchedName,
                                                                    tName);
                                    break;
                                }
                            }
                        }
                        else if (isFirstFew)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild]     variantsPtr not readable (0xE8 bytes)\n"));
                        }
                    }
                }

                // Strategy 2: Exact display name match
                if (!matchedWidget && !name.empty() && name == m_targetBuildName)
                {
                    matchedWidget = w;
                    matchedName = name;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] MATCH (exact display name) on '{}'\n"), name);
                }

                // Strategy 3: Fuzzy display name match (containment)
                if (!matchedWidget && !name.empty())
                {
                    std::wstring nameNorm = normalizeForMatch(name);
                    std::wstring refNoBP = normalizeForMatch(m_targetBuildRecipeRef);
                    if (refNoBP.size() > 2 && refNoBP.substr(0, 2) == L"bp") refNoBP = refNoBP.substr(2);
                    std::wstring targetNorm = normalizeForMatch(m_targetBuildName);

                    if (nameNorm == refNoBP || nameNorm == targetNorm)
                    {
                        matchedWidget = w;
                        matchedName = name;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] MATCH (normalized exact) on '{}'\n"), name);
                    }
                }

                if (matchedWidget) break;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Result: {} visible, bLockNull={} memFail={} varEmpty={} match={}\n"),
                                            visibleCount,
                                            bLockNullCount,
                                            bLockMemFailCount,
                                            variantsEmptyCount,
                                            matchedWidget ? matchedName.c_str() : L"NO");

            if (!matchedWidget)
            {
                showOnScreen((L"Recipe '" + m_targetBuildName + L"' not found in build menu").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return;

            uint8_t params[132]{};

            // Read fresh bLock data from the matched widget
            bool gotFreshBLock = false;
            if (m_bLockWidgetOffset >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                if (m_bLockIsIndirect)
                {
                    uint8_t* ptr = *reinterpret_cast<uint8_t**>(widgetBase + m_bLockWidgetOffset);
                    if (ptr && isReadableMemory(ptr, BLOCK_DATA_SIZE))
                    {
                        std::memcpy(params, ptr, BLOCK_DATA_SIZE);
                        gotFreshBLock = true;
                    }
                }
                else
                {
                    std::memcpy(params, widgetBase + m_bLockWidgetOffset, BLOCK_DATA_SIZE);
                    gotFreshBLock = true;
                }
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] Calling blockSelectedEvent: freshBLock={} selfRef={:p}\n"),
                                            gotFreshBLock,
                                            static_cast<void*>(matchedWidget));

            *reinterpret_cast<UObject**>(params + 120) = matchedWidget;
            *reinterpret_cast<int32_t*>(params + 128) = 0;

            // Suppress post-hook capture during automated selection (RAII guard ensures reset on exception)
            m_isAutoSelecting = true;
            struct AutoSelectGuard
            {
                bool& flag;
                ~AutoSelectGuard() { flag = false; }
            } guard{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params);
            m_isAutoSelecting = false;

            showOnScreen((L"Build: " + matchedName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true; // track menu so we refresh ActionBar when it closes
            refreshActionBar();        // also refresh immediately after recipe selection
        }

        // ── Hotbar Display: Win32 overlay at top-center of screen ──

        // ── 7H2: Experimental UMG Widget Bar ─────────────────────────────────
        // Creates a transparent UMG widget with 12 action bar frame images in a row.
        // Uses StaticConstructObject to create UImage + UHorizontalBox widgets at runtime.

        // Helper: set a UImage's brush to a texture via ProcessEvent
        void umgSetBrush(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = true;
            img->ProcessEvent(setBrushFn, bp.data());
        }

        // Helper: set opacity on a UImage
        void umgSetOpacity(UObject* img, float opacity)
        {
            auto* fn = img->GetFunctionByNameInChain(STR("SetOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal()) = opacity;
            img->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetSize(FSlateChildSize) on a slot
        void umgSetSlotSize(UObject* slot, float value, uint8_t sizeRule)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* base = buf.data() + p->GetOffset_Internal();
            *reinterpret_cast<float*>(base + 0) = value;
            *reinterpret_cast<uint8_t*>(base + 4) = sizeRule;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetPadding on a slot (FMargin: Left, Top, Right, Bottom)
        void umgSetSlotPadding(UObject* slot, float left, float top, float right, float bottom)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetPadding"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InPadding"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* m = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            m[0] = left; m[1] = top; m[2] = right; m[3] = bottom;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetHorizontalAlignment on a slot
        void umgSetHAlign(UObject* slot, uint8_t align)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InHorizontalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetVerticalAlignment on a slot
        void umgSetVAlign(UObject* slot, uint8_t align)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InVerticalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetRenderScale on a UWidget (FVector2D: ScaleX, ScaleY)
        void umgSetRenderScale(UObject* widget, float sx, float sy)
        {
            auto* fn = widget->GetFunctionByNameInChain(STR("SetRenderScale"));
            if (!fn) return;
            auto* p = findParam(fn, STR("Scale"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = sx; v[1] = sy;
            widget->ProcessEvent(fn, buf.data());
        }

        // Helper: set text on a UTextBlock via SetText(FText)
        void umgSetText(UObject* textBlock, const std::wstring& text)
        {
            if (!textBlock) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetText"));
            if (!fn) return;
            auto* pInText = findParam(fn, STR("InText"));
            if (!pInText) return;
            FText ftext(text.c_str());
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pInText->GetOffset_Internal(), &ftext, sizeof(FText));
            textBlock->ProcessEvent(fn, buf.data());
        }

        // Helper: set text color on a UTextBlock via SetColorAndOpacity(FSlateColor)
        void umgSetTextColor(UObject* textBlock, float r, float g, float b, float a)
        {
            if (!textBlock) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InColorAndOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* color = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            color[0] = r; color[1] = g; color[2] = b; color[3] = a;
            // ColorUseRule at offset 0x10 stays 0 (UseColor_Specified) from zero-init
            textBlock->ProcessEvent(fn, buf.data());
        }

        // Helper: set bold typeface on a UTextBlock by patching FSlateFontInfo.TypefaceFontName
        // FSlateFontInfo layout (SlateCore.hpp:309): FontObject@0x00, FontMaterial@0x08,
        //   OutlineSettings@0x10(0x20), TypefaceFontName@0x40(FName), Size@0x48, LetterSpacing@0x4C
        // UTextBlock::Font property at offset 0x0188 (size 0x58)
        void umgSetBold(UObject* textBlock)
        {
            if (!textBlock) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            // Read current FSlateFontInfo from the TextBlock
            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[0x58];
            std::memcpy(fontBuf, tbRaw + 0x0188, 0x58);

            // Patch TypefaceFontName (FName at offset 0x40) to "Bold"
            RC::Unreal::FName boldName(STR("Bold"), RC::Unreal::FNAME_Add);
            std::memcpy(fontBuf + 0x40, &boldName, sizeof(RC::Unreal::FName));

            // Call SetFont with the patched FSlateFontInfo
            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, 0x58);
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        // Set font size on a UTextBlock (patches FSlateFontInfo.Size at struct offset 0x48)
        void umgSetFontSize(UObject* textBlock, int32_t fontSize)
        {
            if (!textBlock) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[0x58];
            std::memcpy(fontBuf, tbRaw + 0x0188, 0x58);
            // Patch Size (int32 at offset 0x48)
            std::memcpy(fontBuf + 0x48, &fontSize, sizeof(int32_t));

            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, 0x58);
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        // Refresh key labels on all UMG toolbars from current s_bindings
        void refreshKeyLabels()
        {
            for (int i = 0; i < 8; i++)
                if (m_umgKeyLabels[i])
                    umgSetText(m_umgKeyLabels[i], keyName(s_bindings[i].key));
            for (int i = 0; i < MC_SLOTS; i++)
                if (m_mcKeyLabels[i])
                    umgSetText(m_mcKeyLabels[i], keyName(s_bindings[MC_BIND_BASE + i].key));
            // Advanced Builder toolbar key label
            if (m_abKeyLabel)
                umgSetText(m_abKeyLabel, keyName(s_bindings[BIND_AB_OPEN].key));
        }

        // Update MC slot 0 rotation label text from current rotation atomics
        void updateMcRotationLabel()
        {
            if (!m_mcRotationLabel) return;
            int step = s_overlay.rotationStep;
            int total = s_overlay.totalRotation;
            std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
            umgSetText(m_mcRotationLabel, txt);
        }

        // Set a toolbar slot's state icon (Empty/Inactive/Active)
        void setUmgSlotState(int slot, UmgSlotState state)
        {
            if (slot < 0 || slot >= 8 || !m_umgStateImages[slot]) return;
            m_umgSlotStates[slot] = state;
            UObject* tex = nullptr;
            switch (state)
            {
            case UmgSlotState::Empty:    tex = m_umgTexEmpty; break;
            case UmgSlotState::Inactive: tex = m_umgTexInactive; break;
            case UmgSlotState::Active:   tex = m_umgTexActive; break;
            }
            if (!tex || !m_umgSetBrushFn) return;
            umgSetBrush(m_umgStateImages[slot], tex, m_umgSetBrushFn);
        }

        // Helper: set brush WITHOUT matching size (for icon images that need explicit sizing)
        void umgSetBrushNoMatch(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = false;
            img->ProcessEvent(setBrushFn, bp.data());
        }

        // Helper: call SetBrushSize on a UImage (FVector2D: Width, Height)
        void umgSetBrushSize(UObject* img, float w, float h)
        {
            auto* fn = img->GetFunctionByNameInChain(STR("SetBrushSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("DesiredSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = w; v[1] = h;
            img->ProcessEvent(fn, buf.data());
        }

        // Set a UMG icon image's recipe texture (or hide if nullptr)
        // Sizes the icon to fit within the state icon bounds, preserving aspect ratio, shrunk 5%
        void setUmgSlotIcon(int slot, UObject* texture)
        {
            if (slot < 0 || slot >= 8 || !m_umgIconImages[slot] || !m_umgSetBrushFn) return;
            m_umgIconTextures[slot] = texture;
            if (texture)
            {
                // Set texture WITH bMatchSize=true so ImageSize gets the native tex dimensions
                umgSetBrush(m_umgIconImages[slot], texture, m_umgSetBrushFn);

                // Read the icon texture's native size from the brush (FSlateBrush.ImageSize at UImage+0x108+0x08)
                uint8_t* iBase = reinterpret_cast<uint8_t*>(m_umgIconImages[slot]);
                float texW = *reinterpret_cast<float*>(iBase + 0x108 + 0x08);
                float texH = *reinterpret_cast<float*>(iBase + 0x108 + 0x0C);

                // Get state icon size as the container bounds
                float containerW = 64.0f;
                float containerH = 64.0f;
                if (m_umgStateImages[slot])
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(m_umgStateImages[slot]);
                    containerW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
                    containerH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);
                }
                if (containerW < 1.0f) containerW = 64.0f;
                if (containerH < 1.0f) containerH = 64.0f;

                // Scale icon to fit within state icon, preserving aspect ratio, then shrink 5%
                float iconW = containerW;
                float iconH = containerH;
                if (texW > 0.0f && texH > 0.0f)
                {
                    float scaleX = containerW / texW;
                    float scaleY = containerH / texH;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY; // fit inside
                    scale *= 0.76f; // shrink to fit (95% * 80% = 76%)
                    iconW = texW * scale;
                    iconH = texH * scale;
                }

                umgSetBrushSize(m_umgIconImages[slot], iconW, iconH);
                umgSetOpacity(m_umgIconImages[slot], 1.0f); // fully visible
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Slot #{} icon sized: {}x{} (container: {}x{}, tex: {}x{})\n"),
                                                slot, iconW, iconH, containerW, containerH, texW, texH);
            }
            else
            {
                umgSetOpacity(m_umgIconImages[slot], 0.0f); // hidden
            }
        }

        // Find a UTexture2D by name from all loaded textures
        UObject* findTexture2DByName(const std::wstring& name)
        {
            if (name.empty()) return nullptr;
            std::vector<UObject*> textures;
            UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
            for (auto* t : textures)
            {
                if (!t) continue;
                if (std::wstring(t->GetName()) == name) return t;
            }
            return nullptr;
        }

        // Sync quick-build recipe slot states to UMG builders bar
        void updateBuildersBar()
        {
            if (!m_umgBarWidget) return;
            for (int i = 0; i < 8; i++)
            {
                if (!m_recipeSlots[i].used)
                {
                    setUmgSlotState(i, UmgSlotState::Empty);
                    if (m_umgIconTextures[i] || !m_umgIconNames[i].empty())
                    {
                        setUmgSlotIcon(i, nullptr);
                        m_umgIconNames[i].clear();
                    }
                }
                else
                {
                    if (i == m_activeBuilderSlot)
                        setUmgSlotState(i, UmgSlotState::Active);
                    else
                        setUmgSlotState(i, UmgSlotState::Inactive);

                    // Check if texture name changed (new recipe assigned to this slot)
                    bool nameChanged = (m_umgIconNames[i] != m_recipeSlots[i].textureName);
                    if (nameChanged)
                    {
                        // Invalidate cached texture so we re-lookup
                        m_umgIconTextures[i] = nullptr;
                        m_umgIconNames[i] = m_recipeSlots[i].textureName;
                    }

                    // Find and set icon texture if not yet cached
                    if (!m_umgIconTextures[i] && !m_recipeSlots[i].textureName.empty())
                    {
                        UObject* tex = findTexture2DByName(m_recipeSlots[i].textureName);
                        if (tex)
                        {
                            setUmgSlotIcon(i, tex);
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Slot #{} icon set: {}\n"),
                                                            i, m_recipeSlots[i].textureName);
                        }
                    }
                }
            }
        }

        void destroyExperimentalBar()
        {
            if (!m_umgBarWidget) return;
            auto* removeFn = m_umgBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_umgBarWidget->ProcessEvent(removeFn, nullptr);
            m_umgBarWidget = nullptr;
            m_umgSetBrushFn = nullptr;
            for (int i = 0; i < 8; i++)
            {
                m_umgStateImages[i] = nullptr;
                m_umgIconImages[i] = nullptr;
                m_umgIconTextures[i] = nullptr;
                m_umgIconNames[i].clear();
                m_umgSlotStates[i] = UmgSlotState::Empty;
                m_umgKeyLabels[i] = nullptr;
                m_umgKeyBgImages[i] = nullptr;
            }
            m_umgTexBlankRect = nullptr;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Bar removed from viewport\n"));
        }

        void createExperimentalBar()
        {
            if (m_umgBarWidget)
            {
                destroyExperimentalBar();
                showOnScreen(L"UMG bar removed", 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] === Creating 8-slot toolbar ===\n"));

            // --- Phase A: Find all 4 textures ---
            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Found {} Texture2D objects\n"), textures.size());
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Empty")) texEmpty = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Disabled")) texInactive = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Textures: frame={} empty={} inactive={} active={} blankRect={}\n"),
                                            texFrame ? STR("OK") : STR("NO"), texEmpty ? STR("OK") : STR("NO"),
                                            texInactive ? STR("OK") : STR("NO"), texActive ? STR("OK") : STR("NO"),
                                            texBlankRect ? STR("OK") : STR("NO"));
            if (!texFrame || !texEmpty)
            {
                showOnScreen(L"UMG: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }
            m_umgTexEmpty = texEmpty;
            m_umgTexInactive = texInactive;
            m_umgTexActive = texActive;
            m_umgTexBlankRect = texBlankRect;

            // --- Phase B: Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showOnScreen(L"UMG: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Phase C1: Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"UMG: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"UMG: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) { showOnScreen(L"UMG: WBL CDO null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showOnScreen(L"UMG: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Phase C2: Get WidgetTree ---
            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Phase C3: Build widget tree ---
            // Two nested Borders: outer = solid white line (2px padding), inner = fully transparent
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"UMG: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            m_umgSetBrushFn = setBrushFn; // cache for runtime state updates

            // Outer border: visible white outline
            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) { showOnScreen(L"UMG: outer border failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // Set as WidgetTree root
            if (widgetTree)
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = outerBorder;

            // Outer border: fully transparent (invisible frame)
            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // fully transparent
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            // Outer border padding = 0 (no border line)
            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 0.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 0.0f;
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // Inner border: fully transparent (hides outer's white fill behind content)
            FStaticConstructObjectParameters innerBorderP(borderClass, outer);
            UObject* innerBorder = UObjectGlobals::StaticConstructObject(innerBorderP);
            if (!innerBorder) { showOnScreen(L"UMG: inner border failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // Inner border: transparent black
            auto* setBrushColorFn2 = innerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn2)
            {
                auto* pColor = findParam(setBrushColorFn2, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn2->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // fully transparent
                    innerBorder->ProcessEvent(setBrushColorFn2, cb.data());
                }
            }

            // Set inner border as outer border's child
            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = innerBorder;
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Create HBox inside inner border
            FStaticConstructObjectParameters hboxP(hboxClass, outer);
            UObject* hbox = UObjectGlobals::StaticConstructObject(hboxP);
            if (!hbox) { showOnScreen(L"UMG: HBox failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            auto* setContentFn2 = innerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn2)
            {
                auto* pContent = findParam(setContentFn2, STR("Content"));
                int sz = setContentFn2->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = hbox;
                innerBorder->ProcessEvent(setContentFn2, sc.data());
            }

            // --- Phase C4: Create 8 columns ---
            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Creating 8 slot columns...\n"));
            for (int i = 0; i < 8; i++)
            {
                // Create VBox column
                FStaticConstructObjectParameters vboxP(vboxClass, outer);
                UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                if (!vbox) continue;

                // Create state image (bottom layer), icon image (top layer), and frame image
                FStaticConstructObjectParameters siP(imageClass, outer);
                UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                if (!stateImg) continue;
                FStaticConstructObjectParameters iiP(imageClass, outer);
                UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                if (!iconImg) continue;
                FStaticConstructObjectParameters fiP(imageClass, outer);
                UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                if (!frameImg) continue;

                // Create UOverlay to stack state + icon images
                FStaticConstructObjectParameters olP(overlayClass, outer);
                UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                if (!overlay) continue;

                // Set textures (bMatchSize=true to preserve aspect ratio)
                umgSetBrush(stateImg, texEmpty, setBrushFn);
                umgSetBrush(frameImg, texFrame, setBrushFn);
                // Icon image starts with no brush (transparent/invisible until recipe assigned)
                umgSetOpacity(iconImg, 0.0f); // hidden until recipe set

                // Read native sizes from first slot (FSlateBrush.ImageSize at UImage+0x108+0x08)
                if (i == 0)
                {
                    uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
                    frameW = *reinterpret_cast<float*>(fBase + 0x108 + 0x08);
                    frameH = *reinterpret_cast<float*>(fBase + 0x108 + 0x0C);
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    stateW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
                    stateH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Frame icon: {}x{}, State icon: {}x{}\n"),
                                                    frameW, frameH, stateW, stateH);
                }

                // State icon: fully opaque
                umgSetOpacity(stateImg, 1.0f);
                // Frame icon: 75% transparent
                umgSetOpacity(frameImg, 0.25f);

                // Add state + icon images to Overlay (state first = bottom layer, icon on top)
                auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    auto* pC = findParam(addToOverlayFn, STR("Content"));
                    auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                    // State image (bottom layer of overlay) — centered to preserve aspect ratio
                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                        overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateOlSlot)
                        {
                            auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2; // HAlign_Center
                                stateOlSlot->ProcessEvent(setHAFn, hb.data());
                            }
                            auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2; // VAlign_Center
                                stateOlSlot->ProcessEvent(setVAFn, vb.data());
                            }
                        }
                    }
                    // Icon image (top layer of overlay — transparent PNG on top of state)
                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                        overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (iconSlot)
                        {
                            // Center the icon within the overlay
                            auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2; // HAlign_Center
                                iconSlot->ProcessEvent(setHAFn, hb.data());
                            }
                            auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2; // VAlign_Center
                                iconSlot->ProcessEvent(setVAFn, vb.data());
                            }
                        }
                    }
                }

                // Add to VBox: Overlay (top), Frame image (bottom)
                auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (addToVBoxFn)
                {
                    auto* pC = findParam(addToVBoxFn, STR("Content"));
                    auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));

                    // Overlay (top) — Auto size, centered, contains state + icon stacked
                    {
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                        vbox->ProcessEvent(addToVBoxFn, ap.data());
                        UObject* stateSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateSlot)
                        {
                            umgSetSlotSize(stateSlot, 1.0f, 0); // Auto — natural size, no stretch
                            umgSetHAlign(stateSlot, 2);          // HAlign_Center
                        }
                    }

                    // Frame overlay (bottom) — wraps frameImg + keyBgImg + keyLabel
                    {
                        // Create frame overlay to stack frame + keycap bg + key text
                        FStaticConstructObjectParameters foP(overlayClass, outer);
                        UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                        if (frameOverlay)
                        {
                            auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                            if (addToFoFn)
                            {
                                auto* foC = findParam(addToFoFn, STR("Content"));
                                auto* foR = findParam(addToFoFn, STR("ReturnValue"));

                                // Layer 1: frameImg (bottom — fills overlay)
                                {
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                }

                                // Layer 2: keyBgImg (keycap background, centered)
                                if (texBlankRect)
                                {
                                    FStaticConstructObjectParameters kbP(imageClass, outer);
                                    UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                    if (keyBgImg && setBrushFn)
                                    {
                                        umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                        umgSetOpacity(keyBgImg, 0.8f);

                                        int sz2 = addToFoFn->GetParmsSize();
                                        std::vector<uint8_t> ap2(sz2, 0);
                                        if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyBgImg;
                                        frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                        UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (kbSlot)
                                        {
                                            auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                        m_umgKeyBgImages[i] = keyBgImg;
                                    }
                                }

                                // Layer 3: keyLabel (UTextBlock, centered)
                                if (textBlockClass)
                                {
                                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                    UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                    if (keyLabel)
                                    {
                                        std::wstring kn = keyName(s_bindings[i].key);
                                        umgSetText(keyLabel, kn);
                                        umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);

                                        int sz2 = addToFoFn->GetParmsSize();
                                        std::vector<uint8_t> ap2(sz2, 0);
                                        if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyLabel;
                                        frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                        UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (tlSlot)
                                        {
                                            auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                        m_umgKeyLabels[i] = keyLabel;
                                    }
                                }
                            }
                        }

                        // Add frameOverlay (or fall back to frameImg) to VBox
                        UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                        vbox->ProcessEvent(addToVBoxFn, ap.data());
                        UObject* frameSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (frameSlot)
                        {
                            umgSetSlotSize(frameSlot, 1.0f, 0); // Auto
                            umgSetHAlign(frameSlot, 2);          // HAlign_Center
                            // Negative top padding pulls frame up into state icon (15% overlap)
                            float overlapPx = stateH * 0.15f;
                            umgSetSlotPadding(frameSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                        }
                    }
                }

                // Add VBox to HBox — Fill for even column distribution
                auto* addToHBoxFn = hbox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                if (addToHBoxFn)
                {
                    auto* pC = findParam(addToHBoxFn, STR("Content"));
                    auto* pR = findParam(addToHBoxFn, STR("ReturnValue"));
                    int sz = addToHBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = vbox;
                    hbox->ProcessEvent(addToHBoxFn, ap.data());
                    UObject* hSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (hSlot)
                    {
                        umgSetSlotSize(hSlot, 1.0f, 1); // Fill
                        umgSetVAlign(hSlot, 0);          // VAlign_Fill
                        // Negative horizontal padding to overlap adjacent icons
                        float colW = (frameW > stateW) ? frameW : stateW;
                        float hOverlap = colW * 0.1f; // each side = 10%, so 20% overlap between neighbors
                        umgSetSlotPadding(hSlot, -hOverlap, 0.0f, -hOverlap, 0.0f);
                    }
                }

                m_umgStateImages[i] = stateImg;
                m_umgIconImages[i] = iconImg;
                m_umgSlotStates[i] = UmgSlotState::Empty;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Slot #{} created\n"), i);
            }

            // --- Phase D: Size frame from icon dimensions and center on screen ---
            // SetRenderScale: horizontal 0.825, vertical 0.75
            float umgScaleX = 0.825f;
            float umgScaleY = 0.75f;
            umgSetRenderScale(outerBorder, umgScaleX, umgScaleY);

            // Use the larger of frame/state width for column width
            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f; // fallback
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;                   // 15% vertical overlap
            float hOverlapPerSlot = iconW * 0.20f;             // 20% horizontal overlap (10% each side)
            // Viewport size matches render scale so invisible frame fits the visual
            float totalW = (8.0f * iconW - 7.0f * hOverlapPerSlot) * umgScaleX;
            float totalH = (frameH + stateH - vOverlap) * umgScaleY;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Frame size: {}x{} (iconW={} frameH={} stateH={})\n"),
                                            totalW, totalH, iconW, frameH, stateH);

            // Set explicit pixel size
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = totalW; v[1] = totalH;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport FIRST (creates the slot for anchor/position/alignment to work on)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size for absolute positioning
            int32_t viewW = 1920, viewH = 1080; // fallback
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] Viewport: {}x{}\n"), viewW, viewH);

            // Alignment: center-center pivot (widget center placed at position)
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f; // center-center pivot
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: center of widget at (screenW/2, 25) — absolute coordinates
            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) / 2.0f;  // X: screen center
                    v2[1] = 100.0f;                              // Y: 100px from top
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            m_umgBarWidget = userWidget;
            showOnScreen(L"Builders bar created!", 3.0f, 0.0f, 1.0f, 0.0f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [UMG] === Builders bar creation complete ===\n"));

            // Sync quick-build slot states to the builders bar
            updateBuildersBar();
        }

        // ── Advanced Builder Toolbar (single toggle button, lower-right) ────────

        void destroyAdvancedBuilderBar()
        {
            if (!m_abBarWidget) return;
            auto* removeFn = m_abBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_abBarWidget->ProcessEvent(removeFn, nullptr);
            m_abBarWidget = nullptr;
            m_abKeyLabel = nullptr;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] Advanced Builder toolbar removed\n"));
        }

        void createAdvancedBuilderBar()
        {
            if (m_abBarWidget) return; // already exists — persists until world unload

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] === Creating Advanced Builder Toolbar ===\n"));

            // --- Phase A: Find textures ---
            UObject* texFrame = nullptr;     // T_UI_Frame_HUD_AB_Active_BothHands
            UObject* texActive = nullptr;    // T_UI_Btn_HUD_EpicAB_Focused
            UObject* texBlankRect = nullptr; // T_UI_Icon_Input_Blank_Rect
            UObject* texToolsIcon = nullptr; // Tools_Icon
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                    else if (name == STR("Tools_Icon")) texToolsIcon = t;
                }
            }
            // StaticFindObject fallback for Tools_Icon
            if (!texToolsIcon)
            {
                texToolsIcon = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Game/UI/textures/ClothingIcons/Tools_Icon.Tools_Icon"));
                if (texToolsIcon)
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] Tools_Icon found via StaticFindObject\n"));
                else
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] WARNING: Tools_Icon NOT found\n"));
            }
            if (!texFrame || !texActive)
            {
                showOnScreen(L"AB: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Phase B: Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !vboxClass || !borderClass || !overlayClass)
            {
                showOnScreen(L"AB: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Phase C: Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"AB: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"AB: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showOnScreen(L"AB: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Get WidgetTree ---
            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"AB: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;

            // --- Outer border (transparent) ---
            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) return;

            if (widgetTree)
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = outerBorder;

            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // --- Build single-slot VBox ---
            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;

            // Set VBox as border content
            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Create images: stateImg, iconImg, frameImg
            FStaticConstructObjectParameters siP(imageClass, outer);
            UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
            FStaticConstructObjectParameters iiP(imageClass, outer);
            UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
            FStaticConstructObjectParameters fiP(imageClass, outer);
            UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
            if (!stateImg || !iconImg || !frameImg) return;

            // Set textures
            umgSetBrush(stateImg, texActive, setBrushFn);  // active state (always active)
            umgSetBrush(frameImg, texFrame, setBrushFn);
            umgSetOpacity(stateImg, 1.0f);
            umgSetOpacity(frameImg, 0.25f);

            // Set Tools_Icon on iconImg at 75%
            if (texToolsIcon)
            {
                umgSetBrush(iconImg, texToolsIcon, setBrushFn);
                umgSetOpacity(iconImg, 1.0f);
                uint8_t* iBase = reinterpret_cast<uint8_t*>(iconImg);
                float texW = *reinterpret_cast<float*>(iBase + 0x108 + 0x08);
                float texH = *reinterpret_cast<float*>(iBase + 0x108 + 0x0C);
                if (texW > 0.0f && texH > 0.0f)
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    float containerW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
                    float containerH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);
                    if (containerW < 1.0f) containerW = 64.0f;
                    if (containerH < 1.0f) containerH = 64.0f;
                    float scaleX = containerW / texW;
                    float scaleY = containerH / texH;
                    float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.75f;
                    umgSetBrushSize(iconImg, texW * scale, texH * scale);
                }
            }
            else
            {
                umgSetOpacity(iconImg, 0.0f);
            }

            // Read native sizes
            uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
            float frameW = *reinterpret_cast<float*>(fBase + 0x108 + 0x08);
            float frameH = *reinterpret_cast<float*>(fBase + 0x108 + 0x0C);
            uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
            float stateW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
            float stateH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);

            // Create Overlay for state + icon
            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
            if (!overlay) return;

            auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
            if (addToOverlayFn)
            {
                auto* pC = findParam(addToOverlayFn, STR("Content"));
                auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                // State image (bottom layer) — centered
                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                    overlay->ProcessEvent(addToOverlayFn, ap.data());
                    UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (stateOlSlot)
                    {
                        auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                        if (setHAFn)
                        {
                            int sz2 = setHAFn->GetParmsSize();
                            std::vector<uint8_t> hb(sz2, 0);
                            auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                            if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                            stateOlSlot->ProcessEvent(setHAFn, hb.data());
                        }
                        auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            stateOlSlot->ProcessEvent(setVAFn, vb.data());
                        }
                    }
                }
                // Icon image (top layer) — centered
                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                    overlay->ProcessEvent(addToOverlayFn, ap.data());
                    UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (iconSlot)
                    {
                        auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                        if (setHAFn)
                        {
                            int sz2 = setHAFn->GetParmsSize();
                            std::vector<uint8_t> hb(sz2, 0);
                            auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                            if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                            iconSlot->ProcessEvent(setHAFn, hb.data());
                        }
                        auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            iconSlot->ProcessEvent(setVAFn, vb.data());
                        }
                    }
                }
            }

            // Add Overlay + frame to VBox
            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (addToVBoxFn)
            {
                auto* pC = findParam(addToVBoxFn, STR("Content"));
                auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));

                // Overlay (top)
                {
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                    vbox->ProcessEvent(addToVBoxFn, ap.data());
                    UObject* olSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (olSlot)
                    {
                        umgSetSlotSize(olSlot, 1.0f, 0); // Auto
                        umgSetHAlign(olSlot, 2);          // HAlign_Center
                    }
                }

                // Frame overlay (bottom) — frameImg + keyBgImg + keyLabel
                {
                    FStaticConstructObjectParameters foP(overlayClass, outer);
                    UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                    if (frameOverlay)
                    {
                        auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                        if (addToFoFn)
                        {
                            auto* foC = findParam(addToFoFn, STR("Content"));
                            auto* foR = findParam(addToFoFn, STR("ReturnValue"));

                            // Layer 1: frameImg
                            {
                                int sz2 = addToFoFn->GetParmsSize();
                                std::vector<uint8_t> ap2(sz2, 0);
                                if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                            }

                            // Layer 2: keyBgImg (keycap background, centered)
                            if (texBlankRect)
                            {
                                FStaticConstructObjectParameters kbP(imageClass, outer);
                                UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                if (keyBgImg && setBrushFn)
                                {
                                    umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                    umgSetOpacity(keyBgImg, 0.8f);
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyBgImg;
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                    UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (kbSlot)
                                    {
                                        auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                }
                            }

                            // Layer 3: keyLabel (UTextBlock, centered)
                            if (textBlockClass)
                            {
                                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                if (keyLabel)
                                {
                                    std::wstring kn = keyName(s_bindings[BIND_AB_OPEN].key);
                                    umgSetText(keyLabel, kn);
                                    umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyLabel;
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                    UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (tlSlot)
                                    {
                                        auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                    m_abKeyLabel = keyLabel;
                                }
                            }
                        }
                    }

                    // Add frameOverlay to VBox
                    UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                    vbox->ProcessEvent(addToVBoxFn, ap.data());
                    UObject* fSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (fSlot)
                    {
                        umgSetSlotSize(fSlot, 1.0f, 0); // Auto
                        umgSetHAlign(fSlot, 2);
                        float overlapPx = stateH * 0.15f;
                        umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                    }
                }
            }

            // --- Phase D: Size and position (lower-right, 25px from edges) ---
            umgSetRenderScale(outerBorder, 0.81f, 0.81f);

            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;
            float abScale = 0.81f;
            float abTotalW = iconW * abScale;
            float abTotalH = (frameH + stateH - vOverlap) * abScale;

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = abTotalW; v[1] = abTotalH;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }

            // Alignment: bottom-right pivot
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 1.0f; v[1] = 1.0f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: 25px from lower-right corner
            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) - 10.0f;   // 10px from right edge
                    v2[1] = static_cast<float>(viewH) - 45.0f;   // 20px up from previous
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            m_abBarWidget = userWidget;
            showOnScreen(L"Advanced Builder toolbar created!", 3.0f, 0.0f, 1.0f, 0.0f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] === Advanced Builder toolbar created ({}x{}) ===\n"),
                                            abTotalW, abTotalH);
        }

        // ── UMG Target Info Popup ────────────────────────────────────────────

        void destroyTargetInfoWidget()
        {
            if (!m_targetInfoWidget) return;
            auto* removeFn = m_targetInfoWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_targetInfoWidget->ProcessEvent(removeFn, nullptr);
            m_targetInfoWidget = nullptr;
            m_tiTitleLabel = nullptr;
            m_tiClassLabel = nullptr;
            m_tiNameLabel = nullptr;
            m_tiDisplayLabel = nullptr;
            m_tiPathLabel = nullptr;
            m_tiBuildLabel = nullptr;
            m_tiRecipeLabel = nullptr;
            m_tiShowTick = 0;
        }

        void createTargetInfoWidget()
        {
            if (m_targetInfoWidget) return;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TI] === Creating Target Info UMG widget ===\n"));

            // Find UClasses
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            if (!userWidgetClass || !vboxClass || !borderClass || !textBlockClass) return;

            auto* pc = findPlayerController();
            if (!pc) return;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            // Create UserWidget
            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root SizeBox — enforces fixed width so TextBlocks can wrap text
            UObject* rootSizeBox = nullptr;
            if (sizeBoxClass)
            {
                FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
                rootSizeBox = UObjectGlobals::StaticConstructObject(sbP);
                if (rootSizeBox)
                {
                    if (widgetTree)
                        *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = rootSizeBox;
                    // SetWidthOverride(550) — hard width constraint for text wrapping
                    auto* setWFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                    if (setWFn) { int sz = setWFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 550.0f; rootSizeBox->ProcessEvent(setWFn, wp.data()); }
                    // Clip overflow to SizeBox bounds
                    auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
                    if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; /* ClipToBounds */ rootSizeBox->ProcessEvent(setClipFn, cp.data()); }
                }
            }

            // Border (dark blue background) — child of SizeBox
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            // If SizeBox exists, add border as its content; otherwise border is root widget
            if (rootSizeBox)
            {
                auto* setContentFn2 = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                if (setContentFn2)
                {
                    auto* pC = findParam(setContentFn2, STR("Content"));
                    int sz = setContentFn2->GetParmsSize();
                    std::vector<uint8_t> sc(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootBorder;
                    rootSizeBox->ProcessEvent(setContentFn2, sc.data());
                }
            }
            else if (widgetTree)
            {
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = rootBorder;
            }

            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // transparent (no bg)
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            // Border padding
            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 12.0f; m[1] = 8.0f; m[2] = 12.0f; m[3] = 8.0f; // L, T, R, B
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox as border content
            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Helper lambda: create TextBlock and add to VBox
            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));
            auto* vbR = findParam(addToVBoxFn, STR("ReturnValue"));

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                // Enable text wrapping at fixed pixel width (doesn't depend on parent layout)
                auto* wrapAtFn = tb->GetFunctionByNameInChain(STR("SetWrapTextAt"));
                if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 520.0f; tb->ProcessEvent(wrapAtFn, wp.data()); }
                auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
                if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; tb->ProcessEvent(wrapFn, wp.data()); }
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
                return tb;
            };

            // Title
            m_tiTitleLabel = makeTextBlock(L"Target Info", 0.78f, 0.86f, 1.0f, 1.0f);
            // Separator (thin text line)
            makeTextBlock(L"────────────────────────────────", 0.31f, 0.51f, 0.78f, 0.5f);
            // Data rows
            m_tiClassLabel   = makeTextBlock(L"Class:", 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiNameLabel    = makeTextBlock(L"Name:", 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiDisplayLabel = makeTextBlock(L"Display:", 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiPathLabel    = makeTextBlock(L"Path:", 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiBuildLabel   = makeTextBlock(L"Build:", 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiRecipeLabel  = makeTextBlock(L"Recipe:", 0.86f, 0.90f, 0.96f, 0.9f);

            // Add to viewport (hidden)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 101;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Set desired size
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 550.0f; v[1] = 320.0f;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Get viewport size
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }

            // Alignment: right-center
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 1.0f; v[1] = 0.5f; // right edge, vertical center
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: right side, 50px above vertical center
            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) - 675.0f;   // 100px further left
                    v2[1] = static_cast<float>(viewH) / 2.0f - 250.0f; // 100px further up
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_targetInfoWidget = userWidget;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TI] Target Info UMG widget created\n"));
        }

        void hideTargetInfo()
        {
            if (!m_targetInfoWidget) return;
            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; m_targetInfoWidget->ProcessEvent(fn, p); }
            m_tiShowTick = 0;
        }

        void showTargetInfoUMG(const std::wstring& name,
                               const std::wstring& display,
                               const std::wstring& path,
                               const std::wstring& cls,
                               bool buildable,
                               const std::wstring& recipe,
                               const std::wstring& rowName)
        {
            if (!m_targetInfoWidget) createTargetInfoWidget();
            if (!m_targetInfoWidget) return;

            // Update text labels
            umgSetText(m_tiClassLabel, L"Class:    " + cls);
            umgSetText(m_tiNameLabel, L"Name:     " + name);
            umgSetText(m_tiDisplayLabel, L"Display:  " + display);
            umgSetText(m_tiPathLabel, L"Path:     " + path);
            std::wstring buildStr = buildable ? L"Yes" : L"No";
            umgSetText(m_tiBuildLabel, L"Build:    " + buildStr);
            if (buildable)
                umgSetTextColor(m_tiBuildLabel, 0.31f, 0.86f, 0.31f, 1.0f);
            else
                umgSetTextColor(m_tiBuildLabel, 0.7f, 0.55f, 0.39f, 0.8f);
            std::wstring recipeDisplay = !rowName.empty() ? rowName : recipe;
            umgSetText(m_tiRecipeLabel, recipeDisplay.empty() ? L"" : (L"Recipe:   " + recipeDisplay));

            // Show widget
            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_targetInfoWidget->ProcessEvent(fn, p); }

            // Auto-copy to clipboard
            std::wstring copyText = L"Class: " + cls + L"\r\n" + L"Name: " + name + L"\r\n" +
                                    L"Display: " + display + L"\r\n" + L"Path: " + path + L"\r\n" +
                                    L"Buildable: " + buildStr;
            if (!recipeDisplay.empty()) copyText += L"\r\nRecipe: " + recipeDisplay;
            HWND hwnd = findGameWindow();
            if (OpenClipboard(hwnd))
            {
                EmptyClipboard();
                size_t sz = (copyText.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem)
                {
                    memcpy(GlobalLock(hMem), copyText.c_str(), sz);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Target info copied to clipboard\n"));
            }

            m_tiShowTick = GetTickCount64();
        }

        // ── UMG Info Box Popup (removal messages) ───────────────────────────────

        void destroyInfoBox()
        {
            if (!m_infoBoxWidget) return;
            auto* removeFn = m_infoBoxWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_infoBoxWidget->ProcessEvent(removeFn, nullptr);
            m_infoBoxWidget = nullptr;
            m_ibTitleLabel = nullptr;
            m_ibMessageLabel = nullptr;
            m_ibShowTick = 0;
        }

        void createInfoBox()
        {
            if (m_infoBoxWidget) return;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [IB] === Creating Info Box UMG widget ===\n"));

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !vboxClass || !borderClass || !textBlockClass) return;

            auto* pc = findPlayerController();
            if (!pc) return;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root border (dark blue bg — same as Target Info)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = rootBorder;

            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.098f; c[1] = 0.118f; c[2] = 0.176f; c[3] = 0.86f;
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 12.0f; m[1] = 8.0f; m[2] = 12.0f; m[3] = 8.0f;
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox
            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
                return tb;
            };

            m_ibTitleLabel   = makeTextBlock(L"Info", 0.78f, 0.86f, 1.0f, 1.0f);
            m_ibMessageLabel = makeTextBlock(L"", 0.86f, 0.90f, 0.96f, 0.9f);

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 102;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 400.0f; v[1] = 80.0f;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }

            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 1.0f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) - 25.0f;
                    v2[1] = static_cast<float>(viewH) / 2.0f + 100.0f; // below Target Info
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_infoBoxWidget = userWidget;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [IB] Info Box UMG widget created\n"));
        }

        void hideInfoBox()
        {
            if (!m_infoBoxWidget) return;
            auto* fn = m_infoBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; m_infoBoxWidget->ProcessEvent(fn, p); }
            m_ibShowTick = 0;
        }

        void showInfoBox(const std::wstring& title, const std::wstring& message,
                         float r = 0.0f, float g = 1.0f, float b = 0.5f)
        {
            if (!m_infoBoxWidget) createInfoBox();
            if (!m_infoBoxWidget) return;

            umgSetText(m_ibTitleLabel, title);
            umgSetTextColor(m_ibTitleLabel, r, g, b, 1.0f);
            umgSetText(m_ibMessageLabel, message);

            auto* fn = m_infoBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_infoBoxWidget->ProcessEvent(fn, p); }

            m_ibShowTick = GetTickCount64();
        }

        // ── UMG Config Menu (first pass) ────────────────────────────────────

        void destroyConfigWidget()
        {
            if (!m_configWidget) return;
            auto* removeFn = m_configWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_configWidget->ProcessEvent(removeFn, nullptr);
            m_configWidget = nullptr;
            for (auto& t : m_cfgTabLabels) t = nullptr;
            for (auto& t : m_cfgTabContent) t = nullptr;
            for (auto& t : m_cfgTabImages) t = nullptr;
            m_cfgTabActiveTexture = nullptr;
            m_cfgTabInactiveTexture = nullptr;
            m_cfgVignetteImage = nullptr;
            for (auto& s : m_cfgScrollBoxes) s = nullptr;
            m_cfgFreeBuildLabel = nullptr;
            m_cfgFreeBuildCheckImg = nullptr;
            m_cfgUnlockBtnImg = nullptr;
            for (auto& k : m_cfgKeyValueLabels) k = nullptr;
            for (auto& k : m_cfgKeyBoxLabels) k = nullptr;
            m_cfgModifierLabel = nullptr;
            m_cfgModBoxLabel = nullptr;
            m_cfgRemovalHeader = nullptr;
            m_cfgRemovalVBox = nullptr;
            m_cfgLastRemovalCount = -1;
            m_cfgVisible = false;
            m_cfgActiveTab = 0;
        }

        void switchConfigTab(int tab)
        {
            if (tab < 0 || tab >= 3 || tab == m_cfgActiveTab) return;
            m_cfgActiveTab = tab;
            auto* sBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            for (int i = 0; i < 3; i++)
            {
                // Show/hide ScrollBoxes (which wrap the tab VBoxes)
                UObject* target = m_cfgScrollBoxes[i] ? m_cfgScrollBoxes[i] : m_cfgTabContent[i];
                if (target)
                {
                    auto* fn = target->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (fn) { uint8_t p[8]{}; p[0] = (i == tab) ? 0 : 1; target->ProcessEvent(fn, p); }
                }
                // Update tab label colors
                if (m_cfgTabLabels[i])
                {
                    if (i == tab)
                        umgSetTextColor(m_cfgTabLabels[i], 0.78f, 0.86f, 1.0f, 1.0f); // bright
                    else
                        umgSetTextColor(m_cfgTabLabels[i], 0.47f, 0.55f, 0.71f, 0.7f); // dim
                }
                // Swap tab background textures
                if (m_cfgTabImages[i] && sBrushFn)
                {
                    UObject* tex = (i == tab) ? m_cfgTabActiveTexture : m_cfgTabInactiveTexture;
                    if (tex) umgSetBrushNoMatch(m_cfgTabImages[i], tex, sBrushFn);
                }
            }
            // Cancel any active key capture when switching away from Tab 1
            if (tab != 1 && s_capturingBind >= 0)
            {
                s_capturingBind = -1;
                updateConfigKeyLabels();
            }
            // Refresh removal list when switching to Tab 2
            if (tab == 2)
            {
                int curCount = s_config.removalCount.load();
                if (curCount != m_cfgLastRemovalCount)
                    rebuildRemovalList();
            }
        }

        void updateConfigKeyLabels()
        {
            int capturing = s_capturingBind.load();
            for (int i = 0; i < BIND_COUNT; i++)
            {
                // Update old-style labels (kept for compat)
                if (m_cfgKeyValueLabels[i])
                {
                    std::wstring row = std::wstring(s_bindings[i].label) + L":  " + keyName(s_bindings[i].key);
                    umgSetText(m_cfgKeyValueLabels[i], row);
                }
                // Update new key box labels
                if (m_cfgKeyBoxLabels[i])
                {
                    if (capturing == i)
                    {
                        umgSetText(m_cfgKeyBoxLabels[i], L"Press key...");
                        umgSetTextColor(m_cfgKeyBoxLabels[i], 1.0f, 0.9f, 0.0f, 1.0f); // yellow
                    }
                    else
                    {
                        umgSetText(m_cfgKeyBoxLabels[i], keyName(s_bindings[i].key));
                        umgSetTextColor(m_cfgKeyBoxLabels[i], 1.0f, 1.0f, 1.0f, 1.0f); // white
                    }
                }
            }
            if (m_cfgModifierLabel)
            {
                std::wstring modText = L"Set Modifier Key:  " + std::wstring(modifierName(s_modifierVK));
                umgSetText(m_cfgModifierLabel, modText);
            }
            // Update modifier key box label
            if (m_cfgModBoxLabel)
            {
                umgSetText(m_cfgModBoxLabel, std::wstring(modifierName(s_modifierVK)));
            }
        }

        void updateConfigFreeBuild()
        {
            bool on = s_config.freeBuild;
            if (m_cfgFreeBuildLabel)
            {
                umgSetText(m_cfgFreeBuildLabel, on ? L"  Free Build  (ON)" : L"  Free Build");
                umgSetTextColor(m_cfgFreeBuildLabel, on ? 0.31f : 0.55f, on ? 0.86f : 0.55f, on ? 0.47f : 0.55f, 1.0f);
            }
            // Show/hide check mark image
            if (m_cfgFreeBuildCheckImg)
            {
                auto* visFn = m_cfgFreeBuildCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; m_cfgFreeBuildCheckImg->ProcessEvent(visFn, p); }
            }
        }

        void updateConfigRemovalCount()
        {
            if (m_cfgRemovalHeader)
            {
                int count = s_config.removalCount.load();
                umgSetText(m_cfgRemovalHeader, L"Saved Removals (" + std::to_wstring(count) + L" entries)");
            }
        }

        void rebuildRemovalList()
        {
            if (!m_cfgRemovalVBox) return;

            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!imageClass || !hboxClass || !vboxClass || !textBlockClass) return;

            // Get the outer from the VBox
            UObject* outer = m_cfgRemovalVBox->GetOuterPrivate();
            if (!outer) outer = m_cfgRemovalVBox;

            // Helper: add widget to VBox
            auto addToVBox = [&](UObject* vbox, UObject* child) {
                auto* fn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (!fn) return;
                auto* pC2 = findParam(fn, STR("Content"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                vbox->ProcessEvent(fn, ap.data());
            };

            // Clear existing entry rows (all children after the header)
            // Use ClearChildren then re-add header
            auto* clearFn = m_cfgRemovalVBox->GetFunctionByNameInChain(STR("ClearChildren"));
            if (clearFn) m_cfgRemovalVBox->ProcessEvent(clearFn, nullptr);

            // Re-add header
            int count = s_config.removalCount.load();
            if (m_cfgRemovalHeader)
            {
                umgSetText(m_cfgRemovalHeader, L"Saved Removals (" + std::to_wstring(count) + L" entries)");
                addToVBox(m_cfgRemovalVBox, m_cfgRemovalHeader);
            }

            UObject* texDanger = findTexture2DByName(L"T_UI_Icon_Danger");

            if (s_config.removalCSInit)
            {
                EnterCriticalSection(&s_config.removalCS);
                for (size_t i = 0; i < s_config.removalEntries.size(); i++)
                {
                    const auto& entry = s_config.removalEntries[i];

                    // Each row: HBox { Image(danger, 40x40) + VBox { TextBlock(name, bold 24pt), TextBlock(coords, 18pt gray) } }
                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                    UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowP);
                    if (!rowHBox) continue;

                    auto* addToHFn = rowHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                    if (!addToHFn) continue;
                    auto* hbC = findParam(addToHFn, STR("Content"));

                    // Danger icon
                    if (texDanger && setBrushFn)
                    {
                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* dangerImg = UObjectGlobals::StaticConstructObject(imgP);
                        if (dangerImg)
                        {
                            umgSetBrushNoMatch(dangerImg, texDanger, setBrushFn);
                            umgSetBrushSize(dangerImg, 56.0f, 56.0f);
                            int sz = addToHFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = dangerImg;
                            rowHBox->ProcessEvent(addToHFn, ap.data());
                        }
                    }

                    // Info VBox: name + coords
                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                    if (infoVBox)
                    {
                        // Name (bold)
                        FStaticConstructObjectParameters tbP1(textBlockClass, outer);
                        UObject* nameTB = UObjectGlobals::StaticConstructObject(tbP1);
                        if (nameTB)
                        {
                            umgSetText(nameTB, entry.friendlyName);
                            umgSetTextColor(nameTB, 0.3f, 0.85f, 0.3f, 1.0f); // medium green
                            umgSetFontSize(nameTB, 24);
                            umgSetBold(nameTB);
                            addToVBox(infoVBox, nameTB);
                        }
                        // Coords or "TYPE RULE"
                        FStaticConstructObjectParameters tbP2(textBlockClass, outer);
                        UObject* coordsTB = UObjectGlobals::StaticConstructObject(tbP2);
                        if (coordsTB)
                        {
                            std::wstring coordText = entry.isTypeRule ? L"TYPE RULE" : entry.coordsW;
                            umgSetText(coordsTB, coordText);
                            umgSetTextColor(coordsTB, 0.85f, 0.25f, 0.25f, 1.0f); // medium red
                            umgSetFontSize(coordsTB, 18);
                            addToVBox(infoVBox, coordsTB);
                        }

                        int sz = addToHFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = infoVBox;
                        rowHBox->ProcessEvent(addToHFn, ap.data());
                    }

                    addToVBox(m_cfgRemovalVBox, rowHBox);
                }
                LeaveCriticalSection(&s_config.removalCS);
            }
            m_cfgLastRemovalCount = count;
        }

        void createConfigWidget()
        {
            if (m_configWidget) return;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] === Creating Config UMG widget ===\n"));

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* scrollBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ScrollBox"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!userWidgetClass || !vboxClass || !hboxClass || !borderClass || !textBlockClass) return;
            if (!overlayClass || !imageClass || !scrollBoxClass || !sizeBoxClass || !setBrushFn) return;

            auto* pc = findPlayerController();
            if (!pc) return;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Make widget focusable for modal input mode
            uint8_t* uwRaw = reinterpret_cast<uint8_t*>(userWidget);
            uwRaw[0x01E4] |= 0x02; // bIsFocusable (bit 1 at 0x01E4)

            // Root SizeBox to enforce fixed 1400x450 size
            FStaticConstructObjectParameters rootSbP(sizeBoxClass, outer);
            UObject* rootSizeBox = UObjectGlobals::StaticConstructObject(rootSbP);
            if (!rootSizeBox) return;
            if (widgetTree)
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = rootSizeBox;
            // SetWidthOverride(1400) and SetHeightOverride(900)
            auto* setWidthOvFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
            if (setWidthOvFn) { int sz = setWidthOvFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWidthOvFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 1400.0f; rootSizeBox->ProcessEvent(setWidthOvFn, wp.data()); }
            auto* setHeightOvFn = rootSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
            if (setHeightOvFn) { int sz = setHeightOvFn->GetParmsSize(); std::vector<uint8_t> hp(sz, 0); auto* p = findParam(setHeightOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 900.0f; rootSizeBox->ProcessEvent(setHeightOvFn, hp.data()); }
            // SetClipping(ClipToBounds) — clip overflow content to SizeBox bounds
            auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
            if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; /* ClipToBounds */ rootSizeBox->ProcessEvent(setClipFn, cp.data()); }

            // Root Overlay (stacks vignette image behind content) — child of SizeBox
            FStaticConstructObjectParameters rootOlP(overlayClass, outer);
            UObject* rootOverlay = UObjectGlobals::StaticConstructObject(rootOlP);
            if (!rootOverlay) return;
            // Add overlay as SizeBox content
            auto* setSbContentFn = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
            if (setSbContentFn)
            {
                auto* pC = findParam(setSbContentFn, STR("Content"));
                int sz = setSbContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootOverlay;
                rootSizeBox->ProcessEvent(setSbContentFn, sc.data());
            }

            auto* addToOverlayFn = rootOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
            if (!addToOverlayFn) return;
            auto* olC = findParam(addToOverlayFn, STR("Content"));

            // Layer 0: Vignette border image (tinted dark blue)
            UObject* texVignette = findTexture2DByName(L"T_UI_Waypoint_Vignette_White_Optimized");
            if (texVignette)
            {
                FStaticConstructObjectParameters vigP(imageClass, outer);
                UObject* vigImg = UObjectGlobals::StaticConstructObject(vigP);
                if (vigImg)
                {
                    umgSetBrush(vigImg, texVignette, setBrushFn);
                    // Tint to dark blue
                    auto* setColorFn = vigImg->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
                    if (setColorFn)
                    {
                        auto* pColor = findParam(setColorFn, STR("InColorAndOpacity"));
                        if (pColor)
                        {
                            int sz = setColorFn->GetParmsSize();
                            std::vector<uint8_t> cb(sz, 0);
                            auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                            c[0] = 0.059f; c[1] = 0.071f; c[2] = 0.110f; c[3] = 0.92f;
                            vigImg->ProcessEvent(setColorFn, cb.data());
                        }
                    }
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (olC) *reinterpret_cast<UObject**>(ap.data() + olC->GetOffset_Internal()) = vigImg;
                    rootOverlay->ProcessEvent(addToOverlayFn, ap.data());
                    m_cfgVignetteImage = vigImg;
                }
            }
            else
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Vignette texture not found, skipping border\n"));

            // Layer 1: Transparent Border with padding (content container)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    // Semi-transparent dark blue background (50% opacity)
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.059f; c[1] = 0.071f; c[2] = 0.110f; c[3] = 0.50f;
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 40.0f; m[1] = 28.0f; m[2] = 40.0f; m[3] = 28.0f;
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }
            // Add border to overlay layer 1
            {
                int sz = addToOverlayFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (olC) *reinterpret_cast<UObject**>(ap.data() + olC->GetOffset_Internal()) = rootBorder;
                rootOverlay->ProcessEvent(addToOverlayFn, ap.data());
            }

            // Main VBox inside the border
            FStaticConstructObjectParameters mainVP(vboxClass, outer);
            UObject* mainVBox = UObjectGlobals::StaticConstructObject(mainVP);
            if (!mainVBox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = mainVBox;
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Helper: add widget to a VBox, returns slot for further configuration
            auto addToVBox = [&](UObject* vbox, UObject* child) -> UObject* {
                auto* fn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (!fn) return nullptr;
                auto* pC2 = findParam(fn, STR("Content"));
                auto* pRV = findParam(fn, STR("ReturnValue"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                vbox->ProcessEvent(fn, ap.data());
                return pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
            };

            // Helper: create TextBlock with optional font size (0 = default)
            auto makeTB = [&](const std::wstring& text, float r, float g, float b, float a, int32_t fontSize = 0) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                if (fontSize > 0) umgSetFontSize(tb, fontSize);
                return tb;
            };

            // Helper: add TextBlock to a VBox and return it
            auto addTB = [&](UObject* vbox, const std::wstring& text, float r, float g, float b, float a, int32_t fontSize = 0) -> UObject* {
                UObject* tb = makeTB(text, r, g, b, a, fontSize);
                if (tb) addToVBox(vbox, tb);
                return tb;
            };

            // Helper: add child to a ScrollBox (uses UPanelWidget::AddChild)
            auto addToScrollBox = [&](UObject* scrollBox, UObject* child) {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("AddChild"));
                if (!fn) return;
                auto* pC2 = findParam(fn, STR("Content"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                scrollBox->ProcessEvent(fn, ap.data());
            };

            // Title
            addTB(mainVBox, L"Building Mod Configuration Menu", 0.78f, 0.86f, 1.0f, 1.0f, 36);
            addTB(mainVBox, L"────────────────────────────────────────────", 0.31f, 0.51f, 0.78f, 0.4f, 20);

            // Tab bar: HBox with texture-backed tabs
            UObject* texP1 = findTexture2DByName(L"T_UI_Btn_P1_Up");
            UObject* texP2 = findTexture2DByName(L"T_UI_Btn_P2_Up");
            if (!texP1) texP1 = findTexture2DByName(L"T_UI_Btn_HUD_EpicAB_Focused");   // fallback
            if (!texP2) texP2 = findTexture2DByName(L"T_UI_Btn_HUD_EpicAB_Disabled");   // fallback
            m_cfgTabActiveTexture = texP1;
            m_cfgTabInactiveTexture = texP2;

            {
                FStaticConstructObjectParameters hbP(hboxClass, outer);
                UObject* tabHBox = UObjectGlobals::StaticConstructObject(hbP);
                if (tabHBox)
                {
                    addToVBox(mainVBox, tabHBox);
                    auto* addToHBoxFn = tabHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                    const wchar_t* tabNames[3] = {L"Optional Mods", L"Key Mapping", L"Hide Environment"};

                    for (int t = 0; t < 3; t++)
                    {
                        // Each tab = Overlay { UImage(texture) + TextBlock(label) }
                        FStaticConstructObjectParameters tolP(overlayClass, outer);
                        UObject* tabOl = UObjectGlobals::StaticConstructObject(tolP);
                        if (!tabOl) continue;

                        auto* addToTabOlFn = tabOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                        if (!addToTabOlFn) continue;
                        auto* tolC = findParam(addToTabOlFn, STR("Content"));
                        auto* tolR = findParam(addToTabOlFn, STR("ReturnValue"));

                        // Layer 0: tab background image (sized to cover text)
                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* tabImg = UObjectGlobals::StaticConstructObject(imgP);
                        m_cfgTabImages[t] = tabImg;
                        if (tabImg)
                        {
                            UObject* tex = (t == 0) ? texP1 : texP2; // tab 0 active by default
                            if (tex) umgSetBrushNoMatch(tabImg, tex, setBrushFn);
                            umgSetBrushSize(tabImg, 420.0f, 66.0f);
                            int sz = addToTabOlFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (tolC) *reinterpret_cast<UObject**>(ap.data() + tolC->GetOffset_Internal()) = tabImg;
                            tabOl->ProcessEvent(addToTabOlFn, ap.data());
                        }

                        // Layer 1: tab label text
                        UObject* tabLabel = makeTB(tabNames[t],
                                                   (t == 0) ? 0.78f : 0.47f,
                                                   (t == 0) ? 0.86f : 0.55f,
                                                   (t == 0) ? 1.0f  : 0.71f,
                                                   (t == 0) ? 1.0f  : 0.7f,
                                                   28);
                        m_cfgTabLabels[t] = tabLabel;
                        if (tabLabel)
                        {
                            int sz = addToTabOlFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (tolC) *reinterpret_cast<UObject**>(ap.data() + tolC->GetOffset_Internal()) = tabLabel;
                            tabOl->ProcessEvent(addToTabOlFn, ap.data());
                            UObject* labelSlot = tolR ? *reinterpret_cast<UObject**>(ap.data() + tolR->GetOffset_Internal()) : nullptr;
                            if (labelSlot)
                            {
                                auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                                auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                            }
                        }

                        // Add tab overlay to HBox
                        if (addToHBoxFn)
                        {
                            auto* hbC = findParam(addToHBoxFn, STR("Content"));
                            int sz = addToHBoxFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = tabOl;
                            tabHBox->ProcessEvent(addToHBoxFn, ap.data());
                        }
                    }
                }
            }

            addTB(mainVBox, L"─────────────────────────────────────────────────────────────", 0.31f, 0.51f, 0.78f, 0.4f, 20);

            // Helper: configure ScrollBox to always show scrollbar
            auto configureScrollBox = [&](UObject* scrollBox) {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("SetAlwaysShowScrollbar"));
                if (fn) {
                    auto* p = findParam(fn, STR("NewAlwaysShowScrollbar"));
                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);
                    if (p) *reinterpret_cast<bool*>(buf.data() + p->GetOffset_Internal()) = true;
                    scrollBox->ProcessEvent(fn, buf.data());
                }
            };

            // Tab 0: Optional Mods (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[0] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab0VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[0] = tab0VBox;

                if (scrollBox && tab0VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab0VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }

                    addTB(tab0VBox, L"Cheat Toggles", 0.78f, 0.86f, 1.0f, 1.0f, 32);

                    // Free Build checkbox row: HBox { Overlay{checkbox+check} + TextBlock }
                    {
                        FStaticConstructObjectParameters hbP(hboxClass, outer);
                        UObject* cbRow = UObjectGlobals::StaticConstructObject(hbP);
                        if (cbRow)
                        {
                            addToVBox(tab0VBox, cbRow);
                            auto* addToHFn = cbRow->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));

                            // Checkbox overlay: background + check mark
                            FStaticConstructObjectParameters olP(overlayClass, outer);
                            UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                            if (cbOl && addToHFn)
                            {
                                auto* addToOlFn = cbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                // Layer 0: checkbox background (always visible)
                                UObject* texCB = findTexture2DByName(L"T_UI_Icon_Checkbox_DiamondBG");
                                if (texCB && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* cbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (cbBgImg)
                                    {
                                        umgSetBrushNoMatch(cbBgImg, texCB, setBrushFn);
                                        umgSetBrushSize(cbBgImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = cbBgImg;
                                        cbOl->ProcessEvent(addToOlFn, ap.data());
                                    }
                                }
                                // Layer 1: check mark (shown/hidden based on state)
                                UObject* texCheck = findTexture2DByName(L"T_UI_Icon_Check");
                                if (texCheck && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* checkImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (checkImg)
                                    {
                                        umgSetBrushNoMatch(checkImg, texCheck, setBrushFn);
                                        umgSetBrushSize(checkImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = checkImg;
                                        cbOl->ProcessEvent(addToOlFn, ap.data());
                                        m_cfgFreeBuildCheckImg = checkImg;
                                        // Start hidden (will be shown by updateConfigFreeBuild)
                                        auto* visFn = checkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                        if (visFn) { uint8_t p[8]{}; p[0] = 1; checkImg->ProcessEvent(visFn, p); }
                                    }
                                }

                                // Add checkbox overlay to HBox
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = cbOl;
                                cbRow->ProcessEvent(addToHFn, ap.data());
                            }

                            // Label: "Free Build" text next to checkbox
                            UObject* fbLabel = makeTB(L"  Free Build", 0.55f, 0.55f, 0.55f, 1.0f, 26);
                            m_cfgFreeBuildLabel = fbLabel;
                            if (fbLabel && addToHFn)
                            {
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = fbLabel;
                                cbRow->ProcessEvent(addToHFn, ap.data());
                            }
                        }
                    }

                    addTB(tab0VBox, L"  Build without materials", 0.47f, 0.55f, 0.71f, 0.6f, 24);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 12);

                    // "Unlock All Recipes" button with T_UI_Btn_P2_Active texture
                    {
                        UObject* texBtn = findTexture2DByName(L"T_UI_Btn_P2_Active");
                        FStaticConstructObjectParameters olP(overlayClass, outer);
                        UObject* btnOl = UObjectGlobals::StaticConstructObject(olP);
                        if (btnOl)
                        {
                            auto* addToOlFn = btnOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                            if (addToOlFn && texBtn)
                            {
                                // Layer 0: button background image
                                FStaticConstructObjectParameters imgP2(imageClass, outer);
                                UObject* btnImg = UObjectGlobals::StaticConstructObject(imgP2);
                                if (btnImg)
                                {
                                    umgSetBrushNoMatch(btnImg, texBtn, setBrushFn);
                                    umgSetBrushSize(btnImg, 420.0f, 66.0f);
                                    auto* pCC = findParam(addToOlFn, STR("Content"));
                                    int sz = addToOlFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = btnImg;
                                    btnOl->ProcessEvent(addToOlFn, ap.data());
                                    m_cfgUnlockBtnImg = btnImg;
                                }
                            }
                            if (addToOlFn)
                            {
                                // Layer 1: "Unlock All Recipes" label
                                UObject* btnLabel = makeTB(L"Unlock All Recipes", 0.86f, 0.90f, 1.0f, 0.95f, 26);
                                if (btnLabel)
                                {
                                    auto* pCC = findParam(addToOlFn, STR("Content"));
                                    auto* pRV = findParam(addToOlFn, STR("ReturnValue"));
                                    int sz = addToOlFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = btnLabel;
                                    btnOl->ProcessEvent(addToOlFn, ap.data());
                                    // Center the label on the button
                                    UObject* labelSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                    if (labelSlot)
                                    {
                                        auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                }
                            }
                            // Add button to VBox center-justified
                            auto* addBtnFn = tab0VBox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                            if (addBtnFn)
                            {
                                auto* pC = findParam(addBtnFn, STR("Content"));
                                auto* pR = findParam(addBtnFn, STR("ReturnValue"));
                                int sz = addBtnFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = btnOl;
                                tab0VBox->ProcessEvent(addBtnFn, ap.data());
                                UObject* btnSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                                if (btnSlot)
                                {
                                    // Center-justify the button
                                    auto* setHA = btnSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                    if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; /* HAlign_Center */ btnSlot->ProcessEvent(setHA, h.data()); }
                                }
                            }
                        }
                    }

                    // Pad tab 0 content — add spacer lines so it visually fills the scroll area like other tabs
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                }
            }

            // Tab 1: Key Mapping (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[1] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab1VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[1] = tab1VBox;

                if (scrollBox && tab1VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab1VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }

                    // Section heading background texture
                    UObject* texSectionBg = findTexture2DByName(L"T_UI_Map_LocationName_HUD");
                    // Key box background texture
                    UObject* texKeyBox = findTexture2DByName(L"T_UI_Btn_P1_Active");

                    const wchar_t* lastSection = nullptr;
                    for (int b = 0; b < BIND_COUNT; b++)
                    {
                        // Section header with background texture
                        if (!lastSection || wcscmp(lastSection, s_bindings[b].section) != 0)
                        {
                            lastSection = s_bindings[b].section;
                            if (texSectionBg)
                            {
                                FStaticConstructObjectParameters solP(overlayClass, outer);
                                UObject* secOl = UObjectGlobals::StaticConstructObject(solP);
                                if (secOl)
                                {
                                    auto* addToSecOlFn = secOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                    if (addToSecOlFn)
                                    {
                                        // Layer 0: background image
                                        FStaticConstructObjectParameters imgP2(imageClass, outer);
                                        UObject* secBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                        if (secBgImg)
                                        {
                                            umgSetBrushNoMatch(secBgImg, texSectionBg, setBrushFn);
                                            umgSetBrushSize(secBgImg, 1300.0f, 40.0f);
                                            auto* pCC = findParam(addToSecOlFn, STR("Content"));
                                            int sz = addToSecOlFn->GetParmsSize();
                                            std::vector<uint8_t> ap(sz, 0);
                                            if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = secBgImg;
                                            secOl->ProcessEvent(addToSecOlFn, ap.data());
                                        }
                                        // Layer 1: section name text
                                        UObject* secLabel = makeTB(std::wstring(lastSection), 0.78f, 0.86f, 1.0f, 1.0f, 32);
                                        if (secLabel)
                                        {
                                            umgSetBold(secLabel);
                                            auto* pCC = findParam(addToSecOlFn, STR("Content"));
                                            auto* pRV = findParam(addToSecOlFn, STR("ReturnValue"));
                                            int sz = addToSecOlFn->GetParmsSize();
                                            std::vector<uint8_t> ap(sz, 0);
                                            if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = secLabel;
                                            secOl->ProcessEvent(addToSecOlFn, ap.data());
                                            // Center vertically, left-align
                                            UObject* secSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                            if (secSlot)
                                            {
                                                auto* setVA = secSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                                if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; secSlot->ProcessEvent(setVA, v.data()); }
                                            }
                                        }
                                    }
                                    addToVBox(tab1VBox, secOl);
                                }
                            }
                            else
                            {
                                addTB(tab1VBox, std::wstring(lastSection), 0.78f, 0.86f, 1.0f, 1.0f, 32);
                            }
                        }

                        // Key binding row: HBox { Label(left) + Overlay{KeyBoxImg+KeyText}(right) }
                        FStaticConstructObjectParameters rowHbP(hboxClass, outer);
                        UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowHbP);
                        if (rowHBox)
                        {
                            auto* addToRowFn = rowHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                            auto* rowC = findParam(addToRowFn, STR("Content"));
                            auto* rowR = findParam(addToRowFn, STR("ReturnValue"));

                            // Left: binding label
                            UObject* bindLabel = makeTB(std::wstring(s_bindings[b].label), 0.86f, 0.90f, 0.96f, 0.85f, 26);
                            if (bindLabel && addToRowFn)
                            {
                                int sz = addToRowFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = bindLabel;
                                rowHBox->ProcessEvent(addToRowFn, ap.data());
                                // Make label fill available space (push key box to right)
                                UObject* labelSlot = rowR ? *reinterpret_cast<UObject**>(ap.data() + rowR->GetOffset_Internal()) : nullptr;
                                if (labelSlot)
                                {
                                    auto* setFill = labelSlot->GetFunctionByNameInChain(STR("SetSize"));
                                    if (setFill) { int s2 = setFill->GetParmsSize(); std::vector<uint8_t> fp(s2, 0); auto* p = findParam(setFill, STR("InSize")); if (p) { *reinterpret_cast<float*>(fp.data() + p->GetOffset_Internal()) = 1.0f; fp[p->GetOffset_Internal() + 4] = 1; /* SizeRule=Fill */ } labelSlot->ProcessEvent(setFill, fp.data()); }
                                }
                            }

                            // Right: key box = Overlay { Image(key box bg) + TextBlock(key name) }
                            FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                            UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                            if (kbOl)
                            {
                                auto* addToKbFn = kbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                if (addToKbFn && texKeyBox)
                                {
                                    // Layer 0: key box background
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* kbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (kbBgImg)
                                    {
                                        umgSetBrushNoMatch(kbBgImg, texKeyBox, setBrushFn);
                                        umgSetBrushSize(kbBgImg, 220.0f, 42.0f);
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbBgImg;
                                        kbOl->ProcessEvent(addToKbFn, ap.data());
                                    }
                                }
                                if (addToKbFn)
                                {
                                    // Layer 1: key name text
                                    UObject* kbLabel = makeTB(keyName(s_bindings[b].key), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                    m_cfgKeyBoxLabels[b] = kbLabel;
                                    if (kbLabel)
                                    {
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        auto* pRV = findParam(addToKbFn, STR("ReturnValue"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbLabel;
                                        kbOl->ProcessEvent(addToKbFn, ap.data());
                                        // Center text on key box
                                        UObject* kbSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                        if (kbSlot)
                                        {
                                            auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                    }
                                }
                                // Add key box overlay to row HBox
                                if (addToRowFn)
                                {
                                    int sz = addToRowFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = kbOl;
                                    rowHBox->ProcessEvent(addToRowFn, ap.data());
                                }
                            }

                            addToVBox(tab1VBox, rowHBox);
                        }
                    }

                    // Modifier key row (same layout as regular rows)
                    {
                        FStaticConstructObjectParameters rowHbP2(hboxClass, outer);
                        UObject* modRow = UObjectGlobals::StaticConstructObject(rowHbP2);
                        if (modRow)
                        {
                            auto* addToRowFn = modRow->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                            auto* rowC = findParam(addToRowFn, STR("Content"));
                            auto* rowR = findParam(addToRowFn, STR("ReturnValue"));

                            // Left: "Set Modifier Key" label
                            UObject* modLabel = makeTB(L"Set Modifier Key", 0.86f, 0.90f, 0.96f, 0.85f, 26);
                            if (modLabel && addToRowFn)
                            {
                                int sz = addToRowFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = modLabel;
                                modRow->ProcessEvent(addToRowFn, ap.data());
                                UObject* labelSlot = rowR ? *reinterpret_cast<UObject**>(ap.data() + rowR->GetOffset_Internal()) : nullptr;
                                if (labelSlot)
                                {
                                    auto* setFill = labelSlot->GetFunctionByNameInChain(STR("SetSize"));
                                    if (setFill) { int s2 = setFill->GetParmsSize(); std::vector<uint8_t> fp(s2, 0); auto* p = findParam(setFill, STR("InSize")); if (p) { *reinterpret_cast<float*>(fp.data() + p->GetOffset_Internal()) = 1.0f; fp[p->GetOffset_Internal() + 4] = 1; /* SizeRule=Fill */ } labelSlot->ProcessEvent(setFill, fp.data()); }
                                }
                            }

                            // Right: modifier key box
                            FStaticConstructObjectParameters kbOlP2(overlayClass, outer);
                            UObject* modKbOl = UObjectGlobals::StaticConstructObject(kbOlP2);
                            if (modKbOl)
                            {
                                auto* addToKbFn = modKbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                UObject* texKeyBox2 = findTexture2DByName(L"T_UI_Btn_P1_Active");
                                if (addToKbFn && texKeyBox2)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* kbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (kbBgImg)
                                    {
                                        umgSetBrushNoMatch(kbBgImg, texKeyBox2, setBrushFn);
                                        umgSetBrushSize(kbBgImg, 220.0f, 42.0f);
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbBgImg;
                                        modKbOl->ProcessEvent(addToKbFn, ap.data());
                                    }
                                }
                                if (addToKbFn)
                                {
                                    UObject* modKbLabel = makeTB(std::wstring(modifierName(s_modifierVK)), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                    m_cfgModBoxLabel = modKbLabel;
                                    if (modKbLabel)
                                    {
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        auto* pRV = findParam(addToKbFn, STR("ReturnValue"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = modKbLabel;
                                        modKbOl->ProcessEvent(addToKbFn, ap.data());
                                        UObject* modKbSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                        if (modKbSlot)
                                        {
                                            auto* setHA = modKbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; modKbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = modKbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; modKbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                    }
                                }
                                if (addToRowFn)
                                {
                                    int sz = addToRowFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = modKbOl;
                                    modRow->ProcessEvent(addToRowFn, ap.data());
                                }
                            }

                            addToVBox(tab1VBox, modRow);
                        }
                    }

                    // Start hidden (ScrollBox hides, not inner VBox)
                    auto* visFn = scrollBox->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFn) { uint8_t p[8]{}; p[0] = 1; scrollBox->ProcessEvent(visFn, p); }
                }
            }

            // Tab 2: Hide Environment (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[2] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab2VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[2] = tab2VBox;

                if (scrollBox && tab2VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab2VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }
                    m_cfgRemovalVBox = tab2VBox;

                    int count = s_config.removalCount.load();
                    m_cfgRemovalHeader = addTB(tab2VBox, L"Saved Removals (" + std::to_wstring(count) + L" entries)",
                                               0.78f, 0.86f, 1.0f, 1.0f, 32);
                    m_cfgLastRemovalCount = count;

                    // Populate removal entries with danger icons
                    rebuildRemovalList();

                    // Start hidden
                    auto* visFn = scrollBox->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFn) { uint8_t p[8]{}; p[0] = 1; scrollBox->ProcessEvent(visFn, p); }
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 200;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1400.0f; v[1] = 900.0f;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }

            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f; // centered
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) / 2.0f;
                    v2[1] = static_cast<float>(viewH) / 2.0f - 100.0f;
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_configWidget = userWidget;
            updateConfigFreeBuild();
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Config UMG widget created\n"));
        }

        // ── Mod Controller Toolbar (3x3, lower-right) ──────────────────────────

        void destroyModControllerBar()
        {
            if (!m_mcBarWidget) return;
            auto* removeFn = m_mcBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_mcBarWidget->ProcessEvent(removeFn, nullptr);
            m_mcBarWidget = nullptr;
            for (int i = 0; i < MC_SLOTS; i++)
            {
                m_mcStateImages[i] = nullptr;
                m_mcIconImages[i] = nullptr;
                m_mcSlotStates[i] = UmgSlotState::Empty;
                m_mcKeyLabels[i] = nullptr;
                m_mcKeyBgImages[i] = nullptr;
            }
            m_mcRotationLabel = nullptr;
            m_mcSlot0Overlay = nullptr;
            m_mcSlot4Overlay = nullptr;
            m_mcSlot6Overlay = nullptr;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] Mod Controller bar removed\n"));
        }

        void createModControllerBar()
        {
            if (m_mcBarWidget)
            {
                destroyModControllerBar();
                showOnScreen(L"Mod Controller removed", 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] === Creating 4x2 Mod Controller toolbar ===\n"));

            // --- Find textures (reuse same state/frame textures + MC slot icons) ---
            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            UObject* texRotation = nullptr;     // T_UI_Refresh — MC slot 0 (Rotation)
            UObject* texTarget = nullptr;       // T_UI_Search — MC slot 1 (Target)
            UObject* texToolbarSwap = nullptr;  // Swap-Bag_Icon — MC slot 2 (Toolbar Swap)
            UObject* texRemoveTarget = nullptr; // T_UI_Icon_GoodPlace2 — MC slot 4 (Remove Target)
            UObject* texUndoLast = nullptr;     // T_UI_Alert_BakedIcon — MC slot 5 (Undo Last)
            UObject* texRemoveAll = nullptr;    // T_UI_Icon_Filled_GoodPlace2 — MC slot 6 (Remove All)
            UObject* texSettings = nullptr;     // T_UI_Icon_Settings — MC slot 7 (Configuration)
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Empty")) texEmpty = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Disabled")) texInactive = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                    else if (name == STR("T_UI_Refresh")) texRotation = t;
                    else if (name == STR("T_UI_Search")) texTarget = t;
                    else if (name == STR("Swap-Bag_Icon")) texToolbarSwap = t;
                    else if (name == STR("T_UI_Icon_GoodPlace2")) texRemoveTarget = t;
                    else if (name == STR("T_UI_Alert_BakedIcon")) texUndoLast = t;
                    else if (name == STR("T_UI_Icon_Filled_GoodPlace2")) texRemoveAll = t;
                    else if (name == STR("T_UI_Icon_Settings")) texSettings = t;
                }
            }
            if (!texFrame || !texEmpty)
            {
                showOnScreen(L"MC: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }
            if (!m_umgTexBlankRect && texBlankRect) m_umgTexBlankRect = texBlankRect; // cache if not yet cached

            // Fallback: try StaticFindObject for textures not found in FindAllOf scan
            // (some textures may not be loaded yet; StaticFindObject with full path can locate them)
            struct TexFallback { UObject*& ref; const TCHAR* path; const wchar_t* name; };
            TexFallback fallbacks[] = {
                {texToolbarSwap, STR("/Game/UI/textures/Interactables/Swap-Bag_Icon.Swap-Bag_Icon"), L"Swap-Bag_Icon"},
                {texRemoveTarget, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Icon_GoodPlace2.T_UI_Icon_GoodPlace2"), L"T_UI_Icon_GoodPlace2"},
                {texUndoLast, STR("/Game/UI/textures/_Shared/Icons/T_UI_Alert_BakedIcon.T_UI_Alert_BakedIcon"), L"T_UI_Alert_BakedIcon"},
                {texRemoveAll, STR("/Game/UI/textures/_Icons/Waypoints/FilledIcons/T_UI_Icon_Filled_GoodPlace2.T_UI_Icon_Filled_GoodPlace2"), L"T_UI_Icon_Filled_GoodPlace2"},
                {texSettings, STR("/Game/UI/textures/_Shared/Icons/T_UI_Icon_Settings.T_UI_Icon_Settings"), L"T_UI_Icon_Settings"},
                {texRotation, STR("/Game/UI/textures/_Shared/Icons/T_UI_Refresh.T_UI_Refresh"), L"T_UI_Refresh"},
                {texTarget, STR("/Game/UI/textures/_Icons/Menus/T_UI_Search.T_UI_Search"), L"T_UI_Search"},
            };
            for (auto& fb : fallbacks)
            {
                if (!fb.ref)
                {
                    fb.ref = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, fb.path);
                    if (fb.ref)
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] {} found via StaticFindObject fallback\n"), fb.name);
                    else
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] WARNING: {} NOT found via FindAllOf or StaticFindObject\n"), fb.name);
                }
            }

            // --- Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showOnScreen(L"MC: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"MC: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"MC: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showOnScreen(L"MC: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Get WidgetTree ---
            UObject* widgetTree = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + 0x01D8);
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"MC: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            // Reuse m_umgSetBrushFn if not already set
            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;

            // --- Outer border (transparent, invisible frame) ---
            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) return;

            if (widgetTree)
                *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + 0x0028) = outerBorder;

            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // --- Root VBox (3 rows) inside border ---
            FStaticConstructObjectParameters rootVBoxP(vboxClass, outer);
            UObject* rootVBox = UObjectGlobals::StaticConstructObject(rootVBoxP);
            if (!rootVBox) return;

            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = rootVBox;
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // --- Create 2 rows x 4 columns = 8 slots ---
            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            int slotIdx = 0;
            for (int row = 0; row < 2; row++)
            {
                // Create HBox for this row
                FStaticConstructObjectParameters hboxP(hboxClass, outer);
                UObject* hbox = UObjectGlobals::StaticConstructObject(hboxP);
                if (!hbox) continue;

                // Add HBox to root VBox
                auto* addRowFn = rootVBox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (addRowFn)
                {
                    auto* pC = findParam(addRowFn, STR("Content"));
                    auto* pR = findParam(addRowFn, STR("ReturnValue"));
                    int sz = addRowFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = hbox;
                    rootVBox->ProcessEvent(addRowFn, ap.data());
                    UObject* rowSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (rowSlot)
                    {
                        umgSetSlotSize(rowSlot, 1.0f, 0); // Auto
                    }
                }

                for (int col = 0; col < 4; col++)
                {
                    int i = slotIdx++;

                    // Create VBox column
                    FStaticConstructObjectParameters vboxP(vboxClass, outer);
                    UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                    if (!vbox) continue;

                    // Create images
                    FStaticConstructObjectParameters siP(imageClass, outer);
                    UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                    if (!stateImg) continue;
                    FStaticConstructObjectParameters iiP(imageClass, outer);
                    UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                    if (!iconImg) continue;
                    FStaticConstructObjectParameters fiP(imageClass, outer);
                    UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                    if (!frameImg) continue;

                    // Create UOverlay
                    FStaticConstructObjectParameters olP(overlayClass, outer);
                    UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                    if (!overlay) continue;

                    // Set textures
                    umgSetBrush(stateImg, texEmpty, setBrushFn);
                    umgSetBrush(frameImg, texFrame, setBrushFn);
                    umgSetOpacity(iconImg, 0.0f);

                    // Read native sizes from first slot; save overlay ref for slot 0
                    if (i == 0)
                    {
                        uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
                        frameW = *reinterpret_cast<float*>(fBase + 0x108 + 0x08);
                        frameH = *reinterpret_cast<float*>(fBase + 0x108 + 0x0C);
                        uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                        stateW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
                        stateH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] Frame: {}x{}, State: {}x{}\n"),
                                                        frameW, frameH, stateW, stateH);
                        m_mcSlot0Overlay = overlay; // save for rotation label (added after loop)
                    }
                    if (i == 4) m_mcSlot4Overlay = overlay; // save for "Single" label
                    if (i == 6) m_mcSlot6Overlay = overlay; // save for "All" label

                    umgSetOpacity(stateImg, 1.0f);
                    umgSetOpacity(frameImg, 0.25f);

                    // Add state + icon to Overlay
                    auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                    if (addToOverlayFn)
                    {
                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                        // State image (bottom layer) — centered to preserve aspect
                        {
                            int sz = addToOverlayFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                            overlay->ProcessEvent(addToOverlayFn, ap.data());
                            UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                            if (stateOlSlot)
                            {
                                auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                if (setHAFn)
                                {
                                    int sz2 = setHAFn->GetParmsSize();
                                    std::vector<uint8_t> hb(sz2, 0);
                                    auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                    if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                                    stateOlSlot->ProcessEvent(setHAFn, hb.data());
                                }
                                auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                if (setVAFn)
                                {
                                    int sz2 = setVAFn->GetParmsSize();
                                    std::vector<uint8_t> vb(sz2, 0);
                                    auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                    if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                                    stateOlSlot->ProcessEvent(setVAFn, vb.data());
                                }
                            }
                        }
                        // Icon image (top layer) — centered
                        {
                            int sz = addToOverlayFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                            overlay->ProcessEvent(addToOverlayFn, ap.data());
                            UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                            if (iconSlot)
                            {
                                auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                if (setHAFn)
                                {
                                    int sz2 = setHAFn->GetParmsSize();
                                    std::vector<uint8_t> hb(sz2, 0);
                                    auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                    if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                                    iconSlot->ProcessEvent(setHAFn, hb.data());
                                }
                                auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                if (setVAFn)
                                {
                                    int sz2 = setVAFn->GetParmsSize();
                                    std::vector<uint8_t> vb(sz2, 0);
                                    auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                    if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                                    iconSlot->ProcessEvent(setVAFn, vb.data());
                                }
                            }
                        }
                    }

                    // Add Overlay + frame to VBox
                    auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                    if (addToVBoxFn)
                    {
                        auto* pC = findParam(addToVBoxFn, STR("Content"));
                        auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));

                        // Overlay (top)
                        {
                            int sz = addToVBoxFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                            vbox->ProcessEvent(addToVBoxFn, ap.data());
                            UObject* olSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                            if (olSlot)
                            {
                                umgSetSlotSize(olSlot, 1.0f, 0); // Auto
                                umgSetHAlign(olSlot, 2);          // HAlign_Center
                            }
                        }
                        // Frame overlay (bottom) — wraps frameImg + keyBgImg + keyLabel
                        {
                            FStaticConstructObjectParameters foP(overlayClass, outer);
                            UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                            if (frameOverlay)
                            {
                                auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                if (addToFoFn)
                                {
                                    auto* foC = findParam(addToFoFn, STR("Content"));
                                    auto* foR = findParam(addToFoFn, STR("ReturnValue"));

                                    // Layer 1: frameImg (bottom — fills overlay)
                                    {
                                        int sz2 = addToFoFn->GetParmsSize();
                                        std::vector<uint8_t> ap2(sz2, 0);
                                        if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                        frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                    }

                                    // Layer 2: keyBgImg (keycap background, centered)
                                    if (texBlankRect)
                                    {
                                        FStaticConstructObjectParameters kbP(imageClass, outer);
                                        UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                        if (keyBgImg && setBrushFn)
                                        {
                                            umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                            umgSetOpacity(keyBgImg, 0.8f);

                                            int sz2 = addToFoFn->GetParmsSize();
                                            std::vector<uint8_t> ap2(sz2, 0);
                                            if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyBgImg;
                                            frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                            UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                            if (kbSlot)
                                            {
                                                auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                                if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                                auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                                if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                            }
                                            m_mcKeyBgImages[i] = keyBgImg;
                                        }
                                    }

                                    // Layer 3: keyLabel (UTextBlock, centered)
                                    if (textBlockClass)
                                    {
                                        FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                        UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                        if (keyLabel)
                                        {
                                            std::wstring kn = keyName(s_bindings[MC_BIND_BASE + i].key);
                                            umgSetText(keyLabel, kn);
                                            umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);

                                            int sz2 = addToFoFn->GetParmsSize();
                                            std::vector<uint8_t> ap2(sz2, 0);
                                            if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyLabel;
                                            frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                            UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                            if (tlSlot)
                                            {
                                                auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                                if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setHA, h.data()); }
                                                auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                                if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setVA, v.data()); }
                                            }
                                            m_mcKeyLabels[i] = keyLabel;
                                        }
                                    }
                                }
                            }

                            // Add frameOverlay (or fall back to frameImg) to VBox
                            UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                            int sz = addToVBoxFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                            vbox->ProcessEvent(addToVBoxFn, ap.data());
                            UObject* fSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                            if (fSlot)
                            {
                                umgSetSlotSize(fSlot, 1.0f, 0); // Auto
                                umgSetHAlign(fSlot, 2);
                                float overlapPx = stateH * 0.25f; // 25% vertical overlap (reduced 5%)
                                umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                            }
                        }
                    }

                    // Add VBox to HBox
                    auto* addToHBoxFn = hbox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                    if (addToHBoxFn)
                    {
                        auto* pC = findParam(addToHBoxFn, STR("Content"));
                        auto* pR = findParam(addToHBoxFn, STR("ReturnValue"));
                        int sz = addToHBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = vbox;
                        hbox->ProcessEvent(addToHBoxFn, ap.data());
                        UObject* hSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (hSlot)
                        {
                            umgSetSlotSize(hSlot, 1.0f, 1); // Fill
                            umgSetVAlign(hSlot, 0);          // VAlign_Fill
                            float colW2 = (frameW > stateW) ? frameW : stateW;
                            float hOverlap = colW2 * 0.10f; // 10% each side = 20% overlap (reduced from 40% for more spacing)
                            umgSetSlotPadding(hSlot, -hOverlap, 0.0f, -hOverlap, 0.0f);
                        }
                    }

                    m_mcStateImages[i] = stateImg;
                    m_mcIconImages[i] = iconImg;
                    m_mcSlotStates[i] = UmgSlotState::Empty;
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] All 8 slots created (4x2)\n"));

            // --- Set custom icons for all 8 MC slots ---
            {
                UObject* mcSlotTextures[MC_SLOTS] = {
                    texRotation, texTarget, texToolbarSwap, nullptr,
                    texRemoveTarget, texUndoLast, texRemoveAll, texSettings
                };
                const wchar_t* mcSlotNames[MC_SLOTS] = {
                    L"T_UI_Refresh", L"T_UI_Search", L"Swap-Bag_Icon", nullptr,
                    L"T_UI_Icon_GoodPlace2", L"T_UI_Alert_BakedIcon", L"T_UI_Icon_Filled_GoodPlace2", L"T_UI_Icon_Settings"
                };
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (!mcSlotTextures[i]) continue; // slot 3 has no icon yet
                    if (m_mcIconImages[i] && setBrushFn)
                    {
                        umgSetBrush(m_mcIconImages[i], mcSlotTextures[i], setBrushFn);
                        umgSetOpacity(m_mcIconImages[i], 1.0f);
                        // Switch state image from empty to active frame for slots with icons
                        if (m_mcStateImages[i] && texActive)
                            umgSetBrush(m_mcStateImages[i], texActive, setBrushFn);
                        // Scale icon to 70% with aspect ratio preservation
                        uint8_t* iBase = reinterpret_cast<uint8_t*>(m_mcIconImages[i]);
                        float texW = *reinterpret_cast<float*>(iBase + 0x108 + 0x08);
                        float texH = *reinterpret_cast<float*>(iBase + 0x108 + 0x0C);
                        if (texW > 0.0f && texH > 0.0f)
                        {
                            // Get state icon size as container bounds
                            float containerW = 64.0f, containerH = 64.0f;
                            if (m_mcStateImages[i])
                            {
                                uint8_t* sBase = reinterpret_cast<uint8_t*>(m_mcStateImages[i]);
                                containerW = *reinterpret_cast<float*>(sBase + 0x108 + 0x08);
                                containerH = *reinterpret_cast<float*>(sBase + 0x108 + 0x0C);
                            }
                            if (containerW < 1.0f) containerW = 64.0f;
                            if (containerH < 1.0f) containerH = 64.0f;
                            float scaleX = containerW / texW;
                            float scaleY = containerH / texH;
                            float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.70f;
                            umgSetBrushSize(m_mcIconImages[i], texW * scale, texH * scale);
                        }
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] Slot {} icon set to {}\n"), i, mcSlotNames[i]);
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] WARNING: {} texture not found for slot {}\n"), mcSlotNames[i], i);
                    }
                }
            }

            // --- Add rotation text overlay on MC slot 0 ---
            if (m_mcSlot0Overlay && textBlockClass)
            {
                auto* addToOverlayFn = m_mcSlot0Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* rotLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (rotLabel)
                    {
                        // Initial text: "5°\nT0"
                        int step = s_overlay.rotationStep;
                        int total = s_overlay.totalRotation;
                        std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
                        umgSetText(rotLabel, txt);
                        umgSetTextColor(rotLabel, 0.4f, 0.6f, 1.0f, 1.0f); // medium blue

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = rotLabel;
                        m_mcSlot0Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* rotSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (rotSlot)
                        {
                            auto* setHA = rotSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; rotSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = rotSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; rotSlot->ProcessEvent(setVA, v.data()); }
                        }
                        m_mcRotationLabel = rotLabel;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] Rotation label created on slot 0\n"));
                    }
                }
            }

            // --- Add "Single" text overlay on MC slot 4 (Remove Target) ---
            if (m_mcSlot4Overlay && textBlockClass)
            {
                auto* addToOverlayFn = m_mcSlot4Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* singleLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (singleLabel)
                    {
                        umgSetText(singleLabel, L"Single");
                        umgSetTextColor(singleLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                        umgSetFontSize(singleLabel, 31); // 10% larger

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = singleLabel;
                        m_mcSlot4Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* labelSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (labelSlot)
                        {
                            auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                        }
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] 'Single' label created on slot 4\n"));
                    }
                }
            }

            // --- Add "All" text overlay on MC slot 6 (Remove All) ---
            if (m_mcSlot6Overlay && textBlockClass)
            {
                auto* addToOverlayFn = m_mcSlot6Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* allLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (allLabel)
                    {
                        umgSetText(allLabel, L"All");
                        umgSetTextColor(allLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                        umgSetFontSize(allLabel, 31); // 10% larger

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = allLabel;
                        m_mcSlot6Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* labelSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (labelSlot)
                        {
                            auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                        }
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] 'All' label created on slot 6\n"));
                    }
                }
            }

            // --- Size and position: lower-right of screen ---
            // SetRenderScale 0.81, 0.81 (shrunk 10% from 0.9)
            umgSetRenderScale(outerBorder, 0.81f, 0.81f);

            float mcIconW = (frameW > stateW) ? frameW : stateW;
            if (mcIconW < 1.0f) mcIconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float mcVOverlap = stateH * 0.25f;                     // 25% vertical overlap (matches slot padding)
            float mcHOverlapPerSlot = mcIconW * 0.20f;             // 20% horizontal overlap (10% each side, reduced from 40%)
            float mcScale = 0.81f;                                  // match render scale for viewport size
            float mcTotalW = (4.0f * mcIconW - 3.0f * mcHOverlapPerSlot) * mcScale * 1.2f;  // 4 cols, 3 gaps, +20% wider for spacing
            float mcSlotH = (frameH + stateH - mcVOverlap);
            float mcTotalH = (2.0f * mcSlotH) * mcScale;           // 2 rows

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = mcTotalW; v[1] = mcTotalH;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size for positioning
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }

            // Alignment: bottom-right pivot (1.0, 1.0) so position is from bottom-right corner
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 1.0f; v[1] = 1.0f; // bottom-right pivot
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: lower-right with 20px margin from edges
            auto* setPosFn = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (setPosFn)
            {
                auto* pPos = findParam(setPosFn, STR("Position"));
                if (pPos)
                {
                    int sz = setPosFn->GetParmsSize();
                    std::vector<uint8_t> pb(sz, 0);
                    auto* v2 = reinterpret_cast<float*>(pb.data() + pPos->GetOffset_Internal());
                    v2[0] = static_cast<float>(viewW) - 135.0f;  // 50px left from previous
                    v2[1] = static_cast<float>(viewH) - 595.0f; // 5px up from previous
                    userWidget->ProcessEvent(setPosFn, pb.data());
                }
            }

            m_mcBarWidget = userWidget;
            showOnScreen(L"Mod Controller created!", 3.0f, 0.0f, 1.0f, 0.0f);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [MC] === Mod Controller bar creation complete ({}x{}) ===\n"),
                                            mcTotalW, mcTotalH);
        }

        // ── 7I: Overlay & Window Management ───────────────────────────────────
        // Start/stop overlay, config window, target info panel
        // Thread lifecycle, GDI+ initialization, slot updates

        // Syncs overlay slot display from m_recipeSlots[]. Loads icon PNGs from disk.
        // Called after recipe assignment, icon extraction, or on mod startup.
        void updateOverlaySlots()
        {
            if (!s_overlay.csInit) return;
            EnterCriticalSection(&s_overlay.slotCS);
            for (int i = 0; i < OVERLAY_BUILD_SLOTS && i < QUICK_BUILD_SLOTS; i++)
            {
                s_overlay.slots[i].used = m_recipeSlots[i].used;
                s_overlay.slots[i].displayName = m_recipeSlots[i].displayName;
                // If texture changed, discard old icon so new one loads
                if (s_overlay.slots[i].textureName != m_recipeSlots[i].textureName)
                {
                    s_overlay.slots[i].icon.reset();
                }
                s_overlay.slots[i].textureName = m_recipeSlots[i].textureName;
                // Try loading PNG icon if we have a texture name and no icon yet
                if (s_overlay.slots[i].used && !s_overlay.slots[i].textureName.empty() && !s_overlay.slots[i].icon && !s_overlay.iconFolder.empty())
                {
                    std::wstring pngPath = s_overlay.iconFolder + L"\\" + s_overlay.slots[i].textureName + L".png";
                    Gdiplus::Image* img = Gdiplus::Image::FromFile(pngPath.c_str());
                    if (img && img->GetLastStatus() == Gdiplus::Ok)
                    {
                        s_overlay.slots[i].icon.reset(img);
                    }
                    else
                    {
                        delete img;
                    }
                }
            }
            LeaveCriticalSection(&s_overlay.slotCS);
            s_overlay.needsUpdate = true;
        }

        // Keep old name as alias for backward compat
        void updateOverlayText()
        {
            updateOverlaySlots();
        }

        void startOverlay()
        {
            if (s_overlay.thread) return;
            if (!s_overlay.csInit)
            {
                InitializeCriticalSection(&s_overlay.slotCS);
                s_overlay.csInit = true;
            }
            // Set icon folder to Mods/MoriaCppMod/icons/
            wchar_t dllPath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
            std::wstring gamePath(dllPath);
            auto pos = gamePath.rfind(L'\\');
            if (pos != std::wstring::npos) gamePath = gamePath.substr(0, pos);
            s_overlay.iconFolder = gamePath + L"\\Mods\\MoriaCppMod\\icons";

            // Initialize GDI+ early so updateOverlaySlots can load cached icon PNGs
            if (!s_overlay.gdipToken)
            {
                Gdiplus::GdiplusStartupInput gdipInput;
                Gdiplus::GdiplusStartup(&s_overlay.gdipToken, &gdipInput, nullptr);
            }

            s_overlay.running = true;
            s_overlay.visible = m_showHotbar;
            s_overlay.activeToolbar = m_activeToolbar;
            updateOverlaySlots();
            s_overlay.thread = CreateThread(nullptr, 0, overlayThreadProc, nullptr, 0, nullptr);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Overlay thread started, icons: {}\n"), s_overlay.iconFolder);
        }

        // LINT NOTE (#9 — stopOverlay race): The 3-second WaitForSingleObject timeout means the render
        // thread could theoretically still hold slotCS when we DeleteCriticalSection below. Analyzed and
        // intentionally skipped: this only fires during mod destructor (game shutdown). Even if the timeout
        // expires and the CS is deleted under the render thread, the game is already closing. Changing to
        // INFINITE wait risks hanging the game exit if the render thread is stuck in GDI+. The current
        // pragmatic timeout works 99.9% of the time and any crash is invisible to the user.
        void stopOverlay()
        {
            s_overlay.running = false;
            if (s_overlay.overlayHwnd) PostMessage(s_overlay.overlayHwnd, WM_CLOSE, 0, 0);
            if (s_overlay.thread)
            {
                WaitForSingleObject(s_overlay.thread, 3000);
                CloseHandle(s_overlay.thread);
                s_overlay.thread = nullptr;
            }
            // Fix #7: Reset GDI+ token so startOverlay() can re-initialize if overlay is restarted.
            // overlayThreadProc() calls GdiplusShutdown on exit but never reset the token to 0,
            // causing the guard `if (!s_overlay.gdipToken)` to skip re-init on restart.
            // Matches the pattern used by configThreadProc and targetInfoThreadProc.
            if (s_overlay.gdipToken)
            {
                Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
                s_overlay.gdipToken = 0;
            }
            // Clean up loaded icons (shared_ptr handles deallocation)
            for (int i = 0; i < OVERLAY_SLOTS; i++)
            {
                s_overlay.slots[i].icon.reset();
            }
            if (s_overlay.csInit)
            {
                DeleteCriticalSection(&s_overlay.slotCS);
                s_overlay.csInit = false;
            }
        }

#if 0 // DISABLED: Win32 Config start/stop — replaced by UMG toggleConfig()
        void startConfig()
        {
            if (s_config.thread) return;
            s_config.running = true;
            s_config.visible = true;
            s_config.activeTab = 0;
            s_config.thread = CreateThread(nullptr, 0, configThreadProc, nullptr, 0, nullptr);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Config window thread started\n"));
        }

        void stopConfig()
        {
            s_config.running = false;
            if (s_config.configHwnd) PostMessage(s_config.configHwnd, WM_CLOSE, 0, 0);
            if (s_config.thread)
            {
                WaitForSingleObject(s_config.thread, 3000);
                CloseHandle(s_config.thread);
                s_config.thread = nullptr;
            }
        }
#endif // Win32 Config start/stop disabled

        // ── Input Mode Helpers (for modal Config Menu) ──────────────────────────
        // Switch to UI-only input so the mouse cursor appears and game input is blocked.
        void setInputModeUI()
        {
            auto* pc = findPlayerController();
            if (!pc || !m_configWidget) return;

            // Find SetInputMode_UIOnlyEx on WidgetBlueprintLibrary CDO
            auto* uiFunc = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:SetInputMode_UIOnlyEx"));
            auto* wblCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary"));
            if (!uiFunc || !wblCDO) {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] setInputModeUI: could not find UIOnlyEx func/CDO\n"));
                return;
            }

            // Params: PlayerController@0, InWidgetToFocus@8, InMouseLockMode@16 (byte)
            uint8_t params[24]{};
            std::memcpy(params + 0, &pc, 8);
            UObject* widget = m_configWidget;
            std::memcpy(params + 8, &widget, 8);
            params[16] = 0; // EMouseLockMode::DoNotLock
            wblCDO->ProcessEvent(uiFunc, params);

            // Set bShowMouseCursor bit (offset 0x0448, bit 0)
            uint8_t* pcRaw = reinterpret_cast<uint8_t*>(pc);
            pcRaw[0x0448] |= 0x01;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Input mode → UI Only (mouse cursor ON)\n"));
        }

        // Restore game-only input so game controls work normally.
        void setInputModeGame()
        {
            auto* pc = findPlayerController();
            if (!pc) return;

            auto* gameFunc = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:SetInputMode_GameOnly"));
            auto* wblCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary"));
            if (!gameFunc || !wblCDO) {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] setInputModeGame: could not find GameOnly func/CDO\n"));
                return;
            }

            // Params: PlayerController@0
            uint8_t params[8]{};
            std::memcpy(params + 0, &pc, 8);
            wblCDO->ProcessEvent(gameFunc, params);

            // Clear bShowMouseCursor bit (offset 0x0448, bit 0)
            uint8_t* pcRaw = reinterpret_cast<uint8_t*>(pc);
            pcRaw[0x0448] &= ~0x01;

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Input mode → Game Only (mouse cursor OFF)\n"));
        }

        void toggleConfig()
        {
            if (!m_configWidget) createConfigWidget();
            if (!m_configWidget) return;
            m_cfgVisible = !m_cfgVisible;
            auto* fn = m_configWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = m_cfgVisible ? 0 : 1; m_configWidget->ProcessEvent(fn, p); }
            if (m_cfgVisible)
            {
                updateConfigKeyLabels();
                updateConfigFreeBuild();
                updateConfigRemovalCount();
                setInputModeUI();
            }
            else
            {
                setInputModeGame();
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Config {} (UMG)\n"), m_cfgVisible ? STR("shown") : STR("hidden"));
        }

#if 0 // DISABLED: Win32 Target Info start/stop — replaced by UMG
        void startTargetInfo()
        {
            if (s_targetInfo.thread) return;
            if (!s_targetInfo.csInit)
            {
                InitializeCriticalSection(&s_targetInfo.dataCS);
                s_targetInfo.csInit = true;
            }
            s_targetInfo.running = true;
            s_targetInfo.visible = true;
            s_targetInfo.thread = CreateThread(nullptr, 0, targetInfoThreadProc, nullptr, 0, nullptr);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] TargetInfo window thread started\n"));
        }

        void stopTargetInfo()
        {
            s_targetInfo.running = false;
            if (s_targetInfo.hwnd) PostMessage(s_targetInfo.hwnd, WM_CLOSE, 0, 0);
            if (s_targetInfo.thread)
            {
                WaitForSingleObject(s_targetInfo.thread, 3000);
                CloseHandle(s_targetInfo.thread);
                s_targetInfo.thread = nullptr;
            }
            if (s_targetInfo.csInit)
            {
                DeleteCriticalSection(&s_targetInfo.dataCS);
                s_targetInfo.csInit = false;
            }
        }
#endif

        void showTargetInfo(const std::wstring& name,
                            const std::wstring& display,
                            const std::wstring& path,
                            const std::wstring& cls,
                            bool buildable = false,
                            const std::wstring& recipe = L"",
                            const std::wstring& rowName = L"")
        {
            showTargetInfoUMG(name, display, path, cls, buildable, recipe, rowName);
        }

        // ── startRotationSpy — DISABLED: keybind removed ──
#if 0
        void startRotationSpy()
        {
            if (m_spyActive)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Spy] Already active\n"));
                return;
            }
            m_spyActive = true;
            m_spyFrameCount = 0;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Spy] === ROTATION SPY ACTIVE — press R now! ===\n"));
            showOnScreen(L"SPY ACTIVE - press R now!", 5.0f, 1.0f, 1.0f, 0.0f);
        }
#endif

        void rotateAimedBuilding_legacy()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return;

            uint8_t ltParams[LTOff::ParmsSize]{};
            std::memcpy(ltParams + LTOff::WorldContextObject, &pc, 8);
            std::memcpy(ltParams + LTOff::Start, &start, 12);
            std::memcpy(ltParams + LTOff::End, &end, 12);
            ltParams[LTOff::TraceChannel] = 0;
            ltParams[LTOff::bTraceComplex] = 0;
            ltParams[LTOff::bIgnoreSelf] = 1;

            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(ltParams + LTOff::ActorsToIgnore, &arrPtr, 8);
                std::memcpy(ltParams + LTOff::ActorsToIgnore + 8, &one, 4);
                std::memcpy(ltParams + LTOff::ActorsToIgnore + 12, &one, 4);
            }

            kslCDO->ProcessEvent(ltFunc, ltParams);

            if (ltParams[LTOff::ReturnValue] == 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] No hit\n"));
                return;
            }

            uint8_t hitBuf[136]{};
            std::memcpy(hitBuf, ltParams + LTOff::OutHit, 136);
            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp) return;

            auto* ownerFunc = hitComp->GetFunctionByNameInChain(STR("GetOwner"));
            UObject* actor = nullptr;
            if (ownerFunc)
            {
                struct
                {
                    UObject* Ret{nullptr};
                } op{};
                hitComp->ProcessEvent(ownerFunc, &op);
                actor = op.Ret;
            }
            if (!actor) return;

            std::wstring actorClass = safeClassName(actor);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Actor: {} ({})\n"), std::wstring(actor->GetName()), actorClass);

            // Get root component
            UObject* rootComp = nullptr;
            auto* getRootComp = actor->GetFunctionByNameInChain(STR("K2_GetRootComponent"));
            if (getRootComp)
            {
                struct
                {
                    UObject* Ret{nullptr};
                } rc{};
                actor->ProcessEvent(getRootComp, &rc);
                rootComp = rc.Ret;
            }

            // Collect actor's scene components
            std::vector<UObject*> actorComps;
            {
                std::vector<UObject*> allComps;
                UObjectGlobals::FindAllOf(STR("SceneComponent"), allComps);
                for (auto* c : allComps)
                {
                    if (!c) continue;
                    auto* cOwner = c->GetFunctionByNameInChain(STR("GetOwner"));
                    if (!cOwner) continue;
                    struct
                    {
                        UObject* Ret{nullptr};
                    } co{};
                    c->ProcessEvent(cOwner, &co);
                    if (co.Ret == actor) actorComps.push_back(c);
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Found {} scene components on actor\n"), actorComps.size());

            // ── Step 1: Read rotation before ──
            auto* getRot = actor->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
            FRotator3f before{};
            if (getRot) actor->ProcessEvent(getRot, &before);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] BEFORE: P={:.2f} Y={:.2f} R={:.2f}\n"), before.Pitch, before.Yaw, before.Roll);

            // ── Step 2: HIDE actor first (purges static draw list entries) ──
            auto* setHidden = actor->GetFunctionByNameInChain(STR("SetActorHiddenInGame"));
            if (setHidden)
            {
                uint8_t bHide = 1;
                actor->ProcessEvent(setHidden, &bHide);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Hidden actor\n"));
            }
            // Also hide each component individually
            for (auto* c : actorComps)
            {
                auto* setVis = c->GetFunctionByNameInChain(STR("SetVisibility"));
                if (setVis)
                {
                    uint8_t visParams[2] = {0, 1}; // hide, propagate
                    c->ProcessEvent(setVis, visParams);
                }
            }

            // ── Step 3: Set mobility to Movable on all components ──
            int mobilitySet = 0;
            for (auto* c : actorComps)
            {
                for (auto* prop : c->GetClassPrivate()->ForEachPropertyInChain())
                {
                    std::wstring pn(prop->GetName());
                    if (pn == STR("Mobility"))
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(c);
                        uint8_t curMob = *(base + prop->GetOffset_Internal());
                        if (curMob == 2)
                        {
                            mobilitySet++;
                            break;
                        }
                        auto* setMobFunc = c->GetFunctionByNameInChain(STR("SetMobility"));
                        if (setMobFunc)
                        {
                            uint8_t mobParam = 2;
                            c->ProcessEvent(setMobFunc, &mobParam);
                        }
                        else
                        {
                            *(base + prop->GetOffset_Internal()) = 2;
                        }
                        mobilitySet++;
                        break;
                    }
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Set Movable on {} components\n"), mobilitySet);

            // ── Step 4: Set rotation via K2_SetActorLocationAndRotation (atomic + teleport) ──
            auto* getLocFunc = actor->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
            auto* setLocRot = actor->GetFunctionByNameInChain(STR("K2_SetActorLocationAndRotation"));
            if (getLocFunc && setLocRot)
            {
                FVec3f curLoc{};
                actor->ProcessEvent(getLocFunc, &curLoc);
                // Layout: Location@0(12) Rotation@12(12) bSweep@24(1) SweepHitResult@28(136) bTeleport@164(1) ret@165(1)
                uint8_t slrParams[166]{};
                std::memcpy(slrParams, &curLoc, 12);
                FRotator3f newRot = {before.Pitch, before.Yaw + 45.0f, before.Roll};
                std::memcpy(slrParams + 12, &newRot, 12);
                slrParams[164] = 1; // bTeleport
                actor->ProcessEvent(setLocRot, slrParams);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] K2_SetActorLocationAndRotation applied\n"));
            }

            // Also set on root component directly
            if (rootComp)
            {
                auto* compSetRot = rootComp->GetFunctionByNameInChain(STR("K2_SetWorldRotation"));
                if (compSetRot)
                {
                    uint8_t p2[153]{};
                    FRotator3f newRot = {before.Pitch, before.Yaw + 45.0f, before.Roll};
                    std::memcpy(p2, &newRot, 12);
                    p2[152] = 1; // bTeleport
                    rootComp->ProcessEvent(compSetRot, p2);
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] RootComp K2_SetWorldRotation applied\n"));
                }
            }

            // ── Step 5: SHOW actor again (re-registers with new mobility + rotation) ──
            if (setHidden)
            {
                uint8_t bHide = 0;
                actor->ProcessEvent(setHidden, &bHide);
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] Shown actor\n"));
            }
            for (auto* c : actorComps)
            {
                auto* setVis = c->GetFunctionByNameInChain(STR("SetVisibility"));
                if (setVis)
                {
                    uint8_t visParams[2] = {1, 1}; // show, propagate
                    c->ProcessEvent(setVis, visParams);
                }
            }

            // ── Verify final rotation ──
            FRotator3f finalRot{};
            if (getRot) actor->ProcessEvent(getRot, &finalRot);
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] FINAL: P={:.2f} Y={:.2f} R={:.2f}\n"), finalRot.Pitch, finalRot.Yaw, finalRot.Roll);

            if (std::abs(finalRot.Yaw - before.Yaw) > 0.1f)
            {
                showOnScreen(L"Rotated +45 deg", 3.0f, 0.0f, 1.0f, 0.0f);
            }
            else
            {
                showOnScreen(L"Rotation failed - check log", 3.0f, 1.0f, 0.3f, 0.0f);
            }

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Rotate] === DONE ===\n"));
        }

        void undoLast()
        {
            if (m_undoStack.empty())
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Nothing to undo\n"));
                showInfoBox(L"Undo", L"Nothing to undo", 1.0f, 0.5f, 0.0f);
                return;
            }

            auto& last = m_undoStack.back();

            // Type rule undo: restore all instances and remove the @rule
            if (last.isTypeRule)
            {
                std::string meshId = last.typeRuleMeshId;

                // Collect all undo entries for this type rule (they're contiguous at the back)
                std::vector<RemovedInstance> toRestore;
                while (!m_undoStack.empty())
                {
                    auto& entry = m_undoStack.back();
                    if (!entry.isTypeRule || entry.typeRuleMeshId != meshId) break;
                    toRestore.push_back(entry);
                    m_undoStack.pop_back();
                }

                // Restore all instances (un-hide them by restoring original transform)
                int restored = 0;
                for (auto& ri : toRestore)
                {
                    // Validate UObject pointer before restoring (GC may have freed it)
                    if (ri.component && isReadableMemory(ri.component, sizeof(void*)))
                    {
                        if (restoreInstance(ri.component, ri.instanceIndex, ri.transform)) restored++;
                    }
                }

                // Remove the type rule
                m_typeRemovals.erase(meshId);
                rewriteSaveFile();
                buildRemovalEntries();

                std::wstring meshIdW(meshId.begin(), meshId.end());
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Undo type rule: {} — restored {} instances\n"), meshIdW, restored);
                showInfoBox(L"Undo Type", meshIdW + L" (" + std::to_wstring(restored) + L" restored)", 0.5f, 0.5f, 1.0f);
                return;
            }

            // Single instance undo
            std::string meshId = componentNameToMeshId(last.componentName);
            float px = last.transform.Translation.X;
            float py = last.transform.Translation.Y;
            float pz = last.transform.Translation.Z;

            bool foundInSave = false;
            for (size_t i = 0; i < m_savedRemovals.size(); i++)
            {
                if (m_savedRemovals[i].meshName == meshId)
                {
                    float ddx = m_savedRemovals[i].posX - px;
                    float ddy = m_savedRemovals[i].posY - py;
                    float ddz = m_savedRemovals[i].posZ - pz;
                    if (ddx * ddx + ddy * ddy + ddz * ddz < POS_TOLERANCE * POS_TOLERANCE)
                    {
                        m_savedRemovals.erase(m_savedRemovals.begin() + i);
                        if (i < m_appliedRemovals.size()) m_appliedRemovals.erase(m_appliedRemovals.begin() + i);
                        foundInSave = true;
                        break;
                    }
                }
            }
            if (foundInSave)
            {
                rewriteSaveFile();
                buildRemovalEntries();
            }

            // Validate UObject pointer before restoring (GC may have freed it)
            bool ok = false;
            if (last.component && isReadableMemory(last.component, sizeof(void*)))
            {
                ok = restoreInstance(last.component, last.instanceIndex, last.transform);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Undo: component pointer stale, skipping restore\n"));
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Restored index {} ({}) | {} remaining\n"),
                                            last.instanceIndex,
                                            ok ? STR("ok") : STR("FAILED"),
                                            m_savedRemovals.size());
            showInfoBox(L"Restored", std::to_wstring(m_savedRemovals.size()) + L" saved", 0.5f, 0.5f, 1.0f);

            m_undoStack.pop_back();
        }

      public:
        // ── 7J: Public Interface ──────────────────────────────────────────────
        // Constructor, destructor, on_unreal_init (keybinds + hooks),
        // on_update (per-frame tick loop for state machines + replay)
        MoriaCppMod()
        {
            ModVersion = STR("1.3");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("HISM removal + 45-deg rotation + quick-build hotbar");
            // Init removal list CS before loadSaveFile can be called
            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Loaded v1.3\n"));
        }

        ~MoriaCppMod() override
        {
            // Disable ProcessEvent hooks FIRST — before any blocking waits.
            // The pre/post hooks check s_instance and early-return when null.
            // Without this, hooks fire during the stopOverlay 3s wait, calling
            // GetName()/GetClassPrivate() on UObjects mid-destruction, which can
            // corrupt the UObject hash table and cause RemoveFromHash crashes.
            s_instance = nullptr;

            stopOverlay();
            // stopConfig() removed — Config is now a UMG widget (no Win32 thread)
            // stopTargetInfo() removed — Target Info is now a UMG widget
            if (s_config.removalCSInit)
            {
                DeleteCriticalSection(&s_config.removalCS);
                s_config.removalCSInit = false;
            }
        }

        // Called once when UE4SS has finished initializing Unreal Engine hooks.
        // Discovers functions, loads save files, registers keybinds, starts overlay,
        // and installs ProcessEvent pre/post hooks for rotation + recipe capture.
        auto on_unreal_init() -> void override
        {
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Unreal initialized.\n"));

            // Storage patch disabled — bag-as-intermediary swap doesn't need it
            // patchBodyInventoryStorage();

            m_saveFilePath = "Mods/MoriaCppMod/removed_instances.txt";
            loadSaveFile();
            buildRemovalEntries();
            probePrintString();
            loadQuickBuildSlots();
            loadKeybindings();

            // Num1/Num2/Num6 removal handlers removed — now handled by MC polling (slots 4/5/6)

            register_keydown_event(Input::Key::NUM_FOUR, [this]() {
                if (m_cfgVisible) return;
                dumpAimedActor();
            });

            // Quick-build hotbar: F1-F8 = build, Modifier+F1-F8 = assign slot
            // Modifier key is configurable (SHIFT/CTRL/ALT) via F12 Key Mapping tab
            // All 3 modifier variants registered; isModifierDown() gates the callback at runtime

            const Input::Key fkeys[] = {Input::Key::F1, Input::Key::F2, Input::Key::F3, Input::Key::F4, Input::Key::F5, Input::Key::F6, Input::Key::F7, Input::Key::F8};
            for (int i = 0; i < 8; i++)
            { // F1-F8 for quickbuild — skip when config menu is visible (modal)
                register_keydown_event(fkeys[i], [this, i]() {
                    if (m_cfgVisible) return;
                    quickBuildSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::SHIFT}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::CONTROL}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::ALT}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
            }

            // OEM_FIVE/F12/F9/F10 handlers removed — now handled by MC polling (slots 0-2, 7)

            // Hotbar overlay toggle: Numpad * (Multiply)
            register_keydown_event(Input::Key::MULTIPLY, [this]() {
                if (m_cfgVisible) return;
                m_showHotbar = !m_showHotbar;
                s_overlay.visible = m_showHotbar;
                s_overlay.needsUpdate = true;
                showOnScreen(m_showHotbar ? L"Hotbar overlay ON" : L"Hotbar overlay OFF", 2.0f, 0.2f, 0.8f, 1.0f);
            });

            // Builders bar toggle: now handled by AB toolbar key polling (s_bindings[BIND_AB_OPEN])

            // Mod Controller toolbar toggle: Numpad 7
            register_keydown_event(Input::Key::NUM_SEVEN, [this]() { if (m_cfgVisible) return; createModControllerBar(); });

            // Spy mode: capture ProcessEvent calls with rotation/build in the function name
            s_instance = this;
            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;

#if 0 // DISABLED: Tutorial/breadcrumb tracking + suppression — features removed
                // ── Tutorial Tracking: log all tutorial-related ProcessEvent calls ──
                if (s_config.trackTutorials.load() && parms)
                {
                    std::wstring fn(func->GetName());
                    bool isTutorialFunc =
                        fn == STR("TriggerTutorial") || fn == STR("TriggerTutorialUponCompletion") ||
                        fn == STR("CanTriggerTutorial") || fn == STR("RecordBreadcrumb") ||
                        fn == STR("ReportTutorialAction") || fn == STR("ReportTutorialItemAction") ||
                        fn == STR("ReportTutorialRepairAction") || fn == STR("ShowTutorialDisplay") ||
                        fn == STR("OnTutorialUnlock") || fn == STR("OnTutorialComplete") ||
                        fn == STR("BP_ShowTutorialDisplay") || fn == STR("Show Next Tutorial") ||
                        fn == STR("ShowNextIncompleteTutorial") || fn == STR("ManualShowTutorialDisplay") ||
                        fn == STR("HandleUpdateHUDTutorial") || fn == STR("SetTutorialEntrySelected") ||
                        fn == STR("SetTutorialEntryViewed") || fn == STR("IsTutorialCompleted") ||
                        fn == STR("IsTutorialListItemCompleted") || fn == STR("UnlockTip") ||
                        fn == STR("SetBreadcrumb") || fn == STR("OnPinnedRecipeChanged") ||
                        fn == STR("SetRecipeUnlockState") || fn == STR("RefreshButton") ||
                        fn == STR("NativeRecordItemRecipeBreadcrumbs") || fn == STR("NativeClearItemRecipeBreadcrumbs") ||
                        fn == STR("NativeForgetItemRecipeBreadcrumbs") || fn == STR("UpdateBreadcrumbCounts") ||
                        fn == STR("HasBreadcrumb") || fn == STR("ClearBreadcrumb") ||
                        fn == STR("SetRecipeViewed") || fn == STR("IsRecipeViewed") ||
                        fn == STR("MarkAllAsRead") || fn == STR("MarkAllRead") ||
                        fn == STR("ChangeRecipeViewerNew") || fn == STR("CheckShouldBreadcrumb") ||
                        fn == STR("NativeRecordConstructionRecipeBreadcrumbs");
                    if (isTutorialFunc)
                    {
                        std::wstring cls = safeClassName(context);
                        Output::send<LogLevel::Warning>(STR("[TutTrack] {}::{} ({}B)\n"), cls, fn, func->GetParmsSize());
                        try
                        {
                            uint8_t* p = static_cast<uint8_t*>(parms);
                            if (fn == STR("TriggerTutorial") || fn == STR("TriggerTutorialUponCompletion"))
                            {
                                // Params: ACharacter*(8B) + FMorTutorialRowHandle(DataTable* 8B + FName 8B)
                                if (isReadableMemory(p + 16, 8))
                                {
                                    auto* rowName = reinterpret_cast<FName*>(p + 16);
                                    Output::send<LogLevel::Warning>(STR("[TutTrack]   RowName={}\n"), rowName->ToString());
                                }
                            }
                            else if (fn == STR("RecordBreadcrumb"))
                            {
                                if (cls.find(STR("TutorialManager")) != std::wstring::npos)
                                {
                                    // BP_TutorialManager::RecordBreadcrumb(FMorTutorialRowHandle, bool IsNew)
                                    if (isReadableMemory(p + 8, 8))
                                    {
                                        auto* rowName = reinterpret_cast<FName*>(p + 8);
                                        bool isNew = *reinterpret_cast<bool*>(p + 16);
                                        Output::send<LogLevel::Warning>(STR("[TutTrack]   TutRowName={}, IsNew={}\n"), rowName->ToString(), isNew ? 1 : 0);
                                    }
                                }
                                else
                                {
                                    // Subsystem::RecordBreadcrumb(FGameplayTag, FName CategoryName, FName UniqueName)
                                    if (isReadableMemory(p, 24))
                                    {
                                        auto* tag = reinterpret_cast<FName*>(p + 0);
                                        auto* catName = reinterpret_cast<FName*>(p + 8);
                                        auto* uniqName = reinterpret_cast<FName*>(p + 16);
                                        Output::send<LogLevel::Warning>(STR("[TutTrack]   tag={}, cat={}, unique={}\n"),
                                                                        tag->ToString(), catName->ToString(), uniqName->ToString());
                                    }
                                }
                            }
                            else if (fn == STR("IsTutorialCompleted") || fn == STR("IsTutorialListItemCompleted"))
                            {
                                // Params: ACharacter*(8B) + FMorTutorialRowHandle(DataTable* 8B + FName 8B)
                                if (isReadableMemory(p + 16, 8))
                                {
                                    auto* rowName = reinterpret_cast<FName*>(p + 16);
                                    Output::send<LogLevel::Warning>(STR("[TutTrack]   RowName={}\n"), rowName->ToString());
                                }
                            }
                            else if (fn == STR("SetBreadcrumb"))
                            {
                                // UI_WBP_Craft_List_Item_C::SetBreadcrumb(bool On)
                                bool on = *reinterpret_cast<bool*>(p);
                                Output::send<LogLevel::Warning>(STR("[TutTrack]   On={}\n"), on ? 1 : 0);
                            }
                            else if (fn == STR("SetRecipeUnlockState"))
                            {
                                // SetRecipeUnlockState(EMorRecipeDiscoveryState, int32 curMat, int32 totalMat, int32 curFrag, int32 totalFrag)
                                uint8_t state = *reinterpret_cast<uint8_t*>(p);
                                int32_t curMat = *reinterpret_cast<int32_t*>(p + 4);
                                int32_t totalMat = *reinterpret_cast<int32_t*>(p + 8);
                                Output::send<LogLevel::Warning>(STR("[TutTrack]   state={}, mat={}/{}\n"), static_cast<int>(state), curMat, totalMat);
                            }
                            else if (fn == STR("ShowTutorialDisplay") || fn == STR("BP_ShowTutorialDisplay") ||
                                     fn == STR("ManualShowTutorialDisplay"))
                            {
                                if (context)
                                    Output::send<LogLevel::Warning>(STR("[TutTrack]   context={}\n"), std::wstring(context->GetName()));
                            }
                            else if (fn == STR("HasBreadcrumb") || fn == STR("ClearBreadcrumb"))
                            {
                                // (FGameplayTag, FName CategoryName, FName UniqueName)
                                if (isReadableMemory(p, 24))
                                {
                                    auto* tag = reinterpret_cast<FName*>(p + 0);
                                    auto* catName = reinterpret_cast<FName*>(p + 8);
                                    auto* uniqName = reinterpret_cast<FName*>(p + 16);
                                    Output::send<LogLevel::Warning>(STR("[TutTrack]   tag={}, cat={}, unique={}\n"),
                                                                    tag->ToString(), catName->ToString(), uniqName->ToString());
                                }
                            }
                            else if (fn == STR("ChangeRecipeViewerNew"))
                            {
                                // (int32 OldCount, int32 NewCount)
                                int32_t oldC = *reinterpret_cast<int32_t*>(p);
                                int32_t newC = *reinterpret_cast<int32_t*>(p + 4);
                                Output::send<LogLevel::Warning>(STR("[TutTrack]   old={}, new={}\n"), oldC, newC);
                            }
                            else if (fn == STR("CheckShouldBreadcrumb"))
                            {
                                // (bool& IsNew) — output param
                                Output::send<LogLevel::Warning>(STR("[TutTrack]   (output param)\n"));
                            }
                        }
                        catch (...) { /* safe — skip param extraction on error */ }
                    }
                }

                // ── Permanent tutorial HUD suppression (set by "Complete All Tutorials" button) ──
                // Blocks all tutorial display functions so "Learning the game" never appears
                if (s_config.hideTutorialHUD.load() && parms)
                {
                    std::wstring fn(func->GetName());
                    if (fn == STR("ShowTutorialDisplay") || fn == STR("BP_ShowTutorialDisplay") ||
                        fn == STR("ShowNextIncompleteTutorial") || fn == STR("Show Next Tutorial") ||
                        fn == STR("ManualShowTutorialDisplay") || fn == STR("HandleUpdateHUDTutorial") ||
                        fn == STR("OnTutorialUnlock") || fn == STR("OnTutorialComplete"))
                    {
                        int sz = func->GetParmsSize();
                        if (sz > 0 && sz <= 128) std::memset(parms, 0, sz);
                        return;
                    }
                }

                // Suppress tutorial/tip/lore notification ProcessEvent calls when enabled
                if (s_config.suppressTutorials.load() && parms)
                {
                    std::wstring fn(func->GetName());
                    // Block tutorial triggers by nulling the Player (first 8B) param
                    // Bypass when internalTutorialCall is set (our own TriggerTutorial calls)
                    if (!s_config.internalTutorialCall.load() &&
                        (fn == STR("TriggerTutorial") || fn == STR("TriggerTutorialUponCompletion") || fn == STR("CanTriggerTutorial") ||
                         fn == STR("ReportTutorialAction") || fn == STR("ReportTutorialItemAction") || fn == STR("ReportTutorialRepairAction")))
                    {
                        std::memset(parms, 0, 8); // null out ACharacter* Player (first param)
                        return;
                    }
                    // Block tutorial display/notification (zero entire small param buffers)
                    if (fn == STR("ShowTutorialDisplay") || fn == STR("OnTutorialUnlock") || fn == STR("OnTutorialComplete") ||
                        fn == STR("BP_ShowTutorialDisplay") || fn == STR("Show Next Tutorial") || fn == STR("ShowNextIncompleteTutorial") ||
                        fn == STR("ManualShowTutorialDisplay") || fn == STR("HandleUpdateHUDTutorial") ||
                        fn == STR("GetClientTutorials") || fn == STR("IsTutorialCompleted") ||
                        fn == STR("CanTriggerTutorial") || fn == STR("TriggerTutorial"))
                    {
                        int sz = func->GetParmsSize();
                        if (sz > 0 && sz <= 128) std::memset(parms, 0, sz);
                        return;
                    }
                    // Block tip unlock (already all unlocked, but just in case)
                    if (fn == STR("UnlockTip"))
                    {
                        int sz = func->GetParmsSize();
                        if (sz > 0 && sz <= 64) std::memset(parms, 0, sz);
                        return;
                    }
                    // Block lore unlock notifications
                    if (fn == STR("OnLoreUnlock") || fn == STR("GoalComplete"))
                    {
                        int sz = func->GetParmsSize();
                        if (sz > 0 && sz <= 128) std::memset(parms, 0, sz);
                        return;
                    }
                }
#endif // DISABLED tutorial/breadcrumb tracking + suppression

                // Intercept RotatePressed on BuildHUD: set GATA rotation step + track cumulative rotation
                {
                    std::wstring fn(func->GetName());
                    if (fn == STR("RotatePressed") || fn == STR("RotateCcwPressed"))
                    {
                        std::wstring cls = safeClassName(context);
                        if (!cls.empty() && cls.find(STR("BuildHUD")) != std::wstring::npos)
                        {
                            UObject* gata = s_instance->resolveGATA();
                            if (gata)
                            {
                                const int step = s_overlay.rotationStep.load();
                                s_instance->setGATARotation(gata, static_cast<float>(step));
                                // Track cumulative rotation (0-359°)
                                if (fn == STR("RotatePressed"))
                                {
                                    s_overlay.totalRotation = (s_overlay.totalRotation.load() + step) % 360;
                                }
                                else
                                {
                                    s_overlay.totalRotation = (s_overlay.totalRotation.load() - step + 360) % 360;
                                }
                                s_overlay.needsUpdate = true;
                                if (s_instance) s_instance->updateMcRotationLabel();
                            }
                        }
                    }
                }

                // (Quick-build capture moved to post-hook below)

                // Full spy mode: log EVERY ProcessEvent call (during RotatePressed)
                if (s_instance->m_spyAll)
                {
                    std::wstring funcName(func->GetName());
                    std::wstring objClass = context ? safeClassName(context) : STR("null");
                    std::wstring objName = context ? std::wstring(context->GetName()) : STR("null");
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SPYALL] {}.{} ({}B) on {}\n"), objClass, funcName, func->GetParmsSize(), objName);
                    return;
                }

                if (!s_instance->m_spyActive) return;
                std::wstring funcName(func->GetName());
                if (funcName.find(STR("otat")) != std::wstring::npos || funcName.find(STR("uild")) != std::wstring::npos ||
                    funcName.find(STR("lace")) != std::wstring::npos || funcName.find(STR("onstruct")) != std::wstring::npos)
                {
                    std::wstring objName = context ? std::wstring(context->GetName()) : STR("null");
                    std::wstring objClass = context ? safeClassName(context) : STR("null");
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SPY] {}.{} ({}B) on {} ({})\n"), objClass, funcName, func->GetParmsSize(), objName, objClass);
                    if (funcName.find(STR("OnRotation")) != std::wstring::npos && parms && func->GetParmsSize() > 0)
                    {
                        int sz = func->GetParmsSize();
                        uint8_t* p = reinterpret_cast<uint8_t*>(parms);
                        int32_t ival = (sz >= 4) ? *reinterpret_cast<int32_t*>(p) : 0;
                        float fval = (sz >= 4) ? *reinterpret_cast<float*>(p) : 0.0f;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SPY]   param: int={} float={:.2f}\n"), ival, fval);
                    }
                }
            });

            // Post-hook: capture recipe display name from blockSelectedEvent
            // Only captures from MANUAL clicks — automated quickbuild selections are suppressed
            Unreal::Hook::RegisterProcessEventPostCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance || !func) return;

                // Skip capture during automated quickbuild activation
                if (s_instance->m_isAutoSelecting) return;

                // Tutorial tracking: log return values from completion checks (post-call)
                if (s_config.trackTutorials.load() && parms)
                {
                    std::wstring tfn(func->GetName());
                    if (tfn == STR("IsTutorialCompleted") || tfn == STR("IsTutorialListItemCompleted") ||
                        tfn == STR("CanTriggerTutorial"))
                    {
                        try
                        {
                            uint8_t* p = static_cast<uint8_t*>(parms);
                            int sz = func->GetParmsSize();
                            // FName RowName at offset 16 (after ACharacter* 8B + DataTable* 8B)
                            std::wstring rowName;
                            if (isReadableMemory(p + 16, 8))
                                rowName = reinterpret_cast<FName*>(p + 16)->ToString();
                            // Return bool at end of param buffer (after 24B of input params)
                            bool result = false;
                            if (sz > 24) result = *reinterpret_cast<bool*>(p + 24);
                            Output::send<LogLevel::Warning>(STR("[TutTrack:Post] {} RowName={} => {}\n"),
                                                            tfn, rowName, result ? STR("TRUE") : STR("FALSE"));
                        }
                        catch (...) { /* safe */ }
                    }
                }

                std::wstring fn(func->GetName());
                if (fn != STR("blockSelectedEvent")) return;
                std::wstring cls = safeClassName(context);
                if (cls != STR("UI_WBP_Build_Tab_C")) return;

                int sz = func->GetParmsSize();
                if (!parms || sz < 132) return;
                uint8_t* p = reinterpret_cast<uint8_t*>(parms);

                // Extract display name from selfRef widget's blockName TextBlock
                UObject* selfRef = *reinterpret_cast<UObject**>(p + 120);
                if (!selfRef) return;

                std::wstring displayName = s_instance->readWidgetDisplayName(selfRef);
                if (displayName.empty()) return;

                s_instance->m_lastCapturedName = displayName;
                std::memcpy(s_instance->m_lastCapturedBLock, p, BLOCK_DATA_SIZE);
                s_instance->m_hasLastCapture = true;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] Captured: '{}' (with bLock data)\n"), displayName);

                // ONE-TIME: scan selfRef widget memory to find where bLock data lives
                // This discovers the offset so we can read fresh recipe data at activation time
                if (s_instance->m_bLockWidgetOffset < 0)
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(selfRef);
                    // Scan first 2048 bytes of widget for the bLock data
                    // Match on first 16 bytes (should be unique enough — recipe identifier)
                    for (int off = 0; off <= 2048 - BLOCK_DATA_SIZE; off += 8)
                    {
                        if (std::memcmp(widgetBase + off, p, 16) == 0)
                        {
                            // Verify full 120-byte match (skip bytes 104-111 which is the volatile pointer)
                            bool match = (std::memcmp(widgetBase + off, p, 104) == 0) && (std::memcmp(widgetBase + off + 112, p + 112, 8) == 0);
                            if (match)
                            {
                                s_instance->m_bLockWidgetOffset = off;
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] DISCOVERED: bLock data at widget offset {} (0x{:X})\n"), off, off);
                                break;
                            }
                        }
                    }
                    if (s_instance->m_bLockWidgetOffset < 0)
                    {
                        // Try indirect: check if widget has a pointer to memory containing bLock
                        for (int off = 0; off <= 2048 - 8; off += 8)
                        {
                            uint8_t* ptr = *reinterpret_cast<uint8_t**>(widgetBase + off);
                            if (!ptr || !isReadableMemory(ptr, BLOCK_DATA_SIZE)) continue;
                            if (std::memcmp(ptr, p, 104) == 0 && std::memcmp(ptr + 112, p + 112, 8) == 0)
                            {
                                // Found it via pointer indirection
                                s_instance->m_bLockWidgetOffset = off;
                                s_instance->m_bLockIsIndirect = true;
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] DISCOVERED: bLock via POINTER at widget offset {} (0x{:X})\n"),
                                                                off,
                                                                off);
                                break;
                            }
                        }
                    }
                    if (s_instance->m_bLockWidgetOffset < 0)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] WARNING: bLock data NOT found in widget memory!\n"));
                    }
                }
            });

            m_replayActive = true;
            Output::send<LogLevel::Warning>(
                    STR("[MoriaCppMod] v1.7: Num=removal | Alt+Num=cheats | F1-F8=build | F9=rotate | \\=swap | F12=config | Num*=overlay\n"));
        }

        // Per-frame tick. Drives all state machines and periodic tasks:
        //   - Character load/unload detection (BP_FGKDwarf_C)
        //   - HISM replay queue (throttled, max 3 hides/frame)
        //   - Toolbar swap phases (resolve → stash → restore)
        //   - Quick-build state machine (open menu → find widget → select recipe)
        //   - Icon extraction pipeline (render target → export → PNG conversion)
        //   - Clear-hotbar progress, config window actions, periodic rescans
        auto on_update() -> void override
        {
            // Auto-disable spy after ~300 frames (~5s)
            if (m_spyActive)
            {
                m_spyFrameCount++;
                if (m_spyFrameCount > 300)
                {
                    m_spyActive = false;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Spy] === SPY DISABLED (timeout) ===\n"));
                    showOnScreen(L"Spy disabled", 2.0f, 0.5f, 0.5f, 0.5f);
                }
            }

            // Auto-disable spy-all after ~180 frames (~3s)
            if (m_spyAll)
            {
                m_spyAllFrameCount++;
                if (m_spyAllFrameCount > 180)
                {
                    m_spyAll = false;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SpyAll] === DISABLED (3s timeout) ===\n"));
                    showOnScreen(L"SpyAll disabled", 2.0f, 0.5f, 0.5f, 0.5f);
                }
            }

            // Old GDI+ overlay: disabled — replaced by UMG Mod Controller toolbar
            // if (m_characterLoaded && !s_overlay.thread) { startOverlay(); }
            // else if (m_characterLoaded && s_overlay.thread && !s_overlay.visible) { s_overlay.visible = true; }

            // Create all three toolbars when character loads
            {
                bool justCreated = false;
                if (m_characterLoaded && !m_mcBarWidget)
                {
                    createModControllerBar();
                    justCreated = true;
                }
                if (m_characterLoaded && !m_umgBarWidget)
                {
                    createExperimentalBar();
                    justCreated = true;
                }
                if (m_characterLoaded && !m_abBarWidget)
                {
                    createAdvancedBuilderBar();
                    justCreated = true;
                }
                if (m_characterLoaded && !m_targetInfoWidget)
                    createTargetInfoWidget();
                if (m_characterLoaded && !m_infoBoxWidget)
                    createInfoBox();
                if (m_characterLoaded && !m_configWidget)
                    createConfigWidget();
                // Hide MC + Builders bar immediately after creation (AB toggle reveals them)
                if (justCreated && !m_toolbarsVisible)
                {
                    auto hideWidget = [](UObject* w) {
                        if (!w) return;
                        auto* fn = w->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (fn) { uint8_t p[8]{}; p[0] = 1; w->ProcessEvent(fn, p); }
                    };
                    hideWidget(m_mcBarWidget);
                    hideWidget(m_umgBarWidget);
                }
            }

            // Refresh key labels when config screen changes a keybind (cross-thread flag)
            if (s_pendingKeyLabelRefresh.exchange(false))
            {
                refreshKeyLabels();
                if (m_cfgVisible) updateConfigKeyLabels();
            }

            // Target Info auto-close (10 seconds)
            if (m_tiShowTick > 0 && (GetTickCount64() - m_tiShowTick) >= 10000)
                hideTargetInfo();

            // Info Box auto-close (10 seconds)
            if (m_ibShowTick > 0 && (GetTickCount64() - m_ibShowTick) >= 10000)
                hideInfoBox();

            // Failsafe: if config is flagged visible but widget is gone, reset state
            if (m_cfgVisible && !m_configWidget)
            {
                m_cfgVisible = false;
                setInputModeGame();
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Failsafe: config widget lost, resetting state\n"));
            }

            // Config key (MC slot 7) always polled — allows toggle even when config is visible
            {
                static bool s_lastCfgKey = false;
                uint8_t cfgVk = s_bindings[MC_BIND_BASE + 7].key;
                if (cfgVk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(cfgVk) & 0x8000) != 0;
                    if (nowDown && !s_lastCfgKey)
                        toggleConfig();
                    s_lastCfgKey = nowDown;
                }
            }

            // MC keybind polling via GetAsyncKeyState — dispatch slot actions
            // Always track key state to prevent stale edges; only skip ACTIONS when config visible
            if (m_mcBarWidget)
            {
                static bool s_lastMcKey[MC_SLOTS]{};
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (i == 7) { s_lastMcKey[i] = false; continue; } // slot 7 handled by always-on config toggle
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_lastMcKey[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastMcKey[i] && !m_cfgVisible)
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[MoriaCppMod] [MC] Slot {} pressed (VK=0x{:02X})\n"), i, vk);
                        switch (i)
                        {
                        case 0: // Rotation — same as F10 handler
                        {
                            bool modDown = isModifierDown();
                            int cur = s_overlay.rotationStep;
                            int next;
                            if (modDown)
                                next = (cur <= 5) ? 90 : cur - 5;   // modifier = decrease
                            else
                                next = (cur >= 90) ? 5 : cur + 5;   // no modifier = increase
                            s_overlay.rotationStep = next;
                            s_overlay.needsUpdate = true;
                            UObject* gata = resolveGATA();
                            if (gata) setGATARotation(gata, static_cast<float>(next));
                            std::wstring msg = L"Rotation step: " + std::to_wstring(next) + L"\xB0";
                            showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
                            updateMcRotationLabel();
                            break;
                        }
                        case 1: // Target — same as F9 handler
                            if (isModifierDown())
                                buildFromTarget();
                            else if (m_tiShowTick > 0)
                                hideTargetInfo(); // toggle off if visible
                            else
                                dumpAimedActor();
                            break;
                        case 2: // Toolbar Swap — same as OEM_FIVE handler
                            swapToolbar();
                            break;
                        case 3: // ModMenu 4: no action yet
                            break;
                        case 4: // Remove Target — same as Num1 handler
                            removeAimed();
                            break;
                        case 5: // Undo Last — same as Num2 handler
                            undoLast();
                            break;
                        case 6: // Remove All — same as Num6 handler
                            removeAllOfType();
                            break;
                        case 7: // Configuration — handled separately above (always-on toggle)
                            break;
                        default:
                            break;
                        }
                    }
                    s_lastMcKey[i] = nowDown;
                }
            }

            // AB toolbar keybind polling — toggle builders bar + MC bar visibility
            // Always track key state; only skip action when config is visible
            {
                static bool s_lastAbKey = false;
                uint8_t vk = s_bindings[BIND_AB_OPEN].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbKey && !m_cfgVisible)
                    {
                        m_toolbarsVisible = !m_toolbarsVisible;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [AB] Toggle pressed — toolbars {}\n"),
                                                        m_toolbarsVisible ? STR("VISIBLE") : STR("HIDDEN"));

                        // Toggle visibility on both toolbars via SetVisibility
                        // ESlateVisibility: 0=Visible, 1=Collapsed
                        uint8_t vis = m_toolbarsVisible ? 0 : 1;
                        auto setWidgetVis = [vis](UObject* widget) {
                            if (!widget) return;
                            auto* fn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
                            if (fn)
                            {
                                uint8_t parms[8]{};
                                parms[0] = vis;
                                widget->ProcessEvent(fn, parms);
                            }
                        };
                        setWidgetVis(m_umgBarWidget);
                        setWidgetVis(m_mcBarWidget);
                    }
                    s_lastAbKey = nowDown; // always update — prevents stale edge after config closes
                }
            }

            // ── UMG Config keyboard interaction ──
            if (m_cfgVisible && m_configWidget)
            {
                // Tab switching: 1/2/3 keys
                static bool s_lastCfgTab[3]{};
                for (int t = 0; t < 3; t++)
                {
                    bool down = (GetAsyncKeyState('1' + t) & 0x8000) != 0;
                    if (down && !s_lastCfgTab[t])
                        switchConfigTab(t);
                    s_lastCfgTab[t] = down;
                }

                // Mouse click tab switching
                static bool s_captureSkipTick = false; // skip key scan one frame after entering capture mode
                static bool s_lastLMB = false;
                bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmbDown && !s_lastLMB)
                {
                    HWND gw = findGameWindow();
                    if (gw)
                    {
                        POINT cursor;
                        GetCursorPos(&cursor);
                        ScreenToClient(gw, &cursor);
                        RECT cr;
                        GetClientRect(gw, &cr);
                        int viewW = cr.right; int viewH = cr.bottom;
                        // Config widget: pos (viewW/2, viewH/2 - 100), size 1400x900, alignment (0.5,0.5)
                        int wLeft = viewW / 2 - 700;
                        int wTop  = viewH / 2 - 100 - 450;
                        // Tab bar: ~98px from top (padding 28 + title 44 + sep 26), each tab 420x66, 40px left padding
                        int tabY0 = wTop + 98, tabY1 = tabY0 + 66;
                        if (cursor.y >= tabY0 && cursor.y <= tabY1)
                        {
                            int tabX0 = wLeft + 40;
                            for (int t = 0; t < 3; t++)
                            {
                                if (cursor.x >= tabX0 + t * 420 && cursor.x < tabX0 + (t + 1) * 420)
                                {
                                    switchConfigTab(t);
                                    break;
                                }
                            }
                        }
                        // Tab 0: Free Build checkbox click — entire row (checkbox + "Free Build" + "(ON)" text)
                        // Content starts at ~190px (pad28+title44+sep26+tabs66+sep26), then secHdr ~40px
                        if (m_cfgActiveTab == 0)
                        {
                            int cbY0 = wTop + 230, cbY1 = cbY0 + 52;
                            int cbX0 = wLeft + 40, cbX1 = wLeft + 1400 - 40; // full width of content area
                            if (cursor.x >= cbX0 && cursor.x <= cbX1 && cursor.y >= cbY0 && cursor.y <= cbY1)
                            {
                                s_config.pendingToggleFreeBuild = true;
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Free Build toggle via mouse click\n"));
                            }
                            // Unlock All Recipes button: centered, 420px wide, ~340px from top
                            int ubY0 = wTop + 330, ubY1 = ubY0 + 68;
                            int ubX0 = wLeft + (1400 - 420) / 2, ubX1 = ubX0 + 420;
                            if (cursor.x >= ubX0 && cursor.x <= ubX1 && cursor.y >= ubY0 && cursor.y <= ubY1)
                            {
                                s_config.pendingUnlockAllRecipes = true;
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Unlock All Recipes via mouse click\n"));
                            }
                        }
                        // Tab 1: Key box click for rebinding
                        if (m_cfgActiveTab == 1)
                        {
                            // Key boxes are right-aligned, ~220px wide, in the right portion of the widget
                            // Wider click zone to account for scrollbar and layout variance
                            int kbX0 = wLeft + 1400 - 40 - 280; // generous hit zone
                            int kbX1 = wLeft + 1400 - 10;
                            // First key row starts after tabs+seps (~190px from top)
                            // Section headers take ~48px, key rows ~44px
                            int contentY = wTop + 190; // start of first section header
                            int rowHeight = 44;
                            int sectionHeight = 48;
                            // Get ScrollBox scroll offset to account for scrolled content
                            float scrollOff = 0.0f;
                            if (m_cfgScrollBoxes[1])
                            {
                                auto* getScrollFn = m_cfgScrollBoxes[1]->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    m_cfgScrollBoxes[1]->ProcessEvent(getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV) scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }
                            if (cursor.x >= kbX0 && cursor.x <= kbX1)
                            {
                                // Add scroll offset: screen click maps to content position + scroll
                                int y = cursor.y - contentY + static_cast<int>(scrollOff);
                                if (y >= 0)
                                {
                                    // Walk through bindings to find which row was clicked
                                    int currentY = 0;
                                    const wchar_t* lastSec = nullptr;
                                    bool bindMatched = false;
                                    for (int b = 0; b < BIND_COUNT; b++)
                                    {
                                        if (!lastSec || wcscmp(lastSec, s_bindings[b].section) != 0)
                                        {
                                            lastSec = s_bindings[b].section;
                                            currentY += sectionHeight; // section header
                                        }
                                        if (y >= currentY && y < currentY + rowHeight)
                                        {
                                            s_capturingBind = b;
                                            s_captureSkipTick = true; // skip scan this frame
                                            bindMatched = true;
                                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Capturing key for bind {}\n"), b);
                                            break;
                                        }
                                        currentY += rowHeight;
                                    }
                                    // Modifier key row is after all bindings — only check if no binding was matched
                                    if (!bindMatched && y >= currentY && y < currentY + rowHeight)
                                    {
                                        // Cycle modifier: CTRL → SHIFT → ALT → Right ALT → CTRL
                                        if (s_modifierVK == VK_CONTROL)
                                            s_modifierVK = VK_SHIFT;
                                        else if (s_modifierVK == VK_SHIFT)
                                            s_modifierVK = VK_MENU;
                                        else if (s_modifierVK == VK_MENU)
                                            s_modifierVK = VK_RMENU; // Right ALT (0xA5)
                                        else
                                            s_modifierVK = VK_CONTROL;
                                        saveKeybindings();
                                        updateConfigKeyLabels();
                                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Modifier key cycled to VK 0x{:02X}\n"), (int)s_modifierVK);
                                    }
                                }
                            }
                        }
                        // Tab 2: Danger icon click to delete removal entry
                        if (m_cfgActiveTab == 2)
                        {
                            // Danger icons are in the left 60px of the content area
                            int iconX0 = wLeft + 40, iconX1 = iconX0 + 70;
                            int entryStart = wTop + 230; // after header
                            int entryHeight = 70; // each entry row height (icon 56px + padding)
                            if (cursor.x >= iconX0 && cursor.x <= iconX1 && cursor.y >= entryStart)
                            {
                                int entryIdx = (cursor.y - entryStart) / entryHeight;
                                int count = s_config.removalCount.load();
                                if (entryIdx >= 0 && entryIdx < count)
                                {
                                    s_config.pendingRemoveIndex = entryIdx;
                                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Delete removal entry {} via mouse click\n"), entryIdx);
                                }
                            }
                        }
                    }
                }
                s_lastLMB = lmbDown;

                // Key capture for rebinding (Tab 1) — uses 0x8000 edge detection (not 0x0001 transition bit)
                // The transition bit is unreliable: UE4SS input hooks and game WndProc consume it
                // before our scan loop, causing F-keys and numpad keys to be missed.
                static bool s_captureKeyPrev[256]{};
                if (s_capturingBind >= 0 && s_capturingBind < BIND_COUNT)
                {
                    // Skip one frame after entering capture mode — just snapshot current key state
                    if (s_captureSkipTick)
                    {
                        s_captureSkipTick = false;
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                            s_captureKeyPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    }
                    else
                    {
                        // Scan for key press (rising edge: not down last frame, down now)
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                        {
                            // Skip modifier keys and mouse buttons
                            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                                vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
                                vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU)
                                continue;
                            bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            bool wasDown = s_captureKeyPrev[vk];
                            s_captureKeyPrev[vk] = nowDown;
                            if (!nowDown || wasDown) continue; // only rising edge
                            // ESC cancels capture
                            if (vk == VK_ESCAPE)
                            {
                                s_capturingBind = -1;
                                updateConfigKeyLabels();
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Key capture cancelled\n"));
                                break;
                            }
                            // Capture this key
                            int idx = s_capturingBind.load();
                            if (idx >= 0 && idx < BIND_COUNT)
                            {
                                s_bindings[idx].key = static_cast<uint8_t>(vk);
                                s_capturingBind = -1;
                                saveKeybindings();
                                updateConfigKeyLabels();
                                s_overlay.needsUpdate = true;
                                s_pendingKeyLabelRefresh = true;
                                Output::send<LogLevel::Warning>(
                                    STR("[MoriaCppMod] [CFG] Key bound: bind {} = VK 0x{:02X} ({})\n"),
                                    idx, vk, vk >= 0x70 && vk <= 0x87 ? STR("F-key") :
                                             vk >= 0x60 && vk <= 0x69 ? STR("Numpad") : STR("other"));
                            }
                            break;
                        }
                    }
                }
                else
                {
                    // ESC to close config (only when not capturing)
                    static bool s_lastCfgEsc = false;
                    bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                    if (escDown && !s_lastCfgEsc)
                        toggleConfig();
                    s_lastCfgEsc = escDown;
                }

                // Refresh key labels if capturing state changed (show yellow "Press key..." text)
                {
                    static int s_lastCapturing = -1;
                    int curCapturing = s_capturingBind.load();
                    if (curCapturing != s_lastCapturing)
                    {
                        updateConfigKeyLabels();
                        s_lastCapturing = curCapturing;
                    }
                }

                // Tab 2: refresh removal list if count changed
                if (m_cfgActiveTab == 2)
                {
                    int curCount = s_config.removalCount.load();
                    if (curCount != m_cfgLastRemovalCount)
                    {
                        rebuildRemovalList();
                    }
                }

                // T = toggle Free Build (Tab 0 only)
                if (m_cfgActiveTab == 0)
                {
                    static bool s_lastCfgT = false;
                    bool tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
                    if (tDown && !s_lastCfgT)
                    {
                        s_config.pendingToggleFreeBuild = true;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Free Build toggle requested\n"));
                    }
                    s_lastCfgT = tDown;
                }

                // U = unlock all recipes (Tab 0 only)
                if (m_cfgActiveTab == 0)
                {
                    static bool s_lastCfgU = false;
                    bool uDown = (GetAsyncKeyState('U') & 0x8000) != 0;
                    if (uDown && !s_lastCfgU)
                    {
                        s_config.pendingUnlockAllRecipes = true;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CFG] Unlock All Recipes requested\n"));
                    }
                    s_lastCfgU = uDown;
                }

                // M = cycle modifier key (Tab 1 only)
                if (m_cfgActiveTab == 1)
                {
                    static bool s_lastCfgM = false;
                    bool mDown = (GetAsyncKeyState('M') & 0x8000) != 0;
                    if (mDown && !s_lastCfgM)
                    {
                        s_modifierVK = nextModifier(s_modifierVK);
                        saveKeybindings();
                        updateConfigKeyLabels();
                    }
                    s_lastCfgM = mDown;
                }
            }

            // ── Config window: consume pending cheat toggle requests ──
            // Retry up to 120 frames (~2s) then give up with error message.
            {
                static int s_freeBuildRetries = 0;
                static int s_freeCraftRetries = 0;
                static int s_instantCraftRetries = 0;
                constexpr int MAX_RETRIES = 120;

                if (s_config.pendingToggleFreeBuild)
                {
                    if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Construction")))
                    {
                        s_config.pendingToggleFreeBuild = false;
                        syncDebugToggleState(); // read actual state instead of blind flip
                        showDebugMenuState();
                        if (m_cfgVisible) updateConfigFreeBuild();
                        s_freeBuildRetries = 0;
                    }
                    else if (++s_freeBuildRetries > MAX_RETRIES)
                    {
                        s_config.pendingToggleFreeBuild = false;
                        s_freeBuildRetries = 0;
                        showOnScreen(L"Free Build toggle failed - debug actor not found", 3.0f, 1.0f, 0.3f, 0.3f);
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Toggle Free Construction FAILED after {} retries\n"), MAX_RETRIES);
                    }
                }
                // Free Crafting / Instant Crafting handlers removed — game's debug flags are non-functional
            }
            if (s_config.pendingUnlockAllRecipes)
            {
                if (callDebugFunc(STR("BP_DebugMenu_Recipes_C"), STR("All Recipes")))
                    showOnScreen(L"ALL RECIPES UNLOCKED!", 5.0f, 0.0f, 1.0f, 0.0f);
                else
                    showOnScreen(L"Recipe debug actor not found", 3.0f, 1.0f, 0.3f, 0.3f);
                s_config.pendingUnlockAllRecipes = false;
            }

#if 0 // DISABLED: Unlock All Content, Mark All Read, Complete Tutorials, Auto-mark recipes
            // — breadcrumb/tutorial systems don't persist reliably across reloads
            if (s_config.pendingUnlockAll)
            {
                s_config.suppressTutorials = true;
                callDebugFunc(STR("BP_DebugMenu_Lore_C"), STR("All Lore"));
                completeTips();
                completeTutorials();
                s_config.pendingUnlockAll = false;
                showOnScreen(L"ALL CONTENT UNLOCKED!", 5.0f, 0.0f, 1.0f, 0.0f);
            }
            if (s_config.pendingCompleteTips)
            {
                bool anyMarked = markAllAsRead();
                int recipesMarked = markAllCraftingRecipesRead();
                s_config.markRecipesRead = true;
                if (!anyMarked && recipesMarked < 0)
                    showOnScreen(L"Recipes will auto-mark as read when crafting screen opens", 5.0f, 0.0f, 1.0f, 1.0f);
                else if (recipesMarked > 0)
                    showOnScreen((L"Marked " + std::to_wstring(recipesMarked) + L" crafting recipes as read").c_str(),
                                 5.0f, 0.0f, 1.0f, 0.0f);
                s_config.pendingCompleteTips = false;
            }
            if (s_config.pendingMarkTutorialsRead)
            {
                completeAllTutorials();
                s_config.hideTutorialHUD = true;
                {
                    std::vector<UObject*> tutDisplays;
                    UObjectGlobals::FindAllOf(STR("MorTutorialDisplay"), tutDisplays);
                    for (auto* disp : tutDisplays)
                    {
                        if (!disp || safeClassName(disp) != STR("WBP_TutorialDisplay_C")) continue;
                        UFunction* visFunc = disp->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (visFunc) { uint8_t visParms[8]{}; visParms[0] = 1; disp->ProcessEvent(visFunc, visParms); }
                    }
                    std::vector<UObject*> overlays;
                    UObjectGlobals::FindAllOf(STR("MoriaHUDWidget"), overlays);
                    for (auto* ov : overlays)
                    {
                        if (!ov || safeClassName(ov) != STR("UI_WBP_TutorialOverlay_C")) continue;
                        UFunction* visFunc = ov->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (visFunc) { uint8_t visParms[8]{}; visParms[0] = 1; ov->ProcessEvent(visFunc, visParms); }
                    }
                }
                showOnScreen(L"TUTORIALS COMPLETED & HUD HIDDEN", 5.0f, 0.0f, 1.0f, 0.0f);
                s_config.pendingMarkTutorialsRead = false;
            }
            if (s_config.markRecipesRead.load())
            {
                static bool s_lastCraftScreenOpen = false;
                static int s_craftScreenDelayFrames = 0;
                bool craftScreenOpen = false;
                {
                    std::vector<UObject*> screens;
                    UObjectGlobals::FindAllOf(STR("MorCraftingScreen"), screens);
                    for (auto* s : screens)
                    {
                        if (s && safeClassName(s).find(L"UI_WBP_Crafting_Screen") != std::wstring::npos)
                        { craftScreenOpen = true; break; }
                    }
                }
                if (craftScreenOpen && !s_lastCraftScreenOpen) s_craftScreenDelayFrames = 30;
                else if (craftScreenOpen && s_craftScreenDelayFrames > 0)
                {
                    if (--s_craftScreenDelayFrames == 0)
                    {
                        int result = markAllCraftingRecipesRead();
                        if (result > 0)
                            showOnScreen((L"Auto-marked " + std::to_wstring(result) + L" recipes as read").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
                    }
                }
                s_lastCraftScreenOpen = craftScreenOpen;
            }
#endif

            // Config UI: consume pending removal deletion from Building Options tab
            {
                int removeIdx = s_config.pendingRemoveIndex.load();
                if (removeIdx >= 0)
                {
                    RemovalEntry toRemove;
                    bool valid = false;
                    if (s_config.removalCSInit)
                    {
                        EnterCriticalSection(&s_config.removalCS);
                        if (removeIdx < static_cast<int>(s_config.removalEntries.size()))
                        {
                            toRemove = s_config.removalEntries[removeIdx];
                            valid = true;
                        }
                        LeaveCriticalSection(&s_config.removalCS);
                    }
                    if (valid)
                    {
                        if (toRemove.isTypeRule)
                        {
                            m_typeRemovals.erase(toRemove.meshName);
                        }
                        else
                        {
                            for (size_t i = 0; i < m_savedRemovals.size(); i++)
                            {
                                if (m_savedRemovals[i].meshName == toRemove.meshName)
                                {
                                    float dx = m_savedRemovals[i].posX - toRemove.posX;
                                    float dy = m_savedRemovals[i].posY - toRemove.posY;
                                    float dz = m_savedRemovals[i].posZ - toRemove.posZ;
                                    if (dx * dx + dy * dy + dz * dz < POS_TOLERANCE * POS_TOLERANCE)
                                    {
                                        m_savedRemovals.erase(m_savedRemovals.begin() + i);
                                        if (i < m_appliedRemovals.size())
                                            m_appliedRemovals.erase(m_appliedRemovals.begin() + i);
                                        break;
                                    }
                                }
                            }
                        }
                        rewriteSaveFile();
                        buildRemovalEntries();
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Config UI: removed entry {} ({})\n"),
                                                        removeIdx,
                                                        std::wstring(toRemove.friendlyName));
                    }
                    s_config.pendingRemoveIndex = -1;
                }
            }

            // Detect build menu close → refresh ActionBar (fixes stale hotbar display)
            // Only runs while we're tracking a quickbuild/target-build menu session
            if (m_buildMenuWasOpen)
            {
                UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
                if (!buildTab)
                {
                    m_buildMenuWasOpen = false;
                    refreshActionBar();
                }
            }

            // Handle pending quick-build: state machine with B-key retry
            // Phase 1 (frames < 0): waiting for menu to close after B key toggle
            // Phase 2 (frame 0): send B key to open menu
            // Phase 3 (frames 1-14): waiting for menu to become visible
            // Phase 4 (frame 15+): check visibility. If not found by frame 45, retry B key
            if (m_pendingQuickBuildSlot >= 0)
            {
                m_pendingBuildFrames++;
                if (m_pendingBuildFrames == 0)
                {
                    // Send B key to (re)open build menu
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] SM: frame 0 — sending B key to open menu\n"));
                    keybd_event(0x42, 0, 0, 0);
                    keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                }
                else if (m_pendingBuildFrames == 45)
                {
                    // Retry: if tab still not visible, maybe first B closed the menu — send B again
                    UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
                    if (!buildTab)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] SM: frame 45 — tab NOT visible, RETRYING B key\n"));
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                    }
                }
                else if (m_pendingBuildFrames > 150)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] SM: TIMEOUT at frame {} — build tab never became visible\n"),
                                                    m_pendingBuildFrames);
                    showOnScreen(L"Build menu didn't open (timeout)", 3.0f, 1.0f, 0.3f, 0.0f);
                    m_pendingQuickBuildSlot = -1;
                }
                else if (m_pendingBuildFrames >= 15)
                {
                    UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
                    if (buildTab)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] SM: build tab found at frame {} — selecting recipe\n"),
                                                        m_pendingBuildFrames);
                        selectRecipeOnBuildTab(buildTab, m_pendingQuickBuildSlot);
                        // Reset total rotation for new build placement
                        s_overlay.totalRotation = 0;
                        s_overlay.needsUpdate = true;
                        updateMcRotationLabel();
                        m_pendingQuickBuildSlot = -1;
                    }
                }
            }

            // Toolbar swap state machine: one item per tick
            swapToolbarTick();

            // Win32 swap-key fallback removed — MC polling handles all keybinds now

            // ── Pending target-build state machine (Shift+F10 → build from targeted actor) ──
            if (m_pendingTargetBuild)
            {
                m_pendingTargetBuildFrames++;
                if (m_pendingTargetBuildFrames == 0)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] SM: frame 0 — sending B key to open menu\n"));
                    keybd_event(0x42, 0, 0, 0);
                    keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                }
                else if (m_pendingTargetBuildFrames == 45)
                {
                    // Retry: if tab still not visible, maybe first B closed the menu — send B again
                    UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
                    if (!buildTab)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] SM: frame 45 — tab NOT visible, RETRYING B key\n"));
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                    }
                }
                else if (m_pendingTargetBuildFrames > 150)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] SM: TIMEOUT at frame {} — build tab never became visible\n"),
                                                    m_pendingTargetBuildFrames);
                    showOnScreen(L"Build menu didn't open (timeout)", 3.0f, 1.0f, 0.3f, 0.0f);
                    m_pendingTargetBuild = false;
                }
                else if (m_pendingTargetBuildFrames >= 15)
                {
                    UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", true);
                    if (buildTab)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [TargetBuild] SM: build tab found at frame {} — selecting recipe\n"),
                                                        m_pendingTargetBuildFrames);
                        selectRecipeByTargetName(buildTab);
                        // Reset total rotation for new build placement
                        s_overlay.totalRotation = 0;
                        s_overlay.needsUpdate = true;
                        updateMcRotationLabel();
                        m_pendingTargetBuild = false;
                    }
                }
            }

            if (!m_replayActive) return;
            m_frameCounter++;
            m_rescanCounter++;

            // Detect world switch: if character was loaded but disappears, reset for new world
            if (m_characterLoaded && m_frameCounter % 60 == 0)
            {
                std::vector<UObject*> dwarves;
                UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
                if (dwarves.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Character lost — world unloading, resetting replay state\n"));
                    m_characterLoaded = false;
                    s_overlay.visible = false; // hide overlay until character reloads
                    m_initialReplayDone = false;
                    m_processedComps.clear();
                    m_undoStack.clear();
                    m_stuckLogCount = 0;
                    m_rescanCounter = 0;
                    m_chatWidget = nullptr;
                    m_sysMessages = nullptr;
                    m_replay = {}; // stop any active replay
                    // Reset all applied flags so replay re-runs for new world
                    m_appliedRemovals.assign(m_appliedRemovals.size(), false);
                    // Clear swap state — handles become stale on world unload
                    m_bodyInvHandle.clear();
                    m_bodyInvHandles.clear();
                    m_bagHandle.clear();
                    m_ihfCDO = nullptr;
                    m_dropItemMgr = nullptr;
                    // UMG builders bar destroyed with world
                    m_umgBarWidget = nullptr;
                    for (int i = 0; i < 8; i++)
                    {
                        m_umgStateImages[i] = nullptr;
                        m_umgIconImages[i] = nullptr;
                        m_umgIconTextures[i] = nullptr;
                        m_umgIconNames[i].clear();
                        m_umgSlotStates[i] = UmgSlotState::Empty;
                        m_umgKeyLabels[i] = nullptr;
                        m_umgKeyBgImages[i] = nullptr;
                    }
                    m_activeBuilderSlot = -1;
                    m_umgSetBrushFn = nullptr;
                    m_umgTexEmpty = nullptr;
                    m_umgTexInactive = nullptr;
                    m_umgTexActive = nullptr;
                    m_umgTexBlankRect = nullptr;
                    // Mod Controller toolbar destroyed with world
                    m_mcBarWidget = nullptr;
                    for (int i = 0; i < MC_SLOTS; i++)
                    {
                        m_mcStateImages[i] = nullptr;
                        m_mcIconImages[i] = nullptr;
                        m_mcSlotStates[i] = UmgSlotState::Empty;
                        m_mcKeyLabels[i] = nullptr;
                        m_mcKeyBgImages[i] = nullptr;
                    }
                    m_mcRotationLabel = nullptr;
                    m_mcSlot0Overlay = nullptr;
                    m_mcSlot4Overlay = nullptr;
                    m_mcSlot6Overlay = nullptr;
                    // Advanced Builder toolbar destroyed with world
                    m_abBarWidget = nullptr;
                    m_abKeyLabel = nullptr;
                    m_toolbarsVisible = false;
                    // Target Info + Info Box destroyed with world
                    m_targetInfoWidget = nullptr;
                    m_tiTitleLabel = nullptr;
                    m_tiClassLabel = nullptr;
                    m_tiNameLabel = nullptr;
                    m_tiDisplayLabel = nullptr;
                    m_tiPathLabel = nullptr;
                    m_tiBuildLabel = nullptr;
                    m_tiRecipeLabel = nullptr;
                    m_tiShowTick = 0;
                    m_infoBoxWidget = nullptr;
                    m_ibTitleLabel = nullptr;
                    m_ibMessageLabel = nullptr;
                    m_ibShowTick = 0;
                    // Config Menu destroyed with world
                    m_configWidget = nullptr;
                    m_cfgTabLabels[0] = m_cfgTabLabels[1] = m_cfgTabLabels[2] = nullptr;
                    m_cfgTabContent[0] = m_cfgTabContent[1] = m_cfgTabContent[2] = nullptr;
                    m_cfgTabImages[0] = m_cfgTabImages[1] = m_cfgTabImages[2] = nullptr;
                    m_cfgTabActiveTexture = nullptr;
                    m_cfgTabInactiveTexture = nullptr;
                    m_cfgVignetteImage = nullptr;
                    m_cfgScrollBoxes[0] = m_cfgScrollBoxes[1] = m_cfgScrollBoxes[2] = nullptr;
                    m_cfgActiveTab = 0;
                    m_cfgVisible = false;
                    m_cfgFreeBuildLabel = nullptr;
                    m_cfgFreeBuildCheckImg = nullptr;
                    m_cfgUnlockBtnImg = nullptr;
                    for (int i = 0; i < BIND_COUNT; i++) m_cfgKeyValueLabels[i] = nullptr;
                    for (int i = 0; i < BIND_COUNT; i++) m_cfgKeyBoxLabels[i] = nullptr;
                    m_cfgModifierLabel = nullptr;
                    m_cfgModBoxLabel = nullptr;
                    m_cfgRemovalHeader = nullptr;
                    m_cfgRemovalVBox = nullptr;
                    m_cfgLastRemovalCount = -1;
                    m_swap = {};
                    m_activeToolbar = 0;
                    s_overlay.activeToolbar = 0;
                    // Reset cheat toggle states — debug menu actors are destroyed on unload
                    s_config.freeBuild = false;
                    s_config.freeCraft = false;
                    s_config.instantCraft = false;
                    s_config.pendingToggleFreeBuild = false;
                    s_config.pendingToggleFreeCraft = false;
                    s_config.pendingToggleInstantCraft = false;
                    s_config.pendingUnlockAllRecipes = false;
                    s_config.pendingCompleteTips = false;
                    s_config.pendingUnlockAll = false;
                    s_config.pendingMarkTutorialsRead = false;
                    s_config.suppressTutorials = false;
                    s_config.internalTutorialCall = false;
                    s_config.savedTutorialTable = nullptr;
                    // Hide target info popup (now UMG)
                    m_tiShowTick = 0; // will be hidden on next tick or already destroyed
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Cleared all container handles, swap state, and cheat toggles\n"));
                }
            }

            // Storage patch disabled — bag-as-intermediary swap doesn't need it
            // if (!m_storagePatched && m_frameCounter % 60 == 1) {
            //     patchBodyInventoryStorage();
            // }

            if (!m_characterLoaded)
            {
                if (m_frameCounter % 30 == 0)
                { // check every ~0.5s
                    std::vector<UObject*> dwarves;
                    UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
                    if (!dwarves.empty())
                    {
                        m_characterLoaded = true;
                        m_charLoadFrame = m_frameCounter;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Character loaded at frame {} — waiting 15s before replay\n"), m_frameCounter);
                    }
                }
                return; // don't do anything until character exists
            }

            int framesSinceChar = m_frameCounter - m_charLoadFrame;

            // Auto-scan containers: retry every ~2s after initial 5s delay, give up after ~60s
            if (m_bodyInvHandle.empty() && framesSinceChar > 300 && framesSinceChar < 3900 && framesSinceChar % 120 == 0)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Container scan attempt (frame {}). bodyInvHandle.empty={} handles.size={}\n"),
                                                m_frameCounter,
                                                m_bodyInvHandle.empty(),
                                                m_bodyInvHandles.size());
                UObject* playerChar = nullptr;
                {
                    std::vector<UObject*> actors;
                    UObjectGlobals::FindAllOf(STR("Character"), actors);
                    for (auto* a : actors)
                    {
                        if (a && safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                        {
                            playerChar = a;
                            break;
                        }
                    }
                }
                if (playerChar)
                {
                    auto* invComp = findPlayerInventoryComponent(playerChar);
                    if (invComp)
                    {
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === Auto-scan containers (retry) ===\n"));
                        discoverBagHandle(invComp);
                        if (!m_bodyInvHandle.empty())
                        {
                            showOnScreen(L"Containers discovered!", 3.0f, 0.0f, 1.0f, 0.0f);
                        }
                    }
                }
            }

            // Log failure if container scan times out after ~60s
            if (m_bodyInvHandle.empty() && framesSinceChar == 3900)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Container discovery FAILED after 60s — toolbar swap unavailable this session\n"));
                showOnScreen(L"Container discovery failed - swap unavailable", 5.0f, 1.0f, 0.3f, 0.0f);
            }

            // Initial replay 15 seconds after character load (~900 frames at 60fps)
            // Extra delay to let streaming settle before modifying instance buffers
            static constexpr int INITIAL_DELAY = 900;
            if (!m_initialReplayDone && framesSinceChar == INITIAL_DELAY)
            {
                m_initialReplayDone = true;
                if (!m_savedRemovals.empty() || !m_typeRemovals.empty())
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Starting initial replay (15s after char load)...\n"));
                    startReplay();
                }

                // Sync debug toggle state from the actual debug menu actor
                syncDebugToggleState();
            }

            // Process throttled replay batch (max MAX_HIDES_PER_FRAME per frame)
            if (m_replay.active)
            {
                processReplayBatch();
            }

            // Check for newly-streamed components every ~3s (after initial replay, when not already replaying)
            if (m_initialReplayDone && !m_replay.active && m_frameCounter % STREAM_CHECK_INTERVAL == 0)
            {
                checkForNewComponents();
            }

            // Periodic full rescan every ~60s while there are pending removals
            static constexpr int RESCAN_INTERVAL = 3600; // ~60s at 60fps
            if (m_initialReplayDone && !m_replay.active && m_rescanCounter >= RESCAN_INTERVAL && hasPendingRemovals())
            {
                m_rescanCounter = 0;
                int pending = pendingCount();
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Periodic rescan ({} pending)...\n"), pending);
                m_processedComps.clear();
                startReplay();
                if (m_stuckLogCount == 0 && pending > 0)
                {
                    m_stuckLogCount++;
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === Pending entries ({}) ===\n"), pending);
                    for (size_t i = 0; i < m_savedRemovals.size(); i++)
                    {
                        if (m_appliedRemovals[i]) continue;
                        std::wstring meshW(m_savedRemovals[i].meshName.begin(), m_savedRemovals[i].meshName.end());
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod]   PENDING [{}]: {} @ ({:.1f},{:.1f},{:.1f})\n"),
                                                        i,
                                                        meshW,
                                                        m_savedRemovals[i].posX,
                                                        m_savedRemovals[i].posY,
                                                        m_savedRemovals[i].posZ);
                    }
                }
            }
        }
    };
} // namespace MoriaMods

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT RC::CppUserModBase* start_mod()
    {
        return new MoriaMods::MoriaCppMod();
    }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
