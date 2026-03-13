// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_datatable.inl — DataTable CRUD utility (runtime-only)              ║
// ║  Included inside the mod class body, after moria_common.inl.              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ════════════════════════════════════════════════════════════════════════════════
// DataTableUtil — read, write, and add rows to UDataTable at runtime
// ════════════════════════════════════════════════════════════════════════════════
//
// Usage:
//   DataTableUtil dt;
//   if (dt.bind(L"DT_Constructions")) {
//       auto names = dt.getRowNames();
//       int32_t val = dt.readInt32(L"MyRow", L"SomeProperty");
//       dt.writeInt32(L"MyRow", L"SomeProperty", 42);
//       dt.writeFloat(L"MyRow", L"SomeProperty", 1.5f);
//   }

struct DataTableUtil
{
    // ── Cached State ──
    UObject*    table{nullptr};
    UStruct*    rowStruct{nullptr};
    std::wstring tableName;
    int         rowStructOff{-2};
    int         rowSize{0};
    std::unordered_map<std::wstring, int> propOffsetCache;

    // ── Internal: RowMap raw memory header ──
    struct RowMapHeader { uint8_t* Data; int32_t Num; int32_t Max; };

    static constexpr int SET_ELEMENT_SIZE = 24;  // FName(8) + ptr(8) + hash(8)
    static constexpr int FNAME_SIZE = 8;

    bool getRowMapHeader(RowMapHeader& out) const
    {
        if (!table) return false;
        auto* base = reinterpret_cast<uint8_t*>(table);
        if (!isReadableMemory(base + DT_ROWMAP_OFFSET, 16)) return false;
        std::memcpy(&out, base + DT_ROWMAP_OFFSET, 16);
        return out.Data != nullptr && out.Num >= 0;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // bind — find a DataTable by name and cache its RowStruct
    // ════════════════════════════════════════════════════════════════════════════
    bool bind(const wchar_t* name)
    {
        table = nullptr; rowStruct = nullptr; rowSize = 0;
        propOffsetCache.clear(); rowStructOff = -2;
        tableName = name;

        std::vector<UObject*> dataTables;
        UObjectGlobals::FindAllOf(STR("DataTable"), dataTables);
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

    // ════════════════════════════════════════════════════════════════════════════
    // unbind — release cached state
    // ════════════════════════════════════════════════════════════════════════════
    void unbind()
    {
        table = nullptr; rowStruct = nullptr; rowSize = 0;
        propOffsetCache.clear(); rowStructOff = -2;
        tableName.clear();
    }

    bool isBound() const { return table != nullptr; }

    // ════════════════════════════════════════════════════════════════════════════
    // Row Enumeration
    // ════════════════════════════════════════════════════════════════════════════

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

    // ════════════════════════════════════════════════════════════════════════════
    // findRowData — get raw pointer to row data by name
    // ════════════════════════════════════════════════════════════════════════════

    uint8_t* findRowData(const wchar_t* rowName) const
    {
        if (!rowName || !rowName[0]) return nullptr;
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return nullptr;
        if (hdr.Num < 0 || hdr.Num > 100000) return nullptr;

        FName searchName(rowName, FNAME_Add);
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

    // ════════════════════════════════════════════════════════════════════════════
    // Property Offset Resolution (cached per property name)
    // ════════════════════════════════════════════════════════════════════════════

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

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 1: Typed Read Operations
    // ════════════════════════════════════════════════════════════════════════════

    // Internal: get row data + property offset, returns nullptr + -1 on failure
    std::pair<uint8_t*, int> locateField(const wchar_t* row, const wchar_t* prop)
    {
        uint8_t* data = findRowData(row);
        if (!data) return {nullptr, -1};
        int off = resolvePropertyOffset(prop);
        if (off < 0) return {nullptr, -1};
        return {data, off};
    }

    int32_t readInt32(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 4)) return 0;
        int32_t val; std::memcpy(&val, data + off, 4);
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
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 8)) return nullptr;
        UObject* obj; std::memcpy(&obj, data + off, 8);
        return obj;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // TArray Header (used by addRow and definition processing)
    // ════════════════════════════════════════════════════════════════════════════

    // TArray layout: { T* Data; int32 ArrayNum; int32 ArrayMax; } = 16 bytes
    struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 2: Typed Write Operations (in-place to existing row data)
    // ════════════════════════════════════════════════════════════════════════════

