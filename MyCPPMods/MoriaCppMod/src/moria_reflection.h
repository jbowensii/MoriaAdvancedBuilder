


#pragma once

#include "moria_common.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/FField.hpp>

namespace MoriaMods
{

    // Reflection-resolved offset cache.
    //
    // Each `s_off_*` is a per-process sentinel-tracked cache of a struct or
    // property byte offset that we resolve lazily via UE4 reflection
    // (UStruct::FindPropertyByName + GetOffset_Internal) and reuse on every
    // subsequent read/write. The naming convention is:
    //
    //   s_off_<short-id>   : -2 = never tried; -1 = lookup attempted and
    //                        failed; >= 0 = resolved offset in bytes
    //
    // The -2 / -1 / >=0 trichotomy lets the resolver fall back to the
    // hardcoded constants in moria_common.h (BRUSH_IMAGE_SIZE_X, etc.) when
    // reflection is unavailable (e.g. a lookup fires before the relevant
    // class is registered) WITHOUT re-querying every call. Once we land in
    // -1 we never re-query; the helper getter (e.g. brushImageSizeX())
    // returns the BRUSH_IMAGE_SIZE_X constant in that case.
    //
    // Lifetime: these are process-global. They're NOT invalidated on world
    // transition. The Moria code only resolves engine + game struct offsets
    // here; both are stable across world load/unload. If we ever needed
    // BP-derived offsets (which CAN drift across DLC), we'd need a
    // LoadMap-time reset hook - none present yet.
    //
    // Adding a new offset:
    //   1. Declare the sentinel (`inline int s_off_<x> = -2;`)
    //   2. In the corresponding probe function (e.g. ensureBrushOffset),
    //      do the lookup and write the resolved offset, or -1 on failure.
    //   3. Add a getter that returns the cached value or the moria_common.h
    //      hardcoded fallback.

    inline int s_off_font = -2;
    inline int s_off_brush = -2;
    inline int s_off_bLock = -2;
    inline int s_off_icon = -2;
    inline int s_off_blockName = -2;
    inline int s_off_texParamValues = -2;


    inline int s_off_brushImageSize = -2;
    inline int s_off_brushResourceObj = -2;
    inline int s_off_fontTypefaceName = -2;
    inline int s_off_fontSize = -2;
    inline int s_off_texParamValue = -2;


    inline int s_off_rbVariants = -2;


    inline int s_off_varResultHandle = -2;
    inline int s_off_rhRowName = -2;
    inline int s_off_variantEntrySize = -2;


    inline int s_off_iiItem = -2;
    inline int s_off_iiCount = -2;
    inline int s_off_iiID = -2;
    inline int s_off_iiDur = -2;
    inline int s_off_iiSize = -2;


    inline int s_off_iiaList = -2;

    // Active-item-effect cluster (FActiveItemEffect on FFastArraySerializer).
    inline int s_off_aieListOff      = -2;  // Effects.List inner offset (FFastArray header size; usually 0x0110)
    inline int s_off_aieStride       = -2;  // sizeof(FActiveItemEffect) (typ. 0x30)
    inline int s_off_aieOnItem       = -2;  // FActiveItemEffect.OnItem (typ. 0x0C)
    inline int s_off_aieEffect       = -2;  // FActiveItemEffect.Effect (typ. 0x10)
    inline int s_off_aieEndTime      = -2;  // FActiveItemEffect.EndTime (typ. 0x18)
    inline int s_off_aieAssetIdLo    = -2;  // FActiveItemEffect.AssetId word0 (typ. 0x1C)
    inline int s_off_aieAssetIdHi    = -2;  // FActiveItemEffect.AssetId word1 (typ. 0x20)

    // FMorConnectionHistoryItem cluster (game struct in moria_session_history.inl).
    inline int s_off_chiStride       = -2;  // sizeof(FMorConnectionHistoryItem) (typ. 0x58)
    inline int s_off_chiWorldName    = -2;  // typ. 0x00
    inline int s_off_chiConnType     = -2;  // typ. 0x10
    inline int s_off_chiInviteString = -2;  // typ. 0x18
    inline int s_off_chiUniqueInvite = -2;  // typ. 0x28
    inline int s_off_chiPassword     = -2;  // typ. 0x38
    inline int s_off_chiIsDedicated  = -2;  // typ. 0x48
    inline int s_off_chiCreated      = -2;  // typ. 0x50

