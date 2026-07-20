# Khazad-dûm Advanced Builder Pack v8.7.0 — "Steadfast Porter"

**Requires:** Secrets of Khazad-dûm v4.3.6 + Porter Goat Goat-v1.15.0 (Tobi Ichiro) or later.

## Porter Goat: persistence, solved

Rûdh the porter goat now survives game reloads **with his full saddlebag
inventory**, using the game's own NPC persistence:

- **Settled-goat architecture** — at registration the goat is automatically
  anchored to your active Delving (native `ServerRescueNpc`) and given the
  Porter role natively (`ServerNpcSetRole`). Settlement membership is what
  keeps an NPC's actor record restorable across saves; the goat now rides
  the exact mechanism the dwarf NPCs use.
- **Native recall bell** — ring with the goat alive: he teleports to you.
  Ring with no goat in the world: the bell *rescues him back from the save
  record* — inventory included — and auto-calls him to your side. A brand
  new goat is spawned only when the roster has none.
- **Delving required** — with no active Delving the bell shows
  "Rûdh needs a Delving" (save-style notification) and does nothing:
  a goat without a settlement anchor could never persist, so we no longer
  create one.
- **Native stay/follow** — Follow and Stay each install a native behavior
  state once (`ReplaceBehaviorState`); no per-tick refresh, no fighting the
  AI. Works with the settlement schedule via the native Porter role.
- **Native inventory** — with Goat-v1.15.0 the goat's 6×6 body inventory
  and equipment slots are built by the game itself; the mod's runtime
  container construction stands down automatically when native containers
  are present.

## Reliability

- **Menu-reload state fix** — reloading a world through the main menu now
  performs the full per-world reset (previously only 4 of ~200 fields
  reset, leaving stale caches that could hang the game on reload and break
  goat systems in the next world).
- Bell CALL teleports the goat to the player's own position (the old
  offset landing could drop him into geometry).
- Corrupt "epic pack" container item eliminated at the source (fixed
  upstream in Goat-v1.14.0+; the mod no longer synthesizes the rows that
  fed it).

## Compatibility

- NPC inventory grid finalized at **6×6** for dwarves and the goat
  (DT capacity now upstream in Tobi's mod; widget override pak included).
- No local byte patches of Tobi's paks are required anymore — clean
  installs of his releases just work.
