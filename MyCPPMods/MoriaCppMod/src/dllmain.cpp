// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  MoriaCppMod v2.0 — Advanced Builder & HISM Removal for Return to Moria   ║
// ║                                                                            ║
// ║  A UE4SS C++ mod for Return to Moria (UE4.27) providing:                  ║
// ║    - HISM instance hiding with persistence across sessions/worlds          ║
// ║    - Quick-build hotbar (F1-F8) with recipe capture & icon overlay         ║
// ║    - Dual-toolbar swap system (PageDown) with name-matching resolve        ║
// ║    - Rotation step control (F9) with ProcessEvent hook integration         ║
// ║    - UMG config menu, mod controller toolbar, and target info popup       ║
// ║    - Win32 GDI+ overlay bar with icon extraction pipeline                 ║
// ║                                                                            ║
// ║  Build:  cmake --build build --config Game__Shipping__Win64                ║
// ║          --target MoriaCppMod                                              ║
// ║  Deploy: Copy MoriaCppMod.dll -> <game>/Mods/MoriaCppMod/dlls/main.dll    ║
// ║                                                                            ║
// ║  Source: github.com/jbowensii/MoriaAdvancedBuilder                        ║
// ║  Date:   2026-02-26                                                        ║
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
#include <iomanip>
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
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>

#include "moria_testable.h"

namespace MoriaMods
{
    using namespace RC;
    using namespace RC::Unreal;

    // Verbose logging gate — when false (default), all VLOG() calls are short-circuited
    // to avoid format-string overhead.  Set to true via config or code for debugging.
    static bool s_verbose = false;
    static std::string s_language = "en"; // localization file (e.g. "en" loads en.json)
    // NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — macro needed to short-circuit variadic template
    #define VLOG(...) do { if (s_verbose) Output::send<LogLevel::Warning>(__VA_ARGS__); } while (0)

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

    // ── Cached property offsets (resolved via ForEachProperty on first use) ──
    // -2 = not yet resolved, -1 = property not found
    static inline int s_off_widgetTree = -2;       // UUserWidget::WidgetTree
    static inline int s_off_rootWidget = -2;       // UWidgetTree::RootWidget
    static inline int s_off_bIsFocusable = -2;     // UUserWidget::bIsFocusable
    static inline int s_off_font = -2;             // UTextBlock::Font
    static inline int s_off_brush = -2;            // UImage::Brush
    static inline int s_off_showMouseCursor = -2;  // APlayerController::bShowMouseCursor
    static inline int s_off_charMovement = -2;     // ACharacter::CharacterMovement
    static inline int s_off_capsuleComp = -2;      // ACharacter::CapsuleComponent
    static inline int s_off_bCheatFlying = -2;     // UCharacterMovementComponent::bCheatFlying
    static inline int s_off_recipeSelectMode = -2; // UI_WBP_BuildHUDv2_C::recipeSelectMode
    static inline int s_off_bLock = -2;            // UI_WBP_Build_Item_C::bLock
    static inline int s_off_icon = -2;             // UI_WBP_Build_Item_Medium_C::Icon
    static inline int s_off_blockName = -2;        // UI_WBP_Build_Item_Medium_C::blockName
    static inline int s_off_stackCount = -2;       // UI_WBP_Build_Item_Medium_C::StackCount
    static inline int s_off_texParamValues = -2;   // UMaterialInstanceDynamic::TextureParameterValues
    static inline int s_off_targetActor = -2;      // UBuildOverlayWidget::TargetActor
    static inline int s_off_selectedRecipe = -2;    // UI_WBP_Build_Tab_C::selectedRecipe
    static inline int s_off_selectedName = -2;      // UI_WBP_Build_Tab_C::selectedName
    static inline int s_off_recipesDataTable = -2;  // UI_WBP_Build_Tab_C::recipesDataTable

    // ── Struct-internal offsets (confirmed from SlateCore.hpp / Moria.hpp headers) ──
    // These are POD struct internals or UObject base layout — NOT resolvable via ForEachProperty
    static constexpr int BRUSH_IMAGE_SIZE_X = 0x08;     // FSlateBrush::ImageSize.X
    static constexpr int BRUSH_IMAGE_SIZE_Y = 0x0C;     // FSlateBrush::ImageSize.Y
    static constexpr int BRUSH_RESOURCE_OBJECT = 0x48;  // FSlateBrush::ResourceObject
    static constexpr int FONT_TYPEFACE_NAME = 0x40;     // FSlateFontInfo::TypefaceFontName
    static constexpr int FONT_SIZE = 0x48;              // FSlateFontInfo::Size
    static constexpr int FONT_STRUCT_SIZE = 0x58;       // sizeof(FSlateFontInfo)
    static constexpr int CONSTRUCTION_DISPLAY_NAME = 0x18;  // FMorConstructionDefinition::DisplayName (FText)
    static constexpr int RECIPE_BLOCK_VARIANTS = 0x68;   // FMorRecipeBlock::Variants (TArray data ptr)
    static constexpr int RECIPE_BLOCK_VARIANTS_NUM = 0x70; // FMorRecipeBlock::Variants.Num (int32)
    static constexpr int VARIANT_ROW_CI = 0xE0;         // FMorConstructionRecipeDefinition::ResultConstructionHandle.RowName.ComparisonIndex
    static constexpr int VARIANT_ROW_NUM = 0xE4;        // FMorConstructionRecipeDefinition::ResultConstructionHandle.RowName.Number
    static constexpr int TEX_PARAM_VALUE_PTR = 0x10;     // FTextureParameterValue::ParameterValue (UTexture*)
    static constexpr int DT_ROWMAP_OFFSET = 0x30;       // UDataTable internal RowMap offset
    static constexpr int DT_ROW_ACTOR_FNAME = 0x60;     // FSoftObjectPath.AssetPathName within construction row

    // Resolve a UProperty offset by name on an object's class (walks full inheritance chain).
    static int resolveOffset(UObject* obj, const wchar_t* propName, int& cache)
    {
        if (cache != -2) return cache;
        cache = -1;
        if (!obj) return -1;
        // Walk the class hierarchy: child → parent → grandparent → ...
        for (auto* strct = static_cast<UStruct*>(obj->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                {
                    cache = prop->GetOffset_Internal();
                    VLOG(STR("[MoriaCppMod] Resolved '{}' at offset 0x{:04X} (on {})\n"),
                         std::wstring(propName), cache, strct->GetName());
                    return cache;
                }
            }
        }
        VLOG(STR("[MoriaCppMod] WARNING: property '{}' not found on {} (full chain)\n"),
                                        std::wstring(propName), obj->GetClassPrivate()->GetName());
        return cache;
    }

    // Ensure s_off_brush is resolved (call with any UImage* before reading Brush fields)
    static void ensureBrushOffset(UObject* imageWidget)
    {
        if (s_off_brush == -2 && imageWidget)
            resolveOffset(imageWidget, L"Brush", s_off_brush);
    }

