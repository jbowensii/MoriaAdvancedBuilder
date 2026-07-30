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

struct NpcRecoveryEntry
{
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
    ULONGLONG lastStatusLogMs{0}; // rc.6 verbose throttle
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

// rc.47: day-cycle-triggered NPC scan state.
//
// Architecture: detect day/night phase transitions by sampling
// ATimeManager::CurrentPeriodIndex (int32 @ 0x0278) each tick.
// When it changes, open a 30 s scan window. During the window,
// walk AMorNPCManager.NpcInfo.Items (TArray<FMorNPCInfo>) via
// direct memory reads — NO PE calls — checking each NPC's
// CurrentActivity.RowName for CantReach*. On hit, look up the
// matching controller in our cache by NpcGuid match and call
// onNpcBlockedActivityEvent for it.
//
// Cost at idle: 1 int32 read per tick. Cost during scan window:
// (N×24-byte stride iteration + N×FName.ToString() per second
// for 30 s). For 30 NPCs: ~30 FName resolves/sec during
// window only. Outside window: zero NPC work.
//
// Why CurrentPeriodIndex: ATimeManager exposes a per-phase
// index (dawn/day/dusk/night/etc.) at known offset. Activity
// transitions cluster around phase boundaries because NPCs
// re-evaluate their schedule then. Sampling 30 s after each
// boundary catches every relevant transition.
RC::Unreal::FWeakObjectPtr m_cachedTimeManager;
RC::Unreal::FWeakObjectPtr m_cachedNpcManager;
int32 m_lastTimePeriodIndex{-1};

// v7.2.0-rc.2: NpcInfo traversal offsets, lazily resolved via
// reflection on first call. Sentinel pattern: -2=untried,
// -1=attempted-and-failed (caller uses documented fallbacks),
// >=0=resolved.
int32 m_off_npcInfoOnMgr{-2};      // AMorNPCManager.NpcInfo
int32 m_off_itemsOnInfoArr{-2};    // FMorNPCInfoArray.Items
int32 m_off_morNpcInfoStride{-2};  // sizeof(FMorNPCInfo)
int32 m_off_npcInfoNpcGuid{-2};    // FMorNPCInfo.PersistentData.NpcGuid (absolute)
int32 m_off_npcInfoCurRowName{-2}; // FMorNPCInfo.CurrentActivity.RowName (absolute)
int32 m_off_npcInfoIntRowName{-2}; // FMorNPCInfo.InterruptedActivity.RowName (absolute)
// ATimeManager.CurrentPeriodIndex offset (B6).
int32 m_off_timeManagerPeriodIdx{-2};
// rc.50: single one-shot scan instead of 30s window.
// m_scanDueMs is the timestamp when the next scan should
// fire; 0 = none pending. Each trigger schedules ONE scan
// 10 s in the future (lets cache populate + NPCs settle
// into post-event activities), runs once, clears m_scanDueMs.
ULONGLONG m_scanDueMs{0};

// [rc.139] true only while runUnstuckNpcsNow() (the "Unstuck NPCs"
// keybind handler) is executing: lets the shared teleport pipeline
// bypass the [NpcRecovery] Enabled gate + the 30 s per-NPC throttle
// for explicit user-initiated passes.
bool m_unstuckManualPass{false};

bool m_npcRecoveryEnabled{
        false}; // [rc.26 2026-05-27] Default OFF — investigating all-NPCs-missing regression. Re-enable via INI [NpcRecovery] Enabled=true after the regression is isolated.
ULONGLONG m_npcStuckThresholdMs{5000};                       // rc.6: 5 s default (was 10 s) — faster response on the user's blocked-bed scenario
static constexpr ULONGLONG NPC_TELEPORT_THROTTLE_MS = 30000; // 30s between teleports per NPC
static constexpr float NPC_PROGRESS_THRESHOLD_CM = 100.0f;   // 1m of movement = "making progress"
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
            STR("Bed"),
            STR("Home"),
            STR("Sleep"),
            STR("Rest"),
            STR("Workstation"),
            STR("Job"),
            STR("Task"),
            STR("Assigned"),
            STR("Owner"),
            STR("Settlement"),
            STR("Target"),
            STR("Goal"),
            STR("Destination"),
            // rc.10 additions:
            STR("Interact"),
            STR("Choice"),
            STR("Option"),
            STR("Action"),
            STR("Available"),
            STR("Status"),
            STR("State"),
            STR("Reason"),
            STR("Block"),
            STR("Cant"),
            STR("Disable"),
            STR("Need"),
            STR("Mood"),
            STR("Want"),
            STR("Issue"),
            STR("Worker"),
            STR("Behavior"),
    };
    auto containsKeywordCI = [](const std::wstring& name) -> bool {
        std::wstring lo;
        lo.reserve(name.size());
        for (wchar_t c : name)
            lo.push_back((wchar_t)towlower(c));
        for (const wchar_t* kw : kKeywords)
        {
            std::wstring kwl;
            kwl.reserve(wcslen(kw));
            for (size_t i = 0; kw[i]; ++i)
                kwl.push_back((wchar_t)towlower(kw[i]));
            if (lo.find(kwl) != std::wstring::npos) return true;
        }
        return false;
    };

    auto scan = [&](UObject* obj, const wchar_t* tgtLabel) {
        if (!obj || !isObjectAlive(obj)) return;
        UClass* cls = nullptr;
        try
        {
            cls = obj->GetClassPrivate();
        }
        catch (...)
        {
        }
        if (!cls) return;
        std::wstring clsName;
        try
        {
            clsName = cls->GetName();
        }
        catch (...)
        {
            return;
        }
        try
        {
            for (auto* prop : cls->ForEachPropertyInChain())
            {
                if (!prop) continue;
                std::wstring pn;
                try
                {
                    pn = prop->GetName();
                }
                catch (...)
                {
                    continue;
                }
                if (!containsKeywordCI(pn)) continue;
                std::wstring oneShot = std::wstring(tgtLabel) + STR(":") + clsName + STR("::") + pn;
                if (!m_npcDiscoveryPropsSeen.insert(oneShot).second) continue;
                int32 off = -1;
                try
                {
                    off = prop->GetOffset_Internal();
                }
                catch (...)
                {
                }
                VLOG(STR("[NpcRecovery] DISCOVER {} '{}::{}' off=0x{:04x}\n"), tgtLabel, clsName.c_str(), pn.c_str(), (unsigned)off);
            }
        }
        catch (...)
        {
        }
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
        if (child == leaf) break; // self-loop guard
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
        auto* pRot = findParam(fn, STR("DestRotation"));
        auto* pRet = findParam(fn, STR("ReturnValue"));
        if (pDest)
        {
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            auto* loc = reinterpret_cast<float*>(buf.data() + pDest->GetOffset_Internal());
            loc[0] = x;
            loc[1] = y;
            loc[2] = z;
            // DestRotation defaults to zero (FRotator{0,0,0}).
            if (pRot)
            {
                auto* rot = reinterpret_cast<float*>(buf.data() + pRot->GetOffset_Internal());
                rot[0] = 0.0f;
                rot[1] = 0.0f;
                rot[2] = 0.0f;
            }
            try
            {
                safeProcessEvent(pawn, fn, buf.data());
            }
            catch (...)
            {
                return false;
            }
            // Optional ReturnValue check — K2_TeleportTo returns
            // bool indicating whether the move succeeded. If
            // false, fall through to K2_SetActorLocation below.
            bool retVal = true;
            if (pRet)
            {
                bool* r = reinterpret_cast<bool*>(buf.data() + pRet->GetOffset_Internal());
                if (isReadableMemory(r, 1)) retVal = *r;
            }
            if (retVal) return true;
        }
    }

    // Fallback: K2_SetActorLocation(NewLocation, bSweep=false, bTeleport=true).
    auto* fn2 = pawn->GetFunctionByNameInChain(STR("K2_SetActorLocation"));
    if (!fn2) return false;
    auto* pNew = findParam(fn2, STR("NewLocation"));
    auto* pSweep = findParam(fn2, STR("bSweep"));
    auto* pTele = findParam(fn2, STR("bTeleport"));
    if (!pNew || !pSweep || !pTele) return false;
    std::vector<uint8_t> buf(fn2->GetParmsSize(), 0);
    auto* loc = reinterpret_cast<float*>(buf.data() + pNew->GetOffset_Internal());
    loc[0] = x;
    loc[1] = y;
    loc[2] = z;
    *reinterpret_cast<bool*>(buf.data() + pSweep->GetOffset_Internal()) = false;
    *reinterpret_cast<bool*>(buf.data() + pTele->GetOffset_Internal()) = true;
    try
    {
        safeProcessEvent(pawn, fn2, buf.data());
    }
    catch (...)
    {
        return false;
    }
    return true;
}

