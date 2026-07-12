# Porter Goat — Follow / Stay Architecture (rc.71, 2026-07-03)

Status: **WORKING** (user-verified with Tobi SecretsOfKhazadDum Goat-v1.10.0 pak).

## Ownership split (hybrid pak + DLL)

| Piece | Owner | Detail |
|-------|-------|--------|
| E-menu rows ("Follow / Stay", "Manage") | **Tobi's pak** | Repurposed MorNPC interaction slots; supplies row labels |
| Click detection & dispatch | **Our DLL** | Reads the row's `InteractionText`, routes on label |
| `stayMode` state | Our DLL | Toggled by `onGoatFollow` / `onGoatStay` |
| Goat movement | Our DLL | `tickFollowGoats` physically drives the goat |
| Goat tracking (`m_followGoats`) | Our DLL | Populated by `[BellSpawn]` or `adoptNativeGoat` scan |

## Movement mechanism — MoveToActor, NOT LeashActor

**Critical, hard-won fact:** Tobi's v1.10.0 `Bst_NPCGoatWorkPorter` behavior does **NOT** follow the `LeashActor` blackboard key.

Runtime proof (instrumented `setGoatLeashActor`):
```
[Leash] SET LeashActor=player on ctrl='BP_NpcGoat_AIController_C' bbProp='Blackboard' PE_ok=true
```
We set the key successfully every second — the goat still never moved. The behavior ignores it.

### What actually works (restored from pre-rc.61)

In `tickFollowGoats`, per goat, at 1 Hz:
- **Follow** (`!stayMode`): `AAIController::MoveToActor(Goal=player, AcceptanceRadius=250, bUsePathfinding=true)`
- **Stay** (`stayMode`): `AAIController::StopMovement()` each tick to hold position

Params are set by name via `findParam` (offsets vary by build). Controller = `BP_NpcGoat_AIController_C`, resolved from `APawn::Controller`. `pawn` = local player pawn (`m_localPawn`).

The `LeashActor` write is left in place as a harmless no-op, in case a future Tobi build wires the behavior to consume it.

## History / do-not-repeat

- **rc.61 (June 28)** removed the manual `MoveToActor` drive on the directive *"use the Goat AI alone."* That directive assumed Tobi's AI would follow on its own. **Runtime logging (rc.71) disproved it.** Do not remove the `MoveToActor` drive again — it is the only thing that moves the goat.
- Follow/Stay "worked a week ago" precisely because the pre-rc.61 `MoveToActor` drive was still active.

## Native-goat adoption (secondary)

Tobi's GA_Bell summons the goat outside our spawn path, leaving `m_followGoats` empty (which would make every menu handler early-return). `tickAdoptNativeGoat()` — a 2s throttled `seh_findAnyGoatActor` scan strictly filtered to `BP_NpcGoat_C` — adopts it with all one-shot mutation flags PRE-SET so the tick skips wild-fauna surgery and only runs the drive. In practice the `[BellSpawn]` path usually tracks it first, so adoption no-ops.

## Key symbols

- `tickFollowGoats()` — the movement driver (moria_goat.inl)
- `onGoatFollow()` / `onGoatStay()` — set `stayMode`, toast
- `adoptNativeGoat()` / `tickAdoptNativeGoat()` — native-goat fallback tracking
- `setGoatLeashActor()` — instrumented; proven no-op on v1.10.0
- Menu dispatch: label match on `"Follow"` → toggle, `"Saddlebag"` → openGoatSaddlebagInventory
