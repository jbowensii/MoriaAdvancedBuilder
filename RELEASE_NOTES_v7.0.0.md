# Khazad-dum Advanced Builder Pack v7.0.0 — Release Notes

The first major release since v6.7.0 (May 2026). Six months of feature work,
124 commits, and a comprehensive cleanup pass that landed a stable, well-
tested base for the 7.x line.

---

## Headline Features

### Settings UI Take-Over (Option-C merge)
Mod content is now injected directly into the game's native Settings screen
rather than rendered through a separate F12 panel.

* **Key Bindings tab** — 24 mod keybinds appear in two new sections under
  the native keymap:
    * **Inventory and Items** — 16 Quick Build entries (F1–F8 USE chord +
      SHIFT+F1–F8 SET chord)
    * **Mod Key Bindings** — 9 mod-action entries (Snap, Integrity Check,
      Invisible Dwarf, Flying Dwarf, Target, Duplicate Target, Remove Single,
      Undo Remove, Remove All, Reposition HUD)
* **Gameplay tab** — Mod Game Options merged into the native Gameplay tab
  alongside vanilla settings. Buff toggles and tweak cycles render as native
  carousels and checkboxes; mod packs render as togglable rows. Persistence
  via `[Cheats]` and `[Tweaks]` sections in `MoriaCppMod.ini`.
* In-game keymap rebinds capture every chord including `Ctrl+F1`, `Alt+G`,
  `DEL`, etc. Bare-modifier captures are detected and dropped so chord rebinds
  don't lock onto a stray Shift press.
* The legacy F12 dev panel is `#if 0`'d out; `F12` is now reusable as the
  default Save Game keybind.

### Native Pause-Menu Mod Block
`UI_WBP_EscapeMenu2_C` (the in-game pause menu) gets a small mod-action block:

* **UNLOCK RECIPES** — Iterates recipe DataTables, filters
  protected/hidden/unfinished/DLC-gated rows, paces `DiscoverRecipe` calls
  across frames (50/frame) to avoid `FFastArraySerializer` burst-flood.
* **READ ALL LORE** — One ProcessEvent call on
  `WBP_LoreScreen_v2_C::MarkAllRead` (Blueprint-native).
* **CLEAR ALL BUFFS** — Resets every Cheats-tab buff toggle to OFF.

Buttons sit above LeaveButton/QuitToDesktop, with proper spacers and
SmallText sizing matching the native LEAVE/SETTINGS/FREE CAM rows.

### New Building Bar (NBB)
A from-scratch top-of-screen builder bar that replaces the legacy MC, AB,
and UMG QuickBuild toolbars (all three retired in v6.21.x).

* Visual chrome cloned from `WBP_UI_ActionBar_C` (the native HUD ActionBar).
* 8 numbered slots (F1–F8). Active slot highlighted via the BP's own
  focused-state image.
* Slot icons populated from `m_recipeSlots[i].textureName` (the user's
  QuickBuild assignments persisted in `[Quickbuilds]`).
* F# key labels rendered on small grey rect backgrounds (NBB-toolbar style).
  Labels track live keybind state — rebinding a slot from F1→Q updates the
  label without restart.
* ZOrder 100 so the bar stays above the game's main HUD.

### Native Character Rename Modal Hijack
SAVE GAME / RENAME / etc. previously used a home-rolled UMG dialog. That
~500 LOC of UI code is replaced with patches to the game's own
`WBP_CharacterCreatorRenameDialog_C`:

* **MaxNameLength bumped to 22** (BP CDO ships 12). Re-applied on every
  `OnAfterShow` because the BP can re-construct the instance from CDO between
  shows.
* **DisallowedWords cleared** so legitimate names aren't blocked.
* `CheckNewCharacterName` reads `this.MaxNameLength` (instance member
  inherited from native parent `UMorCharacterCreatorRenameDialog`), so writing
  on the instance is enough — no CDO patching, no pak mod, no asset edit.

### Save Game Keybind
`BIND_SAVE_GAME` (default F12). Edge-triggered; suppressed while Settings UI
or rename popup is open so a stray F12 in those contexts doesn't accidentally
save.

### Reposition HUD Mode (F10)
Toggles a dedicated reposition mode for the rotation display + inspect
window:

