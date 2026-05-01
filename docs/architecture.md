# MoriaCppMod Architecture

A UE4SS C++ mod for Return to Moria (Unreal Engine 4.27). Provides HISM instance hiding with bubble tracking, quick-build hotbar, dual-toolbar swap, rotation/pitch/roll control, UMG config menu, crosshair reticle, inventory management (trash/replenish/remove-attrs), definition processing, localization, and a Win32 GDI+ overlay bar.

**Version**: v5.5.0 | **UE4SS**: v4.0.0-rc1 (custom-built from source, commit 0bfec09e)

## Source Layout

The main class `MoriaCppMod` lives in `src/dllmain.cpp` (~3,200 lines). It inherits from `RC::CppUserModBase` (the UE4SS mod API). The class body is assembled via `#include` directives that pull `.inl` files directly into the class scope. **Include order matters** because later files call functions defined in earlier ones.

Total source: ~17,000 lines across dllmain.cpp + 14 .inl files + 4 headers.

### Headers (included at file top, before the class)

| File | Lines | Purpose |
|------|-------|---------|
| `moria_testable.h` | ~928 | Pure-logic functions (parsing, string helpers, localization) shared with test suite. No UE4SS dependency. |
| `moria_common.h` | ~328 | Types, macros, constants, `OverlayState`, `ConfigState`, RAII `CriticalSectionLock`, logging macros (`VLOG`, `QBLOG`), `safeProcessEvent` wrapper, `modPath()` helper. Includes `moria_testable.h`. |
| `moria_reflection.h` | ~672 | Property offset cache (`s_off_*` globals), `resolveOffset()` with sentinel values, accessor functions for struct-internal offsets. |
| `moria_keybinds.h` | ~125 | 24 rebindable `KeyBind` entries, modifier key logic, VK code conversion, `findGameWindow()`. |

### Inline Files (included inside the class body)

| Order | File | Lines | Purpose |
|-------|------|-------|---------|
| 1 | `moria_common.inl` | ~211 | `ScreenCoords` struct (viewport/cursor/DPI coordinate conversion), `safeProcessEvent` calls via `ScreenCoords::queryViewportScale`. Must be first. |
| 2 | `moria_datatable.inl` | ~372 | Runtime DataTable read/write/add via engine vtable dispatch. |
| 3 | `moria_DefinitionProcessing.inl` | -- | Definition file processing (loads .def/.ini mod packs at map load). |
| 4 | `moria_debug.inl` | ~328 | Display helpers, debug commands, cheat toggles, `showOnScreen()`, `dispatchMcSlot()`. |
| 5 | `moria_hism.inl` | ~1,124 | HISM removal: match, hide, undo, replay, type rules, bubble tracking, migration. |
| 6 | `moria_inventory.inl` | ~1,133 | Inventory inspection, toolbar swap state machine, container discovery, trash/replenish/remove-attrs. SEH wrapper via `safeProcessEvent`. |
| 7 | `moria_stability.inl` | ~426 | Stability audit (scan for unsupported pieces, PointLight highlights). |
| 8 | `moria_placement.inl` | ~1,217 | Build menu open/close, ghost detection, snap toggle, CancelTargeting, pitch/roll via BuildNewConstruction intercept. Defines `getCachedBuildComp`, `getCachedBuildHUD`, `getCachedBuildTab` (shared with quickbuild/widgets). |
| 9 | `moria_quickbuild.inl` | ~1,121 | Quick-build state machine dispatch, recipe selection, icon extraction pipeline, slot I/O. |
| 10 | `moria_widgets.inl` | ~4,483 | UMG widget creation (builders bar, MC toolbar, AB toggle, config menu, target info popup, crosshair reticle, error box, trash dialog). `deferRemoveWidget()` for safe widget destruction. |
| 11 | `moria_overlay_mgmt.inl` | ~596 | Overlay lifecycle, slot sync, overlay start/stop, input mode switching, toolbar repositioning. |
| 12 | `moria_widget_harvest.inl` | ~508 | Dev-only: walks UMG widget trees and dumps them to JSON for analysis. Triggered by N hotkey on supported screens. |
| 13 | `moria_session_history.inl` | ~1,030 | v6.7.0+: JSON-backed session-history storage (replaces broken native auto-history); CRUD with XOR+Base64 password obfuscation; injects rows via BP `AddSessionHistoryItem`; right-click delete with `WBP_UI_GenericPopup_C` confirmation. |
| 14 | `moria_join_world_ui.inl` | ~970 | v6.6.0+: in-place modification of native `WBP_UI_JoinWorldScreen_C` — tints TextBlocks, calls `injectSessionHistoryRows()`. Native flow handles all clicks/Esc/back-nav. |
| 15 | `moria_advanced_join_ui.inl` | ~245 | v6.6.0+: in-place modification of native `WBP_UI_AdvancedJoinOptions_C` — tints TextBlocks. |

**See [joinworld-ui-takeover.md](joinworld-ui-takeover.md)** for the in-place UI take-over methodology — the safe pattern for replacing/augmenting a game UMG screen without breaking native button wiring.

### Separate Compilation Unit

