// moria_join_world_ui.inl — v6.7.0+ in-place mod of WBP_UI_JoinWorldScreen
//
// ARCHITECTURE: in-place modification, NOT spawn-duplicate.
//
// We do NOT remove the native widget from the viewport, and we do NOT spawn a
// fresh BP instance. Instead, on `OnAfterShow` we modify the live native
// widget in place — tint key TextBlocks, replace its session-history rows with
// JSON-driven mod-controlled rows. The native widget keeps its parent, its
// delegates, its switcher state, and its Esc/back behavior. All button
// wiring works natively.
//
// (v6.6.0 attempted spawn-duplicate via WidgetBlueprintLibrary::Create. That
//  failed because a standalone-spawned UserWidget has no MainMenu wiring —
//  Join, Advanced, Cancel, and Esc all silently broke. Pivoted in v6.7.0.)
//
// Flow:
//   ProcessEvent post-hook on `OnAfterShow` (in dllmain.cpp) detects
//     safeClassName(context) == STR("WBP_UI_JoinWorldScreen_C")
//   → onNativeJoinWorldShown(nativeWidget):
//       sets m_nativeJoinWorldWidget = m_modJoinWorldWidget = nativeWidget
//       sets m_pendingShowJoinWorldUI = true (deferred)
//   → next main tick consumes the flag → applyModificationsToJoinWorld():
//       tints TextBlocks bright sky-blue (visible mod marker)
//       calls injectSessionHistoryRows() (in moria_session_history.inl) to
//         swap native rows for our JSON entries via the BP's own
//         AddSessionHistoryItem(FMorConnectionHistoryItem) UFunction
//
// hideModJoinWorldUI() is a no-op that just clears tracking pointers — native
// flow handles its own dismissal via the BP's normal Close/Esc path. There is
// NO restore-from-RemoveFromParent dance to re-add the native widget.
//
// Asset paths centralised in moria_join_assets.h — included at top of
// dllmain.cpp (cannot be #included from inside a class scope).
//
// v6.18.0 — All v6.6.0 spawn-duplicate state DELETED:
//   font-capture buffers (m_jwFont*), texture caches (m_jwTexBtn*,
//   m_jwIconTexSearch, m_jwBgGradientTex), input-style buffer
//   (m_jwInputStyle), 7 unused class refs (m_jwCls_CraftBigButton, etc.),
//   and 3 unused helper functions (jw_captureFontFromTextBlock,
//   jw_applyCapturedFont, jw_applyFontByAssetPath, plus
//   jw_setCanvasSlot/addToCanvas/setBorderColor in v6.15.0).
// Only m_jwCls_FrontEndButton remains — used by jw_spawnFEButton.
//
// See `cpp-mod/docs/joinworld-ui-takeover.md` for the full methodology.

        // ---------- State ----------
        // Both pointers track the same native widget instance. Kept as two
        // FWeakObjectPtrs for legacy/historical reasons; consolidating to one
        // is a v6.7.1 cleanup task.
        FWeakObjectPtr m_modJoinWorldWidget;
        FWeakObjectPtr m_nativeJoinWorldWidget;
        bool m_pendingShowJoinWorldUI{false};
        bool m_pendingHideJoinWorldUI{false};
        // (m_suppressNextJoinWorldIntercept removed — was used by the
        //  legacy spawn-duplicate path; in-place modification doesn't need it.)
        ULONGLONG m_modJoinWorldShownAt{0};

        // Cached UClass ref harvested from the native widget tree on first
        // intercept. WBP_FrontEndButton_C is the only class we still
        // instantiate via jw_spawnFEButton. UClass lives forever once
        // loaded — safe to cache as raw pointer.
        UClass* m_jwCls_FrontEndButton{nullptr};       // WBP_FrontEndButton_C

        // v6.18.0 — DELETED legacy fields from the v6.6.0 spawn-duplicate
        // path (replaced by in-place modification in v6.7.0):
        //   m_jwCls_CraftBigButton/_GameDataPanel/_SessionHistoryList/
        //   _AdvancedJoinPanel/_LowerThird/_NetworkAlert/_ControlPrompt/
        //   _TextHeader (all write-only after v6.7.0)
        //   m_jwFontTitle/Breadcrumb/Subtitle/HistoryHeader + *Captured
        //     (88-byte FSlateFontInfo capture buffers, never read)
        //   m_jwIconTexSearch, m_jwBgGradientTex, m_jwTexBtnP1Up/P2Up/
        //     CTADisabled (texture caches for the duplicate, never read)
        //   m_jwInputStyle, m_jwInputStyleCaptured + INPUT_STYLE_BYTES
        //     (768-byte EditableText style buffer, never read)

        // v6.18.0 — DELETED unused font helpers (~85 lines):
        //   jw_captureFontFromTextBlock — only called from the v6.6.0
        //     m_ao* font capture path (deleted in v6.18.0).
        //   jw_applyCapturedFont — never called (was the symmetric apply
        //     that consumed jw_captureFontFromTextBlock output).
        //   jw_applyFontByAssetPath — never called (capture-free
        //     alternative; obsolete since v6.7.0 in-place mod).

        // Apply texture + force brush.ImageSize via DIRECT memory write at the
        // known FSlateBrush offsets (resolved via reflection in moria_reflection.h).
        // This bypasses any UMG-side property-sync timing issues — once the
        // bytes are written, SImage's ComputeDesiredSize will return W,H.
        //
        // ResourceObject is set via SetBrushFromTexture(false) UFunction (handles
        // engine-side bookkeeping like material setup). ImageSize is then patched
        // directly. Logs the read-back so we can verify what's actually in memory.
        bool jw_setImageBrush(UObject* img, UObject* texture, UFunction* setBrushFn,
                              float w, float h, const wchar_t* tag = STR("?"))
        {
            if (!img || !isObjectAlive(img) || !setBrushFn || !texture) return false;
            ensureBrushOffset(img);
            if (s_off_brush < 0) return false;
            uint8_t* base = reinterpret_cast<uint8_t*>(img);

            // 1. Set ResourceObject via SetBrushFromTexture(false)
            try { umgSetBrushNoMatch(img, texture, setBrushFn); } catch (...) {}

            // 2. Write Brush.ImageSize directly
            float* xPtr = reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX());
            float* yPtr = reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY());
            *xPtr = w;
            *yPtr = h;

            // 3. Verify by reading back
            float gotW = *xPtr, gotH = *yPtr;
            UObject* gotRes = *reinterpret_cast<UObject**>(base + s_off_brush + brushResourceObj());
            VLOG(STR("[JoinWorldUI] jw_setImageBrush[{}] img={:p} requested=({},{}) read-back=({},{}) tex={:p} resObj={:p}\n"),
                 tag, (void*)img, w, h, gotW, gotH, (void*)texture, (void*)gotRes);
            return true;
        }

        // Load a UTexture2D by /Game/... path. Returns nullptr if not present
        // in the object table (paks normally have all textures resident at boot).
        // Wrapped in try/catch — UE4SS's internal path parser can throw
        // GetPackageNameFromLongName for malformed names, and an uncaught
        // exception out of cacheJoinWorldClassRefs unloads our intercept hook.
        UObject* jw_loadTexture(const wchar_t* path)
        {
            if (!path) return nullptr;
            try {
                return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
            } catch (...) {
                return nullptr;
            }
        }

        // Force-load a UObject by /Game/... path via KismetSystemLibrary:LoadAsset_Blocking.
        // Used when StaticFindObject returns null because the asset isn't yet
        // resident in the engine's object table at intercept time.
        //
        // Constructs a TSoftObjectPtr<UObject> in memory (40 bytes layout in 4.27):
        //   offset  0  FName  AssetPathName       (8: ComparisonIndex + Number)
        //   offset  8  FString SubPathString      (16: ptr + ArrayNum + ArrayMax)
        //   offset 24  int32  TagAtLastTest       (4)
        //   offset 28  FWeakObjectPtr WeakPtr     (8: ObjectIndex + SerialNumber)
        //   offset 36  padding to 40
        //
        // Path format: "/Game/UI/Textures/_Shared/T_UI_Btn_P1_Up.T_UI_Btn_P1_Up"
        // The whole string (including .AssetName) becomes the FName; SubPathString
        // is left empty for top-level assets.
        UObject* jw_loadAssetBlocking(const wchar_t* path)
        {
            if (!path) return nullptr;
            try
            {
                auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"));
                if (!fn) {
                    VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking('{}'): UFunction not found\n"), path);
                    return nullptr;
                }
                auto* cls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary"));
                if (!cls) {
                    VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking: KismetSystemLibrary class not found\n"));
                    return nullptr;
                }
                UObject* cdo = cls->GetClassDefaultObject();
                if (!cdo) {
                    VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking: KSL CDO null\n"));
                    return nullptr;
                }

                auto* pAsset = findParam(fn, STR("Asset"));
                auto* pRet   = findParam(fn, STR("ReturnValue"));
                if (!pAsset || !pRet) {
                    VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking: missing parm Asset={:p} Ret={:p}\n"),
                         (void*)pAsset, (void*)pRet);
                    return nullptr;
                }

                int sz = fn->GetParmsSize();
                std::vector<uint8_t> buf(sz, 0);
                uint8_t* tsop = buf.data() + pAsset->GetOffset_Internal();

                RC::Unreal::FName name(path, RC::Unreal::FNAME_Add);
                uint32_t ci  = name.GetComparisonIndex();
                uint32_t num = name.GetNumber();
                VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking('{}'): FName ci={} num={}, parmSize={} assetOff={} retOff={}\n"),
                     path, ci, num, sz, pAsset->GetOffset_Internal(), pRet->GetOffset_Internal());
                if (ci == 0) {
                    VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking: FName creation returned ci=0 — path malformed\n"));
                    return nullptr;
                }
                std::memcpy(tsop + 0, &ci,  4);
                std::memcpy(tsop + 4, &num, 4);

                safeProcessEvent(cdo, fn, buf.data());
                UObject* result = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking('{}') => {:p}\n"), path, (void*)result);
                return result;
            }
            catch (...) {
                VLOG(STR("[JoinWorldUI] jw_loadAssetBlocking('{}'): EXCEPTION caught\n"), path);
                return nullptr;
            }
        }

        // ---------- Class cache + look-and-feel capture ----------
        // Walks the native WBP_UI_JoinWorldScreen_C widget tree once and:
        //   Cache only m_jwCls_FrontEndButton — that's the one class
        //   we still spawn instances of (jw_spawnFEButton). Everything
        //   else this function used to capture was for the v6.6.0
        //   spawn-duplicate path, which v6.7.0 replaced with in-place
        //   modification. Cleanup landed in v6.18.0.
        void cacheJoinWorldClassRefs(UObject* userWidget)
        {
            if (!userWidget) return;
            if (m_jwCls_FrontEndButton) return; // already cached

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

            // Recursive walker — early-exits as soon as the FrontEndButton
            // class is captured so we don't iterate the whole tree.
            std::function<void(UObject*)> walk = [&](UObject* w) {
                if (!w || !isObjectAlive(w) || m_jwCls_FrontEndButton) return;
                if (safeClassName(w) == STR("WBP_FrontEndButton_C"))
                {
                    if (UClass* c = w->GetClassPrivate()) m_jwCls_FrontEndButton = c;
                    return;
                }
                auto* slotsAddr = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slotsAddr)
                {
                    int n = slotsAddr->Num();
                    for (int i = 0; i < n && !m_jwCls_FrontEndButton; ++i)
                    {
                        UObject* slot = (*slotsAddr)[i];
                        if (!slot || !isObjectAlive(slot)) continue;
                        auto* contentPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (contentPtr && *contentPtr) walk(*contentPtr);
                    }
                }
                auto* singleContent = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (singleContent && *singleContent && !m_jwCls_FrontEndButton) walk(*singleContent);
                auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (nestedWt && *nestedWt && !m_jwCls_FrontEndButton)
                {
                    auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot);
                }
            };

            walk(root);
            VLOG(STR("[JoinWorldUI] class cache: feBtn={:p}\n"), (void*)m_jwCls_FrontEndButton);
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

        // Recursively walk a UserWidget's WidgetTree to find a named child widget.
        // Used for "taming" WBP_FrontEndButton_C internals — we set Visibility on
        // specific state images (HoverImage, FocusImage, etc.) to keep only the
        // resting-state chrome visible.
        UObject* jw_findChildInTree(UObject* userWidget, const wchar_t* targetName)
        {
            if (!userWidget) return nullptr;
            auto* wtPtr = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtPtr ? *wtPtr : nullptr;
            if (!widgetTree) return nullptr;
            auto* rootPtr = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
            UObject* root = rootPtr ? *rootPtr : nullptr;
            if (!root) return nullptr;

            UObject* found = nullptr;
            std::function<void(UObject*)> walk = [&](UObject* w) {
                if (!w || found || !isObjectAlive(w)) return;
                std::wstring nm; try { nm = w->GetName(); } catch (...) {}
                if (nm == targetName) { found = w; return; }
                // PanelWidget.Slots
                auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slots) {
                    for (int i = 0; i < slots->Num() && !found; ++i) {
                        UObject* s = (*slots)[i];
                        if (!s || !isObjectAlive(s)) continue;
                        auto* c = s->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (c && *c) walk(*c);
                    }
                }
                // ContentWidget.Content (Border, SizeBox, ScaleBox, etc.)
                auto* singleC = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (singleC && *singleC) walk(*singleC);
                // Nested UserWidgets have their own WidgetTree
                auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (nestedWt && *nestedWt) {
                    auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot);
                }
            };
            walk(root);
            return found;
        }

        // Hide a named child widget inside a UserWidget tree (Visibility = Hidden=2).
        // Hidden keeps layout space (so siblings don't shift) but doesn't render.
        void jw_setChildVisibility(UObject* userWidget, const wchar_t* childName, uint8_t visibility)
        {
            UObject* w = jw_findChildInTree(userWidget, childName);
            if (!w) return;
            auto* fn = w->GetFunctionByNameInChain(STR("SetVisibility"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InVisibility"));
            if (!p) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            *reinterpret_cast<uint8_t*>(b.data() + p->GetOffset_Internal()) = visibility;
            safeProcessEvent(w, fn, b.data());
        }

        // Spawn a WBP_FrontEndButton_C at given size, set label, hide overdraw
        // state images, and wrap in a clipped SizeBox. Returns the SizeBox UObject*
        // ready to be placed via placeOnCanvas.
        // If iconTex is provided, button is icon-mode; else label is text mode.
        UObject* jw_spawnFEButton(const wchar_t* label, UObject* iconTex,
                                  float W, float H, UClass* sizeBoxClass, UObject* outer)
        {
            if (!m_jwCls_FrontEndButton) return nullptr;
            UObject* btn = jw_createGameWidget(m_jwCls_FrontEndButton);
            if (!btn) return nullptr;

            // Configure label vs icon mode
            if (iconTex)
            {
                auto* texPtr = btn->GetValuePtrByPropertyNameInChain<UObject*>(STR("IconTexture"));
                if (texPtr) *texPtr = iconTex;
                auto* isIconPtr = btn->GetValuePtrByPropertyNameInChain<bool>(STR("isIcon"));
                if (isIconPtr) *isIconPtr = true;
                if (auto* fn = btn->GetFunctionByNameInChain(STR("UpdateImageLabel")))
                {
                    auto* p = findParam(fn, STR("Texture"));
                    if (p)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = iconTex;
                        safeProcessEvent(btn, fn, b.data());
                    }
                }
            }
            else if (label)
            {
                jw_setFrontEndButtonLabel(btn, label);
            }

            // Hide state images that would over-draw our render. Per the BP,
            // these are conditionally visible based on hover/focus/disabled.
            // Setting Visibility = Hidden (2) keeps layout but skips render.
            for (const wchar_t* n : { STR("HoverImage"), STR("FocusImage"),
                                       STR("DisabledImage") })
            {
                jw_setChildVisibility(btn, n, 2);  // 2 = Hidden
            }

            // Tame the BP's content sizing. The BP defaults to large content
            // (ButtonText Leksa 40, icon SizeBox 76 wide) — too big for our
            // compact 90×90 search button and 460×90 advanced button.
            //
            // BP's ButtonText/ButtonIcon resist our overrides — the BP keeps
            // re-applying its design-time settings. NEW TACTIC: hide the BP's
            // internal text + icon, and we DON'T need to add replacements
            // because they're added by the caller (search and Advanced spawn
            // sites) after this function returns, layered on the SizeBox we
            // wrap below.
            UObject* buttonText = jw_findChildInTree(btn, STR("ButtonText"));
            if (buttonText)
            {
                // Try Visibility=Collapsed; ALSO empty the text so if BP Tick
                // resets visibility, there's nothing to render anyway.
                jw_setChildVisibility(btn, STR("ButtonText"), 1);
                umgSetText(buttonText, L"");
                VLOG(STR("[JoinWorldUI] hid+emptied BP ButtonText so we can layer our own\n"));
            }
            // Also empty the BP's ButtonLabel property so UpdateTextLabel can't
            // re-fill ButtonText.Text from it on subsequent ticks.
            if (auto* btnLabelPtr = btn->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel")))
            {
                *btnLabelPtr = FText(L"");
            }
            UObject* buttonIcon = jw_findChildInTree(btn, STR("ButtonIcon"));
            if (buttonIcon)
            {
                jw_setChildVisibility(btn, STR("ButtonIcon"), 1);  // Collapsed
                VLOG(STR("[JoinWorldUI] hid BP ButtonIcon so we can layer our own\n"));
            }

            // Wrap in SizeBox(W, H) and enable clipping so glow that extends
            // beyond the button bounds gets clipped to our spec rect.
            FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
            UObject* sb = UObjectGlobals::StaticConstructObject(sbP);
            if (!sb) return nullptr;
            jw_setSizeBoxOverride(sb, W, H);
            jw_setContent(sb, btn);
            // Enable clipping on the SizeBox so glow images (with negative
            // padding inside the BP) don't bleed outside our W×H rectangle.
            if (auto* fn = sb->GetFunctionByNameInChain(STR("SetClipping")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                auto* p = findParam(fn, STR("InClipping"));
                if (p)
                {
                    *reinterpret_cast<uint8_t*>(b.data() + p->GetOffset_Internal()) = 1;  // ClipToBounds
                    safeProcessEvent(sb, fn, b.data());
                }
            }
            return sb;
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

        // ---------- IN-PLACE MODIFICATION (architecture pivot) ----------
        // Earlier we spawned a fresh BP instance via WidgetBlueprintLibrary::Create
        // and tried to use that as a duplicate. Problem: the standalone instance
        // had no MainMenu wiring, so Join/Advanced/Close/Esc all broke (delegates
        // were never bound). New approach: just modify the native widget in
        // place — tint TextBlocks yellow + inject our session history. All
        // native button wiring, switcher logic, and Esc handling stays intact.
        void onNativeJoinWorldShown(UObject* nativeWidget)
        {
            if (!nativeWidget || !isObjectAlive(nativeWidget)) return;

            VLOG(STR("[JoinWorldUI] native WBP_UI_JoinWorldScreen_C OnAfterShow at {:p}, queuing in-place modification\n"),
                 (void*)nativeWidget);

            cacheJoinWorldClassRefs(nativeWidget);

            // Both pointers track the same instance — we no longer maintain a
            // separate duplicate. m_modJoinWorldWidget remains the gate used
            // by other code (right-click polling, Advanced gate) to know when
            // JoinWorld is "live and modified".
            m_nativeJoinWorldWidget = FWeakObjectPtr(nativeWidget);
            m_modJoinWorldWidget    = FWeakObjectPtr(nativeWidget);
            m_pendingShowJoinWorldUI = true;
        }

        // hideModJoinWorldUI is now mostly a no-op — we don't manage a separate
        // duplicate anymore. Native flow handles Esc, Close, and navigation.
        // Just clear our tracking pointers so other gated code knows JW isn't
        // in our "modified" state any longer.
        void hideModJoinWorldUI()
        {
            VLOG(STR("[JoinWorldUI] hideModJoinWorldUI() — clearing tracking (native handles cleanup)\n"));
            m_modJoinWorldWidget    = FWeakObjectPtr();
            m_nativeJoinWorldWidget = FWeakObjectPtr();
        }

        // ---------- Tick handler ----------
        // Called from on_update / main tick. Modifies the native JoinWorld
        // widget in place — tints + injects session history.
        void tickJoinWorldUI()
        {
            if (m_pendingShowJoinWorldUI)
            {
                m_pendingShowJoinWorldUI = false;
                applyModificationsToJoinWorld();
                m_modJoinWorldShownAt = GetTickCount64();
            }

            if (m_pendingHideJoinWorldUI)
            {
                m_pendingHideJoinWorldUI = false;
                hideModJoinWorldUI();
            }
        }

        // Apply our modifications to the native JoinWorld instance: yellow
        // tints + replace session history rows with our JSON entries.
        void applyModificationsToJoinWorld()
        {
            UObject* widget = m_nativeJoinWorldWidget.Get();
            if (!widget || !isObjectAlive(widget))
            {
                VLOG(STR("[JoinWorldUI] applyModificationsToJoinWorld: widget gone\n"));
                return;
            }

            VLOG(STR("[JoinWorldUI] applying in-place modifications to {:p}\n"), (void*)widget);

            // Yellow tint markers — visual proof that modification fired
            for (const wchar_t* nm : { STR("TextBlock_63"), STR("Title"), STR("InviteCodeLabel") })
            {
                if (UObject* tb = jw_findChildInTree(widget, nm))
                {
                    umgSetTextColor(tb, 0.35f, 0.65f, 1.0f, 1.0f);  // bright sky blue
                    VLOG(STR("[JoinWorldUI] tinted '{}' blue\n"), nm);
                }
            }

            // Replace native session-history rows with mod-controlled JSON entries
            injectSessionHistoryRows(widget);
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

            // The UFunction SetWidthOverride/SetHeightOverride is the ONLY
            // reliable way to set the bitfield bOverride flags — those are
            // uint8 bitfields and direct property writes via reflection
            // don't always honor the bit mask correctly.
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

            // Diagnostic — read the values back and verify they took.
            auto* widthPtr = sb->GetValuePtrByPropertyNameInChain<float>(STR("WidthOverride"));
            auto* heightPtr = sb->GetValuePtrByPropertyNameInChain<float>(STR("HeightOverride"));
            float gotW = widthPtr ? *widthPtr : -1.0f;
            float gotH = heightPtr ? *heightPtr : -1.0f;
            VLOG(STR("[JoinWorldUI] jw_setSizeBoxOverride sb={:p} requested=({},{}) read-back=({},{})\n"),
                 (void*)sb, w, h, gotW, gotH);

            // Force-sync to the Slate widget. SynchronizeProperties pushes the
            // C++ UPROPERTY values (including bOverride bits) to the live
            // Slate widget so it actually constrains its children.
            if (auto* syncFn = sb->GetFunctionByNameInChain(STR("SynchronizeProperties")))
            {
                std::vector<uint8_t> nb(syncFn->GetParmsSize(), 0);
                safeProcessEvent(sb, syncFn, nb.data());
            }
        }

        // v6.15.0 — Three unreferenced legacy helpers DELETED (~115 lines):
        //   jw_setBorderColor, jw_addToCanvas, jw_setCanvasSlot
        // All from the v6.6.0 spawn-duplicate JoinWorld path; never called
        // since v6.7.0 in-place mod approach replaced that strategy.

        // (legacy buildModJoinWorldPlaceholder removed — superseded by
        //  applyModificationsToJoinWorld which modifies the native widget
        //  in place. See onNativeJoinWorldShown / tickJoinWorldUI above.)
