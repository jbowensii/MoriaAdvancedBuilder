# WBP_UI_JoinWorldScreen — Blueprint reference

Decoded from `Moria-Replication/tools/cloud-exports-by-type/Function/UI/MainMenu/WorldSelect/WBP_UI_JoinWorldScreen.json` (1.12 MB, includes the full WidgetTree + property defaults + 69 Blueprint functions).

Re-decode with: `retoc to-legacy --filter WBP_UI_JoinWorldScreen` then `UAssetGUI tojson … VER_UE4_27`.

---

## Backend API the screen drives — `MorGameSessionManager` (UCLASS native C++ on `UMorGameSessionManager`)

The screen never caches the manager — every call goes through `MorGameSessionManager:Get` (singleton accessor) and then dispatches.

### Search / Join flow

| Step | UFunction (UE path) | Trigger |
|---|---|---|
| Search by invite code | `MorGameSessionManager:JoinSession` | User commits InviteCodeInput or clicks SearchButton |
| Confirm a prepared session | `MorGameSessionManager:JoinPreparedSession` | After OnSearchSuccess populates the GameDataPanel and user clicks JoinGame |
| Confirm w/ password | `MorGameSessionManager:JoinPreparedSessionWithPassword` | If session is password-protected |
| Direct IP join | `MorGameSessionManager:DirectJoinSessionWithPassword` | Advanced Join Options panel |
| Final connect step | `MorGameSessionManager:FinishConnectingToServer` | After server-rules popup confirmed |
| Status polling | `MorGameSessionManager:GetJoinStatus` | Drives status text + throbber |

### Auth / privilege chain

`CheckPremiumSubscription`, `CheckPrivilege`, `IsPragmaAuthenticated`, `ShouldRetryOssAuthentication`, `RetryOssAuthentication`, `RetryPragmaAuthentication`, `ShowAccountUpgradeUI`, `HasRestrictedPermissions`, `GetPermissions`, `GetCustomJoinOptionalEntitlements`, `GetServerRulesText`, `GetPlayerNatType`, `GetSessionJoinMethod`, `ResetGameConfig`

### UI plumbing

| Function | Use |
|---|---|
| `MorMenuManager:Get` + `ShowSystemMessageBox` | Generic error popups |
| `FGKUIManager:ShowScreen` | Screen navigation |
| `FGKUIManager:ShowOneButtonPopup` / `ShowTwoButtonPopup` | Server-rules popup, confirmation prompts |
| `FGKUIScreen:IsShowing` | Visibility checks |
| `MorUIManager:BPGetManager` | Top-level UI coordinator |

### Helpers (don't reinvent)

- `MorInviteCodeGeneratorUtils:GetInviteCodeMaxLength` — canonical max length for the input field
- `MorProfanityFilter:ActivateProfanityFilterScope` / `DeactivateProfanityFilterScope` — must bracket input field lifetime
- `MorSettingStateManager:IsLimitedMultiplayerSessionModePlatform` — gates whether direct-IP join is shown (console restriction)

---

## Layout (design canvas: 1920 × 1080)

### Top-level CanvasPanel children

| Slot | Content | Position (offsets) | Anchors | Notes |
|---|---|---|---|---|
| `CanvasPanelSlot_4` | `BackgroundBlur_0` (with `BackgroundImg` child) | Right=1440, Bottom=0 | (0,0)→(0,1) | Stretches LEFT 1440 × full height. Color #7F0D0D0F (dark, alpha 0.5). 180° rotated. Material `M_GradientMask` |
| `CanvasPanelSlot_0` | `GridPanel_0` (breadcrumb + title) | Left=124, Top=128 | (0,0) | Auto-size |
| `CanvasPanelSlot_1` | `Overlay_1` (main content) | Left=124, Top=356, Right=1920, Bottom=1010 | (0,0) | Auto-size |
| `CanvasPanelSlot_2` | `WBP_UI_NetworkAlert` | Left=-96, Top=96, Bottom=246 | (1,0) | Top-right corner alert |
| `CanvasPanelSlot_3` | `UI_WBP_LowerThird_C_0` | Right=0, Bottom=172.8 | (0,1)→(1,1) | Bottom-of-screen back/confirm strip |

### GridPanel_0 (title block)

- `GridSlot_0` row 0 → `TextBlock_63` ("WORLD SELECTION")
- `GridSlot_1` row 1 → `Title` ("Join Other World" + ToUpper)

### Overlay_1 → SizeBox_4 → VerticalBox_0

`SizeBox_4`: WidthOverride=900, MinDesiredWidth=900, MaxDesiredWidth=1037.4146 (flexible 900–1037!).

