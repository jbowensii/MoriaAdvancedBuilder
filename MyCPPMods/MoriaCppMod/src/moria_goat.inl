// moria_goat.inl — NUM- spawns a tame BP_NpcGoat_C that follows the player.
//
// Spawn path:
//   UGameplayStatics::BeginDeferredActorSpawnFromClass + FinishSpawningActor
//   (canonical reflection-callable spawn route — UWorld::SpawnActor is RAW C++).
//
// Flee suppression:
//   The goat's AIController owns FGKActorFSMComponent, FGKAIPerceptionComponent,
//   and FGKAITargetingComponent. Deactivating those three kills the awareness-
//   based behavior states (BSt_Wander_Prey_Large, BSt_Prey_Run from the
//   BSt_NPCGoatRoot tree) so the goat stays passive and ignores the player.
//
// Follow:
//   Once per second per goat, AAIController::MoveToActor(player, radius=250).
//   MoveToActor is a UFUNCTION (BlueprintCallable) — direct PE dispatch.
//
// Lifetime: goats persist for the play session. They die on world unload
// along with everything else; pruning here just walks weak pointers and
// drops dead entries on the next tick.

// Lazy-bound UClass + UFunction caches. Resolved on first NUM- press,
// re-used across spawns. UClass / static UFunction pointers are stable
// for the lifetime of the engine, so plain pointers are safe (no GC).
UClass* m_goatBPClass{nullptr};
UObject* m_kismetGameplayStaticsCDO{nullptr};
UFunction* m_goatBeginSpawnFn{nullptr};
UFunction* m_goatFinishSpawnFn{nullptr};
// The skin material the user wants applied to every spawned goat.
// Resolved lazily; persists for the session once loaded.
UObject* m_goatSkinMaterial{nullptr};
// Adventurer's-pack item BP class (BP_EpicPack_AdventurersPack_Large_C).
// Preloaded at character-load so the spawn-tick equip call has zero
// blocking I/O on the game thread.
UClass* m_packItemClass{nullptr};

// [v7.2.0-rc.3 2026-05-22] Tobi's porter-goat saddlebag class.
// v1.5.0 ships with the slot-wrapper BP_ContainerItem_Goat_Slot_EpicPack_C
// populated in the goat's InvComp but no actual saddlebag in the slot
// (Tobi's BeginPlay graph doesn't fill it). When [GoatExperimental]
// AutoEquipSaddleBag = true, we equip BP_PorterGoatSaddlebags_C onto
// the goat's MorEquipComponent post-spawn so the slot actually holds
// a real bag and clicking Saddlebags opens the storage grid.
UClass* m_saddlebagItemClass{nullptr};
bool m_autoEquipSaddleBag{true}; // INI-toggleable; default ON for rc.3 testing

// [v7.2.0-rc.4 2026-05-22] Goat saddlebag inventory widget tracking.
// Per Desktop Claude's recon: vanilla FRG ships
// WBP_UI_Inventory_Screen_StorageMode_C with isOpenedFromNPC /
// AssociatedNPC / InventoryComponent / Target exposed on spawn.
//
// [rc.5 2026-05-22] DEFAULT OFF — rc.4 testing showed the widget
// spawns and accepts bindings BUT renders with placeholder template
// text + displaces player inventory items + has no built-in close
// path. The widget's internal storage-resolve logic likely hits the
// tag mismatch ("Goat.Slot.EpicPack" vs widget's expected
// "Inventory.Slot.EpicPack") and falls back to player-side state.
// Until we work that out, users opt in via INI.
UClass* m_goatSaddlebagWidgetCls{nullptr};
UObject* m_goatSaddlebagWidget{nullptr};
bool m_enableGoatSaddleUI{false}; // INI: [GoatExperimental] EnableGoatSaddleUI = true

// UTF-8 double-encoding tolerance: saves may hold a mojibake copy
// of the goat name (see memory goat-final-architecture → "name
// encoding"). Matching both forms lets the marker scans find the
// entry; the idempotent identity write then repairs it.
std::wstring goatNameMojibake() const
{
    std::wstring out;
    for (wchar_t c : m_goatName)
    {
        if (c < 0x80)
            out += c;
        else if (c < 0x800)
        {
            out += static_cast<wchar_t>(0xC0 | (c >> 6));
            out += static_cast<wchar_t>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<wchar_t>(0xE0 | (c >> 12));
            out += static_cast<wchar_t>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<wchar_t>(0x80 | (c & 0x3F));
        }
    }
    return out;
}
bool isGoatNameMatch(const std::wstring& s) const
{
    return s == m_goatName || s == goatNameMojibake();
}
bool m_goatSaddleWrapperDumped{false}; // rc.6 one-shot wrapper diagnostic

// [v7.2.0-rc.12 2026-05-22] Phantom chest infrastructure.
// Desktop Claude's plan: spawn a hidden BP_StorageChest_Construction
// attached to the goat. Vanilla chest's FSM handles owner-validation,
// widget spawn, and UI hand-off — the path rc.4-rc.10 couldn't
// synthesize from scratch. When user clicks "Saddlebags" on goat
// menu, DLL calls the chest's OpenChest UFunction directly.
//
// 12a: visible chest spawn above goat — verify class resolves +
//      OpenChest works in clean isolation.
// 12b: attach to goat + hide mesh.
// 12c: replace rc.11 saddlebags no-op with OpenChest dispatch.
// 12d: suppress chest's E-prompt.
UClass* m_phantomChestClass{nullptr};
RC::Unreal::FWeakObjectPtr m_phantomChest;
bool m_enablePhantomChest{false}; // INI: [GoatExperimental] PhantomChest = true

// [v7.2.0-rc.12b 2026-05-23] Saddlebag-as-world-actor experiment.
// Spawn BP_SaddleBags_Goat_C at goat's feet — bag is itself an
// AInventoryItem subclass (so an AActor). Bag exposes its own
// open/use API (same one right-clicking it in player inventory
// triggers). After spawn, probe + auto-call its Open* / Use* /
// Interact* UFunctions and see if any surface the proper "Goat
// Saddlebags" UI.
RC::Unreal::FWeakObjectPtr m_saddlebagWorldActor;
bool m_enableSaddlebagAtGoat{false}; // INI: [GoatExperimental] SaddlebagAtGoat = true
// Static mesh of the dwarven mountaineer pack (Dwarf_Pack01_Static).
// We swap this into the goat's existing Hat StaticMeshComponent slot
// (instead of clearing it to null) so a visible pack appears without
// needing to spawn new components at runtime.
UObject* m_packStaticMesh{nullptr};

// Cached vanilla AAIController class (for replacing the BP_Goat_AIController_C).
UClass* m_vanillaAIControllerClass{nullptr};

struct FollowGoatRecord
{
    RC::Unreal::FWeakObjectPtr pawn;
    RC::Unreal::FWeakObjectPtr controller; // current possessing controller
    ULONGLONG lastMoveTickMs{0};
    ULONGLONG lastCtrlAttemptLogMs{0};
    int ctrlAttempts{0};
    bool fleeSuppressed{false};
    bool componentsLogged{false};
    bool controllerReplaced{false}; // vanilla controller swap done
    bool porterRoleAssigned{false}; // SetRoleFuzzy("Porter") fired
    int postEquipDumpAttempts{0};   // 3-tick delay before post-equip diag fires
    // v1.1.0 diagnostics: count ticks since spawn so we can fire a
    // post-registration component dump (looking for whatever new
    // AI / wander source registration added) and log MoveToActor
    // dispatches for the first N seconds.
    int ticksSinceSpawn{0};
    bool postRegDumpDone{false};
    int moveToActorLogsRemaining{8}; // log first 8 MoveToActors then go quiet
    bool interactiveRefired{false};  // v1.1.0 fixup: re-enable interaction on existing goats
    bool bellSpawned{false};         // [rc.46] bell-summoned (skip porter role, run MoveToActor tick)
    bool stayMode{false};            // [rc.52] goat-menu Stay button: skip MoveToActor
    float lastDiagPos[3]{0, 0, 0};   // [rc.138] follow-motion diagnostic: last sampled goat location
    ULONGLONG lastDiagMs{0};         // [rc.138] timestamp of last motion sample
    bool maxSpeedLogged{false};      // [rc.138] one-shot MaxWalkSpeed log
    bool brainStopped{false};        // [v8.2.x] one-shot StopLogic on the registered-NPC brain (~1s post-spawn, after possession)
    ULONGLONG lastBrainStopMs{0};    // [v8.2.x] FOLLOW-ALWAYS: 10s re-assert timestamp for the FSM disable
};
std::vector<FollowGoatRecord> m_followGoats;

// Companion goat is single-instance only. NUM- is now a toggle:
// press to spawn if none exists, press to despawn if one does.
static constexpr size_t MAX_FOLLOW_GOATS = 1;

// [rc.59 AUTO-RESTORE 2026-06-26] Per save-system-architecture
// empirical work: the DT_NPCUniqueCharacters row path is
// structurally incompatible with bell-ring registration
// (populating ValidNpcRestores forces RegisterWithNPCManager
// into "find existing entry" mode → bell-ring stops creating
// entries → saddlebag UI breaks). See
// [[feedback_validnpcrestores_trap]] for the full analysis.
//
// Path C instead: on world load, scan NpcInfo for entries
// with Name='Rûdh' (our marker from prior bell-rings). If
// found AND no live BP_NpcGoat has matching NpcGuid, invoke
// spawnBellGoat() — existing GuidAdopt logic finds the
// marker entry and binds the new actor to it.
//
// Fires once at ~5s post-character-load (after Probe N).
// Vanilla devs never finished goat persistence wiring —
// this completes it from the DLL side without touching
// any DataTable.
bool m_autoRestoreFired{false};

// ───── helpers ──────────────────────────────────────────────────

// Blocking load for a Blueprint-generated class that isn't yet resident
// in the engine's object table. Mirrors moria_join_world_ui.inl's
// jw_loadAssetBlocking pattern but uses LoadClassAsset_Blocking
// (TSoftClassPtr<UObject>) instead of LoadAsset_Blocking. The 40-byte
// soft-pointer layout is identical between TSoftObjectPtr and
// TSoftClassPtr in 4.27 — the FName lives at offset 0.
// Enumerate UClass instances whose name contains "Goat" — diagnostic
// helper used when class-path loads fail. Confirms whether the asset
// even exists in the running process's UObject table under any name.
void goat_dumpResidentGoatClasses()
{
    std::vector<UObject*> classes;
    if (!seh_findAllOf(STR("BlueprintGeneratedClass"), &classes))
    {
        VLOG(STR("[MoriaCppMod] [Goat] dump: FindAllOf<BlueprintGeneratedClass> failed\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Goat] dump: {} BlueprintGeneratedClass instances resident\n"), classes.size());
    int hits = 0;
    for (auto* c : classes)
    {
        if (!c || !isObjectAlive(c)) continue;
        std::wstring name = c->GetName();
        if (name.find(L"Goat") == std::wstring::npos) continue;
        std::wstring path = c->GetPathName();
        VLOG(STR("[MoriaCppMod] [Goat] resident class: {} (path={})\n"), name.c_str(), path.c_str());
        if (++hits >= 20)
        {
            VLOG(STR("[MoriaCppMod] [Goat] (cut at 20)\n"));
            break;
        }
    }
    if (hits == 0) VLOG(STR("[MoriaCppMod] [Goat] no *Goat* in {} BlueprintGeneratedClass results\n"), classes.size());
}

// Generic blocking soft-asset load via KismetSystemLibrary. fnSubpath
// is "LoadAsset_Blocking" or "LoadClassAsset_Blocking"; parmName is
// "Asset" or "AssetClass". Returns the UFUNCTION's UObject* return
// (caller casts to UClass* if appropriate). Logs FName + offset
// diagnostics so a null return reveals which step failed.
UObject* goat_callBlockingLoader(const wchar_t* fnPath, const wchar_t* cdoPath, const wchar_t* parmName, const wchar_t* assetPath)
{
    try
    {
        auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, fnPath);
        auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, cdoPath);
        if (!fn || !cdo)
        {
            VLOG(STR("[MoriaCppMod] [Goat] loader '{}' unresolved (fn={} cdo={})\n"), fnPath, (void*)fn, (void*)cdo);
            return nullptr;
        }
        auto* pAsset = findParam(fn, parmName);
        auto* pRet = findParam(fn, STR("ReturnValue"));
        if (!pAsset || !pRet)
        {
            VLOG(STR("[MoriaCppMod] [Goat] loader '{}' missing parms (asset={} ret={})\n"), fnPath, (void*)pAsset, (void*)pRet);
            return nullptr;
        }

        int sz = fn->GetParmsSize();
        std::vector<uint8_t> buf(sz, 0);

        RC::Unreal::FName name(assetPath, RC::Unreal::FNAME_Add);
        uint32_t ci = name.GetComparisonIndex();
        uint32_t num = name.GetNumber();

        int aOff = pAsset->GetOffset_Internal();
        int rOff = pRet->GetOffset_Internal();
        VLOG(STR("[MoriaCppMod] [Goat] loader='{}' assetPath='{}' ci={} num={} parmSize={} aOff={} rOff={}\n"), fnPath, assetPath, ci, num, sz, aOff, rOff);
        if (ci == 0) return nullptr;

        // [v7.1.0-rc.39.4 OFFSET FIX 2026-05-12] TSoftObjectPtr /
        // TSoftClassPtr layout (UE4.27):
        //   off=0   FWeakObjectPtr WeakPtr (8 bytes)
        //   off=8   int32 TagAtLastTest (4 bytes)
        //   off=12  4-byte padding
        //   off=16  FName AssetPathName (8 bytes)   <--- THIS is where we write
        //   off=24  FString SubPathString (16 bytes)
        // Total: 40 bytes. Previous code wrote FName at off=0 (inside
        // WeakPtr) so LoadAsset_Blocking received a null path and
        // returned null. Confirmed via UE4SS PersistentObjectPtr.hpp.
        uint8_t* tspc = buf.data() + aOff;
        // Leave bytes 0..15 zeroed (clean WeakPtr + TagAtLastTest).
        // Write FName at offset 16 of the soft-ptr struct.
        std::memcpy(tspc + 16, &ci, 4);
        std::memcpy(tspc + 20, &num, 4);
        // bytes 24..39 = FString SubPathString, stays zeroed (empty FString)

        if (!safeProcessEvent(cdo, fn, buf.data())) return nullptr;
        UObject* result = *reinterpret_cast<UObject**>(buf.data() + rOff);
        VLOG(STR("[MoriaCppMod] [Goat] loader '{}' => {:p}\n"), fnPath, (void*)result);
        return result;
    }
    catch (...)
    {
        VLOG(STR("[MoriaCppMod] [Goat] loader '{}' EXCEPTION\n"), fnPath);
        return nullptr;
    }
}

UClass* goat_loadClassAssetBlocking(const wchar_t* classPath)
{
    if (!classPath) return nullptr;
    // 1) Preferred: LoadClassAsset_Blocking (correctly typed for UClass).
    UObject* r = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadClassAsset_Blocking"),
                                         STR("/Script/Engine.Default__KismetSystemLibrary"),
                                         STR("AssetClass"),
                                         classPath);
    if (r) return reinterpret_cast<UClass*>(r);

    // 2) Fallback: LoadAsset_Blocking — will load the package and may
    // resolve the class as a UObject*. Cooked-build asset registries
    // sometimes prefer this path when the class hasn't been touched
    // by the runtime asset index yet.
    VLOG(STR("[MoriaCppMod] [Goat] LoadClassAsset_Blocking returned null; trying LoadAsset_Blocking\n"));
    r = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                STR("/Script/Engine.Default__KismetSystemLibrary"),
                                STR("Asset"),
                                classPath);
    if (r) return reinterpret_cast<UClass*>(r);

    // 3) Diagnostic: still null. Dump anything *Goat* that IS resident
    // so we can see whether the engine has the asset under a different
    // name / path, or if it's genuinely not in the loaded paks.
    goat_dumpResidentGoatClasses();

    // 4) Last-chance: walk currently-resident BlueprintGeneratedClass
    // instances looking for "BP_NpcGoat_C" by name. If a single goat
    // has ever been spawned in this session (asset registry chunked it
    // in), this works without touching the loader UFunction.
    std::vector<UObject*> classes;
    if (seh_findAllOf(STR("BlueprintGeneratedClass"), &classes))
    {
        for (auto* c : classes)
        {
            if (!c || !isObjectAlive(c)) continue;
            if (c->GetName() == std::wstring_view(STR("BP_NpcGoat_C")))
            {
                VLOG(STR("[MoriaCppMod] [Goat] found resident BP_NpcGoat_C via FindAllOf scan: {:p}\n"), (void*)c);
                return reinterpret_cast<UClass*>(c);
            }
        }
    }
    return nullptr;
}

// Resolve the user's preferred goat skin material. Tries StaticFindObject
// first (instant if already resident in memory), then falls back to a
// blocking package load. Caches the result on the mod instance.
UObject* ensureGoatSkinMaterial()
{
    if (m_goatSkinMaterial && isObjectAlive(m_goatSkinMaterial)) return m_goatSkinMaterial;
    const wchar_t* matPath = STR("/Game/CharacterArt/Creatures/Goat/Materials/MI_Goat.MI_Goat");
    m_goatSkinMaterial = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, matPath);
    if (!m_goatSkinMaterial)
    {
        m_goatSkinMaterial = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                                     STR("/Script/Engine.Default__KismetSystemLibrary"),
                                                     STR("Asset"),
                                                     matPath);
    }
    if (m_goatSkinMaterial)
        VLOG(STR("[MoriaCppMod] [Goat] skin material resolved: {:p}\n"), (void*)m_goatSkinMaterial);
    else
        VLOG(STR("[MoriaCppMod] [Goat] skin material NOT loaded ({})\n"), matPath);
    return m_goatSkinMaterial;
}

// Apply MI_Goat to every material slot on the goat. User explicitly
// wants all slots to use this skin even though it removes the
// alpha-cutout hair-card fur silhouette on slot 0.
void applyGoatSkin(UObject* meshComp)
{
    if (!meshComp || !isObjectAlive(meshComp)) return;
    UObject* mi = ensureGoatSkinMaterial();
    if (!mi) return;

    // Diagnostic: log each slot's current material before override.
    // Empty slots return null and are silently ignored.
    auto* getMatFn = meshComp->GetFunctionByNameInChain(STR("GetMaterial"));
    auto* setMatFn = meshComp->GetFunctionByNameInChain(STR("SetMaterial"));
    if (!setMatFn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] SetMaterial UFunction missing on mesh\n"));
        return;
    }

    int applied = 0;
    int getSz = getMatFn ? getMatFn->GetParmsSize() : 0;
    int setSz = setMatFn->GetParmsSize();
    for (int slot = 0; slot < 8; ++slot)
    {
        if (getMatFn)
        {
            std::vector<uint8_t> gbuf(getSz, 0);
            writeGoatParm<int32_t>(getMatFn, gbuf.data(), STR("ElementIndex"), slot);
            if (safeProcessEvent(meshComp, getMatFn, gbuf.data()))
            {
                UObject* cur = readGoatParm<UObject*>(getMatFn, gbuf.data(), STR("ReturnValue"), nullptr);
                if (cur && isObjectAlive(cur))
                {
                    std::wstring nm;
                    try
                    {
                        nm = cur->GetName();
                    }
                    catch (...)
                    {
                    }
                    VLOG(STR("[MoriaCppMod] [Goat] mesh slot {} was: {}\n"), slot, nm.c_str());
                }
                else
                    continue; // empty slot — skip override
            }
        }
        std::vector<uint8_t> sbuf(setSz, 0);
        writeGoatParm<int32_t>(setMatFn, sbuf.data(), STR("ElementIndex"), slot);
        writeGoatParm<UObject*>(setMatFn, sbuf.data(), STR("Material"), mi);
        safeProcessEvent(meshComp, setMatFn, sbuf.data());
        ++applied;
    }
    VLOG(STR("[MoriaCppMod] [Goat] applied MI_Goat to {} mesh slot(s)\n"), applied);
}

// Candidate paths in priority order. BP_NpcGoat_C (the porter variant
// with bells + pack equipment) is preferred per user request. Falls
// back to BP_Fauna_Goat_C (wild goat) if NpcGoat isn't loadable in
// this session. The AssetRegistry path scan at character-load is what
// makes BP_NpcGoat_C loadable without first walking past a settlement.
// [rc.49.1 2026-05-12] v1.3.0 ships modded BP_NpcGoat at the SAME
// vanilla path (override, not subclass). All porter wiring lives
// in the BP's CDO + SCS edits + interaction-struct clones. Runtime
// just needs SpawnActor + SpawnDefaultController + SetIsInteractive.
// [rc.15 PATH A 2026-05-24] BP_PorterGoat_C (Desktop Claude's
// PsiPath_PathA_P sibling-clone) is primary. It carries the v1.6.0
// porter customizations baked in (mesh + StorageHandle + CharacterData
// redirect → DA_NpcGoat_PorterLoadout → BP_SaddleBags_Goat at slot 4),
// is its own class identity so save-graph constraints don't bind
// against existing BP_NpcGoat instances. Falls back to BP_NpcGoat_C
// (Tobi's v1.6.0 baseline) if PathA pak isn't installed, then to
// BP_Fauna_Goat_C (vanilla wild goat) as last resort.
// [rc.98 2026-07-10] SUMMON TOBI'S GOAT ONLY. Tobi confirmed his design
// has no summon — our mod owns it, and it must spawn HIS goat
// (BP_NpcGoat_C: StorageHandle=Goat.Slot.EpicPack + DefaultContainers +
// NPCGoat DT row + persistor wiring). The old PathA primary
// BP_PorterGoat_C is our abandoned custom clone — it is NOT in Tobi's
// pak (not in the v1.12.0 changeset); resolving it yields a stale class
// with GetName()='' whose BeginDeferred throws an SEH (the 4.3.3 crash).
// BP_Fauna_Goat_C is the vanilla wild goat (no saddlebag wiring) — a
// silent wrong-goat. Both removed: spawn BP_NpcGoat_C or fail loudly.
static constexpr const wchar_t* GOAT_CLASS_PATHS[] = {
        STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"),
};
static constexpr const wchar_t* GOAT_CLASS_NAMES[] = {
        STR("BP_NpcGoat_C"),
};

// [rc.15 PATH A 2026-05-24] FindAllOf is exact-class match — under
// Path A the live goat may be BP_PorterGoat_C (preferred) or
// BP_NpcGoat_C (Tobi-only install) or BP_Fauna_Goat_C (no mods at
// all). Walk every known goat class name and accumulate. Used by
// every "find live goat in world" call site.
bool seh_findAnyGoatActor(std::vector<UObject*>* out)
{
    if (!out) return false;
    out->clear();
    bool any = false;
    for (auto* nm : GOAT_CLASS_NAMES)
    {
        std::vector<UObject*> hit;
        if (seh_findAllOf(nm, &hit))
        {
            any = true;
            for (UObject* g : hit)
                out->push_back(g);
        }
    }
    return any;
}

// Force the AssetRegistry to scan a directory path on disk so that
// chunk-streamed assets (like BP_NpcGoat) become loadable without first
// visiting the zone they normally spawn in. Calls
// UAssetRegistryImpl::ScanPathsSynchronous (UFUNCTION) with a one-element
// TArray<FString>. The TArray + FString memory MUST come from
// FMemory::Malloc so the engine's destructor can FMemory::Free it after
// our ProcessEvent returns — allocator-mismatch (CRT new vs FMemory)
// would crash on cleanup.
void scanAssetRegistryPath(const wchar_t* path)
{
    if (!path) return;

    // 1) Find the AssetRegistry singleton. The cooked class name differs
    // from the editor name, so FindAllOf("AssetRegistryImpl") isn't
    // reliable. Use the canonical UAssetRegistryHelpers::GetAssetRegistry
    // UFUNCTION which returns a TScriptInterface<IAssetRegistry> — the
    // first 8 bytes of which is the UObject* registry instance.
    UObject* reg = nullptr;
    {
        auto* getFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/AssetRegistry.AssetRegistryHelpers:GetAssetRegistry"));
        auto* helpersCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/AssetRegistry.Default__AssetRegistryHelpers"));
        if (getFn && helpersCDO)
        {
            int gsz = getFn->GetParmsSize();
            std::vector<uint8_t> gbuf(gsz, 0);
            if (safeProcessEvent(helpersCDO, getFn, gbuf.data()))
            {
                auto* pRet = findParam(getFn, STR("ReturnValue"));
                if (pRet)
                {
                    // TScriptInterface<T> layout: { UObject* Object; void* Interface; }
                    reg = *reinterpret_cast<UObject**>(gbuf.data() + pRet->GetOffset_Internal());
                }
            }
        }
    }
    // Fallback: try class-name-based singleton lookup with a couple of
    // plausible names if the helper UFUNCTION isn't accessible.
    if (!reg)
    {
        static constexpr const wchar_t* kRegistryNames[] = {
                STR("AssetRegistryImpl"),
                STR("AssetRegistry"),
                STR("IAssetRegistry"),
        };
        for (auto* nm : kRegistryNames)
        {
            std::vector<UObject*> regs;
            if (seh_findAllOf(nm, &regs) && !regs.empty())
            {
                reg = regs[0];
                break;
            }
        }
    }
    if (!reg)
    {
        VLOG(STR("[MoriaCppMod] [Goat] AssetRegistry singleton not found; skip path scan {}\n"), path);
        return;
    }
    VLOG(STR("[MoriaCppMod] [Goat] AssetRegistry instance: {:p} class={}\n"), (void*)reg, safeClassName(reg).c_str());

    auto* fn = reg->GetFunctionByNameInChain(STR("ScanPathsSynchronous"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ScanPathsSynchronous UFunction missing on registry\n"));
        return;
    }
    auto* pInPaths = findParam(fn, STR("InPaths"));
    if (!pInPaths)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ScanPathsSynchronous missing 'InPaths' param\n"));
        return;
    }

    // 2) Build TArray<FString> InPaths = { path } using FMemory::Malloc.
    //    Layouts (UE4.27 64-bit):
    //      FString  = { TCHAR* data, int32 num, int32 max }   → 16 bytes
    //      TArray   = { void*  data, int32 num, int32 max }   → 16 bytes
    int32_t pathLen = static_cast<int32_t>(wcslen(path)) + 1; // include null term
    void* pathBuf = FMemory::Malloc(pathLen * sizeof(wchar_t), 8);
    void* fstrSlots = FMemory::Malloc(16, 8); // one FString
    if (!pathBuf || !fstrSlots)
    {
        if (pathBuf) FMemory::Free(pathBuf);
        if (fstrSlots) FMemory::Free(fstrSlots);
        return;
    }
    wmemcpy(static_cast<wchar_t*>(pathBuf), path, pathLen);

    uint8_t* slot = static_cast<uint8_t*>(fstrSlots);
    *reinterpret_cast<void**>(slot + 0) = pathBuf;
    *reinterpret_cast<int32_t*>(slot + 8) = pathLen;
    *reinterpret_cast<int32_t*>(slot + 12) = pathLen;

    // 3) Pack params buffer.
    int sz = fn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    uint8_t* arr = buf.data() + pInPaths->GetOffset_Internal();
    *reinterpret_cast<void**>(arr + 0) = fstrSlots;
    *reinterpret_cast<int32_t*>(arr + 8) = 1;
    *reinterpret_cast<int32_t*>(arr + 12) = 1;
    writeGoatParm<bool>(fn, buf.data(), STR("bForceRescan"), false);

    VLOG(STR("[MoriaCppMod] [Goat] ScanPathsSynchronous('{}') firing...\n"), path);
    safeProcessEvent(reg, fn, buf.data());
    VLOG(STR("[MoriaCppMod] [Goat] ScanPathsSynchronous('{}') returned\n"), path);
    // The engine's UFUNCTION cleanup will free pathBuf + fstrSlots via
    // the TArray<FString> destructor. Do not free here.
}

bool m_goatRegistryScanned{false};
bool m_goatAnchorAttempted{false};

// Force-load the v1.0.1 PorterGoat anchor BP. Per editor Claude's
// PorterGoat_v1.0.1 pak, /Game/Mods/PorterGoat/BP_PorterGoatLoader has
// a hard TSubclassOf<APawn> default pointing at BP_NpcGoat_C. Loading
// the anchor instantiates its CDO, which resolves the hard ref, which
// pulls /Game/Character/NpcGoat/BP_NpcGoat into memory transitively —
// making BP_NpcGoat_C resolvable from any zone, no settlement visit
// required.
//
// Diagnostics differentiate the failure modes:
//   - anchor null pre-load + null post-load → pak isn't mounting
//   - anchor null pre-load + non-null post-load → loader works as designed
//   - anchor non-null but BP_NpcGoat still null → import retarget is broken
void primeGoatAnchorLoad()
{
    if (m_goatAnchorAttempted) return;
    m_goatAnchorAttempted = true;

    const wchar_t* anchorPath = STR("/Game/Mods/PorterGoat/BP_PorterGoatLoader.BP_PorterGoatLoader_C");
    const wchar_t* targetPath = STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C");

    UObject* anchorPre = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, anchorPath);
    UObject* targetPre = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, targetPath);
    VLOG(STR("[MoriaCppMod] [Goat] anchor diag pre-load: anchor={:p} target={:p}\n"), (void*)anchorPre, (void*)targetPre);

    // Try to load the anchor. LoadClassAsset_Blocking is correct for a
    // TSubclassOf<>-typed default — but the anchor itself is a UObject
    // BP, so LoadAsset_Blocking is the canonical loader.
    UObject* anchor = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                              STR("/Script/Engine.Default__KismetSystemLibrary"),
                                              STR("Asset"),
                                              anchorPath);
    if (!anchor)
    {
        // Fallback: try the class-typed loader in case the anchor's
        // CDO is what gets registered as a UClass.
        anchor = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadClassAsset_Blocking"),
                                         STR("/Script/Engine.Default__KismetSystemLibrary"),
                                         STR("AssetClass"),
                                         anchorPath);
    }

    UObject* anchorPost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, anchorPath);
    UObject* targetPost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, targetPath);
    VLOG(STR("[MoriaCppMod] [Goat] anchor diag post-load: anchor={:p} target={:p} (loader returned {:p})\n"), (void*)anchorPost, (void*)targetPost, (void*)anchor);

    // Editor-Claude probe: try to load /Game/Character/NpcDwarf/DT_NPCRoles
    // (a path our pak OVERRIDES — exists in shipping content). If this
    // resolves while the brand-new /Game/Mods/PorterGoat/... loader path
    // does not, IoStore is rejecting brand-new mod-pak paths from
    // PackageStore lookups (Theory A). If both fail, the pak isn't
    // mounting at all from PorterGoat_v1.0.1_P/.
    const wchar_t* overridePath = STR("/Game/Character/NpcDwarf/DT_NPCRoles.DT_NPCRoles");
    UObject* overrideFind = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, overridePath);
    UObject* overrideLoad = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                                    STR("/Script/Engine.Default__KismetSystemLibrary"),
                                                    STR("Asset"),
                                                    overridePath);
    UObject* overridePost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, overridePath);
    VLOG(STR("[MoriaCppMod] [Goat] override-path probe DT_NPCRoles: pre={:p} loaderRet={:p} post={:p}\n"), (void*)overrideFind, (void*)overrideLoad, (void*)overridePost);
}

bool ensureGoatSpawnBindings()
{
    // [rc.121 2026-07-11] Stale-cache purge: across world loads the
    // cached pointers can go stale while isObjectAlive still passes
    // (rc.118 lesson — reused memory). safeObjectName is SEH-wrapped:
    // empty result = garbage → drop the cache and re-resolve fresh.
    // (This was the silent bell-summon failure: cached class name=''
    // → BeginDeferred SEH → bail, every ring.)
    if (m_goatBPClass && safeObjectName(m_goatBPClass).empty())
    {
        VLOG(STR("[MoriaCppMod] [Goat] [rc.121] cached goat class STALE (SEH probe) — purging all spawn bindings\n"));
        m_goatBPClass = nullptr;
        m_goatBeginSpawnFn = nullptr;
        m_goatFinishSpawnFn = nullptr;
        m_kismetGameplayStaticsCDO = nullptr;
    }
    if (m_kismetGameplayStaticsCDO && safeObjectName(m_kismetGameplayStaticsCDO).empty()) m_kismetGameplayStaticsCDO = nullptr;

    if (!m_goatBPClass)
    {
        // 0a) Prime the v1.0.1 anchor load. Pulls BP_NpcGoat into
        // memory via the loader BP's hard UClass default.
        primeGoatAnchorLoad();

        // 0b) AssetRegistry scan — historical fallback for the v1.0.0
        // soft-pointer-only setup. Cheap if already done.
        if (!m_goatRegistryScanned)
        {
            m_goatRegistryScanned = true;
            scanAssetRegistryPath(STR("/Game/Character/NpcGoat"));
            scanAssetRegistryPath(STR("/Game/Character/Creatures/Goat"));
        }

        // [rc.64 CRITICAL FIX 2026-05-21] Force-resolve BP_NpcGoat (PRIMARY,
        // GOAT_CLASS_PATHS[0]) via LoadClassAsset_Blocking BEFORE falling
        // to StaticFindObject. BP_NpcGoat is in Tobi's pak and lazy-loaded
        // — StaticFindObject doesn't see lazy-loaded pak BPs, returns null,
        // loop falls to BP_Fauna_Goat (vanilla, always in registry) which
        // is the WRONG class. Symptoms: vanilla goat mesh instead of Porter
        // Goat; no MorNPCComponent; no menu match; prey AI behavior.
        //
        // Same lazy-load pattern Desktop Claude identified for the persistor.
        // The fix mirrors Probe J's resolution: blocking load first, then
        // FindObject as fallback verification.
        m_goatBPClass = goat_loadClassAssetBlocking(GOAT_CLASS_PATHS[0]);
        if (m_goatBPClass)
        {
            VLOG(STR("[MoriaCppMod] [Goat] resolved PRIMARY class via LoadClassAsset_Blocking: {}\n"), GOAT_CLASS_PATHS[0]);
        }
        // 1) StaticFindObject for each candidate path (cheap, instant) as fallback.
        if (!m_goatBPClass)
        {
            for (auto* path : GOAT_CLASS_PATHS)
            {
                m_goatBPClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
                if (m_goatBPClass)
                {
                    VLOG(STR("[MoriaCppMod] [Goat] resolved class via StaticFindObject: {}\n"), path);
                    break;
                }
            }
        }
        // 2) Walk resident BlueprintGeneratedClass instances by name.
        // Catches the case where the class is loaded under a different
        // package layout than the static path string we expect.
        if (!m_goatBPClass)
        {
            std::vector<UObject*> classes;
            if (seh_findAllOf(STR("BlueprintGeneratedClass"), &classes))
            {
                for (auto* c : classes)
                {
                    if (!c || !isObjectAlive(c)) continue;
                    std::wstring n = c->GetName();
                    for (auto* candidate : GOAT_CLASS_NAMES)
                    {
                        if (n == std::wstring_view(candidate))
                        {
                            m_goatBPClass = reinterpret_cast<UClass*>(c);
                            VLOG(STR("[MoriaCppMod] [Goat] resolved class via FindAllOf scan: {} (path={})\n"), n.c_str(), c->GetPathName().c_str());
                            break;
                        }
                    }
                    if (m_goatBPClass) break;
                }
            }
        }
        // 3) Last resort: blocking load. Loops the candidate paths.
        if (!m_goatBPClass)
        {
            for (auto* path : GOAT_CLASS_PATHS)
            {
                m_goatBPClass = goat_loadClassAssetBlocking(path);
                if (m_goatBPClass) break;
            }
        }
    }
    if (!m_kismetGameplayStaticsCDO)
        m_kismetGameplayStaticsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
    if (!m_goatBeginSpawnFn)
        m_goatBeginSpawnFn =
                UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
    if (!m_goatFinishSpawnFn)
        m_goatFinishSpawnFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));
    return m_goatBPClass && m_kismetGameplayStaticsCDO && m_goatBeginSpawnFn && m_goatFinishSpawnFn;
}

// Write `value` at the offset of the named UProperty inside `parmsBuf`.
// No-op on missing param or invalid offset (defensive against UFUNCTION
// signature drift between builds).
template <typename T>
void writeGoatParm(UFunction* fn, void* parmsBuf, const wchar_t* name, const T& value)
{
    auto* prop = findParam(fn, name);
    if (!prop) return;
    int off = prop->GetOffset_Internal();
    if (off < 0) return;
    *reinterpret_cast<T*>(static_cast<uint8_t*>(parmsBuf) + off) = value;
}

template <typename T>
T readGoatParm(UFunction* fn, const void* parmsBuf, const wchar_t* name, T fallback)
{
    auto* prop = findParam(fn, name);
    if (!prop) return fallback;
    int off = prop->GetOffset_Internal();
    if (off < 0) return fallback;
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(parmsBuf) + off);
}

// Resolve `controller`'s component matching `fgkClassPath` and call
// SetActive(false). Used to silence the goat's AI subsystems so it
// never enters a flee state.
void deactivateGoatAIComponent(UObject* controller, const wchar_t* fgkClassPath, const wchar_t* logTag)
{
    if (!controller || !isObjectAlive(controller)) return;

    auto* compCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, fgkClassPath);
    if (!compCls)
    {
        VLOG(STR("[MoriaCppMod] [Goat] could not resolve {} class\n"), logTag);
        return;
    }
    auto* getCompFn = controller->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn) return;

    int sz = getCompFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), compCls);
    if (!safeProcessEvent(controller, getCompFn, buf.data())) return;
    UObject* comp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!comp || !isObjectAlive(comp))
    {
        VLOG(STR("[MoriaCppMod] [Goat] {} not found on controller\n"), logTag);
        return;
    }

    auto* setActiveFn = comp->GetFunctionByNameInChain(STR("SetActive"));
    if (!setActiveFn) return;
    int sz2 = setActiveFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    writeGoatParm<bool>(setActiveFn, buf2.data(), STR("bNewActive"), false);
    writeGoatParm<bool>(setActiveFn, buf2.data(), STR("bReset"), false);
    safeProcessEvent(comp, setActiveFn, buf2.data());

    VLOG(STR("[MoriaCppMod] [Goat] deactivated {}\n"), logTag);
}

// One-shot UFunction-name dump for MorPlayerController. Editor side
// needs to know what "open container inventory" UFunction the goat
// E-hook should call. Output: one line per UFunction with name +
// parameter shape.
bool m_pcSchemaDumped{false};
void dumpPlayerControllerSchema()
{
    if (m_pcSchemaDumped) return;
    m_pcSchemaDumped = true;

    auto* pcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorPlayerController"));
    if (!pcCls)
    {
        VLOG(STR("[MoriaCppMod] [PCDump] MorPlayerController class NOT resident\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [PCDump] === MorPlayerController dump start ===\n"));

    std::wstring clsName;
    try
    {
        clsName = pcCls->GetName();
    }
    catch (...)
    {
    }

    // Walk PROPERTIES first — we need to find a SettlementId / active
    // settlement reference so we can call ServerRescueNpc directly.
    // Live values are read from m_localPC if it's resident.
    UObject* liveInst = m_localPC && isObjectAlive(m_localPC) ? m_localPC : nullptr;
    int propCount = 0;
    for (auto* strct = static_cast<UStruct*>(pcCls); strct; strct = strct->GetSuperStruct())
    {
        std::wstring strctName;
        try
        {
            strctName = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            if (propCount >= 600) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            // Filter to interesting names so the log isn't a wall.
            std::wstring lpn = pn;
            for (auto& c : lpn)
                c = (wchar_t)::towlower(c);
            bool keep = lpn.find(L"settlement") != std::wstring::npos || lpn.find(L"npc") != std::wstring::npos || lpn.find(L"hall") != std::wstring::npos ||
                        lpn.find(L"home") != std::wstring::npos || lpn.find(L"manager") != std::wstring::npos || lpn.find(L"id") != std::wstring::npos ||
                        lpn.find(L"guid") != std::wstring::npos;
            if (!keep)
            {
                ++propCount;
                continue;
            }

            std::wstring valueStr;
            if (liveInst)
            {
                try
                {
                    int32_t off = prop->GetOffset_Internal();
                    uint8_t* base = reinterpret_cast<uint8_t*>(liveInst);
                    uint8_t* slot = base + off;
                    if (pcn == STR("ObjectProperty") || pcn == STR("WeakObjectProperty") || pcn == STR("ClassProperty"))
                    {
                        UObject* val = *reinterpret_cast<UObject**>(slot);
                        if (val)
                        {
                            std::wstring vName, vCls;
                            try
                            {
                                vName = val->GetName();
                            }
                            catch (...)
                            {
                            }
                            try
                            {
                                vCls = val->GetClassPrivate()->GetName();
                            }
                            catch (...)
                            {
                            }
                            valueStr = STR(" value=") + vName + STR(" (") + vCls + STR(")");
                        }
                        else
                            valueStr = STR(" value=null");
                    }
                    else if (pcn == STR("UInt32Property") || pcn == STR("IntProperty"))
                    {
                        int32_t v = *reinterpret_cast<int32_t*>(slot);
                        wchar_t buf[40];
                        swprintf(buf, 40, STR(" value=%d (0x%X)"), v, v);
                        valueStr = buf;
                    }
                    else if (pcn == STR("StructProperty"))
                    {
                        uint32_t* g = reinterpret_cast<uint32_t*>(slot);
                        wchar_t buf[80];
                        swprintf(buf, 80, STR(" maybe-guid=%08X-%08X-%08X-%08X"), g[0], g[1], g[2], g[3]);
                        valueStr = buf;
                    }
                }
                catch (...)
                {
                }
            }

            VLOG(STR("[MoriaCppMod] [PCDump] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                 clsName.c_str(),
                 strctName.c_str(),
                 pn.c_str(),
                 pcn.c_str(),
                 (unsigned)prop->GetOffset_Internal(),
                 valueStr.c_str());
            ++propCount;
        }
        if (propCount >= 600) break;
    }

    // Walk UFunctions (methods) on MorPlayerController + parents.
    int fnCount = 0;
    for (auto* fn : pcCls->ForEachFunctionInChain())
    {
        if (fnCount >= 600) break;
        std::wstring fnName;
        try
        {
            fnName = fn->GetName();
        }
        catch (...)
        {
        }
        // Filter to interesting names: those involving inventory/open/container/storage/UI.
        std::wstring lower = fnName;
        for (auto& c : lower)
            c = (wchar_t)::towlower(c);
        if (lower.find(L"inventor") != std::wstring::npos || lower.find(L"container") != std::wstring::npos || lower.find(L"storage") != std::wstring::npos ||
            lower.find(L"open") != std::wstring::npos || lower.find(L"close") != std::wstring::npos || lower.find(L"interact") != std::wstring::npos ||
            lower.find(L"rescue") != std::wstring::npos || lower.find(L"loot") != std::wstring::npos || lower.find(L"chest") != std::wstring::npos)
        {
            int parmCount = 0;
            std::wstring paramSig;
            for (auto* prop : fn->ForEachProperty())
            {
                if (parmCount >= 6) break;
                std::wstring pn, pcn;
                try
                {
                    pn = prop->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    pcn = prop->GetClass().GetName();
                }
                catch (...)
                {
                }
                if (!paramSig.empty()) paramSig += L", ";
                paramSig += pcn + L" " + pn;
                ++parmCount;
            }
            VLOG(STR("[MoriaCppMod] [PCDump] UFunc {}({})\n"), fnName.c_str(), paramSig.c_str());
        }
        ++fnCount;
    }
    VLOG(STR("[MoriaCppMod] [PCDump] === MorPlayerController dump done (walked {} UFunctions, filtered) ===\n"), fnCount);
}

// One-shot property-schema dump for MorEquipOverrideComponent and
// MorEquipComponent. Editor-side requested this so they can find which
// UPROPERTY field on the override component would hold a "default
// equipped items" reference (since the C++ source isn't available).
// Output is one line per property, format:
//   [EquipDump] ClassName.PropertyName : PropertyClassName
// Ship the log to editor Claude.
bool m_equipSchemaDumped{false};
void dumpEquipComponentSchemas()
{
    if (m_equipSchemaDumped) return;
    m_equipSchemaDumped = true;

    const wchar_t* classPaths[] = {
            STR("/Script/Moria.MorEquipOverrideComponent"),
            STR("/Script/Moria.MorEquipComponent"),
            STR("/Script/FGK.FGKEquipComponent"), // parent of MorEquipComponent
    };
    for (auto* path : classPaths)
    {
        auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
        if (!cls)
        {
            VLOG(STR("[MoriaCppMod] [EquipDump] {} = NOT RESIDENT (skipped)\n"), path);
            continue;
        }
        std::wstring clsName = cls->GetName();
        int count = 0;
        for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
        {
            std::wstring strctName;
            try
            {
                strctName = strct->GetName();
            }
            catch (...)
            {
            }
            for (auto* prop : strct->ForEachProperty())
            {
                std::wstring pn, pcn;
                try
                {
                    pn = prop->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    pcn = prop->GetClass().GetName();
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [EquipDump] {} (from {}).{} : {}\n"), clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str());
                if (++count >= 200) break;
            }
            if (count >= 200) break;
        }
        VLOG(STR("[MoriaCppMod] [EquipDump] === {} dump done ({} props) ===\n"), clsName.c_str(), count);
    }
}

// One-shot schema dump for UI_WBP_Interaction_C. Triggered the first
// time we see OnSetInteraction fire on that widget while a follower
// goat is alive. We walk the UClass for *every* property + UFunction
// (so we can find which FText holds the visible "Rescue" label and
// which UObject* holds the interaction target), then walk the live
// widget instance and print the current value of every UObject*,
// FText, FString, FName property — that reveals which field points
// to the goat right now.
bool m_interactionWidgetDumped{false};

// Preload the static-mesh version of the dwarven mountaineer pack at
// character-load time. We repurpose the goat's existing Hat
// StaticMeshComponent (which had Gandalfs_Hat by default) to carry
// this mesh instead — a visible pack appears without spawning new
// components at runtime.
UObject* ensurePackStaticMesh()
{
    if (m_packStaticMesh && isObjectAlive(m_packStaticMesh)) return m_packStaticMesh;
    const wchar_t* meshPath = STR("/Game/CharacterArt/Dwarfs/Dwarf_Backpacks/Dwarf_Pack01_Static.Dwarf_Pack01_Static");
    m_packStaticMesh = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, meshPath);
    if (!m_packStaticMesh)
    {
        m_packStaticMesh = goat_callBlockingLoader(STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                                                   STR("/Script/Engine.Default__KismetSystemLibrary"),
                                                   STR("Asset"),
                                                   meshPath);
    }
    if (m_packStaticMesh)
        VLOG(STR("[MoriaCppMod] [Goat] pack static mesh preloaded: {:p}\n"), (void*)m_packStaticMesh);
    else
        VLOG(STR("[MoriaCppMod] [Goat] pack static mesh NOT loadable yet ({})\n"), meshPath);
    return m_packStaticMesh;
}

// Preload the pack BP class at character-load time so that the equip
// call from tickFollowGoats has no blocking I/O on the game thread.
// (Earlier hang was traced to goat_loadClassAssetBlocking running on
// the game thread waiting for chunk streaming.) Once cached, the
// pointer survives world transitions for the session.
UClass* ensurePackItemClass()
{
    if (m_packItemClass) return m_packItemClass;
    const wchar_t* packPath = STR("/Game/Items/EpicPacks/BP_EpicPack_AdventurersPack_Large.BP_EpicPack_AdventurersPack_Large_C");
    m_packItemClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, packPath);
    if (!m_packItemClass)
    {
        m_packItemClass = goat_loadClassAssetBlocking(packPath);
    }
    if (m_packItemClass)
        VLOG(STR("[MoriaCppMod] [Goat] pack item class preloaded: {:p}\n"), (void*)m_packItemClass);
    else
        VLOG(STR("[MoriaCppMod] [Goat] pack item class NOT loadable yet ({})\n"), packPath);
    return m_packItemClass;
}

// Equip the visible adventurer's pack on the goat. The asset chain
// discovered via JSON inspection of BP_NpcGoat + DT_EpicPacks:
//   BP_NpcGoat.InventoryComp.StorageHandle.RowName = "AdventurersPack_Large"
//     -> DT_Storage row "AdventurersPack_Large" defines a 5x8 inventory grid
//     -> DT_EpicPacks row "AdventurersPack_Large" has CharacterClass =
//        /Game/Items/EpicPacks/BP_EpicPack_AdventurersPack_Large.BP_EpicPack_AdventurersPack_Large_C
//     -> That BP has a SkeletalMeshComponent with Dwarf_Pack01 mesh
//        attached at BackSocket.
// Calling ServerEquipDummyItem(packClass) on the goat's MorEquipComponent
// dispatches the equip pipeline server-side, which attaches the pack's
// SkeletalMesh to whichever socket the goat skeleton exposes (typically
// the spine/back).
bool equipAdventurersPack(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return false;

    // Use the cached pack class only — no blocking I/O on the game
    // thread. The class should have been preloaded at character-load
    // time via ensurePackItemClass(). If it's still null here, we
    // skip the equip rather than risk a hang.
    UClass* packCls = m_packItemClass;
    if (!packCls)
    {
        // One last chance: maybe it became resident after character
        // load. Resident-only check — never trigger blocking load.
        packCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr,
                                                            nullptr,
                                                            STR("/Game/Items/EpicPacks/BP_EpicPack_AdventurersPack_Large.BP_EpicPack_AdventurersPack_Large_C"));
        if (packCls) m_packItemClass = packCls;
    }
    if (!packCls)
    {
        VLOG(STR("[MoriaCppMod] [Goat] pack item class not yet resident; skipping equip (preload failed at character-load)\n"));
        return false;
    }

    // Find the goat's MorEquipComponent.
    auto* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorEquipComponent"));
    if (!equipCls)
    {
        VLOG(STR("[MoriaCppMod] [Goat] MorEquipComponent UClass not resident\n"));
        return false;
    }
    auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn) return false;
    int sz = getCompFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), equipCls);
    if (!safeProcessEvent(goat, getCompFn, buf.data())) return false;
    UObject* equipComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!equipComp || !isObjectAlive(equipComp))
    {
        VLOG(STR("[MoriaCppMod] [Goat] no MorEquipComponent on goat\n"));
        return false;
    }

    // Call ServerEquipDummyItem(packCls). This is a Server RPC — fires
    // server-side, replicates back. On a host or single-player session
    // it dispatches synchronously.
    auto* sedFn = equipComp->GetFunctionByNameInChain(STR("ServerEquipDummyItem"));
    if (!sedFn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ServerEquipDummyItem UFunction missing on EquipComp\n"));
        return false;
    }
    int sz2 = sedFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    writeGoatParm<UClass*>(sedFn, buf2.data(), STR("ItemToEquip"), packCls);
    safeProcessEvent(equipComp, sedFn, buf2.data());
    VLOG(STR("[MoriaCppMod] [Goat] ServerEquipDummyItem(BP_EpicPack_AdventurersPack_Large_C) fired on EquipComp={:p}\n"), (void*)equipComp);
    return true;
}

// [v7.2.0-rc.3] Resolve + cache Tobi's porter saddlebag UClass.
//
// Tobi shipped the goat content inside `SecretsOfKhazadDum_Assets_P.pak`
// (his umbrella mod). The class name + path under /Game/Mods/* is not
// fixed across pak releases. We try a list of candidate paths first;
// if none resolve, we fall back to a runtime scan of all loaded
// BlueprintGeneratedClass objects looking for "Saddle"/"SaddleBag"/
// "GoatPack"/"GoatBag" substrings, logging every hit so future
// sessions inherit the discovery via the cache. Cached for the session.
UClass* ensureSaddlebagItemClass()
{
    if (m_saddlebagItemClass && isObjectAlive(m_saddlebagItemClass)) return m_saddlebagItemClass;

    // 1) Direct path candidates. First hit wins.
    static const wchar_t* kCandidates[] = {STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                                           STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                                           STR("/Game/Mods/PorterGoat/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                                           STR("/Game/Mods/PorterGoat/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                                           STR("/Game/Mods/SecretsOfKhazadDum/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                                           STR("/Game/Mods/SecretsOfKhazadDum/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                                           nullptr};
    for (const wchar_t** p = kCandidates; *p; ++p)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, *p);
        if (!c) c = goat_loadClassAssetBlocking(*p);
        if (c && isObjectAlive(c))
        {
            m_saddlebagItemClass = c;
            VLOG(STR("[MoriaCppMod] [Goat] saddlebag item class resolved (direct): {} -> {:p}\n"), *p, (void*)c);
            return m_saddlebagItemClass;
        }
    }

    // 2) Runtime scan fallback — walk loaded BlueprintGeneratedClass
    //    objects, match by name substring. One-shot per session.
    VLOG(STR("[MoriaCppMod] [Goat] saddlebag class direct lookup failed across {} candidate paths — scanning loaded BPs\n"),
         (int)(sizeof(kCandidates) / sizeof(kCandidates[0]) - 1));
    std::vector<UObject*> bpClasses;
    if (!findAllOfSafe(STR("BlueprintGeneratedClass"), bpClasses))
    {
        VLOG(STR("[MoriaCppMod] [Goat] BlueprintGeneratedClass scan failed (findAllOf returned false)\n"));
        return nullptr;
    }
    VLOG(STR("[MoriaCppMod] [Goat] BlueprintGeneratedClass scan: {} loaded classes\n"), (int)bpClasses.size());

    auto containsCI = [](const std::wstring& s, const wchar_t* needle) -> bool {
        std::wstring lo;
        lo.reserve(s.size());
        for (wchar_t c : s)
            lo.push_back((wchar_t)towlower(c));
        std::wstring nl;
        for (size_t i = 0; needle[i]; ++i)
            nl.push_back((wchar_t)towlower(needle[i]));
        return lo.find(nl) != std::wstring::npos;
    };

    UClass* firstMatch = nullptr;
    for (UObject* c : bpClasses)
    {
        if (!c || !isObjectAlive(c)) continue;
        std::wstring n;
        try
        {
            n = c->GetName();
        }
        catch (...)
        {
            continue;
        }
        // Match anything saddlebag-ish OR with a path under /Mods/PorterGoat/
        bool matchName = containsCI(n, STR("Saddle")) || containsCI(n, STR("GoatBag")) || containsCI(n, STR("GoatPack")) || containsCI(n, STR("PorterPack")) ||
                         containsCI(n, STR("PorterBag"));
        std::wstring fullPath;
        try
        {
            fullPath = c->GetFullName();
        }
        catch (...)
        {
        }
        bool matchPath = containsCI(fullPath, STR("/PorterGoat/")) || containsCI(fullPath, STR("/Mods/SecretsOfKhazadDum/"));
        if (!matchName && !matchPath) continue;

        VLOG(STR("[MoriaCppMod] [Goat] scan hit: name='{}' fullPath='{}'\n"), n.c_str(), fullPath.c_str());

        // Prefer classes that look like item bags (Saddle/Bag/Pack in
        // name) over slot wrappers or other PorterGoat support classes.
        if (matchName && !containsCI(n, STR("Slot")) && !containsCI(n, STR("Wrapper")))
        {
            if (!firstMatch) firstMatch = static_cast<UClass*>(c);
        }
    }

    if (firstMatch && isObjectAlive(firstMatch))
    {
        m_saddlebagItemClass = firstMatch;
        std::wstring name;
        try
        {
            name = firstMatch->GetName();
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [Goat] saddlebag item class resolved (scan): '{}' -> {:p}\n"), name.c_str(), (void*)firstMatch);
        return m_saddlebagItemClass;
    }

    VLOG(STR("[MoriaCppMod] [Goat] saddlebag class scan yielded no item-shaped match — equip will be skipped\n"));
    return nullptr;
}

// [v7.2.0-rc.3] Equip Tobi's BP_PorterGoatSaddlebags_C onto the goat's
// MorEquipComponent via ServerEquipDummyItem — same RPC + parm shape
// as the visible adventurer's-pack equip path above. Returns true on
// success, false on any guard failure (defensive: every branch logs).
//
// v1.5.0 of Tobi's pak ships the goat with an empty EpicPack slot
// wrapper (BP_ContainerItem_Goat_Slot_EpicPack_C) but never fills it.
// This call fills the slot so the saddlebag click in the goat's
// interaction menu opens a real storage grid instead of doing nothing.
//
// Gated by m_autoEquipSaddleBag (INI: [GoatExperimental] AutoEquipSaddleBag).
// If ServerEquipDummyItem turns out to be the wrong RPC for a non-
// dummy bag, the call no-ops and the slot stays empty — same state
// as today, no regression. Worst case is logging noise.
bool equipPorterSaddlebag(UObject* goat)
{
    if (!m_autoEquipSaddleBag)
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] disabled via INI — skipping\n"));
        return false;
    }
    if (!goat || !isObjectAlive(goat))
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] goat null/dead — bail\n"));
        return false;
    }

    UClass* saddleCls = ensureSaddlebagItemClass();
    if (!saddleCls)
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] saddlebag class unresolved — bail\n"));
        return false;
    }

    // Resolve the goat's MorEquipComponent.
    UClass* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorEquipComponent"));
    if (!equipCls)
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] MorEquipComponent UClass not resident\n"));
        return false;
    }
    auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn)
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] GetComponentByClass missing on goat\n"));
        return false;
    }
    std::vector<uint8_t> gbuf(getCompFn->GetParmsSize(), 0);
    writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), equipCls);
    if (!safeProcessEvent(goat, getCompFn, gbuf.data()))
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] GetComponentByClass dispatch failed\n"));
        return false;
    }
    UObject* equipComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
    if (!equipComp || !isObjectAlive(equipComp))
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] goat has no MorEquipComponent\n"));
        return false;
    }

    // Fire ServerEquipDummyItem(saddleCls).
    auto* sedFn = equipComp->GetFunctionByNameInChain(STR("ServerEquipDummyItem"));
    if (!sedFn)
    {
        VLOG(STR("[MoriaCppMod] [SaddleEquip] ServerEquipDummyItem missing on goat EquipComp\n"));
        return false;
    }
    std::vector<uint8_t> buf(sedFn->GetParmsSize(), 0);
    writeGoatParm<UClass*>(sedFn, buf.data(), STR("ItemToEquip"), saddleCls);
    safeProcessEvent(equipComp, sedFn, buf.data());
    VLOG(STR("[MoriaCppMod] [SaddleEquip] ServerEquipDummyItem(BP_PorterGoatSaddlebags_C) fired on EquipComp={:p}\n"), (void*)equipComp);
    return true;
}

// [v7.2.0-rc.12a 2026-05-22] Resolve the chest class for phantom-
// chest approach. Primary target is BP_StorageChest_Construction_C
// (the settlement-buildable storage chest); fall back to other
// chest receptacles if that class isn't resident. Cached.
UClass* ensurePhantomChestClass()
{
    if (m_phantomChestClass && isObjectAlive(m_phantomChestClass)) return m_phantomChestClass;
    // [rc.12a.2] BP_StorageChest_Construction_C resolved cleanly in
    // rc.12a but its UI didn't open — likely because it requires
    // settlement-construction context (build state, materials).
    // Pivot to BP_ChestReceptacle_C first — exploration-reward
    // chests are complete by spawn, no construction state needed.
    static const wchar_t* kCandidates[] = {STR("/Game/LevelDesign/Placeables/Containers/BP_ChestReceptacle.BP_ChestReceptacle_C"),
                                           STR("/Game/LevelDesign/Placeables/Containers/BP_SmallChestReceptacle.BP_SmallChestReceptacle_C"),
                                           STR("/Game/LevelDesign/Placeables/Containers/BP_StorageChest_Construction.BP_StorageChest_Construction_C"),
                                           STR("/Game/LevelDesign/Placeables/Containers/BP_FallBackReceptacle.BP_FallBackReceptacle_C"),
                                           STR("/Game/LevelDesign/Placeables/Containers/BP_BarrelReceptacle.BP_BarrelReceptacle_C"),
                                           nullptr};
    for (const wchar_t** p = kCandidates; *p; ++p)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, *p);
        if (!c) c = goat_loadClassAssetBlocking(*p);
        if (c && isObjectAlive(c))
        {
            m_phantomChestClass = c;
            VLOG(STR("[MoriaCppMod] [PhantomChest] class resolved: {} -> {:p}\n"), *p, (void*)c);
            return c;
        }
    }
    VLOG(STR("[MoriaCppMod] [PhantomChest] no chest class resolvable across {} candidates\n"), (int)(sizeof(kCandidates) / sizeof(kCandidates[0]) - 1));
    return nullptr;
}

// [rc.88 GOAT CHEST 2026-07-07] The goat's own cargo container can't be
// instantiated mod-side (editor-only). A CHEST's inventory works fine,
// so give the goat a hidden BP_ChestReceptacle: spawn it invisible + no
// collision, keep it, and open its inventory on demand. The StorageMode
// widget binds to the chest's real container by handle, so the chest can
// stay hidden anywhere — position/visibility don't matter for opening.
UObject* m_hiddenGoatChest{nullptr};
void spawnHiddenGoatChest(UObject* goat)
{
    if (m_hiddenGoatChest && isObjectAlive(m_hiddenGoatChest)) return;
    if (!goat || !isObjectAlive(goat)) return;
    if (!ensureGoatSpawnBindings())
    {
        VLOG(STR("[MoriaCppMod] [GoatChest] spawn bindings not ready\n"));
        return;
    }
    UClass* chestCls = ensurePhantomChestClass();
    if (!chestCls)
    {
        VLOG(STR("[MoriaCppMod] [GoatChest] chest class missing\n"));
        return;
    }

    FVec3f loc{};
    if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
    {
        struct
        {
            FVec3f Ret{};
        } lp{};
        if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
    }
    FTransformRaw xform{};
    xform.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    // 3000 units under the world: the chest is only a POINTER target
    // (StorageObject/InteractableRef) — if it sits near the goat its
    // interaction prompt floats in the world ("Open Chest" in mid-air,
    // 2026-07-16 report) and it persists into the save at that spot.
    xform.Translation = {loc.X, loc.Y, loc.Z - 3000.0f};
    xform.Scale3D = {1.0f, 1.0f, 1.0f};

    std::vector<uint8_t> buf(m_goatBeginSpawnFn->GetParmsSize(), 0);
    writeGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
    writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), chestCls);
    writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"), xform);
    writeGoatParm<uint8_t>(m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);
    if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) return;
    UObject* chest = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!chest)
    {
        VLOG(STR("[MoriaCppMod] [GoatChest] BeginDeferred returned null\n"));
        return;
    }

    std::vector<uint8_t> buf2(m_goatFinishSpawnFn->GetParmsSize(), 0);
    writeGoatParm<UObject*>(m_goatFinishSpawnFn, buf2.data(), STR("Actor"), chest);
    writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
    safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

    // Hide it and kill collision so the player never sees or bumps it.
    if (auto* hideFn = chest->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
    {
        struct
        {
            bool b{true};
        } p{};
        try
        {
            safeProcessEvent(chest, hideFn, &p);
        }
        catch (...)
        {
        }
    }
    if (auto* collFn = chest->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
    {
        struct
        {
            bool b{false};
        } p{};
        try
        {
            safeProcessEvent(chest, collFn, &p);
        }
        catch (...)
        {
        }
    }

    m_hiddenGoatChest = chest;
    VLOG(STR("[MoriaCppMod] [GoatChest] spawned + hid chest={:p} (cls={:p})\n"), (void*)chest, (void*)chestCls);
}

// Open the hidden goat chest's inventory (its container is real, unlike
// the goat's). Spawns the chest on first use if needed.
void openGoatChestStorage()
{
    if (!m_hiddenGoatChest || !isObjectAlive(m_hiddenGoatChest))
    {
        UObject* g = findOurGoatAlive();
        if (g) spawnHiddenGoatChest(g);
    }
    if (!m_hiddenGoatChest || !isObjectAlive(m_hiddenGoatChest))
    {
        showOnScreen(L"No goat chest (summon a goat first)", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    UObject* chestInv = nullptr;
    if (invCls)
    {
        if (auto* getComp = m_hiddenGoatChest->GetFunctionByNameInChain(STR("GetComponentByClass")))
        {
            std::vector<uint8_t> b(getComp->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getComp, b.data(), STR("ComponentClass"), invCls);
            if (safeProcessEvent(m_hiddenGoatChest, getComp, b.data())) chestInv = readGoatParm<UObject*>(getComp, b.data(), STR("ReturnValue"), nullptr);
        }
    }
    if (!chestInv || !isObjectAlive(chestInv))
    {
        VLOG(STR("[MoriaCppMod] [GoatChest] chest has no MorInventoryComponent\n"));
        showOnScreen(L"Goat chest has no inventory", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }
    // Chest containers ARE real — GetContainers[0] gives a valid handle.
    uint8_t bagHandle[20] = {0};
    int32_t bagId = 0;
    if (auto* gcFn = chestInv->GetFunctionByNameInChain(STR("GetContainers")))
    {
        std::vector<uint8_t> gb(gcFn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(chestInv, gcFn, gb.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(gcFn, STR("ReturnValue")))
        {
            uint8_t* arr = gb.data() + pRet->GetOffset_Internal();
            uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
            VLOG(STR("[MoriaCppMod] [GoatChest] chest GetContainers -> {}\n"), num);
            if (data && num > 0)
            {
                int32_t id0 = *reinterpret_cast<int32_t*>(data);
                if (id0 != 0)
                {
                    std::memcpy(bagHandle, data, 20);
                    bagId = id0;
                    RC::Unreal::FWeakObjectPtr ownerWP(chestInv);
                    std::memcpy(bagHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
                }
            }
        }
    }
    if (bagId == 0)
    {
        VLOG(STR("[MoriaCppMod] [GoatChest] chest container id=0 — cannot open\n"));
        showOnScreen(L"Goat chest container not ready", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }
    VLOG(STR("[MoriaCppMod] [GoatChest] opening chest storage id={}\n"), bagId);
    openStorageWidgetForHandle(m_hiddenGoatChest, chestInv, bagHandle);
}

int32_t readNpcInfoItemsNum()
{
    UObject* mgr = nullptr;
    // Path 1: MoriaUtils::GetNpcManager static UFunction
    UClass* utilsCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MoriaUtils"));
    if (utilsCls && isObjectAlive(utilsCls))
    {
        UObject* utilsCDO = nullptr;
        try
        {
            utilsCDO = utilsCls->GetClassDefaultObject();
        }
        catch (...)
        {
        }
        if (utilsCDO && isObjectAlive(utilsCDO))
        {
            auto* getMgrFn = utilsCls->GetFunctionByNameInChain(STR("GetNpcManager"));
            if (getMgrFn)
            {
                std::vector<uint8_t> buf(getMgrFn->GetParmsSize(), 0);
                auto* pWC = findParam(getMgrFn, STR("WorldContextObject"));
                if (pWC && m_localPC && isObjectAlive(m_localPC)) *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = m_localPC;
                try
                {
                    safeProcessEvent(utilsCDO, getMgrFn, buf.data());
                }
                catch (...)
                {
                }
                auto* pRet = findParam(getMgrFn, STR("ReturnValue"));
                if (pRet) mgr = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            }
        }
    }
    // Path 2: findAllOfSafe fallback
    if (!mgr || !isObjectAlive(mgr))
    {
        std::vector<UObject*> candidates;
        if (findAllOfSafe(STR("MorNPCManager"), candidates))
        {
            for (UObject* o : candidates)
            {
                if (!o || !isObjectAlive(o)) continue;
                std::wstring cn = safeClassName(o);
                if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                mgr = o;
                break;
            }
        }
    }
    if (!mgr || !isObjectAlive(mgr)) return -1;

    // NpcInfo struct at +0x03a0; Items TArray at struct+0x0108; Num at +8.
    uint8_t* itemsHdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
    if (!isReadableMemory(itemsHdr, 16)) return -1;
    return *reinterpret_cast<int32_t*>(itemsHdr + 8);
}

// Fire the rescue blueprint directly:
// MorPlayerController::ServerRescueNpc(FGuid NpcGuid, uint32 SettlementId)
// This is what the player's hold-E rescue charge eventually
// calls. Runs the full chain — settlement assign + OnNpcRescued
// delegate broadcast + downstream state flips. Fastest way to
// get the goat fully-rescued without going through the UI charge.
void forceRescueGoat(UObject* npcComp, uint32_t settlementId)
{
    if (!npcComp || !isObjectAlive(npcComp)) return;
    if (!m_localPC || !isObjectAlive(m_localPC))
    {
        VLOG(STR("[MoriaCppMod] [Goat] forceRescueGoat: no local PC\n"));
        return;
    }
    uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
    if (!guidPtr)
    {
        VLOG(STR("[MoriaCppMod] [Goat] forceRescueGoat: NpcGuid property missing\n"));
        return;
    }
    auto* rescueFn = m_localPC->GetFunctionByNameInChain(STR("ServerRescueNpc"));
    if (!rescueFn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ServerRescueNpc UFunction missing on PC\n"));
        return;
    }
    int sz = rescueFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    auto* pGuid = findParam(rescueFn, STR("NpcGuid"));
    auto* pSet = findParam(rescueFn, STR("SettlementId"));
    if (!pGuid || !pSet)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ServerRescueNpc params missing (guid={:p} settlement={:p})\n"), (void*)pGuid, (void*)pSet);
        return;
    }
    std::memcpy(buf.data() + pGuid->GetOffset_Internal(), guidPtr, 16);
    *reinterpret_cast<uint32_t*>(buf.data() + pSet->GetOffset_Internal()) = settlementId;

    uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
    VLOG(STR("[MoriaCppMod] [Goat] firing ServerRescueNpc(guid={:08X}-{:08X}-{:08X}-{:08X}, settlementId={})\n"), g[0], g[1], g[2], g[3], settlementId);
    safeProcessEvent(m_localPC, rescueFn, buf.data());
    VLOG(STR("[MoriaCppMod] [Goat] ServerRescueNpc returned\n"));
}

// [v1.1.1 PROBE 2026-05-10] Find BP_StoryManager singleton + dump
// every subscriber to its OnNpcRescued multicast delegate.
// Per desktop's BP-graph decode: each rescued NPC subscribes
// itself to this multicast in its BeginPlay (dwarf op[99-102]).
// If our hypothesis is right, dwarves appear in the subscriber
// list and our goat doesn't — that's why rescue's state-flip
// skips the goat. NUM/ (VK_DIVIDE) triggers this dump.
void dumpStoryMgrRescueSubscribers()
{
    VLOG(STR("[MoriaCppMod] [Probe] === OnNpcRescued subscriber dump ===\n"));
    UObject* worldCtx = m_localPC && isObjectAlive(m_localPC) ? m_localPC : (m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr);
    if (!worldCtx)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no world context (PC/pawn) — bail\n"));
        return;
    }

    // 1. Resolve manager class. OnNpcRescued is defined on
    // C++ MorSettlementManager (not BP_StoryManager — confirmed
    // by reflection walk on previous probe). Try the BP subclass
    // first; fall back to the C++ class.
    auto* storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
    if (!storyMgrCls)
    {
        storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
    }
    if (!storyMgrCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] settlement-manager class not resident (tried BP and C++)\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] using manager class={}\n"), storyMgrCls->GetName().c_str());

    // 2. Find FGKUtils::GetManager UFunction.
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkUtilsCDO)
    {
        VLOG(STR("[MoriaCppMod] [Probe] FGKUtils::GetManager unresolvable (fn={:p} cdo={:p})\n"), (void*)getMgrFn, (void*)fgkUtilsCDO);
        return;
    }

    // 3. Call FGKUtils::GetManager(worldCtx, BP_StoryManager_C class).
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), storyMgrCls);
    if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data()))
    {
        VLOG(STR("[MoriaCppMod] [Probe] FGKUtils::GetManager PE failed\n"));
        return;
    }
    UObject* storyMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!storyMgr || !isObjectAlive(storyMgr))
    {
        VLOG(STR("[MoriaCppMod] [Probe] FGKUtils::GetManager returned null — no story manager active\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] story manager singleton={:p} class={}\n"), (void*)storyMgr, safeClassName(storyMgr).c_str());

    // 4. Find OnNpcRescued MulticastInlineDelegateProperty on the
    // manager (walks super chain — likely defined on the C++
    // MorSettlementManager parent per desktop's brief).
    FProperty* delegateProp = nullptr;
    std::wstring delegateOwnerName;
    for (auto* strct = static_cast<UStruct*>(storyMgr->GetClassPrivate()); strct && !delegateProp; strct = strct->GetSuperStruct())
    {
        for (auto* prop : strct->ForEachProperty())
        {
            std::wstring pn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            if (pn == STR("OnNpcRescued"))
            {
                delegateProp = prop;
                try
                {
                    delegateOwnerName = strct->GetName();
                }
                catch (...)
                {
                }
                break;
            }
        }
    }
    if (!delegateProp)
    {
        VLOG(STR("[MoriaCppMod] [Probe] OnNpcRescued UPROPERTY not found on story manager\n"));
        return;
    }
    std::wstring delegatePropTypeName;
    try
    {
        delegatePropTypeName = delegateProp->GetClass().GetName();
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [Probe] OnNpcRescued found: type={} owner={} off=0x{:X}\n"),
         delegatePropTypeName.c_str(),
         delegateOwnerName.c_str(),
         (unsigned)delegateProp->GetOffset_Internal());

    // 5. Walk the FMulticastScriptDelegate's InvocationList.
    // FMulticastScriptDelegate layout (UE4.27): TArray<FScriptDelegate>.
    // FScriptDelegate = { FWeakObjectPtr Object (8 bytes); FName FunctionName (8 bytes); }
    // = 16 bytes per entry. The TArray itself is { ptr, num, max } = 16 bytes.
    uint8_t* delegateSlot = reinterpret_cast<uint8_t*>(storyMgr) + delegateProp->GetOffset_Internal();
    // The MulticastInlineDelegate has a layer of indirection — it stores
    // a FMulticastScriptDelegate which has TArray<FScriptDelegate> at offset 0.
    // For UE4.27 FMulticastScriptDelegate, the InvocationList TArray is at offset 0.
    void** listData = reinterpret_cast<void**>(delegateSlot + 0);
    int32_t* listNum = reinterpret_cast<int32_t*>(delegateSlot + 8);
    int32_t* listMax = reinterpret_cast<int32_t*>(delegateSlot + 12);
    int32_t num = *listNum;
    VLOG(STR("[MoriaCppMod] [Probe] OnNpcRescued subscribers: count={} (max={}, data={:p})\n"), num, *listMax, *listData);
    if (num <= 0 || !*listData)
    {
        VLOG(STR("[MoriaCppMod] [Probe] (no subscribers — list empty)\n"));
        return;
    }
    // Each FScriptDelegate is 16 bytes: FWeakObjectPtr (8) + FName (8).
    uint8_t* entries = reinterpret_cast<uint8_t*>(*listData);
    for (int32_t i = 0; i < num && i < 64; ++i)
    {
        uint8_t* entry = entries + i * 16;
        // FWeakObjectPtr = { int32 ObjectIndex; int32 ObjectSerialNumber; }
        int32_t* wptr = reinterpret_cast<int32_t*>(entry + 0);
        // FName = 8 bytes (NameEntryId 4 + Number 4)
        RC::Unreal::FName fname;
        std::memcpy(&fname, entry + 8, sizeof(RC::Unreal::FName));
        std::wstring fnameStr;
        try
        {
            fnameStr = fname.ToString();
        }
        catch (...)
        {
        }
        // Resolve the weak object via UE4SS's wrapper.
        RC::Unreal::FWeakObjectPtr wp;
        std::memcpy(&wp, entry + 0, sizeof(RC::Unreal::FWeakObjectPtr));
        UObject* obj = wp.Get();
        std::wstring objName, objCls;
        if (obj && isObjectAlive(obj))
        {
            try
            {
                objName = obj->GetName();
            }
            catch (...)
            {
            }
            try
            {
                objCls = obj->GetClassPrivate()->GetName();
            }
            catch (...)
            {
            }
        }
        else
        {
            objName = STR("<null/dead>");
        }
        VLOG(STR("[MoriaCppMod] [Probe]   [{}] obj={} cls={} fn={} (wptrIdx={} sn={})\n"), i, objName.c_str(), objCls.c_str(), fnameStr.c_str(), wptr[0], wptr[1]);
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end OnNpcRescued subscriber dump ===\n"));

    // [v1.2.2 EXTENSIONS 2026-05-10] Per desktop's brief asks:
    //   2) dump deeps-goat's MorNPCComponent runtime flags
    //   3) dump BP_StoryManager.Wanderers list
    dumpDeepsGoatNpcFlags();
    dumpStoryMgrWanderers(worldCtx);
    // [v1.2.3 ADDITION 2026-05-10] dump AMorNPCManager tracked list
    // — desktop wants to know if the deeps goat is in NPCManager's
    // tracked-companion array at spawn (the "auto-recruited"
    // gate that may explain the Manage prompt set).
    dumpNPCManagerTrackedList(worldCtx);
    // [v1.2.8 ADDITION 2026-05-10] verify desktop's SCS surgery —
    // is MorWandererComponent now physically attached to the goat?
    // If yes, dump its full UPROPERTY + UFunction schema so we
    // can find OnInteract / NpcGuid / etc. for the runtime
    // recruit-chain replication.
    dumpGoatWandererComponent();
    // [v7.1.0-rc.32 ADDITION 2026-05-10] post-rescue delta probe —
    // dump every Array/Bool/Int/Name property on BP_MorSettlementManager
    // so desktop can see whether the goat made it into any rescue
    // roster after path B ran (proves how far the dispatcher got).
    dumpSettlementManagerRescueState(worldCtx);
    // [v7.1.0-rc.34 ADDITION 2026-05-10] enumerate UFunctions on
    // BP_NpcGoat_C + BP_NpcGoat_AIController_C super chains —
    // looking for follow-trigger / set-master / become-porter calls
    // that we can invoke post-rescue to put the goat into porter
    // follow mode (goals 1+3 from porter-goat-feature-goals.md).
    dumpGoatAIControllerUFuncs();
    // [v7.1.0-rc.36 ADDITION 2026-05-10] component-stack comparison
    // of a live NPC dwarf vs the live goat. Per porter-goat-feature-goals.md
    // — instead of fighting the C++ AMorSettlementManager dispatch,
    // examine what COMPONENTS the dwarf has that drive save/persistence
    // and inventory, so we can attach equivalents to the goat at runtime.
    dumpDwarfVsGoatComponents();
    // [v7.1.0-rc.37 ADDITION 2026-05-11] per desktop's Model α brief
    // (data-record save mechanism): probe BP_FGKMoriaPlayerController_C
    // for any cached rescuee field that surfaces post-rescue, and
    // enumerate every UFunction on AMorSettlementManager class chain
    // so we can spot a callable "AddResident"/"WriteRecord"/
    // "RegisterRescuedNpc" style function that the C++ rescue path
    // calls. If found and callable from runtime, that's the lever
    // to write a goat-class record post-rescue.
    dumpPlayerControllerRescueState();
    dumpSettlementManagerUFunctions(worldCtx);
}

// [v1.2.8] Find the deeps goat, look up its MorWandererComponent
// (added via desktop's SCS edit), and dump every UPROPERTY and
// UFunction on it. This is the data we need to wire the runtime
// recruit chain (OnInteract bind + state setup).
void dumpGoatWandererComponent()
{
    VLOG(STR("[MoriaCppMod] [Probe] === goat MorWandererComponent dump ===\n"));
    // Find a goat (re-using broadened candidates — same as elsewhere).
    const wchar_t* goatCands[] = {
            STR("BP_NpcGoat_C"),
            STR("BP_NpcGoat_Survivor_C"),
            STR("BP_NpcGoat_Survivor_1_C"),
            STR("BP_NpcGoat_Survivor_2_C"),
            STR("BP_NpcGoat_Survivor_3_C"),
            STR("BP_NpcGoat_Wanderer_C"),
            STR("BP_NpcGoat_Wanderer_1_C"),
            STR("BP_PorterGoat_C"),
    };
    UObject* goat = nullptr;
    for (auto* cn : goatCands)
    {
        std::vector<UObject*> hit;
        if (seh_findAllOf(cn, &hit) && !hit.empty())
        {
            for (UObject* g : hit)
                if (g && isObjectAlive(g))
                {
                    goat = g;
                    break;
                }
            if (goat) break;
        }
    }
    if (!goat)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no goat in world\n"));
        return;
    }

    // Look up MorWandererComponent class (try several paths since
    // it could be in /Script/Moria or another module).
    const wchar_t* wandererClassPaths[] = {
            STR("/Script/Moria.MorWandererComponent"),
            STR("/Script/FGK.MorWandererComponent"),
    };
    UClass* wandCls = nullptr;
    for (auto* p : wandererClassPaths)
    {
        wandCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, p);
        if (wandCls) break;
    }
    if (!wandCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] MorWandererComponent class not resident — desktop's SCS surgery may not have been picked up\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] MorWandererComponent class resident: {}\n"), wandCls->GetName().c_str());

    // GetComponentByClass on the goat.
    auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn)
    {
        VLOG(STR("[MoriaCppMod] [Probe] GetComponentByClass UFunction missing on goat\n"));
        return;
    }
    int sz = getCompFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), wandCls);
    if (!safeProcessEvent(goat, getCompFn, buf.data()))
    {
        VLOG(STR("[MoriaCppMod] [Probe] GetComponentByClass PE failed\n"));
        return;
    }
    UObject* wandComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!wandComp || !isObjectAlive(wandComp))
    {
        VLOG(STR("[MoriaCppMod] [Probe] *** MorWandererComponent NOT FOUND on goat — SCS surgery didn't attach ***\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] *** MorWandererComponent FOUND on goat: ptr={:p} cls={} ***\n"), (void*)wandComp, safeClassName(wandComp).c_str());

    // [v1.2.9 BASELINE 2026-05-10] Live values of the key bools
    // desktop asked about — pre-rescue state.
    auto readWandBool = [wandComp](const wchar_t* name) -> std::wstring {
        auto* bp = resolveBoolProperty(wandComp, name);
        if (!bp) return STR("(not found)");
        return bp->GetPropertyValueInContainer(wandComp) ? STR("true") : STR("false");
    };
    VLOG(STR("[MoriaCppMod] [Probe]   LIVE bRecruitInteractionEnabled = {}\n"), readWandBool(STR("bRecruitInteractionEnabled")).c_str());
    VLOG(STR("[MoriaCppMod] [Probe]   LIVE bRecruitInteractionRegister = {}\n"), readWandBool(STR("bRecruitInteractionRegister")).c_str());
    VLOG(STR("[MoriaCppMod] [Probe]   LIVE bBuffInteractionEnabled = {}\n"), readWandBool(STR("bBuffInteractionEnabled")).c_str());
    VLOG(STR("[MoriaCppMod] [Probe]   LIVE bIsActive = {}\n"), readWandBool(STR("bIsActive")).c_str());

    // Dump all UPROPERTYs (full chain).
    int propCount = 0;
    for (auto* strct = static_cast<UStruct*>(wandComp->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
    {
        std::wstring strctName;
        try
        {
            strctName = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            if (propCount >= 200) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [Probe]   PROP (from {}).{} : {} off=0x{:X}\n"), strctName.c_str(), pn.c_str(), pcn.c_str(), (unsigned)prop->GetOffset_Internal());
            ++propCount;
        }
        if (propCount >= 200) break;
    }
    // Dump all UFunctions (full chain).
    int fnCount = 0;
    for (auto* fn : wandComp->GetClassPrivate()->ForEachFunctionInChain())
    {
        if (fnCount >= 200) break;
        std::wstring fnName;
        try
        {
            fnName = fn->GetName();
        }
        catch (...)
        {
        }
        int parmCount = 0;
        std::wstring sig;
        for (auto* p : fn->ForEachProperty())
        {
            if (parmCount >= 6) break;
            std::wstring pn, pcn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = p->GetClass().GetName();
            }
            catch (...)
            {
            }
            if (!sig.empty()) sig += STR(", ");
            sig += pcn + STR(" ") + pn;
            ++parmCount;
        }
        VLOG(STR("[MoriaCppMod] [Probe]   UFUNC {}({})\n"), fnName.c_str(), sig.c_str());
        ++fnCount;
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end MorWandererComponent dump ({} props, {} fns) ===\n"), propCount, fnCount);
}

// Dump AMorNPCManager's tracked-NPC array. If the deeps goat is
// in there at spawn (before any rescue fires), that means
// `ValidNpcClasses` whitelist is auto-registering the goat as a
// tracked-companion — the "already-recruited" path. Desktop
// would then drop the ValidNpcClasses entry in v1.2.4.
void dumpNPCManagerTrackedList(UObject* worldCtx)
{
    VLOG(STR("[MoriaCppMod] [Probe] === AMorNPCManager tracked-list dump ===\n"));
    if (!worldCtx)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no world context — bail\n"));
        return;
    }
    // Try the BP subclass first; fall back to the C++ class.
    auto* npcMgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_NPCManager.BP_NPCManager_C"));
    if (!npcMgrCls) npcMgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCManager"));
    if (!npcMgrCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] AMorNPCManager class missing\n"));
        return;
    }
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkUtilsCDO) return;
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), npcMgrCls);
    if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data())) return;
    UObject* npcMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!npcMgr || !isObjectAlive(npcMgr))
    {
        VLOG(STR("[MoriaCppMod] [Probe] AMorNPCManager singleton missing\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] NPCManager singleton={:p} cls={}\n"), (void*)npcMgr, safeClassName(npcMgr).c_str());

    // Probe candidate UPROPERTY names for the tracked-NPC list.
    const wchar_t* candidates[] = {
            STR("TrackedNPCs"),
            STR("TrackedCompanions"),
            STR("RegisteredNPCs"),
            STR("KnownNPCs"),
            STR("ManagedNPCs"),
            STR("ActiveNPCs"),
            STR("AllNPCs"),
            STR("NPCs"),
            STR("Companions"),
            STR("Survivors"),
            STR("RescuedNPCs"),
            STR("ValidNpcClasses"),
            STR("ManagedNpcs"),
            STR("LiveNpcs"),
            STR("SpawnedNpcs"),
    };
    int hitsFound = 0;
    for (auto* nm : candidates)
    {
        FProperty* prop = npcMgr->GetClassPrivate()->FindProperty(RC::Unreal::FName(nm, RC::Unreal::FNAME_Find));
        if (!prop) continue;
        std::wstring tn;
        try
        {
            tn = prop->GetClass().GetName();
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [Probe]   property '{}' EXISTS (type={} off=0x{:X})\n"), nm, tn.c_str(), (unsigned)prop->GetOffset_Internal());

        if (tn == STR("ArrayProperty"))
        {
            uint8_t* slot = reinterpret_cast<uint8_t*>(npcMgr) + prop->GetOffset_Internal();
            void** data = reinterpret_cast<void**>(slot + 0);
            int32_t* num = reinterpret_cast<int32_t*>(slot + 8);
            VLOG(STR("[MoriaCppMod] [Probe]     array entries: {}\n"), *num);

            // [v1.2.10 SAFE] Identify inner element type via the
            // FArrayProperty's Inner property. We MUST NOT call
            // FName::ToString on raw entry bytes — SEH AV is not
            // catchable by C++ try/catch (proved by v1.2.9 crash).
            auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
            FProperty* inner = nullptr;
            try
            {
                inner = arrProp->GetInner();
            }
            catch (...)
            {
            }
            std::wstring innerTn;
            if (inner)
            {
                try
                {
                    innerTn = inner->GetClass().GetName();
                }
                catch (...)
                {
                }
            }
            VLOG(STR("[MoriaCppMod] [Probe]     inner type: {}\n"), innerTn.empty() ? STR("<unknown>") : innerTn.c_str());

            if (*num > 0 && *data)
            {
                int limit = (*num < 16) ? *num : 16;
                // ONLY walk as UObject** if inner is ObjectProperty.
                // Anything else (SoftClassProperty / SoftObjectProperty /
                // StructProperty) — log raw bytes instead, never FName::ToString.
                if (innerTn == STR("ObjectProperty"))
                {
                    UObject** entries = reinterpret_cast<UObject**>(*data);
                    for (int j = 0; j < limit; ++j)
                    {
                        UObject* e = entries[j];
                        if (!e) continue;
                        if ((uintptr_t)e < 0x10000) continue;
                        if (!isObjectAlive(e)) continue;
                        std::wstring eName, eCls;
                        try
                        {
                            eName = e->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            eCls = e->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                        VLOG(STR("[MoriaCppMod] [Probe]       [{}] uobj={} ({})\n"), j, eName.c_str(), eCls.c_str());
                    }
                }
                else
                {
                    // Hex dump first 2 entries (40 bytes each) for offline analysis.
                    uint8_t* raw = reinterpret_cast<uint8_t*>(*data);
                    int dumpN = (limit < 2) ? limit : 2;
                    for (int j = 0; j < dumpN; ++j)
                    {
                        uint8_t* e = raw + j * 40;
                        VLOG(STR("[MoriaCppMod] [Probe]       [{}] hex(40)="
                                 "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X} "
                                 "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X} "
                                 "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X} "
                                 "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X} "
                                 "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}\n"),
                             j,
                             e[0],
                             e[1],
                             e[2],
                             e[3],
                             e[4],
                             e[5],
                             e[6],
                             e[7],
                             e[8],
                             e[9],
                             e[10],
                             e[11],
                             e[12],
                             e[13],
                             e[14],
                             e[15],
                             e[16],
                             e[17],
                             e[18],
                             e[19],
                             e[20],
                             e[21],
                             e[22],
                             e[23],
                             e[24],
                             e[25],
                             e[26],
                             e[27],
                             e[28],
                             e[29],
                             e[30],
                             e[31],
                             e[32],
                             e[33],
                             e[34],
                             e[35],
                             e[36],
                             e[37],
                             e[38],
                             e[39]);
                    }
                }
            }
        }
        ++hitsFound;
    }
    if (hitsFound == 0)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no tracked-list candidates found on AMorNPCManager — schema dump may be needed\n"));
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end AMorNPCManager tracked-list dump ===\n"));
}

// [v7.1.0-rc.32 POST-RESCUE DELTA 2026-05-10] Probe BP_MorSettlementManager
// for every ArrayProperty / BoolProperty / IntProperty / NameProperty
// (no FName::ToString — see chest-link crash) on the full super chain.
// For ObjectProperty arrays, iterate entries and log each one's class
// name — that's how we detect if BP_NpcGoat_C ever made it into a
// rescue-roster array after path B ran. For bool/int/name props,
// log the current value so we can compare pre- vs post-rescue.
// [v7.1.0-rc.36 DWARF-VS-GOAT COMPONENT COMPARE 2026-05-10] Find a
// live NPC dwarf in the world and the live goat. Walk each one's
// BlueprintCreatedComponents / InstanceComponents / OwnedComponents.
// Log every component's class + key save-related UPROPERTYs. For
// inventory/container-class components, dump DefaultContainers and
// any saved-state flags. Goal: identify the minimum set of
// components we need to attach (or settings to copy) to make the
// goat persist with the world save the way dwarves do.
void dumpDwarfVsGoatComponents()
{
    VLOG(STR("[MoriaCppMod] [Probe] === dwarf-vs-goat component stack compare ===\n"));

    auto walkComponents = [](UObject* actor, const wchar_t* label) {
        if (!actor || !isObjectAlive(actor))
        {
            VLOG(STR("[MoriaCppMod] [Probe] {} actor missing/dead\n"), label);
            return;
        }
        std::wstring aCls = safeClassName(actor);
        VLOG(
                STR("[MoriaCppMod] [Probe] --- {} actor: {} (class {}) ---\n"),
                label,
                [&] {
                    try
                    {
                        return actor->GetName();
                    }
                    catch (...)
                    {
                        return std::wstring();
                    }
                }()
                        .c_str(),
                aCls.c_str());

        const wchar_t* arrPropNames[] = {
                STR("BlueprintCreatedComponents"),
                STR("InstanceComponents"),
                STR("OwnedComponents"),
        };
        for (auto* arrName : arrPropNames)
        {
            UClass* cls = nullptr;
            try
            {
                cls = actor->GetClassPrivate();
            }
            catch (...)
            {
            }
            if (!cls) continue;
            FProperty* arrProp = nullptr;
            for (auto* strct = static_cast<UStruct*>(cls); strct && !arrProp; strct = strct->GetSuperStruct())
            {
                for (auto* p : strct->ForEachProperty())
                {
                    std::wstring pn;
                    try
                    {
                        pn = p->GetName();
                    }
                    catch (...)
                    {
                    }
                    if (pn == arrName)
                    {
                        arrProp = p;
                        break;
                    }
                }
            }
            if (!arrProp) continue;
            uint8_t* slot = reinterpret_cast<uint8_t*>(actor) + arrProp->GetOffset_Internal();
            UObject** data = *reinterpret_cast<UObject***>(slot + 0);
            int32_t num = *reinterpret_cast<int32_t*>(slot + 8);
            VLOG(STR("[MoriaCppMod] [Probe]   {}: count={}\n"), arrName, num);
            if (!data || num <= 0) continue;
            for (int32_t i = 0; i < num && i < 64; ++i)
            {
                UObject* c = data[i];
                if (!c || !isObjectAlive(c)) continue;
                std::wstring cName, cCls;
                try
                {
                    cName = c->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    cCls = c->GetClassPrivate()->GetName();
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [Probe]     [{}] {} : {}\n"), i, cName.c_str(), cCls.c_str());

                // Deep-dump inventory/container-related components.
                bool isInv = (cCls.find(STR("Inventory")) != std::wstring::npos) || (cCls.find(STR("Container")) != std::wstring::npos);
                if (!isInv) continue;

                VLOG(STR("[MoriaCppMod] [Probe]       --- {}: deep-dump ---\n"), cCls.c_str());
                UClass* compCls = c->GetClassPrivate();
                if (!compCls) continue;
                for (auto* strct = static_cast<UStruct*>(compCls); strct; strct = strct->GetSuperStruct())
                {
                    std::wstring scopeCls;
                    try
                    {
                        scopeCls = strct->GetName();
                    }
                    catch (...)
                    {
                    }
                    for (auto* prop : strct->ForEachProperty())
                    {
                        std::wstring pn, tn;
                        try
                        {
                            pn = prop->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            tn = prop->GetClass().GetName();
                        }
                        catch (...)
                        {
                        }
                        unsigned off = (unsigned)prop->GetOffset_Internal();
                        uint8_t* cslot = reinterpret_cast<uint8_t*>(c) + off;

                        if (tn == STR("ArrayProperty"))
                        {
                            auto* arrP = static_cast<RC::Unreal::FArrayProperty*>(prop);
                            FProperty* inner = nullptr;
                            try
                            {
                                inner = arrP->GetInner();
                            }
                            catch (...)
                            {
                            }
                            std::wstring innerTn;
                            if (inner)
                            {
                                try
                                {
                                    innerTn = inner->GetClass().GetName();
                                }
                                catch (...)
                                {
                                }
                            }
                            int32_t* arrNum = reinterpret_cast<int32_t*>(cslot + 8);
                            void** arrData = reinterpret_cast<void**>(cslot + 0);
                            VLOG(STR("[MoriaCppMod] [Probe]         ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                                 scopeCls.c_str(),
                                 pn.c_str(),
                                 off,
                                 innerTn.empty() ? STR("<unknown>") : innerTn.c_str(),
                                 *arrNum);
                            if (innerTn == STR("ObjectProperty") && *arrNum > 0 && *arrData)
                            {
                                UObject** entries = reinterpret_cast<UObject**>(*arrData);
                                int limit = (*arrNum < 16) ? *arrNum : 16;
                                for (int j = 0; j < limit; ++j)
                                {
                                    UObject* e = entries[j];
                                    if (!e || (uintptr_t)e < 0x10000 || !isObjectAlive(e)) continue;
                                    std::wstring eName, eCls;
                                    try
                                    {
                                        eName = e->GetName();
                                    }
                                    catch (...)
                                    {
                                    }
                                    try
                                    {
                                        eCls = e->GetClassPrivate()->GetName();
                                    }
                                    catch (...)
                                    {
                                    }
                                    VLOG(STR("[MoriaCppMod] [Probe]           [{}] {} ({})\n"), j, eName.c_str(), eCls.c_str());
                                }
                            }
                        }
                        else if (tn == STR("BoolProperty"))
                        {
                            uint8_t b = *cslot;
                            VLOG(STR("[MoriaCppMod] [Probe]         BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"), scopeCls.c_str(), pn.c_str(), off, b);
                        }
                        else if (tn == STR("IntProperty"))
                        {
                            int32_t v = *reinterpret_cast<int32_t*>(cslot);
                            VLOG(STR("[MoriaCppMod] [Probe]         INT  [{}].{} off=0x{:X} val={}\n"), scopeCls.c_str(), pn.c_str(), off, v);
                        }
                        else if (tn == STR("ObjectProperty"))
                        {
                            UObject* o = *reinterpret_cast<UObject**>(cslot);
                            std::wstring oCls;
                            if (o && (uintptr_t)o >= 0x10000 && isObjectAlive(o)) try
                                {
                                    oCls = o->GetClassPrivate()->GetName();
                                }
                                catch (...)
                                {
                                }
                            VLOG(STR("[MoriaCppMod] [Probe]         OBJ  [{}].{} off=0x{:X} ptr={:p} cls={}\n"),
                                 scopeCls.c_str(),
                                 pn.c_str(),
                                 off,
                                 (void*)o,
                                 oCls.empty() ? STR("<null>") : oCls.c_str());
                        }
                    }
                }
                VLOG(STR("[MoriaCppMod] [Probe]       --- end {} deep-dump ---\n"), cCls.c_str());
            }
        }
    };

    // 1. Find a live NPC dwarf. Try common class names; first alive wins.
    const wchar_t* dwarfCands[] = {
            STR("BP_NpcDwarf_Wanderer_RecruitAndSettlement_C"),
            STR("BP_NpcDwarf_Recruit_C"),
            STR("BP_NpcDwarf_Wanderer_C"),
            STR("BP_NpcDwarf_Survivor_C"),
            STR("BP_NpcDwarf_Survivor_1_C"),
            STR("BP_NpcDwarf_Survivor_2_C"),
            STR("BP_NpcDwarf_Survivor_3_C"),
            STR("BP_NpcDwarf_C"),
            STR("BP_NpcDwarf_Resident_C"),
            STR("BP_NpcDwarfBase_C"),
    };
    UObject* dwarf = nullptr;
    std::wstring dwarfClsHit;
    for (auto* cn : dwarfCands)
    {
        std::vector<UObject*> hit;
        if (seh_findAllOf(cn, &hit) && !hit.empty())
        {
            for (UObject* d : hit)
                if (d && isObjectAlive(d))
                {
                    dwarf = d;
                    dwarfClsHit = cn;
                    break;
                }
            if (dwarf) break;
        }
    }
    if (dwarf)
        walkComponents(dwarf, STR("DWARF"));
    else
        VLOG(STR("[MoriaCppMod] [Probe] no live NPC dwarf found (tried 10 class names)\n"));

    // 2. Find the live goat.
    UObject* goat = nullptr;
    {
        std::vector<UObject*> hit;
        if (seh_findAnyGoatActor(&hit) && !hit.empty())
            for (UObject* g : hit)
                if (g && isObjectAlive(g))
                {
                    goat = g;
                    break;
                }
    }
    if (goat)
        walkComponents(goat, STR("GOAT"));
    else
        VLOG(STR("[MoriaCppMod] [Probe] no live BP_NpcGoat_C found\n"));

    VLOG(STR("[MoriaCppMod] [Probe] === end dwarf-vs-goat compare ===\n"));
}

// [v7.1.0-rc.35 SUMMON 2026-05-10] Per porter-goat-feature-goals.md
// (goals 1+3): teleport the live BP_NpcGoat_C to the player. Calls
// MorCharacter::ServerTeleportTo(FVector DestLocation, FRotator DestRotation)
// (UFunction signature confirmed by rc.34 reflection enum). The goat's
// existing porter BT (Bst_NPCGoatWorkPorter via BP_NpcGoat_AIController_C)
// handles follow once the goat is co-located with the player.
// [v7.1.0-rc.39.2 BELL/SADDLEBAGS GRANT 2026-05-11] Helper: grant ONE
// instance of a porter-goat item class to the player's inventory via
// ServerDebugSetItem. Used for both the Bell and Saddlebags during
// debug testing — production acquisition is via Shire merchant
// recipe-bundle purchase + crafting (per v1.1.1 pak design).
//
// Asset paths from v1.1.1 desktop handoff:
//   Bell:       /Game/Mods/PorterGoat/Items/BP_PorterGoatBell.BP_PorterGoatBell_C
//   Saddlebags: /Game/Mods/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C
// Both inherit from MorContainerItem (same family as BP_ContainerItem_Dwarf_BodyInventory).
bool grantPorterItemToPlayer(const wchar_t* classPath, const wchar_t* logTag)
{
    VLOG(STR("[MoriaCppMod] [{}] granting {} to player\n"), logTag, classPath);

    // [v7.1.0-rc.39.3 FIX] StaticFindObject only finds already-loaded
    // objects. Modded BP classes in a pak aren't resident until
    // something references them. Force-load via the existing
    // blocking-loader helper (KismetSystemLibrary::LoadClassAsset_Blocking
    // → LoadAsset_Blocking fallback). Falls through to StaticFindObject
    // if the asset is already resident from prior session work.
    UClass* itemCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classPath);
    if (!itemCls)
    {
        VLOG(STR("[MoriaCppMod] [{}] not resident — force-loading via blocking loader\n"), logTag);
        itemCls = goat_loadClassAssetBlocking(classPath);
    }
    if (!itemCls)
    {
        VLOG(STR("[MoriaCppMod] [{}] item class FAILED to load (path: {})\n"), logTag, classPath);
        return false;
    }
    VLOG(STR("[MoriaCppMod] [{}] item class resolved={:p}\n"), logTag, (void*)itemCls);

    // [v7.1.0-rc.39.5 INVENTORY-COMP FIX 2026-05-12] ServerDebugSetItem
    // lives on UInventoryComponent (per CXXHeaderDump/FGK.hpp:7359),
    // NOT on the player controller or pawn. Get the player's
    // MorInventoryComponent and dispatch the RPC from there.
    // Parameter is named "Item" (TSubclassOf<AInventoryItem>), not "ItemClass".
    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    if (!pawn)
    {
        VLOG(STR("[MoriaCppMod] [{}] no local pawn — bail (load a world first)\n"), logTag);
        return false;
    }
    UObject* invComp = findPlayerInventoryComponent(pawn);
    if (!invComp || !isObjectAlive(invComp))
    {
        VLOG(STR("[MoriaCppMod] [{}] player MorInventoryComponent not found — bail\n"), logTag);
        return false;
    }
    UFunction* dsiFn = invComp->GetFunctionByNameInChain(STR("ServerDebugSetItem"));
    if (!dsiFn)
    {
        VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem not found on MorInventoryComponent — bail\n"), logTag);
        return false;
    }
    VLOG(
            STR("[MoriaCppMod] [{}] ServerDebugSetItem found on {} ({})\n"),
            logTag,
            [&] {
                try
                {
                    return invComp->GetName();
                }
                catch (...)
                {
                    return std::wstring(L"<?>");
                }
            }()
                    .c_str(),
            safeClassName(invComp).c_str());

    int sz = dsiFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    // Try both "Item" (CXXHeaderDump signature) and "ItemClass" (older guess).
    auto* pItem = findParam(dsiFn, STR("Item"));
    if (!pItem) pItem = findParam(dsiFn, STR("ItemClass"));
    auto* pCount = findParam(dsiFn, STR("Count"));
    if (!pItem || !pCount)
    {
        VLOG(STR("[MoriaCppMod] [{}] expected parms missing (Item={:p} Count={:p}) — dumping all parms:\n"), logTag, (void*)pItem, (void*)pCount);
        for (auto* p : dsiFn->ForEachProperty())
        {
            std::wstring pn, pcn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = p->GetClass().GetName();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [{}]   parm {} : {} off=0x{:X}\n"), logTag, pn.c_str(), pcn.c_str(), (unsigned)p->GetOffset_Internal());
        }
        return false;
    }
    *reinterpret_cast<UClass**>(buf.data() + pItem->GetOffset_Internal()) = itemCls;
    *reinterpret_cast<int32_t*>(buf.data() + pCount->GetOffset_Internal()) = 1;
    UObject* dsiCtx = invComp; // RPC dispatched on the component itself

    if (!safeProcessEvent(dsiCtx, dsiFn, buf.data()))
    {
        VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem PE returned false\n"), logTag);
        return false;
    }
    VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem dispatched\n"), logTag);
    return true;
}

void grantSaddlebagsToPlayer()
{
    if (grantPorterItemToPlayer(STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"), STR("Saddlebags")))
    {
        showOnScreen(L"Saddlebags granted!", 3.0f, 0.4f, 0.9f, 0.4f);
    }
    else
    {
        showOnScreen(L"Saddlebags grant failed — see log", 2.5f, 0.9f, 0.4f, 0.4f);
    }
}

// [rc.52 GOAT MENU 2026-05-12]
// E-press near tracked goat → custom UMG menu (Stay/Follow/Dismiss/
// Access Saddlebags). Proximity gate: 300 units + player roughly
// facing the goat. ESC closes.
//
// Widget structure: UserWidget root → VerticalBox containing:
//   TextBlock(m_goatName)
//   Button("STAY")     → onGoatStay
//   Button("FOLLOW")   → onGoatFollow
//   Button("DISMISS")  → onGoatDismiss
//   Button("ACCESS SADDLEBAGS") → onGoatAccessSaddlebags
//
// Buttons styled via WBP_FrontEndButton_C (same as pause menu).

void tryOpenGoatMenu()
{
    // 1-second dedupe so a held-E doesn't reopen immediately after close.
    ULONGLONG now = GetTickCount64();
    if (now - m_lastGoatMenuMs < 500) return;

    // Find closest tracked goat.
    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    if (!pawn) return;

    FVec3f pLoc{};
    FVec3f pFwd{1.0f, 0.0f, 0.0f};
    if (auto* getLoc = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
    {
        struct
        {
            FVec3f Ret{};
        } gp{};
        if (safeProcessEvent(pawn, getLoc, &gp)) pLoc = gp.Ret;
    }
    if (auto* fwdFn = pawn->GetFunctionByNameInChain(STR("GetActorForwardVector")))
    {
        struct
        {
            FVec3f Ret{};
        } gp{};
        if (safeProcessEvent(pawn, fwdFn, &gp)) pFwd = gp.Ret;
    }

    UObject* nearestGoat = nullptr;
    float nearestDistSq = 300.0f * 300.0f; // 300 units max
    for (auto& rec : m_followGoats)
    {
        UObject* g = rec.pawn.Get();
        if (!g || !isObjectAlive(g)) continue;
        FVec3f gLoc{};
        if (auto* getLoc = g->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
        {
            struct
            {
                FVec3f Ret{};
            } gp{};
            if (safeProcessEvent(g, getLoc, &gp)) gLoc = gp.Ret;
        }
        float dx = gLoc.X - pLoc.X;
        float dy = gLoc.Y - pLoc.Y;
        float dz = gLoc.Z - pLoc.Z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > nearestDistSq) continue;
        // Facing check: player→goat direction normalized · forward > 0.3 (~70deg cone)
        float dist = std::sqrt(distSq);
        if (dist > 1.0f)
        {
            float dotF = (dx * pFwd.X + dy * pFwd.Y + dz * pFwd.Z) / dist;
            if (dotF < 0.3f) continue;
        }
        nearestDistSq = distSq;
        nearestGoat = g;
    }
    if (!nearestGoat) return;

    m_lastGoatMenuMs = now;
    VLOG(STR("[MoriaCppMod] [GoatMenu] E near goat ptr={:p} — vanilla menu pivot rc.65, custom UMG suppressed\n"), (void*)nearestGoat);
}

// Build "{name} — {state}" using current stayMode of the first record.
std::wstring goatStatusText() const
{
    const wchar_t* state = STR("following");
    for (auto const& g : m_followGoats)
    {
        if (g.bellSpawned)
        {
            state = g.stayMode ? STR("staying") : STR("following");
            break;
        }
    }
    return m_goatName + STR(" \x2014 ") + state; // U+2014 em dash
}

// ====================================================================
// [Phase 6 / Path α] Vanilla-styled submenu
// ====================================================================
// On Details-press for our goat, we spawn a fresh UI_WBP_InteractionMenu_C
// and populate it with 7 cloned UI_WBP_Interaction_C rows. Vanilla's
// cursor subsystem doesn't navigate cloned rows (proven during Path A),
// so we own input ourselves: arrow keys / W-S poll for keyboard nav,
// mouse-hover detection for visual focus, LBUTTON release + E-press
// for activation, ESC to close.

// Button action handlers — invoked from the PE-pre click filter
// (onInteractionPressPre) when the user E-presses one of our
// injected rows in the vanilla proximity menu.

// [rc.64 ROLE HELPER 2026-06-28] Slim SetRoleFuzzy invoker — no
// Register, no SetIsInteractive, no equipment refresh. Just changes
// the role on the goat's MorNPCComponent. Used by onGoatFollow /
// onGoatStay to toggle Porter <-> Wanderer per approved plan.
bool setRoleFuzzyOnGoat(UObject* goat, const wchar_t* roleStr)
{
    if (!goat || !isObjectAlive(goat) || !roleStr) return false;
    UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
    if (!npcCompCls) return false;
    auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn) return false;
    int gsz = getCompFn->GetParmsSize();
    std::vector<uint8_t> gbuf(gsz, 0);
    writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), npcCompCls);
    if (!safeProcessEvent(goat, getCompFn, gbuf.data())) return false;
    UObject* npcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
    if (!npcComp || !isObjectAlive(npcComp)) return false;
    auto* setRoleFn = npcComp->GetFunctionByNameInChain(STR("SetRoleFuzzy"));
    if (!setRoleFn) return false;
    auto* pRole = findParam(setRoleFn, STR("RoleString"));
    if (!pRole) return false;
    int32_t strLen = static_cast<int32_t>(wcslen(roleStr)) + 1;
    void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
    if (!strBuf) return false;
    wmemcpy(static_cast<wchar_t*>(strBuf), roleStr, strLen);
    int sz2 = setRoleFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    uint8_t* fstr = buf2.data() + pRole->GetOffset_Internal();
    *reinterpret_cast<void**>(fstr + 0) = strBuf;
    *reinterpret_cast<int32_t*>(fstr + 8) = strLen;
    *reinterpret_cast<int32_t*>(fstr + 12) = strLen;
    try
    {
        safeProcessEvent(npcComp, setRoleFn, buf2.data());
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [GoatMenu] setRoleFuzzy('{}') fired on goat={:p} npcComp={:p}\n"), roleStr, (void*)goat, (void*)npcComp);
    return true;
}

// Stay is retired (see memory: goat-final-architecture). Clears the
// Talk-slot flag behind the Follow/Stay row; the Manage slot
// (Saddlebags) is untouched. NOTE: Tobi's v1.12 row ignores this
// flag — the dispatch hook's click-neutralizer is the real removal.
void removeGoatFollowStayRow(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
    if (!npcCompCls) return;
    auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
    if (!getCompFn) return;
    std::vector<uint8_t> gbuf(getCompFn->GetParmsSize(), 0);
    writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), npcCompCls);
    if (!safeProcessEvent(goat, getCompFn, gbuf.data())) return;
    UObject* npcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
    if (!npcComp || !isObjectAlive(npcComp)) return;
    if (setBoolProp(npcComp, STR("bTalkInteractionEnabled"), false))
    {
        VLOG(STR("[MoriaCppMod] [GoatMenu] Follow/Stay row REMOVED (bTalkInteractionEnabled=false on npcComp={:p})\n"), (void*)npcComp);
    }
    else
    {
        VLOG(STR("[MoriaCppMod] [GoatMenu] bTalkInteractionEnabled property not found — row not removed\n"));
    }
}

// FGK gait gates actual speed (Walking gait ≈ 60 u/s regardless of
// MaxWalkSpeed=600). EFGKGait: Walking=0, Running=1, Sprinting=2.
void setGoatGaitRunning(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    auto* fn = goat->GetFunctionByNameInChain(STR("SetGait"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [GoatGait] SetGait UFunction missing\n"));
        return;
    }
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    b[0] = 1; // EFGKGait::Running
    try
    {
        safeProcessEvent(goat, fn, b.data());
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [GoatGait] SetGait(Running) fired on goat={:p}\n"), (void*)goat);
}

// Disable the goat's FGK FSM — its NPC "brain" (settlement idle/
// wander) that otherwise overrides the MoveToActor drive. NOT a UE
// BehaviorTree (the AIController has no BrainComponent); FSM comps
// live on BOTH pawn and controller. onlyIfActive=true is the quiet
// watchdog path: IsActive() query first, touch nothing while the
// FSM stays down. History: memory goat-final-architecture.
bool stopGoatBrainLogic(UObject* goat, const wchar_t* reason, bool onlyIfActive = false)
{
    if (!goat || !isObjectAlive(goat)) return false;

    auto disableFsmOn = [&](UObject* owner, const wchar_t* ownerLabel) -> int {
        if (!owner || !isObjectAlive(owner)) return 0;
        int disabled = 0;
        // Class name containing "FSM" covers BodyFSMComp + siblings
        // regardless of which property they hang off.
        UClass* aCls = nullptr;
        try
        {
            aCls = owner->GetClassPrivate();
        }
        catch (...)
        {
        }
        if (!aCls) return 0;
        for (auto* strct = static_cast<UStruct*>(aCls); strct; strct = strct->GetSuperStruct())
        {
            for (auto* prop : strct->ForEachProperty())
            {
                auto* objProp = CastField<FObjectProperty>(prop);
                if (!objProp) continue;
                UObject** vp = owner->GetValuePtrByPropertyNameInChain<UObject*>(prop->GetName().c_str());
                if (!vp || !*vp) continue;
                UObject* comp = *vp;
                if (!isObjectAlive(comp)) continue;
                std::wstring cls = safeClassName(comp);
                if (cls.find(STR("FSM")) == std::wstring::npos) continue;
                if (onlyIfActive)
                {
                    // Quiet check: skip (no writes, no logs) unless the
                    // game re-activated this FSM since we disabled it.
                    bool active = false;
                    if (auto* isActFn = comp->GetFunctionByNameInChain(STR("IsActive")))
                    {
                        std::vector<uint8_t> ab(isActFn->GetParmsSize(), 0);
                        if (safeProcessEvent(comp, isActFn, ab.data())) active = ab[0] != 0;
                    }
                    if (!active) continue;
                    VLOG(STR("[MoriaCppMod] [GoatBrain] FSM re-activated by game — re-disabling (cls={})\n"), cls.c_str());
                }
                if (auto* deactFn = comp->GetFunctionByNameInChain(STR("Deactivate")))
                {
                    try
                    {
                        safeProcessEvent(comp, deactFn, nullptr);
                    }
                    catch (...)
                    {
                    }
                }
                if (auto* tickFn = comp->GetFunctionByNameInChain(STR("SetComponentTickEnabled")))
                {
                    std::vector<uint8_t> tb(tickFn->GetParmsSize(), 0);
                    tb[0] = 0; // bEnabled = false
                    try
                    {
                        safeProcessEvent(comp, tickFn, tb.data());
                    }
                    catch (...)
                    {
                    }
                }
                ++disabled;
                VLOG(STR("[MoriaCppMod] [GoatBrain] FSM comp DISABLED on {}: cls={} ptr={:p} (reason='{}')\n"), ownerLabel, cls.c_str(), (void*)comp, reason);
            }
        }
        return disabled;
    };

    int total = disableFsmOn(goat, STR("pawn"));
    auto* ctrlPtr = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
    UObject* ctrl = (ctrlPtr && *ctrlPtr) ? *ctrlPtr : nullptr;
    if (ctrl && isObjectAlive(ctrl)) total += disableFsmOn(ctrl, STR("controller"));

    if (total == 0 && !onlyIfActive) VLOG(STR("[MoriaCppMod] [GoatBrain] no FSM components found on goat={:p} (reason='{}')\n"), (void*)goat, reason);
    return total > 0;
}

// [NATIVE-AI v2 2026-07-18] THE follow lever, from the FGK controller API:
// AFGKAIController::ReplaceBehaviorState(TSubclassOf<UFGKBehaviorState>)
// — one PE call installs a behavior state CLASS directly, bypassing the
// root FSM's WorkTime/schedule gate that kept the Porter tree from ever
// running. Porter tree is self-driving after that (EQS fills LeashActor,
// tracking MoveTo follows, catch-up teleports).
bool goatReplaceBehaviorState(UObject* ctrl, const wchar_t* clsPath, const wchar_t* tag)
{
    if (!ctrl || !isObjectAlive(ctrl)) return false;
    UClass* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, clsPath);
    if (!cls) cls = goat_loadClassAssetBlocking(clsPath);
    if (!cls)
    {
        VLOG(STR("[MoriaCppMod] [NativeAI] behavior state class not loadable: {}\n"), clsPath);
        return false;
    }
    auto* fn = ctrl->GetFunctionByNameInChain(STR("ReplaceBehaviorState"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [NativeAI] ReplaceBehaviorState missing on ctrl\n"));
        return false;
    }
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* p = findParam(fn, STR("NewState"))) *reinterpret_cast<UClass**>(b.data() + p->GetOffset_Internal()) = cls;
    bool ok = safeProcessEvent(ctrl, fn, b.data());
    VLOG(STR("[MoriaCppMod] [NativeAI] ReplaceBehaviorState({}) -> {}\n"), tag, ok ? STR("OK") : STR("FAILED"));
    return ok;
}

void onGoatFollow()
{
    // [NATIVE-AI 2026-07-18] FOLLOW = ONE-SHOT: write LeashActor on the
    // goat's own AI controller and let the native behavior tree drive.
    // No brain-stop, no gait/movement forcing, no tick refresh.
    for (auto& rec : m_followGoats)
    {
        rec.stayMode = false;
        UObject* goat = rec.pawn.Get();
        if (!goat || !isObjectAlive(goat)) continue;
        setRoleFuzzyOnGoat(goat, STR("Porter"));
        UObject* ctrl = rec.controller.Get();
        if (!ctrl || !isObjectAlive(ctrl))
        {
            UObject** cp = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            ctrl = cp ? *cp : nullptr;
        }
        if (ctrl && isObjectAlive(ctrl) && m_localPawn && isObjectAlive(m_localPawn))
        {
            setGoatLeashActor(ctrl, m_localPawn);
            goatReplaceBehaviorState(ctrl, STR("/Game/Character/NpcGoat/Bst_NPCGoatWorkPorter.Bst_NPCGoatWorkPorter_C"), STR("Porter/follow"));
        }
    }
    VLOG(STR("[MoriaCppMod] [NativeAI] FOLLOW — one-shot LeashActor set (native BT drives)\n"));
    showOnScreen(L"Porter Goat: following", 1.5f, 0.7f, 0.9f, 0.7f);
    clearGoatInjectedRows();
}

void onGoatStay()
{
    // [NATIVE-AI 2026-07-18] STAY = ONE-SHOT: clear LeashActor; the native
    // behavior tree loses its follow target and idles. No refresh.
    for (auto& rec : m_followGoats)
    {
        rec.stayMode = true;
        UObject* goat = rec.pawn.Get();
        if (!goat || !isObjectAlive(goat)) continue;
        UObject* ctrl = rec.controller.Get();
        if (!ctrl || !isObjectAlive(ctrl))
        {
            UObject** cp = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            ctrl = cp ? *cp : nullptr;
        }
        if (ctrl && isObjectAlive(ctrl))
        {
            clearGoatLeashActor(ctrl);
            goatReplaceBehaviorState(ctrl, STR("/Game/Character/AI/Behaviors/BehaviorStates/BSt_Idle.BSt_Idle_C"), STR("Idle/stay"));
        }
    }
    VLOG(STR("[MoriaCppMod] [NativeAI] STAY — one-shot LeashActor cleared (native BT idles)\n"));
    showOnScreen(L"Porter Goat: staying here", 1.5f, 0.7f, 0.9f, 0.7f);
    clearGoatInjectedRows();
}

// [rc.70 2026-05-21] Open the goat's equipped saddlebag inventory.
//
// Architecture per Tobi's pak v1.5.x:
//   - Goat has a UMorEquipComponent and a UMorInventoryComponent
//   - Saddlebag is BP_SaddleBags_Goat (an EpicPack subclass) equipped
//     on the goat's back slot
//   - BP_ContainerItem_Goat_Slot_EpicPack wraps the access pattern
//
// Strategy:
//   1. Find the first live goat (m_followGoats)
//   2. ONE-SHOT diagnostic: dump UFunctions on goat's EquipComp and
//      InventoryComp matching open/use/show/activate keywords + dump
//      goat's component list
//   3. Best-effort attempts in priority order until one returns a
//      non-falsy result:
//      a) ServerUse on the equipped saddlebag item via player controller
//      b) ServerInteract with goat as target
//      c) Direct UMorInventoryScreen spawn (Path B)
//   4. Toast user on success/failure
bool m_goatSaddlebagDiagDumped{false};
// [rc.64 B3 HELPER 2026-06-28] Walk player's MorInventoryComponent
// Items.List and return the FItemInstance ID whose Item UClass name
// matches the given class-name substring. Returns 0 if not found.
// [ChestSweep 2026-07-16] Earlier builds spawned the hidden broker chest
// AT the goat's position; receptacles persist in the world save, so old
// spots kept an invisible chest with a floating "Open Chest" prompt.
// One-shot at character load: destroy every HIDDEN receptacle of the
// phantom classes (legit world chests are never hidden).
void sweepStrandedHiddenChests()
{
    static const wchar_t* kChestClasses[] = {
        STR("BP_ChestReceptacle_C"),
        STR("BP_SmallChestReceptacle_C"),
        STR("BP_StorageChest_Construction_C"),
        STR("BP_FallBackReceptacle_C"),
        STR("BP_BarrelReceptacle_C"),
    };
    int destroyed = 0;
    for (const wchar_t* cls : kChestClasses)
    {
        std::vector<UObject*> found;
        if (!seh_findAllOf(cls, &found)) continue;
        for (UObject* a : found)
        {
            if (!a || !isObjectAlive(a)) continue;
            if (a == m_hiddenGoatChest) continue; // current session's broker
            std::wstring nm = safeObjectName(a);
            if (nm.rfind(STR("Default__"), 0) == 0) continue;
            bool hidden = false;
            if (auto* bp = resolveBoolProperty(a, L"bHidden"))
                hidden = bp->GetPropertyValueInContainer(a);
            if (!hidden) continue;
            if (auto* dFn = a->GetFunctionByNameInChain(STR("K2_DestroyActor")))
            {
                std::vector<uint8_t> b(dFn->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(a, dFn, b.data());
                }
                catch (...)
                {
                }
                destroyed++;
                VLOG(STR("[MoriaCppMod] [ChestSweep] destroyed stranded hidden {} '{}'\n"), cls, nm.c_str());
            }
        }
    }
    if (destroyed > 0)
        VLOG(STR("[MoriaCppMod] [ChestSweep] {} stranded hidden chest(s) removed (save cleans on next save)\n"), destroyed);
}

// [BellSeed] If the bell is ALREADY selected (sitting in the MainHand
// equip container) when the character loads, no ItemEquipped event ever
// fires, so m_bellInHand stays false and a gameplay LMB does nothing
// until the player re-selects the bell. Seed the cached bell id and the
// in-hand state from a direct inventory read shortly after load.
void seedBellInHand()
{
    UObject* inv = playerInvOf();
    if (!inv || !isObjectAlive(inv)) return;
    FProperty* itemsProp = inv->GetPropertyByNameInChain(STR("Items"));
    if (!itemsProp) return;
    uint8_t* listBase = reinterpret_cast<uint8_t*>(inv) + itemsProp->GetOffset_Internal() + iiaListOff();
    if (!isReadableMemory(listBase, 16)) return;
    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
    int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
    if (!arrData || arrNum <= 0 || arrNum > 10000) return;
    const int stride = iiSize(), itemOff = iiItemOff(), idOff = iiIDOff();
    const int slotOff = iiSlotOff(), csOff = iiContainerStartOff();
    int32_t mainHandStart = -1, bellSlot = -1, bellId = 0;
    for (int i = 0; i < arrNum && i < 256; ++i)
    {
        uint8_t* e = arrData + i * stride;
        if (!isReadableMemory(e, stride)) continue;
        UClass* c = *reinterpret_cast<UClass**>(e + itemOff);
        if (!c || !isObjectAlive(c)) continue;
        std::wstring n;
        try
        {
            n = c->GetName();
        }
        catch (...)
        {
            continue;
        }
        if (n == STR("EQ_GoatBell_C"))
        {
            bellId = *reinterpret_cast<int32_t*>(e + idOff);
            bellSlot = *reinterpret_cast<int32_t*>(e + slotOff);
        }
        else if (n.find(STR("Slot_MainHand")) != std::wstring::npos)
        {
            mainHandStart = *reinterpret_cast<int32_t*>(e + csOff);
        }
    }
    if (bellId != 0 && m_cachedBellID == 0) m_cachedBellID = bellId;

    // [2026-07-16] A toolbar-SELECTED bell stays in the backpack container
    // (slot 1916, log-proven) — MainHand membership was the wrong test.
    // Ask the equip component directly: ItemIsEquipped(FItemHandle).
    bool inHand = false;
    if (bellId != 0 && m_localPawn && isObjectAlive(m_localPawn))
    {
        UObject* equipCls = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.MorEquipComponent"));
        auto* getCompFn = m_localPawn->GetFunctionByNameInChain(STR("GetComponentByClass"));
        UObject* equipComp = nullptr;
        if (equipCls && getCompFn)
        {
            std::vector<uint8_t> b(getCompFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>(getCompFn, b.data(), STR("ComponentClass"), equipCls);
            if (safeProcessEvent(m_localPawn, getCompFn, b.data()))
                equipComp = readGoatParm<UObject*>(getCompFn, b.data(), STR("ReturnValue"), nullptr);
        }
        if (equipComp && isObjectAlive(equipComp))
        {
            if (auto* iieFn = equipComp->GetFunctionByNameInChain(STR("ItemIsEquipped")))
            {
                std::vector<uint8_t> b(iieFn->GetParmsSize(), 0);
                if (auto* pItem = findParam(iieFn, STR("Item")))
                {
                    uint8_t* h = b.data() + pItem->GetOffset_Internal();
                    *reinterpret_cast<int32_t*>(h) = bellId;
                    RC::Unreal::FWeakObjectPtr wp(inv);
                    std::memcpy(h + 8, &wp, sizeof(wp));
                }
                if (safeProcessEvent(equipComp, iieFn, b.data()))
                    if (auto* pr = findParam(iieFn, STR("ReturnValue"))) inHand = *(b.data() + pr->GetOffset_Internal()) != 0;
            }
        }
    }
    if (inHand) m_bellInHand = true;
    VLOG(STR("[MoriaCppMod] [BellSeed] bellId={} bellSlot={} mainHandStart={} ItemIsEquipped={} -> inHand={}\n"),
         bellId, bellSlot, mainHandStart, inHand ? STR("YES") : STR("no"), inHand ? STR("YES") : STR("no"));
}

int32_t findPlayerItemIdByClassName(UObject* playerInv, const wchar_t* classNameSubstr)
{
    if (!playerInv || !isObjectAlive(playerInv) || !classNameSubstr) return 0;
    FProperty* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items"));
    if (!itemsProp) return 0;
    uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv) + itemsProp->GetOffset_Internal() + iiaListOff();
    if (!isReadableMemory(listBase, 16)) return 0;
    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
    int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
    if (!arrData || arrNum <= 0) return 0;
    int stride = iiSize();
    int itemOff = iiItemOff();
    int idOff = iiIDOff();
    for (int i = 0; i < arrNum && i < 256; ++i)
    {
        uint8_t* entry = arrData + i * stride;
        if (!isReadableMemory(entry, stride)) continue;
        UClass* itemCls = *reinterpret_cast<UClass**>(entry + itemOff);
        int32_t itemID = *reinterpret_cast<int32_t*>(entry + idOff);
        if (itemID == 0 || !itemCls || !isObjectAlive(itemCls)) continue;
        std::wstring cls;
        try
        {
            cls = itemCls->GetName();
        }
        catch (...)
        {
            continue;
        }
        if (cls.find(classNameSubstr) != std::wstring::npos)
        {
            VLOG(STR("[MoriaCppMod] [B3] player Items[{}] id={} class='{}' matches '{}'\n"), i, itemID, cls.c_str(), classNameSubstr);
            return itemID;
        }
    }
    return 0;
}

// [rc.64 B3 PRIMARY 2026-06-28] Open the player's equipped EpicPack
// saddlebag container. Mirrors openGoatSaddlebagInventory_Test3's
// widget-spawn-and-bind technique but targets PLAYER inv, not goat.
// Side benefits: vanilla character save serializes player.Items.List
// so the bag's contents persist automatically; no DLL persistence
// code needed; goat's broken/dwarven inventory is never displayed.
void openPlayerEpicPackContainer()
{
    // Dismiss any stale widget from a prior Test3 / repeat click
    if (m_test3SaddlebagWidget && isObjectAlive(m_test3SaddlebagWidget))
    {
        if (auto* rmFn = m_test3SaddlebagWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
        {
            try
            {
                safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr);
            }
            catch (...)
            {
            }
        }
        m_test3SaddlebagWidget = nullptr;
    }

    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    if (!pawn)
    {
        showOnScreen(L"No player pawn", 2.0f, 0.9f, 0.4f, 0.4f);
        return;
    }

    // 1. Resolve player MorInventoryComponent
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    UObject* playerInv = nullptr;
    if (invCls)
    {
        if (auto* getCompFn = pawn->GetFunctionByNameInChain(STR("GetComponentByClass")))
        {
            std::vector<uint8_t> gb(getCompFn->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getCompFn, gb.data(), STR("ComponentClass"), invCls);
            try
            {
                safeProcessEvent(pawn, getCompFn, gb.data());
            }
            catch (...)
            {
            }
            playerInv = readGoatParm<UObject*>(getCompFn, gb.data(), STR("ReturnValue"), nullptr);
        }
    }
    if (!playerInv || !isObjectAlive(playerInv))
    {
        VLOG(STR("[MoriaCppMod] [B3] player has no MorInventoryComponent\n"));
        showOnScreen(L"Player inventory missing", 2.0f, 0.9f, 0.4f, 0.4f);
        return;
    }

    // 2. Locate the EpicPack saddlebag item in player's Items
    // Priority: BP_SaddleBags_Goat_C (the actual bag) > slot wrapper
    int32_t bagID = findPlayerItemIdByClassName(playerInv, STR("BP_SaddleBags_Goat_C"));
    if (bagID == 0) bagID = findPlayerItemIdByClassName(playerInv, STR("BP_PorterGoatSaddlebags_C"));
    if (bagID == 0) bagID = findPlayerItemIdByClassName(playerInv, STR("BP_ContainerItem_Goat_Slot_EpicPack_C"));
    if (bagID == 0)
    {
        VLOG(STR("[MoriaCppMod] [B3] no saddlebag item found in player inventory\n"));
        showOnScreen(L"No saddlebag equipped - pick one up near the bell", 3.0f, 1.0f, 0.5f, 0.5f);
        return;
    }
    VLOG(STR("[MoriaCppMod] [B3] using player saddlebag handle id={} owner=playerInv={:p}\n"), bagID, (void*)playerInv);

    // 3. Resolve widget class
    const wchar_t* widgetPath = STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C");
    UClass* widgetCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widgetPath);
    if (!widgetCls) widgetCls = goat_loadClassAssetBlocking(widgetPath);
    if (!widgetCls)
    {
        showOnScreen(L"StorageMode widget class missing", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }

    // 4. Build FInventoryItemHandle (20 B) — id=bagID, owner=playerInv
    uint8_t bagHandle[20] = {0};
    *reinterpret_cast<int32_t*>(bagHandle + 0) = bagID;
    {
        RC::Unreal::FWeakObjectPtr ownerWP(playerInv);
        std::memcpy(bagHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
    }

    // 5. Build FGameplayTag for EpicPack slot (8 B FName)
    RC::Unreal::FName epicPackName(STR("Inventory.Slot.EpicPack"), RC::Unreal::FNAME_Add);
    uint8_t epicPackTag[8] = {0};
    std::memcpy(epicPackTag, &epicPackName, sizeof(RC::Unreal::FName));

    // 6. Player body inventory handle (for right pane)
    uint8_t playerBodyHandle[20] = {0};
    if (auto* getByTagFn = playerInv->GetFunctionByNameInChain(STR("GetContainerByTag")))
    {
        RC::Unreal::FName bodyTagName(STR("Inventory.BodyInventory"), RC::Unreal::FNAME_Add);
        int sz = getByTagFn->GetParmsSize();
        std::vector<uint8_t> tb(sz, 0);
        auto* pIn = findParam(getByTagFn, STR("Tag"));
        if (!pIn) pIn = findParam(getByTagFn, STR("ContainerTag"));
        auto* pRet = findParam(getByTagFn, STR("ReturnValue"));
        int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
        int rOff = pRet ? pRet->GetOffset_Internal() : 8;
        std::memcpy(tb.data() + tagOff, &bodyTagName, sizeof(RC::Unreal::FName));
        try
        {
            safeProcessEvent(playerInv, getByTagFn, tb.data());
        }
        catch (...)
        {
        }
        std::memcpy(playerBodyHandle, tb.data() + rOff, 20);
        RC::Unreal::FWeakObjectPtr ownerWP(playerInv);
        std::memcpy(playerBodyHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
    }

    // 7. Spawn the widget
    UObject* w = jw_createGameWidget(widgetCls);
    if (!w || !isObjectAlive(w))
    {
        showOnScreen(L"StorageMode widget create failed", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }

    // 8. Bind instance vars (memcpy raw bytes via reflection lookup)
    auto writeBytes = [&](const wchar_t* propName, const uint8_t* src, int size) {
        if (auto* p = w->GetValuePtrByPropertyNameInChain<uint8_t>(propName)) std::memcpy(p, src, size);
    };
    auto writeObj = [&](const wchar_t* propName, UObject* val) {
        if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(propName)) *p = val;
    };
    auto writeBool = [&](const wchar_t* propName, bool val) {
        setBoolProp(w, propName, val);
    };

    // LEFT pane = player's saddlebag (the bag)
    writeBytes(STR("storageInventoryHandle"), bagHandle, 20);
    writeBytes(STR("epicPackTag"), epicPackTag, 8);
    writeBytes(STR("InventoryContainerEpicPackTag"), epicPackTag, 8);

    // RIGHT pane = player's body inventory
    writeObj(STR("InventoryComponent"), playerInv);
    writeBytes(STR("bodyInventoryHandle"), playerBodyHandle, 20);

    // Mode flags: storage view ON, NPC mode OFF (this is the player's own bag)
    writeBool(STR("isStorageView"), true);
    writeBool(STR("isOpenedFromNPC"), false);
    writeObj(STR("AssociatedNPC"), nullptr);

    VLOG(STR("[MoriaCppMod] [B3] widget bound: bagID={} playerInv={:p} (NPC mode OFF, storage view ON)\n"), bagID, (void*)playerInv);

    // 9. Fire HandleStorageView(false) — non-NPC mode
    if (auto* hsvFn = w->GetFunctionByNameInChain(STR("HandleStorageView")))
    {
        int hsvSz = hsvFn->GetParmsSize();
        std::vector<uint8_t> hsvBuf(hsvSz, 0);
        hsvBuf[0] = 0; // NPCMode = false
        if (auto* pn = findParam(hsvFn, STR("NPCMode"))) hsvBuf[pn->GetOffset_Internal()] = 0;
        try
        {
            safeProcessEvent(w, hsvFn, hsvBuf.data());
        }
        catch (...)
        {
        }
    }

    // 10. AddToViewport + cache for ESC dismiss
    m_test3SaddlebagWidget = w; // reuse Test 3's ESC dismiss cache
    if (auto* addFn = w->GetFunctionByNameInChain(STR("AddToViewport")))
    {
        int addSz = addFn->GetParmsSize();
        std::vector<uint8_t> addBuf(addSz, 0);
        if (addSz >= 4) *reinterpret_cast<int32_t*>(addBuf.data()) = 100;
        try
        {
            safeProcessEvent(w, addFn, addBuf.data());
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [B3] Saddlebag widget shown (player EpicPack)\n"));
    }
}

// ================================================================
// [rc.119 SIDECAR PERSISTENCE 2026-07-11] The game's chest save flow
// is native-only (BagWatch proved no PE-visible registration), so WE
// own saddlebag contents persistence: snapshot the bag's items to a
// per-goat-GUID file (close/park), refill the bag on a fresh fit.
// Goat identity (GUID/NpcInfo) already persists via rc.112.
// ================================================================
// ================================================================
// [rc.123 B2 NATIVE CARRIER 2026-07-11] The saddlebag as a REAL world
// ACTOR (hidden, attached to the goat), registered with the save
// system via StoreRuntimeActor — the dropped-item/chest-actor native
// persistence model, using Tobi's own BP_SaddleBags_Goat_C. The UI
// binds to the ACTOR's inventory. Sidecar remains reserve.
// ================================================================
UObject* m_saddlebagActor{nullptr};

// Hide + no-collision + attach to the goat (snap location/rotation).
void tameSaddlebagActor(UObject* bag, UObject* goat)
{
    if (!bag || !isObjectAlive(bag)) return;
    if (auto* hideFn = bag->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
    {
        struct
        {
            bool b{true};
        } p{};
        try
        {
            safeProcessEvent(bag, hideFn, &p);
        }
        catch (...)
        {
        }
    }
    if (auto* collFn = bag->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
    {
        struct
        {
            bool b{false};
        } p{};
        try
        {
            safeProcessEvent(bag, collFn, &p);
        }
        catch (...)
        {
        }
    }
    if (goat && isObjectAlive(goat))
    {
        if (auto* att = bag->GetFunctionByNameInChain(STR("K2_AttachToActor")))
        {
            std::vector<uint8_t> ab(att->GetParmsSize(), 0);
            if (auto* pP = findParam(att, STR("ParentActor"))) *reinterpret_cast<UObject**>(ab.data() + pP->GetOffset_Internal()) = goat;
            if (auto* pL = findParam(att, STR("LocationRule"))) ab[pL->GetOffset_Internal()] = 2; // SnapToTarget
            if (auto* pR = findParam(att, STR("RotationRule"))) ab[pR->GetOffset_Internal()] = 2;
            if (auto* pS = findParam(att, STR("ScaleRule"))) ab[pS->GetOffset_Internal()] = 1; // KeepWorld
            try
            {
                safeProcessEvent(bag, att, ab.data());
            }
            catch (...)
            {
            }
        }
    }
}

// [rc.123 B2] Open the storage UI bound to the bag ACTOR's own
// inventory container. Returns false if the actor has no container
// (caller falls back to the legacy AddItem fit path).
bool openViaBagActor(UObject* goat, UObject* bagActor)
{
    if (!bagActor || !isObjectAlive(bagActor)) return false;
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    if (!invCls) return false;
    UObject* bagInv = nullptr;
    if (auto* gc = bagActor->GetFunctionByNameInChain(STR("GetComponentByClass")))
    {
        std::vector<uint8_t> b(gc->GetParmsSize(), 0);
        writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), invCls);
        if (safeProcessEvent(bagActor, gc, b.data())) bagInv = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
    }
    if (!bagInv || !isObjectAlive(bagInv))
    {
        VLOG(STR("[MoriaCppMod] [BagActor rc.123] open: bag actor has NO MorInventoryComponent\n"));
        return false;
    }
    uint8_t h[20] = {0};
    int32_t id = 0;
    if (auto* gcf = bagInv->GetFunctionByNameInChain(STR("GetContainers")))
    {
        std::vector<uint8_t> gb(gcf->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(bagInv, gcf, gb.data());
        }
        catch (...)
        {
        }
        if (auto* pr = findParam(gcf, STR("ReturnValue")))
        {
            uint8_t* arr = gb.data() + pr->GetOffset_Internal();
            uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
            if (data && num > 0)
            {
                std::memcpy(h, data, 20);
                id = *reinterpret_cast<int32_t*>(data);
                RC::Unreal::FWeakObjectPtr wp(bagInv);
                std::memcpy(h + 8, &wp, sizeof(wp));
            }
        }
    }
    if (id == 0)
    {
        VLOG(STR("[MoriaCppMod] [BagActor rc.123] open: bag actor inventory has NO containers\n"));
        return false;
    }
    VLOG(STR("[MoriaCppMod] [BagActor rc.123] opening CHEST-mode bound to BAG ACTOR container id={} (native carrier)\n"), id);
    openStorageWidgetForHandle(goat, bagInv, h);
    m_storageSuppressAtMs = GetTickCount64() + 1500; // [rc.130] suppression WINDOW (sweeps every 150ms)
    m_storageHarvestAtMs = GetTickCount64() + 1200;
    return true;
}

// ================================================================
// [rc.126 B5 NATIVE CARRIER 2026-07-11] The goat's cargo = a
// MorDroppedItem (the game's native persistent world container —
// USER-VERIFIED: a dropped pack keeps its contents across reload).
// Created via the game's OWN drop flow (self-registering), then
// hidden + attached to the goat. TRUE transfer of the crafted pack.
// ================================================================

// Find a live dropped-item actor carrying the goat saddlebag pack.
UObject* findSaddlebagCarrier()
{
    UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));
    for (const wchar_t* dropCls : {STR("BP_DropItem_C"), STR("MorDroppedItem")})
    {
        std::vector<UObject*> drops;
        findAllOfSafe(dropCls, drops);
        for (auto* d : drops)
        {
            if (!d || !isObjectAlive(d)) continue;
            std::wstring nm = safeObjectName(d);
            if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
            // DroppedItem is an FItemCount { TSubclassOf<AInventoryItem> Item; int32 Count; }
            if (auto* p = d->GetValuePtrByPropertyNameInChain<UClass*>(STR("DroppedItem")))
            {
                if (saddleCls && *p == saddleCls)
                {
                    VLOG(STR("[MoriaCppMod] [Carrier rc.126] found saddlebag carrier {:p} '{}'\n"), (void*)d, nm.c_str());
                    return d;
                }
            }
        }
    }
    return nullptr;
}

// [rc.131 B5-v2 2026-07-12] Eject the crafted pack as a WORLD DROP via
// the inventory's eject family — the equip-slot drop path (proven by
// Part A: it spawns the BP_DropItem wrapper that persists contents
// natively). Full param logging so a wrong layout is self-diagnosing.
bool ejectCraftedPack(UObject* playerInv, UClass* saddleCls)
{
    if (!playerInv || !saddleCls) return false;
    int32_t itemId = 0;
    if (auto* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items")))
    {
        uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv) + itemsProp->GetOffset_Internal() + iiaListOff();
        if (isReadableMemory(listBase, 16))
        {
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
            int stride = iiSize(), itemOff = iiItemOff(), idOff = iiIDOff();
            for (int32_t i = 0; arrData && i < arrNum && i < 2000; i++)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;
                if (*reinterpret_cast<UClass**>(entry + itemOff) == saddleCls)
                {
                    itemId = *reinterpret_cast<int32_t*>(entry + idOff);
                    break;
                }
            }
        }
    }
    if (itemId == 0)
    {
        VLOG(STR("[MoriaCppMod] [B5v2 rc.131] crafted pack ID not found in player inv\n"));
        return false;
    }
    uint8_t handle[20] = {0};
    *reinterpret_cast<int32_t*>(handle) = itemId;
    RC::Unreal::FWeakObjectPtr wp(playerInv);
    std::memcpy(handle + 8, &wp, sizeof(wp));

    for (const wchar_t* fnName : {STR("ServerEjectActorDirection"), STR("EjectActorDirection"), STR("ServerEjectActor")})
    {
        auto* ef = playerInv->GetFunctionByNameInChain(fnName);
        if (!ef) continue;
        std::vector<uint8_t> b(ef->GetParmsSize(), 0);
        FProperty* pItem = nullptr;
        for (auto* p : ef->ForEachProperty())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
            }
            std::wstring pt;
            try
            {
                pt = p->GetClass().GetName();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} param '{}' type={} off={} size={}\n"), fnName, pn.c_str(), pt.c_str(), p->GetOffset_Internal(), p->GetSize());
            if (!pItem && p->GetSize() == 20 && pt == STR("StructProperty")) pItem = p;
        }
        if (!pItem)
        {
            VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} has no 20B handle param — skip\n"), fnName);
            continue;
        }
        std::memcpy(b.data() + pItem->GetOffset_Internal(), handle, 20);
        try
        {
            safeProcessEvent(playerInv, ef, b.data());
        }
        catch (...)
        {
            continue;
        }
        VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} fired for crafted pack id={}\n"), fnName, itemId);
        return true;
    }
    VLOG(STR("[MoriaCppMod] [B5v2 rc.131] no eject fn available\n"));
    return false;
}

// ================================================================
// [rc.133 B7 2026-07-12] "ONE TRUE PACK": the crafted saddlebag is a
// single item instance whose SUB-CONTENTS travel with it (user-proven:
// pack in player inventory keeps contents through save/reload —
// NATIVE). During play it rides the GOAT; at save-time it is silently
// moved into the PLAYER inventory (character save persists it), then
// moved back. Real ServerMoveItem transfers — never copies.
// ================================================================
UObject* playerInvOf(UObject** outPawn = nullptr)
{
    UObject* pawn = (m_localPawn && isObjectAlive(m_localPawn)) ? m_localPawn : getPawn();
    if (outPawn) *outPawn = pawn;
    return pawn ? findPlayerInventoryComponent(pawn) : nullptr;
}
UObject* goatInvOf(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return nullptr;
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    if (!invCls) return nullptr;
    if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
    {
        std::vector<uint8_t> b(gf->GetParmsSize(), 0);
        if (auto* pCls = findParam(gf, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
        try
        {
            safeProcessEvent(goat, gf, b.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(gf, STR("ReturnValue")))
        {
            uint8_t* arr = b.data() + pRet->GetOffset_Internal();
            UObject** data = *reinterpret_cast<UObject***>(arr);
            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
            for (int32_t i = 0; data && i < num && i < 16; i++)
            {
                UObject* c = data[i];
                if (!c || !isObjectAlive(c)) continue;
                std::wstring nm;
                try
                {
                    nm = c->GetName();
                }
                catch (...)
                {
                }
                if (nm == STR("Inventory Comp")) return c; // SCS cargo comp
            }
            for (int32_t i = 0; data && i < num && i < 16; i++)
                if (data[i] && isObjectAlive(data[i])) return data[i];
        }
    }
    return nullptr;
}
// find the pack ENTRY id in an inventory (0 = not present)
int32_t findPackId(UObject* inv, UClass* saddleCls)
{
    if (!inv || !saddleCls) return 0;
    auto* itemsProp = inv->GetPropertyByNameInChain(STR("Items"));
    if (!itemsProp) return 0;
    uint8_t* listBase = reinterpret_cast<uint8_t*>(inv) + itemsProp->GetOffset_Internal() + iiaListOff();
    if (!isReadableMemory(listBase, 16)) return 0;
    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
    int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
    int stride = iiSize(), itemOff = iiItemOff(), idOff = iiIDOff();
    // [rc.134] prefer the pack WITH a live container (contents) —
    // containerStartSlot>0 marks it. Falls back to any pack entry.
    int32_t anyId = 0;
    for (int32_t i = 0; arrData && i < arrNum && i < 2000; i++)
    {
        uint8_t* entry = arrData + i * stride;
        if (!isReadableMemory(entry, stride)) continue;
        if (*reinterpret_cast<UClass**>(entry + itemOff) != saddleCls) continue;
        int32_t id = *reinterpret_cast<int32_t*>(entry + idOff);
        int32_t cs = *reinterpret_cast<int32_t*>(entry + iiContainerStartOff());
        if (cs > 0) return id; // the loaded/containered pack wins
        if (!anyId) anyId = id;
    }
    return anyId;
}
// MOVE the pack (with contents) from one inventory to another via
// ServerMoveItem. Destination = target inventory's first/default
// container handle (or zero-id auto-place). Returns true if the pack
// ended up in `toInv` (verified by re-scan).
bool movePackBetween(UObject* fromInv, UObject* toInv, const wchar_t* tag)
{
    UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));
    if (!fromInv || !toInv || !saddleCls) return false;
    int32_t packId = findPackId(fromInv, saddleCls);
    if (packId == 0)
    {
        VLOG(STR("[MoriaCppMod] [B7 rc.133] {}: pack not in source inv\n"), tag);
        return false;
    }
    uint8_t itemH[20] = {0};
    *reinterpret_cast<int32_t*>(itemH) = packId;
    RC::Unreal::FWeakObjectPtr wpFrom(fromInv);
    std::memcpy(itemH + 8, &wpFrom, sizeof(wpFrom));

    // destination handle: target inv's first container (id) or zero auto
    uint8_t destH[20] = {0};
    RC::Unreal::FWeakObjectPtr wpTo(toInv);
    std::memcpy(destH + 8, &wpTo, sizeof(wpTo));
    if (auto* gcf = toInv->GetFunctionByNameInChain(STR("GetContainers")))
    {
        std::vector<uint8_t> gb(gcf->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(toInv, gcf, gb.data());
        }
        catch (...)
        {
        }
        if (auto* pr = findParam(gcf, STR("ReturnValue")))
        {
            uint8_t* arr = gb.data() + pr->GetOffset_Internal();
            uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
            if (data && num > 0) *reinterpret_cast<int32_t*>(destH) = *reinterpret_cast<int32_t*>(data);
        }
    }
    for (const wchar_t* fnName : {STR("ServerMoveItem"), STR("MoveItem")})
    {
        auto* mf = fromInv->GetFunctionByNameInChain(fnName);
        if (!mf) continue;
        FProperty* pItem = nullptr;
        FProperty* pDest = nullptr;
        FProperty* pAdd = nullptr;
        for (auto* p : mf->ForEachProperty())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
            }
            if (pn == STR("Item")) pItem = p;
            if (pn == STR("Destination")) pDest = p;
            if (pn == STR("AddType")) pAdd = p;
        }
        if (!pItem || !pDest)
        {
            VLOG(STR("[MoriaCppMod] [B7 rc.133] {}: Item/Destination params missing — skip\n"), fnName);
            continue;
        }
        // [rc.134] AddType enum decides placement behavior — zero didn't
        // create a container on a bare inventory. Try each value until
        // the pack actually ARRIVES (rescan-verified).
        for (uint8_t addType = 0; addType <= 3; addType++)
        {
            std::vector<uint8_t> b(mf->GetParmsSize(), 0);
            std::memcpy(b.data() + pItem->GetOffset_Internal(), itemH, 20);
            std::memcpy(b.data() + pDest->GetOffset_Internal(), destH, 20);
            if (pAdd) b[pAdd->GetOffset_Internal()] = addType;
            try
            {
                safeProcessEvent(fromInv, mf, b.data());
            }
            catch (...)
            {
                break;
            }
            bool arrived = findPackId(toInv, saddleCls) != 0;
            bool left = findPackId(fromInv, saddleCls) == 0;
            VLOG(STR("[MoriaCppMod] [B7 rc.134] {} via {} AddType={}: arrived={} leftSource={}\n"), tag, fnName, (int)addType, arrived, left);
            if (arrived) return true;
            if (!pAdd) break; // no enum to vary
        }
    }
    return false;
}
// [rc.135] Create Tobi's DESIGNED slot on the goat: the 1×1
// Goat.Slot.EpicPack container (AllowedItems=Item.GoatPack) that
// DefaultContainers was meant to instantiate. rc.92 tried AddItem of
// the slot-container item with Method=0 only — sweep the enum.
bool ensureGoatSlotContainer(UObject* goatInv)
{
    if (!goatInv) return false;
    auto contCountL = [&]() -> int32_t {
        auto* gc = goatInv->GetFunctionByNameInChain(STR("GetContainers"));
        if (!gc) return -1;
        std::vector<uint8_t> gb(gc->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(goatInv, gc, gb.data());
        }
        catch (...)
        {
        }
        auto* pr = findParam(gc, STR("ReturnValue"));
        if (!pr) return -1;
        return *reinterpret_cast<int32_t*>(gb.data() + pr->GetOffset_Internal() + 8);
    };
    if (contCountL() > 0) return true;
    const wchar_t* kSlotPath = STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C");
    UClass* slotCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kSlotPath);
    if (!slotCls) slotCls = goat_loadClassAssetBlocking(kSlotPath);
    if (!slotCls)
    {
        VLOG(STR("[MoriaCppMod] [B7 rc.135] slot-container class missing\n"));
        return false;
    }
    auto* af = goatInv->GetFunctionByNameInChain(STR("AddItem"));
    if (!af) return false;
    auto* pItem = findParam(af, STR("Item"));
    if (!pItem) pItem = findParam(af, STR("Class"));
    auto* pCount = findParam(af, STR("Count"));
    auto* pMeth = findParam(af, STR("Method"));
    for (uint8_t m = 0; m <= 3; m++)
    {
        std::vector<uint8_t> ab(af->GetParmsSize(), 0);
        if (pItem) *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = slotCls;
        if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
        if (pMeth) ab[pMeth->GetOffset_Internal()] = m;
        try
        {
            safeProcessEvent(goatInv, af, ab.data());
        }
        catch (...)
        {
            break;
        }
        int32_t c = contCountL();
        VLOG(STR("[MoriaCppMod] [B7 rc.135] AddItem(SLOT container) Method={} -> containers={}\n"), (int)m, c);
        if (c > 0) return true;
        if (!pMeth) break;
    }
    return contCountL() > 0;
}

ULONGLONG m_sbWidgetOpenMs{0}; // [rc.130] widget-open time (Esc/Tab grace)
// Sidecar (file-based contents backup) fully REMOVED 2026-07-12 per
// user - persistence is native (pack in player inventory + NPC
// registry). History: memory goat-final-architecture / tobi log.

// [rc.109 2026-07-11] Cache for the one-shot delayed container re-drive
// (protects the saddlebag grid against later NPC-path rebuilds).
uint8_t m_sbHandleCache[20]{};
UObject* m_sbGoatInvCache{nullptr};
UObject* m_sbPlayerInvCache{nullptr};
ULONGLONG m_contReSetupAtMs{0};

// [rc.109] Drive the screen's Storage_Container child down the chest
// path: write its members (storageHandle + components + screen ref),
// then call its own 'Set Up Storage Container' (which ClearChildren's
// the pane and builds the grid widget from the handle).
// [ChestTrace] Phase-0 diagnostic for the chest-flow saddlebag rework
// (plan: duplicate the native CHEST open instead of the takeover). While
// the player opens a REAL chest, log the StorageMode screen's show-
// sequence events and dump the screen/container state after each, so the
// mod can mirror the exact chest bind. Verbose-gated; capped per session.
void traceChestOpenEvent(UObject* screen, const wchar_t* evt)
{
    if (!s_verbose) return;
    static int s_traceLines = 0;
    if (s_traceLines >= 60) return;
    if (!screen || !isObjectAlive(screen)) return;
    s_traceLines++;

    VLOG(STR("[ChestTrace] ===== {} on screen={:p} =====\n"), evt, (void*)screen);

    // Is this the UI manager's cached instance?
    do
    {
        auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Moria.MorUIManager:BPGetManager"));
        auto* mgrCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorUIManager"));
        if (!getMgrFn || !mgrCDO || !m_localPC || !isObjectAlive(m_localPC)) break;
        std::vector<uint8_t> gb(getMgrFn->GetParmsSize(), 0);
        writeGoatParm<UObject*>(getMgrFn, gb.data(), STR("WorldContextObject"), m_localPC);
        if (!safeProcessEvent(mgrCDO, getMgrFn, gb.data())) break;
        UObject* mgr = readGoatParm<UObject*>(getMgrFn, gb.data(), STR("ReturnValue"), nullptr);
        if (!mgr || !isObjectAlive(mgr)) break;
        auto* getScreenFn = mgr->GetFunctionByNameInChain(STR("GetScreen"));
        if (!getScreenFn) { VLOG(STR("[ChestTrace] mgr={:p} but GetScreen fn missing\n"), (void*)mgr); break; }
        std::vector<uint8_t> sb(getScreenFn->GetParmsSize(), 0);
        writeGoatParm<UClass*>(getScreenFn, sb.data(), STR("ScreenClass"), screen->GetClassPrivate());
        if (!safeProcessEvent(mgr, getScreenFn, sb.data())) break;
        UObject* mgrScreen = readGoatParm<UObject*>(getScreenFn, sb.data(), STR("ReturnValue"), nullptr);
        VLOG(STR("[ChestTrace]   uiMgr={:p} GetScreen(StorageMode)={:p} sameAsEvent={}\n"),
             (void*)mgr, (void*)mgrScreen, mgrScreen == screen ? STR("YES") : STR("NO"));
    } while (false);

    // Screen-level members.
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<bool>(STR("isOpenedFromNPC")))
        VLOG(STR("[ChestTrace]   screen.isOpenedFromNPC={}\n"), *p);
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<bool>(STR("isStorageView")))
        VLOG(STR("[ChestTrace]   screen.isStorageView={}\n"), *p);
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC")))
        VLOG(STR("[ChestTrace]   screen.AssociatedNPC={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageObject")))
        VLOG(STR("[ChestTrace]   screen.StorageObject={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));

    // Container-level members.
    UObject* cont = jw_findChildInTree(screen, STR("WBP_UI_Inventory_Storage_Container"));
    if (!cont || !isObjectAlive(cont))
    {
        VLOG(STR("[ChestTrace]   Storage_Container child NOT FOUND\n"));
        return;
    }
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<uint8_t>(STR("storageHandle")))
    {
        wchar_t hex[80]; int off = 0;
        for (int i = 0; i < 20 && off < 76; i++) off += swprintf(hex + off, 80 - off, L"%02X ", p[i]);
        int32_t id = *reinterpret_cast<int32_t*>(p);
        UObject* owner = nullptr;
        // FWeakObjectPtr at +8 — resolve via the same layout the mod writes.
        VLOG(STR("[ChestTrace]   cont.storageHandle id={} bytes=[{}]\n"), id, hex);
        (void)owner;
    }
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<uint8_t>(STR("InteractableRef")))
    {
        UObject* obj = *reinterpret_cast<UObject**>(p);
        VLOG(STR("[ChestTrace]   cont.InteractableRef obj={:p} ({})\n"), (void*)obj,
             (obj && isObjectAlive(obj)) ? safeClassName(obj).c_str() : STR("null/dead"));
    }
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("storageInventoryComponent")))
        VLOG(STR("[ChestTrace]   cont.storageInventoryComponent={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent")))
        VLOG(STR("[ChestTrace]   cont.InventoryComponent={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageScreenRef")))
        VLOG(STR("[ChestTrace]   cont.StorageScreenRef={:p}\n"), (void*)*p);
}

void driveSaddlebagStorageContainer(UObject* screen, UObject* goatInv, UObject* playerInv, const uint8_t bagHandle[20], const wchar_t* tag,
                                    UObject* interactableOverride = nullptr)
{
    UObject* cont = jw_findChildInTree(screen, STR("WBP_UI_Inventory_Storage_Container"));
    if (!cont || !isObjectAlive(cont))
    {
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] Storage_Container child NOT FOUND\n"), tag);
        return;
    }
    // Never write stale pointers into a live engine object's
    // properties (goatInv/playerInv come from caches that can
    // outlive their objects on re-drive paths).
    if (goatInv && !isObjectAlive(goatInv))
    {
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] goatInv cache STALE — drive skipped\n"), tag);
        return;
    }
    if (playerInv && !isObjectAlive(playerInv))
    {
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] playerInv cache STALE — drive skipped\n"), tag);
        return;
    }
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<uint8_t>(STR("storageHandle"))) std::memcpy(p, bagHandle, 20);
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("storageInventoryComponent"))) *p = goatInv;
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent"))) *p = playerInv;
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageScreenRef"))) *p = screen;
    // [rc.110 2026-07-11] NULL InteractableRef BEFORE setup. Decoded
    // 'Is NPC Type Container?' = InteractableRef valid AND its actor
    // has a MorNPCComponent — the goat qualifies, so CreateStorageWidget
    // built WBP_UI_Inventory_NPC_C (the dwarf 4×3) instead of falling
    // through to the generic WBP_UI_Storage_Dynamic grid. Chests never
    // hit this (no NPC component). FInterfaceProperty = {UObject*,void*}.
    if (auto* p = cont->GetValuePtrByPropertyNameInChain<uint8_t>(STR("InteractableRef")))
    {
        UObject* prev = *reinterpret_cast<UObject**>(p);
        std::memset(p, 0, 16);
        // [ChestFlow] When a chest-like actor is supplied, write it as the
        // interactable so any native rebuild classifies the pane as a CHEST
        // (generic DT-driven grid) instead of re-deriving the goat → NPC →
        // hardcoded dwarf 4x3. Interface half left null; the BP validity
        // check keys off the object.
        if (interactableOverride && isObjectAlive(interactableOverride))
        {
            *reinterpret_cast<UObject**>(p) = interactableOverride;
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] InteractableRef was {:p} -> chest {:p}\n"), tag, (void*)prev, (void*)interactableOverride);
        }
        else
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.110] InteractableRef was {:p} -> NULLED (kills NPC-type classification)\n"), (void*)prev);
    }
    else
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.110] InteractableRef property NOT FOUND\n"));
    if (auto* f = cont->GetFunctionByNameInChain(STR("Set Up Storage Container")))
    {
        int psz = (int)f->GetParmsSize();
        if (psz < 1) psz = 1;
        std::vector<uint8_t> b((size_t)psz, 0);
        try
        {
            safeProcessEvent(cont, f, b.data());
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] Storage_Container driven: handle+comps written, 'Set Up Storage Container' called (cont={:p})\n"), tag, (void*)cont);
    }
    else
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] 'Set Up Storage Container' fn NOT FOUND\n"), tag);
}

// [ChestFlow 2026-07-15] Open the Saddlebags exactly the way a CHEST
// opens (trace-verified): use the UI MANAGER's cached StorageMode screen
// and show it natively so the manager owns input mode — native drag,
// native shift/ctrl-right-click stack splitting, native close; no 10 Hz
// input re-assert, no 4 Hz pane sweep. The container is driven once
// AFTER the show sequence (the native RebindThisPack that runs during
// Show writes an empty bind — driving after wins), with the hidden goat
// chest as InteractableRef so any native rebuild classifies the pane as
// a chest (generic DT-driven grid), never the goat → NPC → dwarf 4x3.
FWeakObjectPtr m_sbChestFlowScreen;
FWeakObjectPtr m_sbChestFlowCont; // Storage_Container child (for hook-side handle compare)
FWeakObjectPtr m_sbChestFlowMgr;  // MorUIManager (shows/re-shows via ShowScreenInstance)

// [Forensics 2026-07-16] Comparative screen dump: capture the full
// layered render state (per-widget Visibility, RenderOpacity,
// RenderTransform, switcher index; screen tab/storage state) once while
// a REAL chest is open (renders) and once while our open is active
// (doesn't). The offline diff of the two dumps pinpoints the hidden
// layer no single-lever probe has reached.
ULONGLONG m_forensicsDumpAtMs{0};
const wchar_t* m_forensicsLabel{STR("")};

void dumpScreenForensics(UObject* scr, const wchar_t* label)
{
    if (!scr || !isObjectAlive(scr)) return;
    VLOG(STR("[Forensics] ===== {} screen={:p} =====\n"), label, (void*)scr);
    // Screen-level state.
    if (auto* p = scr->GetValuePtrByPropertyNameInChain<UObject*>(STR("ActiveTab")))
        VLOG(STR("[Forensics] ActiveTab={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));
    if (auto* p = scr->GetValuePtrByPropertyNameInChain<uint8_t>(STR("ActiveTabName")))
    {
        RC::Unreal::FName* fn = reinterpret_cast<RC::Unreal::FName*>(p);
        std::wstring n;
        try
        {
            n = fn->ToString();
        }
        catch (...)
        {
        }
        VLOG(STR("[Forensics] ActiveTabName='{}'\n"), n.c_str());
    }
    if (auto* p = scr->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageObject")))
        VLOG(STR("[Forensics] StorageObject={:p} ({})\n"), (void*)*p, *p ? safeClassName(*p).c_str() : STR("null"));
    for (const wchar_t* bn : {STR("isStorageView"), STR("isOpenedFromNPC")})
        if (auto* bp = resolveBoolProperty(scr, bn))
            VLOG(STR("[Forensics] {}={}\n"), bn, bp->GetPropertyValueInContainer(scr));
    // Native unreflected tail (0x3C8 StorageObject .. 0x3D7): the
    // opened-with-storage / from-interact flags live here per layout.
    {
        uint8_t* base = reinterpret_cast<uint8_t*>(scr);
        if (isReadableMemory(base + 0x3C8, 0x10))
        {
            wchar_t hex[64];
            int off = 0;
            for (int i = 0; i < 0x10 && off < 60; i++) off += swprintf(hex + off, 64 - off, L"%02X ", base[0x3C8 + i]);
            VLOG(STR("[Forensics] tail 0x3C8-0x3D7: {}\n"), hex);
        }
    }
    probeStorageGetters(scr, label);

    int lines = 0;
    std::function<void(UObject*, int)> walk = [&](UObject* w, int depth) {
        if (!w || !isObjectAlive(w) || depth > 12 || lines > 220) return;
        lines++;
        std::wstring nm, cls = safeClassName(w);
        try
        {
            nm = w->GetName();
        }
        catch (...)
        {
        }
        uint8_t vis = 255;
        if (auto* vp = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"))) vis = *vp;
        float op = -1.0f;
        if (auto* opp = w->GetValuePtrByPropertyNameInChain<float>(STR("RenderOpacity"))) op = *opp;
        // FWidgetTransform RenderTransform: Translation(2f) Scale(2f) Shear(2f) Angle(f)
        float tx = 0, ty = 0, sx = 1, sy = 1, ang = 0;
        bool hasXf = false;
        if (auto* xf = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("RenderTransform")))
        {
            float* f = reinterpret_cast<float*>(xf);
            tx = f[0];
            ty = f[1];
            sx = f[2];
            sy = f[3];
            ang = f[6];
            hasXf = true;
        }
        int32_t swIdx = -999;
        if (cls == STR("WidgetSwitcher"))
            if (auto* ip = w->GetValuePtrByPropertyNameInChain<int32_t>(STR("ActiveWidgetIndex"))) swIdx = *ip;
        std::wstring pad(static_cast<size_t>(depth) * 2, L' ');
        if (hasXf && (tx != 0 || ty != 0 || sx != 1 || sy != 1 || ang != 0))
            VLOG(STR("[Forensics] {}{} '{}' vis={} op={:.2f} XF=({:.0f},{:.0f} s{:.2f},{:.2f} a{:.0f}){}\n"),
                 pad.c_str(), cls.c_str(), nm.c_str(), vis, op, tx, ty, sx, sy, ang,
                 swIdx != -999 ? (STR(" swIdx=") + std::to_wstring(swIdx)).c_str() : STR(""));
        else
            VLOG(STR("[Forensics] {}{} '{}' vis={} op={:.2f}{}\n"),
                 pad.c_str(), cls.c_str(), nm.c_str(), vis, op,
                 swIdx != -999 ? (STR(" swIdx=") + std::to_wstring(swIdx)).c_str() : STR(""));
        // Recurse: panel Slots + single Content + WidgetTree root.
        if (auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots")))
            for (int i = 0; i < slots->Num() && i < 40; ++i)
            {
                UObject* slot = (*slots)[i];
                if (!slot) continue;
                if (auto* cp = slot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content")))
                    if (*cp) walk(*cp, depth + 1);
            }
        if (auto* cp = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content")))
            if (*cp && *cp != w) walk(*cp, depth + 1);
        if (auto* wt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree")))
            if (*wt)
                if (auto* rp = (*wt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget")))
                    if (*rp) walk(*rp, depth + 1);
    };
    walk(scr, 0);
    VLOG(STR("[Forensics] ===== END {} ({} widgets) =====\n"), label, lines);
}

void tickForensicsDump()
{
    if (m_forensicsDumpAtMs == 0 || GetTickCount64() < m_forensicsDumpAtMs) return;
    m_forensicsDumpAtMs = 0;
    // Dump whichever StorageMode instance is live.
    std::vector<UObject*> all;
    seh_findAllOf(STR("WBP_UI_Inventory_Screen_StorageMode_C"), &all);
    for (auto* w : all)
        if (w && isObjectAlive(w) && safeObjectName(w).rfind(STR("Default__"), 0) != 0)
            dumpScreenForensics(w, m_forensicsLabel);
}

// [v2 2026-07-16] Play the screen's inherited reveal animations. The
// deep dive found the render bug: the BP's OnAfterShow plays
// StorageIntro/StorageOpen — UMG animations that drive the CHILD
// widgets' transforms/opacity from the authored-hidden design pose into
// view. Every flag we checked (root visibility/opacity/IsShowing) was
// green because the hidden pose lives in the children's animated
// properties. Without the intro the screen is "shown" but parked.
void chestFlowPlayRevealAnims(UObject* scr)
{
    auto* paFn = scr->GetFunctionByNameInChain(STR("PlayAnimationForward"));
    if (!paFn)
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] PlayAnimationForward fn missing\n"));
        return;
    }
    for (const wchar_t* animName : {STR("StorageIntro"), STR("StorageOpen")})
    {
        UObject* anim = nullptr;
        if (auto* p = scr->GetValuePtrByPropertyNameInChain<UObject*>(animName)) anim = *p;
        if (!anim || !isObjectAlive(anim))
        {
            VLOG(STR("[MoriaCppMod] [ChestFlow] anim '{}' not found on screen\n"), animName);
            continue;
        }
        std::vector<uint8_t> b(paFn->GetParmsSize(), 0);
        if (auto* p = findParam(paFn, STR("InAnimation"))) *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = anim;
        if (auto* p = findParam(paFn, STR("PlaybackSpeed"))) *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 1.0f;
        // bRestoreState stays false: keep the end pose.
        bool ok = false;
        try
        {
            ok = safeProcessEvent(scr, paFn, b.data());
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [ChestFlow] PlayAnimationForward('{}') -> {}\n"), animName, ok ? STR("OK") : STR("FAILED"));
    }
}

// [v2 2026-07-16] Present via the CONFIG-ROW pipeline when possible —
// the native interact path shows screens through an FFGKUIScreenConfig
// row (ZOrderOffset, HudBehavior, bTakesInputControl, tab). Direct
// ShowScreenInstance has no config. Config table: mgr->Config(+0x38)
// ->ScreensConfigTable; row = the one whose ScreenClass soft path names
// StorageMode. Falls back to ShowScreenInstance, then screen->Show().
bool chestFlowShowViaConfigRow(UObject* mgr, UObject* screen)
{
    // Resolve the screen-configs DataTable via the manager's UFGKUIConfig.
    UObject* cfg = nullptr;
    if (auto* p = mgr->GetValuePtrByPropertyNameInChain<UObject*>(STR("Config"))) cfg = *p;
    if (!cfg || !isObjectAlive(cfg))
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] mgr Config unreadable/null (cfg={:p}) - config-row show unavailable\n"), (void*)cfg);
        return false;
    }
    UObject* table = nullptr;
    // TSoftObjectPtr<UDataTable>: resolve via the property's weak ptr Get?
    // Read as FSoftObjectPath is complex; try the already-loaded object via
    // StaticFindObject on the path string is overkill — instead many FGK
    // configs also keep the table resident; read the soft ptr's weak part.
    if (auto* p = cfg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("ScreensConfigTable")))
    {
        // TSoftObjectPtr layout: FSoftObjectPath(FName + FString) then
        // TWeakObjectPtr at +24... but the reliable route is the weak ptr
        // only if the table is loaded. Fall back to name-based find below.
        RC::Unreal::FWeakObjectPtr* wp = reinterpret_cast<RC::Unreal::FWeakObjectPtr*>(p + 24 + 4);
        (void)wp; // layout uncertain — use findAllOf below instead
    }
    // Robust: the configs table is a UMorUIScreenConfigsTable instance.
    {
        std::vector<UObject*> tabs;
        if (seh_findAllOf(STR("MorUIScreenConfigsTable"), &tabs))
            for (auto* t : tabs)
                if (t && isObjectAlive(t) && safeObjectName(t).rfind(STR("Default__"), 0) != 0)
                {
                    table = t;
                    break;
                }
    }
    if (!table)
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] screen-configs table not found\n"));
        return false;
    }

    // Find the row whose ScreenClass path mentions StorageMode.
    DataTableUtil cfgDT;
    if (!cfgDT.bindFromObject(table, L"ScreenConfigs")) return false;
    int clsOff = cfgDT.resolvePropertyOffset(L"ScreenClass");
    if (clsOff < 0) return false;
    RC::Unreal::FName rowName{};
    bool haveRow = false;
    for (const auto& rn : cfgDT.getRowNamesRaw())
    {
        std::wstring rnStr;
        try
        {
            rnStr = rn.ToString();
        }
        catch (...)
        {
            continue;
        }
        uint8_t* rowData = cfgDT.findRowData(rnStr.c_str());
        if (!rowData) continue;
        // TSoftClassPtr: FSoftObjectPath at +0 = FName AssetPathName at +0.
        RC::Unreal::FName* apn = reinterpret_cast<RC::Unreal::FName*>(rowData + clsOff);
        std::wstring path;
        try
        {
            path = apn->ToString();
        }
        catch (...)
        {
            continue;
        }
        if (path.find(STR("StorageMode")) != std::wstring::npos)
        {
            rowName = rn;
            haveRow = true;
            VLOG(STR("[MoriaCppMod] [ChestFlow] config row '{}' -> {}\n"), rnStr.c_str(), path.c_str());
            break;
        }
    }
    if (!haveRow)
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] no StorageMode row in screen configs\n"));
        return false;
    }

    // ShowScreenWithHandle(FMorUIScreenConfigRowHandle{DataTable*, RowName})
    auto* sswhFn = mgr->GetFunctionByNameInChain(STR("ShowScreenWithHandle"));
    if (!sswhFn) return false;
    std::vector<uint8_t> b(sswhFn->GetParmsSize(), 0);
    if (auto* p = findParam(sswhFn, STR("ScreenHandle")))
    {
        uint8_t* h = b.data() + p->GetOffset_Internal();
        *reinterpret_cast<UObject**>(h) = table;
        std::memcpy(h + 8, &rowName, sizeof(RC::Unreal::FName));
    }
    bool ok = false;
    try
    {
        ok = safeProcessEvent(mgr, sswhFn, b.data());
    }
    catch (...)
    {
    }
    UObject* shown = ok ? readGoatParm<UObject*>(sswhFn, b.data(), STR("ReturnValue"), nullptr) : nullptr;
    VLOG(STR("[MoriaCppMod] [ChestFlow] ShowScreenWithHandle -> {} shown={:p} (expected {:p})\n"),
         ok ? STR("OK") : STR("FAILED"), (void*)shown, (void*)screen);
    return ok && shown != nullptr;
}

// Show the screen through the MANAGER pipeline. PE-ing the screen's own
// Show() fires the events and flips IsShowing, but the screen never
// renders (2026-07-16: every widget-level flag green, nothing on
// screen) — the manager's ShowScreenInstance owns the stack push and
// the actual presentation.
// [v5 2026-07-16] The getter probe pinned the real gate: with the chest
// pre-bound, WasOpenedWithStorage=1 but IsActivatedFromInteract=0 on
// every pass — and THAT snapshot is what the screen's tick recomputes
// isStorageView from. It records HOW the screen was activated, and the
// one native entry that sets it is the controller's interact-activation
// path: AMorPlayerController::ActivateHud(HudClass, bFromInteract).
// BlueprintCallable — show the screen the way a chest interact does.
bool chestFlowActivateHud(UObject* screen)
{
    if (!m_localPC || !isObjectAlive(m_localPC) || !screen) return false;
    auto* fn = cachedFnInChain(m_localPC, STR("ActivateHud"));
    if (!fn) return false;
    UClass* cls = nullptr;
    try
    {
        cls = static_cast<UClass*>(screen->GetClassPrivate());
    }
    catch (...)
    {
        return false;
    }
    if (!cls) return false;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* p = findParam(fn, STR("HudClass"))) *reinterpret_cast<UClass**>(b.data() + p->GetOffset_Internal()) = cls;
    if (auto* p = findParam(fn, STR("bFromInteract"))) *(b.data() + p->GetOffset_Internal()) = 1;
    bool ok = safeProcessEvent(m_localPC, fn, b.data());
    VLOG(STR("[MoriaCppMod] [ChestFlow] ActivateHud(StorageMode, bFromInteract=true) -> {}\n"), ok ? STR("OK") : STR("FAILED"));
    return ok;
}

bool chestFlowShowViaManager(UObject* screen)
{
    // v5: the interact-activation entry first — the ONLY path that sets
    // the IsActivatedFromInteract state the storage view depends on.
    if (chestFlowActivateHud(screen)) return true;

    // v2: prefer the CONFIG-ROW pipeline (native presentation params).
    UObject* mgrCfg = m_sbChestFlowMgr.Get();
    if (mgrCfg && isObjectAlive(mgrCfg) && chestFlowShowViaConfigRow(mgrCfg, screen)) return true;

    UObject* mgr = m_sbChestFlowMgr.Get();
    if (mgr && isObjectAlive(mgr))
    {
        if (auto* ssiFn = mgr->GetFunctionByNameInChain(STR("ShowScreenInstance")))
        {
            std::vector<uint8_t> b(ssiFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>(ssiFn, b.data(), STR("ScreenInstance"), screen);
            if (safeProcessEvent(mgr, ssiFn, b.data()))
            {
                VLOG(STR("[MoriaCppMod] [ChestFlow] ShowScreenInstance via manager {:p}\n"), (void*)mgr);
                return true;
            }
        }
        VLOG(STR("[MoriaCppMod] [ChestFlow] ShowScreenInstance unavailable — falling back to screen Show()\n"));
    }
    if (auto* showFn = screen->GetFunctionByNameInChain(STR("Show")))
    {
        std::vector<uint8_t> b(showFn->GetParmsSize(), 0);
        return safeProcessEvent(screen, showFn, b.data());
    }
    return false;
}
// Deferred-drive timer: the native show sequence fires on the tick(s)
// AFTER Show(), and its RebindThisPack re-derives the bind from the
// goat interaction (trace 2026-07-16: handle→goat id=2, InteractableRef→
// MorNPCComponent, isStorageView→false), wiping an immediate drive. The
// hook re-arms this timer after RebindThisPack/OnAfterShow; the tick
// then drives our pack bind once the native pass is done.
ULONGLONG m_chestFlowDriveAtMs{0};
ULONGLONG m_chestFlowLastDriveMs{0}; // echo suppression: our own drive fires a RebindThisPack echo
int m_chestFlowDriveCount{0};        // per-open cap (loop guard)
bool m_chestFlowAnimsPlayed{false};  // reveal anims played for the current show

// Screen-level "storage view" dressing. The container bind alone leaves
// the LEFT pane invisible: the screen only shows the storage overlay
// after its view-switch runs (HandleStorageView) — a real chest gets
// this from the native flow via StorageObject; our open must do it
// explicitly (2026-07-16: bind verified correct in trace, pane still
// blank until this ran). Idempotent.
// PE-calls the screen's pure native getters — the direct readout of the
// internal state the per-tick view recompute uses. THE decisive probe:
// native chest shows true/true; whatever ours reports is what we must fix.
void probeStorageGetters(UObject* scr, const wchar_t* tag)
{
    auto callBoolGetter = [&](const wchar_t* fnName, int& out) {
        out = -1;
        if (auto* fn = scr->GetFunctionByNameInChain(fnName))
        {
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            if (safeProcessEvent(scr, fn, b.data()))
                if (auto* pr = findParam(fn, STR("ReturnValue"))) out = (*(b.data() + pr->GetOffset_Internal()) != 0) ? 1 : 0;
        }
    };
    int openedWithStorage = -1, fromInteract = -1;
    callBoolGetter(STR("WasOpenedWithStorage"), openedWithStorage);
    callBoolGetter(STR("IsActivatedFromInteract"), fromInteract);
    VLOG(STR("[MoriaCppMod] [ChestFlow] getters[{}]: WasOpenedWithStorage={} IsActivatedFromInteract={}\n"),
         tag, openedWithStorage, fromInteract);
}

void applyChestFlowScreenDressing(UObject* scr)
{
    if (auto* p = scr->GetValuePtrByPropertyNameInChain<uint8_t>(STR("storageInventoryHandle")))
        std::memcpy(p, m_sbHandleCache, 20);

    if (auto* hsvFn = scr->GetFunctionByNameInChain(STR("HandleStorageView")))
    {
        std::vector<uint8_t> b(hsvFn->GetParmsSize(), 0);
        for (auto* p : hsvFn->ForEachProperty())
        {
            if (!p) continue;
            std::wstring pn, pt;
            try
            {
                pn = p->GetName();
                pt = p->GetClass().GetName();
            }
            catch (...)
            {
                continue;
            }
            if (pt == STR("BoolProperty"))
            {
                bool val = false;
                if (pn.find(STR("NPC")) != std::wstring::npos || pn.find(STR("Npc")) != std::wstring::npos)
                    val = false;
                else if (pn.find(STR("torage")) != std::wstring::npos || pn.find(STR("View")) != std::wstring::npos ||
                         pn.find(STR("Show")) != std::wstring::npos || pn.find(STR("Enable")) != std::wstring::npos)
                    val = true;
                b[p->GetOffset_Internal()] = val ? 1 : 0;
            }
        }
        try
        {
            safeProcessEvent(scr, hsvFn, b.data());
        }
        catch (...)
        {
        }
    }

    // Overlay/background visibility (what the 4 Hz sweep used to force).
    auto setVis = [&](const wchar_t* childName, uint8_t v) {
        UObject* c = jw_findChildInTree(scr, childName);
        if (!c || !isObjectAlive(c)) return;
        if (auto* f = cachedFnInChain(c, STR("SetVisibility")))
        {
            std::vector<uint8_t> vb(f->GetParmsSize(), 0);
            vb[0] = v;
            try
            {
                safeProcessEvent(c, f, vb.data());
            }
            catch (...)
            {
            }
        }
    };
    setVis(STR("storageOverlay"), 4);      // SelfHitTestInvisible
    setVis(STR("Background"), 1);          // Collapsed
    setVis(STR("BackgroundWStorage"), 0);  // Visible
    setVis(STR("NPCTitleCluster"), 1);
    setVis(STR("SkillPanel"), 1);
    setVis(STR("NPCDetailsBox"), 1);
}

// Runs from the main tick: (re)binds the pack onto the chest-flow screen.
void tickChestFlowDeferredDrive()
{
    if (m_chestFlowDriveAtMs == 0 || GetTickCount64() < m_chestFlowDriveAtMs) return;
    m_chestFlowDriveAtMs = 0;
    if (m_chestFlowDriveCount >= 8) return; // cap per open
    UObject* scr = m_sbChestFlowScreen.Get();
    if (!scr || !isObjectAlive(scr)) return;
    if (!m_sbPlayerInvCache || !isObjectAlive(m_sbPlayerInvCache)) return;

    // [2026-07-16] The native goat handler shows the screen on the row
    // click, but the SAME E-press then reaches the open screen as
    // OnInteractInput and closes it (~120ms later) — by the time our
    // deferred drive ran, the screen was hidden (inViewport but
    // !IsShowing → "saddlebag UI did not display"). If it got closed,
    // re-show it first; the drive lands on the next pass.
    bool showing = false;
    if (auto* showingFn = scr->GetFunctionByNameInChain(STR("IsShowing")))
    {
        std::vector<uint8_t> b(showingFn->GetParmsSize(), 0);
        if (safeProcessEvent(scr, showingFn, b.data()))
            if (auto* pr = findParam(showingFn, STR("ReturnValue"))) showing = *(b.data() + pr->GetOffset_Internal()) != 0;
    }
    if (!showing)
    {
        chestFlowShowViaManager(scr);
        m_chestFlowAnimsPlayed = false; // reveal anims must re-run after a re-show
        m_chestFlowDriveCount++;
        m_chestFlowDriveAtMs = GetTickCount64() + 120; // drive after the show pipeline settles
        VLOG(STR("[MoriaCppMod] [ChestFlow] screen was hidden — re-shown via manager, drive re-armed (round {})\n"), m_chestFlowDriveCount);
        return;
    }

    m_chestFlowDriveCount++;
    m_chestFlowLastDriveMs = GetTickCount64();
    setBoolProp(scr, STR("isOpenedFromNPC"), false);
    setBoolProp(scr, STR("isStorageView"), true);
    if (auto* p = scr->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC"))) *p = nullptr;
    UObject* chest = (m_hiddenGoatChest && isObjectAlive(m_hiddenGoatChest)) ? m_hiddenGoatChest : nullptr;
    // [2026-07-16] StorageObject drives the screen's PER-TICK view logic:
    // with it null the BP re-asserts the non-storage layout every frame
    // (widget dump: our dressing reverted within 50ms) — exactly how a
    // real chest keeps the storage view. Point it at the hidden chest.
    if (chest)
        if (auto* p = scr->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageObject"))) *p = chest;

    // Rebuild the grid only when the handle actually drifted — the
    // rebuild (ClearChildren) is the flash source; clean passes just
    // re-dress.
    bool drifted = true;
    if (UObject* cont0 = m_sbChestFlowCont.Get())
        if (isObjectAlive(cont0))
            if (auto* hp = cont0->GetValuePtrByPropertyNameInChain<uint8_t>(STR("storageHandle")))
                drifted = (std::memcmp(hp, m_sbHandleCache, 20) != 0);
    if (drifted)
        driveSaddlebagStorageContainer(scr, m_sbGoatInvCache, m_sbPlayerInvCache, m_sbHandleCache, STR("chest-flow drive"), chest);
    applyChestFlowScreenDressing(scr);

    // Diagnostics only — the getters are the source of truth for the
    // native view-decision state (v5: no raw tail writes).
    probeStorageGetters(scr, STR("drive"));

    // [v2] Reveal animations — once per show (the actual render fix).
    if (!m_chestFlowAnimsPlayed)
    {
        m_chestFlowAnimsPlayed = true;
        chestFlowPlayRevealAnims(scr);
    }

    // [2026-07-16] Opacity insurance: the E-press that opened the goat
    // menu can reach the fresh screen as OnInteractInput and start a
    // fade-out — the screen stays "Showing"/Visible/in-viewport but
    // renders at opacity 0 (this session: all flags good, nothing on
    // screen). Force full opacity every pass.
    float opBefore = -1.0f;
    if (auto* op = scr->GetValuePtrByPropertyNameInChain<float>(STR("RenderOpacity"))) opBefore = *op;
    if (auto* sroFn = scr->GetFunctionByNameInChain(STR("SetRenderOpacity")))
    {
        std::vector<uint8_t> b(sroFn->GetParmsSize(), 0);
        if (auto* p = findParam(sroFn, STR("InOpacity"))) *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 1.0f;
        try
        {
            safeProcessEvent(scr, sroFn, b.data());
        }
        catch (...)
        {
        }
    }

    // Cache the container child so the rebind hook can compare handles
    // without a tree walk (memory read only — no PE inside hooks).
    if (UObject* cont = jw_findChildInTree(scr, STR("WBP_UI_Inventory_Storage_Container")))
        m_sbChestFlowCont = FWeakObjectPtr(cont);
    VLOG(STR("[MoriaCppMod] [ChestFlow] pass #{}: drifted={} opacityBefore={:.2f} (screen={:p})\n"),
         m_chestFlowDriveCount, drifted ? STR("YES") : STR("no"), opBefore, (void*)scr);

    // Late verify pass: catches a fade/hide that lands after this pass.
    if (m_chestFlowDriveCount < 4)
        m_chestFlowDriveAtMs = GetTickCount64() + 900;
}

void openSaddlebagsChestFlow(UObject* goat, UObject* playerInv, const uint8_t bagHandle[20])
{
    // Toggle: pressing Saddlebags while our screen is up closes it.
    if (UObject* prev = m_sbChestFlowScreen.Get())
    {
        if (isObjectAlive(prev))
        {
            bool showing = false;
            if (auto* showingFn = prev->GetFunctionByNameInChain(STR("IsShowing")))
            {
                std::vector<uint8_t> b(showingFn->GetParmsSize(), 0);
                if (safeProcessEvent(prev, showingFn, b.data()))
                    if (auto* pr = findParam(showingFn, STR("ReturnValue"))) showing = *(b.data() + pr->GetOffset_Internal()) != 0;
            }
            if (showing)
            {
                if (auto* hideFn = prev->GetFunctionByNameInChain(STR("Hide")))
                {
                    std::vector<uint8_t> b(hideFn->GetParmsSize(), 0);
                    safeProcessEvent(prev, hideFn, b.data());
                }
                m_sbChestFlowScreen = FWeakObjectPtr();
                m_chestFlowDriveAtMs = 0;
                VLOG(STR("[MoriaCppMod] [ChestFlow] toggle — screen hidden via native Hide\n"));
                return;
            }
        }
        m_sbChestFlowScreen = FWeakObjectPtr();
    }

    // Resolve the UI manager and ITS cached StorageMode instance.
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Moria.MorUIManager:BPGetManager"));
    auto* mgrCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorUIManager"));
    UObject* ctx = (m_localPC && isObjectAlive(m_localPC)) ? m_localPC : goat;
    UObject* mgr = nullptr;
    if (getMgrFn && mgrCDO && ctx)
    {
        std::vector<uint8_t> gb(getMgrFn->GetParmsSize(), 0);
        writeGoatParm<UObject*>(getMgrFn, gb.data(), STR("WorldContextObject"), ctx);
        if (safeProcessEvent(mgrCDO, getMgrFn, gb.data()))
            mgr = readGoatParm<UObject*>(getMgrFn, gb.data(), STR("ReturnValue"), nullptr);
    }
    const wchar_t* screenPath = STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C");
    UClass* screenCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, screenPath);
    UObject* screen = nullptr;
    if (mgr && isObjectAlive(mgr) && screenCls)
    {
        if (auto* getScreenFn = mgr->GetFunctionByNameInChain(STR("GetScreen")))
        {
            std::vector<uint8_t> sb(getScreenFn->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getScreenFn, sb.data(), STR("ScreenClass"), screenCls);
            if (safeProcessEvent(mgr, getScreenFn, sb.data()))
                screen = readGoatParm<UObject*>(getScreenFn, sb.data(), STR("ReturnValue"), nullptr);
        }
    }
    if (!screen || !isObjectAlive(screen))
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] manager/screen unavailable (mgr={:p} cls={:p}) — falling back to legacy takeover\n"),
             (void*)mgr, (void*)screenCls);
        openStorageWidgetForHandle(goat, playerInv, bagHandle);
        return;
    }

    // [v4 2026-07-16] BIND STORAGE BEFORE SHOW. v3 proved the flip is a
    // show-time snapshot: isStorageView was still true at RebindThisPack
    // and false by OnCustomFocusSet, and NOTHING written afterwards
    // (isStorageView each pass, StorageObject=chest, tail 0x3D0=1) undid
    // it — "WasOpenedWithStorage" is decided DURING the show pipeline
    // from what is bound at that moment. Native keeps it true because
    // the chest is bound before the show events fire; ours was null
    // (bound +250ms later). So: spawn the chest and point StorageObject
    // at it BEFORE the manager show.
    spawnHiddenGoatChest(goat);
    UObject* chest = (m_hiddenGoatChest && isObjectAlive(m_hiddenGoatChest)) ? m_hiddenGoatChest : nullptr;

    // Chest-state per the trace: NOT an NPC open, storage view, no NPC refs.
    setBoolProp(screen, STR("isOpenedFromNPC"), false);
    setBoolProp(screen, STR("isStorageView"), true);
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC"))) *p = nullptr;
    if (auto* p = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageObject"))) *p = chest;

    // [v5] No raw tail writes: the getter probe disproved the 0x3D0
    // guess (WasOpenedWithStorage=1 with 0x3D0=0 — it reads
    // StorageObject). The real gate is IsActivatedFromInteract, set by
    // showing via ActivateHud(bFromInteract=true) below.

    // Native show FIRST — via the MANAGER pipeline (stack push + input +
    // presentation). PE-ing the screen's own Show() fires events and
    // flips IsShowing but renders nothing (2026-07-16: every widget-
    // level flag green, screen never visible).
    m_sbChestFlowMgr = FWeakObjectPtr(mgr);
    m_sbChestFlowScreen = FWeakObjectPtr(screen);
    if (!chestFlowShowViaManager(screen))
    {
        VLOG(STR("[MoriaCppMod] [ChestFlow] manager show FAILED — falling back to legacy takeover\n"));
        m_sbChestFlowScreen = FWeakObjectPtr();
        openStorageWidgetForHandle(goat, playerInv, bagHandle);
        return;
    }

    // Do NOT drive yet: the native show sequence runs on the following
    // tick(s) and its RebindThisPack would overwrite us with the goat
    // interaction bind. Cache everything and arm the deferred drive
    // (also re-armed by the RebindThisPack/OnAfterShow hook).
    m_sbGoatInvCache = playerInv;
    m_sbPlayerInvCache = playerInv;
    std::memcpy(m_sbHandleCache, bagHandle, 20);
    m_sbChestFlowScreen = FWeakObjectPtr(screen);
    m_sbWidgetOpenMs = GetTickCount64();
    m_chestFlowDriveCount = 0;
    m_chestFlowAnimsPlayed = false;
    m_chestFlowDriveAtMs = GetTickCount64() + 250; // fallback if no event fires
    m_forensicsLabel = STR("MOD-OPEN");
    m_forensicsDumpAtMs = GetTickCount64() + 1600;
    VLOG(STR("[MoriaCppMod] [ChestFlow] OPEN via UI manager: screen={:p} chest={:p} (drive deferred past native rebind)\n"),
         (void*)screen, (void*)chest);
}

// [rc.67 STORAGE WIDGET HELPER 2026-06-29] Spawn vanilla
// WBP_UI_Inventory_Screen_StorageMode_C bound to the given container
// handle on the goat. Used by rc.67 openGoatSaddlebagInventory.
void openStorageWidgetForHandle(UObject* goat, UObject* goatInv, const uint8_t bagHandle[20])
{
    // Dismiss any prior widget instance (anti-stack)
    if (m_test3SaddlebagWidget && isObjectAlive(m_test3SaddlebagWidget))
    {
        if (auto* rmFn = m_test3SaddlebagWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
        {
            try
            {
                safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr);
            }
            catch (...)
            {
            }
        }
        m_test3SaddlebagWidget = nullptr;
    }

    const wchar_t* widgetPath = STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C");
    UClass* widgetCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widgetPath);
    if (!widgetCls) widgetCls = goat_loadClassAssetBlocking(widgetPath);
    if (!widgetCls)
    {
        showOnScreen(L"StorageMode widget class missing", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }

    UObject* w = jw_createGameWidget(widgetCls);
    if (!w || !isObjectAlive(w))
    {
        showOnScreen(L"Widget create failed", 2.5f, 0.9f, 0.4f, 0.4f);
        return;
    }

    auto writeBytes = [&](const wchar_t* propName, const uint8_t* src, int size) {
        if (auto* p = w->GetValuePtrByPropertyNameInChain<uint8_t>(propName)) std::memcpy(p, src, size);
    };
    auto writeObj = [&](const wchar_t* propName, UObject* val) {
        if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(propName)) *p = val;
    };
    auto writeBool = [&](const wchar_t* propName, bool val) {
        setBoolProp(w, propName, val);
    };

    // [rc.103 CHEST MODE 2026-07-10] LEFT pane = the passed container
    // handle, rendered as a storage GRID. The old NPC-mode bindings
    // (isOpenedFromNPC=true + AssociatedNPC + stale Dwarf.BodyInventoryGOAT
    // epicPackTag) routed the left pane to WBP_UI_Inventory_NPC_C — a
    // widget with 12 HARDCODED dwarf body slots (rc.102 dump proof).
    // Chest mode is what chests use and renders the actual grid shape
    // (Goat_Saddlebags = 8x8).
    writeBytes(STR("storageInventoryHandle"), bagHandle, 20);

    // RIGHT pane = player's body inventory
    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    UObject* playerInv = pawn ? findPlayerInventoryComponent(pawn) : nullptr;
    if (playerInv)
    {
        uint8_t bodyHandle[20] = {0};
        if (auto* gbtFn = playerInv->GetFunctionByNameInChain(STR("GetContainerByTag")))
        {
            RC::Unreal::FName bodyTag(STR("Inventory.BodyInventory"), RC::Unreal::FNAME_Add);
            int sz = gbtFn->GetParmsSize();
            std::vector<uint8_t> tb(sz, 0);
            auto* pIn = findParam(gbtFn, STR("Tag"));
            if (!pIn) pIn = findParam(gbtFn, STR("ContainerTag"));
            auto* pRet = findParam(gbtFn, STR("ReturnValue"));
            int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
            int rOff = pRet ? pRet->GetOffset_Internal() : 8;
            std::memcpy(tb.data() + tagOff, &bodyTag, sizeof(RC::Unreal::FName));
            try
            {
                safeProcessEvent(playerInv, gbtFn, tb.data());
            }
            catch (...)
            {
            }
            std::memcpy(bodyHandle, tb.data() + rOff, 20);
            RC::Unreal::FWeakObjectPtr ownerWP(playerInv);
            std::memcpy(bodyHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
        }
        writeObj(STR("InventoryComponent"), playerInv);
        writeBytes(STR("bodyInventoryHandle"), bodyHandle, 20);
    }

    writeBool(STR("isStorageView"), true);
    // [rc.103] CHEST mode: isOpenedFromNPC stays false, NO AssociatedNPC —
    // those two are exactly what forced the dwarf NPC widget.
    writeBool(STR("isOpenedFromNPC"), false);
    (void)goat; // goat not bound in chest mode (kept in signature for logging/callers)

    // [rc.106 2026-07-10] Zeroing ALL of HandleStorageView's params told
    // the screen "non-storage view": it HID storageOverlay (left pane
    // empty despite Pack_Dynamic being built inside!) and picked the
    // wrong background (Background visible / BackgroundWStorage
    // collapsed = the "corrupt" right side). Log the real param list,
    // then call it with storage-ish bools TRUE and NPC-ish bools FALSE.
    if (auto* hsvFn = w->GetFunctionByNameInChain(STR("HandleStorageView")))
    {
        int sz = hsvFn->GetParmsSize();
        std::vector<uint8_t> b(sz, 0);
        for (auto* p : hsvFn->ForEachProperty())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
            }
            std::wstring pt;
            try
            {
                pt = p->GetClass().GetName();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106] HandleStorageView param '{}' type={} off={} size={}\n"),
                 pn.c_str(),
                 pt.c_str(),
                 p->GetOffset_Internal(),
                 p->GetSize());
            if (pt == STR("BoolProperty"))
            {
                bool val = false;
                // NPC-ish → false; storage/view/show-ish → true
                if (pn.find(STR("NPC")) != std::wstring::npos || pn.find(STR("Npc")) != std::wstring::npos)
                    val = false;
                else if (pn.find(STR("torage")) != std::wstring::npos || pn.find(STR("View")) != std::wstring::npos ||
                         pn.find(STR("Show")) != std::wstring::npos || pn.find(STR("Enable")) != std::wstring::npos)
                    val = true;
                b[p->GetOffset_Internal()] = val ? 1 : 0;
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106]   -> set '{}' = {}\n"), pn.c_str(), val);
            }
        }
        try
        {
            safeProcessEvent(w, hsvFn, b.data());
        }
        catch (...)
        {
        }
    }

    m_test3SaddlebagWidget = w;
    if (auto* addFn = w->GetFunctionByNameInChain(STR("AddToViewport")))
    {
        int sz = addFn->GetParmsSize();
        std::vector<uint8_t> b(sz, 0);
        if (sz >= 4) *reinterpret_cast<int32_t*>(b.data()) = 100;
        try
        {
            safeProcessEvent(w, addFn, b.data());
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.104] OUR chest-mode widget={:p} added to viewport (Z=100)\n"), (void*)w);
    }

    // [rc.106] Belt-and-braces: force the storage view-state to the
    // exact values captured from the WORKING native screen dump —
    //   storageOverlay      vis=4 (SelfHitTestInvisible)
    //   Background          vis=1 (Collapsed)
    //   BackgroundWStorage  vis=0 (Visible)
    {
        auto setVis = [&](const wchar_t* childName, uint8_t v) {
            UObject* c = jw_findChildInTree(w, childName);
            if (!c || !isObjectAlive(c))
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106] setVis: '{}' NOT FOUND\n"), childName);
                return;
            }
            if (auto* f = c->GetFunctionByNameInChain(STR("SetVisibility")))
            {
                std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                vb[0] = v;
                try
                {
                    safeProcessEvent(c, f, vb.data());
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106] setVis '{}' -> {}\n"), childName, (int)v);
            }
        };
        setVis(STR("storageOverlay"), 4);
        setVis(STR("Background"), 1);
        setVis(STR("BackgroundWStorage"), 0);
    }

    // [rc.109 ORDER FIX 2026-07-11] rc.108 fired the show events AFTER
    // our container setup — they re-ran the container down the NPC path
    // and the dwarf 4×3 came back (dump proof: WBP_UI_Inventory_NPC_C
    // re-stacked). Correct order: native show events FIRST (build the
    // player pane), OUR container setup LAST — 'Set Up Storage
    // Container' starts with ClearChildren, so it wipes whatever the
    // show flow put in the pane and builds the saddlebag grid.
    for (const wchar_t* evName : {STR("OnBeforeShow"), STR("OnAfterShow")})
    {
        if (auto* f = w->GetFunctionByNameInChain(evName))
        {
            int psz = (int)f->GetParmsSize();
            if (psz < 1) psz = 1;
            std::vector<uint8_t> b((size_t)psz, 0);
            try
            {
                safeProcessEvent(w, f, b.data());
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.108] fired native '{}'\n"), evName);
        }
    }

    // [rc.107 2026-07-10] Drive the Storage_Container child EXACTLY the
    // way the game's chest flow does (decoded from the vanilla widget BP):
    // 'Set Up Storage Container' (parmless) reads MEMBER state —
    // validates storageHandle, ClearChildren, CreateStorageWidget
    // (pack-type → 'Set Up Pack Dynamic Container' → Create New Grid of
    // GetStorageMaxSlots(storageHandle)), adds to its overlay, shows
    // RootGrid, updates TakeAll/Deposit buttons. So: write the members,
    // call the function. "Like any other storage chest."
    driveSaddlebagStorageContainer(w, goatInv, playerInv, bagHandle, STR("rc.107"));

    // [rc.109] one-shot delayed re-drive: if anything later rebuilds
    // the pane down the NPC path, we re-assert the saddlebag grid.
    std::memcpy(m_sbHandleCache, bagHandle, 20);
    m_sbGoatInvCache = goatInv;
    m_sbPlayerInvCache = playerInv;
    m_contReSetupAtMs = GetTickCount64() + 600;

    // [rc.108] Collapse the NPC placeholder banner ("NPC Name • NPC
    // Role / This dwarf needs...") + skill panel — chest mode has no
    // NPC, so these render design-time defaults.
    {
        auto collapse = [&](const wchar_t* childName) {
            UObject* c = jw_findChildInTree(w, childName);
            if (!c || !isObjectAlive(c)) return;
            if (auto* f = c->GetFunctionByNameInChain(STR("SetVisibility")))
            {
                std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                vb[0] = 1; // Collapsed
                try
                {
                    safeProcessEvent(c, f, vb.data());
                }
                catch (...)
                {
                }
            }
        };
        collapse(STR("NPCTitleCluster"));
        collapse(STR("SkillPanel"));
        collapse(STR("NPCDetailsBox"));
    }

    // [rc.108] UI input mode — screen was left in GAME input (Swing/
    // Look prompts, clicks swung the pickaxe). Match native screens.
    setInputModeUI(w);

    // [rc.107] register with the Esc/Tab close tick (it watches
    // m_goatSaddlebagWidget — our open only set m_test3SaddlebagWidget,
    // so the screen had NO close handler). Also enables the per-tick
    // visibility re-assert against the screen's self-reverting state.
    m_goatSaddlebagWidget = w;
    m_sbWidgetOpenMs = GetTickCount64(); // [rc.130] Esc/Tab grace anchor
}

// [rc.67 BODYINVENTORYGOAT 2026-06-29] Per approved plan
// deep-percolating-parnas.md — open the goat's MorInventoryComponent
// bound to its `Dwarf.BodyInventoryGOAT` container (8x8). Pak edits
// A2/A3/A4 added the DT_Storage row + new BP_ContainerItem class +
// DT_NPCUniqueCharacters row. The goat's MorInventoryComponent will
// have the BodyInventoryGOAT container once we either modify the
// BP_NpcGoat CDO (deferred A5 — high risk) OR mutate at runtime
// (deferred to rc.68). For NOW the container may not exist yet;
// this handler logs the binding attempt and either succeeds or
// shows a clear diagnostic toast.
// [rc.101 LIVE UI HARVEST 2026-07-10] Deferred deep-dump of the live
// StorageMode screen after the Saddlebags menu row opens it. 800ms
// delay lets the screen finish binding/populating. Produces:
//   1. widget-harvest/StorageMode_GoatLive.json — full recursive tree
//      (class, name, slot, brush, size, visibility, color, font) for
//      future recreation;
//   2. compact [UIDump] VLOG tree (class 'name' vis=) in the log.
ULONGLONG m_storageHarvestAtMs{0};
// [rc.104 2026-07-10] E-release on the goat menu row ALSO fires the
// vanilla interact (OnReleaseInteract -> ServerRescueNpc) which opens
// the game's own StorageMode in NPC mode ON TOP of our chest-mode
// widget (log: instances=2). Suppress pass: shortly after our open,
// RemoveFromParent any StorageMode instance that is not ours.
ULONGLONG m_storageSuppressAtMs{0};
void tickPendingStorageHarvest()
{
    ULONGLONG now = GetTickCount64();

    // [rc.130 2026-07-12] Suppression is now a WINDOW (sweeps every
    // ~150ms until the deadline) instead of a one-shot at +500ms — the
    // vanilla NPC screen was visible long enough that the user Esc'd
    // it, and that same Esc killed OUR screen underneath.
    if (m_storageSuppressAtMs != 0 && now < m_storageSuppressAtMs)
    {
        static ULONGLONG s_lastSweep = 0;
        if (now - s_lastSweep >= 150)
        {
            s_lastSweep = now;
            std::vector<UObject*> all;
            seh_findAllOf(STR("WBP_UI_Inventory_Screen_StorageMode_C"), &all);
            for (auto* w : all)
            {
                if (!w || !isObjectAlive(w)) continue;
                if (w == m_test3SaddlebagWidget) continue;
                // [ChestFlow] the manager's native instance IS our screen
                // on the chest-flow path — never collapse it.
                if (w == m_sbChestFlowScreen.Get()) continue;
                bool inViewport = false;
                if (auto* ivFn = cachedFnInChain(w, STR("IsInViewport")))
                {
                    std::vector<uint8_t> b(ivFn->GetParmsSize(), 0);
                    try
                    {
                        safeProcessEvent(w, ivFn, b.data());
                    }
                    catch (...)
                    {
                    }
                    if (auto* pr = findParam(ivFn, STR("ReturnValue"))) inViewport = *reinterpret_cast<bool*>(b.data() + pr->GetOffset_Internal());
                }
                if (inViewport)
                {
                    // [rc.137] COLLAPSE, don't RemoveFromParent — ripping the
                    // game's SINGLETON screen from the viewport broke every
                    // later chest open (Show() can't re-add it). Collapse is
                    // reversible: the native Show restores visibility.
                    if (auto* svFn = cachedFnInChain(w, STR("SetVisibility")))
                    {
                        std::vector<uint8_t> vb(svFn->GetParmsSize(), 0);
                        vb[0] = 1; // Collapsed
                        try
                        {
                            safeProcessEvent(w, svFn, vb.data());
                        }
                        catch (...)
                        {
                        }
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.137 SWEEP] native StorageMode {:p} collapsed\n"), (void*)w);
                    }
                }
            }
        }
    }
    else if (m_storageSuppressAtMs != 0 && now >= m_storageSuppressAtMs)
    {
        m_storageSuppressAtMs = 0;
        std::vector<UObject*> all;
        seh_findAllOf(STR("WBP_UI_Inventory_Screen_StorageMode_C"), &all);
        for (auto* w : all)
        {
            if (!w || !isObjectAlive(w)) continue;
            bool ours = (w == m_test3SaddlebagWidget) || (w == m_sbChestFlowScreen.Get());
            bool inViewport = false;
            if (auto* ivFn = w->GetFunctionByNameInChain(STR("IsInViewport")))
            {
                std::vector<uint8_t> b(ivFn->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(w, ivFn, b.data());
                }
                catch (...)
                {
                }
                if (auto* pr = findParam(ivFn, STR("ReturnValue"))) inViewport = *reinterpret_cast<bool*>(b.data() + pr->GetOffset_Internal());
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.104 SUPPRESS] instance={:p} ours={} inViewport={}\n"), (void*)w, ours, inViewport);
            if (!ours && inViewport)
            {
                // [rc.137] collapse, don't remove (see sweep note — removal
                // permanently broke chest opens this session)
                if (auto* svFn = w->GetFunctionByNameInChain(STR("SetVisibility")))
                {
                    std::vector<uint8_t> vb(svFn->GetParmsSize(), 0);
                    vb[0] = 1; // Collapsed
                    try
                    {
                        safeProcessEvent(w, svFn, vb.data());
                    }
                    catch (...)
                    {
                    }
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.137 SUPPRESS] native StorageMode {:p} collapsed\n"), (void*)w);
                }
            }
        }
    }

    if (m_storageHarvestAtMs == 0) return;
    if (now < m_storageHarvestAtMs) return;
    m_storageHarvestAtMs = 0;

    // Dump ALL live instances' bindings; walk OUR widget (else first).
    std::vector<UObject*> found;
    seh_findAllOf(STR("WBP_UI_Inventory_Screen_StorageMode_C"), &found);
    UObject* screen = nullptr;
    for (auto* w : found)
    {
        if (!w || !isObjectAlive(w)) continue;
        bool ours = (w == m_test3SaddlebagWidget);
        UObject* np = nullptr;
        bool fromNpc = false;
        UObject* ic = nullptr;
        if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC"))) np = *p;
        if (auto* p = w->GetValuePtrByPropertyNameInChain<bool>(STR("isOpenedFromNPC"))) fromNpc = *p;
        if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent"))) ic = *p;
        VLOG(STR("[MoriaCppMod] [UIDump rc.101] instance={:p} ours={} AssociatedNPC={:p} isOpenedFromNPC={} InventoryComponent={:p}\n"),
             (void*)w,
             ours,
             (void*)np,
             fromNpc,
             (void*)ic);
        if (!screen) screen = w;
    }
    // Prefer OUR widget explicitly if alive.
    if (m_test3SaddlebagWidget && isObjectAlive(m_test3SaddlebagWidget)) screen = m_test3SaddlebagWidget;
    VLOG(STR("[MoriaCppMod] [UIDump rc.101] live StorageMode instances={} using={:p} (ours={})\n"), found.size(), (void*)screen, screen == m_test3SaddlebagWidget);
    if (!screen) return;

    // 1. Full-detail JSON for future recreation.
    harvestLiveWidgetToFile(screen, STR("StorageMode_GoatLive"));

    // 2. Compact tree in the log: class 'name' vis=<0..4> [+key bindings].
    //    Visibility enum: 0=Visible 1=Collapsed 2=Hidden 3=HitTestInvisible 4=SelfHitTestInvisible
    int lines = 0;
    std::function<void(UObject*, int)> walk = [&](UObject* w, int depth) {
        if (!w || !isObjectAlive(w) || lines > 2000 || depth > 16) return;
        std::wstring cls = safeClassName(w);
        std::wstring nm = safeObjectName(w);
        // [rc.102] Skip the hidden NPC debug overlay subtree — its huge
        // tree ate the line budget before the walk reached the elements
        // we actually need (storageOverlay + epicPack+DetailsHBox).
        if (nm == STR("DebugNPCOverlay") || cls == STR("WBP_NPC_Debug_C"))
        {
            VLOG(STR("[MoriaCppMod] [UIDump rc.101] {}{} '{}' (subtree SKIPPED)\n"), std::wstring(depth * 2, L' ').c_str(), cls.c_str(), nm.c_str());
            lines++;
            return;
        }
        uint8_t vis = 255;
        if (auto* v = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"))) vis = *v;
        VLOG(STR("[MoriaCppMod] [UIDump rc.101] {}{} '{}' vis={}\n"), std::wstring(depth * 2, L' ').c_str(), cls.c_str(), nm.c_str(), (int)vis);
        lines++;
        // Panel children
        if (auto* gcc = w->GetFunctionByNameInChain(STR("GetChildrenCount")))
        {
            std::vector<uint8_t> b(gcc->GetParmsSize(), 0);
            try
            {
                safeProcessEvent(w, gcc, b.data());
            }
            catch (...)
            {
            }
            int32_t n = 0;
            if (auto* pr = findParam(gcc, STR("ReturnValue"))) n = *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal());
            if (auto* gca = w->GetFunctionByNameInChain(STR("GetChildAt")))
            {
                for (int32_t i = 0; i < n && i < 64; i++)
                {
                    std::vector<uint8_t> cb(gca->GetParmsSize(), 0);
                    if (auto* pi = findParam(gca, STR("Index"))) *reinterpret_cast<int32_t*>(cb.data() + pi->GetOffset_Internal()) = i;
                    try
                    {
                        safeProcessEvent(w, gca, cb.data());
                    }
                    catch (...)
                    {
                    }
                    UObject* child = nullptr;
                    if (auto* pr2 = findParam(gca, STR("ReturnValue"))) child = *reinterpret_cast<UObject**>(cb.data() + pr2->GetOffset_Internal());
                    if (child) walk(child, depth + 1);
                }
            }
        }
        // Nested user-widget: descend into its own tree
        if (auto* wt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree")))
        {
            if (*wt)
            {
                if (auto* rp = (*wt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget")))
                    if (*rp) walk(*rp, depth + 1);
            }
        }
    };
    // Key screen-level bindings first.
    if (auto* np = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC")))
        VLOG(STR("[MoriaCppMod] [UIDump rc.101] screen.AssociatedNPC={:p} ({})\n"), (void*)*np, *np ? safeClassName(*np).c_str() : STR("null"));
    if (auto* bp = screen->GetValuePtrByPropertyNameInChain<bool>(STR("isOpenedFromNPC")))
        VLOG(STR("[MoriaCppMod] [UIDump rc.101] screen.isOpenedFromNPC={}\n"), *bp);
    if (auto* ic = screen->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent")))
        VLOG(STR("[MoriaCppMod] [UIDump rc.101] screen.InventoryComponent={:p}\n"), (void*)*ic);
    walk(screen, 0);
    VLOG(STR("[MoriaCppMod] [UIDump rc.101] DONE ({} widgets logged; full JSON in widget-harvest/StorageMode_GoatLive.json)\n"), lines);
    showOnScreen(L"UI dump written (log + StorageMode_GoatLive.json)", 2.5f, 0.4f, 0.9f, 0.4f);
}

void openGoatSaddlebagInventory()
{
    // [rc.67 SCOPED 2026-06-29] Wrap in a block so locals don't
    // collide with the unreachable legacy code that follows after
    // the return; statement at the end of this block.
    {
        UObject* goat = nullptr;
        for (auto& g : m_followGoats)
        {
            UObject* p = g.pawn.Get();
            if (p && isObjectAlive(p))
            {
                goat = p;
                break;
            }
        }
        if (!goat)
        {
            showOnScreen(L"No goat present", 2.0f, 0.9f, 0.4f, 0.4f);
            return;
        }

        // [NPC44 2026-07-17] Pure NATIVE saddlebags: do not open, drive,
        // suppress, or spawn anything — Tobi's own Saddlebags row handler
        // shows the StorageMode screen, the goat classifies as NPC, and
        // the NPC44-pak-enlarged dwarf pane renders 4x4. All override
        // machinery (chest-flow, legacy takeover, suppress window) stays
        // compiled behind this gate.
        if (m_sbNativeUI)
        {
            // [NPC44 v2 2026-07-17] Tobi's v1.12 Saddlebags row has NO
            // native handler (log-proven: gate fired 6x, zero screen-show
            // events) — the UI was always mod-opened. Minimal native open:
            // resolve the manager's StorageMode screen and fire
            // ActivateHud(bFromInteract=true). NOTHING else — no property
            // writes, no chest, no drives. The interact selection IS the
            // goat, so the native flow classifies NPC and binds the goat's
            // storage root itself (dwarf pane, 4x4 via the NPC44 paks).
            // [NO-SADDLEBAG-ITEM 2026-07-18, user spec] the saddlebag ITEM
            // is no longer used by the mod — the goat's storage is its own
            // NPC body inventory (dwarf-pattern containers).
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Moria.MorUIManager:BPGetManager"));
            auto* mgrCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorUIManager"));
            UObject* ctx = (m_localPC && isObjectAlive(m_localPC)) ? m_localPC : goat;
            UObject* mgr = nullptr;
            if (getMgrFn && mgrCDO && ctx)
            {
                std::vector<uint8_t> gb(getMgrFn->GetParmsSize(), 0);
                writeGoatParm<UObject*>(getMgrFn, gb.data(), STR("WorldContextObject"), ctx);
                if (safeProcessEvent(mgrCDO, getMgrFn, gb.data()))
                    mgr = readGoatParm<UObject*>(getMgrFn, gb.data(), STR("ReturnValue"), nullptr);
            }
            UObject* screen = nullptr;
            UClass* screenCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C"));
            if (mgr && isObjectAlive(mgr) && screenCls)
            {
                if (auto* getScreenFn = mgr->GetFunctionByNameInChain(STR("GetScreen")))
                {
                    std::vector<uint8_t> sb(getScreenFn->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getScreenFn, sb.data(), STR("ScreenClass"), screenCls);
                    if (safeProcessEvent(mgr, getScreenFn, sb.data()))
                        screen = readGoatParm<UObject*>(getScreenFn, sb.data(), STR("ReturnValue"), nullptr);
                }
            }
            bool shown = (screen && isObjectAlive(screen)) ? chestFlowActivateHud(screen) : false;
            VLOG(STR("[MoriaCppMod] [GoatSaddle] NATIVE mode — ActivateHud(no override) -> {} (screen={:p})\n"),
                 shown ? STR("OK") : STR("FAILED"), (void*)screen);
            return;
        }

        // [rc.99 EPIC-SLOT 2026-07-10] Option A: open Tobi's native
        // StorageMode bound to the goat's epic-pack slot so the player
        // can drop a CRAFTED PorterGoatSaddlebags in. Native manage showed
        // a corrupt default grid because the goat's epic-pack slot
        // (Goat.Slot.EpicPack, from DefaultContainers) is NOT instantiated
        // on a summoned goat (GetContainers=0 → nothing to bind). Try to
        // instantiate it, log the result, then fire native manage. Deep
        // logging tells us whether the slot finally appears.
        {
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
            UObject* goatInv = nullptr;
            auto readSH = [&](UObject* c) -> std::wstring {
                if (!c) return STR("");
                auto* p = c->GetPropertyByNameInChain(STR("StorageHandle"));
                if (!p) return STR("");
                auto* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + p->GetOffset_Internal() + 8);
                try
                {
                    return rn->ToString();
                }
                catch (...)
                {
                    return STR("");
                }
            };
            auto contCount = [&](UObject* c) -> int32_t {
                auto* f = c ? c->GetFunctionByNameInChain(STR("GetContainers")) : nullptr;
                if (!f) return -1;
                std::vector<uint8_t> b(f->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(c, f, b.data());
                }
                catch (...)
                {
                }
                auto* pr = findParam(f, STR("ReturnValue"));
                if (!pr) return -1;
                return *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal() + 8);
            };
            if (invCls)
            {
                if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
                {
                    std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                    if (auto* pCls = findParam(gf, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                    try
                    {
                        safeProcessEvent(goat, gf, b.data());
                    }
                    catch (...)
                    {
                    }
                    if (auto* pRet = findParam(gf, STR("ReturnValue")))
                    {
                        uint8_t* arr = b.data() + pRet->GetOffset_Internal();
                        UObject** data = *reinterpret_cast<UObject***>(arr);
                        int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                        UObject* firstAny = nullptr;
                        for (int32_t i = 0; data && i < num && i < 16; i++)
                        {
                            UObject* c = data[i];
                            if (!c || !isObjectAlive(c)) continue;
                            std::wstring nm;
                            try
                            {
                                nm = c->GetName();
                            }
                            catch (...)
                            {
                            }
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.99] comp[{}] name='{}' SH='{}' containers={}\n"), i, nm.c_str(), readSH(c).c_str(), contCount(c));
                            if (!firstAny) firstAny = c;
                            if (nm == STR("Inventory Comp")) goatInv = c; // prefer the SCS cargo comp
                        }
                        if (!goatInv) goatInv = firstAny;
                    }
                }
            }
            // [rc.103 CHEST-MODE 2026-07-10] The rc.102 dump PROVED the
            // NPC-mode left pane is WBP_UI_Inventory_NPC_C with 12
            // HARDCODED dwarf body slots — the goat can never render
            // correctly there. New flow (uses Tobi's craft recipe):
            //   1. If the goat has no container yet: require a CRAFTED
            //      PorterGoatSaddlebags in the player inventory, consume
            //      it, AddItem(BP_SaddleBags_Goat_C) on the goat (the one
            //      call proven to create the container, rc.92).
            //   2. Open StorageMode in CHEST mode (storageInventoryHandle
            //      bound to the goat container, isOpenedFromNPC=false)
            //      → left pane renders the 8x8 Goat_Saddlebags grid.
            if (!goatInv)
            {
                showOnScreen(L"Goat has no inventory component", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            // NOTE: never sweep/destroy stray BP_SaddleBags_Goat_C
            // actors here — the game drops equippables as raw actors,
            // and a cleanup sweep once destroyed the player's crafted pack.

            // [rc.126 B5] carrier-first find kept (wrapper carriers only).
            if (UObject* carrier = findSaddlebagCarrier())
            {
                tameSaddlebagActor(carrier, goat); // hide + attach
                if (openViaBagActor(goat, carrier)) return;
                VLOG(STR("[MoriaCppMod] [Carrier rc.126] carrier found but unopenable — continuing legacy path\n"));
            }

            // [rc.136 FINAL 2026-07-12] THE PLAYER-CARRIED PACK IS THE
            // BAG. Slot creation impossible (all Methods fail); moving
            // pack instances INTO the goat impossible (all AddTypes
            // fail); AddItem only mints empty duplicates. The pack in
            // the PLAYER inventory persists natively with its contents
            // (user-proven). Goat = access gate + visuals.
            {
                const wchar_t* kSaddlePathF = STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
                UClass* saddleClsF = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kSaddlePathF);
                if (!saddleClsF) saddleClsF = goat_loadClassAssetBlocking(kSaddlePathF);
                UObject* pInvF = playerInvOf();
                int32_t packIdF = (pInvF && saddleClsF) ? findPackId(pInvF, saddleClsF) : 0;
                if (packIdF != 0)
                {
                    // find the pack's 8×8 container on the PLAYER inventory
                    int32_t cid = 0;
                    if (auto* gbt = pInvF->GetFunctionByNameInChain(STR("GetContainerByTag")))
                    {
                        std::vector<uint8_t> tb(gbt->GetParmsSize(), 0);
                        auto* pIn = findParam(gbt, STR("Tag"));
                        if (!pIn) pIn = findParam(gbt, STR("ContainerTag"));
                        auto* pRet = findParam(gbt, STR("ReturnValue"));
                        RC::Unreal::FName t(STR("Goat_Saddlebags"), RC::Unreal::FNAME_Add);
                        if (pIn) std::memcpy(tb.data() + pIn->GetOffset_Internal(), &t, sizeof(t));
                        try
                        {
                            safeProcessEvent(pInvF, gbt, tb.data());
                        }
                        catch (...)
                        {
                        }
                        if (pRet) cid = *reinterpret_cast<int32_t*>(tb.data() + pRet->GetOffset_Internal());
                    }
                    VLOG(STR("[MoriaCppMod] [rc.136] player pack id={} GetContainerByTag('Goat_Saddlebags') -> cid={}\n"), packIdF, cid);
                    if (cid == 0)
                    {
                        // diagnosis: list ALL player containers
                        if (auto* gcf = pInvF->GetFunctionByNameInChain(STR("GetContainers")))
                        {
                            std::vector<uint8_t> gb(gcf->GetParmsSize(), 0);
                            try
                            {
                                safeProcessEvent(pInvF, gcf, gb.data());
                            }
                            catch (...)
                            {
                            }
                            if (auto* pr = findParam(gcf, STR("ReturnValue")))
                            {
                                uint8_t* arr = gb.data() + pr->GetOffset_Internal();
                                uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
                                int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                                VLOG(STR("[MoriaCppMod] [rc.136] player inv has {} container(s):\n"), num);
                                for (int32_t i = 0; data && i < num && i < 24; i++)
                                    VLOG(STR("[MoriaCppMod] [rc.136]   container[{}] id={}\n"), i, *reinterpret_cast<int32_t*>(data + i * 20));
                                // heuristic: the pack's container is the LAST one
                                if (data && num > 0) cid = *reinterpret_cast<int32_t*>(data + (num - 1) * 20);
                            }
                        }
                        VLOG(STR("[MoriaCppMod] [rc.136] tag miss — using LAST player container id={}\n"), cid);
                    }
                    if (cid != 0)
                    {
                        uint8_t ph[20] = {0};
                        *reinterpret_cast<int32_t*>(ph) = cid;
                        RC::Unreal::FWeakObjectPtr wpP(pInvF);
                        std::memcpy(ph + 8, &wpP, sizeof(wpP));
                        // [NO-SADDLEBAG-ITEM] equip removed (legacy path, unreachable in native mode)
                        VLOG(STR("[MoriaCppMod] [rc.136] opening PLAYER-side pack container id={} via goat menu\n"), cid);
                        // [ChestFlow] native chest-style open via the UI
                        // manager (default); legacy takeover kept behind
                        // [GoatCompanion] ChestFlowUI=false as a fallback.
                        if (m_chestFlowUI)
                            openSaddlebagsChestFlow(goat, pInvF, ph);
                        else
                            openStorageWidgetForHandle(goat, pInvF, ph);
                        m_storageSuppressAtMs = GetTickCount64() + 1500;
                        m_storageHarvestAtMs = GetTickCount64() + 1200;
                        return;
                    }
                }
            }

            int32_t cnt = contCount(goatInv);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.103] containers at open={}\n"), cnt);
            if (cnt <= 0)
            {
                // [rc.116 RESTORED-BAG SCAN 2026-07-11] Before fitting a
                // NEW bag, look for a save-system-RESTORED saddlebag actor
                // from a previous session (Channel-C restore recreates
                // registered container actors on world load). Pure
                // discovery this round — logs what reload gives us.
                {
                    std::vector<UObject*> bags;
                    findAllOfSafe(STR("BP_SaddleBags_Goat_C"), bags);
                    int liveBags = 0;
                    for (auto* b : bags)
                    {
                        if (!b || !isObjectAlive(b)) continue;
                        std::wstring nm = safeObjectName(b);
                        if (nm.rfind(STR("Default__"), 0) == 0) continue;
                        UObject* owner = nullptr;
                        if (auto* f = b->GetFunctionByNameInChain(STR("GetOwner")))
                        {
                            std::vector<uint8_t> ob(f->GetParmsSize(), 0);
                            try
                            {
                                safeProcessEvent(b, f, ob.data());
                            }
                            catch (...)
                            {
                            }
                            if (auto* pr = findParam(f, STR("ReturnValue"))) owner = *reinterpret_cast<UObject**>(ob.data() + pr->GetOffset_Internal());
                        }
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116 SCAN] live saddlebag actor '{}' {:p} owner={} \n"),
                             nm.c_str(),
                             (void*)b,
                             owner ? safeClassName(owner).c_str() : STR("(none)"));
                        liveBags++;
                    }
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116 SCAN] {} live saddlebag actor(s) in world before fit\n"), liveBags);
                }
                const wchar_t* kSaddlePath = STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
                UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kSaddlePath);
                if (!saddleCls) saddleCls = goat_loadClassAssetBlocking(kSaddlePath);
                UObject* pawn = (m_localPawn && isObjectAlive(m_localPawn)) ? m_localPawn : getPawn();
                UObject* playerInv = pawn ? findPlayerInventoryComponent(pawn) : nullptr;
                // Count crafted saddlebags in the player inventory.
                int32_t playerHas = 0;
                if (playerInv && saddleCls)
                {
                    if (auto* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items")))
                    {
                        uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv) + itemsProp->GetOffset_Internal() + iiaListOff();
                        if (isReadableMemory(listBase, 16))
                        {
                            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                            int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
                            int stride = iiSize(), itemOff = iiItemOff();
                            for (int32_t i = 0; arrData && i < arrNum && i < 2000; i++)
                            {
                                uint8_t* entry = arrData + i * stride;
                                if (!isReadableMemory(entry, stride)) continue;
                                if (*reinterpret_cast<UClass**>(entry + itemOff) == saddleCls) playerHas++;
                            }
                        }
                    }
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.103] saddleCls={:p} playerInv={:p} playerHas={}\n"), (void*)saddleCls, (void*)playerInv, playerHas);
                if (playerHas <= 0)
                {
                    showOnScreen(L"Craft Goat Saddlebags first, then use Saddlebags again", 3.5f, 1.0f, 0.7f, 0.3f);
                    // [rc.128] still suppress the vanilla NPC screen the
                    // E-release opens — otherwise the dwarf 4×3 appears
                    // and reads as "the saddlebags broke".
                    m_storageSuppressAtMs = GetTickCount64() + 1500; // [rc.130] suppression WINDOW (sweeps every 150ms)
                    return;
                }

                // [rc.131 B5-v2] Craft confirmed → EJECT the crafted pack
                // (equip-slot drop family → BP_DropItem wrapper, the
                // natively-persistent container Part A proved) → tame →
                // open bound to the wrapper's own inventory. TRUE
                // transfer of the crafted item; contents ride the SAVE.
                {
                    if (ejectCraftedPack(playerInv, saddleCls))
                    {
                        UObject* carrier = findSaddlebagCarrier();
                        if (carrier)
                        {
                            tameSaddlebagActor(carrier, goat);
                            if (openViaBagActor(goat, carrier))
                            {
                                showOnScreen(L"Saddlebags fitted to the goat", 2.0f, 0.4f, 0.9f, 0.4f);
                                return;
                            }
                            VLOG(STR("[MoriaCppMod] [B5v2 rc.131] carrier unopenable — legacy fallback\n"));
                        }
                        else
                            VLOG(STR("[MoriaCppMod] [B5v2 rc.131] eject fired but NO wrapper carrier found — legacy fallback\n"));
                    }
                }

                // [rc.133 B7] TRUE TRANSFER first: MOVE the player's
                // crafted pack (with any contents inside!) onto the
                // goat — one item instance forever. AddItem below only
                // runs if the move fails.
                {
                    UObject* gInvB7 = goatInvOf(goat);
                    // [rc.135] Tobi's designed home first: instantiate the
                    // 1×1 Goat.Slot.EpicPack container (Method sweep), THEN
                    // move the pack into it — pack-in-slot, dwarf-style.
                    if (gInvB7) ensureGoatSlotContainer(gInvB7);
                    if (gInvB7 && movePackBetween(playerInv, gInvB7, STR("fit player->goat")))
                    {
                        cnt = contCount(goatInv);
                        VLOG(STR("[MoriaCppMod] [B7 rc.133] fit-by-move: goat containers now={}\n"), cnt);
                    }
                }
                if (cnt > 0)
                {
                    showOnScreen(L"Saddlebags fitted to the goat", 2.0f, 0.4f, 0.9f, 0.4f);
                }
                else
                {
                    // [rc.124] B2 spawn removed (inert prop — see above).
                    // Create the goat's container by adding the pack item.
                    for (const wchar_t* fnName : {STR("AddItem"), STR("RequestAddItem")})
                    {
                        auto* af = goatInv->GetFunctionByNameInChain(fnName);
                        if (!af) continue;
                        std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                        auto* pItem = findParam(af, STR("Item"));
                        if (!pItem) pItem = findParam(af, STR("Class"));
                        auto* pCount = findParam(af, STR("Count"));
                        if (pItem) *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = saddleCls;
                        if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                        try
                        {
                            safeProcessEvent(goatInv, af, ab.data());
                        }
                        catch (...)
                        {
                        }
                        break;
                    }
                    cnt = contCount(goatInv);
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.103] after AddItem(saddlebag): containers={}\n"), cnt);
                    if (cnt <= 0)
                    {
                        showOnScreen(L"Failed to fit saddlebags to goat (see log)", 2.5f, 0.9f, 0.4f, 0.4f);
                        return;
                    }

                    // [rc.116 SAVE-REGISTER 2026-07-11] The fitted bag is a
                    // real container but the save system knows nothing about
                    // it (no GUID record) — that's why contents vanish on
                    // reload. Register the newly created saddlebag ACTOR via
                    // StoreRuntimeActor (Channel-C, same as chests): its
                    // contents then serialize with the save keyed by GUID.
                    {
                        std::vector<UObject*> bags;
                        findAllOfSafe(STR("BP_SaddleBags_Goat_C"), bags);
                        UObject* newest = nullptr;
                        for (auto* b : bags)
                        {
                            if (!b || !isObjectAlive(b)) continue;
                            std::wstring nm = safeObjectName(b);
                            if (nm.rfind(STR("Default__"), 0) == 0) continue;
                            newest = b; // FindAllOf order: last = most recently created
                        }
                        if (newest)
                        {
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116] registering fitted saddlebag actor {:p} '{}' with save system...\n"),
                                 (void*)newest,
                                 safeObjectName(newest).c_str());
                            storeGoatInWorldState(newest);
                        }
                        else
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116] NO saddlebag actor found after fit — container may be item-entry only (no actor); "
                                     "Channel-C not applicable, need alternate contents persistence\n"));
                    }

                    // DO NOT consume the crafted saddlebag — the pack item
                    // stays in the PLAYER inventory permanently; that IS
                    // the persistence (final architecture).
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.105] crafted saddlebag NOT consumed (persistence pending)\n"));
                    showOnScreen(L"Saddlebags fitted to the goat", 2.0f, 0.4f, 0.9f, 0.4f);
                } // [rc.133] end AddItem fallback (B7 move failed)
            }
            // Bind the first container's 20-byte handle and open CHEST mode.
            uint8_t bagH[20] = {0};
            int32_t bagId2 = 0;
            if (auto* gcFn = goatInv->GetFunctionByNameInChain(STR("GetContainers")))
            {
                std::vector<uint8_t> gb(gcFn->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(goatInv, gcFn, gb.data());
                }
                catch (...)
                {
                }
                if (auto* pRet = findParam(gcFn, STR("ReturnValue")))
                {
                    uint8_t* arr = gb.data() + pRet->GetOffset_Internal();
                    uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
                    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                    if (data && num > 0)
                    {
                        // [rc.135] bind the LAST container — with the slot
                        // architecture GetContainers = [1×1 slot, pack 8×8];
                        // the cargo grid is the newest entry.
                        uint8_t* elem = data + (num - 1) * 20;
                        std::memcpy(bagH, elem, 20);
                        bagId2 = *reinterpret_cast<int32_t*>(elem);
                        RC::Unreal::FWeakObjectPtr wp(goatInv);
                        std::memcpy(bagH + 8, &wp, sizeof(wp));
                        VLOG(STR("[MoriaCppMod] [B7 rc.135] {} container(s); binding LAST id={}\n"), num, bagId2);
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.103] opening CHEST-mode StorageMode, container id={}\n"), bagId2);
            if (bagId2 == 0)
            {
                showOnScreen(L"Goat container not found (see log)", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            openStorageWidgetForHandle(goat, goatInv, bagH);
            // [rc.104] the vanilla E-release interact opens the native
            // NPC-mode screen ~100-300ms later ON TOP of ours — suppress
            // it at +500ms, then dump the (now unobstructed) UI at +1200ms.
            m_storageSuppressAtMs = GetTickCount64() + 1500; // [rc.130] suppression WINDOW (sweeps every 150ms)
            m_storageHarvestAtMs = GetTickCount64() + 1200;
            return;
        }

        // [rc.87 EQUIP PACK 2026-07-06] The goat's Goat_Saddlebags storage
        // is an EQUIP-BASED pack (DT_Storage row has AllowedEquip[4], a
        // proper clone of the vanilla AdventurersPack_Large). StorageHandle
        // alone does NOT instantiate the container (HasContainers=false) —
        // the cargo grid is created when a PACK ITEM is equipped. So equip
        // Tobi's saddlebag (BP_SaddleBags_Goat, whose storage IS
        // Goat_Saddlebags 8x8) onto the goat's MorEquipComponent via
        // ServerEquipDummyItem, THEN open. One-shot per goat.
        {
            UClass* saddleCls = ensureSaddlebagItemClass();
            UClass* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorEquipComponent"));
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] saddleCls={:p} equipCls={:p}\n"), (void*)saddleCls, (void*)equipCls);
            if (saddleCls && equipCls)
            {
                if (auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), equipCls);
                    UObject* equipComp = nullptr;
                    if (safeProcessEvent(goat, getComp, gb.data())) equipComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
                    if (equipComp && isObjectAlive(equipComp))
                    {
                        if (auto* sedFn = equipComp->GetFunctionByNameInChain(STR("ServerEquipDummyItem")))
                        {
                            std::vector<uint8_t> eb(sedFn->GetParmsSize(), 0);
                            auto* pIt = findParam(sedFn, STR("ItemToEquip"));
                            if (!pIt) pIt = findParam(sedFn, STR("Item"));
                            if (pIt) *reinterpret_cast<UClass**>(eb.data() + pIt->GetOffset_Internal()) = saddleCls;
                            try
                            {
                                safeProcessEvent(equipComp, sedFn, eb.data());
                            }
                            catch (...)
                            {
                            }
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] ServerEquipDummyItem(BP_SaddleBags_Goat) fired on EquipComp={:p}\n"), (void*)equipComp);
                        }
                        else
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] ServerEquipDummyItem missing on EquipComp\n"));
                    }
                    else
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] no MorEquipComponent on goat\n"));
                }
            }
        }

        // [rc.86 NATIVE MANAGE 2026-07-06] Per the vanilla BP_NpcGoat
        // analysis: the goat's inventory is StorageHandle-based, and its
        // ubergraph wires MorNpcOnManageLocalInteraction -> the StorageMode
        // screen bound to the goat's primary storage. Fire that native
        // handler (now valid GUID via adoption + rc.85). After the equip
        // above instantiates the Goat_Saddlebags container, this shows it.
        {
            UClass* gc = nullptr;
            try
            {
                gc = goat->GetClassPrivate();
            }
            catch (...)
            {
            }
            UFunction* mgFn = nullptr;
            if (gc)
            {
                try
                {
                    for (auto* fn : gc->ForEachFunctionInChain())
                    {
                        if (!fn) continue;
                        std::wstring fn2;
                        try
                        {
                            fn2 = fn->GetName();
                        }
                        catch (...)
                        {
                            continue;
                        }
                        if (fn2.find(STR("MorNpcOnManageLocalInteraction")) != std::wstring::npos)
                        {
                            mgFn = fn;
                            break;
                        }
                    }
                }
                catch (...)
                {
                }
            }
            // [rc.90 2026-07-07] DISABLED. The native manage handler
            // opens the goat's BASE MorInventoryComponent (equipment /
            // "body" slots = the 4x3 "helmet" grid the user keeps
            // seeing), NOT the Goat.Slot.EpicPack cargo container Tobi
            // v1.11.0 added via DefaultContainers. Firing it + returning
            // here bypassed the whole container hunt below (log proves
            // it: rc.76 enumeration never ran). Skip native manage and
            // let the container hunt target the real container.
            const bool kUseNativeManage = false;
            if (kUseNativeManage && mgFn)
            {
                std::wstring fn2;
                try
                {
                    fn2 = mgFn->GetName();
                }
                catch (...)
                {
                }
                int psz = mgFn->GetParmsSize();
                std::vector<uint8_t> pbuf(psz, 0);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] firing native '{}' (parmSize={}) on goat={:p}\n"), fn2.c_str(), psz, (void*)goat);
                try
                {
                    safeProcessEvent(goat, mgFn, pbuf.data());
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] native manage handler dispatched — watch for the storage screen\n"));
                showOnScreen(L"Opening goat storage (native)", 1.5f, 0.4f, 0.9f, 0.5f);
                return; // native path owns the UI; do not run the container hunt
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.90] native manage SKIPPED (shows base 4x3); running container hunt for Goat.Slot.EpicPack\n"));
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] MorNpcOnManageLocalInteraction NOT found on goat — falling back to container path\n"));
        }

        UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
        UObject* goatInv = nullptr;

        // [rc.76 2026-07-03] The goat has TWO MorInventoryComponents:
        // Tobi's "Inventory Comp" (StorageHandle=Goat_Saddlebags, the
        // real one) and a base "InventoryComp" (empty, inherited).
        // GetComponentByClass returns the FIRST — often the empty base
        // one, which is why we always read StorageHandle='None'/Items=0.
        // Enumerate ALL and prefer the one whose StorageHandle.RowName
        // is Goat_Saddlebags (fall back to first non-empty, then any).
        auto readStorageRow = [&](UObject* comp) -> std::wstring {
            if (!comp) return STR("");
            auto* shP = comp->GetPropertyByNameInChain(STR("StorageHandle"));
            if (!shP) return STR("");
            RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(comp) + shP->GetOffset_Internal() + 8);
            try
            {
                return rn->ToString();
            }
            catch (...)
            {
                return STR("");
            }
        };
        if (invCls)
        {
            // Try the array enumerator first (K2_GetComponentsByClass),
            // else GetComponentsByClass, else fall back to single.
            UObject* firstAny = nullptr;
            for (const wchar_t* enumFn : {STR("K2_GetComponentsByClass"), STR("GetComponentsByClass")})
            {
                auto* gf = goat->GetFunctionByNameInChain(enumFn);
                if (!gf) continue;
                std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                if (auto* pCls = findParam(gf, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                try
                {
                    safeProcessEvent(goat, gf, b.data());
                }
                catch (...)
                {
                }
                auto* pRet = findParam(gf, STR("ReturnValue"));
                if (!pRet) continue;
                uint8_t* arr = b.data() + pRet->GetOffset_Internal();
                UObject** data = *reinterpret_cast<UObject***>(arr);
                int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.76] {} -> {} MorInventoryComponent(s)\n"), enumFn, num);
                for (int32_t i = 0; data && i < num && i < 16; i++)
                {
                    UObject* comp = data[i];
                    if (!comp || !isObjectAlive(comp)) continue;
                    std::wstring nm;
                    try
                    {
                        nm = comp->GetName();
                    }
                    catch (...)
                    {
                    }
                    std::wstring sh = readStorageRow(comp);
                    // [rc.90] Ground-truth per component: does it actually
                    // have a container now (Tobi v1.11.0 DefaultContainers)?
                    bool compHas = false;
                    int compDefNum = -1;
                    if (auto* hcF = comp->GetFunctionByNameInChain(STR("HasContainers")))
                    {
                        std::vector<uint8_t> hb(hcF->GetParmsSize(), 0);
                        try
                        {
                            safeProcessEvent(comp, hcF, hb.data());
                        }
                        catch (...)
                        {
                        }
                        if (auto* pr = findParam(hcF, STR("ReturnValue"))) compHas = *reinterpret_cast<bool*>(hb.data() + pr->GetOffset_Internal());
                    }
                    if (auto* dcP = comp->GetPropertyByNameInChain(STR("DefaultContainers")))
                    {
                        uint8_t* a = reinterpret_cast<uint8_t*>(comp) + dcP->GetOffset_Internal();
                        if (isReadableMemory(a, 16)) compDefNum = *reinterpret_cast<int32_t*>(a + 8);
                    }
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.90]   comp[{}] name='{}' StorageHandle='{}' HasContainers={} DefaultContainers.Num={}\n"),
                         i,
                         nm.c_str(),
                         sh.c_str(),
                         compHas,
                         compDefNum);
                    if (!firstAny) firstAny = comp;
                    // Prefer a component that ACTUALLY has containers and
                    // whose StorageHandle is the goat cargo/epic-pack slot.
                    if (compHas && (sh == STR("Goat.Slot.EpicPack") || sh == STR("Goat_Saddlebags")))
                    {
                        goatInv = comp;
                    }
                    else if (!goatInv && compHas)
                    {
                        goatInv = comp;
                    }
                }
                if (goatInv || firstAny) break;
            }
            if (!goatInv) goatInv = firstAny;
            // Absolute fallback: single-component API.
            if (!goatInv)
            {
                if (auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> b(getCompFn->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getCompFn, b.data(), STR("ComponentClass"), invCls);
                    if (safeProcessEvent(goat, getCompFn, b.data())) goatInv = readGoatParm<UObject*>(getCompFn, b.data(), STR("ReturnValue"), nullptr);
                }
            }
        }
        if (!goatInv || !isObjectAlive(goatInv))
        {
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.67] goat has no MorInventoryComponent\n"));
            showOnScreen(L"Goat has no inventory component", 2.5f, 0.9f, 0.4f, 0.4f);
            return;
        }
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.76] chosen goatInv={:p} StorageHandle='{}'\n"), (void*)goatInv, readStorageRow(goatInv).c_str());

        // [rc.75 LOADOUT APPLY 2026-07-03] Tobi's BP_NpcGoat.InventoryComp
        // has an EMPTY DefaultContainers (unlike the dwarf), so no grid is
        // ever created on our raw-spawned goat (HasContainers=false,
        // Items=0). His BP_PorterGoatLoader would apply DA_NpcGoat_Porter-
        // Loadout via GA_Bell, which we bypass. Try to reproduce that here:
        // if the inventory has no containers, apply the porter loadout
        // ourselves via ServerDebugApplyLoadout / SetStartingLoadout. Zero
        // pak edits — if this builds the container we're done.
        auto goatHasContainers = [&]() -> bool {
            auto* hc = goatInv->GetFunctionByNameInChain(STR("HasContainers"));
            if (!hc) return false;
            std::vector<uint8_t> hb(hc->GetParmsSize(), 0);
            try
            {
                safeProcessEvent(goatInv, hc, hb.data());
            }
            catch (...)
            {
            }
            auto* pr = findParam(hc, STR("ReturnValue"));
            return pr ? *reinterpret_cast<bool*>(hb.data() + pr->GetOffset_Internal()) : false;
        };
        bool hc0 = goatHasContainers();
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.75] HasContainers(before)={}\n"), hc0);
        // [rc.92 INSTANTIATE 2026-07-07] Tobi v1.11.0 populated
        // DefaultContainers (Num=1) but nothing runs the native
        // instantiation on our bell-spawned goat (HasContainers=false;
        // his BP_PorterGoatPersistor is a TimeManager clone with no
        // inventory code). The MorInventoryComponent has NO
        // Create/Add/Init-Container UFUNCTION (native only), BUT it does
        // expose ResetToStarting() (re-runs starting init → re-processes
        // DefaultContainers) and AddItem(Class,Count,Method) (a container
        // ITEM added to the inventory becomes a container). Force it:
        //   1. ResetToStarting()  → build the 1x1 Goat.Slot.EpicPack slot
        //   2. AddItem(epic-slot container class) if step 1 didn't
        //   3. AddItem(BP_SaddleBags_Goat pack) → fills slot → 8x8 cargo
        // Log HasContainers + container count after each so we see what
        // actually builds. Runs once per open while HasContainers=false.
        auto goatContainerCount = [&]() -> int32_t {
            auto* gc = goatInv->GetFunctionByNameInChain(STR("GetContainers"));
            if (!gc) return -1;
            std::vector<uint8_t> gb(gc->GetParmsSize(), 0);
            try
            {
                safeProcessEvent(goatInv, gc, gb.data());
            }
            catch (...)
            {
            }
            auto* pr = findParam(gc, STR("ReturnValue"));
            if (!pr) return -1;
            uint8_t* arr = gb.data() + pr->GetOffset_Internal();
            return *reinterpret_cast<int32_t*>(arr + 8);
        };
        auto goatAddItem = [&](const wchar_t* clsPath) -> void {
            UClass* cc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, clsPath);
            if (!cc) cc = goat_loadClassAssetBlocking(clsPath);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] AddItem class={:p} '{}'\n"), (void*)cc, clsPath);
            if (!cc) return;
            // Prefer server RPC if present, else the plain AddItem.
            for (const wchar_t* fnName : {STR("AddItem"), STR("RequestAddItem")})
            {
                auto* af = goatInv->GetFunctionByNameInChain(fnName);
                if (!af) continue;
                std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                auto* pItem = findParam(af, STR("Item"));
                if (!pItem) pItem = findParam(af, STR("Class"));
                auto* pCount = findParam(af, STR("Count"));
                if (pItem) *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = cc;
                if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                try
                {
                    safeProcessEvent(goatInv, af, ab.data());
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] {}('{}') -> HasContainers={} count={}\n"), fnName, clsPath, goatHasContainers(), goatContainerCount());
                break; // one add fn is enough
            }
        };

        if (!hc0)
        {
            // Step 1 — ResetToStarting (zero-arg; re-processes DefaultContainers)
            if (auto* rs = goatInv->GetFunctionByNameInChain(STR("ResetToStarting")))
            {
                std::vector<uint8_t> rb(rs->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(goatInv, rs, rb.data());
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] ResetToStarting() -> HasContainers={} count={}\n"), goatHasContainers(), goatContainerCount());
            }
            else
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] ResetToStarting NOT FOUND\n"));

            // Step 2 — AddItem the epic-slot container class if still empty
            if (!goatHasContainers()) goatAddItem(STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C"));

            // Step 3 — AddItem the saddlebag pack (fills the epic slot → 8x8 cargo)
            goatAddItem(STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));

            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] AFTER injection: HasContainers={} count={}\n"), goatHasContainers(), goatContainerCount());
        }

        // [rc.93 NATIVE OPEN 2026-07-07] The container now EXISTS
        // (saddlebag added -> HasContainers=true). Our custom
        // openStorageWidgetForHandle hand-binds the StorageMode widget
        // with stale "Dwarf.BodyInventoryGOAT" tags + AssociatedNPC=goat,
        // which makes the widget re-derive from the goat tag and fall
        // back to a default 4x3 (the "axes/helmets" placeholder). Now
        // that a real container is present, let the GAME open the goat's
        // storage its own way (MorNpcOnManageLocalInteraction) — the
        // epic-pack slot expands to show the saddlebag's 8x8 (vanilla
        // pack behavior). Fall through to the custom widget only if the
        // native handler isn't found.
        if (goatHasContainers())
        {
            UClass* gc2 = nullptr;
            try
            {
                gc2 = goat->GetClassPrivate();
            }
            catch (...)
            {
            }
            UFunction* mg2 = nullptr;
            if (gc2)
            {
                try
                {
                    for (auto* fn : gc2->ForEachFunctionInChain())
                    {
                        if (!fn) continue;
                        std::wstring n;
                        try
                        {
                            n = fn->GetName();
                        }
                        catch (...)
                        {
                            continue;
                        }
                        if (n.find(STR("MorNpcOnManageLocalInteraction")) != std::wstring::npos)
                        {
                            mg2 = fn;
                            break;
                        }
                    }
                }
                catch (...)
                {
                }
            }
            if (mg2)
            {
                std::vector<uint8_t> pb(mg2->GetParmsSize(), 0);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.93] container exists -> firing native manage '{}' (parm={})\n"),
                     mg2->GetName().c_str(),
                     mg2->GetParmsSize());
                try
                {
                    safeProcessEvent(goat, mg2, pb.data());
                }
                catch (...)
                {
                }
                showOnScreen(L"Opening goat saddlebags", 1.5f, 0.4f, 0.9f, 0.5f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.93] native manage fn NOT found; using custom widget path\n"));
        }

        // [rc.72 2026-07-03] Tobi's v1.10.0 goat uses its native
        // MorInventoryComponent whose StorageHandle.RowName is
        // "Goat_Saddlebags" (8x8). The old "Dwarf.BodyInventoryGOAT"
        // tag was OUR abandoned build's name and never resolves on his
        // goat (log: GetContainerByTag(...) -> id=0). Try the real
        // candidate tags in priority order and open the first that
        // resolves; log every attempt so a miss tells us the exact tag.
        uint8_t bagHandle[20] = {0};
        int32_t bagId = 0;
        const wchar_t* kCandidateTags[] = {
                STR("Goat_Saddlebags"),    // 8x8 cargo (present once a pack is in the epic slot)
                STR("Goat.Slot.EpicPack"), // 1x1 epic-pack slot (Tobi v1.11.0 DefaultContainers)
                STR("Inventory.BodyInventory"),
                STR("BodyInventory"),
                STR("Goat.BodyInventory"),
                STR("Dwarf.BodyInventoryGOAT"), // our legacy name (last resort)
        };
        if (auto* getByTagFn = goatInv->GetFunctionByNameInChain(STR("GetContainerByTag")))
        {
            auto* pIn = findParam(getByTagFn, STR("Tag"));
            if (!pIn) pIn = findParam(getByTagFn, STR("ContainerTag"));
            auto* pRet = findParam(getByTagFn, STR("ReturnValue"));
            int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
            int rOff = pRet ? pRet->GetOffset_Internal() : 8;
            int sz = getByTagFn->GetParmsSize();
            for (const wchar_t* cand : kCandidateTags)
            {
                std::vector<uint8_t> tb(sz, 0);
                RC::Unreal::FName bagTag(cand, RC::Unreal::FNAME_Add);
                std::memcpy(tb.data() + tagOff, &bagTag, sizeof(RC::Unreal::FName));
                try
                {
                    safeProcessEvent(goatInv, getByTagFn, tb.data());
                }
                catch (...)
                {
                }
                int32_t id = *reinterpret_cast<int32_t*>(tb.data() + rOff);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.72] GetContainerByTag('{}') -> id={}\n"), cand, id);
                if (id != 0)
                {
                    std::memcpy(bagHandle, tb.data() + rOff, 20);
                    bagId = id;
                    RC::Unreal::FWeakObjectPtr ownerWP(goatInv);
                    std::memcpy(bagHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
                    break;
                }
            }
        }
        else
        {
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.72] GetContainerByTag missing on goat InvComp\n"));
        }

        // [rc.79 2026-07-03] Tag lookup failed but a container exists
        // (rc.78 injection -> HasContainers=true). Enumerate GetContainers
        // and take the first container's handle directly. Log the element
        // bytes so we can see the handle layout.
        if (bagId == 0)
        {
            if (auto* gcFn = goatInv->GetFunctionByNameInChain(STR("GetContainers")))
            {
                std::vector<uint8_t> gb(gcFn->GetParmsSize(), 0);
                try
                {
                    safeProcessEvent(goatInv, gcFn, gb.data());
                }
                catch (...)
                {
                }
                if (auto* pRet = findParam(gcFn, STR("ReturnValue")))
                {
                    uint8_t* arr = gb.data() + pRet->GetOffset_Internal();
                    uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
                    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.79] GetContainers -> {} container(s)\n"), num);
                    if (data && num > 0)
                    {
                        uint8_t* e0 = data;
                        int32_t id0 = *reinterpret_cast<int32_t*>(e0);
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.79] elem0 id={} b0..3={:02x}{:02x}{:02x}{:02x} b8..11={:02x}{:02x}{:02x}{:02x}\n"),
                             id0,
                             e0[0],
                             e0[1],
                             e0[2],
                             e0[3],
                             e0[8],
                             e0[9],
                             e0[10],
                             e0[11]);
                        if (id0 != 0)
                        {
                            std::memcpy(bagHandle, e0, 20);
                            bagId = id0;
                            RC::Unreal::FWeakObjectPtr ownerWP(goatInv);
                            std::memcpy(bagHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.79] using container id={} from GetContainers -> opening\n"), bagId);
                        }
                    }
                }
            }
            else
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.79] GetContainers missing on goat InvComp\n"));
        }

        if (bagId == 0)
        {
            // [rc.91 FULL FN ENUM 2026-07-07] Tobi's v1.11.0 added
            // DefaultContainers (Num=1) but nothing instantiates it
            // (HasContainers=false). His BP_PorterGoatPersistor is a
            // TimeManager clone with NO inventory code, and neither
            // GA_Bell nor BP_NPCManager creates containers — so no BP
            // runs the DefaultContainers processing on our goat. We must
            // call the instantiation function ourselves. Enumerate EVERY
            // function on the MorInventoryComponent so we can see the real
            // create/add/init-container API (stop guessing 4 names).
            {
                UClass* icls = nullptr;
                try
                {
                    icls = goatInv->GetClassPrivate();
                }
                catch (...)
                {
                }
                int total = 0, interesting = 0;
                if (icls)
                {
                    try
                    {
                        for (auto* fn : icls->ForEachFunctionInChain())
                        {
                            if (!fn) continue;
                            std::wstring fnn;
                            try
                            {
                                fnn = fn->GetName();
                            }
                            catch (...)
                            {
                                continue;
                            }
                            total++;
                            // Log ALL names compactly; expand params only for
                            // names hinting at container creation/mutation.
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM] {} (parm={})\n"), fnn.c_str(), fn->GetParmsSize());
                            bool hot = fnn.find(STR("Container")) != std::wstring::npos || fnn.find(STR("Init")) != std::wstring::npos ||
                                       fnn.find(STR("Create")) != std::wstring::npos || fnn.find(STR("Add")) != std::wstring::npos ||
                                       fnn.find(STR("Give")) != std::wstring::npos || fnn.find(STR("Setup")) != std::wstring::npos ||
                                       fnn.find(STR("Default")) != std::wstring::npos || fnn.find(STR("Spawn")) != std::wstring::npos ||
                                       fnn.find(STR("Loadout")) != std::wstring::npos || fnn.find(STR("Register")) != std::wstring::npos ||
                                       fnn.find(STR("Populate")) != std::wstring::npos;
                            if (hot)
                            {
                                interesting++;
                                for (auto* p : fn->ForEachProperty())
                                {
                                    if (!p) continue;
                                    std::wstring pn;
                                    try
                                    {
                                        pn = p->GetName();
                                    }
                                    catch (...)
                                    {
                                    }
                                    std::wstring pt;
                                    try
                                    {
                                        pt = p->GetClass().GetName();
                                    }
                                    catch (...)
                                    {
                                    }
                                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM]      * param '{}' type={} off={} size={}\n"),
                                         pn.c_str(),
                                         pt.c_str(),
                                         p->GetOffset_Internal(),
                                         p->GetSize());
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM] component class='{}' total functions={} hot={}\n"),
                     icls ? icls->GetName().c_str() : STR("(null)"),
                     total,
                     interesting);
            }
            if (auto* shProp = goatInv->GetPropertyByNameInChain(STR("StorageHandle")))
            {
                uint8_t* sh = reinterpret_cast<uint8_t*>(goatInv) + shProp->GetOffset_Internal();
                // MorStorageRowHandle = { UDataTable* (8) ; FName RowName (@+8) }
                RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(sh + 8);
                try
                {
                    std::wstring rnStr = rn->ToString();
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [DISCOVERY] StorageHandle.RowName='{}'\n"), rnStr.c_str());
                }
                catch (...)
                {
                }
            }
            if (auto* itemsProp = goatInv->GetPropertyByNameInChain(STR("Items")))
            {
                int itemsOff = itemsProp->GetOffset_Internal();
                uint8_t* listBase = reinterpret_cast<uint8_t*>(goatInv) + itemsOff + iiaListOff();
                if (isReadableMemory(listBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                    int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
                    int stride = iiSize(), itemOff = iiItemOff(), idOff = iiIDOff();
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [DISCOVERY] goat inventory Items={}\n"), arrNum);
                    if (arrData && arrNum > 0 && arrNum < 2000)
                    {
                        for (int32_t i = 0; i < arrNum; i++)
                        {
                            uint8_t* entry = arrData + i * stride;
                            if (!isReadableMemory(entry, stride)) continue;
                            UClass* ic = *reinterpret_cast<UClass**>(entry + itemOff);
                            int32_t id = *reinterpret_cast<int32_t*>(entry + idOff);
                            int32_t slot = *reinterpret_cast<int32_t*>(entry + iiSlotOff());
                            int32_t cs = *reinterpret_cast<int32_t*>(entry + iiContainerStartOff());
                            std::wstring nm = ic ? ic->GetName() : STR("(null)");
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [DISCOVERY]   [{}] id={} slot={} containerStart={} class={}\n"), i, id, slot, cs, nm.c_str());
                        }
                    }
                }
            }
            showOnScreen(L"Goat storage not found - see log DISCOVERY dump", 3.5f, 1.0f, 0.7f, 0.3f);
            return;
        }

        // Container exists! Open StorageMode widget bound to it
        openStorageWidgetForHandle(goat, goatInv, bagHandle);
        return;
    }
}

// [rc.5 2026-05-22] Tick-driven close for the goat saddlebag widget.
// Polled from gameThreadTick whenever m_goatSaddlebagWidget is
// non-null. Watches Esc + Tab (both are standard inventory-close
// keys in Moria's UI). Uses GetAsyncKeyState which reads OS-level
// state regardless of UE input mode, so this works even when the
// widget has captured focus.
void tickGoatSaddlebagWidget()
{
    if (!m_goatSaddlebagWidget) return;
    if (!isObjectAlive(m_goatSaddlebagWidget))
    {
        // Stale handle (widget GC'd from under us). Reset state.
        m_goatSaddlebagWidget = nullptr;
        setInputModeGame();
        return;
    }
    // The game's UI manager pops input back to GameOnly whenever its
    // own screen stack is empty — our screen isn't registered with
    // it, so a single SetInputMode gets reverted. Re-assert at 10 Hz
    // while open (event-based reverts lose to a periodic re-assert).
    // See memory goat-final-architecture → "input mode".
    {
        static ULONGLONG s_lastInputAssertMs = 0;
        static int s_inputAssertLogCounter = 0;
        ULONGLONG inow = GetTickCount64();
        // Never re-assert while LMB is down — SetInputMode_UIOnlyEx
        // refocuses the widget, which would cancel an active drag.
        bool lmbHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!lmbHeld && inow - s_lastInputAssertMs >= 100)
        {
            s_lastInputAssertMs = inow;
            setInputModeUI(m_goatSaddlebagWidget);
            if ((s_inputAssertLogCounter++ % 50) == 0) VLOG(STR("[MoriaCppMod] [GoatSaddle] [v8.2.x] input-mode UI enforcement active (10 Hz while open)\n"));
        }
    }
    // [rc.107 2026-07-10] The screen's own logic re-asserts the
    // "non-storage" view every frame (rc.106 proof: our SetVisibility
    // wins the call, loses the next tick). While OUR widget is open,
    // re-assert the storage view-state at 4 Hz — the same three values
    // captured from the working native screen.
    {
        static ULONGLONG s_lastVisAssert = 0;
        ULONGLONG now = GetTickCount64();
        if (now - s_lastVisAssert >= 250)
        {
            s_lastVisAssert = now;
            auto setVis = [&](const wchar_t* childName, uint8_t v) {
                UObject* c = jw_findChildInTree(m_goatSaddlebagWidget, childName);
                if (!c || !isObjectAlive(c)) return;
                if (auto* f = cachedFnInChain(c, STR("SetVisibility")))
                {
                    std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                    vb[0] = v;
                    try
                    {
                        safeProcessEvent(c, f, vb.data());
                    }
                    catch (...)
                    {
                    }
                }
            };
            setVis(STR("storageOverlay"), 4);
            setVis(STR("Background"), 1);
            setVis(STR("BackgroundWStorage"), 0);
            // [rc.108] keep the NPC placeholder banner collapsed too
            setVis(STR("NPCTitleCluster"), 1);
            setVis(STR("SkillPanel"), 1);
            setVis(STR("NPCDetailsBox"), 1);

            // [rc.110/111] detect broken pane states: (a) a
            // WBP_UI_Inventory_NPC_C (dwarf 4×3) got rebuilt into the
            // pane by drag/interaction handlers, or (b) the pane got
            // cleared/hidden (RootGrid no longer Visible). Either way,
            // RE-DRIVE the saddlebag grid (rc.111) — collapse alone left
            // the pane empty ("8×8 disappears when grabbing an item").
            {
                int found = 0;
                std::function<void(UObject*, int)> hunt = [&](UObject* ww, int depth) {
                    if (!ww || !isObjectAlive(ww) || depth > 14 || found > 4) return;
                    if (safeClassName(ww) == STR("WBP_UI_Inventory_NPC_C"))
                    {
                        if (auto* f = cachedFnInChain(ww, STR("SetVisibility")))
                        {
                            std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                            vb[0] = 1; // Collapsed
                            try
                            {
                                safeProcessEvent(ww, f, vb.data());
                            }
                            catch (...)
                            {
                            }
                        }
                        found++;
                        return;
                    }
                    if (auto* gcc = cachedFnInChain(ww, STR("GetChildrenCount")))
                    {
                        std::vector<uint8_t> b(gcc->GetParmsSize(), 0);
                        try
                        {
                            safeProcessEvent(ww, gcc, b.data());
                        }
                        catch (...)
                        {
                        }
                        int32_t n = 0;
                        if (auto* pr = findParam(gcc, STR("ReturnValue"))) n = *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal());
                        if (auto* gca = cachedFnInChain(ww, STR("GetChildAt")))
                        {
                            for (int32_t i = 0; i < n && i < 32; i++)
                            {
                                std::vector<uint8_t> cb(gca->GetParmsSize(), 0);
                                if (auto* pi = findParam(gca, STR("Index"))) *reinterpret_cast<int32_t*>(cb.data() + pi->GetOffset_Internal()) = i;
                                try
                                {
                                    safeProcessEvent(ww, gca, cb.data());
                                }
                                catch (...)
                                {
                                }
                                UObject* child = nullptr;
                                if (auto* pr2 = findParam(gca, STR("ReturnValue"))) child = *reinterpret_cast<UObject**>(cb.data() + pr2->GetOffset_Internal());
                                if (child) hunt(child, depth + 1);
                            }
                        }
                    }
                    if (auto* wt = ww->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree")))
                        if (*wt)
                            if (auto* rp = (*wt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget")))
                                if (*rp) hunt(*rp, depth + 1);
                };
                hunt(m_goatSaddlebagWidget, 0);

                // [rc.111] pane-health check: RootGrid must be Visible(0).
                bool paneBroken = (found > 0);
                if (!paneBroken)
                {
                    if (UObject* rg = jw_findChildInTree(m_goatSaddlebagWidget, STR("RootGrid")))
                    {
                        if (auto* v = rg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                            if (*v == 1 || *v == 2) // Collapsed / Hidden
                                paneBroken = true;
                    }
                }
                if (paneBroken && m_sbGoatInvCache && isObjectAlive(m_sbGoatInvCache))
                {
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.111] pane broken (npcWidget={} ) — re-driving saddlebag grid\n"), found);
                    driveSaddlebagStorageContainer(m_goatSaddlebagWidget, m_sbGoatInvCache, m_sbPlayerInvCache, m_sbHandleCache, STR("rc.111 heal"));
                }
            }
        }
    }

    // [rc.109] one-shot delayed re-drive of the saddlebag grid — wins
    // against any late NPC-path rebuild of the container pane.
    if (m_contReSetupAtMs != 0 && GetTickCount64() >= m_contReSetupAtMs)
    {
        m_contReSetupAtMs = 0;
        if (m_sbGoatInvCache && isObjectAlive(m_sbGoatInvCache))
            driveSaddlebagStorageContainer(m_goatSaddlebagWidget, m_sbGoatInvCache, m_sbPlayerInvCache, m_sbHandleCache, STR("rc.109 re-drive"));
    }

    // Edge-trigger Esc OR Tab.
    static bool s_lastEsc = false;
    static bool s_lastTab = false;
    bool eDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool tDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    bool fire = (eDown && !s_lastEsc) || (tDown && !s_lastTab);
    s_lastEsc = eDown;
    s_lastTab = tDown;
    // [rc.130] grace period: a reflexive Esc aimed at the (about-to-be-
    // suppressed) vanilla screen must not kill OUR screen underneath.
    if (fire && GetTickCount64() - m_sbWidgetOpenMs < 800)
    {
        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.130] Esc/Tab within grace window - ignored\n"));
        fire = false;
    }
    if (fire)
    {
        VLOG(STR("[MoriaCppMod] [GoatSaddle] Esc/Tab pressed — closing widget\n"));
        closeGoatSaddlebagInventory();
    }
}

// [v7.2.0-rc.4 2026-05-22] Close handler for the goat saddlebag widget.
// Invoked by the toggle path in openGoatSaddlebagInventory (E-press
// dismisses), the tick-driven Esc/Tab handler, and (future) goat
// despawn hook.
void closeGoatSaddlebagInventory()
{
    if (!m_goatSaddlebagWidget) return;
    if (isObjectAlive(m_goatSaddlebagWidget))
    {
        if (auto* fn = m_goatSaddlebagWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
        {
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            try
            {
                safeProcessEvent(m_goatSaddlebagWidget, fn, b.data());
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] RemoveFromParent fired on widget={:p}\n"), (void*)m_goatSaddlebagWidget);
        }
    }
    m_goatSaddlebagWidget = nullptr;
    setInputModeGame();
    VLOG(STR("[MoriaCppMod] [GoatSaddle] widget closed + input mode Game\n"));
}

// Button tracking — used by the PE-pre click filter to route clicks
// to the right handler.
UObject* m_goatBtnStay{nullptr};
UObject* m_goatBtnFollow{nullptr};
UObject* m_goatBtnWander{nullptr};
UObject* m_goatBtnDismiss{nullptr};
UObject* m_goatBtnInv{nullptr};
UObject* m_goatBtnFeed{nullptr};
UObject* m_goatBtnRename{nullptr};
UObject* m_goatHeader{nullptr}; // status text — updated per show

// [Phase 3 PATH A] Vanilla-menu row injection state.
FWeakObjectPtr m_pendingInteractMenu{};
bool m_goatInjectPending{false};
bool m_goatInjectLogged{false};

// One injected row per action. Index encodes which handler fires on press.
enum class GoatRowAction : int
{
    Follow = 0,
    Stay,
    Wander,
    Saddlebags,
    Feed,
    Rename,
    Dismiss,
    Count_
};
static constexpr int kGoatRowCount = static_cast<int>(GoatRowAction::Count_);
FWeakObjectPtr m_goatRows[kGoatRowCount]{};

// Probe: per-tick log of vanilla's selection cursor while goat menu
// is open. Helps verify Q1-Q3 from the Desktop recon.
UObject* m_goatProbeLastCursor{nullptr};
ULONGLONG m_goatProbeLastMs{0};

// (own-cursor refactor reverted — using vanilla's cursor again)

// PE-post: menu's OnShow just fired — by now SetNameText has run,
// GetInteractionWidgetsContainer was queried, and at least one row
// is in the container. Inspect the first row's Interactable (off
// 0x260 on UI_WBP_Interaction_C) to identify the NPC; if it's our
// goat, flag pending injection.
void onInteractMenuShownPost(UObject* menu)
{
    VLOG(STR("[MoriaCppMod] [GoatInject] OnShow PE-post menu={:p}\n"), (void*)menu);
    if (!menu || !isObjectAlive(menu) || m_followGoats.empty()) return;

    // Resolve the row container.
    UObject* container = nullptr;
    if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
    {
        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(menu, fn, buf.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) container = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
    }
    if (!container || !isObjectAlive(container)) return;

    // First child's Interactable tells us which NPC the menu is for.
    int32_t count = 0;
    if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount")))
    {
        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(container, fn, buf.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) count = *reinterpret_cast<int32_t*>(buf.data() + pRet->GetOffset_Internal());
    }
    if (count <= 0) return;

    UObject* firstRow = nullptr;
    if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildAt")))
    {
        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
        if (auto* p = findParam(fn, STR("Index"))) *reinterpret_cast<int32_t*>(buf.data() + p->GetOffset_Internal()) = 0;
        try
        {
            safeProcessEvent(container, fn, buf.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) firstRow = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
    }
    if (!firstRow || !isObjectAlive(firstRow)) return;

    auto* npcCompPtr = firstRow->GetValuePtrByPropertyNameInChain<UObject*>(STR("Interactable"));
    if (!npcCompPtr || !isReadableMemory(npcCompPtr, sizeof(UObject*))) return;
    UObject* npcComp = *npcCompPtr;
    if (!npcComp || !isObjectAlive(npcComp)) return;

    UObject* owner = nullptr;
    try
    {
        owner = npcComp->GetOuterPrivate();
    }
    catch (...)
    {
    }
    if (!owner || !isObjectAlive(owner)) return;

    std::wstring ownerCls;
    try
    {
        ownerCls = owner->GetClassPrivate()->GetName();
    }
    catch (...)
    {
    }
    bool ours = false;
    for (auto& g : m_followGoats)
    {
        UObject* mine = g.pawn.Get();
        if (mine && mine == owner)
        {
            ours = true;
            break;
        }
    }
    VLOG(STR("[MoriaCppMod] [GoatHeader] OnShow inspect owner={:p} cls='{}' ours={}\n"), (void*)owner, ownerCls, ours);
    if (!ours) return;

    // Capture the proximity menu pointer here (OnShow PE-post,
    // goat menus only) so later hide/collapse logic has a target.
    m_pendingInteractMenu = FWeakObjectPtr(menu);
    VLOG(STR("[MoriaCppMod] [GoatInject] captured proximity menu={:p} into m_pendingInteractMenu\n"), (void*)menu);

    // [Phase 5] Header override.
    // The menu displays "Name ◆ Role" — name comes from SetNameText,
    // role is queried from CurrentRole.DisplayName. SetRole(Porter)
    // either failed or the menu cached the role before we set it.
    // Override BOTH text elements directly: SetNameText for the name
    // half, and a widget-tree walk to find any TextBlock containing
    // "Citizen" / role text and override it too.
    const wchar_t* name = m_goatName.empty() ? STR("Porter Goat") : m_goatName.c_str();
    overrideMenuNameText(menu, name);
    overrideRoleTextInMenuTree(menu, STR("Porter Goat"));
    VLOG(STR("[MoriaCppMod] [GoatHeader] menu={:p} renamed name='{}' + role override fired\n"), (void*)menu, name);
}

// Walk the menu's widget tree depth-first, find any TextBlock whose
// current text is one of the vanilla role names ("Citizen", default
// role display), and overwrite it with replacement.
void overrideRoleTextInMenuTree(UObject* menu, const wchar_t* replacement)
{
    if (!menu || !isObjectAlive(menu)) return;
    auto* wtPtr = menu->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
    UObject* widgetTree = wtPtr ? *wtPtr : nullptr;
    if (!widgetTree) return;
    auto* rootPtr = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
    UObject* root = rootPtr ? *rootPtr : nullptr;
    if (!root) return;

    // Read a TextBlock's current text via GetText UFunction. Returns
    // empty if not a TextBlock or read fails.
    auto readText = [](UObject* w) -> std::wstring {
        if (!w || !isObjectAlive(w)) return STR("");
        auto* fn = w->GetFunctionByNameInChain(STR("GetText"));
        if (!fn) return STR("");
        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(w, fn, buf.data());
        }
        catch (...)
        {
            return STR("");
        }
        auto* pRet = findParam(fn, STR("ReturnValue"));
        if (!pRet) return STR("");
        FText* t = reinterpret_cast<FText*>(buf.data() + pRet->GetOffset_Internal());
        if (!t) return STR("");
        try
        {
            return std::wstring(t->ToString());
        }
        catch (...)
        {
            return STR("");
        }
    };

    // Vanilla role names that should be replaced.
    auto shouldOverride = [](const std::wstring& s) {
        if (s.empty()) return false;
        static const wchar_t* kRoleStrs[] = {
                STR("Citizen"),
                STR("Wanderer"),
                STR("Recruit"),
                STR("Survivor"),
                STR("Porter") // already-Porter still gets the explicit "Porter Goat" override
        };
        for (auto* r : kRoleStrs)
            if (s == r) return true;
        return false;
    };

    static bool s_dumped = false;
    bool dumpNow = !s_dumped;
    int hits = 0;
    std::function<void(UObject*)> walk = [&](UObject* w) {
        if (!w || !isObjectAlive(w)) return;
        std::wstring cls;
        try
        {
            cls = w->GetClassPrivate()->GetName();
        }
        catch (...)
        {
        }
        if (cls == STR("TextBlock"))
        {
            std::wstring cur = readText(w);
            std::wstring wname;
            try
            {
                wname = std::wstring(w->GetNamePrivate().ToString());
            }
            catch (...)
            {
            }
            if (dumpNow)
            {
                VLOG(STR("[MoriaCppMod] [GoatHeader] DUMP TextBlock {:p} name='{}' text='{}'\n"), (void*)w, wname, cur);
            }
            if (shouldOverride(cur))
            {
                umgSetText(w, std::wstring(replacement));
                VLOG(STR("[MoriaCppMod] [GoatHeader] role-text override on {:p} name='{}': '{}' -> '{}'\n"), (void*)w, wname, cur, replacement);
                hits++;
            }
        }
        auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
        if (slots)
        {
            for (int i = 0; i < slots->Num(); ++i)
            {
                UObject* s = (*slots)[i];
                if (!s || !isObjectAlive(s)) continue;
                auto* c = s->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (c && *c) walk(*c);
            }
        }
        auto* singleC = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
        if (singleC && *singleC) walk(*singleC);
        auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
        if (nestedWt && *nestedWt)
        {
            auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
            if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot);
        }
    };
    walk(root);
    if (dumpNow) s_dumped = true;
    VLOG(STR("[MoriaCppMod] [GoatHeader] role-text walk: {} overrides applied (dump={})\n"), hits, dumpNow ? STR("YES") : STR("no"));
}

// Strip every injected row from its parent so vanilla's pool sees a
// clean container on the next NPC. Idempotent.
void clearGoatInjectedRows()
{
    for (int i = 0; i < kGoatRowCount; ++i)
    {
        UObject* row = m_goatRows[i].Get();
        if (row && isObjectAlive(row))
        {
            if (auto* fn = row->GetFunctionByNameInChain(STR("RemoveFromParent")))
            {
                try
                {
                    safeProcessEvent(row, fn, nullptr);
                }
                catch (...)
                {
                }
            }
        }
        m_goatRows[i] = FWeakObjectPtr();
    }
}

// Returns the action index (0..kGoatRowCount-1) if widget is one of
// our injected rows, else -1.
int findGoatRowIndex(UObject* widget) const
{
    if (!widget) return -1;
    for (int i = 0; i < kGoatRowCount; ++i)
    {
        if (m_goatRows[i].Get() == widget) return i;
    }
    return -1;
}

// Call MorNPCComponent::SetRole(FMorNPCRoleRowHandle{DT_NPCRoles, rowName}).
// No-ops gracefully if the row is disabled (per Desktop confirmation).
// [Phase 5 probe] Read back the current role from MorNPCComponent
// after SetRole. Confirms whether the write propagated. Possible
// outcomes per Desktop diag:
//   Row=Porter  → SetRole worked; menu reads NameText from
//                 elsewhere (CharacterData.Name / actor.DisplayName).
//   Row=Default → SetRole silent no-op'd, likely tag-check failure.
//   Row=None    → role is unset entirely.
void probeCurrentRole(UObject* npcComp)
{
    if (!npcComp || !isObjectAlive(npcComp)) return;
    auto* fn = npcComp->GetFunctionByNameInChain(STR("GetCurrentRole"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [RoleProbe] GetCurrentRole UFunction not found on npcComp={:p}\n"), (void*)npcComp);
        return;
    }
    std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
    try
    {
        safeProcessEvent(npcComp, fn, buf.data());
    }
    catch (...)
    {
        VLOG(STR("[MoriaCppMod] [RoleProbe] GetCurrentRole PE exception\n"));
        return;
    }
    // Find the first StructProperty parameter (return value or out-param).
    FProperty* outParam = nullptr;
    for (auto* prop : fn->ForEachProperty())
    {
        if (!prop) continue;
        bool isParm = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80));
        if (!isParm) continue;
        std::wstring cls;
        try
        {
            cls = prop->GetClass().GetName();
        }
        catch (...)
        {
        }
        if (cls == STR("StructProperty"))
        {
            outParam = prop;
            break;
        }
    }
    if (!outParam)
    {
        VLOG(STR("[MoriaCppMod] [RoleProbe] no StructProperty out-param on GetCurrentRole\n"));
        return;
    }
    uint8_t* handle = buf.data() + outParam->GetOffset_Internal();
    UObject* dt = *reinterpret_cast<UObject**>(handle + 0);
    FName rowFName = *reinterpret_cast<FName*>(handle + 8);
    std::wstring dtName, rowStr;
    if (dt && isObjectAlive(dt))
    {
        try
        {
            dtName = dt->GetName();
        }
        catch (...)
        {
        }
    }
    try
    {
        rowStr = rowFName.ToString();
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [RoleProbe] After SetRole: DT={:p} ('{}'), Row='{}'\n"), (void*)dt, dtName, rowStr);
}

void setGoatRole(UObject* npcComp, const wchar_t* rowName)
{
    if (!npcComp || !isObjectAlive(npcComp) || !rowName) return;
    auto* setRoleFn = npcComp->GetFunctionByNameInChain(STR("SetRole"));
    if (!setRoleFn)
    {
        VLOG(STR("[MoriaCppMod] [SetRole] UFunction not found on npcComp={:p}\n"), (void*)npcComp);
        return;
    }
    // Find the StructProperty parameter regardless of name. SetRole
    // takes one struct (FMorNPCRoleRowHandle) and probably no other
    // params, so we just look for the first StructProperty.
    FProperty* handleParam = nullptr;
    std::wstring handleParamName;
    {
        static bool s_dumped = false;
        if (!s_dumped)
        {
            VLOG(STR("[MoriaCppMod] [SetRole] enumerating SetRole params (parmSize={}):\n"), setRoleFn->GetParmsSize());
        }
        for (auto* prop : setRoleFn->ForEachProperty())
        {
            if (!prop) continue;
            bool isParam = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80 /*CPF_Parm*/));
            if (!isParam) continue;
            std::wstring n;
            try
            {
                n = std::wstring(prop->GetName());
            }
            catch (...)
            {
            }
            std::wstring cls;
            try
            {
                cls = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            if (!s_dumped)
            {
                VLOG(STR("[MoriaCppMod] [SetRole]   param '{}' cls='{}' off=0x{:X}\n"), n, cls, prop->GetOffset_Internal());
            }
            if (!handleParam && cls == STR("StructProperty"))
            {
                handleParam = prop;
                handleParamName = n;
            }
        }
        s_dumped = true;
    }
    if (!handleParam)
    {
        VLOG(STR("[MoriaCppMod] [SetRole] no StructProperty param found — abort\n"));
        return;
    }
    auto* dt = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Game/Character/NpcDwarf/DT_NPCRoles.DT_NPCRoles"));
    std::vector<uint8_t> buf(setRoleFn->GetParmsSize(), 0);
    // FMorNPCRoleRowHandle = { UDataTable* DataTable; FName RowName; }
    uint8_t* handle = buf.data() + handleParam->GetOffset_Internal();
    *reinterpret_cast<UObject**>(handle + 0) = dt;
    FName rowFName(rowName);
    *reinterpret_cast<FName*>(handle + 8) = rowFName;
    try
    {
        safeProcessEvent(npcComp, setRoleFn, buf.data());
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [SetRole] SetRole(param='{}', DT={:p}, RowName='{}') fired on npcComp={:p}\n"), handleParamName, (void*)dt, rowName, (void*)npcComp);

    // [rc.62 ENHANCED DIAG 2026-05-21]
    //
    // rc.61 confirmed SetRole is being called with valid DT+RowName but
    // GetCurrentRole returns null. Hypothesis: SetRole's C++ impl
    // validates against AMorNPCManager.ValidNpcRoles (empty per ProbeB
    // — Num=0), silently rejecting any role assignment.
    //
    // Three diagnostic + fallback steps:
    //   1) One-shot dump of MorNPCComponent's UPROPERTYs to locate the
    //      CurrentRole field offset (so we can later direct-write).
    //   2) Direct-read CurrentRole UPROPERTY bytes after SetRole. If
    //      these are non-null, SetRole wrote but GetCurrentRole reads
    //      differently. If null, SetRole truly rejected.
    //   3) SetRoleFuzzy fallback — invokes the alternative C++ path
    //      (different validation rules per earlier code's existence).

    // (1) One-shot UPROPERTY dump on MorNPCComponent.
    {
        static bool s_dumpedNpcCompProps = false;
        if (!s_dumpedNpcCompProps)
        {
            s_dumpedNpcCompProps = true;
            RC::Unreal::UClass* nc = nullptr;
            try
            {
                nc = npcComp->GetClassPrivate();
            }
            catch (...)
            {
            }
            if (nc)
            {
                VLOG(STR("[MoriaCppMod] [SetRoleDiag] MorNPCComponent UPROPERTYs:\n"));
                int n = 0;
                try
                {
                    for (auto* prop : nc->ForEachProperty())
                    {
                        std::wstring pname, ptype;
                        try
                        {
                            pname = prop->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            ptype = prop->GetClass().GetName();
                        }
                        catch (...)
                        {
                        }
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag]   .{} : {} @ +0x{:x}\n"),
                             pname.empty() ? STR("?") : pname.c_str(),
                             ptype.empty() ? STR("?") : ptype.c_str(),
                             prop->GetOffset_Internal());
                        if (++n > 48)
                        {
                            VLOG(STR("[MoriaCppMod] [SetRoleDiag]   ...(truncated)\n"));
                            break;
                        }
                    }
                }
                catch (...)
                {
                }
            }
        }
    }

    // (2) Direct-read CurrentRole UPROPERTY immediately after SetRole.
    // CurrentRole on MorNPCComponent is a FMorNPCRoleRowHandle struct
    // (16 bytes: UDataTable* + FName).
    try
    {
        auto* curRolePtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentRole"));
        if (curRolePtr)
        {
            UObject* dtAfter = *reinterpret_cast<UObject**>(curRolePtr + 0);
            std::wstring rowAfter = seh_fnameToString(curRolePtr + 8);
            VLOG(STR("[MoriaCppMod] [SetRoleDiag] Direct read CurrentRole: DT={:p} Row='{}'\n"), (void*)dtAfter, rowAfter.empty() ? STR("None") : rowAfter.c_str());
        }
        else
        {
            VLOG(STR("[MoriaCppMod] [SetRoleDiag] CurrentRole UPROPERTY not found on npcComp\n"));
        }
    }
    catch (...)
    {
        VLOG(STR("[MoriaCppMod] [SetRoleDiag] Direct CurrentRole read threw\n"));
    }

    // (3) SetRoleFuzzy fallback if SetRole silently no-op'd.
    // [rc.63 ENHANCED] rc.62 reported "no FName param" — SetRoleFuzzy
    // doesn't take FName directly. Dump full param list to identify
    // signature, then try Struct fallback (likely FMorNPCRoleRowHandle
    // same as SetRole) AND FString fallback.
    {
        auto* setFuzzyFn = npcComp->GetFunctionByNameInChain(STR("SetRoleFuzzy"));
        if (setFuzzyFn)
        {
            VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy enum params (parmSize={}):\n"), setFuzzyFn->GetParmsSize());
            FProperty* structParam = nullptr;
            FProperty* strParam = nullptr;
            FProperty* nameParam = nullptr;
            for (auto* prop : setFuzzyFn->ForEachProperty())
            {
                if (!prop) continue;
                bool isParam = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80));
                if (!isParam) continue;
                std::wstring pname, ptype;
                try
                {
                    pname = prop->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    ptype = prop->GetClass().GetName();
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [SetRoleDiag]   fuzzy param '{}' cls='{}' off=0x{:X}\n"), pname, ptype, prop->GetOffset_Internal());
                if (!structParam && ptype == STR("StructProperty")) structParam = prop;
                if (!strParam && ptype == STR("StrProperty")) strParam = prop;
                if (!nameParam && ptype == STR("NameProperty")) nameParam = prop;
            }
            std::vector<uint8_t> fbuf(setFuzzyFn->GetParmsSize(), 0);
            bool fired = false;
            if (structParam)
            {
                // Try as FMorNPCRoleRowHandle (DT + RowName).
                uint8_t* h = fbuf.data() + structParam->GetOffset_Internal();
                *reinterpret_cast<UObject**>(h + 0) = dt;
                *reinterpret_cast<FName*>(h + 8) = FName(rowName);
                try
                {
                    safeProcessEvent(npcComp, setFuzzyFn, fbuf.data());
                    fired = true;
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with Struct payload (DT+Name)\n"));
            }
            else if (strParam)
            {
                FString fs(rowName);
                *reinterpret_cast<FString*>(fbuf.data() + strParam->GetOffset_Internal()) = fs;
                try
                {
                    safeProcessEvent(npcComp, setFuzzyFn, fbuf.data());
                    fired = true;
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with FString payload\n"));
            }
            else if (nameParam)
            {
                *reinterpret_cast<FName*>(fbuf.data() + nameParam->GetOffset_Internal()) = FName(rowName);
                try
                {
                    safeProcessEvent(npcComp, setFuzzyFn, fbuf.data());
                    fired = true;
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with FName payload\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy: no recognized param type — abandoned\n"));
            }
            // (3b) Read GetCurrentRole UFunction back if we fired.
            // Direct CurrentRole UPROPERTY isn't on the component
            // (rc.62 finding) — the role lives elsewhere (likely
            // AMorNPCManager.NpcInfo entry which we don't have for
            // this goat). So this readback may still show 'None'.
            if (fired)
            {
                probeCurrentRole(npcComp);
            }
        }
        else
        {
            VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy UFunction not on MorNPCComponent\n"));
        }
    }
}

// Helper: get child at index from the InteractionMenu's container.
UObject* goatMenuGetChildAt(int idx)
{
    UObject* menu = m_pendingInteractMenu.Get();
    if (!menu || !isObjectAlive(menu)) return nullptr;
    UObject* container = nullptr;
    if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
    {
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(menu, fn, b.data());
        }
        catch (...)
        {
            return nullptr;
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) container = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
    }
    if (!container || !isObjectAlive(container)) return nullptr;
    auto* fn = container->GetFunctionByNameInChain(STR("GetChildAt"));
    if (!fn) return nullptr;
    auto* pIdx = findParam(fn, STR("Index"));
    auto* pRet = findParam(fn, STR("ReturnValue"));
    if (!pIdx || !pRet) return nullptr;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    *reinterpret_cast<int32_t*>(b.data() + pIdx->GetOffset_Internal()) = idx;
    try
    {
        safeProcessEvent(container, fn, b.data());
    }
    catch (...)
    {
        return nullptr;
    }
    return *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
}

int goatMenuChildCount()
{
    UObject* menu = m_pendingInteractMenu.Get();
    if (!menu || !isObjectAlive(menu)) return 0;
    UObject* container = nullptr;
    if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
    {
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(menu, fn, b.data());
        }
        catch (...)
        {
            return 0;
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) container = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
    }
    if (!container || !isObjectAlive(container)) return 0;
    auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount"));
    if (!fn) return 0;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    try
    {
        safeProcessEvent(container, fn, b.data());
    }
    catch (...)
    {
        return 0;
    }
    if (auto* pRet = findParam(fn, STR("ReturnValue"))) return *reinterpret_cast<int32_t*>(b.data() + pRet->GetOffset_Internal());
    return 0;
}

// [Phase 3 keystone] Find the row's MorCursorSlotComponent and
// call RegisterComponent() on it. This is the missing step that
// makes vanilla's cursor subsystem aware of our cloned row.
// Per Desktop recon: cursor walks REGISTERED MorCursorSlotComponent
// instances, not container.Children. WidgetBlueprintLibrary::Create
// duplicates the component subobject but doesn't register it.
// Brute-force: scan every live UObject for any MorCursorSlotComponent
// whose outer chain includes the given row. If vanilla creates the
// cursor slot as a SubObject (not a UPROPERTY), this finds it.
UObject* findCursorSlotByOuterChain(UObject* row)
{
    if (!row) return nullptr;
    UObject* found = nullptr;
    UObjectGlobals::ForEachUObject([&](UObject* obj, int32_t, int32_t) -> LoopAction {
        if (!obj || found) return LoopAction::Continue;
        UClass* c = nullptr;
        try
        {
            c = obj->GetClassPrivate();
        }
        catch (...)
        {
            return LoopAction::Continue;
        }
        if (!c) return LoopAction::Continue;
        std::wstring cn;
        try
        {
            cn = c->GetName();
        }
        catch (...)
        {
            return LoopAction::Continue;
        }
        if (cn.find(STR("CursorSlot")) == std::wstring::npos) return LoopAction::Continue;
        // Walk outer chain looking for row.
        UObject* o = nullptr;
        try
        {
            o = obj->GetOuterPrivate();
        }
        catch (...)
        {
        }
        for (int hop = 0; hop < 8 && o; ++hop)
        {
            if (o == row)
            {
                found = obj;
                return LoopAction::Break;
            }
            try
            {
                o = o->GetOuterPrivate();
            }
            catch (...)
            {
                break;
            }
        }
        return LoopAction::Continue;
    });
    return found;
}

// One-shot diagnostic to dump cursor-slot ownership on the vanilla
// Details row + one cloned row. Tells us whether the component
// exists at all and what its outer chain looks like.
bool m_cursorSlotProbeDone{false};
void probeCursorSlots(UObject* vanillaRow, UObject* clonedRow)
{
    if (m_cursorSlotProbeDone) return;
    m_cursorSlotProbeDone = true;
    UObject* vSlot = findCursorSlotByOuterChain(vanillaRow);
    UObject* cSlot = findCursorSlotByOuterChain(clonedRow);
    std::wstring vCls, cCls;
    if (vSlot) try
        {
            vCls = vSlot->GetClassPrivate()->GetName();
        }
        catch (...)
        {
        }
    if (cSlot) try
        {
            cCls = cSlot->GetClassPrivate()->GetName();
        }
        catch (...)
        {
        }
    VLOG(STR("[MoriaCppMod] [CursorReg] PROBE vanilla row={:p} -> slot={:p} cls='{}'\n"), (void*)vanillaRow, (void*)vSlot, vCls);
    VLOG(STR("[MoriaCppMod] [CursorReg] PROBE cloned row={:p} -> slot={:p} cls='{}'\n"), (void*)clonedRow, (void*)cSlot, cCls);
    if (vSlot)
    {
        // Walk vanilla slot's outer chain so we know what to look for.
        UObject* o = vSlot;
        int hop = 0;
        while (o && hop < 8)
        {
            std::wstring oCls;
            try
            {
                oCls = o->GetClassPrivate()->GetName();
            }
            catch (...)
            {
            }
            std::wstring oName;
            try
            {
                oName = std::wstring(o->GetNamePrivate().ToString());
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [CursorReg] PROBE vanilla slot outer[{}] = {:p} cls='{}' name='{}'\n"), hop, (void*)o, oCls, oName);
            try
            {
                o = o->GetOuterPrivate();
            }
            catch (...)
            {
                break;
            }
            hop++;
        }
    }
}

void registerCursorSlotOnRow(UObject* row)
{
    if (!row || !isObjectAlive(row)) return;
    UObject* slot = nullptr;
    // First-pass: try the most likely UPROPERTY names directly.
    for (const wchar_t* candidate : {
                 STR("MorCursorSlotComponent"),
                 STR("CursorSlotComponent"),
                 STR("CursorSlot"),
                 STR("MorCursorSlot"),
         })
    {
        auto* p = row->GetValuePtrByPropertyNameInChain<UObject*>(candidate);
        if (p && *p && isObjectAlive(*p))
        {
            slot = *p;
            VLOG(STR("[MoriaCppMod] [CursorReg] found slot via property name '{}' = {:p}\n"), candidate, (void*)slot);
            break;
        }
    }
    // Fallback: walk all ObjectProperty + match class name substring.
    // Also one-shot dump every ObjectProperty value so we can see
    // what's actually on the row.
    if (!slot)
    {
        static bool s_dumped = false;
        bool dumpNow = !s_dumped;
        try
        {
            auto* cls = row->GetClassPrivate();
            uint8_t* rowBase = reinterpret_cast<uint8_t*>(row);
            for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
            {
                for (auto* prop : strct->ForEachProperty())
                {
                    if (!prop) continue;
                    std::wstring pcn;
                    try
                    {
                        pcn = prop->GetClass().GetName();
                    }
                    catch (...)
                    {
                    }
                    if (pcn != STR("ObjectProperty")) continue;
                    std::wstring pname;
                    try
                    {
                        pname = std::wstring(prop->GetName());
                    }
                    catch (...)
                    {
                    }
                    UObject** valPtr = nullptr;
                    try
                    {
                        valPtr = reinterpret_cast<UObject**>(rowBase + prop->GetOffset_Internal());
                    }
                    catch (...)
                    {
                        continue;
                    }
                    if (!valPtr) continue;
                    UObject* val = *valPtr;
                    std::wstring valCls;
                    if (val && isObjectAlive(val))
                    {
                        try
                        {
                            valCls = val->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                    }
                    if (dumpNow)
                    {
                        VLOG(STR("[MoriaCppMod] [CursorReg] DUMP prop='{}' val={:p} valCls='{}'\n"), pname, (void*)val, valCls);
                    }
                    if (!slot && val && isObjectAlive(val) &&
                        (valCls.find(STR("CursorSlot")) != std::wstring::npos || valCls.find(STR("Cursor")) != std::wstring::npos ||
                         pname.find(STR("Cursor")) != std::wstring::npos || pname.find(STR("Slot")) != std::wstring::npos))
                    {
                        slot = val;
                        VLOG(STR("[MoriaCppMod] [CursorReg] match prop='{}' valCls='{}' slot={:p}\n"), pname, valCls, (void*)slot);
                        if (!dumpNow) break;
                    }
                }
                if (slot && !dumpNow) break;
            }
        }
        catch (...)
        {
        }
        if (dumpNow) s_dumped = true;
    }
    if (!slot)
    {
        VLOG(STR("[MoriaCppMod] [CursorReg] no slot found on row {:p} (see DUMP above)\n"), (void*)row);
        return;
    }
    auto* regFn = slot->GetFunctionByNameInChain(STR("RegisterComponent"));
    if (!regFn)
    {
        VLOG(STR("[MoriaCppMod] [CursorReg] no RegisterComponent UFunction on slot={:p}\n"), (void*)slot);
        return;
    }
    try
    {
        safeProcessEvent(slot, regFn, nullptr);
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [CursorReg] RegisterComponent fired on slot={:p} (row={:p})\n"), (void*)slot, (void*)row);
}

// Toggle a row's focus state via SetIsSelected (C++ wrapper) or
// OnSetIsSelected (BP event) — whichever is callable.
void goatRowSetSelected(UObject* row, bool sel)
{
    if (!row || !isObjectAlive(row)) return;
    auto* fn = row->GetFunctionByNameInChain(STR("SetIsSelected"));
    if (!fn) fn = row->GetFunctionByNameInChain(STR("OnSetIsSelected"));
    if (!fn) return;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* p = findParam(fn, STR("bSelected"))) *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = sel;
    try
    {
        safeProcessEvent(row, fn, b.data());
    }
    catch (...)
    {
    }
}

// Advance vanilla's selection cursor by direction (+1 / -1). NO
// wrap (vanilla's own menu doesn't wrap either — bottom row + scroll
// down = stay). Walks container.Children to find current index, then
// writes the menu's selection cursor and toggles SetIsSelected on
// the old / new rows.
void cycleGoatMenuCursor(int direction)
{
    UObject* menu = m_pendingInteractMenu.Get();
    if (!menu || !isObjectAlive(menu)) return;

    int count = goatMenuChildCount();
    if (count <= 0) return;

    UObject* current = nullptr;
    if (auto* fn = menu->GetFunctionByNameInChain(STR("GetCurrentInteractionWidget")))
    {
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(menu, fn, b.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(fn, STR("ReturnValue"))) current = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
    }

    int currentIdx = -1;
    for (int i = 0; i < count; ++i)
    {
        if (goatMenuGetChildAt(i) == current)
        {
            currentIdx = i;
            break;
        }
    }
    // Clamp at top and bottom — vanilla style, no wrap.
    int newIdx = (currentIdx < 0) ? 0 : (currentIdx + direction);
    if (newIdx < 0) newIdx = 0;
    if (newIdx >= count) newIdx = count - 1;
    if (newIdx == currentIdx) return;

    UObject* newCursor = goatMenuGetChildAt(newIdx);
    if (!newCursor || !isObjectAlive(newCursor)) return;

    if (auto* slot = menu->GetValuePtrByPropertyNameInChain<UObject*>(STR("storedCurrentInteractionWidget"))) *slot = newCursor;

    goatRowSetSelected(current, false);
    goatRowSetSelected(newCursor, true);

    VLOG(STR("[MoriaCppMod] [GoatNav] cursor {} -> {} (idx {} -> {}, dir={}, count={})\n"), (void*)current, (void*)newCursor, currentIdx, newIdx, direction, count);
}

// PE-post on OnMoveNext / OnMovePrevious — always force-cycle the
// cursor ourselves. Vanilla's BP path is unreliable across our
// injected rows (works for some pairs, not others). Owning the
// cycle deterministically is simpler than chasing vanilla's gaps.
ULONGLONG m_goatNavLastMs{0};
void onRowMovePost(UObject* row, int direction)
{
    UObject* menu = m_pendingInteractMenu.Get();
    if (!menu || !isObjectAlive(menu)) return;
    // Debounce: scroll wheel can fire multiple OnMoveNext per tick.
    ULONGLONG now = GetTickCount64();
    if (now - m_goatNavLastMs < 80) return;
    m_goatNavLastMs = now;
    // Always force-cycle (don't trust vanilla to have advanced).
    cycleGoatMenuCursor(direction);
}

// Override the InteractionMenu header text. Vanilla feeds it from
// GetCurrentRole()->DisplayName which currently resolves to
// "Citizen" even after SetRole(Porter). Override directly via
// SetNameText.
void overrideMenuNameText(UObject* menu, const wchar_t* text)
{
    if (!menu || !isObjectAlive(menu) || !text) return;
    auto* fn = menu->GetFunctionByNameInChain(STR("SetNameText"));
    if (!fn) return;
    // Find the FText param (whatever its name).
    FProperty* textParam = nullptr;
    for (auto* prop : fn->ForEachProperty())
    {
        if (!prop) continue;
        bool isParam = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80 /*CPF_Parm*/));
        if (!isParam) continue;
        std::wstring cls;
        try
        {
            cls = prop->GetClass().GetName();
        }
        catch (...)
        {
        }
        if (cls == STR("TextProperty"))
        {
            textParam = prop;
            break;
        }
    }
    if (!textParam) return;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    FText ftxt(text);
    std::memcpy(b.data() + textParam->GetOffset_Internal(), &ftxt, sizeof(FText));
    try
    {
        safeProcessEvent(menu, fn, b.data());
    }
    catch (...)
    {
    }
}

// Convenience: find the MorNPCComponent on the active follower goat
// (uses the first one in m_followGoats for simplicity).
UObject* findGoatNpcComp()
{
    for (auto& g : m_followGoats)
    {
        UObject* goat = g.pawn.Get();
        if (!goat || !isObjectAlive(goat)) continue;
        if (auto* fn = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
        {
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!cls) continue;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            if (auto* p = findParam(fn, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + p->GetOffset_Internal()) = cls;
            try
            {
                safeProcessEvent(goat, fn, b.data());
            }
            catch (...)
            {
                continue;
            }
            if (auto* pRet = findParam(fn, STR("ReturnValue")))
            {
                UObject* comp = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
                if (comp && isObjectAlive(comp)) return comp;
            }
        }
    }
    return nullptr;
}

// [rc.40 BELL TOGGLE 2026-05-12] Helpers + toggle entry point.
// Toggle semantics: if a goat we've registered is alive in the world,
// dismiss it (teleport to limbo at z=-99999 — keeps actor alive so
// re-summon is symmetric, no need to spawn fresh from class). If no
// tracked goat alive, summon any BP_NpcGoat_C found in loaded chunks
// (e.g. the deeps wild goat) and adopt it. Cross-bubble fresh-spawn
// when no goat exists at all is rc.41+ work.
//
// Triggered initially by NUM9 (manual test keybind) and eventually
// by the player right-clicking the bell item in inventory (rc.40.1
// once we discover the right-click UFunction signature).

bool isGoatTracked(UObject* goat)
{
    if (!goat) return false;
    for (auto& rec : m_followGoats)
    {
        if (rec.pawn.Get() == goat) return true;
    }
    return false;
}

// DEAD (ephemeral design): world-goat adoption is retired — the only
// companion is the one the bell spawned (tracked at spawn). Kept as
// an inert stub because the tick is still wired in dllmain.
ULONGLONG m_lastNativeGoatScanMs{0};

UObject* findOurGoatAlive()
{
    for (auto& rec : m_followGoats)
    {
        UObject* g = rec.pawn.Get();
        if (g && isObjectAlive(g)) return g;
    }
    return nullptr;
}

// Destroyed actors LINGER until garbage collection (isObjectAlive
// still passes) — log-proven 2026-07-13: after a bell dismiss, four
// consecutive rings all "found" the same corpse and re-dismissed it.
// NOTE: AActor::IsActorBeingDestroyed is a plain C++ inline in 4.27
// (NOT a UFUNCTION — the first fix silently no-op'd). The PE-callable
// equivalent is KismetSystemLibrary::IsValid, which returns false for
// pending-kill objects.
bool isGoatActorUsable(UObject* g)
{
    if (!g || !isObjectAlive(g)) return false;
    auto* isValidFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:IsValid"));
    auto* kslCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary"));
    UObject* kslCDO = kslCls ? kslCls->GetClassDefaultObject() : nullptr;
    if (isValidFn && kslCDO)
    {
        std::vector<uint8_t> b(isValidFn->GetParmsSize(), 0);
        if (auto* pObj = findParam(isValidFn, STR("Object"))) *reinterpret_cast<UObject**>(b.data() + pObj->GetOffset_Internal()) = g;
        bool valid = false;
        if (safeProcessEvent(kslCDO, isValidFn, b.data()))
            if (auto* pRet = findParam(isValidFn, STR("ReturnValue"))) valid = *reinterpret_cast<bool*>(b.data() + pRet->GetOffset_Internal());
        if (!valid)
        {
            VLOG(STR("[MoriaCppMod] [BellToggle] corpse filtered (KSL IsValid=false): {:p}\n"), (void*)g);
            return false;
        }
    }
    return true;
}

UObject* findAnyGoatInWorld()
{
    std::vector<UObject*> hit;
    if (seh_findAnyGoatActor(&hit))
    {
        for (UObject* g : hit)
            if (isGoatActorUsable(g)) return g;
    }
    return nullptr;
}

// Destroy the goat actor. Per user spec: "i would not teleport the
// goast, it would fall in void space and be deleted anyway. I would
// summon and dismiss." So dismiss = real K2_DestroyActor; next
// summon must fresh-spawn via SpawnActor (rc.40b).
bool destroyGoat(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return false;
    auto* destroyFn = goat->GetFunctionByNameInChain(STR("K2_DestroyActor"));
    if (!destroyFn)
    {
        VLOG(STR("[MoriaCppMod] [BellToggle] K2_DestroyActor not found on goat — bail\n"));
        return false;
    }
    int sz = destroyFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    if (!safeProcessEvent(goat, destroyFn, buf.data()))
    {
        VLOG(STR("[MoriaCppMod] [BellToggle] K2_DestroyActor PE returned false\n"));
        return false;
    }
    VLOG(STR("[MoriaCppMod] [BellToggle] destroyed goat ptr={:p}\n"), (void*)goat);
    return true;
}

// Main toggle entry — bell right-click target (and NUM9 test keybind).
// 2-second cooldown enforced via m_lastBellToggleMs.
void setGoatHidden(UObject* goat, bool hidden)
{
    if (!goat || !isObjectAlive(goat)) return;
    if (auto* fn = goat->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
    {
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        b[0] = hidden ? 1 : 0;
        try
        {
            safeProcessEvent(goat, fn, b.data());
        }
        catch (...)
        {
        }
    }
    if (auto* fn = goat->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
    {
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        b[0] = hidden ? 0 : 1;
        try
        {
            safeProcessEvent(goat, fn, b.data());
        }
        catch (...)
        {
        }
    }
}

void toggleGoatFromBell()
{
    ULONGLONG now = GetTickCount64();
    if (now - m_lastBellToggleMs < 2000)
    {
        showOnScreen(L"Bell on cooldown", 1.0f, 0.7f, 0.7f, 0.4f);
        return;
    }
    m_lastBellToggleMs = now;

    if (!m_characterLoaded)
    {
        showOnScreen(L"Load a world first", 1.5f, 0.7f, 0.7f, 0.7f);
        return;
    }

    // Prune dead/destroyed goats from tracking so a stale weak ref
    // doesn't block re-summon after a session reload.
    m_followGoats.erase(std::remove_if(m_followGoats.begin(),
                                       m_followGoats.end(),
                                       [](FollowGoatRecord& g) {
                                           UObject* p = g.pawn.Get();
                                           return !p || !isObjectAlive(p);
                                       }),
                        m_followGoats.end());

    // [CALL-ONLY 2026-07-18, user spec] "the bell should never hide or
    // destroy etc the goat, it should only call the goat to your
    // location. period."
    //   marker goat exists → teleport it to the player (unhide safety
    //   for goats parked by older builds; never hides/destroys)
    //   no goat → spawn one (registered; GUID stored via rc.112 tail)
    UObject* live = nullptr;
    for (auto& g : m_followGoats)
    {
        UObject* p = g.pawn.Get();
        if (isGoatActorUsable(p))
        {
            live = p;
            break;
        }
    }
    if (!live)
    {
        // untracked candidate — but ONLY our marker goat, never wild goats
        UObject* stray = findAnyGoatInWorld();
        if (stray && goatHasRudhMarker(stray)) live = stray;
    }

    if (live)
    {
        setGoatHidden(live, false); // normalize legacy parked/hidden state
        teleportGoatToPlayer(live);
        VLOG(STR("[MoriaCppMod] [BellToggle] CALL — goat {:p} teleported to player\n"), (void*)live);
        showOnScreen(L"Rûdh comes to you", 2.0f, 0.4f, 0.9f, 0.4f);
        return;
    }

    // [NATIVE-RECALL 2026-07-18] No live goat but the roster remembers one:
    // ServerRescueNpc respawns the RECORD goat — WITH its inventory (log-
    // proven 15:23: the rescued goat carried the previous session's full
    // load) — at the active settlement; the adopt tick then auto-CALLs it
    // to the player. Spawning a brand-new goat here would orphan that
    // record (the 15:22 duplicate-goat incident). Only spawn fresh when
    // there is no marker or nowhere to rescue to.
    {
        // [RALLY-GATE 2026-07-18, user spec] Every path that CREATES a goat
        // (rescue or fresh spawn) requires an active settlement — the only
        // channel that keeps the goat's record restorable. Without one,
        // show an error and do nothing (no unpersistable goats, no orphaned
        // records). CALL of a live goat above needs no settlement.
        uint32_t sid = readFirstActiveSettlementId();
        if (sid == 0)
        {
            VLOG(STR("[MoriaCppMod] [BellToggle] no active settlement — bell refused (rally stone required)\n"));
            // Same message channel as the F12 save confirmation (gold-on-dark
            // panel) — showOnScreen was not visible here per user report.
            showGameNotification(L"Rûdh needs a Delving", L"", 3.0f);
            return;
        }
        uint8_t rg[16] = {0};
        if (findRudhMarkerGuidRaw(rg))
        {
            if (callGoatRescueAndRole(rg, sid))
            {
                m_recallCallUntilMs = GetTickCount64() + 30000;
                VLOG(STR("[MoriaCppMod] [BellToggle] native RECALL — record goat rescued to settlement {}, auto-CALL armed\n"), sid);
                showOnScreen(L"Recalling Rûdh...", 2.5f, 0.7f, 0.9f, 0.7f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [BellToggle] rescue call failed — falling back to spawn\n"));
        }
    }

    VLOG(STR("[MoriaCppMod] [BellToggle] no goat in world — spawning\n"));
    spawnBellGoat();
}

// [rc.46 BELL SPAWN 2026-05-12]
// Bell-summoned goat = wild fauna with manual MoveToActor follow.
// Does NOT call assignPorterRole — that registers the goat with the
// dwarf NPC manager + sets Porter role, which gives the goat the
// dwarf-style management UI ("Standing By / Needs a Furnace",
// dwarven inventory full of helmets, work assignment UI). We just
// want a goat that follows the player.
//
// The 8×8 storage is on the saddlebags item the player carries; the
// goat actor has no accessible inventory (we'll explicitly suppress
// any E-interact in rc.47 + add our custom menu).
// [rc.81 RUNTIME DEFAULTCONTAINERS 2026-07-04] Write a container class
// into the goat's configured "Inventory Comp".DefaultContainers at spawn
// time (after BeginDeferred, BEFORE FinishSpawning) so the component's
// BeginPlay builds the container from it — reproducing what a pak
// DefaultContainers edit would do, but at runtime (dodging retoc's
// import mangling). The container class's RowHandle -> DT_ContainerItems
// -> DT_Storage determines the grid size. Returns true if written.
bool writeGoatDefaultContainers(UObject* goat, UClass* containerCls)
{
    if (!goat || !containerCls) return false;
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    if (!invCls) return false;

    // Find the configured "Inventory Comp" (StorageHandle=Goat_Saddlebags),
    // not the empty inherited base component.
    UObject* inv = nullptr;
    UObject* firstAny = nullptr;
    if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
    {
        std::vector<uint8_t> b(gf->GetParmsSize(), 0);
        if (auto* pCls = findParam(gf, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
        try
        {
            safeProcessEvent(goat, gf, b.data());
        }
        catch (...)
        {
        }
        if (auto* pRet = findParam(gf, STR("ReturnValue")))
        {
            uint8_t* arr = b.data() + pRet->GetOffset_Internal();
            UObject** data = *reinterpret_cast<UObject***>(arr);
            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
            for (int32_t i = 0; data && i < num && i < 16; i++)
            {
                UObject* c = data[i];
                if (!c || !isObjectAlive(c)) continue;
                if (!firstAny) firstAny = c;
                if (auto* shP = c->GetPropertyByNameInChain(STR("StorageHandle")))
                {
                    RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
                    std::wstring sh;
                    try
                    {
                        sh = rn->ToString();
                    }
                    catch (...)
                    {
                    }
                    if (sh == STR("Goat_Saddlebags"))
                    {
                        inv = c;
                        break;
                    }
                }
            }
        }
    }
    if (!inv) inv = firstAny;
    if (!inv || !isObjectAlive(inv))
    {
        VLOG(STR("[MoriaCppMod] [DefCont] no Inventory Comp on deferred goat\n"));
        return false;
    }
    auto* dcProp = inv->GetPropertyByNameInChain(STR("DefaultContainers"));
    if (!dcProp)
    {
        VLOG(STR("[MoriaCppMod] [DefCont] DefaultContainers property missing on inv={:p}\n"), (void*)inv);
        return false;
    }
    uint8_t* arrPtr = reinterpret_cast<uint8_t*>(inv) + dcProp->GetOffset_Internal();
    int32_t oldNum = *reinterpret_cast<int32_t*>(arrPtr + 8);
    // TArray<UClass*> layout: [Data ptr(8)][Num(4)][Max(4)]. Empty arrays
    // have null Data so overwriting the header doesn't leak.
    void* elemMem = FMemory::Malloc(sizeof(UClass*), alignof(UClass*));
    if (!elemMem) return false;
    *reinterpret_cast<UClass**>(elemMem) = containerCls;
    *reinterpret_cast<void**>(arrPtr + 0) = elemMem;
    *reinterpret_cast<int32_t*>(arrPtr + 8) = 1;  // Num
    *reinterpret_cast<int32_t*>(arrPtr + 12) = 1; // Max
    VLOG(STR("[MoriaCppMod] [DefCont] wrote DefaultContainers=[{:p}] on Inventory Comp={:p} (was Num={})\n"), (void*)containerCls, (void*)inv, oldNum);
    return true;
}

// [rc.82 ARCHETYPE PATCH 2026-07-04] Patch the goat's "Inventory Comp"
// component TEMPLATE (the archetype loaded with BP_NpcGoat_C) so every
// SCS-instantiated instance inherits DefaultContainers. This is the true
// runtime equivalent of the pak DefaultContainers edit — no retoc, and
// it beats the SCS timing (rc.81 proved the instance is created during
// FinishSpawning). Idempotent; runs once a valid template is patched.
bool m_goatArchetypePatched{false};
void ensureGoatInventoryArchetypePatched()
{
    if (m_goatArchetypePatched) return;
    const wchar_t* contPath = STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C");
    UClass* cc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, contPath);
    if (!cc) cc = goat_loadClassAssetBlocking(contPath);
    if (!cc)
    {
        VLOG(STR("[MoriaCppMod] [DefCont] archetype: container class not loadable\n"));
        return;
    }

    std::vector<UObject*> comps;
    if (!seh_findAllOf(STR("MorInventoryComponent"), &comps)) return;
    int patched = 0, goatComps = 0;
    for (auto* c : comps)
    {
        if (!c || !isObjectAlive(c)) continue;
        auto* shP = c->GetPropertyByNameInChain(STR("StorageHandle"));
        if (!shP) continue;
        RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
        std::wstring sh;
        try
        {
            sh = rn->ToString();
        }
        catch (...)
        {
        }
        if (sh != STR("Goat_Saddlebags")) continue;
        goatComps++;
        std::wstring nm;
        try
        {
            nm = c->GetName();
        }
        catch (...)
        {
        }
        auto* dcProp = c->GetPropertyByNameInChain(STR("DefaultContainers"));
        if (!dcProp) continue;
        uint8_t* arrPtr = reinterpret_cast<uint8_t*>(c) + dcProp->GetOffset_Internal();
        int32_t oldNum = *reinterpret_cast<int32_t*>(arrPtr + 8);
        VLOG(STR("[MoriaCppMod] [DefCont] Goat_Saddlebags comp '{}' {:p} DefaultContainers Num={}\n"), nm.c_str(), (void*)c, oldNum);
        if (oldNum > 0)
        {
            patched++;
            continue;
        }
        void* elemMem = FMemory::Malloc(sizeof(UClass*), alignof(UClass*));
        if (!elemMem) continue;
        *reinterpret_cast<UClass**>(elemMem) = cc;
        *reinterpret_cast<void**>(arrPtr + 0) = elemMem;
        *reinterpret_cast<int32_t*>(arrPtr + 8) = 1;
        *reinterpret_cast<int32_t*>(arrPtr + 12) = 1;
        VLOG(STR("[MoriaCppMod] [DefCont] PATCHED '{}' {:p} DefaultContainers=[{:p}]\n"), nm.c_str(), (void*)c, (void*)cc);
        patched++;
    }
    VLOG(STR("[MoriaCppMod] [DefCont] archetype patch: {} Goat_Saddlebags comps, {} patched\n"), goatComps, patched);
    if (patched > 0) m_goatArchetypePatched = true;
}

// ============================================================
// [NPC-REG 2026-07-17] rc.112 identity machinery RECONNECTED
// (recovered from pre-ephemeral revision 60be7cd^). The goat is
// a REGISTERED NPC again: NpcInfo entry + Name marker +
// UniqueNpc.RowName='NPCGoat' -> the manager's native reload
// path respawns it by GUID; bell re-ring ADOPTS the same GUID.
// See memory: npc-save-restore-architecture, ue4ss-dll-integration.
// ============================================================
//   3. memcpy 24 bytes into entry + 0x030
//
// Refcount safety: Conv_StringToText returns FText with TSharedRef
// internals (refcount=1). Our parm buffer is std::vector<uint8_t> —
// no FText destructor runs when vector destructs, so refcount stays
// intact. NpcInfo entry's Name now holds a legitimate ref.
bool writeGoatNameDirectToNpcInfoEntry(const uint8_t* targetGuid16, const std::wstring& name)
{
    // Find MorNPCManager singleton
    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
    {
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
            mgr = o; break;
        }
    }
    if (!mgr)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] MorNPCManager not findable — bail\n"));
        return false;
    }

    uint8_t* niBase   = reinterpret_cast<uint8_t*>(mgr) + 0x03a0;
    uint8_t* itemsHdr = niBase + 0x0108;
    if (!isReadableMemory(itemsHdr, 16))
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] NpcInfo.Items header unreadable — bail\n"));
        return false;
    }
    uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
    int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
    constexpr int kStride  = 0x260;
    constexpr int kGuidOff = 0x001c;
    constexpr int kNameOff = 0x0030;
    if (!itemsData || itemsNum <= 0 || itemsNum > 500
        || !isReadableMemory(itemsData, itemsNum * kStride))
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] NpcInfo entries unreadable — bail\n"));
        return false;
    }

    // Find entry matching target GUID
    int matchIdx = -1;
    for (int i = 0; i < itemsNum; ++i)
    {
        uint8_t* entry = itemsData + i * kStride;
        if (std::memcmp(entry + kGuidOff, targetGuid16, 16) == 0)
        {
            matchIdx = i; break;
        }
    }
    if (matchIdx < 0)
    {
        uint32_t* tg = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(targetGuid16));
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] GUID {:08X}-{:08X}-{:08X}-{:08X} not found in {} entries — bail\n"),
             tg[0], tg[1], tg[2], tg[3], itemsNum);
        return false;
    }

    uint32_t* tg = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(targetGuid16));
    VLOG(STR("[MoriaCppMod] [NameWriteDirect] Found target entry at NpcInfo[{}] GUID={:08X}-{:08X}-{:08X}-{:08X}\n"),
         matchIdx, tg[0], tg[1], tg[2], tg[3]);

    // Construct FText via Conv_StringToText
    UObject* ktl = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/Engine.Default__KismetTextLibrary"));
    if (!ktl)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] KismetTextLibrary CDO not found — bail\n"));
        return false;
    }
    UFunction* convFn = nullptr;
    try { convFn = ktl->GetFunctionByNameInChain(STR("Conv_StringToText")); } catch (...) {}
    if (!convFn)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] Conv_StringToText UFunction not found — bail\n"));
        return false;
    }
    auto* pStr = findParam(convFn, STR("InString"));
    auto* pRet = findParam(convFn, STR("ReturnValue"));
    if (!pStr || !pRet)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] Conv_StringToText parms not resolvable — bail\n"));
        return false;
    }
    int csz = convFn->GetParmsSize();
    std::vector<uint8_t> cparms(csz, 0);

    // Pack FString InString
    int32_t strLen = static_cast<int32_t>(name.size()) + 1;
    void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
    if (!strBuf)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] FString alloc failed — bail\n"));
        return false;
    }
    wmemcpy(static_cast<wchar_t*>(strBuf), name.c_str(), strLen);
    uint8_t* sP = cparms.data() + pStr->GetOffset_Internal();
    *reinterpret_cast<void**>   (sP + 0)  = strBuf;
    *reinterpret_cast<int32_t*> (sP + 8)  = strLen;
    *reinterpret_cast<int32_t*> (sP + 12) = strLen;

    try { safeProcessEvent(ktl, convFn, cparms.data()); }
    catch (...)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] Conv_StringToText PE threw — bail\n"));
        return false;
    }

    // Read returned FText size (should be 24 per Probe T)
    int textSz = pRet->GetSize();
    if (textSz <= 0 || textSz > 64)
    {
        VLOG(STR("[MoriaCppMod] [NameWriteDirect] FText return size {} unexpected — bail\n"), textSz);
        return false;
    }

    // memcpy FText (textSz bytes) from cparms[+pRet] to entry + 0x030
    uint8_t* entry = itemsData + matchIdx * kStride;
    uint8_t* src = cparms.data() + pRet->GetOffset_Internal();
    uint8_t* dst = entry + kNameOff;

    // Log src/dst bytes BEFORE write for diagnostics
    VLOG(STR("[MoriaCppMod] [NameWriteDirect] Pre-write entry Name FText bytes (24): {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}...\n"),
         dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
    VLOG(STR("[MoriaCppMod] [NameWriteDirect] Constructed FText bytes (24): {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}...\n"),
         src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);

    std::memcpy(dst, src, textSz);

    // Verify by reading back
    wchar_t verifyBuf[256];
    seh_ftextToStringToBuf(dst, verifyBuf, 256);
    std::wstring verifyStr = verifyBuf;
    VLOG(STR("[MoriaCppMod] [NameWriteDirect] Wrote FText to entry[{}]+0x{:04X}; readback Name='{}'\n"),
         matchIdx, (unsigned)kNameOff, verifyStr.c_str());

    return verifyStr == name;
}

// [rc.58 UNIQUENPC ROW WRITE 2026-06-24] Find the NpcInfo entry
// matching targetGuid16 and write FName('Goat') into its
// PersistentData.UniqueNpc.RowName field. This is the per-entry
// restore key — without it the manager's reload code has no
// DT_NPCUniqueCharacters row to look up, and the actor isn't
// recreated. Paired with DC's pak edit that adds the 'Goat' row
// to DT_NPCUniqueCharacters.
//
// Layout (from Probe W):
//   entry stride                              = 0x260
//   PersistentData base within entry          = +0x10
//   UniqueNpc (FMorUniqueNpcRowHandle, 16B)   = +0x01A0 within PersistentData
//   UniqueNpc.RowName (FName, 8B)             = +8 within UniqueNpc
//   Absolute: entry + 0x10 + 0x01A0 + 8       = entry + 0x01B8
bool writeUniqueNpcRowNameToEntry(const uint8_t* targetGuid16, const wchar_t* rowName)
{
    if (!targetGuid16 || !rowName || !rowName[0]) return false;

    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
    {
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
            mgr = o; break;
        }
    }
    if (!mgr)
    {
        VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] MorNPCManager not findable — bail\n"));
        return false;
    }

    uint8_t* niBase   = reinterpret_cast<uint8_t*>(mgr) + 0x03a0;
    uint8_t* itemsHdr = niBase + 0x0108;
    if (!isReadableMemory(itemsHdr, 16))
    {
        VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] NpcInfo.Items header unreadable — bail\n"));
        return false;
    }
    uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
    int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
    constexpr int kStride           = 0x260;
    constexpr int kGuidOff          = 0x001c;
    constexpr int kUniqueRowNameOff = 0x01b8;
    if (!itemsData || itemsNum <= 0 || itemsNum > 500
        || !isReadableMemory(itemsData, itemsNum * kStride))
    {
        VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] NpcInfo entries unreadable — bail\n"));
        return false;
    }

    int matchIdx = -1;
    for (int i = 0; i < itemsNum; ++i)
    {
        uint8_t* entry = itemsData + i * kStride;
        if (std::memcmp(entry + kGuidOff, targetGuid16, 16) == 0)
        {
            matchIdx = i; break;
        }
    }
    if (matchIdx < 0)
    {
        uint32_t* tg = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(targetGuid16));
        VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] GUID {:08X}-{:08X}-{:08X}-{:08X} not found in {} entries — bail\n"),
             tg[0], tg[1], tg[2], tg[3], itemsNum);
        return false;
    }

    uint8_t* entry = itemsData + matchIdx * kStride;
    uint8_t* dst   = entry + kUniqueRowNameOff;

    // Read pre-write bytes
    VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] Pre-write UniqueNpc.RowName bytes (8): {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}\n"),
         dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
    std::wstring preName = seh_fnameToString(dst);
    VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] Pre-write UniqueNpc.RowName decoded = '{}'\n"),
         preName.c_str());

    // Construct FName via global pool add
    RC::Unreal::FName rowFName(rowName, RC::Unreal::FNAME_Add);
    std::memcpy(dst, &rowFName, 8);

    // Verify by reading back
    std::wstring postName = seh_fnameToString(dst);
    VLOG(STR("[MoriaCppMod] [UniqueNpcWrite] Wrote FName('{}') to entry[{}]+0x{:04X}; readback RowName='{}'\n"),
         rowName, matchIdx, (unsigned)kUniqueRowNameOff, postName.c_str());

    return postName == std::wstring(rowName);
}

// [rc.48 STORE RUNTIME ACTOR 2026-06-14] Call
// WorldState.StoreRuntimeActor(goat, &handle, bStoreStability=true)
// via PE. This is the missing piece that registers the goat actor
// with WorldState's save graph — without it the actor is destroyed
// on world unload (as Probe P consistently confirmed).
//
// Per Q.2 enumeration, ParmsSize=42:
//   parms[0..7]   = Actor* (8 bytes)
//   parms[8..39]  = FRuntimeActorHandle InOut (32 bytes, zero-init)
//   parms[40]     = bool bStoreStability (1 byte, true)
//   parms[41]     = bool ReturnValue (1 byte, engine fills)
//
// Logs handle bytes for struct-layout decode.

// [rc.59 AUTO-RESTORE 2026-06-26] Scan NpcInfo for entries
// with Name=='Rûdh'. For each marker entry with no live
// BP_NpcGoat actor matching its NpcGuid, invoke spawnBellGoat()
// to recreate the actor. Existing GuidAdopt logic finds the
// marker and binds the new actor to it — same flow as a user-
// initiated bell-ring, just triggered automatically at world load.
void autoRestoreGoatsFromMarker()
{
    VLOG(STR("[MoriaCppMod] [AutoRestore] === scan NpcInfo for Name='Rûdh' markers ===\n"));

    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
    {
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
            mgr = o; break;
        }
    }
    if (!mgr)
    {
        VLOG(STR("[MoriaCppMod] [AutoRestore] MorNPCManager not findable — bail\n"));
        return;
    }

    uint8_t* mgrBase = reinterpret_cast<uint8_t*>(mgr);
    uint8_t* arrayHeader = mgrBase + 0x03a0 + 0x0108;
    if (!isReadableMemory(arrayHeader, 16))
    {
        VLOG(STR("[MoriaCppMod] [AutoRestore] NpcInfo header unreadable — bail\n"));
        return;
    }
    uint8_t* itemsData = *reinterpret_cast<uint8_t**>(arrayHeader);
    int32_t  itemsNum  = *reinterpret_cast<int32_t*>(arrayHeader + 8);
    constexpr int kStride  = 0x0260;
    constexpr int kGuidOff = 0x001C;
    constexpr int kNameOff = 0x0030;
    if (!itemsData || itemsNum <= 0 || itemsNum > 500
        || !isReadableMemory(itemsData, itemsNum * kStride))
    {
        VLOG(STR("[MoriaCppMod] [AutoRestore] Items region unreadable — bail\n"));
        return;
    }

    // Collect marker entries' GUIDs
    std::vector<std::array<uint8_t,16>> markerGuids;
    for (int i = 0; i < itemsNum; ++i)
    {
        uint8_t* entry = itemsData + i * kStride;
        wchar_t nameBuf[256] = L"";
        seh_ftextToStringToBuf(entry + kNameOff, nameBuf, 256);
        std::wstring nm = nameBuf;
        if (isGoatNameMatch(nm))  // [v8.2.x] tolerates mojibake variant
        {
            std::array<uint8_t,16> g{};
            std::memcpy(g.data(), entry + kGuidOff, 16);
            markerGuids.push_back(g);
            uint32_t* gu = reinterpret_cast<uint32_t*>(entry + kGuidOff);
            VLOG(STR("[MoriaCppMod] [AutoRestore] marker entry [{}] GUID={:08X}-{:08X}-{:08X}-{:08X}\n"),
                 i, gu[0], gu[1], gu[2], gu[3]);
        }
    }
    if (markerGuids.empty())
    {
        VLOG(STR("[MoriaCppMod] [AutoRestore] no Name='{}' markers in {} entries — nothing to restore\n"),
             m_goatName.c_str(), itemsNum);
        return;
    }
    VLOG(STR("[MoriaCppMod] [AutoRestore] found {} marker entries\n"), (int)markerGuids.size());

    // Check live BP_NpcGoat NpcGuids
    std::vector<std::array<uint8_t,16>> liveGuids;
    {
        std::vector<UObject*> found;
        if (seh_findAllOf(STR("BP_NpcGoat_C"), &found))
        {
            UClass* goatCls = m_goatBPClass;
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            for (auto* o : found)
            {
                if (!o || !isObjectAlive(o)) continue;
                if (goatCls && o == reinterpret_cast<UObject*>(goatCls)) continue;
                if (goatCls && o == goatCls->GetClassDefaultObject()) continue;
                if (!npcCompCls) break;
                auto* getCompFn = o->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (!getCompFn) continue;
                struct { UClass* InClass; UObject* Ret; } parms{};
                parms.InClass = npcCompCls;
                if (!safeProcessEvent(o, getCompFn, &parms)) continue;
                UObject* npcComp = parms.Ret;
                if (!npcComp || !isObjectAlive(npcComp)) continue;
                uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                if (!guidPtr) continue;
                std::array<uint8_t,16> g{};
                std::memcpy(g.data(), guidPtr, 16);
                liveGuids.push_back(g);
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [AutoRestore] {} live BP_NpcGoat with NpcGuids\n"), (int)liveGuids.size());

    // SINGLE-GOAT GATE: never spawn for orphan markers while any
    // live goat exists (MAX_FOLLOW_GOATS=1) — native restore already
    // brings the goat back; a second spawn causes dedupe churn.
    // Details: memory goat-final-architecture → identity/restore.
    if (!liveGuids.empty())
    {
        VLOG(STR("[MoriaCppMod] [AutoRestore] {} live goat(s) present — orphan-marker spawns SKIPPED (single-goat gate)\n"),
             (int)liveGuids.size());
        VLOG(STR("[MoriaCppMod] [AutoRestore] === END — spawned 0 ===\n"));
        return;
    }

    // For each marker without a live actor, spawn one (at most one per call)
    int spawned = 0;
    for (auto& mg : markerGuids)
    {
        bool alreadyAlive = false;
        for (auto& lg : liveGuids)
        {
            if (std::memcmp(lg.data(), mg.data(), 16) == 0)
            {
                alreadyAlive = true; break;
            }
        }
        if (alreadyAlive)
        {
            uint32_t* gu = reinterpret_cast<uint32_t*>(mg.data());
            VLOG(STR("[MoriaCppMod] [AutoRestore] marker GUID={:08X}-... already has live actor — skip\n"), gu[0]);
            continue;
        }
        uint32_t* gu = reinterpret_cast<uint32_t*>(mg.data());
        VLOG(STR("[MoriaCppMod] [AutoRestore] *** spawning goat for orphan marker GUID={:08X}-{:08X}-{:08X}-{:08X} ***\n"),
             gu[0], gu[1], gu[2], gu[3]);
        m_lastBellToggleMs = 0;  // reset bell cooldown so spawnBellGoat proceeds
        spawnBellGoat();
        ++spawned;
        break;
    }
    VLOG(STR("[MoriaCppMod] [AutoRestore] === END — spawned {} ===\n"), spawned);
}

// [rc.60 TAME GOAT 2026-06-28] Strip dwarven-NPC components and
// stop the behavior tree so the goat behaves like wild fauna
// (passive, controllable via our MoveToActor calls) rather
// than a working dwarf settler. Called after spawn from
// spawnBellGoat once the actor is fully constructed.

// Does this goat's MorNPCComponent.NpcGuid match a 'Rûdh' NpcInfo marker?
// Distinguishes OUR registered goat (bell-spawned or manager-restored)
// from the game's WILD goats — 2026-07-17 log: wild-herd churn re-adopted
// a fresh wild goat every 2s (same recycled address), hogging the single
// herd slot and blocking bell summons.
bool goatHasRudhMarker(UObject* goat)
{
    UObject* npcComp = nullptr;
    UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
    if (npcCls)
    {
        if (auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
        {
            std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), npcCls);
            if (safeProcessEvent(goat, getComp, gb.data()))
                npcComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
        }
    }
    if (!npcComp || !isObjectAlive(npcComp)) return false;
    uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
    if (!guidPtr) return false;
    static const uint8_t kZero[16] = {0};
    if (std::memcmp(guidPtr, kZero, 16) == 0) return false;

    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
            mgr = o;
            break;
        }
    if (!mgr) return false;
    uint8_t* hdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
    if (!isReadableMemory(hdr, 16)) return false;
    uint8_t* data = *reinterpret_cast<uint8_t**>(hdr);
    int32_t num = *reinterpret_cast<int32_t*>(hdr + 8);
    constexpr int kStride = 0x260, kGuidOff = 0x001c, kNameOff = 0x0030;
    if (!data || num <= 0 || num > 500 || !isReadableMemory(data, num * kStride)) return false;
    for (int i = 0; i < num; ++i)
    {
        uint8_t* entry = data + i * kStride;
        if (std::memcmp(entry + kGuidOff, guidPtr, 16) != 0) continue;
        wchar_t nameBuf[256] = L"";
        seh_ftextToStringToBuf(entry + kNameOff, nameBuf, 256);
        return isGoatNameMatch(std::wstring(nameBuf));
    }
    return false;
}

void adoptNativeGoat(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    if (isGoatTracked(goat)) return;
    if (m_followGoats.size() >= MAX_FOLLOW_GOATS) return;
    std::wstring cls;
    try { cls = goat->GetClassPrivate()->GetName(); } catch (...) { return; }
    if (cls != STR("BP_NpcGoat_C") && cls != STR("BP_PorterGoat_C")) return;
    // [NPC-REG gate] only OUR registered goat — never wild goats.
    if (!goatHasRudhMarker(goat)) return;
    FollowGoatRecord rec{};
    rec.pawn               = RC::Unreal::FWeakObjectPtr(goat);
    rec.bellSpawned        = false;  // porter-role goat: leash-follow, not manual MoveToActor
    rec.stayMode           = false;  // default to following on adoption
    rec.componentsLogged   = true;   // skip the one-shot component-deactivation block
    rec.porterRoleAssigned = true;   // Tobi's BP already assigns the Porter role
    rec.controllerReplaced = true;   // never swap Tobi's AIController
    rec.interactiveRefired = true;   // leave Tobi's interaction prompts untouched
    rec.postRegDumpDone    = true;
    // [CALL-ONLY] the bell never hides — but a goat saved by an OLDER
    // build (or restored mid-park) can arrive hidden. Normalize.
    if (auto* hiddenPtr = goat->GetValuePtrByPropertyNameInChain<uint8_t>(STR("bHidden")))
    {
        if ((*hiddenPtr & 0x01) != 0)
        {
            VLOG(STR("[MoriaCppMod] [NativeGoat] adopted goat was HIDDEN — unhiding (call-only spec)\n"));
            setGoatHidden(goat, false);
        }
    }
    m_followGoats.push_back(rec);
    VLOG(STR("[MoriaCppMod] [NativeGoat] adopted Tobi-summoned {} {:p} (herd={})\n"),
         cls.c_str(), (void*)goat, (int)m_followGoats.size());
    // [NPC-REG v4] a manager-restored goat spawned natively BEFORE our
    // template patch could land — apply dwarf defs to its live comps too.
    patchGoatInstanceInventory(goat);
    // [NATIVE-RECALL] the bell rescued this goat back from its record —
    // finish the CALL by bringing it from the settlement to the player.
    if (m_recallCallUntilMs != 0 && GetTickCount64() < m_recallCallUntilMs)
    {
        m_recallCallUntilMs = 0;
        teleportGoatToPlayer(goat);
        VLOG(STR("[MoriaCppMod] [NativeGoat] recalled goat auto-CALLed to player\n"));
        showOnScreen(L"Rûdh comes to you", 2.0f, 0.4f, 0.9f, 0.4f);
    }
    showOnScreen(L"Porter Goat linked", 1.5f, 0.7f, 0.9f, 0.7f);
}


// Total loose stacks across a goat's containers (0 = empty/fresh).
int countGoatStacks(UObject* goat)
{
    std::vector<UObject*> comps;
    goatInvCompsWithContainers(goat, comps);
    std::vector<std::string> lines;
    for (auto* c : comps) collectCompStacks(c, lines);
    return (int)lines.size();
}

// [CALL-ONLY 2026-07-18, user spec] NO multi-goat cleanup — never
// destroy/hide anything. Simple adopt: when the herd slot is empty and
// a marker goat (record-restored or otherwise ours) exists, track it.
void tickAdoptNativeGoat()
{
    if (!m_characterLoaded) return;
    if (m_followGoats.size() >= MAX_FOLLOW_GOATS) return;
    ULONGLONG now = GetTickCount64();
    if (now - m_lastNativeGoatScanMs < 2000) return;
    m_lastNativeGoatScanMs = now;

    std::vector<UObject*> hits;
    if (!seh_findAnyGoatActor(&hits)) return;
    for (UObject* g : hits)
    {
        if (!g || !isObjectAlive(g)) continue;
        std::wstring cls;
        try
        {
            cls = g->GetClassPrivate()->GetName();
        }
        catch (...)
        {
            continue;
        }
        if (cls != STR("BP_NpcGoat_C") && cls != STR("BP_PorterGoat_C")) continue;
        std::wstring nm;
        try
        {
            nm = g->GetName();
        }
        catch (...)
        {
        }
        if (nm.rfind(STR("Default__"), 0) == 0) continue;
        if (!goatHasRudhMarker(g)) continue; // wild goats never participate
        adoptNativeGoat(g);
        break; // MAX_FOLLOW_GOATS == 1
    }
}

// [NPC-REG 2026-07-17] Goat body-inventory archetype patch (rc.82
// technique, retargeted). Root cause of the "all helmets /
// uninitialized" goat pane: the goat components' StorageHandle row
// 'Goat.Slot.EpicPack' does NOT exist in live DT_Storage (Tobi never
// shipped it), so native container instantiation has nothing to build.
// Fix: patch the BP_NpcGoat_C component TEMPLATEs to the DWARF defs
// that DO exist — StorageHandle row 'Dwarf.Inventory' +
// DefaultContainers = [BP_ContainerItem_Dwarf_BodyInventoryNPC_C]
// (the row our paks grew to 6x6). Templates only: future spawns
// inherit; native construction should instantiate a real container.
bool m_goatBodyInvPatched{false};
// Load the SAME 7 DefaultContainers classes BP_NpcDwarf uses (body 6x6 +
// 6 equipment slot containers) — all with LIVE DT rows. Returns count
// loaded; out[0] (the body container) is mandatory for callers.
int loadDwarfContainerClasses(UClass** out)
{
    static const wchar_t* kDwarfContainers[] = {
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_BodyInventoryNPC.BP_ContainerItem_Dwarf_BodyInventoryNPC_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_Helmet.BP_ContainerItem_Dwarf_Slot_Helmet_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_Torso.BP_ContainerItem_Dwarf_Slot_Torso_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_Gloves.BP_ContainerItem_Dwarf_Slot_Gloves_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_Boots.BP_ContainerItem_Dwarf_Slot_Boots_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_MainHandNPC.BP_ContainerItem_Dwarf_Slot_MainHandNPC_C"),
            STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_Slot_OffHandNPC.BP_ContainerItem_Dwarf_Slot_OffHandNPC_C"),
    };
    int loaded = 0;
    for (int i = 0; i < 7; i++)
    {
        out[i] = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kDwarfContainers[i]);
        if (!out[i]) out[i] = goat_loadClassAssetBlocking(kDwarfContainers[i]);
        if (out[i]) loaded++;
    }
    VLOG(STR("[MoriaCppMod] [BodyInv] dwarf container classes loaded {}/7\n"), loaded);
    return loaded;
}

// Write the dwarf defs onto one MorInventoryComponent (template OR live
// instance): StorageHandle row -> 'Dwarf.Inventory', DefaultContainers ->
// the 7 dwarf containers. Returns true if the array was written.
bool writeDwarfDefsToComp(UObject* c, UClass** classes)
{
    if (auto* shP = c->GetPropertyByNameInChain(STR("StorageHandle")))
    {
        RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
        RC::Unreal::FName dwarfRow(STR("Dwarf.Inventory"), RC::Unreal::FNAME_Add);
        *rn = dwarfRow;
    }
    auto* dcProp = c->GetPropertyByNameInChain(STR("DefaultContainers"));
    if (!dcProp) return false;
    uint8_t* arrPtr = reinterpret_cast<uint8_t*>(c) + dcProp->GetOffset_Internal();
    void* elemMem = FMemory::Malloc(sizeof(UClass*) * 7, alignof(UClass*));
    if (!elemMem) return false;
    int n = 0;
    for (int i = 0; i < 7; i++)
        if (classes[i]) reinterpret_cast<UClass**>(elemMem)[n++] = classes[i];
    if (n == 0) return false;
    *reinterpret_cast<void**>(arrPtr + 0) = elemMem;
    *reinterpret_cast<int32_t*>(arrPtr + 8) = n;
    *reinterpret_cast<int32_t*>(arrPtr + 12) = n;
    return true;
}

void ensureGoatBodyInventoryArchetype()
{
    if (m_goatBodyInvPatched) return;
    UClass* classes[7] = {nullptr};
    if (loadDwarfContainerClasses(classes) == 0 || !classes[0])
    {
        VLOG(STR("[MoriaCppMod] [BodyInv] dwarf body container class not loadable — patch skipped\n"));
        return;
    }
    std::vector<UObject*> comps;
    if (!seh_findAllOf(STR("MorInventoryComponent"), &comps)) return;
    int patched = 0, goatTemplates = 0;
    for (auto* c : comps)
    {
        if (!c || !isObjectAlive(c)) continue;
        std::wstring nm, outerNm;
        try
        {
            nm = c->GetName();
            if (auto* outer = c->GetOuterPrivate()) outerNm = outer->GetName();
        }
        catch (...)
        {
            continue;
        }
        // component TEMPLATES inside the BP_NpcGoat_C class only.
        // [fix 2026-07-17] templates are outered to SCS_Node/ICH objects,
        // not the BPGC directly — walk the whole outer CHAIN for NpcGoat
        // (log proved immediate-outer matching found 0 templates).
        if (nm.find(STR("GEN_VARIABLE")) == std::wstring::npos) continue;
        bool inGoat = false;
        try
        {
            UObject* o = c->GetOuterPrivate();
            for (int d = 0; o && d < 6 && !inGoat; d++)
            {
                std::wstring on = o->GetName();
                if (on.find(STR("NpcGoat")) != std::wstring::npos) inGoat = true;
                o = o->GetOuterPrivate();
            }
        }
        catch (...)
        {
            continue;
        }
        if (!inGoat)
        {
            // one-shot diagnostic: log every non-goat template's identity once
            static int s_tplLogs = 30;
            if (s_tplLogs > 0)
            {
                --s_tplLogs;
                VLOG(STR("[MoriaCppMod] [BodyInv] (skip) template '{}' outer '{}'\n"), nm.c_str(), outerNm.c_str());
            }
            continue;
        }
        goatTemplates++;
        if (writeDwarfDefsToComp(c, classes))
        {
            VLOG(STR("[MoriaCppMod] [BodyInv] template '{}' (outer '{}') -> dwarf defs (Dwarf.Inventory + 7 containers)\n"),
                 nm.c_str(), outerNm.c_str());
            patched++;
        }
    }
    VLOG(STR("[MoriaCppMod] [BodyInv] archetype patch: {} goat templates found, {} patched\n"), goatTemplates, patched);
    if (patched > 0) m_goatBodyInvPatched = true;
}

// [NPC-REG v3 2026-07-17] Missing-row synthesis (user direction: honor
// Tobi's ORIGINAL design instead of aliasing the goat onto dwarf defs).
// The goat's components reference 'Goat.Slot.EpicPack' — the row Tobi
// never shipped in DT_Storage/DT_ContainerItems ("missing DT row").
// The DWARF has the exact same pattern (Dwarf.Slot.EpicPack, a 1x1
// equipped-only slot that holds a pack whose OWN container provides
// the grid) — so synthesize the goat rows at runtime as copies of the
// dwarf rows with goat-specific fixups. Rows are never destroyed, so
// byte-copied FText internals are safe (no double-free path).
bool m_goatRowsEnsured{false};
DataTableUtil m_dtStorageGoat;
DataTableUtil m_dtContItemsGoat;
void ensureGoatStorageRows()
{
    if (m_goatRowsEnsured) return;
    if (!m_dtStorageGoat.isBound()) m_dtStorageGoat.bind(STR("DT_Storage"));
    if (!m_dtContItemsGoat.isBound()) m_dtContItemsGoat.bind(STR("DT_ContainerItems"));
    if (!m_dtStorageGoat.isBound() || !m_dtContItemsGoat.isBound())
    {
        VLOG(STR("[MoriaCppMod] [GoatRows] bind failed (storage={} items={})\n"), m_dtStorageGoat.isBound(), m_dtContItemsGoat.isBound());
        return;
    }
    auto propOff = [](DataTableUtil& dt, const wchar_t* name) -> int {
        if (!dt.rowStruct) return -1;
        try
        {
            for (auto* p : dt.rowStruct->ForEachProperty())
                if (p && p->GetName() == name) return p->GetOffset_Internal();
        }
        catch (...)
        {
        }
        return -1;
    };

    bool okStorage = true, okItems = true;

    // 1) DT_Storage['Goat.Slot.EpicPack'] — verbatim copy of the dwarf slot row (1x1, equipped-only).
    if (!m_dtStorageGoat.findRowData(STR("Goat.Slot.EpicPack")))
    {
        uint8_t* src = m_dtStorageGoat.findRowData(STR("Dwarf.Slot.EpicPack"));
        if (src && m_dtStorageGoat.rowSize > 0)
        {
            uint8_t* copy = reinterpret_cast<uint8_t*>(FMemory::Malloc(m_dtStorageGoat.rowSize));
            std::memcpy(copy, src, m_dtStorageGoat.rowSize);
            okStorage = m_dtStorageGoat.callAddRowInternal(STR("Goat.Slot.EpicPack"), copy);
        }
        else okStorage = false;
        VLOG(STR("[MoriaCppMod] [GoatRows] DT_Storage['Goat.Slot.EpicPack'] add -> {} (rowSize={})\n"), okStorage, m_dtStorageGoat.rowSize);
    }

    // 2) DT_ContainerItems['Goat.Slot.EpicPack'] — dwarf copy + goat fixups:
    //    StorageRowHandle.RowName -> our new storage row, Actor -> Tobi's
    //    BP_ContainerItem_Goat_Slot_EpicPack (softpath FName @ +16 per
    //    dt-npcunique-structure: TSoftObjectPtr stores AssetPathName at +16).
    if (!m_dtContItemsGoat.findRowData(STR("Goat.Slot.EpicPack")))
    {
        uint8_t* src = m_dtContItemsGoat.findRowData(STR("Dwarf.Slot.EpicPack"));
        int srhOff = propOff(m_dtContItemsGoat, STR("StorageRowHandle"));
        int actorOff = propOff(m_dtContItemsGoat, STR("Actor"));
        if (src && m_dtContItemsGoat.rowSize > 0 && srhOff >= 0 && actorOff >= 0)
        {
            uint8_t* copy = reinterpret_cast<uint8_t*>(FMemory::Malloc(m_dtContItemsGoat.rowSize));
            std::memcpy(copy, src, m_dtContItemsGoat.rowSize);
            RC::Unreal::FName goatRow(STR("Goat.Slot.EpicPack"), RC::Unreal::FNAME_Add);
            std::memcpy(copy + srhOff + 8, &goatRow, 8); // FDataTableRowHandle.RowName @ +8
            RC::Unreal::FName goatActor(STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C"),
                                        RC::Unreal::FNAME_Add);
            std::memcpy(copy + actorOff + 16, &goatActor, 8); // TSoftObjectPtr.AssetPathName @ +16
            okItems = m_dtContItemsGoat.callAddRowInternal(STR("Goat.Slot.EpicPack"), copy);
        }
        else okItems = false;
        VLOG(STR("[MoriaCppMod] [GoatRows] DT_ContainerItems['Goat.Slot.EpicPack'] add -> {} (srhOff={} actorOff={})\n"),
             okItems, propOff(m_dtContItemsGoat, STR("StorageRowHandle")), propOff(m_dtContItemsGoat, STR("Actor")));
    }
    if (okStorage && okItems)
    {
        m_goatRowsEnsured = true;
        VLOG(STR("[MoriaCppMod] [GoatRows] goat slot rows LIVE — Tobi's original defs now resolve\n"));
    }
}

// [DOOR-1 REVALIDATION 2026-07-18, user-directed] Runtime injection of
// the DT_NPCUniqueCharacters 'NPCGoat' row — re-testing the 2026-06-26
// "ValidNpcRestores trap" with today's architecture. Differences from
// the rc.58 pak test that produced the trap finding:
//   1. Row added at RUNTIME (+5s), AFTER AMorNPCManager::BeginPlay —
//      the manager's ValidNpcRestores cache (built at BeginPlay) stays
//      empty this session, so Register should keep creating entries.
//      The per-entry restore lookup on NEXT load may still resolve the
//      row live (we re-add every session at +5s, restore is bubble-late).
//   2. Row name 'NPCGoat' (matches the roster UniqueNpc write), not 'Goat'.
//   3. No saddlebags-item flow anymore — the old "saddlebag UI gone"
//      symptom can't confound the result.
// Trap signals to watch: bell-ring '[rc.112] adopted=... NpcInfo count
// X->Y' (count must grow in a fresh world) and the ValidNpcRestores
// counts logged here + at the +20s RecordProbe.
DataTableUtil m_dtNpcUnique;
bool m_npcUniqueRowEnsured{false};

int readValidNpcRestoresCount()
{
    std::vector<UObject*> mgrs;
    if (!findAllOfSafe(STR("MorNPCManager"), mgrs)) return -1;
    for (UObject* o : mgrs)
    {
        if (!o || !isObjectAlive(o)) continue;
        std::wstring cn = safeClassName(o);
        if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
        if (auto* arr = o->GetValuePtrByPropertyNameInChain<uint8_t>(STR("ValidNpcRestores")))
            return *reinterpret_cast<int32_t*>(arr + 8);
        return -1;
    }
    return -1;
}

void ensureNpcUniqueGoatRow()
{
    if (m_npcUniqueRowEnsured) return;
    if (!m_dtNpcUnique.isBound()) m_dtNpcUnique.bind(STR("DT_NPCUniqueCharacters"));
    if (!m_dtNpcUnique.isBound())
    {
        VLOG(STR("[MoriaCppMod] [NpcUniqueRow] DT_NPCUniqueCharacters bind failed\n"));
        return;
    }
    if (m_dtNpcUnique.findRowData(STR("NPCGoat")))
    {
        m_npcUniqueRowEnsured = true;
        return;
    }
    uint8_t* src = m_dtNpcUnique.findRowData(STR("Wanderer"));
    if (!src || m_dtNpcUnique.rowSize <= 0)
    {
        VLOG(STR("[MoriaCppMod] [NpcUniqueRow] source row 'Wanderer' not found (rowSize={})\n"), m_dtNpcUnique.rowSize);
        return;
    }
    int ccOff = -1, apOff = -1;
    try
    {
        for (auto* p : m_dtNpcUnique.rowStruct->ForEachProperty())
        {
            if (!p) continue;
            if (p->GetName() == STR("CharacterClass")) ccOff = p->GetOffset_Internal();
            else if (p->GetName() == STR("AppearancePreset")) apOff = p->GetOffset_Internal();
        }
    }
    catch (...)
    {
    }
    if (ccOff < 0 || apOff < 0)
    {
        VLOG(STR("[MoriaCppMod] [NpcUniqueRow] field resolve failed (ccOff={} apOff={})\n"), ccOff, apOff);
        return;
    }
    uint8_t* copy = reinterpret_cast<uint8_t*>(FMemory::Malloc(m_dtNpcUnique.rowSize));
    std::memcpy(copy, src, m_dtNpcUnique.rowSize);
    // CharacterClass TSoftClassPtr: zero the cached WeakObjectPtr+Tag (16B)
    // so nothing resolves to Wanderer's dwarf class, then set AssetPathName
    // (FName @ +16 per dt-npcunique-structure surprise #3).
    std::memset(copy + ccOff, 0, 16);
    RC::Unreal::FName goatCls(STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"), RC::Unreal::FNAME_Add);
    std::memcpy(copy + ccOff + 16, &goatCls, 8);
    // AppearancePreset FDataTableRowHandle.RowName @ +8 -> 'None' (no preset for the goat).
    RC::Unreal::FName noneRow(STR("None"), RC::Unreal::FNAME_Add);
    std::memcpy(copy + apOff + 8, &noneRow, 8);
    bool ok = m_dtNpcUnique.callAddRowInternal(STR("NPCGoat"), copy);
    if (ok) m_npcUniqueRowEnsured = true;
    VLOG(STR("[MoriaCppMod] [NpcUniqueRow] DT_NPCUniqueCharacters['NPCGoat'] add -> {} (rowSize={} ccOff={} apOff={}) ValidNpcRestores={}\n"),
         ok, m_dtNpcUnique.rowSize, ccOff, apOff, readValidNpcRestoresCount());
}

// [NPC-REG 2026-07-17] Instance-side pass: patch the LIVE goat's
// MorInventoryComponents to the dwarf defs and retry instantiation.
// Rationale: construction-time DefaultContainers instantiation failed
// silently for the goat because its DT rows didn't exist — every
// rc.100-era trigger (ResetToStarting etc.) was tested against BROKEN
// defs. With valid dwarf rows in place, retry ResetToStarting and log
// HasContainers before/after (the ground-truth signal).
// [BAD-OBJECT SWEEP 2026-07-18] Tobi's loc-less 'Goat.Slot.EpicPack'
// container item is serialized inside older goats' actor records — it
// restores with the goat every load and the UI delete doesn't stick
// (container count stayed 8 across the user's delete + reload). Remove
// every BP_ContainerItem_Goat* item by CLASS from each inventory comp:
// RemoveItem(TSubclassOf, Count, EInventoryQuery::Personal=0) on the
// FGK inventory. Idempotent; runs from patchGoatInstanceInventory so
// both adopt (restored goat) and fresh spawn are covered.
void sweepGoatContainerItemsByPrefix(UObject* c, const wchar_t* prefix)
{
    if (!c || !isObjectAlive(c) || !prefix) return;
    FProperty* itemsProp = c->GetPropertyByNameInChain(STR("Items"));
    if (!itemsProp) return;
    uint8_t* listBase = reinterpret_cast<uint8_t*>(c) + itemsProp->GetOffset_Internal() + iiaListOff();
    if (!isReadableMemory(listBase, 16)) return;
    auto* rmFn = c->GetFunctionByNameInChain(STR("RemoveItem"));
    if (!rmFn) return;
    auto* pItem = findParam(rmFn, STR("Item"));
    auto* pCount = findParam(rmFn, STR("Count"));
    for (int pass = 0; pass < 8; pass++) // re-read after each removal
    {
        uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
        int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
        if (!arrData || arrNum <= 0 || arrNum > 10000) return;
        UClass* target = nullptr;
        const int stride = iiSize(), itemOff = iiItemOff();
        for (int i = 0; i < arrNum && i < 256; ++i)
        {
            uint8_t* e = arrData + i * stride;
            if (!isReadableMemory(e, stride)) continue;
            UClass* ic = *reinterpret_cast<UClass**>(e + itemOff);
            if (!ic || !isObjectAlive(ic)) continue;
            std::wstring n;
            try
            {
                n = ic->GetName();
            }
            catch (...)
            {
                continue;
            }
            if (n.rfind(prefix, 0) == 0)
            {
                target = ic;
                break;
            }
        }
        if (!target) return;
        std::vector<uint8_t> rb(rmFn->GetParmsSize(), 0);
        if (pItem) *reinterpret_cast<UClass**>(rb.data() + pItem->GetOffset_Internal()) = target;
        if (pCount) *reinterpret_cast<int32_t*>(rb.data() + pCount->GetOffset_Internal()) = 99;
        bool ok = false;
        try
        {
            ok = safeProcessEvent(c, rmFn, rb.data());
        }
        catch (...)
        {
        }
        std::wstring tn;
        try
        {
            tn = target->GetName();
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [BadObjSweep] RemoveItem('{}') on comp '{}' pe={}\n"),
             tn.c_str(), safeObjectName(c).c_str(), ok ? STR("OK") : STR("FAIL"));
        if (!ok) return;
    }
}

// Legacy wrapper (call disabled per feedback_no_modside_item_deletion).
void sweepGoatEpicPackItems(UObject* c)
{
    sweepGoatContainerItemsByPrefix(c, STR("BP_ContainerItem_Goat"));
}

void patchGoatInstanceInventory(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/FGK.MorInventoryComponent"));
    if (!invCls) invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    UClass* bodyCls = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, STR("/Game/Items/ContainerItems/BP_ContainerItem_Dwarf_BodyInventoryNPC.BP_ContainerItem_Dwarf_BodyInventoryNPC_C"));
    if (!invCls || !bodyCls)
    {
        VLOG(STR("[MoriaCppMod] [BodyInv] instance pass: classes unavailable (inv={:p} body={:p})\n"), (void*)invCls, (void*)bodyCls);
        return;
    }
    auto* getComps = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass"));
    if (!getComps) getComps = goat->GetFunctionByNameInChain(STR("GetComponentsByClass"));
    if (!getComps)
    {
        VLOG(STR("[MoriaCppMod] [BodyInv] instance pass: GetComponentsByClass missing\n"));
        return;
    }
    std::vector<uint8_t> b(getComps->GetParmsSize(), 0);
    writeGoatParm<UClass*>(getComps, b.data(), STR("ComponentClass"), invCls);
    if (!safeProcessEvent(goat, getComps, b.data())) return;
    auto* pr = findParam(getComps, STR("ReturnValue"));
    if (!pr) return;
    uint8_t* arr = b.data() + pr->GetOffset_Internal();
    uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
    VLOG(STR("[MoriaCppMod] [BodyInv] instance pass: {} MorInventoryComponent(s) on goat\n"), num);

    // [TOBI v1.15 2026-07-20] Native container init WORKS now (BP_NpcGoat
    // 'Inventory Comp' ships SH=Dwarf.Inventory + all 7 dwarf DefaultContainers;
    // rows are pak-live). If ANY comp already has containers — native-built
    // fresh goat OR record-restored goat — the whole legacy patch must stand
    // down: writing defs / AddItem onto the OTHER (blanked) comp would create
    // a parallel container set. Legacy path remains only for goats with zero
    // containers everywhere (pre-1.15 edge cases).
    {
        int32_t totalContainers = 0;
        for (int32_t i = 0; data && i < num && i < 8; i++)
        {
            UObject* c = *reinterpret_cast<UObject**>(data + i * 8);
            if (!c || !isObjectAlive(c)) continue;
            if (auto* gc = c->GetFunctionByNameInChain(STR("GetContainers")))
            {
                std::vector<uint8_t> gb(gc->GetParmsSize(), 0);
                try
                {
                    if (safeProcessEvent(c, gc, gb.data()))
                        if (auto* gr = findParam(gc, STR("ReturnValue")))
                            totalContainers += *reinterpret_cast<int32_t*>(gb.data() + gr->GetOffset_Internal() + 8);
                }
                catch (...)
                {
                }
            }
        }
        if (totalContainers > 0)
        {
            VLOG(STR("[MoriaCppMod] [BodyInv] {} native/restored container(s) present — legacy patch skipped (Tobi v1.15 native init)\n"),
                 totalContainers);
            return;
        }
    }
    for (int32_t i = 0; data && i < num && i < 8; i++)
    {
        UObject* c = *reinterpret_cast<UObject**>(data + i * 8);
        if (!c || !isObjectAlive(c)) continue;
        std::wstring nm;
        try
        {
            nm = c->GetName();
        }
        catch (...)
        {
            continue;
        }
        // [BAD-OBJECT SWEEP] DISABLED 2026-07-18 per user: no mod-side
        // deletion of inventory items — the epic-pack source is fixed at
        // the pak level (DefaultContainers nulled; Tobi to remove it
        // upstream in his editor). Function kept compiled for reference.
        // sweepGoatEpicPackItems(c);
        // current state
        auto hasContainers = [&]() -> int {
            if (auto* fn = c->GetFunctionByNameInChain(STR("HasContainers")))
            {
                std::vector<uint8_t> hb(fn->GetParmsSize(), 0);
                if (safeProcessEvent(c, fn, hb.data()))
                    if (auto* hr = findParam(fn, STR("ReturnValue"))) return (*(hb.data() + hr->GetOffset_Internal()) != 0) ? 1 : 0;
            }
            return -1;
        };
        int before = hasContainers();
        std::wstring sh;
        if (auto* shP = c->GetPropertyByNameInChain(STR("StorageHandle")))
        {
            RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
            try
            {
                sh = rn->ToString();
            }
            catch (...)
            {
            }
        }
        // [v5 2026-07-17] THE missing dwarf step, finally identified:
        // containers are created by ADDING the container ITEM into the
        // inventory (rc.92: AddItem was the ONLY operation that ever
        // created a goat container; v3 log: valid rows + construction +
        // ResetToStarting all still 0->0). Dwarf init AddItems its
        // DefaultContainers natively; our goat never runs that init —
        // so do the AddItems ourselves: dwarf defs + AddItem(body 6x6)
        // + AddItem(equip slots), Method sweep as in ensureGoatSlotContainer.
        UClass* classes[7] = {nullptr};
        int loaded = loadDwarfContainerClasses(classes);
        if (loaded > 0) writeDwarfDefsToComp(c, classes);
        // [SLOT-STRIP EXPERIMENT ENDED 2026-07-18] user verdict: removing the
        // equip-slot containers locks up the NPC UI — leave all 7 in place.
        // sweepGoatContainerItemsByPrefix(c, STR("BP_ContainerItem_Dwarf_Slot_"));
        auto containerCount = [&]() -> int32_t {
            auto* gc = c->GetFunctionByNameInChain(STR("GetContainers"));
            if (!gc) return -1;
            std::vector<uint8_t> gb(gc->GetParmsSize(), 0);
            try
            {
                safeProcessEvent(c, gc, gb.data());
            }
            catch (...)
            {
                return -1;
            }
            auto* gr = findParam(gc, STR("ReturnValue"));
            if (!gr) return -1;
            return *reinterpret_cast<int32_t*>(gb.data() + gr->GetOffset_Internal() + 8);
        };
        int32_t cntBefore = containerCount();
        // [NATIVE-PERSIST] a record-restored goat already HAS containers
        // (with contents!) — AddItem would duplicate them. Only build
        // containers on a genuinely fresh (empty) component.
        if (cntBefore > 0)
        {
            VLOG(STR("[MoriaCppMod] [BodyInv] instance comp '{}' already has {} container(s) — AddItem skipped (restored goat)\n"),
                 nm.c_str(), cntBefore);
            continue;
        }
        if (auto* af = c->GetFunctionByNameInChain(STR("AddItem")))
        {
            auto* pItem = findParam(af, STR("Item"));
            if (!pItem) pItem = findParam(af, STR("Class"));
            auto* pCount = findParam(af, STR("Count"));
            auto* pMeth = findParam(af, STR("Method"));
            for (int k = 0; k < 7; k++)
            {
                if (!classes[k]) continue;
                int32_t pre = containerCount();
                for (uint8_t m = 0; m <= 3; m++)
                {
                    std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                    if (pItem) *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = classes[k];
                    if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                    if (pMeth) ab[pMeth->GetOffset_Internal()] = m;
                    try
                    {
                        safeProcessEvent(c, af, ab.data());
                    }
                    catch (...)
                    {
                        break;
                    }
                    if (containerCount() > pre) break; // this class landed
                    if (!pMeth) break;
                }
            }
        }
        int after = hasContainers();
        VLOG(STR("[MoriaCppMod] [BodyInv] instance comp '{}' (SH was '{}' -> Dwarf.Inventory): HasContainers {} -> {}, containers {} -> {} (AddItem x{} dwarf classes)\n"),
             nm.c_str(), sh.c_str(), before, after, cntBefore, containerCount(), loaded);
        // [SLOT-STRIP EXPERIMENT ENDED 2026-07-18] see note above — all 7
        // containers stay (removal locked up the NPC UI).
        // sweepGoatContainerItemsByPrefix(c, STR("BP_ContainerItem_Dwarf_Slot_"));
    }
}

// ============================================================
// [SIDECAR v2 2026-07-17] GUID-keyed contents persistence for the
// dwarf-pattern goat (recovered from rc.119-124, which was user-
// confirmed working before the ephemeral pivot removed it).
// Containers are session-local (AddItem-built, entry-only — no actor,
// so Channel-C StoreRuntimeActor can't apply, and NpcInfo carries no
// inventory). Contents therefore persist via a per-GUID file:
//   snapshot: bell dismiss (before destroy), F12 save, 60s tick
//   restore:  after the spawn/adopt container build
// ============================================================
bool m_sidecarRestoreFailed{false};
ULONGLONG m_lastSidecarTickMs{0};

std::string sidecarClassPath(UClass* ic)
{
    if (!ic) return "";
    std::wstring full;
    try
    {
        full = ic->GetFullName();
    }
    catch (...)
    {
        return "";
    }
    std::string s = wideToUtf8(full);
    size_t sp = s.find_last_of(' ');
    if (sp != std::string::npos) s = s.substr(sp + 1);
    return s;
}

// GUID of a goat's MorNPCComponent as 32-hex (empty on failure).
std::string goatGuidHex(UObject* goat)
{
    do
    {
        if (!goat || !isObjectAlive(goat)) break;
        UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
        if (!npcCls) break;
        auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
        if (!getComp) break;
        std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
        writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), npcCls);
        if (!safeProcessEvent(goat, getComp, gb.data())) break;
        UObject* npcComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
        if (!npcComp || !isObjectAlive(npcComp)) break;
        uint8_t* g = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
        if (!g) break;
        const uint32_t* u = reinterpret_cast<const uint32_t*>(g);
        char buf[40];
        snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", u[0], u[1], u[2], u[3]);
        return buf;
    } while (false);
    return "";
}

// 32-hex GUID of the 'Rûdh' marker entry in NpcInfo (empty if none) —
// the IN-GAME registry key, used to organize per-world/per-goat files.
std::string findRudhMarkerGuidHex()
{
    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
            mgr = o;
            break;
        }
    if (!mgr) return "";
    uint8_t* hdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
    if (!isReadableMemory(hdr, 16)) return "";
    uint8_t* data = *reinterpret_cast<uint8_t**>(hdr);
    int32_t num = *reinterpret_cast<int32_t*>(hdr + 8);
    constexpr int kStride = 0x260, kGuidOff = 0x001c, kNameOff = 0x0030;
    if (!data || num <= 0 || num > 500 || !isReadableMemory(data, num * kStride)) return "";
    for (int i = 0; i < num; ++i)
    {
        uint8_t* entry = data + i * kStride;
        wchar_t nameBuf[256] = L"";
        seh_ftextToStringToBuf(entry + kNameOff, nameBuf, 256);
        if (!isGoatNameMatch(std::wstring(nameBuf))) continue;
        const uint32_t* u = reinterpret_cast<const uint32_t*>(entry + kGuidOff);
        char buf[40];
        snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", u[0], u[1], u[2], u[3]);
        return buf;
    }
    return "";
}

// [NATIVE-RESCUE TEST 2026-07-18] Raw-bytes variant of the Rûdh marker
// scan — same roster walk as findRudhMarkerGuidHex, but hands back the
// 16 GUID bytes for the native GUID-keyed NPC lifecycle RPCs.
bool findRudhMarkerGuidRaw(uint8_t out[16])
{
    UObject* mgr = nullptr;
    std::vector<UObject*> mgrs;
    if (findAllOfSafe(STR("MorNPCManager"), mgrs))
        for (UObject* o : mgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
            mgr = o;
            break;
        }
    if (!mgr) return false;
    uint8_t* hdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
    if (!isReadableMemory(hdr, 16)) return false;
    uint8_t* data = *reinterpret_cast<uint8_t**>(hdr);
    int32_t num = *reinterpret_cast<int32_t*>(hdr + 8);
    constexpr int kStride = 0x260, kGuidOff = 0x001c, kNameOff = 0x0030;
    if (!data || num <= 0 || num > 500 || !isReadableMemory(data, num * kStride)) return false;
    for (int i = 0; i < num; ++i)
    {
        uint8_t* entry = data + i * kStride;
        wchar_t nameBuf[256] = L"";
        seh_ftextToStringToBuf(entry + kNameOff, nameBuf, 256);
        if (!isGoatNameMatch(std::wstring(nameBuf))) continue;
        std::memcpy(out, entry + kGuidOff, 16);
        return true;
    }
    return false;
}

// [NATIVE-RECALL 2026-07-18] First active settlement id (0 = none).
// ActiveSettlements is a plain TArray<uint32> UPROPERTY on the manager.
uint32_t readFirstActiveSettlementId()
{
    UObject* smgr = nullptr;
    std::vector<UObject*> smgrs;
    if (findAllOfSafe(STR("MorSettlementManager"), smgrs))
        for (UObject* o : smgrs)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
            smgr = o;
            break;
        }
    if (!smgr) return 0;
    auto* arr = smgr->GetValuePtrByPropertyNameInChain<uint8_t>(STR("ActiveSettlements"));
    if (!arr) return 0;
    uint32_t* ids = *reinterpret_cast<uint32_t**>(arr);
    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
    if (!ids || num <= 0 || num >= 64 || !isReadableMemory(ids, num * 4)) return 0;
    return ids[0];
}

// [NATIVE-RECALL 2026-07-18] The proven persistence core, shared by the
// bell recall, the auto-anchor at registration, and the NUM8 harness:
//   ServerRescueNpc(guid, settlement) — on a LIVE goat: assigns it to the
//     settlement (persistence anchor; log-proven 10:49). On a MISSING
//     goat: respawns it from its actor record WITH INVENTORY at the
//     settlement (log-proven 15:23 — rescue brought back a goat carrying
//     the previous session's full load).
//   ServerNpcSetRole(guid, DT_NPCRoles 'Porter') — native roster role so
//     the settlement schedule's Work state dispatches the Porter follow
//     tree itself (nothing for the schedule FSM to stomp).
// [ROLE-FIX 2026-07-22] Native authoritative Porter-role assert, split out
// of callGoatRescueAndRole so the per-session adopt one-shot can use it too.
// The raw roster CurrentRole memcpy is invisible to replication — any entry
// refresh (settlement revalidation, HandleRoleUpdate, MP resync) flips the
// display back to Default ("Citizen") and kills the role-dispatched follow.
// ServerNpcSetRole makes Porter the server's own truth.
bool callGoatSetPorterRole(const uint8_t guid[16])
{
    if (!m_localPC || !isObjectAlive(m_localPC)) return false;
    if (auto* roleFn = m_localPC->GetFunctionByNameInChain(STR("ServerNpcSetRole")))
    {
        UObject* rolesDT = nullptr;
        try
        {
            std::vector<UObject*> dts;
            if (findAllOfSafe(STR("DataTable"), dts))
                for (UObject* t : dts)
                {
                    if (!t || !isObjectAlive(t)) continue;
                    if (safeObjectName(t) == STR("DT_NPCRoles"))
                    {
                        rolesDT = t;
                        break;
                    }
                }
        }
        catch (...)
        {
        }
        std::vector<uint8_t> rb(roleFn->GetParmsSize(), 0);
        if (auto* pId = findParam(roleFn, STR("NpcId")))
            std::memcpy(rb.data() + pId->GetOffset_Internal(), guid, 16);
        if (auto* pRole = findParam(roleFn, STR("NewRole")))
        {
            uint8_t* h = rb.data() + pRole->GetOffset_Internal();
            *reinterpret_cast<UObject**>(h) = rolesDT;
            RC::Unreal::FName porter(STR("Porter"), RC::Unreal::FNAME_Add);
            std::memcpy(h + 8, &porter, 8);
        }
        bool rok = safeProcessEvent(m_localPC, roleFn, rb.data());
        VLOG(STR("[MoriaCppMod] [NativeRescue] ServerNpcSetRole(Porter, dt={:p}) pe={}\n"), (void*)rolesDT, rok ? STR("OK") : STR("FAIL"));
        return rok;
    }
    VLOG(STR("[MoriaCppMod] [NativeRescue] ServerNpcSetRole NOT FOUND on PC\n"));
    return false;
}

// [NATIVE-RECALL 2026-07-18] rescue-to-settlement + native Porter role.
// On a LIVE goat the rescue is a pure settlement assignment; on a MISSING
// goat it respawns the record goat WITH inventory (log-proven 15:23).
bool callGoatRescueAndRole(const uint8_t guid[16], uint32_t settlementId)
{
    if (!m_localPC || !isObjectAlive(m_localPC) || settlementId == 0) return false;
    auto* fn = m_localPC->GetFunctionByNameInChain(STR("ServerRescueNpc"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [NativeRescue] ServerRescueNpc NOT FOUND on PC\n"));
        return false;
    }
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* pGuid = findParam(fn, STR("NpcGuid")))
        std::memcpy(b.data() + pGuid->GetOffset_Internal(), guid, 16);
    writeGoatParm<uint32_t>(fn, b.data(), STR("SettlementId"), settlementId);
    bool ok = safeProcessEvent(m_localPC, fn, b.data());
    VLOG(STR("[MoriaCppMod] [NativeRescue] ServerRescueNpc(settlement={}) pe={}\n"), settlementId, ok ? STR("OK") : STR("FAIL"));
    callGoatSetPorterRole(guid);
    return ok;
}

// Armed by the bell's native recall: when the rescued record goat streams
// in and tickAdoptNativeGoat links it, CALL it straight to the player.
ULONGLONG m_recallCallUntilMs{0};

// Teleport a goat to the player's own position (small Z lift — the old
// +150/+150 offset landed in geometry and killed the courier).
void teleportGoatToPlayer(UObject* g)
{
    if (!g || !isObjectAlive(g)) return;
    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    if (!pawn) return;
    if (auto* getLoc = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
    {
        std::vector<uint8_t> lb(getLoc->GetParmsSize(), 0);
        if (safeProcessEvent(pawn, getLoc, lb.data()))
            if (auto* pr = findParam(getLoc, STR("ReturnValue")))
            {
                float* v = reinterpret_cast<float*>(lb.data() + pr->GetOffset_Internal());
                npcTeleportPawn(g, v[0], v[1], v[2] + 60.0f);
            }
    }
}

// [NATIVE-RESCUE TEST 2026-07-18] Harness for the live-captured native
// dwarf dismiss/recall machinery (see npc-dismiss-recall-native-api
// memory): AMorPlayerController Server RPCs, GUID-keyed via the roster.
//   NUM8       -> ServerRescueNpc(goatGuid, firstActiveSettlementId)
//   Ctrl+NUM8  -> ServerDismissNpc(goatGuid)
void goatNativeLifecycleTest(bool dismiss)
{
    uint8_t guid[16] = {0};
    if (!findRudhMarkerGuidRaw(guid))
    {
        VLOG(STR("[MoriaCppMod] [NativeRescue] no Rûdh marker in roster — register the goat first (ring bell)\n"));
        showOnScreen(L"No goat in roster", 2.0f, 0.9f, 0.6f, 0.6f);
        return;
    }
    const uint32_t* gu = reinterpret_cast<const uint32_t*>(guid);
    VLOG(STR("[MoriaCppMod] [NativeRescue] roster goat GUID {:08X}{:08X}{:08X}{:08X}\n"), gu[0], gu[1], gu[2], gu[3]);
    if (!m_localPC || !isObjectAlive(m_localPC))
    {
        VLOG(STR("[MoriaCppMod] [NativeRescue] no local PlayerController\n"));
        return;
    }
    UObject* liveBefore = findOurGoatAlive();
    VLOG(STR("[MoriaCppMod] [NativeRescue] live goat BEFORE: {:p}\n"), (void*)liveBefore);

    if (dismiss)
    {
        auto* fn = m_localPC->GetFunctionByNameInChain(STR("ServerDismissNpc"));
        if (!fn)
        {
            VLOG(STR("[MoriaCppMod] [NativeRescue] ServerDismissNpc NOT FOUND on PC\n"));
            return;
        }
        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
        if (auto* pGuid = findParam(fn, STR("NpcGuid")))
            std::memcpy(b.data() + pGuid->GetOffset_Internal(), guid, 16);
        bool ok = safeProcessEvent(m_localPC, fn, b.data());
        VLOG(STR("[MoriaCppMod] [NativeRescue] ServerDismissNpc pe={}\n"), ok ? STR("OK") : STR("FAIL"));
        showOnScreen(L"Native DISMISS sent", 2.0f, 0.9f, 0.8f, 0.5f);
        return;
    }

    // Rescue + Porter role via the shared core.
    uint32_t settlementId = readFirstActiveSettlementId();
    if (settlementId == 0)
    {
        VLOG(STR("[MoriaCppMod] [NativeRescue] no ACTIVE settlement — place/activate a settlement stone first\n"));
        showOnScreen(L"No active settlement — place a settlement stone", 2.5f, 0.9f, 0.6f, 0.6f);
        return;
    }
    callGoatRescueAndRole(guid, settlementId);
    showOnScreen(L"Native RESCUE + Porter role sent", 2.0f, 0.7f, 0.9f, 0.7f);
}

std::string goatSidecarPath(UObject* goat)
{
    std::string guid = "default";
    do
    {
        if (!goat || !isObjectAlive(goat)) break;
        UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
        if (!npcCls) break;
        auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
        if (!getComp) break;
        std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
        writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), npcCls);
        if (!safeProcessEvent(goat, getComp, gb.data())) break;
        UObject* npcComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
        if (!npcComp || !isObjectAlive(npcComp)) break;
        uint8_t* g = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
        if (!g) break;
        const uint32_t* u = reinterpret_cast<const uint32_t*>(g);
        char buf[40];
        snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", u[0], u[1], u[2], u[3]);
        guid = buf;
    } while (false);
    return modPath("Mods/MoriaCppMod/goat-saddlebag-" + guid + ".txt");
}

// All goat MorInventoryComponents that currently have containers.
void goatInvCompsWithContainers(UObject* goat, std::vector<UObject*>& out)
{
    out.clear();
    if (!goat || !isObjectAlive(goat)) return;
    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/FGK.MorInventoryComponent"));
    if (!invCls) invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
    if (!invCls) return;
    auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass"));
    if (!gf) gf = goat->GetFunctionByNameInChain(STR("GetComponentsByClass"));
    if (!gf) return;
    std::vector<uint8_t> b(gf->GetParmsSize(), 0);
    if (auto* pCls = findParam(gf, STR("ComponentClass"))) *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
    try
    {
        safeProcessEvent(goat, gf, b.data());
    }
    catch (...)
    {
        return;
    }
    auto* pRet = findParam(gf, STR("ReturnValue"));
    if (!pRet) return;
    uint8_t* arr = b.data() + pRet->GetOffset_Internal();
    UObject** data = *reinterpret_cast<UObject***>(arr);
    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
    for (int32_t i = 0; data && i < num && i < 16; i++)
    {
        UObject* c = data[i];
        if (!c || !isObjectAlive(c)) continue;
        auto* hc = c->GetFunctionByNameInChain(STR("HasContainers"));
        if (!hc) continue;
        std::vector<uint8_t> hb(hc->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(c, hc, hb.data());
        }
        catch (...)
        {
            continue;
        }
        if (auto* pr = findParam(hc, STR("ReturnValue")))
            if (*reinterpret_cast<bool*>(hb.data() + pr->GetOffset_Internal())) out.push_back(c);
    }
}

// Append one component's loose stacks (skips container items) to `lines`.
int collectCompStacks(UObject* goatInv, std::vector<std::string>& lines)
{
    FProperty* itemsProp = goatInv->GetPropertyByNameInChain(STR("Items"));
    if (!itemsProp) return 0;
    uint8_t* listBase = reinterpret_cast<uint8_t*>(goatInv) + itemsProp->GetOffset_Internal() + iiaListOff();
    if (!isReadableMemory(listBase, 16)) return 0;
    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
    int32_t arrNum = *reinterpret_cast<int32_t*>(listBase + 8);
    if (!arrData || arrNum < 0 || arrNum > 10000) return 0;
    const int stride = iiSize(), itemOff = iiItemOff();
    const int countOff = 0x18, csOff = 0x2C;
    int written = 0;
    for (int32_t i = 0; i < arrNum; i++)
    {
        uint8_t* entry = arrData + i * stride;
        if (!isReadableMemory(entry, stride)) continue;
        if (*reinterpret_cast<int32_t*>(entry + csOff) > 0) continue; // container item itself
        UClass* ic = *reinterpret_cast<UClass**>(entry + itemOff);
        if (!ic || !isObjectAlive(ic)) continue;
        int32_t cnt = *reinterpret_cast<int32_t*>(entry + countOff);
        if (cnt <= 0) continue;
        std::string cpath = sidecarClassPath(ic);
        if (cpath.empty()) continue;
        lines.push_back(cpath + "|" + std::to_string(cnt));
        written++;
    }
    return written;
}

void snapshotGoatSaddlebag()
{
    UObject* goat = nullptr;
    for (auto& g : m_followGoats)
    {
        UObject* p = g.pawn.Get();
        if (p && isObjectAlive(p))
        {
            goat = p;
            break;
        }
    }
    if (!goat) return;
    std::vector<UObject*> comps;
    goatInvCompsWithContainers(goat, comps);
    if (comps.empty()) return;
    std::vector<std::string> lines;
    for (auto* c : comps) collectCompStacks(c, lines);
    // [rc.122 clobber guard] never overwrite a good sidecar with an empty
    // bag right after a failed restore.
    if (lines.empty() && m_sidecarRestoreFailed)
    {
        VLOG(STR("[MoriaCppMod] [Sidecar] snapshot SKIPPED (bag empty + last restore failed)\n"));
        return;
    }
    m_sidecarRestoreFailed = false;
    std::string path = goatSidecarPath(goat);
    std::ofstream f = openOutputFile(path, std::ios::trunc);
    if (!f.is_open())
    {
        VLOG(STR("[MoriaCppMod] [Sidecar] snapshot FAILED to open file\n"));
        return;
    }
    for (auto& l : lines) f << l << "\n";
    f.close();
    VLOG(STR("[MoriaCppMod] [Sidecar] snapshot: {} stack(s) -> {}\n"), (int)lines.size(), utf8ToWide(path).c_str());
}

void restoreGoatSaddlebagFromSidecar(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    std::vector<UObject*> comps;
    goatInvCompsWithContainers(goat, comps);
    if (comps.empty())
    {
        VLOG(STR("[MoriaCppMod] [Sidecar] restore: no containers on goat — skipped\n"));
        return;
    }
    UObject* goatInv = comps[0];
    std::string path = goatSidecarPath(goat);
    std::ifstream f(utf8PathToWide(path));
    if (!f.is_open())
    {
        VLOG(STR("[MoriaCppMod] [Sidecar] no sidecar file — nothing to restore\n"));
        return;
    }
    auto* af = goatInv->GetFunctionByNameInChain(STR("AddItem"));
    if (!af) return;
    auto* pItem = findParam(af, STR("Item"));
    if (!pItem) pItem = findParam(af, STR("Class"));
    auto* pCount = findParam(af, STR("Count"));
    int restored = 0, failed = 0;
    std::string line;
    while (std::getline(f, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        std::string cpath = line.substr(0, bar);
        int cnt = atoi(line.c_str() + bar + 1);
        size_t sp = cpath.find_last_of(' ');
        if (sp != std::string::npos) cpath = cpath.substr(sp + 1);
        if (cpath.empty() || cnt <= 0) continue;
        std::wstring wpath = utf8ToWide(cpath);
        UClass* cc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, wpath.c_str());
        if (!cc) cc = goat_loadClassAssetBlocking(wpath.c_str());
        if (!cc || !isObjectAlive(cc))
        {
            failed++;
            continue;
        }
        std::vector<uint8_t> ab(af->GetParmsSize(), 0);
        if (pItem) *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = cc;
        if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = cnt;
        try
        {
            safeProcessEvent(goatInv, af, ab.data());
            restored++;
        }
        catch (...)
        {
            failed++;
        }
    }
    VLOG(STR("[MoriaCppMod] [Sidecar] restore: {} stack(s) re-added, {} failed\n"), restored, failed);
    m_sidecarRestoreFailed = (failed > 0);
    if (restored > 0) showOnScreen(L"Saddlebag contents restored", 2.0f, 0.4f, 0.9f, 0.4f);
}

// [WORLDSTORE 2026-07-17] Sidecar REJECTED by user — native save only.
// Save-file forensics verdict: the game persists NO NPC inventory (Nithi's
// star ingots absent from the save; goat's silver absent; the NPC record
// struct has no inventory field). The ONE untested native mechanism is the
// generic runtime-actor store: StoreRuntimeActor provably round-trips
// actors (rc.116 restored a stored prop), UInventoryComponent implements
// IFGKSaveGameObject, and every FItemInstance field is SaveGame-flagged.
// Experiment: store the goat actor periodically + at dismiss; on reload,
// does the level record respawn it WITH contents?
uint8_t m_goatStoreHandle[0x20]{};
bool m_goatStoredOnce{false};
void storeGoatToWorldState(UObject* goat, const wchar_t* tag)
{
    if (!goat || !isObjectAlive(goat)) return;
    UObject* ws = nullptr;
    auto* libFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Moria.MorSaveSystemBlueprintLibrary:GetSaveSystemWorldState"));
    auto* libCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorSaveSystemBlueprintLibrary"));
    if (libFn && libCDO)
    {
        std::vector<uint8_t> b(libFn->GetParmsSize(), 0);
        if (safeProcessEvent(libCDO, libFn, b.data()))
            ws = readGoatParm<UObject*>(libFn, b.data(), STR("ReturnValue"), nullptr);
    }
    if (!ws || !isObjectAlive(ws))
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] WorldState unavailable ({})\n"), tag);
        return;
    }
    auto* fn = ws->GetFunctionByNameInChain(STR("StoreRuntimeActor"));
    if (!fn) return;
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* p = findParam(fn, STR("Actor"))) *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = goat;
    auto* ph = findParam(fn, STR("InOutRuntimeActorHandle"));
    if (ph) std::memcpy(b.data() + ph->GetOffset_Internal(), m_goatStoreHandle, 0x20);
    if (auto* p = findParam(fn, STR("bStoreStability"))) *(b.data() + p->GetOffset_Internal()) = 0;
    bool ok = safeProcessEvent(ws, fn, b.data());
    bool ret = false;
    if (auto* pr = findParam(fn, STR("ReturnValue"))) ret = *(b.data() + pr->GetOffset_Internal()) != 0;
    if (ph) std::memcpy(m_goatStoreHandle, b.data() + ph->GetOffset_Internal(), 0x20);
    wchar_t hex[80];
    int off = 0;
    for (int i = 0; i < 0x20 && off < 76; i++) off += swprintf(hex + off, 80 - off, L"%02X", m_goatStoreHandle[i]);
    VLOG(STR("[MoriaCppMod] [WorldStore] StoreRuntimeActor({}) pe={} ret={} handle={}\n"), tag, ok, ret, hex);
    if (ret)
    {
        m_goatStoredOnce = true;
        // [RecordProbe 2026-07-18] persist the record handle across
        // sessions — next load calls GetRuntimeActorFromHandle with it
        // ("store the guid and call it back", literally).
        std::string ghex = goatGuidHex(goat);
        std::ofstream f = openOutputFile(modPath("Mods/MoriaCppMod/goat-record-" + (ghex.empty() ? std::string("default") : ghex) + ".txt"), std::ios::trunc);
        if (f.is_open())
        {
            char ahex[70];
            int ao = 0;
            for (int i = 0; i < 0x20; i++) ao += snprintf(ahex + ao, sizeof(ahex) - ao, "%02X", m_goatStoreHandle[i]);
            f << ahex << "\n";
            f.close();
        }
    }
}

// [RecordProbe 2026-07-18] one-shot at +20s after load: read the persisted
// record handle and ask the save system for the actor back. Answers the
// central unknown: does the level record hold the goat across sessions,
// and does GetRuntimeActorFromHandle resurrect/return it?
bool m_recordProbeDone{false};
void probeGoatRecordHandle()
{
    if (m_recordProbeDone) return;
    m_recordProbeDone = true;
    // [DOOR-1 REVALIDATION] trap telemetry: did ValidNpcRestores populate
    // late (it caches at manager BeginPlay; our runtime row lands at +5s)?
    VLOG(STR("[MoriaCppMod] [RecordProbe] ValidNpcRestores count at +20s = {}\n"), readValidNpcRestoresCount());
    // In-game registry key: the roster GUID names the handle file, so
    // multiple worlds/characters can never collide.
    std::string ghex = findRudhMarkerGuidHex();
    if (ghex.empty())
    {
        VLOG(STR("[MoriaCppMod] [RecordProbe] no Rûdh marker in NpcInfo — skip\n"));
        return;
    }
    std::ifstream f(utf8PathToWide(modPath("Mods/MoriaCppMod/goat-record-" + ghex + ".txt")));
    if (!f.is_open())
    {
        VLOG(STR("[MoriaCppMod] [RecordProbe] no persisted handle for this GUID — skip\n"));
        return;
    }
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.size() < 0x40)
    {
        VLOG(STR("[MoriaCppMod] [RecordProbe] handle file malformed\n"));
        return;
    }
    uint8_t handle[0x20];
    for (int i = 0; i < 0x20; i++)
    {
        unsigned v = 0;
        sscanf(line.c_str() + i * 2, "%2X", &v);
        handle[i] = (uint8_t)v;
    }
    UObject* ws = nullptr;
    auto* libFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Moria.MorSaveSystemBlueprintLibrary:GetSaveSystemWorldState"));
    auto* libCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Moria.Default__MorSaveSystemBlueprintLibrary"));
    if (libFn && libCDO)
    {
        std::vector<uint8_t> b(libFn->GetParmsSize(), 0);
        if (safeProcessEvent(libCDO, libFn, b.data()))
            ws = readGoatParm<UObject*>(libFn, b.data(), STR("ReturnValue"), nullptr);
    }
    if (!ws || !isObjectAlive(ws))
    {
        VLOG(STR("[MoriaCppMod] [RecordProbe] WorldState unavailable\n"));
        return;
    }
    auto* fn = ws->GetFunctionByNameInChain(STR("GetRuntimeActorFromHandle"));
    if (!fn)
    {
        VLOG(STR("[MoriaCppMod] [RecordProbe] GetRuntimeActorFromHandle missing\n"));
        return;
    }
    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
    if (auto* p = findParam(fn, STR("ActorHandle"))) std::memcpy(b.data() + p->GetOffset_Internal(), handle, 0x20);
    bool ok = safeProcessEvent(ws, fn, b.data());
    UObject* actor = nullptr;
    bool valid = false;
    if (auto* pr = findParam(fn, STR("ReturnValue"))) actor = *reinterpret_cast<UObject**>(b.data() + pr->GetOffset_Internal());
    if (auto* pv = findParam(fn, STR("bActorIsValid"))) valid = *(b.data() + pv->GetOffset_Internal()) != 0;
    std::wstring cls = actor ? safeClassName(actor) : STR("(null)");
    VLOG(STR("[MoriaCppMod] [RecordProbe] GetRuntimeActorFromHandle: pe={} valid={} actor={:p} cls={}\n"),
         ok, valid, (void*)actor, cls.c_str());
    // Seed the live store handle from the persisted one so this session's
    // StoreRuntimeActor calls UPDATE the same record instead of minting a
    // new one per session (prevents stale-record buildup in the save).
    if (valid) std::memcpy(m_goatStoreHandle, handle, 0x20);
}

// Periodic native store (was the sidecar tick; sidecar rejected).
void tickSidecarSnapshot()
{
    if (!m_characterLoaded) return;
    ULONGLONG now = GetTickCount64();
    if (now - m_lastSidecarTickMs < 60000) return;
    m_lastSidecarTickMs = now;
    UObject* goat = nullptr;
    for (auto& g : m_followGoats)
    {
        UObject* p = g.pawn.Get();
        if (p && isObjectAlive(p))
        {
            goat = p;
            break;
        }
    }
    if (goat) storeGoatToWorldState(goat, STR("60s tick"));
}

// [DwarfBag probe 2026-07-17] one-shot dump of every settlement dwarf's
// inventory stacks (GUID-tagged) — run once per session; compare across
// relogin to learn whether the GAME persists NPC dwarf bag contents.
int m_dwarfProbeTries{0};
ULONGLONG m_dwarfProbeNextMs{0};
int probeDwarfBagContents()
{
    std::vector<UObject*> dwarves;
    // [fix] settlement dwarves are BP_NpcDwarf_NoLink etc., and they stream
    // in late — caller retries until at least one is found.
    for (const wchar_t* cls : {STR("BP_NpcDwarf_C"), STR("BP_NpcDwarf_NoLink_C"), STR("BP_NpcDwarf_Survivor_C"),
                               STR("BP_NpcDwarf_Recruit_C"), STR("BP_NpcDwarf_Wanderer_C"),
                               STR("BP_NpcDwarf_Wanderer_RecruitAndSettlement_C")})
    {
        std::vector<UObject*> found;
        findAllOfSafe(cls, found);
        dwarves.insert(dwarves.end(), found.begin(), found.end());
    }
    int logged = 0;
    for (auto* d : dwarves)
    {
        if (!d || !isObjectAlive(d) || logged >= 25) continue;
        std::wstring nm;
        try
        {
            nm = d->GetName();
        }
        catch (...)
        {
            continue;
        }
        if (nm.rfind(STR("Default__"), 0) == 0) continue;
        std::vector<UObject*> comps;
        goatInvCompsWithContainers(d, comps);
        std::vector<std::string> lines;
        for (auto* c : comps) collectCompStacks(c, lines);
        std::string guid = "?";
        {
            UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            auto* getComp = npcCls ? d->GetFunctionByNameInChain(STR("GetComponentByClass")) : nullptr;
            if (getComp)
            {
                std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
                writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), npcCls);
                if (safeProcessEvent(d, getComp, gb.data()))
                    if (UObject* nc = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr))
                        if (uint8_t* g = nc->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid")))
                        {
                            char buf[40];
                            const uint32_t* u = reinterpret_cast<const uint32_t*>(g);
                            snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", u[0], u[1], u[2], u[3]);
                            guid = buf;
                        }
            }
        }
        VLOG(STR("[MoriaCppMod] [DwarfBag] '{}' GUID={} comps={} stacks={}\n"),
             nm.c_str(), utf8ToWide(guid).c_str(), (int)comps.size(), (int)lines.size());
        for (auto& l : lines) VLOG(STR("[MoriaCppMod] [DwarfBag]    {}\n"), utf8ToWide(l).c_str());
        logged++;
    }
    VLOG(STR("[MoriaCppMod] [DwarfBag] === {} dwarf(s) dumped ===\n"), logged);
    return logged;
}

// Retry wrapper: dwarves stream in late — probe every 10s until found.
void tickDwarfBagProbe()
{
    if (!m_characterLoaded || m_dwarfProbeTries >= 12) return;
    ULONGLONG now = GetTickCount64();
    if (now < m_dwarfProbeNextMs) return;
    m_dwarfProbeNextMs = now + 10000;
    m_dwarfProbeTries++;
    if (probeDwarfBagContents() > 0) m_dwarfProbeTries = 12; // done this session
}

// Spawn-tail identity pass: ADOPT existing marker GUID or REGISTER,
// then write Name + UniqueNpc.RowName into our NpcInfo entry.
void adoptOrRegisterGoatIdentity(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    UObject* npcComp = nullptr;
    UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
    if (npcCls)
    {
        if (auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
        {
            std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), npcCls);
            if (safeProcessEvent(goat, getComp, gb.data()))
                npcComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
        }
    }
    if (!npcComp || !isObjectAlive(npcComp))
    {
        VLOG(STR("[MoriaCppMod] [NPC-REG] no MorNPCComponent on goat — register skipped\n"));
        return;
    }
        // [rc.112 PERSIST IDENTITY 2026-07-11] Full identity pass
        // (reconnected from the pre-strip rc.42/50/85 machinery):
        //   1. ADOPT: scan NpcInfo (stride 0x260, Name FText @+0x30,
        //      GUID @+0x1C) backwards for Name==m_goatName — an entry
        //      from a previous session. Found → copy its GUID into
        //      npcComp.NpcGuid and SKIP Register (no duplicates).
        //   2. Else REGISTER (mints GUID + should append an entry —
        //      count logged to detect the ValidNpcRestores trap).
        //   3. Write Name=m_goatName + UniqueNpc.RowName='NPCGoat'
        //      into our entry → manager reload lookup resolves via
        //      Tobi's DT_NPCUniqueCharacters['NPCGoat'] → native respawn.
        auto findMgr = [&]() -> UObject* {
            UObject* mgr = nullptr;
            std::vector<UObject*> mgrs;
            if (findAllOfSafe(STR("MorNPCManager"), mgrs))
                for (UObject* o : mgrs)
                {
                    if (!o || !isObjectAlive(o)) continue;
                    std::wstring cn = safeClassName(o);
                    if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
                    mgr = o; break;
                }
            return mgr;
        };
        auto npcInfoCount = [&]() -> int32_t {
            UObject* mgr = findMgr();
            if (!mgr) return -1;
            uint8_t* hdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
            if (!isReadableMemory(hdr, 16)) return -1;
            return *reinterpret_cast<int32_t*>(hdr + 8);
        };

        bool adopted = false;
        uint8_t* myGuidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
        {
            UObject* mgr = findMgr();
            if (mgr && myGuidPtr)
            {
                uint8_t* hdr = reinterpret_cast<uint8_t*>(mgr) + 0x03a0 + 0x0108;
                if (isReadableMemory(hdr, 16))
                {
                    uint8_t* data = *reinterpret_cast<uint8_t**>(hdr);
                    int32_t  num  = *reinterpret_cast<int32_t*>(hdr + 8);
                    constexpr int kStride = 0x260, kGuidOff = 0x001c, kNameOff = 0x0030;
                    if (data && num > 0 && num < 500)
                    {
                        for (int i = num - 1; i >= 0 && !adopted; --i)
                        {
                            uint8_t* entry = data + i * kStride;
                            if (!isReadableMemory(entry, kStride)) continue;
                            wchar_t tmpName[256] = {0};
                            seh_ftextToStringToBuf(entry + kNameOff, tmpName, 256);
                            // [rc.113] list every entry — proves the NpcInfo↔DT-row
                            // mapping (12 entries == 12 vanilla unique rows; goat row
                            // missing because world predates it).
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.113] NpcInfo[{}] Name='{}'\n"),
                                 i, tmpName);
                            if (isGoatNameMatch(std::wstring(tmpName)))  // [v8.2.x] tolerates mojibake variant
                            {
                                std::memcpy(myGuidPtr, entry + kGuidOff, 16);
                                adopted = true;
                                uint32_t* g = reinterpret_cast<uint32_t*>(entry + kGuidOff);
                                VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.112 ADOPT] NpcInfo[{}] Name='{}' GUID={:08X}-{:08X}-{:08X}-{:08X} adopted into npcComp\n"),
                                     i, m_goatName.c_str(), g[0], g[1], g[2], g[3]);
                            }
                        }
                    }
                }
            }
        }

        int32_t cnt0 = npcInfoCount();
        if (!adopted)
        {
            if (auto* regFn = npcComp->GetFunctionByNameInChain(STR("RegisterWithNPCManager")))
            {
                std::vector<uint8_t> rb(regFn->GetParmsSize(), 0);
                try { safeProcessEvent(npcComp, regFn, rb.data()); } catch (...) {}
            }
        }
        int32_t cnt1 = npcInfoCount();
        VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.112] adopted={} NpcInfo count {}->{} ({})\n"),
             adopted, cnt0, cnt1,
             adopted ? STR("Register SKIPPED") : (cnt1 > cnt0 ? STR("Register CREATED entry") : STR("Register created NOTHING — trap?")));

        // 3. identity writes (idempotent for the adopt path)
        if (myGuidPtr)
        {
            bool nOk = writeGoatNameDirectToNpcInfoEntry(myGuidPtr, m_goatName);
            bool rOk = writeUniqueNpcRowNameToEntry(myGuidPtr, STR("NPCGoat"));
            VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.112] identity writes: Name='{}' ok={} UniqueNpc.RowName='NPCGoat' ok={}\n"),
                 m_goatName.c_str(), nOk, rOk);
            // [NATIVE-AI 2026-07-18] BT decode: the root FSM's
            // MorBehaviorState_Role dispatches the Porter work tree from
            // the ROSTER CurrentRole (PersistentData @+0x38, RowName @+8)
            // — component-level role writes never reached it. Write
            // 'Porter' into the entry so WorkTime→Role→Porter can run.
            {
                UObject* mgrR = findMgr();
                if (mgrR)
                {
                    uint8_t* hdr2 = reinterpret_cast<uint8_t*>(mgrR) + 0x03a0 + 0x0108;
                    if (isReadableMemory(hdr2, 16))
                    {
                        uint8_t* data2 = *reinterpret_cast<uint8_t**>(hdr2);
                        int32_t num2 = *reinterpret_cast<int32_t*>(hdr2 + 8);
                        constexpr int kStride2 = 0x260, kGuidOff2 = 0x001c;
                        for (int i2 = 0; data2 && i2 < num2 && num2 < 500; i2++)
                        {
                            uint8_t* entry2 = data2 + i2 * kStride2;
                            if (!isReadableMemory(entry2, kStride2)) continue;
                            if (std::memcmp(entry2 + kGuidOff2, myGuidPtr, 16) != 0) continue;
                            RC::Unreal::FName porterRow(STR("Porter"), RC::Unreal::FNAME_Add);
                            // entry + 0x10 (PersistentData) + 0x38 (CurrentRole) + 8 (RowName)
                            std::memcpy(entry2 + 0x10 + 0x38 + 8, &porterRow, 8);
                            VLOG(STR("[MoriaCppMod] [NativeAI] roster CurrentRole='Porter' written to NpcInfo[{}]\n"), i2);
                            break;
                        }
                    }
                }
            }
            // [NATIVE-RECALL 2026-07-18] AUTO-ANCHOR: settle the goat +
            // native Porter role right at registration. Settlement
            // membership is the ONLY channel that keeps the actor record
            // restorable (unsettled goats never come back — proven across
            // four reload tests); on a LIVE goat the rescue is a pure
            // assignment (no respawn, log-proven 10:49).
            {
                uint32_t sid = readFirstActiveSettlementId();
                if (sid != 0)
                {
                    bool aok = callGoatRescueAndRole(myGuidPtr, sid);
                    VLOG(STR("[MoriaCppMod] [BellSpawn] auto-anchor to settlement {} -> {}\n"), sid, aok);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [BellSpawn] auto-anchor SKIPPED — no active settlement (goat will NOT persist until one exists)\n"));
                    showOnScreen(L"No rally stone — Rûdh won't survive a reload yet", 3.0f, 0.9f, 0.8f, 0.4f);
                }
            }
        }
        else VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.112] NpcGuid property NOT FOUND — identity writes skipped\n"));

}

// Bell-rung goat spawn: BeginDeferred + FinishSpawning, tracked in
// m_followGoats with bellSpawned=true so the tick loop drives the
// MoveToActor follow.
void spawnBellGoat()
{
    VLOG(STR("[MoriaCppMod] [BellSpawn] entry — checking bindings\n"));
    if (!ensureGoatSpawnBindings())
    {
        VLOG(STR("[MoriaCppMod] [BellSpawn] bindings not ready\n"));
        showOnScreen(L"Goat asset not loaded", 2.0f, 0.9f, 0.4f, 0.4f);
        return;
    }
    VLOG(STR("[MoriaCppMod] [BellSpawn] bindings OK: cls={:p} begin={:p} finish={:p} cdo={:p}\n"),
         (void*)m_goatBPClass,
         (void*)m_goatBeginSpawnFn,
         (void*)m_goatFinishSpawnFn,
         (void*)m_kismetGameplayStaticsCDO);

    // [NPC-REG v4] the goat is a DWARF-PATTERN NPC (Tobi's epic-pack
    // design retired per user — "his did not work"): patch the component
    // TEMPLATES to dwarf defs before the spawn so this instance inherits
    // an instantiable 6x6 body inventory. Row synthesis kept as belt-and-
    // suspenders for anything still referencing Goat.Slot.EpicPack.
    ensureGoatStorageRows();
    ensureGoatBodyInventoryArchetype();

    UObject* pawn = m_localPawn ? m_localPawn : getPawn();
    if (!pawn || !isObjectAlive(pawn))
    {
        VLOG(STR("[MoriaCppMod] [BellSpawn] no pawn\n"));
        showOnScreen(L"No player pawn", 2.0f, 0.9f, 0.4f, 0.4f);
        return;
    }

    FVec3f loc = getPawnLocation();
    FVec3f fwd{1.0f, 0.0f, 0.0f};
    if (auto* fwdFn = pawn->GetFunctionByNameInChain(STR("GetActorForwardVector")))
    {
        struct
        {
            FVec3f Ret;
        } p{};
        if (safeProcessEvent(pawn, fwdFn, &p)) fwd = p.Ret;
    }
    FVec3f spawnLoc{loc.X + fwd.X * 300.0f, loc.Y + fwd.Y * 300.0f, loc.Z};
    VLOG(STR("[MoriaCppMod] [BellSpawn] pawn loc=({:.1f},{:.1f},{:.1f}) fwd=({:.2f},{:.2f},{:.2f}) → spawn=({:.1f},{:.1f},{:.1f})\n"),
         loc.X,
         loc.Y,
         loc.Z,
         fwd.X,
         fwd.Y,
         fwd.Z,
         spawnLoc.X,
         spawnLoc.Y,
         spawnLoc.Z);

    FTransformRaw xform{};
    xform.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    xform.Translation = spawnLoc;
    xform.Scale3D = {1.0f, 1.0f, 1.0f};

    // BeginDeferredActorSpawnFromClass
    int sz = m_goatBeginSpawnFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), pawn);
    writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), m_goatBPClass);
    writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"), xform);
    // 1 = AlwaysSpawn (unconditional, no collision check). Mode 4
    // is DontSpawnIfColliding (worst), mode 2 is the common
    // adjust-if-possible-but-always-spawn — I had the enum backward
    // earlier. Sticking with 1 for guaranteed spawn.
    writeGoatParm<uint8_t>(m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);

    // Pre-spawn class diagnostic — confirm the BP class is alive,
    // not abstract, not deprecated, and a child of AActor.
    //
    // [rc.56 CRASH FIX 2026-05-20] Post-dismiss/resummon AV at
    // FName::ToString on m_goatBPClass->GetName(). The cached class
    // pointer goes stale across the dismiss → resummon cycle (likely
    // because the BP asset is unloaded when the last instance is
    // destroyed). Validate liveness BEFORE calling GetName and force
    // a fresh class lookup if the cached pointer is dead. Use
    // safeClassName (SEH-wrapped) instead of bare GetName — C++
    // try/catch does NOT catch SEH access violations.
    // [rc.121] SEH probe — isObjectAlive lies on reused memory.
    // [v8.5.2] name must MATCH an expected goat class, not merely be
    // non-empty: when GC purges the class between character load and
    // the first ring, the reused slot can decode to a garbage-but-
    // non-empty name, which fooled the old check and made every
    // BeginDeferred fail until restart.
    bool clsAlive = false;
    if (m_goatBPClass && isObjectAlive(m_goatBPClass))
    {
        std::wstring probeName = safeObjectName(m_goatBPClass);
        for (auto* candidate : GOAT_CLASS_NAMES)
        {
            if (probeName == std::wstring_view(candidate))
            {
                clsAlive = true;
                break;
            }
        }
    }
    if (!clsAlive)
    {
        VLOG(STR("[MoriaCppMod] [BellSpawn] cached class STALE/null — re-resolving via GOAT_CLASS_PATHS\n"));
        m_goatBPClass = nullptr;
        // [rc.64 CRITICAL FIX 2026-05-21] Same lazy-load issue as
        // ensureGoatSpawnBindings: StaticFindObject misses pak BPs.
        // Force-resolve BP_NpcGoat (PRIMARY) via LoadClassAsset_Blocking
        // first; only fall through to StaticFindObject scan if blocking
        // load fails. Otherwise BP_Fauna_Goat (vanilla, always-loaded)
        // wins and we spawn the wrong class.
        {
            UClass* refreshed = goat_loadClassAssetBlocking(GOAT_CLASS_PATHS[0]);
            if (refreshed && isObjectAlive(refreshed))
            {
                m_goatBPClass = refreshed;
                VLOG(STR("[MoriaCppMod] [BellSpawn] re-resolved PRIMARY class via LoadClassAsset_Blocking: {}\n"), GOAT_CLASS_PATHS[0]);
            }
        }
        if (!m_goatBPClass)
        {
            for (auto* path : GOAT_CLASS_PATHS)
            {
                UClass* refreshed = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
                if (refreshed && isObjectAlive(refreshed))
                {
                    m_goatBPClass = refreshed;
                    VLOG(STR("[MoriaCppMod] [BellSpawn] re-resolved class via StaticFindObject: {}\n"), path);
                    break;
                }
            }
        }
        clsAlive = (m_goatBPClass && isObjectAlive(m_goatBPClass));
        if (!clsAlive)
        {
            VLOG(STR("[MoriaCppMod] [BellSpawn] re-resolve FAILED — class not findable; bail\n"));
            showOnScreen(L"Goat class unloaded — restart game", 3.0f, 0.9f, 0.4f, 0.4f);
            return;
        }
        // Need to also re-bind the spawn UFunctions which may have stale class refs.
        if (!ensureGoatSpawnBindings())
        {
            VLOG(STR("[MoriaCppMod] [BellSpawn] re-bind after class refresh FAILED — bail\n"));
            showOnScreen(L"Goat spawn re-bind failed", 2.5f, 0.9f, 0.4f, 0.4f);
            return;
        }
    }
    // [rc.57 COSMETIC FIX] safeClassName on a UClass returns the
    // meta-class name "BlueprintGeneratedClass", not the actual class
    // name. safeObjectName calls obj->GetName() directly (SEH-wrapped)
    // — for a UClass that returns "BP_NpcGoat_C" or whichever.
    std::wstring clsName = safeObjectName(m_goatBPClass);
    VLOG(STR("[MoriaCppMod] [BellSpawn] pre-spawn class diag: ptr={:p} alive={} name='{}'\n"), (void*)m_goatBPClass, clsAlive, clsName.c_str());

    if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data()))
    {
        showOnScreen(L"Goat spawn failed (Begin)", 2.0f, 0.9f, 0.4f, 0.4f);
        VLOG(STR("[MoriaCppMod] [BellSpawn] safeProcessEvent BeginDeferred FAILED — bail\n"));
        return;
    }
    UObject* goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
    VLOG(STR("[MoriaCppMod] [BellSpawn] BeginDeferred returned goat={:p} (mode=1=AlwaysSpawn)\n"), (void*)goat);
    if (!goat)
    {
        VLOG(STR("[MoriaCppMod] [BellSpawn] null goat — actorClass(cached)={:p} cdo={:p} pawn={:p} spawnLoc=({},{},{})\n"),
             (void*)m_goatBPClass,
             (void*)m_kismetGameplayStaticsCDO,
             (void*)pawn,
             spawnLoc.X,
             spawnLoc.Y,
             spawnLoc.Z);
        // [rc.17 FALLBACK 2026-05-25] BeginDeferred returned null.
        // Common cause: the cached class is a half-formed BP (e.g.
        // PathA's sibling-clone BP_PorterGoat_C — GetName() returns
        // 'None'/garbage, UE refuses to spawn). Walk GOAT_CLASS_PATHS
        // in order, skipping the cached pointer; accept the first one
        // that BeginDeferred actually spawns. Caches the successful
        // fallback so subsequent spawns skip the broken primary.
        for (auto* altPath : GOAT_CLASS_PATHS)
        {
            UClass* altCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, altPath);
            if (!altCls) altCls = goat_loadClassAssetBlocking(altPath);
            if (!altCls || !isObjectAlive(altCls)) continue;
            if (altCls == m_goatBPClass) continue; // already failed with this one
            VLOG(STR("[MoriaCppMod] [BellSpawn] fallback retry with {} (cls={:p})\n"), altPath, (void*)altCls);
            writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), altCls);
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) continue;
            goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (goat)
            {
                VLOG(STR("[MoriaCppMod] [BellSpawn] fallback SUCCESS with {} → goat={:p} (caching as new primary)\n"), altPath, (void*)goat);
                m_goatBPClass = altCls; // cache so subsequent spawns skip broken primary
                break;
            }
        }
        if (!goat)
        {
            showOnScreen(L"Goat spawn null (all classes failed)", 2.5f, 0.9f, 0.4f, 0.4f);
            return;
        }
        showOnScreen(L"Goat spawned via fallback class", 2.0f, 0.4f, 0.9f, 0.4f);
    }

    // [rc.82] The instance's SCS "Inventory Comp" is created DURING
    // FinishSpawning (rc.81 proved a pre-finish write hits the wrong
    // component). Instead modify the component ARCHETYPE/template (the
    // "Inventory Comp_GEN_VARIABLE" object loaded with the class) so the
    // instance inherits DefaultContainers when SCS instantiates it —
    // the true runtime equivalent of the pak edit, no retoc.
    ensureGoatInventoryArchetypePatched();

    // FinishSpawningActor
    int sz2 = m_goatFinishSpawnFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    writeGoatParm<UObject*>(m_goatFinishSpawnFn, buf2.data(), STR("Actor"), goat);
    writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
    safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

    // SpawnDefaultController for AI (porter BT etc.)
    if (auto* sdc = goat->GetFunctionByNameInChain(STR("SpawnDefaultController")))
    {
        safeProcessEvent(goat, sdc, nullptr);
    }

    // [rc.98 VERIFY 2026-07-10] Confirm we summoned TOBI'S goat
    // (BP_NpcGoat_C — the one wired with Goat.Slot.EpicPack +
    // DefaultContainers + persistor + NPCGoat DT row), not a stale
    // class or the vanilla fauna goat. safeObjectName on the instance's
    // UClass returns the concrete BP class name.
    {
        UClass* gcls = nullptr;
        try
        {
            gcls = goat->GetClassPrivate();
        }
        catch (...)
        {
        }
        std::wstring spawnedCls = gcls ? safeObjectName(gcls) : STR("(null)");
        bool isTobiGoat = (spawnedCls == STR("BP_NpcGoat_C"));
        VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.98 VERIFY] spawned actor class='{}' isTobiGoat={}\n"), spawnedCls.c_str(), isTobiGoat);
        if (!isTobiGoat) showOnScreen(L"WARNING: wrong goat class spawned!", 3.5f, 0.9f, 0.4f, 0.4f);
    }

    // [rc.100 REGISTER EXPERIMENT 2026-07-10] Dwarven NPCs get their
    // DefaultContainers instantiated by the native NPC lifecycle
    // (AMorNPCManager). Our raw-spawned goat skips the manager, and
    // NOTHING else builds the epic-pack slot (proven: containers=0
    // after every init call; Tobi's manage handler only opens the UI —
    // decoded ubergraph: GetScreen(StorageMode)+AssociatedNPC+Show).
    // EPHEMERAL GOAT (2026-07-12): registration + identity writes
    // REMOVED. The goat is never registered with the NPC manager -
    // unregistered = passive fauna (no settlement brain, no escort
    // teleport, no native restore). The only persistent state is the
    // saddlebag pack in the PLAYER inventory. See memory
    // goat-final-architecture.

    // [rc.64 BARE TOBI 2026-06-28] Per approved plan
    // deep-percolating-parnas.md — bell-spawn does ONLY tracking.
    // rc.63's spawn-time wiring (SetIsInteractive + SetRoleFuzzy +
    // equipPorterSaddlebag + initial setGoatLeashActor) broke both
    // follow AND saddlebag, so we revert to the rc.62 baseline that
    // works:
    //   - tickFollowGoats lazily resolves the controller via
    //     APawn::Controller and writes the LeashActor blackboard
    //     every 1s when !stayMode (moria_goat.inl ~14242).
    //   - Vanilla Bst_NPCGoatWorkPorter reads LeashActor → follow.
    //   - Tobi's BP CDO defaults already set Role=Porter so the
    //     root state routes correctly without our intervention.
    //
    // Stay/Follow is handled in onGoatStay/onGoatFollow (B2) by
    // toggling the AI role + LeashActor.
    // Saddlebag click is handled in openGoatSaddlebagInventory (B3)
    // by opening the PLAYER's EpicPack container — never the goat's
    // own inventory.
    {
        // [rc.66 ROLLBACK 2026-06-28] User rejected rc.65's player-
        // EpicPack approach: "I need to ADD the saddlebag to the
        // character save, not replace the backpack with the
        // saddlebag... the saddlebag will only be accessible through
        // the goat... contents save with the character not the game
        // file, that way each character will have a goat with a
        // saddlebag and that contents will go with the character
        // between worlds."
        // [NPC-REG 2026-07-17] Goat is a registered NPC again: adopt an
        // existing 'Rûdh' marker GUID or register + write identity into
        // NpcInfo (manager reload respawns it natively via the NPCGoat row).
        adoptOrRegisterGoatIdentity(goat);
        // Live-instance inventory pass: valid dwarf defs + instantiation retry.
        patchGoatInstanceInventory(goat);
        // [WorldStore] sidecar rejected — restore is native (level record).
        FollowGoatRecord rec{};
        rec.pawn = RC::Unreal::FWeakObjectPtr(goat);
        rec.controller = RC::Unreal::FWeakObjectPtr();
        rec.bellSpawned = true;
        rec.interactiveRefired = true;
        m_followGoats.push_back(rec);
        VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.66 Rollback] goat={:p} at ({:.1f},{:.1f},{:.1f}); herd size={}; NO player saddlebag grant (per user correction "
                 "2026-06-28)\n"),
             (void*)goat,
             spawnLoc.X,
             spawnLoc.Y,
             spawnLoc.Z,
             m_followGoats.size());
        showOnScreen(L"Goat summoned", 2.0f, 0.4f, 0.9f, 0.4f);
        return;
    }
}

// [rc.40b FRESH SPAWN 2026-05-12] Spawn a fresh BP_NpcGoat_C at the
// player via UGameplayStatics::BeginDeferredActorSpawnFromClass +
// FinishSpawningActor (the two-step BP-equivalent of SpawnActor).
//
// FTransform layout (UE4.27, 48 bytes, SSE-aligned):
//   off=0   FQuat Rotation (X, Y, Z, W as 4 floats) = 16 bytes
//   off=16  FVector Translation (X, Y, Z + 4-byte pad) = 16 bytes
//   off=32  FVector Scale3D (X, Y, Z + 4-byte pad) = 16 bytes
// Identity transform: rot=(0,0,0,1), trans=player loc, scale=(1,1,1).
UObject* spawnGoatAtPlayer()
{
    // 1. Resolve BP_NpcGoat_C (load if needed).
    UClass* goatCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
    if (!goatCls)
    {
        goatCls = goat_loadClassAssetBlocking(STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
    }
    if (!goatCls)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] BP_NpcGoat_C class missing — bail\n"));
        return nullptr;
    }

    UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
    UObject* pc = m_localPC && isObjectAlive(m_localPC) ? m_localPC : nullptr;
    if (!pawn || !pc)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] no PC/pawn\n"));
        return nullptr;
    }

    // 2. Player loc + rot.
    float pLoc[3] = {0, 0, 0};
    float pRot[3] = {0, 0, 0};
    if (auto* getLoc = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
    {
        int gsz = getLoc->GetParmsSize();
        std::vector<uint8_t> gbuf(gsz, 0);
        if (safeProcessEvent(pawn, getLoc, gbuf.data()))
        {
            auto* pRet = findParam(getLoc, STR("ReturnValue"));
            if (pRet)
            {
                float* fv = reinterpret_cast<float*>(gbuf.data() + pRet->GetOffset_Internal());
                pLoc[0] = fv[0];
                pLoc[1] = fv[1];
                pLoc[2] = fv[2];
            }
        }
    }
    if (auto* getRot = pawn->GetFunctionByNameInChain(STR("K2_GetActorRotation")))
    {
        int gsz = getRot->GetParmsSize();
        std::vector<uint8_t> gbuf(gsz, 0);
        if (safeProcessEvent(pawn, getRot, gbuf.data()))
        {
            auto* pRet = findParam(getRot, STR("ReturnValue"));
            if (pRet)
            {
                float* fv = reinterpret_cast<float*>(gbuf.data() + pRet->GetOffset_Internal());
                pRot[0] = fv[0];
                pRot[1] = fv[1];
                pRot[2] = fv[2];
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Spawn] player loc=({:.1f},{:.1f},{:.1f})\n"), pLoc[0], pLoc[1], pLoc[2]);

    // 3. Build the FTransform (48-byte SSE-aligned).
    //    [rc.44 2026-05-12] Spawn in front of player at ground level
    //    (was spawning at player+50Z = head height = goat-on-head).
    //    Player capsule half-height ~88. Forward vector from yaw.
    //    Place goat 200 units in front, at player.Z - 70 (just above
    //    the floor relative to player's center). Once AI controller
    //    settles + gravity applies, the goat will plant on the
    //    ground via CharacterMovementComponent.
    const float DEG2RAD = 3.14159265f / 180.0f;
    float yawRad = pRot[1] * DEG2RAD;
    float fwdX = cosf(yawRad);
    float fwdY = sinf(yawRad);
    float spawnLoc[3] = {pLoc[0] + fwdX * 200.0f, pLoc[1] + fwdY * 200.0f, pLoc[2] - 70.0f};
    VLOG(STR("[MoriaCppMod] [Spawn] requested loc=({:.1f},{:.1f},{:.1f}) (player + fwd*200, Z-70)\n"), spawnLoc[0], spawnLoc[1], spawnLoc[2]);

    uint8_t xform[48];
    std::memset(xform, 0, sizeof(xform));
    // Rotation quat (identity)
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(xform + 0, quat, sizeof(quat));
    // Translation (X, Y, Z, _pad)
    float trans[4] = {spawnLoc[0], spawnLoc[1], spawnLoc[2], 0.0f};
    std::memcpy(xform + 16, trans, sizeof(trans));
    // Scale (X, Y, Z, _pad)
    float scale[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    std::memcpy(xform + 32, scale, sizeof(scale));

    // 4. BeginDeferredActorSpawnFromClass.
    auto* beginFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
    auto* gsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
    if (!beginFn || !gsCDO)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferredActorSpawnFromClass unresolved (fn={:p} cdo={:p})\n"), (void*)beginFn, (void*)gsCDO);
        return nullptr;
    }

    int sz = beginFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    auto* pWC = findParam(beginFn, STR("WorldContextObject"));
    auto* pCls = findParam(beginFn, STR("ActorClass"));
    auto* pXform = findParam(beginFn, STR("SpawnTransform"));
    auto* pColl = findParam(beginFn, STR("CollisionHandlingOverride"));
    auto* pOwner = findParam(beginFn, STR("Owner"));
    auto* pRet = findParam(beginFn, STR("ReturnValue"));
    if (!pWC || !pCls || !pXform || !pColl || !pRet)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferred parm missing (WC={:p} Cls={:p} Xform={:p} Coll={:p} Ret={:p})\n"),
             (void*)pWC,
             (void*)pCls,
             (void*)pXform,
             (void*)pColl,
             (void*)pRet);
        return nullptr;
    }
    *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = pc;
    *reinterpret_cast<UClass**>(buf.data() + pCls->GetOffset_Internal()) = goatCls;
    std::memcpy(buf.data() + pXform->GetOffset_Internal(), xform, sizeof(xform));
    // CollisionHandling = 1 (AlwaysSpawn) — bypass overlap rejection
    *reinterpret_cast<uint8_t*>(buf.data() + pColl->GetOffset_Internal()) = 1;
    if (pOwner)
    {
        *reinterpret_cast<UObject**>(buf.data() + pOwner->GetOffset_Internal()) = pc;
    }

    if (!safeProcessEvent(gsCDO, beginFn, buf.data()))
    {
        VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferred PE failed\n"));
        return nullptr;
    }
    UObject* deferred = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
    if (!deferred)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferred returned null actor\n"));
        return nullptr;
    }
    VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferred returned actor ptr={:p}\n"), (void*)deferred);

    // 5. FinishSpawningActor.
    auto* finishFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));
    if (!finishFn)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawningActor missing — returning deferred actor (may be partial spawn)\n"));
        return deferred;
    }

    int sz2 = finishFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    auto* pActor = findParam(finishFn, STR("Actor"));
    auto* pXform2 = findParam(finishFn, STR("SpawnTransform"));
    auto* pRet2 = findParam(finishFn, STR("ReturnValue"));
    if (!pActor || !pXform2 || !pRet2)
    {
        VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawning parm missing — returning deferred\n"));
        return deferred;
    }
    *reinterpret_cast<UObject**>(buf2.data() + pActor->GetOffset_Internal()) = deferred;
    std::memcpy(buf2.data() + pXform2->GetOffset_Internal(), xform, sizeof(xform));
    if (!safeProcessEvent(gsCDO, finishFn, buf2.data()))
    {
        VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawning PE failed — returning deferred\n"));
        return deferred;
    }
    UObject* finalActor = *reinterpret_cast<UObject**>(buf2.data() + pRet2->GetOffset_Internal());
    VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawning returned actor ptr={:p}\n"), (void*)finalActor);

    // [rc.43 POST-SPAWN ACTIVATION 2026-05-12]
    // rc.42 diagnostic revealed two problems:
    //   1. bHidden=true   — actor is hidden, won't render
    //   2. Controller=0x0 — no AI controller attached, no brain
    // Both are standard UE4 UFunctions: unhide via
    // K2_SetActorHiddenInGame(false) and attach AI via
    // SpawnDefaultController() (APawn method that uses the BP's
    // AIControllerClass — for BP_NpcGoat_C that resolves to
    // BP_NpcGoat_AIController_C with its existing porter BT).
    {
        UObject* target = finalActor ? finalActor : deferred;
        if (target && isObjectAlive(target))
        {
            // [rc.44] Unhide via SetActorHiddenInGame (no K2 prefix —
            // rc.43 had wrong UFunction name and the call silently
            // dropped). The bp-callable AActor method is named
            // SetActorHiddenInGame.
            if (auto* unhideFn = target->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
            {
                int hsz = unhideFn->GetParmsSize();
                std::vector<uint8_t> hbuf(hsz, 0);
                auto* pHidden = findParam(unhideFn, STR("bNewHidden"));
                if (!pHidden) pHidden = findParam(unhideFn, STR("NewHidden"));
                if (pHidden)
                {
                    *reinterpret_cast<bool*>(hbuf.data() + pHidden->GetOffset_Internal()) = false;
                    safeProcessEvent(target, unhideFn, hbuf.data());
                    VLOG(STR("[MoriaCppMod] [Spawn] SetActorHiddenInGame(false) fired\n"));
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [Spawn] SetActorHiddenInGame parm 'bNewHidden' not found\n"));
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [Spawn] SetActorHiddenInGame not found on goat\n"));
            }
            // Spawn AI controller
            if (auto* sdcFn = target->GetFunctionByNameInChain(STR("SpawnDefaultController")))
            {
                int csz = sdcFn->GetParmsSize();
                std::vector<uint8_t> cbuf(csz, 0);
                safeProcessEvent(target, sdcFn, cbuf.data());
                VLOG(STR("[MoriaCppMod] [Spawn] SpawnDefaultController fired\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [Spawn] SpawnDefaultController not found on goat\n"));
            }

            // [rc.44] Disable vanilla rescue + recruit interaction
            // prompts on the MorNPCComponent. The bell-summoned goat
            // is OUR companion — no rescue chain, no recruit chain.
            // The E-menu replacement (Stay/Follow/Dismiss/Access
            // Saddlebags) lands in rc.45. For now, killing the
            // rescue prompt at minimum stops the wrong UI from
            // appearing when player approaches the goat.
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (auto* getCompFn = target->GetFunctionByNameInChain(STR("GetComponentByClass")))
            {
                int gsz = getCompFn->GetParmsSize();
                std::vector<uint8_t> gbuf(gsz, 0);
                writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), npcCompCls);
                if (safeProcessEvent(target, getCompFn, gbuf.data()))
                {
                    UObject* npcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                    if (npcComp && isObjectAlive(npcComp))
                    {
                        auto setFlag = [npcComp](const wchar_t* name, bool val) {
                            if (setBoolProp(npcComp, name, val)) VLOG(STR("[MoriaCppMod] [Spawn] set {}={}\n"), name, val ? STR("true") : STR("false"));
                        };
                        setFlag(STR("bRescueInteractionEnabled"), false);
                        setFlag(STR("bRecruitInteractionEnabled"), false);
                        setFlag(STR("bManageInteractionEnabled"), true);
                        setFlag(STR("bDetailsInteractionEnabled"), false);
                        setFlag(STR("bTalkInteractionEnabled"), false);
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [Spawn] MorNPCComponent not resolved on goat\n"));
                    }
                }
            }
        }
    }

    // [rc.42 POST-SPAWN DIAGNOSTIC 2026-05-12]
    // Confirmed: the actor is created and tracked, but invisible.
    // Vanilla deeps goats come through Moria's NPC spawn pipeline
    // (encounter system → BP_RequestSpawn) which initializes mesh
    // visibility, AI controller, replication, etc. Raw SpawnActor
    // skips all that. Diagnostic: log post-spawn state so we can
    // see what's missing.
    UObject* check = finalActor ? finalActor : deferred;
    if (check && isObjectAlive(check))
    {
        // Actor location — confirms it spawned where we asked.
        if (auto* getLoc = check->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
        {
            int gsz = getLoc->GetParmsSize();
            std::vector<uint8_t> gbuf(gsz, 0);
            if (safeProcessEvent(check, getLoc, gbuf.data()))
            {
                auto* pRet3 = findParam(getLoc, STR("ReturnValue"));
                if (pRet3)
                {
                    float* fv = reinterpret_cast<float*>(gbuf.data() + pRet3->GetOffset_Internal());
                    VLOG(STR("[MoriaCppMod] [Spawn-Diag] actual goat loc=({:.1f},{:.1f},{:.1f})\n"), fv[0], fv[1], fv[2]);
                }
            }
        }
        // AI Controller — does the goat have one? Without it, no behavior tree.
        UObject** ctrlPtr = check->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
        if (ctrlPtr)
        {
            UObject* ctrl = *ctrlPtr;
            std::wstring ctrlCls = ctrl && isObjectAlive(ctrl) ? safeClassName(ctrl) : L"<null>";
            VLOG(STR("[MoriaCppMod] [Spawn-Diag] Controller={:p} class={}\n"), (void*)ctrl, ctrlCls.c_str());
        }
        else
        {
            VLOG(STR("[MoriaCppMod] [Spawn-Diag] Controller property not found on goat\n"));
        }
        // Components walk — see what's attached.
        UClass* aCls = nullptr;
        try
        {
            aCls = check->GetClassPrivate();
        }
        catch (...)
        {
        }
        if (aCls)
        {
            FProperty* bpccProp = nullptr;
            for (auto* strct = static_cast<UStruct*>(aCls); strct && !bpccProp; strct = strct->GetSuperStruct())
            {
                for (auto* p : strct->ForEachProperty())
                {
                    std::wstring pn;
                    try
                    {
                        pn = p->GetName();
                    }
                    catch (...)
                    {
                    }
                    if (pn == STR("BlueprintCreatedComponents"))
                    {
                        bpccProp = p;
                        break;
                    }
                }
            }
            if (bpccProp)
            {
                uint8_t* slot = reinterpret_cast<uint8_t*>(check) + bpccProp->GetOffset_Internal();
                UObject** data = *reinterpret_cast<UObject***>(slot + 0);
                int32_t num = *reinterpret_cast<int32_t*>(slot + 8);
                VLOG(STR("[MoriaCppMod] [Spawn-Diag] BlueprintCreatedComponents count={}\n"), num);
                for (int32_t i = 0; i < num && i < 40; ++i)
                {
                    UObject* c = data[i];
                    if (!c || !isObjectAlive(c)) continue;
                    std::wstring cName, cCls;
                    try
                    {
                        cName = c->GetName();
                    }
                    catch (...)
                    {
                    }
                    try
                    {
                        cCls = c->GetClassPrivate()->GetName();
                    }
                    catch (...)
                    {
                    }
                    VLOG(STR("[MoriaCppMod] [Spawn-Diag]   [{}] {} : {}\n"), i, cName.c_str(), cCls.c_str());
                }
            }
        }
        // Actor bHidden flag.
        bool* hiddenPtr = check->GetValuePtrByPropertyNameInChain<bool>(STR("bHidden"));
        if (hiddenPtr)
        {
            VLOG(STR("[MoriaCppMod] [Spawn-Diag] bHidden={}\n"), *hiddenPtr ? STR("true") : STR("false"));
        }
    }

    return finalActor ? finalActor : deferred;
}

void dumpSettlementManagerRescueState(UObject* worldCtx)
{
    VLOG(STR("[MoriaCppMod] [Probe] === BP_MorSettlementManager rescue-state dump ===\n"));
    if (!worldCtx)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no world ctx\n"));
        return;
    }
    auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
    if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
    if (!mgrCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr class missing\n"));
        return;
    }
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkCDO) return;
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), mgrCls);
    if (!safeProcessEvent(fgkCDO, getMgrFn, buf.data())) return;
    UObject* mgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!mgr || !isObjectAlive(mgr))
    {
        VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton missing\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton={:p} cls={}\n"), (void*)mgr, safeClassName(mgr).c_str());

    // Find the goat actor up front so we can flag matches by ptr identity.
    UObject* deepsGoat = nullptr;
    {
        std::vector<UObject*> hit;
        if (seh_findAnyGoatActor(&hit))
            for (UObject* g : hit)
                if (g && isObjectAlive(g))
                {
                    deepsGoat = g;
                    break;
                }
    }
    VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match detection): {:p}\n"), (void*)deepsGoat);

    // Walk full super chain.
    int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0;
    for (auto* strct = static_cast<UStruct*>(mgr->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
    {
        std::wstring scopeCls;
        try
        {
            scopeCls = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            std::wstring pn, tn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                tn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            unsigned off = (unsigned)prop->GetOffset_Internal();
            uint8_t* slot = reinterpret_cast<uint8_t*>(mgr) + off;

            if (tn == STR("ArrayProperty"))
            {
                auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                FProperty* inner = nullptr;
                try
                {
                    inner = arrProp->GetInner();
                }
                catch (...)
                {
                }
                std::wstring innerTn;
                if (inner)
                {
                    try
                    {
                        innerTn = inner->GetClass().GetName();
                    }
                    catch (...)
                    {
                    }
                }
                void** data = reinterpret_cast<void**>(slot + 0);
                int32_t* num = reinterpret_cast<int32_t*>(slot + 8);
                VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                     scopeCls.c_str(),
                     pn.c_str(),
                     off,
                     innerTn.empty() ? STR("<unknown>") : innerTn.c_str(),
                     *num);
                if (innerTn == STR("ObjectProperty") && *num > 0 && *data)
                {
                    UObject** entries = reinterpret_cast<UObject**>(*data);
                    int limit = (*num < 32) ? *num : 32;
                    for (int j = 0; j < limit; ++j)
                    {
                        UObject* e = entries[j];
                        if (!e) continue;
                        if ((uintptr_t)e < 0x10000) continue;
                        if (!isObjectAlive(e)) continue;
                        std::wstring eName, eCls;
                        try
                        {
                            eName = e->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            eCls = e->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                        bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                        VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"), j, eName.c_str(), eCls.c_str(), isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                    }
                }
                ++arrCount;
            }
            else if (tn == STR("BoolProperty"))
            {
                // BoolProperty has a bitmask; cheap read just looks at byte 0.
                uint8_t b = *slot;
                VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"), scopeCls.c_str(), pn.c_str(), off, b);
                ++boolCount;
            }
            else if (tn == STR("IntProperty"))
            {
                int32_t v = *reinterpret_cast<int32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"), scopeCls.c_str(), pn.c_str(), off, v);
                ++intCount;
            }
            else if (tn == STR("NameProperty"))
            {
                // Safe: reading raw FName bytes without ToString.
                // Log the comparison index only.
                uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"), scopeCls.c_str(), pn.c_str(), off, fnameRaw[0], fnameRaw[1]);
                ++nameCount;
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end settlement-mgr dump ({} arrays, {} bools, {} ints, {} names) ===\n"), arrCount, boolCount, intCount, nameCount);
}

// [v7.1.0-rc.37 PLAYER-CONTROLLER STATE 2026-05-11] Walk the live
// BP_FGKMoriaPlayerController_C class chain for every Array/Bool/Int/
// Name/Object property. Goal: spot any cached rescuee field
// (LastRescuedNpc / PendingResident / CurrentRecruitTarget) that
// populates post-rescue. If an ObjectProperty holds the goat ptr,
// that's a hook surface — we can read it from PE-post on
// ServerRescueNpc and use the cached goat for downstream actions.
void dumpPlayerControllerRescueState()
{
    VLOG(STR("[MoriaCppMod] [Probe] === BP_FGKMoriaPlayerController rescue-state dump ===\n"));
    UObject* pc = m_localPC && isObjectAlive(m_localPC) ? m_localPC : nullptr;
    if (!pc)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no local PC — bail\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] PC ptr={:p} class={}\n"), (void*)pc, safeClassName(pc).c_str());

    // Find the goat ptr up-front so we can flag pointer matches.
    UObject* deepsGoat = nullptr;
    {
        std::vector<UObject*> hit;
        if (seh_findAnyGoatActor(&hit))
            for (UObject* g : hit)
                if (g && isObjectAlive(g))
                {
                    deepsGoat = g;
                    break;
                }
    }
    VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match): {:p}\n"), (void*)deepsGoat);

    int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0, objCount = 0;
    for (auto* strct = static_cast<UStruct*>(pc->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
    {
        std::wstring scopeCls;
        try
        {
            scopeCls = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            std::wstring pn, tn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                tn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            unsigned off = (unsigned)prop->GetOffset_Internal();
            uint8_t* slot = reinterpret_cast<uint8_t*>(pc) + off;

            if (tn == STR("ObjectProperty"))
            {
                UObject* o = *reinterpret_cast<UObject**>(slot);
                if (!o || (uintptr_t)o < 0x10000) continue;
                if (!isObjectAlive(o)) continue;
                std::wstring oName, oCls;
                try
                {
                    oName = o->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    oCls = o->GetClassPrivate()->GetName();
                }
                catch (...)
                {
                }
                bool isGoat = (o == deepsGoat) || (oCls == STR("BP_NpcGoat_C"));
                VLOG(STR("[MoriaCppMod] [Probe]   OBJ  [{}].{} off=0x{:X} ptr={:p} name={} cls={}{}\n"),
                     scopeCls.c_str(),
                     pn.c_str(),
                     off,
                     (void*)o,
                     oName.c_str(),
                     oCls.c_str(),
                     isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                ++objCount;
            }
            else if (tn == STR("ArrayProperty"))
            {
                auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                FProperty* inner = nullptr;
                try
                {
                    inner = arrProp->GetInner();
                }
                catch (...)
                {
                }
                std::wstring innerTn;
                if (inner)
                {
                    try
                    {
                        innerTn = inner->GetClass().GetName();
                    }
                    catch (...)
                    {
                    }
                }
                void** data = reinterpret_cast<void**>(slot + 0);
                int32_t* num = reinterpret_cast<int32_t*>(slot + 8);
                VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                     scopeCls.c_str(),
                     pn.c_str(),
                     off,
                     innerTn.empty() ? STR("<unknown>") : innerTn.c_str(),
                     *num);
                if (innerTn == STR("ObjectProperty") && *num > 0 && *data)
                {
                    UObject** entries = reinterpret_cast<UObject**>(*data);
                    int limit = (*num < 16) ? *num : 16;
                    for (int j = 0; j < limit; ++j)
                    {
                        UObject* e = entries[j];
                        if (!e || (uintptr_t)e < 0x10000 || !isObjectAlive(e)) continue;
                        std::wstring eName, eCls;
                        try
                        {
                            eName = e->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            eCls = e->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                        bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                        VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"), j, eName.c_str(), eCls.c_str(), isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                    }
                }
                ++arrCount;
            }
            else if (tn == STR("BoolProperty"))
            {
                uint8_t b = *slot;
                VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"), scopeCls.c_str(), pn.c_str(), off, b);
                ++boolCount;
            }
            else if (tn == STR("IntProperty"))
            {
                int32_t v = *reinterpret_cast<int32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"), scopeCls.c_str(), pn.c_str(), off, v);
                ++intCount;
            }
            else if (tn == STR("NameProperty"))
            {
                uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"), scopeCls.c_str(), pn.c_str(), off, fnameRaw[0], fnameRaw[1]);
                ++nameCount;
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end PC dump ({} arrays, {} bools, {} ints, {} names, {} objects) ===\n"), arrCount, boolCount, intCount, nameCount, objCount);
}

// [v7.1.0-rc.37 SETTLEMENT MGR UFUNC ENUM 2026-05-11] Enumerate every
// UFunction on AMorSettlementManager class chain (resolved via
// FGKUtils::GetManager → live singleton's class). Logs name +
// parameter signature so we can grep for AddResident /
// RegisterRescuedNpc / WriteRecord / AddCamper / AddPendingResident
// candidates that we could call from runtime to bypass the C++
// second-filter that's blocking goat record creation per desktop's
// Model α analysis.
void dumpSettlementManagerUFunctions(UObject* worldCtx)
{
    VLOG(STR("[MoriaCppMod] [Probe] === AMorSettlementManager UFunction enum ===\n"));
    if (!worldCtx)
    {
        VLOG(STR("[MoriaCppMod] [Probe] no world ctx\n"));
        return;
    }
    auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
    if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
    if (!mgrCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr class missing\n"));
        return;
    }
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkCDO) return;
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), mgrCls);
    if (!safeProcessEvent(fgkCDO, getMgrFn, buf.data())) return;
    UObject* mgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!mgr || !isObjectAlive(mgr))
    {
        VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton missing\n"));
        return;
    }
    UClass* liveCls = mgr->GetClassPrivate();
    if (!liveCls) return;
    VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr live class={}\n"), safeClassName(mgr).c_str());

    int count = 0;
    for (auto* fn : liveCls->ForEachFunctionInChain())
    {
        if (count >= 600) break;
        std::wstring fnName, ownerName;
        try
        {
            fnName = fn->GetName();
        }
        catch (...)
        {
        }
        try
        {
            if (auto* outer = fn->GetOuterPrivate()) ownerName = outer->GetName();
        }
        catch (...)
        {
        }
        std::wstring paramSig;
        int parmCount = 0;
        for (auto* prop : fn->ForEachProperty())
        {
            if (parmCount >= 6) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            if (!paramSig.empty()) paramSig += STR(", ");
            paramSig += pcn + STR(" ") + pn;
            ++parmCount;
        }
        VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"), ownerName.c_str(), fnName.c_str(), paramSig.c_str());
        ++count;
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end settlement-mgr UFunction enum ({} fns) ===\n"), count);
}

// [v7.1.0-rc.34 UFUNC ENUM 2026-05-10] Per porter-goat-feature-goals.md
// and desktop's reply on AI controller travel: enumerate every
// UFunction on BP_NpcGoat_C class chain AND on
// BP_NpcGoat_AIController_C class chain (or fallback parents).
// Looking for follow-trigger / become-porter / set-master / set-target
// calls that we can invoke post-rescue to switch the goat into
// porter follow mode. Logs name + parameter signature for each
// UFunction so desktop and we can grep offline.
void dumpGoatAIControllerUFuncs()
{
    VLOG(STR("[MoriaCppMod] [Probe] === goat actor + AI controller UFunction enum ===\n"));

    auto dumpClass = [](const wchar_t* classPath, const wchar_t* label, int maxFns) {
        UClass* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classPath);
        if (!cls)
        {
            VLOG(STR("[MoriaCppMod] [Probe] {} class not loaded (path={})\n"), label, classPath);
            return;
        }
        VLOG(STR("[MoriaCppMod] [Probe] --- {} ({}) UFunctions ---\n"), label, classPath);
        int count = 0;
        for (auto* fn : cls->ForEachFunctionInChain())
        {
            if (count >= maxFns) break;
            std::wstring fnName, ownerName;
            try
            {
                fnName = fn->GetName();
            }
            catch (...)
            {
            }
            try
            {
                if (auto* outer = fn->GetOuterPrivate()) ownerName = outer->GetName();
            }
            catch (...)
            {
            }
            std::wstring paramSig;
            int parmCount = 0;
            for (auto* prop : fn->ForEachProperty())
            {
                if (parmCount >= 6) break;
                std::wstring pn, pcn;
                try
                {
                    pn = prop->GetName();
                }
                catch (...)
                {
                }
                try
                {
                    pcn = prop->GetClass().GetName();
                }
                catch (...)
                {
                }
                if (!paramSig.empty()) paramSig += STR(", ");
                paramSig += pcn + STR(" ") + pn;
                ++parmCount;
            }
            VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"), ownerName.c_str(), fnName.c_str(), paramSig.c_str());
            ++count;
        }
        VLOG(STR("[MoriaCppMod] [Probe] --- end {} ({} UFunctions) ---\n"), label, count);
    };

    // Goat actor's class chain
    dumpClass(STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"), STR("BP_NpcGoat_C"), 400);

    // Goat's AI controller — desktop confirmed the class path
    dumpClass(STR("/Game/Character/NpcGoat/BP_NpcGoat_AIController.BP_NpcGoat_AIController_C"), STR("BP_NpcGoat_AIController_C"), 400);

    // FaunaBase parent — common base for animal AI controllers
    dumpClass(STR("/Game/Character/Fauna/BP_AIController_FaunaBase.BP_AIController_FaunaBase_C"), STR("BP_AIController_FaunaBase_C"), 200);

    // Look up the LIVE goat actor's actual AI controller — gives us
    // class name even if the path guess above is wrong.
    std::vector<UObject*> hit;
    if (seh_findAnyGoatActor(&hit) && !hit.empty())
    {
        UObject* goat = nullptr;
        for (UObject* g : hit)
            if (g && isObjectAlive(g))
            {
                goat = g;
                break;
            }
        if (goat)
        {
            // APawn::Controller UPROPERTY (ObjectProperty).
            UObject** controllerPtr = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            if (controllerPtr && *controllerPtr && isObjectAlive(*controllerPtr))
            {
                UObject* ctrl = *controllerPtr;
                std::wstring ctrlClsName = safeClassName(ctrl);
                VLOG(STR("[MoriaCppMod] [Probe] LIVE goat AI controller: ptr={:p} class={}\n"), (void*)ctrl, ctrlClsName.c_str());
                // Enumerate UFunctions on that live class
                // regardless of class-path guess.
                UClass* liveCls = ctrl->GetClassPrivate();
                if (liveCls)
                {
                    VLOG(STR("[MoriaCppMod] [Probe] --- LIVE controller ({}) UFunctions ---\n"), ctrlClsName.c_str());
                    int count = 0;
                    for (auto* fn : liveCls->ForEachFunctionInChain())
                    {
                        if (count >= 600) break;
                        std::wstring fnName, ownerName;
                        try
                        {
                            fnName = fn->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            if (auto* outer = fn->GetOuterPrivate()) ownerName = outer->GetName();
                        }
                        catch (...)
                        {
                        }
                        std::wstring paramSig;
                        int parmCount = 0;
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (parmCount >= 6) break;
                            std::wstring pn, pcn;
                            try
                            {
                                pn = prop->GetName();
                            }
                            catch (...)
                            {
                            }
                            try
                            {
                                pcn = prop->GetClass().GetName();
                            }
                            catch (...)
                            {
                            }
                            if (!paramSig.empty()) paramSig += STR(", ");
                            paramSig += pcn + STR(" ") + pn;
                            ++parmCount;
                        }
                        VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"), ownerName.c_str(), fnName.c_str(), paramSig.c_str());
                        ++count;
                    }
                    VLOG(STR("[MoriaCppMod] [Probe] --- end LIVE controller ({} UFunctions) ---\n"), count);
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [Probe] live goat has no Controller pointer\n"));
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end UFunction enum ===\n"));
}

// [v1.2.8 RECRUIT WIRE-UP 2026-05-10] Full goat-rescue runtime
// chain. Replaces old SubGoat with a complete setup:
//   1. Find goat + MorWandererComponent
//   2. Enable bRecruitInteractionEnabled + call SetRecruitInteractionEnabled(true)
//   3. Append FScriptDelegate to component's OnWandererRecruited (off=0xB0)
//   4. Append FScriptDelegate to settlement-mgr's OnNpcRescued (existing path)
//   5. Track the goat ptr so the PE-pre detector can fire onGoatRecruited
//      when the bound UFunction name dispatches.
// Sentinel UFunction name we bind — must NOT exist on the goat
// class (so engine FindFunction returns null and skips the call,
// never crashing). The PE-pre filter watches for this name to
// detect when the multicast fires for any subscriber, then
// checks if the bound object is one of our tracked goats.
static constexpr const wchar_t* kGoatRecruitMarkerFn = STR("MoriaModGoatRecruitMarker");

// Helper: shallow-copy a single named UPROPERTY from src to dst when
// both objects share a class. Used for cloning proximity-menu row
// fields (Interactable, Interactor, InteractComponent) from a
// template without hardcoding their byte offsets — survives any
// FGK widget layout shift.
bool copyRowPropertyByName(UObject* dst, UObject* src, const wchar_t* name)
{
    if (!dst || !src) return false;
    UClass* cls = nullptr;
    try
    {
        cls = dst->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (!cls) return false;
    RC::Unreal::FProperty* found = nullptr;
    try
    {
        for (auto* p : cls->ForEachPropertyInChain())
        {
            if (!p) continue;
            std::wstring pn;
            try
            {
                pn = p->GetName();
            }
            catch (...)
            {
                continue;
            }
            if (pn == name)
            {
                found = p;
                break;
            }
        }
    }
    catch (...)
    {
    }
    if (!found) return false;
    int32 off = -1;
    int32 size = -1;
    try
    {
        off = found->GetOffset_Internal();
        size = found->GetElementSize();
    }
    catch (...)
    {
    }
    if (off < 0 || size <= 0) return false;
    uint8_t* srcPtr = reinterpret_cast<uint8_t*>(src) + off;
    uint8_t* dstPtr = reinterpret_cast<uint8_t*>(dst) + off;
    if (!isReadableMemory(srcPtr, (size_t)size)) return false;
    if (!isReadableMemory(dstPtr, (size_t)size)) return false;
    std::memcpy(dstPtr, srcPtr, (size_t)size);
    return true;
}

// Helper: append an entry to a multicast's InvocationList. Returns
// true if appended (or already present), false on any error.
// Layout: TArray header (16B) at field offset, FScriptDelegate
// entries (16B each: FWeakObjectPtr + FName).
bool appendMulticastEntry(uint8_t* listSlot, UObject* obj, const wchar_t* fnName, const wchar_t* tag)
{
    void** listData = reinterpret_cast<void**>(listSlot + 0);
    int32_t* listNum = reinterpret_cast<int32_t*>(listSlot + 8);
    int32_t* listMax = reinterpret_cast<int32_t*>(listSlot + 12);

    // Idempotency: if already subscribed, don't double-append.
    uint8_t* entries = reinterpret_cast<uint8_t*>(*listData);
    if (entries && *listNum > 0)
    {
        for (int32_t i = 0; i < *listNum; ++i)
        {
            RC::Unreal::FWeakObjectPtr wp;
            std::memcpy(&wp, entries + i * 16, sizeof(RC::Unreal::FWeakObjectPtr));
            if (wp.Get() == obj)
            {
                VLOG(STR("[MoriaCppMod] [Recruit/{}] obj already subscribed at slot {}\n"), tag, i);
                return true;
            }
        }
    }
    if (*listNum >= *listMax)
    {
        VLOG(STR("[MoriaCppMod] [Recruit/{}] no headroom (num={} max={}) — would need realloc\n"), tag, *listNum, *listMax);
        return false;
    }
    if (!*listData)
    {
        VLOG(STR("[MoriaCppMod] [Recruit/{}] InvocationList Data ptr null\n"), tag);
        return false;
    }
    uint8_t* nextSlot = entries + (*listNum) * 16;
    new (nextSlot + 0) RC::Unreal::FWeakObjectPtr(obj);
    RC::Unreal::FName fn(fnName, RC::Unreal::FNAME_Add);
    std::memcpy(nextSlot + 8, &fn, sizeof(RC::Unreal::FName));
    ++(*listNum);
    VLOG(STR("[MoriaCppMod] [Recruit/{}] appended obj={:p} fn={} ; new num={}\n"), tag, (void*)obj, fnName, *listNum);
    return true;
}

// [v1.2.3 OLD — kept for legacy NUM- compatibility]
//
// Layout: TArray<FScriptDelegate> at off=0x2F8 of MorSettlementManager.
//         FScriptDelegate = { FWeakObjectPtr Object (8) + FName FunctionName (8) } = 16B
//         TArray header = { ptr Data (8) + int32 Num (4) + int32 Max (4) } = 16B at field offset
//
// Probe earlier showed Num=2 Max=4 → there's headroom for at
// least 2 more entries without reallocation. We only append 1.
void subscribeDeepsGoatToOnNpcRescued()
{
    UObject* worldCtx = m_localPC && isObjectAlive(m_localPC) ? m_localPC : (m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr);
    if (!worldCtx)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] no world context — bail\n"));
        return;
    }

    // 1. Find BP_MorSettlementManager singleton.
    auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
    if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
    if (!mgrCls)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] settlement manager class missing\n"));
        return;
    }
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkUtilsCDO)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] FGKUtils::GetManager unresolvable\n"));
        return;
    }
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), mgrCls);
    if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data())) return;
    UObject* settleMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!settleMgr || !isObjectAlive(settleMgr))
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] settlement manager singleton missing\n"));
        return;
    }

    // 2. Find OnNpcRescued multicast property (walks super chain).
    FProperty* delegateProp = nullptr;
    for (auto* strct = static_cast<UStruct*>(settleMgr->GetClassPrivate()); strct && !delegateProp; strct = strct->GetSuperStruct())
    {
        for (auto* prop : strct->ForEachProperty())
        {
            std::wstring pn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            if (pn == STR("OnNpcRescued"))
            {
                delegateProp = prop;
                break;
            }
        }
    }
    if (!delegateProp)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] OnNpcRescued property not found\n"));
        return;
    }
    uint8_t* delegateSlot = reinterpret_cast<uint8_t*>(settleMgr) + delegateProp->GetOffset_Internal();
    void** listData = reinterpret_cast<void**>(delegateSlot + 0);
    int32_t* listNum = reinterpret_cast<int32_t*>(delegateSlot + 8);
    int32_t* listMax = reinterpret_cast<int32_t*>(delegateSlot + 12);
    VLOG(STR("[MoriaCppMod] [SubGoat] InvocationList state pre-append: num={} max={} data={:p}\n"), *listNum, *listMax, *listData);

    // 3. Find the deeps goat. FindAllOf is exact-match, and
    // desktop's v1.2.x may have introduced a `BP_NpcGoat_Survivor_*`
    // subclass mirroring the dwarf survivor pattern. Probe all
    // plausible class names.
    const wchar_t* goatClassCandidates[] = {
            STR("BP_NpcGoat_C"),
            STR("BP_NpcGoat_Survivor_C"),
            STR("BP_NpcGoat_Survivor_1_C"),
            STR("BP_NpcGoat_Survivor_2_C"),
            STR("BP_NpcGoat_Survivor_3_C"),
            STR("BP_NpcGoat_Wanderer_C"),
            STR("BP_NpcGoat_Wanderer_1_C"),
            STR("BP_PorterGoat_C"),
            STR("BP_PorterGoat_Survivor_1_C"),
    };
    UObject* goat = nullptr;
    std::wstring goatClsHit;
    for (auto* cn : goatClassCandidates)
    {
        std::vector<UObject*> goats;
        if (!seh_findAllOf(cn, &goats) || goats.empty()) continue;
        for (UObject* g : goats)
        {
            if (g && isObjectAlive(g))
            {
                goat = g;
                goatClsHit = cn;
                break;
            }
        }
        if (goat) break;
    }
    if (!goat)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] no goat class found in world (tried 9 candidates) — must be near deeps goat AND class must match\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [SubGoat] target goat={:p} matched on class={} (actual cls={})\n"), (void*)goat, goatClsHit.c_str(), safeClassName(goat).c_str());

    // 4. Check if already subscribed (idempotency).
    uint8_t* entries = reinterpret_cast<uint8_t*>(*listData);
    if (entries)
    {
        for (int32_t i = 0; i < *listNum; ++i)
        {
            RC::Unreal::FWeakObjectPtr wp;
            std::memcpy(&wp, entries + i * 16, sizeof(RC::Unreal::FWeakObjectPtr));
            if (wp.Get() == goat)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] goat already subscribed at slot {} — skipping\n"), i);
                return;
            }
        }
    }

    // 5. Headroom check. Refuse if Num == Max (no realloc this iteration).
    if (*listNum >= *listMax)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] no headroom (num={} max={}) — would need realloc; skipping\n"), *listNum, *listMax);
        return;
    }
    if (!*listData)
    {
        VLOG(STR("[MoriaCppMod] [SubGoat] InvocationList Data ptr is null — skipping\n"));
        return;
    }

    // 6. Append: { FWeakObjectPtr(goat), FName("OnNpcRescued_Event_2") }
    // FunctionName matches the dwarf's binding pattern. Goat doesn't
    // have this UFunction so engine FindFunction would no-op if the
    // multicast ever fires on this entry. Per hypothesis the
    // multicast doesn't actually fire on goats anyway — what
    // matters is membership in the list.
    uint8_t* nextSlot = entries + (*listNum) * 16;
    // FWeakObjectPtr at offset 0
    new (nextSlot + 0) RC::Unreal::FWeakObjectPtr(goat);
    // FName at offset 8
    RC::Unreal::FName fn(STR("OnNpcRescued_Event_2"), RC::Unreal::FNAME_Add);
    std::memcpy(nextSlot + 8, &fn, sizeof(RC::Unreal::FName));
    // Increment Num
    ++(*listNum);

    VLOG(STR("[MoriaCppMod] [SubGoat] appended: goat={:p} fn=OnNpcRescued_Event_2 ; new num={}\n"), (void*)goat, *listNum);
    showOnScreen(L"Goat subscribed to OnNpcRescued", 2.5f, 0.4f, 0.9f, 0.4f);
}

// Find any BP_NpcGoat_C actor in the world (could be desktop's
// deeps placement OR our NUM--summoned goat). Dump its
// MorNPCComponent runtime flag values that desktop asked about:
// bIsRescued, bRescueInteractionEnabled, bManageInteractionEnabled,
// bInteractionEnabled. Also probe for any "CurrentInteraction" /
// "ActiveInteraction" field that might hold the runtime-selected
// prompt.
void dumpDeepsGoatNpcFlags()
{
    VLOG(STR("[MoriaCppMod] [Probe] === goat MorNPCComponent runtime flags ===\n"));
    // Resolve BP_NpcGoat_C class — should be loaded since deeps
    // is streamed in.
    UClass* goatCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
    if (!goatCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] BP_NpcGoat_C class not loaded — cannot find world goat\n"));
        return;
    }
    // FindAllOf is exact-match; probe several candidate class
    // names since desktop may have introduced a Survivor subclass.
    const wchar_t* probeCandidates[] = {
            STR("BP_NpcGoat_C"),
            STR("BP_NpcGoat_Survivor_C"),
            STR("BP_NpcGoat_Survivor_1_C"),
            STR("BP_NpcGoat_Survivor_2_C"),
            STR("BP_NpcGoat_Survivor_3_C"),
            STR("BP_NpcGoat_Wanderer_C"),
            STR("BP_NpcGoat_Wanderer_1_C"),
            STR("BP_PorterGoat_C"),
            STR("BP_PorterGoat_Survivor_1_C"),
    };
    std::vector<UObject*> goats;
    for (auto* cn : probeCandidates)
    {
        std::vector<UObject*> hit;
        if (seh_findAllOf(cn, &hit) && !hit.empty())
        {
            for (UObject* o : hit)
                goats.push_back(o);
        }
    }
    if (goats.empty())
    {
        VLOG(STR("[MoriaCppMod] [Probe] no goat-class instances in loaded world (tried 9 candidates)\n"));
        return;
    }
    // Resolve MorNPCComponent class once.
    UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
    if (!npcCompCls) return;

    for (size_t i = 0; i < goats.size() && i < 4; ++i)
    {
        UObject* g = goats[i];
        if (!g || !isObjectAlive(g)) continue;
        std::wstring gName;
        try
        {
            gName = g->GetName();
        }
        catch (...)
        {
        }
        VLOG(STR("[MoriaCppMod] [Probe]   goat[{}] {} ptr={:p}\n"), i, gName.c_str(), (void*)g);

        // Get MorNPCComponent via GetComponentByClass.
        auto* getCompFn = g->GetFunctionByNameInChain(STR("GetComponentByClass"));
        if (!getCompFn) continue;
        int sz = getCompFn->GetParmsSize();
        std::vector<uint8_t> buf(sz, 0);
        writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
        if (!safeProcessEvent(g, getCompFn, buf.data())) continue;
        UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
        if (!npcComp || !isObjectAlive(npcComp)) continue;

        // Read each flag desktop asked about.
        auto readBool = [npcComp](const wchar_t* name) -> std::wstring {
            auto* bp = resolveBoolProperty(npcComp, name);
            if (!bp) return STR("(not found)");
            return bp->GetPropertyValueInContainer(npcComp) ? STR("true") : STR("false");
        };
        VLOG(STR("[MoriaCppMod] [Probe]     bIsRescued                 = {}\n"), readBool(STR("bIsRescued")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bInteractionEnabled        = {}\n"), readBool(STR("bInteractionEnabled")).c_str());
        // [v1.2.9 BASELINE 2026-05-10] Log NpcGuid value (not
        // just existence) — desktop wants to confirm BeginPlay
        // auto-init populated it post-whitelist re-add.
        uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
        if (guidPtr)
        {
            uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
            bool isZero = (g[0] | g[1] | g[2] | g[3]) == 0;
            VLOG(STR("[MoriaCppMod] [Probe]     NpcGuid                    = {:08X}-{:08X}-{:08X}-{:08X} {}\n"),
                 g[0],
                 g[1],
                 g[2],
                 g[3],
                 isZero ? STR("(ZERO — not registered)") : STR("(non-zero — registered)"));
        }
        else
        {
            VLOG(STR("[MoriaCppMod] [Probe]     NpcGuid                    = (property not found)\n"));
        }
        VLOG(STR("[MoriaCppMod] [Probe]     bRescueInteractionEnabled  = {}\n"), readBool(STR("bRescueInteractionEnabled")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bRescueInteractionRegister = {}\n"), readBool(STR("bRescueInteractionRegister")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bManageInteractionEnabled  = {}\n"), readBool(STR("bManageInteractionEnabled")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bRecruitInteractionEnabled = {}\n"), readBool(STR("bRecruitInteractionEnabled")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bTalkInteractionEnabled    = {}\n"), readBool(STR("bTalkInteractionEnabled")).c_str());
        VLOG(STR("[MoriaCppMod] [Probe]     bDetailsInteractionEnabled = {}\n"), readBool(STR("bDetailsInteractionEnabled")).c_str());

        // Probe candidate "CurrentInteraction" / "ActiveInteraction"
        // names — desktop wants to see the runtime-selected prompt.
        const wchar_t* probes[] = {
                STR("CurrentInteraction"),
                STR("ActiveInteraction"),
                STR("SelectedInteraction"),
                STR("HighestPriorityInteraction"),
                STR("PreferredInteraction"),
        };
        for (auto* nm : probes)
        {
            if (auto* prop = npcComp->GetClassPrivate()->FindProperty(RC::Unreal::FName(nm, RC::Unreal::FNAME_Find)))
            {
                std::wstring tn;
                try
                {
                    tn = prop->GetClass().GetName();
                }
                catch (...)
                {
                }
                VLOG(STR("[MoriaCppMod] [Probe]     {} EXISTS (type={} off=0x{:X})\n"), nm, tn.c_str(), (unsigned)prop->GetOffset_Internal());
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end goat MorNPCComponent flags dump ===\n"));
}

// Dump BP_StoryManager.Wanderers list (or equivalent). Per
// desktop: in vanilla flow, SurvivorRescue2 adds BP_NpcDwarf_Survivor_2
// to this list at spawn. If our goat isn't here, that's a
// different gate (AI-spawn-side filter, not CDO).
void dumpStoryMgrWanderers(UObject* worldCtx)
{
    VLOG(STR("[MoriaCppMod] [Probe] === BP_StoryManager.Wanderers dump ===\n"));
    auto* storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Tech/Managers/BP_StoryManager.BP_StoryManager_C"));
    if (!storyMgrCls)
    {
        VLOG(STR("[MoriaCppMod] [Probe] BP_StoryManager_C class missing\n"));
        return;
    }
    auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
    auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
    if (!getMgrFn || !fgkUtilsCDO || !worldCtx) return;
    int sz = getMgrFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
    writeGoatParm<UClass*>(getMgrFn, buf.data(), STR("ManagerClass"), storyMgrCls);
    if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data())) return;
    UObject* storyMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!storyMgr || !isObjectAlive(storyMgr))
    {
        VLOG(STR("[MoriaCppMod] [Probe] BP_StoryManager singleton missing\n"));
        return;
    }
    VLOG(STR("[MoriaCppMod] [Probe] BP_StoryManager singleton={:p}\n"), (void*)storyMgr);

    // [v7.1.0-rc.33 FULL SCHEMA WALK 2026-05-10] Walk the full
    // super chain on BP_StoryManager. Same pattern as the
    // settlement-mgr probe — every Array/Bool/Int/Name property,
    // ObjectProperty entries are class-name'd + flagged if the
    // goat is among them. Replaces the previous narrow
    // candidate-name probe (which found nothing).
    UObject* deepsGoat = nullptr;
    {
        std::vector<UObject*> hit;
        if (seh_findAnyGoatActor(&hit))
            for (UObject* g : hit)
                if (g && isObjectAlive(g))
                {
                    deepsGoat = g;
                    break;
                }
    }
    VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match detection): {:p}\n"), (void*)deepsGoat);

    int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0;
    for (auto* strct = static_cast<UStruct*>(storyMgr->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
    {
        std::wstring scopeCls;
        try
        {
            scopeCls = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            std::wstring pn, tn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                tn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            unsigned off = (unsigned)prop->GetOffset_Internal();
            uint8_t* slot = reinterpret_cast<uint8_t*>(storyMgr) + off;

            if (tn == STR("ArrayProperty"))
            {
                auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                FProperty* inner = nullptr;
                try
                {
                    inner = arrProp->GetInner();
                }
                catch (...)
                {
                }
                std::wstring innerTn;
                if (inner)
                {
                    try
                    {
                        innerTn = inner->GetClass().GetName();
                    }
                    catch (...)
                    {
                    }
                }
                void** data = reinterpret_cast<void**>(slot + 0);
                int32_t* num = reinterpret_cast<int32_t*>(slot + 8);
                VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                     scopeCls.c_str(),
                     pn.c_str(),
                     off,
                     innerTn.empty() ? STR("<unknown>") : innerTn.c_str(),
                     *num);
                if (innerTn == STR("ObjectProperty") && *num > 0 && *data)
                {
                    UObject** entries = reinterpret_cast<UObject**>(*data);
                    int limit = (*num < 32) ? *num : 32;
                    for (int j = 0; j < limit; ++j)
                    {
                        UObject* e = entries[j];
                        if (!e) continue;
                        if ((uintptr_t)e < 0x10000) continue;
                        if (!isObjectAlive(e)) continue;
                        std::wstring eName, eCls;
                        try
                        {
                            eName = e->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            eCls = e->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                        bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                        VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"), j, eName.c_str(), eCls.c_str(), isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                    }
                }
                ++arrCount;
            }
            else if (tn == STR("BoolProperty"))
            {
                uint8_t b = *slot;
                VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"), scopeCls.c_str(), pn.c_str(), off, b);
                ++boolCount;
            }
            else if (tn == STR("IntProperty"))
            {
                int32_t v = *reinterpret_cast<int32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"), scopeCls.c_str(), pn.c_str(), off, v);
                ++intCount;
            }
            else if (tn == STR("NameProperty"))
            {
                uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"), scopeCls.c_str(), pn.c_str(), off, fnameRaw[0], fnameRaw[1]);
                ++nameCount;
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [Probe] === end BP_StoryManager dump ({} arrays, {} bools, {} ints, {} names) ===\n"), arrCount, boolCount, intCount, nameCount);
}

// Read the goat's NpcGuid (FGuid struct, 16 bytes via reflection)
// and call MorPlayerController::ServerSendNpcToSettlement to assign
// it to the player's primary settlement (waypoint 0). Cleanest way
// to flip the goat's state to "settlement member" without going
// through the rescue dialog flow.
void assignGoatToSettlement(UObject* npcComp, int32_t waypointId)
{
    if (!npcComp || !isObjectAlive(npcComp)) return;
    if (!m_localPC || !isObjectAlive(m_localPC))
    {
        VLOG(STR("[MoriaCppMod] [Goat] assignGoatToSettlement: no local PC\n"));
        return;
    }

    // FGuid is 16 bytes (4 uint32). Read it via reflection from the
    // NpcGuid UPROPERTY. Templated access wants the right size, so
    // we use a raw byte pointer through GetValuePtrByPropertyNameInChain<uint8_t>.
    uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
    if (!guidPtr)
    {
        VLOG(STR("[MoriaCppMod] [Goat] assignGoatToSettlement: NpcGuid property missing\n"));
        return;
    }

    auto* sendFn = m_localPC->GetFunctionByNameInChain(STR("ServerSendNpcToSettlement"));
    if (!sendFn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ServerSendNpcToSettlement UFunction missing on PC\n"));
        return;
    }

    int sz = sendFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    // Find param offsets via reflection — no hard-coded layout.
    auto* pGuid = findParam(sendFn, STR("NpcGuid"));
    auto* pWp = findParam(sendFn, STR("SettlementWaypointID"));
    if (!pGuid || !pWp)
    {
        VLOG(STR("[MoriaCppMod] [Goat] ServerSendNpcToSettlement params missing (guid={:p} wp={:p})\n"), (void*)pGuid, (void*)pWp);
        return;
    }
    // Copy the 16-byte FGuid into the parm slot.
    std::memcpy(buf.data() + pGuid->GetOffset_Internal(), guidPtr, 16);
    *reinterpret_cast<int32_t*>(buf.data() + pWp->GetOffset_Internal()) = waypointId;

    // Log the guid we're sending for verification.
    uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
    VLOG(STR("[MoriaCppMod] [Goat] firing ServerSendNpcToSettlement(guid={:08X}-{:08X}-{:08X}-{:08X}, waypoint={})\n"), g[0], g[1], g[2], g[3], waypointId);
    safeProcessEvent(m_localPC, sendFn, buf.data());
    VLOG(STR("[MoriaCppMod] [Goat] ServerSendNpcToSettlement returned\n"));
}

// Set the AI controller's blackboard key "LeashActor" to the
// player pawn. Per desktop brief: Bst_NPCGoatWorkPorter_C reads
// this key for its FollowPlayer task. The vanilla EQS that picks
// the leash target (EQS_Npc_PorterLeashPlayer) doesn't pick up
// the player, so we set the key directly. Re-set per tick because
// EQS may overwrite it.
// [DIAG 2026-07-03] Throttled so a follow-state toggle logs the full
// chain without spamming at 1 Hz forever. Logs once, then goes quiet
// for 5s. Reveals: controller class, whether the Blackboard component
// was found (and under which property), and whether SetValueAsObject
// dispatched — the exact point where follow silently dies on Tobi's
// v1.10.0 goat if it does.
ULONGLONG m_lastLeashDiagMs{0};
void setGoatLeashActor(UObject* ctrl, UObject* playerPawn)
{
    ULONGLONG nowd = GetTickCount64();
    bool diag = (nowd - m_lastLeashDiagMs) > 5000;
    if (diag) m_lastLeashDiagMs = nowd;

    if (!ctrl || !isObjectAlive(ctrl))
    {
        if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: ctrl null/dead\n"));
        return;
    }
    if (!playerPawn || !isObjectAlive(playerPawn))
    {
        if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: playerPawn null/dead\n"));
        return;
    }
    std::wstring ctrlCls;
    try
    {
        ctrlCls = ctrl->GetClassPrivate()->GetName();
    }
    catch (...)
    {
    }
    // AAIController has a "Blackboard" UPROPERTY (UE4 standard) —
    // alternative name "BlackboardComp" on some custom controllers.
    const wchar_t* bbProp = STR("Blackboard");
    UObject** bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Blackboard"));
    if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr))
    {
        bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComp"));
        bbProp = STR("BlackboardComp");
    }
    if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr))
    {
        if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: no Blackboard on ctrl cls='{}' (tried Blackboard + BlackboardComp)\n"), ctrlCls.c_str());
        return;
    }
    UObject* bb = *bbPtr;
    auto* setObjFn = bb->GetFunctionByNameInChain(STR("SetValueAsObject"));
    if (!setObjFn)
    {
        if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: SetValueAsObject missing on bb\n"));
        return;
    }
    auto* pKey = findParam(setObjFn, STR("KeyName"));
    auto* pVal = findParam(setObjFn, STR("ObjectValue"));
    if (!pKey || !pVal)
    {
        if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: SetValueAsObject params missing (key={:p} val={:p})\n"), (void*)pKey, (void*)pVal);
        return;
    }
    int sz = setObjFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    // FName "LeashActor"
    RC::Unreal::FName keyName(STR("LeashActor"), RC::Unreal::FNAME_Add);
    std::memcpy(buf.data() + pKey->GetOffset_Internal(), &keyName, sizeof(RC::Unreal::FName));
    *reinterpret_cast<UObject**>(buf.data() + pVal->GetOffset_Internal()) = playerPawn;
    bool ok = safeProcessEvent(bb, setObjFn, buf.data());
    if (diag) VLOG(STR("[MoriaCppMod] [Leash] SET LeashActor=player on ctrl='{}' bbProp='{}' PE_ok={}\n"), ctrlCls.c_str(), bbProp, ok);
}

// [rc.40 STAY FIX 2026-06-10] Clear the LeashActor blackboard key
// so the BT FollowPlayer task has no target and idles. Gating
// setGoatLeashActor refresh alone (rc.39) wasn't enough — the
// stored value persists after we stop refreshing, so the BT keeps
// following the cached pointer. Must actively clear on entry to
// stayMode. Uses ClearValue if available, falls back to
// SetValueAsObject(nullptr).
void clearGoatLeashActor(UObject* ctrl)
{
    if (!ctrl || !isObjectAlive(ctrl)) return;
    UObject** bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Blackboard"));
    if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr)) bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComp"));
    if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr)) return;
    UObject* bb = *bbPtr;
    RC::Unreal::FName keyName(STR("LeashActor"), RC::Unreal::FNAME_Add);

    // Preferred: ClearValue(KeyName) — leaves key in "unset" state
    if (auto* clearFn = bb->GetFunctionByNameInChain(STR("ClearValue")))
    {
        auto* pKey = findParam(clearFn, STR("KeyName"));
        if (pKey)
        {
            int sz = clearFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pKey->GetOffset_Internal(), &keyName, sizeof(RC::Unreal::FName));
            try
            {
                safeProcessEvent(bb, clearFn, buf.data());
            }
            catch (...)
            {
            }
            return;
        }
    }

    // Fallback: SetValueAsObject(KeyName, nullptr) — writes null
    auto* setObjFn = bb->GetFunctionByNameInChain(STR("SetValueAsObject"));
    if (!setObjFn) return;
    auto* pKey = findParam(setObjFn, STR("KeyName"));
    auto* pVal = findParam(setObjFn, STR("ObjectValue"));
    if (!pKey || !pVal) return;
    int sz = setObjFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    std::memcpy(buf.data() + pKey->GetOffset_Internal(), &keyName, sizeof(RC::Unreal::FName));
    *reinterpret_cast<UObject**>(buf.data() + pVal->GetOffset_Internal()) = nullptr;
    try
    {
        safeProcessEvent(bb, setObjFn, buf.data());
    }
    catch (...)
    {
    }
}

// [rc.54 PROBE V 2026-06-16] BP_NPCManager_C ValidNpc* dump.
// SaveGameObjectId lives on the MANAGER, not individual NPCs.
// Manager recreates per-NPC actors on restore from NpcInfo
// metadata, gated by ValidNpcClasses (off=0x0808). This probe
// iterates each gating array and prints every entry so we
// can confirm whether BP_NpcGoat_C is in the allowlist.
//
// Layout (UE4.27 standard):
//   TArray:         { void* Data; int32 Num; int32 Max; }  // 16 bytes
//   FSoftObjectPath: { FName AssetPathName; FString SubPathString; }  // 24 bytes
//   FSoftClassPath: derived from FSoftObjectPath              // 24 bytes
//   FString:        { TCHAR* Data; int32 Num; int32 Max; }   // 16 bytes
struct PVTArrayHdr
{
    void* Data;
    int32_t Num;
    int32_t Max;
};
struct PVStringHdr
{
    wchar_t* Data;
    int32_t Num;
    int32_t Max;
};

// [rc.60 TAME GOAT 2026-06-28] Strip dwarven-NPC components and
// stop the behavior tree so the goat behaves like wild fauna
// (passive, controllable via our MoveToActor calls) rather
// than a working dwarf settler. Called after spawn from
// spawnBellGoat once the actor is fully constructed.
void tameSpawnedGoat(UObject* goat)
{
    if (!goat || !isObjectAlive(goat)) return;
    VLOG(STR("[MoriaCppMod] [TameGoat] === taming spawned goat={:p} ===\n"), (void*)goat);

    // === Step 1: Destroy dwarven inventory child actors ===
    // DwarfMirror BP attaches these at construction. They have to go.
    // Class names to strip:
    const wchar_t* dwarfInvClasses[] = {
            STR("BP_ContainerItem_Dwarf_BodyInventoryNPC_C"),
            STR("BP_ContainerItem_Dwarf_Slot_Helmet_C"),
            STR("BP_ContainerItem_Dwarf_Slot_Torso_C"),
            STR("BP_ContainerItem_Dwarf_Slot_Boots_C"),
            STR("BP_ContainerItem_Dwarf_Slot_Gloves_C"),
            STR("BP_ContainerItem_Dwarf_Slot_MainHandNPC_C"),
            STR("BP_ContainerItem_Dwarf_Slot_OffHandNPC_C"),
    };
    int destroyedCount = 0;
    for (const wchar_t* clsName : dwarfInvClasses)
    {
        std::vector<UObject*> found;
        if (!seh_findAllOf(clsName, &found)) continue;
        for (UObject* item : found)
        {
            if (!item || !isObjectAlive(item)) continue;
            // Only destroy items whose owner is our goat
            UObject* owner = nullptr;
            if (auto* getOwnerFn = item->GetFunctionByNameInChain(STR("GetOwner")))
            {
                struct
                {
                    UObject* Ret;
                } op{};
                if (safeProcessEvent(item, getOwnerFn, &op)) owner = op.Ret;
            }
            if (owner != goat) continue;

            if (auto* destFn = item->GetFunctionByNameInChain(STR("K2_DestroyActor")))
            {
                safeProcessEvent(item, destFn, nullptr);
                ++destroyedCount;
                VLOG(STR("[MoriaCppMod] [TameGoat] destroyed dwarven inventory child '{}' @{:p}\n"), clsName, (void*)item);
            }
        }
    }
    VLOG(STR("[MoriaCppMod] [TameGoat] destroyed {} dwarven inventory child actors\n"), destroyedCount);

    // === Step 2: Stop behavior tree on the goat's AI controller ===
    UObject* ctrl = nullptr;
    if (auto* getCtrlFn = goat->GetFunctionByNameInChain(STR("GetController")))
    {
        struct
        {
            UObject* Ret;
        } cp{};
        if (safeProcessEvent(goat, getCtrlFn, &cp)) ctrl = cp.Ret;
    }
    if (!ctrl || !isObjectAlive(ctrl))
    {
        VLOG(STR("[MoriaCppMod] [TameGoat] no AI controller found — skip BT stop\n"));
    }
    else
    {
        std::wstring ctrlCls = safeClassName(ctrl);
        VLOG(STR("[MoriaCppMod] [TameGoat] AI controller class='{}'\n"), ctrlCls.c_str());

        // Find BrainComponent (BehaviorTreeComponent is a subclass)
        UClass* brainCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/AIModule.BrainComponent"));
        UObject* brain = nullptr;
        if (brainCls)
        {
            if (auto* getCompFn = ctrl->GetFunctionByNameInChain(STR("GetComponentByClass")))
            {
                struct
                {
                    UClass* C;
                    UObject* Ret;
                } bp{};
                bp.C = brainCls;
                if (safeProcessEvent(ctrl, getCompFn, &bp)) brain = bp.Ret;
            }
        }
        if (!brain || !isObjectAlive(brain))
        {
            VLOG(STR("[MoriaCppMod] [TameGoat] no BrainComponent found on controller — BT not stopped\n"));
        }
        else
        {
            std::wstring brainCl = safeClassName(brain);
            VLOG(STR("[MoriaCppMod] [TameGoat] BrainComponent class='{}'\n"), brainCl.c_str());

            // Call StopLogic(FString Reason)
            if (auto* stopFn = brain->GetFunctionByNameInChain(STR("StopLogic")))
            {
                int sz = stopFn->GetParmsSize();
                std::vector<uint8_t> sbuf(sz, 0);
                // Pack FString "tamed by porter-goat mod" — FString = (TCHAR* Data, int32 Num, int32 Max)
                const wchar_t* reason = STR("tamed by porter-goat mod");
                int32_t rlen = (int32_t)wcslen(reason) + 1;
                void* rbuf = FMemory::Malloc(rlen * sizeof(wchar_t), 8);
                if (rbuf)
                {
                    wmemcpy(static_cast<wchar_t*>(rbuf), reason, rlen);
                    auto* pReason = findParam(stopFn, STR("Reason"));
                    if (pReason)
                    {
                        uint8_t* p = sbuf.data() + pReason->GetOffset_Internal();
                        *reinterpret_cast<void**>(p + 0) = rbuf;
                        *reinterpret_cast<int32_t*>(p + 8) = rlen;
                        *reinterpret_cast<int32_t*>(p + 12) = rlen;
                    }
                    safeProcessEvent(brain, stopFn, sbuf.data());
                    VLOG(STR("[MoriaCppMod] [TameGoat] StopLogic('{}') fired on BrainComponent\n"), reason);
                    // Note: BP's StopLogic typically copies the FString, so we can free our buffer.
                    // If it didn't, we'd leak — small leak, acceptable.
                    FMemory::Free(rbuf);
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [TameGoat] StopLogic UFunction not findable on BrainComponent\n"));
            }
        }
    }

    VLOG(STR("[MoriaCppMod] [TameGoat] === done ===\n"));
}

// [rc.48 STORE RUNTIME ACTOR 2026-06-14] Call
// WorldState.StoreRuntimeActor(goat, &handle, bStoreStability=true)
// via PE. This is the missing piece that registers the goat actor
// with WorldState's save graph — without it the actor is destroyed
// on world unload (as Probe P consistently confirmed).
//
// Per Q.2 enumeration, ParmsSize=42:
//   parms[0..7]   = Actor* (8 bytes)
//   parms[8..39]  = FRuntimeActorHandle InOut (32 bytes, zero-init)
//   parms[40]     = bool bStoreStability (1 byte, true)
//   parms[41]     = bool ReturnValue (1 byte, engine fills)
//
// Logs handle bytes for struct-layout decode.
bool storeGoatInWorldState(UObject* goat)
{
    if (!goat || !isObjectAlive(goat))
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] goat null/dead — skip\n"));
        return false;
    }
    UObject* ws = nullptr;
    std::vector<UObject*> wsCands;
    if (findAllOfSafe(STR("MorSaveSystemWorldState"), wsCands))
    {
        for (UObject* o : wsCands)
        {
            if (!o || !isObjectAlive(o)) continue;
            std::wstring cn = safeClassName(o);
            if (cn.size() >= 9 && cn.substr(0, 9) == STR("Default__")) continue;
            ws = o;
            break;
        }
    }
    if (!ws)
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] WorldState singleton not findable\n"));
        return false;
    }
    UFunction* storeFn = nullptr;
    try
    {
        storeFn = ws->GetFunctionByNameInChain(STR("StoreRuntimeActor"));
    }
    catch (...)
    {
    }
    if (!storeFn)
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] StoreRuntimeActor UFunction not found\n"));
        return false;
    }
    int parmsSize = 0;
    try
    {
        parmsSize = storeFn->GetParmsSize();
    }
    catch (...)
    {
    }
    if (parmsSize != 42)
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] WARN: ParmsSize={} (expected 42 per Q.2 enumeration)\n"), parmsSize);
    }
    std::vector<uint8_t> parms(parmsSize > 0 ? parmsSize : 42, 0);

    // Resolve parm offsets by name to avoid hard-coded layout assumptions
    auto* pActor = findParam(storeFn, STR("Actor"));
    auto* pHandle = findParam(storeFn, STR("InOutRuntimeActorHandle"));
    auto* pStab = findParam(storeFn, STR("bStoreStability"));
    auto* pRet = findParam(storeFn, STR("ReturnValue"));
    if (!pActor || !pHandle || !pStab || !pRet)
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] Missing parm reflection: Actor={} Handle={} Stab={} Ret={}\n"), (void*)pActor, (void*)pHandle, (void*)pStab, (void*)pRet);
        return false;
    }
    *reinterpret_cast<UObject**>(parms.data() + pActor->GetOffset_Internal()) = goat;
    // [rc.52 STABILITY FLIP 2026-06-14] DC Tier 1 hypothesis: maybe
    // bStoreStability has inverted semantics — true=transient,
    // false=stable. We tried true in rc.48-rc.51 (returned SUCCESS
    // but actor didn't persist). Trying false now. If items
    // persist after reload → DC was right, ship rc.52. If not,
    // escalate back to DC for source reading.
    *reinterpret_cast<bool*>(parms.data() + pStab->GetOffset_Internal()) = false;
    // Handle region stays zero-initialized (engine fills it on success).

    VLOG(STR("[MoriaCppMod] [WorldStore] firing StoreRuntimeActor on ws={:p} goat={:p} (parmsSize={}, bStoreStability=FALSE per rc.52 DC hypothesis test)\n"),
         (void*)ws,
         (void*)goat,
         parmsSize);
    try
    {
        safeProcessEvent(ws, storeFn, parms.data());
    }
    catch (...)
    {
        VLOG(STR("[MoriaCppMod] [WorldStore] PE threw — failed\n"));
        return false;
    }

    bool success = *reinterpret_cast<bool*>(parms.data() + pRet->GetOffset_Internal());

    // Dump the 32-byte handle for struct-layout decode
    uint8_t* h = parms.data() + pHandle->GetOffset_Internal();
    VLOG(STR("[MoriaCppMod] [WorldStore] StoreRuntimeActor returned {} — handle bytes:\n"), success ? STR("SUCCESS") : STR("FAILURE"));
    VLOG(STR("[MoriaCppMod] [WorldStore]   +0x00: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
             "{:02X}\n"),
         h[0],
         h[1],
         h[2],
         h[3],
         h[4],
         h[5],
         h[6],
         h[7],
         h[8],
         h[9],
         h[10],
         h[11],
         h[12],
         h[13],
         h[14],
         h[15]);
    VLOG(STR("[MoriaCppMod] [WorldStore]   +0x10: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
             "{:02X}\n"),
         h[16],
         h[17],
         h[18],
         h[19],
         h[20],
         h[21],
         h[22],
         h[23],
         h[24],
         h[25],
         h[26],
         h[27],
         h[28],
         h[29],
         h[30],
         h[31]);
    // Try interpreting first 16 bytes as FGuid
    uint32_t* g32 = reinterpret_cast<uint32_t*>(h);
    VLOG(STR("[MoriaCppMod] [WorldStore]   (first 16B as FGuid: {:08X}-{:08X}-{:08X}-{:08X})\n"), g32[0], g32[1], g32[2], g32[3]);

    return success;
}

// One-shot read of the controller's FSM current-state name for
// diagnostic. Per brief: the FSM lives at controller.BehaviorFSMComp,
// and the Porter follow logic only runs when state == "WorkTime".
// If state stays "Unaware" we know the FSM isn't auto-transitioning
// and we'll need a force-transition call.
void logGoatFSMState(UObject* ctrl, const wchar_t* tag)
{
    if (!ctrl || !isObjectAlive(ctrl)) return;
    UObject** fsmPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
    if (!fsmPtr || !*fsmPtr || !isObjectAlive(*fsmPtr))
    {
        VLOG(STR("[MoriaCppMod] [Goat][{}] BehaviorFSMComp not resolvable on ctrl={:p}\n"), tag, (void*)ctrl);
        return;
    }
    UObject* fsm = *fsmPtr;
    // Try common state-name property names.
    const wchar_t* candidates[] = {
            STR("CurrentStateName"),
            STR("CurrentState"),
            STR("StateName"),
            STR("ActiveState"),
    };
    for (auto* nm : candidates)
    {
        if (auto* fnPtr = fsm->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(nm))
        {
            std::wstring s;
            try
            {
                s = fnPtr->ToString();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [Goat][{}] FSM.{} = '{}'\n"), tag, nm, s.c_str());
            return;
        }
    }
    VLOG(STR("[MoriaCppMod] [Goat][{}] FSM has no recognized state-name property; need schema dump\n"), tag);
}

// Stop any standard Unreal UBrainComponent / behavior tree running
// on the goat's AIController. AAIController has a `BrainComponent`
// UPROPERTY (FObjectProperty) — UBrainComponent has `StopLogic(FString)`
// UFunction. Calling this halts whatever behavior tree the
// controller was running, including the Porter brain that gets
// attached after registration.
void stopGoatAIBrain(UObject* ctrl)
{
    if (!ctrl || !isObjectAlive(ctrl)) return;
    UObject** brainPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BrainComponent"));
    if (!brainPtr || !*brainPtr || !isObjectAlive(*brainPtr))
    {
        VLOG(STR("[MoriaCppMod] [Goat] BrainComponent missing on AIController={:p}\n"), (void*)ctrl);
        return;
    }
    UObject* brain = *brainPtr;
    auto* stopFn = brain->GetFunctionByNameInChain(STR("StopLogic"));
    if (!stopFn)
    {
        VLOG(STR("[MoriaCppMod] [Goat] StopLogic UFunction missing on BrainComponent={:p}\n"), (void*)brain);
        return;
    }
    // Build FString "PorterMod" reason so it shows up in any logs.
    const wchar_t* reason = STR("PorterMod");
    int32_t strLen = static_cast<int32_t>(wcslen(reason)) + 1;
    void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
    if (!strBuf) return;
    wmemcpy(static_cast<wchar_t*>(strBuf), reason, strLen);
    int sz = stopFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    auto* pReason = findParam(stopFn, STR("Reason"));
    if (pReason)
    {
        uint8_t* fstr = buf.data() + pReason->GetOffset_Internal();
        *reinterpret_cast<void**>(fstr + 0) = strBuf;
        *reinterpret_cast<int32_t*>(fstr + 8) = strLen;
        *reinterpret_cast<int32_t*>(fstr + 12) = strLen;
    }
    safeProcessEvent(brain, stopFn, buf.data());
    VLOG(STR("[MoriaCppMod] [Goat] BrainComponent::StopLogic('PorterMod') fired on brain={:p}\n"), (void*)brain);
}

// Replace the goat's auto-spawned BP_Goat_AIController_C with a vanilla
// AAIController so the FGK behavior tree (BSt_NPCGoatRoot, prey-flee
// states, etc.) never gets a chance to drive the pawn. After the swap,
// the only thing moving the goat is our own MoveToActor calls.
//
// Steps:
//   1) Old controller -> UnPossess()
//   2) Old controller -> K2_DestroyActor()
//   3) Spawn /Script/AIModule.AIController via UGameplayStatics
//   4) New controller -> Possess(pawn) (NB: native fn, not UFUNCTION on
//      AAIController in 4.27 — APawn::PossessedBy is the path; we use
//      the AIController's K2_Possess if exposed, or AController->Possess UFUNCTION)
// Returns the new controller, or nullptr on failure.
UObject* replaceGoatControllerWithVanilla(UObject* goat, UObject* oldCtrl)
{
    if (!goat || !isObjectAlive(goat)) return nullptr;
    if (!ensureGoatSpawnBindings()) return nullptr; // need GS:BeginDeferred

    // Resolve vanilla AAIController class once and cache.
    if (!m_vanillaAIControllerClass)
    {
        m_vanillaAIControllerClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/AIModule.AIController"));
        if (!m_vanillaAIControllerClass)
        {
            VLOG(STR("[MoriaCppMod] [Goat] vanilla AAIController class not resident\n"));
            return nullptr;
        }
    }

    // (1) UnPossess + (2) destroy the old AI controller, if any.
    if (oldCtrl && isObjectAlive(oldCtrl))
    {
        if (auto* upFn = oldCtrl->GetFunctionByNameInChain(STR("UnPossess"))) safeProcessEvent(oldCtrl, upFn, nullptr);
        if (auto* dFn = oldCtrl->GetFunctionByNameInChain(STR("K2_DestroyActor"))) safeProcessEvent(oldCtrl, dFn, nullptr);
        VLOG(STR("[MoriaCppMod] [Goat] old AIController unpossessed + destroyed\n"));
    }

    // (3) Spawn the vanilla AAIController at the goat's location.
    FVec3f loc{};
    if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
    {
        struct
        {
            FVec3f Ret{};
        } lp{};
        if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
    }
    FTransformRaw xform{};
    xform.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    xform.Translation = loc;
    xform.Scale3D = {1.0f, 1.0f, 1.0f};

    int sz = m_goatBeginSpawnFn->GetParmsSize();
    std::vector<uint8_t> buf(sz, 0);
    writeGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
    writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), m_vanillaAIControllerClass);
    writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"), xform);
    writeGoatParm<uint8_t>(m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1); // AlwaysSpawn (controllers don't collide)
    if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) return nullptr;
    UObject* newCtrl = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
    if (!newCtrl)
    {
        VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawn returned null\n"));
        return nullptr;
    }
    int sz2 = m_goatFinishSpawnFn->GetParmsSize();
    std::vector<uint8_t> buf2(sz2, 0);
    writeGoatParm<UObject*>(m_goatFinishSpawnFn, buf2.data(), STR("Actor"), newCtrl);
    writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
    safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

    // (4) Possess the goat. AController::Possess is a native fn but
    // there's a Blueprint-callable wrapper Possess on AAIController in
    // 4.27. Try Possess first, fall back to APawn::PossessedBy via
    // setting the Controller UPROPERTY directly if the UFUNCTION lookup
    // fails (we'd need to also set Pawn on the controller and fire
    // OnPossess hooks — risky, so prefer the UFUNCTION path).
    if (auto* possFn = newCtrl->GetFunctionByNameInChain(STR("Possess")))
    {
        struct
        {
            UObject* Pawn{nullptr};
        } pp{};
        pp.Pawn = goat;
        safeProcessEvent(newCtrl, possFn, &pp);
        VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawned + possessing goat: {:p}\n"), (void*)newCtrl);
        return newCtrl;
    }
    else if (auto* k2pFn = newCtrl->GetFunctionByNameInChain(STR("K2_Possess")))
    {
        struct
        {
            UObject* Pawn{nullptr};
        } pp{};
        pp.Pawn = goat;
        safeProcessEvent(newCtrl, k2pFn, &pp);
        VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController possessed via K2_Possess: {:p}\n"), (void*)newCtrl);
        return newCtrl;
    }
    VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawned but no Possess UFunction found\n"));
    return newCtrl;
}

// One-shot dump of a live MorNPCComponent: walks every property on the
// class+supers and prints type + live value for ObjectProperty,
// StrProperty, NameProperty, TextProperty, BoolProperty, StructProperty
// (only Guid struct expanded). We need to find the FGuid property
// (the npc guid) so we can call MorPlayerController::ServerRescueNpc
// directly without going through the failing widget gate.
bool m_npcCompSchemaDumped{false};
void dumpMorNPCComponentSchema(UObject* npcComp)
{
    if (m_npcCompSchemaDumped) return;
    m_npcCompSchemaDumped = true;
    if (!npcComp) return;

    UClass* cls = nullptr;
    try
    {
        cls = npcComp->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (!cls) return;
    std::wstring clsName;
    try
    {
        clsName = cls->GetName();
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [NPCDump] === {} dump start (instance={:p}) ===\n"), clsName.c_str(), (void*)npcComp);

    int propCount = 0;
    for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
    {
        std::wstring strctName;
        try
        {
            strctName = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            if (propCount >= 400) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }

            std::wstring valueStr;
            try
            {
                int32_t off = prop->GetOffset_Internal();
                uint8_t* base = reinterpret_cast<uint8_t*>(npcComp);
                uint8_t* slot = base + off;
                if (pcn == STR("ObjectProperty") || pcn == STR("WeakObjectProperty") || pcn == STR("ClassProperty") || pcn == STR("InterfaceProperty"))
                {
                    UObject* val = *reinterpret_cast<UObject**>(slot);
                    if (val)
                    {
                        std::wstring vName, vCls;
                        try
                        {
                            vName = val->GetName();
                        }
                        catch (...)
                        {
                        }
                        try
                        {
                            vCls = val->GetClassPrivate()->GetName();
                        }
                        catch (...)
                        {
                        }
                        valueStr = STR(" value=") + vName + STR(" (") + vCls + STR(")");
                    }
                    else
                        valueStr = STR(" value=null");
                }
                else if (pcn == STR("StrProperty"))
                {
                    FString* fs = reinterpret_cast<FString*>(slot);
                    std::wstring s;
                    try
                    {
                        s = fs->GetCharArray().GetData() ? fs->GetCharArray().GetData() : STR("");
                    }
                    catch (...)
                    {
                    }
                    valueStr = STR(" value=\"") + s + STR("\"");
                }
                else if (pcn == STR("NameProperty"))
                {
                    FName* fn = reinterpret_cast<FName*>(slot);
                    std::wstring s;
                    try
                    {
                        s = fn->ToString();
                    }
                    catch (...)
                    {
                    }
                    valueStr = STR(" value=") + s;
                }
                else if (pcn == STR("TextProperty"))
                {
                    FText* ft = reinterpret_cast<FText*>(slot);
                    std::wstring s;
                    try
                    {
                        s = ft->ToString();
                    }
                    catch (...)
                    {
                    }
                    valueStr = STR(" value=\"") + s + STR("\"");
                }
                else if (pcn == STR("BoolProperty"))
                {
                    valueStr = (*slot != 0) ? STR(" value=true") : STR(" value=false");
                }
                else if (pcn == STR("StructProperty"))
                {
                    // Best-effort: if struct is 16 bytes, treat as Guid
                    // and dump 4 uint32s. Caller must verify this is
                    // really a Guid by checking the property metadata
                    // (we can't easily do that here from UE4SS).
                    uint32_t* g = reinterpret_cast<uint32_t*>(slot);
                    wchar_t buf[80];
                    swprintf(buf, 80, STR(" maybe-guid=%08X-%08X-%08X-%08X"), g[0], g[1], g[2], g[3]);
                    valueStr = buf;
                }
                else if (pcn == STR("UInt32Property") || pcn == STR("IntProperty"))
                {
                    int32_t v = *reinterpret_cast<int32_t*>(slot);
                    wchar_t buf[40];
                    swprintf(buf, 40, STR(" value=%d (0x%X)"), v, v);
                    valueStr = buf;
                }
            }
            catch (...)
            {
            }

            VLOG(STR("[MoriaCppMod] [NPCDump] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                 clsName.c_str(),
                 strctName.c_str(),
                 pn.c_str(),
                 pcn.c_str(),
                 (unsigned)prop->GetOffset_Internal(),
                 valueStr.c_str());
            ++propCount;
        }
        if (propCount >= 400) break;
    }
    VLOG(STR("[MoriaCppMod] [NPCDump] === {} dump done ({} props) ===\n"), clsName.c_str(), propCount);

    // [v1.1.1 EXTENDED 2026-05-09] Per desktop brief: also dump
    // EVERY UFunction on the class chain, no filter. We're hunting
    // for post-rescue state-flip functions (CompleteRescue,
    // FinalizeRecruitment, MarkAsRecruited, AssignToSettlement,
    // SetSettlementOwner, etc.). Save persistence failed under
    // v1.1.1 whitelist alone — registration must be insufficient
    // and we need a second call to flag the goat as a real
    // settlement member.
    int fnCount = 0;
    for (auto* fn : cls->ForEachFunctionInChain())
    {
        if (fnCount >= 600) break;
        std::wstring fnName;
        try
        {
            fnName = fn->GetName();
        }
        catch (...)
        {
        }
        int parmCount = 0;
        std::wstring paramSig;
        for (auto* prop : fn->ForEachProperty())
        {
            if (parmCount >= 6) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }
            if (!paramSig.empty()) paramSig += STR(", ");
            paramSig += pcn + STR(" ") + pn;
            ++parmCount;
        }
        VLOG(STR("[MoriaCppMod] [NPCDump] UFUNC {}({})\n"), fnName.c_str(), paramSig.c_str());
        ++fnCount;
    }
    VLOG(STR("[MoriaCppMod] [NPCDump] === {} ufunc dump done ({} fns) ===\n"), clsName.c_str(), fnCount);
}

// Proper NPC-manager spawn path retired — see feedback memory
// `feedback_proper_npc_spawn_dead_end.md`. [SpawnDiag] PE hook
// below remains for free observability.

// PE pre-hook: ServerSetInteractableCustomName fires when ANY player
// renames ANY interactable (chest, sign, etc.). When the new name
// is "goat" (case-insensitive), capture the chest reference so we
// can use it as the goat's persistent backing storage. Phase 2 will
// extract the chest's save-game FGuid and persist to JSON; for now
// we just identify-and-log + dump the chest's full property schema
// (one-shot) so we can find the GUID field for next iteration.
bool m_goatChestSchemaDumped{false};
UObject* m_goatChestActor{nullptr};
std::wstring m_goatChestClassName;
std::wstring m_goatChestPath;
// Deferred-toast: rename-UI close animation overpaints any toast
// shown during onChestRenamePre. Set this in the PE callback;
// tickFollowGoats decrements and fires the toast when it hits 1.
int m_pendingChestToastTicks{0};
// Deferred property-schema dump: walking ObjectProperty values
// mid-rename crashed (FName::ToString on stale ObjectIndex).
// Defer until the chest's state has settled. Fires once.
int m_pendingChestSchemaTicks{0};
void onChestRenamePre(UObject* /*context*/, UFunction* func, void* parms)
{
    if (!func || !parms) return;

    // Read CustomName FString parameter.
    auto* pName = findParam(func, STR("CustomName"));
    if (!pName) return;
    FString* nameStr = reinterpret_cast<FString*>(static_cast<uint8_t*>(parms) + pName->GetOffset_Internal());
    const wchar_t* nameData = nullptr;
    try
    {
        nameData = nameStr->GetCharArray().GetData();
    }
    catch (...)
    {
    }
    if (!nameData) return;

    VLOG(STR("[MoriaCppMod] [GoatChest] ServerSetInteractableCustomName CustomName='{}'\n"), nameData);

    // Case-insensitive match against "goat".
    if (_wcsicmp(nameData, STR("goat")) != 0) return;

    // Read ObjectInteractable param — the chest being renamed.
    auto* pObj = findParam(func, STR("ObjectInteractable"));
    if (!pObj) return;
    UObject* chest = *reinterpret_cast<UObject**>(static_cast<uint8_t*>(parms) + pObj->GetOffset_Internal());
    if (!chest || !isObjectAlive(chest)) return;

    std::wstring chestName, chestCls, chestPath;
    try
    {
        chestName = chest->GetName();
    }
    catch (...)
    {
    }
    try
    {
        chestCls = chest->GetClassPrivate()->GetName();
    }
    catch (...)
    {
    }
    try
    {
        chestPath = chest->GetFullName();
    }
    catch (...)
    {
    }

    m_goatChestActor = chest;
    m_goatChestClassName = chestCls;
    m_goatChestPath = chestPath;

    VLOG(STR("[MoriaCppMod] [GoatChest] *** GOAT CHEST CAPTURED *** ptr={:p} name={} cls={} path={}\n"),
         (void*)chest,
         chestName.c_str(),
         chestCls.c_str(),
         chestPath.c_str());
    // Defer the on-screen toast — fired immediately here gets
    // overpainted by the closing rename UI. ~30 ticks ≈ 500ms at
    // 60Hz lets that animation finish first.
    m_pendingChestToastTicks = 30;
    // Defer the property-schema dump too — different reason: the
    // chest's properties are being mutated server-side mid-rename
    // and walking ObjectProperty values crashes inside FName
    // ToString. ~120 ticks (~2s) gives plenty of settle time.
    // Only schedule on first capture; subsequent goat-chest
    // renames don't re-dump.
    if (!m_goatChestSchemaDumped) m_pendingChestSchemaTicks = 120;

    // [PROPERTY DUMP REMOVED — caused crash during rename PE
    // callback because chest properties are mid-mutation
    // server-side; ObjectProperty values can be stale UObject*
    // pointers whose FName entries are not yet committed. Walking
    // them via GetName() crashes inside FName::ToString.
    // TO BRING BACK: defer the dump to the next tick via a
    // pending-flag set here, consumed in tickFollowGoats() — by
    // then the rename will have settled.]
}

// Walk every property on a chest actor, print name/type/offset.
// Live values for ObjectProperty / StrProperty / NameProperty /
// TextProperty / BoolProperty / StructProperty (treated as
// candidate guid). Used to locate the chest's save-game FGuid.
void dumpChestPropertySchema(UObject* chest)
{
    if (!chest) return;
    UClass* cls = nullptr;
    try
    {
        cls = chest->GetClassPrivate();
    }
    catch (...)
    {
    }
    if (!cls) return;
    std::wstring clsName;
    try
    {
        clsName = cls->GetName();
    }
    catch (...)
    {
    }
    VLOG(STR("[MoriaCppMod] [GoatChest] === {} property dump start (instance={:p}) ===\n"), clsName.c_str(), (void*)chest);

    int propCount = 0;
    for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
    {
        std::wstring strctName;
        try
        {
            strctName = strct->GetName();
        }
        catch (...)
        {
        }
        for (auto* prop : strct->ForEachProperty())
        {
            if (propCount >= 400) break;
            std::wstring pn, pcn;
            try
            {
                pn = prop->GetName();
            }
            catch (...)
            {
            }
            try
            {
                pcn = prop->GetClass().GetName();
            }
            catch (...)
            {
            }

            std::wstring valueStr;
            try
            {
                int32_t off = prop->GetOffset_Internal();
                uint8_t* base = reinterpret_cast<uint8_t*>(chest);
                uint8_t* slot = base + off;
                // Only read primitives. Skip ObjectProperty,
                // WeakObjectProperty, NameProperty, TextProperty —
                // those route through FName/FText ToString which
                // crashed with EXCEPTION_ACCESS_VIOLATION at
                // 0x100000017 on stale ObjectIndex slots in the
                // chest. C++ try/catch can't catch SEH AVs.
                if (pcn == STR("StrProperty"))
                {
                    FString* fs = reinterpret_cast<FString*>(slot);
                    const wchar_t* p = nullptr;
                    try
                    {
                        p = fs->GetCharArray().GetData();
                    }
                    catch (...)
                    {
                    }
                    valueStr = STR(" value=\"") + std::wstring(p ? p : STR("")) + STR("\"");
                }
                else if (pcn == STR("BoolProperty"))
                {
                    valueStr = (*slot != 0) ? STR(" value=true") : STR(" value=false");
                }
                else if (pcn == STR("StructProperty"))
                {
                    // Print first 16 bytes as 4 uint32s; if this
                    // is a Guid struct it'll be obvious.
                    uint32_t* g = reinterpret_cast<uint32_t*>(slot);
                    wchar_t buf[80];
                    swprintf(buf, 80, STR(" raw16=%08X-%08X-%08X-%08X"), g[0], g[1], g[2], g[3]);
                    valueStr = buf;
                }
                else if (pcn == STR("UInt32Property") || pcn == STR("IntProperty"))
                {
                    int32_t v = *reinterpret_cast<int32_t*>(slot);
                    wchar_t buf[40];
                    swprintf(buf, 40, STR(" value=%d"), v);
                    valueStr = buf;
                }
                // Object/Weak/Name/Text deliberately not read —
                // SEH-unsafe on chest actors mid-rename / with
                // stale slots.
            }
            catch (...)
            {
            }

            VLOG(STR("[MoriaCppMod] [GoatChest] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                 clsName.c_str(),
                 strctName.c_str(),
                 pn.c_str(),
                 pcn.c_str(),
                 (unsigned)prop->GetOffset_Internal(),
                 valueStr.c_str());
            ++propCount;
        }
        if (propCount >= 400) break;
    }
    VLOG(STR("[MoriaCppMod] [GoatChest] === {} property dump done ({} props) ===\n"), clsName.c_str(), propCount);
}

// [SUSPENDED 2026-05-09] InteractDiag — proved chests do NOT use
// ServerInteract (zero vanilla calls observed during real chest
// interactions). See `chest-link-future.md` for restart notes.

// PE pre-hook diagnostic: log every BP_RequestSpawn call (ours +
// every natural caller). Logs spawner class, context enum, and the
// requested character class path. In the Lower Deeps where natural
// goat spawns occur, we'll see the working shape (which spawner
// type, which context, which class) and can compare it to our
// failing NUM. attempts.
void onBPRequestSpawnPre(UObject* context, UFunction* func, void* parms)
{
    if (!context || !func || !parms) return;
    std::wstring spawnerCls = safeClassName(context);

    std::wstring chTypePath = STR("?");
    std::wstring spawnerArg = STR("?");
    int ctxVal = -1;

    try
    {
        if (auto* p = findParam(func, STR("InCharacterType")))
        {
            uint8_t* slot = static_cast<uint8_t*>(parms) + p->GetOffset_Internal();
            // FName AssetPathName at offset 16 (8 bytes).
            RC::Unreal::FName fn;
            std::memcpy(&fn, slot + 16, sizeof(RC::Unreal::FName));
            chTypePath = fn.ToString();
            if (chTypePath.empty()) chTypePath = STR("<empty>");
        }
    }
    catch (...)
    {
    }

    try
    {
        if (auto* p = findParam(func, STR("InSpawner")))
        {
            UObject* sp = *reinterpret_cast<UObject**>(static_cast<uint8_t*>(parms) + p->GetOffset_Internal());
            if (sp && isObjectAlive(sp))
            {
                spawnerArg = safeClassName(sp);
            }
            else
                spawnerArg = STR("<null>");
        }
    }
    catch (...)
    {
    }

    try
    {
        if (auto* p = findParam(func, STR("InSpawnContext")))
        {
            uint8_t v = *(static_cast<uint8_t*>(parms) + p->GetOffset_Internal());
            ctxVal = v;
        }
    }
    catch (...)
    {
    }

    // Decode context enum for readability.
    const wchar_t* ctxName = STR("?");
    switch (ctxVal)
    {
    case 0:
        ctxName = STR("None");
        break;
    case 1:
        ctxName = STR("AIPopulation");
        break;
    case 2:
        ctxName = STR("AIPatrol");
        break;
    case 3:
        ctxName = STR("AIChallenge");
        break;
    case 4:
        ctxName = STR("AILair");
        break;
    case 5:
        ctxName = STR("AIWaveEncounter");
        break;
    case 6:
        ctxName = STR("AISavedSingleSpawner");
        break;
    }

    VLOG(STR("[MoriaCppMod] [SpawnDiag] BP_RequestSpawn manager={} class='{}' spawner='{}' ctx={}({})\n"),
         spawnerCls.c_str(),
         chTypePath.c_str(),
         spawnerArg.c_str(),
         ctxName,
         ctxVal);
}

// One-shot dump: walk every component on a live AActor and print
// each component's class name + UFunction names that look related
// to inventory / storage / container access. This is how we'll
// find the goat's storage component (likely MorContainerComponent
// or MorInventoryComponent) and the open-UI UFunction we need to
// call when the player taps E on the goat.
bool m_goatComponentsDumped{false};

// [rc.66 HELPER 2026-05-21] Walk a UI_WBP_Interaction_C row widget's
// children to find the TextBlock named 'InteractionText' and return
// its current text. Same safe-read pattern as the [GoatHeader] DUMP
// logic at line ~3640 — uses the TextBlock's GetText UFunction whose
// return-value FText is then safely ToString'd.
//
// Scoped to a single row widget (not the whole menu), so we know
// exactly which row's label we're reading.
std::wstring readRowInteractionText(UObject* rowWidget)
{
    if (!rowWidget || !isObjectAlive(rowWidget)) return STR("");

    auto readText = [](UObject* w) -> std::wstring {
        if (!w || !isObjectAlive(w)) return STR("");
        auto* fn = w->GetFunctionByNameInChain(STR("GetText"));
        if (!fn) return STR("");
        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
        try
        {
            safeProcessEvent(w, fn, buf.data());
        }
        catch (...)
        {
            return STR("");
        }
        auto* pRet = findParam(fn, STR("ReturnValue"));
        if (!pRet) return STR("");
        FText* t = reinterpret_cast<FText*>(buf.data() + pRet->GetOffset_Internal());
        if (!t) return STR("");
        try
        {
            return std::wstring(t->ToString());
        }
        catch (...)
        {
            return STR("");
        }
    };

    // [rc.69 FIX 2026-05-21] rc.68 dump revealed the goods:
    //   .InteractionText : ObjectProperty @ +0x4d8  (TextBlock — null at PressInteract time)
    //   .DisplayText     : TextProperty @ +0x278    ← THIS is the raw FText label,
    //                                                  populated by SetInteraction
    //   .HoldDisplayText : TextProperty @ +0x290
    //
    // DisplayText lives on MorInteractionWidget (native parent) and holds the
    // current row label as a fully-constructed FText. Read it directly via
    // reflection and SEH-wrap the ToString call for safety.
    {
        auto* dt = rowWidget->GetValuePtrByPropertyNameInChain<RC::Unreal::FText>(STR("DisplayText"));
        if (dt)
        {
            std::wstring s = seh_ftextToString(dt);
            if (!s.empty())
            {
                VLOG(STR("[MoriaCppMod] [GoatMenu] DisplayText='{}'\n"), s.c_str());
                return s;
            }
        }
    }

    // Fallback: recursive descent (kept for robustness).
    std::wstring found;
    std::function<void(UObject*, int)> walk = [&](UObject* w, int depth) {
        if (!w || !isObjectAlive(w) || !found.empty() || depth > 8) return;
        std::wstring cls;
        try
        {
            cls = w->GetClassPrivate()->GetName();
        }
        catch (...)
        {
        }
        if (cls == STR("TextBlock"))
        {
            std::wstring wname;
            try
            {
                wname = std::wstring(w->GetNamePrivate().ToString());
            }
            catch (...)
            {
            }
            if (wname.find(STR("InteractionText")) != std::wstring::npos && wname.find(STR("HoldInteractionText")) == std::wstring::npos)
            {
                std::wstring t = readText(w);
                if (!t.empty())
                {
                    found = t;
                    return;
                }
            }
        }
        auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
        if (nestedWt && *nestedWt)
        {
            auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
            if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot, depth + 1);
        }
        if (!found.empty()) return;
        auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
        if (slots)
        {
            for (int i = 0; i < slots->Num(); ++i)
            {
                UObject* s = (*slots)[i];
                if (!s || !isObjectAlive(s)) continue;
                auto* c = s->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (c && *c) walk(*c, depth + 1);
                if (!found.empty()) return;
            }
        }
        auto* singleC = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
        if (singleC && *singleC) walk(*singleC, depth + 1);
    };
    walk(rowWidget, 0);
    return found;
}

// PE pre-hook: OnPressInteract fires on UI_WBP_Interaction_C when the
// player taps E with an interaction prompt visible. Per rc.65 testing,
// vanilla DOES NOT dispatch any UFunction on goat menu rows (even with
// 1.36s hold) — the C++ rescue/details handlers silently reject the
// goat. So we own all dispatch from this PE-pre hook.
void onInteractionPressPre(UObject* widgetCtx)
{
    if (!widgetCtx) return;
    if (m_followGoats.empty()) return;

    // [Phase 4 / Path B'] When the player presses E on a proximity
    // row belonging to our porter goat, defer-open our centered
    // modal. Vanilla's Details dispatch is a no-op for goats so we
    // don't need to suppress it explicitly. Filter: check the row's
    // Interactable.Owner against m_followGoats.
    //
    // We bail before falling into legacy chest-link / row-inject
    // dispatch code — those are dormant in Phase 4.

    auto* interactablePtr = widgetCtx->GetValuePtrByPropertyNameInChain<UObject*>(STR("Interactable"));
    UObject* interactable = (interactablePtr && isReadableMemory(interactablePtr, sizeof(UObject*))) ? *interactablePtr : nullptr;
    if (!interactable || !isObjectAlive(interactable)) return;

    UObject* outer = nullptr;
    try
    {
        outer = interactable->GetOuterPrivate();
    }
    catch (...)
    {
    }
    if (!outer || !isObjectAlive(outer)) return;

    bool isOurGoat = false;
    for (auto& g : m_followGoats)
    {
        UObject* mine = g.pawn.Get();
        if (mine && mine == outer)
        {
            isOurGoat = true;
            break;
        }
    }
    if (!isOurGoat) return;

    VLOG(STR("[MoriaCppMod] [GoatSubmenu] tap-E on porter goat detected — identifying clicked row\n"));

    // [rc.66 DISPATCH 2026-05-21] Walk the clicked row widget's children
    // to find the InteractionText TextBlock, safe-read its text, and
    // dispatch based on label. Vanilla never fires a UFunction dispatch
    // on goat menu rows (verified rc.65 — even a 1.36s hold produced no
    // ServerRescue/Details/Manage call), so we own all action. No need
    // for PreventOriginalFunctionCall.
    //
    // Labels are set by Tobi's PR #5 pak:
    //   "Saddlebags"     → Rescue slot   → open goat saddlebag UI (rc.67 stub)
    //   "Follow / Stay"  → Details slot  → toggle stayMode (real handler)
    std::wstring rowLabel = readRowInteractionText(widgetCtx);
    VLOG(STR("[MoriaCppMod] [GoatMenu] row label='{}' — dispatching\n"), rowLabel.empty() ? STR("?") : rowLabel.c_str());

    if (rowLabel.find(STR("Follow")) != std::wstring::npos || rowLabel.find(STR("follow")) != std::wstring::npos ||
        rowLabel.find(STR("Stay")) != std::wstring::npos || rowLabel.find(STR("stay")) != std::wstring::npos)
    {
        // [NATIVE-AI 2026-07-18] Stay/Follow are BACK, driven by the
        // goat's own AI via one-shot LeashActor writes (no tick refresh).
        bool anyStay = false;
        for (auto& rec : m_followGoats)
            if (rec.stayMode) anyStay = true;
        if (anyStay)
            onGoatFollow();
        else
            onGoatStay();
    }
    else if (rowLabel.find(STR("Saddlebag")) != std::wstring::npos || rowLabel.find(STR("saddlebag")) != std::wstring::npos ||
             rowLabel.find(STR("Equip")) != std::wstring::npos || rowLabel.find(STR("equip")) != std::wstring::npos)
    {
        // [rc.70 2026-05-21] Saddlebag access — diagnostic + best-effort.
        // [rc.105 2026-07-10] After the vanilla E-release rescue fires,
        // the goat menu's rescue-slot row is replaced by 'Equip' — route
        // that label to the saddlebags too so the storage stays reachable.
        VLOG(STR("[MoriaCppMod] [GoatMenu] SADDLEBAGS/EQUIP clicked — invoking openGoatSaddlebag\n"));
        openGoatSaddlebagInventory();
    }
    else
    {
        VLOG(STR("[MoriaCppMod] [GoatMenu] unrecognized row label '{}' — ignoring\n"), rowLabel.empty() ? STR("<empty>") : rowLabel.c_str());
    }

    // Note: m_goatSubMenuPending intentionally NOT set — submenu
    // injection is fully retired since rc.54.
    return;

    // [SUSPENDED 2026-05-09] Phase 4 chest-link: routed tap-E to
    // captured chest via ServerInteract — but ServerInteract is
    // NOT how chests open in vanilla (zero natural calls observed).
    // Restart from `chest-link-future.md` Path B: spawn
    // UMorInventoryScreen directly with chest's Inventory (off=0x418)
    // as StorageObject.
}

// ───── public entry: NUM- toggle handler ─────────────────────────

// Despawn all currently-tracked companion goats. Calls K2_DestroyActor
// on each live pawn and clears the record list.
void despawnAllFollowGoats()
{
    int destroyed = 0;
    for (auto& g : m_followGoats)
    {
        UObject* goat = g.pawn.Get();
        if (!goat || !isObjectAlive(goat)) continue;
        if (auto* dFn = goat->GetFunctionByNameInChain(STR("K2_DestroyActor")))
        {
            safeProcessEvent(goat, dFn, nullptr);
            ++destroyed;
        }
    }
    m_followGoats.clear();
    VLOG(STR("[MoriaCppMod] [Goat] despawned {} goat(s); herd cleared\n"), destroyed);
    if (destroyed > 0) showOnScreen(L"Goat dismissed", 2.0f, 0.4f, 0.9f, 0.4f);
}

// ───── per-tick follow ─────────────────────────────────────────

void tickFollowGoats()
{

    // Run pending deferred toast BEFORE the empty-herd early-return —
    // the toast must fire even if the player has despawned the goat
    // between rename and the deferred tick.
    if (m_pendingChestToastTicks > 0)
    {
        if (--m_pendingChestToastTicks == 0)
        {
            showOnScreen(L"Goat chest captured!", 3.0f, 0.4f, 0.9f, 0.4f);
        }
    }
    // Run pending deferred chest property-schema dump (one-shot,
    // gated by m_goatChestSchemaDumped). Re-checks isObjectAlive
    // because the chest may have unloaded between rename and now.
    if (m_pendingChestSchemaTicks > 0)
    {
        if (--m_pendingChestSchemaTicks == 0 && !m_goatChestSchemaDumped && m_goatChestActor && isObjectAlive(m_goatChestActor))
        {
            m_goatChestSchemaDumped = true;
            dumpChestPropertySchema(m_goatChestActor);
        }
    }
    if (m_followGoats.empty()) return;
    UObject* pawn = m_localPawn;
    if (!pawn || !isObjectAlive(pawn)) return;

    // Prune dead goats (world unload, manual destroy, etc.) — SEH-wrap
    // the weak get so a corrupted record drops instead of crashing.
    m_followGoats.erase(std::remove_if(m_followGoats.begin(),
                                       m_followGoats.end(),
                                       [](FollowGoatRecord& g) {
                                           UObject* p = g.pawn.Get();
                                           return !p || !isObjectAlive(p);
                                       }),
                        m_followGoats.end());

    ULONGLONG now = GetTickCount64();
    for (auto& g : m_followGoats)
    {
        UObject* goat = g.pawn.Get();
        if (!goat || !isObjectAlive(goat)) continue;

        // One-shot: dump goat components + actual location on first tick
        // after spawn. Confirms whether the SkeletalMeshComponent is
        // attached and reveals if the actor relocated unexpectedly.
        if (!g.componentsLogged)
        {
            g.componentsLogged = true;
            if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                struct
                {
                    FVec3f Ret{};
                } lp{};
                if (safeProcessEvent(goat, gloc, &lp))
                    VLOG(STR("[MoriaCppMod] [Goat] post-tick goat actual loc=({:.1f},{:.1f},{:.1f})\n"), lp.Ret.X, lp.Ret.Y, lp.Ret.Z);
            }
            // Walk the actor's component list via GetComponentsByTag (returns
            // all components if tag empty? not reliable). Use GetComponents
            // by walking property reflection on the actor for "BlueprintCreatedComponents".
            // Simpler: just walk all UProperty fields on the BP instance and
            // log any UObject* whose GetClass()->GetName() ends in "Component".
            int compHits = 0;
            for (auto* strct = static_cast<UStruct*>(goat->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
            {
                for (auto* prop : strct->ForEachProperty())
                {
                    if (compHits >= 200) break;
                    // Skip the per-bone capsule colliders — too noisy.
                    std::wstring pn = prop->GetName();
                    if (pn.find(L"Capsule") != std::wstring::npos) continue;
                    auto* objProp = CastField<FObjectProperty>(prop);
                    if (!objProp) continue;
                    UObject** vp = goat->GetValuePtrByPropertyNameInChain<UObject*>(prop->GetName().c_str());
                    if (!vp || !*vp) continue;
                    UObject* comp = *vp;
                    if (!isObjectAlive(comp)) continue;
                    std::wstring compClsName;
                    try
                    {
                        compClsName = safeClassName(comp);
                    }
                    catch (...)
                    {
                    }
                    if (compClsName.find(L"Component") == std::wstring::npos && compClsName.find(L"Mesh") == std::wstring::npos) continue;
                    VLOG(STR("[MoriaCppMod] [Goat] component: prop={} type={} ptr={:p}\n"), prop->GetName().c_str(), compClsName.c_str(), (void*)comp);
                    ++compHits;

                    // BP_NpcGoat ships with a holiday/decoration "Hat"
                    // StaticMeshComponent in its SCS. We don't want it on
                    // the companion goat — destroy it on first tick. The
                    // component name is "Hat" exactly (case-sensitive
                    // match on prop name, not class name; class is
                    // StaticMeshComponent which we'd otherwise want to
                    // keep for legitimate static-mesh accessories).
                    if (pn == std::wstring_view(STR("Hat")))
                    {
                        // User opted to keep Gandalf's hat (the BP_NpcGoat
                        // SCS default) — small Fellowship easter-egg the
                        // dev team baked in. No mesh override.
                        VLOG(STR("[MoriaCppMod] [Goat] Hat slot kept at default (Gandalfs_Hat)\n"));
                        continue;
                    }

                    // FGKTargetableComponent drives the "E to rescue"
                    // interaction prompt. Without a settlement-rescue
                    // context the prompt shows but the action is a no-op,
                    // which is just visual noise. Deactivate it.
                    if (compClsName.find(L"TargetableComponent") != std::wstring::npos)
                    {
                        if (auto* saFn = comp->GetFunctionByNameInChain(STR("SetActive")))
                        {
                            struct
                            {
                                bool bNewActive{false};
                                bool bReset{false};
                            } sap{};
                            safeProcessEvent(comp, saFn, &sap);
                            VLOG(STR("[MoriaCppMod] [Goat] deactivated {} (suppresses 'E to rescue')\n"), compClsName.c_str());
                        }
                    }

                    // If this is the SkeletalMeshComponent, force visibility
                    // AND apply the user-selected MI_Goat skin to every slot.
                    if (compClsName.find(L"SkeletalMesh") != std::wstring::npos)
                    {
                    }
                }
                if (compHits >= 200) break;
            }

            // Diagnostic phase 2: explicit lookups by class for the
            // components we definitely care about, in case the property
            // walk missed them (inherited from APawn/ACharacter, or
            // hidden behind access modifiers).
            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (getCompFn)
            {
                struct ProbeEntry
                {
                    const wchar_t* clsPath;
                    const wchar_t* logName;
                };
                ProbeEntry probes[] = {
                        {STR("/Script/Engine.SkeletalMeshComponent"), L"Mesh"},
                        {STR("/Script/Moria.ModularCharacterComponent"), L"ModularChar"},
                        {STR("/Script/Moria.MorEquipComponent"), L"EquipComp"},
                        {STR("/Script/Moria.MorInventoryComponent"), L"InventoryComp"},
                };
                for (auto& p : probes)
                {
                    auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, p.clsPath);
                    if (!cls) continue;
                    int sz = getCompFn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);
                    writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), cls);
                    if (!safeProcessEvent(goat, getCompFn, buf.data())) continue;
                    UObject* found = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                    if (!found || !isObjectAlive(found))
                    {
                        VLOG(STR("[MoriaCppMod] [Goat] probe[{}] = null\n"), p.logName);
                        continue;
                    }
                    VLOG(STR("[MoriaCppMod] [Goat] probe[{}] = {:p} ({})\n"), p.logName, (void*)found, safeClassName(found).c_str());

                    // For ModularCharacterComponent: dump its UPROPERTYs +
                    // its own AttachChildren. The visible "hat" almost
                    // certainly comes from one of its slot meshes.
                    if (std::wstring_view(p.logName) == std::wstring_view(L"ModularChar"))
                    {
                        int mccProps = 0;
                        for (auto* strct = static_cast<UStruct*>(found->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
                        {
                            for (auto* mProp : strct->ForEachProperty())
                            {
                                if (mccProps >= 60) break;
                                std::wstring mpn = mProp->GetName();
                                VLOG(STR("[MoriaCppMod] [Goat]   MCC.prop[{}] = {} (type={})\n"), mccProps, mpn.c_str(), mProp->GetClass().GetName().c_str());
                                ++mccProps;
                            }
                            if (mccProps >= 60) break;
                        }

                        // MCC inherits from UActorComponent, which is NOT a
                        // SceneComponent — so no AttachChildren to walk on
                        // the MCC itself. But its sub-meshes (if any) live
                        // as sibling components of the actor; we already
                        // have those in the property walk above.
                    }

                    // For the SkeletalMeshComponent, walk AttachChildren —
                    // bells/pack/hat may be USceneComponents attached via
                    // sockets, not separate UPROPERTYs.
                    if (std::wstring_view(p.logName) == std::wstring_view(L"Mesh"))
                    {

                        // Walk AttachChildren — TArray<USceneComponent*> on USceneComponent
                        auto** ac = found->GetValuePtrByPropertyNameInChain<void*>(STR("AttachChildren"));
                        if (ac)
                        {
                            // TArray layout: { void* data; int32 num; int32 max }
                            uint8_t* arr = reinterpret_cast<uint8_t*>(ac);
                            UObject** elems = *reinterpret_cast<UObject***>(arr + 0);
                            int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                            VLOG(STR("[MoriaCppMod] [Goat] Mesh.AttachChildren count={}\n"), num);
                            int safeNum = num > 50 ? 50 : (num < 0 ? 0 : num);
                            for (int i = 0; i < safeNum; ++i)
                            {
                                UObject* child = elems[i];
                                if (!child || !isObjectAlive(child)) continue;
                                std::wstring childCls = safeClassName(child);
                                std::wstring childName;
                                try
                                {
                                    childName = child->GetName();
                                }
                                catch (...)
                                {
                                }
                                VLOG(STR("[MoriaCppMod] [Goat]   attached[{}]: name={} type={} ptr={:p}\n"), i, childName.c_str(), childCls.c_str(), (void*)child);
                            }
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [Goat] Mesh.AttachChildren property not found\n"));
                        }
                    }
                }
            }
        }

        // Lazy controller resolve. Read APawn::Controller UPROPERTY
        // directly — more reliable than the K2_GetController UFUNCTION
        // (which can return null right after spawn even when Controller
        // is set). Walk the super chain so the inherited APawn::Controller
        // field is found.
        UObject* ctrl = g.controller.Get();
        if (!ctrl || !isObjectAlive(ctrl))
        {
            g.ctrlAttempts++;
            UObject** ctrlPtr = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            UObject* c = ctrlPtr ? *ctrlPtr : nullptr;
            if (c && isObjectAlive(c))
            {
                ctrl = c;
                g.controller = RC::Unreal::FWeakObjectPtr(ctrl);
                VLOG(STR("[MoriaCppMod] [Goat] controller resolved (attempt #{}, via UPROPERTY Controller): {:p}\n"), g.ctrlAttempts, (void*)ctrl);
            }
            else if ((now - g.lastCtrlAttemptLogMs) > 2000)
            {
                g.lastCtrlAttemptLogMs = now;
                VLOG(STR("[MoriaCppMod] [Goat] controller still null after {} attempts (UPROPERTY ptr={:p}, deref={:p})\n"), g.ctrlAttempts, (void*)ctrlPtr, (void*)c);
                // Self-heal: a NATIVELY-RESTORED goat can come back
                // UNPOSSESSED (no AIController) — without one the
                // follow drive never runs and the goat can't walk at
                // all. Our own spawn path calls SpawnDefaultController;
                // do the same here once resolution has clearly failed.
                if (g.ctrlAttempts >= 3)
                {
                    if (auto* sdc = goat->GetFunctionByNameInChain(STR("SpawnDefaultController")))
                    {
                        try
                        {
                            safeProcessEvent(goat, sdc, nullptr);
                        }
                        catch (...)
                        {
                        }
                        VLOG(STR("[MoriaCppMod] [Goat] SpawnDefaultController fired on unpossessed goat {:p}\n"), (void*)goat);
                    }
                }
            }
        }
        if (!ctrl || !isObjectAlive(ctrl)) continue;

        // [NATIVE-AI 2026-07-18 EXPERIMENT, user spec] Hand movement to the
        // goat's OWN AI with ONE-SHOT writes only — no per-tick refresh,
        // no FSM disable, no gait/movement forcing, no manual MoveToActor.
        // (Past "leash key never consumed" findings predate registration —
        // re-testing with zero assumptions.) Follow = LeashActor set once;
        // Stay = LeashActor cleared once (onGoatStay/onGoatFollow).
        static constexpr bool kNativeAI = true;
        if (kNativeAI)
        {
            ++g.ticksSinceSpawn;
            if (!g.brainStopped) // reused as the native-init one-shot flag
            {
                g.brainStopped = true;
                setRoleFuzzyOnGoat(goat, STR("Porter"));
                // [ROLE-FIX 2026-07-22] assert Porter via the NATIVE RPC every
                // session. The raw roster write is invisible to replication;
                // any entry refresh (settlement revalidation, HandleRoleUpdate,
                // MP resync) flipped the display to Default = "Citizen" and
                // killed the role-dispatched follow.
                {
                    uint8_t rg[16] = {0};
                    if (findRudhMarkerGuidRaw(rg)) callGoatSetPorterRole(rg);
                }
                if (!g.stayMode) setGoatLeashActor(ctrl, pawn);
                if (!g.stayMode)
                    goatReplaceBehaviorState(ctrl, STR("/Game/Character/NpcGoat/Bst_NPCGoatWorkPorter.Bst_NPCGoatWorkPorter_C"), STR("Porter/init"));
                VLOG(STR("[MoriaCppMod] [NativeAI] one-shot init: role=Porter, LeashActor {} (goat={:p} ctrl={:p})\n"),
                     g.stayMode ? STR("left clear (stay)") : STR("SET to player"), (void*)goat, (void*)ctrl);
                logGoatFSMState(ctrl, STR("native-init"));
            }
            // diagnostic ONLY (no control writes): FSM state every ~30s so
            // we can see whether the root ever enters WorkTime/Porter.
            if ((g.ticksSinceSpawn % 1800) == 0) logGoatFSMState(ctrl, STR("native-30s"));
            continue; // native AI owns the goat from here
        }

        // ORDER MATTERS: deactivate FGK components FIRST, before any
        // role assignment / equip dispatch. Otherwise the equip's
        // role-changed delegate fires while the FGK FSM is still
        // alive, which triggers a porter-state side effect (goat
        // tries to walk to a settlement waypoint to deliver the pack).
        if (!g.fleeSuppressed)
        {
            g.fleeSuppressed = true;
        }

        // Now assign the Porter role + equip the pack. With FGK dead,
        // the role-changed delegate has no behavior tree to trigger
        // a porter-state side effect.
        if (!g.porterRoleAssigned)
        {
            g.porterRoleAssigned = true;
            // Per Option 2: keep the rescue interaction ENABLED so the
            // E-to-Rescue prompt shows up. We hook the rescue UFunction
            // call elsewhere (ServerRescueNpc PE pre-hook) to intercept
            // E presses on our goats and run our own action instead.
            g.postEquipDumpAttempts = 999;
        }

        // Post-equip diagnostic. Crash on the previous build was at
        // g.pawn.Get() (now SEH-wrapped via safeGoatWeakGet above), not
        // inside this block. So the diagnostic itself stays as-is.
        if (g.porterRoleAssigned && g.postEquipDumpAttempts < 3)
        {
            g.postEquipDumpAttempts++;
            if (g.postEquipDumpAttempts == 3) // 3rd tick after equip
            {
                VLOG(STR("[MoriaCppMod] [Goat] === POST-EQUIP COMPONENT DUMP (looking for pack) ===\n"));
                int packLikeFound = 0;
                for (auto* strct = static_cast<UStruct*>(goat->GetClassPrivate()); strct; strct = strct->GetSuperStruct())
                {
                    for (auto* prop : strct->ForEachProperty())
                    {
                        auto* objProp = CastField<FObjectProperty>(prop);
                        if (!objProp) continue;
                        UObject** vp = goat->GetValuePtrByPropertyNameInChain<UObject*>(prop->GetName().c_str());
                        if (!vp || !*vp) continue;
                        UObject* c = *vp;
                        if (!isObjectAlive(c)) continue;
                        std::wstring cn = safeClassName(c);
                        std::wstring pn2 = prop->GetName();
                        if (pn2.find(L"Capsule") != std::wstring::npos) continue;
                        // Log everything that looks pack-related or equipment-related
                        if (cn.find(L"SkeletalMesh") != std::wstring::npos || pn2.find(L"Pack") != std::wstring::npos ||
                            pn2.find(L"Dummy") != std::wstring::npos || pn2.find(L"Equipped") != std::wstring::npos)
                        {
                            VLOG(STR("[MoriaCppMod] [Goat]   POST-EQUIP found: prop={} type={} ptr={:p}\n"), pn2.c_str(), cn.c_str(), (void*)c);
                            ++packLikeFound;
                        }
                    }
                }

                // Also walk the SkeletalMeshComponent's AttachChildren for
                // any newly-added pack child component.
                auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (getCompFn)
                {
                    auto* skmCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.SkeletalMeshComponent"));
                    if (skmCls)
                    {
                        int gsz = getCompFn->GetParmsSize();
                        std::vector<uint8_t> gbuf(gsz, 0);
                        writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), skmCls);
                        if (safeProcessEvent(goat, getCompFn, gbuf.data()))
                        {
                            UObject* skm = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                            if (skm && isObjectAlive(skm))
                            {
                                auto** ac = skm->GetValuePtrByPropertyNameInChain<void*>(STR("AttachChildren"));
                                if (ac)
                                {
                                    uint8_t* arr = reinterpret_cast<uint8_t*>(ac);
                                    UObject** elems = *reinterpret_cast<UObject***>(arr + 0);
                                    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                                    VLOG(STR("[MoriaCppMod] [Goat]   POST-EQUIP Mesh.AttachChildren count={}\n"), num);
                                    int safeNum = num > 50 ? 50 : (num < 0 ? 0 : num);
                                    for (int i = 0; i < safeNum; ++i)
                                    {
                                        UObject* child = elems[i];
                                        if (!child || !isObjectAlive(child)) continue;
                                        std::wstring cls2 = safeClassName(child);
                                        std::wstring n2;
                                        try
                                        {
                                            n2 = child->GetName();
                                        }
                                        catch (...)
                                        {
                                        }
                                        // Filter to non-capsule, non-audio for noise reduction
                                        if (cls2.find(L"AkComponent") != std::wstring::npos) continue;
                                        if (n2.find(L"Capsule") != std::wstring::npos) continue;
                                        if (cls2.find(L"FGKTargetable") != std::wstring::npos) continue;
                                        VLOG(STR("[MoriaCppMod] [Goat]     attached non-noise[{}]: name={} type={} ptr={:p}\n"),
                                             i,
                                             n2.c_str(),
                                             cls2.c_str(),
                                             (void*)child);
                                    }
                                }
                            }
                        }
                    }
                }
                VLOG(STR("[MoriaCppMod] [Goat] === END POST-EQUIP DUMP (pack-like hits: {}) ===\n"), packLikeFound);
            }
        }

        // (FGK component deactivation moved earlier — runs BEFORE
        // role assignment + equip so the delegate side-effects don't
        // trigger porter-state walk.)

        if (false && !g.controllerReplaced)
        {
            g.controllerReplaced = true;
            UObject* newCtrl = replaceGoatControllerWithVanilla(goat, ctrl);
            if (newCtrl && isObjectAlive(newCtrl))
            {
                ctrl = newCtrl;
                g.controller = RC::Unreal::FWeakObjectPtr(ctrl);
                g.fleeSuppressed = true; // brain replaced; nothing to suppress
            }
            else
            {
                // Fallback: replacement failed — fall back to deactivating
                // FGK components on the existing controller. Less reliable
                // (the FSM may still own the pawn) but better than nothing.
                VLOG(STR("[MoriaCppMod] [Goat] controller replacement failed; falling back to FGK component deactivation\n"));
                deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKActorFSMComponent"), L"FGKActorFSMComponent");
                deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAIPerceptionComponent"), L"FGKAIPerceptionComponent");
                deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAITargetingComponent"), L"FGKAITargetingComponent");
                g.fleeSuppressed = true;
            }
        }

        // v1.1.0 diagnostic: post-registration component dump.
        // Fire ~5s after spawn (300 ticks at ~60Hz) so registration
        // has fully settled and any new AI components have shown up.
        // Logs every component the goat (pawn AND controller) has,
        // so we can find whatever new wander/idle source crept in
        // post-Porter-registration.
        ++g.ticksSinceSpawn;
        if (!g.postRegDumpDone && g.ticksSinceSpawn >= 300)
        {
            g.postRegDumpDone = true;
            // Also log the controller's class name to detect if
            // registration swapped to a Porter-specific AIController.
            std::wstring ctrlCls;
            try
            {
                ctrlCls = ctrl->GetClassPrivate()->GetName();
            }
            catch (...)
            {
            }
            VLOG(STR("[MoriaCppMod] [Goat] post-reg controller class={}\n"), ctrlCls.c_str());
        }

        // Throttle MoveToActor to 1 Hz per goat (well under PE budget).
        if ((now - g.lastMoveTickMs) < 1000) continue;
        g.lastMoveTickMs = now;

        // FOLLOW-ALWAYS: ~1s after spawn/adopt (post-possession, so
        // controller + FSM exist) put the goat in full follow state.
        // See memory goat-final-architecture for why each piece.
        if (!g.brainStopped && g.ticksSinceSpawn > 60)
        {
            g.brainStopped = true;
            g.lastBrainStopMs = now;
            stopGoatBrainLogic(goat, STR("MoriaCppMod spawn"));
            setGoatGaitRunning(goat);
            setRoleFuzzyOnGoat(goat, STR("Porter"));
            removeGoatFollowStayRow(goat);
            // "NpcId Invalid" log-spam mitigation: the UNREGISTERED
            // goat's MorNPCComponent polls the NPC manager each tick
            // (IsNpcInteracting / GetNpcSchedule) with an id the
            // manager doesn't know. Stop its tick — the E-menu rows
            // are event-driven and keep working. Revert if not.
            {
                UClass* npcCls2 = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
                if (npcCls2)
                    if (auto* gc2 = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                    {
                        std::vector<uint8_t> b2(gc2->GetParmsSize(), 0);
                        writeGoatParm<UClass*>(gc2, b2.data(), STR("ComponentClass"), npcCls2);
                        if (safeProcessEvent(goat, gc2, b2.data()))
                        {
                            UObject* nc = readGoatParm<UObject*>(gc2, b2.data(), STR("ReturnValue"), nullptr);
                            if (nc && isObjectAlive(nc))
                                if (auto* tf = nc->GetFunctionByNameInChain(STR("SetComponentTickEnabled")))
                                {
                                    std::vector<uint8_t> tb2(tf->GetParmsSize(), 0);
                                    tb2[0] = 0;
                                    try
                                    {
                                        safeProcessEvent(nc, tf, tb2.data());
                                    }
                                    catch (...)
                                    {
                                    }
                                    VLOG(STR("[MoriaCppMod] [GoatBrain] MorNPCComponent tick disabled (NpcId-Invalid spam mitigation)\n"));
                                }
                        }
                    }
            }
            // Movement mode can be stuck on MOVE_None from a prior
            // bell dismiss (DisableMovement) — force Walking.
            UClass* mvCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.CharacterMovementComponent"));
            if (mvCls)
                if (auto* gc = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> b(gc->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), mvCls);
                    if (safeProcessEvent(goat, gc, b.data()))
                    {
                        UObject* mv = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
                        if (mv && isObjectAlive(mv))
                            if (auto* sm = mv->GetFunctionByNameInChain(STR("SetMovementMode")))
                            {
                                std::vector<uint8_t> mb(sm->GetParmsSize(), 0);
                                mb[0] = 1; // MOVE_Walking
                                try
                                {
                                    safeProcessEvent(mv, sm, mb.data());
                                }
                                catch (...)
                                {
                                }
                            }
                    }
                }
        }
        else if (g.brainStopped && now - g.lastBrainStopMs >= 10000)
        {
            g.lastBrainStopMs = now;
            stopGoatBrainLogic(goat, STR("MoriaCppMod watchdog"), /*onlyIfActive=*/true);
        }

        // v1.1.0: per-second, write LeashActor blackboard key on
        // the AIController so Bst_NPCGoatWorkPorter_C's FollowPlayer
        // task has a target. EQS_Npc_PorterLeashPlayer doesn't pick
        // the player by default, so we set this directly. Re-set
        // every second in case the EQS clears it.
        //
        // [rc.39 STAY FIX 2026-06-10] Gate by !stayMode. Re-arming
        // LeashActor every tick was defeating Stay — the BT's
        // FollowPlayer task would re-issue MoveTo on its next tick
        // after our per-tick StopMovement landed. Pre-rc.46 the
        // manual MoveToActor was disabled for bell goats so the
        // leash refresh didn't matter (BT wasn't running porter
        // follow). Now that manual MoveToActor is back, the leash
        // refresh ALSO has to be gated. See DC report 2026-06-09.
        // [DIAG 2026-07-03] Confirm the tick reaches the leash-assert
        // and what stayMode is. Throttled inside setGoatLeashActor; here
        // we log the reach + stayMode once per 5s window.
        {
            static ULONGLONG s_lastTickLeashDiag = 0;
            ULONGLONG tnow = GetTickCount64();
            if (tnow - s_lastTickLeashDiag > 5000)
            {
                s_lastTickLeashDiag = tnow;
                VLOG(STR("[MoriaCppMod] [Leash] tick reached assert: goat={:p} ctrl={:p} stayMode={} bellSpawned={}\n"),
                     (void*)goat,
                     (void*)ctrl,
                     g.stayMode,
                     g.bellSpawned);
            }
        }
        if (!g.stayMode) setGoatLeashActor(ctrl, pawn);

        // v1.1.0 FIXUP: one-shot re-enable of SetIsInteractive(true)
        // on existing goats. Goats spawned by an earlier build had
        // interaction OFF (we suppressed the "E to rescue" prompt),
        // which also blocks inventory inspection. Without despawning
        // them (which would lose save state), force interaction back
        // on.
        if (!g.interactiveRefired)
        {
            g.interactiveRefired = true;
            auto* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (npcCompCls)
            {
                auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (getCompFn)
                {
                    int gsz = getCompFn->GetParmsSize();
                    std::vector<uint8_t> gbuf(gsz, 0);
                    writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), npcCompCls);
                    if (safeProcessEvent(goat, getCompFn, gbuf.data()))
                    {
                        UObject* npcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                        if (npcComp && isObjectAlive(npcComp))
                        {
                            if (auto* siFn = npcComp->GetFunctionByNameInChain(STR("SetIsInteractive")))
                            {
                                struct
                                {
                                    bool bValue{true};
                                } sip{};
                                safeProcessEvent(npcComp, siFn, &sip);
                                VLOG(STR("[MoriaCppMod] [Goat] FIXUP: SetIsInteractive(true) re-fired on existing goat={:p}\n"), (void*)goat);
                            }
                        }
                    }
                }
            }
        }

        // Slow-cadence FSM state diagnostic — log every ~5s so we
        // can see whether the FSM is in "WorkTime" (Porter follow
        // active) or stuck in "Unaware" (idle wander).
        if ((g.ticksSinceSpawn % 300) == 0) logGoatFSMState(ctrl, STR("tick"));

        // [rc.71 RESTORE DRIVE 2026-07-03] The rc.61 "Goat AI alone"
        // experiment is now DISPROVEN by runtime logging: the [Leash]
        // diagnostic confirms we set LeashActor=player on
        // BP_NpcGoat_AIController_C every second (PE_ok=true), yet
        // Tobi's v1.10.0 Bst_NPCGoatWorkPorter never consumes that key
        // — the goat does not move. So restore the manual drive that
        // worked pre-rc.61 (rc.46-rc.60): on follow issue
        // AAIController::MoveToActor(player, radius=250) at 1 Hz; on
        // stay issue StopMovement. This drives the goat directly and
        // is independent of whatever Tobi's behavior tree does.
        // Motion diagnostics: 1 Hz position sample; logs distance +
        // speed every ~3s (or instantly on >1200 u/s = teleport).
        {
            float gLoc[3] = {0, 0, 0}, pLoc[3] = {0, 0, 0};
            bool haveG = false, haveP = false;
            if (auto* getLoc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                std::vector<uint8_t> gbuf(getLoc->GetParmsSize(), 0);
                if (safeProcessEvent(goat, getLoc, gbuf.data()))
                    if (auto* pRet = findParam(getLoc, STR("ReturnValue")))
                    {
                        float* fv = reinterpret_cast<float*>(gbuf.data() + pRet->GetOffset_Internal());
                        gLoc[0] = fv[0];
                        gLoc[1] = fv[1];
                        gLoc[2] = fv[2];
                        haveG = true;
                    }
            }
            if (pawn)
                if (auto* getLoc = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
                {
                    std::vector<uint8_t> gbuf(getLoc->GetParmsSize(), 0);
                    if (safeProcessEvent(pawn, getLoc, gbuf.data()))
                        if (auto* pRet = findParam(getLoc, STR("ReturnValue")))
                        {
                            float* fv = reinterpret_cast<float*>(gbuf.data() + pRet->GetOffset_Internal());
                            pLoc[0] = fv[0];
                            pLoc[1] = fv[1];
                            pLoc[2] = fv[2];
                            haveP = true;
                        }
                }
            if (haveG)
            {
                ULONGLONG dnow = GetTickCount64();
                if (g.lastDiagMs != 0)
                {
                    float dt = (dnow - g.lastDiagMs) / 1000.0f;
                    if (dt > 0.25f)
                    {
                        float dx = gLoc[0] - g.lastDiagPos[0], dy = gLoc[1] - g.lastDiagPos[1], dz = gLoc[2] - g.lastDiagPos[2];
                        float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
                        float speed = moved / dt;
                        float dist = -1.0f;
                        if (haveP)
                        {
                            float ex = gLoc[0] - pLoc[0], ey = gLoc[1] - pLoc[1], ez = gLoc[2] - pLoc[2];
                            dist = std::sqrt(ex * ex + ey * ey + ez * ez);
                        }
                        static ULONGLONG s_lastMotionLog = 0;
                        if (speed > 1200.0f || (dnow - s_lastMotionLog) > 3000)
                        {
                            s_lastMotionLog = dnow;
                            VLOG(STR("[MoriaCppMod] [GoatDiag] distToPlayer={:.0f} speed={:.0f}/s (moved {:.0f} in {:.2f}s) stay={}\n"), dist, speed, moved, dt, g.stayMode);
                        }
                    }
                }
                g.lastDiagPos[0] = gLoc[0];
                g.lastDiagPos[1] = gLoc[1];
                g.lastDiagPos[2] = gLoc[2];
                g.lastDiagMs = dnow;
            }
            if (!g.maxSpeedLogged)
            {
                g.maxSpeedLogged = true;
                UClass* mvCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.CharacterMovementComponent"));
                if (mvCls)
                    if (auto* gc = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                    {
                        std::vector<uint8_t> b(gc->GetParmsSize(), 0);
                        writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), mvCls);
                        if (safeProcessEvent(goat, gc, b.data()))
                        {
                            UObject* mv = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
                            if (mv && isObjectAlive(mv))
                                if (auto* spd = mv->GetValuePtrByPropertyNameInChain<float>(STR("MaxWalkSpeed")))
                                    VLOG(STR("[MoriaCppMod] [GoatDiag] MaxWalkSpeed={:.0f}\n"), *spd);
                        }
                    }
            }
        }

        // THE mover: the goat never self-walks (proven on v1.10.0
        // AND v1.12.0 — the porter BT ignores LeashActor). Do NOT
        // disable this drive; the native escort teleport is only a
        // far-distance backstop. See memory goat-final-architecture.
        static constexpr bool kManualMoveDrive = true;
        if (kManualMoveDrive && !g.stayMode)
        {
            if (auto* moveFn = ctrl->GetFunctionByNameInChain(STR("MoveToActor")))
            {
                int msz = moveFn->GetParmsSize();
                std::vector<uint8_t> mbuf(msz, 0);
                if (auto* pGoal = findParam(moveFn, STR("Goal"))) *reinterpret_cast<UObject**>(mbuf.data() + pGoal->GetOffset_Internal()) = pawn;
                if (auto* pRad = findParam(moveFn, STR("AcceptanceRadius"))) *reinterpret_cast<float*>(mbuf.data() + pRad->GetOffset_Internal()) = 250.0f;
                if (auto* pPath = findParam(moveFn, STR("bUsePathfinding"))) *reinterpret_cast<bool*>(mbuf.data() + pPath->GetOffset_Internal()) = true;
                try
                {
                    safeProcessEvent(ctrl, moveFn, mbuf.data());
                }
                catch (...)
                {
                }
                if (g.moveToActorLogsRemaining > 0)
                {
                    g.moveToActorLogsRemaining--;
                    VLOG(STR("[MoriaCppMod] [GoatDrive] MoveToActor(player,r=250) issued on ctrl={:p} goat={:p}\n"), (void*)ctrl, (void*)goat);
                }
            }
        }
        else
        {
            // Stay: actively halt each second so the goat's own idle/
            // wander AI can't drift it away from where the player left it.
            if (auto* stopFn = ctrl->GetFunctionByNameInChain(STR("StopMovement")))
            {
                try
                {
                    safeProcessEvent(ctrl, stopFn, nullptr);
                }
                catch (...)
                {
                }
            }
        }
    }
}
