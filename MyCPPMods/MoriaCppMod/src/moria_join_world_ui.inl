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

        // Cached UClass refs harvested from the native widget tree on first intercept.
        // UClasses live forever once loaded — safe to cache as raw pointers.
        UClass* m_jwCls_FrontEndButton{nullptr};       // WBP_FrontEndButton_C
        UClass* m_jwCls_CraftBigButton{nullptr};       // UI_WBP_Craft_BigButton_C
        UClass* m_jwCls_GameDataPanel{nullptr};        // WBP_JoinWorldScreen_GameDataPanel_C
        UClass* m_jwCls_SessionHistoryList{nullptr};   // WBP_UI_SessionHistoryList_C
        UClass* m_jwCls_AdvancedJoinPanel{nullptr};    // WBP_UI_AdvancedJoinOptions_C
        UClass* m_jwCls_LowerThird{nullptr};           // UI_WBP_LowerThird_C
        UClass* m_jwCls_NetworkAlert{nullptr};         // WBP_UI_NetworkAlert_C
        UClass* m_jwCls_ControlPrompt{nullptr};        // UI_WBP_HUD_ControlPrompt_C
        UClass* m_jwCls_TextHeader{nullptr};           // UI_WBP_Text_Header_C

        // ---- Look-and-feel cloning: captured FSlateFontInfo struct bytes ----
        // We memcpy the whole 88-byte FSlateFontInfo from native TextBlocks at
        // intercept time, then apply via SetFont when constructing our duplicates.
        // This preserves font asset, typeface, size, color, outline, shadows —
        // everything the harvested JSON couldn't reach via reflection.
        uint8_t m_jwFontTitle[FONT_STRUCT_SIZE]{};         // "JOIN OTHER WORLD"
        uint8_t m_jwFontBreadcrumb[FONT_STRUCT_SIZE]{};    // "WORLD SELECTION"
        uint8_t m_jwFontSubtitle[FONT_STRUCT_SIZE]{};      // "Enter Invite Code..."
        uint8_t m_jwFontHistoryHeader[FONT_STRUCT_SIZE]{}; // "SESSION HISTORY"
        bool m_jwFontTitleCaptured{false};
        bool m_jwFontBreadcrumbCaptured{false};
        bool m_jwFontSubtitleCaptured{false};
        bool m_jwFontHistoryHeaderCaptured{false};

        // ---------- Look-and-feel cloning helpers (PATTERN) ----------
        // Capture the entire FSlateFontInfo struct from a TextBlock's Font property.
        // The struct is plain-old-data + UObject pointers (font asset), no heap-owned
        // members, so a memcpy is safe and fast.
        bool jw_captureFontFromTextBlock(UObject* textBlock, uint8_t* outBuf)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return false;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return false;
            std::memcpy(outBuf, reinterpret_cast<uint8_t*>(textBlock) + fontOff, FONT_STRUCT_SIZE);
            return true;
        }

        // Apply a previously-captured FSlateFontInfo struct to a TextBlock via
        // its SetFont UFunction. Optional sizeOverride lets you reuse the same
        // captured font with a different size (e.g., title font @ 18pt for a label).
        void jw_applyCapturedFont(UObject* textBlock, const uint8_t* fontBuf, int32_t sizeOverride = -1)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InFontInfo"));
            if (!p) return;
            uint8_t local[FONT_STRUCT_SIZE];
            std::memcpy(local, fontBuf, FONT_STRUCT_SIZE);
            if (sizeOverride > 0)
                std::memcpy(local + fontSizeOff(), &sizeOverride, sizeof(int32_t));
            std::vector<uint8_t> parmBuf(fn->GetParmsSize(), 0);
            std::memcpy(parmBuf.data() + p->GetOffset_Internal(), local, FONT_STRUCT_SIZE);
            safeProcessEvent(textBlock, fn, parmBuf.data());
        }

        // ---------- Class cache + look-and-feel capture ----------
        // Walks the native WBP_UI_JoinWorldScreen_C widget tree once and:
        //   1. caches UClass refs for game-styled child widgets we'll instantiate
        //   2. captures FSlateFontInfo bytes from key TextBlocks for font cloning
        // Both run in a single tree walk to avoid double iteration.
        void cacheJoinWorldClassRefs(UObject* userWidget)
        {
            if (!userWidget) return;
            // Already cached?
            if (m_jwCls_FrontEndButton && m_jwCls_GameDataPanel) return;

            // Native UserWidget itself has no children — content lives under
            // WidgetTree.RootWidget. Walk down to that first.
            auto* wtPtr = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtPtr ? *wtPtr : nullptr;
            UObject* root = nullptr;
            if (widgetTree)
            {
                auto* rootPtr = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                root = rootPtr ? *rootPtr : nullptr;
            }
            if (!root)
            {
                VLOG(STR("[JoinWorldUI] cache: no WidgetTree.RootWidget on native widget\n"));
                return;
            }

            // Recursive walker over the panel tree
            std::function<void(UObject*)> walk = [&](UObject* w) {
                if (!w || !isObjectAlive(w)) return;

                std::wstring cls = safeClassName(w);
                std::wstring name;
                try { name = w->GetName(); } catch (...) {}

                UClass* c = w->GetClassPrivate();
                if (c)
                {
                    if (cls == STR("WBP_FrontEndButton_C") && !m_jwCls_FrontEndButton)
                        m_jwCls_FrontEndButton = c;
                    else if (cls == STR("UI_WBP_Craft_BigButton_C") && !m_jwCls_CraftBigButton)
                        m_jwCls_CraftBigButton = c;
                    else if (cls == STR("WBP_JoinWorldScreen_GameDataPanel_C") && !m_jwCls_GameDataPanel)
                        m_jwCls_GameDataPanel = c;
                    else if (cls == STR("WBP_UI_SessionHistoryList_C") && !m_jwCls_SessionHistoryList)
                        m_jwCls_SessionHistoryList = c;
                    else if (cls == STR("WBP_UI_AdvancedJoinOptions_C") && !m_jwCls_AdvancedJoinPanel)
                        m_jwCls_AdvancedJoinPanel = c;
                    else if (cls == STR("UI_WBP_LowerThird_C") && !m_jwCls_LowerThird)
                        m_jwCls_LowerThird = c;
                    else if (cls == STR("WBP_UI_NetworkAlert_C") && !m_jwCls_NetworkAlert)
                        m_jwCls_NetworkAlert = c;
                    else if (cls == STR("UI_WBP_HUD_ControlPrompt_C") && !m_jwCls_ControlPrompt)
                        m_jwCls_ControlPrompt = c;
                    else if (cls == STR("UI_WBP_Text_Header_C") && !m_jwCls_TextHeader)
                        m_jwCls_TextHeader = c;
                }

                // Look-and-feel capture: snapshot FSlateFontInfo bytes from
                // specific named TextBlocks. Names from harvested JSON tree.
                if (cls == STR("TextBlock"))
                {
                    if (name == STR("Title") && !m_jwFontTitleCaptured)
                        m_jwFontTitleCaptured = jw_captureFontFromTextBlock(w, m_jwFontTitle);
                    else if (name == STR("TextBlock_63") && !m_jwFontBreadcrumbCaptured)
                        m_jwFontBreadcrumbCaptured = jw_captureFontFromTextBlock(w, m_jwFontBreadcrumb);
                    else if (name == STR("InviteCodeLabel") && !m_jwFontSubtitleCaptured)
                        m_jwFontSubtitleCaptured = jw_captureFontFromTextBlock(w, m_jwFontSubtitle);
                }

                // Walk into UPanelWidget.Slots — each slot has a Content UWidget
                auto* slotsAddr = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slotsAddr)
                {
                    int n = slotsAddr->Num();
                    for (int i = 0; i < n; ++i)
                    {
                        UObject* slot = (*slotsAddr)[i];
                        if (!slot || !isObjectAlive(slot)) continue;
                        auto* contentPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (contentPtr && *contentPtr)
                            walk(*contentPtr);
                    }
                }
                // Single-child via Content (UContentWidget like Border/SizeBox)
                auto* singleContent = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (singleContent && *singleContent)
                    walk(*singleContent);

                // For nested UUserWidget instances (e.g. WBP_UI_AdvancedJoinOptions_C),
                // also recurse into THEIR WidgetTree.RootWidget so we can pick up classes
                // they define internally.
                auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (nestedWt && *nestedWt)
                {
                    auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    if (nestedRoot && *nestedRoot && *nestedRoot != w)
                        walk(*nestedRoot);
                }
            };

            walk(root);

            VLOG(STR("[JoinWorldUI] class cache: feBtn={:p} bigBtn={:p} gameData={:p} sessHist={:p} advJoin={:p} lowerThird={:p} netAlert={:p} ctrlPrompt={:p} txtHdr={:p}\n"),
                 (void*)m_jwCls_FrontEndButton, (void*)m_jwCls_CraftBigButton,
                 (void*)m_jwCls_GameDataPanel, (void*)m_jwCls_SessionHistoryList,
                 (void*)m_jwCls_AdvancedJoinPanel, (void*)m_jwCls_LowerThird,
                 (void*)m_jwCls_NetworkAlert, (void*)m_jwCls_ControlPrompt,
                 (void*)m_jwCls_TextHeader);
            VLOG(STR("[JoinWorldUI] font capture: title={} breadcrumb={} subtitle={} histHeader={}\n"),
                 m_jwFontTitleCaptured ? 1 : 0, m_jwFontBreadcrumbCaptured ? 1 : 0,
                 m_jwFontSubtitleCaptured ? 1 : 0, m_jwFontHistoryHeaderCaptured ? 1 : 0);
        }

        // Construct a game-styled widget by class. Returns nullptr if class isn't cached.
        // Uses WidgetBlueprintLibrary::Create so the Blueprint's PreConstruct/Construct
        // logic runs and textures self-apply.
        UObject* jw_createGameWidget(UClass* widgetClass)
        {
            if (!widgetClass) return nullptr;
            auto* pc = findPlayerController();
            if (!pc) return nullptr;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return nullptr;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return nullptr;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = widgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            try { safeProcessEvent(wblCDO, createFn, cp.data()); } catch (...) { return nullptr; }
            return pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
        }

        // Set the label on a WBP_FrontEndButton_C. The BP's PreConstruct reads
        // ButtonLabel (FText member) and writes ButtonText.Text from it, so setting
        // the inner TextBlock alone is overwritten. UpdateTextLabel(FText) UFunction
        // sets ButtonLabel correctly so the BP's logic stays consistent.
        void jw_setFrontEndButtonLabel(UObject* btn, const std::wstring& label)
        {
            if (!btn || !isObjectAlive(btn)) return;
            auto* fn = btn->GetFunctionByNameInChain(STR("UpdateTextLabel"));
            if (fn)
            {
                auto* p = findParam(fn, STR("Text"));
                if (p)
                {
                    FText ftext(label.c_str());
                    std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                    std::memcpy(buf.data() + p->GetOffset_Internal(), &ftext, sizeof(FText));
                    safeProcessEvent(btn, fn, buf.data());
                    return;
                }
            }
            // Fallback: write ButtonLabel directly + set ButtonText.Text
            auto* btnLabelPtr = btn->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel"));
            if (btnLabelPtr)
            {
                FText ftext(label.c_str());
                *btnLabelPtr = ftext;
            }
            auto* btnTxtPtr = btn->GetValuePtrByPropertyNameInChain<UObject*>(STR("ButtonText"));
            if (btnTxtPtr && *btnTxtPtr)
                umgSetText(*btnTxtPtr, label);
        }

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

            // Cache class refs for game-styled child widgets — this is our one chance
            // before RemoveFromParent + GC eats the tree.
            cacheJoinWorldClassRefs(nativeWidget);

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
            // No full-viewport overlay — the original screen lets the dwarf
            // render through unmodified. Each input field has its own dark
            // backing box; we don't dim the whole scene.

            // ── CONTENT COLUMN ─────────────────────────────────────────
            // SizeBox(width 720) holding a VerticalBox with all the join widgets.
            // Anchored top-left, offset (80, 100) to match the screenshot.
            UObject* contentSb = mk(sizeBoxClass);
            jw_setSizeBoxOverride(contentSb, 720.0f, 0.0f);

            UObject* contentVb = mk(vboxClass);
            jw_setContent(contentSb, contentVb);

            // 1. Breadcrumb "WORLD SELECTION" — uses captured font if available
            UObject* breadcrumb = mk(textClass);
            umgSetText(breadcrumb, L"WORLD SELECTION");
            if (m_jwFontBreadcrumbCaptured)
                jw_applyCapturedFont(breadcrumb, m_jwFontBreadcrumb);
            else { umgSetTextColor(breadcrumb, 0.55f, 0.85f, 0.85f, 1.0f); umgSetFontSize(breadcrumb, 14); }
            addToVBox(contentVb, breadcrumb);

            UObject* spacer1 = mk(spacerClass); addToVBox(contentVb, spacer1);

            // 2. Title "JOIN OTHER WORLD" — uses captured font (game's serif)
            UObject* title = mk(textClass);
            umgSetText(title, L"JOIN OTHER WORLD");
            if (m_jwFontTitleCaptured)
                jw_applyCapturedFont(title, m_jwFontTitle);
            else { umgSetTextColor(title, 1.0f, 1.0f, 1.0f, 1.0f); umgSetFontSize(title, 28); }
            addToVBox(contentVb, title);

            UObject* spacer2 = mk(spacerClass); addToVBox(contentVb, spacer2);

            // 3. Subtitle "Enter Invite Code..." — uses captured font
            UObject* subtitle = mk(textClass);
            umgSetText(subtitle, L"Enter Invite Code for Hosted Game or Server");
            if (m_jwFontSubtitleCaptured)
                jw_applyCapturedFont(subtitle, m_jwFontSubtitle);
            else { umgSetTextColor(subtitle, 0.85f, 0.85f, 0.85f, 1.0f); umgSetFontSize(subtitle, 16); }
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

            //   Search button — game-styled WBP_FrontEndButton_C if available, fallback to bare UMG
            UObject* searchSb = mk(sizeBoxClass);
            jw_setSizeBoxOverride(searchSb, 100.0f, 48.0f);
            UObject* searchBtn = nullptr;
            if (m_jwCls_FrontEndButton)
            {
                searchBtn = jw_createGameWidget(m_jwCls_FrontEndButton);
                if (searchBtn) jw_setFrontEndButtonLabel(searchBtn, L"Search");
            }
            if (!searchBtn)
            {
                searchBtn = mk(buttonClass);
                UObject* searchLbl = mk(textClass);
                umgSetText(searchLbl, L"Search");
                umgSetTextColor(searchLbl, 1.0f, 1.0f, 1.0f, 1.0f);
                jw_setContent(searchBtn, searchLbl);
            }
            jw_setContent(searchSb, searchBtn);
            UObject* searchSlot = addToHBox(inviteHb, searchSb);
            if (searchSlot) { umgSetVAlign(searchSlot, 2); umgSetSlotPadding(searchSlot, 0, 8, 0, 0); }

            addToVBox(contentVb, inviteHb);

            UObject* spacer3 = mk(spacerClass); addToVBox(contentVb, spacer3);

            // 5. Advanced Join Options button — game-styled WBP_FrontEndButton_C
            UObject* advBtn = nullptr;
            if (m_jwCls_FrontEndButton)
            {
                advBtn = jw_createGameWidget(m_jwCls_FrontEndButton);
                if (advBtn) jw_setFrontEndButtonLabel(advBtn, L"Advanced Join Options");
            }
            if (!advBtn)
            {
                advBtn = mk(buttonClass);
                UObject* advLbl = mk(textClass);
                umgSetText(advLbl, L"  Advanced Join Options  ");
                umgSetTextColor(advLbl, 0.9f, 0.9f, 0.9f, 1.0f);
                jw_setContent(advBtn, advLbl);
            }
            UObject* advSlot = addToVBox(contentVb, advBtn);
            if (advSlot) { umgSetSlotPadding(advSlot, 0, 12, 0, 12); }

            // 6. Session History header — reuses breadcrumb font (same style)
            UObject* histHeader = mk(textClass);
            umgSetText(histHeader, L"SESSION HISTORY");
            if (m_jwFontBreadcrumbCaptured)
                jw_applyCapturedFont(histHeader, m_jwFontBreadcrumb);
            else { umgSetTextColor(histHeader, 0.55f, 0.85f, 0.85f, 1.0f); umgSetFontSize(histHeader, 14); }
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

            // No bottom-left Back button — the original gets its Back UI from a
            // separate LowerThird widget that lives outside the JoinWorldScreen
            // proper. Esc closes our duplicate and restores the native, which
            // brings the LowerThird back along with it. Keep our footprint small.

            // ── ADD CONTENT TO ROOT CANVAS ─────────────────────────────
            UObject* contentSlot = jw_addToCanvas(root, contentSb);
            // Anchor top-left, position (80, 100), auto-size from VerticalBox content.
            jw_setCanvasSlot(contentSlot,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             80.0f, 100.0f, 0.0f, 0.0f,
                             0.0f, 0.0f,
                             true);

            // (back button removed — see note above)

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
