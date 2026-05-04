


struct DefChange
{
    std::string item;
    std::string property;
    std::string value;
};

struct DefDelete
{
    std::string item;
    std::string property;
    std::string value;
};

struct DefAddRow
{
    std::string rowName;
    std::string json;
};

struct DefMod
{
    std::string filePath;
    std::vector<DefChange> changes;
    std::vector<DefDelete> deletes;
    std::vector<DefAddRow> addRows;
};

struct DefDefinition
{
    std::string title;
    std::string author;
    std::string description;
    std::vector<DefMod> mods;
};

struct DefManifest
{
    std::string title;
    std::string authors;
    std::string description;
    bool includeSecrets{false};
    std::vector<std::string> defPaths;
};


static bool strEndsWithCI(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
        [](char a, char b) { return tolower(static_cast<unsigned char>(a)) == tolower(static_cast<unsigned char>(b)); });
}


struct XmlAttribute
{
    std::string name;
    std::string value;
};

struct XmlElement
{
    std::string tag;
    std::vector<XmlAttribute> attrs;
    std::string text;
    std::vector<XmlElement> children;
    bool selfClosing{false};
};

static std::string xmlGetAttr(const XmlElement& elem, const std::string& name)
{
    for (auto& a : elem.attrs)
        if (a.name == name) return a.value;
    return "";
}


static size_t xmlSkipWS(const std::string& xml, size_t pos)
{
    while (pos < xml.size() && (xml[pos] == ' ' || xml[pos] == '\t' || xml[pos] == '\r' || xml[pos] == '\n'))
        ++pos;
    return pos;
}


static size_t xmlParseAttrValue(const std::string& xml, size_t pos, std::string& out)
{
    if (pos >= xml.size()) return pos;
    char quote = xml[pos];
    if (quote != '"' && quote != '\'') return pos;
    ++pos;
    size_t start = pos;
    while (pos < xml.size() && xml[pos] != quote) ++pos;
    out = xml.substr(start, pos - start);
    if (pos < xml.size()) ++pos;

    std::string decoded;
    decoded.reserve(out.size());
    for (size_t i = 0; i < out.size(); i++)
    {
        if (out[i] == '&')
        {
            size_t semi = out.find(';', i);
            if (semi != std::string::npos)
            {
                std::string ent = out.substr(i + 1, semi - i - 1);
                if (ent == "amp") { decoded += '&'; i = semi; continue; }
                if (ent == "lt") { decoded += '<'; i = semi; continue; }
                if (ent == "gt") { decoded += '>'; i = semi; continue; }
                if (ent == "quot") { decoded += '"'; i = semi; continue; }
                if (ent == "apos") { decoded += '\''; i = semi; continue; }
            }
        }
        decoded += out[i];
    }
    out = decoded;
    return pos;
}


static size_t xmlParseAttrs(const std::string& xml, size_t pos, std::vector<XmlAttribute>& attrs, bool& selfClose)
{
    selfClose = false;
    while (pos < xml.size())
    {
        pos = xmlSkipWS(xml, pos);
        if (pos >= xml.size()) break;
        if (xml[pos] == '/')
        {
            selfClose = true;
            ++pos;
            pos = xmlSkipWS(xml, pos);
            if (pos < xml.size() && xml[pos] == '>') ++pos;
            return pos;
        }
        if (xml[pos] == '>')
        {
            ++pos;
            return pos;
        }

        size_t nameStart = pos;
        while (pos < xml.size() && xml[pos] != '=' && xml[pos] != ' ' && xml[pos] != '>' && xml[pos] != '/') ++pos;
        std::string attrName = xml.substr(nameStart, pos - nameStart);
        pos = xmlSkipWS(xml, pos);
        if (pos < xml.size() && xml[pos] == '=')
        {
            ++pos;
            pos = xmlSkipWS(xml, pos);
            std::string attrVal;
            pos = xmlParseAttrValue(xml, pos, attrVal);
            attrs.push_back({attrName, attrVal});
        }
    }
    return pos;
}


static size_t xmlParseElement(const std::string& xml, size_t pos, XmlElement& elem)
{
    pos = xmlSkipWS(xml, pos);
    if (pos >= xml.size() || xml[pos] != '<') return pos;


    if (pos + 1 < xml.size() && xml[pos + 1] == '?')
    {
        size_t end = xml.find("?>", pos);
        if (end != std::string::npos) return end + 2;
        return xml.size();
    }
    if (pos + 3 < xml.size() && xml.substr(pos, 4) == "<!--")
    {
        size_t end = xml.find("-->", pos);
        if (end != std::string::npos) return end + 3;
        return xml.size();
    }

    ++pos;

    size_t tagStart = pos;
    while (pos < xml.size() && xml[pos] != ' ' && xml[pos] != '>' && xml[pos] != '/' && xml[pos] != '\t' && xml[pos] != '\r' && xml[pos] != '\n') ++pos;
    elem.tag = xml.substr(tagStart, pos - tagStart);


    bool selfClose = false;
    pos = xmlParseAttrs(xml, pos, elem.attrs, selfClose);
    elem.selfClosing = selfClose;
    if (selfClose) return pos;


    std::string closeTag = "</" + elem.tag;
    while (pos < xml.size())
    {
        pos = xmlSkipWS(xml, pos);
        if (pos >= xml.size()) break;


        if (pos + closeTag.size() < xml.size() && xml.substr(pos, closeTag.size()) == closeTag)
        {
            pos += closeTag.size();
            pos = xmlSkipWS(xml, pos);
            if (pos < xml.size() && xml[pos] == '>') ++pos;
            return pos;
        }

        if (xml[pos] == '<')
        {

            if (pos + 8 < xml.size() && xml.substr(pos, 9) == "<![CDATA[")
            {
                size_t cdataStart = pos + 9;
                size_t cdataEnd = xml.find("]]>", cdataStart);
                if (cdataEnd != std::string::npos)
                {
                    elem.text += xml.substr(cdataStart, cdataEnd - cdataStart);
                    pos = cdataEnd + 3;
                }
                else
                    pos = xml.size();
                continue;
            }

            if (pos + 3 < xml.size() && xml.substr(pos, 4) == "<!--")
            {
                size_t end = xml.find("-->", pos);
                pos = (end != std::string::npos) ? end + 3 : xml.size();
                continue;
            }

            if (pos + 1 < xml.size() && xml[pos + 1] == '?')
            {
                size_t end = xml.find("?>", pos);
                pos = (end != std::string::npos) ? end + 2 : xml.size();
                continue;
            }

            if (pos + 1 < xml.size() && xml[pos + 1] == '/')
                break;

            XmlElement child;
            pos = xmlParseElement(xml, pos, child);
            if (!child.tag.empty())
                elem.children.push_back(std::move(child));
        }
        else
        {

            size_t textStart = pos;
            while (pos < xml.size() && xml[pos] != '<') ++pos;
            std::string text = xml.substr(textStart, pos - textStart);

            size_t a = text.find_first_not_of(" \t\r\n");
            size_t b = text.find_last_not_of(" \t\r\n");
            if (a != std::string::npos)
                elem.text += text.substr(a, b - a + 1);
        }
    }
    return pos;
}


static XmlElement xmlParse(const std::string& xml)
{
    XmlElement root;
    size_t pos = 0;
    while (pos < xml.size())
    {
        pos = xmlSkipWS(xml, pos);
        if (pos >= xml.size()) break;
        if (xml[pos] != '<') { ++pos; continue; }


        if (pos + 1 < xml.size() && xml[pos + 1] == '?')
        {
            size_t end = xml.find("?>", pos);
            pos = (end != std::string::npos) ? end + 2 : xml.size();
            continue;
        }
        if (pos + 3 < xml.size() && xml.substr(pos, 4) == "<!--")
        {
            size_t end = xml.find("-->", pos);
            pos = (end != std::string::npos) ? end + 3 : xml.size();
            continue;
        }

        pos = xmlParseElement(xml, pos, root);
        if (!root.tag.empty()) break;
    }
    return root;
}