// SEH-wrapped 16-byte byte-scan helper. Plain C function body
// (no destructors) so __try/__except is legal here.
// Returns true if a 16-byte sequence matching `needle` was
// found in [base, base+limit). Step is the alignment.
static bool seh_scan_for_guid(const uint8_t* base, int32 limit, const uint8_t* needle) noexcept
{
    __try
    {
        for (int32 off = 0; off + 16 <= limit; off += 4)
        {
            if (std::memcmp(base + off, needle, 16) == 0) return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// rc.26: event-driven trigger called from the PE post-hook on
// UMorNPCComponent::SetCurrentActivity / MorNpcUpdateActivity
// when the activity row name matches CantReachBed /
// CantReachAssignedBed. The hook gives us the NPC component;
// we resolve to pawn → controller → FSM destination → teleport.
// Throttle still applies (NPC_TELEPORT_THROTTLE_MS, 30 s).
// rc.51: activityNameHint lets callers pass the row name they
// already have so we can route the fallback by target type
// (bed/furnace/etc.). Empty string → no hint, defaults to bed.
void onNpcBlockedActivityEvent(UObject* npcComp, const std::wstring& activityNameHint = std::wstring())
{
    // [rc.139] m_unstuckManualPass = the user pressed the "Unstuck
    // NPCs" keybind — always allowed, regardless of the legacy
    // [NpcRecovery] Enabled ini flag (which only governed the now-
    // retired automatic scanning).
    if (!m_npcRecoveryEnabled && !m_unstuckManualPass) return;
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
    // [rc.139] Manual keybind presses bypass the throttle — the user
    // explicitly asked for a pass right now.
    if (!m_unstuckManualPass && entry.lastTeleportTickMs != 0 && now - entry.lastTeleportTickMs < NPC_TELEPORT_THROTTLE_MS) return;

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
        dx_t = entry.lastDestX;
        dy_t = entry.lastDestY;
        dz_t = entry.lastDestZ;
        destSrc = STR("cached");
    }
    else
    {
        auto* fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
        UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
        if (npcReadFsmMoveDest(fsmComp, dx_t, dy_t, dz_t))
        {
            destSrc = STR("fsm-walk");
            entry.lastDestX = dx_t;
            entry.lastDestY = dy_t;
            entry.lastDestZ = dz_t;
            entry.lastDestSeenTickMs = now;
            entry.hasDestination = true;
        }
        else
        {
            // rc.51: ACTIVITY-AWARE fallback routing.
            // Inspect the activity name hint (lowercased) and
            // pick the appropriate target-type finder:
            //   contains "furnace" → nearest furnace/forge
            //   contains "bed"     → nearest unoccupied bed
            //   default            → nearest unoccupied bed
            //                        (no info; bed is most common)
            //
            // This fixes the rc.50 bug where furnace-blocked
            // NPCs got teleported to a bed instead of a
            // furnace (because the fallback was bed-only).
            std::wstring actLo = activityNameHint;
            for (auto& c : actLo)
                c = (wchar_t)towlower(c);
            bool wantFurnace = (actLo.find(STR("furnace")) != std::wstring::npos) || (actLo.find(STR("forge")) != std::wstring::npos);

            UObject* tgt = nullptr;
            if (wantFurnace)
            {
                tgt = npcFindNearestFurnace(pawn);
                if (tgt) destSrc = STR("nearest-furnace");
            }
            else
            {
                UObject* bed = npcFindAssignedBed(pawn);
                bool fromGuidScan = (bed && isObjectAlive(bed));
                if (!fromGuidScan) bed = npcFindNearestUnoccupiedBed(pawn);
                tgt = bed;
                if (tgt) destSrc = fromGuidScan ? STR("bed-guid-scan") : STR("nearest-bed");
            }

            if (!tgt || !isObjectAlive(tgt))
            {
                if (s_verbose)
                {
                    static bool s_diagDumped = false;
                    if (!s_diagDumped && !wantFurnace)
                    {
                        s_diagDumped = true;
                        npcDumpBedDiagnostic(pawn);
                    }
                    VLOG(STR("[NpcRecovery] EVENT-DRIVEN: no destination — fsm-walk + activity-fallback all failed for {} (activity='{}')\n"),
                         cls.c_str(),
                         activityNameHint.c_str());
                }
                return;
            }
            if (!npcReadPawnLocation(tgt, dx_t, dy_t, dz_t))
            {
                if (s_verbose) VLOG(STR("[NpcRecovery] EVENT-DRIVEN: target {} found but location read failed for {}\n"), destSrc, cls.c_str());
                return;
            }
            entry.lastDestX = dx_t;
            entry.lastDestY = dy_t;
            entry.lastDestZ = dz_t;
            entry.lastDestSeenTickMs = now;
            entry.hasDestination = true;
            if (!wantFurnace)
            {
                entry.cachedAssignedBed = RC::Unreal::FWeakObjectPtr(tgt);
            }
        }
    }

    float px, py, pz;
    if (!npcReadPawnLocation(pawn, px, py, pz)) return;

    // rc.48: 200 cm (2 m) offset from the target object. NPCs
    // were appearing in the center of the bed/furnace mesh
    // and getting stuck inside collision geometry. Offset
    // direction = vector from target back toward the NPC's
    // pre-teleport position (normalized), so they land along
    // their natural approach line instead of on top.
    // Edge case: if NPC is already AT target XY, use an
    // arbitrary direction so we still nudge them outside the
    // collision volume.
    {
        float dx = px - dx_t;
        float dy = py - dy_t;
        float len2D = std::sqrt(dx * dx + dy * dy);
        float nx, ny;
        if (len2D > 1.0f)
        {
            nx = dx / len2D;
            ny = dy / len2D;
        }
        else
        {
            // NPC essentially on top of target — push along +X.
            nx = 1.0f;
            ny = 0.0f;
        }
        // rc.49: reduced 200→75 cm. 2 m was overshooting the
        // approach line and dropping NPCs too far from their
        // target. 75 cm clears the bed/furnace collision
        // radius while leaving them close enough to walk one
        // step onto the target.
        constexpr float kTargetOffsetCm = 75.0f;
        dx_t += nx * kTargetOffsetCm;
        dy_t += ny * kTargetOffsetCm;
    }

    bool ok = npcTeleportPawn(pawn, dx_t, dy_t, dz_t);
    entry.lastTeleportTickMs = now;
    entry.lastProgressTickMs = now;
    entry.lastX = px;
    entry.lastY = py;
    entry.lastZ = pz;

    if (s_verbose)
    {
        VLOG(STR("[NpcRecovery] EVENT-DRIVEN teleport ({}) src={} from ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f}) [+75cm offset applied] result={}\n"),
             cls.c_str(),
             destSrc,
             px,
             py,
             pz,
             dx_t,
             dy_t,
             dz_t,
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
                 g[0],
                 g[1],
                 g[2],
                 g[3],
                 g[4],
                 g[5],
                 g[6],
                 g[7],
                 g[8],
                 g[9],
                 g[10],
                 g[11],
                 g[12],
                 g[13],
                 g[14],
                 g[15]);
        }
        else
        {
            VLOG(STR("[NpcRecovery] DIAG NpcGuid: unreadable\n"));
        }
    }

    // (b) first bed's UPROPERTY chain
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
        for (UObject* b : chunk)
        {
            if (!b || !isObjectAlive(b)) continue;
            std::wstring c = safeClassName(b);
            if (c.size() >= 9 && c.substr(0, 9) == STR("Default__")) continue;
            beds.push_back(b);
        }
        if (!beds.empty()) break;
    }
    if (beds.empty())
    {
        VLOG(STR("[NpcRecovery] DIAG: no live beds to probe\n"));
        return;
    }
    UObject* firstBed = beds[0];
    std::wstring bedCls = safeClassName(firstBed);
    VLOG(STR("[NpcRecovery] DIAG first bed: cls='{}' ptr={:p}\n"), bedCls.c_str(), (void*)firstBed);

    UClass* bedClsObj = nullptr;
    try
    {
        bedClsObj = firstBed->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (bedClsObj)
    {
        int dumped = 0;
        try
        {
            for (auto* prop : bedClsObj->ForEachPropertyInChain())
            {
                if (!prop) continue;
                std::wstring pn;
                try
                {
                    pn = prop->GetName();
                }
                catch (...)
                {
                    continue;
                }
                int32 off = -1;
                try
                {
                    off = prop->GetOffset_Internal();
                }
                catch (...)
                {
                }
                VLOG(STR("[NpcRecovery] DIAG bed prop[{}] off=0x{:04x} name='{}'\n"), dumped, (unsigned)off, pn.c_str());
                if (++dumped >= 200)
                {
                    VLOG(STR("[NpcRecovery] DIAG bed prop dump truncated at 200\n"));
                    break;
                }
            }
        }
        catch (...)
        {
        }
        VLOG(STR("[NpcRecovery] DIAG total bed properties logged: {}\n"), dumped);
    }
}

// rc.51: find the nearest FURNACE / fueled-crafting-station
// actor to the given pawn. Used when activity is CantReachFurnace.
//
// Furnaces inherit from BP_FueledCraftingStation_C; concrete
// classes include BP_BasicFurnace_C and BP_BasicForge_C. They
// each have at least one UMorAIBehaviorPointComponent that
// NPCs path to for interaction. For simplicity we teleport to
// the actor's root location with the 75 cm offset applied at
// the call site — the NPC's settlement/AI logic should
// re-route from there.
//
// Note: we don't filter by "occupied" here because furnaces
// don't have a simple bIsBeingUsed flag like beds — they're
// "occupied" only briefly during craft interactions. Multiple
// metalworkers can share one furnace by queueing.
UObject* npcFindNearestFurnace(UObject* pawn)
{
    if (!pawn || !isObjectAlive(pawn)) return nullptr;
    float px, py, pz;
    if (!npcReadPawnLocation(pawn, px, py, pz)) return nullptr;

    static const wchar_t* kFurnaceClasses[] = {
            STR("BP_BasicFurnace_C"),
            STR("BP_BasicForge_C"),
            STR("BP_FueledCraftingStation_C"),
    };
    UObject* best = nullptr;
    float bestDist2 = std::numeric_limits<float>::max();
    for (const wchar_t* clsName : kFurnaceClasses)
    {
        std::vector<UObject*> chunk;
        if (!findAllOfSafe(clsName, chunk)) continue;
        for (UObject* st : chunk)
        {
            if (!st || !isObjectAlive(st)) continue;
            std::wstring cls = safeClassName(st);
            if (cls.size() >= 9 && cls.substr(0, 9) == STR("Default__")) continue;
            float sx, sy, sz;
            if (!npcReadPawnLocation(st, sx, sy, sz)) continue;
            float dx = sx - px, dy = sy - py, dz = sz - pz;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                best = st;
            }
        }
    }
    return best;
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
            STR("BP_Bed_Mansion_C"), // rc.42: missed earlier
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
            if (cls.size() >= 9 && cls.substr(0, 9) == STR("Default__")) continue;

            // Skip occupied. UPROPERTY 'bIsBeingUsed' bool at 0x0620.
            auto* busy = bed->GetValuePtrByPropertyNameInChain<bool>(STR("bIsBeingUsed"));
            if (busy && *busy) continue;

            float bx, by, bz;
            if (!npcReadPawnLocation(bed, bx, by, bz)) continue;
            float dx = bx - px, dy = by - py, dz = bz - pz;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                best = bed;
            }
        }
    }
    return best;
}

