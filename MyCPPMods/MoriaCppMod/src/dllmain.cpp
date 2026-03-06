// Ã¢â€¢â€Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢â€”
// Ã¢â€¢â€˜  MoriaCppMod v2.7 Ã¢â‚¬â€ Advanced Builder & HISM Removal for Return to Moria   Ã¢â€¢â€˜
// Ã¢â€¢â€˜                                                                            Ã¢â€¢â€˜
// Ã¢â€¢â€˜  A UE4SS C++ mod for Return to Moria (UE4.27) providing:                  Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - HISM instance hiding with persistence across sessions/worlds          Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - Quick-build hotbar (F1-F8) with recipe capture & icon overlay         Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - Dual-toolbar swap system (PageDown) with name-matching resolve        Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - Rotation step control (F9) with ProcessEvent hook integration         Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - UMG config menu, mod controller toolbar, and target info popup       Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - Win32 GDI+ overlay bar with icon extraction pipeline                 Ã¢â€¢â€˜
// Ã¢â€¢â€˜                                                                            Ã¢â€¢â€˜
// Ã¢â€¢â€˜  Build:  cmake --build build --config Game__Shipping__Win64                Ã¢â€¢â€˜
// Ã¢â€¢â€˜          --target MoriaCppMod                                              Ã¢â€¢â€˜
// Ã¢â€¢â€˜  Deploy: Copy MoriaCppMod.dll -> <game>/Mods/MoriaCppMod/dlls/main.dll    Ã¢â€¢â€˜
// Ã¢â€¢â€˜                                                                            Ã¢â€¢â€˜
// Ã¢â€¢â€˜  Source: github.com/jbowensii/MoriaAdvancedBuilder                        Ã¢â€¢â€˜
// Ã¢â€¢â€˜  Date:   2026-02-26                                                        Ã¢â€¢â€˜
// Ã¢â€¢Å¡Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢Â

#include "moria_common.h"
#include "moria_reflection.h"
#include "moria_keybinds.h"

namespace MoriaMods
{

    // Overlay thread entry point (moria_overlay.cpp)
    DWORD WINAPI overlayThreadProc(LPVOID);


    // Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢Â
    // Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢Â
    // Section 6: MoriaCppMod Class Ã¢â‚¬â€ Main Mod Implementation
    //   Subsections: 6A File I/O, 6B Player Helpers, 6C Display/UI,
    //   6D HISM Removal, 6E Inventory/Toolbar, 6F Debug/Cheat, 6G Quick-Build,
    //   6H Icon Extraction, 6I UMG Widgets, 6J Overlay Management, 6K Public API
    // Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢Â
    class MoriaCppMod : public RC::CppUserModBase
    {
      private:
        std::vector<RemovedInstance> m_undoStack;
        std::vector<SavedRemoval> m_savedRemovals;
        std::set<std::string> m_typeRemovals; // mesh IDs to remove ALL of (Num6)
        std::set<UObject*> m_processedComps;
        int m_frameCounter{0};
        bool m_replayActive{false};
        bool m_characterLoaded{false};
        bool m_initialReplayDone{false};
        int m_stuckLogCount{0}; // only log stuck entries once
        std::string m_saveFilePath;
        PSOffsets m_ps;
        // BELIEVED DEAD CODE -- chat widget system superseded by showOnScreen()
        // UObject* m_chatWidget{nullptr};
        // UObject* m_sysMessages{nullptr};
        std::vector<bool> m_appliedRemovals; // parallel to m_savedRemovals: true = already removed

        // Real-time interval tracking
        ULONGLONG m_lastWorldCheck{0};     // world-unload detection (~1s)
        ULONGLONG m_lastCharPoll{0};       // character detection (~0.5s)
        ULONGLONG m_lastStreamCheck{0};    // new component streaming (~3s)
        ULONGLONG m_lastRescanTime{0};     // full rescan (~60s)
        ULONGLONG m_charLoadTime{0};       // timestamp when character first detected
        ULONGLONG m_lastContainerScan{0};  // container discovery retry (~2s)
        bool m_containerTimeoutLogged{false}; // one-shot flag for 65s timeout message

        // Throttled replay: spread hides across frames (render thread safety)
        struct ReplayState
        {
            std::vector<RC::Unreal::FWeakObjectPtr> compQueue;
            size_t compIdx{0};
            int instanceIdx{0}; // resume position within current component
            bool active{false};
            int totalHidden{0};
        };
        ReplayState m_replay;
        static constexpr int MAX_HIDES_PER_FRAME = 3; // conservative limit

        // Ã¢â€â‚¬Ã¢â€â‚¬ Tracking helpers Ã¢â€â‚¬Ã¢â€â‚¬

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

        // Ã¢â€â‚¬Ã¢â€â‚¬ File I/O Ã¢â€â‚¬Ã¢â€â‚¬

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6A: File I/O & Persistence Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // Save/load HISM removals + quickbuild slots; format: meshName|posX|posY|posZ or @meshName

        void loadSaveFile()
        {
            m_savedRemovals.clear();
            m_typeRemovals.clear();
            std::ifstream file(m_saveFilePath);
            if (!file.is_open())
            {
                VLOG(STR("[MoriaCppMod] No save file found (first run)\n"));
                return;
            }
            std::string line;
            while (std::getline(file, line))
            {
                auto parsed = parseRemovalLine(line);
                if (auto* pos = std::get_if<ParsedRemovalPosition>(&parsed))
                {
                    m_savedRemovals.push_back({pos->meshName, pos->posX, pos->posY, pos->posZ});
                }
                else if (auto* tr = std::get_if<ParsedRemovalTypeRule>(&parsed))
                {
                    m_typeRemovals.insert(tr->meshName);
                }
            }

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

            // No dedup: stacked instances share position, each matches a different one on replay
            m_appliedRemovals.assign(m_savedRemovals.size(), false);

            VLOG(STR("[MoriaCppMod] Loaded {} position removals + {} type rules\n"), m_savedRemovals.size(), m_typeRemovals.size());
        }

        void appendToSaveFile(const SavedRemoval& sr)
        {
            std::ofstream file(m_saveFilePath, std::ios::app);
            if (!file.is_open()) return;
            file << sr.meshName << "|" << sr.posX << "|" << sr.posY << "|" << sr.posZ << "\n";
        }

        void rewriteSaveFile()
        {
            std::ofstream file(m_saveFilePath, std::ios::trunc);
            if (!file.is_open()) return;
            file << "# MoriaCppMod removed instances\n";
            file << "# meshName|posX|posY|posZ = single instance\n";
            file << "# @meshName = remove ALL of this type\n";
            for (auto& type : m_typeRemovals)
                file << "@" << type << "\n";
            for (auto& sr : m_savedRemovals)
                file << sr.meshName << "|" << sr.posX << "|" << sr.posY << "|" << sr.posZ << "\n";
        }

