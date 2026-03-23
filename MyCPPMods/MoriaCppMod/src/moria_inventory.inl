// moria_inventory.inl — Inventory helpers: component lookup, tint/effects, trash/replenish/remove-attrs (v5.5.0)
// SEH wrapper via safeProcessEvent from moria_common.h (shadowed local copy removed — was infinite recursion bug)
// callServerTintItem centralizes 5 formerly copy-pasted tint blocks

        UObject* findActorComponentByClass(UObject* owner, const wchar_t* className)
        {
            if (!owner) return nullptr;
            std::vector<UObject*> allComps;
            UObjectGlobals::FindAllOf(STR("ActorComponent"), allComps);
            for (auto* c : allComps)
            {
                if (!c) continue;
                auto* ownerFunc = c->GetFunctionByNameInChain(STR("GetOwner"));
                if (!ownerFunc) continue;
                struct { UObject* Ret{nullptr}; } op{};
                if (!safeProcessEvent(c, ownerFunc, &op)) continue;
                if (op.Ret != owner) continue;
                if (safeClassName(c) == className) return c;
            }
            return nullptr;
        }


        UObject* findPlayerInventoryComponent(UObject* playerChar)
        {
            return findActorComponentByClass(playerChar, STR("MorInventoryComponent"));
        }


        bool callServerTintItem(UObject* craftComp, const uint8_t* itemHandle, UObject* effect, UObject* interactor)
        {
            auto* tintFn = craftComp->GetFunctionByNameInChain(STR("ServerTintItem"));
            if (!tintFn) return false;
            int sz = tintFn->GetParmsSize();
            std::vector<uint8_t> p(sz, 0);
            auto* pHandle = findParam(tintFn, STR("ItemHandle"));
            auto* pEffect = findParam(tintFn, STR("TintEffect"));
            auto* pActor  = findParam(tintFn, STR("Interactor"));
            if (pHandle) std::memcpy(p.data() + pHandle->GetOffset_Internal(), itemHandle, 20);
            if (pEffect) *reinterpret_cast<UObject**>(p.data() + pEffect->GetOffset_Internal()) = effect;
            if (pActor)  *reinterpret_cast<UObject**>(p.data() + pActor->GetOffset_Internal()) = interactor;
            return safeProcessEvent(craftComp, tintFn, p.data());
        }


        // WARNING [W4]: Direct TArray memory manipulation — no UFUNCTION alternative exists.
        // Bypasses replication/internal bookkeeping. Bounds-checked. Single-player context only.
        // See memory/deferred-fixes.md for rationale.
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
            constexpr int kStride = 0x30;

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


        void dumpItemEffects(UClass* itemClass, int32_t itemID, UObject* invComp)
        {

            UObject* cdo = itemClass->GetClassDefaultObject();
            if (cdo)
            {

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


            FProperty* effectsProp = invComp->GetPropertyByNameInChain(STR("Effects"));
            if (effectsProp)
            {
                int effectsOff = effectsProp->GetOffset_Internal();

                uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + effectsOff + 0x0110;
                if (isReadableMemory(effectsBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(effectsBase);
                    int32_t arrNum = *reinterpret_cast<int32_t*>(effectsBase + 8);
                    VLOG(STR("[MoriaCppMod] [EffectDump] InvComp Effects.List count={} (effectsOff=0x{:X})\n"), arrNum, effectsOff);


                    constexpr int kStride = 0x30;

                    for (int32_t j = 0; j < arrNum && j < 50; j++)
                    {
                        uint8_t* e = arrData + j * kStride;
                        if (!isReadableMemory(e, kStride)) { VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] <unreadable>\n"), j); continue; }

                        int32_t onItem = *reinterpret_cast<int32_t*>(e + 0x0C);
                        UObject* effect = *reinterpret_cast<UObject**>(e + 0x10);
                        float endTime = *reinterpret_cast<float*>(e + 0x18);

                        std::wstring effectName = L"nullptr";
                        if (effect)
                        {
                            if (isReadableMemory(reinterpret_cast<uint8_t*>(effect), 64))
                            {
                                effectName = safeClassName(effect);

                            }
                            else
                            {
                                effectName = L"<bad-ptr>";
                            }
                        }


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


        // Inventory audit: detect/remove orphaned items at invalid slots, verify Body Inventory size.
        // Verbose logging only when s_verbose is true; corrections always run.
        void auditInventory()
        {
            std::vector<UObject*> dwarves;
            UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
            if (dwarves.empty()) return;
            UObject* pawn = dwarves[0];

            std::vector<UObject*> allComps;
            UObjectGlobals::FindAllOf(STR("InventoryComponent"), allComps);

            for (auto* comp : allComps)
            {
                if (!comp) continue;
                auto* ownerFunc = comp->GetFunctionByNameInChain(STR("GetOwner"));
                if (!ownerFunc) continue;
                struct { UObject* Ret{nullptr}; } op{};
                if (!safeProcessEvent(comp, ownerFunc, &op)) continue;
                if (op.Ret != pawn) continue;

                probeItemInstanceStruct(comp);

                FProperty* itemsProp = comp->GetPropertyByNameInChain(STR("Items"));
                if (!itemsProp) continue;
                int itemsOff = itemsProp->GetOffset_Internal();
                uint8_t* listBase = reinterpret_cast<uint8_t*>(comp) + itemsOff + iiaListOff();
                if (!isReadableMemory(listBase, 16)) continue;

                uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
                if (!arrData || arrNum <= 0 || arrNum > 10000) continue;

                int stride = iiSize();
                int itemOff = iiItemOff();
                int countOff = 0x18;
                int slotOff  = 0x1C;
                int idOff    = iiIDOff();
                int containerStartSlotOff = 0x2C;

                VLOG(STR("[MoriaCppMod] [InvAudit] ===== Inventory: {} items =====\n"), arrNum);

                // First pass: log all items (verbose only), collect container info
                int numContainers = 0;
                for (int32_t i = 0; i < arrNum; i++)
                {
                    uint8_t* entry = arrData + i * stride;
                    if (!isReadableMemory(entry, stride)) continue;
                    int32_t cs = *reinterpret_cast<int32_t*>(entry + containerStartSlotOff);
                    if (cs > 0) numContainers++;

                    if (s_verbose)
                    {
                        UClass* ic = *reinterpret_cast<UClass**>(entry + itemOff);
                        int32_t cnt = *reinterpret_cast<int32_t*>(entry + countOff);
                        int32_t sl  = *reinterpret_cast<int32_t*>(entry + slotOff);
                        int32_t id  = *reinterpret_cast<int32_t*>(entry + idOff);
                        std::wstring nm = ic ? ic->GetName() : STR("(null)");
                        std::wstring extra;
                        if (cs > 0) extra = STR(" [CONTAINER start=") + std::to_wstring(cs) + STR("]");
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [InvAudit]   [{}] id={} slot={} count={} class={}{}\n"),
                            i, id, sl, cnt, nm, extra);
                    }
                }

                // Second pass: find and remove orphaned items not in any container
                for (int32_t i = 0; i < arrNum; i++)
                {
                    uint8_t* entry = arrData + i * stride;
                    if (!isReadableMemory(entry, stride)) continue;

                    int32_t slot = *reinterpret_cast<int32_t*>(entry + slotOff);
                    int32_t cs2  = *reinterpret_cast<int32_t*>(entry + containerStartSlotOff);
                    if (cs2 > 0) continue;  // skip containers themselves

                    bool inContainer = false;
                    for (int32_t j = 0; j < arrNum; j++)
                    {
                        uint8_t* cEntry = arrData + j * stride;
                        if (!isReadableMemory(cEntry, stride)) continue;
                        int32_t cStart = *reinterpret_cast<int32_t*>(cEntry + containerStartSlotOff);
                        if (cStart <= 0) continue;
                        int32_t cMax = 101; // safe fallback (game uses ~101 slot spacing)
                        UClass* cClass = *reinterpret_cast<UClass**>(cEntry + itemOff);
                        if (cClass)
                        {
                            if (UObject* cdo = cClass->GetClassDefaultObject())
                            {
                                FProperty* sp = cdo->GetPropertyByNameInChain(STR("Storage"));
                                if (sp)
                                {
                                    uint8_t* sPtr = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(cdo) + sp->GetOffset_Internal());
                                    if (sPtr && isReadableMemory(sPtr, 0x44))
                                        cMax = *reinterpret_cast<int32_t*>(sPtr + 0x38);
                                }
                            }
                        }
                        if (slot >= cStart && slot < cStart + cMax) { inContainer = true; break; }
                    }

                    if (!inContainer)
                    {
                        UClass* ic2 = *reinterpret_cast<UClass**>(entry + itemOff);
                        int32_t id2 = *reinterpret_cast<int32_t*>(entry + idOff);
                        int32_t count2 = *reinterpret_cast<int32_t*>(entry + countOff);
                        std::wstring orphanName = ic2 ? ic2->GetName() : STR("(null)");

                        VLOG(STR("[MoriaCppMod] [InvAudit] *** ORPHANED: id={} slot={} count={} class={} — removing...\n"),
                            id2, slot, count2, orphanName);

                        auto* removeFn = comp->GetFunctionByNameInChain(STR("RemoveItem"));
                        if (removeFn && ic2)
                        {
                            int dsz = removeFn->GetParmsSize();
                            std::vector<uint8_t> dp(dsz, 0);
                            auto* itemParam = findParam(removeFn, STR("Item"));
                            auto* countParam = findParam(removeFn, STR("Count"));
                            auto* fromParam = findParam(removeFn, STR("From"));
                            if (itemParam) *reinterpret_cast<UClass**>(dp.data() + itemParam->GetOffset_Internal()) = ic2;
                            if (countParam) *reinterpret_cast<int32_t*>(dp.data() + countParam->GetOffset_Internal()) = count2;
                            if (fromParam) *reinterpret_cast<uint8_t*>(dp.data() + fromParam->GetOffset_Internal()) = 0;
                            if (safeProcessEvent(comp, removeFn, dp.data()))
                                VLOG(STR("[MoriaCppMod] [InvAudit] *** REMOVED orphaned {} x{}\n"), orphanName, count2);
                            else
                                VLOG(STR("[MoriaCppMod] [InvAudit] *** REMOVE FAILED for {}\n"), orphanName);
                        }
                    }
                }

                VLOG(STR("[MoriaCppMod] [InvAudit] ===== END ({} containers, {} items) =====\n"), numContainers, arrNum);
                break;  // only process the first matching inventory component
            }
        }


        void captureLastChangedItem(UObject* invComp, void* parms)
        {
            if (!invComp || !parms) return;


            int32_t handleID = *reinterpret_cast<int32_t*>(parms);
            if (handleID <= 0) return;


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
                        m_lastPickedUpDisplayName = m_lastPickedUpItemName;

                    m_lastPickedUpCount = *reinterpret_cast<int32_t*>(entry + 0x18);

                    std::memcpy(m_lastItemHandle, parms, 20);
                    m_lastItemInvComp = RC::Unreal::FWeakObjectPtr(invComp);
                    VLOG(STR("[MoriaCppMod] [Capture] item={} display='{}' ID={} count={}\n"), m_lastPickedUpItemName, m_lastPickedUpDisplayName, handleID, m_lastPickedUpCount);

                    if (s_verbose) dumpItemEffects(entryClass, handleID, invComp);
                }
                return;
            }
        }

        void replenishLastItem()
        {
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(Loc::get("msg.no_item_selected"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }


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


            if (!m_dtItems.isBound()) m_dtItems.bind(L"DT_Items");
            if (!m_dtWeapons.isBound()) m_dtWeapons.bind(L"DT_Weapons");
            if (!m_dtTools.isBound()) m_dtTools.bind(L"DT_Tools");
            if (!m_dtArmor.isBound()) m_dtArmor.bind(L"DT_Armor");
            if (!m_dtConsumables.isBound()) m_dtConsumables.bind(L"DT_Consumables");
            if (!m_dtOres.isBound()) m_dtOres.bind(L"DT_Ores");
            if (!m_dtContainerItems.isBound()) m_dtContainerItems.bind(L"DT_ContainerItems");


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


            UObject* pawn = getPawn();
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp)
            {
                showOnScreen(Loc::get("err.replenish_no_inventory"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }


            int32_t currentCount = 0;
            {
                auto* fn = invComp->GetFunctionByNameInChain(STR("GetItemCount"));
                if (fn)
                {

                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> p(sz, 0);

                    auto* pItem = findParam(fn, STR("Item"));
                    auto* pFrom = findParam(fn, STR("From"));
                    auto* pRet  = findParam(fn, STR("ReturnValue"));

                    if (pItem) *reinterpret_cast<UClass**>(p.data() + pItem->GetOffset_Internal()) = m_lastPickedUpItemClass;
                    if (pFrom) *reinterpret_cast<uint8_t*>(p.data() + pFrom->GetOffset_Internal()) = 0;
                    safeProcessEvent(invComp, fn, p.data());
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
                if (pMethod) *reinterpret_cast<uint8_t*>(p.data() + pMethod->GetOffset_Internal()) = 0;

                VLOG(STR("[MoriaCppMod] [Replenish] Calling RequestAddItem({}, count={}, method=Normal)\n"),
                     m_lastPickedUpItemName, deficit);
                safeProcessEvent(invComp, addFn, p.data());
            }

            std::wstring msg = L"Replenished " + rowName + L" +" + std::to_wstring(deficit) + L" (→" + std::to_wstring(maxStack) + L")";
            showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
        }


        void removeItemAttributes()
        {
            if (!m_lastPickedUpItemClass || !m_lastItemInvComp.Get())
            {
                showOnScreen(Loc::get("msg.no_item_selected"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }


            UObject* pawn = getPawn();
            if (!pawn)
            {
                showOnScreen(Loc::get("err.remove_attrs_no_pawn"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            UObject* craftComp = findActorComponentByClass(pawn, STR("MorCraftingComponent"));
            if (!craftComp)
            {
                showOnScreen(Loc::get("err.remove_attrs_no_craft"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            int removed = 0;


            if (callServerTintItem(craftComp, m_lastItemHandle, nullptr, pawn))
            {
                removed++;
                VLOG(STR("[MoriaCppMod] [RemoveAttrs] Called ServerTintItem(nullptr) on {}\n"), m_lastPickedUpItemName);
            }


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
                showOnScreen(Loc::get("err.remove_attrs_no_functions"), 3.0f, 1.0f, 0.4f, 0.4f);
            }
        }


        void showTrashDialog()
        {
            if (m_trashDlgVisible) { VLOG(STR("[MoriaCppMod] [Trash] BLOCKED: already visible\n")); return; }
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(Loc::get("msg.no_item_selected"), 3.0f, 1.0f, 0.4f, 0.4f);
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
            safeProcessEvent(wblClass, createFn, cp.data());
            UObject* userWidget = *reinterpret_cast<UObject**>(cp.data() + pRet->GetOffset_Internal());
            if (!userWidget) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: userWidget is null\n")); return; }
            VLOG(STR("[MoriaCppMod] [Trash] CP1: userWidget created\n"));

            UObject* outer = userWidget;
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = (wtSlot && *wtSlot) ? *wtSlot : nullptr;

            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));


            UObject* texBG = findTexture2DByName(L"T_UI_Pnl_Background_Base");
            UObject* texSectionBg = findTexture2DByName(L"T_UI_Pnl_TabSelected");

            VLOG(STR("[MoriaCppMod] [Trash] CP2: textures done\n"));

            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOl = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOl) { VLOG(STR("[MoriaCppMod] [Trash] FAIL: rootOl is null\n")); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOl);


            {
                auto* setClipFn = rootOl->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz2 = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz2, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; safeProcessEvent(rootOl, setClipFn, cp2.data()); }
            }

            VLOG(STR("[MoriaCppMod] [Trash] CP3: rootOl + clipping done\n"));
            const float dlgW = 500.0f, dlgH = 180.0f;


            {
                FStaticConstructObjectParameters bfP(imageClass, outer);
                UObject* bf = UObjectGlobals::StaticConstructObject(bfP);
                if (bf) { umgSetBrushSize(bf, dlgW, dlgH); umgSetImageColor(bf, 0.08f, 0.14f, 0.32f, 1.0f); addToOverlay(rootOl, bf); }
            }


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


            FStaticConstructObjectParameters cvP(vboxClass, outer);
            UObject* contentVBox = UObjectGlobals::StaticConstructObject(cvP);
            if (contentVBox)
            {
                UObject* cvSlot = addToOverlay(rootOl, contentVBox);
                if (cvSlot) umgSetSlotPadding(cvSlot, 20.0f, 10.0f, 20.0f, 10.0f);


                if (texSectionBg && setBrushFn)
                {
                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                    if (secOl)
                    {
                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, dlgW - 40.0f, 50.0f); addToOverlay(secOl, secImg); }
                        UObject* secLabel = createTextBlock(Loc::get("ui.trash_item_title"), 0.78f, 0.86f, 1.0f, 1.0f, 24);
                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                        addToVBox(contentVBox, secOl);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Trash] CP4: header section done\n"));

                {
                    FStaticConstructObjectParameters itemHbP(hboxClass, outer);
                    UObject* itemRow = UObjectGlobals::StaticConstructObject(itemHbP);
                    if (itemRow)
                    {

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

                                }
                            }
                        }


                        std::wstring labelText = m_lastPickedUpDisplayName;
                        if (stackCount > 1) labelText += L" x" + std::to_wstring(stackCount);
                        if (isContainer) labelText += L"  (+ contents)";


                        UObject* nameLbl = createTextBlock(labelText, 0.9f, 0.75f, 0.2f, 1.0f, 22);
                        if (nameLbl) { umgSetBold(nameLbl); UObject* ns = addToHBox(itemRow, nameLbl); if (ns) { umgSetSlotPadding(ns, 0.0f, 5.0f, 0.0f, 5.0f); umgSetVAlign(ns, 2); } }
                        UObject* itemSlot = addToVBox(contentVBox, itemRow);
                        if (itemSlot) umgSetSlotPadding(itemSlot, 0.0f, 5.0f, 0.0f, 5.0f);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Trash] CP5: item icon + name done\n"));

                {
                    FStaticConstructObjectParameters btnHbP(hboxClass, outer);
                    UObject* btnRow = UObjectGlobals::StaticConstructObject(btnHbP);
                    if (btnRow)
                    {

                        {
                            FStaticConstructObjectParameters cOlP(overlayClass, outer);
                            UObject* cancelOl = UObjectGlobals::StaticConstructObject(cOlP);
                            if (cancelOl)
                            {
                                FStaticConstructObjectParameters cImgP(imageClass, outer);
                                UObject* cImg = UObjectGlobals::StaticConstructObject(cImgP);
                                if (cImg) { umgSetBrushSize(cImg, 200.0f, 50.0f); umgSetImageColor(cImg, 0.6f, 0.12f, 0.12f, 1.0f); addToOverlay(cancelOl, cImg); }
                                UObject* cLbl = createTextBlock(Loc::get("ui.button_cancel"), 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cLbl) { umgSetBold(cLbl); UObject* cs = addToOverlay(cancelOl, cLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cSlot = addToHBox(btnRow, cancelOl);
                                if (cSlot) umgSetSlotPadding(cSlot, 20.0f, 0.0f, 30.0f, 0.0f);
                            }
                        }

                        {
                            FStaticConstructObjectParameters dOlP(overlayClass, outer);
                            UObject* deleteOl = UObjectGlobals::StaticConstructObject(dOlP);
                            if (deleteOl)
                            {
                                FStaticConstructObjectParameters dImgP(imageClass, outer);
                                UObject* dImg = UObjectGlobals::StaticConstructObject(dImgP);
                                if (dImg) { umgSetBrushSize(dImg, 200.0f, 50.0f); umgSetImageColor(dImg, 0.12f, 0.5f, 0.15f, 1.0f); addToOverlay(deleteOl, dImg); }
                                UObject* dLbl = createTextBlock(Loc::get("ui.button_delete"), 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (dLbl) { umgSetBold(dLbl); UObject* ds = addToOverlay(deleteOl, dLbl); if (ds) { umgSetHAlign(ds, 2); umgSetVAlign(ds, 2); } }
                                UObject* dSlot = addToHBox(btnRow, deleteOl);
                                if (dSlot) umgSetSlotPadding(dSlot, 0.0f, 0.0f, 20.0f, 0.0f);
                            }
                        }
                        UObject* btnSlot = addToVBox(contentVBox, btnRow);
                        if (btnSlot) { umgSetHAlign(btnSlot, 2); }
                    }
                }
            }

            VLOG(STR("[MoriaCppMod] [Trash] CP6: all widgets built, about to AddToViewport...\n"));

            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int avSz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(avSz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 300;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize) { int ssz = setDesiredSizeFn->GetParmsSize(); std::vector<uint8_t> sb(ssz, 0); auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal()); v[0] = dlgW; v[1] = dlgH; safeProcessEvent(userWidget, setDesiredSizeFn, sb.data()); }
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn) { auto* pAlign = findParam(setAlignFn, STR("Alignment")); if (pAlign) { int asz = setAlignFn->GetParmsSize(); std::vector<uint8_t> ab(asz, 0); auto* a = reinterpret_cast<float*>(ab.data() + pAlign->GetOffset_Internal()); a[0] = 0.5f; a[1] = 0.5f; safeProcessEvent(userWidget, setAlignFn, ab.data()); } }


            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f), m_screen.fracToPixelY(0.5f), true);

            VLOG(STR("[MoriaCppMod] [Trash] CP7: AddToViewport + sizing done\n"));
            m_trashDlgWidget = userWidget;
            m_trashDlgVisible = true;
            m_trashDlgOpenTick = GetTickCount64();


            m_trashCursorWasVisible = false;
            if (auto* pc2 = findPlayerController())
                m_trashCursorWasVisible = getBoolProp(pc2, L"bShowMouseCursor");
            setInputModeUI(userWidget);
            VLOG(STR("[MoriaCppMod] [Trash] Dialog opened for {}\n"), m_lastPickedUpItemName);
        }

        void hideTrashDialog()
        {
            if (!m_trashDlgVisible) return;
            if (m_trashDlgWidget)
            {
                auto* removeFn = m_trashDlgWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn) safeProcessEvent(m_trashDlgWidget, removeFn, nullptr);
                m_trashDlgWidget = nullptr;
            }
            m_trashDlgVisible = false;

            if (m_ftVisible && m_fontTestWidget)
                setInputModeUI(m_fontTestWidget);
            else if (m_trashCursorWasVisible)
            {

                setInputModeGame();
                if (auto* pc2 = findPlayerController())
                    setBoolProp(pc2, L"bShowMouseCursor", true);
            }
            else
                setInputModeGame();
            VLOG(STR("[MoriaCppMod] [Trash] Dialog closed (cursorRestore={})\n"),
                 m_trashCursorWasVisible ? STR("yes") : STR("no"));
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


            UObject* pawn = getPawn();
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp)
            {
                showOnScreen(Loc::get("err.trash_no_inventory"), 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }


            int32_t itemCount = m_lastPickedUpCount > 0 ? m_lastPickedUpCount : 1;
            VLOG(STR("[MoriaCppMod] [Trash] {} captured count={}\n"), m_lastPickedUpItemName, itemCount);

            if (itemCount <= 0)
            {
                showOnScreen(L"Trash failed: item count is 0", 3.0f, 1.0f, 0.4f, 0.4f);
                hideTrashDialog();
                return;
            }


            hideTrashDialog();


            auto* dropFn = invComp->GetFunctionByNameInChain(STR("DropItem"));
            if (!dropFn)
            {
                VLOG(STR("[MoriaCppMod] [Trash] DropItem not found on invComp\n"));
                showOnScreen(L"Trash failed: DropItem not found", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }


            auto* pHandle = findParam(dropFn, STR("Item"));
            auto* pCount  = findParam(dropFn, STR("Count"));
            int32_t remaining = itemCount;
            int totalDestroyed = 0;
            static constexpr int MAX_DROP_ITERATIONS = 200;

            for (int iter = 0; iter < MAX_DROP_ITERATIONS && remaining > 0; iter++)
            {

                std::vector<UObject*> preDropActors;
                UObjectGlobals::FindAllOf(STR("MorDroppedItem"), preDropActors);
                std::unordered_set<UObject*> preDropSet(preDropActors.begin(), preDropActors.end());


                {
                    int sz = dropFn->GetParmsSize();
                    std::vector<uint8_t> p(sz, 0);
                    if (pHandle) std::memcpy(p.data() + pHandle->GetOffset_Internal(), m_lastItemHandle, 20);
                    if (pCount)  *reinterpret_cast<int32_t*>(p.data() + pCount->GetOffset_Internal()) = remaining;
                    if (iter == 0)
                        VLOG(STR("[MoriaCppMod] [Trash] DropItem(handle, count={}) for {}\n"),
                             remaining, m_lastPickedUpItemName);
                    if (!safeProcessEvent(invComp, dropFn, p.data()))
                    {
                        VLOG(STR("[MoriaCppMod] [Trash] DropItem CRASHED (SEH caught) on iter {}\n"), iter);
                        showOnScreen(L"Trash failed: DropItem crashed", 3.0f, 1.0f, 0.4f, 0.4f);
                        return;
                    }
                }


                {
                    std::vector<UObject*> postDropActors;
                    UObjectGlobals::FindAllOf(STR("MorDroppedItem"), postDropActors);
                    for (auto* actor : postDropActors)
                    {
                        if (!actor || preDropSet.count(actor)) continue;
                        auto* destroyFn2 = actor->GetFunctionByNameInChain(STR("K2_DestroyActor"));
                        if (destroyFn2) { safeProcessEvent(actor, destroyFn2, nullptr); totalDestroyed++; }
                    }
                }


                int32_t nowCount = 0;
                {
                    FProperty* itemsProp3 = invComp->GetPropertyByNameInChain(STR("Items"));
                    if (itemsProp3)
                    {
                        int off3 = itemsProp3->GetOffset_Internal();
                        uint8_t* lb3 = reinterpret_cast<uint8_t*>(invComp) + off3 + iiaListOff();
                        if (isReadableMemory(lb3, 16))
                        {
                            uint8_t* ad3 = *reinterpret_cast<uint8_t**>(lb3);
                            int32_t an3 = *reinterpret_cast<int32_t*>(lb3 + 8);
                            int32_t origID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
                            if (ad3 && an3 > 0 && an3 < 10000)
                            {
                                int stride3 = iiSize(), idOff3 = iiIDOff();
                                for (int32_t i = 0; i < an3; i++)
                                {
                                    uint8_t* e3 = ad3 + i * stride3;
                                    if (!isReadableMemory(e3, stride3)) continue;
                                    if (*reinterpret_cast<int32_t*>(e3 + idOff3) == origID)
                                    { nowCount = *reinterpret_cast<int32_t*>(e3 + 0x18); break; }
                                }
                            }
                        }
                    }
                }
                if (nowCount <= 0) break;
                if (nowCount >= remaining)
                {
                    VLOG(STR("[MoriaCppMod] [Trash] DropItem had no effect (count still {}) — stopping\n"), nowCount);
                    break;
                }
                remaining = nowCount;
            }
            VLOG(STR("[MoriaCppMod] [Trash] Drop loop done: {} drop actors destroyed\n"), totalDestroyed);


            if (pawn)
            {
                UObject* craftComp = findActorComponentByClass(pawn, STR("MorCraftingComponent"));
                if (craftComp)
                {
                    if (callServerTintItem(craftComp, m_lastItemHandle, nullptr, pawn))
                        VLOG(STR("[MoriaCppMod] [Trash] Called ServerTintItem(nullptr) to remove tint\n"));
                }


                int32_t targetID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
                if (invComp && removeItemEffectFromList(invComp, targetID, STR("MorRuneEffect")))
                    VLOG(STR("[MoriaCppMod] [Trash] Removed MorRuneEffect for item ID={}\n"), targetID);
            }


            std::wstring msg = L"Trashed " + m_lastPickedUpDisplayName + L" x" + std::to_wstring(itemCount);
            showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] [Trash] {}\n"), msg);


            bool recaptured = false;
            int32_t originalID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
            if (invComp && originalID > 0)
            {
                FProperty* itemsProp2 = invComp->GetPropertyByNameInChain(STR("Items"));
                if (itemsProp2)
                {
                    int itemsOff2 = itemsProp2->GetOffset_Internal();
                    uint8_t* invBase2 = reinterpret_cast<uint8_t*>(invComp);
                    uint8_t* listBase2 = invBase2 + itemsOff2 + iiaListOff();
                    if (isReadableMemory(listBase2, 16))
                    {
                        uint8_t* arrData2 = *reinterpret_cast<uint8_t**>(listBase2);
                        int32_t arrNum2 = *reinterpret_cast<int32_t*>(listBase2 + 8);
                        if (arrData2 && arrNum2 > 0 && arrNum2 < 10000)
                        {
                            int stride2 = iiSize();
                            int idOff2 = iiIDOff();
                            for (int32_t i = 0; i < arrNum2; i++)
                            {
                                uint8_t* entry2 = arrData2 + i * stride2;
                                if (!isReadableMemory(entry2, stride2)) continue;
                                int32_t entryID = *reinterpret_cast<int32_t*>(entry2 + idOff2);
                                if (entryID != originalID) continue;

                                int32_t newCount = *reinterpret_cast<int32_t*>(entry2 + 0x18);
                                if (newCount <= 0) break;
                                m_lastPickedUpCount = newCount;
                                m_lastItemInvComp = RC::Unreal::FWeakObjectPtr(invComp);
                                recaptured = true;
                                VLOG(STR("[MoriaCppMod] [Trash] Same instance ID={} still exists — remaining count={}\n"),
                                     originalID, newCount);
                                break;
                            }
                        }
                    }
                }
            }

            if (!recaptured)
            {

                m_lastPickedUpItemClass = nullptr;
                m_lastPickedUpItemName.clear();
                m_lastPickedUpDisplayName.clear();
                m_lastPickedUpCount = 0;
                std::memset(m_lastItemHandle, 0, 20);
                m_lastItemInvComp = RC::Unreal::FWeakObjectPtr{};
            }
        }