static std::vector<std::string> listFiles(const std::string& dir, const std::string& pattern = "*")
{
    // v6.4.3 (hotfix) — use wide Windows API. FindFirstFileA interprets the path as the
    // active code page, which mangles UTF-8 bytes like the ™ in Steam's folder name.
    std::vector<std::string> files;
    WIN32_FIND_DATAW fd;
    std::string searchPathUtf8 = dir;
    if (!searchPathUtf8.empty() && searchPathUtf8.back() != '\\' && searchPathUtf8.back() != '/')
        searchPathUtf8 += '\\';
    searchPathUtf8 += pattern;
    std::wstring searchPathW = utf8PathToWide(searchPathUtf8);

    HANDLE hFind = FindFirstFileW(searchPathW.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        VLOG(STR("[MoriaCppMod] [Def] listFiles '{}' FindFirstFileW failed (GLE={})\n"),
             utf8ToWide(searchPathUtf8), err);
        return files;
    }
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            // Convert the wide filename back to UTF-8 for our std::string-based API
            std::wstring nameW(fd.cFileName);
            int ulen = WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), (int)nameW.size(),
                                           nullptr, 0, nullptr, nullptr);
            if (ulen > 0)
            {
                std::string nameU(ulen, '\0');
                WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(), (int)nameW.size(),
                                    nameU.data(), ulen, nullptr, nullptr);
                files.emplace_back(std::move(nameU));
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return files;
}


static std::string readFileToString(const std::string& path)
{
    std::ifstream f = openInputFile(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}


DefManifest parseManifest(const std::string& iniPath, const std::string& defBaseDir)
{
    DefManifest manifest;
    std::ifstream file = openInputFile(iniPath);
    if (!file.is_open()) return manifest;

    std::string section;
    std::string line;
    while (std::getline(file, line))
    {
        auto parsed = parseIniLine(line);
        if (auto* sec = std::get_if<ParsedIniSection>(&parsed))
        {
            section = sec->name;
        }
        else if (auto* kv = std::get_if<ParsedIniKeyValue>(&parsed))
        {
            if (strEqualCI(section, "ModInfo"))
            {
                if (strEqualCI(kv->key, "Title")) manifest.title = kv->value;
                else if (strEqualCI(kv->key, "Authors")) manifest.authors = kv->value;
                else if (strEqualCI(kv->key, "Description")) manifest.description += kv->value + "\n";
            }
            else if (strEqualCI(section, "Paths"))
            {


                if (strEqualCI(kv->value, "true"))
                {
                    std::string key = kv->key;

                    for (auto& c : key) { if (c == '|') c = '\\'; }

                    if (strEndsWithCI(key, ".def"))
                    {

                        std::string fullPath = defBaseDir + "\\" + key;
                        manifest.defPaths.push_back(fullPath);
                    }

                }
            }
            else if (strEqualCI(section, "Settings"))
            {
                if (strEqualCI(kv->key, "include_secrets"))
                    manifest.includeSecrets = strEqualCI(kv->value, "true") || kv->value == "1";
            }
        }
    }
    return manifest;
}


DefDefinition parseDef(const std::string& defPath)
{
    DefDefinition def;
    std::string xml = readFileToString(defPath);
    if (xml.empty())
    {
        VLOG(STR("[MoriaCppMod] [Def] Failed to read: {}\n"), utf8ToWide(defPath));
        return def;
    }

    XmlElement root = xmlParse(xml);


    if (root.tag == "definition")
    {
        for (auto& child : root.children)
        {
            if (child.tag == "title") def.title = child.text;
            else if (child.tag == "author") def.author = child.text;
            else if (child.tag == "description") def.description = child.text;
            else if (child.tag == "mod")
            {
                DefMod mod;
                mod.filePath = xmlGetAttr(child, "file");
                for (auto& op : child.children)
                {
                    if (op.tag == "change")
                    {
                        DefChange c;
                        c.item = xmlGetAttr(op, "item");
                        c.property = xmlGetAttr(op, "property");
                        c.value = xmlGetAttr(op, "value");
                        mod.changes.push_back(std::move(c));
                    }
                    else if (op.tag == "delete")
                    {
                        DefDelete d;
                        d.item = xmlGetAttr(op, "item");
                        d.property = xmlGetAttr(op, "property");
                        d.value = xmlGetAttr(op, "value");
                        mod.deletes.push_back(std::move(d));
                    }
                    else if (op.tag == "add_row")
                    {
                        DefAddRow ar;
                        ar.rowName = xmlGetAttr(op, "name");
                        ar.json = op.text;
                        if (!ar.rowName.empty() && !ar.json.empty())
                            mod.addRows.push_back(std::move(ar));
                    }
                }
                def.mods.push_back(std::move(mod));
            }
        }
    }
    else if (root.tag == "manifest")
    {


        VLOG(STR("[MoriaCppMod] [Def] Skipping manifest file (build-time only): {}\n"),
             utf8ToWide(defPath));
    }

    return def;
}


static std::string extractDataTableName(const std::string& filePath)
{

    size_t lastSlash = filePath.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;


    if (strEndsWithCI(filename, ".json"))
        filename = filename.substr(0, filename.size() - 5);

    return filename;
}


struct ResolvedField
{
    uint8_t* data{nullptr};
    FProperty* prop{nullptr};
};

ResolvedField resolveNestedProperty(uint8_t* rowData, UStruct* rowStruct, const std::string& propertyPath)
{
    if (!rowData || !rowStruct || propertyPath.empty()) return {};


    std::vector<std::string> segments;
    {
        std::string seg;
        for (char c : propertyPath)
        {
            if (c == '.') { if (!seg.empty()) segments.push_back(seg); seg.clear(); }
            else seg += c;
        }
        if (!seg.empty()) segments.push_back(seg);
    }

    uint8_t* currentData = rowData;
    UStruct* currentStruct = rowStruct;

    for (size_t i = 0; i < segments.size(); i++)
    {
        std::string& seg = segments[i];
        bool isLast = (i == segments.size() - 1);


        std::string fieldName = seg;
        int arrayIndex = -1;
        size_t bracket = seg.find('[');
        if (bracket != std::string::npos)
        {
            fieldName = seg.substr(0, bracket);
            size_t closeBracket = seg.find(']', bracket);
            if (closeBracket != std::string::npos)
            {
                std::string idxStr = seg.substr(bracket + 1, closeBracket - bracket - 1);
                try { arrayIndex = std::stoi(idxStr); } catch (...) { return {}; }
            }
        }


        std::wstring wFieldName(fieldName.begin(), fieldName.end());
        FProperty* foundProp = nullptr;
        for (auto* s = currentStruct; s; s = s->GetSuperStruct())
        {
            for (auto* prop : s->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(wFieldName))
                {
                    foundProp = prop;
                    break;
                }
            }
            if (foundProp) break;
        }

        if (!foundProp)
        {
            VLOG(STR("[MoriaCppMod] [Def] Property '{}' not found in struct\n"), wFieldName);
            return {};
        }

        int offset = foundProp->GetOffset_Internal();

        if (isLast && arrayIndex < 0)
        {

            return {currentData + offset, foundProp};
        }

        if (arrayIndex >= 0)
        {

            auto* arrProp = CastField<FArrayProperty>(foundProp);
            if (!arrProp)
            {
                VLOG(STR("[MoriaCppMod] [Def] Property '{}' is not an array\n"), wFieldName);
                return {};
            }

            uint8_t* arrayBase = currentData + offset;
            TArrayView arrayView(arrProp, arrayBase);

            if (arrayIndex >= arrayView.Num())
            {
                VLOG(STR("[MoriaCppMod] [Def] Array '{}' index {} out of range (Num={})\n"),
                     wFieldName, arrayIndex, arrayView.Num());
                return {};
            }

            uint8_t* elemData = arrayView.GetRawPtr(arrayIndex);
            if (!elemData) return {};

            if (isLast)
            {

                return {elemData, foundProp};
            }


            FProperty* inner = arrProp->GetInner();
            auto* structProp = inner ? CastField<FStructProperty>(inner) : nullptr;
            UStruct* innerStruct = structProp ? structProp->GetStruct() : nullptr;
            if (!innerStruct)
            {
                VLOG(STR("[MoriaCppMod] [Def] Array '{}' elements are not structs, cannot traverse further\n"), wFieldName);
                return {};
            }

            currentData = elemData;
            currentStruct = innerStruct;
        }
        else
        {

            auto* structProp = CastField<FStructProperty>(foundProp);
            if (!structProp || !structProp->GetStruct())
            {
                VLOG(STR("[MoriaCppMod] [Def] Property '{}' is not a struct, cannot traverse further\n"), wFieldName);
                return {};
            }
            currentData = currentData + offset;
            currentStruct = structProp->GetStruct();
        }
    }

    return {};
}


