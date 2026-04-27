// moria_join_world_ui.inl — v6.6.0+
// Mod-owned C++ duplicate of WBP_UI_JoinWorldScreen.
//
// Interception strategy: post-hook OnAfterShow on WBP_UI_JoinWorldScreen_C.
// When the native widget shows, we (a) RemoveFromParent it (b) construct our
// duplicate (c) AddToViewport at higher ZOrder. Reactive replacement covers
// every entry path (button click + friend invite + deeplink) without needing
// to enumerate them.
//
// THIS IS THE ITERATION 1 SHELL — placeholder duplicate is a centered dark
// panel with title text + close button. Validates interception works.
// Iteration 2+ will fill in the full layout from the harvested JSON tree
// using game-styled child widgets (WBP_FrontEndButton_C, UI_WBP_Craft_BigButton_C, etc.).

        // ---------- State ----------
        FWeakObjectPtr m_modJoinWorldWidget;       // our duplicate
        FWeakObjectPtr m_nativeJoinWorldWidget;    // native we suppressed (kept ref to restore on close)
        bool m_pendingShowJoinWorldUI{false};
        bool m_pendingHideJoinWorldUI{false};
        ULONGLONG m_modJoinWorldShownAt{0};

        // ---------- Show / hide ----------
        // Called by post-hook on the game thread. Defers actual UMG work to
        // the next main tick to avoid re-entering the WidgetTree mid-PE.
        void onNativeJoinWorldShown(UObject* nativeWidget)
        {
            if (!nativeWidget || !isObjectAlive(nativeWidget)) return;

            // Skip if our duplicate is already visible
            UObject* mod = m_modJoinWorldWidget.Get();
            if (mod && isObjectAlive(mod))
            {
                VLOG(STR("[JoinWorldUI] post-hook fired but mod widget already visible, skipping\n"));
                return;
            }

            VLOG(STR("[JoinWorldUI] native WBP_UI_JoinWorldScreen_C OnAfterShow at {:p}, queuing replacement\n"),
                 (void*)nativeWidget);

            m_nativeJoinWorldWidget = FWeakObjectPtr(nativeWidget);
            m_pendingShowJoinWorldUI = true;
        }

        void hideModJoinWorldUI()
        {
            UObject* mod = m_modJoinWorldWidget.Get();
            if (!mod || !isObjectAlive(mod))
            {
                m_modJoinWorldWidget = FWeakObjectPtr();
                return;
            }

            VLOG(STR("[JoinWorldUI] hideModJoinWorldUI()\n"));

            // Remove from parent
            try
            {
                auto* removeFn = mod->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn)
                {
                    int sz = removeFn->GetParmsSize();
                    std::vector<uint8_t> p(sz, 0);
                    safeProcessEvent(mod, removeFn, p.data());
                }
            }
            catch (...) {}

            m_modJoinWorldWidget = FWeakObjectPtr();
            // Restore input mode so user can navigate Choose World screen with controller/mouse
            setInputModeGame();
        }

        // ---------- Tick handler ----------
        // Called from on_update / main tick. Consumes pending flags, never re-enters.
        void tickJoinWorldUI()
        {
            if (m_pendingShowJoinWorldUI)
            {
                m_pendingShowJoinWorldUI = false;

                // Suppress the native widget — RemoveFromParent
                UObject* nativ = m_nativeJoinWorldWidget.Get();
                if (nativ && isObjectAlive(nativ))
                {
                    try
                    {
                        auto* removeFn = nativ->GetFunctionByNameInChain(STR("RemoveFromParent"));
                        if (removeFn)
                        {
                            int sz = removeFn->GetParmsSize();
                            std::vector<uint8_t> p(sz, 0);
                            safeProcessEvent(nativ, removeFn, p.data());
                            VLOG(STR("[JoinWorldUI] removed native widget from viewport\n"));
                        }
                    }
                    catch (...) {}
                }

                buildModJoinWorldPlaceholder();
                m_modJoinWorldShownAt = GetTickCount64();
            }

            if (m_pendingHideJoinWorldUI)
            {
                m_pendingHideJoinWorldUI = false;
                hideModJoinWorldUI();
            }
        }

        // ---------- Placeholder builder (Iteration 1) ----------
        // Center-screen dark panel with title text + close button.
        // Proves interception + UI ownership without committing to full layout yet.
        void buildModJoinWorldPlaceholder()
        {
            VLOG(STR("[JoinWorldUI] buildModJoinWorldPlaceholder()\n"));

            auto* pc = findPlayerController();
            if (!pc) { VLOG(STR("[JoinWorldUI] no PC, abort\n")); return; }

            // Find UserWidget class (use a generic Blueprint widget — we'll inherit
            // from the engine UserWidget directly for the placeholder).
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            if (!userWidgetClass) { VLOG(STR("[JoinWorldUI] UserWidget class missing\n")); return; }

            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { VLOG(STR("[JoinWorldUI] WBL missing\n")); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { VLOG(STR("[JoinWorldUI] CreateWidget returned null\n")); return; }

            // Get WidgetTree
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Find UClass refs we'll need
            auto* canvasClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CanvasPanel"));
            auto* borderClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* vboxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* buttonClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            if (!canvasClass || !borderClass || !sizeBoxClass || !vboxClass || !textClass || !buttonClass)
            {
                VLOG(STR("[JoinWorldUI] one or more UMG classes missing — abort\n"));
                return;
            }

            // Root: CanvasPanel
            FStaticConstructObjectParameters rootP(canvasClass, outer);
            UObject* root = UObjectGlobals::StaticConstructObject(rootP);
            if (!root) { VLOG(STR("[JoinWorldUI] root canvas null\n")); return; }
            if (widgetTree) setRootWidget(widgetTree, root);

            // Outer Border (translucent dark panel matching game's visual style)
            FStaticConstructObjectParameters bgP(borderClass, outer);
            UObject* bgBorder = UObjectGlobals::StaticConstructObject(bgP);
            if (!bgBorder) return;
            // Set brush color to dark translucent (~0.05 0.05 0.05 alpha 0.85)
            {
                auto* fn = bgBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InBrushColor"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* c = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        c[0] = 0.05f; c[1] = 0.05f; c[2] = 0.05f; c[3] = 0.85f;
                        safeProcessEvent(bgBorder, fn, b.data());
                    }
                }
            }

            // SizeBox to constrain the panel size
            FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
            UObject* sb = UObjectGlobals::StaticConstructObject(sbP);
            if (!sb) return;
            // SetWidthOverride / SetHeightOverride
            auto setSizeOverride = [&](const wchar_t* fnName, float v)
            {
                auto* fn = sb->GetFunctionByNameInChain(fnName);
                if (!fn) return;
                auto* p = findParam(fn, STR("InSize"));
                if (!p) p = findParam(fn, STR("InWidthOverride"));
                if (!p) p = findParam(fn, STR("InHeightOverride"));
                if (!p) return;
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = v;
                safeProcessEvent(sb, fn, b.data());
            };
            setSizeOverride(STR("SetWidthOverride"), 720.0f);
            setSizeOverride(STR("SetHeightOverride"), 220.0f);

            // VerticalBox for stacking title + close button
            FStaticConstructObjectParameters vbP(vboxClass, outer);
            UObject* vb = UObjectGlobals::StaticConstructObject(vbP);
            if (!vb) return;

            // Title text
            FStaticConstructObjectParameters titleP(textClass, outer);
            UObject* title = UObjectGlobals::StaticConstructObject(titleP);
            if (!title) return;
            umgSetText(title, L"MOD-OWNED JOIN OTHER WORLD (placeholder)");
            umgSetTextColor(title, 1.0f, 0.85f, 0.4f, 1.0f);
            umgSetBold(title);

            // Subtitle
            FStaticConstructObjectParameters subP(textClass, outer);
            UObject* sub = UObjectGlobals::StaticConstructObject(subP);
            if (!sub) return;
            umgSetText(sub, L"Native widget intercepted. Iteration 1 shell — full layout coming next.");
            umgSetTextColor(sub, 0.8f, 0.8f, 0.8f, 1.0f);

            // Close button
            FStaticConstructObjectParameters btnP(buttonClass, outer);
            UObject* closeBtn = UObjectGlobals::StaticConstructObject(btnP);
            if (!closeBtn) return;
            // Bind OnClicked → set m_pendingHideJoinWorldUI flag
            // UMG dynamic delegate binding from C++ is heavy; use ProcessEvent on
            // SetClickMethod or just check button state in tick. For placeholder,
            // we'll add a label child and rely on Esc/back to close.
            FStaticConstructObjectParameters btnTxtP(textClass, outer);
            UObject* btnTxt = UObjectGlobals::StaticConstructObject(btnTxtP);
            if (btnTxt)
            {
                umgSetText(btnTxt, L"  Close (use Esc for now)  ");
                umgSetTextColor(btnTxt, 1.0f, 1.0f, 1.0f, 1.0f);
                // Add btnTxt as the Button's content
                auto* setContentFn = closeBtn->GetFunctionByNameInChain(STR("SetContent"));
                if (setContentFn)
                {
                    auto* p = findParam(setContentFn, STR("Content"));
                    if (p)
                    {
                        std::vector<uint8_t> b(setContentFn->GetParmsSize(), 0);
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = btnTxt;
                        safeProcessEvent(closeBtn, setContentFn, b.data());
                    }
                }
            }

            // Assemble: vb -> title, sub, closeBtn ; sb -> vb ; bgBorder -> sb ; root -> bgBorder
            addToVBox(vb, title);
            addToVBox(vb, sub);
            addToVBox(vb, closeBtn);
            // SizeBox.Content = vb
            {
                auto* fn = sb->GetFunctionByNameInChain(STR("SetContent"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("Content"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = vb;
                        safeProcessEvent(sb, fn, b.data());
                    }
                }
            }
            // Border.Content = sb
            {
                auto* fn = bgBorder->GetFunctionByNameInChain(STR("SetContent"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("Content"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = sb;
                        safeProcessEvent(bgBorder, fn, b.data());
                    }
                }
            }
            // CanvasPanel: add bgBorder centered. Use AddChild on canvas to get a CanvasPanelSlot back,
            // then set anchors/alignment for centering.
            UObject* canvasSlot = nullptr;
            {
                auto* fn = root->GetFunctionByNameInChain(STR("AddChildToCanvas"));
                if (fn)
                {
                    auto* pContent = findParam(fn, STR("Content"));
                    auto* pRet = findParam(fn, STR("ReturnValue"));
                    if (pContent && pRet)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        *reinterpret_cast<UObject**>(b.data() + pContent->GetOffset_Internal()) = bgBorder;
                        safeProcessEvent(root, fn, b.data());
                        canvasSlot = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
                    }
                }
            }
            // Center the slot via SetAnchors / SetAlignment / SetSize / SetAutoSize
            if (canvasSlot && isObjectAlive(canvasSlot))
            {
                // SetAnchors → 0.5,0.5,0.5,0.5
                auto* fnAnchors = canvasSlot->GetFunctionByNameInChain(STR("SetAnchors"));
                if (fnAnchors)
                {
                    auto* p = findParam(fnAnchors, STR("InAnchors"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fnAnchors->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = 0.5f; a[1] = 0.5f; a[2] = 0.5f; a[3] = 0.5f;
                        safeProcessEvent(canvasSlot, fnAnchors, b.data());
                    }
                }
                // SetAlignment → 0.5,0.5
                auto* fnAlign = canvasSlot->GetFunctionByNameInChain(STR("SetAlignment"));
                if (fnAlign)
                {
                    auto* p = findParam(fnAlign, STR("InAlignment"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fnAlign->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = 0.5f; a[1] = 0.5f;
                        safeProcessEvent(canvasSlot, fnAlign, b.data());
                    }
                }
                // SetAutoSize → true (so the SizeBox sizes the slot)
                auto* fnAuto = canvasSlot->GetFunctionByNameInChain(STR("SetAutoSize"));
                if (fnAuto)
                {
                    auto* p = findParam(fnAuto, STR("InbAutoSize"));
                    if (!p) p = findParam(fnAuto, STR("InAutoSize"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fnAuto->GetParmsSize(), 0);
                        *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = true;
                        safeProcessEvent(canvasSlot, fnAuto, b.data());
                    }
                }
            }

            // AddToViewport at high ZOrder so it's above whatever's behind
            {
                auto* fn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("ZOrder"));
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    if (p)
                        *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 250;
                    safeProcessEvent(userWidget, fn, b.data());
                }
            }

            m_modJoinWorldWidget = FWeakObjectPtr(userWidget);
            setInputModeUI(userWidget);

            VLOG(STR("[JoinWorldUI] placeholder shown, widget={:p}\n"), (void*)userWidget);
            showOnScreen(L"Mod-owned Join World UI shown (Esc to close)", 4.0f, 0.4f, 1.0f, 0.4f);
        }
