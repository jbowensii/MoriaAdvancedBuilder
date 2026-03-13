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
- 4x3 grid (4 columns, 3 rows) of action buttons created via `createModControllerBar()`
- 12 slots total (`MC_SLOTS`), each with state image, icon, and key label
- Slots dispatch to `dispatchMcSlot()`: rotation, target, stability, super dwarf,
  toolbar swap, snap toggle, remove target, undo, remove all, configuration

### Advanced Builder (AB, index 1)
- Single toggle button, positioned lower-right
- Uses `Tools_Icon` texture overlaid on the standard frame/active textures
- Created by `createAdvancedBuilderBar()`, persists until world unload

### Target Info Popup
- Right-side panel showing aimed actor details (class, name, display name, path)
- Indicates whether the actor is buildable and shows recipe reference
- Built from `UBorder` > `UVerticalBox` > multiple `UTextBlock` rows
- Shown by `dumpAimedActor()` via `showTargetInfoUMG()`

### Config Menu
- 3-tab modal created by `createConfigWidget()` (line 2192)
- Tab 0: Optional Mods (toggle checkboxes for mod features)
- Tab 1: Key Mapping (rebindable keybinds)
- Tab 2: Hide Environment (HISM removal type rules)
- Switches input mode to UI-only (`setInputModeUI`) when visible

## DPI Scaling and Coordinate Conversion

### getViewportDpiScale(worldContext)
- Calls `WidgetLayoutLibrary::GetViewportScale` via ProcessEvent
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
- Calls `SetPositionInViewport(FVector2D, bRemoveDPIScale)` via ProcessEvent
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