`moria_overlay.cpp` (~412 lines) -- Win32 overlay thread (GDI+ rendering). Compiled independently, linked into the DLL.

## Thread Model

| Thread | Responsibilities | Sync |
|--------|-----------------|------|
| **Game thread** | `on_update()` tick, ProcessEvent pre/post hooks, all UObject access | Single-threaded (UE4 game thread) |
| **Overlay thread** | GDI+ rendering at 5 Hz, transparent `WS_EX_LAYERED` window | `s_overlay.slotCS` (CriticalSection) protects slot reads; atomics for `visible`, `needsUpdate`, `rotationStep` |

Only the game thread may call `resolveOffset()` or access UObject pointers. The overlay thread reads `s_overlay.slots[]` under the CriticalSection lock.

## File I/O and Paths

All file I/O uses `modPath("Mods/...")` which prepends `s_ue4ssWorkDir` (set from `UE4SSProgram::get_program().get_working_directory()` in `on_unreal_init()`). This is required because the process CWD is `Win64/` but mod files live in `Win64/ue4ss/`.

## Property Resolution

All Blueprint property offsets are resolved at runtime via `resolveOffset()` (defined in `moria_reflection.h`). Uses `ForEachProperty` reflection to walk a UObject's class hierarchy.

**Sentinel values:** `-2` = not yet resolved, `-1` = property not found. Accessor functions (e.g., `brushImageSizeX()`) return the probed offset if available, falling back to hardcoded constants from `moria_common.h`.

## safeProcessEvent

All 242+ ProcessEvent calls are wrapped with `safeProcessEvent()` which provides:
- `isObjectAlive()` validation before calling ProcessEvent
- SEH (Structured Exception Handling) wrapper to catch access violations
- Consistent error logging on failure

## Key Design Patterns

- **RAII Guards**: `AutoSelectGuard` sets/clears `m_isAutoSelecting` to suppress post-hook recipe capture during automated quickbuild. `CriticalSectionLock` wraps Win32 CriticalSection enter/leave.
- **FWeakObjectPtr caches**: `m_cachedBuildComp`, `m_cachedBuildHUD`, `m_cachedBuildTab`, `m_lastItemInvComp`, `m_auditSpawnedActors` all use `FWeakObjectPtr` to guard against GC slab reuse.
- **Event-driven ghost detection**: `OnAfterShow` / `OnAfterHide` ProcessEvent post-hooks on `UI_WBP_Build_Tab_C` and `UI_WBP_BuildHUDv2_C` signal when the build ghost appears or vanishes, driving snap slot state.
- **Reactive state machine**: `PlacePhase` enum (`Idle -> CancelGhost -> WaitingForShow -> SelectRecipeWalk`) drives quick-build. The 350ms settle delay after OnAfterShow prevents MovieScene re-entrancy crashes.
- **Eager handle resolution**: `HandleResolvePhase` state machine runs once at toolbar creation to batch-resolve recipe FName handles for all saved slots (200ms per-slot cooldown), enabling the fast `SelectRecipe` API path.
- **Deferred widget removal**: `deferRemoveWidget()` hides immediately, removes next frame -- prevents Slate PaintFastPath crash from synchronous widget destruction.
- **Bubble tracking**: `OnPlayerEnteredBubble` delegate hook + 30s fallback poll via `GetBubbleAt`. Auto-tags removal entries with bubble IDs, filters replay to current bubble.
- **CancelTargeting**: Ghost cancellation via ProcessEvent on GATA (replaces `keybd_event(VK_ESCAPE)`).
- **World-reset block**: When `BP_FGKDwarf_C` disappears (world unload), all cached UObject pointers, state machine phases, widget references, and session-only data are nulled/reset.

## Subsection Map (within the class)

| Tag | Name | Key Functions |
|-----|------|---------------|
| 6A | File I/O | `loadSaveFile`, `rewriteSaveFile`, `loadQuickBuildSlots`, `saveConfig` |
| 6B | Player & World | `findPlayerController`, `getPawn`, `getPawnLocation` |
| 6C | Display/UI | `showOnScreen`, `showInfoMessage`, `showErrorBox`, debug overlays |
| 6D | HISM Removal | `removeAimed`, `undoLast`, `startReplay`, `processReplayBatch`, `updateCurrentBubble`, `migrateRemovalsToBubbles` |
| 6E | Inventory/Toolbar | `discoverBagHandle`, `swapToolbarTick`, toolbar stash/restore, `trashItem`, `replenishItem`, `removeItemAttributes` |
| 6F | Debug/Cheat | Free Build toggle, recipe unlock, fly mode |
| 6G | Quick-Build | `quickBuildSlot`, `assignRecipeSlot`, `placementTick` |
| 6H | Icon Extraction | Canvas render target pipeline, PNG export |
| 6I | UMG Widgets | `createExperimentalBar`, `createModControllerBar`, `createConfigWidget`, `createCrosshair`, `deferRemoveWidget` |
| 6J | Overlay Mgmt | `startOverlay`, `stopOverlay`, `updateOverlaySlots` |
| 6K | Public API | Constructor, destructor, `on_unreal_init`, `on_update` |