// rc.47: cache/refresh global manager singletons.
UObject* getOrFindTimeManager()
{
    UObject* tm = m_cachedTimeManager.Get();
    if (tm && isObjectAlive(tm)) return tm;
    std::vector<UObject*> v;
    if (!findAllOfSafe(STR("TimeManager"), v)) return nullptr;
    for (UObject* o : v)
    {
        if (!o || !isObjectAlive(o)) continue;
        std::wstring c = safeClassName(o);
        if (c.size() >= 9 && c.substr(0, 9) == STR("Default__")) continue;
        m_cachedTimeManager = RC::Unreal::FWeakObjectPtr(o);
        return o;
    }
    return nullptr;
}
UObject* getOrFindNpcManager()
{
    UObject* nm = m_cachedNpcManager.Get();
    if (nm && isObjectAlive(nm)) return nm;
    std::vector<UObject*> v;
    if (!findAllOfSafe(STR("MorNPCManager"), v)) return nullptr;
    for (UObject* o : v)
    {
        if (!o || !isObjectAlive(o)) continue;
        std::wstring c = safeClassName(o);
        if (c.size() >= 9 && c.substr(0, 9) == STR("Default__")) continue;
        m_cachedNpcManager = RC::Unreal::FWeakObjectPtr(o);
        return o;
    }
    return nullptr;
}

