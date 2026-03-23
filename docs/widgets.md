# UMG Widget System (moria_widgets.inl)

Section 6I of the MoriaCppMod class body. All UMG widgets are created at runtime
via `StaticConstructObject` + `ProcessEvent` -- no Blueprint assets involved.

## Widget Creation Pattern

Every widget follows the same flow:

1. `StaticFindObject<UClass*>` to locate the UMG class (UserWidget, Image, HorizontalBox, etc.)
2. `WidgetBlueprintLibrary::Create` via ProcessEvent to construct a UUserWidget root
3. Read `WidgetTree` property from the root widget (used as `Outer` for child construction)
4. `StaticConstructObject(FStaticConstructObjectParameters)` to create child widgets
5. `setRootWidget(widgetTree, child)` to attach the root visual element
6. `AddToViewport` via ProcessEvent with a ZOrder parameter for layering

All function parameters are resolved via `ForEachProperty()` at runtime -- no hardcoded
offsets. The `findParam()` helper locates parameters by name and returns their offset.

All ProcessEvent calls use `safeProcessEvent()` with SEH protection and `isObjectAlive`
validation.

## Deferred Widget Removal (v5.3.0+)

**`deferRemoveWidget(widget)`** -- hides a widget immediately (SetVisibility=Hidden),
then schedules `RemoveFromParent` for the next frame. This two-phase approach prevents
Slate `PaintFastPath` crashes that occur when removing a widget synchronously during
the paint pass.

Used at all 7+ UMG widget removal sites: config panel close, target info hide,
crosshair hide, error box hide, trash dialog close, reposition message destroy, and
placeholder info box destroy.

## Toolbar Types

### Builders Bar (BB, index 0)
- 8-slot `UHorizontalBox` inside nested Borders (both transparent)
- Each slot is a `UVerticalBox` containing:
  - `UOverlay` (top): state image (Empty/Inactive/Active) + icon image layered on top
  - Frame overlay (bottom): frame image + keycap background + key label text
- Frame overlaps state icon by 15% (negative top padding)
- Textures: `T_UI_Frame_HUD_AB_Active_BothHands`, `T_UI_Btn_HUD_EpicAB_Empty`,
  `T_UI_Btn_HUD_EpicAB_Disabled`, `T_UI_Btn_HUD_EpicAB_Focused`
- Uniform render scale 0.81x via `SetRenderScale`

### Mod Controller (MC, index 2)
- 3x3 grid (3 columns, 3 rows) of action buttons created via `createModControllerBar()`
- 9 slots total (`MC_SLOTS`), each with state image, icon, and key label
- Slots dispatch to `dispatchMcSlot()`: rotation, snap toggle, target, stability,
  hide character / fly mode, toolbar swap, remove target, undo, remove all

### Advanced Builder (AB, index 1)
- Single toggle button, positioned lower-right
- Uses `Tools_Icon` texture overlaid on the standard frame/active textures
- Created by `createAdvancedBuilderBar()`, persists until world unload

### Target Info Popup
- Right-side panel showing aimed actor details (class, name, display name, path)
- Indicates whether the actor is buildable and shows recipe reference
- Built from `UBorder` > `UVerticalBox` > multiple `UTextBlock` rows
- Shown by `dumpAimedActor()` via `showTargetInfoUMG()`

### Crosshair Reticle (v5.3.6+)
- Centered `T_UI_Bow_Reticle` texture shown when pressing ] (inspect key)
- Resolution-scaled via `uiScale`, positioned at viewport center
- Auto-hides after 40s (`CROSSHAIR_FADE_MS`)
- Created lazily by `createCrosshair()`, shown/hidden via `showCrosshair()`/`hideCrosshair()`

### Error/Info Box
- Always-visible UMG box at screen bottom for status messages
- `showInfoMessage()` for user-facing info (not debug-only like showOnScreen)
- `showErrorBox()` for error conditions
- Auto-hides after 5s (`ERROR_BOX_DURATION_MS`)

### Trash Confirmation Dialog
- Modal dialog shown when DEL key pressed to trash an item
- Displays item icon, name, and stack count
- Confirm/cancel buttons with keyboard support

