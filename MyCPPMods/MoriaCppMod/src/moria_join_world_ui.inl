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
        bool m_suppressNextJoinWorldIntercept{false};  // re-entry guard: skip next OnAfterShow trigger
        ULONGLONG m_modJoinWorldShownAt{0};

        // ---------- Show / hide ----------
        // Called by post-hook on the game thread. Defers actual UMG work to
        // the next main tick to avoid re-entering the WidgetTree mid-PE.
        void onNativeJoinWorldShown(UObject* nativeWidget)
        {
            if (!nativeWidget || !isObjectAlive(nativeWidget)) return;

            // Re-entry guard: skip this fire if we just restored the native widget
            // (avoids infinite intercept→close→re-show→intercept loop).
            if (m_suppressNextJoinWorldIntercept)
            {
                m_suppressNextJoinWorldIntercept = false;
                VLOG(STR("[JoinWorldUI] post-hook fired but suppress flag set, releasing control to native\n"));
                return;
            }

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
            if (mod && isObjectAlive(mod))
            {
                VLOG(STR("[JoinWorldUI] hideModJoinWorldUI() — removing mod widget\n"));
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
            }
            m_modJoinWorldWidget = FWeakObjectPtr();

            // Restore the native widget so the user is not stranded without UI.
            // Set suppress flag so OnAfterShow on the native (re-fired by AddToViewport)
            // doesn't immediately re-intercept and trap us in a loop.
            UObject* nativ = m_nativeJoinWorldWidget.Get();
            if (nativ && isObjectAlive(nativ))
            {
                VLOG(STR("[JoinWorldUI] restoring native widget {:p} to viewport\n"), (void*)nativ);
                m_suppressNextJoinWorldIntercept = true;
                try
                {
                    auto* fn = nativ->GetFunctionByNameInChain(STR("AddToViewport"));
                    if (fn)
                    {
                        int sz = fn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        auto* p = findParam(fn, STR("ZOrder"));
                        if (p) *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 0;
                        safeProcessEvent(nativ, fn, b.data());
                    }
                }
                catch (...)
                {
                    VLOG(STR("[JoinWorldUI] AddToViewport on native threw — user may be stranded\n"));
                }
            }
            else
            {
                VLOG(STR("[JoinWorldUI] WARNING: native widget is null/dead, cannot restore. User may be stranded.\n"));
            }

            m_nativeJoinWorldWidget = FWeakObjectPtr();
            // Don't switch input mode — let the restored native widget keep UI focus.
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

        // ---------- Helpers ----------
        // Set a UPanelSlot's Content via SetContent UFunction.
        void jw_setContent(UObject* parentSlot, UObject* content)
        {
            if (!parentSlot || !content) return;
            auto* fn = parentSlot->GetFunctionByNameInChain(STR("SetContent"));
            if (!fn) return;
            auto* p = findParam(fn, STR("Content"));
            if (!p) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = content;
            safeProcessEvent(parentSlot, fn, b.data());
        }

        void jw_setSizeBoxOverride(UObject* sb, float w, float h)
        {
            if (!sb) return;
            auto setOne = [&](const wchar_t* fnName, float v)
            {
                if (v <= 0.0f) return;
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
            setOne(STR("SetWidthOverride"), w);
            setOne(STR("SetHeightOverride"), h);
        }

        void jw_setBorderColor(UObject* border, float r, float g, float b, float a)
        {
            if (!border) return;
            auto* fn = border->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InBrushColor"));
            if (!p) return;
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            auto* c = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            c[0] = r; c[1] = g; c[2] = b; c[3] = a;
            safeProcessEvent(border, fn, buf.data());
        }

        // Add a child to a CanvasPanel, return the resulting UCanvasPanelSlot* so we
        // can configure anchors/position/size on it.
        UObject* jw_addToCanvas(UObject* canvas, UObject* child)
        {
            if (!canvas || !child) return nullptr;
            auto* fn = canvas->GetFunctionByNameInChain(STR("AddChildToCanvas"));
            if (!fn) return nullptr;
            auto* pContent = findParam(fn, STR("Content"));
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pContent || !pRet) return nullptr;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            *reinterpret_cast<UObject**>(b.data() + pContent->GetOffset_Internal()) = child;
            safeProcessEvent(canvas, fn, b.data());
            return *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
        }

        // Configure a CanvasPanelSlot.
        void jw_setCanvasSlot(UObject* slot,
                              float anchorMinX, float anchorMinY,
                              float anchorMaxX, float anchorMaxY,
                              float offX, float offY, float sizeX, float sizeY,
                              float alignX, float alignY,
                              bool autoSize)
        {
            if (!slot) return;
            // SetAnchors(FAnchors)
            {
                auto* fn = slot->GetFunctionByNameInChain(STR("SetAnchors"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InAnchors"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = anchorMinX; a[1] = anchorMinY;
                        a[2] = anchorMaxX; a[3] = anchorMaxY;
                        safeProcessEvent(slot, fn, b.data());
                    }
                }
            }
            // SetAlignment(FVector2D)
            {
                auto* fn = slot->GetFunctionByNameInChain(STR("SetAlignment"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InAlignment"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = alignX; a[1] = alignY;
                        safeProcessEvent(slot, fn, b.data());
                    }
                }
            }
            // SetPosition(FVector2D)
            {
                auto* fn = slot->GetFunctionByNameInChain(STR("SetPosition"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InPosition"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = offX; a[1] = offY;
                        safeProcessEvent(slot, fn, b.data());
                    }
                }
            }
            // SetSize(FVector2D)
            if (sizeX > 0 || sizeY > 0)
            {
                auto* fn = slot->GetFunctionByNameInChain(STR("SetSize"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InSize"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* a = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                        a[0] = sizeX; a[1] = sizeY;
                        safeProcessEvent(slot, fn, b.data());
                    }
                }
            }
            // SetAutoSize
            {
                auto* fn = slot->GetFunctionByNameInChain(STR("SetAutoSize"));
                if (fn)
                {
                    auto* p = findParam(fn, STR("InbAutoSize"));
                    if (!p) p = findParam(fn, STR("InAutoSize"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = autoSize;
                        safeProcessEvent(slot, fn, b.data());
                    }
                }
            }
        }

        // ---------- Iteration 2: full-width left-column layout ----------
        // Reproduces WBP_UI_JoinWorldScreen visual structure:
        //   • left-aligned column at x≈80
        //   • title block (small breadcrumb + large title)
        //   • subtitle "Enter Invite Code for Hosted Game or Server"
        //   • invite code field + search button placeholder
        //   • Advanced Join Options pill button
        //   • Session History header
        //   • bottom-left "Back" button
        // No backend wiring yet (Iteration 3) — buttons just close the UI.
        void buildModJoinWorldPlaceholder()
        {
            VLOG(STR("[JoinWorldUI] buildModJoinWorldPlaceholder() iteration 2\n"));

            auto* pc = findPlayerController();
            if (!pc) { VLOG(STR("[JoinWorldUI] no PC, abort\n")); return; }

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* createFn        = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass        = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!userWidgetClass || !createFn || !wblClass) { VLOG(STR("[JoinWorldUI] WBL/UserWidget missing\n")); return; }
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

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // UClass refs
            auto* canvasClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CanvasPanel"));
            auto* borderClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* vboxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* hboxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* textClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* buttonClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            auto* spacerClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Spacer"));
            auto* editClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.EditableText"));
            if (!canvasClass || !borderClass || !sizeBoxClass || !vboxClass || !hboxClass ||
                !textClass || !buttonClass || !spacerClass || !editClass)
            {
                VLOG(STR("[JoinWorldUI] missing UMG class — abort\n"));
                return;
            }

            auto mk = [&](UClass* cls) -> UObject* {
                FStaticConstructObjectParameters p(cls, outer);
                return UObjectGlobals::StaticConstructObject(p);
            };

            // Root: full-viewport CanvasPanel
            UObject* root = mk(canvasClass);
            if (!root) return;
            if (widgetTree) setRootWidget(widgetTree, root);

            // ── BACKGROUND ───────────────────────────────────────────────
            // Translucent dark overlay across the entire viewport (subtle, lets
            // the dwarf scene render through). Anchored 0,0 → 1,1 (full screen).
            UObject* fullBg = mk(borderClass);
            jw_setBorderColor(fullBg, 0.0f, 0.0f, 0.0f, 0.35f);
            UObject* fullBgSlot = jw_addToCanvas(root, fullBg);
            jw_setCanvasSlot(fullBgSlot,
                             0.0f, 0.0f, 1.0f, 1.0f,   // anchors: full
                             0.0f, 0.0f, 0.0f, 0.0f,   // pos/size 0 because anchor stretch
                             0.0f, 0.0f,
                             false);

            // ── CONTENT COLUMN ─────────────────────────────────────────
            // SizeBox(width 720) holding a VerticalBox with all the join widgets.
            // Anchored top-left, offset (80, 100) to match the screenshot.
            UObject* contentSb = mk(sizeBoxClass);
            jw_setSizeBoxOverride(contentSb, 720.0f, 0.0f);

            UObject* contentVb = mk(vboxClass);
            jw_setContent(contentSb, contentVb);

            // 1. Breadcrumb "WORLD SELECTION"
            UObject* breadcrumb = mk(textClass);
            umgSetText(breadcrumb, L"WORLD SELECTION");
            umgSetTextColor(breadcrumb, 0.55f, 0.85f, 0.85f, 1.0f);
            umgSetFontSize(breadcrumb, 14);
            addToVBox(contentVb, breadcrumb);

            UObject* spacer1 = mk(spacerClass); addToVBox(contentVb, spacer1);

            // 2. Title "JOIN OTHER WORLD"
            UObject* title = mk(textClass);
            umgSetText(title, L"JOIN OTHER WORLD");
            umgSetTextColor(title, 1.0f, 1.0f, 1.0f, 1.0f);
            umgSetFontSize(title, 36);
            umgSetBold(title);
            addToVBox(contentVb, title);

            UObject* spacer2 = mk(spacerClass); addToVBox(contentVb, spacer2);

            // 3. Subtitle
            UObject* subtitle = mk(textClass);
            umgSetText(subtitle, L"Enter Invite Code for Hosted Game or Server");
            umgSetTextColor(subtitle, 0.85f, 0.85f, 0.85f, 1.0f);
            umgSetFontSize(subtitle, 16);
            addToVBox(contentVb, subtitle);

            // 4. Invite code row — HorizontalBox with input field + search button
            UObject* inviteHb = mk(hboxClass);

            //   Input field: SizeBox(458) → Border (dark bg) → EditableText
            UObject* inviteInputSb = mk(sizeBoxClass);
            jw_setSizeBoxOverride(inviteInputSb, 458.0f, 48.0f);

            UObject* inviteFieldBg = mk(borderClass);
            jw_setBorderColor(inviteFieldBg, 0.08f, 0.08f, 0.08f, 0.95f);

            UObject* inviteInput = mk(editClass);
            // Hint text via SetHintText (UEditableText::SetHintText takes FText)
            // Simpler: just leave empty, the EditableText shows nothing until user types.

            jw_setContent(inviteFieldBg, inviteInput);
            jw_setContent(inviteInputSb, inviteFieldBg);
            UObject* inviteInputSlot = addToHBox(inviteHb, inviteInputSb);
            if (inviteInputSlot) { umgSetVAlign(inviteInputSlot, 2); umgSetSlotPadding(inviteInputSlot, 0, 8, 12, 0); }

            //   Search button placeholder: small dark square button labeled "Search"
            UObject* searchSb = mk(sizeBoxClass);
            jw_setSizeBoxOverride(searchSb, 100.0f, 48.0f);
            UObject* searchBtn = mk(buttonClass);
            UObject* searchLbl = mk(textClass);
            umgSetText(searchLbl, L"Search");
            umgSetTextColor(searchLbl, 1.0f, 1.0f, 1.0f, 1.0f);
            jw_setContent(searchBtn, searchLbl);
            jw_setContent(searchSb, searchBtn);
            UObject* searchSlot = addToHBox(inviteHb, searchSb);
            if (searchSlot) { umgSetVAlign(searchSlot, 2); umgSetSlotPadding(searchSlot, 0, 8, 0, 0); }

            addToVBox(contentVb, inviteHb);

            UObject* spacer3 = mk(spacerClass); addToVBox(contentVb, spacer3);

            // 5. Advanced Join Options button (pill style)
            UObject* advBtn = mk(buttonClass);
            UObject* advLbl = mk(textClass);
            umgSetText(advLbl, L"  Advanced Join Options  ");
            umgSetTextColor(advLbl, 0.9f, 0.9f, 0.9f, 1.0f);
            jw_setContent(advBtn, advLbl);
            UObject* advSlot = addToVBox(contentVb, advBtn);
            if (advSlot) { umgSetSlotPadding(advSlot, 0, 12, 0, 12); }

            // 6. Session History header
            UObject* histHeader = mk(textClass);
            umgSetText(histHeader, L"SESSION HISTORY");
            umgSetTextColor(histHeader, 0.55f, 0.85f, 0.85f, 1.0f);
            umgSetFontSize(histHeader, 14);
            addToVBox(contentVb, histHeader);

            UObject* histSpacer = mk(spacerClass); addToVBox(contentVb, histSpacer);

            // 7. Session History placeholder row
            UObject* histRowBorder = mk(borderClass);
            jw_setBorderColor(histRowBorder, 0.10f, 0.10f, 0.10f, 0.85f);
            UObject* histRowVb = mk(vboxClass);
            UObject* histRowName = mk(textClass);
            umgSetText(histRowName, L"  (no session history yet — backend wiring in Iteration 3)  ");
            umgSetTextColor(histRowName, 0.7f, 0.7f, 0.7f, 1.0f);
            umgSetFontSize(histRowName, 14);
            addToVBox(histRowVb, histRowName);
            jw_setContent(histRowBorder, histRowVb);
            UObject* histRowSlot = addToVBox(contentVb, histRowBorder);
            if (histRowSlot) { umgSetSlotPadding(histRowSlot, 0, 4, 0, 0); }

            // ── BACK BUTTON (bottom-left) ───────────────────────────────
            // Matches the original "Back" with controller hint at viewport bottom-left.
            UObject* backBtn = mk(buttonClass);
            UObject* backLbl = mk(textClass);
            umgSetText(backLbl, L"  ← Back (Esc)  ");
            umgSetTextColor(backLbl, 1.0f, 1.0f, 1.0f, 1.0f);
            jw_setContent(backBtn, backLbl);

            // ── ADD CONTENT TO ROOT CANVAS ─────────────────────────────
            UObject* contentSlot = jw_addToCanvas(root, contentSb);
            // Anchor top-left, position (80, 100), auto-size from VerticalBox content.
            jw_setCanvasSlot(contentSlot,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             80.0f, 100.0f, 0.0f, 0.0f,
                             0.0f, 0.0f,
                             true);

            UObject* backSlot = jw_addToCanvas(root, backBtn);
            // Anchor bottom-left, position (40, -40) i.e., 40 from left, 40 above bottom.
            jw_setCanvasSlot(backSlot,
                             0.0f, 1.0f, 0.0f, 1.0f,
                             40.0f, -56.0f, 0.0f, 0.0f,
                             0.0f, 0.0f,
                             true);

            // ── ADD TO VIEWPORT ────────────────────────────────────────
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

            VLOG(STR("[JoinWorldUI] iteration 2 layout shown, widget={:p}\n"), (void*)userWidget);
            showOnScreen(L"Mod-owned Join World UI (iteration 2) — Esc to exit", 4.0f, 0.4f, 1.0f, 0.4f);
        }
