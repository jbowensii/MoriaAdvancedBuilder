# Khazad-dûm Advanced Builder Pack — v8.6.0 "Clean Forge"

**Date:** 2026-07-13
**Scope:** everything since the v8.5.0 release (includes the local-only v8.5.1 documentation pass).

This is a hardening and cleanup release: a full code review was executed end-to-end, ~14,000 lines of dead code from the Porter Goat development pivots were removed, and two long-standing bugs — the quick-build "no ghost" failure and the bell going dead until restart — were root-caused from logs and fixed.

---

## Fixed

### Quick Build: recipe fires but no ghost appears (long-standing, intermittent)
After assigning a slot (SHIFT+F1) and pressing F1, the mod sometimes showed its "Build: …" toast but the game never spawned the placement ghost. Root cause: the build menu's tile widgets are pooled/recycled — a tile can pass the name match while holding an **empty** recipe block, and selecting with an empty block is a silent no-op in the game. The mod now prefers a tile whose block actually carries recipe variants, and falls back to the known-good block captured at assignment time when the fresh one is empty. This failure had recurred for many versions because it depended on which pooled tile the scan found first.

### Goat bell dead until game restart
If the goat class was garbage-collected between character load and the first bell ring (a ~2-minute idle window), every ring failed silently until restart. The staleness check was fooled by recycled memory that decoded to a garbage-but-non-empty name. The cached class must now match an expected goat class name exactly; a mismatch triggers a clean re-resolve and the spawn recovers.

### Retired "F12 settings" references
The old F12 configuration menu is fully retired — all mod configuration lives in the native **Settings** screen (Key Mapping, Gameplay, cheats/tweaks) and **F12 is the Save Game keybind**. User-facing messages that still pointed at "F12 settings" now say "Settings."

## Code review (full pass, all tiers)

- **Bitfield-safe writes:** every raw boolean property write now goes through the mask-correct `FBoolProperty` path — safe if the game ever packs those flags.
- **Reflective offsets everywhere:** recipe/unlock rows, entitlement (DLC) handle arrays, session-history fields, inventory container sizes, and FItemInstance fields are all resolved from the game's own reflection data at runtime, with the previous literals kept as fallbacks. Game patches that shift struct layouts no longer corrupt data.
- **Stale-pointer hardening:** widget and asset-pointer caches (Join World clone captures, building-bar textures, settings classes, function cache) are reset on world unload and re-harvested on next use; deferred widget removals hold weak pointers; player-controller and inventory pointers are alive-checked before use.
- **Performance:** the saddlebag screen's widget-tree walks now cache resolved functions per class (was ~5–7k lookups/sec while open).
- **UTF-8 correctness:** all byte-mangling string conversions replaced (the class of bug that once corrupted the goat's name "Rûdh" can no longer reach logs or files).

## Removed (dead code purge, ~14,000 lines)

- The entire legacy F12 config panel (~2,600 lines) — superseded by the Settings-screen integration.
- Porter Goat development leftovers: the unreachable registration/identity spawn tail, the retired custom goat menu/submenu, old follow/adoption experiments, the runProbe diagnostic suite, and the saddlebag experiments' legacy paths (~9,400 lines).
- The retired automatic NPC-recovery scan loop ("Unstuck NPCs" keybind is the supported path).
- All `#if 0` archives and commented-out code — git history is the archive.
- The whole source tree is now clang-formatted against the repo style.

## Unchanged / kept

- Ephemeral goat design: bell summons/dismisses; the saddlebag pack in your character inventory is the only persistent state.
- Keybinds: `=` toggles Advanced Builder (starts off each session), `-` runs Unstuck NPCs, F12 saves, F1–F8 quick build with SHIFT+F# assign.
- The `[GoatSaveProbes]` INI-gated diagnostics remain available for future research.

## Install

Run `KhazadDumAdvancedBuilderPack_v8.6.0_Setup.exe` (signed). Compatible with Tobi's SecretsOfKhazadDum 4.3.3 base + Goat-v1.12.0.
