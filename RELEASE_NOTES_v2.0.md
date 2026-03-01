# Khazad-dum Advanced Builder Pack v2.0 — Release Notes

## Highlights

**Runtime Reflection** — All hardcoded Blueprint class offsets have been replaced with runtime property resolution via Unreal's `ForEachProperty()` API. The mod now resolves offsets dynamically at first use, making it resilient to game updates that shift class layouts.

**Major Cleanup** — Over 2,400 lines of dead code removed, stale comments fixed, and documentation updated across the entire codebase. The source is now ~11,000 lines (down from ~12,000).

---

## New Features

- **Recipe Diagnostics** — `logBLockDiagnostics()` logs Tag, CategoryTag, SortOrder, Variants, and hex dumps at 4 instrumentation points (capture, assign, build, target-build) to differentiate same-named recipes across categories (e.g., "Column Crown" in Granite vs Adamant)

## Offset Reflection Conversion

Replaced all inline hardcoded offsets on Blueprint/UObject classes with runtime reflection:

- **4 new reflected properties**: `TargetActor` (UBuildOverlayWidget), `selectedRecipe`, `selectedName`, `recipesDataTable` (UI_WBP_Build_Tab_C)
- **LineTraceSingle parameters**: `LTOff` namespace (14 constexpr values) replaced with `LTResolved` struct resolved via `ForEachProperty()` on the UFunction at first call
- **UpdateInstanceTransform parameters**: Hardcoded offsets (+0, +16, +64, +65, +66, +67) replaced with `UITResolved` struct resolved at runtime
- **GetInstanceTransform_Params validation**: One-time runtime check comparing the packed struct layout against `transFunc->ForEachProperty()` results, logs a warning on mismatch
- **resolveGATA()**: Converted from `hudBase + 1000` to `s_off_targetActor`
- **Probe function**: Converted from hardcoded 1120/1536/1544 to reflected property offsets

Struct-internal offsets (FSlateBrush, FSlateFontInfo, FMorRecipeBlock sub-fields, FItemInstance, UDataTable internals) remain hardcoded — these are POD value types not resolvable via UObject reflection, verified stable against CXXHeaderDump.

## Toolbar Swap Improvements

- `swapToolbarTick()` rewritten to use `GetItemForSlot` (replaces `GetItemForHotbarSlot`)
- Removed dead `clearHotbar` state machine guard from swap entry

## Code Quality

### Dead Code Removed (~2,400 lines)
- 7 `#if 0` blocks: `startRotationSpy`, `testAllDisplayMethods`, `dumpAllWidgets`, `dumpBuildCraftClasses`/`probeBuildTabRecipe`/`probeBuildConstruction`/`dumpDebugMenus`, `probeInventoryHotbar`, `clearHotbar`/`clearHotbarTick`, `rotateBuildPlacement`/`rotateBuildPlacementCcw`
- Dead `clearHotbar` state variables: `m_clearingHotbar`, `m_clearHotbarCount`, `m_clearHotbarDropped`, `m_clearHotbarWait`
- Abandoned pitch/roll research code
- Swap toolbar diagnostic instrumentation (reverted after debugging)

### Stale Comments Fixed
- Toolbar swap section header: "F12" corrected to "PAGE_DOWN"
- Removed "(Num5 toggle)" and "(Num7 toggle)" references to deleted keybinds
- Removed "Clear-hotbar progress" from `on_update()` documentation
- Updated section 6E header and `discoverBagHandle` comment
- Triple-blank-line artifacts cleaned up

### Documentation Updated
- All 11 section headers (6A-6K) audited for accuracy
- File header updated to v2.0
- Version references synchronized across all 6 locations

## Installer

- Welcome page text condensed to prevent vertical cutoff
- Version bumped to 2.0 across `.iss` script and `build.ps1`

## Build

- Clean build, zero warnings (MSVC v14.50, VS2026)
- Source: ~10,858 lines (down from ~12,062 at v1.8)

---

## Upgrade Notes

- **No configuration changes required** — existing `MoriaCppMod.ini`, `quickbuild_slots.txt`, and `removed_instances.txt` files are fully compatible
- The mod will log new `Resolved 'PropertyName' @ offset ...` messages on first use of each reflected property — this is normal startup behavior
- If verbose logging is enabled, `GetInstanceTransform_Params` validation results will appear once per session

## Full Changelog

**v1.8 → v2.0**: 931 insertions, 2,430 deletions across `dllmain.cpp`
