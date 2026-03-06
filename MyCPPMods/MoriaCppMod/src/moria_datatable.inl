// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_datatable.inl — DataTable CRUD utility (runtime-only)              ║
// ║  Included inside the mod class body, after moria_common.inl.              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ════════════════════════════════════════════════════════════════════════════════
// DataTableUtil — read, write, add, and remove rows from UDataTable at runtime
// ════════════════════════════════════════════════════════════════════════════════
//
// Usage:
//   DataTableUtil dt;
//   if (dt.bind(L"DT_Constructions")) {
//       dt.dumpRowStructFields();
//       auto names = dt.getRowNames();
//       float val = dt.readFloat(L"MyRow", L"SomeProperty");
//       dt.writeFloat(L"MyRow", L"SomeProperty", 42.0f);
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
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;
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
        RowMapHeader hdr{};
        if (!getRowMapHeader(hdr)) return nullptr;

        FName searchName(rowName);
        for (int32_t i = 0; i < hdr.Num; i++)
        {
            uint8_t* elem = hdr.Data + i * SET_ELEMENT_SIZE;
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;
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

    // Get property size for a given property name
    int getPropertySize(const wchar_t* propName) const
    {
        if (!rowStruct) return 0;
        for (auto* s = rowStruct; s; s = s->GetSuperStruct())
        {
            for (auto* prop : s->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(propName))
                    return prop->GetSize();
            }
        }
        return 0;
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

    float readFloat(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 4)) return 0.0f;
        float val; std::memcpy(&val, data + off, 4);
        return val;
    }

    bool readBool(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 1)) return false;
        return data[off] != 0;
    }

    uint8_t readUInt8(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 1)) return 0;
        return data[off];
    }

    std::wstring readFName(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, FNAME_SIZE)) return L"";
        FName fname;
        std::memcpy(&fname, data + off, FNAME_SIZE);
        try { return fname.ToString(); }
        catch (...) { return L"(error)"; }
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

    // Read raw bytes at a property offset
    std::vector<uint8_t> readRaw(const wchar_t* row, const wchar_t* prop, int size)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || size <= 0 || !isReadableMemory(data + off, size))
            return {};
        std::vector<uint8_t> buf(size);
        std::memcpy(buf.data(), data + off, size);
        return buf;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 1: TArray Read (raw element bytes)
    // ════════════════════════════════════════════════════════════════════════════

    // TArray layout: { T* Data; int32 ArrayNum; int32 ArrayMax; } = 16 bytes
    struct TArrayHeader { uint8_t* Data; int32_t Num; int32_t Max; };

    // Read TArray element count
    int32_t readTArrayCount(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 16)) return 0;
        TArrayHeader hdr;
        std::memcpy(&hdr, data + off, 16);
        return hdr.Num;
    }

    // Read all TArray elements as raw bytes (caller provides element size)
    std::vector<uint8_t> readTArrayRaw(const wchar_t* row, const wchar_t* prop, int elementSize)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || elementSize <= 0 || !isReadableMemory(data + off, 16))
            return {};
        TArrayHeader hdr;
        std::memcpy(&hdr, data + off, 16);
        if (hdr.Num <= 0 || !hdr.Data) return {};
        int totalBytes = hdr.Num * elementSize;
        if (!isReadableMemory(hdr.Data, totalBytes)) return {};
        std::vector<uint8_t> buf(totalBytes);
        std::memcpy(buf.data(), hdr.Data, totalBytes);
        return buf;
    }

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

    bool writeBool(const wchar_t* row, const wchar_t* prop, bool val)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 1)) return false;
        data[off] = val ? 1 : 0;
        return true;
    }

    bool writeUInt8(const wchar_t* row, const wchar_t* prop, uint8_t val)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 1)) return false;
        data[off] = val;
        return true;
    }

    bool writeFName(const wchar_t* row, const wchar_t* prop, const wchar_t* val)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, FNAME_SIZE)) return false;
        FName fname(val);
        std::memcpy(data + off, &fname, FNAME_SIZE);
        return true;
    }

    // Write raw bytes to a property offset
    bool writeRaw(const wchar_t* row, const wchar_t* prop, const uint8_t* src, int size)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || size <= 0 || !isReadableMemory(data + off, size)) return false;
        std::memcpy(data + off, src, size);
        return true;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 3: Add / Remove / Duplicate Rows (EXPERIMENTAL)
    // ════════════════════════════════════════════════════════════════════════════

    // Add an empty row with default-initialized fields
    // Returns pointer to the new row data, or nullptr on failure
    uint8_t* addRow(const wchar_t* rowName)
    {
        if (!table || !rowStruct || rowSize <= 0) return nullptr;

        // Check if row already exists
        if (findRowData(rowName))
        {
            VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' already exists in '{}'\n"),
                 std::wstring(rowName), tableName);
            return nullptr;
        }

        // Allocate and zero-fill row memory
        uint8_t* newRow = static_cast<uint8_t*>(FMemory::Malloc(rowSize, 8));
        if (!newRow)
        {
            VLOG(STR("[MoriaCppMod] [DT] addRow: FMemory::Malloc failed for '{}' (size={})\n"),
                 std::wstring(rowName), rowSize);
            return nullptr;
        }
        std::memset(newRow, 0, rowSize);

        // Initialize struct defaults if available
        try { rowStruct->InitializeStruct(newRow); }
        catch (...) { /* zero-fill is acceptable fallback */ }

        // Insert into RowMap via raw memory TMap manipulation
        FName fname(rowName);
        auto* base = reinterpret_cast<uint8_t*>(table);
        RowMapHeader hdr{};
        std::memcpy(&hdr, base + DT_ROWMAP_OFFSET, 16);

        // Check if we have space (Num < Max)
        if (hdr.Num < hdr.Max && hdr.Data)
        {
            // Write new entry at end: FName(8) + ptr(8) + hash padding(8)
            uint8_t* newElem = hdr.Data + hdr.Num * SET_ELEMENT_SIZE;
            if (isReadableMemory(newElem, SET_ELEMENT_SIZE))
            {
                std::memcpy(newElem, &fname, FNAME_SIZE);
                std::memcpy(newElem + FNAME_SIZE, &newRow, 8);
                std::memset(newElem + FNAME_SIZE + 8, 0, 8); // zero hash entry

                // Increment Num
                int32_t newNum = hdr.Num + 1;
                std::memcpy(base + DT_ROWMAP_OFFSET + 8, &newNum, 4);

                VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' added to '{}' (slot {}, size={})\n"),
                     std::wstring(rowName), tableName, hdr.Num, rowSize);
                return newRow;
            }
        }

        // Fallback: no space in existing array — would need TMap realloc
        VLOG(STR("[MoriaCppMod] [DT] addRow: '{}' no space in RowMap (Num={} Max={})\n"),
             std::wstring(rowName), hdr.Num, hdr.Max);
        FMemory::Free(newRow);
        return nullptr;
    }

    // Duplicate an existing row under a new name (shallow copy)
    // Returns pointer to the new row data, or nullptr on failure
    uint8_t* duplicateRow(const wchar_t* srcRowName, const wchar_t* newRowName)
    {
        if (!table || rowSize <= 0) return nullptr;

        uint8_t* srcData = findRowData(srcRowName);
        if (!srcData)
        {
            VLOG(STR("[MoriaCppMod] [DT] duplicateRow: source '{}' not found in '{}'\n"),
                 std::wstring(srcRowName), tableName);
            return nullptr;
        }

        if (findRowData(newRowName))
        {
            VLOG(STR("[MoriaCppMod] [DT] duplicateRow: target '{}' already exists in '{}'\n"),
                 std::wstring(newRowName), tableName);
            return nullptr;
        }

        // Allocate and shallow-copy
        uint8_t* newRow = static_cast<uint8_t*>(FMemory::Malloc(rowSize, 8));
        if (!newRow) return nullptr;
        std::memcpy(newRow, srcData, rowSize);

        // Insert via same mechanism as addRow
        FName fname(newRowName);
        auto* base = reinterpret_cast<uint8_t*>(table);
        RowMapHeader hdr{};
        std::memcpy(&hdr, base + DT_ROWMAP_OFFSET, 16);

        if (hdr.Num < hdr.Max && hdr.Data)
        {
            uint8_t* newElem = hdr.Data + hdr.Num * SET_ELEMENT_SIZE;
            if (isReadableMemory(newElem, SET_ELEMENT_SIZE))
            {
                std::memcpy(newElem, &fname, FNAME_SIZE);
                std::memcpy(newElem + FNAME_SIZE, &newRow, 8);
                std::memset(newElem + FNAME_SIZE + 8, 0, 8);

                int32_t newNum = hdr.Num + 1;
                std::memcpy(base + DT_ROWMAP_OFFSET + 8, &newNum, 4);

                VLOG(STR("[MoriaCppMod] [DT] duplicateRow: '{}' -> '{}' in '{}'\n"),
                     std::wstring(srcRowName), std::wstring(newRowName), tableName);
                return newRow;
            }
        }

        VLOG(STR("[MoriaCppMod] [DT] duplicateRow: no space in RowMap for '{}'\n"),
             std::wstring(newRowName));
        FMemory::Free(newRow);
        return nullptr;
    }

    // Remove a row by name (orphans memory — does NOT free, safe for runtime-only)
    bool removeRow(const wchar_t* rowName)
    {
        if (!table) return false;

        auto* base = reinterpret_cast<uint8_t*>(table);
        RowMapHeader hdr{};
        std::memcpy(&hdr, base + DT_ROWMAP_OFFSET, 16);
        if (hdr.Num <= 0 || !hdr.Data) return false;

        FName searchName(rowName);
        for (int32_t i = 0; i < hdr.Num; i++)
        {
            uint8_t* elem = hdr.Data + i * SET_ELEMENT_SIZE;
            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;
            if (std::memcmp(elem, &searchName, FNAME_SIZE) == 0)
            {
                // Move last element into this slot (swap-and-pop)
                int32_t lastIdx = hdr.Num - 1;
                if (i < lastIdx)
                {
                    uint8_t* lastElem = hdr.Data + lastIdx * SET_ELEMENT_SIZE;
                    std::memcpy(elem, lastElem, SET_ELEMENT_SIZE);
                }
                // Zero the old last slot
                uint8_t* lastElem = hdr.Data + lastIdx * SET_ELEMENT_SIZE;
                std::memset(lastElem, 0, SET_ELEMENT_SIZE);

                // Decrement Num
                int32_t newNum = hdr.Num - 1;
                std::memcpy(base + DT_ROWMAP_OFFSET + 8, &newNum, 4);

                VLOG(STR("[MoriaCppMod] [DT] removeRow: '{}' removed from '{}' (was slot {}, {} remaining)\n"),
                     std::wstring(rowName), tableName, i, newNum);
                return true;
            }
        }
        VLOG(STR("[MoriaCppMod] [DT] removeRow: '{}' not found in '{}'\n"),
             std::wstring(rowName), tableName);
        return false;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Phase 4: TArray Write (in-place modification)
    // ════════════════════════════════════════════════════════════════════════════

    // Clear a TArray (set Num=0, keep Data/Max intact — no memory freed)
    bool clearTArray(const wchar_t* row, const wchar_t* prop)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 16)) return false;
        int32_t zero = 0;
        std::memcpy(data + off + 8, &zero, 4); // ArrayNum = 0
        return true;
    }

    // Write elements into an existing TArray (must fit within current Max)
    bool writeTArrayRaw(const wchar_t* row, const wchar_t* prop,
                        const uint8_t* elements, int count, int elementSize)
    {
        auto [data, off] = locateField(row, prop);
        if (!data || !isReadableMemory(data + off, 16)) return false;
        TArrayHeader hdr;
        std::memcpy(&hdr, data + off, 16);

        if (count > hdr.Max || !hdr.Data)
        {
            VLOG(STR("[MoriaCppMod] [DT] writeTArrayRaw: count {} exceeds Max {} for '{}'\n"),
                 count, hdr.Max, tableName);
            return false;
        }

        int totalBytes = count * elementSize;
        if (!isReadableMemory(hdr.Data, totalBytes)) return false;
        std::memcpy(hdr.Data, elements, totalBytes);

        // Update ArrayNum
        int32_t num = count;
        std::memcpy(data + off + 8, &num, 4);
        return true;
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Diagnostics
    // ════════════════════════════════════════════════════════════════════════════

    void dumpRowNames() const
    {
        auto names = getRowNames();
        VLOG(STR("[MoriaCppMod] [DT] '{}': {} rows\n"), tableName, names.size());
        for (size_t i = 0; i < names.size(); i++)
        {
            VLOG(STR("[MoriaCppMod] [DT]   [{}] {}\n"), i, names[i]);
        }
    }

    void dumpRowStructFields() const
    {
        if (!rowStruct)
        {
            VLOG(STR("[MoriaCppMod] [DT] '{}' has no RowStruct\n"), tableName);
            return;
        }
        VLOG(STR("[MoriaCppMod] [DT] '{}' RowStruct='{}' size={}\n"),
             tableName, rowStruct->GetName(), rowSize);
        for (auto* s = rowStruct; s; s = s->GetSuperStruct())
        {
            for (auto* prop : s->ForEachProperty())
            {
                VLOG(STR("[MoriaCppMod] [DT]   {} @0x{:04X} size={} ({})\n"),
                     prop->GetName(), prop->GetOffset_Internal(),
                     prop->GetSize(), s->GetName());
            }
        }
    }
};

