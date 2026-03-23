# HISM Removal System

Source: `MoriaCppMod/src/moria_hism.inl` (Section 6D)

## Purpose

Allows players to hide individual HISM (Hierarchical Instanced Static Mesh) instances
from the world -- rocks, foliage, structural clutter -- by aiming and pressing a key.
Removals persist across sessions via a save file and are replayed on world load.
Also contains fly mode, character visibility toggle, and the "dump aimed actor"
target-info feature.

Since v5.5.0, removals are tagged with bubble IDs and replayed only within the
current bubble. Existing save files are auto-migrated on first load.

## Bubble Tracking

### Overview
Return to Moria's world is divided into "bubbles" (named zones like "Great Hall",
"Lower Deeps"). The mod tracks which bubble the player is in and associates removal
entries with their bubble.

### Detection
Two mechanisms work together:
1. **OnPlayerEnteredBubble delegate** (primary): ProcessEvent post-hook intercepts
   the `AWorldLayout::OnPlayerEnteredBubble` delegate. Extracts the
   `UWorldLayoutBubble*` from params (offset 8) and calls `onBubbleEnteredEvent()`.
2. **30s fallback poll** (secondary): `updateCurrentBubble()` calls `GetBubbleAt()`
   on the cached `AWorldLayout` using the pawn's location. Catches cases where the
   delegate fires before the mod is ready.

### Helper Functions
- **`readBubbleDisplayName(bubble, outName, outId)`**: Reads the `DisplayName`
  FText property via reflection. Returns `(wstring name, string id)`.
- **`bubbleNameToId(name)`**: Converts a display name to an ASCII-safe ID
  (spaces to underscores, alphanumeric only). Returns `"unknown"` if empty.

### Save File Migration
`migrateRemovalsToBubbles()` runs once at initial replay (15s after char load).
For each removal entry without a bubbleId, calls `TryGetBubbleAt()` / `GetBubbleAt()`
with the entry's position to resolve its bubble. Rewrites the save file with
updated IDs.

### Bubble-Filtered Replay
`processReplayBatch()` filters position-based removals by `m_currentBubbleId`.
Type rules apply globally (all bubbles). On bubble change, `m_processedComps` is
cleared so the next stream check re-scans HISM components in the new bubble.

## Key Functions

### getCameraRay(outStart, outEnd)
Constructs a world-space ray from the viewport center. Uses
`DeprojectScreenPositionToWorld` on the player controller (param offsets resolved
via `resolveDSPOffsets`), then offsets the ray start past the pawn
(camera-to-character distance + 50 units) so the trace never hits objects between
the third-person camera and the player. Ray extends to `TRACE_DIST` units.

### doLineTrace(start, end, hitBuf, debugDraw)
Wraps `KismetSystemLibrary::LineTraceSingle` via ProcessEvent on the CDO. Param
offsets (WorldContextObject, Start, End, TraceChannel, ActorsToIgnore, OutHit, etc.)
resolved once via `resolveLTOffsets`. Uses Visibility channel, bTraceComplex=true,
bIgnoreSelf=true. Player pawn added to ActorsToIgnore. Returns true on hit; copies
the 136-byte FHitResultLocal into hitBuf. When debugDraw is true, draws red/green
trace lines in-game for 5 seconds (ForDuration mode). All ProcessEvent calls use
`safeProcessEvent`.

### resolveHitComponent(hitBuf)
Extracts the hit `UObject*` directly from `FHitResult::Component` via
`FWeakObjectPtr::Get()`. Faster and more accurate than name-based component search.

### hideInstance(comp, instanceIndex)
Reads the instance's current transform via `GetInstanceTransform` (validated against
reflection on first call), then calls `UpdateInstanceTransform` to move it 50,000
units underground with 0.001 scale. Safe alternative to `RemoveInstance` which
crashes. Param offsets resolved once via `resolveUITOffsets`.

**CRITICAL**: Maximum 3 `UpdateInstanceTransform` calls per frame
(`MAX_HIDES_PER_FRAME`). Exceeding this crashes the render thread in
`FStaticMeshInstanceBuffer::UpdateFromCommandBuffer_Concurrent`.

### restoreInstance(comp, instanceIndex, originalTransform)
Reverses a hide by writing the saved original transform back via
`UpdateInstanceTransform`. Same ProcessEvent pattern as hideInstance.

### removeAimed()
Full pipeline: getCameraRay -> doLineTrace -> resolveHitComponent -> verify HISM
-> find ALL stacked instances at the same position (within POS_TOLERANCE) -> hide
each one -> push onto undo stack (with FWeakObjectPtr) -> append to save file
(with bubbleId) -> rebuild overlay removal count. Handles stacked instances so a
single keypress removes duplicates at the same location.

Duplicate detection: skips entries that match an existing `m_savedRemovals` entry
by meshName + position within POS_TOLERANCE.

### removeAllOfType()
Traces to identify the aimed HISM component, then records a "type rule"
(`@meshId` line in the save file) in `m_typeRemovals` set. Immediately hides all
instances of that component (skipping already-hidden: Z < -40000). All transforms
saved to undo stack with `isTypeRule=true` and the meshId string.

