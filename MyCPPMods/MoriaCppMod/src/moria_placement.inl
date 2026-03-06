// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_placement.inl — Build placement lifecycle, snap, ghost piece mgmt  ║
// ║  #include inside MoriaCppMod class body (before moria_quickbuild.inl)     ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // ──── Placement Management ────────────────────────────────────────────────────
        // Build lifecycle: menu open/close, recipe selection, ghost piece, snap, rotation

        // Cached UMorBuildingComponent lookup
        UObject* getCachedBuildComp()
        {
            if (m_cachedBuildComp && isWidgetAlive(m_cachedBuildComp))
                return m_cachedBuildComp;
            m_cachedBuildComp = nullptr;
            std::vector<UObject*> comps;
            UObjectGlobals::FindAllOf(STR("MorBuildingComponent"), comps);
            if (!comps.empty() && comps[0])
            {
                m_cachedBuildComp = comps[0];
                QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildComp: found MorBuildingComponent {:p}\n"),
                      static_cast<void*>(m_cachedBuildComp));
            }
            return m_cachedBuildComp;
        }

        // ── Build widget discovery and menu management ───────────────────────────────

        // Find visible BuildHUD widget (falls back to first match if none visible)
        UObject* findBuildHUD()
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos)
                {
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        w->ProcessEvent(visFunc, &vp);
                        if (vp.Ret) return w;
                    }
                }
            }
            // Fallback: first match regardless of visibility
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos) return w;
            }
            return nullptr;
        }

        // Cached BuildHUD: GetActiveBuildingWidget() primary, FindAllOf fallback
        UObject* getCachedBuildHUD()
        {
            if (m_cachedBuildHUD)
            {
                if (!isWidgetAlive(m_cachedBuildHUD) || safeClassName(m_cachedBuildHUD).find(L"BuildHUD") == std::wstring::npos)
                    m_cachedBuildHUD = nullptr;
            }
            if (!m_cachedBuildHUD)
            {
                // Primary: GetActiveBuildingWidget() (avoids widget scan)
                UObject* comp = getCachedBuildComp();
                if (comp)
                {
                    auto* fn = comp->GetFunctionByNameInChain(STR("GetActiveBuildingWidget"));
                    if (fn)
                    {
                        struct { UObject* Ret{nullptr}; } params{};
                        comp->ProcessEvent(fn, &params);
                        if (params.Ret && isWidgetAlive(params.Ret))
                        {
                            m_cachedBuildHUD = params.Ret;
                            QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildHUD: via GetActiveBuildingWidget -> {:p}\n"),
                                  static_cast<void*>(m_cachedBuildHUD));
                        }
                    }
                }
                // Fallback: FindAllOf scan (needed before build mode is active)
                if (!m_cachedBuildHUD)
                {
                    m_cachedBuildHUD = findWidgetByClass(L"UI_WBP_BuildHUDv2_C", false);
                    if (m_cachedBuildHUD && !isWidgetAlive(m_cachedBuildHUD))
                        m_cachedBuildHUD = nullptr;
                }
            }
            return m_cachedBuildHUD;
        }

        // Live check: BuildHUD showing AND past recipe picker
        bool isPlacementActive()
        {
            UObject* hud = getCachedBuildHUD();
            if (!hud) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (no HUD)\n")); return false; }
            auto* fn = hud->GetFunctionByNameInChain(STR("IsShowing"));
            if (!fn) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (no IsShowing fn)\n")); return false; }
            struct { bool Ret{false}; } params{};
            hud->ProcessEvent(fn, &params);
            if (!params.Ret) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (HUD not showing)\n")); return false; }
            // Past the recipe picker?
            static FBoolProperty* s_bp_recipeSelectMode = nullptr;
            if (!s_bp_recipeSelectMode)
                s_bp_recipeSelectMode = resolveBoolProperty(hud, L"recipeSelectMode");
            if (!s_bp_recipeSelectMode) return false;
            bool recipeSelectMode = s_bp_recipeSelectMode->GetPropertyValueInContainer(hud);
            bool result = !recipeSelectMode;
            QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> {} (recipeSelectMode={})\n"),
                  result ? STR("true") : STR("false"), recipeSelectMode ? STR("true") : STR("false"));
            return result;
        }

        // Build_Tab visibility via UWidget::IsVisible (not FGK IsShowing, which may lag)
        bool isBuildTabShowing()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab || !m_fnIsVisible) return false;
            struct { bool Ret{false}; } params{};
            tab->ProcessEvent(m_fnIsVisible, &params);
            QBLOG(STR("[MoriaCppMod] [QB] isBuildTabShowing -> {}\n"), params.Ret ? STR("true") : STR("false"));
            return params.Ret;
        }

        // UMG animation check — retained for future use, not in any guards (350ms cooldown is the proven fix)
        bool isBuildTabAnimating()
        {
            UObject* tab = getCachedBuildTab();
            if (!tab) return false;
            auto* fn = tab->GetFunctionByNameInChain(STR("IsAnyAnimationPlaying"));
            if (!fn) return false;
            struct { bool Ret{false}; } params{};
            tab->ProcessEvent(fn, &params);
            return params.Ret;
        }

        // Open build tab via FGK Show(). 350ms cooldown prevents MovieScene re-entrancy crashes.
        void showBuildTab()
        {
            ULONGLONG now = GetTickCount64();
            if (now - m_lastShowHideTime < 350)
            {
                QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: SKIPPED ({}ms since last Show/Hide)\n"),
                      now - m_lastShowHideTime);
                return;
            }
            UObject* tab = getCachedBuildTab();
            if (!tab) { QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: no tab found\n")); return; }
            auto* fn = tab->GetFunctionByNameInChain(STR("Show"));
            QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: calling Show() fn={}\n"), fn ? STR("YES") : STR("NO"));
            if (fn) tab->ProcessEvent(fn, nullptr);
            m_lastShowHideTime = now;
        }

        // Close build tab via FGK Hide(). 350ms cooldown shared with Show.
        void hideBuildTab()
        {
            ULONGLONG now = GetTickCount64();
            if (now - m_lastShowHideTime < 350)
            {
                QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: SKIPPED ({}ms since last Show/Hide)\n"),
                      now - m_lastShowHideTime);
                return;
            }
            UObject* tab = getCachedBuildTab();
            if (!tab) { QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: no tab found\n")); return; }
            auto* fn = tab->GetFunctionByNameInChain(STR("Hide"));
            QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: calling Hide() fn={}\n"), fn ? STR("YES") : STR("NO"));
            if (fn) tab->ProcessEvent(fn, nullptr);
            m_lastShowHideTime = now;
        }

        // ── GATA (Ghost Actor / Target Actor) management ─────────────────────────────

        UObject* resolveGATA()
        {
            auto* hud = findBuildHUD();
            if (!hud) return nullptr;
            auto* weakPtr = hud->GetValuePtrByPropertyNameInChain<RC::Unreal::FWeakObjectPtr>(STR("TargetActor"));
            if (!weakPtr) return nullptr;
            return weakPtr->Get();
        }

        // Set rotation increment on GATA via runtime property discovery
        bool setGATARotation(UObject* gata, float step)
        {
            float* snap = gata->GetValuePtrByPropertyNameInChain<float>(STR("SnapRotateIncrement"));
            float* free = gata->GetValuePtrByPropertyNameInChain<float>(STR("FreePlaceRotateIncrement"));
            if (!snap || !free) return false;
            *snap = step;
            *free = step;
            return true;
        }

        // ── Snap toggle ──────────────────────────────────────────────────────────────

        // Toggle snap for current piece (auto-restores on placement)
        void toggleSnap()
        {
            VLOG(STR("[MoriaCppMod] [Snap] toggleSnap() called, current state: {} savedDist={}\n"),
                 m_snapEnabled ? STR("ON") : STR("OFF"), m_savedMaxSnapDistance);

            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Snap] resolveGATA returned nullptr — no piece being placed\n"));
                showOnScreen(L"Snap: place a piece first", 2.0f, 1.0f, 0.5f, 0.0f);
                return;
            }

            float* pSnap = gata->GetValuePtrByPropertyNameInChain<float>(STR("MaxSnapDistance"));
            if (!pSnap)
            {
                VLOG(STR("[MoriaCppMod] [Snap] MaxSnapDistance not found on GATA\n"));
                return;
            }

            // Capture original on first use
            if (m_savedMaxSnapDistance < 0.0f)
            {
                m_savedMaxSnapDistance = *pSnap;
                VLOG(STR("[MoriaCppMod] [Snap] Captured original MaxSnapDistance={}\n"), m_savedMaxSnapDistance);
            }

            m_snapEnabled = !m_snapEnabled;
            float newVal = m_snapEnabled ? m_savedMaxSnapDistance : 0.0f;
            *pSnap = newVal;

            VLOG(STR("[MoriaCppMod] [Snap] -> {} MaxSnapDistance={}\n"),
                 m_snapEnabled ? STR("ON") : STR("OFF"), newVal);

            showOnScreen(m_snapEnabled ? L"Snap: ON" : L"Snap: OFF",
                         2.0f, 0.4f, 0.6f, 1.0f);
        }

        // Auto-restore snap after placement — only resets flag, new GATA spawns with default
        void restoreSnap()
        {
            if (m_snapEnabled) return;
            m_snapEnabled = true;
            VLOG(STR("[MoriaCppMod] [Snap] Snap auto-restored to ON after placement (GATA will spawn fresh)\n"));
        }

        // ── MC slot state management ─────────────────────────────────────────────────

        // Update MC toolbar slot visual (frame texture + icon brightness)
        void setMcSlotState(int slot, UmgSlotState state)
        {
            if (slot < 0 || slot >= MC_SLOTS) return;
            if (!m_mcBarWidget) return;
            if (m_mcSlotStates[slot] == state) return;
            m_mcSlotStates[slot] = state;

            UObject* stateImg = m_mcStateImages[slot];
            UObject* iconImg = m_mcIconImages[slot];

            if (state == UmgSlotState::Disabled)
            {
                if (stateImg && m_umgTexInactive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexInactive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 0.3f, 0.3f, 0.3f, 0.4f);
            }
            else if (state == UmgSlotState::Active)
            {
                if (stateImg && m_umgTexActive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexActive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            else if (state == UmgSlotState::Inactive)
            {
                if (stateImg && m_umgTexInactive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexInactive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            else // Empty
            {
                if (stateImg && m_umgTexEmpty && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexEmpty, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        // ── Ghost piece event handlers ───────────────────────────────────────────────

        // Ghost piece appeared — reapply snap/rotation state and enable snap slot
        void onGhostAppeared()
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Placement] onGhostAppeared: resolveGATA returned nullptr (ghost not yet spawned)\n"));
                // Ghost will appear shortly after blockSelectedEvent
                setMcSlotState(5, UmgSlotState::Active);
                return;
            }

            // Reapply snap state to new GATA
            if (!m_snapEnabled && m_savedMaxSnapDistance >= 0.0f)
            {
                float* pSnap = gata->GetValuePtrByPropertyNameInChain<float>(STR("MaxSnapDistance"));
                if (pSnap)
                {
                    *pSnap = 0.0f;
                    VLOG(STR("[MoriaCppMod] [Placement] Re-applied snap OFF to new GATA\n"));
                }
            }
            setGATARotation(gata, static_cast<float>(s_overlay.rotationStep.load()));
            setMcSlotState(5, UmgSlotState::Active);
        }

        // Ghost disappeared — grey out snap slot (suppressed during QB SM transitions)
        void onGhostDisappeared()
        {
            // Suppress during recipe swap — transient ghost flicker crashes RHI thread
            if (m_qbPhase != PlacePhase::Idle)
                return;
            // Also suppress within 500ms of last selection (DIRECT path ghost flicker)
            ULONGLONG now = GetTickCount64();
            if (now - m_lastQBSelectTime < 500)
                return;
            VLOG(STR("[MoriaCppMod] [Placement] onGhostDisappeared: greying out snap slot\n"));
            setMcSlotState(5, UmgSlotState::Disabled);
        }

        // ── Recipe selection (blockSelectedEvent path) ───────────────────────────────

        // Select recipe via blockSelectedEvent on Build_Tab (SM path after menu open)
        SelectResult selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;
            const std::wstring& slotTexture = m_recipeSlots[slot].textureName;

            QBLOG(STR("[MoriaCppMod] [QuickBuild] SELECT: '{}' tex='{}' (F{}) hasHandle={}\n"),
                  targetName, slotTexture, slot + 1, m_recipeSlots[slot].hasHandle);

            UObject* buildHUD = getCachedBuildHUD();

            // Find matching visible Build_Item_Medium widget
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            int visibleCount = 0;
            for (auto* w : widgets)
            {
                if (!w || !isWidgetAlive(w)) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                // Skip non-visible (stale/recycled) widgets
                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    w->ProcessEvent(visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                if (!name.empty() && name == targetName)
                {
                    // Disambiguate same-name items by icon texture
                    if (!slotTexture.empty())
                    {
                        std::wstring widgetTex = extractIconTextureName(w);
                        if (widgetTex != slotTexture) continue;
                    }
                    matchedWidget = w;
                    break;
                }
            }

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   checked {} visible widgets, match: {}\n"), visibleCount, matchedWidget ? L"YES" : L"NO");

            if (!matchedWidget)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] No match among {} visible widgets\n"), visibleCount);
                return (visibleCount == 0) ? SelectResult::Loading : SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            resolveBSEOffsets(func);
            if (!bseOffsetsValid())
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] blockSelectedEvent param resolution failed\n"));
                return SelectResult::NotFound;
            }

            std::vector<uint8_t> params(s_bse.parmsSize, 0);
            bool gotFreshBLock = false;

            // Resolve bLock offset on first quickbuild
            if (s_off_bLock == -2)
            {
                resolveOffset(matchedWidget, L"bLock", s_off_bLock);
                probeRecipeBlockStruct(matchedWidget);
            }

            // Best: fresh bLock from matched widget
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (@0x{:X})\n"), s_off_bLock);
            }

            // Fallback: saved bLock (may be stale)
            if (!gotFreshBLock && m_recipeSlots[slot].hasBLockData)
            {
                std::memcpy(params.data() + s_bse.bLock, m_recipeSlots[slot].bLockData, BLOCK_DATA_SIZE);
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using SAVED bLock (may be stale)\n"));
            }
            else if (!gotFreshBLock)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   WARNING: no bLock data at all, using zeros\n"));
            }

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   calling blockSelectedEvent with selfRef={:p}\n"), static_cast<void*>(matchedWidget));

            // RAII guard suppresses post-hook capture during automated selection
            m_isAutoSelecting = true;
            struct AutoSelectGuardFB
            {
                bool& flag;
                ~AutoSelectGuardFB() { flag = false; }
            } guardFB{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params.data());
            m_isAutoSelecting = false;

            // Cache handle for future DIRECT path
            if (buildHUD)
                cacheRecipeHandleForSlot(buildHUD, slot);

            logBLockDiagnostics(L"BUILD", targetName, params.data());

            showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;
            refreshActionBar();

            m_activeBuilderSlot = slot;
            updateBuildersBar();

            onGhostAppeared();

            return SelectResult::Found;
        }

        // Direct SelectRecipe via cached handle (fast path for mid-placement switching)
        bool trySelectRecipeByHandle(UObject* buildHUD, const uint8_t* handleData)
        {
            if (!buildHUD || !handleData) return false;
            auto* selectFn = buildHUD->GetFunctionByNameInChain(STR("SelectRecipe"));
            if (!selectFn)
            {
                QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe not found on build HUD\n"));
                return false;
            }
            int pSz = selectFn->GetParmsSize();
            if (pSz < RECIPE_HANDLE_SIZE)
            {
                QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe parmsSize={} too small (need {})\n"), pSz, RECIPE_HANDLE_SIZE);
                return false;
            }
            std::vector<uint8_t> params(pSz, 0);
            std::memcpy(params.data(), handleData, RECIPE_HANDLE_SIZE);
            buildHUD->ProcessEvent(selectFn, params.data());
            uint32_t ci = *reinterpret_cast<const uint32_t*>(handleData + 8);
            QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe called with handle CI={}\n"), ci);
            return true;
        }

        // Cache recipe handle from build HUD after successful activation
        void cacheRecipeHandleForSlot(UObject* buildHUD, int slot)
        {
            if (!buildHUD || slot < 0 || slot >= QUICK_BUILD_SLOTS) return;
            auto* getHandleFn = buildHUD->GetFunctionByNameInChain(STR("GetSelectedRecipeHandle"));
            if (!getHandleFn) return;
            int hSz = getHandleFn->GetParmsSize();
            if (hSz < RECIPE_HANDLE_SIZE) return;
            std::vector<uint8_t> hParams(hSz, 0);
            buildHUD->ProcessEvent(getHandleFn, hParams.data());
            std::memcpy(m_recipeSlots[slot].recipeHandle, hParams.data(), RECIPE_HANDLE_SIZE);
            m_recipeSlots[slot].hasHandle = true;
            uint32_t ci = *reinterpret_cast<uint32_t*>(hParams.data() + 8);
            QBLOG(STR("[MoriaCppMod] [QB] Cached recipe handle for F{}: CI={}\n"), slot + 1, ci);
        }

        // ── Build-from-target recipe selection ───────────────────────────────────────

        // Lowercase alphanumeric-only for fuzzy matching
        static std::wstring normalizeForMatch(const std::wstring& s)
        {
            std::wstring out;
            out.reserve(s.size());
            for (wchar_t c : s)
            {
                if (std::iswalnum(c)) out += std::towlower(c);
            }
            return out;
        }

        SelectResult selectRecipeByTargetName(UObject* buildTab)
        {
            QBLOG(STR("[MoriaCppMod] [TargetBuild] Searching: name='{}' recipeRef='{}' bLockOffset=0x{:X}\n"),
                                            m_targetBuildName,
                                            m_targetBuildRecipeRef,
                                            s_off_bLock);

            // Build FName variants for ComparisonIndex matching
            std::vector<std::pair<std::wstring, uint32_t>> targetFNames;
            {
                // Full ref
                FName fn1(m_targetBuildRecipeRef.c_str());
                uint32_t ci1 = fn1.GetComparisonIndex();
                targetFNames.push_back({m_targetBuildRecipeRef, ci1});

                // Without BP_ prefix
                std::wstring noBP = m_targetBuildRecipeRef;
                if (noBP.size() > 3 && noBP.substr(0, 3) == L"BP_") noBP.erase(0, 3);
                FName fn2(noBP.c_str());
                uint32_t ci2 = fn2.GetComparisonIndex();
                targetFNames.push_back({noBP, ci2});

                QBLOG(STR("[MoriaCppMod] [TargetBuild] FName CIs: full='{}' CI={}, short='{}' CI={}\n"),
                                                m_targetBuildRecipeRef,
                                                ci1,
                                                noBP,
                                                ci2);
            }

            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            std::wstring matchedName;
            int visibleCount = 0;
            int bLockNullCount = 0, bLockMemFailCount = 0, variantsEmptyCount = 0;

            for (auto* w : widgets)
            {
                if (!w || !isWidgetAlive(w)) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    w->ProcessEvent(visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                bool isFirstFew = (visibleCount <= 5);

                // Resolve bLock offset on first visible widget
                if (s_off_bLock == -2)
                {
                    resolveOffset(w, L"bLock", s_off_bLock);
                    probeRecipeBlockStruct(w);
                }

                // Log first 5 for diagnostics
                if (isFirstFew)
                {
                    std::wstring objName(w->GetName());
                    QBLOG(STR("[MoriaCppMod] [TargetBuild]   W[{}] obj='{}' display='{}'\n"), visibleCount, objName, name);
                }

                // Strategy 1: bLock RowName CI matching
                if (!matchedWidget && s_off_bLock >= 0 && !m_targetBuildRecipeRef.empty())
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(w);
                    uint8_t* bLock = widgetBase + s_off_bLock;

                    if (!isReadableMemory(bLock, BLOCK_DATA_SIZE))
                    {
                        bLockNullCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock=NULL\n"));
                    }
                    else if (!isReadableMemory(bLock + rbVariantsOff(), 16))
                    {
                        bLockMemFailCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock+104 not readable\n"));
                    }
                    else
                    {
                        uint8_t* variantsPtr = *reinterpret_cast<uint8_t**>(bLock + rbVariantsOff());
                        int32_t variantsCount = *reinterpret_cast<int32_t*>(bLock + rbVariantsNumOff());

                        if (isFirstFew)
                        {
                            // Log bLock tag FName
                            uint32_t tagCI = *reinterpret_cast<uint32_t*>(bLock);
                            int32_t tagNum = *reinterpret_cast<int32_t*>(bLock + 4);
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock tag CI={} Num={} | variants={} ptr={:p}\n"),
                                                            tagCI,
                                                            tagNum,
                                                            variantsCount,
                                                            (void*)variantsPtr);
                        }

                        if (variantsCount <= 0 || !variantsPtr)
                        {
                            variantsEmptyCount++;
                        }
                        else if (isReadableMemory(variantsPtr, variantEntrySize()))
                        {
                            // ResultConstructionHandle.RowName
                            uint32_t rowCI = *reinterpret_cast<uint32_t*>(variantsPtr + variantRowCIOff());
                            int32_t rowNum = *reinterpret_cast<int32_t*>(variantsPtr + variantRowNumOff());

                            if (isFirstFew)
                            {
                                QBLOG(STR("[MoriaCppMod] [TargetBuild]     RowName CI={} Num={}\n"), rowCI, rowNum);
                            }

                            // Match against target FName CIs
                            for (auto& [tName, tCI] : targetFNames)
                            {
                                if (tCI == rowCI)
                                {
                                    matchedWidget = w;
                                    matchedName = name.empty() ? tName : name;
                                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (bLock RowName CI={}) on '{}' target='{}'\n"),
                                                                    rowCI,
                                                                    matchedName,
                                                                    tName);
                                    break;
                                }
                            }
                        }
                        else if (isFirstFew)
                        {
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     variantsPtr not readable (variantEntrySize bytes)\n"));
                        }
                    }
                }

                // Strategy 2: Exact display name match
                if (!matchedWidget && !name.empty() && name == m_targetBuildName)
                {
                    matchedWidget = w;
                    matchedName = name;
                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (exact display name) on '{}'\n"), name);
                }

                // Strategy 3: Fuzzy display name match (containment)
                if (!matchedWidget && !name.empty())
                {
                    std::wstring nameNorm = normalizeForMatch(name);
                    std::wstring refNoBP = normalizeForMatch(m_targetBuildRecipeRef);
                    if (refNoBP.size() > 2 && refNoBP.substr(0, 2) == L"bp") refNoBP = refNoBP.substr(2);
                    std::wstring targetNorm = normalizeForMatch(m_targetBuildName);

                    if (nameNorm == refNoBP || nameNorm == targetNorm)
                    {
                        matchedWidget = w;
                        matchedName = name;
                        QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (normalized exact) on '{}'\n"), name);
                    }
                }

                if (matchedWidget) break;
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Result: {} visible, bLockNull={} memFail={} varEmpty={} match={}\n"),
                                            visibleCount,
                                            bLockNullCount,
                                            bLockMemFailCount,
                                            variantsEmptyCount,
                                            matchedWidget ? matchedName.c_str() : L"NO");

            if (!matchedWidget)
            {
                if (visibleCount == 0) return SelectResult::Loading;
                showOnScreen((L"Recipe '" + m_targetBuildName + L"' not found in build menu").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                return SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            resolveBSEOffsets(func);
            if (!bseOffsetsValid()) return SelectResult::NotFound;

            std::vector<uint8_t> params(s_bse.parmsSize, 0);

            // Fresh bLock from matched widget
            bool gotFreshBLock = false;
            if (s_off_bLock >= 0)
            {
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Calling blockSelectedEvent: freshBLock={} selfRef={:p}\n"),
                                            gotFreshBLock,
                                            static_cast<void*>(matchedWidget));

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;

            // RAII guard suppresses post-hook capture during automated selection
            m_isAutoSelecting = true;
            struct AutoSelectGuardTB
            {
                bool& flag;
                ~AutoSelectGuardTB() { flag = false; }
            } guardTB{m_isAutoSelecting};
            buildTab->ProcessEvent(func, params.data());
            m_isAutoSelecting = false;

            logBLockDiagnostics(L"TARGET-BUILD", matchedName, params.data());

            showOnScreen((L"Build: " + matchedName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;
            refreshActionBar();
            onGhostAppeared();

            return SelectResult::Found;
        }

        // ── Entry points from dispatcher (quickbuild / target build) ─────────────────

        // Start/switch build — called from quickBuildSlot() after debounce/cooldown
        void startOrSwitchBuild(int slot)
        {
            m_isTargetBuild = false;
            QBLOG(STR("[MoriaCppMod] [QuickBuild] ACTIVATE F{} -> '{}' (charLoaded={} frameCounter={} hasHandle={})\n"),
                                            slot + 1,
                                            m_recipeSlots[slot].displayName,
                                            m_characterLoaded,
                                            m_frameCounter,
                                            m_recipeSlots[slot].hasHandle);

            // DIRECT PATH: cached handle + active build system, bypasses SM. 150ms rate-limit.
            if (m_recipeSlots[slot].hasHandle && m_buildMenuPrimed)
            {
                ULONGLONG now = GetTickCount64();
                if (now - m_lastDirectSelectTime < 150)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} DIRECT path cooldown ({}ms since last)\n"),
                          slot + 1, now - m_lastDirectSelectTime);
                    return;
                }

                // Only when build system is active (GetActiveBuildingWidget non-null)
                UObject* buildHUD = nullptr;
                UObject* comp = getCachedBuildComp();
                if (comp)
                {
                    auto* fn = comp->GetFunctionByNameInChain(STR("GetActiveBuildingWidget"));
                    if (fn)
                    {
                        struct { UObject* Ret{nullptr}; } params{};
                        comp->ProcessEvent(fn, &params);
                        if (params.Ret && isWidgetAlive(params.Ret))
                            buildHUD = params.Ret;
                    }
                }
                if (buildHUD)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT SelectRecipe for F{} (skipping state machine)\n"), slot + 1);
                    m_isAutoSelecting = true;
                    if (trySelectRecipeByHandle(buildHUD, m_recipeSlots[slot].recipeHandle))
                    {
                        m_isAutoSelecting = false;
                        m_lastDirectSelectTime = now;
                        m_lastQBSelectTime = now;
                        // Hide build tab if showing
                        if (isBuildTabShowing()) hideBuildTab();
                        const std::wstring& targetName = m_recipeSlots[slot].displayName;
                        showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
                        m_buildMenuWasOpen = true;
                        refreshActionBar();
                        m_activeBuilderSlot = slot;
                        updateBuildersBar();
                        // Reapply snap/rotation to new ghost
                        onGhostAppeared();
                        return;
                    }
                    m_isAutoSelecting = false;
                    // Handle stale — fall through to SM
                    m_recipeSlots[slot].hasHandle = false;
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path failed, falling through to state machine\n"));
                }
                else
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path skipped: GetActiveBuildingWidget() returned null\n"));
                }
            }

            m_pendingQuickBuildSlot = slot;
            m_qbStartTime = GetTickCount64();
            m_qbRetryTime = m_qbStartTime;

            // Phase transitions based on live game state
            if (!m_buildMenuPrimed)
            {
                // Cold start — B key creates widget, OnAfterShow signals ready
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Cold start — priming build menu\n"));
                m_buildTabAfterShowFired = false;
                if (!isBuildTabShowing())
                {
                    keybd_event(VK_BUILD_MENU, 0, 0, 0);
                    keybd_event(VK_BUILD_MENU, 0, KEYEVENTF_KEYUP, 0);
                }
                m_qbPhase = PlacePhase::PrimeOpen;
            }
            else if (isPlacementActive())
            {
                // Cancel active ghost piece first
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Placement active — sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = PlacePhase::CancelGhost;
            }
            else if (isBuildTabShowing())
            {
                // Already open -- select directly
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Build tab already open -- skipping to SelectRecipe\n"));
                m_qbPhase = PlacePhase::SelectRecipe;
            }
            else
            {
                // Open via FGK Show()
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Build tab not open — calling Show()\n"));
                showBuildTab();
                m_qbPhase = PlacePhase::OpenMenu;
            }
        }

        // Start build-from-target via unified SM (guards in quickBuildFromTarget)
        void startBuildFromTarget()
        {
            VLOG(STR("[MoriaCppMod] [TargetBuild] startBuildFromTarget: name='{}' recipeRef='{}'\n"),
                 m_targetBuildName, m_targetBuildRecipeRef);

            m_isTargetBuild = true;
            m_pendingQuickBuildSlot = -1;
            m_qbStartTime = GetTickCount64();
            m_qbRetryTime = m_qbStartTime;

            // Phase transitions (same logic as startOrSwitchBuild)
            if (!m_buildMenuPrimed)
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Cold start — priming build menu\n"));
                m_buildTabAfterShowFired = false;
                if (!isBuildTabShowing())
                {
                    keybd_event(VK_BUILD_MENU, 0, 0, 0);
                    keybd_event(VK_BUILD_MENU, 0, KEYEVENTF_KEYUP, 0);
                }
                m_qbPhase = PlacePhase::PrimeOpen;
            }
            else if (isPlacementActive())
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Placement active — sending ESC to cancel first\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                m_qbPhase = PlacePhase::CancelGhost;
            }
            else if (isBuildTabShowing())
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Build tab already open -- skipping to SelectRecipe\n"));
                m_qbPhase = PlacePhase::SelectRecipe;
            }
            else
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Build tab not open — calling Show()\n"));
                showBuildTab();
                m_qbPhase = PlacePhase::OpenMenu;
            }
        }

        // ── Placement state machine tick ─────────────────────────────────────────────

        void placementTick()
        {
            // ── QB/target-build SM ──
            if (m_qbPhase != PlacePhase::Idle)
            {
                ULONGLONG now = GetTickCount64();
                ULONGLONG elapsed = now - m_qbStartTime;
                ULONGLONG sinceLast = now - m_qbRetryTime;

                // Safety timeout (5s prime, 2.5s normal)
                ULONGLONG maxTime = m_buildMenuPrimed ? 2500 : 5000;
                if (elapsed > maxTime)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: TIMEOUT at {}ms phase {}\n"),
                                                    elapsed, static_cast<int>(m_qbPhase));
                    showOnScreen(Loc::get("msg.build_menu_timeout"), 3.0f, 1.0f, 0.3f, 0.0f);
                    hideBuildTab();
                    m_pendingQuickBuildSlot = -1;
                    m_isTargetBuild = false;
                    m_qbPhase = PlacePhase::Idle;
                }
                else if (m_qbPhase == PlacePhase::CancelGhost)
                {
                    // Wait for ESC to deactivate placement
                    if (!isPlacementActive())
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: placement cancelled ({}ms)\n"), elapsed);
                        m_qbRetryTime = now;
                        if (isBuildTabShowing())
                        {
                            // Menu still open -- skip close/reopen to avoid Slate widget churn
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: menu still open -- skipping to SelectRecipe\n"));
                            m_qbPhase = PlacePhase::SelectRecipe;
                        }
                        else
                        {
                            // Closed — reopen
                            m_buildTabAfterShowFired = false;
                            keybd_event(0x42, 0, 0, 0);
                            keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                            m_qbPhase = PlacePhase::OpenMenu;
                        }
                    }
                }
                else if (m_qbPhase == PlacePhase::CloseMenu)
                {
                    // Wait for tab to close
                    if (!isBuildTabShowing())
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: menu closed ({}ms) -- waiting {} frames\n"), elapsed, m_quickBuildSwapDelay);
                        m_qbRetryTime = now;
                        m_qbWaitCount = m_quickBuildSwapDelay;
                        m_qbPhase = PlacePhase::WaitReopen;
                    }
                }
                else if (m_qbPhase == PlacePhase::WaitReopen)
                {
                    if (--m_qbWaitCount <= 0 && (now - m_lastShowHideTime >= 350))
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: wait complete ({}ms) -- opening fresh via B key\n"), elapsed);
                        m_qbRetryTime = now;
                        m_lastShowHideTime = now; // B key triggers Show — update cooldown
                        m_buildTabAfterShowFired = false;
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                        m_qbPhase = PlacePhase::OpenMenu;
                    }
                }
                else if (m_qbPhase == PlacePhase::PrimeOpen)
                {
                    // Wait for OnAfterShow (FGK "ready" signal)
                    if (m_buildTabAfterShowFired)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: prime complete via OnAfterShow ({}ms) — selecting recipe\n"), elapsed);
                        m_buildTabAfterShowFired = false;
                        m_buildMenuPrimed = true;
                        m_qbRetryTime = now;
                        m_qbPhase = PlacePhase::SelectRecipe;
                    }
                    else if (isBuildTabShowing() && sinceLast > 2000)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: prime complete via fallback ({}ms) — selecting recipe\n"), elapsed);
                        m_buildMenuPrimed = true;
                        m_qbRetryTime = now;
                        m_qbPhase = PlacePhase::SelectRecipe;
                    }
                    else if (!isBuildTabShowing() && sinceLast > 500)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: prime retrying B key ({}ms since last)\n"), sinceLast);
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                        m_qbRetryTime = now;
                    }
                }
                else if (m_qbPhase == PlacePhase::OpenMenu)
                {
                    if (m_buildTabAfterShowFired || isBuildTabShowing())
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: menu ready ({}ms) — selecting recipe\n"), elapsed);
                        m_buildTabAfterShowFired = false;
                        m_qbRetryTime = now;
                        m_qbPhase = PlacePhase::SelectRecipe;
                        // Fall through to SelectRecipe
                    }
                    else if (sinceLast > 500)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: retrying B key ({}ms since last)\n"), sinceLast);
                        m_buildTabAfterShowFired = false;
                        keybd_event(0x42, 0, 0, 0);
                        keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                        m_qbRetryTime = now;
                    }
                }

                if (m_qbPhase == PlacePhase::SelectRecipe)
                {
                    UObject* buildTab = getCachedBuildTab();
                    if (!buildTab)
                    {
                        // Build tab lost — global timeout handles failure
                    }
                    else
                    {
                        SelectResult result = m_isTargetBuild
                            ? selectRecipeByTargetName(buildTab)
                            : selectRecipeOnBuildTab(buildTab, m_pendingQuickBuildSlot);
                        if (result == SelectResult::Found)
                        {
                            // Reset rotation for new placement
                            s_overlay.totalRotation = 0;
                            s_overlay.needsUpdate = true;
                            updateMcRotationLabel();
                            m_pendingQuickBuildSlot = -1;
                            m_isTargetBuild = false;
                            m_qbPhase = PlacePhase::Idle;
                            m_lastQBSelectTime = now;
                        }
                        else if (result == SelectResult::NotFound)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: recipe not found ({}ms)\n"), elapsed);
                            if (m_isTargetBuild)
                            {
                                // Error already shown by selectRecipeByTargetName
                            }
                            else
                            {
                                const std::wstring& targetName = m_recipeSlots[m_pendingQuickBuildSlot].displayName;
                                showOnScreen((L"Recipe '" + targetName + L"' not found in menu!").c_str(), 3.0f, 1.0f, 0.3f, 0.0f);
                            }
                            m_pendingQuickBuildSlot = -1;
                            m_isTargetBuild = false;
                            m_qbPhase = PlacePhase::Idle;
                        }
                        // else Loading: keep retrying until timeout
                    }
                }
            }
        }
