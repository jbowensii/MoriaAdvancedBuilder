// +==============================================================================+
// |  moria_inventory.inl -- Toolbar swap v2: serialize + recreate               |
// |  Phase 2: JSON format, containers, tint preservation, file lock safety      |
// |  #include inside MoriaCppMod class body                                     |
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

        // ---- Toolbar Swap v3: multi-frame state machine -------------------------
        // Splits remove/add across frames so MovieScene can settle between them.
        // Prevents FEntityManager::FreeEntities crash from stale latent actions.
        static constexpr int TOOLBAR_SLOTS = 8;
        int m_activeToolbar{0};          // 0 or 1 -- for overlay T1/T2 display
        UObject* m_ihfCDO{nullptr};      // UItemHandleFunctions CDO
        ULONGLONG m_lastSwapTime{0};     // GetTickCount64() of last successful swap
        bool m_swapInProgress{false};    // blocks re-entrant swaps during retry loop

        // Multi-frame swap state machine
        enum class SwapPhase { Idle = 0, WaitAfterRemove, Add, WaitAfterAdd, Finish };
        SwapPhase m_swapPhase{SwapPhase::Idle};
        int m_swapFrameWait{0};
        static constexpr int SWAP_SETTLE_FRAMES = 5;  // frames to wait for MovieScene

        struct StashedContentItem {
            std::wstring className;
            int32_t count{0};
            float durability{0.0f};
        };

        // Generic item effect (tints, runes, etc.) from Effects.List
        struct StashedEffect {
            std::wstring typeName;   // e.g. "MorItemTintEffect", "MorRuneEffect"
            std::wstring assetName;  // e.g. "LapisLazuli", "Sharpness"
        };

        struct StashedItem {
            std::wstring className;      // UClass name e.g. "EQ_Pick_Iron_C"
            int32_t count{0};
            float durability{0.0f};
            std::wstring tintRowName;    // LEGACY — kept for JSON compat, prefer effects[]
            std::vector<StashedEffect> effects;       // all active effects on this item
            std::vector<StashedContentItem> contents;  // container contents
            bool isContainer{false};     // true = bag/container with nested contents
            bool valid{false};
            int32_t itemId{-1};          // transient — runtime item ID, not serialized
        };

        // Stashed items for multi-frame swap (persists between remove and add phases)
        std::array<StashedItem, TOOLBAR_SLOTS> m_swapStash{};

        // Persistent class cache: name -> UClass*, survives across swaps
        std::unordered_map<std::wstring, UObject*> m_classCache;

        // Persistent effect cache: "typeName:assetName" -> UObject*, populated from hotbar reads + global search
        std::unordered_map<std::wstring, UObject*> m_effectCache;

        std::wstring m_playerGuidStr;    // 32-char hex, set on character load
        bool m_toolbarFileLoaded{false};

        // Read PlayerGuid from MorPlayerController (FGuid at offset 0x09C8)
        void readPlayerGuid()
        {
            m_playerGuidStr.clear();
            auto* pc = findPlayerController();
            if (!pc) return;

            static int s_off_playerGuid = -2;
            if (s_off_playerGuid == -2) resolveOffset(pc, L"PlayerGuid", s_off_playerGuid);
            int guidOff = (s_off_playerGuid >= 0) ? s_off_playerGuid : 0x09C8;

            uint8_t* base = reinterpret_cast<uint8_t*>(pc);
            if (!isReadableMemory(base + guidOff, 16)) return;

            uint32_t parts[4];
            std::memcpy(parts, base + guidOff, 16);

            wchar_t buf[33];
            swprintf(buf, 33, L"%08X%08X%08X%08X", parts[0], parts[1], parts[2], parts[3]);
            m_playerGuidStr = buf;

            if (parts[0] == 0 && parts[1] == 0 && parts[2] == 0 && parts[3] == 0)
            {
                VLOG(STR("[MoriaCppMod] [Swap] PlayerGuid is all zeros -- invalid\n"));
                m_playerGuidStr.clear();
                return;
            }

            VLOG(STR("[MoriaCppMod] [Swap] PlayerGuid = {}\n"), m_playerGuidStr);
        }

        // ---- Helper: ensure IHF CDO and common functions --------------------
        bool ensureIHF()
        {
            if (!m_ihfCDO)
                m_ihfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__ItemHandleFunctions"));
            return m_ihfCDO != nullptr;
        }

        // ---- Helper: call IsValidItem on FItemHandle in buffer at offset ----
        bool callIsValidItem(const uint8_t* handleData, int handleSize)
        {
            if (!ensureIHF()) return false;
            static UFunction* s_fn = nullptr;
            static int s_itemOff = -1, s_retOff = -1;
            if (!s_fn) {
                s_fn = m_ihfCDO->GetFunctionByNameInChain(STR("IsValidItem"));
                if (!s_fn) return false;
                for (auto* p : s_fn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"Item") s_itemOff = p->GetOffset_Internal();
                    if (n == L"ReturnValue") s_retOff = p->GetOffset_Internal();
                }
            }
            if (s_itemOff < 0 || s_retOff < 0) return false;
            std::vector<uint8_t> buf(std::max(s_fn->GetParmsSize() + 32, 64), 0);
            std::memcpy(buf.data() + s_itemOff, handleData, handleSize);
            if (!safeProcessEvent(m_ihfCDO, s_fn, buf.data())) return false;
            return buf[s_retOff] != 0;
        }

        // ---- Helper: call GetStorageMaxSlots on FItemHandle -----------------
        int32_t callGetStorageMaxSlots(const uint8_t* handleData, int handleSize)
        {
            if (!ensureIHF()) return 0;
            static UFunction* s_fn = nullptr;
            static int s_itemOff = -1, s_retOff = -1;
            if (!s_fn) {
                s_fn = m_ihfCDO->GetFunctionByNameInChain(STR("GetStorageMaxSlots"));
                if (!s_fn) return 0;
                for (auto* p : s_fn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"Item") s_itemOff = p->GetOffset_Internal();
                    if (n == L"ReturnValue") s_retOff = p->GetOffset_Internal();
                }
            }
            if (s_itemOff < 0 || s_retOff < 0) return 0;
            std::vector<uint8_t> buf(std::max(s_fn->GetParmsSize() + 32, 64), 0);
            std::memcpy(buf.data() + s_itemOff, handleData, handleSize);
            if (!safeProcessEvent(m_ihfCDO, s_fn, buf.data())) return 0;
            return *reinterpret_cast<int32_t*>(buf.data() + s_retOff);
        }

        // ---- Helper: call GetItemForSlot(handle, slot) on IHF CDO ----------
        // Returns the FItemHandle bytes for the item in the container slot
        std::vector<uint8_t> callGetItemForSlot(const uint8_t* containerHandle, int handleSize, int32_t slot)
        {
            if (!ensureIHF()) return {};
            static UFunction* s_fn = nullptr;
            static int s_itemOff = -1, s_slotOff = -1, s_retOff = -1, s_retSize = 0;
            if (!s_fn) {
                s_fn = m_ihfCDO->GetFunctionByNameInChain(STR("GetItemForSlot"));
                if (!s_fn) return {};
                for (auto* p : s_fn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"Item") s_itemOff = p->GetOffset_Internal();
                    if (n == L"Slot") s_slotOff = p->GetOffset_Internal();
                    if (n == L"ReturnValue") { s_retOff = p->GetOffset_Internal(); s_retSize = p->GetSize(); }
                }
            }
            if (s_itemOff < 0 || s_slotOff < 0 || s_retOff < 0 || s_retSize <= 0) return {};
            std::vector<uint8_t> buf(std::max(s_fn->GetParmsSize() + 32, 64), 0);
            std::memcpy(buf.data() + s_itemOff, containerHandle, handleSize);
            *reinterpret_cast<int32_t*>(buf.data() + s_slotOff) = slot;
            if (!safeProcessEvent(m_ihfCDO, s_fn, buf.data())) return {};
            std::vector<uint8_t> result(s_retSize, 0);
            std::memcpy(result.data(), buf.data() + s_retOff, s_retSize);
            return result;
        }

        // ---- Helper: call GetTintEffect(handle) on MorInventoryItem CDO -----
        // Returns UMorItemTintEffect* (or nullptr)
        UObject* callGetTintEffect(const uint8_t* handleData, int handleSize)
        {
            static UObject* s_cdo = nullptr;
            static UFunction* s_fn = nullptr;
            static int s_itemOff = -1, s_retOff = -1;
            if (!s_cdo) {
                s_cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorInventoryItem"));
                if (!s_cdo) return nullptr;
            }
            if (!s_fn) {
                s_fn = s_cdo->GetFunctionByNameInChain(STR("GetTintEffect"));
                if (!s_fn) return nullptr;
                for (auto* p : s_fn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"Item") s_itemOff = p->GetOffset_Internal();
                    if (n == L"ReturnValue") s_retOff = p->GetOffset_Internal();
                }
            }
            if (s_itemOff < 0 || s_retOff < 0) return nullptr;
            std::vector<uint8_t> buf(std::max(s_fn->GetParmsSize() + 32, 64), 0);
            std::memcpy(buf.data() + s_itemOff, handleData, handleSize);
            if (!safeProcessEvent(s_cdo, s_fn, buf.data())) return nullptr;
            return *reinterpret_cast<UObject**>(buf.data() + s_retOff);
        }

        // ---- Helper: read tint row name from UMorItemTintEffect* -----------
        std::wstring readTintRowName(UObject* tintEffect)
        {
            if (!tintEffect) return {};
            static int s_off_rowHandle = -2;
            if (s_off_rowHandle == -2) resolveOffset(tintEffect, L"RowHandle", s_off_rowHandle);
            if (s_off_rowHandle < 0) return {};

            // FDataTableRowHandle = { UDataTable* DataTable; FName RowName; }
            // RowName is at offset +8 within RowHandle
            uint8_t* base = reinterpret_cast<uint8_t*>(tintEffect);
            int rowNameOff = s_off_rowHandle + 8; // FName follows the UDataTable*
            if (!isReadableMemory(base + rowNameOff, 8)) return {};

            // Read FName -- the comparison index is the first int32
            int32_t nameIndex = *reinterpret_cast<int32_t*>(base + rowNameOff);
            if (nameIndex <= 0) return {};

            // Use FName::ToString via UE4SS API
            try {
                RC::Unreal::FName fname;
                std::memcpy(&fname, base + rowNameOff, sizeof(RC::Unreal::FName));
                return std::wstring(fname.ToString());
            } catch (...) {
                return {};
            }
        }

        // ---- Helper: read ALL effects for a given item ID from Effects.List ---
        // Returns vector of {typeName, assetName} for each active effect on the item.
        // Also caches the UObject* Effect pointers in m_effectCache.
        std::vector<StashedEffect> readItemEffects(UObject* invComp, int32_t itemId)
        {
            std::vector<StashedEffect> result;
            if (!invComp || itemId <= 0) return result;

            static int s_off_effects_r = -2;
            if (s_off_effects_r == -2) resolveOffset(invComp, L"Effects", s_off_effects_r);
            if (s_off_effects_r < 0) return result;

            uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + s_off_effects_r;
            int listOff = iiaListOff();
            if (!isReadableMemory(effectsBase + listOff, 16)) return result;

            struct { uint8_t* Data; int32_t Num; int32_t Max; } effectsList{};
            std::memcpy(&effectsList, effectsBase + listOff, 16);
            if (!effectsList.Data || effectsList.Num <= 0 || effectsList.Num > 500) return result;

            // FActiveItemEffect layout: size=0x30, OnItem@0x0C, Effect@0x10, EffectPrimaryAssetId@0x1C
            constexpr int AE_SIZE = 0x30;
            if (!isReadableMemory(effectsList.Data, effectsList.Num * AE_SIZE)) return result;

            for (int i = 0; i < effectsList.Num; i++)
            {
                uint8_t* entry = effectsList.Data + i * AE_SIZE;
                int32_t onItem = *reinterpret_cast<int32_t*>(entry + 0x0C);
                if (onItem != itemId) continue;

                UObject* effectObj = *reinterpret_cast<UObject**>(entry + 0x10);
                if (!effectObj || !isReadableMemory(effectObj, 64)) continue;

                // Read type from the Effect UObject's class name, and row name from RowHandle
                try {
                    std::wstring typeStr = safeClassName(effectObj);
                    std::wstring assetStr = readTintRowName(effectObj); // RowHandle at 0x60 — same layout for tints & runes

                    if (typeStr.empty() || assetStr.empty()) continue;
                    // Only capture MorItemTintEffect and MorRuneEffect (skip base/irrelevant effects)
                    if (typeStr.find(L"Tint") == std::wstring::npos &&
                        typeStr.find(L"Rune") == std::wstring::npos) continue;

                    StashedEffect eff;
                    eff.typeName = typeStr;
                    eff.assetName = assetStr;
                    result.push_back(eff);

                    // Cache the effect UObject for restoration
                    std::wstring key = typeStr + L":" + assetStr;
                    m_effectCache[key] = effectObj;
                } catch (...) {}
            }
            return result;
        }

        // ---- Read all 8 hotbar slots into StashedItem array -----------------
        std::array<StashedItem, 8> readHotbarItems()
        {
            std::array<StashedItem, 8> result{};
            UObject* pawn = getPawn();
            auto* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp) return result;

            probeItemInstanceStruct(invComp);
            if (!ensureIHF()) return result;

            // Resolve GetItemForHotbarSlot
            auto* getSlotFn = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
            if (!getSlotFn) return result;

            int gsIndexOff = -1, gsRetOff = -1, handleSize = 0;
            for (auto* p : getSlotFn->ForEachProperty()) {
                std::wstring n(p->GetName());
                if (n == L"HotbarIndex") gsIndexOff = p->GetOffset_Internal();
                if (n == L"ReturnValue") { gsRetOff = p->GetOffset_Internal(); handleSize = p->GetSize(); }
            }
            if (gsRetOff < 0 || handleSize <= 0) return result;

            // Build ID->info map from Items.List
            static int s_off_invItems = -2;
            if (s_off_invItems == -2) resolveOffset(invComp, L"Items", s_off_invItems);
            int itemsListOffset = (s_off_invItems >= 0) ? s_off_invItems + iiaListOff() : 0x0330;

            uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
            struct { uint8_t* Data; int32_t Num; int32_t Max; } itemsList{};
            if (isReadableMemory(invBase + itemsListOffset, 16))
                std::memcpy(&itemsList, invBase + itemsListOffset, 16);

            struct ItemInfo { UObject* cls{nullptr}; std::wstring name; int32_t count{0}; float durability{0.0f}; };
            std::unordered_map<int32_t, ItemInfo> idMap;
            if (itemsList.Data && itemsList.Num > 0 && itemsList.Num < 500 && isReadableMemory(itemsList.Data, itemsList.Num * iiSize()))
            {
                for (int i = 0; i < itemsList.Num; i++)
                {
                    uint8_t* entry = itemsList.Data + i * iiSize();
                    int32_t id = *reinterpret_cast<int32_t*>(entry + iiIDOff());
                    UObject* cls = *reinterpret_cast<UObject**>(entry + iiItemOff());
                    if (!cls || !isReadableMemory(cls, 64)) continue;
                    try {
                        ItemInfo info;
                        info.cls = cls;
                        info.name = std::wstring(cls->GetName());
                        info.count = *reinterpret_cast<int32_t*>(entry + 0x18);
                        info.durability = *reinterpret_cast<float*>(entry + iiDurOff());
                        idMap[id] = std::move(info);
                    } catch (...) {}
                }
            }

            // Populate persistent class cache from ALL inventory items
            for (auto& [id, info] : idMap)
                m_classCache[info.name] = info.cls;

            // Read each hotbar slot
            for (int slot = 0; slot < TOOLBAR_SLOTS; slot++)
            {
                std::vector<uint8_t> gsBuf(std::max(getSlotFn->GetParmsSize() + 32, 64), 0);
                *reinterpret_cast<int32_t*>(gsBuf.data() + gsIndexOff) = slot;
                invComp->ProcessEvent(getSlotFn, gsBuf.data());

                // Check validity
                if (!callIsValidItem(gsBuf.data() + gsRetOff, handleSize)) continue;

                int32_t itemId = *reinterpret_cast<int32_t*>(gsBuf.data() + gsRetOff);
                auto it = idMap.find(itemId);
                if (it == idMap.end()) continue;

                result[slot].className = it->second.name;
                result[slot].count = it->second.count;
                result[slot].durability = it->second.durability;
                result[slot].itemId = itemId;
                result[slot].valid = true;

                // Read ALL effects for this item from Effects.List
                result[slot].effects = readItemEffects(invComp, itemId);
                for (auto& eff : result[slot].effects)
                {
                    VLOG(STR("[MoriaCppMod] [Swap] Slot {} has effect: {}:{}\n"), slot, eff.typeName, eff.assetName);
                    // Cache effect UObject for restoration
                    std::wstring key = eff.typeName + L":" + eff.assetName;
                    // (pointer cached inside readItemEffects)
                }
                // Backwards compat: populate tintRowName from effects
                for (auto& eff : result[slot].effects)
                    if (eff.typeName.find(L"Tint") != std::wstring::npos)
                        { result[slot].tintRowName = eff.assetName; break; }

                // Check for container contents
                int32_t maxSlots = callGetStorageMaxSlots(gsBuf.data() + gsRetOff, handleSize);
                if (maxSlots > 0)
                {
                    result[slot].isContainer = true;
                    VLOG(STR("[MoriaCppMod] [Swap] Slot {} is container with {} max slots\n"), slot, maxSlots);
                    for (int32_t cs = 0; cs < maxSlots; cs++)
                    {
                        auto contentHandle = callGetItemForSlot(gsBuf.data() + gsRetOff, handleSize, cs);
                        if (contentHandle.empty()) continue;
                        if (!callIsValidItem(contentHandle.data(), static_cast<int>(contentHandle.size()))) continue;

                        int32_t contentId = *reinterpret_cast<int32_t*>(contentHandle.data());
                        auto cit = idMap.find(contentId);
                        if (cit == idMap.end()) continue;

                        StashedContentItem ci;
                        ci.className = cit->second.name;
                        ci.count = cit->second.count;
                        ci.durability = cit->second.durability;
                        result[slot].contents.push_back(std::move(ci));
                    }
                    VLOG(STR("[MoriaCppMod] [Swap] Container slot {} has {} items inside\n"), slot, result[slot].contents.size());
                }
            }
            return result;
        }

        // ---- JSON file path for stash file ----------------------------------
        std::string stashFilePath()
        {
            std::string narrow;
            for (wchar_t c : m_playerGuidStr) narrow.push_back(static_cast<char>(c));
            return "Mods/MoriaCppMod/toolbar_" + narrow + ".json";
        }

        // ---- Helper: narrow wstring to string (ASCII-safe for class names) --
        static std::string toNarrow(const std::wstring& w)
        {
            std::string s;
            s.reserve(w.size());
            for (wchar_t c : w) s.push_back(static_cast<char>(c));
            return s;
        }

        // ---- Helper: widen string to wstring --------------------------------
        static std::wstring toWide(const std::string& s)
        {
            return std::wstring(s.begin(), s.end());
        }

        // ---- Helper: escape a string for JSON output (just backslash and quote) --
        static std::string jsonEscape(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else out += c;
            }
            return out;
        }

        // ---- Save items to stash file (JSON format) -------------------------
        void saveStashFile(const std::array<StashedItem, 8>& items)
        {
            if (m_playerGuidStr.empty()) return;
            std::string path = stashFilePath();

            // Build entire JSON in memory first, then single write
            std::string json;
            json.reserve(4096);
            json += "{\n  \"slots\": [\n";
            for (int i = 0; i < TOOLBAR_SLOTS; i++)
            {
                json += "    {\n";
                json += "      \"slot\": " + std::to_string(i) + ",\n";
                json += "      \"className\": \"" + jsonEscape(toNarrow(items[i].className)) + "\",\n";
                json += "      \"count\": " + std::to_string(items[i].count) + ",\n";
                char durBuf[32]; snprintf(durBuf, sizeof(durBuf), "%.1f", items[i].durability);
                json += "      \"durability\": " + std::string(durBuf) + ",\n";
                json += std::string("      \"isContainer\": ") + (items[i].isContainer ? "true" : "false") + ",\n";
                json += "      \"tintRowName\": \"" + jsonEscape(toNarrow(items[i].tintRowName)) + "\",\n";
                json += "      \"effects\": [";
                if (items[i].effects.empty()) {
                    json += "],\n";
                } else {
                    json += "\n";
                    for (size_t ei = 0; ei < items[i].effects.size(); ei++) {
                        auto& e = items[i].effects[ei];
                        json += "        { \"type\": \"" + jsonEscape(toNarrow(e.typeName))
                             + "\", \"name\": \"" + jsonEscape(toNarrow(e.assetName)) + "\" }";
                        if (ei + 1 < items[i].effects.size()) json += ",";
                        json += "\n";
                    }
                    json += "      ],\n";
                }
                json += "      \"contents\": [";
                if (items[i].contents.empty()) {
                    json += "]";
                } else {
                    json += "\n";
                    for (size_t ci = 0; ci < items[i].contents.size(); ci++) {
                        auto& c = items[i].contents[ci];
                        char cdBuf[32]; snprintf(cdBuf, sizeof(cdBuf), "%.1f", c.durability);
                        json += "        { \"className\": \"" + jsonEscape(toNarrow(c.className))
                             + "\", \"count\": " + std::to_string(c.count)
                             + ", \"durability\": " + std::string(cdBuf) + " }";
                        if (ci + 1 < items[i].contents.size()) json += ",";
                        json += "\n";
                    }
                    json += "      ]";
                }
                json += "\n    }";
                if (i + 1 < TOOLBAR_SLOTS) json += ",";
                json += "\n";
            }
            json += "  ]\n}\n";

            // Single write — open, write, close
            {
                std::ofstream file(path, std::ios::trunc | std::ios::binary);
                if (!file.is_open()) {
                    VLOG(STR("[MoriaCppMod] [Swap] Failed to write stash file\n"));
                    return;
                }
                file.write(json.data(), json.size());
            } // file closed here

            VLOG(STR("[MoriaCppMod] [Swap] Saved stash file: {}\n"), toWide(path));
        }

        // ---- Simple JSON token helper for parsing ---------------------------
        // Extracts the value for a given key from a JSON-like string (flat, no nesting)
        static std::string jsonValueFor(const std::string& json, const std::string& key)
        {
            std::string searchKey = "\"" + key + "\"";
            size_t pos = json.find(searchKey);
            if (pos == std::string::npos) return "";
            pos = json.find(':', pos + searchKey.size());
            if (pos == std::string::npos) return "";
            pos++; // skip ':'
            while (pos < json.size() && json[pos] == ' ') pos++;
            if (pos >= json.size()) return "";

            if (json[pos] == '"') {
                // String value
                size_t end = json.find('"', pos + 1);
                if (end == std::string::npos) return "";
                return json.substr(pos + 1, end - pos - 1);
            } else if (json[pos] == '[') {
                // Array -- return the whole [...] including brackets
                int depth = 0;
                size_t start = pos;
                for (size_t i = pos; i < json.size(); i++) {
                    if (json[i] == '[') depth++;
                    else if (json[i] == ']') { depth--; if (depth == 0) return json.substr(start, i - start + 1); }
                }
                return "";
            } else {
                // Number/bool
                size_t end = json.find_first_of(",}\n", pos);
                if (end == std::string::npos) end = json.size();
                std::string val = json.substr(pos, end - pos);
                // Trim whitespace
                while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();
                return val;
            }
        }

        // ---- Parse content item from JSON fragment --------------------------
        StashedContentItem parseContentItem(const std::string& json)
        {
            StashedContentItem ci;
            ci.className = toWide(jsonValueFor(json, "className"));
            std::string countStr = jsonValueFor(json, "count");
            std::string durStr = jsonValueFor(json, "durability");
            ci.count = countStr.empty() ? 0 : std::atoi(countStr.c_str());
            ci.durability = durStr.empty() ? 0.0f : static_cast<float>(std::atof(durStr.c_str()));
            return ci;
        }

        // ---- Parse contents array from JSON string --------------------------
        std::vector<StashedContentItem> parseContentsArray(const std::string& arrayStr)
        {
            std::vector<StashedContentItem> result;
            // Find each { ... } object in the array
            size_t pos = 0;
            while (pos < arrayStr.size()) {
                size_t start = arrayStr.find('{', pos);
                if (start == std::string::npos) break;
                size_t end = arrayStr.find('}', start);
                if (end == std::string::npos) break;
                std::string obj = arrayStr.substr(start, end - start + 1);
                auto ci = parseContentItem(obj);
                if (!ci.className.empty())
                    result.push_back(std::move(ci));
                pos = end + 1;
            }
            return result;
        }

        // ---- Load items from stash file (JSON or legacy pipe-delimited) -----
        std::array<StashedItem, 8> loadStashFile()
        {
            std::array<StashedItem, 8> result{};
            if (m_playerGuidStr.empty()) return result;

            // Try JSON first
            std::string jsonPath = stashFilePath();
            std::ifstream jfile(jsonPath);
            if (jfile.is_open())
            {
                std::string content((std::istreambuf_iterator<char>(jfile)), std::istreambuf_iterator<char>());
                jfile.close();

                if (content.find("\"slots\"") != std::string::npos)
                {
                    // Parse each slot object
                    size_t pos = 0;
                    while (pos < content.size())
                    {
                        // Find slot objects by looking for "slot":
                        size_t slotKeyPos = content.find("\"slot\"", pos);
                        if (slotKeyPos == std::string::npos) break;

                        // Find the enclosing { ... } for this slot
                        // Search backward for '{'
                        size_t objStart = content.rfind('{', slotKeyPos);
                        if (objStart == std::string::npos) break;

                        // Find the matching '}' accounting for nested { } in contents
                        int depth = 0;
                        size_t objEnd = objStart;
                        for (size_t i = objStart; i < content.size(); i++) {
                            if (content[i] == '{') depth++;
                            else if (content[i] == '}') {
                                depth--;
                                if (depth == 0) { objEnd = i; break; }
                            }
                        }
                        std::string obj = content.substr(objStart, objEnd - objStart + 1);

                        std::string slotStr = jsonValueFor(obj, "slot");
                        int slot = slotStr.empty() ? -1 : std::atoi(slotStr.c_str());
                        if (slot >= 0 && slot < TOOLBAR_SLOTS)
                        {
                            std::string cls = jsonValueFor(obj, "className");
                            if (!cls.empty())
                            {
                                result[slot].className = toWide(cls);
                                std::string countStr = jsonValueFor(obj, "count");
                                std::string durStr = jsonValueFor(obj, "durability");
                                result[slot].count = countStr.empty() ? 0 : std::atoi(countStr.c_str());
                                result[slot].durability = durStr.empty() ? 0.0f : static_cast<float>(std::atof(durStr.c_str()));
                                result[slot].isContainer = (jsonValueFor(obj, "isContainer") == "true");
                                result[slot].tintRowName = toWide(jsonValueFor(obj, "tintRowName"));
                                // Parse effects array
                                std::string effectsArr = jsonValueFor(obj, "effects");
                                if (!effectsArr.empty() && effectsArr != "[]")
                                {
                                    size_t epos = 0;
                                    while (epos < effectsArr.size()) {
                                        size_t es = effectsArr.find('{', epos);
                                        if (es == std::string::npos) break;
                                        size_t ee = effectsArr.find('}', es);
                                        if (ee == std::string::npos) break;
                                        std::string eobj = effectsArr.substr(es, ee - es + 1);
                                        StashedEffect se;
                                        se.typeName = toWide(jsonValueFor(eobj, "type"));
                                        se.assetName = toWide(jsonValueFor(eobj, "name"));
                                        if (!se.typeName.empty() && !se.assetName.empty())
                                            result[slot].effects.push_back(std::move(se));
                                        epos = ee + 1;
                                    }
                                }
                                // Legacy: if no effects but tintRowName present, create a tint effect entry
                                if (result[slot].effects.empty() && !result[slot].tintRowName.empty())
                                {
                                    StashedEffect se;
                                    se.typeName = L"MorItemTintEffect";
                                    se.assetName = result[slot].tintRowName;
                                    result[slot].effects.push_back(std::move(se));
                                }
                                std::string contentsArr = jsonValueFor(obj, "contents");
                                if (!contentsArr.empty())
                                    result[slot].contents = parseContentsArray(contentsArr);
                                result[slot].valid = true;
                            }
                        }
                        pos = objEnd + 1;
                    }
                    return result;
                }
            }

            // Legacy: try pipe-delimited .txt file
            std::string narrow;
            for (wchar_t c : m_playerGuidStr) narrow.push_back(static_cast<char>(c));
            std::string txtPath = "Mods/MoriaCppMod/toolbar_" + narrow + ".txt";
            std::ifstream tfile(txtPath);
            if (!tfile.is_open()) return result;
            std::string line;
            while (std::getline(tfile, line))
            {
                if (line.empty() || line[0] == '#') continue;
                size_t p1 = line.find('|');
                if (p1 == std::string::npos) continue;
                size_t p2 = line.find('|', p1 + 1);
                if (p2 == std::string::npos) continue;
                size_t p3 = line.find('|', p2 + 1);
                if (p3 == std::string::npos) continue;

                int slot = std::atoi(line.substr(0, p1).c_str());
                if (slot < 0 || slot >= TOOLBAR_SLOTS) continue;

                std::string clsNarrow = line.substr(p1 + 1, p2 - p1 - 1);
                int count = std::atoi(line.substr(p2 + 1, p3 - p2 - 1).c_str());
                float dur = static_cast<float>(std::atof(line.substr(p3 + 1).c_str()));

                result[slot].className = toWide(clsNarrow);
                result[slot].count = count;
                result[slot].durability = dur;
                result[slot].valid = true;
            }
            VLOG(STR("[MoriaCppMod] [Swap] Loaded legacy .txt stash file -- will migrate to .json on next save\n"));
            return result;
        }

        // ---- Resolve UClass* by name -- cache, then instance search, then UClass lookup
        UObject* resolveItemClass(const std::wstring& className)
        {
            auto it = m_classCache.find(className);
            if (it != m_classCache.end() && it->second) return it->second;

            VLOG(STR("[MoriaCppMod] [Swap] resolveItemClass: '{}' not in cache, searching...\n"), className);

            // Strategy 1: Search live InventoryItem instances for the class
            std::vector<UObject*> allItems;
            UObjectGlobals::FindAllOf(STR("InventoryItem"), allItems);
            for (auto* obj : allItems)
            {
                if (!obj) continue;
                try {
                    auto* cls = obj->GetClassPrivate();
                    if (cls && std::wstring(cls->GetName()) == className)
                    {
                        m_classCache[className] = cls;
                        VLOG(STR("[MoriaCppMod] [Swap] Found '{}' via instance search\n"), className);
                        return cls;
                    }
                } catch (...) {}
            }

            // Strategy 2: Find UClass directly by name (works even with no instances)
            // Blueprint classes have UClass objects in memory even when no instances exist
            try {
                auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, className);
                if (cls) {
                    m_classCache[className] = cls;
                    VLOG(STR("[MoriaCppMod] [Swap] Found '{}' via StaticFindObject<UClass*>\n"), className);
                    return cls;
                }
            } catch (...) {}

            // Strategy 3: Search all UClass objects by name
            try {
                std::vector<UObject*> allClasses;
                UObjectGlobals::FindAllOf(STR("Class"), allClasses);
                for (auto* obj : allClasses)
                {
                    if (!obj) continue;
                    try {
                        if (std::wstring(obj->GetName()) == className)
                        {
                            m_classCache[className] = obj;
                            VLOG(STR("[MoriaCppMod] [Swap] Found '{}' via Class enumeration\n"), className);
                            return obj;
                        }
                    } catch (...) {}
                }
            } catch (...) {}

            // Strategy 4: Search item DataTables for the Actor TSoftClassPtr path
            // Each row has an "Actor" field (TSoftClassPtr<AMorItemBase>, 0x28 bytes).
            // Inside TSoftClassPtr: FWeakObjectPtr(8) + int32 Tag(4) + pad(4) + FSoftObjectPath
            // FSoftObjectPath starts with FName AssetPathName (8 bytes) = full asset path
            // e.g. "/Game/Items/Tools/BP_Adamant_Hammer.Adamant_Hammer_C"
            // We extract the class name after the last '.' and match against our target.
            {
                constexpr int SOFTPTR_ASSETPATH_OFFSET = 0x10; // UE4.27 TPersistentObjectPtr layout
                DataTableUtil* tables[] = {
                    &m_dtItems, &m_dtWeapons, &m_dtTools, &m_dtArmor,
                    &m_dtConsumables, &m_dtContainerItems, &m_dtOres
                };
                for (auto* dt : tables)
                {
                    if (!dt->isBound()) continue;
                    int actorOff = dt->resolvePropertyOffset(L"Actor");
                    if (actorOff < 0) continue;

                    auto rowNames = dt->getRowNames();
                    for (auto& rn : rowNames)
                    {
                        uint8_t* rowData = dt->findRowData(rn.c_str());
                        if (!rowData) continue;
                        if (!isReadableMemory(rowData + actorOff + SOFTPTR_ASSETPATH_OFFSET, 8)) continue;

                        FName assetPath;
                        std::memcpy(&assetPath, rowData + actorOff + SOFTPTR_ASSETPATH_OFFSET, 8);
                        std::wstring pathStr;
                        try { pathStr = assetPath.ToString(); } catch (...) { continue; }
                        if (pathStr.empty()) continue;

                        // Extract class name after last '.'
                        auto dotPos = pathStr.rfind(L'.');
                        if (dotPos == std::wstring::npos) continue;
                        std::wstring extracted = pathStr.substr(dotPos + 1);
                        if (extracted != className) continue;

                        // Match found — use full path with StaticFindObject
                        try {
                            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, pathStr);
                            if (cls)
                            {
                                m_classCache[className] = cls;
                                VLOG(STR("[MoriaCppMod] [Swap] Found '{}' via DataTable '{}' row '{}' path '{}'\n"),
                                     className, dt->tableName, rn, pathStr);
                                return cls;
                            }
                        } catch (...) {}

                        VLOG(STR("[MoriaCppMod] [Swap] DataTable match for '{}' in '{}' row '{}' but StaticFindObject failed (path '{}')\n"),
                             className, dt->tableName, rn, pathStr);
                    }
                }
            }

            VLOG(STR("[MoriaCppMod] [Swap] FAIL: '{}' not found anywhere\n"), className);
            return nullptr;
        }

        // ---- Helper: find CraftingComponent on pawn -------------------------
        UObject* findCraftingComponent(UObject* pawn)
        {
            if (!pawn) return nullptr;
            static int s_off_crafting = -2;
            if (s_off_crafting == -2) resolveOffset(pawn, L"Crafting", s_off_crafting);
            if (s_off_crafting < 0) return nullptr;
            uint8_t* base = reinterpret_cast<uint8_t*>(pawn);
            if (!isReadableMemory(base + s_off_crafting, 8)) return nullptr;
            return *reinterpret_cast<UObject**>(base + s_off_crafting);
        }

        // ---- Helper: find effect UObject by type name via global search -------
        UObject* findEffectGlobal(const std::wstring& typeName, const std::wstring& assetName)
        {
            if (typeName.empty() || assetName.empty()) return nullptr;

            // Check cache first
            std::wstring key = typeName + L":" + assetName;
            auto cit = m_effectCache.find(key);
            if (cit != m_effectCache.end() && cit->second && isReadableMemory(cit->second, 64))
                return cit->second;

            // Search all UObjects of this type
            try {
                std::vector<UObject*> allEffects;
                UObjectGlobals::FindAllOf(typeName, allEffects);
                for (auto* obj : allEffects)
                {
                    if (!obj || !isReadableMemory(obj, 64)) continue;
                    try {
                        std::wstring name = readTintRowName(obj); // RowHandle at 0x60 — same layout for tints and runes
                        if (name == assetName) {
                            m_effectCache[key] = obj;
                            VLOG(STR("[MoriaCppMod] [Swap] Found effect '{}:{}' via global search\n"), typeName, assetName);
                            return obj;
                        }
                    } catch (...) {}
                }
            } catch (...) {}
            return nullptr;
        }

        // ---- Helper: restore an effect by directly inserting into Effects.List ---
        // Generic: works for tints (MorItemTintEffect), runes (MorRuneEffect), etc.
        // FActiveItemEffect layout (0x30):
        //   0x00: FFastArraySerializerItem base (0x0C) -- replication metadata, zero for new
        //   0x0C: int32 OnItem -- item ID
        //   0x10: UItemEffect* Effect -- UObject pointer
        //   0x18: float EndTime -- 0 for permanent
        //   0x1C: FPrimaryAssetId (0x10) = { FName Type, FName Name }
        void restoreEffectDirect(UObject* invComp, int32_t newItemId, const StashedEffect& eff)
        {
            if (!invComp || newItemId <= 0 || eff.typeName.empty() || eff.assetName.empty()) return;

            // Find a valid effect UObject (from cache or global search)
            UObject* effectObj = findEffectGlobal(eff.typeName, eff.assetName);
            if (!effectObj) {
                VLOG(STR("[MoriaCppMod] [Swap] restoreEffectDirect: no UObject found for '{}:{}'\n"), eff.typeName, eff.assetName);
                return;
            }

            // Read Effects.List TArray on InventoryComponent
            static int s_off_effects2 = -2;
            if (s_off_effects2 == -2) resolveOffset(invComp, L"Effects", s_off_effects2);
            if (s_off_effects2 < 0) {
                VLOG(STR("[MoriaCppMod] [Swap] restoreEffectDirect: Effects offset not resolved\n"));
                return;
            }

            uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + s_off_effects2;
            int listOff = iiaListOff();

            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };
            auto* arr = reinterpret_cast<TArrayHeader*>(effectsBase + listOff);
            if (!arr->Data || !isReadableMemory(arr->Data, 16)) {
                VLOG(STR("[MoriaCppMod] [Swap] restoreEffectDirect: Effects.List data invalid\n"));
                return;
            }
            if (arr->Num >= arr->Max) {
                VLOG(STR("[MoriaCppMod] [Swap] restoreEffectDirect: Effects.List full ({}/{})\n"), arr->Num, arr->Max);
                return;
            }

            // Build the new FActiveItemEffect entry (0x30 bytes)
            constexpr int AE_SIZE = 0x30;
            uint8_t newEntry[AE_SIZE] = {};

            *reinterpret_cast<int32_t*>(newEntry + 0x0C) = newItemId;
            *reinterpret_cast<UObject**>(newEntry + 0x10) = effectObj;
            *reinterpret_cast<float*>(newEntry + 0x18) = 0.0f; // permanent

            // EffectPrimaryAssetId = { FName(typeName), FName(assetName) }
            RC::Unreal::FName typeFName(eff.typeName.c_str(), FNAME_Add);
            RC::Unreal::FName assetFName(eff.assetName.c_str(), FNAME_Add);
            std::memcpy(newEntry + 0x1C, &typeFName, 8);
            std::memcpy(newEntry + 0x24, &assetFName, 8);

            // Append to TArray
            std::memcpy(arr->Data + arr->Num * AE_SIZE, newEntry, AE_SIZE);
            arr->Num++;

            VLOG(STR("[MoriaCppMod] [Swap] Restored effect '{}:{}' for item id={} (Num now {})\n"),
                 eff.typeName, eff.assetName, newItemId, arr->Num);
        }

        // ---- Purge orphaned effect entries from Effects.List ------------------
        // After RemoveItem, the game may leave stale Effect UObject* pointers
        // in Effects.List for items that no longer exist. CanAutoConsumeItem
        // iterates this list during AddItem and crashes on stale pointers.
        void purgeOrphanedEffects(UObject* invComp)
        {
            if (!invComp) return;

            static int s_off_effectsPurge = -2;
            if (s_off_effectsPurge == -2) resolveOffset(invComp, L"Effects", s_off_effectsPurge);
            if (s_off_effectsPurge < 0) return;

            // Build set of valid item IDs from Items.List
            static int s_off_invItemsPurge = -2;
            if (s_off_invItemsPurge == -2) resolveOffset(invComp, L"Items", s_off_invItemsPurge);
            int itemsListOff = (s_off_invItemsPurge >= 0) ? s_off_invItemsPurge + iiaListOff() : 0x0330;

            uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };
            TArrayHeader itemsList{};
            if (isReadableMemory(invBase + itemsListOff, 16))
                std::memcpy(&itemsList, invBase + itemsListOff, 16);

            std::unordered_set<int32_t> validIds;
            if (itemsList.Data && itemsList.Num > 0 && itemsList.Num < 500 &&
                isReadableMemory(itemsList.Data, itemsList.Num * iiSize()))
            {
                for (int i = 0; i < itemsList.Num; i++)
                {
                    int32_t id = *reinterpret_cast<int32_t*>(itemsList.Data + i * iiSize() + iiIDOff());
                    if (id > 0) validIds.insert(id);
                }
            }

            // Compact Effects.List, removing entries with orphaned item IDs
            uint8_t* effectsBase = invBase + s_off_effectsPurge;
            auto* arr = reinterpret_cast<TArrayHeader*>(effectsBase + iiaListOff());
            if (!arr->Data || arr->Num <= 0 || !isReadableMemory(arr->Data, arr->Num * 0x30)) return;

            constexpr int AE_SIZE = 0x30;
            int writeIdx = 0;
            int removed = 0;
            for (int i = 0; i < arr->Num; i++)
            {
                uint8_t* entry = arr->Data + i * AE_SIZE;
                int32_t onItem = *reinterpret_cast<int32_t*>(entry + 0x0C);
                if (onItem > 0 && validIds.find(onItem) == validIds.end()) {
                    removed++;
                    continue;
                }
                if (writeIdx != i)
                    std::memmove(arr->Data + writeIdx * AE_SIZE, entry, AE_SIZE);
                writeIdx++;
            }
            arr->Num = writeIdx;
            if (removed > 0)
                VLOG(STR("[MoriaCppMod] [Swap] Purged {} orphaned effect entries from Effects.List\n"), removed);
        }

        // ---- Remove effect entries for a specific item ID --------------------
        void removeEffectsForItem(UObject* invComp, int32_t itemId)
        {
            if (!invComp || itemId <= 0) return;

            static int s_off_effectsRm = -2;
            if (s_off_effectsRm == -2) resolveOffset(invComp, L"Effects", s_off_effectsRm);
            if (s_off_effectsRm < 0) return;

            uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + s_off_effectsRm;
            struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };
            auto* arr = reinterpret_cast<TArrayHeader*>(effectsBase + iiaListOff());
            if (!arr->Data || arr->Num <= 0 || !isReadableMemory(arr->Data, arr->Num * 0x30)) return;

            constexpr int AE_SIZE = 0x30;
            int writeIdx = 0;
            int removed = 0;
            for (int i = 0; i < arr->Num; i++)
            {
                uint8_t* entry = arr->Data + i * AE_SIZE;
                int32_t onItem = *reinterpret_cast<int32_t*>(entry + 0x0C);
                if (onItem == itemId) { removed++; continue; }
                if (writeIdx != i)
                    std::memmove(arr->Data + writeIdx * AE_SIZE, entry, AE_SIZE);
                writeIdx++;
            }
            arr->Num = writeIdx;
            if (removed > 0)
                VLOG(STR("[MoriaCppMod] [Swap] Removed {} effect entries for itemId={}\n"), removed, itemId);
        }

        // ---- Helper: find tint effect object by row name --------------------
        // Searches the Effects.List on InventoryComponent for a matching tint
        UObject* findTintEffectByRowName(UObject* invComp, const std::wstring& rowName)
        {
            if (!invComp || rowName.empty()) return nullptr;

            // Resolve Effects property offset
            static int s_off_effects = -2;
            if (s_off_effects == -2) resolveOffset(invComp, L"Effects", s_off_effects);
            if (s_off_effects < 0) return nullptr;

            // FActiveItemEffectArray has List TArray at the same relative offset as FItemInstanceArray
            // FActiveItemEffectArray::List is at +0x0110 (same FFastArraySerializer layout)
            uint8_t* effectsBase = reinterpret_cast<uint8_t*>(invComp) + s_off_effects;
            int listOff = iiaListOff(); // Same offset for List within FFastArraySerializer-derived structs
            if (!isReadableMemory(effectsBase + listOff, 16)) return nullptr;

            struct { uint8_t* Data; int32_t Num; int32_t Max; } effectsList{};
            std::memcpy(&effectsList, effectsBase + listOff, 16);

            if (!effectsList.Data || effectsList.Num <= 0 || effectsList.Num > 500) return nullptr;

            // FActiveItemEffect size = 0x30, Effect UObject* at offset 0x10
            constexpr int AE_SIZE = 0x30;
            constexpr int AE_EFFECT_OFF = 0x10;

            if (!isReadableMemory(effectsList.Data, effectsList.Num * AE_SIZE)) return nullptr;

            for (int i = 0; i < effectsList.Num; i++)
            {
                uint8_t* entry = effectsList.Data + i * AE_SIZE;
                UObject* effect = *reinterpret_cast<UObject**>(entry + AE_EFFECT_OFF);
                if (!effect || !isReadableMemory(effect, 64)) continue;

                try {
                    std::wstring cls = safeClassName(effect);
                    if (cls.find(L"MorItemTintEffect") == std::wstring::npos) continue;

                    std::wstring name = readTintRowName(effect);
                    if (name == rowName)
                        return effect;
                } catch (...) {}
            }
            return nullptr;
        }

        // ---- Helper: find tint effect by searching ALL UMorItemTintEffect objects ----
        // Cross-session fallback: tint stations in the world hold template tint effects
        UObject* findTintEffectGlobal(const std::wstring& rowName)
        {
            if (rowName.empty()) return nullptr;
            try {
                std::vector<UObject*> allEffects;
                UObjectGlobals::FindAllOf(STR("MorItemTintEffect"), allEffects);
                for (auto* obj : allEffects)
                {
                    if (!obj || !isReadableMemory(obj, 64)) continue;
                    try {
                        std::wstring name = readTintRowName(obj);
                        if (name == rowName)
                        {
                            VLOG(STR("[MoriaCppMod] [Swap] Found tint '{}' via global MorItemTintEffect search\n"), rowName);
                            return obj;
                        }
                    } catch (...) {}
                }
            } catch (...) {}
            return nullptr;
        }

        // Stop all action bar animations (parent + 12 children + focus widget).
        // Must be called before AND after inventory mutations to prevent MovieScene crashes.
        // RemoveItem/AddItem trigger ItemChanged → EquipOutro/Intro animations on stale widget state.
        void stopActionBarAnimations()
        {
            UObject* actionBar = findWidgetByClass(L"WBP_UI_ActionBar_C", true);
            if (!actionBar || !isWidgetAlive(actionBar)) return;
            auto* stopFn = actionBar->GetFunctionByNameInChain(STR("StopAllAnimations"));
            if (!stopFn) return;

            // Parent (16 animations)
            safeProcessEvent(actionBar, stopFn, nullptr);

            int childStopped = 0;
            // AllActionBarItems TArray — 12 child widgets x 3 animations each
            static int s_off_allItems = -2;
            if (s_off_allItems == -2) resolveOffset(actionBar, L"AllActionBarItems", s_off_allItems);
            if (s_off_allItems >= 0) {
                uint8_t* base = reinterpret_cast<uint8_t*>(actionBar) + s_off_allItems;
                struct { UObject** Data; int32_t Num; int32_t Max; } arr{};
                std::memcpy(&arr, base, sizeof(arr));
                if (arr.Data && arr.Num > 0 && isReadableMemory(arr.Data, arr.Num * 8)) {
                    for (int i = 0; i < arr.Num; i++) {
                        if (arr.Data[i] && isWidgetAlive(arr.Data[i])) {
                            auto* childStop = arr.Data[i]->GetFunctionByNameInChain(STR("StopAllAnimations"));
                            if (childStop) { safeProcessEvent(arr.Data[i], childStop, nullptr); childStopped++; }
                        }
                    }
                }
            }

            // Focus widget (2 animations: HideFocus/ShowFocus)
            static int s_off_focus = -2;
            if (s_off_focus == -2) resolveOffset(actionBar, L"UI_WBP_ActionBar_Focus", s_off_focus);
            if (s_off_focus >= 0) {
                UObject* focusWidget = *reinterpret_cast<UObject**>(
                    reinterpret_cast<uint8_t*>(actionBar) + s_off_focus);
                if (focusWidget && isWidgetAlive(focusWidget)) {
                    auto* focusStop = focusWidget->GetFunctionByNameInChain(STR("StopAllAnimations"));
                    if (focusStop) { safeProcessEvent(focusWidget, focusStop, nullptr); childStopped++; }
                }
            }

            VLOG(STR("[MoriaCppMod] [Swap] Stopped action bar animations (parent + {} children)\n"), childStopped);
        }

        // ---- Main swap entry point ------------------------------------------
        void swapToolbar()
        {
            if (!m_characterLoaded) { showErrorBox(L"Not loaded"); return; }
            if (m_playerGuidStr.empty()) {
                readPlayerGuid();
                if (m_playerGuidStr.empty()) { showErrorBox(L"PlayerGuid not found"); return; }
            }

            // Block if swap already in progress (multi-frame SM running)
            if (m_swapInProgress) {
                showErrorBox(L"Swap in progress");
                return;
            }

            // Cooldown: game needs time to process inventory changes
            ULONGLONG now = GetTickCount64();
            if (m_lastSwapTime > 0 && (now - m_lastSwapTime) < 2000) {
                ULONGLONG remaining = 2000 - (now - m_lastSwapTime);
                std::wstring msg = std::format(L"Swap cooldown ({:.1f}s)", remaining / 1000.0);
                showErrorBox(msg);
                return;
            }

            m_swapInProgress = true;
            try { swapPhaseRemove(); }
            catch (const std::exception& e) {
                VLOG(STR("[MoriaCppMod] [Swap] EXCEPTION in remove phase: {}\n"), std::wstring(e.what(), e.what() + strlen(e.what())));
                showErrorBox(L"Swap failed (exception)");
                m_swapInProgress = false;
                m_swapPhase = SwapPhase::Idle;
            }
            catch (...) {
                VLOG(STR("[MoriaCppMod] [Swap] UNKNOWN EXCEPTION in remove phase\n"));
                showErrorBox(L"Swap failed (unknown)");
                m_swapInProgress = false;
                m_swapPhase = SwapPhase::Idle;
            }
        }

        // Phase 1: Stop animations, read/save hotbar, remove all items, then wait for MovieScene
        void swapPhaseRemove()
        {
            stopActionBarAnimations();

            UObject* pawn = getPawn();
            auto* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp) { showErrorBox(L"No inventory component"); m_swapInProgress = false; return; }
            probeItemInstanceStruct(invComp);

            // 1. Snapshot current hotbar
            auto currentItems = readHotbarItems();
            int curCount = 0;
            for (auto& item : currentItems) if (item.valid) curCount++;

            // 2. Load stash from file into member storage (persists across frames)
            m_swapStash = loadStashFile();
            int stashCount = 0;
            for (auto& item : m_swapStash) if (item.valid) stashCount++;

            VLOG(STR("[MoriaCppMod] [Swap] Current hotbar: {} items, stash file: {} items\n"), curCount, stashCount);

            // 2b. Pre-resolve all stashed item classes BEFORE removing anything
            for (int i = 0; i < TOOLBAR_SLOTS; i++)
            {
                if (!m_swapStash[i].valid) continue;
                UObject* cls = resolveItemClass(m_swapStash[i].className);
                if (cls)
                    VLOG(STR("[MoriaCppMod] [Swap] Pre-cached class for stash slot {}: {}\n"), i, m_swapStash[i].className);
                for (auto& ci : m_swapStash[i].contents)
                    resolveItemClass(ci.className);
            }

            // 2c. Log stashed effects
            for (int i = 0; i < TOOLBAR_SLOTS; i++)
            {
                if (!m_swapStash[i].valid || m_swapStash[i].effects.empty()) continue;
                for (auto& eff : m_swapStash[i].effects)
                    VLOG(STR("[MoriaCppMod] [Swap] Stash slot {} has effect '{}:{}' — will restore after recreation\n"), i, eff.typeName, eff.assetName);
            }

            // 3. Save current hotbar to file
            saveStashFile(currentItems);
            VLOG(STR("[MoriaCppMod] [Swap] Stash file saved. Proceeding to remove...\n"));

            // 4. Destroy current hotbar items via RemoveItem
            auto* removeFn = invComp->GetFunctionByNameInChain(STR("RemoveItem"));
            if (!removeFn) { showErrorBox(L"RemoveItem not found"); m_swapInProgress = false; return; }
            int rmItemOff = -1, rmCountOff = -1, rmFromOff = -1;
            for (auto* p : removeFn->ForEachProperty()) {
                std::wstring n(p->GetName());
                if (n == L"Item") rmItemOff = p->GetOffset_Internal();
                if (n == L"Count") rmCountOff = p->GetOffset_Internal();
                if (n == L"From") rmFromOff = p->GetOffset_Internal();
            }

            for (int i = 0; i < TOOLBAR_SLOTS; i++)
            {
                if (!currentItems[i].valid) continue;
                if (currentItems[i].itemId > 0)
                    removeEffectsForItem(invComp, currentItems[i].itemId);
                UObject* cls = resolveItemClass(currentItems[i].className);
                if (!cls) {
                    VLOG(STR("[MoriaCppMod] [Swap] WARN: cannot resolve class '{}' for removal\n"), currentItems[i].className);
                    continue;
                }
                // Remove container contents first
                for (auto& ci : currentItems[i].contents)
                {
                    UObject* contentCls = resolveItemClass(ci.className);
                    if (!contentCls) continue;
                    std::vector<uint8_t> rmBuf(std::max(removeFn->GetParmsSize() + 32, 64), 0);
                    *reinterpret_cast<UObject**>(rmBuf.data() + rmItemOff) = contentCls;
                    *reinterpret_cast<int32_t*>(rmBuf.data() + rmCountOff) = ci.count;
                    if (rmFromOff >= 0) rmBuf[rmFromOff] = 0;
                    if (!safeProcessEvent(invComp, removeFn, rmBuf.data()))
                        VLOG(STR("[MoriaCppMod] [Swap] SEH crash removing container content '{}' — continuing\n"), ci.className);
                }
                // Remove the main item
                std::vector<uint8_t> rmBuf(std::max(removeFn->GetParmsSize() + 32, 64), 0);
                *reinterpret_cast<UObject**>(rmBuf.data() + rmItemOff) = cls;
                *reinterpret_cast<int32_t*>(rmBuf.data() + rmCountOff) = currentItems[i].count;
                if (rmFromOff >= 0) rmBuf[rmFromOff] = 0;
                if (!safeProcessEvent(invComp, removeFn, rmBuf.data()))
                    VLOG(STR("[MoriaCppMod] [Swap] SEH crash removing '{}' — continuing\n"), currentItems[i].className);
                VLOG(STR("[MoriaCppMod] [Swap] Removed: {} x{}\n"), currentItems[i].className, currentItems[i].count);
            }

            purgeOrphanedEffects(invComp);

            // Transition to wait phase — let MovieScene settle before adding items
            m_swapPhase = SwapPhase::WaitAfterRemove;
            m_swapFrameWait = 0;
            VLOG(STR("[MoriaCppMod] [Swap] Remove phase complete, waiting {} frames for MovieScene settle\n"), SWAP_SETTLE_FRAMES);
        }

        // Phase 3: Recreate stashed items (called after MovieScene settle)
        void swapPhaseAdd()
        {
            VLOG(STR("[MoriaCppMod] [Swap] Add phase starting...\n"));

            UObject* pawn = getPawn();
            auto* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp) { showErrorBox(L"No inventory component"); m_swapInProgress = false; m_swapPhase = SwapPhase::Idle; return; }

            auto* addFn = invComp->GetFunctionByNameInChain(STR("AddItem"));
            if (!addFn) { showErrorBox(L"AddItem not found"); m_swapInProgress = false; m_swapPhase = SwapPhase::Idle; return; }
            int aiItemOff = -1, aiCountOff = -1, aiMethodOff = -1, aiRetOff = -1;
            for (auto* p : addFn->ForEachProperty()) {
                std::wstring n(p->GetName());
                if (n == L"Item") aiItemOff = p->GetOffset_Internal();
                if (n == L"Count") aiCountOff = p->GetOffset_Internal();
                if (n == L"Method") aiMethodOff = p->GetOffset_Internal();
                if (n == L"ReturnValue") aiRetOff = p->GetOffset_Internal();
            }

            auto* getSlotFn = invComp->GetFunctionByNameInChain(STR("GetItemForHotbarSlot"));
            int gsIndexOff = -1, gsRetOff = -1, handleSize = 0;
            if (getSlotFn) {
                for (auto* p : getSlotFn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"HotbarIndex") gsIndexOff = p->GetOffset_Internal();
                    if (n == L"ReturnValue") { gsRetOff = p->GetOffset_Internal(); handleSize = p->GetSize(); }
                }
            }

            auto* moveFn = invComp->GetFunctionByNameInChain(STR("MoveItem"));
            int mvItemOff = -1, mvDestOff = -1, mvAddTypeOff = -1;
            if (moveFn) {
                for (auto* p : moveFn->ForEachProperty()) {
                    std::wstring n(p->GetName());
                    if (n == L"Item") mvItemOff = p->GetOffset_Internal();
                    if (n == L"Destination") mvDestOff = p->GetOffset_Internal();
                    if (n == L"AddType") mvAddTypeOff = p->GetOffset_Internal();
                }
            }

            static int s_off_invItems3 = -2;
            if (s_off_invItems3 == -2) resolveOffset(invComp, L"Items", s_off_invItems3);
            int itemsListOffset = (s_off_invItems3 >= 0) ? s_off_invItems3 + iiaListOff() : 0x0330;

            // Lambda: read Items.List snapshot
            auto readItemsList = [&]() {
                uint8_t* invBase = reinterpret_cast<uint8_t*>(invComp);
                struct { uint8_t* Data; int32_t Num; int32_t Max; } lst{};
                if (isReadableMemory(invBase + itemsListOffset, 16))
                    std::memcpy(&lst, invBase + itemsListOffset, 16);
                return lst;
            };

            auto patchDurability = [&](int32_t newId, float dur) {
                if (newId <= 0 || dur <= 0.0f) return;
                auto lst = readItemsList();
                if (!lst.Data || lst.Num <= 0 || !isReadableMemory(lst.Data, lst.Num * iiSize())) return;
                for (int j = 0; j < lst.Num; j++) {
                    uint8_t* entry = lst.Data + j * iiSize();
                    int32_t id = *reinterpret_cast<int32_t*>(entry + iiIDOff());
                    if (id == newId) {
                        float oldDur = *reinterpret_cast<float*>(entry + iiDurOff());
                        *reinterpret_cast<float*>(entry + iiDurOff()) = dur;
                        VLOG(STR("[MoriaCppMod] [Swap] Patched durability: {:.1f} -> {:.1f}\n"), oldDur, dur);
                        break;
                    }
                }
            };

            auto findHandleForId = [&](int32_t targetId) -> std::vector<uint8_t> {
                if (!getSlotFn || gsRetOff < 0 || handleSize <= 0) return {};
                auto lst = readItemsList();
                if (!lst.Data || lst.Num <= 0) return {};
                std::vector<uint8_t> handle(handleSize, 0);
                *reinterpret_cast<int32_t*>(handle.data()) = targetId;
                if (callIsValidItem(handle.data(), handleSize))
                    return handle;
                return {};
            };

            auto recreateItem = [&](int i) -> bool {
                if (!m_swapStash[i].valid) return false;
                UObject* cls = resolveItemClass(m_swapStash[i].className);
                if (!cls) {
                    VLOG(STR("[MoriaCppMod] [Swap] WARN: cannot resolve class '{}' for creation -- skipping\n"), m_swapStash[i].className);
                    return false;
                }

                std::vector<uint8_t> aiBuf(std::max(addFn->GetParmsSize() + 32, 128), 0);
                *reinterpret_cast<UObject**>(aiBuf.data() + aiItemOff) = cls;
                *reinterpret_cast<int32_t*>(aiBuf.data() + aiCountOff) = m_swapStash[i].count;
                if (aiMethodOff >= 0) aiBuf[aiMethodOff] = 1; // EAddItem::Create

                if (!safeProcessEvent(invComp, addFn, aiBuf.data())) {
                    VLOG(STR("[MoriaCppMod] [Swap] SEH crash in AddItem for slot {} '{}' — will retry\n"), i, m_swapStash[i].className);
                    return false;
                }

                int32_t newId = (aiRetOff >= 0) ? *reinterpret_cast<int32_t*>(aiBuf.data() + aiRetOff) : -1;
                VLOG(STR("[MoriaCppMod] [Swap] Added: {} x{} -> newId={} (Create)\n"), m_swapStash[i].className, m_swapStash[i].count, newId);

                if (newId <= 0 && aiMethodOff >= 0) {
                    std::memset(aiBuf.data(), 0, aiBuf.size());
                    *reinterpret_cast<UObject**>(aiBuf.data() + aiItemOff) = cls;
                    *reinterpret_cast<int32_t*>(aiBuf.data() + aiCountOff) = m_swapStash[i].count;
                    aiBuf[aiMethodOff] = 2; // EAddItem::Silent
                    if (safeProcessEvent(invComp, addFn, aiBuf.data()))
                        newId = (aiRetOff >= 0) ? *reinterpret_cast<int32_t*>(aiBuf.data() + aiRetOff) : -1;
                    VLOG(STR("[MoriaCppMod] [Swap] Retry with Silent: {} -> newId={}\n"), m_swapStash[i].className, newId);
                }

                if (newId <= 0) return false;

                patchDurability(newId, m_swapStash[i].durability);

                // Restore container contents via MoveItem
                if (!m_swapStash[i].contents.empty() && moveFn && mvItemOff >= 0 && mvDestOff >= 0)
                {
                    auto containerHandle = findHandleForId(newId);
                    if (containerHandle.empty()) {
                        VLOG(STR("[MoriaCppMod] [Swap] WARN: cannot find handle for container newId={}\n"), newId);
                    } else {
                        int contentRestored = 0;
                        for (auto& ci : m_swapStash[i].contents)
                        {
                            UObject* contentCls = resolveItemClass(ci.className);
                            if (!contentCls) continue;

                            std::vector<uint8_t> ciBuf(std::max(addFn->GetParmsSize() + 32, 128), 0);
                            *reinterpret_cast<UObject**>(ciBuf.data() + aiItemOff) = contentCls;
                            *reinterpret_cast<int32_t*>(ciBuf.data() + aiCountOff) = ci.count;
                            if (aiMethodOff >= 0) ciBuf[aiMethodOff] = 1;
                            if (!safeProcessEvent(invComp, addFn, ciBuf.data())) continue;

                            int32_t contentNewId = (aiRetOff >= 0)
                                ? *reinterpret_cast<int32_t*>(ciBuf.data() + aiRetOff) : -1;
                            if (contentNewId <= 0 && aiMethodOff >= 0) {
                                std::memset(ciBuf.data(), 0, ciBuf.size());
                                *reinterpret_cast<UObject**>(ciBuf.data() + aiItemOff) = contentCls;
                                *reinterpret_cast<int32_t*>(ciBuf.data() + aiCountOff) = ci.count;
                                ciBuf[aiMethodOff] = 2;
                                if (safeProcessEvent(invComp, addFn, ciBuf.data()))
                                    contentNewId = (aiRetOff >= 0)
                                        ? *reinterpret_cast<int32_t*>(ciBuf.data() + aiRetOff) : -1;
                            }
                            if (contentNewId <= 0) continue;

                            patchDurability(contentNewId, ci.durability);

                            auto contentHandle = findHandleForId(contentNewId);
                            if (contentHandle.empty()) continue;

                            std::vector<uint8_t> mvBuf(std::max(moveFn->GetParmsSize() + 32, 128), 0);
                            std::memcpy(mvBuf.data() + mvItemOff, contentHandle.data(),
                                        std::min((int)contentHandle.size(), handleSize));
                            std::memcpy(mvBuf.data() + mvDestOff, containerHandle.data(),
                                        std::min((int)containerHandle.size(), handleSize));
                            if (mvAddTypeOff >= 0) mvBuf[mvAddTypeOff] = 0;

                            if (safeProcessEvent(invComp, moveFn, mvBuf.data())) {
                                contentRestored++;
                                VLOG(STR("[MoriaCppMod] [Swap] Moved '{}' x{} into container (slot {})\n"), ci.className, ci.count, i);
                            }
                        }
                        VLOG(STR("[MoriaCppMod] [Swap] Container slot {}: restored {}/{} content items\n"),
                             i, contentRestored, m_swapStash[i].contents.size());
                    }
                }

                if (!m_swapStash[i].effects.empty())
                {
                    for (auto& eff : m_swapStash[i].effects)
                    {
                        VLOG(STR("[MoriaCppMod] [Swap] Restoring effect '{}:{}' for slot {} newId={}\n"), eff.typeName, eff.assetName, i, newId);
                        restoreEffectDirect(invComp, newId, eff);
                    }
                }
                return true;
            };

            int recreated = 0;
            std::vector<int> failed;
            for (int i = 0; i < TOOLBAR_SLOTS; i++)
            {
                if (!m_swapStash[i].valid) continue;
                VLOG(STR("[MoriaCppMod] [Swap] Recreating slot {}: '{}' x{}\n"), i, m_swapStash[i].className, m_swapStash[i].count);
                if (recreateItem(i))
                    recreated++;
                else if (m_swapStash[i].valid)
                    failed.push_back(i);
            }

            // Report failures (no more blocking retry loop — retries removed to avoid Sleep on game thread)
            for (int i : failed)
                VLOG(STR("[MoriaCppMod] [Swap] FAILED: slot {} '{}' — item lost\n"), i, m_swapStash[i].className);

            // Store recreated count for finish phase
            m_swapRecreated = recreated;

            // Transition to wait phase — let MovieScene settle before finishing
            m_swapPhase = SwapPhase::WaitAfterAdd;
            m_swapFrameWait = 0;
            VLOG(STR("[MoriaCppMod] [Swap] Add phase complete ({} items), waiting {} frames for MovieScene settle\n"), recreated, SWAP_SETTLE_FRAMES);
        }

        int m_swapRecreated{0};  // count from add phase, used by finish phase

        // Phase 5: Stop animations, toggle toolbar indicator
        void swapPhaseFinish()
        {
            VLOG(STR("[MoriaCppMod] [Swap] Finish phase — stopping animations\n"));
            stopActionBarAnimations();

            m_activeToolbar = 1 - m_activeToolbar;
            s_overlay.activeToolbar = m_activeToolbar;
            s_overlay.needsUpdate = true;

            m_lastSwapTime = GetTickCount64();
            m_swapInProgress = false;
            m_swapPhase = SwapPhase::Idle;

            // Clear stash to free memory
            m_swapStash = {};

            std::wstring msg = std::format(L"Toolbar {} ({} items)", m_activeToolbar + 1, m_swapRecreated);
            showOnScreen(msg, 3.0f, 0.0f, 1.0f, 0.5f);
            VLOG(STR("[MoriaCppMod] [Swap] Swap complete: {}\n"), msg);
        }

        // Called from on_update() every frame — drives the multi-frame swap SM
        void tickSwap()
        {
            switch (m_swapPhase)
            {
            case SwapPhase::Idle:
                return;

            case SwapPhase::WaitAfterRemove:
                if (++m_swapFrameWait >= SWAP_SETTLE_FRAMES) {
                    m_swapPhase = SwapPhase::Add;
                }
                return;

            case SwapPhase::Add:
                try { swapPhaseAdd(); }
                catch (const std::exception& e) {
                    VLOG(STR("[MoriaCppMod] [Swap] EXCEPTION in add phase: {}\n"), std::wstring(e.what(), e.what() + strlen(e.what())));
                    showErrorBox(L"Swap failed (add exception)");
                    m_swapInProgress = false;
                    m_swapPhase = SwapPhase::Idle;
                }
                catch (...) {
                    VLOG(STR("[MoriaCppMod] [Swap] UNKNOWN EXCEPTION in add phase\n"));
                    showErrorBox(L"Swap failed (add unknown)");
                    m_swapInProgress = false;
                    m_swapPhase = SwapPhase::Idle;
                }
                return;

            case SwapPhase::WaitAfterAdd:
                if (++m_swapFrameWait >= SWAP_SETTLE_FRAMES) {
                    m_swapPhase = SwapPhase::Finish;
                }
                return;

            case SwapPhase::Finish:
                swapPhaseFinish();
                return;
            }
        }
