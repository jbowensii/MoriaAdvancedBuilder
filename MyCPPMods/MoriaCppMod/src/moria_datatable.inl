


struct DataTableUtil
{

    UObject*    table{nullptr};
    UStruct*    rowStruct{nullptr};
    std::wstring tableName;
    int         rowStructOff{-2};
    int         rowSize{0};
    std::unordered_map<std::wstring, int> propOffsetCache;


    struct RowMapHeader { uint8_t* Data; int32_t Num; int32_t Max; };

    static constexpr int SET_ELEMENT_SIZE = 24;
    static constexpr int FNAME_SIZE = 8;

    bool getRowMapHeader(RowMapHeader& out) const
    {
        if (!table) return false;
        auto* base = reinterpret_cast<uint8_t*>(table);
        if (!isReadableMemory(base + DT_ROWMAP_OFFSET, 16)) return false;
        std::memcpy(&out, base + DT_ROWMAP_OFFSET, 16);
        return out.Data != nullptr && out.Num >= 0;
    }


    bool bind(const wchar_t* name)
    {
        table = nullptr; rowStruct = nullptr; rowSize = 0;
        propOffsetCache.clear(); rowStructOff = -2;
        tableName = name;

        std::vector<UObject*> dataTables;
        findAllOfSafe(STR("DataTable"), dataTables); // v6.11.0 — SEH-wrapped
        for (auto* dt : dataTables)
        {
            if (!dt) continue;
            try {
                if (std::wstring(dt->GetName()) == name)
                {
                    table = dt;
                    break;
                }
            } catch (...) {}
        }
        if (!table)
        {
            VLOG(STR("[MoriaCppMod] [DT] '{}' not found\n"), tableName);
            return false;
        }

        resolveOffset(table, L"RowStruct", rowStructOff);
        if (rowStructOff >= 0)
        {
            auto* base = reinterpret_cast<uint8_t*>(table);
            rowStruct = *reinterpret_cast<UStruct**>(base + rowStructOff);
            if (rowStruct) rowSize = rowStruct->GetPropertiesSize();
        }

        VLOG(STR("[MoriaCppMod] [DT] Bound '{}' RowStruct={} rowSize={}\n"),
             tableName,
             rowStruct ? rowStruct->GetName() : STR("(null)"),
             rowSize);
        return true;
    }


    // v6.4.5+ — bind from an already-resolved UDataTable UObject (skip FindAllOf lookup)
    bool bindFromObject(UObject* dt, const wchar_t* logName = nullptr)
    {
        table = nullptr; rowStruct = nullptr; rowSize = 0;
        propOffsetCache.clear(); rowStructOff = -2;
        tableName = logName ? logName : (dt ? std::wstring(dt->GetName()) : L"(null)");
        if (!dt) return false;
        table = dt;
        resolveOffset(table, L"RowStruct", rowStructOff);
        if (rowStructOff >= 0)
        {
            auto* base = reinterpret_cast<uint8_t*>(table);
            rowStruct = *reinterpret_cast<UStruct**>(base + rowStructOff);
            if (rowStruct) rowSize = rowStruct->GetPropertiesSize();
        }
        VLOG(STR("[MoriaCppMod] [DT] BoundObj '{}' RowStruct={} rowSize={}\n"),
             tableName,
             rowStruct ? rowStruct->GetName() : STR("(null)"),
             rowSize);
        return true;
    }

    void unbind()
    {
        table = nullptr; rowStruct = nullptr; rowSize = 0;
        propOffsetCache.clear(); rowStructOff = -2;
        tableName.clear();
    }

    bool isBound() const { return table != nullptr; }


    int32_t getRowCount() const
    {
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return 0;
        return hdr.Num;
    }

