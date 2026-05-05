// moria_unlock.inl — Recipe unlock + read-history clear + buff toggles + peace mode (v6.4.1)

        // v6.4.1 — Cheats tab buff entry data.
        enum class CheatRowKind { ClearAllBtn, SectionHeader, BuffToggle };
        struct CheatEntry {
            CheatRowKind kind;
            const wchar_t* label;
            const wchar_t* effect1;  // nullptr if not applicable
            const wchar_t* effect2;  // nullptr if only one effect
        };

        // Full catalogue of cheat entries beneath the existing Unlock/Read/PeaceMode rows.
        // Kept in a single static table so the UI builder + click handler can iterate identically.
        static const CheatEntry* cheatEntries(int& outCount)
        {
            static const CheatEntry entries[] = {
                { CheatRowKind::ClearAllBtn,   STR("Clear All Buffs"),    STR("GE_DebugRemoveBuffs"),  STR("GE_ClearFoodBuffs") },

                { CheatRowKind::SectionHeader, STR("God-Mode"),           nullptr, nullptr },
                { CheatRowKind::BuffToggle,    STR("Ring of Power"),      STR("GE_DEV_RingOfPower"),            nullptr },
                { CheatRowKind::BuffToggle,    STR("Boots of Speed"),     STR("GE_BootsOfDebugSpeed"),          nullptr },
                { CheatRowKind::BuffToggle,    STR("No Fall Damage"),     STR("GE_FallDamageImmunity"),         nullptr },
                { CheatRowKind::BuffToggle,    STR("Shadow Immunity"),    STR("GE_Darkness_Immunity"),          STR("GE_AntiShadowPoisonConsumable") },

                { CheatRowKind::SectionHeader, STR("Environmental"),      nullptr, nullptr },
                { CheatRowKind::BuffToggle,    STR("Warmth Buff"),        STR("GE_Warm"),                       nullptr },
                { CheatRowKind::BuffToggle,    STR("Poison Immunity"),    STR("GE_AntiPoisonConsumable"),       nullptr },

                { CheatRowKind::SectionHeader, STR("Survival"),           nullptr, nullptr },
                { CheatRowKind::BuffToggle,    STR("No Hunger"),          STR("GE_Full"),                       nullptr },
                { CheatRowKind::BuffToggle,    STR("Breakfast"),          STR("GE_Full_Breakfast"),             nullptr },
                { CheatRowKind::BuffToggle,    STR("Second Breakfast"),   STR("GE_Full_SecondBreakfast"),       nullptr },
                { CheatRowKind::BuffToggle,    STR("Lunch"),              STR("GE_Full_Lunch"),                 nullptr },
                { CheatRowKind::BuffToggle,    STR("Dinner"),             STR("GE_Full_Dinner"),                nullptr },

                { CheatRowKind::SectionHeader, STR("Attributes"),         nullptr, nullptr },
                { CheatRowKind::BuffToggle,    STR("Health Regen"),       STR("GE_HealthBrewSip"),              STR("GE_RingHuntersOrc_PersistentHealingBuff") },
                { CheatRowKind::BuffToggle,    STR("Stamina"),            STR("GE_StaminaBrewSip"),             STR("GE_StaminaBrew") },
                { CheatRowKind::BuffToggle,    STR("Defense"),            STR("GE_DefenseBrewSip"),             nullptr },
                { CheatRowKind::BuffToggle,    STR("Cold Immunity"),      STR("GE_FrostBrew"),                  nullptr },
                { CheatRowKind::BuffToggle,    STR("Strength"),           STR("GE_BeornBrewEffects"),           nullptr },
                { CheatRowKind::BuffToggle,    STR("Vitality"),           STR("GE_EntBrewEffects"),             nullptr },
                { CheatRowKind::BuffToggle,    STR("Swiftness"),          STR("GE_RohanBrewEffects"),           nullptr },
                { CheatRowKind::BuffToggle,    STR("Evening"),            STR("GE_EveningAleSip"),              nullptr },
                { CheatRowKind::BuffToggle,    STR("Energy"),             STR("GE_EveningAle_ReplenishEnergyEffect"), nullptr },
                { CheatRowKind::BuffToggle,    STR("Health"),             STR("GE_HealthBrew"),                 nullptr },
                { CheatRowKind::BuffToggle,    STR("Defensive"),          STR("GE_DefensiveBrew"),              nullptr },

                { CheatRowKind::SectionHeader, STR("Combat"),             nullptr, nullptr },
                { CheatRowKind::BuffToggle,    STR("Damage"),             STR("GE_AttackBoost_Consumable"),     nullptr },
                { CheatRowKind::BuffToggle,    STR("Damage Reduction"),   STR("GE_DefenseBoost_Consumable"),    nullptr },
                { CheatRowKind::BuffToggle,    STR("Boost Stamina"),      STR("GE_StaminaBoost_Consumable"),    nullptr },
                { CheatRowKind::BuffToggle,    STR("Free Dodge"),         STR("GE_FreeDodge_Consumable"),       nullptr },
                { CheatRowKind::BuffToggle,    STR("Free Sprint"),        STR("GE_FreeSprint_Consumable"),      nullptr },
            };
            outCount = (int)(sizeof(entries) / sizeof(entries[0]));
            return entries;
        }

        // v6.4.1 — Tweaks tab entries. Each row cycles through a preset list of integer values.
        // Index 0 is always 0 == DEFAULT (restore originals). Multiplier tweaks interpret the
        // remaining values as "N× original"; absolute tweaks use the value directly.
        enum class TweakKind { SectionHeader, TweakRow, SpecialNoCost, SpecialInstantCraft };
        struct TweakEntry
        {
            TweakKind kind;
            const wchar_t* label;
            const wchar_t* fieldName;        // property name inside row struct
            bool isFloat;                    // false = int32, true = float
            bool isMultiplier;               // true = original*value, false = absolute value
            bool requiresOriginalGt1;        // only affect rows where original > 1 (MaxStack only)
            const wchar_t* rowStructFilter;  // nullptr = any table with field; else require this exact struct
            std::vector<int> cycleValues;    // [0]=0 DEFAULT; rest = target values (or multipliers)
        };

        static const TweakEntry* tweakEntries(int& outCount)
        {
            static const TweakEntry entries[] = {
                { TweakKind::TweakRow, STR("Max Stack"), STR("MaxStackSize"),
                  /*isFloat=*/false, /*isMult=*/false, /*gt1=*/true, /*filter=*/nullptr,
                  {0, 99, 999, 9999} },

                { TweakKind::TweakRow, STR("Trade Multiplier"), STR("BaseTradeValue"),
                  /*isFloat=*/true, /*isMult=*/true, /*gt1=*/false, /*filter=*/nullptr,
                  {0, 2, 3, 5, 10} },

                { TweakKind::SectionHeader, STR("Weapons"), nullptr, false, false, false, nullptr, {} },

                { TweakKind::TweakRow, STR("Durability"), STR("Durability"),
                  /*isFloat=*/false, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorWeaponDefinition"),
                  {0, 2, 5, 10} },

                { TweakKind::TweakRow, STR("Damage"), STR("Damage"),
                  /*isFloat=*/false, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorWeaponDefinition"),
                  {0, 2} },

                { TweakKind::TweakRow, STR("Speed"), STR("Speed"),
                  /*isFloat=*/true, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorWeaponDefinition"),
                  {0, 2} },

                { TweakKind::SectionHeader, STR("Armor"), nullptr, false, false, false, nullptr, {} },

                { TweakKind::TweakRow, STR("Durability"), STR("Durability"),
                  /*isFloat=*/false, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorArmorDefinition"),
                  {0, 2, 5, 10} },

                { TweakKind::TweakRow, STR("Damage Reduction"), STR("DamageReduction"),
                  /*isFloat=*/true, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorArmorDefinition"),
                  {0, 2} },

                { TweakKind::TweakRow, STR("Damage Protection"), STR("DamageProtection"),
                  /*isFloat=*/true, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorArmorDefinition"),
                  {0, 2, 5, 10} },

                { TweakKind::SectionHeader, STR("Tools"), nullptr, false, false, false, nullptr, {} },

                { TweakKind::TweakRow, STR("Durability"), STR("Durability"),
                  /*isFloat=*/false, /*isMult=*/true, /*gt1=*/false,
                  /*filter=*/STR("MorToolDefinition"),
                  {0, 2, 5, 10} },

                { TweakKind::TweakRow, STR("Stamina Cost"), STR("StaminaCost"),
                  /*isFloat=*/true, /*isMult=*/false, /*gt1=*/false,
                  /*filter=*/STR("MorToolDefinition"),
                  {0, 0} },    // index 0=DEFAULT, index 1=set to 0

                { TweakKind::TweakRow, STR("Energy Cost"), STR("EnergyCost"),
                  /*isFloat=*/true, /*isMult=*/false, /*gt1=*/false,
                  /*filter=*/STR("MorToolDefinition"),
                  {0, 0} },    // index 0=DEFAULT, index 1=set to 0

                { TweakKind::TweakRow, STR("Carve Hits"), STR("CarveHits"),
                  /*isFloat=*/false, /*isMult=*/false, /*gt1=*/false,
                  /*filter=*/STR("MorToolDefinition"),
                  {0, 1} },    // index 0=DEFAULT, index 1=set to 1 (one-hit mining)

                { TweakKind::SectionHeader, STR("Crafting"), nullptr, false, false, false, nullptr, {} },

                { TweakKind::SpecialNoCost, STR("No Cost"), nullptr,
                  false, false, false, nullptr, {0, 1} },  // OFF / ON

                { TweakKind::SpecialInstantCraft, STR("Instant Craft"), nullptr,
                  false, false, false, nullptr, {0, 1} },  // OFF / ON
            };
            outCount = (int)(sizeof(entries) / sizeof(entries[0]));
            return entries;
        }

        // Iterate every DataTable; for each row whose RowStruct contains `fieldName`
        // (optionally gated by exact rowStructName match), read the current value into
        // m_tweakOriginals (first time only), then write the new value.
        // If useDefault is true, restore from originals instead.
        void applyFieldTweak(const wchar_t* fieldName, bool isFloat, bool isMultiplier,
                             bool requiresGt1, const wchar_t* rowStructFilter,
                             int tweakValue, bool useDefault)
        {
            if (!fieldName || !fieldName[0]) return;
            std::vector<UObject*> dts;
            UObjectGlobals::FindAllOf(STR("DataTable"), dts);

            int totalRowsTouched = 0;
            int totalTablesTouched = 0;

            for (UObject* dt : dts)
            {
                if (!dt || !isObjectAlive(dt)) continue;

                // Get RowStruct
                auto** rsPtr = dt->GetValuePtrByPropertyNameInChain<UStruct*>(STR("RowStruct"));
                if (!rsPtr || !*rsPtr) continue;
                UStruct* rowStruct = *rsPtr;

                // Optional exact-struct filter
                if (rowStructFilter && rowStructFilter[0])
                {
                    try
                    {
                        std::wstring rsName = rowStruct->GetName();
                        if (rsName != rowStructFilter) continue;
                    }
                    catch (...) { continue; }
                }

                // Find the property inside the row struct, walking the super chain.
                // ForEachProperty returns only properties declared directly on the struct, not
                // inherited ones — so we also iterate super structs for base-class fields like
                // MaxStackSize (declared on FMorItemDefinition but inherited by FMorConsumableDefinition,
                // FMorWeaponDefinition, etc.).
                FProperty* targetProp = nullptr;
                for (UStruct* walk = rowStruct; walk && !targetProp; walk = walk->GetSuperStruct())
                {
                    for (auto* prop : walk->ForEachProperty())
                    {
                        if (prop->GetName() == std::wstring_view(fieldName))
                        {
                            targetProp = prop;
                            break;
                        }
                    }
                }
                if (!targetProp) continue;
                int fieldOff = targetProp->GetOffset_Internal();

                // Bind DataTable for row enumeration
                std::wstring tName;
                try { tName = dt->GetName(); } catch (...) { continue; }
                DataTableUtil util;
                if (!util.bind(tName.c_str())) continue;

                auto rowNames = util.getRowNames();
                int rowsTouched = 0;
                for (const auto& rn : rowNames)
                {
                    uint8_t* rowData = util.findRowData(rn.c_str());
                    if (!rowData) continue;
                    uint8_t* fieldAddr = rowData + fieldOff;
                    if (!isReadableMemory(fieldAddr, 4)) continue;

                    // Composite key: "<rowData addr hex>|<fieldName>"
                    wchar_t kbuf[64];
                    swprintf(kbuf, 64, L"%p|%ls", (void*)rowData, fieldName);
                    std::wstring key(kbuf);

                    // Capture original on first encounter
                    if (!m_tweakOriginals.count(key))
                    {
                        double orig = isFloat ? (double)*reinterpret_cast<float*>(fieldAddr)
                                              : (double)*reinterpret_cast<int32_t*>(fieldAddr);
                        m_tweakOriginals[key] = orig;
                    }
                    double original = m_tweakOriginals[key];

                    if (useDefault)
                    {
                        // Restore original
                        if (isFloat) *reinterpret_cast<float*>(fieldAddr)   = (float)original;
                        else         *reinterpret_cast<int32_t*>(fieldAddr) = (int32_t)original;
                        rowsTouched++;
                        continue;
                    }

                    if (requiresGt1 && original <= 1.0) continue;

                    double finalValue = isMultiplier ? original * (double)tweakValue
                                                     : (double)tweakValue;
                    if (isFloat) *reinterpret_cast<float*>(fieldAddr)   = (float)finalValue;
                    else         *reinterpret_cast<int32_t*>(fieldAddr) = (int32_t)finalValue;
                    rowsTouched++;
                }

                if (rowsTouched > 0) { totalRowsTouched += rowsTouched; totalTablesTouched++; }
            }

            VLOG(STR("[Tweak] field '{}' → {} {} rows across {} tables\n"),
                 fieldName,
                 useDefault ? STR("restored") : STR("set on"),
                 totalRowsTouched, totalTablesTouched);
        }

        // v6.4.1 SpecialNoCost — set all FMorRecipeDefinition-derived DT rows to zero material cost
        // (DefaultRequiredMaterials[*].Count = 0) and set bAllowRefunds = true on construction recipes.
        void applyNoCostRecipe(bool useDefault)
        {
            std::vector<UObject*> dts;
            UObjectGlobals::FindAllOf(STR("DataTable"), dts);

            int totalRows = 0;
            int totalTables = 0;
            for (UObject* dt : dts)
            {
                if (!dt || !isObjectAlive(dt)) continue;

                auto** rsPtr = dt->GetValuePtrByPropertyNameInChain<UStruct*>(STR("RowStruct"));
                if (!rsPtr || !*rsPtr) continue;
                UStruct* rowStruct = *rsPtr;

                // Must inherit from FMorRecipeDefinition (i.e., has DefaultRequiredMaterials array at 0x40)
                bool hasDRM = false;
                for (UStruct* w = rowStruct; w && !hasDRM; w = w->GetSuperStruct())
                {
                    for (auto* p : w->ForEachProperty())
                    {
                        if (p->GetName() == std::wstring_view(L"DefaultRequiredMaterials"))
                        { hasDRM = true; break; }
                    }
                }
                if (!hasDRM) continue;

                // Detect if this is a construction recipe table (has bAllowRefunds)
                bool isConstructionRecipe = false;
                for (UStruct* w = rowStruct; w && !isConstructionRecipe; w = w->GetSuperStruct())
                {
                    for (auto* p : w->ForEachProperty())
                    {
                        if (p->GetName() == std::wstring_view(L"bAllowRefunds"))
                        { isConstructionRecipe = true; break; }
                    }
                }

                std::wstring tName;
                try { tName = dt->GetName(); } catch (...) { continue; }
                DataTableUtil util;
                if (!util.bind(tName.c_str())) continue;

                auto rowNames = util.getRowNames();
                int rows = 0;
                for (const auto& rn : rowNames)
                {
                    uint8_t* rowData = util.findRowData(rn.c_str());
                    if (!rowData) continue;

                    // DefaultRequiredMaterials is at offset 0x40 in FMorRecipeDefinition (base)
                    uint8_t* arrBase = rowData + 0x40;
                    if (!isReadableMemory(arrBase, 16)) continue;

                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(arrBase);
                    int32_t arrNum   = *reinterpret_cast<int32_t*>(arrBase + 8);
                    if (!arrData || arrNum <= 0 || arrNum > 100) { /* still continue to bAllowRefunds */ }
                    else
                    {
                        // Each element is FMorRequiredRecipeMaterial (0x28 bytes), Count at +0x20 (int32)
                        constexpr int kStride = 0x28;
                        for (int32_t i = 0; i < arrNum; ++i)
                        {
                            uint8_t* elem = arrData + i * kStride;
                            if (!isReadableMemory(elem, kStride)) continue;
                            int32_t* countAddr = reinterpret_cast<int32_t*>(elem + 0x20);

                            wchar_t kbuf[96];
                            swprintf(kbuf, 96, L"%p|DRM[%d].Count", (void*)rowData, i);
                            std::wstring key(kbuf);

                            if (!m_tweakOriginals.count(key))
                                m_tweakOriginals[key] = (double)*countAddr;

                            if (useDefault) *countAddr = (int32_t)m_tweakOriginals[key];
                            else            *countAddr = 0;
                        }
                    }

                    // bAllowRefunds at offset 0xF2 (bool, 1 byte) — construction recipes only
                    if (isConstructionRecipe)
                    {
                        uint8_t* flagAddr = rowData + 0xF2;
                        if (isReadableMemory(flagAddr, 1))
                        {
                            wchar_t kbuf[96];
                            swprintf(kbuf, 96, L"%p|bAllowRefunds", (void*)rowData);
                            std::wstring key(kbuf);

                            if (!m_tweakOriginals.count(key))
                                m_tweakOriginals[key] = (double)*flagAddr;

                            if (useDefault) *flagAddr = (uint8_t)m_tweakOriginals[key];
                            else            *flagAddr = 1;
                        }
                    }

                    rows++;
                }
                if (rows > 0) { totalRows += rows; totalTables++; }
            }
            VLOG(STR("[Tweak] NoCost → {} {} rows across {} recipe tables\n"),
                 useDefault ? STR("restored") : STR("zeroed"),
                 totalRows, totalTables);
        }

        // v6.4.1 SpecialInstantCraft — set CraftTimeSeconds to 0.1s on all FMorItemRecipeDefinition rows.
        void applyInstantCraft(bool useDefault)
        {
            std::vector<UObject*> dts;
            UObjectGlobals::FindAllOf(STR("DataTable"), dts);

            int totalRows = 0;
            int totalTables = 0;
            for (UObject* dt : dts)
            {
                if (!dt || !isObjectAlive(dt)) continue;

                auto** rsPtr = dt->GetValuePtrByPropertyNameInChain<UStruct*>(STR("RowStruct"));
                if (!rsPtr || !*rsPtr) continue;
                UStruct* rowStruct = *rsPtr;

                // Find CraftTimeSeconds on the row struct (walk super chain)
                FProperty* ctProp = nullptr;
                for (UStruct* w = rowStruct; w && !ctProp; w = w->GetSuperStruct())
                {
                    for (auto* p : w->ForEachProperty())
                    {
                        if (p->GetName() == std::wstring_view(L"CraftTimeSeconds"))
                        { ctProp = p; break; }
                    }
                }
                if (!ctProp) continue;
                int ctOff = ctProp->GetOffset_Internal();

                std::wstring tName;
                try { tName = dt->GetName(); } catch (...) { continue; }
                DataTableUtil util;
                if (!util.bind(tName.c_str())) continue;

                auto rowNames = util.getRowNames();
                int rows = 0;
                for (const auto& rn : rowNames)
                {
                    uint8_t* rowData = util.findRowData(rn.c_str());
                    if (!rowData) continue;
                    float* ctAddr = reinterpret_cast<float*>(rowData + ctOff);
                    if (!isReadableMemory((uint8_t*)ctAddr, 4)) continue;

                    wchar_t kbuf[64];
                    swprintf(kbuf, 64, L"%p|CraftTimeSeconds", (void*)rowData);
                    std::wstring key(kbuf);

                    if (!m_tweakOriginals.count(key))
                        m_tweakOriginals[key] = (double)*ctAddr;

                    if (useDefault) *ctAddr = (float)m_tweakOriginals[key];
                    else            *ctAddr = 0.1f;
                    rows++;
                }
                if (rows > 0) { totalRows += rows; totalTables++; }
            }
            VLOG(STR("[Tweak] InstantCraft → {} {} rows across {} tables\n"),
                 useDefault ? STR("restored") : STR("set to 0.1s"),
                 totalRows, totalTables);
        }

        // Cycle one tweak entry forward. cycleValues[0] is always the DEFAULT slot (its numeric
        // value is ignored); the tweak restores originals when curIdx==0 and otherwise applies
        // cycleValues[curIdx].
        void cycleTweakValue(int idx)
        {
            int count = 0;
            const TweakEntry* all = tweakEntries(count);
            if (idx < 0 || idx >= count) return;
            const TweakEntry& e = all[idx];

            int& curIdx = m_tweakCurrentIdx[idx];
            curIdx = (curIdx + 1) % (int)e.cycleValues.size();
            int newVal = e.cycleValues[curIdx];
            bool useDefault = (curIdx == 0);

            switch (e.kind)
            {
                case TweakKind::TweakRow:
                    applyFieldTweak(e.fieldName, e.isFloat, e.isMultiplier,
                                    e.requiresOriginalGt1, e.rowStructFilter,
                                    newVal, useDefault);
                    break;
                case TweakKind::SpecialNoCost:
                    applyNoCostRecipe(useDefault);
                    break;
                case TweakKind::SpecialInstantCraft:
                    applyInstantCraft(useDefault);
                    break;
                default: break;
            }

            updateTweakRowUI(idx);
            saveConfig();  // v6.4.4+ — persist to MoriaCppMod.ini
        }

        // Cache: short GE name → UClass*. Populated from enumerating loaded GE Class Default Objects.
        std::unordered_map<std::wstring, UClass*> m_geClassCache;
        bool m_geClassCachePopulated{false};

        // Populate the cache by iterating every UObject via UE4SS's ForEachUObject, filtering
        // to UClass objects whose super-chain includes UGameplayEffect. Expensive on first call
        // (~50K objects), cheap on subsequent calls.
        void populateGEClassCache()
        {
            if (m_geClassCachePopulated) return;
            m_geClassCachePopulated = true;

            // Find the UGameplayEffect base class by its full engine path
            UClass* geBaseClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/GameplayAbilities.GameplayEffect"));
            if (!geBaseClass)
            {
                VLOG(STR("[Buff] UGameplayEffect base class not found — cache left empty\n"));
                return;
            }

            int indexed = 0;
            int visited = 0;

            UObjectGlobals::ForEachUObject([&](UObject* obj, int32_t /*idx*/, int32_t /*chunk*/) -> LoopAction {
                visited++;
                if (!obj) return LoopAction::Continue;

                UClass* objClass = nullptr;
                try { objClass = obj->GetClassPrivate(); } catch (...) { return LoopAction::Continue; }
                if (!objClass) return LoopAction::Continue;

                std::wstring cname;
                try { cname = objClass->GetName(); } catch (...) { return LoopAction::Continue; }

                // Is this UObject itself a UClass-derived object?
                bool isClassObj = (cname == L"Class" ||
                                   cname == L"BlueprintGeneratedClass" ||
                                   cname == L"DynamicClass");
                if (!isClassObj) return LoopAction::Continue;

                // Walk its super chain looking for UGameplayEffect
                UStruct* asStruct = static_cast<UStruct*>(obj);
                for (UStruct* s = asStruct; s; s = s->GetSuperStruct())
                {
                    if (s == geBaseClass)
                    {
                        std::wstring name;
                        try { name = obj->GetName(); } catch (...) { break; }
                        if (name.size() > 2 && name.substr(name.size() - 2) == L"_C")
                            name = name.substr(0, name.size() - 2);
                        if (!name.empty() && m_geClassCache.find(name) == m_geClassCache.end())
                        {
                            m_geClassCache[name] = static_cast<UClass*>(obj);
                            indexed++;
                        }
                        break;
                    }
                }
                return LoopAction::Continue;
            });

            VLOG(STR("[Buff] populateGEClassCache: indexed {} GE classes (visited {} objects)\n"),
                 indexed, visited);
        }

        // Find a GameplayEffect class by its short name (without _C).
        UClass* findGameplayEffectClass(const wchar_t* shortName)
        {
            if (!shortName || !shortName[0]) return nullptr;
            populateGEClassCache();

            std::wstring key(shortName);
            auto it = m_geClassCache.find(key);
            if (it != m_geClassCache.end()) return it->second;

            // Not in cache — may not be loaded yet. Force a fresh scan in case it was just loaded.
            m_geClassCachePopulated = false;
            m_geClassCache.clear();  // wipe to allow fresh indexing
            populateGEClassCache();
            it = m_geClassCache.find(key);
            if (it != m_geClassCache.end()) return it->second;

            VLOG(STR("[Buff] class '{}' not loaded (not in GE cache)\n"), shortName);
            return nullptr;
        }

        // Find the local player's AbilitySystemComponent via the cached pawn.
        UObject* getPlayerAbilitySystem()
        {
            UObject* pawn = m_localPawn;
            if (!pawn || !isObjectAlive(pawn)) pawn = getPawn();
            if (!pawn || !isObjectAlive(pawn)) return nullptr;

            auto** ascPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("AbilitySystem"));
            if (!ascPtr || !*ascPtr) return nullptr;
            if (!isObjectAlive(*ascPtr)) return nullptr;
            return *ascPtr;
        }

        // Apply one GameplayEffect by class-name lookup. Returns true on success.
        bool applyGEByName(const wchar_t* name)
        {
            if (!name || !name[0]) return false;
            UObject* asc = getPlayerAbilitySystem();
            if (!asc) { VLOG(STR("[Buff] ASC not found for {}\n"), name); return false; }

            UClass* geCls = findGameplayEffectClass(name);
            if (!geCls) { VLOG(STR("[Buff] class '{}' not found\n"), name); return false; }

            auto* fn = asc->GetFunctionByNameInChain(STR("BP_ApplyGameplayEffectToSelf"));
            if (!fn) { VLOG(STR("[Buff] BP_ApplyGameplayEffectToSelf UFunction missing\n")); return false; }

            int pSize = fn->GetParmsSize();
            std::vector<uint8_t> buf(pSize, 0);
            auto* pCls   = findParam(fn, STR("GameplayEffectClass"));
            auto* pLevel = findParam(fn, STR("Level"));
            if (!pCls) return false;
            *reinterpret_cast<UClass**>(buf.data() + pCls->GetOffset_Internal()) = geCls;
            if (pLevel)
                *reinterpret_cast<float*>(buf.data() + pLevel->GetOffset_Internal()) = 1.0f;
            // EffectContext left zeroed — engine handles empty context

            if (!safeProcessEvent(asc, fn, buf.data())) return false;
            VLOG(STR("[Buff] Applied {}\n"), name);
            return true;
        }

        // Remove all active instances of the given GE class.
        bool removeGEByName(const wchar_t* name)
        {
            if (!name || !name[0]) return false;
            UObject* asc = getPlayerAbilitySystem();
            if (!asc) return false;

            UClass* geCls = findGameplayEffectClass(name);
            if (!geCls) return false;

            auto* fn = asc->GetFunctionByNameInChain(STR("RemoveActiveGameplayEffectBySourceEffect"));
            if (!fn) return false;

            int pSize = fn->GetParmsSize();
            std::vector<uint8_t> buf(pSize, 0);
            auto* pCls = findParam(fn, STR("GameplayEffect"));
            if (!pCls) return false;
            *reinterpret_cast<UClass**>(buf.data() + pCls->GetOffset_Internal()) = geCls;
            // InstigatorAbilitySystemComponent: nullptr → match any
            // StacksToRemove: 0 → remove all (zeroed)

            if (!safeProcessEvent(asc, fn, buf.data())) return false;
            VLOG(STR("[Buff] Removed {}\n"), name);
            return true;
        }

        // Toggle one buff entry by index into cheatEntries().
        void toggleBuffEntry(int idx)
        {
            int count = 0;
            const CheatEntry* all = cheatEntries(count);
            if (idx < 0 || idx >= count) return;
            if (all[idx].kind != CheatRowKind::BuffToggle) return;

            m_buffStates[idx] = !m_buffStates[idx];
            bool state = m_buffStates[idx];
            if (state)
            {
                if (all[idx].effect1) applyGEByName(all[idx].effect1);
                if (all[idx].effect2) applyGEByName(all[idx].effect2);
                VLOG(STR("[Cheats] '{}' ON\n"), all[idx].label);
            }
            else
            {
                if (all[idx].effect1) removeGEByName(all[idx].effect1);
                if (all[idx].effect2) removeGEByName(all[idx].effect2);
                VLOG(STR("[Cheats] '{}' OFF\n"), all[idx].label);
            }
            updateBuffRowUI(idx);
            saveConfig();  // v6.4.4+ — persist to MoriaCppMod.ini
        }

        // v6.4.4+ — re-apply any buffs/tweaks/peace-mode that were loaded from INI.
        // Called from the character-loaded block in dllmain.cpp once the player's ASC is available
        // (GE classes can't be discovered or applied before then).
        void applySavedCheatsAndTweaks()
        {
            // Peace Mode
            if (m_pendingPeaceMode && !m_peaceModeEnabled)
            {
                VLOG(STR("[Cheats] Re-applying Peace Mode from INI\n"));
                m_peaceModeEnabled = false;  // togglePeaceMode flips it; start from false
                togglePeaceMode();
                m_pendingPeaceMode = false;
            }

            // Buffs — re-apply every state that was loaded as ON
            int nCheats = 0;
            const CheatEntry* cheats = cheatEntries(nCheats);
            int applied = 0;
            for (int i = 0; i < nCheats && i < (int)m_buffStates.size(); ++i)
            {
                if (cheats[i].kind != CheatRowKind::BuffToggle) continue;
                if (!m_buffStates[i]) continue;
                if (cheats[i].effect1) applyGEByName(cheats[i].effect1);
                if (cheats[i].effect2) applyGEByName(cheats[i].effect2);
                updateBuffRowUI(i);
                applied++;
            }
            if (applied > 0) VLOG(STR("[Cheats] Re-applied {} buffs from INI\n"), applied);

            // Tweaks — re-apply every non-default cycle index
            int nTweaks = 0;
            const TweakEntry* tweaks = tweakEntries(nTweaks);
            int twApplied = 0;
            for (int i = 0; i < nTweaks && i < (int)m_tweakCurrentIdx.size(); ++i)
            {
                const TweakEntry& e = tweaks[i];
                if (e.kind == TweakKind::SectionHeader) continue;
                int ci = m_tweakCurrentIdx[i];
                if (ci <= 0) continue;  // DEFAULT — nothing to do
                if (ci >= (int)e.cycleValues.size()) continue;
                int newVal = e.cycleValues[ci];
                switch (e.kind)
                {
                    case TweakKind::TweakRow:
                        applyFieldTweak(e.fieldName, e.isFloat, e.isMultiplier,
                                        e.requiresOriginalGt1, e.rowStructFilter,
                                        newVal, /*useDefault=*/false);
                        break;
                    case TweakKind::SpecialNoCost:        applyNoCostRecipe(false); break;
                    case TweakKind::SpecialInstantCraft:  applyInstantCraft(false); break;
                    default: break;
                }
                updateTweakRowUI(i);
                twApplied++;
            }
            if (twApplied > 0) VLOG(STR("[Tweaks] Re-applied {} tweaks from INI\n"), twApplied);
        }

        // v6.4.1 — Buff refresh tick. Re-applies every toggled-on buff every BUFF_REFRESH_MS
        // milliseconds so duration-based effects (HasDuration policy) never expire naturally.
        // Called from the main tick; cheap no-op when no buffs are active.
        // v6.6.0 fix — gate on m_characterLoaded so the timer doesn't tick before world
        // load. m_buffStates is populated from MoriaCppMod.ini at startup (loadConfig
        // reads [Cheats] Buff_* entries), which made the timer burn its 30s window on
        // applyGEByName calls into a void with no ASC. Early-return *before* the timer
        // check so the first post-load tick fires immediately.
        ULONGLONG m_lastBuffRefresh{0};
        static constexpr ULONGLONG BUFF_REFRESH_MS = 30000;  // every 30 seconds
        void refreshActiveBuffs()
        {
            if (!m_characterLoaded) return;
            if (m_buffStates.empty()) return;
            ULONGLONG now = GetTickCount64();
            if (now - m_lastBuffRefresh < BUFF_REFRESH_MS) return;
            m_lastBuffRefresh = now;

            int count = 0;
            const CheatEntry* all = cheatEntries(count);
            int refreshed = 0;
            for (int i = 0; i < count && i < (int)m_buffStates.size(); ++i)
            {
                if (!m_buffStates[i]) continue;
                if (all[i].kind != CheatRowKind::BuffToggle) continue;
                if (all[i].effect1) { applyGEByName(all[i].effect1); refreshed++; }
                if (all[i].effect2) { applyGEByName(all[i].effect2); refreshed++; }
            }
            if (refreshed > 0)
                VLOG(STR("[Buff] Refreshed {} active effects (periodic tick)\n"), refreshed);
        }

        // Clear All Buffs action — applies GE_DebugRemoveBuffs + GE_ClearFoodBuffs, then uncheck
        // every toggle on the Cheats tab and refresh their UI.
        void clearAllBuffs()
        {
            applyGEByName(STR("GE_DebugRemoveBuffs"));
            applyGEByName(STR("GE_ClearFoodBuffs"));

            int count = 0;
            const CheatEntry* all = cheatEntries(count);
            for (int i = 0; i < count; ++i)
            {
                if (all[i].kind != CheatRowKind::BuffToggle) continue;
                if (!m_buffStates[i]) continue;
                // Also explicitly remove each individual effect (belt + suspenders, in case debug removal misses some)
                if (all[i].effect1) removeGEByName(all[i].effect1);
                if (all[i].effect2) removeGEByName(all[i].effect2);
                m_buffStates[i] = false;
                updateBuffRowUI(i);
            }
            VLOG(STR("[Cheats] Clear All Buffs — all toggles reset\n"));
            showOnScreen(L"All buffs cleared", 3.0f, 0.3f, 1.0f, 0.3f);
            saveConfig();  // v6.4.4+ — persist to MoriaCppMod.ini
        }


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

        // v6.4.1 Peace Mode — toggle AMorAISpawnManager.MaxSpawnLimit between its saved-original
        // value and 0. When enabled: no new orcs/goblins/trolls spawn. Existing enemies remain.
        // When disabled: spawn cap restored. State persists across F12 open/close.
        void togglePeaceMode()
        {
            std::vector<UObject*> mgrs;
            UObjectGlobals::FindAllOf(STR("MorAISpawnManager"), mgrs);
            if (mgrs.empty())
            {
                VLOG(STR("[PeaceMode] MorAISpawnManager not found — load a world first\n"));
                showOnScreen(L"Load a world first", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            UObject* mgr = mgrs[0];
            if (!isObjectAlive(mgr)) return;

            auto* prop = mgr->GetPropertyByNameInChain(STR("MaxSpawnLimit"));
            if (!prop)
            {
                VLOG(STR("[PeaceMode] MaxSpawnLimit property not found\n"));
                showOnScreen(L"MaxSpawnLimit not found", 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            uint8_t* base = reinterpret_cast<uint8_t*>(mgr) + prop->GetOffset_Internal();
            if (!isReadableMemory(base, sizeof(float))) return;
            float* cur = reinterpret_cast<float*>(base);

            m_peaceModeEnabled = !m_peaceModeEnabled;

            if (m_peaceModeEnabled)
            {
                if (m_savedMaxSpawnLimit < 0.0f) m_savedMaxSpawnLimit = *cur;
                *cur = 0.0f;
                VLOG(STR("[PeaceMode] ENABLED — MaxSpawnLimit {} -> 0\n"), m_savedMaxSpawnLimit);
                showOnScreen(L"Peace Mode ON", 3.0f, 0.3f, 1.0f, 0.3f);
            }
            else
            {
                float restoreTo = (m_savedMaxSpawnLimit >= 0.0f) ? m_savedMaxSpawnLimit : 50.0f;
                *cur = restoreTo;
                VLOG(STR("[PeaceMode] DISABLED — MaxSpawnLimit -> {}\n"), restoreTo);
                showOnScreen(L"Peace Mode OFF", 3.0f, 1.0f, 0.5f, 0.2f);
            }

            updateFtPeaceMode();
            saveConfig();  // v6.4.4+ — persist to MoriaCppMod.ini
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

            // Phase 7: Goals screen — WBP_GoalsScreen_C::MarkAllAsRead() (one-shot, covers goals,
            // mysteries, tutorials, tips categories natively). Must be called in addition to Phase 2's
            // per-entry SetLoreEntryViewed because MarkAllAsRead may persist differently.
            {
                std::vector<UObject*> screens;
                UObjectGlobals::FindAllOf(STR("WBP_GoalsScreen_C"), screens);
                int invoked = 0;
                for (UObject* screen : screens)
                {
                    if (!screen || !isObjectAlive(screen)) continue;
                    auto* fn = screen->GetFunctionByNameInChain(STR("MarkAllAsRead"));
                    if (fn && safeProcessEvent(screen, fn, nullptr))
                    {
                        anyScreenFound = true;
                        invoked++;
                        VLOG(STR("[MarkRead] Phase 7: WBP_GoalsScreen_C::MarkAllAsRead invoked on {}\n"),
                             screen->GetName());
                    }
                }
                if (invoked == 0)
                {
                    VLOG(STR("[MarkRead] Phase 7: No WBP_GoalsScreen_C instance — open Goals menu once\n"));
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

            // v6.20.15 — Phase 5: chain a save so the read state actually
            // persists across reloads. The BP MarkAllRead /
            // SetLoreEntryViewed calls flip in-memory ViewModel flags but
            // only the next save round-trip serializes them durably.
            // Without an explicit save, the player's read-state reverts
            // on next reload.
            //
            // v6.20.16 — Increased delay from 1s to 3s. UMorLoreScreen's
            // C++ SetLoreEntryViewed/SetTipEntryViewed/SetTutorialEntryViewed
            // are all EMPTY stubs (verified against UHT MorLoreScreen.cpp);
            // the actual storage write is BP-side in WBP_LoreScreen_v2_C
            // and may iterate hundreds of entries per frame. 3s gives the
            // BP enough headroom to finish writing before save. Also
            // bypasses the 10s save cooldown so the save actually fires.
            m_lastSaveTime = 0;
            m_saveAfterMarkAtMs = GetTickCount64() + 3000ull;
        }

        // v6.20.15 — Phase 5: deferred save trigger after markAllLoreRead.
        // Polled from gameThreadTick (see tickSaveAfterMarkRead below).
        uint64_t m_saveAfterMarkAtMs{0};
        void tickSaveAfterMarkRead()
        {
            if (m_saveAfterMarkAtMs == 0) return;
            uint64_t now = GetTickCount64();
            if (now < m_saveAfterMarkAtMs) return;
            m_saveAfterMarkAtMs = 0;
            VLOG(STR("[MarkRead] Triggering deferred save to persist read-state\n"));
            triggerSaveGame();
        }

        // v6.4.5+ — Reveal map: mark every zone discovered + push chapter discovery
        // to every minimap widget. No mapstone spawning, no waypoint discovery — those
        // didn't survive cross-bubble streaming. Bound to NUM* (VK_MULTIPLY).
        void revealEntireMap()
        {
            VLOG(STR("[RevealMap] revealEntireMap() invoked\n"));

            // 1. Find UMorDatabase singleton (holds DataTable pointers as private UPROPERTYs)
            UObject* morDb = nullptr;
            {
                std::vector<UObject*> dbs;
                UObjectGlobals::FindAllOf(STR("MorDatabase"), dbs);
                for (auto* d : dbs) { if (d && isObjectAlive(d)) { morDb = d; break; } }
            }
            if (!morDb) {
                VLOG(STR("[RevealMap] UMorDatabase not found — bail\n"));
                showOnScreen(L"Reveal Map: database not found", 3.0f, 1.0f, 0.4f, 0.4f);
                return;
            }

            // 2. Pull ZonesTable pointer off the database via reflection
            UObject* zonesDt = nullptr;
            {
                auto** ptr = morDb->GetValuePtrByPropertyNameInChain<UObject*>(STR("ZonesTable"));
                if (ptr && *ptr && isObjectAlive(*ptr)) zonesDt = *ptr;
            }

            // 3. Push every zone row into AMorMinimapManager::SetStartingDiscoveredZones
            int minimapZonesPushed = 0;
            if (zonesDt)
            {
                UObject* mmMgr = nullptr;
                std::vector<UObject*> mgrs;
                UObjectGlobals::FindAllOf(STR("MorMinimapManager"), mgrs);
                for (auto* m : mgrs) { if (m && isObjectAlive(m)) { mmMgr = m; break; } }

                if (mmMgr)
                {
                    DataTableUtil zt;
                    if (zt.bindFromObject(zonesDt, L"ZonesTable"))
                    {
                        auto raw = zt.getRowNamesRaw();
                        const int HANDLE_SIZE = 16;  // FMorZoneRowHandle: {UDataTable*, FName RowName}
                        std::vector<uint8_t> pack(raw.size() * HANDLE_SIZE, 0);
                        for (size_t i = 0; i < raw.size(); i++)
                        {
                            uint8_t* slot = pack.data() + i * HANDLE_SIZE;
                            *reinterpret_cast<UObject**>(slot + 0) = zonesDt;
                            std::memcpy(slot + 8, &raw[i], sizeof(FName));
                        }

                        struct FakeTArray { void* Data; int32_t Num; int32_t Max; };
                        FakeTArray arr{ pack.data(),
                                        static_cast<int32_t>(raw.size()),
                                        static_cast<int32_t>(raw.size()) };

                        auto* setStartFn = mmMgr->GetFunctionByNameInChain(STR("SetStartingDiscoveredZones"));
                        if (setStartFn)
                        {
                            int sz = setStartFn->GetParmsSize();
                            std::vector<uint8_t> buf(sz, 0);
                            auto* pArr = findParam(setStartFn, STR("DiscoveredZones"));
                            int arrOff = pArr ? pArr->GetOffset_Internal() : 0;
                            std::memcpy(buf.data() + arrOff, &arr, sizeof(arr));
                            if (safeProcessEvent(mmMgr, setStartFn, buf.data()))
                                minimapZonesPushed = static_cast<int>(raw.size());
                        }
                    }
                }
            }
            VLOG(STR("[RevealMap] Zones discovered: {}\n"), minimapZonesPushed);

            // 4. Call DiscoverAllChapters on every active minimap widget
            int chaptersDiscovered = 0;
            {
                std::vector<UObject*> widgets;
                UObjectGlobals::FindAllOf(STR("MorGameMinimapWidget"), widgets);
                for (auto* w : widgets)
                {
                    if (!w || !isObjectAlive(w)) continue;
                    auto* fn = w->GetFunctionByNameInChain(STR("DiscoverAllChapters"));
                    if (fn && safeProcessEvent(w, fn, nullptr))
                        chaptersDiscovered++;
                }
            }
            VLOG(STR("[RevealMap] DiscoverAllChapters called on {} minimap widgets\n"), chaptersDiscovered);

            wchar_t summary[128];
            swprintf(summary, 128, L"Map revealed: %d zones, %d chapters", minimapZonesPushed, chaptersDiscovered);
            VLOG(STR("[RevealMap] DONE — {}\n"), std::wstring(summary));
            showOnScreen(summary, 4.0f, 0.3f, 1.0f, 0.5f);
        }
