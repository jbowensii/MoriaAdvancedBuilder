// moria_unlock.inl — Recipe unlock + read-history clear (v6.4.1)
//
// Two features, both operate entirely through game UFUNCTIONs — zero raw memory writes:
//   1. unlockAllAvailableRecipes()  — iterates recipe DataTables, filters protected/hidden/unfinished/DLC-gated rows,
//                                     and paces DiscoverRecipe calls across frames (50/frame) to avoid
//                                     FFastArraySerializer burst-flood that breaks worlds with the built-in cheat menu.
//   2. markAllLoreRead()            — one ProcessEvent call on WBP_LoreScreen_v2_C::MarkAllRead (Blueprint-native).

        // Returns true if the row name matches a hidden/dev/test prefix — those
        // rows are NOT to be unlocked by the "Unlock All Recipes" feature.
        bool unlock_hasHiddenPrefix(const std::wstring& name)
        {
            static const std::vector<std::wstring> prefixes = {
                L"DEV_",       L"dev_",
                L"Test_",      L"TEST_",      L"test_",
                L"Playtest_",  L"PLAYTEST_",  L"playtest_",
                L"Debug_",     L"DEBUG_",     L"debug_",
                L"Cheat_",     L"CHEAT_",     L"cheat_",
                L"WIP_",       L"wip_",
                L"DEPRECATED_", L"_DEPRECATED_"
            };
            for (const auto& p : prefixes)
            {
                if (name.size() >= p.size() && name.compare(0, p.size(), p) == 0)
                    return true;
            }
            return false;
        }

        // Read a FName from an FMor*RowHandle at the given offset (handle base).
        // Handle layout: 0x00 vtable (8B), 0x08 FName RowName. Returns empty FName on failure.
        FName unlock_readHandleName(uint8_t* handleBase)
        {
            if (!isReadableMemory(handleBase, 0x10)) return FName();
            FName out;
            std::memcpy(&out, handleBase + 0x08, sizeof(FName));
            return out;
        }

        // Build the set of DLC-gated row names (by handle RowName) that the player does NOT own.
        // Any recipe whose result points to one of these must be skipped.
        //
        // Algorithm: iterate DT_Entitlements, for each row, check GetIsEntitlementOwned(RowName).
        // If owned → skip (its content is fair game).
        // If not owned → union Items/Constructions/Runes handle names into the block sets.
        void unlock_buildDLCBlockList(std::set<std::wstring>& blockedItems,
                                     std::set<std::wstring>& blockedConstructions,
                                     std::set<std::wstring>& blockedRunes)
        {
            // Locate the entitlement manager
            std::vector<UObject*> mgrs;
            UObjectGlobals::FindAllOf(STR("MorEntitlementManager"), mgrs);
            if (mgrs.empty())
            {
                VLOG(STR("[Unlock] EntitlementManager not found — skipping DLC filter (all recipes considered safe)\n"));
                return;
            }
            UObject* entMgr = mgrs[0];
            if (!isObjectAlive(entMgr)) return;

            auto* isOwnedFn = entMgr->GetFunctionByNameInChain(STR("GetIsEntitlementOwned"));
            if (!isOwnedFn)
            {
                VLOG(STR("[Unlock] GetIsEntitlementOwned UFunction missing — skipping DLC filter\n"));
                return;
            }

            // Locate the entitlements DataTable
            DataTableUtil entTable;
            if (!entTable.bind(L"DT_Entitlements"))
            {
                VLOG(STR("[Unlock] DT_Entitlements not bound — skipping DLC filter\n"));
                return;
            }

            auto entRowNames = entTable.getRowNames();
            int blockedTotal = 0;
            for (const auto& entRowName : entRowNames)
            {
                // Call GetIsEntitlementOwned(FName EntitlementID) -> bool
                struct { FName EntID; bool Ret; } params{};
                params.EntID = FName(entRowName.c_str(), FNAME_Find);
                params.Ret = false;
                if (!safeProcessEvent(entMgr, isOwnedFn, &params)) continue;
                if (params.Ret) continue;  // owned → no block

                // Not owned — union its Items/Constructions/Runes into block sets
                uint8_t* rowData = entTable.findRowData(entRowName.c_str());
                if (!rowData) continue;

                // FMorEntitlementDefinition layout (from Moria.hpp:2568):
                //   0x0128: TArray<FMorAnyItemRowHandle>       Items
                //   0x0138: TArray<FMorConstructionRowHandle>  Constructions
                //   0x0148: TArray<FMorRuneRowHandle>          Runes
                // Each handle is 0x10 bytes (FFGKDataTableRowHandle — vtable@0 + FName@8).
                struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };

                auto collectHandles = [&](int offset, std::set<std::wstring>& blockSet) {
                    if (!isReadableMemory(rowData + offset, sizeof(TArrayHeader))) return;
                    TArrayHeader hdr{};
                    std::memcpy(&hdr, rowData + offset, sizeof(TArrayHeader));
                    if (!hdr.Data || hdr.Num < 0 || hdr.Num > 1000) return;
                    for (int32_t i = 0; i < hdr.Num; ++i)
                    {
                        FName n = unlock_readHandleName(hdr.Data + i * 0x10);
                        try {
                            std::wstring s = n.ToString();
                            if (!s.empty()) { blockSet.insert(s); blockedTotal++; }
                        } catch (...) {}
                    }
                };

                collectHandles(0x0128, blockedItems);
                collectHandles(0x0138, blockedConstructions);
                collectHandles(0x0148, blockedRunes);
            }

            VLOG(STR("[Unlock] DLC filter: {} handles blocked (items={} constructions={} runes={})\n"),
                 blockedTotal, (int)blockedItems.size(), (int)blockedConstructions.size(), (int)blockedRunes.size());
        }

        // Read the ResultItemHandle / ResultConstructionHandle FName at offset 0xD8
        // inside a FMor{Item,Construction}RecipeDefinition row. The handle itself is at
        // rowData + 0xD8, and its FName sits at +0x08 within that handle (0xD8 + 0x08 = 0xE0).
        FName unlock_readRecipeResultName(uint8_t* rowData)
        {
            if (!isReadableMemory(rowData + 0xD8, 0x10)) return FName();
            FName out;
            std::memcpy(&out, rowData + 0xD8 + 0x08, sizeof(FName));
            return out;
        }

        // Enumerate one recipe table and append all eligible row names to outQueue.
        void unlock_collectFromTable(const wchar_t* tableName,
                                    const std::set<std::wstring>& blockedByDLC,
                                    bool checkResultHandle,
                                    std::vector<std::wstring>& outQueue)
        {
            DataTableUtil dt;
            if (!dt.bind(tableName))
            {
                VLOG(STR("[Unlock] Table '{}' not found — skipping\n"), tableName);
                return;
            }

            auto rowNames = dt.getRowNames();
            int before = (int)outQueue.size();
            for (const auto& rowName : rowNames)
            {
                // Filter 1: hidden-prefix names
                if (unlock_hasHiddenPrefix(rowName)) continue;

                // Filter 2: Disabled rows
                uint8_t* rowData = dt.findRowData(rowName.c_str());
                if (!rowData) continue;
                if (!isReadableMemory(rowData, 0x20)) continue;
                uint8_t enabledState = *reinterpret_cast<uint8_t*>(rowData + 0x10);
                if (enabledState != 0) continue;  // Disabled == 1

                // Filter 3: DLC-gated (check the result handle's RowName against block set)
                if (checkResultHandle)
                {
                    FName resultName = unlock_readRecipeResultName(rowData);
                    try {
                        std::wstring resultStr = resultName.ToString();
                        if (!resultStr.empty() && blockedByDLC.count(resultStr) > 0) continue;
                    } catch (...) { continue; }
                }
                else
                {
                    // For rune recipes the row IS the rune — check rowName against block set
                    if (blockedByDLC.count(rowName) > 0) continue;
                }

                outQueue.push_back(rowName);
            }
            VLOG(STR("[Unlock] '{}': {}/{} rows eligible\n"),
                 tableName, (int)outQueue.size() - before, (int)rowNames.size());
        }

        // Entry point — triggered by Ctrl+Shift+U.
        void unlockAllAvailableRecipes()
        {
            if (!m_unlockQueue.empty())
            {
                VLOG(STR("[Unlock] Already running ({} remaining)\n"), (int)m_unlockQueue.size());
                showOnScreen(L"Unlock already in progress", 2.0f, 1.0f, 0.8f, 0.2f);
                return;
            }

            // 1. Find the discovery manager
            std::vector<UObject*> mgrs;
            UObjectGlobals::FindAllOf(STR("MorDiscoveryManager"), mgrs);
            if (mgrs.empty())
            {
                VLOG(STR("[Unlock] MorDiscoveryManager not found — load a world first\n"));
                showOnScreen(L"Load a world first", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            UObject* discoveryMgr = mgrs[0];
            if (!isObjectAlive(discoveryMgr)) return;

            auto* fn = discoveryMgr->GetFunctionByNameInChain(STR("DiscoverRecipe"));
            if (!fn)
            {
                VLOG(STR("[Unlock] DiscoverRecipe UFunction not found\n"));
                showOnScreen(L"DiscoverRecipe not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            // 2. Build the DLC block list (best-effort; empty set = no filter if it fails)
            std::set<std::wstring> blockedItems, blockedConstructions, blockedRunes;
            unlock_buildDLCBlockList(blockedItems, blockedConstructions, blockedRunes);

            // 3. Enumerate each recipe table, collect eligible row names
            std::vector<std::wstring> queue;
            unlock_collectFromTable(L"DT_ConstructionRecipes", blockedConstructions, true,  queue);
            unlock_collectFromTable(L"DT_ItemRecipes",         blockedItems,         true,  queue);
            unlock_collectFromTable(L"DT_Runes",               blockedRunes,         false, queue);

            if (queue.empty())
            {
                VLOG(STR("[Unlock] No eligible recipes found (tables empty or all filtered)\n"));
                showOnScreen(L"No recipes to unlock", 3.0f, 1.0f, 0.8f, 0.2f);
                return;
            }

            // 4. Prime the drain state
            m_unlockQueue = std::move(queue);
            m_unlockTotal = (int)m_unlockQueue.size();
            m_unlockProcessed = 0;
            m_unlockDiscoveryMgr = discoveryMgr;
            m_unlockDiscoverRecipeFn = fn;

            VLOG(STR("[Unlock] Queued {} recipes (paced {} per frame)\n"),
                 m_unlockTotal, UNLOCK_BATCH_SIZE);
            showOnScreen(L"Unlocking recipes...", 3.0f, 0.3f, 1.0f, 0.3f);
        }

        // Called from the main tick each frame — processes UNLOCK_BATCH_SIZE entries then returns.
        void drainUnlockQueue()
        {
            if (m_unlockQueue.empty()) return;
            if (!m_unlockDiscoveryMgr || !isObjectAlive(m_unlockDiscoveryMgr) || !m_unlockDiscoverRecipeFn)
            {
                m_unlockQueue.clear();
                return;
            }

            int n = (int)std::min<size_t>(UNLOCK_BATCH_SIZE, m_unlockQueue.size());
            for (int i = 0; i < n; ++i)
            {
                const std::wstring& rowName = m_unlockQueue.back();
                struct { FName RecipeName; } params{};
                params.RecipeName = FName(rowName.c_str(), FNAME_Find);
                safeProcessEvent(m_unlockDiscoveryMgr, m_unlockDiscoverRecipeFn, &params);
                m_unlockQueue.pop_back();
                m_unlockProcessed++;
            }

            if (m_unlockQueue.empty())
            {
                VLOG(STR("[Unlock] Complete — {} recipes discovered\n"), m_unlockProcessed);
                showOnScreen(L"All available recipes unlocked", 3.0f, 0.3f, 1.0f, 0.3f);
                m_unlockDiscoveryMgr = nullptr;
                m_unlockDiscoverRecipeFn = nullptr;
            }
        }

        // Find every UDataTable in the world whose RowStruct name matches the given name.
        // Used to locate DT_Lore / DT_Tips / DT_Tutorials without hardcoding table names
        // (the game may have multiple lore sub-tables).
        std::vector<UObject*> markread_findTablesByRowStruct(const wchar_t* rowStructName)
        {
            std::vector<UObject*> result;
            std::vector<UObject*> allDTs;
            UObjectGlobals::FindAllOf(STR("DataTable"), allDTs);
            for (auto* dt : allDTs)
            {
                if (!dt || !isObjectAlive(dt)) continue;
                auto** rs = dt->GetValuePtrByPropertyNameInChain<UStruct*>(STR("RowStruct"));
                if (!rs || !*rs) continue;
                try {
                    if (std::wstring((*rs)->GetName()) == rowStructName)
                        result.push_back(dt);
                } catch (...) {}
            }
            return result;
        }

        // Mark every row in every DataTable matching rowStructName as "viewed" by calling
        // screen->fnName(row_as_definition_struct) via ProcessEvent. Returns count marked.
        int markread_markAllByStructType(UObject* screen,
                                         const wchar_t* fnName,
                                         const wchar_t* paramName,
                                         const wchar_t* rowStructName,
                                         int structSize)
        {
            if (!screen || !isObjectAlive(screen)) return 0;
            auto* fn = screen->GetFunctionByNameInChain(fnName);
            if (!fn)
            {
                VLOG(STR("[MarkRead] {} not found on screen — skipping {}\n"), fnName, rowStructName);
                return 0;
            }
            auto* param = findParam(fn, paramName);
            if (!param)
            {
                VLOG(STR("[MarkRead] param '{}' not found on {}\n"), paramName, fnName);
                return 0;
            }
            int off = param->GetOffset_Internal();
            int pSize = fn->GetParmsSize();
            std::vector<uint8_t> paramBuf(pSize, 0);

            auto tables = markread_findTablesByRowStruct(rowStructName);
            if (tables.empty())
            {
                VLOG(STR("[MarkRead] no DataTable found with RowStruct='{}'\n"), rowStructName);
                return 0;
            }

            int marked = 0;
            for (UObject* table : tables)
            {
                DataTableUtil dt;
                std::wstring tName;
                try { tName = table->GetName(); } catch (...) { continue; }
                if (!dt.bind(tName.c_str())) continue;

                auto rowNames = dt.getRowNames();
                for (const auto& rowName : rowNames)
                {
                    uint8_t* rowData = dt.findRowData(rowName.c_str());
                    if (!rowData) continue;
                    if (!isReadableMemory(rowData, structSize)) continue;

                    std::memset(paramBuf.data(), 0, pSize);
                    std::memcpy(paramBuf.data() + off, rowData, structSize);

                    if (safeProcessEvent(screen, fn, paramBuf.data())) marked++;
                }
            }
            VLOG(STR("[MarkRead]   struct={} marked={} (across {} table(s))\n"),
                 rowStructName, marked, (int)tables.size());
            return marked;
        }

        // v6.4.1 Phase 2: mark ALL categories as read — lore, goals, tutorials, tips.
        //
        // Uses three approaches in sequence:
        //   1. WBP_LoreScreen_v2_C::MarkAllRead() — one Blueprint call, handles every lore entry
        //   2. WBP_GoalsScreen_C::SetLoreEntryViewed(def) — per-entry across every DT with
        //      RowStruct=FMorLoreDefinition (goals, mysteries, and category-specific sub-tables)
        //   3. SetTutorialEntryViewed / SetTipEntryViewed — per-row across DT_Tutorials/DT_Tips
        void markAllLoreRead()
        {
            int totalMarked = 0;
            bool anyScreenFound = false;

            // Phase 1: Lore screen MarkAllRead (single-call path)
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("WBP_LoreScreen_v2_C"), screens);
                if (!screens.empty() && isObjectAlive(screens[0]))
                {
                    anyScreenFound = true;
                    auto* fn = screens[0]->GetFunctionByNameInChain(STR("MarkAllRead"));
                    if (fn && safeProcessEvent(screens[0], fn, nullptr))
                    {
                        VLOG(STR("[MarkRead] Phase 1: WBP_LoreScreen_v2_C::MarkAllRead invoked\n"));
                    }
                }
            }

            // Phase 2: Goals screen — per-entry mark via SetLoreEntryViewed / SetTutorialEntryViewed / SetTipEntryViewed
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("WBP_GoalsScreen_C"), screens);
                if (!screens.empty() && isObjectAlive(screens[0]))
                {
                    anyScreenFound = true;
                    UObject* gs = screens[0];

                    // FMorLoreDefinition = 0x130 bytes
                    totalMarked += markread_markAllByStructType(
                        gs, STR("SetLoreEntryViewed"), STR("LoreEntry"),
                        STR("MorLoreDefinition"), 0x130);

                    // FMorTutorialDefinition = 0x60 bytes
                    totalMarked += markread_markAllByStructType(
                        gs, STR("SetTutorialEntryViewed"), STR("TutorialEntry"),
                        STR("MorTutorialDefinition"), 0x60);

                    // FMorTipDefinition = 0xA8 bytes
                    totalMarked += markread_markAllByStructType(
                        gs, STR("SetTipEntryViewed"), STR("TipEntry"),
                        STR("MorTipDefinition"), 0xA8);
                }
            }

            // Phase 3: also run per-entry across the LORE screen (belt-and-suspenders; some
            // sub-tables may not be covered by the Blueprint MarkAllRead above).
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("WBP_LoreScreen_v2_C"), screens);
                if (!screens.empty() && isObjectAlive(screens[0]))
                {
                    UObject* ls = screens[0];
                    totalMarked += markread_markAllByStructType(
                        ls, STR("SetLoreEntryViewed"), STR("LoreEntry"),
                        STR("MorLoreDefinition"), 0x130);
                }
            }

            // Phase 4: Build menu — call UI_WBP_Build_Tab_C::MarkAllAsRead() (one-shot, game-native).
            // This clears the "NEW!" badges on the construction build menu (the "4 unread building" count).
            {
                std::vector<UObject*> tabs;
                UObjectGlobals::FindAllOf(STR("UI_WBP_Build_Tab_C"), tabs);
                if (!tabs.empty() && isObjectAlive(tabs[0]))
                {
                    anyScreenFound = true;
                    auto* fn = tabs[0]->GetFunctionByNameInChain(STR("MarkAllAsRead"));
                    if (fn && safeProcessEvent(tabs[0], fn, nullptr))
                    {
                        VLOG(STR("[MarkRead] Phase 4: UI_WBP_Build_Tab_C::MarkAllAsRead invoked\n"));
                    }
                    else
                    {
                        VLOG(STR("[MarkRead] Phase 4: MarkAllAsRead UFunction missing or call failed\n"));
                    }
                }
                else
                {
                    VLOG(STR("[MarkRead] Phase 4: UI_WBP_Build_Tab_C not instantiated (open build menu once)\n"));
                }
            }

            // Phase 5: Crafting Screen — UI_WBP_Crafting_Screen_C::MarkAllAsRead() (one-call).
            // Same pattern as the build tab. Clears the "296 new" count on crafting stations.
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("UI_WBP_Crafting_Screen_C"), screens);
                int invoked = 0;
                for (UObject* screen : screens)
                {
                    if (!screen || !isObjectAlive(screen)) continue;
                    auto* fn = screen->GetFunctionByNameInChain(STR("MarkAllAsRead"));
                    if (fn && safeProcessEvent(screen, fn, nullptr))
                    {
                        anyScreenFound = true;
                        invoked++;
                        VLOG(STR("[MarkRead] Phase 5: UI_WBP_Crafting_Screen_C::MarkAllAsRead invoked on {}\n"),
                             screen->GetName());
                    }
                }
                if (invoked == 0)
                {
                    VLOG(STR("[MarkRead] Phase 5: No UI_WBP_Crafting_Screen_C instance — open a crafting station once\n"));
                }
            }

            // Phase 6: Recipe Viewer (top-level Crafting menu) — UI_WBP_Recipe_Viewer_C::MarkAllAsRead().
            // This is the pause-menu Crafting entry that opens a recipe compendium.
            {
                std::vector<UObject*> viewers;
                UObjectGlobals::FindAllOf(STR("UI_WBP_Recipe_Viewer_C"), viewers);
                int invoked = 0;
                for (UObject* viewer : viewers)
                {
                    if (!viewer || !isObjectAlive(viewer)) continue;
                    auto* fn = viewer->GetFunctionByNameInChain(STR("MarkAllAsRead"));
                    if (fn && safeProcessEvent(viewer, fn, nullptr))
                    {
                        anyScreenFound = true;
                        invoked++;
                        VLOG(STR("[MarkRead] Phase 6: UI_WBP_Recipe_Viewer_C::MarkAllAsRead invoked on {}\n"),
                             viewer->GetName());
                    }
                }
                if (invoked == 0)
                {
                    VLOG(STR("[MarkRead] Phase 6: No UI_WBP_Recipe_Viewer_C instance — open pause menu Crafting once\n"));
                }
            }

            if (!anyScreenFound)
            {
                VLOG(STR("[MarkRead] No screens instantiated — open Lore, Goals, Build menu, and a ")
                     STR("crafting station once each, then retry\n"));
                showOnScreen(L"Open each menu once first, then retry", 4.0f, 1.0f, 0.8f, 0.2f);
                return;
            }

            VLOG(STR("[MarkRead] Complete — total entries marked across all phases: {}\n"), totalMarked);
            showOnScreen(L"All categories marked as read", 3.0f, 0.3f, 1.0f, 0.3f);
        }
