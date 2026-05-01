# Game-asset reference cache

These `.uasset` binaries are **reference copies** of the game assets the mod's
Join World UI replacement loads at runtime. They are NOT shipped with the mod
and the mod does NOT bundle or repack them — Unreal resolves the paths
against the game's own paks (`Moria-WindowsNoEditor.pak/.ucas/.utoc`) at runtime.

The copies exist so the cpp-mod source tree is self-contained for offline
inspection (UAssetGUI, KismetKompiler, etc.) without needing the
Moria-Replication folder mounted.

Source: `C:\Users\johnb\OneDrive\Documents\Projects\Moria-Replication\tools\extracted-assets\Moria\Content\…`
Header: [`MoriaCppMod/src/moria_join_assets.h`](../../MyCPPMods/MoriaCppMod/src/moria_join_assets.h) — all paths the mod loads.

## Inventory

### Widget Blueprints (UMG)

| Asset | Used as | Game path |
|---|---|---|
| `UI/MainMenu/WorldSelect/WBP_UI_JoinWorldScreen.uasset`        | The screen we replicate. Original target of pre/post-hooks. | `/Game/UI/MainMenu/WorldSelect/WBP_UI_JoinWorldScreen` |
| `UI/MainMenu/WorldSelect/WBP_UI_ChooseWorldScreen.uasset`      | Parent screen — has the "Join World" button click handler we hook | `/Game/UI/MainMenu/WorldSelect/WBP_UI_ChooseWorldScreen` |
| `UI/MainMenu/WorldSelect/WBP_JoinWorldScreen_GameDataPanel.uasset` | Search-result panel (server name / region / players / version) | `/Game/UI/MainMenu/WorldSelect/WBP_JoinWorldScreen_GameDataPanel` |
| `UI/MainMenu/WorldSelect/WBP_UI_AdvancedJoinOptions.uasset`    | "Advanced Join Options" sub-panel (direct IP, password, local) | `/Game/UI/MainMenu/WorldSelect/WBP_UI_AdvancedJoinOptions` |
| `UI/MainMenu/WorldSelect/WBP_UI_SessionHistoryList.uasset`     | Session History list container | `/Game/UI/MainMenu/WorldSelect/WBP_UI_SessionHistoryList` |
| `UI/MainMenu/WorldSelect/WBP_UI_SessionHistory_Item.uasset`    | One row in the list (Border → HBox → VBox{title, sub-row}) | `/Game/UI/MainMenu/WorldSelect/WBP_UI_SessionHistory_Item` |
| `UI/MainMenu/WBP_UI_NetworkAlert.uasset`                       | Network-disconnect overlay (sibling on JoinWorld canvas) | `/Game/UI/MainMenu/WBP_UI_NetworkAlert` |
| `UI/PopUp/WBP_UI_PopUp_DedicatedServerDetails.uasset`          | Dedicated-server EULA / rules popup (UFGKPopup variant) | `/Game/UI/PopUp/WBP_UI_PopUp_DedicatedServerDetails` |
| `UI/FrontEnd/WBP_FrontEndButton.uasset`                        | Game-styled pill button (used for Search + Advanced Join Options) | `/Game/UI/FrontEnd/WBP_FrontEndButton` |
| `UI/Shared/UI_WBP_HUD_ControlPrompt.uasset`                    | Gamepad/keybind prompt glyph + label | `/Game/UI/Shared/UI_WBP_HUD_ControlPrompt` |
| `UI/Shared/UI_WBP_Text_Header.uasset`                          | Section header label ("Session History") | `/Game/UI/Shared/UI_WBP_Text_Header` |
| `UI/Stations/Shared/UI_WBP_LowerThird.uasset`                  | Bottom-of-screen back/confirm prompt strip | `/Game/UI/Stations/Shared/UI_WBP_LowerThird` |
| `UI/Stations/ForgeAndCrafting/UI_WBP_Craft_BigButton.uasset`   | Large primary action button ("Join Game") | `/Game/UI/Stations/ForgeAndCrafting/UI_WBP_Craft_BigButton` |

### Fonts (composite UFont + UFontFace)

| Asset | Role | Game path |
|---|---|---|
| `UI/Font/Leksa.uasset`               | **Display serif** — composite UFont used for screen titles ("JOIN OTHER WORLD"). | `/Game/UI/Font/Leksa` |
| `UI/Font/Leksa_Regular.uasset`       | Leksa Regular (UFontFace, .ttf wrapper) referenced by composite | `/Game/UI/Font/Leksa_Regular` |
| `UI/Font/Leksa_Bold.uasset`          | Leksa Bold | `/Game/UI/Font/Leksa_Bold` |
| `UI/Font/Leksa_Italic.uasset`        | Leksa Italic | `/Game/UI/Font/Leksa_Italic` |
| `UI/Font/Leksa_Bold-Italic.uasset`   | Leksa Bold Italic | `/Game/UI/Font/Leksa_Bold-Italic` |
| `UI/Font/Tisa_Sans.uasset`           | **Body sans** — composite UFont used for breadcrumbs, labels, body text. | `/Game/UI/Font/Tisa_Sans` |
| `UI/Font/DT_RichTextStyles.uasset`   | DataTable mapping rich-text style names (e.g. `Subtitle_Large`) to FSlateFontInfo + color. Useful for matching exact in-game text styling. | `/Game/UI/Font/DT_RichTextStyles` |

### Textures (widget brushes)

| Asset | Role | Game path |
|---|---|---|
| `UI/textures/Widgets/T_UI_NetworkAlertPanel.uasset` | Background image for the network-alert popup | `/Game/UI/textures/Widgets/T_UI_NetworkAlertPanel` |

## Not yet captured

- Magnifying-glass search-icon texture used by `SearchButton` — captured at runtime from the native widget's `IconTexture` property.
- Left-side dark gradient image on the JoinWorld canvas — captured at runtime from `BackgroundImg.Brush.ResourceObject`.
- Both should eventually be located and added to the manifest so the mod no longer depends on having visited the native screen first. Search FModel for: textures referenced by `WBP_UI_JoinWorldScreen` properties `IconTexture` (on `SearchButton`) and `BackgroundImg` (Image widget).

## How to refresh

If the game updates and assets move/rename:

```bash
SRC=/c/Users/johnb/OneDrive/Documents/Projects/Moria-Replication/tools/extracted-assets/Moria/Content
DST=/c/Users/johnb/OneDrive/Documents/Projects/UE4SS\ Testing/cpp-mod/docs/game-assets
# re-run the cp commands from this folder's commit history
```

Verify each path in `moria_join_assets.h` is still valid by booting the game with verbose logging — `LoadObject` failures are reported as `[JoinWorldUI] jw_applyFontByAssetPath: '<path>' not found in object table` in `UE4SS.log`.
