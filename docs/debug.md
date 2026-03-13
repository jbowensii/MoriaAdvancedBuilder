# Debug, Display & Cheat Commands

Source: `MoriaCppMod/src/moria_debug.inl` (Sections 6C and 6F)

## Purpose

Provides on-screen text display via UE4's PrintString, debug menu state
inspection, cheat function invocation, and the MC toolbar slot dispatch
system. This file bridges the mod's internal state with visible feedback
and the game's debug infrastructure.

## Key Functions

### probePrintString()
One-time discovery of `KismetSystemLibrary::PrintString` parameter layout.
Uses `ForEachProperty()` on the UFunction to locate offsets for:
- `WorldContextObject` -- the player controller
- `inString` -- FString (Data ptr 8B + ArrayNum 4B + ArrayMax 4B)
- `bPrintToScreen` -- bool
- `bPrintToLog` -- bool (always set false to avoid log spam)
- `TextColor` -- FLinearColor (R, G, B, A as floats)
- `Duration` -- float seconds

Sets `m_ps.valid = true` only when all critical offsets are found. Must be
called once during mod initialization before any showOnScreen calls.

### showOnScreen(text, duration, r, g, b)
Displays colored text on screen via `KismetSystemLibrary::PrintString`.
Requires `probePrintString()` to have run first (checks `m_ps.valid`).

Implementation:
- Finds the PrintString UFunction and KismetSystemLibrary CDO via
  StaticFindObject (effectively cached by the engine)
- Constructs FString in the param buffer: raw wchar_t pointer, then
  ArrayNum and ArrayMax set to string length + 1 for null terminator
- Color is FLinearColor (4 floats, 0.0-1.0 range), alpha always 1.0
- Calls ProcessEvent on the CDO (static function calling pattern)

The text wstring must remain alive during ProcessEvent. Since it is passed
by const reference and the caller holds it on the stack, this is safe.

### callDebugFunc(actorClass, funcName)
Calls a zero-parameter function on a debug menu actor. Two-pass search:

1. **Direct class search** (fast): `FindAllOf(actorClass)` then
   `GetFunctionByNameInChain(funcName)` + ProcessEvent with nullptr params
2. **Actor scan fallback** (slow): `FindAllOf("Actor")` then match by
   `safeClassName()`. Needed when the direct search class name doesn't
   match the UE4SS class registry exactly.

Logs the first 3 "not found" attempts via a static counter, then goes
silent to avoid log flood on repeated failures. Returns true on success.

### showDebugMenuState()
Reads boolean properties from `BP_DebugMenu_CraftingAndConstruction_C` to
display the current cheat state. Properties read via `getBoolProp()`:

| Property | Display | Note |
|----------|---------|------|
| `free_construction` | FreeBuild:ON/OFF | |
| `free_crafting` | FreeCraft:ON/OFF | |
| `instant_crafting` | InstantCraft:ON/OFF | |
| `construction_prereqs` | Prereqs:ON/OFF | Inverted: true = OFF |
| `construction_stability` | Stability:ON/OFF | Inverted: true = OFF |

Shows formatted summary on screen in cyan via showOnScreen.

### syncDebugToggleState()
Called on character load to sync `s_config` flags with the game's actual
debug menu state. Searches for the debug actor via direct class search
first, falls back to full actor scan. Currently syncs:
- `s_config.freeBuild` from `free_construction` property

Ensures the mod's UI toggles (config widget, overlay) match the game's
real state after world load or respawn.

### dispatchMcSlot(slot)
Central dispatch for MC (Mod Control) toolbar slot actions. Maps slot
indices to mod features via a switch statement. Called by both keyboard
handlers and UMG click handlers.

| Slot | Action | Modifier Variant |
|------|--------|-----------------|
| 0 | Rotation step +5 degrees (wraps 90->5) | Modifier: -5 degrees (wraps 5->90) |
| 1 | Show target info (dumpAimedActor) | Modifier: quickBuildFromTarget |
| 2 | Run stability audit | -- |
| 3 | Toggle hide character | Modifier: toggle fly mode |
| 4 | Swap toolbar | -- |
| 5 | Toggle snap | -- |
| 8 | Remove aimed (HISM) | -- |
| 9 | Undo last removal | -- |
| 10 | Remove all of type | -- |
| 11 | Configuration (handled separately) | -- |

**Slot 0 (Rotation) details**: reads `s_overlay.rotationStep`, increments or
decrements by 5 with wrapping, calls `setGATARotation()` on the resolved GATA
actor, saves config, updates overlay display and MC rotation label.

**Slot 1 (Target) details**: three-state logic. If modifier down, triggers
`quickBuildFromTarget()`. If `m_tiShowTick > 0` (info currently showing),
calls `hideTargetInfo()`. Otherwise calls `dumpAimedActor()` to show new info.

**Slot indices 6-7 are unused** -- reserved gap between build slots and
utility slots in the MC toolbar layout.

## Data Flow

```
Mod init:
  probePrintString() -> resolves m_ps offsets -> enables showOnScreen

Character load:
  syncDebugToggleState() -> reads debug actor bools -> updates s_config

Key/click on MC toolbar:
  dispatchMcSlot(slot) -> switch/case -> feature function

Debug menu query:
  showDebugMenuState() -> scan actors -> read 5 bool props -> showOnScreen
```

## Known Caveats

- showOnScreen constructs FString by writing raw pointers into the param
  buffer. The wstring must outlive the ProcessEvent call (guaranteed by
  stack lifetime of the const reference parameter).
- callDebugFunc only supports zero-parameter functions. Functions with
  parameters would need explicit param buffer construction.
- syncDebugToggleState only syncs freeBuild currently. Other debug
  toggles (free_crafting, instant_crafting) could be added but have no
  mod-side effect yet.
- The debug actor (BP_DebugMenu_CraftingAndConstruction_C) may not exist
  in all game states. Both showDebugMenuState and syncDebugToggleState
  handle the missing-actor case gracefully with fallback messages.
- dispatchMcSlot slot indices are not contiguous (0-5, then 8-11) because
  slots 6-7 are reserved/unused in the MC toolbar layout.
- The static s_debugNotFoundCount in callDebugFunc is never reset, so after
  3 failures the log goes permanently silent for that class name.
