# WBP_UI_AdvancedJoinOptions — Blueprint reference

Decoded from `Moria-Replication/tools/cloud-exports-by-type/Function/UI/MainMenu/WorldSelect/WBP_UI_AdvancedJoinOptions.json` + runtime widget tree harvest at `cpp-mod/docs/widget-harvest/WBP_UI_AdvancedJoinOptions_C.json`.

## Overall structure

A panel that appears when user clicks "Advanced Join Options" on the JoinWorld screen. Two sections:

1. **Direct Join** — IP + password fields, Join button (always shown)
2. **Local Join** — Port + password fields, Join button (PC-only, hidden on console)
3. **Error/Throbber** — single-line status area
4. **Close button** — at bottom, dismisses the panel

Outer SizeBox: **920 wide × auto height**

## Widget tree (key children)

```
CanvasPanel_0
└── SizeBox(920) — root container
    └── VerticalBox_68
        ├── Border_35
        │   └── VerticalBox_66
        │       ├── Header_DirectJoin (UI_WBP_Text_Header_C)
        │       ├── Spacer_184
        │       ├── TextBlock_68 — direct-join description
        │       ├── Spacer_4
        │       ├── DirectJoinIP (VBox)
        │       │   ├── TextBlock_180 — "IP Address" label
        │       │   └── Overlay_0
        │       │       ├── FieldBG (Image background)
        │       │       └── TextField_DirectJoinIP (EditableText)
        │       ├── Spacer_8
        │       ├── DirectJoinPassword (VBox)
        │       │   ├── TextBlock_1 — "Password" label
        │       │   └── Overlay
        │       │       ├── FieldBG_1 (Image)
        │       │       └── TextField_DirectJoinPassword (EditableText)
        │       ├── Spacer_6
        │       ├── Button_DirectJoinIP (WBP_FrontEndButton_C)
        │       ├── PCOnlySection (VBox)
        │       │   ├── Spacer_1
        │       │   ├── Header_LocalJoin (UI_WBP_Text_Header_C)
        │       │   ├── Spacer_3
        │       │   ├── TextBlock — local-join description
        │       │   ├── Spacer_5
        │       │   ├── LocalJoinPort (VBox)
        │       │   │   ├── TextBlock_2 — "Port" label
        │       │   │   └── Overlay_1
        │       │   │       ├── FieldBG_2 (Image)
        │       │   │       └── TextField_LocalJoinPort (EditableText)
        │       │   ├── Spacer
        │       │   ├── LocalJoinPassword (VBox)
        │       │   │   ├── TextBlock_3 — "Password" label
        │       │   │   └── Overlay_2
        │       │   │       ├── FieldBG_3 (Image)
        │       │   │       └── TextField_LocalJoinPassword (EditableText)
        │       │   ├── Spacer_7
        │       │   ├── Button_JoinLocal (WBP_FrontEndButton_C)
        │       │   └── Spacer_9
        │       ├── Divider (Image)
        │       ├── Spacer_2
        │       └── SizeBox_1(0×45.5)
        │           └── ThrobberSwitcher (WidgetSwitcher)
        │               ├── JoinErrorText (TextBlock)
        │               └── Throbber_106
        └── Button_Close (WBP_FrontEndButton_C)
```

## Functions to know (30 total, key ones)

### Lifecycle hooks
- `OnBeforeShow` / `OnAfterShow` — show lifecycle. **Hook `OnAfterShow` post for our intercept.**
- `Construct` — runs once on instance creation
- `ShowPanel` / `ClosePanel` — programmatic show/hide
- `OnAdvancedJoinClosed` — delegate fired when user closes panel

### Input field events
- `BndEvt__Field_DirectJoinIP_OnEditableTextChangedEvent` — IP changed
- `BndEvt__Field_LocalJoinPort_OnEditableTextChangedEvent` — port changed
- `GetLocalTextFieldValues` — pulls current port + password values

### Button events
- `BndEvt__Button_DirectJoinIP_OnMenuButtonClicked` → calls `JoinByIP_Pressed` delegate
- `BndEvt__Button_JoinLocal_OnMenuButtonClicked` → calls `JoinLocalDedicatedServer_Pressed` delegate
- `BndEvt__Button_Close_OnMenuButtonClicked` → calls `ClosePanel`

### State control
- `TogglePCSection(bool)` — show/hide LocalJoin section based on platform
- `ToggleJoinButtonEnabled(bool)` — enable/disable Join buttons (e.g. when no IP entered)
- `ToggleThrobber(bool)` — show throbber while joining
- `UpdateJoinErrorText(FText)` — display error message
- `PlayTextPulseAnimation` — pulse on error
- `ToggleCloseButtonVisibility`

### Focus / navigation
- `OnCustomFocusSet` / `OnCustomFocusLost`
- `GetFocusableWidget` — returns first focusable child for keyboard/gamepad
- `SetUpVerticalNavigation` — wires up ↑↓ navigation
- `OnBindInputs` / `OnUnbindInputs`
- `HandleInputChanged` — triggers `MorSettingStateManager:IsLimitedMultiplayerSessionModePlatform` check, calls `TogglePCSection`

## Backend calls (from BP)

The buttons fire DELEGATES rather than calling `MorGameSessionManager` directly:
- `JoinByIP_Pressed` (delegate broadcast — caller subscribes; in master flow, the JoinWorld screen subscribes and calls `MorGameSessionManager:DirectJoinSessionWithPassword`)
- `JoinLocalDedicatedServer_Pressed` (delegate)
- `OnAdvancedJoinClosed` (delegate)

So the AdvancedJoinOptions panel is **a pure UI panel** — it gathers input + emits events. The actual session-join logic lives on the parent `WBP_UI_JoinWorldScreen` (already cloned).

## Capture targets

When intercepting `OnAfterShow`, walk the tree and capture:

**UClass refs** (already cached, since these are shared with JoinWorldScreen):
- `WBP_FrontEndButton_C` ✓
- `UI_WBP_Text_Header_C` ✓

**FSlateFontInfo bytes** (new — capture from these TextBlocks):
- `TextBlock_68` — direct-join description font
- `TextBlock_180`, `TextBlock_1` — IP/password labels
- `TextBlock_2`, `TextBlock_3` — port/password labels
- `JoinErrorText` — error font + color

**WidgetStyle bytes** (one capture covers all input fields — they all use the same EditableText style):
- `TextField_DirectJoinIP.WidgetStyle` (768 bytes)

**Texture refs**:
- `FieldBG.Brush.ResourceObject` — input field background image
- `Divider.Brush.ResourceObject` — section divider

**Header captures** — Header_DirectJoin and Header_LocalJoin are `UI_WBP_Text_Header_C` instances. Their style is already part of that class — we just spawn fresh instances.

## Master positions (TBD)

Will measure after first capture run via:
- `overlay_measure.py` against fresh master + replica screenshots
- Pixel sampling at known landmarks
- The widget is positioned by the JoinWorld screen via `SessionHistorySwitcher` — when user clicks Advanced Join Options, the WidgetSwitcher flips to show this panel **instead of** the SessionHistory panel
