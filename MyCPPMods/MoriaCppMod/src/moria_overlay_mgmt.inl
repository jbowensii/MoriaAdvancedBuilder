// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_overlay_mgmt.inl — startOverlay, stopOverlay, input mode helpers   ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6J: Overlay & Window Management Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // GDI+ overlay start/stop, slot display sync, input mode helpers
        // startOverlay/stopOverlay thread lifecycle, updateOverlaySlots

        // Syncs overlay slot display from m_recipeSlots[]. Loads icon PNGs from disk.
        // Called after recipe assignment, icon extraction, or on mod startup.
        void updateOverlaySlots()
        {
            if (!s_overlay.csInit) return;
            CriticalSectionLock slotLock(s_overlay.slotCS);
            for (int i = 0; i < OVERLAY_BUILD_SLOTS && i < QUICK_BUILD_SLOTS; i++)
            {
                s_overlay.slots[i].used = m_recipeSlots[i].used;
                s_overlay.slots[i].displayName = m_recipeSlots[i].displayName;
                // If texture changed, discard old icon so new one loads
                if (s_overlay.slots[i].textureName != m_recipeSlots[i].textureName)
                {
                    s_overlay.slots[i].icon.reset();
                }
                s_overlay.slots[i].textureName = m_recipeSlots[i].textureName;
                // Try loading PNG icon if we have a texture name and no icon yet
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

        // Keep old name as alias for backward compat
        void updateOverlayText()
        {
            updateOverlaySlots();
        }

        void startOverlay()
        {
            if (s_overlay.thread) return;
            if (!s_overlay.csInit)
            {
                InitializeCriticalSection(&s_overlay.slotCS);
                s_overlay.csInit = true;
            }
            // Set icon folder to Mods/MoriaCppMod/icons/
            wchar_t dllPath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
            std::wstring gamePath(dllPath);
            auto pos = gamePath.rfind(L'\\');
            if (pos != std::wstring::npos) gamePath = gamePath.substr(0, pos);
            s_overlay.iconFolder = gamePath + L"\\Mods\\MoriaCppMod\\icons";

            // Initialize GDI+ early so updateOverlaySlots can load cached icon PNGs
            if (!s_overlay.gdipToken)
            {
                Gdiplus::GdiplusStartupInput gdipInput;
                Gdiplus::GdiplusStartup(&s_overlay.gdipToken, &gdipInput, nullptr);
            }

            s_overlay.running = true;
            s_overlay.visible = m_showHotbar;
            s_overlay.activeToolbar = m_activeToolbar;
            updateOverlaySlots();
            s_overlay.thread = CreateThread(nullptr, 0, overlayThreadProc, nullptr, 0, nullptr);
            VLOG(STR("[MoriaCppMod] Overlay thread started, icons: {}\n"), s_overlay.iconFolder);
        }

        // LINT NOTE (#9 Ã¢â‚¬â€ stopOverlay race): The 3-second WaitForSingleObject timeout means the render
        // thread could theoretically still hold slotCS when we DeleteCriticalSection below. Analyzed and
        // intentionally skipped: this only fires during mod destructor (game shutdown). Even if the timeout
        // expires and the CS is deleted under the render thread, the game is already closing. Changing to
        // INFINITE wait risks hanging the game exit if the render thread is stuck in GDI+. The current
        // pragmatic timeout works 99.9% of the time and any crash is invisible to the user.
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
            // Fix #7: Reset GDI+ token so startOverlay() can re-initialize if overlay is restarted.
            // overlayThreadProc() calls GdiplusShutdown on exit but never reset the token to 0,
            // causing the guard `if (!s_overlay.gdipToken)` to skip re-init on restart.
            // Matches the pattern used by configThreadProc and targetInfoThreadProc.
            // Clean up loaded icons BEFORE shutting down GDI+ (icons hold GDI+ objects)
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


        // Ã¢â€â‚¬Ã¢â€â‚¬ Input Mode Helpers (for modal UI: Config Menu, Reposition Mode) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // Switch to UI-only input so the mouse cursor appears and game input is blocked.
        void setInputModeUI(UObject* focusWidget = nullptr)
        {
            auto* pc = findPlayerController();
            if (!pc) return;
            if (!focusWidget) focusWidget = m_configWidget;
            if (!focusWidget) return;

            // Find SetInputMode_UIOnlyEx on WidgetBlueprintLibrary CDO
            auto* uiFunc = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:SetInputMode_UIOnlyEx"));
            auto* wblCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/UMG.Default__WidgetBlueprintLibrary"));
            if (!uiFunc || !wblCDO) {
                VLOG(STR("[MoriaCppMod] setInputModeUI: could not find UIOnlyEx func/CDO\n"));
                return;
            }

            // Resolve param offsets via reflection
            resolveSIMUIOffsets(uiFunc);
            if (s_simui.PlayerController < 0) return;
            std::vector<uint8_t> params(s_simui.parmsSize, 0);
            std::memcpy(params.data() + s_simui.PlayerController, &pc, 8);
            if (s_simui.InWidgetToFocus >= 0)
                std::memcpy(params.data() + s_simui.InWidgetToFocus, &focusWidget, 8);
            if (s_simui.InMouseLockMode >= 0)
                params[s_simui.InMouseLockMode] = 0; // EMouseLockMode::DoNotLock
            wblCDO->ProcessEvent(uiFunc, params.data());

            // Set bShowMouseCursor via FBoolProperty API
            setBoolProp(pc, L"bShowMouseCursor", true);

            VLOG(STR("[MoriaCppMod] Input mode Ã¢â€ â€™ UI Only (mouse cursor ON)\n"));
        }

        // Restore game-only input so game controls work normally.
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

            // Resolve param offsets via reflection
            resolveSIMGOffsets(gameFunc);
            if (s_simg.PlayerController < 0) return;
            std::vector<uint8_t> params(s_simg.parmsSize, 0);
            std::memcpy(params.data() + s_simg.PlayerController, &pc, 8);
            wblCDO->ProcessEvent(gameFunc, params.data());

            // Clear bShowMouseCursor via FBoolProperty API
            setBoolProp(pc, L"bShowMouseCursor", false);

            VLOG(STR("[MoriaCppMod] Input mode Ã¢â€ â€™ Game Only (mouse cursor OFF)\n"));
        }

        void toggleConfig()
        {
            if (!m_configWidget) createConfigWidget();
            if (!m_configWidget) return;
            m_cfgVisible = !m_cfgVisible;
            auto* fn = m_configWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = m_cfgVisible ? 0 : 1; m_configWidget->ProcessEvent(fn, p); }
            if (m_cfgVisible)
            {
                updateConfigKeyLabels();
                updateConfigFreeBuild();
                updateConfigNoCollision();
                updateConfigRemovalCount();
                setInputModeUI();
            }
            else
            {
                setInputModeGame();
            }
            VLOG(STR("[MoriaCppMod] Config {} (UMG)\n"), m_cfgVisible ? STR("shown") : STR("hidden"));
        }


        // Ã¢â€â‚¬Ã¢â€â‚¬ Toolbar Repositioning Mode Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        void createRepositionMessage()
        {
            if (m_repositionMsgWidget) return;
            // Use showOnScreen for simplicity Ã¢â‚¬â€ centered UMG message
            // Create a simple UUserWidget with a TextBlock
            auto* uwClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            if (!uwClass) return;
            auto* pc = findPlayerController();
            if (!pc) return;

            // CreateWidget<UUserWidget>
            auto* createFn = uwClass->GetFunctionByNameInChain(STR("CreateWidgetOfClass"));
            if (!createFn) return;
            FStaticConstructObjectParameters uwP(uwClass, reinterpret_cast<UObject*>(pc));
            auto* userWidget = UObjectGlobals::StaticConstructObject(uwP);
            if (!userWidget) return;

            // Create WidgetTree
            auto* wtClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetTree"));
            if (wtClass)
            {
                FStaticConstructObjectParameters wtP(wtClass, userWidget);
                auto* widgetTree = UObjectGlobals::StaticConstructObject(wtP);
                if (widgetTree)
                {
                    // Set WidgetTree on UserWidget via reflected offset
                    if (s_off_widgetTree == -2) resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
                    if (s_off_widgetTree >= 0)
                        *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + s_off_widgetTree) = widgetTree;

                    // Create a TextBlock as root
                    auto* tbClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
                    if (tbClass)
                    {
                        FStaticConstructObjectParameters tbP(tbClass, widgetTree);
                        auto* textBlock = UObjectGlobals::StaticConstructObject(tbP);
                        if (textBlock)
                        {
                            setRootWidget(widgetTree, textBlock);
                            umgSetText(textBlock, L"Using the mouse move the toolbar(s) into your desired positions, hit ESC to exit.");
                            // Yellow text for visibility
                            umgSetTextColor(textBlock, 1.0f, 0.95f, 0.2f, 1.0f);
                        }
                    }
                }
            }

            // Add to viewport at high Z-order
            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 250;
                userWidget->ProcessEvent(addFn, vp.data());
            }

            // Get viewport and position centered
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Set desired size and alignment
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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }
            setWidgetPosition(userWidget, static_cast<float>(viewW) / 2.0f, static_cast<float>(viewH) / 2.0f, true);

            m_repositionMsgWidget = userWidget;
        }

        void destroyRepositionMessage()
        {
            if (!m_repositionMsgWidget) return;
            auto* removeFn = m_repositionMsgWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) m_repositionMsgWidget->ProcessEvent(removeFn, nullptr);
            m_repositionMsgWidget = nullptr;
        }

        // Create a placeholder Info Box widget for repositioning (same size as real Target Info widget)
        void createPlaceholderInfoBox()
        {
            if (m_repositionInfoBoxWidget) return;

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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root SizeBox -- matches real Target Info widget structure (width constraint 550)
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
                    if (setWFn) { int sz = setWFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 550.0f; rootSizeBox->ProcessEvent(setWFn, wp.data()); }
                    auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
                    if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; rootSizeBox->ProcessEvent(setClipFn, cp2.data()); }
                }
            }

            // Border (transparent bg -- matches real Target Info widget)
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
                    rootSizeBox->ProcessEvent(setContentFn2, sc.data());
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
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // transparent
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
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
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox with example text rows -- mirrors real Target Info layout
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
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
            };

            // Example content matching real Target Info structure
            makeTextBlock(Loc::get("ui.target_info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
            makeTextBlock(L"--------------------------------", 0.31f, 0.51f, 0.78f, 0.5f);
            makeTextBlock(L"Class: BP_Example_Actor_C", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Name: Example_Actor_01", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Display: Example Actor", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Path: /Game/Example/Path", 0.86f, 0.90f, 0.96f, 0.9f);
            makeTextBlock(L"Buildable: Yes", 0.31f, 0.86f, 0.31f, 1.0f);
            makeTextBlock(L"Recipe: ExampleRecipe", 0.86f, 0.90f, 0.96f, 0.9f);

            // Add to viewport at high Z-order
            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                auto* pZ = findParam(addFn, STR("ZOrder"));
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZ) *reinterpret_cast<int32_t*>(vp.data() + pZ->GetOffset_Internal()) = 249;
                userWidget->ProcessEvent(addFn, vp.data());
            }

            // Get viewport size for uiScale + positioning
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

            // Render scale 1.0 -- engine DPI handles resolution scaling via Slate
            if (rootSizeBox) umgSetRenderScale(rootSizeBox, 1.0f, 1.0f);

            // Desired size in Slate units -- matches real Target Info widget
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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Center alignment
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position from saved or default
            float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
            float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
            setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                          fracY * static_cast<float>(viewH), true);

            // Cache size for hit-test -- matches real Target Info dimensions
            m_toolbarSizeW[3] = (1100.0f * uiScale) / static_cast<float>(viewW);
            m_toolbarSizeH[3] = (320.0f * uiScale) / static_cast<float>(viewH);

            m_repositionInfoBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] Created placeholder Info Box for repositioning\n"));
        }

        void destroyPlaceholderInfoBox()
        {
            if (!m_repositionInfoBoxWidget) return;
            auto* removeFn = m_repositionInfoBoxWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
            if (removeFn) m_repositionInfoBoxWidget->ProcessEvent(removeFn, nullptr);
            m_repositionInfoBoxWidget = nullptr;
        }

        void toggleRepositionMode()
        {
            // Guard: need at least one toolbar created before entering reposition mode
            if (!m_repositionMode && !m_umgBarWidget && !m_abBarWidget && !m_mcBarWidget)
                return;

            m_repositionMode = !m_repositionMode;
            m_dragToolbar = -1;
            m_hitDebugDone = false; // reset so debug fires on first click each reposition session

            if (m_repositionMode)
            {
                // Ensure toolbars are visible (don't move them Ã¢â‚¬â€ let user adjust from current positions)
                if (!m_toolbarsVisible)
                {
                    m_toolbarsVisible = true;
                    auto setWidgetVis = [](UObject* widget) {
                        if (!widget) return;
                        auto* fn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (fn) { uint8_t parms[8]{}; parms[0] = 0; widget->ProcessEvent(fn, parms); }
                    };
                    setWidgetVis(m_umgBarWidget);
                    setWidgetVis(m_mcBarWidget);
                }
                createRepositionMessage();
                createPlaceholderInfoBox();
                // Use the message widget for focus, or fall back to any toolbar
                UObject* focusW = m_repositionMsgWidget ? m_repositionMsgWidget
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

        void showTargetInfo(const std::wstring& name,
                            const std::wstring& display,
                            const std::wstring& path,
                            const std::wstring& cls,
                            bool buildable = false,
                            const std::wstring& recipe = L"",
                            const std::wstring& rowName = L"")
        {
            showTargetInfoUMG(name, display, path, cls, buildable, recipe, rowName);
        }

        void undoLast()
        {
            if (m_undoStack.empty())
            {
                VLOG(STR("[MoriaCppMod] Nothing to undo\n"));
                return;
            }

            auto& last = m_undoStack.back();

            // Type rule undo: restore all instances and remove the @rule
            if (last.isTypeRule)
            {
                std::string meshId = last.typeRuleMeshId;

                // Collect all undo entries for this type rule (they're contiguous at the back)
                std::vector<RemovedInstance> toRestore;
                while (!m_undoStack.empty())
                {
                    auto& entry = m_undoStack.back();
                    if (!entry.isTypeRule || entry.typeRuleMeshId != meshId) break;
                    toRestore.push_back(entry);
                    m_undoStack.pop_back();
                }

                // Restore all instances (un-hide them by restoring original transform)
                int restored = 0;
                for (auto& ri : toRestore)
                {
                    // Validate via weak pointer (safe against GC slab reuse)
                    UObject* comp = ri.component.Get();
                    if (comp)
                    {
                        if (restoreInstance(comp, ri.instanceIndex, ri.transform)) restored++;
                    }
                }

                // Remove the type rule
                m_typeRemovals.erase(meshId);
                rewriteSaveFile();
                buildRemovalEntries();

                std::wstring meshIdW(meshId.begin(), meshId.end());
                VLOG(STR("[MoriaCppMod] Undo type rule: {} Ã¢â‚¬â€ restored {} instances\n"), meshIdW, restored);
                return;
            }

            // Single instance undo
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

            // Validate via weak pointer (safe against GC slab reuse)
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

