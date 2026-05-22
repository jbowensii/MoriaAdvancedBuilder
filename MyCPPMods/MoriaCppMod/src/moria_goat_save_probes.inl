// moria_goat_save_probes.inl — Phase 2 diagnostic probes (A–E) for porter
// goat save-system integration research. Build: v7.1.0-rc.52 / v1.4.1-probe-rc.1.
//
// READ-ONLY. No mutation. Gated behind [GoatSaveProbes] Enabled=true in
// MoriaCppMod.ini. When the section is absent or Enabled=false, every probe
// is an early-out — zero PE-hot-path cost.
//
// The five probes answer this hierarchy of questions, in order:
//
//   A. Does the world save already contain our goat without intervention?
//      → walk AMorNPCManager.NpcInfo on character-load and on save events
//
//   B. Why is/isn't our goat in NpcInfo? Is there a registration API we
//      can call? Is BP_NpcGoat_C in ValidNpcClasses? What does the goat's
//      class hierarchy expose?
//      → one-shot enumeration at startup
//
//   C. Can we hook the save lifecycle via ProcessEvent?
//      → global PE post-callback whitelisting save-shaped function names,
//        dedup'd per (fnName | ctxClass) pair
//
//   D. Does Moria already have a USaveGame subclass with flexible storage
//      we could repurpose?
//      → ForEachUObject filtered to USaveGame descendants
//
//   E. Can we discover the world-save slot name / file path at runtime so
//      a co-located blob file is viable?
//      → global PE pre-callback reading FString args from save-shaped
//        UFunctions
//
// Probes A and the periodic snapshot are throttled by tickGoatSaveProbes()
// (1 s minimum cadence, 10 s snapshot interval). Probes B+D run once, with
// a 5 s startup grace so the manager and base classes are loaded.

        bool m_goatSaveProbesEnabled{false};
        ULONGLONG m_probesStartupAtMs{0};
        ULONGLONG m_probesLastTickMs{0};
        ULONGLONG m_probesLastSnapMs{0};
        bool m_probesProbeBDone{false};      // gates section (manager-only) — fires once manager is alive
        bool m_probesProbeBGoatDone{false};  // goat hierarchy section — fires once first live goat is found
        bool m_probesProbeDDone{false};
        bool m_probesProbeGDone{false};      // save-participant analysis — one-shot
        bool m_probesProbeIDone{false};      // AIController DynamicBehaviors dump — one-shot per NUM*
        bool m_probesProbeJDone{false};      // Persistor class verify — one-shot per NUM*
        bool m_probesProbeKDone{false};      // Blackboard keys + FSM state — one-shot per NUM*
        std::unordered_set<std::wstring> m_probesSeenC;
        std::unordered_set<std::wstring> m_probesSeenE;
        std::unordered_set<std::wstring> m_probesSeenF;
        std::unordered_set<std::wstring> m_probesSeenH;  // (fnName|ctxCls) dedup for Interaction delegates
        std::unordered_set<std::wstring> m_probesSeenPersistorWatch;

        // FGuid pretty-print (registry-style, 4×u32). Bytes assumed readable.
        static std::wstring probesFormatGuid(const uint8_t* g)
        {
            if (!g) return std::wstring(STR("<null>"));
            uint32_t a = *reinterpret_cast<const uint32_t*>(g + 0);
            uint32_t b = *reinterpret_cast<const uint32_t*>(g + 4);
            uint32_t c = *reinterpret_cast<const uint32_t*>(g + 8);
            uint32_t d = *reinterpret_cast<const uint32_t*>(g + 12);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%08X-%08X-%08X-%08X", a, b, c, d);
            return std::wstring(buf);
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE A — walk AMorNPCManager.NpcInfo.Items
        //
        // Layout (mirrors scanNpcInfoForCantReach in moria_npc_recovery.inl):
        //   AMorNPCManager.NpcInfo           → +0x03A0 (FMorNPCInfoArray)
        //   FMorNPCInfoArray.Items           → +0x0108 (TArray<FMorNPCInfo>)
        //   TArray.Data / Num / Max          → +0x00 / +0x08 / +0x0C
        //   FMorNPCInfo stride               → 0x0260
        //   FMorNPCInfo.PersistentData       → +0x0010 (FMorNpcPersistentData)
        //     .NpcGuid                       → absolute +0x001C (FGuid 16B)
        //     .CurrentRole (RowHandle)       → absolute +0x0048 (DataTable* +0, FName +8)
        //       .RowName                     → absolute +0x0050 (FName 8B)
        //   FMorNPCInfo.CurrentActivity      → +0x0210 (RowName @ +0x0218)
        //   FMorNPCInfo.InterruptedActivity  → +0x0220 (RowName @ +0x0228)
        // ──────────────────────────────────────────────────────────────────
        void runProbeA_walkNpcInfo(const wchar_t* phaseLabel)
        {
            if (!m_goatSaveProbesEnabled) return;
            UObject* mgr = getOrFindNpcManager();
            if (!mgr) {
                VLOG(STR("[ProbeA] {} mgr=null (manager not alive yet)\n"), phaseLabel);
                return;
            }
            const uint8_t* mgrBase = reinterpret_cast<const uint8_t*>(mgr);
            const uint8_t* arrayHeader = mgrBase + 0x03A0 + 0x0108;
            if (!isReadableMemory(arrayHeader, 16)) {
                VLOG(STR("[ProbeA] {} NpcInfo TArray header unreadable\n"), phaseLabel);
                return;
            }
            uint8_t* itemsData = *reinterpret_cast<uint8_t* const*>(arrayHeader + 0x00);
            int32 itemsNum     = *reinterpret_cast<const int32*>(arrayHeader + 0x08);
            VLOG(STR("[ProbeA] {} NpcInfo.Num={} dataPtr={:p}\n"),
                 phaseLabel, itemsNum, (void*)itemsData);
            if (!itemsData || itemsNum <= 0 || itemsNum > 1024) return;

            constexpr int32 kItemStride = 0x0260;
            constexpr int32 kOffNpcGuid = 0x001C;
            constexpr int32 kOffRoleRow = 0x0050;
            constexpr int32 kOffCurAct  = 0x0218;
            constexpr int32 kOffIntAct  = 0x0228;

            for (int32 i = 0; i < itemsNum; ++i)
            {
                uint8_t* item = itemsData + (int64_t)i * kItemStride;
                if (!isReadableMemory(item, kItemStride)) continue;

                std::wstring guidStr = probesFormatGuid(item + kOffNpcGuid);
                std::wstring roleName = seh_fnameToString(item + kOffRoleRow);
                std::wstring curAct   = seh_fnameToString(item + kOffCurAct);
                std::wstring intAct   = seh_fnameToString(item + kOffIntAct);

                VLOG(STR("[ProbeA] {} [{}] guid={} role={} curAct={} intAct={}\n"),
                     phaseLabel, i, guidStr.c_str(),
                     roleName.empty() ? STR("?") : roleName.c_str(),
                     curAct.empty()   ? STR("?") : curAct.c_str(),
                     intAct.empty()   ? STR("?") : intAct.c_str());
            }
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE B — class whitelist + registration interface enumeration
        //
        // Reads three filter arrays on AMorNPCManager:
        //   ValidNpcClasses   @ +0x0808  TArray<FSoftClassPath>
        //   ValidNpcRestores  @ +0x0818  TArray<FSoftObjectPath>
        //   ValidNpcRoles     @ +0x0828  TArray<FName>
        //
        // Then tests candidate UFunction names on the manager and on a live
        // UMorNPCComponent to discover any "register/add/save" entry points
        // we could call to opt our goat into the save graph.
        //
        // Finally walks the BP_NpcGoat_C super chain so we can see the
        // inheritance hierarchy (and any IMorSaveable / IFGKSaveable parent
        // class we may inherit for free).
        // ──────────────────────────────────────────────────────────────────
        void runProbeB_enumerateGates()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeBDone) return;
            UObject* mgr = getOrFindNpcManager();
            if (!mgr) {
                // Don't mark done — try again next eligible tick.
                return;
            }
            m_probesProbeBDone = true;
            const uint8_t* mgrBase = reinterpret_cast<const uint8_t*>(mgr);

            // Dump SaveGameObjectId (struct size 0x20 at +0x0790) as raw bytes.
            if (isReadableMemory(mgrBase + 0x0790, 0x20)) {
                const uint8_t* s = mgrBase + 0x0790;
                wchar_t buf[160];
                swprintf_s(buf, 160,
                    L"%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x "
                    L"%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                    s[0],s[1],s[2],s[3], s[4],s[5],s[6],s[7],
                    s[8],s[9],s[10],s[11], s[12],s[13],s[14],s[15],
                    s[16],s[17],s[18],s[19], s[20],s[21],s[22],s[23],
                    s[24],s[25],s[26],s[27], s[28],s[29],s[30],s[31]);
                VLOG(STR("[ProbeB] AMorNPCManager.SaveGameObjectId(32B) = {}\n"), buf);
            }

            // Helper to dump a TArray<FSoftObjectPath>-shape array (stride 0x18).
            auto dumpSoftArray = [&](const wchar_t* label, int32 offMgr, bool isClassArray) {
                if (!isReadableMemory(mgrBase + offMgr, 16)) {
                    VLOG(STR("[ProbeB] {} unreadable at +0x{:x}\n"), label, offMgr);
                    return;
                }
                uint8_t* dataPtr = *reinterpret_cast<uint8_t* const*>(mgrBase + offMgr + 0);
                int32 num        = *reinterpret_cast<const int32*>(mgrBase + offMgr + 8);
                VLOG(STR("[ProbeB] {}.Num={} dataPtr={:p}\n"), label, num, (void*)dataPtr);
                if (!dataPtr || num <= 0 || num > 256) return;
                constexpr int32 stride = 0x18; // FName(8) + FString(16)
                bool sawGoat = false;
                for (int32 i = 0; i < num; ++i) {
                    uint8_t* entry = dataPtr + (int64_t)i * stride;
                    if (!isReadableMemory(entry, stride)) continue;
                    std::wstring p = seh_fnameToString(entry);
                    VLOG(STR("[ProbeB] {} [{}] = {}\n"), label, i, p.empty() ? STR("?") : p.c_str());
                    if (p.find(STR("BP_NpcGoat")) != std::wstring::npos) sawGoat = true;
                }
                if (isClassArray) {
                    VLOG(STR("[ProbeB] BP_NpcGoat present in {}: {}\n"),
                         label, sawGoat ? STR("YES") : STR("NO"));
                }
            };

            dumpSoftArray(STR("ValidNpcClasses"),  0x0808, true);
            dumpSoftArray(STR("ValidNpcRestores"), 0x0818, false);

            // ValidNpcRoles is TArray<FName> — 8-byte stride.
            if (isReadableMemory(mgrBase + 0x0828, 16)) {
                uint8_t* dataPtr = *reinterpret_cast<uint8_t* const*>(mgrBase + 0x0828 + 0);
                int32 num        = *reinterpret_cast<const int32*>(mgrBase + 0x0828 + 8);
                VLOG(STR("[ProbeB] ValidNpcRoles.Num={} dataPtr={:p}\n"), num, (void*)dataPtr);
                if (dataPtr && num > 0 && num < 256) {
                    for (int32 i = 0; i < num; ++i) {
                        uint8_t* entry = dataPtr + (int64_t)i * 8;
                        if (!isReadableMemory(entry, 8)) continue;
                        std::wstring n = seh_fnameToString(entry);
                        VLOG(STR("[ProbeB] ValidNpcRoles [{}] = {}\n"),
                             i, n.empty() ? STR("?") : n.c_str());
                    }
                }
            }

            // Candidate registration UFunctions on the manager.
            const wchar_t* mgrCandidates[] = {
                STR("RegisterSaveGameObject"), STR("RequestSaveGameObjectId"),
                STR("AddSaveParticipant"),
                STR("AddNpc"), STR("RegisterNpc"), STR("CreatePersistentNpcEntry"),
                STR("AddPersistentNpc"), STR("CreateNpcEntry"),
                STR("RegisterCharacter"), STR("AddCharacter"),
                STR("AddValidNpcClass"), STR("RegisterValidNpcClass"),
                STR("RecruitNpc"), STR("RestoreNpc"), STR("RestoreNpcFromSave"),
                STR("AddNpcToInfo"), STR("AddPersistentData"),
                STR("SpawnNpc"), STR("MarkNpcInfoDirty"),
                STR("SaveGameObjectPreStore"), STR("SaveGameObjectPostStore"),
                STR("SaveGameObjectPreRestore"), STR("SaveGameObjectPostRestore"),
                nullptr
            };
            for (const wchar_t** p = mgrCandidates; *p; ++p) {
                RC::Unreal::UFunction* fn = nullptr;
                try {
                    fn = mgr->GetFunctionByNameInChain(*p);
                } catch (...) {}
                if (!fn) continue;
                VLOG(STR("[ProbeB] AMorNPCManager UFunction PRESENT: {}\n"), *p);
                int parmCount = 0;
                try {
                    for (auto* prop : fn->ForEachProperty()) {
                        std::wstring pname, ptype;
                        try { pname = prop->GetName(); } catch (...) {}
                        try { ptype = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[ProbeB]   .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(),
                             prop->GetOffset_Internal());
                        if (++parmCount > 16) { VLOG(STR("[ProbeB]   ...(truncated)\n")); break; }
                    }
                } catch (...) {}
            }

            // World-state save participant — useful for Probe C hooking too.
            std::vector<UObject*> wsObjs;
            findAllOfSafe(STR("MorSaveSystemWorldState"), wsObjs);
            for (UObject* o : wsObjs) {
                if (!o || !isObjectAlive(o)) continue;
                std::wstring cn = safeClassName(o);
                if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                VLOG(STR("[ProbeB] live MorSaveSystemWorldState={:p} class={}\n"),
                     (void*)o, cn.c_str());
                // Try to find candidate save UFunctions on it
                const wchar_t* wsCandidates[] = {
                    STR("StartSaveGame"), STR("FinishSaveGame"), STR("SaveGame"),
                    STR("OnWorldStateIsReady"), STR("PostWorldStateIsReady"),
                    STR("GetSaveSlotName"), STR("GetCurrentWorldSlot"),
                    STR("GetWorldName"), STR("GetWorldGuid"),
                    nullptr
                };
                for (const wchar_t** p = wsCandidates; *p; ++p) {
                    RC::Unreal::UFunction* fn = nullptr;
                    try {
                        fn = o->GetFunctionByNameInChain(*p);
                    } catch (...) {}
                    if (fn) VLOG(STR("[ProbeB] MorSaveSystemWorldState UFunction PRESENT: {}\n"), *p);
                }
                break;  // one sample is enough
            }

            VLOG(STR("[ProbeB] gates section complete (goat hierarchy deferred until a live BP_NpcGoat_C is found)\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE B (goat section) — deferred until first live BP_NpcGoat_C.
        // The user typically summons the goat several minutes after load,
        // so we keep retrying every tick until we catch one. Fires exactly
        // once, then sets m_probesProbeBGoatDone=true.
        // ──────────────────────────────────────────────────────────────────
        void runProbeB_goatHierarchy()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeBGoatDone) return;

            std::vector<UObject*> goats;
            findAllOfSafe(STR("BP_NpcGoat_C"), goats);
            UObject* sampleGoat = nullptr;
            for (UObject* g : goats) {
                if (!g || !isObjectAlive(g)) continue;
                std::wstring cn = safeClassName(g);
                if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                sampleGoat = g; break;
            }
            if (!sampleGoat) return;  // retry next tick
            m_probesProbeBGoatDone = true;

            VLOG(STR("[ProbeB-goat] sample BP_NpcGoat_C={:p}\n"), (void*)sampleGoat);
            RC::Unreal::UClass* c = nullptr;
            try { c = sampleGoat->GetClassPrivate(); } catch (...) {}
            int depth = 0;
            while (c && depth < 16) {
                std::wstring cname;
                try { cname = c->GetName(); } catch (...) {}
                VLOG(STR("[ProbeB-goat]   class[{}]: {}\n"), depth, cname.c_str());
                RC::Unreal::UStruct* sup = nullptr;
                try { sup = c->GetSuperStruct(); } catch (...) {}
                c = static_cast<RC::Unreal::UClass*>(sup);
                ++depth;
            }

            // Save-shaped UFunctions on the goat itself.
            const wchar_t* pawnCandidates[] = {
                STR("Serialize"), STR("PreSave"), STR("PostLoad"),
                STR("OnSave"), STR("OnRestore"),
                STR("SaveGameObjectPreStore"), STR("SaveGameObjectPostStore"),
                STR("SaveGameObjectPreRestore"), STR("SaveGameObjectPostRestore"),
                STR("GetSaveGameObjectId"), STR("SetSaveGameObjectId"),
                nullptr
            };
            for (const wchar_t** p = pawnCandidates; *p; ++p) {
                RC::Unreal::UFunction* fn = nullptr;
                try { fn = sampleGoat->GetFunctionByNameInChain(*p); } catch (...) {}
                if (fn) VLOG(STR("[ProbeB-goat] BP_NpcGoat_C UFunction PRESENT: {}\n"), *p);
            }

            // The goat's UMorNPCComponent (preferred over picking a random one
            // — this one belongs to OUR pawn, so any inherited save interface
            // here is the one that matters for our case).
            UObject* npcCmp = nullptr;
            try {
                if (auto* p = sampleGoat->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"))) {
                    if (*p && isObjectAlive(*p)) npcCmp = *p;
                }
            } catch (...) {}
            if (npcCmp) {
                VLOG(STR("[ProbeB-goat] goat->NPC component={:p} class={}\n"),
                     (void*)npcCmp, safeClassName(npcCmp).c_str());
                const wchar_t* cmpCandidates[] = {
                    STR("Serialize"), STR("PreSave"), STR("PostLoad"),
                    STR("OnSave"), STR("OnRestore"), STR("OnRep_NpcInfo"),
                    STR("GetPersistentData"), STR("SetPersistentData"),
                    STR("SaveGameObjectPreStore"), STR("SaveGameObjectPostStore"),
                    STR("SaveGameObjectPreRestore"), STR("SaveGameObjectPostRestore"),
                    STR("MarkRepDirty"), STR("MarkArrayDirty"),
                    STR("GetNpcGuid"), STR("GetCurrentRole"),
                    nullptr
                };
                for (const wchar_t** p = cmpCandidates; *p; ++p) {
                    RC::Unreal::UFunction* fn = nullptr;
                    try { fn = npcCmp->GetFunctionByNameInChain(*p); } catch (...) {}
                    if (fn) VLOG(STR("[ProbeB-goat] MorNPCComponent UFunction PRESENT: {}\n"), *p);
                }

                // Walk the NPC component class hierarchy too — any base
                // implements IFGKSaveable etc.?
                RC::Unreal::UClass* nc = nullptr;
                try { nc = npcCmp->GetClassPrivate(); } catch (...) {}
                int nd = 0;
                while (nc && nd < 16) {
                    std::wstring cname;
                    try { cname = nc->GetName(); } catch (...) {}
                    VLOG(STR("[ProbeB-goat]   NPCcmp class[{}]: {}\n"), nd, cname.c_str());
                    RC::Unreal::UStruct* sup = nullptr;
                    try { sup = nc->GetSuperStruct(); } catch (...) {}
                    nc = static_cast<RC::Unreal::UClass*>(sup);
                    ++nd;
                }
            } else {
                VLOG(STR("[ProbeB-goat] goat->NPC component not yet resolvable\n"));
            }

            // After we have the goat, force an immediate NpcInfo snapshot
            // so we can see whether the goat made it into the manager array.
            runProbeA_walkNpcInfo(STR("post-goat-summon"));

            VLOG(STR("[ProbeB-goat] complete\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE C — global PE post-callback whitelisting save-shaped names.
        //
        // Co-located with the existing PE post-hook in dllmain.cpp (called
        // from there). Cheap substring filter FIRST per
        // feedback_filter_pe_by_function_name_first.md, then safeClassName.
        // Dedups by (fnName | ctxCls) so repeated firings don't bury the log.
        // ──────────────────────────────────────────────────────────────────
        void onProbeC_processEventPost(UObject* obj, RC::Unreal::UFunction* func)
        {
            if (!m_goatSaveProbesEnabled || !func) return;
            const auto fname = func->GetName();
            const wchar_t* fnStr = fname.c_str();
            bool matches =
                (wcsstr(fnStr, STR("WorldState"))  != nullptr) ||
                (wcsstr(fnStr, STR("PreStore"))    != nullptr) ||
                (wcsstr(fnStr, STR("PostStore"))   != nullptr) ||
                (wcsstr(fnStr, STR("PreRestore"))  != nullptr) ||
                (wcsstr(fnStr, STR("PostRestore")) != nullptr) ||
                (wcsstr(fnStr, STR("OnSave"))      != nullptr) ||
                (wcsstr(fnStr, STR("OnLoad"))      != nullptr) ||
                (wcsstr(fnStr, STR("SaveSlot"))    != nullptr) ||
                (wcsstr(fnStr, STR("SaveGame"))    != nullptr) ||
                (wcsstr(fnStr, STR("OnRep_NpcInfo")) != nullptr);
            if (!matches) return;

            std::wstring ctxCls = obj ? safeClassName(obj) : std::wstring(STR("<null>"));
            std::wstring key = std::wstring(fnStr) + STR("|") + ctxCls;
            if (m_probesSeenC.size() > 4096) m_probesSeenC.clear();
            if (m_probesSeenC.find(key) != m_probesSeenC.end()) return;
            m_probesSeenC.insert(key);
            VLOG(STR("[ProbeC] fn={} ctx_cls={}\n"), fnStr, ctxCls.c_str());

            // Save-lifecycle events: opportunistically snapshot NpcInfo.
            if (wcsstr(fnStr, STR("WorldStateIsReady")) ||
                wcsstr(fnStr, STR("PostRestore"))       ||
                wcsstr(fnStr, STR("PreStore"))          ||
                wcsstr(fnStr, STR("OnRep_NpcInfo")))
            {
                runProbeA_walkNpcInfo(fnStr);
            }
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE D — enumerate all loaded UClasses descending from USaveGame.
        // Dumps each subclass's UPROPERTY list (capped at 24 per class).
        // ──────────────────────────────────────────────────────────────────
        void runProbeD_findSaveGameSubclasses()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeDDone) return;
            m_probesProbeDDone = true;

            RC::Unreal::UClass* base = nullptr;
            try {
                base = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Engine.SaveGame"));
            } catch (...) {}
            if (!base) {
                VLOG(STR("[ProbeD] /Script/Engine.SaveGame not found\n"));
                return;
            }
            VLOG(STR("[ProbeD] USaveGame base={:p}\n"), (void*)base);

            int count = 0;
            UObjectGlobals::ForEachUObject([&](UObject* o, int32_t, int32_t) -> LoopAction {
                if (!o) return LoopAction::Continue;
                RC::Unreal::UClass* objClass = nullptr;
                try { objClass = o->GetClassPrivate(); } catch (...) { return LoopAction::Continue; }
                if (!objClass) return LoopAction::Continue;
                std::wstring cname;
                try { cname = objClass->GetName(); } catch (...) { return LoopAction::Continue; }
                bool isClassLike =
                    (cname == STR("Class") ||
                     cname == STR("BlueprintGeneratedClass") ||
                     cname == STR("DynamicClass"));
                if (!isClassLike) return LoopAction::Continue;

                RC::Unreal::UStruct* asS = static_cast<RC::Unreal::UStruct*>(o);
                bool isSG = false;
                for (RC::Unreal::UStruct* s = asS; s; ) {
                    if (s == base) { isSG = true; break; }
                    RC::Unreal::UStruct* sup = nullptr;
                    try { sup = s->GetSuperStruct(); } catch (...) { break; }
                    s = sup;
                }
                if (!isSG) return LoopAction::Continue;

                std::wstring name;
                try { name = o->GetName(); } catch (...) {}
                VLOG(STR("[ProbeD] USaveGame subclass: {}\n"),
                     name.empty() ? STR("?") : name.c_str());

                // PATH 2 FIX: walk full super chain so inherited properties
                // are visible (MorWorldSaveGame's own UPROPERTYs may all live
                // on MorSaveGame/MorAccountSaveGame parents — the rc.52 probe
                // missed them because it only iterated own-class properties).
                int totalProps = 0;
                RC::Unreal::UStruct* curStruct = asS;
                int chainDepth = 0;
                while (curStruct && chainDepth < 8 && totalProps < 64) {
                    std::wstring cn;
                    try { cn = curStruct->GetName(); } catch (...) {}
                    VLOG(STR("[ProbeD]   --- props from {} ---\n"),
                         cn.empty() ? STR("?") : cn.c_str());
                    int propN = 0;
                    try {
                        for (auto* prop : curStruct->ForEachProperty()) {
                            std::wstring pname, ptype;
                            try { pname = prop->GetName(); } catch (...) {}
                            try { ptype = prop->GetClass().GetName(); } catch (...) {}
                            VLOG(STR("[ProbeD]     .{} : {} @ +0x{:x}\n"),
                                 pname.empty() ? STR("?") : pname.c_str(),
                                 ptype.empty() ? STR("?") : ptype.c_str(),
                                 prop->GetOffset_Internal());
                            ++propN;
                            ++totalProps;
                            if (propN > 20) {
                                VLOG(STR("[ProbeD]     ...(truncated at level)\n"));
                                break;
                            }
                        }
                    } catch (...) {}
                    if (curStruct == base) break;  // stop at USaveGame; UObject props irrelevant
                    RC::Unreal::UStruct* sup = nullptr;
                    try { sup = curStruct->GetSuperStruct(); } catch (...) { break; }
                    if (!sup || sup == curStruct) break;
                    curStruct = sup;
                    ++chainDepth;
                }
                ++count;
                return LoopAction::Continue;
            });
            VLOG(STR("[ProbeD] complete; found {} USaveGame subclasses\n"), count);
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE E — global PE pre-callback. Reads FString / FName / int args
        // from save-shaped UFunctions to discover slot names / file paths.
        //
        // Co-located with the existing PE pre-hook in dllmain.cpp. Cheap
        // substring filter first; dedup'd per (fnName | ctxCls).
        // ──────────────────────────────────────────────────────────────────
        void onProbeE_processEventPre(UObject* obj, RC::Unreal::UFunction* func, void* parms)
        {
            if (!m_goatSaveProbesEnabled || !func || !parms) return;
            const auto fname = func->GetName();
            const wchar_t* fnStr = fname.c_str();
            bool matches =
                (wcsstr(fnStr, STR("SaveGame"))   != nullptr) ||
                (wcsstr(fnStr, STR("Slot"))       != nullptr) ||
                (wcsstr(fnStr, STR("SaveWorld"))  != nullptr) ||
                (wcsstr(fnStr, STR("WriteSave"))  != nullptr) ||
                (wcsstr(fnStr, STR("LoadGame"))   != nullptr) ||
                (wcsstr(fnStr, STR("Persist"))    != nullptr);
            if (!matches) return;

            std::wstring ctxCls = obj ? safeClassName(obj) : std::wstring(STR("<null>"));
            std::wstring key = std::wstring(fnStr) + STR("|") + ctxCls;
            if (m_probesSeenE.size() > 2048) m_probesSeenE.clear();
            if (m_probesSeenE.find(key) != m_probesSeenE.end()) return;
            m_probesSeenE.insert(key);
            VLOG(STR("[ProbeE] fn={} ctx_cls={}\n"), fnStr, ctxCls.c_str());

            try {
                for (auto* prop : func->ForEachProperty()) {
                    std::wstring pname, ptype;
                    try { pname = prop->GetName(); } catch (...) {}
                    try { ptype = prop->GetClass().GetName(); } catch (...) {}
                    int32 poff = prop->GetOffset_Internal();
                    if (ptype == STR("StrProperty")) {
                        auto* fs = reinterpret_cast<RC::Unreal::FString*>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring v;
                        try {
                            const wchar_t* w = fs->GetCharArray().GetData();
                            if (w) v = w;
                        } catch (...) {}
                        VLOG(STR("[ProbeE]   .{} (FString) = \"{}\"\n"),
                             pname.empty() ? STR("?") : pname.c_str(), v.c_str());
                    } else if (ptype == STR("NameProperty")) {
                        auto* fn = reinterpret_cast<RC::Unreal::FName*>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring v;
                        try { v = fn->ToString(); } catch (...) {}
                        VLOG(STR("[ProbeE]   .{} (FName) = {}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             v.empty() ? STR("?") : v.c_str());
                    } else if (ptype == STR("IntProperty")) {
                        int32 v = *reinterpret_cast<int32*>(
                            static_cast<uint8_t*>(parms) + poff);
                        VLOG(STR("[ProbeE]   .{} (int32) = {}\n"),
                             pname.empty() ? STR("?") : pname.c_str(), v);
                    } else {
                        VLOG(STR("[ProbeE]   .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(), poff);
                    }
                }
            } catch (...) {}
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE F — post-hook for save-subsystem UFunctions. Captures return
        // values (Probe E is pre-hook, so misses anything written by callee).
        // Used to discover the on-disk slot name / world identifier path that
        // the save system actually uses — we didn't see one in rc.52 because
        // Probe E's filter missed the relevant function names.
        // ──────────────────────────────────────────────────────────────────
        void onProbeF_processEventPost(UObject* obj, RC::Unreal::UFunction* func, void* parms)
        {
            if (!m_goatSaveProbesEnabled || !func || !parms) return;
            const auto fname = func->GetName();
            const wchar_t* fnStr = fname.c_str();
            bool matches =
                (wcsstr(fnStr, STR("MorSave"))         != nullptr) ||
                (wcsstr(fnStr, STR("SaveSubsystem"))   != nullptr) ||
                (wcsstr(fnStr, STR("SaveManager"))     != nullptr) ||
                (wcsstr(fnStr, STR("GetCurrentWorld")) != nullptr) ||
                (wcsstr(fnStr, STR("GetWorldGuid"))    != nullptr) ||
                (wcsstr(fnStr, STR("GetSlot"))         != nullptr) ||
                (wcsstr(fnStr, STR("SlotName"))        != nullptr) ||
                (wcsstr(fnStr, STR("WorldSlot"))       != nullptr) ||
                (wcsstr(fnStr, STR("WorldName"))       != nullptr) ||
                (wcsstr(fnStr, STR("WorldId"))         != nullptr) ||
                (wcsstr(fnStr, STR("SaveFile"))        != nullptr);
            if (!matches) return;

            std::wstring ctxCls = obj ? safeClassName(obj) : std::wstring(STR("<null>"));
            std::wstring key = std::wstring(fnStr) + STR("|") + ctxCls;
            if (m_probesSeenF.size() > 2048) m_probesSeenF.clear();
            if (m_probesSeenF.find(key) != m_probesSeenF.end()) return;
            m_probesSeenF.insert(key);
            VLOG(STR("[ProbeF] fn={} ctx_cls={}\n"), fnStr, ctxCls.c_str());

            try {
                for (auto* prop : func->ForEachProperty()) {
                    std::wstring pname, ptype;
                    try { pname = prop->GetName(); } catch (...) {}
                    try { ptype = prop->GetClass().GetName(); } catch (...) {}
                    int32 poff = prop->GetOffset_Internal();
                    if (ptype == STR("StrProperty")) {
                        auto* fs = reinterpret_cast<RC::Unreal::FString*>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring v;
                        try {
                            const wchar_t* w = fs->GetCharArray().GetData();
                            if (w) v = w;
                        } catch (...) {}
                        VLOG(STR("[ProbeF]   .{} (FString) = \"{}\"\n"),
                             pname.empty() ? STR("?") : pname.c_str(), v.c_str());
                    } else if (ptype == STR("NameProperty")) {
                        auto* fn = reinterpret_cast<RC::Unreal::FName*>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring v;
                        try { v = fn->ToString(); } catch (...) {}
                        VLOG(STR("[ProbeF]   .{} (FName) = {}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             v.empty() ? STR("?") : v.c_str());
                    } else if (ptype == STR("IntProperty")) {
                        int32 v = *reinterpret_cast<int32*>(
                            static_cast<uint8_t*>(parms) + poff);
                        VLOG(STR("[ProbeF]   .{} (int32) = {}\n"),
                             pname.empty() ? STR("?") : pname.c_str(), v);
                    } else if (ptype == STR("ObjectProperty")) {
                        UObject* v = *reinterpret_cast<UObject**>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring vc = v ? safeClassName(v) : std::wstring(STR("<null>"));
                        VLOG(STR("[ProbeF]   .{} (UObject*) = {:p} ({})\n"),
                             pname.empty() ? STR("?") : pname.c_str(), (void*)v, vc.c_str());
                    } else {
                        VLOG(STR("[ProbeF]   .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(), poff);
                    }
                }
            } catch (...) {}
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE G — save participant analysis. Find every UClass whose
        // instances respond to GetFunctionByNameInChain("SaveGameObjectPreStore")
        // and dump:
        //   1. Class name + full super chain (find common ancestor — that's
        //      the inherited save base, or the level where the UFunction is
        //      first introduced)
        //   2. Outer chain of one sample live instance (level-placed if
        //      PersistentLevel appears; runtime-spawned otherwise)
        //   3. SaveGameObjectId UPROPERTY presence + value if present
        //
        // Answers: what does our pak BP need to do/be/implement to enter
        // the save graph as a first-class participant?
        // ──────────────────────────────────────────────────────────────────
        void runProbeG_participantAnalysis()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeGDone) return;
            m_probesProbeGDone = true;
            VLOG(STR("[ProbeG] starting participant scan...\n"));

            // Pass 1: collect (UClass*, sample_live_instance*) by scanning
            // all UObjects for GetFunctionByNameInChain("SaveGameObjectPreStore").
            // Prefer non-CDO sample if available.
            std::unordered_map<RC::Unreal::UClass*, UObject*> participants;
            UObjectGlobals::ForEachUObject([&](UObject* o, int32_t, int32_t) -> LoopAction {
                if (!o) return LoopAction::Continue;
                RC::Unreal::UFunction* fn = nullptr;
                try { fn = o->GetFunctionByNameInChain(STR("SaveGameObjectPreStore")); } catch (...) {}
                if (!fn) return LoopAction::Continue;
                RC::Unreal::UClass* cls = nullptr;
                try { cls = o->GetClassPrivate(); } catch (...) {}
                if (!cls) return LoopAction::Continue;
                // Skip CDOs — prefer real instance, but record class either way
                std::wstring cn;
                try { cn = safeClassName(o); } catch (...) {}
                bool isCdo = (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__"));
                auto it = participants.find(cls);
                if (it == participants.end()) {
                    participants[cls] = isCdo ? nullptr : o;
                } else if (!it->second && !isCdo) {
                    it->second = o;
                }
                return LoopAction::Continue;
            });

            VLOG(STR("[ProbeG] found {} unique participant UClasses\n"), participants.size());
            int idx = 0;
            for (auto& kv : participants) {
                RC::Unreal::UClass* p = kv.first;
                UObject* sample = kv.second;
                std::wstring pn;
                try { pn = p->GetName(); } catch (...) {}
                VLOG(STR("[ProbeG] [{}] class: {} sample={:p}\n"),
                     idx, pn.empty() ? STR("?") : pn.c_str(), (void*)sample);

                // (a) Super chain — where does SaveGameObjectPreStore first appear?
                //     If a non-leaf class in the chain also has this UFunction,
                //     the UFunction is inherited from there. The DEEPEST class
                //     that still has it is the introducer (interface or base).
                RC::Unreal::UClass* s = p;
                int d = 0;
                RC::Unreal::UClass* lastHasFn = nullptr;
                while (s && d < 16) {
                    std::wstring sn;
                    try { sn = s->GetName(); } catch (...) {}
                    // Use the CDO of this class to test for the UFunction (so we walk
                    // the *class hierarchy* rather than a single object's chain).
                    UObject* sCdo = nullptr;
                    try { sCdo = s->GetClassDefaultObject(); } catch (...) {}
                    bool hasFn = false;
                    if (sCdo) {
                        RC::Unreal::UFunction* tfn = nullptr;
                        try { tfn = sCdo->GetFunctionByNameInChain(STR("SaveGameObjectPreStore")); } catch (...) {}
                        if (tfn) hasFn = true;
                    }
                    if (hasFn) lastHasFn = s;
                    VLOG(STR("[ProbeG]    super[{}]: {} hasPreStore={}\n"),
                         d, sn.empty() ? STR("?") : sn.c_str(), hasFn ? STR("yes") : STR("no"));
                    RC::Unreal::UStruct* sup = nullptr;
                    try { sup = s->GetSuperStruct(); } catch (...) {}
                    if (!sup || sup == s) break;
                    s = static_cast<RC::Unreal::UClass*>(sup);
                    ++d;
                }
                if (lastHasFn) {
                    std::wstring ln;
                    try { ln = lastHasFn->GetName(); } catch (...) {}
                    VLOG(STR("[ProbeG]    deepest-with-PreStore: {} (this is likely the introducer)\n"),
                         ln.empty() ? STR("?") : ln.c_str());
                }

                // (b) Outer chain of the live sample — is it level-placed?
                if (sample) {
                    UObject* outer = sample;
                    int od = 0;
                    while (outer && od < 8) {
                        std::wstring outerName, outerCls;
                        try { outerName = outer->GetName(); } catch (...) {}
                        try { outerCls = safeClassName(outer); } catch (...) {}
                        VLOG(STR("[ProbeG]    outer[{}]: {} ({})\n"),
                             od,
                             outerName.empty() ? STR("?") : outerName.c_str(),
                             outerCls.empty() ? STR("?") : outerCls.c_str());
                        UObject* next = nullptr;
                        try { next = outer->GetOuterPrivate(); } catch (...) {}
                        if (!next || next == outer) break;
                        outer = next;
                        ++od;
                    }
                }

                // (c) Does this class carry a SaveGameObjectId UPROPERTY?
                if (sample) {
                    try {
                        auto* sgid = sample->GetValuePtrByPropertyNameInChain<uint8_t>(STR("SaveGameObjectId"));
                        if (sgid) {
                            wchar_t buf[80];
                            swprintf_s(buf, 80,
                                L"%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                                sgid[0],sgid[1],sgid[2],sgid[3], sgid[4],sgid[5],sgid[6],sgid[7],
                                sgid[8],sgid[9],sgid[10],sgid[11], sgid[12],sgid[13],sgid[14],sgid[15]);
                            VLOG(STR("[ProbeG]    SaveGameObjectId(16B) = {}\n"), buf);
                        } else {
                            VLOG(STR("[ProbeG]    SaveGameObjectId UPROPERTY: NOT FOUND\n"));
                        }
                    } catch (...) {}
                }

                ++idx;
                if (idx > 64) {
                    VLOG(STR("[ProbeG] truncated participant list at 64 classes\n"));
                    break;
                }
            }
            VLOG(STR("[ProbeG] complete\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE H — interaction delegate capture (rc.54).
        //
        // Goal: capture the live signature of the 6 OnXxxInteraction delegate
        // UFunctions fired when the user clicks goat menu entries. Filter:
        // fnStr contains "Interaction" AND context is BP_NpcGoat_C / MorNPCComponent
        // / class name containing "Npc".  Dedup'd per (fnName|ctxCls).
        //
        // Wired into the existing PE post-hook in dllmain.cpp alongside C+F.
        // ──────────────────────────────────────────────────────────────────
        void onProbeH_processEventPost(UObject* obj, RC::Unreal::UFunction* func, void* parms)
        {
            if (!m_goatSaveProbesEnabled || !func) return;
            const auto fname = func->GetName();
            const wchar_t* fnStr = fname.c_str();

            // [rc.57 WIDENED 2026-05-20] Menu-click dispatch fires on the widget
            // not on the NPC. Two acceptance paths:
            //   Path 1 — name contains "Interaction" AND ctx is NPC-related
            //   Path 2 — name is OnPressInteract/OnReleaseInteract/OnSetInteraction
            //            AND ctx contains "UI_WBP_Interaction" (the row widget)
            //
            // Path 2 captures the actual click events on rows. We read the row's
            // InteractionText UPROPERTY to identify WHICH label was clicked
            // ("Dismiss"/"Rename"/etc.) — that's the dispatch key our DLL will
            // use to route to per-goat actions in rc.58.

            std::wstring ctxCls = obj ? safeClassName(obj) : std::wstring(STR("<null>"));
            bool isNpcCtx = (ctxCls.find(STR("Npc")) != std::wstring::npos ||
                             ctxCls.find(STR("NPC")) != std::wstring::npos ||
                             ctxCls.find(STR("MorNPCComponent")) != std::wstring::npos);
            bool isWidgetCtx = (ctxCls.find(STR("UI_WBP_Interaction")) != std::wstring::npos);
            bool isInteractionFn = (wcsstr(fnStr, STR("Interaction")) != nullptr);

            // [rc.59 CRASH FIX 2026-05-20] OnSetInteraction fires DURING widget
            // construction — the InteractionText FText hasn't been fully
            // initialized at that moment. Reading it via FText::ToString went
            // through UKismetTextLibrary::Conv_TextToString → FText::Rebuild →
            // AV on uninitialized text source. C++ try/catch couldn't catch
            // the SEH AV. Drop OnSetInteraction from the filter — we only care
            // about click events (OnPressInteract / OnReleaseInteract).
            bool isPressFn = (wcscmp(fnStr, STR("OnPressInteract")) == 0 ||
                              wcscmp(fnStr, STR("OnReleaseInteract")) == 0);

            bool path1 = isInteractionFn && isNpcCtx;
            bool path2 = isPressFn && isWidgetCtx;
            if (!path1 && !path2) return;

            // [rc.59 CRASH FIX 2026-05-20] FText::ToString on the widget's
            // InteractionText is unsafe even at OnPressInteract time (calls
            // FText::Rebuild which can AV on certain text states). The
            // existing [GoatHeader] DUMP diagnostic in dllmain already captures
            // row labels at menu-show time via direct TextBlock interrogation.
            // For ProbeH we just log the click event itself + widget address;
            // correlate with GoatHeader DUMP for the label offline.
            std::wstring rowLabel;
            void* rowAddr = (void*)obj;

            // Dedup key includes rowLabel so each (fn|cls|label) tuple logs once.
            std::wstring key = std::wstring(fnStr) + STR("|") + ctxCls + STR("|") + rowLabel;
            if (m_probesSeenH.size() > 2048) m_probesSeenH.clear();
            if (m_probesSeenH.find(key) != m_probesSeenH.end()) return;
            m_probesSeenH.insert(key);
            if (path2) {
                VLOG(STR("[ProbeH] CLICK fn={} ctx_cls={} rowAddr={:p} (correlate with [GoatHeader] DUMP by addr for label)\n"),
                     fnStr, ctxCls.c_str(), rowAddr);
            } else {
                VLOG(STR("[ProbeH] interaction fn={} ctx_cls={}\n"), fnStr, ctxCls.c_str());
            }

            // Dump full param list — for delegate UFunctions the params are the
            // delegate signature (sender, payload, etc.).
            if (!parms) {
                VLOG(STR("[ProbeH]   (no parms)\n"));
                return;
            }
            try {
                for (auto* prop : func->ForEachProperty()) {
                    std::wstring pname, ptype;
                    try { pname = prop->GetName(); } catch (...) {}
                    try { ptype = prop->GetClass().GetName(); } catch (...) {}
                    int32 poff = prop->GetOffset_Internal();
                    if (ptype == STR("ObjectProperty")) {
                        UObject* v = nullptr;
                        try {
                            v = *reinterpret_cast<UObject**>(
                                static_cast<uint8_t*>(parms) + poff);
                        } catch (...) {}
                        std::wstring vc = v ? safeClassName(v) : std::wstring(STR("<null>"));
                        VLOG(STR("[ProbeH]   .{} (UObject*) = {:p} ({})\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             (void*)v, vc.c_str());
                    } else if (ptype == STR("NameProperty")) {
                        auto* fn = reinterpret_cast<RC::Unreal::FName*>(
                            static_cast<uint8_t*>(parms) + poff);
                        std::wstring v;
                        try { v = fn->ToString(); } catch (...) {}
                        VLOG(STR("[ProbeH]   .{} (FName) = {}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             v.empty() ? STR("?") : v.c_str());
                    } else if (ptype == STR("BoolProperty")) {
                        VLOG(STR("[ProbeH]   .{} (bool) @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(), poff);
                    } else {
                        VLOG(STR("[ProbeH]   .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(), poff);
                    }
                }
            } catch (...) {}
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE I — BP_NpcGoat_AIController_C class & DynamicBehaviors dump
        //
        // One-shot per NUM* trigger. Finds a live AIController instance via
        // findAllOfSafe, walks its class hierarchy, dumps all UPROPERTYs at
        // every super level, and hex-dumps the first 64 bytes of the
        // DynamicBehaviors offset (the TMap header) for offline decode.
        //
        // We don't try to enumerate the TMap entries via FScriptMapHelper —
        // that helper isn't exposed in UE4SS deps. Hex dump is enough to
        // confirm Num + Hash + first-pair structure.
        // ──────────────────────────────────────────────────────────────────
        void runProbeI_aiControllerDump()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeIDone) return;

            std::vector<UObject*> ctrls;
            findAllOfSafe(STR("BP_NpcGoat_AIController_C"), ctrls);
            UObject* sample = nullptr;
            for (UObject* c : ctrls) {
                if (!c || !isObjectAlive(c)) continue;
                std::wstring cn = safeClassName(c);
                if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                sample = c; break;
            }
            if (!sample) {
                // Retry next eligible tick — don't mark done yet.
                return;
            }
            m_probesProbeIDone = true;

            VLOG(STR("[ProbeI] BP_NpcGoat_AIController_C sample={:p}\n"), (void*)sample);

            // Class hierarchy
            RC::Unreal::UClass* c = nullptr;
            try { c = sample->GetClassPrivate(); } catch (...) {}
            int depth = 0;
            while (c && depth < 16) {
                std::wstring cname;
                try { cname = c->GetName(); } catch (...) {}
                VLOG(STR("[ProbeI]   class[{}]: {}\n"), depth, cname.c_str());
                RC::Unreal::UStruct* sup = nullptr;
                try { sup = c->GetSuperStruct(); } catch (...) {}
                c = static_cast<RC::Unreal::UClass*>(sup);
                ++depth;
            }

            // Enumerate UPROPERTYs at every super level
            RC::Unreal::UStruct* curStruct = nullptr;
            try { curStruct = sample->GetClassPrivate(); } catch (...) {}
            int chainDepth = 0;
            FProperty* dynBehaviorsProp = nullptr;
            int32 dynBehaviorsOff = -1;
            std::wstring dynBehaviorsTypeName;
            while (curStruct && chainDepth < 8) {
                std::wstring sn;
                try { sn = curStruct->GetName(); } catch (...) {}
                VLOG(STR("[ProbeI]   --- props from {} ---\n"),
                     sn.empty() ? STR("?") : sn.c_str());
                int n = 0;
                try {
                    for (auto* prop : curStruct->ForEachProperty()) {
                        std::wstring pname, ptype;
                        try { pname = prop->GetName(); } catch (...) {}
                        try { ptype = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[ProbeI]     .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(),
                             prop->GetOffset_Internal());
                        if (pname == STR("DynamicBehaviors")) {
                            dynBehaviorsProp = prop;
                            dynBehaviorsOff  = prop->GetOffset_Internal();
                            dynBehaviorsTypeName = ptype;
                        }
                        if (++n > 32) {
                            VLOG(STR("[ProbeI]     ...(truncated)\n"));
                            break;
                        }
                    }
                } catch (...) {}
                RC::Unreal::UStruct* sup = nullptr;
                try { sup = curStruct->GetSuperStruct(); } catch (...) { break; }
                if (!sup || sup == curStruct) break;
                curStruct = sup;
                ++chainDepth;
            }

            // [rc.57 ProbeI-2] Dereference the TMap pairs pointer and dump
            // each entry's key+value bytes. FScriptMap is FScriptSet of pair
            // structs, backed by FScriptSparseArray. Layout decoded from rc.56
            // 80-byte header dump:
            //   offset 0  : Data ptr (FScriptSparseArrayElement*)
            //   offset 8  : Num  (int32)
            //   offset 12 : Max  (int32)
            //
            // Each FScriptSparseArrayElement holds either:
            //   - the pair (Key + Value)  if the slot is live, OR
            //   - a free-list link        if the slot is freed
            // For TMap<FName, X>, pair_size = max(8 + sizeof(X), 16).
            // We assume FName(8) + UObject*(8) = 16 byte pair.
            // Stride probably 16 (no padding required for 8-byte alignment).
            //
            // Dump 32 bytes per slot (covers any reasonable stride + bit-flag
            // padding) and interpret the first 16 bytes as FName + UObject*.
            if (dynBehaviorsOff >= 0) {
                const uint8_t* base = reinterpret_cast<const uint8_t*>(sample);
                const uint8_t* tmapHdr = base + dynBehaviorsOff;
                if (isReadableMemory(tmapHdr, 16)) {
                    uint8_t* pairsData = *reinterpret_cast<uint8_t* const*>(tmapHdr + 0);
                    int32 num = *reinterpret_cast<const int32*>(tmapHdr + 8);
                    int32 max = *reinterpret_cast<const int32*>(tmapHdr + 12);
                    VLOG(STR("[ProbeI-2] DynamicBehaviors decoded: Data={:p} Num={} Max={}\n"),
                         (void*)pairsData, num, max);

                    if (pairsData && num > 0 && num <= 32) {
                        // [rc.58 LAYOUT FIX 2026-05-20] UE4 TMap<FName, T>
                        // stores TSetElement<TTuple<FName,T>>:
                        //   offset 0    : Key (FName, 8B)
                        //   offset 8    : Value (T, 8B for TSubclassOf/UClass*)
                        //   offset 16   : HashNextId (int32)
                        //   offset 20   : HashIndex  (int32)
                        // Stride = 24, NOT 16. rc.57 used 16 → second slot's
                        // FName parse hit HashNextId/HashIndex garbage and AV'd
                        // because C++ try/catch can't catch SEH. rc.58 defaults
                        // to 24 + uses SEH-wrapped seh_fnameToString.
                        for (int32 stride : {24}) {
                            VLOG(STR("[ProbeI-2] --- stride={} interpretation ---\n"), stride);
                            for (int32 i = 0; i < num; ++i) {
                                uint8_t* slot = pairsData + (int64_t)i * stride;
                                if (!isReadableMemory(slot, 32)) {
                                    VLOG(STR("[ProbeI-2]   [{}] slot unreadable\n"), i);
                                    continue;
                                }
                                // First 32 bytes as hex.
                                wchar_t hex[160];
                                swprintf_s(hex, 160,
                                    L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x  "
                                    L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x",
                                    slot[0],slot[1],slot[2],slot[3], slot[4],slot[5],slot[6],slot[7],
                                    slot[8],slot[9],slot[10],slot[11], slot[12],slot[13],slot[14],slot[15],
                                    slot[16],slot[17],slot[18],slot[19], slot[20],slot[21],slot[22],slot[23],
                                    slot[24],slot[25],slot[26],slot[27], slot[28],slot[29],slot[30],slot[31]);
                                // Interpret first 8 bytes as FName (SEH-wrapped
                                // — bare ToString crashes on garbage, AV).
                                std::wstring keyAsName = seh_fnameToString(slot);
                                UObject* valAsObj = nullptr;
                                try {
                                    valAsObj = *reinterpret_cast<UObject**>(slot + 8);
                                } catch (...) {}
                                std::wstring valCls;
                                if (valAsObj) {
                                    try {
                                        if (isObjectAlive(valAsObj)) {
                                            valCls = safeClassName(valAsObj);
                                            // For UClass*, safeClassName returns "BlueprintGeneratedClass"
                                            // (the meta-class) — also try direct GetName for the
                                            // actual class name.
                                            try {
                                                std::wstring directName = valAsObj->GetName();
                                                if (!directName.empty() && directName != valCls)
                                                    valCls += STR(" / direct=") + directName;
                                            } catch (...) {}
                                        }
                                    } catch (...) {}
                                }
                                VLOG(STR("[ProbeI-2]   [{}] bytes={} key='{}' val={:p} valCls='{}'\n"),
                                     i, hex,
                                     keyAsName.empty() ? STR("?") : keyAsName.c_str(),
                                     (void*)valAsObj,
                                     valCls.empty() ? STR("?") : valCls.c_str());
                            }
                            // Only run stride=16 unless it clearly fails. If
                            // we got recognizable FName output, stop trying.
                            // Heuristic: stop after stride 16 — the typical case.
                            break;
                        }
                    } else if (!pairsData) {
                        VLOG(STR("[ProbeI-2] pairsData is null — empty TMap\n"));
                    } else {
                        VLOG(STR("[ProbeI-2] Num={} out of expected range; aborting deref\n"), num);
                    }
                } else {
                    VLOG(STR("[ProbeI-2] tmap header unreadable\n"));
                }
            }

            // (Legacy hex dump kept below for cross-reference.)
            if (dynBehaviorsOff >= 0) {
                VLOG(STR("[ProbeI] DynamicBehaviors found at offset +0x{:x} type={}\n"),
                     dynBehaviorsOff, dynBehaviorsTypeName.c_str());
                const uint8_t* base = reinterpret_cast<const uint8_t*>(sample);
                if (isReadableMemory(base + dynBehaviorsOff, 80)) {
                    const uint8_t* p = base + dynBehaviorsOff;
                    wchar_t buf[320];
                    swprintf_s(buf, 320,
                        L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x  "
                        L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x  "
                        L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x  "
                        L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x  "
                        L"%02x%02x%02x%02x %02x%02x%02x%02x  %02x%02x%02x%02x %02x%02x%02x%02x",
                        p[0],p[1],p[2],p[3], p[4],p[5],p[6],p[7],
                        p[8],p[9],p[10],p[11], p[12],p[13],p[14],p[15],
                        p[16],p[17],p[18],p[19], p[20],p[21],p[22],p[23],
                        p[24],p[25],p[26],p[27], p[28],p[29],p[30],p[31],
                        p[32],p[33],p[34],p[35], p[36],p[37],p[38],p[39],
                        p[40],p[41],p[42],p[43], p[44],p[45],p[46],p[47],
                        p[48],p[49],p[50],p[51], p[52],p[53],p[54],p[55],
                        p[56],p[57],p[58],p[59], p[60],p[61],p[62],p[63],
                        p[64],p[65],p[66],p[67], p[68],p[69],p[70],p[71],
                        p[72],p[73],p[74],p[75], p[76],p[77],p[78],p[79]);
                    VLOG(STR("[ProbeI] DynamicBehaviors raw bytes (80B) = {}\n"), buf);
                }
            } else {
                VLOG(STR("[ProbeI] DynamicBehaviors UPROPERTY: NOT FOUND in class chain\n"));
            }
            VLOG(STR("[ProbeI] complete\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE J — BP_PorterGoatPersistor_C class verification
        //
        // Confirms the persistor class loads, implements MorSaveGameObjectCallbacks,
        // and lists its (TimeManager-inherited) UPROPERTYs. Will report
        // "class not found" cleanly when Tobi's PR #3 hasn't been merged yet —
        // that's not an error, just informational.
        // ──────────────────────────────────────────────────────────────────
        void runProbeJ_persistorVerify()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeJDone) return;
            m_probesProbeJDone = true;

            // [rc.57 FIX 2026-05-20] Pak-side BP classes are LAZY-LOADED.
            // StaticFindObject only sees the live class registry — returns null
            // for pak BPs nothing has referenced yet. rc.56 reported "NOT LOADED"
            // for BP_PorterGoatPersistor_C even though it was in the test pak.
            // Force-resolve via goat_loadClassAssetBlocking (PE call into
            // KismetSystemLibrary::LoadClassAsset_Blocking). Once forced into
            // memory, subsequent FindObject lookups hit it.
            const wchar_t* classPath =
                STR("/Game/Mods/PorterGoat/Persistor/BP_PorterGoatPersistor.BP_PorterGoatPersistor_C");
            RC::Unreal::UClass* persistorClass = nullptr;
            try {
                persistorClass = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, classPath);
            } catch (...) {}
            if (!persistorClass) {
                VLOG(STR("[ProbeJ] StaticFindObject returned null — attempting LoadClassAsset_Blocking force-resolve\n"));
                try {
                    persistorClass = goat_loadClassAssetBlocking(classPath);
                } catch (...) {}
            }
            if (!persistorClass) {
                VLOG(STR("[ProbeJ] {} NOT LOADED even after force-resolve — pak doesn't carry it\n"), classPath);
                return;
            }
            VLOG(STR("[ProbeJ] persistor class loaded at {:p} (path={})\n"),
                 (void*)persistorClass, classPath);

            // Walk super chain
            RC::Unreal::UClass* c = persistorClass;
            int d = 0;
            while (c && d < 16) {
                std::wstring cname;
                try { cname = c->GetName(); } catch (...) {}
                VLOG(STR("[ProbeJ]   class[{}]: {}\n"), d, cname.c_str());

                // Probe interface implementation by checking the CDO for
                // each of the 6 MorSaveGameObjectCallbacks UFunctions.
                UObject* cdo = nullptr;
                try { cdo = c->GetClassDefaultObject(); } catch (...) {}
                if (cdo) {
                    const wchar_t* sgcMethods[] = {
                        STR("SaveGameObjectPreStore"),
                        STR("SaveGameObjectPostStore"),
                        STR("SaveGameObjectPreRestore"),
                        STR("SaveGameObjectPostRestore"),
                        STR("SaveGameObjectPreRestoreDestroy"),
                        STR("SaveGameObjectUpgradeClass"),
                        nullptr
                    };
                    int found = 0;
                    for (const wchar_t** p = sgcMethods; *p; ++p) {
                        RC::Unreal::UFunction* fn = nullptr;
                        try { fn = cdo->GetFunctionByNameInChain(*p); } catch (...) {}
                        if (fn) ++found;
                    }
                    if (found > 0) {
                        VLOG(STR("[ProbeJ]   class[{}] implements {} of 6 MorSaveGameObjectCallbacks methods\n"),
                             d, found);
                    }
                }

                RC::Unreal::UStruct* sup = nullptr;
                try { sup = c->GetSuperStruct(); } catch (...) {}
                c = static_cast<RC::Unreal::UClass*>(sup);
                ++d;
            }

            // Enumerate UPROPERTYs across full super chain (so we see the
            // TimeManager-inherited spillover: Progress_OrcsAreScary_*, etc.)
            RC::Unreal::UStruct* curStruct = persistorClass;
            int chainDepth = 0;
            while (curStruct && chainDepth < 8) {
                std::wstring sn;
                try { sn = curStruct->GetName(); } catch (...) {}
                VLOG(STR("[ProbeJ]   --- props from {} ---\n"),
                     sn.empty() ? STR("?") : sn.c_str());
                int n = 0;
                try {
                    for (auto* prop : curStruct->ForEachProperty()) {
                        std::wstring pname, ptype;
                        try { pname = prop->GetName(); } catch (...) {}
                        try { ptype = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[ProbeJ]     .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(),
                             prop->GetOffset_Internal());
                        if (++n > 24) {
                            VLOG(STR("[ProbeJ]     ...(truncated at level)\n"));
                            break;
                        }
                    }
                } catch (...) {}
                if (sn == STR("Actor") || sn == STR("Object")) break;  // stop at AActor; UObject props irrelevant
                RC::Unreal::UStruct* sup = nullptr;
                try { sup = curStruct->GetSuperStruct(); } catch (...) { break; }
                if (!sup || sup == curStruct) break;
                curStruct = sup;
                ++chainDepth;
            }
            VLOG(STR("[ProbeJ] complete\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // PERSISTOR WATCH (log-only, rc.54).
        //
        // The persistor is a TimeManager-spillover clone — its inherited
        // BeginPlay and 6 SaveGameObject* UFunctions reference DiscoveryManager
        // (null on the persistor) and may null-deref when invoked. We need to
        // suppress these calls before they execute.
        //
        // **However** — UE4SS's PreventOriginalFunctionCall is only available
        // via the new TCallbackIterationData "WithData" callback API, and our
        // current PE hooks are the deprecated metadata-less variant per
        // CLAUDE.md's "C4996 OFF-LIMITS" decision. Migrating one hook to the
        // new API is feasible (a SINGLE new hook doesn't carry the same
        // semantic-migration risk as migrating ALL existing hooks).
        //
        // For rc.54: LOG-ONLY detection. We're not spawning the persistor
        // (probes only), so the methods won't fire. The watch surfaces any
        // unexpected execution. rc.55 will introduce a single WithData hook
        // for true suppression at the moment we begin spawning persistors.
        // ──────────────────────────────────────────────────────────────────
        void onPersistorWatchPre(UObject* obj, RC::Unreal::UFunction* func)
        {
            if (!m_goatSaveProbesEnabled || !func || !obj) return;
            const auto fname = func->GetName();
            const wchar_t* fnStr = fname.c_str();
            bool isWatched =
                (wcscmp(fnStr, STR("ReceiveBeginPlay"))            == 0) ||
                (wcscmp(fnStr, STR("K2_BeginPlay"))                == 0) ||
                (wcscmp(fnStr, STR("ExecuteUbergraph_BP_PorterGoatPersistor")) == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectPreStore"))      == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectPostStore"))     == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectPreRestore"))    == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectPostRestore"))   == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectPreRestoreDestroy")) == 0) ||
                (wcscmp(fnStr, STR("SaveGameObjectUpgradeClass"))  == 0);
            if (!isWatched) return;

            // Only fire for the persistor; ignore other actors with same UFunctions.
            std::wstring ctxCls = safeClassName(obj);
            if (ctxCls.find(STR("BP_PorterGoatPersistor")) == std::wstring::npos) return;

            std::wstring key = std::wstring(fnStr) + STR("|") + ctxCls;
            if (m_probesSeenPersistorWatch.size() > 256) m_probesSeenPersistorWatch.clear();
            if (m_probesSeenPersistorWatch.find(key) != m_probesSeenPersistorWatch.end()) return;
            m_probesSeenPersistorWatch.insert(key);
            VLOG(STR("[PersistorWatch] !! fired: fn={} ctx_cls={} obj={:p} -- LOG ONLY in rc.54; rc.55 will suppress via WithData hook\n"),
                 fnStr, ctxCls.c_str(), (void*)obj);
        }

        // ──────────────────────────────────────────────────────────────────
        // PROBE K — Blackboard + FSM state enumeration (rc.61)
        //
        // Goal: find the BlackboardComponent that holds the AI's runtime
        // values (LeashActor, CurrentTarget, etc.) so the Follow/Stay
        // implementation can write a single key:
        //   Follow → SetValueAsObject("LeashActor", player_pawn)
        //   Stay   → ClearValue("LeashActor")
        //
        // Approach: find a live BP_NpcGoat_AIController_C, then probe three
        // potential blackboard locations:
        //   1. FGKAIController.BlackboardData @ +0x3e0  (the static asset)
        //   2. AAIController.Blackboard                  (UBlackboardComponent*)
        //   3. FGKAIController.BehaviorFSMComp.Blackboard (component sub-prop)
        //
        // Also: dump the current FSM state name + the goat's MorNPCComponent
        // CurrentRole — useful to diagnose why the second bell-summon's goat
        // produces prey-AI even though SetRole("Porter") fired during spawn.
        // ──────────────────────────────────────────────────────────────────
        void runProbeK_blackboardAndFsm()
        {
            if (!m_goatSaveProbesEnabled || m_probesProbeKDone) return;

            std::vector<UObject*> ctrls;
            findAllOfSafe(STR("BP_NpcGoat_AIController_C"), ctrls);
            UObject* sample = nullptr;
            for (UObject* c : ctrls) {
                if (!c || !isObjectAlive(c)) continue;
                std::wstring cn = safeClassName(c);
                if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                sample = c; break;
            }
            if (!sample) return;  // retry next tick until goat spawned
            m_probesProbeKDone = true;

            VLOG(STR("[ProbeK] AIController sample={:p}\n"), (void*)sample);

            // ---- 1) BlackboardData asset at +0x3e0 ----
            try {
                auto* bbData = sample->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardData"));
                if (bbData && *bbData) {
                    UObject* bbAsset = *bbData;
                    std::wstring cls = safeClassName(bbAsset);
                    std::wstring nm  = safeObjectName(bbAsset);
                    VLOG(STR("[ProbeK] BlackboardData asset={:p} class='{}' name='{}'\n"),
                         (void*)bbAsset, cls.c_str(), nm.c_str());
                    // Walk its UPROPERTYs (the Keys TArray plus parent BBs)
                    int n = 0;
                    try {
                        RC::Unreal::UClass* bbCls = nullptr;
                        try { bbCls = bbAsset->GetClassPrivate(); } catch (...) {}
                        if (bbCls) {
                            for (auto* prop : bbCls->ForEachProperty()) {
                                std::wstring pname, ptype;
                                try { pname = prop->GetName(); } catch (...) {}
                                try { ptype = prop->GetClass().GetName(); } catch (...) {}
                                VLOG(STR("[ProbeK]   .BlackboardData.{} : {} @ +0x{:x}\n"),
                                     pname.empty() ? STR("?") : pname.c_str(),
                                     ptype.empty() ? STR("?") : ptype.c_str(),
                                     prop->GetOffset_Internal());
                                if (++n > 16) { VLOG(STR("[ProbeK]   ...(truncated)\n")); break; }
                            }
                        }
                    } catch (...) {}

                    // [rc.62 ProbeK-2 part A] Dereference BlackboardData.Keys
                    // TArray<FBlackboardEntry>. Standard UE4 FBlackboardEntry:
                    //   EntryName  : FName  @ +0    (8B)
                    //   KeyType    : UClass* @ +8   (8B) — UBlackboardKeyType_*
                    //   bInstanceSynced : bool @ +16
                    // Padded stride ~24. Dump first 32 bytes per slot as hex
                    // + interpret FName via seh_fnameToString.
                    try {
                        const uint8_t* bbBase = reinterpret_cast<const uint8_t*>(bbAsset);
                        constexpr int32 kKeysOff = 0x38;  // confirmed from prop dump above
                        if (isReadableMemory(bbBase + kKeysOff, 16)) {
                            uint8_t* arrData = *reinterpret_cast<uint8_t* const*>(bbBase + kKeysOff + 0);
                            int32 arrNum     = *reinterpret_cast<const int32*>(bbBase + kKeysOff + 8);
                            VLOG(STR("[ProbeK-2] BlackboardData.Keys Num={} Data={:p}\n"),
                                 arrNum, (void*)arrData);
                            if (arrData && arrNum > 0 && arrNum < 64) {
                                constexpr int32 stride = 24;
                                for (int32 i = 0; i < arrNum; ++i) {
                                    uint8_t* slot = arrData + (int64_t)i * stride;
                                    if (!isReadableMemory(slot, 32)) continue;
                                    std::wstring keyName = seh_fnameToString(slot);
                                    UObject* keyType = *reinterpret_cast<UObject**>(slot + 8);
                                    std::wstring keyTypeCls;
                                    if (keyType && isObjectAlive(keyType)) {
                                        keyTypeCls = safeClassName(keyType);
                                    }
                                    VLOG(STR("[ProbeK-2]   Key[{}] name='{}' type={:p} typeClass='{}'\n"),
                                         i,
                                         keyName.empty() ? STR("?") : keyName.c_str(),
                                         (void*)keyType,
                                         keyTypeCls.empty() ? STR("?") : keyTypeCls.c_str());
                                }
                            }
                        }
                    } catch (...) {
                        VLOG(STR("[ProbeK-2] BlackboardData.Keys deref threw\n"));
                    }
                    // Also walk Parent chain — inherited keys live there.
                    try {
                        const uint8_t* bbBase = reinterpret_cast<const uint8_t*>(bbAsset);
                        constexpr int32 kParentOff = 0x30;
                        if (isReadableMemory(bbBase + kParentOff, 8)) {
                            UObject* parent = *reinterpret_cast<UObject* const*>(bbBase + kParentOff);
                            if (parent && isObjectAlive(parent)) {
                                VLOG(STR("[ProbeK-2]   Parent BB={:p} name='{}'\n"),
                                     (void*)parent, safeObjectName(parent).c_str());
                            }
                        }
                    } catch (...) {}
                } else {
                    VLOG(STR("[ProbeK] BlackboardData property missing or null\n"));
                }
            } catch (...) {
                VLOG(STR("[ProbeK] BlackboardData read threw\n"));
            }

            // ---- 2) AAIController.Blackboard (the runtime component) ----
            try {
                auto* bbComp = sample->GetValuePtrByPropertyNameInChain<UObject*>(STR("Blackboard"));
                if (bbComp && *bbComp) {
                    UObject* bbc = *bbComp;
                    std::wstring cls = safeClassName(bbc);
                    std::wstring nm  = safeObjectName(bbc);
                    VLOG(STR("[ProbeK] AAIController.Blackboard={:p} class='{}' name='{}'\n"),
                         (void*)bbc, cls.c_str(), nm.c_str());
                    // Walk its UPROPERTYs — expecting some FBlackboardEntry-shaped storage
                    int n = 0;
                    try {
                        RC::Unreal::UClass* bcCls = nullptr;
                        try { bcCls = bbc->GetClassPrivate(); } catch (...) {}
                        if (bcCls) {
                            for (auto* prop : bcCls->ForEachProperty()) {
                                std::wstring pname, ptype;
                                try { pname = prop->GetName(); } catch (...) {}
                                try { ptype = prop->GetClass().GetName(); } catch (...) {}
                                VLOG(STR("[ProbeK]   .Blackboard.{} : {} @ +0x{:x}\n"),
                                     pname.empty() ? STR("?") : pname.c_str(),
                                     ptype.empty() ? STR("?") : ptype.c_str(),
                                     prop->GetOffset_Internal());
                                if (++n > 24) { VLOG(STR("[ProbeK]   ...(truncated)\n")); break; }
                            }
                        }
                    } catch (...) {}
                } else {
                    VLOG(STR("[ProbeK] AAIController.Blackboard property missing or null\n"));
                }
            } catch (...) {
                VLOG(STR("[ProbeK] Blackboard read threw\n"));
            }

            // ---- 3) BehaviorFSMComp current state ----
            try {
                auto* fsmPtr = sample->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
                if (fsmPtr && *fsmPtr) {
                    UObject* fsm = *fsmPtr;
                    std::wstring cls = safeClassName(fsm);
                    VLOG(STR("[ProbeK] BehaviorFSMComp={:p} class='{}'\n"),
                         (void*)fsm, cls.c_str());
                    // Try candidate property names for the current state pointer
                    const wchar_t* stateCands[] = {
                        STR("CurrentState"), STR("ActiveState"), STR("CurrentBehavior"),
                        STR("RootState"), STR("ActiveBehavior"), STR("State"),
                        STR("FSMRoot"),  // [rc.62 ADDED] confirmed in rc.61 prop dump
                        nullptr
                    };
                    for (const wchar_t** p = stateCands; *p; ++p) {
                        try {
                            auto* spp = fsm->GetValuePtrByPropertyNameInChain<UObject*>(*p);
                            if (spp && *spp) {
                                UObject* st = *spp;
                                VLOG(STR("[ProbeK]   FSM.{} = {:p} class='{}' name='{}'\n"),
                                     *p, (void*)st,
                                     safeClassName(st).c_str(),
                                     safeObjectName(st).c_str());
                            }
                        } catch (...) {}
                    }

                    // [rc.62 ProbeK-2 part B] Walk FSMRoot leaf-state chain.
                    // FSMRoot is a UFGKBehaviorState; we recurse via candidate
                    // child-pointer property names to find the live leaf state.
                    try {
                        auto* rootPtr = fsm->GetValuePtrByPropertyNameInChain<UObject*>(STR("FSMRoot"));
                        if (rootPtr && *rootPtr) {
                            UObject* state = *rootPtr;
                            VLOG(STR("[ProbeK-2] walking FSMRoot leaf chain:\n"));
                            const wchar_t* childCands[] = {
                                STR("ActiveChild"), STR("CurrentChild"), STR("Child"),
                                STR("ActiveState"), STR("CurrentState"), STR("RootChild"),
                                STR("ActiveSubState"), nullptr
                            };
                            int depth = 0;
                            while (state && isObjectAlive(state) && depth < 8) {
                                std::wstring cls = safeClassName(state);
                                std::wstring nm  = safeObjectName(state);
                                VLOG(STR("[ProbeK-2]   depth[{}] state={:p} class='{}' name='{}'\n"),
                                     depth, (void*)state, cls.c_str(), nm.c_str());

                                // First pass: log all UPROPERTYs once per depth
                                // so we can spot any child-link we missed.
                                if (depth == 0) {
                                    try {
                                        RC::Unreal::UClass* sCls = nullptr;
                                        try { sCls = state->GetClassPrivate(); } catch (...) {}
                                        if (sCls) {
                                            int pn = 0;
                                            for (auto* prop : sCls->ForEachProperty()) {
                                                std::wstring pname, ptype;
                                                try { pname = prop->GetName(); } catch (...) {}
                                                try { ptype = prop->GetClass().GetName(); } catch (...) {}
                                                VLOG(STR("[ProbeK-2]     prop {} : {} @ +0x{:x}\n"),
                                                     pname.empty() ? STR("?") : pname.c_str(),
                                                     ptype.empty() ? STR("?") : ptype.c_str(),
                                                     prop->GetOffset_Internal());
                                                if (++pn > 16) { VLOG(STR("[ProbeK-2]     ...(truncated)\n")); break; }
                                            }
                                        }
                                    } catch (...) {}
                                }

                                // Try to descend
                                UObject* next = nullptr;
                                for (const wchar_t** p = childCands; *p; ++p) {
                                    try {
                                        auto* cpp = state->GetValuePtrByPropertyNameInChain<UObject*>(*p);
                                        if (cpp && *cpp && isObjectAlive(*cpp)) {
                                            next = *cpp;
                                            VLOG(STR("[ProbeK-2]   depth[{}] descending via .{}\n"), depth, *p);
                                            break;
                                        }
                                    } catch (...) {}
                                }
                                if (!next || next == state) break;
                                state = next;
                                ++depth;
                            }
                            VLOG(STR("[ProbeK-2] FSMRoot walk complete (depth reached: {})\n"), depth);
                        }
                    } catch (...) {
                        VLOG(STR("[ProbeK-2] FSMRoot walk threw\n"));
                    }
                    // Dump all UPROPERTYs on the FSM for completeness
                    int n = 0;
                    try {
                        RC::Unreal::UClass* fsmCls = nullptr;
                        try { fsmCls = fsm->GetClassPrivate(); } catch (...) {}
                        if (fsmCls) {
                            for (auto* prop : fsmCls->ForEachProperty()) {
                                std::wstring pname, ptype;
                                try { pname = prop->GetName(); } catch (...) {}
                                try { ptype = prop->GetClass().GetName(); } catch (...) {}
                                VLOG(STR("[ProbeK]   .FSM.{} : {} @ +0x{:x}\n"),
                                     pname.empty() ? STR("?") : pname.c_str(),
                                     ptype.empty() ? STR("?") : ptype.c_str(),
                                     prop->GetOffset_Internal());
                                if (++n > 24) { VLOG(STR("[ProbeK]   ...(truncated)\n")); break; }
                            }
                        }
                    } catch (...) {}
                } else {
                    VLOG(STR("[ProbeK] BehaviorFSMComp property missing or null\n"));
                }
            } catch (...) {
                VLOG(STR("[ProbeK] BehaviorFSMComp read threw\n"));
            }

            // ---- 4) Goat's MorNPCComponent — read CurrentRole ----
            UObject* possessed = nullptr;
            try {
                auto* p = sample->GetValuePtrByPropertyNameInChain<UObject*>(STR("PossessedCharacter"));
                if (p) possessed = *p;
            } catch (...) {}
            if (!possessed) {
                // Fallback: AAIController.Pawn
                try {
                    auto* p = sample->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
                    if (p) possessed = *p;
                } catch (...) {}
            }
            if (possessed && isObjectAlive(possessed)) {
                VLOG(STR("[ProbeK] possessed pawn={:p} class='{}'\n"),
                     (void*)possessed, safeClassName(possessed).c_str());
                try {
                    auto* npcPtr = possessed->GetValuePtrByPropertyNameInChain<UObject*>(STR("MorNPC"));
                    if (npcPtr && *npcPtr) {
                        UObject* npcComp = *npcPtr;
                        VLOG(STR("[ProbeK]   MorNPC={:p} class='{}'\n"),
                             (void*)npcComp, safeClassName(npcComp).c_str());
                        // Use the existing probeCurrentRole helper to log role
                        probeCurrentRole(npcComp);
                    }
                } catch (...) {}
            }

            VLOG(STR("[ProbeK] complete\n"));
        }

        // ──────────────────────────────────────────────────────────────────
        // Manual trigger: NUM* (VK_MULTIPLY = 0x6A). NUM* binding is retired
        // per v7.1.x cleanup so it's free for diagnostic use. Pressing NUM*:
        //   1. Force-enables m_goatSaveProbesEnabled (so user doesn't need
        //      to edit the INI — convenient for one-off probe runs).
        //   2. Resets one-shot done flags so Probes B/B-goat/D re-run.
        //   3. Fires an immediate NpcInfo snapshot labeled "manual-trigger".
        //   4. Clears Probe C/E dedup sets so we can see save events again.
        //
        // Edge-detected via static bool so a long press doesn't spam.
        // ──────────────────────────────────────────────────────────────────
        void pollProbeManualTrigger()
        {
            static bool s_numStarEdge = false;
            bool down = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0;
            if (down && !s_numStarEdge) {
                s_numStarEdge = true;
                m_goatSaveProbesEnabled  = true;
                m_probesProbeBDone       = false;
                m_probesProbeBGoatDone   = false;
                m_probesProbeDDone       = false;
                m_probesProbeGDone       = false;
                m_probesProbeIDone       = false;
                m_probesProbeJDone       = false;
                m_probesProbeKDone       = false;
                m_probesStartupAtMs      = GetTickCount64() - 4000;  // skip remaining startup grace
                m_probesSeenC.clear();
                m_probesSeenE.clear();
                m_probesSeenF.clear();
                m_probesSeenH.clear();
                m_probesSeenPersistorWatch.clear();
                VLOG(STR("[Probes] ===== NUM* manual trigger — probes ENABLED, one-shots reset (rc.54: +H/I/J + PersistorWatch) =====\n"));
                runProbeA_walkNpcInfo(STR("manual-trigger"));
            } else if (!down) {
                s_numStarEdge = false;
            }
        }

        // ──────────────────────────────────────────────────────────────────
        // Per-tick coordinator. 1 s cadence. After a 5 s startup grace,
        // runs Probes B + D once, then takes a NpcInfo snapshot every 10 s
        // so we see goat presence/absence even outside save events.
        //
        // NUM* manual trigger is polled every tick regardless of enabled state
        // (so the user can flip probes on without restarting / editing INI).
        // ──────────────────────────────────────────────────────────────────
        void tickGoatSaveProbes()
        {
            // Always poll the manual trigger — it's the bootstrap for the rest.
            pollProbeManualTrigger();

            if (!m_goatSaveProbesEnabled) return;
            const ULONGLONG now = GetTickCount64();
            if (now - m_probesLastTickMs < 1000) return;
            m_probesLastTickMs = now;

            if (m_probesStartupAtMs == 0) m_probesStartupAtMs = now;
            if (now - m_probesStartupAtMs < 5000) return;  // startup grace

            if (!m_probesProbeBDone)     runProbeB_enumerateGates();
            if (!m_probesProbeBGoatDone) runProbeB_goatHierarchy();   // retries until goat is alive
            if (!m_probesProbeDDone)     runProbeD_findSaveGameSubclasses();
            if (!m_probesProbeGDone)     runProbeG_participantAnalysis();
            if (!m_probesProbeIDone)     runProbeI_aiControllerDump();    // retries until AIController alive
            if (!m_probesProbeJDone)     runProbeJ_persistorVerify();     // one-shot; reports "not loaded" cleanly if PR#3 absent
            if (!m_probesProbeKDone)     runProbeK_blackboardAndFsm();    // retries until AIController alive — Blackboard + FSM dump

            if (now - m_probesLastSnapMs > 10000) {
                m_probesLastSnapMs = now;
                runProbeA_walkNpcInfo(STR("snapshot"));
            }
        }
