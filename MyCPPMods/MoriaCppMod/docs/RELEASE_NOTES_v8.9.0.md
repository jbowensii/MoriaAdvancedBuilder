# Khazad-dûm Advanced Builder Pack v8.9.0 — "Porter's Bell" **(Beta 2)**

Second beta of the multiplayer Porter Goat. Every fix in this release was
diagnosed from live captures of the game's own systems — the bell now does
exactly what the settlement screen does for dwarves, call for call.

**Requires:** Tobi Ichiro's combined SoKd-Goat pack (or Secrets of
Khazad-dûm v4.3.6 + Goat-v1.15.0+).

## The bell, perfected

- **Withdraw and summon now use the game's own settlement moves** —
  transcribed from a live capture of a dwarf being withdrawn and relocated.
  Ring beside Rûdh: he's **withdrawn** ("Rûdh has been withdrawn") — off the
  world, packs safe in the ledger, exactly like an unassigned dwarf. Ring
  again: he's re-assigned and returns — **with his full cargo**.
- **Reliable dismissal.** The goat's actor sometimes lingered after a
  withdrawal (the settlement system cleans up its own dwarves faster than
  our goat). The mod now verifies every dismissal and completes it within
  ~2 seconds when the game dawdles — no more "he wouldn't leave," no more
  dead bell.
- **Membership fixed.** Summons now properly enroll the goat in your
  Delving (the earlier beta could leave him half-registered, which silently
  broke dismissal).
- **Smart arrivals.** Rûdh now appears **behind you** (or on your flank if
  behind is blocked) — never on top of your character. Applies to summons,
  hand-offs, and recalls alike.

## Gamepad saddlebags: fixed

Controller quick-move (RB+A) sent items into an invisible corner of the
goat's storage — they truly transferred (Take All recovered them) but never
showed in the grid. Two-part fix:

- The storage screen is now properly bound to the goat when opened, so
  controller quick-moves and **Take All** target the goat itself (previously
  they could hit a stray drop-bag).
- A normalizer sweeps any item that lands outside the visible bag grid into
  it within a second — gamepad-moved items now appear in the saddlebags
  just like mouse-dropped ones.

## Full localization

Every player-facing message in the mod (about 280 strings — goat, bell,
save, trash, unlock, builder, and all the rest) now lives in
`localization/en.json`. Translating the mod is now: copy `en.json` →
`<lang>.json`, translate the values, set `Language = <lang>` in
`MoriaCppMod.ini`.

## Still in beta

- The **two-player pass** (shared goat visibility, one-ring hand-off between
  players) is implemented but awaiting full multiplayer testing — reports
  welcome.
- Known cosmetic: gamepad-moved items appear in the bag after a ~1s hop
  (the normalizer at work).