* First press shows both widgets and lets the user drag them with the mouse.
* Drag any cell of the rotation pyramid OR the inspect window's title bar.
* Position fractions persist to `[Positions]` in `MoriaCppMod.ini`.
* Second F10 press OR ESC exits and returns visibility to normal rules.

### Rotation Display Pyramid
A 4-cell pyramid (1 top + 3 below) showing live rotation state of the
active build piece:

* **Top:** F9 step (5°–90°)
* **Bottom row left:** Yaw (game's native Rotate Construction action — read
  from PlayerInput.ActionMappings, default `R`)
* **Bottom row mid:** Pitch (BIND_PITCH_ROTATE)
* **Bottom row right:** Roll (BIND_ROLL_ROTATE)

Each cell shows two centered lines (label + value) plus a small NBB-style
key-marker pill at the bottom. Throttled to 4 Hz; visible only during
placement OR while in F10 reposition mode.

### Inspect Window (right-click target → SHIFT+])
Built as a draggable popup-style chrome panel (matches `WBP_UI_GenericPopup`
look) with a title bar and X close button. Auto-hides 10 seconds after the
last show update; auto-hide is suppressed during F10 reposition.

* SHIFT+] always does a fresh line-trace at press time so it uses current
  aim, not the stale buffer from a previous `]` inspect.
* showUI=false flag suppresses popup + 'no hit' message + actor_dump.txt
  write for the auto-inspect path.

### Cheats Tab + Tweaks Tab
Buff toggles (God Mode, ring effects, illuminate, etc.) and tweak cycles
(spawn rates, damage multipliers, etc.) injected as native carousels and
checkboxes inside the Gameplay tab. State persists across world reloads via
`[Cheats]` and `[Tweaks]` sections in `MoriaCppMod.ini`.

---

## Reliability and Stability

### Multiplayer Reliability
* `ServerDebugSetItem` (Server RPC) replaces `RequestAddItem` (local-only)
  for INS replenish — works on non-host clients.
* Server-fly sweep runs every 2s on dedicated and listen servers; sets
  `bIgnoreClientMovementErrorChecksAndCorrection` and
  `bServerAcceptClientAuthoritativePosition` on every authoritative dwarf.
* All MP-relevant hooks gate on `bIsLocalPlayerController`; capture/dispatch
  is per-client only.
* Dedicated-server detection (no GameViewport) skips all UI creation.

### Q3 Offset Reflection (v6.23.11–v6.23.14)
Five game-data structs migrated from hardcoded byte offsets to runtime
reflection-resolved sentinels. Future game patches that re-pack any of these
structs no longer silently break the mod:

* **FActiveItemEffect** — Effects.List inner walk for replenish + remove-attrs
* **FMorConnectionHistoryItem** — 88-byte session-history row stamping
* **FFGKUITab** — 0xE8-byte settings-screen tab navbar entries (3 sites)

Plus engine-frozen sanity checks for `FInputChord` and
`FInputActionKeyMapping` — UE4.27 is frozen so no full migration, but a
one-shot reflection probe at first use confirms the size matches the
hardcoded constant. If a future engine somehow re-packs either struct, the
mismatch logs loudly via VLOG instead of silently corrupting memory.

### Validation Audit (v6.23.10)
Three drift-site fixes from a comprehensive handle/offset validation pass:

* **callAddRowInternal** — SEH-wrapped vtable call + isObjectAlive precheck
  on cached `UDataTable*` (was vulnerable to use-after-GC).
* **bLock memcpy in placement** — `isReadableMemory` gates on the
  `s_off_bLock` lookup before stamping.
* **HISM replay queue** — `comp.Get()` now isObjectAlive-checked in addition
  to the FWeakObjectPtr serial check (catches mid-BeginDestroy during world
  transition replay).

### Brush Write Defensive Padding (v6.23.15)
The four FSlateBrush ImageSize.X/Y direct-write triples in
`moria_widgets.inl` now check `isReadableMemory(base + s_off_brush, 16)`
before stamping. Belt-and-suspenders against a freshly-constructed widget
whose vtable / property layout is somehow unreadable.

### DataTableUtil Unbind on World Transition (v6.23.16)
Nine cached `DataTableUtil` instances (`m_dtConstructions`, `m_dtItems`,
`m_dtWeapons`, etc.) now call `unbind()` in the world-unload reset block.
Closes the stale-pointer crash window where a cached `UDataTable*` could
outlive its row map across a world transition.

