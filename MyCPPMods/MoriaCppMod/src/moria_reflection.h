// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_reflection.h — Property offset resolution & UFunction helpers      ║
// ║                                                                           ║
// ║  THREAD SAFETY: Offset caches are written only from the game thread.      ║
// ║  The overlay thread must never call resolveOffset or any resolver.         ║
// ║  It only reads s_overlay.slots[] which is protected by slotCS.            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once

#include "moria_common.h"
#include <Unreal/Property/FArrayProperty.hpp>

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
    inline int s_off_dtRowStruct = -2;         // UDataTable::RowStruct (UScriptStruct*)
    inline int s_off_dtRowActor = -2;          // FMorConstructionDefinition::Actor (TSoftClassPtr)
    inline int s_off_dtRowDisplayName = -2;    // FMorConstructionDefinition::DisplayName (FText)

    // Validated struct-internal offsets (resolved from native struct reflection, fallback to constants)
    inline int s_off_brushImageSize = -2;      // FSlateBrush::ImageSize (probed)
    inline int s_off_brushResourceObj = -2;    // FSlateBrush::ResourceObject (probed)
    inline int s_off_fontTypefaceName = -2;    // FSlateFontInfo::TypefaceFontName (probed)
    inline int s_off_fontSize = -2;            // FSlateFontInfo::Size (probed)
    inline int s_off_texParamValue = -2;       // FTextureParameterValue::ParameterValue (probed)

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

    // Resolve a UProperty offset by name, also returning the property size.
    // Used for runtime validation of hardcoded struct sizes.
    inline int resolveOffsetAndSize(UObject* obj, const wchar_t* propName, int& cache, int& sizeOut)
    {
        if (cache != -2) return cache;
        cache = -1;
        sizeOut = 0;
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
        VLOG(STR("[MoriaCppMod] WARNING: property '{}' not found on {} (full chain)\n"),
                                        std::wstring(propName), obj->GetClassPrivate()->GetName());
        return cache;
    }

    // Resolve a property offset by name on a UStruct directly (for DataTable row structs, etc.)
    // Same sentinel values as resolveOffset: -2 = unresolved, -1 = not found.
    inline int resolveStructFieldOffset(UStruct* strct, const wchar_t* propName, int& cache)
    {
        if (cache != -2) return cache;
        cache = -1;
        if (!strct) return -1;
        for (auto* s = strct; s; s = s->GetSuperStruct())
        {
            for (auto* prop : s->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                {
                    cache = prop->GetOffset_Internal();
                    VLOG(STR("[MoriaCppMod] Resolved struct field '{}' at offset 0x{:04X} (on {})\n"),
                         std::wstring(propName), cache, s->GetName());
                    return cache;
                }
            }
        }
        VLOG(STR("[MoriaCppMod] WARNING: struct field '{}' not found on {}\n"),
             std::wstring(propName), strct->GetName());
        return cache;
    }

    // Validate a hardcoded offset against runtime reflection.
    // Returns true if the property exists at the expected offset, false otherwise.
    // If the property is found at a different offset, logs a WARNING with both values.
    inline bool validateOffset(UObject* obj, const wchar_t* propName, int expectedOffset, const char* label)
    {
        if (!obj) return false;
        for (auto* strct = static_cast<UStruct*>(obj->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                {
                    int actual = prop->GetOffset_Internal();
                    int size = prop->GetSize();
                    if (actual == expectedOffset)
                    {
                        VLOG(STR("[MoriaCppMod] [Validate] {} OK: '{}' at 0x{:04X} (size {})\n"),
                             std::wstring(label, label + strlen(label)),
                             std::wstring(propName), actual, size);
                        return true;
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [Validate] {} MISMATCH: '{}' expected 0x{:04X} but found 0x{:04X} (size {})\n"),
                             std::wstring(label, label + strlen(label)),
                             std::wstring(propName), expectedOffset, actual, size);
                        return false;
                    }
                }
            }
        }
        VLOG(STR("[MoriaCppMod] [Validate] {} NOT FOUND: '{}' (expected 0x{:04X})\n"),
             std::wstring(label, label + strlen(label)),
             std::wstring(propName), expectedOffset);
        return false;
    }

    // Helper: find a named property on obj's class chain, return it if it's an FStructProperty.
    inline FStructProperty* findStructProperty(UObject* obj, const wchar_t* propName)
    {
        if (!obj) return nullptr;
        for (auto* strct = static_cast<UStruct*>(obj->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                    return static_cast<FStructProperty*>(prop);
            }
        }
        return nullptr;
    }

    // Ensure s_off_brush is resolved + probe FSlateBrush struct fields for validation
    inline void ensureBrushOffset(UObject* imageWidget)
    {
        if (s_off_brush == -2 && imageWidget)
        {
            resolveOffset(imageWidget, L"Brush", s_off_brush);
            // One-time probe of FSlateBrush struct fields
            if (s_off_brush >= 0 && s_off_brushImageSize == -2)
            {
                auto* structProp = findStructProperty(imageWidget, L"Brush");
                if (structProp)
                {
                    UScriptStruct* brushStruct = structProp->GetStruct();
                    if (brushStruct)
                    {
                        resolveStructFieldOffset(brushStruct, L"ImageSize", s_off_brushImageSize);
                        resolveStructFieldOffset(brushStruct, L"ResourceObject", s_off_brushResourceObj);
                        int structSize = brushStruct->GetPropertiesSize();
                        if (s_off_brushImageSize >= 0 && s_off_brushImageSize != BRUSH_IMAGE_SIZE_X)
                            VLOG(STR("[MoriaCppMod] [Validate] FSlateBrush::ImageSize MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                                 BRUSH_IMAGE_SIZE_X, s_off_brushImageSize);
                        if (s_off_brushResourceObj >= 0 && s_off_brushResourceObj != BRUSH_RESOURCE_OBJECT)
                            VLOG(STR("[MoriaCppMod] [Validate] FSlateBrush::ResourceObject MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                                 BRUSH_RESOURCE_OBJECT, s_off_brushResourceObj);
                        VLOG(STR("[MoriaCppMod] [Validate] FSlateBrush: PropertiesSize={} ImageSize@0x{:02X} ResourceObject@0x{:02X}\n"),
                             structSize,
                             s_off_brushImageSize >= 0 ? s_off_brushImageSize : BRUSH_IMAGE_SIZE_X,
                             s_off_brushResourceObj >= 0 ? s_off_brushResourceObj : BRUSH_RESOURCE_OBJECT);
                    }
                }
            }
        }
    }

    // Probe FSlateFontInfo struct fields (called once when s_off_font is first resolved)
    inline void probeFontStruct(UObject* textBlock)
    {
        if (s_off_fontTypefaceName != -2) return; // already probed
        auto* structProp = findStructProperty(textBlock, L"Font");
        if (!structProp) { s_off_fontTypefaceName = -1; s_off_fontSize = -1; return; }
        UScriptStruct* fontStruct = structProp->GetStruct();
        if (!fontStruct) { s_off_fontTypefaceName = -1; s_off_fontSize = -1; return; }
        resolveStructFieldOffset(fontStruct, L"TypefaceFontName", s_off_fontTypefaceName);
        resolveStructFieldOffset(fontStruct, L"Size", s_off_fontSize);
        int structSize = fontStruct->GetPropertiesSize();
        if (s_off_fontTypefaceName >= 0 && s_off_fontTypefaceName != FONT_TYPEFACE_NAME)
            VLOG(STR("[MoriaCppMod] [Validate] FSlateFontInfo::TypefaceFontName MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                 FONT_TYPEFACE_NAME, s_off_fontTypefaceName);
        if (s_off_fontSize >= 0 && s_off_fontSize != FONT_SIZE)
            VLOG(STR("[MoriaCppMod] [Validate] FSlateFontInfo::Size MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                 FONT_SIZE, s_off_fontSize);
        if (structSize > 0 && structSize != FONT_STRUCT_SIZE)
            VLOG(STR("[MoriaCppMod] [Validate] FSlateFontInfo size MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                 FONT_STRUCT_SIZE, structSize);
        VLOG(STR("[MoriaCppMod] [Validate] FSlateFontInfo: PropertiesSize={} TypefaceFontName@0x{:02X} Size@0x{:02X}\n"),
             structSize,
             s_off_fontTypefaceName >= 0 ? s_off_fontTypefaceName : FONT_TYPEFACE_NAME,
             s_off_fontSize >= 0 ? s_off_fontSize : FONT_SIZE);
    }

    // Probe FTextureParameterValue struct fields (called once when s_off_texParamValues is first resolved)
    inline void probeTexParamStruct(UObject* materialInstance)
    {
        if (s_off_texParamValue != -2) return; // already probed
        if (!materialInstance) { s_off_texParamValue = -1; return; }
        // TextureParameterValues is a TArray<FTextureParameterValue> — get array inner struct
        for (auto* strct = static_cast<UStruct*>(materialInstance->GetClassPrivate());
             strct;
             strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(L"TextureParameterValues"))
                {
                    auto* arrProp = static_cast<FArrayProperty*>(prop);
                    FProperty* inner = arrProp->GetInner();
                    if (inner)
                    {
                        auto* innerStructProp = static_cast<FStructProperty*>(inner);
                        UScriptStruct* elemStruct = innerStructProp->GetStruct();
                        if (elemStruct)
                        {
                            resolveStructFieldOffset(elemStruct, L"ParameterValue", s_off_texParamValue);
                            int structSize = elemStruct->GetPropertiesSize();
                            if (s_off_texParamValue >= 0 && s_off_texParamValue != TEX_PARAM_VALUE_PTR)
                                VLOG(STR("[MoriaCppMod] [Validate] FTextureParameterValue::ParameterValue MISMATCH: expected 0x{:02X}, got 0x{:02X}\n"),
                                     TEX_PARAM_VALUE_PTR, s_off_texParamValue);
                            VLOG(STR("[MoriaCppMod] [Validate] FTextureParameterValue: PropertiesSize={} ParameterValue@0x{:02X}\n"),
                                 structSize,
                                 s_off_texParamValue >= 0 ? s_off_texParamValue : TEX_PARAM_VALUE_PTR);
                        }
                        goto probed;
                    }
                }
            }
        }
        probed:
        if (s_off_texParamValue == -2) s_off_texParamValue = -1; // mark as not found
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