        // Build config UI display entries from removed_instances.txt
        void buildRemovalEntries()
        {
            std::vector<RemovalEntry> entries;
            std::ifstream file(m_saveFilePath);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    if (line.empty() || line[0] == '#') continue;
                    RemovalEntry entry{};
                    if (line[0] == '@')
                    {
                        entry.isTypeRule = true;
                        entry.meshName = line.substr(1);
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        entry.coordsW = Loc::get("ui.type_rule") + L" (all instances)";
                    }
                    else
                    {
                        entry.isTypeRule = false;
                        std::istringstream ss(line);
                        std::string token;
                        if (!std::getline(ss, entry.meshName, '|')) continue;
                        try
                        {
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posX = std::stof(token);
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posY = std::stof(token);
                            if (!std::getline(ss, token, '|')) continue;
                            entry.posZ = std::stof(token);
                        }
                        catch (...)
                        {
                            continue;
                        }
                        entry.friendlyName = extractFriendlyName(entry.meshName);
                        entry.fullPathW = std::wstring(entry.meshName.begin(), entry.meshName.end());
                        entry.coordsW = std::format(L"X: {:.1f}   Y: {:.1f}   Z: {:.1f}", entry.posX, entry.posY, entry.posZ);
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

        #include "moria_common.inl"    // Shared utilities, coordinate system, player helpers
        #include "moria_datatable.inl" // DataTable CRUD utility (read/write/add/remove rows)

        #include "moria_debug.inl"    // 6C + 6F: Display, debug, cheat commands

        #include "moria_hism.inl"       // 6D: HISM removal system

        #include "moria_inventory.inl"   // 6E: Inventory & toolbar swap

        #include "moria_stability.inl"   // Stability audit (scan + highlight)



        static inline MoriaCppMod* s_instance{nullptr};

        // Quick-build hotbar: F1-F12 recipe slots
        static constexpr int QUICK_BUILD_SLOTS = 12;

        static constexpr int BLOCK_DATA_SIZE = 120; // bLock struct size in blockSelectedEvent
        // FMorConstructionRecipeRowHandle: [0-7] DT ptr, [8-15] FName
        static constexpr int RECIPE_HANDLE_SIZE = 16;

        struct RecipeSlot
        {
            std::wstring displayName;
            std::wstring textureName;             // e.g. "T_UI_BuildIcon_AdornedDoor"
            std::wstring rowName;                 // persisted FName RowName (e.g. "Recipe_Beorn_Wall_3x4m_A")
            uint8_t bLockData[BLOCK_DATA_SIZE]{};  // session-only captured recipe struct
            bool hasBLockData{false};
            uint8_t recipeHandle[RECIPE_HANDLE_SIZE]{}; // session-only recipe handle
            bool hasHandle{false};
            bool used{false};
        };
        RecipeSlot m_recipeSlots[QUICK_BUILD_SLOTS]{};

        // Last captured recipe from manual click (post-hook)
        std::wstring m_lastCapturedName;
        uint8_t m_lastCapturedBLock[BLOCK_DATA_SIZE]{};
        uint8_t m_lastCapturedHandle[RECIPE_HANDLE_SIZE]{};
        bool m_hasLastCapture{false};
        bool m_hasLastHandle{false};
        bool m_isAutoSelecting{false}; // suppresses post-hook capture during quickbuild


        // Quick-build state machine
        enum class PlacePhase { Idle, CancelGhost, CloseMenu, WaitReopen, PrimeOpen, OpenMenu, SelectRecipe };
        int m_quickBuildSwapDelay{5};  // frames to wait between close and reopen (Slate cleanup)
        // Result of selectRecipeOnBuildTab Ã¢â‚¬â€ distinguishes "still loading" from "genuinely missing"
        enum class SelectResult { Found, Loading, NotFound };
        int m_pendingQuickBuildSlot{-1};     // which F1-F8 slot is pending (-1 = none)
        PlacePhase m_qbPhase{PlacePhase::Idle};    // quickbuild phase
        ULONGLONG m_qbStartTime{0};          // timestamp when SM entered non-idle
        ULONGLONG m_qbRetryTime{0};          // timestamp of last retry/phase change
        int m_qbWaitCount{0};                // WaitReopen frame countdown

        // Eager handle resolution SM (runs once at toolbar creation)
        enum class HandleResolvePhase { None, Priming, Resolving, Done };
        HandleResolvePhase m_handleResolvePhase{HandleResolvePhase::None};
        ULONGLONG m_handleResolveStartTime{0};  // timestamp when resolve SM started
        ULONGLONG m_handleResolveRetryTime{0};  // timestamp of last retry in Priming phase
        int m_handleResolveSlotIdx{0};          // which slot we're resolving next in Resolving phase
        ULONGLONG m_lastDirectSelectTime{0};    // cooldown: last SelectRecipe via DIRECT path (ms)
        ULONGLONG m_lastShowHideTime{0};        // cooldown: last Show()/Hide() on build tab (ms)
        ULONGLONG m_lastQBSelectTime{0};        // cooldown: last blockSelectedEvent completion (ms)

        // Cached widget/component pointers (invalidated on world unload)
        UObject* m_cachedBuildComp{nullptr}; // UMorBuildingComponent on player character
        UObject* m_cachedBuildHUD{nullptr};  // UI_WBP_BuildHUDv2_C (UBuildOverlayWidget)
        UObject* m_cachedBuildTab{nullptr};  // UI_WBP_Build_Tab_C
        UFunction* m_fnIsVisible{nullptr};   // cached IsVisible() on Build_Tab (UWidget standard)

        // Target-to-build (Shift+F10)
        std::wstring m_targetBuildName;      // display name from last F10 target
        std::wstring m_targetBuildRecipeRef; // class name sans BP_ prefix (for bLock matching)
        std::wstring m_targetBuildRowName;   // DT_Constructions row name (also key for DT_ConstructionRecipes)
        bool m_lastTargetBuildable{false};   // was the last target buildable?
        bool m_isTargetBuild{false};          // true when SM is running a target-build (vs F-key)
        bool m_buildMenuWasOpen{false};      // tracks build menu open/close for ActionBar refresh

        std::vector<uint8_t> m_bagHandle; // cached EpicPack bag FItemHandle

        bool m_showHotbar{true}; // Win32 overlay bar visibility

        // UMG builders bar
        UObject* m_umgBarWidget{nullptr};
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

        // Mod Controller toolbar Ã¢â‚¬â€ 4x3 grid, lower-right of screen
        static constexpr int MC_SLOTS = 12;
        UObject* m_mcBarWidget{nullptr};
        UObject* m_mcStateImages[MC_SLOTS]{};
        UObject* m_mcIconImages[MC_SLOTS]{};
        UmgSlotState m_mcSlotStates[MC_SLOTS]{};

        // Key label overlays (UTextBlock + UImage per slot)
        UObject* m_umgKeyLabels[8]{};
        UObject* m_umgKeyBgImages[8]{};
        UObject* m_mcKeyLabels[MC_SLOTS]{};
        UObject* m_mcKeyBgImages[MC_SLOTS]{};
        UObject* m_umgTexBlankRect{nullptr};
        UObject* m_mcRotationLabel{nullptr};
        UObject* m_mcSlot0Overlay{nullptr};
        UObject* m_mcSlot8Overlay{nullptr};
        UObject* m_mcSlot10Overlay{nullptr};
        // Advanced Builder toolbar (toggle button)
        UObject* m_abBarWidget{nullptr};
        UObject* m_abKeyLabel{nullptr};
        UObject* m_abStateImage{nullptr};
        bool m_toolbarsVisible{false};
        bool m_characterHidden{false};
        bool m_flyMode{false};
        bool m_snapEnabled{true};
        float m_savedMaxSnapDistance{-1.0f};       // -1 = not yet captured
        bool m_noCollisionWhileFlying{true};       // INI: disable collision when flying
        bool m_buildMenuPrimed{false};
        bool m_buildTabAfterShowFired{false};      // set by OnAfterShow hook

        // Stash container repair (once per character load)
        bool m_repairDone{false};

        // Viewport / cursor / scale (moria_common.inl)
        ScreenCoords m_screen;

        // Toolbar repositioning mode
        bool m_repositionMode{false};
        int  m_dragToolbar{-1};
        float m_dragOffsetX{0}, m_dragOffsetY{0};
        bool m_hitDebugDone{false};
        UClass* m_wllClass{nullptr};
        UObject* m_repositionMsgWidget{nullptr};
        UObject* m_repositionInfoBoxWidget{nullptr};
        // Toolbar positions as viewport fractions (0.0-1.0); -1 = default. 0=BB, 1=AB, 2=MC, 3=IB
        static constexpr int TB_COUNT = 4;
        float m_toolbarPosX[TB_COUNT]{-1, -1, -1, -1};
        float m_toolbarPosY[TB_COUNT]{-1, -1, -1, -1};
        // Cached sizes in viewport pixels (for drag hit-test)
        float m_toolbarSizeW[TB_COUNT]{0, 0, 0, 0};
        float m_toolbarSizeH[TB_COUNT]{0, 0, 0, 0};
        // Default positions (4K tuning)
        static constexpr float TB_DEF_X[TB_COUNT]{0.4992f, 0.7505f, 0.8492f, 0.9414f};
        static constexpr float TB_DEF_Y[TB_COUNT]{0.0287f, 0.9111f, 0.6148f, 0.5463f};
        // Toolbar click + hover state
        int m_hoveredToolbar{-1};
        int m_hoveredSlot{-1};
        bool m_lastClickLMB{false};
        FBoolProperty* m_bpShowMouseCursor{nullptr};
        // UMG Target Info popup
        UObject* m_targetInfoWidget{nullptr};
        UObject* m_tiTitleLabel{nullptr};
        UObject* m_tiClassLabel{nullptr};
        UObject* m_tiNameLabel{nullptr};
        UObject* m_tiDisplayLabel{nullptr};
        UObject* m_tiPathLabel{nullptr};
        UObject* m_tiBuildLabel{nullptr};
        UObject* m_tiRecipeLabel{nullptr};
        ULONGLONG m_tiShowTick{0};                  // 0 = hidden
        // BELIEVED DEAD CODE -- InfoBox popup system: widget created but never shown (showInfoBox never called)
        // UObject* m_infoBoxWidget{nullptr};          // root UUserWidget
        // UObject* m_ibTitleLabel{nullptr};            // title (e.g. "Removed", "Undo")
        // UObject* m_ibMessageLabel{nullptr};          // message body
        // ULONGLONG m_ibShowTick{0};                   // GetTickCount64() when shown; 0 = hidden
        // UMG Config Menu
        UObject* m_configWidget{nullptr};
        UObject* m_cfgTabLabels[3]{};
        UObject* m_cfgTabContent[3]{};
        UObject* m_cfgTabImages[3]{};
        UObject* m_cfgTabActiveTexture{nullptr};
        UObject* m_cfgTabInactiveTexture{nullptr};
        UObject* m_cfgVignetteImage{nullptr};
        UObject* m_cfgScrollBoxes[3]{};
        int m_cfgActiveTab{0};
        bool m_cfgVisible{false};
        // Tab 0: Optional Mods
        UObject* m_cfgFreeBuildLabel{nullptr};
        UObject* m_cfgFreeBuildCheckImg{nullptr};
        UObject* m_cfgNoCollisionLabel{nullptr};
        UObject* m_cfgNoCollisionCheckImg{nullptr};
        UObject* m_cfgUnlockBtnImg{nullptr};
        // Tab 1: Key Mapping
        UObject* m_cfgKeyValueLabels[BIND_COUNT]{};
        UObject* m_cfgKeyBoxLabels[BIND_COUNT]{};
        UObject* m_cfgModifierLabel{nullptr};
        UObject* m_cfgModBoxLabel{nullptr};
        // Tab 2: Hide Environment
        UObject* m_cfgRemovalHeader{nullptr};
        UObject* m_cfgRemovalVBox{nullptr};
        int m_cfgLastRemovalCount{-1};
        // Stability audit: PointLights at problem locations
        ULONGLONG m_auditClearTime{0};
        struct AuditLoc { float x, y, z; bool critical; };
        std::vector<AuditLoc> m_auditLocations;
        std::vector<UObject*> m_auditSpawnedActors;

        #include "moria_placement.inl"   // Build placement lifecycle, snap, ghost piece mgmt
        #include "moria_quickbuild.inl"  // 6G + 6H: Quick-build dispatch & icon extraction

        #include "moria_widgets.inl"     // 6I: UMG widget system

        #include "moria_overlay_mgmt.inl" // 6J: Overlay & window management

      public:
        // Ã¢â€â‚¬Ã¢â€â‚¬ 6K: Public Interface Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // Constructor, destructor, on_unreal_init, on_update
        MoriaCppMod()
        {
            ModVersion = STR("2.7");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("Advanced builder, HISM removal, quick-build hotbar, UMG config menu");
            // Init removal list CS before loadSaveFile can be called
            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            VLOG(STR("[MoriaCppMod] Loaded v2.7\n"));
        }

        ~MoriaCppMod() override
        {
            // Null s_instance first: hooks would call GetName() on stale UObjects during shutdown
            s_instance = nullptr;

            stopOverlay();
            if (s_config.removalCSInit)
            {
                DeleteCriticalSection(&s_config.removalCS);
                s_config.removalCSInit = false;
            }
        }

        // Init: load config/saves, register keybinds, start overlay, install ProcessEvent hooks.
        auto on_unreal_init() -> void override
        {
            VLOG(STR("[MoriaCppMod] Unreal initialized.\n"));

            // Load config first so Language preference is available for localization
            loadConfig();

            // Load localization string table (compiled English defaults + optional JSON override)
            Loc::load("Mods/MoriaCppMod/localization/", s_language);
            // Patch static keybind labels from string table
            s_bindings[0].label = Loc::get("bind.quick_build_1").c_str();
            s_bindings[0].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[1].label = Loc::get("bind.quick_build_2").c_str();
            s_bindings[1].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[2].label = Loc::get("bind.quick_build_3").c_str();
            s_bindings[2].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[3].label = Loc::get("bind.quick_build_4").c_str();
            s_bindings[3].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[4].label = Loc::get("bind.quick_build_5").c_str();
            s_bindings[4].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[5].label = Loc::get("bind.quick_build_6").c_str();
            s_bindings[5].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[6].label = Loc::get("bind.quick_build_7").c_str();
            s_bindings[6].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[7].label = Loc::get("bind.quick_build_8").c_str();
            s_bindings[7].section = Loc::get("bind.section_quick_building").c_str();
            s_bindings[8].label = Loc::get("bind.rotation").c_str();
            s_bindings[8].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[9].label = Loc::get("bind.target").c_str();
            s_bindings[9].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[10].label = Loc::get("bind.integrity_check").c_str();
            s_bindings[10].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[11].label = Loc::get("bind.mod_menu_4").c_str();
            s_bindings[11].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[12].label = Loc::get("bind.toolbar_swap").c_str();
            s_bindings[12].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[13].label = Loc::get("bind.snap_off").c_str();
            s_bindings[13].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[14].label = Loc::get("bind.empty").c_str();
            s_bindings[14].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[15].label = Loc::get("bind.empty").c_str();
            s_bindings[15].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[16].label = Loc::get("bind.remove_single").c_str();
            s_bindings[16].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[17].label = Loc::get("bind.undo_last").c_str();
            s_bindings[17].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[18].label = Loc::get("bind.remove_all").c_str();
            s_bindings[18].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[19].label = Loc::get("bind.configuration").c_str();
            s_bindings[19].section = Loc::get("bind.section_mod_controller").c_str();
            s_bindings[20].label = Loc::get("bind.ab_open").c_str();
            s_bindings[20].section = Loc::get("bind.section_advanced_builder").c_str();
            // Patch config tab names
            CONFIG_TAB_NAMES[0] = Loc::get("tab.optional_mods").c_str();
            CONFIG_TAB_NAMES[1] = Loc::get("tab.key_mapping").c_str();
            CONFIG_TAB_NAMES[2] = Loc::get("tab.hide_environment").c_str();

            m_saveFilePath = "Mods/MoriaCppMod/removed_instances.txt";
            loadSaveFile();
            buildRemovalEntries();
            probePrintString();
            loadQuickBuildSlots();

            // F1-F8 quickbuild; all 3 modifier variants registered, isModifierDown() gates at runtime

            const Input::Key fkeys[] = {Input::Key::F1, Input::Key::F2, Input::Key::F3, Input::Key::F4, Input::Key::F5, Input::Key::F6, Input::Key::F7, Input::Key::F8};
            for (int i = 0; i < 8; i++)
            { // F1-F8 for quickbuild Ã¢â‚¬â€ skip when config menu is visible (modal)
                register_keydown_event(fkeys[i], [this, i]() {
                    if (m_cfgVisible) return;
                    quickBuildSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::SHIFT}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::CONTROL}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::ALT}, [this, i]() {
                    if (m_cfgVisible) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
            }

            // Hotbar overlay toggle: Numpad * (Multiply)
            register_keydown_event(Input::Key::MULTIPLY, [this]() {
                if (m_cfgVisible) return;
                m_showHotbar = !m_showHotbar;
                s_overlay.visible = m_showHotbar;
                s_overlay.needsUpdate = true;
                showOnScreen(m_showHotbar ? Loc::get("msg.hotbar_overlay_on") : Loc::get("msg.hotbar_overlay_off"), 2.0f, 0.2f, 0.8f, 1.0f);
            });

            // Mod Controller toolbar toggle: Numpad 7
            register_keydown_event(Input::Key::NUM_SEVEN, [this]() { if (m_cfgVisible) return; createModControllerBar(); });

            // ProcessEvent hooks: rotation interception + recipe capture
            s_instance = this;
            Unreal::Hook::RegisterProcessEventPreCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance) return;
                if (!func) return;


                // Intercept RotatePressed/CcwPressed: set GATA rotation step + track total
                {
                    std::wstring fn(func->GetName());
                    if (fn == STR("RotatePressed") || fn == STR("RotateCcwPressed"))
                    {
                        std::wstring cls = safeClassName(context);
                        if (!cls.empty() && cls.find(STR("BuildHUD")) != std::wstring::npos)
                        {
                            UObject* gata = s_instance->resolveGATA();
                            if (gata)
                            {
                                const int step = s_overlay.rotationStep.load();
                                s_instance->setGATARotation(gata, static_cast<float>(step));
                                // Track cumulative rotation (0-359Ã‚Â°)
                                if (fn == STR("RotatePressed"))
                                {
                                    s_overlay.totalRotation = (s_overlay.totalRotation.load() + step) % 360;
                                }
                                else
                                {
                                    s_overlay.totalRotation = (s_overlay.totalRotation.load() - step + 360) % 360;
                                }
                                s_overlay.needsUpdate = true;
                                if (s_instance) s_instance->updateMcRotationLabel();
                            }
                        }
                    }
                }

                // Auto-restore snap after piece placement
                if (!s_instance->m_snapEnabled)
                {
                    std::wstring fn2(func->GetName());
                    if (fn2 == STR("BuildConstruction") || fn2 == STR("TryBuild"))
                    {
                        VLOG(STR("[MoriaCppMod] [Snap] Placement detected ({}), auto-restoring snap\n"), fn2);
                        s_instance->restoreSnap();
                    }
                }

            });

