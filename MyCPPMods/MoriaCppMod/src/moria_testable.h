// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_testable.h — Pure-logic functions extracted for unit testing        ║
// ║                                                                            ║
// ║  These functions depend only on the C++ standard library and Windows.h.    ║
// ║  They are included by both dllmain.cpp (production) and the test suite.    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once
#ifndef MORIA_TESTABLE_H
#define MORIA_TESTABLE_H

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace MoriaMods
{
    // ════════════════════════════════════════════════════════════════════════════
    // Data Structures (used by both production code and tests)
    // ════════════════════════════════════════════════════════════════════════════

    struct SavedRemoval
    {
        std::string meshName;
        float posX{0}, posY{0}, posZ{0};
    };

    struct RemovalEntry
    {
        bool isTypeRule{false};
        std::string meshName;
        float posX{0}, posY{0}, posZ{0};
        std::wstring friendlyName;
        std::wstring fullPathW;
        std::wstring coordsW;
    };

    // ════════════════════════════════════════════════════════════════════════════
    // Constants
    // ════════════════════════════════════════════════════════════════════════════

    static constexpr int BIND_COUNT = 17;
    static constexpr int OVERLAY_BUILD_SLOTS = 8;

    // ════════════════════════════════════════════════════════════════════════════
    // Localization String Table
    //   Flat JSON string table (en.json) with compiled English defaults.
    //   Loc::get("key") returns const wstring& for zero-copy UI text.
    // ════════════════════════════════════════════════════════════════════════════

    namespace Loc
    {
        static std::unordered_map<std::string, std::wstring> s_table;
        static const std::wstring s_empty;

        // UTF-8 string to wstring via Win32 MultiByteToWideChar
        static std::wstring utf8ToWide(const std::string& s)
        {
            if (s.empty()) return {};
            int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (len <= 0) return {};
            std::wstring w(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
            return w;
        }

        // Minimal flat JSON parser: reads { "key": "value", ... } with escape support.
        // No nesting, no arrays, no numbers — just string key-value pairs.
        static bool parseJsonFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            // Skip BOM if present
            size_t pos = 0;
            if (json.size() >= 3 && json[0] == '\xEF' && json[1] == '\xBB' && json[2] == '\xBF')
                pos = 3;

            // Find opening brace
            while (pos < json.size() && json[pos] != '{') pos++;
            if (pos >= json.size()) return false;
            pos++; // skip '{'

            auto skipWS = [&]() { while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++; };

            auto parseString = [&](std::string& out) -> bool {
                skipWS();
                if (pos >= json.size() || json[pos] != '"') return false;
                pos++; // skip opening quote
                out.clear();
                while (pos < json.size() && json[pos] != '"')
                {
                    if (json[pos] == '\\' && pos + 1 < json.size())
                    {
                        pos++;
                        switch (json[pos])
                        {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case '/': out += '/'; break;
                        case 'u': {
                            // \uXXXX — parse 4 hex digits, encode as UTF-8
                            if (pos + 4 < json.size())
                            {
                                unsigned cp = 0;
                                for (int i = 1; i <= 4; i++)
                                {
                                    char c = json[pos + i];
                                    cp <<= 4;
                                    if (c >= '0' && c <= '9') cp |= (c - '0');
                                    else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                                    else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                                }
                                pos += 4;
                                // Encode codepoint as UTF-8
                                if (cp < 0x80) out += static_cast<char>(cp);
                                else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                                else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                            }
                            break;
                        }
                        default: out += json[pos]; break;
                        }
                    }
                    else
                    {
                        out += json[pos];
                    }
                    pos++;
                }
                if (pos < json.size()) pos++; // skip closing quote
                return true;
            };

            int count = 0;
            while (pos < json.size())
            {
                skipWS();
                if (pos >= json.size() || json[pos] == '}') break;
                if (json[pos] == ',') { pos++; continue; }

                std::string key, value;
                if (!parseString(key)) break;
                skipWS();
                if (pos >= json.size() || json[pos] != ':') break;
                pos++; // skip ':'
                if (!parseString(value)) break;

                s_table[key] = utf8ToWide(value);
                count++;
            }
            return count > 0;
        }

        // Register all English default strings (compiled fallback)
        static void initDefaults()
        {
            // ── Keybind labels ──
            s_table["bind.quick_build_1"] = L"Quick Build 1";
            s_table["bind.quick_build_2"] = L"Quick Build 2";
            s_table["bind.quick_build_3"] = L"Quick Build 3";
            s_table["bind.quick_build_4"] = L"Quick Build 4";
            s_table["bind.quick_build_5"] = L"Quick Build 5";
            s_table["bind.quick_build_6"] = L"Quick Build 6";
            s_table["bind.quick_build_7"] = L"Quick Build 7";
            s_table["bind.quick_build_8"] = L"Quick Build 8";
            s_table["bind.rotation"] = L"Rotation";
            s_table["bind.target"] = L"Target";
            s_table["bind.toolbar_swap"] = L"Toolbar Swap";
            s_table["bind.mod_menu_4"] = L"Super Dwarf";
            s_table["bind.remove_target"] = L"Remove Target";
            s_table["bind.undo_last"] = L"Undo Last";
            s_table["bind.remove_all"] = L"Remove All";
            s_table["bind.configuration"] = L"Configuration";
            s_table["bind.ab_open"] = L"Advanced Builder Open";
            // ── Keybind section names ──
            s_table["bind.section_quick_building"] = L"Quick Building";
            s_table["bind.section_mod_controller"] = L"Mod Controller";
            s_table["bind.section_advanced_builder"] = L"Advanced Builder";
            // ── Modifier key names ──
            s_table["key.shift"] = L"SHIFT";
            s_table["key.ctrl"] = L"CTRL";
            s_table["key.alt"] = L"ALT";
            s_table["key.ralt"] = L"RALT";
            // ── Key display names (non-trivial only) ──
            s_table["key.num_multiply"] = L"Num*";
            s_table["key.num_add"] = L"Num+";
            s_table["key.num_separator"] = L"NumSep";
            s_table["key.num_subtract"] = L"Num-";
            s_table["key.num_decimal"] = L"Num.";
            s_table["key.num_divide"] = L"Num/";
            s_table["key.space"] = L"Space";
            s_table["key.tab"] = L"Tab";
            s_table["key.enter"] = L"Enter";
            s_table["key.insert"] = L"Ins";
            s_table["key.delete"] = L"Del";
            s_table["key.home"] = L"Home";
            s_table["key.end"] = L"End";
            s_table["key.page_up"] = L"PgUp";
            s_table["key.page_down"] = L"PgDn";
            // ── Config tabs ──
            s_table["tab.optional_mods"] = L"Optional Mods";
            s_table["tab.key_mapping"] = L"Key Mapping";
            s_table["tab.hide_environment"] = L"Hide Environment";
            // ── UI labels: config menu ──
            s_table["ui.config_title"] = L"Building Mod Configuration Menu";
            s_table["ui.cheat_toggles"] = L"Cheat Toggles";
            s_table["ui.free_build"] = L"  Free Build";
            s_table["ui.free_build_on"] = L"  Free Build  (ON)";
            s_table["ui.free_build_desc"] = L"  Build without materials";
            s_table["ui.unlock_all_recipes"] = L"Unlock All Recipes";
            s_table["ui.set_modifier_key"] = L"Set Modifier Key:  ";
            s_table["ui.set_modifier_key_short"] = L"Set Modifier Key";
            s_table["ui.press_key"] = L"Press key...";
            s_table["ui.key_separator"] = L":  ";
            s_table["ui.saved_removals_prefix"] = L"Saved Removals (";
            s_table["ui.saved_removals_suffix"] = L" entries)";
            s_table["ui.type_rule"] = L"TYPE RULE";
            // ── UI labels: target info ──
            s_table["ui.target_info_title"] = L"Target Info";
            s_table["ui.label_class"] = L"Class:";
            s_table["ui.label_name"] = L"Name:";
            s_table["ui.label_display"] = L"Display:";
            s_table["ui.label_path"] = L"Path:";
            s_table["ui.label_build"] = L"Build:";
            s_table["ui.label_recipe"] = L"Recipe:";
            s_table["ui.value_class_prefix"] = L"Class:    ";
            s_table["ui.value_name_prefix"] = L"Name:     ";
            s_table["ui.value_display_prefix"] = L"Display:  ";
            s_table["ui.value_path_prefix"] = L"Path:     ";
            s_table["ui.value_build_prefix"] = L"Build:    ";
            s_table["ui.value_recipe_prefix"] = L"Recipe:   ";
            s_table["ui.yes"] = L"Yes";
            s_table["ui.no"] = L"No";
            // ── UI labels: info box ──
            s_table["ui.info_title"] = L"Info";
            // ── Overlay text ──
            s_table["ovr.target"] = L"TGT";
            s_table["ovr.config"] = L"CFG";
            s_table["ovr.degree"] = L"\xB0";
            // ── On-screen messages ──
            s_table["msg.no_hit"] = L"[Inspect] No hit";
            s_table["msg.actor_dump_no_hit"] = L"[ActorDump] No hit";
            s_table["msg.not_in_build_mode"] = L"Not in build mode";
            s_table["msg.slot_cleared"] = L" cleared";
            s_table["msg.no_recipe_selected"] = L"No recipe selected! Click one in Build menu first.";
            s_table["msg.build_menu_not_found"] = L"Build menu not open or no widget found";
            s_table["msg.recipe_not_found"] = L"' not found in menu!";
            s_table["msg.no_buildable_target"] = L"No buildable target \x2014 aim at a building and press F10 first";
            s_table["msg.build_menu_timeout"] = L"Build menu didn't open (timeout)";
            s_table["msg.all_recipes_unlocked"] = L"ALL RECIPES UNLOCKED!";
            s_table["msg.recipe_actor_not_found"] = L"Recipe debug actor not found";
            s_table["msg.free_build_failed"] = L"Free Build toggle failed - debug actor not found";
            s_table["msg.hotbar_overlay_on"] = L"Hotbar overlay ON";
            s_table["msg.hotbar_overlay_off"] = L"Hotbar overlay OFF";
            s_table["msg.already_clearing"] = L"Already clearing hotbar...";
            s_table["msg.wait_swap"] = L"Wait for toolbar swap to finish";
            s_table["msg.wait_clear"] = L"Wait for hotbar clear to finish";
            s_table["msg.player_not_found"] = L"Player not found";
            s_table["msg.inventory_not_found"] = L"Inventory not found";
            s_table["msg.equip_bag"] = L"Equip a bag first!";
            s_table["msg.clearing_hotbar"] = L"Clearing hotbar...";
            s_table["msg.swap_in_progress"] = L"Swap already in progress...";
            s_table["msg.body_inv_not_found"] = L"BodyInventory not found!";
            s_table["msg.containers_discovered"] = L"Containers discovered!";
            s_table["msg.container_discovery_failed"] = L"Container discovery failed - swap unavailable";
            s_table["msg.debug_actor_not_found"] = L"Debug menu actor not found";
            s_table["msg.hud_not_found"] = L"MoriaHUD NOT FOUND";
            s_table["msg.icon_probe_done"] = L"Icon probe done (see UE4SS log)";
            s_table["msg.builders_bar_created"] = L"Builders bar created!";
            s_table["msg.mod_controller_created"] = L"Mod Controller created!";
            s_table["msg.umg_bar_removed"] = L"UMG bar removed";
            s_table["msg.mc_removed"] = L"Mod Controller removed";
            s_table["msg.char_hidden"] = L"Character hidden";
            s_table["msg.char_visible"] = L"Character visible";
            s_table["ovr.hide_char"] = L"HIDE";
            s_table["msg.fly_on"] = L"Fly mode ON";
            s_table["msg.fly_off"] = L"Fly mode OFF";
            // ── Save file headers ──
            s_table["save.removal_header"] = L"# MoriaCppMod removed instances";
            s_table["save.removal_format1"] = L"# meshName|posX|posY|posZ = single instance";
            s_table["save.removal_format2"] = L"# @meshName = remove ALL of this type";
            s_table["save.quickbuild_header"] = L"# MoriaCppMod quick-build slots (F1-F8)";
            s_table["save.quickbuild_format"] = L"# slot|displayName|textureName";
            s_table["save.keybind_header"] = L"# MoriaCppMod keybindings (index|VK_code)";
        }

        // Get localized string by key. Returns const ref — zero-copy at call sites.
        static const std::wstring& get(const char* key)
        {
            auto it = s_table.find(key);
            return (it != s_table.end()) ? it->second : s_empty;
        }

        // Clear the string table (for test isolation)
        static void clear()
        {
            s_table.clear();
        }
    } // namespace Loc

    // ════════════════════════════════════════════════════════════════════════════
    // String Helpers
    // ════════════════════════════════════════════════════════════════════════════

    // Insert newlines into a prefixed string so each line fits within maxLine chars.
    // Tries to break at word boundaries (space, underscore, slash, dash, backslash).
    static std::wstring wrapText(const std::wstring& prefix, const std::wstring& value, size_t maxLine = 70)
    {
        std::wstring full = prefix + value;
        if (full.size() <= maxLine) return full;
        std::wstring result;
        size_t lineStart = 0;
        while (lineStart < full.size())
        {
            if (!result.empty()) result += L'\n';
            size_t lineEnd = lineStart + maxLine;
            if (lineEnd >= full.size())
            {
                result += full.substr(lineStart);
                break;
            }
            // Try to break at a word boundary
            size_t breakAt = lineEnd;
            for (size_t j = lineEnd; j > lineStart + maxLine / 2; j--)
            {
                wchar_t c = full[j];
                if (c == L' ' || c == L'_' || c == L'/' || c == L'-' || c == L'\\')
                {
                    breakAt = j + 1;
                    break;
                }
            }
            result += full.substr(lineStart, breakAt - lineStart);
            lineStart = breakAt;
        }
        return result;
    }

    static std::wstring extractFriendlyName(const std::string& meshName)
    {
        auto dash = meshName.find('-');
        std::string shortName = (dash != std::string::npos) ? meshName.substr(0, dash) : meshName;
        return std::wstring(shortName.begin(), shortName.end());
    }

    // Strips numeric suffix from UE4 component name to get stable mesh ID.
    // e.g. "PWM_Quarry_2x2x2_A-..._2147476295" -> "PWM_Quarry_2x2x2_A-..."
    static std::string componentNameToMeshId(const std::wstring& name)
    {
        std::string narrow;
        narrow.reserve(name.size());
        for (wchar_t c : name)
            narrow.push_back(static_cast<char>(c));
        auto lastUnderscore = narrow.rfind('_');
        if (lastUnderscore != std::string::npos)
        {
            bool allDigits = true;
            for (size_t i = lastUnderscore + 1; i < narrow.size(); i++)
            {
                if (!std::isdigit(narrow[i]))
                {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && lastUnderscore > 0)
            {
                return narrow.substr(0, lastUnderscore);
            }
        }
        return narrow;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Key Name Helpers
    // ════════════════════════════════════════════════════════════════════════════

    static const wchar_t* modifierName(uint8_t vk)
    {
        switch (vk)
        {
        case VK_SHIFT:
            return Loc::get("key.shift").c_str();
        case VK_CONTROL:
            return Loc::get("key.ctrl").c_str();
        case VK_MENU:
            return Loc::get("key.alt").c_str();
        case VK_RMENU:
            return Loc::get("key.ralt").c_str();
        default:
            return Loc::get("key.shift").c_str();
        }
    }

    static uint8_t nextModifier(uint8_t vk)
    {
        switch (vk)
        {
        case VK_SHIFT:
            return VK_CONTROL;
        case VK_CONTROL:
            return VK_MENU;
        case VK_MENU:
            return VK_RMENU;
        case VK_RMENU:
            return VK_SHIFT;
        default:
            return VK_SHIFT;
        }
    }

    static std::wstring keyName(uint8_t vk)
    {
        if (vk >= 0x70 && vk <= 0x7B)
        { // F1-F12
            wchar_t buf[8];
            swprintf_s(buf, L"F%d", vk - 0x70 + 1);
            return buf;
        }
        if (vk >= 0x60 && vk <= 0x69)
        { // Numpad 0-9
            wchar_t buf[8];
            swprintf_s(buf, L"Num%d", vk - 0x60);
            return buf;
        }
        switch (vk)
        {
        case 0x6A:
            return Loc::get("key.num_multiply");
        case 0x6B:
            return Loc::get("key.num_add");
        case 0x6C:
            return Loc::get("key.num_separator");
        case 0x6D:
            return Loc::get("key.num_subtract");
        case 0x6E:
            return Loc::get("key.num_decimal");
        case 0x6F:
            return Loc::get("key.num_divide");
        case 0xDC:
            return L"\\";
        case 0xC0:
            return L"`";
        case 0xBA:
            return L";";
        case 0xBB:
            return L"=";
        case 0xBC:
            return L",";
        case 0xBD:
            return L"-";
        case 0xBE:
            return L".";
        case 0xBF:
            return L"/";
        case 0xDB:
            return L"[";
        case 0xDD:
            return L"]";
        case 0xDE:
            return L"'";
        case 0x20:
            return Loc::get("key.space");
        case 0x09:
            return Loc::get("key.tab");
        case 0x0D:
            return Loc::get("key.enter");
        case 0x2D:
            return Loc::get("key.insert");
        case 0x2E:
            return Loc::get("key.delete");
        case 0x24:
            return Loc::get("key.home");
        case 0x23:
            return Loc::get("key.end");
        case 0x21:
            return Loc::get("key.page_up");
        case 0x22:
            return Loc::get("key.page_down");
        default: {
            wchar_t buf[16];
            if (vk >= 0x30 && vk <= 0x39)
            {
                swprintf_s(buf, L"%c", (wchar_t)vk);
                return buf;
            }
            if (vk >= 0x41 && vk <= 0x5A)
            {
                swprintf_s(buf, L"%c", (wchar_t)vk);
                return buf;
            }
            swprintf_s(buf, L"0x%02X", vk);
            return buf;
        }
        }
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Reverse Key Name Mapping (INI support)
    // ════════════════════════════════════════════════════════════════════════════

    // Helper: case-insensitive wstring compare
    static bool wstrEqualCI(const std::wstring& a, const wchar_t* b)
    {
        std::wstring la = a, lb = b;
        for (auto& c : la)
            c = towlower(c);
        for (auto& c : lb)
            c = towlower(c);
        return la == lb;
    }

    // Reverse of keyName(): human-readable key name -> VK code
    static std::optional<uint8_t> nameToVK(const std::wstring& name)
    {
        if (name.empty()) return std::nullopt;

        // F-keys: F1-F24
        if ((name[0] == L'F' || name[0] == L'f') && name.size() >= 2 && name.size() <= 3)
        {
            bool allDigits = true;
            for (size_t i = 1; i < name.size(); i++)
                if (!iswdigit(name[i])) allDigits = false;
            if (allDigits)
            {
                std::string narrow;
                for (size_t i = 1; i < name.size(); i++) narrow += static_cast<char>(name[i]);
                int n = std::stoi(narrow);
                if (n >= 1 && n <= 24) return static_cast<uint8_t>(0x70 + n - 1);
            }
        }

        // Numpad digits: Num0-Num9
        if (name.size() == 4 && wstrEqualCI(name.substr(0, 3), L"Num") && iswdigit(name[3]))
        {
            return static_cast<uint8_t>(0x60 + (name[3] - L'0'));
        }

        // Numpad operators (hardcoded English — matches en.json Loc defaults)
        if (wstrEqualCI(name, L"Num*")) return 0x6A;
        if (wstrEqualCI(name, L"Num+")) return 0x6B;
        if (wstrEqualCI(name, L"NumSep")) return 0x6C;
        if (wstrEqualCI(name, L"Num-")) return 0x6D;
        if (wstrEqualCI(name, L"Num.")) return 0x6E;
        if (wstrEqualCI(name, L"Num/")) return 0x6F;

        // Symbol keys (single-char)
        if (name.size() == 1)
        {
            switch (name[0])
            {
            case L'\\': return 0xDC;
            case L'`': return 0xC0;
            case L';': return 0xBA;
            case L'=': return 0xBB;
            case L',': return 0xBC;
            case L'-': return 0xBD;
            case L'.': return 0xBE;
            case L'/': return 0xBF;
            case L'[': return 0xDB;
            case L']': return 0xDD;
            case L'\'': return 0xDE;
            default: break;
            }
            // Single digit 0-9
            if (name[0] >= L'0' && name[0] <= L'9')
                return static_cast<uint8_t>(name[0]);
            // Single letter A-Z (case-insensitive)
            wchar_t upper = towupper(name[0]);
            if (upper >= L'A' && upper <= L'Z')
                return static_cast<uint8_t>(upper);
        }

        // Special keys (hardcoded English — matches en.json Loc defaults)
        if (wstrEqualCI(name, L"Space")) return 0x20;
        if (wstrEqualCI(name, L"Tab")) return 0x09;
        if (wstrEqualCI(name, L"Enter")) return 0x0D;
        if (wstrEqualCI(name, L"Ins")) return 0x2D;
        if (wstrEqualCI(name, L"Del")) return 0x2E;
        if (wstrEqualCI(name, L"Home")) return 0x24;
        if (wstrEqualCI(name, L"End")) return 0x23;
        if (wstrEqualCI(name, L"PgUp")) return 0x21;
        if (wstrEqualCI(name, L"PgDn")) return 0x22;

        // Hex fallback: "0xFF" format
        if (name.size() == 4 && name[0] == L'0' && (name[1] == L'x' || name[1] == L'X'))
        {
            try
            {
                std::string hexStr;
                for (auto c : name) hexStr += static_cast<char>(c);
                unsigned long val = std::stoul(hexStr, nullptr, 16);
                if (val > 0 && val < 256) return static_cast<uint8_t>(val);
            }
            catch (...) {}
        }

        return std::nullopt;
    }

    // Reverse of modifierName(): modifier string -> VK code (case-insensitive)
    static std::optional<uint8_t> modifierNameToVK(const std::wstring& name)
    {
        if (wstrEqualCI(name, L"SHIFT")) return VK_SHIFT;
        if (wstrEqualCI(name, L"CTRL")) return VK_CONTROL;
        if (wstrEqualCI(name, L"ALT")) return VK_MENU;
        if (wstrEqualCI(name, L"RALT")) return VK_RMENU;
        return std::nullopt;
    }

    // Modifier VK code -> INI string (narrow, for file output)
    static std::string modifierToIniName(uint8_t vk)
    {
        switch (vk)
        {
        case VK_SHIFT: return "SHIFT";
        case VK_CONTROL: return "CTRL";
        case VK_MENU: return "ALT";
        case VK_RMENU: return "RALT";
        default: return "SHIFT";
        }
    }

    // ════════════════════════════════════════════════════════════════════════════
    // INI File Parsing
    // ════════════════════════════════════════════════════════════════════════════

    struct ParsedIniSection
    {
        std::string name;
    };
    struct ParsedIniKeyValue
    {
        std::string key;
        std::string value;
    };
    using ParsedIniLine = std::variant<std::monostate, ParsedIniSection, ParsedIniKeyValue>;

    // Helper: trim leading/trailing whitespace from a string
    static std::string trimStr(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // Parse one line of an INI file
    static ParsedIniLine parseIniLine(const std::string& line)
    {
        std::string trimmed = trimStr(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            return std::monostate{};

        // Section header: [Name]
        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            std::string name = trimStr(trimmed.substr(1, trimmed.size() - 2));
            if (!name.empty()) return ParsedIniSection{name};
            return std::monostate{};
        }

        // Key = Value
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) return std::monostate{};

        std::string key = trimStr(trimmed.substr(0, eq));
        std::string value = trimStr(trimmed.substr(eq + 1));

        // Strip inline comment (space+semicolon), but not a bare semicolon as value
        for (size_t i = 1; i < value.size(); i++)
        {
            if (value[i] == ';' && value[i - 1] == ' ')
            {
                value = trimStr(value.substr(0, i - 1));
                break;
            }
        }

        if (!key.empty()) return ParsedIniKeyValue{key, value};
        return std::monostate{};
    }

    // INI key name <-> bind index mapping
    static const char* bindIndexToIniKey(int idx)
    {
        static const char* keys[BIND_COUNT] = {
            "QuickBuild1",        // 0
            "QuickBuild2",        // 1
            "QuickBuild3",        // 2
            "QuickBuild4",        // 3
            "QuickBuild5",        // 4
            "QuickBuild6",        // 5
            "QuickBuild7",        // 6
            "QuickBuild8",        // 7
            "Rotation",           // 8
            "Target",             // 9
            "ToolbarSwap",        // 10
            "SuperDwarf",         // 11
            "RemoveTarget",       // 12
            "UndoLast",           // 13
            "RemoveAll",          // 14
            "Configuration",      // 15
            "AdvancedBuilderOpen" // 16
        };
        if (idx < 0 || idx >= BIND_COUNT) return nullptr;
        return keys[idx];
    }

    // Helper: case-insensitive narrow string compare
    static bool strEqualCI(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }

    static int iniKeyToBindIndex(const std::string& key)
    {
        for (int i = 0; i < BIND_COUNT; i++)
        {
            if (strEqualCI(key, bindIndexToIniKey(i))) return i;
        }
        return -1;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Memory Safety Helper
    // ════════════════════════════════════════════════════════════════════════════

    static bool isReadableMemory(const void* ptr, size_t size = 8)
    {
        if (!ptr) return false;
        auto checkPage = [](const void* p) -> bool {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
            if (mbi.State != MEM_COMMIT) return false;
            DWORD protect = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
            return (protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE);
        };
        if (!checkPage(ptr)) return false;
        // Also check the last byte of the range to catch cross-page boundary reads
        if (size > 1)
        {
            const void* end = static_cast<const uint8_t*>(ptr) + size - 1;
            if (!checkPage(end)) return false;
        }
        return true;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // File Format Parse Helpers
    //   Used by both production load methods and unit tests.
    //   Each returns std::nullopt for lines that should be skipped.
    // ════════════════════════════════════════════════════════════════════════════

    // Result types for parsed lines
    struct ParsedRemovalPosition
    {
        std::string meshName;
        float posX, posY, posZ;
    };

    struct ParsedRemovalTypeRule
    {
        std::string meshName;
    };

    using ParsedRemovalLine = std::variant<std::monostate, ParsedRemovalPosition, ParsedRemovalTypeRule>;

    // Parse a single line from removed_instances.txt
    // Returns monostate for skip (comment/empty), Position or TypeRule for valid data
    static ParsedRemovalLine parseRemovalLine(const std::string& line)
    {
        if (line.empty() || line[0] == '#') return std::monostate{};
        if (line[0] == '@')
        {
            return ParsedRemovalTypeRule{line.substr(1)};
        }
        std::istringstream ss(line);
        ParsedRemovalPosition result;
        std::string token;
        if (!std::getline(ss, result.meshName, '|')) return std::monostate{};
        if (!std::getline(ss, token, '|')) return std::monostate{};
        try
        {
            result.posX = std::stof(token);
            if (!std::getline(ss, token, '|')) return std::monostate{};
            result.posY = std::stof(token);
            if (!std::getline(ss, token, '|')) return std::monostate{};
            result.posZ = std::stof(token);
        }
        catch (...)
        {
            return std::monostate{};
        }
        return result;
    }

    // Result for parsed quickbuild slot line
    struct ParsedSlot
    {
        int slotIndex;
        std::string displayName;
        std::string textureName; // may be empty (backward compat)
    };

    struct ParsedRotation
    {
        int step;
    };

    using ParsedSlotLine = std::variant<std::monostate, ParsedSlot, ParsedRotation>;

    // Parse a single line from quickbuild_slots.txt
    static ParsedSlotLine parseSlotLine(const std::string& line)
    {
        if (line.empty() || line[0] == '#') return std::monostate{};
        auto sep1 = line.find('|');
        if (sep1 == std::string::npos) return std::monostate{};
        std::string key = line.substr(0, sep1);

        // Rotation step persistence
        if (key == "rotation")
        {
            try
            {
                int val = std::stoi(line.substr(sep1 + 1));
                if (val >= 0 && val <= 90) return ParsedRotation{val};
            }
            catch (...) {}
            return std::monostate{};
        }

        int slot;
        try
        {
            slot = std::stoi(key);
        }
        catch (...)
        {
            return std::monostate{};
        }
        if (slot < 0 || slot >= OVERLAY_BUILD_SLOTS) return std::monostate{};

        // Parse: displayName|textureName (textureName optional)
        auto sep2 = line.find('|', sep1 + 1);
        std::string name, tex;
        if (sep2 != std::string::npos)
        {
            name = line.substr(sep1 + 1, sep2 - sep1 - 1);
            tex = line.substr(sep2 + 1);
        }
        else
        {
            name = line.substr(sep1 + 1);
        }
        return ParsedSlot{slot, name, tex};
    }

    // Result for parsed keybind line
    struct ParsedKeybind
    {
        int bindIndex;
        uint8_t vkCode;
    };

    struct ParsedModifier
    {
        uint8_t vkCode;
    };

    using ParsedKeybindLine = std::variant<std::monostate, ParsedKeybind, ParsedModifier>;

    // Parse a single line from keybindings.txt
    static ParsedKeybindLine parseKeybindLine(const std::string& line)
    {
        if (line.empty() || line[0] == '#') return std::monostate{};

        // Check for modifier line: "mod|VK_code"
        if (line.size() > 4 && line.substr(0, 4) == "mod|")
        {
            try
            {
                int mvk = std::stoi(line.substr(4));
                if (mvk == VK_SHIFT || mvk == VK_CONTROL || mvk == VK_MENU || mvk == VK_RMENU)
                    return ParsedModifier{static_cast<uint8_t>(mvk)};
            }
            catch (...) {}
            return std::monostate{};
        }

        auto sep = line.find('|');
        if (sep == std::string::npos) return std::monostate{};
        int idx, vk;
        try
        {
            idx = std::stoi(line.substr(0, sep));
            vk = std::stoi(line.substr(sep + 1));
        }
        catch (...)
        {
            return std::monostate{};
        }
        if (idx >= 0 && idx < BIND_COUNT && vk > 0 && vk < 256)
            return ParsedKeybind{idx, static_cast<uint8_t>(vk)};
        return std::monostate{};
    }

} // namespace MoriaMods

#endif // MORIA_TESTABLE_H