    // FFGKUITab cluster (game struct in moria_settings_ui.inl tabArray walk).
    inline int s_off_uitStride       = -2;  // sizeof(FFGKUITab) (typ. 0xE8)
    inline int s_off_uitName         = -2;  // typ. 0x00
    inline int s_off_uitDisplayName  = -2;  // typ. 0x08
    inline int s_off_uitWidgetClass  = -2;  // typ. 0x20
    inline int s_off_uitTabConfig    = -2;  // typ. 0x48


    inline int brushImageSizeX() { return (s_off_brushImageSize >= 0) ? s_off_brushImageSize     : BRUSH_IMAGE_SIZE_X; }
    inline int brushImageSizeY() { return (s_off_brushImageSize >= 0) ? s_off_brushImageSize + 4 : BRUSH_IMAGE_SIZE_Y; }
    inline int brushResourceObj(){ return (s_off_brushResourceObj >= 0) ? s_off_brushResourceObj  : BRUSH_RESOURCE_OBJECT; }
    inline int fontTypefaceName(){ return (s_off_fontTypefaceName >= 0) ? s_off_fontTypefaceName  : FONT_TYPEFACE_NAME; }
    inline int fontSizeOff()     { return (s_off_fontSize >= 0)         ? s_off_fontSize          : FONT_SIZE; }
    inline int texParamValueOff(){ return (s_off_texParamValue >= 0)    ? s_off_texParamValue     : TEX_PARAM_VALUE_PTR; }


    inline int rbVariantsOff()   { return (s_off_rbVariants >= 0)      ? s_off_rbVariants        : RECIPE_BLOCK_VARIANTS; }
    inline int rbVariantsNumOff(){ return (s_off_rbVariants >= 0)      ? s_off_rbVariants + 8    : RECIPE_BLOCK_VARIANTS_NUM; }


    inline int variantRowCIOff() {
        if (s_off_varResultHandle >= 0 && s_off_rhRowName >= 0)
            return s_off_varResultHandle + s_off_rhRowName;
        return VARIANT_ROW_CI;
    }
    inline int variantRowNumOff() {
        if (s_off_varResultHandle >= 0 && s_off_rhRowName >= 0)
            return s_off_varResultHandle + s_off_rhRowName + 4;
        return VARIANT_ROW_NUM;
    }
    inline int variantEntrySize() { return (s_off_variantEntrySize >= 0) ? s_off_variantEntrySize : VARIANT_ENTRY_SIZE; }


    inline int iiItemOff()  { return (s_off_iiItem >= 0)  ? s_off_iiItem : 0x10; }
    inline int iiCountOff() { return (s_off_iiCount >= 0) ? s_off_iiCount: 0x18; }
    inline int iiIDOff()    { return (s_off_iiID >= 0)    ? s_off_iiID   : 0x20; }
    inline int iiDurOff()   { return (s_off_iiDur >= 0)   ? s_off_iiDur  : 0x24; }
    inline int iiSize()     { return (s_off_iiSize >= 0)  ? s_off_iiSize : 0x30; }


    inline int iiaListOff() { return (s_off_iiaList >= 0) ? s_off_iiaList : 0x110; }


