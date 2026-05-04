


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


        void createRepositionMessage()
        {
            if (m_repositionMsgWidget.Get()) return; // v6.17.0 — was raw pointer truthy-check


            auto* uwClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            if (!uwClass) return;
            auto* pc = findPlayerController();
            if (!pc) return;


            auto* createFn = uwClass->GetFunctionByNameInChain(STR("CreateWidgetOfClass"));
            if (!createFn) return;
            FStaticConstructObjectParameters uwP(uwClass, reinterpret_cast<UObject*>(pc));
            auto* userWidget = UObjectGlobals::StaticConstructObject(uwP);
            if (!userWidget) return;


            auto* wtClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetTree"));
            if (wtClass)
            {
                FStaticConstructObjectParameters wtP(wtClass, userWidget);
                auto* widgetTree = UObjectGlobals::StaticConstructObject(wtP);
                if (widgetTree)
                {

                    auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                    if (wtSlot) *wtSlot = widgetTree;


                    auto* tbClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
                    if (tbClass)
                    {
                        FStaticConstructObjectParameters tbP(tbClass, widgetTree);
                        auto* textBlock = UObjectGlobals::StaticConstructObject(tbP);
                        if (textBlock)
                        {
                            setRootWidget(widgetTree, textBlock);
                            umgSetText(textBlock, Loc::get("msg.toolbar_reposition_help"));

                            umgSetTextColor(textBlock, 1.0f, 0.95f, 0.2f, 1.0f);
                        }
                    }
                }
            }


            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 250;
                safeProcessEvent(userWidget, addFn, vp.data());
            }


            m_screen.refresh(findPlayerController());
            float uiScale = m_screen.uiScale;


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
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
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
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }
            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f), m_screen.fracToPixelY(0.5f), true);

            m_repositionMsgWidget = FWeakObjectPtr(userWidget); // v6.17.0 weakptr
        }

        void destroyRepositionMessage()
        {
            // v6.17.0 — FWeakObjectPtr handles the alive-check implicitly:
            // .Get() returns nullptr automatically if the widget was GC'd.
            UObject* w = m_repositionMsgWidget.Get();
            if (!w) return;
            auto* removeFn = w->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) safeProcessEvent(w, removeFn, nullptr);
            m_repositionMsgWidget = FWeakObjectPtr();
        }


        void createPlaceholderInfoBox()
        {
            if (m_repositionInfoBoxWidget.Get()) return; // v6.17.0 weakptr

            auto* uwClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            if (!uwClass || !borderClass || !textBlockClass || !vboxClass) return;
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
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            UObject* rootSizeBox = nullptr;
            if (sizeBoxClass)
            {
                FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
                rootSizeBox = UObjectGlobals::StaticConstructObject(sbP);
                if (rootSizeBox)
                {
                    if (widgetTree)
                        setRootWidget(widgetTree, rootSizeBox);
                    auto* setWFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                    if (setWFn) { int sz = setWFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 550.0f; safeProcessEvent(rootSizeBox, setWFn, wp.data()); }
                    auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
                    if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; safeProcessEvent(rootSizeBox, setClipFn, cp2.data()); }
                }
            }


            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (rootSizeBox)
            {
                auto* setContentFn2 = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                if (setContentFn2)
                {
                    auto* pC = findParam(setContentFn2, STR("Content"));
                    int sz = setContentFn2->GetParmsSize();
                    std::vector<uint8_t> sc(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootBorder;
                    safeProcessEvent(rootSizeBox, setContentFn2, sc.data());
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
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(rootBorder, setBrushColorFn, cb.data());
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
                    safeProcessEvent(rootBorder, setBorderPadFn, pp.data());
                }
            }


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
                safeProcessEvent(rootBorder, setContentFn, sc.data());
            }

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                addToVBox(vbox, tb);
            };


            makeTextBlock(Loc::get("ui.target_info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
            makeTextBlock(L"--------------------------------", 0.31f, 0.51f, 0.78f, 0.5f);
            makeTextBlock(L"Class: BP_Example_Actor_C", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Name: Example_Actor_01", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Display: Example Actor", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Path: /Game/Example/Path", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Buildable: Yes", 0.31f, 0.86f, 0.31f, 1.0f);
            makeTextBlock(L"Recipe: ExampleRecipe", 0.86f, 0.90f, 0.96f, 0.9f);


            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 249;
                safeProcessEvent(userWidget, addFn, vp.data());
            }


            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
            float uiScale = m_screen.uiScale;


            if (rootSizeBox) umgSetRenderScale(rootSizeBox, 1.0f, 1.0f);


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1100.0f; v[1] = 320.0f;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
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
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
            float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
            setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                          m_screen.fracToPixelY(fracY), true);


            m_toolbarSizeW[3] = m_screen.slateToFracX(1100.0f * uiScale);
            m_toolbarSizeH[3] = m_screen.slateToFracY(320.0f * uiScale);

            m_repositionInfoBoxWidget = FWeakObjectPtr(userWidget); // v6.17.0 weakptr
            VLOG(STR("[MoriaCppMod] Created placeholder Info Box for repositioning\n"));
        }

        void destroyPlaceholderInfoBox()
        {
            // v6.17.0 — FWeakObjectPtr handles the alive-check implicitly:
            // .Get() returns nullptr if the widget was GC'd.
            UObject* w = m_repositionInfoBoxWidget.Get();
            if (!w) return;
            auto* removeFn = w->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) safeProcessEvent(w, removeFn, nullptr);
            m_repositionInfoBoxWidget = FWeakObjectPtr();
        }

        void toggleRepositionMode()
        {

            if (!m_repositionMode && !m_umgBarWidget && !m_abBarWidget && !m_mcBarWidget)
                return;

            m_repositionMode = !m_repositionMode;
            m_dragToolbar = -1;

            if (m_repositionMode)
            {

                if (!m_toolbarsVisible)
                {
                    m_toolbarsVisible = true;
                    setWidgetVisibility(m_umgBarWidget, 0);
                    setWidgetVisibility(m_mcBarWidget, 0);
                }
                createRepositionMessage();
                createPlaceholderInfoBox();

                UObject* focusW = m_repositionMsgWidget.Get() ? m_repositionMsgWidget.Get() // v6.17.0 weakptr
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

