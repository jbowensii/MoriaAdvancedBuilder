// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_reflection.h — Property offset resolution & UFunction helpers      ║
// ║                                                                           ║
// ║  THREAD SAFETY: Offset caches are written only from the game thread.      ║
// ║  The overlay thread must never call resolveOffset or any resolver.         ║
// ║  It only reads s_overlay.slots[] which is protected by slotCS.            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once

#include "moria_common.h"

namespace MoriaMods
{
    // ════════════════════════════════════════════════════════════════════════════
    // Cached Property Offsets (resolved via ForEachProperty on first use)
    //   -2 = not yet resolved, -1 = property not found
    // ════════════════════════════════════════════════════════════════════════════
    inline int s_off_widgetTree = -2;       // UUserWidget::WidgetTree
    inline int s_off_rootWidget = -2;       // UWidgetTree::RootWidget
    inline int s_off_font = -2;             // UTextBlock::Font
    inline int s_off_brush = -2;            // UImage::Brush
    inline int s_off_charMovement = -2;     // ACharacter::CharacterMovement
    inline int s_off_capsuleComp = -2;      // ACharacter::CapsuleComponent
    inline int s_off_recipeSelectMode = -2; // UI_WBP_BuildHUDv2_C::recipeSelectMode
    inline int s_off_bLock = -2;            // UI_WBP_Build_Item_C::bLock
    inline int s_off_icon = -2;             // UI_WBP_Build_Item_Medium_C::Icon
    inline int s_off_blockName = -2;        // UI_WBP_Build_Item_Medium_C::blockName
    inline int s_off_stackCount = -2;       // UI_WBP_Build_Item_Medium_C::StackCount
    inline int s_off_texParamValues = -2;   // UMaterialInstanceDynamic::TextureParameterValues
    inline int s_off_targetActor = -2;      // UBuildOverlayWidget::TargetActor
    inline int s_off_selectedRecipe = -2;    // UI_WBP_Build_Tab_C::selectedRecipe
    inline int s_off_selectedName = -2;      // UI_WBP_Build_Tab_C::selectedName
    inline int s_off_recipesDataTable = -2;  // UI_WBP_Build_Tab_C::recipesDataTable
    inline int s_off_playerCameraManager = -2; // APlayerController::PlayerCameraManager
    inline int s_off_camSettings = -2;         // AFGKPlayerCameraManager::Settings
    inline int s_off_probeType = -2;           // AFGKPlayerCameraManager::ProbeType
    inline int s_off_probeRadius = -2;         // AFGKPlayerCameraManager::ProbeRadius

    // ════════════════════════════════════════════════════════════════════════════
    // Property Offset Resolution
    // ════════════════════════════════════════════════════════════════════════════

    // Resolve a UProperty offset by name on an object's class (walks full inheritance chain).
    inline int resolveOffset(UObject* obj, const wchar_t* propName, int& cache)
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
    inline void ensureBrushOffset(UObject* imageWidget)
    {
        if (s_off_brush == -2 && imageWidget)
            resolveOffset(imageWidget, L"Brush", s_off_brush);
    }

