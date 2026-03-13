# Win32 GDI+ Overlay System

Sources:
- `MoriaCppMod/src/moria_overlay_mgmt.inl` (Section 6J -- lifecycle, input mode, config)
- `MoriaCppMod/src/moria_overlay.cpp` (rendering thread, window proc)

## Purpose

Renders a transparent 12-slot hotbar overlay at the top center of the game
window using Win32 GDI+ on a dedicated thread. Slots F1-F8 show build recipe
icons, slots F9-F12 show utility controls (target, rotation, toolbar swap,
config). The overlay is click-through and always-on-top, refreshing at 5 Hz.

## Architecture

The overlay runs entirely on a background thread and must NOT call UE4SS APIs.
It reads shared state from `s_overlay` (protected by `slotCS` critical section)
and `s_bindings` (read-only from the overlay thread -- set once at startup, no
synchronization needed).

Window styles: `WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`
with `WS_POPUP`. Click-through achieved by returning `HTTRANSPARENT` from
`WM_NCHITTEST`. Per-pixel alpha via `UpdateLayeredWindow` with `ULW_ALPHA`.

## Management Functions (moria_overlay_mgmt.inl)

### updateOverlaySlots()
Syncs overlay slot display from `m_recipeSlots[]`. Acquires `slotCS` lock, then
for each of the 8 build slots: copies `used`, `displayName`, `textureName`. If
texture name changed, discards old GDI+ icon. If slot is used with a texture name
but no loaded icon, attempts to load `{iconFolder}\{textureName}.png` via
`Gdiplus::Image::FromFile`. Sets `s_overlay.needsUpdate = true`.

### startOverlay()
Initializes and launches the overlay thread:
1. Guards against double-start (`if (s_overlay.thread) return`).
2. Initializes critical section (`slotCS`) if not already done.
3. Sets icon folder to `{gameDir}\Mods\MoriaCppMod\icons\`.
4. **Initializes GDI+ early** (before thread creation) so updateOverlaySlots
   can load cached PNG icons immediately. This ordering is critical.
5. Sets initial state: running=true, visible=m_showHotbar, activeToolbar.
6. Calls `updateOverlaySlots()` to populate slot data.
7. Creates overlay thread via `CreateThread(overlayThreadProc)`.

### stopOverlay()
Shuts down the overlay thread:
1. Sets `s_overlay.running = false`.
2. Posts `WM_CLOSE` to the overlay window to break the message loop.
3. Waits up to 3 seconds for thread exit (`WaitForSingleObject`).
4. Releases all loaded icon images (must happen before GDI+ shutdown).
5. Calls `GdiplusShutdown` and resets `gdipToken` to 0.
6. Deletes the critical section.

**LINT NOTE (#9)**: The 3-second timeout means the render thread could
theoretically still hold slotCS when DeleteCriticalSection runs. Intentionally
accepted -- only happens during game shutdown. INFINITE wait risks hanging if
GDI+ is stuck.

### setInputModeUI(focusWidget)
Switches to UI-only input via `WidgetBlueprintLibrary::SetInputMode_UIOnlyEx`.
Param offsets resolved via `resolveSIMUIOffsets`. Sets `bShowMouseCursor = true`
on the player controller via `setBoolProp`. Mouse lock mode: DoNotLock.

### setInputModeGame()
Restores game-only input via `WidgetBlueprintLibrary::SetInputMode_GameOnly`.
Param offsets resolved via `resolveSIMGOffsets`. Clears `bShowMouseCursor`.

### toggleConfig()
Toggles UMG config widget visibility. Creates widget if needed. When showing:
updates key labels, FreeBuild state, collision state, removal count, then calls
`setInputModeUI()`. When hiding: calls `setInputModeGame()`.

### Toolbar Repositioning Mode
- **toggleRepositionMode()**: enters/exits drag mode. Creates instruction
  message + placeholder InfoBox on enter; destroys them and saves config on exit.
- **createRepositionMessage()**: UUserWidget with yellow TextBlock at Z-order 250.
- **createPlaceholderInfoBox()**: mock Target Info widget (SizeBox > Border >
  VBox > TextBlocks) at Z-order 249 for drag-positioning.
- **showTargetInfo(...)**: delegates to `showTargetInfoUMG`.
- **undoLast()**: pops undo stack (see hism.md for details).

## Rendering (moria_overlay.cpp)

### renderOverlay(hwnd)
Called on every WM_TIMER (5 Hz). Core rendering pipeline:

1. Finds/validates game window HWND, gets client rect for positioning.
2. Calculates scale: `gameHeight / 1080.0` (min 0.5).
3. Computes dimensions: 48px base slot, 4px gap, 6px padding, 14px label
   height, 8px separator between F8 and F9.
4. Overlay width = padding*2 + 12 slots + 11 gaps + separator.
5. Centered horizontally over game window, 4*scale pixels from top.
6. If not visible, hides window and returns.
7. Creates 32-bit ARGB DIB section for per-pixel alpha rendering.
8. Copies all 12 slots under slotCS lock (localSlots array).
9. GDI+ rendering: rounded-rect background, 12 slot boxes (used slots brighter),
   F1-F8 icons or letter fallback, F9 archery target, F10 rotation step/total,
   F11 toolbar number, F12 procedural gear icon. Key labels below each slot.
10. `UpdateLayeredWindow` with `ULW_ALPHA` for per-pixel transparency.
11. Cleanup: bitmap, memory DC, screen DC.

### overlayThreadProc(param)
Thread entry: GDI+ fallback init, register window class `MoriaCppModOverlay`,
poll for game window (30s max), create layered/topmost window, show + render,
set 200ms timer (5 Hz), run message loop. Cleanup on exit: kill timer, destroy
window, GdiplusShutdown, unregister class.

### overlayWndProc
`WM_NCHITTEST` -> HTTRANSPARENT (click-through). `WM_TIMER` -> renderOverlay.
`WM_DESTROY` -> KillTimer + PostQuitMessage.

## Overlay Slot Layout

```
[F1][F2][F3][F4][F5][F6][F7][F8] | [F9][F10][F11][F12]
 ---- Build recipe icons ----   sep  --- Utility ---
```

12 slots total (8 build + separator + 4 utility). Consolas font throughout.

## Data Flow

```
Recipe assignment / icon extraction (game thread):
  -> updateOverlaySlots() [under slotCS]
     -> copies m_recipeSlots[] to s_overlay.slots[]
     -> loads PNG icons from disk via GDI+

Overlay thread (5 Hz):
  -> WM_TIMER -> renderOverlay()
     -> copies s_overlay.slots[] to localSlots [under slotCS]
     -> renders to ARGB bitmap via GDI+
     -> UpdateLayeredWindow

Lifecycle:
  startOverlay() -> GDI+ init -> updateOverlaySlots() -> CreateThread
  stopOverlay()  -> running=false -> WM_CLOSE -> wait 3s -> cleanup
```

## Known Caveats

- GDI+ must be initialized in startOverlay() BEFORE updateOverlaySlots() is
  called, because loading PNG files requires an active GDI+ session.
- Overlay repositions on next 5 Hz tick if game window moves (up to 200ms lag).
- Icon images (GDI+ objects) must be released BEFORE GdiplusShutdown.
- gdipToken reset to 0 after shutdown enables re-init on restart (Fix #7).
- Config gear icon is procedurally drawn, not loaded from PNG.
- s_bindings are read-only from overlay thread (set once at startup, no lock).
- The 3-second shutdown timeout (LINT NOTE #9) is a pragmatic tradeoff --
  see stopOverlay section.
