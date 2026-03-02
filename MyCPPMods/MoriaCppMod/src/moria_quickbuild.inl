// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_quickbuild.inl — Recipe slots, quick-build SM, icon extraction     ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6G: Quick-Build System Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // F1-F8 recipe slots: capture bLock from build menu, replay via state machine
        // Key discovery: bLock at widget+616 is THE recipe identifier
        // State machine: open menu Ã¢â€ â€™ wait for build tab Ã¢â€ â€™ find widget Ã¢â€ â€™ select recipe

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

        // Cached Build_Tab lookup Ã¢â‚¬â€ avoids FindAllOf on every check
        UObject* getCachedBuildTab()
        {
            if (m_cachedBuildTab)
            {
                if (!isWidgetAlive(m_cachedBuildTab) || safeClassName(m_cachedBuildTab) != L"UI_WBP_Build_Tab_C")
                {
                    m_cachedBuildTab = nullptr;
                    m_fnIsVisible = nullptr;
                }
            }
            if (!m_cachedBuildTab)
            {
                m_cachedBuildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
                if (m_cachedBuildTab && !isWidgetAlive(m_cachedBuildTab))
                    m_cachedBuildTab = nullptr; // findWidgetByClass returned a dying widget
                if (m_cachedBuildTab)
                    m_fnIsVisible = m_cachedBuildTab->GetFunctionByNameInChain(STR("IsVisible"));
            }
            return m_cachedBuildTab;
        }

        // Cheap Build_Tab visibility check via cached IsVisible() Ã¢â‚¬â€ standard UWidget check
        // Uses IsVisible (UMG widget visibility) instead of IsShowing (FGK framework state)
        // because IsShowing() on UFGKUIScreen may not reflect actual widget visibility
        bool isBuildTabShowing()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab || !m_fnIsVisible) return false;
            struct { bool Ret{false}; } params{};
            tab->ProcessEvent(m_fnIsVisible, &params);
            return params.Ret;
        }

        // Open build tab via FGK Show() API (no B key toggle)
        void showBuildTab()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab) return;
            auto* fn = tab->GetFunctionByNameInChain(STR("Show"));
            if (fn) tab->ProcessEvent(fn, nullptr);
        }

        // Close build tab via FGK Hide() API (no B key toggle)
        void hideBuildTab()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab) return;
            auto* fn = tab->GetFunctionByNameInChain(STR("Hide"));
            if (fn) tab->ProcessEvent(fn, nullptr);
        }

        // Count visible Build_Item_Medium widgets (used to detect cold-start widget creation)
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
            // HUD is showing Ã¢â‚¬â€ check if past the recipe picker
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
            file << "NoCollision = " << (m_noCollisionWhileFlying ? "true" : "false") << "\n";

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
                            else if (strEqualCI(kv->key, "NoCollision"))
                            {
                                m_noCollisionWhileFlying = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
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
        // were removed in v1.11 Ã¢â‚¬â€ dead code from the CPU texture extraction attempt.
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
            ULONG_PTR gdipTokenLocal = 0; // declared outside try so catch can clean up
            try
            {
                // --- Get UTexture2D from the widget chain ---
                // widgetÃ¢â€ â€™Icon Ã¢â€ â€™ Brush.ResourceObject(MID) Ã¢â€ â€™ TextureParameterValues[0]+16 Ã¢â€ â€™ UTexture2D*
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
                Gdiplus::GdiplusStartup(&gdipTokenLocal, &gdipInput, nullptr);
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
                                        Gdiplus::GdiplusShutdown(gdipTokenLocal);
                                        return true;
                                    }
                                    break;
                                }
                            }
                        }
                        delete img;
                    }
                }
                Gdiplus::GdiplusShutdown(gdipTokenLocal);
                VLOG(STR("[MoriaCppMod] [Icon] PNG conversion failed\n"));
                return false;
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [Icon] Exception during extraction\n"));
                if (gdipTokenLocal) Gdiplus::GdiplusShutdown(gdipTokenLocal);
                return false;
            }
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6H: Icon Extraction & Build-from-Target Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // Extracts UTexture2D from build menu widgets via Canvas render target.
        // Chain: widget+1104 Ã¢â€ â€™ Image+336 Ã¢â€ â€™ MID+256 Ã¢â€ â€™ TextureParamValues[0]+16
        // Pipeline: CreateRenderTarget2D Ã¢â€ â€™ BeginDraw Ã¢â€ â€™ K2_DrawTexture Ã¢â€ â€™ EndDraw
        //           Ã¢â€ â€™ ExportRenderTarget Ã¢â€ â€™ GDI+ PNG conversion
        // Icons cached to <game>/Mods/MoriaCppMod/icons/*.png

        // Extract UTexture2D name from a Build_Item_Medium widget
        // Chain: widget+1104 (Icon Image) Ã¢â€ â€™ Image+264+72 (MID) Ã¢â€ â€™ MID+256 (TextureParamValues TArray) Ã¢â€ â€™ entry+16 (UTexture2D*)
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
                VLOG(STR("[MoriaCppMod] [{}] '{}' Ã¢â‚¬â€ no bLock data\n"), context, displayName);
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

            // Check if build menu is open Ã¢â‚¬â€ if not, treat as "no build object selected"
            UObject* buildTab = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
            bool hasBuildObject = m_hasLastCapture && !m_lastCapturedName.empty() && buildTab;

            // No build object selected Ã¢â€ â€™ clear the slot
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
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] CLEARED F{}\n"), slot + 1);
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
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} icon: '{}'\n"), slot + 1, m_recipeSlots[slot].textureName);

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

            QBLOG(STR("[MoriaCppMod] [QuickBuild] ASSIGN F{} = '{}'\n"), slot + 1, m_lastCapturedName);
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
        SelectResult selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;
            const std::wstring& slotTexture = m_recipeSlots[slot].textureName;

            QBLOG(STR("[MoriaCppMod] [QuickBuild] SELECT: searching for '{}' tex='{}' (slot F{})\n"), targetName, slotTexture, slot + 1);

            // Find all Build_Item_Medium widgets and match by display name + icon texture
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            int visibleCount = 0;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                // Only use VISIBLE widgets Ã¢â‚¬â€ stale/recycled widgets have invalid internal state
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
                    // Disambiguate same-name items by icon texture (e.g., "Column Crown" variants)
                    if (!slotTexture.empty())
                    {
                        std::wstring widgetTex = extractIconTextureName(w);
                        if (widgetTex != slotTexture) continue;
                    }
                    matchedWidget = w;
                    break;
                }
            }

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   checked {} visible widgets, match: {}\n"), visibleCount, matchedWidget ? L"YES" : L"NO");

            if (!matchedWidget)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] No match among {} visible widgets\n"), visibleCount);
                return (visibleCount == 0) ? SelectResult::Loading : SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            // Resolve blockSelectedEvent param layout via reflection
            resolveBSEOffsets(func);
            if (!bseOffsetsValid())
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] blockSelectedEvent param resolution failed\n"));
                return SelectResult::NotFound;
            }

            std::vector<uint8_t> params(s_bse.parmsSize, 0);
            bool gotFreshBLock = false;

            // Resolve bLock offset on first quickbuild (walks inheritance chain to UI_WBP_Build_Item_C)
            if (s_off_bLock == -2)
                resolveOffset(matchedWidget, L"bLock", s_off_bLock);

            // BEST: read fresh bLock directly from the matched widget (offset resolved via reflection)
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (@0x{:X})\n"), s_off_bLock);
            }

            // FALLBACK: use captured bLock from assignment (may have stale pointer at +104)
            if (!gotFreshBLock && m_recipeSlots[slot].hasBLockData)
            {
                std::memcpy(params.data() + s_bse.bLock, m_recipeSlots[slot].bLockData, BLOCK_DATA_SIZE);
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using SAVED bLock (may be stale)\n"));
            }
            else if (!gotFreshBLock)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   WARNING: no bLock data at all, using zeros\n"));
            }

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   calling blockSelectedEvent with selfRef={:p}\n"), static_cast<void*>(matchedWidget));

            // Suppress post-hook capture during automated selection (RAII guard ensures reset on exception)
            m_isAutoSelecting = true;
            struct AutoSelectGuard
            {
                bool& flag;
                ~AutoSelectGuard() { flag = false; }
            } guard{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params.data());
            m_isAutoSelecting = false;

            logBLockDiagnostics(L"BUILD", targetName, params.data());

            showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;       // track menu so we refresh ActionBar when it closes
            refreshActionBar();              // also refresh immediately after recipe selection

            // Set this slot as Active on the builders bar, all others become Inactive/Empty
            m_activeBuilderSlot = slot;
            updateBuildersBar();
            return SelectResult::Found;
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
                QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} pressed but phase {} active, ignoring\n"),
                                                slot + 1, static_cast<int>(m_qbPhase));
                return;
            }

            QBLOG(STR("[MoriaCppMod] [QuickBuild] ACTIVATE F{} -> '{}' (charLoaded={} frameCounter={})\n"),
                                            slot + 1,
                                            m_recipeSlots[slot].displayName,
                                            m_characterLoaded,
                                            m_frameCounter);

            m_pendingQuickBuildSlot = slot;
            m_qbStartTime = GetTickCount64();
            m_qbRetryTime = m_qbStartTime;

            // Reactive phase transitions: check live game state and proceed accordingly
            if (!m_buildMenuPrimed)
            {
                // First quickbuild in session Ã¢â‚¬â€ prime via B key (creates widget) + OnAfterShow
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Cold start Ã¢â‚¬â€ priming build menu\n"));
                m_buildTabAfterShowFired = false;
                if (!isBuildTabShowing())
                {
                    keybd_event(0x42, 0, 0, 0);
                    keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                }
                m_qbPhase = QBPhase::PrimeOpen;
            }
            else if (isPlacementActive())
            {
                // Player is holding a ghost piece Ã¢â‚¬â€ cancel with ESC first
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Placement active Ã¢â‚¬â€ sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = QBPhase::CancelPlacement;
            }
            else if (isBuildTabShowing())
            {
                // Build menu is open Ã¢â‚¬â€ close it first, then reopen fresh
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Build tab already open Ã¢â‚¬â€ closing first\n"));
                hideBuildTab();
                m_qbPhase = QBPhase::CloseMenu;
            }
            else
            {
                // Menu not open Ã¢â‚¬â€ open it via FGK Show() API
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Build tab not open Ã¢â‚¬â€ calling Show()\n"));
                showBuildTab();
                m_qbPhase = QBPhase::OpenMenu;
            }
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ Build from Target: Shift+F10 Ã¢â‚¬â€ build the last targeted actor Ã¢â€â‚¬Ã¢â€â‚¬

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
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Already active (phase {}), ignoring\n"), static_cast<int>(m_tbPhase));
                return;
            }

            if (!m_lastTargetBuildable || (m_targetBuildName.empty() && m_targetBuildRecipeRef.empty()))
            {
                showOnScreen(Loc::get("msg.no_buildable_target").c_str(), 3.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            m_pendingTargetBuild = true;
            m_tbStartTime = GetTickCount64();
            m_tbRetryTime = m_tbStartTime;

            // Reactive phase transitions: check live game state and proceed accordingly
            if (!m_buildMenuPrimed)
            {
                // First build in session Ã¢â‚¬â€ prime via B key (creates widget) + OnAfterShow
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Cold start Ã¢â‚¬â€ priming build menu\n"));
                m_buildTabAfterShowFired = false;
                if (!isBuildTabShowing())
                {
                    keybd_event(0x42, 0, 0, 0);
                    keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                }
                m_tbPhase = QBPhase::PrimeOpen;
            }
            else if (isPlacementActive())
            {
                // Player is holding a ghost piece Ã¢â‚¬â€ cancel with ESC first
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Placement active Ã¢â‚¬â€ sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_tbPhase = QBPhase::CancelPlacement;
            }
            else if (isBuildTabShowing())
            {
                // Build menu is open Ã¢â‚¬â€ close it first, then reopen fresh
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Build tab already open Ã¢â‚¬â€ closing first\n"));
                hideBuildTab();
                m_tbPhase = QBPhase::CloseMenu;
            }
            else
            {
                // Menu not open Ã¢â‚¬â€ open it via FGK Show() API
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Build tab not open Ã¢â‚¬â€ calling Show()\n"));
                showBuildTab();
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

        SelectResult selectRecipeByTargetName(UObject* buildTab)
        {
            QBLOG(STR("[MoriaCppMod] [TargetBuild] Searching: name='{}' recipeRef='{}' bLockOffset=0x{:X}\n"),
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

                QBLOG(STR("[MoriaCppMod] [TargetBuild] FName CIs: full='{}' CI={}, short='{}' CI={}\n"),
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
                    QBLOG(STR("[MoriaCppMod] [TargetBuild]   W[{}] obj='{}' display='{}'\n"), visibleCount, objName, name);
                }

                // Strategy 1: bLock-based matching via Variants->ResultConstructionHandle RowName
                if (!matchedWidget && s_off_bLock >= 0 && !m_targetBuildRecipeRef.empty())
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(w);
                    uint8_t* bLock = widgetBase + s_off_bLock;

                    if (!isReadableMemory(bLock, BLOCK_DATA_SIZE))
                    {
                        bLockNullCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock=NULL\n"));
                    }
                    else if (!isReadableMemory(bLock + RECIPE_BLOCK_VARIANTS, 16))
                    {
                        bLockMemFailCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock+104 not readable\n"));
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
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock tag CI={} Num={} | variants={} ptr={:p}\n"),
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
                                QBLOG(STR("[MoriaCppMod] [TargetBuild]     RowName CI={} Num={}\n"), rowCI, rowNum);
                            }

                            // Check if RowName CI matches any of our target FName CIs
                            for (auto& [tName, tCI] : targetFNames)
                            {
                                if (tCI == rowCI)
                                {
                                    matchedWidget = w;
                                    matchedName = name.empty() ? tName : name;
                                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (bLock RowName CI={}) on '{}' target='{}'\n"),
                                                                    rowCI,
                                                                    matchedName,
                                                                    tName);
                                    break;
                                }
                            }
                        }
                        else if (isFirstFew)
                        {
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     variantsPtr not readable (0xE8 bytes)\n"));
                        }
                    }
                }

                // Strategy 2: Exact display name match
                if (!matchedWidget && !name.empty() && name == m_targetBuildName)
                {
                    matchedWidget = w;
                    matchedName = name;
                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (exact display name) on '{}'\n"), name);
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
                        QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (normalized exact) on '{}'\n"), name);
                    }
                }

                if (matchedWidget) break;
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Result: {} visible, bLockNull={} memFail={} varEmpty={} match={}\n"),
                                            visibleCount,
                                            bLockNullCount,
                                            bLockMemFailCount,
                                            variantsEmptyCount,
                                            matchedWidget ? matchedName.c_str() : L"NO");

            if (!matchedWidget)
            {
                if (visibleCount == 0) return SelectResult::Loading;
                showOnScreen((L"Recipe '" + m_targetBuildName + L"' not found in build menu").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                return SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            // Resolve blockSelectedEvent param layout via reflection
            resolveBSEOffsets(func);
            if (!bseOffsetsValid()) return SelectResult::NotFound;

            std::vector<uint8_t> params(s_bse.parmsSize, 0);

            // Read fresh bLock data from the matched widget
            bool gotFreshBLock = false;
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Calling blockSelectedEvent: freshBLock={} selfRef={:p}\n"),
                                            gotFreshBLock,
                                            static_cast<void*>(matchedWidget));

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;

            // Suppress post-hook capture during automated selection (RAII guard ensures reset on exception)
            m_isAutoSelecting = true;
            struct AutoSelectGuard
            {
                bool& flag;
                ~AutoSelectGuard() { flag = false; }
            } guard{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params.data());
            m_isAutoSelecting = false;

            logBLockDiagnostics(L"TARGET-BUILD", matchedName, params.data());

            showOnScreen((L"Build: " + matchedName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;       // track menu so we refresh ActionBar when it closes
            refreshActionBar();              // also refresh immediately after recipe selection
            return SelectResult::Found;
        }