bool writeValueToField(uint8_t* fieldData, FProperty* prop, const std::string& value)
{
    if (!fieldData || !prop) return false;


    if (strEqualCI(value, "NULL") || value.empty())
    {
        try { prop->ClearValue(fieldData); return true; }
        catch (...) { return false; }
    }


    try {
        std::wstring wval(value.begin(), value.end());
        const TCHAR* result = prop->ImportText_Direct(wval.c_str(), fieldData, nullptr, 0, nullptr);
        if (result) return true;
    } catch (...) {}

    VLOG(STR("[MoriaCppMod] [Def] ImportText_Direct failed for value '{}'\n"),
         utf8ToWide(value));
    return false;
}


std::string readFieldAsString(uint8_t* fieldData, FProperty* prop)
{
    if (!fieldData || !prop) return "<null>";

    try
    {
        FString outStr{};
        prop->ExportText_Direct(outStr, fieldData, nullptr, nullptr, 0, nullptr);
        const wchar_t* ws = *outStr;
        if (!ws || ws[0] == L'\0') return "<empty>";
        std::string result;
        for (const wchar_t* p = ws; *p != L'\0'; ++p)
            result += static_cast<char>(*p);
        return result;
    }
    catch (...) { return "<error>"; }
}


bool removeGameplayTag(uint8_t* containerData, const std::string& tagName)
{
    if (!containerData) return false;


    DataTableUtil::TArrayHeader hdr;
    if (!isReadableMemory(containerData, 16)) return false;
    std::memcpy(&hdr, containerData, 16);
    if (hdr.Num <= 0 || !hdr.Data) return false;


    static constexpr int TAG_SIZE = 8;
    std::wstring wTagName(tagName.begin(), tagName.end());
    FName searchTag(wTagName.c_str(), FNAME_Find);

    for (int32_t i = 0; i < hdr.Num; i++)
    {
        uint8_t* elem = hdr.Data + i * TAG_SIZE;
        if (!isReadableMemory(elem, TAG_SIZE)) continue;
        if (std::memcmp(elem, &searchTag, TAG_SIZE) == 0)
        {

            int32_t lastIdx = hdr.Num - 1;
            if (i < lastIdx)
            {
                uint8_t* lastElem = hdr.Data + lastIdx * TAG_SIZE;
                std::memcpy(elem, lastElem, TAG_SIZE);
            }

            uint8_t* lastElem = hdr.Data + lastIdx * TAG_SIZE;
            std::memset(lastElem, 0, TAG_SIZE);

            int32_t newNum = hdr.Num - 1;
            std::memcpy(containerData + 8, &newNum, 4);

            VLOG(STR("[MoriaCppMod] [Def] Removed tag '{}' (slot {}, {} remaining)\n"),
                 wTagName, i, newNum);
            return true;
        }
    }

    VLOG(STR("[MoriaCppMod] [Def] Tag '{}' not found in container\n"), wTagName);
    return false;
}


static std::string jsonExtractString(const std::string& json, size_t start, size_t end, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, start);
    if (pos == std::string::npos || pos >= end) return "";
    pos += needle.size();

    while (pos < end && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) ++pos;
    if (pos >= end) return "";

    if (json[pos] == '"')
    {

        ++pos;
        size_t valEnd = pos;
        while (valEnd < end && json[valEnd] != '"') {
            if (json[valEnd] == '\\') valEnd++;
            valEnd++;
        }
        return json.substr(pos, valEnd - pos);
    }
    else if (json[pos] == '{' || json[pos] == '[')
    {

        char open = json[pos], close = (open == '{') ? '}' : ']';
        int depth = 1;
        size_t blockStart = pos;
        ++pos;
        while (pos < end && depth > 0) {
            if (json[pos] == '"') { ++pos; while (pos < end && json[pos] != '"') { if (json[pos] == '\\') ++pos; ++pos; } }
            else if (json[pos] == open) depth++;
            else if (json[pos] == close) depth--;
            ++pos;
        }
        return json.substr(blockStart, pos - blockStart);
    }
    else
    {

        size_t valStart = pos;
        while (pos < end && json[pos] != ',' && json[pos] != '}' && json[pos] != ']'
               && json[pos] != '\r' && json[pos] != '\n') ++pos;
        std::string val = json.substr(valStart, pos - valStart);

        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
        return val;
    }
}


static std::vector<std::pair<size_t, size_t>> jsonArrayObjects(const std::string& json, size_t arrStart, size_t arrEnd)
{
    std::vector<std::pair<size_t, size_t>> objects;
    size_t pos = arrStart;
    while (pos < arrEnd)
    {

        while (pos < arrEnd && json[pos] != '{') ++pos;
        if (pos >= arrEnd) break;
        size_t objStart = pos;
        int depth = 1;
        ++pos;
        while (pos < arrEnd && depth > 0) {
            if (json[pos] == '"') { ++pos; while (pos < arrEnd && json[pos] != '"') { if (json[pos] == '\\') ++pos; ++pos; } }
            else if (json[pos] == '{') depth++;
            else if (json[pos] == '}') depth--;
            ++pos;
        }
        objects.push_back({objStart, pos});
    }
    return objects;
}


int64_t findEnumValueByName(UEnum* uenum, const std::string& val)
{
    if (!uenum) return INDEX_NONE;
    std::wstring wVal(val.begin(), val.end());
    std::wstring wShort;
    size_t colonPos = val.rfind("::");
    if (colonPos != std::string::npos)
    {
        std::string shortVal = val.substr(colonPos + 2);
        wShort = utf8ToWide(shortVal);
    }
    for (auto& pair : uenum->ForEachName())
    {
        std::wstring enumName = pair.Key.ToString();
        if (enumName == wVal) return pair.Value;
        if (!wShort.empty() && enumName == wShort) return pair.Value;

        size_t enumColon = enumName.rfind(L"::");
        if (enumColon != std::wstring::npos)
        {
            std::wstring enumShort = enumName.substr(enumColon + 2);
            if (enumShort == wVal || (!wShort.empty() && enumShort == wShort))
                return pair.Value;
        }
    }
    return INDEX_NONE;
}


