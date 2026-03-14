# MoriaCppMod Developer Guide

Version 4.1.0 — Khazad-Dum Advanced Builder Pack

A UE4SS C++ mod for Lord of the Rings: Return to Moria (Unreal Engine 4.27). This mod adds advanced building tools, inventory management, environment customization, and a data-driven definition system for community modding.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [File Map](#file-map)
3. [Build System](#build-system)
4. [Mod Lifecycle](#mod-lifecycle)
5. [Module Reference](#module-reference)
   - [dllmain.cpp — Main Entry Point](#dllmaincpp--main-entry-point)
   - [moria_common.h — Shared Types and Constants](#moria_commonh--shared-types-and-constants)
   - [moria_reflection.h — Property Resolution and Caching](#moria_reflectionh--property-resolution-and-caching)
   - [moria_keybinds.h — Key Configuration](#moria_keybindsh--key-configuration)
   - [moria_testable.h — Platform-Independent Utilities](#moria_testableh--platform-independent-utilities)
   - [moria_common.inl — Screen Coordinates and Widget Helpers](#moria_commoninl--screen-coordinates-and-widget-helpers)
   - [moria_datatable.inl — DataTable CRUD Operations](#moria_datatableinl--datatable-crud-operations)
   - [moria_DefinitionProcessing.inl — Game Mods System](#moria_definitionprocessinginl--game-mods-system)
   - [moria_inventory.inl — Inventory Management](#moria_inventoryinl--inventory-management)
   - [moria_quickbuild.inl — Quick Build State Machine](#moria_quickbuildinl--quick-build-state-machine)
   - [moria_placement.inl — Placement and Ghost Actor Management](#moria_placementinl--placement-and-ghost-actor-management)
   - [moria_hism.inl — HISM Removal System](#moria_hisminl--hism-removal-system)
   - [moria_widgets.inl — UMG Widget System](#moria_widgetsinl--umg-widget-system)
   - [moria_overlay_mgmt.inl — Overlay Management](#moria_overlay_mgmtinl--overlay-management)
   - [moria_overlay.cpp — Win32 Overlay Renderer](#moria_overlaycpp--win32-overlay-renderer)
   - [moria_debug.inl — Debug and Diagnostics](#moria_debuginl--debug-and-diagnostics)
   - [moria_stability.inl — Stability Audit System](#moria_stabilityinl--stability-audit-system)
6. [Key Workflows](#key-workflows)
   - [Quick Build Workflow](#quick-build-workflow)
   - [Definition Processing Workflow](#definition-processing-workflow)
   - [HISM Removal Workflow](#hism-removal-workflow)
   - [Widget Creation Workflow](#widget-creation-workflow)
   - [Inventory Feature Workflow](#inventory-feature-workflow)
7. [Reflection and Property Resolution](#reflection-and-property-resolution)
8. [Thread Safety and Synchronization](#thread-safety-and-synchronization)
9. [ProcessEvent Hooks](#processevent-hooks)
10. [Configuration System](#configuration-system)
11. [Localization System](#localization-system)
12. [Testing](#testing)
13. [Deployment](#deployment)
14. [Known Constraints and Design Decisions](#known-constraints-and-design-decisions)
15. [Code Review Findings (v4.0.0)](#code-review-findings-v400)

---

## Architecture Overview

MoriaCppMod is a single-class UE4SS C++ mod (`MoriaCppMod : RC::CppUserModBase`) that injects into Return to Moria at runtime. It uses UE4SS's reflection APIs to discover game objects, hook functions, read and write properties, and create UMG widgets — all without any compile-time game SDK dependency.

The mod is structured as one main class defined in `dllmain.cpp`, with implementation split across multiple `.inl` files that are `#include`-d directly inside the class body. This gives all inlined functions direct access to private class members while keeping the codebase organized by feature domain.

### High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    dllmain.cpp                          │
│  MoriaCppMod class                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  on_unreal_   │  │  on_update() │  │  ProcessEvent│  │
│  │  init()       │  │  main tick   │  │  hooks       │  │
│  │  - config     │  │  - UI input  │  │  - rotation  │  │
│  │  - keybinds   │  │  - widgets   │  │  - snap      │  │
│  │  - hooks      │  │  - state     │  │  - build HUD │  │
│  └──────────────┘  │  machines    │  │  - capture   │  │
│                     └──────────────┘  └──────────────┘  │
│                                                         │
│  ┌─── Inlined Modules (.inl) ────────────────────────┐  │
│  │ moria_common.inl       Screen coords, widget find │  │
│  │ moria_datatable.inl    DataTable read/write/add   │  │
│  │ moria_DefinitionProcessing.inl  XML def system    │  │
│  │ moria_inventory.inl    Trash/replenish/effects    │  │
│  │ moria_quickbuild.inl   Quick build state machine  │  │
│  │ moria_placement.inl    Ghost actor, GATA, recipes │  │
│  │ moria_hism.inl         HISM removal/replay        │  │
│  │ moria_widgets.inl      UMG widget creation/mgmt   │  │
│  │ moria_overlay_mgmt.inl Overlay slot updates       │  │
│  │ moria_debug.inl        PrintString, game state    │  │
│  │ moria_stability.inl    VFX/light audit system     │  │
│  └───────────────────────────────────────────────────┘  │
│                                                         │
│  ┌─── Headers (.h) ─────────────────────────────────┐   │
│  │ moria_common.h      Types, constants, overlays   │   │
│  │ moria_reflection.h  Offset caches, probes        │   │
│  │ moria_keybinds.h    KeyBind struct, VK tables    │   │
│  │ moria_testable.h    Pure parsers, no UE4SS deps  │   │
│  └───────────────────────────────────────────────────┘  │
│                                                         │
│  ┌─── Standalone ────────────────────────────────────┐  │
│  │ moria_overlay.cpp   Win32 GDI+ overlay thread    │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Zero hardcoded Blueprint offsets**: All property access goes through runtime reflection (`resolveOffset()` + `ForEachProperty` caching). The mod discovers property offsets at runtime and caches them, so it survives game updates that change struct layouts.

2. **Defensive memory access**: `isReadableMemory()` (VirtualQuery-based) guards all pointer dereferences from external data. Engine APIs like `ImportText`/`ExportText` are preferred where available as they handle memory safety internally.

3. **Lazy initialization**: Widgets, DataTable bindings, and property offset caches are initialized on first use, not at startup. This handles the staggered loading of game assets during world entry.

4. **Game-thread only**: All UObject access happens on the game thread (inside `on_update()` or ProcessEvent callbacks). The only separate thread is the Win32 overlay renderer, which communicates with the game thread through atomic variables and a critical section.

---

## File Map

```
MoriaCppMod/
├── CMakeLists.txt              Build configuration
├── localization/
│   └── en.json                 English localization strings
├── src/
│   ├── dllmain.cpp             Main class (2,054 lines)
│   ├── moria_common.h          Shared types, constants, macros (420+ lines)
│   ├── moria_reflection.h      Property resolution, offset caches (800+ lines)
│   ├── moria_keybinds.h        Keybind configuration (200+ lines)
│   ├── moria_testable.h        Platform-independent parsers (700+ lines)
│   ├── moria_common.inl        Screen coords, widget utilities (215 lines)
│   ├── moria_datatable.inl     DataTable CRUD (370+ lines)
│   ├── moria_DefinitionProcessing.inl  Game Mods system (2,078 lines)
│   ├── moria_inventory.inl     Inventory features (500+ lines)
│   ├── moria_quickbuild.inl    Quick build state machine (1,000+ lines)
│   ├── moria_placement.inl     Ghost placement, GATA (400+ lines)
│   ├── moria_hism.inl          HISM removal system (1,000+ lines)
│   ├── moria_widgets.inl       UMG widget creation (4,238 lines)
│   ├── moria_overlay_mgmt.inl  Overlay slot management (250+ lines)
│   ├── moria_overlay.cpp       Win32 GDI+ renderer (412 lines)
│   ├── moria_debug.inl         Debug utilities (445 lines)
│   ├── moria_stability.inl     Stability audit (425 lines)
│   └── .clang-format           Code formatting rules
└── tests/
    ├── CMakeLists.txt           GoogleTest v1.15.2 setup
    ├── test_file_io.cpp         File I/O parser tests
    ├── test_key_helpers.cpp     Key name/VK code conversion tests
    ├── test_loc.cpp             Localization parser tests
    ├── test_memory.cpp          Memory safety utility tests
    ├── test_string_helpers.cpp  String/text utility tests
    └── build/                   Test build output
```

### Include Order and Dependencies

The `.inl` files are `#include`-d inside the class body in `dllmain.cpp` at two insertion points:

**First block (line 189-199)** — core utilities and data:
```
#include "moria_common.inl"              // Screen coordinate helpers
#include "moria_datatable.inl"           // DataTable CRUD
#include "moria_DefinitionProcessing.inl" // XML definition system
#include "moria_debug.inl"               // Debug/diagnostic functions
#include "moria_hism.inl"                // HISM removal system
#include "moria_inventory.inl"           // Inventory features
#include "moria_stability.inl"           // Stability audit
```

**Second block (line 411-416)** — UI and interaction:
```
#include "moria_placement.inl"           // Ghost placement, GATA
#include "moria_quickbuild.inl"          // Quick build state machine
#include "moria_widgets.inl"             // UMG widget creation
#include "moria_overlay_mgmt.inl"        // Overlay slot updates
```

The headers are included at the top of `dllmain.cpp` in dependency order:
```
#include "moria_common.h"     // Types, constants (includes moria_testable.h)
#include "moria_reflection.h" // Reflection utilities (depends on common.h)
#include "moria_keybinds.h"   // Keybind definitions
```

---

## Build System

### Prerequisites

- Visual Studio 2026 (v18), MSVC v14.50
- CMake 4.1.2
- Rust 1.93.1 (for UE4SS build system)
- UE4SS source (experimental-latest branch, cce0ed64)

### Build Commands

Build the mod DLL (approximately 5 seconds):
```bash
cd "<workspace>/cpp-mod"
PATH="$PATH:/c/Users/johnb/.cargo/bin"
"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    --build build --config "Game__Shipping__Win64" --target MoriaCppMod
```

The build configuration is `Game__Shipping__Win64` — not Release or Debug. This is dictated by the UE4SS build system.

### Deploy

Copy the built DLL to the game's mod directory:
```bash
cp build/MyCPPMods/MoriaCppMod/Game__Shipping__Win64/MoriaCppMod.dll \
   "C:/Program Files/Epic Games/ReturnToMoria/Moria/Binaries/Win64/Mods/MoriaCppMod/dlls/main.dll"
```

### Run Tests

```bash
cd "<workspace>/cpp-mod/MyCPPMods/MoriaCppMod/tests"
cmake --build build --config Release && build/Release/MoriaCppModTests.exe
```

286 tests across 5 test files verify all platform-independent parsers.

---

## Mod Lifecycle

### Startup Sequence

1. **DLL injection**: UE4SS loads `main.dll` from `Mods/MoriaCppMod/dlls/`, calls `start_mod()` which constructs `MoriaCppMod`.

2. **`on_unreal_init()`** (line 447): Called once when the Unreal engine is ready.
   - Loads configuration from `MoriaCppMod.ini`
   - Initializes localization from `en.json`
   - Applies localized labels to all 22 keybindings
   - Registers keybind handlers for F1-F8 (quick build), modifier combos (recipe assignment), utility keys
   - Probes `PrintString` function for debug output
   - Loads saved quick-build slot assignments from INI file
   - Registers ProcessEvent pre/post callbacks for rotation capture and build menu interception

3. **`on_update()`** (line 749): Called every game tick (~60Hz). This is the main loop driving all mod behavior.

### Per-Tick Processing (on_update)

The `on_update()` method (1,290 lines) processes these subsystems in order each tick:

1. **Widget initialization**: Creates UMG toolbars and panels if they don't exist yet. These are deferred because the widget subsystem isn't ready immediately.

2. **Input handling**: Processes keyboard input for rename dialogs, trash confirmation dialogs, and configuration panel interactions.

3. **Mod Controller bar**: Handles F12 config toggle, slot hover detection, LMB click dispatch, Alt-drag repositioning.

4. **Config panel (FontTest)**: Tab switching, checkbox interaction, keybind rebinding UI, removal entry list management, Game Mods tab.

5. **Build system cache**: Refreshes cached build HUD and BuildComp references. Processes the quick-build and handle-resolution state machines.

6. **World/character loading**: Detects player character (BP_FGKDwarf_C), triggers world initialization, applies cheat toggles, handles character unload cleanup.

7. **Removal replay**: After character load (15-second delay), replays saved HISM removals. Periodically rescans for new streamed-in components.

### Shutdown

`~MoriaCppMod()` cleans up the overlay thread and critical section. The `uninstall_mod()` export deletes the class instance.

---

## Module Reference

### dllmain.cpp — Main Entry Point

**Lines**: 2,054
**Role**: Contains the `MoriaCppMod` class definition, member variables, constructor/destructor, `on_unreal_init()`, `on_update()`, ProcessEvent callbacks, and module exports.

**Key sections**:
- Lines 18-200: Private member variables (removal system, timestamps, replay state, file I/O helpers)
- Lines 189-199: First `.inl` include block (data and utility modules)
- Lines 200-416: Quick-build slots (RecipeSlot struct, state enums, UMG widget pointers for all toolbars/panels)
- Lines 411-416: Second `.inl` include block (UI and interaction modules)
- Lines 418-447: Constructor/destructor
- Lines 447-650: `on_unreal_init()` — one-time setup, keybind registration, ProcessEvent hook registration
- Lines 650-748: ProcessEvent pre/post callbacks (rotation tracking, build menu snap detection, recipe handle capture)
- Lines 749-2039: `on_update()` — main per-tick processing loop
- Lines 2043-2054: Module exports (`start_mod`, `uninstall_mod`)

**Member variables of note**:
- `m_undoStack`: Stack of removal entries for undo support
- `m_savedRemovals`: Persistent removal entries loaded from save file
- `m_typeRemovals`: Type-rule removals (remove all instances of a mesh type)
- `m_processedComps`: Set of already-processed HISM component pointers
- `m_qbPhase`: Quick-build state machine phase (Idle, CancelGhost, WaitingForShow, SelectRecipeWalk)
- `m_recipeSlots[12]`: Quick-build recipe slot data (display name, texture, row name, bLock block data, recipe handle)
- Widget pointers: `m_umgBarWidget`, `m_mcBarWidget`, `m_abBarWidget`, `m_fontTestWidget`, `m_trashDlgWidget`, `m_targetInfoWidget`, `m_errorBoxWidget`

---

### moria_common.h — Shared Types and Constants

**Lines**: 420+
**Role**: Central header included by all files. Defines data types, constants, macros, and shared state.

**Key contents**:

- **Logging macros**: `VLOG()` (verbose, gated on `s_verbose`), `QBLOG()` (quick-build specific logging)
- **Global state**: `s_verbose` (runtime verbose flag), `s_language` (localization language code)
- **Geometry structs**: `TArrayView` (wraps FArrayProperty), `FVec3f`, `FQuat4f`, `FTransformRaw` (48-byte packed transform)
- **Function parameter structs**: `GetInstanceCount_Params`, `GetInstanceTransform_Params`, `FHitResultLocal` (0x88 bytes) — these mirror UE4 struct layouts for ProcessEvent calls
- **Removal system**: `RemovalEntry` (mesh name, position, type rule, friendly name)
- **Overlay system**: `OverlaySlot` (12 slots), `OverlayState` (shared state between game thread and overlay thread)
- **Config**: `ConfigState` (persisted preferences), `CONFIG_TAB_COUNT` (number of visible config tabs, currently 4 for debug, 3 for release)
- **Critical section**: `CriticalSectionLock` RAII wrapper for Win32 synchronization
- **Hardcoded byte offsets**: Constants for FSlateBrush, FSlateFontInfo, FDataTableRowHandle, recipe block variant structures. These are fallbacks when reflection fails; the mod prefers runtime-resolved offsets.

---

### moria_reflection.h — Property Resolution and Caching

**Lines**: 800+
**Role**: Runtime property discovery via UE4SS reflection APIs. This is the backbone of the mod's version-resilience: instead of hardcoding offsets from header dumps, the mod discovers property layouts at runtime.

**Offset cache pattern**: Every cached offset is an `int` initialized to `-2` (unresolved sentinel). On first access, `resolveOffset()` walks the UStruct property chain. The result is cached as either `-1` (property not found) or `>= 0` (valid offset). Subsequent accesses hit the cache directly.

**Core functions**:
- `resolveOffset(UObject*, propertyName, cache)`: Finds property offset in a UClass/UStruct hierarchy, caches result
- `resolveOffsetAndSize(UObject*, propertyName, offsetCache, sizeCache)`: Same but also captures property size
- `resolveStructFieldOffset(UStruct*, fieldName, cache)`: Walks struct fields specifically (for DataTable row structs)
- `findStructProperty(UStruct*, name)`: Returns FStructProperty pointer for named property

**Probe functions** perform deep struct introspection with validation:
- `ensureBrushOffset()`: Resolves FSlateBrush.ImageSize and .ResourceObject within UImage
- `probeFontStruct()`: Resolves FSlateFontInfo.TypefaceFontName and .Size within UTextBlock
- `probeRecipeBlockStruct()`: Resolves FMorRecipeBlock fields including variant entries and FDataTableRowHandle
- `probeItemInstanceStruct()`: Resolves FItemInstance and FItemInstanceArray fields for inventory operations

**ProcessEvent parameter resolvers**: Specialized structs and resolver functions for each ProcessEvent call the mod makes:
- `LTResolved` / `resolveLTOffsets()`: LineTraceSingle parameters (raycasting)
- `UITResolved` / `resolveUITOffsets()`: UpdateInstanceTransform parameters (HISM manipulation)
- `DSPResolved` / `resolveDSPOffsets()`: DeprojectScreenPositionToWorld parameters
- `SIMUIResolved` / `resolveSIMUIOffsets()`: SetInputMode_UIOnlyEx parameters
- `BSEResolved` / `resolveBSEOffsets()`: blockSelectedEvent parameters (quick-build recipe selection callback)

---

### moria_keybinds.h — Key Configuration

**Lines**: 200+
**Role**: Defines the 22 rebindable keybinds and Win32 input utilities.

**Keybind layout** (BIND_COUNT = 22):
| Index | Name | Default Key | Section |
|-------|------|-------------|---------|
| 0-7 | Quick Build 1-8 | F1-F8 | Quick Building |
| 8 | Rotation Step | F9 | Mod Controller |
| 9 | Snap Toggle | `[` (OEM_FOUR) | Mod Controller |
| 10 | Integrity Check | `/` (DIVIDE) | Mod Controller |
| 11 | Super Dwarf | `]` (OEM_FIVE) | Mod Controller |
| 12 | Target | `'` (OEM_SIX) | Mod Controller |
| 13 | Configuration | F12 | Mod Controller |
| 14-16 | Remove/Undo/RemoveAll | Num1/2/3 | Mod Controller |
| 17 | Advanced Builder Open | `+` (ADD) | Advanced Builder |
| 18 | Reserved (Diagnostics) | — (disabled) | — |
| 19 | Trash Item | DEL | Game Options |
| 20 | Replenish Item | INS | Game Options |
| 21 | Remove Attributes | END | Game Options |

**Modifier system**: A single modifier key (`s_modifierVK`, default SHIFT) is used for combo operations. `isModifierDown()` checks its state via `GetAsyncKeyState()`. Users can cycle through SHIFT/CTRL/ALT/RALT.

**Numpad alternate mapping**: `numpadShiftAlternate()` handles the Windows behavior where numpad keys produce different VK codes depending on NumLock state.

**Window finding**: `findGameWindow()` enumerates top-level windows to locate the largest visible "UnrealWindow" — used to position the Win32 overlay relative to the game window.

---

### moria_testable.h — Platform-Independent Utilities

**Lines**: 700+
**Role**: All parsing logic that can be tested without a running game. This file has zero UE4SS dependencies, making it fully testable with GoogleTest.

**Key systems**:

- **Localization (Loc namespace)**: JSON file parser with UTF-8 BOM handling, Unicode escape decoding (`\uXXXX`), and 80+ default English strings. `Loc::get("key")` returns a `std::wstring`.

- **Text formatting**: `wrapText()` wraps long strings at word boundaries. `extractFriendlyName()` pulls human-readable names from mesh identifiers. `componentNameToMeshId()` strips trailing instance suffixes.

- **Key conversion**: Bidirectional VK code to display name mapping. Handles F1-F24, numpad keys, special keys, and hex fallback. `keyName(vk)` and `nameToVK(name)` are inverses.

- **INI parsing**: Line-by-line parser for `[Section]` headers, `key=value` pairs, and comments. `bindIndexToIniKey()` maps numeric indices to named keys (e.g., 0 → "QuickBuild1", 19 → "TrashItem").

- **Removal file parsing**: Parses saved removal entries (mesh name + XYZ position) and type rules (@meshName for remove-all patterns).

- **Slot/quickbuild parsing**: Parses pipe-delimited slot assignments (`slotIdx|displayName|textureName|rowName`).

- **Memory safety**: `isReadableMemory()` uses Win32 `VirtualQuery` to validate pointer ranges before access. Handles PAGE_GUARD and PAGE_NOCACHE variants.

---

### moria_common.inl — Screen Coordinates and Widget Helpers

**Lines**: 215
**Role**: Screen coordinate conversion pipeline and widget discovery.

The `ScreenCoords` struct (defined in `moria_common.h`, methods here) provides a coordinate system abstraction over UE4's multi-layer coordinate spaces:

- **Pixel coordinates**: Raw screen pixels (e.g., 2560x1440)
- **Slate coordinates**: DPI-scaled UI coordinates (pixels / viewportScale)
- **Fractional coordinates**: Normalized 0.0-1.0 range

The `refresh()` method queries viewport size and scale factor via ProcessEvent on `WidgetLayoutLibrary::GetViewportSize` and `GetViewportScale`. All coordinate transforms go through this cached state.

**`findWidgetByClass()`**: Searches all loaded UserWidgets by class name using `UObjectGlobals::FindAllOf("UserWidget")`. An optional visibility filter returns only visible widgets.

**`setWidgetVisibility()`**: Sets widget visibility via ProcessEvent on `SetVisibility`, using UE4's `ESlateVisibility` enum values (0=Visible, 1=Collapsed, 2=Hidden, etc.).

---

### moria_datatable.inl — DataTable CRUD Operations

**Lines**: 370+
**Role**: Runtime DataTable access layer. The `DataTableUtil` struct wraps a UDataTable pointer and provides type-safe read/write/add operations through UE4SS reflection.

**DataTableUtil struct**:
- `bind(name)`: Finds a DataTable by name via `FindAllOf("DataTable")`, caches the RowStruct and row size
- `unbind()`: Releases cached references
- `getRowCount()`: Returns number of rows via RowMap header
- `getRowNames()`: Enumerates all row FNames in the DataTable
- `findRowData(rowName)`: Locates raw row data pointer by FName comparison
- `resolvePropertyOffset(propName)`: Finds property offset within the row struct, with caching
- `locateField(row, prop)`: Combines findRowData + resolvePropertyOffset
- `locateFieldWithProp(row, prop)`: Same but also returns the FProperty pointer for type-aware operations
- `readInt32(row, prop)`: Reads int32 value using FNumericProperty API
- `readFText(row, prop)`: Reads FText field as wide string
- `readObjectPtr(row, prop)`: Reads UObject* via FObjectPropertyBase
- `writeInt32(row, prop, val)`: Writes int32 using FNumericProperty API
- `writeFloat(row, prop, val)`: Writes float using FNumericProperty API
- `addRow(rowName)`: Allocates new row, initializes via UStruct::InitializeStruct, inserts via vtable dispatch to AddRowInternal

**Pre-bound DataTable instances** (member variables):
- `m_dtConstructions`, `m_dtConstructionRecipes` — Building system tables
- `m_dtItems`, `m_dtWeapons`, `m_dtTools`, `m_dtArmor`, `m_dtConsumables` — Item tables
- `m_dtContainerItems`, `m_dtOres` — Container and resource tables

**Cross-table helpers**:
- `resolveConstructionRowName()`: Follows a recipe's ResultConstructionHandle to find the construction row
- `lookupRecipeIcon()`: Resolves recipe → construction → Icon UObject
- `lookupRecipeDisplayName()`: Resolves recipe → construction → DisplayName FText

**Important implementation detail**: `findRowData()` uses `FNAME_Find` (not `FNAME_Add`) to avoid polluting the global FName table with lookup-only names. `FNAME_Add` is reserved for `addRow()` where a new persistent FName is needed.

**Row map access**: Currently uses hardcoded `DT_ROWMAP_OFFSET = 0x30` and `SET_ELEMENT_SIZE = 24` for TSet iteration. A planned refactor will replace this with UE4SS `GetRowMap()` wrapper.

---

### moria_DefinitionProcessing.inl — Game Mods System

**Lines**: 2,078
**Role**: The definition pack system — a data-driven modding framework that lets community modders modify game DataTables via XML definition files.

**Architecture**: Mods are packaged as directories containing:
- A `.ini` manifest file (mod name, author, version, file paths)
- One or more `.def` XML files describing DataTable modifications

**XML Parser** (custom, zero dependencies):
- `xmlParse()` → `xmlParseElement()` → `xmlParseAttrs()` → `xmlParseAttrValue()`
- Handles entity decoding (`&amp;`, `&lt;`, `&gt;`, `&apos;`, `&quot;`), self-closing tags, nested elements
- Produces a tree of `XmlElement` structs (tag, attributes, text, children)

**Definition file format**: Each `.def` file targets a DataTable and contains operations:
- `<change>`: Modify an existing row's property
- `<add-row>`: Add a new row with JSON property data
- `<delete>`: Remove a GameplayTag from a row's tag container

**Property writing pipeline**:
1. `applyChange()` resolves the target row and property path (may be nested like `StageDataList[3].RequiredItems`)
2. `resolveNestedProperty()` walks dot-separated path segments through UStruct reflection, handling array indices
3. `writeValueToField()` calls `FProperty::ImportText_Direct()` to convert string values to any property type
4. `readFieldAsString()` calls `FProperty::ExportText_Direct()` for before/after verification logging

**JSON property writer** (`writeJsonPropertyToField`, 390 lines): Handles UAssetAPI JSON format for `<add-row>` operations. Recursively processes JSON objects for struct properties, arrays, and scalars. Supports all UE4 property types including nested structs, TArrays, enums, soft object references, and GameplayTag containers.

**Row handle fixup** (`fixRowHandlePointers`, 129 lines): After adding a new row, copies `DataTable*` pointers from a reference row into any `FDataTableRowHandle` fields in the new row. This is necessary because DataTable row handles contain a pointer to their owning table, which can't be expressed in JSON.

**Mod discovery and management**:
- `discoverGameMods()`: Scans `definitions/` directory for `.ini` manifests
- `readEnabledMods()`: Reads `GameMods.ini` for enabled mod list
- `saveGameMods()`: Writes enabled state to `GameMods.ini`
- `loadAndApplyDefinitions()` (423 lines): Main orchestrator — discovers mods, reads enabled state, parses all definitions, applies changes with verification logging, tracks statistics

---

### moria_inventory.inl — Inventory Management

**Lines**: 500+
**Role**: Item manipulation features accessible via keybinds.

**Trash Item** (DEL key):
- Finds the player's last-moved item handle via ProcessEvent on inventory component
- Shows confirmation dialog with item icon, name, and stack count
- On confirmation: calls `DropItem` ProcessEvent then `DestroyActor` on the dropped item

**Replenish Item** (INS key):
- Reads the item's DataTable row to find MaxStackSize
- Calls `ServerDebugSetItem` ProcessEvent to set the item count to max

**Remove Attributes** (END key):
- Strips tint color and rune effects from the last-moved item
- Uses `callServerTintItem()` with default values to reset appearance

**Common utilities**:
- `findActorComponentByClass()`: Finds a component on an actor by class name
- `callServerTintItem()`: ProcessEvent wrapper for tint modification
- `dumpItemEffects()`: Diagnostic dump of item effect properties (gated on `s_verbose`)
- SEH wrapper: `__try/__except` around ProcessEvent calls for crash resilience

---

### moria_quickbuild.inl — Quick Build State Machine

**Lines**: 1,000+
**Role**: Instant recipe selection system. Players assign recipes to F1-F8 slots, then press F-keys to instantly select that recipe in the build menu.

**State machine phases** (`PlacePhase` enum):
1. **Idle**: Waiting for F-key press
2. **CancelGhost**: Cancels any active placement ghost before menu interaction
3. **WaitingForShow**: Build menu is opening, waiting for OnAfterShow ProcessEvent callback
4. **SelectRecipeWalk**: Walking the recipe tree to find and select the target recipe

**Recipe matching**: Uses `bLock` (a 120-byte data block on each recipe entry) as the unique recipe identifier. When a player assigns a recipe to a slot, the mod captures the current bLock data. During quick-build, it walks the recipe tree comparing bLock values.

**Recipe capture flow**:
1. Player opens build menu and selects a recipe normally
2. ProcessEvent post-callback intercepts the selection event
3. Mod captures bLock data, recipe handle, display name, texture name
4. Player presses SHIFT+F-key to assign to a slot
5. Slot data is persisted to INI file

**Quick-build execution flow**:
1. Player presses F-key → sets `m_pendingQuickBuildSlot`
2. State machine opens build menu if needed (via FGK Show/Hide API)
3. Waits for OnAfterShow callback confirming menu is ready
4. Walks recipe tree comparing bLock data
5. When found, triggers `blockSelectedEvent` ProcessEvent to select the recipe
6. Build menu receives the selection and enters placement mode

**Debouncing**: Rapid F-key presses update `m_pendingQuickBuildSlot` (last wins). A 500ms post-completion cooldown prevents cascade issues.

**Auto-select guard**: `m_isAutoSelecting` flag + RAII `AutoSelectGuard` prevents the ProcessEvent capture hook from intercepting the mod's own programmatic selections.

---

### moria_placement.inl — Placement and Ghost Actor Management

**Lines**: 400+
**Role**: Build placement support including ghost actor management and GATA (Gameplay Ability Target Actor) resolution.

**Key functions**:
- `resolveGATA()`: Finds the active Gameplay Ability Target Actor from the build HUD. Uses `getCachedBuildHUD()` (not `findBuildHUD()`) to avoid expensive widget scans.
- `cancelActivePlacement()`: Cancels an in-progress placement by calling CancelTargeting
- `findCurrentGhostActor()`: Locates the ghost preview actor during placement
- Recipe block data access: Reads recipe properties through the resolved GATA actor

**Build from Target**: When SHIFT+Target key is pressed, the mod captures the aimed-at construction's recipe and triggers a quick-build for that recipe. This is the "build what I'm looking at" feature.

---

### moria_hism.inl — HISM Removal System

**Lines**: 1,000+
**Role**: Hierarchical Instanced Static Mesh (HISM) removal system. Players can remove individual environment meshes (rocks, foliage, debris) and the removals persist across sessions.

**Removal flow**:
1. Player aims at an environment object and presses Remove key (Num1)
2. Mod performs a line trace to find the hit HISM component and instance index
3. Instance transform is captured and stored as a `RemovalEntry`
4. `UpdateInstanceTransform` ProcessEvent scales the instance to zero (effectively hiding it)
5. Entry is appended to save file for persistence

**Replay system**: On world load, saved removals are replayed against all loaded HISM components. A streaming-aware rescan runs every 60 seconds to catch newly-loaded components.

**Type rules**: Prefixing a mesh name with `@` creates a type rule that removes ALL instances of that mesh type. This is persisted and replayed separately from position-based removals.

**Undo**: Pressing Num2 pops the last removal from the undo stack and restores the original transform.

**Remove All** (Num3): Removes all instances of the aimed-at mesh type (creates a type rule).

**Max 3 UpdateInstanceTransform calls per frame**: This is a hard engine constraint. The replay system batches removals across ticks to avoid render thread crashes.

---

### moria_widgets.inl — UMG Widget System

**Lines**: 4,238 (largest file)
**Role**: All UMG (Unreal Motion Graphics) widget creation and management. Widgets are built entirely through reflection — no Blueprint or compiled widget assets.

**Widget creation pattern**: Every widget is constructed via:
1. `UObjectGlobals::StaticConstructObject()` or `NewObject<>()` to create the widget
2. Property writes via reflection for layout, styling, text
3. `AddToViewport()` or `AddChildToCanvas()` for hierarchy placement
4. ProcessEvent calls for dynamic behavior (SetBrush, SetText, SetVisibility, etc.)

**Major widget systems**:

**Experimental Bar** (`createExperimentalBar`, 527 lines): The F1-F8 quick-build toolbar showing recipe icons and state indicators. 12 slots total with icon caching and state images (empty/occupied/active).

**Advanced Builder Bar** (`createAdvancedBuilderBar`, 442 lines): Toolbar showing rotation display, current recipe icon, target indicator, and config button.

**Mod Controller Bar** (`createModControllerBar`, 544 lines): The F12 configuration panel with tabbed interface (Key Mapping, Quick Build, Hide Environment, Game Mods), checkboxes, and keybind rebinding UI.

**Target Info** (`createTargetInfoWidget`, 216 lines): Floating overlay showing aimed-at construction name and details.

**Error Box** (`createErrorBox`, 162 lines): Error message overlay for user notifications.

**Trash Dialog**: Confirmation dialog for item deletion with icon, name, and count display.

**Rename Dialog** (`showRenameDialog`, 292 lines): Text input dialog for renaming removal entries.

**Config panel (toggleFontTestPanel)** (1,091 lines): The full F12 configuration UI with 4 tabs:
- Tab 1 (Key Mapping): Lists all 22 keybinds with current assignments, click to rebind
- Tab 2 (Quick Build): Shows F1-F8 slot assignments
- Tab 3 (Hide Environment): Lists saved removals with rename/delete
- Tab 4 (Game Mods): Mod checkboxes with enable/disable per mod (currently hidden, CONFIG_TAB_COUNT=3)

**Low-level UMG utilities** (lines 4-410): `umgSetBrush`, `umgSetOpacity`, `umgSetSlotSize`, `umgSetText`, `umgSetTextColor`, `umgSetBold`, `umgSetFontSize`, `setWidgetPosition`, `hitTestToolbarSlot`, and more.

---

### moria_overlay_mgmt.inl — Overlay Management

**Lines**: 250+
**Role**: Bridge between game state and the Win32 overlay display.

**`updateOverlaySlots()`**: The primary function that synchronizes overlay slot state from game data. Updates icon textures, display names, state indicators, rotation labels, and toolbar number for all 12 overlay slots.

**`startOverlay()` / `stopOverlay()`**: Manages the overlay thread lifecycle. GDI+ initialization happens in `startOverlay()` BEFORE the first `updateOverlaySlots()` call — this ordering is critical because slot updates may load GDI+ Image objects.

---

### moria_overlay.cpp — Win32 Overlay Renderer

**Lines**: 412
**Role**: Standalone Win32 overlay window rendered with GDI+ on a dedicated thread at 5 Hz.

This is the only file compiled separately (not inlined). It creates a transparent, click-through, always-on-top window (`WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT`) positioned above the game window.

**`renderOverlay()`** (306 lines): The main render function. Uses GDI+ to draw:
- 12 slots (8 build + 4 utility) with background rectangles
- Recipe icons (loaded from PNG cache as GDI+ Image objects)
- Key labels (F1-F8, F9-F12)
- State indicators (slot state, rotation value, toolbar number)
- Separator between build and utility sections

**`overlayWndProc()`**: Window message handler. Returns `HTTRANSPARENT` for `WM_NCHITTEST` to pass mouse events through. Handles `WM_TIMER` for 5 Hz redraw and `WM_DESTROY` for cleanup.

**`overlayThreadProc()`**: Worker thread entry point. Creates the overlay window, starts the timer, and runs a standard Win32 message loop.

**Communication**: The overlay thread reads from `OverlayState` (defined in `moria_common.h`) which is written by the game thread. Access is synchronized via `CriticalSectionLock`.

---

### moria_debug.inl — Debug and Diagnostics

**Lines**: 445
**Role**: Debug output and diagnostic features.

**Key functions**:
- `probePrintString()`: Discovers the PrintString function and caches its parameter offsets. This is needed because PrintString's parameter layout varies.
- `showOnScreen(text, duration, r, g, b)`: Displays colored text on screen via PrintString ProcessEvent.
- `logGameState(label)`: Dumps current player, pawn, inventory component state to the UE4SS console.
- `callDebugFunc(actorClass, funcName)`: Calls arbitrary functions on debug actors by class/function name. Used for cheat toggles.
- `syncDebugToggleState()`: Reads cheat menu state (FreeBuild, FreeCraft, etc.) from BP_DebugMenu actor.
- `showDebugMenuState()`: Displays current cheat state on screen.
- `applyPendingCharacterName()`: Applies character name changes via CustomizationManager.
- `dispatchMcSlot(slot)`: Dispatches Mod Controller toolbar slot clicks to their respective actions.

---

### moria_stability.inl — Stability Audit System

**Lines**: 425
**Role**: Visual stability analysis of player constructions.

**`runStabilityAudit()`**: Scans all construction manager stability components, identifies structural problems (unsupported, overstressed), and spawns visual indicators:
- **Critical issues**: Red point lights at problem locations
- **Marginal issues**: Yellow point lights at warning locations
- **VFX markers**: Niagara particle effects at each location for visibility

**`clearStabilityHighlights()`**: Removes all spawned audit actors.

**`destroyAuditActors()`**: Cleanup function that destroys all spawned actors via ProcessEvent `K2_DestroyActor`.

The audit system uses deferred actor spawning (`SpawnActorDeferred` + `FinishSpawning`) for PointLights to allow property configuration before the actor is activated.

---

## Key Workflows

### Quick Build Workflow

This is the most complex workflow in the mod, involving ProcessEvent hooks, a state machine, and asynchronous menu interaction.

**Assignment flow** (one-time setup):
```
Player opens build menu → selects recipe normally
  → ProcessEvent post-hook captures bLock + handle + name + texture
Player presses SHIFT + F-key
  → Recipe data saved to RecipeSlot[N]
  → Slot persisted to INI file
  → Overlay updated with icon and label
```

**Execution flow** (each use):
```
Player presses F-key
  → on_update() sees m_pendingQuickBuildSlot is set
  → If build menu not open:
      Cancel any active ghost (CancelPlacement)
      Open build menu via FGK Show() API
      Phase → WaitingForShow
  → ProcessEvent post-hook fires OnAfterShow
      Phase → SelectRecipeWalk
  → Walk recipe tree:
      For each recipe block, compare bLock (120 bytes)
      If match found:
        Call blockSelectedEvent ProcessEvent
        m_isAutoSelecting = true (suppress capture)
        Phase → Idle
      If not found after full walk:
        Show error message
        Phase → Idle
```

**Critical timing details**:
- Show/Hide cooldown: 350ms between open/close to let animations complete
- Post-completion cooldown: 500ms after recipe selection before accepting new F-keys
- Debounce: Rapid F-key presses during state machine execution update `m_pendingQuickBuildSlot` (last wins)

### Definition Processing Workflow

**Startup flow**:
```
loadAndApplyDefinitions() called during world init
  → discoverGameMods() scans definitions/ for .ini manifests
  → readEnabledMods() reads GameMods.ini
  → For each enabled mod:
      parseManifest() reads .ini (ModInfo + Paths sections)
      For each .def file path:
        readFileToString() loads XML
        parseDef() produces DefDefinition struct
        extractDataTableName() identifies target table
        getOrBindDataTable() binds DataTableUtil if needed
  → For each definition entry:
      If <change>:
        applyChange() resolves row + property path
        resolveNestedProperty() walks dot-separated segments
        readFieldAsString() captures "before" value
        writeValueToField() applies new value via ImportText_Direct
        readFieldAsString() captures "after" value for verification
      If <add-row>:
        applyAddRow() allocates and inserts new row
        writeJsonPropertyToField() recursively populates all fields
        fixRowHandlePointers() patches DataTable* back-pointers
      If <delete>:
        applyDelete() removes GameplayTag via removeGameplayTag()
  → Log statistics (applied/skipped/errors per mod)
```

### HISM Removal Workflow

**Single removal**:
```
Player aims at environment mesh → presses Num1
  → Line trace finds HISM component + instance index
  → GetInstanceTransform captures original transform
  → UpdateInstanceTransform sets scale to zero (hides instance)
  → RemovalEntry pushed to undo stack
  → SavedRemoval appended to save file
```

**Replay on world load**:
```
Character loaded → 15 second delay (world streaming settle time)
  → For each saved removal:
      Find matching HISM component by mesh name
      Match instance by position (within tolerance)
      UpdateInstanceTransform to hide
  → Max 3 UpdateInstanceTransform per frame (engine limit)
  → Periodic rescan every 60s for newly-streamed components
```

### Widget Creation Workflow

All widgets follow the same reflection-based creation pattern:

```
1. Find widget class:     FindAllOf("UserWidget") or FindFirstOf("CanvasPanel")
2. Create widget:         StaticConstructObject(widgetClass)
3. Set properties:        resolveOffset + memcpy for layout properties
4. Add to viewport:       ProcessEvent("AddToViewport") or AddChildToCanvas
5. Configure content:     ProcessEvent("SetBrush"), ProcessEvent("SetText"), etc.
6. Cache widget pointer:  Store in member variable (m_umgBarWidget, etc.)
7. Cache child pointers:  Store references to frequently-updated children
```

Widgets are destroyed by calling `RemoveFromViewport` and nulling cached pointers. The mod detects stale widget pointers by checking if the widget is still valid before use.

### Inventory Feature Workflow

**Trash Item**:
```
DEL pressed → find last-moved FItemHandle
  → Resolve item class from DataTable
  → Get display name via CDO GetDisplayName
  → Show confirmation dialog (icon + name + count)
  → On confirm: DropItem ProcessEvent → DestroyActor on dropped item
```

**Replenish Item**:
```
INS pressed → find last-moved FItemHandle
  → Look up MaxStackSize from item's DataTable row
  → ServerDebugSetItem ProcessEvent with max count
```

---

## Reflection and Property Resolution

The mod's reflection system is built on UE4SS's `ForEachProperty` iterator and UE4's `FProperty` API.

### Offset Resolution Flow

```
resolveOffset(object, "PropertyName", cacheVariable)
  │
  ├─ If cacheVariable >= 0 → return (already resolved)
  ├─ If cacheVariable == -1 → return (known not found)
  │
  ├─ Walk UStruct hierarchy (object → super → super → ...)
  │   For each UStruct level:
  │     ForEachProperty():
  │       If property name matches:
  │         cacheVariable = GetOffset_Internal()
  │         return
  │
  └─ cacheVariable = -1 (not found)
```

### Property Access Patterns

**Reading a value**:
```cpp
int off = -2;
resolveOffset(actor, L"PropertyName", off);
if (off >= 0) {
    auto* base = reinterpret_cast<uint8_t*>(actor);
    int32_t value;
    std::memcpy(&value, base + off, sizeof(value));
}
```

**Using FProperty API** (preferred for type safety):
```cpp
auto field = dtUtil.locateFieldWithProp(rowName, propName);
if (field.data && field.prop) {
    auto* numProp = CastField<FNumericProperty>(field.prop);
    if (numProp) {
        int64_t value = numProp->GetSignedIntPropertyValue(field.data + field.off);
    }
}
```

**Using ImportText/ExportText** (universal, handles all types):
```cpp
// Write any property from string
prop->ImportText_Direct(L"42", fieldData, nullptr, 0, nullptr);

// Read any property to string
FString outStr;
prop->ExportText_Direct(outStr, fieldData, nullptr, nullptr, 0, nullptr);
```

---

## Thread Safety and Synchronization

The mod has exactly two threads:
1. **Game thread**: All UObject access, ProcessEvent calls, and mod logic
2. **Overlay thread**: Win32 GDI+ rendering at 5 Hz

Communication between threads uses:
- **`OverlayState`** (defined in `moria_common.h`): Shared struct with slot data, visibility flags, position
- **`CriticalSectionLock`**: RAII Win32 critical section wrapper protecting `OverlayState` reads/writes
- **Atomic variables**: `s_capturingBind`, `s_modifierVK` for keybind capture state

**Rules**:
- Never access UObjects from the overlay thread
- Never call ProcessEvent from the overlay thread
- Always acquire the critical section before reading/writing OverlayState
- Overlay slot icon images (GDI+ Image pointers) are managed by the game thread and read by the overlay thread under the critical section

---

## ProcessEvent Hooks

The mod registers two ProcessEvent callbacks:

### Pre-callback (fires before every ProcessEvent)

Intercepts:
- `RotatePressed` / `RotateCcwPressed`: Tracks rotation step changes for overlay display

**Hot-path optimization**: The callback fires on every ProcessEvent in the game. It performs a single `GetName()` call and uses `wcscmp` on the result to minimize string allocation overhead.

### Post-callback (fires after every ProcessEvent)

Intercepts:
- `OnAfterShow`: Build menu opened — signals quick-build state machine
- `SelectRecipeBlock` / `blockSelectedEvent`: Recipe selected — captures bLock data for slot assignment
- `Tick_RecipeLoading`: Recipe loading state for handle resolution
- `OnCharacterReadyAfterJoin`: Character fully loaded notification
- Additional capture hooks for recipe handle and placement data

Same hot-path optimization as pre-callback: single `GetName()` call with `wcscmp` comparisons.

---

## Configuration System

### MoriaCppMod.ini

Located at `Mods/MoriaCppMod/MoriaCppMod.ini`. Sections:

- `[Preferences]`: `Verbose=true/false`, `Modifier=SHIFT/CTRL/ALT/RALT`
- `[Toolbar]`: `ActiveToolbar=1/2`, overlay position (`OverlayX`, `OverlayY`)
- `[KeyBindings]`: Per-key assignments (`QuickBuild1=F1`, `TrashItem=DEL`, etc.)
- `[QuickBuild]`: F1-F8 recipe slot assignments (pipe-delimited)
- `[Options]`: Game option toggles (`TrashItemEnabled`, `ReplenishItemEnabled`, `RemoveAttributesEnabled`)

### GameMods.ini

Located at `Mods/GameMods.ini` (root Mods directory, not inside MoriaCppMod). Contains:
- `[EnabledMods]`: `ModName=true/false` for each discovered definition pack

### ConfigState struct

Runtime representation of persistent preferences. Loaded at startup, saved on change. Includes:
- Active toolbar number (1 or 2)
- Overlay position
- Game option toggles
- Cheat toggles (FreeBuild, NoCollision synced from game debug menu)

---

## Localization System

The mod supports full localization through JSON language files.

### File location

`localization/<lang>.json` (e.g., `localization/en.json`)

### Loading flow

1. `Loc::initDefaults()` populates 80+ English fallback strings
2. `Loc::parseJsonFile()` loads the language file, overriding defaults
3. `Loc::get("key")` returns the localized wide string

### JSON format

```json
{
    "key_name": "Display text",
    "keybind_quickbuild": "Quick Build",
    "msg_trash_confirm": "Delete {0}?"
}
```

The parser handles UTF-8 BOM, standard JSON escapes, and `\uXXXX` Unicode sequences.

---

## Testing

### Test Framework

GoogleTest v1.15.2, configured via `tests/CMakeLists.txt`.

### Test Files

| File | Tests | Coverage |
|------|-------|----------|
| `test_file_io.cpp` | INI parsing, removal line parsing, slot parsing, keybind parsing | File I/O parsers in moria_testable.h |
| `test_key_helpers.cpp` | VK code ↔ name conversion, modifier cycling, bind index mapping | Key system in moria_testable.h |
| `test_loc.cpp` | JSON parsing, UTF-8 BOM, Unicode escapes, entity decoding | Localization in moria_testable.h |
| `test_memory.cpp` | isReadableMemory on valid/invalid/null pointers | Memory safety in moria_testable.h |
| `test_string_helpers.cpp` | wrapText, extractFriendlyName, componentNameToMeshId, trimStr | String utilities in moria_testable.h |

### Running Tests

```bash
cd "<workspace>/cpp-mod/MyCPPMods/MoriaCppMod/tests"
cmake --build build --config Release
build/Release/MoriaCppModTests.exe
```

**Total**: 286 tests. All tests run without UE4SS or the game — they test only the platform-independent code in `moria_testable.h`.

### What Is Not Testable

Code that depends on UE4SS APIs (UObject access, ProcessEvent, ForEachProperty, etc.) cannot be unit tested. These paths are verified through in-game testing with verbose logging enabled.

---

## Deployment

### Directory Structure (Game Side)

```
C:\Program Files\Epic Games\ReturnToMoria\Moria\Binaries\Win64\
├── dwmapi.dll              UE4SS proxy DLL (flat structure)
├── UE4SS.dll               UE4SS runtime
├── UE4SS-settings.ini      UE4SS configuration
├── Mods/
│   ├── GameMods.ini        Definition pack enable/disable state
│   └── MoriaCppMod/
│       ├── enabled.txt     Mod enable flag (presence = enabled)
│       ├── MoriaCppMod.ini Mod configuration
│       ├── dlls/
│       │   └── main.dll    The compiled mod DLL
│       ├── definitions/    Definition pack directories
│       └── localization/   Language files
```

### Debug vs Release Mode

**Debug mode** enables:
- UE4SS console window (ConsoleEnabled=1, GuiConsoleEnabled=1)
- Verbose logging (s_verbose=true, Verbose=true in INI)
- Font scaling 2x in console
- Game Mods tab visible (CONFIG_TAB_COUNT=4)

**Release mode** disables all of the above. The `/debug-mode on|off` skill toggles between these configurations.

### Installer

Built with Inno Setup (`installer/KhazadDumAdvancedBuilderPack.iss`). The `installer/build.ps1` script handles compilation, optional code signing via SSL.com eSigner, and output to `installer/output/`.

---

## Known Constraints and Design Decisions

### Engine Constraints

1. **Max 3 UpdateInstanceTransform calls per frame**: The render thread crashes if more are issued in a single tick. The HISM removal replay system batches operations across frames.

2. **ProcessEvent parameter buffers**: Parameters must be written to raw byte buffers at the correct offsets. The mod resolves these offsets at runtime via reflection and caches them.

3. **FGK Show/Hide timing**: The FGK framework's build menu Show/Hide calls are asynchronous. The mod waits for OnAfterShow callback before interacting with menu contents. A 350ms cooldown between open/close prevents animation re-entrancy crashes.

4. **IoStore containers**: Game assets packaged in IoStore format (.ucas/.utoc) cannot be mounted from UE4SS mods. This is why build menu recipe injection was abandoned in favor of the DataTable definition system.

### Design Decisions

1. **Inline files instead of separate compilation units**: The `.inl` pattern gives all functions access to private class members without friend declarations or accessor boilerplate. The trade-off is longer build times (all code recompiles on any change), but the 5-second build time makes this acceptable.

2. **Win32 overlay instead of pure UMG**: The overlay uses Win32 GDI+ because it needs to render above the game at all times, including during loading screens and menu transitions when UMG widgets may not be active. The UMG toolbars handle in-game interaction; the overlay handles persistent display.

3. **Custom XML parser instead of a library**: The XML parser is 250 lines and handles only the subset needed for definition files. It avoids external dependencies and compiles cleanly with the UE4SS build system.

4. **FNAME_Find vs FNAME_Add**: Read-only FName lookups use `FNAME_Find` to avoid polluting the global FName table. `FNAME_Add` is used only when creating persistent entries (new DataTable rows). This was identified as a stability issue during the v4.0.0 code review.

5. **Property offset caching**: Every resolved offset is cached in a static `int` variable. The sentinel value `-2` means unresolved, `-1` means not found, `>= 0` is the valid offset. This avoids repeated ForEachProperty iteration on hot paths.

6. **No hardcoded Blueprint offsets**: All property access goes through runtime reflection. Hardcoded byte offsets in `moria_common.h` are fallbacks for engine-level structs (FSlateBrush, FSlateFontInfo) where reflection may not be available, and are validated against reflection results when possible.

---

## Code Review Findings (v4.0.0)

### Changes Applied

**Dead code removed**:
- 7 unused `ScreenCoords` methods (getCursorSlate, pixelToFracX/Y, slateToPixelX/Y, fracToSlateX/Y)
- `updateOverlayText()` and `showTargetInfo()` pass-through wrappers
- Legacy `ParsedRotation` handler in quickbuild
- MC diagnostics one-shot block
- Hit-test debug block and `m_hitDebugDone` member
- Debug position readback block in widget creation
- Unused `viewW`/`viewH` and `uiScale` variables in 4 widget functions

**Stability fixes**:
- `FNAME_Add` → `FNAME_Find` in 3 locations (findRowData, removeGameplayTag, recipe handle resolution) — prevents FName table pollution on lookup-only operations
- `GlobalLock()` null safety check added before `memcpy` in clipboard operations
- `dumpItemEffects()` gated on `s_verbose` to avoid expensive diagnostic work in production

**Hot-path optimizations**:
- ProcessEvent pre-callback: single `GetName()` call + `wcscmp` instead of 2 separate `std::wstring` constructions
- ProcessEvent post-callback: single `GetName()` call + `wcscmp` for all 7 comparisons
- `resolveGATA()` changed from `findBuildHUD()` (full widget scan) to `getCachedBuildHUD()` (cached lookup)

**All comments removed**: Source files stripped of all inline comments per code review directive. This documentation file serves as the external reference.

### Known Issues Not Yet Addressed

- **Hardcoded byte offsets** for EditableTextBox font in `moria_widgets.inl` (`0x0368`/`0x0958`) — would require extensive probing to resolve dynamically
- **Widget creation boilerplate duplication** across toolbar creation functions — could be factored into shared helpers but risk is low
- **DataTable hardcoded offsets** (`DT_ROWMAP_OFFSET`, `SET_ELEMENT_SIZE`, `FNAME_SIZE`) — planned migration to UE4SS `GetRowMap()` wrapper (see engine API refactor plan)
