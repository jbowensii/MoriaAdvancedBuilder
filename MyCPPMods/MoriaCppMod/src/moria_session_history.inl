// moria_session_history.inl — v6.7.0+ mod-owned session-history storage
//
// REPLACES the game's broken auto-managed history (`UMorGameSessionManager::
// GetConnectionHistory`) with a JSON file fully under user control. The native
// list still exists in memory but we never read from it — we wipe and inject
// rows from `m_sessionHistory` (parsed from the JSON file) every time JoinWorld
// shows.
//
// Architecture overview (in-place UI modification, no spawn-duplicate):
//   • applyModificationsToJoinWorld() (in moria_join_world_ui.inl) calls
//     injectSessionHistoryRows(nativeWidget) on every OnAfterShow.
//   • injectSessionHistoryRows() walks SessionBox children, removes any non-
//     SessionHistoryLabel rows, then calls the BP's own
//     `AddSessionHistoryItem(FMorConnectionHistoryItem)` UFunction once per
//     JSON entry. The BP creates the row, populates it, AND wires the click
//     delegates — so left-click → join works natively.
//
// File: <game>/ue4ss/Mods/MoriaCppMod/session_history.json
//
// Schema (UTF-8 JSON array of objects):
//   [
//     {"name": "Mereaks 1st World", "domain": "mereaksvtt.com", "port": "7003",
//      "password": "enc:Q0ZbVQ==", "lastJoined": "2026-04-30T08:00:00Z"},
//     ...
//   ]
//
// Passwords are XOR-obfuscated (key "MoriaModSessKey!") + Base64 with an
// "enc:" prefix. Plaintext entries (legacy, or hand-edited) are auto-migrated
// to the encrypted form on next save. Threat model: defend against casual
// JSON-file viewing; not against an attacker with the mod source.
//
// Hand-rolled minimal JSON parser because we don't want to vendor nlohmann
// for 5 string fields. Tolerates whitespace, // line comments, standard JSON
// escapes including \uXXXX. Corrupt/missing files reset to an empty list
// with a log warning — never crashes the game.
//
// Critical patterns this file embodies (caught in v6.7.0 code review):
//   • FString-bearing structs to BP UFunctions: placement-new each FString,
//     ALWAYS call ~FString() after the safeProcessEvent (or you leak heap).
//     See buildConnectionHistoryItem / destroyConnectionHistoryItem.
//   • UTF-8 → wide: never `std::wstring(s.begin(), s.end())`. Use the
//     utf8ToWide() / wideToUtf8() helpers.
//   • Throttle ProcessEvent polling. Hover-test for right-click hit-testing
//     samples at 100ms cadence; never per-tick.
//   • Defer ProcessEvent calls out of post-hooks. Manual-entry capture
//     (`Button_DirectJoinIP_OnMenuButtonClicked` BndEvt) queues the click
//     context for next-tick processing instead of re-entering PE inline.
//
// See `cpp-mod/docs/joinworld-ui-takeover.md` for the full methodology.

        // ────────────────────────────────────────────────────────────────
        // DATA MODEL
        // ────────────────────────────────────────────────────────────────
        struct SessionHistoryEntry
        {
            std::string name;        // user-visible label e.g. "Mereaks 1st World"
            std::string domain;      // hostname or IP
            std::string port;        // string so leading zeroes / IPv6 don't mangle
            std::string password;    // XOR-obfuscated; see writeSessionHistory at line 154 for the cipher
            std::string lastJoined;  // ISO-8601 timestamp at last successful join
        };

        std::vector<SessionHistoryEntry> m_sessionHistory;
        bool m_sessionHistoryLoaded{false};

        // ────────────────────────────────────────────────────────────────
        // PATH HELPER
        // ────────────────────────────────────────────────────────────────
        static std::string sessionHistoryPath()
        {
            return modPath("Mods/MoriaCppMod/session_history.json");
        }

        // ────────────────────────────────────────────────────────────────
        // PASSWORD OBFUSCATION
        // Threat model: prevent casual file viewing from exposing server
        // passwords. The mod runs locally, so any sufficiently determined
        // attacker can extract the obfuscation key — we don't pretend to
        // defend against that. XOR + Base64 is enough to keep passwords
        // out of plaintext in the JSON.
        //
        // Format: stored values are prefixed "enc:" followed by Base64 of the
        // XORed bytes. Strings without that prefix are treated as plaintext
        // (transparent migration from the old plaintext format).
        // ────────────────────────────────────────────────────────────────
        static inline const uint8_t* sessionPwdKey()
        {
            // 16-byte XOR key — ASCII "MoriaModSessKey!". Inline so it's safe
            // to live inside the class scope (.inl) without out-of-class def.
            static const uint8_t k[16] = {
                0x4D, 0x6F, 0x72, 0x69, 0x61, 0x4D, 0x6F, 0x64,
                0x53, 0x65, 0x73, 0x73, 0x4B, 0x65, 0x79, 0x21
            };
            return k;
        }

        static std::string base64Encode(const std::vector<uint8_t>& in)
        {
            static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((in.size() + 2) / 3) * 4);
            size_t i = 0;
            while (i + 3 <= in.size())
            {
                uint32_t n = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
                out += tbl[(n >> 18) & 0x3F];
                out += tbl[(n >> 12) & 0x3F];
                out += tbl[(n >> 6)  & 0x3F];
                out += tbl[(n)       & 0x3F];
                i += 3;
            }
            if (i < in.size())
            {
                uint32_t n = in[i] << 16;
                if (i + 1 < in.size()) n |= in[i+1] << 8;
                out += tbl[(n >> 18) & 0x3F];
                out += tbl[(n >> 12) & 0x3F];
                out += (i + 1 < in.size()) ? tbl[(n >> 6) & 0x3F] : '=';
                out += '=';
            }
            return out;
        }

        static std::vector<uint8_t> base64Decode(const std::string& in)
        {
            static int8_t rev[256];
            static bool init = false;
            if (!init)
            {
                std::fill(std::begin(rev), std::end(rev), int8_t(-1));
                const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (int i = 0; i < 64; ++i) rev[(uint8_t)tbl[i]] = (int8_t)i;
                init = true;
            }
            std::vector<uint8_t> out;
            out.reserve((in.size() / 4) * 3);
            uint32_t buf = 0; int bits = 0;
            for (char c : in)
            {
                if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
                int8_t v = rev[(uint8_t)c];
                if (v < 0) continue;
                buf = (buf << 6) | (uint32_t)v;
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back((uint8_t)((buf >> bits) & 0xFF));
                }
            }
            return out;
        }

        static std::string obfuscatePassword(const std::string& plain)
        {
            if (plain.empty()) return "";
            std::vector<uint8_t> xored(plain.size());
            for (size_t i = 0; i < plain.size(); ++i)
                xored[i] = (uint8_t)plain[i] ^ sessionPwdKey()[i % 16];
            return std::string("enc:") + base64Encode(xored);
        }

        static std::string deobfuscatePassword(const std::string& stored)
        {
            const std::string prefix = "enc:";
            if (stored.size() <= prefix.size() ||
                stored.compare(0, prefix.size(), prefix) != 0)
            {
                // Legacy plaintext or empty — return as-is.
                return stored;
            }
            std::vector<uint8_t> data = base64Decode(stored.substr(prefix.size()));
            std::string out(data.size(), '\0');
            for (size_t i = 0; i < data.size(); ++i)
                out[i] = (char)(data[i] ^ sessionPwdKey()[i % 16]);
            return out;
        }

        // ────────────────────────────────────────────────────────────────
        // JSON SERIALIZE — escape and emit. Newlines + 2-space indent so
        // the file is human-editable in any text editor.
        // ────────────────────────────────────────────────────────────────
        static void jsonEscape(std::string& out, const std::string& s)
        {
            out.push_back('"');
            for (char c : s)
            {
                switch (c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                            out += buf;
                        }
                        else
                        {
                            out.push_back(c);
                        }
                }
            }
            out.push_back('"');
        }

        std::string serializeSessionHistoryJson()
        {
            std::string out;
            out.reserve(256 + m_sessionHistory.size() * 256);
            out += "[\n";
            for (size_t i = 0; i < m_sessionHistory.size(); ++i)
            {
                const auto& e = m_sessionHistory[i];
                out += "  {";
                out += "\"name\": ";       jsonEscape(out, e.name);       out += ", ";
                out += "\"domain\": ";     jsonEscape(out, e.domain);     out += ", ";
                out += "\"port\": ";       jsonEscape(out, e.port);       out += ", ";
                out += "\"password\": ";   jsonEscape(out, obfuscatePassword(e.password)); out += ", ";
                out += "\"lastJoined\": "; jsonEscape(out, e.lastJoined);
                out += "}";
                if (i + 1 < m_sessionHistory.size()) out += ",";
                out += "\n";
            }
            out += "]\n";
            return out;
        }

        // ────────────────────────────────────────────────────────────────
        // JSON PARSE — minimal recursive-descent for our exact schema.
        // Supports: array of objects with string-typed fields; standard
        // string escapes (\" \\ \b \f \n \r \t \uXXXX). Returns empty
        // vector on any parse error (logs a warning).
        // ────────────────────────────────────────────────────────────────
        struct JsonCursor { const char* p; const char* end; };

        static bool jsonSkipWs(JsonCursor& c)
        {
            while (c.p < c.end)
            {
                char ch = *c.p;
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
                {
                    ++c.p;
                    continue;
                }
                // Tolerate // line comments (not strictly JSON but handy)
                if (ch == '/' && c.p + 1 < c.end && c.p[1] == '/')
                {
                    while (c.p < c.end && *c.p != '\n') ++c.p;
                    continue;
                }
                break;
            }
            return c.p < c.end;
        }

        static bool jsonExpect(JsonCursor& c, char ch)
        {
            jsonSkipWs(c);
            if (c.p < c.end && *c.p == ch) { ++c.p; return true; }
            return false;
        }

        static bool jsonParseString(JsonCursor& c, std::string& out)
        {
            jsonSkipWs(c);
            if (c.p >= c.end || *c.p != '"') return false;
            ++c.p;
            out.clear();
            while (c.p < c.end)
            {
                char ch = *c.p++;
                if (ch == '"') return true;
                if (ch != '\\') { out.push_back(ch); continue; }
                if (c.p >= c.end) return false;
                char esc = *c.p++;
                switch (esc)
                {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                    {
                        if (c.end - c.p < 4) return false;
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            cp <<= 4;
                            char h = c.p[i];
                            if      (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= 10 + (h - 'a');
                            else if (h >= 'A' && h <= 'F') cp |= 10 + (h - 'A');
                            else return false;
                        }
                        c.p += 4;
                        // UTF-8 encode (BMP only, no surrogate pairs in our data)
                        if (cp < 0x80)
                            out.push_back(static_cast<char>(cp));
                        else if (cp < 0x800)
                        {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        else
                        {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }

        bool parseSessionHistoryJson(const std::string& text,
                                     std::vector<SessionHistoryEntry>& out)
        {
            out.clear();
            JsonCursor c{ text.data(), text.data() + text.size() };
            if (!jsonExpect(c, '[')) return false;
            jsonSkipWs(c);
            if (c.p < c.end && *c.p == ']') { ++c.p; return true; }  // empty array
            while (c.p < c.end)
            {
                if (!jsonExpect(c, '{')) return false;
                SessionHistoryEntry e;
                while (true)
                {
                    std::string key;
                    if (!jsonParseString(c, key))   return false;
                    if (!jsonExpect(c, ':'))        return false;
                    std::string val;
                    if (!jsonParseString(c, val))   return false;
                    if      (key == "name")       e.name       = val;
                    else if (key == "domain")     e.domain     = val;
                    else if (key == "port")       e.port       = val;
                    else if (key == "password")   e.password   = deobfuscatePassword(val);
                    else if (key == "lastJoined") e.lastJoined = val;
                    // unknown keys silently ignored — forward-compat
                    jsonSkipWs(c);
                    if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
                    if (c.p < c.end && *c.p == '}') { ++c.p; break; }
                    return false;
                }
                out.push_back(std::move(e));
                jsonSkipWs(c);
                if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
                if (c.p < c.end && *c.p == ']') { ++c.p; return true; }
                return false;
            }
            return false;
        }

        // ────────────────────────────────────────────────────────────────
        // LOAD / SAVE
        // ────────────────────────────────────────────────────────────────
        void loadSessionHistory()
        {
            m_sessionHistory.clear();
            const std::string path = sessionHistoryPath();
            std::ifstream f(path, std::ios::binary);
            if (!f)
            {
                VLOG(STR("[SessionHistory] no file at '{}', starting empty\n"),
                     utf8ToWide(path).c_str());
                m_sessionHistoryLoaded = true;
                return;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            std::string text = ss.str();
            if (!parseSessionHistoryJson(text, m_sessionHistory))
            {
                m_sessionHistory.clear();
                VLOG(STR("[SessionHistory] parse error at '{}', starting empty\n"),
                     utf8ToWide(path).c_str());
            }
            else
            {
                VLOG(STR("[SessionHistory] loaded {} entries from '{}'\n"),
                     (int)m_sessionHistory.size(),
                     utf8ToWide(path).c_str());
            }
            m_sessionHistoryLoaded = true;
        }

        bool saveSessionHistory()
        {
            const std::string path = sessionHistoryPath();
            std::string text = serializeSessionHistoryJson();
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
            {
                VLOG(STR("[SessionHistory] save FAILED — cannot open '{}'\n"),
                     utf8ToWide(path).c_str());
                return false;
            }
            f.write(text.data(), text.size());
            VLOG(STR("[SessionHistory] saved {} entries to '{}'\n"),
                 (int)m_sessionHistory.size(),
                 utf8ToWide(path).c_str());
            return true;
        }

        // ────────────────────────────────────────────────────────────────
        // CRUD — operations preserve user order (no auto-sort).
        // ────────────────────────────────────────────────────────────────
        // Match by domain+port (case-insensitive on domain).
        static bool sessionEntryMatches(const SessionHistoryEntry& e,
                                        const std::string& domain,
                                        const std::string& port)
        {
            if (e.port != port) return false;
            if (e.domain.size() != domain.size()) return false;
            for (size_t i = 0; i < domain.size(); ++i)
            {
                char a = e.domain[i], b = domain[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) return false;
            }
            return true;
        }

        // ISO 8601 UTC timestamp "YYYY-MM-DDTHH:MM:SSZ" — short, sortable.
        static std::string nowIsoTimestamp()
        {
            std::time_t t = std::time(nullptr);
            std::tm gm{};
#if defined(_WIN32)
            gmtime_s(&gm, &t);
#else
            gm = *std::gmtime(&t);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                          gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday,
                          gm.tm_hour, gm.tm_min, gm.tm_sec);
            return buf;
        }

        // Add a new entry or refresh an existing one. Existing entries with
        // matching domain+port are updated in place (preserves position in
        // the list — important so the user's chosen ordering survives joins).
        void addOrUpdateSessionHistory(const SessionHistoryEntry& incoming)
        {
            if (!m_sessionHistoryLoaded) loadSessionHistory();
            for (auto& e : m_sessionHistory)
            {
                if (sessionEntryMatches(e, incoming.domain, incoming.port))
                {
                    e.name       = incoming.name;
                    e.password   = incoming.password;
                    e.lastJoined = incoming.lastJoined.empty()
                                   ? nowIsoTimestamp()
                                   : incoming.lastJoined;
                    saveSessionHistory();
                    return;
                }
            }
            SessionHistoryEntry copy = incoming;
            if (copy.lastJoined.empty()) copy.lastJoined = nowIsoTimestamp();
            m_sessionHistory.push_back(std::move(copy));
            saveSessionHistory();
        }

        // Remove an entry by domain+port. Returns true if removed.
        bool removeSessionHistory(const std::string& domain,
                                  const std::string& port)
        {
            if (!m_sessionHistoryLoaded) loadSessionHistory();
            for (auto it = m_sessionHistory.begin(); it != m_sessionHistory.end(); ++it)
            {
                if (sessionEntryMatches(*it, domain, port))
                {
                    m_sessionHistory.erase(it);
                    saveSessionHistory();
                    return true;
                }
            }
            return false;
        }

        // Replace by index (0-based). Used by the right-click delete UI which
        // already knows which row was clicked. Returns true on success.
        bool removeSessionHistoryAt(size_t index)
        {
            if (!m_sessionHistoryLoaded) loadSessionHistory();
            if (index >= m_sessionHistory.size()) return false;
            m_sessionHistory.erase(m_sessionHistory.begin() + index);
            saveSessionHistory();
            return true;
        }

        // ────────────────────────────────────────────────────────────────
        // RENDER INJECTION
        // After the JoinWorld BP is spawned, find SessionHistoryList →
        // SessionBox, clear native row children (keep the SessionHistoryLabel),
        // and inject one game-native WBP_UI_SessionHistory_Item_C row per
        // entry in our JSON storage. We populate the row's TextBlocks by
        // name (Host_Or_WorldName_TextBox, SessionDetailsTextBox).
        // ────────────────────────────────────────────────────────────────
        UClass* m_jwCls_SessionHistoryItem{nullptr};

        // Resolve and cache WBP_UI_SessionHistory_Item_C UClass.
        // Tries (a) existing rows in the SessionBox, (b) StaticFindObject by path.
        UClass* resolveSessionHistoryItemClass(UObject* sessionBox)
        {
            if (m_jwCls_SessionHistoryItem) return m_jwCls_SessionHistoryItem;
            if (sessionBox)
            {
                auto* slots = sessionBox->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slots)
                {
                    for (int i = 0; i < slots->Num(); ++i)
                    {
                        UObject* slot = (*slots)[i];
                        if (!slot || !isObjectAlive(slot)) continue;
                        auto* contentPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (!contentPtr || !*contentPtr) continue;
                        std::wstring cls = safeClassName(*contentPtr);
                        if (cls == STR("WBP_UI_SessionHistory_Item_C"))
                        {
                            m_jwCls_SessionHistoryItem = static_cast<UClass*>((*contentPtr)->GetClassPrivate());
                            break;
                        }
                    }
                }
            }
            if (!m_jwCls_SessionHistoryItem)
            {
                try
                {
                    m_jwCls_SessionHistoryItem = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr,
                        STR("/Game/UI/MainMenu/WorldSelect/WBP_UI_SessionHistory_Item.WBP_UI_SessionHistory_Item_C"));
                }
                catch (...) {}
            }
            return m_jwCls_SessionHistoryItem;
        }

        // Set a TextBlock's text by name within a given user widget. No-op if
        // either the child or its SetText UFunction can't be resolved.
        void setTextBlockTextByName(UObject* userWidget, const wchar_t* childName,
                                    const std::wstring& text)
        {
            UObject* tb = jw_findChildInTree(userWidget, childName);
            if (!tb) return;
            umgSetText(tb, text);
        }

        // Per-row tracking — populated during injection so right-click delete
        // can map a row click back to its JSON entry index.
        struct InjectedRowRef
        {
            FWeakObjectPtr widget;     // the WBP_UI_SessionHistory_Item_C instance
            size_t         entryIndex; // position in m_sessionHistory
        };
        std::vector<InjectedRowRef> m_injectedRows;
        FWeakObjectPtr m_sessionHistoryScrollBox;  // our UScrollBox holding the rows

        // Resolve FMorConnectionHistoryItem layout via reflection. Probe walks
        // the AddSessionHistoryItem UFunction → ConnectionHistoryData parm
        // FStructProperty → UScriptStruct, reads stride + each field offset.
        // Stores results in s_off_chi* sentinels (in moria_reflection.h).
        // Falls back to the previously-hardcoded values (stride 0x58,
        // WorldName 0x00, ConnType 0x10, InviteString 0x18, UniqueInvite 0x28,
        // Password 0x38, IsDedicated 0x48, Created 0x50) on any failure —
        // those values are the historic shipping layout.
        bool ensureConnectionHistoryItemOffsets(FProperty* pData)
        {
            if (s_off_chiStride > 0
                && s_off_chiWorldName    >= 0
                && s_off_chiConnType     >= 0
                && s_off_chiInviteString >= 0
                && s_off_chiUniqueInvite >= 0
                && s_off_chiPassword     >= 0
                && s_off_chiIsDedicated  >= 0
                && s_off_chiCreated      >= 0)
                return true;
            if (!pData) return false;

            int stride = -1;
            int offWorldName = -1, offConnType = -1, offInviteString = -1;
            int offUniqueInvite = -1, offPassword = -1;
            int offIsDedicated = -1, offCreated = -1;

            if (auto* sProp = CastField<FStructProperty>(pData))
            {
                if (UStruct* sStruct = sProp->GetStruct())
                {
                    if (auto* ss = static_cast<UScriptStruct*>(sStruct))
                        stride = static_cast<int>(ss->GetStructureSize());
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("WorldName")))
                        offWorldName = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("ConnectionType")))
                        offConnType = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("InviteString")))
                        offInviteString = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("UniqueInviteCode")))
                        offUniqueInvite = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("OptionalPassword")))
                        offPassword = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("bIsDedicatedServer")))
                        offIsDedicated = p->GetOffset_Internal();
                    if (auto* p = sStruct->GetPropertyByNameInChain(STR("CreationDateTime")))
                        offCreated = p->GetOffset_Internal();
                }
            }

            s_off_chiStride       = (stride          > 0) ? stride          : 0x58;
            s_off_chiWorldName    = (offWorldName    >= 0) ? offWorldName    : 0x00;
            s_off_chiConnType     = (offConnType     >= 0) ? offConnType     : 0x10;
            s_off_chiInviteString = (offInviteString >= 0) ? offInviteString : 0x18;
            s_off_chiUniqueInvite = (offUniqueInvite >= 0) ? offUniqueInvite : 0x28;
            s_off_chiPassword     = (offPassword     >= 0) ? offPassword     : 0x38;
            s_off_chiIsDedicated  = (offIsDedicated  >= 0) ? offIsDedicated  : 0x48;
            s_off_chiCreated      = (offCreated      >= 0) ? offCreated      : 0x50;
            VLOG(STR("[SessionHistory] FMorConnectionHistoryItem layout resolved: stride={:#x} "
                     "World={:#x} ConnType={:#x} Invite={:#x} Unique={:#x} Pwd={:#x} Ded={:#x} Created={:#x}\n"),
                 (unsigned)s_off_chiStride, (unsigned)s_off_chiWorldName, (unsigned)s_off_chiConnType,
                 (unsigned)s_off_chiInviteString, (unsigned)s_off_chiUniqueInvite,
                 (unsigned)s_off_chiPassword, (unsigned)s_off_chiIsDedicated, (unsigned)s_off_chiCreated);
            return true;
        }

        // Build an FMorConnectionHistoryItem (~88 bytes) in the given byte buffer,
        // populated from a JSON entry. Field offsets and stride resolved via
        // ensureConnectionHistoryItemOffsets() above. Historic shipping layout
        // (preserved as fallbacks) from dumps/CXXHeaderDump/Moria.hpp:
        //   0x00: FString WorldName            (16 bytes — name shown on row)
        //   0x10: EMorConnectionType (uint8)   (1=IpAndPort)
        //   0x18: FString InviteString         (16 bytes — host:port for join)
        //   0x28: FString UniqueInviteCode     (16 bytes — empty for IP joins)
        //   0x38: FString OptionalPassword     (16 bytes)
        //   0x48: bool bIsDedicatedServer      (1 byte)
        //   0x50: FDateTime CreationDateTime   (8 bytes — int64 ticks)
        // Total: 0x58 (88 bytes).
        //
        // FStrings constructed via the FString(const wchar_t*) ctor (allocates
        // a heap buffer). UE4 ProcessEvent semantics: when the receiving BP
        // function takes the struct by value, the engine COPIES it (calling
        // FString copy ctors which bump the refcount / re-alloc), and the
        // caller is still responsible for destructing its own copy. We
        // explicitly destruct via destroyConnectionHistoryItem() after PE.
        void buildConnectionHistoryItem(uint8_t* dst, const SessionHistoryEntry& e,
                                        bool isDedicated)
        {
            std::memset(dst, 0, s_off_chiStride > 0 ? s_off_chiStride : 0x58);

            // WorldName (UTF-8 → wide via proper MultiByteToWideChar)
            std::wstring wname = utf8ToWide(e.name);
            new (dst + s_off_chiWorldName) FString(wname.c_str());

            // ConnectionType = IpAndPort (1)
            *(dst + s_off_chiConnType) = 1;

            // InviteString = "domain:port"
            std::string urlS = e.domain;
            if (!e.port.empty()) { urlS += ":"; urlS += e.port; }
            std::wstring wurl = utf8ToWide(urlS);
            new (dst + s_off_chiInviteString) FString(wurl.c_str());

            // UniqueInviteCode (leave empty)
            new (dst + s_off_chiUniqueInvite) FString(STR(""));

            // OptionalPassword
            std::wstring wpass = utf8ToWide(e.password);
            new (dst + s_off_chiPassword) FString(wpass.c_str());

            // bIsDedicatedServer
            *(dst + s_off_chiIsDedicated) = isDedicated ? 1 : 0;

            // CreationDateTime — leave 0 (unknown).
            *reinterpret_cast<int64_t*>(dst + s_off_chiCreated) = 0;
        }

        // Symmetric destructor for the FStrings we placement-new'd above.
        // Call after the BP UFunction has consumed (copied) the struct.
        void destroyConnectionHistoryItem(uint8_t* dst)
        {
            reinterpret_cast<FString*>(dst + s_off_chiWorldName   )->~FString();
            reinterpret_cast<FString*>(dst + s_off_chiInviteString)->~FString();
            reinterpret_cast<FString*>(dst + s_off_chiUniqueInvite)->~FString();
            reinterpret_cast<FString*>(dst + s_off_chiPassword    )->~FString();
        }

        // Inject our session-history entries into the native JoinWorld.
        // Strategy: clear all existing native rows from SessionBox, then call
        // the BP's own `AddSessionHistoryItem(FMorConnectionHistoryItem)` for
        // each JSON entry — which creates fully-wired rows whose left-click
        // fires JoinSessionPressed → SessionHistoryList → JoinWorld → session
        // manager DirectJoin call. No manual delegate binding needed.
        void injectSessionHistoryRows(UObject* spawnedJoinWorld)
        {
            if (!spawnedJoinWorld || !isObjectAlive(spawnedJoinWorld)) return;
            if (!m_sessionHistoryLoaded) loadSessionHistory();

            UObject* sessHistList = jw_findChildInTree(spawnedJoinWorld, STR("SessionHistoryList"));
            if (!sessHistList)
            {
                VLOG(STR("[SessionHistory] SessionHistoryList not found in spawned JW\n"));
                return;
            }
            UObject* sessionBox = jw_findChildInTree(sessHistList, STR("SessionBox"));
            if (!sessionBox)
            {
                VLOG(STR("[SessionHistory] SessionBox not found in SessionHistoryList\n"));
                return;
            }

            UClass* itemCls = resolveSessionHistoryItemClass(sessionBox);
            if (!itemCls)
            {
                VLOG(STR("[SessionHistory] WBP_UI_SessionHistory_Item_C UClass unresolved — skipping inject\n"));
                return;
            }

            // Clear all rows under SessionBox EXCEPT SessionHistoryLabel
            // (the header text). VerticalBox.RemoveChild(UWidget*) takes the
            // child widget and detaches it.
            auto* removeFn = sessionBox->GetFunctionByNameInChain(STR("RemoveChild"));
            auto* slots = sessionBox->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            std::vector<UObject*> toRemove;
            if (slots && removeFn)
            {
                for (int i = 0; i < slots->Num(); ++i)
                {
                    UObject* slot = (*slots)[i];
                    if (!slot || !isObjectAlive(slot)) continue;
                    auto* contentPtr = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                    if (!contentPtr || !*contentPtr) continue;
                    std::wstring nm; try { nm = (*contentPtr)->GetName(); } catch (...) {}
                    if (nm == STR("SessionHistoryLabel")) continue;
                    toRemove.push_back(*contentPtr);
                }
                for (UObject* w : toRemove)
                {
                    std::vector<uint8_t> b(removeFn->GetParmsSize(), 0);
                    auto* p = findParam(removeFn, STR("Content"));
                    if (!p) p = findParam(removeFn, STR("Widget"));
                    if (p)
                    {
                        *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = w;
                        safeProcessEvent(sessionBox, removeFn, b.data());
                    }
                }
            }

            // Reset injected-row tracking; we'll repopulate from the rows
            // that AddSessionHistoryItem creates below.
            m_injectedRows.clear();
            m_sessionHistoryScrollBox = FWeakObjectPtr();

            if (m_sessionHistory.empty())
            {
                VLOG(STR("[SessionHistory] cleared {} native row(s); 0 mod entries to add\n"),
                     (int)toRemove.size());
                return;
            }

            // Use the BP's own AddSessionHistoryItem(FMorConnectionHistoryItem)
            // UFunction to create each row. The BP wires up SelectButton's
            // OnReleased delegate internally so left-click fires the proper
            // JoinSessionPressed → join chain natively. We just provide data.
            auto* addItemFn = sessHistList->GetFunctionByNameInChain(STR("AddSessionHistoryItem"));
            if (!addItemFn)
            {
                VLOG(STR("[SessionHistory] AddSessionHistoryItem UFunction not found — fallback abort\n"));
                return;
            }
            auto* pData = findParam(addItemFn, STR("ConnectionHistoryData"));
            if (!pData) pData = findParam(addItemFn, STR("Connection History Item Data"));
            if (!pData) pData = findParam(addItemFn, STR("ConnectionHistoryItemData"));
            if (!pData)
            {
                VLOG(STR("[SessionHistory] AddSessionHistoryItem param not resolved — abort\n"));
                return;
            }
            ensureConnectionHistoryItemOffsets(pData);

            int added = 0;
            for (size_t idx = 0; idx < m_sessionHistory.size(); ++idx)
            {
                const auto& e = m_sessionHistory[idx];
                std::vector<uint8_t> buf(addItemFn->GetParmsSize(), 0);
                buildConnectionHistoryItem(buf.data() + pData->GetOffset_Internal(),
                                           e, /*isDedicated*/ true);
                try
                {
                    safeProcessEvent(sessHistList, addItemFn, buf.data());
                    ++added;
                    // Track the most-recently-added row by walking SessionBox
                    // children once after each call. The freshly-added row is
                    // the last child (excluding SessionHistoryLabel).
                    auto* slotsAfter = sessionBox->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                    if (slotsAfter && slotsAfter->Num() > 0)
                    {
                        UObject* lastSlot = (*slotsAfter)[slotsAfter->Num() - 1];
                        if (lastSlot && isObjectAlive(lastSlot))
                        {
                            auto* contentPtr = lastSlot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                            if (contentPtr && *contentPtr)
                            {
                                std::wstring nm; try { nm = (*contentPtr)->GetName(); } catch (...) {}
                                if (nm != STR("SessionHistoryLabel"))
                                {
                                    InjectedRowRef ref;
                                    ref.widget = FWeakObjectPtr(*contentPtr);
                                    ref.entryIndex = idx;
                                    m_injectedRows.push_back(ref);
                                }
                            }
                        }
                    }
                }
                catch (...) {}
                // Destruct the FStrings we placement-new'd. The BP has by now
                // copied the struct contents into its own row member, so the
                // local heap allocations are safe to free.
                destroyConnectionHistoryItem(buf.data() + pData->GetOffset_Internal());
            }
            VLOG(STR("[SessionHistory] cleared {} native row(s), added {} via AddSessionHistoryItem\n"),
                 (int)toRemove.size(), added);
        }

        // ────────────────────────────────────────────────────────────────
        // RIGHT-CLICK DELETE
        // Polled from main tick when JoinWorld duplicate is up:
        //   • on RBUTTON release, get mouse pos in design pixels
        //   • walk m_injectedRows, find the row whose rendered geometry
        //     contains the click, capture its entryIndex
        //   • spawn the game's WBP_UI_GenericPopup_C with two buttons,
        //     bind ConfirmButton.OnMenuButtonClicked to the actual delete
        // ────────────────────────────────────────────────────────────────
        UClass* m_jwCls_GenericPopup{nullptr};
        FWeakObjectPtr m_pendingDeletePopup;
        size_t m_pendingDeleteIndex{(size_t)-1};
        // (m_pendingDeleteConfirmed removed in v6.23.0 - was only read by the
        //  obsolete tickSessionHistoryConfirm polling tick, also deleted.)

        // Deferred manual-join capture. The Button_DirectJoinIP / Button_JoinLocal
        // BndEvt fires from inside the global ProcessEvent post-hook; reading
        // input field text from there means re-entering ProcessEvent (GetText +
        // Conv_TextToString = two PE calls) which is the documented reentrancy
        // hazard. Instead the post-hook just stashes the context + isLocal flag
        // here, and tickSessionHistoryDeferredCapture() on the next tick safely
        // reads the field text on the game thread proper.
        FWeakObjectPtr m_pendingManualJoinWidget;
        bool m_pendingManualJoinIsLocal{false};

        void queueManualJoinCapture(UObject* advancedJoinWidget, bool isLocal)
        {
            m_pendingManualJoinWidget = FWeakObjectPtr(advancedJoinWidget);
            m_pendingManualJoinIsLocal = isLocal;
        }

        // Called from main tick. Consumes the pending capture (if any).
        void tickSessionHistoryDeferredCapture()
        {
            UObject* w = m_pendingManualJoinWidget.Get();
            if (!w || !isObjectAlive(w)) return;
            bool isLocal = m_pendingManualJoinIsLocal;
            // Clear pending state immediately so we don't re-fire if reading fails.
            m_pendingManualJoinWidget = FWeakObjectPtr();
            m_pendingManualJoinIsLocal = false;

            auto readFieldText = [&](const wchar_t* childName) -> std::string {
                UObject* tb = jw_findChildInTree(w, childName);
                if (!tb || !isObjectAlive(tb)) return "";
                auto* getFn = tb->GetFunctionByNameInChain(STR("GetText"));
                if (!getFn) return "";
                std::vector<uint8_t> buf(getFn->GetParmsSize(), 0);
                try { safeProcessEvent(tb, getFn, buf.data()); } catch (...) { return ""; }
                auto* pRet = findParam(getFn, STR("ReturnValue"));
                if (!pRet) return "";
                auto* asStrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.KismetTextLibrary:Conv_TextToString"));
                auto* ktlClass = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Engine.KismetTextLibrary"));
                if (!asStrFn || !ktlClass) return "";
                UObject* ktlCDO = ktlClass->GetClassDefaultObject();
                if (!ktlCDO) return "";
                std::vector<uint8_t> sb(asStrFn->GetParmsSize(), 0);
                auto* pIn  = findParam(asStrFn, STR("InText"));
                auto* pOut = findParam(asStrFn, STR("ReturnValue"));
                if (!pIn || !pOut) return "";
                std::memcpy(sb.data() + pIn->GetOffset_Internal(),
                            buf.data() + pRet->GetOffset_Internal(),
                            pIn->GetSize());
                try { safeProcessEvent(ktlCDO, asStrFn, sb.data()); } catch (...) { return ""; }
                FString* fs = reinterpret_cast<FString*>(sb.data() + pOut->GetOffset_Internal());
                if (!fs) return "";
                const wchar_t* wptr = fs->GetCharArray().GetData();
                if (!wptr || !*wptr) return "";
                return wideToUtf8(std::wstring(wptr));
            };

            std::string hostOrPort = readFieldText(
                isLocal ? STR("TextField_LocalJoinPort") : STR("TextField_DirectJoinIP"));
            std::string pwd = readFieldText(
                isLocal ? STR("TextField_LocalJoinPassword") : STR("TextField_DirectJoinPassword"));
            if (hostOrPort.empty()) return;

            std::string domain = hostOrPort, port;
            if (isLocal)
            {
                domain = "127.0.0.1";
                port   = hostOrPort;
            }
            else
            {
                size_t colon = hostOrPort.rfind(':');
                if (colon != std::string::npos && colon > 0 && colon < hostOrPort.size() - 1)
                {
                    std::string tail = hostOrPort.substr(colon + 1);
                    bool allDigits = !tail.empty();
                    for (char c : tail) if (c < '0' || c > '9') { allDigits = false; break; }
                    if (allDigits) { domain = hostOrPort.substr(0, colon); port = tail; }
                }
            }

            SessionHistoryEntry entry;
            entry.name     = (isLocal ? "Local " : "Direct ") + (domain + (port.empty() ? "" : ":" + port));
            entry.domain   = domain;
            entry.port     = port;
            entry.password = pwd;
            entry.lastJoined = "";
            addOrUpdateSessionHistory(entry);
            VLOG(STR("[SessionHistory] saved (manual entry, deferred) host='{}' port='{}' pwd-len={}\n"),
                 utf8ToWide(domain).c_str(),
                 utf8ToWide(port).c_str(),
                 (int)pwd.size());
        }

        UClass* resolveGenericPopupClass()
        {
            if (m_jwCls_GenericPopup) return m_jwCls_GenericPopup;
            try
            {
                m_jwCls_GenericPopup = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr,
                    STR("/Game/UI/PopUp/WBP_UI_GenericPopup.WBP_UI_GenericPopup_C"));
            }
            catch (...) {}
            return m_jwCls_GenericPopup;
        }

        // Read a UWidget's cached geometry. Returns true if both top-left
        // (absolute Slate position) and size are available. Coordinates are
        // in absolute slate pixels (DPI-scaled), suitable for hit-testing
        // against getMousePositionSlate() output (which is design pixels;
        // we'll convert by sampling viewport scale).
        bool getWidgetSlateRect(UObject* widget, float& outX, float& outY,
                                float& outW, float& outH)
        {
            if (!widget || !isObjectAlive(widget)) return false;
            // Use UMG's GetCachedGeometry → returns FGeometry. We'll use
            // GetTickSpaceGeometry which has the most useful absolute coords
            // for hit testing in screen space.
            auto* fn = widget->GetFunctionByNameInChain(STR("GetCachedGeometry"));
            if (!fn) return false;
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pRet) return false;
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            try { safeProcessEvent(widget, fn, buf.data()); } catch (...) { return false; }
            // FGeometry layout in 4.27 (approximate):
            //   FVector2D LocalSize        @ +0  (8 bytes)
            //   FSlateLayoutTransform AccumulatedLayoutTransform @ +8 (12: scale + translation)
            //   FSlateRenderTransform AccumulatedRenderTransform @ +20 (40+: m + translation)
            //   FVector2D LocalPosition    @ later
            // Reading geometry directly from struct is fragile — better to use
            // BlueprintFunctionLibrary helpers, but those need extra plumbing.
            // For now, use GetCachedGeometry's LocalSize at +0 and assume
            // position via AbsolutePosition through a slot lookup.
            //
            // Pragmatic fallback: just hit-test each row by its index given
            // we know the inject order and approximate row height.
            uint8_t* g = buf.data() + pRet->GetOffset_Internal();
            float sizeX = *reinterpret_cast<float*>(g + 0);
            float sizeY = *reinterpret_cast<float*>(g + 4);
            outW = sizeX; outH = sizeY;
            outX = 0; outY = 0;  // unknown without full struct decode — caller falls back to index math
            return (sizeX > 0 || sizeY > 0);
        }

        // Spawn the game's GenericPopup with two buttons. Sets the title +
        // message + button labels, then adds it to the viewport at high ZOrder.
        // Returns the spawned widget on success.
        UObject* showDeleteConfirmPopup(size_t entryIndex)
        {
            UClass* popupCls = resolveGenericPopupClass();
            if (!popupCls)
            {
                VLOG(STR("[SessionHistory] WBP_UI_GenericPopup_C class not found — abort confirm\n"));
                return nullptr;
            }
            UObject* popup = jw_createGameWidget(popupCls);
            if (!popup) return nullptr;

            if (auto* fn = popup->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                auto* p = findParam(fn, STR("ZOrder"));
                if (p) *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 500;
                safeProcessEvent(popup, fn, b.data());
            }

            // Call the BP's OnShowWithTwoButtons(Title, Message, Confirm, Cancel)
            if (auto* showFn = popup->GetFunctionByNameInChain(STR("OnShowWithTwoButtons")))
            {
                std::vector<uint8_t> b(showFn->GetParmsSize(), 0);
                auto setText = [&](const wchar_t* parmName, const wchar_t* val)
                {
                    auto* p = findParam(showFn, parmName);
                    if (!p) return;
                    FText t(val);
                    std::memcpy(b.data() + p->GetOffset_Internal(), &t, sizeof(FText));
                };
                std::wstring entryLabel;
                if (entryIndex < m_sessionHistory.size())
                {
                    const auto& e = m_sessionHistory[entryIndex];
                    entryLabel = utf8ToWide(e.name) + L" (" +
                                 utf8ToWide(e.domain) + L":" +
                                 utf8ToWide(e.port) + L")";
                }
                setText(STR("Title"),               L"Delete from Session History?");
                std::wstring msg = L"Are you sure you want to remove\n" + entryLabel +
                                   L"\nfrom your session history?";
                setText(STR("Message"),             msg.c_str());
                setText(STR("ConfirmButtonText"),   L"Delete");
                setText(STR("CancelButtonText"),    L"Cancel");
                safeProcessEvent(popup, showFn, b.data());
            }

            m_pendingDeletePopup = FWeakObjectPtr(popup);
            m_pendingDeleteIndex = entryIndex;
            VLOG(STR("[SessionHistory] confirm popup shown for entry index {}\n"), (int)entryIndex);
            return popup;
        }

        // Called from the global ProcessEvent post-hook on either
        // `OnButtonReleasedEvent` (the actual click — fires on the
        // WBP_FrontEndButton_C instance) or `OnMenuButtonClicked` (broadcast).
        // Compare context to popup's ConfirmButton/CancelButton members to
        // know which one was clicked, then act immediately (no waiting for
        // the popup's hide animation).
        void onAnyMenuButtonClicked(UObject* context, const wchar_t* fnName)
        {
            UObject* popup = m_pendingDeletePopup.Get();
            if (!popup || !isObjectAlive(popup)) return;
            if (!context) return;

            // Read the popup's ConfirmButton + CancelButton members.
            auto* confirmPtr = popup->GetValuePtrByPropertyNameInChain<UObject*>(STR("ConfirmButton"));
            auto* cancelPtr  = popup->GetValuePtrByPropertyNameInChain<UObject*>(STR("CancelButton"));
            UObject* confirmBtn = confirmPtr ? *confirmPtr : nullptr;
            UObject* cancelBtn  = cancelPtr  ? *cancelPtr  : nullptr;

            bool isConfirm = (context == confirmBtn);
            bool isCancel  = (context == cancelBtn);
            if (!isConfirm && !isCancel) return;  // some other button — ignore

            VLOG(STR("[SessionHistory] popup button event fn='{}' -> {}\n"),
                 fnName ? fnName : STR("?"),
                 isConfirm ? STR("CONFIRM") : STR("CANCEL"));

            if (isConfirm && m_pendingDeleteIndex < m_sessionHistory.size())
            {
                size_t idx = m_pendingDeleteIndex;
                std::string nameCopy = m_sessionHistory[idx].name;
                bool ok = removeSessionHistoryAt(idx);
                VLOG(STR("[SessionHistory] DELETED entry index {} ('{}'), removeOk={}\n"),
                     (int)idx,
                     utf8ToWide(nameCopy).c_str(),
                     ok ? 1 : 0);
                UObject* jw = m_modJoinWorldWidget.Get();
                if (jw && isObjectAlive(jw)) injectSessionHistoryRows(jw);
            }
            else
            {
                VLOG(STR("[SessionHistory] cancel — keeping entry {}\n"),
                     (int)m_pendingDeleteIndex);
            }

            // Force the popup to close (its own BP graph isn't subscribed to
            // these events when we spawn it standalone, so the hide animation
            // never plays). Use deferRemoveWidget for safety.
            try
            {
                if (auto* hideFn = popup->GetFunctionByNameInChain(STR("Hide")))
                {
                    std::vector<uint8_t> b(hideFn->GetParmsSize(), 0);
                    safeProcessEvent(popup, hideFn, b.data());
                }
            }
            catch (...) {}
            deferRemoveWidget(popup);

            m_pendingDeletePopup     = FWeakObjectPtr();
            m_pendingDeleteIndex     = (size_t)-1;
        }

        // Mouse polling: right-click on a row triggers the confirm popup.
        // Called from main tick when JoinWorld duplicate is up.
        // Strategy: use UMG's IsHovered() UFunction on each injected row
        // to find which one the cursor is over. Way more reliable than
        // approximate design-pixel math (rows differ in height etc.).
        void pollRightClickDeleteSessionHistory()
        {
            if (m_modJoinWorldWidget.Get() == nullptr) return;
            if (m_pendingDeletePopup.Get() != nullptr) return;  // popup already up

            // Track which row is hovered. UE4's hover state can clear on
            // RMB-down so we keep a rolling sticky last-known-hovered row.
            //
            // THROTTLED to ~10 Hz (every 100ms) to avoid the ProcessEvent
            // flood the architecture doc warns against. With 15 rows that's
            // 15 PE calls every 100ms vs 900/sec previously. Plenty fast for
            // hit-testing — humans can't right-click in <100ms after moving.
            static bool   s_lastRMB = false;
            static size_t s_lastHoveredEntry = (size_t)-1;
            static int    s_lastHoveredIdx   = -1;
            static ULONGLONG s_lastSampleMs  = 0;

            ULONGLONG nowMs = GetTickCount64();
            bool shouldSample = (nowMs - s_lastSampleMs) >= 100;
            if (shouldSample) s_lastSampleMs = nowMs;

            auto callIsHovered = [](UObject* w) -> bool {
                if (!w || !isObjectAlive(w)) return false;
                auto* fn = w->GetFunctionByNameInChain(STR("IsHovered"));
                if (!fn) return false;
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try { safeProcessEvent(w, fn, buf.data()); } catch (...) { return false; }
                auto* pRet = findParam(fn, STR("ReturnValue"));
                if (!pRet) return false;
                return *reinterpret_cast<bool*>(buf.data() + pRet->GetOffset_Internal());
            };

            // Sample hover state at the throttled cadence. Sticky-keep last
            // good value so even if hover clears on RMB-down we still know
            // which row was the click target.
            for (size_t i = 0; shouldSample && i < m_injectedRows.size(); ++i)
            {
                UObject* row = m_injectedRows[i].widget.Get();
                if (!row) continue;
                bool rowHov = callIsHovered(row);
                bool btnHov = false;
                UObject* selectBtn = jw_findChildInTree(row, STR("SelectButton"));
                if (selectBtn) btnHov = callIsHovered(selectBtn);
                if (rowHov || btnHov)
                {
                    s_lastHoveredEntry = m_injectedRows[i].entryIndex;
                    s_lastHoveredIdx   = (int)i;
                    break;
                }
            }

            bool nowDown  = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            bool released = (s_lastRMB && !nowDown);
            s_lastRMB = nowDown;
            if (!released) return;

            VLOG(STR("[SessionHistory] RMB released; last-hovered row={} entry={}; rows={}\n"),
                 s_lastHoveredIdx, (int)s_lastHoveredEntry,
                 (int)m_injectedRows.size());

            int hitVisibleIdx = s_lastHoveredIdx;
            size_t hitEntryIdx = s_lastHoveredEntry;

            if (hitEntryIdx == (size_t)-1)
            {
                VLOG(STR("[SessionHistory] no row hovered during press; ignoring (move cursor onto row before right-click)\n"));
                return;
            }

            VLOG(STR("[SessionHistory] right-click on row visible #{} -> entry index {}\n"),
                 hitVisibleIdx, (int)hitEntryIdx);
            showDeleteConfirmPopup(hitEntryIdx);
        }
