# dllmain.cpp Reference

Detailed documentation for `src/dllmain.cpp` -- the main source file of MoriaCppMod (~1,977 lines).

## Enums

| Enum | Values | Purpose |
|------|--------|---------|
| `PlacePhase` | `Idle, CancelGhost, CloseMenu, WaitReopen, PrimeOpen, OpenMenu, SelectRecipe` | Quick-build reactive state machine phases |
| `SelectResult` | `Found, Loading, NotFound` | Return value from `selectRecipeOnBuildTab` |
| `HandleResolvePhase` | `None, Priming, Resolving, Done` | Eager handle resolution state machine |
| `UmgSlotState` | `Empty, Inactive, Active, Disabled` | Visual state for UMG toolbar slot icons |

## Member Variables

### HISM Removal (lines 44-77)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_undoStack` | `vector<RemovedInstance>` | Stack of removed instances for undo |
| `m_savedRemovals` | `vector<SavedRemoval>` | Position-based removals loaded from disk |
| `m_typeRemovals` | `set<string>` | Mesh IDs for remove-all-of-type rules |
| `m_processedComps` | `set<UObject*>` | HISM components already scanned |
| `m_appliedRemovals` | `vector<bool>` | Parallel to `m_savedRemovals`: true = already hidden |
| `m_replay` | `ReplayState` | Throttled replay queue (max 3 hides/frame) |
| `m_saveFilePath` | `string` | Path to `removed_instances.txt` |

### Interval Timers (lines 58-64)

| Variable | Interval | Purpose |
|----------|----------|---------|
| `m_lastWorldCheck` | ~1s | Detect world unload (character disappears) |
| `m_lastCharPoll` | ~0.5s | Detect character load (`BP_FGKDwarf_C`) |
| `m_lastStreamCheck` | ~3s | Scan for newly-streamed HISM components |
| `m_lastRescanTime` | ~60s | Full rescan for pending removals |
| `m_lastContainerScan` | ~2s | Container discovery retry for toolbar swap |
| `m_charLoadTime` | -- | Timestamp when character first detected |

### Quick-Build State Machine (lines 314-333)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_recipeSlots[12]` | `RecipeSlot` | Per-slot recipe data (name, bLock, handle, rowName) |
| `m_pendingQuickBuildSlot` | `int` | F-key slot pending activation (-1 = none) |
| `m_qbPhase` | `PlacePhase` | Current quickbuild phase |
| `m_qbStartTime` | `ULONGLONG` | When SM entered non-idle state |
| `m_qbRetryTime` | `ULONGLONG` | Last retry/phase-change timestamp |
| `m_lastShowHideTime` | `ULONGLONG` | Cooldown for Show()/Hide() calls (350ms) |
| `m_lastQBSelectTime` | `ULONGLONG` | Post-completion cooldown (500ms) |
| `m_isAutoSelecting` | `bool` | Suppresses post-hook capture during automated selection |

### Cached Widget Pointers (lines 336-339)

| Variable | Type | Purpose |
|----------|------|---------|
| `m_cachedBuildComp` | `UObject*` | `UMorBuildingComponent` on player character |
| `m_cachedBuildHUD` | `UObject*` | `UI_WBP_BuildHUDv2_C` (build overlay widget) |
| `m_cachedBuildTab` | `UObject*` | `UI_WBP_Build_Tab_C` (recipe grid) |
| `m_fnIsVisible` | `UFunction*` | Cached `IsVisible()` on Build_Tab |

### UMG Toolbars (lines 355-432)

Three toolbars are created as UMG `UUserWidget` instances at character load:

- **Builders Bar** (`m_umgBarWidget`): 8 slots with state/icon image pairs, key labels
- **Mod Controller** (`m_mcBarWidget`): 12 slots (4x3 grid), rotation label overlay
- **Advanced Builder** (`m_abBarWidget`): Single toggle button, lower-right corner

Each toolbar tracks per-slot `UImage*` arrays for state frames and icon overlays, plus `UmgSlotState` arrays.

### Config Menu (lines 441-466)

`m_configWidget` is a 3-tab UMG panel: Tab 0 (Optional Mods), Tab 1 (Key Mapping), Tab 2 (Hide Environment). Each tab has its own `UScrollBox`, content VBox, and header TextBlock/Image.

### Toggle States (lines 389-396)

| Variable | Default | Purpose |
|----------|---------|---------|
| `m_toolbarsVisible` | `false` | Builders bar + MC bar visibility |
| `m_characterHidden` | `false` | Player character hidden |
| `m_flyMode` | `false` | No-clip flight active |
| `m_snapEnabled` | `true` | Snap-to-grid active |
| `m_buildMenuPrimed` | `false` | Build menu widgets loaded at least once |

## ProcessEvent Hooks

### on_unreal_init (line 527)

Called once after UE4SS initializes. Performs:
1. Load config and localization strings
2. Load save file (`removed_instances.txt`) and quickbuild slots
3. Register F1-F8 keybinds (with SHIFT/CTRL/ALT modifier variants)
4. Register overlay toggle (Numpad `*`) and MC toolbar create (Numpad 7)
5. Set `s_instance = this` and install ProcessEvent callbacks

### Pre-Hook (line 635)

Registered via `RegisterProcessEventPreCallback`. Intercepts:
- **RotatePressed / RotateCcwPressed** on BuildHUD: Applies custom rotation step via GATA, tracks cumulative rotation (0-359 degrees), updates overlay
- **BuildConstruction / TryBuild**: Auto-restores snap after piece placement when snap was disabled

