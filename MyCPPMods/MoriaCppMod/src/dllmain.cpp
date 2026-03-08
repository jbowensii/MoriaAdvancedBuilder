// Ã¢â€¢â€Ã¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢ÂÃ¢â€¢â€”
// Ã¢â€¢â€˜  MoriaCppMod v3.1 Ã¢â‚¬â€ Advanced Builder & HISM Removal for Return to Moria   Ã¢â€¢â€˜
// Ã¢â€¢â€˜                                                                            Ã¢â€¢â€˜
// Ã¢â€¢â€˜  A UE4SS C++ mod for Return to Moria (UE4.27) providing:                  Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - HISM instance hiding with persistence across sessions/worlds          Ã¢â€¢â€˜
// Ã¢â€¢â€˜    - Quick-build hotbar (F1-F8) with recipe capture & icon overlay         Ã¢â€¢â€˜
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
        std::vector<bool> m_appliedRemovals; // parallel to m_savedRemovals: true = already removed

        // Real-time interval tracking
        ULONGLONG m_lastWorldCheck{0};     // world-unload detection (~1s)
        ULONGLONG m_lastCharPoll{0};       // character detection (~0.5s)
        ULONGLONG m_lastStreamCheck{0};    // new component streaming (~3s)
        ULONGLONG m_lastRescanTime{0};     // full rescan (~60s)
        ULONGLONG m_charLoadTime{0};       // timestamp when character first detected

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

        #include "moria_inventory.inl"   // 6E: Inventory utilities

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
        // Event-driven: Idle | CancelGhost (ESC→OnAfterHide) | WaitingForShow (→OnAfterShow) | SelectRecipeWalk
        enum class PlacePhase { Idle, CancelGhost, WaitingForShow, SelectRecipeWalk };
        // Result of selectRecipeOnBuildTab Ã¢â‚¬â€ distinguishes "still loading" from "genuinely missing"
        enum class SelectResult { Found, Loading, NotFound };
        int m_pendingQuickBuildSlot{-1};     // which F1-F8 slot is pending (-1 = none)
        PlacePhase m_qbPhase{PlacePhase::Idle};    // quickbuild phase
        ULONGLONG m_qbStartTime{0};          // timestamp when SM entered non-idle (watchdog only)

        // Eager handle resolution SM (runs once at toolbar creation)
        enum class HandleResolvePhase { None, Priming, Resolving, Done };
        HandleResolvePhase m_handleResolvePhase{HandleResolvePhase::None};
        ULONGLONG m_handleResolveStartTime{0};  // timestamp when resolve SM started
        int m_handleResolveSlotIdx{0};          // which slot we're resolving next in Resolving phase
        ULONGLONG m_lastDirectSelectTime{0};    // cooldown: last SelectRecipe via DIRECT path (ms)
        ULONGLONG m_lastShowHideTime{0};        // cooldown: last Show()/Hide() on build tab (ms)
        ULONGLONG m_lastQBSelectTime{0};        // cooldown: last blockSelectedEvent completion (ms)

        // Cached widget/component pointers (invalidated on world unload)
        UObject* m_cachedBuildComp{nullptr}; // UMorBuildingComponent on player character
        UObject* m_cachedBuildHUD{nullptr};  // UI_WBP_BuildHUDv2_C (UBuildOverlayWidget)
        UObject* m_cachedBuildTab{nullptr};  // UI_WBP_Build_Tab_C

        // Target-to-build (Shift+F10)
        std::wstring m_targetBuildName;      // display name from last F10 target
        std::wstring m_targetBuildRecipeRef; // class name sans BP_ prefix (for bLock matching)
        std::wstring m_targetBuildRowName;   // DT_Constructions row name (also key for DT_ConstructionRecipes)
        bool m_lastTargetBuildable{false};   // was the last target buildable?
        bool m_isTargetBuild{false};          // true when SM is running a target-build (vs F-key)
        bool m_buildMenuWasOpen{false};      // tracks build menu open/close for ActionBar refresh
        bool m_deferHideAndRefresh{false};   // deferred: hide build tab + refresh action bar next frame

        bool m_showHotbar{true}; // Win32 overlay bar visibility

        // Character rename (ALT+Ins experimental feature)
        std::atomic<bool> m_pendingCharNameReady{false};
        std::mutex m_charNameMutex;
        std::wstring m_pendingCharName;

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
        // Settings panel (F12 toggle)
        UObject* m_fontTestWidget{nullptr};
        bool m_ftVisible{false};
        UObject* m_ftTabImages[3]{};
        UObject* m_ftTabLabels[3]{};
        UObject* m_ftTabActiveTexture{nullptr};
        UObject* m_ftTabInactiveTexture{nullptr};
        int m_ftSelectedTab{0};
        UObject* m_ftScrollBox{nullptr};
        UObject* m_ftTabContent[3]{};
        UObject* m_ftKeyBoxLabels[BIND_COUNT]{};
        UObject* m_ftCheckImages[BIND_COUNT]{};
        UObject* m_ftModBoxLabel{nullptr};
        // Tab 1 (Game Options) widgets
        UObject* m_ftFreeBuildCheckImg{nullptr};
        UObject* m_ftFreeBuildLabel{nullptr};
        UObject* m_ftFreeBuildKeyLabel{nullptr};
        UObject* m_ftNoCollisionCheckImg{nullptr};
        UObject* m_ftNoCollisionLabel{nullptr};
        UObject* m_ftNoCollisionKeyLabel{nullptr};
        UObject* m_ftTrashCheckImg{nullptr};
        UObject* m_ftReplenishCheckImg{nullptr};
        UObject* m_ftRemoveAttrsCheckImg{nullptr};
        UObject* m_ftRenameWidget{nullptr};    // Rename character dialog (UserWidget)
        UObject* m_ftRenameInput{nullptr};     // EditableTextBox in rename dialog
        UObject* m_ftRenameConfirmLabel{nullptr}; // CONFIRM button label
        bool m_ftRenameVisible{false};
        UObject* m_trashDlgWidget{nullptr};  // Trash Item confirmation dialog (UserWidget)
        bool m_trashDlgVisible{false};
        ULONGLONG m_trashDlgOpenTick{0};     // GetTickCount64 when dialog opened (grace period)
        // Tab 2 (Environment) widgets
        UObject* m_ftRemovalVBox{nullptr};
        UObject* m_ftRemovalHeader{nullptr};
        int m_ftLastRemovalCount{-1};

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
        bool m_trashItemEnabled{true};             // INI: enable Trash Item feature
        bool m_replenishItemEnabled{true};         // INI: enable Replenish Item feature
        bool m_removeAttrsEnabled{true};           // INI: enable Remove Attributes feature
        bool m_buildMenuPrimed{false};
        bool m_buildTabAfterShowFired{false};      // set by OnAfterShow hook


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
        // Inventory item tracking (Replenish / Remove Attributes)
        UClass* m_lastPickedUpItemClass{nullptr};
        std::wstring m_lastPickedUpItemName;        // internal class name (e.g. "EQ_Pick_Iron_C")
        std::wstring m_lastPickedUpDisplayName;     // human-readable display name (e.g. "Iron Pickaxe")
        int32_t m_lastPickedUpCount{0};             // stack count from FItemInstance at capture time
        uint8_t m_lastItemHandle[20]{}; // FItemHandle (0x14 bytes): {ID, Payload, Owner}
        UObject* m_lastItemInvComp{nullptr}; // inventory component that owns the handle
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
        // Error Box — UMG popup for error/status messages (auto-fades after 5s)
        UObject* m_errorBoxWidget{nullptr};            // root UUserWidget
        UObject* m_ebMessageLabel{nullptr};            // message text
        ULONGLONG m_ebShowTick{0};                     // GetTickCount64() when shown; 0 = hidden
        static constexpr ULONGLONG ERROR_BOX_DURATION_MS = 5000;
        // (Old UMG Config Menu removed — replaced by Settings Panel)
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
            ModVersion = STR("3.1");
            ModName = STR("MoriaCppMod");
            ModAuthors = STR("johnb");
            ModDescription = STR("Advanced builder, HISM removal, quick-build hotbar, UMG config menu");
            // Init removal list CS before loadSaveFile can be called
            InitializeCriticalSection(&s_config.removalCS);
            s_config.removalCSInit = true;
            VLOG(STR("[MoriaCppMod] Loaded v3.1\n"));
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
                    if (m_ftVisible || !s_bindings[i].enabled) return;
                    quickBuildSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::SHIFT}, [this, i]() {
                    if (m_ftVisible || !s_bindings[i].enabled) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::CONTROL}, [this, i]() {
                    if (m_ftVisible || !s_bindings[i].enabled) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
                register_keydown_event(fkeys[i], {Input::ModifierKey::ALT}, [this, i]() {
                    if (m_ftVisible || !s_bindings[i].enabled) return;
                    if (isModifierDown()) assignRecipeSlot(i);
                });
            }

            // Hotbar overlay toggle: Numpad * (Multiply)
            register_keydown_event(Input::Key::MULTIPLY, [this]() {
                if (m_ftVisible) return;
                m_showHotbar = !m_showHotbar;
                s_overlay.visible = m_showHotbar;
                s_overlay.needsUpdate = true;
                showOnScreen(m_showHotbar ? Loc::get("msg.hotbar_overlay_on") : Loc::get("msg.hotbar_overlay_off"), 2.0f, 0.2f, 0.8f, 1.0f);
            });

            // Mod Controller toolbar toggle: Numpad 7
            register_keydown_event(Input::Key::NUM_SEVEN, [this]() { if (m_ftVisible) return; createModControllerBar(); });

            // (ALT+INS binding removed — F12 now opens Settings Panel)

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

                // OnAfterShow on Build_Tab: event-driven SM transition
                if (fn == STR("OnAfterShow"))
                {
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        s_instance->m_buildTabAfterShowFired = true;
                        s_instance->m_buildMenuPrimed = true;
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow fired on Build_Tab\n"));

                        // Event-driven: transition WaitingForShow -> SelectRecipeWalk
                        if (s_instance->m_qbPhase == PlacePhase::WaitingForShow)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterShow: transitioning to SelectRecipeWalk\n"));
                            s_instance->m_qbPhase = PlacePhase::SelectRecipeWalk;
                        }
                    }
                    return;
                }

                // OnAfterHide on BuildHUD or Build_Tab: event-driven ghost/cancel handling
                if (fn == STR("OnAfterHide"))
                {
                    std::wstring cls = safeClassName(context);
                    if (cls == STR("UI_WBP_BuildHUDv2_C") || cls == STR("UI_WBP_Build_Tab_C"))
                    {
                        QBLOG(STR("[MoriaCppMod] [Placement] OnAfterHide fired on {}\n"), cls);

                        // Event-driven: CancelGhost complete -> activate build mode
                        if (s_instance->m_qbPhase == PlacePhase::CancelGhost)
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] OnAfterHide: ghost cancelled, activating build mode\n"));
                            if (s_instance->activateBuildMode())
                                s_instance->m_qbPhase = PlacePhase::WaitingForShow;
                            // else: safety timeout will catch it
                        }
                        else
                        {
                            s_instance->onGhostDisappeared();
                        }
                    }
                    return;
                }

                // Capture last moved item from ServerMoveItem / MoveSwapItem on player inventory.
                // User moves an item in inventory UI → we capture its class for Replenish.
                // ServerMoveItem(FItemHandle Item, FItemHandle Dest, EAddItem) — first param is source item.
                if (fn == STR("ServerMoveItem") || fn == STR("MoveSwapItem") || fn == STR("BroadcastToContainers_OnChanged"))
                {
                    if (parms)
                    {
                        std::wstring cls = safeClassName(context);
                        if (cls == STR("MorInventoryComponent"))
                            s_instance->captureLastChangedItem(context, parms);
                    }
                    if (fn == STR("BroadcastToContainers_OnChanged")) return;
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
                    STR("[MoriaCppMod] v3.1: F1-F8=build | F9=rotate | F12=config | MC toolbar + AB bar\n"));
        }

        // Per-frame tick: state machines, replay, quickbuild, icons, config, rescans.
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
                            m_buildTabAfterShowFired = false;
                            activateBuildMode();
                        }
                        else
                        {
                            m_handleResolvePhase = HandleResolvePhase::Done;
                        }
                    }
                }
                if (m_characterLoaded && !m_targetInfoWidget)
                    createTargetInfoWidget();
                if (m_characterLoaded && !m_errorBoxWidget)
                    createErrorBox();
                // (Old config widget pre-creation removed)
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
                if (m_ftVisible) updateFontTestKeyLabels();
            }

            // Pick up pending character rename from Win32 dialog thread
            if (m_pendingCharNameReady.exchange(false))
                applyPendingCharacterName();

            // Rename dialog: Enter = confirm, Escape = cancel, mouse clicks on buttons
            if (m_ftRenameVisible)
            {
                if (GetAsyncKeyState(VK_RETURN) & 1)
                    confirmRenameDialog();
                else if (GetAsyncKeyState(VK_ESCAPE) & 1)
                    hideRenameDialog();

                // Mouse click on CANCEL / CONFIRM buttons
                static bool s_renameLMBPrev = false;
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmb && !s_renameLMBPrev)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;
                        // Dialog is 700x220 Slate units, centered on viewport
                        float cx = viewW / 2.0f;
                        float cy = viewH / 2.0f;
                        float dlgW = 700.0f * s2p, dlgH = 220.0f * s2p;
                        float dlgLeft = cx - dlgW / 2.0f;
                        float dlgTop  = cy - dlgH / 2.0f;
                        // Button row near bottom, padded 15 Slate units from bottom
                        // CANCEL: left, CONFIRM: right, each 250x55 Slate units, 30px gap between
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

            // Trash Item dialog: Enter = delete, Escape = cancel, mouse clicks on buttons
            if (m_trashDlgVisible)
            {
                // Grace period: ignore all input for 500ms after opening (prevents DEL key-hold from closing)
                if (GetTickCount64() - m_trashDlgOpenTick < 500)
                {
                    // Consume key states so they don't trigger on first valid frame
                    GetAsyncKeyState(VK_RETURN);
                    GetAsyncKeyState(VK_ESCAPE);
                }
                else if (GetAsyncKeyState(VK_RETURN) & 1)
                    confirmTrashItem();
                else if (GetAsyncKeyState(VK_ESCAPE) & 1)
                    hideTrashDialog();

                // Mouse click on CANCEL / DELETE buttons
                static bool s_trashLMBPrev = false;
                bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmb && !s_trashLMBPrev)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;
                        // Dialog is 500x180 Slate units, centered on viewport
                        float cx = viewW / 2.0f;
                        float cy = viewH / 2.0f;
                        float dlgW = 500.0f * s2p, dlgH = 180.0f * s2p;
                        float dlgLeft = cx - dlgW / 2.0f;
                        float dlgTop  = cy - dlgH / 2.0f;
                        // Button row near bottom, padded 15 Slate units from bottom
                        // CANCEL: left (red), DELETE: right (green), each 200x50 Slate units
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

            // Target Info auto-close (10 seconds)
            if (m_tiShowTick > 0 && (GetTickCount64() - m_tiShowTick) >= 10000)
                hideTargetInfo();

            // Error Box auto-close (5 seconds)
            if (m_ebShowTick > 0 && (GetTickCount64() - m_ebShowTick) >= ERROR_BOX_DURATION_MS)
                hideErrorBox();

            // Stability audit — auto-clear PointLights after timeout
            if (m_auditClearTime > 0 && GetTickCount64() >= m_auditClearTime)
                clearStabilityHighlights();


            // Config key (F12) opens Settings Panel
            {
                static bool s_lastCfgKey = false;
                uint8_t cfgVk = s_bindings[MC_BIND_BASE + 11].key;
                if (cfgVk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(cfgVk) & 0x8000) != 0;
                    if (nowDown && !s_lastCfgKey)
                        toggleFontTestPanel();
                    s_lastCfgKey = nowDown;
                }
            }

            // MC keybind polling; track state always, skip actions when config visible
            if (m_mcBarWidget)
            {
                static bool s_lastMcKey[MC_SLOTS]{};
                static bool s_mcDiagLogged = false;
                if (!s_mcDiagLogged)
                {
                    s_mcDiagLogged = true;
                    for (int d = 0; d < MC_SLOTS; d++)
                        VLOG(STR("[MoriaCppMod] [MC-DIAG] slot {} VK=0x{:02X} label='{}'\n"),
                             d, s_bindings[MC_BIND_BASE + d].key, s_bindings[MC_BIND_BASE + d].label);
                }
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
                if (vk != 0 && s_bindings[BIND_AB_OPEN].enabled)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastAbKey && !m_ftVisible)
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

            // Game Options key polling: Replenish Item (INS), Trash Item (DEL), Remove Attributes (END)
            {
                static bool s_lastReplenishKey = false;
                uint8_t vk = s_bindings[BIND_REPLENISH_ITEM].key;
                if (vk != 0)
                {
                    bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (nowDown && !s_lastReplenishKey && !m_ftVisible)
                    {
                        if (m_replenishItemEnabled) replenishLastItem();
                        else showOnScreen(L"Replenish Item is disabled (enable in F12 settings)", 2.0f, 1.0f, 0.4f, 0.4f);
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
                    if (nowDown && !s_lastRemoveAttrsKey && !m_ftVisible)
                    {
                        if (m_removeAttrsEnabled) removeItemAttributes();
                        else showOnScreen(L"Remove Attributes is disabled (enable in F12 settings)", 2.0f, 1.0f, 0.4f, 0.4f);
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
                    if (nowDown && !s_lastTrashKey && !m_ftVisible && !m_trashDlgVisible)
                    {
                        if (m_trashItemEnabled) showTrashDialog();
                        else showOnScreen(L"Trash Item is disabled (enable in F12 settings)", 2.0f, 1.0f, 0.4f, 0.4f);
                    }
                    s_lastTrashKey = nowDown;
                }
            }

            // Re-apply UI input mode after Alt-Tab
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

            // Keep cursor visible while settings panel is open (game resets bShowMouseCursor per-tick)
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

            // Toolbar click + hover detection (when cursor is visible and toolbars shown)
            if (m_toolbarsVisible && !m_repositionMode && !m_ftVisible)
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

            // (Old UMG Config interaction block removed — replaced by Settings Panel below)
            // Settings panel (F12) interaction
            if (m_ftVisible && m_fontTestWidget)
            {
                // ESC closes
                static bool s_lastFtEsc = false;
                bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_lastFtEsc && s_capturingBind < 0 && !m_ftRenameVisible)
                    toggleFontTestPanel();
                s_lastFtEsc = escDown;

                // Mouse click: tab switching + keybind button clicks (blocked while rename dialog is open)
                static bool s_lastFtLMB = false;
                static bool s_ftCaptureSkipTick = false;
                bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lmbDown && !s_lastFtLMB && !m_ftRenameVisible)
                {
                    int curX, curY, viewW, viewH;
                    if (m_screen.getCursorClientPixels(curX, curY, viewW, viewH))
                    {
                        float s2p = m_screen.viewportScale;
                        int wLeft = static_cast<int>(viewW / 2.0f - 720.0f * s2p);
                        int wTop  = static_cast<int>(viewH / 2.0f - 440.0f * s2p);

                        // Tab clicks: 3 tabs stacked vertically on left
                        int tabX0 = static_cast<int>(wLeft + 32.5f * s2p);
                        int tabX1 = static_cast<int>(tabX0 + 512.0f * s2p);
                        if (curX >= tabX0 && curX <= tabX1)
                        {
                            for (int t = 0; t < 3; t++)
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

                        // Key box + checkbox clicks (Tab 0 only)
                        if (m_ftSelectedTab == 0)
                        {
                            int kbX0 = static_cast<int>(wLeft + (1440.0f - 30.0f - 10.0f - 400.0f - 4.0f) * s2p);
                            int kbX1 = static_cast<int>(wLeft + (1440.0f - 30.0f - 10.0f - 4.0f) * s2p);
                            // Checkbox area: border starts ~547px from panel left, +10 padding, +4 slot padding, 80px wide
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
                                    m_ftScrollBox->ProcessEvent(getScrollFn, sp.data());
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
                                    const wchar_t* lastSec = nullptr;
                                    bool bindMatched = false;
                                    for (int b = 0; b < BIND_COUNT; b++)
                                    {
                                        if (wcscmp(s_bindings[b].label, L"Reserved") == 0) continue;
                                        if (!lastSec || wcscmp(lastSec, s_bindings[b].section) != 0)
                                        {
                                            lastSec = s_bindings[b].section;
                                            currentY += sectionHeight;
                                        }
                                        if (y >= currentY && y < currentY + rowHeight)
                                        {
                                            if (inCheckBox)
                                            {
                                                // Toggle enabled state
                                                s_bindings[b].enabled = !s_bindings[b].enabled;
                                                if (m_ftCheckImages[b])
                                                {
                                                    auto* visFn = m_ftCheckImages[b]->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = s_bindings[b].enabled ? 0 : 2; m_ftCheckImages[b]->ProcessEvent(visFn, vp); }
                                                }
                                                saveConfig();
                                                VLOG(STR("[MoriaCppMod] [Settings] Bind {} enabled={}\n"), b, s_bindings[b].enabled);
                                            }
                                            else
                                            {
                                                // Key box click — start key capture
                                                s_capturingBind = b;
                                                s_ftCaptureSkipTick = true;
                                                updateFontTestKeyLabels();
                                                // (old config updateConfigKeyLabels removed)
                                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for bind {}\n"), b);
                                            }
                                            bindMatched = true;
                                            break;
                                        }
                                        currentY += rowHeight;
                                    }
                                    if (!bindMatched && inKeyBox && y >= currentY && y < currentY + rowHeight)
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

                        // Tab 1 (Game Options): checkbox clicks + unlock button + keybind rows
                        if (m_ftSelectedTab == 1)
                        {
                            // Reuse same X coordinates as Tab 0 checkbox area
                            int cbX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f) * s2p);
                            int cbX1 = static_cast<int>(cbX0 + 80.0f * s2p);
                            int kbX0 = static_cast<int>(wLeft + (1440.0f - 30.0f - 10.0f - 400.0f - 4.0f) * s2p);
                            int kbX1 = static_cast<int>(wLeft + (1440.0f - 30.0f - 10.0f - 4.0f) * s2p);
                            int contentY = static_cast<int>(wTop + 40.0f * s2p);
                            int sectionH = static_cast<int>(80.0f * s2p);
                            int rowH = static_cast<int>(128.0f * s2p);

                            // Account for scroll offset (content is inside ScrollBox)
                            float scrollOff = 0.0f;
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int gsz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(gsz, 0);
                                    m_ftScrollBox->ProcessEvent(getScrollFn, sp.data());
                                    auto* pRV = findParam(getScrollFn, STR("ReturnValue"));
                                    if (pRV && pRV->GetOffset_Internal() + (int)sizeof(float) <= gsz)
                                        scrollOff = *reinterpret_cast<float*>(sp.data() + pRV->GetOffset_Internal());
                                }
                            }
                            // Convert cursor Y to content-space Y (accounting for scroll)
                            int y = curY - contentY + static_cast<int>(scrollOff * s2p);

                            // Row layout: section(80) + rows(128 each)
                            // Row 0 = section header, Row 1 = Free Build, Row 2 = No Collision, Row 3 = Unlock, Row 4 = Rename
                            // Row 5 = Trash Item, Row 6 = Replenish Item, Row 7 = Remove Attributes
                            int fbY0 = sectionH;
                            int ncY0 = fbY0 + rowH;
                            int ulY0 = ncY0 + rowH;
                            int rcY0 = ulY0 + rowH;
                            int trY0 = rcY0 + rowH;
                            int rpY0 = trY0 + rowH;
                            int raY0 = rpY0 + rowH;

                            bool inKeyBox = (curX >= kbX0 && curX <= kbX1);
                            bool inCheckBox = (curX >= cbX0 && curX <= cbX1);
                            bool inFullRow = (curX >= cbX0 && curX <= kbX1);

                            // Free Build checkbox click
                            if (inCheckBox && y >= fbY0 && y < fbY0 + rowH)
                            {
                                s_config.pendingToggleFreeBuild = true;
                                VLOG(STR("[MoriaCppMod] [Settings] Free Build toggle via checkbox\n"));
                            }
                            // No Collision checkbox click
                            else if (inCheckBox && y >= ncY0 && y < ncY0 + rowH)
                            {
                                m_noCollisionWhileFlying = !m_noCollisionWhileFlying;
                                saveConfig();
                                updateFtNoCollision();
                                VLOG(STR("[MoriaCppMod] [Settings] No Collision toggle: {}\n"), m_noCollisionWhileFlying ? 1 : 0);
                            }
                            // Unlock button click (key box area on row 3)
                            else if (inKeyBox && y >= ulY0 && y < ulY0 + rowH)
                            {
                                s_config.pendingUnlockAllRecipes = true;
                                VLOG(STR("[MoriaCppMod] [Settings] Unlock All Recipes via button\n"));
                            }
                            // Rename Character button click (entire row is clickable — no checkbox)
                            else if (inFullRow && y >= rcY0 && y < rcY0 + rowH)
                            {
                                showRenameDialog();
                                VLOG(STR("[MoriaCppMod] [Settings] Rename Character via button\n"));
                            }
                            // Trash Item checkbox click (row 5)
                            else if (inCheckBox && y >= trY0 && y < trY0 + rowH)
                            {
                                m_trashItemEnabled = !m_trashItemEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Trash Item toggle: {}\n"), m_trashItemEnabled ? 1 : 0);
                            }
                            // Replenish Item checkbox click (row 6)
                            else if (inCheckBox && y >= rpY0 && y < rpY0 + rowH)
                            {
                                m_replenishItemEnabled = !m_replenishItemEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Replenish Item toggle: {}\n"), m_replenishItemEnabled ? 1 : 0);
                            }
                            // Remove Attributes checkbox click (row 7)
                            else if (inCheckBox && y >= raY0 && y < raY0 + rowH)
                            {
                                m_removeAttrsEnabled = !m_removeAttrsEnabled;
                                saveConfig();
                                updateFtGameOptCheckboxes();
                                VLOG(STR("[MoriaCppMod] [Settings] Remove Attributes toggle: {}\n"), m_removeAttrsEnabled ? 1 : 0);
                            }
                            // Trash Item keybind click (key box area on row 5)
                            else if (inKeyBox && y >= trY0 && y < trY0 + rowH)
                            {
                                s_capturingBind = BIND_TRASH_ITEM;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Trash Item (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_TRASH_ITEM], keyName(s_bindings[BIND_TRASH_ITEM].key));
                            }
                            // Replenish Item keybind click (key box area on row 6)
                            else if (inKeyBox && y >= rpY0 && y < rpY0 + rowH)
                            {
                                s_capturingBind = BIND_REPLENISH_ITEM;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Replenish Item (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_REPLENISH_ITEM], keyName(s_bindings[BIND_REPLENISH_ITEM].key));
                            }
                            // Remove Attributes keybind click (key box area on row 7)
                            else if (inKeyBox && y >= raY0 && y < raY0 + rowH)
                            {
                                s_capturingBind = BIND_REMOVE_ATTRS;
                                s_ftCaptureSkipTick = true;
                                updateFontTestKeyLabels();
                                VLOG(STR("[MoriaCppMod] [Settings] Capturing key for Remove Attributes (label={} cur={})\n"), (void*)m_ftKeyBoxLabels[BIND_REMOVE_ATTRS], keyName(s_bindings[BIND_REMOVE_ATTRS].key));
                            }
                        }

                        // Tab 2 (Environment): danger icon click to delete
                        if (m_ftSelectedTab == 2)
                        {
                            // Danger icons at left of each row
                            int iconX0 = static_cast<int>(wLeft + (30.0f + 517.0f + 10.0f + 4.0f) * s2p);
                            int iconX1 = static_cast<int>(iconX0 + 70.0f * s2p);
                            // Header = first child, entries start after (~30px for header text)
                            int entryStart = static_cast<int>(wTop + 40.0f * s2p + 30.0f * s2p);
                            int entryH = static_cast<int>(72.0f * s2p);

                            float scrollOff = 0.0f;
                            if (m_ftScrollBox)
                            {
                                auto* getScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("GetScrollOffset"));
                                if (getScrollFn)
                                {
                                    int sz = getScrollFn->GetParmsSize();
                                    std::vector<uint8_t> sp(sz, 0);
                                    m_ftScrollBox->ProcessEvent(getScrollFn, sp.data());
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
                    }
                }
                s_lastFtLMB = lmbDown;

                // Key capture scan
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

            // Ã¢â€â‚¬Ã¢â€â‚¬ Config window: consume pending cheat toggle requests Ã¢â€â‚¬Ã¢â€â‚¬
            {
                // Tab 2 (Environment): auto-refresh removal list if count changed
                if (m_ftVisible && m_ftSelectedTab == 2)
                {
                    int curCount = s_config.removalCount.load();
                    if (curCount != m_ftLastRemovalCount)
                        rebuildFtRemovalList();
                }

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
                            if (m_ftVisible) { updateFtFreeBuild(); updateFtNoCollision(); updateFtGameOptCheckboxes(); }
                            s_freeBuildRetries = 0;
                        }
                        else if (++s_freeBuildRetries > MAX_RETRIES)
                        {
                            s_config.pendingToggleFreeBuild = false;
                            s_freeBuildRetries = 0;
                            showErrorBox(Loc::get("msg.free_build_failed"));
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
                    showErrorBox(Loc::get("msg.recipe_actor_not_found"));
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
                        if (m_ftVisible) rebuildFtRemovalList();
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
                // No retry — activateBuildMode() was called once at start, OnAfterShow handles the rest
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
                refreshActionBar();
            }

            // Deferred hide+refresh from DIRECT path (1-frame delay for Slate invalidation safety)
            if (m_deferHideAndRefresh)
            {
                m_deferHideAndRefresh = false;
                if (isBuildTabShowing()) hideBuildTab();
                refreshActionBar();
            }

            // Placement state machines (quickbuild + target-build)
            placementTick();

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
                    m_bpShowMouseCursor = nullptr;
                    m_lastPickedUpItemClass = nullptr;
                    m_lastPickedUpItemName.clear();
                    m_lastPickedUpDisplayName.clear();
                    m_lastPickedUpCount = 0;
                    std::memset(m_lastItemHandle, 0, 20);
                    m_lastItemInvComp = nullptr;
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
                    m_replay = {}; // stop any active replay
                    // Reset all applied flags so replay re-runs for new world
                    m_appliedRemovals.assign(m_appliedRemovals.size(), false);
                    m_deferHideAndRefresh = false;
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
                    m_ftFreeBuildCheckImg = nullptr;
                    m_ftFreeBuildLabel = nullptr;
                    m_ftFreeBuildKeyLabel = nullptr;
                    m_ftNoCollisionCheckImg = nullptr;
                    m_ftNoCollisionLabel = nullptr;
                    m_ftNoCollisionKeyLabel = nullptr;
                    m_ftRemovalVBox = nullptr;
                    m_ftRemovalHeader = nullptr;
                    m_ftLastRemovalCount = -1;
                    m_ftRenameWidget = nullptr;
                    m_ftRenameInput = nullptr;
                    m_ftRenameConfirmLabel = nullptr;
                    m_ftRenameVisible = false;
                    m_trashDlgWidget = nullptr;
                    m_trashDlgVisible = false;
                    m_trashDlgOpenTick = 0;
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
                    // Error Box destroyed with world
                    m_errorBoxWidget = nullptr;
                    m_ebMessageLabel = nullptr;
                    m_ebShowTick = 0;
                    // Stability audit highlights cleared with world
                    clearStabilityHighlights();
                    // (Old Config Menu cleanup removed — Settings Panel handles its own cleanup)
                    // Reset cheat toggles (debug actors destroyed)
                    s_config.freeBuild = false;
                    s_config.pendingToggleFreeBuild = false;
                    s_config.pendingUnlockAllRecipes = false;
                    VLOG(STR("[MoriaCppMod] Cleared cheat toggles\n"));
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
