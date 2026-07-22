# Khazad-dûm Advanced Builder Pack v8.7.1 — "Steadfast Porter"

Maintenance release of v8.7.0, verified against Tobi Ichiro's **combined
SoKd-Goat pack** (single download containing Secrets of Khazad-dûm and the
Porter Goat module together).

**Requires:** Tobi Ichiro's combined SoKd-Goat pack (or Secrets of
Khazad-dûm v4.3.6 + Goat-v1.15.0 installed separately).

## Changes

- **Fixed: NPC and goat inventory reverting to 4×3.** After switching to
  the combined SoKd-Goat pack, the inventory screen could fall back to
  the base game's 4×3 grid. The 6×6 inventory is restored — this
  installer ships the 6×6 NPC inventory screen (widget override), which
  together with the combined pak's 6×6 capacity gives dwarves and the
  goat the full 36-slot grid again.
- Compatibility validation for the combined SoKd-Goat pack — the goat's
  native inventory configuration and the 6×6 grid capacity were verified
  present and correct in the combined pak.
- No other functional changes since v8.7.0. See
  [RELEASE_NOTES_v8.7.0.md](RELEASE_NOTES_v8.7.0.md) for the full
  "Steadfast Porter" feature set: settled-goat native persistence,
  native recall bell (Delving-gated), native stay/follow, menu-reload
  reliability fix, 6×6 NPC inventory.

## Install order

1. Tobi's combined SoKd-Goat pack into `Moria/Content/Paks/`
2. This installer (includes the 6×6 NPC inventory widget override and
   the UE4SS mod)