bool writeJsonPropertyToField(uint8_t* structData, UStruct* ustruct,
                               const std::string& json, size_t objStart, size_t objEnd)
{
    if (!structData || !ustruct) return false;

    std::string type = jsonExtractString(json, objStart, objEnd, "$type");
    std::string name = jsonExtractString(json, objStart, objEnd, "Name");
    if (name.empty()) return false;


    std::string isZero = jsonExtractString(json, objStart, objEnd, "IsZero");
    if (isZero == "true") return false;


    std::wstring wName(name.begin(), name.end());
    FProperty* prop = nullptr;
    for (auto* s = ustruct; s; s = s->GetSuperStruct())
    {
        for (auto* p : s->ForEachProperty())
        {
            if (p->GetName() == std::wstring_view(wName))
            {
                prop = p;
                break;
            }
        }
        if (prop) break;
    }

    if (!prop)
    {
        if (s_verbose)
        {
            std::wstring wType(type.begin(), type.end());
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]     SKIP '{}' ({}): not found in live struct\n"),
                wName, wType);
        }
        return false;
    }

    int offset = prop->GetOffset_Internal();
    uint8_t* fieldData = structData + offset;


    if (type.find("IntPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;
        return writeValueToField(fieldData, prop, val);
    }


    if (type.find("FloatPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;

        if (val == "+0" || val == "+0.0") val = "0";
        return writeValueToField(fieldData, prop, val);
    }


    if (type.find("BoolPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        return writeValueToField(fieldData, prop, val);
    }


    if (type.find("BytePropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;
        return writeValueToField(fieldData, prop, val);
    }


    if (type.find("NamePropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty() || val == "null" || val == "None") return false;
        return writeValueToField(fieldData, prop, val);
    }


    if (type.find("EnumPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty() || val == "null") return false;


        std::wstring className;
        try { className = prop->GetClass().GetName(); } catch (...) { return false; }

        if (className.find(L"EnumProperty") != std::wstring::npos)
        {


            auto* enumProp = CastField<FEnumProperty>(prop);
            if (enumProp)
            {
                auto* underlying = enumProp->GetUnderlyingProperty();
                if (underlying)
                {
                    UEnum* uenum = enumProp->GetEnum().Get();
                    int64_t idx = findEnumValueByName(uenum, val);
                    if (idx != INDEX_NONE)
                    {
                        int propSize = underlying->GetSize();
                        if (propSize == 1 && isReadableMemory(fieldData, 1))
                        {
                            fieldData[0] = static_cast<uint8_t>(idx);
                            return true;
                        }
                        else if (propSize == 4 && isReadableMemory(fieldData, 4))
                        {
                            int32_t v = static_cast<int32_t>(idx);
                            std::memcpy(fieldData, &v, 4);
                            return true;
                        }
                    }
                }
            }
            return false;
        }
        else if (className.find(L"ByteProperty") != std::wstring::npos)
        {

            size_t colonPos = val.rfind("::");
            if (colonPos != std::string::npos)
            {

                auto* byteProp = CastField<FByteProperty>(prop);
                if (byteProp)
                {
                    UEnum* uenum = byteProp->GetEnum().Get();
                    int64_t idx = findEnumValueByName(uenum, val);
                    if (idx != INDEX_NONE && isReadableMemory(fieldData, 1))
                    {
                        fieldData[0] = static_cast<uint8_t>(idx);
                        return true;
                    }
                }
            }

            try {
                uint8_t v = static_cast<uint8_t>(std::stoi(val));
                if (isReadableMemory(fieldData, 1)) { fieldData[0] = v; return true; }
            } catch (...) {}
            return false;
        }
        return false;
    }


    if (type.find("SoftObjectPropertyData") != std::string::npos)
    {


        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");
        if (valueBlock.empty() || valueBlock == "null") return false;


        std::string assetPath = jsonExtractString(valueBlock, 0, valueBlock.size(), "AssetPath");
        if (assetPath.empty()) return false;
        std::string assetName = jsonExtractString(assetPath, 0, assetPath.size(), "AssetName");
        if (assetName.empty() || assetName == "null" || assetName == "None") return false;


        std::wstring wPath(assetName.begin(), assetName.end());
        FName pathName(wPath.c_str(), FNAME_Add);


        if (isReadableMemory(fieldData, 8))
        {
            std::memcpy(fieldData, &pathName, 8);
            if (s_verbose)
            {
                std::wstring wAsset(assetName.begin(), assetName.end());
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def]     SoftObj '{}' = '{}' (FName written to offset {})\n"),
                    wName, wAsset, offset);
            }
            return true;
        }
        return false;
    }


    if (type.find("StructPropertyData") != std::string::npos)
    {
        std::string structType = jsonExtractString(json, objStart, objEnd, "StructType");
        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");
        if (valueBlock.empty() || valueBlock[0] != '[') return false;


        auto* structProp = CastField<FStructProperty>(prop);
        if (!structProp || !structProp->GetStruct()) return false;
        UStruct* innerStruct = structProp->GetStruct();


        if (structType == "GameplayTagContainer")
        {

            auto innerObjects = jsonArrayObjects(valueBlock, 0, valueBlock.size());
            for (auto& [iStart, iEnd] : innerObjects)
            {
                std::string iType = jsonExtractString(valueBlock, iStart, iEnd, "$type");
                if (iType.find("GameplayTagContainerPropertyData") != std::string::npos)
                {
                    std::string tagsBlock = jsonExtractString(valueBlock, iStart, iEnd, "Value");
                    if (tagsBlock.empty() || tagsBlock == "[]") break;


                    std::vector<std::string> tagNames;
                    size_t tPos = 1;
                    while (tPos < tagsBlock.size())
                    {
                        while (tPos < tagsBlock.size() && tagsBlock[tPos] != '"' && tagsBlock[tPos] != ']') ++tPos;
                        if (tPos >= tagsBlock.size() || tagsBlock[tPos] == ']') break;
                        ++tPos;
                        size_t tEnd = tPos;
                        while (tEnd < tagsBlock.size() && tagsBlock[tEnd] != '"') ++tEnd;
                        tagNames.push_back(tagsBlock.substr(tPos, tEnd - tPos));
                        tPos = tEnd + 1;
                    }

                    if (!tagNames.empty())
                    {

                        int tagCount = static_cast<int>(tagNames.size());
                        uint8_t* tagData = static_cast<uint8_t*>(FMemory::Malloc(tagCount * 8, 8));
                        if (tagData)
                        {
                            std::memset(tagData, 0, tagCount * 8);
                            for (int t = 0; t < tagCount; t++)
                            {
                                std::wstring wTag(tagNames[t].begin(), tagNames[t].end());
                                FName fn(wTag.c_str(), FNAME_Add);
                                std::memcpy(tagData + t * 8, &fn, 8);
                            }

                            if (isReadableMemory(fieldData, 16))
                            {
                                std::memcpy(fieldData, &tagData, 8);
                                std::memcpy(fieldData + 8, &tagCount, 4);
                                std::memcpy(fieldData + 12, &tagCount, 4);
                            }
                        }
                    }
                    break;
                }
            }
            return true;
        }


        if (structType == "GameplayTag")
        {
            auto innerObjects = jsonArrayObjects(valueBlock, 0, valueBlock.size());
            for (auto& [iStart, iEnd] : innerObjects)
            {
                std::string iName = jsonExtractString(valueBlock, iStart, iEnd, "Name");
                if (iName == "TagName")
                {
                    std::string tagVal = jsonExtractString(valueBlock, iStart, iEnd, "Value");
                    if (!tagVal.empty() && tagVal != "null" && tagVal != "None")
                    {

                        std::wstring wTag(tagVal.begin(), tagVal.end());
                        FName fn(wTag.c_str(), FNAME_Add);
                        if (isReadableMemory(fieldData, 8))
                            std::memcpy(fieldData, &fn, 8);
                    }
                    return true;
                }
            }
            return false;
        }


        auto innerObjects = jsonArrayObjects(valueBlock, 0, valueBlock.size());
        for (auto& [iStart, iEnd] : innerObjects)
        {
            writeJsonPropertyToField(fieldData, innerStruct, valueBlock, iStart, iEnd);
        }
        return true;
    }


    if (type.find("ObjectPropertyData") != std::string::npos)
    {
        if (s_verbose)
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]     SKIP '{}' (ObjectPropertyData): requires package resolution\n"), wName);
        return false;
    }


    if (type.find("TextPropertyData") != std::string::npos)
    {
        if (s_verbose)
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]     SKIP '{}' (TextPropertyData): requires StringTable binding\n"), wName);
        return false;
    }


    if (type.find("ArrayPropertyData") != std::string::npos)
    {
        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");

        if (valueBlock.empty() || valueBlock == "[]") return false;


        std::string arrayType = jsonExtractString(json, objStart, objEnd, "ArrayType");

        auto* arrProp = CastField<FArrayProperty>(prop);
        if (!arrProp) return false;
        FProperty* inner = arrProp->GetInner();
        if (!inner) return false;
        int elemSize = inner->GetSize();
        if (elemSize <= 0) return false;

        auto elements = jsonArrayObjects(valueBlock, 0, valueBlock.size());
        if (elements.empty()) return false;

        int count = static_cast<int>(elements.size());


        uint8_t* arrData = static_cast<uint8_t*>(FMemory::Malloc(count * elemSize, 8));
        if (!arrData) return false;


        for (int i = 0; i < count; i++)
            inner->InitializeValue(arrData + i * elemSize);

        auto* innerStructProp = CastField<FStructProperty>(inner);
        UStruct* innerStruct = innerStructProp ? innerStructProp->GetStruct() : nullptr;


        for (int i = 0; i < count; i++)
        {
            uint8_t* elemData = arrData + i * elemSize;
            auto& [eStart, eEnd] = elements[i];

            if (innerStruct)
            {

                std::string elemValue = jsonExtractString(valueBlock, eStart, eEnd, "Value");
                if (!elemValue.empty() && elemValue[0] == '[')
                {
                    auto innerProps = jsonArrayObjects(elemValue, 0, elemValue.size());
                    for (auto& [ipStart, ipEnd] : innerProps)
                    {
                        writeJsonPropertyToField(elemData, innerStruct,
                                                  elemValue, ipStart, ipEnd);
                    }
                }
            }
            else
            {

                std::string val = jsonExtractString(valueBlock, eStart, eEnd, "Value");
                if (!val.empty())
                    writeValueToField(elemData, inner, val);
            }
        }


        std::memcpy(fieldData, &arrData, 8);
        std::memcpy(fieldData + 8, &count, 4);
        std::memcpy(fieldData + 12, &count, 4);
        return true;
    }


    if (type.find("MapPropertyData") != std::string::npos)
        return false;


    if (s_verbose)
    {
        std::wstring wType(type.begin(), type.end());
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]     SKIP '{}' ({}): unhandled type\n"), wName, wType);
    }
    return false;
}


