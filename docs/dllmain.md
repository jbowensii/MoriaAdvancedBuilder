# dllmain.cpp Reference

Detailed documentation for `src/dllmain.cpp` -- the main source file of MoriaCppMod (~2,244 lines).

## Enums

| Enum | Values | Purpose |
|------|--------|---------|
| `PlacePhase` | `Idle, CancelGhost, WaitingForShow, SelectRecipeWalk` | Quick-build reactive state machine phases (simplified from 7 phases in v2.x) |
| `SelectResult` | `Found, Loading, NotFound` | Return value from `selectRecipeOnBuildTab` |
| `HandleResolvePhase` | `None, Priming, Resolving, Done` | Eager handle resolution state machine |
| `UmgSlotState` | `Empty, Inactive, Active, Disabled` | Visual state for UMG toolbar slot icons |

## Member Variables

### HISM Removal & Bubble Tracking (lines 22-45)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_undoStack` | `vector<RemovedInstance>` | Stack of removed instances for undo |
| `m_savedRemovals` | `vector<SavedRemoval>` | Position-based removals loaded from disk (now includes bubbleId field) |
| `m_typeRemovals` | `set<string>` | Mesh IDs for remove-all-of-type rules |
| `m_processedComps` | `set<UObject*>` | HISM components already scanned |
| `m_appliedRemovals` | `vector<bool>` | Parallel to `m_savedRemovals`: true = already hidden |
| `m_replay` | `ReplayState` | Throttled replay queue (max 3 hides/frame), uses FWeakObjectPtr for comp queue |
| `m_saveFilePath` | `string` | Path to `removed_instances.txt` |
| `m_worldLayout` | `UObject*` | Cached AWorldLayout for bubble queries |
| `m_currentBubbleId` | `string` | ASCII ID of current bubble (e.g., "Great_Hall") |
| `m_currentBubbleName` | `wstring` | Display name of current bubble |

### Interval Timers (lines 41-45)

| Variable | Interval | Purpose |
|----------|----------|---------|
| `m_lastWorldCheck` | ~1s | Detect world unload (character disappears) |
| `m_lastCharPoll` | ~0.5s | Detect character load (`BP_FGKDwarf_C`) |
| `m_lastStreamCheck` | ~3s | Scan for newly-streamed HISM components |
| `m_lastRescanTime` | ~60s | Full rescan for pending removals |
| `m_lastBubbleCheck` | ~30s | Fallback bubble tracking poll |
| `m_charLoadTime` | -- | Timestamp when character first detected |

### Quick-Build State Machine (lines 250-266)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_recipeSlots[12]` | `RecipeSlot` | Per-slot recipe data (name, bLock, handle, rowName) |
| `m_pendingQuickBuildSlot` | `int` | F-key slot pending activation (-1 = none) |
| `m_qbPhase` | `PlacePhase` | Current quickbuild phase |
| `m_showSettleTime` | `ULONGLONG` | Timestamp of last OnAfterShow (350ms settle delay) |
| `m_lastHandleResolveSlotTime` | `ULONGLONG` | 200ms per-slot cooldown during HandleResolve |
| `m_qbStartTime` | `ULONGLONG` | When SM entered non-idle state |
| `m_lastShowHideTime` | `ULONGLONG` | Cooldown for Show()/Hide() calls (350ms) |
| `m_lastQBSelectTime` | `ULONGLONG` | Post-completion cooldown (500ms) |
| `m_isAutoSelecting` | `bool` | Suppresses post-hook capture during automated selection |

### Cached Widget Pointers (lines 269-271) -- FWeakObjectPtr

| Variable | Type | Purpose |
|----------|------|---------|
| `m_cachedBuildComp` | `FWeakObjectPtr` | `UMorBuildingComponent` on player character |
| `m_cachedBuildHUD` | `FWeakObjectPtr` | `UI_WBP_BuildHUDv2_C` (build overlay widget) |
| `m_cachedBuildTab` | `FWeakObjectPtr` | `UI_WBP_Build_Tab_C` (recipe grid) |

### UMG Toolbars (lines 293-362)

Three toolbars are created as UMG `UUserWidget` instances at character load:

- **Builders Bar** (`m_umgBarWidget`): 8 slots with state/icon image pairs, key labels
- **Mod Controller** (`m_mcBarWidget`): 9 slots (3x3 grid), rotation label overlay
- **Advanced Builder** (`m_abBarWidget`): Single toggle button, lower-right corner

Each toolbar tracks per-slot `UImage*` arrays for state frames and icon overlays, plus `UmgSlotState` arrays.

### Config Menu (lines 324-358)

`m_fontTestWidget` is a 4-tab UMG panel: Tab 0 (Game Options), Tab 1 (Key Mapping), Tab 2 (Hide Environment -- grouped by bubble), Tab 3 (Game Mods). Each tab has its own `UScrollBox`, content VBox, and header TextBlock/Image.

