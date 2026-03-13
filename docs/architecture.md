# MoriaCppMod Architecture

A UE4SS C++ mod for Return to Moria (Unreal Engine 4.27). Provides HISM instance hiding, quick-build hotbar, dual-toolbar swap, rotation control, UMG config menu, and a Win32 GDI+ overlay bar.

## Source Layout

The main class `MoriaCppMod` lives in `src/dllmain.cpp` (~1,977 lines). It inherits from `RC::CppUserModBase` (the UE4SS mod API). The class body is assembled via `#include` directives that pull `.inl` files directly into the class scope. **Include order matters** because later files call functions defined in earlier ones.

### Headers (included at file top, before the class)

| File | Purpose |
|------|---------|
| `moria_testable.h` | Pure-logic functions (parsing, string helpers, localization) shared with test suite. No UE4SS dependency. |
| `moria_common.h` | Types, macros, constants, `OverlayState`, `ConfigState`, RAII `CriticalSectionLock`, logging macros (`VLOG`, `QBLOG`). Includes `moria_testable.h`. |
| `moria_reflection.h` | Property offset cache (`s_off_*` globals), `resolveOffset()` with sentinel values, accessor functions for struct-internal offsets. |
| `moria_keybinds.h` | 22 rebindable `KeyBind` entries, modifier key logic, VK code conversion, `findGameWindow()`. |

### Inline Files (included inside the class body)

Included between member variable declarations (lines 44-483) and the public interface (line 491):

| Order | File | Subsection | Purpose |
|-------|------|------------|---------|
| 1 | `moria_common.inl` | -- | `ScreenCoords` struct (viewport/cursor/DPI coordinate conversion). Must be first. |
| 2 | `moria_debug.inl` | 6C + 6F | Display helpers, debug commands, cheat toggles, `showOnScreen()`. |
| 3 | `moria_hism.inl` | 6D | HISM removal: match, hide, undo, replay, type rules. |
| 4 | `moria_inventory.inl` | 6E | Inventory inspection, toolbar swap state machine, container discovery. |
| 5 | `moria_stability.inl` | -- | Stability audit (scan for unsupported pieces, PointLight highlights). |
| 6 | `moria_placement.inl` | -- | Build menu open/close, ghost detection, snap toggle. Defines `isWidgetAlive`, `findWidgetByClass`, `getCachedBuildComp` (shared with quickbuild/widgets). |
| 7 | `moria_quickbuild.inl` | 6G + 6H | Quick-build state machine dispatch, recipe selection, icon extraction pipeline. |
| 8 | `moria_widgets.inl` | 6I | UMG widget creation (builders bar, MC toolbar, AB toggle, config menu, target info popup). |
| 9 | `moria_overlay_mgmt.inl` | 6J | Overlay lifecycle, slot sync, overlay start/stop. |

### Separate Compilation Unit

`moria_overlay.cpp` -- Win32 overlay thread (GDI+ rendering). Compiled independently, linked into the DLL.

## Thread Model

| Thread | Responsibilities | Sync |
|--------|-----------------|------|
| **Game thread** | `on_update()` tick, ProcessEvent pre/post hooks, all UObject access | Single-threaded (UE4 game thread) |
| **Overlay thread** | GDI+ rendering at 5 Hz, transparent `WS_EX_LAYERED` window | `s_overlay.slotCS` (CriticalSection) protects slot reads; atomics for `visible`, `needsUpdate`, `rotationStep` |

Only the game thread may call `resolveOffset()` or access UObject pointers. The overlay thread reads `s_overlay.slots[]` under the CriticalSection lock.

## Property Resolution

All Blueprint property offsets are resolved at runtime via `resolveOffset()` (defined in `moria_reflection.h`). Uses `ForEachProperty` reflection to walk a UObject's class hierarchy.

**Sentinel values:** `-2` = not yet resolved, `-1` = property not found. Accessor functions (e.g., `brushImageSizeX()`) return the probed offset if available, falling back to hardcoded constants from `moria_common.h`.

## Key Design Patterns

- **RAII Guards**: `AutoSelectGuard` sets/clears `m_isAutoSelecting` to suppress post-hook recipe capture during automated quickbuild. `CriticalSectionLock` wraps Win32 CriticalSection enter/leave.
- **Event-driven ghost detection**: `OnAfterShow` / `OnAfterHide` ProcessEvent post-hooks on `UI_WBP_Build_Tab_C` and `UI_WBP_BuildHUDv2_C` signal when the build ghost appears or vanishes, driving snap slot state.
- **Reactive state machine**: `PlacePhase` enum (`Idle -> CancelGhost -> CloseMenu -> WaitReopen -> PrimeOpen -> OpenMenu -> SelectRecipe`) drives quick-build without frame counting. Timestamps + cooldowns replace brittle frame delays.
- **Eager handle resolution**: `HandleResolvePhase` state machine runs once at toolbar creation to batch-resolve recipe FName handles for all saved slots, enabling the fast `SelectRecipe` API path.
- **World-reset block**: When `BP_FGKDwarf_C` disappears (world unload), all cached UObject pointers, state machine phases, widget references, and session-only data are nulled/reset (lines 1696-1846).

## Subsection Map (within the class)

| Tag | Name | Key Functions |
|-----|------|---------------|
| 6A | File I/O | `loadSaveFile`, `rewriteSaveFile`, `loadQuickBuildSlots`, `saveConfig` |
| 6B | Player & World | `findPlayerController`, `getPawn`, `getPawnLocation` |
| 6C | Display/UI | `showOnScreen`, debug overlays |
| 6D | HISM Removal | `removeAimed`, `undoLast`, `startReplay`, `processReplayBatch` |
| 6E | Inventory/Toolbar | `discoverBagHandle`, `swapToolbarTick`, toolbar stash/restore |
| 6F | Debug/Cheat | Free Build toggle, recipe unlock, fly mode |
| 6G | Quick-Build | `quickBuildSlot`, `assignRecipeSlot`, `placementTick` |
| 6H | Icon Extraction | Canvas render target pipeline, PNG export |
| 6I | UMG Widgets | `createExperimentalBar`, `createModControllerBar`, `createConfigWidget` |
| 6J | Overlay Mgmt | `startOverlay`, `stopOverlay`, `updateOverlaySlots` |
| 6K | Public API | Constructor, destructor, `on_unreal_init`, `on_update` |
