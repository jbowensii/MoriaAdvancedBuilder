---
name: goat-final-architecture
description: "Porter Goat DEFINITIVE architecture + every hard-won lesson as of v8.2.x (2026-07-12): movement stack (FSM disable + gait + MoveToActor), interaction model (follow-always, bell dismiss/recall, menu=Saddlebags only), storage/input (UI-manager owns input mode), identity/persistence, and the full dead-end list. Read this BEFORE touching moria_goat.inl."
metadata:
  type: project
  originSessionId: 7b117499-8365-4ea5-8fd8-5566c3f2bd52
---

# Porter Goat — Final Architecture (v8.2.x, 2026-07-12)

## ⭐ EPHEMERAL PIVOT (commit 60be7cd — supersedes the identity sections below)
USER DECISION: the ONLY persistent state is the saddlebag pack in the PLAYER inventory (native character save, proven). The goat is EPHEMERAL:
- **Bell = plain toggle**: goat present → K2_DestroyActor (destroying is safe — goat carries nothing); absent → spawnBellGoat fresh in front of player. No hide/park/recall, no bellDismissed, no movement-freeze tricks.
- **Goat is NEVER REGISTERED**: no RegisterWithNPCManager, no NpcInfo writes, no adopt scans. Unregistered = passive fauna = no settlement brain, no escort catch-up teleport, no native restore — the regime where follow provably worked (rc.46-71).
- **NO load-time goat handling at all** (commit a201b1c 2026-07-13): the brief stray-sweep was removed too — it only served the dev test worlds with leftover NpcInfo markers, which the user backed up before testing. Fresh/clean worlds never have marker goats. tickAdoptNativeGoat gutted; dedupe call + B6 save-moment StoreRuntimeActor call removed.
- **Saddlebags flow unchanged**: binds the pack container in the PLAYER inventory; the goat is only the E-menu anchor + auto-equipped visual. No linking needed for a fresh goat.
- Trade-offs accepted: no goat standing where you left it (re-ring), nameplate may be generic (no NpcInfo name).
- Still relevant below: movement stack (FSM disable + gait + drive), menu simplification, storage/UI/input, name-encoding rule (historical), API lessons. The "identity/restore" + "native restore gotchas" sections describe the RETIRED design (kept for archaeology — do not rebuild it).

User-confirmed working before pivot: follow, 8×8 saddlebag UI, contents persistence through reload. Stay RETIRED by user decision.

## Interaction model (user-approved final)
- **Follow is permanent** — the goat always follows; there is no Stay.
- **Bell** (craftable: Tobi's `PorterGoatBell` recipe → `Tool.BellGoat`, Workbench, 10s) = dismiss (hide actor, cargo+identity kept alive, DisableMovement so it can't fall through floor) / recall (unhide, teleport to player, follow). State machine keys on `bellDismissed` flag (presence-based), NOT stayMode.
- **Goat menu = Saddlebags/Equip row only.** The Follow/Stay row (Tobi's Talk slot) is removed two ways: `bTalkInteractionEnabled=false` on MorNPCComponent (did NOT hide the row on Tobi v1.12 — row ignores the flag) + click-neutralizer in the dispatch hook (forces Follow, collapses the row widget). Internal onGoatFollow/onGoatStay remain — bell dismiss parks via the stay logic.

## Movement stack — why each piece exists (ALL required)
1. **FSM disable** (`stopGoatBrainLogic`): Moria NPC AI = FGK FSM (`FGKActorFSMComponent`, e.g. `BodyFSMComp`) on BOTH pawn and controller — NOT a UE BehaviorTree (`AIController.BrainComponent` is NULL on the goat; `UBrainComponent::StopLogic` was a proven wrong lever). Registered NPCs (rc.112+) run settlement idle/wander ("At Ease") through this FSM, which overrides external MoveToActor. Disable = walk ObjectProperty fields on pawn+controller, class name contains "FSM" → `Deactivate` + `SetComponentTickEnabled(false)` (both BlueprintCallable). Fired one-shot ~1s post-spawn (ticksSinceSpawn>60, after possession) and on Follow/Stay presses. 10s **quiet watchdog**: `IsActive()` query first, re-disable ONLY if the game re-activated it (user: no blind re-asserts).
2. **Gait** (`setGoatGaitRunning`): `AFGKBaseCharacter::SetGait(EFGKGait)` BlueprintCallable, Walking=0/Running=1/Sprinting=2. Default Walking gait moves ~33-80 u/s vs MaxWalkSpeed 600 — the goat "followed" at a crawl, fell behind, native teleport did the rest. Running gait = real following.
3. **Drive**: 1 Hz `AAIController::MoveToActor(Goal=player, AcceptanceRadius=250, bUsePathfinding=true)` in tickFollowGoats when !stayMode; StopMovement each second when stayMode. This is THE mover — proven twice (v1.10.0 LeashActor instrumentation, v1.12.0 rc.138 drive-off experiment: goat NEVER self-walks; the porter BT does not consume LeashActor). LeashActor writes kept as no-op priming in case Tobi wires it.
4. **Movement mode**: force `SetMovementMode(Walking=1)` at spawn one-shot + Follow + bell recall (a rc.130 dismiss DisableMovement can strand a goat after dedupe swaps).
5. **Porter role**: `SetRoleFuzzy("Porter")` at spawn default + Follow (cosmetic label + primes native path; role does NOT drive movement with FSM down).

## Known limits (mod-side unfixable)
- **Native escort catch-up teleport**: server-side C++ snaps a registered "following" NPC to ~300u from the player when >~2000u away. No hook, no flag. This is why Stay was retired. Threshold-based, not throttled; fires even on hidden goats (harmless there).
- Tobi's `Goat.Slot.EpicPack` slot container NEVER instantiates (`containers=0` even on fresh v1.12 world) — his DefaultContainers still broken; `MorContainerInstanceSetupComponent` is the lead for his editor fix.

## Storage / UI / input
- Saddlebag UI = OUR chest-mode takeover of `WBP_UI_Inventory_Screen_StorageMode_C` (parallel widget, container driven via 'Set Up Storage Container' with member writes: storageHandle/storageInventoryComponent/InventoryComponent/StorageScreenRef; InteractableRef NULLED to kill NPC-type classification; show events fired BEFORE the drive; 4Hz visibility re-assert while open).
- **INPUT MODE IS UI-MANAGER-OWNED**: the FGK UI manager pops input to GameOnly whenever ITS screen stack is empty — our screen isn't registered with it, so single `SetInputMode_UIOnlyEx` calls get reverted (each suppression collapse of the native singleton re-triggers the pop). Old-world "working" drag-drop was parasitic on the manager coincidentally holding a screen open. FIX: 10 Hz `setInputModeUI` re-assert while our widget is open, skipped while LMB held (refocus would cancel drags). LONG-TERM: route through the manager (Tobi's BP wires `GetScreen(WBP_UI_Inventory_Screen_StorageMode_C)` + `GetScreen(WBP_UI_SelectSettlementScreen_C)`) and re-bind the container on the manager's instance.
- **NEVER RemoveFromParent the native StorageMode singleton** — Show() can't re-add it; world chests die for the session. SetVisibility(Collapsed) only.
- Contents persistence = the pack item lives in the PLAYER inventory (native character save serializes sub-contents). Goat-side channels all closed (see [[npc-save-restore-architecture]], [[tobi-1.9.0-integration]] dead-end list).

