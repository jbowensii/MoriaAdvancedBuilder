# Khazad-dum Advanced Builder Pack

A feature-rich mod for **The Lord of the Rings: Return to Moria** that dramatically improves the building experience, adds powerful inventory tools, lets you customize your environment, and gives you full control over game settings — all from an easy-to-use in-game interface.

Whether you're building an elaborate Dwarven hall, clearing out unwanted scenery, or just want your tools to last longer, this mod has you covered.

---

## Table of Contents

- [Installation](#installation)
- [Quick-Build System](#quick-build-system)
- [Building with Pitch and Roll](#building-with-pitch-and-roll)
- [Rotation Control](#rotation-control)
- [Snap Toggle](#snap-toggle)
- [On-Screen Toolbars](#on-screen-toolbars)
- [Target Inspection](#target-inspection)
- [Environment Removal](#environment-removal)
- [Inventory Management](#inventory-management)
- [Super Dwarf — Hide Character and Fly Mode](#super-dwarf--hide-character-and-fly-mode)
- [Stability Check](#stability-check)
- [In-Game Configuration Menu](#in-game-configuration-menu)
- [Game Modification Packs](#game-modification-packs)
- [Crosshair Reticle](#crosshair-reticle)
- [Save Game on Demand](#save-game-on-demand)
- [Rename Character](#rename-character)
- [Multiplayer and Server Support](#multiplayer-and-server-support)
- [UI Scaling and Widget Repositioning](#ui-scaling-and-widget-repositioning)
- [Localization](#localization)
- [Default Keybindings](#default-keybindings)
- [Installation Details](#installation-details)
- [Building from Source](#building-from-source)
- [License](#license)

---

## Installation

Download the latest signed installer from the [Releases](https://github.com/jbowensii/MoriaAdvancedBuilder/releases) page. The installer automatically finds your Return to Moria installation (Epic Games Store or Steam), deploys everything to the right place, and takes care of all the setup for you. If your game is installed on a non-default drive (D:, E:, etc.), select the Custom path option and browse to your game folder. If you're upgrading from a previous version, the installer handles that too.

For manual installation instructions, see [Installation Details](#installation-details) below.

---

## Quick-Build System

The Quick-Build system is the heart of this mod. It gives you **eight hotkey slots** (F1 through F8) that you can assign to any building recipe in the game. Once a recipe is saved to a slot, pressing that key instantly opens the build menu and selects that recipe for you — no scrolling through menus, no hunting for the right item. Just press a key and start placing.

### How to Use It

1. Open the build menu normally (press B).
2. Find the recipe you want to save.
3. Hold your modifier key (Shift by default) and press one of the F1-F8 keys.
4. The recipe is now saved to that slot. You'll see the recipe's icon appear in the overlay bar at the top of your screen.

From that point on, just press that F-key and the mod will automatically open the build menu, navigate to the correct recipe, and select it for you. If you're already in build mode, it switches to the new recipe instantly.

### Persistent Slots

Your saved recipes are stored to a file so they survive between play sessions. You can reassign any slot at any time by holding the modifier key and pressing the slot key while a different recipe is selected. You can also clear a slot by holding the modifier key and pressing the slot key when no recipe is selected.

### Automatic Icon Extraction

When you assign a recipe, the mod extracts the recipe's in-game icon and caches it as a PNG image file. These icons are displayed in the overlay bar so you always know which recipe is in which slot at a glance.

---

## Building with Pitch and Roll

Normally in Return to Moria, you can only rotate building pieces left and right (yaw). This mod adds the ability to **tilt building pieces forward/backward (pitch) and side-to-side (roll)**, letting you place walls at angles, create sloped roofs, build ramps, and create architectural designs that simply aren't possible with the base game.

### How to Use It

1. Enter build mode and select a building piece.
2. Press the **Period (.)** key to pitch the piece (tilt it forward or backward).
3. Press the **Comma (,)** key to roll the piece (tilt it left or right).
4. Use your normal rotation key to yaw (spin left/right) as usual.
5. All three rotations combine together, so you can create any angle you want.

The ghost preview (the transparent piece you see before placing) updates in real time to show you exactly how the piece will look, including for multi-part structures like walls with attached pillars or archways with decorative elements. When you place the piece, it lands at exactly the position and angle shown in the ghost preview — the mod compensates for pivot-point offsets so there is no drift between what you see and what you get.

Both pitch and roll rotation can be enabled or disabled individually through the F12 configuration menu under Game Options.

---

## Rotation Control

The Rotation Control feature gives you fine-grained control over how much your building pieces rotate each time you spin them.

- Press the **Rotation key (F9 by default)** to increase the rotation step.
- Hold the **modifier key** and press the Rotation key to decrease the step.
- Steps range from 5 degrees up to 90 degrees, in 5-degree increments.

The overlay bar shows your current rotation step size in bold, along with the total accumulated rotation. When you manually select a new building piece from the menu, the total rotation resets to zero so you start fresh. Your rotation step preference is saved to your configuration file.

---

## Snap Toggle

Press the **Snap Toggle key ([ by default)** to turn building snap on or off. When snap is disabled, the game's snap distance is set to zero, allowing completely free placement of building pieces. Press the key again to restore snap to its normal value.

---

## On-Screen Toolbars

The mod provides two separate toolbars to keep important information visible while you play.

### Win32 Overlay Bar (Top of Screen)

A transparent, always-visible bar sits at the very top of your screen, above the game. It has 12 slots showing:

- **Slots 1-8**: Each slot displays the icon of the saved building recipe (or is empty if unassigned), corresponding to F1-F8.
- **Slots 9-12**: Utility indicators for rotation step, target information, and configuration status.

This overlay stays on top of the game window and doesn't interfere with clicking or gameplay. It automatically hides when the game HUD is hidden (for example, during photo mode).

### Mod Controller Toolbar (In-Game)

A second toolbar appears in the game screen as an in-game widget with icons and key labels. It has 9 slots for the mod's controller functions:

- **Slot 0**: Rotation control (shows current step and total rotation)
- **Slot 1**: Snap toggle
- **Slot 2**: Stability / integrity check
- **Slot 3**: Super Dwarf (hide character / fly mode)
- **Slot 4**: Target inspection
- **Slot 5**: Reserved
- **Slot 6**: Remove target (labeled "Single")
- **Slot 7**: Undo last removal
- **Slot 8**: Remove all of type (labeled "All")

Each button shows its assigned key, so you always know which key does what.

---

## Target Inspection

Want to know what something in the game world actually is? The Target Inspection feature lets you aim at any object and get detailed information about it.

### How to Use It

1. Aim your crosshair at any object in the world.
2. Press the **Target key (} by default)**.
3. An information panel appears showing the object's class, name, display name, asset path, whether it's a buildable item, and its recipe ID.
4. The information is also automatically copied to your clipboard.
5. The panel closes when you press the Target key again.

### Build from Target

If you aim at a building piece that's already been placed in the world and hold the **modifier key** while pressing the Target key, the mod will find that recipe in the build menu and select it for you. This is a quick way to say "I want to build more of *that*" without having to search through menus.

---

## Environment Removal

Return to Moria's underground environments are filled with rocks, roots, crystals, mushrooms, and other scenery objects. Sometimes these get in the way of your building plans. The Environment Removal system lets you hide specific pieces of scenery so they're no longer visible or in the way.

### How to Use It

- **Remove a single object**: Aim at the object and press the **Remove Target key (Numpad 1 by default)**. The object disappears.
- **Undo the last removal**: Press the **Undo key (Numpad 2)**. The most recently removed object reappears.
- **Remove all objects of a type**: Press the **Remove All key (Numpad 3)** while aiming at an object. Every object of that same type in the area is removed at once. Great for clearing out all mushrooms, all crystals, or all of a particular rock formation.

### Persistence

Your removals are saved to a file so they persist between play sessions. When you load into a world, the mod automatically re-hides everything you previously removed. The removal list is organized by which area of the mines ("bubble") you're in, so removals in one area don't accidentally affect another.

### Managing Removals

The F12 configuration menu has a dedicated **Environment** tab where you can see a list of everything you've removed, organized by type. Danger icons highlight type-wide removal rules so you know which categories are being bulk-removed.

---

## Inventory Management

The mod adds three powerful inventory tools that save you time and frustration when managing your items. Each one can be enabled or disabled individually through the F12 configuration menu under Game Options.

### Trash Item (Delete Key)

Accidentally picked up a stack of junk you don't want? The Trash Item feature lets you permanently destroy items from your inventory.

1. Move an item in your inventory (drag it to a different slot). This tells the mod which item you're working with.
2. Press the **Delete key**.
3. A confirmation dialog appears showing the item's icon, name, and how many you're about to destroy.
4. Confirm to destroy the item, or cancel to keep it.

This is especially useful for getting rid of unwanted items without having to drop them on the ground.

### Replenish Item (Insert Key)

Need a full stack of something? The Replenish Item feature fills any stackable item to its maximum stack size instantly.

1. Move the item you want to replenish.
2. Press the **Insert key**.
3. The item's count is set to the maximum stack size as defined in the game's data.

This works with any stackable item — ores, food, building materials, and more. In multiplayer, the replenish command is sent as a server-authoritative request, so it works correctly whether you are the host or a connected client.

### Remove Attributes (End Key)

Items in Return to Moria can have visual tints (colors) and rune effects applied to them. If you want to strip these off and return an item to its default appearance:

1. Move the item you want to clean up.
2. Press the **End key**.
3. All tint colors and rune effects are removed from the item.

---

## Super Dwarf — Hide Character and Fly Mode

Sometimes you need to get your character out of the way for building or photography, or you need to fly around to survey your work from above.

### Hide Character

Press the **Super Dwarf key (] by default)** to toggle your character's visibility. Your dwarf disappears from view but you can still walk around, interact with objects, and build normally. Press the key again to make your character visible again.

### Fly Mode

Hold the **modifier key** and press the Super Dwarf key to toggle fly mode. When fly mode is active, your character can fly freely in any direction without being affected by gravity. This is perfect for building tall structures, inspecting your work from above, or reaching areas that are normally hard to get to. Press the combination again to return to normal movement.

Fly mode works in **single-player, listen-server (hosted) multiplayer, and dedicated servers**. The mod automatically sets server-authoritative movement flags so that the server accepts your position while flying, preventing the rubber-banding that would otherwise occur. On dedicated servers, the mod runs server-side and configures movement authority for all connected players.

### No Collision While Flying

When No Collision is enabled in Game Options, your character passes through walls, floors, and other solid objects while flying. This makes it easy to fly through structures for inspection or to reach enclosed areas.

---

## Stability Check

Press the **Integrity Check key (/ by default)** to run a structural stability audit on the building pieces near you. The mod scans all stability components in the area and highlights any pieces that are structurally marginal or critical. This helps you identify weak points in your constructions before they collapse.

---

## In-Game Configuration Menu

Press **F12** to open the full configuration menu. This is a tabbed dialog that gives you control over every aspect of the mod without ever leaving the game.

### Tab 1: Key Bindings

This tab lets you rebind every single key the mod uses. Keys are organized into sections (Quick Building, Mod Controller, Advanced Builder, Game Options) so they're easy to find. To rebind a key:

1. Click on the key you want to change. It will show "Press key..."
2. Press the new key you want to assign.
3. The change is saved immediately.

You can also change your modifier key (the key you hold for secondary actions) by cycling through Shift, Ctrl, Alt, and Right Alt.

### Tab 2: Game Options

This tab has toggle switches and action buttons for optional features:

- **No Collision (Flying)**: Disable collision detection while flying.
- **Rename Character**: Change your character's display name.
- **Save Game**: Force an immediate save of your game world.
- **Trash Item**: Enable or disable the Delete key item destruction feature.
- **Replenish Item**: Enable or disable the Insert key stack refill feature.
- **Remove Attributes**: Enable or disable the End key effect removal feature.
- **Pitch Rotate**: Enable or disable tilting building pieces forward/backward.
- **Roll Rotate**: Enable or disable tilting building pieces left/right.

### Tab 3: Environment

This tab shows a complete list of everything you've removed using the Environment Removal system. You can see:

- Individual removals with their position in the world.
- Type-wide removal rules (marked with a warning icon).
- The total number of saved removals.

Removal entries are organized by the area of the mines they belong to, with the area you're currently in shown first.

### Tab 4: Game Mods

This tab lists all available game modification packs (see below) with checkboxes to enable or disable each one. Changes take effect the next time you launch the game.

---

## Game Modification Packs

The mod ships with **18 pre-built game modification packs** that tweak various aspects of the game. These are optional — you choose which ones to enable through the F12 configuration menu's Game Mods tab, and they take effect the next time you launch the game.

### Available Packs

| Pack | What It Does |
|------|-------------|
| **Build Anywhere** | Removes placement restrictions so you can build in more locations |
| **Clean Fat Stacks** | Tidies up item stack sizes for a cleaner inventory |
| **Durable Armors** | Makes armor last significantly longer before breaking |
| **Durable Tools** | Makes tools last significantly longer before breaking |
| **Durable Weapons** | Makes weapons last significantly longer before breaking |
| **Earlier Elven Farmbox** | Unlocks the Elven Farmbox earlier in progression |
| **Epic Packs Everywhere** | Makes epic-quality item packs appear in more locations |
| **Fast Monuments** | Speeds up monument construction progress |
| **Fat Loot** | Increases loot drops from containers and enemies |
| **Fat Storage** | Increases the capacity of storage containers |
| **Fat Trade** | Improves trade values with merchants |
| **Free Building** | Removes material requirements for building |
| **Longer Buff Duration** | Makes food and potion buffs last longer |
| **Mental Stability** | Adjusts the mental stability mechanics |
| **Mereaks Secrets Changes** | Modifies Mereak's secrets progression |
| **More Wardrobe Colors** | Adds additional color options for character customization |
| **No Fall Damage** | Eliminates fall damage completely |
| **Unlocked NPCs** | Makes NPCs available earlier in the game |

These packs modify the game's internal data tables at startup, changing values like stack sizes, durability ratings, recipe requirements, buff durations, and more. They use a safe XML-based definition system that reads and modifies game data through the engine's own reflection system.

---

## Crosshair Reticle

When you press the Target key to inspect an object, a crosshair reticle appears at the center of your screen to help you aim precisely. The reticle uses the game's own bow sight graphic and scales properly with your screen resolution. It automatically disappears after 40 seconds.

---

## Save Game on Demand

The Game Options tab in the F12 configuration menu includes a **Save Game** button that triggers an immediate save of your game world. This uses the game's own auto-save system, so it's safe and reliable. There's a 10-second cooldown between saves to prevent accidental rapid saves, and the mod checks that the save system is in a valid state before proceeding.

---

## Rename Character

The Game Options tab in the F12 configuration menu includes a **Rename Character** button that lets you change your dwarf's display name. An editable text box appears where you can type a new name, then confirm or cancel the change.

---

## Multiplayer and Server Support

The mod is fully multiplayer-aware and includes dedicated server support. All players on a server can use the mod's features — fly mode, replenish, building, and rotation all work regardless of whether you are the host or a connected client.

### For Server Operators

The installer includes an option to create a **server deployment folder** with everything a dedicated server needs:

- The mod DLL and UE4SS framework
- A Cheat Manager Enabler mod (for admin commands)
- A Console Enabler mod (for server console access)
- Shared utility libraries
- A Game Mods configuration file with all definition packs
- A server-specific README with setup instructions

To deploy, simply copy the `Win64/` folder contents from the server package into your server's `Moria/Binaries/Win64/` directory.

### Automatic Server Detection

When running on a dedicated server (headless, no display), the mod automatically detects the server environment and skips all visual features (toolbars, overlays, UI panels). Server-side features continue to work:

- **Definition packs** apply DataTable changes on the server so all clients see consistent game data.
- **Fly mode authority** — the server sets movement-authority flags on all player pawns, allowing any connected client to fly without being corrected back to the ground.
- **HISM environment replay** — environment removals are replayed server-side so geometry is consistent for all clients.

### How Fly Mode Works in Multiplayer

Each player's mod instance handles the client-side fly (setting movement mode, visual state). The server-side mod (running on the host or dedicated server) sets two flags on every player's movement component — `bIgnoreClientMovementErrorChecksAndCorrection` and `bServerAcceptClientAuthoritativePosition` — which tell the server to accept the client's reported position without correction. This is what prevents rubber-banding.

On a listen server (you hosting), the mod on your machine handles both roles. On a dedicated server, the mod running on the server handles the authority flags, and each client's mod handles the visual fly state.

---

## UI Scaling and Widget Repositioning

### Automatic Scaling

All of the mod's visual elements (toolbars, menus, information panels, the crosshair reticle) automatically scale based on your screen resolution. Whether you're playing at 1080p, 1440p, or 4K, everything looks properly sized and positioned.

### Drag to Reposition

If you don't like where the toolbars are positioned on your screen, you can move them:

1. Hold the **modifier key** and press the **Advanced Builder Open key (Numpad + by default)**.
2. You enter repositioning mode. The toolbars become draggable.
3. Click and drag any toolbar to your preferred position.
4. Press **Escape** to exit repositioning mode.

Your custom positions are saved to the configuration file and restored each time you play.

---

## Localization

All text displayed by the mod — button labels, status messages, error messages, key names, and more — is loaded from a language file. The mod ships with English text, and you can customize any string by editing the `en.json` file in the mod's localization folder. This also means the mod can be translated to other languages by creating additional language files.

---

## Default Keybindings

Every key listed below can be rebound through the in-game Configuration menu (F12) or by editing `MoriaCppMod.ini` directly.

| Default Key | Action | Description |
|-------------|--------|-------------|
| F1-F8 | Quick Build 1-8 | Instantly select a saved building recipe |
| F9 | Rotation | Increase rotation step (modifier + F9 to decrease) |
| [ | Snap Toggle | Toggle building snap on/off |
| ] | Super Dwarf | Hide character (modifier + key for fly mode) |
| } | Target | Inspect aimed object (modifier + key to build from target) |
| / | Integrity Check | Run structural stability audit on nearby buildings |
| F12 | Configuration | Open the mod configuration menu |
| Numpad 1 | Remove Target | Remove the aimed scenery object |
| Numpad 2 | Undo Last | Undo the most recent removal |
| Numpad 3 | Remove All | Remove all objects of the aimed type |
| Numpad + | Advanced Builder | Open the advanced builder (modifier + key to reposition toolbars) |
| Delete | Trash Item | Destroy the last-moved inventory item |
| Insert | Replenish Item | Fill the last-moved item to max stack size |
| End | Remove Attributes | Strip tint and rune effects from last-moved item |
| . (Period) | Pitch Rotate | Tilt building piece forward/backward |
| , (Comma) | Roll Rotate | Tilt building piece left/right |
| Shift | Modifier | Hold for secondary actions on all keys above |

---

## Installation Details

### Using the Installer (Recommended)

1. Download the latest installer from [Releases](https://github.com/jbowensii/MoriaAdvancedBuilder/releases).
2. Run the installer. It will find your Return to Moria installation automatically.
3. Choose whether to include the server deployment files (optional).
4. Click Install. You're done.

The installer is code-signed for your security.

### Manual Installation

1. Install UE4SS to your game's `Moria/Binaries/Win64/` folder using the `ue4ss/` subfolder layout:
   - Place `dwmapi.dll` in `Win64/`
   - Place `UE4SS.dll`, `UE4SS-settings.ini`, and the `Mods/` folder inside `Win64/ue4ss/`
2. Copy `MoriaCppMod.dll` to `Win64/ue4ss/Mods/MoriaCppMod/dlls/main.dll`
3. Copy the `localization/` folder to `Win64/ue4ss/Mods/MoriaCppMod/localization/`
4. Copy the `definitions/` folder to `Win64/ue4ss/Mods/MoriaCppMod/definitions/`
5. Create an empty file called `enabled.txt` in the `MoriaCppMod/` folder.

### Upgrading from an Older Version

If you previously installed the mod using the old flat layout (everything directly in `Win64/`), you must remove those files before installing the new version. The installer handles this automatically, but if installing manually, delete the old `UE4SS.dll`, `UE4SS-settings.ini`, and `Mods/` folder from `Win64/` before proceeding.

---

## Building from Source

### Prerequisites
- Visual Studio 2022 or later with the C++ workload
- CMake 3.20 or later
- Rust toolchain (required by one of the dependencies)
- The [UE4SS source code](https://github.com/UE4SS-RE/RE-UE4SS) (included as a Git submodule)

### Build the Mod
```bash
cd cpp-mod
cmake --build build --config Game__Shipping__Win64 --target MoriaCppMod
```

### Build and Run Tests
```bash
cd cpp-mod/MyCPPMods/MoriaCppMod/tests
cmake -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
./build/Release/MoriaCppModTests.exe
```

286 unit tests covering localization, string processing, key mapping, file I/O, INI parsing, and memory safety.

### Build the Installer
```powershell
cd installer
.\build.ps1 -SkipSign          # Unsigned (for development)
.\build.ps1                     # Signed (requires code signing credentials)
```

Requires [Inno Setup 6](https://jrsoftware.org/isdl.php).

---

## License

MIT License

Copyright (c) 2025-2026 jbowensii

Includes [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS), licensed under the MIT License.
