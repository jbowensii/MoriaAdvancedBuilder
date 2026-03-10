// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_DefinitionProcessing.inl — Definition file loading & runtime apply ║
// ║  Included inside the mod class body, after moria_datatable.inl.           ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
//
// Reads .ini manifests and .def XML files from the definitions/ directory,
// then applies <change> and <delete> operations to live DataTables at runtime
// using the existing DataTableUtil infrastructure.
//
// Supports:
//   <change item="RowName" property="PropName" value="123" />
//   <change item="NONE" property="PropName" value="123" />       (all rows)
//   <change item="RowName" property="PropName" value="NULL" />   (clear/zero)
//   <change item="X" property="Nested[2].Field" value="..." />   (nested path)
//   <delete item="RowName" property="TagContainer" value="Tag.Name" />

// ════════════════════════════════════════════════════════════════════════════════
// Data Structures
// ════════════════════════════════════════════════════════════════════════════════

struct DefChange
{
    std::string item;       // row name or "NONE" for all rows
    std::string property;   // property path (may contain [N] and dots)
    std::string value;      // value string or "NULL" for clear
};

struct DefDelete
{
    std::string item;       // row name
    std::string property;   // GameplayTagContainer property name
    std::string value;      // tag to remove (e.g. "Item.EpicPack")
};

struct DefAddRow
{
    std::string rowName;    // new row name (from name="..." attribute)
    std::string json;       // full JSON CDATA block (UAssetAPI format)
};

struct DefMod
{
    std::string filePath;               // original JSON path (used to derive DT name)
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
    std::vector<std::string> defPaths;  // relative paths to .def files (pipe-separated → slash)
};

// ════════════════════════════════════════════════════════════════════════════════
// Utility: case-insensitive string ends-with
// ════════════════════════════════════════════════════════════════════════════════

static bool strEndsWithCI(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
        [](char a, char b) { return tolower(static_cast<unsigned char>(a)) == tolower(static_cast<unsigned char>(b)); });
}

static std::string strToLower(const std::string& s)
{
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return r;
}

// ════════════════════════════════════════════════════════════════════════════════
// Lightweight XML Parser (handles elements, attributes, self-closing tags)
// ════════════════════════════════════════════════════════════════════════════════

struct XmlAttribute
{
    std::string name;
    std::string value;
};

struct XmlElement
{
    std::string tag;
    std::vector<XmlAttribute> attrs;
    std::string text;                    // text content between open/close
    std::vector<XmlElement> children;
    bool selfClosing{false};
};

static std::string xmlGetAttr(const XmlElement& elem, const std::string& name)
{
    for (auto& a : elem.attrs)
        if (a.name == name) return a.value;
    return "";
}

// Skip whitespace, return new position
static size_t xmlSkipWS(const std::string& xml, size_t pos)
{
    while (pos < xml.size() && (xml[pos] == ' ' || xml[pos] == '\t' || xml[pos] == '\r' || xml[pos] == '\n'))
        ++pos;
    return pos;
}