## ✅ FEATURE COMPLETE 2026-07-13 (user: "all is working")
Verified in-game: LMB bell ring (summon/dismiss/re-summon), follow at pace, Saddlebags menu + drag-drop, contents persistence, F10 reposition, AB toggle. KNOWN ACCEPTED COSMETIC: "NpcId Invalid" (IsNpcInteracting/GetNpcSchedule) log spam PERSISTS even with MorNPCComponent tick disabled → the poller is game-side (manager/interaction system iterating NPC comps), inherent to an unregistered NPC-component-bearing actor. User accepted (no gameplay perf impact — failed map lookup + log line). Tick-off mitigation LEFT IN (harmless, menu unaffected). Next mitigation candidates if ever wanted: UE4SS log-category suppression or registering a dummy id (rejected — reintroduces registration).

## Ephemeral round 2 (2026-07-13, commit 812e3c8) — bell WORKS (user-confirmed) + LMB ring + spam
Bell spawn/destroy toggle user-confirmed working. Added: (1) **LMB rings the bell when it's in hand** — the melee-swing path never carries the item ID (unhookable); instead m_bellInHand is tracked via ItemEquipped/ItemUnequipped WIDE-MATCH events (bell ID at parm 0), and a gameplay LMB edge (cursor hidden via m_bpShowMouseCursor FBoolProperty, no mod UI open) fires toggleGoatFromBell (2s cooldown absorbs spam; swing anim plays = thematic). (2) **"NpcId Invalid" spam (IsNpcInteracting/GetNpcSchedule)**: caused by the UNREGISTERED goat's MorNPCComponent polling the NPC manager every tick with an unknown id — the ephemeral trade-off surfacing in the log. Mitigation: SetComponentTickEnabled(false) on MorNPCComponent in the setup one-shot (E-menu rows are event-driven and expected to keep working; REVERT this if the Saddlebags menu breaks).

