// moria_inventory.inl — Inventory helpers: component lookup, tint/effects, trash/replenish/remove-attrs
// SEH wrapper via safeProcessEvent from moria_common.h (shadowed local copy removed — was infinite recursion bug)
// callServerTintItem centralizes 5 formerly copy-pasted tint blocks

        UObject* findActorComponentByClass(UObject* owner, const wchar_t* className)
        {
            if (!owner) return nullptr;
            std::vector<UObject*> allComps;
            findAllOfSafe(STR("ActorComponent"), allComps);
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


        // Lazy probe of the FActiveItemEffect struct layout. The Effects
        // member on a MorInventoryComponent is an FFastArraySerializer
        // whose `List` field is a TArray<FActiveItemEffect>. We need:
        //   - sizeof(FActiveItemEffect)            (the entry stride)
        //   - offset of OnItem (int32 itemID)
        //   - offset of Effect (UObject* effect class)
        // Falls back to the previously-hardcoded values (stride 0x30,
        // OnItem 0x0C, Effect 0x10) on any reflection failure - those
        // values are stable across the game's life today.
        bool ensureActiveItemEffectOffsets(UObject* invComp)
        {
            if (s_off_aieStride > 0 && s_off_aieOnItem >= 0 && s_off_aieEffect >= 0)
                return true;
            if (!invComp) return false;
            int stride = -1;
            int offOnItem = resolveArrayStructLayout(invComp, STR("Effects.List"), STR("OnItem"), &stride);
            // FFastArraySerializer.List isn't directly addressable as a
            // dotted property on most UE4SS reflection paths, so fall back
            // to walking via the Effects FStructProperty.
            if (offOnItem < 0)
            {
                if (auto* eProp = invComp->GetPropertyByNameInChain(STR("Effects")))
                {
                    if (auto* sProp = CastField<FStructProperty>(eProp))
                    {
                        UStruct* fastArrStruct = sProp->GetStruct();
                        if (fastArrStruct)
                        {
                            if (auto* listProp = fastArrStruct->GetPropertyByNameInChain(STR("List")))
                            {
                                if (auto* listArrProp = CastField<FArrayProperty>(listProp))
                                {
                                    auto* innerProp = listArrProp->GetInner();
                                    if (auto* innerStructProp = CastField<FStructProperty>(innerProp))
                                    {
                                        UStruct* aieStruct = innerStructProp->GetStruct();
                                        if (aieStruct)
                                        {
                                            if (auto* sStruct = static_cast<UScriptStruct*>(aieStruct))
                                                stride = static_cast<int>(sStruct->GetStructureSize());
                                            if (auto* onItemProp = aieStruct->GetPropertyByNameInChain(STR("OnItem")))
                                                offOnItem = onItemProp->GetOffset_Internal();
                                            if (auto* effectProp = aieStruct->GetPropertyByNameInChain(STR("Effect")))
                                                s_off_aieEffect = effectProp->GetOffset_Internal();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            s_off_aieStride = (stride > 0) ? stride : 0x30;
            s_off_aieOnItem = (offOnItem >= 0) ? offOnItem : 0x0C;
            if (s_off_aieEffect < 0) s_off_aieEffect = 0x10;
            return true;
        }

        // WARNING [W4]: Direct TArray memory manipulation — no UFUNCTION alternative exists.
        // Bypasses replication/internal bookkeeping. Bounds-checked. Single-player context only.
        // See memory/deferred-fixes.md for rationale.
        bool removeItemEffectFromList(UObject* invComp, int32_t itemID, const wchar_t* effectClassName)
        {
            if (!invComp) return false;
            FProperty* effectsProp = invComp->GetPropertyByNameInChain(STR("Effects"));
            if (!effectsProp) return false;

            ensureActiveItemEffectOffsets(invComp);
            int effectsOff = effectsProp->GetOffset_Internal();
            // The FFastArraySerializer.List inner offset (0x0110 in this
            // game's UE4.27 build) is engine-stable — not reflected, just
            // used as a constant. The interesting layout is the inner
            // FActiveItemEffect, which we DO resolve via reflection above.
            uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp) + effectsOff + 0x0110;
            if (!isReadableMemory(listBase, 16)) return false;

            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t& arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
            const int stride   = s_off_aieStride;
            const int offOnItem = s_off_aieOnItem;
            const int offEffect = s_off_aieEffect;

            for (int32_t j = arrNum - 1; j >= 0; j--)
            {
                uint8_t* e = arrData + j * stride;
                if (!isReadableMemory(e, stride)) continue;

                int32_t onItem = *reinterpret_cast<int32_t*>(e + offOnItem);
                if (onItem != itemID) continue;

                UObject* effect = *reinterpret_cast<UObject**>(e + offEffect);
                if (!effect || !isReadableMemory(reinterpret_cast<uint8_t*>(effect), 64)) continue;

                std::wstring cls = safeClassName(effect);
                if (cls != effectClassName) continue;

                VLOG(STR("[MoriaCppMod] [RemoveEffect] Removing {} at index {} (OnItem={}) from Effects.List\n"), effectClassName, j, onItem);
                if (j < arrNum - 1)
                    std::memcpy(e, arrData + (arrNum - 1) * stride, stride);
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
                ensureActiveItemEffectOffsets(invComp);
                int effectsOff = effectsProp->GetOffset_Internal();

                uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + effectsOff + 0x0110;
                if (isReadableMemory(effectsBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(effectsBase);
                    int32_t arrNum = *reinterpret_cast<int32_t*>(effectsBase + 8);
                    VLOG(STR("[MoriaCppMod] [EffectDump] InvComp Effects.List count={} (effectsOff=0x{:X} stride=0x{:X})\n"),
                         arrNum, effectsOff, s_off_aieStride);

                    const int stride    = s_off_aieStride;
                    const int offOnItem = s_off_aieOnItem;
                    const int offEffect = s_off_aieEffect;
                    // EndTime + AssetId offsets aren't reflected by name in
                    // this build; keep diagnostic-only hardcoded fallbacks.
                    constexpr int offEndTime    = 0x18;
                    constexpr int offAssetIdLo  = 0x1C;
                    constexpr int offAssetIdHi  = 0x20;

                    for (int32_t j = 0; j < arrNum && j < 50; j++)
                    {
                        uint8_t* e = arrData + j * stride;
                        if (!isReadableMemory(e, stride)) { VLOG(STR("[MoriaCppMod] [EffectDump]   [{}] <unreadable>\n"), j); continue; }

                        int32_t onItem = *reinterpret_cast<int32_t*>(e + offOnItem);
                        UObject* effect = *reinterpret_cast<UObject**>(e + offEffect);
                        float endTime = *reinterpret_cast<float*>(e + offEndTime);

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


                        uint32_t assetIdWord0 = *reinterpret_cast<uint32_t*>(e + offAssetIdLo);
                        uint32_t assetIdWord1 = *reinterpret_cast<uint32_t*>(e + offAssetIdHi);

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
            // MP fix: audit local player's inventory only, not first dwarf found
            UObject* pawn = getPawn();
            if (!pawn) return;

            std::vector<UObject*> allComps;
            findAllOfSafe(STR("InventoryComponent"), allComps);

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

                    m_lastPickedUpCount = *reinterpret_cast<int32_t*>(entry + iiCountOff());

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
                // Log which tables were checked and their bind status
                VLOG(STR("[MoriaCppMod] [Replenish] FAILED for '{}' — not found in any table:\n"), rowName);
                const wchar_t* tblNames[] = {L"DT_Items", L"DT_Weapons", L"DT_Tools", L"DT_Armor", L"DT_Consumables", L"DT_Ores", L"DT_ContainerItems"};
                for (int ti = 0; ti < 7; ti++)
                    VLOG(STR("[MoriaCppMod] [Replenish]   {} bound={}\n"), tblNames[ti], itemTables[ti]->isBound() ? 1 : 0);
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


            // INS replenish: bump JUST the targeted stack to
            // MaxStack. NO MERGING with other stacks of the same class.
            //
            // Why we don't use ServerDebugSetItem(ItemClass, Count): it's
            // class-level - the server walks FItemInstance entries and
            // sets the FIRST-found stack's count to N, ignoring which
            // specific stack the user is hovering. With 2+ stacks of the
            // same item, the user's targeted stack is left untouched.
            //
            // Why we don't merge: per user direction, the other stacks
            // must remain unchanged. Only the targeted instance gets
            // bumped to MaxStack.
            //
            // The approach: walk the FItemInstance Items array, find the
            // entry whose ID matches m_lastItemHandle's ID, and write
            // its Count field directly via reflected offset. The same
            // pattern removeItemEffectFromList uses successfully for the
            // Effects FFastArraySerializer at moria_inventory.inl:49-84.
            //
            // Replication note: direct memory writes to FFastArraySerializer
            // entries are authoritative on the host (host IS the server,
            // so a local change is canonical and replicates to clients
            // on the next FastArray tick). On non-host clients the change
            // is local-only and may be overwritten by the next replication
            // pulse. For the common case (single-player and host), this
            // works correctly. Non-host clients should use the in-game
            // hotbar swap or trade with host instead.
            //
            // FItemInstance layout (FGK.hpp:1723-1733, total 0x30 bytes):
            //   +0x00  FFastArraySerializerItem header (16 bytes)
            //   +0x10  TSubclassOf<AInventoryItem> Item (8 bytes)
            //   +0x18  int32 Count                <- the field we write
            //   +0x1C  int32 Slot
            //   +0x20  int32 ID                   <- match against targetID
            //   +0x24  float Durability
            //   +0x28  int32 RepairCount
            //   +0x2C  int32 ContainerStartSlot
            // The 0x18 (Count) and 0x20 (ID) offsets are stable across all
            // tested patches; iiSize/iiItemOff/iiIDOff helpers in
            // moria_reflection.h cache the resolved offsets and fall back
            // to these constants if reflection fails.
            //
            // FItemHandle layout (FGK.hpp:1715, total 0x14 = 20 bytes):
            //   +0x00  int32 ID                   <- m_lastItemHandle bytes 0-3
            //   +0x04  int32 Payload
            //   +0x08  TWeakObjectPtr<UInventoryComponent> Owner (8 bytes)
            // m_lastItemHandle is captured during BroadcastToContainers_OnChanged
            // / ServerMoveItem / MoveSwapItem PE post-hooks (see captureLastChangedItem
            // earlier in this file).
            int32_t targetID = *reinterpret_cast<int32_t*>(m_lastItemHandle);
            if (targetID == 0)
            {
                showOnScreen(L"Replenish failed: no item targeted (move/hover an item first)",
                             3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            probeItemInstanceStruct(invComp);
            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp)
            {
                showOnScreen(L"Replenish failed: Items property not resolved",
                             3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            int itemsOff = itemsProp->GetOffset_Internal();
            uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp) + itemsOff + iiaListOff();
            if (!isReadableMemory(listBase, 16))
            {
                showOnScreen(L"Replenish failed: Items list unreadable",
                             3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t arrNum   = *reinterpret_cast<int32_t*>(listBase + 8);
            if (!arrData || arrNum <= 0 || arrNum > 10000)
            {
                showOnScreen(L"Replenish failed: Items array invalid",
                             3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            int stride   = iiSize();
            int idOff    = iiIDOff();
            int countOff = 0x18;
            uint8_t* targetEntry = nullptr;
            int32_t  targetCount = 0;
            for (int32_t i = 0; i < arrNum; i++)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;
                int32_t entryID = *reinterpret_cast<int32_t*>(entry + idOff);
                if (entryID == targetID)
                {
                    targetEntry = entry;
                    targetCount = *reinterpret_cast<int32_t*>(entry + countOff);
                    break;
                }
            }
            if (!targetEntry)
            {
                VLOG(STR("[MoriaCppMod] [Replenish] target ID={} not found in Items (arrNum={})\n"),
                     targetID, arrNum);
                showOnScreen(L"Replenish failed: targeted stack not found in inventory",
                             3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [Replenish] target ID={} count={} maxStack={}\n"),
                 targetID, targetCount, maxStack);

            if (targetCount >= maxStack)
            {
                std::wstring msg = rowName + L" already at max (" + std::to_wstring(maxStack) + L")";
                showOnScreen(msg, 3.0f, 0.3f, 1.0f, 0.3f);
                return;
            }

            // The actual replenish: direct write of Count = maxStack on
            // the targeted instance only. Other stacks of the same class
            // are untouched.
            int32_t deficit = maxStack - targetCount;
            *reinterpret_cast<int32_t*>(targetEntry + countOff) = maxStack;
            VLOG(STR("[MoriaCppMod] [Replenish] direct write: ID={} count {} -> {} (deficit={})\n"),
                 targetID, targetCount, maxStack, deficit);

            std::wstring msg = L"Replenished " + rowName + L" +" + std::to_wstring(deficit)
                             + L" (->" + std::to_wstring(maxStack) + L")";
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


        // Phase 4 fix: replaced home-rolled UMG dialog with the
        // game's own WBP_UI_GenericPopup_C template (same one used by the
        // session-history delete confirm in moria_session_history.inl). The
        // home-rolled version had no font setup so text didn't render in
        // certain conditions, and clicks went through cursor-rect math
        // that was fragile against pause-menu input mode and DPI scaling.
        // The GenericPopup uses real game UButtons whose OnButtonReleased
        // delegates we hook in the existing PE post-hook chain.
        FWeakObjectPtr m_pendingTrashPopup;

        void showTrashDialog()
        {
            if (m_trashDlgVisible) { VLOG(STR("[MoriaCppMod] [Trash] BLOCKED: already visible\n")); return; }
            if (!m_lastPickedUpItemClass)
            {
                showOnScreen(Loc::get("msg.no_item_selected"), 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Trash] Starting GenericPopup for {}...\n"), m_lastPickedUpItemName);

            // Resolve WBP_UI_GenericPopup_C class (same path as session-history).
            static UClass* s_genericPopupCls = nullptr;
            if (!s_genericPopupCls)
            {
                try
                {
                    s_genericPopupCls = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr,
                        STR("/Game/UI/PopUp/WBP_UI_GenericPopup.WBP_UI_GenericPopup_C"));
                }
                catch (...) {}
            }
            if (!s_genericPopupCls)
            {
                VLOG(STR("[MoriaCppMod] [Trash] WBP_UI_GenericPopup_C class not found\n"));
                showOnScreen(L"Trash: popup template not loaded — open inventory once first", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            UObject* popup = jw_createGameWidget(s_genericPopupCls);
            if (!popup) { VLOG(STR("[MoriaCppMod] [Trash] popup spawn failed\n")); return; }

            // AddToViewport at high ZOrder so it sits above pause menu (~100-200).
            if (auto* fn = popup->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                auto* p = findParam(fn, STR("ZOrder"));
                if (p) *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 500;
                safeProcessEvent(popup, fn, b.data());
            }

            // Build labelText (item name + count + container-marker).
            int32_t stackCount = m_lastPickedUpCount > 0 ? m_lastPickedUpCount : 1;
            bool isContainer = false;
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
            std::wstring itemDesc = m_lastPickedUpDisplayName;
            if (stackCount > 1) itemDesc += L" x" + std::to_wstring(stackCount);
            if (isContainer)    itemDesc += L"  (+ contents)";

            // Call OnShowWithTwoButtons(Title, Message, ConfirmButtonText, CancelButtonText).
            if (auto* showFn = popup->GetFunctionByNameInChain(STR("OnShowWithTwoButtons")))
            {
                std::vector<uint8_t> b(showFn->GetParmsSize(), 0);
                auto setText = [&](const wchar_t* parmName, const wchar_t* val) {
                    auto* p = findParam(showFn, parmName);
                    if (!p) return;
                    FText t(val);
                    std::memcpy(b.data() + p->GetOffset_Internal(), &t, sizeof(FText));
                };
                std::wstring msg = L"Permanently delete:\n" + itemDesc;
                setText(STR("Title"),             Loc::get("ui.trash_item_title").c_str());
                setText(STR("Message"),           msg.c_str());
                setText(STR("ConfirmButtonText"), Loc::get("ui.button_delete").c_str());
                setText(STR("CancelButtonText"),  Loc::get("ui.button_cancel").c_str());
                safeProcessEvent(popup, showFn, b.data());
            }

            m_trashDlgWidget    = popup;
            m_pendingTrashPopup = FWeakObjectPtr(popup);
            m_trashDlgVisible   = true;
            m_trashDlgOpenTick  = GetTickCount64();

            m_trashCursorWasVisible = false;
            if (auto* pc2 = findPlayerController())
                m_trashCursorWasVisible = getBoolProp(pc2, L"bShowMouseCursor");
            // only switch to UI-only input mode if cursor was
            // hidden (i.e. user pressed DEL while in pure gameplay). When
            // an existing UI (inventory) is already up, leave its input
            // mode alone - the popup buttons are polled via cursor pixel
            // position in dllmain.cpp, so we don't need Slate click
            // routing to reach them. This avoids the cursor-stuck-after-
            // close bug (the matched skip on close lives in hideTrashDialog).
            if (!m_trashCursorWasVisible)
                setInputModeUI(popup);
            VLOG(STR("[MoriaCppMod] [Trash] GenericPopup opened for {}\n"), m_lastPickedUpItemName);
        }

        // Called from the global ProcessEvent post-hook on
        // OnButtonReleasedEvent / OnMenuButtonClicked. Compares the firing
        // button to the trash popup's ConfirmButton/CancelButton members
        // and dispatches confirmTrashItem() or hideTrashDialog().
        void onTrashPopupButtonClicked(UObject* context)
        {
            UObject* popup = m_pendingTrashPopup.Get();
            if (!popup || !isObjectAlive(popup) || !context) return;

            auto* confirmPtr = popup->GetValuePtrByPropertyNameInChain<UObject*>(STR("ConfirmButton"));
            auto* cancelPtr  = popup->GetValuePtrByPropertyNameInChain<UObject*>(STR("CancelButton"));
            UObject* confirmBtn = confirmPtr ? *confirmPtr : nullptr;
            UObject* cancelBtn  = cancelPtr  ? *cancelPtr  : nullptr;

            bool isConfirm = (context == confirmBtn);
            bool isCancel  = (context == cancelBtn);
            if (!isConfirm && !isCancel) return;

            VLOG(STR("[MoriaCppMod] [Trash] popup button: {}\n"),
                 isConfirm ? STR("DELETE") : STR("CANCEL"));

            // Clear pending state BEFORE calling confirmTrashItem so its
            // call to hideTrashDialog → RemoveFromParent doesn't race.
            m_pendingTrashPopup = FWeakObjectPtr();

            if (isConfirm) confirmTrashItem(); // confirms + hides
            else            hideTrashDialog();
        }

        void hideTrashDialog()
        {
            if (!m_trashDlgVisible) return;
            if (m_trashDlgWidget)
            {
                // popup is now WBP_UI_GenericPopup_C. Try its
                // built-in Hide animation first (plays the Outro), then
                // RemoveFromParent for cleanup.
                if (auto* hideFn = m_trashDlgWidget->GetFunctionByNameInChain(STR("Hide")))
                {
                    std::vector<uint8_t> b(hideFn->GetParmsSize(), 0);
                    try { safeProcessEvent(m_trashDlgWidget, hideFn, b.data()); } catch (...) {}
                }
                auto* removeFn = m_trashDlgWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn) safeProcessEvent(m_trashDlgWidget, removeFn, nullptr);
                m_trashDlgWidget = nullptr;
            }
            m_pendingTrashPopup = FWeakObjectPtr();
            m_trashDlgVisible = false;

            // cursor-stuck-after-trash bug fix.
            // Old code path "m_trashCursorWasVisible == true": called
            // setInputModeGame() and then forced bShowMouseCursor=true,
            // creating a broken hybrid where input went to the character
            // (game mode) but cursor was visible-yet-uninteractive. User
            // had to press ESC to recover (game's ESC handler restores a
            // sane input mode). The fix: when cursor was visible BEFORE
            // the popup opened (i.e. inventory or other UI was open),
            // skip the input-mode reset entirely - the underlying native
            // UI's input mode is still valid and the popup didn't displace
            // the player's UI focus. Only call setInputModeGame() when we
            // know the world was in pure gameplay mode (cursor hidden)
            // before the popup.
            if (m_ftVisible && m_fontTestWidget)
                setInputModeUI(m_fontTestWidget);
            else if (!m_trashCursorWasVisible)
                setInputModeGame();
            // else: cursor was already visible before the popup opened,
            // so the underlying native UI (inventory, etc.) still has
            // input focus. Don't touch it.
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
                findAllOfSafe(STR("MorDroppedItem"), preDropActors);
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
                    findAllOfSafe(STR("MorDroppedItem"), postDropActors);
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
                                    { nowCount = *reinterpret_cast<int32_t*>(e3 + iiCountOff()); break; }
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

                                int32_t newCount = *reinterpret_cast<int32_t*>(entry2 + iiCountOff());
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