// Parse a quoted attribute value, return new position after closing quote
static size_t xmlParseAttrValue(const std::string& xml, size_t pos, std::string& out)
{
    if (pos >= xml.size()) return pos;
    char quote = xml[pos];
    if (quote != '"' && quote != '\'') return pos;
    ++pos;
    size_t start = pos;
    while (pos < xml.size() && xml[pos] != quote) ++pos;
    out = xml.substr(start, pos - start);
    if (pos < xml.size()) ++pos; // skip closing quote
    // Unescape basic XML entities
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

// Parse attributes until '>' or '/>' is found, return new position
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
        // Parse attribute name
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

// Recursive element parser
static size_t xmlParseElement(const std::string& xml, size_t pos, XmlElement& elem)
{
    pos = xmlSkipWS(xml, pos);
    if (pos >= xml.size() || xml[pos] != '<') return pos;

    // Skip XML declaration and comments
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

    ++pos; // skip '<'
    // Parse tag name
    size_t tagStart = pos;
    while (pos < xml.size() && xml[pos] != ' ' && xml[pos] != '>' && xml[pos] != '/' && xml[pos] != '\t' && xml[pos] != '\r' && xml[pos] != '\n') ++pos;
    elem.tag = xml.substr(tagStart, pos - tagStart);

    // Parse attributes
    bool selfClose = false;
    pos = xmlParseAttrs(xml, pos, elem.attrs, selfClose);
    elem.selfClosing = selfClose;
    if (selfClose) return pos;

    // Parse children and text content until closing tag
    std::string closeTag = "</" + elem.tag;
    while (pos < xml.size())
    {
        pos = xmlSkipWS(xml, pos);
        if (pos >= xml.size()) break;

        // Check for closing tag
        if (pos + closeTag.size() < xml.size() && xml.substr(pos, closeTag.size()) == closeTag)
        {
            pos += closeTag.size();
            pos = xmlSkipWS(xml, pos);
            if (pos < xml.size() && xml[pos] == '>') ++pos;
            return pos;
        }

        if (xml[pos] == '<')
        {
            // CDATA section: <![CDATA[...]]>
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
            // Skip comments inside elements
            if (pos + 3 < xml.size() && xml.substr(pos, 4) == "<!--")
            {
                size_t end = xml.find("-->", pos);
                pos = (end != std::string::npos) ? end + 3 : xml.size();
                continue;
            }
            // Skip declarations
            if (pos + 1 < xml.size() && xml[pos + 1] == '?')
            {
                size_t end = xml.find("?>", pos);
                pos = (end != std::string::npos) ? end + 2 : xml.size();
                continue;
            }
            // Closing tag of parent? Stop.
            if (pos + 1 < xml.size() && xml[pos + 1] == '/')
                break;

            XmlElement child;
            pos = xmlParseElement(xml, pos, child);
            if (!child.tag.empty())
                elem.children.push_back(std::move(child));
        }
        else
        {
            // Text content
            size_t textStart = pos;
            while (pos < xml.size() && xml[pos] != '<') ++pos;
            std::string text = xml.substr(textStart, pos - textStart);
            // Trim
            size_t a = text.find_first_not_of(" \t\r\n");
            size_t b = text.find_last_not_of(" \t\r\n");
            if (a != std::string::npos)
                elem.text += text.substr(a, b - a + 1);
        }
    }
    return pos;
}

// Parse a complete XML document into a root element
static XmlElement xmlParse(const std::string& xml)
{
    XmlElement root;
    size_t pos = 0;
    while (pos < xml.size())
    {
        pos = xmlSkipWS(xml, pos);
        if (pos >= xml.size()) break;
        if (xml[pos] != '<') { ++pos; continue; }

        // Skip declaration/comments at top level
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
        if (!root.tag.empty()) break; // got root element
    }
    return root;
}

// ════════════════════════════════════════════════════════════════════════════════
// Directory Listing (Win32)
// ════════════════════════════════════════════════════════════════════════════════

static std::vector<std::string> listFiles(const std::string& dir, const std::string& pattern = "*")
{
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    std::string searchPath = dir;
    if (!searchPath.empty() && searchPath.back() != '\\' && searchPath.back() != '/') searchPath += '\\';
    searchPath += pattern;

    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return files;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.emplace_back(fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return files;
}

static std::vector<std::string> listDirs(const std::string& dir)
{
    std::vector<std::string> dirs;
    WIN32_FIND_DATAA fd;
    std::string searchPath = dir;
    if (!searchPath.empty() && searchPath.back() != '\\' && searchPath.back() != '/') searchPath += '\\';
    searchPath += '*';

    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return dirs;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            std::string(fd.cFileName) != "." && std::string(fd.cFileName) != "..")
            dirs.emplace_back(fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return dirs;
}

// Read entire file to string
static std::string readFileToString(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════════
// INI Manifest Parser
// ════════════════════════════════════════════════════════════════════════════════

DefManifest parseManifest(const std::string& iniPath, const std::string& defBaseDir)
{
    DefManifest manifest;
    std::ifstream file(iniPath);
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
                // Keys are either directory names or "dir|filename.def"
                // Value is "true" to enable
                if (strEqualCI(kv->value, "true"))
                {
                    std::string key = kv->key;
                    // Convert pipe separator to path separator
                    for (auto& c : key) { if (c == '|') c = '\\'; }

                    if (strEndsWithCI(key, ".def"))
                    {
                        // It's a .def file path relative to definitions dir
                        std::string fullPath = defBaseDir + "\\" + key;
                        manifest.defPaths.push_back(fullPath);
                    }
                    // else it's a directory marker — .def files are referenced individually
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

// ════════════════════════════════════════════════════════════════════════════════
// .def XML Parser
// ════════════════════════════════════════════════════════════════════════════════

DefDefinition parseDef(const std::string& defPath)
{
    DefDefinition def;
    std::string xml = readFileToString(defPath);
    if (xml.empty())
    {
        VLOG(STR("[MoriaCppMod] [Def] Failed to read: {}\n"), std::wstring(defPath.begin(), defPath.end()));
        return def;
    }

    XmlElement root = xmlParse(xml);

    // Handle both <definition> and <manifest> root elements
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
                        ar.json = op.text; // CDATA content
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
        // Secrets manifest — lists JSON file overlays, no runtime changes to apply
        // These are build-time only; skip for runtime processing
        VLOG(STR("[MoriaCppMod] [Def] Skipping manifest file (build-time only): {}\n"),
             std::wstring(defPath.begin(), defPath.end()));
    }

    return def;
}

// ════════════════════════════════════════════════════════════════════════════════
// DataTable Name Resolution — derive DT name from JSON file path
// ════════════════════════════════════════════════════════════════════════════════
//
// Examples:
//   "\Moria\Content\Tech\Data\Items\DT_Items.json"       → "DT_Items"
//   "Moria\Content\Tech\Data\Economy\DT_TradeGoods.json"  → "DT_TradeGoods"
//   "\Moria\Content\Tech\Data\Items\DT_Storage.json"      → "DT_Storage"
//   "Moria\Content\Tech\Data\Settlements\DT_MonumentData.json" → "DT_MonumentData"

static std::string extractDataTableName(const std::string& filePath)
{
    // Find the filename (after last slash/backslash)
    size_t lastSlash = filePath.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

    // Remove .json extension
    if (strEndsWithCI(filename, ".json"))
        filename = filename.substr(0, filename.size() - 5);

    return filename;
}

// ════════════════════════════════════════════════════════════════════════════════
// Nested Property Path Traversal
// ════════════════════════════════════════════════════════════════════════════════
//
// Handles dot-separated paths like "StageDataList[3].MonumentProgressonPointsNeeded"
// by walking UStruct reflection at runtime.
//
// Returns: pointer to the final field and its UStruct property, or nullptr on failure.

struct ResolvedField
{
    uint8_t* data{nullptr};     // pointer to the field's raw memory
    FProperty* prop{nullptr};   // the UE4 FProperty describing the field
};

ResolvedField resolveNestedProperty(uint8_t* rowData, UStruct* rowStruct, const std::string& propertyPath)
{
    if (!rowData || !rowStruct || propertyPath.empty()) return {};

    // Split path by '.'
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

        // Check for array index: "FieldName[N]"
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

        // Find the property in the current struct
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
            // Final simple field
            return {currentData + offset, foundProp};
        }

        if (arrayIndex >= 0)
        {
            // TArray access: read header, index into elements
            // TArray layout: { T* Data; int32 Num; int32 Max; }
            uint8_t* arrayBase = currentData + offset;
            if (!isReadableMemory(arrayBase, 16)) return {};

            DataTableUtil::TArrayHeader hdr;
            std::memcpy(&hdr, arrayBase, 16);
            if (arrayIndex >= hdr.Num || !hdr.Data) return {};

            // Get inner property via FArrayProperty::GetInner()
            int elemSize = 0;
            UStruct* innerStruct = nullptr;

            try
            {
                auto* arrProp = static_cast<FArrayProperty*>(foundProp);
                FProperty* inner = arrProp->GetInner();
                if (inner)
                {
                    elemSize = inner->GetSize();
                    auto* structProp = CastField<FStructProperty>(inner);
                    if (structProp)
                        innerStruct = structProp->GetStruct();
                }
            }
            catch (...) {}

            if (elemSize <= 0)
            {
                VLOG(STR("[MoriaCppMod] [Def] Cannot determine element size for array '{}'\n"), wFieldName);
                return {};
            }

            uint8_t* elemData = hdr.Data + arrayIndex * elemSize;
            if (!isReadableMemory(elemData, elemSize)) return {};

            if (isLast)
            {
                // Caller wants the entire array element
                return {elemData, foundProp};
            }

            // Continue traversal into the struct element
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
            // Non-array struct field — continue traversal
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

// ════════════════════════════════════════════════════════════════════════════════
// Type-Aware Value Writer
// ════════════════════════════════════════════════════════════════════════════════

bool writeValueToField(uint8_t* fieldData, FProperty* prop, const std::string& value)
{
    if (!fieldData || !prop) return false;
    int size = prop->GetSize();

    // NULL means zero/clear the field
    if (strEqualCI(value, "NULL") || value.empty())
    {
        if (isReadableMemory(fieldData, size))
        {
            std::memset(fieldData, 0, size);
            return true;
        }
        return false;
    }

    // Determine type from property class name
    std::wstring className;
    try { className = prop->GetClass().GetName(); } catch (...) { return false; }

    // Int properties (int32, int16, int8)
    if (className.find(L"IntProperty") != std::wstring::npos ||
        className.find(L"Int32Property") != std::wstring::npos)
    {
        try {
            int32_t v = std::stoi(value);
            if (isReadableMemory(fieldData, 4)) { std::memcpy(fieldData, &v, 4); return true; }
        } catch (...) {}
        return false;
    }

    // Float property
    if (className.find(L"FloatProperty") != std::wstring::npos)
    {
        try {
            float v = std::stof(value);
            if (isReadableMemory(fieldData, 4)) { std::memcpy(fieldData, &v, 4); return true; }
        } catch (...) {}
        return false;
    }

    // Double property
    if (className.find(L"DoubleProperty") != std::wstring::npos)
    {
        try {
            double v = std::stod(value);
            if (isReadableMemory(fieldData, 8)) { std::memcpy(fieldData, &v, 8); return true; }
        } catch (...) {}
        return false;
    }

    // Bool property
    if (className.find(L"BoolProperty") != std::wstring::npos)
    {
        auto* boolProp = CastField<FBoolProperty>(prop);
        if (boolProp)
        {
            bool bVal = strEqualCI(value, "true") || value == "1" || strEqualCI(value, "yes");
            boolProp->SetPropertyValue(fieldData, bVal);
            return true;
        }
        return false;
    }

    // Byte/UInt8 property (also covers enums stored as uint8)
    if (className.find(L"ByteProperty") != std::wstring::npos ||
        className.find(L"EnumProperty") != std::wstring::npos)
    {
        try {
            uint8_t v = static_cast<uint8_t>(std::stoi(value));
            if (isReadableMemory(fieldData, 1)) { fieldData[0] = v; return true; }
        } catch (...) {}
        return false;
    }

    // FName property
    if (className.find(L"NameProperty") != std::wstring::npos)
    {
        std::wstring wval(value.begin(), value.end());
        FName fname(wval.c_str(), FNAME_Add);
        if (isReadableMemory(fieldData, 8)) { std::memcpy(fieldData, &fname, 8); return true; }
        return false;
    }

    // For unsupported types, try writing as int32 if size matches
    if (size == 4)
    {
        try {
            // Try float first (if it contains a decimal point)
            if (value.find('.') != std::string::npos)
            {
                float v = std::stof(value);
                if (isReadableMemory(fieldData, 4)) { std::memcpy(fieldData, &v, 4); return true; }
            }
            else
            {
                int32_t v = std::stoi(value);
                if (isReadableMemory(fieldData, 4)) { std::memcpy(fieldData, &v, 4); return true; }
            }
        } catch (...) {}
    }

    VLOG(STR("[MoriaCppMod] [Def] Unsupported property type '{}' for write\n"), className);
    return false;
}

// ════════════════════════════════════════════════════════════════════════════════
// Type-Aware Value Reader (debug verification)
// ════════════════════════════════════════════════════════════════════════════════

std::string readFieldAsString(uint8_t* fieldData, FProperty* prop)
{
    if (!fieldData || !prop) return "<null>";
    int size = prop->GetSize();

    std::wstring className;
    try { className = prop->GetClass().GetName(); } catch (...) { return "<error>"; }

    if (className.find(L"IntProperty") != std::wstring::npos ||
        className.find(L"Int32Property") != std::wstring::npos)
    {
        if (isReadableMemory(fieldData, 4))
        {
            int32_t v; std::memcpy(&v, fieldData, 4);
            return std::to_string(v);
        }
    }
    else if (className.find(L"FloatProperty") != std::wstring::npos)
    {
        if (isReadableMemory(fieldData, 4))
        {
            float v; std::memcpy(&v, fieldData, 4);
            return std::to_string(v);
        }
    }
    else if (className.find(L"DoubleProperty") != std::wstring::npos)
    {
        if (isReadableMemory(fieldData, 8))
        {
            double v; std::memcpy(&v, fieldData, 8);
            return std::to_string(v);
        }
    }
    else if (className.find(L"BoolProperty") != std::wstring::npos)
    {
        auto* boolProp = CastField<FBoolProperty>(prop);
        if (boolProp)
            return boolProp->GetPropertyValue(fieldData) ? "true" : "false";
    }
    else if (className.find(L"ByteProperty") != std::wstring::npos ||
             className.find(L"EnumProperty") != std::wstring::npos)
    {
        if (isReadableMemory(fieldData, 1))
            return std::to_string(fieldData[0]);
    }
    else if (className.find(L"NameProperty") != std::wstring::npos)
    {
        if (isReadableMemory(fieldData, 8))
        {
            FName fn; std::memcpy(&fn, fieldData, 8);
            try {
                auto ws = fn.ToString();
                std::string r; r.reserve(ws.size());
                for (auto wc : ws) r += static_cast<char>(wc);
                return r;
            }
            catch (...) { return "<FName?>"; }
        }
    }

    // Fallback: if 4 bytes, show as int32
    if (size == 4 && isReadableMemory(fieldData, 4))
    {
        int32_t v; std::memcpy(&v, fieldData, 4);
        return std::to_string(v) + "?";
    }

    std::string cn; cn.reserve(className.size());
    for (auto wc : className) cn += static_cast<char>(wc);
    return "<unsupported:" + cn + ">";
}

// ════════════════════════════════════════════════════════════════════════════════
// GameplayTag Container Operations
// ════════════════════════════════════════════════════════════════════════════════
//
// FGameplayTagContainer layout (UE4.27):
//   TArray<FGameplayTag> GameplayTags;   // offset 0x00, each FGameplayTag is FName (8 bytes)
//
// <delete> removes a tag by FName comparison.

bool removeGameplayTag(uint8_t* containerData, const std::string& tagName)
{
    if (!containerData) return false;

    // FGameplayTagContainer starts with TArray<FGameplayTag> at offset 0
    DataTableUtil::TArrayHeader hdr;
    if (!isReadableMemory(containerData, 16)) return false;
    std::memcpy(&hdr, containerData, 16);
    if (hdr.Num <= 0 || !hdr.Data) return false;

    // Each element is an FGameplayTag = FName (8 bytes)
    static constexpr int TAG_SIZE = 8;
    std::wstring wTagName(tagName.begin(), tagName.end());
    FName searchTag(wTagName.c_str(), FNAME_Add);

    for (int32_t i = 0; i < hdr.Num; i++)
    {
        uint8_t* elem = hdr.Data + i * TAG_SIZE;
        if (!isReadableMemory(elem, TAG_SIZE)) continue;
        if (std::memcmp(elem, &searchTag, TAG_SIZE) == 0)
        {
            // Swap-and-pop: move last element here, decrement count
            int32_t lastIdx = hdr.Num - 1;
            if (i < lastIdx)
            {
                uint8_t* lastElem = hdr.Data + lastIdx * TAG_SIZE;
                std::memcpy(elem, lastElem, TAG_SIZE);
            }
            // Zero the old last slot
            uint8_t* lastElem = hdr.Data + lastIdx * TAG_SIZE;
            std::memset(lastElem, 0, TAG_SIZE);
            // Update Num in the TArray header
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

// ════════════════════════════════════════════════════════════════════════════════
// Lightweight JSON Value Extractor (for UAssetAPI add_row CDATA blocks)
// ════════════════════════════════════════════════════════════════════════════════
//
// UAssetAPI JSON format:
//   { "Value": [ { "$type": "...IntPropertyData...", "Name": "Durability", "Value": 1000 }, ... ] }
//
// We extract top-level properties from the "Value" array and write each to the
// newly created row using existing reflection-based writeValueToField().
//
// Supported $types → UE4 FProperty mapping:
//   IntPropertyData        → IntProperty      (int32)
//   FloatPropertyData      → FloatProperty     (float)
//   BoolPropertyData       → BoolProperty      (bool via FBoolProperty)
//   BytePropertyData       → ByteProperty      (uint8)
//   NamePropertyData       → NameProperty      (FName)
//   EnumPropertyData       → EnumProperty/ByteProperty (uint8 from last segment after ::)
//   TextPropertyData       → skipped (FText requires StringTable — not writable at runtime)
//   ObjectPropertyData     → skipped (UObject* requires package index resolution)
//   SoftObjectPropertyData → SoftObjectProperty (FSoftObjectPath via FName write)
//   ArrayPropertyData      → skipped for complex arrays; empty arrays just leave zero-init
//   MapPropertyData        → skipped (TMap — zero-init is correct for empty maps)
//   StructPropertyData     → recurse into nested struct (GameplayTag, handles, etc.)
//
// Properties not in the JSON keep their InitializeStruct defaults (zero or CDO values).

// Find a JSON string value for a given key at the current nesting level
// Returns empty string if not found
static std::string jsonExtractString(const std::string& json, size_t start, size_t end, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, start);
    if (pos == std::string::npos || pos >= end) return "";
    pos += needle.size();
    // Skip whitespace and colon
    while (pos < end && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) ++pos;
    if (pos >= end) return "";

    if (json[pos] == '"')
    {
        // String value
        ++pos;
        size_t valEnd = pos;
        while (valEnd < end && json[valEnd] != '"') {
            if (json[valEnd] == '\\') valEnd++; // skip escaped char
            valEnd++;
        }
        return json.substr(pos, valEnd - pos);
    }
    else if (json[pos] == '{' || json[pos] == '[')
    {
        // Object or array — return the entire block
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
        // Number, bool, or null
        size_t valStart = pos;
        while (pos < end && json[pos] != ',' && json[pos] != '}' && json[pos] != ']'
               && json[pos] != '\r' && json[pos] != '\n') ++pos;
        std::string val = json.substr(valStart, pos - valStart);
        // Trim trailing whitespace
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
        return val;
    }
}

// Find boundaries of each top-level object in a JSON array: [ {...}, {...}, ... ]
// Returns vector of (start, end) positions for each object
static std::vector<std::pair<size_t, size_t>> jsonArrayObjects(const std::string& json, size_t arrStart, size_t arrEnd)
{
    std::vector<std::pair<size_t, size_t>> objects;
    size_t pos = arrStart;
    while (pos < arrEnd)
    {
        // Find next '{'
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

// Look up an enum value by name string, searching both full and short forms
// Returns the int64 value or INDEX_NONE if not found
int64_t findEnumValueByName(UEnum* uenum, const std::string& val)
{
    if (!uenum) return INDEX_NONE;
    std::wstring wVal(val.begin(), val.end());
    std::wstring wShort;
    size_t colonPos = val.rfind("::");
    if (colonPos != std::string::npos)
    {
        std::string shortVal = val.substr(colonPos + 2);
        wShort = std::wstring(shortVal.begin(), shortVal.end());
    }
    for (auto& pair : uenum->ForEachName())
    {
        std::wstring enumName = pair.Key.ToString();
        if (enumName == wVal) return pair.Value;
        if (!wShort.empty() && enumName == wShort) return pair.Value;
        // Also try matching the short suffix of the enum name
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

// Write a single JSON property to a struct field using UE4 reflection
// Returns true if the property was written, false if skipped or failed
bool writeJsonPropertyToField(uint8_t* structData, UStruct* ustruct,
                               const std::string& json, size_t objStart, size_t objEnd)
{
    if (!structData || !ustruct) return false;

    std::string type = jsonExtractString(json, objStart, objEnd, "$type");
    std::string name = jsonExtractString(json, objStart, objEnd, "Name");
    if (name.empty()) return false;

    // Skip zero/null entries
    std::string isZero = jsonExtractString(json, objStart, objEnd, "IsZero");
    if (isZero == "true") return false;

    // Find the property in the UStruct
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

    // ── IntPropertyData ──
    if (type.find("IntPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;
        return writeValueToField(fieldData, prop, val);
    }

    // ── FloatPropertyData ──
    if (type.find("FloatPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;
        // Handle "+0" format from UAssetAPI
        if (val == "+0" || val == "+0.0") val = "0";
        return writeValueToField(fieldData, prop, val);
    }

    // ── BoolPropertyData ──
    if (type.find("BoolPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        return writeValueToField(fieldData, prop, val);
    }

    // ── BytePropertyData ──
    if (type.find("BytePropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty()) return false;
        return writeValueToField(fieldData, prop, val);
    }

    // ── NamePropertyData ──
    if (type.find("NamePropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty() || val == "null" || val == "None") return false;
        return writeValueToField(fieldData, prop, val);
    }

    // ── EnumPropertyData ──
    if (type.find("EnumPropertyData") != std::string::npos)
    {
        std::string val = jsonExtractString(json, objStart, objEnd, "Value");
        if (val.empty() || val == "null") return false;

        // Enums are stored as "EnumType::ValueName" — we need to find the
        // numeric index. For UE4 byte-backed enums, write via FName of the
        // full enum value string so the engine's enum property resolution works.
        // If it's a ByteProperty underneath, try the enum value as FName.
        std::wstring className;
        try { className = prop->GetClass().GetName(); } catch (...) { return false; }

        if (className.find(L"EnumProperty") != std::wstring::npos)
        {
            // EnumProperty stores as int64 (enum index). Find the value name
            // after "::" and write as byte index. For now, write 0 for the
            // default value and let the engine figure it out via reflection.
            // Actually, UE4 EnumProperty can be resolved by FName:
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
            // Byte-backed enum — extract numeric suffix or try FName
            size_t colonPos = val.rfind("::");
            if (colonPos != std::string::npos)
            {
                // Try to resolve via the enum object on the ByteProperty
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
            // Last resort: try as plain integer
            try {
                uint8_t v = static_cast<uint8_t>(std::stoi(val));
                if (isReadableMemory(fieldData, 1)) { fieldData[0] = v; return true; }
            } catch (...) {}
            return false;
        }
        return false;
    }

    // ── SoftObjectPropertyData ──
    if (type.find("SoftObjectPropertyData") != std::string::npos)
    {
        // FSoftObjectPath has: { FTopLevelAssetPath AssetPath; FString SubPathString; }
        // FTopLevelAssetPath: { FName PackageName; FName AssetName; }
        // For runtime, we write the AssetName as an FName to AssetPath.AssetName
        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");
        if (valueBlock.empty() || valueBlock == "null") return false;

        // Extract nested AssetPath.AssetName
        std::string assetPath = jsonExtractString(valueBlock, 0, valueBlock.size(), "AssetPath");
        if (assetPath.empty()) return false;
        std::string assetName = jsonExtractString(assetPath, 0, assetPath.size(), "AssetName");
        if (assetName.empty() || assetName == "null" || assetName == "None") return false;

        // FSoftObjectPath layout: FName PackageName (8) + FName AssetName (8) + FString SubPath (16)
        // Actually in UE4.27 with FTopLevelAssetPath, it's:
        //   FName PackageName @0, FName AssetName @8
        // Then FSoftObjectPath wraps that with SubPathString.
        // For the soft object property, write the full path as an FName to the AssetName field.
        // In practice: SoftObjectProperty's internal is FSoftObjectPath which starts with
        // FTopLevelAssetPath { FName PackageName; FName AssetName; }
        // We write the full path string to the first FName slot which is how the game resolves it.
        std::wstring wPath(assetName.begin(), assetName.end());
        FName pathName(wPath.c_str(), FNAME_Add);
        // SoftObjectProperty internal layout varies; write to the appropriate offset
        // In UE4.27, FSoftObjectPath is: { FName AssetPathName; FString SubPathString; }
        // So the first 8 bytes are the asset path FName
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

    // ── StructPropertyData (nested structs) ──
    if (type.find("StructPropertyData") != std::string::npos)
    {
        std::string structType = jsonExtractString(json, objStart, objEnd, "StructType");
        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");
        if (valueBlock.empty() || valueBlock[0] != '[') return false;

        // Get the inner struct from the property
        auto* structProp = CastField<FStructProperty>(prop);
        if (!structProp || !structProp->GetStruct()) return false;
        UStruct* innerStruct = structProp->GetStruct();

        // Special case: GameplayTagContainer — write tags as FName array
        if (structType == "GameplayTagContainer")
        {
            // Find the GameplayTagContainerPropertyData inside
            auto innerObjects = jsonArrayObjects(valueBlock, 0, valueBlock.size());
            for (auto& [iStart, iEnd] : innerObjects)
            {
                std::string iType = jsonExtractString(valueBlock, iStart, iEnd, "$type");
                if (iType.find("GameplayTagContainerPropertyData") != std::string::npos)
                {
                    std::string tagsBlock = jsonExtractString(valueBlock, iStart, iEnd, "Value");
                    if (tagsBlock.empty() || tagsBlock == "[]") break;

                    // Parse tag strings from array: ["Tag.A", "Tag.B", ...]
                    // The TArray<FGameplayTag> at offset 0 of the container is already zero-init.
                    // We need to allocate an FName array and write it.
                    std::vector<std::string> tagNames;
                    size_t tPos = 1; // skip '['
                    while (tPos < tagsBlock.size())
                    {
                        while (tPos < tagsBlock.size() && tagsBlock[tPos] != '"' && tagsBlock[tPos] != ']') ++tPos;
                        if (tPos >= tagsBlock.size() || tagsBlock[tPos] == ']') break;
                        ++tPos; // skip opening quote
                        size_t tEnd = tPos;
                        while (tEnd < tagsBlock.size() && tagsBlock[tEnd] != '"') ++tEnd;
                        tagNames.push_back(tagsBlock.substr(tPos, tEnd - tPos));
                        tPos = tEnd + 1;
                    }

                    if (!tagNames.empty())
                    {
                        // Allocate persistent FName array for the tags
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
                            // Write TArray header: Data, Num, Max
                            if (isReadableMemory(fieldData, 16))
                            {
                                std::memcpy(fieldData, &tagData, 8);          // Data pointer
                                std::memcpy(fieldData + 8, &tagCount, 4);     // Num
                                std::memcpy(fieldData + 12, &tagCount, 4);    // Max
                            }
                        }
                    }
                    break;
                }
            }
            return true;
        }

        // Special case: GameplayTag — single tag with TagName FName
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
                        // GameplayTag is just { FName TagName; } — write at offset 0
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

        // Generic nested struct (e.g. MorConstructionRowHandle, MorAnyItemRowHandle)
        // These typically contain RowName as an FName
        auto innerObjects = jsonArrayObjects(valueBlock, 0, valueBlock.size());
        for (auto& [iStart, iEnd] : innerObjects)
        {
            writeJsonPropertyToField(fieldData, innerStruct, valueBlock, iStart, iEnd);
        }
        return true;
    }

    // ── ObjectPropertyData ──
    // UObject* pointers require package resolution — skip (leave as null/zero-init)
    if (type.find("ObjectPropertyData") != std::string::npos)
    {
        if (s_verbose)
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]     SKIP '{}' (ObjectPropertyData): requires package resolution\n"), wName);
        return false;
    }

    // ── TextPropertyData ──
    // FText with StringTable references — skip (requires engine StringTable binding)
    if (type.find("TextPropertyData") != std::string::npos)
    {
        if (s_verbose)
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def]     SKIP '{}' (TextPropertyData): requires StringTable binding\n"), wName);
        return false;
    }

    // ── ArrayPropertyData ──
    if (type.find("ArrayPropertyData") != std::string::npos)
    {
        std::string valueBlock = jsonExtractString(json, objStart, objEnd, "Value");
        // Empty arrays: leave zero-init (Num=0, Max=0, Data=null)
        if (valueBlock.empty() || valueBlock == "[]") return false;

        // For non-empty arrays of structs (like CraftingStations, InitialRepairCost),
        // we need to allocate a TArray and write each struct element.
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

        // Allocate persistent element array
        uint8_t* arrData = static_cast<uint8_t*>(FMemory::Malloc(count * elemSize, 8));
        if (!arrData) return false;
        std::memset(arrData, 0, count * elemSize);

        // Initialize each element's struct defaults
        auto* innerStructProp = CastField<FStructProperty>(inner);
        UStruct* innerStruct = innerStructProp ? innerStructProp->GetStruct() : nullptr;

        if (innerStruct)
        {
            for (int i = 0; i < count; i++)
            {
                try { innerStruct->InitializeStruct(arrData + i * elemSize); } catch (...) {}
            }
        }

        // Write each element
        for (int i = 0; i < count; i++)
        {
            auto& [eStart, eEnd] = elements[i];

            if (innerStruct)
            {
                // Struct array element — recurse into its "Value" array
                std::string elemValue = jsonExtractString(valueBlock, eStart, eEnd, "Value");
                if (!elemValue.empty() && elemValue[0] == '[')
                {
                    auto innerProps = jsonArrayObjects(elemValue, 0, elemValue.size());
                    for (auto& [ipStart, ipEnd] : innerProps)
                    {
                        writeJsonPropertyToField(arrData + i * elemSize, innerStruct,
                                                  elemValue, ipStart, ipEnd);
                    }
                }
            }
            else
            {
                // Scalar array element — try to write Value directly
                std::string val = jsonExtractString(valueBlock, eStart, eEnd, "Value");
                if (!val.empty())
                    writeValueToField(arrData + i * elemSize, inner, val);
            }
        }

        // Write TArray header to the field
        if (isReadableMemory(fieldData, 16))
        {
            std::memcpy(fieldData, &arrData, 8);      // Data pointer
            std::memcpy(fieldData + 8, &count, 4);     // Num
            std::memcpy(fieldData + 12, &count, 4);    // Max
        }
        return true;
    }

    // ── MapPropertyData ──
    // TMap — leave zero-init for empty maps
    if (type.find("MapPropertyData") != std::string::npos)
        return false;

    // Unhandled type
    if (s_verbose)
    {
        std::wstring wType(type.begin(), type.end());
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]     SKIP '{}' ({}): unhandled type\n"), wName, wType);
    }
    return false;
}

// ════════════════════════════════════════════════════════════════════════════════
// Fix RowHandle DataTable pointers — copy from existing game row
// ════════════════════════════════════════════════════════════════════════════════
// FFGKDataTableRowHandle is { UDataTable* @0x00; FName RowName @0x08; } = 0x10.
// Our .def files only set RowName (via nested StructPropertyData). The DataTable*
// at offset 0 is an ObjectProperty that we skip. If GetRow() checks DataTable != null,
// all handle resolution fails. Fix: copy the pointer from an existing game row.

void fixRowHandlePointers(DataTableUtil& dt, uint8_t* newRow)
{
    if (!dt.rowStruct || !newRow) return;

    // Get first existing game row as reference
    DataTableUtil::RowMapHeader hdr{};
    if (!dt.getRowMapHeader(hdr) || !hdr.Data || hdr.Num < 1) return;
    if (!isReadableMemory(hdr.Data, DataTableUtil::SET_ELEMENT_SIZE)) return;
    uint8_t* refRow = *reinterpret_cast<uint8_t**>(hdr.Data + DataTableUtil::FNAME_SIZE);
    if (!refRow || !isReadableMemory(refRow, dt.rowSize)) return;

    int fixed = 0;
    // Walk top-level struct properties
    for (auto* s = dt.rowStruct; s; s = s->GetSuperStruct())
    {
        for (auto* p : s->ForEachProperty())
        {
            auto* sp = CastField<FStructProperty>(p);
            if (!sp) continue;
            UStruct* innerStruct = sp->GetStruct();
            if (!innerStruct) continue;

            // Check if struct name contains "RowHandle" or "DataTableRowHandle"
            std::wstring structName = innerStruct->GetName();
            if (structName.find(L"RowHandle") == std::wstring::npos &&
                structName.find(L"DataTableRowHandle") == std::wstring::npos)
                continue;

            int off = p->GetOffset_Internal();
            int sz = p->GetSize();
            if (off < 0 || sz < 16) continue; // Need at least 16 bytes (ptr + FName)

            // Check if new row has null pointer (first 8 bytes)
            uint64_t newPtr = 0, refPtr = 0;
            if (!isReadableMemory(newRow + off, 8) || !isReadableMemory(refRow + off, 8))
                continue;
            std::memcpy(&newPtr, newRow + off, 8);
            std::memcpy(&refPtr, refRow + off, 8);

            if (newPtr != refPtr && refPtr != 0)
            {
                std::memcpy(newRow + off, refRow + off, 8); // Copy DataTable pointer
                fixed++;
                if (s_verbose)
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     RowHandle fix: '{}' @{} was=0x{:016X} now=0x{:016X}\n"),
                        p->GetName(), off, newPtr, refPtr);
            }
        }
    }

    // Also fix RowHandle pointers inside arrays of structs (e.g. DefaultRequiredMaterials)
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

            // Check if any sub-property of the array element struct is a RowHandle
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
            // Read TArray header from BOTH new and reference rows
            struct TArrHdr { uint8_t* Data; int32_t Num; int32_t Max; };
            TArrHdr newArr{}, refArr{};
            if (!isReadableMemory(newRow + arrOff, 16) || !isReadableMemory(refRow + arrOff, 16))
                continue;
            std::memcpy(&newArr, newRow + arrOff, 16);
            std::memcpy(&refArr, refRow + arrOff, 16);

            // For each element in the NEW array, find RowHandle sub-properties
            // and copy the DataTable pointer from the REFERENCE array's first element
            // (all elements of the same handle type point to the same DataTable)
            if (!newArr.Data || newArr.Num <= 0) continue;
            if (!refArr.Data || refArr.Num <= 0) continue;
            if (!isReadableMemory(newArr.Data, newArr.Num * elemSize)) continue;
            if (!isReadableMemory(refArr.Data, elemSize)) continue;

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

                    // Get DataTable pointer from reference array element[0]
                    uint64_t refSubPtr = 0;
                    if (!isReadableMemory(refArr.Data + subOff, 8)) continue;
                    std::memcpy(&refSubPtr, refArr.Data + subOff, 8);
                    if (refSubPtr == 0) continue;

                    // Apply to all new array elements
                    for (int i = 0; i < newArr.Num; i++)
                    {
                        uint8_t* elemBase = newArr.Data + i * elemSize;
                        uint64_t curPtr = 0;
                        if (!isReadableMemory(elemBase + subOff, 8)) continue;
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

// ════════════════════════════════════════════════════════════════════════════════
// Apply add_row — create a new DataTable row and populate from JSON
// ════════════════════════════════════════════════════════════════════════════════

int applyAddRow(DataTableUtil& dt, const DefAddRow& addRow)
{
    if (!dt.isBound() || !dt.rowStruct || dt.rowSize <= 0) return 0;

    std::wstring wRowName(addRow.rowName.begin(), addRow.rowName.end());

    // Check if row already exists (skip duplicates)
    if (dt.findRowData(wRowName.c_str()))
    {
        VLOG(STR("[MoriaCppMod] [Def] add_row: '{}' already exists in '{}', skipping\n"),
             wRowName, dt.tableName);
        return 0;
    }

    // Add empty row (allocates + InitializeStruct + inserts into RowMap)
    uint8_t* rowData = dt.addRow(wRowName.c_str());
    if (!rowData)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] add_row: FAILED to add '{}' to '{}'\n"),
            wRowName, dt.tableName);
        return 0;
    }

    // Parse the JSON "Value" array to get the property list
    const std::string& json = addRow.json;
    std::string valueBlock = jsonExtractString(json, 0, json.size(), "Value");
    if (valueBlock.empty() || valueBlock[0] != '[')
    {
        VLOG(STR("[MoriaCppMod] [Def] add_row: '{}' has no Value array\n"), wRowName);
        return 1; // Row was added (with defaults) even if no properties to set
    }

    auto properties = jsonArrayObjects(valueBlock, 0, valueBlock.size());
    int propsWritten = 0;
    for (auto& [pStart, pEnd] : properties)
    {
        if (writeJsonPropertyToField(rowData, dt.rowStruct, valueBlock, pStart, pEnd))
            propsWritten++;
    }

    // Write row FName at unreflected offset 0x08 in FFGKTableRowBase
    // (game code uses this to look up associated construction definitions)
    {
        FName rowFName(wRowName.c_str(), FNAME_Add);
        std::memcpy(rowData + 0x08, &rowFName, sizeof(FName));
    }

    // Post-pass: copy DataTable* pointers for RowHandle properties from existing game rows
    fixRowHandlePointers(dt, rowData);

    if (s_verbose)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]   + {} ({} props written)\n"),
            wRowName, propsWritten);
    }

    return 1;
}

// ════════════════════════════════════════════════════════════════════════════════
// Apply Operations
// ════════════════════════════════════════════════════════════════════════════════

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
            // Simple flat property — use DataTableUtil typed writes
            std::wstring wProp(change.property.begin(), change.property.end());
            int off = dt.resolvePropertyOffset(wProp.c_str());
            if (off < 0) return false;

            uint8_t* rowData = dt.findRowData(rowName);
            if (!rowData) return false;

            // Determine type from UStruct reflection
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

            // Fallback: try int32 then float
            if (change.value.find('.') != std::string::npos)
                return dt.writeFloat(rowName, wProp.c_str(), std::stof(change.value));
            else
            {
                try { return dt.writeInt32(rowName, wProp.c_str(), std::stoi(change.value)); }
                catch (...) { return false; }
            }
        }
        else
        {
            // Nested property path
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
        // Apply to all rows
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
        // Read back tag count for verification
        DataTableUtil::TArrayHeader hdr;
        std::memcpy(&hdr, rowData + off, 16);
        std::wstring wVal(del.value.begin(), del.value.end());
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def]   {} . {} -= '{}' (tags remaining: {})\n"),
            wItem, wProp, wVal, hdr.Num);
    }
    return ok ? 1 : 0;
}

// ════════════════════════════════════════════════════════════════════════════════
// DataTable Binding by Name (dynamic — binds tables not in the pre-cached set)
// ════════════════════════════════════════════════════════════════════════════════

DataTableUtil& getOrBindDataTable(const std::string& dtName, std::unordered_map<std::string, DataTableUtil>& dynamicTables)
{
    // Check pre-cached tables first
    static const std::unordered_map<std::string, DataTableUtil*> knownTables = {
        {"DT_Constructions", &m_dtConstructions},
        {"DT_ConstructionRecipes", &m_dtConstructionRecipes},
        {"DT_Items", &m_dtItems},
        {"DT_ItemRecipes", &m_dtItemRecipes},
        {"DT_Weapons", &m_dtWeapons},
        {"DT_Tools", &m_dtTools},
        {"DT_Armor", &m_dtArmor},
        {"DT_Consumables", &m_dtConsumables},
        {"DT_ContainerItems", &m_dtContainerItems},
        {"DT_Ores", &m_dtOres},
    };

    // Case-insensitive lookup
    for (auto& [name, dt] : knownTables)
    {
        if (strEqualCI(name, dtName) && dt->isBound())
            return *dt;
    }

    // Dynamic binding for tables not pre-cached (e.g. DT_Storage, DT_MonumentData, etc.)
    auto it = dynamicTables.find(dtName);
    if (it != dynamicTables.end() && it->second.isBound())
        return it->second;

    auto& dt = dynamicTables[dtName];
    std::wstring wName(dtName.begin(), dtName.end());
    dt.bind(wName.c_str());
    return dt;
}

// ════════════════════════════════════════════════════════════════════════════════
// Game Mods Discovery & Persistence
// ════════════════════════════════════════════════════════════════════════════════
//
// GameMods.ini location: Mods/GameMods.ini (root Mods directory)
// Definitions dir:       Mods/MoriaCppMod/definitions/

static inline const std::string GAMEMODS_INI_PATH = "Mods/GameMods.ini";
static inline const std::string DEFINITIONS_DIR = "Mods/MoriaCppMod/definitions";

struct GameModEntry
{
    std::string name;           // manifest name (without .ini extension)
    std::string title;          // from [ModInfo] Title
    std::string description;    // from [ModInfo] Description (HTML)
    bool enabled{false};        // from GameMods.ini
};

// Scan definitions/ for available .ini manifests and merge with GameMods.ini state
std::vector<GameModEntry> discoverGameMods()
{
    std::vector<GameModEntry> entries;

    // Read current enabled state from GameMods.ini
    std::unordered_map<std::string, bool> enabledMap;
    {
        std::ifstream file(GAMEMODS_INI_PATH);
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

    // Scan for .ini manifests in definitions directory
    auto iniFiles = listFiles(DEFINITIONS_DIR, "*.ini");
    for (auto& iniFile : iniFiles)
    {
        // Strip .ini extension for the name
        std::string name = iniFile;
        if (strEndsWithCI(name, ".ini"))
            name = name.substr(0, name.size() - 4);

        // Parse manifest to get title and description
        std::string iniPath = DEFINITIONS_DIR + "\\" + iniFile;
        DefManifest manifest = parseManifest(iniPath, DEFINITIONS_DIR);

        // Skip manifests with no .def paths (e.g. secrets-only)
        if (manifest.defPaths.empty()) continue;

        GameModEntry entry;
        entry.name = name;
        entry.title = manifest.title.empty() ? name : manifest.title;
        entry.description = manifest.description;

        // Check enabled state — default OFF
        auto it = enabledMap.find(name);
        entry.enabled = (it != enabledMap.end()) ? it->second : false;

        entries.push_back(std::move(entry));
    }

    return entries;
}

// Save enabled/disabled state to GameMods.ini
void saveGameMods(const std::vector<GameModEntry>& entries)
{
    std::ofstream file(GAMEMODS_INI_PATH, std::ios::trunc);
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

// ════════════════════════════════════════════════════════════════════════════════
// GameMods.ini Reader — reads Mods/GameMods.ini to find enabled manifests
// ════════════════════════════════════════════════════════════════════════════════
//
// Format:
//   [EnabledMods]
//   Clean Fat Stacks = true
//   Fat Trade = false
//
// Keys are manifest names (without .ini extension), values are true/false.

static std::vector<std::string> readEnabledMods(const std::string& gameModsPath)
{
    std::vector<std::string> enabled;
    std::ifstream file(gameModsPath);
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

// ════════════════════════════════════════════════════════════════════════════════
// Main Entry Point: loadAndApplyDefinitions()
// ════════════════════════════════════════════════════════════════════════════════

void loadAndApplyDefinitions()
{
    // Read GameMods.ini to find which manifests are enabled
    auto enabledMods = readEnabledMods(GAMEMODS_INI_PATH);
    if (enabledMods.empty())
    {
        VLOG(STR("[MoriaCppMod] [Def] No mods enabled in Mods/GameMods.ini (or file not found)\n"));
        return;
    }

    // Verify definitions directory exists
    WIN32_FIND_DATAA fd;
    HANDLE hTest = FindFirstFileA((DEFINITIONS_DIR + "\\*").c_str(), &fd);
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
    // Track tables that received add_rows for post-processing verification
    std::unordered_map<std::string, std::string> tablesWithAddRows; // dtName -> firstRowName

    for (auto& modName : enabledMods)
    {
        // Find the corresponding .ini manifest in the definitions directory
        std::string iniPath = DEFINITIONS_DIR + "\\" + modName + ".ini";
        std::ifstream testFile(iniPath);
        if (!testFile.is_open())
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Manifest '{}' not found at {}\n"),
                std::wstring(modName.begin(), modName.end()),
                std::wstring(iniPath.begin(), iniPath.end()));
            continue;
        }
        testFile.close();

        DefManifest manifest = parseManifest(iniPath, DEFINITIONS_DIR);
        if (manifest.defPaths.empty())
        {
            VLOG(STR("[MoriaCppMod] [Def] Manifest '{}' has no .def paths, skipping\n"),
                 std::wstring(modName.begin(), modName.end()));
            continue;
        }

        totalManifests++;
        std::string displayName = manifest.title.empty() ? modName : manifest.title;
        RC::Output::send<RC::LogLevel::Warning>(STR("[MoriaCppMod] [Def] Loading '{}' ({} defs)\n"),
            std::wstring(displayName.begin(), displayName.end()),
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
                         std::wstring(mod.filePath.begin(), mod.filePath.end()));
                    continue;
                }

                DataTableUtil& dt = getOrBindDataTable(dtName, dynamicTables);
                if (!dt.isBound())
                {
                    VLOG(STR("[MoriaCppMod] [Def] DataTable '{}' not found in game — skipping\n"),
                         std::wstring(dtName.begin(), dtName.end()));
                    continue;
                }

                // Apply add_rows first (new rows must exist before changes/deletes reference them)
                for (auto& ar : mod.addRows)
                {
                    totalAddRows++;
                    int ok = applyAddRow(dt, ar);
                    totalApplied += ok;
                    // Track first added row per table for verification
                    if (tablesWithAddRows.find(dtName) == tablesWithAddRows.end())
                        tablesWithAddRows[dtName] = ar.rowName;
                    // Collect recipe names for post-world-load DiscoverRecipe calls
                    if (ok > 0 && dtName == "DT_ConstructionRecipes")
                        m_addedRecipeNames.emplace_back(ar.rowName.begin(), ar.rowName.end());
                }

                // Apply deletes (order matters: remove restrictions before changing values)
                for (auto& del : mod.deletes)
                {
                    totalDeletes++;
                    int n = applyDelete(dt, del);
                    totalApplied += n;
                }

                // Then apply changes
                for (auto& change : mod.changes)
                {
                    totalChanges++;
                    int n = applyChange(dt, change);
                    totalApplied += n;
                }
            }
        }
    }

    // ── Post-processing verification: confirm added rows are findable ──
    // Uses ZERO FName::ToString() calls (SEH-unsafe). All output is hex/numeric only.
    // Tests both linear scan (array) and engine hash lookup (DoesDataTableRowExist).
    if (!tablesWithAddRows.empty())
    {
        // Helper: dump N bytes as hex string (no FName::ToString involved)
        auto hexDump = [](const uint8_t* data, int len) -> std::wstring {
            std::wstring hex;
            for (int b = 0; b < len; b++) {
                wchar_t buf[8]; swprintf(buf, 8, L"%02X ", data[b]);
                hex += buf;
            }
            return hex;
        };

        // Find UDataTableFunctionLibrary::DoesDataTableRowExist for hash verification
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

            // ── UDataTable object structure dump ──
            // Dump raw bytes of the UDataTable beyond UObject base to see full layout
            {
                auto* dtBase = reinterpret_cast<uint8_t*>(dt.table);
                // UObject is ~0x28 bytes, UDataTable adds RowStruct, RowMap, flags, etc.
                // Dump 0x28..0xA0 (120 bytes covering RowStruct, RowMap, and beyond)
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
                // Package path
                try {
                    auto* outermost = dt.table->GetOutermost();
                    if (outermost)
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     Package: {}\n"),
                            outermost->GetPathName());
                } catch (...) {}
                // RowStruct name
                if (dt.rowStruct)
                {
                    try {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def]     RowStruct: {} (size={})\n"),
                            dt.rowStruct->GetFullName(), dt.rowSize);
                    } catch (...) {}
                }
            }

            // ── Search for UFGKDataTableBase wrapper that references this UDataTable ──
            // UFGKDataTableBase has: TableAsset @0x28, TestTableAsset @0x30, DynamicTableAsset @0x38
            {
                std::vector<UObject*> fgkBases;
                UObjectGlobals::FindAllOf(STR("FGKDataTableBase"), fgkBases);
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
                                // Dump DynamicTableAsset (UFGKAdditiveDataTable, inherits UDataTable)
                                // Check its RowMap for row count
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

            // Test 1: Linear scan (array-based, no hash)
            uint8_t* rowData = dt.findRowData(wFirstRow.c_str());
            bool foundLinear = (rowData != nullptr);

            // Test 2: Engine hash lookup via DoesDataTableRowExist (ADDED row)
            bool foundHash = false;
            bool hashTestRan = false;
            if (doesRowExistFn && dtFuncLib && dt.table)
            {
                struct { UObject* Table; FName RowName; bool ReturnValue; } params{};
                params.Table = dt.table;
                params.RowName = FName(wFirstRow.c_str(), FNAME_Find);
                params.ReturnValue = false;
                try {
                    dtFuncLib->ProcessEvent(doesRowExistFn, &params);
                    foundHash = params.ReturnValue;
                    hashTestRan = true;
                } catch (...) {}
            }

            // Test 3: Engine hash lookup on EXISTING (original game) row as control
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
                        dtFuncLib->ProcessEvent(doesRowExistFn, &ctrlP);
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

            // Dump key fields as raw hex (ZERO ToString calls)
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

                // Also dump first 32 bytes of the row for structural analysis
                if (isReadableMemory(rowData, 32))
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def]     Row first 32B: {}\n"),
                        hexDump(rowData, 32));

                // COMPARISON: dump same fields from an existing GAME row (element[0])
                DataTableUtil::RowMapHeader hdr{};
                if (dt.getRowMapHeader(hdr) && hdr.Data && hdr.Num > 0
                    && isReadableMemory(hdr.Data, DataTableUtil::SET_ELEMENT_SIZE))
                {
                    // Get row data pointer from element[0]
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
                    // Annotate: first 8 bytes = potential UDataTable*, next 8 = RowName FName
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

                // COMPARISON: dump ResultConstructionHandle from an existing GAME row
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

    // ── Patch FGK ConstructionRecipeLookup TMap (CRITICAL: must happen BEFORE map load) ──
    // The FGK wrapper UMorConstructionRecipesTable has its own ConstructionRecipeLookup TMap
    // at offset 0x0110, built at asset load time from the DataTable. Even though addRow()
    // properly inserts into the DataTable's RowMap (via engine AddRowInternal), the FGK
    // wrapper's TMap is a SEPARATE structure that was already built. We must patch it here
    // (during LoadMapPreCallback) so that when the DiscoveryManager initializes during map
    // load, it reads the FGK system and finds ALL recipes (base + ours), builds its Recipes
    // TArray with all entries, and populates the category caches including our recipes.
    if (!m_addedRecipeNames.empty())
    {
        std::vector<UObject*> fgkTables;
        try { UObjectGlobals::FindAllOf(STR("MorConstructionRecipesTable"), fgkTables); } catch (...) {}
        if (fgkTables.empty())
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FGK wrapper NOT FOUND at LoadMapPre time — will patch later\n"));
        }
        else
        {
            auto* fgkBytes = reinterpret_cast<uint8_t*>(fgkTables[0]);
            static constexpr int LOOKUP_OFFSET = 0x0110;
            static constexpr size_t FGK_ELEM_SIZE = 0x20;  // 32 bytes per TSetElement

            struct TSparseHdr { uint8_t* Data; int32_t Num; int32_t Max; };
            TSparseHdr sparse{};
            if (isReadableMemory(fgkBytes + LOOKUP_OFFSET, sizeof(TSparseHdr)))
                std::memcpy(&sparse, fgkBytes + LOOKUP_OFFSET, sizeof(TSparseHdr));

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FGK TMap (LoadMapPre) BEFORE: Num={} Max={}\n"),
                sparse.Num, sparse.Max);

            uintptr_t fgkVtable = 0;
            if (sparse.Data && sparse.Num > 0 && isReadableMemory(sparse.Data, 8))
                std::memcpy(&fgkVtable, sparse.Data, sizeof(uintptr_t));

            if (fgkVtable != 0)
            {
                int32_t oldNum = sparse.Num;
                int32_t addCount = static_cast<int32_t>(m_addedRecipeNames.size());
                int32_t newTotal = oldNum + addCount;
                size_t oldBytes = static_cast<size_t>(oldNum) * FGK_ELEM_SIZE;
                size_t newBytes = static_cast<size_t>(newTotal) * FGK_ELEM_SIZE;

                uint8_t* newData = static_cast<uint8_t*>(FMemory::Realloc(sparse.Data, newBytes, 8));
                if (newData)
                {
                    std::memset(newData + oldBytes, 0, newBytes - oldBytes);

                    // Append new elements
                    for (int32_t i = 0; i < addCount; i++)
                    {
                        int elemIdx = oldNum + i;
                        uint8_t* elem = newData + static_cast<size_t>(elemIdx) * FGK_ELEM_SIZE;
                        std::memcpy(elem + 0x00, &fgkVtable, sizeof(uintptr_t));
                        FName keyFName(m_addedRecipeNames[i].c_str(), FNAME_Add);
                        std::memcpy(elem + 0x08, &keyFName, sizeof(FName));
                        std::memcpy(elem + 0x10, &keyFName, sizeof(FName));
                    }

                    // Rechain ALL elements into single bucket (degenerate O(N) but correct)
                    for (int i = 0; i < newTotal; i++)
                    {
                        uint8_t* elem = newData + static_cast<size_t>(i) * FGK_ELEM_SIZE;
                        int32_t nextId = (i < newTotal - 1) ? (i + 1) : -1;
                        std::memcpy(elem + 0x18, &nextId, 4);
                        int32_t bucketIdx = 0;
                        std::memcpy(elem + 0x1C, &bucketIdx, 4);
                    }

                    // Update allocation bitmap
                    std::memset(fgkBytes + LOOKUP_OFFSET + 0x10, 0xFF, 16);
                    int32_t bitmapU32Count = (newTotal + 31) / 32;
                    uint32_t* heapBitmap = nullptr;
                    std::memcpy(&heapBitmap, fgkBytes + LOOKUP_OFFSET + 0x20, sizeof(uint32_t*));
                    uint32_t* newBitmap = static_cast<uint32_t*>(
                        FMemory::Realloc(heapBitmap, static_cast<size_t>(bitmapU32Count) * 4, 8));
                    if (newBitmap)
                    {
                        std::memset(newBitmap, 0xFF, static_cast<size_t>(bitmapU32Count) * 4);
                        int32_t trailingBits = newTotal % 32;
                        if (trailingBits != 0)
                            newBitmap[bitmapU32Count - 1] = (1u << trailingBits) - 1;
                        std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x20, &newBitmap, sizeof(uint32_t*));
                    }
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x28, &newTotal, 4);
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x2C, &newTotal, 4);

                    int32_t minusOne = -1, zero = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x30, &minusOne, 4);
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x34, &zero, 4);

                    int32_t headIdx = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x38, &headIdx, 4);
                    uintptr_t nullPtr = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x40, &nullPtr, sizeof(uintptr_t));
                    int32_t hashSize = 1;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x48, &hashSize, 4);

                    sparse.Data = newData;
                    sparse.Num = newTotal;
                    sparse.Max = newTotal;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET, &sparse, sizeof(TSparseHdr));

                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] FGK TMap (LoadMapPre) AFTER: Num={} (added {})\n"),
                        newTotal, addCount);
                }
                else
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] FGK TMap (LoadMapPre): Realloc failed\n"));
                }
            }
            else
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] FGK TMap (LoadMapPre): cannot capture vtable\n"));
            }

            // ── Expand FGK internal pointer arrays (0x090, 0x0A0, 0x100) ──────────
            // These TArray<uint8_t*> hold pointers to DataTable row data.
            // 0x090 = AllRows (814), 0x0A0 = EnabledRows (795), 0x100 = AllRows2 (814)
            // The build menu iterates these arrays. We must add our new recipe row pointers.
            {
                bool dtWasBound = m_dtConstructionRecipes.isBound();
                if (!dtWasBound)
                    m_dtConstructionRecipes.bind(L"DT_ConstructionRecipes");

                // Collect pointers to our new rows in DT_ConstructionRecipes
                std::vector<uint8_t*> newRowPtrs;
                newRowPtrs.reserve(m_addedRecipeNames.size());
                for (auto& rn : m_addedRecipeNames)
                {
                    uint8_t* rowData = m_dtConstructionRecipes.findRowData(rn.c_str());
                    if (rowData)
                        newRowPtrs.push_back(rowData);
                }

                if (newRowPtrs.empty())
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] FGK array expansion: no valid row pointers found\n"));
                }
                else
                {
                    // Expand arrays at offsets 0x090, 0x0A0, and 0x100
                    static constexpr int FGK_PTR_ARRAYS[] = { 0x090, 0x0A0, 0x100 };
                    for (int arrOff : FGK_PTR_ARRAYS)
                    {
                        struct TArrHdr { uint8_t* Data; int32_t Num; int32_t Max; };
                        TArrHdr arr{};
                        if (!isReadableMemory(fgkBytes + arrOff, sizeof(TArrHdr)))
                        {
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def] FGK[0x{:03X}]: not readable, skip\n"), arrOff);
                            continue;
                        }
                        std::memcpy(&arr, fgkBytes + arrOff, sizeof(TArrHdr));

                        int32_t oldNum = arr.Num;
                        int32_t addCount = static_cast<int32_t>(newRowPtrs.size());
                        int32_t newNum = oldNum + addCount;

                        // Grow if needed
                        if (newNum > arr.Max)
                        {
                            int32_t newMax = newNum + (newNum / 4) + 64;
                            uint8_t* newData = static_cast<uint8_t*>(
                                FMemory::Realloc(arr.Data, static_cast<size_t>(newMax) * 8, 8));
                            if (!newData)
                            {
                                RC::Output::send<RC::LogLevel::Warning>(
                                    STR("[MoriaCppMod] [Def] FGK[0x{:03X}]: Realloc FAILED\n"), arrOff);
                                continue;
                            }
                            arr.Data = newData;
                            arr.Max = newMax;
                        }

                        // Append pointers
                        for (int32_t i = 0; i < addCount; i++)
                        {
                            uintptr_t ptr = reinterpret_cast<uintptr_t>(newRowPtrs[i]);
                            std::memcpy(arr.Data + static_cast<size_t>(oldNum + i) * 8, &ptr, 8);
                        }
                        arr.Num = newNum;

                        // Write back
                        std::memcpy(fgkBytes + arrOff, &arr, sizeof(TArrHdr));

                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def] FGK[0x{:03X}]: expanded {}→{} (Max={})\n"),
                            arrOff, oldNum, newNum, arr.Max);
                    }
                }

                if (!dtWasBound) m_dtConstructionRecipes.unbind();
            }
        }
    }

    // Unbind dynamic tables
    for (auto& [name, dt] : dynamicTables)
        dt.unbind();

    RC::Output::send<RC::LogLevel::Warning>(
        STR("[MoriaCppMod] [Def] Done: {} manifests, {} add_rows + {} changes + {} deletes = {} applied\n"),
        totalManifests, totalAddRows, totalChanges, totalDeletes, totalApplied);

    if (!m_addedRecipeNames.empty())
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] {} construction recipes queued for post-load DiscoverRecipe\n"),
            m_addedRecipeNames.size());
}

