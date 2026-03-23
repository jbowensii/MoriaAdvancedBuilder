# moria_placement.inl -- Build Placement Lifecycle

Included inside the `MoriaCppMod` class body **before** `moria_quickbuild.inl`.
Owns the build lifecycle: menu open/close, recipe selection, ghost piece tracking,
snap toggle, rotation, pitch/roll building placement, and cleanup on ESC or recipe switch.

## Shared Widget Utilities

These helpers live here (not in dllmain.cpp) because both `moria_placement.inl`
and `moria_quickbuild.inl` need them, and include order requires they be defined
before the quickbuild file.

- **`isWidgetAlive(UObject*)`** -- static. Checks `RF_BeginDestroyed`,
  `RF_FinishDestroyed`, and `IsUnreachable()` before any `ProcessEvent` call.
- **`findWidgetByClass(className, requireVisible)`** -- scans all `UserWidget`
  instances via `FindAllOf`, matches by class name substring. Optional visibility
  filter via `IsVisible()`.
- **`getCachedBuildComp()`** -- finds and caches the single
  `UMorBuildingComponent` via `FindAllOf` into `FWeakObjectPtr`. Used by
  `getCachedBuildHUD()` and the DIRECT path in `startOrSwitchBuild`.

## Build Widget Discovery

- **`getCachedBuildHUD()`** -- primary path calls
  `GetActiveBuildingWidget()` on the cached `MorBuildingComponent` (no widget
  scan). Falls back to `findWidgetByClass("UI_WBP_BuildHUDv2_C")`. Caches
  result in `FWeakObjectPtr`.
- **`getCachedBuildTab()`** -- finds and caches `UI_WBP_Build_Tab_C` via
  `findWidgetByClass`. Uses `FWeakObjectPtr` cache.
- **`isPlacementActive()`** -- returns true when the BuildHUD `IsShowing()` AND
  `recipeSelectMode` is false (player has a ghost piece, not the recipe picker).
- **`isBuildTabShowing()`** -- cached `IsVisible()` on the `Build_Tab` widget.
  Uses UMG visibility, not FGK `IsShowing()`.

## Menu Management

- **`showBuildTab()`** -- calls FGK `Show()` on the cached Build_Tab. Rate-limited
  to 350ms via `m_lastShowHideTime` to prevent MovieScene re-entrancy crashes
  (`UUMGSequenceTickManager::TickWidgetAnimations` null UObject access).
- **`hideBuildTab()`** -- calls FGK `Hide()`. Shares the same 350ms cooldown.
- **`activateBuildMode()`** -- calls `ConstructMode()` on the player pawn to
  enter build mode (replaces B-key simulation from v2.x).

## CancelTargeting (v5.5.0)

Ghost cancellation uses `CancelTargeting()` via ProcessEvent on the GATA actor,
replacing the fragile `keybd_event(VK_ESCAPE)` approach from earlier versions.
Falls back to keybd_event if `CancelTargeting` function is not found on the GATA.

- **`cancelPlacementViaAPI()`** -- resolves GATA via `resolveGATA()`, calls
  `CancelTargeting` via `safeProcessEvent`. Returns true if cancel was issued.

## GATA (Ghost Actor / Target Actor)

- **`resolveGATA()`** -- reads `TargetActor` weak pointer from the BuildHUD via
  `GetValuePtrByPropertyNameInChain<FWeakObjectPtr>`.
- **`setGATARotation(gata, step)`** -- writes `SnapRotateIncrement` and
  `FreePlaceRotateIncrement` via runtime property discovery.

## Snap System

- **`toggleSnap()`** -- per-piece snap toggle. Saves original `MaxSnapDistance`
  on first use, then alternates between saved value and 0. Shows on-screen
  feedback.
- **`restoreSnap()`** -- auto-restores snap flag after piece placement (called
  from the ProcessEvent hook). Only resets the bool; the new GATA spawns with
  its default `MaxSnapDistance`.

## MC Slot State Management

- **`setMcSlotState(slot, state)`** -- updates an MC toolbar slot's visual
  state (Active/Inactive/Disabled/Empty) by swapping frame textures and
  adjusting icon opacity via `umgSetBrush` / `umgSetImageColor`. Includes a
  **no-op guard**: if the requested state matches the current state, the call
  returns immediately to avoid redundant Slate mutations that can crash the RHI
  thread during rapid recipe switching.

## Ghost Piece Event Handlers

- **`onGhostAppeared()`** -- called after recipe selection. Re-applies snap-off
  state and rotation step to the newly spawned GATA. Enables MC slot for snap
  button to Active/Inactive based on snap state.