### Additional Widgets (lines 413-431)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_crosshairWidget` | `UObject*` | Centered reticle shown during inspect (T_UI_Bow_Reticle) |
| `m_crosshairShowTick` | `ULONGLONG` | Auto-hide after 40s |
| `m_errorBoxWidget` | `UObject*` | Always-visible error/info box (5s duration) |
| `m_lastItemInvComp` | `FWeakObjectPtr` | Inventory component for last picked-up item |

### Stability Audit (lines 433-436)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_auditSpawnedActors` | `vector<FWeakObjectPtr>` | Spawned PointLight actors for cleanup |

### Toggle States (lines 365-377)

| Variable | Default | Purpose |
|----------|---------|---------|
| `m_toolbarsVisible` | `false` | Builders bar + MC bar visibility |
| `m_characterHidden` | `false` | Player character hidden |
| `m_flyMode` | `false` | No-clip flight active |
| `m_snapEnabled` | `true` | Snap-to-grid active |
| `m_buildMenuPrimed` | `false` | Build menu widgets loaded at least once |
| `m_pitchRotateEnabled` | `true` | Pitch rotation via comma key |
| `m_rollRotateEnabled` | `true` | Roll rotation via period key |
| `m_trashItemEnabled` | `true` | DEL key trashes items |
| `m_replenishItemEnabled` | `true` | INS key replenishes items |
| `m_removeAttrsEnabled` | `true` | END key strips tint/effects |

## ProcessEvent Hooks

### on_unreal_init (line ~525)

Called once after UE4SS initializes. Performs:
1. Set `s_ue4ssWorkDir` from UE4SS working directory (required for `modPath()`)
2. Load config, localization strings (`Loc::load()` + `initDefaults()` fallbacks)
3. Load save file (`removed_instances.txt`) and quickbuild slots
4. Register F1-F8 keybinds (with SHIFT/CTRL/ALT modifier variants)
5. Register overlay toggle (Numpad `*`) and MC toolbar create (Numpad 7)
6. Set `s_instance = this` and install ProcessEvent callbacks
7. Register LoadMapPreCallback for definition processing

### Pre-Hook (line ~575)

Registered via `RegisterProcessEventPreCallback`. Intercepts:
- **RotatePressed / RotateCcwPressed** on BuildHUD: Applies custom rotation step via GATA, tracks cumulative rotation (0-359 degrees), updates overlay
- **BuildNewConstruction**: Calls `onBuildNewConstruction()` to inject pitch/roll into the FTransform quaternion, then auto-restores snap after piece placement

### Post-Hook (line ~630)

Registered via `RegisterProcessEventPostCallback`. Intercepts:
- **OnAfterShow** on `UI_WBP_Build_Tab_C`: Records `m_showSettleTime` for 350ms settle delay (C1 fix). Transitions `WaitingForShow` to recipe selection only after settle.
- **OnAfterHide** on BuildHUD or Build_Tab: During `CancelGhost` phase, activates build mode and transitions to `WaitingForShow`. Otherwise calls `onGhostDisappeared()`.
- **ExecuteUbergraph_WBP_FreeCamHUD**: Hides all mod toolbars on freecam enter.
- **OnCustomDisableCamera**: Restores mod toolbars on freecam exit.
- **OnPlayerEnteredBubble**: Extracts bubble UObject from params, calls `onBubbleEnteredEvent()` for bubble tracking.
- **ServerMoveItem / MoveSwapItem / BroadcastToContainers_OnChanged**: Captures last changed item for trash/replenish features.
- **blockSelectedEvent** on `UI_WBP_Build_Tab_C`: Captures recipe display name, bLock data (120 bytes), recipe handle (16 bytes) from manual recipe clicks. Skipped when `m_isAutoSelecting` is true. Also resolves `s_off_bLock` on first capture.

## on_update Flow (line ~846)

The per-frame tick runs the following sequence:

