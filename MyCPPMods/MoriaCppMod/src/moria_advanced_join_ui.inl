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
// v6.18.0 — All m_ao* capture buffers DELETED. They were a leftover
// from the v6.6.0 spawn-duplicate path that v6.7.0 replaced with
// in-place modification. Confirmed write-only by the 2026-05-03 audit
// and removed in this version.
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

        // v6.18.0 — DELETED legacy AdvancedJoinOptions capture buffers:
        //   m_aoFontDescription/FieldLabel/JoinError + *Captured flags
        //   m_aoFieldStyle + m_aoFieldStyleCaptured
        //   m_aoFieldBgTex, m_aoDividerTex
        // All from the v6.6.0 spawn-duplicate path, replaced by v6.7.0
        // in-place modification. They were write-only after v6.7.0.

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
        // v6.18.0 — Stripped to a no-op stub. The full ~120-line capture
        // tree walk was for the v6.6.0 spawn-duplicate path; v6.7.0 in-
        // place modification doesn't read any of those captured buffers.
        // Stub kept because onNativeAdvancedJoinShown still calls it.
        // m_jwCls_AdvancedJoinPanel was the only surviving live field
        // and it was write-only too (only used in VLOGs).
        void cacheAdvancedJoinClassRefs(UObject* userWidget)
        {
            (void)userWidget;
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