// ════════════════════════════════════════════════════════════════════════════════
// Cached DataTableUtil instances + bind/unbind helpers
// ════════════════════════════════════════════════════════════════════════════════

DataTableUtil m_dtConstructions;
DataTableUtil m_dtConstructionRecipes;
DataTableUtil m_dtItems;
DataTableUtil m_dtItemRecipes;
DataTableUtil m_dtWeapons;
DataTableUtil m_dtTools;
DataTableUtil m_dtArmor;

void bindAllDataTables()
{
    m_dtConstructions.bind(L"DT_Constructions");
    m_dtConstructionRecipes.bind(L"DT_ConstructionRecipes");
    m_dtItems.bind(L"DT_Items");
    m_dtItemRecipes.bind(L"DT_ItemRecipes");
    m_dtWeapons.bind(L"DT_Weapons");
    m_dtTools.bind(L"DT_Tools");
    m_dtArmor.bind(L"DT_Armor");
}

void unbindAllDataTables()
{
    m_dtConstructions.unbind();
    m_dtConstructionRecipes.unbind();
    m_dtItems.unbind();
    m_dtItemRecipes.unbind();
    m_dtWeapons.unbind();
    m_dtTools.unbind();
    m_dtArmor.unbind();
}

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

// Get the display name from DT_Constructions for a given recipe row name
std::wstring lookupRecipeDisplayName(const wchar_t* recipeRowName)
{
    if (!m_dtConstructions.isBound()) return L"";
    std::wstring constrName = resolveConstructionRowName(recipeRowName);
    if (constrName.empty()) return L"";
    return m_dtConstructions.readFText(constrName.c_str(), L"DisplayName");
}