1. **Widget creation** -- Create MC bar, builders bar, AB bar, target info, and config widgets when character is loaded but widgets are null. Hide MC/builders bar if `m_toolbarsVisible` is false.
2. **Key label refresh** -- Consume `s_pendingKeyLabelRefresh` atomic flag from config thread.
3. **Auto-close timers** -- Target info popup (10s), crosshair reticle (40s), error box (5s), stability audit highlights (configurable).
4. **Config failsafe** -- Reset `m_ftVisible` if config widget was destroyed.
5. **Config key polling** -- Always-on F12 (or rebound key) toggles config menu.
6. **MC keybind polling** -- `GetAsyncKeyState` loop over 9 MC slots, dispatches `dispatchMcSlot()`.
7. **Repositioning mode** -- ESC exit, mouse drag with fraction-based coordinates.
8. **AB toggle polling** -- AB_OPEN key toggles toolbar visibility; MODIFIER+AB_OPEN toggles reposition mode.
9. **Alt-Tab recovery** -- Re-applies UI input mode if config is open after focus regain.
10. **Toolbar click/hover** -- Hit-test toolbars when cursor is visible, dispatch slot actions on LMB.
11. **Config UI interaction** -- Tab switching (1/2/3/4 keys or mouse), checkbox clicks, key capture for rebinding, removal entry deletion (bubble-grouped).
12. **Pending config actions** -- Consume `pendingToggleFreeBuild`, `pendingUnlockAllRecipes`, `pendingRemoveIndex`.
13. **Handle resolution SM** -- `Priming` (opens build menu via ConstructMode) then `Resolving` (one slot per frame via `trySelectRecipeByHandle`, 200ms per-slot cooldown).
14. **Build menu close detection** -- Clears cached FWeakObjectPtr widget pointers, calls `refreshActionBar()`.
15. **Deferred hide/refresh** -- Processes `m_deferHideAndRefresh` and `m_deferRemovalRebuild` (2-phase close/reopen for F12 panel updates).
16. **Placement tick** -- Drives quickbuild state machine (`placementTick()`).
17. **Pitch/roll tick** -- `tickPitchRoll()` updates GATA rotation (guarded by enabled flags).
18. **World-unload detection** -- Every 1s, checks if `BP_FGKDwarf_C` disappeared. Triggers world-reset block.
19. **Character polling** -- Every 0.5s when character not loaded.
20. **Initial HISM replay** -- 15s after character load, runs `migrateRemovalsToBubbles()` then `startReplay()`.
21. **Inventory audit** -- 20s after character load, one-shot `auditInventory()`.
22. **Replay processing** -- `processReplayBatch()` (max 3 hides/frame).
23. **Bubble tracking** -- Every 30s, `updateCurrentBubble()` poll. Clears `m_processedComps` on change.
24. **Stream check** -- Every 3s, scans for newly-streamed HISM components.
25. **Periodic rescan** -- Every 60s while pending removals exist.

## World-Reset Block (lines ~1989-2142)

Triggered when `BP_FGKDwarf_C` disappears during the 1s world-unload check. Resets all state for a clean new-world load:

| Category | What Gets Reset |
|----------|----------------|
| **Gameplay toggles** | `m_flyMode`, `m_snapEnabled`, `m_characterHidden`, `m_buildMenuPrimed`, `s_config.freeBuild` |
| **Quickbuild SM** | `m_qbPhase -> Idle`, all cooldown timestamps to 0, `m_pendingQuickBuildSlot = -1` |
| **Handle resolution** | `m_handleResolvePhase -> None`, slot index and timestamps reset |
| **Cached UObjects** | `m_cachedBuildComp/HUD/Tab` (FWeakObjectPtr reset), `m_bpShowMouseCursor`, `m_lastItemInvComp` all nulled |
| **Session-only slot data** | `hasBLockData = false`, `hasHandle = false` for all 12 recipe slots |
| **HISM state** | `m_processedComps` cleared, `m_undoStack` cleared, `m_appliedRemovals` all reset to false, `m_replay` stopped |
| **Bubble state** | `m_worldLayout` nulled, `m_currentBubbleId` cleared, `m_currentBubbleName` cleared, `m_lastBubbleCheck` reset |
| **Inventory/swap** | `m_bodyInvHandle`, `m_bodyInvHandles`, `m_bagHandle` cleared, `m_swap = {}` |
| **UMG widgets** | All widget pointers nulled (builders bar, MC bar, AB bar, config, target info, crosshair, error box, trash dialog, reposition) |
| **Overlay** | `s_overlay.visible = false`, `activeToolbar = 0` |
| **Cheat states** | `freeBuild = false`, pending toggles cleared |

## Key Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `QUICK_BUILD_SLOTS` | 12 | Total recipe slots (F1-F8 build + utility) |
| `OVERLAY_BUILD_SLOTS` | 8 | Build recipe slots shown in overlay |
| `BLOCK_DATA_SIZE` | 120 | Size of bLock struct in `blockSelectedEvent` params |
| `RECIPE_HANDLE_SIZE` | 16 | Size of `FMorConstructionRecipeRowHandle` |
| `MAX_HIDES_PER_FRAME` | 3 | Conservative limit for `UpdateInstanceTransform` per tick |
| `MC_SLOTS` | 9 | Mod Controller toolbar slot count (3x3 grid) |
| `TB_COUNT` | 4 | Toolbar count for repositioning (BB, AB, MC, InfoBox) |
| `CONFIG_TAB_COUNT` | 4 | Config panel tab count (Game Options, Key Mapping, Environment, Game Mods) |
| `CROSSHAIR_FADE_MS` | 40000 | Crosshair auto-hide timeout |
| `ERROR_BOX_DURATION_MS` | 5000 | Error/info box display duration |
