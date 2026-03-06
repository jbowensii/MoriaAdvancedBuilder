// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_inventory.inl — Container discovery, toolbar swap state machine     ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // ──── 6E: Inventory & Toolbar System ────────────────────────────────────

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
                c->ProcessEvent(ownerFunc, &op);
                if (op.Ret != playerChar) continue;
                std::wstring cls = safeClassName(c);
                if (cls == STR("MorInventoryComponent")) return c;
            }
            return nullptr;
        }

        // Discover the EpicPack bag container handle for swapToolbar
        bool discoverBagHandle(UObject* invComp)
        {
            auto* getContainersFunc = invComp->GetFunctionByNameInChain(STR("GetContainers"));
            if (!getContainersFunc)
            {
                showOnScreen(L"GetContainers not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return false;
            }

            struct ParamInfo
            {
                int offset{-1};
                int size{0};
            };
            auto findParam = [](UFunction* func, const wchar_t* name) -> ParamInfo {
                for (auto* prop : func->ForEachProperty())
                {
                    if (std::wstring(prop->GetName()) == name) return {prop->GetOffset_Internal(), prop->GetSize()};
                }
                return {};
            };

            auto* getSlotFunc = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
            int handleSize = getSlotFunc ? findParam(getSlotFunc, L"ReturnValue").size : 0;
            if (handleSize <= 0) handleSize = 20; // fallback

            auto contRet = findParam(getContainersFunc, L"ReturnValue");
            std::vector<uint8_t> contBuf(std::max(getContainersFunc->GetParmsSize() + 32, 64), 0);
            invComp->ProcessEvent(getContainersFunc, contBuf.data());

            uint8_t* contData = *reinterpret_cast<uint8_t**>(contBuf.data() + contRet.offset);
            int32_t contNum = *reinterpret_cast<int32_t*>(contBuf.data() + contRet.offset + 8);

            // Build ID→className map from Items.List (offsets via probeItemInstanceStruct)

            static int s_off_invItems = -2;
            if (s_off_invItems == -2)
            {
                resolveOffset(invComp, L"Items", s_off_invItems);
                probeItemInstanceStruct(invComp);
            }
            int itemsListOffset = (s_off_invItems >= 0)
                ? s_off_invItems + iiaListOff()
                : 0x0330; // fallback to original hardcoded value

            uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
            struct
            {
                uint8_t* Data;
                int32_t Num;
                int32_t Max;
            } itemsList{};
            if (isReadableMemory(invBase + itemsListOffset, 16)) std::memcpy(&itemsList, invBase + itemsListOffset, 16);

            std::unordered_map<int32_t, std::wstring> idToClass;
            UObject* bodyInvClass = nullptr; // save BodyInventory UClass* for creating stash containers
            if (itemsList.Data && itemsList.Num > 0 && itemsList.Num < 500 && isReadableMemory(itemsList.Data, itemsList.Num * iiSize()))
            {
                for (int i = 0; i < itemsList.Num; i++)
                {
                    uint8_t* entry = itemsList.Data + i * iiSize();
                    int32_t id = *reinterpret_cast<int32_t*>(entry + iiIDOff());
                    UObject* cls = *reinterpret_cast<UObject**>(entry + iiItemOff());
                    if (cls && isReadableMemory(cls, 64))
                    {
                        try
                        {
                            std::wstring name = std::wstring(cls->GetName());
                            idToClass[id] = name;
                            if (name.find(STR("BodyInventory")) != std::wstring::npos) bodyInvClass = cls;
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }

            // Find IHF CDO for GetStorageMaxSlots diagnostic
            if (!m_ihfCDO)
            {
                m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
            }
            UFunction* ihfMaxSlots = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("GetStorageMaxSlots")) : nullptr;
            ParamInfo msItem{}, msRet{};
            if (ihfMaxSlots)
            {
                for (auto* prop : ihfMaxSlots->ForEachProperty())
                {
                    std::wstring pn(prop->GetName());
                    if (pn == L"Item") msItem = {prop->GetOffset_Internal(), prop->GetSize()};
                    if (pn == L"ReturnValue") msRet = {prop->GetOffset_Internal(), prop->GetSize()};
                }
            }

            // Find the EpicPack bag AND BodyInventory container handles
            m_bagHandle.clear();
            m_bodyInvHandle.clear();
            m_bodyInvHandles.clear();
            if (contData && contNum > 0 && contNum < 32 && isReadableMemory(contData, contNum * handleSize))
            {
                for (int i = 0; i < contNum; i++)
                {
                    uint8_t* hPtr = contData + i * handleSize;
                    int32_t cId = *reinterpret_cast<int32_t*>(hPtr);
                    auto it = idToClass.find(cId);
                    std::wstring cName = (it != idToClass.end()) ? it->second : L"(unknown)";

                    // Query max slots via IHF::GetStorageMaxSlots
                    int32_t maxSlots = -1;
                    if (ihfMaxSlots && msItem.offset >= 0 && msRet.offset >= 0)
                    {
                        std::vector<uint8_t> msBuf(std::max(ihfMaxSlots->GetParmsSize() + 32, 64), 0);
                        std::memcpy(msBuf.data() + msItem.offset, hPtr, handleSize);
                        m_ihfCDO->ProcessEvent(ihfMaxSlots, msBuf.data());
                        maxSlots = *reinterpret_cast<int32_t*>(msBuf.data() + msRet.offset);
                    }

                    if (cName.find(STR("EpicPack")) != std::wstring::npos)
                    {
                        m_bagHandle.assign(hPtr, hPtr + handleSize);
                    }
                    if (cName.find(STR("BodyInventory")) != std::wstring::npos)
                    {
                        m_bodyInvHandles.push_back(std::vector<uint8_t>(hPtr, hPtr + handleSize));
                    }
                    VLOG(STR("[MoriaCppMod] Container[{}] ID={} slots={} class='{}'\n"), i, cId, maxSlots, cName);
                }
            }

            if (!m_bodyInvHandles.empty())
            {
                m_bodyInvHandle = m_bodyInvHandles[0]; // fallback: first scanned container
            }
            VLOG(STR("[MoriaCppMod] Found {} BodyInventory containers\n"), m_bodyInvHandles.size());

            // Repair stale stash containers (>3 found): drop contents + containers, rebuild exactly 2
            if (m_bodyInvHandles.size() > 3 && !m_repairDone)
            {
                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Found {} BodyInventory containers (expected 3) Ã¢â‚¬â€ repairing\n"),
                                                m_bodyInvHandles.size());
                auto* dropFunc = invComp->GetFunctionByNameInChain(STR("DropItem"));
                auto* ihfGetSlotN = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot")) : nullptr;
                auto* ihfIsValidN = m_ihfCDO ? m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem")) : nullptr;
                if (dropFunc && ihfGetSlotN && ihfIsValidN)
                    {
                        auto diItem = findParam(dropFunc, L"Item");
                        auto diCount = findParam(dropFunc, L"Count");
                        int gsItem = -1, gsSlot = -1, gsRet = -1, ivItem = -1, ivRet = -1;
                        for (auto* p : ihfGetSlotN->ForEachProperty()) {
                            std::wstring n(p->GetName());
                            if (n == STR("Item")) gsItem = p->GetOffset_Internal();
                            else if (n == STR("Slot")) gsSlot = p->GetOffset_Internal();
                            else if (n == STR("ReturnValue")) gsRet = p->GetOffset_Internal();
                        }
                        for (auto* p : ihfIsValidN->ForEachProperty()) {
                            std::wstring n(p->GetName());
                            if (n == STR("Item")) ivItem = p->GetOffset_Internal();
                            else if (n == STR("ReturnValue")) ivRet = p->GetOffset_Internal();
                        }
                        for (int d = static_cast<int>(m_bodyInvHandles.size()) - 1; d >= 1; d--)
                        {
                            for (int s = 0; s < TOOLBAR_SLOTS; s++)
                            {
                                std::vector<uint8_t> gb(std::max(ihfGetSlotN->GetParmsSize() + 32, 64), 0);
                                std::memcpy(gb.data() + gsItem, m_bodyInvHandles[d].data(), handleSize);
                                *reinterpret_cast<int32_t*>(gb.data() + gsSlot) = s;
                                m_ihfCDO->ProcessEvent(ihfGetSlotN, gb.data());
                                std::vector<uint8_t> vb(std::max(ihfIsValidN->GetParmsSize() + 32, 64), 0);
                                std::memcpy(vb.data() + ivItem, gb.data() + gsRet, handleSize);
                                m_ihfCDO->ProcessEvent(ihfIsValidN, vb.data());
                                if (vb[ivRet] != 0)
                                {
                                    auto* ih = reinterpret_cast<const FItemHandleLocal*>(gb.data() + gsRet);
                                    VLOG(STR("[MoriaCppMod] Repair: dropping item ID={} from stash[{}] slot {}\n"), ih->ID, d, s);
                                    std::vector<uint8_t> db(std::max(dropFunc->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(db.data() + diItem.offset, gb.data() + gsRet, handleSize);
                                    *reinterpret_cast<int32_t*>(db.data() + diCount.offset) = 1;
                                    invComp->ProcessEvent(dropFunc, db.data());
                                }
                            }
                            int droppedId = reinterpret_cast<const FItemHandleLocal*>(m_bodyInvHandles[d].data())->ID;
                            std::vector<uint8_t> db(std::max(dropFunc->GetParmsSize() + 32, 64), 0);
                            std::memcpy(db.data() + diItem.offset, m_bodyInvHandles[d].data(), handleSize);
                            *reinterpret_cast<int32_t*>(db.data() + diCount.offset) = 1;
                            invComp->ProcessEvent(dropFunc, db.data());
                            VLOG(STR("[MoriaCppMod] Repair: dropped stash container ID={}\n"), droppedId);
                        }
                        m_bodyInvHandles.resize(1);
                        m_bodyInvHandle = m_bodyInvHandles[0];
                        m_repairDone = true;
                        Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Repair: stash containers removed Ã¢â‚¬â€ recreating fresh\n"));
                    }
            }

            // If only 1 BodyInventory (hotbar), create 2 more for toolbar swap stash
            if (m_bodyInvHandles.size() == 1 && bodyInvClass)
            {
                auto* addItemFunc = invComp->GetFunctionByNameInChain(STR("AddItem"));
                if (addItemFunc)
                {
                    auto aiItem = findParam(addItemFunc, L"Item");
                    auto aiCount = findParam(addItemFunc, L"Count");
                    auto aiMethod = findParam(addItemFunc, L"Method");
                    auto aiRet = findParam(addItemFunc, L"ReturnValue");

                    if (aiItem.offset >= 0 && aiCount.offset >= 0 && aiRet.offset >= 0)
                    {
                        for (int i = 0; i < 2; i++)
                        {
                            std::vector<uint8_t> aiBuf(std::max(addItemFunc->GetParmsSize() + 32, 128), 0);
                            *reinterpret_cast<UObject**>(aiBuf.data() + aiItem.offset) = bodyInvClass;
                            *reinterpret_cast<int32_t*>(aiBuf.data() + aiCount.offset) = 1;
                            if (aiMethod.offset >= 0) aiBuf[aiMethod.offset] = 1; // EAddItem::Create
                            invComp->ProcessEvent(addItemFunc, aiBuf.data());

                            std::vector<uint8_t> newHandle(aiBuf.data() + aiRet.offset, aiBuf.data() + aiRet.offset + handleSize);
                            int32_t newId = reinterpret_cast<const FItemHandleLocal*>(newHandle.data())->ID;
                            m_bodyInvHandles.push_back(newHandle);

                            VLOG(STR("[MoriaCppMod] Created stash BodyInventory #{} Ã¢â‚¬â€ ID={}\n"), i + 1, newId);
                        }
                        VLOG(STR("[MoriaCppMod] Now have {} BodyInventory containers (1 hotbar + 2 stash)\n"), m_bodyInvHandles.size());
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] AddItem param offsets not found (Item={} Count={} Ret={})\n"),
                                                        aiItem.offset,
                                                        aiCount.offset,
                                                        aiRet.offset);
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] AddItem function not found on inventory component\n"));
                }
            }
            else if (m_bodyInvHandles.size() == 1)
            {
                VLOG(STR("[MoriaCppMod] Only 1 BodyInventory but class not found Ã¢â‚¬â€ cannot create stash\n"));
            }

            if (m_bagHandle.empty())
            {
                VLOG(STR("[MoriaCppMod] Note: EpicPack bag not equipped\n"));
            }
            if (m_bodyInvHandle.empty())
            {
                showOnScreen(Loc::get("msg.body_inv_not_found"), 3.0f, 1.0f, 0.3f, 0.3f);
                return false;
            }
            return true;
        }


        // ── Toolbar Swap: PAGE_DOWN — 2 toolbars via BodyInventory containers ──
        static constexpr int TOOLBAR_SLOTS = 8;
        int m_activeToolbar{0}; // 0 or 1 Ã¢â‚¬â€ which toolbar is currently visible

        // Swap state machine (multi-frame, one move per tick)
        struct SwapState
        {
            bool active{false};
            bool resolved{false}; // true after name-matching resolve phase
            bool cleared{false};      // true after EmptyContainer on stash destination
            bool dropToGround{false}; // true = drop hotbar items instead of stashing (both-containers failsafe)
            int phase{0};         // 0 = stash hotbarÃ¢â€ â€™container, 1 = restore containerÃ¢â€ â€™hotbar
            int slot{0};          // current slot being processed (0-7)
            int moved{0};         // items successfully moved
            int wait{0};          // frame delay between operations
            int nextTB{0};        // which toolbar we're switching TO
            int curTB{0};         // which toolbar we're switching FROM
            int stashIdx{-1};     // resolved container index for stashing (1 or 2)
            int restoreIdx{-1};   // resolved container index for restoring (1 or 2)
        };
        SwapState m_swap{};

        std::vector<uint8_t> m_bodyInvHandle;               // hotbar container handle
        std::vector<std::vector<uint8_t>> m_bodyInvHandles; // all BodyInventory handles
        UObject* m_ihfCDO{nullptr};                         // UItemHandleFunctions CDO
        UObject* m_dropItemMgr{nullptr};                    // BP_DropItemManager for GetNameForItemHandle

        // Initiates toolbar swap: Resolve → Phase 0 (stash hotbar→container) → Phase 1 (restore container→hotbar)
        void swapToolbar()
        {
            VLOG(STR("[MoriaCppMod] [Swap] === swapToolbar() called ===\n"));
            try
            {
                VLOG(STR("[MoriaCppMod] [Swap] State: active={} bodyInvHandle.empty={} handles.size={} charLoaded={}\n"),
                                                m_swap.active,
                                                m_bodyInvHandle.empty(),
                                                m_bodyInvHandles.size(),
                                                m_characterLoaded);

                if (m_swap.active)
                {
                    showOnScreen(Loc::get("msg.swap_in_progress"), 2.0f, 1.0f, 1.0f, 0.0f);
                    VLOG(STR("[MoriaCppMod] [Swap] BLOCKED: swap already active\n"));
                    return;
                }
                if (m_bodyInvHandle.empty())
                {
                    VLOG(STR("[MoriaCppMod] [Swap] No cached handles, running discovery...\n"));
                    UObject* playerChar = nullptr;
                    {
                        std::vector<UObject*> actors;
                        UObjectGlobals::FindAllOf(STR("Character"), actors);
                        for (auto* a : actors)
                        {
                            if (a && safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                            {
                                playerChar = a;
                                break;
                            }
                        }
                    }
                    if (playerChar)
                    {
                        VLOG(STR("[MoriaCppMod] [Swap] Player found, discovering containers...\n"));
                        auto* invComp = findPlayerInventoryComponent(playerChar);
                        if (invComp)
                        {
                            discoverBagHandle(invComp);
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [Swap] FAIL: findPlayerInventoryComponent returned null\n"));
                        }
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [Swap] FAIL: Dwarf character not found in actors\n"));
                    }
                }
                if (m_bodyInvHandle.empty())
                {
                    showOnScreen(Loc::get("msg.body_inv_not_found"), 3.0f, 1.0f, 0.3f, 0.3f);
                    VLOG(STR("[MoriaCppMod] [Swap] FAIL: m_bodyInvHandle still empty after discovery\n"));
                    return;
                }
                // Need at least 3 BodyInventory containers: [0]=hotbar, [1]=T1 stash, [2]=T2 stash
                if (m_bodyInvHandles.size() < 3)
                {
                    showOnScreen(std::format(L"Need 3 BodyInventory containers, found {}", m_bodyInvHandles.size()), 3.0f, 1.0f, 0.3f, 0.3f);
                    VLOG(STR("[MoriaCppMod] [Swap] FAIL: only {} BodyInventory containers (need 3)\n"), m_bodyInvHandles.size());
                    return;
                }

                for (size_t hi = 0; hi < m_bodyInvHandles.size(); hi++)
                {
                    int32_t hid = reinterpret_cast<const FItemHandleLocal*>(m_bodyInvHandles[hi].data())->ID;
                    VLOG(STR("[MoriaCppMod] [Swap] Handle[{}] ID={} size={}\n"), hi, hid, m_bodyInvHandles[hi].size());
                }

                int curTB = m_activeToolbar;
                int nextTB = 1 - curTB; // toggle 0Ã¢â€ â€1

                VLOG(STR("[MoriaCppMod] toolbar swap: T{} -> T{} (stash->container[{}], restore<-container[{}])\n"),
                                                curTB + 1,
                                                nextTB + 1,
                                                curTB + 1,
                                                nextTB + 1);

                m_swap = {};
                m_swap.active = true;
                m_swap.resolved = false;
                m_swap.phase = 0;
                m_swap.slot = 0;
                m_swap.moved = 0;
                m_swap.wait = 0;
                m_swap.curTB = curTB;
                m_swap.nextTB = nextTB;
                m_swap.stashIdx = curTB + 1;    // default, may be overridden by resolve
                m_swap.restoreIdx = nextTB + 1; // default, may be overridden by resolve

                showOnScreen(std::format(L"Swapping to Toolbar {}...", nextTB + 1), 2.0f, 0.0f, 1.0f, 0.5f);
            }
            catch (const std::exception&)
            {
                VLOG(STR("[MoriaCppMod] [Swap] EXCEPTION in swapToolbar()\n"));
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [Swap] UNKNOWN EXCEPTION in swapToolbar()\n"));
            }
        }

        // Called from on_update() — processes one swap move per tick (Phase 0: stash, Phase 1: restore)
        void swapToolbarTick()
        {
            if (!m_swap.active) return;
            if (m_swap.wait > 0)
            {
                m_swap.wait--;
                return;
            }

            try
            {
                UObject* playerChar = nullptr;
                {
                    std::vector<UObject*> actors;
                    UObjectGlobals::FindAllOf(STR("Character"), actors);
                    for (auto* a : actors)
                    {
                        if (!a) continue;
                        if (safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                        {
                            playerChar = a;
                            break;
                        }
                    }
                }
                if (!playerChar)
                {
                    m_swap.active = false;
                    return;
                }
                auto* invComp = findPlayerInventoryComponent(playerChar);
                if (!invComp)
                {
                    m_swap.active = false;
                    return;
                }

                // Look up IHF functions for swap operations
                auto* moveFunc = invComp->GetFunctionByNameInChain(STR("MoveItem"));
                if (!m_ihfCDO)
                {
                    m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
                    if (m_ihfCDO)
                        VLOG(STR("[MoriaCppMod] Found IHF CDO: '{}'\n"), std::wstring(m_ihfCDO->GetName()));
                }
                if (!m_ihfCDO || !moveFunc)
                {
                    VLOG(STR("[MoriaCppMod] Swap: functions missing (IHF={} MoveItem={})\n"), m_ihfCDO != nullptr, moveFunc != nullptr);
                    m_swap.active = false;
                    return;
                }

                auto* ihfGetSlot = m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot"));
                auto* ihfIsValid = m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem"));
                if (!ihfGetSlot || !ihfIsValid)
                {
                    VLOG(STR("[MoriaCppMod] Swap: IHF functions missing (GetItemForSlot={} IsValidItem={})\n"),
                                                    ihfGetSlot != nullptr, ihfIsValid != nullptr);
                    m_swap.active = false;
                    return;
                }

                struct ParamInfo
                {
                    int offset{-1};
                    int size{0};
                };
                auto findParam = [](UFunction* func, const wchar_t* name) -> ParamInfo {
                    for (auto* prop : func->ForEachProperty())
                    {
                        if (std::wstring(prop->GetName()) == name) return {prop->GetOffset_Internal(), prop->GetSize()};
                    }
                    return {};
                };

                auto gsItem = findParam(ihfGetSlot, L"Item");
                auto gsSlot = findParam(ihfGetSlot, L"Slot");
                auto gsRet = findParam(ihfGetSlot, L"ReturnValue");
                auto ivItem = findParam(ihfIsValid, L"Item");
                auto ivRet = findParam(ihfIsValid, L"ReturnValue");
                auto mItem = findParam(moveFunc, L"Item");
                auto mDest = findParam(moveFunc, L"Destination");
                int handleSize = gsRet.size;
                if (handleSize <= 0) handleSize = 20;

                auto& hotbarContainer = m_bodyInvHandles[0];

                // Check if a container slot has a valid item; returns handle in slotBuf at gsRet.offset
                auto readSlot = [&](const std::vector<uint8_t>& container, int slot,
                                    std::vector<uint8_t>& slotBuf) -> bool {
                    slotBuf.assign(std::max(ihfGetSlot->GetParmsSize() + 32, 64), 0);
                    std::memcpy(slotBuf.data() + gsItem.offset, container.data(), handleSize);
                    *reinterpret_cast<int32_t*>(slotBuf.data() + gsSlot.offset) = slot;
                    m_ihfCDO->ProcessEvent(ihfGetSlot, slotBuf.data());
                    std::vector<uint8_t> vb(std::max(ihfIsValid->GetParmsSize() + 32, 64), 0);
                    std::memcpy(vb.data() + ivItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                    m_ihfCDO->ProcessEvent(ihfIsValid, vb.data());
                    return vb[ivRet.offset] != 0;
                };

                // ── Resolve Phase: determine stash/restore containers by item count ──
                if (!m_swap.resolved)
                {
                    m_swap.resolved = true;

                    auto countItems = [&](int cIdx) -> int {
                        int count = 0;
                        std::vector<uint8_t> buf;
                        for (int s = 0; s < TOOLBAR_SLOTS; s++)
                            if (readSlot(m_bodyInvHandles[cIdx], s, buf)) count++;
                        return count;
                    };

                    int c1Count = countItems(1);
                    int c2Count = countItems(2);

                    VLOG(STR("[MoriaCppMod] Resolve: c[1]={} items, c[2]={} items\n"), c1Count, c2Count);

                    if (c1Count == 0 && c2Count > 0)
                    {
                        // Container[1] empty, [2] has items
                        m_swap.stashIdx = 1;
                        m_swap.restoreIdx = 2;
                    }
                    else if (c2Count == 0 && c1Count > 0)
                    {
                        // Container[2] empty, [1] has items
                        m_swap.stashIdx = 2;
                        m_swap.restoreIdx = 1;
                    }
                    else if (c1Count > 0 && c2Count > 0)
                    {
                        // Both containers have items (ERROR STATE) — drop hotbar, restore from larger
                        m_swap.dropToGround = true;
                        m_swap.restoreIdx = (c1Count >= c2Count) ? 1 : 2;
                        Output::send<LogLevel::Warning>(
                            STR("[MoriaCppMod] Resolve: BOTH containers have items (c[1]={}, c[2]={}) Ã¢â‚¬â€ dropping hotbar, restoring from [{}]\n"),
                            c1Count, c2Count, m_swap.restoreIdx);
                    }
                    // else: both empty — keep default mapping (first swap, Toolbar 2 starts empty)

                    VLOG(STR("[MoriaCppMod] Resolve: RESULT stashIdx={} restoreIdx={} dropToGround={}\n"),
                                                    m_swap.stashIdx, m_swap.restoreIdx, m_swap.dropToGround);
                    m_swap.wait = 1;
                    return;
                }

                auto& stashContainer = m_bodyInvHandles[m_swap.stashIdx];
                auto& restoreContainer = m_bodyInvHandles[m_swap.restoreIdx];

                // ── Phase 0: Move hotbar items → stash container (or drop on ground) ──
                if (m_swap.phase == 0)
                {
                    if (m_swap.dropToGround)
                    {
                        // Failsafe: drop hotbar items on ground (both-containers state)
                        auto* dropFunc = invComp->GetFunctionByNameInChain(STR("DropItem"));
                        if (!dropFunc)
                        {
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: DropItem function not found\n"));
                            m_swap.active = false;
                            return;
                        }
                        auto diItem = findParam(dropFunc, L"Item");
                        auto diCount = findParam(dropFunc, L"Count");

                        for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                        {
                            std::vector<uint8_t> slotBuf;
                            if (!readSlot(hotbarContainer, slot, slotBuf))
                            {
                                m_swap.slot = slot + 1;
                                continue;
                            }

                            auto* slotHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                            std::vector<uint8_t> dropBuf(std::max(dropFunc->GetParmsSize() + 32, 128), 0);
                            std::memcpy(dropBuf.data() + diItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                            *reinterpret_cast<int32_t*>(dropBuf.data() + diCount.offset) = 999999;
                            invComp->ProcessEvent(dropFunc, dropBuf.data());

                            m_swap.moved++;
                            m_swap.slot = slot + 1;
                            Output::send<LogLevel::Warning>(
                                STR("[MoriaCppMod] Phase0: hotbar[{}] id={} DROPPED on ground (both-containers failsafe)\n"),
                                slot, slotHandle->ID);
                            m_swap.wait = 3;
                            return;
                        }
                    }
                    else
                    {
                        // Normal path: stash hotbar items; clear stash destination first
                        if (!m_swap.cleared)
                        {
                            m_swap.cleared = true;
                            auto* emptyFunc = invComp->GetFunctionByNameInChain(STR("EmptyContainer"));
                            if (emptyFunc)
                            {
                                auto emptyItem = findParam(emptyFunc, L"Item");
                                if (emptyItem.offset >= 0)
                                {
                                    std::vector<uint8_t> emptyBuf(std::max(emptyFunc->GetParmsSize() + 32, 64), 0);
                                    std::memcpy(emptyBuf.data() + emptyItem.offset, stashContainer.data(), handleSize);
                                    invComp->ProcessEvent(emptyFunc, emptyBuf.data());
                                    VLOG(STR("[MoriaCppMod] Phase0: EmptyContainer([{}]) Ã¢â‚¬â€ clearing stale items before stash\n"),
                                                                    m_swap.stashIdx);
                                }
                            }
                            m_swap.wait = 3;
                            return; // give a tick for empty to process
                        }

                        for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                        {
                            std::vector<uint8_t> slotBuf;
                            if (!readSlot(hotbarContainer, slot, slotBuf))
                            {
                                m_swap.slot = slot + 1;
                                continue;
                            }

                            auto* stashHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                            std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                            std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                            std::memcpy(moveBuf.data() + mDest.offset, stashContainer.data(), handleSize);
                            invComp->ProcessEvent(moveFunc, moveBuf.data());

                            // Validate slot is now empty
                            std::vector<uint8_t> verifyBuf;
                            bool stillHasItem = readSlot(hotbarContainer, slot, verifyBuf);
                            if (stillHasItem)
                            {
                                auto* vh = reinterpret_cast<const FItemHandleLocal*>(verifyBuf.data() + gsRet.offset);
                                Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase0: WARN hotbar[{}] still has item id={} after MoveItem\n"), slot, vh->ID);
                            }
                            else
                            {
                                VLOG(STR("[MoriaCppMod] Phase0: hotbar[{}] verified empty after move\n"), slot);
                            }

                            m_swap.moved++;
                            m_swap.slot = slot + 1;

                            VLOG(STR("[MoriaCppMod] Phase0: hotbar[{}] id={} -> container[{}]\n"), slot, stashHandle->ID, m_swap.stashIdx);

                            m_swap.wait = 3;
                            return;
                        }
                    }

                    VLOG(STR("[MoriaCppMod] Phase0 done: {} items {}. Restoring from container[{}]\n"),
                                                    m_swap.moved,
                                                    m_swap.dropToGround ? STR("dropped") : STR("stashed"),
                                                    m_swap.restoreIdx);

                    m_swap.phase = 1;
                    m_swap.slot = 0;
                    m_swap.wait = 3;
                    return;
                }

                // ── Phase 1: Move items from restore container → hotbar ──
                if (m_swap.phase == 1)
                {
                    for (int slot = m_swap.slot; slot < TOOLBAR_SLOTS; slot++)
                    {
                        std::vector<uint8_t> slotBuf;
                        if (!readSlot(restoreContainer, slot, slotBuf))
                        {
                            m_swap.slot = slot + 1;
                            continue;
                        }

                        auto* restHandle = reinterpret_cast<const FItemHandleLocal*>(slotBuf.data() + gsRet.offset);

                        VLOG(STR("[MoriaCppMod] Phase1: container[{}][{}] id={} -> hotbar\n"), m_swap.restoreIdx, slot, restHandle->ID);

                        std::vector<uint8_t> moveBuf(std::max(moveFunc->GetParmsSize() + 32, 128), 0);
                        std::memcpy(moveBuf.data() + mItem.offset, slotBuf.data() + gsRet.offset, handleSize);
                        std::memcpy(moveBuf.data() + mDest.offset, hotbarContainer.data(), handleSize);
                        invComp->ProcessEvent(moveFunc, moveBuf.data());

                        // Validate item arrived
                        std::vector<uint8_t> verifyBuf;
                        bool arrived = readSlot(hotbarContainer, slot, verifyBuf);
                        if (!arrived)
                            Output::send<LogLevel::Warning>(STR("[MoriaCppMod] Phase1: WARN hotbar[{}] still empty after MoveItem from container[{}]\n"), slot, m_swap.restoreIdx);
                        else
                        {
                            auto* vh = reinterpret_cast<const FItemHandleLocal*>(verifyBuf.data() + gsRet.offset);
                            VLOG(STR("[MoriaCppMod] Phase1: hotbar[{}] verified item id={} after move\n"), slot, vh->ID);
                        }

                        m_swap.moved++;

                        m_swap.slot = slot + 1;
                        m_swap.wait = 3;
                        return;
                    }

                    // Phase 1 complete — empty the restore container for cleanup
                    auto* emptyFunc = invComp->GetFunctionByNameInChain(STR("EmptyContainer"));
                    if (emptyFunc)
                    {
                        auto emptyItem = findParam(emptyFunc, L"Item");
                        if (emptyItem.offset >= 0)
                        {
                            std::vector<uint8_t> emptyBuf(std::max(emptyFunc->GetParmsSize() + 32, 64), 0);
                            std::memcpy(emptyBuf.data() + emptyItem.offset, restoreContainer.data(), handleSize);
                            invComp->ProcessEvent(emptyFunc, emptyBuf.data());
                            VLOG(STR("[MoriaCppMod] Phase1: EmptyContainer([{}]) Ã¢â‚¬â€ cleanup after restore\n"), m_swap.restoreIdx);
                        }
                    }

                    m_activeToolbar = m_swap.nextTB;
                    s_overlay.activeToolbar = m_swap.nextTB;
                    s_overlay.needsUpdate = true;

                    std::wstring msg = std::format(L"Toolbar {} active ({} items moved)", m_swap.nextTB + 1, m_swap.moved);
                    showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
                    VLOG(STR("[MoriaCppMod] {}\n"), msg);
                    m_swap.active = false;
                    refreshActionBar();
                    return;
                }
            }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] EXCEPTION in swapToolbarTick()\n"));
                m_swap.active = false;
            }
        }

        