void fixRowHandlePointers(DataTableUtil& dt, uint8_t* newRow)
{
    if (!dt.rowStruct || !newRow) return;


    DataTableUtil::RowMapHeader hdr{};
    if (!dt.getRowMapHeader(hdr) || !hdr.Data || hdr.Num < 1) return;
    if (!isReadableMemory(hdr.Data, DataTableUtil::SET_ELEMENT_SIZE)) return;
    uint8_t* refRow = *reinterpret_cast<uint8_t**>(hdr.Data + DataTableUtil::FNAME_SIZE);
    if (!refRow || !isReadableMemory(refRow, dt.rowSize)) return;

    int fixed = 0;

    for (auto* s = dt.rowStruct; s; s = s->GetSuperStruct())
    {
        for (auto* p : s->ForEachProperty())
        {
            auto* sp = CastField<FStructProperty>(p);
            if (!sp) continue;
            UStruct* innerStruct = sp->GetStruct();
            if (!innerStruct) continue;


            std::wstring structName = innerStruct->GetName();
            if (structName.find(L"RowHandle") == std::wstring::npos &&
                structName.find(L"DataTableRowHandle") == std::wstring::npos)
                continue;

            int off = p->GetOffset_Internal();
            int sz = p->GetSize();
            if (off < 0 || sz < 16) continue;


            uint64_t newPtr = 0, refPtr = 0;
            if (!isReadableMemory(newRow + off, 8) || !isReadableMemory(refRow + off, 8))
                continue;
            std::memcpy(&newPtr, newRow + off, 8);
            std::memcpy(&refPtr, refRow + off, 8);

            if (newPtr != refPtr && refPtr != 0)
            {
                std::memcpy(newRow + off, refRow + off, 8);
                fixed++;
                if (s_verbose)
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     RowHandle fix: '{}' @{} was=0x{:016X} now=0x{:016X}\n"),
                        p->GetName(), off, newPtr, refPtr);
            }
        }
    }


    for (auto* s = dt.rowStruct; s; s = s->GetSuperStruct())
    {
        for (auto* p : s->ForEachProperty())
        {
            auto* arrProp = CastField<FArrayProperty>(p);
            if (!arrProp) continue;
            auto* innerSP = CastField<FStructProperty>(arrProp->GetInner());
            if (!innerSP) continue;
            UStruct* elemStruct = innerSP->GetStruct();
            if (!elemStruct) continue;
            int elemSize = innerSP->GetSize();
            if (elemSize <= 0) continue;


            bool hasRowHandle = false;
            for (auto* es = elemStruct; es && !hasRowHandle; es = es->GetSuperStruct())
                for (auto* ep : es->ForEachProperty())
                {
                    auto* esp = CastField<FStructProperty>(ep);
                    if (!esp || !esp->GetStruct()) continue;
                    std::wstring esName = esp->GetStruct()->GetName();
                    if (esName.find(L"RowHandle") != std::wstring::npos) { hasRowHandle = true; break; }
                }
            if (!hasRowHandle) continue;

            int arrOff = p->GetOffset_Internal();


            TArrayView newArr(arrProp, newRow + arrOff);
            TArrayView refArr(arrProp, refRow + arrOff);

            if (newArr.Num() <= 0 || refArr.Num() <= 0) continue;

            uint8_t* refElem0 = refArr.GetRawPtr(0);
            if (!refElem0) continue;

            for (auto* es = elemStruct; es; es = es->GetSuperStruct())
            {
                for (auto* ep : es->ForEachProperty())
                {
                    auto* esp = CastField<FStructProperty>(ep);
                    if (!esp || !esp->GetStruct()) continue;
                    std::wstring esName = esp->GetStruct()->GetName();
                    if (esName.find(L"RowHandle") == std::wstring::npos) continue;

                    int subOff = ep->GetOffset_Internal();
                    if (subOff < 0 || ep->GetSize() < 16) continue;


                    uint64_t refSubPtr = 0;
                    std::memcpy(&refSubPtr, refElem0 + subOff, 8);
                    if (refSubPtr == 0) continue;


                    for (int i = 0; i < newArr.Num(); i++)
                    {
                        uint8_t* elemBase = newArr.GetRawPtr(i);
                        if (!elemBase) continue;
                        uint64_t curPtr = 0;
                        std::memcpy(&curPtr, elemBase + subOff, 8);
                        if (curPtr != refSubPtr)
                        {
                            std::memcpy(elemBase + subOff, &refSubPtr, 8);
                            fixed++;
                        }
                    }
                    if (s_verbose && fixed > 0)
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     RowHandle fix (array): '{}.{}' DataTable*=0x{:016X}\n"),
                            p->GetName(), ep->GetName(), refSubPtr);
                }
            }
        }
    }

    if (fixed > 0)
        VLOG(STR("[MoriaCppMod] [Def]   RowHandle DataTable* pointers fixed: {}\n"), fixed);
}