`VerticalBox_0` slots (render order = slot array order in the parent's `Slots`):

| Slot | Content | Padding | Notes |
|---|---|---|---|
| `VBoxSlot_0` | `Spacer_767` | — | Size Y=40 |
| `VBoxSlot_1` | `SessionHistorySwitcher` | Top=24 | WidgetSwitcher: GameDetails ↔ SessionHistoryList |
| `VBoxSlot_2` | `Vbox_InviteCode` | — | Label + input row |
| `VBoxSlot_3` | `MessageContainer` | Fill rule, VAlign Bottom | Status text + throbber |
| `VBoxSlot_4` | `Button_AdvancedJoinOptions` | Bottom=24, HAlign Left | WBP_FrontEndButton_C |
| `VBoxSlot_5` | `Vbox_Password` | Bottom=24 | Hidden by default |

### SizeBox values (all in 1920×1080 design pixels)

| Widget | WidthOverride | HeightOverride |
|---|---|---|
| `SizeBox_4` (outer) | 900 (min 900, max 1037.4) | — |
| `InviteCodeSizeBox` | 458.7595 | (auto) |
| `SearchButtonSizeBox` | 100 | 100 |
| `JoinGameSizeBox` (the big "Join Game" button) | 800 | (auto) |
| `SizeBox` (password input) | 458.7595 | (auto) |

---

## Fonts & colors (ground truth)

All text on this screen uses the same color: **#E1DFD7** (warm off-white) — `RGB(0.749, 0.737, 0.675, 1.0)`.

| TextBlock | Text source | Font | Size | Notes |
|---|---|---|---|---|
| `TextBlock_63` | string-table `Menu.Text.WorldSelection` ("WORLD SELECTION") | `/Game/UI/Font/Tisa_Sans` Regular | (default) | Breadcrumb |
| `Title` | string-table `Menu.Button.JoinWorld` ("Join Other World") | `/Game/UI/Font/Leksa` | **59** | `ETextTransformPolicy::ToUpper` |
| `InviteCodeLabel` | `Menu.SessionKeyPrompt` ("Enter Invite Code for Hosted Game or Server") | `/Game/UI/Font/Leksa` | **32** | NB: it's Leksa, not Tisa |

String tables live at `/Game/Tech/Data/StringTables/UI` (key e.g. `Menu.Button.JoinWorld`).

---

## Key BP function names (69 total)

Lifecycle / hooks: `OnAfterShow`, `OnBeforeShow`, `OnBeforeHide`, `PreConstruct`, `OnBindInputs`, `OnUnbindInputs`, `OnCustomFocusLost`, `OnCustomFocusSet`

Delegate handlers from manager: `OnSearchSuccess`, `OnSearchFailed`, `OnJoinStatusChanged`, `OnPlayerJoinFailFromPassword`, `OnLoginComplete`, `OnNATTypeRetrieved`, `OnNatRetryTimerExpired`, `OnPrivilgeChecked`, `OnPrivilegeFailure`, `OnPremiumSubscriptionCheckedForSearch`

Internal: `SearchForGame_Internal`, `Join Session`, `DirectJoinSession`, `DirectJoin_LoginComplete`, `Retry Login then Search`, `TryDirectJoinByIP`, `TryJoinLocalDedicatedServer`, `TryJoinPreviousSession`, `Check Authentication`, `CheckCanJoin`, `CanSearchForSession`, `ClearSearchResults`, `BindGameSessionManagerEvents`, `UnbindGameSessionManagerEvents`, `BindAdvancedOptionsEvents`, `HandleAdvancedJoinClose`, `HandleJoinGameFromFriendsList`, `Preconnect Messages Ready`, `ConfirmServerMessage`, `CancelServerMessage`, `HideServerRulesPopup`, `Show Cant Join Text`, `Get Search Complete Message Text`, `GetJoinFailMessage`, `GetCachedGameSessionManager`, `GetFocusableWidget`, `GetPermissionSetText`, `GetOptionalEntitlementsText`, `Set Up Vertical Navigation`, `SetGameStatus`, `SetUpBottomNav`, `Bind Input Actions`, `OnActionCalled`, `OnGamepadCancel`, `OnGamepadSearch`, `ToggleSearchThrobbers`, `ToggleMKBButtons`, `UpdateGameDataVisibility`, `UpdateInputMethod`, `UpdateWidgetSwitcher`, `AreAnyGameDataElementsVisible`

UMG-bound delegate forwards (from K2 component bindings):
- `BndEvt__SearchButton_K2Node_ComponentBoundEvent_1_OnMenuButtonClicked`
- `BndEvt__InviteCodeInput_K2Node_ComponentBoundEvent_3_OnEditableTextCommittedEvent`
- `BndEvt__InviteCodeInput_K2Node_ComponentBoundEvent_2_OnEditableTextChangedEvent`
- `BndEvt__Button_AdvancedJoinOptions_K2Node_ComponentBoundEvent_4_OnMenuButtonClicked`
- `BndEvt__JoinGame_K2Node_ComponentBoundEvent_0_buttonPressed`
- `BndEvt__PasswordInput_K2Node_ComponentBoundEvent_5_OnEditableTextChangedEvent`
- `BndEvt__PasswordInput_K2Node_ComponentBoundEvent_6_OnEditableTextCommittedEvent`

---

## Animations (referenced but not yet wired in mod)

- `Intro_INST` (MovieScene `Intro`) — fade-in animation
- `Outro_INST` (`Outro`) — fade-out animation
- `TextPulse_INST` (`TextPulse`) — pulsing emphasis on Title

To trigger: `UserWidget:PlayAnimationForward` / `StopAllAnimations` (already in the call inventory).
