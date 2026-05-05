


        void updateOverlaySlots()
        {
            if (!s_overlay.csInit) return;
            CriticalSectionLock slotLock(s_overlay.slotCS);
            for (int i = 0; i < OVERLAY_BUILD_SLOTS && i < QUICK_BUILD_SLOTS; i++)
            {
                s_overlay.slots[i].used = m_recipeSlots[i].used;
                s_overlay.slots[i].displayName = m_recipeSlots[i].displayName;

                if (s_overlay.slots[i].textureName != m_recipeSlots[i].textureName)
                {
                    s_overlay.slots[i].icon.reset();
                }
                s_overlay.slots[i].textureName = m_recipeSlots[i].textureName;

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
            s_overlay.needsUpdate = true;
        }

        void startOverlay()
        {
            if (s_overlay.thread) return;
            if (!s_overlay.csInit)
            {
                InitializeCriticalSection(&s_overlay.slotCS);
                s_overlay.csInit = true;
            }

            wchar_t dllPath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
            std::wstring gamePath(dllPath);
            auto pos = gamePath.rfind(L'\\');
            if (pos != std::wstring::npos) gamePath = gamePath.substr(0, pos);
            s_overlay.iconFolder = gamePath + L"\\Mods\\MoriaCppMod\\icons";


            if (!s_overlay.gdipToken)
            {
                Gdiplus::GdiplusStartupInput gdipInput;
                Gdiplus::GdiplusStartup(&s_overlay.gdipToken, &gdipInput, nullptr);
            }

            s_overlay.running = true;
            s_overlay.visible = m_showHotbar;
            updateOverlaySlots();
            s_overlay.thread = CreateThread(nullptr, 0, overlayThreadProc, nullptr, 0, nullptr);
            VLOG(STR("[MoriaCppMod] Overlay thread started, icons: {}\n"), s_overlay.iconFolder);
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

            for (int i = 0; i < OVERLAY_SLOTS; i++)
            {
                s_overlay.slots[i].icon.reset();
            }
            if (s_overlay.gdipToken)
            {
                Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
                s_overlay.gdipToken = 0;
            }
            if (s_overlay.csInit)
            {
                DeleteCriticalSection(&s_overlay.slotCS);
                s_overlay.csInit = false;
            }
        }


        void setInputModeUI(UObject* focusWidget = nullptr)
        {
            auto* pc = findPlayerController();
            if (!pc) return;
            if (!focusWidget) focusWidget = m_fontTestWidget;
            if (!focusWidget) return;


            auto* uiFunc = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:SetInputMode_UIOnlyEx"));
            auto* wblCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary"));
            if (!uiFunc || !wblCDO) {
                VLOG(STR("[MoriaCppMod] setInputModeUI: could not find UIOnlyEx func/CDO\n"));
                return;
            }


            resolveSIMUIOffsets(uiFunc);
            if (s_simui.PlayerController < 0) return;
            std::vector<uint8_t> params(s_simui.parmsSize, 0);
            std::memcpy(params.data() + s_simui.PlayerController, &pc, 8);
            if (s_simui.InWidgetToFocus >= 0)
                std::memcpy(params.data() + s_simui.InWidgetToFocus, &focusWidget, 8);
            if (s_simui.InMouseLockMode >= 0)
                params[s_simui.InMouseLockMode] = 0;
            safeProcessEvent(wblCDO, uiFunc, params.data());


            setBoolProp(pc, L"bShowMouseCursor", true);

            VLOG(STR("[MoriaCppMod] Input mode Ã¢â€ â€™ UI Only (mouse cursor ON)\n"));
        }


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


            resolveSIMGOffsets(gameFunc);
            if (s_simg.PlayerController < 0) return;
            std::vector<uint8_t> params(s_simg.parmsSize, 0);
            std::memcpy(params.data() + s_simg.PlayerController, &pc, 8);
            safeProcessEvent(wblCDO, gameFunc, params.data());


            setBoolProp(pc, L"bShowMouseCursor", false);

            VLOG(STR("[MoriaCppMod] Input mode Ã¢â€ â€™ Game Only (mouse cursor OFF)\n"));
        }


        // v6.21.8 batch 3 Tier 2 - removed reposition mode entirely:
        // createRepositionMessage / destroyRepositionMessage /
        // createPlaceholderInfoBox / destroyPlaceholderInfoBox /
        // toggleRepositionMode (335 lines). Only entry point was the
        // disabled NUM+/AB toggle block (also removed). Inspect window
        // and rotation display have their own independent drag systems.

        void undoLast()
        {
            if (m_undoStack.empty())
            {
                VLOG(STR("[MoriaCppMod] Nothing to undo\n"));
                return;
            }

            auto& last = m_undoStack.back();


            if (last.isTypeRule)
            {
                std::string meshId = last.typeRuleMeshId;


                std::vector<RemovedInstance> toRestore;
                while (!m_undoStack.empty())
                {
                    auto& entry = m_undoStack.back();
                    if (!entry.isTypeRule || entry.typeRuleMeshId != meshId) break;
                    toRestore.push_back(entry);
                    m_undoStack.pop_back();
                }


                int restored = 0;
                for (auto& ri : toRestore)
                {

                    UObject* comp = ri.component.Get();
                    if (comp)
                    {
                        if (restoreInstance(comp, ri.instanceIndex, ri.transform)) restored++;
                    }
                }


                m_typeRemovals.erase(meshId);
                rewriteSaveFile();
                buildRemovalEntries();

                std::wstring meshIdW(meshId.begin(), meshId.end());
                VLOG(STR("[MoriaCppMod] Undo type rule: {} Ã¢â‚¬â€ restored {} instances\n"), meshIdW, restored);
                return;
            }


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


            bool ok = false;
            UObject* comp = last.component.Get();
            if (comp)
            {
                ok = restoreInstance(comp, last.instanceIndex, last.transform);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] Undo: component pointer stale (GC'd), skipping restore\n"));
            }
            VLOG(STR("[MoriaCppMod] Restored index {} ({}) | {} remaining\n"),
                                            last.instanceIndex,
                                            ok ? STR("ok") : STR("FAILED"),
                                            m_savedRemovals.size());

            m_undoStack.pop_back();
        }