int applyAddRow(DataTableUtil& dt, const DefAddRow& addRow)
{
    if (!dt.isBound() || !dt.rowStruct || dt.rowSize <= 0) return 0;

    std::wstring wRowName(addRow.rowName.begin(), addRow.rowName.end());


    if (dt.findRowData(wRowName.c_str()))
    {
        VLOG(STR("[MoriaCppMod] [Def] add_row: '{}' already exists in '{}', skipping\n"),
             wRowName, dt.tableName);
        return 0;
    }


    uint8_t* rowData = dt.addRow(wRowName.c_str());
    if (!rowData)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] add_row: FAILED to add '{}' to '{}'\n"),
            wRowName, dt.tableName);
        return 0;
    }


    const std::string& json = addRow.json;
    std::string valueBlock = jsonExtractString(json, 0, json.size(), "Value");
    if (valueBlock.empty() || valueBlock[0] != '[')
    {
        VLOG(STR("[MoriaCppMod] [Def] add_row: '{}' has no Value array\n"), wRowName);
        return 1;
    }

    auto properties = jsonArrayObjects(valueBlock, 0, valueBlock.size());
    int propsWritten = 0;
    for (auto& [pStart, pEnd] : properties)
    {
        if (writeJsonPropertyToField(rowData, dt.rowStruct, valueBlock, pStart, pEnd))
            propsWritten++;
    }


    {
        FName rowFName(wRowName.c_str(), FNAME_Add);
        std::memcpy(rowData + 0x08, &rowFName, sizeof(FName));
    }


    fixRowHandlePointers(dt, rowData);

    if (s_verbose)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]   + {} ({} props written)\n"),
            wRowName, propsWritten);
    }

    return 1;
}


int applyChange(DataTableUtil& dt, const DefChange& change)
{
    if (!dt.isBound()) return 0;

    bool isSimple = (change.property.find('.') == std::string::npos &&
                     change.property.find('[') == std::string::npos);
    bool isNone = strEqualCI(change.item, "NONE");
    int applied = 0;

    auto applyToRow = [&](const wchar_t* rowName) -> bool
    {
        if (isSimple)
        {

            std::wstring wProp(change.property.begin(), change.property.end());
            int off = dt.resolvePropertyOffset(wProp.c_str());
            if (off < 0) return false;

            uint8_t* rowData = dt.findRowData(rowName);
            if (!rowData) return false;


            FProperty* prop = nullptr;
            if (dt.rowStruct)
            {
                for (auto* s = dt.rowStruct; s; s = s->GetSuperStruct())
                {
                    for (auto* p : s->ForEachProperty())
                    {
                        if (p->GetName() == std::wstring_view(wProp))
                        {
                            prop = p;
                            break;
                        }
                    }
                    if (prop) break;
                }
            }

            if (prop)
            {
                bool ok = writeValueToField(rowData + off, prop, change.value);
                if (ok && s_verbose)
                {
                    std::string readback = readFieldAsString(rowData + off, prop);
                    std::wstring wReadback(readback.begin(), readback.end());
                    std::wstring wVal(change.value.begin(), change.value.end());
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]   {} . {} = {} (readback: {})\n"),
                        rowName, wProp, wVal, wReadback);
                }
                return ok;
            }


            if (change.value.find('.') != std::string::npos)
            {
                try { return dt.writeFloat(rowName, wProp.c_str(), std::stof(change.value)); }
                catch (...) { return false; }
            }
            else
            {
                try { return dt.writeInt32(rowName, wProp.c_str(), std::stoi(change.value)); }
                catch (...) { return false; }
            }
        }
        else
        {

            uint8_t* rowData = dt.findRowData(rowName);
            if (!rowData || !dt.rowStruct) return false;

            auto resolved = resolveNestedProperty(rowData, dt.rowStruct, change.property);
            if (!resolved.data) return false;

            bool ok = writeValueToField(resolved.data, resolved.prop, change.value);
            if (ok && s_verbose)
            {
                std::string readback = readFieldAsString(resolved.data, resolved.prop);
                std::wstring wReadback(readback.begin(), readback.end());
                std::wstring wItem(change.item.begin(), change.item.end());
                std::wstring wProp(change.property.begin(), change.property.end());
                std::wstring wVal(change.value.begin(), change.value.end());
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def]   {} . {} = {} (readback: {})\n"),
                    wItem, wProp, wVal, wReadback);
            }
            return ok;
        }
    };

    if (isNone)
    {

        auto names = dt.getRowNames();
        for (auto& name : names)
        {
            if (applyToRow(name.c_str())) applied++;
        }
    }
    else
    {
        std::wstring wItem(change.item.begin(), change.item.end());
        if (applyToRow(wItem.c_str())) applied++;
    }

    return applied;
}

int applyDelete(DataTableUtil& dt, const DefDelete& del)
{
    if (!dt.isBound()) return 0;

    std::wstring wItem(del.item.begin(), del.item.end());
    std::wstring wProp(del.property.begin(), del.property.end());

    uint8_t* rowData = dt.findRowData(wItem.c_str());
    if (!rowData) return 0;

    int off = dt.resolvePropertyOffset(wProp.c_str());
    if (off < 0) return 0;

    bool ok = removeGameplayTag(rowData + off, del.value);
    if (ok && s_verbose)
    {

        int32_t tagCount = 0;
        std::memcpy(&tagCount, rowData + off + 8, 4);
        std::wstring wVal(del.value.begin(), del.value.end());
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]   {} . {} -= '{}' (tags remaining: {})\n"),
            wItem, wProp, wVal, tagCount);
    }
    return ok ? 1 : 0;
}


DataTableUtil& getOrBindDataTable(const std::string& dtName, std::unordered_map<std::string, DataTableUtil>& dynamicTables)
{

    static const std::unordered_map<std::string, DataTableUtil*> knownTables = {
        {"DT_Constructions", &m_dtConstructions},
        {"DT_ConstructionRecipes", &m_dtConstructionRecipes},
        {"DT_Items", &m_dtItems},
        {"DT_Weapons", &m_dtWeapons},
        {"DT_Tools", &m_dtTools},
        {"DT_Armor", &m_dtArmor},
        {"DT_Consumables", &m_dtConsumables},
        {"DT_ContainerItems", &m_dtContainerItems},
        {"DT_Ores", &m_dtOres},
    };


    for (auto& [name, dt] : knownTables)
    {
        if (strEqualCI(name, dtName) && dt->isBound())
            return *dt;
    }


    auto it = dynamicTables.find(dtName);
    if (it != dynamicTables.end() && it->second.isBound())
        return it->second;

    auto& dt = dynamicTables[dtName];
    std::wstring wName(dtName.begin(), dtName.end());
    dt.bind(wName.c_str());
    return dt;
}


static inline std::string gameModsIniPath() { return modPath("Mods/GameMods.ini"); }
static inline std::string definitionsDir() { return modPath("Mods/MoriaCppMod/definitions"); }

struct GameModEntry
{
    std::string name;
    std::string title;
    std::string description;
    bool enabled{false};
};


std::vector<GameModEntry> discoverGameMods()
{
    std::vector<GameModEntry> entries;


    std::unordered_map<std::string, bool> enabledMap;
    {
        std::ifstream file = openInputFile(gameModsIniPath());
        if (file.is_open())
        {
            std::string section, line;
            while (std::getline(file, line))
            {
                auto parsed = parseIniLine(line);
                if (auto* sec = std::get_if<ParsedIniSection>(&parsed))
                    section = sec->name;
                else if (auto* kv = std::get_if<ParsedIniKeyValue>(&parsed))
                {
                    if (strEqualCI(section, "EnabledMods"))
                        enabledMap[kv->key] = strEqualCI(kv->value, "true");
                }
            }
        }
    }


    auto iniFiles = listFiles(definitionsDir(), "*.ini");
    for (auto& iniFile : iniFiles)
    {

        std::string name = iniFile;
        if (strEndsWithCI(name, ".ini"))
            name = name.substr(0, name.size() - 4);


        std::string iniPath = definitionsDir() + "\\" + iniFile;
        DefManifest manifest = parseManifest(iniPath, definitionsDir());


        if (manifest.defPaths.empty()) continue;

        GameModEntry entry;
        entry.name = name;
        entry.title = manifest.title.empty() ? name : manifest.title;
        entry.description = manifest.description;


        auto it = enabledMap.find(name);
        entry.enabled = (it != enabledMap.end()) ? it->second : false;

        entries.push_back(std::move(entry));
    }

    return entries;
}


