# Mod-Owned Join World UI (v6.7.0+)

The pattern for safely taking over a native game UMG screen without breaking
its native button wiring, switcher logic, or back-navigation. Reference
implementation: `WBP_UI_JoinWorldScreen` and `WBP_UI_AdvancedJoinOptions`.

This is a project-specific companion to the global skill at
`C:\Users\johnb\.claude\skills\ue4-ui-duplication\SKILL.md`.

## Overview

We modify the native UMG widget **in place**. We do NOT remove it from the
viewport, we do NOT spawn a fresh duplicate. The native widget keeps its
parent (the MainMenu), its delegates, its switcher state, and its Esc/back
behavior — and we just walk its widget tree at intercept time to:

- Tint key TextBlocks bright sky-blue (visible proof we modified it)
- Replace the broken native session-history rows with mod-controlled JSON entries
- Add right-click → confirmation popup → delete UX

Initial attempts in v6.6.0 used the spawn-duplicate pattern (`WidgetBlueprintLibrary::Create` of the same UClass). That failed because a standalone-spawned UserWidget has no MainMenu wiring — Join, Advanced, Cancel, and Esc all silently broke. The pivot to in-place modification took ~30 minutes
once decided.

## Files

| File | Role |
|---|---|
| `moria_join_world_ui.inl`     | JoinWorld in-place mods: tint + inject session rows |
| `moria_advanced_join_ui.inl`  | AdvancedJoinOptions in-place mods: tint TextBlocks |
| `moria_session_history.inl`   | JSON CRUD, password obfuscation, BP row injection, right-click delete + popup |
| `moria_join_assets.h`         | Asset path constants (textures, fonts) |

All four are `#include`d inside the class scope of `MoriaCppMod` in `dllmain.cpp`.

## Architecture (in-place modification)

```text
1. ProcessEvent post-hook on `OnAfterShow`
   ├── safeClassName(context) == WBP_UI_JoinWorldScreen_C → onNativeJoinWorldShown()
   └── safeClassName(context) == WBP_UI_AdvancedJoinOptions_C → onNativeAdvancedJoinShown()
2. onNativeXxxShown(nativeWidget):
   ├── m_nativeXxxWidget = m_modXxxWidget = nativeWidget   ← same pointer, no duplicate
   └── m_pendingShowXxxUI = true                            ← deferred to next tick
3. tickXxxUI() (called from main update):
   ├── consume pending flag
   └── applyModificationsToXxx()
       ├── walk tree, find target TextBlocks, tint them
       └── (JoinWorld only) injectSessionHistoryRows() to swap in our JSON entries
4. hideModXxxUI() — minimal cleanup. Just clears the FWeakObjectPtr fields.
   Native flow handles its own dismissal via the BP's normal Close/Esc path.
```

## Session history storage

**File:** `<game>/ue4ss/Mods/MoriaCppMod/session_history.json` (managed via `modPath()`).

**Schema:** array of objects:
```json
{ "name": "...", "domain": "...", "port": "7777",
  "password": "enc:...", "lastJoined": "2026-05-01T12:00:00Z" }
```

Passwords are XOR-obfuscated (key `"MoriaModSessKey!"`) + Base64-encoded, prefixed
with `enc:` so we can detect plaintext entries on first load and migrate them.

**CRUD entry points** (in `moria_session_history.inl`):
- `loadSessionHistory()` / `saveSessionHistory()`
- `addOrUpdateSessionHistory(entry)` — match by domain+port (case-insensitive)
- `removeSessionHistoryAt(idx)` / `removeSessionHistory(domain, port)`

## Row injection — use BP's own UFunction

**Don't** manually construct `WBP_UI_SessionHistory_Item_C` widgets and set TextBlock
text by hand. Use the BP's own `AddSessionHistoryItem(FMorConnectionHistoryItem)`
UFunction. The BP creates each row, sets `ConnectionHistoryItemData`, subscribes
button events, and registers with the parent `WBP_UI_SessionHistoryList`. The
native click chain then works for free:

```text
SelectButton.OnReleased → JoinSessionPressed delegate → 
SessionHistoryList.OnJoinSessionHistoryItemPressed →
JoinWorldScreen.OnTryJoinPreviousSessionPressed →
MorGameSessionManager.DirectJoinSessionWithPassword
```

`FMorConnectionHistoryItem` layout (88 bytes, see `dumps/CXXHeaderDump/Moria.hpp:1960`):

| offset | type | field |
|---|---|---|
| 0x00 | FString | WorldName |
| 0x10 | uint8 (EMorConnectionType) | ConnectionType (0=InviteCode, 1=IpAndPort) |
| 0x18 | FString | InviteString — used as `HostAndOptionalPort` |
| 0x28 | FString | UniqueInviteCode (empty for IP joins) |
| 0x38 | FString | OptionalPassword |
| 0x48 | bool   | bIsDedicatedServer |
| 0x50 | int64  | FDateTime ticks |

### Critical: FString lifetime around ProcessEvent

`buildConnectionHistoryItem(dst, entry, isDedicated)` placement-news four
FStrings into the parm buffer. Each FString allocates a heap buffer in its ctor.
**You MUST call `destroyConnectionHistoryItem(dst)` after `safeProcessEvent`** to
destruct each FString — otherwise every inject leaks 4 heap allocations × N rows.

UE4 BP value-param semantics deep-copy the struct on entry, so destructing the
caller's copy is safe — the BP's row keeps its own deep-copied FStrings.

### Critical: UTF-8 conversion

Never `std::wstring(s.begin(), s.end())` — that zero-extends each UTF-8 byte and
corrupts non-ASCII characters. Use `utf8ToWide()` from `moria_common.h` (and
`wideToUtf8()` from `moria_testable.h`).