// SEH-only int32 read at a runtime offset. Separate noexcept
// function with no C++ objects so __try / __except is permitted
// (mixing SEH and try/catch in the same body is forbidden by MSVC).
static int32 seh_readInt32At(const uint8_t* base, int32 off) noexcept
{
    __try
    {
        return *reinterpret_cast<const int32*>(base + off);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// rc.47: read ATimeManager.CurrentPeriodIndex.
// v7.2.0-rc.2: offset reflectively resolved + cached on first call;
// falls back to documented +0x0278 (per CXXHeaderDump line 11109)
// if reflection is unavailable. The actual memory read is SEH-
// wrapped via seh_readInt32At so any layout mismatch survives.
int32 readTimePeriodIndex(UObject* tm)
{
    if (!tm) return -1;
    if (m_off_timeManagerPeriodIdx == -2)
    {
        m_off_timeManagerPeriodIdx = -1; // pessimistic
        UClass* cls = nullptr;
        try
        {
            cls = tm->GetClassPrivate();
        }
        catch (...)
        {
        }
        if (cls)
        {
            try
            {
                for (auto* p : cls->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    std::wstring pn;
                    try
                    {
                        pn = p->GetName();
                    }
                    catch (...)
                    {
                        continue;
                    }
                    if (pn == STR("CurrentPeriodIndex"))
                    {
                        int32 o = -1;
                        try
                        {
                            o = p->GetOffset_Internal();
                        }
                        catch (...)
                        {
                        }
                        if (o >= 0) m_off_timeManagerPeriodIdx = o;
                        break;
                    }
                }
            }
            catch (...)
            {
            }
        }
        if (s_verbose)
            VLOG(STR("[NpcRecovery] CurrentPeriodIndex offset resolved: 0x{:04x} (fallback 0x0278)\n"),
                 (unsigned)(m_off_timeManagerPeriodIdx >= 0 ? m_off_timeManagerPeriodIdx : 0x0278));
    }
    int32 off = (m_off_timeManagerPeriodIdx >= 0) ? m_off_timeManagerPeriodIdx : 0x0278;
    return seh_readInt32At(reinterpret_cast<const uint8_t*>(tm), off);
}

// v7.2.0-rc.2: lazy reflective resolve of the NpcInfo traversal
// offsets. Walks AMorNPCManager → NpcInfo (FStructProperty) →
// FMorNPCInfoArray → Items (FArrayProperty) → FMorNPCInfo
// (inner FStructProperty) → PersistentData / CurrentActivity /
// InterruptedActivity. Caches resolved offsets in m_off_* fields.
// Caller falls back to documented constants if any step fails.
void ensureNpcInfoOffsets(UObject* npcManager)
{
    if (m_off_npcInfoOnMgr != -2) return; // already tried
    m_off_npcInfoOnMgr = -1;              // pessimistic until full success
    if (!npcManager) return;

    UClass* mgrCls = nullptr;
    try
    {
        mgrCls = npcManager->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (!mgrCls) return;

    // 1. AMorNPCManager.NpcInfo (FStructProperty -> FMorNPCInfoArray)
    RC::Unreal::FProperty* npcInfoProp = nullptr;
    try
    {
        for (auto* p : mgrCls->ForEachPropertyInChain())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
                continue;
            }
            if (pn == STR("NpcInfo"))
            {
                npcInfoProp = p;
                break;
            }
        }
    }
    catch (...)
    {
        return;
    }
    if (!npcInfoProp) return;
    int32 mgrInfoOff = -1;
    try
    {
        mgrInfoOff = npcInfoProp->GetOffset_Internal();
    }
    catch (...)
    {
        return;
    }
    if (mgrInfoOff < 0) return;

    auto* infoArrSp = static_cast<RC::Unreal::FStructProperty*>(npcInfoProp);
    RC::Unreal::UScriptStruct* infoArrStruct = nullptr;
    try
    {
        infoArrStruct = infoArrSp->GetStruct();
    }
    catch (...)
    {
        return;
    }
    if (!infoArrStruct) return;

    // 2. FMorNPCInfoArray.Items (FArrayProperty<FMorNPCInfo>)
    RC::Unreal::FArrayProperty* itemsProp = nullptr;
    try
    {
        for (auto* p : infoArrStruct->ForEachPropertyInChain())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
                continue;
            }
            if (pn == STR("Items"))
            {
                itemsProp = static_cast<RC::Unreal::FArrayProperty*>(p);
                break;
            }
        }
    }
    catch (...)
    {
        return;
    }
    if (!itemsProp) return;
    int32 itemsOff = -1;
    try
    {
        itemsOff = itemsProp->GetOffset_Internal();
    }
    catch (...)
    {
        return;
    }
    if (itemsOff < 0) return;

    // 3. Inner FMorNPCInfo struct
    auto* innerProp = itemsProp->GetInner();
    if (!innerProp) return;
    auto* innerSp = static_cast<RC::Unreal::FStructProperty*>(innerProp);
    RC::Unreal::UScriptStruct* infoStruct = nullptr;
    try
    {
        infoStruct = innerSp->GetStruct();
    }
    catch (...)
    {
        return;
    }
    if (!infoStruct) return;
    int32 stride = -1;
    try
    {
        stride = infoStruct->GetPropertiesSize();
    }
    catch (...)
    {
        return;
    }
    if (stride <= 0) return;

    // 4. PersistentData / CurrentActivity / InterruptedActivity offsets
    RC::Unreal::FStructProperty* persistProp = nullptr;
    RC::Unreal::FStructProperty* curActProp = nullptr;
    RC::Unreal::FStructProperty* intActProp = nullptr;
    try
    {
        for (auto* p : infoStruct->ForEachPropertyInChain())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
                continue;
            }
            if (pn == STR("PersistentData"))
                persistProp = static_cast<RC::Unreal::FStructProperty*>(p);
            else if (pn == STR("CurrentActivity"))
                curActProp = static_cast<RC::Unreal::FStructProperty*>(p);
            else if (pn == STR("InterruptedActivity"))
                intActProp = static_cast<RC::Unreal::FStructProperty*>(p);
        }
    }
    catch (...)
    {
        return;
    }
    if (!persistProp || !curActProp || !intActProp) return;
    int32 persistOff = -1, curActOff = -1, intActOff = -1;
    try
    {
        persistOff = persistProp->GetOffset_Internal();
        curActOff = curActProp->GetOffset_Internal();
        intActOff = intActProp->GetOffset_Internal();
    }
    catch (...)
    {
        return;
    }

    // 5. NpcGuid inside PersistentData
    int32 npcGuidInnerOff = -1;
    RC::Unreal::UScriptStruct* persistStruct = nullptr;
    try
    {
        persistStruct = persistProp->GetStruct();
    }
    catch (...)
    {
    }
    if (persistStruct)
    {
        try
        {
            for (auto* p : persistStruct->ForEachPropertyInChain())
            {
                if (!p) continue;
                std::wstring pn;
                try
                {
                    pn = p->GetName();
                }
                catch (...)
                {
                    continue;
                }
                if (pn == STR("NpcGuid"))
                {
                    try
                    {
                        npcGuidInnerOff = p->GetOffset_Internal();
                    }
                    catch (...)
                    {
                    }
                    break;
                }
            }
        }
        catch (...)
        {
        }
    }

    // 6. RowName inside Activity row handle struct
    int32 rowNameInnerOff = -1;
    RC::Unreal::UScriptStruct* actStruct = nullptr;
    try
    {
        actStruct = curActProp->GetStruct();
    }
    catch (...)
    {
    }
    if (actStruct)
    {
        try
        {
            for (auto* p : actStruct->ForEachPropertyInChain())
            {
                if (!p) continue;
                std::wstring pn;
                try
                {
                    pn = p->GetName();
                }
                catch (...)
                {
                    continue;
                }
                if (pn == STR("RowName"))
                {
                    try
                    {
                        rowNameInnerOff = p->GetOffset_Internal();
                    }
                    catch (...)
                    {
                    }
                    break;
                }
            }
        }
        catch (...)
        {
        }
    }
    if (persistOff < 0 || curActOff < 0 || intActOff < 0 || npcGuidInnerOff < 0 || rowNameInnerOff < 0) return;

    // All resolved — commit.
    m_off_npcInfoOnMgr = mgrInfoOff;
    m_off_itemsOnInfoArr = itemsOff;
    m_off_morNpcInfoStride = stride;
    m_off_npcInfoNpcGuid = persistOff + npcGuidInnerOff;
    m_off_npcInfoCurRowName = curActOff + rowNameInnerOff;
    m_off_npcInfoIntRowName = intActOff + rowNameInnerOff;

    if (s_verbose)
    {
        VLOG(STR("[NpcRecovery] NpcInfo offsets resolved: NpcInfo=0x{:04x} Items=0x{:04x} stride=0x{:04x} guid=0x{:04x} curRow=0x{:04x} intRow=0x{:04x}\n"),
             (unsigned)mgrInfoOff,
             (unsigned)itemsOff,
             (unsigned)stride,
             (unsigned)(persistOff + npcGuidInnerOff),
             (unsigned)(curActOff + rowNameInnerOff),
             (unsigned)(intActOff + rowNameInnerOff));
    }
}

