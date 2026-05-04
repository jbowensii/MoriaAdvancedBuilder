// moria_settings_ui.inl — v6.8.0+ Settings-screen take-over
//
// GOAL: take over the game's Settings screen the same way we did Join World —
// in-place modification, not spawn-duplicate. End-state features:
//   1. Add custom key-binding entries to WBP_ControlsTab (or its peer)
//   2. Append a new tab to the navbar that hosts our F12 menu equivalent
//      (Cheats, Tweaks, Game Mods, etc.) — replacing the Win32 overlay panel.
//
// CURRENT STATE: instrumentation only. We hook OnAfterShow on the various
// settings screens, log what we see, and (when triggered) dump the live
// widget tree to docs/widget-harvest/ for analysis. No modifications yet.
//
// Settings classes from dumps/CXXHeaderDump:
//   WBP_SettingsScreen_C        — main settings shell, has tabArray + tab members
//   WBP_AccessibilityTab_C      — Accessibility tab content
//   WBP_AudioTab_C              — Audio
//   WBP_ControllerTab_C         — Controller settings
//   WBP_ControllerMappingTab_C  — Controller key mapping
//   WBP_ControlsTab_C           — Keyboard key mapping (★ where we want to add custom keys)
//   WBP_EditMappingTab_C        — Sub-screen for editing a single binding
//   WBP_GameplayTab_C           — Gameplay settings
//   WBP_LegalTab_C              — Legal info / credits
//   WBP_VideoTab_C              — Video / display settings
//   UI_WBP_EscapeMenu2_C        — In-game pause menu (entry point to settings)
//
// Key data structure (from dumps/CXXHeaderDump/FGKUIToolkit.hpp):
//   struct FFGKUITab {                                  // 0xE8 bytes
//       FName Name;                       // 0x00
//       FText DisplayName;                // 0x08 (FText, 0x18)
//       TSoftClassPtr<UUserWidget> WidgetClass; // 0x20 (TSoftClassPtr, 0x28)
//       FFGKUIScreenConfig TabConfig;     // 0x48 (0xA0)
//   };
//
// To add a tab: build an FFGKUITab and append to WBP_SettingsScreen.tabArray
// before the BP populates the navbar. Best hook point is `Construct()` or the
// custom `GetNavBarTabs()` UFunction documented on UWBP_SettingsScreen_C.
//
// See `cpp-mod/docs/joinworld-ui-takeover.md` for the in-place methodology.

        // ────────────────────────────────────────────────────────────────
        // STATE
        // ────────────────────────────────────────────────────────────────
        FWeakObjectPtr m_nativeSettingsScreen;
        bool m_pendingShowSettings{false};

        // True while the Settings UI is on screen. Mod input handlers
        // (Quick Build F-keys, etc.) check this and bail out so user
        // keypresses for rebind don't trigger gameplay actions.
        bool m_settingsScreenOpen{false};
        bool isSettingsScreenOpen() const { return m_settingsScreenOpen; }

        // Set of class names we've already logged once on first sight, so the
        // log doesn't spam if the user opens/closes Settings repeatedly.
        std::set<std::wstring> m_settingsClassesSeen;

        // Cached UClasses used to spawn rows in the keymap tab.
        UClass* m_settingsKeySelectorCls{nullptr};
        UClass* m_settingsSectionHeadingCls{nullptr};
        UClass* m_settingsCheckBoxCls{nullptr};   // v0.32 — for ON/OFF toggles
        UClass* m_settingsCarouselCls{nullptr};   // v0.33 — for cycle rows (tweaks)
        // v0.37 — captured FSlateBrush bytes from a native CheckIcon
        // (MinimapLock or StreamerMode). Pasted onto each spawned
        // checkbox's CheckIcon so the check graphic actually displays.
        // FSlateBrush is ~136 bytes in UE 4.27.
        uint8_t m_checkIconBrush[160]{};
        bool    m_checkIconBrushCaptured{false};

        // Track which EditMappingTab instance we already injected into so we
        // don't duplicate rows if the tab is shown a second time on the same
        // instance. The tab is recycled per Settings session.
        FWeakObjectPtr m_keymapInjectedFor;

        // Per-row state for INI persistence + modifier-glyph display.
        //   isSetRow — true for the 8 "Quick Build N Set" rows. Distinct
        //              from modBits because a SET row can be rebound to
        //              a non-modifier chord (just G) and still count as
        //              the SET slot.
        //   modBits  — bShift bit0, bCtrl bit1, bAlt bit2 — drives glyph.
        struct KeymapRow {
            FWeakObjectPtr selector;
            int            bindIdx;     // s_bindings index, or -1
            uint8_t        vk;
            bool           isSetRow;
            uint8_t        modBits;
            std::string    iniKey;
            FWeakObjectPtr modGlyphImg;
            FWeakObjectPtr nativeKeyGlyphForSize; // captured KeyGlyph for brush.ImageSize cloning
            uint8_t        lastAppliedModBits{0xFF}; // sentinel, forces first apply
        };
        std::vector<KeymapRow> m_keymapRows;

        // Cached input-glyph textures + brush UFunction. Loaded on first
        // SET row injection; reused across all rows.
        UObject*   m_glyphTex_Shift{nullptr};
        UObject*   m_glyphTex_Ctrl{nullptr};
        UObject*   m_glyphTex_Alt{nullptr};
        UFunction* m_imageSetBrushFn{nullptr};

        // ────────────────────────────────────────────────────────────────
        // INTERCEPT — called from dllmain.cpp ProcessEvent post-hook on
        // OnAfterShow / Construct of any Settings-related UClass.
        // ────────────────────────────────────────────────────────────────
        void onNativeSettingsScreenShown(UObject* nativeWidget)
        {
            if (!nativeWidget || !isObjectAlive(nativeWidget)) return;

            std::wstring cls = safeClassName(nativeWidget);
            VLOG(STR("[SettingsUI] {} shown at {:p}\n"), cls.c_str(), (void*)nativeWidget);

            m_nativeSettingsScreen = FWeakObjectPtr(nativeWidget);
            m_pendingShowSettings  = true;
            // Block mod input so user keypresses for rebind don't
            // trigger gameplay (Quick Build, etc.).
            if (cls == STR("WBP_SettingsScreen_C")) m_settingsScreenOpen = true;
        }

        void onNativeSettingsScreenHidden()
        {
            m_settingsScreenOpen = false;
        }

        // Generic logger fired once-per-class for any settings-related widget
        // we see. Helps us discover what classes actually appear at runtime
        // (header dumps list candidates; runtime tells us which ARE used).
        void onSettingsRelatedShown(UObject* widget, const wchar_t* fnName)
        {
            if (!widget) return;
            std::wstring cls = safeClassName(widget);
            if (m_settingsClassesSeen.count(cls)) return;  // first-sight only
            m_settingsClassesSeen.insert(cls);

            std::wstring path;
            try {
                if (UClass* uc = static_cast<UClass*>(widget->GetClassPrivate()))
                    path = uc->GetFullName();
            } catch (...) {}

            VLOG(STR("[SettingsUI] FIRST-SIGHT cls='{}' fn='{}' fullPath='{}'\n"),
                 cls.c_str(), fnName ? fnName : STR("?"), path.c_str());
        }

        // ────────────────────────────────────────────────────────────────
        // TICK — applies modifications when the pending flag is set.
        // For now: walk the widget tree and dump structure to log.
        // ────────────────────────────────────────────────────────────────
        void tickSettingsUI()
        {
            if (!m_pendingShowSettings) return;
            m_pendingShowSettings = false;

            UObject* w = m_nativeSettingsScreen.Get();
            if (!w || !isObjectAlive(w)) return;

            VLOG(STR("[SettingsUI] dumping widget tree of {} ({:p})\n"),
                 safeClassName(w).c_str(), (void*)w);

            // List the tab-member properties so we can see which are populated.
            // Property names from WBP_SettingsScreen.hpp.
            for (const wchar_t* memberName : {
                STR("WBP_AccessibilityTab"),
                STR("WBP_AudioTab"),
                STR("WBP_ControllerMappingTab"),
                STR("WBP_ControllerTab"),
                STR("WBP_ControlsTab_87"),
                STR("WBP_EditMappingTab"),
                STR("WBP_GameplayTab"),
                STR("WBP_LegalTab"),
                STR("WBP_VideoTab"),
            })
            {
                auto* ptr = w->GetValuePtrByPropertyNameInChain<UObject*>(memberName);
                UObject* tab = ptr ? *ptr : nullptr;
                if (tab && isObjectAlive(tab))
                {
                    VLOG(STR("[SettingsUI]   member '{}' -> {} {:p}\n"),
                         memberName, safeClassName(tab).c_str(), (void*)tab);
                }
                else
                {
                    VLOG(STR("[SettingsUI]   member '{}' -> null/dead\n"), memberName);
                }
            }

            // Read tabArray length so we can plan how to inject a new tab.
            // Each FFGKUITab is 0xE8 bytes:
            //   0x00  FName Name
            //   0x08  FText DisplayName (0x18)
            //   0x20  TSoftClassPtr<UUserWidget> WidgetClass (0x28)
            //   0x48  FFGKUIScreenConfig TabConfig (0xA0)
            // We dump each entry's FName so we know the existing tab keys
            // and can pick a non-colliding name for our injected tab.
            auto* tabArrFnameBased = w->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("tabArray"));
            if (tabArrFnameBased)
            {
                int n = tabArrFnameBased->Num();
                VLOG(STR("[SettingsUI]   tabArray.Num() = {}\n"), n);

                // The TArray is read as bytes; each element is 0xE8 long.
                // Walk by manual stride and decode FName at offset 0.
                if (uint8_t* base = tabArrFnameBased->GetData())
                {
                    constexpr int kStride = 0xE8;
                    for (int i = 0; i < n && i < 16; ++i)
                    {
                        uint8_t* entry = base + (size_t)i * kStride;
                        // FName is 8 bytes — 4 ComparisonIndex + 4 Number
                        FName* fn = reinterpret_cast<FName*>(entry + 0x00);
                        std::wstring nameStr;
                        try { nameStr = fn->ToString(); } catch (...) {}
                        VLOG(STR("[SettingsUI]     tabArray[{}].Name = '{}'\n"),
                             i, nameStr.empty() ? STR("(?)") : nameStr.c_str());
                    }
                }
            }

            // Dump WBP_EditMappingTab structure if alive — that's where the
            // key-binding entries live (each is a WBP_SettingsKeySelector_C
            // member field). To inject custom keys we need to find the
            // parent container (likely a VerticalBox under a ScrollBox).
            auto* editMapPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_EditMappingTab"));
            UObject* editMap = editMapPtr ? *editMapPtr : nullptr;
            if (editMap && isObjectAlive(editMap))
            {
                VLOG(STR("[SettingsUI]   probing WBP_EditMappingTab tree...\n"));
                // Find a known KeySelector member to derive its parent
                for (const wchar_t* probeName : {
                    STR("AttackMineKey"), STR("Build"), STR("CalloutKey"),
                    STR("CrouchKey"), STR("DodgeKey"), STR("ChatKey")
                })
                {
                    auto* sel = editMap->GetValuePtrByPropertyNameInChain<UObject*>(probeName);
                    UObject* selWidget = sel ? *sel : nullptr;
                    if (!selWidget || !isObjectAlive(selWidget)) continue;
                    // Walk Outer chain to find the parent panel (UPanelWidget)
                    UObject* outer = nullptr;
                    try { outer = selWidget->GetOuterPrivate(); } catch (...) {}
                    VLOG(STR("[SettingsUI]     KeySelector '{}' = {} {:p}, outer = {} {:p}\n"),
                         probeName,
                         safeClassName(selWidget).c_str(), (void*)selWidget,
                         outer ? safeClassName(outer).c_str() : STR("(null)"),
                         (void*)outer);
                    break;  // one probe is enough for path discovery
                }
            }

            // v6.8.0 CP1 — inject mod keymap rows into the EditMappingTab if it's
            // already alive at the moment SettingsScreen is shown. The tab
            // is constructed eagerly by the BP, so this works on the very
            // first Settings open without waiting for the user to click into
            // the keymap tab.
            applyModificationsToSettings();

            // Dump WBP_GameplayTab's ParentPanel so we know where a "Mod"
            // tab content widget would naturally fit (it's a UVerticalBox
            // member named "ParentPanel" on GameplayTab).
            auto* gpPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_GameplayTab"));
            UObject* gp = gpPtr ? *gpPtr : nullptr;
            if (gp && isObjectAlive(gp))
            {
                auto* ppPtr = gp->GetValuePtrByPropertyNameInChain<UObject*>(STR("ParentPanel"));
                UObject* pp = ppPtr ? *ppPtr : nullptr;
                if (pp && isObjectAlive(pp))
                {
                    VLOG(STR("[SettingsUI]     GameplayTab.ParentPanel = {} {:p}\n"),
                         safeClassName(pp).c_str(), (void*)pp);
                }
            }
        }

        // ────────────────────────────────────────────────────────────────
        // KEYMAP TAB INJECTION (v6.8.0 CP1 — visible rows, no chord wiring yet)
        // ────────────────────────────────────────────────────────────────
        // Schema: each row in the keymap tab is a HorizontalBox containing
        //   - left: TextBlock label (action name, e.g. "Quick Build 1")
        //   - right: WBP_SettingsKeySelector_C (chord display + edit)
        // We append our rows under two new section headings:
        //   "INVENTORY AND ITEMS"   — 16 Quick Build entries (use + set)
        //   "MOD KEY BINDINGS"      — 15 mod-action entries
        // Both go AFTER the native Emotes section, at the bottom of the
        // ScrollBox. Future iteration (v0.2+) wires chord persistence and
        // dispatch; v0.1 is layout-only.

        struct ModKeymapRow
        {
            const wchar_t* label;
            const wchar_t* iniKey;     // for v0.2 persistence
            const wchar_t* defaultChord; // human-readable e.g. "Shift+F1"
        };

        // Build the chord display string for a given s_bindings[] index,
        // optionally with a "Shift +" prefix (used for the Quick Build SET
        // variants which currently infer the modifier from s_modifierVK at
        // dispatch time).
        std::wstring chordTextForBinding(int bindIdx, bool withShift = false)
        {
            if (bindIdx < 0 || bindIdx >= BIND_COUNT) return L"(unbound)";
            uint8_t vk = s_bindings[bindIdx].key;
            if (!vk) return L"(unbound)";
            std::wstring base = keyName(vk);
            if (withShift) return std::wstring(L"Shift + ") + base;
            return base;
        }

        // Inventory and Items — 16 entries (Quick Build N + Quick Build N Set
        // for N=1..8). The "use" chord is s_bindings[N-1]; the "set" chord is
        // currently inferred at dispatch as "Shift + use chord".
        // Indices: BIND_QB[0..7] = 0..7 in s_bindings[].
        std::vector<ModKeymapRow> buildInventoryRows()
        {
            // Static label / iniKey strings (per-slot). Default chord text is
            // computed at runtime from s_bindings[] so it reflects the user's
            // current binding instead of a hardcoded "F1" placeholder.
            static const wchar_t* useLabels[8] = {
                STR("Quick Build 1"), STR("Quick Build 2"), STR("Quick Build 3"), STR("Quick Build 4"),
                STR("Quick Build 5"), STR("Quick Build 6"), STR("Quick Build 7"), STR("Quick Build 8")
            };
            static const wchar_t* setLabels[8] = {
                STR("Quick Build 1 Set"), STR("Quick Build 2 Set"), STR("Quick Build 3 Set"), STR("Quick Build 4 Set"),
                STR("Quick Build 5 Set"), STR("Quick Build 6 Set"), STR("Quick Build 7 Set"), STR("Quick Build 8 Set")
            };
            static const wchar_t* useIniKeys[8] = {
                STR("QuickBuild1.Use"), STR("QuickBuild2.Use"), STR("QuickBuild3.Use"), STR("QuickBuild4.Use"),
                STR("QuickBuild5.Use"), STR("QuickBuild6.Use"), STR("QuickBuild7.Use"), STR("QuickBuild8.Use")
            };
            static const wchar_t* setIniKeys[8] = {
                STR("QuickBuild1.Set"), STR("QuickBuild2.Set"), STR("QuickBuild3.Set"), STR("QuickBuild4.Set"),
                STR("QuickBuild5.Set"), STR("QuickBuild6.Set"), STR("QuickBuild7.Set"), STR("QuickBuild8.Set")
            };
            // m_chordCache holds the rendered chord wstrings whose c_str() we
            // hand back via ModKeymapRow.defaultChord — keep them alive.
            m_chordCache.clear();
            m_chordCache.reserve(16);
            std::vector<ModKeymapRow> rows;
            rows.reserve(16);
            for (int i = 0; i < 8; ++i)
            {
                m_chordCache.push_back(chordTextForBinding(i, false));
                rows.push_back({ useLabels[i], useIniKeys[i], m_chordCache.back().c_str() });
                m_chordCache.push_back(chordTextForBinding(i, true));
                rows.push_back({ setLabels[i], setIniKeys[i], m_chordCache.back().c_str() });
            }
            return rows;
        }

        // Mod Key Bindings — 15 entries. defaultChord pulled from existing
        // s_bindings[] via the index map below. Indices that don't yet exist
        // (Flying Dwarf, Duplicate Target — new in v6.8.0) show "(unbound)".
        std::vector<ModKeymapRow> buildModKeymapRows()
        {
            // v0.28 — six binds (Set Rotation, Pitch Rotate, Roll Rotate,
            // Trash Item, Replenish Item, Remove Attributes) MOVED into
            // native sections (Building / Inventory and Items) via
            // injectIntoNativeSection(). This MOD KEY BINDINGS section
            // keeps the 9 entries that have no native home.
            struct Spec { const wchar_t* label; const wchar_t* iniKey; int bindIdx; };
            static const Spec specs[] = {
                { STR("Snap Toggle"),        STR("ModBind.SnapToggle"),      9  /* BIND_SNAP */ },
                { STR("Integrity Check"),    STR("ModBind.IntegrityCheck"),  10 /* unused slot */ },
                { STR("Invisible Dwarf"),    STR("ModBind.InvisibleDwarf"),  11 },
                { STR("Flying Dwarf"),       STR("ModBind.FlyingDwarf"),     -1 },
                { STR("Target"),             STR("ModBind.Target"),          12 /* BIND_TARGET */ },
                { STR("Duplicate Target"),   STR("ModBind.DuplicateTarget"), -1 },
                { STR("Remove Single"),      STR("ModBind.RemoveSingle"),    14 },
                { STR("Undo Remove"),        STR("ModBind.UndoRemove"),      15 },
                { STR("Remove All"),         STR("ModBind.RemoveAll"),       16 },
            };

            std::vector<ModKeymapRow> rows;
            rows.reserve(sizeof(specs) / sizeof(specs[0]));
            for (const auto& s : specs)
            {
                m_chordCache.push_back(chordTextForBinding(s.bindIdx, false));
                rows.push_back({ s.label, s.iniKey, m_chordCache.back().c_str() });
            }
            return rows;
        }

        // Backing storage for runtime-computed chord display strings.
        // Lifetime: cleared at the start of each row-build pass; entries
        // remain valid for that injection cycle only.
        std::vector<std::wstring> m_chordCache;

        // Resolve and cache the WBP_SettingsKeySelector_C / SectionHeading
        // UClasses by reading them off LIVE instances on the EditMappingTab.
        // The asset paths aren't reliable (we don't know the exact /Game/...
        // package path), but every native row is already an instance of the
        // class we want, so we just read GetClassPrivate() off one and cache.
        //
        // EditMappingTab has these known-named members we can probe:
        //   KeySelector:  AttackMineKey, Build, CalloutKey, ChatKey, ...
        //   Heading:      ConstructionHeading, EmotesHeading, EquipmentHeading,
        //                 GeneralHeading, MovementHeading
        bool ensureSettingsKeyClassesCached(UObject* editMapTab)
        {
            if (m_settingsKeySelectorCls && m_settingsSectionHeadingCls) return true;
            if (!editMapTab || !isObjectAlive(editMapTab)) return false;

            if (!m_settingsKeySelectorCls)
            {
                for (const wchar_t* probe : {
                    STR("AttackMineKey"), STR("Build"), STR("ChatKey"),
                    STR("CalloutKey"), STR("CrouchKey"), STR("DodgeKey")
                })
                {
                    auto* p = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(probe);
                    UObject* w = p ? *p : nullptr;
                    if (w && isObjectAlive(w))
                    {
                        m_settingsKeySelectorCls = static_cast<UClass*>(w->GetClassPrivate());
                        if (m_settingsKeySelectorCls)
                        {
                            VLOG(STR("[SettingsUI] cached WBP_SettingsKeySelector UClass={:p} from member '{}'\n"),
                                 (void*)m_settingsKeySelectorCls, probe);
                            break;
                        }
                    }
                }
            }
            if (!m_settingsSectionHeadingCls)
            {
                for (const wchar_t* probe : {
                    STR("ConstructionHeading"), STR("EmotesHeading"),
                    STR("EquipmentHeading"),    STR("GeneralHeading"),
                    STR("MovementHeading")
                })
                {
                    auto* p = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(probe);
                    UObject* w = p ? *p : nullptr;
                    if (w && isObjectAlive(w))
                    {
                        m_settingsSectionHeadingCls = static_cast<UClass*>(w->GetClassPrivate());
                        if (m_settingsSectionHeadingCls)
                        {
                            VLOG(STR("[SettingsUI] cached WBP_SettingsSectionHeading UClass={:p} from member '{}'\n"),
                                 (void*)m_settingsSectionHeadingCls, probe);
                            break;
                        }
                    }
                }
            }
            return m_settingsKeySelectorCls && m_settingsSectionHeadingCls;
        }

        // Spawn a SectionHeading widget with the given label, ready to
        // append to a ScrollBox. Returns nullptr on failure.
        UObject* spawnSectionHeading(const wchar_t* labelText)
        {
            if (!m_settingsSectionHeadingCls) return nullptr;
            UObject* heading = jw_createGameWidget(m_settingsSectionHeadingCls);
            if (!heading) return nullptr;

            // Set HeadingLabel (FText member at offset 0x278 per
            // WBP_SettingsSectionHeading.hpp). PreConstruct copies it to
            // HeadingText.Text. Use the property by name for portability.
            if (auto* labelPtr = heading->GetValuePtrByPropertyNameInChain<FText>(STR("HeadingLabel")))
            {
                *labelPtr = FText(labelText);
            }
            // Also write the inner HeadingText TextBlock directly in case
            // PreConstruct already ran and won't re-fire.
            if (auto* tbPtr = heading->GetValuePtrByPropertyNameInChain<UObject*>(STR("HeadingText")))
            {
                if (UObject* tb = *tbPtr)
                    umgSetText(tb, labelText);
            }
            return heading;
        }

        // Helper to set FSlateChildSize on a panel slot (for HorizontalBox /
        // VerticalBox / ScrollBox slots). Stride and field layout:
        //   FSlateChildSize { float Value @0; uint8 SizeRule @4 }
        //   ESlateSizeRule: Automatic=0, Fill=1
        void setSlotSizeFill(UObject* slot, float value = 1.0f)
        {
            if (!slot) return;
            auto* fn = slot->GetFunctionByNameInChain(STR("SetSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InSizeArgs"));
            if (!p) p = findParam(fn, STR("Size"));
            if (!p) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            uint8_t* d = b.data() + p->GetOffset_Internal();
            *reinterpret_cast<float*>(d + 0) = value;
            *(d + 4) = 1; // SizeRule=Fill
            safeProcessEvent(slot, fn, b.data());
        }

        // Spawn a row matching native Edit Mapping rows EXACTLY by re-using
        // the BP itself (Pattern B: re-instantiate-and-tame). The native
        // KeySelector's internal WidgetTree already provides:
        //   SizeBox > Overlay > HBox
        //     ├── Border 'LabelBorder' > SizeBox > ScaleBox > TextBlock 'OptionNameTextBlock'
        //     └── Border 'InputBorder' > Overlay { InputKeySelector + keyText + glyph + alert }
        //
        // 'OptionNameTextBlock' is exposed as a class member on the parent
        // class UMorSettingsElement (offset 0x278), and 'OptionName' (FText
        // at 0x280) is the source-of-truth that PreConstruct copies onto
        // OptionNameTextBlock. Set both for safety.
        //
        // No outer HBox/Border wrapper — the BP IS the row.
        UObject* spawnKeybindRow(UObject* /*outer*/,
                                 const wchar_t* labelText,
                                 const wchar_t* /*defaultChordText*/,
                                 uint8_t defaultVk,
                                 bool defaultShift,
                                 std::vector<UObject*>& outSelectorsToBind)
        {
            if (!m_settingsKeySelectorCls) return nullptr;

            UObject* selector = jw_createGameWidget(m_settingsKeySelectorCls);
            if (!selector) return nullptr;

            // 1. Set OptionName FText (source-of-truth that PreConstruct
            //    typically copies to OptionNameTextBlock.Text).
            if (auto* namePtr = selector->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(labelText);

            // 2. Also set the OptionNameTextBlock directly in case
            //    PreConstruct already ran or doesn't copy on its own.
            if (auto* tbPtr = selector->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, labelText);

            // Default chord intentionally NOT applied here — must happen
            // AFTER the selector is added to the ScrollBox. Construct()
            // fires on add and runs ResetToCurrent() which clears
            // CurrentSelectedKey. Caller is responsible for invoking
            // applyDefaultChordToSelector() post-add.
            outSelectorsToBind.push_back(selector);
            return selector;
        }

        // Robust SetContent for UContentWidget descendants. UBorder /
        // UButton's SetContent UFunction declares its parameter as
        // `InContent` (UE4 default) but the older jw_setContent helper
        // looked only for `Content`. Try both.
        void jw_setContentEither(UObject* contentWidget, UObject* child)
        {
            if (!contentWidget || !child) return;
            auto* fn = contentWidget->GetFunctionByNameInChain(STR("SetContent"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InContent"));
            if (!p) p = findParam(fn, STR("Content"));
            if (!p) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = child;
            safeProcessEvent(contentWidget, fn, b.data());
        }

        // VK → engine FKey FName. Engine uses specific names (F1, SpaceBar,
        // LeftMouseButton, etc.) — keyName() output mostly matches but a few
        // need translation.
        std::wstring vkToFKeyName(uint8_t vk)
        {
            // F1..F12 → "F1".."F12" (matches keyName)
            if (vk >= 0x70 && vk <= 0x7B)
            {
                wchar_t b[8]; swprintf_s(b, L"F%d", vk - 0x70 + 1); return b;
            }
            // Numpad 0..9 → "NumPadZero"..."NumPadNine" in engine FKey table
            switch (vk)
            {
                case VK_NUMPAD0: return L"NumPadZero";
                case VK_NUMPAD1: return L"NumPadOne";
                case VK_NUMPAD2: return L"NumPadTwo";
                case VK_NUMPAD3: return L"NumPadThree";
                case VK_NUMPAD4: return L"NumPadFour";
                case VK_NUMPAD5: return L"NumPadFive";
                case VK_NUMPAD6: return L"NumPadSix";
                case VK_NUMPAD7: return L"NumPadSeven";
                case VK_NUMPAD8: return L"NumPadEight";
                case VK_NUMPAD9: return L"NumPadNine";
                case VK_MULTIPLY:  return L"Multiply";
                case VK_ADD:       return L"Add";
                case VK_SUBTRACT:  return L"Subtract";
                case VK_DIVIDE:    return L"Divide";
                case VK_DECIMAL:   return L"Decimal";
                case VK_DELETE:    return L"Delete";
                case VK_INSERT:    return L"Insert";
                case VK_HOME:      return L"Home";
                case VK_END:       return L"End";
                case VK_PRIOR:     return L"PageUp";
                case VK_NEXT:      return L"PageDown";
                case VK_LEFT:      return L"Left";
                case VK_RIGHT:     return L"Right";
                case VK_UP:        return L"Up";
                case VK_DOWN:      return L"Down";
                case VK_SPACE:     return L"SpaceBar";
                case VK_RETURN:    return L"Enter";
                case VK_TAB:       return L"Tab";
                case VK_ESCAPE:    return L"Escape";
                case VK_BACK:      return L"BackSpace";
                case VK_OEM_4:     return L"LeftBracket";
                case VK_OEM_6:     return L"RightBracket";
                case VK_OEM_5:     return L"Backslash";
                case VK_OEM_COMMA: return L"Comma";
                case VK_OEM_PERIOD:return L"Period";
                case VK_OEM_1:     return L"Semicolon";
                case VK_OEM_2:     return L"Slash";
                case VK_OEM_7:     return L"Apostrophe";
                case VK_OEM_3:     return L"Tilde";
                case VK_OEM_MINUS: return L"Hyphen";
                case VK_OEM_PLUS:  return L"Equals";
            }
            // 0-9 / A-Z map to ASCII char names ("Zero".."Nine", "A".."Z")
            if (vk >= '0' && vk <= '9')
            {
                static const wchar_t* digitNames[] = {
                    L"Zero", L"One", L"Two", L"Three", L"Four",
                    L"Five", L"Six", L"Seven", L"Eight", L"Nine"
                };
                return digitNames[vk - '0'];
            }
            if (vk >= 'A' && vk <= 'Z')
            {
                wchar_t b[2] = { (wchar_t)vk, 0 };
                return b;
            }
            return L"";
        }

        // Apply a default chord to a freshly-spawned KeySelector.
        //
        // Strategy (works because we know each step succeeds independently):
        //   1. Call OnKeySelectedBP(chord) — populates the FKey portion of
        //      CurrentSelectedKey via the BP's normal entry path. This is
        //      what got "F1" displayed in earlier builds.
        //   2. Directly write modifier flag byte at FInputChord offset 0x18
        //      on parent's CurrentSelectedKey (in case OnKeySelectedBP only
        //      handles the FKey portion).
        //   3. Also write modifier flag byte on inner OptionKeySelector's
        //      SelectedKey (offset 0x5F0) so a click-to-rebind interaction
        //      sees the current chord state.
        //   4. Call BP_OnUpdated() to refresh the visible KeyGlyph + keyText
        //      using the now-complete CurrentSelectedKey.
        //
        // FInputChord (0x20 bytes):
        //   FKey Key      @ 0x00  (0x18 bytes — FName + TSharedPtr<FKeyDetails>)
        //   uint8 flags   @ 0x18  (bShift=bit0, bCtrl=bit1, bAlt=bit2, bCmd=bit3)
        void applyDefaultChordToSelector(UObject* selector, uint8_t vk, bool withShift)
        {
            static int s_entryCount = 0;
            if (s_entryCount < 6) {
                VLOG(STR("[SettingsUI] applyDefaultChord ENTRY sel={:p} vk=0x{:02x} shift={}\n"),
                     (void*)selector, vk, withShift ? STR("Y") : STR("N"));
                ++s_entryCount;
            }
            if (!selector || !isObjectAlive(selector) || !vk) return;

            std::wstring fkeyName = vkToFKeyName(vk);
            if (fkeyName.empty()) return;

            // Build the FInputChord locally (0x20 bytes).
            uint8_t chord[0x20];
            std::memset(chord, 0, 0x20);
            RC::Unreal::FName keyFName(fkeyName.c_str(), RC::Unreal::FNAME_Add);
            std::memcpy(chord + 0, &keyFName, sizeof(RC::Unreal::FName));
            uint8_t flags = 0;
            if (withShift) flags |= 0x01;
            chord[0x18] = flags;

            // (1) OnKeySelectedBP — the only entry point that successfully
            //     populates the FKey portion (visible as "F1" in earlier
            //     builds). ConfirmSelectedKey is a no-op for our purposes.
            if (auto* fn = selector->GetFunctionByNameInChain(STR("OnKeySelectedBP")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                auto* p = findParam(fn, STR("SelectedKey"));
                if (p)
                {
                    std::memcpy(buf.data() + p->GetOffset_Internal(), chord, 0x20);
                    safeProcessEvent(selector, fn, buf.data());
                }
            }

            // (2) Diagnostic confirmed OnKeySelectedBP doesn't write the
            //     FKey when the source chord's KeyDetails TSharedPtr is
            //     null. Write the full FInputChord directly to both
            //     storage sites so the BP's render path finds the chord:
            //       - parent's CurrentSelectedKey (UMorSettingsKeySelector
            //         offset 0x2E8)
            //       - inner OptionKeySelector's SelectedKey (UInputKeySelector
            //         offset 0x5F0) — likely the source-of-truth that
            //         BP_OnUpdated reads for KeyGlyph rendering.
            if (auto* dst = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                std::memcpy(dst, chord, 0x20);
            if (auto* innerPtr = selector->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionKeySelector")))
            {
                if (UObject* inner = *innerPtr)
                {
                    if (auto* sk = inner->GetValuePtrByPropertyNameInChain<uint8_t>(STR("SelectedKey")))
                        std::memcpy(sk, chord, 0x20);
                }
            }

            // (3) Allow modifier-only keys + clear EscapeKeys (direct
            //     property write only — the UFunction call was destabilising
            //     the rebind machinery).
            if (auto* innerPtr = selector->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionKeySelector")))
            {
                if (UObject* inner = *innerPtr)
                {
                    if (auto* fn = inner->GetFunctionByNameInChain(STR("SetAllowModifierKeys")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* p = findParam(fn, STR("bInAllowModifierKeys"));
                        if (!p) p = findParam(fn, STR("AllowModifierKeys"));
                        if (p) { b[p->GetOffset_Internal()] = 1; safeProcessEvent(inner, fn, b.data()); }
                    }
                    if (auto* esc = inner->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("EscapeKeys")))
                        esc->SetNum(0);
                }
            }

            // (4) Trigger BP_OnUpdated so the BP renders the FKey portion
            //     (KeyGlyph image for "F1"). The BP doesn't render modifier
            //     prefixes itself, so we override below.
            if (auto* fn = selector->GetFunctionByNameInChain(STR("BP_OnUpdated")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                safeProcessEvent(selector, fn, buf.data());
            }

            // Diagnostic: read back CurrentSelectedKey to see what stuck.
            static int s_diagCount = 0;
            if (s_diagCount < 4)
            {
                if (auto* dst = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                {
                    FName* readKey = reinterpret_cast<FName*>(dst);
                    std::wstring keyStr;
                    try { keyStr = readKey->ToString(); } catch (...) {}
                    VLOG(STR("[SettingsUI] applyChord vk=0x{:02x}({}) fname='{}' shift={} -> readback Key='{}' flags=0x{:02x}\n"),
                         vk, fkeyName.c_str(), fkeyName.c_str(),
                         withShift ? STR("Y") : STR("N"),
                         keyStr.c_str(), dst[0x18]);
                }
                ++s_diagCount;
            }

            // Modifier-prefix rendering ("L-SHIFT + F1") deferred —
            // earlier attempts (writing keyText, hiding KeyGlyph) broke
            // the inner UInputKeySelector's selecting-state machine and
            // caused click-to-rebind lockups + text-layer artifacts.
            //
            // SET rows currently render the same as USE rows (F-key glyph
            // only). Will revisit once chord persistence (CP2) is wired,
            // using a tick-driven reapply that respects
            // OptionKeySelector.GetIsSelectingKey() so we never fight the
            // BP during user interaction.
        }

        // Append a widget to a UScrollBox via its AddChild UFunction.
        void addToScrollBox(UObject* scrollBox, UObject* child)
        {
            if (!scrollBox || !child) return;
            auto* fn = scrollBox->GetFunctionByNameInChain(STR("AddChild"));
            if (!fn) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            auto* p = findParam(fn, STR("Content"));
            if (!p) p = findParam(fn, STR("Widget"));
            if (!p) return;
            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = child;
            safeProcessEvent(scrollBox, fn, b.data());
        }

        // Inject the new sections + rows into a live WBP_EditMappingTab.
        // Idempotent — keyed by the EditMappingTab instance pointer.
        void injectModKeybindRows(UObject* editMapTab)
        {
            if (!editMapTab || !isObjectAlive(editMapTab)) return;
            UObject* alreadyInjected = m_keymapInjectedFor.Get();
            if (alreadyInjected == editMapTab) return;

            if (!ensureSettingsKeyClassesCached(editMapTab))
            {
                VLOG(STR("[SettingsUI] keybind row inject — UClasses not yet resolvable, deferring\n"));
                return;
            }

            auto* sbPtr = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("ScrollBox"));
            UObject* scrollBox = sbPtr ? *sbPtr : nullptr;
            if (!scrollBox || !isObjectAlive(scrollBox))
            {
                VLOG(STR("[SettingsUI] keybind row inject — ScrollBox missing\n"));
                return;
            }

            auto* wtPtr = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : editMapTab;

            VLOG(STR("[SettingsUI] injecting mod keymap rows into ScrollBox {:p}\n"),
                 (void*)scrollBox);

            // One-shot deeper diagnostic — dump the native KeySelector's
            // INTERNAL WidgetTree to discover the label widget. The chain
            // dump showed the KeySelector is directly in ScrollBox, so the
            // wide blue bar + action label must be internal to the
            // KeySelector's BP template.
            static bool s_dumpedNativeRow = false;
            if (!s_dumpedNativeRow)
            {
                if (auto* probe = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("Build")))
                {
                    UObject* selector = *probe;
                    if (selector && isObjectAlive(selector))
                    {
                        VLOG(STR("[SettingsUI] native KeySelector 'Build' INTERNAL tree:\n"));
                        // Read its WidgetTree.RootWidget then walk children
                        auto* wtPtr = selector->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                        UObject* wt = wtPtr ? *wtPtr : nullptr;
                        if (wt && isObjectAlive(wt))
                        {
                            auto* rootPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                            UObject* root = rootPtr ? *rootPtr : nullptr;
                            std::function<void(UObject*, int)> walk = [&](UObject* w, int depth) {
                                if (!w || !isObjectAlive(w) || depth > 10) return;
                                std::wstring name; try { name = w->GetName(); } catch (...) {}
                                std::wstring cls = safeClassName(w);
                                std::wstring pad(static_cast<size_t>(depth) * 2, L' ');
                                VLOG(STR("[SettingsUI]    {}{} '{}' = {}\n"),
                                     pad.c_str(), cls.c_str(), name.c_str(), (void*)w);
                                // Walk panel children via Slots (HBox/VBox/Overlay)
                                auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                                if (slots) {
                                    for (int i = 0; i < slots->Num(); ++i) {
                                        UObject* slot = (*slots)[i];
                                        if (!slot) continue;
                                        auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                                        if (cPtr && *cPtr) walk(*cPtr, depth + 1);
                                    }
                                }
                                // Walk single-content (Border / SizeBox)
                                auto* singleC = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                                if (singleC && *singleC && *singleC != w) walk(*singleC, depth + 1);
                            };
                            walk(root, 0);
                        }
                        s_dumpedNativeRow = true;
                    }
                }
            }

            std::vector<UObject*> selectors;  // collected for v6.8.0 CP2 binding

            // Reset per-tab tracking. Each open of the keymap tab gets a
            // fresh injected-row list since the tab widgets are recycled.
            m_keymapRows.clear();

            auto trackRow = [this](UObject* selector, int bindIdx, uint8_t vk,
                                   bool isSetRow, uint8_t modBits, const char* iniKey)
            {
                if (!selector) return;
                KeymapRow r;
                r.selector = FWeakObjectPtr(selector);
                r.bindIdx = bindIdx;
                r.vk = vk;
                r.isSetRow = isSetRow;
                r.modBits = modBits;
                if (iniKey) r.iniKey = iniKey;
                m_keymapRows.push_back(std::move(r));
            };

            // ── Quick Build Menu section (16 rows: 8 use + 8 set) ───────
            if (UObject* hd = spawnSectionHeading(STR("Quick Build Menu")))
                addToScrollBox(scrollBox, hd);
            int invAdded = 0;
            for (int i = 0; i < 8; ++i)
            {
                wchar_t buf[32];
                char    iniKeyUse[32];
                char    iniKeySet[32];
                swprintf_s(buf, L"Quick Build %d", i + 1);
                snprintf(iniKeyUse, sizeof(iniKeyUse), "QuickBuild%d", i + 1);
                UObject* useRow = spawnKeybindRow(outer, buf, nullptr,
                                                  s_bindings[i].key, false, selectors);
                if (useRow) {
                    addToScrollBox(scrollBox, useRow);
                    applyDefaultChordToSelector(useRow, s_bindings[i].key, false);
                    trackRow(useRow, i, s_bindings[i].key, false, 0, iniKeyUse);
                    ++invAdded;
                }

                swprintf_s(buf, L"Quick Build %d Set", i + 1);
                snprintf(iniKeySet, sizeof(iniKeySet), "QuickBuild%dSet", i + 1);
                UObject* setRow = spawnKeybindRow(outer, buf, nullptr,
                                                  s_setBindings[i].vk, (s_setBindings[i].modBits & 0x01) != 0, selectors);
                if (setRow) {
                    addToScrollBox(scrollBox, setRow);
                    applyDefaultChordToSelector(setRow, s_setBindings[i].vk, (s_setBindings[i].modBits & 0x01) != 0);
                    trackRow(setRow, i, s_setBindings[i].vk, true, s_setBindings[i].modBits, iniKeySet);
                    ++invAdded;
                }
            }

            // ── Mod Key Bindings section (9 rows — minus the 6 moved
            //    into native sections in v0.28) ──────────────────────────
            struct ModSpec { const wchar_t* label; int bindIdx; const char* iniKey; };
            // v0.30 — Snap Toggle moved to Building section in native
            // headers below. MOD KEY BINDINGS shrinks to 8 entries.
            static const ModSpec mods[] = {
                { STR("Integrity Check"),    10, "StabilityCheck" },
                { STR("Invisible Dwarf"),    11, "SuperDwarf" },
                { STR("Flying Dwarf"),       -1, "FlyingDwarf" },
                { STR("Target"),             12, "Target" },
                { STR("Duplicate Target"),   -1, "DuplicateTarget" },
                { STR("Remove Single"),      14, "RemoveTarget" },
                { STR("Undo Remove"),        15, "UndoLast" },
                { STR("Remove All"),         16, "RemoveAll" },
            };
            if (UObject* hd = spawnSectionHeading(STR("Mod Key Bindings")))
                addToScrollBox(scrollBox, hd);
            int modAdded = 0;
            for (const auto& m : mods)
            {
                uint8_t vk = (m.bindIdx >= 0 && m.bindIdx < BIND_COUNT) ? s_bindings[m.bindIdx].key : 0;
                UObject* row = spawnKeybindRow(outer, m.label, nullptr, vk, false, selectors);
                if (row) {
                    addToScrollBox(scrollBox, row);
                    applyDefaultChordToSelector(row, vk, false);
                    trackRow(row, m.bindIdx, vk, false, 0, m.iniKey);
                    ++modAdded;
                }
            }

            // ── v0.28 — Move 6 binds into native headings ──────────────
            // Building (ConstructionHeading): Set Rotation, Pitch Rotate, Roll Rotate
            // Inventory and Items (EquipmentHeading): Trash Item, Replenish Item, Remove Attributes
            struct NativeMove { const wchar_t* label; int bindIdx; const char* iniKey;
                                const wchar_t* headingMember; };
            // v0.30 — Snap Toggle added under Building.
            static const NativeMove moves[] = {
                { STR("Set Rotation"),       8,  "Rotation",          STR("ConstructionHeading") },
                { STR("Snap Toggle"),        9,  "SnapToggle",        STR("ConstructionHeading") },
                { STR("Pitch Rotate"),       22, "PitchRotate",       STR("ConstructionHeading") },
                { STR("Roll Rotate"),        23, "RollRotate",        STR("ConstructionHeading") },
                { STR("Trash Item"),         19, "TrashItem",         STR("EquipmentHeading") },
                { STR("Replenish Item"),     20, "ReplenishItem",     STR("EquipmentHeading") },
                { STR("Remove Attributes"),  21, "RemoveAttributes",  STR("EquipmentHeading") },
            };
            int nativeAdded = 0;
            for (const auto& mv : moves)
            {
                uint8_t vk = (mv.bindIdx >= 0 && mv.bindIdx < BIND_COUNT) ? s_bindings[mv.bindIdx].key : 0;
                UObject* row = spawnKeybindRow(outer, mv.label, nullptr, vk, false, selectors);
                if (!row) continue;
                if (insertIntoNativeKeymapSection(scrollBox, editMapTab, mv.headingMember, row))
                {
                    applyDefaultChordToSelector(row, vk, false);
                    trackRow(row, mv.bindIdx, vk, false, 0, mv.iniKey);
                    ++nativeAdded;
                }
                else
                {
                    // Fallback — append to scrollbox so the row isn't lost.
                    addToScrollBox(scrollBox, row);
                    applyDefaultChordToSelector(row, vk, false);
                    trackRow(row, mv.bindIdx, vk, false, 0, mv.iniKey);
                    VLOG(STR("[SettingsUI] CP1 — native-section insert FAILED for '{}', appended to end\n"),
                         mv.label);
                }
            }

            VLOG(STR("[SettingsUI] keymap inject complete: Quick Build={} rows, Mod={} rows, native-moved={}, selectors={}\n"),
                 invAdded, modAdded, nativeAdded, (int)selectors.size());

            m_keymapInjectedFor = FWeakObjectPtr(editMapTab);
        }

        // v0.28 — Insert a row into the keymap ScrollBox right before the
        // next SectionHeading after `headingMemberName` — i.e. at the end
        // of that heading's section. Public-API path: collect the tail
        // starting at insertion index, RemoveChildAt them (highest first
        // for index stability), AddChild the new row, then AddChild the
        // saved tail back in original order.
        bool insertIntoNativeKeymapSection(UObject* scrollBox, UObject* editMapTab,
                                           const wchar_t* headingMemberName, UObject* newRow)
        {
            if (!scrollBox || !editMapTab || !newRow) return false;
            auto* hPtr = editMapTab->GetValuePtrByPropertyNameInChain<UObject*>(headingMemberName);
            UObject* targetHeading = hPtr ? *hPtr : nullptr;
            if (!targetHeading || !isObjectAlive(targetHeading)) return false;

            // Gather scrollBox children in order via GetChildAt UFunction.
            int childCount = 0;
            if (auto* gccFn = scrollBox->GetFunctionByNameInChain(STR("GetChildrenCount")))
            {
                int sz = gccFn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                safeProcessEvent(scrollBox, gccFn, b.data());
                if (auto* rp = findParam(gccFn, STR("ReturnValue")))
                    childCount = *reinterpret_cast<int32_t*>(b.data() + rp->GetOffset_Internal());
            }
            if (childCount <= 0) return false;

            auto getChildAt = [&](int idx) -> UObject* {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("GetChildAt"));
                if (!fn) return nullptr;
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                if (auto* p = findParam(fn, STR("Index")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = idx;
                safeProcessEvent(scrollBox, fn, b.data());
                if (auto* rp = findParam(fn, STR("ReturnValue")))
                    return *reinterpret_cast<UObject**>(b.data() + rp->GetOffset_Internal());
                return nullptr;
            };

            // Find headingIdx; then the next index whose widget is also a
            // SectionHeading (or end). Insert at that boundary.
            int headingIdx = -1;
            for (int i = 0; i < childCount; ++i)
            {
                if (getChildAt(i) == targetHeading) { headingIdx = i; break; }
            }
            if (headingIdx < 0) return false;

            int insertAt = childCount; // default = end of scrollbox
            for (int i = headingIdx + 1; i < childCount; ++i)
            {
                UObject* child = getChildAt(i);
                if (!child) continue;
                if (child->GetClassPrivate() == m_settingsSectionHeadingCls)
                {
                    insertAt = i;
                    break;
                }
            }

            // Save the tail [insertAt, childCount), remove it (high to low),
            // append newRow, then re-add the tail in original order.
            std::vector<UObject*> tail;
            tail.reserve(childCount - insertAt);
            for (int i = insertAt; i < childCount; ++i)
                tail.push_back(getChildAt(i));

            auto removeAt = [&](int idx) {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("RemoveChildAt"));
                if (!fn) return false;
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                if (auto* p = findParam(fn, STR("Index")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = idx;
                safeProcessEvent(scrollBox, fn, b.data());
                return true;
            };
            for (int i = childCount - 1; i >= insertAt; --i)
                removeAt(i);
            addToScrollBox(scrollBox, newRow);
            for (UObject* t : tail)
                if (t && isObjectAlive(t)) addToScrollBox(scrollBox, t);
            return true;
        }

        // Resolve the engine FKey FName for a modifier flag combination.
        // For now we only show one modifier at a time (Shift / Ctrl / Alt
        // are mutually exclusive in our UI; combos like Ctrl+Shift+F1
        // would need a richer renderer).
        UObject* glyphTextureForMods(uint8_t modBits)
        {
            if (modBits & 0x01) return m_glyphTex_Shift;
            if (modBits & 0x02) return m_glyphTex_Ctrl;
            if (modBits & 0x04) return m_glyphTex_Alt;
            return nullptr;
        }

        // Lazy-load input-glyph textures. ONE attempt per session; caches
        // both success and failure to stop tick-loop spam.
        bool m_glyphTexturesAttempted{false};
        UObject* tryLoadGlyph(const wchar_t* keyName)
        {
            // Folder candidates.
            const wchar_t* prefixes[] = {
                STR("/Game/UI/Textures/_Shared/InputGlyphs/Mouse+Keyboard/"),
                STR("/Game/UI/textures/_Shared/InputGlyphs/Mouse+Keyboard/"),
                STR("/Game/UI/Textures/_Shared/InputGlyphs/Mouse_Keyboard/"),
                STR("/Game/UI/textures/_Shared/InputGlyphs/Mouse_Keyboard/"),
                STR("/Game/UI/Textures/_Shared/InputGlyphs/MouseKeyboard/"),
                STR("/Game/UI/textures/_Shared/InputGlyphs/MouseKeyboard/"),
                STR("/Game/UI/Textures/_Shared/InputGlyphs/"),
                STR("/Game/UI/textures/_Shared/InputGlyphs/"),
            };
            // Asset-name candidates per logical key name. Some games name
            // the Ctrl glyph "Ctrl", others "Control" or "LCtrl".
            std::vector<std::wstring> nameVariants;
            nameVariants.push_back(keyName);
            std::wstring kn = keyName;
            if (kn == L"Ctrl") {
                nameVariants.push_back(L"Control");
                nameVariants.push_back(L"LCtrl");
                nameVariants.push_back(L"LeftCtrl");
                nameVariants.push_back(L"LControl");
                nameVariants.push_back(L"L_Ctrl");
            } else if (kn == L"Alt") {
                nameVariants.push_back(L"LAlt");
                nameVariants.push_back(L"LeftAlt");
            } else if (kn == L"Shift") {
                nameVariants.push_back(L"LShift");
                nameVariants.push_back(L"LeftShift");
            }
            for (auto* prefix : prefixes)
            {
                for (const auto& name : nameVariants)
                {
                    std::wstring p = std::wstring(prefix) + L"T_UI_Icon_Input_MKB_" + name + L".T_UI_Icon_Input_MKB_" + name;
                    UObject* tex = nullptr;
                    try { tex = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, p.c_str()); } catch (...) {}
                    if (tex)
                    {
                        VLOG(STR("[SettingsUI] glyph '{}' resolved at '{}'\n"), keyName, p.c_str());
                        return tex;
                    }
                }
            }
            // Synchronous load fallback.
            for (const auto& name : nameVariants)
            {
                std::wstring p = std::wstring(L"/Game/UI/Textures/_Shared/InputGlyphs/Mouse+Keyboard/T_UI_Icon_Input_MKB_") + name + L".T_UI_Icon_Input_MKB_" + name;
                UObject* tex = jw_loadAssetBlocking(p.c_str());
                if (tex) {
                    VLOG(STR("[SettingsUI] glyph '{}' loaded via blocking '{}'\n"), keyName, p.c_str());
                    return tex;
                }
            }
            return nullptr;
        }
        void ensureGlyphTexturesLoaded()
        {
            if (m_glyphTexturesAttempted) return;
            m_glyphTexturesAttempted = true;
            m_glyphTex_Shift = tryLoadGlyph(L"Shift");
            m_glyphTex_Ctrl  = tryLoadGlyph(L"Ctrl");
            m_glyphTex_Alt   = tryLoadGlyph(L"Alt");
            VLOG(STR("[SettingsUI] glyph load complete: Shift={:p} Ctrl={:p} Alt={:p}\n"),
                 (void*)m_glyphTex_Shift, (void*)m_glyphTex_Ctrl, (void*)m_glyphTex_Alt);
            if (!m_imageSetBrushFn)
            {
                auto* imgClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
                if (imgClass)
                {
                    UObject* cdo = imgClass->GetClassDefaultObject();
                    if (cdo) m_imageSetBrushFn = cdo->GetFunctionByNameInChain(STR("SetBrushFromTexture"));
                }
            }
        }

        // Recursive WidgetTree walker — find a child by FName.
        UObject* findWidgetByName(UObject* root, const wchar_t* targetName)
        {
            if (!root || !isObjectAlive(root)) return nullptr;
            std::wstring n; try { n = root->GetName(); } catch (...) {}
            if (n == targetName) return root;
            // Walk panel children via Slots.
            auto* slots = root->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (slots)
            {
                for (int i = 0; i < slots->Num(); ++i)
                {
                    UObject* slot = (*slots)[i];
                    if (!slot) continue;
                    auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                    if (cPtr && *cPtr) {
                        if (UObject* found = findWidgetByName(*cPtr, targetName)) return found;
                    }
                }
            }
            // Walk single-content (Border / SizeBox / ScaleBox).
            auto* singleC = root->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
            if (singleC && *singleC && *singleC != root)
                if (UObject* found = findWidgetByName(*singleC, targetName)) return found;
            return nullptr;
        }

        // Tick-gated reapply of modifier glyph Image on SET rows. Spawns
        // a UImage as sibling of KeyGlyph in Overlay_1 with the modifier
        // texture, positioned to the LEFT. One-shot per row (modGlyphInjected
        // flag) — but we still tick to retry if WidgetTree wasn't ready
        // on first try. Skips during selecting mode.
        // Poll keyboard during selecting mode for ANY key + modifiers.
        // Bypasses the BP's UInputKeySelector entirely — captures every
        // chord including Ctrl+F1, Alt+G, DEL, etc. and applies it directly
        // to our row data + visual.
        bool m_anyKeyDownEdge[256]{};
        void tickCaptureSpecialKeys()
        {
            if (!m_settingsScreenOpen) return;

            // Modifier state right now (held).
            uint8_t modBits = 0;
            if ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0) modBits |= 0x01;
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) modBits |= 0x02;
            if ((GetAsyncKeyState(VK_MENU)    & 0x8000) != 0) modBits |= 0x04;

            // VK -> engine FKey FName candidates we care about.
            struct VKEntry { uint8_t vk; const wchar_t* fname; };
            static const VKEntry vkTable[] = {
                {VK_F1,L"F1"},{VK_F2,L"F2"},{VK_F3,L"F3"},{VK_F4,L"F4"},
                {VK_F5,L"F5"},{VK_F6,L"F6"},{VK_F7,L"F7"},{VK_F8,L"F8"},
                {VK_F9,L"F9"},{VK_F10,L"F10"},{VK_F11,L"F11"},{VK_F12,L"F12"},
                {VK_DELETE,L"Delete"},{VK_INSERT,L"Insert"},
                {VK_HOME,L"Home"},{VK_END,L"End"},
                {VK_PRIOR,L"PageUp"},{VK_NEXT,L"PageDown"},
                {VK_LEFT,L"Left"},{VK_RIGHT,L"Right"},{VK_UP,L"Up"},{VK_DOWN,L"Down"},
                {VK_NUMPAD0,L"NumPadZero"},{VK_NUMPAD1,L"NumPadOne"},
                {VK_NUMPAD2,L"NumPadTwo"},{VK_NUMPAD3,L"NumPadThree"},
                {VK_NUMPAD4,L"NumPadFour"},{VK_NUMPAD5,L"NumPadFive"},
                {VK_NUMPAD6,L"NumPadSix"},{VK_NUMPAD7,L"NumPadSeven"},
                {VK_NUMPAD8,L"NumPadEight"},{VK_NUMPAD9,L"NumPadNine"},
                {VK_OEM_COMMA,L"Comma"},{VK_OEM_PERIOD,L"Period"},
                {VK_OEM_4,L"LeftBracket"},{VK_OEM_6,L"RightBracket"},
                {VK_OEM_5,L"Backslash"},{VK_OEM_2,L"Slash"},
                {VK_DIVIDE,L"Divide"},{VK_MULTIPLY,L"Multiply"},
                {VK_ADD,L"Add"},{VK_SUBTRACT,L"Subtract"},
            };
            // Add A..Z and 0..9 dynamically below.

            for (auto& r : m_keymapRows)
            {
                UObject* sel = r.selector.Get();
                if (!sel || !isObjectAlive(sel)) continue;
                auto* innerPtr = sel->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionKeySelector"));
                UObject* inner = innerPtr ? *innerPtr : nullptr;
                if (!inner || !isObjectAlive(inner)) continue;
                bool selecting = false;
                if (auto* fn = inner->GetFunctionByNameInChain(STR("GetIsSelectingKey")))
                {
                    std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                    safeProcessEvent(inner, fn, buf.data());
                    selecting = buf.size() >= 1 && buf[0] != 0;
                }
                if (!selecting) continue;

                auto applyCapture = [&](uint8_t vk, const std::wstring& fname) {
                    uint8_t chord[0x20]{};
                    RC::Unreal::FName keyFName(fname.c_str(), RC::Unreal::FNAME_Add);
                    std::memcpy(chord, &keyFName, sizeof(RC::Unreal::FName));
                    chord[0x18] = modBits;

                    // Write to BOTH parent CurrentSelectedKey and inner SelectedKey
                    // so BP_OnUpdated reads consistent state.
                    if (auto* dst = sel->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                        std::memcpy(dst, chord, 0x20);
                    if (auto* sk = inner->GetValuePtrByPropertyNameInChain<uint8_t>(STR("SelectedKey")))
                        std::memcpy(sk, chord, 0x20);
                    if (auto* fn = sel->GetFunctionByNameInChain(STR("BP_OnUpdated")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        safeProcessEvent(sel, fn, b.data());
                    }
                    // Exit selecting mode by writing bIsSelectingKey=false
                    // directly to the inner widget's property.
                    if (auto* selecting = inner->GetValuePtrByPropertyNameInChain<uint8_t>(STR("bIsSelectingKey")))
                        *selecting = 0;

                    if (r.isSetRow && r.bindIdx >= 0 && r.bindIdx < 8) {
                        s_setBindings[r.bindIdx].vk = vk;
                        s_setBindings[r.bindIdx].modBits = modBits;
                    } else if (!r.isSetRow && r.bindIdx >= 0 && r.bindIdx < BIND_COUNT) {
                        s_bindings[r.bindIdx].key = vk;
                    }
                    r.vk = vk;
                    r.modBits = modBits;
                    VLOG(STR("[SettingsUI] capture: '{}' vk=0x{:02x} mods=0x{:02x} on '{}'\n"),
                         fname.c_str(), vk, modBits,
                         utf8ToWide(r.iniKey).c_str());
                    saveConfig();
                };

                // Edge-detect each table entry.
                for (const auto& e : vkTable)
                {
                    bool down = (GetAsyncKeyState(e.vk) & 0x8000) != 0;
                    if (down && !m_anyKeyDownEdge[e.vk]) {
                        m_anyKeyDownEdge[e.vk] = true;
                        applyCapture(e.vk, e.fname);
                        goto nextRow;
                    }
                    if (!down) m_anyKeyDownEdge[e.vk] = false;
                }
                // A..Z
                for (uint8_t vk = 'A'; vk <= 'Z'; ++vk)
                {
                    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (down && !m_anyKeyDownEdge[vk]) {
                        m_anyKeyDownEdge[vk] = true;
                        wchar_t fbuf[2] = { (wchar_t)vk, 0 };
                        applyCapture(vk, fbuf);
                        goto nextRow;
                    }
                    if (!down) m_anyKeyDownEdge[vk] = false;
                }
                // 0..9 (top-row digits)
                {
                    static const wchar_t* digitNames[] = {
                        L"Zero",L"One",L"Two",L"Three",L"Four",
                        L"Five",L"Six",L"Seven",L"Eight",L"Nine"
                    };
                    for (uint8_t i = 0; i < 10; ++i) {
                        uint8_t vk = '0' + i;
                        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                        if (down && !m_anyKeyDownEdge[vk]) {
                            m_anyKeyDownEdge[vk] = true;
                            applyCapture(vk, digitNames[i]);
                            goto nextRow;
                        }
                        if (!down) m_anyKeyDownEdge[vk] = false;
                    }
                }
                nextRow:;
            }
        }

        // Modifier-glyph rendering for SET rows.
        //
        // Each row has at most one modifier glyph Image. State machine:
        //   - First time: create the Image, add to Overlay_1, cache weak ref
        //   - Each tick: check current chord modBits, swap texture and
        //     visibility accordingly. modBits == 0 → Collapsed (key alone).
        //   - On user rebind: chord may change modifier — same path picks
        //     up the new modBits without recreating the widget.
        //
        // Skipped while inner UInputKeySelector is in selecting mode.
        void tickReapplyModifierPrefixes()
        {
            if (!m_settingsScreenOpen || m_keymapRows.empty()) return;
            ensureGlyphTexturesLoaded();
            if (!m_glyphTex_Shift && !m_glyphTex_Ctrl && !m_glyphTex_Alt)
                return; // textures missing — nothing to render

            for (auto& r : m_keymapRows)
            {
                // Show a modifier glyph whenever the row's chord has any
                // modifier bit set — applies to both SET rows (default
                // Shift+F1) AND USE/Mod rows that the user binds with a
                // modifier (e.g., Ctrl+F9).
                bool needGlyph = (r.modBits != 0);
                UObject* sel = r.selector.Get();
                if (!sel || !isObjectAlive(sel)) continue;

                // While user is rebinding, hide the glyph so the BP's
                // "Press a Key" prompt has a clean view.
                auto* innerPtr = sel->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionKeySelector"));
                UObject* inner = innerPtr ? *innerPtr : nullptr;
                if (inner && isObjectAlive(inner))
                {
                    if (auto* fn = inner->GetFunctionByNameInChain(STR("GetIsSelectingKey")))
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                        safeProcessEvent(inner, fn, buf.data());
                        bool selecting = buf.size() >= 1 && buf[0] != 0;
                        if (selecting) {
                            // Hide existing glyph during select mode.
                            UObject* g = r.modGlyphImg.Get();
                            if (g && isObjectAlive(g)) setWidgetVisibility(g, 1); // Collapsed
                            continue;
                        }
                    }
                }

                UObject* glyph = r.modGlyphImg.Get();

                // Lazy-create glyph: ScaleBox > Image, mirroring the
                // native KeyGlyph layout. Capture the native ScaleBox_1's
                // Stretch byte AND KeyGlyph.Brush.ImageSize so our glyph
                // renders identically (Pattern A from ue4-ui-duplication).
                if (!glyph)
                {
                    auto* wtPtr = sel->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                    UObject* wt = wtPtr ? *wtPtr : nullptr;
                    if (!wt || !isObjectAlive(wt)) continue;
                    auto* rootPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    UObject* root = rootPtr ? *rootPtr : nullptr;
                    if (!root) continue;
                    UObject* overlay1 = findWidgetByName(root, STR("Overlay_1"));
                    if (!overlay1) continue;

                    // Native KeyGlyph + ScaleBox_1 — source of truth for
                    // dimensions and stretch behavior.
                    UObject* nativeKeyGlyph = nullptr;
                    if (auto* p = sel->GetValuePtrByPropertyNameInChain<UObject*>(STR("KeyGlyph")))
                        nativeKeyGlyph = *p;
                    UObject* nativeScaleBox = findWidgetByName(root, STR("ScaleBox_1"));

                    auto* imgClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
                    auto* sbClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ScaleBox"));
                    if (!imgClass || !sbClass) continue;

                    FStaticConstructObjectParameters cpI(imgClass, wt);
                    glyph = UObjectGlobals::StaticConstructObject(cpI);
                    if (!glyph) continue;
                    FStaticConstructObjectParameters cpS(sbClass, wt);
                    UObject* sbox = UObjectGlobals::StaticConstructObject(cpS);
                    if (!sbox) continue;

                    // Capture & replicate ScaleBox_1's Stretch byte. Falls
                    // back to ScaleToFit (1) if reflection access fails.
                    uint8_t nativeStretch = 1;
                    if (nativeScaleBox)
                    {
                        if (auto* sp = nativeScaleBox->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Stretch")))
                            nativeStretch = *sp;
                    }
                    if (auto* fn = sbox->GetFunctionByNameInChain(STR("SetStretch")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* p = findParam(fn, STR("InStretch"));
                        if (p) { b[p->GetOffset_Internal()] = nativeStretch; safeProcessEvent(sbox, fn, b.data()); }
                    }
                    // Apply ScaleBox.Content = our Image
                    if (auto* fn = sbox->GetFunctionByNameInChain(STR("SetContent")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* p = findParam(fn, STR("InContent"));
                        if (!p) p = findParam(fn, STR("Content"));
                        if (p) {
                            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = glyph;
                            safeProcessEvent(sbox, fn, b.data());
                        }
                    }

                    UObject* slot = addToOverlay(overlay1, sbox);
                    if (slot)
                    {
                        umgSetHAlign(slot, 2); // Center
                        umgSetVAlign(slot, 2); // Center
                        if (auto* fn = slot->GetFunctionByNameInChain(STR("SetPadding")))
                        {
                            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                            auto* p = findParam(fn, STR("InPadding"));
                            if (p) {
                                float* m = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                                // Larger right padding shifts the centered
                                // ScaleBox further left so it doesn't overlap
                                // the KeyGlyph + leaves a visible gap.
                                m[0] = 0.0f; m[1] = 0.0f; m[2] = 120.0f; m[3] = 0.0f;
                                safeProcessEvent(slot, fn, b.data());
                            }
                        }
                    }

                    // Cache native KeyGlyph for size lookup later.
                    r.modGlyphImg = FWeakObjectPtr(glyph);
                    r.nativeKeyGlyphForSize = FWeakObjectPtr(nativeKeyGlyph);
                    VLOG(STR("[SettingsUI] mod glyph (ScaleBox stretch={} + Image) created on selector {:p}\n"),
                         nativeStretch, (void*)sel);
                }

                // Update glyph texture + visibility based on current modBits.
                // Only re-apply brush when modBits actually changed — otherwise
                // jw_setImageBrush logs spam every frame.
                UObject* tex = glyphTextureForMods(r.modBits);
                if (r.lastAppliedModBits == r.modBits)
                {
                    // No-op fast path: brush already correct, just refresh
                    // visibility (cheap — no log).
                    setWidgetVisibility(glyph, needGlyph ? 0 : 1);
                    continue;
                }
                r.lastAppliedModBits = r.modBits;
                if (needGlyph && tex && m_imageSetBrushFn)
                {
                    // Match F1 KeyGlyph's HEIGHT exactly (so SHIFT/CTRL/ALT
                    // appear the same size as F1). Width is computed via
                    // texture native aspect ratio so each modifier keeps
                    // its proper proportions.
                    float targetH = 0.0f;
                    UObject* native = r.nativeKeyGlyphForSize.Get();
                    if (native && isObjectAlive(native))
                    {
                        ensureBrushOffset(native);
                        if (s_off_brush >= 0)
                        {
                            uint8_t* base = reinterpret_cast<uint8_t*>(native);
                            targetH = *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY());
                        }
                    }
                    if (targetH <= 0 || targetH > 256) targetH = 28.0f;
                    // Scale up ~20% so modifier glyphs visually match the
                    // perceived size of F-key glyphs (the BP renders F1
                    // larger than its brush.ImageSize via ScaleBox stretch).
                    targetH *= 1.2f;

                    // Read modifier texture's native dims for aspect.
                    float w = 0.0f, h = 0.0f;
                    auto callIntReturn = [&](const wchar_t* fname) -> int32_t {
                        UFunction* fn = tex->GetFunctionByNameInChain(fname);
                        if (!fn) return 0;
                        int sz = fn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        safeProcessEvent(tex, fn, b.data());
                        auto* p = findParam(fn, STR("ReturnValue"));
                        if (p && p->GetOffset_Internal() + (int)sizeof(int32_t) <= sz)
                            return *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal());
                        return 0;
                    };
                    int32_t tw = callIntReturn(STR("GetSizeX"));
                    int32_t th = callIntReturn(STR("GetSizeY"));
                    if (tw > 0 && th > 0)
                    {
                        float scale = targetH / (float)th;
                        w = (float)tw * scale;
                        h = targetH;
                    } else {
                        w = targetH * 1.6f; h = targetH;
                    }
                    jw_setImageBrush(glyph, tex, m_imageSetBrushFn, w, h, STR("ModGlyph"));
                    setWidgetVisibility(glyph, 0); // Visible
                }
                else
                {
                    setWidgetVisibility(glyph, 1); // Collapsed
                }
            }
        }

        // Called from a ProcessEvent post-hook on OnKeySelectedBP. If the
        // selector is one of ours, persist the new chord to MoriaCppMod.ini.
        void onModSelectorRebound(UObject* selector)
        {
            if (!selector) return;
            for (auto& r : m_keymapRows)
            {
                if (r.selector.Get() != selector) continue;
                // Read CurrentSelectedKey (FInputChord at offset 0x2E8).
                auto* chordPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey"));
                if (!chordPtr) return;
                FName* keyName = reinterpret_cast<FName*>(chordPtr);
                std::wstring keyStr;
                try { keyStr = keyName->ToString(); } catch (...) {}
                bool isShift = (chordPtr[0x18] & 0x01) != 0;
                VLOG(STR("[SettingsUI] rebind '{}' -> Key='{}' Shift={}\n"),
                     utf8ToWide(r.iniKey).c_str(),
                     keyStr.c_str(), isShift ? STR("Y") : STR("N"));
                // Map FName back to VK (for s_bindings storage).
                uint8_t vk = fkeyNameToVK(keyStr);

                // Capture modifier flags. Try in order:
                //   1. Inner UInputKeySelector.SelectedKey (offset 0x5F0)
                //      — populated by Slate at capture time, includes
                //      bShift/bCtrl/bAlt/bCmd correctly.
                //   2. Parent CurrentSelectedKey (offset 0x2E8) — backup.
                //   3. GetAsyncKeyState fallback (in case modifiers were
                //      released by the time we get here).
                uint8_t modBits = 0;
                uint8_t innerFlagByte = 0xFF, parentFlagByte = chordPtr[0x18];
                bool gakShift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
                bool gakCtrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool gakAlt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
                {
                    auto* innerPtr = selector->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionKeySelector"));
                    if (innerPtr && *innerPtr)
                    {
                        auto* skBytes = (*innerPtr)->GetValuePtrByPropertyNameInChain<uint8_t>(STR("SelectedKey"));
                        if (skBytes)
                        {
                            innerFlagByte = skBytes[0x18];
                            if (innerFlagByte & 0x01) modBits |= 0x01;
                            if (innerFlagByte & 0x02) modBits |= 0x02;
                            if (innerFlagByte & 0x04) modBits |= 0x04;
                        }
                    }
                }
                if (modBits == 0)
                {
                    if (parentFlagByte & 0x01) modBits |= 0x01;
                    if (parentFlagByte & 0x02) modBits |= 0x02;
                    if (parentFlagByte & 0x04) modBits |= 0x04;
                }
                if (modBits == 0)
                {
                    if (gakShift) modBits |= 0x01;
                    if (gakCtrl)  modBits |= 0x02;
                    if (gakAlt)   modBits |= 0x04;
                }
                VLOG(STR("[SettingsUI] modBits diag: inner=0x{:02x} parent=0x{:02x} gak={}{}{} -> picked=0x{:02x}\n"),
                     innerFlagByte, parentFlagByte,
                     gakShift ? STR("S") : STR("-"),
                     gakCtrl  ? STR("C") : STR("-"),
                     gakAlt   ? STR("A") : STR("-"),
                     modBits);

                if (vk)
                {
                    if (r.isSetRow && r.bindIdx >= 0 && r.bindIdx < 8)
                    {
                        // SET rows write to s_setBindings[] — separate from
                        // the USE row's s_bindings index.
                        s_setBindings[r.bindIdx].vk = vk;
                        s_setBindings[r.bindIdx].modBits = modBits;
                    }
                    else if (!r.isSetRow && r.bindIdx >= 0 && r.bindIdx < BIND_COUNT)
                    {
                        s_bindings[r.bindIdx].key = vk;
                    }
                    r.vk = vk;
                    r.modBits = modBits;
                    VLOG(STR("[SettingsUI] rebind '{}' captured chord vk=0x{:02x} mods=0x{:02x}\n"),
                         utf8ToWide(r.iniKey).c_str(), vk, modBits);
                }
                saveConfig();
                return;
            }
        }

        // Inverse of vkToFKeyName. Map the engine's FKey FName back to the
        // Windows VK code so we can store in s_bindings[].key.
        uint8_t fkeyNameToVK(const std::wstring& fname)
        {
            // F1..F12
            if (fname.size() >= 2 && fname[0] == L'F')
            {
                wchar_t* end = nullptr;
                long n = wcstol(fname.c_str() + 1, &end, 10);
                if (n >= 1 && n <= 12 && end && *end == 0)
                    return static_cast<uint8_t>(0x70 + n - 1);
            }
            if (fname == L"NumPadZero")  return VK_NUMPAD0;
            if (fname == L"NumPadOne")   return VK_NUMPAD1;
            if (fname == L"NumPadTwo")   return VK_NUMPAD2;
            if (fname == L"NumPadThree") return VK_NUMPAD3;
            if (fname == L"NumPadFour")  return VK_NUMPAD4;
            if (fname == L"NumPadFive")  return VK_NUMPAD5;
            if (fname == L"NumPadSix")   return VK_NUMPAD6;
            if (fname == L"NumPadSeven") return VK_NUMPAD7;
            if (fname == L"NumPadEight") return VK_NUMPAD8;
            if (fname == L"NumPadNine")  return VK_NUMPAD9;
            if (fname == L"Multiply")    return VK_MULTIPLY;
            if (fname == L"Add")         return VK_ADD;
            if (fname == L"Subtract")    return VK_SUBTRACT;
            if (fname == L"Divide")      return VK_DIVIDE;
            if (fname == L"Decimal")     return VK_DECIMAL;
            if (fname == L"Delete")      return VK_DELETE;
            if (fname == L"Insert")      return VK_INSERT;
            if (fname == L"Home")        return VK_HOME;
            if (fname == L"End")         return VK_END;
            if (fname == L"PageUp")      return VK_PRIOR;
            if (fname == L"PageDown")    return VK_NEXT;
            if (fname == L"Left")        return VK_LEFT;
            if (fname == L"Right")       return VK_RIGHT;
            if (fname == L"Up")          return VK_UP;
            if (fname == L"Down")        return VK_DOWN;
            if (fname == L"SpaceBar")    return VK_SPACE;
            if (fname == L"Enter")       return VK_RETURN;
            if (fname == L"Tab")         return VK_TAB;
            if (fname == L"Escape")      return VK_ESCAPE;
            if (fname == L"BackSpace")   return VK_BACK;
            if (fname == L"LeftBracket") return VK_OEM_4;
            if (fname == L"RightBracket")return VK_OEM_6;
            if (fname == L"Backslash")   return VK_OEM_5;
            if (fname == L"Comma")       return VK_OEM_COMMA;
            if (fname == L"Period")      return VK_OEM_PERIOD;
            if (fname == L"Semicolon")   return VK_OEM_1;
            if (fname == L"Slash")       return VK_OEM_2;
            if (fname == L"Apostrophe")  return VK_OEM_7;
            if (fname == L"Tilde")       return VK_OEM_3;
            if (fname == L"Hyphen")      return VK_OEM_MINUS;
            if (fname == L"Equals")      return VK_OEM_PLUS;
            if (fname == L"Zero")  return '0';
            if (fname == L"One")   return '1';
            if (fname == L"Two")   return '2';
            if (fname == L"Three") return '3';
            if (fname == L"Four")  return '4';
            if (fname == L"Five")  return '5';
            if (fname == L"Six")   return '6';
            if (fname == L"Seven") return '7';
            if (fname == L"Eight") return '8';
            if (fname == L"Nine")  return '9';
            if (fname.size() == 1 && fname[0] >= L'A' && fname[0] <= L'Z')
                return static_cast<uint8_t>(fname[0]);
            return 0;
        }

        // Called from the main tick when a settings page is shown — runs the
        // same kind of "apply modifications" path as the JoinWorld flow.
        void applyModificationsToSettings()
        {
            UObject* screen = m_nativeSettingsScreen.Get();
            if (!screen || !isObjectAlive(screen)) return;
            // The keymap tab member is named WBP_EditMappingTab on
            // WBP_SettingsScreen_C.
            auto* editMapPtr = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_EditMappingTab"));
            UObject* editMap = editMapPtr ? *editMapPtr : nullptr;
            if (editMap && isObjectAlive(editMap))
            {
                injectModKeybindRows(editMap);
            }
        }

        // ────────────────────────────────────────────────────────────────
        // CP4 — "Mod Game Options" section in Gameplay tab
        // ────────────────────────────────────────────────────────────────
        // Each entry maps a spawned WBP_FrontEndButton to a kind code so a
        // ProcessEvent post-hook on OnMenuButtonClicked can dispatch.
        enum class GameOptKind { None, Rename, Save, Unlock, ReadAll, ClearBuffs,
                                  ToggleNoCollision, TogglePeace };
        struct GameOptButton {
            FWeakObjectPtr widget;
            GameOptKind    kind;
        };
        std::vector<GameOptButton> m_gameOptButtons;
        UClass* m_frontEndButtonCls{nullptr};
        FWeakObjectPtr m_gameplayTabInjectedFor;

        bool ensureFrontEndButtonClassCached(UObject* gameplayTab)
        {
            if (m_frontEndButtonCls) return true;
            // BlockListButton is a known WBP_FrontEndButton_C member on the tab.
            auto* btnPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlockListButton"));
            UObject* btn = btnPtr ? *btnPtr : nullptr;
            if (btn && isObjectAlive(btn))
            {
                m_frontEndButtonCls = static_cast<UClass*>(btn->GetClassPrivate());
                return m_frontEndButtonCls != nullptr;
            }
            return false;
        }

        UObject* spawnGameOptButton(UObject* outer, const wchar_t* labelText, GameOptKind kind)
        {
            if (!m_frontEndButtonCls) return nullptr;
            UObject* btn = jw_createGameWidget(m_frontEndButtonCls);
            if (!btn) return nullptr;
            // Set ButtonLabel FText (PreConstruct copies to ButtonText TextBlock).
            if (auto* lblPtr = btn->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel")))
                *lblPtr = FText(labelText);
            // Belt-and-suspenders: also UpdateTextLabel UFunction call.
            if (auto* fn = btn->GetFunctionByNameInChain(STR("UpdateTextLabel")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                auto* p = findParam(fn, STR("Text"));
                if (p) {
                    FText t(labelText);
                    std::memcpy(b.data() + p->GetOffset_Internal(), &t, sizeof(FText));
                    safeProcessEvent(btn, fn, b.data());
                }
            }
            GameOptButton g; g.widget = FWeakObjectPtr(btn); g.kind = kind;
            m_gameOptButtons.push_back(g);
            return btn;
        }

        void appendToVerticalBox(UObject* vbox, UObject* child)
        {
            if (!vbox || !child) return;
            auto* fn = vbox->GetFunctionByNameInChain(STR("AddChild"));
            if (!fn) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            auto* p = findParam(fn, STR("Content"));
            if (!p) p = findParam(fn, STR("Widget"));
            if (!p) return;
            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = child;
            safeProcessEvent(vbox, fn, b.data());
        }

        // Capture every native child of ParentPanel one time, so that
        // when we later hide them for the Cheats context we know the
        // original visibility to restore. Called on first sight of a
        // gameplayTab; idempotent within m_gameplayTabInjectedFor.
        void captureGameplayNativeChildren(UObject* parentPanel)
        {
            if (!parentPanel || !isObjectAlive(parentPanel)) return;
            m_gameplayNativeChildren.clear();
            auto* slots = parentPanel->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (!slots) {
                VLOG(STR("[SettingsUI] CP5-DBG — Slots property not found on ParentPanel cls={}\n"),
                     safeClassName(parentPanel).c_str());
                return;
            }
            VLOG(STR("[SettingsUI] CP5-DBG — captureGameplayNativeChildren: ParentPanel has {} slots\n"),
                 slots->Num());
            for (int i = 0; i < slots->Num(); ++i)
            {
                UObject* slot = (*slots)[i];
                if (!slot) continue;
                auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                UObject* child = (cPtr && *cPtr) ? *cPtr : nullptr;
                if (!child || !isObjectAlive(child)) continue;
                OriginalChild oc;
                oc.widget = FWeakObjectPtr(child);
                auto* visPtr = child->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                oc.origVis = visPtr ? *visPtr : 0;
                m_gameplayNativeChildren.push_back(oc);
                VLOG(STR("[SettingsUI] CP5-DBG —   child[{}]: cls={} name={} origVis={}\n"),
                     i, safeClassName(child).c_str(),
                     [&]{ try { return child->GetName(); } catch(...) { return std::wstring(L"?"); } }().c_str(),
                     (int)oc.origVis);
            }
        }

        // Direct visibility byte write — bypasses any BP-side
        // SetVisibility override. Some game widgets fight UFunction-based
        // visibility changes via Tick reapply.
        void writeVisibilityDirect(UObject* w, uint8_t vis)
        {
            if (!w || !isObjectAlive(w)) return;
            auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
            if (visPtr) *visPtr = vis;
            // Also call SetVisibility UFunction so SlateVisibility cache updates.
            setWidgetVisibility(w, vis);
        }

        // Toggle visibility of all captured native gameplay children.
        // hide=true → set Collapsed (1). hide=false → restore original.
        void setNativeGameplayChildrenHidden(bool hide)
        {
            int n = 0;
            for (auto& oc : m_gameplayNativeChildren)
            {
                UObject* w = oc.widget.Get();
                if (!w || !isObjectAlive(w)) continue;
                writeVisibilityDirect(w, hide ? 1 : oc.origVis);
                ++n;
            }
            VLOG(STR("[SettingsUI] CP5-DBG — setNativeGameplayChildrenHidden({}): wrote vis to {} widgets\n"),
                 hide ? L"true" : L"false", n);
        }

        // Toggle visibility of a tracked widget set.
        void setWidgetSetVisibility(std::vector<FWeakObjectPtr>& set, bool visible)
        {
            int n = 0;
            for (auto& wp : set)
            {
                UObject* w = wp.Get();
                if (!w || !isObjectAlive(w)) continue;
                writeVisibilityDirect(w, visible ? 0 : 1); // 0=Visible, 1=Collapsed
                ++n;
            }
            VLOG(STR("[SettingsUI] CP5-DBG — setWidgetSetVisibility({}, size={}): wrote {} widgets\n"),
                 visible ? L"true" : L"false", (int)set.size(), n);
        }

        // Walk Outer chain from a tab widget to find the owning
        // WBP_SettingsScreen_C. Used to distinguish the regular Gameplay
        // tab (=== settingsScreen->WBP_GameplayTab) from a clone instance
        // spawned by the framework for our "Cheats" tabArray entry.
        UObject* findOwningSettingsScreen(UObject* tab)
        {
            UObject* cur = tab;
            for (int i = 0; i < 10 && cur; ++i)
            {
                std::wstring cls = safeClassName(cur);
                if (cls == STR("WBP_SettingsScreen_C")) return cur;
                cur = cur->GetOuterPrivate();
            }
            return nullptr;
        }

        // True if `gameplayTab` is the dedicated clone the framework
        // spawned for our "Cheats" tabArray entry — i.e. it's NOT the
        // SettingsScreen's WBP_GameplayTab member.
        bool isCheatsHostInstance(UObject* gameplayTab)
        {
            UObject* ss = findOwningSettingsScreen(gameplayTab);
            if (!ss) return false;
            auto* gpPtr = ss->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_GameplayTab"));
            UObject* nativeGameplay = gpPtr ? *gpPtr : nullptr;
            return (nativeGameplay && nativeGameplay != gameplayTab);
        }

        void injectModGameOptions(UObject* gameplayTab)
        {
            if (!gameplayTab || !isObjectAlive(gameplayTab)) return;
            if (!ensureFrontEndButtonClassCached(gameplayTab))
            {
                VLOG(STR("[SettingsUI] CP4 — FrontEndButton class not cached, deferring\n"));
                return;
            }
            auto* ppPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("ParentPanel"));
            UObject* parentPanel = ppPtr ? *ppPtr : nullptr;
            if (!parentPanel || !isObjectAlive(parentPanel))
            {
                VLOG(STR("[SettingsUI] CP4 — ParentPanel missing on GameplayTab\n"));
                return;
            }
            auto* wtPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : gameplayTab;

            const bool isCheatsHost = isCheatsHostInstance(gameplayTab);
            VLOG(STR("[SettingsUI] CP5 — injectModGameOptions on instance {:p}, isCheatsHost={}\n"),
                 (void*)gameplayTab, isCheatsHost ? L"YES" : L"no");

            // ─────────────────────────────────────────────────────────
            // CHEATS HOST PATH — this is the framework's clone of
            // WBP_GameplayTab spawned for our "Cheats" tabArray entry.
            // We want it to display ONLY cheats content.
            // ─────────────────────────────────────────────────────────
            if (isCheatsHost)
            {
                if (m_cheatsHostInjectedFor.Get() == gameplayTab) return; // idempotent

                // Hide every native child the BP's Construct event added
                // (sliders, headings, comboboxes — all the Gameplay UI).
                auto* slots = parentPanel->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                int hidCount = 0;
                if (slots)
                {
                    for (int i = 0; i < slots->Num(); ++i)
                    {
                        UObject* slot = (*slots)[i];
                        if (!slot) continue;
                        auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        UObject* child = (cPtr && *cPtr) ? *cPtr : nullptr;
                        if (!child || !isObjectAlive(child)) continue;
                        // Direct byte write + UFunction call.
                        auto* visPtr = child->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                        if (visPtr) *visPtr = 1; // Collapsed
                        setWidgetVisibility(child, 1);
                        // v6.15.0 — m_cheatsHostNativeChildren tracking removed
                        // (vector was write-only; never iterated for restore).
                        ++hidCount;
                    }
                }
                VLOG(STR("[SettingsUI] CP5 — Cheats host: collapsed {} native children\n"), hidCount);

                m_cheatsButtons.clear();

                if (m_settingsSectionHeadingCls)
                {
                    if (UObject* hd = spawnSectionHeading(STR("Cheats")))
                        appendToVerticalBox(parentPanel, hd);
                }

                struct CSpec { const wchar_t* label; CheatKind kind; };
                static const CSpec cheatSpecs[] = {
                    { STR("Unlock All Recipes"),         CheatKind::Unlock },
                    { STR("Mark All Lore Read"),         CheatKind::ReadAll },
                    { STR("Clear All Buffs"),            CheatKind::ClearBuffs },
                    { STR("Toggle Peace Mode"),          CheatKind::TogglePeace },
                    { STR("Toggle No-Collision Flying"), CheatKind::ToggleNoCollision },
                    { STR("Rename Character"),           CheatKind::RenameChar },
                    { STR("Save Game"),                  CheatKind::SaveGame },
                };
                int cheatsAdded = 0;
                for (const auto& s : cheatSpecs)
                {
                    UObject* btn = jw_createGameWidget(m_frontEndButtonCls);
                    if (!btn) continue;
                    if (auto* lblPtr = btn->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel")))
                        *lblPtr = FText(s.label);
                    if (auto* fn = btn->GetFunctionByNameInChain(STR("UpdateTextLabel")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        auto* p = findParam(fn, STR("Text"));
                        if (p) {
                            FText t(s.label);
                            std::memcpy(b.data() + p->GetOffset_Internal(), &t, sizeof(FText));
                            safeProcessEvent(btn, fn, b.data());
                        }
                    }
                    appendToVerticalBox(parentPanel, btn);
                    CheatButton entry; entry.widget = FWeakObjectPtr(btn); entry.kind = s.kind;
                    m_cheatsButtons.push_back(entry);
                    ++cheatsAdded;
                }
                VLOG(STR("[SettingsUI] CP5 — Cheats host: injected {} cheat buttons\n"), cheatsAdded);
                m_cheatsHostInjectedFor = FWeakObjectPtr(gameplayTab);
                return;
            }

            // ─────────────────────────────────────────────────────────
            // REGULAR GAMEPLAY TAB PATH — leave native UI alone, append
            // "Mod Game Options" section at the bottom (legacy CP4).
            // ─────────────────────────────────────────────────────────
            if (m_gameplayTabInjectedFor.Get() == gameplayTab) return; // idempotent

            m_gameOptButtons.clear();

            if (m_settingsSectionHeadingCls)
            {
                if (UObject* hd = spawnSectionHeading(STR("Mod Game Options")))
                    appendToVerticalBox(parentPanel, hd);
            }
            // v0.50 — 5 action buttons (Rename/Save/Unlock/Read All/
            // Clear All Buffs) moved to the in-game pause menu
            // (UI_WBP_EscapeMenu2_C → injectPauseMenuButtons). Only the
            // two toggle buttons remain on the Gameplay tab here.
            struct Spec { const wchar_t* label; GameOptKind kind; };
            static const Spec gameOptSpecs[] = {
                { STR("Toggle No Collision Flying"), GameOptKind::ToggleNoCollision },
                { STR("Toggle Peace Mode"),  GameOptKind::TogglePeace },
            };
            int gameOptAdded = 0;
            for (const auto& s : gameOptSpecs)
            {
                UObject* btn = spawnGameOptButton(outer, s.label, s.kind);
                if (btn) {
                    appendToVerticalBox(parentPanel, btn);
                    ++gameOptAdded;
                }
            }
            VLOG(STR("[SettingsUI] CP4 — injected {} mod-game-option buttons (regular Gameplay tab)\n"),
                 gameOptAdded);
            m_gameplayTabInjectedFor = FWeakObjectPtr(gameplayTab);
        }

        // v0.50 — Insert child into a UVerticalBox at a specific index.
        // Uses InsertChildAt UFunction so we can place mod buttons in the
        // middle of the native button stack (e.g. above LeaveButton).
        // Falls back to AddChild on failure.
        void insertIntoVerticalBox(UObject* vbox, UObject* child, int32_t index)
        {
            if (!vbox || !child) return;
            auto* fn = vbox->GetFunctionByNameInChain(STR("InsertChildAt"));
            if (!fn)
            {
                appendToVerticalBox(vbox, child);
                return;
            }
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            auto* pIdx = findParam(fn, STR("Index"));
            auto* pCtt = findParam(fn, STR("Content"));
            if (!pCtt) pCtt = findParam(fn, STR("Widget"));
            if (!pIdx || !pCtt)
            {
                appendToVerticalBox(vbox, child);
                return;
            }
            *reinterpret_cast<int32_t*>(b.data() + pIdx->GetOffset_Internal()) = index;
            *reinterpret_cast<UObject**>(b.data() + pCtt->GetOffset_Internal()) = child;
            safeProcessEvent(vbox, fn, b.data());
        }

        // v0.50 — find the slot index of a named child widget within a
        // UPanelWidget's Slots array. Used to position our injected
        // buttons relative to known native buttons (e.g. before LeaveButton).
        // Returns -1 if not found.
        int32_t findChildSlotIndex(UObject* panel, UObject* targetChild)
        {
            if (!panel || !targetChild) return -1;
            auto* slots = panel->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (!slots) return -1;
            for (int i = 0; i < slots->Num(); ++i)
            {
                UObject* slot = (*slots)[i];
                if (!slot) continue;
                auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (cPtr && *cPtr == targetChild) return i;
            }
            return -1;
        }

        // v0.50 — Inject the 5 mod action buttons into the pause menu
        // (UI_WBP_EscapeMenu2_C → VerticalBox_0). Buttons reuse the same
        // spawnGameOptButton path as the (former) Gameplay-tab buttons,
        // so click dispatch flows through the existing
        // OnMenuButtonClicked / OnButtonReleasedEvent handlers and
        // m_gameOptButtons matching by Outer-chain.
        void injectPauseMenuButtons(UObject* escapeMenu)
        {
            if (!escapeMenu || !isObjectAlive(escapeMenu)) return;
            // Idempotent — same instance only injected once.
            if (m_pauseMenuInjectedFor.Get() == escapeMenu) return;

            if (!ensureFrontEndButtonClassCached(escapeMenu))
            {
                // Probe the EscapeMenu's own ResumeButton for the BP class.
                auto* rbPtr = escapeMenu->GetValuePtrByPropertyNameInChain<UObject*>(STR("ResumeButton"));
                UObject* rb = rbPtr ? *rbPtr : nullptr;
                if (rb && isObjectAlive(rb))
                    m_frontEndButtonCls = static_cast<UClass*>(rb->GetClassPrivate());
                if (!m_frontEndButtonCls)
                {
                    VLOG(STR("[SettingsUI] PauseMenu — FrontEndButton class not cached, deferring\n"));
                    return;
                }
            }

            auto* vbPtr = escapeMenu->GetValuePtrByPropertyNameInChain<UObject*>(STR("VerticalBox_0"));
            UObject* vbox = vbPtr ? *vbPtr : nullptr;
            if (!vbox || !isObjectAlive(vbox))
            {
                VLOG(STR("[SettingsUI] PauseMenu — VerticalBox_0 missing on EscapeMenu2\n"));
                return;
            }

            auto* wtPtr = escapeMenu->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : escapeMenu;

            // v0.56 — ALL CAPS labels for uniformity with native buttons
            // ("RETURN TO GAME", "SETTINGS", "FREE CAM"). Click dispatch
            // matches by widget pointer, not by label, so casing never
            // affects which handler fires.
            struct Spec { const wchar_t* label; GameOptKind kind; };
            static const Spec specs[] = {
                { STR("RENAME CHARACTER"), GameOptKind::Rename     },
                { STR("SAVE GAME"),        GameOptKind::Save       },
                { STR("UNLOCK RECIPES"),   GameOptKind::Unlock     },
                { STR("READ ALL LORE"),    GameOptKind::ReadAll    },
                { STR("CLEAR ALL BUFFS"),  GameOptKind::ClearBuffs },
            };

            // 1) Snapshot the ORIGINAL native order so we can rebuild
            //    later, with mod buttons inserted, spacers between, and
            //    Leave/Quit at the bottom.
            std::vector<UObject*> nativeOrder;
            {
                auto* slots = vbox->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slots) {
                    for (int i = 0; i < slots->Num(); ++i) {
                        UObject* slot = (*slots)[i];
                        if (!slot) continue;
                        auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        UObject* child = (cPtr && *cPtr) ? *cPtr : nullptr;
                        if (!child || !isObjectAlive(child)) continue;
                        nativeOrder.push_back(child);
                    }
                }
            }
            VLOG(STR("[SettingsUI] PauseMenu — captured {} native children\n"),
                 (int)nativeOrder.size());

            // 2) Spawn 5 mod buttons. Force SmallText=true so they render
            //    at the same compact size as native SETTINGS / DIFFICULTY /
            //    FREE CAM (only RETURN TO GAME uses the larger font).
            std::vector<UObject*> modButtons;
            int added = 0;
            for (const auto& s : specs)
            {
                UObject* btn = spawnGameOptButton(outer, s.label, s.kind);
                if (!btn) continue;
                if (auto* smPtr = btn->GetValuePtrByPropertyNameInChain<bool>(STR("SmallText")))
                    *smPtr = true; // v0.56 — match SETTINGS/FREE CAM size
                modButtons.push_back(btn);
                ++added;
            }
            VLOG(STR("[SettingsUI] PauseMenu — spawned {} mod action buttons\n"), added);

            // 3) Compute final order. Pull LeaveButton + QuitToDesktopButton
            //    out of nativeOrder so they go to the very end. Insert mod
            //    buttons after the rest of the natives.
            UObject* leaveBtn = nullptr;
            UObject* quitBtn  = nullptr;
            {
                auto* lbPtr2 = escapeMenu->GetValuePtrByPropertyNameInChain<UObject*>(STR("LeaveButton"));
                auto* qbPtr  = escapeMenu->GetValuePtrByPropertyNameInChain<UObject*>(STR("QuitToDesktopButton"));
                leaveBtn = lbPtr2 ? *lbPtr2 : nullptr;
                quitBtn  = qbPtr  ? *qbPtr  : nullptr;
            }
            std::vector<UObject*> finalOrder;
            for (UObject* c : nativeOrder)
                if (c != leaveBtn && c != quitBtn) finalOrder.push_back(c);
            for (UObject* m : modButtons) finalOrder.push_back(m);
            if (leaveBtn) finalOrder.push_back(leaveBtn);
            if (quitBtn)  finalOrder.push_back(quitBtn);

            // 4) ClearChildren on the VerticalBox via UFUNCTION (ClearChildren
            //    IS reflected; InsertChildAt is NOT — that's why prior spacer
            //    inserts silently fell back to AddChild and ended up at the
            //    bottom). The widgets themselves stay alive — only the slots
            //    are released.
            if (auto* fnClr = vbox->GetFunctionByNameInChain(STR("ClearChildren")))
            {
                std::vector<uint8_t> b(fnClr->GetParmsSize(), 0);
                safeProcessEvent(vbox, fnClr, b.data());
            }

            // 5) v0.56 — Use USizeBox with HeightOverride for spacers.
            //    USpacer's `Size` (FVector2D) write didn't render reliably
            //    in this game; SizeBox + jw_setSizeBoxOverride is the
            //    proven path used elsewhere in the codebase.
            UClass* spacerCls = nullptr;
            try {
                spacerCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            } catch (...) {}

            auto spawnSpacer = [&](float pixels) -> UObject* {
                if (!spacerCls) return nullptr;
                FStaticConstructObjectParameters spP(spacerCls, outer);
                UObject* sp = nullptr;
                try { sp = UObjectGlobals::StaticConstructObject(spP); } catch (...) {}
                if (!sp) return nullptr;
                jw_setSizeBoxOverride(sp, 0.0f, pixels);
                if (auto* fn = sp->GetFunctionByNameInChain(STR("SetVisibility")))
                {
                    std::vector<uint8_t> bb(fn->GetParmsSize(), 0);
                    auto* p = findParam(fn, STR("InVisibility"));
                    if (p) {
                        *reinterpret_cast<uint8_t*>(bb.data() + p->GetOffset_Internal()) = 3; // SelfHitTestInvisible
                        safeProcessEvent(sp, fn, bb.data());
                    }
                }
                return sp;
            };

            // 6) Re-add every entry in finalOrder, with a spacer BEFORE
            //    every entry after the first. AddChild IS reflected and
            //    is the proven path.
            int gapsAdded = 0;
            for (size_t i = 0; i < finalOrder.size(); ++i)
            {
                if (i > 0)
                {
                    if (UObject* sp = spawnSpacer(12.0f))
                    {
                        appendToVerticalBox(vbox, sp);
                        ++gapsAdded;
                    }
                }
                appendToVerticalBox(vbox, finalOrder[i]);
            }
            VLOG(STR("[SettingsUI] PauseMenu — rebuilt vbox: {} children + {} spacers\n"),
                 (int)finalOrder.size(), gapsAdded);

            m_pauseMenuInjectedFor = FWeakObjectPtr(escapeMenu);
        }

        // Re-apply Cheats context every frame while settings are open.
        // Defends against the Gameplay tab's Tick / Construct logic
        // re-showing the native children we want hidden in Cheats mode.
        // Throttled to ~5 Hz to keep CPU low.
        uint64_t m_lastCheatsReapplyMs{0};
        // v0.32 — Spawn a buff toggle row using the native
        // WBP_SettingsCheckBox_C widget (same as Crossplay/Vibration).
        // Visual is a real checkbox; click flips state via toggleBuffEntry.
        // Falls back to KeySelector if checkbox class isn't cached yet.
        UObject* spawnBuffToggleRow(int cheatEntryIdx)
        {
            int n = 0;
            const CheatEntry* entries = cheatEntries(n);
            if (cheatEntryIdx < 0 || cheatEntryIdx >= n) return nullptr;
            const CheatEntry& e = entries[cheatEntryIdx];
            if (m_settingsCheckBoxCls)
            {
                UObject* row = spawnCheckBoxRow(e.label,
                    [this, cheatEntryIdx]() -> bool {
                        return (cheatEntryIdx >= 0 && cheatEntryIdx < (int)m_buffStates.size())
                               ? m_buffStates[cheatEntryIdx] : false;
                    },
                    [this, cheatEntryIdx](bool /*newState*/) {
                        // toggleBuffEntry flips m_buffStates[idx] AND applies
                        // the GE; we just call it (it ignores newState).
                        toggleBuffEntry(cheatEntryIdx);
                    });
                if (row) return row;
            }
            // Fallback to keymap-style row if checkbox class not cached.
            if (!m_settingsKeySelectorCls) return nullptr;
            UObject* row = jw_createGameWidget(m_settingsKeySelectorCls);
            if (!row) return nullptr;
            if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(e.label);
            if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, e.label);
            BuffToggleRow btr; btr.selector = FWeakObjectPtr(row); btr.cheatEntryIdx = cheatEntryIdx;
            m_buffToggleRows.push_back(btr);
            registerRowStatus(row, L"OFF", 0.65f, 0.65f, 0.65f,
                [this, cheatEntryIdx](std::wstring& t, float& r, float& g, float& b) {
                    bool on = (cheatEntryIdx >= 0 && cheatEntryIdx < (int)m_buffStates.size())
                              ? m_buffStates[cheatEntryIdx] : false;
                    if (on) { t = L"ON";  r=0.31f; g=0.78f; b=0.47f; }
                    else    { t = L"OFF"; r=0.65f; g=0.65f; b=0.65f; }
                });
            return row;
        }

        // v0.27 — Spawn a row for a tweak cycle. Status text shows the
        // current value label (e.g. "DEFAULT", "99", "999", "2x").
        UObject* spawnTweakCycleRow(int tweakIdx)
        {
            if (!m_settingsKeySelectorCls) return nullptr;
            int nT = 0;
            const TweakEntry* tweaks = tweakEntries(nT);
            if (tweakIdx < 0 || tweakIdx >= nT) return nullptr;
            const TweakEntry& t = tweaks[tweakIdx];
            UObject* row = jw_createGameWidget(m_settingsKeySelectorCls);
            if (!row) return nullptr;
            if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(t.label);
            if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, t.label);
            TweakCycleRow tcr; tcr.selector = FWeakObjectPtr(row); tcr.tweakIdx = tweakIdx;
            m_tweakCycleRows.push_back(tcr);
            // Dynamic value-label provider.
            registerRowStatus(row, L"DEFAULT", 0.65f, 0.65f, 0.65f,
                [this, tweakIdx](std::wstring& outT, float& r, float& g, float& b) {
                    int nT2 = 0;
                    const TweakEntry* tw = tweakEntries(nT2);
                    if (tweakIdx < 0 || tweakIdx >= nT2) return;
                    const TweakEntry& te = tw[tweakIdx];
                    int curIdx = (tweakIdx < (int)m_tweakCurrentIdx.size()) ? m_tweakCurrentIdx[tweakIdx] : 0;
                    if (curIdx == 0) { outT = L"DEFAULT"; r=0.65f; g=0.65f; b=0.65f; return; }
                    if (curIdx >= (int)te.cycleValues.size()) { outT = L"?"; return; }
                    int v = te.cycleValues[curIdx];
                    wchar_t buf[32];
                    if (te.kind == TweakKind::SpecialNoCost ||
                        te.kind == TweakKind::SpecialInstantCraft)
                    {
                        outT = (v == 0) ? L"OFF" : L"ON";
                        if (v != 0) { r=0.31f; g=0.78f; b=0.47f; }
                        else        { r=0.65f; g=0.65f; b=0.65f; }
                        return;
                    }
                    if (te.isMultiplier) {
                        swprintf_s(buf, L"%dx", v);
                    } else {
                        swprintf_s(buf, L"%d", v);
                    }
                    outT = buf;
                    r=0.9f; g=0.75f; b=0.2f;
                });
            return row;
        }

        // v0.32 — Cache the WBP_SettingsCheckBox_C UClass by probing the
        // Gameplay tab's `MinimapLock` (or `StreamerMode`) member. Same
        // technique used for KeySelector / SectionHeading classes.
        bool ensureSettingsCheckBoxClassCached(UObject* gameplayTabAny)
        {
            if (m_settingsCheckBoxCls) return true;
            if (!gameplayTabAny || !isObjectAlive(gameplayTabAny)) return false;
            // Walk up to SettingsScreen, look at WBP_GameplayTab.MinimapLock.
            UObject* gp = gameplayTabAny;
            std::wstring cls = safeClassName(gp);
            if (cls != STR("WBP_GameplayTab_C"))
                gp = settingsScreenForTab(gameplayTabAny);
            if (gp)
            {
                std::wstring c = safeClassName(gp);
                if (c == STR("WBP_SettingsScreen_C")) {
                    auto* gpPtr = gp->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_GameplayTab"));
                    if (gpPtr) gp = *gpPtr;
                }
            }
            if (!gp || !isObjectAlive(gp)) return false;
            for (const wchar_t* probe : { STR("MinimapLock"), STR("StreamerMode") })
            {
                auto* p = gp->GetValuePtrByPropertyNameInChain<UObject*>(probe);
                UObject* w = p ? *p : nullptr;
                if (w && isObjectAlive(w))
                {
                    if (!m_settingsCheckBoxCls)
                        m_settingsCheckBoxCls = static_cast<UClass*>(w->GetClassPrivate());
                    // v0.37 — also capture the CheckIcon brush bytes so
                    // we can paste them onto our spawned checkboxes.
                    if (!m_checkIconBrushCaptured)
                    {
                        if (auto* iconPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("CheckIcon")))
                        {
                            if (UObject* icon = *iconPtr)
                            {
                                if (auto* brushPtr = icon->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Brush")))
                                {
                                    std::memcpy(m_checkIconBrush, brushPtr, sizeof(m_checkIconBrush));
                                    m_checkIconBrushCaptured = true;
                                    VLOG(STR("[SettingsUI] captured CheckIcon brush from '{}'\n"), probe);
                                }
                            }
                        }
                    }
                    if (m_settingsCheckBoxCls)
                    {
                        VLOG(STR("[SettingsUI] cached WBP_SettingsCheckBox UClass={:p} from '{}'\n"),
                             (void*)m_settingsCheckBoxCls, probe);
                        return true;
                    }
                }
            }
            return false;
        }

        // v0.32 — CheckBox toggle row tracking. Each entry maps the
        // spawned WBP_SettingsCheckBox_C to a callback fired on state
        // change. Status text is gone — the native checkbox visual IS
        // the status indicator (checked = ON).
        struct CheckBoxRow {
            FWeakObjectPtr widget;
            std::function<void(bool)> onChanged;
            std::function<bool()> getCurrent;
            bool ignoreNextStateChange{false};
        };
        std::vector<CheckBoxRow> m_checkBoxRows;

        // v0.44 — replace native checkbox with a 2-option carousel
        // (OFF / ON). The carousel widget actually works (we use it for
        // tweaks). Each toggle row tracks the carousel + an onChanged
        // callback. Click prev/next arrow → flip state.
        struct CarouselToggleRow {
            FWeakObjectPtr widget;
            std::function<void(bool)> onChanged;
            std::function<bool()> getCurrent;
        };
        std::vector<CarouselToggleRow> m_carouselToggles;

        // v0.44 — toggle row spawned as a 2-option Carousel (OFF/ON).
        // The native checkbox widget never showed its check mark for our
        // dynamically-spawned instances; the carousel always works.
        UObject* spawnCheckBoxRow(const wchar_t* label,
                                  std::function<bool()> getCurrent,
                                  std::function<void(bool)> onChanged)
        {
            if (m_settingsCarouselCls)
            {
                UObject* car = jw_createGameWidget(m_settingsCarouselCls);
                if (car)
                {
                    if (auto* namePtr = car->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                        *namePtr = FText(label);
                    if (auto* tbPtr = car->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                        if (UObject* tb = *tbPtr) umgSetText(tb, label);
                    // Clear any defaults.
                    if (auto* clearFn = car->GetFunctionByNameInChain(STR("ClearOptions")))
                    {
                        std::vector<uint8_t> b(clearFn->GetParmsSize(), 0);
                        safeProcessEvent(car, clearFn, b.data());
                    }
                    // Add OFF and ON options.
                    auto* addFn = car->GetFunctionByNameInChain(STR("Add Option"));
                    if (!addFn) addFn = car->GetFunctionByNameInChain(STR("AddOption"));
                    if (addFn)
                    {
                        for (int v = 0; v < 2; ++v)
                        {
                            int sz = addFn->GetParmsSize();
                            std::vector<uint8_t> b(sz, 0);
                            auto* pL = findParam(addFn, STR("Label"));
                            auto* pV = findParam(addFn, STR("Value"));
                            FText lbl(v ? L"ON" : L"OFF");
                            FString val(v ? L"1" : L"0");
                            if (pL) std::memcpy(b.data() + pL->GetOffset_Internal(), &lbl, sizeof(FText));
                            if (pV) std::memcpy(b.data() + pV->GetOffset_Internal(), &val, sizeof(FString));
                            safeProcessEvent(car, addFn, b.data());
                        }
                    }
                    bool curOn = getCurrent ? getCurrent() : false;
                    // Set initial value via SetValue.
                    if (auto* svFn = car->GetFunctionByNameInChain(STR("SetValue")))
                    {
                        std::vector<uint8_t> b(svFn->GetParmsSize(), 0);
                        if (auto* p = findParam(svFn, STR("NewValue")))
                        {
                            FString val(curOn ? L"1" : L"0");
                            std::memcpy(b.data() + p->GetOffset_Internal(), &val, sizeof(FString));
                            safeProcessEvent(car, svFn, b.data());
                        }
                    }
                    // Also write OptionText directly for first-paint.
                    if (auto* otPtr = car->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
                        if (UObject* ot = *otPtr) umgSetText(ot, curOn ? L"ON" : L"OFF");
                    CarouselToggleRow ctr;
                    ctr.widget = FWeakObjectPtr(car);
                    ctr.onChanged = std::move(onChanged);
                    ctr.getCurrent = std::move(getCurrent);
                    m_carouselToggles.push_back(std::move(ctr));
                    return car;
                }
            }
            // Fallback to native checkbox if carousel class not yet cached.
            if (!m_settingsCheckBoxCls) return nullptr;
            UObject* row = jw_createGameWidget(m_settingsCheckBoxCls);
            if (!row) return nullptr;
            if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(label);
            if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, label);
            // Set initial check state via the inner UCheckBox.
            // We push to m_checkBoxRows BEFORE SetIsChecked so the
            // ignoreNextStateChange flag can suppress the delegate
            // ping-pong on the initial assignment.
            bool initialOn = getCurrent ? getCurrent() : false;
            CheckBoxRow cbr;
            cbr.widget = FWeakObjectPtr(row);
            cbr.getCurrent = std::move(getCurrent);
            cbr.onChanged = std::move(onChanged);
            cbr.ignoreNextStateChange = true; // suppress initial fire
            m_checkBoxRows.push_back(std::move(cbr));
            m_checkBoxLabels.push_back(label ? std::wstring(label) : std::wstring());
            if (auto* cbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionCheckBox")))
            {
                if (UObject* cb = *cbPtr)
                {
                    auto* setFn = cb->GetFunctionByNameInChain(STR("SetIsChecked"));
                    if (setFn)
                    {
                        int sz = setFn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        b[0] = initialOn ? 1 : 0;
                        safeProcessEvent(cb, setFn, b.data());
                    }
                }
            }
            // v0.41 — diagnostic: dump Vibration's CheckIcon (a known
            // working native checkbox on the Controller tab) and
            // compare to ours after spawn so we can see what's different.
            static bool s_dumpedVibrationCompare = false;
            if (!s_dumpedVibrationCompare)
            {
                UObject* ss = settingsScreenForTab(row);
                if (ss)
                {
                    auto* ctPtr = ss->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_ControllerTab"));
                    UObject* ct = ctPtr ? *ctPtr : nullptr;
                    if (ct && isObjectAlive(ct))
                    {
                        auto* vibPtr = ct->GetValuePtrByPropertyNameInChain<UObject*>(STR("Vibration"));
                        UObject* vib = vibPtr ? *vibPtr : nullptr;
                        if (vib && isObjectAlive(vib))
                        {
                            auto dumpCheckBox = [this](const wchar_t* tag, UObject* cb) {
                                if (!cb) return;
                                auto* cbInner = cb->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionCheckBox"));
                                auto* iconPtr = cb->GetValuePtrByPropertyNameInChain<UObject*>(STR("CheckIcon"));
                                UObject* icon = iconPtr ? *iconPtr : nullptr;
                                UObject* inner = cbInner ? *cbInner : nullptr;
                                uint8_t* iconVis = icon ? icon->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")) : nullptr;
                                uint8_t* iconBrush = icon ? icon->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Brush")) : nullptr;
                                UObject* iconResource = nullptr;
                                if (iconBrush) iconResource = *reinterpret_cast<UObject**>(iconBrush + 16);
                                VLOG(STR("[SettingsUI] CP5-CMP {}: cb={:p} cls={}, OptionCheckBox={:p}, CheckIcon={:p} cls={}, iconVis={}, brush[0..3]={},{},{},{}, ResourceObject@brush+16={:p}\n"),
                                     tag, (void*)cb, safeClassName(cb).c_str(),
                                     (void*)inner,
                                     (void*)icon, icon ? safeClassName(icon).c_str() : L"null",
                                     iconVis ? (int)*iconVis : -1,
                                     iconBrush ? (int)iconBrush[0] : -1,
                                     iconBrush ? (int)iconBrush[1] : -1,
                                     iconBrush ? (int)iconBrush[2] : -1,
                                     iconBrush ? (int)iconBrush[3] : -1,
                                     (void*)iconResource);
                            };
                            dumpCheckBox(STR("Vibration"), vib);
                            dumpCheckBox(STR("OurRow"), row);
                            s_dumpedVibrationCompare = true;
                        }
                    }
                }
            }

            // v0.39 — set CheckIcon's brush via SetBrushFromTexture
            // UFunction with the F12 menu's known-good T_UI_Icon_Check
            // texture. The byte-copy approach didn't render properly.
            if (auto* iconPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("CheckIcon")))
            {
                if (UObject* icon = *iconPtr)
                {
                    UObject* texCheck = findTexture2DByName(L"T_UI_Icon_Check");
                    if (texCheck)
                    {
                        auto* fn = icon->GetFunctionByNameInChain(STR("SetBrushFromTexture"));
                        if (fn)
                        {
                            int sz = fn->GetParmsSize();
                            std::vector<uint8_t> b(sz, 0);
                            auto* p1 = findParam(fn, STR("Texture"));
                            auto* p2 = findParam(fn, STR("bMatchSize"));
                            if (p1) *reinterpret_cast<UObject**>(b.data() + p1->GetOffset_Internal()) = texCheck;
                            if (p2) *(b.data() + p2->GetOffset_Internal()) = 0;
                            safeProcessEvent(icon, fn, b.data());
                        }
                        // Also set explicit brush size so the texture renders.
                        if (auto* sizeFn = icon->GetFunctionByNameInChain(STR("SetBrushSize")))
                        {
                            int sz = sizeFn->GetParmsSize();
                            std::vector<uint8_t> b(sz, 0);
                            if (auto* p = findParam(sizeFn, STR("DesiredSize"))) {
                                float sizeBytes[2] = { 32.0f, 32.0f };
                                std::memcpy(b.data() + p->GetOffset_Internal(), sizeBytes, sizeof(sizeBytes));
                            }
                            safeProcessEvent(icon, sizeFn, b.data());
                        }
                    }
                    setWidgetVisibilityForce(icon, initialOn ? 0 : 1);
                }
            }
            // (Already pushed to m_checkBoxRows / m_checkBoxLabels above
            // with ignoreNextStateChange=true.)
            return row;
        }

        // Called from ProcessEvent post-hook on OnCheckBoxStateChanged
        // (or BndEvt__...OnCheckBoxComponentStateChanged). Returns true
        // if the widget matched one of our rows.
        bool maybeFireCheckBoxRow(UObject* widget, bool newState)
        {
            if (!widget) return false;
            for (auto& cbr : m_checkBoxRows)
            {
                if (cbr.widget.Get() != widget) continue;
                // v0.42 — swallow the delegate when the change came from
                // our own SetIsChecked call. Prevents user-state ping-pong.
                if (cbr.ignoreNextStateChange) {
                    cbr.ignoreNextStateChange = false;
                    return true;
                }
                if (cbr.onChanged) cbr.onChanged(newState);
                // CheckIcon visibility now driven by refreshCheckBoxIcons
                // tick; we don't write it here to avoid extra delegate fires.
                return true;
            }
            return false;
        }

        // v0.40 — also store the original label per checkbox so we can
        // prepend "✓ " when on. The native CheckIcon UImage approach
        // didn't render reliably (4 attempts), so we take the visual
        // signal into the OptionNameTextBlock label itself.
        std::vector<std::wstring> m_checkBoxLabels; // parallel to m_checkBoxRows

        void refreshCheckBoxIcons()
        {
            for (size_t idx = 0; idx < m_checkBoxRows.size(); ++idx)
            {
                auto& cbr = m_checkBoxRows[idx];
                UObject* w = cbr.widget.Get();
                if (!w || !isObjectAlive(w)) continue;
                bool on = cbr.getCurrent ? cbr.getCurrent() : false;

                // v0.42 — DO NOT call SetIsChecked here. It fires the
                // OnCheckBoxStateChanged delegate which would ping-pong
                // user toggles. The user sees the gold ring (UCheckBox's
                // own visual) which reflects the actual click state.
                // We only update the LABEL prefix and CheckIcon UImage
                // visibility based on m_buffStates.

                // v0.43 — no label prefix (user explicitly didn't want it).
                // Just keep the inner UCheckBox.CheckedState in sync via
                // direct property write + SynchronizeProperties so the
                // Slate widget renders the proper checkmark.
                if (auto* cbPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionCheckBox")))
                {
                    if (UObject* cb = *cbPtr)
                    {
                        // Direct write of CheckedState (ECheckBoxState enum
                        // 0=Unchecked, 1=Checked, 2=Undetermined).
                        if (auto* csPtr = cb->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CheckedState")))
                        {
                            uint8_t want = on ? 1 : 0;
                            if (*csPtr != want) *csPtr = want;
                        }
                        // Push to Slate.
                        if (auto* syncFn = cb->GetFunctionByNameInChain(STR("SynchronizeProperties")))
                        {
                            std::vector<uint8_t> b(syncFn->GetParmsSize(), 0);
                            safeProcessEvent(cb, syncFn, b.data());
                        }
                    }
                }
                // CheckIcon UImage — toggle visibility for the native indicator.
                if (auto* iconPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("CheckIcon")))
                {
                    if (UObject* icon = *iconPtr)
                        setWidgetVisibility(icon, on ? 0 : 1);
                }
            }
        }

        // v0.33 — Carousel row tracking + helpers. Each row maps to a
        // tweakIdx; the carousel exposes preset values, click on
        // </> arrow buttons cycles to next/prev option which fires
        // CarouselValueChanged with the new value string.
        struct CarouselRow {
            FWeakObjectPtr widget;
            int tweakIdx;
        };
        std::vector<CarouselRow> m_carouselRows;

        bool ensureSettingsCarouselClassCached(UObject* anyTab)
        {
            if (m_settingsCarouselCls) return true;
            if (!anyTab || !isObjectAlive(anyTab)) return false;
            UObject* gp = anyTab;
            std::wstring c = safeClassName(gp);
            if (c != STR("WBP_GameplayTab_C"))
                gp = settingsScreenForTab(anyTab);
            if (gp && safeClassName(gp) == STR("WBP_SettingsScreen_C"))
            {
                auto* gpPtr = gp->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_GameplayTab"));
                if (gpPtr) gp = *gpPtr;
            }
            if (!gp || !isObjectAlive(gp)) return false;
            for (const wchar_t* probe : {
                STR("Carousel_MultiplayerNamesMode"),
                STR("Carousel_MultiplayerSessionMode"),
                STR("Carousel_WaypointNamesMode"),
            })
            {
                auto* p = gp->GetValuePtrByPropertyNameInChain<UObject*>(probe);
                UObject* w = p ? *p : nullptr;
                if (w && isObjectAlive(w))
                {
                    m_settingsCarouselCls = static_cast<UClass*>(w->GetClassPrivate());
                    if (m_settingsCarouselCls) {
                        VLOG(STR("[SettingsUI] cached Carousel UClass={:p} from '{}'\n"),
                             (void*)m_settingsCarouselCls, probe);
                        return true;
                    }
                }
            }
            return false;
        }

        // Spawn a native-style carousel row. Options is a list of
        // (display label, value-string) pairs where value-string is the
        // index of the cycle value as a stringified int. CurrentValue is
        // the stringified current index.
        UObject* spawnCarouselRow(int tweakIdx,
                                  const wchar_t* label,
                                  const std::vector<std::pair<std::wstring, std::wstring>>& options,
                                  const std::wstring& currentValue)
        {
            if (!m_settingsCarouselCls) return nullptr;
            UObject* row = jw_createGameWidget(m_settingsCarouselCls);
            if (!row) return nullptr;
            if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(label);
            if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, label);

            // Clear any defaults the BP placed.
            if (auto* clearFn = row->GetFunctionByNameInChain(STR("ClearOptions")))
            {
                int sz = clearFn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                safeProcessEvent(row, clearFn, b.data());
            }
            // Add Option(FText Label, FString Value) — note the literal
            // function name has a space (per BP graph).
            auto* addFn = row->GetFunctionByNameInChain(STR("Add Option"));
            if (!addFn) addFn = row->GetFunctionByNameInChain(STR("AddOption"));
            if (addFn)
            {
                for (const auto& opt : options)
                {
                    int sz = addFn->GetParmsSize();
                    std::vector<uint8_t> b(sz, 0);
                    auto* pL = findParam(addFn, STR("Label"));
                    auto* pV = findParam(addFn, STR("Value"));
                    if (pL) {
                        FText lbl(opt.first.c_str());
                        std::memcpy(b.data() + pL->GetOffset_Internal(), &lbl, sizeof(FText));
                    }
                    if (pV) {
                        FString val(opt.second.c_str());
                        std::memcpy(b.data() + pV->GetOffset_Internal(), &val, sizeof(FString));
                    }
                    safeProcessEvent(row, addFn, b.data());
                }
            }
            // SetValue(FString) — sets initial selection.
            if (auto* svFn = row->GetFunctionByNameInChain(STR("SetValue")))
            {
                int sz = svFn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                if (auto* p = findParam(svFn, STR("NewValue")))
                {
                    FString val(currentValue.c_str());
                    std::memcpy(b.data() + p->GetOffset_Internal(), &val, sizeof(FString));
                    safeProcessEvent(row, svFn, b.data());
                }
            }
            // v0.36 — also write OptionText directly so the displayed
            // label matches the current INI selection on first paint.
            // (The BP's display logic depends on ValueArray which our
            // dynamic Add Option calls may not populate cleanly.)
            int curIdxInt = 0;
            try { curIdxInt = std::stoi(currentValue); } catch (...) {}
            if (curIdxInt >= 0 && curIdxInt < (int)options.size())
            {
                if (auto* otPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
                    if (UObject* ot = *otPtr) umgSetText(ot, options[curIdxInt].first.c_str());
            }
            CarouselRow cr; cr.widget = FWeakObjectPtr(row); cr.tweakIdx = tweakIdx;
            m_carouselRows.push_back(cr);
            return row;
        }

        bool maybeFireCarouselRow(UObject* widget, const std::wstring& newValue)
        {
            for (auto& cr : m_carouselRows)
            {
                if (cr.widget.Get() != widget) continue;
                int newIdx = 0;
                try { newIdx = std::stoi(newValue); } catch (...) { return true; }
                int nT = 0;
                const TweakEntry* tw = tweakEntries(nT);
                if (cr.tweakIdx < 0 || cr.tweakIdx >= nT) return true;
                if ((int)m_tweakCurrentIdx.size() != nT)
                    m_tweakCurrentIdx.assign(nT, 0);
                int curIdx = m_tweakCurrentIdx[cr.tweakIdx];
                int cycleLen = (int)tw[cr.tweakIdx].cycleValues.size();
                int hops = ((newIdx - curIdx) % cycleLen + cycleLen) % cycleLen;
                for (int h = 0; h < hops; ++h) cycleTweakValue(cr.tweakIdx);
                return true;
            }
            return false;
        }

        // v0.35+v0.44 — handle BndEvt delegates fired on the carousel
        // widget itself. fnName contains "PrevButton" or "NextButton".
        // Handles BOTH tweak carousels AND ON/OFF toggle carousels.
        bool maybeFireCarouselViaDelegate(UObject* widget, const wchar_t* fnName)
        {
            if (!widget || !fnName) return false;
            bool isPrev = wcsstr(fnName, STR("PrevButton")) != nullptr;
            bool isNext = wcsstr(fnName, STR("NextButton")) != nullptr;
            if (!isPrev && !isNext) return false;
            // ON/OFF toggle carousel rows.
            for (auto& ctr : m_carouselToggles)
            {
                if (ctr.widget.Get() != widget) continue;
                bool curOn = ctr.getCurrent ? ctr.getCurrent() : false;
                bool newOn = !curOn; // either prev or next flips for 2-option
                if (ctr.onChanged) ctr.onChanged(newOn);
                if (auto* otPtr = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
                    if (UObject* ot = *otPtr) umgSetText(ot, newOn ? L"ON" : L"OFF");
                return true;
            }
            for (auto& cr : m_carouselRows)
            {
                if (cr.widget.Get() != widget) continue;
                int nT = 0;
                const TweakEntry* tw = tweakEntries(nT);
                if (cr.tweakIdx < 0 || cr.tweakIdx >= nT) return true;
                int cycleLen = (int)tw[cr.tweakIdx].cycleValues.size();
                if (cycleLen <= 0) return true;
                int hops = isNext ? 1 : (cycleLen - 1);
                for (int h = 0; h < hops; ++h) cycleTweakValue(cr.tweakIdx);
                int curIdx = (cr.tweakIdx < (int)m_tweakCurrentIdx.size())
                           ? m_tweakCurrentIdx[cr.tweakIdx] : 0;
                if (auto* otPtr = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
                {
                    if (UObject* ot = *otPtr) {
                        std::wstring lbl;
                        if (curIdx == 0) lbl = L"Default";
                        else {
                            wchar_t buf[32];
                            if (tw[cr.tweakIdx].isMultiplier)
                                swprintf_s(buf, L"%dx", tw[cr.tweakIdx].cycleValues[curIdx]);
                            else
                                swprintf_s(buf, L"%d",  tw[cr.tweakIdx].cycleValues[curIdx]);
                            lbl = buf;
                        }
                        umgSetText(ot, lbl.c_str());
                    }
                }
                VLOG(STR("[SettingsUI] CP5 — carousel '{}' delegate cycled, dir={}\n"),
                     tw[cr.tweakIdx].label, isNext ? L"Next" : L"Prev");
                return true;
            }
            return false;
        }

        // v0.34 — fallback for when the BP carousel doesn't fire its
        // CarouselValueChanged delegate (e.g. ValueArray not populated).
        // Detects raw clicks on the inner Prev/Next UButtons by walking
        // the click target's Outer chain to find one of our tracked
        // carousel widgets, then drives cycleTweakValue ourselves and
        // calls SetValue on the carousel to refresh its display.
        bool maybeFireCarouselButton(UObject* clickedBtn)
        {
            if (!clickedBtn) return false;
            // The clicked button is a UButton (PrevButton or NextButton).
            // Walk up Outer to find a carousel widget we tracked.
            UObject* cur = clickedBtn->GetOuterPrivate();
            UObject* matchedCarousel = nullptr;
            int matchedTweakIdx = -1;
            // Also detect ON/OFF toggle carousels here.
            CarouselToggleRow* matchedToggle = nullptr;
            for (int hop = 0; hop < 8 && cur; ++hop)
            {
                for (auto& cr : m_carouselRows)
                {
                    if (cr.widget.Get() == cur) {
                        matchedCarousel = cur;
                        matchedTweakIdx = cr.tweakIdx;
                        break;
                    }
                }
                if (!matchedCarousel)
                {
                    for (auto& ctr : m_carouselToggles)
                    {
                        if (ctr.widget.Get() == cur) {
                            matchedCarousel = cur;
                            matchedToggle = &ctr;
                            break;
                        }
                    }
                }
                if (matchedCarousel) break;
                cur = cur->GetOuterPrivate();
            }
            if (matchedToggle)
            {
                bool curOn = matchedToggle->getCurrent ? matchedToggle->getCurrent() : false;
                bool newOn = !curOn;
                if (matchedToggle->onChanged) matchedToggle->onChanged(newOn);
                if (auto* otPtr = matchedCarousel->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
                    if (UObject* ot = *otPtr) umgSetText(ot, newOn ? L"ON" : L"OFF");
                return true;
            }
            if (!matchedCarousel || matchedTweakIdx < 0) return false;

            // Determine direction by inspecting the button's name.
            std::wstring btnName;
            try { btnName = clickedBtn->GetName(); } catch (...) {}
            bool isNext = btnName.find(STR("Next")) != std::wstring::npos;
            bool isPrev = btnName.find(STR("Prev")) != std::wstring::npos;
            if (!isNext && !isPrev) return false;

            int nT = 0;
            const TweakEntry* tw = tweakEntries(nT);
            if (matchedTweakIdx >= nT) return true;
            int cycleLen = (int)tw[matchedTweakIdx].cycleValues.size();
            if (cycleLen <= 0) return true;
            // cycleTweakValue advances by 1. For Prev, advance (cycleLen-1).
            int hops = isNext ? 1 : (cycleLen - 1);
            for (int h = 0; h < hops; ++h) cycleTweakValue(matchedTweakIdx);

            // Push new value back to the carousel so its visual updates.
            int curIdx = (matchedTweakIdx < (int)m_tweakCurrentIdx.size())
                       ? m_tweakCurrentIdx[matchedTweakIdx] : 0;
            if (auto* svFn = matchedCarousel->GetFunctionByNameInChain(STR("SetValue")))
            {
                int sz = svFn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                if (auto* p = findParam(svFn, STR("NewValue"))) {
                    std::wstring s = std::to_wstring(curIdx);
                    FString val(s.c_str());
                    std::memcpy(b.data() + p->GetOffset_Internal(), &val, sizeof(FString));
                    safeProcessEvent(matchedCarousel, svFn, b.data());
                }
            }
            // Also update the visible OptionText so the change is shown
            // even if the BP's display logic didn't auto-refresh.
            if (auto* otPtr = matchedCarousel->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionText")))
            {
                if (UObject* ot = *otPtr) {
                    std::wstring lbl;
                    if (curIdx == 0) lbl = L"Default";
                    else {
                        wchar_t buf[32];
                        if (tw[matchedTweakIdx].isMultiplier)
                            swprintf_s(buf, L"%dx", tw[matchedTweakIdx].cycleValues[curIdx]);
                        else
                            swprintf_s(buf, L"%d",  tw[matchedTweakIdx].cycleValues[curIdx]);
                        lbl = buf;
                    }
                    umgSetText(ot, lbl.c_str());
                }
            }
            VLOG(STR("[SettingsUI] CP5 — carousel '{}' cycled, tweakIdx={}, dir={}\n"),
                 tw[matchedTweakIdx].label, matchedTweakIdx, isNext ? L"Next" : L"Prev");
            return true;
        }

        // v0.33 — Build a label-left + button-right row for action items
        // (Rename Character, Save Game, Unlock Recipes, Read All, Clear
        // All Buffs). Returns the HBox row; the inner FrontEndButton is
        // tracked as the click target in m_gameOptButtons.
        UObject* spawnActionButtonRow(UObject* outer,
                                      const wchar_t* label,
                                      const wchar_t* buttonText,
                                      GameOptKind kind)
        {
            if (!m_frontEndButtonCls) return nullptr;
            UClass* hboxCls = nullptr;
            UClass* tbCls = nullptr;
            UClass* sbCls = nullptr;
            try {
                hboxCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
                tbCls   = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
                sbCls   = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            } catch (...) {}
            if (!hboxCls || !tbCls) return nullptr;

            FStaticConstructObjectParameters hP(hboxCls, outer);
            UObject* row = UObjectGlobals::StaticConstructObject(hP);
            if (!row) return nullptr;

            // v0.35 — two equal-width columns. Each column fills 50% of
            // the row; content within each is centered horizontally and
            // vertically. Label uses size 32 (larger), button keeps its
            // own BP-default font (looks smaller relative to the label).
            FStaticConstructObjectParameters tP(tbCls, outer);
            UObject* labelTb = UObjectGlobals::StaticConstructObject(tP);
            if (labelTb)
            {
                umgSetText(labelTb, label);
                umgSetTextColor(labelTb, 0.86f, 0.90f, 0.96f, 1.0f);
                umgSetFontSize(labelTb, 32);
                UObject* slot = addToHBox(row, labelTb);
                if (slot) {
                    umgSetSlotSize(slot, 1.0f, 1); // Fill — left column
                    umgSetSlotPadding(slot, 16.0f, 16.0f, 16.0f, 16.0f);
                    umgSetHAlign(slot, 2); // Center horizontally in left col
                    umgSetVAlign(slot, 2); // Center vertically
                }
            }

            // v0.38 — use spawnGameOptButton (the proven path) so the
            // button registers in m_gameOptButtons exactly like CP4 used
            // to. Then put it inside our HBox row.
            UObject* btn = spawnGameOptButton(outer, buttonText, kind);
            if (!btn) return row;
            if (UObject* innerTb = jw_findChildInTree(btn, STR("ButtonText")))
                umgSetFontSize(innerTb, 18);

            // v0.35 — narrow SizeBox so the button is smaller than the
            // 50% column width, then center it within the right column.
            UObject* btnContainer = btn;
            if (sbCls)
            {
                FStaticConstructObjectParameters sP(sbCls, outer);
                UObject* sb = UObjectGlobals::StaticConstructObject(sP);
                if (sb) {
                    jw_setSizeBoxOverride(sb, 240.0f, 64.0f);
                    if (auto* setFn = sb->GetFunctionByNameInChain(STR("SetContent"))) {
                        int sz = setFn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        auto* p = findParam(setFn, STR("InContent"));
                        if (!p) p = findParam(setFn, STR("Content"));
                        if (p) {
                            *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = btn;
                            safeProcessEvent(sb, setFn, b.data());
                            btnContainer = sb;
                        }
                    }
                }
            }

            UObject* btnSlot = addToHBox(row, btnContainer);
            if (btnSlot) {
                umgSetSlotSize(btnSlot, 1.0f, 1); // Fill — right column
                umgSetSlotPadding(btnSlot, 16.0f, 16.0f, 16.0f, 16.0f);
                umgSetHAlign(btnSlot, 2); // Center horizontally in right col
                umgSetVAlign(btnSlot, 2); // Center vertically
            }
            // (m_gameOptButtons registration done inside spawnGameOptButton.)
            return row;
        }

        // v0.32 — ModPack row uses native checkbox.
        UObject* spawnModPackRow(int modIdx, const std::wstring& displayName, bool /*enabled*/)
        {
            if (m_settingsCheckBoxCls)
            {
                return spawnCheckBoxRow(displayName.c_str(),
                    [this, modIdx]() -> bool {
                        return (modIdx >= 0 && modIdx < (int)m_ftGameModEntries.size())
                               ? m_ftGameModEntries[modIdx].enabled : false;
                    },
                    [this, modIdx](bool newState) {
                        if (modIdx >= 0 && modIdx < (int)m_ftGameModEntries.size()) {
                            m_ftGameModEntries[modIdx].enabled = newState;
                            saveGameMods(m_ftGameModEntries);
                        }
                    });
            }
            // Fallback (shouldn't normally happen).
            if (!m_settingsKeySelectorCls) return nullptr;
            UObject* row = jw_createGameWidget(m_settingsKeySelectorCls);
            if (!row) return nullptr;
            if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                *namePtr = FText(displayName.c_str());
            if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                if (UObject* tb = *tbPtr) umgSetText(tb, displayName.c_str());
            ModPackRow mpr; mpr.selector = FWeakObjectPtr(row); mpr.modIdx = modIdx;
            m_modPackRows.push_back(mpr);
            return row;
        }

        // Find the keyText TextBlock inside a WBP_SettingsKeySelector_C
        // instance. The widget tree is: SizeBox > Overlay > HBox > Border
        // 'InputBorder' > Overlay { InputKeySelector + keyText + glyph + alert }.
        UObject* findRowKeyText(UObject* selector)
        {
            if (!selector) return nullptr;
            return jw_findChildInTree(selector, STR("keyText"));
        }

        // Track a status-text override for an action row. The tick loop
        // re-applies the text every ~200ms so the BP's selecting-state
        // machine resets don't strip it. `dyn` is optional — if provided,
        // it recomputes text+color each tick from current state (used for
        // toggleables like ON/OFF, PEACE/FIGHT).
        void registerRowStatus(UObject* selector,
                               const wchar_t* staticText,
                               float r, float g, float b,
                               std::function<void(std::wstring&, float&, float&, float&)> dyn = {})
        {
            if (!selector) return;
            RowStatus rs;
            rs.selector = FWeakObjectPtr(selector);
            rs.text = staticText ? staticText : L"";
            rs.r = r; rs.g = g; rs.b = b;
            rs.dyn = std::move(dyn);
            m_rowStatus.push_back(std::move(rs));
            // Apply once immediately so the row isn't blank on first show.
            if (UObject* tb = findRowKeyText(selector))
            {
                std::wstring t = rs.text;
                float rr = rs.r, gg = rs.g, bb = rs.b;
                if (rs.dyn) rs.dyn(t, rr, gg, bb);
                umgSetText(tb, t.c_str());
                umgSetTextColor(tb, rr, gg, bb, 1.0f);
            }
        }

        // v0.36 — force-set Visibility via BOTH the UFunction AND a direct
        // byte write on the property. Some BP-managed widgets (e.g. our
        // composite HBox rows) ignore SetVisibility because their parent
        // re-applies a different value each tick.
        void setWidgetVisibilityForce(UObject* w, uint8_t vis)
        {
            if (!w || !isObjectAlive(w)) return;
            auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
            if (visPtr) *visPtr = vis;
            setWidgetVisibility(w, vis);
        }

        // v0.40 — recursive visibility hiding. For composite HBox rows
        // the parent's Collapsed visibility wasn't hiding the contents,
        // so we walk the Slots chain and force every child Collapsed too.
        void setRowVisibilityDeep(UObject* w, uint8_t vis)
        {
            if (!w || !isObjectAlive(w)) return;
            setWidgetVisibilityForce(w, vis);
            auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (slots)
            {
                for (int i = 0; i < slots->Num(); ++i)
                {
                    UObject* slot = (*slots)[i];
                    if (!slot) continue;
                    auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                    if (cPtr && *cPtr) setRowVisibilityDeep(*cPtr, vis);
                }
            }
            // Single-content (Border / SizeBox / ScaleBox).
            auto* singleC = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
            if (singleC && *singleC && *singleC != w)
                setRowVisibilityDeep(*singleC, vis);
        }

        // v0.41 — move widgets between scrollbox (visible) and a hidden
        // stash VerticalBox. Each panel keeps a slot that GC-roots the
        // widget, so swapping doesn't destroy them. Only runs on tab
        // state change (not every tick) so RemoveChild/AddChild aren't
        // hammered.
        // v6.15.0 — m_lastApplyCheatsTabActive + m_lastApplyValid removed
        // (defunct memoisation; the only thing they memoised was a flag
        // that became permanently false in v0.48).

        void parentMove(UObject* fromPanel, UObject* toPanel, UObject* widget)
        {
            if (!widget || !isObjectAlive(widget) || !toPanel) return;
            // Try removing from `fromPanel` first (no-op if not a child).
            if (fromPanel && isObjectAlive(fromPanel))
            {
                if (auto* fn = fromPanel->GetFunctionByNameInChain(STR("RemoveChild")))
                {
                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> b(sz, 0);
                    auto* p = findParam(fn, STR("Content"));
                    if (p) {
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = widget;
                        safeProcessEvent(fromPanel, fn, b.data());
                    }
                }
            }
            // Add to destination.
            if (auto* fn = toPanel->GetFunctionByNameInChain(STR("AddChild")))
            {
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                auto* p = findParam(fn, STR("Content"));
                if (!p) p = findParam(fn, STR("Widget"));
                if (p) {
                    *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = widget;
                    safeProcessEvent(toPanel, fn, b.data());
                }
            }
        }

        void applyTabContextVisibility()
        {
            // v6.15.0 — removed defunct m_lastApplyValid/m_lastApplyCheatsTabActive
            // memoisation — m_cheatsTabActive is permanently false since v0.48
            // so the early-return never fired anyway.
            UObject* gpVB = m_dualGameplayScroll.Get();
            UObject* chVB = m_dualCheatsScroll.Get();
            // v0.45 — if VBox refs went null (GC), re-find by walking
            // scrollbox children. The first two VerticalBox children we
            // injected are gpVB then chVB, in order.
            if ((!gpVB || !chVB))
            {
                UObject* sb = m_dualScrollBox.Get();
                if (sb && isObjectAlive(sb))
                {
                    int childCount = 0;
                    if (auto* gccFn = sb->GetFunctionByNameInChain(STR("GetChildrenCount")))
                    {
                        int sz = gccFn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        safeProcessEvent(sb, gccFn, b.data());
                        if (auto* rp = findParam(gccFn, STR("ReturnValue")))
                            childCount = *reinterpret_cast<int32_t*>(b.data() + rp->GetOffset_Internal());
                    }
                    auto getChildAt = [&](int idx) -> UObject* {
                        auto* fn = sb->GetFunctionByNameInChain(STR("GetChildAt"));
                        if (!fn) return nullptr;
                        int sz = fn->GetParmsSize();
                        std::vector<uint8_t> b(sz, 0);
                        if (auto* p = findParam(fn, STR("Index")))
                            *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = idx;
                        safeProcessEvent(sb, fn, b.data());
                        if (auto* rp = findParam(fn, STR("ReturnValue")))
                            return *reinterpret_cast<UObject**>(b.data() + rp->GetOffset_Internal());
                        return nullptr;
                    };
                    UObject* foundFirst = nullptr;
                    UObject* foundSecond = nullptr;
                    for (int i = 0; i < childCount; ++i)
                    {
                        UObject* c = getChildAt(i);
                        if (!c || !isObjectAlive(c)) continue;
                        std::wstring cls = safeClassName(c);
                        if (cls == STR("VerticalBox")) {
                            if (!foundFirst) foundFirst = c;
                            else if (!foundSecond) { foundSecond = c; break; }
                        }
                    }
                    if (foundFirst && !gpVB)  { gpVB = foundFirst;  m_dualGameplayScroll = FWeakObjectPtr(gpVB); }
                    if (foundSecond && !chVB) { chVB = foundSecond; m_dualCheatsScroll   = FWeakObjectPtr(chVB); }
                }
            }
            VLOG(STR("[SettingsUI] CP5 — applyTabContext cheats={}, gpVB={:p}, chVB={:p}\n"),
                 m_cheatsTabActive ? L"YES" : L"no", (void*)gpVB, (void*)chVB);
            if (gpVB && isObjectAlive(gpVB))
                setWidgetVisibilityForce(gpVB, m_cheatsTabActive ? 1 : 0);
            if (chVB && isObjectAlive(chVB))
                setWidgetVisibilityForce(chVB, m_cheatsTabActive ? 0 : 1);
        }

        void tickReapplyCheatsContext()
        {
            if (!m_settingsScreenOpen) return;
            uint64_t now = GetTickCount64();
            UObject* gp = m_dualInjectedFor.Get();
            if (!gp || !isObjectAlive(gp))
                gp = m_gameplayTabInjectedFor.Get();
            if (!gp || !isObjectAlive(gp)) return;
            if (now - m_lastCheatsReapplyMs < 100) return; // ~10 Hz
            m_lastCheatsReapplyMs = now;

            // Reapply F12-style status text on each registered action row.
            for (auto& rs : m_rowStatus)
            {
                UObject* sel = rs.selector.Get();
                if (!sel || !isObjectAlive(sel)) continue;
                UObject* tb = findRowKeyText(sel);
                if (!tb || !isObjectAlive(tb)) continue;
                std::wstring t = rs.text;
                float rr = rs.r, gg = rs.g, bb = rs.b;
                if (rs.dyn) rs.dyn(t, rr, gg, bb);
                umgSetText(tb, t.c_str());
                umgSetTextColor(tb, rr, gg, bb, 1.0f);
            }
            // v0.35+v0.39 — refresh native checkbox icons + reapply
            // visibility every tick (visibility-based swap — no flicker
            // since the visibility values stay the same across ticks).
            refreshCheckBoxIcons();
            applyTabContextVisibility();
            // Quiet re-apply (no logs every tick).
            if (m_cheatsTabActive)
            {
                for (auto& oc : m_gameplayNativeChildren)
                {
                    UObject* w = oc.widget.Get();
                    if (!w || !isObjectAlive(w)) continue;
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    if (visPtr && *visPtr != 1) *visPtr = 1; // Collapsed
                }
                for (auto& wp : m_modGameOptWidgets)
                {
                    UObject* w = wp.Get();
                    if (!w || !isObjectAlive(w)) continue;
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    if (visPtr && *visPtr != 1) *visPtr = 1;
                }
                for (auto& wp : m_cheatsContextWidgets)
                {
                    UObject* w = wp.Get();
                    if (!w || !isObjectAlive(w)) continue;
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    if (visPtr && *visPtr != 0) *visPtr = 0; // Visible
                }
            }
            else
            {
                for (auto& wp : m_cheatsContextWidgets)
                {
                    UObject* w = wp.Get();
                    if (!w || !isObjectAlive(w)) continue;
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    if (visPtr && *visPtr != 1) *visPtr = 1;
                }
            }
        }

        // Toggle which widget set is visible based on m_cheatsTabActive.
        void applyGameplayTabContext()
        {
            VLOG(STR("[SettingsUI] CP5-DBG — applyGameplayTabContext: cheats={}, native={}, modOpt={}, cheatsW={}\n"),
                 m_cheatsTabActive ? L"true" : L"false",
                 (int)m_gameplayNativeChildren.size(),
                 (int)m_modGameOptWidgets.size(),
                 (int)m_cheatsContextWidgets.size());
            if (m_cheatsTabActive)
            {
                setNativeGameplayChildrenHidden(true);
                setWidgetSetVisibility(m_modGameOptWidgets,    false);
                setWidgetSetVisibility(m_cheatsContextWidgets, true);
            }
            else
            {
                setNativeGameplayChildrenHidden(false);
                setWidgetSetVisibility(m_modGameOptWidgets,    true);
                setWidgetSetVisibility(m_cheatsContextWidgets, false);
            }
        }

        // CP5 v0.8 — pre-hook for navTabPressed.
        //
        // The framework's WBP_SettingsScreen has fixed per-tab member
        // widgets (WBP_GameplayTab, WBP_AudioTab, etc.) and switches
        // between them by tab name. There's no bound widget for "Cheats"
        // — clicking it would route to nothing. So we:
        //   1. Set m_cheatsTabActive = true when KeyName == "Cheats".
        //   2. Rewrite the KeyName parameter to "Gameplay" so the
        //      framework displays the Gameplay widget (our content host).
        //   3. Our injectModGameOptions handler reads m_cheatsTabActive
        //      and renders cheats-only content (native Gameplay UI +
        //      Mod Game Options collapsed; Cheats section visible).
        // For any other tab, clear the flag.
        // v0.11 — when "Cheats" is clicked:
        //   1. Find the SettingsScreen's WBP_GameplayTab member
        //   2. Force-inject our cheats content into it (if not already)
        //   3. Hide all native gameplay children + Mod Game Options
        //   4. Show only cheats content
        //   5. Rewrite KeyName -> "Gameplay" so the framework's tab
        //      switcher actually displays the Gameplay widget (which
        //      we've now turned into a cheats panel).
        // When any other tab is clicked: restore native children and
        // hide cheats content.
        // v0.17 — pre-hook rewrites KeyName "Cheats" -> "Gameplay" so
        // the framework's tab switcher displays the Gameplay widget
        // (which we visibility-swap to show cheats content). Post-hook
        // then calls navbar.SelectThisTab("Cheats") UFunction to fix
        // the highlight. GetWidgetSwitcher isn't a UFunction in this
        // binary so the SetActiveWidget approach was unreachable — we
        // go back to KeyName rewrite which is the only working path.
        void onNavTabPressedPre(UObject* /*context*/, UFunction* fn, void* parms)
        {
            // v0.48 — Cheats tab removed. Just track the flag for
            // legacy code paths; no KeyName rewrite.
            if (!fn || !parms) return;
            auto* p = findParam(fn, STR("KeyName"));
            if (!p) return;
            FName* pName = reinterpret_cast<FName*>(static_cast<uint8_t*>(parms) + p->GetOffset_Internal());
            std::wstring name;
            try { name = pName->ToString(); } catch (...) { return; }
            m_cheatsTabActive = false; // never active anymore
        }

        // CP5 v0.16 — post-hook on navTabPressed.
        //
        // Uses proper UFunction APIs throughout:
        //   - UFGKUIScreen.GetWidgetSwitcher() -> UWidgetSwitcher
        //   - UWidgetSwitcher.SetActiveWidget(UWidget*)
        //   - UWidget.SetVisibility(ESlateVisibility)
        //
        // For non-cheats tabs: framework already handled everything;
        // we just clean up cheats visibility (so they don't peek into
        // other tabs that briefly share the panel during transitions).
        void onNavTabPressedPost(UObject* context, UFunction* /*fn*/, void* /*parms*/)
        {
            if (!context || !isObjectAlive(context)) return;
            UObject* settingsScreen = nullptr;
            std::wstring ccls = safeClassName(context);
            if (ccls == STR("WBP_SettingsScreen_C")) settingsScreen = context;
            else if (ccls == STR("UI_WBP_NavBar_Build_C"))
            {
                UObject* cur = context;
                for (int i = 0; i < 10 && cur; ++i) {
                    if (safeClassName(cur) == STR("WBP_SettingsScreen_C")) { settingsScreen = cur; break; }
                    cur = cur->GetOuterPrivate();
                }
            }
            // v6.15.0 — was a fallback through m_settingsScreenAppendedFor;
            // field removed (only writer was deleted in v6.14.0).
            if (!settingsScreen || !isObjectAlive(settingsScreen)) return;

            VLOG(STR("[SettingsUI] CP5 — post-hook: cheats={}\n"),
                 m_cheatsTabActive ? L"YES" : L"no");

            auto* gpPtr = settingsScreen->GetValuePtrByPropertyNameInChain<UObject*>(STR("WBP_GameplayTab"));
            UObject* gameplayTab = gpPtr ? *gpPtr : nullptr;
            if (!gameplayTab || !isObjectAlive(gameplayTab)) return;

            // v0.46 — Option B: rebuild context content on every tab
            // click. forceInjectDualContent now sets up the panel
            // scaffolding (idempotent) AND respawns the active context's
            // widgets every call.
            forceInjectDualContent(gameplayTab);

            // Apply visibility per active context.
            applyDualContent();

            // v0.35 — apply tab-context visibility immediately.
            applyTabContextVisibility();

            if (!m_cheatsTabActive) return;

            // Highlight the Cheats button. Framework just ran
            // SelectThisTab("Gameplay") because we rewrote KeyName.
            // Override with SelectThisTab("Cheats") UFunction.
            auto* navPtr = settingsScreen->GetValuePtrByPropertyNameInChain<UObject*>(STR("UI_WBP_NavBar_Build"));
            UObject* navbar = navPtr ? *navPtr : nullptr;
            if (navbar && isObjectAlive(navbar))
            {
                if (auto* fnSel = navbar->GetFunctionByNameInChain(STR("SelectThisTab")))
                {
                    int sz = fnSel->GetParmsSize();
                    std::vector<uint8_t> b(sz, 0);
                    auto* p = findParam(fnSel, STR("KeyName"));
                    if (p)
                    {
                        RC::Unreal::FName cheats(STR("Cheats"), RC::Unreal::FNAME_Add);
                        std::memcpy(b.data() + p->GetOffset_Internal(), &cheats, sizeof(RC::Unreal::FName));
                        safeProcessEvent(navbar, fnSel, b.data());
                        VLOG(STR("[SettingsUI] CP5 — navbar.SelectThisTab('Cheats') called\n"));
                    }
                }
            }
        }
        // v6.14.0 — DELETED: legacy v0.15 highlight #if 0 block + orphan
        // helper `walkAndUpdateNavButtonHighlight`. Both were part of the
        // Cheats-tab nav highlight path that was replaced by
        // SetActiveWidget in v0.15 and superseded by Option C
        // (Cheats merged into Gameplay tab) in v0.48. ~135 lines removed.

        // ─────────────────────────────────────────────────────────
        // v0.11 — single-instance dual-content state.
        // We use the SettingsScreen's existing WBP_GameplayTab as the
        // host, and toggle two parallel widget sets (Mod Game Options
        // vs Cheats) plus the original native children.
        // ─────────────────────────────────────────────────────────
        FWeakObjectPtr m_dualInjectedFor;
        // Original native children captured on first inject.
        std::vector<FWeakObjectPtr> m_dualNativeChildren;
        // Mod Game Options widgets (heading + 7 buttons).
        std::vector<FWeakObjectPtr> m_dualModGameWidgets;
        // Cheats widgets (heading + 7 buttons).
        std::vector<FWeakObjectPtr> m_dualCheatsWidgets;

        // Walk Outer chain from a tab widget to find the owning
        // WBP_SettingsScreen_C.
        UObject* settingsScreenForTab(UObject* tab)
        {
            UObject* cur = tab;
            for (int i = 0; i < 10 && cur; ++i)
            {
                if (safeClassName(cur) == STR("WBP_SettingsScreen_C")) return cur;
                cur = cur->GetOuterPrivate();
            }
            // v6.15.0 — fallback returned m_settingsScreenAppendedFor; field
            // removed (only writer deleted in v6.14.0). Caller already
            // null-checks, so returning nullptr is safe.
            return nullptr;
        }

        void forceInjectDualContent(UObject* gameplayTab)
        {
            if (!gameplayTab || !isObjectAlive(gameplayTab)) return;
            if (!ensureFrontEndButtonClassCached(gameplayTab))
            {
                VLOG(STR("[SettingsUI] CP5 — FrontEndButton not cached, deferring inject\n"));
                return;
            }
            auto* ppPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("ParentPanel"));
            UObject* panel = ppPtr ? *ppPtr : nullptr;
            if (!panel || !isObjectAlive(panel))
            {
                VLOG(STR("[SettingsUI] CP5 — ParentPanel null on Gameplay\n"));
                return;
            }
            auto* wtPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : gameplayTab;

            // In-game detection: even the main menu has a Pawn (menu
            // animation pawn / camera placeholder). Check the Pawn's
            // class name — only the actual game character ("Dwarf"-class
            // pawn) signals an active gameplay session.
            bool isInGame = false;
            UObject* pawn = getPawn();
            std::wstring pawnCls = pawn ? safeClassName(pawn) : std::wstring();
            if (pawn && (pawnCls.find(STR("Dwarf")) != std::wstring::npos
                         || pawnCls.find(STR("FGK")) != std::wstring::npos))
                isInGame = true;
            UObject* ss = settingsScreenForTab(gameplayTab);
            VLOG(STR("[SettingsUI] CP5 — inject context: isInGame={}, pawnCls='{}', ss={:p}\n"),
                 isInGame ? L"YES" : L"no", pawnCls.c_str(), (void*)ss);

            // Main menu: leave the Gameplay tab completely native.
            if (!isInGame)
            {
                m_dualNativeChildren.clear();
                m_dualModGameWidgets.clear();
                m_dualCheatsWidgets.clear();
                m_dualInjectedFor = FWeakObjectPtr(gameplayTab);
                return;
            }

            // v0.48 — Option C: single Gameplay tab, all content cached.
            // After first inject for THIS gameplayTab, subsequent calls
            // early-return (no re-injection — duplicates eliminated).
            // If gameplayTab differs (new SettingsScreen instance), reset
            // the cache so a fresh inject runs for it.
            if (m_dualInjectedFor.Get() != gameplayTab)
                m_dualContentCached = false;
            if (m_dualInjectedFor.Get() == gameplayTab && m_dualContentCached)
                return;
            const bool setupAlreadyDone = (m_panelSetupFor.Get() == gameplayTab);

            // First-time setup OR rebuild: this block creates scrollbox
            // only if not already done.
            UObject* injectTarget = panel;
            UObject* scrollBox = setupAlreadyDone ? m_dualScrollBox.Get() : nullptr;
            if (setupAlreadyDone && scrollBox) injectTarget = scrollBox;
            UClass* scrollBoxCls = nullptr;
            // Skip the entire setup block if already done.
            if (setupAlreadyDone) goto skip_panel_setup;
            for (const wchar_t* path : {
                STR("/Script/UMG.ScrollBox"),
                STR("ScrollBox"),
                STR("/Script/UMG.Default__ScrollBox"),
            })
            {
                try {
                    scrollBoxCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
                } catch (...) { scrollBoxCls = nullptr; }
                if (scrollBoxCls) {
                    VLOG(STR("[SettingsUI] CP5 — ScrollBox class found via '{}'\n"), path);
                    break;
                }
            }
            if (!scrollBoxCls)
                VLOG(STR("[SettingsUI] CP5 — ScrollBox class NOT found, falling back to VBox\n"));
            if (scrollBoxCls)
            {
                // ScrollBox is a raw UMG widget (not UUserWidget) so
                // WidgetBlueprintLibrary::Create won't construct it.
                // Use StaticConstructObject with WidgetTree as outer.
                FStaticConstructObjectParameters sbP(scrollBoxCls, outer);
                try { scrollBox = UObjectGlobals::StaticConstructObject(sbP); }
                catch (...) { scrollBox = nullptr; }
                VLOG(STR("[SettingsUI] CP5 — ScrollBox construct: {:p}\n"), (void*)scrollBox);
                if (scrollBox)
                {
                    // Collect existing native children (UWidget pointers).
                    std::vector<UObject*> natives;
                    auto* slots = panel->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                    if (slots) {
                        for (int i = 0; i < slots->Num(); ++i) {
                            UObject* slot = (*slots)[i];
                            if (!slot) continue;
                            auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                            UObject* child = (cPtr && *cPtr) ? *cPtr : nullptr;
                            if (child && isObjectAlive(child)) natives.push_back(child);
                        }
                    }
                    // RemoveChild each native from panel via UPanelWidget
                    // RemoveChild UFunction.
                    int reparented = 0;
                    for (UObject* child : natives)
                    {
                        if (auto* fnRm = panel->GetFunctionByNameInChain(STR("RemoveChild")))
                        {
                            int sz = fnRm->GetParmsSize();
                            std::vector<uint8_t> b(sz, 0);
                            auto* p = findParam(fnRm, STR("Content"));
                            if (p) {
                                *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = child;
                                safeProcessEvent(panel, fnRm, b.data());
                            }
                        }
                        addToScrollBox(scrollBox, child);
                        ++reparented;
                    }
                    // Wrap the ScrollBox in a SizeBox with an explicit
                    // height override. The parent UVerticalBox has Auto
                    // sizing on its slots by default — Slot.Size=Fill
                    // alone is unreliable because the VerticalBox itself
                    // may be unbounded in its CanvasPanel slot. A SizeBox
                    // with HeightOverride forces a fixed visible height
                    // so the ScrollBox actually scrolls overflow.
                    UClass* sizeBoxCls = nullptr;
                    try {
                        sizeBoxCls = UObjectGlobals::StaticFindObject<UClass*>(
                            nullptr, nullptr, STR("/Script/UMG.SizeBox"));
                    } catch (...) {}
                    UObject* sizeBox = nullptr;
                    if (sizeBoxCls)
                    {
                        FStaticConstructObjectParameters sbxP(sizeBoxCls, outer);
                        try { sizeBox = UObjectGlobals::StaticConstructObject(sbxP); }
                        catch (...) { sizeBox = nullptr; }
                    }
                    if (sizeBox)
                    {
                        // 1400px to match the Key Mapping tab's
                        // designer-placed ScrollBox height.
                        jw_setSizeBoxOverride(sizeBox, 0.0f, 1400.0f);
                        // SizeBox.SetContent(scrollBox) via UFunction.
                        if (auto* setFn = sizeBox->GetFunctionByNameInChain(STR("SetContent")))
                        {
                            int sz = setFn->GetParmsSize();
                            std::vector<uint8_t> b(sz, 0);
                            auto* p = findParam(setFn, STR("InContent"));
                            if (!p) p = findParam(setFn, STR("Content"));
                            if (p)
                            {
                                *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = scrollBox;
                                safeProcessEvent(sizeBox, setFn, b.data());
                            }
                        }
                        appendToVerticalBox(panel, sizeBox);
                        VLOG(STR("[SettingsUI] CP5 — wrapped ScrollBox in SizeBox(1400h)\n"));
                    }
                    else
                    {
                        // Fallback if SizeBox class missing.
                        appendToVerticalBox(panel, scrollBox);
                        auto* slotsP = panel->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                        if (slotsP && slotsP->Num() > 0)
                        {
                            UObject* sbSlot = (*slotsP)[slotsP->Num() - 1];
                            if (sbSlot) setSlotSizeFill(sbSlot, 1.0f);
                        }
                    }
                    injectTarget = scrollBox;
                    m_dualScrollBox = FWeakObjectPtr(scrollBox);

                    // v0.43 — two VerticalBox containers inside the
                    // ScrollBox. Tab swap = toggle visibility of these
                    // two single widgets (reliable for single-widget
                    // hide; the previous per-row HBox visibility-collapse
                    // didn't propagate to children). Native sliders go in
                    // VBoxA (gameplay), action buttons + cheats in VBoxB.
                    UClass* vboxClsForCtx = nullptr;
                    try {
                        vboxClsForCtx = UObjectGlobals::StaticFindObject<UClass*>(
                            nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
                    } catch (...) {}
                    if (vboxClsForCtx)
                    {
                        // Gameplay context container.
                        FStaticConstructObjectParameters gP(vboxClsForCtx, outer);
                        UObject* gpVB = nullptr;
                        try { gpVB = UObjectGlobals::StaticConstructObject(gP); } catch (...) {}
                        if (gpVB) {
                            addToScrollBox(scrollBox, gpVB);
                            m_dualGameplayScroll = FWeakObjectPtr(gpVB);
                        }
                        // Cheats context container.
                        FStaticConstructObjectParameters cP(vboxClsForCtx, outer);
                        UObject* chVB = nullptr;
                        try { chVB = UObjectGlobals::StaticConstructObject(cP); } catch (...) {}
                        if (chVB) {
                            addToScrollBox(scrollBox, chVB);
                            m_dualCheatsScroll = FWeakObjectPtr(chVB);
                        }
                        // Move the reparented native sliders into the
                        // Gameplay VBox (they were just added to scrollBox
                        // as siblings of our two VBoxes — move them in).
                        if (gpVB)
                        {
                            for (UObject* nat : natives)
                            {
                                if (auto* fnRm = scrollBox->GetFunctionByNameInChain(STR("RemoveChild")))
                                {
                                    int sz = fnRm->GetParmsSize();
                                    std::vector<uint8_t> rb(sz, 0);
                                    auto* p = findParam(fnRm, STR("Content"));
                                    if (p) {
                                        *reinterpret_cast<UObject**>(rb.data() + p->GetOffset_Internal()) = nat;
                                        safeProcessEvent(scrollBox, fnRm, rb.data());
                                    }
                                }
                                appendToVerticalBox(gpVB, nat);
                            }
                        }
                        VLOG(STR("[SettingsUI] CP5 — created two VerticalBox containers (gp={:p}, ch={:p})\n"),
                             (void*)gpVB, (void*)chVB);
                    }
                    VLOG(STR("[SettingsUI] CP5 — reparented {} native widgets into ScrollBox\n"),
                         reparented);
                }
            }
            // v0.46 — record that panel setup is done so subsequent
            // tab-swap calls skip the scrollbox creation.
            m_panelSetupFor = FWeakObjectPtr(gameplayTab);
            m_cachedOuter   = FWeakObjectPtr(outer);
        skip_panel_setup:;

            // v0.43 — route widgets to one of two context containers.
            // addToGameplayCtx → gameplay tab content (native sliders +
            // Combat + Game Settings + Loaded Mods + No Coll/Peace).
            // addToCheatsCtx → cheats tab content (action buttons + God
            // Mode + Buffs).
            UObject* gpVB = m_dualGameplayScroll.Get();
            UObject* chVB = m_dualCheatsScroll.Get();
            auto addToGameplayCtx = [&](UObject* child) {
                if (gpVB) appendToVerticalBox(gpVB, child);
                else if (injectTarget == panel) appendToVerticalBox(panel, child);
                else addToScrollBox(injectTarget, child);
            };
            // v0.51 — Cheats tab is gone (Option C). Route everything
            // that previously went to the cheats context onto the
            // Gameplay tab instead, so God Mode + Buffs become visible.
            auto addToCheatsCtx = [&](UObject* child) {
                if (gpVB) appendToVerticalBox(gpVB, child);
                else if (chVB) appendToVerticalBox(chVB, child);
                else if (injectTarget == panel) appendToVerticalBox(panel, child);
                else addToScrollBox(injectTarget, child);
            };
            // Legacy alias — defaults to gameplay context.
            auto addToTarget = [&](UObject* child) { addToGameplayCtx(child); };

            m_dualModGameWidgets.clear();
            m_dualCheatsWidgets.clear();
            m_gameOptButtons.clear();
            m_cheatsButtons.clear();
            m_rowStatus.clear();
            m_buffToggleRows.clear();
            m_tweakCycleRows.clear();
            m_modPackRows.clear();
            m_checkBoxRows.clear();
            m_checkBoxLabels.clear();
            m_carouselRows.clear();
            m_carouselToggles.clear();
            // v0.32+v0.33 — cache the native checkbox + carousel classes
            // via the gameplay tab member widgets.
            ensureSettingsCheckBoxClassCached(gameplayTab);
            ensureSettingsCarouselClassCached(gameplayTab);

            // Capture native children. If we successfully reparented them
            // into the ScrollBox above, m_dualNativeChildren is already
            // populated indirectly — we recapture by walking the actual
            // current container (panel or scrollBox).
            m_dualNativeChildren.clear();
            UObject* nativeContainer = (injectTarget == scrollBox && scrollBox) ? scrollBox : panel;
            auto* slots = nativeContainer->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (slots)
            {
                for (int i = 0; i < slots->Num(); ++i)
                {
                    UObject* slot = (*slots)[i];
                    if (!slot) continue;
                    auto* cPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                    UObject* child = (cPtr && *cPtr) ? *cPtr : nullptr;
                    if (!child || !isObjectAlive(child)) continue;
                    // Skip the ScrollBox itself if it ended up here.
                    if (child == scrollBox) continue;
                    m_dualNativeChildren.push_back(FWeakObjectPtr(child));
                }
            }

            // Mod Game Options block — only injected when in-game.
            // These actions (Save Game, Unlock Recipes, etc.) need a
            // running session, so on the main-menu settings we skip
            // them entirely. Cheats are also game-state-dependent but
            // the user wants the Cheats tab visible everywhere; cheat
            // handlers no-op gracefully if no session.
            if (isInGame)
            {
                // v0.48 — All content merged onto Gameplay tab, no
                // Cheats context. "Game Options" header + action buttons.
                if (m_settingsSectionHeadingCls)
                    if (UObject* hd = spawnSectionHeading(STR("Game Options"))) {
                        addToTarget(hd);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                    }
                // Each row uses WBP_SettingsKeySelector_C (matches keymap
                // visual layout) with the right-side keyText overridden
                // to show the F12 status word + color, like the F12 menu.
                struct GSpec {
                    const wchar_t* label;
                    GameOptKind kind;
                    const wchar_t* statusText;
                    float r, g, b;
                };
                // v0.30 — labels per user spec: Title-Case button text,
                // toggleables show ON/OFF (no PEACE/FIGHT, no SAVE NOW).
                // v0.51 — 5 action buttons (Rename/Save/Unlock/Read All/
                // Clear All Buffs) moved to the in-game pause menu
                // (UI_WBP_EscapeMenu2_C → injectPauseMenuButtons). Only
                // the two toggle rows remain on the Gameplay tab.
                static const GSpec gSpecs[] = {
                    { STR("No Collision (Flying)"), GameOptKind::ToggleNoCollision, nullptr, 0,0,0 },
                    { STR("Peace Mode"),           GameOptKind::TogglePeace,       nullptr,         0,0,0 },
                };
                for (const auto& s : gSpecs)
                {
                    // v0.48 — spawn ALL gSpecs items (toggles + actions)
                    // since everything lives on the Gameplay tab now.
                    UObject* row = nullptr;
                    // v0.32 — toggle rows use native checkbox.
                    if (s.kind == GameOptKind::ToggleNoCollision)
                    {
                        row = spawnCheckBoxRow(s.label,
                            [this]() { return m_noCollisionWhileFlying; },
                            [this](bool newState) {
                                m_noCollisionWhileFlying = newState;
                                showOnScreen(newState ? L"No Collision Flying: ON" : L"No Collision Flying: OFF",
                                             2.0f, 0.3f, 0.8f, 1.0f);
                                saveConfig();
                            });
                    }
                    else if (s.kind == GameOptKind::TogglePeace)
                    {
                        row = spawnCheckBoxRow(s.label,
                            [this]() { return m_peaceModeEnabled; },
                            [this](bool /*newState*/) {
                                togglePeaceMode(); // also persists
                            });
                    }
                    else
                    {
                        // v0.33 — action button rows use a custom HBox
                        // with a real WBP_FrontEndButton on the right.
                        row = spawnActionButtonRow(outer, s.label,
                                                   s.statusText ? s.statusText : s.label,
                                                   s.kind);
                    }
                    if (!row) continue;
                    // v0.48 — all rows go to Gameplay panel.
                    addToTarget(row);
                    m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                }
            // v0.48 — same isInGame branch continues here. NoColl/Peace
            // toggles already handled in the gSpecs loop above. Below
            // adds NoCost/Instant Craft + Combat + Game Settings + Loaded Mods.
                // v0.32 — No Cost + Instant Craft as native checkboxes.
                {
                    int nT = 0;
                    const TweakEntry* tw = tweakEntries(nT);
                    if ((int)m_tweakCurrentIdx.size() != nT)
                        m_tweakCurrentIdx.assign(nT, 0);
                    for (int i = 0; i < nT; ++i)
                    {
                        if (tw[i].kind != TweakKind::SpecialNoCost &&
                            tw[i].kind != TweakKind::SpecialInstantCraft) continue;
                        const wchar_t* lbl = tw[i].label;
                        int idx = i;
                        UObject* row = spawnCheckBoxRow(lbl,
                            [this, idx]() -> bool {
                                return idx < (int)m_tweakCurrentIdx.size()
                                    && m_tweakCurrentIdx[idx] != 0;
                            },
                            [this, idx](bool /*newState*/) {
                                cycleTweakValue(idx); // 2-state cycle [0,1] = OFF/ON
                            });
                        if (!row) continue;
                        addToTarget(row);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                    }
                }

                // v0.27 — Combat section (5 buff toggles from F12 Combat).
                if (m_settingsSectionHeadingCls)
                    if (UObject* hd = spawnSectionHeading(STR("Combat"))) {
                        addToTarget(hd);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                    }
                {
                    int nC = 0;
                    const CheatEntry* ents = cheatEntries(nC);
                    bool inCombat = false;
                    for (int i = 0; i < nC; ++i)
                    {
                        if (ents[i].kind == CheatRowKind::SectionHeader) {
                            std::wstring hl = ents[i].label ? ents[i].label : STR("");
                            inCombat = (hl == STR("Combat"));
                            continue;
                        }
                        if (!inCombat) continue;
                        if (ents[i].kind != CheatRowKind::BuffToggle) continue;
                        UObject* row = spawnBuffToggleRow(i);
                        if (!row) continue;
                        addToTarget(row);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                    }
                }

                // v0.27 — Game Settings section (tweak cycles, excluding
                // the SpecialNoCost / SpecialInstantCraft already placed
                // in Mod Game Options).
                if (m_settingsSectionHeadingCls)
                    if (UObject* hd = spawnSectionHeading(STR("Game Settings"))) {
                        addToTarget(hd);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                    }
                {
                    int nT = 0;
                    const TweakEntry* tw = tweakEntries(nT);
                    if ((int)m_tweakCurrentIdx.size() != nT)
                        m_tweakCurrentIdx.assign(nT, 0);
                    for (int i = 0; i < nT; ++i)
                    {
                        if (tw[i].kind == TweakKind::SectionHeader)
                        {
                            std::wstring hl = tw[i].label ? tw[i].label : STR("");
                            if (hl == STR("Crafting")) continue;
                            if (m_settingsSectionHeadingCls)
                                if (UObject* hd = spawnSectionHeading(tw[i].label)) {
                                    addToTarget(hd);
                                    m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                                }
                            continue;
                        }
                        if (tw[i].kind != TweakKind::TweakRow) continue; // skip Specials
                        // v0.33 — try carousel first, fall back to keymap-row.
                        UObject* row = nullptr;
                        if (m_settingsCarouselCls)
                        {
                            std::vector<std::pair<std::wstring,std::wstring>> opts;
                            for (size_t k = 0; k < tw[i].cycleValues.size(); ++k)
                            {
                                std::wstring lbl;
                                if (k == 0) lbl = L"Default";
                                else
                                {
                                    wchar_t buf[32];
                                    if (tw[i].isMultiplier) swprintf_s(buf, L"%dx", tw[i].cycleValues[k]);
                                    else                    swprintf_s(buf, L"%d",  tw[i].cycleValues[k]);
                                    lbl = buf;
                                }
                                opts.emplace_back(lbl, std::to_wstring((int)k));
                            }
                            std::wstring cur = std::to_wstring(m_tweakCurrentIdx[i]);
                            row = spawnCarouselRow(i, tw[i].label, opts, cur);
                        }
                        if (!row) row = spawnTweakCycleRow(i);
                        if (!row) continue;
                        addToTarget(row);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                    }
                }

                // v0.51 — God Mode + Buffs sections (relocated to land
                // BEFORE the "Loaded Mods" section per user request).
                {
                    int nCheatEntries = 0;
                    const CheatEntry* cheats = cheatEntries(nCheatEntries);
                    if ((int)m_buffStates.size() != nCheatEntries)
                        m_buffStates.assign(nCheatEntries, false);
                    bool buffsHeaderEmitted = false;
                    bool inCombatSection = false;
                    bool inGodModeSection = false;
                    for (int i = 0; i < nCheatEntries; ++i)
                    {
                        const CheatEntry& e = cheats[i];
                        if (e.kind == CheatRowKind::ClearAllBtn) continue; // moved to pause menu
                        if (e.kind == CheatRowKind::SectionHeader)
                        {
                            std::wstring hl = e.label ? e.label : STR("");
                            if (hl == STR("Combat"))
                            {
                                inCombatSection = true;
                                inGodModeSection = false;
                                continue; // Combat handled by Gameplay tab
                            }
                            inCombatSection = false;
                            if (hl == STR("God-Mode"))
                            {
                                inGodModeSection = true;
                                if (UObject* hd = spawnSectionHeading(STR("God Mode"))) {
                                    addToTarget(hd); // v0.53 — direct to gpVB, no Cheats indirection
                                    m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                                }
                            }
                            else
                            {
                                inGodModeSection = false;
                                if (!buffsHeaderEmitted)
                                {
                                    buffsHeaderEmitted = true;
                                    if (UObject* hd = spawnSectionHeading(STR("Buffs"))) {
                                        addToTarget(hd); // v0.53
                                        m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                                    }
                                }
                            }
                            continue;
                        }
                        if (e.kind != CheatRowKind::BuffToggle) continue;
                        if (inCombatSection) continue; // Gameplay Combat section handles these
                        UObject* row = spawnBuffToggleRow(i);
                        if (!row) continue;
                        addToTarget(row); // v0.53 — guaranteed gameplay tab
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                    }
                    VLOG(STR("[SettingsUI] v0.53 — God Mode + Buffs block ran: nCheatEntries={}\n"),
                         nCheatEntries);
                }

                // v0.27 — Loaded Mods section (per-pack toggles).
                if (m_settingsSectionHeadingCls)
                    if (UObject* hd = spawnSectionHeading(STR("Loaded Mods"))) {
                        addToTarget(hd);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(hd));
                    }
                {
                    if (m_ftGameModEntries.empty())
                        m_ftGameModEntries = discoverGameMods();
                    for (int i = 0; i < (int)m_ftGameModEntries.size(); ++i)
                    {
                        const auto& g = m_ftGameModEntries[i];
                        const std::string& dn = !g.title.empty() ? g.title : g.name;
                        std::wstring dnW = utf8ToWide(dn);
                        UObject* row = spawnModPackRow(i, dnW, g.enabled);
                        if (!row) continue;
                        addToTarget(row);
                        m_dualModGameWidgets.push_back(FWeakObjectPtr(row));
                    }
                }
            }

            // Defunct CSpec block kept compiling for now — no rows
            // produced because cSpecs[] is empty.
            struct CSpec {
                const wchar_t* label; CheatKind kind;
                const wchar_t* statusText; float r, g, b;
            };
            static const CSpec cSpecs[] = {
                { nullptr, CheatKind::Unlock, nullptr, 0,0,0 } // sentinel
            };
            const bool useKeySelectorRows = (m_settingsKeySelectorCls != nullptr);
            for (const auto& s : cSpecs)
            {
                if (!s.label) continue;
                UObject* row = nullptr;
                if (useKeySelectorRows)
                {
                    // Settings-row formatted like keymap rows.
                    row = jw_createGameWidget(m_settingsKeySelectorCls);
                    if (row)
                    {
                        if (auto* namePtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("OptionName")))
                            *namePtr = FText(s.label);
                        if (auto* tbPtr = row->GetValuePtrByPropertyNameInChain<UObject*>(STR("OptionNameTextBlock")))
                            if (UObject* tb = *tbPtr) umgSetText(tb, s.label);
                    }
                }
                if (!row)
                {
                    // Fallback: plain FrontEndButton if KeySelector class
                    // not yet resolved.
                    row = jw_createGameWidget(m_frontEndButtonCls);
                    if (!row) continue;
                    if (auto* lblPtr = row->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel")))
                        *lblPtr = FText(s.label);
                    if (auto* fn2 = row->GetFunctionByNameInChain(STR("UpdateTextLabel")))
                    {
                        std::vector<uint8_t> b(fn2->GetParmsSize(), 0);
                        auto* pp = findParam(fn2, STR("Text"));
                        if (pp) {
                            FText t(s.label);
                            std::memcpy(b.data() + pp->GetOffset_Internal(), &t, sizeof(FText));
                            safeProcessEvent(row, fn2, b.data());
                        }
                    }
                }
                addToTarget(row);
                CheatButton entry; entry.widget = FWeakObjectPtr(row); entry.kind = s.kind;
                m_cheatsButtons.push_back(entry);
                m_dualCheatsWidgets.push_back(FWeakObjectPtr(row));

                // F12-style right-side status text. Toggleables update
                // dynamically; static actions show a fixed colored word.
                if (useKeySelectorRows)
                {
                    if (s.kind == CheatKind::ToggleNoCollision)
                    {
                        registerRowStatus(row, L"OFF", 0.65f, 0.65f, 0.65f,
                            [this](std::wstring& t, float& r, float& g, float& b) {
                                if (m_noCollisionWhileFlying) { t = L"ON";  r=0.31f; g=0.78f; b=0.47f; }
                                else                          { t = L"OFF"; r=0.65f; g=0.65f; b=0.65f; }
                            });
                    }
                    else if (s.kind == CheatKind::TogglePeace)
                    {
                        registerRowStatus(row, L"FIGHT", 0.9f, 0.45f, 0.25f,
                            [this](std::wstring& t, float& r, float& g, float& b) {
                                if (m_peaceModeEnabled) { t = L"PEACE"; r=0.31f; g=0.78f; b=0.47f; }
                                else                    { t = L"FIGHT"; r=0.9f;  g=0.45f; b=0.25f; }
                            });
                    }
                    else if (s.statusText)
                    {
                        registerRowStatus(row, s.statusText, s.r, s.g, s.b);
                    }
                }
            }

            VLOG(STR("[SettingsUI] CP5 — injected (Option C): native={}, modGame={}, cheats={}\n"),
                 (int)m_dualNativeChildren.size(),
                 (int)m_dualModGameWidgets.size(),
                 (int)m_dualCheatsWidgets.size());
            m_dualInjectedFor = FWeakObjectPtr(gameplayTab);
            m_dualContentCached = true; // v0.48 — early-return on repeat calls
        }

        // Toggle visibility of the three widget sets (native gameplay
        // children, Mod Game Options, Cheats) based on m_cheatsTabActive.
        // Uses the proper UWidget::SetVisibility(ESlateVisibility) UFunction.
        // ESlateVisibility values: 0=Visible, 1=Collapsed, 2=Hidden,
        //                          3=HitTestInvisible, 4=SelfHitTestInvisible.
        void applyDualContent()
        {
            constexpr uint8_t SHOW = 0; // ESlateVisibility::Visible
            constexpr uint8_t HIDE = 1; // ESlateVisibility::Collapsed
            const uint8_t nativeV  = m_cheatsTabActive ? HIDE : SHOW;
            const uint8_t modGameV = m_cheatsTabActive ? HIDE : SHOW;
            const uint8_t cheatsV  = m_cheatsTabActive ? SHOW : HIDE;
            for (auto& wp : m_dualNativeChildren)  setWidgetVisibility(wp.Get(), nativeV);
            for (auto& wp : m_dualModGameWidgets)  setWidgetVisibility(wp.Get(), modGameV);
            for (auto& wp : m_dualCheatsWidgets)   setWidgetVisibility(wp.Get(), cheatsV);
        }

        // Dispatch CP4 button clicks. Called from ProcessEvent post-hook
        // when WBP_FrontEndButton_C::OnMenuButtonClicked fires.
        void onModGameOptionClicked(UObject* clickedButton)
        {
            if (!clickedButton) return;
            // v0.49 — match by direct widget OR by outer-chain (when the
            // event fires on the inner UButton, walk up to find the
            // FrontEndButton we tracked).
            UObject* matched = nullptr;
            GameOptKind matchedKind = GameOptKind::None;
            UObject* cur = clickedButton;
            for (int hop = 0; hop < 6 && cur; ++hop)
            {
                for (auto& g : m_gameOptButtons) {
                    if (g.widget.Get() == cur) { matched = cur; matchedKind = g.kind; break; }
                }
                if (matched) break;
                cur = cur->GetOuterPrivate();
            }
            static int s_diagCount = 0;
            if (s_diagCount < 16) {
                VLOG(STR("[SettingsUI] CP4-DBG — onModGameOptionClicked: btn={:p} cls={}, list-size={}, matched={:p} kind={}\n"),
                     (void*)clickedButton,
                     safeClassName(clickedButton).c_str(),
                     (int)m_gameOptButtons.size(),
                     (void*)matched, (int)matchedKind);
                ++s_diagCount;
            }
            if (!matched) return;
            // Use the legacy iteration path with `matched` as the target.
            for (auto& g : m_gameOptButtons)
            {
                if (g.widget.Get() != matched) continue;
                VLOG(STR("[SettingsUI] CP4 — button clicked kind={}\n"), (int)g.kind);
                switch (g.kind)
                {
                    case GameOptKind::Rename:
                        showRenameDialog();
                        break;
                    case GameOptKind::Save:
                        triggerSaveGame();
                        break;
                    case GameOptKind::Unlock:
                        unlockAllAvailableRecipes();
                        break;
                    case GameOptKind::ReadAll:
                        markAllLoreRead();
                        break;
                    case GameOptKind::ClearBuffs:
                        clearAllBuffs();
                        break;
                    case GameOptKind::ToggleNoCollision:
                        m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                        showOnScreen(m_noCollisionWhileFlying ? L"No Collision Flying: ON" : L"No Collision Flying: OFF",
                                     2.0f, 0.3f, 0.8f, 1.0f);
                        saveConfig();
                        break;
                    case GameOptKind::TogglePeace:
                        togglePeaceMode();
                        break;
                    default:
                        break;
                }
                return;
            }
        }

        // ────────────────────────────────────────────────────────────────
        // CP5 — repurpose the "legal" tab as our "Cheats" tab.
        //
        // Approach:
        //   1. On WBP_SettingsScreen.OnAfterShow, walk tabArray, find the
        //      entry whose Name FName is "legal", rewrite its DisplayName
        //      FText to "Cheats" so the navbar reads "Cheats".
        //   2. On WBP_LegalTab_C.OnAfterShow, find its content panel,
        //      collapse the native widgets (EULA/Privacy/TOS/Telemetry),
        //      and inject our own Cheats UI.
        //
        // No new tabArray entries (avoiding the TSoftClassPtr construction
        // problem). Trade-off: legal screen is no longer accessible. The
        // Privacy/EULA/TOS URLs are still in the binary so users can find
        // them via web search if needed.
        // ────────────────────────────────────────────────────────────────

        FWeakObjectPtr m_cheatsTabInjectedFor;
        // v6.15.0 — m_settingsScreenAppendedFor removed (writer deleted
        // in v6.14.0; fallback readers updated to nullptr).
        // Index of our appended Cheats tab in the tabArray (typically 7
        // since the array shipped with 7 entries 0..6).
        int m_cheatsTabIndex{-1};
        // Set true while the framework is in the middle of spawning the
        // content widget for our cheats tab. Cleared after we inject.
        // Used to differentiate "this WBP_LegalTab_C OnAfterShow is the
        // cheats instance" from "this is the actual legal instance".
        bool m_cheatsTabExpectedNext{false};

        // CP5 v0.10 — diagnostic flag set by onNavTabPressedPre when
        // user clicks the "Cheats" navbar button. Used for log
        // correlation; the actual rendering decision is made per-instance
        // (cheats host vs regular Gameplay) in injectModGameOptions.
        bool m_cheatsTabActive{false};
        // Tracks which Cheats-host widget instance we've already injected
        // into. Idempotent guard.
        FWeakObjectPtr m_cheatsHostInjectedFor;

        // v0.26 — F12-style status text for action rows. Each row using
        // WBP_SettingsKeySelector_C as its visual base has its keyText
        // TextBlock overridden with a contextual word ("RENAME", "SAVE NOW",
        // "UNLOCK", "ON"/"OFF", "PEACE"/"FIGHT", etc.) instead of a key
        // chord. The tick-reapply loop keeps that text persistent through
        // the BP's selecting-state machine resets.
        struct RowStatus {
            FWeakObjectPtr selector;       // WBP_SettingsKeySelector_C
            std::wstring text;             // current display word
            float r{1.0f}, g{1.0f}, b{1.0f};
            // Dynamic provider — if set, called every reapply tick to
            // recompute the text+color from current state.
            std::function<void(std::wstring&, float&, float&, float&)> dyn;
        };
        std::vector<RowStatus> m_rowStatus;

        // v0.37 — track the ScrollBox we inject content into so tab
        // context switches can RemoveChild / AddChild widgets directly
        // (visibility swap was unreliable).
        FWeakObjectPtr m_dualScrollBox;
        // v0.46 — Option B: rebuild context content on every tab swap.
        FWeakObjectPtr m_panelSetupFor;
        FWeakObjectPtr m_cachedOuter;
        // v0.47 — Cached UWidgetSwitcher containing two VBoxes
        // (gameplay context + cheats context). Tab swap = single
        // SetActiveWidgetIndex call. Content stays cached after first
        // injection.
        FWeakObjectPtr m_dualWidgetSwitcher;
        bool m_dualContentCached{false};
        // v0.49b — Pause menu (UI_WBP_EscapeMenu2_C) injection. Tracks
        // which menu instance we've injected mod action buttons into.
        FWeakObjectPtr m_pauseMenuInjectedFor;
        // v0.41 — hidden VerticalBox where we park widgets that should
        // not be visible in the current tab context. Sibling of the
        // ScrollBox, has Visibility=Collapsed so its contents don't
        // render. Acts as a GC root for the parked widgets.
        FWeakObjectPtr m_dualHiddenStash;
        // v0.43 — two-ScrollBox approach. Gameplay-context content goes
        // in m_dualGameplayScroll, Cheats-context in m_dualCheatsScroll.
        // Tab swap = toggle visibility on these two single widgets,
        // which is reliable (single-widget hide vs composite-row hide).
        FWeakObjectPtr m_dualGameplayScroll;
        FWeakObjectPtr m_dualCheatsScroll;

        // v0.27 — buff toggle rows (Cheats God Mode + Buffs sections,
        // and Gameplay Combat section). Each row maps to an index in
        // cheatEntries[]. Click → toggleBuffEntry(idx).
        struct BuffToggleRow { FWeakObjectPtr selector; int cheatEntryIdx; };
        std::vector<BuffToggleRow> m_buffToggleRows;

        // v0.27 — tweak cycle rows (Gameplay Game Settings section).
        // Each row maps to an index in tweakEntries[]. Click →
        // cycleTweakValue(idx).
        struct TweakCycleRow { FWeakObjectPtr selector; int tweakIdx; };
        std::vector<TweakCycleRow> m_tweakCycleRows;

        // v0.27 — Loaded Mods rows (Gameplay Loaded Mods section).
        // Each row maps to an index in m_ftGameModEntries.
        struct ModPackRow { FWeakObjectPtr selector; int modIdx; };
        std::vector<ModPackRow> m_modPackRows;
        // Deadline (ms via GetTickCount64) until which tickReapplyCheatsContext
        // should keep refreshing the navbar selection. Set by pre-hook so
        // the highlight repaints even if framework writes stale state late.
        uint64_t m_cheatsHighlightRefreshDeadline{0};
        // v6.15.0 — m_cheatsHostNativeChildren removed (vector was
        // write-only; never iterated for restore).
        // Legacy fields — retained for back-compat with helpers but not
        // populated under the v0.10 per-instance approach.
        struct OriginalChild { FWeakObjectPtr widget; uint8_t origVis; };
        std::vector<OriginalChild> m_gameplayNativeChildren;
        std::vector<FWeakObjectPtr> m_cheatsContextWidgets;
        std::vector<FWeakObjectPtr> m_modGameOptWidgets;

        // Append a new "Cheats" tab to WBP_SettingsScreen.tabArray.
        //
        // Approach:
        //  1. Ensure ArrayMax has spare capacity (Num < Max). If not, we
        //     can't safely realloc the engine's TArray buffer from outside.
        //  2. Copy bytes of an existing entry as a template — the legal
        //     entry is the simplest (single content widget, no complex state).
        //     We get the right WidgetClass + TabConfig defaults that way.
        //  3. Overwrite Name FName -> "Cheats" and DisplayName FText -> "Cheats"
        //     at offsets 0x00 and 0x08.
        //  4. Increment ArrayNum.
        //
        // Idempotent: tracked by m_settingsScreenAppendedFor weak ref.
        // Append to a single named TArray<FFGKUITab>. Returns true on
        // success (tab appended or already present in this run).
        // v6.14.0 — DELETED: appendCheatsTabToNamedArray (~95 lines).
        // Was the worker function for appendCheatsTabToArray's tabArray
        // injection. Both removed when Cheats merged into Gameplay tab
        // (v0.48 / Option C). The function has had no live callers since.

        // v6.15.0 — appendCheatsTabToArray DELETED (was a stub since
        // v6.14.0; both callers in dllmain.cpp removed in v6.15.0).

        // Inject Cheats content into the legal tab's widget tree.
        // Pattern mirrors injectModGameOptions but adds buttons that
        // dispatch to our existing F12 menu cheat actions.
        enum class CheatKind { None, Unlock, ReadAll, ClearBuffs, TogglePeace,
                                ToggleNoCollision, RenameChar, SaveGame };
        struct CheatButton { FWeakObjectPtr widget; CheatKind kind; };
        std::vector<CheatButton> m_cheatsButtons;

        void injectCheatsTabContent(UObject* legalTab)
        {
            if (!legalTab || !isObjectAlive(legalTab)) return;
            UObject* prev = m_cheatsTabInjectedFor.Get();
            if (prev == legalTab) return;

            // Collapse the native legal widgets so the panel is clean for
            // our injection.
            for (const wchar_t* memberName : {
                STR("EULA_Button"),
                STR("PrivacyPolicy_Button"),
                STR("TOS_Button"),
                STR("Telemetry"),
                STR("Divider"),
                STR("Divider_1"),
                STR("WebBrowser"),
            })
            {
                if (auto* p = legalTab->GetValuePtrByPropertyNameInChain<UObject*>(memberName))
                {
                    if (UObject* w = *p) setWidgetVisibility(w, 1); // Collapsed
                }
            }

            // Find a panel to inject into. WBP_LegalTab probably has a
            // root VerticalBox or CanvasPanel — try common names then fall
            // back to walking the WidgetTree.
            UObject* panel = nullptr;
            for (const wchar_t* candidate : {
                STR("ParentPanel"), STR("RootPanel"), STR("VerticalBox"),
                STR("ContentBox"), STR("ScrollBox"),
            })
            {
                if (auto* p = legalTab->GetValuePtrByPropertyNameInChain<UObject*>(candidate))
                {
                    if (UObject* w = *p) { panel = w; break; }
                }
            }
            if (!panel)
            {
                auto* wtPtr = legalTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                UObject* wt = wtPtr ? *wtPtr : nullptr;
                if (wt)
                {
                    auto* rootPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    panel = rootPtr ? *rootPtr : nullptr;
                }
            }
            if (!panel)
            {
                VLOG(STR("[SettingsUI] CP5 — couldn't locate a panel inside WBP_LegalTab\n"));
                return;
            }

            if (!ensureFrontEndButtonClassCached(legalTab))
            {
                VLOG(STR("[SettingsUI] CP5 — FrontEndButton UClass not yet resolvable\n"));
                return;
            }

            auto* wtPtr = legalTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : legalTab;

            m_cheatsButtons.clear();

            if (m_settingsSectionHeadingCls)
            {
                if (UObject* hd = spawnSectionHeading(STR("Cheats")))
                    appendToVerticalBox(panel, hd);
            }

            struct Spec { const wchar_t* label; CheatKind kind; };
            static const Spec specs[] = {
                { STR("Unlock All Recipes"),   CheatKind::Unlock },
                { STR("Mark All Lore Read"),   CheatKind::ReadAll },
                { STR("Clear All Buffs"),      CheatKind::ClearBuffs },
                { STR("Toggle Peace Mode"),    CheatKind::TogglePeace },
                { STR("Toggle No-Collision Flying"), CheatKind::ToggleNoCollision },
                { STR("Rename Character"),     CheatKind::RenameChar },
                { STR("Save Game"),            CheatKind::SaveGame },
            };
            int added = 0;
            for (const auto& s : specs)
            {
                UObject* btn = jw_createGameWidget(m_frontEndButtonCls);
                if (!btn) continue;
                if (auto* lblPtr = btn->GetValuePtrByPropertyNameInChain<FText>(STR("ButtonLabel")))
                    *lblPtr = FText(s.label);
                if (auto* fn = btn->GetFunctionByNameInChain(STR("UpdateTextLabel")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    auto* p = findParam(fn, STR("Text"));
                    if (p) {
                        FText t(s.label);
                        std::memcpy(b.data() + p->GetOffset_Internal(), &t, sizeof(FText));
                        safeProcessEvent(btn, fn, b.data());
                    }
                }
                appendToVerticalBox(panel, btn);
                CheatButton entry; entry.widget = FWeakObjectPtr(btn); entry.kind = s.kind;
                m_cheatsButtons.push_back(entry);
                ++added;
            }
            VLOG(STR("[SettingsUI] CP5 — injected {} cheat buttons into Legal tab\n"), added);
            m_cheatsTabInjectedFor = FWeakObjectPtr(legalTab);
        }

        // Click dispatch — called from ProcessEvent post-hook on
        // OnMenuButtonClicked. Same hook covers CP4 buttons; this handler
        // just adds the Cheats tab buttons.
        // Returns true if the selector matches one of our action rows
        // (Cheats or Mod Game Options) — fires the action and tells the
        // caller to skip the keymap-rebind path.
        bool maybeFireCheatFromSelector(UObject* selector)
        {
            if (!selector) return false;
            // Cheats rows.
            for (auto& b : m_cheatsButtons)
            {
                if (b.widget.Get() != selector) continue;
                VLOG(STR("[SettingsUI] CP5 — cheat-via-selector clicked, kind={}\n"),
                     (int)b.kind);
                switch (b.kind)
                {
                    case CheatKind::Unlock:           unlockAllAvailableRecipes(); break;
                    case CheatKind::ReadAll:          markAllLoreRead(); break;
                    case CheatKind::ClearBuffs:       clearAllBuffs(); break;
                    case CheatKind::TogglePeace:      togglePeaceMode(); break;
                    case CheatKind::ToggleNoCollision:
                        m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                        showOnScreen(m_noCollisionWhileFlying ? L"No Collision Flying: ON" : L"No Collision Flying: OFF",
                                     2.0f, 0.3f, 0.8f, 1.0f);
                        saveConfig();
                        break;
                    case CheatKind::RenameChar:       showRenameDialog(); break;
                    case CheatKind::SaveGame:         triggerSaveGame(); break;
                    default: break;
                }
                if (auto* curPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                    std::memset(curPtr, 0, 32);
                return true;
            }
            // Mod Game Options rows.
            for (auto& g : m_gameOptButtons)
            {
                if (g.widget.Get() != selector) continue;
                VLOG(STR("[SettingsUI] CP5 — gameopt-via-selector clicked, kind={}\n"),
                     (int)g.kind);
                switch (g.kind)
                {
                    case GameOptKind::Rename:           showRenameDialog(); break;
                    case GameOptKind::Save:             triggerSaveGame(); break;
                    case GameOptKind::Unlock:           unlockAllAvailableRecipes(); break;
                    case GameOptKind::ReadAll:          markAllLoreRead(); break;
                    case GameOptKind::ClearBuffs:       clearAllBuffs(); break;
                    case GameOptKind::ToggleNoCollision:
                        m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                        showOnScreen(m_noCollisionWhileFlying ? L"No Collision Flying: ON" : L"No Collision Flying: OFF",
                                     2.0f, 0.3f, 0.8f, 1.0f);
                        saveConfig();
                        break;
                    case GameOptKind::TogglePeace:      togglePeaceMode(); break;
                    default: break;
                }
                if (auto* curPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                    std::memset(curPtr, 0, 32);
                return true;
            }
            // v0.27 — Buff toggle rows (Cheats / Combat).
            for (auto& b : m_buffToggleRows)
            {
                if (b.selector.Get() != selector) continue;
                VLOG(STR("[SettingsUI] CP5 — buff-toggle clicked, idx={}\n"), b.cheatEntryIdx);
                toggleBuffEntry(b.cheatEntryIdx);
                if (auto* curPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                    std::memset(curPtr, 0, 32);
                return true;
            }
            // v0.27 — Tweak cycle rows (Game Settings).
            for (auto& t : m_tweakCycleRows)
            {
                if (t.selector.Get() != selector) continue;
                VLOG(STR("[SettingsUI] CP5 — tweak-cycle clicked, idx={}\n"), t.tweakIdx);
                cycleTweakValue(t.tweakIdx);
                if (auto* curPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                    std::memset(curPtr, 0, 32);
                return true;
            }
            // v0.27 — Loaded Mods rows.
            for (auto& m : m_modPackRows)
            {
                if (m.selector.Get() != selector) continue;
                if (m.modIdx >= 0 && m.modIdx < (int)m_ftGameModEntries.size())
                {
                    m_ftGameModEntries[m.modIdx].enabled = !m_ftGameModEntries[m.modIdx].enabled;
                    saveGameMods(m_ftGameModEntries);
                    VLOG(STR("[SettingsUI] CP5 — modpack[{}].enabled={}\n"),
                         m.modIdx, m_ftGameModEntries[m.modIdx].enabled ? L"true" : L"false");
                }
                if (auto* curPtr = selector->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentSelectedKey")))
                    std::memset(curPtr, 0, 32);
                return true;
            }
            return false;
        }

        void onCheatsTabButtonClicked(UObject* clickedButton)
        {
            if (!clickedButton) return;
            for (auto& b : m_cheatsButtons)
            {
                if (b.widget.Get() != clickedButton) continue;
                VLOG(STR("[SettingsUI] CP5 — cheat button clicked kind={}\n"), (int)b.kind);
                switch (b.kind)
                {
                    case CheatKind::Unlock:           unlockAllAvailableRecipes(); break;
                    case CheatKind::ReadAll:          markAllLoreRead(); break;
                    case CheatKind::ClearBuffs:       clearAllBuffs(); break;
                    case CheatKind::TogglePeace:      togglePeaceMode(); break;
                    case CheatKind::ToggleNoCollision:
                        m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                        showOnScreen(m_noCollisionWhileFlying ? L"No Collision Flying: ON" : L"No Collision Flying: OFF",
                                     2.0f, 0.3f, 0.8f, 1.0f);
                        saveConfig();
                        break;
                    case CheatKind::RenameChar:       showRenameDialog(); break;
                    case CheatKind::SaveGame:         triggerSaveGame(); break;
                    default: break;
                }
                return;
            }
        }

        // v6.15.0 — onInitializeNavBarPre DELETED (~80 lines). Was the
        // CP5 v0.4 NavBar pre-hook for Cheats-tab insertion via clone of
        // legal tab; superseded by Option C in v0.48 (Cheats merged
        // into Gameplay tab). PE pre-hook callsite in dllmain.cpp also
        // removed in this version.

        // CP5 v0.3 — post-hook for WBP_SettingsScreen.GetNavBarTabs.
        //
        // The function signature is:
        //   void GetNavBarTabs(bool InGame, TArray<FFGKUITab>& tabArray);
        //
        // After the function fills its OutArray, we append our Cheats
        // entry to it. The navbar widget then renders 8 buttons.
        //
        // We construct the new entry by cloning bytes from an existing
        // entry (the legal one) and overwriting Name + DisplayName.
        // If the OutArray's TArray buffer doesn't have spare capacity,
        // we reallocate via FMemory.
        void onGetNavBarTabsPost(UObject* settingsScreen, UFunction* fn, void* parms)
        {
            if (!fn || !parms) return;
            // Find the OutArray param by name.
            auto* p = findParam(fn, STR("tabArray"));
            if (!p) return;
            uint8_t* parmsBytes = static_cast<uint8_t*>(parms);
            TArray<uint8_t>* outArr = reinterpret_cast<TArray<uint8_t>*>(parmsBytes + p->GetOffset_Internal());
            if (!outArr) return;
            int n = outArr->Num();
            int maxN = outArr->Max();
            uint8_t* base = outArr->GetData();
            if (!base || n <= 0) return;
            constexpr int kStride = 0xE8;

            // Skip if Cheats already in this array (double-add guard).
            // Also find legal entry to clone from.
            int legalIdx = -1;
            for (int i = 0; i < n && i < 16; ++i)
            {
                uint8_t* entry = base + (size_t)i * kStride;
                FName* fnEntry = reinterpret_cast<FName*>(entry + 0x00);
                std::wstring name;
                try { name = fnEntry->ToString(); } catch (...) { continue; }
                if (name == L"Cheats") return;
                if (name == L"legal" || name == L"Legal") legalIdx = i;
            }
            if (legalIdx < 0) return;

            // Capacity grow if needed (same FMemory technique as appendCheatsTabToArray).
            uint8_t* writeBase = base;
            if (n >= maxN)
            {
                int newCapacity = maxN < 8 ? 8 : maxN * 2;
                size_t newBytes = (size_t)newCapacity * kStride;
                uint8_t* newBuf = static_cast<uint8_t*>(FMemory::Malloc(newBytes, 8));
                if (!newBuf) return;
                std::memset(newBuf, 0, newBytes);
                std::memcpy(newBuf, base, (size_t)n * kStride);
                struct TAH { uint8_t* Data; int32_t Num; int32_t Max; };
                TAH* hdr = reinterpret_cast<TAH*>(outArr);
                uint8_t* oldBuf = hdr->Data;
                hdr->Data = newBuf;
                hdr->Max = newCapacity;
                if (oldBuf) FMemory::Free(oldBuf);
                writeBase = newBuf;
            }

            uint8_t* dst = writeBase + (size_t)n * kStride;
            uint8_t* src = writeBase + (size_t)legalIdx * kStride;
            std::memcpy(dst, src, kStride);
            RC::Unreal::FName cheatsName(STR("Cheats"), RC::Unreal::FNAME_Add);
            std::memcpy(dst + 0x00, &cheatsName, sizeof(RC::Unreal::FName));
            FText cheatsDisplay(L"Cheats");
            std::memcpy(dst + 0x08, &cheatsDisplay, sizeof(FText));
            outArr->SetNum(n + 1, false);

            static int s_loggedTimes = 0;
            if (s_loggedTimes < 3)
            {
                VLOG(STR("[SettingsUI] CP5 — GetNavBarTabs post-hook: appended Cheats (was {} entries, now {})\n"),
                     n, n + 1);
                ++s_loggedTimes;
            }
        }
