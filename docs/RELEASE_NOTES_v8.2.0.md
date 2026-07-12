# Khazad-dûm Advanced Builder Pack v8.2.0 — "Pre-Goat Release"

**Release date:** 2026-07-12
**Covers:** everything since the last shipped release, v7.0.2 (2026-05-12)

---

## Highlights

### Performance: background scanning removed
Players reported gameplay stutter from the mod's background work. This release removes it:

- **Automatic NPC-recovery scanning is gone.** The mod no longer runs day-cycle scans, character-load scans, recurring post-load sweeps, or the 5-second NPC controller cache refresh.
- **New "Unstuck NPCs" key (default `-`, remappable).** One press runs one complete pass: every settlement dwarf stuck in a `CantReach*` state (blocked from bed, furnace, workstation, …) is teleported to its intended destination — same proven pipeline as before, now only when you ask for it. An on-screen summary reports how many NPCs were found and moved.
- **New "Toggle Advanced Builder" key (default `=`, remappable).** The entire Advanced Builder subsystem — building bar, F1–F8 quick build, builder hotkeys, build-menu priming — now starts **OFF** every session and consumes no processing until you switch it on. Press again to switch it off.

Both keys appear in **Settings → Key Mapping → Mod Key Bindings** and persist in `MoriaCppMod.ini`.

### Quick Build keybinding correctness
F1–F8 now strictly honor the keys assigned in the menu. Previously, rebinding a Quick Build slot left **both** the new key and the original F-key active; now only the assigned key fires.

### NPC stuck-pathing recovery (the v7.1.0 line, now on-demand)
Developed across v7.1.0-rc.1 → rc.45 and shipped here under the new manual key:
- Detects NPCs whose activity is any `CantReach*` state (substring match — future variants included).
- Destination resolution chain: cached path destination → FSM behavior-tree walk (the NPC's actual intended target) → activity-aware fallback (nearest furnace/forge for furnace activities, assigned/nearest unoccupied bed otherwise).
- Teleports land 75 cm outside the target's collision along the approach vector, via `K2_TeleportTo` (movement-component-aware — survives nav re-anchoring).
- Manual passes bypass the per-NPC 30-second throttle.

### Porter Goat companion — groundwork (why "Pre-Goat")
Extensive foundation for the goat companion (built in collaboration with Tobi's *Secrets of Khazad-dûm* mod), shipping disabled-by-default / dev-gated in this release:
- Bell summon / dismiss / recall with a presence-based state machine (goat visibly leaves and returns; cargo and identity preserved).
- 8×8 saddlebag storage UI (chest-mode takeover of the game's storage screen).
- Goat identity persistence across save/reload (GUID + NPC registry + unique-NPC row, auto-restore on load, duplicate cleanup).
- Follow/Stay via a 1 Hz movement drive (instrumentation proved the goat never self-walks; the game's native escort catch-up teleport is its only built-in mover).
- Saddlebag contents persist by living in the player inventory (native character save).
- Fixed: suppressing the game's storage screen no longer breaks world chests for the rest of the session (visibility-collapse instead of viewport removal).

### Other fixes and changes since v7.0.2
- **Reveal Map** action in the pause menu (added during the v7.1.0 line).
- BuildHUD visibility is event-driven (no polling).
- Crash fix: SEH guards in the HISM bubble tracker during world reloads (stale-pointer access on fast world transitions).
- Save Game keybind (default F12) — trigger a manual save any time.
- All hardcoded developer/diagnostic keys (numpad probes, widget-harvest key) disabled; the only inputs the mod reads are the remappable bindings plus context keys inside its own dialogs.
- "Mereak's Secrets Changes" content fully removed from the shipping product (v7.0.2 follow-through).
- Code-review cleanup pass: dead code removal, reflective offsets everywhere (no hardcoded blueprint offsets).

---

## Keybind reference (defaults)

| Key | Action |
|---|---|
| `F1`–`F8` | Quick Build slots (requires Advanced Builder ON) |
| `=` | Toggle Advanced Builder (starts OFF each session) |
| `-` | Unstuck NPCs (one recovery pass) |
| `F9` | Rotation, `[` Snap, `]` Target, `Num1/2/3` Remove/Undo/Remove-All (Advanced Builder ON) |
| `F12` | Save Game |
| `Del` / `Ins` / `End` | Trash / Replenish / Remove Attributes |
| `.` / `,` | Pitch / Roll rotate |
| `F10` | Reposition HUD |

All remappable in Settings → Key Mapping.

---

## Compatibility
- Return to Moria (UE4.27), Epic Games Store build.
- UE4SS custom build (v4.0.0-rc1 base) bundled by the installer.
- Server package included (`staging-server`) for dedicated servers.