## Ephemeral verification round 1 (2026-07-13, commit 72566ff)
User test: summon ✓ dismiss ✓ — but re-summon failed: **destroyed actors linger until GC and isObjectAlive PASSES on them**; findAnyGoatInWorld kept returning the corpse → 4 rings all "dismissed" it; 4 min later (post-GC) summon worked. FIX v2 (859d90c): `isGoatActorUsable()` = isObjectAlive + **KismetSystemLibrary::IsValid** PE call (false for pending-kill). ⚠ `AActor::IsActorBeingDestroyed` is a PLAIN C++ INLINE in 4.27, NOT a UFUNCTION — the v1 filter silently no-op'd (GetFunctionByNameInChain null → passed everything). GENERAL RULE: after K2_DestroyActor, filter re-finds with KSL IsValid; and never assume an engine method is PE-callable without checking the UHT dump. ALSO fixed same round (non-goat): rotation display + NBB spawners were once-per-SESSION (m_*SpawnAttempted never cleared at world unload) → widgets stale after ANY reload, F10 SEH'd + circles never showed; world-unload reset now clears them. F10 now auto-activates the Advanced Builder + creates/shows NBB + message window (user: "F10 restores the AB UI"); circles show in reposition mode regardless of the '=' toggle. Draggable in reposition mode: inspect window + rotation circles; NBB is embedded in the game's own action bar (not free-floating) and the message box has no drag — flagged as possible follow-up.

## Bell summon delivery (2026-07-12, commit 7455de3 — the "bell broke" post-mortem)
Git-archaeology finding: the bell code never changed — its DELIVERY ecosystem did. All along, `ServerTeleportTo` **silently fails at range** (return unchecked; encroachment at the player's exact spot); recall "worked" because the native escort catch-up teleport cleaned up after it. Killing the FSM (for Stay/wander) removed that net, and native restore started placing the goat at its saved location km away (log: distToPlayer=14120; after recall the goat FELL through unstreamed world at 2000-3000 u/s, dist 17k→23k). FIX: summon = dest 250u in FRONT of player +50 up; PRIMARY `K2_TeleportTo` (movement-component-aware, checked bool, logged OK/FAILED); fallback `K2_SetActorLocation(bTeleport=true)`; ServerTeleportTo kept only as replication follow-up. LESSON: never trust an unchecked teleport RPC; never teleport onto the player's exact location.

## Native restore gotchas (2026-07-12, log-proven)
- **The natively-restored goat comes back UNPOSSESSED** — `Controller` UPROPERTY stays null forever (900+ resolve attempts observed). No controller = no follow drive, no MoveToActor, inert pawn even after ServerTeleportTo ("bell does not summon"). FIX: the controller-resolve loop in tickFollowGoats fires `SpawnDefaultController` after 3 failed attempts (retries every 2s until possessed).
- A goat saved while bell-dismissed restores HIDDEN — adoptNativeGoat reads actor `bHidden` (bit 0) and sets `bellDismissed` so the first ring RECALLS instead of no-op dismissing.
- **Sidecar REMOVED entirely 2026-07-12** (commit fdf0232) per user — functions, flag, call sites all deleted. Persistence is native-only.

## Identity / restore
- Identity = NpcInfo entry: Name (FText marker, m_goatName 'Rûdh') + NpcGuid + UniqueNpc.RowName='NPCGoat', written idempotently at spawn/adopt (writeGoatNameDirectToNpcInfoEntry / writeUniqueNpcRowNameToEntry).
- **Single-goat gate**: AutoRestore skips ALL orphan-marker spawns when any live goat exists (native restore via ValidNpcRestores is now ACTIVE since the NPCGoat row writes → the game restores the goat itself; double-spawn → dedupe churn destroyed nameplates). Stale extra marker (mojibake era, GUID 6FE6…) remains in NpcInfo, inert.
- **Name encoding**: `isGoatNameMatch()` accepts m_goatName AND its UTF-8-double-encoded mojibake (adopt heals the stored name). NEVER round-trip MoriaCppMod.ini through PowerShell Get-Content/Set-Content without explicit UTF8 — it double-encodes 'Rûdh' (burned 2026-07-12; propagated into the save).

## API / stability lessons (general value)
- `seh_getFnInChain` (moria_hism.inl): SEH-wrap GetFunctionByNameInChain during world transitions — isObjectAlive + safeClassName probes BOTH pass on reused memory; only SEH on the chain walk is airtight (crash 0x100000040 @ hism:244).
- FString UFUNCTION params: FMemory::Malloc'd buffer + {ptr,len,len} triple (setRoleFuzzyOnGoat pattern).
- Component discovery: walk super-chain `ForEachProperty` + `CastField<FObjectProperty>` + GetValuePtrByPropertyNameInChain (OwnedComponents is a TSet — NOT TArray-readable; BlueprintCreatedComponents/InstanceComponents are TArrays).
- MorNPCComponent has GetNpcName but NO name setter; interaction rows gate on bXxxInteractionEnabled bools (Manage 0x1A1, Revive 0x358, Rescue 0x511, Recruit 0x6C9, DeliverResearch 0x881, Details 0xA39, Talk 0xBF1) — but Tobi's v1.12 menu rows don't all honor them.
- DLL deploy fails silently-ish while the game runs (file lock) — use a background retry watcher; don't print "[Deployed]" without checking.

Related: [[tobi-1.9.0-integration]] (chronological saga), [[goat-native-follow-mechanism]], [[npc-save-restore-architecture]], [[storage-persistence-api-findings]], [[feedback_activity_set_is_direct_write]]
