# Khazad-dum Advanced Builder Pack v7.2.0-rc.1 — Release Notes

The first release candidate of the 7.2 line, rolling up the entire `npc-recovery`
branch (rc.1 → rc.45) **plus** the Porter Goat collaboration with Tobi Ichiro
(rc.46 → rc.71), and folding in the v7.0.1 / v7.0.2 hotfixes that shipped on
`main` after the v7.0.0 release.

> **Branch:** `npc-recovery` — `main` is still parked at v7.0.2. v7.2.0-rc.1
> rides on the recovery branch until in-game sign-off; on positive sign-off the
> branch will be fast-forwarded to `main` and re-tagged `v7.2.0`.

---

## Headline Features

### NPC Stuck-Pathing Recovery
Friendly dwarves that get stuck (Blocked from Assigned Bed, CantReach Furnace,
etc.) are now automatically teleported to their destination instead of standing
idle forever.

* **Activity-aware trigger** — detects `CantReach*` substring on
  `UMorNPCComponent.GetCurrentActivity()` row names. Catches CantReachBed,
  CantReachAssignedBed, CantReachFurnace, and any future variants without a
  rebuild.
* **Destination resolution** — walks the FSM tree to find
  `FGKBehaviorState_MoveToBlackboardKey` instances, reads the actor-based
  destination first (`TargetBlackboardKeyName` → bed actor → its location),
  then falls back to vector-based `BlackboardKeyName` lookup.
* **Activity-aware fallback router** — when the FSM has no usable destination,
  picks the nearest unoccupied bed / furnace based on the activity name.
  Includes `BP_Bed_Mansion_C` in the bed search; uses the `bIsBeingUsed` flag
  at +0x0620 on `AMorBed` to skip occupied beds.
* **Recurring post-load sweep** — every 30 s for the first 5 minutes after
  character load (handles NPCs that transition to `CantReachBed` at +70 s,
  past the original one-shot probe at +30 s).
* **Teleport primitive** — uses `K2_TeleportTo` (which commits via the
  CharacterMovementComponent and survives the next tick) rather than
  `K2_SetActorLocation` (which `CharacterMovementComponent` re-anchored
  to the nav-mesh on the next tick, making the pawn appear unmoved).
  75 cm offset along the approach vector so the pawn lands just shy of
  the bed/furnace and walks the final step.
* **Performance budget** — 0.2 Hz polling, 5 s controller-cache refresh,
  30 s per-NPC throttle, per-NPC adaptive skip during cooldown. At 30 NPCs,
  worst case is ~12 ProcessEvent calls/sec, normal is 0–4/sec.
* **INI gate** — `[NpcRecovery] Enabled = true|false` in `MoriaCppMod.ini`
  (defaults to ON).

### Porter Goat (Tobi Ichiro collaboration)
A summonable pack goat for the player, shipped in collaboration with Tobi
Ichiro's *Secrets of Khazad-Dum* pak. This is the runtime DLL half of the
collaboration — the mesh, recipe, and BP wiring ride on Tobi's pak.

* **Vanilla interaction menu wiring** — the goat's native E-menu now shows
  *Follow* / *Stay* / *Saddlebags* entries. Routed via PE post-hook on
  `MorInteractionWidget::OnInteractionPress`, with the row label read directly
  from `DisplayText` (FText @ +0x278) via SEH-wrapped reflection.
* **Bell summon / dismiss** — using the bell item summons the goat at the
  player's location; using it again dismisses. Stale-class re-resolve added
  so dismiss + re-summon survives a class GC (previously crashed).
* **Saddlebag access** — Saddlebags entry dispatches `ServerUse` on the
  matched item handle inside the goat's `MorInventoryComponent.Items`
  (FFastArraySerializer walk).
* **Save-graph research** — `moria_goat_save_probes.inl` ships disabled by
  default (`[GoatSaveProbes] Enabled=true` opt-in). Probes A through K cover
  blackboard state, FSM state, DynamicBehaviors TMap decode, and full
  property-chain dumps. For diagnostic builds only.

### Rename popup width fix
The rename popup's `EditableTextBox` was sized 1100×80 from an earlier
full-screen context and overflowed the popup card. Now 480×60 to fit inside
the modal.

---

## What's New Since v7.0.0

### v7.2.0-rc.1 (this release)
Porter Goat menu wiring, NPC recovery rc.31–rc.45 production iteration,
rename popup sizing fix, `s_verbose = false` flipped for shipping default.

### v7.1.0-rc.31 → rc.45 — NPC recovery production
* rc.31..rc.42 — long iteration on bed-assignment data (not reachable via
  reflection or byte-scan; pivoted to nearest-unoccupied-bed). Added
  `BP_Bed_Mansion_C` to bed enumeration. Recurring post-load sweep replaces
  one-shot.
* rc.43 — teleport primitive switched from `K2_SetActorLocation` →
  `K2_TeleportTo` (the former was being re-anchored to nav-mesh next tick).
* rc.44 — CantReach matcher widened from exact bed-only to any
  `cantreach*` substring across all three call sites.
* rc.45 — polling dropped 1 Hz → 0.2 Hz, per-NPC adaptive throttle skips
  NPCs in their 30 s cooldown.

### v7.1.0-rc.21 → rc.30 — Pause-menu and event-driven wiring
* rc.21 — `REVEAL MAP` added to pause-menu button stack. Legacy NUM+/NUM−
  polled handler retired entirely.
* rc.22 — `QUICKBUILD_LOGGING` flipped to 0 (kills `[QB] isPlacementActive`
  log spam).