### Config Menu (F12 Panel)
- 4-tab modal created by `createConfigWidget()`
- Tab 0: Game Options (toggle checkboxes for mod features: NoCollision, Trash, Replenish, RemoveAttrs, Pitch, Roll, Save Game button)
- Tab 1: Key Mapping (rebindable keybinds)
- Tab 2: Hide Environment (HISM removal entries, grouped by bubble with current bubble starred at top)
- Tab 3: Game Mods (definition pack checkboxes, saves to GameMods.ini)
- Switches input mode to UI-only (`setInputModeUI`) when visible
- F12 panel alignment: `wLeft` corrected for 1540px panel width
- Environment tab button hit detection: kbX1 = kbX0 + inner button width (300px), excludes gold frame and scrollbar zone
- Scrollbar: Panel 1540px, rightSizeBox SetWidthOverride(960), ScrollbarPadding 60px

## DPI Scaling and Coordinate Conversion

### getViewportDpiScale(worldContext)
- Calls `WidgetLayoutLibrary::GetViewportScale` via `safeProcessEvent`
- Returns the engine DPI scale factor (e.g., 1.0 at 100% DPI, 1.5 at 150%)
- Guarded: returns 1.0 if result is below 0.1

### computeUiScale(physicalH, dpiScale)
- Maps design-space sizes (authored at 1080p logical) to current viewport
- Formula: `(physicalH / dpiScale) / 1080.0`
- Minimum 0.5 for readability

### Coordinate System
- `m_screen.refresh(pc)` caches viewport dimensions, DPI scale, and conversion helpers
- `fracToPixelX/Y`: fractional viewport position (0.0-1.0) to pixel coordinates
- `pixelToFracX/Y`: pixel coordinates to fractional position
- Widget positions stored as fractions for resolution independence

### setWidgetPosition(widget, x, y, bRemoveDPIScale)
- Calls `SetPositionInViewport(FVector2D, bRemoveDPIScale)` via `safeProcessEvent`
- `bRemoveDPIScale=false`: position in slate units (engine won't divide by DPI again)
- `bRemoveDPIScale=true`: position in raw pixels (engine divides by DPI scale)

## Key Label Overlays

- Each toolbar slot has a `UTextBlock` key label overlaid on a keycap background image
- `refreshKeyLabels()` syncs all key labels from current `s_bindings` array
- BB slots 0-7: `keyName(s_bindings[i].key)`
- MC slots: `keyName(s_bindings[MC_BIND_BASE + i].key)`
- AB slot: `keyName(s_bindings[BIND_AB_OPEN].key)`

## Texture Caching

- `m_umgIconTextures[8]` caches resolved `UTexture2D*` per BB slot
- `m_umgIconNames[8]` tracks texture name per slot to detect changes
- `findTexture2DByName()` scans all loaded Texture2D objects by name
- `updateBuildersBar()` syncs recipe slot states and loads icon textures
- `setUmgSlotIcon()` sizes icons to fit within state icon bounds (76% scale, aspect preserved)
- Brush property offset (`s_off_brush`) resolved once via `ensureBrushOffset()`
- Native texture size read from `FSlateBrush.ImageSize` at `UImage + s_off_brush + offset`

## UMG Helper Functions

| Function | Purpose |
|---|---|
| `umgSetBrush` | Set UImage brush to a texture via SetBrushFromTexture |
| `umgSetOpacity` | Set opacity on a UImage |
| `umgSetText` | Set text on a UTextBlock via SetText(FText) |
| `umgSetTextColor` | Set text color via SetColorAndOpacity(FSlateColor) |
| `umgSetBold` | Patch FSlateFontInfo.TypefaceFontName to "Bold" |
| `umgSetFontSize` | Patch FSlateFontInfo font size |
| `umgSetRenderScale` | Set render scale (FVector2D) on a UWidget |
| `umgSetBrushSize` | Set explicit brush size on a UImage |
| `umgSetSlotSize` | Set FSlateChildSize on a layout slot |
| `umgSetSlotPadding` | Set FMargin padding on a layout slot |
| `umgSetHAlign` | Set horizontal alignment on a slot |
| `umgSetVAlign` | Set vertical alignment on a slot |
| `hitTestToolbarSlot` | Hit-test cursor against all visible toolbars (BB/AB/MC) |
| `getMousePositionSlate` | Get cursor in DPI-adjusted viewport coordinates |
| `deferRemoveWidget` | Two-phase widget removal (hide + next-frame destroy) |
| `setWidgetVisibility` | Set ESlateVisibility on a widget |
