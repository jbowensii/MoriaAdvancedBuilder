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
            // rc.5: remembered destination across leaf-state transitions.
            // Blocked NPCs cycle MoveToBlackboardKey ↔ Idle/TakeMeal too
            // fast for a leaf-scoped timer — we cache the last seen path
            // destination so we can teleport even when the controller is
            // currently between path-follow attempts.
            float lastDestX{0.0f}, lastDestY{0.0f}, lastDestZ{0.0f};
            ULONGLONG lastDestSeenTickMs{0};
            ULONGLONG lastStatusLogMs{0};   // rc.6 verbose throttle
            // rc.18: bed-walk no-match cooldown. If our last scan
            // found no bed for this NPC, don't rescan for this many
            // seconds — avoids the per-tick scan loop that caused lag.
            ULONGLONG lastBedScanFailMs{0};
            // rc.18: cached matched bed (FWeakObjectPtr so it auto-
            // invalidates on GC). If set + alive, skip the walk.
            RC::Unreal::FWeakObjectPtr cachedAssignedBed;
            bool hasDestination{false};
            bool initialized{false};
        };
        std::unordered_map<UObject*, NpcRecoveryEntry> m_npcRecoveryStates;
        std::vector<RC::Unreal::FWeakObjectPtr> m_npcControllerCache;

        ULONGLONG m_lastNpcRecoveryTickMs{0};
        ULONGLONG m_lastNpcCacheRefreshMs{0};
        // rc.28: post-load sweep state. NPCs already in CantReachBed
        // when the world loads fire their SetCurrentActivity BEFORE
        // our PE post-hook is registered, so the event-driven path
        // misses them.
        //
        // rc.41: changed from one-shot at +30 s to recurring every
        // 30 s for the first 5 minutes after character-load. Live
        // log showed an NPC transitioning NoBed → CantReachBed at
        // +70 s, which the one-shot at +30 s missed entirely.
        ULONGLONG m_npcPostLoadSweepNextMs{0};
        ULONGLONG m_npcPostLoadSweepEndMs{0};

        bool m_npcRecoveryEnabled{true};        // rc.3: hardcoded ON for testing; rc.4 adds Settings toggle
        ULONGLONG m_npcStuckThresholdMs{5000};   // rc.6: 5 s default (was 10 s) — faster response on the user's blocked-bed scenario
        static constexpr ULONGLONG NPC_TELEPORT_THROTTLE_MS = 30000;  // 30s between teleports per NPC
        static constexpr float NPC_PROGRESS_THRESHOLD_CM = 100.0f;     // 1m of movement = "making progress"
        // rc.5: how recently we must have observed a path-follow
        // destination for it to count as a valid teleport target. Blocked
        // NPCs cycle MoveToBlackboardKey ↔ Idle every ~1-3 s; 30 s is
        // plenty of slack to ride out the gaps without firing on stale
        // destinations after the NPC genuinely abandoned the path.
        static constexpr ULONGLONG NPC_DEST_FRESHNESS_MS = 30000;
        // rc.9: skip teleport if pawn is already within this distance
        // of its target. Prevents pointless snap-warps when an NPC is
        // standing right next to its bed but the FSM hasn't formally
        // marked the path complete.
        static constexpr float NPC_PROXIMITY_SKIP_CM = 500.0f;

        // Diagnostic: log each NEW leaf-state class name once per session
        // so we can map the state-name landscape as the user plays.
        std::set<std::wstring> m_npcLeafClassesSeen;
        // rc.5: log each unique (state-class × Block*-property) pair
        // once so we can chart where "Blocked" surfaces reflectively.
        std::set<std::wstring> m_npcBlockedPropsSeen;
        // rc.7: log each unique (class × discovery-keyword-property)
        // pair once so we can chart where assigned bed / home / target
        // / etc references surface on the controller and pawn.
        std::set<std::wstring> m_npcDiscoveryPropsSeen;
        // rc.11: log each unique CurrentActivity row name once.
        std::set<std::wstring> m_npcActivityNamesSeen;

        // rc.7: scan a UObject's full property surface (own + inherited)
        // for any UPROPERTY whose name contains one of a set of
        // settlement-assignment keywords. One-shot logged per
        // (class × property-name × scan-target-label).
        void npcDiscoveryProbe(UObject* ctrl, UObject* pawn)
        {
            // rc.10: widened keyword set. The user reports a "Blocked"
            // status visible in the NPC interact menu — that data must
            // surface through a property somewhere reachable. Original
            // rc.7 keywords focused on destination/assignment; this
            // adds the menu/choice/availability/status surface.
            static const wchar_t* kKeywords[] = {
                STR("Bed"), STR("Home"), STR("Sleep"), STR("Rest"),
                STR("Workstation"), STR("Job"), STR("Task"),
                STR("Assigned"), STR("Owner"), STR("Settlement"),
                STR("Target"), STR("Goal"), STR("Destination"),
                // rc.10 additions:
                STR("Interact"), STR("Choice"), STR("Option"),
                STR("Action"), STR("Available"),
                STR("Status"), STR("State"), STR("Reason"),
                STR("Block"), STR("Cant"), STR("Disable"),
                STR("Need"), STR("Mood"), STR("Want"), STR("Issue"),
                STR("Worker"), STR("Behavior"),
            };
            auto containsKeywordCI = [](const std::wstring& name) -> bool {
                std::wstring lo; lo.reserve(name.size());
                for (wchar_t c : name) lo.push_back((wchar_t)towlower(c));
                for (const wchar_t* kw : kKeywords) {
                    std::wstring kwl; kwl.reserve(wcslen(kw));
                    for (size_t i = 0; kw[i]; ++i) kwl.push_back((wchar_t)towlower(kw[i]));
                    if (lo.find(kwl) != std::wstring::npos) return true;
                }
                return false;
            };

            auto scan = [&](UObject* obj, const wchar_t* tgtLabel) {
                if (!obj || !isObjectAlive(obj)) return;
                UClass* cls = nullptr;
                try { cls = obj->GetClassPrivate(); } catch (...) {}
                if (!cls) return;
                std::wstring clsName;
                try { clsName = cls->GetName(); } catch (...) { return; }
                try {
                    for (auto* prop : cls->ForEachPropertyInChain())
                    {
                        if (!prop) continue;
                        std::wstring pn;
                        try { pn = prop->GetName(); } catch (...) { continue; }
                        if (!containsKeywordCI(pn)) continue;
                        std::wstring oneShot = std::wstring(tgtLabel) + STR(":") + clsName + STR("::") + pn;
                        if (!m_npcDiscoveryPropsSeen.insert(oneShot).second) continue;
                        int32 off = -1;
                        try { off = prop->GetOffset_Internal(); } catch (...) {}
                        VLOG(STR("[NpcRecovery] DISCOVER {} '{}::{}' off=0x{:04x}\n"),
                             tgtLabel, clsName.c_str(), pn.c_str(), (unsigned)off);
                    }
                } catch (...) {}
            };

            scan(ctrl, STR("ctrl"));
            scan(pawn, STR("pawn"));

            // rc.10: also scan the FSM component (it's a separate
            // UObject hung off the controller). Behavior status flags
            // — including potential "Blocked" — are sometimes parked
            // on the FSM root rather than the controller class.
            if (ctrl)
            {
                auto* fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
                UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
                if (fsmComp) scan(fsmComp, STR("fsm"));
            }

            // rc.15: also scan UMorNPCComponent on the pawn ("NPC" at
            // offset 0xF98 per CXXHeaderDump). InteractedBed on the
            // pawn is null for NPCs blocked from a bed they've never
            // reached — the assigned bed reference must live somewhere
            // else, possibly on this component.
            if (pawn)
            {
                auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
                UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
                if (npcComp) scan(npcComp, STR("npccomp"));
            }
        }

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

        // rc.43: PE dispatch K2_TeleportTo(DestLocation, DestRotation)
        // INSTEAD OF K2_SetActorLocation. K2_TeleportTo is the proper
        // character-aware teleport — it does one collision sweep and
        // commits the move via the movement component, so the
        // CharacterMovementComponent re-anchor logic doesn't snap the
        // pawn back to its previous nav-mesh point on the next tick.
        //
        // rc.0 → rc.42 used K2_SetActorLocation(bTeleport=true) which
        // reliably updates RootComponent.RelativeLocation (our
        // post-teleport read-backs confirmed this) but for Characters
        // the CMC's UpdatedComponent re-syncs from the capsule's
        // physics state on next tick, defeating the move visually.
        // K2_TeleportTo skips that path.
        bool npcTeleportPawn(UObject* pawn, float x, float y, float z)
        {
            if (!pawn || !isObjectAlive(pawn)) return false;

            // Try K2_TeleportTo first.
            auto* fn = pawn->GetFunctionByNameInChain(STR("K2_TeleportTo"));
            if (fn)
            {
                auto* pDest = findParam(fn, STR("DestLocation"));
                auto* pRot  = findParam(fn, STR("DestRotation"));
                auto* pRet  = findParam(fn, STR("ReturnValue"));
                if (pDest)
                {
                    std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                    auto* loc = reinterpret_cast<float*>(buf.data() + pDest->GetOffset_Internal());
                    loc[0] = x; loc[1] = y; loc[2] = z;
                    // DestRotation defaults to zero (FRotator{0,0,0}).
                    if (pRot) {
                        auto* rot = reinterpret_cast<float*>(buf.data() + pRot->GetOffset_Internal());
                        rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f;
                    }
                    try { safeProcessEvent(pawn, fn, buf.data()); } catch (...) { return false; }
                    // Optional ReturnValue check — K2_TeleportTo returns
                    // bool indicating whether the move succeeded. If
                    // false, fall through to K2_SetActorLocation below.
                    bool retVal = true;
                    if (pRet) {
                        bool* r = reinterpret_cast<bool*>(buf.data() + pRet->GetOffset_Internal());
                        if (isReadableMemory(r, 1)) retVal = *r;
                    }
                    if (retVal) return true;
                }
            }

            // Fallback: K2_SetActorLocation(NewLocation, bSweep=false, bTeleport=true).
            auto* fn2 = pawn->GetFunctionByNameInChain(STR("K2_SetActorLocation"));
            if (!fn2) return false;
            auto* pNew   = findParam(fn2, STR("NewLocation"));
            auto* pSweep = findParam(fn2, STR("bSweep"));
            auto* pTele  = findParam(fn2, STR("bTeleport"));
            if (!pNew || !pSweep || !pTele) return false;
            std::vector<uint8_t> buf(fn2->GetParmsSize(), 0);
            auto* loc = reinterpret_cast<float*>(buf.data() + pNew->GetOffset_Internal());
            loc[0] = x; loc[1] = y; loc[2] = z;
            *reinterpret_cast<bool*>(buf.data() + pSweep->GetOffset_Internal()) = false;
            *reinterpret_cast<bool*>(buf.data() + pTele->GetOffset_Internal())  = true;
            try { safeProcessEvent(pawn, fn2, buf.data()); } catch (...) { return false; }
            return true;
        }

        // SEH-wrapped 16-byte byte-scan helper. Plain C function body
        // (no destructors) so __try/__except is legal here.
        // Returns true if a 16-byte sequence matching `needle` was
        // found in [base, base+limit). Step is the alignment.
        static bool seh_scan_for_guid(const uint8_t* base, int32 limit,
                                      const uint8_t* needle) noexcept
        {
            __try {
                for (int32 off = 0; off + 16 <= limit; off += 4)
                {
                    if (std::memcmp(base + off, needle, 16) == 0) return true;
                }
                return false;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        // rc.26: event-driven trigger called from the PE post-hook on
        // UMorNPCComponent::SetCurrentActivity / MorNpcUpdateActivity
        // when the activity row name matches CantReachBed /
        // CantReachAssignedBed. The hook gives us the NPC component;
        // we resolve to pawn → controller → FSM destination → teleport.
        // Throttle still applies (NPC_TELEPORT_THROTTLE_MS, 30 s).
        void onNpcBlockedActivityEvent(UObject* npcComp)
        {
            if (!m_npcRecoveryEnabled) return;
            if (!npcComp || !isObjectAlive(npcComp)) return;
            // Authority gate — same as the polling tick. Server only.
            if (!m_isDedicatedServer && (!m_localPC || !isObjectAlive(m_localPC))) return;

            // pawn = npcComp->NPCCharacter (AMorCharacter*)
            auto* pawnPtr = npcComp->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPCCharacter"));
            UObject* pawn = (pawnPtr && *pawnPtr) ? *pawnPtr : nullptr;
            if (!pawn || !isObjectAlive(pawn)) return;

            // controller = pawn->Controller (AAIController*)
            auto* ctrlPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            UObject* ctrl = (ctrlPtr && *ctrlPtr) ? *ctrlPtr : nullptr;
            if (!ctrl || !isObjectAlive(ctrl)) return;

            // Filter to friendly NPC dwarf controllers (same prefix the
            // cache uses) — skip orcs / wildlife / unrelated AI.
            std::wstring cls = safeClassName(ctrl);
            if (cls.size() < 19 || cls.substr(0, 19) != STR("BP_AiController_Npc")) return;

            // Get-or-create the recovery state entry. Reuses the same
            // throttle + cached destination as the polling path.
            ULONGLONG now = GetTickCount64();
            NpcRecoveryEntry& entry = m_npcRecoveryStates[ctrl];
            if (!entry.initialized)
            {
                entry.controller = RC::Unreal::FWeakObjectPtr(ctrl);
                entry.pawn = RC::Unreal::FWeakObjectPtr(pawn);
                entry.initialized = true;
            }

            // Throttle: don't re-teleport same NPC within 30 s.
            if (entry.lastTeleportTickMs != 0 &&
                now - entry.lastTeleportTickMs < NPC_TELEPORT_THROTTLE_MS)
                return;

            // Resolve destination. Three paths in order of preference:
            //   (1) Cached path-dest — captured during a prior tick
            //       when the NPC was actively in MoveToBlackboardKey.
            //   (2) FSM tree walk — read TargetBlackboardKey actor or
            //       BlackboardKey vector from any MoveToBlackboardKey
            //       state in the tree (state may be dormant).
            //   (3) Bed-walk byte-scan — iterate AMorBed actors, find
            //       the one whose private NpcGuid matches our NPC's.
            //       Last-resort because it's a brute-force scan, but
            //       it's the only path that works when the FSM has
            //       been parked long enough that all MoveToBlackboardKey
            //       state was cleared.
            float dx_t = 0, dy_t = 0, dz_t = 0;
            const wchar_t* destSrc = STR("?");
            if (entry.hasDestination)
            {
                dx_t = entry.lastDestX; dy_t = entry.lastDestY; dz_t = entry.lastDestZ;
                destSrc = STR("cached");
            }
            else
            {
                auto* fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
                UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
                if (npcReadFsmMoveDest(fsmComp, dx_t, dy_t, dz_t))
                {
                    destSrc = STR("fsm-walk");
                    entry.lastDestX = dx_t; entry.lastDestY = dy_t; entry.lastDestZ = dz_t;
                    entry.lastDestSeenTickMs = now;
                    entry.hasDestination = true;
                }
                else
                {
                    UObject* bed = npcFindAssignedBed(pawn);
                    if (!bed || !isObjectAlive(bed))
                    {
                        // rc.42: guid-scan can't match — bed assignment
                        // isn't stored as a 16-byte FGuid in bed
                        // memory. Fall back to nearest unoccupied bed.
                        bed = npcFindNearestUnoccupiedBed(pawn);
                    }
                    if (!bed || !isObjectAlive(bed))
                    {
                        // rc.30 one-shot diag: dump on first total failure.
                        if (s_verbose)
                        {
                            static bool s_diagDumped = false;
                            if (!s_diagDumped) {
                                s_diagDumped = true;
                                npcDumpBedDiagnostic(pawn);
                            }
                            VLOG(STR("[NpcRecovery] EVENT-DRIVEN: no destination — fsm-walk + bed-walk + nearest-bed all failed for {}\n"),
                                 cls.c_str());
                        }
                        return;
                    }
                    if (!npcReadPawnLocation(bed, dx_t, dy_t, dz_t))
                    {
                        if (s_verbose)
                            VLOG(STR("[NpcRecovery] EVENT-DRIVEN: bed found but its location read failed for {}\n"),
                                 cls.c_str());
                        return;
                    }
                    destSrc = STR("bed-walk");
                    entry.lastDestX = dx_t; entry.lastDestY = dy_t; entry.lastDestZ = dz_t;
                    entry.lastDestSeenTickMs = now;
                    entry.hasDestination = true;
                    entry.cachedAssignedBed = RC::Unreal::FWeakObjectPtr(bed);
                }
            }

            float px, py, pz;
            if (!npcReadPawnLocation(pawn, px, py, pz)) return;

            bool ok = npcTeleportPawn(pawn, dx_t, dy_t, dz_t);
            entry.lastTeleportTickMs = now;
            entry.lastProgressTickMs = now;
            entry.lastX = px; entry.lastY = py; entry.lastZ = pz;

            if (s_verbose)
            {
                VLOG(STR("[NpcRecovery] EVENT-DRIVEN teleport ({}) src={} from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f}) result={}\n"),
                     cls.c_str(), destSrc, px, py, pz, dx_t, dy_t, dz_t,
                     ok ? STR("OK") : STR("FAILED"));
            }
            // Tell the caller (sweep counter) the teleport actually
            // fired vs. silently bailed on no-destination.
            m_lastEventTeleportFired = true;
        }
        bool m_lastEventTeleportFired{false};

        // rc.30: one-shot diagnostic when the bed-walk fails to find
        // a match for any NPC. Logs:
        //   (a) the NPC's NpcGuid bytes (proves it's populated, not all-zero)
        //   (b) the first bed's class + every UPROPERTY in its chain
        //       (so we can spot any field name like AssignedNpc /
        //       OwnerGuid / etc that we missed)
        //   (c) a hex dump of the first bed's first 0x300 bytes of
        //       instance memory (so we can eyeball whether any 16-byte
        //       window matches the NPC's guid pattern — confirms the
        //       byte-scan approach is or isn't viable)
        void npcDumpBedDiagnostic(UObject* pawn)
        {
            if (!pawn || !isObjectAlive(pawn)) return;

            // (a) NPC guid bytes
            auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
            UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
            if (npcComp && isObjectAlive(npcComp))
            {
                auto* g = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                if (g && isReadableMemory(g, 16))
                {
                    VLOG(STR("[NpcRecovery] DIAG NpcGuid bytes: {:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}\n"),
                         g[0],g[1],g[2],g[3], g[4],g[5],g[6],g[7],
                         g[8],g[9],g[10],g[11], g[12],g[13],g[14],g[15]);
                }
                else
                {
                    VLOG(STR("[NpcRecovery] DIAG NpcGuid: unreadable\n"));
                }
            }

            // (b) first bed's UPROPERTY chain
            std::vector<UObject*> beds;
            static const wchar_t* kBedClasses[] = {
                STR("BP_Bedroll_C"), STR("BP_Bed_Base_C"), STR("MorBed"),
            };
            for (const wchar_t* clsName : kBedClasses) {
                std::vector<UObject*> chunk;
                if (!findAllOfSafe(clsName, chunk)) continue;
                for (UObject* b : chunk) {
                    if (!b || !isObjectAlive(b)) continue;
                    std::wstring c = safeClassName(b);
                    if (c.size() >= 9 && c.substr(0,9) == STR("Default__")) continue;
                    beds.push_back(b);
                }
                if (!beds.empty()) break;
            }
            if (beds.empty()) {
                VLOG(STR("[NpcRecovery] DIAG: no live beds to probe\n"));
                return;
            }
            UObject* firstBed = beds[0];
            std::wstring bedCls = safeClassName(firstBed);
            VLOG(STR("[NpcRecovery] DIAG first bed: cls='{}' ptr={:p}\n"),
                 bedCls.c_str(), (void*)firstBed);

            UClass* bedClsObj = nullptr;
            try { bedClsObj = firstBed->GetClassPrivate(); } catch (...) {}
            if (bedClsObj) {
                int dumped = 0;
                try {
                    for (auto* prop : bedClsObj->ForEachPropertyInChain()) {
                        if (!prop) continue;
                        std::wstring pn;
                        try { pn = prop->GetName(); } catch (...) { continue; }
                        int32 off = -1;
                        try { off = prop->GetOffset_Internal(); } catch (...) {}
                        VLOG(STR("[NpcRecovery] DIAG bed prop[{}] off=0x{:04x} name='{}'\n"),
                             dumped, (unsigned)off, pn.c_str());
                        if (++dumped >= 200) {
                            VLOG(STR("[NpcRecovery] DIAG bed prop dump truncated at 200\n"));
                            break;
                        }
                    }
                } catch (...) {}
                VLOG(STR("[NpcRecovery] DIAG total bed properties logged: {}\n"), dumped);
            }
        }

        // rc.42: find the NEAREST UNOCCUPIED bed to the given pawn.
        //
        // The "assigned bed" data isn't reachable from reflection
        // anywhere we've checked (rc.30 diagnostic dumped 130+ UPROPERTYs
        // on AMorBed — no NpcGuid / Owner / Assigned* field. Byte-scan
        // for our NpcGuid in bed memory also returned no match across
        // 21+ beds). The assignment lives somewhere private (settlement
        // manager or raw C++ state).
        //
        // Pragmatic fallback: warp the NPC onto the nearest bed whose
        // `bIsBeingUsed` flag (UPROPERTY at offset 0x0620 on AMorBed) is
        // false. They end up on A bed, the game's settlement system
        // re-routes from there. Beats sitting frozen forever.
        UObject* npcFindNearestUnoccupiedBed(UObject* pawn)
        {
            if (!pawn || !isObjectAlive(pawn)) return nullptr;
            float px, py, pz;
            if (!npcReadPawnLocation(pawn, px, py, pz)) return nullptr;

            static const wchar_t* kBedClasses[] = {
                STR("BP_Bedroll_C"),
                STR("BP_Bed_Base_C"),
                STR("BP_Bed_Mansion_C"),  // rc.42: missed earlier
                STR("MorBed"),
            };
            UObject* best = nullptr;
            float bestDist2 = std::numeric_limits<float>::max();
            for (const wchar_t* clsName : kBedClasses)
            {
                std::vector<UObject*> chunk;
                if (!findAllOfSafe(clsName, chunk)) continue;
                for (UObject* bed : chunk)
                {
                    if (!bed || !isObjectAlive(bed)) continue;
                    std::wstring cls = safeClassName(bed);
                    if (cls.size() >= 9 && cls.substr(0,9) == STR("Default__")) continue;

                    // Skip occupied. UPROPERTY 'bIsBeingUsed' bool at 0x0620.
                    auto* busy = bed->GetValuePtrByPropertyNameInChain<bool>(STR("bIsBeingUsed"));
                    if (busy && *busy) continue;

                    float bx, by, bz;
                    if (!npcReadPawnLocation(bed, bx, by, bz)) continue;
                    float dx = bx - px, dy = by - py, dz = bz - pz;
                    float d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        best = bed;
                    }
                }
            }
            return best;
        }

        // rc.16: find the AMorBed assigned to the given NPC pawn.
        //
        // The bed-assignment data isn't reachable from the pawn /
        // controller / NPCComponent side — we audited every reflective
        // surface in rc.10/rc.15 and none expose the bed reference.
        // The assignment lives on the bed itself (private replicated
        // FGuid field, not in the header dumps).
        //
        // Approach: read the pawn's NpcGuid (16 bytes on
        // UMorNPCComponent at offset 0x178), enumerate all AMorBed
        // actors, and byte-scan each bed's instance memory for a
        // matching 16-byte sequence. False positives are
        // astronomically unlikely with 128-bit random guids.
        UObject* npcFindAssignedBed(UObject* pawn)
        {
            if (!pawn || !isObjectAlive(pawn)) return nullptr;
            auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
            UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
            if (!npcComp || !isObjectAlive(npcComp)) return nullptr;

            auto* myGuid = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
            if (!myGuid || !isReadableMemory(myGuid, 16)) return nullptr;

            // Sanity: don't search for an all-zero guid (unset NPC),
            // it'd match every uninitialized FGuid field in every bed.
            bool allZero = true;
            for (int i = 0; i < 16; ++i) if (myGuid[i] != 0) { allZero = false; break; }
            if (allZero) return nullptr;

            // rc.17: enumerate beds by all known subclass names.
            // FindAllOf is exact-match (per ue4ss-class-enumeration.md);
            // querying "MorBed" alone returned 0 because every actual
            // bed instance is a subclass (BP_Bedroll_C / BP_Bed_Base_C).
            // Same pattern the controller cache uses.
            std::vector<UObject*> beds;
            static const wchar_t* kBedClasses[] = {
                STR("BP_Bedroll_C"),
                STR("BP_Bed_Base_C"),
                STR("MorBed"),
            };
            for (const wchar_t* clsName : kBedClasses)
            {
                std::vector<UObject*> chunk;
                if (!findAllOfSafe(clsName, chunk)) continue;
                for (UObject* b : chunk) {
                    if (!b || !isObjectAlive(b)) continue;
                    std::wstring c = safeClassName(b);
                    if (c.size() >= 9 && c.substr(0,9) == STR("Default__")) continue;
                    beds.push_back(b);
                }
            }

            // Verbose diagnostic — once per scan, log bed count and
            // first bed's class. Tells us immediately if enumeration
            // is finding anything.
            if (s_verbose) {
                static size_t s_lastLoggedBedCount = (size_t)-1;
                if (beds.size() != s_lastLoggedBedCount) {
                    s_lastLoggedBedCount = beds.size();
                    std::wstring firstCls = beds.empty() ? std::wstring(STR("(none)"))
                                                         : safeClassName(beds[0]);
                    VLOG(STR("[NpcRecovery] bed-walk found {} bed actor(s); first cls='{}'\n"),
                         (int)beds.size(), firstCls.c_str());
                }
            }

            // Cap scan range — AMorBed CXXHeaderDump shows class size
            // 0x8B0; BP subclass adds ~UberGraph + a few component
            // fields, round up to 0xC00. Step by 4 bytes (FGuid is
            // 4-byte aligned in UE4 layout).
            constexpr int32 kScanLimit = 0xC00;
            constexpr int32 kStep = 4;

            // rc.18: SEH-wrap the whole scan, NO per-step
            // isReadableMemory. Per-step VirtualQuery costs ~50µs each
            // and at 768 steps × 30 beds × 9 NPCs = 200K syscalls/sec
            // — that was the lag source. The bed object's instance
            // memory is contiguously allocated by UE4; if any 4-byte
            // window is unreadable, the whole object is dead and SEH
            // catches the AV. One try/catch per bed instead of 768.
            for (UObject* bed : beds)
            {
                if (!bed || !isObjectAlive(bed)) continue;
                std::wstring cls = safeClassName(bed);
                if (cls.size() >= 9 && cls.substr(0, 9) == STR("Default__")) continue;

                if (seh_scan_for_guid(reinterpret_cast<const uint8_t*>(bed),
                                      kScanLimit, myGuid))
                    return bed;
            }
            return nullptr;
        }

        // rc.19: recursively walk the FSM state tree and read the
        // FIRST FGKBehaviorState_MoveTo* / MoveToBlackboardKey
        // instance's Destination field, regardless of whether that
        // state is currently the active leaf.
        //
        // Rationale: blocked NPCs cycle between Emote/Idle/RunEQS as
        // their leaf — they never enter MoveToBlackboardKey while
        // we're polling. Status logs show hasDest=N for the entire
        // session, despite the NPC clearly having a target (the bed
        // they can't reach). The MoveToBlackboardKey state OBJECT
        // exists in the FSM's state tree (we saw 8 sibling states
        // in rc.1's AllStates dump) and retains its Destination
        // FVector — it just isn't the currently-active leaf.
        //
        // Walk: state -> Children TArray -> recurse. Cap depth at 16
        // for self-loop safety.
        // rc.20: GetValueAsVector on a UBlackboardComponent for a
        // given key name. The MoveToBlackboardKey state references its
        // destination via name; the actual FVector lives in the
        // blackboard and persists across state activations. The state
        // object's Destination cache resets to FLT_MAX when dormant
        // (rc.19 hit this bug), but the blackboard key value is stable.
        bool npcGetBlackboardVector(UObject* blackboardComp, RC::Unreal::FName keyName,
                                    float& ox, float& oy, float& oz)
        {
            if (!blackboardComp || !isObjectAlive(blackboardComp)) return false;
            auto* fn = blackboardComp->GetFunctionByNameInChain(STR("GetValueAsVector"));
            if (!fn) return false;
            auto* pKey = findParam(fn, STR("KeyName"));
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pKey || !pRet) return false;
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            *reinterpret_cast<RC::Unreal::FName*>(buf.data() + pKey->GetOffset_Internal()) = keyName;
            try { safeProcessEvent(blackboardComp, fn, buf.data()); } catch (...) { return false; }
            float* v = reinterpret_cast<float*>(buf.data() + pRet->GetOffset_Internal());
            if (!isReadableMemory(v, sizeof(float) * 3)) return false;
            // Reject FLT_MAX sentinel and (0,0,0).
            const float kSentinel = 1e30f;
            if (v[0] >  kSentinel || v[1] >  kSentinel || v[2] >  kSentinel) return false;
            if (v[0] < -kSentinel || v[1] < -kSentinel || v[2] < -kSentinel) return false;
            if (v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f) return false;
            ox = v[0]; oy = v[1]; oz = v[2];
            return true;
        }

        // rc.27: GetValueAsObject on a UBlackboardComponent for a
        // given key name. MoveToBlackboardKey states sometimes carry
        // a TargetBlackboardKeyName that points to an actor (the bed,
        // workstation, etc.) rather than just a vector waypoint.
        // Reading the actor and using ITS location is more reliable
        // than the vector key — the vector can be a path-end-point
        // that's already where the NPC is stuck (we hit this in
        // rc.26 testing: dest equalled current location, teleport
        // moved nothing).
        bool npcGetBlackboardActor(UObject* blackboardComp, RC::Unreal::FName keyName,
                                   UObject*& outActor)
        {
            outActor = nullptr;
            if (!blackboardComp || !isObjectAlive(blackboardComp)) return false;
            auto* fn = blackboardComp->GetFunctionByNameInChain(STR("GetValueAsObject"));
            if (!fn) return false;
            auto* pKey = findParam(fn, STR("KeyName"));
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pKey || !pRet) return false;
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            *reinterpret_cast<RC::Unreal::FName*>(buf.data() + pKey->GetOffset_Internal()) = keyName;
            try { safeProcessEvent(blackboardComp, fn, buf.data()); } catch (...) { return false; }
            UObject** pp = reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            if (!isReadableMemory(pp, sizeof(UObject*))) return false;
            UObject* obj = *pp;
            if (!obj || !isObjectAlive(obj)) return false;
            outActor = obj;
            return true;
        }

        bool npcReadFsmMoveDestRec(UObject* state, float& ox, float& oy, float& oz, int depth)
        {
            if (!state || !isObjectAlive(state) || depth > 16) return false;
            std::wstring cls = safeClassName(state);

            // MoveToBlackboardKey state has THREE potential sources for
            // the destination, in order of preference:
            //
            //   (1) TargetBlackboardKeyName → blackboard actor → its
            //       world location. Most reliable for "blocked from
            //       bed" — gives us the actual bed actor's location,
            //       not a path waypoint.
            //   (2) BlackboardKeyName → blackboard vector. The path's
            //       target FVector. Can be stale (game gives up at the
            //       obstacle, parking the dest where the NPC ended).
            //   (3) state.Destination FVector. Only valid while active.
            if (cls.find(STR("MoveToBlackboardKey")) != std::wstring::npos)
            {
                // (1) Try TargetBlackboardKey → actor location.
                auto* tgtKeyPtr  = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(STR("TargetBlackboardKeyName"));
                auto* bbCompPtr2 = state->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComponent"));
                if (tgtKeyPtr && bbCompPtr2 && *bbCompPtr2)
                {
                    UObject* tgtActor = nullptr;
                    if (npcGetBlackboardActor(*bbCompPtr2, *tgtKeyPtr, tgtActor) && tgtActor)
                    {
                        if (npcReadPawnLocation(tgtActor, ox, oy, oz))
                            return true;
                    }
                }
                // (2) Fallback to vector key.
                auto* keyNamePtr = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(STR("BlackboardKeyName"));
                if (keyNamePtr && bbCompPtr2 && *bbCompPtr2)
                {
                    if (npcGetBlackboardVector(*bbCompPtr2, *keyNamePtr, ox, oy, oz))
                        return true;
                }
            }
            if (cls.find(STR("MoveTo")) != std::wstring::npos)
            {
                auto* destPtr = state->GetValuePtrByPropertyNameInChain<float>(STR("Destination"));
                if (destPtr)
                {
                    float x = destPtr[0], y = destPtr[1], z = destPtr[2];
                    // Reject FLT_MAX sentinel and (0,0,0).
                    const float kSentinel = 1e30f;
                    if (x <  kSentinel && y <  kSentinel && z <  kSentinel &&
                        x > -kSentinel && y > -kSentinel && z > -kSentinel &&
                        (x != 0.0f || y != 0.0f || z != 0.0f))
                    {
                        ox = x; oy = y; oz = z;
                        return true;
                    }
                }
            }
            // Recurse into Children TArray.
            auto* childrenArr = state->GetValuePtrByPropertyNameInChain<RC::Unreal::TArray<UObject*>>(STR("Children"));
            if (childrenArr)
            {
                for (int32 i = 0; i < childrenArr->Num(); ++i)
                {
                    UObject* c = (*childrenArr)[i];
                    if (npcReadFsmMoveDestRec(c, ox, oy, oz, depth + 1)) return true;
                }
            }
            return false;
        }

        bool npcReadFsmMoveDest(UObject* fsmComp, float& ox, float& oy, float& oz)
        {
            if (!fsmComp || !isObjectAlive(fsmComp)) return false;
            auto* rootPtr = fsmComp->GetValuePtrByPropertyNameInChain<UObject*>(STR("FSMRoot"));
            UObject* root = (rootPtr && *rootPtr) ? *rootPtr : nullptr;
            if (!root) return false;
            return npcReadFsmMoveDestRec(root, ox, oy, oz, 0);
        }

        // rc.11: read the NPC's CurrentActivity row name. The
        // "Blocked from Assigned Bed" status surfaced in the in-game
        // interact menu corresponds to EMorNpcActivity::CantReach
        // (value 11) on the activity enum. This enum value is exposed
        // through UMorNPCComponent::GetCurrentActivity() which returns
        // an FMorNPCActivityRowHandle (DataTable* + FName). The FName
        // is the DataTable row key, conventionally named after the
        // enum — e.g. "CantReach", "Idle", "Working".
        //
        // Returns the row name (FName converted to wstring) on
        // success, or an empty string on any failure. NPCComponent
        // pointer is found via the pawn's "NPC" UPROPERTY at offset
        // 0x0F98 on AMorCharacter (per CXXHeaderDump line 7569).
        // Helper: invoke an NPC-component UFunction that returns
        // FMorNPCActivityRowHandle and pull the row name (FName) out
        // of the return parm. Used for both GetCurrentActivity and
        // GetInterruptedActivity below.
        std::wstring npcReadActivityNameViaFn(UObject* npcComp, const wchar_t* fnName)
        {
            if (!npcComp || !isObjectAlive(npcComp)) return {};
            auto* fn = npcComp->GetFunctionByNameInChain(fnName);
            if (!fn) return {};
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pRet) return {};
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            try { safeProcessEvent(npcComp, fn, buf.data()); } catch (...) { return {}; }
            int32 retOff = pRet->GetOffset_Internal();
            if (retOff < 0) return {};
            uint8_t* retBase = buf.data() + retOff;
            if (!isReadableMemory(retBase, 16)) return {};
            // FMorNPCActivityRowHandle layout: DataTable* @0, FName @8.
            auto* fname = reinterpret_cast<RC::Unreal::FName*>(retBase + 8);
            std::wstring rowName;
            try { rowName = fname->ToString(); } catch (...) { return {}; }
            return rowName;
        }

        std::wstring npcReadCurrentActivityName(UObject* pawn)
        {
            if (!pawn || !isObjectAlive(pawn)) return {};
            auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
            UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
            return npcReadActivityNameViaFn(npcComp, STR("GetCurrentActivity"));
        }

        // The "Blocked from Assigned Bed" UI label corresponds to the
        // INTERRUPTED activity, not the current one — when an NPC
        // can't reach their bed, they fall back to Working in
        // CurrentActivity while the bed-attempt is parked in
        // InterruptedActivity with a "CantReach"/"Blocked" row name.
        std::wstring npcReadInterruptedActivityName(UObject* pawn)
        {
            if (!pawn || !isObjectAlive(pawn)) return {};
            auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
            UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
            return npcReadActivityNameViaFn(npcComp, STR("GetInterruptedActivity"));
        }

        // Refresh the controller cache. Called every 5 s — controllers
        // come and go slowly (bubble streaming), no need for per-tick
        // FindAllOf calls.
        void npcRefreshControllerCache()
        {
            m_npcControllerCache.clear();
            std::vector<UObject*> ctrls;

            // rc.6: FindAllOf is exact-match (per
            // ue4ss-class-enumeration.md). Calling it once on the
            // parent class "MorAIController" misses every subclass
            // instance — the actual NPC controllers are
            // BP_AiController_NpcDwarf_C, BP_AiController_NpcWanderer_*,
            // etc. Iterate each known friendly-NPC subclass name and
            // accumulate. Costs N FindAllOf calls but they're cheap and
            // only run every 5 s (cache refresh interval).
            //
            // Inventory from the user's blueprint research; if a
            // friendly-NPC subclass is missing here, the cache won't
            // see it — track via the cache-size log below.
            static const wchar_t* kFriendlyNpcClasses[] = {
                STR("BP_AiController_NpcDwarf_C"),
                STR("BP_AiController_NpcWanderer_Simple_C"),
                STR("BP_AiController_NpcEmissary_C"),
                STR("BP_AiController_NpcMerchant_C"),
                STR("BP_AiController_NpcWarden_C"),
                STR("BP_AiController_NpcRecruit_C"),
                STR("BP_AiController_NpcRecruitAndSettlement_C"),
            };
            for (const wchar_t* clsName : kFriendlyNpcClasses)
            {
                std::vector<UObject*> chunk;
                if (!findAllOfSafe(clsName, chunk)) continue;
                for (UObject* c : chunk)
                {
                    if (!c || !isObjectAlive(c)) continue;
                    // Reject CDOs (Class Default Objects):
                    // FindAllOf returns the CDO alongside instances
                    // and we don't want to teleport that.
                    std::wstring cls = safeClassName(c);
                    if (cls.size() >= 9 && cls.substr(0, 9) == STR("Default__"))
                        continue;
                    ctrls.emplace_back(c);
                }
            }

            // De-dup by pointer (some NPCs may match >1 of the parent
            // chains used above; cheap to filter).
            std::set<UObject*> seenSet;
            for (UObject* c : ctrls)
            {
                if (!seenSet.insert(c).second) continue;
                m_npcControllerCache.emplace_back(c);
            }

            // Verbose: log cache size at each refresh so we can see if
            // bubble streaming is loading NPCs or not.
            if (s_verbose)
            {
                static size_t s_lastLoggedCacheSize = (size_t)-1;
                if (m_npcControllerCache.size() != s_lastLoggedCacheSize)
                {
                    VLOG(STR("[NpcRecovery] cache refresh: {} friendly NPC controller(s) live\n"),
                         (int)m_npcControllerCache.size());
                    s_lastLoggedCacheSize = m_npcControllerCache.size();
                }
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
        // rc.28: one-shot sweep ~30 s after character-load. NPCs
        // already in CantReachBed when the world was loaded had their
        // SetCurrentActivity fire BEFORE our PE post-hook was wired,
        // so the event-driven path doesn't see them. This sweep
        // explicitly walks the cache and trips the teleport for any
        // NPC currently in a CantReach* activity. Fires once per
        // character-load.
        void runPostLoadCantReachSweep()
        {
            if (m_npcControllerCache.empty())
            {
                // Cache not populated yet — try a forced refresh.
                npcRefreshControllerCache();
                if (m_npcControllerCache.empty()) {
                    VLOG(STR("[NpcRecovery] post-load sweep: no NPCs in cache, skipping\n"));
                    return;
                }
            }

            int swept = 0, triggered = 0;
            for (auto& wctrl : m_npcControllerCache)
            {
                UObject* ctrl = wctrl.Get();
                if (!ctrl || !isObjectAlive(ctrl)) continue;
                ++swept;
                auto* pawnPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
                UObject* pawn = (pawnPtr && *pawnPtr) ? *pawnPtr : nullptr;
                if (!pawn || !isObjectAlive(pawn)) continue;

                std::wstring cur  = npcReadCurrentActivityName(pawn);
                std::wstring intr = npcReadInterruptedActivityName(pawn);
                // rc.44: widened to ANY cantreach* substring (was
                // bed-only). Catches CantReachFurnace + future
                // variants. See matching widening in the polling
                // tick + dllmain PE post-hook.
                auto isCantReachAny = [](const std::wstring& s) {
                    std::wstring lo = s;
                    for (auto& c : lo) c = (wchar_t)towlower(c);
                    return lo.find(STR("cantreach")) != std::wstring::npos;
                };

                if (!(isCantReachAny(cur) || isCantReachAny(intr))) continue;

                auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
                UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
                if (!npcComp) continue;
                m_lastEventTeleportFired = false;
                onNpcBlockedActivityEvent(npcComp);
                if (m_lastEventTeleportFired) ++triggered;
            }
            VLOG(STR("[NpcRecovery] post-load sweep: scanned {} NPC(s), {} actually teleported\n"),
                 swept, triggered);
        }

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

            // rc.45: drop polling rate from 1 Hz to 0.2 Hz (every 5 s).
            // Concern is 30+ NPCs at 1 Hz = ~60 PE calls/sec just for
            // activity reads. At 0.2 Hz that drops to ~12 PE/sec.
            // CantReach* states persist for minutes, so a 5 s detection
            // latency is invisible to the player. The recurring
            // post-load sweep + event-driven PE hook (once we get the
            // function-name match right) handle responsive cases;
            // polling is just the long-game safety net.
            static constexpr ULONGLONG NPC_POLL_INTERVAL_MS = 5000;

            // rc.41: recurring post-load sweep every 30 s for the
            // first 5 minutes after character-load.
            if (m_npcPostLoadSweepNextMs != 0
                && now >= m_npcPostLoadSweepNextMs)
            {
                runPostLoadCantReachSweep();
                if (now < m_npcPostLoadSweepEndMs)
                    m_npcPostLoadSweepNextMs = now + 30000;
                else
                    m_npcPostLoadSweepNextMs = 0;  // window closed
            }

            if (now - m_lastNpcRecoveryTickMs < NPC_POLL_INTERVAL_MS) return;  // 0.2 Hz
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

                // rc.45: adaptive per-NPC throttle. If this NPC was
                // teleported in the last NPC_TELEPORT_THROTTLE_MS, we
                // know we just acted on it and the activity won't
                // re-trigger until the throttle expires. Skip ALL
                // per-NPC work (no activity read, no FSM walk, nothing)
                // until the throttle window opens. For 30+ NPCs the
                // dominant cost is the 2 PE calls/NPC for activity
                // reads — this lets just-teleported NPCs contribute
                // zero ProcessEvent calls for 30 s.
                {
                    auto it = m_npcRecoveryStates.find(ctrl);
                    if (it != m_npcRecoveryStates.end()
                        && it->second.lastTeleportTickMs != 0
                        && now - it->second.lastTeleportTickMs < NPC_TELEPORT_THROTTLE_MS)
                    {
                        continue;
                    }
                }

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
                // For motion-suspect classes (names containing Move /
                // Travel / Goto / Approach / Flee), also dump the full
                // property chain + try multiple candidate destination
                // field names — rc.3 found the game uses
                // FGKBehaviorState_MoveToBlackboardKey, NOT the simple
                // FGKBehaviorState_MoveTo, so 'Destination' isn't the
                // right field name. This dump tells us what IS.
                if (s_verbose)
                {
                    std::wstring leafCls = safeClassName(leaf);
                    if (m_npcLeafClassesSeen.insert(leafCls).second)
                    {
                        VLOG(STR("[NpcRecovery] new leaf state class observed: '{}'\n"),
                             leafCls.c_str());

                        bool isMotionSuspect =
                            (leafCls.find(STR("Move")) != std::wstring::npos ||
                             leafCls.find(STR("Travel")) != std::wstring::npos ||
                             leafCls.find(STR("Goto")) != std::wstring::npos ||
                             leafCls.find(STR("GoTo")) != std::wstring::npos ||
                             leafCls.find(STR("Approach")) != std::wstring::npos ||
                             leafCls.find(STR("Flee")) != std::wstring::npos ||
                             leafCls.find(STR("Path")) != std::wstring::npos ||
                             leafCls.find(STR("Walk")) != std::wstring::npos);

                        if (isMotionSuspect)
                        {
                            // Full property dump.
                            std::wstring label = STR("[motion-suspect] ") + leafCls;
                            npcProbeLogPropertyChain(leaf, label.c_str());

                            // Try multiple candidate destination field names.
                            const wchar_t* fvecCandidates[] = {
                                STR("Destination"),
                                STR("TargetLocation"),
                                STR("GoalLocation"),
                                STR("DestLocation"),
                                STR("CurrentEndLocation"),
                                STR("EndLocation"),
                                STR("FinalLocation"),
                                STR("MoveGoal"),
                                STR("Goal"),
                            };
                            for (const wchar_t* name : fvecCandidates)
                            {
                                auto* p = leaf->GetValuePtrByPropertyNameInChain<float>(name);
                                if (p)
                                {
                                    VLOG(STR("[NpcRecovery]   FVector '{}' EXISTS = ({:.1f}, {:.1f}, {:.1f})\n"),
                                         name, p[0], p[1], p[2]);
                                }
                            }

                            // Try blackboard key-name candidates (FName).
                            const wchar_t* fnameCandidates[] = {
                                STR("LocationBlackboardKeyName"),
                                STR("TargetBlackboardKeyName"),
                                STR("BlackboardKey"),
                                STR("GoalKey"),
                                STR("DestinationKey"),
                            };
                            for (const wchar_t* name : fnameCandidates)
                            {
                                // FName is 8 bytes — try reading as such.
                                auto* p = leaf->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(name);
                                if (p)
                                {
                                    std::wstring keyStr;
                                    try { keyStr = p->ToString(); } catch (...) { keyStr = STR("(unreadable)"); }
                                    VLOG(STR("[NpcRecovery]   FName '{}' EXISTS = '{}'\n"),
                                         name, keyStr.c_str());
                                }
                            }

                            // Try AActor* destination candidates.
                            const wchar_t* actorCandidates[] = {
                                STR("DestinationActor"),
                                STR("TargetActor"),
                                STR("GoalActor"),
                            };
                            for (const wchar_t* name : actorCandidates)
                            {
                                auto* p = leaf->GetValuePtrByPropertyNameInChain<UObject*>(name);
                                if (p && *p)
                                {
                                    std::wstring acls = safeClassName(*p);
                                    VLOG(STR("[NpcRecovery]   Actor '{}' EXISTS = {:p} class={}\n"),
                                         name, (void*)*p, acls.c_str());
                                }
                            }
                        }
                    }
                }

                // rc.5: instant teleport when leaf state class name
                // contains "block" (case-insensitive). The user reports
                // NPCs showing a "blocked from assigned bed"
                // notification — if a state class with a "block"
                // substring is ever observed in the FSM, we should warp
                // immediately rather than wait out the stuck timer.
                // Scoped wide on purpose: we don't yet know the exact
                // name (camel-case, all-caps, lowercase, prefix `b`,
                // etc.); the diag dump below prints class + property
                // names on first sighting so we can tighten in a
                // follow-up. Case-insensitive search via lowercase.
                auto containsBlockCI = [](const std::wstring& s) -> bool {
                    std::wstring lo; lo.reserve(s.size());
                    for (wchar_t c : s) lo.push_back((wchar_t)towlower(c));
                    return lo.find(STR("block")) != std::wstring::npos;
                };

                std::wstring leafClsForBlock = safeClassName(leaf);
                bool isBlockedState = containsBlockCI(leafClsForBlock);

                // rc.20: per-tick block-prop UPROPERTY scan DISABLED.
                // Walking ForEachPropertyInChain every tick on every
                // NPC's leaf state is expensive and produced zero hits
                // across all earlier tests — "blocked" isn't exposed
                // as a UPROPERTY anywhere on the FSM. The
                // CurrentActivity-based CantReach trigger (downstream)
                // is the authoritative signal.
                bool blockedPropTrue = false;
                std::wstring blockedPropName;
                if (false)
                {
                    UClass* cls = nullptr;
                    try { cls = leaf->GetClassPrivate(); } catch (...) {}
                    if (cls)
                    {
                        try {
                            for (auto* prop : cls->ForEachPropertyInChain())
                            {
                                if (!prop) continue;
                                std::wstring pn;
                                try { pn = prop->GetName(); } catch (...) { continue; }
                                if (!containsBlockCI(pn)) continue;

                                std::wstring oneShot = leafClsForBlock + STR("::") + pn;
                                bool firstSighting = m_npcBlockedPropsSeen.insert(oneShot).second;
                                int32 off = -1;
                                try { off = prop->GetOffset_Internal(); } catch (...) {}

                                // Try to read as bool (1 byte).
                                bool bVal = false;
                                if (off >= 0)
                                {
                                    auto* b = reinterpret_cast<uint8_t*>(leaf) + off;
                                    if (isReadableMemory(b, 1))
                                        bVal = (*b != 0);
                                }
                                if (firstSighting && s_verbose)
                                {
                                    VLOG(STR("[NpcRecovery] BLOCK-PROP first-seen on '{}': name='{}' off=0x{:04x} val(bool)={}\n"),
                                         leafClsForBlock.c_str(), pn.c_str(),
                                         (unsigned)off, bVal ? STR("true") : STR("false"));
                                }
                                if (bVal) {
                                    blockedPropTrue = true;
                                    blockedPropName = pn;
                                }
                            }
                        } catch (...) {}
                    }
                }

                bool instantBlock = isBlockedState || blockedPropTrue;

                // rc.5: per-pawn stuck tracking, decoupled from leaf state.
                //
                // Why: rc.3/rc.4 only ran their stuck timer while the leaf
                // was a MoveTo* state. Blocked NPCs (e.g. "blocked from
                // assigned bed") cycle MoveToBlackboardKey ↔ Idle/TakeMeal
                // every 1-3 s — the leaf-scoped timer kept resetting and
                // never reached the 10 s threshold despite the pawn being
                // visibly stuck for minutes. Rc.5 inverts the logic:
                //   • Always read pawn location and tick the stuck timer,
                //     regardless of what state we're in.
                //   • Cache the most-recently observed path destination
                //     from any MoveTo* leaf (FGKBehaviorState_MoveTo or
                //     FGKBehaviorState_MoveToBlackboardKey both expose
                //     'Destination' — confirmed in rc.4 logs).
                //   • Trigger teleport when stuck >= threshold AND we
                //     have a remembered destination from within the last
                //     NPC_DEST_FRESHNESS_MS window. The freshness window
                //     bridges the rapid Idle ↔ MoveTo cycles.

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
                    // Don't continue — fall through so we still capture
                    // a destination on this same tick if the leaf is a
                    // MoveTo* state.
                }

                // Capture-or-refresh the remembered destination if we're
                // currently in a path-follow state. Both MoveTo and
                // MoveToBlackboardKey expose 'Destination' as FVector.
                auto* destPtr = leaf->GetValuePtrByPropertyNameInChain<float>(STR("Destination"));
                {
                    bool destValid = false;
                    if (destPtr) {
                        const float kSentinel = 1e30f;
                        float x = destPtr[0], y = destPtr[1], z = destPtr[2];
                        destValid = (x <  kSentinel && y <  kSentinel && z <  kSentinel &&
                                     x > -kSentinel && y > -kSentinel && z > -kSentinel &&
                                     (x != 0.0f || y != 0.0f || z != 0.0f));
                    }
                    if (destValid)
                    {
                        entry.lastDestX = destPtr[0];
                        entry.lastDestY = destPtr[1];
                        entry.lastDestZ = destPtr[2];
                        entry.lastDestSeenTickMs = now;
                        entry.hasDestination = true;
                    }
                }
                if (!entry.hasDestination)
                {
                    // rc.19: leaf isn't a MoveTo* state but we don't
                    // have a cached destination yet. Walk the entire
                    // FSM state tree for any sibling MoveToBlackboardKey
                    // instance and read its Destination directly. The
                    // state object persists in the tree even when not
                    // currently active.
                    auto* fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
                    UObject* fsmComp2 = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
                    float fx, fy, fz;
                    if (npcReadFsmMoveDest(fsmComp2, fx, fy, fz))
                    {
                        entry.lastDestX = fx;
                        entry.lastDestY = fy;
                        entry.lastDestZ = fz;
                        entry.lastDestSeenTickMs = now;
                        entry.hasDestination = true;
                        if (s_verbose) {
                            static bool s_loggedFirstFsmHit = false;
                            if (!s_loggedFirstFsmHit) {
                                s_loggedFirstFsmHit = true;
                                VLOG(STR("[NpcRecovery] destination resolved via FSM tree walk for first NPC: ({:.1f}, {:.1f}, {:.1f})\n"),
                                     fx, fy, fz);
                            }
                        }
                    }
                }

                // rc.12: work-state filter REMOVED. With rc.11's
                // CurrentActivity-based trigger, the leaf state is
                // irrelevant — only the activity row name (e.g.
                // "CantReachBed") gates the teleport. Filtering by
                // leaf state was rejecting CantReach NPCs that
                // happened to be parked in a Work_C leaf state at
                // sample time, blocking the very fix the user wanted.

                // Update location-progress bookkeeping (kept for
                // status-log completeness; no longer the trigger).
                {
                    float dx = px - entry.lastX;
                    float dy = py - entry.lastY;
                    float dz = pz - entry.lastZ;
                    float d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 > NPC_PROGRESS_THRESHOLD_CM * NPC_PROGRESS_THRESHOLD_CM)
                    {
                        entry.lastProgressTickMs = now;
                        entry.lastX = px; entry.lastY = py; entry.lastZ = pz;
                    }
                }

                // rc.11: PRIMARY TRIGGER — read the NPC's CurrentActivity
                // row name and compare against "CantReach" (the
                // EMorNpcActivity enum value displayed as "Blocked from
                // Assigned <X>" in the in-game interact menu). This is
                // the literal "blocked" signal the user wants. No time
                // threshold — fires immediately on detection.
                std::wstring activityCurrent     = npcReadCurrentActivityName(pawn);
                std::wstring activityInterrupted = npcReadInterruptedActivityName(pawn);

                // rc.44: widened to ANY cantreach* substring. The user
                // wanted blocked-furnace and other CantReach* variants
                // to trigger too (rc.25 had narrowed to bed-only).
                // Observed row names so far:
                //   CantReachBed / CantReachAssignedBed (sleep)
                //   CantReachFurnace (workstation - metalworker)
                // Likely future variants: CantReachKitchen, CantReachStation,
                // CantReachWorkbench, etc. Substring match handles all.
                //
                // Note: rejecting the No* prefix family (NoBed, NoFurnace,
                // NoMealsNeeded, NoSuitableStorage) — those are
                // preconditions ("NPC has nothing to do") rather than
                // path failures and don't need teleport.
                auto isCantReachStr = [](const std::wstring& s) {
                    std::wstring lo = s;
                    for (auto& c : lo) c = (wchar_t)towlower(c);
                    return lo.find(STR("cantreach")) != std::wstring::npos;
                };

                // The "Blocked from Assigned Bed" UI lives in the
                // INTERRUPTED activity slot — current is whatever
                // they're doing instead (Working / NoMealsNeeded / etc).
                // We trigger on either being CantReach.
                bool isCantReach = isCantReachStr(activityCurrent)
                                || isCantReachStr(activityInterrupted);

                // Use whichever one is the CantReach* one for the
                // status log + activity-name first-seen tracking.
                std::wstring activityName = isCantReachStr(activityInterrupted)
                                          ? activityInterrupted
                                          : activityCurrent;

                // Verbose: log first sighting of each unique activity
                // name so we can chart what row keys the DT actually
                // uses (case + spelling).
                if (s_verbose && !activityName.empty())
                {
                    if (m_npcActivityNamesSeen.insert(activityName).second)
                        VLOG(STR("[NpcRecovery] activity-name first seen: '{}'\n"),
                             activityName.c_str());
                }

                // rc.22: per-NPC status log AFTER activity is read, so
                // we can see the actual gate-relevant state — activity
                // name, isCantReach, hasDest. Throttled to 10 s/NPC.
                if (s_verbose && (now - entry.lastStatusLogMs >= 10000))
                {
                    entry.lastStatusLogMs = now;
                    VLOG(STR("[NpcRecovery] status: ctrl={:p} curAct='{}' intrAct='{}' isCantReach={} hasDest={} leaf='{}'\n"),
                         (void*)ctrl,
                         activityCurrent.c_str(),
                         activityInterrupted.c_str(),
                         isCantReach ? STR("Y") : STR("N"),
                         entry.hasDestination ? STR("Y") : STR("N"),
                         leafClsForBlock.c_str());
                }

                // Trigger gate: only the CantReach activity (or our
                // legacy isBlockedState/blockedPropTrue paths) fires
                // a teleport now. Stuck-timer + state denylist are
                // GONE — they produced false positives on workers and
                // mass-fallback to the player.
                ULONGLONG stuckElapsed = now - entry.lastProgressTickMs;
                if (!isCantReach && !instantBlock) continue;

                // Throttle: don't teleport same NPC more than once every 30 s.
                if (entry.lastTeleportTickMs != 0 &&
                    now - entry.lastTeleportTickMs < NPC_TELEPORT_THROTTLE_MS)
                    continue;

                // rc.18: SIMPLIFIED — blocked → teleport to cached
                // path destination, period. The cached destination is
                // captured up-tick whenever the FSM is in
                // FGKBehaviorState_MoveToBlackboardKey. We keep it for
                // the entire session (no freshness expiry) so an NPC
                // who once attempted to path can be unblocked even
                // long after the FSM gave up. If we never observed
                // them attempting a path, we have nothing to send
                // them to and skip.
                if (!entry.hasDestination)
                {
                    // rc.41: bed-walk fallback in the polling path
                    // too. The event-driven path already has this; we
                    // need it here because activity transitions from
                    // NoBed → CantReachBed can happen long after the
                    // post-load sweep (~70s in observed log), and the
                    // PE post-hook on SetCurrentActivity has not been
                    // confirmed to fire reliably yet. Bed-walk is
                    // the last-resort path that doesn't depend on
                    // the FSM having a live MoveTo state.
                    auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
                    UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
                    if (npcComp)
                    {
                        UObject* bed = npcFindAssignedBed(pawn);
                        if (bed && isObjectAlive(bed))
                        {
                            float bx, by, bz;
                            if (npcReadPawnLocation(bed, bx, by, bz))
                            {
                                entry.lastDestX = bx; entry.lastDestY = by; entry.lastDestZ = bz;
                                entry.lastDestSeenTickMs = now;
                                entry.hasDestination = true;
                                entry.cachedAssignedBed = RC::Unreal::FWeakObjectPtr(bed);
                                if (s_verbose) {
                                    static bool s_loggedFirstBedHit = false;
                                    if (!s_loggedFirstBedHit) {
                                        s_loggedFirstBedHit = true;
                                        VLOG(STR("[NpcRecovery] polling resolved bed via guid-scan walk: ({:.1f}, {:.1f}, {:.1f})\n"),
                                             bx, by, bz);
                                    }
                                }
                            }
                        }
                    }
                }
                if (!entry.hasDestination)
                {
                    // rc.42: final fallback — nearest unoccupied bed.
                    // Assignment-side data isn't reachable; we just put
                    // the NPC on A bed. Game's settlement system can
                    // re-route. Solves the "frozen forever" symptom.
                    UObject* anyBed = npcFindNearestUnoccupiedBed(pawn);
                    if (anyBed && isObjectAlive(anyBed))
                    {
                        float bx, by, bz;
                        if (npcReadPawnLocation(anyBed, bx, by, bz))
                        {
                            entry.lastDestX = bx; entry.lastDestY = by; entry.lastDestZ = bz;
                            entry.lastDestSeenTickMs = now;
                            entry.hasDestination = true;
                            entry.cachedAssignedBed = RC::Unreal::FWeakObjectPtr(anyBed);
                            if (s_verbose) {
                                static bool s_loggedFirstNearest = false;
                                if (!s_loggedFirstNearest) {
                                    s_loggedFirstNearest = true;
                                    VLOG(STR("[NpcRecovery] polling resolved nearest UNOCCUPIED bed: cls='{}' loc=({:.1f}, {:.1f}, {:.1f})\n"),
                                         safeClassName(anyBed).c_str(), bx, by, bz);
                                }
                            }
                        }
                    }
                }
                if (!entry.hasDestination)
                {
                    // Still nothing — even the nearest-unoccupied
                    // fallback found no bed. Throttled diagnostic.
                    if (s_verbose && (now - entry.lastStatusLogMs >= 30000))
                    {
                        entry.lastStatusLogMs = now;
                        VLOG(STR("[NpcRecovery] {} blocked but no destination resolvable — skipping\n"),
                             safeClassName(ctrl).c_str());
                    }
                    continue;
                }

                float dx_t = entry.lastDestX;
                float dy_t = entry.lastDestY;
                float dz_t = entry.lastDestZ;
                ULONGLONG destAgeMs = now - entry.lastDestSeenTickMs;
                const wchar_t* destSource = STR("path-dest");

                std::wstring leafCls = safeClassName(leaf);
                std::wstring ctrlCls = safeClassName(ctrl);

                bool ok = npcTeleportPawn(pawn, dx_t, dy_t, dz_t);

                const wchar_t* trigger =
                    isCantReach ? STR("CANT-REACH") :
                    isBlockedState ? STR("BLOCKED-state") :
                    blockedPropTrue ? STR("BLOCKED-prop") :
                    STR("STUCK-timer");

                VLOG(STR("[NpcRecovery] {} teleported pawn ({}) trigger={} target={} "
                         "from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f}) "
                         "after {}s stuck (current state '{}'{}{}, dest age {}ms, result={})\n"),
                     ctrlCls.c_str(), safeClassName(pawn).c_str(),
                     trigger, destSource,
                     px, py, pz, dx_t, dy_t, dz_t,
                     (unsigned)(stuckElapsed / 1000), leafCls.c_str(),
                     blockedPropTrue ? STR(", prop=") : STR(""),
                     blockedPropTrue ? blockedPropName.c_str() : STR(""),
                     (unsigned)destAgeMs,
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
