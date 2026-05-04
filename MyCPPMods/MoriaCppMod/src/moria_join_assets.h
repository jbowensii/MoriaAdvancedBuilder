// moria_join_assets.h — Asset path constants for the Join World UI replacement.
//
// All paths the mod references at runtime via LoadObject / StaticFindObject are
// listed here. Reference copies of the underlying .uasset binaries live at
// cpp-mod/docs/game-assets/ so the source tree is self-contained for inspection.
//
// At RUNTIME the engine resolves these paths against the loaded paks
// (Moria-WindowsNoEditor.pak/.ucas/.utoc) — the mod itself does NOT bundle
// or ship any game assets.
//
// Path format: /Game/<Content-relative path>/<AssetName>.<ObjectName>
//   where ObjectName is normally identical to AssetName (the primary export).
//
// To verify a path is correct, check cpp-mod/docs/game-assets/MANIFEST.md.

#pragma once

namespace MoriaJoinAssets
{
    // ── Fonts (composite UFont assets) ──────────────────────────────────
    // Leksa is the display serif used for screen titles ("JOIN OTHER WORLD").
    // Tisa_Sans is the body sans used for breadcrumbs, labels, subtitles.
    inline constexpr const wchar_t* FONT_TITLE_DISPLAY  = L"/Game/UI/Font/Leksa.Leksa";
    inline constexpr const wchar_t* FONT_BODY_SANS      = L"/Game/UI/Font/Tisa_Sans.Tisa_Sans";

    // ── Widget classes (BlueprintGeneratedClass) ────────────────────────
    // Path uses _C suffix on the inner ObjectName because Unreal generates
    // a UClass named "<AssetName>_C" alongside the BP asset.
    inline constexpr const wchar_t* WBP_JoinWorldScreen        = L"/Game/UI/MainMenu/WorldSelect/WBP_UI_JoinWorldScreen.WBP_UI_JoinWorldScreen_C";
    inline constexpr const wchar_t* WBP_ChooseWorldScreen      = L"/Game/UI/MainMenu/WorldSelect/WBP_UI_ChooseWorldScreen.WBP_UI_ChooseWorldScreen_C";
    inline constexpr const wchar_t* WBP_GameDataPanel          = L"/Game/UI/MainMenu/WorldSelect/WBP_JoinWorldScreen_GameDataPanel.WBP_JoinWorldScreen_GameDataPanel_C";
    inline constexpr const wchar_t* WBP_AdvancedJoinOptions    = L"/Game/UI/MainMenu/WorldSelect/WBP_UI_AdvancedJoinOptions.WBP_UI_AdvancedJoinOptions_C";
    inline constexpr const wchar_t* WBP_SessionHistoryList     = L"/Game/UI/MainMenu/WorldSelect/WBP_UI_SessionHistoryList.WBP_UI_SessionHistoryList_C";
    inline constexpr const wchar_t* WBP_SessionHistoryItem     = L"/Game/UI/MainMenu/WorldSelect/WBP_UI_SessionHistory_Item.WBP_UI_SessionHistory_Item_C";
    inline constexpr const wchar_t* WBP_NetworkAlert           = L"/Game/UI/MainMenu/WBP_UI_NetworkAlert.WBP_UI_NetworkAlert_C";
    inline constexpr const wchar_t* WBP_PopUp_DedicatedServer  = L"/Game/UI/PopUp/WBP_UI_PopUp_DedicatedServerDetails.WBP_UI_PopUp_DedicatedServerDetails_C";
    inline constexpr const wchar_t* WBP_FrontEndButton         = L"/Game/UI/FrontEnd/WBP_FrontEndButton.WBP_FrontEndButton_C";
    inline constexpr const wchar_t* WBP_HUD_ControlPrompt      = L"/Game/UI/Shared/UI_WBP_HUD_ControlPrompt.UI_WBP_HUD_ControlPrompt_C";
    inline constexpr const wchar_t* WBP_Text_Header            = L"/Game/UI/Shared/UI_WBP_Text_Header.UI_WBP_Text_Header_C";
    inline constexpr const wchar_t* WBP_LowerThird             = L"/Game/UI/Stations/Shared/UI_WBP_LowerThird.UI_WBP_LowerThird_C";
    inline constexpr const wchar_t* WBP_Craft_BigButton        = L"/Game/UI/Stations/ForgeAndCrafting/UI_WBP_Craft_BigButton.UI_WBP_Craft_BigButton_C";