            // Post-hook: recipe capture from manual clicks + OnAfterShow signal
            Unreal::Hook::RegisterProcessEventPostCallback([](UObject* context, UFunction* func, void* parms) {
                if (!s_instance || !func) return;

                std::wstring fn(func->GetName());

                // OnAfterShow on Build_Tab: signal that menu is fully initialized
                if (fn == STR("OnAfterShow"))
                {
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        s_instance->m_buildTabAfterShowFired = true;
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow fired on Build_Tab\n"));
                    }
                    return;
                }

                // OnAfterHide on BuildHUD or Build_Tab: ghost piece gone, grey out snap slot
                if (fn == STR("OnAfterHide"))
                {
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_BuildHUDv2_C") || cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        QBLOG(STR("[MoriaCppMod] [Placement] OnAfterHide fired on {}\n"), cls);
                        s_instance->onGhostDisappeared();
                    }
                    return;
                }

                // Skip capture during automated quickbuild activation
                if (s_instance->m_isAutoSelecting) return;

                if (fn != STR("blockSelectedEvent")) return;
                std::wstring cls = safeClassName(context);
                if (cls != STR("UI_WBP_Build_Tab_C")) return;

                int sz = func->GetParmsSize();
                QBLOG(STR("[MoriaCppMod] [QB] POST-HOOK blockSelectedEvent: parmsSize={} parms={}\n"),
                      sz, parms ? STR("YES") : STR("NO"));
                if (!parms || sz < 132) return;
                uint8_t* p = reinterpret_cast<uint8_t*>(parms);

