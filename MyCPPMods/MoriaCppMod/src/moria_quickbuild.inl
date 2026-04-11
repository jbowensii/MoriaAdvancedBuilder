


        UObject* getCachedBuildTab()
        {
            UObject* tab = m_cachedBuildTab.Get();
            if (tab && safeClassName(tab) == L"UI_WBP_Build_Tab_C")
                return tab;

            UObject* found = findWidgetByClass(L"UI_WBP_Build_Tab_C", false);
            if (found)
            {
                m_cachedBuildTab = RC::Unreal::FWeakObjectPtr(found);
                QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildTab: fresh lookup -> FOUND\n"));
                return found;
            }
            QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildTab: fresh lookup -> NOT FOUND\n"));
            return nullptr;
        }


        void refreshActionBar()
        {
            UObject* actionBar = findWidgetByClass(L"WBP_UI_ActionBar_C", true);
            if (!actionBar || !isObjectAlive(actionBar))
                return;

            auto* refreshFunc = actionBar->GetFunctionByNameInChain(STR("Set All Action Bar Items"));
            if (refreshFunc)
            {
                safeProcessEvent(actionBar, refreshFunc, nullptr);
                VLOG(STR("[MoriaCppMod] ActionBar: Set All Action Bar Items called\n"));
            }
        }

        void saveQuickBuildSlots()
        {
            std::ofstream file(modPath("Mods/MoriaCppMod/quickbuild_slots.txt"), std::ios::trunc);
            if (!file.is_open()) { VLOG(STR("[MoriaCppMod] saveQuickBuildSlots: failed to open file for writing\n")); return; }
            file << "# MoriaCppMod quick-build slots (F1-F8)\n";
            file << "# slot|displayName|textureName|rowName\n";
            for (int i = 0; i < OVERLAY_BUILD_SLOTS; i++)
            {
                if (!m_recipeSlots[i].used) continue;
                std::string narrowName, narrowTex, narrowRow;
                for (wchar_t c : m_recipeSlots[i].displayName)
                    narrowName.push_back(static_cast<char>(c));
                for (wchar_t c : m_recipeSlots[i].textureName)
                    narrowTex.push_back(static_cast<char>(c));
                for (wchar_t c : m_recipeSlots[i].rowName)
                    narrowRow.push_back(static_cast<char>(c));
                file << i << "|" << narrowName << "|" << narrowTex << "|" << narrowRow << "\n";
            }

        }

        void loadQuickBuildSlots()
        {
            std::ifstream file(modPath("Mods/MoriaCppMod/quickbuild_slots.txt"));
            if (!file.is_open()) { VLOG(STR("[MoriaCppMod] loadQuickBuildSlots: file not found or unreadable\n")); return; }
            std::string line;
            int loaded = 0;
            while (std::getline(file, line))
            {
                auto parsed = parseSlotLine(line);
                if (auto* slot = std::get_if<ParsedSlot>(&parsed))
                {
                    m_recipeSlots[slot->slotIndex].displayName = std::wstring(slot->displayName.begin(), slot->displayName.end());
                    m_recipeSlots[slot->slotIndex].textureName = std::wstring(slot->textureName.begin(), slot->textureName.end());
                    m_recipeSlots[slot->slotIndex].rowName = std::wstring(slot->rowName.begin(), slot->rowName.end());
                    m_recipeSlots[slot->slotIndex].used = true;
                    loaded++;
                }
            }
            if (loaded > 0)
            {
                VLOG(STR("[MoriaCppMod] Loaded {} quick-build slots from disk\n"), loaded);
                updateOverlaySlots();
                updateBuildersBar();
            }
        }

        void saveConfig()
        {
            std::ofstream file(iniPath(), std::ios::trunc);
            if (!file.is_open()) { VLOG(STR("[MoriaCppMod] saveConfig: failed to open INI for writing\n")); return; }

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
                for (auto wc : wname) name += static_cast<char>(wc);
                file << iniKey << " = " << name << "\n";
                if (!s_bindings[i].enabled)
                    file << iniKey << "_Enabled = false\n";
            }
            file << "ModifierKey = " << modifierToIniName(s_modifierVK) << "\n";

            file << "\n[Preferences]\n";
            file << "Verbose = " << (s_verbose ? "true" : "false") << "\n";
            file << "RotationStep = " << s_overlay.rotationStep.load() << "\n";
            file << "Language = " << s_language << "\n";
            file << "NoCollision = " << (m_noCollisionWhileFlying ? "true" : "false") << "\n";
            file << "TrashItem = " << (m_trashItemEnabled ? "true" : "false") << "\n";
            file << "ReplenishItem = " << (m_replenishItemEnabled ? "true" : "false") << "\n";
            file << "RemoveAttributes = " << (m_removeAttrsEnabled ? "true" : "false") << "\n";
            file << "PitchRotate = " << (m_pitchRotateEnabled ? "true" : "false") << "\n";
            file << "RollRotate = " << (m_rollRotateEnabled ? "true" : "false") << "\n";

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
            std::ifstream file(iniPath());
            if (file.is_open())
            {
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

                                std::string k = kv->key;
                                bool isEnabledKey = false;
                                if (k.size() > 8 && strEqualCI(k.substr(k.size() - 8), "_Enabled"))
                                {
                                    std::string base = k.substr(0, k.size() - 8);
                                    int idx = iniKeyToBindIndex(base);
                                    if (idx >= 0)
                                    {
                                        s_bindings[idx].enabled = !(kv->value == "false" || kv->value == "0" || kv->value == "no");
                                        isEnabledKey = true;
                                    }
                                }
                                if (!isEnabledKey)
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
                            else if (strEqualCI(kv->key, "TrashItem"))
                            {
                                m_trashItemEnabled = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
                            }
                            else if (strEqualCI(kv->key, "ReplenishItem"))
                            {
                                m_replenishItemEnabled = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
                            }
                            else if (strEqualCI(kv->key, "RemoveAttributes"))
                            {
                                m_removeAttrsEnabled = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
                            }
                            else if (strEqualCI(kv->key, "PitchRotate"))
                            {
                                m_pitchRotateEnabled = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
                            }
                            else if (strEqualCI(kv->key, "RollRotate"))
                            {
                                m_rollRotateEnabled = (kv->value == "true" || kv->value == "1" || kv->value == "yes");
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


            std::ifstream oldFile(oldKeybindPath());
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

                saveConfig();
                std::rename(oldKeybindPath().c_str(), modPath("Mods/MoriaCppMod/keybindings.txt.bak").c_str());
                return;
            }


            saveConfig();
        }


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


        bool extractAndSaveIcon(UObject* widget, const std::wstring& textureName,
                                const std::wstring& outPath, UObject* textureOverride = nullptr)
        {
            QBLOG(STR("[MoriaCppMod] [QB] extractAndSaveIcon ENTER: widget={:p} tex='{}' path='{}' override={:p}\n"),
                  (void*)widget, textureName, outPath, (void*)textureOverride);
            if (textureName.empty()) return false;
            if (!textureOverride && !widget) return false;
            try
            {

                UObject* texture = textureOverride;
                if (!texture && widget)
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
                            UObject* brushResource = *reinterpret_cast<UObject**>(imgBase + s_off_brush + brushResourceObj());
                            if (brushResource && isReadableMemory(brushResource, 64))
                            {
                                std::wstring resClass = safeClassName(brushResource);
                                if (resClass.find(L"Texture2D") != std::wstring::npos)
                                {
                                    texture = brushResource;
                                }
                                else if (resClass.find(L"Material") != std::wstring::npos && isReadableMemory(brushResource, 280))
                                {

                                    if (s_off_texParamValues == -2)
                                    {
                                        resolveOffset(brushResource, L"TextureParameterValues", s_off_texParamValues);
                                        probeTexParamStruct(brushResource);
                                    }
                                    if (s_off_texParamValues >= 0)
                                    {
                                        uint8_t* midBase = reinterpret_cast<uint8_t*>(brushResource);
                                        uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + s_off_texParamValues);
                                        int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + s_off_texParamValues + 8);
                                        if (arrNum >= 1 && arrNum <= 32 && arrData && isReadableMemory(arrData, 40))
                                        {
                                            texture = *reinterpret_cast<UObject**>(arrData + texParamValueOff());
                                            if (texture && !isReadableMemory(texture, 64)) texture = nullptr;
                                        }
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
                if (!worldCtx || !isObjectAlive(worldCtx))
                {
                    VLOG(STR("[MoriaCppMod] [Icon] No valid PlayerController\n"));
                    return false;
                }


                auto* krlClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary"));
                if (!krlClass) return false;
                UObject* krlCDO = krlClass->GetClassDefaultObject();
                if (!krlCDO) return false;


                UObject* renderTarget = nullptr;
                {
                    int pSz = createRTFn->GetParmsSize();
                    std::vector<uint8_t> params(pSz, 0);
                    auto* pWC = findParam(createRTFn, STR("WorldContextObject"));
                    auto* pW = findParam(createRTFn, STR("Width"));
                    auto* pH = findParam(createRTFn, STR("Height"));
                    auto* pF = findParam(createRTFn, STR("Format"));
                    auto* pRV = findParam(createRTFn, STR("ReturnValue"));
                    if (!pWC || !pRV) { VLOG(STR("[MoriaCppMod] [Icon] CreateRenderTarget2D: critical params missing\n")); return false; }
                    if (pWC) *reinterpret_cast<UObject**>(params.data() + pWC->GetOffset_Internal()) = worldCtx;
                    if (pW) *reinterpret_cast<int32_t*>(params.data() + pW->GetOffset_Internal()) = 128;
                    if (pH) *reinterpret_cast<int32_t*>(params.data() + pH->GetOffset_Internal()) = 128;
                    if (pF) params[pF->GetOffset_Internal()] = 2;
                    safeProcessEvent(krlCDO, createRTFn, params.data());
                    renderTarget = pRV ? *reinterpret_cast<UObject**>(params.data() + pRV->GetOffset_Internal()) : nullptr;
                }
                if (!renderTarget)
                {
                    VLOG(STR("[MoriaCppMod] [Icon] CreateRenderTarget2D returned null\n"));
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] Created render target OK\n"));


                UObject* canvas = nullptr;
                std::vector<uint8_t> beginParams;
                {
                    int pSz = beginDrawFn->GetParmsSize();
                    beginParams.resize(pSz, 0);
                    auto* bWC = findParam(beginDrawFn, STR("WorldContextObject"));
                    auto* bRT = findParam(beginDrawFn, STR("TextureRenderTarget"));
                    auto* bCanvas = findParam(beginDrawFn, STR("Canvas"));
                    if (!bWC || !bRT) { VLOG(STR("[MoriaCppMod] [Icon] BeginDraw: critical params missing\n")); return false; }
                    *reinterpret_cast<UObject**>(beginParams.data() + bWC->GetOffset_Internal()) = worldCtx;
                    *reinterpret_cast<UObject**>(beginParams.data() + bRT->GetOffset_Internal()) = renderTarget;
                    safeProcessEvent(krlCDO, beginDrawFn, beginParams.data());
                    canvas = bCanvas ? *reinterpret_cast<UObject**>(beginParams.data() + bCanvas->GetOffset_Internal()) : nullptr;
                }
                if (!canvas)
                {
                    VLOG(STR("[MoriaCppMod] [Icon] BeginDrawCanvasToRenderTarget returned no Canvas\n"));

                    {
                        int pSz = endDrawFn->GetParmsSize();
                        std::vector<uint8_t> eParams(pSz, 0);
                        auto* eWC = findParam(endDrawFn, STR("WorldContextObject"));
                        auto* eCtx = findParam(endDrawFn, STR("Context"));
                        if (eWC) *reinterpret_cast<UObject**>(eParams.data() + eWC->GetOffset_Internal()) = worldCtx;
                        if (eCtx)
                        {
                            auto* bCtx = findParam(beginDrawFn, STR("Context"));
                            if (bCtx && bCtx->GetSize() <= eCtx->GetSize())
                                memcpy(eParams.data() + eCtx->GetOffset_Internal(), beginParams.data() + bCtx->GetOffset_Internal(), bCtx->GetSize());
                        }
                        safeProcessEvent(krlCDO, endDrawFn, eParams.data());
                    }
                    return false;
                }
                VLOG(STR("[MoriaCppMod] [Icon] Got Canvas: {} '{}'\n"), safeClassName(canvas), std::wstring(canvas->GetName()));


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
                    if (!dTex) { VLOG(STR("[MoriaCppMod] [Icon] K2_DrawTexture: RenderTexture param missing, skipping draw\n")); }
                    else
                    {
                    *reinterpret_cast<UObject**>(dtParams.data() + dTex->GetOffset_Internal()) = texture;
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
                    if (dBlend) *reinterpret_cast<uint8_t*>(dtParams.data() + dBlend->GetOffset_Internal()) = 0;
                    if (dRotation) *reinterpret_cast<float*>(dtParams.data() + dRotation->GetOffset_Internal()) = 0.0f;
                    if (dPivot)
                    {
                        auto* v = reinterpret_cast<float*>(dtParams.data() + dPivot->GetOffset_Internal());
                        v[0] = 0.5f;
                        v[1] = 0.5f;
                    }

                    safeProcessEvent(canvas, drawTexFn, dtParams.data());
                    VLOG(STR("[MoriaCppMod] [Icon] Drew texture to canvas\n"));
                    }
                }


                {
                    int pSz = endDrawFn->GetParmsSize();
                    std::vector<uint8_t> eParams(pSz, 0);
                    auto* eWC = findParam(endDrawFn, STR("WorldContextObject"));
                    auto* eCtx = findParam(endDrawFn, STR("Context"));
                    if (eWC) *reinterpret_cast<UObject**>(eParams.data() + eWC->GetOffset_Internal()) = worldCtx;

                    if (eCtx)
                    {
                        auto* bCtx = findParam(beginDrawFn, STR("Context"));
                        if (bCtx && bCtx->GetSize() <= eCtx->GetSize())
                        {
                            memcpy(eParams.data() + eCtx->GetOffset_Internal(), beginParams.data() + bCtx->GetOffset_Internal(), bCtx->GetSize());
                        }
                    }
                    safeProcessEvent(krlCDO, endDrawFn, eParams.data());
                    VLOG(STR("[MoriaCppMod] [Icon] EndDrawCanvasToRenderTarget OK\n"));
                }


                {
                    int eSz = exportRTFn->GetParmsSize();
                    std::vector<uint8_t> eParams(eSz, 0);
                    auto* eWC = findParam(exportRTFn, STR("WorldContextObject"));
                    auto* eRT = findParam(exportRTFn, STR("TextureRenderTarget"));
                    auto* eFP = findParam(exportRTFn, STR("FilePath"));
                    auto* eFN = findParam(exportRTFn, STR("FileName"));
                    if (!eWC || !eRT) { VLOG(STR("[MoriaCppMod] [Icon] ExportRT: critical params missing\n")); }
                    else
                    {

                    *reinterpret_cast<UObject**>(eParams.data() + eWC->GetOffset_Internal()) = worldCtx;
                    *reinterpret_cast<UObject**>(eParams.data() + eRT->GetOffset_Internal()) = renderTarget;

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
                    safeProcessEvent(krlCDO, exportRTFn, eParams.data());
                    VLOG(STR("[MoriaCppMod] [Icon] ExportRenderTarget called\n"));
                    }
                }


                {
                    auto* releaseRTFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetRenderingLibrary:ReleaseRenderTarget2D"));
                    if (releaseRTFn)
                    {
                        int pSz = releaseRTFn->GetParmsSize();
                        std::vector<uint8_t> rParams(pSz, 0);
                        auto* rRT = findParam(releaseRTFn, STR("TextureRenderTarget"));
                        if (!rRT)
                        {
                            VLOG(STR("[MoriaCppMod] [Icon] ReleaseRenderTarget2D: param not found, skipping\n"));
                        }
                        else
                        {
                            *reinterpret_cast<UObject**>(rParams.data() + rRT->GetOffset_Internal()) = renderTarget;
                            safeProcessEvent(krlCDO, releaseRTFn, rParams.data());
                        }
                        VLOG(STR("[MoriaCppMod] [Icon] ReleaseRenderTarget2D OK\n"));
                    }
                }


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


                static ULONG_PTR s_gdipToken = 0;
                if (!s_gdipToken)
                {
                    Gdiplus::GdiplusStartupInput gdipInput;
                    Gdiplus::GdiplusStartup(&s_gdipToken, &gdipInput, nullptr);
                }
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
                                        return true;
                                    }
                                    break;
                                }
                            }
                        }
                        delete img;
                    }
                }
                VLOG(STR("[MoriaCppMod] [Icon] PNG conversion failed\n"));
                return false;
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [Icon] Exception during extraction\n"));
                return false;
            }
        }


        std::wstring extractIconTextureName(UObject* widget)
        {
            if (!widget) { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: widget=null\n")); return L""; }
            try
            {
                uint8_t* base = reinterpret_cast<uint8_t*>(widget);
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: widget={:p} cls='{}'\n"),
                      (void*)widget, safeClassName(widget));
                if (s_off_icon == -2) resolveOffset(widget, L"Icon", s_off_icon);
                if (s_off_icon < 0) { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: s_off_icon not resolved\n")); return L""; }
                UObject* iconImg = *reinterpret_cast<UObject**>(base + s_off_icon);
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: iconImg={:p} readable={}\n"),
                      (void*)iconImg, iconImg ? isReadableMemory(iconImg, 400) : false);
                if (!iconImg || !isReadableMemory(iconImg, 400)) return L"";

                ensureBrushOffset(iconImg);
                if (s_off_brush < 0) { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: s_off_brush not resolved\n")); return L""; }
                uint8_t* imgBase = reinterpret_cast<uint8_t*>(iconImg);
                UObject* brushResource = *reinterpret_cast<UObject**>(imgBase + s_off_brush + brushResourceObj());
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: brushResource={:p} readable={}\n"),
                      (void*)brushResource, brushResource ? isReadableMemory(brushResource, 64) : false);
                if (!brushResource || !isReadableMemory(brushResource, 64)) return L"";

                std::wstring resourceClass = safeClassName(brushResource);
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: brushResource class='{}'\n"), resourceClass);
                if (resourceClass.find(L"Texture2D") != std::wstring::npos)
                {
                    std::wstring result(brushResource->GetName());
                    QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: SUCCESS (direct texture) -> '{}'\n"), result);
                    return result;
                }


                if (resourceClass.find(L"Material") == std::wstring::npos)
                {
                    QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: brushResource is neither Texture2D nor Material, skipping\n"));
                    return L"";
                }
                if (!isReadableMemory(brushResource, 280)) return L"";
                if (s_off_texParamValues == -2) resolveOffset(brushResource, L"TextureParameterValues", s_off_texParamValues);
                if (s_off_texParamValues < 0) { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: s_off_texParamValues not resolved\n")); return L""; }
                uint8_t* midBase = reinterpret_cast<uint8_t*>(brushResource);
                uint8_t* arrData = *reinterpret_cast<uint8_t**>(midBase + s_off_texParamValues);
                int32_t arrNum = *reinterpret_cast<int32_t*>(midBase + s_off_texParamValues + 8);
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: texParamValues arrData={:p} arrNum={}\n"),
                      (void*)arrData, arrNum);
                if (arrNum < 1 || arrNum > 32 || !arrData || !isReadableMemory(arrData, 40))
                { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: arrData invalid or arrNum out of range\n")); return L""; }
                UObject* texPtr = *reinterpret_cast<UObject**>(arrData + texParamValueOff());
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: texPtr={:p} readable={}\n"),
                      (void*)texPtr, texPtr ? isReadableMemory(texPtr, 64) : false);
                if (!texPtr || !isReadableMemory(texPtr, 64)) return L"";
                std::wstring texClass = safeClassName(texPtr);
                if (texClass.find(L"Texture") == std::wstring::npos)
                { QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: texture class ptr invalid\n")); return L""; }
                std::wstring result(texPtr->GetName());
                QBLOG(STR("[MoriaCppMod] [QB] extractIconTextureName: SUCCESS -> '{}'\n"), result);
                return result;
            }
            catch (...)
            {
                return L"";
            }
        }


        UObject* findBuildItemWidget(const std::wstring& recipeName)
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            int mediumCount = 0;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;
                mediumCount++;
                std::wstring name = readWidgetDisplayName(w);
                if (name == recipeName)
                {
                    QBLOG(STR("[MoriaCppMod] [QB] findBuildItemWidget('{}') -> FOUND (checked {} medium widgets)\n"),
                          recipeName, mediumCount);
                    return w;
                }
            }
            QBLOG(STR("[MoriaCppMod] [QB] findBuildItemWidget('{}') -> NOT FOUND ({} medium widgets checked)\n"),
                  recipeName, mediumCount);
            return nullptr;
        }


        void logBLockDiagnostics(const wchar_t* context, const std::wstring& displayName, const uint8_t* bLock)
        {
            if (!bLock)
            {
                VLOG(STR("[MoriaCppMod] [{}] '{}' Ã¢â‚¬â€ no bLock data\n"), context, displayName);
                return;
            }


            uint32_t tagCI = *reinterpret_cast<const uint32_t*>(bLock);
            int32_t tagNum = *reinterpret_cast<const int32_t*>(bLock + 4);


            uint32_t catTagCI = *reinterpret_cast<const uint32_t*>(bLock + 0x20);
            int32_t catTagNum = *reinterpret_cast<const int32_t*>(bLock + 0x24);


            int32_t sortOrder = *reinterpret_cast<const int32_t*>(bLock + 0x60);

            VLOG(STR("[MoriaCppMod] [{}] '{}' Tag CI={} Num={} | CatTag CI={} Num={} | SortOrder={}\n"),
                 context, displayName, tagCI, tagNum, catTagCI, catTagNum, sortOrder);


            const uint8_t* variantsPtr = *reinterpret_cast<const uint8_t* const*>(bLock + rbVariantsOff());
            int32_t variantsCount = *reinterpret_cast<const int32_t*>(bLock + rbVariantsNumOff());

            if (variantsCount > 0 && variantsPtr && isReadableMemory(variantsPtr, variantEntrySize()))
            {
                uint32_t rowCI = *reinterpret_cast<const uint32_t*>(variantsPtr + variantRowCIOff());
                int32_t rowNum = *reinterpret_cast<const int32_t*>(variantsPtr + variantRowNumOff());
                VLOG(STR("[MoriaCppMod] [{}]   Variants={} RowName CI={} Num={}\n"),
                     context, variantsCount, rowCI, rowNum);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [{}]   Variants={} (no readable data)\n"), context, variantsCount);
            }


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
            QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot(F{}) ENTER: hasLastCapture={} lastCapturedName='{}'\n"),
                  slot + 1, m_hasLastCapture, m_lastCapturedName);
            if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return;


            UObject* buildTab = getCachedBuildTab();
            bool hasBuildObject = m_hasLastCapture && !m_lastCapturedName.empty() && buildTab;
            QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: buildTab={} hasBuildObject={}\n"),
                  buildTab ? STR("YES") : STR("NO"), hasBuildObject ? STR("true") : STR("false"));


            if (!hasBuildObject)
            {
                if (m_recipeSlots[slot].used)
                {
                    m_recipeSlots[slot].displayName.clear();
                    m_recipeSlots[slot].textureName.clear();
                    m_recipeSlots[slot].rowName.clear();
                    std::memset(m_recipeSlots[slot].bLockData, 0, BLOCK_DATA_SIZE);
                    m_recipeSlots[slot].hasBLockData = false;
                    std::memset(m_recipeSlots[slot].recipeHandle, 0, RECIPE_HANDLE_SIZE);
                    m_recipeSlots[slot].hasHandle = false;
                    m_recipeSlots[slot].used = false;
                    if (m_activeBuilderSlot == slot) m_activeBuilderSlot = -1;
                    saveQuickBuildSlots();
                    updateOverlaySlots();
                    updateBuildersBar();
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] CLEARED F{}\n"), slot + 1);
                    std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" cleared";
                    showErrorBox(msg);
                }
                return;
            }

            m_recipeSlots[slot].displayName = m_lastCapturedName;
            std::memcpy(m_recipeSlots[slot].bLockData, m_lastCapturedBLock, BLOCK_DATA_SIZE);
            m_recipeSlots[slot].hasBLockData = true;
            if (m_hasLastHandle)
            {
                std::memcpy(m_recipeSlots[slot].recipeHandle, m_lastCapturedHandle, RECIPE_HANDLE_SIZE);
                m_recipeSlots[slot].hasHandle = true;

                uint32_t ci = *reinterpret_cast<uint32_t*>(m_lastCapturedHandle + 8);
                uint32_t num = *reinterpret_cast<uint32_t*>(m_lastCapturedHandle + 12);
                RC::Unreal::FName fn(ci, num);
                m_recipeSlots[slot].rowName = fn.ToString();
                QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: stored handle + rowName='{}' for F{}\n"),
                      m_recipeSlots[slot].rowName, slot + 1);
            }
            m_recipeSlots[slot].used = true;
            QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: slot data set, looking up icon...\n"));


            UObject* dtIcon = nullptr;
            if (!m_recipeSlots[slot].rowName.empty())
            {
                if (!m_dtConstructions.isBound()) m_dtConstructions.bind(L"DT_Constructions");
                if (!m_dtConstructionRecipes.isBound()) m_dtConstructionRecipes.bind(L"DT_ConstructionRecipes");
                dtIcon = lookupRecipeIcon(m_recipeSlots[slot].rowName.c_str());
                if (dtIcon && isReadableMemory(dtIcon, 64))
                {
                    m_recipeSlots[slot].textureName = std::wstring(dtIcon->GetName());
                    QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: DataTable icon='{}'\n"), m_recipeSlots[slot].textureName);
                }
                else
                {
                    dtIcon = nullptr;
                    QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: DataTable icon lookup failed, falling back to widget\n"));
                }
            }


            UObject* itemWidget = nullptr;
            if (!dtIcon)
            {
                itemWidget = findBuildItemWidget(m_lastCapturedName);
                QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: findBuildItemWidget -> {}\n"),
                      itemWidget ? STR("FOUND") : STR("NOT FOUND"));
                if (itemWidget)
                {
                    m_recipeSlots[slot].textureName = extractIconTextureName(itemWidget);
                    QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: textureName='{}'\n"), m_recipeSlots[slot].textureName);
                }
            }


            if (!m_recipeSlots[slot].textureName.empty())
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} icon: '{}'\n"), slot + 1, m_recipeSlots[slot].textureName);

                if (!s_overlay.iconFolder.empty())
                {
                    std::wstring pngPath = s_overlay.iconFolder + L"\\" + m_recipeSlots[slot].textureName + L".png";
                    DWORD attr = GetFileAttributesW(pngPath.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES)
                    {
                        QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: extracting PNG to '{}'\n"), pngPath);
                        bool iconOk = extractAndSaveIcon(itemWidget, m_recipeSlots[slot].textureName, pngPath, dtIcon);
                        QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot: extractAndSaveIcon -> {}\n"),
                              iconOk ? STR("OK") : STR("FAILED"));
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [Icon] PNG already exists: {}\n"), pngPath);
                    }
                }
            }

            saveQuickBuildSlots();
            updateOverlaySlots();
            updateBuildersBar();

            QBLOG(STR("[MoriaCppMod] [QB] ASSIGN F{} = '{}' tex='{}'\n"),
                  slot + 1, m_lastCapturedName, m_recipeSlots[slot].textureName);
            logBLockDiagnostics(L"ASSIGN", m_lastCapturedName, m_recipeSlots[slot].bLockData);

            std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" = " + m_lastCapturedName;
            showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.0f);
            QBLOG(STR("[MoriaCppMod] [QB] assignRecipeSlot(F{}) EXIT OK\n"), slot + 1);
        }


        std::wstring readWidgetDisplayName(UObject* widget)
        {
            if (!widget || !isObjectAlive(widget)) return L"";
            uint8_t* base = reinterpret_cast<uint8_t*>(widget);
            if (s_off_blockName == -2) resolveOffset(widget, L"blockName", s_off_blockName);
            if (s_off_blockName < 0) return L"";
            UObject* blockNameWidget = *reinterpret_cast<UObject**>(base + s_off_blockName);
            if (!blockNameWidget || !isObjectAlive(blockNameWidget)) return L"";

            auto* getTextFunc = blockNameWidget->GetFunctionByNameInChain(STR("GetText"));
            if (!getTextFunc || getTextFunc->GetParmsSize() != sizeof(FText)) return L"";

            FText textResult{};
            safeProcessEvent(blockNameWidget, getTextFunc, &textResult);
            if (!textResult.Data) return L"";
            return textResult.ToString();
        }


        void quickBuildSlot(int slot)
        {
            if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return;
            logGameState(STR("QB-pre"));

            if (!m_recipeSlots[slot].used)
            {
                std::wstring msg = L"F" + std::to_wstring(slot + 1) + L" empty \x2014 select recipe in B menu, then " + modifierName(s_modifierVK) + L"+F" + std::to_wstring(slot + 1);
                showErrorBox(msg);
                return;
            }


            if (m_handleResolvePhase == HandleResolvePhase::Priming
                || m_handleResolvePhase == HandleResolvePhase::Resolving)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} blocked (handle resolution in progress)\n"), slot + 1);
                return;
            }


            if (m_qbPhase != PlacePhase::Idle)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} pressed while phase {} active -- updating pending slot\n"),
                                                        slot + 1, static_cast<int>(m_qbPhase));
                m_pendingQuickBuildSlot = slot;
                return;
            }


            {
                ULONGLONG now = GetTickCount64();
                if (now - m_lastQBSelectTime < 500)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} cooldown ({}ms since last select)\n"),
                          slot + 1, now - m_lastQBSelectTime);
                    return;
                }
            }


            startOrSwitchBuild(slot);
            logGameState(STR("QB-post"));
        }


        void quickBuildFromTarget()
        {

            if (m_handleResolvePhase == HandleResolvePhase::Priming
                || m_handleResolvePhase == HandleResolvePhase::Resolving)
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Blocked (handle resolution in progress)\n"));
                return;
            }


            if (!m_lastTargetBuildable || (m_targetBuildName.empty() && m_targetBuildRecipeRef.empty()))
            {
                showErrorBox(Loc::get("msg.no_buildable_target"));
                return;
            }


            if (m_qbPhase != PlacePhase::Idle)
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Blocked (SM phase {} active)\n"), static_cast<int>(m_qbPhase));
                return;
            }


            {
                ULONGLONG now = GetTickCount64();
                if (now - m_lastQBSelectTime < 500)
                {
                    QBLOG(STR("[MoriaCppMod] [TargetBuild] Cooldown ({}ms since last select)\n"),
                          now - m_lastQBSelectTime);
                    return;
                }
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] ACTIVATE target='{}' recipeRef='{}'\n"),
                  m_targetBuildName, m_targetBuildRecipeRef);


            startBuildFromTarget();
        }

