// MoriaCppMod v6.21.0 — Return to Moria UE4SS C++ mod (~17,000 lines across dllmain.cpp + 15 .inl files)
// Features: quick-build system, HISM removal with bubble tracking, inventory management (trash/replenish/remove-attrs),
// definition processing, pitch/roll placement, crosshair reticle, Win32 overlay toolbar, F12 config panel, localization
// Stability: FWeakObjectPtr caches, CancelTargeting via ProcessEvent, deferRemoveWidget, 350ms settle delays

#include "moria_common.h"
#include "moria_reflection.h"
#include "moria_keybinds.h"
#include "moria_dualsense.h"
#include "moria_join_assets.h"
#include <Unreal/Hooks.hpp>
#include <UE4SSProgram.hpp>
#include <psapi.h>  // GetModuleInformation for FGK injection test

namespace MoriaMods
{


    DWORD WINAPI overlayThreadProc(LPVOID);


    class MoriaCppMod : public RC::CppUserModBase
    {
      private:
        std::vector<RemovedInstance> m_undoStack;
        std::vector<SavedRemoval> m_savedRemovals;
        std::set<std::string> m_typeRemovals;
        std::set<UObject*> m_processedComps;
        int m_frameCounter{0};
        bool m_replayActive{false};
        bool m_characterLoaded{false};
        bool m_isDedicatedServer{false};  // true = headless server (no viewport, skip UI)
        UObject* m_localPC{nullptr};     // cached local PlayerController (set at character load)
        UObject* m_localPawn{nullptr};   // cached local pawn (set at character load)
        bool m_serverDetected{false};     // flag: detection has run
        bool m_initialReplayDone{false};
        bool m_inventoryAuditDone{false};
        bool m_definitionsApplied{false};
        int m_stuckLogCount{0};
        std::string m_saveFilePath;
        UObject* m_worldLayout{nullptr};
        std::string m_currentBubbleId;
        std::wstring m_currentBubbleName;
        UObject* m_currentBubble{nullptr};  // v6.4.2 — cached for bubble-local coord calc
        PSOffsets m_ps;
        std::vector<bool> m_appliedRemovals;


        ULONGLONG m_lastWorldCheck{0};
        ULONGLONG m_lastCharPoll{0};
        ULONGLONG m_lastStreamCheck{0};
        ULONGLONG m_lastRescanTime{0};
        ULONGLONG m_lastBubbleCheck{0};
        ULONGLONG m_lastServerFlySweep{0};
        ULONGLONG m_charLoadTime{0};


        struct ReplayState
        {
            std::vector<RC::Unreal::FWeakObjectPtr> compQueue;
            size_t compIdx{0};
            int instanceIdx{0};
            bool active{false};
            int totalHidden{0};
        };
        ReplayState m_replay;
        static constexpr int MAX_HIDES_PER_FRAME = 3;


        bool hasPendingRemovals() const
        {
            for (size_t i = 0; i < m_appliedRemovals.size(); i++)
            {
                if (!m_appliedRemovals[i]) return true;
            }
            return false;
        }

        int pendingCount() const
        {
            int n = 0;
            for (size_t i = 0; i < m_appliedRemovals.size(); i++)
            {
                if (!m_appliedRemovals[i]) n++;
            }
            return n;
        }


        void loadSaveFile()
        {
            m_savedRemovals.clear();
            m_typeRemovals.clear();
            std::ifstream file = openInputFile(m_saveFilePath);
            if (!file.is_open())
            {
                VLOG(STR("[MoriaCppMod] No save file found (first run)\n"));
                return;
            }
            std::string line;
            bool sawLegacyLine = false;  // track if any line was non-JSON non-comment (for auto-migration)
            while (std::getline(file, line))
            {
                if (!line.empty() && line[0] != '#' && line[0] != '{' && line[0] != '\r')
                    sawLegacyLine = true;

                auto parsed = parseRemovalLine(line);
                if (auto* pos = std::get_if<ParsedRemovalPosition>(&parsed))
                {
                    m_savedRemovals.push_back({
                        pos->meshName,
                        pos->posX, pos->posY, pos->posZ,
                        pos->localX, pos->localY, pos->localZ,
                        pos->bubbleId,
                        pos->bubbleName});
                }
                else if (auto* tr = std::get_if<ParsedRemovalTypeRule>(&parsed))
                {
                    m_typeRemovals.insert(tr->meshName);
                }
            }
            file.close();

            {
                size_t before = m_savedRemovals.size();
                std::erase_if(m_savedRemovals, [this](const SavedRemoval& sr) {
                    return m_typeRemovals.count(sr.meshName) > 0;
                });
                size_t redundant = before - m_savedRemovals.size();
                if (redundant > 0)
                {
                    VLOG(STR("[MoriaCppMod] Removed {} position entries redundant with type rules\n"), redundant);
                }
            }


            m_appliedRemovals.assign(m_savedRemovals.size(), false);

            VLOG(STR("[MoriaCppMod] Loaded {} position removals + {} type rules\n"), m_savedRemovals.size(), m_typeRemovals.size());

            // v6.4.2 — auto-migrate legacy pipe-delimited or @-prefixed entries to JSON format.
            if (sawLegacyLine)
            {
                VLOG(STR("[MoriaCppMod] Detected legacy-format entries — rewriting save file as JSON Lines\n"));
                rewriteSaveFile();
            }
            // Log first 5 saved mesh names for replay diagnostics
            for (size_t i = 0; i < std::min(m_savedRemovals.size(), (size_t)5); i++)
            {
                auto& sr = m_savedRemovals[i];
                VLOG(STR("[MoriaCppMod] [Saved-Diag] [{}] mesh='{}' pos=({},{},{}) bubble='{}'\n"),
                     i, utf8ToWide(sr.meshName),
                     sr.posX, sr.posY, sr.posZ,
                     utf8ToWide(sr.bubbleId));
            }
        }

        void appendToSaveFile(const SavedRemoval& sr)
        {
            std::ofstream file = openOutputFile(m_saveFilePath, std::ios::app);
            if (!file.is_open()) return;
            file << formatRemovalJson(sr) << "\n";
        }

        void rewriteSaveFile()
        {
            std::ofstream file = openOutputFile(m_saveFilePath, std::ios::trunc);
            if (!file.is_open()) return;
            file << "# MoriaCppMod removed instances (JSON Lines format, v6.4.2+)\n";
            file << "# One JSON object per line. Lines starting with # are comments.\n";
            file << "# Position entry: {\"mesh\":\"...\",\"bubble\":\"<id>\",\"bubbleName\":\"<display name>\",\"world\":[x,y,z],\"local\":[x,y,z]}\n";
            file << "# Type rule:      {\"typeRule\":\"...\"}\n";
            for (auto& type : m_typeRemovals)
                file << formatTypeRuleJson(type) << "\n";
            for (auto& sr : m_savedRemovals)
                file << formatRemovalJson(sr) << "\n";
        }