                // Resolve BSE offsets on first capture (needed before we can read selfRef)
                resolveBSEOffsets(func);

                // Extract display name from selfRef widget's blockName TextBlock
                QBLOG(STR("[MoriaCppMod] [QB] POST-HOOK: s_bse.selfRef={} s_bse.bLock={}\n"),
                      s_bse.selfRef, s_bse.bLock);
                if (s_bse.selfRef < 0) return;
                UObject* selfRef = *reinterpret_cast<UObject**>(p + s_bse.selfRef);
                if (!selfRef) return;

                std::wstring displayName = s_instance->readWidgetDisplayName(selfRef);
                if (displayName.empty()) return;

                s_instance->m_lastCapturedName = displayName;
                std::memcpy(s_instance->m_lastCapturedBLock, p, BLOCK_DATA_SIZE);
                s_instance->m_hasLastCapture = true;
                QBLOG(STR("[MoriaCppMod] [QuickBuild] Captured: '{}' (with bLock data)\n"), displayName);
                s_instance->logBLockDiagnostics(L"CAPTURE", displayName, p);

                // Capture recipe handle via GetSelectedRecipeHandle
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
                            buildHUD->ProcessEvent(getHandleFn, hParams.data());
                            std::memcpy(s_instance->m_lastCapturedHandle, hParams.data(), RECIPE_HANDLE_SIZE);
                            s_instance->m_hasLastHandle = true;
                            // Log the handle's RowName for diagnostics
                            uint32_t handleCI = *reinterpret_cast<uint32_t*>(hParams.data() + 8);
                            int32_t handleNum = *reinterpret_cast<int32_t*>(hParams.data() + 12);
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] Captured handle: RowName CI={} Num={}\n"), handleCI, handleNum);
                        }
                    }
                    else
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] GetSelectedRecipeHandle not found on build HUD\n"));
                    }
                }

                // Reset total rotation for new build piece selection
                s_overlay.totalRotation = 0;
                s_overlay.needsUpdate = true;
                s_instance->updateMcRotationLabel();

                // ONE-TIME: resolve bLock offset and validate BLOCK_DATA_SIZE
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

                // Ghost piece will appear from manual recipe selection — enable snap slot
                s_instance->onGhostAppeared();
            });

            m_replayActive = true;
            VLOG(
                    STR("[MoriaCppMod] v2.7: F1-F8=build | F9=rotate | F12=config | MC toolbar + AB bar\n"));
        }

        // Per-frame tick: state machines, replay, toolbar swap, quickbuild, icons, config, rescans.
        auto on_update() -> void override
        {

            // Create all three toolbars when character loads
            {
                bool justCreated = false;
                if (m_characterLoaded && !m_mcBarWidget)
                {
                    createModControllerBar();
                    justCreated = true;
                }
                if (m_characterLoaded && !m_umgBarWidget)
                {
                    createExperimentalBar();
                    justCreated = true;
                }
                if (m_characterLoaded && !m_abBarWidget)
                {
                    createAdvancedBuilderBar();
                    justCreated = true;
                    // Kick off eager handle resolution if any slots have rowNames from disk
                    if (m_handleResolvePhase == HandleResolvePhase::None)
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
                            QBLOG(STR("[MoriaCppMod] [HandleResolve] Toolbar created, starting eager handle resolution\n"));
                            m_handleResolvePhase = HandleResolvePhase::Priming;
                            m_handleResolveStartTime = GetTickCount64();
                            m_handleResolveRetryTime = m_handleResolveStartTime;
                        }
                        else
                        {
                            m_handleResolvePhase = HandleResolvePhase::Done;
                        }
                    }
                }
                if (m_characterLoaded && !m_targetInfoWidget)
                    createTargetInfoWidget();
                // BELIEVED DEAD CODE -- InfoBox popup never shown
                // if (m_characterLoaded && !m_infoBoxWidget)
                //     createInfoBox();
                if (m_characterLoaded && !m_configWidget)
                    createConfigWidget();
                // Hide MC + Builders bar immediately after creation (AB toggle reveals them)
                if (justCreated && !m_toolbarsVisible)
                {
                    setWidgetVisibility(m_mcBarWidget, 1);
                    setWidgetVisibility(m_umgBarWidget, 1);
                }
            }

            // Refresh key labels when config screen changes a keybind (cross-thread flag)
            if (s_pendingKeyLabelRefresh.exchange(false))
            {
                refreshKeyLabels();
                if (m_cfgVisible) updateConfigKeyLabels();
            }

            // Target Info auto-close (10 seconds)
            if (m_tiShowTick > 0 && (GetTickCount64() - m_tiShowTick) >= 10000)
                hideTargetInfo();

            // Stability audit — auto-clear PointLights after timeout
            if (m_auditClearTime > 0 && GetTickCount64() >= m_auditClearTime)
                clearStabilityHighlights();

            // BELIEVED DEAD CODE -- InfoBox popup never shown, m_ibShowTick always 0
            // if (m_ibShowTick > 0 && (GetTickCount64() - m_ibShowTick) >= 10000)
            //     hideInfoBox();

            // Failsafe: if config is flagged visible but widget is gone, reset state
            if (m_cfgVisible && !m_configWidget)
            {
                m_cfgVisible = false;
                setInputModeGame();
                VLOG(STR("[MoriaCppMod] [CFG] Failsafe: config widget lost, resetting state\n"));
            }

            // Config key always polled (works even when config visible)
            {
                static bool s_lastCfgKey = false;
                uint8_t cfgVk = s_bindings[MC_BIND_BASE + 11].key;
                if (cfgVk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(cfgVk) & 0x8000) != 0;
                    if (nowDown && !s_lastCfgKey)
                        toggleConfig();
                    s_lastCfgKey = nowDown;
                }
            }

            // MC keybind polling; track state always, skip actions when config visible
            if (m_mcBarWidget)
            {
                static bool s_lastMcKey[MC_SLOTS]{};
                // Hoist SHIFT check (avoids redundant GetAsyncKeyState calls)
                const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (i == 11) { s_lastMcKey[i] = false; continue; } // slot 11 handled by always-on config toggle
                    uint8_t vk = s_bindings[MC_BIND_BASE + i].key;
                    if (vk == 0) { s_lastMcKey[i] = false; continue; }
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    // SHIFT reverses numpad keys; check alternate VK (only when physically held)
                    if (!nowDown && shiftHeld)
                    {
                        uint8_t alt = numpadShiftAlternate(vk);
                        if (alt) nowDown = (GetAsyncKeyState(alt) & 0x8000) != 0;
                    }
                    if (nowDown && !s_lastMcKey[i] && !m_cfgVisible && !m_repositionMode)
                    {
                        VLOG(
                            STR("[MoriaCppMod] [MC] Slot {} pressed (VK=0x{:02X})\n"), i, vk);
                        dispatchMcSlot(i);
                    }
                    s_lastMcKey[i] = nowDown;
                }
            }

            // Repositioning mode Ã¢â‚¬â€ handle ESC exit + mouse drag (runs every frame)
            if (m_repositionMode)
            {
                // ESC to exit repositioning mode
                static bool s_lastReposEsc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastReposEsc)
                    toggleRepositionMode();
                s_lastReposEsc = escDown;

                // Mouse drag in viewport fractions [0..1] (avoids Win32/UMG mismatch)
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

                // Get cursor as fraction of viewport
                float curFracX = 0.5f, curFracY = 0.5f;
                m_screen.getCursorFraction(curFracX, curFracY);

                // Viewport size (cached)
                int32_t rawVW = m_screen.viewW, rawVH = m_screen.viewH;

                // Cursor in UMG slate units
                float slateCursorX = 0.0f, slateCursorY = 0.0f;
                bool gotMouse = getMousePositionSlate(slateCursorX, slateCursorY);
                if (!gotMouse)
                {
                    // Fallback: Win32 client pixels converted to slate units
                    int cx, cy, cw, ch;
                    if (m_screen.getCursorClientPixels(cx, cy, cw, ch)) {
                        slateCursorX = m_screen.pixelToSlateX(static_cast<float>(cx));
                        slateCursorY = m_screen.pixelToSlateY(static_cast<float>(cy));
                    }
                }

                if (lmb && m_dragToolbar < 0)
                {
                    // Pick closest widget within hit radius (fractions)
                    constexpr float kHitRadX = 0.35f;  // 35% of screen width each side
                    constexpr float kHitRadY = 0.25f;  // 25% of screen height each side
                    float bestDist = 1e9f;
                    int   bestIdx  = -1;
                    UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget};
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

                    if (!m_hitDebugDone)
                    {
                        m_hitDebugDone = true;
                        VLOG(STR("[MoriaCppMod] [HitTest] curFrac=({:.3f},{:.3f}) hit={}\n"), curFracX, curFracY, bestIdx);
                        UObject* dbgW[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget};
                        for (int di = 0; di < TB_COUNT; di++)
                        {
                            float dfx = (m_toolbarPosX[di] >= 0) ? m_toolbarPosX[di] : TB_DEF_X[di];
                            float dfy = (m_toolbarPosY[di] >= 0) ? m_toolbarPosY[di] : TB_DEF_Y[di];
                            VLOG(STR("[MoriaCppMod] [HitTest] [{}] frac=({:.3f},{:.3f}) widget={}\n"),
                                di, dfx, dfy, dbgW[di] ? STR("OK") : STR("null"));
                        }
                        showOnScreen(std::format(L"HIT: cur=({:.3f},{:.3f}) hit={}", curFracX, curFracY, bestIdx).c_str(), 8.0f, 1.0f, 0.5f, 0.0f);
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
                    UObject* widgets[TB_COUNT] = {m_umgBarWidget, m_abBarWidget, m_mcBarWidget, m_repositionInfoBoxWidget};
                    float px = m_screen.fracToPixelX(fx);
                    float py = m_screen.fracToPixelY(fy);
                    static int s_dragLog = 0;
                    if (++s_dragLog % 30 == 1) // log every 30 ticks (~0.5s)
                        VLOG(STR("[MoriaCppMod] [Drag] tb={} curFrac=({:.3f},{:.3f}) -> frac=({:.3f},{:.3f}) pos=({:.0f},{:.0f}) vp={}x{}\n"),
                            m_dragToolbar, curFracX, curFracY, fx, fy, px, py, rawVW, rawVH);
                    setWidgetPosition(widgets[m_dragToolbar], px, py, true);
                }
                else if (!lmb && m_dragToolbar >= 0)
                {
                    m_dragToolbar = -1;
                }
            }

            // AB toolbar polling: toggle visibility, MODIFIER = reposition mode
            {
                static bool s_lastAbKey = false;
                uint8_t vk = s_bindings[BIND_AB_OPEN].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbKey && !m_cfgVisible)
                    {
                        if (isModifierDown())
                        {
                            // MODIFIER + AB_OPEN Ã¢â€ â€™ toggle repositioning mode
                            toggleRepositionMode();
                        }
                        else if (!m_repositionMode)
                        {
                            // AB_OPEN alone Ã¢â€ â€™ toggle toolbar visibility (existing behavior)
                            m_toolbarsVisible = !m_toolbarsVisible;
                            VLOG(STR("[MoriaCppMod] [AB] Toggle pressed Ã¢â‚¬â€ toolbars {}\n"),
                                                            m_toolbarsVisible ? STR("VISIBLE") : STR("HIDDEN"));

                            uint8_t vis = m_toolbarsVisible ? 0 : 1;
                            setWidgetVisibility(m_umgBarWidget, vis);
                            setWidgetVisibility(m_mcBarWidget, vis);
                        }
                    }
                    s_lastAbKey = nowDown; // always update -- prevents stale edge after config closes
                }
            }


            // Re-apply UI input mode after Alt-Tab
            {
                HWND gameWnd = findGameWindow();
                static bool s_lastGameFocused = true;
                bool gameFocused = gameWnd && (GetForegroundWindow() == gameWnd);
                if (gameFocused && !s_lastGameFocused && m_cfgVisible)
                {
                    setInputModeUI();
                    VLOG(STR("[MoriaCppMod] [CFG] Focus regained -- re-applied UI input mode\n"));
                }
                s_lastGameFocused = gameFocused;
            }

            // Toolbar click + hover detection (when cursor is visible and toolbars shown)
            if (m_toolbarsVisible && !m_repositionMode && !m_cfgVisible)
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
                        case 0: // Builders Bar — quickbuild slot 0-7
                            // Guard: skip while placement active (LMB would also place, causing crash)
                            if (hitSlot >= 0 && hitSlot < 8 && !isPlacementActive())
                                quickBuildSlot(hitSlot);
                            break;
                        case 1: // AB bar — toggle toolbar visibility
                        {
                            m_toolbarsVisible = !m_toolbarsVisible;
                            uint8_t vis = m_toolbarsVisible ? 0 : 1;
                            setWidgetVisibility(m_umgBarWidget, vis);
                            setWidgetVisibility(m_mcBarWidget, vis);
                            break;
                        }
                        case 2: // MC bar — dispatch slot action
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
                    // Cursor not visible — clear hover
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

            // UMG Config keyboard interaction
            if (m_cfgVisible && m_configWidget)
            {
                // Tab switching: 1/2/3 keys
                static bool s_lastCfgTab[3]{};
                for (int t = 0; t < 3; t++)
                {
                    bool down = (GetAsyncKeyState('1' + t) & 0x8000) != 0;
                    if (down && !s_lastCfgTab[t])
                        switchConfigTab(t);
                    s_lastCfgTab[t] = down;
                }

                // Mouse click tab switching
                static bool s_captureSkipTick = false; // skip key scan one frame after entering capture mode
                static bool s_lastLMB = false;
                bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmbDown && !s_lastLMB)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        POINT cursor{curX, curY};
                        float s2p = m_screen.viewportScale;
                        // Config widget: pos (viewW/2, viewH/2 - 100), size 1400x900 Slate, alignment (0.5,0.5)
                        int wLeft = static_cast<int>(viewW / 2.0f - 700.0f * s2p);
                        int wTop  = static_cast<int>(viewH / 2.0f - 100.0f - 450.0f * s2p);
                        // Tab bar: ~98px from top, each tab 420x66, 40px left padding (all scaled)
                        int tabY0 = static_cast<int>(wTop + 98.0f * s2p), tabY1 = static_cast<int>(tabY0 + 66.0f * s2p);
                        if (cursor.y >= tabY0 && cursor.y <= tabY1)
                        {
                            int tabX0 = static_cast<int>(wLeft + 40.0f * s2p);
                            int tabW = static_cast<int>(420.0f * s2p);
                            for (int t = 0; t < 3; t++)
                            {
                                if (cursor.x >= tabX0 + t * tabW && cursor.x < tabX0 + (t + 1) * tabW)
                                {
                                    switchConfigTab(t);
                                    break;
                                }
                            }
                        }
                        // Tab 0: Free Build checkbox click Ã¢â‚¬â€ entire row
                        if (m_cfgActiveTab == 0)
                        {
                            int cbX0 = static_cast<int>(wLeft + 20.0f * s2p), cbX1 = static_cast<int>(wLeft + (1400.0f - 20.0f) * s2p);
                            // Free Build: checkbox row + description text (generous Y range)
                            int cbY0 = static_cast<int>(wTop + 210.0f * s2p), cbY1 = static_cast<int>(wTop + 310.0f * s2p);
                            if (cursor.x >= cbX0 && cursor.x <= cbX1 && cursor.y >= cbY0 && cursor.y <= cbY1)
                            {
                                s_config.pendingToggleFreeBuild = true;
                                VLOG(STR("[MoriaCppMod] [CFG] Free Build toggle via mouse click\n"));
                            }
                            // No Collision: checkbox row + description text (generous Y range)
                            int ncY0 = static_cast<int>(wTop + 310.0f * s2p), ncY1 = static_cast<int>(wTop + 410.0f * s2p);
                            if (cursor.x >= cbX0 && cursor.x <= cbX1 && cursor.y >= ncY0 && cursor.y <= ncY1)
                            {
                                m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                                saveConfig();
                                updateConfigNoCollision();
                                VLOG(STR("[MoriaCppMod] [CFG] No Collision toggle: {}\n"), m_noCollisionWhileFlying ? 1 : 0);
                            }
                            // Unlock All Recipes button: centered, 420px wide
                            int ubY0 = static_cast<int>(wTop + 430.0f * s2p), ubY1 = static_cast<int>(ubY0 + 68.0f * s2p);
                            int ubX0 = static_cast<int>(wLeft + (1400.0f - 420.0f) / 2.0f * s2p), ubX1 = static_cast<int>(ubX0 + 420.0f * s2p);
                            if (cursor.x >= ubX0 && cursor.x <= ubX1 && cursor.y >= ubY0 && cursor.y <= ubY1)
                            {
                                s_config.pendingUnlockAllRecipes = true;
                                VLOG(STR("[MoriaCppMod] [CFG] Unlock All Recipes via mouse click\n"));
                            }
                        }
                        // Tab 1: Key box click for rebinding
                        if (m_cfgActiveTab == 1)
                        {
                            // Key boxes are right-aligned within the slate-scaled widget
                            int kbX0 = static_cast<int>(wLeft + 1050.0f * s2p);
                            int kbX1 = static_cast<int>(wLeft + 1400.0f * s2p);
                            // First key row starts after tabs+seps (~190px from top)
                            int contentY = static_cast<int>(wTop + 190.0f * s2p);
                            int rowHeight = static_cast<int>(44.0f * s2p);
                            int sectionHeight = static_cast<int>(48.0f * s2p);
                            // Get ScrollBox scroll offset to account for scrolled content
                            float scrollOff = 0.0f;
                            if (m_cfgScrollBoxes[1])
                            {
                                auto* getScrollFn = m_cfgScrollBoxes[1]->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    m_cfgScrollBoxes[1]->ProcessEvent(getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV) scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }
                            if (cursor.x >= kbX0 && cursor.x <= kbX1)
                            {
                                // Add scroll offset: screen click maps to content position + scroll
                                int y = cursor.y - contentY + static_cast<int>(scrollOff * s2p);
                                if (y >= 0)
                                {
                                    // Walk through bindings to find which row was clicked
                                    int currentY = 0;
                                    const wchar_t* lastSec = nullptr;
                                    bool bindMatched = false;
                                    for (int b = 0; b < BIND_COUNT; b++)
                                    {
                                        // Skip reserved/placeholder entries (must match createConfigWidget loop)
                                        if (wcscmp(s_bindings[b].label, L"Reserved") == 0) continue;

                                        if (!lastSec || wcscmp(lastSec, s_bindings[b].section) != 0)
                                        {
                                            lastSec = s_bindings[b].section;
                                            currentY += sectionHeight; // section header
                                        }
                                        if (y >= currentY && y < currentY + rowHeight)
                                        {
                                            s_capturingBind = b;
                                            s_captureSkipTick = true; // skip scan this frame
                                            bindMatched = true;
                                            updateConfigKeyLabels(); // show "[Press key...]" in yellow
                                            VLOG(STR("[MoriaCppMod] [CFG] Capturing key for bind {}\n"), b);
                                            break;
                                        }
                                        currentY += rowHeight;
                                    }
                                    // Modifier key row is after all bindings Ã¢â‚¬â€ only check if no binding was matched
                                    if (!bindMatched && y >= currentY && y < currentY + rowHeight)
                                    {
                                        // Cycle modifier: CTRL Ã¢â€ â€™ SHIFT Ã¢â€ â€™ ALT Ã¢â€ â€™ Right ALT Ã¢â€ â€™ CTRL
                                        if (s_modifierVK == VK_CONTROL)
                                            s_modifierVK = VK_SHIFT;
                                        else if (s_modifierVK == VK_SHIFT)
                                            s_modifierVK = VK_MENU;
                                        else if (s_modifierVK == VK_MENU)
                                            s_modifierVK = VK_RMENU; // Right ALT (0xA5)
                                        else
                                            s_modifierVK = VK_CONTROL;
                                        saveConfig();
                                        updateConfigKeyLabels();
                                        VLOG(STR("[MoriaCppMod] [CFG] Modifier key cycled to VK 0x{:02X}\n"), (int)s_modifierVK);
                                    }
                                }
                            }
                        }
                        // Tab 2: Danger icon click to delete removal entry
                        if (m_cfgActiveTab == 2)
                        {
                            // Danger icons are in the left 60px of the content area
                            int iconX0 = static_cast<int>(wLeft + 40.0f * s2p), iconX1 = static_cast<int>(iconX0 + 70.0f * s2p);
                            int entryStart = static_cast<int>(wTop + 230.0f * s2p);
                            int entryHeight = static_cast<int>(70.0f * s2p);
                            if (cursor.x >= iconX0 && cursor.x <= iconX1 && cursor.y >= entryStart)
                            {
                                int entryIdx = (cursor.y - entryStart) / entryHeight;
                                int count = s_config.removalCount.load();
                                if (entryIdx >= 0 && entryIdx < count)
                                {
                                    s_config.pendingRemoveIndex = entryIdx;
                                    VLOG(STR("[MoriaCppMod] [CFG] Delete removal entry {} via mouse click\n"), entryIdx);
                                }
                            }
                        }
                    }
                }
                s_lastLMB = lmbDown;

                // Key capture for rebinding (Tab 1): 0x8000 edge detection (transition bit unreliable)
                static bool s_captureKeyPrev[256]{};
                if (s_capturingBind >= 0 && s_capturingBind < BIND_COUNT)
                {
                    // Skip one frame after entering capture mode Ã¢â‚¬â€ just snapshot current key state
                    if (s_captureSkipTick)
                    {
                        s_captureSkipTick = false;
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                            s_captureKeyPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    }
                    else
                    {
                        // Scan for key press (rising edge: not down last frame, down now)
                        for (int vk = 0x08; vk <= 0xFE; vk++)
                        {
                            // Skip modifier keys and mouse buttons
                            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                                vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
                                vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU)
                                continue;
                            bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                            bool wasDown = s_captureKeyPrev[vk];
                            s_captureKeyPrev[vk] = nowDown;
                            if (!nowDown || wasDown) continue; // only rising edge
                            // ESC cancels capture
                            if (vk == VK_ESCAPE)
                            {
                                s_capturingBind = -1;
                                updateConfigKeyLabels();
                                VLOG(STR("[MoriaCppMod] [CFG] Key capture cancelled\n"));
                                break;
                            }
                            // Capture this key
                            int idx = s_capturingBind.load();
                            if (idx >= 0 && idx < BIND_COUNT)
                            {
                                s_bindings[idx].key = static_cast<uint8_t>(vk);
                                s_capturingBind = -1;
                                saveConfig();
                                updateConfigKeyLabels();
                                s_overlay.needsUpdate = true;
                                s_pendingKeyLabelRefresh = true;
                                VLOG(
                                    STR("[MoriaCppMod] [CFG] Key bound: bind {} = VK 0x{:02X} ({})\n"),
                                    idx, vk, vk >= 0x70 && vk <= 0x87 ? STR("F-key") :
                                             vk >= 0x60 && vk <= 0x69 ? STR("Numpad") : STR("other"));
                            }
                            break;
                        }
                    }
                }
                else
                {
                    // ESC to close config (only when not capturing)
                    static bool s_lastCfgEsc = false;
                    bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                    if (escDown && !s_lastCfgEsc)
                        toggleConfig();
                    s_lastCfgEsc = escDown;
                }

                // Refresh key labels if capturing state changed (show yellow "Press key..." text)
                {
                    static int s_lastCapturing = -1;
                    int curCapturing = s_capturingBind.load();
                    if (curCapturing != s_lastCapturing)
                    {
                        updateConfigKeyLabels();
                        s_lastCapturing = curCapturing;
                    }
                }

                // Tab 2: refresh removal list if count changed
                if (m_cfgActiveTab == 2)
                {
                    int curCount = s_config.removalCount.load();
                    if (curCount != m_cfgLastRemovalCount)
                    {
                        rebuildRemovalList();
                    }
                }

                // T = toggle Free Build (Tab 0 only)
                if (m_cfgActiveTab == 0)
                {
                    static bool s_lastCfgT = false;
                    bool tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
                    if (tDown && !s_lastCfgT)
                    {
                        s_config.pendingToggleFreeBuild = true;
                        VLOG(STR("[MoriaCppMod] [CFG] Free Build toggle requested\n"));
                    }
                    s_lastCfgT = tDown;
                }

                // U = unlock all recipes (Tab 0 only)
                if (m_cfgActiveTab == 0)
                {
                    static bool s_lastCfgU = false;
                    bool uDown = (GetAsyncKeyState('U') & 0x8000) != 0;
                    if (uDown && !s_lastCfgU)
                    {
                        s_config.pendingUnlockAllRecipes = true;
                        VLOG(STR("[MoriaCppMod] [CFG] Unlock All Recipes requested\n"));
                    }
                    s_lastCfgU = uDown;
                }

                // M = cycle modifier key (Tab 1 only)
                if (m_cfgActiveTab == 1)
                {
                    static bool s_lastCfgM = false;
                    bool mDown = (GetAsyncKeyState('M') & 0x8000) != 0;
                    if (mDown && !s_lastCfgM)
                    {
                        s_modifierVK = nextModifier(s_modifierVK);
                        saveConfig();
                        updateConfigKeyLabels();
                    }
                    s_lastCfgM = mDown;
                }
            }

            // Ã¢â€â‚¬Ã¢â€â‚¬ Config window: consume pending cheat toggle requests Ã¢â€â‚¬Ã¢â€â‚¬
            {
                static int s_freeBuildRetries = 0;
                static int s_freeBuildThrottle = 0;
                constexpr int MAX_RETRIES = 12;
                constexpr int RETRY_INTERVAL = 10; // check every 10 frames

                if (s_config.pendingToggleFreeBuild)
                {
                    if (++s_freeBuildThrottle >= RETRY_INTERVAL)
                    {
                        s_freeBuildThrottle = 0;
                        if (callDebugFunc(STR("BP_DebugMenu_CraftingAndConstruction_C"), STR("Toggle Free Construction")))
                        {
                            s_config.pendingToggleFreeBuild = false;
                            syncDebugToggleState(); // read actual state instead of blind flip
                            showDebugMenuState();
                            if (m_cfgVisible) { updateConfigFreeBuild(); updateConfigNoCollision(); }
                            s_freeBuildRetries = 0;
                        }
                        else if (++s_freeBuildRetries > MAX_RETRIES)
                        {
                            s_config.pendingToggleFreeBuild = false;
                            s_freeBuildRetries = 0;
                            showOnScreen(Loc::get("msg.free_build_failed"), 3.0f, 1.0f, 0.3f, 0.3f);
                            VLOG(STR("[MoriaCppMod] Toggle Free Construction FAILED after {} retries\n"), MAX_RETRIES);
                        }
                    }
                }
            }
            if (s_config.pendingUnlockAllRecipes)
            {
                if (callDebugFunc(STR("BP_DebugMenu_Recipes_C"), STR("All Recipes")))
                    showOnScreen(Loc::get("msg.all_recipes_unlocked"), 5.0f, 0.0f, 1.0f, 0.0f);
                else
                    showOnScreen(Loc::get("msg.recipe_actor_not_found"), 3.0f, 1.0f, 0.3f, 0.3f);
                s_config.pendingUnlockAllRecipes = false;
            }


            // Config UI: consume pending removal deletion from Building Options tab
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
                        VLOG(STR("[MoriaCppMod] Config UI: removed entry {} ({})\n"),
                                                        removeIdx,
                                                        std::wstring(toRemove.friendlyName));
                    }
                    s_config.pendingRemoveIndex = -1;
                }
            }

            // ── Eager handle resolution state machine ──

            if (m_handleResolvePhase == HandleResolvePhase::Priming)
            {
                ULONGLONG now = GetTickCount64();
                ULONGLONG sinceLast = now - m_handleResolveRetryTime;
                ULONGLONG elapsed = now - m_handleResolveStartTime;

                if (elapsed > 5000)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] Timeout in Priming ({}ms), aborting\n"), elapsed);
                    if (isBuildTabShowing()) hideBuildTab();
                    m_handleResolvePhase = HandleResolvePhase::Done;
                }
                else if (m_buildTabAfterShowFired)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] OnAfterShow fired, primed in {}ms\n"), elapsed);
                    m_buildTabAfterShowFired = false;
                    m_buildMenuPrimed = true;
                    m_handleResolveSlotIdx = 0;
                    m_handleResolvePhase = HandleResolvePhase::Resolving;
                }
                else if (isBuildTabShowing() && sinceLast > 2000)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] 2s fallback, entering Resolving\n"));
                    m_buildMenuPrimed = true;
                    m_handleResolveSlotIdx = 0;
                    m_handleResolvePhase = HandleResolvePhase::Resolving;
                }
                else if (!isBuildTabShowing() && sinceLast > 500) // originally 417ms
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] Sending B key to prime build menu\n"));
                    m_buildTabAfterShowFired = false;
                    keybd_event(0x42, 0, 0, 0);
                    keybd_event(0x42, 0, KEYEVENTF_KEYUP, 0);
                    m_handleResolveRetryTime = now;
                }
            }
            else if (m_handleResolvePhase == HandleResolvePhase::Resolving)
            {
                // Resolve one slot per frame (Slate invalidation safety)
                UObject* buildHUD = getCachedBuildHUD();
                if (!buildHUD)
                {
                    QBLOG(STR("[MoriaCppMod] [HandleResolve] No BuildHUD found, aborting\n"));
                    hideBuildTab();
                    m_handleResolvePhase = HandleResolvePhase::Done;
                }
                else
                {
                    while (m_handleResolveSlotIdx < QUICK_BUILD_SLOTS)
                    {
                        auto& slot = m_recipeSlots[m_handleResolveSlotIdx];
                        if (slot.used && !slot.hasHandle && !slot.rowName.empty())
                            break;
                        m_handleResolveSlotIdx++;
                    }

                    if (m_handleResolveSlotIdx >= QUICK_BUILD_SLOTS)
                    {
                        hideBuildTab();
                        m_handleResolvePhase = HandleResolvePhase::Done;
                        ULONGLONG totalMs = GetTickCount64() - m_handleResolveStartTime;
                        QBLOG(STR("[MoriaCppMod] [HandleResolve] Complete in {}ms\n"), totalMs);
                    }
                    else
                    {
                        int i = m_handleResolveSlotIdx;
                        RC::Unreal::FName fn(m_recipeSlots[i].rowName.c_str(), RC::Unreal::FNAME_Add);
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
                    }
                }
            }

            // Build menu close: refresh ActionBar, invalidate cached pointers
            if (m_buildMenuWasOpen && !isBuildTabShowing())
            {
                m_buildMenuWasOpen = false;
                // Clear cached pointers Ã¢â‚¬â€ Build_Tab widget may be getting destroyed
                m_cachedBuildComp = nullptr;
                m_cachedBuildTab = nullptr;
                m_cachedBuildHUD = nullptr;
                m_fnIsVisible = nullptr;
                refreshActionBar();
            }

            // Placement state machines (quickbuild + target-build)
            placementTick();

            // Toolbar swap state machine: one item per tick
            swapToolbarTick();


            if (!m_replayActive) return;
            m_frameCounter++;

            // Detect world switch: if character was loaded but disappears, reset for new world
            if (m_characterLoaded && intervalElapsed(m_lastWorldCheck, 1000))
            {
                std::vector<UObject*> dwarves;
                UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
                if (dwarves.empty())
                {
                    VLOG(STR("[MoriaCppMod] Character lost Ã¢â‚¬â€ world unloading, resetting replay state\n"));
                    m_characterLoaded = false;
                    m_characterHidden = false;       // reset hide toggle for new world
                    m_flyMode = false;               // reset fly toggle for new world
                    m_snapEnabled = true;            // reset snap toggle for new world
                    m_savedMaxSnapDistance = -1.0f;   // force re-capture on next use
                    m_buildMenuPrimed = false;       // force re-prime on next quickbuild
                    // Reset quickbuild SM (cached pointers stale)
                    m_cachedBuildComp = nullptr;
                    m_cachedBuildHUD = nullptr;
                    m_cachedBuildTab = nullptr;
                    m_fnIsVisible = nullptr;
                    m_bpShowMouseCursor = nullptr;
                    m_qbPhase = PlacePhase::Idle;
                    m_isTargetBuild = false;
                    m_handleResolvePhase = HandleResolvePhase::None; // re-resolve after next load
                    m_pendingQuickBuildSlot = -1;
                    m_hasLastCapture = false;           // bLock data stale in new world
                    m_hasLastHandle = false;            // recipe handle stale in new world
                    m_lastCapturedName.clear();
                    for (auto& slot : m_recipeSlots)    // invalidate session-only data
                    {
                        slot.hasBLockData = false;
                        slot.hasHandle = false;
                    }
                    s_overlay.visible = false; // hide overlay until character reloads
                    m_initialReplayDone = false;
                    m_processedComps.clear();
                    m_undoStack.clear();
                    m_stuckLogCount = 0;
                    m_lastRescanTime = 0;
                    m_lastStreamCheck = 0;
                    m_lastContainerScan = 0;
                    m_containerTimeoutLogged = false;
                    // BELIEVED DEAD CODE -- chat widget system
                    // m_chatWidget = nullptr;
                    // m_sysMessages = nullptr;
                    m_replay = {}; // stop any active replay
                    // Reset all applied flags so replay re-runs for new world
                    m_appliedRemovals.assign(m_appliedRemovals.size(), false);
                    // Clear swap state (handles stale on unload)
                    m_bodyInvHandle.clear();
                    m_bodyInvHandles.clear();
                    m_repairDone = false;
                    m_bagHandle.clear();
                    m_ihfCDO = nullptr;
                    m_dropItemMgr = nullptr;
                    // UMG builders bar destroyed with world
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
                    // Mod Controller toolbar destroyed with world
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
                    m_mcSlot8Overlay = nullptr;
                    m_mcSlot10Overlay = nullptr;
                    // Advanced Builder toolbar destroyed with world
                    m_abBarWidget = nullptr;
                    m_abKeyLabel = nullptr;
                    m_abStateImage = nullptr;
                    m_toolbarsVisible = false;
                    // Toolbar click/hover state reset
                    m_hoveredToolbar = -1;
                    m_hoveredSlot = -1;
                    m_lastClickLMB = false;
                    // Repositioning mode destroyed with world
                    m_repositionMode = false;
                    m_dragToolbar = -1;
                    m_repositionMsgWidget = nullptr;
                    m_repositionInfoBoxWidget = nullptr;
                    // Target Info + Info Box destroyed with world
                    m_targetInfoWidget = nullptr;
                    m_tiTitleLabel = nullptr;
                    m_tiClassLabel = nullptr;
                    m_tiNameLabel = nullptr;
                    m_tiDisplayLabel = nullptr;
                    m_tiPathLabel = nullptr;
                    m_tiBuildLabel = nullptr;
                    m_tiRecipeLabel = nullptr;
                    m_tiShowTick = 0;
                    // BELIEVED DEAD CODE -- InfoBox popup system
                    // m_infoBoxWidget = nullptr;
                    // m_ibTitleLabel = nullptr;
                    // m_ibMessageLabel = nullptr;
                    // m_ibShowTick = 0;
                    // Stability audit highlights cleared with world
                    clearStabilityHighlights();
                    // Config Menu destroyed with world
                    m_configWidget = nullptr;
                    m_cfgTabLabels[0] = m_cfgTabLabels[1] = m_cfgTabLabels[2] = nullptr;
                    m_cfgTabContent[0] = m_cfgTabContent[1] = m_cfgTabContent[2] = nullptr;
                    m_cfgTabImages[0] = m_cfgTabImages[1] = m_cfgTabImages[2] = nullptr;
                    m_cfgTabActiveTexture = nullptr;
                    m_cfgTabInactiveTexture = nullptr;
                    m_cfgVignetteImage = nullptr;
                    m_cfgScrollBoxes[0] = m_cfgScrollBoxes[1] = m_cfgScrollBoxes[2] = nullptr;
                    m_cfgActiveTab = 0;
                    m_cfgVisible = false;
                    m_cfgFreeBuildLabel = nullptr;
                    m_cfgFreeBuildCheckImg = nullptr;
                    m_cfgUnlockBtnImg = nullptr;
                    for (int i = 0; i < BIND_COUNT; i++) m_cfgKeyValueLabels[i] = nullptr;
                    for (int i = 0; i < BIND_COUNT; i++) m_cfgKeyBoxLabels[i] = nullptr;
                    m_cfgModifierLabel = nullptr;
                    m_cfgModBoxLabel = nullptr;
                    m_cfgRemovalHeader = nullptr;
                    m_cfgRemovalVBox = nullptr;
                    m_cfgLastRemovalCount = -1;
                    m_swap = {};
                    m_activeToolbar = 0;
                    s_overlay.activeToolbar = 0;
                    // Reset cheat toggles (debug actors destroyed)
                    s_config.freeBuild = false;
                    s_config.pendingToggleFreeBuild = false;
                    s_config.pendingUnlockAllRecipes = false;
                    VLOG(STR("[MoriaCppMod] [Swap] Cleared all container handles, swap state, and cheat toggles\n"));
                }
            }


            if (!m_characterLoaded)
            {
                if (intervalElapsed(m_lastCharPoll, 500))
                { // check every 0.5s
                    std::vector<UObject*> dwarves;
                    UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
                    if (!dwarves.empty())
                    {
                        m_characterLoaded = true;
                        m_charLoadTime = GetTickCount64();
                        VLOG(STR("[MoriaCppMod] Character loaded Ã¢â‚¬â€ waiting 15s before replay\n"));
                    }
                }
                return; // don't do anything until character exists
            }

            ULONGLONG msSinceChar = GetTickCount64() - m_charLoadTime;

            // Auto-scan containers: 2s retry, 5s-65s window
            if (m_bodyInvHandle.empty() && msSinceChar > 5000 && msSinceChar < 65000 && intervalElapsed(m_lastContainerScan, 2000))
            {
                VLOG(STR("[MoriaCppMod] [Swap] Container scan attempt (frame {}). bodyInvHandle.empty={} handles.size={}\n"),
                                                m_frameCounter,
                                                m_bodyInvHandle.empty(),
                                                m_bodyInvHandles.size());
                UObject* playerChar = nullptr;
                {
                    std::vector<UObject*> actors;
                    UObjectGlobals::FindAllOf(STR("Character"), actors);
                    for (auto* a : actors)
                    {
                        if (a && safeClassName(a).find(STR("Dwarf")) != std::wstring::npos)
                        {
                            playerChar = a;
                            break;
                        }
                    }
                }
                if (playerChar)
                {
                    auto* invComp = findPlayerInventoryComponent(playerChar);
                    if (invComp)
                    {
                        VLOG(STR("[MoriaCppMod] === Auto-scan containers (retry) ===\n"));
                        discoverBagHandle(invComp);
                        if (!m_bodyInvHandle.empty())
                        {
                            showOnScreen(Loc::get("msg.containers_discovered"), 3.0f, 0.0f, 1.0f, 0.0f);
                        }
                    }
                }
            }

            // 65s container scan timeout (one-shot log)
            if (m_bodyInvHandle.empty() && msSinceChar >= 65000 && !m_containerTimeoutLogged)
            {
                m_containerTimeoutLogged = true;
                VLOG(STR("[MoriaCppMod] [Swap] Container discovery FAILED after 65s Ã¢â‚¬â€ toolbar swap unavailable this session\n"));
                showOnScreen(Loc::get("msg.container_discovery_failed"), 5.0f, 1.0f, 0.3f, 0.0f);
            }

            // Initial replay 15s after char load (streaming settle time)
            if (!m_initialReplayDone && msSinceChar >= 15000)
            {
                m_initialReplayDone = true;
                if (!m_savedRemovals.empty() || !m_typeRemovals.empty())
                {
                    VLOG(STR("[MoriaCppMod] Starting initial replay (15s after char load)...\n"));
                    startReplay();
                }

                // Sync debug toggle state from the actual debug menu actor
                syncDebugToggleState();
            }

            // Throttled replay batch
            if (m_replay.active)
            {
                processReplayBatch();
            }

            // Check for newly-streamed components (3s interval)
            if (m_initialReplayDone && !m_replay.active && intervalElapsed(m_lastStreamCheck, 3000))
            {
                checkForNewComponents();
            }

            // Periodic full rescan (60s, only when pending)
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
    };
} // namespace MoriaMods

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