### Slate Prepass AV Root-Cause Fix (v6.21.39)
The rotation display widget is now pre-spawned at character-load instead of
lazily created on the first placement-active tick. The lazy-create path was
the root cause of `SWidget::Prepass_Internal` AVs at `0xFF...` during
quickbuild — widget construction was racing the Slate frame that ghost spawn
lands on. Toggling visibility on a fully-realized widget is safe.

### INS Replenish Multi-Stack Fix (v6.23.2 → v6.23.5)
`ServerDebugSetItem` is class-level, not instance-level. The replenish path
now walks Items first to find the targeted instance and writes its
`Count` directly (offset 0x18). Stacks are NOT combined — only the targeted
stack increases to max.

### Stability Hardening
* `safeClassName` SEH-wrapped (protects every PE pre-hook against malformed
  context).
* `safeClassName` hoisted in `OnAfterShow` / `OnAfterHide` so each PE post-hook
  resolves the class name once, not three times.
* `FindAllOf` calls routed through `seh_findAllOf` where applicable.
* `dwmapi.dll` proxy from build artifact (not `zDEV/`).

---

## Cleanup Pass

### Dead Code Removal (v6.22.x → v6.23.0)
Comment-out-before-delete discipline. v6.22.1 through v6.22.5 wrapped each
dead cluster in `#if 0` shipped to GitHub for one full session of testing,
then v6.23.0 hard-deleted ~1,553 LOC across 5 files:

* DualSenseReader cluster (~270 LOC)
* Controller dispatch loop + 13 PE hooks + state fields (~720 LOC)
* FGK runtime-injection diagnostics
* MoriaJoinAssets namespace placeholder
* AdvancedJoin captures
* Settings cheats cluster

Plus removed:
* Legacy MC bar widget infrastructure (~700 LOC, v6.21.19)
* Advanced Builder bar (~510 LOC, v6.21.15)
* Legacy `showRenameDialog` UI (~322 LOC, v6.21.24)
* Reposition-mode toolbar drag (~470 LOC, v6.21.8)
* OLD UMG QuickBuild bar code (v6.21.5)

### Comment Audit (v6.23.17 → v6.23.26)
9 supervised iterations covering all 4 audit files. ~750 LOC of stale
tombstones, WHAT-comments, dev-scaffolding, investigation prose, and
version-stamp rot eliminated. Diagnostics gated behind `s_verbose` (silent
in release, lit in verbose mode):

* Navbar UFunction discovery dump
* CP4-CLICK click trace
* Native KeySelector internal-tree dump
* Vibration CheckIcon comparison dump
* EditableTextBox property dump
* Replay-Diag mesh-id dump

---

## Bug Fixes Worth Calling Out

* **Trash popup cursor-stuck bug** — Don't change input mode if cursor was
  already visible before popup opened.
* **`applyAddRow` heap leak** — Definition-pack re-apply was leaking
  reflection-allocated row buffers.
* **Pitch/Roll keybind dispatch** — Was previously gated on
  `if (m_mcBarWidget)` — ungated so keypresses fire `dispatchMcSlot`
  regardless of the legacy MC toolbar widget.
* **Handle-resolution priming** — Was nested inside the disabled AB-bar
  block, blocking F1–F8 USE, chord polling for SET, and every keybind that
  gates on `HandleResolvePhase::Done`. Hoisted out so it runs once per
  character-load regardless of toolbar state.
* **NBB at 4K resolutions** — Slot icons + key labels render correctly at
  the game's full DPI scale curve.
* **FFastArraySerializer.List inner offset** — `0x0110` is engine-stable;
  the interesting layout is the inner `FActiveItemEffect`, which now resolves
  via reflection.
