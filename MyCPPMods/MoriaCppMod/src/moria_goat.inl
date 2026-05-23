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
        UClass*    m_goatBPClass{nullptr};
        UObject*   m_kismetGameplayStaticsCDO{nullptr};
        UFunction* m_goatBeginSpawnFn{nullptr};
        UFunction* m_goatFinishSpawnFn{nullptr};
        // The skin material the user wants applied to every spawned goat.
        // Resolved lazily; persists for the session once loaded.
        UObject*   m_goatSkinMaterial{nullptr};
        // Adventurer's-pack item BP class (BP_EpicPack_AdventurersPack_Large_C).
        // Preloaded at character-load so the spawn-tick equip call has zero
        // blocking I/O on the game thread.
        UClass*    m_packItemClass{nullptr};

        // [v7.2.0-rc.3 2026-05-22] Tobi's porter-goat saddlebag class.
        // v1.5.0 ships with the slot-wrapper BP_ContainerItem_Goat_Slot_EpicPack_C
        // populated in the goat's InvComp but no actual saddlebag in the slot
        // (Tobi's BeginPlay graph doesn't fill it). When [GoatExperimental]
        // AutoEquipSaddleBag = true, we equip BP_PorterGoatSaddlebags_C onto
        // the goat's MorEquipComponent post-spawn so the slot actually holds
        // a real bag and clicking Saddlebags opens the storage grid.
        UClass*    m_saddlebagItemClass{nullptr};
        bool       m_autoEquipSaddleBag{true};   // INI-toggleable; default ON for rc.3 testing

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
        UClass*    m_goatSaddlebagWidgetCls{nullptr};
        UObject*   m_goatSaddlebagWidget{nullptr};
        bool       m_enableGoatSaddleUI{false};   // INI: [GoatExperimental] EnableGoatSaddleUI = true
        bool       m_goatSaddleWrapperDumped{false};  // rc.6 one-shot wrapper diagnostic

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
        // 12e: persistence via sidecar.
        UClass*    m_phantomChestClass{nullptr};
        RC::Unreal::FWeakObjectPtr m_phantomChest;
        bool       m_enablePhantomChest{false};  // INI: [GoatExperimental] PhantomChest = true

        // [v7.2.0-rc.12b 2026-05-23] Saddlebag-as-world-actor experiment.
        // Spawn BP_SaddleBags_Goat_C at goat's feet — bag is itself an
        // AInventoryItem subclass (so an AActor). Bag exposes its own
        // open/use API (same one right-clicking it in player inventory
        // triggers). After spawn, probe + auto-call its Open* / Use* /
        // Interact* UFunctions and see if any surface the proper "Goat
        // Saddlebags" UI.
        RC::Unreal::FWeakObjectPtr m_saddlebagWorldActor;
        bool       m_enableSaddlebagAtGoat{false};  // INI: [GoatExperimental] SaddlebagAtGoat = true
        // Static mesh of the dwarven mountaineer pack (Dwarf_Pack01_Static).
        // We swap this into the goat's existing Hat StaticMeshComponent slot
        // (instead of clearing it to null) so a visible pack appears without
        // needing to spawn new components at runtime.
        UObject*   m_packStaticMesh{nullptr};

        // Cached vanilla AAIController class (for replacing the BP_Goat_AIController_C).
        UClass* m_vanillaAIControllerClass{nullptr};

        struct FollowGoatRecord
        {
            RC::Unreal::FWeakObjectPtr pawn;
            RC::Unreal::FWeakObjectPtr controller;       // current possessing controller
            ULONGLONG lastMoveTickMs{0};
            ULONGLONG lastCtrlAttemptLogMs{0};
            int       ctrlAttempts{0};
            bool fleeSuppressed{false};
            bool componentsLogged{false};
            bool controllerReplaced{false};               // vanilla controller swap done
            bool porterRoleAssigned{false};               // SetRoleFuzzy("Porter") fired
            int  postEquipDumpAttempts{0};                // 3-tick delay before post-equip diag fires
            // v1.1.0 diagnostics: count ticks since spawn so we can fire a
            // post-registration component dump (looking for whatever new
            // AI / wander source registration added) and log MoveToActor
            // dispatches for the first N seconds.
            int  ticksSinceSpawn{0};
            bool postRegDumpDone{false};
            int  moveToActorLogsRemaining{8};             // log first 8 MoveToActors then go quiet
            bool interactiveRefired{false};               // v1.1.0 fixup: re-enable interaction on existing goats
            bool bellSpawned{false};                      // [rc.46] bell-summoned (skip porter role, run MoveToActor tick)
            bool stayMode{false};                         // [rc.52] goat-menu Stay button: skip MoveToActor
        };
        std::vector<FollowGoatRecord> m_followGoats;

        // Companion goat is single-instance only. NUM- is now a toggle:
        // press to spawn if none exists, press to despawn if one does.
        static constexpr size_t MAX_FOLLOW_GOATS = 1;

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
            VLOG(STR("[MoriaCppMod] [Goat] dump: {} BlueprintGeneratedClass instances resident\n"),
                 classes.size());
            int hits = 0;
            for (auto* c : classes)
            {
                if (!c || !isObjectAlive(c)) continue;
                std::wstring name = c->GetName();
                if (name.find(L"Goat") == std::wstring::npos) continue;
                std::wstring path = c->GetPathName();
                VLOG(STR("[MoriaCppMod] [Goat] resident class: {} (path={})\n"), name.c_str(), path.c_str());
                if (++hits >= 20) { VLOG(STR("[MoriaCppMod] [Goat] (cut at 20)\n")); break; }
            }
            if (hits == 0)
                VLOG(STR("[MoriaCppMod] [Goat] no *Goat* in {} BlueprintGeneratedClass results\n"), classes.size());
        }

        // Generic blocking soft-asset load via KismetSystemLibrary. fnSubpath
        // is "LoadAsset_Blocking" or "LoadClassAsset_Blocking"; parmName is
        // "Asset" or "AssetClass". Returns the UFUNCTION's UObject* return
        // (caller casts to UClass* if appropriate). Logs FName + offset
        // diagnostics so a null return reveals which step failed.
        UObject* goat_callBlockingLoader(const wchar_t* fnPath, const wchar_t* cdoPath,
                                         const wchar_t* parmName, const wchar_t* assetPath)
        {
            try
            {
                auto* fn  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, fnPath);
                auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, cdoPath);
                if (!fn || !cdo)
                {
                    VLOG(STR("[MoriaCppMod] [Goat] loader '{}' unresolved (fn={} cdo={})\n"),
                         fnPath, (void*)fn, (void*)cdo);
                    return nullptr;
                }
                auto* pAsset = findParam(fn, parmName);
                auto* pRet   = findParam(fn, STR("ReturnValue"));
                if (!pAsset || !pRet)
                {
                    VLOG(STR("[MoriaCppMod] [Goat] loader '{}' missing parms (asset={} ret={})\n"),
                         fnPath, (void*)pAsset, (void*)pRet);
                    return nullptr;
                }

                int sz = fn->GetParmsSize();
                std::vector<uint8_t> buf(sz, 0);

                RC::Unreal::FName name(assetPath, RC::Unreal::FNAME_Add);
                uint32_t ci  = name.GetComparisonIndex();
                uint32_t num = name.GetNumber();

                int aOff = pAsset->GetOffset_Internal();
                int rOff = pRet->GetOffset_Internal();
                VLOG(STR("[MoriaCppMod] [Goat] loader='{}' assetPath='{}' ci={} num={} parmSize={} aOff={} rOff={}\n"),
                     fnPath, assetPath, ci, num, sz, aOff, rOff);
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
                std::memcpy(tspc + 16, &ci,  4);
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
            UObject* r = goat_callBlockingLoader(
                STR("/Script/Engine.KismetSystemLibrary:LoadClassAsset_Blocking"),
                STR("/Script/Engine.Default__KismetSystemLibrary"),
                STR("AssetClass"), classPath);
            if (r) return reinterpret_cast<UClass*>(r);

            // 2) Fallback: LoadAsset_Blocking — will load the package and may
            // resolve the class as a UObject*. Cooked-build asset registries
            // sometimes prefer this path when the class hasn't been touched
            // by the runtime asset index yet.
            VLOG(STR("[MoriaCppMod] [Goat] LoadClassAsset_Blocking returned null; trying LoadAsset_Blocking\n"));
            r = goat_callBlockingLoader(
                STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                STR("/Script/Engine.Default__KismetSystemLibrary"),
                STR("Asset"), classPath);
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
            if (m_goatSkinMaterial && isObjectAlive(m_goatSkinMaterial))
                return m_goatSkinMaterial;
            const wchar_t* matPath = STR("/Game/CharacterArt/Creatures/Goat/Materials/MI_Goat.MI_Goat");
            m_goatSkinMaterial = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, matPath);
            if (!m_goatSkinMaterial)
            {
                m_goatSkinMaterial = goat_callBlockingLoader(
                    STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                    STR("/Script/Engine.Default__KismetSystemLibrary"),
                    STR("Asset"), matPath);
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
                            std::wstring nm; try { nm = cur->GetName(); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [Goat] mesh slot {} was: {}\n"), slot, nm.c_str());
                        }
                        else continue;  // empty slot — skip override
                    }
                }
                std::vector<uint8_t> sbuf(setSz, 0);
                writeGoatParm<int32_t>(setMatFn, sbuf.data(), STR("ElementIndex"), slot);
                writeGoatParm<UObject*>(setMatFn, sbuf.data(), STR("Material"),    mi);
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
        static constexpr const wchar_t* GOAT_CLASS_PATHS[] = {
            STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"),
            STR("/Game/Character/Creatures/Goat/BP_Fauna_Goat.BP_Fauna_Goat_C"),
        };
        static constexpr const wchar_t* GOAT_CLASS_NAMES[] = {
            STR("BP_NpcGoat_C"),
            STR("BP_Fauna_Goat_C"),
        };

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
                auto* getFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                    STR("/Script/AssetRegistry.AssetRegistryHelpers:GetAssetRegistry"));
                auto* helpersCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                    STR("/Script/AssetRegistry.Default__AssetRegistryHelpers"));
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
                    if (seh_findAllOf(nm, &regs) && !regs.empty()) { reg = regs[0]; break; }
                }
            }
            if (!reg)
            {
                VLOG(STR("[MoriaCppMod] [Goat] AssetRegistry singleton not found; skip path scan {}\n"), path);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Goat] AssetRegistry instance: {:p} class={}\n"),
                 (void*)reg, safeClassName(reg).c_str());

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
            int32_t pathLen = static_cast<int32_t>(wcslen(path)) + 1;  // include null term
            void* pathBuf   = FMemory::Malloc(pathLen * sizeof(wchar_t), 8);
            void* fstrSlots = FMemory::Malloc(16, 8);  // one FString
            if (!pathBuf || !fstrSlots)
            {
                if (pathBuf)   FMemory::Free(pathBuf);
                if (fstrSlots) FMemory::Free(fstrSlots);
                return;
            }
            wmemcpy(static_cast<wchar_t*>(pathBuf), path, pathLen);

            uint8_t* slot = static_cast<uint8_t*>(fstrSlots);
            *reinterpret_cast<void**>   (slot + 0)  = pathBuf;
            *reinterpret_cast<int32_t*> (slot + 8)  = pathLen;
            *reinterpret_cast<int32_t*> (slot + 12) = pathLen;

            // 3) Pack params buffer.
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            uint8_t* arr = buf.data() + pInPaths->GetOffset_Internal();
            *reinterpret_cast<void**>  (arr + 0)  = fstrSlots;
            *reinterpret_cast<int32_t*>(arr + 8)  = 1;
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

            UObject* anchorPre  = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, anchorPath);
            UObject* targetPre  = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, targetPath);
            VLOG(STR("[MoriaCppMod] [Goat] anchor diag pre-load: anchor={:p} target={:p}\n"),
                 (void*)anchorPre, (void*)targetPre);

            // Try to load the anchor. LoadClassAsset_Blocking is correct for a
            // TSubclassOf<>-typed default — but the anchor itself is a UObject
            // BP, so LoadAsset_Blocking is the canonical loader.
            UObject* anchor = goat_callBlockingLoader(
                STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                STR("/Script/Engine.Default__KismetSystemLibrary"),
                STR("Asset"), anchorPath);
            if (!anchor)
            {
                // Fallback: try the class-typed loader in case the anchor's
                // CDO is what gets registered as a UClass.
                anchor = goat_callBlockingLoader(
                    STR("/Script/Engine.KismetSystemLibrary:LoadClassAsset_Blocking"),
                    STR("/Script/Engine.Default__KismetSystemLibrary"),
                    STR("AssetClass"), anchorPath);
            }

            UObject* anchorPost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, anchorPath);
            UObject* targetPost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, targetPath);
            VLOG(STR("[MoriaCppMod] [Goat] anchor diag post-load: anchor={:p} target={:p} (loader returned {:p})\n"),
                 (void*)anchorPost, (void*)targetPost, (void*)anchor);

            // Editor-Claude probe: try to load /Game/Character/NpcDwarf/DT_NPCRoles
            // (a path our pak OVERRIDES — exists in shipping content). If this
            // resolves while the brand-new /Game/Mods/PorterGoat/... loader path
            // does not, IoStore is rejecting brand-new mod-pak paths from
            // PackageStore lookups (Theory A). If both fail, the pak isn't
            // mounting at all from PorterGoat_v1.0.1_P/.
            const wchar_t* overridePath = STR("/Game/Character/NpcDwarf/DT_NPCRoles.DT_NPCRoles");
            UObject* overrideFind = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, overridePath);
            UObject* overrideLoad = goat_callBlockingLoader(
                STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                STR("/Script/Engine.Default__KismetSystemLibrary"),
                STR("Asset"), overridePath);
            UObject* overridePost = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, overridePath);
            VLOG(STR("[MoriaCppMod] [Goat] override-path probe DT_NPCRoles: pre={:p} loaderRet={:p} post={:p}\n"),
                 (void*)overrideFind, (void*)overrideLoad, (void*)overridePost);
        }

        bool ensureGoatSpawnBindings()
        {
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
                    VLOG(STR("[MoriaCppMod] [Goat] resolved PRIMARY class via LoadClassAsset_Blocking: {}\n"),
                         GOAT_CLASS_PATHS[0]);
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
                                    VLOG(STR("[MoriaCppMod] [Goat] resolved class via FindAllOf scan: {} (path={})\n"),
                                         n.c_str(), c->GetPathName().c_str());
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
                m_kismetGameplayStaticsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.Default__GameplayStatics"));
            if (!m_goatBeginSpawnFn)
                m_goatBeginSpawnFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
            if (!m_goatFinishSpawnFn)
                m_goatFinishSpawnFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));
            return m_goatBPClass && m_kismetGameplayStaticsCDO
                && m_goatBeginSpawnFn && m_goatFinishSpawnFn;
        }

        // Write `value` at the offset of the named UProperty inside `parmsBuf`.
        // No-op on missing param or invalid offset (defensive against UFUNCTION
        // signature drift between builds).
        template<typename T>
        void writeGoatParm(UFunction* fn, void* parmsBuf, const wchar_t* name, const T& value)
        {
            auto* prop = findParam(fn, name);
            if (!prop) return;
            int off = prop->GetOffset_Internal();
            if (off < 0) return;
            *reinterpret_cast<T*>(static_cast<uint8_t*>(parmsBuf) + off) = value;
        }

        template<typename T>
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

            auto* compCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, fgkClassPath);
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

            auto* pcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorPlayerController"));
            if (!pcCls)
            {
                VLOG(STR("[MoriaCppMod] [PCDump] MorPlayerController class NOT resident\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PCDump] === MorPlayerController dump start ===\n"));

            std::wstring clsName;
            try { clsName = pcCls->GetName(); } catch (...) {}

            // Walk PROPERTIES first — we need to find a SettlementId / active
            // settlement reference so we can call ServerRescueNpc directly.
            // Live values are read from m_localPC if it's resident.
            UObject* liveInst = m_localPC && isObjectAlive(m_localPC) ? m_localPC : nullptr;
            int propCount = 0;
            for (auto* strct = static_cast<UStruct*>(pcCls); strct; strct = strct->GetSuperStruct())
            {
                std::wstring strctName;
                try { strctName = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    if (propCount >= 600) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                    // Filter to interesting names so the log isn't a wall.
                    std::wstring lpn = pn;
                    for (auto& c : lpn) c = (wchar_t)::towlower(c);
                    bool keep = lpn.find(L"settlement") != std::wstring::npos
                             || lpn.find(L"npc") != std::wstring::npos
                             || lpn.find(L"hall") != std::wstring::npos
                             || lpn.find(L"home") != std::wstring::npos
                             || lpn.find(L"manager") != std::wstring::npos
                             || lpn.find(L"id") != std::wstring::npos
                             || lpn.find(L"guid") != std::wstring::npos;
                    if (!keep) { ++propCount; continue; }

                    std::wstring valueStr;
                    if (liveInst)
                    {
                        try
                        {
                            int32_t off = prop->GetOffset_Internal();
                            uint8_t* base = reinterpret_cast<uint8_t*>(liveInst);
                            uint8_t* slot = base + off;
                            if (pcn == STR("ObjectProperty")
                                || pcn == STR("WeakObjectProperty")
                                || pcn == STR("ClassProperty"))
                            {
                                UObject* val = *reinterpret_cast<UObject**>(slot);
                                if (val)
                                {
                                    std::wstring vName, vCls;
                                    try { vName = val->GetName(); } catch (...) {}
                                    try { vCls = val->GetClassPrivate()->GetName(); } catch (...) {}
                                    valueStr = STR(" value=") + vName + STR(" (") + vCls + STR(")");
                                }
                                else valueStr = STR(" value=null");
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
                                swprintf(buf, 80, STR(" maybe-guid=%08X-%08X-%08X-%08X"),
                                         g[0], g[1], g[2], g[3]);
                                valueStr = buf;
                            }
                        } catch (...) {}
                    }

                    VLOG(STR("[MoriaCppMod] [PCDump] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                         clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str(),
                         (unsigned)prop->GetOffset_Internal(), valueStr.c_str());
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
                try { fnName = fn->GetName(); } catch (...) {}
                // Filter to interesting names: those involving inventory/open/container/storage/UI.
                std::wstring lower = fnName;
                for (auto& c : lower) c = (wchar_t)::towlower(c);
                if (lower.find(L"inventor") != std::wstring::npos
                    || lower.find(L"container") != std::wstring::npos
                    || lower.find(L"storage")   != std::wstring::npos
                    || lower.find(L"open")      != std::wstring::npos
                    || lower.find(L"close")     != std::wstring::npos
                    || lower.find(L"interact")  != std::wstring::npos
                    || lower.find(L"rescue")    != std::wstring::npos
                    || lower.find(L"loot")      != std::wstring::npos
                    || lower.find(L"chest")     != std::wstring::npos)
                {
                    int parmCount = 0;
                    std::wstring paramSig;
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (parmCount >= 6) break;
                        std::wstring pn, pcn;
                        try { pn = prop->GetName(); } catch (...) {}
                        try { pcn = prop->GetClass().GetName(); } catch (...) {}
                        if (!paramSig.empty()) paramSig += L", ";
                        paramSig += pcn + L" " + pn;
                        ++parmCount;
                    }
                    VLOG(STR("[MoriaCppMod] [PCDump] UFunc {}({})\n"),
                         fnName.c_str(), paramSig.c_str());
                }
                ++fnCount;
            }
            VLOG(STR("[MoriaCppMod] [PCDump] === MorPlayerController dump done (walked {} UFunctions, filtered) ===\n"),
                 fnCount);
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
                STR("/Script/FGK.FGKEquipComponent"),  // parent of MorEquipComponent
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
                    try { strctName = strct->GetName(); } catch (...) {}
                    for (auto* prop : strct->ForEachProperty())
                    {
                        std::wstring pn, pcn;
                        try { pn = prop->GetName(); } catch (...) {}
                        try { pcn = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [EquipDump] {} (from {}).{} : {}\n"),
                             clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str());
                        if (++count >= 200) break;
                    }
                    if (count >= 200) break;
                }
                VLOG(STR("[MoriaCppMod] [EquipDump] === {} dump done ({} props) ===\n"),
                     clsName.c_str(), count);
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
        void dumpInteractionWidgetSchema(UObject* widgetInstance)
        {
            if (m_interactionWidgetDumped) return;
            m_interactionWidgetDumped = true;
            if (!widgetInstance) return;

            UClass* cls = nullptr;
            try { cls = widgetInstance->GetClassPrivate(); } catch (...) {}
            if (!cls)
            {
                VLOG(STR("[MoriaCppMod] [InteractDump] widget has no class\n"));
                return;
            }
            std::wstring clsName;
            try { clsName = cls->GetName(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [InteractDump] === {} dump start (instance={:p}) ===\n"),
                 clsName.c_str(), (void*)widgetInstance);

            // Walk every property on class+supers; print name + type. ALSO
            // print live value for object/text/string/name/bool fields by
            // resolving the offset and reading the instance.
            int propCount = 0;
            for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
            {
                std::wstring strctName;
                try { strctName = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    if (propCount >= 400) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}

                    // Read live value when type is one of the common ones.
                    std::wstring valueStr;
                    try
                    {
                        int32_t off = prop->GetOffset_Internal();
                        uint8_t* base = reinterpret_cast<uint8_t*>(widgetInstance);
                        uint8_t* slot = base + off;
                        if (pcn == STR("ObjectProperty")
                            || pcn == STR("WeakObjectProperty")
                            || pcn == STR("ClassProperty")
                            || pcn == STR("InterfaceProperty"))
                        {
                            UObject* val = *reinterpret_cast<UObject**>(slot);
                            if (val)
                            {
                                std::wstring vName, vCls;
                                try { vName = val->GetName(); } catch (...) {}
                                try { vCls = val->GetClassPrivate()->GetName(); } catch (...) {}
                                valueStr = STR(" value=") + vName + STR(" (") + vCls + STR(")");
                            }
                            else valueStr = STR(" value=null");
                        }
                        else if (pcn == STR("StrProperty"))
                        {
                            FString* fs = reinterpret_cast<FString*>(slot);
                            std::wstring s;
                            try { s = fs->GetCharArray().GetData() ? fs->GetCharArray().GetData() : STR(""); } catch (...) {}
                            valueStr = STR(" value=\"") + s + STR("\"");
                        }
                        else if (pcn == STR("NameProperty"))
                        {
                            FName* fn = reinterpret_cast<FName*>(slot);
                            std::wstring s;
                            try { s = fn->ToString(); } catch (...) {}
                            valueStr = STR(" value=") + s;
                        }
                        else if (pcn == STR("TextProperty"))
                        {
                            FText* ft = reinterpret_cast<FText*>(slot);
                            std::wstring s;
                            try { s = ft->ToString(); } catch (...) {}
                            valueStr = STR(" value=\"") + s + STR("\"");
                        }
                        else if (pcn == STR("BoolProperty"))
                        {
                            valueStr = (*slot != 0) ? STR(" value=true") : STR(" value=false");
                        }
                    }
                    catch (...) {}

                    VLOG(STR("[MoriaCppMod] [InteractDump] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                         clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str(),
                         (unsigned)prop->GetOffset_Internal(), valueStr.c_str());
                    ++propCount;
                }
                if (propCount >= 400) break;
            }

            // Walk every UFunction on class+supers.
            int fnCount = 0;
            for (auto* fn : cls->ForEachFunctionInChain())
            {
                if (fnCount >= 300) break;
                std::wstring fnName;
                try { fnName = fn->GetName(); } catch (...) {}
                int parmCount = 0;
                std::wstring paramSig;
                for (auto* prop : fn->ForEachProperty())
                {
                    if (parmCount >= 6) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                    if (!paramSig.empty()) paramSig += STR(", ");
                    paramSig += pcn + STR(" ") + pn;
                    ++parmCount;
                }
                VLOG(STR("[MoriaCppMod] [InteractDump] UFUNC {}({})\n"),
                     fnName.c_str(), paramSig.c_str());
                ++fnCount;
            }

            VLOG(STR("[MoriaCppMod] [InteractDump] === {} dump done ({} props, {} ufuncs) ===\n"),
                 clsName.c_str(), propCount, fnCount);
        }

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
                m_packStaticMesh = goat_callBlockingLoader(
                    STR("/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking"),
                    STR("/Script/Engine.Default__KismetSystemLibrary"),
                    STR("Asset"), meshPath);
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
                packCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Game/Items/EpicPacks/BP_EpicPack_AdventurersPack_Large.BP_EpicPack_AdventurersPack_Large_C"));
                if (packCls) m_packItemClass = packCls;
            }
            if (!packCls)
            {
                VLOG(STR("[MoriaCppMod] [Goat] pack item class not yet resident; skipping equip (preload failed at character-load)\n"));
                return false;
            }

            // Find the goat's MorEquipComponent.
            auto* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorEquipComponent"));
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
            VLOG(STR("[MoriaCppMod] [Goat] ServerEquipDummyItem(BP_EpicPack_AdventurersPack_Large_C) fired on EquipComp={:p}\n"),
                 (void*)equipComp);
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
            if (m_saddlebagItemClass && isObjectAlive(m_saddlebagItemClass))
                return m_saddlebagItemClass;

            // 1) Direct path candidates. First hit wins.
            static const wchar_t* kCandidates[] = {
                STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                STR("/Game/Mods/PorterGoat/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                STR("/Game/Mods/PorterGoat/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                STR("/Game/Mods/SecretsOfKhazadDum/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                STR("/Game/Mods/SecretsOfKhazadDum/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"),
                nullptr
            };
            for (const wchar_t** p = kCandidates; *p; ++p)
            {
                UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, *p);
                if (!c) c = goat_loadClassAssetBlocking(*p);
                if (c && isObjectAlive(c))
                {
                    m_saddlebagItemClass = c;
                    VLOG(STR("[MoriaCppMod] [Goat] saddlebag item class resolved (direct): {} -> {:p}\n"),
                         *p, (void*)c);
                    return m_saddlebagItemClass;
                }
            }

            // 2) Runtime scan fallback — walk loaded BlueprintGeneratedClass
            //    objects, match by name substring. One-shot per session.
            VLOG(STR("[MoriaCppMod] [Goat] saddlebag class direct lookup failed across {} candidate paths — scanning loaded BPs\n"),
                 (int)(sizeof(kCandidates)/sizeof(kCandidates[0]) - 1));
            std::vector<UObject*> bpClasses;
            if (!findAllOfSafe(STR("BlueprintGeneratedClass"), bpClasses))
            {
                VLOG(STR("[MoriaCppMod] [Goat] BlueprintGeneratedClass scan failed (findAllOf returned false)\n"));
                return nullptr;
            }
            VLOG(STR("[MoriaCppMod] [Goat] BlueprintGeneratedClass scan: {} loaded classes\n"),
                 (int)bpClasses.size());

            auto containsCI = [](const std::wstring& s, const wchar_t* needle) -> bool {
                std::wstring lo; lo.reserve(s.size());
                for (wchar_t c : s) lo.push_back((wchar_t)towlower(c));
                std::wstring nl; for (size_t i = 0; needle[i]; ++i) nl.push_back((wchar_t)towlower(needle[i]));
                return lo.find(nl) != std::wstring::npos;
            };

            UClass* firstMatch = nullptr;
            for (UObject* c : bpClasses)
            {
                if (!c || !isObjectAlive(c)) continue;
                std::wstring n;
                try { n = c->GetName(); } catch (...) { continue; }
                // Match anything saddlebag-ish OR with a path under /Mods/PorterGoat/
                bool matchName = containsCI(n, STR("Saddle"))
                              || containsCI(n, STR("GoatBag"))
                              || containsCI(n, STR("GoatPack"))
                              || containsCI(n, STR("PorterPack"))
                              || containsCI(n, STR("PorterBag"));
                std::wstring fullPath;
                try { fullPath = c->GetFullName(); } catch (...) {}
                bool matchPath = containsCI(fullPath, STR("/PorterGoat/"))
                              || containsCI(fullPath, STR("/Mods/SecretsOfKhazadDum/"));
                if (!matchName && !matchPath) continue;

                VLOG(STR("[MoriaCppMod] [Goat] scan hit: name='{}' fullPath='{}'\n"),
                     n.c_str(), fullPath.c_str());

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
                try { name = firstMatch->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [Goat] saddlebag item class resolved (scan): '{}' -> {:p}\n"),
                     name.c_str(), (void*)firstMatch);
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
            UClass* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorEquipComponent"));
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
            VLOG(STR("[MoriaCppMod] [SaddleEquip] ServerEquipDummyItem(BP_PorterGoatSaddlebags_C) fired on EquipComp={:p}\n"),
                 (void*)equipComp);
            return true;
        }

        // [v7.2.0-rc.12a 2026-05-22] Resolve the chest class for phantom-
        // chest approach. Primary target is BP_StorageChest_Construction_C
        // (the settlement-buildable storage chest); fall back to other
        // chest receptacles if that class isn't resident. Cached.
        UClass* ensurePhantomChestClass()
        {
            if (m_phantomChestClass && isObjectAlive(m_phantomChestClass))
                return m_phantomChestClass;
            // [rc.12a.2] BP_StorageChest_Construction_C resolved cleanly in
            // rc.12a but its UI didn't open — likely because it requires
            // settlement-construction context (build state, materials).
            // Pivot to BP_ChestReceptacle_C first — exploration-reward
            // chests are complete by spawn, no construction state needed.
            static const wchar_t* kCandidates[] = {
                STR("/Game/LevelDesign/Placeables/Containers/BP_ChestReceptacle.BP_ChestReceptacle_C"),
                STR("/Game/LevelDesign/Placeables/Containers/BP_SmallChestReceptacle.BP_SmallChestReceptacle_C"),
                STR("/Game/LevelDesign/Placeables/Containers/BP_StorageChest_Construction.BP_StorageChest_Construction_C"),
                STR("/Game/LevelDesign/Placeables/Containers/BP_FallBackReceptacle.BP_FallBackReceptacle_C"),
                STR("/Game/LevelDesign/Placeables/Containers/BP_BarrelReceptacle.BP_BarrelReceptacle_C"),
                nullptr
            };
            for (const wchar_t** p = kCandidates; *p; ++p)
            {
                UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, *p);
                if (!c) c = goat_loadClassAssetBlocking(*p);
                if (c && isObjectAlive(c))
                {
                    m_phantomChestClass = c;
                    VLOG(STR("[MoriaCppMod] [PhantomChest] class resolved: {} -> {:p}\n"),
                         *p, (void*)c);
                    return c;
                }
            }
            VLOG(STR("[MoriaCppMod] [PhantomChest] no chest class resolvable across {} candidates\n"),
                 (int)(sizeof(kCandidates)/sizeof(kCandidates[0]) - 1));
            return nullptr;
        }

        // [v7.2.0-rc.12a 2026-05-22] Spawn a phantom chest near the goat.
        // For 12a: chest is SPAWNED VISIBLE at goat_location + (0,0,200)
        // so the player can walk up to it, press E (vanilla), and confirm
        // chest UI opens. No attach, no hide — that's 12b. No OpenChest
        // dispatch from goat menu — that's 12c.
        //
        // Gated by m_enablePhantomChest (INI: [GoatExperimental] PhantomChest).
        // Default OFF — opt-in via INI to test.
        //
        // Uses the same GameplayStatics two-step spawn pattern as goat
        // spawn (BeginDeferredActorSpawnFromClass + FinishSpawningActor).
        void spawnPhantomChestNearGoat(UObject* goat)
        {
            if (!m_enablePhantomChest)
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] disabled via INI — skip\n"));
                return;
            }
            if (!goat || !isObjectAlive(goat))
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] goat null/dead — skip\n"));
                return;
            }
            if (!ensureGoatSpawnBindings())
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] spawn bindings not ready\n"));
                return;
            }
            UClass* chestCls = ensurePhantomChestClass();
            if (!chestCls)
            {
                showOnScreen(L"Phantom chest class missing", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }

            // Read goat location.
            FVec3f loc{};
            if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                struct { FVec3f Ret{}; } lp{};
                if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
            }
            // 12a: spawn 200 cm above goat for visibility.
            FVec3f chestLoc{loc.X, loc.Y, loc.Z + 200.0f};
            FTransformRaw xform{};
            xform.Rotation    = {0.0f, 0.0f, 0.0f, 1.0f};
            xform.Translation = chestLoc;
            xform.Scale3D     = {1.0f, 1.0f, 1.0f};

            std::vector<uint8_t> buf(m_goatBeginSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
            writeGoatParm<UClass*>      (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         chestCls);
            writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            writeGoatParm<uint8_t>      (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);  // AlwaysSpawn

            VLOG(STR("[MoriaCppMod] [PhantomChest] BeginDeferred: goatLoc=({:.1f},{:.1f},{:.1f}) chestLoc=({:.1f},{:.1f},{:.1f})\n"),
                 loc.X, loc.Y, loc.Z, chestLoc.X, chestLoc.Y, chestLoc.Z);
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data()))
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] BeginDeferred PE failed\n"));
                return;
            }
            UObject* chest = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!chest)
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] BeginDeferred returned null\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PhantomChest] BeginDeferred returned chest={:p}\n"), (void*)chest);

            // FinishSpawningActor — commits the spawn.
            std::vector<uint8_t> buf2(m_goatFinishSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          chest);
            writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
            safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

            // Track for later access (12b attach, 12c OpenChest dispatch).
            m_phantomChest = RC::Unreal::FWeakObjectPtr(chest);
            VLOG(STR("[MoriaCppMod] [PhantomChest] FinishSpawningActor fired; tracked at {:p}\n"),
                 (void*)chest);
            VLOG(STR("[MoriaCppMod] [PhantomChest] 12a SUCCESS: walk to chest above goat, press E, confirm vanilla chest UI opens\n"));
            showOnScreen(L"Phantom chest spawned above goat — walk up, press E", 5.0f, 0.4f, 0.9f, 0.4f);

            // 12a one-shot diagnostic: dump chest's UFunctions matching
            // open/use keywords so we know what OpenChest looks like.
            {
                VLOG(STR("[MoriaCppMod] [PhantomChest] === chest UFunctions matching open/use/show/activate/storage ===\n"));
                const wchar_t* candidates[] = {
                    STR("OpenChest"), STR("OpenContainer"), STR("OpenInventory"),
                    STR("ServerOpenChest"), STR("ServerInteract"), STR("ShowInventory"),
                    STR("ActivateChest"), STR("OnInteract"), STR("OnUsed"),
                    STR("BP_OnInteract"), STR("Interact"),
                    nullptr
                };
                for (const wchar_t** p = candidates; *p; ++p)
                {
                    auto* fn = chest->GetFunctionByNameInChain(*p);
                    if (fn)
                    {
                        int parms = 0;
                        try { parms = fn->GetParmsSize(); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [PhantomChest]   {} PRESENT (parmSize={})\n"), *p, parms);
                    }
                }
                VLOG(STR("[MoriaCppMod] [PhantomChest] === end UFunc probe ===\n"));
            }

            // [rc.12a.2 2026-05-22] Immediately call OpenChest after spawn.
            // Bypasses needing to walk to chest + press E (which depends on
            // the chest's E-prompt being correctly configured for non-
            // settlement contexts). If OpenChest opens a UI here, then the
            // FSM works regardless of E-prompt — that's the answer 12c
            // needs.
            //
            // Brief delay would be nicer (let chest finish initializing)
            // but we have no game-thread sleep primitive. Fire immediately;
            // if it fails because the chest isn't ready, the log will tell
            // us and we can defer to a tick-based open in 12a.3.
            {
                auto* openFn = chest->GetFunctionByNameInChain(STR("OpenChest"));
                if (openFn)
                {
                    int parmsSize = 0;
                    try { parmsSize = openFn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [PhantomChest] calling OpenChest (parmSize={}) on chest={:p} immediately post-spawn\n"),
                         parmsSize, (void*)chest);
                    if (parmsSize == 0)
                    {
                        try { safeProcessEvent(chest, openFn, nullptr); } catch (...) {}
                    }
                    else
                    {
                        std::vector<uint8_t> ob(parmsSize, 0);
                        try { safeProcessEvent(chest, openFn, ob.data()); } catch (...) {}
                    }
                    VLOG(STR("[MoriaCppMod] [PhantomChest] OpenChest dispatched — expect storage UI to appear\n"));
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PhantomChest] OpenChest UFunction missing on this chest class — won't auto-open\n"));
                }
            }
        }

        // [v7.2.0-rc.12b 2026-05-23] Spawn Tobi's saddlebag as a world
        // actor near the goat. BP_SaddleBags_Goat_C inherits from
        // MorContainerItem → AInventoryItem → AActor, so it spawns via
        // the same BeginDeferred + FinishSpawningActor pattern.
        //
        // Three things we want to learn:
        //   1. Does the bag spawn cleanly as a world actor (it's an item
        //      class so it might require a wrapper)?
        //   2. What UFunctions does the spawned instance expose for opening
        //      its storage?
        //   3. Does any of them surface the proper "Goat Saddlebags" UI
        //      when called directly post-spawn?
        //
        // Gated by m_enableSaddlebagAtGoat (INI: [GoatExperimental]
        // SaddlebagAtGoat = true). Default OFF — opt-in.
        void spawnSaddlebagAtGoat(UObject* goat)
        {
            if (!m_enableSaddlebagAtGoat)
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] disabled via INI — skip\n"));
                return;
            }
            if (!goat || !isObjectAlive(goat))
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] goat null/dead — skip\n"));
                return;
            }
            if (!ensureGoatSpawnBindings())
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] spawn bindings not ready\n"));
                return;
            }
            UClass* saddleCls = ensureSaddlebagItemClass();  // BP_SaddleBags_Goat_C
            if (!saddleCls)
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] saddlebag class unresolved\n"));
                showOnScreen(L"Saddlebag class missing", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }

            // Read goat location + forward vector — spawn 100 cm in front of goat.
            FVec3f loc{};
            FVec3f fwd{1.0f, 0.0f, 0.0f};
            if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                struct { FVec3f Ret{}; } lp{};
                if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
            }
            if (auto* fwdFn = goat->GetFunctionByNameInChain(STR("GetActorForwardVector")))
            {
                struct { FVec3f Ret; } p{};
                if (safeProcessEvent(goat, fwdFn, &p)) fwd = p.Ret;
            }
            FVec3f bagLoc{ loc.X + fwd.X * 100.0f, loc.Y + fwd.Y * 100.0f, loc.Z };
            FTransformRaw xform{};
            xform.Rotation    = {0.0f, 0.0f, 0.0f, 1.0f};
            xform.Translation = bagLoc;
            xform.Scale3D     = {1.0f, 1.0f, 1.0f};

            std::vector<uint8_t> buf(m_goatBeginSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
            writeGoatParm<UClass*>      (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         saddleCls);
            writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            writeGoatParm<uint8_t>      (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);  // AlwaysSpawn

            VLOG(STR("[MoriaCppMod] [SaddleAtGoat] BeginDeferred: goatLoc=({:.1f},{:.1f},{:.1f}) bagLoc=({:.1f},{:.1f},{:.1f})\n"),
                 loc.X, loc.Y, loc.Z, bagLoc.X, bagLoc.Y, bagLoc.Z);
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data()))
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] BeginDeferred PE failed\n"));
                return;
            }
            UObject* bag = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!bag)
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] BeginDeferred returned null — item class may not be world-spawnable\n"));
                showOnScreen(L"Saddlebag spawn returned null", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [SaddleAtGoat] BeginDeferred returned bag={:p}\n"), (void*)bag);

            std::vector<uint8_t> buf2(m_goatFinishSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          bag);
            writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
            safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

            m_saddlebagWorldActor = RC::Unreal::FWeakObjectPtr(bag);
            VLOG(STR("[MoriaCppMod] [SaddleAtGoat] FinishSpawningActor fired; tracked at {:p}\n"), (void*)bag);
            showOnScreen(L"Saddlebag spawned at goat — look for it / E it", 5.0f, 0.4f, 0.9f, 0.4f);

            // Probe + auto-open. Try a broad set of UFunction names the
            // bag may expose for opening its storage.
            VLOG(STR("[MoriaCppMod] [SaddleAtGoat] === bag UFunctions matching open/use/show/activate/storage/interact ===\n"));
            const wchar_t* candidates[] = {
                STR("OpenChest"), STR("OpenContainer"), STR("OpenInventory"),
                STR("OpenStorage"), STR("ShowStorage"), STR("ShowInventory"),
                STR("ServerUse"), STR("ServerInteract"), STR("ServerOpen"),
                STR("ActivateContainer"), STR("OnInteract"), STR("OnUsed"),
                STR("BP_OnInteract"), STR("Interact"), STR("Use"),
                nullptr
            };
            UFunction* firstOpener = nullptr;
            for (const wchar_t** p = candidates; *p; ++p)
            {
                auto* fn = bag->GetFunctionByNameInChain(*p);
                if (fn)
                {
                    int parms = 0;
                    try { parms = fn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [SaddleAtGoat]   {} PRESENT (parmSize={})\n"), *p, parms);
                    if (!firstOpener) firstOpener = fn;  // remember first hit
                }
            }
            VLOG(STR("[MoriaCppMod] [SaddleAtGoat] === end UFunc probe ===\n"));

            // Auto-call the first opener we found.
            if (firstOpener)
            {
                int parmsSize = 0;
                try { parmsSize = firstOpener->GetParmsSize(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] auto-calling first opener (parmSize={}) on bag={:p}\n"),
                     parmsSize, (void*)bag);
                if (parmsSize == 0)
                {
                    try { safeProcessEvent(bag, firstOpener, nullptr); } catch (...) {}
                }
                else
                {
                    std::vector<uint8_t> ob(parmsSize, 0);
                    try { safeProcessEvent(bag, firstOpener, ob.data()); } catch (...) {}
                }
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] opener dispatched — expect storage UI\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [SaddleAtGoat] no opener UFunction found — walk to bag, press E manually\n"));
            }
        }

        // [v7.2.0-rc.12b.2 2026-05-23] Add the saddlebag directly to the
        // goat's MorInventoryComponent via ServerDebugSetItem. Mirrors
        // grantPorterItemToPlayer (which targets the player's InvComp);
        // this targets the goat's. Logs goat's Items array before + after
        // so we can see whether ServerDebugSetItem accepts non-player
        // InvComp targets.
        //
        // Gated by m_enableSaddlebagAtGoat (reused INI flag). If this
        // succeeds, the bag is "in the goat" architecturally. Opening
        // it from the goat menu still hits the cross-actor authority
        // wall we proved in rc.9 — separate problem.
        bool addSaddlebagToGoatInventory(UObject* goat)
        {
            if (!m_enableSaddlebagAtGoat) return false;
            if (!goat || !isObjectAlive(goat)) return false;

            UClass* saddleCls = ensureSaddlebagItemClass();
            if (!saddleCls)
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] saddlebag class unresolved\n"));
                return false;
            }

            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            if (!invCls) return false;
            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (!getCompFn) return false;
            std::vector<uint8_t> gbuf(getCompFn->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), invCls);
            if (!safeProcessEvent(goat, getCompFn, gbuf.data())) return false;
            UObject* goatInv = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
            if (!goatInv || !isObjectAlive(goatInv))
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] goat MorInventoryComponent not found\n"));
                return false;
            }

            // Log goat inventory count BEFORE.
            auto countGoatItems = [&]() -> int {
                FProperty* itemsProp = goatInv->GetPropertyByNameInChain(STR("Items"));
                if (!itemsProp) return -1;
                uint8_t* lb = reinterpret_cast<uint8_t*>(goatInv)
                            + itemsProp->GetOffset_Internal() + iiaListOff();
                if (!isReadableMemory(lb, 16)) return -2;
                int32_t n = *reinterpret_cast<int32_t*>(lb + 8);
                return n;
            };
            int before = countGoatItems();
            VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] goat InvComp.Items count BEFORE = {}\n"), before);

            UFunction* dsiFn = goatInv->GetFunctionByNameInChain(STR("ServerDebugSetItem"));
            if (!dsiFn)
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] ServerDebugSetItem missing on goat InvComp — bail\n"));
                return false;
            }
            int sz = dsiFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* pItem  = findParam(dsiFn, STR("Item"));
            if (!pItem) pItem = findParam(dsiFn, STR("ItemClass"));
            auto* pCount = findParam(dsiFn, STR("Count"));
            if (!pItem || !pCount)
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] expected parms missing — bail\n"));
                return false;
            }
            *reinterpret_cast<UClass**>(buf.data() + pItem->GetOffset_Internal()) = saddleCls;
            *reinterpret_cast<int32_t*>(buf.data() + pCount->GetOffset_Internal()) = 1;
            VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] firing ServerDebugSetItem(BP_SaddleBags_Goat_C, 1) on goat InvComp={:p}\n"),
                 (void*)goatInv);
            if (!safeProcessEvent(goatInv, dsiFn, buf.data()))
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] ServerDebugSetItem PE returned false\n"));
                return false;
            }
            int after = countGoatItems();
            VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] goat InvComp.Items count AFTER = {} (delta {})\n"),
                 after, after - before);
            if (after > before)
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] SUCCESS — bag added to goat inventory\n"));
                showOnScreen(L"Saddlebag added to goat inventory", 3.0f, 0.4f, 0.9f, 0.4f);
                return true;
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [AddSaddleToGoat] count unchanged — RPC may be player-only\n"));
                showOnScreen(L"ServerDebugSetItem rejected on goat InvComp", 3.0f, 0.95f, 0.7f, 0.4f);
                return false;
            }
        }

        // Find the goat's UMorNPCComponent and call SetRoleFuzzy("Porter") on it.
        // This triggers the porter equipment loadout via HandleEquipmentChanged
        // → ResolveEquipmentChanged on the pawn's MorEquipComponent, which is
        // what makes the bells + packsaddle visible. The role's EnabledState
        // was flipped from Disabled to Live by the PorterGoat_P.pak edit to
        // /Game/Character/NpcDwarf/DT_NPCRoles, so the role is now assignable.
        // Returns true if the call dispatched successfully.
        bool assignPorterRole(UObject* goat)
        {
            if (!goat || !isObjectAlive(goat))
            {
                VLOG(STR("[MoriaCppMod] [Goat] assignPorterRole: goat not alive (silent bail)\n"));
                return false;
            }

            // Resolve UMorNPCComponent class once (cached implicitly via
            // StaticFindObject hit cache).
            auto* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!npcCompCls)
            {
                VLOG(STR("[MoriaCppMod] [Goat] MorNPCComponent UClass not resident\n"));
                return false;
            }

            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (!getCompFn)
            {
                VLOG(STR("[MoriaCppMod] [Goat] assignPorterRole: GetComponentByClass UFn not found (silent bail)\n"));
                return false;
            }
            int sz = getCompFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
            if (!safeProcessEvent(goat, getCompFn, buf.data()))
            {
                VLOG(STR("[MoriaCppMod] [Goat] assignPorterRole: PE GetComponentByClass FAILED (silent bail)\n"));
                return false;
            }
            UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!npcComp || !isObjectAlive(npcComp))
            {
                VLOG(STR("[MoriaCppMod] [Goat] no MorNPCComponent on goat — porter role not assignable\n"));
                return false;
            }

            // Register the goat with the settlement NPC manager FIRST. This is
            // what lets the equipment-loadout delegate (HandleEquipmentChanged
            // → ResolveEquipmentChanged) see the role change later. Without
            // registration, SetRoleFuzzy fires the delegate but no equipment
            // pipeline picks it up (no bells, no pack).
            if (auto* regFn = npcComp->GetFunctionByNameInChain(STR("RegisterWithNPCManager")))
            {
                safeProcessEvent(npcComp, regFn, nullptr);
                VLOG(STR("[MoriaCppMod] [Goat] RegisterWithNPCManager fired on MorNPCComponent={:p}\n"),
                     (void*)npcComp);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [Goat] RegisterWithNPCManager UFunction missing\n"));
            }

            // [v1.1.0 FIXUP 2026-05-09] Was: SetIsInteractive(false) to
            // suppress "E to rescue" prompt. That also blocks inventory
            // inspection — user can't open the goat's bag. Now that the
            // goat is properly registered (post-v1.1.0 pak), the vanilla
            // interaction prompt may show different options anyway. Leave
            // interaction ON so the player can actually USE the goat.
            // The "E to rescue" cosmetic concern can be addressed via the
            // suspended chest-link-future research's text override path
            // when we revisit it.
            if (auto* siFn = npcComp->GetFunctionByNameInChain(STR("SetIsInteractive")))
            {
                struct { bool bValue{true}; } sip{};
                safeProcessEvent(npcComp, siFn, &sip);
                VLOG(STR("[MoriaCppMod] [Goat] SetIsInteractive(true) fired on MorNPCComponent (interaction enabled — inventory accessible)\n"));
            }

            // [v1.1.0 ROLE RE-ENABLED 2026-05-09] Per desktop brief: Porter
            // role is REQUIRED to activate Bst_NPCGoatWorkPorter_C — the
            // follow-the-leash behavior tree. We previously skipped it
            // thinking the role was the wander source; the actual cause
            // is that the FSM defaults to "Unaware" (idle wander) and
            // never auto-transitions to "WorkTime" where the Porter follow
            // logic runs. Plus the EQS query that picks the leash target
            // doesn't return the player. See setGoatLeashActor + tick
            // diagnostics for the fix.
            auto* setRoleFn = npcComp->GetFunctionByNameInChain(STR("SetRoleFuzzy"));
            if (!setRoleFn)
            {
                VLOG(STR("[MoriaCppMod] [Goat] SetRoleFuzzy UFunction missing\n"));
                return false;
            }
            auto* pRole = findParam(setRoleFn, STR("RoleString"));
            if (!pRole) return false;
            const wchar_t* roleStr = STR("Porter");
            int32_t strLen = static_cast<int32_t>(wcslen(roleStr)) + 1;
            void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
            if (!strBuf) return false;
            wmemcpy(static_cast<wchar_t*>(strBuf), roleStr, strLen);
            int sz2 = setRoleFn->GetParmsSize();
            std::vector<uint8_t> buf2(sz2, 0);
            uint8_t* fstr = buf2.data() + pRole->GetOffset_Internal();
            *reinterpret_cast<void**>   (fstr + 0)  = strBuf;
            *reinterpret_cast<int32_t*> (fstr + 8)  = strLen;
            *reinterpret_cast<int32_t*> (fstr + 12) = strLen;
            safeProcessEvent(npcComp, setRoleFn, buf2.data());
            VLOG(STR("[MoriaCppMod] [Goat] SetRoleFuzzy(\"Porter\") fired on MorNPCComponent={:p}\n"),
                 (void*)npcComp);

            // [v1.1.1 OPTION B 2026-05-09] Per desktop's usmap analysis,
            // post-rescue state lives in `bIsRescued` (master flag) and
            // gates Talk/Details/Manage interaction prompts. Goat
            // defaults to false everywhere → no prompts surface.
            // Skip rescue dialog flow entirely; flip flags directly via
            // reflection (no raw offsets — UE4SS GetValuePtrByPropertyNameInChain).
            auto setBoolByName = [npcComp](const wchar_t* name, bool value, const wchar_t* tag) {
                bool* p = npcComp->GetValuePtrByPropertyNameInChain<bool>(name);
                if (p)
                {
                    *p = value;
                    VLOG(STR("[MoriaCppMod] [Goat] post-rescue flag {} = {} (tag={})\n"),
                         name, value ? STR("true") : STR("false"), tag);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [Goat] post-rescue flag {} NOT FOUND (tag={})\n"),
                         name, tag);
                }
            };

            // Master state + interaction enables.
            setBoolByName(STR("bIsRescued"),                    true,  STR("master-rescued"));
            setBoolByName(STR("bInteractionEnabled"),           true,  STR("master-interaction"));
            setBoolByName(STR("bTalkInteractionEnabled"),       true,  STR("talk"));
            setBoolByName(STR("bDetailsInteractionEnabled"),    true,  STR("details"));
            setBoolByName(STR("bManageInteractionEnabled"),     true,  STR("manage"));
            // Register companions (per desktop: probably bind to input system).
            setBoolByName(STR("bTalkInteractionRegister"),      true,  STR("talk-reg"));
            setBoolByName(STR("bDetailsInteractionRegister"),   true,  STR("details-reg"));
            setBoolByName(STR("bManagerInteractionRegister"),   true,  STR("manage-reg"));
            // [v1.1.1 REVERT 2026-05-10] Disabling Rescue/Recruit broke
            // Porter follow behavior (brain likely checks one of these
            // bools to pick pre-rescue vs post-rescue logic). Leave on.
            // Rescue prompt visible is better than no prompt + no follow.
            // The IsUsableBy override (PE-post hook below in dllmain) is
            // the right fix for the prompt-resolution gate, not these.

            // Diagnostic read-back for the master flags.
            if (auto* p = npcComp->GetValuePtrByPropertyNameInChain<bool>(STR("bIsRescued")))
                VLOG(STR("[MoriaCppMod] [Goat] confirmed bIsRescued = {}\n"),
                     *p ? STR("true") : STR("false"));

            // [v1.1.1 INTERACTABLE-MANAGER REG 2026-05-09] We've been
            // calling RegisterWithNPCManager (NPC roster) but not
            // RegisterToInteractableManager (the prompt-resolver). The
            // interaction widget queries the InteractableManager for
            // candidates within range — a goat enrolled in the NPC
            // roster but NOT the InteractableManager has no prompts
            // surface. Fix.
            if (auto* regInteractFn = npcComp->GetFunctionByNameInChain(STR("RegisterToInteractableManager")))
            {
                safeProcessEvent(npcComp, regInteractFn, nullptr);
                VLOG(STR("[MoriaCppMod] [Goat] RegisterToInteractableManager fired on MorNPCComponent\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [Goat] RegisterToInteractableManager UFunction missing\n"));
            }

            // Diagnostics — what does the goat itself report about its
            // interactability after all our writes?
            if (auto* getIntFn = npcComp->GetFunctionByNameInChain(STR("GetIsInteractive")))
            {
                int sz = getIntFn->GetParmsSize();
                std::vector<uint8_t> buf(sz, 0);
                if (safeProcessEvent(npcComp, getIntFn, buf.data()))
                {
                    bool isInt = readGoatParm<bool>(getIntFn, buf.data(), STR("ReturnValue"), false);
                    VLOG(STR("[MoriaCppMod] [Goat] GetIsInteractive() returned {}\n"),
                         isInt ? STR("true") : STR("false"));
                }
            }
            if (auto* usableFn = npcComp->GetFunctionByNameInChain(STR("IsUsableBy")))
            {
                UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
                if (pawn)
                {
                    int sz = usableFn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);
                    writeGoatParm<UObject*>(usableFn, buf.data(), STR("Interactor"), pawn);
                    if (safeProcessEvent(npcComp, usableFn, buf.data()))
                    {
                        bool usable = readGoatParm<bool>(usableFn, buf.data(), STR("ReturnValue"), false);
                        VLOG(STR("[MoriaCppMod] [Goat] IsUsableBy(player) returned {}\n"),
                             usable ? STR("true") : STR("false"));
                    }
                }
            }

            // [v1.1.1 FULL RESCUE 2026-05-10] Fire the actual blueprint
            // rescue path directly — ServerRescueNpc(guid, settlement)
            // is what the player's hold-E charge eventually triggers.
            // This runs the FULL chain: settlement-assign + OnNpcRescued
            // delegate broadcast + BP_NPCManager state flip + whatever
            // C++-internal flags rescue toggles. Per desktop's brief
            // analysis, this is the right trigger for the post-rescue
            // state we've been chasing manually.
            forceRescueGoat(npcComp, /*settlementId=*/0);

            // ServerSendNpcToSettlement as a safety net — if rescue
            // already adds to roster, this is a no-op duplicate; if
            // rescue silently bailed for some validation reason, this
            // still gets us roster membership for IsUsableBy.
            assignGoatToSettlement(npcComp, /*waypointId=*/0);

            return true;
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
            auto* pSet  = findParam(rescueFn, STR("SettlementId"));
            if (!pGuid || !pSet)
            {
                VLOG(STR("[MoriaCppMod] [Goat] ServerRescueNpc params missing (guid={:p} settlement={:p})\n"),
                     (void*)pGuid, (void*)pSet);
                return;
            }
            std::memcpy(buf.data() + pGuid->GetOffset_Internal(), guidPtr, 16);
            *reinterpret_cast<uint32_t*>(buf.data() + pSet->GetOffset_Internal()) = settlementId;

            uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
            VLOG(STR("[MoriaCppMod] [Goat] firing ServerRescueNpc(guid={:08X}-{:08X}-{:08X}-{:08X}, settlementId={})\n"),
                 g[0], g[1], g[2], g[3], settlementId);
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
            UObject* worldCtx = m_localPC && isObjectAlive(m_localPC) ? m_localPC :
                                (m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr);
            if (!worldCtx)
            {
                VLOG(STR("[MoriaCppMod] [Probe] no world context (PC/pawn) — bail\n"));
                return;
            }

            // 1. Resolve manager class. OnNpcRescued is defined on
            // C++ MorSettlementManager (not BP_StoryManager — confirmed
            // by reflection walk on previous probe). Try the BP subclass
            // first; fall back to the C++ class.
            auto* storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
            if (!storyMgrCls)
            {
                storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
            }
            if (!storyMgrCls)
            {
                VLOG(STR("[MoriaCppMod] [Probe] settlement-manager class not resident (tried BP and C++)\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [Probe] using manager class={}\n"),
                 storyMgrCls->GetName().c_str());

            // 2. Find FGKUtils::GetManager UFunction.
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkUtilsCDO)
            {
                VLOG(STR("[MoriaCppMod] [Probe] FGKUtils::GetManager unresolvable (fn={:p} cdo={:p})\n"),
                     (void*)getMgrFn, (void*)fgkUtilsCDO);
                return;
            }

            // 3. Call FGKUtils::GetManager(worldCtx, BP_StoryManager_C class).
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       storyMgrCls);
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
            VLOG(STR("[MoriaCppMod] [Probe] story manager singleton={:p} class={}\n"),
                 (void*)storyMgr, safeClassName(storyMgr).c_str());

            // 4. Find OnNpcRescued MulticastInlineDelegateProperty on the
            // manager (walks super chain — likely defined on the C++
            // MorSettlementManager parent per desktop's brief).
            FProperty* delegateProp = nullptr;
            std::wstring delegateOwnerName;
            for (auto* strct = static_cast<UStruct*>(storyMgr->GetClassPrivate());
                 strct && !delegateProp; strct = strct->GetSuperStruct())
            {
                for (auto* prop : strct->ForEachProperty())
                {
                    std::wstring pn;
                    try { pn = prop->GetName(); } catch (...) {}
                    if (pn == STR("OnNpcRescued"))
                    {
                        delegateProp = prop;
                        try { delegateOwnerName = strct->GetName(); } catch (...) {}
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
            try { delegatePropTypeName = delegateProp->GetClass().GetName(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [Probe] OnNpcRescued found: type={} owner={} off=0x{:X}\n"),
                 delegatePropTypeName.c_str(), delegateOwnerName.c_str(),
                 (unsigned)delegateProp->GetOffset_Internal());

            // 5. Walk the FMulticastScriptDelegate's InvocationList.
            // FMulticastScriptDelegate layout (UE4.27): TArray<FScriptDelegate>.
            // FScriptDelegate = { FWeakObjectPtr Object (8 bytes); FName FunctionName (8 bytes); }
            // = 16 bytes per entry. The TArray itself is { ptr, num, max } = 16 bytes.
            uint8_t* delegateSlot = reinterpret_cast<uint8_t*>(storyMgr) + delegateProp->GetOffset_Internal();
            // The MulticastInlineDelegate has a layer of indirection — it stores
            // a FMulticastScriptDelegate which has TArray<FScriptDelegate> at offset 0.
            // For UE4.27 FMulticastScriptDelegate, the InvocationList TArray is at offset 0.
            void**   listData = reinterpret_cast<void**>(delegateSlot + 0);
            int32_t* listNum  = reinterpret_cast<int32_t*>(delegateSlot + 8);
            int32_t* listMax  = reinterpret_cast<int32_t*>(delegateSlot + 12);
            int32_t  num      = *listNum;
            VLOG(STR("[MoriaCppMod] [Probe] OnNpcRescued subscribers: count={} (max={}, data={:p})\n"),
                 num, *listMax, *listData);
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
                try { fnameStr = fname.ToString(); } catch (...) {}
                // Resolve the weak object via UE4SS's wrapper.
                RC::Unreal::FWeakObjectPtr wp;
                std::memcpy(&wp, entry + 0, sizeof(RC::Unreal::FWeakObjectPtr));
                UObject* obj = wp.Get();
                std::wstring objName, objCls;
                if (obj && isObjectAlive(obj))
                {
                    try { objName = obj->GetName(); } catch (...) {}
                    try { objCls = obj->GetClassPrivate()->GetName(); } catch (...) {}
                }
                else { objName = STR("<null/dead>"); }
                VLOG(STR("[MoriaCppMod] [Probe]   [{}] obj={} cls={} fn={} (wptrIdx={} sn={})\n"),
                     i, objName.c_str(), objCls.c_str(), fnameStr.c_str(),
                     wptr[0], wptr[1]);
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
                        if (g && isObjectAlive(g)) { goat = g; break; }
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
            VLOG(STR("[MoriaCppMod] [Probe] MorWandererComponent class resident: {}\n"),
                 wandCls->GetName().c_str());

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
            VLOG(STR("[MoriaCppMod] [Probe] *** MorWandererComponent FOUND on goat: ptr={:p} cls={} ***\n"),
                 (void*)wandComp, safeClassName(wandComp).c_str());

            // [v1.2.9 BASELINE 2026-05-10] Live values of the key bools
            // desktop asked about — pre-rescue state.
            auto readWandBool = [wandComp](const wchar_t* name) -> std::wstring {
                bool* p = wandComp->GetValuePtrByPropertyNameInChain<bool>(name);
                if (!p) return STR("(not found)");
                return *p ? STR("true") : STR("false");
            };
            VLOG(STR("[MoriaCppMod] [Probe]   LIVE bRecruitInteractionEnabled = {}\n"),
                 readWandBool(STR("bRecruitInteractionEnabled")).c_str());
            VLOG(STR("[MoriaCppMod] [Probe]   LIVE bRecruitInteractionRegister = {}\n"),
                 readWandBool(STR("bRecruitInteractionRegister")).c_str());
            VLOG(STR("[MoriaCppMod] [Probe]   LIVE bBuffInteractionEnabled = {}\n"),
                 readWandBool(STR("bBuffInteractionEnabled")).c_str());
            VLOG(STR("[MoriaCppMod] [Probe]   LIVE bIsActive = {}\n"),
                 readWandBool(STR("bIsActive")).c_str());

            // Dump all UPROPERTYs (full chain).
            int propCount = 0;
            for (auto* strct = static_cast<UStruct*>(wandComp->GetClassPrivate());
                 strct; strct = strct->GetSuperStruct())
            {
                std::wstring strctName;
                try { strctName = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    if (propCount >= 200) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [Probe]   PROP (from {}).{} : {} off=0x{:X}\n"),
                         strctName.c_str(), pn.c_str(), pcn.c_str(),
                         (unsigned)prop->GetOffset_Internal());
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
                try { fnName = fn->GetName(); } catch (...) {}
                int parmCount = 0;
                std::wstring sig;
                for (auto* p : fn->ForEachProperty())
                {
                    if (parmCount >= 6) break;
                    std::wstring pn, pcn;
                    try { pn = p->GetName(); } catch (...) {}
                    try { pcn = p->GetClass().GetName(); } catch (...) {}
                    if (!sig.empty()) sig += STR(", ");
                    sig += pcn + STR(" ") + pn;
                    ++parmCount;
                }
                VLOG(STR("[MoriaCppMod] [Probe]   UFUNC {}({})\n"), fnName.c_str(), sig.c_str());
                ++fnCount;
            }
            VLOG(STR("[MoriaCppMod] [Probe] === end MorWandererComponent dump ({} props, {} fns) ===\n"),
                 propCount, fnCount);
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
            auto* npcMgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_NPCManager.BP_NPCManager_C"));
            if (!npcMgrCls)
                npcMgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorNPCManager"));
            if (!npcMgrCls)
            {
                VLOG(STR("[MoriaCppMod] [Probe] AMorNPCManager class missing\n"));
                return;
            }
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkUtilsCDO) return;
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       npcMgrCls);
            if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data())) return;
            UObject* npcMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!npcMgr || !isObjectAlive(npcMgr))
            {
                VLOG(STR("[MoriaCppMod] [Probe] AMorNPCManager singleton missing\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [Probe] NPCManager singleton={:p} cls={}\n"),
                 (void*)npcMgr, safeClassName(npcMgr).c_str());

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
                FProperty* prop = npcMgr->GetClassPrivate()->FindProperty(
                    RC::Unreal::FName(nm, RC::Unreal::FNAME_Find));
                if (!prop) continue;
                std::wstring tn;
                try { tn = prop->GetClass().GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [Probe]   property '{}' EXISTS (type={} off=0x{:X})\n"),
                     nm, tn.c_str(), (unsigned)prop->GetOffset_Internal());

                if (tn == STR("ArrayProperty"))
                {
                    uint8_t* slot = reinterpret_cast<uint8_t*>(npcMgr) + prop->GetOffset_Internal();
                    void**   data = reinterpret_cast<void**>  (slot + 0);
                    int32_t* num  = reinterpret_cast<int32_t*>(slot + 8);
                    VLOG(STR("[MoriaCppMod] [Probe]     array entries: {}\n"), *num);

                    // [v1.2.10 SAFE] Identify inner element type via the
                    // FArrayProperty's Inner property. We MUST NOT call
                    // FName::ToString on raw entry bytes — SEH AV is not
                    // catchable by C++ try/catch (proved by v1.2.9 crash).
                    auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                    FProperty* inner = nullptr;
                    try { inner = arrProp->GetInner(); } catch (...) {}
                    std::wstring innerTn;
                    if (inner) { try { innerTn = inner->GetClass().GetName(); } catch (...) {} }
                    VLOG(STR("[MoriaCppMod] [Probe]     inner type: {}\n"),
                         innerTn.empty() ? STR("<unknown>") : innerTn.c_str());

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
                                try { eName = e->GetName(); } catch (...) {}
                                try { eCls = e->GetClassPrivate()->GetName(); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [Probe]       [{}] uobj={} ({})\n"),
                                     j, eName.c_str(), eCls.c_str());
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
                                     e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7],
                                     e[8], e[9], e[10], e[11], e[12], e[13], e[14], e[15],
                                     e[16], e[17], e[18], e[19], e[20], e[21], e[22], e[23],
                                     e[24], e[25], e[26], e[27], e[28], e[29], e[30], e[31],
                                     e[32], e[33], e[34], e[35], e[36], e[37], e[38], e[39]);
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
                VLOG(STR("[MoriaCppMod] [Probe] --- {} actor: {} (class {}) ---\n"),
                     label, [&]{ try { return actor->GetName(); } catch (...) { return std::wstring(); } }().c_str(),
                     aCls.c_str());

                const wchar_t* arrPropNames[] = {
                    STR("BlueprintCreatedComponents"),
                    STR("InstanceComponents"),
                    STR("OwnedComponents"),
                };
                for (auto* arrName : arrPropNames)
                {
                    UClass* cls = nullptr;
                    try { cls = actor->GetClassPrivate(); } catch (...) {}
                    if (!cls) continue;
                    FProperty* arrProp = nullptr;
                    for (auto* strct = static_cast<UStruct*>(cls); strct && !arrProp;
                         strct = strct->GetSuperStruct())
                    {
                        for (auto* p : strct->ForEachProperty())
                        {
                            std::wstring pn;
                            try { pn = p->GetName(); } catch (...) {}
                            if (pn == arrName) { arrProp = p; break; }
                        }
                    }
                    if (!arrProp) continue;
                    uint8_t* slot = reinterpret_cast<uint8_t*>(actor) + arrProp->GetOffset_Internal();
                    UObject** data = *reinterpret_cast<UObject***>(slot + 0);
                    int32_t num    = *reinterpret_cast<int32_t*>(slot + 8);
                    VLOG(STR("[MoriaCppMod] [Probe]   {}: count={}\n"), arrName, num);
                    if (!data || num <= 0) continue;
                    for (int32_t i = 0; i < num && i < 64; ++i)
                    {
                        UObject* c = data[i];
                        if (!c || !isObjectAlive(c)) continue;
                        std::wstring cName, cCls;
                        try { cName = c->GetName(); } catch (...) {}
                        try { cCls = c->GetClassPrivate()->GetName(); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [Probe]     [{}] {} : {}\n"),
                             i, cName.c_str(), cCls.c_str());

                        // Deep-dump inventory/container-related components.
                        bool isInv = (cCls.find(STR("Inventory")) != std::wstring::npos)
                                  || (cCls.find(STR("Container")) != std::wstring::npos);
                        if (!isInv) continue;

                        VLOG(STR("[MoriaCppMod] [Probe]       --- {}: deep-dump ---\n"), cCls.c_str());
                        UClass* compCls = c->GetClassPrivate();
                        if (!compCls) continue;
                        for (auto* strct = static_cast<UStruct*>(compCls); strct;
                             strct = strct->GetSuperStruct())
                        {
                            std::wstring scopeCls;
                            try { scopeCls = strct->GetName(); } catch (...) {}
                            for (auto* prop : strct->ForEachProperty())
                            {
                                std::wstring pn, tn;
                                try { pn = prop->GetName(); } catch (...) {}
                                try { tn = prop->GetClass().GetName(); } catch (...) {}
                                unsigned off = (unsigned)prop->GetOffset_Internal();
                                uint8_t* cslot = reinterpret_cast<uint8_t*>(c) + off;

                                if (tn == STR("ArrayProperty"))
                                {
                                    auto* arrP = static_cast<RC::Unreal::FArrayProperty*>(prop);
                                    FProperty* inner = nullptr;
                                    try { inner = arrP->GetInner(); } catch (...) {}
                                    std::wstring innerTn;
                                    if (inner) { try { innerTn = inner->GetClass().GetName(); } catch (...) {} }
                                    int32_t* arrNum = reinterpret_cast<int32_t*>(cslot + 8);
                                    void** arrData = reinterpret_cast<void**>(cslot + 0);
                                    VLOG(STR("[MoriaCppMod] [Probe]         ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                                         scopeCls.c_str(), pn.c_str(), off,
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
                                            try { eName = e->GetName(); } catch (...) {}
                                            try { eCls = e->GetClassPrivate()->GetName(); } catch (...) {}
                                            VLOG(STR("[MoriaCppMod] [Probe]           [{}] {} ({})\n"),
                                                 j, eName.c_str(), eCls.c_str());
                                        }
                                    }
                                }
                                else if (tn == STR("BoolProperty"))
                                {
                                    uint8_t b = *cslot;
                                    VLOG(STR("[MoriaCppMod] [Probe]         BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"),
                                         scopeCls.c_str(), pn.c_str(), off, b);
                                }
                                else if (tn == STR("IntProperty"))
                                {
                                    int32_t v = *reinterpret_cast<int32_t*>(cslot);
                                    VLOG(STR("[MoriaCppMod] [Probe]         INT  [{}].{} off=0x{:X} val={}\n"),
                                         scopeCls.c_str(), pn.c_str(), off, v);
                                }
                                else if (tn == STR("ObjectProperty"))
                                {
                                    UObject* o = *reinterpret_cast<UObject**>(cslot);
                                    std::wstring oCls;
                                    if (o && (uintptr_t)o >= 0x10000 && isObjectAlive(o))
                                        try { oCls = o->GetClassPrivate()->GetName(); } catch (...) {}
                                    VLOG(STR("[MoriaCppMod] [Probe]         OBJ  [{}].{} off=0x{:X} ptr={:p} cls={}\n"),
                                         scopeCls.c_str(), pn.c_str(), off,
                                         (void*)o, oCls.empty() ? STR("<null>") : oCls.c_str());
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
                        if (d && isObjectAlive(d)) { dwarf = d; dwarfClsHit = cn; break; }
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
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit) && !hit.empty())
                    for (UObject* g : hit) if (g && isObjectAlive(g)) { goat = g; break; }
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
            UClass* itemCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, classPath);
            if (!itemCls)
            {
                VLOG(STR("[MoriaCppMod] [{}] not resident — force-loading via blocking loader\n"),
                     logTag);
                itemCls = goat_loadClassAssetBlocking(classPath);
            }
            if (!itemCls)
            {
                VLOG(STR("[MoriaCppMod] [{}] item class FAILED to load (path: {})\n"),
                     logTag, classPath);
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
                VLOG(STR("[MoriaCppMod] [{}] no local pawn — bail (load a world first)\n"),
                     logTag);
                return false;
            }
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp || !isObjectAlive(invComp))
            {
                VLOG(STR("[MoriaCppMod] [{}] player MorInventoryComponent not found — bail\n"),
                     logTag);
                return false;
            }
            UFunction* dsiFn = invComp->GetFunctionByNameInChain(STR("ServerDebugSetItem"));
            if (!dsiFn)
            {
                VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem not found on MorInventoryComponent — bail\n"),
                     logTag);
                return false;
            }
            VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem found on {} ({})\n"),
                 logTag,
                 [&]{ try { return invComp->GetName(); } catch (...) { return std::wstring(L"<?>"); } }().c_str(),
                 safeClassName(invComp).c_str());

            int sz = dsiFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            // Try both "Item" (CXXHeaderDump signature) and "ItemClass" (older guess).
            auto* pItem  = findParam(dsiFn, STR("Item"));
            if (!pItem) pItem = findParam(dsiFn, STR("ItemClass"));
            auto* pCount = findParam(dsiFn, STR("Count"));
            if (!pItem || !pCount)
            {
                VLOG(STR("[MoriaCppMod] [{}] expected parms missing (Item={:p} Count={:p}) — dumping all parms:\n"),
                     logTag, (void*)pItem, (void*)pCount);
                for (auto* p : dsiFn->ForEachProperty())
                {
                    std::wstring pn, pcn;
                    try { pn = p->GetName(); } catch (...) {}
                    try { pcn = p->GetClass().GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [{}]   parm {} : {} off=0x{:X}\n"),
                         logTag, pn.c_str(), pcn.c_str(), (unsigned)p->GetOffset_Internal());
                }
                return false;
            }
            *reinterpret_cast<UClass**>(buf.data() + pItem->GetOffset_Internal()) = itemCls;
            *reinterpret_cast<int32_t*>(buf.data() + pCount->GetOffset_Internal()) = 1;
            UObject* dsiCtx = invComp;  // RPC dispatched on the component itself

            if (!safeProcessEvent(dsiCtx, dsiFn, buf.data()))
            {
                VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem PE returned false\n"), logTag);
                return false;
            }
            VLOG(STR("[MoriaCppMod] [{}] ServerDebugSetItem dispatched\n"), logTag);
            return true;
        }

        void grantBellToPlayer()
        {
            if (grantPorterItemToPlayer(
                    STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatBell.BP_PorterGoatBell_C"),
                    STR("Bell")))
            {
                showOnScreen(L"Bell of the Goat granted!", 3.0f, 0.4f, 0.9f, 0.4f);
            }
            else
            {
                showOnScreen(L"Bell grant failed — see log", 2.5f, 0.9f, 0.4f, 0.4f);
            }
        }

        void grantSaddlebagsToPlayer()
        {
            if (grantPorterItemToPlayer(
                    STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatSaddlebags.BP_PorterGoatSaddlebags_C"),
                    STR("Saddlebags")))
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
                struct { FVec3f Ret{}; } gp{};
                if (safeProcessEvent(pawn, getLoc, &gp)) pLoc = gp.Ret;
            }
            if (auto* fwdFn = pawn->GetFunctionByNameInChain(STR("GetActorForwardVector")))
            {
                struct { FVec3f Ret{}; } gp{};
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
                    struct { FVec3f Ret{}; } gp{};
                    if (safeProcessEvent(g, getLoc, &gp)) gLoc = gp.Ret;
                }
                float dx = gLoc.X - pLoc.X;
                float dy = gLoc.Y - pLoc.Y;
                float dz = gLoc.Z - pLoc.Z;
                float distSq = dx*dx + dy*dy + dz*dz;
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
            VLOG(STR("[MoriaCppMod] [GoatMenu] E near goat ptr={:p} — vanilla menu pivot rc.65, custom UMG suppressed\n"),
                 (void*)nearestGoat);
            // [rc.65 RE-DISABLED 2026-05-21] Custom UMG menu confirmed UGLY +
            // non-functional in user test (no click dispatch, no scroll, no
            // native E prompt on approach). Pivoting back to vanilla menu via
            // repurposed Rescue+Details slots — rc.59 proved those two ALWAYS
            // show. Desktop Claude is re-enabling those Register flags and
            // changing labels to "Saddlebags" + "Follow / Stay". Custom UMG
            // showGoatMenu() call disabled here; revive only if vanilla menu
            // approach is later abandoned again.
            // showGoatMenu();
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
            return m_goatName + STR(" \x2014 ") + state;  // U+2014 em dash
        }

        // [rc.62 NEW 2026-05-21] E-key poll handler that fires tryOpenGoatMenu().
        //
        // Background: in rc.59 we proved Tobi's BP menu is now suppressed by PR
        // #4 (only Dismiss + Rename were ever visible; both Register flags
        // flipped False in the menu disable). With no BP menu rows, our existing
        // onInteractionPressPre PE pre-hook never fires (no row widget exists
        // for `OnPressInteract` to fire on), so the deferred submenu open path
        // doesn't trigger and the custom UMG menu stays closed.
        //
        // Fix: poll VK_E directly. tryOpenGoatMenu has its own 300-unit /
        // 70-degree facing gate so we don't fire when E targets a chest or
        // workstation. 500ms dedupe inside tryOpenGoatMenu prevents
        // re-open on held E.
        //
        // No-op when no goat is tracked (m_followGoats empty) — vanilla E
        // behavior runs unmolested.
        void pollGoatMenuKey()
        {
            // [rc.65] Vanilla menu pivot — body unused. Kept as a stub
            // because dllmain.cpp still wires this into the tick loop.
            return;
        }

        void showGoatMenu()
        {
            if (m_goatMenuVisible) return;
            if (!m_goatMenuWidget || !isObjectAlive(m_goatMenuWidget))
            {
                createGoatMenuWidget();
            }
            if (!m_goatMenuWidget || !isObjectAlive(m_goatMenuWidget))
            {
                VLOG(STR("[MoriaCppMod] [GoatMenu] widget creation failed — bail\n"));
                return;
            }
            // Refresh header status (name + current state).
            if (m_goatHeader && isObjectAlive(m_goatHeader))
                umgSetText(m_goatHeader, goatStatusText());
            // Add to viewport
            if (auto* addFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                std::vector<uint8_t> buf(addFn->GetParmsSize(), 0);
                if (auto* pZ = findParam(addFn, STR("ZOrder")))
                    *reinterpret_cast<int32_t*>(buf.data() + pZ->GetOffset_Internal()) = 100;
                safeProcessEvent(m_goatMenuWidget, addFn, buf.data());
            }
            // Center on screen: alignment pivot (0.5,0.5), position at viewport center.
            if (auto* alignFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport")))
            {
                std::vector<uint8_t> b(alignFn->GetParmsSize(), 0);
                if (auto* p = findParam(alignFn, STR("Alignment")))
                {
                    auto* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = 0.5f; xy[1] = 0.5f;
                }
                safeProcessEvent(m_goatMenuWidget, alignFn, b.data());
            }
            m_screen.refresh(findPlayerController());
            float cx = static_cast<float>(m_screen.viewW) * 0.5f;
            float cy = static_cast<float>(m_screen.viewH) * 0.5f;
            if (auto* posFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("SetPositionInViewport")))
            {
                std::vector<uint8_t> b(posFn->GetParmsSize(), 0);
                if (auto* p = findParam(posFn, STR("Position")))
                {
                    auto* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = cx; xy[1] = cy;
                }
                if (auto* p = findParam(posFn, STR("bRemoveDPIScale")))
                    *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = true;
                safeProcessEvent(m_goatMenuWidget, posFn, b.data());
            }
            // Show
            if (auto* visFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("SetVisibility")))
            {
                struct { uint8_t Vis{0}; } vp{};  // 0 = Visible
                safeProcessEvent(m_goatMenuWidget, visFn, &vp);
            }
            m_goatMenuVisible = true;
            VLOG(STR("[MoriaCppMod] [GoatMenu] menu shown (centered at {},{})\n"), cx, cy);
        }

        // [Phase 5] Custom modal disabled — pure vanilla-menu approach.
        void tickGoatModalDeferred()
        {
            m_goatModalPending = false;  // always clear; never open modal
            return;
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

        // Force vanilla proximity menu to stay hidden while submenu is up.
        // Multi-pronged: outer Visibility=Collapsed + inner Root collapse +
        // SetRenderOpacity(0) on outer (opacity is hard to tick-overwrite).
        void forceProximityMenuHidden()
        {
            // 2026-05-14 — was only hiding the cached m_proximityMenuAtOpen,
            // but the user reported "E Details" still showing at the bottom
            // of screen. Root cause: multiple UI_WBP_InteractionMenu_C
            // instances can be alive (one floating proximity, plus HUD
            // interaction-prompt instances tied to different actors). Find
            // ALL live instances and collapse every one that isn't our
            // submenu.
            // 2026-05-14 — reverted the recursive SetText("")+Collapse walk.
            // That walk destroyed the proximity menu's TextBlocks PERMANENTLY
            // (text="" couldn't be restored), so subsequent proximity shows
            // had no readable content. Now we only collapse the outer + root,
            // which restores cleanly when closeGoatSubmenu un-collapses. The
            // "E Details" prompt on our SUBMENU is handled separately in
            // OnShow PE-post for our submenu (see killSubmenuFooterPrompt).
            auto collapseOne = [](UObject* w) {
                if (!w || !isObjectAlive(w)) return;
                if (auto* fn = w->GetFunctionByNameInChain(STR("SetVisibility")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    if (auto* p = findParam(fn, STR("InVisibility")))
                        *reinterpret_cast<uint8_t*>(b.data() + p->GetOffset_Internal()) = 1;  // Collapsed
                    try { safeProcessEvent(w, fn, b.data()); } catch (...) {}
                }
                if (auto* fn = w->GetFunctionByNameInChain(STR("SetRenderOpacity")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    if (auto* p = findParam(fn, STR("InOpacity")))
                        *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 0.0f;
                    try { safeProcessEvent(w, fn, b.data()); } catch (...) {}
                }
                // Inner CanvasPanel via WidgetTree.RootWidget — collapse only,
                // do NOT touch leaf TextBlocks (their text gets lost).
                if (auto* wtPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree")))
                {
                    if (UObject* wt = *wtPtr; wt && isObjectAlive(wt))
                    {
                        if (auto* rootPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget")))
                        {
                            if (UObject* root = *rootPtr; root && isObjectAlive(root))
                            {
                                if (auto* fn = root->GetFunctionByNameInChain(STR("SetVisibility")))
                                {
                                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                                    if (auto* p = findParam(fn, STR("InVisibility")))
                                        *reinterpret_cast<uint8_t*>(b.data() + p->GetOffset_Internal()) = 1;
                                    try { safeProcessEvent(root, fn, b.data()); } catch (...) {}
                                }
                            }
                        }
                    }
                }
            };

            // 2026-05-14 — REMOVED the findAllOfSafe(UI_WBP_InteractionMenu_C)
            // sweep. It collapsed EVERY interaction menu instance every tick
            // (chest interactions, other NPC menus), and closeGoatSubmenu only
            // un-collapses the one we cached — so chest interactions stayed
            // broken until next game launch. Now we only collapse the cached
            // proximity menu, which is what we actually intended.
            if (UObject* cached = m_proximityMenuAtOpen)
            {
                if (cached != m_goatSubMenu) collapseOne(cached);
            }
        }

        // Tick handler: if pending, open submenu; if visible, poll input.
        void tickGoatSubmenu()
        {
            // [rc.65] Custom UMG submenu abandoned. Vanilla menu handles all
            // goat interaction. Kept as a stub because dllmain.cpp still
            // wires this into the tick loop.
            return;
        }

        // Called from PE-pre hook in dllmain.cpp on OnSelectionNext/Previous
        // when vanilla's input system routes mouse-wheel / gamepad bumpers
        // / D-pad to our submenu instance. Advances our cursor and updates
        // the visual SetIsSelected state on old/new rows.
        void onGoatSubmenuScroll(int direction)
        {
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] scroll-handler entered dir={} visible={}\n"),
                 direction, m_goatSubMenuVisible);
            if (!m_goatSubMenuVisible) return;
            static ULONGLONG s_lastScrollMs = 0;
            ULONGLONG now = GetTickCount64();
            if (now - s_lastScrollMs < 100)
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] scroll throttled (delta={}ms)\n"),
                     (int)(now - s_lastScrollMs));
                return;
            }
            s_lastScrollMs = now;
            int oldCursor = m_goatSubMenuCursorIdx;
            int newCursor = (oldCursor + direction + 6) % 6;
            if (newCursor == oldCursor) return;
            goatRowSetSelected(m_goatSubMenuRows[oldCursor], false);
            goatRowSetSelected(m_goatSubMenuRows[newCursor], true);
            m_goatSubMenuCursorIdx = newCursor;
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] scroll dir={} cursor {} -> {}\n"),
                 direction, oldCursor, newCursor);
        }

        void dispatchGoatSubmenuAction(int idx)
        {
            // 6 actions: Follow / Stay / Saddlebags / Feed / Rename / Dismiss.
            // Rename opens its own popup — flag is set inside it.
            switch (idx)
            {
                case 0: onGoatFollow();           break;
                case 1: onGoatStay();             break;
                case 2: onGoatAccessSaddlebags(); break;
                case 3: onGoatFeed();             break;
                case 4: onGoatRename();           break;
                case 5: onGoatDismiss();          break;
            }
            closeGoatSubmenu();
        }

        // Spawn UI_WBP_InteractionMenu_C, populate with 7 cloned rows from
        // the cached Details template, center on screen, switch input mode.
        void openGoatSubmenu()
        {
            // Re-entry guard — refuse second spawn if a submenu is already up.
            if (m_goatSubMenu && isObjectAlive(m_goatSubMenu))
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] open already in flight — skip\n"));
                return;
            }
            UObject* tmpl = m_goatSubMenuTemplateRow;
            if (!tmpl || !isObjectAlive(tmpl))
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] template row not alive — abort\n"));
                return;
            }
            // Cache proximity menu pointer NOW (before our submenu opens
            // and risks updating m_pendingInteractMenu via OnShow hook).
            m_proximityMenuAtOpen = m_pendingInteractMenu.Get();
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] cached proximity menu={:p}\n"),
                 (void*)m_proximityMenuAtOpen);
            UClass* rowCls = nullptr;
            try { rowCls = tmpl->GetClassPrivate(); } catch (...) {}
            if (!rowCls) return;

            // Find the menu class. Use the class of the proximity menu
            // we already cached (m_pendingInteractMenu) since it's the
            // same class.
            UClass* menuCls = nullptr;
            UObject* proxMenu = m_pendingInteractMenu.Get();
            if (proxMenu && isObjectAlive(proxMenu))
            {
                try { menuCls = proxMenu->GetClassPrivate(); } catch (...) {}
            }
            if (!menuCls)
            {
                // Fallback: search by path.
                menuCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr,
                    STR("/Game/UI/HUD/InteractionMenu/UI_WBP_InteractionMenu.UI_WBP_InteractionMenu_C"));
            }
            if (!menuCls)
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] menu class not found — abort\n"));
                return;
            }

            UObject* menu = jw_createGameWidget(menuCls);
            if (!menu)
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] CreateWidget(menu) null — abort\n"));
                return;
            }
            m_goatSubMenu = menu;

            // AddToViewport, then center via SetAlignment + SetPosition.
            if (auto* addFn = menu->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                std::vector<uint8_t> b(addFn->GetParmsSize(), 0);
                if (auto* p = findParam(addFn, STR("ZOrder")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 110;
                try { safeProcessEvent(menu, addFn, b.data()); } catch (...) {}
            }
            if (auto* fn = menu->GetFunctionByNameInChain(STR("SetAlignmentInViewport")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("Alignment")))
                {
                    auto* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = 0.5f; xy[1] = 0.5f;
                }
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) {}
            }
            m_screen.refresh(findPlayerController());
            float cx = static_cast<float>(m_screen.viewW) * 0.5f;
            float cy = static_cast<float>(m_screen.viewH) * 0.5f;
            if (auto* fn = menu->GetFunctionByNameInChain(STR("SetPositionInViewport")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("Position")))
                {
                    auto* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = cx; xy[1] = cy;
                }
                if (auto* p = findParam(fn, STR("bRemoveDPIScale")))
                    *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = true;
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) {}
            }

            // Set header text to "Porter Goat" (or user-set goat name).
            const wchar_t* header = m_goatName.empty() ? STR("Porter Goat") : m_goatName.c_str();
            overrideMenuNameText(menu, header);

            // Get the menu's row container.
            UObject* container = nullptr;
            if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    container = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
            }
            if (!container || !isObjectAlive(container))
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] no container — abort\n"));
                closeGoatSubmenu();
                return;
            }

            // Clear any pre-existing rows in the container before adding ours.
            // Try ClearChildren first; if it doesn't exist or doesn't take,
            // fall back to RemoveChildAt loop.
            int preCount = 0;
            if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                try { safeProcessEvent(container, fn, b.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    preCount = *reinterpret_cast<int32_t*>(b.data() + pRet->GetOffset_Internal());
            }
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] pre-clear: container has {} children\n"), preCount);

            if (auto* fn = container->GetFunctionByNameInChain(STR("ClearChildren")))
            {
                try { safeProcessEvent(container, fn, nullptr); } catch (...) {}
            }

            // Verify + fallback: walk children and RemoveChildAt(0) until empty.
            int safety = 32;
            while (safety-- > 0)
            {
                int n = 0;
                if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    try { safeProcessEvent(container, fn, b.data()); } catch (...) {}
                    if (auto* pRet = findParam(fn, STR("ReturnValue")))
                        n = *reinterpret_cast<int32_t*>(b.data() + pRet->GetOffset_Internal());
                }
                if (n <= 0) break;
                if (auto* fn = container->GetFunctionByNameInChain(STR("RemoveChildAt")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    if (auto* p = findParam(fn, STR("Index")))
                        *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 0;
                    try { safeProcessEvent(container, fn, b.data()); } catch (...) { break; }
                }
                else break;
            }
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] container cleared (safety left={})\n"), safety);

            // 6 rows total: Follow / Stay / Saddlebags / Feed / Rename / Dismiss.
            // (v1.4.0 changed the saddlebag from a container to an EpicPack
            // equipped on the player's back, but the user wanted the
            // submenu shortcut kept — opens player inventory as convenience.)
            static const wchar_t* kLabels[6] = {
                STR("Follow"), STR("Stay"), STR("Saddlebags"),
                STR("Feed"), STR("Rename"), STR("Dismiss"),
            };
            auto* tVisPtr = tmpl->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
            uint8_t tVis = tVisPtr ? *tVisPtr : 0;

            for (int i = 0; i < 6; ++i)
            {
                UObject* row = jw_createGameWidget(rowCls);
                if (!row)
                {
                    VLOG(STR("[MoriaCppMod] [GoatSubmenu] row[{}] create null\n"), i);
                    continue;
                }
                // Copy Interactable / Interactor / InteractComponent from
                // template (reflective — survives FGK widget layout shifts).
                copyRowPropertyByName(row, tmpl, STR("Interactable"));
                copyRowPropertyByName(row, tmpl, STR("Interactor"));
                copyRowPropertyByName(row, tmpl, STR("InteractComponent"));

                // AddChild.
                addToVBox(container, row);

                // Match template visibility.
                if (auto* nVisPtr = row->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                    *nVisPtr = tVis;

                // OnUnpool + OnSetInteraction.
                if (auto* fn = row->GetFunctionByNameInChain(STR("OnUnpool")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    try { safeProcessEvent(row, fn, b.data()); } catch (...) {}
                }
                if (auto* fn = row->GetFunctionByNameInChain(STR("OnSetInteraction")))
                {
                    auto* pTxt  = findParam(fn, STR("FormattedText"));
                    auto* pBool = findParam(fn, STR("bCanDoInteraction"));
                    if (pTxt && pBool)
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        FText txt(kLabels[i]);
                        std::memcpy(b.data() + pTxt->GetOffset_Internal(), &txt, sizeof(FText));
                        *reinterpret_cast<bool*>(b.data() + pBool->GetOffset_Internal()) = true;
                        try { safeProcessEvent(row, fn, b.data()); } catch (...) {}
                    }
                }
                // Initialize as unselected.
                goatRowSetSelected(row, false);
                m_goatSubMenuRows[i] = row;
            }

            // Post-populate trim: walk the container's children. For each,
            // check if it's one of our injected rows. If NOT, remove it.
            // This catches BP-added default rows wherever they land (top,
            // bottom, mid-list).
            auto* getCountFn = container->GetFunctionByNameInChain(STR("GetChildrenCount"));
            auto* getChildFn = container->GetFunctionByNameInChain(STR("GetChildAt"));
            auto* removeAtFn = container->GetFunctionByNameInChain(STR("RemoveChildAt"));
            auto getCount = [&]() -> int {
                if (!getCountFn) return 0;
                std::vector<uint8_t> b(getCountFn->GetParmsSize(), 0);
                try { safeProcessEvent(container, getCountFn, b.data()); } catch (...) { return 0; }
                if (auto* p = findParam(getCountFn, STR("ReturnValue")))
                    return *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal());
                return 0;
            };
            auto getChild = [&](int idx) -> UObject* {
                if (!getChildFn) return nullptr;
                std::vector<uint8_t> b(getChildFn->GetParmsSize(), 0);
                if (auto* p = findParam(getChildFn, STR("Index")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = idx;
                try { safeProcessEvent(container, getChildFn, b.data()); } catch (...) { return nullptr; }
                if (auto* pRet = findParam(getChildFn, STR("ReturnValue")))
                    return *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
                return nullptr;
            };
            auto isOurRow = [&](UObject* w) {
                for (int i = 0; i < 6; ++i)
                    if (m_goatSubMenuRows[i] == w) return true;
                return false;
            };
            int trimSafety = 32;
            while (trimSafety-- > 0)
            {
                int n = getCount();
                int strangerIdx = -1;
                for (int i = 0; i < n; ++i)
                {
                    UObject* c = getChild(i);
                    if (c && !isOurRow(c)) { strangerIdx = i; break; }
                }
                if (strangerIdx < 0) break;
                if (!removeAtFn) break;
                std::vector<uint8_t> b(removeAtFn->GetParmsSize(), 0);
                if (auto* p = findParam(removeAtFn, STR("Index")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = strangerIdx;
                try { safeProcessEvent(container, removeAtFn, b.data()); } catch (...) { break; }
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] removed stranger row at idx {}\n"), strangerIdx);
            }
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] post-populate trim: final count = {}\n"), getCount());

            // Select first row as initial cursor.
            m_goatSubMenuCursorIdx = 0;
            goatRowSetSelected(m_goatSubMenuRows[0], true);

            // Trigger the menu's OnShow event so its BP graph wires up
            // input listeners (ListenForInputAction for scroll wheel etc.).
            // Without this, our submenu may not receive scroll events.
            if (auto* fn = menu->GetFunctionByNameInChain(STR("OnShow")))
            {
                try { safeProcessEvent(menu, fn, nullptr); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] OnShow UFunc fired on submenu\n"));
            }

            // Hide the proximity menu. OnHide triggers vanilla's slide-out;
            // then forceProximityMenuHidden collapses outer + inner widget
            // tree. tickGoatSubmenu re-asserts this every frame.
            if (UObject* prox = m_proximityMenuAtOpen)
            {
                if (isObjectAlive(prox))
                {
                    if (auto* fn = prox->GetFunctionByNameInChain(STR("OnHide")))
                    {
                        try { safeProcessEvent(prox, fn, nullptr); } catch (...) {}
                    }
                }
            }
            forceProximityMenuHidden();

            // Switch input to UI so mouse cursor works.
            setInputModeUI(menu);

            m_goatSubMenuVisible = true;
            VLOG(STR("[MoriaCppMod] [GoatSubmenu] opened menu={:p} container={:p}, 6 rows populated, cursor=0 (proximity menu hidden)\n"),
                 (void*)menu, (void*)container);
        }

        void closeGoatSubmenu()
        {
            if (!m_goatSubMenuVisible && !m_goatSubMenu) return;
            if (m_goatSubMenu && isObjectAlive(m_goatSubMenu))
            {
                // Remove rows first (clean up children).
                for (int i = 0; i < 6; ++i)
                {
                    UObject* row = m_goatSubMenuRows[i];
                    if (row && isObjectAlive(row))
                    {
                        if (auto* fn = row->GetFunctionByNameInChain(STR("RemoveFromParent")))
                        {
                            try { safeProcessEvent(row, fn, nullptr); } catch (...) {}
                        }
                    }
                    m_goatSubMenuRows[i] = nullptr;
                }
                // Remove menu.
                if (auto* fn = m_goatSubMenu->GetFunctionByNameInChain(STR("RemoveFromParent")))
                {
                    try { safeProcessEvent(m_goatSubMenu, fn, nullptr); } catch (...) {}
                }
            }
            m_goatSubMenu = nullptr;
            m_goatSubMenuVisible = false;
            m_goatSubMenuPending = false;
            m_goatSubMenuTemplateRow = nullptr;
            m_goatSubMenuCursorIdx = 0;

            // Restore proximity menu visibility — outer + inner — so it
            // reappears next time vanilla refreshes its proximity check.
            if (UObject* prox = m_proximityMenuAtOpen)
            {
                if (isObjectAlive(prox))
                {
                    auto setVisVisible = [](UObject* w) {
                        if (!w || !isObjectAlive(w)) return;
                        if (auto* fn = w->GetFunctionByNameInChain(STR("SetVisibility")))
                        {
                            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                            if (auto* p = findParam(fn, STR("InVisibility")))
                                *reinterpret_cast<uint8_t*>(b.data() + p->GetOffset_Internal()) = 0;  // Visible
                            try { safeProcessEvent(w, fn, b.data()); } catch (...) {}
                        }
                    };
                    setVisVisible(prox);
                    if (auto* fn = prox->GetFunctionByNameInChain(STR("SetRenderOpacity")))
                    {
                        std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                        if (auto* p = findParam(fn, STR("InOpacity")))
                            *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 1.0f;
                        try { safeProcessEvent(prox, fn, b.data()); } catch (...) {}
                    }
                    if (auto* wtPtr = prox->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree")))
                    {
                        if (UObject* wt = *wtPtr; wt && isObjectAlive(wt))
                        {
                            if (auto* rootPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget")))
                                setVisVisible(*rootPtr);
                        }
                    }
                }
            }
            m_proximityMenuAtOpen = nullptr;

            // Skip the input-mode revert when a handler (Rename / Saddlebags)
            // opened its own UI that needs to keep UI input mode.
            if (!m_keepUIModeAfterSubmenuClose)
            {
                setInputModeGame();
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] closed + input restored\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatSubmenu] closed — UI input mode preserved for handler popup\n"));
            }
            m_keepUIModeAfterSubmenuClose = false;
        }

        // Click dispatch — polls IsHovered + LMB-release on each button while
        // the menu is visible. Same pattern as moria_session_history row clicks.
        void tickGoatMenu()
        {
            if (!m_goatMenuVisible) return;
            static bool s_lastLMB = false;
            static int  s_lastHovered = -1; // 0=Stay 1=Follow 2=Dismiss 3=Inv

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

            // ESC closes the modal.
            static bool s_lastEsc = false;
            bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (escDown && !s_lastEsc)
            {
                s_lastEsc = escDown;
                VLOG(STR("[MoriaCppMod] [GoatMenu] ESC pressed — closing modal\n"));
                closeGoatMenu();
                return;
            }
            s_lastEsc = escDown;

            UObject* btns[7] = {
                m_goatBtnFollow, m_goatBtnStay, m_goatBtnWander,
                m_goatBtnInv, m_goatBtnFeed, m_goatBtnRename, m_goatBtnDismiss
            };
            for (int i = 0; i < 6; ++i)
            {
                if (callIsHovered(btns[i])) { s_lastHovered = i; break; }
            }

            bool nowDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            bool released = (s_lastLMB && !nowDown);
            s_lastLMB = nowDown;
            if (!released || s_lastHovered < 0) return;

            int hit = s_lastHovered;
            s_lastHovered = -1;
            VLOG(STR("[MoriaCppMod] [GoatMenu] click hit={}\n"), hit);
            switch (hit)
            {
                case 0: onGoatFollow(); break;
                case 1: onGoatStay(); break;
                case 2: onGoatWander(); break;
                case 3: onGoatAccessSaddlebags(); break;
                case 4: onGoatFeed(); break;
                case 5: onGoatRename(); break;
                case 6: onGoatDismiss(); break;
            }
        }

        void closeGoatMenu()
        {
            if (!m_goatMenuVisible) return;
            if (m_goatMenuWidget && isObjectAlive(m_goatMenuWidget))
            {
                if (auto* visFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("SetVisibility")))
                {
                    struct { uint8_t Vis{1}; } vp{};  // 1 = Collapsed
                    safeProcessEvent(m_goatMenuWidget, visFn, &vp);
                }
                if (auto* remFn = m_goatMenuWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
                {
                    safeProcessEvent(m_goatMenuWidget, remFn, nullptr);
                }
            }
            // [Phase 4] null the widget + all button pointers so the next
            // showGoatMenu rebuilds from scratch. The engine may GC the
            // widget after RemoveFromParent — reusing a dangling pointer
            // crashed on second E-press.
            m_goatMenuWidget = nullptr;
            m_goatBtnFollow  = nullptr;
            m_goatBtnStay    = nullptr;
            m_goatBtnWander  = nullptr;
            m_goatBtnInv     = nullptr;
            m_goatBtnFeed    = nullptr;
            m_goatBtnRename  = nullptr;
            m_goatBtnDismiss = nullptr;
            m_goatHeader     = nullptr;
            m_goatMenuVisible = false;
            m_lastGoatMenuMs = GetTickCount64();
            VLOG(STR("[MoriaCppMod] [GoatMenu] menu closed (widget nulled, will rebuild next open)\n"));
        }

        void createGoatMenuWidget()
        {
            UObject* pc = m_localPC && isObjectAlive(m_localPC) ? m_localPC : nullptr;
            if (!pc) return;
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* btnClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.Button"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!userWidgetClass || !vboxClass || !textClass || !btnClass ||
                !borderClass || !sizeBoxClass || !createFn || !wblClass)
            {
                VLOG(STR("[MoriaCppMod] [GoatMenu] missing UMG classes\n"));
                return;
            }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { VLOG(STR("[MoriaCppMod] [GoatMenu] CreateWidget null\n")); return; }
            m_goatMenuWidget = userWidget;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Layout: SizeBox(420 wide) → Border(dark bg) → VerticalBox(padded)
            // The UserWidget is then centered via SetAlignmentInViewport +
            // SetPositionInViewport in showGoatMenu.

            FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
            UObject* sizeBox = UObjectGlobals::StaticConstructObject(sbP);
            if (!sizeBox) return;
            if (widgetTree) setRootWidget(widgetTree, sizeBox);

            if (auto* fn = sizeBox->GetFunctionByNameInChain(STR("SetWidthOverride")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("InWidthOverride")))
                    *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 420.0f;
                else if (auto* p = findParam(fn, STR("InSize")))
                    *reinterpret_cast<float*>(b.data() + p->GetOffset_Internal()) = 420.0f;
                safeProcessEvent(sizeBox, fn, b.data());
            }

            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* border = UObjectGlobals::StaticConstructObject(borderP);
            if (!border) return;
            if (auto* fn = border->GetFunctionByNameInChain(STR("SetBrushColor")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("InBrushColor")))
                {
                    auto* c = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    c[0] = 0.05f; c[1] = 0.05f; c[2] = 0.07f; c[3] = 0.92f;
                }
                safeProcessEvent(border, fn, b.data());
            }
            // SizeBox.SetContent(border)
            if (auto* fn = sizeBox->GetFunctionByNameInChain(STR("SetContent")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("Content")))
                    *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = border;
                safeProcessEvent(sizeBox, fn, b.data());
            }

            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;
            if (auto* fn = border->GetFunctionByNameInChain(STR("SetContent")))
            {
                std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("Content")))
                    *reinterpret_cast<UObject**>(b.data() + p->GetOffset_Internal()) = vbox;
                safeProcessEvent(border, fn, b.data());
            }

            // Header text — name + " — " + state. Updated per show.
            FStaticConstructObjectParameters textP(textClass, outer);
            UObject* header = UObjectGlobals::StaticConstructObject(textP);
            if (header)
            {
                umgSetText(header, goatStatusText());
                umgSetTextColor(header, 1.0f, 0.92f, 0.6f, 1.0f);
                umgSetFontSize(header, 22);
                UObject* slot = addToVBox(vbox, header);
                if (slot)
                {
                    umgSetSlotPadding(slot, 16, 16, 16, 12);
                    if (auto* fnH = slot->GetFunctionByNameInChain(STR("SetHorizontalAlignment")))
                    { std::vector<uint8_t> bb(fnH->GetParmsSize(), 0); bb[0] = 2; safeProcessEvent(slot, fnH, bb.data()); }
                }
                m_goatHeader = header;
            }

            // Button factory — creates a UButton with a TextBlock child.
            auto makeButton = [&](const wchar_t* label) -> UObject* {
                FStaticConstructObjectParameters btnP(btnClass, outer);
                UObject* btn = UObjectGlobals::StaticConstructObject(btnP);
                if (!btn) return nullptr;
                FStaticConstructObjectParameters labelP(textClass, outer);
                UObject* labelTxt = UObjectGlobals::StaticConstructObject(labelP);
                if (labelTxt)
                {
                    umgSetText(labelTxt, std::wstring(label));
                    umgSetTextColor(labelTxt, 1.0f, 1.0f, 1.0f, 1.0f);
                    umgSetFontSize(labelTxt, 16);
                    // UButton is a ContentWidget — use SetContent, not AddChild.
                    if (auto* setContentFn = btn->GetFunctionByNameInChain(STR("SetContent")))
                    {
                        int sz = setContentFn->GetParmsSize();
                        std::vector<uint8_t> ab(sz, 0);
                        auto* pCh = findParam(setContentFn, STR("Content"));
                        if (pCh)
                        {
                            *reinterpret_cast<UObject**>(ab.data() + pCh->GetOffset_Internal()) = labelTxt;
                            safeProcessEvent(btn, setContentFn, ab.data());
                        }
                    }
                }
                UObject* slot = addToVBox(vbox, btn);
                if (slot)
                {
                    umgSetSlotPadding(slot, 16, 4, 16, 4);
                    if (auto* fnH = slot->GetFunctionByNameInChain(STR("SetHorizontalAlignment")))
                    { std::vector<uint8_t> bb(fnH->GetParmsSize(), 0); bb[0] = 3; safeProcessEvent(slot, fnH, bb.data()); }
                }
                return btn;
            };

            UObject* btnFollow   = makeButton(STR("Follow"));
            UObject* btnStay     = makeButton(STR("Stay"));
            UObject* btnWander   = makeButton(STR("Wander"));
            UObject* btnInv      = makeButton(STR("Saddlebags"));
            UObject* btnFeed     = makeButton(STR("Feed"));
            UObject* btnRename   = makeButton(STR("Rename"));
            UObject* btnDismiss  = makeButton(STR("Dismiss"));

            m_goatBtnFollow  = btnFollow;
            m_goatBtnStay    = btnStay;
            m_goatBtnWander  = btnWander;
            m_goatBtnInv     = btnInv;
            m_goatBtnFeed    = btnFeed;
            m_goatBtnRename  = btnRename;
            m_goatBtnDismiss = btnDismiss;

            VLOG(STR("[MoriaCppMod] [GoatMenu] widget built — header + 7 buttons (fol={:p} sta={:p} wan={:p} inv={:p} fee={:p} ren={:p} dis={:p})\n"),
                 (void*)btnFollow, (void*)btnStay, (void*)btnWander,
                 (void*)btnInv, (void*)btnFeed, (void*)btnRename, (void*)btnDismiss);
        }

        // Button action handlers — invoked from the PE-pre click filter
        // (onInteractionPressPre) when the user E-presses one of our
        // injected rows in the vanilla proximity menu.

        void onGoatFollow()
        {
            for (auto& rec : m_followGoats) rec.stayMode = false;
            if (UObject* nc = findGoatNpcComp()) setGoatRole(nc, STR("Porter"));
            VLOG(STR("[MoriaCppMod] [GoatMenu] FOLLOW — stayMode=false + SetRole(Porter)\n"));
            showOnScreen(L"Porter Goat: following", 1.5f, 0.7f, 0.9f, 0.7f);
            clearGoatInjectedRows();
        }

        void onGoatStay()
        {
            for (auto& rec : m_followGoats) rec.stayMode = true;
            // Stay = Wanderer role + immediate stop. The goat will idle once
            // its current MoveTo finishes; we don't issue further MoveToActor
            // because stayMode gates the tick.
            if (UObject* nc = findGoatNpcComp()) setGoatRole(nc, STR("Wanderer"));
            // Try to stop current movement on each goat's controller.
            for (auto& rec : m_followGoats)
            {
                UObject* goat = rec.pawn.Get();
                if (!goat || !isObjectAlive(goat)) continue;
                if (auto* getCtrlFn = goat->GetFunctionByNameInChain(STR("K2_GetController")))
                {
                    std::vector<uint8_t> b(getCtrlFn->GetParmsSize(), 0);
                    try { safeProcessEvent(goat, getCtrlFn, b.data()); } catch (...) { continue; }
                    auto* pRet = findParam(getCtrlFn, STR("ReturnValue"));
                    if (!pRet) continue;
                    UObject* ctrl = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
                    if (!ctrl || !isObjectAlive(ctrl)) continue;
                    if (auto* stopFn = ctrl->GetFunctionByNameInChain(STR("StopMovement")))
                    {
                        try { safeProcessEvent(ctrl, stopFn, nullptr); } catch (...) {}
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatMenu] STAY — stayMode=true + SetRole(Wanderer) + StopMovement\n"));
            showOnScreen(L"Porter Goat: staying", 1.5f, 0.7f, 0.9f, 0.7f);
            clearGoatInjectedRows();
        }

        void onGoatWander()
        {
            // Per user spec: Wander = SetRole(Wanderer) + stayMode=false.
            // stayMode=false means tickFollowGoats still issues MoveToActor
            // (goat moves toward player) AND vanilla BT runs the Wanderer
            // role — goat actively wanders nearby.
            for (auto& rec : m_followGoats) rec.stayMode = false;
            if (UObject* nc = findGoatNpcComp()) setGoatRole(nc, STR("Wanderer"));
            VLOG(STR("[MoriaCppMod] [GoatMenu] WANDER — stayMode=false + SetRole(Wanderer)\n"));
            showOnScreen(L"Porter Goat: wandering", 1.5f, 0.7f, 0.9f, 0.7f);
            clearGoatInjectedRows();
        }

        void onGoatDismiss()
        {
            VLOG(STR("[MoriaCppMod] [GoatMenu] DISMISS\n"));
            clearGoatInjectedRows();
            despawnAllFollowGoats();
            showOnScreen(L"Goat dismissed", 2.0f, 0.7f, 0.7f, 0.7f);
        }
        // Read Windows clipboard as UTF-16 wide string. Returns empty on
        // failure or non-text clipboard contents.
        std::wstring readClipboardText()
        {
            std::wstring out;
            if (!OpenClipboard(nullptr)) return out;
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h)
            {
                if (auto* p = static_cast<wchar_t*>(GlobalLock(h)))
                {
                    out = p;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            // Trim trailing CR/LF/spaces.
            while (!out.empty() &&
                   (out.back() == L'\r' || out.back() == L'\n' ||
                    out.back() == L' '  || out.back() == L'\t'))
                out.pop_back();
            // Cap length so a paste-bomb can't ruin the header.
            if (out.size() > 32) out.resize(32);
            return out;
        }

        void onGoatFeed()
        {
            VLOG(STR("[MoriaCppMod] [GoatMenu] FEED entered\n"));
            UObject* pawn = m_localPawn;
            if (!pawn) { closeGoatMenu(); return; }
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp) { closeGoatMenu(); return; }
            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp) { closeGoatMenu(); return; }
            uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16)) { closeGoatMenu(); return; }
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
            int stride  = iiSize();
            int itemOff = iiItemOff();
            UClass* foundCls = nullptr;
            for (int i = 0; i < arrNum; ++i)
            {
                uint8_t* entry = arrData + i * stride;
                UClass* cls = *reinterpret_cast<UClass**>(entry + itemOff);
                if (!cls || !isObjectAlive(cls)) continue;
                std::wstring n;
                try { n = cls->GetName(); } catch (...) { continue; }
                // Case-insensitive substring match — works for
                // BP_Cabbage_C, BP_Item_Cabbage_C, anything with "Cabbage".
                std::wstring lc = n;
                for (auto& ch : lc) ch = std::towlower(ch);
                if (lc.find(STR("cabbage")) != std::wstring::npos)
                {
                    foundCls = cls;
                    break;
                }
            }
            if (!foundCls)
            {
                showOnScreen(L"Porter Goat: no cabbage", 5.0f, 0.95f, 0.7f, 0.4f);
                VLOG(STR("[MoriaCppMod] [GoatMenu] FEED — no cabbage in player inventory\n"));
                return;
            }
            auto* removeFn = invComp->GetFunctionByNameInChain(STR("RemoveItem"));
            if (!removeFn)
            {
                showOnScreen(L"RemoveItem not found", 2.0f, 0.9f, 0.4f, 0.4f);
                closeGoatMenu();
                return;
            }
            int sz = removeFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            if (auto* p = findParam(removeFn, STR("Item")))
                *reinterpret_cast<UClass**>(buf.data() + p->GetOffset_Internal()) = foundCls;
            if (auto* p = findParam(removeFn, STR("Count")))
                *reinterpret_cast<int32_t*>(buf.data() + p->GetOffset_Internal()) = 1;
            if (auto* p = findParam(removeFn, STR("From")))
                *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = 0;
            safeProcessEvent(invComp, removeFn, buf.data());
            showOnScreen(L"Porter Goat: fed (1 cabbage)", 5.0f, 0.7f, 0.9f, 0.7f);
            VLOG(STR("[MoriaCppMod] [GoatMenu] FED — RemoveItem({}, 1) fired\n"),
                 foundCls->GetName());
            // Goat sound TODO: play AkComponent event ("Goat_Vocalize" or
            // similar) on the goat actor's SoundComp. Requires asset-name
            // discovery first; sound system has Ak event names like
            // "GOAT_BLEAT" but exact tag unknown. Punt for v1.
        }

        void onGoatRename()
        {
            // Open the existing in-game rename modal (GenericPopup_C chrome
            // + EditableTextBox) used elsewhere for character/world rename.
            // Set the m_renamingGoat flag so confirmRenameDialog routes the
            // confirmed name to m_goatName (not the character-rename flow).
            // Keep UI input mode after closeGoatSubmenu so the rename modal's
            // EditableTextBox keeps keyboard focus.
            m_keepUIModeAfterSubmenuClose = true;
            m_renamingGoat = true;
            showRenameDialog_v2();
            VLOG(STR("[MoriaCppMod] [GoatMenu] RENAME — opened rename modal (m_renamingGoat=true)\n"));
        }

        void onGoatAccessSaddlebags()
        {
            // 2026-05-14 v1.3.10 RETRY → STILL HANGS. Even with the new
            // Item.ContainerItem tag on the saddlebag, ServerUse(ID) from
            // here doesn't surface the inventory grid — and combined with UI
            // mode preservation the user gets locked. Reverted to the safe
            // stub. Workaround: per the v1.3.10 brief, the tag change should
            // make direct-click in player inventory open the 8×8 grid; tell
            // the user to use that path until we wire UMorInventoryScreen
            // directly.
            VLOG(STR("[MoriaCppMod] [GoatMenu] SADDLEBAGS — v1.4.0: bag is EpicPack on player back; open inventory (I) to access\n"));
            showOnScreen(L"Saddlebag is on your back — open inventory (I) to use", 5.0f, 0.95f, 0.85f, 0.4f);
            return;
            // Scaffolding kept for future Path B (spawn UMorInventoryScreen).
            #if 0
            m_keepUIModeAfterSubmenuClose = true;
            UObject* pawn = m_localPawn;
            if (!pawn) { closeGoatMenu(); return; }
            UObject* invComp = findPlayerInventoryComponent(pawn);
            if (!invComp) { closeGoatMenu(); return; }
            FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp) { closeGoatMenu(); return; }
            uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16)) { closeGoatMenu(); return; }
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
            int stride  = iiSize();
            int itemOff = iiItemOff();
            int idOff   = iiIDOff();
            int32_t saddlebagsID = 0;
            for (int i = 0; i < arrNum; ++i)
            {
                uint8_t* entry = arrData + i * stride;
                UClass* cls = *reinterpret_cast<UClass**>(entry + itemOff);
                if (cls && isObjectAlive(cls))
                {
                    std::wstring n;
                    try { n = cls->GetName(); } catch (...) {}
                    if (n == STR("BP_PorterGoatSaddlebags_C"))
                    {
                        saddlebagsID = *reinterpret_cast<int32_t*>(entry + idOff);
                        break;
                    }
                }
            }
            if (saddlebagsID == 0)
            {
                showOnScreen(L"No saddlebags in inventory", 2.0f, 0.9f, 0.7f, 0.4f);
                closeGoatMenu();
                return;
            }
            auto* useFn = invComp->GetFunctionByNameInChain(STR("ServerUse"));
            if (!useFn)
            {
                showOnScreen(L"ServerUse not found", 2.0f, 0.9f, 0.4f, 0.4f);
                closeGoatMenu();
                return;
            }
            int sz = useFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            // FItemHandle layout: int32 ID (4) + int32 Payload (4) + WeakObjectPtr (8) + 4 bytes = 20
            *reinterpret_cast<int32_t*>(buf.data() + 0) = saddlebagsID;
            // Owner WeakObjectPtr — leave zeroed; engine will resolve via ID lookup
            safeProcessEvent(invComp, useFn, buf.data());
            VLOG(STR("[MoriaCppMod] [GoatMenu] ACCESS SADDLEBAGS — ServerUse(ID={}) fired\n"), saddlebagsID);
            #endif // scaffolding for future Path B
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
        void openGoatSaddlebagInventory()
        {
            // Find first live goat
            UObject* goat = nullptr;
            for (auto& g : m_followGoats) {
                UObject* p = g.pawn.Get();
                if (p && isObjectAlive(p)) { goat = p; break; }
            }
            if (!goat) {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no live goat in m_followGoats\n"));
                showOnScreen(L"No goat present", 2.0f, 0.9f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] target goat={:p}\n"), (void*)goat);

            // Resolve component UClasses (cheap; cached after first hit).
            UClass* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorEquipComponent"));
            UClass* invCls   = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));

            auto getComp = [&](UObject* owner, UClass* cls) -> UObject* {
                if (!owner || !cls) return nullptr;
                auto* fn = owner->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (!fn) return nullptr;
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                writeGoatParm<UClass*>(fn, buf.data(), STR("ComponentClass"), cls);
                if (!safeProcessEvent(owner, fn, buf.data())) return nullptr;
                return readGoatParm<UObject*>(fn, buf.data(), STR("ReturnValue"), nullptr);
            };

            UObject* goatEquip = getComp(goat, equipCls);
            UObject* goatInv   = getComp(goat, invCls);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] goat EquipComp={:p} InvComp={:p}\n"),
                 (void*)goatEquip, (void*)goatInv);

            // ───── ONE-SHOT DIAGNOSTIC ─────
            if (!m_goatSaddlebagDiagDumped) {
                m_goatSaddlebagDiagDumped = true;
                auto dumpUFuncs = [&](UObject* o, const wchar_t* label) {
                    if (!o || !isObjectAlive(o)) return;
                    RC::Unreal::UClass* c = nullptr;
                    try { c = o->GetClassPrivate(); } catch (...) { return; }
                    if (!c) return;
                    VLOG(STR("[MoriaCppMod] [GoatSaddleDiag] === UFuncs matching open/use/show/activate/storage/container on {} ===\n"), label);
                    RC::Unreal::UStruct* cur = c;
                    int depth = 0;
                    while (cur && depth < 6) {
                        std::wstring sn;
                        try { sn = cur->GetName(); } catch (...) {}
                        // Iterate UFunctions via Children chain on the UStruct
                        // by checking GetFunctionByNameInChain candidates.
                        const wchar_t* keywords[] = {
                            STR("Open"), STR("Use"), STR("Show"), STR("Activate"),
                            STR("Storage"), STR("Container"), STR("Inventory"),
                            STR("Interact"), STR("Server"), nullptr
                        };
                        // We can't easily iterate UFunctions on a UClass directly
                        // via UE4SS exposed API; we know GetFunctionByNameInChain
                        // works. Instead, iterate properties (some are
                        // delegates / interactions we can match) and also list
                        // common UFunction candidates by name probe.
                        const wchar_t* candidates[] = {
                            STR("ServerUseItem"), STR("ServerUse"), STR("ServerUseEquippedItem"),
                            STR("OpenStorage"), STR("OpenContainer"), STR("OpenInventory"),
                            STR("ShowInventory"), STR("ShowStorage"),
                            STR("ActivateEquippedItem"), STR("UseEquippedItem"),
                            STR("RequestUseItem"), STR("UseFromEquipment"),
                            STR("ServerInteract"), STR("OnInteract"),
                            STR("ServerEquipDummyItem"), STR("ServerUnequip"),
                            nullptr
                        };
                        for (const wchar_t** p = candidates; *p; ++p) {
                            auto* fn = o->GetFunctionByNameInChain(*p);
                            if (fn) {
                                VLOG(STR("[MoriaCppMod] [GoatSaddleDiag]   {} . {} (parmSize={}) PRESENT\n"),
                                     sn.c_str(), *p, fn->GetParmsSize());
                            }
                        }
                        RC::Unreal::UStruct* sup = nullptr;
                        try { sup = cur->GetSuperStruct(); } catch (...) {}
                        if (!sup || sup == cur) break;
                        cur = sup;
                        ++depth;
                    }
                };
                dumpUFuncs(goatEquip, STR("goat.EquipComp"));
                dumpUFuncs(goatInv,   STR("goat.InvComp"));
                dumpUFuncs(goat,      STR("goat (actor)"));

                // Also dump player controller — for ServerUse with goat target
                if (m_localPC && isObjectAlive(m_localPC)) {
                    dumpUFuncs(m_localPC, STR("player.PC"));
                }
            }

            // ───── rc.4 IMPLEMENTATION (Desktop Claude recon 2026-05-22) ─────
            // ServerUse on the slot wrapper has been confirmed to no-op
            // (rc.3b proved this empirically). The fix is to spawn the
            // vanilla WBP_UI_Inventory_Screen_StorageMode_C widget directly
            // and bind it to the goat's MorInventoryComponent via the
            // ExposeOnSpawn bindings the parent widget already declares:
            //   • isOpenedFromNPC : bool   (load-bearing — engages NPC routing)
            //   • AssociatedNPC   : Object*
            //   • InventoryComponent : Object*
            //   • Target          : Object*  (mirror vanilla chest pattern)
            //   • isStorageView   : bool
            //
            // CRITICAL TIMING: isOpenedFromNPC has CPF_ExposeOnSpawn — it MUST
            // be set before AddToViewport (which triggers Construct). We
            // set it via reflection on the freshly-created widget instance
            // immediately after WidgetBlueprintLibrary::Create returns,
            // BEFORE AddToViewport.
            if (!goatInv || !isObjectAlive(goatInv)) {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no MorInventoryComponent on goat — abort\n"));
                showOnScreen(L"Goat has no inventory component", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }

            // [rc.11 2026-05-22] ALL DLL-side paths exhausted.
            //
            // Test matrix:
            //   rc.3:    ServerEquipDummyItem on goat → cosmetic only, no storage
            //   rc.4-6:  Widget direct-spawn → broken render (NPC placeholders,
            //            items in midair, build HUD bleed)
            //   rc.7:    ServerUse on player's bag → works mechanically but
            //            wrong architecture (bag-on-player, not bag-on-goat)
            //   rc.9:    ServerUse on goat's wrapper w/ probe pak → fires
            //            cleanly, no UI surfaces (cross-actor authority)
            //   rc.10:   Widget direct-spawn w/ probe pak → same broken
            //            render as rc.4-6, probe pak's storage redirect
            //            didn't fix it
            //
            // Cross-actor authority gap requires a UI broker (state
            // machine) that we cannot easily reconstruct from C++. Tobi
            // needs to add the BP graph wire on his side. See message to
            // Desktop Claude documenting the exhaustive test set.
            //
            // Safe no-op + toast until that lands. The rc.4-rc.10 spawn
            // code lives in #if 0 below for revival once Tobi delivers.
            // [rc.12c 2026-05-23] Tobi shipped v1.6.0 which changed
            // BP_NpcGoat.InvComp.StorageHandle.RowName from
            // "Goat.Slot.EpicPack" (1×1 custom slot) to "Dwarf.Inventory"
            // (vanilla dwarf body inventory). The cross-actor authority
            // gate we hit in rc.9 may have been keyed on storage config —
            // with the wrapper now backed by dwarf-standard inventory
            // semantics, ServerUse may now surface UI.
            //
            // RETRY the rc.9 ServerUse-on-goat-wrapper path. If it now
            // opens the UI, Tobi's data-only change solved it natively.
            // If it still fires-without-effect (same as rc.9 outcome),
            // we're back to phantom-chest territory.
            FProperty* itemsProp = goatInv->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no Items property on goat InvComp\n"));
                return;
            }
            uint8_t* listBase = reinterpret_cast<uint8_t*>(goatInv)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16)) return;
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] (rc.12c retry under v1.6.0 Dwarf.Inventory) goat Items Num={}\n"), arrNum);
            if (!arrData || arrNum <= 0)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] goat inventory empty\n"));
                showOnScreen(L"Goat inventory empty", 2.5f, 0.9f, 0.7f, 0.4f);
                return;
            }

            int stride  = iiSize();
            int itemOff = iiItemOff();
            int idOff   = iiIDOff();
            int32_t targetID = 0;
            std::wstring targetCls;
            for (int i = 0; i < arrNum && i < 64; ++i)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;
                UClass* itemCls = *reinterpret_cast<UClass**>(entry + itemOff);
                int32_t itemID  = *reinterpret_cast<int32_t*>(entry + idOff);
                std::wstring cls;
                if (itemCls && isObjectAlive(itemCls))
                {
                    try { cls = itemCls->GetName(); } catch (...) {}
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle]   item[{}] id={} class='{}'\n"),
                     i, itemID, cls.empty() ? STR("?") : cls.c_str());
                // Prefer wrapper/saddlebag-shaped match; fall back to first item.
                if (targetID == 0 && itemID != 0)
                {
                    if (cls.find(STR("Goat_Slot")) != std::wstring::npos
                     || cls.find(STR("SaddleBag")) != std::wstring::npos
                     || cls.find(STR("Saddlebag")) != std::wstring::npos
                     || cls.find(STR("EpicPack")) != std::wstring::npos)
                    {
                        targetID = itemID;
                        targetCls = cls;
                    }
                }
            }
            // Fallback: first non-zero item if no class match.
            if (targetID == 0)
            {
                for (int i = 0; i < arrNum && i < 64; ++i)
                {
                    uint8_t* entry = arrData + i * stride;
                    int32_t itemID = *reinterpret_cast<int32_t*>(entry + idOff);
                    if (itemID != 0) { targetID = itemID; break; }
                }
            }
            if (targetID == 0)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no usable item handle in goat inventory\n"));
                showOnScreen(L"No usable items on goat", 2.5f, 0.9f, 0.7f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] targeting handle id={} class='{}'\n"),
                 targetID, targetCls.empty() ? STR("(first item)") : targetCls.c_str());

            auto* useFn = goatInv->GetFunctionByNameInChain(STR("ServerUse"));
            if (!useFn)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] ServerUse missing on goat InvComp\n"));
                return;
            }
            std::vector<uint8_t> buf(useFn->GetParmsSize(), 0);
            *reinterpret_cast<int32_t*>(buf.data() + 0) = targetID;
            try { safeProcessEvent(goatInv, useFn, buf.data()); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [GoatSaddle] ServerUse(ItemHandle.ID={}) fired on goat InvComp — UI should open if v1.6.0 Dwarf.Inventory unlocked authority\n"),
                 targetID);
            showOnScreen(L"Saddlebag click dispatched (v1.6.0 retest)", 1.5f, 0.4f, 0.9f, 0.4f);
        }

        // Dormant rc.4-rc.7 implementations preserved as commented source
        // for when Tobi's saddlebag slot lands. They reference local vars
        // from openGoatSaddlebagInventory's frame — wrapped in #if 0 so
        // they compile out cleanly. See git history rc.4/rc.6/rc.7 for the
        // live versions.
