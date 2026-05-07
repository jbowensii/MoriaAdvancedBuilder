// moria_npc_recovery.inl — NPC stuck-pathing recovery (teleport-as-fallback).
//
// PHASE 1 (v7.1.0-rc.1): READ-ONLY DIAGNOSTIC PROBE.
// No state mutation, no teleport, no Settings toggle yet. The probe walks
// the first AMorAIController-subclass NPC it sees, dumps its property
// surface to UE4SS.log, then sets m_npcProbeDone=true so it never fires
// again. Confirms three open implementation TODOs from
// plans/deep-percolating-parnas.md before Phase 2 commits to a design:
//
//   #1 — exact UPROPERTY name for the FSM's "current state" pointer
//        on UFGKActorFSMComponent (likely CurrentState or ActiveState)
//   #2 — whether AAIController.Pawn is reflectively accessible, or we
//        need to dispatch K2_GetPawn() via PE
//   #3 — whether FGK exposes a built-in bIsStuck / LastValidPathTime on
//        UMorPathFollowingComponent we should prefer over a location-
//        delta polling heuristic
//
// Probe gating: behind `s_verbose` AND a one-shot `m_npcProbeDone` flag.
// Production builds with verbose off see no overhead.

        bool m_npcProbeDone{false};

        // ════════════════════════════════════════════════════════════════
        // PHASE 2 (v7.1.0-rc.3) — PRODUCTION NPC STUCK-PATHING RECOVERY.
        // ════════════════════════════════════════════════════════════════
        //
        // Algorithm (per the approved plan in deep-percolating-parnas.md):
        //   1) Every 1 s on the game thread, walk cached friendly NPC
        //      controllers (cache refreshes every 5 s via FindAllOf).
        //   2) For each: read pawn (direct reflective UPROPERTY),
        //      walk FSMRoot.ActiveChild chain to leaf state.
        //   3) If leaf has a 'Destination' UPROPERTY (FVector), it's a
        //      path-following state — read pawn location via direct
        //      memory at RootComponent.RelativeLocation.
        //   4) If pawn moved >100 cm since last tick → reset stuck timer
        //      (NPC is making progress). Else accumulate stuck time.
        //   5) After m_npcStuckThresholdMs of no progress AND throttle
        //      window passed → ProcessEvent K2_SetActorLocation with
        //      destination, bSweep=false, bTeleport=true.
        //
        // Authority-only: gated on m_isDedicatedServer || (m_localPC alive).
        // Client peers see no teleport activity — server replicates the
        // movement via standard pawn replication.

        struct NpcRecoveryEntry {
            RC::Unreal::FWeakObjectPtr controller;
            RC::Unreal::FWeakObjectPtr pawn;
            float lastX{0.0f}, lastY{0.0f}, lastZ{0.0f};
            ULONGLONG lastProgressTickMs{0};
            ULONGLONG lastTeleportTickMs{0};
            bool initialized{false};
        };
        std::unordered_map<UObject*, NpcRecoveryEntry> m_npcRecoveryStates;
        std::vector<RC::Unreal::FWeakObjectPtr> m_npcControllerCache;

        ULONGLONG m_lastNpcRecoveryTickMs{0};
        ULONGLONG m_lastNpcCacheRefreshMs{0};

        bool m_npcRecoveryEnabled{true};        // rc.3: hardcoded ON for testing; rc.4 adds Settings toggle
        ULONGLONG m_npcStuckThresholdMs{10000};  // 10 seconds default
        static constexpr ULONGLONG NPC_TELEPORT_THROTTLE_MS = 30000;  // 30s between teleports per NPC
        static constexpr float NPC_PROGRESS_THRESHOLD_CM = 100.0f;     // 1m of movement = "making progress"

        // Diagnostic: log each NEW leaf-state class name once per session
        // so we can map the state-name landscape as the user plays.
        std::set<std::wstring> m_npcLeafClassesSeen;

        // Walk FSMRoot.ActiveChild chain to find the leaf currently-
        // running state. Caps at depth 16 to avoid runaway in case of
        // a self-referencing state graph.
        UObject* npcFindLeafActiveState(UObject* fsmComp)
        {
            if (!fsmComp || !isObjectAlive(fsmComp)) return nullptr;
            auto* rootPtr = fsmComp->GetValuePtrByPropertyNameInChain<UObject*>(STR("FSMRoot"));
            UObject* state = (rootPtr && *rootPtr) ? *rootPtr : nullptr;
            if (!state || !isObjectAlive(state)) return nullptr;

            UObject* leaf = state;
            for (int depth = 0; depth < 16; ++depth)
            {
                auto* childPtr = leaf->GetValuePtrByPropertyNameInChain<UObject*>(STR("ActiveChild"));
                UObject* child = (childPtr && *childPtr) ? *childPtr : nullptr;
                if (!child || !isObjectAlive(child)) break;
                if (child == leaf) break;  // self-loop guard
                leaf = child;
            }
            return leaf;
        }

        // Read pawn world location via direct memory at
        // pawn->RootComponent->RelativeLocation. ~10x faster than
        // K2_GetActorLocation PE call. Verified equivalent in rc.1.
        // Returns false if any link in the chain is null/unreadable.
        bool npcReadPawnLocation(UObject* pawn, float& outX, float& outY, float& outZ)
        {
            if (!pawn || !isObjectAlive(pawn)) return false;
            auto* rcPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
            UObject* root = (rcPtr && *rcPtr) ? *rcPtr : nullptr;
            if (!root || !isObjectAlive(root)) return false;
            auto* relLoc = root->GetValuePtrByPropertyNameInChain<float>(STR("RelativeLocation"));
            if (!relLoc) return false;
            outX = relLoc[0];
            outY = relLoc[1];
            outZ = relLoc[2];
            return true;
        }

        // PE dispatch: K2_SetActorLocation(NewLocation, bSweep=false,
        // SweepHitResult, bTeleport=true). bTeleport=true bypasses sweep
        // and interpolation — the pawn warps instantly. UE4 replicates
        // the new position to clients via standard pawn movement
        // replication.
        bool npcTeleportPawn(UObject* pawn, float x, float y, float z)
        {
            if (!pawn || !isObjectAlive(pawn)) return false;
            auto* fn = pawn->GetFunctionByNameInChain(STR("K2_SetActorLocation"));
            if (!fn) return false;

            auto* pNew   = findParam(fn, STR("NewLocation"));
            auto* pSweep = findParam(fn, STR("bSweep"));
            auto* pTele  = findParam(fn, STR("bTeleport"));
            if (!pNew || !pSweep || !pTele) return false;

            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            auto* loc = reinterpret_cast<float*>(buf.data() + pNew->GetOffset_Internal());
            loc[0] = x; loc[1] = y; loc[2] = z;
            *reinterpret_cast<bool*>(buf.data() + pSweep->GetOffset_Internal()) = false;
            *reinterpret_cast<bool*>(buf.data() + pTele->GetOffset_Internal())  = true;

            try { safeProcessEvent(pawn, fn, buf.data()); } catch (...) { return false; }
            return true;
        }

        // Refresh the controller cache. Called every 5 s — controllers
        // come and go slowly (bubble streaming), no need for per-tick
        // FindAllOf calls.
        void npcRefreshControllerCache()
        {
            m_npcControllerCache.clear();
            std::vector<UObject*> ctrls;
            findAllOfSafe(STR("MorAIController"), ctrls);
            for (UObject* c : ctrls)
            {
                if (!c || !isObjectAlive(c)) continue;
                std::wstring cls = safeClassName(c);
                // Cheap prefix filter: friendly dwarf NPC controllers.
                // "BP_AiController_Npc" — lowercase 'i'. Excludes orcs
                // (BP_AIController_*), watcher, grendel, goat (different
                // prefixes entirely).
                if (cls.size() < 19) continue;
                if (cls.substr(0, 19) != STR("BP_AiController_Npc")) continue;
                m_npcControllerCache.emplace_back(c);
            }

            // Lazy cleanup: drop state entries whose controller is no
            // longer in the cache.
            if (!m_npcRecoveryStates.empty())
            {
                std::set<UObject*> live;
                for (auto& w : m_npcControllerCache)
                {
                    UObject* c = w.Get();
                    if (c) live.insert(c);
                }
                for (auto it = m_npcRecoveryStates.begin(); it != m_npcRecoveryStates.end(); )
                {
                    if (live.find(it->first) == live.end())
                        it = m_npcRecoveryStates.erase(it);
                    else
                        ++it;
                }
            }
        }

        // Production tick. Per the design budget: throttled to 1 Hz,
        // controller-cache refresh every 5 s, no PE in the hot path
        // (only on actual teleport, which is rare).
        void tickNpcRecovery()
        {
            if (!m_npcRecoveryEnabled) return;
            if (!m_characterLoaded && !m_isDedicatedServer) return;

            // Authority-only: skip on remote clients. The server is
            // the only one whose teleport will replicate.
            if (!m_isDedicatedServer)
            {
                // Listen-server host: m_localPC alive + pawn alive
                // implies authority for this player's controllers.
                // For now we run on any host that has a local player —
                // the server-fly sweep uses the same gate.
                if (!m_localPC) return;
            }

            ULONGLONG now = GetTickCount64();
            if (now - m_lastNpcRecoveryTickMs < 1000) return;  // 1 Hz tick
            m_lastNpcRecoveryTickMs = now;

            // Refresh controller cache every 5 s.
            if (now - m_lastNpcCacheRefreshMs >= 5000)
            {
                m_lastNpcCacheRefreshMs = now;
                npcRefreshControllerCache();
            }

            if (m_npcControllerCache.empty()) return;

            for (auto& wctrl : m_npcControllerCache)
            {
                UObject* ctrl = wctrl.Get();
                if (!ctrl || !isObjectAlive(ctrl)) continue;

                // Pawn (direct reflective read — confirmed working in rc.1).
                auto* pawnPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
                UObject* pawn = (pawnPtr && *pawnPtr) ? *pawnPtr : nullptr;
                if (!pawn || !isObjectAlive(pawn)) continue;

                // FSM component → leaf active state.
                auto* fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
                UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
                UObject* leaf = npcFindLeafActiveState(fsmComp);
                if (!leaf) continue;

                // Diagnostic: one-shot log per new leaf class so we can
                // map the state-name landscape as the user plays.
                if (s_verbose)
                {
                    std::wstring leafCls = safeClassName(leaf);
                    if (m_npcLeafClassesSeen.insert(leafCls).second)
                    {
                        VLOG(STR("[NpcRecovery] new leaf state class observed: '{}'\n"),
                             leafCls.c_str());
                    }
                }

                // Path-following test: leaf has a 'Destination' UPROPERTY.
                // Idle/Talk/Work/EQS states don't expose this; only the
                // FGKBehaviorState_MoveTo family does.
                auto* destPtr = leaf->GetValuePtrByPropertyNameInChain<float>(STR("Destination"));
                if (!destPtr)
                {
                    // Not a MoveTo state — clear stuck tracking for this
                    // controller. Next time it enters MoveTo we'll start
                    // a fresh timer.
                    auto it = m_npcRecoveryStates.find(ctrl);
                    if (it != m_npcRecoveryStates.end())
                        it->second.initialized = false;
                    continue;
                }

                // Read pawn location via direct memory.
                float px, py, pz;
                if (!npcReadPawnLocation(pawn, px, py, pz)) continue;

                // Get-or-create state entry.
                NpcRecoveryEntry& entry = m_npcRecoveryStates[ctrl];
                if (!entry.initialized)
                {
                    entry.controller = RC::Unreal::FWeakObjectPtr(ctrl);
                    entry.pawn = RC::Unreal::FWeakObjectPtr(pawn);
                    entry.lastX = px; entry.lastY = py; entry.lastZ = pz;
                    entry.lastProgressTickMs = now;
                    entry.initialized = true;
                    continue;
                }

                // Did the pawn make progress? Compute squared distance to
                // avoid a sqrt; threshold² is 100*100 = 10000.
                float dx = px - entry.lastX;
                float dy = py - entry.lastY;
                float dz = pz - entry.lastZ;
                float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > NPC_PROGRESS_THRESHOLD_CM * NPC_PROGRESS_THRESHOLD_CM)
                {
                    entry.lastProgressTickMs = now;
                    entry.lastX = px; entry.lastY = py; entry.lastZ = pz;
                    continue;
                }

                // Pawn is not making progress. Has stuck time exceeded threshold?
                ULONGLONG stuckElapsed = now - entry.lastProgressTickMs;
                if (stuckElapsed < m_npcStuckThresholdMs) continue;

                // Throttle: don't teleport same NPC more than once every 30 s.
                if (entry.lastTeleportTickMs != 0 &&
                    now - entry.lastTeleportTickMs < NPC_TELEPORT_THROTTLE_MS)
                    continue;

                // Stuck. Read destination + teleport.
                float dx_t = destPtr[0], dy_t = destPtr[1], dz_t = destPtr[2];
                std::wstring leafCls = safeClassName(leaf);
                std::wstring ctrlCls = safeClassName(ctrl);

                bool ok = npcTeleportPawn(pawn, dx_t, dy_t, dz_t);

                VLOG(STR("[NpcRecovery] {} teleported pawn ({}) "
                         "from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f}) "
                         "after {}s stuck in state '{}' (result={})\n"),
                     ctrlCls.c_str(), safeClassName(pawn).c_str(),
                     px, py, pz, dx_t, dy_t, dz_t,
                     (unsigned)(stuckElapsed / 1000), leafCls.c_str(),
                     ok ? STR("OK") : STR("FAILED"));

                entry.lastTeleportTickMs = now;
                entry.lastProgressTickMs = now;  // reset stuck timer
                entry.lastX = px; entry.lastY = py; entry.lastZ = pz;
            }
        }

        // Walk a UStruct's full property chain and log each property's
        // name + type + offset. Used at probe time to discover the
        // unknown UPROPERTY names on UFGKActorFSMComponent.
        void npcProbeLogPropertyChain(UObject* obj, const wchar_t* label)
        {
            if (!obj || !isObjectAlive(obj)) return;
            UClass* cls = nullptr;
            try { cls = obj->GetClassPrivate(); } catch (...) {}
            if (!cls) return;
            std::wstring clsName;
            try { clsName = cls->GetName(); } catch (...) { clsName = STR("?"); }
            VLOG(STR("[NpcProbe] {} obj={:p} class={}\n"), label, (void*)obj, clsName.c_str());

            int propCount = 0;
            try {
                for (auto* prop : cls->ForEachPropertyInChain())
                {
                    if (!prop) continue;
                    std::wstring propName;
                    try { propName = std::wstring(prop->GetName()); } catch (...) {}
                    int32_t off = 0;
                    try { off = prop->GetOffset_Internal(); } catch (...) {}
                    VLOG(STR("[NpcProbe]   prop[{}] off={:#06x} name={}\n"),
                         propCount, (unsigned)off, propName.c_str());
                    if (++propCount >= 80) {
                        VLOG(STR("[NpcProbe]   ... (truncated at 80 properties)\n"));
                        break;
                    }
                }
            } catch (...) {}
            VLOG(STR("[NpcProbe] {} total properties logged: {}\n"), label, propCount);
        }

        // PHASE 1 — read-only one-shot probe. Walks the first NPC dwarf
        // controller it can find, dumps everything we need to confirm
        // open implementation TODOs #1 / #2 / #3.
        //
        // Throttled: only fires when s_verbose is true AND the probe
        // hasn't completed yet. Authority-only — runs on host or dedi,
        // skipped on remote clients (controllers there are proxies,
        // their FSM state is not authoritative).
        void tickNpcRecoveryProbe()
        {
            if (m_npcProbeDone) return;
            if (!s_verbose) return;
            if (!m_characterLoaded && !m_isDedicatedServer) return;

            // Throttle: only attempt once every 5 seconds while waiting
            // for an NPC to surface. Bubble streaming may not have
            // populated the controller list at character-load time.
            static ULONGLONG s_lastProbeAttemptMs = 0;
            ULONGLONG now = GetTickCount64();
            if (now - s_lastProbeAttemptMs < 5000) return;
            s_lastProbeAttemptMs = now;

            std::vector<UObject*> controllers;
            findAllOfSafe(STR("MorAIController"), controllers);

            // Filter to friendly NPC dwarves only. Cheap class-name
            // prefix match: dwarf controllers start with
            // "BP_AiController_Npc" (lowercase 'i'); orcs / goblins /
            // grendel / watcher / goat use uppercase 'I' or different
            // prefixes entirely.
            UObject* probeCtrl = nullptr;
            std::wstring probeCls;
            for (UObject* ctrl : controllers)
            {
                if (!ctrl || !isObjectAlive(ctrl)) continue;
                std::wstring cls = safeClassName(ctrl);
                if (cls.size() < 19) continue;
                if (cls.substr(0, 19) != STR("BP_AiController_Npc")) continue;
                probeCtrl = ctrl;
                probeCls = cls;
                break;
            }

            if (!probeCtrl)
            {
                VLOG(STR("[NpcProbe] no friendly NPC dwarf controller found yet "
                         "(scanned {} MorAIController subclasses)\n"),
                     (int)controllers.size());
                return;
            }

            VLOG(STR("[NpcProbe] ====== NPC RECOVERY DIAGNOSTIC PROBE ======\n"));
            VLOG(STR("[NpcProbe] target controller: {} at {:p}\n"),
                 probeCls.c_str(), (void*)probeCtrl);

            // ---- TODO #2: is AAIController.Pawn reflectively accessible? ----
            UObject* pawn = nullptr;
            {
                auto* pawnPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
                if (pawnPtr && *pawnPtr) pawn = *pawnPtr;
                VLOG(STR("[NpcProbe] [#2] AAIController.Pawn reflective read: ptr={:p} pawn={:p}\n"),
                     (void*)pawnPtr, (void*)pawn);
            }
            if (!pawn)
            {
                VLOG(STR("[NpcProbe] [#2] FALLBACK: trying K2_GetPawn() UFunction dispatch\n"));
                if (auto* fn = probeCtrl->GetFunctionByNameInChain(STR("K2_GetPawn")))
                {
                    auto* pRet = findParam(fn, STR("ReturnValue"));
                    if (pRet)
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                        try { safeProcessEvent(probeCtrl, fn, buf.data()); } catch (...) {}
                        pawn = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                        VLOG(STR("[NpcProbe] [#2] K2_GetPawn returned pawn={:p}\n"),
                             (void*)pawn);
                    }
                }
            }

            if (pawn && isObjectAlive(pawn))
            {
                std::wstring pawnCls = safeClassName(pawn);
                VLOG(STR("[NpcProbe] pawn class: {}\n"), pawnCls.c_str());

                // ---- pawn->RootComponent->RelativeLocation direct read ----
                auto* rcPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
                UObject* rootComp = (rcPtr && *rcPtr) ? *rcPtr : nullptr;
                VLOG(STR("[NpcProbe] pawn->RootComponent: ptr={:p} comp={:p}\n"),
                     (void*)rcPtr, (void*)rootComp);
                if (rootComp && isObjectAlive(rootComp))
                {
                    auto* relLocPtr = rootComp->GetValuePtrByPropertyNameInChain<float>(STR("RelativeLocation"));
                    if (relLocPtr)
                    {
                        VLOG(STR("[NpcProbe] pawn loc (direct mem): ({:.1f}, {:.1f}, {:.1f})\n"),
                             relLocPtr[0], relLocPtr[1], relLocPtr[2]);
                    }
                }

                // PE-fallback location read for cross-check.
                if (auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
                {
                    auto* pRet = findParam(fn, STR("ReturnValue"));
                    if (pRet)
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                        try { safeProcessEvent(pawn, fn, buf.data()); } catch (...) {}
                        auto* peLoc = reinterpret_cast<float*>(buf.data() + pRet->GetOffset_Internal());
                        VLOG(STR("[NpcProbe] pawn loc (PE K2_GetActorLocation): ({:.1f}, {:.1f}, {:.1f})\n"),
                             peLoc[0], peLoc[1], peLoc[2]);
                    }
                }
            }

            // ---- BehaviorFSMComp ----
            auto* fsmPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
            UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
            VLOG(STR("[NpcProbe] controller->BehaviorFSMComp: ptr={:p} comp={:p}\n"),
                 (void*)fsmPtr, (void*)fsmComp);

            if (fsmComp && isObjectAlive(fsmComp))
            {
                // ---- TODO #1 (rc.2): the FSM doesn't expose CurrentState. ----
                // Properties dumped in rc.1: FSMRoot, AllStates, StateMap,
                // RunTimeStates. Active-state detection requires walking
                // these structures.

                // (a) FSMRoot — UFGKState*. Dump its property surface to
                //     understand what UFGKState exposes (likely bIsActive,
                //     Children, etc.).
                {
                    auto* rootPtr = fsmComp->GetValuePtrByPropertyNameInChain<UObject*>(STR("FSMRoot"));
                    UObject* root = (rootPtr && *rootPtr) ? *rootPtr : nullptr;
                    VLOG(STR("[NpcProbe] [#1a] FSMRoot ptr={:p} obj={:p}\n"),
                         (void*)rootPtr, (void*)root);
                    if (root && isObjectAlive(root))
                    {
                        npcProbeLogPropertyChain(root, STR("[#1a] FSMRoot"));
                    }
                }

                // (b) AllStates — TArray<UFGKState*>. Dump first ~10 entries:
                //     class name + check for an active flag.
                {
                    auto* allStatesPtr =
                        fsmComp->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("AllStates"));
                    if (allStatesPtr)
                    {
                        int n = allStatesPtr->Num();
                        VLOG(STR("[NpcProbe] [#1b] AllStates.Num() = {}\n"), n);
                        int dumpLimit = (n > 10) ? 10 : n;
                        for (int i = 0; i < dumpLimit; ++i)
                        {
                            UObject* state = (*allStatesPtr)[i];
                            if (!state || !isObjectAlive(state)) {
                                VLOG(STR("[NpcProbe] [#1b]   AllStates[{}] = null/dead\n"), i);
                                continue;
                            }
                            std::wstring sCls = safeClassName(state);
                            VLOG(STR("[NpcProbe] [#1b]   AllStates[{}] = {:p} class={}\n"),
                                 i, (void*)state, sCls.c_str());

                            // Probe candidate "is active" field names on each state.
                            const wchar_t* activeFlags[] = {
                                STR("bIsActive"), STR("bActive"), STR("bIsCurrent"),
                                STR("bIsRunning"), STR("bEntered"),
                            };
                            for (const wchar_t* fname : activeFlags) {
                                auto* fptr = state->GetValuePtrByPropertyNameInChain<bool>(fname);
                                if (fptr) {
                                    VLOG(STR("[NpcProbe] [#1b]     {}={}  (field '{}')\n"),
                                         fname, *fptr ? STR("TRUE") : STR("false"), fname);
                                }
                            }

                            // Dump first state fully so we see UFGKState's complete shape.
                            if (i == 0) {
                                npcProbeLogPropertyChain(state, STR("[#1b] AllStates[0] full prop dump"));
                            }
                        }
                    } else {
                        VLOG(STR("[NpcProbe] [#1b] AllStates property not reachable\n"));
                    }
                }

                // (c) RunTimeStates — alternate access path (TArray).
                {
                    auto* rtsPtr =
                        fsmComp->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("RunTimeStates"));
                    if (rtsPtr)
                    {
                        VLOG(STR("[NpcProbe] [#1c] RunTimeStates.Num() = {}\n"), rtsPtr->Num());
                        int dumpLimit = (rtsPtr->Num() > 5) ? 5 : rtsPtr->Num();
                        for (int i = 0; i < dumpLimit; ++i)
                        {
                            UObject* state = (*rtsPtr)[i];
                            if (!state || !isObjectAlive(state)) continue;
                            std::wstring sCls = safeClassName(state);
                            VLOG(STR("[NpcProbe] [#1c]   RunTimeStates[{}] = {:p} class={}\n"),
                                 i, (void*)state, sCls.c_str());
                        }
                    }
                }
            }

            // ---- TODO #3 (rc.2): re-aimed. PathFollowingComponent lives ----
            // on the AIController in standard UE4, not the pawn. Probe both
            // sides + try Mor-prefixed alt names.
            {
                struct ProbeTarget { UObject* obj; const wchar_t* label; };
                ProbeTarget targets[] = {
                    { probeCtrl, STR("[#3a] controller->PathFollowingComponent") },
                    { probeCtrl, STR("[#3a-alt] controller->MorPathFollowingComponent") },
                    { pawn,      STR("[#3b] pawn->PathFollowingComponent") },
                    { pawn,      STR("[#3b-alt] pawn->MorPathFollowingComponent") },
                };
                const wchar_t* propNames[] = {
                    STR("PathFollowingComponent"),
                    STR("MorPathFollowingComponent"),
                    STR("PathFollowingComponent"),
                    STR("MorPathFollowingComponent"),
                };
                for (size_t i = 0; i < 4; ++i)
                {
                    if (!targets[i].obj || !isObjectAlive(targets[i].obj)) continue;
                    auto* pfcPtr = targets[i].obj->GetValuePtrByPropertyNameInChain<UObject*>(propNames[i]);
                    UObject* pfc = (pfcPtr && *pfcPtr) ? *pfcPtr : nullptr;
                    VLOG(STR("[NpcProbe] {}: ptr={:p} comp={:p}\n"),
                         targets[i].label, (void*)pfcPtr, (void*)pfc);
                    if (pfc && isObjectAlive(pfc))
                    {
                        npcProbeLogPropertyChain(pfc, targets[i].label);
                        // After first successful resolve, stop walking — both
                        // alts on the same object would dump the same thing.
                        break;
                    }
                }
            }

            VLOG(STR("[NpcProbe] ====== probe complete; m_npcProbeDone=true ======\n"));
            m_npcProbeDone = true;
        }
