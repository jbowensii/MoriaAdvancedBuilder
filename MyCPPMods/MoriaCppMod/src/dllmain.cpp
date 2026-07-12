// MoriaCppMod - Return to Moria UE4SS C++ mod. v8.2.0 "Pre-Goat Release"
// Top-level mod class lives here; subsystem implementations are #included via .inl files.

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
        UObject* m_currentBubble{nullptr};  // cached for bubble-local coord calc
        PSOffsets m_ps;
        std::vector<bool> m_appliedRemovals;


        ULONGLONG m_lastWorldCheck{0};
        ULONGLONG m_lastCharPoll{0};
        // [rc.120 STORAGECAP 2026-07-11] Armed full-call capture window (NUM9):
        // logs every unique PE call for 30s while the user crafts/places/opens
        // storage and saves — reference flows for native persistence.
        ULONGLONG m_storageCapUntilMs{0};
        std::set<std::wstring> m_storageCapSeen;
        int m_storageCapLines{0};
        // [rc.40] Bell toggle cooldown — 2 seconds between summon/dismiss toggles.
        ULONGLONG m_lastBellToggleMs{0};
        // [rc.52] Goat companion settings (persisted via MoriaCppMod.ini [GoatCompanion]).
        std::wstring m_goatName{ L"Rûdh" };  // default "Rûdh"
        // Goat-menu state — UMG widget pre-created at character-load, visibility toggled.
        UObject* m_goatMenuWidget{nullptr};
        bool     m_goatMenuVisible{false};
        // [rc.22] Test 3 saddlebag widget — spawned on saddlebag click, dismissed by ESC.
        UObject* m_test3SaddlebagWidget{nullptr};
        // [rc.25] Manage-capture diagnostic window. When NUM0 is pressed,
        // m_mngCapEndMs = now + 5000ms. While active, PE-pre logs any UFunction
        // whose context class matches dwarf/NPC/PlayerController/UIManager/etc.
        // Lets us diff vanilla dwarf-Manage vs goat-Saddlebags UFunction chains.
        ULONGLONG m_mngCapEndMs{0};
        ULONGLONG m_lastGoatMenuMs{0};  // E-press dedupe cooldown
        bool     m_goatModalPending{false};  // [Phase 4] set in PE-pre, consumed on tick

        // [Phase 6 / Path α] Submenu state — fresh UI_WBP_InteractionMenu_C
        // spawned on Details-press, populated with 7 cloned rows. We own its
        // cursor/input completely (vanilla cursor doesn't navigate our rows
        // — known from Path A probes — so we poll input + drive SetIsSelected
        // ourselves).
        UObject*  m_goatSubMenu{nullptr};
        bool      m_goatSubMenuVisible{false};
        bool      m_goatSubMenuPending{false};
        UObject*  m_goatSubMenuRows[6]{};   // [0]=Follow [1]=Stay [2]=Saddlebags [3]=Feed [4]=Rename [5]=Dismiss (Wander dropped)
        UObject*  m_proximityMenuAtOpen{nullptr};  // cached at submenu-open time, for reliable hide-target
        int       m_goatSubMenuCursorIdx{0};
        UObject*  m_goatSubMenuTemplateRow{nullptr};  // cached vanilla Details row for clone source
        bool      m_renamingGoat{false};  // [Phase 6] when true, confirmRenameDialog routes typed name to m_goatName
        bool      m_keepUIModeAfterSubmenuClose{false};  // handlers that open their own popups set this so closeGoatSubmenu skips setInputModeGame
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

            // auto-migrate legacy pipe-delimited or @-prefixed entries to JSON format.
            if (sawLegacyLine)
            {
                VLOG(STR("[MoriaCppMod] Detected legacy-format entries - rewriting save file as JSON Lines\n"));
                rewriteSaveFile();
            }
            // Log first 5 saved mesh names for replay diagnostics
            for (size_t i = 0; i < std::min(m_savedRemovals.size(), (size_t)5); i++)
            {
                auto& sr = m_savedRemovals[i];
                VLOG(STR("[MoriaCppMod] [Saved-Diag] [{}] mesh='{}' pos=({},{},{}) bubble='{}'\n"),
                     i, std::wstring(sr.meshName.begin(), sr.meshName.end()),
                     sr.posX, sr.posY, sr.posZ,
                     std::wstring(sr.bubbleId.begin(), sr.bubbleId.end()));
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
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        // JSON-tagged display for type rules
                        std::string json = formatTypeRuleJson(entry.meshName);
                        entry.coordsW = std::wstring(json.begin(), json.end());
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
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        // JSON-tagged display: compact one-line object showing bubble id + name + world + local coords
                        char buf[512];
                        std::snprintf(buf, sizeof(buf),
                            "{\"bubble\":\"%s\",\"bubbleName\":\"%s\",\"world\":[%.1f,%.1f,%.1f],\"local\":[%.1f,%.1f,%.1f]}",
                            RemovalJson::escape(entry.bubbleId).c_str(),
                            RemovalJson::escape(entry.bubbleName).c_str(),
                            entry.posX, entry.posY, entry.posZ,
                            entry.localX, entry.localY, entry.localZ);
                        std::string s(buf);
                        entry.coordsW = std::wstring(s.begin(), s.end());
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

        // [v1.1.1 DIAG TOGGLE 2026-05-10] NUM. flips this. When true,
        // PE-pre callback logs every UFunction whose name contains any
        // rescue-related keyword (Settlement / Wanderer / Recruit /
        // Roster / Register / OnNpc / Despawn / Rescue / Interact)
        // WITHOUT dedup. Off by default — zero overhead for normal play.
        static inline bool s_diagMode{false};

        // Recipe unlock queue state (drained by main tick at UNLOCK_BATCH_SIZE/frame)
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
        // v6.9.0 CP3 - edge-detector for chord-aware Quick Build SET + USE.
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


        int m_activeBuilderSlot{-1};


        static constexpr int MC_SLOTS = 9;
        // New Building Bar — clone of WBP_UI_ActionBar_C used as visual chrome
        // for our 8 builder slots; native inventory wiring is suppressed.
        UObject* m_newBuildingBar{nullptr};
        bool m_newBuildingBarSpawnAttempted{false};
        // [rc.139] Master switch for the Advanced Builder subsystem (NBB bar,
        // F1-F8 quick build, MC keys, handle-resolve priming). ALWAYS starts
        // inactive each session (user directive — zero builder processing
        // until explicitly enabled via BIND_AB_TOGGLE, default '=').
        bool m_advBuilderActive{false};
        // Pre-spawned at character-load (NOT lazily on first placement tick) —
        // constructing this widget on the same Slate frame as ghost spawn races
        // the engine's UMG work and produces SWidget::Prepass AVs at 0xFF...
        bool m_rotDisplaySpawnAttempted{false};
        UObject* m_nbbSlotEmpty[8]{};   // empty-state UImage
        UObject* m_nbbSlotFocus[8]{};   // focused-state UImage (visibility toggled for highlight)
        UObject* m_nbbSlotIcon[8]{};    // icon UImage (Phase 2 - builder piece icon)
        UObject* m_nbbSlotKeyLbl[8]{};  // F-key UTextBlock
        UObject* m_nbbSlotMarker[8]{};  // numbered marker UImage above each slot
        UObject* m_nbbSlotButton[8]{};  // UButton wrapper (Phase 2 - for clicks)
        UObject* m_nbbSlotKeyBg[8]{};   // grey rect under the F# label
        // Per-session texture cache. Resolved once in nbbDiscoverAssets;
        // pointers are stable for the lifetime of the session.
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



        UObject* m_fontTestWidget{nullptr};
        bool m_ftVisible{false};
        UObject* m_ftTabImages[6]{};
        UObject* m_ftTabLabels[6]{};
        UObject* m_ftTabActiveTexture{nullptr};
        UObject* m_ftTabInactiveTexture{nullptr};
        int m_ftSelectedTab{0};
        UObject* m_ftScrollBox{nullptr};
        UObject* m_ftTabContent[6]{};

        // Cheats tab - widget refs for action buttons and toggle state
        UObject* m_ftCheatsUnlockBtnImg{nullptr};
        UObject* m_ftCheatsReadBtnImg{nullptr};

        // Peace Mode - zero MaxSpawnLimit on AMorAISpawnManager to suppress new spawns.
        bool m_peaceModeEnabled{false};
        bool m_pendingPeaceMode{false};                // loaded from INI, applied on char load
        float m_savedMaxSpawnLimit{-1.0f};             // -1 = not yet captured
        UObject* m_ftPeaceCheckImg{nullptr};           // check icon (visible when enabled)
        UObject* m_ftPeaceBtnLabel{nullptr};           // button text ("PEACE"/"FIGHT")
        UObject* m_ftPeaceCheckBoxOl{nullptr};         // checkbox hit-test ref
        UObject* m_ftPeaceBtnImg{nullptr};             // button hit-test ref

        // Cheats tab buff toggles - parallel arrays to cheatEntries().
        std::vector<bool>      m_buffStates;             // active/inactive state per entry
        std::vector<UObject*>  m_ftBuffCheckImgs;        // checkbox mark widget per entry
        std::vector<UObject*>  m_ftBuffBtnLabels;        // ON/OFF label per entry (nullptr for headers)
        std::vector<int>       m_buffRowTopYs;           // row top-Y offset within the content VBox (px, at 1x scale)
        std::vector<int>       m_buffRowHeights;         // row height (px, at 1x scale)
        int m_cheatsContentTotalHeight{0};               // total rendered height of the cheat entries area

        // Tweaks tab - parallel arrays to tweakEntries(). Each entry cycles through a set
        // of integer values; applyFieldTweak modifies all DataTable rows whose RowStruct has the field.
        std::vector<int>       m_tweakCurrentIdx;        // current index in cycleValues (0 = DEFAULT)
        std::vector<UObject*>  m_ftTweakBtnLabels;       // button text widget per entry
        std::vector<int>       m_tweakRowTopYs;          // row top-Y per entry (px, at 1x scale)
        std::vector<int>       m_tweakRowHeights;        // row height per entry (px, at 1x scale)
        // Key: (rowData pointer + fieldName), Value: original value before any tweak.
        // Used to restore DEFAULT and to compute multipliers from the original baseline.
        std::unordered_map<std::wstring, double> m_tweakOriginals;
        UObject* m_ftKeyBoxLabels[BIND_COUNT]{};
        UObject* m_ftCheckImages[BIND_COUNT]{};
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
        // UserWidget host for our standalone EditableTextBox in the rename popup.
        // Released alongside m_ftRenameWidget on hideRenameDialog.
        FWeakObjectPtr m_ftRenameInputUW;
        // Hovering the popup's Confirm/Cancel buttons steals keyboard focus
        // from the EditableTextBox after 1-2 keystrokes; tickRenameFocus
        // re-asserts focus on m_ftRenameInput each frame while visible.
        bool m_renameFocusReassertNeeded{false};
        ULONGLONG m_renameFocusLastReassertMs{0};

        // HUD reposition mode (BIND_REPOSITION_HUD, default F10).
        // While true: inspect window + 4-circle rotation display are forced
        // visible so the user can drag them. ESC or another F10 press exits.
        bool m_repositionHudMode{false};
        bool m_repositionHudPrevTargetInfoVisible{false};
        bool m_ftRenameVisible{false};
        // True when the in-game native modal (WBP_UI_RenameWorldModal_C) is
        // up. The modal owns its own keyboard input — we must NOT poll
        // RETURN/ESCAPE/LBUTTON or the BP confirm fires twice / focus chain
        // fights itself. Set in showRenameDialog_v2; cleared in hideRenameDialog.
        bool m_ftRenameUsingModal{false};
        // Force-loaded once and held across the session. The modal asset
        // gets GC-unloaded between opens otherwise; StaticFindObject would
        // return null on the second open.
        FWeakObjectPtr m_renameModalCls;

        // Rotation display: 4-cell pyramid (top: F9 step 5°-90°; bottom row:
        // Yaw/Pitch/Roll of active build piece). Always visible after character
        // load — the player has no cursor focus while building so a build-mode-
        // only widget couldn't be dragged. Throttled to 4 Hz.
        // Position persisted to ini under [Positions] RotDisplayX/Y.
        UObject* m_rotDisplayWidget{nullptr};
        UObject* m_rotDisplayStep{nullptr};
        UObject* m_rotDisplayYaw{nullptr};
        UObject* m_rotDisplayPitch{nullptr};
        UObject* m_rotDisplayRoll{nullptr};
        // Per-cell key marker TextBlocks; refreshed each tick from current
        // bindings so rebinds reflect live.
        UObject* m_rotDisplayStepKey{nullptr};
        UObject* m_rotDisplayYawKey{nullptr};
        UObject* m_rotDisplayPitchKey{nullptr};
        UObject* m_rotDisplayRollKey{nullptr};
        UObject* m_rotInvArmorTex{nullptr}; // cached frame texture
        float    m_rotDispPosX{-1.0f};      // fraction; -1 = use default
        float    m_rotDispPosY{-1.0f};
        bool     m_rotDispDragActive{false};
        int      m_rotDispDragOffsetX{0};
        int      m_rotDispDragOffsetY{0};
        bool     m_rotDispLMBPrev{false};
        // m_rotDispLastTickMs declared in moria_widgets.inl alongside tickRotationDisplay
        UObject* m_trashDlgWidget{nullptr};
        bool m_trashDlgVisible{false};
        ULONGLONG m_trashDlgOpenTick{0};

        UObject* m_ftRemovalVBox{nullptr};
        UObject* m_ftRemovalHeader{nullptr};
        int m_ftLastRemovalCount{-1};

        static constexpr int MAX_GAME_MODS = 16;
        UObject* m_ftGameModCheckImages[MAX_GAME_MODS]{};
        std::vector<GameModEntry> m_ftGameModEntries;


        bool m_toolbarsVisible{true};
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
        // v7.1.x: cached "is the build HUD currently visible?" state.
        // Set by the OnAfterShow / OnAfterHide PE post-hooks on
        // UI_WBP_BuildHUDv2_C. Replaces the per-tick PE polling that
        // isPlacementActive() used to do every 250ms — when this is
        // false, isPlacementActive() returns false immediately with
        // no PE dispatch.
        bool m_buildHudShowing{false};


        ScreenCoords m_screen;


        UClass* m_wllClass{nullptr};

        // TB_COUNT=4 retained for ABI of m_toolbarPosX/Y/SizeW/H + TB_DEF_X/Y.
        // Only index 3 is live (inspect window position via tickTargetInfoDrag);
        // indices 0-2 are unused. Do not compress - other code indexes by [3].
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
        // title bar / drag / close
        UObject* m_tiTitleBar{nullptr};
        UObject* m_tiCloseButton{nullptr};
        bool     m_tiDragActive{false};
        int      m_tiDragOffsetX{0}; // cursor-px - widget-px at drag start
        int      m_tiDragOffsetY{0};
        bool     m_tiLMBPrev{false}; // edge detector for LMB
        ULONGLONG m_tiAutoHideAtMs{0}; // computed at show-time

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

        #include "moria_npc_recovery.inl"

        #include "moria_goat.inl"

        #include "moria_goat_save_probes.inl"

      public:


        MoriaCppMod()
        {
            ModVersion = STR("8.2.0");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("Advanced builder, HISM removal, quick-build hotbar, UMG config menu");

            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            VLOG(STR("[MoriaCppMod] Loaded v8.2.0 \"Pre-Goat Release\" (Advanced Builder now a master toggle [=], starts OFF; NPC unstuck on-demand [-]; automatic NPC scanning removed; goat companion work in progress)\n"));
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
            // UE4SS working dir is the canonical I/O root - process CWD may differ.
            // Path must be converted via WideCharToMultiByte(CP_UTF8); never
            // static_cast<char> on wchar_t - U+2122 (™) in Steam's folder collapses
            // to 0x22 (") and silently corrupts every file path.
            {
                auto wd = UE4SSProgram::get_program().get_working_directory();
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
            VLOG(STR("[MoriaCppMod] Loaded v8.2.0 (workDir={})\n"),
                 utf8PathToWide(s_ue4ssWorkDir));

            // Startup diag: log resolved paths + GetFileAttributes result.
            // Mangled chars in the logged path indicate a wide-path conversion regression.
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
            // [rc.139] Unstuck NPCs (on-demand CantReach teleport pass)
            s_bindings[BIND_UNSTUCK_NPCS].label = Loc::get("bind.unstuck_npcs");
            s_bindings[BIND_UNSTUCK_NPCS].section = Loc::get("bind.section_general");

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
                // Quick Build USE: F-key alone fires quickBuildSlot. Skip ONLY if the
                // currently-held modifier+key combo is this slot's SET chord (avoid
                // double-firing). Generic modifier filtering breaks F-keys when overlays
                // like Discord/Steam transiently hold SHIFT.
                register_keydown_event(fkeys[i], [this, i]() {
                    // [v8.2.x] Honor the ASSIGNED binding, not the raw F-key:
                    // if the user rebound Quick Build i off its default F-key,
                    // this event must go dead — the polling dispatcher owns
                    // the new key. (Previously a rebind left BOTH keys live.)
                    if (s_bindings[i].key != (uint8_t)(0x70 + i)) return;
                    // [rc.139] Advanced Builder master switch — quick build
                    // is inert until the user enables the subsystem.
                    if (!m_advBuilderActive) return;
                    if (m_ftVisible) {
                        VLOG(STR("[QuickBuild] F{} BP-USE dropped: m_ftVisible\n"), i+1);
                        return;
                    }
                    if (isSettingsScreenOpen()) {
                        VLOG(STR("[QuickBuild] F{} BP-USE dropped: settings screen open\n"), i+1);
                        return;
                    }
                    if (!s_bindings[i].enabled) return;
                    if (m_handleResolvePhase != HandleResolvePhase::Done) {
                        VLOG(STR("[QuickBuild] F{} BP-USE dropped: handleResolvePhase != Done (={})\n"),
                             i+1, (int)m_handleResolvePhase);
                        return;
                    }
                    bool sh = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
                    bool ct = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool al = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
                    uint8_t curMod = (uint8_t)((sh?1:0) | (ct?2:0) | (al?4:0));
                    // Only bail if THIS slot's SET chord matches what's held.
                    // SET chord must be on the same VK as this F-key AND require
                    // a non-zero modifier mask AND that mask matches what's held.
                    bool isSetChord =
                        (s_setBindings[i].vk     == (uint8_t)(0x70 + i)) &&
                        (s_setBindings[i].modBits != 0) &&
                        (s_setBindings[i].modBits == curMod);
                    if (isSetChord) {
                        // Polling will fire SET on rising edge.
                        return;
                    }
                    quickBuildSlot(i);
                });
            }


            // v7.1.x: NUM* binding fully retired. Previously toggled
            // the legacy GDI+ overlay HUD (m_showHotbar / s_overlay)
            // which was deprecated at v6.10.0 when the New Building
            // Bar UMG widget replaced it. Reveal-Map moved to the
            // pause-menu button stack. NUM* is now free.



            // Pitch/Roll keybinds register lazily after config load (registerPitchRollKeys).

            s_instance = this;

            // MP helper: walk a UObject's Outer chain looking for the local PC or
            // pawn (cached at character load). Fail-open (returns true) when the
            // local PC isn't cached yet, so single-player isn't gated.
            // Note: UMG widgets have a WidgetTree Outer, not a PC/Pawn — UI hooks
            // below don't (and shouldn't) use this; on a client only the local
            // player's UMG instances exist.
            static auto isLocalContext = [](UObject* context) -> bool {
                if (!context || !s_instance) return true;
                if (!s_instance->m_localPC) return true;  // not cached yet - fail-open

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

            // [rc.120 STORAGECAP-POST 2026-07-11] Post-hook: capture the OUTPUT
            // of StoreRuntimeActor calls (native or ours) — ReturnValue + the
            // engine-filled 32B record handle. The pre-hook can't see these.
            Unreal::Hook::RegisterProcessEventPostCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance || !func || !parms) return;
                const auto fnName = func->GetName();
                const wchar_t* fnStr = fnName.c_str();
                if (!wcsstr(fnStr, STR("StoreRuntimeActor"))) return;
                bool ret = false;
                if (auto* pRet = s_instance->findParam(func, STR("ReturnValue")))
                    ret = *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(parms) + pRet->GetOffset_Internal());
                std::wstring handleHex;
                if (auto* pH = s_instance->findParam(func, STR("InOutRuntimeActorHandle")))
                {
                    uint8_t* h = reinterpret_cast<uint8_t*>(parms) + pH->GetOffset_Internal();
                    wchar_t buf[8];
                    int hs = pH->GetSize() > 64 ? 64 : pH->GetSize();
                    for (int i = 0; i < hs; i++)
                    {
                        swprintf(buf, 8, STR("%02X"), h[i]);
                        handleHex += buf;
                        if ((i & 3) == 3) handleHex += STR(" ");
                    }
                }
                VLOG(STR("[MoriaCppMod] [StorageCap rc.120 POST] {} ret={} handle=[{}] ctx={}\n"),
                     fnStr, ret, handleHex.c_str(), context ? safeClassName(context).c_str() : STR("?"));
            });

            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;


                const auto fnName = func->GetName();
                const wchar_t* fnStr = fnName.c_str();

                // [rc.97 GA_BELL DIAG 2026-07-10] Does Tobi's native bell fire ANY
                // ability / summon logic on use? Cheap fn-name filter first
                // (feedback_filter_pe_by_function_name_first), then log the context
                // class. Catches ExecuteUbergraph_GA_Bell, PorterGoat/Loader graphs,
                // and any Summon* call. If NOTHING here fires when the bell is used,
                // Tobi's bell has no native summon wired. Dedup-capped.
                if (wcsstr(fnStr, STR("Bell")) || wcsstr(fnStr, STR("Summon"))
                    || wcsstr(fnStr, STR("PorterGoat")) || wcsstr(fnStr, STR("SpawnNpc"))
                    || wcsstr(fnStr, STR("ActivateAbility")))
                {
                    static std::set<std::wstring> s_bellDiagSeen;
                    std::wstring cctx;
                    if (context) { try { cctx = context->GetClassPrivate()->GetName(); } catch (...) {} }
                    std::wstring key = cctx + STR("::") + fnStr;
                    if (s_bellDiagSeen.size() < 400 && s_bellDiagSeen.insert(key).second)
                        VLOG(STR("[MoriaCppMod] [BellDiag rc.97] ctx='{}' fn='{}'\n"),
                             cctx.empty() ? STR("?") : cctx.c_str(), fnStr);
                }

                // [rc.120 STORAGECAP 2026-07-11] Armed capture window (NUM9):
                // log EVERY unique fn+ctx pair for 30s while the user performs
                // reference actions (craft bag, place chest, open, save).
                // Noise-blacklisted + deduped + line-capped; registration fns
                // (BagWatch tier1 below) log unconditionally regardless.
                if (s_instance->m_storageCapUntilMs != 0)
                {
                    ULONGLONG nowCap = GetTickCount64();
                    if (nowCap >= s_instance->m_storageCapUntilMs)
                    {
                        VLOG(STR("[MoriaCppMod] [StorageCap rc.120] === window CLOSED: {} unique calls logged ===\n"),
                             s_instance->m_storageCapLines);
                        s_instance->m_storageCapUntilMs = 0;
                        s_instance->m_storageCapSeen.clear();
                    }
                    else if (s_instance->m_storageCapLines < 4000)
                    {
                        // noise blacklist (pure per-frame chatter)
                        bool noise =
                            wcsstr(fnStr, STR("Tick"))     || wcsstr(fnStr, STR("Anim"))   ||
                            wcsstr(fnStr, STR("Blend"))    ||   // [rc.131] "Cursor" un-blacklisted — it hid ServerHandleCursorDrop (the real pack-drop call)
                            wcsstr(fnStr, STR("Hover"))    || wcsstr(fnStr, STR("Mouse"))  ||
                            wcsstr(fnStr, STR("Camera"))   || wcsstr(fnStr, STR("Movement")) ||
                            wcsstr(fnStr, STR("Footstep")) || wcsstr(fnStr, STR("Audio"))  ||
                            wcsstr(fnStr, STR("Sound"))    || wcsstr(fnStr, STR("Breath")) ||
                            wcsstr(fnStr, STR("Stamina"))  || wcsstr(fnStr, STR("GetHealth"));
                        if (!noise)
                        {
                            std::wstring cctx = context ? safeClassName(context) : STR("?");
                            std::wstring key = cctx + STR("::") + fnStr;
                            if (s_instance->m_storageCapSeen.insert(key).second)
                            {
                                s_instance->m_storageCapLines++;
                                std::wstring objn = context ? safeObjectName(context) : STR("?");
                                VLOG(STR("[MoriaCppMod] [StorageCap rc.120] fn='{}' ctx={}('{}')\n"),
                                     fnStr, cctx.c_str(), objn.c_str());
                            }
                        }
                    }
                }

                // [rc.122 SAVE-MOMENT SNAPSHOT 2026-07-11] StorageCap found the
                // save trigger: MorCheatManager::SaveSystemAutoSave fires once
                // per save (manual + auto). Snapshot the goat saddlebag at that
                // exact moment — same "serialize at save time" semantics the
                // game uses for chest contents.
                if (wcscmp(fnStr, STR("SaveSystemAutoSave")) == 0 && s_instance->m_characterLoaded)
                {
                    VLOG(STR("[MoriaCppMod] [Sidecar rc.122] SaveSystemAutoSave detected — snapshotting saddlebag\n"));
                    try { s_instance->snapshotGoatSaddlebag(); } catch (...) {}
                    // [rc.127 B6 2026-07-11] Register the GOAT ACTOR in the
                    // level records at save-moment. Records serialize FULL actor
                    // state (how dwarf/chest/dropped-item contents persist
                    // without SaveGame flags) → goat + inventory + cargo ride
                    // the save wholesale. AutoRestore adopts the record-respawned
                    // goat on load (no duplicate; dedupe guards).
                    try {
                        for (auto& g : s_instance->m_followGoats)
                        {
                            UObject* p = g.pawn.Get();
                            if (p && isObjectAlive(p))
                            {
                                VLOG(STR("[MoriaCppMod] [B6 rc.127] save-moment — StoreRuntimeActor(goat)\n"));
                                s_instance->storeGoatInWorldState(p);
                                // [rc.136] stash retired — the pack now LIVES in the
                                // player inventory permanently (final architecture);
                                // nothing to move at save time.
                                // s_instance->b7SaveMomentStash(p);
                                break;
                            }
                        }
                    } catch (...) {}
                }

                // [rc.117 BAGWATCH 2026-07-11] Container-lifecycle tap. User
                // creates bags/chests in-game; we log how the GAME creates,
                // save-registers, opens and closes them — the reference flow
                // for goat-saddlebag persistence. Two tiers (cheap fn-name
                // check first per feedback_filter_pe_by_function_name_first):
                //   tier1: registration/open/close fns — always logged
                //   tier2: lifecycle/interact fns — only on container-ish classes
                {
                    bool tier1 =
                        wcsstr(fnStr, STR("StoreRuntimeActor"))   || wcsstr(fnStr, STR("RuntimeActor")) ||
                        wcsstr(fnStr, STR("OpenChest"))           || wcsstr(fnStr, STR("CloseChest")) ||
                        wcsstr(fnStr, STR("Storage Container"))   || wcsstr(fnStr, STR("CreateStorageWidget")) ||
                        wcsstr(fnStr, STR("SaveGameObject"))      || wcsstr(fnStr, STR("StorageChanged")) ||
                        wcsstr(fnStr, STR("IsStorageChest"));
                    bool tier2 = !tier1 && (
                        wcscmp(fnStr, STR("ReceiveBeginPlay")) == 0 ||
                        wcsstr(fnStr, STR("ReceiveEndPlay"))   != nullptr ||
                        wcsstr(fnStr, STR("ReceiveDestroyed")) != nullptr ||
                        wcsstr(fnStr, STR("ServerInteract"))   != nullptr ||
                        wcsstr(fnStr, STR("OnInteract"))       != nullptr);
                    if (tier1 || tier2)
                    {
                        std::wstring cctx = context ? safeClassName(context) : STR("?");
                        bool ctxIsContainer =
                            cctx.find(STR("Chest"))         != std::wstring::npos ||
                            cctx.find(STR("Saddle"))        != std::wstring::npos ||
                            cctx.find(STR("EpicPack"))      != std::wstring::npos ||
                            cctx.find(STR("ContainerItem")) != std::wstring::npos ||
                            cctx.find(STR("Storage"))       != std::wstring::npos ||
                            cctx.find(STR("Receptacle"))    != std::wstring::npos;
                        if (tier1 || ctxIsContainer)
                        {
                            std::wstring extra;
                            // StoreRuntimeActor: log WHICH actor is being registered
                            if (wcsstr(fnStr, STR("StoreRuntimeActor")) && parms && func)
                            {
                                if (auto* pActor = s_instance->findParam(func, STR("Actor")))
                                {
                                    UObject* a = *reinterpret_cast<UObject**>(
                                        reinterpret_cast<uint8_t*>(parms) + pActor->GetOffset_Internal());
                                    if (a) extra = STR(" actor=") + safeClassName(a) + STR("/") + safeObjectName(a);
                                }
                                if (auto* pStab = s_instance->findParam(func, STR("bStoreStability")))
                                {
                                    bool st = *reinterpret_cast<bool*>(
                                        reinterpret_cast<uint8_t*>(parms) + pStab->GetOffset_Internal());
                                    extra += st ? STR(" bStoreStability=1") : STR(" bStoreStability=0");
                                }
                            }
                            std::wstring objn = context ? safeObjectName(context) : STR("?");
                            VLOG(STR("[MoriaCppMod] [BagWatch rc.117] fn='{}' ctx={}('{}'){}\n"),
                                 fnStr, cctx.c_str(), objn.c_str(), extra.c_str());
                        }
                    }
                }

                // [rc.25 MANAGE CAPTURE 2026-05-26] When user arms NUM0,
                // log every UFunction call on dwarf/NPC/PC/UIManager-related
                // contexts for 5 seconds. Lets us diff the vanilla dwarf
                // Manage flow against goat Saddlebags. Cheap early-out: bail
                // if window inactive.
                if (s_instance->m_mngCapEndMs != 0)
                {
                    ULONGLONG mcNow = GetTickCount64();
                    if (mcNow < s_instance->m_mngCapEndMs)
                    {
                        // [rc.28 2026-05-27] Pre-filter the noisy per-frame
                        // functions (animation, BT updates, socket queries,
                        // reticle CenterCheck, etc.) before the class-name
                        // check. These swamp the meaningful events otherwise.
                        bool noise =
                            wcscmp(fnStr, STR("OnUpdate")) == 0 ||
                            wcscmp(fnStr, STR("ReceiveTick")) == 0 ||
                            wcscmp(fnStr, STR("Tick")) == 0 ||
                            wcscmp(fnStr, STR("CenterCheck")) == 0 ||
                            wcscmp(fnStr, STR("OnCurrentTargetChanged")) == 0 ||
                            wcscmp(fnStr, STR("OnIconStateUpdated")) == 0 ||
                            wcscmp(fnStr, STR("BlueprintUpdateAnimation")) == 0 ||
                            wcscmp(fnStr, STR("BlueprintPostEvaluateAnimation")) == 0 ||
                            wcscmp(fnStr, STR("GetSocketLocation")) == 0 ||
                            wcscmp(fnStr, STR("DoesSocketExist")) == 0 ||
                            wcscmp(fnStr, STR("K2_GetPawn")) == 0 ||
                            wcscmp(fnStr, STR("K2_GetActorLocation")) == 0 ||
                            wcscmp(fnStr, STR("K2_GetActorRotation")) == 0 ||
                            wcscmp(fnStr, STR("GetActorForwardVector")) == 0 ||
                            wcscmp(fnStr, STR("GetViewportSize")) == 0 ||
                            wcscmp(fnStr, STR("MoveToActor")) == 0 ||
                            wcsstr(fnStr, STR("EvaluateGraphExposedInputs_")) == fnStr ||
                            wcsstr(fnStr, STR("ExecuteUbergraph_")) == fnStr ||
                            wcsstr(fnStr, STR("AnimGraphNode_")) != nullptr;
                        if (!noise)
                        {
                            std::wstring ctxCls = context ? safeClassName(context) : STR("(null)");
                            bool match =
                                ctxCls.find(STR("Dwarf"))            != std::wstring::npos ||
                                ctxCls.find(STR("Npc"))              != std::wstring::npos ||
                                ctxCls.find(STR("NPC"))              != std::wstring::npos ||
                                ctxCls.find(STR("PlayerController")) != std::wstring::npos ||
                                ctxCls.find(STR("UIManager"))        != std::wstring::npos ||
                                ctxCls.find(STR("UIScreen"))         != std::wstring::npos ||
                                ctxCls.find(STR("FGKUI"))            != std::wstring::npos ||
                                ctxCls.find(STR("Inventory_Screen")) != std::wstring::npos ||
                                ctxCls.find(STR("Interaction"))      != std::wstring::npos ||
                                ctxCls.find(STR("Goat"))             != std::wstring::npos ||
                                ctxCls.find(STR("Porter"))           != std::wstring::npos ||
                                ctxCls.find(STR("Manage"))           != std::wstring::npos ||
                                ctxCls.find(STR("Settlement"))       != std::wstring::npos ||
                                ctxCls.find(STR("FrontEndButton"))   != std::wstring::npos;
                            if (match)
                            {
                                VLOG(STR("[ManageCapture] fn='{}' ctx='{}' ctxPtr={:p}\n"),
                                     fnStr, ctxCls.c_str(), (void*)context);
                            }
                        }
                    }
                    else
                    {
                        s_instance->m_mngCapEndMs = 0;
                        VLOG(STR("[ManageCapture] === window closed (10s elapsed) ===\n"));
                    }
                }

                // v1.4.1-probe: Probe E — save-shaped UFunction arg capture.
                // PersistorWatch — log-only detection of BeginPlay / SaveGameObject*
                //                  calls on BP_PorterGoatPersistor_C (rc.54).
                // Early-out fast when probes disabled (the common case).
                if (s_instance->m_goatSaveProbesEnabled) {
                    s_instance->onProbeE_processEventPre(context, func, parms);
                    s_instance->onPersistorWatchPre(context, func);
                }

                // navTabPressed pre-hook: when user clicks the Cheats tab,
                // rewrite the tab name to "Gameplay" so the framework
                // displays the Gameplay tab content. Set a flag so the
                // Gameplay tab's OnAfterShow hook injects cheats content.
                if (wcscmp(fnStr, STR("navTabPressed")) == 0 && parms)
                {
                    s_instance->onNavTabPressedPre(context, func, parms);
                }

                // Goat E-press hooks — narrow targets known so far.
                // [rc.95 BELL-ONLY] disabled — let Tobi's native goat interaction run
                // if (wcscmp(fnStr, STR("ServerInteract")) == 0 && parms)
                // {
                //     s_instance->onGoatInteractPre(context, func, parms);
                // }
                // if (wcscmp(fnStr, STR("ServerRescueNpc")) == 0 && parms)
                // {
                //     s_instance->onGoatRescuePre(context, func, parms);
                // }
                // Narrow keyword diagnostic — kept tight because the per-PE
                // call cost adds up (this fires on every UFunction
                // dispatch, thousands per second). Only the substrings we
                // still actively need: "Interact" (for OnPressInteract /
                // OnReleaseInteract / OnSetInteraction etc on goat) and
                // "Rescue" (for legacy ServerRescueNpc visibility). Cheap
                // wcsstr — no lowercase, no per-call set lookup unless we
                // match.
                // [v1.1.1 DIAG MODE 2026-05-10] When NUM.-toggled diag
                // mode is ON, log EVERY PE-pre call matching any rescue-
                // chain keyword without dedup. Independent of the narrow
                // m_followGoats-gated filter below — fires regardless of
                // whether a goat is summoned, so it can capture a fresh
                // dwarf rescue from start to finish.
                if (s_diagMode)
                {
                    if (   wcsstr(fnStr, STR("Rescue"))     != nullptr
                        || wcsstr(fnStr, STR("Settlement")) != nullptr
                        || wcsstr(fnStr, STR("Wanderer"))   != nullptr
                        || wcsstr(fnStr, STR("Recruit"))    != nullptr
                        || wcsstr(fnStr, STR("Roster"))     != nullptr
                        || wcsstr(fnStr, STR("Register"))   != nullptr
                        || wcsstr(fnStr, STR("OnNpc"))      != nullptr
                        || wcsstr(fnStr, STR("Despawn"))    != nullptr
                        || wcsstr(fnStr, STR("Interact"))   != nullptr
                        || wcsstr(fnStr, STR("AddNpc"))     != nullptr
                        || wcsstr(fnStr, STR("RemoveNpc"))  != nullptr
                        || wcsstr(fnStr, STR("Recruited"))  != nullptr
                        || wcsstr(fnStr, STR("Rescued"))    != nullptr
                        // [rc.40 BELL DISCOVERY 2026-05-12] keywords likely
                        // to catch container-item right-click / use events.
                        || wcsstr(fnStr, STR("UseItem"))    != nullptr
                        || wcsstr(fnStr, STR("ServerUse")) != nullptr
                        || wcsstr(fnStr, STR("Activate"))   != nullptr
                        || wcsstr(fnStr, STR("OnItem"))     != nullptr
                        || wcsstr(fnStr, STR("RightClick")) != nullptr
                        || wcsstr(fnStr, STR("OnSlot"))     != nullptr
                        || wcsstr(fnStr, STR("OpenContainer")) != nullptr)
                    {
                        std::wstring ctxCls = context ? safeClassName(context) : L"<null>";
                        VLOG(STR("[MoriaCppMod] [DiagMode] PE-pre: '{}' on class={}\n"),
                             fnStr, ctxCls.c_str());
                    }
                }

                // [rc.40.2 BELL RIGHT-CLICK → TOGGLE 2026-05-12]
                // Hook target confirmed by rc.40.1 dump:
                //   fn='ItemRightClicked' ctx='WBP_UI_Inventory_Screen_C'
                //   parm[0] ItemHandle : StructProperty off=0x0 size=20
                //   FItemHandle layout: int32 ID at +0x00 (then Payload, WeakObjectPtr)
                //
                // We read the int32 ID from the parm buffer, look it up in
                // the player's MorInventoryComponent.Items FFastArraySerializer,
                // pull the item's TSubclassOf<AInventoryItem>, and compare
                // class name. If it's BP_PorterGoatBell_C → toggleGoatFromBell().
                //
                // All offsets reflection-resolved. iiSize/iiItemOff/iiIDOff
                // helpers (moria_reflection.h) walk the FItemInstance UStruct
                // at startup; fall back to FGK.hpp constants if reflection
                // fails. iiaListOff() resolves the FFastArraySerializer's
                // inner array offset the same way.
                if (wcscmp(fnStr, STR("ItemRightClicked")) == 0
                    && parms && func && s_instance
                    && s_instance->m_localPawn && isObjectAlive(s_instance->m_localPawn))
                {
                    const uint8_t* parmBytes = reinterpret_cast<const uint8_t*>(parms);
                    int32_t handleID = *reinterpret_cast<const int32_t*>(parmBytes + 0);
                    if (handleID != 0)
                    {
                        UObject* invComp = s_instance->findPlayerInventoryComponent(s_instance->m_localPawn);
                        if (invComp && isObjectAlive(invComp))
                        {
                            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
                            if (itemsProp)
                            {
                                uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp)
                                                  + itemsProp->GetOffset_Internal() + iiaListOff();
                                if (isReadableMemory(listBase, 16))
                                {
                                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                                    int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                                    if (arrData && arrNum > 0 && arrNum < 10000)
                                    {
                                        int stride  = iiSize();
                                        int itemOff = iiItemOff();
                                        int idOff   = iiIDOff();
                                        UClass* matchedCls = nullptr;
                                        for (int i = 0; i < arrNum; ++i)
                                        {
                                            uint8_t* entry = arrData + i * stride;
                                            int32_t entryID = *reinterpret_cast<int32_t*>(entry + idOff);
                                            if (entryID == handleID)
                                            {
                                                matchedCls = *reinterpret_cast<UClass**>(entry + itemOff);
                                                break;
                                            }
                                        }
                                        if (matchedCls && isObjectAlive(matchedCls))
                                        {
                                            std::wstring clsName;
                                            try { clsName = matchedCls->GetName(); } catch (...) {}
                                            if (clsName == STR("EQ_GoatBell_C"))
                                            {
                                                // [rc.98 2026-07-10] Summon re-enabled. Tobi's bell
                                                // (GA_Bell) is only a melee weapon — our mod owns
                                                // summon (spawns his BP_NpcGoat_C).
                                                VLOG(STR("[MoriaCppMod] [BellHook] *** bell right-clicked (ID={}) — firing toggleGoatFromBell ***\n"),
                                                     handleID);
                                                s_instance->toggleGoatFromBell();
                                            }
                                            // Other items: silent pass-through. We don't
                                            // suppress the vanilla right-click — context
                                            // menu still opens (the bell's 1×1 storage UI
                                            // will appear briefly; harmless for v1.1.6
                                            // testing, can suppress later if desired).
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // [rc.51 WIDE HOTBAR DISCOVERY 2026-05-12]
                // rc.50's ServerUse hook didn't fire on hotbar press. The
                // hotbar key-press for a container item must route through a
                // different UFunction (possibly OpenContainer, ServerEquip,
                // or an EnhancedInputAction event). Cache bell ID at first
                // sight from the inventory walk and log first-sight of any
                // UFunction whose first 4 parm bytes match. Catches the
                // dispatch regardless of name.
                if (parms && func && s_instance
                    && s_instance->m_localPawn && isObjectAlive(s_instance->m_localPawn))
                {
                    // Resolve bell ID once per session via the player's inventory.
                    static int32_t s_cachedBellID = 0;
                    static ULONGLONG s_lastBellIDScan = 0;
                    ULONGLONG nowMs = GetTickCount64();
                    if (s_cachedBellID == 0 && nowMs - s_lastBellIDScan > 2000)
                    {
                        s_lastBellIDScan = nowMs;
                        UObject* invComp = s_instance->findPlayerInventoryComponent(s_instance->m_localPawn);
                        if (invComp && isObjectAlive(invComp))
                        {
                            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
                            if (itemsProp)
                            {
                                uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp)
                                                  + itemsProp->GetOffset_Internal() + iiaListOff();
                                if (isReadableMemory(listBase, 16))
                                {
                                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                                    int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                                    if (arrData && arrNum > 0 && arrNum < 10000)
                                    {
                                        int stride  = iiSize();
                                        int itemOff = iiItemOff();
                                        int idOff   = iiIDOff();
                                        for (int i = 0; i < arrNum; ++i)
                                        {
                                            uint8_t* entry = arrData + i * stride;
                                            UClass* itemCls = *reinterpret_cast<UClass**>(entry + itemOff);
                                            if (itemCls && isObjectAlive(itemCls))
                                            {
                                                std::wstring n;
                                                try { n = itemCls->GetName(); } catch (...) {}
                                                if (n == STR("EQ_GoatBell_C"))
                                                {
                                                    s_cachedBellID = *reinterpret_cast<int32_t*>(entry + idOff);
                                                    VLOG(STR("[MoriaCppMod] [BellHook] cached bell ID={} for wide-discovery probe\n"),
                                                         s_cachedBellID);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // Probe: if first parm bytes equal cached bell ID, log fn.
                    if (s_cachedBellID != 0)
                    {
                        const uint8_t* parmBytes = reinterpret_cast<const uint8_t*>(parms);
                        int32_t maybeID = *reinterpret_cast<const int32_t*>(parmBytes + 0);
                        if (maybeID == s_cachedBellID)
                        {
                            static std::set<std::wstring> s_seenBellDispatchFns;
                            if (s_seenBellDispatchFns.size() < 100
                                && s_seenBellDispatchFns.insert(fnStr).second)
                            {
                                std::wstring ctxCls = context ? safeClassName(context) : L"<null>";
                                VLOG(STR("[MoriaCppMod] [BellHook] *** WIDE-MATCH fn='{}' ctx='{}' (bell ID at parm offset 0) ***\n"),
                                     fnStr, ctxCls.c_str());
                            }
                        }
                    }
                }

                // [rc.51.1 HOTBAR HOOK 2026-05-12]
                // Confirmed via WIDE-MATCH discovery: hotbar key dispatch for
                // container items fires UMorEquipComponent::ServerEquip(FItemHandle, EEquipMode).
                // NOT ServerUse — the engine treats hotbar-slot activation as
                // "equip" for containers. Same FItemHandle layout at parm
                // offset 0, same ID-match → toggleGoatFromBell.
                if ((wcscmp(fnStr, STR("ServerEquip")) == 0
                  || wcscmp(fnStr, STR("ServerUse")) == 0)
                    && parms && func && s_instance
                    && s_instance->m_localPawn && isObjectAlive(s_instance->m_localPawn))
                {
                    const uint8_t* parmBytes = reinterpret_cast<const uint8_t*>(parms);
                    int32_t handleID = *reinterpret_cast<const int32_t*>(parmBytes + 0);
                    if (handleID != 0)
                    {
                        UObject* invComp = s_instance->findPlayerInventoryComponent(s_instance->m_localPawn);
                        if (invComp && isObjectAlive(invComp))
                        {
                            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
                            if (itemsProp)
                            {
                                uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp)
                                                  + itemsProp->GetOffset_Internal() + iiaListOff();
                                if (isReadableMemory(listBase, 16))
                                {
                                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                                    int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                                    if (arrData && arrNum > 0 && arrNum < 10000)
                                    {
                                        int stride  = iiSize();
                                        int itemOff = iiItemOff();
                                        int idOff   = iiIDOff();
                                        UClass* matchedCls = nullptr;
                                        for (int i = 0; i < arrNum; ++i)
                                        {
                                            uint8_t* entry = arrData + i * stride;
                                            int32_t entryID = *reinterpret_cast<int32_t*>(entry + idOff);
                                            if (entryID == handleID)
                                            {
                                                matchedCls = *reinterpret_cast<UClass**>(entry + itemOff);
                                                break;
                                            }
                                        }
                                        if (matchedCls && isObjectAlive(matchedCls))
                                        {
                                            std::wstring clsName;
                                            try { clsName = matchedCls->GetName(); } catch (...) {}
                                            if (clsName == STR("EQ_GoatBell_C"))
                                            {
                                                // [rc.98 2026-07-10] summon re-enabled (our mod owns summon)
                                                VLOG(STR("[MoriaCppMod] [BellHook] *** bell ServerUse (hotbar) ID={} — firing toggleGoatFromBell ***\n"),
                                                     handleID);
                                                s_instance->toggleGoatFromBell();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // [rc.48 DISCOVERY PROBES 2026-05-12]
                //
                // (A) Hotbar slot activation discovery. When player presses
                //     hotbar number key (1-8) to "use" an item in that slot,
                //     some UFunction fires with the item handle as parm.
                //     We don't know the function name yet. Log first-sight
                //     of any function whose name suggests hotbar/quickslot
                //     dispatch, regardless of context.
                //
                // (B) Manage widget show discovery. When player E-interacts
                //     with our bell-spawned goat, the engine surfaces the
                //     vanilla dwarf "Standing By / Needs a Furnace" manage
                //     widget. To suppress it we need to know its class +
                //     show-event UFunction name. Log first-sight of any
                //     widget show event when the fn name suggests a
                //     manage/NPC-UI surface.
                {
                    bool hotbarLike =
                           wcsstr(fnStr, STR("Hotbar"))     != nullptr
                        || wcsstr(fnStr, STR("QuickSlot")) != nullptr
                        || wcsstr(fnStr, STR("SlotKey"))    != nullptr
                        || wcsstr(fnStr, STR("SlotPress")) != nullptr
                        || wcsstr(fnStr, STR("ActionBar")) != nullptr
                        || wcsstr(fnStr, STR("UseSlot"))    != nullptr
                        || wcsstr(fnStr, STR("ActivateSlot")) != nullptr
                        || wcsstr(fnStr, STR("OnSlotClicked")) != nullptr;
                    bool manageWidgetLike =
                           wcsstr(fnStr, STR("ManageScreen"))     != nullptr
                        || wcsstr(fnStr, STR("NpcManage"))         != nullptr
                        || wcsstr(fnStr, STR("WBP_NPC"))           != nullptr
                        || wcsstr(fnStr, STR("MorNpcManage"))      != nullptr
                        || wcsstr(fnStr, STR("NPCManage"))         != nullptr
                        || (wcsstr(fnStr, STR("OnAfterShow"))      != nullptr ||
                            wcsstr(fnStr, STR("OnBeforeShow"))     != nullptr ||
                            wcsstr(fnStr, STR("NativeBeforeShow")) != nullptr ||
                            wcsstr(fnStr, STR("NativeAfterShow"))  != nullptr);
                    if (hotbarLike || manageWidgetLike)
                    {
                        static std::set<std::wstring> s_seenHotbarFns;
                        if (s_seenHotbarFns.size() < 200
                            && s_seenHotbarFns.insert(fnStr).second)
                        {
                            std::wstring ctxCls = context ? safeClassName(context) : L"<null>";
                            const wchar_t* tag = hotbarLike ? STR("hotbar") : STR("widget");
                            VLOG(STR("[MoriaCppMod] [BellDiscover] {} fn='{}' ctx='{}'\n"),
                                 tag, fnStr, ctxCls.c_str());
                        }
                    }
                }

                if (!s_instance->m_followGoats.empty()
                    && (wcsstr(fnStr, STR("Interact")) != nullptr
                        || wcsstr(fnStr, STR("Rescue"))   != nullptr))
                {
                    static std::set<std::wstring> s_seenInteractFns;
                    if (s_seenInteractFns.size() < 80
                        && s_seenInteractFns.insert(fnStr).second)
                    {
                        std::wstring ctxCls = context ? safeClassName(context) : L"<null>";
                        VLOG(STR("[MoriaCppMod] [GoatPEDiag] PE-pre: '{}' on class={}\n"),
                             fnStr, ctxCls.c_str());
                    }
                    // First time we see UI_WBP_Interaction_C fire one of
                    // these, dump its full property+UFunction schema so
                    // we can find the visible-label FText and the target
                    // UObject* to override "Rescue" -> "Open".
                    if (context)
                    {
                        std::wstring ctxCls = safeClassName(context);
                        if (ctxCls == STR("UI_WBP_Interaction_C"))
                        {
                            // [Phase 3] silenced — InteractDump (271 lines/session) was
                            // the discovery probe; we know the schema now.
                            // s_instance->dumpInteractionWidgetSchema(context);
                        }
                    }
                }
                // BP_RequestSpawn full-args diagnostic (every call, not
                // one-shot). Logs spawner+context+class so we can compare
                // natural-spawn callers to our own attempts.
                if (wcscmp(fnStr, STR("BP_RequestSpawn")) == 0 && parms && func)
                {
                    s_instance->onBPRequestSpawnPre(context, func, parms);
                }
                // [v7.1.0-rc.51 SUSPENDED 2026-05-10] recruit-fire detector
                // commented out — paired with NUM- chain. Without the
                // setupGoatRecruit append (also suspended), there's nothing
                // tracked so this would never match anyway, but keep it
                // grouped under the same #if 0 for one-iteration test then
                // delete.
#if 0 // NUM_MINUS_RECRUIT_SUSPENDED_2026_05_10
                // [v1.2.8 RECRUIT FIRE DETECTOR] When our marker UFunction
                // name dispatches via PE, that means a multicast we
                // subscribed (OnWandererRecruited or OnNpcRescued) fired
                // for a tracked goat. Trigger the post-recruit handler.
                // Cheap fnStr cmp first; only resolve context if matched.
                if (wcscmp(fnStr, STR("MoriaModGoatRecruitMarker")) == 0
                    || wcscmp(fnStr, STR("OnNpcRescued_Event_2")) == 0)
                {
                    if (context && s_instance->isGoatPendingRecruit(context))
                    {
                        VLOG(STR("[MoriaCppMod] [Recruit] detected '{}' fire on tracked goat={:p} — running post-recruit handler\n"),
                             fnStr, (void*)context);
                        s_instance->onGoatRecruited(context);
                    }
                }
#endif
                // ServerSetInteractableCustomName: capture the chest the
                // player just renamed; if the new name is "goat" (case
                // insensitive) it becomes our persistent backing storage.
                if (wcscmp(fnStr, STR("ServerSetInteractableCustomName")) == 0
                    && parms && func)
                {
                    s_instance->onChestRenamePre(context, func, parms);
                }
                // [SUSPENDED 2026-05-09] InteractDiag wire — see
                // `chest-link-future.md`. ServerInteract is not the
                // chest-open path; logger removed to cut PE-pre overhead.
                // Tap-E on companion goat: bypass the failing rescue gate by
                // force-calling ExecuteInteraction on the widget when E is
                // pressed and the widget is bound to one of our goats.
                if (!s_instance->m_followGoats.empty()
                    && wcscmp(fnStr, STR("OnPressInteract")) == 0
                    && context)
                {
                    std::wstring ctxCls = safeClassName(context);
                    if (ctxCls == STR("UI_WBP_Interaction_C"))
                    {
                        s_instance->onInteractionPressPre(context);
                    }
                }
                // [rc.54 PATH A] OnSetInteractable on the menu — fires when
                // proximity widget opens for a new NPC. We hook PE-pre to
                // detect our goat early; deferred injection happens in
                // tickGoatMenuInject() so vanilla's row population finishes
                // first.
                // [Phase 6 / Path α] Scroll-wheel / arrow-key nav for our
                // submenu. Vanilla's input system fires OnSelectionNext/
                // OnSelectionPrevious on the menu — AND ALSO OnMoveNext/
                // OnMovePrevious on the currently-focused row. Hook both so
                // wheel events reach us regardless of which level vanilla
                // dispatches at.
                // Diagnostic: log any "Selection" / "Move" / "Scroll" /
                // "Navigate" UFunc that fires on our submenu or its rows
                // while submenu is visible. Dedup per-fnStr per session.
                if (s_instance->m_goatSubMenuVisible && context)
                {
                    bool isOurs = (context == s_instance->m_goatSubMenu);
                    if (!isOurs) {
                        for (int i = 0; i < 7; ++i)
                            if (s_instance->m_goatSubMenuRows[i] == context) { isOurs = true; break; }
                    }
                    if (isOurs &&
                        (wcsstr(fnStr, STR("Select")) ||
                         wcsstr(fnStr, STR("Move")) ||
                         wcsstr(fnStr, STR("Scroll")) ||
                         wcsstr(fnStr, STR("Navigate")) ||
                         wcsstr(fnStr, STR("Mouse"))))
                    {
                        static std::set<std::wstring> s_seen;
                        if (s_seen.size() < 60 && s_seen.insert(fnStr).second)
                            VLOG(STR("[MoriaCppMod] [GoatSubmenuDiag] '{}' on ctx={:p} (is-menu={})\n"),
                                 fnStr, (void*)context, context == s_instance->m_goatSubMenu);
                    }
                }

                if (s_instance->m_goatSubMenuVisible
                    && context == s_instance->m_goatSubMenu
                    && (wcscmp(fnStr, STR("OnSelectionNext")) == 0
                        || wcscmp(fnStr, STR("OnSelectionPrevious")) == 0))
                {
                    int dir = (wcscmp(fnStr, STR("OnSelectionNext")) == 0) ? +1 : -1;
                    s_instance->onGoatSubmenuScroll(dir);
                }
                // Restored 2026-05-14 — dropping this handler broke scroll
                // entirely. OnSelectionNext on the menu only fires on first
                // open; subsequent opens rely on OnMoveNext on row contexts.
                // 2-3x advance per tick was the user complaint; the 100ms
                // throttle inside onGoatSubmenuScroll dedupes that, so we can
                // safely listen to both signals again.
                if (s_instance->m_goatSubMenuVisible
                    && context
                    && (wcscmp(fnStr, STR("OnMoveNext")) == 0
                        || wcscmp(fnStr, STR("OnMovePrevious")) == 0))
                {
                    std::wstring ctxCls = safeClassName(context);
                    if (ctxCls == STR("UI_WBP_Interaction_C"))
                    {
                        int dir = (wcscmp(fnStr, STR("OnMoveNext")) == 0) ? +1 : -1;
                        s_instance->onGoatSubmenuScroll(dir);
                    }
                }

                // [Phase 4 — Path A hooks disabled]
                // OnSetInteractable + NavProbe + MenuProbe hooks were for
                // the row-injection discovery work. Path A is dead;
                // disabled to cut PE-pre overhead and log noise.
                #if 0
                if (!s_instance->m_followGoats.empty()
                    && wcscmp(fnStr, STR("OnSetInteractable")) == 0
                    && context)
                {
                    std::wstring ctxCls = safeClassName(context);
                    if (ctxCls == STR("UI_WBP_InteractionMenu_C"))
                    {
                        s_instance->onInteractMenuSetInteractablePre(context, func, parms);
                    }
                }
                #endif
                // Legacy v0.4 hook - keep in case some path still calls it.
                if (wcscmp(fnStr, STR("Initialize NavBar")) == 0 ||
                    wcscmp(fnStr, STR("InitializeNavBar")) == 0)
                {
                    s_instance->onInitializeNavBarPre(context, func, parms);
                }
                // Navbar UFunction discovery diagnostic - one-shot per name.
                if (s_verbose)
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

                if (wcscmp(fnStr, STR("RotatePressed")) == 0 || wcscmp(fnStr, STR("RotateCcwPressed")) == 0)
                {
                    if (s_instance->m_isDedicatedServer) return;
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

                // v1.4.1-probe: Probe C — save-lifecycle PE post-hook capture.
                // Probe F — broader save-subsystem filter + return-value reader.
                // Probe H — Interaction delegate capture (rc.54).
                // Early-out fast when probes disabled (the common case). Runs
                // before the MP guard so the dedicated-server case is logged too.
                if (s_instance->m_goatSaveProbesEnabled) {
                    s_instance->onProbeC_processEventPost(context, func);
                    s_instance->onProbeF_processEventPost(context, func, parms);
                    s_instance->onProbeH_processEventPost(context, func, parms);
                }

                // MP guard: skip UI/state hooks on dedicated server - each client has its own mod instance
                // Only OnPlayerEnteredBubble is allowed through (useful for server-side bubble tracking)
                if (s_instance->m_isDedicatedServer)
                {
                    if (wcscmp(fnStr2, STR("OnPlayerEnteredBubble")) == 0 && parms)
                    {
                        if (auto* pBubble = func->GetPropertyByNameInChain(STR("Bubble")))
                        {
                            auto* bubble = *reinterpret_cast<UObject**>(
                                static_cast<uint8_t*>(parms) + pBubble->GetOffset_Internal());
                            if (bubble && isObjectAlive(bubble))
                                s_instance->onBubbleEnteredEvent(bubble);
                        }
                    }
                    return;
                }

                // [v1.1.1 GATE OVERRIDE 2026-05-10] IsUsableBy on the
                // goat's MorNPCComponent returns false because the goat
                // hasn't actually joined the player's settlement (the
                // ownership/faction check fails internally). Override
                // the return value to true when the context is one of
                // our follower goats' MorNPCComponent. Cheap fnStr cmp
                // first, only resolve context's outer when name matches.
                if (wcscmp(fnStr2, STR("IsUsableBy")) == 0
                    && parms && context && !s_instance->m_followGoats.empty())
                {
                    UObject* outer = nullptr;
                    try { outer = context->GetOuterPrivate(); } catch (...) {}
                    if (outer && isObjectAlive(outer))
                    {
                        bool isOurGoat = false;
                        for (auto& g : s_instance->m_followGoats)
                        {
                            UObject* mine = g.pawn.Get();
                            if (mine && mine == outer) { isOurGoat = true; break; }
                        }
                        if (isOurGoat)
                        {
                            if (auto* pRet = findParam(func, STR("ReturnValue")))
                            {
                                bool* slot = reinterpret_cast<bool*>(
                                    static_cast<uint8_t*>(parms) + pRet->GetOffset_Internal());
                                if (!*slot)
                                {
                                    *slot = true;
                                    static int s_overrideLogsRemaining = 5;
                                    if (s_overrideLogsRemaining > 0)
                                    {
                                        --s_overrideLogsRemaining;
                                        VLOG(STR("[MoriaCppMod] [Goat] IsUsableBy overridden false->true on goat={:p} (remaining={})\n"),
                                             (void*)outer, s_overrideLogsRemaining);
                                    }
                                }
                            }
                        }
                    }
                }

                // [Phase 5] Re-enable OnShow PE-post hook on the vanilla
                // proximity menu to override the header text from "Citizen"
                // to "Porter Goat" when the menu shows for our goat.
                // (OnMoveNext/Previous force-cycling stays disabled — Path A
                // row injection is dead.)
                if (!s_instance->m_followGoats.empty()
                    && wcscmp(fnStr2, STR("OnShow")) == 0
                    && context)
                {
                    std::wstring ctxCls = safeClassName(context);
                    if (ctxCls == STR("UI_WBP_InteractionMenu_C"))
                    {
                        s_instance->onInteractMenuShownPost(context);
                    }
                }

                // rc.26: event-driven NPC blocked-bed trigger.
                // SetCurrentActivity is the canonical UFunction the
                // game calls when an NPC's activity transitions; its
                // companion MorNpcUpdateActivity broadcasts the
                // OnUpdateActivity multicast. Either fires the moment
                // an NPC's Activity becomes CantReachBed / etc — no
                // polling needed. Cheap function-name filter first
                // per feedback_filter_pe_by_function_name_first.md.
                // rc.46 BROAD DIAGNOSTIC: log EVERY UFunction called
                // on a UMorNPCComponent context, one-shot per unique
                // function name. If anything UFunction-shaped fires
                // during an activity change, this surfaces it. If
                // nothing does, that's conclusive evidence the change
                // is a direct C++ property write with no PE dispatch.
                //
                // Cheap function-name filter first per
                // feedback_filter_pe_by_function_name_first.md — we
                // only resolve safeClassName on a small handful of
                // candidate function names (anything starting with
                // a common activity/NPC-related prefix).
                {
                    bool maybeNpcFunc =
                        (wcsstr(fnStr2, STR("Activity")) != nullptr) ||
                        (wcsstr(fnStr2, STR("MorNpc")) != nullptr) ||
                        (wcsstr(fnStr2, STR("NPC")) != nullptr) ||
                        (wcsstr(fnStr2, STR("SetCurrent")) != nullptr) ||
                        (wcsstr(fnStr2, STR("Interrupted")) != nullptr);
                    if (maybeNpcFunc && context)
                    {
                        std::wstring ctxCls = safeClassName(context);
                        bool isNpcCtx = (ctxCls.find(STR("MorNPC")) != std::wstring::npos
                                      || ctxCls.find(STR("NpcDwarf")) != std::wstring::npos
                                      || ctxCls.find(STR("MorCharacter")) != std::wstring::npos);
                        if (isNpcCtx)
                        {
                            static std::set<std::wstring> s_seenBroad;
                            std::wstring key = std::wstring(fnStr2) + STR("@") + ctxCls;
                            if (s_seenBroad.insert(key).second)
                            {
                                VLOG(STR("[NpcRecovery] BROAD-DIAG: fn='{}' ctxCls='{}' (one-shot)\n"),
                                     fnStr2, ctxCls.c_str());
                            }
                        }
                    }
                }

                if (wcscmp(fnStr2, STR("SetCurrentActivity")) == 0 ||
                    wcscmp(fnStr2, STR("MorNpcUpdateActivity")) == 0)
                {
                    // rc.27 diag: one-shot log of each function name +
                    // each unique row-name we observe. Confirms whether
                    // the hook fires at all; if no log lines for these
                    // appear, the function names don't match the runtime.
                    {
                        static std::set<std::wstring> s_seenFn;
                        if (s_seenFn.insert(fnStr2).second)
                            VLOG(STR("[NpcRecovery] EVENT-HOOK first sighting: fn='{}'\n"), fnStr2);
                    }
                    if (parms)
                    {
                        // Parm name is "ActivityHandle" on
                        // SetCurrentActivity, "NpcActivity" on the
                        // delegate broadcast. Try both.
                        FProperty* pAct = func->GetPropertyByNameInChain(STR("ActivityHandle"));
                        if (!pAct) pAct = func->GetPropertyByNameInChain(STR("NpcActivity"));
                        if (pAct)
                        {
                            uint8_t* base = static_cast<uint8_t*>(parms) + pAct->GetOffset_Internal();
                            // FMorNPCActivityRowHandle layout:
                            // DataTable* @0 (8B), FName RowName @8 (8B).
                            auto* fname = reinterpret_cast<RC::Unreal::FName*>(base + 8);
                            std::wstring rowName;
                            try { rowName = fname->ToString(); } catch (...) {}
                            if (!rowName.empty())
                            {
                                // rc.27 diag: log unique row names too.
                                {
                                    static std::set<std::wstring> s_seenRow;
                                    if (s_seenRow.insert(rowName).second)
                                        VLOG(STR("[NpcRecovery] EVENT-HOOK row-name first sighting: '{}'\n"),
                                             rowName.c_str());
                                }
                                std::wstring lo = rowName;
                                for (auto& c : lo) c = (wchar_t)towlower(c);
                                // rc.44: widened to ANY cantreach*
                                // substring. Catches Furnace + future
                                // variants without re-shipping.
                                if (lo.find(STR("cantreach")) != std::wstring::npos)
                                {
                                    // context is the UMorNPCComponent.
                                    // rc.51: pass row name as hint for
                                    // activity-aware fallback routing.
                                    s_instance->onNpcBlockedActivityEvent(context, rowName);
                                }
                            }
                        }
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
                    s_instance->onTrashPopupButtonClicked(context); // v6.21.1 - Phase 4 trash popup
                    s_instance->maybeFireCarouselButton(context);
                    // BndEvt_..._{Prev,Next}Button_..._OnButton...
                    // delegates fire on the carousel itself; the fn name
                    // contains "PrevButton" or "NextButton".
                    s_instance->maybeFireCarouselViaDelegate(context, fnStr2);
                }

                // Native settings checkbox state-change.
                // BP delegate name: BndEvt__WBP_SettingsCheckBox_OptionCheckBox_K2Node_..._OnCheckBoxComponentStateChanged__DelegateSignature
                // The C++-level event UMorSettingsCheckBox::OnCheckBoxStateChanged is also fine.
                if ((wcsstr(fnStr2, STR("OnCheckBoxComponentStateChanged")) != nullptr ||
                     wcscmp(fnStr2, STR("OnCheckBoxStateChanged")) == 0) && parms)
                {
                    bool newState = false;
                    newState = *reinterpret_cast<bool*>(parms);
                    s_instance->maybeFireCheckBoxRow(context, newState);
                }

                // Native settings carousel value changed.
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

                // Cheats tab is rendered by piggy-backing the native Gameplay tab:
                //   pre-hook   navTabPressed: rewrite KeyName "Cheats" -> "Gameplay"
                //                so the framework displays the Gameplay widget;
                //   post-hook  navTabPressed: restore currentTabName to "Cheats" and
                //                refresh navbar selection so the visual highlight matches;
                //   GetNavBarTabs post-hook appends the Cheats entry to the OutArray
                //                (modifying tabArray alone is insufficient - BP iterates
                //                a copy).
                if (wcscmp(fnStr2, STR("navTabPressed")) == 0 && parms)
                {
                    s_instance->onNavTabPressedPost(context, func, parms);
                }

                // Popup-trace: gated by s_verbose AND fn-name filter BEFORE the
                // Outer-chain walk so production cost is zero. The filter list
                // excludes IsInViewport/IsHovered/Tick/mouse callbacks: walking
                // on those drove input interference in earlier builds.
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

                // Manual Direct/Local Join button click - fires the BndEvt
                // delegate handler on the AdvancedJoinOptions widget. We
                // DEFER reading the input field text to the next main tick
                // (queueManualJoinCapture) - calling GetText + Conv_TextToString
                // from inside this post-hook would re-enter ProcessEvent and is
                // a documented reentrancy hazard.
                if ((wcsstr(fnStr2, STR("Button_DirectJoinIP")) != nullptr ||
                     wcsstr(fnStr2, STR("Button_JoinLocal")) != nullptr) &&
                    wcsstr(fnStr2, STR("OnMenuButtonClicked")) != nullptr)
                {
                    bool isLocal = (wcsstr(fnStr2, STR("Button_JoinLocal")) != nullptr);
                    s_instance->queueManualJoinCapture(context, isLocal);
                }

                // BP join events: coarse name-match first, then narrow by parameter
                // signature. Direct C++ functions like DirectJoinSessionWithPassword
                // are not always UFunction-exposed; BP delegates always are.
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
                    // read those offsets blindly - it's safe since we're reading
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
                             std::wstring(entry.name.begin(), entry.name.end()).c_str(),
                             std::wstring(entry.domain.begin(), entry.domain.end()).c_str(),
                             std::wstring(entry.port.begin(), entry.port.end()).c_str());
                    }
                }

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

                        VLOG(STR("[SessionHistory] hooked {} - captured host='{}', port='{}', pwd-len={}\n"),
                             fnStr2,
                             utf8ToWide(domain).c_str(),
                             utf8ToWide(port).c_str(),
                             (int)passStr.size());
                    }
                }

                if (wcscmp(fnStr2, STR("OnAfterShow")) == 0)
                {
                    // Single safeClassName per OnAfterShow shared across all gates below;
                    // it's SEH-wrapped (per-call cost is non-trivial).
                    std::wstring cls = safeClassName(context);

                    // Popup tracker: log any widget whose class name contains
                    // Popup/Confirm/Dialog so we can discover the real class
                    // path of game-native confirmation popups.
                    if (cls.find(STR("Popup")) != std::wstring::npos ||
                        cls.find(STR("PopUp")) != std::wstring::npos ||
                        cls.find(STR("Confirm")) != std::wstring::npos ||
                        cls.find(STR("Dialog")) != std::wstring::npos)
                    {
                        std::wstring path;
                        try
                        {
                            UClass* uc = static_cast<UClass*>(context->GetClassPrivate());
                            if (uc) path = uc->GetFullName();
                        } catch (...) {}
                        VLOG(STR("[PopupTracker] OnAfterShow on cls='{}' fullClassPath='{}'\n"),
                             cls.c_str(), path.c_str());
                    }

                    // Patch the native CharacterCreator rename dialog at every
                    // show: bump MaxNameLength to 22 (BP CDO ships 12) and clear
                    // DisallowedWords. The BP's CheckNewCharacterName reads
                    // `this.MaxNameLength` so writing on the instance is enough —
                    // no CDO patching, no pak mod. Re-applied on every
                    // OnAfterShow because the BP can re-construct from CDO
                    // between shows.
                    if (cls == STR("WBP_CharacterCreatorRenameDialog_C"))
                    {
                        if (auto* mnlPtr = context->GetValuePtrByPropertyNameInChain<int32_t>(STR("MaxNameLength")))
                        {
                            int32_t prev = *mnlPtr;
                            *mnlPtr = 22;
                            VLOG(STR("[MoriaCppMod] [Rename] patched MaxNameLength on native dialog: {} -> 22\n"),
                                 prev);
                        }
                        if (auto* dwPtr = context->GetValuePtrByPropertyNameInChain<FText>(STR("DisallowedWords")))
                        {
                            // Empty FText = no filter. Leave the existing
                            // FText shell intact and reset its content.
                            try { *dwPtr = FText(STR("")); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [Rename] cleared DisallowedWords on native dialog\n"));
                        }
                    }
                    if (cls == STR("UI_WBP_BuildHUDv2_C"))
                    {
                        // v7.1.x: cache build-HUD visibility so
                        // isPlacementActive() can early-exit without
                        // a per-frame PE call. Replaces the polling
                        // loop in tickRotationDisplay (4 PE/sec).
                        s_instance->m_buildHudShowing = true;
                    }
                    if (cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        s_instance->m_buildTabAfterShowFired = true;
                        s_instance->m_buildMenuPrimed = true;
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow fired on Build_Tab\n"));

                        if (s_instance->m_qbPhase == PlacePhase::WaitingForShow)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow: recording settle time (350ms delay)\n"));
                            s_instance->m_showSettleTime = GetTickCount64();
                            // Don't transition yet - let placementTick handle after settle delay
                        }
                    }
                    // v6.6.0+ - intercept Join Other World screen and replace with mod-owned duplicate
                    else if (cls == STR("WBP_UI_JoinWorldScreen_C"))
                    {
                        s_instance->onNativeJoinWorldShown(context);
                    }
                    // Sister widget - AdvancedJoinOptions panel that opens when user
                    // clicks "Advanced Join Options" on the JoinWorld screen.
                    else if (cls == STR("WBP_UI_AdvancedJoinOptions_C"))
                    {
                        s_instance->onNativeAdvancedJoinShown(context);
                    }
                    // Settings take-over - capture every Settings-related widget
                    // that fires OnAfterShow. Main SettingsScreen also queues a tree dump.
                    else if (cls == STR("WBP_SettingsScreen_C") ||
                             cls == STR("UI_WBP_EscapeMenu2_C"))
                    {
                        s_instance->onNativeSettingsScreenShown(context);
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        // inject mod action buttons (Unlock/
                        // Read All/Clear All Buffs) into the pause menu's
                        // VerticalBox_0 right above LeaveButton.
                        if (cls == STR("UI_WBP_EscapeMenu2_C"))
                            s_instance->injectPauseMenuButtons(context);
                    }
                    // Cheats tab content piggy-backs WBP_LegalTab_C; the
                    // m_cheatsTabExpectedNext flag distinguishes a legal-tab
                    // open from a cheats-tab open.
                    else if (cls == STR("WBP_LegalTab_C"))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        s_instance->injectCheatsTabContent(context);
                    }
                    // EditMappingTab fires on first Settings open AND when the
                    // user clicks into the keymap navbar tab — inject rows in
                    // both cases.
                    else if (cls == STR("WBP_EditMappingTab_C"))
                    {
                        s_instance->onSettingsRelatedShown(context, fnStr2);
                        s_instance->injectModKeybindRows(context);
                    }
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


                // Mod Game Options button clicks — catch both BP-level
                // OnMenuButtonClicked AND the lower BndEvt OnButtonReleasedEvent
                // on FrontEndButton.
                {
                    bool isMenuClick = wcscmp(fnStr2, STR("OnMenuButtonClicked")) == 0;
                    bool isFEReleased = (wcsstr(fnStr2, STR("OnButtonReleasedEvent")) != nullptr);
                    if (isMenuClick || isFEReleased)
                    {
                        if (s_verbose) {
                            static int s_clickDiagCount = 0;
                            if (s_clickDiagCount < 24) {
                                VLOG(STR("[CP4-CLICK] fn='{}' ctxCls='{}' ctx={:p}\n"),
                                     fnStr2,
                                     context ? safeClassName(context).c_str() : L"null",
                                     (void*)context);
                                ++s_clickDiagCount;
                            }
                        }
                        s_instance->onModGameOptionClicked(context);
                        s_instance->onCheatsTabButtonClicked(context);
                        if (isMenuClick) return;
                    }
                }

                if (wcscmp(fnStr2, STR("GetNavBarTabs")) == 0)
                {
                    s_instance->onGetNavBarTabsPost(context, func, parms);
                    return;
                }

                // Capture in-game keymap rebinds. OnKeySelectedBP fires on a
                // WBP_SettingsKeySelector_C after the user picks a new chord;
                // persist to MoriaCppMod.ini if the selector is one of ours.
                if (wcscmp(fnStr2, STR("OnKeySelectedBP")) == 0)
                {
                    std::wstring cls = safeClassName(context);
                    VLOG(STR("[SettingsUI] OnKeySelectedBP fired on cls='{}' obj={:p}\n"),
                         cls.c_str(), (void*)context);
                    if (cls.find(STR("KeySelector")) != std::wstring::npos)
                    {
                        // first try cheats dispatch. Cheat rows are
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
                    std::wstring cls = safeClassName(context);

                    // Clear settings-screen-open gate when SettingsScreen
                    // hides so mod input handlers resume normal operation.
                    if (cls == STR("WBP_SettingsScreen_C"))
                        s_instance->onNativeSettingsScreenHidden();

                    if (cls == STR("UI_WBP_BuildHUDv2_C"))
                    {
                        // v7.1.x: see corresponding OnAfterShow above.
                        s_instance->m_buildHudShowing = false;
                    }
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
                        VLOG(STR("[MoriaCppMod] [HUD] Entered FreeCam - hiding toolbars\n"));
                        s_overlay.visible = false;
                        s_overlay.needsUpdate = true;
                        // v6.10.0+ New Building Bar — same hide-on-
                        // freecam behavior the legacy MC toolbar had.
                        // SetVisibility(1) = ESlateVisibility::Collapsed
                        // (no layout space, no input).
                        if (s_instance->m_newBuildingBar &&
                            isObjectAlive(s_instance->m_newBuildingBar))
                        {
                            auto* visFn = s_instance->m_newBuildingBar
                                ->GetFunctionByNameInChain(STR("SetVisibility"));
                            if (visFn) {
                                uint8_t p[8]{}; p[0] = 1;
                                safeProcessEvent(s_instance->m_newBuildingBar, visFn, p);
                            }
                        }
                    }
                    return;
                }

                // FreeCam exit: restore all mod toolbars
                if (wcscmp(fnStr2, STR("OnCustomDisableCamera")) == 0)
                {
                    s_instance->m_inFreeCam = false;
                    s_instance->m_gameHudVisible = true;
                    VLOG(STR("[MoriaCppMod] [HUD] Exited FreeCam - restoring toolbars\n"));
                    if (s_instance->m_toolbarsVisible)
                    {
                        if (s_instance->m_showHotbar) { s_overlay.visible = true; s_overlay.needsUpdate = true; }
                    }
                    // Restore NBB visibility. SetVisibility(0) =
                    // ESlateVisibility::Visible.
                    if (s_instance->m_newBuildingBar &&
                        isObjectAlive(s_instance->m_newBuildingBar))
                    {
                        auto* visFn = s_instance->m_newBuildingBar
                            ->GetFunctionByNameInChain(STR("SetVisibility"));
                        if (visFn) {
                            uint8_t p[8]{}; p[0] = 0;
                            safeProcessEvent(s_instance->m_newBuildingBar, visFn, p);
                        }
                    }
                    return;
                }


                // Bubble change - fired by AWorldLayout's OnPlayerEnteredBubble delegate
                if (wcscmp(fnStr2, STR("OnPlayerEnteredBubble")) == 0)
                {
                    if (parms)
                    {
                        if (auto* pBubble = func->GetPropertyByNameInChain(STR("Bubble")))
                        {
                            auto* bubble = *reinterpret_cast<UObject**>(
                                static_cast<uint8_t*>(parms) + pBubble->GetOffset_Internal());
                            if (bubble && isObjectAlive(bubble))
                                s_instance->onBubbleEnteredEvent(bubble);
                        }
                    }
                    return;
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
                // Note: isLocalContext removed here - Build_Tab is a client-side UMG widget
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


                if (s_off_bLock == -2)
                {
                    int bLockSize = 0;
                    resolveOffsetAndSize(selfRef, L"bLock", s_off_bLock, bLockSize);
                    if (bLockSize > 0 && bLockSize != BLOCK_DATA_SIZE)
                    {
                        VLOG(STR("[MoriaCppMod] WARNING: bLock property size {} != BLOCK_DATA_SIZE {} - struct layout may have changed!\n"),
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
            VLOG(STR("[MoriaCppMod] {}: F1-F8=build | F9=rotate | '='=toggle Advanced Builder | '-'=unstuck NPCs | Reveal map in pause menu | Mod keybinds in Settings → keymap tab\n"),
                 ModVersion);


            // Register game thread tick - fires once per frame ON the game thread
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
                    // [v7.1.0-rc.51 CRASH FIX 2026-05-11] Clear stale widget
                    // pointers + character-load state on map transition.
                    // Without this, tickRotationDisplay can hit a stale
                    // m_rotDisplayWidget (slot reused with class ptr
                    // 0xFF...FF) during the world-tear-down window before
                    // our pawn-lost detection fires.
                    m_rotDisplayWidget = nullptr;
                    m_characterLoaded = false;
                    m_localPC = nullptr;
                    m_localPawn = nullptr;
                    // [rc.46] Reset probe-fired flags on every LoadMap.
                    // The polling-based "Character lost" path at line 3979
                    // misses save→main-menu→load-save cycles because our
                    // tick doesn't run during main menu. LoadMap fires
                    // reliably on every world transition so it's the right
                    // place to invalidate stale probe state.
                    m_probeNFired = false;
                    m_probePFired = false;
                    m_probeQFired = false;  // [rc.47]
                    m_probeRFired = false;  // [rc.48]
                    m_probeSFired = false;  // [rc.49]
                    m_probeTFired = false;  // [rc.50]
                    m_probeUFired = false;  // [rc.53]
                    m_probeVFired = false;  // [rc.54]
                    m_probeWFired = false;  // [rc.55]
                    m_probeXFired = false;  // [rc.56]
                    m_probeYFired = false;  // [rc.57]
                    m_autoRestoreFired = false;  // [rc.59]

                    if (!m_definitionsApplied)
                    {
                        m_definitionsApplied = true;
                        try { loadAndApplyDefinitions(); }
                        catch (...) { RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] Exception during definition loading\n")); }
                    }
                    return {false, false};
                });
        }


        // Game thread tick - called once per frame ON the game thread via EngineTick hook.
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
                // 'justCreated' bool removed (no setters left after
                // MC + AB create blocks were deleted; visibility-restore block
                // below is also gone).
                // UMG QuickBuild bar (m_umgBarWidget) RE-ENABLED.
                // The from-scratch top-of-screen New Building Bar in
                // v6.10.0 never reliably displayed icons or F-key labels,
                // and several iterations of fix attempts (v6.19→v6.21-wip)
                // failed to converge. Restoring the OLD UMG bar (which
                // had icons + labels working since v2.x) lets the user
                // play again while we iterate on the New Building Bar
                // separately.
                //
                // MC bar (m_mcBarWidget) and Advanced Builder (m_abBarWidget)
                // remain disabled - their dispatch is restored separately
                // via the gameThreadTick MC polling block (planned fix #1).
                // MC bar destroy removed (bar gone)
                // AB bar destroy removed (bar gone)
                // OLD UMG bar code removed entirely (batch 2).
                // NBB at top of screen is the only toolbar. m_umgBarWidget
                // is also being removed (batch 3) so this safety call is dead.

                // MC bar create block removed (was gated 'if(false)';
                // Run handle-resolve priming on every character load, independent
                // of any toolbar widget. F1-F8 USE + chord SET polling all gate
                // on HandleResolvePhase::Done.
                // [rc.139] Handle-resolve priming only runs once the Advanced
                // Builder subsystem is active — it briefly opens build mode,
                // which shouldn't happen while the builder is switched off.
                // Phase stays None until activation, so toggling on later
                // primes at that moment.
                if (m_advBuilderActive && m_characterLoaded && m_handleResolvePhase == HandleResolvePhase::None)
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


            } // end if (!m_isDedicatedServer) - skip UI on headless server



            if (s_pendingKeyLabelRefresh.exchange(false))
            {
                refreshKeyLabels();
                if (m_ftVisible) updateFontTestKeyLabels();
            }


            if (m_pendingCharNameReady.exchange(false))
                applyPendingCharacterName();

            tickDeferredWidgetRemovals();

            // In-game rename modal owns RETURN/ESCAPE/click handling via its
            // own BP; double-handling here would fire confirm twice.
            if (m_ftRenameVisible && !m_ftRenameUsingModal)
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


            if (m_crosshairShowTick > 0 && (GetTickCount64() - m_crosshairShowTick) >= CROSSHAIR_FADE_MS)
                hideCrosshair();


            if (m_ebShowTick > 0 && (GetTickCount64() - m_ebShowTick) >= ERROR_BOX_DURATION_MS)
                hideErrorBox();


            if (m_auditClearTime > 0 && GetTickCount64() >= m_auditClearTime)
                clearStabilityHighlights();


            // F12 (BIND_CONFIG) dispatcher disabled — the legacy config menu
            // is replaced by Settings → Gameplay injection, and F12 is now
            // the default Save Game keybind (dispatched below).
            // toggleFontTestPanel() retained for ad-hoc testing; no live
            // keypress reaches it.
            #if 0
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
            #endif

            // Save Game keybind dispatcher (BIND_SAVE_GAME). Edge-triggered;
            // suppressed while Settings UI or rename popup is open so a stray
            // keypress in those contexts doesn't trigger a save.
            {
                static bool s_lastSaveKey = false;
                uint8_t svVk = s_bindings[BIND_SAVE_GAME].key;
                if (svVk != 0 && s_bindings[BIND_SAVE_GAME].enabled
                    && m_characterLoaded && !m_ftVisible && !m_ftRenameVisible
                    && !isSettingsScreenOpen())
                {
                    bool nowDown = (GetAsyncKeyState(svVk) & 0x8000) != 0;
                    if (nowDown && !s_lastSaveKey)
                    {
                        VLOG(STR("[MoriaCppMod] [SaveGame] Keybind pressed (VK=0x{:02X})\n"), svVk);
                        triggerSaveGame();
                    }
                    s_lastSaveKey = nowDown;
                }
                else
                {
                    s_lastSaveKey = false;
                }
            }

            // [rc.139] Unstuck NPCs keybind (BIND_UNSTUCK_NPCS, default Num-).
            // Replaces the automatic day-cycle/character-load CantReach scan:
            // one press = one full scan + teleport pass, no background loop.
            {
                static bool s_lastUnstuckKey = false;
                uint8_t unVk = s_bindings[BIND_UNSTUCK_NPCS].key;
                // single-key action: skip while a modifier is held (default
                // is main-keyboard '-', so Shift+'-' = '_' must not fire).
                const bool unMod =
                    ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_MENU)    & 0x8000) != 0);
                if (unVk != 0 && s_bindings[BIND_UNSTUCK_NPCS].enabled
                    && m_characterLoaded && !m_ftVisible && !m_ftRenameVisible
                    && !isSettingsScreenOpen() && !unMod)
                {
                    bool nowDown = (GetAsyncKeyState(unVk) & 0x8000) != 0;
                    if (nowDown && !s_lastUnstuckKey)
                    {
                        VLOG(STR("[MoriaCppMod] [UnstuckNpcs] Keybind pressed (VK=0x{:02X})\n"), unVk);
                        runUnstuckNpcsNow();
                    }
                    s_lastUnstuckKey = nowDown;
                }
                else
                {
                    s_lastUnstuckKey = false;
                }
            }

            // [rc.139] Toggle Advanced Builder keybind (BIND_AB_TOGGLE,
            // default '='). Master enable/disable for the builder subsystem;
            // starts OFF every session. When OFF: NBB bar hidden, F1-F8 quick
            // build + MC keys inert, handle-resolve priming skipped.
            {
                static bool s_lastAbToggleKey = false;
                uint8_t abVk = s_bindings[BIND_AB_TOGGLE].key;
                // single-key action (default main-keyboard '='): skip while
                // any modifier is held so combos aimed elsewhere don't toggle.
                const bool abMod =
                    ((GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_MENU)    & 0x8000) != 0);
                if (abVk != 0 && s_bindings[BIND_AB_TOGGLE].enabled
                    && m_characterLoaded && !m_ftVisible && !m_ftRenameVisible
                    && !isSettingsScreenOpen() && !abMod)
                {
                    bool nowDown = (GetAsyncKeyState(abVk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbToggleKey)
                    {
                        m_advBuilderActive = !m_advBuilderActive;
                        VLOG(STR("[MoriaCppMod] [AdvBuilder] toggled -> {}\n"),
                             m_advBuilderActive ? STR("ACTIVE") : STR("INACTIVE"));
                        if (m_advBuilderActive)
                        {
                            // First activation this session: create the bar.
                            if (!m_newBuildingBarSpawnAttempted)
                            {
                                m_newBuildingBarSpawnAttempted = true;
                                createNewBuildingBar();
                            }
                            else if (m_newBuildingBar && isObjectAlive(m_newBuildingBar))
                            {
                                if (auto* visFn = m_newBuildingBar->GetFunctionByNameInChain(STR("SetVisibility")))
                                { uint8_t p[8]{}; p[0] = 0; safeProcessEvent(m_newBuildingBar, visFn, p); }
                            }
                            updateBuildersBar();
                            showInfoMessage(L"Advanced Builder: ON");
                        }
                        else
                        {
                            if (m_newBuildingBar && isObjectAlive(m_newBuildingBar))
                            {
                                if (auto* visFn = m_newBuildingBar->GetFunctionByNameInChain(STR("SetVisibility")))
                                { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(m_newBuildingBar, visFn, p); }
                            }
                            showInfoMessage(L"Advanced Builder: OFF");
                        }
                    }
                    s_lastAbToggleKey = nowDown;
                }
                else
                {
                    s_lastAbToggleKey = false;
                }
            }

            // [Phase 4 / Path B' 2026-05-14]
            // Vanilla proximity widget shows "[E] Details" on goat (pak
            // ships bDetailsInteractionEnabled=true). E-press on that row
            // fires UI_WBP_Interaction_C::OnPressInteract → our PE-pre hook
            // (onInteractionPressPre) sets m_goatModalPending = true. This
            // tick handler picks up the flag and opens our centered modal.
            // Path A row-injection + cursor cycling are confirmed dead-ends
            // (vanilla's nav iterates NPC interaction structs, not the
            // widget tree); kept dormant for archaeology.
            // [rc.95 BELL-ONLY 2026-07-10] goat menu ticks disabled to observe
            // Tobi's native workflow. [rc.107] saddlebag widget tick RE-ENABLED:
            // Esc/Tab close + 4Hz storage view-state re-assert for our chest UI.
            // s_instance->tickGoatModalDeferred();
            // s_instance->tickGoatSubmenu();
            s_instance->tickGoatSaddlebagWidget();
            // s_instance->tickGoatMenuInject();  // no-op (gated internally)
            s_instance->tickPathXDelayedProbe();   // [rc.24] +1s NpcInfo.Items.Num snapshot after RegisterWithNPCManager
            // s_instance->tickGoatMenuProbe();  // disabled — no Path A probe needed

            // Reposition HUD keybind dispatcher (default F10). First press
            // shows the inspect window + rotation display draggable; second
            // press OR ESC exits and returns visibility to normal rules.
            {
                static bool s_lastReposKey = false;
                static bool s_lastReposEsc = false;
                uint8_t reposVk = s_bindings[BIND_REPOSITION_HUD].key;
                bool gateOK = m_characterLoaded && !m_ftVisible && !m_ftRenameVisible
                              && !isSettingsScreenOpen();
                if (reposVk != 0 && s_bindings[BIND_REPOSITION_HUD].enabled && gateOK)
                {
                    bool nowDown = (GetAsyncKeyState(reposVk) & 0x8000) != 0;
                    if (nowDown && !s_lastReposKey)
                    {
                        m_repositionHudMode = !m_repositionHudMode;
                        VLOG(STR("[MoriaCppMod] [ReposHUD] toggled -> {}\n"),
                             m_repositionHudMode ? STR("ON") : STR("OFF"));
                        if (m_repositionHudMode)
                        {
                            // Force inspect window visible so it can be dragged.
                            if (!m_targetInfoWidget) createTargetInfoWidget();
                            if (m_targetInfoWidget && isObjectAlive(m_targetInfoWidget))
                            {
                                showTargetInfoUMG(L"Reposition", L"Drag me", L"", L"", false, L"", L"");
                                m_tiAutoHideAtMs = 0;  // disable auto-hide while in mode
                            }
                            // Switch to UI input mode so the mouse cursor
                            // appears and clicks reach the widgets.
                            setInputModeUI(m_targetInfoWidget ? m_targetInfoWidget : nullptr);
                        }
                        else
                        {
                            // Exiting: hide the inspect chrome; the rotation
                            // display returns to its conditional rule
                            // (only visible while building/ghost active).
                            hideTargetInfo();
                            setInputModeGame();
                        }
                    }
                    s_lastReposKey = nowDown;
                }
                else
                {
                    s_lastReposKey = false;
                }
                // ESC also exits the mode (alongside the F10 toggle).
                if (m_repositionHudMode)
                {
                    bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                    if (escDown && !s_lastReposEsc)
                    {
                        m_repositionHudMode = false;
                        hideTargetInfo();
                        setInputModeGame();
                        VLOG(STR("[MoriaCppMod] [ReposHUD] ESC pressed - exiting mode\n"));
                    }
                    s_lastReposEsc = escDown;
                }
                else
                {
                    s_lastReposEsc = false;
                }
            }

            // [rc.22] ESC dismisses the Test 3 saddlebag widget. Spawned in
            // openGoatSaddlebagInventory_Test3, cached on m_test3SaddlebagWidget.
            // Drives RemoveFromParent + clears the cache so a subsequent
            // saddlebag click can spawn a fresh widget.
            if (m_test3SaddlebagWidget)
            {
                static bool s_lastT3Esc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastT3Esc)
                {
                    if (isObjectAlive(m_test3SaddlebagWidget))
                    {
                        if (auto* rmFn = m_test3SaddlebagWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
                        {
                            try { safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [Test 3] ESC pressed — RemoveFromParent fired on widget {:p}\n"),
                                 (void*)m_test3SaddlebagWidget);
                        }
                    }
                    m_test3SaddlebagWidget = nullptr;
                }
                s_lastT3Esc = escDown;
            }


            // MC keybind polling: dispatch independent of any toolbar widget.
            // Suppressed while Settings UI is open so rebind keystrokes don't
            // double as gameplay actions.
            // [rc.139] Also gated on the Advanced Builder master switch.
            if (m_advBuilderActive && m_characterLoaded && !m_ftVisible
                && !isSettingsScreenOpen())
            {
                static bool s_lastMcKey[MC_SLOTS]{};

                const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (i == 5) { s_lastMcKey[i] = false; continue; }
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_lastMcKey[i] = false; continue; }
                    if (!s_bindings[MC_BIND_BASE + i].enabled) { s_lastMcKey[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;

                    if (!nowDown && shiftHeld)
                    {
                        uint8_t alt = numpadShiftAlternate(vk);
                        if (alt) nowDown = (GetAsyncKeyState(alt) & 0x8000) != 0;
                    }
                    if (nowDown && !s_lastMcKey[i])
                    {
                        VLOG(
                            STR("[MoriaCppMod] [MC] Slot {} pressed (VK=0x{:02X})\n"), i, vk);
                        dispatchMcSlot(i);
                    }
                    s_lastMcKey[i] = nowDown;
                }
            }






            // skip single-key action binds (trash/replenish/remove-attrs) when ANY modifier
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
            // v7.1.x: NUM* polled handler retired — reveal-map moved to
            // the pause-menu (ESC) button stack alongside UNLOCK
            // RECIPES / READ ALL LORE / CLEAR ALL BUFFS. NUM* is now
            // free again. See moria_settings_ui.inl for the new entry
            // (GameOptKind::RevealMap).
            // [rc.16 2026-05-25] NUM- → grant Bell-of-the-Goat to player
            // inventory. Same call as NUM7 (which remains for backward
            // compat). User requested the binding on NUM- since it was
            // free after the rc.51 OnNpcRescued recruit-chain test was
            // suspended and rendered redundant by Tobi's v1.2.10 dispatcher
            // patch. Bell asset path:
            //   /Game/Mods/PorterGoat/Items/BP_PorterGoatBell.BP_PorterGoatBell_C
            // grantBellToPlayer() lives in moria_goat.inl.
            // [rc.139] NUM- dev grant RETIRED — Num- is now the default for
            // the user-facing "Unstuck NPCs" bind (BIND_UNSTUCK_NPCS).
            // NUM7 still grants the bell for goat testing.
            // {
            //     static bool s_lastBellMinusKey = false;
            //     bool nowDown = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;
            //     if (nowDown && !s_lastBellMinusKey && !m_ftVisible && !modDown)
            //     {
            //         VLOG(STR("[MoriaCppMod] [Bell] NUM- press — grant Bell-of-the-Goat\n"));
            //         s_instance->grantBellToPlayer();
            //     }
            //     s_lastBellMinusKey = nowDown;
            // }
            // [v7.1.0-rc.51 SUMMON 2026-05-10] NUM+ → teleport the live
            // goat (BP_NpcGoat_C) to the player's location. Hook target
            // discovered via rc.34 reflection: MorCharacter::ServerTeleportTo
            // (FVector DestLocation, FRotator DestRotation). Goals 1+3 of
            // porter-goat-feature-goals.md — summon makes the goat appear
            // at the player; the goat's existing porter BT
            // (Bst_NPCGoatWorkPorter) handles follow once it's nearby.
#if 0 // [v8.2.x] ALL hardcoded NUM/N dev keys disabled per user directive — no polling, remove entirely later
            {
                static bool s_lastSummonKey = false;
                bool nowDown = (GetAsyncKeyState(VK_ADD) & 0x8000) != 0;
                if (nowDown && !s_lastSummonKey && !m_ftVisible && !modDown)
                {
                    // [rc.95 BELL-ONLY] disabled — observe Tobi's native summon/follow
                    // s_instance->summonGoatToPlayer();
                }
                s_lastSummonKey = nowDown;
            }
#endif
            // [rc.86 TEST 2026-07-06] NUM* (multiply) → open goat storage via
            // the native MorNpcOnManageLocalInteraction handler. Reliable test
            // trigger independent of the E-menu state.
#if 0 // [v8.2.x] disabled per user directive
            {
                static bool s_lastGoatStoreKey = false;
                bool nowDown = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0;
                if (nowDown && !s_lastGoatStoreKey && !m_ftVisible && !modDown)
                {
                    // [rc.95 BELL-ONLY] disabled — use Tobi's native Manage/craft flow
                    // s_instance->openGoatSaddlebagInventory();
                }
                s_lastGoatStoreKey = nowDown;
            }
#endif
            // [v7.1.0-rc.51 BELL/SADDLEBAGS GRANT DEBUG 2026-05-12]
            //   NUM7 → grant Bell-of-the-Goat (BP_PorterGoatBell_C)
            //   NUM8 → grant Saddlebags (BP_PorterGoatSaddlebags_C)
            // Moved off NUM2/NUM3 — those are bound to Remove Single / Undo
            // Remove / Remove All in Settings → Gameplay tab.
            //
            // Both paths from PorterGoatBell_v1.1.1 pak. Bell triggers
            // summon (rc.40 will hook right-click on it). Saddlebags is
            // the 8x8 hidden storage anchor. Production acquisition is
            // via Shire merchant recipe-bundle purchase; these debug
            // keybinds skip the campaign progression for fast iteration.
#if 0 // [v8.2.x] NUM7 bell grant disabled per user directive (bell must be crafted/acquired in-game)
            {
                static bool s_lastBellGrantKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD7) & 0x8000) != 0;
                if (nowDown && !s_lastBellGrantKey && !m_ftVisible && !modDown)
                {
                    s_instance->grantBellToPlayer();
                }
                s_lastBellGrantKey = nowDown;
            }
#endif
#if 0 // [v8.2.x] NUM9 StorageCap+census disabled per user directive
            // [rc.120] NUM9 → arm/re-arm the 30s StorageCap capture window.
            {
                static bool s_lastStorageCapKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) != 0;
                if (nowDown && !s_lastStorageCapKey && !m_ftVisible && !modDown)
                {
                    m_storageCapUntilMs = GetTickCount64() + 30000;
                    m_storageCapSeen.clear();
                    m_storageCapLines = 0;
                    VLOG(STR("[MoriaCppMod] [StorageCap rc.120] === window ARMED for 30s — craft/place/open/save now ===\n"));
                    // [rc.125 WORLD CENSUS 2026-07-11] Count live container-ish
                    // actors — evidence for the dwarf-NPC container-actor model
                    // (invisible AMorContainerItem instances backing NPC 4×3s).
                    for (const wchar_t* cls : {
                        STR("MorDroppedItem"), STR("BP_DropItem_C"),
                        STR("MorContainerItem"),
                        STR("BP_ContainerItem_Dwarf_BodyInventoryNPC_C"),
                        STR("BP_ContainerItem_Dwarf_BodyInventory_C"),
                        STR("BP_ContainerItem_Goat_Slot_EpicPack_C"),
                        STR("BP_SaddleBags_Goat_C"),
                        STR("BP_StorageChest_Construction_C"),
                        STR("BP_NpcDwarf_C"), STR("BP_NpcGoat_C") })
                    {
                        std::vector<UObject*> hits;
                        findAllOfSafe(cls, hits);
                        int live = 0;
                        for (auto* h : hits)
                        {
                            if (!h || !isObjectAlive(h)) continue;
                            std::wstring nm = safeObjectName(h);
                            if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                            live++;
                        }
                        VLOG(STR("[MoriaCppMod] [Census rc.125] {} live={}\n"), cls, live);
                    }
                    showOnScreen(L"StorageCap ARMED 30s + census logged", 3.0f, 0.4f, 0.9f, 0.9f);
                }
                s_lastStorageCapKey = nowDown;
            }
#endif
#if 0 // [v8.2.x] NUM8 B5 probe disabled per user directive
            {
                static bool s_lastSaddlebagsGrantKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) != 0;
                if (nowDown && !s_lastSaddlebagsGrantKey && !m_ftVisible && !modDown)
                {
                    // [rc.132] NUM8 repurposed: B5 probe — drop crafted pack via
                    // ServerDropItem + deferred wrapper/raw scans.
                    s_instance->probeB5DropCraftedPack();
                }
                s_lastSaddlebagsGrantKey = nowDown;
            }
#endif
            // [rc.36 DEBUG SADDLEBAG TRIGGER 2026-06-06] NUM4 fires
            // openGoatSaddlebagInventory directly on the nearest goat,
            // bypassing the missing menu entry. v1.8.0 SAFE pak's menu
            // cleanup over-fired and removed Saddlebags+Rename rows. With
            // RegisterWithNPCManager finally producing delta=+1 (first
            // ever in 35+ iterations), this lets us test if the engine
            // UI now binds correctly to the registered goat.
#if 0 // [v8.2.x] NUM4 disabled per user directive
            {
                static bool s_lastSaddleTrigKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) != 0;
                if (nowDown && !s_lastSaddleTrigKey && !m_ftVisible && !modDown)
                {
                    // [rc.95 BELL-ONLY] disabled — use Tobi's native Manage/craft flow
                    // VLOG(STR("[MoriaCppMod] [SaddleTrigger] NUM4 press — bypassing menu, firing openGoatSaddlebag directly\n"));
                    // s_instance->openGoatSaddlebagInventory();
                    // showOnScreen(L"NUM4: openGoatSaddlebag fired (menu bypass)", 2.0f, 0.4f, 0.9f, 0.4f);
                }
                s_lastSaddleTrigKey = nowDown;
            }
#endif
            // [rc.25 MANAGE CAPTURE 2026-05-26] NUM0 arms a 5-second PE-pre
            // capture window. While active, every UFunction call on contexts
            // matching dwarf/NPC/PC/UIManager-related classes logs to
            // [ManageCapture]. Goal: diff vanilla dwarf-Manage flow against
            // goat-Saddlebags flow to find the missing entry-point function.
            // [rc.29 2026-05-27] ManageCapture moved from NUM0 → NUM9.
            // NUM0 is bound to BubbleInfo (~line 2917). NUM9 was freed by
            // rc.40a (test keybind removed per user spec).
#if 0 // [v8.2.x] NUM9 ManageCapture disabled per user directive (was also double-bound with StorageCap)
            {
                static bool s_lastMngCapKey = false;
                bool nowDown = (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) != 0;
                if (nowDown && !s_lastMngCapKey && !m_ftVisible && !modDown)
                {
                    m_mngCapEndMs = GetTickCount64() + 10000;
                    VLOG(STR("[MoriaCppMod] [ManageCapture] === window armed (10s) — interact with NPC NOW ===\n"));
                    showOnScreen(L"ManageCapture armed 10s (NUM9) — interact NPC now", 3.0f, 0.4f, 0.9f, 0.4f);
                }
                s_lastMngCapKey = nowDown;
            }
#endif
            // [rc.40a 2026-05-12] NUM9 test keybind removed per user spec.
            // Bell toggle is right-click-only now. Discovery happens via the
            // [BellHook] PE-pre logging on any UFunction called with the
            // bell as context (see PE-pre handler ~line 880).
            // Num. (Decimal) — toggle diag mode. When ON, every PE-pre call
            // matching wide rescue-related keywords logs WITHOUT dedup.
            // When OFF (default), back to the narrow Interact/Rescue
            // filter with one-shot dedup. Use this to capture full
            // dwarf-rescue chain in a fresh game.
#if 0 // [v8.2.x] NUM. diag toggle disabled per user directive
            {
                static bool s_lastDiagKey = false;
                bool nowDown = (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
                if (nowDown && !s_lastDiagKey && !m_ftVisible && !modDown)
                {
                    s_diagMode = !s_diagMode;
                    VLOG(STR("[MoriaCppMod] [DiagMode] toggled {} (rescue-chain capture)\n"),
                         s_diagMode ? STR("ON") : STR("OFF"));
                    showOnScreen(s_diagMode ? L"Rescue diag: ON" : L"Rescue diag: OFF",
                                 2.0f, 0.4f, 0.9f, 0.4f);
                }
                s_lastDiagKey = nowDown;
            }
#endif
            // Num/ (Divide) — probe: dump BP_StoryManager.OnNpcRescued
            // multicast subscriber list. Per desktop's BP-graph decode,
            // each rescued NPC self-subscribes in BeginPlay; the goat
            // doesn't, so the multicast bypasses it. Dumping the list
            // before/after a real dwarf rescue confirms or refutes.
#if 0 // [v8.2.x] NUM/ probe disabled per user directive
            {
                static bool s_lastProbeKey = false;
                bool nowDown = (GetAsyncKeyState(VK_DIVIDE) & 0x8000) != 0;
                if (nowDown && !s_lastProbeKey && !m_ftVisible && !modDown)
                {
                    VLOG(STR("[MoriaCppMod] [Probe] NUM/ press detected — dumping rescue subscribers\n"));
                    dumpStoryMgrRescueSubscribers();
                    showOnScreen(L"Rescue probe: dumped subscribers",
                                 2.0f, 0.4f, 0.9f, 0.4f);
                }
                s_lastProbeKey = nowDown;
            }
#endif
            // Goat follow tick (no-op when herd is empty; throttled to 1 Hz/goat internally).
            // [rc.101] RE-ENABLED — user: stay/follow must work (rc.95 strip broke it;
            // Tobi has no native follow drive, ours is the only one).
            tickFollowGoats();
            // [rc.101] deferred live StorageMode UI deep-dump (no-op unless scheduled)
            tickPendingStorageHarvest();
            // [rc.132] B5 probe deferred scans (no-op unless armed via NUM8)
            tickB5ProbeScan();
            // [rc.133] B7 post-save pack return (no-op unless a stash is pending)
            tickB7MoveBack();
            // Goat menu click dispatch (no-op unless menu visible).
            tickGoatMenu();
            // Num0 - capture bubble info to clipboard + Target Info widget (v6.4.5+)
            // Windows maps numpad-0 to VK_NUMPAD0 only when NumLock is ON; with NumLock OFF
            // the same physical key sends VK_INSERT (which Replenish owns). We always
            // check VK_NUMPAD0 and also fall back to a fresh key-state via GetKeyState
            // to survive focus-related async state drops.
#if 0 // [v8.2.x] NUM0 bubble-info capture disabled per user directive
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
#endif
#if 0 // [v8.2.x] N widget-harvest key disabled per user directive
            // N - debug widget harvest (dev-only). Constructs the 12 Join World
            // widget classes off-viewport, walks each WidgetTree, dumps to JSON
            // under Mods/MoriaCppMod/widget-harvest/. Plain N (no modifier).
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
#endif
            // Pitch rotation (. / SHIFT+.) - BIND_PITCH_ROTATE defaults to '.'
            // gated to ghost-visible (resolveGATA returns non-null).
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
            // Roll rotation (, / SHIFT+,) - BIND_ROLL_ROTATE defaults to ','
            // gated to ghost-visible.
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


            if (m_toolbarsVisible && !m_ftVisible && m_gameHudVisible)
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
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
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
                                                if (m_ftCheckImages[b])
                                                {
                                                    auto* visFn = m_ftCheckImages[b]->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = s_bindings[b].enabled ? 0 : 2; safeProcessEvent(m_ftCheckImages[b], visFn, vp); }
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
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
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
                                showRenameDialog_v2();
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
                            // Use full width for icon detection - icons are at the left of the content area
                            // Icon is 56px wide with 4px left padding - click area covers just the icon
                            int iconX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f) * s2p);
                            int iconX1 = static_cast<int>(iconX0 + 64.0f * s2p);

                            int entryStart = static_cast<int>(wTop + 40.0f * s2p + 30.0f * s2p);
                            int entryH = static_cast<int>(72.0f * s2p);

                            VLOG(STR("[MoriaCppMod] [Env] Click: cur=({},{}) iconX=[{},{}] entryStart={} count={}\n"),
                                 curX, curY, iconX0, iconX1, entryStart, s_config.removalCount.load());

                            float scrollOff = 0.0f;
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
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
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
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
                                        if (m_ftGameModCheckImages[idx])
                                        {
                                            auto* visFn = m_ftGameModCheckImages[idx]->GetFunctionByNameInChain(STR("SetVisibility"));
                                            if (visFn) { uint8_t vp[8]{}; vp[0] = m_ftGameModEntries[idx].enabled ? 0 : 2; safeProcessEvent(m_ftGameModCheckImages[idx], visFn, vp); }
                                        }

                                        saveGameMods(m_ftGameModEntries);
                                        VLOG(STR("[MoriaCppMod] [Settings] Game Mod '{}' = {}\n"),
                                            std::wstring(m_ftGameModEntries[idx].name.begin(), m_ftGameModEntries[idx].name.end()),
                                            m_ftGameModEntries[idx].enabled ? 1 : 0);
                                    }
                                }
                            }
                        }


                        // Cheats tab (index 4): existing 3 rows (Unlock/Read/Peace) at
                        // contentY + 0/128/256, plus the buff entries table beneath starting at
                        // contentY + 384. All entries at 1x-scale Y offsets are in m_buffRowTopYs +
                        // m_buffRowHeights; multiply by s2p and add scrollOff.
                        if (m_ftSelectedTab == 4)
                        {
                            // Read current scroll offset (scrollbox moves tab content upward)
                            float scrollOff = 0.0f;
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }

                            int btnLeft  = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f - 400.0f) * s2p);
                            int btnRight = static_cast<int>(wLeft + (1540.0f - 30.0f - 50.0f) * s2p);

                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int scrollPx = static_cast<int>(scrollOff * s2p);

                            // Existing 3 rows (unscrolled at tab top - tab content follows scroll like a VBox)
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

                            if (curX >= btnLeft && curX <= btnRight)
                            {
                                if (curY >= unlockT && curY <= unlockB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] UNLOCK clicked\n")); unlockAllAvailableRecipes(); handled = true; }
                                else if (curY >= readT && curY <= readB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] READ clicked\n"));   markAllLoreRead(); handled = true; }
                                else if (curY >= peaceT && curY <= peaceB)
                                { VLOG(STR("[MoriaCppMod] [Cheats] PEACE/FIGHT clicked\n")); togglePeaceMode(); handled = true; }
                            }
                            if (!handled && curX >= cbX0 && curX <= cbX1 && curY >= peaceT && curY <= peaceB)
                            { VLOG(STR("[MoriaCppMod] [Cheats] Peace checkbox clicked\n")); togglePeaceMode(); handled = true; }

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

                        // Tweaks tab (index 5). Click on a row's value button cycles.
                        if (m_ftSelectedTab == 5)
                        {
                            // Read scroll offset (same pattern as tab 4)
                            float scrollOff = 0.0f;
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    safeProcessEvent(m_ftScrollBox, getScrollFn, sp.data());
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
                    // Per-slot cooldown - 200ms between slot resolutions to let animations settle
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
                // close the F12 panel via toggle (deferred removal)
                m_deferRemovalRebuild = 1;
                if (m_ftVisible) toggleFontTestPanel(); // closes it
            }
            else if (m_deferRemovalRebuild == 1)
            {
                // reopen - toggle creates fresh panel with updated data
                m_deferRemovalRebuild = 0;
                if (!m_ftVisible) toggleFontTestPanel(); // reopens it
            }


            placementTick();
            tickPitchRoll();
            drainUnlockQueue();   // process 50 recipe-discovery calls per frame (no-op when queue empty)
            refreshActiveBuffs(); // re-apply toggled-on buffs every 5s so they don't expire
            tickJoinWorldUI();    // consume pending show/hide flags for mod-owned Join World UI
            tickAdvancedJoinUI(); // consume pending show/hide flags for mod-owned Advanced Join Options UI
            tickSettingsUI();     // Settings screen take-over (mod keybinds in keymap tab)
            tickReapplyModifierPrefixes(); // keep "L-SHIFT + F1" text on SET rows alive
            tickCaptureSpecialKeys();      // capture DEL/INS/HOME/etc the BP rejects
            tickSaveAfterMarkRead();       // post-MarkAllAsRead lore persistence
            tickPendingCraftingMark();     // fire MarkAllAsRead when crafting opens
            tickTargetInfoDrag();          // inspect window drag + close + auto-hide
            tickRotationDisplay();         // rotation display 4-cell pyramid
            tickRenameFocus();             // re-assert focus on rename input
            tickNpcRecoveryProbe();        // PHASE 1 DIAG: NPC stuck-pathing probe (s_verbose only, one-shot)
            tickNpcRecovery();             // rc.46 EXPERIMENT: polling early-out via `if (true) return` — kept for code preservation, body inert
            // [rc.139] AUTOMATIC NPC scan RETIRED per user directive (mod
            // users reported stutter from the background scanning). The
            // CantReach teleport pass is now on-demand only: the "Unstuck
            // NPCs" keybind (BIND_UNSTUCK_NPCS, default Num-) fires
            // runUnstuckNpcsNow(). This also stops the 5s controller-cache
            // FindAllOf refresh and the per-tick time-period read.
            // tickDayCycleNpcScan();      // rc.47: day-cycle-triggered direct-read scan (Path 2 — zero PE in hot loop)
            tickGoatSaveProbes();          // v1.4.1-probe: Phase 2 goat-save research probes (gated by [GoatSaveProbes] Enabled=true)
            // [rc.112] adoption RE-ENABLED — a manager-restored goat (native respawn
            // via NPCGoat DT row on world load) must be adopted into m_followGoats
            // so follow/menu/saddlebag work on it.
            tickAdoptNativeGoat();
            // pollGoatMenuKey();             // rc.62: VK_E edge-detect → tryOpenGoatMenu (BP menu suppression made onInteractionPressPre path inert)

            // Quick Build chord-aware dispatch.
            //   USE (s_bindings[i].key, no modifiers): fires quickBuildSlot
            //   SET (s_setBindings[i].vk + modBits):   fires assignRecipeSlot
            // Default F1..F8 USE still goes through register_keydown_event for
            // low-latency; this polling only handles user rebinds off the F-keys.
            // [rc.139] Gated on the Advanced Builder master switch.
            if (m_advBuilderActive && !m_ftVisible && !isSettingsScreenOpen() &&
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
            tickSessionHistoryDeferredCapture();

            // We modify the native JoinWorld widget in place — Esc on it runs
            // the BP's own ClosePanel logic; we just clear our tracking
            // pointers when the widget is no longer alive (handled below).
            if (!m_replayActive) return;
            m_frameCounter++;

            // Server-fly sweep: must run BEFORE the m_characterLoaded gate below,
            // because on a dedicated server m_characterLoaded is never true (no local
            // pawn) and the code returns early. The sweep needs to run on dedi servers
            // and listen-server hosts to set client-auth movement flags on all dwarves.
            if ((m_characterLoaded || m_isDedicatedServer) && intervalElapsed(m_lastServerFlySweep, 2000))
            {
                std::vector<UObject*> dwarves;
                findAllOfSafe(STR("BP_FGKDwarf_C"), dwarves);
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
                    VLOG(STR("[MoriaCppMod] Character lost - world unloading, resetting replay state\n"));
                    m_characterLoaded = false;
                    m_characterHidden = false;
                    m_flyMode = false;
                    m_snapEnabled = true;
                    m_savedMaxSnapDistance = -1.0f;
                    m_buildMenuPrimed = false;
                    m_probeNFired = false;  // [rc.42] re-fire Probe N on next character-load
                    m_probePFired = false;  // [rc.44] re-fire Probe P on next character-load
                    m_probeQFired = false;  // [rc.47] re-fire Probe Q on next character-load
                    m_probeRFired = false;  // [rc.48] re-fire Probe R on next character-load
                    m_probeSFired = false;  // [rc.49] re-fire Probe S on next character-load
                    m_probeTFired = false;  // [rc.50] re-fire Probe T on next character-load
                    m_probeUFired = false;  // [rc.53] re-fire Probe U on next character-load
                    m_probeVFired = false;  // [rc.54] re-fire Probe V on next character-load
                    m_probeWFired = false;  // [rc.55] re-fire Probe W on next character-load
                    m_probeXFired = false;  // [rc.56] re-fire Probe X on next character-load
                    m_probeYFired = false;  // [rc.57] re-fire Probe Y on next character-load
                    m_autoRestoreFired = false;  // [rc.59] re-fire AutoRestore on next character-load
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

                    // Audit Iteration E: unbind cached UDataTable* pointers
                    // so a world-transition rebind catches a fresh DT
                    // instance instead of a stale-but-not-yet-GC'd one.
                    // Each DataTableUtil lazy-rebinds on first miss, so
                    // first DT use after the next character load takes a
                    // one-shot FindAllOf hit (acceptable cost).
                    m_dtConstructions.unbind();
                    m_dtConstructionRecipes.unbind();
                    m_dtItems.unbind();
                    m_dtWeapons.unbind();
                    m_dtTools.unbind();
                    m_dtArmor.unbind();
                    m_dtConsumables.unbind();
                    m_dtContainerItems.unbind();
                    m_dtOres.unbind();
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

                    m_activeBuilderSlot = -1;
                    m_fontTestWidget = nullptr;
                    m_ftVisible = false;
                    for (auto& t : m_ftTabImages) t = nullptr;
                    for (auto& t : m_ftTabLabels) t = nullptr;
                    m_ftTabActiveTexture = nullptr;
                    m_ftTabInactiveTexture = nullptr;
                    m_ftSelectedTab = 0;
                    m_ftScrollBox = nullptr;
                    for (auto& c : m_ftTabContent) c = nullptr;
                    for (auto& l : m_ftKeyBoxLabels) l = nullptr;
                    for (auto& c : m_ftCheckImages) c = nullptr;
                    m_ftModBoxLabel = nullptr;
                    m_ftControllerCheckImg = nullptr;
                    m_ftControllerProfileLabel = nullptr;
                    m_ftNoCollisionCheckImg = nullptr;
                    m_ftNoCollisionLabel = nullptr;
                    m_ftNoCollisionKeyLabel = nullptr;
                    m_ftRemovalVBox = nullptr;
                    m_ftRemovalHeader = nullptr;
                    m_ftLastRemovalCount = -1;
                    for (auto& c : m_ftGameModCheckImages) c = nullptr;
                    m_ftGameModEntries.clear();
                    m_ftRenameWidget = nullptr;
                    m_ftRenameInput = nullptr;
                    m_ftRenameConfirmLabel = nullptr;
                    m_ftRenameInputUW = FWeakObjectPtr{};
                    m_ftRenameVisible = false;
                    m_trashDlgWidget = nullptr;
                    m_trashDlgVisible = false;
                    m_trashDlgOpenTick = 0;

                    m_toolbarsVisible = false;

                    m_hoveredToolbar = -1;
                    m_hoveredSlot = -1;
                    m_lastClickLMB = false;

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

                        // [rc.12d 2026-05-23] PsiPath install-detection probe.
                        // Tobi's psi.2 pak ships /Game/Mods/PorterGoat/
                        // CharacterData/DA_NpcGoat_CharacterData_C (the
                        // FGKCharacterData aggregator). If it resolves on
                        // startup, psi.2 is installed and the runtime
                        // loadout chain should fire on goat spawn. If not,
                        // user is still on v1.5.0/v1.6.0 baseline.
                        {
                            static bool s_psiProbed = false;
                            if (!s_psiProbed)
                            {
                                s_psiProbed = true;
                                // [rc.17 PROBE FIX 2026-05-25] DataAssets are
                                // UObject instances, not UClass — drop the
                                // bogus `_C` suffix and look up as UObject*.
                                // Per Desktop Claude's log review: the old
                                // class-style path always returned NOT_FOUND
                                // even when the psi.2 pak was installed.
                                // [rc.17 PROBE FIX 2026-05-25] DataAssets are
                                // UObject instances, not UClass — drop the
                                // bogus `_C` suffix and look up as UObject*
                                // via LoadAsset_Blocking. Per Desktop Claude's
                                // log review: the old class-style path always
                                // returned NOT_FOUND even when the psi.2 pak
                                // was installed.
                                const wchar_t* psiPath = STR("/Game/Mods/PorterGoat/CharacterData/DA_NpcGoat_CharacterData.DA_NpcGoat_CharacterData");
                                UObject* psiObj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, psiPath);
                                if (!psiObj)
                                {
                                    psiObj = goat_callBlockingLoader(
                                        STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                        STR("/Script/Engine.Default__KismetSystemLibrary"),
                                        STR("Asset"), psiPath);
                                }
                                if (psiObj && isObjectAlive(psiObj))
                                {
                                    VLOG(STR("[MoriaCppMod] [Saddle-psi] DA_NpcGoat_CharacterData asset load: {:p} — psi.2 chain DETECTED\n"),
                                         (void*)psiObj);
                                }
                                else
                                {
                                    VLOG(STR("[MoriaCppMod] [Saddle-psi] DA_NpcGoat_CharacterData asset load: NOT_FOUND — psi.2 chain NOT installed\n"));
                                }
                                // Also probe the loadout DA.
                                const wchar_t* loadoutPath = STR("/Game/Mods/PorterGoat/Loadouts/DA_NpcGoat_PorterLoadout.DA_NpcGoat_PorterLoadout");
                                UObject* loadoutObj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, loadoutPath);
                                if (!loadoutObj)
                                {
                                    loadoutObj = goat_callBlockingLoader(
                                        STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                        STR("/Script/Engine.Default__KismetSystemLibrary"),
                                        STR("Asset"), loadoutPath);
                                }
                                VLOG(STR("[MoriaCppMod] [Saddle-psi] DA_NpcGoat_PorterLoadout asset load: {:p}\n"),
                                     (void*)loadoutObj);
                            }
                        }
                        // rc.41: arm a RECURRING post-load NPC sweep.
                        // First fire at +30 s, then every 30 s up to
                        // +5 min after character-load. Live log
                        // showed an NPC transitioning NoBed →
                        // CantReachBed at +70 s — the original
                        // one-shot at +30 s missed it.
                        // [rc.139] automatic post-load sweeps + character-load
                        // scan RETIRED — NPC unstuck is on-demand only now
                        // (BIND_UNSTUCK_NPCS keybind → runUnstuckNpcsNow()).
                        // m_npcPostLoadSweepNextMs = m_charLoadTime + 30000;
                        // m_npcPostLoadSweepEndMs  = m_charLoadTime + 300000;
                        // openNpcScanWindow(m_charLoadTime, STR("character-load"));
                        VLOG(STR("[MoriaCppMod] Character loaded - PC={:p} Pawn={:p}, waiting 15s before replay\n"),
                             (void*)m_localPC, (void*)m_localPawn);

                        // Preload goat-spawn assets so NUM- works in any zone,
                        // not only after walking past a wild goat. Best-effort —
                        // chunk-locked assets will still fail until visited.
                        ensureGoatSpawnBindings();
                        ensureGoatSkinMaterial();
                        ensurePackItemClass();
                        ensurePackStaticMesh();
                        dumpEquipComponentSchemas();
                        dumpPlayerControllerSchema();

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

                        // Re-apply persisted Cheats + Tweaks state now that the
                        // player's ASC + DataTables + world are all ready.
                        applySavedCheatsAndTweaks();

                        // Auto-spawn the New Building Bar once player + world are
                        // ready. One attempt per session.
                        // [rc.139] Only when the Advanced Builder is active —
                        // it starts INACTIVE, so on a normal load no bar is
                        // created (and no builder processing runs) until the
                        // user presses the toggle (Num+), which creates it.
                        if (m_advBuilderActive && !m_newBuildingBarSpawnAttempted)
                        {
                            m_newBuildingBarSpawnAttempted = true;
                            createNewBuildingBar();
                        }

                        // Pre-spawn rotation display hidden; tickRotationDisplay
                        // toggles visibility thereafter. Eliminates the Slate
                        // Prepass AV at 0xFF... that lazy-create-on-placement-edge
                        // produced (widget construction racing the Slate frame
                        // that ghost spawn lands on).
                        if (!m_rotDisplaySpawnAttempted)
                        {
                            m_rotDisplaySpawnAttempted = true;
                            createRotationDisplay();
                            if (m_rotDisplayWidget && isObjectAlive(m_rotDisplayWidget))
                            {
                                if (auto* fn = m_rotDisplayWidget->GetFunctionByNameInChain(STR("SetVisibility")))
                                { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(m_rotDisplayWidget, fn, p); }
                            }
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

            // [rc.41 PROBE N 2026-06-10] One-shot NpcInfo dump at ~3s
            // post-character-load. Tells us whether goat-shaped entries
            // survived save/reload, which determines whether GUID adoption
            // in spawnBellGoat will find existing entries to bind to.
            if (m_characterLoaded && !m_probeNFired && msSinceChar >= 3000)
            {
                m_probeNFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeN();
            }

            // [rc.44 PROBE P 2026-06-10] One-shot world scan at ~5s
            // post-character-load. Determines whether goat actors, body
            // containers, or item actors persisted in memory after reload.
            // Decides rc.45 architecture (sidecar vs reconnect).
            if (m_characterLoaded && !m_probePFired && msSinceChar >= 5000)
            {
                m_probePFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeP();
            }

            // [rc.47 PROBE Q 2026-06-12] One-shot manager + worldstate
            // UFunction sweep + dwarf PersistentData hex dump at ~7s
            // post-character-load. Per DC brief — hunts for restore path.
            if (m_characterLoaded && !m_probeQFired && msSinceChar >= 7000)
            {
                m_probeQFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeQ();
            }

            // [rc.48 PROBE R 2026-06-14] One-shot WorldState property
            // walk at ~9s post-character-load. Hunts for the registered-
            // runtime-actor list/map that StoreRuntimeActor writes to
            // internally. Critical for understanding the engine's save
            // graph and validating our rc.48 StoreRuntimeActor calls.
            if (m_characterLoaded && !m_probeRFired && msSinceChar >= 9000)
            {
                m_probeRFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeR();
            }

            // [rc.49 PROBE S 2026-06-14] One-shot MorNPCComponent
            // UFunction enumeration at ~11s post-character-load.
            // Hunts for name-write UFunctions (SetName/Rename/etc)
            // so we can pivot from the proven-fragile Role marker
            // to the more robust Name marker per user's original
            // direction.
            if (m_characterLoaded && !m_probeSFired && msSinceChar >= 11000)
            {
                m_probeSFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeS();
            }

            // [rc.50 PROBE T 2026-06-14] One-shot BP_NpcGoat_C
            // (actor class) name-keyword UFunction enumeration at
            // ~13s post-character-load. Probe S found only getters
            // + CanSetCustomDisplayName check on MorNPCComponent.
            // The setter must live on the actor itself.
            if (m_characterLoaded && !m_probeTFired && msSinceChar >= 13000)
            {
                m_probeTFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeT();
            }

            // [rc.53 PROBE U 2026-06-15] IMorSaveGameObject diagnostic
            // at ~15s post-character-load. Probes goat + chest for
            // SaveGameObjectIgnore/GetId/GetDormancy side-by-side.
            // Chests always persist → their interface state is the
            // reference the goat must match for save to include it.
            if (m_characterLoaded && !m_probeUFired && msSinceChar >= 15000)
            {
                m_probeUFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeU();
            }

            // [rc.54 PROBE V 2026-06-16] BP_NPCManager_C dump of
            // ValidNpcClasses / ValidNpcRestores / ValidNpcRoles.
            // Decides whether actor-recreation gate is open for
            // BP_NpcGoat_C. Fires at ~17s post-character-load.
            if (m_characterLoaded && !m_probeVFired && msSinceChar >= 17000)
            {
                m_probeVFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeV();
            }

            // [rc.55 PROBE W 2026-06-16] Full FMorNpcPersistentData
            // field dump for every NpcInfo entry. Reflection-driven
            // walk of the inner struct; logs every property name +
            // offset + decoded value. Diff goat vs vanilla dwarves
            // pinpoints the per-entry gating field.
            if (m_characterLoaded && !m_probeWFired && msSinceChar >= 20000)
            {
                m_probeWFired = true;
                // [rc.102 2026-07-10] DISABLED — runProbeW crashed the game
                // (FName::ToString AV 0x9ce0 via runProbeW_dumpField on a stale
                // NpcInfo pointer). Its question (per-entry persistence gating
                // field) was answered weeks ago; pure legacy diagnostic.
                // runProbeW();
            }

            // [rc.56 PROBE X 2026-06-16] NPCUnique DataTable row
            // enumeration. Finds the DataTable whose RowStruct is
            // MorUniqueNPCDefinition, iterates rows, logs each
            // (Name, CharacterClass softpath). The row matching
            // BP_NpcGoat is the row name we must write into bell-
            // spawned NpcInfo entry's UniqueNpc.RowName.
            if (m_characterLoaded && !m_probeXFired && msSinceChar >= 22000)
            {
                m_probeXFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeX();
            }

            // [rc.57 PROBE Y 2026-06-23] Wider net for goat row.
            // Re-runs Probe X with fixed CharacterClass offset (+16
            // in TSoftClassPtr), iterates ALL MorUniqueNPCDefinition
            // tables (not just first), brute-force scans every loaded
            // DataTable for goat-shaped rows.
            if (m_characterLoaded && !m_probeYFired && msSinceChar >= 24000)
            {
                m_probeYFired = true;
                // [rc.115 2026-07-11] legacy diagnostic DISABLED (probe T crashed FName::ToString on stale ptr; W crashed earlier; all questions answered)
                // runProbeY();
            }

            // [rc.59 AUTO-RESTORE 2026-06-26] At +5s post-character-load,
            // scan NpcInfo for Name='Rûdh' markers (persisted across
            // save/reload via FFastArraySerializer SaveGame flag).
            // For each marker without a live BP_NpcGoat actor matching
            // its NpcGuid, invoke spawnBellGoat() — existing GuidAdopt
            // finds the entry, binds the new actor, skips Register.
            //
            // Per [[feedback_validnpcrestores_trap]]: this completes
            // Free Range's unfinished goat persistence wiring without
            // touching any DataTable (avoids the ValidNpcRestores
            // populate trap that breaks bell-ring registration).
            if (m_characterLoaded && !m_autoRestoreFired && msSinceChar >= 5000)
            {
                m_autoRestoreFired = true;
                // [rc.112] RE-ENABLED — persistence phase 1: scan NpcInfo for our
                // goat marker on world load and restore/adopt.
                autoRestoreGoatsFromMarker();
                // [rc.126] duplicate-goat cleanup (park-not-destroy + orphan
                // spawns multiplied goats — census showed 3; zero-GUID goats
                // corrupt identity + sidecar keys). Keeps the identified one.
                dedupeGoats();
            }


            if (m_replay.active)
            {
                processReplayBatch();
            }


            // Bubble tracking - poll every 1s
            if (m_initialReplayDone && intervalElapsed(m_lastBubbleCheck, 30000))
            {
                if (updateCurrentBubble())
                {
                    VLOG(STR("[MoriaCppMod] [Bubble] Bubble changed - clearing processed comps for next scan\n"));
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

        // Update thread tick - called ~5ms on UE4SS's dedicated update thread.
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