    inline int resolveOffset(UObject* obj, const wchar_t* propName, int& cache)
    {
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


    inline void ensureBrushOffset(UObject* imageWidget)
    {
        if (s_off_brush == -2 && imageWidget)
        {
            resolveOffset(imageWidget, L"Brush", s_off_brush);

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


    inline void probeFontStruct(UObject* textBlock)
    {
        if (s_off_fontTypefaceName != -2) return;
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


    inline void probeTexParamStruct(UObject* materialInstance)
    {
        if (s_off_texParamValue != -2) return;
        if (!materialInstance) { s_off_texParamValue = -1; return; }

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
        if (s_off_texParamValue == -2) s_off_texParamValue = -1;
    }


    inline void probeRecipeBlockStruct(UObject* widget)
    {
        if (s_off_rbVariants != -2) return;
        s_off_rbVariants = -1;
        s_off_varResultHandle = -1;
        s_off_rhRowName = -1;
        s_off_variantEntrySize = -1;


        FProperty* bLockProp = widget->GetPropertyByNameInChain(STR("bLock"));
        if (!bLockProp)
        {
            VLOG(STR("[MoriaCppMod] [Validate] probeRecipeBlockStruct: bLock property not found\n"));
            return;
        }
        auto* structProp = static_cast<FStructProperty*>(bLockProp);
        UScriptStruct* recipeBlockStruct = structProp->GetStruct();
        if (!recipeBlockStruct)
        {
            VLOG(STR("[MoriaCppMod] [Validate] probeRecipeBlockStruct: UScriptStruct null\n"));
            return;
        }


        resolveStructFieldOffset(recipeBlockStruct, L"Variants", s_off_rbVariants);

        VLOG(STR("[MoriaCppMod] [Validate] FMorRecipeBlock: Variants@0x{:02X} (expected @0x{:02X})\n"),
             s_off_rbVariants >= 0 ? s_off_rbVariants : RECIPE_BLOCK_VARIANTS,
             RECIPE_BLOCK_VARIANTS);


        for (auto* prop : recipeBlockStruct->ForEachProperty())
        {
            if (prop->GetName() == std::wstring_view(L"Variants"))
            {
                auto* arrProp = static_cast<FArrayProperty*>(prop);
                FProperty* inner = arrProp->GetInner();
                if (!inner) break;
                auto* innerStructProp = static_cast<FStructProperty*>(inner);
                UScriptStruct* variantStruct = innerStructProp->GetStruct();
                if (!variantStruct) break;

                s_off_variantEntrySize = variantStruct->GetPropertiesSize();


                resolveStructFieldOffset(variantStruct, L"ResultConstructionHandle", s_off_varResultHandle);

                VLOG(STR("[MoriaCppMod] [Validate] FMorConstructionRecipeDefinition: size=0x{:X} ResultConstructionHandle@0x{:02X} (expected @0xD8)\n"),
                     s_off_variantEntrySize, s_off_varResultHandle >= 0 ? s_off_varResultHandle : 0xD8);


                if (s_off_varResultHandle >= 0)
                {
                    for (auto* vProp : variantStruct->ForEachProperty())
                    {
                        if (vProp->GetName() == std::wstring_view(L"ResultConstructionHandle"))
                        {
                            auto* handleStructProp = static_cast<FStructProperty*>(vProp);
                            UScriptStruct* handleStruct = handleStructProp->GetStruct();
                            if (handleStruct)
                            {
                                resolveStructFieldOffset(handleStruct, L"RowName", s_off_rhRowName);
                                VLOG(STR("[MoriaCppMod] [Validate] FDataTableRowHandle: RowName@0x{:02X} (expected @0x08)\n"),
                                     s_off_rhRowName >= 0 ? s_off_rhRowName : 0x08);
                            }
                            break;
                        }
                    }
                }
                break;
            }
        }
    }


    inline void probeItemInstanceStruct(UObject* invComp)
    {
        if (s_off_iiItem != -2) return;
        s_off_iiItem = -1;
        s_off_iiCount = -1;
        s_off_iiID = -1;
        s_off_iiDur = -1;
        s_off_iiSize = -1;
        s_off_iiaList = -1;


        FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
        if (!itemsProp)
        {
            VLOG(STR("[MoriaCppMod] [Validate] probeItemInstanceStruct: Items property not found\n"));
            return;
        }
        auto* itemsStructProp = static_cast<FStructProperty*>(itemsProp);
        UScriptStruct* iiaStruct = itemsStructProp->GetStruct();
        if (!iiaStruct)
        {
            VLOG(STR("[MoriaCppMod] [Validate] probeItemInstanceStruct: FItemInstanceArray struct null\n"));
            return;
        }


        resolveStructFieldOffset(iiaStruct, L"List", s_off_iiaList);
        VLOG(STR("[MoriaCppMod] [Validate] FItemInstanceArray: List@0x{:02X} (expected @0x110)\n"),
             s_off_iiaList >= 0 ? s_off_iiaList : 0x110);


        for (auto* prop : iiaStruct->ForEachProperty())
        {
            if (prop->GetName() == std::wstring_view(L"List"))
            {
                auto* arrProp = static_cast<FArrayProperty*>(prop);
                FProperty* inner = arrProp->GetInner();
                if (!inner) break;
                auto* innerStructProp = static_cast<FStructProperty*>(inner);
                UScriptStruct* iiStruct = innerStructProp->GetStruct();
                if (!iiStruct) break;

                s_off_iiSize = iiStruct->GetPropertiesSize();
                resolveStructFieldOffset(iiStruct, L"Item", s_off_iiItem);
                resolveStructFieldOffset(iiStruct, L"Count", s_off_iiCount);
                resolveStructFieldOffset(iiStruct, L"ID", s_off_iiID);
                resolveStructFieldOffset(iiStruct, L"Durability", s_off_iiDur);
                VLOG(STR("[MoriaCppMod] [Validate] FItemInstance: size=0x{:02X} Item@0x{:02X} Count@0x{:02X} ID@0x{:02X} Dur@0x{:02X}\n"),
                     s_off_iiSize, s_off_iiItem >= 0 ? s_off_iiItem : 0x10, s_off_iiCount >= 0 ? s_off_iiCount : 0x18, s_off_iiID >= 0 ? s_off_iiID : 0x20, s_off_iiDur >= 0 ? s_off_iiDur : 0x24);
                break;
            }
        }
    }


    inline void setRootWidget(UObject* widgetTree, UObject* root)
    {
        auto* slot = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
        if (slot) *slot = root;
    }


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
                    return bp;
                }
            }
        }
        VLOG(STR("[MoriaCppMod] WARNING: FBoolProperty '{}' not found on {}\n"),
             std::wstring(propName), obj->GetClassPrivate()->GetName());
        return nullptr;
    }


