# Khazad-dûm Advanced Builder Pack v8.8.0 — "Porter's Bell" **(BETA)**

> **This is a beta release.** The multiplayer goat interactions below are
> new and under active testing — feedback welcome, especially from
> multi-player (multi-dwarf) games. If anything misbehaves, v8.7.1 remains
> available as the stable release.

**Requires:** Tobi Ichiro's combined SoKd-Goat pack (or Secrets of
Khazad-dûm v4.3.6 + Goat-v1.15.0+).

## Multiplayer: the goat is now truly shared

Rûdh is one goat for the whole company — one identity, one set of
saddlebags — and every player can now see and use him:

- **Visible to everyone.** Summons now run through the game's own server
  calls instead of local spawning — the goat exists for all players, not
  just whoever rang the bell. (This was the cause of "only the summoner
  can see the goat.")
- **One-ring hand-off.** Ring while the goat is elsewhere — with another
  player, across the map — and he's automatically brought to *you*
  ("Rûdh is coming to you"). Ring while he's at your side and you dismiss
  him back to the Delving. No double-ringing, no fighting over him: the
  bell reads your intent from where the goat is.
- **Same goat, same cargo, every swap.** Hand-offs and dismissals ride the
  game's native NPC persistence — identity and full saddlebag inventory
  survive every exchange between players (and every reload).
- **Dwarf-style assign/unassign.** The bell now works exactly like
  assigning/unassigning a dwarf at a Delving: dismissing un-assigns the
  goat (he steps out of the world; his packs are safe), summoning
  re-assigns him and brings him over.
- First-time goat creation is done by the host; after that, any player's
  bell works both directions.

## Where the bell works

- **Expeditions:** goat summons are blocked ("Goat cannot hear you").
- **No-craft surface zones** (The Dimrill Dale, Hollin, and any zone where
  building is forbidden): summons are blocked with the same message —
  detected from the game's own zone rules, not a hard-coded list.
- **No Delving yet:** "Rûdh needs a Delving" — the goat needs a rally
  stone anchor to persist, so the bell won't create him without one.

## Fixes

- **Goat role/name no longer reverts to "Citizen".** The Porter role is
  now asserted through the game's own role system every session (and
  replicates to all players in multiplayer). This also fixes the goat
  quietly refusing to follow after the label flipped.
- **Interaction menu consistency:** the goat's storage row now always
  reads **"Equip"** (it previously alternated between "Saddlebags" and
  "Equip" depending on state). Same action either way: opens the
  saddlebags.

## Known issues (beta)

- **Gamepad:** items moved into the saddlebags with a controller may not
  appear in the grid until reopened (mouse drag is unaffected; "Take All"
  always recovers items). Diagnosis is instrumented in this build and a
  fix is in progress.
- The hand-off re-summon fires ~2.5s after the dismissal; on a heavily
  loaded host the goat may occasionally need a second ring.
