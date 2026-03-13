# Stability Audit System

Source: `MoriaCppMod/src/moria_stability.inl`

## Purpose

Scans all player-placed structures via the construction manager's
`AllStabilityComponents` array and visually highlights problem pieces.
Critical structures get red PointLights, marginal ones get amber lights,
and both receive a one-shot Niagara particle burst. Lights auto-clear
after 10 seconds. Triggered from MC toolbar slot 2.

## EStabilityState Enum

Values from `Moria_enums.hpp`:

| Value | Name | Behavior |
|-------|------|----------|
| 0 | Uninitialized | Skipped |
| 1 | Initializing | Skipped |
| 2 | Stable | Counted but not highlighted |
| 3 | Unstable | Always classified as critical |
| 4 | Provisional | Classified as marginal (unless stability <= 20) |
| 5 | Deconstructed | Skipped |

## Classification Thresholds

Each stability component has a `State` (uint8) and `Stability` (float, 0-100).
Both read via `GetValuePtrByPropertyNameInChain`.

- **Critical**: State == Unstable (3) OR Stability <= 20.0
- **Marginal**: State == Provisional (4) OR Stability <= 50.0 (but > 20)
- **Stable**: State == Stable (2) AND Stability > 50.0

## Key Functions

### spawnVfxAtLocation(worldContext, niagaraSystem, x, y, z)
Fires a one-shot Niagara particle burst at the given world location using
`NiagaraFunctionLibrary::SpawnSystemAtLocation` via ProcessEvent on the CDO.
Uses the game's own `StabilityLossVFX` Niagara system template.

Parameter offsets resolved once from the UFunction and cached in statics:
WorldContextObject, SystemTemplate, Location, Rotation, Scale,
bAutoDestroy (true), bAutoActivate (true). Scale set to (1,1,1).

### spawnPointLightAtLocation(worldContext, x, y, z, critical)
Spawns a PointLight actor using the two-step UE4 deferred spawn pattern:

1. `GameplayStatics::BeginDeferredActorSpawnFromClass` -- creates the actor
   with an FTransform (identity rotation, position at xyz, unit scale).
   The PointLight UClass is found via StaticFindObject.
2. `GameplayStatics::FinishSpawningActor` -- completes initialization with
   the same transform.

Then configures the light via the root PointLightComponent (obtained via
`K2_GetRootComponent`):
- **SetIntensity**: 25,000 (critical) or 15,000 (marginal)
- **SetLightColor**: red {1,0,0,1} (critical) or amber {1,0.8,0,1} (marginal)
- **SetAttenuationRadius**: 10.0 (tight focused highlight)

All function lookups and param offset resolutions are cached in statics.
The spawned actor is tracked in `m_auditSpawnedActors` for later cleanup.

### destroyAuditActors()
Iterates `m_auditSpawnedActors` and calls `K2_DestroyActor` on each via
ProcessEvent. Clears the vector afterward. Logs the count of destroyed actors.

### clearStabilityHighlights()
Calls `destroyAuditActors()`, clears `m_auditLocations` (position + critical
flag per highlight), and resets `m_auditClearTime` to 0. Called on: re-scan,
auto-clear timeout (10s), and world unload.

### runStabilityAudit() -- Main Entry Point
Full audit pipeline:

1. **Clear previous**: `clearStabilityHighlights()` removes existing lights.
2. **Find construction manager**: searches for `MorConstructionManager` via
   FindAllOf, skips CDOs (names containing "Default__").
3. **Get VFX reference**: reads `StabilityLossVFX` property from the manager.
4. **Read AllStabilityComponents**: extracts the TArray pointer and count via
   `GetValuePtrByPropertyNameInChain("AllStabilityComponents")`. Each element
   is a stability component UObject.
5. **Classify**: for each component, reads State (uint8) and Stability (float).
   Skips Deconstructed/Uninitialized/Initializing. Counts stable/marginal/critical.
6. **Collect problems**: for each non-stable component, gets owning actor via
   `GetOwner` ProcessEvent, then location via `K2_GetActorLocation`.
7. **Highlight**: for each problem, spawns VFX (if StabilityLossVFX available)
   and PointLight. Logs distance from player for each piece.
8. **Auto-clear timer**: sets `m_auditClearTime = GetTickCount64() + 10000`.

## Data Flow

```
MC slot 2 keypress
  -> runStabilityAudit()
     -> clearStabilityHighlights()  [remove previous]
     -> find MorConstructionManager (skip CDOs)
     -> read StabilityLossVFX property
     -> read AllStabilityComponents TArray (pointer + count)
     -> for each component:
        -> read State + Stability
        -> classify: Critical / Marginal / Stable / Skip
     -> for each problem:
        -> GetOwner -> K2_GetActorLocation
        -> spawnVfxAtLocation()         [Niagara particle burst]
        -> spawnPointLightAtLocation()  [colored PointLight actor]
     -> set auto-clear timer (10s)

on_update tick:
  -> if m_auditClearTime > 0 && GetTickCount64() > m_auditClearTime:
     -> clearStabilityHighlights()
```

## Tuning Constants

```
AUDIT_LIGHT_CRITICAL_INTENSITY = 25000.0f
AUDIT_LIGHT_MARGINAL_INTENSITY = 15000.0f
AUDIT_LIGHT_RADIUS             = 10.0f     (attenuation radius, UE units)
THRESHOLD_CRITICAL             = 20.0f     (stability value, 0-100 scale)
THRESHOLD_MARGINAL             = 50.0f     (stability value, 0-100 scale)
Auto-clear timeout             = 10 seconds (via GetTickCount64)
```

## Known Caveats

- All parameter offsets for spawn functions, light configuration, and Niagara
  are resolved via runtime reflection and cached in statics. A game update
  that changes function signatures requires a game restart to re-resolve.
- The VFX system pointer (StabilityLossVFX) is read from the construction
  manager each audit. If no VFX is assigned, particle bursts are silently
  skipped (lights still spawn).
- PointLight spawning requires a valid world context (player controller).
  If null, spawning is skipped without error.
- Large bases with many problem pieces spawn many PointLights. The 10-second
  auto-clear prevents accumulation across multiple scans.
- The lightComp fallback (`if (!lightComp) lightComp = spawned`) means if
  K2_GetRootComponent fails, light configuration is attempted on the actor
  itself, which may silently fail for SetIntensity/SetLightColor/SetAttenuationRadius.
- Distance calculation in logs uses Euclidean distance / 100 as approximate
  meters -- not exact due to UE unit conventions.
