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
        std::atomic<int> activeToolbar{0};          // which toolbar is visible (0/1/2) — shown in F12 slot
    };
    static OverlayState s_overlay;

    // ════════════════════════════════════════════════════════════════════════════
    // Section 3: Keybinding System & Window Discovery
    //   Rebindable F-key assignments, VK code → string conversion,
    //   findGameWindow() for overlay positioning
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr int BIND_COUNT = 12;
    struct KeyBind
    {
        const wchar_t* label;
        const wchar_t* section;
        uint8_t key; // Input::Key value (same as VK code)
    };
    static KeyBind s_bindings[BIND_COUNT] = {
            {L"Quick Build 1", L"Quick Building", Input::Key::F1}, // 0
            {L"Quick Build 2", L"Quick Building", Input::Key::F2}, // 1
            {L"Quick Build 3", L"Quick Building", Input::Key::F3}, // 2
            {L"Quick Build 4", L"Quick Building", Input::Key::F4}, // 3
            {L"Quick Build 5", L"Quick Building", Input::Key::F5}, // 4
            {L"Quick Build 6", L"Quick Building", Input::Key::F6}, // 5
            {L"Quick Build 7", L"Quick Building", Input::Key::F7}, // 6
            {L"Quick Build 8", L"Quick Building", Input::Key::F8}, // 7
            {L"Rotation", L"Quick Building", Input::Key::F9},      // 8
            {L"Target", L"Quick Building", Input::Key::F10},       // 9
            {L"Configuration", L"Misc", Input::Key::F12},          // 10
            {L"Toolbar Swap", L"Misc", Input::Key::OEM_FIVE},      // 11
    };
    static std::atomic<int> s_capturingBind{-1};

    static std::wstring keyName(uint8_t vk)
    {
        if (vk >= 0x70 && vk <= 0x7B)
        { // F1-F12
            wchar_t buf[8];
            swprintf_s(buf, L"F%d", vk - 0x70 + 1);
            return buf;
        }
        switch (vk)
        {
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

                // Slot 8 (Rotation): show rotation step value
                if (i == 8)
                {
                    int rotVal = s_overlay.rotationStep;
                    std::wstring rotStr = std::to_wstring(rotVal) + L"\xB0";
                    float rotFontSz = slotSize * 0.35f;
                    Gdiplus::Font rotFont(&fontFamily, rotFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush rotBrush(Gdiplus::Color(220, 180, 210, 255));
                    Gdiplus::RectF rotRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(rotStr.c_str(), -1, &rotFont, rotRect, &centerFmt, &rotBrush);
                }

                // Slot 9 (Target): archery target icon + TGT text
                if (i == 9)
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

                // Key label below slot — pulled from s_bindings
                // Slot mapping: 0-8=bindings[0-8], 9=bindings[9](Target), 10=bindings[11](Swap), 11=bindings[10](Config)
                std::wstring fLabel;
                if (i <= 9)
                {
                    fLabel = keyName(s_bindings[i].key); // Quick Build 1-8 + Rotation + Target
                }
                else if (i == 10)
                {
                    fLabel = keyName(s_bindings[11].key); // Toolbar Swap
                }
                else if (i == 11)
                {
                    fLabel = keyName(s_bindings[10].key); // Configuration
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
    static const wchar_t* CONFIG_TAB_NAMES[CONFIG_TAB_COUNT] = {L"Building Options", L"Optional Mods", L"Key Mapping"};

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

        // Scrollbar state
        int scrollY{0};
        int contentHeight{0}; // total logical content height
        int visibleHeight{0}; // visible content area height
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
                return 0;
            }
            break;
        case WM_TIMER:
            renderTargetInfo(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
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
            gfx.DrawString(L"MoriaCppMod Config", -1, &titleFont, titleRect, &leftFmt, &titleBrush);

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

            if (s_config.activeTab == 2)
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
                gfx.DrawString(L"SHIFT", -1, &rowFont, modKeyTextRect, &centerFmt, &keyBrush);
                cy += rowH;
            }
            else if (s_config.activeTab == 1)
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
                        {L"Free Crafting", L"Craft without materials", s_config.freeCraft},
                        {L"Instant Crafting", L"Crafting completes instantly", s_config.instantCraft},
                };

                // Section header
                Gdiplus::RectF secRect((float)pad, (float)cy, (float)(configW - pad * 2 - scrollbarW), (float)rowH);
                gfx.DrawString(L"Cheat Toggles", -1, &sectionFont, secRect, &leftFmt, &sectionBrush);
                cy += rowH;
                gfx.DrawLine(&sepPen, pad, cy - static_cast<int>(4 * scale), configW - pad - scrollbarW, cy - static_cast<int>(4 * scale));

                float descFontSz = 10.0f * scale;
                Gdiplus::Font descFont(&fontFamily, descFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

                for (int ti = 0; ti < 3; ti++)
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
            }
            else
            {
                // Building Options tab: placeholder
                Gdiplus::RectF contentRect((float)pad, (float)(cy), (float)(configW - pad * 2 - scrollbarW), (float)(configH - cy - pad));
                std::wstring placeholder = std::wstring(L"Tab: ") + CONFIG_TAB_NAMES[s_config.activeTab] + L"\n\n(Content coming soon)";
                gfx.DrawString(placeholder.c_str(), -1, &rowFont, contentRect, &leftFmt, &dimBrush);
                cy += static_cast<int>(60 * scale);
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

            // Hit-test key boxes on Key Mapping tab (index 2)
            if (s_config.activeTab == 2 && my > contentY)
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
                s_capturingBind = -1;
                renderConfig(hwnd);
            }

            // Hit-test toggles and button on Optional Mods tab (index 1)
            if (s_config.activeTab == 1 && my > contentY)
            {
                int rowH = static_cast<int>(22 * scale);
                int toggleW = static_cast<int>(44 * scale);
                int toggleH = static_cast<int>(20 * scale);
                int toggleX = configW - pad - toggleW - scrollbarW;

                int cy = contentY + static_cast<int>(10 * scale);
                cy += rowH; // section header

                for (int ti = 0; ti < 3; ti++)
                {
                    int toggleY = cy + (rowH - toggleH) / 2;
                    if (mx >= toggleX && mx <= toggleX + toggleW && logicalMy >= toggleY && logicalMy <= toggleY + toggleH)
                    {
                        if (ti == 0)
                            s_config.pendingToggleFreeBuild = true;
                        else if (ti == 1)
                            s_config.pendingToggleFreeCraft = true;
                        else if (ti == 2)
                            s_config.pendingToggleInstantCraft = true;
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
                // Persist to disk
                {
                    std::ofstream kf("Mods/MoriaCppMod/keybindings.txt", std::ios::trunc);
                    if (kf.is_open())
                    {
                        kf << "# MoriaCppMod keybindings (index|VK_code)\n";
                        for (int bi = 0; bi < BIND_COUNT; bi++)
                            kf << bi << "|" << (int)s_bindings[bi].key << "\n";
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

        // ── Test all display methods (Num5) ──

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

        void removeAimed()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(L"No hit", 2.0f, 1.0f, 0.3f, 0.3f);
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
            showOnScreen(L"REMOVED " + std::to_wstring(hiddenCount) + L"x: " + meshIdW, 3.0f, 0.0f, 1.0f, 0.0f);
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
                showOnScreen(L"No hit", 2.0f, 1.0f, 0.3f, 0.3f);
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
            showOnScreen(L"TYPE RULE: " + meshIdW + L" (" + std::to_wstring(hidden) + L" hidden)", 5.0f, 1.0f, 0.5f, 0.0f);
            showGameMessage(L"[Mod] Type rule: " + meshIdW + L" (" + std::to_wstring(hidden) + L" hidden)");
        }

        // ── Building / UI Exploration (Num7/Num8/Num9) ──

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

            // Show target info popup window (now with DT_Constructions display name)
            showTargetInfo(actorName, displayName, assetPath, actorClassName, isBuildable, recipeRef, dtRowName);

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
        }

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

        // ── Inventory/Hotbar Probe: find items in the bottom-center action bar ──
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
        // Calls a named 0-param function on a debug menu actor
        bool callDebugFunc(const wchar_t* actorClass, const wchar_t* funcName)
        {
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
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Called {}::{}\n"), cls, std::wstring(funcName));
                        return true;
                    }
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Function {} not found on {}\n"), std::wstring(funcName), cls);
                    return false;
                }
            }
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Actor {} not found\n"), std::wstring(actorClass));
            return false;
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
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Saved {} keybindings to disk\n"), BIND_COUNT);
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

            // Safety: only assign if build menu is open and a recipe was captured
            if (!m_hasLastCapture || m_lastCapturedName.empty())
            {
                // Don't show message or do anything — silently ignore
                return;
            }
            // Verify build menu is actually open (widgets exist)
            UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
            if (!buildTab)
            {
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

            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [QuickBuild] ASSIGN F{} = '{}'\n"), slot + 1, m_lastCapturedName);

            std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" = " + m_lastCapturedName;
            showOnScreen(msg.c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
        }

        // ── Deep probe: dump Icon image data from selected Build_Item_Medium widget ──

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
        }

        void quickBuildSlot(int slot)
        {
            if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return; // F1-F8 only

            if (!m_recipeSlots[slot].used)
            {
                std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" empty \x2014 select recipe in B menu, then Shift+F" + std::to_wstring(slot + 1);
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

        void toggleConfig()
        {
            if (!s_config.thread)
            {
                startConfig();
            }
            else if (s_config.visible)
            {
                s_config.visible = false;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Config window hidden\n"));
            }
            else
            {
                s_config.visible = true;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Config window shown\n"));
            }
        }

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

        void showTargetInfo(const std::wstring& name,
                            const std::wstring& display,
                            const std::wstring& path,
                            const std::wstring& cls,
                            bool buildable = false,
                            const std::wstring& recipe = L"",
                            const std::wstring& rowName = L"")
        {
            if (!s_targetInfo.csInit)
            {
                InitializeCriticalSection(&s_targetInfo.dataCS);
                s_targetInfo.csInit = true;
            }
            EnterCriticalSection(&s_targetInfo.dataCS);
            s_targetInfo.actorName = name;
            s_targetInfo.displayName = display;
            s_targetInfo.assetPath = path;
            s_targetInfo.actorClass = cls;
            s_targetInfo.buildable = buildable;
            s_targetInfo.recipeRef = recipe;
            s_targetInfo.rowName = rowName;
            LeaveCriticalSection(&s_targetInfo.dataCS);

            if (!s_targetInfo.thread)
            {
                startTargetInfo();
            }
            else
            {
                s_targetInfo.visible = true;
            }
        }

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
                showOnScreen(L"Nothing to undo", 2.0f, 1.0f, 0.5f, 0.0f);
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

                std::wstring meshIdW(meshId.begin(), meshId.end());
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Undo type rule: {} — restored {} instances\n"), meshIdW, restored);
                showOnScreen(L"Undo type: " + meshIdW + L" (" + std::to_wstring(restored) + L" restored)", 3.0f, 0.5f, 0.5f, 1.0f);
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
            if (foundInSave) rewriteSaveFile();

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
            showOnScreen(L"Restored | " + std::to_wstring(m_savedRemovals.size()) + L" saved", 3.0f, 0.5f, 0.5f, 1.0f);

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
            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Loaded v1.3\n"));
        }

        ~MoriaCppMod() override
        {
            stopOverlay();
            stopConfig();
            stopTargetInfo();
            s_instance = nullptr;
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
            probePrintString();
            loadQuickBuildSlots();
            loadKeybindings();

            register_keydown_event(Input::Key::NUM_ZERO, [this]() {
                int pending = pendingCount();
                int applied = static_cast<int>(m_savedRemovals.size()) - pending;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Status: {} saved ({} applied, {} pending), {} undo, {} type rules\n"),
                                                m_savedRemovals.size(),
                                                applied,
                                                pending,
                                                m_undoStack.size(),
                                                m_typeRemovals.size());
                showOnScreen(L"Status: " + std::to_wstring(m_savedRemovals.size()) + L" saved (" + std::to_wstring(applied) + L" applied, " +
                                     std::to_wstring(pending) + L" pending), " + std::to_wstring(m_undoStack.size()) + L" undo",
                             5.0f);
                showGameMessage(L"[Mod] " + std::to_wstring(applied) + L"/" + std::to_wstring(m_savedRemovals.size()) + L" applied, " +
                                std::to_wstring(pending) + L" pending");
            });

            register_keydown_event(Input::Key::NUM_ONE, [this]() {
                removeAimed();
            });
            register_keydown_event(Input::Key::NUM_TWO, [this]() {
                undoLast();
            });

            register_keydown_event(Input::Key::NUM_THREE, [this]() {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Forcing full replay (throttled)...\n"));
                m_processedComps.clear();
                m_appliedRemovals.assign(m_appliedRemovals.size(), false);
                m_replay = {}; // reset any in-progress replay
                startReplay();
                showOnScreen(L"Replay started (throttled)", 3.0f, 0.0f, 0.8f, 1.0f);
            });

            register_keydown_event(Input::Key::NUM_FOUR, [this]() {
                inspectAimed();
            });

            register_keydown_event(Input::Key::NUM_FIVE, [this]() {
                testAllDisplayMethods();
            });
            register_keydown_event(Input::Key::NUM_SIX, [this]() {
                removeAllOfType();
            });

            // Building exploration keys
            register_keydown_event(Input::Key::NUM_SEVEN, [this]() {
                dumpAllWidgets();
            });
            register_keydown_event(Input::Key::NUM_EIGHT, [this]() {
                dumpAimedActor();
            });
            register_keydown_event(Input::Key::NUM_NINE, [this]() {
                dumpBuildCraftClasses();
            });

            // Inventory probe (Num.)
            register_keydown_event(Input::Key::DECIMAL, [this]() {
                probeInventoryHotbar();
            });

            // Clear hotbar (Alt+Num.) — move hotbar items to inventory or drop
            register_keydown_event(Input::Key::DECIMAL, {Input::ModifierKey::ALT}, [this]() {
                clearHotbar();
            });

            // Deep probes (Alt+Numpad)
            register_keydown_event(Input::Key::NUM_FIVE, {Input::ModifierKey::ALT}, [this]() {
                startRotationSpy();
            });
            register_keydown_event(Input::Key::NUM_SEVEN, {Input::ModifierKey::ALT}, [this]() {
                probeBuildTabRecipe();
            });
            register_keydown_event(Input::Key::NUM_EIGHT, {Input::ModifierKey::ALT}, [this]() {
                probeBuildConstruction();
            });
            register_keydown_event(Input::Key::NUM_NINE, {Input::ModifierKey::ALT}, [this]() {
                dumpDebugMenus();
            });

            // SPY ALL: Insert key — captures ALL ProcessEvent calls for 3 seconds
            // Use this to discover what function the B key triggers to open build menu
            register_keydown_event(Input::Key::INS, [this]() {
                if (m_spyAll)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SpyAll] Already active\n"));
                    return;
                }
                m_spyAll = true;
                m_spyAllFrameCount = 0;
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [SpyAll] === ALL ProcessEvent logging for 3s — press B NOW! ===\n"));
                showOnScreen(L"SPY ALL ACTIVE (3s) \x2014 press B now!", 3.0f, 1.0f, 1.0f, 0.0f);
            });

            // Deep probe: dump all data on selected build recipe
            register_keydown_event(Input::Key::DIVIDE, [this]() {
                probeSelectedRecipe();
            });

            // Debug cheats (Alt+Numpad)
            register_keydown_event(Input::Key::NUM_ZERO, {Input::ModifierKey::ALT}, [this]() {
                showDebugMenuState();
            });
            register_keydown_event(Input::Key::NUM_ONE, {Input::ModifierKey::ALT}, [this]() {
                toggleFreeConstruction();
            });
            register_keydown_event(Input::Key::NUM_TWO, {Input::ModifierKey::ALT}, [this]() {
                toggleFreeCrafting();
            });
            register_keydown_event(Input::Key::NUM_THREE, {Input::ModifierKey::ALT}, [this]() {
                toggleInstantCrafting();
            });
            register_keydown_event(Input::Key::NUM_FOUR, {Input::ModifierKey::ALT}, [this]() {
                rotateBuildPlacement();
            });
            register_keydown_event(Input::Key::NUM_SIX, {Input::ModifierKey::ALT}, [this]() {
                unlockAllRecipes();
            });

            // Quick-build hotbar: F1-F12 = build, Shift+F1-F12 = assign slot
            // Slots persist to disk via display names (stable across sessions)

            const Input::Key fkeys[] = {Input::Key::F1, Input::Key::F2, Input::Key::F3, Input::Key::F4, Input::Key::F5, Input::Key::F6, Input::Key::F7, Input::Key::F8};
            for (int i = 0; i < 8; i++)
            { // F1-F8 for quickbuild
                register_keydown_event(fkeys[i], [this, i]() {
                    quickBuildSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::SHIFT}, [this, i]() {
                    assignRecipeSlot(i);
                });
            }

            // \ (backslash): toggle between 2 toolbars
            register_keydown_event(Input::Key::OEM_FIVE, [this]() {
                swapToolbar();
            });

            // F12: toggle config window
            register_keydown_event(Input::Key::F12, [this]() {
                toggleConfig();
            });

            // F9: increase rotation step by 5 (wraps 90 → 5)
            register_keydown_event(Input::Key::F9, [this]() {
                int cur = s_overlay.rotationStep;
                int next = (cur >= 90) ? 5 : cur + 5;
                s_overlay.rotationStep = next;
                s_overlay.needsUpdate = true;
                UObject* gata = resolveGATA();
                if (gata) setGATARotation(gata, static_cast<float>(next));
                std::wstring msg = L"Rotation step: " + std::to_wstring(next) + L"\xB0";
                showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            });

            // Shift+F9: decrease rotation step by 5 (wraps 5 → 90)
            register_keydown_event(Input::Key::F9, {Input::ModifierKey::SHIFT}, [this]() {
                int cur = s_overlay.rotationStep;
                int next = (cur <= 5) ? 90 : cur - 5;
                s_overlay.rotationStep = next;
                s_overlay.needsUpdate = true;
                UObject* gata = resolveGATA();
                if (gata) setGATARotation(gata, static_cast<float>(next));
                std::wstring msg = L"Rotation step: " + std::to_wstring(next) + L"\xB0";
                showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            });

            // F10: Target (dump aimed actor — deep inspect)
            register_keydown_event(Input::Key::F10, [this]() {
                dumpAimedActor();
            });

            // Shift+F10: Build the last targeted buildable object
            register_keydown_event(Input::Key::F10, {Input::ModifierKey::SHIFT}, [this]() {
                buildFromTarget();
            });

            // Hotbar overlay toggle: Numpad * (Multiply)
            register_keydown_event(Input::Key::MULTIPLY, [this]() {
                m_showHotbar = !m_showHotbar;
                s_overlay.visible = m_showHotbar;
                s_overlay.needsUpdate = true;
                showOnScreen(m_showHotbar ? L"Hotbar overlay ON" : L"Hotbar overlay OFF", 2.0f, 0.2f, 0.8f, 1.0f);
            });

            // Spy mode: capture ProcessEvent calls with rotation/build in the function name
            s_instance = this;
            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;

                // Intercept RotatePressed on BuildHUD: set GATA rotation step from overlay setting
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
                                const float step = static_cast<float>(s_overlay.rotationStep);
                                s_instance->setGATARotation(gata, step);
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

            // Overlay: start if not running yet, or re-show after character reload
            if (m_characterLoaded && !s_overlay.thread)
            {
                startOverlay();
            }
            else if (m_characterLoaded && s_overlay.thread && !s_overlay.visible)
            {
                s_overlay.visible = true; // character reloaded, show overlay again
            }

            // ── Config window: consume pending cheat toggle requests ──
            // Only flip UI state if callDebugFunc succeeds. If actor not found,
            // keep the pending flag set so it retries next frame automatically.
            if (s_config.pendingToggleFreeBuild)
            {
                if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Construction")))
                {
                    s_config.pendingToggleFreeBuild = false;
                    s_config.freeBuild = !s_config.freeBuild;
                    showDebugMenuState();
                }
            }
            if (s_config.pendingToggleFreeCraft)
            {
                if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Crafting")))
                {
                    s_config.pendingToggleFreeCraft = false;
                    s_config.freeCraft = !s_config.freeCraft;
                    showDebugMenuState();
                }
            }
            if (s_config.pendingToggleInstantCraft)
            {
                if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Instant Crafting")))
                {
                    s_config.pendingToggleInstantCraft = false;
                    s_config.instantCraft = !s_config.instantCraft;
                    showDebugMenuState();
                }
            }
            if (s_config.pendingUnlockAllRecipes)
            {
                if (callDebugFunc(STR("BP_DebugMenu_Recipes_C"), STR("All Recipes")))
                {
                    s_config.pendingUnlockAllRecipes = false;
                    showOnScreen(L"ALL RECIPES UNLOCKED!", 5.0f, 0.0f, 1.0f, 0.0f);
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
                        m_pendingQuickBuildSlot = -1;
                    }
                }
            }

            // Clear-hotbar state machine: one item per tick
            clearHotbarTick();

            // Toolbar swap state machine: one item per tick
            swapToolbarTick();

            // ── Win32 fallback: poll toolbar swap key via GetAsyncKeyState ──
            // The UE4SS register_keydown_event may not fire for some keys.
            // This polls the ACTUAL bound key (from s_bindings[11]) as a functional fallback.
            {
                static bool s_lastSwapKey = false;
                uint8_t swapVK = s_bindings[11].key; // respects user rebinding (e.g. PgDn = 0x22)
                bool nowDown = (GetAsyncKeyState(swapVK) & 0x8000) != 0;
                if (nowDown && !s_lastSwapKey)
                {
                    Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [Swap] Key 0x{:02X} detected via GetAsyncKeyState fallback\n"), swapVK);
                    swapToolbar();
                }
                s_lastSwapKey = nowDown;
            }

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
                    // Hide target info popup
                    s_targetInfo.visible = false;
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