### Post-Hook (line 685)

Registered via `RegisterProcessEventPostCallback`. Intercepts:
- **OnAfterShow** on `UI_WBP_Build_Tab_C`: Sets `m_buildTabAfterShowFired = true` (signals menu readiness)
- **OnAfterHide** on BuildHUD or Build_Tab: Calls `onGhostDisappeared()` to grey out snap slot
- **blockSelectedEvent** on `UI_WBP_Build_Tab_C`: Captures recipe display name, bLock data (120 bytes), recipe handle (16 bytes) from manual recipe clicks. Skipped when `m_isAutoSelecting` is true. Also resolves `s_off_bLock` on first capture.

## on_update Flow (line 810)

The per-frame tick runs the following sequence:

1. **Widget creation** -- Create MC bar, builders bar, AB bar, target info, and config widgets when character is loaded but widgets are null. Hide MC/builders bar if `m_toolbarsVisible` is false.
2. **Key label refresh** -- Consume `s_pendingKeyLabelRefresh` atomic flag from config thread.
3. **Auto-close timers** -- Target info popup (10s), stability audit highlights (configurable).
4. **Config failsafe** -- Reset `m_cfgVisible` if config widget was destroyed.
5. **Config key polling** -- Always-on F12 (or rebound key) toggles config menu.
6. **MC keybind polling** -- `GetAsyncKeyState` loop over 12 MC slots, dispatches `dispatchMcSlot()`.
7. **Repositioning mode** -- ESC exit, mouse drag with fraction-based coordinates.
8. **AB toggle polling** -- AB_OPEN key toggles toolbar visibility; MODIFIER+AB_OPEN toggles reposition mode.
9. **Alt-Tab recovery** -- Re-applies UI input mode if config is open after focus regain.
10. **Toolbar click/hover** -- Hit-test toolbars when cursor is visible, dispatch slot actions on LMB.
11. **Config UI interaction** -- Tab switching (1/2/3 keys or mouse), checkbox clicks, key capture for rebinding, removal entry deletion.
12. **Pending config actions** -- Consume `pendingToggleFreeBuild`, `pendingUnlockAllRecipes`, `pendingRemoveIndex`.
13. **Handle resolution SM** -- `Priming` (opens build menu via B key) then `Resolving` (one slot per frame via `trySelectRecipeByHandle`).
14. **Build menu close detection** -- Clears cached widget pointers, calls `refreshActionBar()`.
15. **Placement tick** -- Drives quickbuild + target-build state machines (`placementTick()`).
16. **Swap tick** -- Drives toolbar swap state machine (`swapToolbarTick()`).
17. **World-unload detection** -- Every 1s, checks if `BP_FGKDwarf_C` disappeared. Triggers world-reset block.
18. **Character polling** -- Every 0.5s when character not loaded.
19. **Container scan** -- Retry every 2s (5-65s after char load), discovers inventory handles.
20. **Initial HISM replay** -- 15s after character load, starts throttled replay.
21. **Replay processing** -- `processReplayBatch()` (max 3 hides/frame).
22. **Stream check** -- Every 3s, scans for newly-streamed HISM components.
23. **Periodic rescan** -- Every 60s while pending removals exist.

## World-Reset Block (lines 1696-1846)

Triggered when `BP_FGKDwarf_C` disappears during the 1s world-unload check. Resets all state for a clean new-world load:

| Category | What Gets Reset |
|----------|----------------|
| **Gameplay toggles** | `m_flyMode`, `m_snapEnabled`, `m_characterHidden`, `m_buildMenuPrimed`, `s_config.freeBuild` |
| **Quickbuild SM** | `m_qbPhase -> Idle`, all cooldown timestamps to 0, `m_pendingQuickBuildSlot = -1` |
| **Handle resolution** | `m_handleResolvePhase -> None`, slot index and timestamps reset |
| **Cached UObjects** | `m_cachedBuildComp/HUD/Tab`, `m_fnIsVisible`, `m_bpShowMouseCursor` all nulled |
| **Session-only slot data** | `hasBLockData = false`, `hasHandle = false` for all 12 recipe slots |
| **HISM state** | `m_processedComps` cleared, `m_undoStack` cleared, `m_appliedRemovals` all reset to false, `m_replay` stopped |
| **Inventory/swap** | `m_bodyInvHandle`, `m_bodyInvHandles`, `m_bagHandle` cleared, `m_swap = {}` |
| **UMG widgets** | All widget pointers nulled (builders bar, MC bar, AB bar, config, target info, reposition) |
| **Overlay** | `s_overlay.visible = false`, `activeToolbar = 0` |
| **Cheat states** | `freeBuild = false`, pending toggles cleared |

## Key Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `QUICK_BUILD_SLOTS` | 12 | Total recipe slots (F1-F8 build + F9-F12 utility) |
| `BLOCK_DATA_SIZE` | 120 | Size of bLock struct in `blockSelectedEvent` params |
| `RECIPE_HANDLE_SIZE` | 16 | Size of `FMorConstructionRecipeRowHandle` |
| `MAX_HIDES_PER_FRAME` | 3 | Conservative limit for `UpdateInstanceTransform` per tick |
| `MC_SLOTS` | 12 | Mod Controller toolbar slot count |
| `TB_COUNT` | 4 | Toolbar count for repositioning (BB, AB, MC, InfoBox) |
