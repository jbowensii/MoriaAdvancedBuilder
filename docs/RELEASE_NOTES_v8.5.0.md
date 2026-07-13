# Khazad-dûm Advanced Builder Pack v8.5.0 — "Porter Goat"

**Release date:** 2026-07-13
**Covers:** everything since v8.2.1 "Ephemeral Goat" (2026-07-13)

---

## Highlights

### Porter Goat companion — feature complete (verified in play)
The goat companion now works end to end with Tobi's *Secrets of Khazad-dûm* mod:

- **Ring the bell with a left-click.** Equip the Bell of the Goat in your hand and left-click to ring it — goat appears in front of you and follows. Ring again: it leaves. Ring again: a new goat, instantly (using it from the hotbar still works too).
- **Reliable re-summon.** Fixed the case where ringing after a dismissal did nothing until the engine's garbage collector ran — dismissed goats are now filtered out the moment they're destroyed.
- **Saddlebags** (crafted at a Workbench) live in your character's inventory and persist natively through every save/reload. Open the goat's Saddlebags menu row for the 8×8 storage with full mouse drag-and-drop.
- The goat keeps pace, never wanders, and needs no upkeep — it exists to follow you and carry your view of the bags.

### Advanced Builder & HUD fixes
- **F10 (Reposition HUD) restores the Advanced Builder UI**: it activates the builder subsystem, shows the building bar, message window, rotation circles, and inspect window, and lets you drag the inspect window and rotation circles. F10/Esc exits.
- Fixed the rotation circles and building bar going permanently missing after reloading a world mid-session (stale one-shot widget spawners now reset on world unload).
- Rotation circles show during building only while the Advanced Builder toggle (`=`) is on; F10's reposition mode shows them regardless.

### Logging note
With verbose logging enabled you may see repeating `NpcId Invalid` lines from the game while the goat is out — the game's NPC manager querying our deliberately-unregistered companion. It is cosmetic and has no gameplay performance impact.

---

## Compatibility
- Return to Moria (UE4.27), Epic Games Store build.
- Goat features require Tobi's *Secrets of Khazad-dûm* mod (bell + saddlebags recipes; craft both at a Workbench).
- Server package included for dedicated servers.