- **`onGhostDisappeared()`** -- called on ESC cancel or build menu close. Greys
  out MC snap slot. **Suppressed** when the QB state machine is running
  (`m_qbPhase != Idle`) or within 500ms of last recipe selection
  (`m_lastQBSelectTime`), because the ghost briefly disappears between recipe
  swaps and rapid Disabled/Active Slate mutations crash the RHI thread.

## Pitch/Roll Building Placement (v5.2.0+)

Allows rotating build pieces in pitch and roll axes (not just yaw). Uses comma
and period keys (rebindable). Guarded by `m_pitchRotateEnabled` and
`m_rollRotateEnabled` checkboxes in F12 Game Options.

### tickPitchRoll()
Called every frame from `on_update()`. When `m_pitchRollActive` is true:
- Reads current rotation from GATA trace results (hardcoded offsets from CXX dump)
- Validates GATA offsets against reflection on first use
- Accumulates pitch/roll from keyboard input
- Writes modified rotation back to both TraceResults and LastTraceResults
- Deactivates when GATA becomes invalid or both features disabled

### onBuildNewConstruction(context, func, parms)
ProcessEvent pre-hook intercept on `BuildNewConstruction`. When pitch/roll is active:
- Locates the FTransform parameter within the function params
- Converts the accumulated (pitch, yaw, roll) to a quaternion
- Patches the FTransform's rotation quaternion in-place before the engine processes it
- This ensures the placed piece uses the modified rotation

**WARNING [C7]**: GATA trace result offsets are hardcoded from CXX dump. Will break
if game updates change the GATA class layout.

## Recipe Selection

### selectRecipeOnBuildTab(buildTab, slot) -- F-key path
Scans all visible `UI_WBP_Build_Item_Medium_C` widgets (H3 fix: targets specific
class, not all UserWidgets), matches by display name and icon texture (for
same-name disambiguation like "Column Crown" variants). Reads fresh `bLock` data
from the matched widget, fills `blockSelectedEvent` params via reflected offsets
(`resolveBSEOffsets`), and calls `safeProcessEvent`. Sets `m_isAutoSelecting = true`
(with RAII `AutoSelectGuard` -- H4 fix: RAII guard for DIRECT path too). After
success, caches the recipe handle, updates the builders bar, and calls
`onGhostAppeared()`.

### trySelectRecipeByHandle(buildHUD, handleData) -- DIRECT path
Calls `SelectRecipe` on the BuildHUD with a cached 16-byte recipe handle.
Bypasses the entire state machine when the build system is already active.
Rate-limited to 150ms via `m_lastDirectSelectTime`.

### selectRecipeByTargetName(buildTab) -- target-build path
Three-strategy matching against visible build-item widgets:
1. **bLock RowName CI** -- reads `Variants[0].ResultConstructionHandle.RowName`
   FName ComparisonIndex from each widget's bLock struct.
2. **Exact display name** match.
3. **Normalized fuzzy match** -- lowercase alphanumeric containment.

Uses the same `blockSelectedEvent` dispatch as the F-key path.

## Entry Points

- **`startOrSwitchBuild(slot)`** -- called from `quickBuildSlot()`. Tries the
  DIRECT path first (cached handle + active build system). On failure or when
  unavailable, starts the state machine: evaluates current game state and enters
  the appropriate phase (WaitingForShow / CancelGhost / SelectRecipeWalk).
- **`startBuildFromTarget()`** -- called from `quickBuildFromTarget()`. Sets
  `m_isTargetBuild = true`, then follows the same phase-selection logic.

## State Machine: placementTick()

Called every frame from `on_update()`. Drives the `PlacePhase` state machine:

| Phase | Behavior |
|-------|----------|
| **CancelGhost** | Calls `cancelPlacementViaAPI()` (CancelTargeting on GATA). On OnAfterHide, activates build mode and transitions to WaitingForShow. |
| **WaitingForShow** | Waits for `m_showSettleTime` + 350ms to elapse after OnAfterShow fires (C1 settle delay). Then transitions to SelectRecipeWalk. |
| **SelectRecipeWalk** | Dispatches to `selectRecipeOnBuildTab` (F-key) or `selectRecipeByTargetName` (target-build) based on `m_isTargetBuild`. |

Global safety timeout: 5s. On timeout, hides the build tab and resets to Idle.

## Removed Functions (v5.5.0 dead code cleanup)

The following functions were removed as dead code (~280 lines):
- `findBuildHUD()` -- replaced by `getCachedBuildHUD()` with FWeakObjectPtr
- `logPlacementRotation()` -- debug-only, unused
- `dumpObjectProperties()` -- debug-only, unused
- `dumpGATADeep()` -- debug-only, unused
