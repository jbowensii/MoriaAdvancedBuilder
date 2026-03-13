# moria_placement.inl -- Build Placement Lifecycle

Included inside the `MoriaCppMod` class body **before** `moria_quickbuild.inl`.
Owns the build lifecycle: menu open/close, recipe selection, ghost piece tracking,
snap toggle, rotation, and cleanup on ESC or recipe switch.

## Shared Widget Utilities

These three helpers live here (not in dllmain.cpp) because both
`moria_placement.inl` and `moria_quickbuild.inl` need them, and include order
requires they be defined before the quickbuild file.

- **`isWidgetAlive(UObject*)`** -- static. Checks `RF_BeginDestroyed`,
  `RF_FinishDestroyed`, and `IsUnreachable()` before any `ProcessEvent` call.
- **`findWidgetByClass(className, requireVisible)`** -- scans all `UserWidget`
  instances via `FindAllOf`, matches by class name substring. Optional visibility
  filter via `IsVisible()`.
- **`getCachedBuildComp()`** -- finds and caches the single
  `UMorBuildingComponent` via `FindAllOf`. Used by `getCachedBuildHUD()` and
  the DIRECT path in `startOrSwitchBuild`.

## Build Widget Discovery

- **`findBuildHUD()`** -- full `FindAllOf` scan for any widget whose class name
  contains `"BuildHUD"`. Prefers visible instances; falls back to first match.
- **`getCachedBuildHUD()`** -- primary path calls
  `GetActiveBuildingWidget()` on the cached `MorBuildingComponent` (no widget
  scan). Falls back to `findWidgetByClass("UI_WBP_BuildHUDv2_C")`.
- **`isPlacementActive()`** -- returns true when the BuildHUD `IsShowing()` AND
  `recipeSelectMode` is false (player has a ghost piece, not the recipe picker).
- **`isBuildTabShowing()`** -- cached `IsVisible()` on the `Build_Tab` widget.
  Uses UMG visibility, not FGK `IsShowing()`.
- **`isBuildTabAnimating()`** -- calls `IsAnyAnimationPlaying()`. Retained for
  future use but NOT used in any guards; the 350ms Show/Hide cooldown is the
  proven crash prevention mechanism.

## Menu Management

- **`showBuildTab()`** -- calls FGK `Show()` on the cached Build_Tab. Rate-limited
  to 350ms via `m_lastShowHideTime` to prevent MovieScene re-entrancy crashes
  (`UUMGSequenceTickManager::TickWidgetAnimations` null UObject access).
- **`hideBuildTab()`** -- calls FGK `Hide()`. Shares the same 350ms cooldown.

## GATA (Ghost Actor / Target Actor)

- **`resolveGATA()`** -- reads `TargetActor` weak pointer from the BuildHUD.
- **`setGATARotation(gata, step)`** -- writes `SnapRotateIncrement` and
  `FreePlaceRotateIncrement` via runtime property discovery.

## Snap System

- **`toggleSnap()`** -- per-piece snap toggle. Saves original `MaxSnapDistance`
  on first use, then alternates between saved value and 0. Shows on-screen
  feedback.
- **`restoreSnap()`** -- auto-restores snap flag after piece placement (called
  from the ProcessEvent hook). Only resets the bool; the new GATA spawns with
  its default `MaxSnapDistance`.

## MC Slot 5 State Management

- **`setMcSlotState(slot, state)`** -- updates an MC toolbar slot's visual
  state (Active/Inactive/Disabled/Empty) by swapping frame textures and
  adjusting icon opacity via `umgSetBrush` / `umgSetImageColor`. Includes a
  **no-op guard**: if the requested state matches the current state, the call
  returns immediately to avoid redundant Slate mutations that can crash the RHI
  thread during rapid recipe switching.

## Ghost Piece Event Handlers

- **`onGhostAppeared()`** -- called after recipe selection. Re-applies snap-off
  state and rotation step to the newly spawned GATA. Enables MC slot 5 (snap
  button) to Active.
- **`onGhostDisappeared()`** -- called on ESC cancel or build menu close. Greys
  out MC slot 5. **Suppressed** when the QB state machine is running
  (`m_qbPhase != Idle`) or within 500ms of last recipe selection
  (`m_lastQBSelectTime`), because the ghost briefly disappears between recipe
  swaps and rapid Disabled/Active Slate mutations crash the RHI thread.

## Recipe Selection

### selectRecipeOnBuildTab(buildTab, slot) -- F-key path
Scans all visible `UI_WBP_Build_Item_Medium_C` widgets, matches by display name
and icon texture (for same-name disambiguation like "Column Crown" variants).
Reads fresh `bLock` data from the matched widget, fills `blockSelectedEvent`
params via reflected offsets (`resolveBSEOffsets`), and calls `ProcessEvent`.
Sets `m_isAutoSelecting = true` (with RAII `AutoSelectGuard`) to suppress the
post-hook capture. After success, caches the recipe handle, updates the builders
bar, and calls `onGhostAppeared()`.

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
  the appropriate phase (PrimeOpen / CancelGhost / SelectRecipe / OpenMenu).
- **`startBuildFromTarget()`** -- called from `quickBuildFromTarget()`. Sets
  `m_isTargetBuild = true`, then follows the same phase-selection logic.

## State Machine: placementTick()

Called every frame from `on_update()`. Drives the `PlacePhase` state machine:

| Phase | Behavior |
|-------|----------|
| **CancelGhost** | Waits for ESC to deactivate placement. If build tab still open, skips to SelectRecipe (avoids 300-468 widget churn per close/reopen). |
| **CloseMenu** | Waits for build tab to close, then enters WaitReopen. |
| **WaitReopen** | Waits N frames + 350ms cooldown, then sends B key to reopen. |
| **PrimeOpen** | First-ever quickbuild. Waits for `OnAfterShow` signal (or 2s fallback). Sets `m_buildMenuPrimed = true`. |
| **OpenMenu** | Waits for build tab to appear. Retries B key every 500ms. |
| **SelectRecipe** | Dispatches to `selectRecipeOnBuildTab` (F-key) or `selectRecipeByTargetName` (target-build) based on `m_isTargetBuild`. |

Global safety timeout: 2.5s when primed, 5s during first prime. On timeout,
hides the build tab and resets to Idle.