    inline bool setBoolProp(UObject* obj, const wchar_t* propName, bool value)
    {
        auto* bp = resolveBoolProperty(obj, propName);
        if (!bp) return false;
        bp->SetPropertyValueInContainer(obj, value);
        return true;
    }


    inline bool getBoolProp(UObject* obj, const wchar_t* propName)
    {
        auto* bp = resolveBoolProperty(obj, propName);
        if (!bp) return false;
        return bp->GetPropertyValueInContainer(obj);
    }


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

    inline bool bseOffsetsValid()
    {
        return s_bse.resolved && s_bse.bLock >= 0 && s_bse.selfRef >= 0;
    }

    // Resolves the size + a property offset of a UScriptStruct that's the
    // element type of a TArray<FStructXxx> property on `owner`. Used to
    // back-fill stride + inner-field offsets via reflection rather than
    // hardcoding them.
    //
    //   owner          : the UObject whose TArray<FStructXxx> we walk
    //   arrayPropName  : name of the TArray property on owner
    //   fieldName      : name of an inner field on FStructXxx (or nullptr
    //                    to only resolve stride)
    // Returns the inner field's offset, or -1 if any step failed.
    // Always also writes the struct stride into outStride if the struct
    // was found (even when fieldName is null or fieldName lookup fails).
    inline int resolveArrayStructLayout(UObject* owner,
                                        const wchar_t* arrayPropName,
                                        const wchar_t* fieldName,
                                        int* outStride)
    {
        if (outStride) *outStride = -1;
        if (!owner || !arrayPropName) return -1;
        auto* prop = owner->GetPropertyByNameInChain(arrayPropName);
        if (!prop) return -1;
        auto* arrProp = CastField<FArrayProperty>(prop);
        if (!arrProp) return -1;
        FProperty* inner = arrProp->GetInner();
        if (!inner) return -1;
        auto* sProp = CastField<FStructProperty>(inner);
        if (!sProp) return -1;
        UScriptStruct* sStruct = sProp->GetStruct();
        if (!sStruct) return -1;
        if (outStride) *outStride = static_cast<int>(sStruct->GetStructureSize());
        if (!fieldName) return -1;
        auto* fProp = sStruct->GetPropertyByNameInChain(fieldName);
        return fProp ? fProp->GetOffset_Internal() : -1;
    }

    // Same as resolveArrayStructLayout but for a single FStructProperty
    // (not a TArray). Used for FMorConnectionHistoryItem etc. that
    // appear as struct-typed UFunction parms.
    //
    // Returns the inner field's offset, or -1 on failure. outStride gets
    // the struct's GetStructureSize() if the struct was located.
    inline int resolveStructLayout(UStruct* sStruct,
                                   const wchar_t* fieldName,
                                   int* outStride)
    {
        if (outStride) *outStride = -1;
        if (!sStruct) return -1;
        if (auto* ss = sStruct->IsA<UScriptStruct>() ? static_cast<UScriptStruct*>(sStruct) : nullptr)
            if (outStride) *outStride = static_cast<int>(ss->GetStructureSize());
        if (!fieldName) return -1;
        auto* fProp = sStruct->GetPropertyByNameInChain(fieldName);
        return fProp ? fProp->GetOffset_Internal() : -1;
    }

}
