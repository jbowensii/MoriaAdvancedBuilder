# moria_quickbuild.inl -- F-key Dispatch, Slot I/O, Icon Extraction

Included inside the `MoriaCppMod` class body **after** `moria_placement.inl`.
Owns recipe slot persistence, F-key dispatch with guards, recipe capture from
post-hooks, icon extraction via Canvas render targets, and overlay sync.

## Slot File I/O

### saveQuickBuildSlots()
Writes `Mods/MoriaCppMod/quickbuild_slots.txt`. Format:

```
# slot|displayName|textureName|rowName
0|Stone Wall|T_StoneWall_Icon|BP_Stone_Wall_4x2m
```

Only writes slots where `m_recipeSlots[i].used == true`. Rotation step is
persisted separately in `MoriaCppMod.ini [Preferences]`.

### loadQuickBuildSlots()
Parses the slot file via `parseSlotLine()` (returns `ParsedSlot` or
`ParsedRotation` variant). Populates `m_recipeSlots[]` display name, texture
name, and row name. Legacy `rotation=N` lines are still accepted for backward
compatibility. Calls `updateOverlayText()` and `updateBuildersBar()` after load.

### saveConfig() / loadConfig()
INI-based config for keybindings, preferences, and toolbar positions. Supports
migration from old `keybindings.txt` format (renames to `.bak` after migration).

## F-key Dispatch: quickBuildSlot(slot)

Guards (checked in order):
1. **Slot range** -- F1-F8 only (0-7).
2. **Slot empty** -- shows assignment instructions if `!m_recipeSlots[slot].used`.
3. **Handle resolution in progress** -- blocks during `HandleResolvePhase::Priming`
   or `Resolving`.
4. **Debounce** -- if SM is already running (`m_qbPhase != Idle`), updates
   `m_pendingQuickBuildSlot` so the last F-key wins. The in-flight cycle picks up
   the final slot at SelectRecipe, avoiding multiple `blockSelectedEvent` calls
   that would each trigger game UI transitions and corrupt MovieScene state.
5. **Post-completion cooldown** -- 500ms via `m_lastQBSelectTime`. Allows game UI
   cascade (HideAllScreens, StopAnimation) to settle after the previous recipe
   selection.

On pass, delegates to `startOrSwitchBuild(slot)` in `moria_placement.inl`.

## Target-Build Dispatch: quickBuildFromTarget()

Guards (checked in order):
1. **Handle resolution in progress** -- same as F-key path.
2. **Target data exists** -- requires `m_lastTargetBuildable` and either
   `m_targetBuildName` or `m_targetBuildRecipeRef` to be non-empty.
3. **SM idle** -- unlike F-key debounce (which updates pending slot), target-build
   is simply rejected if the SM is busy.
4. **Post-completion cooldown** -- same 500ms as F-keys.

On pass, delegates to `startBuildFromTarget()` in `moria_placement.inl`.

## Recipe Capture (Post-Hook)

The `blockSelectedEvent` post-hook in `dllmain.cpp` captures recipe data whenever
the player manually clicks a build-menu item. The hook:

1. Checks `m_isAutoSelecting` -- if true, returns immediately (suppresses capture
   during automated quickbuild selections to prevent overwriting user's last
   manual pick).
2. Resolves BSE param offsets, reads `selfRef` widget, calls
   `readWidgetDisplayName()` to get the display name.
3. Copies the full `bLock` data (BLOCK_DATA_SIZE bytes) into `m_lastCapturedBLock`.
4. Calls `GetSelectedRecipeHandle()` on the BuildHUD to capture the 16-byte handle
   into `m_lastCapturedHandle`.
5. Sets `m_hasLastCapture = true` and `m_hasLastHandle = true`.

### assignRecipeSlot(slot)
Called when the player presses Modifier+F-key to assign the last captured recipe.
If no capture exists (or build menu is closed), clears the slot instead. On
assignment: copies display name, bLock data, recipe handle, and extracts the row
name from the handle's FName. Calls `extractIconTextureName()` and
`extractAndSaveIcon()` to create a PNG for the overlay. Saves slots to disk and
updates the overlay and builders bar.

## Icon Extraction

### extractIconTextureName(widget)
Walks the widget property chain to find the UTexture2D name:
`widget->Icon (UImage) -> Brush.ResourceObject`. If the resource is a
`UTexture2D`, returns its name directly. If it is a `MaterialInstanceDynamic`,
walks `TextureParameterValues[0]` to reach the underlying texture.

### extractAndSaveIcon(widget, textureName, outPath)
Full Canvas render target pipeline:
1. Resolves UTexture2D from the widget chain (same traversal as above).
2. Finds required UFunctions: `CreateRenderTarget2D`, `BeginDrawCanvasToRenderTarget`,
   `K2_DrawTexture`, `EndDrawCanvasToRenderTarget`, `ExportRenderTarget`.
3. Creates a 128x128 RGBA8 render target.
4. Draws the texture to the Canvas via `K2_DrawTexture`.
5. Exports via `ExportRenderTarget` (produces `.hdr` or `.bmp`).
6. Converts to PNG using GDI+ (one-time init, never shutdown).
7. Releases the render target via `ReleaseRenderTarget2D`.

Icons are cached to `Mods/MoriaCppMod/icons/*.png`. Skips extraction if the PNG
already exists on disk.

### findBuildItemWidget(recipeName)
Scans all `UI_WBP_Build_Item_Medium_C` widgets and matches by display name via
`readWidgetDisplayName()`. Used by `assignRecipeSlot` to find the widget for icon
extraction.

## Handle Caching: cacheRecipeHandleForSlot(buildHUD, slot)

Called after successful recipe activation via `blockSelectedEvent`. Reads the
16-byte handle from `GetSelectedRecipeHandle()` on the BuildHUD and stores it in
`m_recipeSlots[slot].recipeHandle`. This enables the DIRECT path
(`trySelectRecipeByHandle`) on subsequent F-key presses, bypassing the full state
machine.

## Diagnostics

### readWidgetDisplayName(widget)
Reads the `blockName` TextBlock child widget, calls `GetText()`, returns the
FText string. Used by both capture and selection paths.

### logBLockDiagnostics(context, displayName, bLock)
Dumps Tag FName, CategoryTag FName, SortOrder, Variants RowName, and hex of key
bLock regions. Called after capture, assignment, and selection for recipe
differentiation debugging.
