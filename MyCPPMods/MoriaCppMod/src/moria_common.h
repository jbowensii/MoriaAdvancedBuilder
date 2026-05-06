// moria_common.h — Shared declarations for MoriaCppMod
// Key exports: safeProcessEvent (SEH-wrapped), modPath (s_ue4ssWorkDir prefix for all file I/O),
// s_verbose/s_language globals, FWeakObjectPtr via UE4SS header, geometry/hit-result structs

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
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")
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

    // Moved from moria_common.inl. Lived as MoriaCppMod class
    // members because the .inl is included at class scope. That meant any
    // TU that included moria_common.h alone (e.g. moria_keybinds.h - whose
    // isModifierDown/isChordHeld at lines 95/102 call GetAsyncKeyState
    // directly) saw the raw Win32 API and silently bypassed the focus
    // filter. Promoting both helpers to free functions at namespace scope
    // here makes the macro override visible everywhere moria_common.h is
    // included, closing the alt-tab spurious-keypress hole.

    // Returns true when our process owns the foreground (focused) window.
    // Used to gate GetAsyncKeyState-based input polling so the mod doesn't
    // react to keystrokes the user typed into another app while alt-tabbed.
    inline bool isGameWindowFocused()
    {
        HWND fg = ::GetForegroundWindow();
        if (!fg) return false;
        DWORD fgPid = 0;
        ::GetWindowThreadProcessId(fg, &fgPid);
        return fgPid == ::GetCurrentProcessId();
    }

    // Focus-aware wrapper around the Win32 GetAsyncKeyState. Returns 0 when
    // the game isn't the foreground process, suppressing every keybind /
    // modifier check the mod performs while alt-tabbed.
    inline SHORT focusedAsyncKeyState(int vk)
    {
        if (!isGameWindowFocused()) return 0;
        return ::GetAsyncKeyState(vk);
    }

    // Macro-replace every GetAsyncKeyState call in our codebase that
    // follows this include so the focus guard kicks in automatically.
    // Win32 API calls outside our mod (UE4SS internals, system code) are
    // unaffected because they don't include this header.
#ifdef GetAsyncKeyState
#  undef GetAsyncKeyState
#endif
#define GetAsyncKeyState ::MoriaMods::focusedAsyncKeyState


    inline bool s_verbose = true;
    inline std::string s_language = "en";
    inline std::string s_ue4ssWorkDir;  // set once in on_unreal_init from UE4SSProgram::get_working_directory

    inline std::string modPath(const char* relativePath)
    {
        return s_ue4ssWorkDir + relativePath;
    }
    inline std::string modPath(const std::string& relativePath)
    {
        return s_ue4ssWorkDir + relativePath;
    }

    // v6.4.3 Steam ™ path fix — openInputFile / openOutputFile / renameUtf8Path helpers
    // are defined in moria_testable.h (which is included above), before anything that needs them.

    // UTF-8 ↔ wide-string helpers. NEVER do `std::wstring(s.begin(), s.end())` —
    // that zero-extends each UTF-8 byte, corrupting any character above 0x7F.
    // Always use these for runtime strings that may contain non-ASCII (server
    // names, passwords, paths with the Steam ™ symbol, accented domains, etc.).
    inline std::wstring utf8ToWide(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (wlen <= 0) return std::wstring();
        std::wstring out(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], wlen);
        return out;
    }

    // wideToUtf8 already defined in moria_testable.h (above) — re-using it.

    #define VLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)

    #define QUICKBUILD_LOGGING 1

    #if QUICKBUILD_LOGGING
        #define QBLOG(...) do { if (::MoriaMods::s_verbose) ::RC::Output::send<::RC::LogLevel::Warning>(__VA_ARGS__); } while (0)
    #else
        #define QBLOG(...) do {} while (0)
    #endif


    // Safe ProcessEvent wrapper — validates object and function before calling
    inline bool safeProcessEvent(UObject* obj, UFunction* fn, void* parms)
    {
        if (!obj || !fn) {
            VLOG(STR("[MoriaCppMod] [SAFE] ProcessEvent BLOCKED: obj={} fn={}\n"), (void*)obj, (void*)fn);
            return false;
        }
        __try {
            if (obj->HasAnyFlags(static_cast<EObjectFlags>(0x00400000))) { // RF_FinishDestroyed only
                VLOG(STR("[MoriaCppMod] [SAFE] ProcessEvent BLOCKED: obj {} is FinishDestroyed\n"), (void*)obj);
                return false;
            }
            if (obj->IsUnreachable()) {
                VLOG(STR("[MoriaCppMod] [SAFE] ProcessEvent BLOCKED: obj {} is Unreachable\n"), (void*)obj);
                return false;
            }
            obj->ProcessEvent(fn, parms);
            return true;
        } __except(1) {
            VLOG(STR("[MoriaCppMod] [SAFE] ProcessEvent CAUGHT SEH exception on obj={}\n"), (void*)obj);
            return false;
        }
    }

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


    static constexpr int CONFIG_TAB_COUNT = 6;
    inline const wchar_t* CONFIG_TAB_NAMES[6] = {L"Optional Mods", L"Key Mapping", L"Hide Environment", L"Game Mods", L"Cheats", L"Tweaks"};

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
