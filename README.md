# Khazad-dum Advanced Builder Pack

A comprehensive building and utility mod for **The Lord of the Rings: Return to Moria** (Unreal Engine 4.27).

Built as a UE4SS C++ mod, this pack adds quick-build hotkeys, a rotation controller, target inspection, toolbar management, environment removal, an in-game configuration menu, and a Win32 overlay HUD — all with a signed Windows installer.

## Features

### Quick-Build System (F1-F8)
- **8 hotkey slots** bound to F1-F8 for instant recipe activation
- Capture any build recipe by selecting it in the build menu and pressing a slot key
- Recipes persist across sessions (saved to `quickbuild_slots.txt`)
- Automatic icon extraction from game textures (cached as PNGs)

### Mod Controller Toolbar
- **8-slot UMG toolbar** (4x2 grid) with live icons and key labels
- Slots: Rotation | Target | Toolbar Swap | Super Dwarf | Remove Target | Undo Last | Remove All | Configuration

### Rotation Control
- Step-based rotation: 5-90 degrees in 5-degree increments
- Modifier key decreases, plain key increases
- Live overlay display showing current step and total rotation

### Target Inspection
- Aim at any actor and press the Target key to inspect
- Displays: Class, Name, Display Name, Asset Path, Buildable status, Recipe ID
- Auto-copies target info to clipboard
- Auto-closes after 10 seconds
- Modifier+Target triggers "Build from Target" (re-selects the aimed recipe)

### Toolbar Swap
- Instantly swap between hotbar and bag inventory
- Automated multi-step process: open inventory, select containers, transfer items

### Super Dwarf (Hide Character / Fly Mode)
- **Key alone**: Toggle player character visibility via `SetActorHiddenInGame`
- **Modifier+Key**: Toggle fly mode via `SetMovementMode(MOVE_Flying)` + `bCheatFlying` (BETA)
- Eye icon on Mod Controller toolbar

### Environment Removal (HISM)
- Remove aimed Hierarchical Instanced Static Mesh components
- Undo last removal
- Remove all instances of a type
- Persistent removal list (saved to `removed_instances.txt`, re-applied on world load)
- Type rules for automatic bulk removal

### In-Game Configuration Menu (F12)
- **3-tab UMG modal dialog** (1400x900):
  - **Optional Mods**: Free Build toggle, Unlock All Recipes
  - **Key Mapping**: Rebind all keys, cycle modifier key (SHIFT/CTRL/ALT/RALT), visual key boxes
  - **Hide Environment**: Removal list with danger icons, saved removal count
- All keybind polling paused while config is visible
- Key capture with 0x8000 edge detection

### Win32 Overlay HUD
- Transparent always-on-top overlay bar above the game
- 12 slots: F1-F8 (build recipes with icons) | separator | F9-F12 (utility)
- GDI+ rendering with per-pixel alpha
- Auto-updates on slot changes, rotation, and target info

### Localization
- Full string table with English defaults compiled in
- JSON override file (`localization/en.json`) for customization
- All UI text, messages, key names, and labels are localizable

## Installation

### Installer (Recommended)
Download the latest signed installer from [Releases](https://github.com/jbowensii/MoriaAdvancedBuilder/releases):

```
KhazadDumAdvancedBuilderPack_v1.15_Setup.exe
```

The installer auto-detects your Return to Moria installation (Epic Games Store or Steam) and deploys all files.

### Manual Installation
1. Install [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) to `<game>/Moria/Binaries/Win64/`
2. Copy `MoriaCppMod.dll` to `<game>/Moria/Binaries/Win64/Mods/MoriaCppMod/dlls/main.dll`
3. Copy `en.json` to `<game>/Moria/Binaries/Win64/Mods/MoriaCppMod/localization/en.json`
4. Create an empty `enabled.txt` in the `MoriaCppMod/` directory

## Default Keybindings

| Key | Action | Section |
|-----|--------|---------|
| F1-F8 | Quick Build slots 1-8 | Quick Building |
| F10 | Rotation control | Mod Controller |
| F9 | Target inspection | Mod Controller |
| PgDn | Toolbar swap | Mod Controller |
| Num4 | Super Dwarf (hide character) | Mod Controller |
| Num1 | Remove aimed target | Mod Controller |
| Num2 | Undo last removal | Mod Controller |
| Num3 | Remove all of type | Mod Controller |
| F12 | Configuration menu | Mod Controller |
| Enter | Advanced Builder open | Advanced Builder |
| SHIFT | Modifier key (rebindable) | Global |

All keys are rebindable via the in-game Configuration menu (Key Mapping tab).

## Building from Source

### Prerequisites
- Visual Studio 2022+ with C++ workload
- CMake 3.20+
- Rust toolchain (for patternsleuth)
- [UE4SS source](https://github.com/UE4SS-RE/RE-UE4SS) (included as submodule)

### Build Mod
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

108 unit tests covering: localization parsing, string helpers, key mapping, file I/O parsing, and memory safety.

### Build Installer
```powershell
cd installer
.\build.ps1 -SkipSign          # unsigned (development)
.\build.ps1                     # signed (requires SSL.com eSigner credentials)
```

Requires [Inno Setup 6](https://jrsoftware.org/isdl.php).

## Project Structure

```
cpp-mod/                          # Git repo root
  MyCPPMods/MoriaCppMod/
    src/
      dllmain.cpp                 # Main mod source (~12,000 lines)
      moria_testable.h            # Extracted pure-logic functions (shared with tests)
    localization/
      en.json                     # English string table
    tests/
      CMakeLists.txt              # GoogleTest standalone project
      test_loc.cpp                # Localization + JSON parser tests
      test_string_helpers.cpp     # extractFriendlyName, componentNameToMeshId, wrapText
      test_key_helpers.cpp        # keyName, modifierName, nextModifier
      test_file_io.cpp            # parseRemovalLine, parseSlotLine, parseKeybindLine
      test_memory.cpp             # isReadableMemory
  README.md
  .gitignore
```

## Technical Details

- **Engine**: Unreal Engine 4.27
- **Framework**: UE4SS (custom build, experimental-latest branch)
- **Player Character**: `BP_FGKDwarf_C` (inherits `AMorCharacter` -> `ACharacter`)
- **UE4SS Settings**: GraphicsAPI=opengl (dx11 hangs this game), RenderMode=ExternalThread
- **Icon Extraction**: Canvas render target approach (CPU bulk data not available after GPU upload)
- **Key Polling**: `GetAsyncKeyState` with 0x8000 edge detection + SHIFT-numpad VK alternate handling
- **Overlay**: `WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT` with GDI+ per-pixel alpha

## License

MIT License

Copyright (c) 2025-2026 jbowensii

Includes [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS), licensed under the MIT License.