* rc.23 — BuildHUD show/hide PE post-hooks replace per-frame PE poll in
  `isPlacementActive()`.
* rc.24 — Read `InterruptedActivity` in addition to `CurrentActivity`
  (the *Blocked from Assigned Bed* UI is sourced from the interrupted slot).
* rc.25 — Trigger narrowed to exact `CantReachBed` / `CantReachAssignedBed`.
* rc.26 — PE post-hook on `UMorNPCComponent::SetCurrentActivity` /
  `MorNpcUpdateActivity` for event-driven trigger.
* rc.27 — Actor-based dest read (`TargetBlackboardKeyName` → bed actor)
  before fallback to vector lookup.
* rc.28 — One-shot post-load `CantReach` sweep at +30 s.
* rc.29 — Bed-walk fallback in `onNpcBlockedActivityEvent` (last-resort).
* rc.30 — One-shot diagnostic dump of NpcGuid bytes + first bed's full
  UPROPERTY chain.

### v7.1.0-rc.5 → rc.20 — Algorithm bring-up
* Trigger: NPC's `UMorNPCComponent.GetCurrentActivity()` row name contains
  `cantreach` / `blocked` (case-insensitive).
* Target resolution: walk the FSM state tree from `FSMRoot`, find any
  `FGKBehaviorState_MoveToBlackboardKey`, read its `BlackboardKeyName` +
  `BlackboardComponent` and dispatch `GetValueAsVector` for the persistent
  destination. The state's own `Destination` field caches FLT_MAX when
  dormant; the blackboard holds the live value.
* **Steam ™ path fix** — `moria_session_history.inl` load/save routed
  through `openInputFile`/`openOutputFile` (UTF-8 → UTF-16 wide-path).
* **Steam ™ path fix** — `moria_widget_harvest.inl`
  `CreateDirectoryA` → `CreateDirectoryW(utf8PathToWide())`.
* `[NpcRecovery]` INI section, defaults ON.
* NBB (New Building Bar) freecam-hide restored — the v6.10.0+ NBB widget
  wasn't wired into the FreeCam enter/exit hook that hid the legacy MC
  toolbar.

### v7.1.0-rc.1 → rc.4 — Read-only diagnostic probes
Phase 1 NPC-recovery diagnostic-only builds. Discovered that game NPCs
use `FGKBehaviorState_MoveToBlackboardKey` (NOT `FGKBehaviorState_MoveTo`).
Extended probe to dump 9 candidate FVector fields, 5 FName blackboard-key
fields, and 3 AActor* fields per motion state, gated by `s_verbose` +
one-shot per class.

### v7.0.2 — Mereak's Secrets removed
* `definitions/Mereaks Secrets Changes.ini` + `definitions/Secrets/`
  (6 MODIFY .def files) removed from shipping product.
* `installer/build.ps1` no longer writes the toggle into the generated
  `GameMods.ini`.

### v7.0.1 — Steam ™ hotfix
Three sites silently failed on Steam installs whose path contains the
trademark glyph (`Return to Moria™` → UTF-8 `0xE2 0x84 0xA2`).
The session-history fix is load-bearing — Steam players couldn't see
their recent-worlds list on the JoinWorld screen.

* `moria_session_history.inl` `loadSessionHistory` → `openInputFile()`.
* `moria_session_history.inl` `saveSessionHistory` → `openOutputFile()`.
* `moria_widget_harvest.inl` → `CreateDirectoryW(utf8PathToWide(...))`.

---

## Known Limitations

* **Porter Goat saddlebag access is blocked Tobi-side.** rc.71 diagnostic
  confirmed the goat's `MorInventoryComponent.Items` array contains only
  the empty slot wrapper `BP_ContainerItem_Goat_Slot_EpicPack_C` — the
  actual saddlebag item needs to be equipped pak-side. `ServerUse` fires
  correctly but there's nothing to open. Follow / Stay both function.
* **`LogMoriaNPC` log spam** when `ConsoleEnabled=1`. The Porter Goat
  has `NpcGuid=0` (not registered in `AMorNPCManager.NpcInfo`), so the
  game's manager logs `IsNpcRescued` / `IsNpcRecruit` / etc. errors.
  Production ships with `ConsoleEnabled=0` so this is invisible to end
  users; only diagnostic builds will see the spam.
* **Activity transitions are not event-driven.** Multiple sessions of
  PE post-hook attempts on `SetCurrentActivity` / `MorNpcUpdateActivity`
  confirmed activity is set via direct C++ property write, bypassing
  the BlueprintCallable UFunction. Polling remains the only reliable
  detector — hence the 0.2 Hz tick.

---

## Production / Install

* `s_verbose = false` is now the default in `moria_common.h` — earlier
  builds shipped with verbose on by accident, generating large log files.
* `ConsoleEnabled = 0` and `GuiConsoleEnabled = 0` in both staging INIs
  (client + dedicated server).
* DLL built in `Game__Shipping__Win64`; installer signed with SSL.com
  eSigner; 7.49 MB output.

## Install

Run **`KhazadDumAdvancedBuilderPack_v7.2.0-rc.1_Setup.exe`** — the installer
deploys UE4SS and all mod files to your game directory. Upgrading from any
v6.x or v7.0.x install is supported in place; the installer cleans up the
removed Mereak's Secrets files first.

---

## Working Agreement

This is a *release candidate* on the `npc-recovery` branch. `main` remains
at v7.0.2. After in-game testing the branch will be fast-forwarded to
`main` and re-tagged `v7.2.0`. If the recovery system causes regressions
in real play, the branch can be abandoned and `main` is unchanged.
