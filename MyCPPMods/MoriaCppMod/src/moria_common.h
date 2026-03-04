// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_common.h — Shared types, constants, macros, and RAII utilities     ║
// ║  Included by all MoriaCppMod source files.                                ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once

// ════════════════════════════════════════════════════════════════════════════════
// Standard Library & Platform Includes
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
#include <unordered_set>
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

// ════════════════════════════════════════════════════════════════════════════════
// UE4SS Includes
// ════════════════════════════════════════════════════════════════════════════════
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
#include <Unreal/Property/FBoolProperty.hpp>

#include "moria_testable.h"

namespace MoriaMods
{
    using namespace RC;
    using namespace RC::Unreal;

    // ════════════════════════════════════════════════════════════════════════════
    // Logging Macros
    // ════════════════════════════════════════════════════════════════════════════

    // Verbose logging gate — when false (default), all VLOG() calls are short-circuited
    // to avoid format-string overhead.  Set to true via config or code for debugging.
    inline bool s_verbose = false;
    inline std::string s_language = "en"; // localization file (e.g. "en" loads en.json)
    // NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — macro needed to short-circuit variadic template
    #define VLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)

    // QuickBuild/TargetBuild logging — compile-time switch (0=off, 1=on)
    #define QUICKBUILD_LOGGING 1
    // NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
    #if QUICKBUILD_LOGGING
        #define QBLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)
    #else
        #define QBLOG(...) do {} while (0)
    #endif

    // ════════════════════════════════════════════════════════════════════════════
    // Mathematical Constants
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr float MY_PI = 3.14159265358979323846f;
    static constexpr float DEG2RAD = MY_PI / 180.0f;
    static constexpr float TRACE_DIST = 5000.0f;   // 50m (was 500m — way too far)
    static constexpr float POS_TOLERANCE = 100.0f; // 1 meter — game scale is huge (walls = 2000 units)

    // ════════════════════════════════════════════════════════════════════════════
    // Struct-Internal Offsets (POD struct internals, NOT resolvable via ForEachProperty)
    // ════════════════════════════════════════════════════════════════════════════
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
    static constexpr int CAM_SETTINGS_BLOB_SIZE = 0x4C; // sizeof(FFGKCameraStateSettings) — 18 floats + FVector2D

    // ════════════════════════════════════════════════════════════════════════════
    // Raw UE4.27 Types (floats, not doubles)
    // ════════════════════════════════════════════════════════════════════════════
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

    // ════════════════════════════════════════════════════════════════════════════
    // ProcessEvent Param Structs (layouts confirmed by probe)
    // ════════════════════════════════════════════════════════════════════════════
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

    // ════════════════════════════════════════════════════════════════════════════
    // Application Structs
    // ════════════════════════════════════════════════════════════════════════════

    // SavedRemoval, RemovalEntry, extractFriendlyName — defined in moria_testable.h

    struct RemovedInstance
    {
        RC::Unreal::FWeakObjectPtr component;
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

    // ════════════════════════════════════════════════════════════════════════════
    // Win32 Overlay State
    // ════════════════════════════════════════════════════════════════════════════

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
    inline OverlayState s_overlay;

    // Loc namespace core (s_table, utf8ToWide, parseJsonFile, initDefaults, get) — defined in moria_testable.h
    // Loc::load() remains here because it uses UE4SS Output::send for logging.
    namespace Loc
    {
        // Load string table: init defaults, then override from JSON file if available
        inline void load(const std::string& locDir, const std::string& lang = "en")
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

    // ════════════════════════════════════════════════════════════════════════════
    // Config State & Cross-Thread Signals
    // ════════════════════════════════════════════════════════════════════════════
    static constexpr int CONFIG_TAB_COUNT = 3;
    inline const wchar_t* CONFIG_TAB_NAMES[CONFIG_TAB_COUNT] = {L"Optional Mods", L"Key Mapping", L"Hide Environment"};

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
    inline ConfigState s_config{};

    inline std::atomic<bool> s_pendingKeyLabelRefresh{false}; // cross-thread flag: config→game thread

    // ════════════════════════════════════════════════════════════════════════════
    // RAII CriticalSection Lock
    // ════════════════════════════════════════════════════════════════════════════
    struct CriticalSectionLock
    {
        CRITICAL_SECTION& cs;
        CriticalSectionLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
        ~CriticalSectionLock() { LeaveCriticalSection(&cs); }
        CriticalSectionLock(const CriticalSectionLock&) = delete;
        CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;
    };

} // namespace MoriaMods