### undoLast() (in moria_overlay_mgmt.inl)
Pops the most recent entry from `m_undoStack`. For type rules, collects all
contiguous entries for the same meshId, restores each via `restoreInstance`, erases
the `@meshId` rule, and rewrites the save file. For single instances, restores the
transform and removes the matching entry from `m_savedRemovals`. Uses
`FWeakObjectPtr::Get()` to validate component pointers against GC slab reuse.

### updateCurrentBubble()
Fallback poll path. Gets pawn location via `K2_GetActorLocation`, calls
`GetBubbleAt()` (or `TryGetBubbleAt()`) on cached WorldLayout. Reads bubble
DisplayName via `readBubbleDisplayName()`. Returns true if bubble changed.

### onBubbleEnteredEvent(bubble)
Event-driven path. Called from the `OnPlayerEnteredBubble` ProcessEvent hook.
Reads bubble identity, clears `m_processedComps` on change.

### migrateRemovalsToBubbles()
One-shot migration at initial replay. Iterates all `m_savedRemovals` entries with
empty bubbleId, resolves each via `TryGetBubbleAt()`, rewrites save file.

## Data Flow

```
Remove (keypress):
  getCameraRay -> doLineTrace -> hideInstance
    -> push m_undoStack (FWeakObjectPtr + transform)
    -> append m_savedRemovals + save file (with bubbleId)
    -> duplicate detection (skip if already in m_savedRemovals)
    -> buildRemovalEntries (overlay count, grouped by bubble)

Replay (world load):
  migrateRemovalsToBubbles() [one-shot: tag entries with bubble IDs]
  startReplay() -> queue all HISM components as FWeakObjectPtr
  processReplayBatch() [on_update, MAX_HIDES_PER_FRAME per tick]
    -> position-based: match meshId + position within POS_TOLERANCE
       + filter by m_currentBubbleId
    -> type rules: hide every instance of matching mesh (all bubbles)
  checkForNewComponents() -> detect streamed-in HISM -> queue new replay

Bubble tracking:
  OnPlayerEnteredBubble hook -> onBubbleEnteredEvent() [instant]
  updateCurrentBubble() poll [30s fallback]
    -> on change: clear m_processedComps for re-scan

Undo (keypress):
  undoLast() -> restoreInstance -> remove from save -> rewrite file
```

## Persistence Format (removed_instances.txt)

```
# MoriaCppMod removed instances
# meshName|posX|posY|posZ[|bubbleId] = single instance
# @meshName = remove ALL of this type

# Position-based removal with bubble ID:
SM_Rock_Large_01|12345.0|-6789.0|500.0|Great_Hall

# Position-based removal (legacy, no bubble):
SM_Rock_Large_01|12345.0|-6789.0|500.0

# Type rule (all instances of this mesh):
@SM_Mushroom_Cluster_02
```

## Replay System (Throttled)

Spreads UpdateInstanceTransform calls across frames to avoid render thread crashes.

- `startReplay()`: collects all GHISM/HISM components via FindAllOf as
  FWeakObjectPtr queue. Sets `m_replay.active = true`.
- `processReplayBatch()`: called from on_update. Processes up to
  MAX_HIDES_PER_FRAME (3) per tick. For type rules, hides all instances
  (skips Z < -40000). For position-based, matches meshId + position +
  bubbleId filter. Returns true if budget exhausted, false when complete.
- `checkForNewComponents()`: detects newly streamed-in HISM components
  not in `m_processedComps` set and queues them for replay.

## F12 Environment Tab (Tab 2)

Removal entries are displayed grouped by bubble:
- Current bubble marked with a star icon at top
- Other bubbles listed alphabetically below
- Each entry shows friendly name, coordinates, and delete button
- Deferred rebuild: close/reopen cycle on entry deletion

## Additional Features

### dumpAimedActor()
Simple trace (bTraceComplex=false) to hit actor bounds. Resolves owning actor via
GetOwner, gathers: name, class, asset path, display name (GetDisplayName with BP_
cleanup fallback), buildability (checks MorConstructionSnap/Stability/Permit
properties and super class chain), recipe reference from DT_Constructions DataTable.
Results shown via showTargetInfo UMG widget.

### toggleFlyMode()
Sets CharacterMovement::MovementMode to Flying (5) or Falling (3). Toggles
bCheatFlying bool. Order matters: disable bCheatFlying BEFORE mode transition;
enable AFTER. When `m_noCollisionWhileFlying` is set, also calls
`SetActorEnableCollision(false)` for noclip (uses actor-level API, not
CapsuleComponent -- fixes camera issues from v2.1).

### toggleHideCharacter()
Calls `SetActorHiddenInGame` on all `BP_FGKDwarf_C` instances. Tracked via
`m_characterHidden` bool. Useful for screenshots and precise aiming.

## Known Caveats

- The 3-per-frame hide limit is absolute. Replay of large type rules can take
  many frames to complete.
- FWeakObjectPtr guards against GC slab reuse but undo silently skips
  restoration if the component was garbage collected.
- Position tolerance (POS_TOLERANCE) must be loose enough for floating-point
  drift but tight enough to avoid hiding neighbor instances.
- removeAllOfType hides all instances immediately (no per-frame throttle)
  because it runs on keypress. Very large meshes could cause a frame hitch.
- Bubble migration depends on WorldLayout being available at 15s post-load.
  If WorldLayout is not yet streamed in, entries remain untagged until next
  session.
- The 30s fallback poll is intentionally slow. The OnPlayerEnteredBubble
  delegate handles the fast path.