    std::vector<std::wstring> getRowNames() const
    {
        std::vector<std::wstring> names;
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return names;
        names.reserve(hdr.Num);
        for (int32_t i = 0; i < hdr.Num; i++)
        {
            uint8_t* elem = hdr.Data + i * SET_ELEMENT_SIZE;
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) { VLOG(STR("[MoriaCppMod] DataTable row {} unreadable\n"), i); continue; }
            FName rowName;
            std::memcpy(&rowName, elem, FNAME_SIZE);
            try { names.push_back(rowName.ToString()); }
            catch (...) { names.push_back(L"(error)"); }
        }
        return names;
    }

    // v6.4.5+ — Return raw FName values from the RowMap without string round-tripping.
    // Preserves any Number suffix on auto-incremented FNames (e.g. Zone_5 vs FName("Zone", 5)).
    std::vector<FName> getRowNamesRaw() const
    {
        std::vector<FName> out;
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return out;
        out.reserve(hdr.Num);
        for (int32_t i = 0; i < hdr.Num; i++)
        {
            uint8_t* elem = hdr.Data + i * SET_ELEMENT_SIZE;
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;
            FName rowName;
            std::memcpy(&rowName, elem, FNAME_SIZE);
            out.push_back(rowName);
        }
        return out;
    }


    uint8_t* findRowData(const wchar_t* rowName) const
    {
        if (!rowName || !rowName[0]) return nullptr;
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return nullptr;
        if (hdr.Num < 0 || hdr.Num > 100000) return nullptr;

        FName searchName(rowName, FNAME_Find);
        for (int32_t i = 0; i < hdr.Num; i++)
        {
            uint8_t* elem = hdr.Data + i * SET_ELEMENT_SIZE;
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) { VLOG(STR("[MoriaCppMod] DataTable readRow: row {} unreadable\n"), i); continue; }
            if (std::memcmp(elem, &searchName, FNAME_SIZE) == 0)
            {
                uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + FNAME_SIZE);
                if (rowData && isReadableMemory(rowData, 8)) return rowData;
            }
        }
        return nullptr;
    }


    int resolvePropertyOffset(const wchar_t* propName)
    {
        std::wstring key(propName);
        auto it = propOffsetCache.find(key);
        if (it != propOffsetCache.end()) return it->second;

        int off = -1;
        if (rowStruct)
        {
            int cache = -2;
            resolveStructFieldOffset(rowStruct, propName, cache);
            off = cache;
        }
        propOffsetCache[key] = off;
        if (off < 0)
            VLOG(STR("[MoriaCppMod] [DT] Property '{}' not found in '{}'\n"),
                 std::wstring(propName), tableName);
        return off;
    }


    std::pair<uint8_t*, int> locateField(const wchar_t* row, const wchar_t* prop)
    {
        uint8_t* data = findRowData(row);
        if (!data) return {nullptr, -1};
        int off = resolvePropertyOffset(prop);
        if (off < 0) return {nullptr, -1};
        return {data, off};
    }


    struct LocatedField { uint8_t* data; int off; FProperty* prop; };
    LocatedField locateFieldWithProp(const wchar_t* row, const wchar_t* propName)
    {
        uint8_t* data = findRowData(row);
        if (!data) return {nullptr, -1, nullptr};
        int off = resolvePropertyOffset(propName);
        if (off < 0) return {nullptr, -1, nullptr};

        FProperty* prop = nullptr;
        for (auto* s = rowStruct; s && !prop; s = s->GetSuperStruct())
        {
            for (auto* p : s->ForEachProperty())
            {
                if (p->GetName() == std::wstring_view(propName))
                {
                    prop = p;
                    break;
                }
            }
        }
        return {data, off, prop};
    }

    int32_t readInt32(const wchar_t* row, const wchar_t* prop)
    {
        auto field = locateFieldWithProp(row, prop);
        if (!field.data || !field.prop) return 0;
        auto* numProp = CastField<FNumericProperty>(field.prop);
        if (numProp) return static_cast<int32_t>(numProp->GetSignedIntPropertyValue(field.data + field.off));

        if (!isReadableMemory(field.data + field.off, 4)) return 0;
        int32_t val; std::memcpy(&val, field.data + field.off, 4);
        return val;
    }

    std::wstring readFText(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 0x18)) return L"";


        FText* txt = reinterpret_cast<FText*>(data + off);
        try {
            if (txt && txt->Data && isReadableMemory(txt->Data, 8))
                return txt->ToString();
        } catch (...) {}
        return L"";
    }

    UObject* readObjectPtr(const wchar_t* row, const wchar_t* prop)
    {
        auto field = locateFieldWithProp(row, prop);
        if (!field.data || !field.prop) return nullptr;
        auto* objProp = CastField<FObjectPropertyBase>(field.prop);
        if (objProp) return objProp->GetObjectPropertyValue(field.data + field.off);

        if (!isReadableMemory(field.data + field.off, 8)) return nullptr;
        UObject* obj; std::memcpy(&obj, field.data + field.off, 8);
        return obj;
    }


    struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };


    bool writeInt32(const wchar_t* row, const wchar_t* prop, int32_t val)
    {
        auto field = locateFieldWithProp(row, prop);
        if (!field.data || !field.prop) return false;
        auto* numProp = CastField<FNumericProperty>(field.prop);
        if (numProp) { numProp->SetIntPropertyValue(field.data + field.off, static_cast<int64>(val)); return true; }
        if (!isReadableMemory(field.data + field.off, 4)) return false;
        std::memcpy(field.data + field.off, &val, 4);
        return true;
    }

    bool writeFloat(const wchar_t* row, const wchar_t* prop, float val)
    {
        auto field = locateFieldWithProp(row, prop);
        if (!field.data || !field.prop) return false;
        auto* numProp = CastField<FNumericProperty>(field.prop);
        if (numProp) { numProp->SetFloatingPointPropertyValue(field.data + field.off, static_cast<double>(val)); return true; }
        if (!isReadableMemory(field.data + field.off, 4)) return false;
        std::memcpy(field.data + field.off, &val, 4);
        return true;
    }


    static constexpr uint32_t VTABLE_AddRowInternal = 0x278;


    bool callAddRowInternal(const wchar_t* rowName, uint8_t* rowData)
    {
        if (!table || !rowData) return false;
        FName fname(rowName, FNAME_Add);
        try {
            std::byte* vt = std::bit_cast<std::byte*>(*std::bit_cast<std::byte**>(table));
            void* rawFunc = *std::bit_cast<void**>(vt + VTABLE_AddRowInternal);
            if (!rawFunc)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[MoriaCppMod] [DT] callAddRowInternal: vtable slot 0x{:X} is null!\n"),
                    VTABLE_AddRowInternal);
                return false;
            }
            using MFP = void(UObject::*)(FName, uint8_t*);
            auto func = std::bit_cast<MFP>(rawFunc);
            (table->*func)(fname, rowData);
            return true;
        } catch (const std::exception& e) {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [DT] callAddRowInternal EXCEPTION for '{}': {}\n"),
                std::wstring(rowName),
                std::wstring(std::string(e.what()).begin(), std::string(e.what()).end()));
            return false;
        } catch (...) {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [DT] callAddRowInternal UNKNOWN EXCEPTION for '{}'\n"),
                std::wstring(rowName));
            return false;
        }
    }


    uint8_t* addRow(const wchar_t* rowName)
    {
        if (!table || !rowStruct || rowSize <= 0)
        {
            VLOG(STR("[MoriaCppMod] [DT] addRow: precondition failed (table={}, rowStruct={}, rowSize={})\n"),
                 table ? STR("OK") : STR("NULL"),
                 rowStruct ? STR("OK") : STR("NULL"),
                 rowSize);
            return nullptr;
        }


        if (findRowData(rowName))
        {
            VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' already exists in '{}'\n"),
                 std::wstring(rowName), tableName);
            return nullptr;
        }


        uint8_t* newRow = static_cast<uint8_t*>(FMemory::Malloc(rowSize, 8));
        if (!newRow)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [DT] addRow: FMemory::Malloc FAILED for '{}' (size={})\n"),
                std::wstring(rowName), rowSize);
            return nullptr;
        }
        std::memset(newRow, 0, rowSize);


        try { rowStruct->InitializeStruct(newRow); }
        catch (...) {
            VLOG(STR("[MoriaCppMod] [DT] addRow: InitializeStruct failed for '{}', using zero-fill\n"),
                 std::wstring(rowName));
        }


        if (!callAddRowInternal(rowName, newRow))
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [DT] addRow: AddRowInternal FAILED for '{}' in '{}'\n"),
                std::wstring(rowName), tableName);
            FMemory::Free(newRow);
            return nullptr;
        }

        VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' added to '{}' via engine AddRowInternal (size={})\n"),
             std::wstring(rowName), tableName, rowSize);
        return newRow;
    }

};


