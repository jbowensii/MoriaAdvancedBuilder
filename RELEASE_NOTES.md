# Khazad-dum Advanced Builder Pack v2.5 — Release Notes

## Highlights

**Modular Architecture** — The monolithic 11,683-line `dllmain.cpp` has been decomposed into 12 source files across headers, inline class includes, and a separate overlay translation unit. The codebase is now 12,937 lines of mod source plus 1,672 lines of tests.

**In-Game Config Menu** — A full UMG configuration widget with three tabbed panels: Optional Mods (Free Build, No Collision toggles), Key Bindings (17 rebindable keys with scroll support), and Build From Target settings. Press F12 (default) to open.

**Runtime Reflection** — All hardcoded Blueprint class offsets replaced with runtime property resolution via Unreal's `ForEachProperty()` API, making the mod resilient to game updates that shift class layouts.

**199 Unit Tests** — Expanded test suite covering file I/O, key helpers, localization, memory utilities, and string helpers.

---

## What's New in v2.5 (from v2.2)

### Modular Decomposition
Monolithic `dllmain.cpp` split into 12 files:
- `moria_common.h` — Shared types, constants, forward declarations
- `moria_keybinds.h` — Keybind definitions and tables
- `moria_reflection.h` — Runtime property resolution utilities
- `moria_debug.inl` — Debug display and diagnostics
- `moria_hism.inl` — HISM instance removal/undo system
- `moria_inventory.inl` — Inventory and toolbar management
- `moria_overlay.cpp` — Win32 GDI+ overlay (separate translation unit)
- `moria_overlay_mgmt.inl` — Overlay slot management and config toggle
- `moria_quickbuild.inl` — Quick-build and target-build state machines
- `moria_widgets.inl` — UMG widget creation, Target Info, Config Menu

### In-Game Config Menu (UMG)
- Three tabbed panels: Optional Mods, Key Bindings, Build From Target
- Tab 0: Toggle Free Build and No Collision with full-row click areas
- Tab 1: 17 rebindable keys with scrollable list and yellow "[Press key...]" capture feedback
- Tab 2: Build From Target distance and settings
- DPI-aware hit-test coordinate system using game viewport scale
- Alt-Tab focus recovery — re-applies UI input mode when game regains foreground
- ESC or click-outside to close

### Code Review Fixes (10 items)
- `FWeakObjectPtr` for GC-safe UObject references
- GDI+ lifecycle management (proper init/shutdown)
- `ProcessEvent` buffer safety (zero-initialized parameter blocks)
- `unique_ptr` for icon extraction texture buffers
- RAII `CriticalSectionLock` for overlay thread synchronization
- `stopOverlay` race condition fix (join with timeout)
- Consistent error checking on property resolution failures

### Quick-Build Swap Delay
- New `WaitReopen` phase in both quick-build and target-build state machines
- Configurable frame delay (`m_quickBuildSwapDelay`, default 5 frames) between menu close and reopen
- Mitigates UE4.27 `FSlateCachedFastPathRenderingData` crash when rapidly switching build recipes

### Dead Code Cleanup
- Chat widget system (`findWidgets`, `showGameMessage`) commented out with BELIEVED DEAD CODE markers
- InfoBox popup system (4 functions, never displayed) commented out
- `toggleBuildHUD` diagnostic function commented out

### Test Suite Expansion
- 137 tests expanded to 199 tests (+45%)
- New test files: `test_key_helpers.cpp`, `test_memory.cpp`, `test_loc.cpp`, `test_file_io.cpp`

### Target Info & Placeholder Widgets
- Placeholder Info Box resized to match real Target Info widget (1100x320)
- DPI-aware toolbar repositioning with fractional viewport coordinates
- Widget rendering uses engine DPI (`RenderScale=1.0`, unscaled `DesiredSize`)

---

## What's New in v2.2 (from v2.1)

### Build Bar Scaling Fix
- Restored non-uniform Build Bar scaling from v1.7 (0.825x, 0.75y)
- Fixes squished appearance reported on 2560x1440 displays

---

## What's New in v2.1 (from v2.0)

### Build Menu API Upgrade
- Replaced `keybd_event(B)` hack with FGK `Show()`/`Hide()` API for build menu control
- `OnAfterShow` ProcessEvent hook as definitive "menu ready" signal
- Eliminated `PrimeClose` phase — direct transition from `PrimeOpen` to `SelectRecipe`

### Noclip Flight Fix
- Switched from `CapsuleComponent::SetCollisionEnabled` to `AActor::SetActorEnableCollision`
- Fixes camera dropping below character model during noclip flight

### Cleanup
- Removed `countVisibleBuildItems()` helper (replaced by OnAfterShow hook)
- Added fly mode localization string

---

## v2.0 (from v1.8)

### Runtime Reflection Conversion
Replaced all inline hardcoded offsets on Blueprint/UObject classes with runtime reflection:
- **4 reflected properties**: `TargetActor`, `selectedRecipe`, `selectedName`, `recipesDataTable`
- **LineTraceSingle parameters**: `LTOff` namespace (14 constexpr values) replaced with `LTResolved` struct
- **UpdateInstanceTransform parameters**: Hardcoded offsets replaced with `UITResolved` struct
- **GetInstanceTransform_Params**: Runtime validation against `ForEachProperty()` results
- Struct-internal offsets (FSlateBrush, FSlateFontInfo, FMorRecipeBlock, etc.) remain hardcoded — POD types verified stable against CXXHeaderDump

### Recipe Diagnostics
- `logBLockDiagnostics()` logs Tag, CategoryTag, SortOrder, Variants, and hex dumps at 4 instrumentation points to differentiate same-named recipes across categories

### Toolbar Swap
- `swapToolbarTick()` rewritten to use `GetItemForSlot` (replaces `GetItemForHotbarSlot`)
- Removed dead `clearHotbar` state machine guard

### Code Quality (~2,400 lines removed)
- 7 `#if 0` blocks removed: `startRotationSpy`, `testAllDisplayMethods`, `dumpAllWidgets`, `dumpBuildCraftClasses`, `probeInventoryHotbar`, `clearHotbar`/`clearHotbarTick`, `rotateBuildPlacement`
- Dead state variables removed
- Stale comments fixed (F12 references, deleted keybind notes)
- All 11 section headers audited for accuracy

---

## Build

- Clean build, zero warnings (MSVC v14.50, VS2026)
- Source: ~12,937 lines across 12 files (up from ~10,858 single-file at v2.0)
- Tests: 199 tests across 5 test files (1,672 lines)

## Installer

- Version 2.5
- Code-signed with SSL.com eSigner

---

## Upgrade Notes

- **No configuration changes required** — existing `MoriaCppMod.ini`, `quickbuild_slots.txt`, and `removed_instances.txt` files are fully compatible
- The mod logs `Resolved 'PropertyName' @ offset ...` messages on first use of each reflected property — this is normal startup behavior
- Config menu accessible via F12 (or your rebound key)

## Full Changelog

**v2.0 → v2.5**: 11,091 insertions, 9,502 deletions across 18 files
