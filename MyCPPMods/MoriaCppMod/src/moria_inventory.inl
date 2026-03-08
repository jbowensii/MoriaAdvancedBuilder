// +==============================================================================+
// |  moria_inventory.inl -- Inventory utilities                                |
// |  safeProcessEvent SEH wrapper, player inventory component lookup           |
// |  #include inside MoriaCppMod class body                                    |
// +==============================================================================+

        // SEH-safe ProcessEvent wrapper — catches access violations from game code
        // (e.g. async asset loader crash during AddItem auto-equip).
        // Must be a standalone function: __try/__except cannot coexist with C++ destructors.
        static bool safeProcessEvent(UObject* obj, RC::Unreal::UFunction* fn, void* params)
        {
            __try {
                obj->ProcessEvent(fn, params);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        // ---- 6E: Inventory & Toolbar System -----------------------------------

        // Finds the MorInventoryComponent on the player character.
        UObject* findPlayerInventoryComponent(UObject* playerChar)
        {
            if (!playerChar) return nullptr;
            std::vector<UObject*> allComps;
            UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
            for (auto* c : allComps)
            {
                if (!c) continue;
                auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                if (!ownerFunc) continue;
                struct
                {
                    UObject* Ret{nullptr};
                } op{};
                if (!safeProcessEvent(c, ownerFunc, &op)) continue;
                if (op.Ret != playerChar) continue;
                std::wstring cls = safeClassName(c);
                if (cls == STR("MorInventoryComponent")) return c;
            }
            return nullptr;
        }

        // Removes a per-instance item effect from the inventory component's Effects.List TArray.
        // effectClassName: e.g. "MorRuneEffect" or "MorItemTintEffect"
        // Returns true if an entry was found and removed.
        bool removeItemEffectFromList(UObject* invComp, int32_t itemID, const wchar_t* effectClassName)
        {
            if (!invComp) return false;
            FProperty* effectsProp = invComp->GetPropertyByNameInChain(STR("Effects"));
            if (!effectsProp) return false;

            int effectsOff = effectsProp->GetOffset_Internal();
            uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp) + effectsOff + 0x0110;
            if (!isReadableMemory(listBase, 16)) return false;

            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t& arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
            constexpr int kStride = 0x30; // FActiveItemEffect size

            for (int32_t j = arrNum - 1; j >= 0; j--)
            {
                uint8_t* e = arrData + j * kStride;
                if (!isReadableMemory(e, kStride)) continue;

                int32_t onItem = *reinterpret_cast<int32_t*>(e + 0x0C);
                if (onItem != itemID) continue;

                UObject* effect = *reinterpret_cast<UObject**>(e + 0x10);
                if (!effect || !isReadableMemory(reinterpret_cast<uint8_t*>(effect), 64)) continue;

                std::wstring cls = safeClassName(effect);
                if (cls != effectClassName) continue;

                VLOG(STR("[MoriaCppMod] [RemoveEffect] Removing {} at index {} (OnItem={}) from Effects.List\n"), effectClassName, j, onItem);
                if (j < arrNum - 1)
                    std::memcpy(e, arrData + (arrNum - 1) * kStride, kStride);
                arrNum--;
                return true;
            }
            return false;
        }

        // ---- Diagnostic: dump item effect arrays ----
        void dumpItemEffects(UClass* itemClass, int32_t itemID, UObject* invComp)
        {
            // 1) CDO-level arrays: StartingItemEffects, VisibleEffects, EquipEffects (class-level, same for all instances)
            UObject* cdo = itemClass->GetClassDefaultObject();
            if (cdo)
            {
                // StartingItemEffects — TArray<UItemEffect*> @0x0240
                auto* sieProp = cdo->GetPropertyByNameInChain(STR("StartingItemEffects"));
                if (sieProp)
                {
                    int off = sieProp->GetOffset_Internal();
                    uint8_t* base = reinterpret_cast<uint8_t*>(cdo) + off;
                    if (isReadableMemory(base, 16))
                    {
                        auto** data = *reinterpret_cast<UObject***>(base);
                        int32_t num = *reinterpret_cast<int32_t*>(base + 8);
                        VLOG(STR("[MoriaCppMod] [EffectDump] CDO StartingItemEffects count={}\n"), num);
                        for (int32_t j = 0; j < num && j < 20; j++)
                        {
                            if (data[j] && isReadableMemory(reinterpret_cast<uint8_t*>(data[j]), 64))
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] {} (class={})\n"), j, safeClassName(data[j]), safeClassName(data[j]));
                            else
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] 0x{:X}\n"), j, reinterpret_cast<uintptr_t>(data[j]));
                        }
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [EffectDump] CDO StartingItemEffects property NOT FOUND\n"));
                }

                // VisibleEffects — TArray<TSubclassOf<UGameplayEffect>>
                auto* veProp = cdo->GetPropertyByNameInChain(STR("VisibleEffects"));
                if (veProp)
                {
                    int off = veProp->GetOffset_Internal();
                    uint8_t* base = reinterpret_cast<uint8_t*>(cdo) + off;
                    if (isReadableMemory(base, 16))
                    {
                        auto** data = *reinterpret_cast<UClass***>(base);
                        int32_t num = *reinterpret_cast<int32_t*>(base + 8);
                        VLOG(STR("[MoriaCppMod] [EffectDump] CDO VisibleEffects count={}\n"), num);
                        for (int32_t j = 0; j < num && j < 20; j++)
                        {
                            if (data[j] && isReadableMemory(reinterpret_cast<uint8_t*>(data[j]), 64))
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] {}\n"), j, data[j]->GetName());
                            else
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] 0x{:X}\n"), j, reinterpret_cast<uintptr_t>(data[j]));
                        }
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [EffectDump] CDO VisibleEffects property NOT FOUND\n"));
                }

                // EquipEffects (named "Effects" in CXXHeaderDump but GetEquipEffects() exists)
                // Try both names
                auto* eeProp = cdo->GetPropertyByNameInChain(STR("EquipEffects"));
                if (!eeProp) eeProp = cdo->GetPropertyByNameInChain(STR("Effects"));
                if (eeProp)
                {
                    int off = eeProp->GetOffset_Internal();
                    uint8_t* base = reinterpret_cast<uint8_t*>(cdo) + off;
                    if (isReadableMemory(base, 16))
                    {
                        auto** data = *reinterpret_cast<UClass***>(base);
                        int32_t num = *reinterpret_cast<int32_t*>(base + 8);
                        VLOG(STR("[MoriaCppMod] [EffectDump] CDO EquipEffects count={}\n"), num);
                        for (int32_t j = 0; j < num && j < 20; j++)
                        {
                            if (data[j] && isReadableMemory(reinterpret_cast<uint8_t*>(data[j]), 64))
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] {}\n"), j, data[j]->GetName());
                            else
                                VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] 0x{:X}\n"), j, reinterpret_cast<uintptr_t>(data[j]));
                        }
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [EffectDump] CDO EquipEffects property NOT FOUND\n"));
                }
            }

            // 2) Per-instance effects: FActiveItemEffectArray Effects on inventory component
            // Effects is at offset 0x0340 on UInventoryComponent, List at +0x0110 within it
            // FActiveItemEffect: { FFastArraySerializerItem(0x10), int32 OnItem(0x10), UItemEffect* Effect(0x18), float EndTime(0x20), FPrimaryAssetId(0x28) }
            FProperty* effectsProp = invComp->GetPropertyByNameInChain(STR("Effects"));
            if (effectsProp)
            {
                int effectsOff = effectsProp->GetOffset_Internal();
                // FActiveItemEffectArray extends FFastArraySerializer — List TArray at +0x0110
                uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + effectsOff + 0x0110;
                if (isReadableMemory(effectsBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(effectsBase);
                    int32_t arrNum = *reinterpret_cast<int32_t*>(effectsBase + 8);
                    VLOG(STR("[MoriaCppMod] [EffectDump] InvComp Effects.List count={} (effectsOff=0x{:X})\n"), arrNum, effectsOff);

                    // FActiveItemEffect (CXXHeaderDump): Size=0x30
                    //   FFastArraySerializerItem base: ~0x0C
                    //   int32 OnItem @0x0C (size 4)
                    //   UItemEffect* Effect @0x10 (size 8)
                    //   float EndTime @0x18 (size 4)
                    //   FPrimaryAssetId @0x1C (size 0x10)
                    constexpr int kStride = 0x30;

                    for (int32_t j = 0; j < arrNum && j < 50; j++)
                    {
                        uint8_t* e = arrData + j * kStride;
                        if (!isReadableMemory(e, kStride)) { VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] <unreadable>\n"), j); continue; }

                        int32_t onItem = *reinterpret_cast<int32_t*>(e + 0x0C);
                        UObject* effect = *reinterpret_cast<UObject**>(e + 0x10);
                        float endTime = *reinterpret_cast<float*>(e + 0x18);

                        std::wstring effectName = L"nullptr";
                        std::wstring effectClass = L"?";
                        if (effect)
                        {
                            if (isReadableMemory(reinterpret_cast<uint8_t*>(effect), 64))
                            {
                                effectName = safeClassName(effect); // uses SEH internally
                                // Don't call GetName() on effect — use safeClassName only
                            }
                            else
                            {
                                effectName = L"<bad-ptr>";
                            }
                        }

                        // Also dump raw hex at +0x1C for FPrimaryAssetId (FName-based)
                        uint32_t assetIdWord0 = *reinterpret_cast<uint32_t*>(e + 0x1C);
                        uint32_t assetIdWord1 = *reinterpret_cast<uint32_t*>(e + 0x20);

                        bool isOurs = (onItem == itemID);
                        VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] OnItem={}{} Effect=0x{:X} (class={}) EndTime={} AssetId=[0x{:X},0x{:X}]\n"),
                             j, onItem, isOurs ? STR(" <<<MATCH") : STR(""),
                             reinterpret_cast<uintptr_t>(effect), effectName, endTime, assetIdWord0, assetIdWord1);
                    }
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [EffectDump] InvComp Effects property NOT FOUND\n"));
            }
        }

        // ---- Replenish Last Changed Item ----------------------------------------
        // Captures the last item class that triggered BroadcastToContainers_OnChanged
        // on the player's MorInventoryComponent (fires on pickup, move, etc.).
        // When user presses Replenish, finds all FItemInstances matching that class
        // and sets Count = MaxStackSize from DataTables.
        // Uses pure reflection — no hardcoded byte offsets.

        // Called from ProcessEvent post-hook when BroadcastToContainers_OnChanged fires.
        // Extracts item class from FItemHandle parameter via inventory Items.List lookup.
        void captureLastChangedItem(UObject* invComp, void* parms)
        {
            if (!invComp || !parms) return;

            // FItemHandle layout: { int32 ID (0x00), int32 Payload (0x04), TWeakObjectPtr Owner (0x08) }
            // ID is the item instance ID; we use it to find the matching FItemInstance
            int32_t handleID = *reinterpret_cast<int32_t*>(parms);
            if (handleID <= 0) return;

            // Ensure FItemInstance offsets are probed
            probeItemInstanceStruct(invComp);

            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp) return;
            int itemsOff = itemsProp->GetOffset_Internal();

            uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
            uint8_t* listBase = invBase + itemsOff + iiaListOff();
            if (!isReadableMemory(listBase, 16)) return;

            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
            if (!arrData || arrNum <= 0 || arrNum > 10000) return;

            int stride = iiSize();
            int itemOff = iiItemOff();
            int idOff = iiIDOff();

            for (int32_t i = 0; i < arrNum; i++)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;

                int32_t entryID = *reinterpret_cast<int32_t*>(entry + idOff);
                if (entryID != handleID) continue;

                UClass* entryClass = *reinterpret_cast<UClass**>(entry + itemOff);
                if (entryClass)
                {
                    m_lastPickedUpItemClass = entryClass;
                    m_lastPickedUpItemName = entryClass->GetName();
                    // Resolve human-readable display name from CDO
                    m_lastPickedUpDisplayName.clear();
                    if (UObject* cdo = entryClass->GetClassDefaultObject())
                    {
                        auto* getNameFn = cdo->GetFunctionByNameInChain(STR("GetDisplayName"));
                        if (getNameFn)
                        {
                            int nsz = getNameFn->GetParmsSize();
                            std::vector<uint8_t> nbuf(nsz, 0);
                            if (safeProcessEvent(cdo, getNameFn, nbuf.data()))
                            {
                                auto* pNameRet = findParam(getNameFn, STR("ReturnValue"));
                                if (pNameRet)
                                {
                                    FText* ft = reinterpret_cast<FText*>(nbuf.data() + pNameRet->GetOffset_Internal());
                                    m_lastPickedUpDisplayName = ft->ToString();
                                }
                            }
                        }
                    }
                    if (m_lastPickedUpDisplayName.empty())
                        m_lastPickedUpDisplayName = m_lastPickedUpItemName; // fallback to class name
                    // Read stack count from FItemInstance.Count at entry+0x18
                    m_lastPickedUpCount = *reinterpret_cast<int32_t*>(entry + 0x18);
                    // Store the full FItemHandle (0x14 bytes) and inventory component
                    std::memcpy(m_lastItemHandle, parms, 20);
                    m_lastItemInvComp = invComp;
                    VLOG(STR("[MoriaCppMod] [Capture] item={} display='{}' ID={} count={}\n"), m_lastPickedUpItemName, m_lastPickedUpDisplayName, handleID, m_lastPickedUpCount);

                    // --- Diagnostic: dump CDO effect arrays and per-instance effects ---
                    dumpItemEffects(entryClass, handleID, invComp);
                }
                return;
            }
        }

        void replenishLastItem()
        {
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(L"No item selected — move an item in inventory first", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            // Step 1: Get CDO and resolve RowHandle → RowName via reflection
            UObject* cdo = m_lastPickedUpItemClass->GetClassDefaultObject();
            if (!cdo)
            {
                showOnScreen(L"Replenish failed: no CDO", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            FProperty* rhProp = cdo->GetPropertyByNameInChain(STR("RowHandle"));
            if (!rhProp)
            {
                VLOG(STR("[MoriaCppMod] [Replenish] RowHandle property not found on CDO {}\n"), cdo->GetClassPrivate()->GetName());
                showOnScreen(L"Replenish failed: no RowHandle on item", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            int rhOff = rhProp->GetOffset_Internal();
            int rnOff = (s_off_rhRowName >= 0) ? s_off_rhRowName : 0x08;
            uint8_t* cdoBase = reinterpret_cast<uint8_t*>(cdo);
            if (!isReadableMemory(cdoBase + rhOff + rnOff, 8))
            {
                showOnScreen(L"Replenish failed: RowHandle unreadable", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }
            FName rowNameFN;
            std::memcpy(&rowNameFN, cdoBase + rhOff + rnOff, 8);
            std::wstring rowName;
            try { rowName = rowNameFN.ToString(); } catch (...) { }
            if (rowName.empty())
            {
                showOnScreen(L"Replenish failed: empty row name", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Replenish] Item CDO={} RowName='{}'\n"), cdo->GetClassPrivate()->GetName(), rowName);

            // Step 2: Lazy-bind item DataTables on first use
            if (!m_dtItems.isBound()) m_dtItems.bind(L"DT_Items");
            if (!m_dtWeapons.isBound()) m_dtWeapons.bind(L"DT_Weapons");
            if (!m_dtTools.isBound()) m_dtTools.bind(L"DT_Tools");
            if (!m_dtArmor.isBound()) m_dtArmor.bind(L"DT_Armor");
            if (!m_dtConsumables.isBound()) m_dtConsumables.bind(L"DT_Consumables");
            if (!m_dtOres.isBound()) m_dtOres.bind(L"DT_Ores");
            if (!m_dtContainerItems.isBound()) m_dtContainerItems.bind(L"DT_ContainerItems");

            // Search all item DataTables for MaxStackSize
            DataTableUtil* itemTables[] = {
                &m_dtItems, &m_dtWeapons, &m_dtTools, &m_dtArmor,
                &m_dtConsumables, &m_dtOres, &m_dtContainerItems
            };
            int32_t maxStack = 0;
            for (auto* dt : itemTables)
            {
                if (!dt->isBound()) continue;
                uint8_t* rowData = dt->findRowData(rowName.c_str());
                if (rowData)
                {
                    maxStack = dt->readInt32(rowName.c_str(), L"MaxStackSize");
                    VLOG(STR("[MoriaCppMod] [Replenish] Found '{}' in {} — MaxStackSize={}\n"),
                         rowName, dt->tableName, maxStack);
                    break;
                }
            }
            if (maxStack <= 0)
            {
                std::wstring msg = L"'" + rowName + L"' is not stackable";
                showOnScreen(msg, 3.0f, 1.0f, 0.7f, 0.2f);
                return;
            }

            // Step 3: Find inventory component
            UObject* pawn = getPawn();
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp)
            {
                showOnScreen(L"Replenish failed: no inventory", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            // Step 4: Get current count of this item type via GetItemCount ProcessEvent
            int32_t currentCount = 0;
            {
                auto* fn = invComp->GetFunctionByNameInChain(STR("GetItemCount"));
                if (fn)
                {
                    // Params: TSubclassOf<AInventoryItem> Item, EInventoryQuery From, ReturnValue int32
                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> p(sz, 0);

                    auto* pItem = findParam(fn, STR("Item"));
                    auto* pFrom = findParam(fn, STR("From"));
                    auto* pRet  = findParam(fn, STR("ReturnValue"));

                    if (pItem) *reinterpret_cast<UClass**>(p.data() + pItem->GetOffset_Internal()) = m_lastPickedUpItemClass;
                    if (pFrom) *reinterpret_cast<uint8_t*>(p.data() + pFrom->GetOffset_Internal()) = 0; // EInventoryQuery default
                    invComp->ProcessEvent(fn, p.data());
                    if (pRet) currentCount = *reinterpret_cast<int32_t*>(p.data() + pRet->GetOffset_Internal());
                }
            }

            VLOG(STR("[MoriaCppMod] [Replenish] {} currentCount={} maxStack={}\n"),
                 m_lastPickedUpItemName, currentCount, maxStack);

            int32_t deficit = maxStack - currentCount;
            if (deficit <= 0)
            {
                std::wstring msg = rowName + L" already at max (" + std::to_wstring(maxStack) + L")";
                showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
                return;
            }

            // Step 5: Call RequestAddItem via ProcessEvent to add the deficit
            auto* addFn = invComp->GetFunctionByNameInChain(STR("RequestAddItem"));
            if (!addFn)
            {
                VLOG(STR("[MoriaCppMod] [Replenish] RequestAddItem not found on invComp\n"));
                showOnScreen(L"Replenish failed: RequestAddItem not found", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            {
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> p(sz, 0);

                auto* pClass  = findParam(addFn, STR("Class"));
                auto* pCount  = findParam(addFn, STR("Count"));
                auto* pMethod = findParam(addFn, STR("Method"));

                if (pClass)  *reinterpret_cast<UClass**>(p.data() + pClass->GetOffset_Internal()) = m_lastPickedUpItemClass;
                if (pCount)  *reinterpret_cast<int32_t*>(p.data() + pCount->GetOffset_Internal()) = deficit;
                if (pMethod) *reinterpret_cast<uint8_t*>(p.data() + pMethod->GetOffset_Internal()) = 0; // EAddItem::Normal

                VLOG(STR("[MoriaCppMod] [Replenish] Calling RequestAddItem({}, count={}, method=Normal)\n"),
                     m_lastPickedUpItemName, deficit);
                invComp->ProcessEvent(addFn, p.data());
            }

            std::wstring msg = L"Replenished " + rowName + L" +" + std::to_wstring(deficit) + L" (→" + std::to_wstring(maxStack) + L")";
            showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
        }

        // ---- Remove Attributes (tint + rune) from last captured item ----
        // Calls ServerTintItem(handle, nullptr, char) and ServerInscribeRune(handle, nullptr, char)
        // on MorCraftingComponent via ProcessEvent to strip tint color and enchant.
        void removeItemAttributes()
        {
            if (!m_lastPickedUpItemClass || !m_lastItemInvComp)
            {
                showOnScreen(L"No item selected — move an item in inventory first", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            // Find MorCraftingComponent on the player pawn
            UObject* pawn = getPawn();
            if (!pawn)
            {
                showOnScreen(L"Remove Attributes failed: no pawn", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            UObject* craftComp = nullptr;
            {
                std::vector<UObject*> allComps;
                UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
                for (auto* c : allComps)
                {
                    if (!c) continue;
                    auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                    if (!ownerFunc) continue;
                    struct { UObject* Ret{nullptr}; } op{};
                    if (!safeProcessEvent(c, ownerFunc, &op)) continue;
                    if (op.Ret != pawn) continue;
                    std::wstring cls = safeClassName(c);
                    if (cls == STR("MorCraftingComponent")) { craftComp = c; break; }
                }
            }
            if (!craftComp)
            {
                showOnScreen(L"Remove Attributes failed: no crafting component", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            int removed = 0;

            // Clear tint: ServerTintItem(FItemHandle, nullptr, ACharacter*)
            auto* tintFn = craftComp->GetFunctionByNameInChain(STR("ServerTintItem"));
            if (tintFn)
            {
                int sz = tintFn->GetParmsSize();
                std::vector<uint8_t> p(sz, 0);
                auto* pHandle = findParam(tintFn, STR("ItemHandle"));
                auto* pEffect = findParam(tintFn, STR("TintEffect"));
                auto* pActor  = findParam(tintFn, STR("Interactor"));
                VLOG(STR("[MoriaCppMod] [RemoveAttrs] ServerTintItem ParmsSize={} pHandle={} pEffect={} pActor={}\n"),
                     sz, (void*)pHandle, (void*)pEffect, (void*)pActor);
                if (pHandle)
                {
                    std::memcpy(p.data() + pHandle->GetOffset_Internal(), m_lastItemHandle, 20);
                    VLOG(STR("[MoriaCppMod] [RemoveAttrs]   Handle offset={} ID={}\n"),
                         pHandle->GetOffset_Internal(), *reinterpret_cast<int32_t*>(m_lastItemHandle));
                }
                if (pEffect)
                {
                    *reinterpret_cast<UObject**>(p.data() + pEffect->GetOffset_Internal()) = nullptr;
                    VLOG(STR("[MoriaCppMod] [RemoveAttrs]   TintEffect offset={} (nullptr)\n"), pEffect->GetOffset_Internal());
                }
                if (pActor)
                {
                    *reinterpret_cast<UObject**>(p.data() + pActor->GetOffset_Internal()) = pawn;
                    VLOG(STR("[MoriaCppMod] [RemoveAttrs]   Interactor offset={} pawn={}\n"),
                         pActor->GetOffset_Internal(), (void*)pawn);
                }
                craftComp->ProcessEvent(tintFn, p.data());
                removed++;
                VLOG(STR("[MoriaCppMod] [RemoveAttrs] Called ServerTintItem(nullptr) on {}\n"), m_lastPickedUpItemName);
            }

            // Clear rune: directly remove MorRuneEffect entry from Effects.List
            // ServerInscribeRune(nullptr) doesn't work (validation rejects null Rune),
            // so we use removeItemEffectFromList() to manipulate the TArray directly.
            {
                int32_t targetID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
                UObject* invComp = findPlayerInventoryComponent(pawn);
                if (invComp && removeItemEffectFromList(invComp, targetID, STR("MorRuneEffect")))
                    removed++;
            }

            if (removed > 0)
            {
                std::wstring msg = L"Removed attributes from " + m_lastPickedUpDisplayName;
                showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
            }
            else
            {
                showOnScreen(L"Remove Attributes failed: no tint/rune functions found", 3.0f, 1.0f, 0.4f, 0.4f);
            }
        }

        // ---- Trash Item Dialog (confirmation modal) ---------------------------------
        // Shows a modal dialog with the item's icon and name, asking user to confirm deletion.
        // DELETE removes all of that item type from inventory + strips tint/rune effects.

        void showTrashDialog()
        {
            if (m_trashDlgVisible) { VLOG(STR("[MoriaCppMod] [Trash] BLOCKED: already visible\n")); return; }
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(L"No item selected — move an item in inventory first", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Trash] Starting dialog creation for {}...\n"), m_lastPickedUpItemName);

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass      = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* overlayClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* hboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !overlayClass || !hboxClass || !vboxClass || !textBlockClass)
            { showErrorBox(L"Trash: missing UMG classes"); return; }

            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"Trash: no PlayerController"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: createFn={} wblClass={}\n"), (void*)createFn, (void*)wblClass); return; }

            int sz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(sz, 0);
            auto* pOwner = findParam(createFn, STR("WorldContextObject"));
            auto* pClass = findParam(createFn, STR("WidgetType"));
            auto* pRet   = findParam(createFn, STR("ReturnValue"));
            if (!pOwner || !pClass || !pRet) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: pOwner={} pClass={} pRet={}\n"), (void*)pOwner, (void*)pClass, (void*)pRet); return; }
            *reinterpret_cast<UObject**>(cp.data() + pOwner->GetOffset_Internal()) = pc;
            *reinterpret_cast<UObject**>(cp.data() + pClass->GetOffset_Internal()) = userWidgetClass;
            wblClass->ProcessEvent(createFn, cp.data());
            UObject* userWidget = *reinterpret_cast<UObject**>(cp.data() + pRet->GetOffset_Internal());
            if (!userWidget) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: userWidget is null\n")); return; }
            VLOG(STR("[MoriaCppMod] [Trash] CP1: userWidget created\n"));

            UObject* outer = userWidget;
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = (wtSlot && *wtSlot) ? *wtSlot : nullptr;

            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));

            // Find textures
            UObject* texBG = findTexture2DByName(L"T_UI_Pnl_Background_Base");
            UObject* texSectionBg = findTexture2DByName(L"T_UI_Pnl_TabSelected");

            VLOG(STR("[MoriaCppMod] [Trash] CP2: textures done\n"));
            // Root overlay
            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOl = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOl) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: rootOl is null\n")); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOl);

            // Clip overflow
            {
                auto* setClipFn = rootOl->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz2 = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz2, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; rootOl->ProcessEvent(setClipFn, cp2.data()); }
            }

            VLOG(STR("[MoriaCppMod] [Trash] CP3: rootOl + clipping done\n"));
            const float dlgW = 500.0f, dlgH = 180.0f;

            // Layer 0: Border frame (dark blue)
            {
                FStaticConstructObjectParameters bfP(imageClass, outer);
                UObject* bf = UObjectGlobals::StaticConstructObject(bfP);
                if (bf) { umgSetBrushSize(bf, dlgW, dlgH); umgSetImageColor(bf, 0.08f, 0.14f, 0.32f, 1.0f); addToOverlay(rootOl, bf); }
            }

            // Layer 1: BG image inset 1px (fully opaque)
            if (texBG && setBrushFn)
            {
                FStaticConstructObjectParameters bgP(imageClass, outer);
                UObject* bg = UObjectGlobals::StaticConstructObject(bgP);
                if (bg)
                {
                    umgSetBrush(bg, texBG, setBrushFn);
                    umgSetBrushSize(bg, dlgW - 2.0f, dlgH - 2.0f);
                    umgSetOpacity(bg, 1.0f);
                    UObject* s = addToOverlay(rootOl, bg);
                    if (s) { umgSetHAlign(s, 2); umgSetVAlign(s, 2); }
                }
            }

            // Layer 2: Content VBox
            FStaticConstructObjectParameters cvP(vboxClass, outer);
            UObject* contentVBox = UObjectGlobals::StaticConstructObject(cvP);
            if (contentVBox)
            {
                UObject* cvSlot = addToOverlay(rootOl, contentVBox);
                if (cvSlot) umgSetSlotPadding(cvSlot, 20.0f, 10.0f, 20.0f, 10.0f);

                // Section header: "Trash Item"
                if (texSectionBg && setBrushFn)
                {
                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                    if (secOl)
                    {
                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, dlgW - 40.0f, 50.0f); addToOverlay(secOl, secImg); }
                        UObject* secLabel = createTextBlock(L"Trash Item", 0.78f, 0.86f, 1.0f, 1.0f, 24);
                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                        addToVBox(contentVBox, secOl);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Trash] CP4: header section done\n"));
                // Item icon + name row
                {
                    FStaticConstructObjectParameters itemHbP(hboxClass, outer);
                    UObject* itemRow = UObjectGlobals::StaticConstructObject(itemHbP);
                    if (itemRow)
                    {
                        // Get item icon from CDO via GetIcon() -> UTexture2D*
                        UObject* cdo = m_lastPickedUpItemClass->GetClassDefaultObject();
                        UObject* iconTex = nullptr;
                        if (cdo)
                        {
                            auto* getIconFn = cdo->GetFunctionByNameInChain(STR("GetIcon"));
                            if (getIconFn)
                            {
                                int isz = getIconFn->GetParmsSize();
                                std::vector<uint8_t> ibuf(isz, 0);
                                if (safeProcessEvent(cdo, getIconFn, ibuf.data()))
                                {
                                    auto* pIconRet = findParam(getIconFn, STR("ReturnValue"));
                                    if (pIconRet)
                                        iconTex = *reinterpret_cast<UObject**>(ibuf.data() + pIconRet->GetOffset_Internal());
                                }
                            }
                            VLOG(STR("[MoriaCppMod] [Trash] GetIcon CDO={} iconTex={}\n"), (void*)cdo, (void*)iconTex);
                        }

                        // Create icon image (64x64)
                        if (iconTex && setBrushFn)
                        {
                            FStaticConstructObjectParameters iconImgP(imageClass, outer);
                            UObject* iconImg = UObjectGlobals::StaticConstructObject(iconImgP);
                            if (iconImg)
                            {
                                umgSetBrushNoMatch(iconImg, iconTex, setBrushFn);
                                umgSetBrushSize(iconImg, 64.0f, 64.0f);
                                UObject* iconSlot = addToHBox(itemRow, iconImg);
                                if (iconSlot) { umgSetSlotPadding(iconSlot, 0.0f, 5.0f, 10.0f, 5.0f); umgSetVAlign(iconSlot, 2); }
                            }
                        }

                        // Check if item is a container; use captured count from FItemInstance
                        bool isContainer = false;
                        int32_t stackCount = m_lastPickedUpCount > 0 ? m_lastPickedUpCount : 1;
                        {
                            auto* ihfClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/FGK.ItemHandleFunctions"));
                            if (ihfClass)
                            {
                                UObject* ihfCDO = ihfClass->GetClassDefaultObject();
                                if (ihfCDO)
                                {
                                    auto* isContFn = ihfClass->GetFunctionByNameInChain(STR("IsContainer"));
                                    if (isContFn)
                                    {
                                        int csz = isContFn->GetParmsSize();
                                        std::vector<uint8_t> cbuf(csz, 0);
                                        auto* pH = findParam(isContFn, STR("Item"));
                                        auto* pR = findParam(isContFn, STR("ReturnValue"));
                                        if (pH) std::memcpy(cbuf.data() + pH->GetOffset_Internal(), m_lastItemHandle, 20);
                                        if (safeProcessEvent(ihfCDO, isContFn, cbuf.data()) && pR)
                                            isContainer = *reinterpret_cast<bool*>(cbuf.data() + pR->GetOffset_Internal());
                                    }
                                    // stackCount already set from m_lastPickedUpCount (captured from FItemInstance)
                                }
                            }
                        }

                        // Build display label: "Iron Pickaxe x3" or "Leather Bag (+ contents)"
                        std::wstring labelText = m_lastPickedUpDisplayName;
                        if (stackCount > 1) labelText += L" x" + std::to_wstring(stackCount);
                        if (isContainer) labelText += L"  (+ contents)";

                        // Item name label (display name, not class name)
                        UObject* nameLbl = createTextBlock(labelText, 0.9f, 0.75f, 0.2f, 1.0f, 22);
                        if (nameLbl) { umgSetBold(nameLbl); UObject* ns = addToHBox(itemRow, nameLbl); if (ns) { umgSetSlotPadding(ns, 0.0f, 5.0f, 0.0f, 5.0f); umgSetVAlign(ns, 2); } }
                        UObject* itemSlot = addToVBox(contentVBox, itemRow);
                        if (itemSlot) umgSetSlotPadding(itemSlot, 0.0f, 5.0f, 0.0f, 5.0f);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Trash] CP5: item icon + name done\n"));
                // Button row: HBox { CANCEL (red) | DELETE (green) }
                {
                    FStaticConstructObjectParameters btnHbP(hboxClass, outer);
                    UObject* btnRow = UObjectGlobals::StaticConstructObject(btnHbP);
                    if (btnRow)
                    {
                        // CANCEL button — solid red bg, white text
                        {
                            FStaticConstructObjectParameters cOlP(overlayClass, outer);
                            UObject* cancelOl = UObjectGlobals::StaticConstructObject(cOlP);
                            if (cancelOl)
                            {
                                FStaticConstructObjectParameters cImgP(imageClass, outer);
                                UObject* cImg = UObjectGlobals::StaticConstructObject(cImgP);
                                if (cImg) { umgSetBrushSize(cImg, 200.0f, 50.0f); umgSetImageColor(cImg, 0.6f, 0.12f, 0.12f, 1.0f); addToOverlay(cancelOl, cImg); }
                                UObject* cLbl = createTextBlock(L"CANCEL", 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cLbl) { umgSetBold(cLbl); UObject* cs = addToOverlay(cancelOl, cLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cSlot = addToHBox(btnRow, cancelOl);
                                if (cSlot) umgSetSlotPadding(cSlot, 20.0f, 0.0f, 30.0f, 0.0f);
                            }
                        }
                        // DELETE button — solid green bg, white text
                        {
                            FStaticConstructObjectParameters dOlP(overlayClass, outer);
                            UObject* deleteOl = UObjectGlobals::StaticConstructObject(dOlP);
                            if (deleteOl)
                            {
                                FStaticConstructObjectParameters dImgP(imageClass, outer);
                                UObject* dImg = UObjectGlobals::StaticConstructObject(dImgP);
                                if (dImg) { umgSetBrushSize(dImg, 200.0f, 50.0f); umgSetImageColor(dImg, 0.12f, 0.5f, 0.15f, 1.0f); addToOverlay(deleteOl, dImg); }
                                UObject* dLbl = createTextBlock(L"DELETE", 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (dLbl) { umgSetBold(dLbl); UObject* ds = addToOverlay(deleteOl, dLbl); if (ds) { umgSetHAlign(ds, 2); umgSetVAlign(ds, 2); } }
                                UObject* dSlot = addToHBox(btnRow, deleteOl);
                                if (dSlot) umgSetSlotPadding(dSlot, 0.0f, 0.0f, 20.0f, 0.0f);
                            }
                        }
                        UObject* btnSlot = addToVBox(contentVBox, btnRow);
                        if (btnSlot) { umgSetHAlign(btnSlot, 2); } // center the button row
                    }
                }
            }

            VLOG(STR("[MoriaCppMod] [Trash] CP6: all widgets built, about to AddToViewport...\n"));
            // Add to viewport at ZOrder 300 (above settings panel)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int avSz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(avSz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 300;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Set size
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize) { int ssz = setDesiredSizeFn->GetParmsSize(); std::vector<uint8_t> sb(ssz, 0); auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal()); v[0] = dlgW; v[1] = dlgH; userWidget->ProcessEvent(setDesiredSizeFn, sb.data()); }
            }

            // Center alignment
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn) { auto* pAlign = findParam(setAlignFn, STR("Alignment")); if (pAlign) { int asz = setAlignFn->GetParmsSize(); std::vector<uint8_t> ab(asz, 0); auto* a = reinterpret_cast<float*>(ab.data() + pAlign->GetOffset_Internal()); a[0] = 0.5f; a[1] = 0.5f; userWidget->ProcessEvent(setAlignFn, ab.data()); } }

            // Center position
            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f), m_screen.fracToPixelY(0.5f), true);

            VLOG(STR("[MoriaCppMod] [Trash] CP7: AddToViewport + sizing done\n"));
            m_trashDlgWidget = userWidget;
            m_trashDlgVisible = true;
            m_trashDlgOpenTick = GetTickCount64();
            setInputModeUI(userWidget);
            VLOG(STR("[MoriaCppMod] [Trash] Dialog opened for {}\n"), m_lastPickedUpItemName);
        }

        void hideTrashDialog()
        {
            if (!m_trashDlgVisible) return;
            if (m_trashDlgWidget)
            {
                auto* removeFn = m_trashDlgWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn) m_trashDlgWidget->ProcessEvent(removeFn, nullptr);
                m_trashDlgWidget = nullptr;
            }
            m_trashDlgVisible = false;
            // Restore input to settings panel if still open
            if (m_ftVisible && m_fontTestWidget)
                setInputModeUI(m_fontTestWidget);
            else
                setInputModeGame();
            VLOG(STR("[MoriaCppMod] [Trash] Dialog closed\n"));
        }

        void confirmTrashItem()
        {
            if (!m_trashDlgVisible) return;
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(L"Trash failed: no item selected", 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }

            VLOG(STR("[MoriaCppMod] [Trash] Confirming deletion of {}...\n"), m_lastPickedUpItemName);

            // Step 1: Find inventory component
            UObject* pawn = getPawn();
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp)
            {
                showOnScreen(L"Trash failed: no inventory", 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }

            // Step 2: Use stack count captured from FItemInstance at move time
            // (UItemHandleFunctions::GetCount returns 1 for stacks — unreliable)
            int32_t itemCount = m_lastPickedUpCount > 0 ? m_lastPickedUpCount : 1;
            VLOG(STR("[MoriaCppMod] [Trash] {} captured count={}\n"), m_lastPickedUpItemName, itemCount);

            if (itemCount <= 0)
            {
                showOnScreen(L"Trash failed: item count is 0", 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }

            // Step 3: DropItem(handle, count) removes the exact item instance from inventory
            // For containers (bags): DropItem drops the container WITH its contents into a single
            // MorDroppedItem actor. K2_DestroyActor on that actor destroys everything cleanly —
            // the MorDroppedItem has its own UMorInventoryComponent that holds the contents.
            // Then we destroy the spawned pickup actor so the item is truly trashed.
            auto* dropFn = invComp->GetFunctionByNameInChain(STR("DropItem"));
            if (!dropFn)
            {
                VLOG(STR("[MoriaCppMod] [Trash] DropItem not found on invComp\n"));
                showOnScreen(L"Trash failed: DropItem not found", 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }

            // Snapshot existing drop actors so we can find the new one after the drop
            std::vector<UObject*> preDropActors;
            UObjectGlobals::FindAllOf(STR("MorDroppedItem"), preDropActors);
            std::unordered_set<UObject*> preDropSet(preDropActors.begin(), preDropActors.end());
            VLOG(STR("[MoriaCppMod] [Trash] Pre-drop MorDroppedItem count={}\n"), preDropActors.size());

            {
                int sz = dropFn->GetParmsSize();
                std::vector<uint8_t> p(sz, 0);
                auto* pHandle = findParam(dropFn, STR("Item"));
                auto* pCount  = findParam(dropFn, STR("Count"));
                if (pHandle) std::memcpy(p.data() + pHandle->GetOffset_Internal(), m_lastItemHandle, 20);
                if (pCount)  *reinterpret_cast<int32_t*>(p.data() + pCount->GetOffset_Internal()) = itemCount;
                VLOG(STR("[MoriaCppMod] [Trash] Calling DropItem(handle, count={}) for {}\n"),
                     itemCount, m_lastPickedUpItemName);
                if (!safeProcessEvent(invComp, dropFn, p.data()))
                {
                    VLOG(STR("[MoriaCppMod] [Trash] DropItem CRASHED (SEH caught)\n"));
                    showOnScreen(L"Trash failed: DropItem crashed", 3.0f, 1.0f, 0.4f, 0.4f);
                    hideTrashDialog();
                    return;
                }
            }

            // Destroy the spawned drop actor so the item doesn't appear in the world
            {
                std::vector<UObject*> postDropActors;
                UObjectGlobals::FindAllOf(STR("MorDroppedItem"), postDropActors);
                VLOG(STR("[MoriaCppMod] [Trash] Post-drop MorDroppedItem count={}\n"), postDropActors.size());
                int destroyed = 0;
                for (auto* actor : postDropActors)
                {
                    if (!actor || preDropSet.count(actor)) continue;
                    // This is the newly spawned drop — destroy it
                    auto* destroyFn = actor->GetFunctionByNameInChain(STR("K2_DestroyActor"));
                    if (destroyFn)
                    {
                        safeProcessEvent(actor, destroyFn, nullptr);
                        destroyed++;
                        VLOG(STR("[MoriaCppMod] [Trash] Destroyed drop actor {}\n"), (void*)actor);
                    }
                }
                if (destroyed == 0)
                    VLOG(STR("[MoriaCppMod] [Trash] WARNING: no new drop actors found to destroy!\n"));
            }

            // Step 4: Remove tint effect via ServerTintItem(handle, nullptr, pawn) on CraftingComponent
            if (pawn)
            {
                UObject* craftComp = nullptr;
                {
                    std::vector<UObject*> allComps;
                    UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
                    for (auto* c : allComps)
                    {
                        if (!c) continue;
                        auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                        if (!ownerFunc) continue;
                        struct { UObject* Ret{nullptr}; } op{};
                        if (!safeProcessEvent(c, ownerFunc, &op)) continue;
                        if (op.Ret != pawn) continue;
                        std::wstring cls = safeClassName(c);
                        if (cls == STR("MorCraftingComponent")) { craftComp = c; break; }
                    }
                }
                if (craftComp)
                {
                    auto* tintFn = craftComp->GetFunctionByNameInChain(STR("ServerTintItem"));
                    if (tintFn)
                    {
                        int sz = tintFn->GetParmsSize();
                        std::vector<uint8_t> p(sz, 0);
                        auto* pHandle = findParam(tintFn, STR("ItemHandle"));
                        auto* pEffect = findParam(tintFn, STR("TintEffect"));
                        auto* pActor  = findParam(tintFn, STR("Interactor"));
                        if (pHandle) std::memcpy(p.data() + pHandle->GetOffset_Internal(), m_lastItemHandle, 20);
                        if (pEffect) *reinterpret_cast<UObject**>(p.data() + pEffect->GetOffset_Internal()) = nullptr;
                        if (pActor)  *reinterpret_cast<UObject**>(p.data() + pActor->GetOffset_Internal()) = pawn;
                        safeProcessEvent(craftComp, tintFn, p.data());
                        VLOG(STR("[MoriaCppMod] [Trash] Called ServerTintItem(nullptr) to remove tint\n"));
                    }
                }

                // Step 5: Remove rune effect from Effects.List
                int32_t targetID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
                if (invComp && removeItemEffectFromList(invComp, targetID, STR("MorRuneEffect")))
                    VLOG(STR("[MoriaCppMod] [Trash] Removed MorRuneEffect for item ID={}\n"), targetID);
            }

            // Step 6: Show confirmation message
            std::wstring msg = L"Trashed " + m_lastPickedUpDisplayName + L" x" + std::to_wstring(itemCount);
            showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] [Trash] {}\n"), msg);

            // Step 7: Close dialog and clear item state
            hideTrashDialog();
            m_lastPickedUpItemClass = nullptr;
            m_lastPickedUpItemName.clear();
            m_lastPickedUpDisplayName.clear();
            m_lastPickedUpCount = 0;
            std::memset(m_lastItemHandle, 0, 20);
            m_lastItemInvComp = nullptr;
        }