void saveGameMods(const std::vector<GameModEntry>& entries)
{
    std::ofstream file = openOutputFile(gameModsIniPath(), std::ios::trunc);
    if (!file.is_open())
    {
        RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] Failed to write Mods/GameMods.ini\n"));
        return;
    }

    file << "; Game Mods Configuration\n";
    file << "; Managed by MoriaCppMod — changes take effect on next game launch\n\n";
    file << "[EnabledMods]\n";
    for (auto& e : entries)
        file << e.name << " = " << (e.enabled ? "true" : "false") << "\n";
    file.flush();
}


static std::vector<std::string> readEnabledMods(const std::string& gameModsPath)
{
    std::vector<std::string> enabled;
    std::ifstream file = openInputFile(gameModsPath);
    if (!file.is_open()) return enabled;

    std::string section;
    std::string line;
    while (std::getline(file, line))
    {
        auto parsed = parseIniLine(line);
        if (auto* sec = std::get_if<ParsedIniSection>(&parsed))
        {
            section = sec->name;
        }
        else if (auto* kv = std::get_if<ParsedIniKeyValue>(&parsed))
        {
            if (strEqualCI(section, "EnabledMods") && strEqualCI(kv->value, "true"))
                enabled.push_back(kv->key);
        }
    }
    return enabled;
}


