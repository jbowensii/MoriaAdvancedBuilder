


#pragma once


#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <memory>
#include <mutex>
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
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UEnum.hpp>

#include "moria_testable.h"

namespace MoriaMods
{
    using namespace RC;
    using namespace RC::Unreal;


    inline bool s_verbose = false;
    inline std::string s_language = "en";

    #define VLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)

    #define QUICKBUILD_LOGGING 1

    #if QUICKBUILD_LOGGING
        #define QBLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)
    #else
        #define QBLOG(...) do {} while (0)
    #endif


    static constexpr float TRACE_DIST = 5000.0f;
    static constexpr float POS_TOLERANCE = 100.0f;


    static constexpr int BRUSH_IMAGE_SIZE_X = 0x08;
    static constexpr int BRUSH_IMAGE_SIZE_Y = 0x0C;
    static constexpr int BRUSH_RESOURCE_OBJECT = 0x48;
    static constexpr int FONT_TYPEFACE_NAME = 0x40;
    static constexpr int FONT_SIZE = 0x48;
    static constexpr int FONT_STRUCT_SIZE = 0x58;
    static constexpr int RECIPE_BLOCK_VARIANTS = 0x68;
    static constexpr int RECIPE_BLOCK_VARIANTS_NUM = 0x70;
    static constexpr int VARIANT_ROW_CI = 0xE0;
    static constexpr int VARIANT_ROW_NUM = 0xE4;
    static constexpr int VARIANT_ENTRY_SIZE = 0xE8;
    static constexpr int TEX_PARAM_VALUE_PTR = 0x10;
    static constexpr int DT_ROWMAP_OFFSET = 0x30;
    static constexpr int SOFTCLASSPTR_ASSETPATH_FNAME = 0x10;


    struct TArrayView
    {
        uint8_t* data{nullptr};
        int32_t  num{0};
        int32_t  elemSize{0};

        TArrayView() = default;


        TArrayView(FArrayProperty* arrProp, uint8_t* arrayField)
        {
            if (!arrProp || !arrayField) return;
            FProperty* inner = arrProp->GetInner();
            if (!inner) return;
            elemSize = inner->GetSize();
            if (elemSize <= 0) return;
            if (!isReadableMemory(arrayField, 16)) return;

            std::memcpy(&data, arrayField, 8);
            std::memcpy(&num, arrayField + 8, 4);
        }

        [[nodiscard]] int32_t Num() const { return num; }
        [[nodiscard]] bool IsValidIndex(int32_t idx) const { return idx >= 0 && idx < num && data != nullptr; }

        [[nodiscard]] uint8_t* GetRawPtr(int32_t idx) const
        {
            if (!IsValidIndex(idx)) return nullptr;
            return data + idx * elemSize;
        }
    };


    struct FVec3f
    {
        float X, Y, Z;
    };
    struct FQuat4f
    {
        float X, Y, Z, W;
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


#pragma pack(push, 1)
    struct FHitResultLocal
    {
        int32_t FaceIndex;
        float Time;
        float Distance;
        FVec3f Location;
        FVec3f ImpactPoint;
        FVec3f Normal;
        FVec3f ImpactNormal;
        FVec3f TraceStart;
        FVec3f TraceEnd;
        float PenetrationDepth;
        int32_t Item;
        uint8_t ElementIndex;
        uint8_t bBlockingHit;
        uint8_t bStartPenetrating;
        uint8_t _pad5F;
        RC::Unreal::FWeakObjectPtr PhysMaterial;
        RC::Unreal::FWeakObjectPtr Actor;
        RC::Unreal::FWeakObjectPtr Component;
        uint8_t BoneName[8];
        uint8_t MyBoneName[8];
    };
#pragma pack(pop)
    static_assert(sizeof(FHitResultLocal) == 0x88, "FHitResult must be 136 bytes");


    struct RemovedInstance
    {
        RC::Unreal::FWeakObjectPtr component;
        int32_t instanceIndex{-1};
        FTransformRaw transform;
        std::wstring componentName;
        bool isTypeRule{false};
        std::string typeRuleMeshId;
    };


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


    static constexpr int OVERLAY_SLOTS = 12;

    struct OverlaySlot
    {
        std::wstring displayName;
        std::wstring textureName;
        std::shared_ptr<Gdiplus::Image> icon;
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
        std::wstring iconFolder;
        std::atomic<int> rotationStep{5};
        std::atomic<int> totalRotation{0};
    };
    inline OverlayState s_overlay;

    namespace Loc
    {

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
    }


    static constexpr int CONFIG_TAB_COUNT = 4;
    inline const wchar_t* CONFIG_TAB_NAMES[4] = {L"Optional Mods", L"Key Mapping", L"Hide Environment", L"Game Mods"};

    struct ConfigState
    {
        CRITICAL_SECTION removalCS;
        std::atomic<bool> removalCSInit{false};
        std::vector<RemovalEntry> removalEntries;
        std::atomic<int> removalCount{0};
        std::atomic<int> pendingRemoveIndex{-1};
    };
    inline ConfigState s_config{};

    inline std::atomic<bool> s_pendingKeyLabelRefresh{false};


    struct CriticalSectionLock
    {
        CRITICAL_SECTION& cs;
        explicit CriticalSectionLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
        ~CriticalSectionLock() { LeaveCriticalSection(&cs); }
        CriticalSectionLock(const CriticalSectionLock&) = delete;
        CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;
    };

}