#if 0
        // [rc.6 2026-05-22] One-shot: dump every property on the goat's
            // first inventory entry (the slot wrapper). Tells us the actual
            // tag string Tobi uses so we can match it against the widget's
            // expected tag. Gated by m_goatSaddleWrapperDumped so it only
            // fires once per session. Walks Items[0] memory + reflects each
            // UPROPERTY against the FItemInstance struct.
            if (!m_goatSaddleWrapperDumped)
            {
                m_goatSaddleWrapperDumped = true;
                FProperty* itemsProp = goatInv->GetPropertyByNameInChain(STR("Items"));
                if (itemsProp)
                {
                    uint8_t* listBase = reinterpret_cast<uint8_t*>(goatInv)
                                      + itemsProp->GetOffset_Internal() + iiaListOff();
                    if (isReadableMemory(listBase, 16))
                    {
                        uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                        int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                        if (arrData && arrNum > 0)
                        {
                            int stride  = iiSize();
                            int itemOff = iiItemOff();
                            uint8_t* entry = arrData + 0 * stride;
                            UClass* itemCls = (isReadableMemory(entry + itemOff, sizeof(UClass*)))
                                ? *reinterpret_cast<UClass**>(entry + itemOff) : nullptr;
                            if (itemCls && isObjectAlive(itemCls))
                            {
                                std::wstring clsName;
                                try { clsName = itemCls->GetName(); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [GoatSaddleDump] === wrapper CDO property dump for class '{}' ===\n"),
                                     clsName.c_str());
                                UObject* cdo = nullptr;
                                try { cdo = itemCls->GetClassDefaultObject(); } catch (...) {}
                                if (cdo && isObjectAlive(cdo))
                                {
                                    int propCount = 0;
                                    try {
                                        for (auto* p : itemCls->ForEachPropertyInChain())
                                        {
                                            if (!p) continue;
                                            std::wstring pn;
                                            try { pn = p->GetName(); } catch (...) { continue; }
                                            int32 off = -1;
                                            try { off = p->GetOffset_Internal(); } catch (...) {}
                                            // Try to read as common types and log first non-zero
                                            // representation. Best-effort: many properties will
                                            // log as raw 8 bytes which is fine for diagnostic.
                                            uint8_t* cdoBase = reinterpret_cast<uint8_t*>(cdo);
                                            if (off < 0 || !isReadableMemory(cdoBase + off, 8))
                                            {
                                                VLOG(STR("[MoriaCppMod] [GoatSaddleDump]   {} off=0x{:04x} (unreadable)\n"),
                                                     pn.c_str(), (unsigned)off);
                                                ++propCount;
                                                continue;
                                            }
                                            // Heuristic: try FName first if name contains "Tag" or "Row"
                                            bool isTagish = (pn.find(STR("Tag")) != std::wstring::npos)
                                                         || (pn.find(STR("Row")) != std::wstring::npos)
                                                         || (pn.find(STR("Name")) != std::wstring::npos);
                                            if (isTagish)
                                            {
                                                std::wstring tagStr = seh_fnameToString(cdoBase + off);
                                                VLOG(STR("[MoriaCppMod] [GoatSaddleDump]   {} off=0x{:04x} (FName?)='{}'\n"),
                                                     pn.c_str(), (unsigned)off,
                                                     tagStr.empty() ? STR("?") : tagStr.c_str());
                                            }
                                            else
                                            {
                                                uint64_t raw = *reinterpret_cast<uint64_t*>(cdoBase + off);
                                                VLOG(STR("[MoriaCppMod] [GoatSaddleDump]   {} off=0x{:04x} raw=0x{:016x}\n"),
                                                     pn.c_str(), (unsigned)off, raw);
                                            }
                                            ++propCount;
                                            if (propCount > 80) break;  // safety cap
                                        }
                                    } catch (...) {}
                                    VLOG(STR("[MoriaCppMod] [GoatSaddleDump] === end ({} props) ===\n"), propCount);
                                }
                                else
                                {
                                    VLOG(STR("[MoriaCppMod] [GoatSaddleDump] wrapper CDO null/dead — skipped\n"));
                                }
                            }
                        }
                    }
                }
            }

            // 1. Resolve saddlebag class (cached from rc.3b onward).
            UClass* saddleCls = ensureSaddlebagItemClass();
            if (!saddleCls)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] saddlebag class unresolved — bail\n"));
                showOnScreen(L"Saddlebag class not loaded", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }

            // 2. Find the player's MorInventoryComponent.
            UObject* playerInv = findPlayerInventoryComponent(m_localPawn);
            if (!playerInv || !isObjectAlive(playerInv))
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] player InvComp not found\n"));
                showOnScreen(L"Player inventory not accessible", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }

            // 3. Walk player's Items array looking for the saddlebag.
            FProperty* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no Items property on player InvComp\n"));
                return;
            }
            uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16))
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] player Items list base unreadable\n"));
                return;
            }
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] player InvComp.Items: Num={} (looking for class={:p})\n"),
                 arrNum, (void*)saddleCls);

            if (!arrData || arrNum <= 0)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] player inventory empty — no saddlebag to use\n"));
                showOnScreen(L"Craft saddlebags first (need them in inventory)", 3.0f, 0.95f, 0.7f, 0.4f);
                return;
            }

            int stride  = iiSize();
            int itemOff = iiItemOff();
            int idOff   = iiIDOff();
            int32_t saddleID = 0;
            for (int i = 0; i < arrNum && i < 200; ++i)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;
                UClass* itemCls = *reinterpret_cast<UClass**>(entry + itemOff);
                if (!itemCls || !isObjectAlive(itemCls)) continue;
                if (itemCls != saddleCls) continue;
                int32_t itemID = *reinterpret_cast<int32_t*>(entry + idOff);
                if (itemID != 0) { saddleID = itemID; break; }
            }

            if (saddleID == 0)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no BP_SaddleBags_Goat_C in player inventory — craft prompt\n"));
                showOnScreen(L"No saddlebags found — craft them first", 3.0f, 0.95f, 0.7f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] found player saddlebag handle ID={}\n"), saddleID);

            // 4. ServerUse(ItemHandle) on the PLAYER's InvComp.
            // FItemHandle layout: int32 ID (+0), int32 Payload (+4),
            // FWeakObjectPtr (+8), pad to 20 bytes.
            auto* useFn = playerInv->GetFunctionByNameInChain(STR("ServerUse"));
            if (!useFn)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] ServerUse missing on player InvComp\n"));
                showOnScreen(L"ServerUse not available", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            std::vector<uint8_t> buf(useFn->GetParmsSize(), 0);
            *reinterpret_cast<int32_t*>(buf.data() + 0) = saddleID;
            // Payload/WeakObjectPtr/padding stay zero — engine resolves by ID.
            try { safeProcessEvent(playerInv, useFn, buf.data()); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [GoatSaddle] ServerUse(ItemHandle.ID={}) fired on PLAYER InvComp={:p} — saddlebag UI should open\n"),
                 saddleID, (void*)playerInv);
#endif  // dormant rc.4-rc.7 saddlebag UI attempts

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
            // Edge-trigger Esc OR Tab.
            static bool s_lastEsc = false;
            static bool s_lastTab = false;
            bool eDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            bool tDown = (GetAsyncKeyState(VK_TAB)    & 0x8000) != 0;
            bool fire = (eDown && !s_lastEsc) || (tDown && !s_lastTab);
            s_lastEsc = eDown;
            s_lastTab = tDown;
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
                    try { safeProcessEvent(m_goatSaddlebagWidget, fn, b.data()); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] RemoveFromParent fired on widget={:p}\n"),
                         (void*)m_goatSaddlebagWidget);
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
        UObject* m_goatHeader{nullptr};  // status text — updated per show

        // [Phase 3 PATH A] Vanilla-menu row injection state.
        FWeakObjectPtr m_pendingInteractMenu{};
        bool           m_goatInjectPending{false};
        bool           m_goatInjectLogged{false};

        // One injected row per action. Index encodes which handler fires on press.
        enum class GoatRowAction : int {
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

            // [Phase 6 / Path α] OnShow can fire for our own submenu too
            // (it's a UI_WBP_InteractionMenu_C). For our submenu we still
            // want the role-text walker (to override "Citizen" → "Porter
            // Goat"), but we must NOT update m_pendingInteractMenu to point
            // at our submenu (otherwise forceProximityMenuHidden would
            // collapse our own UI).
            bool isOurSubmenu = (menu == m_goatSubMenu);
            if (isOurSubmenu)
            {
                VLOG(STR("[MoriaCppMod] [GoatInject] OnShow for OUR submenu — walker only, skip proximity-tracking\n"));
                const wchar_t* name = m_goatName.empty() ? STR("Porter Goat") : m_goatName.c_str();
                overrideMenuNameText(menu, name);
                overrideRoleTextInMenuTree(menu, STR("Porter Goat"));
                return;
            }

            // Resolve the row container.
            UObject* container = nullptr;
            if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try { safeProcessEvent(menu, fn, buf.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    container = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            }
            if (!container || !isObjectAlive(container)) return;

            // First child's Interactable tells us which NPC the menu is for.
            int32_t count = 0;
            if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try { safeProcessEvent(container, fn, buf.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    count = *reinterpret_cast<int32_t*>(buf.data() + pRet->GetOffset_Internal());
            }
            if (count <= 0) return;

            UObject* firstRow = nullptr;
            if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildAt")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                if (auto* p = findParam(fn, STR("Index")))
                    *reinterpret_cast<int32_t*>(buf.data() + p->GetOffset_Internal()) = 0;
                try { safeProcessEvent(container, fn, buf.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    firstRow = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            }
            if (!firstRow || !isObjectAlive(firstRow)) return;

            auto* npcCompPtr = firstRow->GetValuePtrByPropertyNameInChain<UObject*>(STR("Interactable"));
            if (!npcCompPtr || !isReadableMemory(npcCompPtr, sizeof(UObject*))) return;
            UObject* npcComp = *npcCompPtr;
            if (!npcComp || !isObjectAlive(npcComp)) return;

            UObject* owner = nullptr;
            try { owner = npcComp->GetOuterPrivate(); } catch (...) {}
            if (!owner || !isObjectAlive(owner)) return;

            std::wstring ownerCls;
            try { ownerCls = owner->GetClassPrivate()->GetName(); } catch (...) {}
            bool ours = false;
            for (auto& g : m_followGoats)
            {
                UObject* mine = g.pawn.Get();
                if (mine && mine == owner) { ours = true; break; }
            }
            VLOG(STR("[MoriaCppMod] [GoatHeader] OnShow inspect owner={:p} cls='{}' ours={}\n"),
                 (void*)owner, ownerCls, ours);
            if (!ours) return;

            // ROOT CAUSE FIX (2026-05-14): the original Path A hook that set
            // m_pendingInteractMenu is #if 0'd out, leaving the field NULL.
            // openGoatSubmenu reads m_pendingInteractMenu to capture the
            // proximity menu pointer so closeGoatSubmenu can hide it. Without
            // this, "cached proximity menu=0x0" every time → no hide → flicker
            // + the original menu (with "E Details") stays visible behind ours.
            // Now we populate it here in OnShow PE-post for goat menus only.
            m_pendingInteractMenu = FWeakObjectPtr(menu);
            VLOG(STR("[MoriaCppMod] [GoatInject] captured proximity menu={:p} into m_pendingInteractMenu\n"),
                 (void*)menu);

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
            VLOG(STR("[MoriaCppMod] [GoatHeader] menu={:p} renamed name='{}' + role override fired\n"),
                 (void*)menu, name);
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
                try { safeProcessEvent(w, fn, buf.data()); } catch (...) { return STR(""); }
                auto* pRet = findParam(fn, STR("ReturnValue"));
                if (!pRet) return STR("");
                FText* t = reinterpret_cast<FText*>(buf.data() + pRet->GetOffset_Internal());
                if (!t) return STR("");
                try { return std::wstring(t->ToString()); } catch (...) { return STR(""); }
            };

            // Vanilla role names that should be replaced.
            auto shouldOverride = [](const std::wstring& s) {
                if (s.empty()) return false;
                static const wchar_t* kRoleStrs[] = {
                    STR("Citizen"), STR("Wanderer"), STR("Recruit"), STR("Survivor"),
                    STR("Porter")  // already-Porter still gets the explicit "Porter Goat" override
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
                try { cls = w->GetClassPrivate()->GetName(); } catch (...) {}
                if (cls == STR("TextBlock"))
                {
                    std::wstring cur = readText(w);
                    std::wstring wname;
                    try { wname = std::wstring(w->GetNamePrivate().ToString()); } catch (...) {}
                    if (dumpNow)
                    {
                        VLOG(STR("[MoriaCppMod] [GoatHeader] DUMP TextBlock {:p} name='{}' text='{}'\n"),
                             (void*)w, wname, cur);
                    }
                    if (shouldOverride(cur))
                    {
                        umgSetText(w, std::wstring(replacement));
                        VLOG(STR("[MoriaCppMod] [GoatHeader] role-text override on {:p} name='{}': '{}' -> '{}'\n"),
                             (void*)w, wname, cur, replacement);
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
                if (nestedWt && *nestedWt) {
                    auto* nestedRoot = (*nestedWt)->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                    if (nestedRoot && *nestedRoot && *nestedRoot != w) walk(*nestedRoot);
                }
            };
            walk(root);
            if (dumpNow) s_dumped = true;
            VLOG(STR("[MoriaCppMod] [GoatHeader] role-text walk: {} overrides applied (dump={})\n"),
                 hits, dumpNow ? STR("YES") : STR("no"));
        }

        // PE-pre callback: vanilla UI_WBP_InteractionMenu_C is being
        // pointed at a new Interactable (NPC entering proximity range).
        // We read the function's first param (the new Interactable, an
        // FScriptInterface or ObjectProperty, both have the UObject* at
        // offset 0). Walk to its owner Actor, check m_followGoats, flag a
        // tick-deferred discovery walk.
        void onInteractMenuSetInteractablePre(UObject* menu, UFunction* func, void* parms)
        {
            VLOG(STR("[MoriaCppMod] [GoatInject] hook entered menu={:p} func={:p} parms={:p} herdSz={}\n"),
                 (void*)menu, (void*)func, parms, (int)m_followGoats.size());
            if (!menu || !isObjectAlive(menu)) return;
            if (m_followGoats.empty()) return;

            // Param 0 = NewInteractable (FScriptInterface or UObject*).
            // Both layouts put the UObject* at offset 0.
            UObject* npcComp = nullptr;
            if (parms)
            {
                npcComp = *reinterpret_cast<UObject**>(parms);
            }
            VLOG(STR("[MoriaCppMod] [GoatInject] param-read npcComp={:p}\n"), (void*)npcComp);

            if (!npcComp || !isObjectAlive(npcComp))
            {
                // Unhover / null interactable — drop pending state and
                // rip ALL injected rows out so vanilla's pool doesn't see
                // strangers when it repopulates for a different NPC.
                clearGoatInjectedRows();
                m_pendingInteractMenu = FWeakObjectPtr();
                m_goatInjectPending = false;
                m_goatInjectLogged = false;
                VLOG(STR("[MoriaCppMod] [GoatInject] null npcComp — cleared pending + removed all injected rows\n"));
                return;
            }

            UObject* owner = nullptr;
            try { owner = npcComp->GetOuterPrivate(); } catch (...) {}
            std::wstring npcClsName, ownerClsName;
            try { npcClsName = npcComp->GetClassPrivate()->GetName(); } catch (...) {}
            if (owner) { try { ownerClsName = owner->GetClassPrivate()->GetName(); } catch (...) {} }
            VLOG(STR("[MoriaCppMod] [GoatInject] npcCls='{}' owner={:p} ownerCls='{}'\n"),
                 npcClsName, (void*)owner, ownerClsName);
            if (!owner || !isObjectAlive(owner)) return;

            bool ours = false;
            for (auto& g : m_followGoats)
            {
                UObject* mine = g.pawn.Get();
                if (mine && mine == owner) { ours = true; break; }
            }
            if (!ours)
            {
                VLOG(STR("[MoriaCppMod] [GoatInject] owner not in m_followGoats — skip\n"));
                return;
            }

            VLOG(STR("[MoriaCppMod] [GoatInject] menu={:p} npcComp={:p} owner={:p} — flagging pending\n"),
                 (void*)menu, (void*)npcComp, (void*)owner);
            m_pendingInteractMenu = FWeakObjectPtr(menu);
            m_goatInjectPending = true;
            m_goatInjectLogged = false;
        }

        // [Phase 4 disabled — Path A dead end]
        // Row injection into vanilla UI_WBP_InteractionMenu_C confirmed
        // structurally blocked: vanilla's cursor iterates the NPC's enabled
        // interaction structs, not container children. Empirically only 2
        // slots (Rescue + Details) can be enabled via pak. Path B' (custom
        // UMG modal owned by us) is now the way. This function is gated to
        // no-op; code below is kept dormant for archaeology.
        void tickGoatMenuInject()
        {
            return;  // Path A disabled
            if (!m_goatInjectPending) return;
            UObject* menu = m_pendingInteractMenu.Get();
            if (!menu || !isObjectAlive(menu))
            {
                m_goatInjectPending = false;
                return;
            }
            // One-shot per menu open.
            if (m_goatInjectLogged) return;
            m_goatInjectLogged = true;

            // 1. Resolve the row container via GetInteractionWidgetsContainer.
            UObject* container = nullptr;
            if (auto* fn = menu->GetFunctionByNameInChain(STR("GetInteractionWidgetsContainer")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try { safeProcessEvent(menu, fn, buf.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    container = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            }
            std::wstring containerCls;
            if (container && isObjectAlive(container))
            {
                try { containerCls = container->GetClassPrivate()->GetName(); } catch (...) {}
            }
            VLOG(STR("[MoriaCppMod] [GoatInject] container={:p} cls='{}'\n"),
                 (void*)container, containerCls);
            if (!container || !isObjectAlive(container)) return;

            // 2. Walk children via UPanelWidget UFunctions.
            int32_t count = 0;
            if (auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount")))
            {
                std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                try { safeProcessEvent(container, fn, buf.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    count = *reinterpret_cast<int32_t*>(buf.data() + pRet->GetOffset_Internal());
            }
            VLOG(STR("[MoriaCppMod] [GoatInject] container has {} children\n"), count);

            auto* getChildAt = container->GetFunctionByNameInChain(STR("GetChildAt"));
            auto* idxParam   = getChildAt ? findParam(getChildAt, STR("Index"))       : nullptr;
            auto* retParam   = getChildAt ? findParam(getChildAt, STR("ReturnValue")) : nullptr;

            // Find the first UI_WBP_Interaction_C child — our clone template.
            UObject* templateRow = nullptr;
            for (int i = 0; i < count && getChildAt && idxParam && retParam; ++i)
            {
                std::vector<uint8_t> b(getChildAt->GetParmsSize(), 0);
                *reinterpret_cast<int32_t*>(b.data() + idxParam->GetOffset_Internal()) = i;
                try { safeProcessEvent(container, getChildAt, b.data()); } catch (...) { continue; }
                UObject* child = *reinterpret_cast<UObject**>(b.data() + retParam->GetOffset_Internal());
                if (!child || !isObjectAlive(child)) continue;
                std::wstring cls;
                try { cls = child->GetClassPrivate()->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatInject]   child[{}] = {:p} cls='{}'\n"),
                     i, (void*)child, cls);
                if (cls == STR("UI_WBP_Interaction_C") && !templateRow)
                    templateRow = child;
            }
            if (!templateRow)
            {
                VLOG(STR("[MoriaCppMod] [GoatInject] no UI_WBP_Interaction_C template found — abort\n"));
                return;
            }

            // [Phase 3] — clone the template 7 times, one per action.
            UClass* rowCls = nullptr;
            try { rowCls = templateRow->GetClassPrivate(); } catch (...) {}
            if (!rowCls)
            {
                VLOG(STR("[MoriaCppMod] [GoatInject] template has no class — abort\n"));
                return;
            }

            // Drop any stale rows from a prior open in case cleanup missed.
            clearGoatInjectedRows();

            static const wchar_t* const kLabels[kGoatRowCount] = {
                STR("Follow"),
                STR("Stay"),
                STR("Wander"),
                STR("Access Saddlebags"),
                STR("Feed"),
                STR("Rename (from clipboard)"),
                STR("Dismiss"),
            };

            auto* tVisPtr = templateRow->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
            uint8_t tVis = tVisPtr ? *tVisPtr : 0;

            for (int i = 0; i < kGoatRowCount; ++i)
            {
                UObject* newRow = jw_createGameWidget(rowCls);
                if (!newRow)
                {
                    VLOG(STR("[MoriaCppMod] [GoatInject] row[{}] create returned null — skip\n"), i);
                    continue;
                }

                // Copy Interactable / Interactor / InteractComponent from
                // template (reflective — survives FGK widget layout shifts).
                copyRowPropertyByName(newRow, templateRow, STR("Interactable"));
                copyRowPropertyByName(newRow, templateRow, STR("Interactor"));
                copyRowPropertyByName(newRow, templateRow, STR("InteractComponent"));

                // AddChild to the container.
                addToVBox(container, newRow);

                // [Phase 3 keystone] Register the row's MorCursorSlotComponent
                // with the cursor subsystem — this is what makes vanilla's
                // selection cursor see our row as a navigable target. Without
                // this, the cursor walks only registered components (Details
                // + intermittent first/last clones).
                registerCursorSlotOnRow(newRow);

                // One-shot diagnostic on the first clone: scan all live
                // UObjects for any MorCursorSlot* whose outer chain includes
                // (a) the vanilla Details row, (b) our clone. Reveals where
                // vanilla actually stores the component.
                if (i == 0) probeCursorSlots(templateRow, newRow);

                // Match template visibility, then call OnUnpool + OnSetInteraction.
                auto* nVisPtr = newRow->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                if (tVisPtr && nVisPtr) *nVisPtr = tVis;

                if (auto* fn = newRow->GetFunctionByNameInChain(STR("OnUnpool")))
                {
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    try { safeProcessEvent(newRow, fn, b.data()); } catch (...) {}
                }
                if (auto* setFn = newRow->GetFunctionByNameInChain(STR("OnSetInteraction")))
                {
                    auto* pTxt  = findParam(setFn, STR("FormattedText"));
                    auto* pBool = findParam(setFn, STR("bCanDoInteraction"));
                    if (pTxt && pBool)
                    {
                        std::vector<uint8_t> buf(setFn->GetParmsSize(), 0);
                        FText txt(kLabels[i]);
                        std::memcpy(buf.data() + pTxt->GetOffset_Internal(), &txt, sizeof(FText));
                        *reinterpret_cast<bool*>(buf.data() + pBool->GetOffset_Internal()) = true;
                        try { safeProcessEvent(newRow, setFn, buf.data()); } catch (...) {}
                    }
                }

                // Force Unfocused state — without this every injected row
                // shows its E icon at default (all 7 E icons visible at once).
                // Vanilla normally calls OnSetIsSelected(false) on pool-fetch
                // before reusing a row.
                if (auto* selFn = newRow->GetFunctionByNameInChain(STR("OnSetIsSelected")))
                {
                    std::vector<uint8_t> b(selFn->GetParmsSize(), 0);
                    if (auto* p = findParam(selFn, STR("bSelected")))
                        *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = false;
                    try { safeProcessEvent(newRow, selFn, b.data()); } catch (...) {}
                }

                m_goatRows[i] = FWeakObjectPtr(newRow);
                VLOG(STR("[MoriaCppMod] [GoatInject] row[{}] '{}' = {:p}\n"),
                     i, kLabels[i], (void*)newRow);
            }
            VLOG(STR("[MoriaCppMod] [GoatInject] injected {} rows into menu={:p}\n"),
                 kGoatRowCount, (void*)menu);
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
                        try { safeProcessEvent(row, fn, nullptr); } catch (...) {}
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

        // [Phase 3] Per-tick probe — called from main tick when the goat
        // menu is open. Reads vanilla's selection cursor via
        // GetCurrentInteractionWidget() and logs cursor changes only (low
        // chatter). Validates Desktop recon Q2 — whether scroll input
        // actually walks our injected rows.
        void tickGoatMenuProbe()
        {
            UObject* menu = m_pendingInteractMenu.Get();
            if (!menu || !isObjectAlive(menu)) return;
            ULONGLONG now = GetTickCount64();
            if (now - m_goatProbeLastMs < 200) return;  // 5 Hz
            m_goatProbeLastMs = now;
            auto* fn = menu->GetFunctionByNameInChain(STR("GetCurrentInteractionWidget"));
            if (!fn) return;
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            try { safeProcessEvent(menu, fn, buf.data()); } catch (...) { return; }
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pRet) return;
            UObject* cur = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
            if (cur != m_goatProbeLastCursor)
            {
                int idx = findGoatRowIndex(cur);
                VLOG(STR("[MoriaCppMod] [GoatProbe] cursor moved {:p} -> {:p} (ourRowIdx={})\n"),
                     (void*)m_goatProbeLastCursor, (void*)cur, idx);
                m_goatProbeLastCursor = cur;
            }
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
                VLOG(STR("[MoriaCppMod] [RoleProbe] GetCurrentRole UFunction not found on npcComp={:p}\n"),
                     (void*)npcComp);
                return;
            }
            std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
            try { safeProcessEvent(npcComp, fn, buf.data()); } catch (...) {
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
                try { cls = prop->GetClass().GetName(); } catch (...) {}
                if (cls == STR("StructProperty")) { outParam = prop; break; }
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
            if (dt && isObjectAlive(dt)) { try { dtName = dt->GetName(); } catch (...) {} }
            try { rowStr = rowFName.ToString(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [RoleProbe] After SetRole: DT={:p} ('{}'), Row='{}'\n"),
                 (void*)dt, dtName, rowStr);
        }

        void setGoatRole(UObject* npcComp, const wchar_t* rowName)
        {
            if (!npcComp || !isObjectAlive(npcComp) || !rowName) return;
            auto* setRoleFn = npcComp->GetFunctionByNameInChain(STR("SetRole"));
            if (!setRoleFn)
            {
                VLOG(STR("[MoriaCppMod] [SetRole] UFunction not found on npcComp={:p}\n"),
                     (void*)npcComp);
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
                    VLOG(STR("[MoriaCppMod] [SetRole] enumerating SetRole params (parmSize={}):\n"),
                         setRoleFn->GetParmsSize());
                }
                for (auto* prop : setRoleFn->ForEachProperty())
                {
                    if (!prop) continue;
                    bool isParam = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80 /*CPF_Parm*/));
                    if (!isParam) continue;
                    std::wstring n;
                    try { n = std::wstring(prop->GetName()); } catch (...) {}
                    std::wstring cls;
                    try { cls = prop->GetClass().GetName(); } catch (...) {}
                    if (!s_dumped)
                    {
                        VLOG(STR("[MoriaCppMod] [SetRole]   param '{}' cls='{}' off=0x{:X}\n"),
                             n, cls, prop->GetOffset_Internal());
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
            auto* dt = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Game/Character/NpcDwarf/DT_NPCRoles.DT_NPCRoles"));
            std::vector<uint8_t> buf(setRoleFn->GetParmsSize(), 0);
            // FMorNPCRoleRowHandle = { UDataTable* DataTable; FName RowName; }
            uint8_t* handle = buf.data() + handleParam->GetOffset_Internal();
            *reinterpret_cast<UObject**>(handle + 0) = dt;
            FName rowFName(rowName);
            *reinterpret_cast<FName*>(handle + 8) = rowFName;
            try { safeProcessEvent(npcComp, setRoleFn, buf.data()); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [SetRole] SetRole(param='{}', DT={:p}, RowName='{}') fired on npcComp={:p}\n"),
                 handleParamName, (void*)dt, rowName, (void*)npcComp);

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
                    try { nc = npcComp->GetClassPrivate(); } catch (...) {}
                    if (nc)
                    {
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag] MorNPCComponent UPROPERTYs:\n"));
                        int n = 0;
                        try {
                            for (auto* prop : nc->ForEachProperty()) {
                                std::wstring pname, ptype;
                                try { pname = prop->GetName(); } catch (...) {}
                                try { ptype = prop->GetClass().GetName(); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [SetRoleDiag]   .{} : {} @ +0x{:x}\n"),
                                     pname.empty() ? STR("?") : pname.c_str(),
                                     ptype.empty() ? STR("?") : ptype.c_str(),
                                     prop->GetOffset_Internal());
                                if (++n > 48) { VLOG(STR("[MoriaCppMod] [SetRoleDiag]   ...(truncated)\n")); break; }
                            }
                        } catch (...) {}
                    }
                }
            }

            // (2) Direct-read CurrentRole UPROPERTY immediately after SetRole.
            // CurrentRole on MorNPCComponent is a FMorNPCRoleRowHandle struct
            // (16 bytes: UDataTable* + FName).
            try {
                auto* curRolePtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CurrentRole"));
                if (curRolePtr)
                {
                    UObject* dtAfter = *reinterpret_cast<UObject**>(curRolePtr + 0);
                    std::wstring rowAfter = seh_fnameToString(curRolePtr + 8);
                    VLOG(STR("[MoriaCppMod] [SetRoleDiag] Direct read CurrentRole: DT={:p} Row='{}'\n"),
                         (void*)dtAfter, rowAfter.empty() ? STR("None") : rowAfter.c_str());
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [SetRoleDiag] CurrentRole UPROPERTY not found on npcComp\n"));
                }
            } catch (...) {
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
                    VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy enum params (parmSize={}):\n"),
                         setFuzzyFn->GetParmsSize());
                    FProperty* structParam = nullptr;
                    FProperty* strParam = nullptr;
                    FProperty* nameParam = nullptr;
                    for (auto* prop : setFuzzyFn->ForEachProperty())
                    {
                        if (!prop) continue;
                        bool isParam = prop->HasAnyPropertyFlags(static_cast<RC::Unreal::EPropertyFlags>(0x80));
                        if (!isParam) continue;
                        std::wstring pname, ptype;
                        try { pname = prop->GetName(); } catch (...) {}
                        try { ptype = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag]   fuzzy param '{}' cls='{}' off=0x{:X}\n"),
                             pname, ptype, prop->GetOffset_Internal());
                        if (!structParam && ptype == STR("StructProperty")) structParam = prop;
                        if (!strParam    && ptype == STR("StrProperty"))    strParam    = prop;
                        if (!nameParam   && ptype == STR("NameProperty"))   nameParam   = prop;
                    }
                    std::vector<uint8_t> fbuf(setFuzzyFn->GetParmsSize(), 0);
                    bool fired = false;
                    if (structParam) {
                        // Try as FMorNPCRoleRowHandle (DT + RowName).
                        uint8_t* h = fbuf.data() + structParam->GetOffset_Internal();
                        *reinterpret_cast<UObject**>(h + 0) = dt;
                        *reinterpret_cast<FName*>(h + 8) = FName(rowName);
                        try { safeProcessEvent(npcComp, setFuzzyFn, fbuf.data()); fired = true; } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with Struct payload (DT+Name)\n"));
                    } else if (strParam) {
                        FString fs(rowName);
                        *reinterpret_cast<FString*>(fbuf.data() + strParam->GetOffset_Internal()) = fs;
                        try { safeProcessEvent(npcComp, setFuzzyFn, fbuf.data()); fired = true; } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with FString payload\n"));
                    } else if (nameParam) {
                        *reinterpret_cast<FName*>(fbuf.data() + nameParam->GetOffset_Internal()) = FName(rowName);
                        try { safeProcessEvent(npcComp, setFuzzyFn, fbuf.data()); fired = true; } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy fired with FName payload\n"));
                    } else {
                        VLOG(STR("[MoriaCppMod] [SetRoleDiag] SetRoleFuzzy: no recognized param type — abandoned\n"));
                    }
                    // (3b) Read GetCurrentRole UFunction back if we fired.
                    // Direct CurrentRole UPROPERTY isn't on the component
                    // (rc.62 finding) — the role lives elsewhere (likely
                    // AMorNPCManager.NpcInfo entry which we don't have for
                    // this goat). So this readback may still show 'None'.
                    if (fired) {
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
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) { return nullptr; }
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    container = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
            }
            if (!container || !isObjectAlive(container)) return nullptr;
            auto* fn = container->GetFunctionByNameInChain(STR("GetChildAt"));
            if (!fn) return nullptr;
            auto* pIdx = findParam(fn, STR("Index"));
            auto* pRet = findParam(fn, STR("ReturnValue"));
            if (!pIdx || !pRet) return nullptr;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            *reinterpret_cast<int32_t*>(b.data() + pIdx->GetOffset_Internal()) = idx;
            try { safeProcessEvent(container, fn, b.data()); } catch (...) { return nullptr; }
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
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) { return 0; }
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    container = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
            }
            if (!container || !isObjectAlive(container)) return 0;
            auto* fn = container->GetFunctionByNameInChain(STR("GetChildrenCount"));
            if (!fn) return 0;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            try { safeProcessEvent(container, fn, b.data()); } catch (...) { return 0; }
            if (auto* pRet = findParam(fn, STR("ReturnValue")))
                return *reinterpret_cast<int32_t*>(b.data() + pRet->GetOffset_Internal());
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
                try { c = obj->GetClassPrivate(); } catch (...) { return LoopAction::Continue; }
                if (!c) return LoopAction::Continue;
                std::wstring cn;
                try { cn = c->GetName(); } catch (...) { return LoopAction::Continue; }
                if (cn.find(STR("CursorSlot")) == std::wstring::npos) return LoopAction::Continue;
                // Walk outer chain looking for row.
                UObject* o = nullptr;
                try { o = obj->GetOuterPrivate(); } catch (...) {}
                for (int hop = 0; hop < 8 && o; ++hop)
                {
                    if (o == row) { found = obj; return LoopAction::Break; }
                    try { o = o->GetOuterPrivate(); } catch (...) { break; }
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
            if (vSlot) try { vCls = vSlot->GetClassPrivate()->GetName(); } catch (...) {}
            if (cSlot) try { cCls = cSlot->GetClassPrivate()->GetName(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [CursorReg] PROBE vanilla row={:p} -> slot={:p} cls='{}'\n"),
                 (void*)vanillaRow, (void*)vSlot, vCls);
            VLOG(STR("[MoriaCppMod] [CursorReg] PROBE cloned row={:p} -> slot={:p} cls='{}'\n"),
                 (void*)clonedRow, (void*)cSlot, cCls);
            if (vSlot)
            {
                // Walk vanilla slot's outer chain so we know what to look for.
                UObject* o = vSlot;
                int hop = 0;
                while (o && hop < 8)
                {
                    std::wstring oCls;
                    try { oCls = o->GetClassPrivate()->GetName(); } catch (...) {}
                    std::wstring oName;
                    try { oName = std::wstring(o->GetNamePrivate().ToString()); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [CursorReg] PROBE vanilla slot outer[{}] = {:p} cls='{}' name='{}'\n"),
                         hop, (void*)o, oCls, oName);
                    try { o = o->GetOuterPrivate(); } catch (...) { break; }
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
            }) {
                auto* p = row->GetValuePtrByPropertyNameInChain<UObject*>(candidate);
                if (p && *p && isObjectAlive(*p))
                {
                    slot = *p;
                    VLOG(STR("[MoriaCppMod] [CursorReg] found slot via property name '{}' = {:p}\n"),
                         candidate, (void*)slot);
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
                try {
                    auto* cls = row->GetClassPrivate();
                    uint8_t* rowBase = reinterpret_cast<uint8_t*>(row);
                    for (auto* strct = static_cast<UStruct*>(cls);
                         strct;
                         strct = strct->GetSuperStruct())
                    {
                        for (auto* prop : strct->ForEachProperty())
                        {
                            if (!prop) continue;
                            std::wstring pcn;
                            try { pcn = prop->GetClass().GetName(); } catch (...) {}
                            if (pcn != STR("ObjectProperty")) continue;
                            std::wstring pname;
                            try { pname = std::wstring(prop->GetName()); } catch (...) {}
                            UObject** valPtr = nullptr;
                            try { valPtr = reinterpret_cast<UObject**>(rowBase + prop->GetOffset_Internal()); } catch (...) { continue; }
                            if (!valPtr) continue;
                            UObject* val = *valPtr;
                            std::wstring valCls;
                            if (val && isObjectAlive(val))
                            {
                                try { valCls = val->GetClassPrivate()->GetName(); } catch (...) {}
                            }
                            if (dumpNow)
                            {
                                VLOG(STR("[MoriaCppMod] [CursorReg] DUMP prop='{}' val={:p} valCls='{}'\n"),
                                     pname, (void*)val, valCls);
                            }
                            if (!slot && val && isObjectAlive(val)
                                && (valCls.find(STR("CursorSlot")) != std::wstring::npos
                                    || valCls.find(STR("Cursor")) != std::wstring::npos
                                    || pname.find(STR("Cursor")) != std::wstring::npos
                                    || pname.find(STR("Slot")) != std::wstring::npos))
                            {
                                slot = val;
                                VLOG(STR("[MoriaCppMod] [CursorReg] match prop='{}' valCls='{}' slot={:p}\n"),
                                     pname, valCls, (void*)slot);
                                if (!dumpNow) break;
                            }
                        }
                        if (slot && !dumpNow) break;
                    }
                } catch (...) {}
                if (dumpNow) s_dumped = true;
            }
            if (!slot)
            {
                VLOG(STR("[MoriaCppMod] [CursorReg] no slot found on row {:p} (see DUMP above)\n"),
                     (void*)row);
                return;
            }
            auto* regFn = slot->GetFunctionByNameInChain(STR("RegisterComponent"));
            if (!regFn)
            {
                VLOG(STR("[MoriaCppMod] [CursorReg] no RegisterComponent UFunction on slot={:p}\n"),
                     (void*)slot);
                return;
            }
            try { safeProcessEvent(slot, regFn, nullptr); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [CursorReg] RegisterComponent fired on slot={:p} (row={:p})\n"),
                 (void*)slot, (void*)row);
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
            if (auto* p = findParam(fn, STR("bSelected")))
                *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = sel;
            try { safeProcessEvent(row, fn, b.data()); } catch (...) {}
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
                try { safeProcessEvent(menu, fn, b.data()); } catch (...) {}
                if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    current = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
            }

            int currentIdx = -1;
            for (int i = 0; i < count; ++i)
            {
                if (goatMenuGetChildAt(i) == current) { currentIdx = i; break; }
            }
            // Clamp at top and bottom — vanilla style, no wrap.
            int newIdx = (currentIdx < 0) ? 0 : (currentIdx + direction);
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= count) newIdx = count - 1;
            if (newIdx == currentIdx) return;

            UObject* newCursor = goatMenuGetChildAt(newIdx);
            if (!newCursor || !isObjectAlive(newCursor)) return;

            if (auto* slot = menu->GetValuePtrByPropertyNameInChain<UObject*>(STR("storedCurrentInteractionWidget")))
                *slot = newCursor;

            goatRowSetSelected(current, false);
            goatRowSetSelected(newCursor, true);

            VLOG(STR("[MoriaCppMod] [GoatNav] cursor {} -> {} (idx {} -> {}, dir={}, count={})\n"),
                 (void*)current, (void*)newCursor, currentIdx, newIdx, direction, count);
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
                try { cls = prop->GetClass().GetName(); } catch (...) {}
                if (cls == STR("TextProperty")) { textParam = prop; break; }
            }
            if (!textParam) return;
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            FText ftxt(text);
            std::memcpy(b.data() + textParam->GetOffset_Internal(), &ftxt, sizeof(FText));
            try { safeProcessEvent(menu, fn, b.data()); } catch (...) {}
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
                    auto* cls = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
                    if (!cls) continue;
                    std::vector<uint8_t> b(fn->GetParmsSize(), 0);
                    if (auto* p = findParam(fn, STR("ComponentClass")))
                        *reinterpret_cast<UClass**>(b.data() + p->GetOffset_Internal()) = cls;
                    try { safeProcessEvent(goat, fn, b.data()); } catch (...) { continue; }
                    if (auto* pRet = findParam(fn, STR("ReturnValue")))
                    {
                        UObject* comp = *reinterpret_cast<UObject**>(b.data() + pRet->GetOffset_Internal());
                        if (comp && isObjectAlive(comp)) return comp;
                    }
                }
            }
            return nullptr;
        }

        void summonGoatToPlayer()
        {
            VLOG(STR("[MoriaCppMod] [Summon] NUM+ press — summon goat to player\n"));

            // 1. Find a live BP_NpcGoat_C actor. Try the exact class first;
            //    later versions could add subclasses.
            UObject* goat = nullptr;
            {
                std::vector<UObject*> hit;
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit))
                {
                    for (UObject* g : hit)
                        if (g && isObjectAlive(g)) { goat = g; break; }
                }
            }
            if (!goat)
            {
                VLOG(STR("[MoriaCppMod] [Summon] no live BP_NpcGoat_C in loaded chunks — bail\n"));
                showOnScreen(L"No goat in world (must be in same area)", 2.0f, 0.9f, 0.6f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Summon] target goat ptr={:p}\n"), (void*)goat);

            // 2. Read player's FVector + FRotator from the local pawn.
            UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
            if (!pawn)
            {
                VLOG(STR("[MoriaCppMod] [Summon] no local pawn — bail\n"));
                return;
            }
            float pLoc[3]  = {0,0,0};
            float pRot[3]  = {0,0,0};
            // K2_GetActorLocation() -> FVector (12 bytes, 3 floats)
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
                        pLoc[0] = fv[0]; pLoc[1] = fv[1]; pLoc[2] = fv[2];
                    }
                }
            }
            // K2_GetActorRotation() -> FRotator (12 bytes, Pitch/Yaw/Roll floats)
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
                        pRot[0] = fv[0]; pRot[1] = fv[1]; pRot[2] = fv[2];
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [Summon] player loc=({:.1f},{:.1f},{:.1f}) rot=({:.1f},{:.1f},{:.1f})\n"),
                 pLoc[0], pLoc[1], pLoc[2], pRot[0], pRot[1], pRot[2]);

            // 3. Find ServerTeleportTo on the goat.
            auto* teleFn = goat->GetFunctionByNameInChain(STR("ServerTeleportTo"));
            if (!teleFn)
            {
                VLOG(STR("[MoriaCppMod] [Summon] ServerTeleportTo not found on goat — bail\n"));
                return;
            }

            // 4. Build parms by reflection — find the FVector + FRotator
            //    StructProperty offsets in the function's parm layout.
            int psz = teleFn->GetParmsSize();
            std::vector<uint8_t> pbuf(psz, 0);
            auto* destLocProp = findParam(teleFn, STR("DestLocation"));
            auto* destRotProp = findParam(teleFn, STR("DestRotation"));
            if (!destLocProp || !destRotProp)
            {
                VLOG(STR("[MoriaCppMod] [Summon] ServerTeleportTo missing expected parms (destLoc={:p} destRot={:p})\n"),
                     (void*)destLocProp, (void*)destRotProp);
                return;
            }
            std::memcpy(pbuf.data() + destLocProp->GetOffset_Internal(), pLoc, sizeof(pLoc));
            std::memcpy(pbuf.data() + destRotProp->GetOffset_Internal(), pRot, sizeof(pRot));

            // 5. Fire it. ServerTeleportTo is a Server RPC, so the call
            //    routes through the network layer — must be invoked on the
            //    client; the server actually performs the move + replicates.
            if (!safeProcessEvent(goat, teleFn, pbuf.data()))
            {
                VLOG(STR("[MoriaCppMod] [Summon] ServerTeleportTo PE returned false\n"));
                showOnScreen(L"Summon failed (PE call rejected)", 2.0f, 0.9f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [Summon] ServerTeleportTo dispatched — goat should arrive at player\n"));
            // [rc.40] Register the summoned goat into m_followGoats so the
            // bell-toggle path can find/dismiss it later.
            if (!isGoatTracked(goat))
            {
                adoptExistingGoat(goat);
                VLOG(STR("[MoriaCppMod] [Summon] goat adopted into m_followGoats (size now {})\n"),
                     m_followGoats.size());
            }
            showOnScreen(L"Goat summoned!", 2.0f, 0.4f, 0.9f, 0.4f);
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

        UObject* findOurGoatAlive()
        {
            for (auto& rec : m_followGoats)
            {
                UObject* g = rec.pawn.Get();
                if (g && isObjectAlive(g)) return g;
            }
            return nullptr;
        }

        UObject* findAnyGoatInWorld()
        {
            std::vector<UObject*> hit;
            if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit))
            {
                for (UObject* g : hit)
                    if (g && isObjectAlive(g)) return g;
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
            m_followGoats.erase(
                std::remove_if(m_followGoats.begin(), m_followGoats.end(),
                    [](FollowGoatRecord& g) {
                        UObject* p = g.pawn.Get();
                        return !p || !isObjectAlive(p);
                    }),
                m_followGoats.end());

            // Toggle: if a tracked goat exists → dismiss, else → summon.
            if (!m_followGoats.empty())
            {
                VLOG(STR("[MoriaCppMod] [BellToggle] DISMISS (herd size {})\n"),
                     m_followGoats.size());
                despawnAllFollowGoats();
                showOnScreen(L"Goat dismissed", 2.0f, 0.7f, 0.7f, 0.7f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [BellToggle] SUMMON (no porter role — wild fauna with manual follow)\n"));
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
        //
        // Spawn path mirrors spawnFollowGoat (proven, used by NUM- earlier)
        // minus the Porter role assignment + with bellSpawned=true so the
        // tick loop drives MoveToActor follow.
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
                 (void*)m_goatBPClass, (void*)m_goatBeginSpawnFn,
                 (void*)m_goatFinishSpawnFn, (void*)m_kismetGameplayStaticsCDO);

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
                struct { FVec3f Ret; } p{};
                if (safeProcessEvent(pawn, fwdFn, &p)) fwd = p.Ret;
            }
            FVec3f spawnLoc{ loc.X + fwd.X * 300.0f,
                             loc.Y + fwd.Y * 300.0f,
                             loc.Z };
            VLOG(STR("[MoriaCppMod] [BellSpawn] pawn loc=({:.1f},{:.1f},{:.1f}) fwd=({:.2f},{:.2f},{:.2f}) → spawn=({:.1f},{:.1f},{:.1f})\n"),
                 loc.X, loc.Y, loc.Z, fwd.X, fwd.Y, fwd.Z,
                 spawnLoc.X, spawnLoc.Y, spawnLoc.Z);

            FTransformRaw xform{};
            xform.Rotation    = {0.0f, 0.0f, 0.0f, 1.0f};
            xform.Translation = spawnLoc;
            xform.Scale3D     = {1.0f, 1.0f, 1.0f};

            // BeginDeferredActorSpawnFromClass
            int sz = m_goatBeginSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>      (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), pawn);
            writeGoatParm<UClass*>       (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         m_goatBPClass);
            writeGoatParm<FTransformRaw> (m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            // 1 = AlwaysSpawn (unconditional, no collision check). Mode 4
            // is DontSpawnIfColliding (worst), mode 2 is the common
            // adjust-if-possible-but-always-spawn — I had the enum backward
            // earlier. Sticking with 1 for guaranteed spawn.
            writeGoatParm<uint8_t>       (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);

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
            bool clsAlive = (m_goatBPClass && isObjectAlive(m_goatBPClass));
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
                        VLOG(STR("[MoriaCppMod] [BellSpawn] re-resolved PRIMARY class via LoadClassAsset_Blocking: {}\n"),
                             GOAT_CLASS_PATHS[0]);
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
                            VLOG(STR("[MoriaCppMod] [BellSpawn] re-resolved class via StaticFindObject: {}\n"),
                                 path);
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
            VLOG(STR("[MoriaCppMod] [BellSpawn] pre-spawn class diag: ptr={:p} alive={} name='{}'\n"),
                 (void*)m_goatBPClass, clsAlive, clsName.c_str());

            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data()))
            {
                showOnScreen(L"Goat spawn failed (Begin)", 2.0f, 0.9f, 0.4f, 0.4f);
                VLOG(STR("[MoriaCppMod] [BellSpawn] safeProcessEvent BeginDeferred FAILED — bail\n"));
                return;
            }
            UObject* goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            VLOG(STR("[MoriaCppMod] [BellSpawn] BeginDeferred returned goat={:p} (mode=1=AlwaysSpawn)\n"),
                 (void*)goat);
            if (!goat) {
                showOnScreen(L"Goat spawn null", 2.0f, 0.9f, 0.4f, 0.4f);
                // Re-resolve the BP class fresh — pointer may have gone stale
                // if anything in the engine reloaded the asset.
                UClass* freshCls = nullptr;
                for (auto* path : GOAT_CLASS_PATHS)
                {
                    freshCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
                    if (freshCls) break;
                }
                VLOG(STR("[MoriaCppMod] [BellSpawn] null goat — actorClass(cached)={:p} freshLookup={:p} cdo={:p} pawn={:p} spawnLoc=({},{},{})\n"),
                     (void*)m_goatBPClass, (void*)freshCls, (void*)m_kismetGameplayStaticsCDO, (void*)pawn,
                     spawnLoc.X, spawnLoc.Y, spawnLoc.Z);
                if (freshCls && freshCls != m_goatBPClass)
                {
                    VLOG(STR("[MoriaCppMod] [BellSpawn] cached class was STALE — refreshing m_goatBPClass and retrying once\n"));
                    m_goatBPClass = freshCls;
                    // Retry with fresh class.
                    writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), m_goatBPClass);
                    safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data());
                    goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
                    VLOG(STR("[MoriaCppMod] [BellSpawn] retry returned goat={:p}\n"), (void*)goat);
                }
                if (!goat) return;
            }

            // FinishSpawningActor
            int sz2 = m_goatFinishSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf2(sz2, 0);
            writeGoatParm<UObject*>      (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          goat);
            writeGoatParm<FTransformRaw> (m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
            safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

            // SpawnDefaultController for AI (porter BT etc.)
            if (auto* sdc = goat->GetFunctionByNameInChain(STR("SpawnDefaultController")))
            {
                safeProcessEvent(goat, sdc, nullptr);
            }

            // [v7.2.0-rc.3 2026-05-22] Auto-equip Tobi's saddlebag onto the
            // goat's MorEquipComponent. Fills the empty slot wrapper that v1.5.0
            // ships with so clicking "Saddlebags" in the menu opens real
            // storage. Gated by [GoatExperimental] AutoEquipSaddleBag.
            equipPorterSaddlebag(goat);

            // [v7.2.0-rc.12a 2026-05-22] Spawn phantom chest above goat for
            // OpenChest broker testing. Gated by [GoatExperimental]
            // PhantomChest = true (default OFF). For 12a: chest is visible
            // 200 cm above goat so user can walk to it and press E to
            // confirm vanilla chest UI opens.
            spawnPhantomChestNearGoat(goat);

            // [v7.2.0-rc.12b 2026-05-23] Alternative: spawn the actual
            // saddlebag (BP_SaddleBags_Goat_C) as a world actor in front
            // of the goat, probe its open/use UFunctions, auto-call the
            // first one. Gated by [GoatExperimental] SaddlebagAtGoat = true.
            spawnSaddlebagAtGoat(goat);

            // [v7.2.0-rc.12b.2 2026-05-23] Parallel attempt: add the bag
            // directly to the goat's MorInventoryComponent via
            // ServerDebugSetItem. Tests whether the bag-on-goat
            // architecture is achievable from DLL side. Same INI flag.
            addSaddlebagToGoatInventory(goat);

            // [rc.49.1 2026-05-12] v1.3.0 modded BP_NpcGoat carries porter-mode
            // wiring natively (MorWandererComponent + cloned MorNPC interaction
            // structs + DefaultContainers wiring + porter-aware CDO defaults).
            //
            // Per desktop's v1.3.0 brief, the runtime sequence simplifies to:
            //   1. SpawnActor<BP_NpcGoat_C> ← done above
            //   2. SpawnDefaultController     ← done above
            //   3. SetIsInteractive(true)     ← belt-and-suspenders (CDO already
            //                                   wires interaction; safe to re-fire)
            //   DROPPED: SetRoleFuzzy("Porter")     — BP wiring carries this natively
            //   DROPPED: RegisterWithNPCManager     — was the dwarf-treatment culprit
            {
                UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
                if (npcCompCls)
                {
                    if (auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
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
                                    struct { bool bValue{true}; } sip{};
                                    safeProcessEvent(npcComp, siFn, &sip);
                                    VLOG(STR("[MoriaCppMod] [BellSpawn] SetIsInteractive(true) fired (safety belt)\n"));
                                }
                            }
                        }
                    }
                }
            }

            // [rc.47.1 2026-05-12] DON'T touch interaction flags. Original
            // spawnFollowGoat() (which user confirmed worked perfectly for
            // NUM-) leaves all flags alone after assignPorterRole. Disabling
            // bManageInteractionEnabled in rc.47 likely toggled internal
            // state that gates porter follow behavior.
            //
            // Cost: dwarf-style "Standing By" UI will appear on E-interact.
            // Tradeoff: working follow > clean UI. The UI suppression lands
            // in rc.48 via a widget-show hook (intercept the manage widget's
            // show event and close it when target is our bell-spawned goat).

            // Track in m_followGoats with bellSpawned=true. tickFollowGoats
            // checks this flag to: (a) skip the SetIsInteractive(true) fixup
            // that would re-enable vanilla UI, (b) run the manual MoveToActor
            // follow tick (porter BT path is unreliable without porter role).
            FollowGoatRecord rec{};
            rec.pawn               = RC::Unreal::FWeakObjectPtr(goat);
            rec.controller         = RC::Unreal::FWeakObjectPtr();
            rec.bellSpawned        = true;
            rec.interactiveRefired = true;  // suppress the fixup
            m_followGoats.push_back(rec);

            VLOG(STR("[MoriaCppMod] [BellSpawn] goat spawned at ({:.1f},{:.1f},{:.1f}); herd size={}\n"),
                 spawnLoc.X, spawnLoc.Y, spawnLoc.Z, m_followGoats.size());
            showOnScreen(L"Goat summoned!", 2.0f, 0.4f, 0.9f, 0.4f);

            // [Phase 3] Assign Porter role so vanilla menu header shows
            // "Porter Goat" instead of "Citizen". Requires v1.3.8 pak with
            // Porter.EnabledState=Live; pre-v1.3.8 this is a silent no-op.
            UClass* npcCompCls2 = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (npcCompCls2)
            {
                if (auto* getCompFn2 = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> gb(getCompFn2->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getCompFn2, gb.data(), STR("ComponentClass"), npcCompCls2);
                    if (safeProcessEvent(goat, getCompFn2, gb.data()))
                    {
                        UObject* npc = readGoatParm<UObject*>(getCompFn2, gb.data(), STR("ReturnValue"), nullptr);
                        if (npc && isObjectAlive(npc))
                        {
                            setGoatRole(npc, STR("Porter"));
                            // [Desktop probe] Read back GetCurrentRole to
                            // confirm whether SetRole(Porter) propagated.
                            probeCurrentRole(npc);
                        }
                    }
                }
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
            UClass* goatCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
            if (!goatCls)
            {
                goatCls = goat_loadClassAssetBlocking(
                    STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
            }
            if (!goatCls)
            {
                VLOG(STR("[MoriaCppMod] [Spawn] BP_NpcGoat_C class missing — bail\n"));
                return nullptr;
            }

            UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
            UObject* pc   = m_localPC   && isObjectAlive(m_localPC)   ? m_localPC   : nullptr;
            if (!pawn || !pc) { VLOG(STR("[MoriaCppMod] [Spawn] no PC/pawn\n")); return nullptr; }

            // 2. Player loc + rot.
            float pLoc[3] = {0,0,0};
            float pRot[3] = {0,0,0};
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
                        pLoc[0] = fv[0]; pLoc[1] = fv[1]; pLoc[2] = fv[2];
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
                        pRot[0] = fv[0]; pRot[1] = fv[1]; pRot[2] = fv[2];
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [Spawn] player loc=({:.1f},{:.1f},{:.1f})\n"),
                 pLoc[0], pLoc[1], pLoc[2]);

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
            float spawnLoc[3] = {
                pLoc[0] + fwdX * 200.0f,
                pLoc[1] + fwdY * 200.0f,
                pLoc[2] - 70.0f
            };
            VLOG(STR("[MoriaCppMod] [Spawn] requested loc=({:.1f},{:.1f},{:.1f}) (player + fwd*200, Z-70)\n"),
                 spawnLoc[0], spawnLoc[1], spawnLoc[2]);

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
            auto* beginFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr,
                STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
            auto* gsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                STR("/Script/Engine.Default__GameplayStatics"));
            if (!beginFn || !gsCDO)
            {
                VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferredActorSpawnFromClass unresolved (fn={:p} cdo={:p})\n"),
                     (void*)beginFn, (void*)gsCDO);
                return nullptr;
            }

            int sz = beginFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* pWC    = findParam(beginFn, STR("WorldContextObject"));
            auto* pCls   = findParam(beginFn, STR("ActorClass"));
            auto* pXform = findParam(beginFn, STR("SpawnTransform"));
            auto* pColl  = findParam(beginFn, STR("CollisionHandlingOverride"));
            auto* pOwner = findParam(beginFn, STR("Owner"));
            auto* pRet   = findParam(beginFn, STR("ReturnValue"));
            if (!pWC || !pCls || !pXform || !pColl || !pRet)
            {
                VLOG(STR("[MoriaCppMod] [Spawn] BeginDeferred parm missing (WC={:p} Cls={:p} Xform={:p} Coll={:p} Ret={:p})\n"),
                     (void*)pWC, (void*)pCls, (void*)pXform, (void*)pColl, (void*)pRet);
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
            auto* finishFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr,
                STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));
            if (!finishFn)
            {
                VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawningActor missing — returning deferred actor (may be partial spawn)\n"));
                return deferred;
            }

            int sz2 = finishFn->GetParmsSize();
            std::vector<uint8_t> buf2(sz2, 0);
            auto* pActor   = findParam(finishFn, STR("Actor"));
            auto* pXform2  = findParam(finishFn, STR("SpawnTransform"));
            auto* pRet2    = findParam(finishFn, STR("ReturnValue"));
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
            VLOG(STR("[MoriaCppMod] [Spawn] FinishSpawning returned actor ptr={:p}\n"),
                 (void*)finalActor);

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
                    UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
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
                                    bool* p = npcComp->GetValuePtrByPropertyNameInChain<bool>(name);
                                    if (p)
                                    {
                                        *p = val;
                                        VLOG(STR("[MoriaCppMod] [Spawn] set {}={}\n"),
                                             name, val ? STR("true") : STR("false"));
                                    }
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
                            VLOG(STR("[MoriaCppMod] [Spawn-Diag] actual goat loc=({:.1f},{:.1f},{:.1f})\n"),
                                 fv[0], fv[1], fv[2]);
                        }
                    }
                }
                // AI Controller — does the goat have one? Without it, no behavior tree.
                UObject** ctrlPtr = check->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
                if (ctrlPtr)
                {
                    UObject* ctrl = *ctrlPtr;
                    std::wstring ctrlCls = ctrl && isObjectAlive(ctrl) ? safeClassName(ctrl) : L"<null>";
                    VLOG(STR("[MoriaCppMod] [Spawn-Diag] Controller={:p} class={}\n"),
                         (void*)ctrl, ctrlCls.c_str());
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [Spawn-Diag] Controller property not found on goat\n"));
                }
                // Components walk — see what's attached.
                UClass* aCls = nullptr;
                try { aCls = check->GetClassPrivate(); } catch (...) {}
                if (aCls)
                {
                    FProperty* bpccProp = nullptr;
                    for (auto* strct = static_cast<UStruct*>(aCls); strct && !bpccProp;
                         strct = strct->GetSuperStruct())
                    {
                        for (auto* p : strct->ForEachProperty())
                        {
                            std::wstring pn;
                            try { pn = p->GetName(); } catch (...) {}
                            if (pn == STR("BlueprintCreatedComponents")) { bpccProp = p; break; }
                        }
                    }
                    if (bpccProp)
                    {
                        uint8_t* slot = reinterpret_cast<uint8_t*>(check) + bpccProp->GetOffset_Internal();
                        UObject** data = *reinterpret_cast<UObject***>(slot + 0);
                        int32_t num    = *reinterpret_cast<int32_t*>(slot + 8);
                        VLOG(STR("[MoriaCppMod] [Spawn-Diag] BlueprintCreatedComponents count={}\n"), num);
                        for (int32_t i = 0; i < num && i < 40; ++i)
                        {
                            UObject* c = data[i];
                            if (!c || !isObjectAlive(c)) continue;
                            std::wstring cName, cCls;
                            try { cName = c->GetName(); } catch (...) {}
                            try { cCls = c->GetClassPrivate()->GetName(); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [Spawn-Diag]   [{}] {} : {}\n"),
                                 i, cName.c_str(), cCls.c_str());
                        }
                    }
                }
                // Actor bHidden flag.
                bool* hiddenPtr = check->GetValuePtrByPropertyNameInChain<bool>(STR("bHidden"));
                if (hiddenPtr)
                {
                    VLOG(STR("[MoriaCppMod] [Spawn-Diag] bHidden={}\n"),
                         *hiddenPtr ? STR("true") : STR("false"));
                }
            }

            return finalActor ? finalActor : deferred;
        }

        void dumpSettlementManagerRescueState(UObject* worldCtx)
        {
            VLOG(STR("[MoriaCppMod] [Probe] === BP_MorSettlementManager rescue-state dump ===\n"));
            if (!worldCtx) { VLOG(STR("[MoriaCppMod] [Probe] no world ctx\n")); return; }
            auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
            if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
            if (!mgrCls) { VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr class missing\n")); return; }
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkCDO) return;
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       mgrCls);
            if (!safeProcessEvent(fgkCDO, getMgrFn, buf.data())) return;
            UObject* mgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton missing\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton={:p} cls={}\n"),
                 (void*)mgr, safeClassName(mgr).c_str());

            // Find the goat actor up front so we can flag matches by ptr identity.
            UObject* deepsGoat = nullptr;
            {
                std::vector<UObject*> hit;
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit))
                    for (UObject* g : hit) if (g && isObjectAlive(g)) { deepsGoat = g; break; }
            }
            VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match detection): {:p}\n"),
                 (void*)deepsGoat);

            // Walk full super chain.
            int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0;
            for (auto* strct = static_cast<UStruct*>(mgr->GetClassPrivate());
                 strct; strct = strct->GetSuperStruct())
            {
                std::wstring scopeCls;
                try { scopeCls = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    std::wstring pn, tn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { tn = prop->GetClass().GetName(); } catch (...) {}
                    unsigned off = (unsigned)prop->GetOffset_Internal();
                    uint8_t* slot = reinterpret_cast<uint8_t*>(mgr) + off;

                    if (tn == STR("ArrayProperty"))
                    {
                        auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                        FProperty* inner = nullptr;
                        try { inner = arrProp->GetInner(); } catch (...) {}
                        std::wstring innerTn;
                        if (inner) { try { innerTn = inner->GetClass().GetName(); } catch (...) {} }
                        void**   data = reinterpret_cast<void**>  (slot + 0);
                        int32_t* num  = reinterpret_cast<int32_t*>(slot + 8);
                        VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
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
                                try { eName = e->GetName(); } catch (...) {}
                                try { eCls = e->GetClassPrivate()->GetName(); } catch (...) {}
                                bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                                VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"),
                                     j, eName.c_str(), eCls.c_str(),
                                     isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                            }
                        }
                        ++arrCount;
                    }
                    else if (tn == STR("BoolProperty"))
                    {
                        // BoolProperty has a bitmask; cheap read just looks at byte 0.
                        uint8_t b = *slot;
                        VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"),
                             scopeCls.c_str(), pn.c_str(), off, b);
                        ++boolCount;
                    }
                    else if (tn == STR("IntProperty"))
                    {
                        int32_t v = *reinterpret_cast<int32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"),
                             scopeCls.c_str(), pn.c_str(), off, v);
                        ++intCount;
                    }
                    else if (tn == STR("NameProperty"))
                    {
                        // Safe: reading raw FName bytes without ToString.
                        // Log the comparison index only.
                        uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
                             fnameRaw[0], fnameRaw[1]);
                        ++nameCount;
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [Probe] === end settlement-mgr dump ({} arrays, {} bools, {} ints, {} names) ===\n"),
                 arrCount, boolCount, intCount, nameCount);
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
            VLOG(STR("[MoriaCppMod] [Probe] PC ptr={:p} class={}\n"),
                 (void*)pc, safeClassName(pc).c_str());

            // Find the goat ptr up-front so we can flag pointer matches.
            UObject* deepsGoat = nullptr;
            {
                std::vector<UObject*> hit;
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit))
                    for (UObject* g : hit) if (g && isObjectAlive(g)) { deepsGoat = g; break; }
            }
            VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match): {:p}\n"),
                 (void*)deepsGoat);

            int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0, objCount = 0;
            for (auto* strct = static_cast<UStruct*>(pc->GetClassPrivate());
                 strct; strct = strct->GetSuperStruct())
            {
                std::wstring scopeCls;
                try { scopeCls = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    std::wstring pn, tn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { tn = prop->GetClass().GetName(); } catch (...) {}
                    unsigned off = (unsigned)prop->GetOffset_Internal();
                    uint8_t* slot = reinterpret_cast<uint8_t*>(pc) + off;

                    if (tn == STR("ObjectProperty"))
                    {
                        UObject* o = *reinterpret_cast<UObject**>(slot);
                        if (!o || (uintptr_t)o < 0x10000) continue;
                        if (!isObjectAlive(o)) continue;
                        std::wstring oName, oCls;
                        try { oName = o->GetName(); } catch (...) {}
                        try { oCls = o->GetClassPrivate()->GetName(); } catch (...) {}
                        bool isGoat = (o == deepsGoat) || (oCls == STR("BP_NpcGoat_C"));
                        VLOG(STR("[MoriaCppMod] [Probe]   OBJ  [{}].{} off=0x{:X} ptr={:p} name={} cls={}{}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
                             (void*)o, oName.c_str(), oCls.c_str(),
                             isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                        ++objCount;
                    }
                    else if (tn == STR("ArrayProperty"))
                    {
                        auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                        FProperty* inner = nullptr;
                        try { inner = arrProp->GetInner(); } catch (...) {}
                        std::wstring innerTn;
                        if (inner) { try { innerTn = inner->GetClass().GetName(); } catch (...) {} }
                        void**   data = reinterpret_cast<void**>  (slot + 0);
                        int32_t* num  = reinterpret_cast<int32_t*>(slot + 8);
                        VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
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
                                try { eName = e->GetName(); } catch (...) {}
                                try { eCls = e->GetClassPrivate()->GetName(); } catch (...) {}
                                bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                                VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"),
                                     j, eName.c_str(), eCls.c_str(),
                                     isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                            }
                        }
                        ++arrCount;
                    }
                    else if (tn == STR("BoolProperty"))
                    {
                        uint8_t b = *slot;
                        VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"),
                             scopeCls.c_str(), pn.c_str(), off, b);
                        ++boolCount;
                    }
                    else if (tn == STR("IntProperty"))
                    {
                        int32_t v = *reinterpret_cast<int32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"),
                             scopeCls.c_str(), pn.c_str(), off, v);
                        ++intCount;
                    }
                    else if (tn == STR("NameProperty"))
                    {
                        uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
                             fnameRaw[0], fnameRaw[1]);
                        ++nameCount;
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [Probe] === end PC dump ({} arrays, {} bools, {} ints, {} names, {} objects) ===\n"),
                 arrCount, boolCount, intCount, nameCount, objCount);
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
            if (!worldCtx) { VLOG(STR("[MoriaCppMod] [Probe] no world ctx\n")); return; }
            auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
            if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
            if (!mgrCls) { VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr class missing\n")); return; }
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkCDO) return;
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       mgrCls);
            if (!safeProcessEvent(fgkCDO, getMgrFn, buf.data())) return;
            UObject* mgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr singleton missing\n"));
                return;
            }
            UClass* liveCls = mgr->GetClassPrivate();
            if (!liveCls) return;
            VLOG(STR("[MoriaCppMod] [Probe] settlement-mgr live class={}\n"),
                 safeClassName(mgr).c_str());

            int count = 0;
            for (auto* fn : liveCls->ForEachFunctionInChain())
            {
                if (count >= 600) break;
                std::wstring fnName, ownerName;
                try { fnName = fn->GetName(); } catch (...) {}
                try {
                    if (auto* outer = fn->GetOuterPrivate())
                        ownerName = outer->GetName();
                } catch (...) {}
                std::wstring paramSig;
                int parmCount = 0;
                for (auto* prop : fn->ForEachProperty())
                {
                    if (parmCount >= 6) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                    if (!paramSig.empty()) paramSig += STR(", ");
                    paramSig += pcn + STR(" ") + pn;
                    ++parmCount;
                }
                VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"),
                     ownerName.c_str(), fnName.c_str(), paramSig.c_str());
                ++count;
            }
            VLOG(STR("[MoriaCppMod] [Probe] === end settlement-mgr UFunction enum ({} fns) ===\n"),
                 count);
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
                UClass* cls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, classPath);
                if (!cls)
                {
                    VLOG(STR("[MoriaCppMod] [Probe] {} class not loaded (path={})\n"),
                         label, classPath);
                    return;
                }
                VLOG(STR("[MoriaCppMod] [Probe] --- {} ({}) UFunctions ---\n"),
                     label, classPath);
                int count = 0;
                for (auto* fn : cls->ForEachFunctionInChain())
                {
                    if (count >= maxFns) break;
                    std::wstring fnName, ownerName;
                    try { fnName = fn->GetName(); } catch (...) {}
                    try {
                        if (auto* outer = fn->GetOuterPrivate())
                            ownerName = outer->GetName();
                    } catch (...) {}
                    std::wstring paramSig;
                    int parmCount = 0;
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (parmCount >= 6) break;
                        std::wstring pn, pcn;
                        try { pn = prop->GetName(); } catch (...) {}
                        try { pcn = prop->GetClass().GetName(); } catch (...) {}
                        if (!paramSig.empty()) paramSig += STR(", ");
                        paramSig += pcn + STR(" ") + pn;
                        ++parmCount;
                    }
                    VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"),
                         ownerName.c_str(), fnName.c_str(), paramSig.c_str());
                    ++count;
                }
                VLOG(STR("[MoriaCppMod] [Probe] --- end {} ({} UFunctions) ---\n"),
                     label, count);
            };

            // Goat actor's class chain
            dumpClass(STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"),
                      STR("BP_NpcGoat_C"), 400);

            // Goat's AI controller — desktop confirmed the class path
            dumpClass(STR("/Game/Character/NpcGoat/BP_NpcGoat_AIController.BP_NpcGoat_AIController_C"),
                      STR("BP_NpcGoat_AIController_C"), 400);

            // FaunaBase parent — common base for animal AI controllers
            dumpClass(STR("/Game/Character/Fauna/BP_AIController_FaunaBase.BP_AIController_FaunaBase_C"),
                      STR("BP_AIController_FaunaBase_C"), 200);

            // Look up the LIVE goat actor's actual AI controller — gives us
            // class name even if the path guess above is wrong.
            std::vector<UObject*> hit;
            if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit) && !hit.empty())
            {
                UObject* goat = nullptr;
                for (UObject* g : hit) if (g && isObjectAlive(g)) { goat = g; break; }
                if (goat)
                {
                    // APawn::Controller UPROPERTY (ObjectProperty).
                    UObject** controllerPtr = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
                    if (controllerPtr && *controllerPtr && isObjectAlive(*controllerPtr))
                    {
                        UObject* ctrl = *controllerPtr;
                        std::wstring ctrlClsName = safeClassName(ctrl);
                        VLOG(STR("[MoriaCppMod] [Probe] LIVE goat AI controller: ptr={:p} class={}\n"),
                             (void*)ctrl, ctrlClsName.c_str());
                        // Enumerate UFunctions on that live class
                        // regardless of class-path guess.
                        UClass* liveCls = ctrl->GetClassPrivate();
                        if (liveCls)
                        {
                            VLOG(STR("[MoriaCppMod] [Probe] --- LIVE controller ({}) UFunctions ---\n"),
                                 ctrlClsName.c_str());
                            int count = 0;
                            for (auto* fn : liveCls->ForEachFunctionInChain())
                            {
                                if (count >= 600) break;
                                std::wstring fnName, ownerName;
                                try { fnName = fn->GetName(); } catch (...) {}
                                try {
                                    if (auto* outer = fn->GetOuterPrivate())
                                        ownerName = outer->GetName();
                                } catch (...) {}
                                std::wstring paramSig;
                                int parmCount = 0;
                                for (auto* prop : fn->ForEachProperty())
                                {
                                    if (parmCount >= 6) break;
                                    std::wstring pn, pcn;
                                    try { pn = prop->GetName(); } catch (...) {}
                                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                                    if (!paramSig.empty()) paramSig += STR(", ");
                                    paramSig += pcn + STR(" ") + pn;
                                    ++parmCount;
                                }
                                VLOG(STR("[MoriaCppMod] [Probe]   [{}] {}({})\n"),
                                     ownerName.c_str(), fnName.c_str(), paramSig.c_str());
                                ++count;
                            }
                            VLOG(STR("[MoriaCppMod] [Probe] --- end LIVE controller ({} UFunctions) ---\n"),
                                 count);
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
        std::vector<RC::Unreal::FWeakObjectPtr> m_recruitedGoatsPending;
        bool isGoatPendingRecruit(UObject* goat)
        {
            for (auto& wp : m_recruitedGoatsPending)
                if (wp.Get() == goat) return true;
            return false;
        }
        void trackGoatPendingRecruit(UObject* goat)
        {
            if (isGoatPendingRecruit(goat)) return;
            m_recruitedGoatsPending.push_back(RC::Unreal::FWeakObjectPtr(goat));
        }
        void untrackGoatPendingRecruit(UObject* goat)
        {
            m_recruitedGoatsPending.erase(
                std::remove_if(m_recruitedGoatsPending.begin(), m_recruitedGoatsPending.end(),
                    [goat](RC::Unreal::FWeakObjectPtr& wp) {
                        UObject* p = wp.Get();
                        return !p || p == goat;
                    }),
                m_recruitedGoatsPending.end());
        }

        // Helper: shallow-copy a single named UPROPERTY from src to dst when
        // both objects share a class. Used for cloning proximity-menu row
        // fields (Interactable, Interactor, InteractComponent) from a
        // template without hardcoding their byte offsets — survives any
        // FGK widget layout shift.
        bool copyRowPropertyByName(UObject* dst, UObject* src, const wchar_t* name)
        {
            if (!dst || !src) return false;
            UClass* cls = nullptr;
            try { cls = dst->GetClassPrivate(); } catch (...) {}
            if (!cls) return false;
            RC::Unreal::FProperty* found = nullptr;
            try {
                for (auto* p : cls->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    std::wstring pn;
                    try { pn = p->GetName(); } catch (...) { continue; }
                    if (pn == name) { found = p; break; }
                }
            } catch (...) {}
            if (!found) return false;
            int32 off = -1;
            int32 size = -1;
            try { off = found->GetOffset_Internal(); size = found->GetElementSize(); } catch (...) {}
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
        bool appendMulticastEntry(uint8_t* listSlot, UObject* obj, const wchar_t* fnName,
                                  const wchar_t* tag)
        {
            void**   listData = reinterpret_cast<void**>(listSlot + 0);
            int32_t* listNum  = reinterpret_cast<int32_t*>(listSlot + 8);
            int32_t* listMax  = reinterpret_cast<int32_t*>(listSlot + 12);

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
                        VLOG(STR("[MoriaCppMod] [Recruit/{}] obj already subscribed at slot {}\n"),
                             tag, i);
                        return true;
                    }
                }
            }
            if (*listNum >= *listMax)
            {
                VLOG(STR("[MoriaCppMod] [Recruit/{}] no headroom (num={} max={}) — would need realloc\n"),
                     tag, *listNum, *listMax);
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
            VLOG(STR("[MoriaCppMod] [Recruit/{}] appended obj={:p} fn={} ; new num={}\n"),
                 tag, (void*)obj, fnName, *listNum);
            return true;
        }

        // Full setup. Idempotent. Returns true if all steps succeeded.
        bool setupGoatRecruit(UObject* goat)
        {
            if (!goat || !isObjectAlive(goat)) return false;

            // 1. Get MorWandererComponent on the goat.
            UClass* wandCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorWandererComponent"));
            if (!wandCls)
            {
                VLOG(STR("[MoriaCppMod] [Recruit] MorWandererComponent class not loaded\n"));
                return false;
            }
            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (!getCompFn) return false;
            int sz = getCompFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), wandCls);
            if (!safeProcessEvent(goat, getCompFn, buf.data())) return false;
            UObject* wandComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!wandComp || !isObjectAlive(wandComp))
            {
                VLOG(STR("[MoriaCppMod] [Recruit] goat has no MorWandererComponent\n"));
                return false;
            }
            VLOG(STR("[MoriaCppMod] [Recruit] goat={:p} wanderer={:p}\n"), (void*)goat, (void*)wandComp);

            // 2. Enable Recruit interaction. Reflective bool writes survive
            // FGK component layout shifts on DLC; the UFunction setter below
            // covers any side-effects the property writes alone skip.
            uint8_t* base = reinterpret_cast<uint8_t*>(wandComp);
            if (auto* p = wandComp->GetValuePtrByPropertyNameInChain<bool>(STR("bRecruitInteractionRegister")))
                *p = true;
            if (auto* p = wandComp->GetValuePtrByPropertyNameInChain<bool>(STR("bRecruitInteractionEnabled")))
                *p = true;
            VLOG(STR("[MoriaCppMod] [Recruit] flipped bRecruitInteractionRegister + bRecruitInteractionEnabled = true\n"));
            if (auto* setEnFn = wandComp->GetFunctionByNameInChain(STR("SetRecruitInteractionEnabled")))
            {
                struct { bool Val{true}; } sip{};
                safeProcessEvent(wandComp, setEnFn, &sip);
                VLOG(STR("[MoriaCppMod] [Recruit] SetRecruitInteractionEnabled(true) fired\n"));
            }

            // 3. Append FScriptDelegate to component's OnWandererRecruited
            // multicast. Reflectively resolved so FGK layout shifts on DLC
            // don't silently corrupt neighboring bytes.
            uint8_t* recruitListSlot = wandComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("OnWandererRecruited"));
            if (recruitListSlot)
                appendMulticastEntry(recruitListSlot, goat, kGoatRecruitMarkerFn, STR("OnRecruited"));
            else
                VLOG(STR("[MoriaCppMod] [Recruit] OnWandererRecruited property not found via reflection\n"));

            // 4. Subscribe goat to settlement-mgr's OnNpcRescued (existing
            // path — kept because the rescue-state probe correlation in
            // earlier sessions proved subscriber-presence is the gate
            // for the (E) Rescue prompt to surface).
            UObject* worldCtx = m_localPC && isObjectAlive(m_localPC) ? m_localPC :
                                (m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr);
            if (worldCtx)
            {
                auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
                if (!mgrCls) mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
                auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
                auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
                if (mgrCls && getMgrFn && fgkUtilsCDO)
                {
                    int gsz = getMgrFn->GetParmsSize();
                    std::vector<uint8_t> gbuf(gsz, 0);
                    writeGoatParm<UObject*>(getMgrFn, gbuf.data(), STR("WorldContextObject"), worldCtx);
                    writeGoatParm<UClass*> (getMgrFn, gbuf.data(), STR("ManagerClass"),       mgrCls);
                    if (safeProcessEvent(fgkUtilsCDO, getMgrFn, gbuf.data()))
                    {
                        UObject* settleMgr = readGoatParm<UObject*>(getMgrFn, gbuf.data(), STR("ReturnValue"), nullptr);
                        if (settleMgr && isObjectAlive(settleMgr))
                        {
                            FProperty* delegateProp = nullptr;
                            for (auto* strct = static_cast<UStruct*>(settleMgr->GetClassPrivate());
                                 strct && !delegateProp; strct = strct->GetSuperStruct())
                            {
                                for (auto* prop : strct->ForEachProperty())
                                {
                                    std::wstring pn;
                                    try { pn = prop->GetName(); } catch (...) {}
                                    if (pn == STR("OnNpcRescued")) { delegateProp = prop; break; }
                                }
                            }
                            if (delegateProp)
                            {
                                uint8_t* slot = reinterpret_cast<uint8_t*>(settleMgr) + delegateProp->GetOffset_Internal();
                                appendMulticastEntry(slot, goat, STR("OnNpcRescued_Event_2"), STR("OnNpcRescued"));
                            }
                        }
                    }
                }
            }

            // 5. Track the goat for the PE-pre detector (handler path).
            trackGoatPendingRecruit(goat);
            VLOG(STR("[MoriaCppMod] [Recruit] tracked goat={:p} (pending recruit detection)\n"), (void*)goat);

            return true;
        }

        // Handler — called when we detect the recruit fired on this goat
        // (via PE-pre dispatching our marker-fn name). Replicates the
        // dwarf wanderer post-recruit chain that the goat's BP graph
        // can't do.
        void onGoatRecruited(UObject* goat)
        {
            if (!goat || !isObjectAlive(goat)) return;
            VLOG(STR("[MoriaCppMod] [Recruit] *** post-recruit handler firing on goat={:p} ***\n"),
                 (void*)goat);
            untrackGoatPendingRecruit(goat);

            // Get MorNPCComponent and call RegisterWithNPCManager so the
            // goat enters the persistent NPC roster (correct NpcGuid path).
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (npcCompCls)
            {
                auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (getCompFn)
                {
                    int sz = getCompFn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);
                    writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
                    if (safeProcessEvent(goat, getCompFn, buf.data()))
                    {
                        UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                        if (npcComp && isObjectAlive(npcComp))
                        {
                            if (auto* regFn = npcComp->GetFunctionByNameInChain(STR("RegisterWithNPCManager")))
                            {
                                safeProcessEvent(npcComp, regFn, nullptr);
                                VLOG(STR("[MoriaCppMod] [Recruit] RegisterWithNPCManager fired\n"));
                            }
                            // [v1.2.8 RESCUE-CRASH FIX 2026-05-10] Drop
                            // RegisterToInteractableManager. It added the
                            // goat to the player's CurrentInteracts list,
                            // and after we destroyed the actor the dead
                            // pointer crashed MorInteractComponent::
                            // GetNearestInteractableOfType (next Rope
                            // state CanActivate walk). Goat is already
                            // an interactable from spawn-time; no
                            // re-registration needed.
                            //
                            // [v1.2.8 FIX 2 2026-05-10] SendBackToSettlement
                            // (no args) needs a pre-assigned settlement
                            // target our goat doesn't have. Use the
                            // parameterized RPC: MorPlayerController::
                            // ServerSendNpcToSettlement(NpcGuid, waypointId=0)
                            // which explicitly assigns + relocates.
                            // Reuses our existing assignGoatToSettlement
                            // helper.
                            assignGoatToSettlement(npcComp, /*waypointId=*/0);
                        }
                    }
                }
            }
            // [v1.2.8 RESCUE-CRASH FIX 2026-05-10] Removed manual
            // K2_DestroyActor — it left stale pointers in the player's
            // MorInteractComponent.CurrentInteracts list and crashed the
            // next tick. SendBackToSettlement above does the proper
            // teardown. If SendBackToSettlement didn't despawn the goat,
            // the goat stays alive but is registered and persists.
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
            UObject* worldCtx = m_localPC && isObjectAlive(m_localPC) ? m_localPC :
                                (m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr);
            if (!worldCtx)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] no world context — bail\n"));
                return;
            }

            // 1. Find BP_MorSettlementManager singleton.
            auto* mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_MorSettlementManager.BP_MorSettlementManager_C"));
            if (!mgrCls)
                mgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorSettlementManager"));
            if (!mgrCls)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] settlement manager class missing\n"));
                return;
            }
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkUtilsCDO)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] FGKUtils::GetManager unresolvable\n"));
                return;
            }
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       mgrCls);
            if (!safeProcessEvent(fgkUtilsCDO, getMgrFn, buf.data())) return;
            UObject* settleMgr = readGoatParm<UObject*>(getMgrFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!settleMgr || !isObjectAlive(settleMgr))
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] settlement manager singleton missing\n"));
                return;
            }

            // 2. Find OnNpcRescued multicast property (walks super chain).
            FProperty* delegateProp = nullptr;
            for (auto* strct = static_cast<UStruct*>(settleMgr->GetClassPrivate());
                 strct && !delegateProp; strct = strct->GetSuperStruct())
            {
                for (auto* prop : strct->ForEachProperty())
                {
                    std::wstring pn;
                    try { pn = prop->GetName(); } catch (...) {}
                    if (pn == STR("OnNpcRescued")) { delegateProp = prop; break; }
                }
            }
            if (!delegateProp)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] OnNpcRescued property not found\n"));
                return;
            }
            uint8_t* delegateSlot = reinterpret_cast<uint8_t*>(settleMgr) + delegateProp->GetOffset_Internal();
            void**   listData = reinterpret_cast<void**>(delegateSlot + 0);
            int32_t* listNum  = reinterpret_cast<int32_t*>(delegateSlot + 8);
            int32_t* listMax  = reinterpret_cast<int32_t*>(delegateSlot + 12);
            VLOG(STR("[MoriaCppMod] [SubGoat] InvocationList state pre-append: num={} max={} data={:p}\n"),
                 *listNum, *listMax, *listData);

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
                    if (g && isObjectAlive(g)) { goat = g; goatClsHit = cn; break; }
                }
                if (goat) break;
            }
            if (!goat)
            {
                VLOG(STR("[MoriaCppMod] [SubGoat] no goat class found in world (tried 9 candidates) — must be near deeps goat AND class must match\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [SubGoat] target goat={:p} matched on class={} (actual cls={})\n"),
                 (void*)goat, goatClsHit.c_str(), safeClassName(goat).c_str());

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
                VLOG(STR("[MoriaCppMod] [SubGoat] no headroom (num={} max={}) — would need realloc; skipping\n"),
                     *listNum, *listMax);
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

            VLOG(STR("[MoriaCppMod] [SubGoat] appended: goat={:p} fn=OnNpcRescued_Event_2 ; new num={}\n"),
                 (void*)goat, *listNum);
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
            UClass* goatCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
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
                    for (UObject* o : hit) goats.push_back(o);
                }
            }
            if (goats.empty())
            {
                VLOG(STR("[MoriaCppMod] [Probe] no goat-class instances in loaded world (tried 9 candidates)\n"));
                return;
            }
            // Resolve MorNPCComponent class once.
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!npcCompCls) return;

            for (size_t i = 0; i < goats.size() && i < 4; ++i)
            {
                UObject* g = goats[i];
                if (!g || !isObjectAlive(g)) continue;
                std::wstring gName;
                try { gName = g->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [Probe]   goat[{}] {} ptr={:p}\n"),
                     i, gName.c_str(), (void*)g);

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
                    bool* p = npcComp->GetValuePtrByPropertyNameInChain<bool>(name);
                    if (!p) return STR("(not found)");
                    return *p ? STR("true") : STR("false");
                };
                VLOG(STR("[MoriaCppMod] [Probe]     bIsRescued                 = {}\n"),
                     readBool(STR("bIsRescued")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bInteractionEnabled        = {}\n"),
                     readBool(STR("bInteractionEnabled")).c_str());
                // [v1.2.9 BASELINE 2026-05-10] Log NpcGuid value (not
                // just existence) — desktop wants to confirm BeginPlay
                // auto-init populated it post-whitelist re-add.
                uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                if (guidPtr)
                {
                    uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
                    bool isZero = (g[0] | g[1] | g[2] | g[3]) == 0;
                    VLOG(STR("[MoriaCppMod] [Probe]     NpcGuid                    = {:08X}-{:08X}-{:08X}-{:08X} {}\n"),
                         g[0], g[1], g[2], g[3],
                         isZero ? STR("(ZERO — not registered)") : STR("(non-zero — registered)"));
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [Probe]     NpcGuid                    = (property not found)\n"));
                }
                VLOG(STR("[MoriaCppMod] [Probe]     bRescueInteractionEnabled  = {}\n"),
                     readBool(STR("bRescueInteractionEnabled")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bRescueInteractionRegister = {}\n"),
                     readBool(STR("bRescueInteractionRegister")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bManageInteractionEnabled  = {}\n"),
                     readBool(STR("bManageInteractionEnabled")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bRecruitInteractionEnabled = {}\n"),
                     readBool(STR("bRecruitInteractionEnabled")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bTalkInteractionEnabled    = {}\n"),
                     readBool(STR("bTalkInteractionEnabled")).c_str());
                VLOG(STR("[MoriaCppMod] [Probe]     bDetailsInteractionEnabled = {}\n"),
                     readBool(STR("bDetailsInteractionEnabled")).c_str());

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
                    if (auto* prop = npcComp->GetClassPrivate()->FindProperty(
                            RC::Unreal::FName(nm, RC::Unreal::FNAME_Find)))
                    {
                        std::wstring tn;
                        try { tn = prop->GetClass().GetName(); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [Probe]     {} EXISTS (type={} off=0x{:X})\n"),
                             nm, tn.c_str(), (unsigned)prop->GetOffset_Internal());
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
            auto* storyMgrCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Tech/Managers/BP_StoryManager.BP_StoryManager_C"));
            if (!storyMgrCls)
            {
                VLOG(STR("[MoriaCppMod] [Probe] BP_StoryManager_C class missing\n"));
                return;
            }
            auto* getMgrFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/FGK.FGKUtils:GetManager"));
            auto* fgkUtilsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/FGK.Default__FGKUtils"));
            if (!getMgrFn || !fgkUtilsCDO || !worldCtx) return;
            int sz = getMgrFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>(getMgrFn, buf.data(), STR("WorldContextObject"), worldCtx);
            writeGoatParm<UClass*> (getMgrFn, buf.data(), STR("ManagerClass"),       storyMgrCls);
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
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &hit))
                    for (UObject* g : hit) if (g && isObjectAlive(g)) { deepsGoat = g; break; }
            }
            VLOG(STR("[MoriaCppMod] [Probe] deeps goat ptr (for match detection): {:p}\n"),
                 (void*)deepsGoat);

            int arrCount = 0, boolCount = 0, intCount = 0, nameCount = 0;
            for (auto* strct = static_cast<UStruct*>(storyMgr->GetClassPrivate());
                 strct; strct = strct->GetSuperStruct())
            {
                std::wstring scopeCls;
                try { scopeCls = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    std::wstring pn, tn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { tn = prop->GetClass().GetName(); } catch (...) {}
                    unsigned off = (unsigned)prop->GetOffset_Internal();
                    uint8_t* slot = reinterpret_cast<uint8_t*>(storyMgr) + off;

                    if (tn == STR("ArrayProperty"))
                    {
                        auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(prop);
                        FProperty* inner = nullptr;
                        try { inner = arrProp->GetInner(); } catch (...) {}
                        std::wstring innerTn;
                        if (inner) { try { innerTn = inner->GetClass().GetName(); } catch (...) {} }
                        void**   data = reinterpret_cast<void**>  (slot + 0);
                        int32_t* num  = reinterpret_cast<int32_t*>(slot + 8);
                        VLOG(STR("[MoriaCppMod] [Probe]   ARR [{}].{} off=0x{:X} inner={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
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
                                try { eName = e->GetName(); } catch (...) {}
                                try { eCls = e->GetClassPrivate()->GetName(); } catch (...) {}
                                bool isGoat = (e == deepsGoat) || (eCls == STR("BP_NpcGoat_C"));
                                VLOG(STR("[MoriaCppMod] [Probe]       [{}] {} ({}){}\n"),
                                     j, eName.c_str(), eCls.c_str(),
                                     isGoat ? STR("  <<< GOAT MATCH >>>") : STR(""));
                            }
                        }
                        ++arrCount;
                    }
                    else if (tn == STR("BoolProperty"))
                    {
                        uint8_t b = *slot;
                        VLOG(STR("[MoriaCppMod] [Probe]   BOOL [{}].{} off=0x{:X} byte=0x{:02X}\n"),
                             scopeCls.c_str(), pn.c_str(), off, b);
                        ++boolCount;
                    }
                    else if (tn == STR("IntProperty"))
                    {
                        int32_t v = *reinterpret_cast<int32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   INT  [{}].{} off=0x{:X} val={}\n"),
                             scopeCls.c_str(), pn.c_str(), off, v);
                        ++intCount;
                    }
                    else if (tn == STR("NameProperty"))
                    {
                        uint32_t* fnameRaw = reinterpret_cast<uint32_t*>(slot);
                        VLOG(STR("[MoriaCppMod] [Probe]   NAME [{}].{} off=0x{:X} idx={} num={}\n"),
                             scopeCls.c_str(), pn.c_str(), off,
                             fnameRaw[0], fnameRaw[1]);
                        ++nameCount;
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [Probe] === end BP_StoryManager dump ({} arrays, {} bools, {} ints, {} names) ===\n"),
                 arrCount, boolCount, intCount, nameCount);
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
            auto* pWp   = findParam(sendFn, STR("SettlementWaypointID"));
            if (!pGuid || !pWp)
            {
                VLOG(STR("[MoriaCppMod] [Goat] ServerSendNpcToSettlement params missing (guid={:p} wp={:p})\n"),
                     (void*)pGuid, (void*)pWp);
                return;
            }
            // Copy the 16-byte FGuid into the parm slot.
            std::memcpy(buf.data() + pGuid->GetOffset_Internal(), guidPtr, 16);
            *reinterpret_cast<int32_t*>(buf.data() + pWp->GetOffset_Internal()) = waypointId;

            // Log the guid we're sending for verification.
            uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
            VLOG(STR("[MoriaCppMod] [Goat] firing ServerSendNpcToSettlement(guid={:08X}-{:08X}-{:08X}-{:08X}, waypoint={})\n"),
                 g[0], g[1], g[2], g[3], waypointId);
            safeProcessEvent(m_localPC, sendFn, buf.data());
            VLOG(STR("[MoriaCppMod] [Goat] ServerSendNpcToSettlement returned\n"));
        }

        // Set the AI controller's blackboard key "LeashActor" to the
        // player pawn. Per desktop brief: Bst_NPCGoatWorkPorter_C reads
        // this key for its FollowPlayer task. The vanilla EQS that picks
        // the leash target (EQS_Npc_PorterLeashPlayer) doesn't pick up
        // the player, so we set the key directly. Re-set per tick because
        // EQS may overwrite it.
        void setGoatLeashActor(UObject* ctrl, UObject* playerPawn)
        {
            if (!ctrl || !isObjectAlive(ctrl)) return;
            if (!playerPawn || !isObjectAlive(playerPawn)) return;
            // AAIController has a "Blackboard" UPROPERTY (UE4 standard) —
            // alternative name "BlackboardComp" on some custom controllers.
            UObject** bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Blackboard"));
            if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr))
                bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComp"));
            if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr)) return;
            UObject* bb = *bbPtr;
            auto* setObjFn = bb->GetFunctionByNameInChain(STR("SetValueAsObject"));
            if (!setObjFn) return;
            auto* pKey = findParam(setObjFn, STR("KeyName"));
            auto* pVal = findParam(setObjFn, STR("ObjectValue"));
            if (!pKey || !pVal) return;
            int sz = setObjFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            // FName "LeashActor"
            RC::Unreal::FName keyName(STR("LeashActor"), RC::Unreal::FNAME_Add);
            std::memcpy(buf.data() + pKey->GetOffset_Internal(), &keyName, sizeof(RC::Unreal::FName));
            *reinterpret_cast<UObject**>(buf.data() + pVal->GetOffset_Internal()) = playerPawn;
            safeProcessEvent(bb, setObjFn, buf.data());
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
                VLOG(STR("[MoriaCppMod] [Goat][{}] BehaviorFSMComp not resolvable on ctrl={:p}\n"),
                     tag, (void*)ctrl);
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
                    try { s = fnPtr->ToString(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [Goat][{}] FSM.{} = '{}'\n"),
                         tag, nm, s.c_str());
                    return;
                }
            }
            VLOG(STR("[MoriaCppMod] [Goat][{}] FSM has no recognized state-name property; need schema dump\n"),
                 tag);
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
                *reinterpret_cast<void**>   (fstr + 0)  = strBuf;
                *reinterpret_cast<int32_t*> (fstr + 8)  = strLen;
                *reinterpret_cast<int32_t*> (fstr + 12) = strLen;
            }
            safeProcessEvent(brain, stopFn, buf.data());
            VLOG(STR("[MoriaCppMod] [Goat] BrainComponent::StopLogic('PorterMod') fired on brain={:p}\n"),
                 (void*)brain);
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
            if (!ensureGoatSpawnBindings()) return nullptr;  // need GS:BeginDeferred

            // Resolve vanilla AAIController class once and cache.
            if (!m_vanillaAIControllerClass)
            {
                m_vanillaAIControllerClass = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/AIModule.AIController"));
                if (!m_vanillaAIControllerClass)
                {
                    VLOG(STR("[MoriaCppMod] [Goat] vanilla AAIController class not resident\n"));
                    return nullptr;
                }
            }

            // (1) UnPossess + (2) destroy the old AI controller, if any.
            if (oldCtrl && isObjectAlive(oldCtrl))
            {
                if (auto* upFn = oldCtrl->GetFunctionByNameInChain(STR("UnPossess")))
                    safeProcessEvent(oldCtrl, upFn, nullptr);
                if (auto* dFn = oldCtrl->GetFunctionByNameInChain(STR("K2_DestroyActor")))
                    safeProcessEvent(oldCtrl, dFn, nullptr);
                VLOG(STR("[MoriaCppMod] [Goat] old AIController unpossessed + destroyed\n"));
            }

            // (3) Spawn the vanilla AAIController at the goat's location.
            FVec3f loc{};
            if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                struct { FVec3f Ret{}; } lp{};
                if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
            }
            FTransformRaw xform{};
            xform.Rotation    = {0.0f, 0.0f, 0.0f, 1.0f};
            xform.Translation = loc;
            xform.Scale3D     = {1.0f, 1.0f, 1.0f};

            int sz = m_goatBeginSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>     (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
            writeGoatParm<UClass*>      (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         m_vanillaAIControllerClass);
            writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            writeGoatParm<uint8_t>      (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);  // AlwaysSpawn (controllers don't collide)
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) return nullptr;
            UObject* newCtrl = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!newCtrl)
            {
                VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawn returned null\n"));
                return nullptr;
            }
            int sz2 = m_goatFinishSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf2(sz2, 0);
            writeGoatParm<UObject*>     (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          newCtrl);
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
                struct { UObject* Pawn{nullptr}; } pp{};
                pp.Pawn = goat;
                safeProcessEvent(newCtrl, possFn, &pp);
                VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawned + possessing goat: {:p}\n"), (void*)newCtrl);
                return newCtrl;
            }
            else if (auto* k2pFn = newCtrl->GetFunctionByNameInChain(STR("K2_Possess")))
            {
                struct { UObject* Pawn{nullptr}; } pp{};
                pp.Pawn = goat;
                safeProcessEvent(newCtrl, k2pFn, &pp);
                VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController possessed via K2_Possess: {:p}\n"), (void*)newCtrl);
                return newCtrl;
            }
            VLOG(STR("[MoriaCppMod] [Goat] vanilla AIController spawned but no Possess UFunction found\n"));
            return newCtrl;
        }

        // PE pre-hook: ServerInteract fires when player presses E on any
        // interactable. Param 0 (ObjectInteractable) is the target.  If the
        // target is one of our spawned goats, run our custom action.
        // Currently a placeholder — logs + toast. Will eventually call the
        // open-inventory UFunction once we identify it.
        void onGoatInteractPre(UObject* /*context*/, UFunction* func, void* parms)
        {
            if (!parms || !func) return;
            auto* pTarget = findParam(func, STR("ObjectInteractable"));
            if (!pTarget) return;
            UObject* target = *reinterpret_cast<UObject**>(
                static_cast<uint8_t*>(parms) + pTarget->GetOffset_Internal());
            if (!target || !isObjectAlive(target)) return;

            // Is this one of our spawned goats?
            for (auto& g : m_followGoats)
            {
                UObject* mine = g.pawn.Get();
                if (mine && mine == target)
                {
                    VLOG(STR("[MoriaCppMod] [Goat] E-press detected on companion goat (ServerInteract hook fired) target={:p}\n"),
                         (void*)target);
                    showOnScreen(L"Goat: open inventory (TODO)", 2.0f, 0.4f, 0.9f, 0.4f);
                    return;
                }
            }
        }

        // PE pre-hook: ServerRescueNpc fires when the rescue interaction is
        // dispatched. We can't easily map the NpcGuid back to our spawned
        // goat (our goats don't have a persistent guid), so this is a
        // catch-all — if the player has a follower goat AND someone is
        // calling rescue, assume it's our goat and run the placeholder.
        void onGoatRescuePre(UObject* /*context*/, UFunction* /*func*/, void* /*parms*/)
        {
            if (m_followGoats.empty()) return;  // no goats, not us
            VLOG(STR("[MoriaCppMod] [Goat] ServerRescueNpc fired (with follower goat present — likely E on goat)\n"));
            showOnScreen(L"Goat: rescue intercepted (TODO open inventory)", 2.0f, 0.4f, 0.9f, 0.4f);
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
            try { cls = npcComp->GetClassPrivate(); } catch (...) {}
            if (!cls) return;
            std::wstring clsName;
            try { clsName = cls->GetName(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [NPCDump] === {} dump start (instance={:p}) ===\n"),
                 clsName.c_str(), (void*)npcComp);

            int propCount = 0;
            for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
            {
                std::wstring strctName;
                try { strctName = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    if (propCount >= 400) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}

                    std::wstring valueStr;
                    try
                    {
                        int32_t off = prop->GetOffset_Internal();
                        uint8_t* base = reinterpret_cast<uint8_t*>(npcComp);
                        uint8_t* slot = base + off;
                        if (pcn == STR("ObjectProperty")
                            || pcn == STR("WeakObjectProperty")
                            || pcn == STR("ClassProperty")
                            || pcn == STR("InterfaceProperty"))
                        {
                            UObject* val = *reinterpret_cast<UObject**>(slot);
                            if (val)
                            {
                                std::wstring vName, vCls;
                                try { vName = val->GetName(); } catch (...) {}
                                try { vCls = val->GetClassPrivate()->GetName(); } catch (...) {}
                                valueStr = STR(" value=") + vName + STR(" (") + vCls + STR(")");
                            }
                            else valueStr = STR(" value=null");
                        }
                        else if (pcn == STR("StrProperty"))
                        {
                            FString* fs = reinterpret_cast<FString*>(slot);
                            std::wstring s;
                            try { s = fs->GetCharArray().GetData() ? fs->GetCharArray().GetData() : STR(""); } catch (...) {}
                            valueStr = STR(" value=\"") + s + STR("\"");
                        }
                        else if (pcn == STR("NameProperty"))
                        {
                            FName* fn = reinterpret_cast<FName*>(slot);
                            std::wstring s;
                            try { s = fn->ToString(); } catch (...) {}
                            valueStr = STR(" value=") + s;
                        }
                        else if (pcn == STR("TextProperty"))
                        {
                            FText* ft = reinterpret_cast<FText*>(slot);
                            std::wstring s;
                            try { s = ft->ToString(); } catch (...) {}
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
                            swprintf(buf, 80, STR(" maybe-guid=%08X-%08X-%08X-%08X"),
                                     g[0], g[1], g[2], g[3]);
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
                    catch (...) {}

                    VLOG(STR("[MoriaCppMod] [NPCDump] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                         clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str(),
                         (unsigned)prop->GetOffset_Internal(), valueStr.c_str());
                    ++propCount;
                }
                if (propCount >= 400) break;
            }
            VLOG(STR("[MoriaCppMod] [NPCDump] === {} dump done ({} props) ===\n"),
                 clsName.c_str(), propCount);

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
                try { fnName = fn->GetName(); } catch (...) {}
                int parmCount = 0;
                std::wstring paramSig;
                for (auto* prop : fn->ForEachProperty())
                {
                    if (parmCount >= 6) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}
                    if (!paramSig.empty()) paramSig += STR(", ");
                    paramSig += pcn + STR(" ") + pn;
                    ++parmCount;
                }
                VLOG(STR("[MoriaCppMod] [NPCDump] UFUNC {}({})\n"),
                     fnName.c_str(), paramSig.c_str());
                ++fnCount;
            }
            VLOG(STR("[MoriaCppMod] [NPCDump] === {} ufunc dump done ({} fns) ===\n"),
                 clsName.c_str(), fnCount);
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
            FString* nameStr = reinterpret_cast<FString*>(
                static_cast<uint8_t*>(parms) + pName->GetOffset_Internal());
            const wchar_t* nameData = nullptr;
            try { nameData = nameStr->GetCharArray().GetData(); } catch (...) {}
            if (!nameData) return;

            VLOG(STR("[MoriaCppMod] [GoatChest] ServerSetInteractableCustomName CustomName='{}'\n"),
                 nameData);

            // Case-insensitive match against "goat".
            if (_wcsicmp(nameData, STR("goat")) != 0) return;

            // Read ObjectInteractable param — the chest being renamed.
            auto* pObj = findParam(func, STR("ObjectInteractable"));
            if (!pObj) return;
            UObject* chest = *reinterpret_cast<UObject**>(
                static_cast<uint8_t*>(parms) + pObj->GetOffset_Internal());
            if (!chest || !isObjectAlive(chest)) return;

            std::wstring chestName, chestCls, chestPath;
            try { chestName = chest->GetName(); } catch (...) {}
            try { chestCls = chest->GetClassPrivate()->GetName(); } catch (...) {}
            try { chestPath = chest->GetFullName(); } catch (...) {}

            m_goatChestActor = chest;
            m_goatChestClassName = chestCls;
            m_goatChestPath = chestPath;

            VLOG(STR("[MoriaCppMod] [GoatChest] *** GOAT CHEST CAPTURED *** ptr={:p} name={} cls={} path={}\n"),
                 (void*)chest, chestName.c_str(), chestCls.c_str(), chestPath.c_str());
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
            if (!m_goatChestSchemaDumped)
                m_pendingChestSchemaTicks = 120;

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
            try { cls = chest->GetClassPrivate(); } catch (...) {}
            if (!cls) return;
            std::wstring clsName;
            try { clsName = cls->GetName(); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [GoatChest] === {} property dump start (instance={:p}) ===\n"),
                 clsName.c_str(), (void*)chest);

            int propCount = 0;
            for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
            {
                std::wstring strctName;
                try { strctName = strct->GetName(); } catch (...) {}
                for (auto* prop : strct->ForEachProperty())
                {
                    if (propCount >= 400) break;
                    std::wstring pn, pcn;
                    try { pn = prop->GetName(); } catch (...) {}
                    try { pcn = prop->GetClass().GetName(); } catch (...) {}

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
                            try { p = fs->GetCharArray().GetData(); } catch (...) {}
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
                            swprintf(buf, 80, STR(" raw16=%08X-%08X-%08X-%08X"),
                                     g[0], g[1], g[2], g[3]);
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
                    catch (...) {}

                    VLOG(STR("[MoriaCppMod] [GoatChest] PROP {} (from {}).{} : {} off=0x{:X}{}\n"),
                         clsName.c_str(), strctName.c_str(), pn.c_str(), pcn.c_str(),
                         (unsigned)prop->GetOffset_Internal(), valueStr.c_str());
                    ++propCount;
                }
                if (propCount >= 400) break;
            }
            VLOG(STR("[MoriaCppMod] [GoatChest] === {} property dump done ({} props) ===\n"),
                 clsName.c_str(), propCount);
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
            } catch (...) {}

            try
            {
                if (auto* p = findParam(func, STR("InSpawner")))
                {
                    UObject* sp = *reinterpret_cast<UObject**>(
                        static_cast<uint8_t*>(parms) + p->GetOffset_Internal());
                    if (sp && isObjectAlive(sp))
                    {
                        spawnerArg = safeClassName(sp);
                    }
                    else spawnerArg = STR("<null>");
                }
            } catch (...) {}

            try
            {
                if (auto* p = findParam(func, STR("InSpawnContext")))
                {
                    uint8_t v = *(static_cast<uint8_t*>(parms) + p->GetOffset_Internal());
                    ctxVal = v;
                }
            } catch (...) {}

            // Decode context enum for readability.
            const wchar_t* ctxName = STR("?");
            switch (ctxVal)
            {
                case 0: ctxName = STR("None"); break;
                case 1: ctxName = STR("AIPopulation"); break;
                case 2: ctxName = STR("AIPatrol"); break;
                case 3: ctxName = STR("AIChallenge"); break;
                case 4: ctxName = STR("AILair"); break;
                case 5: ctxName = STR("AIWaveEncounter"); break;
                case 6: ctxName = STR("AISavedSingleSpawner"); break;
            }

            VLOG(STR("[MoriaCppMod] [SpawnDiag] BP_RequestSpawn manager={} class='{}' spawner='{}' ctx={}({})\n"),
                 spawnerCls.c_str(), chTypePath.c_str(), spawnerArg.c_str(), ctxName, ctxVal);
        }


        // One-shot dump: walk every component on a live AActor and print
        // each component's class name + UFunction names that look related
        // to inventory / storage / container access. This is how we'll
        // find the goat's storage component (likely MorContainerComponent
        // or MorInventoryComponent) and the open-UI UFunction we need to
        // call when the player taps E on the goat.
        bool m_goatComponentsDumped{false};
        void dumpGoatComponents(UObject* goatActor)
        {
            if (m_goatComponentsDumped) return;
            m_goatComponentsDumped = true;
            if (!goatActor) return;

            VLOG(STR("[MoriaCppMod] [GoatCompDump] === components on goat={:p} cls={} ===\n"),
                 (void*)goatActor, safeClassName(goatActor).c_str());

            // K2_GetComponentsByClass(UActorComponent::StaticClass()) returns
            // every ActorComponent. We use UClass*=ActorComponent to grab
            // them all, then filter UFunction listing per component.
            auto* getCompsFn = goatActor->GetFunctionByNameInChain(STR("GetComponents"));
            if (!getCompsFn)
                getCompsFn = goatActor->GetFunctionByNameInChain(STR("K2_GetComponentsByClass"));
            // Fallback: walk InstanceComponents UPROPERTY directly.
            UObject** instCompsArrPtr = goatActor->GetValuePtrByPropertyNameInChain<UObject*>(
                STR("InstanceComponents"));
            (void)instCompsArrPtr;  // we'll iterate via property reflection

            // Iterate the BlueprintCreatedComponents and InstanceComponents
            // arrays via reflection.
            const wchar_t* arrPropNames[] = {
                STR("BlueprintCreatedComponents"),
                STR("InstanceComponents"),
                STR("OwnedComponents"),
            };
            int totalComps = 0;
            for (auto* arrName : arrPropNames)
            {
                UClass* aCls = nullptr;
                try { aCls = goatActor->GetClassPrivate(); } catch (...) {}
                if (!aCls) continue;
                FProperty* arrProp = nullptr;
                for (auto* strct = static_cast<UStruct*>(aCls); strct && !arrProp;
                     strct = strct->GetSuperStruct())
                {
                    for (auto* p : strct->ForEachProperty())
                    {
                        std::wstring pn;
                        try { pn = p->GetName(); } catch (...) {}
                        if (pn == arrName) { arrProp = p; break; }
                    }
                }
                if (!arrProp) continue;
                // TArray<UObject*> layout: { UObject** Data; int32 Num; int32 Max; }
                uint8_t* slot = reinterpret_cast<uint8_t*>(goatActor) + arrProp->GetOffset_Internal();
                UObject** data = *reinterpret_cast<UObject***>(slot + 0);
                int32_t num    = *reinterpret_cast<int32_t*>(slot + 8);
                if (!data || num <= 0) continue;
                VLOG(STR("[MoriaCppMod] [GoatCompDump]   {} count={}\n"), arrName, num);
                for (int32_t i = 0; i < num && i < 64; ++i)
                {
                    UObject* c = data[i];
                    if (!c || !isObjectAlive(c)) continue;
                    std::wstring cName, cCls;
                    try { cName = c->GetName(); } catch (...) {}
                    try { cCls = c->GetClassPrivate()->GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [GoatCompDump]     [{}] {} : {}\n"),
                         i, cName.c_str(), cCls.c_str());
                    ++totalComps;

                    // For components whose class name suggests storage,
                    // inventory, container — list interesting UFunctions.
                    std::wstring lcCls = cCls;
                    for (auto& ch : lcCls) ch = (wchar_t)::towlower(ch);
                    if (lcCls.find(L"inventor")  == std::wstring::npos
                        && lcCls.find(L"storage")  == std::wstring::npos
                        && lcCls.find(L"container")== std::wstring::npos
                        && lcCls.find(L"equip")    == std::wstring::npos
                        && lcCls.find(L"loot")     == std::wstring::npos
                        && lcCls.find(L"pack")     == std::wstring::npos
                        && lcCls.find(L"interact") == std::wstring::npos)
                    { continue; }

                    UClass* ccls = nullptr;
                    try { ccls = c->GetClassPrivate(); } catch (...) {}
                    if (!ccls) continue;
                    int fnCount = 0;
                    for (auto* fn : ccls->ForEachFunctionInChain())
                    {
                        if (fnCount >= 60) break;
                        std::wstring fnName;
                        try { fnName = fn->GetName(); } catch (...) {}
                        std::wstring lfn = fnName;
                        for (auto& ch : lfn) ch = (wchar_t)::towlower(ch);
                        if (lfn.find(L"open") == std::wstring::npos
                            && lfn.find(L"close") == std::wstring::npos
                            && lfn.find(L"show") == std::wstring::npos
                            && lfn.find(L"request") == std::wstring::npos
                            && lfn.find(L"transfer") == std::wstring::npos
                            && lfn.find(L"loot") == std::wstring::npos
                            && lfn.find(L"access") == std::wstring::npos
                            && lfn.find(L"interact") == std::wstring::npos)
                        { ++fnCount; continue; }
                        VLOG(STR("[MoriaCppMod] [GoatCompDump]       UFUNC {}.{}\n"),
                             cCls.c_str(), fnName.c_str());
                        ++fnCount;
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatCompDump] === total components walked: {} ===\n"), totalComps);
        }

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
                try { safeProcessEvent(w, fn, buf.data()); } catch (...) { return STR(""); }
                auto* pRet = findParam(fn, STR("ReturnValue"));
                if (!pRet) return STR("");
                FText* t = reinterpret_cast<FText*>(buf.data() + pRet->GetOffset_Internal());
                if (!t) return STR("");
                try { return std::wstring(t->ToString()); } catch (...) { return STR(""); }
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
                if (dt) {
                    std::wstring s = seh_ftextToString(dt);
                    if (!s.empty()) {
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
                try { cls = w->GetClassPrivate()->GetName(); } catch (...) {}
                if (cls == STR("TextBlock"))
                {
                    std::wstring wname;
                    try { wname = std::wstring(w->GetNamePrivate().ToString()); } catch (...) {}
                    if (wname.find(STR("InteractionText")) != std::wstring::npos &&
                        wname.find(STR("HoldInteractionText")) == std::wstring::npos)
                    {
                        std::wstring t = readText(w);
                        if (!t.empty()) { found = t; return; }
                    }
                }
                auto* nestedWt = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
                if (nestedWt && *nestedWt) {
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
            try { outer = interactable->GetOuterPrivate(); } catch (...) {}
            if (!outer || !isObjectAlive(outer)) return;

            bool isOurGoat = false;
            for (auto& g : m_followGoats)
            {
                UObject* mine = g.pawn.Get();
                if (mine && mine == outer) { isOurGoat = true; break; }
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
            VLOG(STR("[MoriaCppMod] [GoatMenu] row label='{}' — dispatching\n"),
                 rowLabel.empty() ? STR("?") : rowLabel.c_str());

            if (rowLabel.find(STR("Follow")) != std::wstring::npos ||
                rowLabel.find(STR("follow")) != std::wstring::npos)
            {
                // Follow / Stay toggle — flip stayMode based on first record's current state.
                bool currentlyStaying = false;
                if (!m_followGoats.empty()) currentlyStaying = m_followGoats[0].stayMode;
                if (currentlyStaying)
                    onGoatFollow();  // sets stayMode=false + role=Porter + toast "following"
                else
                    onGoatStay();    // sets stayMode=true + StopMovement + role=Wanderer + toast "staying"
            }
            else if (rowLabel.find(STR("Saddlebag")) != std::wstring::npos ||
                     rowLabel.find(STR("saddlebag")) != std::wstring::npos)
            {
                // [rc.70 2026-05-21] Saddlebag access — diagnostic + best-effort.
                VLOG(STR("[MoriaCppMod] [GoatMenu] SADDLEBAGS clicked — invoking openGoatSaddlebag\n"));
                openGoatSaddlebagInventory();
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatMenu] unrecognized row label '{}' — ignoring\n"),
                     rowLabel.empty() ? STR("<empty>") : rowLabel.c_str());
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
            if (destroyed > 0)
                showOnScreen(L"Goat dismissed", 2.0f, 0.4f, 0.9f, 0.4f);
        }

        // Lazy scan for an EXISTING registered porter goat in the loaded
        // world. Used at NUM- press time before deciding spawn-vs-despawn —
        // adopts a save-persisted goat from a prior session into our
        // m_followGoats so toggle behaves correctly across save/reload.
        //
        // Filter: BP_NpcGoat_C with non-zero NpcGuid. Wild goats in goat
        // zones have NpcGuid=0 (never registered with NPC manager); our
        // porter goat is registered (post-v1.1.0 install + assignPorterRole)
        // so its NpcGuid is non-zero.
        UObject* findExistingRegisteredGoat()
        {
            if (!m_goatBPClass) return nullptr;
            std::vector<UObject*> candidates;
            // FindAllOf is exact-class; BP_NpcGoat_C *should* match.
            // Fallback: walk BlueprintGeneratedClass via seh_findAllOf and
            // filter by class name if needed (existing pattern in codebase).
            std::wstring goatClsName;
            try { goatClsName = m_goatBPClass->GetName(); } catch (...) {}
            if (goatClsName.empty()) return nullptr;
            if (!seh_findAllOf(goatClsName.c_str(), &candidates)) return nullptr;

            // Resolve MorNPCComponent class once for GetComponentByClass.
            auto* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!npcCompCls) return nullptr;

            for (UObject* candidate : candidates)
            {
                if (!candidate || !isObjectAlive(candidate)) continue;

                auto* getCompFn = candidate->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (!getCompFn) continue;
                int sz = getCompFn->GetParmsSize();
                std::vector<uint8_t> buf(sz, 0);
                writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
                if (!safeProcessEvent(candidate, getCompFn, buf.data())) continue;
                UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                if (!npcComp || !isObjectAlive(npcComp)) continue;

                // Read NpcGuid via reflection (FGuid is 16B / 4 uint32s).
                // All zeros = wild goat / unregistered. Any non-zero word = ours.
                uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                if (!guidPtr) continue;
                uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
                if (g[0] == 0 && g[1] == 0 && g[2] == 0 && g[3] == 0) continue;

                VLOG(STR("[MoriaCppMod] [Goat] found existing registered porter goat={:p} guid={:08X}-{:08X}-{:08X}-{:08X}\n"),
                     (void*)candidate, g[0], g[1], g[2], g[3]);
                return candidate;
            }
            return nullptr;
        }

        // Adopt an existing world-resident goat into our follow tracking.
        // Used by toggle's lazy scan to bring a save-persisted goat under
        // our follow loop without re-spawning.
        void adoptExistingGoat(UObject* goat)
        {
            if (!goat) return;
            FollowGoatRecord rec{};
            rec.pawn             = RC::Unreal::FWeakObjectPtr(goat);
            rec.controller       = RC::Unreal::FWeakObjectPtr();
            rec.lastMoveTickMs   = 0;
            rec.fleeSuppressed   = false;
            rec.componentsLogged = false;
            m_followGoats.push_back(rec);
            VLOG(STR("[MoriaCppMod] [Goat] adopted existing registered goat into follow tracking; herd size={}\n"),
                 m_followGoats.size());

            // [v7.2.0-rc.3 2026-05-22] Save-persisted goats re-enter the world
            // with the same empty-slot bug as fresh spawns (Tobi's BeginPlay
            // graph doesn't fill the slot regardless of whether the goat is
            // fresh or restored). Equip on adopt too.
            equipPorterSaddlebag(goat);
        }

        // NUM- toggle: spawn if no goat is currently tracked; despawn if one is.
        // Replaces the previous "spawn until max" behavior.
        void toggleFollowGoat()
        {
            if (!m_characterLoaded)
            {
                VLOG(STR("[MoriaCppMod] [Goat] character not loaded; ignoring NUM-\n"));
                return;
            }
            // Prune stale records first so a dead goat doesn't block re-spawn.
            m_followGoats.erase(
                std::remove_if(m_followGoats.begin(), m_followGoats.end(),
                    [](FollowGoatRecord& g) {
                        UObject* p = g.pawn.Get();
                        return !p || !isObjectAlive(p);
                    }),
                m_followGoats.end());

            // Lazy scan: if our tracking is empty, check whether a
            // save-persisted registered goat already exists in the loaded
            // world (e.g., from a prior session post-v1.1.0). If so, adopt
            // it so the toggle behavior — alive→despawn — fires correctly
            // on this press rather than spawning a duplicate.
            if (m_followGoats.empty())
            {
                if (UObject* existing = findExistingRegisteredGoat())
                    adoptExistingGoat(existing);
            }

            if (!m_followGoats.empty())
            {
                despawnAllFollowGoats();
                return;
            }
            spawnFollowGoat();
        }

        void spawnFollowGoat()
        {
            if (!m_characterLoaded)
            {
                VLOG(STR("[MoriaCppMod] [Goat] character not loaded; ignoring NUM-\n"));
                return;
            }
            if (m_followGoats.size() >= MAX_FOLLOW_GOATS)
            {
                showInfoMessage(L"Goat already summoned. Press NUM- again to dismiss.");
                return;
            }
            if (!ensureGoatSpawnBindings())
            {
                VLOG(STR("[MoriaCppMod] [Goat] bindings not ready (cls={} cdo={} begin={} finish={})\n"),
                     (void*)m_goatBPClass, (void*)m_kismetGameplayStaticsCDO,
                     (void*)m_goatBeginSpawnFn, (void*)m_goatFinishSpawnFn);
                showInfoMessage(L"Goat spawn unavailable (asset not loaded).");
                return;
            }

            UObject* pawn = m_localPawn ? m_localPawn : getPawn();
            if (!pawn || !isObjectAlive(pawn))
            {
                VLOG(STR("[MoriaCppMod] [Goat] no local pawn for spawn\n"));
                return;
            }

            // Spawn ~300 cm in front of the pawn so it doesn't telefrag the player.
            FVec3f loc = getPawnLocation();
            FVec3f fwd{1.0f, 0.0f, 0.0f};
            if (auto* fwdFn = pawn->GetFunctionByNameInChain(STR("GetActorForwardVector")))
            {
                struct { FVec3f Ret; } p{};
                if (safeProcessEvent(pawn, fwdFn, &p)) fwd = p.Ret;
            }
            FVec3f spawnLoc{ loc.X + fwd.X * 300.0f,
                             loc.Y + fwd.Y * 300.0f,
                             loc.Z };

            FTransformRaw xform{};
            xform.Rotation    = {0.0f, 0.0f, 0.0f, 1.0f};      // identity
            xform.Translation = spawnLoc;
            xform.Scale3D     = {1.0f, 1.0f, 1.0f};

            // 1) BeginDeferredActorSpawnFromClass — returns deferred actor
            int sz = m_goatBeginSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UObject*>      (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), pawn);
            writeGoatParm<UClass*>       (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         m_goatBPClass);
            writeGoatParm<FTransformRaw> (m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            writeGoatParm<uint8_t>       (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"),
                                          /*AdjustIfPossibleButAlwaysSpawn*/ 2);
            // Owner stays nullptr (zero-initialised buffer).
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data()))
            {
                showInfoMessage(L"Goat spawn failed (BeginDeferredActorSpawn).");
                return;
            }
            UObject* goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!goat)
            {
                VLOG(STR("[MoriaCppMod] [Goat] BeginDeferredActorSpawn returned null\n"));
                showInfoMessage(L"Goat spawn failed (no actor).");
                return;
            }

            // 2) FinishSpawningActor — runs construction script + BeginPlay
            int sz2 = m_goatFinishSpawnFn->GetParmsSize();
            std::vector<uint8_t> buf2(sz2, 0);
            writeGoatParm<UObject*>      (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          goat);
            writeGoatParm<FTransformRaw> (m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
            safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

            // [v1.1.0 GENERIC MODE 2026-05-09] Force-visibility / force-collision
            // suspended. v1.1.0 PorterGoat NPC is a properly-defined registered
            // character; defaults should be correct.
#if 0 // GENERIC_MODE_2026_05_09
            if (auto* hideFn = goat->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
            {
                struct { bool bNewHidden{false}; } pp{};
                safeProcessEvent(goat, hideFn, &pp);
            }
            if (auto* collFn = goat->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
            {
                struct { bool bNewActorEnableCollision{true}; } pp{};
                safeProcessEvent(goat, collFn, &pp);
            }
#endif // GENERIC_MODE

            // 4) Some BPs need an explicit SpawnDefaultController call when
            // spawned via deferred-spawn paths (engine-internal AutoPossessAI
            // hook may not run if BeginPlay is gated). APawn::SpawnDefaultController
            // is a UFUNCTION on APawn.
            if (auto* sdc = goat->GetFunctionByNameInChain(STR("SpawnDefaultController")))
            {
                safeProcessEvent(goat, sdc, nullptr);
                VLOG(STR("[MoriaCppMod] [Goat] SpawnDefaultController called on pawn\n"));
            }

            // 5) Defer controller resolve + flee suppression to the tick — the
            // AIController spawn runs async after PostInitializeComponents, so
            // K2_GetController right here usually returns null. tickFollowGoats
            // will retry until the controller is alive, then deactivate the AI
            // components once.
            FollowGoatRecord rec{};
            rec.pawn           = RC::Unreal::FWeakObjectPtr(goat);
            rec.controller     = RC::Unreal::FWeakObjectPtr();
            rec.lastMoveTickMs = 0;
            rec.fleeSuppressed = false;
            rec.componentsLogged = false;
            m_followGoats.push_back(rec);

            VLOG(STR("[MoriaCppMod] [Goat] spawned at ({:.1f},{:.1f},{:.1f}); herd size={}\n"),
                 spawnLoc.X, spawnLoc.Y, spawnLoc.Z, m_followGoats.size());

            // v1.1.0+ — register the goat with BP_NPCManager + assign
            // Porter role. Without this the goat is ephemeral (NpcGuid=0,
            // not in save state). assignPorterRole already wires:
            //   - RegisterWithNPCManager (gives non-zero NpcGuid; persists)
            //   - SetIsInteractive(true) (interaction enabled)
            //   - SetRoleFuzzy("Porter") (now Live since DT_NPCRoles edit)
            // Per `bell-summon-plan.md` + chest-link suspended notes, this
            // is the single registration step that flips the goat from
            // ephemeral to save-game persistent.
            assignPorterRole(goat);

            // [v1.1.1 DIAGNOSTIC 2026-05-09] Save persistence failed under
            // v1.1.1 whitelist alone — registration alone doesn't seem to
            // make the goat a "settlement member." Dump the live
            // MorNPCComponent's full schema (props + UFunctions) so we
            // can find the post-rescue state-flip mechanism.
            {
                auto* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
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
                            UObject* npcComp = readGoatParm<UObject*>(
                                getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                            if (npcComp && isObjectAlive(npcComp))
                                dumpMorNPCComponentSchema(npcComp);
                        }
                    }
                }
            }

            showOnScreen(L"Goat spawned (follow on)", 2.0f, 0.4f, 0.9f, 0.4f);
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
                if (--m_pendingChestSchemaTicks == 0
                    && !m_goatChestSchemaDumped
                    && m_goatChestActor
                    && isObjectAlive(m_goatChestActor))
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
            m_followGoats.erase(
                std::remove_if(m_followGoats.begin(), m_followGoats.end(),
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
                        struct { FVec3f Ret{}; } lp{};
                        if (safeProcessEvent(goat, gloc, &lp))
                            VLOG(STR("[MoriaCppMod] [Goat] post-tick goat actual loc=({:.1f},{:.1f},{:.1f})\n"),
                                 lp.Ret.X, lp.Ret.Y, lp.Ret.Z);
                    }
                    // Walk the actor's component list via GetComponentsByTag (returns
                    // all components if tag empty? not reliable). Use GetComponents
                    // by walking property reflection on the actor for "BlueprintCreatedComponents".
                    // Simpler: just walk all UProperty fields on the BP instance and
                    // log any UObject* whose GetClass()->GetName() ends in "Component".
                    int compHits = 0;
                    for (auto* strct = static_cast<UStruct*>(goat->GetClassPrivate());
                         strct; strct = strct->GetSuperStruct())
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
                            try { compClsName = safeClassName(comp); } catch (...) {}
                            if (compClsName.find(L"Component") == std::wstring::npos
                                && compClsName.find(L"Mesh")     == std::wstring::npos) continue;
                            VLOG(STR("[MoriaCppMod] [Goat] component: prop={} type={} ptr={:p}\n"),
                                 prop->GetName().c_str(), compClsName.c_str(), (void*)comp);
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
                                    struct { bool bNewActive{false}; bool bReset{false}; } sap{};
                                    safeProcessEvent(comp, saFn, &sap);
                                    VLOG(STR("[MoriaCppMod] [Goat] deactivated {} (suppresses 'E to rescue')\n"),
                                         compClsName.c_str());
                                }
                            }

                            // If this is the SkeletalMeshComponent, force visibility
                            // AND apply the user-selected MI_Goat skin to every slot.
                            if (compClsName.find(L"SkeletalMesh") != std::wstring::npos)
                            {
                                // [v1.1.0 GENERIC MODE 2026-05-09] Mesh
                                // visibility-force + skin override
                                // suspended — let v1.1.0 defaults stand.
#if 0 // GENERIC_MODE_2026_05_09
                                if (auto* svFn = comp->GetFunctionByNameInChain(STR("SetVisibility")))
                                {
                                    struct { bool bNewVisibility{true}; bool bPropagate{true}; } svp{};
                                    safeProcessEvent(comp, svFn, &svp);
                                }
                                if (auto* shFn = comp->GetFunctionByNameInChain(STR("SetHiddenInGame")))
                                {
                                    struct { bool bNewHidden{false}; bool bPropagate{true}; } shp{};
                                    safeProcessEvent(comp, shFn, &shp);
                                }
                                VLOG(STR("[MoriaCppMod] [Goat] forced SkeletalMesh visible\n"));
                                applyGoatSkin(comp);
#endif // GENERIC_MODE
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
                        struct ProbeEntry { const wchar_t* clsPath; const wchar_t* logName; };
                        ProbeEntry probes[] = {
                            { STR("/Script/Engine.SkeletalMeshComponent"),       L"Mesh" },
                            { STR("/Script/Moria.ModularCharacterComponent"),    L"ModularChar" },
                            { STR("/Script/Moria.MorEquipComponent"),            L"EquipComp" },
                            { STR("/Script/Moria.MorInventoryComponent"),        L"InventoryComp" },
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
                            VLOG(STR("[MoriaCppMod] [Goat] probe[{}] = {:p} ({})\n"),
                                 p.logName, (void*)found, safeClassName(found).c_str());

                            // For ModularCharacterComponent: dump its UPROPERTYs +
                            // its own AttachChildren. The visible "hat" almost
                            // certainly comes from one of its slot meshes.
                            if (std::wstring_view(p.logName) == std::wstring_view(L"ModularChar"))
                            {
                                int mccProps = 0;
                                for (auto* strct = static_cast<UStruct*>(found->GetClassPrivate());
                                     strct; strct = strct->GetSuperStruct())
                                {
                                    for (auto* mProp : strct->ForEachProperty())
                                    {
                                        if (mccProps >= 60) break;
                                        std::wstring mpn = mProp->GetName();
                                        VLOG(STR("[MoriaCppMod] [Goat]   MCC.prop[{}] = {} (type={})\n"),
                                             mccProps, mpn.c_str(),
                                             mProp->GetClass().GetName().c_str());
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
                                // [v1.1.0 GENERIC MODE 2026-05-09] Skin
                                // override + force-visibility suspended.
#if 0 // GENERIC_MODE_2026_05_09
                                applyGoatSkin(found);
                                if (auto* svFn = found->GetFunctionByNameInChain(STR("SetVisibility")))
                                {
                                    struct { bool bNewVisibility{true}; bool bPropagate{true}; } svp{};
                                    safeProcessEvent(found, svFn, &svp);
                                }
#endif // GENERIC_MODE

                                // Walk AttachChildren — TArray<USceneComponent*> on USceneComponent
                                auto** ac = found->GetValuePtrByPropertyNameInChain<void*>(STR("AttachChildren"));
                                if (ac)
                                {
                                    // TArray layout: { void* data; int32 num; int32 max }
                                    uint8_t* arr = reinterpret_cast<uint8_t*>(ac);
                                    UObject** elems = *reinterpret_cast<UObject***>(arr + 0);
                                    int32_t num     = *reinterpret_cast<int32_t*>(arr + 8);
                                    VLOG(STR("[MoriaCppMod] [Goat] Mesh.AttachChildren count={}\n"), num);
                                    int safeNum = num > 50 ? 50 : (num < 0 ? 0 : num);
                                    for (int i = 0; i < safeNum; ++i)
                                    {
                                        UObject* child = elems[i];
                                        if (!child || !isObjectAlive(child)) continue;
                                        std::wstring childCls = safeClassName(child);
                                        std::wstring childName;
                                        try { childName = child->GetName(); } catch (...) {}
                                        VLOG(STR("[MoriaCppMod] [Goat]   attached[{}]: name={} type={} ptr={:p}\n"),
                                             i, childName.c_str(), childCls.c_str(), (void*)child);
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
                        VLOG(STR("[MoriaCppMod] [Goat] controller resolved (attempt #{}, via UPROPERTY Controller): {:p}\n"),
                             g.ctrlAttempts, (void*)ctrl);
                    }
                    else if ((now - g.lastCtrlAttemptLogMs) > 2000)
                    {
                        g.lastCtrlAttemptLogMs = now;
                        VLOG(STR("[MoriaCppMod] [Goat] controller still null after {} attempts (UPROPERTY ptr={:p}, deref={:p})\n"),
                             g.ctrlAttempts, (void*)ctrlPtr, (void*)c);
                    }
                }
                if (!ctrl || !isObjectAlive(ctrl)) continue;

                // ORDER MATTERS: deactivate FGK components FIRST, before any
                // role assignment / equip dispatch. Otherwise the equip's
                // role-changed delegate fires while the FGK FSM is still
                // alive, which triggers a porter-state side effect (goat
                // tries to walk to a settlement waypoint to deliver the pack).
                if (!g.fleeSuppressed)
                {
                    g.fleeSuppressed = true;
                    // [v1.1.0 GENERIC MODE 2026-05-09] All FGK deactivations
                    // suspended. v1.1.0 pak makes the goat a registered
                    // PorterGoat NPC; we should let vanilla AI run. If
                    // perception/targeting/parkour/patrol are wrong for a
                    // settlement-grade NPC, the issue is in the v1.1.0
                    // editor side, not our runtime overrides.
#if 0 // GENERIC_MODE_2026_05_09
                    deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAIPerceptionComponent"), L"FGKAIPerceptionComponent");
                    deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAITargetingComponent"),  L"FGKAITargetingComponent");
                    deactivateGoatAIComponent(goat, STR("/Script/FGK.FGKParkourComponent"),      L"FGKParkourComponent");
                    deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAIPatrolComponent"),     L"FGKAIPatrolComponent");
#endif // GENERIC_MODE
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
                    if (g.postEquipDumpAttempts == 3)  // 3rd tick after equip
                    {
                        VLOG(STR("[MoriaCppMod] [Goat] === POST-EQUIP COMPONENT DUMP (looking for pack) ===\n"));
                        int packLikeFound = 0;
                        for (auto* strct = static_cast<UStruct*>(goat->GetClassPrivate());
                             strct; strct = strct->GetSuperStruct())
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
                                if (cn.find(L"SkeletalMesh") != std::wstring::npos
                                    || pn2.find(L"Pack") != std::wstring::npos
                                    || pn2.find(L"Dummy") != std::wstring::npos
                                    || pn2.find(L"Equipped") != std::wstring::npos)
                                {
                                    VLOG(STR("[MoriaCppMod] [Goat]   POST-EQUIP found: prop={} type={} ptr={:p}\n"),
                                         pn2.c_str(), cn.c_str(), (void*)c);
                                    ++packLikeFound;
                                }
                            }
                        }

                        // Also walk the SkeletalMeshComponent's AttachChildren for
                        // any newly-added pack child component.
                        auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
                        if (getCompFn)
                        {
                            auto* skmCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                STR("/Script/Engine.SkeletalMeshComponent"));
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
                                            int32_t num     = *reinterpret_cast<int32_t*>(arr + 8);
                                            VLOG(STR("[MoriaCppMod] [Goat]   POST-EQUIP Mesh.AttachChildren count={}\n"), num);
                                            int safeNum = num > 50 ? 50 : (num < 0 ? 0 : num);
                                            for (int i = 0; i < safeNum; ++i)
                                            {
                                                UObject* child = elems[i];
                                                if (!child || !isObjectAlive(child)) continue;
                                                std::wstring cls2 = safeClassName(child);
                                                std::wstring n2;  try { n2 = child->GetName(); } catch (...) {}
                                                // Filter to non-capsule, non-audio for noise reduction
                                                if (cls2.find(L"AkComponent") != std::wstring::npos) continue;
                                                if (n2.find(L"Capsule") != std::wstring::npos) continue;
                                                if (cls2.find(L"FGKTargetable") != std::wstring::npos) continue;
                                                VLOG(STR("[MoriaCppMod] [Goat]     attached non-noise[{}]: name={} type={} ptr={:p}\n"),
                                                     i, n2.c_str(), cls2.c_str(), (void*)child);
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
                        g.fleeSuppressed = true;  // brain replaced; nothing to suppress
                    }
                    else
                    {
                        // Fallback: replacement failed — fall back to deactivating
                        // FGK components on the existing controller. Less reliable
                        // (the FSM may still own the pawn) but better than nothing.
                        VLOG(STR("[MoriaCppMod] [Goat] controller replacement failed; falling back to FGK component deactivation\n"));
                        deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKActorFSMComponent"),     L"FGKActorFSMComponent");
                        deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAIPerceptionComponent"), L"FGKAIPerceptionComponent");
                        deactivateGoatAIComponent(ctrl, STR("/Script/FGK.FGKAITargetingComponent"),  L"FGKAITargetingComponent");
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
                    // [Phase 3] silenced — post-reg dump (70 lines/spawn).
                    // VLOG(STR("[MoriaCppMod] [Goat] === POST-REG component dump (pawn={:p} ctrl={:p}) ===\n"),
                    //      (void*)goat, (void*)ctrl);
                    // dumpGoatComponents(goat);
                    // VLOG(STR("[MoriaCppMod] [Goat] === POST-REG controller component dump ===\n"));
                    // dumpGoatComponents(ctrl);
                    // Also log the controller's class name to detect if
                    // registration swapped to a Porter-specific AIController.
                    std::wstring ctrlCls;
                    try { ctrlCls = ctrl->GetClassPrivate()->GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [Goat] post-reg controller class={}\n"),
                         ctrlCls.c_str());
                }

                // Throttle MoveToActor to 1 Hz per goat (well under PE budget).
                if ((now - g.lastMoveTickMs) < 1000) continue;
                g.lastMoveTickMs = now;

                // v1.1.0: per-second, write LeashActor blackboard key on
                // the AIController so Bst_NPCGoatWorkPorter_C's FollowPlayer
                // task has a target. EQS_Npc_PorterLeashPlayer doesn't pick
                // the player by default, so we set this directly. Re-set
                // every second in case the EQS clears it.
                setGoatLeashActor(ctrl, pawn);

                // v1.1.0 FIXUP: one-shot re-enable of SetIsInteractive(true)
                // on existing goats. Goats spawned by an earlier build had
                // interaction OFF (we suppressed the "E to rescue" prompt),
                // which also blocks inventory inspection. Without despawning
                // them (which would lose save state), force interaction back
                // on.
                if (!g.interactiveRefired)
                {
                    g.interactiveRefired = true;
                    auto* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
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
                                UObject* npcComp = readGoatParm<UObject*>(
                                    getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                                if (npcComp && isObjectAlive(npcComp))
                                {
                                    if (auto* siFn = npcComp->GetFunctionByNameInChain(STR("SetIsInteractive")))
                                    {
                                        struct { bool bValue{true}; } sip{};
                                        safeProcessEvent(npcComp, siFn, &sip);
                                        VLOG(STR("[MoriaCppMod] [Goat] FIXUP: SetIsInteractive(true) re-fired on existing goat={:p}\n"), (void*)goat);
                                    }

                                    // [v1.1.1 REVERT 2026-05-09] Per desktop
                                    // brief — with BP_NpcGoat_C in the
                                    // ValidNpcClasses whitelist, registration
                                    // may push the goat through the full
                                    // settlement-member state and surface
                                    // Details/Talk naturally. Test WITHOUT
                                    // disabling these bools first.
#if 0 // POST_v1_1_1_TEST
                                    uint8_t* base = reinterpret_cast<uint8_t*>(npcComp);
                                    *(base + 0x511) = 0; // bRescueInteractionEnabled = false
                                    *(base + 0x6C9) = 0; // bRecruitInteractionEnabled = false
                                    VLOG(STR("[MoriaCppMod] [Goat] FIXUP: Rescue + Recruit interactions disabled (Details/Talk should now show)\n"));
#endif
                                }
                            }
                        }
                    }
                }

                // Slow-cadence FSM state diagnostic — log every ~5s so we
                // can see whether the FSM is in "WorkTime" (Porter follow
                // active) or stuck in "Unaware" (idle wander).
                if ((g.ticksSinceSpawn % 300) == 0)
                    logGoatFSMState(ctrl, STR("tick"));

                // [v1.1.0 GENERIC MODE 2026-05-09] Manual MoveToActor follow
                // loop. Originally suspended when porter-role goats were
                // expected to follow via Bst_NPCGoatWorkPorter_C reading the
                // LeashActor blackboard. Re-enabled in rc.46 for bell-spawned
                // goats which DON'T have porter role assigned (we deliberately
                // skip assignPorterRole to avoid the dwarf-style management UI).
                // The manual tick drives follow regardless of BT state.
                // [rc.52] Stay mode: skip MoveToActor entirely so the goat
                // remains at its current position. Cleared when player picks
                // "Follow" from the menu.
                if (g.bellSpawned && !g.stayMode)
                {
                    auto* mtaFn = ctrl->GetFunctionByNameInChain(STR("MoveToActor"));
                    if (mtaFn)
                    {
                        int sz = mtaFn->GetParmsSize();
                        std::vector<uint8_t> buf(sz, 0);
                        writeGoatParm<UObject*>(mtaFn, buf.data(), STR("Goal"),               pawn);
                        writeGoatParm<float>   (mtaFn, buf.data(), STR("AcceptanceRadius"),   250.0f);
                        writeGoatParm<bool>    (mtaFn, buf.data(), STR("bStopOnOverlap"),     true);
                        writeGoatParm<bool>    (mtaFn, buf.data(), STR("bUsePathfinding"),    true);
                        writeGoatParm<bool>    (mtaFn, buf.data(), STR("bCanStrafe"),         false);
                        writeGoatParm<bool>    (mtaFn, buf.data(), STR("bAllowPartialPath"),  true);
                        safeProcessEvent(ctrl, mtaFn, buf.data());
                        if (g.moveToActorLogsRemaining > 0)
                        {
                            --g.moveToActorLogsRemaining;
                            VLOG(STR("[MoriaCppMod] [BellSpawn] MoveToActor->player issued (ctrl={:p}, accept=250)\n"),
                                 (void*)ctrl);
                        }
                    }
                }
            }
        }