    // ── Textures used as widget brushes ─────────────────────────────────
    inline constexpr const wchar_t* TEX_NetworkAlertPanel      = L"/Game/UI/Textures/Widgets/T_UI_NetworkAlertPanel.T_UI_NetworkAlertPanel";
    // Shared button-background textures the JoinWorld screen reuses:
    inline constexpr const wchar_t* TEX_BtnP1Up                = L"/Game/UI/Textures/_Shared/T_UI_Btn_P1_Up.T_UI_Btn_P1_Up";          // primary button (search, advanced)
    inline constexpr const wchar_t* TEX_BtnP2Up                = L"/Game/UI/Textures/_Shared/T_UI_Btn_P2_Up.T_UI_Btn_P2_Up";          // secondary (history row card)
    inline constexpr const wchar_t* TEX_BtnCTADisabled         = L"/Game/UI/Textures/_Shared/T_UI_Btn_CTA_Disabled.T_UI_Btn_CTA_Disabled"; // thin line / divider
    // Note: search-icon and left-side gradient were originally captured at
    // runtime by the v6.6.0 spawn-duplicate path. v6.7.0 switched to
    // in-place modification of the native widget, so those assets are no
    // longer needed. (Stale TODO removed in v6.14.0.)

    // ── Rich text style data ────────────────────────────────────────────
    inline constexpr const wchar_t* DT_RichTextStyles          = L"/Game/UI/Font/DT_RichTextStyles.DT_RichTextStyles";

    // ── Backend UFunctions (decoded from BP — see docs/blueprint-reference/JoinWorldScreen.md) ──
    // Resolve at runtime via GetFunctionByNameInChain(STR("...")) on the manager
    // singleton (obtained via MorGameSessionManager:Get).
    namespace SessionFn
    {
        inline constexpr const wchar_t* Get                                = L"Get";
        inline constexpr const wchar_t* JoinSession                        = L"JoinSession";
        inline constexpr const wchar_t* JoinPreparedSession                = L"JoinPreparedSession";
        inline constexpr const wchar_t* JoinPreparedSessionWithPassword    = L"JoinPreparedSessionWithPassword";
        inline constexpr const wchar_t* DirectJoinSessionWithPassword      = L"DirectJoinSessionWithPassword";
        inline constexpr const wchar_t* FinishConnectingToServer           = L"FinishConnectingToServer";
        inline constexpr const wchar_t* GetJoinStatus                      = L"GetJoinStatus";
        inline constexpr const wchar_t* GetPermissions                     = L"GetPermissions";
        inline constexpr const wchar_t* GetServerRulesText                 = L"GetServerRulesText";
        inline constexpr const wchar_t* GetPlayerNatType                   = L"GetPlayerNatType";
        inline constexpr const wchar_t* GetCustomJoinOptionalEntitlements  = L"GetCustomJoinOptionalEntitlements";
        inline constexpr const wchar_t* GetSessionJoinMethod               = L"GetSessionJoinMethod";
        inline constexpr const wchar_t* HasRestrictedPermissions           = L"HasRestrictedPermissions";
        inline constexpr const wchar_t* IsPragmaAuthenticated              = L"IsPragmaAuthenticated";
        inline constexpr const wchar_t* RetryOssAuthentication             = L"RetryOssAuthentication";
        inline constexpr const wchar_t* RetryPragmaAuthentication          = L"RetryPragmaAuthentication";
        inline constexpr const wchar_t* ShouldRetryOssAuthentication       = L"ShouldRetryOssAuthentication";
        inline constexpr const wchar_t* CheckPremiumSubscription           = L"CheckPremiumSubscription";
        inline constexpr const wchar_t* CheckPrivilege                     = L"CheckPrivilege";
        inline constexpr const wchar_t* ResetGameConfig                    = L"ResetGameConfig";
        inline constexpr const wchar_t* ShowAccountUpgradeUI               = L"ShowAccountUpgradeUI";
    }

    // Helper utility classes (call via their CDO + GetFunctionByNameInChain)
    inline constexpr const wchar_t* CLS_MorInviteCodeGeneratorUtils  = L"/Script/Moria.MorInviteCodeGeneratorUtils";
    inline constexpr const wchar_t* CLS_MorProfanityFilter           = L"/Script/Moria.MorProfanityFilter";
    inline constexpr const wchar_t* CLS_MorSettingStateManager       = L"/Script/Moria.MorSettingStateManager";
    inline constexpr const wchar_t* CLS_MorGameSessionManager        = L"/Script/Moria.MorGameSessionManager";
    inline constexpr const wchar_t* CLS_MorMenuManager               = L"/Script/Moria.MorMenuManager";
    inline constexpr const wchar_t* CLS_MorUIManager                 = L"/Script/Moria.MorUIManager";
    inline constexpr const wchar_t* CLS_FGKUIManager                 = L"/Script/FGK.FGKUIManager";

    // String table for localised UI strings
    inline constexpr const wchar_t* STR_TABLE_UI                     = L"/Game/Tech/Data/StringTables/UI";
}