void loadAndApplyDefinitions()
{

    auto enabledMods = readEnabledMods(gameModsIniPath());
    if (enabledMods.empty())
    {
        VLOG(STR("[MoriaCppMod] [Def] No mods enabled in Mods/GameMods.ini (or file not found)\n"));
        return;
    }


    // v6.4.3 (hotfix) — wide API for Steam ™ path compatibility
    WIN32_FIND_DATAW fd;
    std::wstring testPathW = utf8PathToWide(definitionsDir() + "\\*");
    HANDLE hTest = FindFirstFileW(testPathW.c_str(), &fd);
    if (hTest == INVALID_HANDLE_VALUE)
    {
        VLOG(STR("[MoriaCppMod] [Def] No definitions directory found at Mods/MoriaCppMod/definitions/\n"));
        return;
    }
    FindClose(hTest);

    RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] {} mods enabled in GameMods.ini\n"),
        enabledMods.size());

    int totalManifests = 0;
    int totalAddRows = 0;
    int totalChanges = 0;
    int totalDeletes = 0;
    int totalApplied = 0;
    std::unordered_map<std::string, DataTableUtil> dynamicTables;

    std::unordered_map<std::string, std::string> tablesWithAddRows;

    for (auto& modName : enabledMods)
    {

        std::string iniPath = definitionsDir() + "\\" + modName + ".ini";
        std::ifstream testFile = openInputFile(iniPath);
        if (!testFile.is_open())
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Manifest '{}' not found at {}\n"),
                utf8ToWide(modName),
                utf8ToWide(iniPath));
            continue;
        }
        testFile.close();

        DefManifest manifest = parseManifest(iniPath, definitionsDir());
        if (manifest.defPaths.empty())
        {
            VLOG(STR("[MoriaCppMod] [Def] Manifest '{}' has no .def paths, skipping\n"),
                 utf8ToWide(modName));
            continue;
        }

        totalManifests++;
        std::string displayName = manifest.title.empty() ? modName : manifest.title;
        RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] Loading '{}' ({} defs)\n"),
            utf8ToWide(displayName),
            manifest.defPaths.size());

        for (auto& defPath : manifest.defPaths)
        {
            DefDefinition def = parseDef(defPath);
            if (def.mods.empty()) continue;

            for (auto& mod : def.mods)
            {
                std::string dtName = extractDataTableName(mod.filePath);
                if (dtName.empty())
                {
                    VLOG(STR("[MoriaCppMod] [Def] Cannot extract DT name from '{}'\n"),
                         utf8ToWide(mod.filePath));
                    continue;
                }

                DataTableUtil& dt = getOrBindDataTable(dtName, dynamicTables);
                if (!dt.isBound())
                {
                    VLOG(STR("[MoriaCppMod] [Def] DataTable '{}' not found in game — skipping\n"),
                         utf8ToWide(dtName));
                    continue;
                }


                for (auto& ar : mod.addRows)
                {
                    totalAddRows++;
                    int ok = applyAddRow(dt, ar);
                    totalApplied += ok;

                    if (tablesWithAddRows.find(dtName) == tablesWithAddRows.end())
                        tablesWithAddRows[dtName] = ar.rowName;
                }


                for (auto& del : mod.deletes)
                {
                    totalDeletes++;
                    int n = applyDelete(dt, del);
                    totalApplied += n;
                }


                for (auto& change : mod.changes)
                {
                    totalChanges++;
                    int n = applyChange(dt, change);
                    totalApplied += n;
                }
            }
        }
    }


    if (!tablesWithAddRows.empty())
    {

        auto hexDump = [](const uint8_t* data, int len) -> std::wstring {
            std::wstring hex;
            for (int b = 0; b < len; b++) {
                wchar_t buf[8]; swprintf(buf, 8, L"%02X ", data[b]);
                hex += buf;
            }
            return hex;
        };


        UObject* dtFuncLib = nullptr;
        UFunction* doesRowExistFn = nullptr;
        {
            doesRowExistFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr,
                STR("/Script/Engine.DataTableFunctionLibrary:DoesDataTableRowExist"));
            dtFuncLib = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                STR("/Script/Engine.Default__DataTableFunctionLibrary"));
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] === ADD_ROW VERIFICATION === (hash check: {})\n"),
            doesRowExistFn ? STR("AVAILABLE") : STR("NOT AVAILABLE"));

        for (auto& [dtName, firstRow] : tablesWithAddRows)
        {
            auto it = dynamicTables.find(dtName);
            if (it == dynamicTables.end() || !it->second.isBound()) continue;
            DataTableUtil& dt = it->second;

            int32_t totalRows = dt.getRowCount();
            std::wstring wDtName(dtName.begin(), dtName.end());
            std::wstring wFirstRow(firstRow.begin(), firstRow.end());


            {
                auto* dtBase = reinterpret_cast<uint8_t*>(dt.table);


                if (isReadableMemory(dtBase + 0x28, 0x78))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]   UDataTable obj @ 0x{:016X}\n"),
                        reinterpret_cast<uint64_t>(dtBase));
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     +0x28 (RowStruct*):  {}\n"),
                        hexDump(dtBase + 0x28, 8));
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     +0x30 (RowMap):      {}\n"),
                        hexDump(dtBase + 0x30, 24));
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     +0x48..+0x60:        {}\n"),
                        hexDump(dtBase + 0x48, 24));
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     +0x60..+0x80:        {}\n"),
                        hexDump(dtBase + 0x60, 32));
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     +0x80..+0xA0:        {}\n"),
                        hexDump(dtBase + 0x80, 32));
                }

                try {
                    auto* outermost = dt.table->GetOutermost();
                    if (outermost)
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     Package: {}\n"),
                            outermost->GetPathName());
                } catch (...) {}

                if (dt.rowStruct)
                {
                    try {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     RowStruct: {} (size={})\n"),
                            dt.rowStruct->GetFullName(), dt.rowSize);
                    } catch (...) {}
                }
            }


            {
                std::vector<UObject*> fgkBases;
                findAllOfSafe(STR("FGKDataTableBase"), fgkBases); // v6.11.0 — SEH-wrapped
                if (!fgkBases.empty())
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     FGKDataTableBase instances: {}\n"),
                        fgkBases.size());
                    for (auto* fgkBase : fgkBases)
                    {
                        auto* fgkBytes = reinterpret_cast<uint8_t*>(fgkBase);
                        if (!isReadableMemory(fgkBytes + 0x28, 0x18)) continue;
                        UObject* tableAsset = nullptr;
                        UObject* dynamicAsset = nullptr;
                        std::memcpy(&tableAsset, fgkBytes + 0x28, 8);
                        std::memcpy(&dynamicAsset, fgkBytes + 0x38, 8);
                        if (tableAsset == dt.table)
                        {
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     >>> FGKDataTableBase FOUND for '{}'\n"),
                                wDtName);
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]       TableAsset @0x28: 0x{:016X}\n"),
                                reinterpret_cast<uint64_t>(tableAsset));
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]       TestTableAsset @0x30: {}\n"),
                                hexDump(fgkBytes + 0x30, 8));
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]       DynamicTableAsset @0x38: 0x{:016X} ({})\n"),
                                reinterpret_cast<uint64_t>(dynamicAsset),
                                dynamicAsset ? STR("HAS DYNAMIC TABLE") : STR("NULL"));
                            if (dynamicAsset)
                            {


                                auto* dynBytes = reinterpret_cast<uint8_t*>(dynamicAsset);
                                if (isReadableMemory(dynBytes + 0x30, 16))
                                {
                                    DataTableUtil::RowMapHeader dynHdr{};
                                    std::memcpy(&dynHdr, dynBytes + 0x30, 16);
                                    RC::Output::send<RC::LogLevel::Warning>(
                                        STR("[MoriaCppMod] [Def]       DynamicTable RowMap: Num={} Max={}\n"),
                                        dynHdr.Num, dynHdr.Max);
                                }
                            }
                            break;
                        }
                    }
                }
                else
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     FGKDataTableBase: no instances found (may not be instantiated yet)\n"));
                }
            }


            uint8_t* rowData = dt.findRowData(wFirstRow.c_str());
            bool foundLinear = (rowData != nullptr);


            bool foundHash = false;
            bool hashTestRan = false;
            if (doesRowExistFn && dtFuncLib && dt.table)
            {
                struct { UObject* Table; FName RowName; bool ReturnValue; } params{};
                params.Table = dt.table;
                params.RowName = FName(wFirstRow.c_str(), FNAME_Find);
                params.ReturnValue = false;
                try {
                    safeProcessEvent(dtFuncLib, doesRowExistFn, &params);
                    foundHash = params.ReturnValue;
                    hashTestRan = true;
                } catch (...) {}
            }


            bool controlHash = false;
            bool controlRan = false;
            if (doesRowExistFn && dtFuncLib && dt.table && totalRows > 0)
            {
                DataTableUtil::RowMapHeader hdr{};
                if (dt.getRowMapHeader(hdr) && hdr.Data && hdr.Num > 0
                    && isReadableMemory(hdr.Data, DataTableUtil::SET_ELEMENT_SIZE))
                {
                    FName controlFName;
                    std::memcpy(&controlFName, hdr.Data, DataTableUtil::FNAME_SIZE);
                    struct { UObject* Table; FName RowName; bool ReturnValue; } ctrlP{};
                    ctrlP.Table = dt.table;
                    ctrlP.RowName = controlFName;
                    ctrlP.ReturnValue = false;
                    try {
                        safeProcessEvent(dtFuncLib, doesRowExistFn, &ctrlP);
                        controlHash = ctrlP.ReturnValue;
                        controlRan = true;
                    } catch (...) {}
                }
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]   {} : rows={} linear={} hash={} control={}\n"),
                wDtName, totalRows,
                foundLinear ? STR("YES") : STR("NO"),
                hashTestRan ? (foundHash ? STR("YES") : STR("NO")) : STR("N/A"),
                controlRan ? (controlHash ? STR("YES") : STR("NO")) : STR("N/A"));


            if (foundLinear && dtName == "DT_Constructions")
            {
                int actorOff = dt.resolvePropertyOffset(L"Actor");
                int tagsOff  = dt.resolvePropertyOffset(L"Tags");
                int enableOff = dt.resolvePropertyOffset(L"EnabledState");

                if (actorOff >= 0 && isReadableMemory(rowData + actorOff, 24))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     Actor @{} raw: {}\n"),
                        actorOff, hexDump(rowData + actorOff, 24));

                if (tagsOff >= 0 && isReadableMemory(rowData + tagsOff, 16))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     Tags @{} raw: {}\n"),
                        tagsOff, hexDump(rowData + tagsOff, 16));

                if (enableOff >= 0 && isReadableMemory(rowData + enableOff, 1))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     EnabledState @{}: {}\n"),
                        enableOff, rowData[enableOff]);


                if (isReadableMemory(rowData, 32))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     Row first 32B: {}\n"),
                        hexDump(rowData, 32));


                DataTableUtil::RowMapHeader hdr{};
                if (dt.getRowMapHeader(hdr) && hdr.Data && hdr.Num > 0
                    && isReadableMemory(hdr.Data, DataTableUtil::SET_ELEMENT_SIZE))
                {

                    uint8_t* existingRow = *reinterpret_cast<uint8_t**>(hdr.Data + DataTableUtil::FNAME_SIZE);
                    if (existingRow && isReadableMemory(existingRow, dt.rowSize))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     --- EXISTING row[0] for comparison ---\n"));
                        if (actorOff >= 0 && isReadableMemory(existingRow + actorOff, 24))
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     Actor @{} raw: {}\n"),
                                actorOff, hexDump(existingRow + actorOff, 24));
                        if (tagsOff >= 0 && isReadableMemory(existingRow + tagsOff, 16))
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     Tags @{} raw: {}\n"),
                                tagsOff, hexDump(existingRow + tagsOff, 16));
                        if (enableOff >= 0 && isReadableMemory(existingRow + enableOff, 1))
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     EnabledState @{}: {}\n"),
                                enableOff, existingRow[enableOff]);
                        if (isReadableMemory(existingRow, 32))
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     Row first 32B: {}\n"),
                                hexDump(existingRow, 32));
                    }
                }
            }

            if (foundLinear && dtName == "DT_ConstructionRecipes")
            {
                int resultOff = dt.resolvePropertyOffset(L"ResultConstructionHandle");
                int bOnFloorOff = dt.resolvePropertyOffset(L"bOnFloor");

                if (resultOff >= 0 && isReadableMemory(rowData + resultOff, 16))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     ADDED ResultConstructionHandle @{} raw: {}\n"),
                        resultOff, hexDump(rowData + resultOff, 16));

                    uint64_t dtPtr = 0;
                    std::memcpy(&dtPtr, rowData + resultOff, 8);
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]       bytes[0-7] (DataTable*?): 0x{:016X} ({})\n"),
                        dtPtr, dtPtr ? STR("NON-NULL") : STR("NULL"));
                }

                if (bOnFloorOff >= 0 && isReadableMemory(rowData + bOnFloorOff, 1))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     bOnFloor @{}: {}\n"),
                        bOnFloorOff, rowData[bOnFloorOff]);


                DataTableUtil::RowMapHeader hdr2{};
                if (dt.getRowMapHeader(hdr2) && hdr2.Data && hdr2.Num > 0
                    && isReadableMemory(hdr2.Data, DataTableUtil::SET_ELEMENT_SIZE))
                {
                    uint8_t* existRow = *reinterpret_cast<uint8_t**>(hdr2.Data + DataTableUtil::FNAME_SIZE);
                    if (existRow && isReadableMemory(existRow, dt.rowSize))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     --- EXISTING row[0] for comparison ---\n"));
                        if (resultOff >= 0 && isReadableMemory(existRow + resultOff, 16))
                        {
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]     EXISTING ResultConstructionHandle @{} raw: {}\n"),
                                resultOff, hexDump(existRow + resultOff, 16));
                            uint64_t existDtPtr = 0;
                            std::memcpy(&existDtPtr, existRow + resultOff, 8);
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def]       bytes[0-7] (DataTable*?): 0x{:016X} ({})\n"),
                                existDtPtr, existDtPtr ? STR("NON-NULL") : STR("NULL"));
                        }
                    }
                }
            }
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] === END VERIFICATION ===\n"));
    }


    for (auto& [name, dt] : dynamicTables)
        dt.unbind();

    RC::Output::send<RC::LogLevel::Warning>(
        STR("[MoriaCppMod] [Def] Done: {} manifests, {} add_rows + {} changes + {} deletes = {} applied\n"),
        totalManifests, totalAddRows, totalChanges, totalDeletes, totalApplied);
}