    // Set UWidgetTree::RootWidget via reflected offset
    static void setRootWidget(UObject* widgetTree, UObject* root)
    {
        int off = resolveOffset(widgetTree, L"RootWidget", s_off_rootWidget);
        if (off >= 0)
            *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + off) = root;
    }

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

    // LineTraceSingle param offsets — resolved at runtime from UFunction::ForEachProperty
    struct LTResolved
    {
        int WorldContextObject{-1}, Start{-1}, End{-1}, TraceChannel{-1};
        int bTraceComplex{-1}, ActorsToIgnore{-1}, DrawDebugType{-1};
        int OutHit{-1}, bIgnoreSelf{-1}, TraceColor{-1};
        int TraceHitColor{-1}, DrawTime{-1}, ReturnValue{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    static inline LTResolved s_lt{};

    static void resolveLTOffsets(UFunction* ltFunc)
    {
        if (s_lt.resolved) return;
        s_lt.resolved = true;
        s_lt.parmsSize = ltFunc->GetParmsSize();
        for (auto* prop : ltFunc->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"WorldContextObject") s_lt.WorldContextObject = off;
            else if (n == L"Start") s_lt.Start = off;
            else if (n == L"End") s_lt.End = off;
            else if (n == L"TraceChannel") s_lt.TraceChannel = off;
            else if (n == L"bTraceComplex") s_lt.bTraceComplex = off;
            else if (n == L"ActorsToIgnore") s_lt.ActorsToIgnore = off;
            else if (n == L"DrawDebugType") s_lt.DrawDebugType = off;
            else if (n == L"OutHit") s_lt.OutHit = off;
            else if (n == L"bIgnoreSelf") s_lt.bIgnoreSelf = off;
            else if (n == L"TraceColor") s_lt.TraceColor = off;
            else if (n == L"TraceHitColor") s_lt.TraceHitColor = off;
            else if (n == L"DrawTime") s_lt.DrawTime = off;
            else if (n == L"ReturnValue") s_lt.ReturnValue = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved LineTraceSingle: parmsSize={} Start={} End={} OutHit={} ReturnValue={}\n"),
             s_lt.parmsSize, s_lt.Start, s_lt.End, s_lt.OutHit, s_lt.ReturnValue);
    }

    // UpdateInstanceTransform param offsets — resolved at runtime from UFunction::ForEachProperty
    struct UITResolved
    {
        int InstanceIndex{-1}, NewInstanceTransform{-1};
        int bWorldSpace{-1}, bMarkRenderStateDirty{-1}, bTeleport{-1};
        int ReturnValue{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    static inline UITResolved s_uit{};

    static void resolveUITOffsets(UFunction* uitFunc)
    {
        if (s_uit.resolved) return;
        s_uit.resolved = true;
        s_uit.parmsSize = uitFunc->GetParmsSize();
        for (auto* prop : uitFunc->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"InstanceIndex") s_uit.InstanceIndex = off;
            else if (n == L"NewInstanceTransform") s_uit.NewInstanceTransform = off;
            else if (n == L"bWorldSpace") s_uit.bWorldSpace = off;
            else if (n == L"bMarkRenderStateDirty") s_uit.bMarkRenderStateDirty = off;
            else if (n == L"bTeleport") s_uit.bTeleport = off;
            else if (n == L"ReturnValue") s_uit.ReturnValue = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved UpdateInstanceTransform: parmsSize={} Index={} Transform={} ReturnValue={}\n"),
             s_uit.parmsSize, s_uit.InstanceIndex, s_uit.NewInstanceTransform, s_uit.ReturnValue);
    }

    // SavedRemoval, RemovalEntry, extractFriendlyName — defined in moria_testable.h

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

    // OVERLAY_BUILD_SLOTS (8) defined in moria_testable.h
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

    // Loc namespace core (s_table, utf8ToWide, parseJsonFile, initDefaults, get) — defined in moria_testable.h
    // Loc::load() remains here because it uses UE4SS Output::send for logging.
    namespace Loc
    {
        // Load string table: init defaults, then override from JSON file if available
        static void load(const std::string& locDir, const std::string& lang = "en")
        {
            initDefaults();
            std::string jsonPath = locDir + lang + ".json";
            if (parseJsonFile(jsonPath))
            {
                VLOG(STR("[MoriaCppMod] Loaded localization from {}\n"),
                                                utf8ToWide(jsonPath));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] Using compiled English defaults (no localization file)\n"));
            }
        }
    } // namespace Loc

    // modifierName, nextModifier, keyName — defined in moria_testable.h

    // ════════════════════════════════════════════════════════════════════════════
    // Section 3: Keybinding System & Window Discovery
    //   Rebindable F-key assignments, VK code → string conversion,
    //   findGameWindow() for overlay positioning
    // ════════════════════════════════════════════════════════════════════════════
    // BIND_COUNT defined in moria_testable.h
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
            {L"Rotation", L"Mod Controller", Input::Key::F9},                          // 8  (BIND_ROTATION, MC slot 0)
            {L"Target", L"Mod Controller", Input::Key::OEM_SIX},                       // 9  (BIND_TARGET, MC slot 1)
            {L"Toolbar Swap", L"Mod Controller", Input::Key::PAGE_DOWN},               // 10 (BIND_SWAP, MC slot 2)
            {L"Super Dwarf", L"Mod Controller", Input::Key::OEM_FIVE},                   // 11 (MC slot 3)
            {L"Remove Target", L"Mod Controller", Input::Key::NUM_ONE},                // 12 (MC slot 4)
            {L"Undo Last", L"Mod Controller", Input::Key::NUM_TWO},                    // 13 (MC slot 5)
            {L"Remove All", L"Mod Controller", Input::Key::NUM_THREE},                 // 14 (MC slot 6)
            {L"Configuration", L"Mod Controller", Input::Key::F12},                    // 15 (BIND_CONFIG, MC slot 7)
            {L"Advanced Builder Open", L"Advanced Builder", Input::Key::ADD},             // 16 (BIND_AB_OPEN)
    };
    static std::atomic<int> s_capturingBind{-1};

    // Modifier key choice: VK_SHIFT (0x10), VK_CONTROL (0x11), VK_MENU (0x12 = ALT), or VK_RMENU (0xA5 = RALT)
    static std::atomic<uint8_t> s_modifierVK{VK_SHIFT};

    // Config file paths
    static const char* INI_PATH = "Mods/MoriaCppMod/MoriaCppMod.ini";
    static const char* OLD_KEYBIND_PATH = "Mods/MoriaCppMod/keybindings.txt";

    static bool isModifierDown()
    {
        return (GetAsyncKeyState(s_modifierVK.load()) & 0x8000) != 0;
    }

    // When SHIFT is held with NumLock on, Windows reverses numpad keys:
    // VK_NUMPAD0-9 become their navigation equivalents (Insert, End, Down, etc.)
    // Returns the alternate VK code for a numpad key, or 0 if not a numpad key.
    static uint8_t numpadShiftAlternate(uint8_t vk)
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
                    gfx.DrawString(Loc::get("ovr.target").c_str(), -1, &tgtFont, tgtRect, &centerFmt, &tgtBrush);
                }

                // Slot 9 (Rotation): step degrees (top, bold) | separator line | T+total (bottom)
                if (i == 9)
                {
                    int stepVal = s_overlay.rotationStep;
                    int totalVal = s_overlay.totalRotation;

                    // Top line: step value with degree symbol (bold)
                    std::wstring stepStr = std::to_wstring(stepVal) + Loc::get("ovr.degree");
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
                    gfx.DrawString(Loc::get("ovr.config").c_str(), -1, &cfgFont, cfgRect, &centerFmt, &cfgBrush);
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
    // ════════════════════════════════════════════════════════════════════════════
    // Section 5: Config State & Cross-Thread Signals
    //   ConfigState struct: cheat toggles, pending actions, removal list
    //   s_pendingKeyLabelRefresh: config→game thread key label update signal
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr int CONFIG_TAB_COUNT = 3;
    static const wchar_t* CONFIG_TAB_NAMES[CONFIG_TAB_COUNT] = {L"Optional Mods", L"Key Mapping", L"Hide Environment"};

    struct ConfigState
    {
        // Cheat toggle states (read from debug menu actor on game thread)
        std::atomic<bool> freeBuild{false};

        // Pending actions (set by UMG config UI, consumed by game thread in on_update)
        std::atomic<bool> pendingToggleFreeBuild{false};
        std::atomic<bool> pendingUnlockAllRecipes{false};

        // Removal list (Building Options tab)
        CRITICAL_SECTION removalCS;
        std::atomic<bool> removalCSInit{false};
        std::vector<RemovalEntry> removalEntries; // display snapshot, protected by removalCS
        std::atomic<int> removalCount{0};         // quick count without lock

        // Pending removal (set by UI thread, consumed by game thread)
        std::atomic<int> pendingRemoveIndex{-1};
    };
    static ConfigState s_config{};

    static inline std::atomic<bool> s_pendingKeyLabelRefresh{false}; // cross-thread flag: config→game thread


    // ════════════════════════════════════════════════════════════════════════════
    // Section 6: MoriaCppMod Class — Main Mod Implementation
    //   Subsections: 6A File I/O, 6B Player Helpers, 6C Display/UI,
    //   6D HISM Removal, 6E Inventory/Toolbar, 6F Debug/Cheat, 6G Quick-Build,
    //   6H Icon Extraction, 6I UMG Widgets, 6J Overlay Management, 6K Public API
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

        // ── 6A: File I/O & Persistence ────────────────────────────────────────
        // Save/load HISM removal data (removed_instances.txt)
        // Save/load quick-build slots (quickbuild_slots.txt)
        // Format: meshName|posX|posY|posZ (single instance) or @meshName (type rule)

        // componentNameToMeshId — defined in moria_testable.h

        void loadSaveFile()
        {
            m_savedRemovals.clear();
            m_typeRemovals.clear();
            std::ifstream file(m_saveFilePath);
            if (!file.is_open())
            {
                VLOG(STR("[MoriaCppMod] No save file found (first run)\n"));
                return;
            }
            std::string line;
            while (std::getline(file, line))
            {
                auto parsed = parseRemovalLine(line);
                if (auto* pos = std::get_if<ParsedRemovalPosition>(&parsed))
                {
                    m_savedRemovals.push_back({pos->meshName, pos->posX, pos->posY, pos->posZ});
                }
                else if (auto* tr = std::get_if<ParsedRemovalTypeRule>(&parsed))
                {
                    m_typeRemovals.insert(tr->meshName);
                }
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
                    VLOG(STR("[MoriaCppMod] Removed {} position entries redundant with type rules\n"), redundant);
                }
            }

            // No dedup — stacked instances share the same position,
            // and each entry matches a different stacked instance on replay

            // Initialize tracking: all pending (not yet applied)
            m_appliedRemovals.assign(m_savedRemovals.size(), false);

            VLOG(STR("[MoriaCppMod] Loaded {} position removals + {} type rules\n"), m_savedRemovals.size(), m_typeRemovals.size());
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
                        entry.coordsW = Loc::get("ui.type_rule") + L" (all instances)";
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

        // ── 6B: Player & World Helpers ─────────────────────────────────────────
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

        // ── 6C: Display & UI Helpers ──────────────────────────────────────────
        // PrintString, on-screen text, chat widget, system messages

        // Discovers KismetSystemLibrary::PrintString param offsets at runtime.
        // Uses ForEachProperty() to locate params by name — safe across game updates.
        void probePrintString()
        {
            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:PrintString"));
            if (!fn)
            {
                VLOG(STR("[MoriaCppMod] PrintString NOT FOUND\n"));
                return;
            }
            m_ps.parmsSize = fn->GetParmsSize();
            VLOG(STR("[MoriaCppMod] PrintString ParmsSize={}\n"), m_ps.parmsSize);

            for (auto* prop : fn->ForEachProperty())
            {
                auto name = prop->GetName();
                int offset = prop->GetOffset_Internal();
                int size = prop->GetSize();
                VLOG(STR("[MoriaCppMod]   PS: {} @{} size={}\n"), name, offset, size);

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
            VLOG(STR("[MoriaCppMod] PrintString valid={}\n"), m_ps.valid);
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
            if (!fn || !cdo || !pc)
            {
                VLOG(STR("[MoriaCppMod] showOnScreen FAILED: fn={} cdo={} pc={}\n"),
                                                (void*)fn, (void*)cdo, (void*)pc);
                return;
            }

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
            VLOG(STR("[MoriaCppMod] showOnScreen: '{}' dur={:.1f}\n"), text, duration);
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
                VLOG(STR("[MoriaCppMod] AddToShortChat not found\n"));
                return;
            }

            FText ftext(text.c_str());
            uint8_t buf[sizeof(FText) + 16]{};
            std::memcpy(buf, &ftext, sizeof(FText));
            m_chatWidget->ProcessEvent(func, buf);
        }


        // ── Camera & Trace ──

        // ── 6D: HISM Removal System ──────────────────────────────────────────
        // Line trace from camera, instance hiding (UpdateInstanceTransform), undo, replay
        // Includes: removeAimed, removeAllOfType, undoLast, dumpAimedActor (target info)
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

            resolveUITOffsets(updateFunc);
            if (s_uit.ReturnValue < 0) return false;

            // Get current transform first
            auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!transFunc) return false;

            // One-time: validate GetInstanceTransform_Params layout against reflection
            static bool s_gtpValidated = false;
            if (!s_gtpValidated)
            {
                s_gtpValidated = true;
                for (auto* prop : transFunc->ForEachProperty())
                {
                    std::wstring n(prop->GetName());
                    int off = prop->GetOffset_Internal();
                    if (n == L"InstanceIndex" && off != 0)
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform InstanceIndex expected @0, got @{}\n"), off);
                    else if (n == L"bWorldSpace" && off != offsetof(GetInstanceTransform_Params, bWorldSpace))
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform bWorldSpace expected @{}, got @{}\n"),
                             (int)offsetof(GetInstanceTransform_Params, bWorldSpace), off);
                    else if (n == L"ReturnValue" && off != offsetof(GetInstanceTransform_Params, ReturnValue))
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform ReturnValue expected @{}, got @{}\n"),
                             (int)offsetof(GetInstanceTransform_Params, ReturnValue), off);
                }
                VLOG(STR("[MoriaCppMod] Validated GetInstanceTransform_Params ({}B struct vs {}B UFunction)\n"),
                     (int)sizeof(GetInstanceTransform_Params), transFunc->GetParmsSize());
            }

            GetInstanceTransform_Params gtp{};
            gtp.InstanceIndex = instanceIndex;
            gtp.bWorldSpace = 1;
            comp->ProcessEvent(transFunc, &gtp);
            if (!gtp.ReturnValue) return false;

            // Move deep underground, scale to near-zero
            FTransformRaw hidden = gtp.OutTransform;
            hidden.Translation.Z -= 50000.0f;
            hidden.Scale3D = {0.001f, 0.001f, 0.001f};

            // UpdateInstanceTransform — offsets resolved from UFunction
            std::vector<uint8_t> params(s_uit.parmsSize, 0);
            std::memcpy(params.data() + s_uit.InstanceIndex, &instanceIndex, 4);
            std::memcpy(params.data() + s_uit.NewInstanceTransform, &hidden, 48);
            params[s_uit.bWorldSpace] = 1;
            params[s_uit.bMarkRenderStateDirty] = 1;
            params[s_uit.bTeleport] = 1;
            comp->ProcessEvent(updateFunc, params.data());
            return params[s_uit.ReturnValue] != 0;
        }

        // Restore instance to original transform (undo a hide)
        bool restoreInstance(UObject* comp, int32_t instanceIndex, const FTransformRaw& original)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            resolveUITOffsets(updateFunc);
            if (s_uit.ReturnValue < 0) return false;

            std::vector<uint8_t> params(s_uit.parmsSize, 0);
            std::memcpy(params.data() + s_uit.InstanceIndex, &instanceIndex, 4);
            std::memcpy(params.data() + s_uit.NewInstanceTransform, &original, 48);
            params[s_uit.bWorldSpace] = 1;
            params[s_uit.bMarkRenderStateDirty] = 1;
            params[s_uit.bTeleport] = 1;
            comp->ProcessEvent(updateFunc, params.data());
            return params[s_uit.ReturnValue] != 0;
        }

        // Performs KismetSystemLibrary::LineTraceSingle via ProcessEvent.
        // Returns true if hit. Fills hitBuf (136 bytes = FHitResultLocal).
        // debugDraw=true shows red/green trace line in-game for 5 seconds.
        // Param offsets resolved from UFunction at runtime (s_lt struct).
        bool doLineTrace(const FVec3f& start, const FVec3f& end, uint8_t* hitBuf, bool debugDraw = false)
        {
            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return false;

            resolveLTOffsets(ltFunc);
            if (s_lt.ReturnValue < 0) return false;

            std::vector<uint8_t> params(s_lt.parmsSize, 0);
            std::memcpy(params.data() + s_lt.WorldContextObject, &pc, 8);
            std::memcpy(params.data() + s_lt.Start, &start, 12);
            std::memcpy(params.data() + s_lt.End, &end, 12);
            params[s_lt.TraceChannel] = 0;  // Visibility
            params[s_lt.bTraceComplex] = 1; // Per-triangle for accuracy
            params[s_lt.bIgnoreSelf] = 1;

            // Add player pawn to ActorsToIgnore so trace doesn't hit the character
            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params.data() + s_lt.ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 8, &one, 4);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 12, &one, 4);
            }

            if (debugDraw)
            {
                params[s_lt.DrawDebugType] = 2; // ForDuration
                float greenColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                float redColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float drawTime = 5.0f;
                std::memcpy(params.data() + s_lt.TraceColor, greenColor, 16);
                std::memcpy(params.data() + s_lt.TraceHitColor, redColor, 16);
                std::memcpy(params.data() + s_lt.DrawTime, &drawTime, 4);
            }
            else
            {
                params[s_lt.DrawDebugType] = 0; // None
            }

            kslCDO->ProcessEvent(ltFunc, params.data());

            bool bHit = params[s_lt.ReturnValue] != 0;
            if (bHit)
            {
                std::memcpy(hitBuf, params.data() + s_lt.OutHit, 136);
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
                VLOG(STR("[MoriaCppMod] Starting throttled replay ({} comps, max {} hides/frame)\n"),
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
            VLOG(STR("[MoriaCppMod] Replay done: {} hidden, {} pending\n"), m_replay.totalHidden, pending);
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
            VLOG(STR("[MoriaCppMod] Streaming: {} new components queued for replay\n"), m_replay.compQueue.size());
        }

        // ── Actions ──

        void inspectAimed()
        {
            VLOG(STR("[MoriaCppMod] --- Inspect ---\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                VLOG(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }

            VLOG(STR("[MoriaCppMod] Ray: ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f})\n"), start.X, start.Y, start.Z, end.X, end.Y, end.Z);

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf, true))
            { // debugDraw=true
                VLOG(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(Loc::get("msg.no_hit").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
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

            VLOG(STR("[MoriaCppMod] Component: {} | Class: {} | Item: {} | HISM: {}\n"), compName, className, item, isHISM);
            VLOG(STR("[MoriaCppMod] FullPath: {}\n"), fullName);
            VLOG(STR("[MoriaCppMod] MeshID: {} | Impact: ({:.1f},{:.1f},{:.1f})\n"),
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
                        VLOG(STR("[MoriaCppMod] Instance #{} pos: ({:.1f},{:.1f},{:.1f})\n"),
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
                VLOG(STR("[MoriaCppMod] No hit\n"));
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
                VLOG(STR("[MoriaCppMod] Not HISM: {} ({})\n"), name, cls);
                return;
            }

            if (item < 0)
            {
                VLOG(STR("[MoriaCppMod] No instance index (Item=-1)\n"));
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
            VLOG(STR("[MoriaCppMod] REMOVED {} stacked at ({:.0f},{:.0f},{:.0f}) from {} | Total: {}\n"),
                                            hiddenCount,
                                            targetX,
                                            targetY,
                                            targetZ,
                                            compName,
                                            m_savedRemovals.size());
        }

        void removeAllOfType()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                VLOG(STR("[MoriaCppMod] No hit\n"));
                return;
            }

            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp || !isHISMComponent(hitComp))
            {
                std::wstring name = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
                VLOG(STR("[MoriaCppMod] Not HISM: {}\n"), name);
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
            VLOG(STR("[MoriaCppMod] TYPE RULE: @{} — hidden {} instances (persists across all worlds)\n"), meshIdW, hidden);
        }


        void toggleHideCharacter()
        {
            std::vector<UObject*> dwarves;
            UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
            if (dwarves.empty()) return;

            m_characterHidden = !m_characterHidden;
            for (auto* dwarf : dwarves)
            {
                auto* fn = dwarf->GetFunctionByNameInChain(STR("SetActorHiddenInGame"));
                if (fn)
                {
                    uint8_t params[1] = {m_characterHidden ? uint8_t(1) : uint8_t(0)};
                    dwarf->ProcessEvent(fn, params);
                }
            }
            if (m_characterHidden)
                showOnScreen(Loc::get("msg.char_hidden").c_str(), 2.0f, 0.3f, 0.8f, 1.0f);
            else
                showOnScreen(Loc::get("msg.char_visible").c_str(), 2.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] Character hidden = {}\n"), m_characterHidden ? 1 : 0);
        }

        void toggleFlyMode()
        {
            std::vector<UObject*> dwarves;
            UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
            if (dwarves.empty()) return;

            m_flyMode = !m_flyMode;
            // EMovementMode: MOVE_Walking=1, MOVE_Falling=3, MOVE_Flying=5
            constexpr uint8_t MOVE_Falling = 3;
            constexpr uint8_t MOVE_Flying  = 5;

            for (auto* dwarf : dwarves)
            {
                int cmOff = resolveOffset(dwarf, L"CharacterMovement", s_off_charMovement);
                if (cmOff < 0) continue;
                auto* movComp = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(dwarf) + cmOff);
                if (!movComp)
                {
                    VLOG(STR("[MoriaCppMod] CharacterMovement is null!\n"));
                    continue;
                }

                int cfOff = resolveOffset(movComp, L"bCheatFlying", s_off_bCheatFlying);
                if (cfOff < 0) continue;
                uint8_t* flags = reinterpret_cast<uint8_t*>(movComp) + cfOff;

                // Order matters:
                // - Disable: clear bCheatFlying FIRST so engine allows mode transition
                // - Enable: call SetMovementMode FIRST, then set bCheatFlying to keep it active
                if (!m_flyMode)
                    *flags &= ~0x08; // clear bCheatFlying before mode change

                // SetMovementMode(NewMovementMode, NewCustomMode) via ProcessEvent
                // Use MOVE_Falling when disabling — character drops naturally to ground
                auto* fn = movComp->GetFunctionByNameInChain(STR("SetMovementMode"));
                if (fn)
                {
                    uint8_t params[2] = {m_flyMode ? MOVE_Flying : MOVE_Falling, 0};
                    movComp->ProcessEvent(fn, params);
                    VLOG(STR("[MoriaCppMod] SetMovementMode({}) called\n"),
                                                    m_flyMode ? 5 : 3);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] SetMovementMode not found!\n"));
                }

                if (m_flyMode)
                    *flags |= 0x08; // set bCheatFlying after mode change

                VLOG(STR("[MoriaCppMod] bCheatFlying = {}, flags byte = 0x{:02X}\n"),
                                                m_flyMode ? 1 : 0, *flags);

                // Toggle capsule collision for noclip (disable when flying, enable when walking)
                // SetCollisionEnabled on CapsuleComponent: NoCollision=0, QueryAndPhysics=3
                int capOff = resolveOffset(dwarf, L"CapsuleComponent", s_off_capsuleComp);
                if (capOff >= 0)
                {
                    auto* capsule = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(dwarf) + capOff);
                    if (capsule)
                    {
                        auto* colFn = capsule->GetFunctionByNameInChain(STR("SetCollisionEnabled"));
                        if (colFn)
                        {
                            uint8_t newType = m_flyMode ? 0 : 3; // NoCollision=0, QueryAndPhysics=3
                            capsule->ProcessEvent(colFn, &newType);
                            VLOG(STR("[MoriaCppMod] CapsuleComponent SetCollisionEnabled({}) — noclip {}\n"),
                                                            newType, m_flyMode ? STR("ON") : STR("OFF"));
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] SetCollisionEnabled not found on CapsuleComponent\n"));
                        }
                    }
                }
            }
            if (m_flyMode)
                showOnScreen(L"Fly + Noclip ON", 2.0f, 0.3f, 0.8f, 1.0f);
            else
                showOnScreen(Loc::get("msg.fly_off").c_str(), 2.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] Fly mode = {}\n"), m_flyMode ? 1 : 0);
        }

        void dumpAimedActor()
        {
            VLOG(STR("[MoriaCppMod] === AIMED ACTOR DUMP ===\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                VLOG(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }

            // Use a wider trace — we want to hit actors, not just HISM instances
            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return;

            resolveLTOffsets(ltFunc);
            if (s_lt.ReturnValue < 0) return;

            std::vector<uint8_t> params(s_lt.parmsSize, 0);
            std::memcpy(params.data() + s_lt.WorldContextObject, &pc, 8);
            std::memcpy(params.data() + s_lt.Start, &start, 12);
            std::memcpy(params.data() + s_lt.End, &end, 12);
            params[s_lt.TraceChannel] = 0;  // Visibility
            params[s_lt.bTraceComplex] = 0; // Simple trace to hit actor bounds
            params[s_lt.bIgnoreSelf] = 1;
            params[s_lt.DrawDebugType] = 2; // ForDuration
            float greenColor[4] = {0.0f, 1.0f, 1.0f, 1.0f};
            float redColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
            float drawTime = 5.0f;
            std::memcpy(params.data() + s_lt.TraceColor, greenColor, 16);
            std::memcpy(params.data() + s_lt.TraceHitColor, redColor, 16);
            std::memcpy(params.data() + s_lt.DrawTime, &drawTime, 4);

            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params.data() + s_lt.ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 8, &one, 4);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 12, &one, 4);
            }

            kslCDO->ProcessEvent(ltFunc, params.data());

            bool bHit = params[s_lt.ReturnValue] != 0;
            if (!bHit)
            {
                VLOG(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(Loc::get("msg.actor_dump_no_hit").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            uint8_t hitBuf[136]{};
            std::memcpy(hitBuf, params.data() + s_lt.OutHit, 136);

            // Get the hit component and its owning actor
            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp)
            {
                VLOG(STR("[MoriaCppMod] Hit but null component\n"));
                return;
            }

            std::wstring compName(hitComp->GetName());
            std::wstring compClass = safeClassName(hitComp);
            VLOG(STR("[MoriaCppMod] Hit component: {} ({})\n"), compName, compClass);

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
                VLOG(STR("[MoriaCppMod] No owning actor found\n"));
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
                VLOG(STR("[MoriaCppMod] GetDisplayName found, parmsSize={}\n"), getDispFn->GetParmsSize());
                if (getDispFn->GetParmsSize() == sizeof(FText))
                {
                    FText txt{};
                    actor->ProcessEvent(getDispFn, &txt);
                    if (txt.Data) displayName = txt.ToString();
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] GetDisplayName NOT found on this actor\n"));
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
                VLOG(STR("[MoriaCppMod] Display name fallback: '{}'\n"), displayName);
            }

            VLOG(STR("[MoriaCppMod] Actor: {} | Class: {}\n"), actorName, actorClassName);
            VLOG(STR("[MoriaCppMod] Display: {}\n"), displayName);
            VLOG(STR("[MoriaCppMod] Path: {}\n"), assetPath);

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

            VLOG(STR("[MoriaCppMod] Buildable: {} Recipe: {}\n"), isBuildable ? STR("Yes") : STR("No"), recipeRef);

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

                        // Read RowMap: DT_ROWMAP_OFFSET, TSet<TPair<FName, uint8*>>
                        uint8_t* dtBase = reinterpret_cast<uint8_t*>(dtConst);
                        constexpr int SET_ELEMENT_SIZE = 24;
                        constexpr int FNAME_SIZE = 8;

                        struct
                        {
                            uint8_t* Data;
                            int32_t Num;
                            int32_t Max;
                        } elemArray{};
                        std::memcpy(&elemArray, dtBase + DT_ROWMAP_OFFSET, 16);

                        dumpFile << L"RowMap: " << elemArray.Num << L" rows\n\n";
                        dumpFile.flush();

                        // TSoftClassPtr = TPersistentObjectPtr<FSoftObjectPath> layout:
                        //   +0x00 (row+0x50): TWeakObjectPtr (8 bytes) — cached resolved ptr
                        //   +0x08 (row+0x58): int32 TagAtLastTest (4 bytes) + 4 bytes padding
                        //   +0x10 (row+0x60): FName AssetPathName (8 bytes) — the asset path
                        //   +0x18 (row+0x68): FString SubPathString (16 bytes) — usually empty
                        // DT_ROW_ACTOR_FNAME defined in struct-internal constants section

                        int matchCount = 0;

                        for (int i = 0; i < elemArray.Num; i++)
                        {
                            uint8_t* elem = elemArray.Data + i * SET_ELEMENT_SIZE;
                            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;

                            uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + FNAME_SIZE);
                            if (!rowData || !isReadableMemory(rowData, 0x78)) continue;

                            // Read Actor AssetPathName FName at row+0x60
                            FName assetFName;
                            std::memcpy(&assetFName, rowData + DT_ROW_ACTOR_FNAME, FNAME_SIZE);
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
                                    FText* txt = reinterpret_cast<FText*>(rowData + CONSTRUCTION_DISPLAY_NAME);
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

                    VLOG(STR("[MoriaCppMod] DT_Constructions scan -> actor_dump.txt\n"));
                }
            }

            // Use DT_Constructions display name if found, overriding the fallback
            if (!dtDisplayName.empty())
            {
                displayName = dtDisplayName;
                VLOG(STR("[MoriaCppMod] DT display name: '{}' (row '{}')\n"), displayName, dtRowName);
            }

            // Store for Shift+F10 build-from-target
            m_lastTargetBuildable = isBuildable;
            m_targetBuildRecipeRef = recipeRef;
            m_targetBuildRowName = dtRowName; // DT_Constructions row name (also key for DT_ConstructionRecipes)
            if (isBuildable && !displayName.empty())
            {
                m_targetBuildName = displayName;
                VLOG(STR("[MoriaCppMod] [TargetBuild] Stored target: name='{}' recipeRef='{}' row='{}'\n"),
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

                        VLOG(STR("[MoriaCppMod] [F10] Non-buildable inspect: {} | HISM={} | MeshID={}\n"),
                                                        inspCompName, inspIsHISM, inspMeshIdW);

                        // Show inspect data in target info popup instead of actor data
                        showTargetInfo(inspCompName, friendlyName, inspMeshIdW, inspClassName, false, posInfo, L"");
                        VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
                        return;
                    }
                }
            }

            // Show target info popup window (buildable objects or inspect fallback failed)
            showTargetInfo(actorName, displayName, assetPath, actorClassName, isBuildable, recipeRef, dtRowName);

            VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
        }


        // Find the MorInventoryComponent on a character
        // ── 6E: Inventory & Toolbar System ────────────────────────────────────
        // Inventory component discovery, toolbar swap, BodyInventory stash containers
        // Name-matching resolve phase

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

        // Discover the EpicPack bag container handle — used by swapToolbar
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
                    VLOG(STR("[MoriaCppMod] Container[{}] ID={} slots={} class='{}'\n"), i, cId, maxSlots, cName);
                }
            }

            if (!m_bodyInvHandles.empty())
            {
                m_bodyInvHandle = m_bodyInvHandles[0]; // fallback: first scanned container
            }
            VLOG(STR("[MoriaCppMod] Found {} BodyInventory containers\n"), m_bodyInvHandles.size());

            // Repair stale stash containers (once per session):
            // Trigger: more than 2 stash containers (extras accumulated from previous sessions)
            // Drop contents + containers, let creation code rebuild exactly 2.
            if (m_bodyInvHandles.size() > 3 && !m_repairDone)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {} BodyInventory containers (expected 3) — repairing\n"),
                                                m_bodyInvHandles.size());
                auto* dropFunc = invComp->GetFunctionByNameInChain(STR("DropItem"));
                auto* ihfGetSlotN = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot")) : nullptr;
                auto* ihfIsValidN = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem")) : nullptr;
                if (dropFunc && ihfGetSlotN && ihfIsValidN)
                    {
                        auto diItem = findParam(dropFunc, L"Item");
                        auto diCount = findParam(dropFunc, L"Count");
                        int gsItem = -1, gsSlot = -1, gsRet = -1, ivItem = -1, ivRet = -1;
                        for (auto* p : ihfGetSlotN->ForEachProperty()) {
                            std::wstring n(p->GetName());
                            if (n == STR("Item")) gsItem = p->GetOffset_Internal();
                            else if (n == STR("Slot")) gsSlot = p->GetOffset_Internal();
                            else if (n == STR("ReturnValue")) gsRet = p->GetOffset_Internal();
                        }
                        for (auto* p : ihfIsValidN->ForEachProperty()) {
                            std::wstring n(p->GetName());
                            if (n == STR("Item")) ivItem = p->GetOffset_Internal();
                            else if (n == STR("ReturnValue")) ivRet = p->GetOffset_Internal();
                        }
                        for (int d = static_cast<int>(m_bodyInvHandles.size()) - 1; d >= 1; d--)
                        {
                            for (int s = 0; s < TOOLBAR_SLOTS; s++)
                            {
                                std::vector<uint8_t> gb(std::max(ihfGetSlotN->GetParmsSize() + 32, 64), 0);
                                std::memcpy(gb.data() + gsItem, m_bodyInvHandles[d].data(), handleSize);
                                *reinterpret_cast<int32_t*>(gb.data() + gsSlot) = s;
                                m_ihfCDO->ProcessEvent(ihfGetSlotN, gb.data());
                                std::vector<uint8_t> vb(std::max(ihfIsValidN->GetParmsSize() + 32, 64), 0);
                                std::memcpy(vb.data() + ivItem, gb.data() + gsRet, handleSize);
                                m_ihfCDO->ProcessEvent(ihfIsValidN, vb.data());
                                if (vb[ivRet] != 0)
                                {
                                    auto* ih = reinterpret_cast<const FItemHandleLocal*>(gb.data() + gsRet);
                                    VLOG(STR("[MoriaCppMod] Repair: dropping item ID={} from stash[{}] slot {}\n"), ih->ID, d, s);
                                    std::vector<uint8_t> db(std::max(dropFunc->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(db.data() + diItem.offset, gb.data() + gsRet, handleSize);
                                    *reinterpret_cast<int32_t*>(db.data() + diCount.offset) = 1;
                                    invComp->ProcessEvent(dropFunc, db.data());
                                }
                            }
                            int droppedId = reinterpret_cast<const FItemHandleLocal*>(m_bodyInvHandles[d].data())->ID;
                            std::vector<uint8_t> db(std::max(dropFunc->GetParmsSize() + 32, 64), 0);
                            std::memcpy(db.data() + diItem.offset, m_bodyInvHandles[d].data(), handleSize);
                            *reinterpret_cast<int32_t*>(db.data() + diCount.offset) = 1;
                            invComp->ProcessEvent(dropFunc, db.data());
                            VLOG(STR("[MoriaCppMod] Repair: dropped stash container ID={}\n"), droppedId);
                        }
                        m_bodyInvHandles.resize(1);
                        m_bodyInvHandle = m_bodyInvHandles[0];
                        m_repairDone = true;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Repair: stash containers removed — recreating fresh\n"));
                    }
            }

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

                            VLOG(STR("[MoriaCppMod] Created stash BodyInventory #{} — ID={}\n"), i + 1, newId);
                        }
                        VLOG(STR("[MoriaCppMod] Now have {} BodyInventory containers (1 hotbar + 2 stash)\n"), m_bodyInvHandles.size());
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] AddItem param offsets not found (Item={} Count={} Ret={})\n"),
                                                        aiItem.offset,
                                                        aiCount.offset,
                                                        aiRet.offset);
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] AddItem function not found on inventory component\n"));
                }
            }
            else if (m_bodyInvHandles.size() == 1)
            {
                VLOG(STR("[MoriaCppMod] Only 1 BodyInventory but class not found — cannot create stash\n"));
            }

            if (m_bagHandle.empty())
            {
                VLOG(STR("[MoriaCppMod] Note: EpicPack bag not equipped\n"));
            }
            if (m_bodyInvHandle.empty())
            {
                showOnScreen(Loc::get("msg.body_inv_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                return false;
            }
            return true;
        }


        // ── Toolbar Swap: PAGE_DOWN — 2 toolbars via BodyInventory containers ──
        // m_bodyInvHandles[0] = hotbar, [1] = T1 stash, [2] = T2 stash
        // Phase 0: MoveItem(hotbar items → stash container) using GetItemForHotbarSlot
        // Phase 1: MoveItem(stash items → hotbar) using IHF::GetItemForSlot on stash
        static constexpr int TOOLBAR_SLOTS = 8;
        int m_activeToolbar{0}; // 0 or 1 — which toolbar is currently visible

        // Swap state machine (multi-frame, one move per tick)
        struct SwapState
        {
            bool active{false};
            bool resolved{false}; // true after name-matching resolve phase
            bool cleared{false};      // true after EmptyContainer on stash destination
            bool dropToGround{false}; // true = drop hotbar items instead of stashing (both-containers failsafe)
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
            VLOG(STR("[MoriaCppMod] [Swap] === swapToolbar() called ===\n"));
            try
            {
                VLOG(STR("[MoriaCppMod] [Swap] State: active={} bodyInvHandle.empty={} handles.size={} charLoaded={}\n"),
                                                m_swap.active,
                                                m_bodyInvHandle.empty(),
                                                m_bodyInvHandles.size(),
                                                m_characterLoaded);

                if (m_swap.active)
                {
                    showOnScreen(Loc::get("msg.swap_in_progress").c_str(), 2.0f, 1.0f, 1.0f, 0.0f);
                    VLOG(STR("[MoriaCppMod] [Swap] BLOCKED: swap already active\n"));
                    return;
                }
                // Discover container handles if not cached
                if (m_bodyInvHandle.empty())
                {
                    VLOG(STR("[MoriaCppMod] [Swap] No cached handles, running discovery...\n"));
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
                        VLOG(STR("[MoriaCppMod] [Swap] Player found, discovering containers...\n"));
                        auto* invComp = findPlayerInventoryComponent(playerChar);
                        if (invComp)
                        {
                            discoverBagHandle(invComp);
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [Swap] FAIL: findPlayerInventoryComponent returned null\n"));
                        }
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [Swap] FAIL: Dwarf character not found in actors\n"));
                    }
                }
                if (m_bodyInvHandle.empty())
                {
                    showOnScreen(Loc::get("msg.body_inv_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                    VLOG(STR("[MoriaCppMod] [Swap] FAIL: m_bodyInvHandle still empty after discovery\n"));
                    return;
                }
                // Need at least 3 BodyInventory containers: [0]=hotbar, [1]=T1 stash, [2]=T2 stash
                if (m_bodyInvHandles.size() < 3)
                {
                    showOnScreen(std::format(L"Need 3 BodyInventory containers, found {}", m_bodyInvHandles.size()), 3.0f, 1.0f, 0.3f, 0.3f);
                    VLOG(STR("[MoriaCppMod] [Swap] FAIL: only {} BodyInventory containers (need 3)\n"), m_bodyInvHandles.size());
                    return;
                }

                // Log handle IDs for debugging
                for (size_t hi = 0; hi < m_bodyInvHandles.size(); hi++)
                {
                    int32_t hid = reinterpret_cast<const FItemHandleLocal*>(m_bodyInvHandles[hi].data())->ID;
                    VLOG(STR("[MoriaCppMod] [Swap] Handle[{}] ID={} size={}\n"), hi, hid, m_bodyInvHandles[hi].size());
                }

                int curTB = m_activeToolbar;
                int nextTB = 1 - curTB; // toggle 0↔1

                VLOG(STR("[MoriaCppMod] toolbar swap: T{} -> T{} (stash->container[{}], restore<-container[{}])\n"),
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
                VLOG(STR("[MoriaCppMod] [Swap] EXCEPTION in swapToolbar()\n"));
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [Swap] UNKNOWN EXCEPTION in swapToolbar()\n"));
            }
        }

        // Called from on_update() — processes one move per tick.
        // Phase 0: Move hotbar items → stash BodyInventory container (or drop on ground)
        // Phase 1: Move stash items from BodyInventory container → hotbar
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

                // Look up functions — all phases use IHF::GetItemForSlot + IsValidItem
                auto* moveFunc = invComp->GetFunctionByNameInChain(STR("MoveItem"));
                if (!m_ihfCDO)
                {
                    m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
                    if (m_ihfCDO)
                        VLOG(STR("[MoriaCppMod] Found IHF CDO: '{}'\n"), std::wstring(m_ihfCDO->GetName()));
                }
                if (!m_ihfCDO || !moveFunc)
                {
                    VLOG(STR("[MoriaCppMod] Swap: functions missing (IHF={} MoveItem={})\n"), m_ihfCDO != nullptr, moveFunc != nullptr);
                    m_swap.active = false;
                    return;
                }

                auto* ihfGetSlot = m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot"));
                auto* ihfIsValid = m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem"));
                if (!ihfGetSlot || !ihfIsValid)
                {
                    VLOG(STR("[MoriaCppMod] Swap: IHF functions missing (GetItemForSlot={} IsValidItem={})\n"),
                                                    ihfGetSlot != nullptr, ihfIsValid != nullptr);
                    m_swap.active = false;
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

                auto gsItem = findParam(ihfGetSlot, L"Item");
                auto gsSlot = findParam(ihfGetSlot, L"Slot");
                auto gsRet = findParam(ihfGetSlot, L"ReturnValue");
                auto ivItem = findParam(ihfIsValid, L"Item");
                auto ivRet = findParam(ihfIsValid, L"ReturnValue");
                auto mItem = findParam(moveFunc, L"Item");
                auto mDest = findParam(moveFunc, L"Destination");
                int handleSize = gsRet.size;
                if (handleSize <= 0) handleSize = 20;

                auto& hotbarContainer = m_bodyInvHandles[0];

                // Helper: check if a slot in a container has a valid item
                // Returns true if valid, and slotBuf will contain the item handle at gsRet.offset
                auto readSlot = [&](const std::vector<uint8_t>& container, int slot,
                                    std::vector<uint8_t>& slotBuf) -> bool {
                    slotBuf.assign(std::max(ihfGetSlot->GetParmsSize() + 32, 64), 0);
                    std::memcpy(slotBuf.data() + gsItem.offset, container.data(), handleSize);
                    *reinterpret_cast<int32_t*>(slotBuf.data() + gsSlot.offset) = slot;
                    m_ihfCDO->ProcessEvent(ihfGetSlot, slotBuf.data());
                    std::vector<uint8_t> vb(std::max(ihfIsValid->GetParmsSize() + 32, 64), 0);
                    std::memcpy(vb.data() + ivItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                    m_ihfCDO->ProcessEvent(ihfIsValid, vb.data());
                    return vb[ivRet.offset] != 0;
                };

                // ── Resolve Phase: determine stash/restore containers by item count ──
                if (!m_swap.resolved)
                {
                    m_swap.resolved = true;

                    // Count valid items in each stash container
                    auto countItems = [&](int cIdx) -> int {
                        int count = 0;
                        std::vector<uint8_t> buf;
                        for (int s = 0; s < TOOLBAR_SLOTS; s++)
                            if (readSlot(m_bodyInvHandles[cIdx], s, buf)) count++;
                        return count;
                    };

                    int c1Count = countItems(1);
                    int c2Count = countItems(2);

                    VLOG(STR("[MoriaCppMod] Resolve: c[1]={} items, c[2]={} items\n"), c1Count, c2Count);

                    if (c1Count == 0 && c2Count > 0)
                    {
                        // Container[1] empty, [2] has items → stash to [1], restore from [2]
                        m_swap.stashIdx = 1;
                        m_swap.restoreIdx = 2;
                    }
                    else if (c2Count == 0 && c1Count > 0)
                    {
                        // Container[2] empty, [1] has items → stash to [2], restore from [1]
                        m_swap.stashIdx = 2;
                        m_swap.restoreIdx = 1;
                    }
                    else if (c1Count > 0 && c2Count > 0)
                    {
                        // Both containers have items — ERROR STATE
                        // Drop hotbar items on ground, restore from whichever has more items
                        m_swap.dropToGround = true;
                        m_swap.restoreIdx = (c1Count >= c2Count) ? 1 : 2;
                        Output::send<LogLevel::Warning>(
                            STR("[MoriaCppMod] Resolve: BOTH containers have items (c[1]={}, c[2]={}) — dropping hotbar, restoring from [{}]\n"),
                            c1Count, c2Count, m_swap.restoreIdx);
                    }
                    // else: both empty — keep default mapping (first swap, Toolbar 2 starts empty)

                    VLOG(STR("[MoriaCppMod] Resolve: RESULT stashIdx={} restoreIdx={} dropToGround={}\n"),
                                                    m_swap.stashIdx, m_swap.restoreIdx, m_swap.dropToGround);
                    m_swap.wait = 1;
                    return;
                }

                auto& stashContainer = m_bodyInvHandles[m_swap.stashIdx];
                auto& restoreContainer = m_bodyInvHandles[m_swap.restoreIdx];

                // ── Phase 0: Move hotbar items → stash container (or drop on ground) ──
                if (m_swap.phase == 0)
                {
                    if (m_swap.dropToGround)
                    {
                        // Failsafe: both containers have items — drop hotbar items on ground
                        auto* dropFunc = invComp->GetFunctionByNameInChain(STR("DropItem"));
                        if (!dropFunc)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: DropItem function not found\n"));
                            m_swap.active = false;
                            return;
                        }
                        auto diItem = findParam(dropFunc, L"Item");
                        auto diCount = findParam(dropFunc, L"Count");

                        for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                        {
                            std::vector<uint8_t> slotBuf;
                            if (!readSlot(hotbarContainer, slot, slotBuf))
                            {
                                m_swap.slot = slot + 1;
                                continue;
                            }

                            auto* slotHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                            std::vector<uint8_t> dropBuf(std::max(dropFunc->GetParmsSize() + 32, 128), 0);
                            std::memcpy(dropBuf.data() + diItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                            *reinterpret_cast<int32_t*>(dropBuf.data() + diCount.offset) = 999999;
                            invComp->ProcessEvent(dropFunc, dropBuf.data());

                            m_swap.moved++;
                            m_swap.slot = slot + 1;
                            Output::send<LogLevel::Warning>(
                                STR("[MoriaCppMod] Phase0: hotbar[{}] id={} DROPPED on ground (both-containers failsafe)\n"),
                                slot, slotHandle->ID);
                            m_swap.wait = 3;
                            return;
                        }
                    }
                    else
                    {
                        // Normal path: stash hotbar items → stash container
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
                                    VLOG(STR("[MoriaCppMod] Phase0: EmptyContainer([{}]) — clearing stale items before stash\n"),
                                                                    m_swap.stashIdx);
                                }
                            }
                            m_swap.wait = 3;
                            return; // give a tick for empty to process
                        }

                        for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                        {
                            std::vector<uint8_t> slotBuf;
                            if (!readSlot(hotbarContainer, slot, slotBuf))
                            {
                                m_swap.slot = slot + 1;
                                continue;
                            }

                            auto* stashHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                            std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                            std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                            std::memcpy(moveBuf.data() + mDest.offset, stashContainer.data(), handleSize);
                            invComp->ProcessEvent(moveFunc, moveBuf.data());

                            // Validate: re-read the slot we just moved FROM
                            std::vector<uint8_t> verifyBuf;
                            bool stillHasItem = readSlot(hotbarContainer, slot, verifyBuf);
                            if (stillHasItem)
                            {
                                auto* vh = reinterpret_cast<const FItemHandleLocal*>(verifyBuf.data() + gsRet.offset);
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: WARN hotbar[{}] still has item id={} after MoveItem\n"), slot, vh->ID);
                            }
                            else
                            {
                                VLOG(STR("[MoriaCppMod] Phase0: hotbar[{}] verified empty after move\n"), slot);
                            }

                            m_swap.moved++;
                            m_swap.slot = slot + 1;

                            VLOG(STR("[MoriaCppMod] Phase0: hotbar[{}] id={} -> container[{}]\n"), slot, stashHandle->ID, m_swap.stashIdx);

                            m_swap.wait = 3;
                            return;
                        }
                    }

                    VLOG(STR("[MoriaCppMod] Phase0 done: {} items {}. Restoring from container[{}]\n"),
                                                    m_swap.moved,
                                                    m_swap.dropToGround ? STR("dropped") : STR("stashed"),
                                                    m_swap.restoreIdx);

                    m_swap.phase = 1;
                    m_swap.slot = 0;
                    m_swap.wait = 3;
                    return;
                }

                // ── Phase 1: Move items from restore container → hotbar ──
                if (m_swap.phase == 1)
                {
                    for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                    {
                        std::vector<uint8_t> slotBuf;
                        if (!readSlot(restoreContainer, slot, slotBuf))
                        {
                            m_swap.slot = slot + 1;
                            continue;
                        }

                        auto* restHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                        VLOG(STR("[MoriaCppMod] Phase1: container[{}][{}] id={} -> hotbar\n"), m_swap.restoreIdx, slot, restHandle->ID);

                        std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                        std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                        std::memcpy(moveBuf.data() + mDest.offset, hotbarContainer.data(), handleSize);
                        invComp->ProcessEvent(moveFunc, moveBuf.data());

                        // Validate: re-read the hotbar slot we just moved TO
                        std::vector<uint8_t> verifyBuf;
                        bool arrived = readSlot(hotbarContainer, slot, verifyBuf);
                        if (!arrived)
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: WARN hotbar[{}] still empty after MoveItem from container[{}]\n"), slot, m_swap.restoreIdx);
                        else
                        {
                            auto* vh = reinterpret_cast<const FItemHandleLocal*>(verifyBuf.data() + gsRet.offset);
                            VLOG(STR("[MoriaCppMod] Phase1: hotbar[{}] verified item id={} after move\n"), slot, vh->ID);
                        }

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
                            VLOG(STR("[MoriaCppMod] Phase1: EmptyContainer([{}]) — cleanup after restore\n"), m_swap.restoreIdx);
                        }
                    }

                    // Swap done
                    m_activeToolbar = m_swap.nextTB;
                    s_overlay.activeToolbar = m_swap.nextTB;
                    s_overlay.needsUpdate = true;

                    std::wstring msg = std::format(L"Toolbar {} active ({} items moved)", m_swap.nextTB + 1, m_swap.moved);
                    showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
                    VLOG(STR("[MoriaCppMod] {}\n"), msg);
                    m_swap.active = false;
                    refreshActionBar();
                    return;
                }
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] EXCEPTION in swapToolbarTick()\n"));
                m_swap.active = false;
            }
        }

        void toggleBuildHUD()
        {
            VLOG(STR("[MoriaCppMod] === Toggle Build HUD ===\n"));

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
                VLOG(STR("[MoriaCppMod] WBP_MoriaHUD_C NOT FOUND (visible)\n"));
                showOnScreen(Loc::get("msg.hud_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // First probe BuildHUDShow params
            auto* showFunc = moriaHUD->GetFunctionByNameInChain(STR("BuildHUDShow"));
            auto* hideFunc = moriaHUD->GetFunctionByNameInChain(STR("BuildHUDHide"));

            if (showFunc)
            {
                VLOG(STR("[MoriaCppMod] BuildHUDShow params ({}B):\n"), showFunc->GetParmsSize());
                for (auto* prop : showFunc->ForEachProperty())
                {
                    VLOG(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }

                // Try calling it with zeroed params
                int parmsSize = showFunc->GetParmsSize();
                std::vector<uint8_t> buf(parmsSize, 0);
                moriaHUD->ProcessEvent(showFunc, buf.data());
                VLOG(STR("[MoriaCppMod] Called BuildHUDShow!\n"));
                showOnScreen(L"BuildHUDShow called!", 3.0f, 0.0f, 1.0f, 0.0f);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] BuildHUDShow NOT FOUND\n"));
            }

            if (hideFunc)
            {
                VLOG(STR("[MoriaCppMod] BuildHUDHide params ({}B):\n"), hideFunc->GetParmsSize());
                for (auto* prop : hideFunc->ForEachProperty())
                {
                    VLOG(STR("[MoriaCppMod]   param: {} @{} size={}\n"),
                                                    std::wstring(prop->GetName()),
                                                    prop->GetOffset_Internal(),
                                                    prop->GetSize());
                }
            }

            VLOG(STR("[MoriaCppMod] === END Toggle Build HUD ===\n"));
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
                            VLOG(STR("[MoriaCppMod] Called {}::{} (direct find)\n"),
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
                        VLOG(STR("[MoriaCppMod] Called {}::{} (actor scan)\n"), cls, std::wstring(funcName));
                        return true;
                    }
                    VLOG(STR("[MoriaCppMod] Function {} not found on {}\n"), std::wstring(funcName), cls);
                    return false;
                }
            }
            // Only log on first retry (not every frame)
            static int s_debugNotFoundCount = 0;
            if (++s_debugNotFoundCount <= 3)
                VLOG(STR("[MoriaCppMod] Actor {} not found (attempt {})\n"), std::wstring(actorClass), s_debugNotFoundCount);
            return false;
        }


        // Read debug menu bool properties to show current state
        // ── 6F: Debug & Cheat Commands ────────────────────────────────────────
        // Debug menu toggles (Free Build), rotation control, aimed building rotation

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
                    VLOG(STR("[MoriaCppMod] {}\n"), msg);
                    return;
                }
            }
            showOnScreen(Loc::get("msg.debug_actor_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
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
                VLOG(STR("[MoriaCppMod] syncDebugToggleState: debug actor not found\n"));
                return false;
            }

            UObject* debugActor = objs[0];
            auto readBool = [&](const TCHAR* name) -> bool {
                void* ptr = debugActor->GetValuePtrByPropertyNameInChain(name);
                return ptr && *static_cast<uint8_t*>(ptr) != 0;
            };

            s_config.freeBuild = readBool(STR("free_construction"));

            VLOG(
                STR("[MoriaCppMod] syncDebugToggleState: freeBuild={}\n"),
                s_config.freeBuild ? 1 : 0);
            return true;
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
            if (s_off_targetActor == -2)
                resolveOffset(hud, L"TargetActor", s_off_targetActor);
            if (s_off_targetActor < 0) return nullptr;
            uint8_t* hudBase = reinterpret_cast<uint8_t*>(hud);
            if (!isReadableMemory(hudBase + s_off_targetActor, sizeof(RC::Unreal::FWeakObjectPtr))) return nullptr;
            RC::Unreal::FWeakObjectPtr weakPtr{};
            std::memcpy(&weakPtr, hudBase + s_off_targetActor, sizeof(RC::Unreal::FWeakObjectPtr));
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

        // bLock offset resolved via s_off_bLock (ForEachProperty on UI_WBP_Build_Item_C)

        // Reactive quick-build state machine (replaces frame-counting)
        enum class QBPhase { Idle, CancelPlacement, CloseMenu, OpenMenu, SelectRecipe };
        int m_pendingQuickBuildSlot{-1};     // which F1-F8 slot is pending (-1 = none)
        QBPhase m_qbPhase{QBPhase::Idle};    // quickbuild phase
        int m_qbTimeout{0};                  // safety timeout counter

        // Cached widget pointers for cheap state checks (invalidated on world unload)
        UObject* m_cachedBuildHUD{nullptr};  // UI_WBP_BuildHUDv2_C
        UObject* m_cachedBuildTab{nullptr};  // UI_WBP_Build_Tab_C
        UFunction* m_fnIsShowing{nullptr};   // cached IsShowing() on Build_Tab

        // Target-to-build: Shift+F10 — build the last targeted buildable object
        std::wstring m_targetBuildName;      // display name from last F10 target
        std::wstring m_targetBuildRecipeRef; // class name sans BP_ prefix (for bLock matching)
        std::wstring m_targetBuildRowName;   // DT_Constructions row name (also key for DT_ConstructionRecipes)
        bool m_lastTargetBuildable{false};   // was the last target buildable?
        bool m_pendingTargetBuild{false};    // pending build-from-target state machine
        bool m_buildMenuWasOpen{false};      // tracks build menu open/close for ActionBar refresh
        QBPhase m_tbPhase{QBPhase::Idle};    // target-build phase
        int m_tbTimeout{0};                  // safety timeout counter

        std::vector<uint8_t> m_bagHandle; // cached EpicPack bag FItemHandle

        // Hotbar overlay: Win32 transparent bar at top-center of screen
        bool m_showHotbar{true}; // ON by default

        // Experimental UMG toolbar
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

        // Mod Controller toolbar — 4x2 grid, lower-right of screen
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
        bool m_characterHidden{false};             // toggle state: is player character hidden?
        bool m_flyMode{false};                     // toggle state: is fly mode active?

        // Stash container repair — run once per character load, not on every retry scan
        bool m_repairDone{false};

        // Toolbar repositioning mode
        bool m_repositionMode{false};              // are we in drag-to-reposition mode?
        int  m_dragToolbar{-1};                    // -1=none, 0=builders, 1=AB, 2=MC, 3=infobox
        float m_dragOffsetX{0}, m_dragOffsetY{0};  // cursor-to-position offset at grab start
        UObject* m_repositionMsgWidget{nullptr};   // centered instruction message
        UObject* m_repositionInfoBoxWidget{nullptr}; // placeholder info box during reposition
        // Toolbar positions as viewport coordinates (0.0-1.0); -1 = use default
        // Index: 0=BuildersBar, 1=AdvancedBuilder, 2=ModController, 3=InfoBox
        static constexpr int TB_COUNT = 4;
        float m_toolbarPosX[TB_COUNT]{-1, -1, -1, -1};
        float m_toolbarPosY[TB_COUNT]{-1, -1, -1, -1};
        // Cached sizes in viewport pixels (set during creation, used for drag hit-test)
        float m_toolbarSizeW[TB_COUNT]{0, 0, 0, 0};
        float m_toolbarSizeH[TB_COUNT]{0, 0, 0, 0};
        // Alignment pivots — all center-based for intuitive dragging
        float m_toolbarAlignX[TB_COUNT]{0.5f, 0.5f, 0.5f, 0.5f};
        float m_toolbarAlignY[TB_COUNT]{0.5f, 0.5f, 0.5f, 0.5f};
        // Default positions (from 4K tuning): BB=top-center, AB=lower-right, MC=right-mid, IB=right-center
        static constexpr float TB_DEF_X[TB_COUNT]{0.4992f, 0.7505f, 0.8492f, 0.9414f};
        static constexpr float TB_DEF_Y[TB_COUNT]{0.0287f, 0.9111f, 0.6148f, 0.5463f};
        // UMG Target Info popup
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
        // isReadableMemory — defined in moria_testable.h

        // Safe wrapper for GetClassPrivate()->GetName() — returns empty string on null
        static std::wstring safeClassName(UObject* obj)
        {
            if (!obj) return L"";
            auto* cls = obj->GetClassPrivate();
            if (!cls) return L"";
            return std::wstring(cls->GetName());
        }

        // ── 6G: Quick-Build System ────────────────────────────────────────────
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

        // Check if a UObject is safe to call ProcessEvent on (not pending GC destruction)
        static bool isWidgetAlive(UObject* obj)
        {
            if (!obj) return false;
            if (obj->HasAnyFlags(static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed))) return false;
            if (obj->IsUnreachable()) return false;
            return true;
        }

        // Cached Build_Tab lookup — avoids FindAllOf on every check
        UObject* getCachedBuildTab()
        {
            if (m_cachedBuildTab)
            {
                if (!isWidgetAlive(m_cachedBuildTab) || safeClassName(m_cachedBuildTab) != L"UI_WBP_Build_Tab_C")
                {
                    m_cachedBuildTab = nullptr;
                    m_fnIsShowing = nullptr;
                }
            }
            if (!m_cachedBuildTab)
            {
                m_cachedBuildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
                if (m_cachedBuildTab && !isWidgetAlive(m_cachedBuildTab))
                    m_cachedBuildTab = nullptr; // findWidgetByClass returned a dying widget
                if (m_cachedBuildTab)
                    m_fnIsShowing = m_cachedBuildTab->GetFunctionByNameInChain(STR("IsShowing"));
            }
            return m_cachedBuildTab;
        }

        // Cheap Build_Tab visibility check via cached IsShowing() — no FindAllOf
        bool isBuildTabShowing()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab || !m_fnIsShowing) return false;
            struct { bool Ret{false}; } params{};
            tab->ProcessEvent(m_fnIsShowing, &params);
            return params.Ret;
        }

        // Cached BuildHUD lookup
        UObject* getCachedBuildHUD()
        {
            if (m_cachedBuildHUD)
            {
                if (!isWidgetAlive(m_cachedBuildHUD) || safeClassName(m_cachedBuildHUD).find(L"BuildHUD") == std::wstring::npos)
                    m_cachedBuildHUD = nullptr;
            }
            if (!m_cachedBuildHUD)
            {
                m_cachedBuildHUD = findWidgetByClass(L"UI_WBP_BuildHUDv2_C", false);
                if (m_cachedBuildHUD && !isWidgetAlive(m_cachedBuildHUD))
                    m_cachedBuildHUD = nullptr;
            }
            return m_cachedBuildHUD;
        }

        // Live placement-active check: BuildHUD is showing AND not in recipe select mode
        // Replaces the mod-maintained m_buildPlacementActive flag
        bool isPlacementActive()
        {
            UObject* hud = getCachedBuildHUD();
            if (!hud) return false;
            // Check IsShowing via ProcessEvent
            auto* fn = hud->GetFunctionByNameInChain(STR("IsShowing"));
            if (!fn) return false;
            struct { bool Ret{false}; } params{};
            hud->ProcessEvent(fn, &params);
            if (!params.Ret) return false;
            // HUD is showing — check if past the recipe picker
            int rsOff = resolveOffset(hud, L"recipeSelectMode", s_off_recipeSelectMode);
            if (rsOff < 0) return false;
            bool recipeSelectMode = *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(hud) + rsOff);
            return !recipeSelectMode; // in placement if HUD showing but NOT selecting recipes
        }

        // Force the game's action bar (hotbar UI) to refresh its display
        void refreshActionBar()
        {
            UObject* actionBar = findWidgetByClass(L"WBP_UI_ActionBar_C", true);
            if (!actionBar)
                return;

            auto* refreshFunc = actionBar->GetFunctionByNameInChain(STR("Set All Action Bar Items"));
            if (refreshFunc)
            {
                actionBar->ProcessEvent(refreshFunc, nullptr);
                VLOG(STR("[MoriaCppMod] ActionBar: Set All Action Bar Items called\n"));
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
            // NOTE: rotationStep now persisted in MoriaCppMod.ini [Preferences], not here
        }

        void loadQuickBuildSlots()
        {
            std::ifstream file("Mods/MoriaCppMod/quickbuild_slots.txt");
            if (!file.is_open()) return;
            std::string line;
            int loaded = 0;
            while (std::getline(file, line))
            {
                auto parsed = parseSlotLine(line);
                if (auto* slot = std::get_if<ParsedSlot>(&parsed))
                {
                    m_recipeSlots[slot->slotIndex].displayName = std::wstring(slot->displayName.begin(), slot->displayName.end());
                    m_recipeSlots[slot->slotIndex].textureName = std::wstring(slot->textureName.begin(), slot->textureName.end());
                    m_recipeSlots[slot->slotIndex].used = true;
                    loaded++;
                }
                else if (auto* rot = std::get_if<ParsedRotation>(&parsed))
                {
                    s_overlay.rotationStep = rot->step;
                }
            }
            if (loaded > 0)
            {
                VLOG(STR("[MoriaCppMod] Loaded {} quick-build slots from disk\n"), loaded);
                updateOverlayText();
                updateBuildersBar();
            }
        }

        void saveConfig()
        {
            std::ofstream file(INI_PATH, std::ios::trunc);
            if (!file.is_open()) return;

            file << "; MoriaCppMod Configuration\n";
            file << "; Key names: F1-F12, Num0-Num9, Num+, Num-, Num*, Num/, A-Z, 0-9,\n";
            file << ";   PgUp, PgDn, Home, End, Ins, Del, Space, Tab, Enter,\n";
            file << ";   [ ] \\ ; = , - . / ` '\n";
            file << "; Modifier: SHIFT, CTRL, ALT, RALT\n";
            file << "\n";

            file << "[Keybindings]\n";
            for (int i = 0; i < BIND_COUNT; i++)
            {
                const char* iniKey = bindIndexToIniKey(i);
                if (!iniKey) continue;
                std::wstring wname = keyName(s_bindings[i].key);
                std::string name;
                for (auto wc : wname) name += static_cast<char>(wc); // ASCII-safe narrow
                file << iniKey << " = " << name << "\n";
            }
            file << "ModifierKey = " << modifierToIniName(s_modifierVK) << "\n";

            file << "\n[Preferences]\n";
            file << "Verbose = " << (s_verbose ? "true" : "false") << "\n";
            file << "RotationStep = " << s_overlay.rotationStep.load() << "\n";
            file << "Language = " << s_language << "\n";

            // Only write [Positions] if user has customized at least one toolbar
            bool hasCustomPos = false;
            for (int i = 0; i < TB_COUNT; i++)
                if (m_toolbarPosX[i] >= 0) hasCustomPos = true;
            if (hasCustomPos)
            {
                file << "\n[Positions]\n";
                file << "; Toolbar positions as viewport fractions (0.0-1.0)\n";
                file << "; Delete this section to reset to defaults\n";
                const char* tbNames[TB_COUNT] = {"BuildersBar", "AdvancedBuilder", "ModController", "InfoBox"};
                for (int i = 0; i < TB_COUNT; i++)
                {
                    float fx = (m_toolbarPosX[i] >= 0) ? m_toolbarPosX[i] : TB_DEF_X[i];
                    float fy = (m_toolbarPosY[i] >= 0) ? m_toolbarPosY[i] : TB_DEF_Y[i];
                    file << tbNames[i] << "X = " << std::fixed << std::setprecision(4) << fx << "\n";
                    file << tbNames[i] << "Y = " << std::fixed << std::setprecision(4) << fy << "\n";
                }
            }

            VLOG(STR("[MoriaCppMod] Saved config to MoriaCppMod.ini\n"));
        }

        void loadConfig()
        {
            std::ifstream file(INI_PATH);
            if (file.is_open())
            {
                // Parse INI file
                std::string section;
                std::string line;
                int loaded = 0;
                while (std::getline(file, line))
                {
                    auto parsed = parseIniLine(line);
                    if (auto* sec = std::get_if<ParsedIniSection>(&parsed))
                    {
                        section = sec->name;
                    }
                    else if (auto* kv = std::get_if<ParsedIniKeyValue>(&parsed))
                    {
                        if (strEqualCI(section, "Keybindings"))
                        {
                            if (strEqualCI(kv->key, "ModifierKey"))
                            {
                                std::wstring wval(kv->value.begin(), kv->value.end());
                                auto mvk = modifierNameToVK(wval);
                                if (mvk) s_modifierVK = *mvk;
                            }
                            else
                            {
                                int idx = iniKeyToBindIndex(kv->key);
                                if (idx >= 0)
                                {
                                    std::wstring wval(kv->value.begin(), kv->value.end());
                                    auto vk = nameToVK(wval);
                                    if (vk)
                                    {
                                        s_bindings[idx].key = *vk;
                                        loaded++;
                                    }
                                    else
                                    {
                                        VLOG(STR("[MoriaCppMod] INI: unrecognized key '{}' for {}\n"),
                                             std::wstring(kv->value.begin(), kv->value.end()),
                                             std::wstring(kv->key.begin(), kv->key.end()));
                                    }
                                }
                            }
                        }
                        else if (strEqualCI(section, "Preferences"))
                        {
                            if (strEqualCI(kv->key, "Verbose"))
                            {
                                s_verbose = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
                            }
                            else if (strEqualCI(kv->key, "RotationStep"))
                            {
                                try
                                {
                                    int val = std::stoi(kv->value);
                                    if (val >= 0 && val <= 90) s_overlay.rotationStep = val;
                                }
                                catch (...) {}
                            }
                            else if (strEqualCI(kv->key, "Language"))
                            {
                                if (!kv->value.empty()) s_language = kv->value;
                            }
                        }
                        else if (strEqualCI(section, "Positions"))
                        {
                            const char* tbNames[TB_COUNT] = {"BuildersBar", "AdvancedBuilder", "ModController", "InfoBox"};
                            for (int i = 0; i < TB_COUNT; i++)
                            {
                                try
                                {
                                    if (strEqualCI(kv->key, std::string(tbNames[i]) + "X"))
                                    {
                                        float val = std::stof(kv->value);
                                        if (val >= 0.0f && val <= 1.0f) m_toolbarPosX[i] = val;
                                    }
                                    else if (strEqualCI(kv->key, std::string(tbNames[i]) + "Y"))
                                    {
                                        float val = std::stof(kv->value);
                                        if (val >= 0.0f && val <= 1.0f) m_toolbarPosY[i] = val;
                                    }
                                }
                                catch (...) {}
                            }
                        }
                    }
                }
                if (loaded > 0)
                {
                    VLOG(STR("[MoriaCppMod] Loaded {} keybindings from MoriaCppMod.ini\n"), loaded);
                }
                return;
            }

            // Migration: try old keybindings.txt format
            std::ifstream oldFile(OLD_KEYBIND_PATH);
            if (oldFile.is_open())
            {
                std::string line;
                int loaded = 0;
                while (std::getline(oldFile, line))
                {
                    auto parsed = parseKeybindLine(line);
                    if (auto* kb = std::get_if<ParsedKeybind>(&parsed))
                    {
                        s_bindings[kb->bindIndex].key = kb->vkCode;
                        loaded++;
                    }
                    else if (auto* mod = std::get_if<ParsedModifier>(&parsed))
                    {
                        s_modifierVK = mod->vkCode;
                    }
                }
                oldFile.close();
                VLOG(STR("[MoriaCppMod] Migrated {} keybindings from keybindings.txt\n"), loaded);

                // Write new INI and rename old file
                saveConfig();
                std::rename(OLD_KEYBIND_PATH, "Mods/MoriaCppMod/keybindings.txt.bak");
                return;
            }

            // First run: write default INI from hardcoded s_bindings
            saveConfig();
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
                // widget→Icon → Brush.ResourceObject(MID) → TextureParameterValues[0]+16 → UTexture2D*
                UObject* texture = nullptr;
                {
                    uint8_t* base = reinterpret_cast<uint8_t*>(widget);
                    if (s_off_icon == -2) resolveOffset(widget, L"Icon", s_off_icon);
                    UObject* iconImg = (s_off_icon >= 0) ? *reinterpret_cast<UObject**>(base + s_off_icon) : nullptr;
                    if (iconImg && isReadableMemory(iconImg, 400))
                    {
                        ensureBrushOffset(iconImg);
                        if (s_off_brush >= 0)
                        {
                            uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImg);
                            UObject* mid = *reinterpret_cast<UObject**>(imgBase + s_off_brush + BRUSH_RESOURCE_OBJECT);
                            if (mid && isReadableMemory(mid, 280))
                            {
                                if (s_off_texParamValues == -2) resolveOffset(mid, L"TextureParameterValues", s_off_texParamValues);
                                if (s_off_texParamValues >= 0)
                                {
                                    uint8_t* midBase = reinterpret_cast<uint8_t*>(mid);
                                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + s_off_texParamValues);
                                    int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + s_off_texParamValues + 8);
                                    if (arrNum >= 1 && arrNum <= 32 && arrData && isReadableMemory(arrData, 40))
                                    {
                                        texture = *reinterpret_cast<UObject**>(arrData + TEX_PARAM_VALUE_PTR);
                                        if (texture && !isReadableMemory(texture, 64)) texture = nullptr;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!texture)
                {
                    VLOG(STR("[MoriaCppMod] [Icon] UTexture2D not found from widget chain\n"));
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] UTexture2D: {} '{}'\n"), safeClassName(texture), std::wstring(texture->GetName()));

                // --- Find required UFunctions ---
                auto* createRTFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:CreateRenderTarget2D"));
                auto* beginDrawFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:BeginDrawCanvasToRenderTarget"));
                auto* endDrawFn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:EndDrawCanvasToRenderTarget"));
                auto* exportRTFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:ExportRenderTarget"));
                auto* drawTexFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.Canvas:K2_DrawTexture"));

                VLOG(STR("[MoriaCppMod] [Icon] CreateRT={} BeginDraw={} EndDraw={} ExportRT={} K2_DrawTex={}\n"),
                                                createRTFn ? STR("YES") : STR("no"),
                                                beginDrawFn ? STR("YES") : STR("no"),
                                                endDrawFn ? STR("YES") : STR("no"),
                                                exportRTFn ? STR("YES") : STR("no"),
                                                drawTexFn ? STR("YES") : STR("no"));

                if (!createRTFn || !beginDrawFn || !endDrawFn || !exportRTFn || !drawTexFn)
                {
                    VLOG(STR("[MoriaCppMod] [Icon] Missing required UFunctions\n"));
                    return false;
                }

                // Log parameter layouts for all functions (first time only)
                static bool s_loggedParams = false;
                if (!s_loggedParams)
                {
                    s_loggedParams = true;
                    for (auto* fn : {createRTFn, beginDrawFn, endDrawFn, drawTexFn, exportRTFn})
                    {
                        VLOG(STR("[MoriaCppMod] [Icon] {} ParmsSize={}:\n"), std::wstring(fn->GetName()), fn->GetParmsSize());
                        for (auto* prop : fn->ForEachProperty())
                        {
                            VLOG(STR("[MoriaCppMod] [Icon]   {} @{} sz={} {}\n"),
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
                    VLOG(STR("[MoriaCppMod] [Icon] No PlayerController\n"));
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
                    VLOG(STR("[MoriaCppMod] [Icon] CreateRenderTarget2D returned null\n"));
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] Created render target OK\n"));

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
                    VLOG(STR("[MoriaCppMod] [Icon] BeginDrawCanvasToRenderTarget returned no Canvas\n"));
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] Got Canvas: {} '{}'\n"), safeClassName(canvas), std::wstring(canvas->GetName()));

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
                    VLOG(STR("[MoriaCppMod] [Icon] Drew texture to canvas\n"));
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
                    VLOG(STR("[MoriaCppMod] [Icon] EndDrawCanvasToRenderTarget OK\n"));
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
                    VLOG(STR("[MoriaCppMod] [Icon] Exporting to folder='{}' file='{}'\n"), folder, fileName);

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
                    VLOG(STR("[MoriaCppMod] [Icon] ExportRenderTarget called\n"));
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
                        VLOG(STR("[MoriaCppMod] [Icon] Found file: {} ({} bytes)\n"), candidate, fsize);
                        if (fsize > 0)
                        {
                            exportedPath = candidate;
                            break;
                        }
                    }
                }

                if (exportedPath.empty())
                {
                    VLOG(STR("[MoriaCppMod] [Icon] No exported file found (or 0 bytes)\n"));
                    for (const wchar_t* ext : {L".hdr", L".bmp", L".png", L".exr", L""})
                    {
                        std::wstring candidate = baseName + ext;
                        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(candidate.c_str());
                    }
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] Found exported file: {}\n"), exportedPath);

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
                                        VLOG(STR("[MoriaCppMod] [Icon] Converted to PNG: {}\n"), outPath);
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
                VLOG(STR("[MoriaCppMod] [Icon] PNG conversion failed\n"));
                return false;
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [Icon] Exception during extraction\n"));
                return false;
            }
        }

        // ── 6H: Icon Extraction & Build-from-Target ──────────────────────────
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
                if (s_off_icon == -2) resolveOffset(widget, L"Icon", s_off_icon);
                if (s_off_icon < 0) return L"";
                UObject* iconImg = *reinterpret_cast<UObject**>(base + s_off_icon);
                if (!iconImg || !isReadableMemory(iconImg, 400)) return L"";
                // MID via Brush.ResourceObject
                ensureBrushOffset(iconImg);
                if (s_off_brush < 0) return L"";
                uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImg);
                UObject* mid = *reinterpret_cast<UObject**>(imgBase + s_off_brush + BRUSH_RESOURCE_OBJECT);
                if (!mid || !isReadableMemory(mid, 280)) return L"";
                // TextureParameterValues TArray on MID
                if (s_off_texParamValues == -2) resolveOffset(mid, L"TextureParameterValues", s_off_texParamValues);
                if (s_off_texParamValues < 0) return L"";
                uint8_t* midBase = reinterpret_cast<uint8_t*>(mid);
                uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + s_off_texParamValues);
                int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + s_off_texParamValues + 8);
                if (arrNum < 1 || arrNum > 32 || !arrData || !isReadableMemory(arrData, 40)) return L"";
                // UTexture2D* at entry+16
                UObject* texPtr = *reinterpret_cast<UObject**>(arrData + TEX_PARAM_VALUE_PTR);
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

        // Log bLock diagnostics for recipe differentiation.
        // Dumps Tag, CategoryTag.Tag, Variants RowName, and hex of key regions.
        void logBLockDiagnostics(const wchar_t* context, const std::wstring& displayName, const uint8_t* bLock)
        {
            if (!bLock)
            {
                VLOG(STR("[MoriaCppMod] [{}] '{}' — no bLock data\n"), context, displayName);
                return;
            }

            // bLock+0x00: Tag (FGameplayTag = FName, 8B)
            uint32_t tagCI = *reinterpret_cast<const uint32_t*>(bLock);
            int32_t tagNum = *reinterpret_cast<const int32_t*>(bLock + 4);

            // bLock+0x20: CategoryTag.Tag (FGameplayTag = FName, 8B)
            //   CategoryTag starts at +0x08, inherits FFGKTableRowBase (+0x18), so Tag is at +0x08+0x18=+0x20
            uint32_t catTagCI = *reinterpret_cast<const uint32_t*>(bLock + 0x20);
            int32_t catTagNum = *reinterpret_cast<const int32_t*>(bLock + 0x24);

            // bLock+0x60: CategoryTag.SortOrder (int32)
            int32_t sortOrder = *reinterpret_cast<const int32_t*>(bLock + 0x60);

            VLOG(STR("[MoriaCppMod] [{}] '{}' Tag CI={} Num={} | CatTag CI={} Num={} | SortOrder={}\n"),
                 context, displayName, tagCI, tagNum, catTagCI, catTagNum, sortOrder);

            // Variants TArray at bLock+0x68
            const uint8_t* variantsPtr = *reinterpret_cast<const uint8_t* const*>(bLock + RECIPE_BLOCK_VARIANTS);
            int32_t variantsCount = *reinterpret_cast<const int32_t*>(bLock + RECIPE_BLOCK_VARIANTS_NUM);

            if (variantsCount > 0 && variantsPtr && isReadableMemory(variantsPtr, 0xE8))
            {
                uint32_t rowCI = *reinterpret_cast<const uint32_t*>(variantsPtr + VARIANT_ROW_CI);
                int32_t rowNum = *reinterpret_cast<const int32_t*>(variantsPtr + VARIANT_ROW_NUM);
                VLOG(STR("[MoriaCppMod] [{}]   Variants={} RowName CI={} Num={}\n"),
                     context, variantsCount, rowCI, rowNum);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [{}]   Variants={} (no readable data)\n"), context, variantsCount);
            }

            // Hex dump: first 16 bytes (Tag + start of CategoryTag) and bytes 0x60-0x77 (SortOrder + Variants)
            auto hexRow = [](const uint8_t* p, int len) -> std::wstring {
                std::wstring h;
                for (int i = 0; i < len; i++)
                {
                    wchar_t buf[4];
                    swprintf(buf, 4, L"%02X ", p[i]);
                    h += buf;
                }
                return h;
            };
            VLOG(STR("[MoriaCppMod] [{}]   hex[00-0F]: {}\n"), context, hexRow(bLock, 16));
            VLOG(STR("[MoriaCppMod] [{}]   hex[20-2F]: {}\n"), context, hexRow(bLock + 0x20, 16));
            VLOG(STR("[MoriaCppMod] [{}]   hex[60-77]: {}\n"), context, hexRow(bLock + 0x60, 24));
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
                    VLOG(STR("[MoriaCppMod] [QuickBuild] CLEARED F{}\n"), slot + 1);
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
                    VLOG(STR("[MoriaCppMod] [QuickBuild] F{} icon: '{}'\n"), slot + 1, m_recipeSlots[slot].textureName);

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
                            VLOG(STR("[MoriaCppMod] [Icon] PNG already exists: {}\n"), pngPath);
                        }
                    }
                }
            }

            saveQuickBuildSlots();
            updateOverlayText();
            updateBuildersBar();

            VLOG(STR("[MoriaCppMod] [QuickBuild] ASSIGN F{} = '{}'\n"), slot + 1, m_lastCapturedName);
            logBLockDiagnostics(L"ASSIGN", m_lastCapturedName, m_recipeSlots[slot].bLockData);

            std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" = " + m_lastCapturedName;
            showOnScreen(msg.c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
        }

        // Read display name from a Build_Item_Medium widget's blockName TextBlock
        std::wstring readWidgetDisplayName(UObject* widget)
        {
            uint8_t* base = reinterpret_cast<uint8_t*>(widget);
            if (s_off_blockName == -2) resolveOffset(widget, L"blockName", s_off_blockName);
            if (s_off_blockName < 0) return L"";
            UObject* blockNameWidget = *reinterpret_cast<UObject**>(base + s_off_blockName);
            if (!blockNameWidget) return L"";

            auto* getTextFunc = blockNameWidget->GetFunctionByNameInChain(STR("GetText"));
            if (!getTextFunc || getTextFunc->GetParmsSize() != sizeof(FText)) return L"";

            FText textResult{};
            blockNameWidget->ProcessEvent(getTextFunc, &textResult);
            if (!textResult.Data) return L"";
            return textResult.ToString();
        }

        // Find a Build_Item_Medium widget whose display name matches, then trigger blockSelectedEvent
        // Returns true if recipe was found and selected, false if not found (allows retry)
        bool selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;

            VLOG(STR("[MoriaCppMod] [QuickBuild] SELECT: searching for '{}' (slot F{})\n"), targetName, slot + 1);

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

            VLOG(STR("[MoriaCppMod] [QuickBuild]   checked {} visible widgets, match: {}\n"), visibleCount, matchedWidget ? L"YES" : L"NO");

            if (!matchedWidget)
            {
                VLOG(STR("[MoriaCppMod] [QuickBuild] No match among {} visible widgets\n"), visibleCount);
                return false;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return false;

            // blockSelectedEvent params: bLock@0(120B) + selfRef@120(8B) + Index@128(4B)
            uint8_t params[132]{};
            bool gotFreshBLock = false;

            // Resolve bLock offset on first quickbuild (walks inheritance chain to UI_WBP_Build_Item_C)
            if (s_off_bLock == -2)
                resolveOffset(matchedWidget, L"bLock", s_off_bLock);

            // BEST: read fresh bLock directly from the matched widget (offset resolved via reflection)
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
                VLOG(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (@0x{:X})\n"), s_off_bLock);
            }

            // FALLBACK: use captured bLock from assignment (may have stale pointer at +104)
            if (!gotFreshBLock && m_recipeSlots[slot].hasBLockData)
            {
                std::memcpy(params, m_recipeSlots[slot].bLockData, BLOCK_DATA_SIZE);
                VLOG(STR("[MoriaCppMod] [QuickBuild]   using SAVED bLock (may be stale)\n"));
            }
            else if (!gotFreshBLock)
            {
                VLOG(STR("[MoriaCppMod] [QuickBuild]   WARNING: no bLock data at all, using zeros\n"));
            }

            *reinterpret_cast<UObject**>(params + 120) = matchedWidget;
            *reinterpret_cast<int32_t*>(params + 128) = 0;

            VLOG(STR("[MoriaCppMod] [QuickBuild]   calling blockSelectedEvent with selfRef={:p}\n"), static_cast<void*>(matchedWidget));

            // Suppress post-hook capture during automated selection (RAII guard ensures reset on exception)
            m_isAutoSelecting = true;
            struct AutoSelectGuard
            {
                bool& flag;
                ~AutoSelectGuard() { flag = false; }
            } guard{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params);
            m_isAutoSelecting = false;

            logBLockDiagnostics(L"BUILD", targetName, params);

            showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;       // track menu so we refresh ActionBar when it closes
            refreshActionBar();              // also refresh immediately after recipe selection

            // Set this slot as Active on the builders bar, all others become Inactive/Empty
            m_activeBuilderSlot = slot;
            updateBuildersBar();
            return true;
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

            // Guard: if a previous quickbuild is already in progress, skip
            if (m_qbPhase != QBPhase::Idle)
            {
                VLOG(STR("[MoriaCppMod] [QuickBuild] F{} pressed but phase {} active, ignoring\n"),
                                                slot + 1, static_cast<int>(m_qbPhase));
                return;
            }

            VLOG(STR("[MoriaCppMod] [QuickBuild] ACTIVATE F{} -> '{}' (charLoaded={} frameCounter={})\n"),
                                            slot + 1,
                                            m_recipeSlots[slot].displayName,
                                            m_characterLoaded,
                                            m_frameCounter);

            m_pendingQuickBuildSlot = slot;
            m_qbTimeout = 0;

            // Reactive phase transitions: check live game state and proceed accordingly
            if (isPlacementActive())
            {
                // Player is holding a ghost piece — cancel with ESC first
                VLOG(STR("[MoriaCppMod] [QuickBuild] Placement active — sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = QBPhase::CancelPlacement;
            }
            else if (isBuildTabShowing())
            {
                // Build menu is open — close it first, then reopen fresh
                VLOG(STR("[MoriaCppMod] [QuickBuild] Build tab already open — closing first\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = QBPhase::CloseMenu;
            }
            else
            {
                // Menu not open — open it
                VLOG(STR("[MoriaCppMod] [QuickBuild] Build tab not open — sending B key\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = QBPhase::OpenMenu;
            }
        }

        // ── Build from Target: Shift+F10 — build the last targeted actor ──

        void buildFromTarget()
        {
            VLOG(
                    STR("[MoriaCppMod] [TargetBuild] buildFromTarget() called: buildable={} name='{}' recipeRef='{}' charLoaded={} frame={}\n"),
                    m_lastTargetBuildable,
                    m_targetBuildName,
                    m_targetBuildRecipeRef,
                    m_characterLoaded,
                    m_frameCounter);

            // Guard: if a previous target-build is already in progress, skip
            if (m_tbPhase != QBPhase::Idle)
            {
                VLOG(STR("[MoriaCppMod] [TargetBuild] Already active (phase {}), ignoring\n"), static_cast<int>(m_tbPhase));
                return;
            }

            if (!m_lastTargetBuildable || (m_targetBuildName.empty() && m_targetBuildRecipeRef.empty()))
            {
                showOnScreen(Loc::get("msg.no_buildable_target").c_str(), 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            m_pendingTargetBuild = true;
            m_tbTimeout = 0;

            // Reactive phase transitions: check live game state
            if (isPlacementActive())
            {
                VLOG(STR("[MoriaCppMod] [TargetBuild] Placement active — sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_tbPhase = QBPhase::CancelPlacement;
            }
            else if (isBuildTabShowing())
            {
                VLOG(STR("[MoriaCppMod] [TargetBuild] Build tab already open — closing first\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_tbPhase = QBPhase::CloseMenu;
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [TargetBuild] Build tab not open — sending B key\n"));
                keybd_event(0x42, 0, 0, 0);
                keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                m_tbPhase = QBPhase::OpenMenu;
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
            VLOG(STR("[MoriaCppMod] [TargetBuild] Searching: name='{}' recipeRef='{}' bLockOffset=0x{:X}\n"),
                                            m_targetBuildName,
                                            m_targetBuildRecipeRef,
                                            s_off_bLock);

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

                VLOG(STR("[MoriaCppMod] [TargetBuild] FName CIs: full='{}' CI={}, short='{}' CI={}\n"),
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

                // Resolve bLock offset on first visible widget (walks inheritance chain)
                if (s_off_bLock == -2)
                    resolveOffset(w, L"bLock", s_off_bLock);

                // Log first 5 widgets in detail for diagnostics
                if (isFirstFew)
                {
                    std::wstring objName(w->GetName());
                    VLOG(STR("[MoriaCppMod] [TargetBuild]   W[{}] obj='{}' display='{}'\n"), visibleCount, objName, name);
                }

                // Strategy 1: bLock-based matching via Variants->ResultConstructionHandle RowName
                if (!matchedWidget && s_off_bLock >= 0 && !m_targetBuildRecipeRef.empty())
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(w);
                    uint8_t* bLock = widgetBase + s_off_bLock;

                    if (!isReadableMemory(bLock, BLOCK_DATA_SIZE))
                    {
                        bLockNullCount++;
                        if (isFirstFew) VLOG(STR("[MoriaCppMod] [TargetBuild]     bLock=NULL\n"));
                    }
                    else if (!isReadableMemory(bLock + RECIPE_BLOCK_VARIANTS, 16))
                    {
                        bLockMemFailCount++;
                        if (isFirstFew) VLOG(STR("[MoriaCppMod] [TargetBuild]     bLock+104 not readable\n"));
                    }
                    else
                    {
                        uint8_t* variantsPtr = *reinterpret_cast<uint8_t**>(bLock + RECIPE_BLOCK_VARIANTS);
                        int32_t variantsCount = *reinterpret_cast<int32_t*>(bLock + RECIPE_BLOCK_VARIANTS_NUM);

                        if (isFirstFew)
                        {
                            // Log first 8 bytes of bLock (FGameplayTag FName)
                            uint32_t tagCI = *reinterpret_cast<uint32_t*>(bLock);
                            int32_t tagNum = *reinterpret_cast<int32_t*>(bLock + 4);
                            VLOG(STR("[MoriaCppMod] [TargetBuild]     bLock tag CI={} Num={} | variants={} ptr={:p}\n"),
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
                            uint32_t rowCI = *reinterpret_cast<uint32_t*>(variantsPtr + VARIANT_ROW_CI);
                            int32_t rowNum = *reinterpret_cast<int32_t*>(variantsPtr + VARIANT_ROW_NUM);

                            if (isFirstFew)
                            {
                                VLOG(STR("[MoriaCppMod] [TargetBuild]     RowName CI={} Num={}\n"), rowCI, rowNum);
                            }

                            // Check if RowName CI matches any of our target FName CIs
                            for (auto& [tName, tCI] : targetFNames)
                            {
                                if (tCI == rowCI)
                                {
                                    matchedWidget = w;
                                    matchedName = name.empty() ? tName : name;
                                    VLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (bLock RowName CI={}) on '{}' target='{}'\n"),
                                                                    rowCI,
                                                                    matchedName,
                                                                    tName);
                                    break;
                                }
                            }
                        }
                        else if (isFirstFew)
                        {
                            VLOG(STR("[MoriaCppMod] [TargetBuild]     variantsPtr not readable (0xE8 bytes)\n"));
                        }
                    }
                }

                // Strategy 2: Exact display name match
                if (!matchedWidget && !name.empty() && name == m_targetBuildName)
                {
                    matchedWidget = w;
                    matchedName = name;
                    VLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (exact display name) on '{}'\n"), name);
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
                        VLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (normalized exact) on '{}'\n"), name);
                    }
                }

                if (matchedWidget) break;
            }

            VLOG(STR("[MoriaCppMod] [TargetBuild] Result: {} visible, bLockNull={} memFail={} varEmpty={} match={}\n"),
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
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
            }

            VLOG(STR("[MoriaCppMod] [TargetBuild] Calling blockSelectedEvent: freshBLock={} selfRef={:p}\n"),
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

            logBLockDiagnostics(L"TARGET-BUILD", matchedName, params);

            showOnScreen((L"Build: " + matchedName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;       // track menu so we refresh ActionBar when it closes
            refreshActionBar();              // also refresh immediately after recipe selection
        }

        // ── 6I: UMG Widget System ────────────────────────────────────────────
        // Runtime UMG widget creation via StaticConstructObject + ProcessEvent:
        //   - Action Bar (12-slot hotbar frame images in UHorizontalBox)
        //   - Advanced Builder Toolbar (lower-right toggle button)
        //   - Target Info Popup (right-side actor details panel)
        //   - Info Box Popup (removal confirmation messages)
        //   - Config Menu (3-tab modal: Optional Mods, Key Mapping, Hide Environment)
        //   - Mod Controller Toolbar (3x3 grid of action buttons)

        // Helper: set a UImage's brush to a texture via ProcessEvent
        void umgSetBrush(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            ensureBrushOffset(img); // resolve Brush property offset on first call
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

        // Helper: set position of a UUserWidget via SetPositionInViewport(FVector2D)
        void setWidgetPosition(UObject* widget, float x, float y)
        {
            if (!widget) return;
            auto* fn = widget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (!fn) return;
            auto* p = findParam(fn, STR("Position"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = x; v[1] = y;
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
        void umgSetBold(UObject* textBlock)
        {
            if (!textBlock) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            // Read current FSlateFontInfo from the TextBlock
            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, tbRaw + fontOff, FONT_STRUCT_SIZE);

            // Patch TypefaceFontName to "Bold"
            RC::Unreal::FName boldName(STR("Bold"), RC::Unreal::FNAME_Add);
            std::memcpy(fontBuf + FONT_TYPEFACE_NAME, &boldName, sizeof(RC::Unreal::FName));

            // Call SetFont with the patched FSlateFontInfo
            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        void umgSetFontSize(UObject* textBlock, int32_t fontSize)
        {
            if (!textBlock) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, tbRaw + fontOff, FONT_STRUCT_SIZE);
            std::memcpy(fontBuf + FONT_SIZE, &fontSize, sizeof(int32_t));

            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
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
            ensureBrushOffset(img);
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
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);

                // Get state icon size as the container bounds
                float containerW = 64.0f;
                float containerH = 64.0f;
                if (m_umgStateImages[slot])
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(m_umgStateImages[slot]);
                    containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} icon sized: {}x{} (container: {}x{}, tex: {}x{})\n"),
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
                            VLOG(STR("[MoriaCppMod] [UMG] Slot #{} icon set: {}\n"),
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
            VLOG(STR("[MoriaCppMod] [UMG] Bar removed from viewport\n"));
        }

        void createExperimentalBar()
        {
            if (m_umgBarWidget)
            {
                destroyExperimentalBar();
                showOnScreen(Loc::get("msg.umg_bar_removed").c_str(), 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [UMG] === Creating 8-slot toolbar ===\n"));

            // --- Phase A: Find all 4 textures ---
            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
                VLOG(STR("[MoriaCppMod] [UMG] Found {} Texture2D objects\n"), textures.size());
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
            VLOG(STR("[MoriaCppMod] [UMG] Textures: frame={} empty={} inactive={} active={} blankRect={}\n"),
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
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
                setRootWidget(widgetTree, outerBorder);

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
            VLOG(STR("[MoriaCppMod] [UMG] Creating 8 slot columns...\n"));
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
                    frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                    VLOG(STR("[MoriaCppMod] [UMG] Frame icon: {}x{}, State icon: {}x{}\n"),
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
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} created\n"), i);
            }

            // --- Phase D: Size frame from icon dimensions and center on screen ---
            // Get viewport size early for uiScale computation
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
            VLOG(STR("[MoriaCppMod] [UMG] Viewport: {}x{}\n"), viewW, viewH);
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // SetRenderScale: uniform 0.81 scaled by uiScale (matches AB/MC)
            float bbScale = 0.81f * uiScale;
            umgSetRenderScale(outerBorder, bbScale, bbScale);

            // Use the larger of frame/state width for column width
            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f; // fallback
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;                   // 15% vertical overlap
            float hOverlapPerSlot = iconW * 0.20f;             // 20% horizontal overlap (10% each side)
            // Viewport size matches render scale so invisible frame fits the visual
            float totalW = (8.0f * iconW - 7.0f * hOverlapPerSlot) * bbScale;
            float totalH = (frameH + stateH - vOverlap) * bbScale;
            VLOG(STR("[MoriaCppMod] [UMG] Frame size: {}x{} (iconW={} frameH={} stateH={})\n"),
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

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[0] >= 0) ? m_toolbarPosX[0] : TB_DEF_X[0];
                float fracY = (m_toolbarPosY[0] >= 0) ? m_toolbarPosY[0] : TB_DEF_Y[0];
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH));
            }

            // Cache size and alignment for repositioning hit-test
            m_toolbarSizeW[0] = totalW;
            m_toolbarSizeH[0] = totalH;
            m_umgBarWidget = userWidget;
            showOnScreen(Loc::get("msg.builders_bar_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [UMG] === Builders bar creation complete ===\n"));

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
            VLOG(STR("[MoriaCppMod] [AB] Advanced Builder toolbar removed\n"));
        }

        void createAdvancedBuilderBar()
        {
            if (m_abBarWidget) return; // already exists — persists until world unload

            VLOG(STR("[MoriaCppMod] [AB] === Creating Advanced Builder Toolbar ===\n"));

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
                    VLOG(STR("[MoriaCppMod] [AB] Tools_Icon found via StaticFindObject\n"));
                else
                    VLOG(STR("[MoriaCppMod] [AB] WARNING: Tools_Icon NOT found\n"));
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
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
                setRootWidget(widgetTree, outerBorder);

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
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                if (texW > 0.0f && texH > 0.0f)
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    float containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    float containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
            float frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
            float frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
            uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
            float stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
            float stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);

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
            // Get viewport size for uiScale
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            float abScale = 0.81f * uiScale;
            umgSetRenderScale(outerBorder, abScale, abScale);

            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;
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

            // Alignment: center pivot (0.5, 0.5) — consistent for drag repositioning
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[1] >= 0) ? m_toolbarPosX[1] : TB_DEF_X[1];
                float fracY = (m_toolbarPosY[1] >= 0) ? m_toolbarPosY[1] : TB_DEF_Y[1];
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH));
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[1] = abTotalW;
            m_toolbarSizeH[1] = abTotalH;
            m_abBarWidget = userWidget;
            showOnScreen(L"Advanced Builder toolbar created!", 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [AB] === Advanced Builder toolbar created ({}x{}) ===\n"),
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
            VLOG(STR("[MoriaCppMod] [TI] === Creating Target Info UMG widget ===\n"));

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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
                        setRootWidget(widgetTree, rootSizeBox);
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
                setRootWidget(widgetTree, rootBorder);
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
                if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 1040.0f; tb->ProcessEvent(wrapAtFn, wp.data()); }
                auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
                if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; tb->ProcessEvent(wrapFn, wp.data()); }
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
                return tb;
            };

            // Title
            m_tiTitleLabel = makeTextBlock(Loc::get("ui.target_info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
            // Separator (thin text line)
            makeTextBlock(L"────────────────────────────────", 0.31f, 0.51f, 0.78f, 0.5f);
            // Data rows
            m_tiClassLabel   = makeTextBlock(Loc::get("ui.label_class"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiNameLabel    = makeTextBlock(Loc::get("ui.label_name"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiDisplayLabel = makeTextBlock(Loc::get("ui.label_display"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiPathLabel    = makeTextBlock(Loc::get("ui.label_path"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiBuildLabel   = makeTextBlock(Loc::get("ui.label_build"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiRecipeLabel  = makeTextBlock(Loc::get("ui.label_recipe"), 0.86f, 0.90f, 0.96f, 0.9f);

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

            // Get viewport size for uiScale
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Apply render scale for resolution independence
            if (rootSizeBox) umgSetRenderScale(rootSizeBox, uiScale, uiScale);

            // Set desired size (scaled)
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1100.0f * uiScale; v[1] = 320.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Alignment: center pivot (matches InfoBox)
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH));
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_targetInfoWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [TI] Target Info UMG widget created\n"));
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

            // Update text labels (wrapText inserts newlines for lines > 70 chars)
            umgSetText(m_tiClassLabel, wrapText(Loc::get("ui.value_class_prefix"), cls));
            umgSetText(m_tiNameLabel, wrapText(Loc::get("ui.value_name_prefix"), name));
            umgSetText(m_tiDisplayLabel, wrapText(Loc::get("ui.value_display_prefix"), display));
            umgSetText(m_tiPathLabel, wrapText(Loc::get("ui.value_path_prefix"), path));
            std::wstring buildStr = buildable ? Loc::get("ui.yes") : Loc::get("ui.no");
            umgSetText(m_tiBuildLabel, Loc::get("ui.value_build_prefix") + buildStr);
            if (buildable)
                umgSetTextColor(m_tiBuildLabel, 0.31f, 0.86f, 0.31f, 1.0f);
            else
                umgSetTextColor(m_tiBuildLabel, 0.7f, 0.55f, 0.39f, 0.8f);
            std::wstring recipeDisplay = !rowName.empty() ? rowName : recipe;
            umgSetText(m_tiRecipeLabel, recipeDisplay.empty() ? L"" : wrapText(Loc::get("ui.value_recipe_prefix"), recipeDisplay));

            // Reposition to current saved position (may have changed via drag)
            {
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
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_targetInfoWidget, fracX * static_cast<float>(viewW),
                                                      fracY * static_cast<float>(viewH));
            }

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
                VLOG(STR("[MoriaCppMod] Target info copied to clipboard\n"));
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
            VLOG(STR("[MoriaCppMod] [IB] === Creating Info Box UMG widget ===\n"));

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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root border (dark blue bg — same as Target Info)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootBorder);

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

            m_ibTitleLabel   = makeTextBlock(Loc::get("ui.info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
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

            // Get viewport size for uiScale
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Apply render scale for resolution independence
            if (vbox) umgSetRenderScale(vbox, uiScale, uiScale);

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 400.0f * uiScale; v[1] = 80.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH));
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[3] = 400.0f * uiScale;
            m_toolbarSizeH[3] = 80.0f * uiScale;

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_infoBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [IB] Info Box UMG widget created\n"));
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

            // Reposition to current saved position (may have changed via drag)
            {
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
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_infoBoxWidget, fracX * static_cast<float>(viewW),
                                                   fracY * static_cast<float>(viewH));
            }

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
                    std::wstring row = std::wstring(s_bindings[i].label) + Loc::get("ui.key_separator") + keyName(s_bindings[i].key);
                    umgSetText(m_cfgKeyValueLabels[i], row);
                }
                // Update new key box labels
                if (m_cfgKeyBoxLabels[i])
                {
                    if (capturing == i)
                    {
                        umgSetText(m_cfgKeyBoxLabels[i], Loc::get("ui.press_key"));
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
                std::wstring modText = Loc::get("ui.set_modifier_key") + std::wstring(modifierName(s_modifierVK));
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
                umgSetText(m_cfgFreeBuildLabel, on ? Loc::get("ui.free_build_on") : Loc::get("ui.free_build"));
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
                umgSetText(m_cfgRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
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
                umgSetText(m_cfgRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
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
                            std::wstring coordText = entry.isTypeRule ? Loc::get("ui.type_rule") : entry.coordsW;
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
            VLOG(STR("[MoriaCppMod] [CFG] === Creating Config UMG widget ===\n"));

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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Make widget focusable for modal input mode
            uint8_t* uwRaw = reinterpret_cast<uint8_t*>(userWidget);
            int focOff = resolveOffset(userWidget, L"bIsFocusable", s_off_bIsFocusable);
            if (focOff >= 0) uwRaw[focOff] |= 0x02;

            // Root SizeBox to enforce fixed 1400x450 size
            FStaticConstructObjectParameters rootSbP(sizeBoxClass, outer);
            UObject* rootSizeBox = UObjectGlobals::StaticConstructObject(rootSbP);
            if (!rootSizeBox) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootSizeBox);
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
                VLOG(STR("[MoriaCppMod] [CFG] Vignette texture not found, skipping border\n"));

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
            addTB(mainVBox, Loc::get("ui.config_title"), 0.78f, 0.86f, 1.0f, 1.0f, 36);
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
                    const wchar_t* tabNames[3] = {CONFIG_TAB_NAMES[0], CONFIG_TAB_NAMES[1], CONFIG_TAB_NAMES[2]};

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

                    addTB(tab0VBox, Loc::get("ui.cheat_toggles"), 0.78f, 0.86f, 1.0f, 1.0f, 32);

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
                            UObject* fbLabel = makeTB(Loc::get("ui.free_build"), 0.55f, 0.55f, 0.55f, 1.0f, 26);
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

                    addTB(tab0VBox, Loc::get("ui.free_build_desc"), 0.47f, 0.55f, 0.71f, 0.6f, 24);
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
                                UObject* btnLabel = makeTB(Loc::get("ui.unlock_all_recipes"), 0.86f, 0.90f, 1.0f, 0.95f, 26);
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
                            UObject* modLabel = makeTB(Loc::get("ui.set_modifier_key_short"), 0.86f, 0.90f, 0.96f, 0.85f, 26);
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
                    m_cfgRemovalHeader = addTB(tab2VBox, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"),
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

            // Get viewport size for uiScale
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Apply render scale for resolution independence (scales entire widget tree)
            if (rootSizeBox) umgSetRenderScale(rootSizeBox, uiScale, uiScale);

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1400.0f * uiScale; v[1] = 900.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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

            // Position: centered, scaled Y offset
            {
                float posX = static_cast<float>(viewW) / 2.0f;
                float posY = static_cast<float>(viewH) / 2.0f - 100.0f * uiScale;
                setWidgetPosition(userWidget, posX, posY);
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_configWidget = userWidget;
            updateConfigFreeBuild();
            VLOG(STR("[MoriaCppMod] [CFG] Config UMG widget created\n"));
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
            VLOG(STR("[MoriaCppMod] [MC] Mod Controller bar removed\n"));
        }

        void createModControllerBar()
        {
            if (m_mcBarWidget)
            {
                destroyModControllerBar();
                showOnScreen(Loc::get("msg.mc_removed").c_str(), 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [MC] === Creating 4x2 Mod Controller toolbar ===\n"));

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
            UObject* texHideChar = nullptr;     // T_UI_Eye_Open — MC slot 3 (Hide Character)
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
                    else if (name == STR("T_UI_Eye_Open")) texHideChar = t;
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
                {texHideChar, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Eye_Open.T_UI_Eye_Open"), L"T_UI_Eye_Open"},
            };
            for (auto& fb : fallbacks)
            {
                if (!fb.ref)
                {
                    fb.ref = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, fb.path);
                    if (fb.ref)
                        VLOG(STR("[MoriaCppMod] [MC] {} found via StaticFindObject fallback\n"), fb.name);
                    else
                        VLOG(STR("[MoriaCppMod] [MC] WARNING: {} NOT found via FindAllOf or StaticFindObject\n"), fb.name);
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
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
                setRootWidget(widgetTree, outerBorder);

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
                        frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                        stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        VLOG(STR("[MoriaCppMod] [MC] Frame: {}x{}, State: {}x{}\n"),
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
            VLOG(STR("[MoriaCppMod] [MC] All 8 slots created (4x2)\n"));

            // --- Set custom icons for all 8 MC slots ---
            {
                UObject* mcSlotTextures[MC_SLOTS] = {
                    texRotation, texTarget, texToolbarSwap, texHideChar,
                    texRemoveTarget, texUndoLast, texRemoveAll, texSettings
                };
                const wchar_t* mcSlotNames[MC_SLOTS] = {
                    L"T_UI_Refresh", L"T_UI_Search", L"Swap-Bag_Icon", L"T_UI_Eye_Open",
                    L"T_UI_Icon_GoodPlace2", L"T_UI_Alert_BakedIcon", L"T_UI_Icon_Filled_GoodPlace2", L"T_UI_Icon_Settings"
                };
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (!mcSlotTextures[i]) continue; // skip slots with no icon
                    if (m_mcIconImages[i] && setBrushFn)
                    {
                        umgSetBrush(m_mcIconImages[i], mcSlotTextures[i], setBrushFn);
                        umgSetOpacity(m_mcIconImages[i], 1.0f);
                        // Switch state image from empty to active frame for slots with icons
                        if (m_mcStateImages[i] && texActive)
                            umgSetBrush(m_mcStateImages[i], texActive, setBrushFn);
                        // Scale icon to 70% with aspect ratio preservation
                        uint8_t* iBase = reinterpret_cast<uint8_t*>(m_mcIconImages[i]);
                        float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        if (texW > 0.0f && texH > 0.0f)
                        {
                            // Get state icon size as container bounds
                            float containerW = 64.0f, containerH = 64.0f;
                            if (m_mcStateImages[i])
                            {
                                uint8_t* sBase = reinterpret_cast<uint8_t*>(m_mcStateImages[i]);
                                containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                                containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                            }
                            if (containerW < 1.0f) containerW = 64.0f;
                            if (containerH < 1.0f) containerH = 64.0f;
                            float scaleX = containerW / texW;
                            float scaleY = containerH / texH;
                            float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.70f;
                            umgSetBrushSize(m_mcIconImages[i], texW * scale, texH * scale);
                        }
                        VLOG(STR("[MoriaCppMod] [MC] Slot {} icon set to {}\n"), i, mcSlotNames[i]);
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [MC] WARNING: {} texture not found for slot {}\n"), mcSlotNames[i], i);
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
                        VLOG(STR("[MoriaCppMod] [MC] Rotation label created on slot 0\n"));
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
                        VLOG(STR("[MoriaCppMod] [MC] 'Single' label created on slot 4\n"));
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
                        VLOG(STR("[MoriaCppMod] [MC] 'All' label created on slot 6\n"));
                    }
                }
            }

            // --- Size and position: lower-right of screen ---
            // Get viewport size for uiScale
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // SetRenderScale 0.81 * uiScale
            float mcScale = 0.81f * uiScale;
            umgSetRenderScale(outerBorder, mcScale, mcScale);

            float mcIconW = (frameW > stateW) ? frameW : stateW;
            if (mcIconW < 1.0f) mcIconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float mcVOverlap = stateH * 0.25f;                     // 25% vertical overlap (matches slot padding)
            float mcHOverlapPerSlot = mcIconW * 0.20f;             // 20% horizontal overlap (10% each side, reduced from 40%)
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

            // Alignment: center pivot (0.5, 0.5) — consistent for drag repositioning
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[2] >= 0) ? m_toolbarPosX[2] : TB_DEF_X[2];
                float fracY = (m_toolbarPosY[2] >= 0) ? m_toolbarPosY[2] : TB_DEF_Y[2];
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH));
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[2] = mcTotalW;
            m_toolbarSizeH[2] = mcTotalH;
            m_mcBarWidget = userWidget;
            showOnScreen(Loc::get("msg.mod_controller_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [MC] === Mod Controller bar creation complete ({}x{}) ===\n"),
                                            mcTotalW, mcTotalH);
        }

        // ── 6J: Overlay & Window Management ──────────────────────────────────
        // GDI+ overlay start/stop, slot display sync, input mode helpers
        // startOverlay/stopOverlay thread lifecycle, updateOverlaySlots

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
            VLOG(STR("[MoriaCppMod] Overlay thread started, icons: {}\n"), s_overlay.iconFolder);
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


        // ── Input Mode Helpers (for modal UI: Config Menu, Reposition Mode) ─────
        // Switch to UI-only input so the mouse cursor appears and game input is blocked.
        void setInputModeUI(UObject* focusWidget = nullptr)
        {
            auto* pc = findPlayerController();
            if (!pc) return;
            if (!focusWidget) focusWidget = m_configWidget;
            if (!focusWidget) return;

            // Find SetInputMode_UIOnlyEx on WidgetBlueprintLibrary CDO
            auto* uiFunc = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:SetInputMode_UIOnlyEx"));
            auto* wblCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary"));
            if (!uiFunc || !wblCDO) {
                VLOG(STR("[MoriaCppMod] setInputModeUI: could not find UIOnlyEx func/CDO\n"));
                return;
            }

            // Params: PlayerController@0, InWidgetToFocus@8, InMouseLockMode@16 (byte)
            uint8_t params[24]{};
            std::memcpy(params + 0, &pc, 8);
            std::memcpy(params + 8, &focusWidget, 8);
            params[16] = 0; // EMouseLockMode::DoNotLock
            wblCDO->ProcessEvent(uiFunc, params);

            // Set bShowMouseCursor bit
            int mcOff = resolveOffset(pc, L"bShowMouseCursor", s_off_showMouseCursor);
            uint8_t* pcRaw = reinterpret_cast<uint8_t*>(pc);
            if (mcOff >= 0) pcRaw[mcOff] |= 0x01;

            VLOG(STR("[MoriaCppMod] Input mode → UI Only (mouse cursor ON)\n"));
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
                VLOG(STR("[MoriaCppMod] setInputModeGame: could not find GameOnly func/CDO\n"));
                return;
            }

            // Params: PlayerController@0
            uint8_t params[8]{};
            std::memcpy(params + 0, &pc, 8);
            wblCDO->ProcessEvent(gameFunc, params);

            // Clear bShowMouseCursor bit
            int mcOff = resolveOffset(pc, L"bShowMouseCursor", s_off_showMouseCursor);
            uint8_t* pcRaw = reinterpret_cast<uint8_t*>(pc);
            if (mcOff >= 0) pcRaw[mcOff] &= ~0x01;

            VLOG(STR("[MoriaCppMod] Input mode → Game Only (mouse cursor OFF)\n"));
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
            VLOG(STR("[MoriaCppMod] Config {} (UMG)\n"), m_cfgVisible ? STR("shown") : STR("hidden"));
        }


        // ── Toolbar Repositioning Mode ──────────────────────────────────────────
        void createRepositionMessage()
        {
            if (m_repositionMsgWidget) return;
            // Use showOnScreen for simplicity — centered UMG message
            // Create a simple UUserWidget with a TextBlock
            auto* uwClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            if (!uwClass) return;
            auto* pc = findPlayerController();
            if (!pc) return;

            // CreateWidget<UUserWidget>
            auto* createFn = uwClass->GetFunctionByNameInChain(STR("CreateWidgetOfClass"));
            if (!createFn) return;
            FStaticConstructObjectParameters uwP(uwClass, reinterpret_cast<UObject*>(pc));
            auto* userWidget = UObjectGlobals::StaticConstructObject(uwP);
            if (!userWidget) return;

            // Create WidgetTree
            auto* wtClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetTree"));
            if (wtClass)
            {
                FStaticConstructObjectParameters wtP(wtClass, userWidget);
                auto* widgetTree = UObjectGlobals::StaticConstructObject(wtP);
                if (widgetTree)
                {
                    // Set WidgetTree on UserWidget via reflected offset
                    if (s_off_widgetTree == -2) resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
                    if (s_off_widgetTree >= 0)
                        *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + s_off_widgetTree) = widgetTree;

                    // Create a TextBlock as root
                    auto* tbClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
                    if (tbClass)
                    {
                        FStaticConstructObjectParameters tbP(tbClass, widgetTree);
                        auto* textBlock = UObjectGlobals::StaticConstructObject(tbP);
                        if (textBlock)
                        {
                            setRootWidget(widgetTree, textBlock);
                            umgSetText(textBlock, L"Using the mouse move the toolbar(s) into your desired positions, hit ESC to exit.");
                            // Yellow text for visibility
                            umgSetTextColor(textBlock, 1.0f, 0.95f, 0.2f, 1.0f);
                        }
                    }
                }
            }

            // Add to viewport at high Z-order
            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 250;
                userWidget->ProcessEvent(addFn, vp.data());
            }

            // Get viewport and position centered
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Set desired size and alignment
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1200.0f * uiScale; v[1] = 60.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }
            setWidgetPosition(userWidget, static_cast<float>(viewW) / 2.0f, static_cast<float>(viewH) / 2.0f);

            m_repositionMsgWidget = userWidget;
        }

        void destroyRepositionMessage()
        {
            if (!m_repositionMsgWidget) return;
            auto* removeFn = m_repositionMsgWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) m_repositionMsgWidget->ProcessEvent(removeFn, nullptr);
            m_repositionMsgWidget = nullptr;
        }

        // Create a placeholder Info Box widget for repositioning (same size as real info box)
        void createPlaceholderInfoBox()
        {
            if (m_repositionInfoBoxWidget) return;

            auto* uwClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!uwClass || !borderClass || !textBlockClass) return;
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
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = uwClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root border (dark blue bg — same as real info box)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootBorder);

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

            // Text label — just says "Info Box"
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            FStaticConstructObjectParameters tbP(textBlockClass, outer);
            UObject* textBlock = UObjectGlobals::StaticConstructObject(tbP);
            if (textBlock && setContentFn)
            {
                umgSetText(textBlock, L"Info Box");
                umgSetTextColor(textBlock, 0.78f, 0.86f, 1.0f, 1.0f);
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = textBlock;
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Add to viewport at high Z-order
            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 249;
                userWidget->ProcessEvent(addFn, vp.data());
            }

            // Get viewport size for uiScale + positioning
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
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Render scale
            if (rootBorder) umgSetRenderScale(rootBorder, uiScale, uiScale);

            // Desired size — same as real info box (400x80 at 4K)
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 400.0f * uiScale; v[1] = 80.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Center alignment
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position from saved or default
            float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
            float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
            setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                          fracY * static_cast<float>(viewH));

            // Cache size for hit-test
            m_toolbarSizeW[3] = 400.0f * uiScale;
            m_toolbarSizeH[3] = 80.0f * uiScale;

            m_repositionInfoBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] Created placeholder Info Box for repositioning\n"));
        }

        void destroyPlaceholderInfoBox()
        {
            if (!m_repositionInfoBoxWidget) return;
            auto* removeFn = m_repositionInfoBoxWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) m_repositionInfoBoxWidget->ProcessEvent(removeFn, nullptr);
            m_repositionInfoBoxWidget = nullptr;
        }

        void toggleRepositionMode()
        {
            // Guard: need at least one toolbar created before entering reposition mode
            if (!m_repositionMode && !m_umgBarWidget && !m_abBarWidget && !m_mcBarWidget)
                return;

            m_repositionMode = !m_repositionMode;
            m_dragToolbar = -1;

            if (m_repositionMode)
            {
                // Ensure toolbars are visible (don't move them — let user adjust from current positions)
                if (!m_toolbarsVisible)
                {
                    m_toolbarsVisible = true;
                    auto setWidgetVis = [](UObject* widget) {
                        if (!widget) return;
                        auto* fn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (fn) { uint8_t parms[8]{}; parms[0] = 0; widget->ProcessEvent(fn, parms); }
                    };
                    setWidgetVis(m_umgBarWidget);
                    setWidgetVis(m_mcBarWidget);
                }
                createRepositionMessage();
                createPlaceholderInfoBox();
                // Use the message widget for focus, or fall back to any toolbar
                UObject* focusW = m_repositionMsgWidget ? m_repositionMsgWidget
                                : m_umgBarWidget ? m_umgBarWidget
                                : m_abBarWidget;
                setInputModeUI(focusW);
                VLOG(STR("[MoriaCppMod] Entered toolbar repositioning mode\n"));
            }
            else
            {
                setInputModeGame();
                destroyRepositionMessage();
                destroyPlaceholderInfoBox();
                saveConfig();
                VLOG(STR("[MoriaCppMod] Exited toolbar repositioning mode, positions saved\n"));
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
            showTargetInfoUMG(name, display, path, cls, buildable, recipe, rowName);
        }

        void undoLast()
        {
            if (m_undoStack.empty())
            {
                VLOG(STR("[MoriaCppMod] Nothing to undo\n"));
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
                VLOG(STR("[MoriaCppMod] Undo type rule: {} — restored {} instances\n"), meshIdW, restored);
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
                VLOG(STR("[MoriaCppMod] Undo: component pointer stale, skipping restore\n"));
            }
            VLOG(STR("[MoriaCppMod] Restored index {} ({}) | {} remaining\n"),
                                            last.instanceIndex,
                                            ok ? STR("ok") : STR("FAILED"),
                                            m_savedRemovals.size());

            m_undoStack.pop_back();
        }

      public:
        // ── 6K: Public Interface ─────────────────────────────────────────────
        // Constructor, destructor, on_unreal_init (keybinds + ProcessEvent hooks),
        // on_update (per-frame tick: state machines, replay, UMG config, keybinds)
        MoriaCppMod()
        {
            ModVersion = STR("2.0");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("Advanced builder, HISM removal, quick-build hotbar, UMG config menu");
            // Init removal list CS before loadSaveFile can be called
            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            VLOG(STR("[MoriaCppMod] Loaded v2.0\n"));
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
            VLOG(STR("[MoriaCppMod] Unreal initialized.\n"));

            // Load config first so Language preference is available for localization
            loadConfig();

            // Load localization string table (compiled English defaults + optional JSON override)
            Loc::load("Mods/MoriaCppMod/localization/", s_language);
            // Patch static keybind labels from string table
            s_bindings[0].label = Loc::get("bind.quick_build_1").c_str();
            s_bindings[0].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[1].label = Loc::get("bind.quick_build_2").c_str();
            s_bindings[1].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[2].label = Loc::get("bind.quick_build_3").c_str();
            s_bindings[2].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[3].label = Loc::get("bind.quick_build_4").c_str();
            s_bindings[3].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[4].label = Loc::get("bind.quick_build_5").c_str();
            s_bindings[4].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[5].label = Loc::get("bind.quick_build_6").c_str();
            s_bindings[5].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[6].label = Loc::get("bind.quick_build_7").c_str();
            s_bindings[6].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[7].label = Loc::get("bind.quick_build_8").c_str();
            s_bindings[7].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[8].label = Loc::get("bind.rotation").c_str();
            s_bindings[8].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[9].label = Loc::get("bind.target").c_str();
            s_bindings[9].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[10].label = Loc::get("bind.toolbar_swap").c_str();
            s_bindings[10].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[11].label = Loc::get("bind.mod_menu_4").c_str();
            s_bindings[11].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[12].label = Loc::get("bind.remove_target").c_str();
            s_bindings[12].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[13].label = Loc::get("bind.undo_last").c_str();
            s_bindings[13].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[14].label = Loc::get("bind.remove_all").c_str();
            s_bindings[14].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[15].label = Loc::get("bind.configuration").c_str();
            s_bindings[15].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[16].label = Loc::get("bind.ab_open").c_str();
            s_bindings[16].section = Loc::get("bind.section_advanced_builder").c_str();
            // Patch config tab names
            CONFIG_TAB_NAMES[0] = Loc::get("tab.optional_mods").c_str();
            CONFIG_TAB_NAMES[1] = Loc::get("tab.key_mapping").c_str();
            CONFIG_TAB_NAMES[2] = Loc::get("tab.hide_environment").c_str();

            m_saveFilePath = "Mods/MoriaCppMod/removed_instances.txt";
            loadSaveFile();
            buildRemovalEntries();
            probePrintString();
            loadQuickBuildSlots();

            // Num1/Num2/Num6 removal handlers removed — now handled by MC polling (slots 4/5/6)

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
                showOnScreen(m_showHotbar ? Loc::get("msg.hotbar_overlay_on").c_str() : Loc::get("msg.hotbar_overlay_off").c_str(), 2.0f, 0.2f, 0.8f, 1.0f);
            });

            // Builders bar toggle: now handled by AB toolbar key polling (s_bindings[BIND_AB_OPEN])

            // Mod Controller toolbar toggle: Numpad 7
            register_keydown_event(Input::Key::NUM_SEVEN, [this]() { if (m_cfgVisible) return; createModControllerBar(); });

            // Spy mode: capture ProcessEvent calls with rotation/build in the function name
            s_instance = this;
            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;


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
                VLOG(STR("[MoriaCppMod] [QuickBuild] Captured: '{}' (with bLock data)\n"), displayName);
                s_instance->logBLockDiagnostics(L"CAPTURE", displayName, p);

                // Reset total rotation for new build piece selection
                s_overlay.totalRotation = 0;
                s_overlay.needsUpdate = true;
                s_instance->updateMcRotationLabel();

                // ONE-TIME: resolve bLock property offset via reflection
                if (s_off_bLock == -2)
                    resolveOffset(selfRef, L"bLock", s_off_bLock);
            });

            m_replayActive = true;
            VLOG(
                    STR("[MoriaCppMod] v2.0: F1-F8=build | F9=rotate | F12=config | MC toolbar + AB bar\n"));
        }

        // Per-frame tick. Drives all state machines and periodic tasks:
        //   - Character load/unload detection (BP_FGKDwarf_C)
        //   - HISM replay queue (throttled, max 3 hides/frame)
        //   - Toolbar swap phases (resolve → stash → restore)
        //   - Quick-build state machine (open menu → find widget → select recipe)
        //   - Icon extraction pipeline (render target → export → PNG conversion)
        //   - Config window actions, periodic rescans
        auto on_update() -> void override
        {

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
                VLOG(STR("[MoriaCppMod] [CFG] Failsafe: config widget lost, resetting state\n"));
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
                // Hoist SHIFT check once before the loop (perf: avoids up to 7 GetAsyncKeyState calls)
                const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (i == 7) { s_lastMcKey[i] = false; continue; } // slot 7 handled by always-on config toggle
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_lastMcKey[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    // SHIFT reverses numpad keys (e.g. Num9 → PageUp) — also check alternate VK.
                    // Only when SHIFT is physically held, to avoid false positives from nav keys.
                    if (!nowDown && shiftHeld)
                    {
                        uint8_t alt = numpadShiftAlternate(vk);
                        if (alt) nowDown = (GetAsyncKeyState(alt) & 0x8000) != 0;
                    }
                    if (nowDown && !s_lastMcKey[i] && !m_cfgVisible && !m_repositionMode)
                    {
                        VLOG(
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
                            saveConfig();
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
                        case 3: // Super Dwarf: modifier+key = fly, key alone = hide character
                            if (isModifierDown())
                                toggleFlyMode();
                            else
                                toggleHideCharacter();
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

            // Repositioning mode — handle ESC exit + mouse drag (runs every frame)
            if (m_repositionMode)
            {
                // ESC to exit repositioning mode
                static bool s_lastReposEsc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastReposEsc)
                    toggleRepositionMode();
                s_lastReposEsc = escDown;

                // Mouse drag logic
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                HWND gw = findGameWindow();
                if (gw)
                {
                    POINT cursor;
                    GetCursorPos(&cursor);
                    ScreenToClient(gw, &cursor);
                    RECT cr;
                    GetClientRect(gw, &cr);
                    float vw = static_cast<float>(cr.right);
                    float vh = static_cast<float>(cr.bottom);

                    if (lmb && m_dragToolbar < 0)
                    {
                        // Start drag: check which toolbar/widget was clicked
                        UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget};
                        for (int i = 0; i < TB_COUNT; i++)
                        {
                            if (!widgets[i]) continue;
                            float fx = (m_toolbarPosX[i] >= 0) ? m_toolbarPosX[i] : TB_DEF_X[i];
                            float fy = (m_toolbarPosY[i] >= 0) ? m_toolbarPosY[i] : TB_DEF_Y[i];
                            float posX = fx * vw;
                            float posY = fy * vh;
                            float w = m_toolbarSizeW[i], h = m_toolbarSizeH[i];
                            float left = posX - w * m_toolbarAlignX[i];
                            float top = posY - h * m_toolbarAlignY[i];
                            if (cursor.x >= left && cursor.x <= left + w &&
                                cursor.y >= top && cursor.y <= top + h)
                            {
                                m_dragToolbar = i;
                                m_dragOffsetX = cursor.x - posX;
                                m_dragOffsetY = cursor.y - posY;
                                break;
                            }
                        }
                    }
                    else if (lmb && m_dragToolbar >= 0)
                    {
                        // Continue drag: update position in real-time
                        float newX = cursor.x - m_dragOffsetX;
                        float newY = cursor.y - m_dragOffsetY;
                        // Clamp to viewport bounds
                        float fx = std::clamp(newX / vw, 0.01f, 0.99f);
                        float fy = std::clamp(newY / vh, 0.01f, 0.99f);
                        m_toolbarPosX[m_dragToolbar] = fx;
                        m_toolbarPosY[m_dragToolbar] = fy;
                        UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget};
                        setWidgetPosition(widgets[m_dragToolbar], fx * vw, fy * vh);
                    }
                    else if (!lmb && m_dragToolbar >= 0)
                    {
                        // Release: end drag
                        m_dragToolbar = -1;
                    }
                }
            }

            // AB toolbar keybind polling — toggle builders bar + MC bar visibility
            // MODIFIER + AB_OPEN = toggle repositioning mode; AB_OPEN alone = toggle visibility
            // Always track key state; only skip action when config is visible
            {
                static bool s_lastAbKey = false;
                uint8_t vk = s_bindings[BIND_AB_OPEN].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbKey && !m_cfgVisible)
                    {
                        if (isModifierDown())
                        {
                            // MODIFIER + AB_OPEN → toggle repositioning mode
                            toggleRepositionMode();
                        }
                        else if (!m_repositionMode)
                        {
                            // AB_OPEN alone → toggle toolbar visibility (existing behavior)
                            m_toolbarsVisible = !m_toolbarsVisible;
                            VLOG(STR("[MoriaCppMod] [AB] Toggle pressed — toolbars {}\n"),
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
                        float uis = static_cast<float>(viewH) / 2160.0f; // uiScale for hit-test
                        if (uis < 0.5f) uis = 0.5f;
                        // Config widget: pos (viewW/2, viewH/2 - 100*uis), size 1400*uis x 900*uis, alignment (0.5,0.5)
                        int wLeft = static_cast<int>(viewW / 2 - 700 * uis);
                        int wTop  = static_cast<int>(viewH / 2 - 100 * uis - 450 * uis);
                        // Tab bar: ~98px from top, each tab 420x66, 40px left padding (all scaled)
                        int tabY0 = static_cast<int>(wTop + 98 * uis), tabY1 = static_cast<int>(tabY0 + 66 * uis);
                        if (cursor.y >= tabY0 && cursor.y <= tabY1)
                        {
                            int tabX0 = static_cast<int>(wLeft + 40 * uis);
                            int tabW = static_cast<int>(420 * uis);
                            for (int t = 0; t < 3; t++)
                            {
                                if (cursor.x >= tabX0 + t * tabW && cursor.x < tabX0 + (t + 1) * tabW)
                                {
                                    switchConfigTab(t);
                                    break;
                                }
                            }
                        }
                        // Tab 0: Free Build checkbox click — entire row
                        if (m_cfgActiveTab == 0)
                        {
                            int cbY0 = static_cast<int>(wTop + 230 * uis), cbY1 = static_cast<int>(cbY0 + 52 * uis);
                            int cbX0 = static_cast<int>(wLeft + 40 * uis), cbX1 = static_cast<int>(wLeft + (1400 - 40) * uis);
                            if (cursor.x >= cbX0 && cursor.x <= cbX1 && cursor.y >= cbY0 && cursor.y <= cbY1)
                            {
                                s_config.pendingToggleFreeBuild = true;
                                VLOG(STR("[MoriaCppMod] [CFG] Free Build toggle via mouse click\n"));
                            }
                            // Unlock All Recipes button: centered, 420px wide, ~340px from top
                            int ubY0 = static_cast<int>(wTop + 330 * uis), ubY1 = static_cast<int>(ubY0 + 68 * uis);
                            int ubX0 = static_cast<int>(wLeft + (1400 - 420) / 2 * uis), ubX1 = static_cast<int>(ubX0 + 420 * uis);
                            if (cursor.x >= ubX0 && cursor.x <= ubX1 && cursor.y >= ubY0 && cursor.y <= ubY1)
                            {
                                s_config.pendingUnlockAllRecipes = true;
                                VLOG(STR("[MoriaCppMod] [CFG] Unlock All Recipes via mouse click\n"));
                            }
                        }
                        // Tab 1: Key box click for rebinding
                        if (m_cfgActiveTab == 1)
                        {
                            // Key boxes are right-aligned, ~220px wide, in the right portion of the widget
                            int kbX0 = static_cast<int>(wLeft + (1400 - 40 - 280) * uis);
                            int kbX1 = static_cast<int>(wLeft + (1400 - 10) * uis);
                            // First key row starts after tabs+seps (~190px from top)
                            int contentY = static_cast<int>(wTop + 190 * uis);
                            int rowHeight = static_cast<int>(44 * uis);
                            int sectionHeight = static_cast<int>(48 * uis);
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
                                            VLOG(STR("[MoriaCppMod] [CFG] Capturing key for bind {}\n"), b);
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
                                        saveConfig();
                                        updateConfigKeyLabels();
                                        VLOG(STR("[MoriaCppMod] [CFG] Modifier key cycled to VK 0x{:02X}\n"), (int)s_modifierVK);
                                    }
                                }
                            }
                        }
                        // Tab 2: Danger icon click to delete removal entry
                        if (m_cfgActiveTab == 2)
                        {
                            // Danger icons are in the left 60px of the content area
                            int iconX0 = static_cast<int>(wLeft + 40 * uis), iconX1 = static_cast<int>(iconX0 + 70 * uis);
                            int entryStart = static_cast<int>(wTop + 230 * uis);
                            int entryHeight = static_cast<int>(70 * uis);
                            if (cursor.x >= iconX0 && cursor.x <= iconX1 && cursor.y >= entryStart)
                            {
                                int entryIdx = (cursor.y - entryStart) / entryHeight;
                                int count = s_config.removalCount.load();
                                if (entryIdx >= 0 && entryIdx < count)
                                {
                                    s_config.pendingRemoveIndex = entryIdx;
                                    VLOG(STR("[MoriaCppMod] [CFG] Delete removal entry {} via mouse click\n"), entryIdx);
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
                                VLOG(STR("[MoriaCppMod] [CFG] Key capture cancelled\n"));
                                break;
                            }
                            // Capture this key
                            int idx = s_capturingBind.load();
                            if (idx >= 0 && idx < BIND_COUNT)
                            {
                                s_bindings[idx].key = static_cast<uint8_t>(vk);
                                s_capturingBind = -1;
                                saveConfig();
                                updateConfigKeyLabels();
                                s_overlay.needsUpdate = true;
                                s_pendingKeyLabelRefresh = true;
                                VLOG(
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
                        VLOG(STR("[MoriaCppMod] [CFG] Free Build toggle requested\n"));
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
                        VLOG(STR("[MoriaCppMod] [CFG] Unlock All Recipes requested\n"));
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
                        saveConfig();
                        updateConfigKeyLabels();
                    }
                    s_lastCfgM = mDown;
                }
            }

            // ── Config window: consume pending cheat toggle requests ──
            // Retry up to 12 attempts (~2s at every-10-frame throttle) then give up.
            {
                static int s_freeBuildRetries = 0;
                static int s_freeBuildThrottle = 0;
                constexpr int MAX_RETRIES = 12;
                constexpr int RETRY_INTERVAL = 10; // check every 10 frames

                if (s_config.pendingToggleFreeBuild)
                {
                    if (++s_freeBuildThrottle >= RETRY_INTERVAL)
                    {
                        s_freeBuildThrottle = 0;
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
                            showOnScreen(Loc::get("msg.free_build_failed").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                            VLOG(STR("[MoriaCppMod] Toggle Free Construction FAILED after {} retries\n"), MAX_RETRIES);
                        }
                    }
                }
            }
            if (s_config.pendingUnlockAllRecipes)
            {
                if (callDebugFunc(STR("BP_DebugMenu_Recipes_C"), STR("All Recipes")))
                    showOnScreen(Loc::get("msg.all_recipes_unlocked").c_str(), 5.0f, 0.0f, 1.0f, 0.0f);
                else
                    showOnScreen(Loc::get("msg.recipe_actor_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
                s_config.pendingUnlockAllRecipes = false;
            }


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
                        VLOG(STR("[MoriaCppMod] Config UI: removed entry {} ({})\n"),
                                                        removeIdx,
                                                        std::wstring(toRemove.friendlyName));
                    }
                    s_config.pendingRemoveIndex = -1;
                }
            }

            // Detect build menu close → refresh ActionBar (fixes stale hotbar display)
            // Uses cheap isBuildTabShowing() — cached pointer with GC-flag validation
            if (m_buildMenuWasOpen && !isBuildTabShowing())
            {
                m_buildMenuWasOpen = false;
                // Clear cached pointers — Build_Tab widget may be getting destroyed
                m_cachedBuildTab = nullptr;
                m_cachedBuildHUD = nullptr;
                m_fnIsShowing = nullptr;
                refreshActionBar();
            }

            // ── Reactive quickbuild state machine ──
            // Polls cheap widget booleans each tick, proceeds the instant game state transitions
            if (m_qbPhase != QBPhase::Idle)
            {
                m_qbTimeout++;

                // Global safety timeout
                if (m_qbTimeout > 150)
                {
                    VLOG(STR("[MoriaCppMod] [QuickBuild] SM: TIMEOUT at tick {} phase {}\n"),
                                                    m_qbTimeout, static_cast<int>(m_qbPhase));
                    showOnScreen(Loc::get("msg.build_menu_timeout").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                    m_pendingQuickBuildSlot = -1;
                    m_qbPhase = QBPhase::Idle;
                    m_qbTimeout = 0;
                }
                else if (m_qbPhase == QBPhase::CancelPlacement)
                {
                    // Wait for ESC to take effect — placement deactivates
                    if (!isPlacementActive())
                    {
                        VLOG(STR("[MoriaCppMod] [QuickBuild] SM: placement cancelled (tick {})\n"), m_qbTimeout);
                        if (isBuildTabShowing())
                        {
                            // Build menu still showing after ESC — close it first
                            keybd_event(0x42, 0, 0, 0);
                            keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                            m_qbPhase = QBPhase::CloseMenu;
                        }
                        else
                        {
                            // Menu already closed — open fresh
                            keybd_event(0x42, 0, 0, 0);
                            keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                            m_qbPhase = QBPhase::OpenMenu;
                        }
                    }
                }
                else if (m_qbPhase == QBPhase::CloseMenu)
                {
                    // Wait for build tab to close
                    if (!isBuildTabShowing())
                    {
                        VLOG(STR("[MoriaCppMod] [QuickBuild] SM: menu closed (tick {}) — opening fresh\n"), m_qbTimeout);
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                        m_qbPhase = QBPhase::OpenMenu;
                    }
                }
                else if (m_qbPhase == QBPhase::OpenMenu)
                {
                    if (isBuildTabShowing())
                    {
                        // Menu opened — proceed to recipe selection
                        VLOG(STR("[MoriaCppMod] [QuickBuild] SM: menu opened (tick {}) — selecting recipe\n"), m_qbTimeout);
                        m_qbPhase = QBPhase::SelectRecipe;
                        // Fall through to SelectRecipe on same tick
                    }
                    else if (m_qbTimeout == 25)
                    {
                        // Retry B key if menu hasn't appeared yet
                        VLOG(STR("[MoriaCppMod] [QuickBuild] SM: tick 25 — retrying B key\n"));
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                    }
                }

                if (m_qbPhase == QBPhase::SelectRecipe)
                {
                    UObject* buildTab = getCachedBuildTab();
                    if (buildTab && selectRecipeOnBuildTab(buildTab, m_pendingQuickBuildSlot))
                    {
                        // Success — reset total rotation for new build placement
                        s_overlay.totalRotation = 0;
                        s_overlay.needsUpdate = true;
                        updateMcRotationLabel();
                        m_pendingQuickBuildSlot = -1;
                        m_qbPhase = QBPhase::Idle;
                        m_qbTimeout = 0;
                    }
                    else if (m_qbTimeout > 100)
                    {
                        // Items never loaded — show error
                        VLOG(STR("[MoriaCppMod] [QuickBuild] SM: recipe not found after {} ticks\n"), m_qbTimeout);
                        const std::wstring& targetName = m_recipeSlots[m_pendingQuickBuildSlot].displayName;
                        showOnScreen((L"Recipe '" + targetName + L"' not found in menu!").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                        m_pendingQuickBuildSlot = -1;
                        m_qbPhase = QBPhase::Idle;
                        m_qbTimeout = 0;
                    }
                    // else: stay in SelectRecipe, items may still be loading
                }
            }

            // Toolbar swap state machine: one item per tick
            swapToolbarTick();

            // ── Reactive target-build state machine (Shift+F10 → build from targeted actor) ──
            if (m_tbPhase != QBPhase::Idle)
            {
                m_tbTimeout++;

                // Global safety timeout
                if (m_tbTimeout > 150)
                {
                    VLOG(STR("[MoriaCppMod] [TargetBuild] SM: TIMEOUT at tick {} phase {}\n"),
                                                    m_tbTimeout, static_cast<int>(m_tbPhase));
                    showOnScreen(Loc::get("msg.build_menu_timeout").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                    m_pendingTargetBuild = false;
                    m_tbPhase = QBPhase::Idle;
                    m_tbTimeout = 0;
                }
                else if (m_tbPhase == QBPhase::CancelPlacement)
                {
                    if (!isPlacementActive())
                    {
                        VLOG(STR("[MoriaCppMod] [TargetBuild] SM: placement cancelled (tick {})\n"), m_tbTimeout);
                        if (isBuildTabShowing())
                        {
                            keybd_event(0x42, 0, 0, 0);
                            keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                            m_tbPhase = QBPhase::CloseMenu;
                        }
                        else
                        {
                            keybd_event(0x42, 0, 0, 0);
                            keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                            m_tbPhase = QBPhase::OpenMenu;
                        }
                    }
                }
                else if (m_tbPhase == QBPhase::CloseMenu)
                {
                    if (!isBuildTabShowing())
                    {
                        VLOG(STR("[MoriaCppMod] [TargetBuild] SM: menu closed (tick {}) — opening fresh\n"), m_tbTimeout);
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                        m_tbPhase = QBPhase::OpenMenu;
                    }
                }
                else if (m_tbPhase == QBPhase::OpenMenu)
                {
                    if (isBuildTabShowing())
                    {
                        VLOG(STR("[MoriaCppMod] [TargetBuild] SM: menu opened (tick {}) — selecting recipe\n"), m_tbTimeout);
                        m_tbPhase = QBPhase::SelectRecipe;
                    }
                    else if (m_tbTimeout == 25)
                    {
                        VLOG(STR("[MoriaCppMod] [TargetBuild] SM: tick 25 — retrying B key\n"));
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                    }
                }

                if (m_tbPhase == QBPhase::SelectRecipe)
                {
                    UObject* buildTab = getCachedBuildTab();
                    if (buildTab)
                    {
                        selectRecipeByTargetName(buildTab);
                        // Reset total rotation for new build placement
                        s_overlay.totalRotation = 0;
                        s_overlay.needsUpdate = true;
                        updateMcRotationLabel();
                        m_pendingTargetBuild = false;
                        m_tbPhase = QBPhase::Idle;
                        m_tbTimeout = 0;
                    }
                    else if (m_tbTimeout > 100)
                    {
                        VLOG(STR("[MoriaCppMod] [TargetBuild] SM: build tab lost after {} ticks\n"), m_tbTimeout);
                        showOnScreen(Loc::get("msg.build_menu_timeout").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                        m_pendingTargetBuild = false;
                        m_tbPhase = QBPhase::Idle;
                        m_tbTimeout = 0;
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
                    VLOG(STR("[MoriaCppMod] Character lost — world unloading, resetting replay state\n"));
                    m_characterLoaded = false;
                    m_characterHidden = false;       // reset hide toggle for new world
                    m_flyMode = false;               // reset fly toggle for new world
                    // Reset reactive state machine — cached pointers are stale in new world
                    m_cachedBuildHUD = nullptr;
                    m_cachedBuildTab = nullptr;
                    m_fnIsShowing = nullptr;
                    m_qbPhase = QBPhase::Idle;
                    m_qbTimeout = 0;
                    m_tbPhase = QBPhase::Idle;
                    m_tbTimeout = 0;
                    m_pendingQuickBuildSlot = -1;
                    m_pendingTargetBuild = false;
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
                    m_repairDone = false;
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
                    // Repositioning mode destroyed with world
                    m_repositionMode = false;
                    m_dragToolbar = -1;
                    m_repositionMsgWidget = nullptr;
                    m_repositionInfoBoxWidget = nullptr;
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
                    s_config.pendingToggleFreeBuild = false;
                    s_config.pendingUnlockAllRecipes = false;
                    VLOG(STR("[MoriaCppMod] [Swap] Cleared all container handles, swap state, and cheat toggles\n"));
                }
            }


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
                        VLOG(STR("[MoriaCppMod] Character loaded at frame {} — waiting 15s before replay\n"), m_frameCounter);
                    }
                }
                return; // don't do anything until character exists
            }

            int framesSinceChar = m_frameCounter - m_charLoadFrame;

            // Auto-scan containers: retry every ~2s after initial 5s delay, give up after ~60s
            if (m_bodyInvHandle.empty() && framesSinceChar > 300 && framesSinceChar < 3900 && framesSinceChar % 120 == 0)
            {
                VLOG(STR("[MoriaCppMod] [Swap] Container scan attempt (frame {}). bodyInvHandle.empty={} handles.size={}\n"),
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
                        VLOG(STR("[MoriaCppMod] === Auto-scan containers (retry) ===\n"));
                        discoverBagHandle(invComp);
                        if (!m_bodyInvHandle.empty())
                        {
                            showOnScreen(Loc::get("msg.containers_discovered").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
                        }
                    }
                }
            }

            // Log failure if container scan times out after ~60s
            if (m_bodyInvHandle.empty() && framesSinceChar == 3900)
            {
                VLOG(STR("[MoriaCppMod] [Swap] Container discovery FAILED after 60s — toolbar swap unavailable this session\n"));
                showOnScreen(Loc::get("msg.container_discovery_failed").c_str(), 5.0f, 1.0f, 0.3f, 0.0f);
            }

            // Initial replay 15 seconds after character load (~900 frames at 60fps)
            // Extra delay to let streaming settle before modifying instance buffers
            static constexpr int INITIAL_DELAY = 900;
            if (!m_initialReplayDone && framesSinceChar == INITIAL_DELAY)
            {
                m_initialReplayDone = true;
                if (!m_savedRemovals.empty() || !m_typeRemovals.empty())
                {
                    VLOG(STR("[MoriaCppMod] Starting initial replay (15s after char load)...\n"));
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
                VLOG(STR("[MoriaCppMod] Periodic rescan ({} pending)...\n"), pending);
                m_processedComps.clear();
                startReplay();
                if (m_stuckLogCount == 0 && pending > 0)
                {
                    m_stuckLogCount++;
                    VLOG(STR("[MoriaCppMod] === Pending entries ({}) ===\n"), pending);
                    for (size_t i = 0; i < m_savedRemovals.size(); i++)
                    {
                        if (m_appliedRemovals[i]) continue;
                        std::wstring meshW(m_savedRemovals[i].meshName.begin(), m_savedRemovals[i].meshName.end());
                        VLOG(STR("[MoriaCppMod]   PENDING [{}]: {} @ ({:.1f},{:.1f},{:.1f})\n"),
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