// rc.47: walk AMorNPCManager.NpcInfo.Items via direct memory
// reads and invoke `cb(item_ptr, npcGuidBytes)` for each item
// whose CurrentActivity row name OR InterruptedActivity row
// name contains "cantreach". Zero PE calls in the hot loop.
//
// v7.2.0-rc.2: all six offsets resolved via reflection at first
// call by `ensureNpcInfoOffsets`. Documented fallbacks (per
// CXXHeaderDump) used only if reflection fails:
//   AMorNPCManager.NpcInfo              → +0x03A0  (FMorNPCInfoArray)
//   FMorNPCInfoArray.Items              → +0x0108  (TArray<FMorNPCInfo>)
//   FMorNPCInfo stride                  → 0x0260
//   FMorNPCInfo.PersistentData.NpcGuid  → +0x001C (FGuid, 16 bytes)
//   FMorNPCInfo.CurrentActivity.RowName → +0x0218 (FName)
//   FMorNPCInfo.InterruptedActivity.RowName → +0x0228
//   TArray.Data @ +0x00, TArray.ArrayNum @ +0x08 — engine-stable.
template <typename CB>
void scanNpcInfoForCantReach(UObject* npcManager, CB&& cb)
{
    if (!npcManager || !isObjectAlive(npcManager)) return;
    ensureNpcInfoOffsets(npcManager);

    const int32 offNpcInfo = (m_off_npcInfoOnMgr >= 0) ? m_off_npcInfoOnMgr : 0x03A0;
    const int32 offItems = (m_off_itemsOnInfoArr >= 0) ? m_off_itemsOnInfoArr : 0x0108;
    const int32 kItemStride = (m_off_morNpcInfoStride >= 0) ? m_off_morNpcInfoStride : 0x0260;
    const int32 kOffNpcGuid = (m_off_npcInfoNpcGuid >= 0) ? m_off_npcInfoNpcGuid : 0x001C;
    const int32 kOffCurRowName = (m_off_npcInfoCurRowName >= 0) ? m_off_npcInfoCurRowName : 0x0218;
    const int32 kOffIntRowName = (m_off_npcInfoIntRowName >= 0) ? m_off_npcInfoIntRowName : 0x0228;

    const uint8_t* mgrBase = reinterpret_cast<const uint8_t*>(npcManager);
    // Probe-readable check around the TArray header.
    if (!isReadableMemory(mgrBase + offNpcInfo + offItems, 16)) return;

    const uint8_t* arrayHeader = mgrBase + offNpcInfo + offItems;
    uint8_t* itemsData = *reinterpret_cast<uint8_t* const*>(arrayHeader + 0x00);
    int32 itemsNum = *reinterpret_cast<const int32*>(arrayHeader + 0x08);

    if (!itemsData || itemsNum <= 0 || itemsNum > 1024) return;

    for (int32 i = 0; i < itemsNum; ++i)
    {
        uint8_t* item = itemsData + (int64_t)i * kItemStride;
        if (!isReadableMemory(item, kItemStride)) continue;

        // rc.51: capture the matched row name so the caller
        // can pass it as activityNameHint to onNpcBlockedActivityEvent.
        std::wstring matched;
        {
            std::wstring rowName = seh_fnameToString(item + kOffCurRowName);
            if (!rowName.empty())
            {
                std::wstring lo = rowName;
                for (auto& c : lo)
                    c = (wchar_t)towlower(c);
                if (lo.find(STR("cantreach")) != std::wstring::npos) matched = rowName;
            }
        }
        if (matched.empty())
        {
            std::wstring rowName = seh_fnameToString(item + kOffIntRowName);
            if (!rowName.empty())
            {
                std::wstring lo = rowName;
                for (auto& c : lo)
                    c = (wchar_t)towlower(c);
                if (lo.find(STR("cantreach")) != std::wstring::npos) matched = rowName;
            }
        }
        if (matched.empty()) continue;

        // Pass guid bytes + item pointer + matched activity name.
        cb(item + kOffNpcGuid, item, matched);
    }
}