* **Steam ™ folder corruption** — UE4SS working dir conversion uses
  `WideCharToMultiByte(CP_UTF8)`. Never `static_cast<char>` on `wchar_t` —
  U+2122 (™) collapses to 0x22 (") and silently corrupts every file path.

---

## Off-Limits / Deferred

* **C4996 deprecation warnings** — UE4SS is migrating away from
  `ForEachProperty`, `FNameEntryId::operator uint32`, the 3 metadata-less
  Hook callbacks, and `TArray::SetNum(bool)`. Migration is semantic, not
  syntactic — risks runtime bugs for zero current functional gain. Build
  emits the warnings; they're documented and ignored.
* **Recipe row offsets in `moria_unlock.inl`** — Marked KNOWN P0 by the
  Q3 audit. Too risky for autonomous fix — wrong migration could corrupt
  recipe data and affect save game state. Needs a separate dedicated
  investigation iteration with explicit user supervision.
* **FGK runtime DataTable injection** — Abandoned 2026-05-04 after exhausting
  the available paths. UE.Toolkit (Reloaded II) is the viable external
  alternative.

---

## Migration Notes

### Settings INI (`MoriaCppMod.ini`)
New sections introduced since v6.7.0:

* `[Cheats]` — Peace Mode + buff toggles. Only "true" entries written.
* `[Tweaks]` — Tweak cycle indices. Only non-default (index > 0) entries
  written.
* `[Positions]` — `RotDisplayX/Y` for the rotation display widget drag
  position.
* `[KeybindingsSet]` — `QuickBuild<N>Set = [SHIFT+|CTRL+|ALT+]<key>` for
  Quick Build SET chords (the rebind that flips a slot's recipe assignment).
* `BIND_SAVE_GAME` (default F12) — Save Game keybind.
* `BIND_REPOSITION_HUD` (default F10) — Reposition HUD mode toggle.

### Behavioral Changes
* **F12 is no longer the dev panel toggle.** It's the default Save Game
  keybind. The dev panel is `#if 0`'d out.
* **The MC bar, AB bar, and OLD UMG QuickBuild bar are all gone.** The New
  Building Bar is the sole toolbar.
* **Mod content lives in the native Settings screen.** The F12 dev panel is
  retired; mod keybinds are in Settings → Key Mapping; mod cheats/tweaks are
  in Settings → Gameplay; the pause-menu mod block is in
  `UI_WBP_EscapeMenu2_C`.
* **Replenish (INS) only refills the targeted stack to max.** It does not
  combine multiple stacks of the same item.

---

## Tag Inventory (v6.7.0 → v7.0.0)

124 tagged commits. Per CLAUDE.md memory, every iteration is its own GitHub
tag for surgical rollback. Highlights:

* `v6.10.0` — Settings take-over Phase 1 (keymap injection).
* `v6.20.x` — Inspect window restyle, rotation display widget,
  notification feed abandonment, lore persistence.
* `v6.21.x` — New Building Bar polish, native rename modal hijack,
  reposition HUD mode, Save Game keybind, big cleanup batches.
* `v6.22.x` — Comment-out-before-delete sweep (3 dead clusters wrapped
  in `#if 0`).
* `v6.23.0` — Hard-delete pass (~1,553 LOC of confirmed-dead code gone).
* `v6.23.1–v6.23.5` — Trash popup + INS replenish bug fixes.
* `v6.23.6–v6.23.9` — Settings_ui orphan-body cleanup, KB regen,
  comment-audit sweep, EXPAND comments.
* `v6.23.10` — Validation-audit drift fixes.
* `v6.23.11–v6.23.14` — Q3 reflection migration (FActiveItemEffect /
  FMorConnectionHistoryItem / FFGKUITab) + engine-frozen sanity checks.
* `v6.23.15–v6.23.16` — Brush-write padding + DataTableUtil unbind.
* `v6.23.17–v6.23.26` — Comment audit (9 iterations, ~750 LOC removed).

---

## Test Coverage

286 unit tests across 5 test files passing on every build:

* `test_file_io.cpp` — `modPath()` + UTF-8 path resolution
* `test_key_helpers.cpp` — keybind parsing, modifier chord encoding
* `test_loc.cpp` — localization (string lookup)
* `test_memory.cpp` — `isReadableMemory` + alive-check semantics
* `test_string_helpers.cpp` — wide/UTF-8 conversion, sanitizers

---

## Compatibility

* **Game:** Return to Moria (Lord of the Rings) by Free Range Games
* **Engine:** Unreal Engine 4.27
* **UE4SS:** Custom build from main branch (`0bfec09e`, v4.0.0-rc1)
* **GraphicsAPI:** `dx11` (`opengl` crashes the game)
* **`GuiConsoleEnabled` MUST stay 0** — enabling causes
  `DXGI_ERROR_DEVICE_REMOVED` on this game.

---

🤖 Generated with [Claude Code](https://claude.com/claude-code) over a
multi-session collaboration. Co-Authored-By: Claude Opus 4.7 (1M context).
