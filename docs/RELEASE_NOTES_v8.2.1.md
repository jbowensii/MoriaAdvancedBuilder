# Khazad-dûm Advanced Builder Pack v8.2.1 — "Ephemeral Goat"

**Release date:** 2026-07-13
**Covers:** everything since v8.2.0 "Pre-Goat Release" (2026-07-12)

---

## Highlights

### Porter Goat companion — simplified and working
The goat companion (used with the *Secrets of Khazad-dûm* mod's craftable Bell and Saddlebags) is now **ephemeral by design**:

- **Bell of the Goat = a simple toggle.** Ring it: a goat appears in front of you and follows. Ring again: it leaves. That's the whole model.
- **Your saddlebags and their contents live in your character's inventory** and persist through every save/reload natively — the goat carries nothing, so dismissing it (or reloading without it) can never lose items.
- Open the goat's **Saddlebags** menu row to access the 8×8 storage; drag and drop with a working mouse cursor (input-mode enforcement fixed).
- The goat **keeps pace** now (running gait fix — it previously crawled at walking-gait speed) and no longer wanders off "At Ease" (its settlement AI is disabled; it exists to follow you and haul your view of the bags).
- The Follow/Stay menu row is retired — follow is permanent; the bell covers leave/return. (Clicking a leftover row simply re-affirms Follow.)

### Fixes
- **Crash fix:** access violation in the environment-bubble tracker during logout→login transitions (SEH-hardened function lookups).
- **World chests** can no longer be broken by opening the goat storage (screen suppression uses visibility collapse, never viewport removal).
- Storage screen mouse capture is enforced while open — the game's UI manager can no longer revert the cursor to camera control.
- Quick Build F1–F8 honor rebinds strictly (previously both the new key and the original F-key stayed active after a rebind).
- All hidden developer/diagnostic hotkeys (numpad probes, widget-harvest key) are disabled.

### Removed
- Five bundled Game Mods definitions removed from the product: **Durable Armors, Durable Weapons, Durable Tools, Fat Trade, No Fall Damage**.
- Internal file-based saddlebag backup ("sidecar") removed — persistence is fully native.

---

## Compatibility
- Return to Moria (UE4.27), Epic Games Store build.
- Goat features require Tobi's *Secrets of Khazad-dûm* mod (bell + saddlebags recipes; craft both at a Workbench).
- Server package included for dedicated servers.