    // Set UWidgetTree::RootWidget via reflected offset
    inline void setRootWidget(UObject* widgetTree, UObject* root)
    {
        int off = resolveOffset(widgetTree, L"RootWidget", s_off_rootWidget);
        if (off >= 0)
            *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widgetTree) + off) = root;
    }

    // Resolve a UProperty offset + property size by name (for runtime struct size validation)
    inline int resolveOffsetWithSize(UObject* obj, const wchar_t* propName, int& cache, int& sizeOut)
    {
        sizeOut = 0;
        if (cache != -2) return cache;
        cache = -1;
        if (!obj) return -1;
        for (auto* strct = static_cast<UStruct*>(obj->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                {
                    cache = prop->GetOffset_Internal();
                    sizeOut = prop->GetSize();
                    VLOG(STR("[MoriaCppMod] Resolved '{}' at offset 0x{:04X} size {} (on {})\n"),
                         std::wstring(propName), cache, sizeOut, strct->GetName());
                    return cache;
                }
            }
        }
        return cache;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Bool Property Helpers (handles bitfields via FBoolProperty API)
    // ════════════════════════════════════════════════════════════════════════════

    // Resolve a bool property using FBoolProperty API (handles bitfields correctly)
    inline FBoolProperty* resolveBoolProperty(UObject* obj, const wchar_t* propName)
    {
        if (!obj) return nullptr;
        for (auto* strct = static_cast<UStruct*>(obj->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                {
                    auto* bp = CastField<FBoolProperty>(prop);
                    if (bp)
                    {
                        VLOG(STR("[MoriaCppMod] Resolved FBoolProperty '{}' (native={}) on {}\n"),
                             std::wstring(propName), bp->IsNativeBool() ? 1 : 0, strct->GetName());
                    }
                    return bp;
                }
            }
        }
        VLOG(STR("[MoriaCppMod] WARNING: FBoolProperty '{}' not found on {}\n"),
             std::wstring(propName), obj->GetClassPrivate()->GetName());
        return nullptr;
    }

    // Set a bool property value on an object using FBoolProperty (handles bitfields)
    inline bool setBoolProp(UObject* obj, const wchar_t* propName, bool value)
    {
        auto* bp = resolveBoolProperty(obj, propName);
        if (!bp) return false;
        bp->SetPropertyValueInContainer(obj, value);
        return true;
    }

    // Get a bool property value from an object using FBoolProperty (handles bitfields)
    inline bool getBoolProp(UObject* obj, const wchar_t* propName)
    {
        auto* bp = resolveBoolProperty(obj, propName);
        if (!bp) return false;
        return bp->GetPropertyValueInContainer(obj);
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Time Interval Helper
    // ════════════════════════════════════════════════════════════════════════════

    // Check if a real-time interval has elapsed (replaces frame-counting)
    inline bool intervalElapsed(ULONGLONG& lastTime, ULONGLONG intervalMs)
    {
        ULONGLONG now = GetTickCount64();
        if (now - lastTime >= intervalMs)
        {
            lastTime = now;
            return true;
        }
        return false;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // UFunction Parameter Finder
    // ════════════════════════════════════════════════════════════════════════════

    // Find a named parameter on a UFunction — returns the FProperty* or nullptr
    inline FProperty* findParam(UFunction* func, const wchar_t* paramName)
    {
        if (!func) return nullptr;
        for (auto* prop : func->ForEachProperty())
        {
            if (prop->GetName() == std::wstring_view(paramName))
                return prop;
        }
        return nullptr;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Resolved Param Offset Structs (runtime-resolved UFunction parameter layouts)
    // ════════════════════════════════════════════════════════════════════════════

    // LineTraceSingle param offsets
    struct LTResolved
    {
        int WorldContextObject{-1}, Start{-1}, End{-1}, TraceChannel{-1};
        int bTraceComplex{-1}, ActorsToIgnore{-1}, DrawDebugType{-1};
        int OutHit{-1}, bIgnoreSelf{-1}, TraceColor{-1};
        int TraceHitColor{-1}, DrawTime{-1}, ReturnValue{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline LTResolved s_lt{};

    inline void resolveLTOffsets(UFunction* ltFunc)
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

    // UpdateInstanceTransform param offsets
    struct UITResolved
    {
        int InstanceIndex{-1}, NewInstanceTransform{-1};
        int bWorldSpace{-1}, bMarkRenderStateDirty{-1}, bTeleport{-1};
        int ReturnValue{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline UITResolved s_uit{};

    inline void resolveUITOffsets(UFunction* uitFunc)
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

    // DeprojectScreenPositionToWorld param offsets
    struct DSPResolved
    {
        int ScreenX{-1}, ScreenY{-1}, WorldLocation{-1}, WorldDirection{-1}, ReturnValue{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline DSPResolved s_dsp{};

    inline void resolveDSPOffsets(UFunction* fn)
    {
        if (s_dsp.resolved) return;
        s_dsp.resolved = true;
        s_dsp.parmsSize = fn->GetParmsSize();
        for (auto* prop : fn->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"ScreenX")            s_dsp.ScreenX = off;
            else if (n == L"ScreenY")       s_dsp.ScreenY = off;
            else if (n == L"WorldLocation") s_dsp.WorldLocation = off;
            else if (n == L"WorldDirection") s_dsp.WorldDirection = off;
            else if (n == L"ReturnValue")   s_dsp.ReturnValue = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved DeprojectScreenPositionToWorld: parmsSize={} ScreenX={} ScreenY={} WorldLoc={} WorldDir={}\n"),
             s_dsp.parmsSize, s_dsp.ScreenX, s_dsp.ScreenY, s_dsp.WorldLocation, s_dsp.WorldDirection);
    }

    // SetInputMode_UIOnlyEx param offsets
    struct SIMUIResolved
    {
        int PlayerController{-1}, InWidgetToFocus{-1}, InMouseLockMode{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline SIMUIResolved s_simui{};

    inline void resolveSIMUIOffsets(UFunction* fn)
    {
        if (s_simui.resolved) return;
        s_simui.resolved = true;
        s_simui.parmsSize = fn->GetParmsSize();
        for (auto* prop : fn->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"PlayerController")       s_simui.PlayerController = off;
            else if (n == L"InWidgetToFocus")   s_simui.InWidgetToFocus = off;
            else if (n == L"InMouseLockMode")   s_simui.InMouseLockMode = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved SetInputMode_UIOnlyEx: parmsSize={} PC={} Focus={} LockMode={}\n"),
             s_simui.parmsSize, s_simui.PlayerController, s_simui.InWidgetToFocus, s_simui.InMouseLockMode);
    }

    // SetInputMode_GameOnly param offsets
    struct SIMGResolved
    {
        int PlayerController{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline SIMGResolved s_simg{};

    inline void resolveSIMGOffsets(UFunction* fn)
    {
        if (s_simg.resolved) return;
        s_simg.resolved = true;
        s_simg.parmsSize = fn->GetParmsSize();
        for (auto* prop : fn->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"PlayerController") s_simg.PlayerController = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved SetInputMode_GameOnly: parmsSize={} PC={}\n"),
             s_simg.parmsSize, s_simg.PlayerController);
    }

    // blockSelectedEvent param offsets
    struct BSEResolved
    {
        int bLock{-1}, selfRef{-1}, Index{-1};
        int parmsSize{0};
        bool resolved{false};
    };
    inline BSEResolved s_bse{};

    inline void resolveBSEOffsets(UFunction* fn)
    {
        if (s_bse.resolved) return;
        s_bse.resolved = true;
        s_bse.parmsSize = fn->GetParmsSize();
        for (auto* prop : fn->ForEachProperty())
        {
            std::wstring n(prop->GetName());
            int off = prop->GetOffset_Internal();
            if (n == L"bLock")      s_bse.bLock = off;
            else if (n == L"selfRef") s_bse.selfRef = off;
            else if (n == L"Index")   s_bse.Index = off;
        }
        VLOG(STR("[MoriaCppMod] Resolved blockSelectedEvent: parmsSize={} bLock={} selfRef={} Index={}\n"),
             s_bse.parmsSize, s_bse.bLock, s_bse.selfRef, s_bse.Index);
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Offset Validation — check that all critical offsets resolved successfully
    // ════════════════════════════════════════════════════════════════════════════

    inline bool uitOffsetsValid()
    {
        return s_uit.resolved && s_uit.InstanceIndex >= 0 && s_uit.NewInstanceTransform >= 0
            && s_uit.bWorldSpace >= 0 && s_uit.bMarkRenderStateDirty >= 0
            && s_uit.bTeleport >= 0 && s_uit.ReturnValue >= 0;
    }

    inline bool ltOffsetsValid()
    {
        return s_lt.resolved && s_lt.WorldContextObject >= 0 && s_lt.Start >= 0
            && s_lt.End >= 0 && s_lt.OutHit >= 0 && s_lt.ReturnValue >= 0
            && s_lt.bTraceComplex >= 0 && s_lt.bIgnoreSelf >= 0
            && s_lt.ActorsToIgnore >= 0;
    }

    inline bool dspOffsetsValid()
    {
        return s_dsp.resolved && s_dsp.ScreenX >= 0 && s_dsp.ScreenY >= 0
            && s_dsp.WorldLocation >= 0 && s_dsp.WorldDirection >= 0;
    }

    inline bool bseOffsetsValid()
    {
        return s_bse.resolved && s_bse.bLock >= 0 && s_bse.selfRef >= 0;
    }

} // namespace MoriaMods