## Capture-on-join (auto-save server connections)

Hook **BP-level events** in the global ProcessEvent post-hook (substring match,
not exact-match — BP-generated UFunctions have long auto-names):

| Hook | Triggers when | Source of data |
|---|---|---|
| `OnJoinSessionHistoryItemPressed` / `JoinSessionPressed` | User clicks a history row | FMorConnectionHistoryItem in parms |
| `Button_DirectJoinIP` ∧ `OnMenuButtonClicked` | User clicks Direct Join on Advanced | (deferred) read TextField_DirectJoinIP / TextField_DirectJoinPassword |
| `Button_JoinLocal` ∧ `OnMenuButtonClicked` | User clicks Local Join on Advanced | (deferred) read TextField_LocalJoinPort / TextField_LocalJoinPassword |

`UMorGameSessionManager.DirectJoinSessionWithPassword` is a C++ direct call —
its ProcessEvent post-hook does **not** reliably fire. Always use the BP-level
events above.

### Deferred manual-entry capture (avoid PE re-entrance)

When the manual-join button BndEvt fires, we cannot call `GetText` +
`Conv_TextToString` from inside the post-hook — that re-enters ProcessEvent
from inside ProcessEvent (documented reentrancy hazard).

Pattern: stash the click context + `isLocal` flag in `m_pendingManualJoinWidget`,
then `tickSessionHistoryDeferredCapture()` runs on the next main tick (off the
hook stack) and reads field text safely.

## Right-click delete + GenericPopup confirmation

**Popup class:** `/Game/UI/PopUp/WBP_UI_GenericPopup.WBP_UI_GenericPopup_C`
(note `PopUp` casing — discovered via the runtime PopupTracker, not by guessing).

**Spawn:**
1. `jw_createGameWidget(popupCls)`
2. `AddToViewport(ZOrder=500)`
3. `OnShowWithTwoButtons(Title, Message, "Delete", "Cancel")`
4. Track in `m_pendingDeletePopup`

**Click detection:** Hook UFunctions whose name contains `OnButtonReleasedEvent`
(the actual click event, NOT `OnMenuButtonClicked`). Compare context to
`popup.ConfirmButton` and `popup.CancelButton` member pointers via
`GetValuePtrByPropertyNameInChain<UObject*>`. Match → execute action immediately,
then call popup's `Hide` UFunction + `deferRemoveWidget` to close it (the BP's
own subscriptions don't fire when the popup is spawned standalone).

**DON'T poll `IsInViewport`** — every call is a ProcessEvent invocation;
spamming interferes with input dispatch and was the reason an earlier popup
implementation didn't respond to clicks at all.

### Right-click hit-testing

Sample each row's `IsHovered()` UFunction (and the inner `SelectButton`'s as
fallback) **continuously** at 100ms cadence. Sticky-keep the last hovered row
in static variables — UE4 clears hover state on RMB-down, so reading it AT
release returns false.

Polling budget: 15 rows × 10 Hz = 150 ProcessEvent calls/sec. Acceptable;
earlier 60 Hz polling pushed 900/sec which was at the edge.

## ProcessEvent budget — rules of thumb

ProcessEvent is expensive (FName lookup, parm marshalling, virtual dispatch)
AND shares a scheduler with input handling. Patterns that fire it more than
~10/sec on hot paths can silently break UI input.

- Hot per-tick paths: throttle to ≤10 ProcessEvent calls/sec total. Use
  `static ULONGLONG s_lastSampleMs; if ((nowMs - s_lastSampleMs) >= 100)`.
- Never poll for an event that has a hook — hook the event instead.
- VLOG argument expressions inside post-hooks: gate the entire diagnostic
  block behind `s_verbose` AND filter cheap fn-name checks BEFORE expensive
  work (Outer-chain walks, `safeClassName()`).

## Visual marker

All mod-modified TextBlocks are tinted bright sky-blue (RGB linear `0.35, 0.65, 1.0`).
Per-screen targets:

| Screen | TextBlocks tinted |
|---|---|
| JoinWorld | TextBlock_63 (WORLD SELECTION), Title (JOIN OTHER WORLD), InviteCodeLabel |
| AdvancedJoinOptions | TextBlock_68 (description), TextBlock_180/_1/_2/_3 (4 field labels), JoinErrorText |

## When extending this pattern to a new screen

Per-screen ` .inl` is the right granularity. Naming convention:
- `moria_<screen>_ui.inl` — modify a specific game screen
- `moria_<feature>_storage.inl` — JSON-backed mod data
- `moria_<feature>.inl` — cross-cutting features (right-click menus etc.)

After the third UI take-over, consider hoisting common helpers
(`applyModifications` boilerplate, `m_modWidget=m_nativeWidget` pattern) into a
shared `moria_ui_takeover.inl`.

## See also

- `~/.claude/skills/ue4-ui-duplication/SKILL.md` — global pattern reference
- `~/.claude/projects/.../memory/joinworld-ui-takeover.md` — implementation state
- `dumps/CXXHeaderDump/Moria.hpp` — `FMorConnectionHistoryItem`,
  `EMorConnectionType`, `UMorGameSessionManager` definitions
- `dumps/CXXHeaderDump/WBP_UI_GenericPopup.hpp` — Confirm/Cancel popup interface
- `dumps/CXXHeaderDump/WBP_UI_SessionHistoryList.hpp` — `AddSessionHistoryItem`
- `dumps/CXXHeaderDump/WBP_UI_SessionHistory_Item.hpp` — `ConnectionHistoryItemData`