// rc.47: given a 16-byte NpcGuid, find the matching controller
// in m_npcControllerCache. Iterates cache, reads each ctrl's
// pawn->NPC->NpcGuid via reflection, byte-compares.
UObject* findControllerByGuid(const uint8_t* targetGuid)
{
    if (!targetGuid) return nullptr;
    for (auto& wctrl : m_npcControllerCache)
    {
        UObject* ctrl = wctrl.Get();
        if (!ctrl || !isObjectAlive(ctrl)) continue;
        auto* pawnPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
        UObject* pawn = (pawnPtr && *pawnPtr) ? *pawnPtr : nullptr;
        if (!pawn || !isObjectAlive(pawn)) continue;
        auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
        UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
        if (!npcComp || !isObjectAlive(npcComp)) continue;
        auto* g = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
        if (!g || !isReadableMemory(g, 16)) continue;
        if (std::memcmp(g, targetGuid, 16) == 0) return ctrl;
    }
    return nullptr;
}

// [rc.139] On-demand "Unstuck NPCs" pass — bound to
// BIND_UNSTUCK_NPCS (default Num-). This replaced the automatic
// day-cycle / character-load scanning loop (mod users reported
// stutter from the background work). One press = refresh the
// controller cache once, walk NpcInfo once, teleport every
// CantReach* NPC via the same pipeline the automatic scan used.
void runUnstuckNpcsNow()
{
    if (!m_characterLoaded && !m_isDedicatedServer)
    {
        showInfoMessage(Loc::get("npc.unstuck_load_world"));
        return;
    }

    // Cache refresh moved here from the retired 5 s background
    // refresh — the FindAllOf cost is now paid only on keypress.
    npcRefreshControllerCache();

    UObject* nm = getOrFindNpcManager();
    if (!nm)
    {
        VLOG(STR("[NpcRecovery] [Unstuck] NPC manager not available\n"));
        showInfoMessage(Loc::get("npc.unstuck_no_manager"));
        return;
    }

    int triggered = 0;
    int scanned = 0;
    m_unstuckManualPass = true;
    scanNpcInfoForCantReach(nm, [&](const uint8_t* guidBytes, uint8_t* item, const std::wstring& activityName) {
        ++scanned;
        UObject* ctrl = findControllerByGuid(guidBytes);
        if (!ctrl) return;
        auto* pawnPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
        UObject* pawn = (pawnPtr && *pawnPtr) ? *pawnPtr : nullptr;
        if (!pawn || !isObjectAlive(pawn)) return;
        auto* npcCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("NPC"));
        UObject* npcComp = (npcCompPtr && *npcCompPtr) ? *npcCompPtr : nullptr;
        if (!npcComp) return;
        m_lastEventTeleportFired = false;
        onNpcBlockedActivityEvent(npcComp, activityName);
        if (m_lastEventTeleportFired) ++triggered;
    });
    m_unstuckManualPass = false;

    VLOG(STR("[NpcRecovery] [Unstuck] manual pass: {} blocked NPC(s) found, {} teleported\n"), scanned, triggered);
    wchar_t msg[96];
    swprintf_s(msg, L"Unstuck NPCs: %d blocked, %d teleported", scanned, triggered);
    showInfoMessage(msg);
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
    for (int i = 0; i < 16; ++i)
        if (myGuid[i] != 0)
        {
            allZero = false;
            break;
        }
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
        for (UObject* b : chunk)
        {
            if (!b || !isObjectAlive(b)) continue;
            std::wstring c = safeClassName(b);
            if (c.size() >= 9 && c.substr(0, 9) == STR("Default__")) continue;
            beds.push_back(b);
        }
    }

    // Verbose diagnostic — once per scan, log bed count and
    // first bed's class. Tells us immediately if enumeration
    // is finding anything.
    if (s_verbose)
    {
        static size_t s_lastLoggedBedCount = (size_t)-1;
        if (beds.size() != s_lastLoggedBedCount)
        {
            s_lastLoggedBedCount = beds.size();
            std::wstring firstCls = beds.empty() ? std::wstring(STR("(none)")) : safeClassName(beds[0]);
            VLOG(STR("[NpcRecovery] bed-walk found {} bed actor(s); first cls='{}'\n"), (int)beds.size(), firstCls.c_str());
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

        if (seh_scan_for_guid(reinterpret_cast<const uint8_t*>(bed), kScanLimit, myGuid)) return bed;
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
bool npcGetBlackboardVector(UObject* blackboardComp, RC::Unreal::FName keyName, float& ox, float& oy, float& oz)
{
    if (!blackboardComp || !isObjectAlive(blackboardComp)) return false;
    auto* fn = blackboardComp->GetFunctionByNameInChain(STR("GetValueAsVector"));
    if (!fn) return false;
    auto* pKey = findParam(fn, STR("KeyName"));
    auto* pRet = findParam(fn, STR("ReturnValue"));
    if (!pKey || !pRet) return false;
    std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
    *reinterpret_cast<RC::Unreal::FName*>(buf.data() + pKey->GetOffset_Internal()) = keyName;
    try
    {
        safeProcessEvent(blackboardComp, fn, buf.data());
    }
    catch (...)
    {
        return false;
    }
    float* v = reinterpret_cast<float*>(buf.data() + pRet->GetOffset_Internal());
    if (!isReadableMemory(v, sizeof(float) * 3)) return false;
    // Reject FLT_MAX sentinel and (0,0,0).
    const float kSentinel = 1e30f;
    if (v[0] > kSentinel || v[1] > kSentinel || v[2] > kSentinel) return false;
    if (v[0] < -kSentinel || v[1] < -kSentinel || v[2] < -kSentinel) return false;
    if (v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f) return false;
    ox = v[0];
    oy = v[1];
    oz = v[2];
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
bool npcGetBlackboardActor(UObject* blackboardComp, RC::Unreal::FName keyName, UObject*& outActor)
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
    try
    {
        safeProcessEvent(blackboardComp, fn, buf.data());
    }
    catch (...)
    {
        return false;
    }
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
        auto* tgtKeyPtr = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(STR("TargetBlackboardKeyName"));
        auto* bbCompPtr2 = state->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComponent"));
        if (tgtKeyPtr && bbCompPtr2 && *bbCompPtr2)
        {
            UObject* tgtActor = nullptr;
            if (npcGetBlackboardActor(*bbCompPtr2, *tgtKeyPtr, tgtActor) && tgtActor)
            {
                if (npcReadPawnLocation(tgtActor, ox, oy, oz)) return true;
            }
        }
        // (2) Fallback to vector key.
        auto* keyNamePtr = state->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(STR("BlackboardKeyName"));
        if (keyNamePtr && bbCompPtr2 && *bbCompPtr2)
        {
            if (npcGetBlackboardVector(*bbCompPtr2, *keyNamePtr, ox, oy, oz)) return true;
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
            if (x < kSentinel && y < kSentinel && z < kSentinel && x > -kSentinel && y > -kSentinel && z > -kSentinel && (x != 0.0f || y != 0.0f || z != 0.0f))
            {
                ox = x;
                oy = y;
                oz = z;
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
    try
    {
        safeProcessEvent(npcComp, fn, buf.data());
    }
    catch (...)
    {
        return {};
    }
    int32 retOff = pRet->GetOffset_Internal();
    if (retOff < 0) return {};
    uint8_t* retBase = buf.data() + retOff;
    if (!isReadableMemory(retBase, 16)) return {};
    // FMorNPCActivityRowHandle layout: DataTable* @0, FName @8.
    return seh_fnameToString(retBase + 8);
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
            if (cls.size() >= 9 && cls.substr(0, 9) == STR("Default__")) continue;
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
            VLOG(STR("[NpcRecovery] cache refresh: {} friendly NPC controller(s) live\n"), (int)m_npcControllerCache.size());
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
        for (auto it = m_npcRecoveryStates.begin(); it != m_npcRecoveryStates.end();)
        {
            if (live.find(it->first) == live.end())
                it = m_npcRecoveryStates.erase(it);
            else
                ++it;
        }
    }
}

// Walk a UStruct's full property chain and log each property's
// name + type + offset. Used at probe time to discover the
// unknown UPROPERTY names on UFGKActorFSMComponent.
void npcProbeLogPropertyChain(UObject* obj, const wchar_t* label)
{
    if (!obj || !isObjectAlive(obj)) return;
    UClass* cls = nullptr;
    try
    {
        cls = obj->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (!cls) return;
    std::wstring clsName;
    try
    {
        clsName = cls->GetName();
    }
    catch (...)
    {
        clsName = STR("?");
    }
    // [Phase 3] silenced — NpcProbe (150 lines/session) was the
    // discovery probe; NPC layout is known.
    (void)label;
    (void)clsName;
    // try { for (auto* prop : cls->ForEachPropertyInChain()) ... } catch (...) {}
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
    VLOG(STR("[NpcProbe] target controller: {} at {:p}\n"), probeCls.c_str(), (void*)probeCtrl);

    // ---- TODO #2: is AAIController.Pawn reflectively accessible? ----
    UObject* pawn = nullptr;
    {
        auto* pawnPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
        if (pawnPtr && *pawnPtr) pawn = *pawnPtr;
        VLOG(STR("[NpcProbe] [#2] AAIController.Pawn reflective read: ptr={:p} pawn={:p}\n"), (void*)pawnPtr, (void*)pawn);
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
                try
                {
                    safeProcessEvent(probeCtrl, fn, buf.data());
                }
                catch (...)
                {
                }
                pawn = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                VLOG(STR("[NpcProbe] [#2] K2_GetPawn returned pawn={:p}\n"), (void*)pawn);
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
        VLOG(STR("[NpcProbe] pawn->RootComponent: ptr={:p} comp={:p}\n"), (void*)rcPtr, (void*)rootComp);
        if (rootComp && isObjectAlive(rootComp))
        {
            auto* relLocPtr = rootComp->GetValuePtrByPropertyNameInChain<float>(STR("RelativeLocation"));
            if (relLocPtr)
            {
                VLOG(STR("[NpcProbe] pawn loc (direct mem): ({:.1f}, {:.1f}, {:.1f})\n"), relLocPtr[0], relLocPtr[1], relLocPtr[2]);
            }
        }

        // PE-fallback location read for cross-check.
        if (auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
        {
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (pRet)
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(pawn, fn, buf.data());
                }
                catch (...)
                {
                }
                auto* peLoc = reinterpret_cast<float*>(buf.data() + pRet->GetOffset_Internal());
                VLOG(STR("[NpcProbe] pawn loc (PE K2_GetActorLocation): ({:.1f}, {:.1f}, {:.1f})\n"), peLoc[0], peLoc[1], peLoc[2]);
            }
        }
    }

    // ---- BehaviorFSMComp ----
    auto* fsmPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
    UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
    VLOG(STR("[NpcProbe] controller->BehaviorFSMComp: ptr={:p} comp={:p}\n"), (void*)fsmPtr, (void*)fsmComp);

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
            VLOG(STR("[NpcProbe] [#1a] FSMRoot ptr={:p} obj={:p}\n"), (void*)rootPtr, (void*)root);
            if (root && isObjectAlive(root))
            {
                npcProbeLogPropertyChain(root, STR("[#1a] FSMRoot"));
            }
        }

        // (b) AllStates — TArray<UFGKState*>. Dump first ~10 entries:
        //     class name + check for an active flag.
        {
            auto* allStatesPtr = fsmComp->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("AllStates"));
            if (allStatesPtr)
            {
                int n = allStatesPtr->Num();
                VLOG(STR("[NpcProbe] [#1b] AllStates.Num() = {}\n"), n);
                int dumpLimit = (n > 10) ? 10 : n;
                for (int i = 0; i < dumpLimit; ++i)
                {
                    UObject* state = (*allStatesPtr)[i];
                    if (!state || !isObjectAlive(state))
                    {
                        VLOG(STR("[NpcProbe] [#1b]   AllStates[{}] = null/dead\n"), i);
                        continue;
                    }
                    std::wstring sCls = safeClassName(state);
                    VLOG(STR("[NpcProbe] [#1b]   AllStates[{}] = {:p} class={}\n"), i, (void*)state, sCls.c_str());

                    // Probe candidate "is active" field names on each state.
                    const wchar_t* activeFlags[] = {
                            STR("bIsActive"),
                            STR("bActive"),
                            STR("bIsCurrent"),
                            STR("bIsRunning"),
                            STR("bEntered"),
                    };
                    for (const wchar_t* fname : activeFlags)
                    {
                        auto* fptr = state->GetValuePtrByPropertyNameInChain<bool>(fname);
                        if (fptr)
                        {
                            VLOG(STR("[NpcProbe] [#1b]     {}={}  (field '{}')\n"), fname, *fptr ? STR("TRUE") : STR("false"), fname);
                        }
                    }

                    // Dump first state fully so we see UFGKState's complete shape.
                    if (i == 0)
                    {
                        npcProbeLogPropertyChain(state, STR("[#1b] AllStates[0] full prop dump"));
                    }
                }
            }
            else
            {
                VLOG(STR("[NpcProbe] [#1b] AllStates property not reachable\n"));
            }
        }

        // (c) RunTimeStates — alternate access path (TArray).
        {
            auto* rtsPtr = fsmComp->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("RunTimeStates"));
            if (rtsPtr)
            {
                VLOG(STR("[NpcProbe] [#1c] RunTimeStates.Num() = {}\n"), rtsPtr->Num());
                int dumpLimit = (rtsPtr->Num() > 5) ? 5 : rtsPtr->Num();
                for (int i = 0; i < dumpLimit; ++i)
                {
                    UObject* state = (*rtsPtr)[i];
                    if (!state || !isObjectAlive(state)) continue;
                    std::wstring sCls = safeClassName(state);
                    VLOG(STR("[NpcProbe] [#1c]   RunTimeStates[{}] = {:p} class={}\n"), i, (void*)state, sCls.c_str());
                }
            }
        }
    }

    // ---- TODO #3 (rc.2): re-aimed. PathFollowingComponent lives ----
    // on the AIController in standard UE4, not the pawn. Probe both
    // sides + try Mor-prefixed alt names.
    {
        struct ProbeTarget
        {
            UObject* obj;
            const wchar_t* label;
        };
        ProbeTarget targets[] = {
                {probeCtrl, STR("[#3a] controller->PathFollowingComponent")},
                {probeCtrl, STR("[#3a-alt] controller->MorPathFollowingComponent")},
                {pawn, STR("[#3b] pawn->PathFollowingComponent")},
                {pawn, STR("[#3b-alt] pawn->MorPathFollowingComponent")},
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
            VLOG(STR("[NpcProbe] {}: ptr={:p} comp={:p}\n"), targets[i].label, (void*)pfcPtr, (void*)pfc);
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