DataTableUtil m_dtConstructions;
DataTableUtil m_dtConstructionRecipes;
DataTableUtil m_dtItems;
DataTableUtil m_dtWeapons;
DataTableUtil m_dtTools;
DataTableUtil m_dtArmor;
DataTableUtil m_dtConsumables;
DataTableUtil m_dtContainerItems;
DataTableUtil m_dtOres;


std::wstring resolveConstructionRowName(const wchar_t* recipeRowName)
{
    if (!m_dtConstructionRecipes.isBound()) return L"";
    uint8_t* rowData = m_dtConstructionRecipes.findRowData(recipeRowName);
    if (!rowData) return L"";
    int handleOff = m_dtConstructionRecipes.resolvePropertyOffset(L"ResultConstructionHandle");
    if (handleOff < 0) return L"";

    if (!isReadableMemory(rowData + handleOff + 0x08, 8)) return L"";
    FName rowName;
    std::memcpy(&rowName, rowData + handleOff + 0x08, 8);
    try { return rowName.ToString(); } catch (...) { return L""; }
}


UObject* lookupRecipeIcon(const wchar_t* recipeRowName)
{
    if (!m_dtConstructions.isBound()) return nullptr;
    std::wstring constrName = resolveConstructionRowName(recipeRowName);
    if (constrName.empty()) return nullptr;
    return m_dtConstructions.readObjectPtr(constrName.c_str(), L"Icon");
}

