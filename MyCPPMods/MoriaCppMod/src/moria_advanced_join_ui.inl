// moria_advanced_join_ui.inl — v6.7.0+ in-place mod of WBP_UI_AdvancedJoinOptions
//
// ARCHITECTURE: in-place modification (same pattern as moria_join_world_ui.inl).
//
// On OnAfterShow of the native AdvancedJoinOptions panel, we tint key
// TextBlocks bright sky-blue. The native flow handles all interaction —
// IP/password input, Direct Join button, Local Join button, Close button.
// We DO hook the Direct/Local Join button BndEvts (in dllmain.cpp's global
// post-hook) to capture the entered host/port/password into our JSON
// session-history file.
//
// The capture state below (m_aoFontDescription / m_aoFontFieldLabel /
// m_aoFieldStyle / m_aoDividerTex etc.) is LEGACY from the v6.6.0
// spawn-duplicate attempt. It's still populated on intercept but never read.
// Deletion deferred to v6.15.0+ pass; tracked in pending-todo.md.
//
// Reference doc: docs/blueprint-reference/AdvancedJoinOptions.md
// Runtime harvest: docs/widget-harvest/WBP_UI_AdvancedJoinOptions_C.json
// Methodology: docs/joinworld-ui-takeover.md

        // ────────────────────────────────────────────────────────────────
        // STATE — both pointers track the same native widget instance
        // (see moria_join_world_ui.inl for the rationale)
        // ────────────────────────────────────────────────────────────────
        FWeakObjectPtr m_modAdvancedJoinWidget;
        FWeakObjectPtr m_nativeAdvancedJoinWidget;
        bool m_pendingShowAdvancedJoinUI{false};
        bool m_pendingHideAdvancedJoinUI{false};
        ULONGLONG m_modAdvancedJoinShownAt{0};

        // Font captures (in addition to JoinWorld's existing m_jwFont* fields)
        // — these target AdvancedJoinOptions-specific TextBlocks.
        uint8_t m_aoFontDescription[FONT_STRUCT_SIZE]{};   // TextBlock_68 (direct-join description)
        uint8_t m_aoFontFieldLabel[FONT_STRUCT_SIZE]{};    // TextBlock_180 / _1 / _2 / _3 (field labels)
        uint8_t m_aoFontJoinError[FONT_STRUCT_SIZE]{};     // JoinErrorText
        bool m_aoFontDescriptionCaptured{false};
        bool m_aoFontFieldLabelCaptured{false};
        bool m_aoFontJoinErrorCaptured{false};

        // EditableText WidgetStyle (4 input fields all share the same style; one
        // capture covers all). 768 bytes covers any FEditableTextStyle in 4.27.
        // (Reuses m_jwInputStyle if already captured by JoinWorld — but capture
        // a separate one for safety in case the styles differ.)
        uint8_t m_aoFieldStyle[INPUT_STYLE_BYTES]{};
        bool m_aoFieldStyleCaptured{false};

        // Texture captures — input field background + section divider
        UObject* m_aoFieldBgTex{nullptr};
        UObject* m_aoDividerTex{nullptr};

        // ────────────────────────────────────────────────────────────────
        // INTERCEPT — called from dllmain.cpp ProcessEvent post-hook on
        // OnAfterShow of WBP_UI_AdvancedJoinOptions_C.
        // ────────────────────────────────────────────────────────────────
        // IN-PLACE MODIFICATION — see notes in moria_join_world_ui.inl.
        // We don't spawn a duplicate; we tint the native widget in place.
        void onNativeAdvancedJoinShown(UObject* nativeWidget)
        {
            if (!nativeWidget || !isObjectAlive(nativeWidget)) return;

            VLOG(STR("[AdvancedJoinUI] WBP_UI_AdvancedJoinOptions_C OnAfterShow at {:p}, queuing in-place modification\n"),
                 (void*)nativeWidget);

            cacheAdvancedJoinClassRefs(nativeWidget);

            m_nativeAdvancedJoinWidget = FWeakObjectPtr(nativeWidget);
            m_modAdvancedJoinWidget    = FWeakObjectPtr(nativeWidget);
            m_pendingShowAdvancedJoinUI = true;
        }

        // ────────────────────────────────────────────────────────────────
        // CAPTURE PHASE — walk the native widget tree, capture state.
        // Following the ue4-ui-duplication skill's pattern.
        // ────────────────────────────────────────────────────────────────
        void cacheAdvancedJoinClassRefs(UObject* userWidget)
        {
            if (!userWidget) return;

            // Cache the AdvancedJoinOptions UClass directly from the live
            // native widget — it can't be discovered via JoinWorld's tree walk
            // because this BP isn't a child of JoinWorld; it's a sibling that
            // only appears when user clicks the Advanced Join button.
            if (!m_jwCls_AdvancedJoinPanel)
            {
                m_jwCls_AdvancedJoinPanel = static_cast<UClass*>(userWidget->GetClassPrivate());
                VLOG(STR("[AdvancedJoinUI] cached AdvancedJoinOptions UClass={:p} from native instance\n"),
                     (void*)m_jwCls_AdvancedJoinPanel);
            }

            // Skip if everything's already cached (safe to re-enter from
            // multiple show/hide cycles without re-walking the tree).
            if (m_aoFontDescriptionCaptured && m_aoFontFieldLabelCaptured
                && m_aoFieldStyleCaptured && m_aoFieldBgTex)
            {
                return;
            }

            auto* wtPtr = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtPtr ? *wtPtr : nullptr;
            UObject* root = nullptr;
            if (widgetTree)
            {
                auto* rootPtr = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                root = rootPtr ? *rootPtr : nullptr;
            }
            if (!root) {
                VLOG(STR("[AdvancedJoinUI] cache: no WidgetTree.RootWidget\n"));
                return;
            }

            std::function<void(UObject*)> walk = [&](UObject* w) {
                if (!w || !isObjectAlive(w)) return;
                std::wstring cls = safeClassName(w);
                std::wstring name; try { name = w->GetName(); } catch (...) {}

                // Capture FSlateFontInfo from key TextBlocks
                if (cls == STR("TextBlock"))
                {
                    if (name == STR("TextBlock_68") && !m_aoFontDescriptionCaptured)
                        m_aoFontDescriptionCaptured = jw_captureFontFromTextBlock(w, m_aoFontDescription);
                    else if ((name == STR("TextBlock_180") || name == STR("TextBlock_1")
                              || name == STR("TextBlock_2") || name == STR("TextBlock_3"))
                             && !m_aoFontFieldLabelCaptured)
                        m_aoFontFieldLabelCaptured = jw_captureFontFromTextBlock(w, m_aoFontFieldLabel);
                    else if (name == STR("JoinErrorText") && !m_aoFontJoinErrorCaptured)
                        m_aoFontJoinErrorCaptured = jw_captureFontFromTextBlock(w, m_aoFontJoinError);
                }

                // Capture EditableText WidgetStyle from any of the 4 fields
                // (they all share the same style — one capture covers all).
                if (cls == STR("EditableText") && !m_aoFieldStyleCaptured)
                {
                    auto* stylePtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("WidgetStyle"));
                    if (stylePtr)
                    {
                        std::memcpy(m_aoFieldStyle, stylePtr, INPUT_STYLE_BYTES);
                        m_aoFieldStyleCaptured = true;
                        VLOG(STR("[AdvancedJoinUI] captured field WidgetStyle from '{}' ({} bytes)\n"),
                             name.c_str(), INPUT_STYLE_BYTES);
                    }
                }

                // Capture FieldBG image's brush ResourceObject (texture or material)
                if (cls == STR("Image") && !m_aoFieldBgTex
                    && (name == STR("FieldBG") || name == STR("FieldBG_1")
                        || name == STR("FieldBG_2") || name == STR("FieldBG_3")))
                {
                    ensureBrushOffset(w);
                    if (s_off_brush >= 0)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(w);
                        UObject* res = *reinterpret_cast<UObject**>(base + s_off_brush + brushResourceObj());
                        if (res && isReadableMemory(res, 64)) m_aoFieldBgTex = res;
                    }
                }

                // Capture Divider image
                if (cls == STR("Image") && !m_aoDividerTex && name == STR("Divider"))
                {
                    ensureBrushOffset(w);
                    if (s_off_brush >= 0)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(w);
                        UObject* res = *reinterpret_cast<UObject**>(base + s_off_brush + brushResourceObj());
                        if (res && isReadableMemory(res, 64)) m_aoDividerTex = res;
                    }
                }

                // Walk panel children
                auto* slotsAddr = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slotsAddr) {
                    int n = slotsAddr->Num();
                    for (int i = 0; i < n; ++i) {
                        UObject* slot = (*slotsAddr)[i];
                        if (!slot || !isObjectAlive(slot)) continue;
                        auto* contentPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (contentPtr && *contentPtr) walk(*contentPtr);
                    }
                }
                auto* singleContent = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (singleContent && *singleContent) walk(*singleContent);
                auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (nestedWt && *nestedWt) {
                    auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot);
                }
            };
            walk(root);

            VLOG(STR("[AdvancedJoinUI] capture: descriptionFont={} fieldLabelFont={} joinErrorFont={} fieldStyle={} fieldBgTex={:p} dividerTex={:p}\n"),
                 m_aoFontDescriptionCaptured ? 1 : 0,
                 m_aoFontFieldLabelCaptured ? 1 : 0,
                 m_aoFontJoinErrorCaptured ? 1 : 0,
                 m_aoFieldStyleCaptured ? 1 : 0,
                 (void*)m_aoFieldBgTex, (void*)m_aoDividerTex);
        }

        // (legacy showAdvancedJoinUMG removed — superseded by
        //  applyModificationsToAdvancedJoin which modifies the native
        //  widget in place. See onNativeAdvancedJoinShown / tickAdvancedJoinUI above.)


        // ────────────────────────────────────────────────────────────────
        // TICK — applies in-place modifications to the native widget.
        // ────────────────────────────────────────────────────────────────
        void tickAdvancedJoinUI()
        {
            if (m_pendingShowAdvancedJoinUI)
            {
                m_pendingShowAdvancedJoinUI = false;
                applyModificationsToAdvancedJoin();
                m_modAdvancedJoinShownAt = GetTickCount64();
            }

            if (m_pendingHideAdvancedJoinUI)
            {
                m_pendingHideAdvancedJoinUI = false;
                hideModAdvancedJoinUI();
            }
        }

        // Apply our modifications to the native AdvancedJoinOptions widget:
        // yellow tints on key TextBlocks. Native handles all interactions.
        void applyModificationsToAdvancedJoin()
        {
            UObject* widget = m_nativeAdvancedJoinWidget.Get();
            if (!widget || !isObjectAlive(widget))
            {
                VLOG(STR("[AdvancedJoinUI] applyModificationsToAdvancedJoin: widget gone\n"));
                return;
            }

            VLOG(STR("[AdvancedJoinUI] applying in-place modifications to {:p}\n"), (void*)widget);

            for (const wchar_t* nm : { STR("TextBlock_68"), STR("TextBlock_180"),
                                       STR("TextBlock_1"),  STR("TextBlock_2"),
                                       STR("TextBlock_3"),  STR("JoinErrorText") })
            {
                if (UObject* tb = jw_findChildInTree(widget, nm))
                {
                    umgSetTextColor(tb, 0.35f, 0.65f, 1.0f, 1.0f);  // bright sky blue
                    VLOG(STR("[AdvancedJoinUI] tinted '{}' blue\n"), nm);
                }
            }
        }

        // hideModAdvancedJoinUI is now a no-op — native flow handles closing.
        void hideModAdvancedJoinUI()
        {
            VLOG(STR("[AdvancedJoinUI] hideModAdvancedJoinUI() — clearing tracking (native handles cleanup)\n"));
            m_modAdvancedJoinWidget    = FWeakObjectPtr();
            m_nativeAdvancedJoinWidget = FWeakObjectPtr();
        }