// ════════════════════════════════════════════════════════════════════════════════
// Post-world-load: populate Recipes TArray + call DiscoverRecipe for each added recipe
// ════════════════════════════════════════════════════════════════════════════════
// AMorDiscoveryManager::DiscoverRecipe(const FName& RecipeName) is a UFUNCTION(BlueprintCallable).
// It handles: DiscoveredRecipes insertion, cache updates, delegate firing.
// We must first add entries to the Recipes TArray (master catalog) so DiscoverRecipe
// can find them, then call DiscoverRecipe to mark them as discovered.

void discoverAddedRecipes(UObject* passedDiscMgr = nullptr)
{
    if (m_addedRecipeNames.empty()) return;

    // ── Find AMorDiscoveryManager ────────────────────────────────────────────
    UObject* discMgr = passedDiscMgr;
    if (!discMgr)
    {
        std::vector<UObject*> managers;
        UObjectGlobals::FindAllOf(STR("MorDiscoveryManager"), managers);
        if (managers.empty())
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DiscoverRecipe: MorDiscoveryManager not found, skipping\n"));
            return;
        }
        discMgr = managers[0];
    }
    auto* dmBytes = reinterpret_cast<uint8_t*>(discMgr);

    VLOG(STR("[MoriaCppMod] [Def] AMorDiscoveryManager: addr=0x{:X}\n"),
        reinterpret_cast<uintptr_t>(discMgr));

    // ── Find DiscoverRecipe UFunction ────────────────────────────────────────
    UFunction* discoverFn = nullptr;
    try {
        discoverFn = discMgr->GetFunctionByName(STR("DiscoverRecipe"));
    } catch (...) {}
    if (!discoverFn)
        discoverFn = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr,
            STR("/Script/Moria.MorDiscoveryManager:DiscoverRecipe"));
    if (!discoverFn)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: function not found on MorDiscoveryManager\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Def] DiscoverRecipe UFunction found at 0x{:X}\n"),
        reinterpret_cast<uintptr_t>(discoverFn));

    // ── Ensure DT_ConstructionRecipes is bound ───────────────────────────────
    bool wasBound = m_dtConstructionRecipes.isBound();
    if (!wasBound)
        m_dtConstructionRecipes.bind(L"DT_ConstructionRecipes");
    if (!m_dtConstructionRecipes.isBound() || !m_dtConstructionRecipes.rowStruct)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: DT_ConstructionRecipes not available\n"));
        return;
    }

    // Element size from the DataTable's row struct (should be 0x128 = 296)
    int elemSize = m_dtConstructionRecipes.rowSize;
    UStruct* rowStruct = m_dtConstructionRecipes.rowStruct;
    if (elemSize <= 0 || elemSize > 4096)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: invalid element size {}, aborting\n"), elemSize);
        return;
    }

    // ── Diagnostic: DataTable state at DiscoveryManager detection time ────────
    {
        int32_t dtRowCount = m_dtConstructionRecipes.getRowCount();
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DIAG: DT_ConstructionRecipes rows at DM detect: {} (our UDataTable=0x{:X})\n"),
            dtRowCount, reinterpret_cast<uintptr_t>(m_dtConstructionRecipes.table));

        // Check if FGK wrapper's TableAsset points to the same UDataTable we modified
        std::vector<UObject*> fgkTables;
        try { UObjectGlobals::FindAllOf(STR("MorConstructionRecipesTable"), fgkTables); } catch (...) {}
        if (!fgkTables.empty())
        {
            auto* fgkBytes = reinterpret_cast<uint8_t*>(fgkTables[0]);
            UObject* tableAsset = nullptr;
            if (isReadableMemory(fgkBytes + 0x28, 8))
                std::memcpy(&tableAsset, fgkBytes + 0x28, 8);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DIAG: FGK TableAsset=0x{:X} match={}\n"),
                reinterpret_cast<uintptr_t>(tableAsset),
                (tableAsset == m_dtConstructionRecipes.table) ? STR("YES") : STR("NO — DIFFERENT OBJECT!"));

            // Check DynamicTableAsset
            UObject* dynAsset = nullptr;
            if (isReadableMemory(fgkBytes + 0x38, 8))
                std::memcpy(&dynAsset, fgkBytes + 0x38, 8);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DIAG: FGK DynamicTableAsset=0x{:X} ({})\n"),
                reinterpret_cast<uintptr_t>(dynAsset),
                dynAsset ? STR("EXISTS") : STR("NULL"));

            // If TableAsset is different, check its row count
            if (tableAsset && tableAsset != m_dtConstructionRecipes.table)
            {
                auto* taBytes = reinterpret_cast<uint8_t*>(tableAsset);
                struct { uint8_t* Data; int32_t Num; int32_t Max; } rmHdr{};
                if (isReadableMemory(taBytes + 0x30, 16))
                {
                    std::memcpy(&rmHdr, taBytes + 0x30, 16);
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] DIAG: FGK TableAsset RowMap.Num={} (OUR DT has {})\n"),
                        rmHdr.Num, dtRowCount);
                }
            }

            // Dump FGK TMap Num to confirm LoadMapPre patch persisted
            int32_t fgkNum = 0;
            if (isReadableMemory(fgkBytes + 0x0110 + 8, 4))
                std::memcpy(&fgkNum, fgkBytes + 0x0110 + 8, 4);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DIAG: FGK ConstructionRecipeLookup.Num={}\n"), fgkNum);

            // ── DEEP DIAG: Scan FGK wrapper internal cache (0x40-0x110) ─────────
            // UFGKDataTableBase reflected fields end at 0x40, but actual size is 0x110.
            // The 0xD0 bytes (0x40-0x110) contain unreflected internal caches.
            // Scan for TArray-like patterns: [ptr(8), Num(4), Max(4)] where Num is plausible.
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DIAG: === FGK wrapper internal scan (0x40-0x110) ===\n"));
            auto fgkHexDump = [](const uint8_t* data, int len) -> std::wstring {
                std::wstring h;
                for (int b = 0; b < len; b++) {
                    wchar_t buf[8]; swprintf(buf, 8, L"%02X ", data[b]);
                    h += buf;
                }
                return h;
            };
            if (isReadableMemory(fgkBytes + 0x40, 0xD0))
            {
                // Hex dump in 16-byte chunks for visual analysis
                for (int off = 0x40; off < 0x110; off += 16)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] DIAG: FGK[0x{:03X}]: {}\n"),
                        off, fgkHexDump(fgkBytes + off, 16));
                }

                // Scan for int32 values that look like row counts (500-1200 range)
                for (int off = 0x40; off < 0x110 - 4; off += 4)
                {
                    int32_t val = 0;
                    std::memcpy(&val, fgkBytes + off, 4);
                    if (val >= 500 && val <= 1200)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def] DIAG: FGK[0x{:03X}] int32={} (potential row count!)\n"),
                            off, val);
                    }
                }

                // Scan for TArray patterns: [ptr(8), Num(4), Max(4)] where ptr looks valid
                for (int off = 0x40; off <= 0x110 - 16; off += 8)
                {
                    uint64_t ptr = 0;
                    int32_t num = 0, max = 0;
                    std::memcpy(&ptr, fgkBytes + off, 8);
                    std::memcpy(&num, fgkBytes + off + 8, 4);
                    std::memcpy(&max, fgkBytes + off + 12, 4);
                    // Valid TArray: ptr looks like heap address, 0 < Num <= Max <= 10000
                    if (ptr > 0x10000 && ptr < 0x7FFFFFFFFFFF && num > 0 && num <= max && max <= 10000)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def] DIAG: FGK[0x{:03X}] TArray? ptr=0x{:X} Num={} Max={}\n"),
                            off, ptr, num, max);
                        // If Num matches known counts, flag it
                        if (num == 814 || num == 520)
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[MoriaCppMod] [Def] DIAG: ^^^ MATCH! Num={} matches known count ^^^\n"), num);
                    }
                }
            }
            else
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] DIAG: FGK wrapper bytes 0x40-0x110 NOT readable\n"));
            }
        }

        // Also check: are there multiple UDataTable objects with this name?
        std::vector<UObject*> allDTs;
        try { UObjectGlobals::FindAllOf(STR("DataTable"), allDTs); } catch (...) {}
        int matchCount = 0;
        for (auto* dt : allDTs)
        {
            try {
                auto path = dt->GetPathName();
                if (path.find(STR("DT_ConstructionRecipes")) != std::wstring::npos)
                {
                    matchCount++;
                    if (matchCount <= 3)
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[MoriaCppMod] [Def] DIAG: DT match #{}: 0x{:X} path='{}'\n"),
                            matchCount, reinterpret_cast<uintptr_t>(dt), path);
                }
            } catch (...) {}
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DIAG: Total UDataTable objects with 'DT_ConstructionRecipes': {}\n"),
            matchCount);
    }

    // ── Read current Recipes TArray header ───────────────────────────────────
    // AMorDiscoveryManager.Recipes is TArray<FMorConstructionRecipeDefinition> at offset 0x0220
    static constexpr int RECIPES_OFFSET = 0x0220;
    struct TArrHdr { uint8_t* Data; int32_t Num; int32_t Max; };
    TArrHdr recipes{};
    if (!isReadableMemory(dmBytes + RECIPES_OFFSET, sizeof(TArrHdr)))
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: Recipes TArray at 0x0220 not readable\n"));
        return;
    }
    std::memcpy(&recipes, dmBytes + RECIPES_OFFSET, sizeof(TArrHdr));
    RC::Output::send<RC::LogLevel::Warning>(
        STR("[MoriaCppMod] [Def] Recipes TArray BEFORE: Num={} Max={}\n"),
        recipes.Num, recipes.Max);

    if (recipes.Num < 0 || recipes.Max < 0 || recipes.Num > 10000 || recipes.Max > 10000)
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: Recipes Num/Max out of range, aborting\n"));
        return;
    }

    // ── Collect valid recipe row data pointers ───────────────────────────────
    struct PendingRecipe {
        std::wstring name;
        uint8_t* srcData;   // pointer into the DataTable row
    };
    std::vector<PendingRecipe> pending;
    pending.reserve(m_addedRecipeNames.size());
    for (auto& recipeName : m_addedRecipeNames)
    {
        uint8_t* rowData = m_dtConstructionRecipes.findRowData(recipeName.c_str());
        if (!rowData)
        {
            VLOG(STR("[MoriaCppMod] [Def] DiscoverRecipe: row '{}' not in DT, skip\n"), recipeName);
            continue;
        }
        pending.push_back({recipeName, rowData});
    }
    if (pending.empty())
    {
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: no valid recipe rows found, aborting\n"));
        return;
    }

    // ── Grow Recipes TArray if needed ────────────────────────────────────────
    int32_t newNum = recipes.Num + static_cast<int32_t>(pending.size());
    if (newNum > recipes.Max)
    {
        int32_t newMax = newNum + (newNum / 2);
        if (newMax < newNum + 64) newMax = newNum + 64;

        size_t newBytes = static_cast<size_t>(newMax) * elemSize;
        uint8_t* newData = nullptr;
        if (recipes.Data && recipes.Max > 0)
        {
            newData = static_cast<uint8_t*>(FMemory::Realloc(recipes.Data, newBytes, 8));
        }
        else
        {
            // Fresh allocation
            newData = static_cast<uint8_t*>(FMemory::Malloc(newBytes, 8));
            if (newData && recipes.Data && recipes.Num > 0)
            {
                // Copy old data (shouldn't happen if Max>0, but be safe)
                std::memcpy(newData, recipes.Data, static_cast<size_t>(recipes.Num) * elemSize);
            }
        }
        if (!newData)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DiscoverRecipe: FMemory allocation failed for {} elements\n"), newMax);
            return;
        }
        recipes.Data = newData;
        recipes.Max = newMax;
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] Recipes TArray grown: Max={} Data=0x{:X}\n"),
            recipes.Max, reinterpret_cast<uintptr_t>(recipes.Data));
    }

    // ── Append each recipe to the Recipes TArray (master catalog) ──────────
    // The Recipes TArray stores FMorConstructionRecipeDefinition structs with a HIDDEN
    // FName at offset 0x08 (not reflected as UPROPERTY). The snapshot code writes the
    // DataTable row name there. DiscoverRecipe matches on this FName, so we must set it.
    static constexpr int HIDDEN_FNAME_OFFSET = 0x08;
    int appended = 0;
    for (auto& pr : pending)
    {
        uint8_t* dst = recipes.Data + static_cast<size_t>(recipes.Num) * elemSize;

        std::memset(dst, 0, elemSize);
        try { rowStruct->InitializeStruct(dst); } catch (...) {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] InitializeStruct THREW for '{}'\n"), pr.name);
        }
        // Deep copy from DataTable row (handles TArray/FString/FName properly)
        try {
            static_cast<UScriptStruct*>(rowStruct)->CopyScriptStruct(dst, pr.srcData);
        } catch (...) {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] CopyScriptStruct THREW for '{}', using memcpy\n"), pr.name);
            std::memcpy(dst, pr.srcData, elemSize);
        }
        // Write the hidden FName (row name) at offset 0x08
        FName rowFName(pr.name.c_str(), FNAME_Add);
        std::memcpy(dst + HIDDEN_FNAME_OFFSET, &rowFName, sizeof(FName));

        recipes.Num++;
        appended++;
    }

    // Write back the updated TArray header
    std::memcpy(dmBytes + RECIPES_OFFSET, &recipes, sizeof(TArrHdr));
    RC::Output::send<RC::LogLevel::Warning>(
        STR("[MoriaCppMod] [Def] Recipes TArray AFTER: Num={} (appended {})\n"),
        recipes.Num, appended);

    // ── Compare base-game vs our added Recipes entry (first 0x20 bytes) ────
    if (appended > 0 && recipes.Num > 0)
    {
        // Dump first base-game entry (index 0)
        uint8_t* baseEntry = recipes.Data;
        uint8_t* ourEntry  = recipes.Data + static_cast<size_t>(recipes.Num - appended) * elemSize;
        auto dumpHex = [](uint8_t* p, size_t len) -> std::wstring {
            std::wstring h;
            for (size_t b = 0; b < len; b++) {
                wchar_t buf[8]; swprintf(buf, 8, L"%02X ", p[b]);
                h += buf;
            }
            return h;
        };
        if (isReadableMemory(baseEntry, 0x20) && isReadableMemory(ourEntry, 0x20))
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Recipes[0] 0x00..0x1F: {}\n"), dumpHex(baseEntry, 0x20));
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Recipes[{}] 0x00..0x1F: {}\n"),
                recipes.Num - appended, dumpHex(ourEntry, 0x20));

            // Compare vtable pointers
            uintptr_t vtBase = 0, vtOur = 0;
            std::memcpy(&vtBase, baseEntry, 8);
            std::memcpy(&vtOur, ourEntry, 8);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Recipes vtable: base=0x{:X} ours=0x{:X} match={}\n"),
                vtBase, vtOur, (vtBase == vtOur) ? STR("YES") : STR("NO"));

            // Compare hidden FName at 0x08
            FName fnBase{}, fnOur{};
            std::memcpy(&fnBase, baseEntry + 0x08, sizeof(FName));
            std::memcpy(&fnOur, ourEntry + 0x08, sizeof(FName));
            std::wstring fnBaseStr = STR("???"), fnOurStr = STR("???");
            try { fnBaseStr = fnBase.ToString(); } catch (...) {}
            try { fnOurStr = fnOur.ToString(); } catch (...) {}
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Recipes FName@0x08: base='{}' ours='{}'\n"), fnBaseStr, fnOurStr);

            // Check EnabledState at 0x10
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] Recipes EnabledState@0x10: base={} ours={}\n"),
                baseEntry[0x10], ourEntry[0x10]);
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 2: Patch FGK ConstructionRecipeLookup TMap (BEFORE DiscoverRecipe)
    // ═════════════════════════════════════════════════════════════════════════
    // The FGK wrapper UMorConstructionRecipesTable is a SEPARATE UObject from the
    // DataTable. It has its own ConstructionRecipeLookup TMap at offset 0x0110,
    // built at asset load time. Our addRow() added rows to the raw UDataTable but
    // NOT to this FGK TMap. We must patch it BEFORE calling DiscoverRecipe, since
    // DiscoverRecipe likely uses the FGK system to validate recipe existence.
    static constexpr int    DISC_RECIPES_OFFSET = 0x0498 + 0x0118;  // = 0x05B0
    static constexpr size_t DR_ELEM_SIZE = 0x28;

    UFunction* countFn = nullptr;
    try {
        countFn = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr,
            STR("/Script/Moria.MorDiscoveryManager:GetDiscoveredConstructionRecipesCount"));
    } catch (...) {}

    {
        std::vector<UObject*> fgkTables;
        try { UObjectGlobals::FindAllOf(STR("MorConstructionRecipesTable"), fgkTables); } catch (...) {}
        if (fgkTables.empty())
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FGK wrapper NOT FOUND, cannot patch ConstructionRecipeLookup\n"));
        }
        else
        {
            auto* fgkBytes = reinterpret_cast<uint8_t*>(fgkTables[0]);
            static constexpr int LOOKUP_OFFSET = 0x0110;
            static constexpr size_t FGK_ELEM_SIZE = 0x20;  // 32 bytes per TSetElement

            struct TSparseHdr { uint8_t* Data; int32_t Num; int32_t Max; };
            TSparseHdr sparse{};
            if (isReadableMemory(fgkBytes + LOOKUP_OFFSET, sizeof(TSparseHdr)))
                std::memcpy(&sparse, fgkBytes + LOOKUP_OFFSET, sizeof(TSparseHdr));

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FGK TMap BEFORE: Num={} Max={}\n"), sparse.Num, sparse.Max);

            // Capture vtable from existing element[0]
            uintptr_t fgkVtable = 0;
            if (sparse.Data && sparse.Num > 0 && isReadableMemory(sparse.Data, 8))
                std::memcpy(&fgkVtable, sparse.Data, sizeof(uintptr_t));

            if (fgkVtable == 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] FGK TMap: cannot capture vtable, skipping\n"));
            }
            else
            {
                int32_t oldNum = sparse.Num;
                int32_t newTotal = oldNum + static_cast<int32_t>(pending.size());
                size_t oldBytes = static_cast<size_t>(oldNum) * FGK_ELEM_SIZE;
                size_t newBytes = static_cast<size_t>(newTotal) * FGK_ELEM_SIZE;

                uint8_t* newData = static_cast<uint8_t*>(FMemory::Realloc(sparse.Data, newBytes, 8));
                if (!newData)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] FGK TMap: Realloc failed\n"));
                }
                else
                {
                    std::memset(newData + oldBytes, 0, newBytes - oldBytes);

                    // Append new elements
                    for (size_t pi = 0; pi < pending.size(); pi++)
                    {
                        int elemIdx = oldNum + static_cast<int>(pi);
                        uint8_t* elem = newData + static_cast<size_t>(elemIdx) * FGK_ELEM_SIZE;
                        std::memcpy(elem + 0x00, &fgkVtable, sizeof(uintptr_t));
                        FName keyFName(pending[pi].name.c_str(), FNAME_Add);
                        std::memcpy(elem + 0x08, &keyFName, sizeof(FName));
                        std::memcpy(elem + 0x10, &keyFName, sizeof(FName));
                    }

                    // Rechain ALL elements into single bucket (degenerate O(N) but correct)
                    for (int i = 0; i < newTotal; i++)
                    {
                        uint8_t* elem = newData + static_cast<size_t>(i) * FGK_ELEM_SIZE;
                        int32_t nextId = (i < newTotal - 1) ? (i + 1) : -1;
                        std::memcpy(elem + 0x18, &nextId, 4);
                        int32_t bucketIdx = 0;
                        std::memcpy(elem + 0x1C, &bucketIdx, 4);
                    }

                    // Update allocation bitmap
                    std::memset(fgkBytes + LOOKUP_OFFSET + 0x10, 0xFF, 16); // inline bits
                    int32_t bitmapU32Count = (newTotal + 31) / 32;
                    uint32_t* heapBitmap = nullptr;
                    std::memcpy(&heapBitmap, fgkBytes + LOOKUP_OFFSET + 0x20, sizeof(uint32_t*));
                    uint32_t* newBitmap = static_cast<uint32_t*>(
                        FMemory::Realloc(heapBitmap, static_cast<size_t>(bitmapU32Count) * 4, 8));
                    if (newBitmap)
                    {
                        std::memset(newBitmap, 0xFF, static_cast<size_t>(bitmapU32Count) * 4);
                        int32_t trailingBits = newTotal % 32;
                        if (trailingBits != 0)
                            newBitmap[bitmapU32Count - 1] = (1u << trailingBits) - 1;
                        std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x20, &newBitmap, sizeof(uint32_t*));
                    }
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x28, &newTotal, 4); // NumBits
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x2C, &newTotal, 4); // MaxBits

                    // Free list: no free slots
                    int32_t minusOne = -1, zero = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x30, &minusOne, 4);
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x34, &zero, 4);

                    // Hash table: single bucket
                    int32_t headIdx = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x38, &headIdx, 4);
                    uintptr_t nullPtr = 0;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x40, &nullPtr, sizeof(uintptr_t));
                    int32_t hashSize = 1;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET + 0x48, &hashSize, 4);

                    // Write back TSparseArray header
                    sparse.Data = newData;
                    sparse.Num = newTotal;
                    sparse.Max = newTotal;
                    std::memcpy(fgkBytes + LOOKUP_OFFSET, &sparse, sizeof(TSparseHdr));

                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[MoriaCppMod] [Def] FGK TMap AFTER: Num={} (added {})\n"),
                        newTotal, static_cast<int>(pending.size()));
                }
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 3: Call DiscoverRecipe for each added recipe
    // ═════════════════════════════════════════════════════════════════════════
    // Now that Recipes TArray is expanded AND FGK TMap is patched,
    // DiscoverRecipe should find our recipes and:
    //   a) Add them to DiscoveredRecipes
    //   b) Update the category caches so they appear in the build menu
    {
        TArrHdr drBefore{};
        if (isReadableMemory(dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr)))
            std::memcpy(&drBefore, dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr));

        int32_t countBefore = 0;
        if (countFn) {
            uint8_t pb[16]{};
            try { discMgr->ProcessEvent(countFn, pb); std::memcpy(&countBefore, pb, 4); } catch (...) {}
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe: calling for {} recipes (DR.Num={}, Count={})\n"),
            pending.size(), drBefore.Num, countBefore);

        // Call DiscoverRecipe with bare name for all recipes
        for (auto& pr : pending)
        {
            FName fn(pr.name.c_str(), FNAME_Add);
            uint8_t pb[16]{};
            std::memcpy(pb, &fn, sizeof(FName));
            try { discMgr->ProcessEvent(discoverFn, pb); } catch (...) {}
        }

        TArrHdr drAfterBare{};
        if (isReadableMemory(dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr)))
            std::memcpy(&drAfterBare, dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr));
        int32_t deltaBare = drAfterBare.Num - drBefore.Num;

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] DiscoverRecipe(bare) x{}: DR delta={}\n"),
            pending.size(), deltaBare);

        // If bare name didn't work, try "Building." prefix
        int32_t totalDelta = deltaBare;
        if (deltaBare == 0)
        {
            for (auto& pr : pending)
            {
                std::wstring qn = STR("Building.") + pr.name;
                FName fn(qn.c_str(), FNAME_Add);
                uint8_t pb[16]{};
                std::memcpy(pb, &fn, sizeof(FName));
                try { discMgr->ProcessEvent(discoverFn, pb); } catch (...) {}
            }
            TArrHdr drAfterBldg{};
            if (isReadableMemory(dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr)))
                std::memcpy(&drAfterBldg, dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr));
            int32_t deltaBuilding = drAfterBldg.Num - drAfterBare.Num;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DiscoverRecipe(Building.) x{}: DR delta={}\n"),
                pending.size(), deltaBuilding);
            totalDelta = deltaBuilding;
        }

        int32_t countAfterDiscover = 0;
        if (countFn) {
            uint8_t pb[16]{};
            try { discMgr->ProcessEvent(countFn, pb); std::memcpy(&countAfterDiscover, pb, 4); } catch (...) {}
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[MoriaCppMod] [Def] After DiscoverRecipe: Count={} (was {})\n"),
            countAfterDiscover, countBefore);

        // ── Fallback: Manual DR insertion if DiscoverRecipe didn't work ──────
        if (totalDelta == 0)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] DiscoverRecipe produced no delta — using manual DR insertion\n"));

            TArrHdr drHdr{};
            if (isReadableMemory(dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr)))
                std::memcpy(&drHdr, dmBytes + DISC_RECIPES_OFFSET, sizeof(TArrHdr));

            uintptr_t handlePrefix = 0;
            if (drHdr.Num > 0 && drHdr.Data && isReadableMemory(drHdr.Data, DR_ELEM_SIZE))
                std::memcpy(&handlePrefix, drHdr.Data + 0x10, sizeof(uintptr_t));

            if (handlePrefix)
            {
                int32_t drNewNum = drHdr.Num + static_cast<int32_t>(pending.size());
                if (drNewNum > drHdr.Max)
                {
                    int32_t drNewMax = drNewNum + 64;
                    uint8_t* drNewData = static_cast<uint8_t*>(
                        FMemory::Realloc(drHdr.Data, static_cast<size_t>(drNewMax) * DR_ELEM_SIZE, 8));
                    if (drNewData) { drHdr.Data = drNewData; drHdr.Max = drNewMax; }
                }

                int drAppended = 0;
                for (auto& pr : pending)
                {
                    uint8_t* dst = drHdr.Data + static_cast<size_t>(drHdr.Num) * DR_ELEM_SIZE;
                    std::memset(dst, 0, DR_ELEM_SIZE);
                    int32_t repId = -(drHdr.Num + 1);
                    std::memcpy(dst + 0x00, &repId, 4);
                    int32_t mostRecent = -1;
                    std::memcpy(dst + 0x08, &mostRecent, 4);
                    std::memcpy(dst + 0x10, &handlePrefix, sizeof(uintptr_t));
                    std::wstring qualifiedName = STR("Building.") + pr.name;
                    FName recipeFName(qualifiedName.c_str(), FNAME_Add);
                    std::memcpy(dst + 0x18, &recipeFName, sizeof(FName));
                    dst[0x20] = 1; // bDiscoverSilent
                    drHdr.Num++;
                    drAppended++;
                }
                std::memcpy(dmBytes + DISC_RECIPES_OFFSET, &drHdr, sizeof(TArrHdr));
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] Manual DR insertion: appended {} (total DR.Num={})\n"),
                    drAppended, drHdr.Num);
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 4: Final diagnostics
    // ═════════════════════════════════════════════════════════════════════════
    {
        if (countFn)
        {
            uint8_t pb[16]{};
            try {
                discMgr->ProcessEvent(countFn, pb);
                int32_t count = 0;
                std::memcpy(&count, pb, 4);
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] FINAL GetDiscoveredConstructionRecipesCount: {}\n"), count);
            } catch (...) {}
        }

        // BP_GetDiscoveredConstructionRecipes
        UFunction* bpGetFn = nullptr;
        try { bpGetFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Moria.MorDiscoveryManager:BP_GetDiscoveredConstructionRecipes")); } catch (...) {}
        if (bpGetFn) {
            uint8_t retBuf[256]{};
            try {
                discMgr->ProcessEvent(bpGetFn, retBuf);
                TArrHdr retArr{};
                std::memcpy(&retArr, retBuf, sizeof(TArrHdr));
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [Def] FINAL BP_GetDiscoveredConstructionRecipes: {}\n"), retArr.Num);
            } catch (...) {}
        }

        // Category caches (read-only check — DO NOT clear)
        TArrHdr catCache{};
        if (isReadableMemory(dmBytes + 0x0268, sizeof(TArrHdr)))
        {
            std::memcpy(&catCache, dmBytes + 0x0268, sizeof(TArrHdr));
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FINAL CategoryGroupsCached.Num={}\n"), catCache.Num);
        }

        // Hidden TMaps (read-only diagnostic)
        for (int m = 0; m < 3; m++)
        {
            int offset = 0x02C8 + m * 0x50;
            int32_t num = 0;
            if (isReadableMemory(dmBytes + offset + 8, 4))
                std::memcpy(&num, dmBytes + offset + 8, 4);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [Def] FINAL HiddenMap[{}] @0x{:04X}: Num={}\n"), m, offset, num);
        }
    }

    if (!wasBound)
        m_dtConstructionRecipes.unbind();
}
