# MoriaCppMod Architecture

A UE4SS C++ mod for Return to Moria (Unreal Engine 4.27). Provides the Advanced Builder subsystem (quick-build hotbar, rotation/pitch/roll control, HISM instance hiding with bubble tracking, build-menu integration), the Porter Goat companion (with Tobi's *Secrets of Khazad-dûm* pak), on-demand NPC unstuck, inventory tools (trash/replenish/remove-attrs), settings-screen keybind integration, session-history management for Join World, definition processing (Game Mods), and localization.

**Version**: v8.5.1 "Porter Goat" | **UE4SS**: v4.0.0-rc1 (custom-built from source, commit 0bfec09e)

> Doc status: refreshed 2026-07-13 for v8.5.1. For the goat subsystem read
> [goat-final-architecture.md](goat-final-architecture.md) — it is the
> definitive record including every dead end. Older subsystem docs (see
> [README.md](README.md)) describe the v5–v6 era; structure is still broadly
> right, details may lag.

## Current state highlights (v8.2.0 → v8.5.1)

- **Advanced Builder is a master toggle** (`=` key, `BIND_AB_TOGGLE`, slot 17; `m_advBuilderActive`). It ALWAYS starts OFF each session. While off: no building bar, F1–F8 quick build and MC keys inert, no handle-resolve priming, no rotation-circle display. F10 (Reposition HUD) auto-activates it.
- **NPC recovery is on-demand only** (`-` key, `BIND_UNSTUCK_NPCS`, slot 25 → `runUnstuckNpcsNow()`). All automatic scanning (day-cycle, character-load, recurring sweeps, 5s controller-cache refresh) is retired — it caused gameplay stutter. The teleport pipeline (destination resolution, 75cm offset, `K2_TeleportTo`) is unchanged and documented in memory `npc-recovery-architecture`.
- **Porter Goat is EPHEMERAL**: the bell (left-click while equipped, or hotbar use) toggles spawn/destroy of an unregistered `BP_NpcGoat_C`. The only persistent state is the saddlebag pack in the player's inventory (native character save). See [goat-final-architecture.md](goat-final-architecture.md).
- **Keybinds**: `BIND_COUNT = 26`, all remappable in Settings → Key Mapping (mod rows injected into the game's own screen — `moria_settings_ui.inl`). All hardcoded dev/diagnostic hotkeys are disabled.
- **Five bundled Game Mods definitions removed** from the product: Durable Armors/Weapons/Tools, Fat Trade, No Fall Damage.
- The legacy GDI+ overlay and the old MC/AB/builders toolbars are gone; the only toolbar is the **New Building Bar** (NBB), painted into the game's own `WBP_UI_ActionBar`.

## Source Layout

The main class `MoriaCppMod` lives in `src/dllmain.cpp`. It inherits from `RC::CppUserModBase` (the UE4SS mod API). The class body is assembled via `#include` directives that pull `.inl` files directly into the class scope. **Include order matters** because later files call functions defined in earlier ones.

### Headers (included at file top, before the class)

| File | Purpose |
|------|---------|
| `moria_testable.h` | Pure-logic functions (parsing, string helpers, localization, `bindIndexToIniKey`) shared with the test suite. No UE4SS dependency. `BIND_COUNT` lives here. |
| `moria_common.h` | Types, macros, constants, logging macros (`VLOG`, `QBLOG`), `safeProcessEvent` wrapper, `modPath()` helper. |
| `moria_reflection.h` | Property offset cache (`s_off_*` globals), `resolveOffset()` with sentinel values. |
| `moria_keybinds.h` | 26 rebindable `KeyBind` entries (APPEND-ONLY — index order is the ini contract), modifier key logic, `findGameWindow()`. |
| `moria_dualsense.h`, `moria_join_assets.h` | Controller support tables; Join World asset path constants. |

### Inline Files (included inside the class body)

| File | Purpose |
|------|---------|
| `moria_common.inl` | `ScreenCoords` (viewport/cursor/DPI conversion). Must be first. |
| `moria_datatable.inl` | Runtime DataTable read/write/add via engine vtable dispatch. |
| `moria_DefinitionProcessing.inl` | Game Mods system (.ini + .def packs applied at map load). |
| `moria_debug.inl` | Display helpers, cheat toggles, `showOnScreen()`, `dispatchMcSlot()`. |
| `moria_hism.inl` | HISM removal: match/hide/undo/replay/type rules/bubble tracking. Contains `seh_getFnInChain` (SEH-wrapped function lookup for world transitions). |
| `moria_inventory.inl` | Inventory inspection, container discovery, trash/replenish/remove-attrs. |
| `moria_stability.inl` | Stability audit (unsupported pieces, PointLight highlights). |
| `moria_placement.inl` | Build menu open/close, ghost detection, snap, pitch/roll. |
| `moria_quickbuild.inl` | Quick-build state machine, recipe selection, icon extraction, `saveConfig`/`loadConfig` (the ini). |
| `moria_widgets.inl` | UMG widget creation (NBB, config menu, target info, rotation display, error box, trash dialog), `deferRemoveWidget()`. |
| `moria_overlay_mgmt.inl` | Legacy overlay lifecycle (deprecated), input-mode switching. |
| `moria_widget_harvest.inl` | Dev-only widget-tree JSON dumps. |
| `moria_session_history.inl` | JSON-backed session-history storage + Join World row injection. |
| `moria_join_world_ui.inl` / `moria_advanced_join_ui.inl` | In-place modification of the native Join World / Advanced Join screens. |
| `moria_settings_ui.inl` | Game Settings screen take-over: injects mod keybind rows into Key Mapping, Gameplay-tab options, Cheats/Tweaks. |
| `moria_npc_recovery.inl` | CantReach NPC teleport pipeline — now fired ONLY by the Unstuck NPCs keybind. |
| `moria_goat.inl` | Porter Goat companion (bell, follow drive, saddlebag UI). See goat-final-architecture.md. |
| `moria_goat_save_probes.inl` | Dormant goat-save research probes (ini-gated, historical). |
| `moria_unlock.inl` | Recipe discovery/unlock draining. |

**See [joinworld-ui-takeover.md](joinworld-ui-takeover.md)** for the in-place UI take-over methodology.

## Thread Model

All mod logic runs on the **game thread** (`on_update` tick + ProcessEvent pre/post hooks). Only the game thread may access UObject pointers. The legacy GDI+ overlay thread still exists but the overlay is deprecated.

## File I/O and Paths

All file I/O uses `modPath("Mods/...")` which prepends the UE4SS working directory — the process CWD is `Win64/` but mod files live in `Win64/ue4ss/`. Config: `Mods/MoriaCppMod/MoriaCppMod.ini` (keybinds, preferences, cheats, tweaks, positions). Never round-trip this ini through tools without explicit UTF-8 — it holds non-ASCII (goat name).

## Property Resolution

All Blueprint property offsets are resolved at runtime via reflection (`resolveOffset()` / `GetValuePtrByPropertyNameInChain`). No hardcoded BP offsets. Sentinels: `-2` = not yet resolved, `-1` = not found.

## Stability rules that were paid for in crashes

- `safeProcessEvent()` wraps every ProcessEvent: liveness check + SEH.
- `isObjectAlive()` is NOT sufficient during world transitions — memory can be reused by another live object. SEH-wrap the *chain walk* itself (`seh_getFnInChain`) and/or probe with `safeClassName().empty()`.
- Destroyed actors linger until GC and pass `isObjectAlive` — filter re-finds with `KismetSystemLibrary::IsValid` (pending-kill aware). `AActor::IsActorBeingDestroyed` is NOT a UFUNCTION in 4.27.
- Never `RemoveFromParent` a game **singleton screen** (e.g. `WBP_UI_Inventory_Screen_StorageMode`) — `Show()` cannot re-add it and the feature dies for the session. `SetVisibility(Collapsed)` only.
- Input mode is owned by the game's UI manager: a mod-owned screen must re-assert `SetInputMode_UIOnlyEx` periodically (10 Hz, skip while LMB held) or the manager reverts to GameOnly.
- Widgets die with the world: every once-per-load widget spawner flag must be reset in the world-unload block or the widget is stale for the rest of the session.
- FString UFUNCTION params: heap buffer + `{ptr,len,len}` triple (see `setRoleFuzzyOnGoat`).
- Max 3 `UpdateInstanceTransform` calls per frame (render-thread crash).
- PE polling ≤10/sec; never poll when a hook exists; filter PE hooks by cheap `wcscmp` on the function name before any expensive class-name lookup.

## Key Design Patterns

- **Reactive state machine**: `PlacePhase` drives quick-build (350ms settle after OnAfterShow prevents MovieScene re-entrancy crashes).
- **Eager handle resolution**: `HandleResolvePhase` batch-resolves recipe handles — now gated on the Advanced Builder toggle.
- **Deferred widget removal**: `deferRemoveWidget()` hides now, removes next frame (Slate PaintFastPath crash otherwise).
- **Bubble tracking**: delegate hook + fallback poll; replay filtered to current bubble.
- **World-reset block**: on world unload every cached UObject pointer, state machine phase, widget reference, and once-per-load spawner flag is reset.
- **In-place UI takeover**: modify native screens instead of replacing them (join world, settings); for parallel mod-owned screens, expect to fight the UI manager for input.
