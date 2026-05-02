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
            struct Spec { const wchar_t* label; const wchar_t* iniKey; int bindIdx; };
            static const Spec specs[] = {
                { STR("Set Rotation"),       STR("ModBind.SetRotation"),     8  /* BIND_ROTATION */ },
                { STR("Pitch Rotate"),       STR("ModBind.PitchRotate"),     22 /* BIND_PITCH_ROTATE */ },
                { STR("Roll Rotate"),        STR("ModBind.RollRotate"),      23 /* BIND_ROLL_ROTATE */ },
                { STR("Snap Toggle"),        STR("ModBind.SnapToggle"),      9  /* BIND_SNAP */ },
                { STR("Integrity Check"),    STR("ModBind.IntegrityCheck"),  10 /* unused slot */ },
                { STR("Invisible Dwarf"),    STR("ModBind.InvisibleDwarf"),  11 },
                { STR("Flying Dwarf"),       STR("ModBind.FlyingDwarf"),     -1 },
                { STR("Target"),             STR("ModBind.Target"),          12 /* BIND_TARGET */ },
                { STR("Duplicate Target"),   STR("ModBind.DuplicateTarget"), -1 },
                { STR("Remove Single"),      STR("ModBind.RemoveSingle"),    14 },
                { STR("Undo Remove"),        STR("ModBind.UndoRemove"),      15 },
                { STR("Remove All"),         STR("ModBind.RemoveAll"),       16 },
                { STR("Trash Item"),         STR("ModBind.TrashItem"),       19 /* BIND_TRASH_ITEM */ },
                { STR("Replenish Item"),     STR("ModBind.ReplenishItem"),   20 /* BIND_REPLENISH_ITEM */ },
                { STR("Remove Attributes"),  STR("ModBind.RemoveAttributes"),21 /* BIND_REMOVE_ATTRS */ },
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

            // ── Mod Key Bindings section (15 rows) ──────────────────────
            struct ModSpec { const wchar_t* label; int bindIdx; const char* iniKey; };
            static const ModSpec mods[] = {
                { STR("Set Rotation"),       8,  "Rotation" },
                { STR("Pitch Rotate"),       22, "PitchRotate" },
                { STR("Roll Rotate"),        23, "RollRotate" },
                { STR("Snap Toggle"),        9,  "SnapToggle" },
                { STR("Integrity Check"),    10, "StabilityCheck" },
                { STR("Invisible Dwarf"),    11, "SuperDwarf" },
                { STR("Flying Dwarf"),       -1, "FlyingDwarf" },
                { STR("Target"),             12, "Target" },
                { STR("Duplicate Target"),   -1, "DuplicateTarget" },
                { STR("Remove Single"),      14, "RemoveTarget" },
                { STR("Undo Remove"),        15, "UndoLast" },
                { STR("Remove All"),         16, "RemoveAll" },
                { STR("Trash Item"),         19, "TrashItem" },
                { STR("Replenish Item"),     20, "ReplenishItem" },
                { STR("Remove Attributes"),  21, "RemoveAttributes" },
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

            VLOG(STR("[SettingsUI] keymap inject complete: Quick Build={} rows, Mod={} rows, selectors={}\n"),
                 invAdded, modAdded, (int)selectors.size());

            m_keymapInjectedFor = FWeakObjectPtr(editMapTab);
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
                         std::wstring(r.iniKey.begin(), r.iniKey.end()).c_str());
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
                     std::wstring(r.iniKey.begin(), r.iniKey.end()).c_str(),
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
                         std::wstring(r.iniKey.begin(), r.iniKey.end()).c_str(), vk, modBits);
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

        void injectModGameOptions(UObject* gameplayTab)
        {
            if (!gameplayTab || !isObjectAlive(gameplayTab)) return;
            UObject* prev = m_gameplayTabInjectedFor.Get();
            if (prev == gameplayTab) return;
            if (!ensureFrontEndButtonClassCached(gameplayTab))
            {
                VLOG(STR("[SettingsUI] CP4 — FrontEndButton class not cached, deferring\n"));
                return;
            }
            // Section heading class is cached during keymap inject; if the
            // user hasn't visited keymap tab yet, skip the heading.
            auto* ppPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("ParentPanel"));
            UObject* parentPanel = ppPtr ? *ppPtr : nullptr;
            if (!parentPanel || !isObjectAlive(parentPanel))
            {
                VLOG(STR("[SettingsUI] CP4 — ParentPanel missing on GameplayTab\n"));
                return;
            }
            auto* wtPtr = gameplayTab->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* outer = (wtPtr && *wtPtr) ? *wtPtr : gameplayTab;

            m_gameOptButtons.clear();

            if (m_settingsSectionHeadingCls)
            {
                if (UObject* hd = spawnSectionHeading(STR("Mod Game Options")))
                    appendToVerticalBox(parentPanel, hd);
            }

            struct Spec { const wchar_t* label; GameOptKind kind; };
            static const Spec specs[] = {
                { STR("Rename Character"),   GameOptKind::Rename },
                { STR("Save Game"),          GameOptKind::Save },
                { STR("Unlock Recipes"),     GameOptKind::Unlock },
                { STR("Read All Lore"),      GameOptKind::ReadAll },
                { STR("Clear All Buffs"),    GameOptKind::ClearBuffs },
                { STR("Toggle No Collision Flying"), GameOptKind::ToggleNoCollision },
                { STR("Toggle Peace Mode"),  GameOptKind::TogglePeace },
            };
            int added = 0;
            for (const auto& s : specs)
            {
                UObject* btn = spawnGameOptButton(outer, s.label, s.kind);
                if (btn) { appendToVerticalBox(parentPanel, btn); ++added; }
            }
            VLOG(STR("[SettingsUI] CP4 — injected {} mod-game-option buttons\n"), added);
            m_gameplayTabInjectedFor = FWeakObjectPtr(gameplayTab);
        }

        // Dispatch CP4 button clicks. Called from ProcessEvent post-hook
        // when WBP_FrontEndButton_C::OnMenuButtonClicked fires.
        void onModGameOptionClicked(UObject* clickedButton)
        {
            if (!clickedButton) return;
            for (auto& g : m_gameOptButtons)
            {
                if (g.widget.Get() != clickedButton) continue;
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
