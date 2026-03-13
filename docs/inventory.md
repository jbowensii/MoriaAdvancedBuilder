# Inventory & Toolbar Swap System

Source: `MoriaCppMod/src/moria_inventory.inl` (Section 6E)

## Purpose

Provides a second toolbar (8-slot hotbar) by using BodyInventory containers as
off-screen stash buffers. Players press PageDown to swap between Toolbar 1 and
Toolbar 2. The system discovers the player's inventory component at runtime,
creates stash containers if needed, and runs a multi-frame state machine to
move items between the hotbar and stash containers.

## Key Functions

### findPlayerInventoryComponent(playerChar)
Searches all ActorComponents via `FindAllOf("ActorComponent")`. For each, calls
`GetOwner` via ProcessEvent and checks if it matches the player character. Returns
the component whose class name is exactly `MorInventoryComponent`, or nullptr.

### discoverBagHandle(invComp)
Central discovery function that maps the player's inventory containers:

1. **Get handle size**: reads `GetItemForHotbarSlot` return value property size
   to determine FItemHandle byte width (fallback: 20 bytes).
2. **Enumerate containers**: calls `GetContainers()` on the inventory component,
   returns a TArray of FItemHandle container references.
3. **Build ID-to-class map**: reads `Items.List` TArray from the inventory
   component (offset resolved via `resolveOffset` + `probeItemInstanceStruct`).
   Each FItemInstance entry provides an int32 ID and a UObject* class pointer.
4. **Identify containers**: iterates container handles, maps each ID to a class
   name. EpicPack -> `m_bagHandle`. BodyInventory -> `m_bodyInvHandles[]`.
5. **Diagnostic**: queries `IHF::GetStorageMaxSlots` via the ItemHandleFunctions
   CDO for each container (log only).
6. **Repair stale containers**: if `m_bodyInvHandles.size() > 3`, drops contents
   and containers for all extras (see Stash Container Repair below).
7. **Create missing stash**: if only 1 BodyInventory exists, creates 2 more via
   `AddItem` with `EAddItem::Create` method using the BodyInventory UClass.

### Stash Container Repair
Triggered when more than 3 BodyInventory containers exist (extras accumulated from
prior sessions). Iterates backward from the last handle:
- Reads every slot via `IHF::GetItemForSlot` + `IsValidItem`
- Drops valid items via `DropItem` (they fall on the ground near the player)
- Drops the container itself via `DropItem`
- Resizes `m_bodyInvHandles` to 1, sets `m_repairDone = true`
Fresh stash containers are then created by the normal creation path.

## Container Layout

```
m_bodyInvHandles[0] -- Hotbar (the player's visible 8-slot bar)
m_bodyInvHandles[1] -- Toolbar 1 stash buffer
m_bodyInvHandles[2] -- Toolbar 2 stash buffer
```

All handles are `std::vector<uint8_t>` containing raw FItemHandle bytes.

## Toolbar Swap State Machine

### SwapState Struct
```
active       -- swap in progress (blocks new swap requests)
resolved     -- resolve phase complete
cleared      -- EmptyContainer called on stash destination
dropToGround -- failsafe: both containers have items, drop instead
phase        -- 0 = stash hotbar, 1 = restore from container
slot         -- current slot being processed (0-7)
moved        -- items successfully moved counter
wait         -- frame delay counter between operations
nextTB/curTB -- toolbar indices (0 or 1)
stashIdx     -- container index for stashing (1 or 2)
restoreIdx   -- container index for restoring (1 or 2)
```

### swapToolbar() -- Entry Point
Validates state: not already active, containers discovered (runs discoverBagHandle
if not cached), at least 3 BodyInventory handles. Initializes `m_swap` with default
container mapping (stashIdx = curTB+1, restoreIdx = nextTB+1). Shows on-screen
notification.

### swapToolbarTick() -- Per-Frame Processing
Called from `on_update()`. Each tick: finds player + inventory, looks up MoveItem
function, resolves IHF function params (GetItemForSlot, IsValidItem). Processes
one item move per tick with 3-frame wait between operations.

**readSlot helper**: lambda that calls `IHF::GetItemForSlot` on a container handle
for a given slot index, then validates with `IHF::IsValidItem`. Returns true if
the slot contains a valid item; the item handle bytes are in the output buffer.

**Resolve Phase** (runs once before Phase 0):
Counts valid items in containers [1] and [2] using readSlot across all 8 slots.
- One empty, one populated: stash to empty, restore from populated
- Both populated (error state): `dropToGround=true`, restore from whichever has more
- Both empty: keep defaults (first swap, Toolbar 2 starts empty)

**Phase 0 -- Stash Hotbar**:
Normal path: calls `EmptyContainer` on stash destination first (clears stale items
from prior sessions), then for each hotbar slot reads the item and calls `MoveItem`
to transfer to the stash container. Verifies each slot is empty after move.
Drop-to-ground path: calls `DropItem` with count=999999 instead of MoveItem.

**Phase 1 -- Restore**:
For each slot in the restore container: reads item via readSlot, calls `MoveItem`
to transfer to the hotbar container, verifies arrival. After all slots processed,
calls `EmptyContainer` on the restore source for cleanup.

On completion: updates `m_activeToolbar`, syncs `s_overlay.activeToolbar`, sets
`s_overlay.needsUpdate`, shows confirmation message, calls `refreshActionBar()`.

## Item Handle Functions (IHF)

CDO found at `/Script/FGK.Default__ItemHandleFunctions`. Used for:
- `GetItemForSlot(Item, Slot)` -- read an item from a container slot
- `IsValidItem(Item)` -- check if an FItemHandle references a real item
- `GetStorageMaxSlots(Item)` -- query container capacity (diagnostic only)

## Data Flow

```
PageDown keypress
  -> swapToolbar()
     -> discoverBagHandle() if not cached
     -> initialize SwapState {active=true, phase=0}
  -> swapToolbarTick() [called each on_update frame]
     -> Resolve: count items in containers [1] and [2]
     -> Phase 0: for each slot, MoveItem(hotbar -> stash) or DropItem
     -> Phase 1: for each slot, MoveItem(restore -> hotbar)
     -> Complete: update activeToolbar, sync overlay, refreshActionBar
```

## Known Caveats

- The 3-frame wait between moves is empirically tuned. Too fast causes items
  to silently fail to move.
- Container repair runs once per session (m_repairDone flag). Restarting the
  game resets this.
- If the BodyInventory class pointer is not found in the Items list, stash
  containers cannot be created and toolbar swap is unavailable.
- The dropToGround failsafe means items land at the player's feet. This only
  triggers when both stash containers have items (error state from prior crash).
- FItemHandle struct internals are probed dynamically via probeItemInstanceStruct.
  Hardcoded fallback offsets exist but may break on game updates.