    bool writeInt32(const wchar_t* row, const wchar_t* prop, int32_t val)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 4)) return false;
        std::memcpy(data + off, &val, 4);
        return true;
    }

    bool writeFloat(const wchar_t* row, const wchar_t* prop, float val)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 4)) return false;
        std::memcpy(data + off, &val, 4);
        return true;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 3: Add Rows
    // Uses engine virtual functions via vtable dispatch for proper TMap hash maintenance
    // ════════════════════════════════════════════════════════════════════════════

    // VTable offsets for UDataTable virtual functions (UE 4.27)
    // Source: RE-UE4SS/generated_include/FunctionBodies/4_27_VTableOffsets_UDataTable_FunctionBody.cpp
    static constexpr uint32_t VTABLE_AddRowInternal = 0x278;  // void(FName, uint8*)

    // Call UDataTable::AddRowInternal(FName, uint8*) via vtable dispatch
    // Engine handles TMap insertion with proper hash bucket maintenance
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

    // Add an empty row with default-initialized fields
    // Uses engine AddRowInternal for proper RowMap hash insertion
    // Returns pointer to the new row data, or nullptr on failure
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

        // Check if row already exists
        if (findRowData(rowName))
        {
            VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' already exists in '{}'\n"),
                 std::wstring(rowName), tableName);
            return nullptr;
        }

        // Allocate and zero-fill row memory via engine allocator
        uint8_t* newRow = static_cast<uint8_t*>(FMemory::Malloc(rowSize, 8));
        if (!newRow)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[MoriaCppMod] [DT] addRow: FMemory::Malloc FAILED for '{}' (size={})\n"),
                std::wstring(rowName), rowSize);
            return nullptr;
        }
        std::memset(newRow, 0, rowSize);

        // Initialize struct defaults via engine virtual call
        try { rowStruct->InitializeStruct(newRow); }
        catch (...) {
            VLOG(STR("[MoriaCppMod] [DT] addRow: InitializeStruct failed for '{}', using zero-fill\n"),
                 std::wstring(rowName));
        }

        // Insert into RowMap via engine's AddRowInternal (proper hash maintenance)
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

// ════════════════════════════════════════════════════════════════════════════════
// Cached DataTableUtil instances + bind/unbind helpers
// ════════════════════════════════════════════════════════════════════════════════

DataTableUtil m_dtConstructions;
DataTableUtil m_dtConstructionRecipes;
DataTableUtil m_dtItems;
DataTableUtil m_dtWeapons;
DataTableUtil m_dtTools;
DataTableUtil m_dtArmor;
DataTableUtil m_dtConsumables;
DataTableUtil m_dtContainerItems;
DataTableUtil m_dtOres;

// ════════════════════════════════════════════════════════════════════════════════
// Cross-table lookups: DT_ConstructionRecipes → DT_Constructions
// ════════════════════════════════════════════════════════════════════════════════

// Extract the DT_Constructions row name from a DT_ConstructionRecipes row
// by reading ResultConstructionHandle.RowName (FName at handle + 0x08)
std::wstring resolveConstructionRowName(const wchar_t* recipeRowName)
{
    if (!m_dtConstructionRecipes.isBound()) return L"";
    uint8_t* rowData = m_dtConstructionRecipes.findRowData(recipeRowName);
    if (!rowData) return L"";
    int handleOff = m_dtConstructionRecipes.resolvePropertyOffset(L"ResultConstructionHandle");
    if (handleOff < 0) return L"";
    // FDataTableRowHandle::RowName at +0x08 (UE4 engine struct, standard layout)
    if (!isReadableMemory(rowData + handleOff + 0x08, 8)) return L"";
    FName rowName;
    std::memcpy(&rowName, rowData + handleOff + 0x08, 8);
    try { return rowName.ToString(); } catch (...) { return L""; }
}

// Look up the UTexture2D* icon from DT_Constructions for a given recipe row name
UObject* lookupRecipeIcon(const wchar_t* recipeRowName)
{
    if (!m_dtConstructions.isBound()) return nullptr;
    std::wstring constrName = resolveConstructionRowName(recipeRowName);
    if (constrName.empty()) return nullptr;
    return m_dtConstructions.readObjectPtr(constrName.c_str(), L"Icon");
}