        void buildRemovalEntries()
        {
            std::vector<RemovalEntry> entries;
            std::ifstream file = openInputFile(m_saveFilePath);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    if (line.empty() || line[0] == '#') continue;
                    auto parsed = parseRemovalLine(line);

                    RemovalEntry entry{};
                    if (auto* tr = std::get_if<ParsedRemovalTypeRule>(&parsed))
                    {
                        entry.isTypeRule = true;
                        entry.meshName = tr->meshName;
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = utf8ToWide(entry.meshName);
                        // JSON-tagged display for type rules
                        std::string json = formatTypeRuleJson(entry.meshName);
                        entry.coordsW = utf8ToWide(json);
                    }
                    else if (auto* pos = std::get_if<ParsedRemovalPosition>(&parsed))
                    {
                        entry.isTypeRule = false;
                        entry.meshName = pos->meshName;
                        entry.posX = pos->posX; entry.posY = pos->posY; entry.posZ = pos->posZ;
                        entry.localX = pos->localX; entry.localY = pos->localY; entry.localZ = pos->localZ;
                        entry.bubbleId = pos->bubbleId;
                        entry.bubbleName = pos->bubbleName;
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = utf8ToWide(entry.meshName);
                        // JSON-tagged display: compact one-line object showing bubble id + name + world + local coords
                        char buf[512];
                        std::snprintf(buf, sizeof(buf),
                            "{\"bubble\":\"%s\",\"bubbleName\":\"%s\",\"world\":[%.1f,%.1f,%.1f],\"local\":[%.1f,%.1f,%.1f]}",
                            RemovalJson::escape(entry.bubbleId).c_str(),
                            RemovalJson::escape(entry.bubbleName).c_str(),
                            entry.posX, entry.posY, entry.posZ,
                            entry.localX, entry.localY, entry.localZ);
                        std::string s(buf);
                        entry.coordsW = utf8ToWide(s);
                    }
                    else
                    {
                        continue;  // unrecognized line
                    }
                    entries.push_back(std::move(entry));
                }
            }
            if (s_config.removalCSInit)
            {
                CriticalSectionLock removalLock(s_config.removalCS);
                s_config.removalEntries = std::move(entries);
                s_config.removalCount = static_cast<int>(s_config.removalEntries.size());
            }
        }

        #include "moria_common.inl"
        #include "moria_datatable.inl"
        #include "moria_DefinitionProcessing.inl"

        #include "moria_debug.inl"

        #include "moria_hism.inl"

        #include "moria_inventory.inl"

        #include "moria_stability.inl"

        #include "moria_unlock.inl"


        static inline MoriaCppMod* s_instance{nullptr};

        // v6.4.1 — Recipe unlock queue state (drained by main tick at UNLOCK_BATCH_SIZE/frame)
        static constexpr int UNLOCK_BATCH_SIZE = 50;
        std::vector<std::wstring> m_unlockQueue;
        UObject*   m_unlockDiscoveryMgr{nullptr};
        UFunction* m_unlockDiscoverRecipeFn{nullptr};
        int m_unlockTotal{0};
        int m_unlockProcessed{0};


        static constexpr int QUICK_BUILD_SLOTS = 12;

        static constexpr int BLOCK_DATA_SIZE = 120;

        static constexpr int RECIPE_HANDLE_SIZE = 16;

        struct RecipeSlot
        {
            std::wstring displayName;
            std::wstring textureName;
            std::wstring rowName;
            uint8_t bLockData[BLOCK_DATA_SIZE]{};
            bool hasBLockData{false};
            uint8_t recipeHandle[RECIPE_HANDLE_SIZE]{};
            bool hasHandle{false};
            bool used{false};
        };
        RecipeSlot m_recipeSlots[QUICK_BUILD_SLOTS]{};


        std::wstring m_lastCapturedName;
        uint8_t m_lastCapturedBLock[BLOCK_DATA_SIZE]{};
        uint8_t m_lastCapturedHandle[RECIPE_HANDLE_SIZE]{};
        bool m_hasLastCapture{false};
        bool m_hasLastHandle{false};
        bool m_isAutoSelecting{false};


        enum class PlacePhase { Idle, CancelGhost, WaitingForShow, SelectRecipeWalk };

        enum class SelectResult { Found, Loading, NotFound };
        int m_pendingQuickBuildSlot{-1};
        // v6.9.0 CP3 — edge-detector for chord-aware Quick Build SET + USE.
        bool m_qbSetEdge[8]{};
        bool m_qbUseEdge[8]{};
        PlacePhase m_qbPhase{PlacePhase::Idle};
        ULONGLONG m_showSettleTime{0};
        ULONGLONG m_lastHandleResolveSlotTime{0};
        ULONGLONG m_qbStartTime{0};


        enum class HandleResolvePhase { None, Priming, Resolving, Done };
        HandleResolvePhase m_handleResolvePhase{HandleResolvePhase::None};
        ULONGLONG m_handleResolveStartTime{0};
        int m_handleResolveSlotIdx{0};
        ULONGLONG m_lastDirectSelectTime{0};
        ULONGLONG m_lastShowHideTime{0};
        ULONGLONG m_lastQBSelectTime{0};


        RC::Unreal::FWeakObjectPtr m_cachedBuildComp;
        RC::Unreal::FWeakObjectPtr m_cachedBuildHUD;
        RC::Unreal::FWeakObjectPtr m_cachedBuildTab;


        std::wstring m_targetBuildName;
        std::wstring m_targetBuildRecipeRef;
        std::wstring m_targetBuildRowName;
        bool m_lastTargetBuildable{false};
        bool m_isTargetBuild{false};
        bool m_buildMenuWasOpen{false};
        bool m_deferHideAndRefresh{false};
        int m_deferRemovalRebuild{0};  // 0=none, 2=hide, 1=rebuild

        bool m_showHotbar{true};
        bool m_gameHudVisible{true};
        bool m_inFreeCam{false};


        std::atomic<bool> m_pendingCharNameReady{false};
        std::mutex m_charNameMutex;
        std::wstring m_pendingCharName;


        UObject* m_umgBarWidget{nullptr};
        UObject* m_umgSlotButtons[8]{};           // UButton wrappers for gamepad navigation
        UObject* m_umgStateImages[8]{};
        UObject* m_umgIconImages[8]{};
        UObject* m_umgIconTextures[8]{};
        std::wstring m_umgIconNames[8];
        UObject* m_umgTexEmpty{nullptr};
        UObject* m_umgTexInactive{nullptr};
        UObject* m_umgTexActive{nullptr};
        enum class UmgSlotState : uint8_t { Empty, Inactive, Active, Disabled };
        UmgSlotState m_umgSlotStates[8]{};
        int m_activeBuilderSlot{-1};
        UFunction* m_umgSetBrushFn{nullptr};


        static constexpr int MC_SLOTS = 9;
        UObject* m_mcBarWidget{nullptr};
        // v6.10.0 — "New Building Bar": cloned WBP_UI_ActionBar_C instance,
        // chrome only (look). Tame-spawned at top of HUD with the 4 special
        // slots (Epic/HeavyCarry/MainHand/Offhand) and inventory wiring
        // hidden/disabled. Phase 2 wires our 8 builder slots to its 8
        // numbered slot widgets.
        UObject* m_newBuildingBar{nullptr};
        bool m_newBuildingBarSpawnAttempted{false};
        // v6.10.0 — per-slot widget pointers for the New Building Bar.
        // Set during createNewBuildingBar(); used for highlight + Phase 2
        // icon/label updates. 8 slots = 8 indexed entries.
        UObject* m_nbbSlotEmpty[8]{};   // empty-state UImage
        UObject* m_nbbSlotFocus[8]{};   // focused-state UImage (visibility toggled for highlight)
        UObject* m_nbbSlotIcon[8]{};    // icon UImage (Phase 2 — builder piece icon)
        UObject* m_nbbSlotKeyLbl[8]{};  // F-key UTextBlock
        UObject* m_nbbSlotMarker[8]{};  // numbered marker UImage above each slot
        UObject* m_nbbSlotButton[8]{};  // UButton wrapper (Phase 2 — for clicks)
        UObject* m_nbbSlotKeyBg[8]{};   // grey rect under the F# label
        // v0.8 — Texture cache. Populated ONCE per session via
        // nbbDiscoverAssets; subsequent createNewBuildingBar calls reuse
        // these pointers without re-scanning. Resolved-texture ptrs are
        // stable within a session.
        bool      m_nbbAssetsCached{false};
        UObject*  m_nbbCachedSlotEmpty{nullptr};
        UObject*  m_nbbCachedSlotFocus{nullptr};
        UObject*  m_nbbCachedSlotCorners{nullptr};
        UObject*  m_nbbCachedBarFrame{nullptr};
        UObject*  m_nbbCachedKeyBg{nullptr};
        UObject*  m_nbbCachedTexChromeTop{nullptr};
        UObject*  m_nbbCachedTexChromeMiddle{nullptr};
        UObject*  m_nbbCachedTexChromeBottom{nullptr};
        bool      m_nbbHudTexturesDumped{false};
        // v6.14.0 — Phase 2 highlight state. Per-slot deadline
        // (GetTickCount64 ms). When current tick > deadline, the slot's
        // focus overlay is hidden again. Zero means "not flashing".
        uint64_t  m_nbbHighlightUntil[8]{};
        UObject* m_mcSlotButtons[MC_SLOTS]{};     // UButton wrappers for gamepad navigation
        UObject* m_mcStateImages[MC_SLOTS]{};
        UObject* m_mcIconImages[MC_SLOTS]{};
        UmgSlotState m_mcSlotStates[MC_SLOTS]{};
        int m_mcFocusedSlot{-1};                  // currently focused MC slot for gamepad
        bool m_gameActionBarFocused{false};       // true when game's built-in action bar has gamepad focus
        int m_gameActionBarIndex{-1};             // current slot index in game's action bar
        int m_gameHotbarSize{9};                  // game's hotbar slot count (9 slots: 0-7 + epic item at 8)
        bool m_modToolbarFocused{false};          // true when our mod toolbar has gamepad focus
        int m_gpFlatIndex{0};                     // current position in flat slot list (all mod slots)
        ULONGLONG m_gpToggleTime{0};              // timestamp of last toggle for double-click detection
        bool m_gpAnyButtonPressed{false};         // true if any nav/action button pressed during mod mode
        int m_gpDismissCalloutFrame{0};           // countdown to send ESC after toggle (dismiss callout)

        // Controller settings (persisted to INI)
        enum class ControllerProfile : uint8_t { None = 0, Xbox = 1, PS5 = 2 };
        bool m_controllerEnabled{false};          // F12 checkbox: controller input active
        ControllerProfile m_controllerProfile{ControllerProfile::Xbox};  // Xbox or PS5
        DualSenseReader m_dsReader;               // PS5 DualSense raw HID reader (fallback)
        DSState m_dsState{};
        DSState m_dsPrevState{};
        DIGamepadReader m_diReader;               // DirectInput gamepad reader (works for ALL controllers)
        DIGamepadState m_diState{};               // current DirectInput state
        DIGamepadState m_diPrevState{};           // previous frame


        UObject* m_umgKeyLabels[8]{};
        UObject* m_umgKeyBgImages[8]{};
        UObject* m_mcKeyLabels[MC_SLOTS]{};
        UObject* m_mcKeyBgImages[MC_SLOTS]{};
        UObject* m_umgTexBlankRect{nullptr};
        UObject* m_mcRotationLabel{nullptr};
        UObject* m_mcSlot0Overlay{nullptr};
        UObject* m_mcSlot6Overlay{nullptr};
        UObject* m_mcSlot8Overlay{nullptr};

        UObject* m_fontTestWidget{nullptr};
        bool m_ftVisible{false};
        UObject* m_ftTabImages[6]{};
        UObject* m_ftTabLabels[6]{};
        UObject* m_ftTabActiveTexture{nullptr};
        UObject* m_ftTabInactiveTexture{nullptr};
        int m_ftSelectedTab{0};
        // v6.17.0 — FWeakObjectPtr migration. Settings panel rebuilds and
        // language changes destroy the underlying scrollbox; weak ref
        // returns nullptr automatically post-GC.
        FWeakObjectPtr m_ftScrollBox;
        UObject* m_ftTabContent[6]{};

        // v6.4.1 Cheats tab — widget refs for action buttons and toggle state
        UObject* m_ftCheatsUnlockBtnImg{nullptr};
        UObject* m_ftCheatsReadBtnImg{nullptr};

        // v6.4.1 Peace Mode — zero MaxSpawnLimit on AMorAISpawnManager to suppress new spawns.
        bool m_peaceModeEnabled{false};
        bool m_pendingPeaceMode{false};                // v6.4.4+ — loaded from INI, applied on char load
        float m_savedMaxSpawnLimit{-1.0f};             // -1 = not yet captured
        UObject* m_ftPeaceCheckImg{nullptr};           // check icon (visible when enabled)
        UObject* m_ftPeaceBtnLabel{nullptr};           // button text ("PEACE"/"FIGHT")
        UObject* m_ftPeaceCheckBoxOl{nullptr};         // checkbox hit-test ref
        UObject* m_ftPeaceBtnImg{nullptr};             // button hit-test ref

        // v6.4.1 Cheats tab buff toggles — parallel arrays to cheatEntries().
        std::vector<bool>      m_buffStates;             // active/inactive state per entry
        std::vector<UObject*>  m_ftBuffCheckImgs;        // checkbox mark widget per entry
        std::vector<UObject*>  m_ftBuffBtnLabels;        // ON/OFF label per entry (nullptr for headers)
        std::vector<int>       m_buffRowTopYs;           // row top-Y offset within the content VBox (px, at 1x scale)
        std::vector<int>       m_buffRowHeights;         // row height (px, at 1x scale)
        int m_cheatsContentTotalHeight{0};               // total rendered height of the cheat entries area

        // v6.4.1 Tweaks tab — parallel arrays to tweakEntries(). Each entry cycles through a set
        // of integer values; applyFieldTweak modifies all DataTable rows whose RowStruct has the field.
        std::vector<int>       m_tweakCurrentIdx;        // current index in cycleValues (0 = DEFAULT)
        std::vector<UObject*>  m_ftTweakBtnLabels;       // button text widget per entry
        std::vector<int>       m_tweakRowTopYs;          // row top-Y per entry (px, at 1x scale)
        std::vector<int>       m_tweakRowHeights;        // row height per entry (px, at 1x scale)
        // Key: (rowData pointer + fieldName), Value: original value before any tweak.
        // Used to restore DEFAULT and to compute multipliers from the original baseline.
        std::unordered_map<std::wstring, double> m_tweakOriginals;
        UObject* m_ftKeyBoxLabels[BIND_COUNT]{};
        // v6.17.0 — FWeakObjectPtr migration. Settings panel rebuild can
        // GC these checkbox images.
        FWeakObjectPtr m_ftCheckImages[BIND_COUNT];
        UObject* m_ftModBoxLabel{nullptr};

        UObject* m_ftControllerCheckImg{nullptr};
        UObject* m_ftControllerProfileLabel{nullptr};
        UObject* m_ftNoCollisionCheckImg{nullptr};
        UObject* m_ftNoCollisionLabel{nullptr};
        UObject* m_ftNoCollisionKeyLabel{nullptr};
        UObject* m_ftTrashCheckImg{nullptr};
        UObject* m_ftReplenishCheckImg{nullptr};
        UObject* m_ftRemoveAttrsCheckImg{nullptr};
        UObject* m_ftPitchCheckImg{nullptr};
        UObject* m_ftRollCheckImg{nullptr};
        UObject* m_ftRenameWidget{nullptr};
        UObject* m_ftRenameInput{nullptr};
        UObject* m_ftRenameConfirmLabel{nullptr};
        bool m_ftRenameVisible{false};
        UObject* m_trashDlgWidget{nullptr};
        bool m_trashDlgVisible{false};
        ULONGLONG m_trashDlgOpenTick{0};

        UObject* m_ftRemovalVBox{nullptr};
        UObject* m_ftRemovalHeader{nullptr};
        int m_ftLastRemovalCount{-1};

        static constexpr int MAX_GAME_MODS = 16;
        // v6.17.0 — FWeakObjectPtr migration; settings panel rebuild.
        FWeakObjectPtr m_ftGameModCheckImages[MAX_GAME_MODS];
        std::vector<GameModEntry> m_ftGameModEntries;


        UObject* m_abBarWidget{nullptr};
        UObject* m_abSlotButton{nullptr};          // UButton wrapper for gamepad navigation
        UObject* m_abKeyLabel{nullptr};
        UObject* m_abStateImage{nullptr};
        bool m_toolbarsVisible{false};
        bool m_characterHidden{false};
        bool m_flyMode{false};
        bool m_snapEnabled{true};
        float m_savedMaxSnapDistance{-1.0f};
        bool m_noCollisionWhileFlying{false};
        bool m_trashItemEnabled{true};
        bool m_replenishItemEnabled{true};
        bool m_removeAttrsEnabled{true};
        bool m_pitchRotateEnabled{true};
        bool m_rollRotateEnabled{true};
        bool m_buildMenuPrimed{false};
        bool m_buildTabAfterShowFired{false};


        ScreenCoords m_screen;


        bool m_repositionMode{false};
        int  m_dragToolbar{-1};
        float m_dragOffsetX{0}, m_dragOffsetY{0};
        UClass* m_wllClass{nullptr};
        // v6.17.0 — FWeakObjectPtr migration. These widgets can outlive
        // their parent across world transitions / panel rebuilds; the
        // weak ref returns nullptr automatically when the underlying
        // UObject is GC'd, eliminating the stale-pointer crash class.
        FWeakObjectPtr m_repositionMsgWidget;
        FWeakObjectPtr m_repositionInfoBoxWidget;

        static constexpr int TB_COUNT = 4;
        float m_toolbarPosX[TB_COUNT]{-1, -1, -1, -1};
        float m_toolbarPosY[TB_COUNT]{-1, -1, -1, -1};

        float m_toolbarSizeW[TB_COUNT]{0, 0, 0, 0};
        float m_toolbarSizeH[TB_COUNT]{0, 0, 0, 0};

        static constexpr float TB_DEF_X[TB_COUNT]{0.4992f, 0.7505f, 0.8492f, 0.9414f};
        static constexpr float TB_DEF_Y[TB_COUNT]{0.0287f, 0.9111f, 0.6148f, 0.5463f};

        int m_hoveredToolbar{-1};
        int m_hoveredSlot{-1};
        bool m_lastClickLMB{false};
        FBoolProperty* m_bpShowMouseCursor{nullptr};

        UClass* m_lastPickedUpItemClass{nullptr};
        std::wstring m_lastPickedUpItemName;
        std::wstring m_lastPickedUpDisplayName;
        int32_t m_lastPickedUpCount{0};
        uint8_t m_lastItemHandle[20]{};
        RC::Unreal::FWeakObjectPtr m_lastItemInvComp;
        bool m_trashCursorWasVisible{false};

        UObject* m_targetInfoWidget{nullptr};
        UObject* m_tiTitleLabel{nullptr};
        UObject* m_tiClassLabel{nullptr};
        UObject* m_tiNameLabel{nullptr};
        UObject* m_tiDisplayLabel{nullptr};
        UObject* m_tiPathLabel{nullptr};
        UObject* m_tiBuildLabel{nullptr};
        UObject* m_tiRecipeLabel{nullptr};
        ULONGLONG m_tiShowTick{0};

        UObject* m_crosshairWidget{nullptr};
        ULONGLONG m_crosshairShowTick{0};
        static constexpr ULONGLONG CROSSHAIR_FADE_MS = 40000;

        UObject* m_errorBoxWidget{nullptr};
        UObject* m_ebMessageLabel{nullptr};
        ULONGLONG m_ebShowTick{0};
        static constexpr ULONGLONG ERROR_BOX_DURATION_MS = 5000;


        ULONGLONG m_auditClearTime{0};
        struct AuditLoc { float x, y, z; bool critical; };
        std::vector<AuditLoc> m_auditLocations;
        std::vector<RC::Unreal::FWeakObjectPtr> m_auditSpawnedActors;

        #include "moria_placement.inl"
        #include "moria_quickbuild.inl"

        #include "moria_widgets.inl"

        #include "moria_widget_harvest.inl"

        #include "moria_session_history.inl"

        #include "moria_join_world_ui.inl"

        #include "moria_advanced_join_ui.inl"

        #include "moria_settings_ui.inl"

        #include "moria_overlay_mgmt.inl"

      public:


        MoriaCppMod()
        {
            ModVersion = STR("6.21.0");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("Advanced builder, HISM removal, quick-build hotbar, UMG config menu");

            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            VLOG(STR("[MoriaCppMod] Loaded v6.21.0\n"));
        }

        ~MoriaCppMod() override
        {

            s_instance = nullptr;

            stopOverlay();
            if (s_config.removalCSInit)
            {
                DeleteCriticalSection(&s_config.removalCS);
                s_config.removalCSInit = false;
            }
        }


        auto on_unreal_init() -> void override
        {
            // Resolve UE4SS working directory for all file I/O (process CWD may differ)
            {
                auto wd = UE4SSProgram::get_program().get_working_directory();
                // Convert wchar_t path to UTF-8 std::string.
                // The old static_cast<char> approach truncated Unicode characters —
                // the ™ in Steam's folder name (U+2122) became 0x22 (double-quote),
                // corrupting the path and silently breaking ALL file I/O.
                s_ue4ssWorkDir.clear();
                if (!wd.empty())
                {
                    int needed = WideCharToMultiByte(CP_UTF8, 0, wd.c_str(), static_cast<int>(wd.size()), nullptr, 0, nullptr, nullptr);
                    if (needed > 0)
                    {
                        s_ue4ssWorkDir.resize(needed);
                        WideCharToMultiByte(CP_UTF8, 0, wd.c_str(), static_cast<int>(wd.size()), s_ue4ssWorkDir.data(), needed, nullptr, nullptr);
                    }
                }
                if (!s_ue4ssWorkDir.empty() && s_ue4ssWorkDir.back() != '\\' && s_ue4ssWorkDir.back() != '/')
                    s_ue4ssWorkDir += '/';
            }

            loadConfig();
            VLOG(STR("[MoriaCppMod] Loaded v6.21.0 (workDir={})\n"),
                 utf8PathToWide(s_ue4ssWorkDir));

            // v6.4.4 — startup diagnostics for Steam ™ path troubleshooting.
            // These tell the user exactly which paths the mod is trying to read and whether
            // they exist. If the log shows a path containing â„¢ or other mangled chars, the
            // wide-path fix didn't take effect (wrong DLL loaded, or Windows still coerced).
            {
                std::string gmIni = modPath("Mods/GameMods.ini");
                std::string defs  = modPath("Mods/MoriaCppMod/definitions");
                DWORD gmAttr = GetFileAttributesW(utf8PathToWide(gmIni).c_str());
                DWORD dfAttr = GetFileAttributesW(utf8PathToWide(defs).c_str());
                VLOG(STR("[MoriaCppMod] [Diag] GameMods.ini path = '{}' (attrs={:#x})\n"),
                     utf8PathToWide(gmIni), gmAttr);
                VLOG(STR("[MoriaCppMod] [Diag] definitions dir  = '{}' (attrs={:#x})\n"),
                     utf8PathToWide(defs), dfAttr);
                if (gmAttr == INVALID_FILE_ATTRIBUTES)
                    VLOG(STR("[MoriaCppMod] [Diag] GameMods.ini NOT found (GLE={})\n"), GetLastError());
                if (dfAttr == INVALID_FILE_ATTRIBUTES)
                    VLOG(STR("[MoriaCppMod] [Diag] definitions dir NOT found (GLE={})\n"), GetLastError());
            }


            Loc::load(modPath("Mods/MoriaCppMod/localization/"), s_language);

            s_bindings[0].label = Loc::get("bind.quick_build_1");
            s_bindings[0].section = Loc::get("bind.section_quick_building");
            s_bindings[1].label = Loc::get("bind.quick_build_2");
            s_bindings[1].section = Loc::get("bind.section_quick_building");
            s_bindings[2].label = Loc::get("bind.quick_build_3");
            s_bindings[2].section = Loc::get("bind.section_quick_building");
            s_bindings[3].label = Loc::get("bind.quick_build_4");
            s_bindings[3].section = Loc::get("bind.section_quick_building");
            s_bindings[4].label = Loc::get("bind.quick_build_5");
            s_bindings[4].section = Loc::get("bind.section_quick_building");
            s_bindings[5].label = Loc::get("bind.quick_build_6");
            s_bindings[5].section = Loc::get("bind.section_quick_building");
            s_bindings[6].label = Loc::get("bind.quick_build_7");
            s_bindings[6].section = Loc::get("bind.section_quick_building");
            s_bindings[7].label = Loc::get("bind.quick_build_8");
            s_bindings[7].section = Loc::get("bind.section_quick_building");
            s_bindings[8].label = Loc::get("bind.rotation");
            s_bindings[8].section = Loc::get("bind.section_mod_controller");
            s_bindings[9].label = Loc::get("bind.snap_off");
            s_bindings[9].section = Loc::get("bind.section_mod_controller");
            s_bindings[10].label = Loc::get("bind.integrity_check");
            s_bindings[10].section = Loc::get("bind.section_mod_controller");
            s_bindings[11].label = Loc::get("bind.mod_menu_4");
            s_bindings[11].section = Loc::get("bind.section_mod_controller");
            s_bindings[12].label = Loc::get("bind.target");
            s_bindings[12].section = Loc::get("bind.section_mod_controller");
            s_bindings[13].label = Loc::get("bind.configuration");
            s_bindings[13].section = Loc::get("bind.section_mod_controller");
            s_bindings[14].label = Loc::get("bind.remove_single");
            s_bindings[14].section = Loc::get("bind.section_mod_controller");
            s_bindings[15].label = Loc::get("bind.undo_last");
            s_bindings[15].section = Loc::get("bind.section_mod_controller");
            s_bindings[16].label = Loc::get("bind.remove_all");
            s_bindings[16].section = Loc::get("bind.section_mod_controller");
            s_bindings[17].label = Loc::get("bind.ab_open");
            s_bindings[17].section = Loc::get("bind.section_advanced_builder");
            s_bindings[18].label = L"Reserved";
            s_bindings[18].section = L"Reserved";
            s_bindings[19].label = Loc::get("bind.trash_item");
            s_bindings[19].section = Loc::get("bind.section_game_options");
            s_bindings[20].label = Loc::get("bind.replenish_item");
            s_bindings[20].section = Loc::get("bind.section_game_options");
            s_bindings[21].label = Loc::get("bind.remove_attrs");
            s_bindings[21].section = Loc::get("bind.section_game_options");

            CONFIG_TAB_NAMES[0] = Loc::get("tab.optional_mods").c_str();
            CONFIG_TAB_NAMES[1] = Loc::get("tab.key_mapping").c_str();
            CONFIG_TAB_NAMES[2] = Loc::get("tab.hide_environment").c_str();

            m_saveFilePath = modPath("Mods/MoriaCppMod/removed_instances.txt");
            loadSaveFile();
            buildRemovalEntries();
            probePrintString();
            loadQuickBuildSlots();


            const Input::Key fkeys[] = {Input::Key::F1, Input::Key::F2, Input::Key::F3, Input::Key::F4, Input::Key::F5, Input::Key::F6, Input::Key::F7, Input::Key::F8};
            for (int i = 0; i < 8; i++)
            {
                // USE chord (F-key alone). v6.9.0 CP3 — gate: bail if ANY
                // modifier is held, since SET dispatch (chord polling in
                // gameThreadTick) handles modifier+key combos.
                register_keydown_event(fkeys[i], [this, i]() {
                    if (m_ftVisible || isSettingsScreenOpen() || !s_bindings[i].enabled) return;
                    if (m_handleResolvePhase != HandleResolvePhase::Done) return;
                    if ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) ||
                        (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
                        (GetAsyncKeyState(VK_MENU)    & 0x8000)) return;
                    quickBuildSlot(i);
                });
                // Legacy SHIFT/CTRL/ALT registrations removed — chord-aware
                // polling in gameThreadTick handles SET dispatch.
            }


            register_keydown_event(Input::Key::MULTIPLY, [this]() {
                if (m_ftVisible) return;
                m_showHotbar = !m_showHotbar;
                s_overlay.visible = m_showHotbar && m_gameHudVisible;
                s_overlay.needsUpdate = true;
                showOnScreen(m_showHotbar ? Loc::get("msg.hotbar_overlay_on") : Loc::get("msg.hotbar_overlay_off"), 2.0f, 0.2f, 0.8f, 1.0f);
            });


            register_keydown_event(Input::Key::NUM_SEVEN, [this]() { if (m_ftVisible) return; createModControllerBar(); });

            // v6.4.1 — Recipe unlock + Mark-all-read features moved to the F12 Cheats tab.
            // See moria_unlock.inl for unlockAllAvailableRecipes() + markAllLoreRead() entry points.
            // Keybinds removed; buttons in F12 > Cheats tab are the only entry now.

            // Pitch/Roll rotation — uses keybind system (BIND_PITCH_ROTATE, BIND_ROLL_ROTATE)
            // Registered dynamically after config load via registerPitchRollKeys()

            s_instance = this;

            // MP helper: check if a UWidget context belongs to the local player.
            // Uses UWidget::GetOwningPlayer() UFUNCTION and compares against findPlayerController().
            // Returns true if context is local or if we can't determine ownership (fail-open for single-player).
            // MP helper: check if a UObject context belongs to the local player.
            // Uses cached m_localPC/m_localPawn (set at character load) — no FindAllOf or
            // ProcessEvent re-entrancy on the hot path. Walks the Outer chain to match ownership.
            // Returns true if local or indeterminate (fail-open for single-player safety).
            static auto isLocalContext = [](UObject* context) -> bool {
                if (!context || !s_instance) return true;
                if (!s_instance->m_localPC) return true;  // not cached yet — fail-open

                // Walk the Outer chain looking for local PC or local pawn
                UObject* outer = context;
                int depth = 0;
                while (outer && depth < 20)  // depth limit prevents infinite loops
                {
                    if (outer == s_instance->m_localPC) return true;
                    if (s_instance->m_localPawn && outer == s_instance->m_localPawn) return true;
                    outer = outer->GetOuterPrivate();
                    depth++;
                }

                return false;  // context doesn't belong to local player
            };

            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;


                const auto fnName = func->GetName();
                const wchar_t* fnStr = fnName.c_str();

                // CP5 v0.5 — pre-hook WBP_SettingsScreen.PreConstruct or
                // Construct or OnInitialized. Append Cheats to tabArray
                // BEFORE the navbar's Construct runs (navbar reads tabArray
                // directly during its own Construct).
                // v6.16.0 — Removed: PE pre-hook on PreConstruct/Construct/
                // OnInitialized of WBP_SettingsScreen_C used to call
                // appendCheatsTabToArray here. The function was stubbed in
                // v6.14.0 (Cheats merged into Gameplay tab in v0.48), so
                // the entire pre-hook branch was a no-op.
                // CP5 v0.7 — when user clicks the Cheats tab in the navbar,
                // redirect the tab name to "Gameplay" so the framework
                // displays the Gameplay tab content. Set a flag so the
                // Gameplay tab's OnAfterShow hook injects cheats content
                // instead of (or in addition to) Mod Game Options.
                if (wcscmp(fnStr, STR("navTabPressed")) == 0 && parms)
                {
                    s_instance->onNavTabPressedPre(context, func, parms);
                }
                // v6.16.0 — Removed: legacy v0.4 NavBar pre-hook for
                // onInitializeNavBarPre (Cheats-tab insertion path that
                // was superseded by Option C in v0.48). Function deleted
                // in this same version.
                // Diagnostic: log all UFunctions called on the navbar class
                // (one-shot per name) so we can find the function that
                // populates tabs even if its name differs from expected.
                {
                    static std::set<std::wstring> s_seenNavbarFns;
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_NavBar_Build_C") &&
                        s_seenNavbarFns.size() < 50 &&
                        s_seenNavbarFns.insert(fnStr).second)
                    {
                        VLOG(STR("[NavBarDiag] PE-pre on NavBar class: '{}'\n"), fnStr);
                    }
                }

                // Suppress server movement corrections when fly mode is active
                // This prevents the server from forcing us back to walking/falling
                if (s_instance->m_flyMode &&
                    (wcscmp(fnStr, STR("ClientAdjustPosition")) == 0 ||
                     wcscmp(fnStr, STR("ClientAdjustPosition_Implementation")) == 0 ||
                     wcscmp(fnStr, STR("ClientVeryShortAdjustPosition")) == 0))
                {
                    if (parms && func->GetParmsSize() > 0)
                        std::memset(parms, 0, func->GetParmsSize());
                    VLOG(STR("[MoriaCppMod] [Fly] SUPPRESSED {} (flyMode=ON)\n"), fnStr);
                    return;
                }

                // When mod toolbar is active, suppress ALL game functions triggered by gamepad.
                if (s_instance->m_modToolbarFocused)
                {
                    // Log ALL ProcessEvent calls while in mod mode (first 50 unique names)
                    // to find what L1/R1 triggers so we can suppress it
                    {
                        static std::set<std::wstring> s_loggedFns;
                        if (s_loggedFns.size() < 50)
                        {
                            std::wstring fn2(fnStr);
                            if (s_loggedFns.find(fn2) == s_loggedFns.end())
                            {
                                std::wstring cls = safeClassName(context);
                                VLOG(STR("[MoriaCppMod] [ModMode-PE] fn='{}' cls='{}'\n"), fnStr, cls);
                                s_loggedFns.insert(fn2);
                            }
                        }
                    }

                    // Suppress specific input/action bar functions.
                    // Do NOT use substring matching — zeroing params for functions
                    // like SetCalloutTargetText crashes (null FText → null deref).
                    bool suppress =
                        wcscmp(fnStr, STR("HUD Focus From Controller")) == 0 ||
                        wcscmp(fnStr, STR("HotBarActionRequest")) == 0 ||
                        wcscmp(fnStr, STR("ProcessHotbarAction")) == 0 ||
                        wcscmp(fnStr, STR("Navigate To Epic Item")) == 0 ||
                        wcscmp(fnStr, STR("NavigateToEpicItem")) == 0 ||
                        wcscmp(fnStr, STR("ToggleGamepadNavIcons")) == 0 ||
                        wcscmp(fnStr, STR("OnEmoteMenuFocusChanged")) == 0 ||
                        wcscmp(fnStr, STR("CallEmote")) == 0 ||
                        wcscmp(fnStr, STR("Input_AnyKey")) == 0 ||
                        wcscmp(fnStr, STR("OnInputDeviceChanged")) == 0 ||
                        wcscmp(fnStr, STR("OnBindInputs")) == 0 ||
                        wcscmp(fnStr, STR("OnInputChanged")) == 0;
                    if (suppress)
                    {
                        if (parms && func->GetParmsSize() > 0)
                            std::memset(parms, 0, func->GetParmsSize());
                        return;
                    }
                }

                // Also suppress emote menu when D-pad Left is used as toggle
                // (prevents callout from opening on the toggle press itself)
                if (s_instance->m_controllerEnabled &&
                    (wcscmp(fnStr, STR("OnEmoteMenuFocusChanged")) == 0 ||
                     wcscmp(fnStr, STR("CallEmote")) == 0))
                {
                    // Check if D-pad Left is currently held (toggle button)
                    // For Xbox: check XInput directly. For PS5: always suppress since
                    // we need IsInputKeyDown to work without emote stealing focus.
                    bool suppressEmote = false;
                    if (s_instance->m_controllerProfile == ControllerProfile::Xbox)
                    {
                        XINPUT_STATE xs{};
                        if (XInputGetState(0, &xs) == ERROR_SUCCESS)
                            suppressEmote = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
                    }
                    else
                    {
                        // PS5: always suppress emote — D-pad Left is our toggle
                        suppressEmote = true;
                    }
                    if (suppressEmote)
                    {
                        if (parms && func->GetParmsSize() > 0)
                            std::memset(parms, 0, func->GetParmsSize());
                        return;
                    }
                }

                if (wcscmp(fnStr, STR("RotatePressed")) == 0 || wcscmp(fnStr, STR("RotateCcwPressed")) == 0)
                {
                    if (s_instance->m_isDedicatedServer) return;
                    // isLocalContext removed — BuildHUD is a UMG widget whose Outer chain
                    // goes to WidgetTree/GameInstance, not PC/Pawn. On a client only the
                    // local player's BuildHUD exists, so no cross-player bleed.
                    std::wstring cls = safeClassName(context);
                    if (!cls.empty() && cls.find(STR("BuildHUD")) != std::wstring::npos)
                    {
                        UObject* gata = s_instance->resolveGATA();
                        if (gata)
                        {
                            const int step = s_overlay.rotationStep.load();
                            s_instance->setGATARotation(gata, static_cast<float>(step));
                            if (wcscmp(fnStr, STR("RotatePressed")) == 0)
                                s_overlay.totalRotation = (s_overlay.totalRotation.load() + step) % 360;
                            else
                                s_overlay.totalRotation = (s_overlay.totalRotation.load() - step + 360) % 360;
                            s_overlay.needsUpdate = true;
                            s_instance->updateMcRotationLabel();
                        }
                    }
                }
                else if (wcscmp(fnStr, STR("BuildNewConstruction")) == 0)
                {
                    // MP guard: only patch local player's builds (server has no pitch/roll state)
                    if (!s_instance->m_isDedicatedServer)
                        s_instance->onBuildNewConstruction(context, func, parms);
                    if (!s_instance->m_snapEnabled)
                    {
                        VLOG(STR("[MoriaCppMod] [Snap] Placement detected (BuildNewConstruction), auto-restoring snap\n"));
                        s_instance->restoreSnap();
                    }
                }
                else if (!s_instance->m_snapEnabled &&
                         (wcscmp(fnStr, STR("BuildConstruction")) == 0 || wcscmp(fnStr, STR("TryBuild")) == 0))
                {
                    VLOG(STR("[MoriaCppMod] [Snap] Placement detected ({}), auto-restoring snap\n"), fnStr);
                    s_instance->restoreSnap();
                }

            });


            Unreal::Hook::RegisterProcessEventPostCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance || !func) return;

                const auto fn = func->GetName();
                const wchar_t* fnStr2 = fn.c_str();

                // MP guard: skip UI/state hooks on dedicated server — each client has its own mod instance
                // Only OnPlayerEnteredBubble is allowed through (useful for server-side bubble tracking)
                if (s_instance->m_isDedicatedServer)
                {
                    if (wcscmp(fnStr2, STR("OnPlayerEnteredBubble")) == 0 && parms)
                    {
                        auto* bubble = *reinterpret_cast<UObject**>(static_cast<uint8_t*>(parms) + 8);
                        if (bubble && isObjectAlive(bubble))
                            s_instance->onBubbleEnteredEvent(bubble);
                    }
                    return;
                }

                // GenericPopup Confirm/Cancel detection. The actual click event
                // is `OnButtonReleasedEvent` on the WBP_FrontEndButton instance
                // (NOT OnMenuButtonClicked). We compare the firing button to
                // the popup's ConfirmButton / CancelButton members.
                if (wcsstr(fnStr2, STR("OnButtonReleasedEvent")) != nullptr ||
                    wcsstr(fnStr2, STR("OnMenuButtonClicked"))   != nullptr ||
                    wcsstr(fnStr2, STR("OnButtonPressedEvent"))  != nullptr ||
                    wcscmp(fnStr2, STR("OnClicked")) == 0)
                {
                    s_instance->onAnyMenuButtonClicked(context, fnStr2);
                    s_instance->maybeFireCarouselButton(context);
                    // v0.35 — BndEvt_..._{Prev,Next}Button_..._OnButton...
                    // delegates fire on the carousel itself; the fn name
                    // contains "PrevButton" or "NextButton".
                    s_instance->maybeFireCarouselViaDelegate(context, fnStr2);
                }

                // v0.32 — Native settings checkbox state-change.
                // BP delegate name: BndEvt__WBP_SettingsCheckBox_OptionCheckBox_K2Node_..._OnCheckBoxComponentStateChanged__DelegateSignature
                // The C++-level event UMorSettingsCheckBox::OnCheckBoxStateChanged is also fine.
                if ((wcsstr(fnStr2, STR("OnCheckBoxComponentStateChanged")) != nullptr ||
                     wcscmp(fnStr2, STR("OnCheckBoxStateChanged")) == 0) && parms)
                {
                    bool newState = false;
                    newState = *reinterpret_cast<bool*>(parms);
                    s_instance->maybeFireCheckBoxRow(context, newState);
                }

                // v0.33 — Native settings carousel value changed.
                // Delegate signature: CarouselValueChanged(FString SelectedValue)
                if (wcscmp(fnStr2, STR("CarouselValueChanged")) == 0 && parms)
                {
                    FString* fs = reinterpret_cast<FString*>(parms);
                    std::wstring val;
                    try {
                        if (fs && fs->GetCharArray().GetData())
                            val = std::wstring(fs->GetCharArray().GetData());
                    } catch (...) {}
                    s_instance->maybeFireCarouselRow(context, val);
                }

                // CP5 v0.12 — post-hook on navTabPressed. Pre-hook
                // rewrites KeyName "Cheats" -> "Gameplay" so the framework
                // displays the Gameplay widget. After navTabPressed runs,
                // the framework has set currentTabName + highlighted the
                // Gameplay button. We patch currentTabName back to "Cheats"
                // and force the navbar to refresh selection so the visual
                // highlight matches what the user clicked.
                if (wcscmp(fnStr2, STR("navTabPressed")) == 0 && parms)
                {
                    s_instance->onNavTabPressedPost(context, func, parms);
                }

                // Diagnostic popup-trace — gated behind s_verbose AND filters
                // BEFORE the expensive Outer-chain walk, so production builds
                // pay nothing here. The Outer walk only runs when (a) the
                // popup is up, (b) the fn name passes the noise filter, AND
                // (c) verbose logging is on. Original unfiltered version flooded
                // ProcessEvent + caused IsInViewport-style input interference.
                if (s_verbose &&
                    s_instance->m_pendingDeletePopup.Get() != nullptr &&
                    wcscmp(fnStr2, STR("IsInViewport")) != 0 &&
                    wcscmp(fnStr2, STR("IsHovered"))    != 0 &&
                    wcscmp(fnStr2, STR("Tick"))         != 0 &&
                    wcscmp(fnStr2, STR("OnMouseMove"))  != 0 &&
                    wcscmp(fnStr2, STR("OnMouseEnter")) != 0 &&
                    wcscmp(fnStr2, STR("OnMouseLeave")) != 0 &&
                    wcsstr(fnStr2, STR("ReceiveTick"))  == nullptr &&
                    wcsstr(fnStr2, STR("SetColor"))     == nullptr &&
                    wcsstr(fnStr2, STR("SetContentColor")) == nullptr)
                {
                    UObject* popupW = s_instance->m_pendingDeletePopup.Get();
                    bool isOnPopup = (context == popupW);
                    if (!isOnPopup && context && popupW && isObjectAlive(context))
                    {
                        UObject* o = context;
                        for (int i = 0; i < 4 && o; ++i)  // walk capped at 4 (was 6)
                        {
                            try { o = o->GetOuterPrivate(); } catch (...) { break; }
                            if (o == popupW) { isOnPopup = true; break; }
                        }
                    }
                    if (isOnPopup)
                    {
                        VLOG(STR("[SessionHistory] popup-trace fn='{}' ctx-cls='{}'\n"),
                             fnStr2, safeClassName(context).c_str());
                    }
                }

                // Manual Direct/Local Join button click — fires the BndEvt
                // delegate handler on the AdvancedJoinOptions widget. We
                // DEFER reading the input field text to the next main tick
                // (queueManualJoinCapture) — calling GetText + Conv_TextToString
                // from inside this post-hook would re-enter ProcessEvent and is
                // a documented reentrancy hazard.
                if ((wcsstr(fnStr2, STR("Button_DirectJoinIP")) != nullptr ||
                     wcsstr(fnStr2, STR("Button_JoinLocal")) != nullptr) &&
                    wcsstr(fnStr2, STR("OnMenuButtonClicked")) != nullptr)
                {
                    bool isLocal = (wcsstr(fnStr2, STR("Button_JoinLocal")) != nullptr);
                    s_instance->queueManualJoinCapture(context, isLocal);
                }

                // Hook BP-level join events. We listen for any UFunction whose
                // name contains a Join keyword as a coarse first filter, then
                // narrow by signature inside. Earlier we tried direct C++
                // function names (DirectJoinSessionWithPassword) but those may
                // not be UFunctions exposed to ProcessEvent. The BP events are
                // always UFunctions and reliably fire.
                if (parms && (wcsstr(fnStr2, STR("JoinSession")) != nullptr ||
                              wcsstr(fnStr2, STR("DirectJoin")) != nullptr ||
                              wcsstr(fnStr2, STR("TryJoinPreviousSession")) != nullptr ||
                              wcsstr(fnStr2, STR("OnJoinSessionHistoryItemPressed")) != nullptr ||
                              wcsstr(fnStr2, STR("JoinByIP_Pressed")) != nullptr ||
                              wcsstr(fnStr2, STR("JoinLocalDedicatedServer_Pressed")) != nullptr))
                {
                    if (s_verbose)
                    {
                        VLOG(STR("[SessionHistory] join-related fn fired: '{}' on cls='{}'\n"),
                             fnStr2, safeClassName(context).c_str());
                    }

                    // Try to read FMorConnectionHistoryItem from parms (BP delegates
                    // pass it as the only argument). Layout offsets relative to parm
                    // base: WorldName@0x00, InviteString@0x18, OptionalPassword@0x38.
                    // If the function's first param is named differently we still
                    // read those offsets blindly — it's safe since we're reading
                    // FString members which are always 16 bytes.
                    auto* p = func->GetChildProperties();
                    int32_t baseOff = -1;
                    bool isHistoryItem = false;
                    if (auto* firstProp = func->GetPropertyByNameInChain(STR("Connection History Item Data")))
                    { baseOff = firstProp->GetOffset_Internal(); isHistoryItem = true; }
                    else if (auto* alt = func->GetPropertyByNameInChain(STR("ConnectionHistoryItemData")))
                    { baseOff = alt->GetOffset_Internal(); isHistoryItem = true; }
                    else if (auto* alt2 = func->GetPropertyByNameInChain(STR("ConnectionHistoryData")))
                    { baseOff = alt2->GetOffset_Internal(); isHistoryItem = true; }
                    (void)p;

                    auto fStringToUtf8Str = [](FString* fs) -> std::string {
                        if (!fs) return "";
                        const wchar_t* w = fs->GetCharArray().GetData();
                        if (!w || !*w) return "";
                        int wlen = (int)wcslen(w);
                        int u8 = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
                        std::string out(u8, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, w, wlen, &out[0], u8, nullptr, nullptr);
                        return out;
                    };

                    if (isHistoryItem && baseOff >= 0)
                    {
                        uint8_t* hist = static_cast<uint8_t*>(parms) + baseOff;
                        FString* worldName  = reinterpret_cast<FString*>(hist + 0x00);
                        FString* inviteStr  = reinterpret_cast<FString*>(hist + 0x18);
                        FString* optPwd     = reinterpret_cast<FString*>(hist + 0x38);
                        std::string nameStr = fStringToUtf8Str(worldName);
                        std::string hostStr = fStringToUtf8Str(inviteStr);
                        std::string passStr = fStringToUtf8Str(optPwd);

                        std::string domain = hostStr, port;
                        size_t colon = hostStr.rfind(':');
                        if (colon != std::string::npos && colon > 0 && colon < hostStr.size() - 1)
                        {
                            std::string tail = hostStr.substr(colon + 1);
                            bool allDigits = !tail.empty();
                            for (char c : tail) if (c < '0' || c > '9') { allDigits = false; break; }
                            if (allDigits) { domain = hostStr.substr(0, colon); port = tail; }
                        }

                        SessionHistoryEntry entry;
                        entry.name     = nameStr.empty() ? hostStr : nameStr;
                        entry.domain   = domain;
                        entry.port     = port;
                        entry.password = passStr;
                        entry.lastJoined = "";
                        s_instance->addOrUpdateSessionHistory(entry);
                        VLOG(STR("[SessionHistory] saved (history-item) name='{}' host='{}' port='{}'\n"),
                             utf8ToWide(entry.name).c_str(),
                             utf8ToWide(entry.domain).c_str(),
                             utf8ToWide(entry.port).c_str());
                    }
                }

                // Original direct-call hook (kept as a fallback in case the
                // C++ implementation is also exposed as a UFunction).
                if (parms &&
                    (wcscmp(fnStr2, STR("DirectJoinSessionWithPassword")) == 0 ||
                     wcscmp(fnStr2, STR("DirectJoinLocalSessionWithPassword")) == 0))
                {
                    bool isLocal = (wcscmp(fnStr2, STR("DirectJoinLocalSessionWithPassword")) == 0);
                    auto* p1 = func->GetPropertyByNameInChain(STR("HostAndOptionalPort"));
                    if (!p1) p1 = func->GetPropertyByNameInChain(STR("PortString"));
                    auto* p2 = func->GetPropertyByNameInChain(STR("OptionalPassword"));
                    if (p1 && p2)
                    {
                        FString* host = reinterpret_cast<FString*>(
                            static_cast<uint8_t*>(parms) + p1->GetOffset_Internal());
                        FString* pass = reinterpret_cast<FString*>(
                            static_cast<uint8_t*>(parms) + p2->GetOffset_Internal());

                        // FString → std::string (UTF-8)
                        auto fStringToUtf8 = [](FString* fs) -> std::string {
                            if (!fs) return "";
                            const wchar_t* w = fs->GetCharArray().GetData();
                            if (!w || !*w) return "";
                            int wlen = (int)wcslen(w);
                            int u8len = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
                            std::string out(u8len, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, w, wlen, &out[0], u8len, nullptr, nullptr);
                            return out;
                        };

                        std::string hostStr = fStringToUtf8(host);
                        std::string passStr = fStringToUtf8(pass);

                        // Split host:port (if present)
                        std::string domain = hostStr, port;
                        size_t colon = hostStr.rfind(':');
                        if (colon != std::string::npos && colon > 0 && colon < hostStr.size() - 1)
                        {
                            // Avoid splitting IPv6-style strings: only split if
                            // everything after ':' is digits
                            std::string tail = hostStr.substr(colon + 1);
                            bool allDigits = !tail.empty();
                            for (char c : tail) if (c < '0' || c > '9') { allDigits = false; break; }
                            if (allDigits)
                            {
                                domain = hostStr.substr(0, colon);
                                port   = tail;
                            }
                        }
                        if (isLocal && port.empty()) { port = hostStr; domain = "127.0.0.1"; }

                        SessionHistoryEntry entry;
                        entry.name     = (isLocal ? "Local Server " : "Direct Join ") + hostStr;
                        entry.domain   = domain;
                        entry.port     = port;
                        entry.password = passStr;
                        entry.lastJoined = "";  // addOrUpdate fills with now
                        s_instance->addOrUpdateSessionHistory(entry);

                        VLOG(STR("[SessionHistory] hooked {} — captured host='{}', port='{}', pwd-len={}\n"),
                             fnStr2,
                             utf8ToWide(domain).c_str(),
                             utf8ToWide(port).c_str(),
                             (int)passStr.size());
                    }
                }

                if (wcscmp(fnStr2, STR("OnAfterShow")) == 0)
                {
                    // Popup tracker: log any widget whose class name contains
                    // Popup/Confirm/Dialog so we can discover the real class
                    // path of game-native confirmation popups (e.g. for our
                    // session-history delete confirm). User triggers quit-game
                    // confirm or similar, we log it.
                    std::wstring clsForPopup = safeClassName(context);
                    if (clsForPopup.find(STR("Popup")) != std::wstring::npos ||
                        clsForPopup.find(STR("PopUp")) != std::wstring::npos ||
                        clsForPopup.find(STR("Confirm")) != std::wstring::npos ||
                        clsForPopup.find(STR("Dialog")) != std::wstring::npos)
                    {
                        std::wstring path;
                        try
                        {
                            UClass* uc = static_cast<UClass*>(context->GetClassPrivate());
                            if (uc) path = uc->GetFullName();
                        } catch (...) {}
                        VLOG(STR("[PopupTracker] OnAfterShow on cls='{}' fullClassPath='{}'\n"),
                             clsForPopup.c_str(), path.c_str());
                    }
                    // isLocalContext removed — UMG widgets have WidgetTree Outer, not PC/Pawn
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        s_instance->m_buildTabAfterShowFired = true;
                        s_instance->m_buildMenuPrimed = true;
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow fired on Build_Tab\n"));

                        if (s_instance->m_qbPhase == PlacePhase::WaitingForShow)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow: recording settle time (350ms delay)\n"));
                            s_instance->m_showSettleTime = GetTickCount64();
                            // Don't transition yet — let placementTick handle after settle delay
                        }
                    }
                    // v6.6.0+ — intercept Join Other World screen and replace with mod-owned duplicate
                    else if (cls == STR("WBP_UI_JoinWorldScreen_C"))
                    {
                        s_instance->onNativeJoinWorldShown(context);
                    }
                    // Sister widget — AdvancedJoinOptions panel that opens when user
                    // clicks "Advanced Join Options" on the JoinWorld screen.
                    else if (cls == STR("WBP_UI_AdvancedJoinOptions_C"))
                    {
                        s_instance->onNativeAdvancedJoinShown(context);
                    }
                    // v6.9.0+ Settings take-over — capture every Settings-related
                    // widget that fires OnAfterShow so we can plan modifications.
                    // The main SettingsScreen also queues a tree dump.
                    else if (cls == STR("WBP_SettingsScreen_C") ||
                             cls == STR("UI_WBP_EscapeMenu2_C"))
                    {
                        s_instance->onNativeSettingsScreenShown(context);
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        // v6.16.0 — Removed: appendCheatsTabToArray call
                        // (function was a v6.14.0 stub since v0.48 merged
                        // Cheats into Gameplay tab). Function itself deleted
                        // in this version.
                        // v0.50 — inject mod action buttons (Rename/Save/Unlock/
                        // Read All/Clear All Buffs) into the pause menu's
                        // VerticalBox_0 right above LeaveButton.
                        if (cls == STR("UI_WBP_EscapeMenu2_C"))
                            s_instance->injectPauseMenuButtons(context);
                    }
                    // CP5 — when the cheats tab spawns its content widget, inject the cheats UI.
                    // Native legal tab spawns the same class but for a different tabArray index;
                    // injectCheatsTabContent uses the m_cheatsTabExpectedNext flag to differentiate.
                    else if (cls == STR("WBP_LegalTab_C"))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        s_instance->injectCheatsTabContent(context);
                    }
                    // v6.9.0 CP1 — inject mod keymap rows when the EditMappingTab
                    // becomes visible. This fires both on first Settings open
                    // and when the user clicks into the keymap navbar tab.
                    else if (cls == STR("WBP_EditMappingTab_C"))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        s_instance->injectModKeybindRows(context);
                    }
                    // v6.9.0 CP4 — inject Mod Game Options section.
                    else if (cls == STR("WBP_GameplayTab_C"))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        s_instance->injectModGameOptions(context);
                    }
                    else if (cls.find(STR("Tab_C")) != std::wstring::npos &&
                             (cls.find(STR("Settings")) != std::wstring::npos ||
                              cls.find(STR("Controls")) != std::wstring::npos ||
                              cls.find(STR("Mapping")) != std::wstring::npos ||
                              cls.find(STR("Audio"))    != std::wstring::npos ||
                              cls.find(STR("Video"))    != std::wstring::npos ||
                              cls.find(STR("Gameplay")) != std::wstring::npos ||
                              cls.find(STR("Accessibility")) != std::wstring::npos ||
                              cls.find(STR("Legal"))    != std::wstring::npos ||
                              cls.find(STR("Controller")) != std::wstring::npos))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                    }
                    return;
                }


                // v6.9.0 CP4 + v0.49 — Mod Game Options button clicks.
                // Catch both BP-level OnMenuButtonClicked AND the lower
                // BndEvt OnButtonReleasedEvent on FrontEndButton class.
                {
                    bool isMenuClick = wcscmp(fnStr2, STR("OnMenuButtonClicked")) == 0;
                    bool isFEReleased = (wcsstr(fnStr2, STR("OnButtonReleasedEvent")) != nullptr);
                    if (isMenuClick || isFEReleased)
                    {
                        // Diagnostic: log what's firing on what class.
                        static int s_clickDiagCount = 0;
                        if (s_clickDiagCount < 24) {
                            VLOG(STR("[CP4-CLICK] fn='{}' ctxCls='{}' ctx={:p}\n"),
                                 fnStr2,
                                 context ? safeClassName(context).c_str() : L"null",
                                 (void*)context);
                            ++s_clickDiagCount;
                        }
                        s_instance->onModGameOptionClicked(context);
                        s_instance->onCheatsTabButtonClicked(context);
                        if (isMenuClick) return;
                    }
                }

                // CP5 v0.3 — append Cheats entry to GetNavBarTabs OutArray.
                // The navbar UI calls this UFunction and renders the
                // returned array. Modifying tabArray alone isn't enough
                // because BP iterates the OutArray copy. Augmenting the
                // OutArray here makes every call see 8 tabs.
                if (wcscmp(fnStr2, STR("GetNavBarTabs")) == 0)
                {
                    s_instance->onGetNavBarTabsPost(context, func, parms);
                    return;
                }

                // v6.9.0 CP2 — capture rebinds done via the in-game keymap UI.
                // OnKeySelectedBP fires on a WBP_SettingsKeySelector_C after
                // the user picks a new chord. We persist to MoriaCppMod.ini
                // if the selector is one of ours.
                if (wcscmp(fnStr2, STR("OnKeySelectedBP")) == 0)
                {
                    std::wstring cls = safeClassName(context);
                    VLOG(STR("[SettingsUI] OnKeySelectedBP fired on cls='{}' obj={:p}\n"),
                         cls.c_str(), (void*)context);
                    if (cls.find(STR("KeySelector")) != std::wstring::npos)
                    {
                        // CP5 — first try cheats dispatch. Cheat rows are
                        // KeySelectors with our cheats label; clicks fire
                        // the cheat action and we IGNORE the captured key.
                        if (s_instance->maybeFireCheatFromSelector(context))
                            return;
                        s_instance->onModSelectorRebound(context);
                    }
                    return;
                }

                if (wcscmp(fnStr2, STR("OnAfterHide")) == 0)
                {
                    // Clear settings-screen-open gate when SettingsScreen
                    // hides so mod input handlers resume normal operation.
                    {
                        std::wstring cls2 = safeClassName(context);
                        if (cls2 == STR("WBP_SettingsScreen_C"))
                            s_instance->onNativeSettingsScreenHidden();
                    }
                    // isLocalContext removed — UMG widgets have WidgetTree Outer, not PC/Pawn
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_BuildHUDv2_C") || cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        QBLOG(STR("[MoriaCppMod] [Placement] OnAfterHide fired on {}\n"), cls);

                        if (s_instance->m_qbPhase == PlacePhase::CancelGhost)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterHide: ghost cancelled, activating build mode\n"));
                            if (s_instance->activateBuildMode())
                                s_instance->m_qbPhase = PlacePhase::WaitingForShow;

                        }
                        else
                        {
                            s_instance->onGhostDisappeared();
                        }
                    }
                    return;
                }


                // FreeCam enter: hide all mod toolbars
                if (wcscmp(fnStr2, STR("ExecuteUbergraph_WBP_FreeCamHUD")) == 0)
                {
                    if (!s_instance->m_inFreeCam)
                    {
                        s_instance->m_inFreeCam = true;
                        s_instance->m_gameHudVisible = false;
                        VLOG(STR("[MoriaCppMod] [HUD] Entered FreeCam — hiding toolbars\n"));
                        s_overlay.visible = false;
                        s_overlay.needsUpdate = true;
                        s_instance->setWidgetVisibility(s_instance->m_umgBarWidget, 1);
                        s_instance->setWidgetVisibility(s_instance->m_abBarWidget, 1);
                        s_instance->setWidgetVisibility(s_instance->m_mcBarWidget, 1);
                    }
                    return;
                }

                // FreeCam exit: restore all mod toolbars
                if (wcscmp(fnStr2, STR("OnCustomDisableCamera")) == 0)
                {
                    s_instance->m_inFreeCam = false;
                    s_instance->m_gameHudVisible = true;
                    VLOG(STR("[MoriaCppMod] [HUD] Exited FreeCam — restoring toolbars\n"));
                    if (s_instance->m_toolbarsVisible)
                    {
                        if (s_instance->m_showHotbar) { s_overlay.visible = true; s_overlay.needsUpdate = true; }
                        s_instance->setWidgetVisibility(s_instance->m_umgBarWidget, 0);
                        s_instance->setWidgetVisibility(s_instance->m_abBarWidget, 0);
                        s_instance->setWidgetVisibility(s_instance->m_mcBarWidget, 0);
                    }
                    return;
                }


                // Bubble change — fired by AWorldLayout's OnPlayerEnteredBubble delegate
                if (wcscmp(fnStr2, STR("OnPlayerEnteredBubble")) == 0)
                {
                    if (parms)
                    {
                        // Params: ACharacter* Character (offset 0), UWorldLayoutBubble* Bubble (offset 8)
                        auto* bubble = *reinterpret_cast<UObject**>(static_cast<uint8_t*>(parms) + 8);
                        if (bubble && isObjectAlive(bubble))
                        {
                            s_instance->onBubbleEnteredEvent(bubble);
                        }
                    }
                    return;
                }

                // Track game's action bar focus for gamepad toolbar bridging
                // Try multiple possible function names (C++ delegate vs Blueprint)
                if (wcscmp(fnStr2, STR("OnHUDActionBarFocusChanged")) == 0 ||
                    wcscmp(fnStr2, STR("HUD Focus From Controller")) == 0 ||
                    wcscmp(fnStr2, STR("HUDFocusFromController")) == 0)
                {
                    if (parms)
                    {
                        bool bFocused = *reinterpret_cast<bool*>(static_cast<uint8_t*>(parms));
                        int32_t index = *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(parms) + 4);
                        s_instance->m_gameActionBarFocused = bFocused;
                        s_instance->m_gameActionBarIndex = index;
                        VLOG(STR("[MoriaCppMod] [Gamepad] ActionBar hook '{}': focused={} index={}\n"),
                             fnStr2, bFocused ? 1 : 0, index);
                    }
                    return;
                }

                // Diagnostic: log any function with Focus/ActionBar/Hotbar in the name (first 20 only)
                {
                    static int s_focusDiagCount = 0;
                    if (s_focusDiagCount < 20)
                    {
                        std::wstring fn2(fnStr2);
                        if (fn2.find(STR("ocus")) != std::wstring::npos ||
                            fn2.find(STR("otbar")) != std::wstring::npos ||
                            fn2.find(STR("ction")) != std::wstring::npos)
                        {
                            std::wstring cls = safeClassName(context);
                            VLOG(STR("[MoriaCppMod] [Gamepad-Diag] fn='{}' cls='{}'\n"), fnStr2, cls);
                            s_focusDiagCount++;
                        }
                    }
                }

                if (wcscmp(fnStr2, STR("ServerMoveItem")) == 0 || wcscmp(fnStr2, STR("MoveSwapItem")) == 0 || wcscmp(fnStr2, STR("BroadcastToContainers_OnChanged")) == 0)
                {
                    if (parms && isLocalContext(context))  // MP: only capture local player's inventory
                    {
                        std::wstring cls = safeClassName(context);
                        if (cls == STR("MorInventoryComponent"))
                            s_instance->captureLastChangedItem(context, parms);
                    }
                    if (wcscmp(fnStr2, STR("BroadcastToContainers_OnChanged")) == 0) return;
                }

                if (s_instance->m_isAutoSelecting) return;

                if (wcscmp(fnStr2, STR("blockSelectedEvent")) != 0) return;
                // Note: isLocalContext removed here — Build_Tab is a client-side UMG widget
                // whose Outer chain leads to WidgetTree/GameInstance, not PC/Pawn.
                // On a client, only the local player's Build_Tab exists.
                std::wstring cls = safeClassName(context);
                if (cls != STR("UI_WBP_Build_Tab_C")) return;

                int sz = func->GetParmsSize();
                QBLOG(STR("[MoriaCppMod] [QB] POST-HOOK blockSelectedEvent: parmsSize={} parms={}\n"),
                      sz, parms ? STR("YES") : STR("NO"));
                if (!parms || sz < 132) return;
                uint8_t* p = reinterpret_cast<uint8_t*>(parms);


                resolveBSEOffsets(func);


                QBLOG(STR("[MoriaCppMod] [QB] POST-HOOK: s_bse.selfRef={} s_bse.bLock={}\n"),
                      s_bse.selfRef, s_bse.bLock);
                if (s_bse.selfRef < 0 || s_bse.selfRef + (int)sizeof(UObject*) > sz) return;
                UObject* selfRef = *reinterpret_cast<UObject**>(p + s_bse.selfRef);
                if (!selfRef) return;

                std::wstring displayName = s_instance->readWidgetDisplayName(selfRef);
                if (displayName.empty()) return;

                s_instance->m_lastCapturedName = displayName;
                std::memcpy(s_instance->m_lastCapturedBLock, p, BLOCK_DATA_SIZE);
                s_instance->m_hasLastCapture = true;
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Captured: '{}' (with bLock data)\n"), displayName);
                s_instance->logBLockDiagnostics(L"CAPTURE", displayName, p);


                s_instance->m_hasLastHandle = false;
                UObject* buildHUD = s_instance->getCachedBuildHUD();
                if (buildHUD)
                {
                    auto* getHandleFn = buildHUD->GetFunctionByNameInChain(STR("GetSelectedRecipeHandle"));
                    if (getHandleFn)
                    {
                        int hSz = getHandleFn->GetParmsSize();
                        if (hSz >= RECIPE_HANDLE_SIZE)
                        {
                            std::vector<uint8_t> hParams(hSz, 0);
                            safeProcessEvent(buildHUD, getHandleFn, hParams.data());
                            std::memcpy(s_instance->m_lastCapturedHandle, hParams.data(), RECIPE_HANDLE_SIZE);
                            s_instance->m_hasLastHandle = true;

                            if (hParams.size() >= 16) {
                                uint32_t handleCI = *reinterpret_cast<uint32_t*>(hParams.data() + 8);
                                int32_t handleNum = *reinterpret_cast<int32_t*>(hParams.data() + 12);
                                QBLOG(STR("[MoriaCppMod] [QuickBuild] Captured handle: RowName CI={} Num={}\n"), handleCI, handleNum);
                            }
                        }
                    }
                    else
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] GetSelectedRecipeHandle not found on build HUD\n"));
                    }
                }


                s_overlay.totalRotation = 0;
                s_overlay.needsUpdate = true;
                s_instance->updateMcRotationLabel();


                if (s_off_bLock == -2)
                {
                    int bLockSize = 0;
                    resolveOffsetAndSize(selfRef, L"bLock", s_off_bLock, bLockSize);
                    if (bLockSize > 0 && bLockSize != BLOCK_DATA_SIZE)
                    {
                        VLOG(STR("[MoriaCppMod] WARNING: bLock property size {} != BLOCK_DATA_SIZE {} — struct layout may have changed!\n"),
                             bLockSize, BLOCK_DATA_SIZE);
                    }
                    else if (bLockSize > 0)
                    {
                        VLOG(STR("[MoriaCppMod] [Validate] BLOCK_DATA_SIZE OK: bLock size = {} (matches constant)\n"), bLockSize);
                    }
                }


                s_instance->onGhostAppeared();
            });

            m_replayActive = true;
            VLOG(
                    STR("[MoriaCppMod] v6.21.0: F1-F8=build | F9=rotate | F12=config | Num0=bubble info | Num*=reveal map | Mod keybinds in Settings → keymap tab\n"));


            // Register game thread tick — fires once per frame ON the game thread
            // All UE4 API calls (ProcessEvent, FindAllOf, reflection) belong here
            Unreal::Hook::RegisterEngineTickPreCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<void>&, UEngine*, float deltaSeconds, bool bIdleMode)
                {
                    if (s_instance) s_instance->gameThreadTick(deltaSeconds);
                }, {});
            VLOG(STR("[MoriaCppMod] Registered EngineTick game thread callback\n"));

            Unreal::Hook::RegisterLoadMapPreCallback(
                [this](UEngine*, FWorldContext&, FURL, UPendingNetGame*, FString&) -> std::pair<bool, bool>
                {
                    if (!m_definitionsApplied)
                    {
                        m_definitionsApplied = true;
                        try { loadAndApplyDefinitions(); }
                        catch (...) { RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] Exception during definition loading\n")); }
                    }
                    // v6.9.0 — one-shot FGK DynamicTableAsset diagnostic.
                    // Logs each FGK wrapper's TableAsset, TestTableAsset, and
                    // DynamicTableAsset pointers + RowMap counts to determine
                    // whether DynamicTableAsset is initialized at runtime
                    // and is a viable runtime-additions slot for build menu.
                    if (!m_fgkDiagDone)
                    {
                        m_fgkDiagDone = true;
                        try { dumpFGKWrapperDiagnostic(); }
                        catch (...) { VLOG(STR("[FGKDiag] exception\n")); }
                    }
                    return {false, false};
                });
        }

        bool m_fgkDiagDone{false};
        void dumpFGKWrapperDiagnostic()
        {
            VLOG(STR("[FGKDiag] === Begin FGK DynamicTableAsset diagnostic ===\n"));
            // Wrapper class names known from CXXHeaderDump/Moria.hpp.
            const wchar_t* wrapperClasses[] = {
                STR("MorConstructionsTable"),
                STR("MorConstructionRecipesTable"),
                STR("MorConstructionStabilitiesTable"),
                STR("MorConsumablesTable"),
                STR("MorContainerItemsTable"),
                STR("MorCosmeticConvertTable"),
                STR("MorWeaponsTable"),
                STR("MorToolsTable"),
                STR("MorArmorsTable"),
                STR("MorOresTable"),
                STR("MorItemsTable"),
            };
            int totalLogged = 0;
            for (auto* wname : wrapperClasses)
            {
                std::vector<UObject*> instances;
                findAllOfSafe(wname, instances); // v6.14.0 — SEH-wrapped
                if (instances.empty()) {
                    VLOG(STR("[FGKDiag] {}: no instances found\n"), wname);
                    continue;
                }
                for (UObject* w : instances)
                {
                    if (!w || !isObjectAlive(w)) continue;
                    auto* taPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("TableAsset"));
                    auto* ttPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("TestTableAsset"));
                    auto* dtPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("DynamicTableAsset"));
                    UObject* ta = taPtr ? *taPtr : nullptr;
                    UObject* tt = ttPtr ? *ttPtr : nullptr;
                    UObject* dt = dtPtr ? *dtPtr : nullptr;
                    auto rowCount = [](UObject* tbl) -> int {
                        if (!tbl) return -1;
                        auto* rm = tbl->GetValuePtrByPropertyNameInChain<uint8_t>(STR("RowMap"));
                        if (!rm) return -2;
                        int32_t num = *reinterpret_cast<int32_t*>(rm + 8); // TMap layout: Data*, Num, Max
                        return num;
                    };
                    VLOG(STR("[FGKDiag] {} @{:p}  TableAsset={:p} (rows={})  TestAsset={:p} (rows={})  DynamicAsset={:p} (rows={})\n"),
                         wname, (void*)w,
                         (void*)ta, rowCount(ta),
                         (void*)tt, rowCount(tt),
                         (void*)dt, rowCount(dt));
                    if (dt && isObjectAlive(dt))
                    {
                        std::wstring dtCls = safeClassName(dt);
                        std::wstring dtName;
                        try { dtName = dt->GetName(); } catch (...) {}
                        VLOG(STR("[FGKDiag]   DynamicAsset class='{}' name='{}'\n"), dtCls.c_str(), dtName.c_str());
                    }
                    ++totalLogged;
                }
            }
            VLOG(STR("[FGKDiag] === End — logged {} wrapper instances ===\n"), totalLogged);

            // DiscoveryManager probe deferred to tickFGKDiscoveryDiag —
            // manager spawns AFTER LoadMap (per-world actor), so polling
            // each tick until it exists is the right hook point.
        }

        // Path #5 diagnostic — dump UMorConstructionsTable.ActorRowNameLookup
        // (TMap<TSoftClassPtr<AActor>, FName> at offset 0x110, header size 0x50).
        //
        // UE4 TMap memory layout:
        //   TSet<TPair<K,V>> Pairs:
        //     TSparseArray<TSetElement<TPair<K,V>>>:
        //       0x00: Data*  (pointer to elements)
        //       0x08: Num    (total slots including holes)
        //       0x0C: Max    (capacity)
        //       0x10: NumFreeIndices
        //       0x14: FirstFreeIndex
        //       0x18: TBitArray AllocationFlags
        //   ... + hash table after sparse array
        //
        // Element layout (TSetElement<TPair<K,V>>):
        //   TPair<K,V>:
        //     K = TSoftClassPtr<AActor> = TSoftObjectPtr<UClass> = 0x28 bytes
        //     V = FName = 0x08 bytes
        //   Then HashNextId (4) + HashIndex (4) = 0x38 total per element
        // Path: pure-UE4SS FGK runtime row injection test.
        //
        // Hypothesis: AddRowInternal + UDataTable::HandleDataTableChanged
        // together trigger FGK wrapper cache rebuild (wrapper picks up the
        // new row in its ActorRowNameLookup TMap automatically).
        //
        // We use the P3R-verified AOB pattern (Moria is UE 4.27.2 same as P3R)
        // to locate HandleDataTableChanged in Moria's binary. UE.Toolkit
        // resolves it at the same pattern, so we know it's present.
        //
        // Test flow:
        //   1. Resolve HandleDataTableChanged via AOB scan
        //   2. Find MorConstructionsTable wrapper instance
        //   3. Get its TableAsset (the underlying UDataTable)
        //   4. Read ActorRowNameLookup.Num at wrapper+0x118 (Num offset of TSet)
        //   5. Use existing m_dtConstructions.addRow() to add a test row
        //   6. Call HandleDataTableChanged(TableAsset, testRowName)
        //   7. Re-read ActorRowNameLookup.Num
        //   8. If Num increased -> wrapper rebuilt cache, FGK injection works
        //      If Num unchanged -> shipping HandleDataTableChanged is a no-op
        typedef void(__fastcall* FnHandleDataTableChanged)(void* table, RC::Unreal::FName changedRow);
        FnHandleDataTableChanged m_pfHandleDataTableChanged{nullptr};
        bool m_fgkInjectionTestDone{false};

        bool resolveHandleDataTableChanged()
        {
            if (m_pfHandleDataTableChanged) return true;
            // P3R-derived pattern, verified by UE.Toolkit on Moria binary.
            // Pattern: 48 89 54 24 ?? 55 53 56 57 48 8D 6C 24 ?? 48 81 EC 98 00 00 00
            //         ^  ^  ^  ^  ?  ^  ^  ^  ^  ^  ^  ^  ^  ?  ^  ^  ^  ^  ^  ^  ^
            static const uint8_t pat[] = {
                0x48,0x89,0x54,0x24,0x00, 0x55,0x53,0x56,0x57,
                0x48,0x8D,0x6C,0x24,0x00, 0x48,0x81,0xEC,0x98,0x00,0x00,0x00
            };
            static const bool wild[] = {
                false,false,false,false,true,  false,false,false,false,
                false,false,false,false,true,  false,false,false,false,false,false,false
            };
            HMODULE hMod = GetModuleHandleW(L"Moria-Win64-Shipping.exe");
            if (!hMod) hMod = GetModuleHandleW(nullptr);
            if (!hMod) return false;
            MODULEINFO modInfo{};
            if (!GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo)))
                return false;
            uint8_t* base = reinterpret_cast<uint8_t*>(hMod);
            size_t size = modInfo.SizeOfImage;
            constexpr size_t patLen = sizeof(pat);
            for (size_t i = 0; i + patLen < size; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patLen; ++j)
                {
                    if (!wild[j] && base[i + j] != pat[j]) { match = false; break; }
                }
                if (match)
                {
                    m_pfHandleDataTableChanged = reinterpret_cast<FnHandleDataTableChanged>(base + i);
                    VLOG(STR("[FGKInject] HandleDataTableChanged found at {:p}\n"),
                         (void*)m_pfHandleDataTableChanged);
                    return true;
                }
            }
            VLOG(STR("[FGKInject] HandleDataTableChanged AOB pattern NOT found in module\n"));
            return false;
        }


        bool m_actorLookupDiagDone{false};
        void tickActorLookupDiag()
        {
            if (m_actorLookupDiagDone) return;
            std::vector<UObject*> wrappers;
            findAllOfSafe(STR("MorConstructionsTable"), wrappers); // v6.14.0 — SEH-wrapped
            if (wrappers.empty()) return;
            m_actorLookupDiagDone = true;

            VLOG(STR("[ActorLookup] === Begin ActorRowNameLookup dump ===\n"));
            for (UObject* w : wrappers)
            {
                if (!w || !isObjectAlive(w)) continue;
                auto* base = reinterpret_cast<uint8_t*>(w);
                struct SparseArrayHdr {
                    uint8_t* Data;       // 0x00
                    int32_t  Num;        // 0x08
                    int32_t  Max;        // 0x0C
                    int32_t  NumFree;    // 0x10
                    int32_t  FirstFree;  // 0x14
                };
                SparseArrayHdr* sa = reinterpret_cast<SparseArrayHdr*>(base + 0x110);
                VLOG(STR("[ActorLookup] wrapper @{:p}  Data={:p} Num={} Max={} NumFree={} FirstFree={}\n"),
                     (void*)w, (void*)sa->Data, sa->Num, sa->Max, sa->NumFree, sa->FirstFree);

                // Hex-dump first 64 bytes of element data + flag bits 0x18..0x40
                if (sa->Data && sa->Num > 0)
                {
                    // First, dump raw bytes 0x18..0x50 (TBitArray + hash table head)
                    VLOG(STR("[ActorLookup] hdr-rest bytes (0x18..0x50):\n"));
                    for (int row = 0x18; row < 0x50; row += 16) {
                        VLOG(STR("[ActorLookup]   +0x{:02x}: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}  {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}\n"),
                             row,
                             base[0x110 + row+0], base[0x110 + row+1], base[0x110 + row+2], base[0x110 + row+3],
                             base[0x110 + row+4], base[0x110 + row+5], base[0x110 + row+6], base[0x110 + row+7],
                             base[0x110 + row+8], base[0x110 + row+9], base[0x110 + row+10], base[0x110 + row+11],
                             base[0x110 + row+12], base[0x110 + row+13], base[0x110 + row+14], base[0x110 + row+15]);
                    }
                    // Hex-dump first 256 bytes of element data — NO FName
                    // interpretation. Lets us see actual layout without
                    // crashing on bad ComparisonIndex values.
                    VLOG(STR("[ActorLookup] First 256 bytes of element data:\n"));
                    int dumpBytes = sa->Num < 8 ? sa->Num * 0x40 : 256;
                    if (dumpBytes > 256) dumpBytes = 256;
                    for (int row = 0; row < dumpBytes; row += 16) {
                        if (!isReadableMemory(sa->Data + row, 16)) {
                            VLOG(STR("[ActorLookup]   +0x{:03x}: <unreadable>\n"), row);
                            break;
                        }
                        uint8_t* p = sa->Data + row;
                        VLOG(STR("[ActorLookup]   +0x{:03x}: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}  {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}\n"),
                             row,
                             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                    }
                }
            }
            VLOG(STR("[ActorLookup] === End ===\n"));
        }

        bool m_dmDiagDone{false};
        void tickFGKDiscoveryDiag()
        {
            if (m_dmDiagDone) return;
            std::vector<UObject*> dms;
            findAllOfSafe(STR("MorDiscoveryManager"), dms); // v6.14.0 — SEH-wrapped
            if (dms.empty()) return; // not spawned yet
            m_dmDiagDone = true;
            VLOG(STR("[FGKDiag] === DiscoveryManager.Recipes (post-load) ===\n"));
            VLOG(STR("[FGKDiag] DiscoveryManager instances: {}\n"), (int)dms.size());
            for (UObject* dm : dms)
            {
                if (!dm || !isObjectAlive(dm)) continue;
                auto* base = reinterpret_cast<uint8_t*>(dm);
                struct TAH { uint8_t* Data; int32_t Num; int32_t Max; };
                TAH* recipes = reinterpret_cast<TAH*>(base + 0x0220);
                VLOG(STR("[FGKDiag] DM @{:p}  Recipes.Data={:p} Num={} Max={}\n"),
                     (void*)dm, (void*)recipes->Data, recipes->Num, recipes->Max);
                if (recipes->Data && recipes->Num > 0)
                {
                    constexpr int kStride = 0x128;
                    int n = recipes->Num < 5 ? recipes->Num : 5;
                    for (int i = 0; i < n; ++i)
                    {
                        uint8_t* row = recipes->Data + (size_t)i * kStride;
                        FName* rn = reinterpret_cast<FName*>(row + 0xD8 + 0x8);
                        std::wstring rnStr;
                        try { rnStr = rn->ToString(); } catch (...) {}
                        VLOG(STR("[FGKDiag]   Recipes[{}] Result.RowName='{}'\n"),
                             i, rnStr.empty() ? STR("(?)") : rnStr.c_str());
                    }
                }
            }
            VLOG(STR("[FGKDiag] === End DiscoveryManager diagnostic ===\n"));
        }


        // Game thread tick — called once per frame ON the game thread via EngineTick hook.
        // ALL mod logic runs here: UE4 API calls, key handling, state machine, widget ops.
        // GetAsyncKeyState is safe here too (Win32 API, reads global state).
        void gameThreadTick(float deltaSeconds)
        {
            // Detect dedicated server once (no GameViewport = headless)
            if (!m_serverDetected)
            {
                m_serverDetected = true;
                auto* pc = findPlayerController();
                if (!pc)
                {
                    // No local PlayerController = dedicated server (headless)
                    m_isDedicatedServer = true;
                    VLOG(STR("[MoriaCppMod] Detected DEDICATED SERVER mode (no local PlayerController)\n"));
                }
            }

            // Skip ALL UI creation on dedicated servers (no display)
            if (!m_isDedicatedServer)
            {
                bool justCreated = false;
                // v6.10.0 (v0.13) — Auto-creation of the three original
                // toolbars (MC, Experimental QuickBuild, Advanced
                // Builder) is DISABLED. The New Building Bar at the top
                // of the screen replaces them. If any were created in
                // a previous boot, destroy them now so they don't
                // linger.
                if (m_mcBarWidget) { destroyModControllerBar(); }
                if (m_umgBarWidget) { destroyExperimentalBar(); }
                if (m_abBarWidget) { destroyAdvancedBuilderBar(); }

                if (false /* v0.13 disabled */ && m_characterLoaded && !m_mcBarWidget)
                {
                    createModControllerBar();
                    justCreated = true;
                }
                if (false /* v0.13 disabled */ && m_characterLoaded && !m_umgBarWidget)
                {
                    createExperimentalBar();
                    justCreated = true;
                }
                if (false /* v0.13 disabled */ && m_characterLoaded && !m_abBarWidget)
                {
                    createAdvancedBuilderBar();
                    justCreated = true;
                }

                // v0.14 — CRITICAL: handle-resolution priming was nested
                // inside the disabled AB-bar block, blocking F1-F8 USE,
                // chord polling for SET, and every keybind that gates on
                // HandleResolvePhase::Done. Move it out so it runs once
                // per character-load regardless of toolbar state.
                if (m_characterLoaded && m_handleResolvePhase == HandleResolvePhase::None)
                {
                    bool needsResolve = false;
                    for (int i = 0; i < QUICK_BUILD_SLOTS; i++)
                    {
                        if (m_recipeSlots[i].used && !m_recipeSlots[i].hasHandle && !m_recipeSlots[i].rowName.empty())
                        {
                            needsResolve = true;
                            break;
                        }
                    }
                    if (needsResolve)
                    {
                        QBLOG(STR("[MoriaCppMod] [HandleResolve] starting eager handle resolution (no toolbar)\n"));
                        m_handleResolvePhase = HandleResolvePhase::Priming;
                        m_handleResolveStartTime = GetTickCount64();
                        m_buildTabAfterShowFired = false;
                        activateBuildMode();
                    }
                    else
                    {
                        m_handleResolvePhase = HandleResolvePhase::Done;
                    }
                }
                if (m_characterLoaded && !m_targetInfoWidget)
                    createTargetInfoWidget();
                if (m_characterLoaded && !m_errorBoxWidget)
                    createErrorBox();


                if (justCreated && !m_toolbarsVisible)
                {
                    setWidgetVisibility(m_mcBarWidget, 1);
                    setWidgetVisibility(m_umgBarWidget, 1);
                }
            } // end if (!m_isDedicatedServer) — skip UI on headless server



            if (s_pendingKeyLabelRefresh.exchange(false))
            {
                refreshKeyLabels();
                if (m_ftVisible) updateFontTestKeyLabels();
                refreshNewBuildingBarKeyLabels(); // v6.14.0 — Phase 2 rebind hook
            }


            if (m_pendingCharNameReady.exchange(false))
                applyPendingCharacterName();

            tickDeferredWidgetRemovals();

            if (m_ftRenameVisible)
            {
                if (GetAsyncKeyState(VK_RETURN) & 1)
                    confirmRenameDialog();
                else if (GetAsyncKeyState(VK_ESCAPE) & 1)
                    hideRenameDialog();


                static bool s_renameLMBPrev = false;
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmb && !s_renameLMBPrev)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;

                        float cx = viewW / 2.0f;
                        float cy = viewH / 2.0f;
                        float dlgW = 700.0f * s2p, dlgH = 220.0f * s2p;
                        float dlgLeft = cx - dlgW / 2.0f;
                        float dlgTop  = cy - dlgH / 2.0f;


                        float btnY = dlgTop + dlgH - 15.0f * s2p - 55.0f * s2p;
                        float btnH = 55.0f * s2p;
                        float cancelX0 = dlgLeft + (700.0f / 2.0f - 250.0f - 15.0f) * s2p;
                        float cancelX1 = cancelX0 + 250.0f * s2p;
                        float confirmX0 = dlgLeft + (700.0f / 2.0f + 15.0f) * s2p;
                        float confirmX1 = confirmX0 + 250.0f * s2p;

                        VLOG(STR("[MoriaCppMod] [Rename] Click: cur=({},{}) btnY=[{:.0f},{:.0f}] cancelX=[{:.0f},{:.0f}] confirmX=[{:.0f},{:.0f}]\n"),
                            curX, curY, btnY, btnY + btnH, cancelX0, cancelX1, confirmX0, confirmX1);
                        if (curY >= btnY && curY <= btnY + btnH)
                        {
                            if (curX >= cancelX0 && curX <= cancelX1)
                            { VLOG(STR("[MoriaCppMod] [Rename] CANCEL clicked\n")); hideRenameDialog(); }
                            else if (curX >= confirmX0 && curX <= confirmX1)
                            { VLOG(STR("[MoriaCppMod] [Rename] CONFIRM clicked\n")); confirmRenameDialog(); }
                        }
                    }
                }
                s_renameLMBPrev = lmb;
            }


            if (m_trashDlgVisible)
            {

                if (GetTickCount64() - m_trashDlgOpenTick < 500)
                {

                    GetAsyncKeyState(VK_RETURN);
                    GetAsyncKeyState(VK_ESCAPE);
                }
                else if (GetAsyncKeyState(VK_RETURN) & 1)
                    confirmTrashItem();
                else if (GetAsyncKeyState(VK_ESCAPE) & 1)
                    hideTrashDialog();


                static bool s_trashLMBPrev = false;
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmb && !s_trashLMBPrev)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;

                        float cx = viewW / 2.0f;
                        float cy = viewH / 2.0f;
                        float dlgW = 500.0f * s2p, dlgH = 180.0f * s2p;
                        float dlgLeft = cx - dlgW / 2.0f;
                        float dlgTop  = cy - dlgH / 2.0f;


                        float btnY = dlgTop + dlgH - 15.0f * s2p - 50.0f * s2p;
                        float btnH = 50.0f * s2p;
                        float cancelX0 = dlgLeft + (500.0f / 2.0f - 200.0f - 15.0f) * s2p;
                        float cancelX1 = cancelX0 + 200.0f * s2p;
                        float deleteX0 = dlgLeft + (500.0f / 2.0f + 15.0f) * s2p;
                        float deleteX1 = deleteX0 + 200.0f * s2p;

                        VLOG(STR("[MoriaCppMod] [Trash] Click: cur=({},{}) btnY=[{:.0f},{:.0f}] cancelX=[{:.0f},{:.0f}] deleteX=[{:.0f},{:.0f}]\n"),
                            curX, curY, btnY, btnY + btnH, cancelX0, cancelX1, deleteX0, deleteX1);
                        if (curY >= btnY && curY <= btnY + btnH)
                        {
                            if (curX >= cancelX0 && curX <= cancelX1)
                            { VLOG(STR("[MoriaCppMod] [Trash] CANCEL clicked\n")); hideTrashDialog(); }
                            else if (curX >= deleteX0 && curX <= deleteX1)
                            { VLOG(STR("[MoriaCppMod] [Trash] DELETE clicked\n")); confirmTrashItem(); }
                        }
                    }
                }
                s_trashLMBPrev = lmb;
            }


            if (m_tiShowTick > 0 && (GetTickCount64() - m_tiShowTick) >= 10000)
                hideTargetInfo();

            if (m_crosshairShowTick > 0 && (GetTickCount64() - m_crosshairShowTick) >= CROSSHAIR_FADE_MS)
                hideCrosshair();


            if (m_ebShowTick > 0 && (GetTickCount64() - m_ebShowTick) >= ERROR_BOX_DURATION_MS)
                hideErrorBox();


            if (m_auditClearTime > 0 && GetTickCount64() >= m_auditClearTime)
                clearStabilityHighlights();


            {
                static bool s_lastCfgKey = false;
                uint8_t cfgVk = s_bindings[MC_BIND_BASE + 5].key;
                if (cfgVk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(cfgVk) & 0x8000) != 0;
                    if (nowDown && !s_lastCfgKey)
                        toggleFontTestPanel();
                    s_lastCfgKey = nowDown;
                }
            }


            if (m_mcBarWidget)
            {
                static bool s_lastMcKey[MC_SLOTS]{};

                const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (i == 5) { s_lastMcKey[i] = false; continue; }
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_lastMcKey[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;

                    if (!nowDown && shiftHeld)
                    {
                        uint8_t alt = numpadShiftAlternate(vk);
                        if (alt) nowDown = (GetAsyncKeyState(alt) & 0x8000) != 0;
                    }
                    if (nowDown && !s_lastMcKey[i] && !m_ftVisible && !m_repositionMode
                        && s_bindings[MC_BIND_BASE + i].enabled)
                    {
                        VLOG(
                            STR("[MoriaCppMod] [MC] Slot {} pressed (VK=0x{:02X})\n"), i, vk);
                        dispatchMcSlot(i);
                    }
                    s_lastMcKey[i] = nowDown;
                }
            }


            if (m_repositionMode)
            {

                static bool s_lastReposEsc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastReposEsc)
                    toggleRepositionMode();
                s_lastReposEsc = escDown;


                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;


                float curFracX = 0.5f, curFracY = 0.5f;
                m_screen.getCursorFraction(curFracX, curFracY);


                int32_t rawVW = m_screen.viewW, rawVH = m_screen.viewH;


                float slateCursorX = 0.0f, slateCursorY = 0.0f;
                bool gotMouse = getMousePositionSlate(slateCursorX, slateCursorY);
                if (!gotMouse)
                {

                    int cx, cy, cw, ch;
                    if (m_screen.getCursorClientPixels(cx, cy, cw, ch)) {
                        slateCursorX = m_screen.pixelToSlateX(static_cast<float>(cx));
                        slateCursorY = m_screen.pixelToSlateY(static_cast<float>(cy));
                    }
                }

                if (lmb && m_dragToolbar < 0)
                {

                    constexpr float kHitRadX = 0.35f;
                    constexpr float kHitRadY = 0.25f;
                    float bestDist = 1e9f;
                    int   bestIdx  = -1;
                    UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget.Get()}; // v6.17.0 weakptr
                    for (int i = 0; i < TB_COUNT; i++)
                    {
                        if (!widgets[i]) continue;
                        float fx = (m_toolbarPosX[i] >= 0) ? m_toolbarPosX[i] : TB_DEF_X[i];
                        float fy = (m_toolbarPosY[i] >= 0) ? m_toolbarPosY[i] : TB_DEF_Y[i];
                        float dx = curFracX - fx;
                        float dy = curFracY - fy;
                        if (std::abs(dx) <= kHitRadX && std::abs(dy) <= kHitRadY)
                        {
                            float dist = dx*dx + dy*dy;
                            if (dist < bestDist) { bestDist = dist; bestIdx = i; }
                        }
                    }

                    if (bestIdx >= 0)
                    {
                        m_dragToolbar = bestIdx;
                        float fx = (m_toolbarPosX[bestIdx] >= 0) ? m_toolbarPosX[bestIdx] : TB_DEF_X[bestIdx];
                        float fy = (m_toolbarPosY[bestIdx] >= 0) ? m_toolbarPosY[bestIdx] : TB_DEF_Y[bestIdx];
                        m_dragOffsetX = curFracX - fx;
                        m_dragOffsetY = curFracY - fy;
                    }
                }
                else if (lmb && m_dragToolbar >= 0)
                {
                    float fx = std::clamp(curFracX - m_dragOffsetX, 0.01f, 0.99f);
                    float fy = std::clamp(curFracY - m_dragOffsetY, 0.01f, 0.99f);
                    m_toolbarPosX[m_dragToolbar] = fx;
                    m_toolbarPosY[m_dragToolbar] = fy;
                    UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget.Get()}; // v6.17.0 weakptr
                    float px = m_screen.fracToPixelX(fx);
                    float py = m_screen.fracToPixelY(fy);
                    static int s_dragLog = 0;
                    if (++s_dragLog % 30 == 1)
                        VLOG(STR("[MoriaCppMod] [Drag] tb={} curFrac=({:.3f},{:.3f}) -> frac=({:.3f},{:.3f}) pos=({:.0f},{:.0f}) vp={}x{}\n"),
                            m_dragToolbar, curFracX, curFracY, fx, fy, px, py, rawVW, rawVH);
                    setWidgetPosition(widgets[m_dragToolbar], px, py, true);
                }
                else if (!lmb && m_dragToolbar >= 0)
                {
                    m_dragToolbar = -1;
                }
            }


            {
                static bool s_lastAbKey = false;
                uint8_t vk = s_bindings[BIND_AB_OPEN].key;
                // v6.10.0 (v0.13) — NUM+ / Advanced Builder Open dispatch
                // DISABLED. The 3 original toolbars no longer auto-spawn,
                // so toggling them is meaningless. Replaced by the New
                // Building Bar at the top of the screen.
                if (false && vk != 0 && s_bindings[BIND_AB_OPEN].enabled)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbKey && !m_ftVisible)
                    {
                        if (isModifierDown())
                        {

                            toggleRepositionMode();
                        }
                        else if (!m_repositionMode && m_gameHudVisible)
                        {

                            m_toolbarsVisible = !m_toolbarsVisible;
                            VLOG(STR("[MoriaCppMod] [AB] Toggle pressed Ã¢â‚¬â€ toolbars {}\n"),
                                                            m_toolbarsVisible ? STR("VISIBLE") : STR("HIDDEN"));

                            uint8_t vis = m_toolbarsVisible ? 0 : 1;
                            setWidgetVisibility(m_umgBarWidget, vis);
                            setWidgetVisibility(m_mcBarWidget, vis);
                        }
                    }
                    s_lastAbKey = nowDown;
                }
            }


            // v6.21.0 — Restore Mod Controller bindings (8-16) keypress dispatch.
            // The v6.4.5 polling loop was wrapped in `if (m_mcBarWidget)`,
            // and v6.10.0 disabled MC bar auto-creation, leaving Set Rotation /
            // Snap / Integrity / Invisible Dwarf / Target / Remove Single /
            // Undo Last / Remove All silent. This block restores dispatch
            // without depending on m_mcBarWidget. Slot 5 (= BIND_CONFIG, F12)
            // is skipped — its dispatch is via register_keydown_event elsewhere.
            // Suppressed entirely while Settings UI is open so the user's
            // keypresses during rebinding don't trigger gameplay (fix #6).
            if (m_characterLoaded && !m_ftVisible && !m_repositionMode &&
                !m_trashDlgVisible && !m_ftRenameVisible && !isSettingsScreenOpen())
            {
                static bool s_mcKeyDown[MC_SLOTS]{};
                const bool mcShiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                for (int i = 0; i < MC_SLOTS; ++i)
                {
                    if (i == 5) { s_mcKeyDown[i] = false; continue; }
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_mcKeyDown[i] = false; continue; }
                    if (!s_bindings[MC_BIND_BASE + i].enabled) { s_mcKeyDown[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (!nowDown && mcShiftHeld)
                    {
                        uint8_t alt = numpadShiftAlternate(vk);
                        if (alt) nowDown = (GetAsyncKeyState(alt) & 0x8000) != 0;
                    }
                    if (nowDown && !s_mcKeyDown[i]) dispatchMcSlot(i);
                    s_mcKeyDown[i] = nowDown;
                }
            }

            // v6.4.1 — skip single-key action binds (trash/replenish/remove-attrs) when ANY modifier
            // is held. Prevents Ctrl+Shift+L etc. from accidentally triggering the trash dialog when
            // the player just presses a modifier-combo key meant for something else.
            const bool modDown =
                ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0) ||
                ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) ||
                ((GetAsyncKeyState(VK_MENU)    & 0x8000) != 0);
            {
                static bool s_lastReplenishKey = false;
                uint8_t vk = s_bindings[BIND_REPLENISH_ITEM].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastReplenishKey && !m_ftVisible)
                    {
                        if (m_replenishItemEnabled) replenishLastItem();
                        else showInfoMessage(Loc::get("msg.replenish_disabled"));
                    }
                    s_lastReplenishKey = nowDown;
                }
            }
            {
                static bool s_lastRemoveAttrsKey = false;
                uint8_t vk = s_bindings[BIND_REMOVE_ATTRS].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastRemoveAttrsKey && !m_ftVisible && !modDown)
                    {
                        if (m_removeAttrsEnabled) removeItemAttributes();
                        else showInfoMessage(Loc::get("msg.remove_attrs_disabled"));
                    }
                    s_lastRemoveAttrsKey = nowDown;
                }
            }
            {
                static bool s_lastTrashKey = false;
                uint8_t vk = s_bindings[BIND_TRASH_ITEM].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastTrashKey && !m_ftVisible && !m_trashDlgVisible && !modDown)
                    {
                        if (m_trashItemEnabled) showTrashDialog();
                        else showInfoMessage(Loc::get("msg.trash_disabled"));
                    }
                    s_lastTrashKey = nowDown;
                }
            }
            // Num* — reveal map (zones + chapters only) (v6.4.5+)
            {
                static bool s_lastRevealMapKey = false;
                bool nowDown = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0;
                if (nowDown && !s_lastRevealMapKey)
                {
                    VLOG(STR("[MoriaCppMod] [RevealMap] NUM* press detected (ftVisible={} modDown={})\n"),
                         m_ftVisible ? 1 : 0, modDown ? 1 : 0);
                    if (!m_ftVisible && !modDown)
                        revealEntireMap();
                }
                s_lastRevealMapKey = nowDown;
            }
            // Num0 — capture bubble info to clipboard + Target Info widget (v6.4.5+)
            // Windows maps numpad-0 to VK_NUMPAD0 only when NumLock is ON; with NumLock OFF
            // the same physical key sends VK_INSERT (which Replenish owns). We always
            // check VK_NUMPAD0 and also fall back to a fresh key-state via GetKeyState
            // to survive focus-related async state drops.
            {
                static bool s_lastBubbleInfoKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) != 0;
                if (nowDown && !s_lastBubbleInfoKey)
                {
                    bool numLockOn = (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
                    VLOG(STR("[MoriaCppMod] [BubbleInfo] NUM0 press detected (numLock={} ftVisible={} modDown={})\n"),
                         numLockOn ? 1 : 0, m_ftVisible ? 1 : 0, modDown ? 1 : 0);
                    if (!m_ftVisible && !modDown)
                        captureBubbleInfo();
                }
                s_lastBubbleInfoKey = nowDown;
            }
            // N — debug widget harvest (v6.6.0+, dev-only). Constructs the 12 Join World
            // widget classes off-viewport, walks each WidgetTree, dumps to JSON under
            // Mods/MoriaCppMod/widget-harvest/. Used to plan C++ duplicates.
            // Plain N (no modifier) since N isn't bound to any other action.
            {
                static bool s_lastHarvestKey = false;
                bool nowDown = (GetAsyncKeyState('N') & 0x8000) != 0;
                if (nowDown && !s_lastHarvestKey)
                {
                    VLOG(STR("[MoriaCppMod] [WidgetHarvest] N press detected (ftVisible={} modDown={})\n"),
                         m_ftVisible ? 1 : 0, modDown ? 1 : 0);
                    if (!m_ftVisible && !modDown)
                        harvestJoinWorldWidgets();
                }
                s_lastHarvestKey = nowDown;
            }
            // Pitch rotation (,  / SHIFT+,)
            // Pitch rotation (. / SHIFT+.) — BIND_PITCH_ROTATE defaults to '.'
            // v0.30 — gated to ghost-visible (resolveGATA returns non-null).
            {
                static bool s_lastPitchKey = false;
                uint8_t vk = s_bindings[BIND_PITCH_ROTATE].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastPitchKey && !m_ftVisible)
                    {
                        if (m_pitchRotateEnabled && resolveGATA())
                        {
                            float step = static_cast<float>(s_overlay.rotationStep.load());
                            if (isModifierDown()) step = -step;
                            m_experimentPitch += step;
                            if (m_experimentPitch >= 360.0f) m_experimentPitch -= 360.0f;
                            if (m_experimentPitch <= -360.0f) m_experimentPitch += 360.0f;
                            injectPitchRoll(m_experimentPitch, m_experimentRoll);
                        }
                    }
                    s_lastPitchKey = nowDown;
                }
            }
            // Roll rotation (, / SHIFT+,) — BIND_ROLL_ROTATE defaults to ','
            // v0.30 — gated to ghost-visible.
            {
                static bool s_lastRollKey = false;
                uint8_t vk = s_bindings[BIND_ROLL_ROTATE].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastRollKey && !m_ftVisible)
                    {
                        if (m_rollRotateEnabled && resolveGATA())
                        {
                            float step = static_cast<float>(s_overlay.rotationStep.load());
                            if (isModifierDown()) step = -step;
                            m_experimentRoll += step;
                            if (m_experimentRoll >= 360.0f) m_experimentRoll -= 360.0f;
                            if (m_experimentRoll <= -360.0f) m_experimentRoll += 360.0f;
                            injectPitchRoll(m_experimentPitch, m_experimentRoll);
                        }
                    }
                    s_lastRollKey = nowDown;
                }
            }

            {
                HWND gameWnd = findGameWindow();
                static bool s_lastGameFocused = true;
                bool gameFocused = gameWnd && (GetForegroundWindow() == gameWnd);
                if (gameFocused && !s_lastGameFocused && m_ftVisible)
                {
                    setInputModeUI();
                    VLOG(STR("[MoriaCppMod] [Settings] Focus regained -- re-applied UI input mode\n"));
                }
                s_lastGameFocused = gameFocused;
            }


            if (m_ftVisible)
            {
                auto* pc = findPlayerController();
                if (pc)
                {
                    if (!m_bpShowMouseCursor)
                        m_bpShowMouseCursor = resolveBoolProperty(pc, L"bShowMouseCursor");
                    if (m_bpShowMouseCursor && !m_bpShowMouseCursor->GetPropertyValueInContainer(pc))
                        m_bpShowMouseCursor->SetPropertyValueInContainer(pc, true);
                }
            }


            if (m_toolbarsVisible && !m_repositionMode && !m_ftVisible && m_gameHudVisible)
            {
                auto* pc = findPlayerController();
                if (pc && !m_bpShowMouseCursor)
                    m_bpShowMouseCursor = resolveBoolProperty(pc, L"bShowMouseCursor");
                bool cursorVisible = (pc && m_bpShowMouseCursor)
                                     ? m_bpShowMouseCursor->GetPropertyValueInContainer(pc)
                                     : false;

                if (cursorVisible)
                {
                    float curFracX = -1, curFracY = -1;
                    m_screen.getCursorFraction(curFracX, curFracY);

                    int hitTB = -1, hitSlot = -1;
                    if (curFracX >= 0)
                        hitTestToolbarSlot(curFracX, curFracY, hitTB, hitSlot);

                    if (hitTB != m_hoveredToolbar || hitSlot != m_hoveredSlot)
                    {
                        UObject* oldImg = getSlotStateImage(m_hoveredToolbar, m_hoveredSlot);
                        if (oldImg) umgSetImageColor(oldImg, 1.0f, 1.0f, 1.0f, 1.0f);
                        UObject* newImg = getSlotStateImage(hitTB, hitSlot);
                        if (newImg) umgSetImageColor(newImg, 0.6f, 0.8f, 1.0f, 1.0f);
                        m_hoveredToolbar = hitTB;
                        m_hoveredSlot = hitSlot;
                    }

                    bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                    if (lmb && !m_lastClickLMB && hitTB >= 0 && hitSlot >= 0)
                    {
                        VLOG(STR("[MoriaCppMod] [Click] Toolbar {} slot {} clicked\n"), hitTB, hitSlot);
                        switch (hitTB)
                        {
                        case 0:
                            if (hitSlot >= 0 && hitSlot < 8
                                && m_handleResolvePhase == HandleResolvePhase::Done)
                            {
                                ULONGLONG clickNow = GetTickCount64();
                                if (clickNow - m_lastQBSelectTime >= 500)
                                    quickBuildSlot(hitSlot);
                            }
                            break;
                        case 1:
                        {
                            m_toolbarsVisible = !m_toolbarsVisible;
                            uint8_t vis = m_toolbarsVisible ? 0 : 1;
                            setWidgetVisibility(m_umgBarWidget, vis);
                            setWidgetVisibility(m_mcBarWidget, vis);
                            break;
                        }
                        case 2:
                            dispatchMcSlot(hitSlot);
                            break;
                        default:
                            break;
                        }
                    }
                    m_lastClickLMB = lmb;
                }
                else
                {

                    if (m_hoveredToolbar >= 0)
                    {
                        UObject* oldImg = getSlotStateImage(m_hoveredToolbar, m_hoveredSlot);
                        if (oldImg) umgSetImageColor(oldImg, 1.0f, 1.0f, 1.0f, 1.0f);
                        m_hoveredToolbar = -1;
                        m_hoveredSlot = -1;
                    }
                    m_lastClickLMB = false;
                }
            }


            // Gamepad: poll UButton::IsPressed() on all toolbar slots
            // Uses a statically-resolved UFunction* (found once via StaticFindObject) to avoid
            // GetFunctionByNameInChain crashes on GC'd or partially-initialized UButtons.
            {
                static UFunction* s_isPressedFn = nullptr;
                if (!s_isPressedFn)
                    s_isPressedFn = UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr, nullptr, STR("/Script/UMG.Button:IsPressed"));

                if (s_isPressedFn)
                {
                    // Helper lambda: poll a single button, return true if just pressed
                    auto pollButton = [](UObject* btn, UFunction* fn, bool& wasPressedRef) -> bool {
                        if (!btn || !isObjectAlive(btn)) return false;
                        struct { bool Ret{false}; } p{};
                        if (!safeProcessEvent(btn, fn, &p)) return false;
                        bool justPressed = p.Ret && !wasPressedRef;
                        wasPressedRef = p.Ret;
                        return justPressed;
                    };

                    // MC toolbar (9 slots)
                    static bool s_mcBtnWasPressed[MC_SLOTS]{};
                    if (m_mcBarWidget)
                    {
                        for (int i = 0; i < MC_SLOTS; i++)
                        {
                            if (pollButton(m_mcSlotButtons[i], s_isPressedFn, s_mcBtnWasPressed[i]))
                            {
                                VLOG(STR("[MoriaCppMod] [Gamepad] MC slot {} button pressed\n"), i);
                                dispatchMcSlot(i);
                            }
                        }
                    }

                    // Quick Build toolbar (8 slots)
                    static bool s_qbBtnWasPressed[8]{};
                    if (m_umgBarWidget)
                    {
                        for (int i = 0; i < 8; i++)
                        {
                            if (pollButton(m_umgSlotButtons[i], s_isPressedFn, s_qbBtnWasPressed[i]))
                            {
                                VLOG(STR("[MoriaCppMod] [Gamepad] QB slot {} button pressed\n"), i);
                                if (m_handleResolvePhase == HandleResolvePhase::Done)
                                {
                                    ULONGLONG clickNow = GetTickCount64();
                                    if (clickNow - m_lastQBSelectTime >= 500)
                                        quickBuildSlot(i);
                                }
                            }
                        }
                    }

                    // Advanced Builder toolbar (1 slot)
                    static bool s_abBtnWasPressed{false};
                    if (m_abBarWidget && pollButton(m_abSlotButton, s_isPressedFn, s_abBtnWasPressed))
                    {
                        VLOG(STR("[MoriaCppMod] [Gamepad] AB button pressed\n"));
                        m_toolbarsVisible = !m_toolbarsVisible;
                        uint8_t vis = m_toolbarsVisible ? 0 : 1;
                        setWidgetVisibility(m_umgBarWidget, vis);
                        setWidgetVisibility(m_mcBarWidget, vis);
                    }
                }
            }

            // Gamepad mod toolbar: D-pad toggle switches between game and mod toolbar control.
            // OFF = all controller input passes to game normally.
            // ON  = all controller input intercepted, game gets nothing, we navigate mod toolbars.
            //
            // Xbox: D-pad Left = toggle.  PS5: D-pad Right or Touchpad = toggle.
            // ON controls: LB/RB navigate slots, X/Square select, A/Cross modifier, B/Circle exit.
            if (m_controllerEnabled && m_characterLoaded)
            {
                static bool s_lastRB{}, s_lastLB{}, s_lastX{}, s_lastA{};
                static bool s_lastToggle{}, s_lastExit{};

                bool hasGamepad = false;
                bool rb{}, lb{}, xBtn{}, aBtn{}, toggleBtn{}, exitBtn{};

                // --- Read controller state based on profile ---
                if (m_controllerProfile == ControllerProfile::Xbox)
                {
                    XINPUT_STATE xstate{};
                    if (XInputGetState(0, &xstate) == ERROR_SUCCESS)
                    {
                        hasGamepad = true;
                        rb        = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
                        lb        = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  != 0;
                        xBtn      = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_X)              != 0;
                        aBtn      = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A)              != 0;
                        toggleBtn = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      != 0;
                        exitBtn   = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_B)              != 0;
                    }
                }
                else if (m_controllerProfile == ControllerProfile::PS5)
                {
                    // PS5: DirectInput — shared access, works with DualSense on Epic Games Store.
                    // DualSense DirectInput button mapping:
                    //   [0]=Square [1]=Cross [2]=Circle [3]=Triangle
                    //   [4]=L1 [5]=R1 [6]=L2 [7]=R2
                    //   [8]=Create [9]=Options [10]=L3 [11]=R3
                    //   [12]=PS [13]=Touchpad
                    m_diPrevState = m_diState;
                    if (m_diReader.poll(m_diState))
                    {
                        hasGamepad = true;
                        rb        = m_diState.buttons[5];   // R1
                        lb        = m_diState.buttons[4];   // L1
                        xBtn      = m_diState.buttons[0];   // Square
                        aBtn      = m_diState.buttons[1];   // Cross
                        toggleBtn = m_diState.dpadLeft;      // D-pad Left
                        exitBtn   = m_diState.buttons[2];   // Circle

                        // Log newly pressed buttons
                        for (int b = 0; b < 14; b++)
                        {
                            if (m_diState.buttons[b] && !m_diPrevState.buttons[b])
                                VLOG(STR("[MoriaCppMod] [DI-Btn] Button {} pressed\n"), b);
                        }
                        if (m_diState.dpadLeft && !m_diPrevState.dpadLeft)
                            VLOG(STR("[MoriaCppMod] [DI-Btn] D-pad LEFT\n"));
                        if (m_diState.dpadRight && !m_diPrevState.dpadRight)
                            VLOG(STR("[MoriaCppMod] [DI-Btn] D-pad RIGHT\n"));
                        if (m_diState.dpadUp && !m_diPrevState.dpadUp)
                            VLOG(STR("[MoriaCppMod] [DI-Btn] D-pad UP\n"));
                        if (m_diState.dpadDown && !m_diPrevState.dpadDown)
                            VLOG(STR("[MoriaCppMod] [DI-Btn] D-pad DOWN\n"));

                        if (m_diState.connected && !m_diPrevState.connected)
                            VLOG(STR("[MoriaCppMod] [DI] DirectInput gamepad connected\n"));
                    }
                }

                if (hasGamepad)
                {
                    // B/Circle also exits mod toolbar mode
                    if (exitBtn && !s_lastExit && m_modToolbarFocused)
                        toggleBtn = true;

                    UObject* pawn = m_localPawn ? m_localPawn : getPawn();
                    auto* pc = m_localPC ? m_localPC : findPlayerController();

                    // --- TOGGLE: D-pad Left enters/exits mod toolbar mode ---
                    if (toggleBtn && !s_lastToggle)
                    {
                        m_modToolbarFocused = !m_modToolbarFocused;
                        m_gpFlatIndex = 0;
                        VLOG(STR("[MoriaCppMod] [Gamepad] MOD TOOLBAR {}\n"),
                             m_modToolbarFocused ? STR("ON") : STR("OFF"));

                        if (m_modToolbarFocused)
                        {
                            m_gpDismissCalloutFrame = 3;
                            // Block pawn input only — NOT the PlayerController.
                            // PC must stay unblocked so keyboard SHIFT+F keys still work.
                            if (pawn) setBoolProp(pawn, L"bBlockInput", true);
                        }
                        else
                        {
                            if (pawn) setBoolProp(pawn, L"bBlockInput", false);
                        }
                    }

                    // Delayed ESC to dismiss callout that D-pad Left triggers.
                    // Only send ESC if we're STILL in mod toolbar mode (not a double-click).
                    // Double-click (ON→OFF quickly) = leave callout visible.
                    if (m_gpDismissCalloutFrame > 0)
                    {
                        if (!m_modToolbarFocused)
                        {
                            // Toggled back OFF before delay expired = double-click, skip ESC
                            m_gpDismissCalloutFrame = 0;
                            VLOG(STR("[MoriaCppMod] [Gamepad] Double-click — callout stays\n"));
                        }
                        else
                        {
                            m_gpDismissCalloutFrame--;
                            if (m_gpDismissCalloutFrame == 0)
                            {
                                keybd_event(VK_ESCAPE, 0, 0, 0);
                                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                                VLOG(STR("[MoriaCppMod] [Gamepad] Delayed ESC → dismiss callout\n"));
                            }
                        }
                    }

                    // Build flat slot list: AB → MC → QB
                    struct GPSlot { int tbId; int slot; };
                    GPSlot gpSlots[1 + MC_SLOTS + 8]{};
                    int gpCount = 0;
                    if (m_abBarWidget)
                        gpSlots[gpCount++] = {2, 0};
                    if (m_toolbarsVisible && m_mcBarWidget)
                        for (int i = 0; i < MC_SLOTS; i++)
                            gpSlots[gpCount++] = {1, i};
                    if (m_toolbarsVisible && m_umgBarWidget)
                        for (int i = 0; i < 8; i++)
                            gpSlots[gpCount++] = {0, i};

                    // Helper: get state image for highlighting
                    auto getGPImg = [this](int tbId, int slot) -> UObject* {
                        switch (tbId) {
                        case 0: return (slot >= 0 && slot < 8) ? m_umgStateImages[slot] : nullptr;
                        case 1: return (slot >= 0 && slot < MC_SLOTS) ? m_mcStateImages[slot] : nullptr;
                        case 2: return (slot == 0) ? m_abStateImage : nullptr;
                        default: return nullptr;
                        }
                    };

                    auto setHL = [this, &getGPImg](int tbId, int slot, bool on) {
                        UObject* img = getGPImg(tbId, slot);
                        if (img && isObjectAlive(img))
                            umgSetImageColor(img, on ? 1.0f : 1.0f, on ? 0.85f : 1.0f, on ? 0.3f : 1.0f, 1.0f);
                    };

                    // modifier=false → normal action (A button)
                    // modifier=true → shift/modifier action (B button)
                    auto dispatchGP = [this](int tbId, int slot, bool modifier) {
                        switch (tbId) {
                        case 0:  // QB: X = activate recipe, A = cancel build (ESC)
                            if (modifier)
                            {
                                // A button: cancel current build placement (like ESC)
                                keybd_event(VK_ESCAPE, 0, 0, 0);
                                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                                VLOG(STR("[MoriaCppMod] [Gamepad] QB slot {} — cancel build (ESC)\n"), slot);
                            }
                            else if (slot >= 0 && slot < 8 && m_handleResolvePhase == HandleResolvePhase::Done)
                            {
                                ULONGLONG t = GetTickCount64();
                                if (t - m_lastQBSelectTime >= 500) quickBuildSlot(slot);
                            }
                            break;
                        case 1:  // MC: dispatchMcSlot already checks isModifierDown()
                            // Temporarily force modifier state for gamepad B button
                            if (slot >= 0 && slot < MC_SLOTS)
                            {
                                if (modifier)
                                {
                                    // Simulate SHIFT held by temporarily pressing it via keybd_event
                                    keybd_event(VK_SHIFT, 0, 0, 0);
                                    dispatchMcSlot(slot);
                                    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
                                }
                                else
                                    dispatchMcSlot(slot);
                            }
                            break;
                        case 2:  // AB: toggle toolbar visibility
                            m_toolbarsVisible = !m_toolbarsVisible;
                            { uint8_t v = m_toolbarsVisible ? 0 : 1; setWidgetVisibility(m_umgBarWidget, v); setWidgetVisibility(m_mcBarWidget, v); }
                            break;
                        }
                    };

                    // Helper: disable game action bar input (HitTestInvisible) and hide nav icons
                    // --- Highlight first slot on enter ---
                    if (m_modToolbarFocused && gpCount > 0)
                    {
                        // Ensure highlight is on current slot (handles fresh entry)
                        setHL(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, true);
                    }

                    // --- Clear highlight on exit ---
                    if (!m_modToolbarFocused && gpCount > 0)
                    {
                        // Clear any stale highlight
                        for (int i = 0; i < gpCount; i++)
                            setHL(gpSlots[i].tbId, gpSlots[i].slot, false);
                    }

                    // --- Navigation + action (only when mod toolbar is ON) ---
                    if (m_modToolbarFocused && gpCount > 0)
                    {
                        if (rb && !s_lastRB)
                        {
                            setHL(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, false);
                            m_gpFlatIndex = (m_gpFlatIndex + 1) % gpCount;
                            setHL(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, true);
                            VLOG(STR("[MoriaCppMod] [Gamepad] RB → slot {}\n"), m_gpFlatIndex);
                        }

                        if (lb && !s_lastLB)
                        {
                            setHL(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, false);
                            m_gpFlatIndex = (m_gpFlatIndex - 1 + gpCount) % gpCount;
                            setHL(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, true);
                            VLOG(STR("[MoriaCppMod] [Gamepad] LB → slot {}\n"), m_gpFlatIndex);
                        }

                        // Diagnostic: log raw button state every 60 frames while in mod mode
                        static int s_diagFrame = 0;
                        if (++s_diagFrame % 60 == 0)
                            VLOG(STR("[MoriaCppMod] [GP-Diag] rb={} lb={} x={} a={} toggle={} exit={}\n"),
                                 rb?1:0, lb?1:0, xBtn?1:0, aBtn?1:0, toggleBtn?1:0, exitBtn?1:0);

                        static bool s_lastX{};
                        if (xBtn && !s_lastX)
                        {
                            VLOG(STR("[MoriaCppMod] [Gamepad] X → tb={} s={}\n"),
                                 gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot);
                            dispatchGP(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, false);
                        }
                        if (aBtn && !s_lastA)
                        {
                            VLOG(STR("[MoriaCppMod] [Gamepad] A(mod) → tb={} s={}\n"),
                                 gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot);
                            dispatchGP(gpSlots[m_gpFlatIndex].tbId, gpSlots[m_gpFlatIndex].slot, true);
                        }
                        s_lastX = xBtn;
                    }

                    s_lastRB = rb;
                    s_lastLB = lb;
                    s_lastA = aBtn;
                    s_lastToggle = toggleBtn;
                    s_lastExit = exitBtn;
                }
            }

            if (m_ftVisible && m_fontTestWidget)
            {

                static bool s_lastFtEsc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastFtEsc && s_capturingBind < 0 && !m_ftRenameVisible)
                    toggleFontTestPanel();
                s_lastFtEsc = escDown;


                static bool s_lastFtLMB = false;
                static bool s_ftCaptureSkipTick = false;
                bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmbDown && !s_lastFtLMB && !m_ftRenameVisible)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;
                        int wLeft = static_cast<int>(viewW / 2.0f - 770.0f * s2p);  // half of 1540px panel
                        int wTop  = static_cast<int>(viewH / 2.0f - 440.0f * s2p);


                        int tabX0 = static_cast<int>(wLeft + 32.5f * s2p);
                        int tabX1 = static_cast<int>(tabX0 + 512.0f * s2p);
                        if (curX >= tabX0 && curX <= tabX1)
                        {
                            for (int t = 0; t < CONFIG_TAB_COUNT; t++)
                            {
                                int tY0 = static_cast<int>(wTop + (42.0f + t * 132.0f) * s2p);
                                int tY1 = static_cast<int>(tY0 + 128.0f * s2p);
                                if (curY >= tY0 && curY <= tY1)
                                {
                                    selectFontTestTab(t);
                                    break;
                                }
                            }
                        }


                        if (m_ftSelectedTab == 0)
                        {
                            int kbImgX0 = static_cast<int>(wLeft + (1540.0f - 30.0f - 60.0f - 400.0f - 4.0f) * s2p);
                            int kbX0 = static_cast<int>(kbImgX0 + 50.0f * s2p);   // inner dark area starts ~50px from image left
                            int kbX1 = static_cast<int>(kbImgX0 + 350.0f * s2p);  // inner dark area ends ~50px from image right

                            int cbX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f) * s2p);
                            int cbX1 = static_cast<int>(cbX0 + 80.0f * s2p);
                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int rowHeight = static_cast<int>(128.0f * s2p);
                            int sectionHeight = static_cast<int>(80.0f * s2p);

                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= sz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            bool inKeyBox = (curX >= kbX0 && curX <= kbX1);
                            bool inCheckBox = (curX >= cbX0 && curX <= cbX1);
                            if (inKeyBox || inCheckBox)
                            {
                                int y = curY - contentY + static_cast<int>(scrollOff * s2p);
                                if (y >= 0)
                                {
                                    int currentY = 0;
                                    std::wstring lastSec;
                                    bool bindMatched = false;
                                    for (int b = 0; b < BIND_COUNT; b++)
                                    {
                                        if (s_bindings[b].label == L"Reserved") continue;
                                        if (lastSec.empty() || s_bindings[b].section != lastSec)
                                        {
                                            lastSec = s_bindings[b].section;
                                            currentY += sectionHeight;
                                        }
                                        if (y >= currentY && y < currentY + rowHeight)
                                        {
                                            if (inCheckBox)
                                            {

                                                s_bindings[b].enabled = !s_bindings[b].enabled;
                                                if (UObject* chk = m_ftCheckImages[b].Get()) // v6.17.0 weakptr
                                                {
                                                    auto* visFn = chk->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = s_bindings[b].enabled ? 0 : 2; safeProcessEvent(chk, visFn, vp); }
                                                }
                                                saveConfig();
                                                VLOG(STR("[MoriaCppMod] [Settings] Bind {} enabled={}\n"), b, s_bindings[b].enabled);
                                            }
                                            else
                                            {

                                                s_capturingBind = b;
                                                s_ftCaptureSkipTick = true;
                                                updateFontTestKeyLabels();

                                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for bind {}\n"), b);
                                            }
                                            bindMatched = true;
                                            break;
                                        }
                                        currentY += rowHeight;
                                    }
                                    // Modifier key: it's the LAST row on the Key Bindings tab.
                                    // Any click in the keyBox column below all keybind rows = modifier key.
                                    if (!bindMatched && inKeyBox)
                                    {
                                        if (s_modifierVK == VK_CONTROL) s_modifierVK = VK_SHIFT;
                                        else if (s_modifierVK == VK_SHIFT) s_modifierVK = VK_MENU;
                                        else if (s_modifierVK == VK_MENU) s_modifierVK = VK_RMENU;
                                        else s_modifierVK = VK_CONTROL;
                                        saveConfig();
                                        updateFontTestKeyLabels();
                                    }
                                }
                            }
                        }


                        if (m_ftSelectedTab == 1)
                        {

                            int cbX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f) * s2p);
                            int cbX1 = static_cast<int>(cbX0 + 80.0f * s2p);
                            int kbImgX0 = static_cast<int>(wLeft + (1540.0f - 30.0f - 60.0f - 400.0f - 4.0f) * s2p);
                            int kbX0 = static_cast<int>(kbImgX0 + 50.0f * s2p);   // inner dark area starts ~50px from image left
                            int kbX1 = static_cast<int>(kbImgX0 + 350.0f * s2p);  // inner dark area ends ~50px from image right
                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int sectionH = static_cast<int>(80.0f * s2p);
                            int rowH = static_cast<int>(128.0f * s2p);


                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            int y = curY - contentY + static_cast<int>(scrollOff * s2p);


                            int ncY0 = sectionH;        // No Collision (first row)
                            int rcY0 = ncY0 + rowH;
                            int sgY0 = rcY0 + rowH;
                            int trY0 = sgY0 + rowH;
                            int rpY0 = trY0 + rowH;
                            int raY0 = rpY0 + rowH;
                            int ptY0 = raY0 + rowH;
                            int rlY0 = ptY0 + rowH;

                            bool inKeyBox = (curX >= kbX0 && curX <= kbX1);
                            bool inCheckBox = (curX >= cbX0 && curX <= cbX1);
                            bool inFullRow = (curX >= cbX0 && curX <= kbX1);


                            if (inCheckBox && y >= ncY0 && y < ncY0 + rowH)
                            {
                                m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                                saveConfig();
                                updateFtNoCollision();
                                VLOG(STR("[MoriaCppMod] [Settings] No Collision toggle: {}\n"), m_noCollisionWhileFlying ? 1 : 0);
                            }

                            else if (inFullRow && y >= rcY0 && y < rcY0 + rowH)
                            {
                                showRenameDialog();
                                VLOG(STR("[MoriaCppMod] [Settings] Rename Character via button\n"));
                            }

                            else if (inFullRow && y >= sgY0 && y < sgY0 + rowH)
                            {
                                triggerSaveGame();
                                VLOG(STR("[MoriaCppMod] [Settings] Save Game via button\n"));
                            }

                            else if (inCheckBox && y >= trY0 && y < trY0 + rowH)
                            {
                                m_trashItemEnabled = !m_trashItemEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Trash Item toggle: {}\n"), m_trashItemEnabled ? 1 : 0);
                            }

                            else if (inCheckBox && y >= rpY0 && y < rpY0 + rowH)
                            {
                                m_replenishItemEnabled = !m_replenishItemEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Replenish Item toggle: {}\n"), m_replenishItemEnabled ? 1 : 0);
                            }

                            else if (inCheckBox && y >= raY0 && y < raY0 + rowH)
                            {
                                m_removeAttrsEnabled = !m_removeAttrsEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Remove Attributes toggle: {}\n"), m_removeAttrsEnabled ? 1 : 0);
                            }

                            else if (inCheckBox && y >= ptY0 && y < ptY0 + rowH)
                            {
                                m_pitchRotateEnabled = !m_pitchRotateEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Pitch Rotate toggle: {}\n"), m_pitchRotateEnabled ? 1 : 0);
                            }

                            else if (inCheckBox && y >= rlY0 && y < rlY0 + rowH)
                            {
                                m_rollRotateEnabled = !m_rollRotateEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Roll Rotate toggle: {}\n"), m_rollRotateEnabled ? 1 : 0);
                            }

                            else if (inKeyBox && y >= trY0 && y < trY0 + rowH)
                            {
                                s_capturingBind = BIND_TRASH_ITEM;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Trash Item (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_TRASH_ITEM], keyName(s_bindings[BIND_TRASH_ITEM].key));
                            }

                            else if (inKeyBox && y >= rpY0 && y < rpY0 + rowH)
                            {
                                s_capturingBind = BIND_REPLENISH_ITEM;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Replenish Item (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_REPLENISH_ITEM], keyName(s_bindings[BIND_REPLENISH_ITEM].key));
                            }

                            else if (inKeyBox && y >= raY0 && y < raY0 + rowH)
                            {
                                s_capturingBind = BIND_REMOVE_ATTRS;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Remove Attributes (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_REMOVE_ATTRS], keyName(s_bindings[BIND_REMOVE_ATTRS].key));
                            }

                            else if (inKeyBox && y >= ptY0 && y < ptY0 + rowH)
                            {
                                s_capturingBind = BIND_PITCH_ROTATE;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Pitch Rotate\n"));
                            }

                            else if (inKeyBox && y >= rlY0 && y < rlY0 + rowH)
                            {
                                s_capturingBind = BIND_ROLL_ROTATE;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Roll Rotate\n"));
                            }
                        }


                        if (m_ftSelectedTab == 2)
                        {
                            // Use full width for icon detection — icons are at the left of the content area
                            // Icon is 56px wide with 4px left padding — click area covers just the icon
                            int iconX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f) * s2p);
                            int iconX1 = static_cast<int>(iconX0 + 64.0f * s2p);

                            int entryStart = static_cast<int>(wTop + 40.0f * s2p + 30.0f * s2p);
                            int entryH = static_cast<int>(72.0f * s2p);

                            VLOG(STR("[MoriaCppMod] [Env] Click: cur=({},{}) iconX=[{},{}] entryStart={} count={}\n"),
                                 curX, curY, iconX0, iconX1, entryStart, s_config.removalCount.load());

                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= sz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            if (curX >= iconX0 && curX <= iconX1 && curY >= entryStart)
                            {
                                int y = curY - entryStart + static_cast<int>(scrollOff * s2p);
                                int entryIdx = y / entryH;
                                int count = s_config.removalCount.load();
                                if (entryIdx >= 0 && entryIdx < count)
                                {
                                    s_config.pendingRemoveIndex = entryIdx;
                                    VLOG(STR("[MoriaCppMod] [Settings] Delete removal entry {} via icon click\n"), entryIdx);
                                }
                            }
                        }


                        if (m_ftSelectedTab == 3)
                        {
                            int cbX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f) * s2p);
                            int cbX1 = static_cast<int>(cbX0 + 80.0f * s2p);
                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int sectionH = static_cast<int>(80.0f * s2p);
                            int rowH = static_cast<int>(128.0f * s2p);

                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            if (curX >= cbX0 && curX <= cbX1)
                            {
                                int y = curY - contentY + static_cast<int>(scrollOff * s2p);

                                int rowY = y - sectionH;
                                if (rowY >= 0)
                                {
                                    int idx = rowY / rowH;
                                    if (idx >= 0 && idx < static_cast<int>(m_ftGameModEntries.size()) && idx < MAX_GAME_MODS)
                                    {
                                        m_ftGameModEntries[idx].enabled = !m_ftGameModEntries[idx].enabled;
                                        if (UObject* chk = m_ftGameModCheckImages[idx].Get()) // v6.17.0 weakptr
                                        {
                                            auto* visFn = chk->GetFunctionByNameInChain(STR("SetVisibility"));
                                            if (visFn) { uint8_t vp[8]{}; vp[0] = m_ftGameModEntries[idx].enabled ? 0 : 2; safeProcessEvent(chk, visFn, vp); }
                                        }

                                        saveGameMods(m_ftGameModEntries);
                                        VLOG(STR("[MoriaCppMod] [Settings] Game Mod '{}' = {}\n"),
                                            utf8ToWide(m_ftGameModEntries[idx].name),
                                            m_ftGameModEntries[idx].enabled ? 1 : 0);
                                    }
                                }
                            }
                        }


                        // v6.4.1 — Cheats tab (index 4): existing 3 rows (Unlock/Read/Peace) at
                        // contentY + 0/128/256, plus the buff entries table beneath starting at
                        // contentY + 384. All entries at 1x-scale Y offsets are in m_buffRowTopYs +
                        // m_buffRowHeights; multiply by s2p and add scrollOff.
                        if (m_ftSelectedTab == 4)
                        {
                            // Read current scroll offset (scrollbox moves tab content upward)
                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            int btnLeft  = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f - 400.0f) * s2p);
                            int btnRight = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f) * s2p);

                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int scrollPx = static_cast<int>(scrollOff * s2p);

                            // Existing 3 rows (unscrolled at tab top — tab content follows scroll like a VBox)
                            auto rowY = [&](int y1x, int h1x, int& outTop, int& outBot) {
                                outTop = contentY - scrollPx + static_cast<int>(y1x * s2p);
                                outBot = outTop + static_cast<int>(h1x * s2p);
                            };

                            int unlockT, unlockB; rowY(0,   128, unlockT, unlockB);
                            int readT,   readB;   rowY(128, 128, readT,   readB);
                            int peaceT,  peaceB;  rowY(256, 128, peaceT,  peaceB);

                            // Diamond checkbox column (for Peace Mode row + buff toggles)
                            int cbX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f + 4.0f) * s2p);
                            int cbX1 = cbX0 + static_cast<int>(80.0f * s2p);

                            bool handled = false;

                            // --- Existing 3 rows (button click) ---
                            if (curX >= btnLeft && curX <= btnRight)
                            {
                                if (curY >= unlockT && curY <= unlockB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] UNLOCK clicked\n")); unlockAllAvailableRecipes(); handled = true; }
                                else if (curY >= readT && curY <= readB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] READ clicked\n"));   markAllLoreRead(); handled = true; }
                                else if (curY >= peaceT && curY <= peaceB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] PEACE/FIGHT clicked\n")); togglePeaceMode(); handled = true; }
                            }
                            // --- Peace Mode checkbox click ---
                            if (!handled && curX >= cbX0 && curX <= cbX1 && curY >= peaceT && curY <= peaceB)
                            { VLOG(STR("[MoriaCppMod] [Cheats] Peace checkbox clicked\n")); togglePeaceMode(); handled = true; }

                            // --- Buff entries (clear-all / headers / toggles) ---
                            if (!handled)
                            {
                                int count = (int)m_buffRowTopYs.size();
                                int ncEntries = 0;
                                const CheatEntry* entries = cheatEntries(ncEntries);
                                for (int i = 0; i < count && i < ncEntries; ++i)
                                {
                                    int rowT, rowB;
                                    rowY(m_buffRowTopYs[i], m_buffRowHeights[i], rowT, rowB);
                                    if (curY < rowT || curY > rowB) continue;

                                    if (entries[i].kind == CheatRowKind::ClearAllBtn)
                                    {
                                        if (curX >= btnLeft && curX <= btnRight)
                                        {
                                            VLOG(STR("[MoriaCppMod] [Cheats] CLEAR clicked\n"));
                                            clearAllBuffs();
                                            handled = true;
                                        }
                                    }
                                    else if (entries[i].kind == CheatRowKind::BuffToggle)
                                    {
                                        bool onCheckbox = (curX >= cbX0 && curX <= cbX1);
                                        bool onButton   = (curX >= btnLeft && curX <= btnRight);
                                        if (onCheckbox || onButton)
                                        {
                                            VLOG(STR("[MoriaCppMod] [Cheats] Toggle '{}' clicked (via {})\n"),
                                                 entries[i].label, onButton ? STR("button") : STR("checkbox"));
                                            toggleBuffEntry(i);
                                            handled = true;
                                        }
                                    }
                                    // Section headers are non-interactive.
                                    break;
                                }
                            }
                        }

                        // v6.4.1 — Tweaks tab (index 5). Click on a row's value button cycles.
                        if (m_ftSelectedTab == 5)
                        {
                            // Read scroll offset (same pattern as tab 4)
                            float scrollOff = 0.0f;
                            if (UObject* sb = m_ftScrollBox.Get())
                            {
                                auto* getScrollFn = sb->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(sb, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            int btnLeft  = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f - 400.0f) * s2p);
                            int btnRight = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f) * s2p);
                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int scrollPx = static_cast<int>(scrollOff * s2p);

                            int count = (int)m_tweakRowTopYs.size();
                            int nTweaks = 0;
                            const TweakEntry* entries = tweakEntries(nTweaks);
                            for (int i = 0; i < count && i < nTweaks; ++i)
                            {
                                int rowT = contentY - scrollPx + static_cast<int>(m_tweakRowTopYs[i] * s2p);
                                int rowB = rowT + static_cast<int>(m_tweakRowHeights[i] * s2p);
                                if (curY < rowT || curY > rowB) continue;

                                if (entries[i].kind != TweakKind::SectionHeader &&
                                    curX >= btnLeft && curX <= btnRight)
                                {
                                    VLOG(STR("[MoriaCppMod] [Tweaks] '{}' clicked\n"), entries[i].label);
                                    cycleTweakValue(i);
                                }
                                break;
                            }
                        }
                    }
                }
                s_lastFtLMB = lmbDown;


                static bool s_ftCaptureKeyPrev[256]{};
                if (s_capturingBind >= 0 && s_capturingBind < BIND_COUNT)
                {
                    if (s_ftCaptureSkipTick)
                    {
                        s_ftCaptureSkipTick = false;
                        VLOG(STR("[MoriaCppMod] [Settings] Capture skip tick for bind {}\n"), s_capturingBind.load());
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                            s_ftCaptureKeyPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    }
                    else
                    {
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                        {
                            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                                vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
                                vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU)
                                continue;
                            bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            bool wasDown = s_ftCaptureKeyPrev[vk];
                            s_ftCaptureKeyPrev[vk] = nowDown;
                            if (!nowDown || wasDown) continue;
                            if (vk == VK_ESCAPE)
                            {
                                s_capturingBind = -1;
                                updateFontTestKeyLabels();
                                break;
                            }
                            int idx = s_capturingBind.load();
                            VLOG(STR("[MoriaCppMod] [Settings] Key captured: VK=0x{:02X} for bind {}\n"), vk, idx);
                            if (idx >= 0 && idx < BIND_COUNT)
                            {
                                s_bindings[idx].key = static_cast<uint8_t>(vk);
                                s_capturingBind = -1;
                                saveConfig();
                                updateFontTestKeyLabels();
                                s_overlay.needsUpdate = true;
                                s_pendingKeyLabelRefresh = true;
                            }
                            break;
                        }
                    }
                }
            }


            {

                if (m_ftVisible && m_ftSelectedTab == 2)
                {
                    int curCount = s_config.removalCount.load();
                    if (curCount != m_ftLastRemovalCount)
                        rebuildFtRemovalList();
                }

                }

            {
                int removeIdx = s_config.pendingRemoveIndex.load();
                if (removeIdx >= 0)
                {
                    RemovalEntry toRemove;
                    bool valid = false;
                    if (s_config.removalCSInit)
                    {
                        CriticalSectionLock removalLock(s_config.removalCS);
                        if (removeIdx < static_cast<int>(s_config.removalEntries.size()))
                        {
                            toRemove = s_config.removalEntries[removeIdx];
                            valid = true;
                        }
                    }
                    if (valid)
                    {
                        if (toRemove.isTypeRule)
                        {
                            m_typeRemovals.erase(toRemove.meshName);
                        }
                        else
                        {
                            for (size_t i = 0; i < m_savedRemovals.size(); i++)
                            {
                                if (m_savedRemovals[i].meshName == toRemove.meshName)
                                {
                                    float dx = m_savedRemovals[i].posX - toRemove.posX;
                                    float dy = m_savedRemovals[i].posY - toRemove.posY;
                                    float dz = m_savedRemovals[i].posZ - toRemove.posZ;
                                    if (dx * dx + dy * dy + dz * dz < POS_TOLERANCE * POS_TOLERANCE)
                                    {
                                        m_savedRemovals.erase(m_savedRemovals.begin() + i);
                                        if (i < m_appliedRemovals.size())
                                            m_appliedRemovals.erase(m_appliedRemovals.begin() + i);
                                        break;
                                    }
                                }
                            }
                        }
                        rewriteSaveFile();
                        buildRemovalEntries();
                        if (m_ftVisible) m_deferRemovalRebuild = 2;
                        VLOG(STR("[MoriaCppMod] Config UI: removed entry {} ({})\n"),
                                                        removeIdx,
                                                        std::wstring(toRemove.friendlyName));
                    }
                    s_config.pendingRemoveIndex = -1;
                }
            }


            if (m_handleResolvePhase == HandleResolvePhase::Priming)
            {
                ULONGLONG now = GetTickCount64();
                ULONGLONG elapsed = now - m_handleResolveStartTime;

                if (elapsed > 5000)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] Timeout in Priming ({}ms), aborting\n"), elapsed);
                    if (isBuildTabShowing()) hideBuildTab();
                    m_handleResolvePhase = HandleResolvePhase::Done;
                }
                else if (m_buildTabAfterShowFired || isBuildTabShowing())
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] Build tab ready ({}ms)\n"), elapsed);
                    m_buildTabAfterShowFired = false;
                    m_buildMenuPrimed = true;
                    m_handleResolveSlotIdx = 0;
                    m_handleResolvePhase = HandleResolvePhase::Resolving;
                }

            }
            else if (m_handleResolvePhase == HandleResolvePhase::Resolving)
            {

                UObject* buildHUD = getCachedBuildHUD();
                if (!buildHUD)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] No BuildHUD found, aborting\n"));
                    hideBuildTab();
                    m_handleResolvePhase = HandleResolvePhase::Done;
                }
                else
                {
                    // Per-slot cooldown — 200ms between slot resolutions to let animations settle
                    bool cooldownActive = (m_handleResolveSlotIdx > 0 && (GetTickCount64() - m_lastHandleResolveSlotTime) < 200);

                    if (!cooldownActive) while (m_handleResolveSlotIdx < QUICK_BUILD_SLOTS)
                    {
                        auto& slot = m_recipeSlots[m_handleResolveSlotIdx];
                        if (slot.used && !slot.hasHandle && !slot.rowName.empty())
                            break;
                        m_handleResolveSlotIdx++;
                    }

                    if (cooldownActive)
                    {
                        // waiting for cooldown, skip this tick
                    }
                    else if (m_handleResolveSlotIdx >= QUICK_BUILD_SLOTS)
                    {
                        hideBuildTab();
                        m_handleResolvePhase = HandleResolvePhase::Done;
                        ULONGLONG totalMs = GetTickCount64() - m_handleResolveStartTime;
                        QBLOG(STR("[MoriaCppMod] [HandleResolve] Complete in {}ms\n"), totalMs);
                    }
                    else
                    {
                        int i = m_handleResolveSlotIdx;
                        RC::Unreal::FName fn(m_recipeSlots[i].rowName.c_str(), RC::Unreal::FNAME_Find);
                        uint32_t ci = fn.GetComparisonIndex();
                        uint32_t num = fn.GetNumber();

                        if (ci == 0)
                        {
                            QBLOG(STR("[MoriaCppMod] [HandleResolve] F{}: FName('{}') returned CI=0, skipping\n"),
                                  i + 1, m_recipeSlots[i].rowName);
                        }
                        else
                        {
                            uint8_t handle[RECIPE_HANDLE_SIZE]{};
                            std::memcpy(handle + 8, &ci, 4);
                            std::memcpy(handle + 12, &num, 4);

                            m_isAutoSelecting = true;
                            if (trySelectRecipeByHandle(buildHUD, handle))
                            {
                                cacheRecipeHandleForSlot(buildHUD, i);
                                QBLOG(STR("[MoriaCppMod] [HandleResolve] F{}: resolved '{}' CI={} hasHandle={}\n"),
                                      i + 1, m_recipeSlots[i].rowName, ci, m_recipeSlots[i].hasHandle);
                            }
                            else
                            {
                                QBLOG(STR("[MoriaCppMod] [HandleResolve] F{}: SelectRecipe failed for '{}'\n"),
                                      i + 1, m_recipeSlots[i].rowName);
                            }
                            m_isAutoSelecting = false;
                        }

                        m_handleResolveSlotIdx++;
                        m_lastHandleResolveSlotTime = GetTickCount64();
                    }
                }
            }


            if (m_buildMenuWasOpen && !isBuildTabShowing())
            {
                m_buildMenuWasOpen = false;

                m_cachedBuildComp = RC::Unreal::FWeakObjectPtr{};
                m_cachedBuildTab = RC::Unreal::FWeakObjectPtr{};
                m_cachedBuildHUD = RC::Unreal::FWeakObjectPtr{};
                refreshActionBar();
            }


            if (m_deferHideAndRefresh)
            {
                m_deferHideAndRefresh = false;
                if (isBuildTabShowing()) hideBuildTab();
                refreshActionBar();
            }

            if (m_deferRemovalRebuild == 2)
            {
                // Phase 1: close the F12 panel via toggle (deferred removal)
                m_deferRemovalRebuild = 1;
                if (m_ftVisible) toggleFontTestPanel(); // closes it
            }
            else if (m_deferRemovalRebuild == 1)
            {
                // Phase 2: reopen — toggle creates fresh panel with updated data
                m_deferRemovalRebuild = 0;
                if (!m_ftVisible) toggleFontTestPanel(); // reopens it
            }


            placementTick();
            tickPitchRoll();
            drainUnlockQueue();   // v6.4.1 — process 50 recipe-discovery calls per frame (no-op when queue empty)
            refreshActiveBuffs(); // v6.4.1 — re-apply toggled-on buffs every 5s so they don't expire
            tickJoinWorldUI();    // v6.6.0 — consume pending show/hide flags for mod-owned Join World UI
            tickAdvancedJoinUI(); // v6.6.0 — consume pending show/hide flags for mod-owned Advanced Join Options UI
            tickSettingsUI();     // v6.9.0 — Settings screen take-over (mod keybinds in keymap tab)
            tickReapplyModifierPrefixes(); // v6.9.0 — keep "L-SHIFT + F1" text on SET rows alive
            tickCaptureSpecialKeys();      // v6.9.0 — capture DEL/INS/HOME/etc the BP rejects
            tickNewBuildingBarHighlight(); // v6.14.0 — clear expired Phase 2 slot flashes
            tickReapplyCheatsContext();    // v6.9.0 — keep Cheats-tab visibility swap stable
            tickFGKDiscoveryDiag();        // v6.9.0 — one-shot probe of AMorDiscoveryManager.Recipes
            tickActorLookupDiag();         // v6.9.0 — Path #5 ActorRowNameLookup TMap byte-layout dump
            // v6.16.0 — Removed: tickFGKInjectionTest call. Function was
            // permanently disabled (datatable-fgk-cache-revisit.md) and
            // its body stripped in v6.14.0; the function itself is also
            // deleted in this version.

            // v6.9.0 CP3 — Quick Build chord-aware dispatch.
            //   USE (s_bindings[i].key, no modifiers): user-rebound USE
            //     chord — fires quickBuildSlot on rising edge.
            //   SET (s_setBindings[i].vk + modBits): user-rebound SET
            //     chord — fires assignRecipeSlot on rising edge.
            // Default-bound USE dispatch (F1..F8 alone) still flows
            // through register_keydown_event for low-latency keystroke;
            // the polling here handles non-default rebinds.
            if (!m_ftVisible && !isSettingsScreenOpen() &&
                m_handleResolvePhase == HandleResolvePhase::Done)
            {
                for (int i = 0; i < 8; ++i)
                {
                    if (!s_bindings[i].enabled) continue;
                    // SET chord
                    bool setHeld = isChordHeld(s_setBindings[i].vk, s_setBindings[i].modBits);
                    if (setHeld && !m_qbSetEdge[i])
                    {
                        m_qbSetEdge[i] = true;
                        assignRecipeSlot(i);
                    }
                    else if (!setHeld && m_qbSetEdge[i])
                    {
                        m_qbSetEdge[i] = false;
                    }
                    // USE chord (only if rebinding moved off F1..F8)
                    bool isDefaultFKey = (s_bindings[i].key == (uint8_t)(0x70 + i));
                    if (!isDefaultFKey)
                    {
                        bool useHeld = isChordHeld(s_bindings[i].key, 0);
                        if (useHeld && !m_qbUseEdge[i])
                        {
                            m_qbUseEdge[i] = true;
                            quickBuildSlot(i);
                        }
                        else if (!useHeld && m_qbUseEdge[i])
                        {
                            m_qbUseEdge[i] = false;
                        }
                    }
                }
            }

            // Session history right-click → confirm → delete
            if (m_modJoinWorldWidget.Get())
            {
                pollRightClickDeleteSessionHistory();
            }
            tickSessionHistoryConfirm();
            tickSessionHistoryDeferredCapture();

            // Esc / close: native flow handles all dismissal — we only modify
            //   widgets in place, so Esc on the native widget runs the BP's
            //   own ClosePanel/back-nav logic. We just clear our tracking
            //   pointers when the widget is no longer alive (handled below).
            if (m_modJoinWorldWidget.Get() == nullptr)
            {
                // Auto-clear stale state when native widget is gone
                static bool s_loggedClear = false;
                if (!s_loggedClear) { s_loggedClear = true; }
            }

            // (legacy hit-test mouse handlers removed — we now modify the native
            //  widget in place, so its real buttons handle Advanced/Close/etc.)

            if (!m_replayActive) return;
            m_frameCounter++;

            // Server-fly sweep: must run BEFORE the m_characterLoaded gate below,
            // because on a dedicated server m_characterLoaded is never true (no local
            // pawn) and the code returns early. The sweep needs to run on dedi servers
            // and listen-server hosts to set client-auth movement flags on all dwarves.
            if ((m_characterLoaded || m_isDedicatedServer) && intervalElapsed(m_lastServerFlySweep, 2000))
            {
                std::vector<UObject*> dwarves;
                UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
                constexpr uint8_t ROLE_Authority = 3;
                for (auto* pawn : dwarves)
                {
                    if (!pawn || !isObjectAlive(pawn)) continue;
                    auto* roleProp = pawn->GetPropertyByNameInChain(STR("Role"));
                    if (!roleProp) continue;
                    uint8_t role = *reinterpret_cast<uint8_t*>(
                        reinterpret_cast<uint8_t*>(pawn) + roleProp->GetOffset_Internal());
                    if (role != ROLE_Authority) continue;

                    auto** cmcPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement"));
                    if (!cmcPtr || !*cmcPtr || !isObjectAlive(*cmcPtr)) continue;
                    setBoolProp(*cmcPtr, L"bIgnoreClientMovementErrorChecksAndCorrection", true);
                    setBoolProp(*cmcPtr, L"bServerAcceptClientAuthoritativePosition", true);
                }
            }


            if (m_characterLoaded && intervalElapsed(m_lastWorldCheck, 1000))
            {
                // MP fix: check if LOCAL pawn still exists, not any dwarf in the world
                UObject* localPawn = getPawn();
                if (!localPawn)
                {
                    VLOG(STR("[MoriaCppMod] Character lost Ã¢â‚¬â€ world unloading, resetting replay state\n"));
                    m_characterLoaded = false;
                    m_characterHidden = false;
                    m_flyMode = false;
                    m_snapEnabled = true;
                    m_savedMaxSnapDistance = -1.0f;
                    m_buildMenuPrimed = false;
                    m_localPC = nullptr;
                    m_localPawn = nullptr;

                    m_cachedBuildComp = RC::Unreal::FWeakObjectPtr{};
                    m_cachedBuildHUD = RC::Unreal::FWeakObjectPtr{};
                    m_cachedBuildTab = RC::Unreal::FWeakObjectPtr{};
                    m_bpShowMouseCursor = nullptr;
                    m_lastPickedUpItemClass = nullptr;
                    m_lastPickedUpItemName.clear();
                    m_lastPickedUpDisplayName.clear();
                    m_lastPickedUpCount = 0;
                    std::memset(m_lastItemHandle, 0, 20);
                    m_lastItemInvComp = RC::Unreal::FWeakObjectPtr{};
                    m_qbPhase = PlacePhase::Idle;
                    m_showSettleTime = 0;
                    m_offTraceResults = -1;
                    m_offLastTraceResults = -1;
                    m_offTargetRotation = -1;
                    m_offCopiedComponents = -1;
                    m_offRelativeRotation = -1;
                    m_offRelativeLocation = -1;
                    m_isTargetBuild = false;
                    m_lastTargetBuildable = false;
                    m_targetBuildName.clear();
                    m_targetBuildRowName.clear();
                    m_buildMenuWasOpen = false;
                    m_handleResolvePhase = HandleResolvePhase::None;
                    m_pendingQuickBuildSlot = -1;
                    m_hasLastCapture = false;
                    m_hasLastHandle = false;
                    m_lastCapturedName.clear();
                    for (auto& slot : m_recipeSlots)
                    {
                        slot.hasBLockData = false;
                        slot.hasHandle = false;
                    }
                    s_overlay.visible = false;
                    m_initialReplayDone = false;
                    m_inventoryAuditDone = false;
                    m_definitionsApplied = false;
                    m_processedComps.clear();
                    m_undoStack.clear();
                    m_stuckLogCount = 0;
                    m_lastRescanTime = 0;
                    m_lastStreamCheck = 0;
                    m_lastBubbleCheck = 0;
                    m_worldLayout = nullptr;
                    m_currentBubbleId.clear();
                    m_currentBubbleName.clear();
                    m_currentBubble = nullptr;
                    m_replay = {};

                    m_appliedRemovals.assign(m_appliedRemovals.size(), false);
                    m_deferHideAndRefresh = false;
                    m_deferRemovalRebuild = 0;
                    m_gameHudVisible = true;
                    m_inFreeCam = false;

                    m_umgBarWidget = nullptr;
                    for (int i = 0; i < 8; i++)
                    {
                        m_umgStateImages[i] = nullptr;
                        m_umgIconImages[i] = nullptr;
                        m_umgIconTextures[i] = nullptr;
                        m_umgIconNames[i].clear();
                        m_umgSlotStates[i] = UmgSlotState::Empty;
                        m_umgKeyLabels[i] = nullptr;
                        m_umgKeyBgImages[i] = nullptr;
                    }
                    m_activeBuilderSlot = -1;
                    m_umgSetBrushFn = nullptr;
                    m_umgTexEmpty = nullptr;
                    m_umgTexInactive = nullptr;
                    m_umgTexActive = nullptr;
                    m_umgTexBlankRect = nullptr;
                    m_fontTestWidget = nullptr;
                    m_ftVisible = false;
                    for (auto& t : m_ftTabImages) t = nullptr;
                    for (auto& t : m_ftTabLabels) t = nullptr;
                    m_ftTabActiveTexture = nullptr;
                    m_ftTabInactiveTexture = nullptr;
                    m_ftSelectedTab = 0;
                    m_ftScrollBox = FWeakObjectPtr(); // v6.17.0 weakptr
                    for (auto& c : m_ftTabContent) c = nullptr;
                    for (auto& l : m_ftKeyBoxLabels) l = nullptr;
                    for (auto& c : m_ftCheckImages) c = FWeakObjectPtr(); // v6.17.0 weakptr reset
                    m_ftModBoxLabel = nullptr;
                    m_ftControllerCheckImg = nullptr;
                    m_ftControllerProfileLabel = nullptr;
                    m_ftNoCollisionCheckImg = nullptr;
                    m_ftNoCollisionLabel = nullptr;
                    m_ftNoCollisionKeyLabel = nullptr;
                    m_ftRemovalVBox = nullptr;
                    m_ftRemovalHeader = nullptr;
                    m_ftLastRemovalCount = -1;
                    for (auto& c : m_ftGameModCheckImages) c = FWeakObjectPtr(); // v6.17.0 weakptr reset
                    m_ftGameModEntries.clear();
                    m_ftRenameWidget = nullptr;
                    m_ftRenameInput = nullptr;
                    m_ftRenameConfirmLabel = nullptr;
                    m_ftRenameVisible = false;
                    m_trashDlgWidget = nullptr;
                    m_trashDlgVisible = false;
                    m_trashDlgOpenTick = 0;

                    m_mcBarWidget = nullptr;
                    for (int i = 0; i < MC_SLOTS; i++)
                    {
                        m_mcStateImages[i] = nullptr;
                        m_mcIconImages[i] = nullptr;
                        m_mcSlotStates[i] = UmgSlotState::Empty;
                        m_mcKeyLabels[i] = nullptr;
                        m_mcKeyBgImages[i] = nullptr;
                    }
                    m_mcRotationLabel = nullptr;
                    m_mcSlot0Overlay = nullptr;
                    m_mcSlot6Overlay = nullptr;
                    m_mcSlot8Overlay = nullptr;

                    m_abBarWidget = nullptr;
                    m_abKeyLabel = nullptr;
                    m_abStateImage = nullptr;
                    m_toolbarsVisible = false;

                    m_hoveredToolbar = -1;
                    m_hoveredSlot = -1;
                    m_lastClickLMB = false;

                    m_repositionMode = false;
                    m_dragToolbar = -1;
                    m_repositionMsgWidget = FWeakObjectPtr(); // v6.17.0 weakptr reset
                    m_repositionInfoBoxWidget = FWeakObjectPtr(); // v6.17.0 weakptr reset

                    m_targetInfoWidget = nullptr;
                    m_tiTitleLabel = nullptr;
                    m_tiClassLabel = nullptr;
                    m_tiNameLabel = nullptr;
                    m_tiDisplayLabel = nullptr;
                    m_tiPathLabel = nullptr;
                    m_tiBuildLabel = nullptr;
                    m_tiRecipeLabel = nullptr;
                    m_tiShowTick = 0;

                    m_crosshairWidget = nullptr;
                    m_crosshairShowTick = 0;

                    m_errorBoxWidget = nullptr;
                    m_ebMessageLabel = nullptr;
                    m_ebShowTick = 0;

                    clearStabilityHighlights();
                }
            }


            if (!m_characterLoaded)
            {
                if (intervalElapsed(m_lastCharPoll, 500))
                {
                    // MP fix: detect LOCAL pawn specifically, not any dwarf in the world
                    // Also verify it's actually a BP_FGKDwarf_C (not a lobby/menu pawn)
                    UObject* localPawn = getPawn();
                    if (localPawn && safeClassName(localPawn).find(STR("BP_FGKDwarf")) != std::wstring::npos)
                    {
                        m_characterLoaded = true;
                        m_charLoadTime = GetTickCount64();
                        m_localPC = findPlayerController();
                        m_localPawn = getPawn();
                        VLOG(STR("[MoriaCppMod] Character loaded — PC={:p} Pawn={:p}, waiting 15s before replay\n"),
                             (void*)m_localPC, (void*)m_localPawn);

                        // Controller blocking at character load removed — UE4's bBlockInput
                        // and DisableInput don't prevent the callout Gameplay Ability from firing.

                        // Server fly: set client-authoritative movement on all characters at login.
                        // Tells the server to trust client positions (allows client fly to work).
                        // MP fix: only set server-fly flags on the LOCAL player's pawn
                        if (m_localPawn && isObjectAlive(m_localPawn))
                        {
                            auto** cmcPtr = m_localPawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement"));
                            if (cmcPtr && *cmcPtr && isObjectAlive(*cmcPtr))
                            {
                                auto* cmc = *cmcPtr;
                                setBoolProp(cmc, L"bIgnoreClientMovementErrorChecksAndCorrection", true);
                                setBoolProp(cmc, L"bServerAcceptClientAuthoritativePosition", true);
                                VLOG(STR("[MoriaCppMod] [ServerFly] Set client-auth movement on local pawn {:p}\n"),
                                     (void*)m_localPawn);
                            }
                        }

                        // v6.4.4+ — re-apply persisted Cheats + Tweaks state now that the
                        // player's ASC + DataTables + world are all ready. No-op if nothing saved.
                        applySavedCheatsAndTweaks();

                        // v6.10.0 — auto-spawn the New Building Bar once the
                        // player + world are ready. One attempt per session.
                        if (!m_newBuildingBarSpawnAttempted)
                        {
                            m_newBuildingBarSpawnAttempted = true;
                            createNewBuildingBar();
                        }
                    }
                }
                return;
            }

            ULONGLONG msSinceChar = GetTickCount64() - m_charLoadTime;


            if (!m_initialReplayDone && msSinceChar >= 15000)
            {
                m_initialReplayDone = true;
                if (!m_savedRemovals.empty() || !m_typeRemovals.empty())
                {
                    VLOG(STR("[MoriaCppMod] Starting initial replay (15s after char load)...\n"));
                    migrateRemovalsToBubbles();
                    startReplay();
                }
            }

            if (m_initialReplayDone && !m_inventoryAuditDone && msSinceChar >= 20000)
            {
                m_inventoryAuditDone = true;
                VLOG(STR("[MoriaCppMod] [InvAudit] Running one-shot inventory audit (20s post-load)...\n"));
                auditInventory();
            }


            if (m_replay.active)
            {
                processReplayBatch();
            }


            // Bubble tracking — poll every 1s
            if (m_initialReplayDone && intervalElapsed(m_lastBubbleCheck, 30000))
            {
                if (updateCurrentBubble())
                {
                    VLOG(STR("[MoriaCppMod] [Bubble] Bubble changed — clearing processed comps for next scan\n"));
                    m_processedComps.clear();
                }
            }

            if (m_initialReplayDone && !m_replay.active && intervalElapsed(m_lastStreamCheck, 3000))
            {
                checkForNewComponents();
            }



            if (m_initialReplayDone && !m_replay.active && intervalElapsed(m_lastRescanTime, 60000) && hasPendingRemovals())
            {
                int pending = pendingCount();
                VLOG(STR("[MoriaCppMod] Periodic rescan ({} pending)...\n"), pending);
                m_processedComps.clear();
                startReplay();
                if (m_stuckLogCount == 0 && pending > 0)
                {
                    m_stuckLogCount++;
                    VLOG(STR("[MoriaCppMod] === Pending entries ({}) ===\n"), pending);
                    for (size_t i = 0; i < m_savedRemovals.size(); i++)
                    {
                        if (m_appliedRemovals[i]) continue;
                        std::wstring meshW(m_savedRemovals[i].meshName.begin(), m_savedRemovals[i].meshName.end());
                        VLOG(STR("[MoriaCppMod]   PENDING [{}]: {} @ ({:.1f},{:.1f},{:.1f})\n"),
                                                        i,
                                                        meshW,
                                                        m_savedRemovals[i].posX,
                                                        m_savedRemovals[i].posY,
                                                        m_savedRemovals[i].posZ);
                    }
                }
            }
        }

        // Update thread tick — called ~5ms on UE4SS's dedicated update thread.
        // Intentionally empty: all logic moved to gameThreadTick() which runs on the game thread.
        // on_update() is kept as a required override but does no work.
        auto on_update() -> void override
        {
        }
    };
}

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT RC::CppUserModBase* start_mod()
    {
        return new MoriaMods::MoriaCppMod();
    }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
