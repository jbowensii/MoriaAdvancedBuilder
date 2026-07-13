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

        // UTF-8 double-encoding tolerance: saves may hold a mojibake copy
        // of the goat name (see memory goat-final-architecture → "name
        // encoding"). Matching both forms lets the marker scans find the
        // entry; the idempotent identity write then repairs it.
        std::wstring goatNameMojibake() const
        {
            std::wstring out;
            for (wchar_t c : m_goatName)
            {
                if (c < 0x80) out += c;
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
            float lastDiagPos[3]{0,0,0};                  // [rc.138] follow-motion diagnostic: last sampled goat location
            ULONGLONG lastDiagMs{0};                      // [rc.138] timestamp of last motion sample
            bool maxSpeedLogged{false};                   // [rc.138] one-shot MaxWalkSpeed log
            bool brainStopped{false};                     // [v8.2.x] one-shot StopLogic on the registered-NPC brain (~1s post-spawn, after possession)
            ULONGLONG lastBrainStopMs{0};                 // [v8.2.x] FOLLOW-ALWAYS: 10s re-assert timestamp for the FSM disable
        };
        std::vector<FollowGoatRecord> m_followGoats;

        // Companion goat is single-instance only. NUM- is now a toggle:
        // press to spawn if none exists, press to despawn if one does.
        static constexpr size_t MAX_FOLLOW_GOATS = 1;

        // [rc.41 PROBE N 2026-06-10] Fires once at ~3s post-character-load
        // BEFORE any bell-ring. Dumps NpcInfo state immediately after save
        // restore so we can empirically tell whether goat-shaped entries
        // (Role='Default' + Rescued=N) survived the save/reload cycle.
        // If they do, GUID adoption in spawnBellGoat will find them.
        bool m_probeNFired{false};

        // [rc.44 PROBE P 2026-06-10] Fires once at ~5s post-character-load.
        // Empirical world scan to determine WHAT actually persists after
        // save/reload: goat actors, body containers, item actors. Tells us
        // whether item persistence is achievable via reconnection (containers
        // exist) or requires sidecar JSON (nothing persists).
        bool m_probePFired{false};

        // [rc.45 ROLE STABILITY 2026-06-10] Set true when spawnBellGoat's
        // adoption block writes an existing GUID into the new actor.
        // Read by the bell-spawn-tail to SKIP assignPorterRole — adopted
        // goats keep Role='Default' permanently (no role drift), so our
        // simple Role='Default' filter stays unambiguous across cycles.
        // Reset to false at the start of each spawnBellGoat call.
        bool m_lastSpawnGuidAdopted{false};

        // [rc.47 PROBE Q 2026-06-12] Per DC brief: hunt for manager-side
        // NPC-restore UFunction(s) that the engine uses to respawn dwarves
        // with items intact on world load. Three parts:
        //   Q.1 — unfiltered UFunction dump on MorNPCManager
        //   Q.2 — unfiltered UFunction dump on MorSaveSystemWorldState
        //   Q.3 — hex dump of FMorNpcPersistentData unknown region
        //         (+0x048..+0x190, 0x148 bytes) for dwarf-shape entries.
        //         Looking for TArray header patterns + FGuid patterns +
        //         counts that match items the user has placed on a dwarf.
        // Fires once at ~7s post-character-load (after Probes N+P).
        bool m_probeQFired{false};

        // [rc.48 PROBE R 2026-06-14] Walks WorldState's class properties
        // looking for the registered-runtime-actor list/map. We know
        // StoreRuntimeActor exists (Q.2) but don't know which property
        // it writes to internally. Probe R dumps every ArrayProperty,
        // MapProperty, and ObjectProperty on BP_MorSaveSystemWorldState_C
        // so DC can identify the registration record from the names.
        // Fires once at ~9s post-character-load (after Probes N+P+Q).
        bool m_probeRFired{false};

        // [rc.49 PROBE S 2026-06-14] User correction: pivot from Role
        // marker (drifts on save/reload) to Name marker (per their
        // original direction). Probe S enumerates ALL UFunctions on
        // MorNPCComponent with full parm signatures. We need to find
        // SetName / SetCustomName / Rename / SetDisplayName / etc. — any
        // UFunction that mutates the entry's Name FText. Fires once at
        // ~11s post-character-load (after N+P+Q+R).
        bool m_probeSFired{false};

        // [rc.50 PROBE T 2026-06-14] Probe S confirmed MorNPCComponent
        // has only Name GETTERS + CanSetCustomDisplayName check, but
        // no SETTER. The "Can" check strongly implies a setter exists
        // on a different class. Probe T enumerates BP_NpcGoat_C
        // (actor class) for name-write candidates so we find the
        // setter and call it. Fires once at ~13s post-character-load.
        bool m_probeTFired{false};

        // [rc.53 PROBE U 2026-06-15] IMorSaveGameObject diagnostic.
        // Dumps SaveGameObjectIgnore / GetId / GetDormancy on the
        // live goat AND on a nearby BP_StorageChest_Construction_C
        // for side-by-side comparison. Chests ALWAYS persist —
        // their interface state is the reference we need to match.
        // Fires once at ~15s post-character-load.
        bool m_probeUFired{false};

        // [rc.54 PROBE V 2026-06-16] ValidNpc{Classes,Restores,Roles}
        // dump on BP_NPCManager_C. Probe U revealed SaveGameObjectId
        // lives on the MANAGER (off=0x0790), not individual NPCs.
        // Manager recreates per-NPC actors from NpcInfo metadata,
        // gated by ValidNpcClasses (off=0x0808). If BP_NpcGoat_C is
        // not in that list, the actor never respawns on reload no
        // matter what we do. Probe V iterates each TArray and prints
        // every entry so we can confirm presence/absence.
        // Fires once at ~17s post-character-load.
        bool m_probeVFired{false};

        // [rc.55 PROBE W 2026-06-16] FMorNpcPersistentData full dump.
        // Probe V confirmed BP_NpcGoat_C IS in ValidNpcClasses (slot
        // 12). The restore gate must be per-entry — most likely the
        // UniqueNpc (FMorUniqueNpcRowHandle) or StaticNpcData
        // (FDataTableRowHandle) row handle being empty/unresolvable
        // for our bell-spawned goat. Probe W walks the FMorNPCInfo →
        // PersistentData struct via reflection, logging every field
        // for every entry. We compare goat[0] to any vanilla
        // dwarves[1..] in the same save to identify the gating field.
        // Fires once at ~20s post-character-load.
        bool m_probeWFired{false};

        // [rc.56 PROBE X 2026-06-16] NPCUnique DataTable enumeration.
        // Vanilla quest NPCs (Warden/Wanderer/Emissary) persist via
        // UniqueNpc.RowName lookup in NPCUnique. We need to write the
        // goat's row name into our bell-spawned NpcInfo entry. Probe X
        // finds the DataTable whose RowStruct=MorUniqueNPCDefinition,
        // iterates rows, reads CharacterName + CharacterClass softpath
        // per row, and logs the row whose CharacterClass = BP_NpcGoat.
        // That row name is the exact string rc.57 will write into
        // UniqueNpc.RowName on bell-spawn.
        // Fires once at ~22s post-character-load.
        bool m_probeXFired{false};

        // [rc.57 PROBE Y 2026-06-23] Wider net for goat row search.
        // Probe X (rc.56) found DT_NPCUniqueCharacters with 12 rows,
        // none being the goat. Probe X also had a CharacterClass
        // decode bug (read at +0 instead of +16 within TSoftClassPtr).
        // Probe Y:
        //   1. Fixes CharacterClass offset (+16 for AssetPathName,
        //      +24 for SubPath)
        //   2. Iterates ALL DataTables with RowStruct=MorUniqueNPCDefinition
        //      (not just first hit)
        //   3. Brute-force scans every loaded DataTable's RowMap for
        //      any FName in any row containing "NpcGoat" — catches
        //      the goat regardless of row struct type
        // Fires once at ~24s post-character-load.
        bool m_probeYFired{false};

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

        // [rc.48 PENDING STORE 2026-06-14] Per DC's Q3 answer: call
        // WorldState.StoreRuntimeActor AFTER FinishSpawning + at least
        // one frame of tick delay (so ConstructionScript + BeginPlay
        // complete first). We queue the goat into this list at
        // bell-spawn end, then fire StoreRuntimeActor from
        // tickFollowGoats once readyMs has elapsed.
        struct PendingStoreRecord {
            RC::Unreal::FWeakObjectPtr goat;
            ULONGLONG readyMs{0};   // GetTickCount64() value at which to fire
        };
        std::vector<PendingStoreRecord> m_pendingStores;

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
                    for (UObject* g : hit) out->push_back(g);
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
            if (m_kismetGameplayStaticsCDO && safeObjectName(m_kismetGameplayStaticsCDO).empty())
                m_kismetGameplayStaticsCDO = nullptr;

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
            if (!ensureGoatSpawnBindings()) { VLOG(STR("[MoriaCppMod] [GoatChest] spawn bindings not ready\n")); return; }
            UClass* chestCls = ensurePhantomChestClass();
            if (!chestCls) { VLOG(STR("[MoriaCppMod] [GoatChest] chest class missing\n")); return; }

            FVec3f loc{};
            if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
            {
                struct { FVec3f Ret{}; } lp{};
                if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret;
            }
            FTransformRaw xform{};
            xform.Rotation = {0.0f,0.0f,0.0f,1.0f};
            xform.Translation = { loc.X, loc.Y, loc.Z - 50.0f };  // slightly under the goat
            xform.Scale3D = {1.0f,1.0f,1.0f};

            std::vector<uint8_t> buf(m_goatBeginSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
            writeGoatParm<UClass*>      (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         chestCls);
            writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
            writeGoatParm<uint8_t>      (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);
            if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) return;
            UObject* chest = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!chest) { VLOG(STR("[MoriaCppMod] [GoatChest] BeginDeferred returned null\n")); return; }

            std::vector<uint8_t> buf2(m_goatFinishSpawnFn->GetParmsSize(), 0);
            writeGoatParm<UObject*>     (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          chest);
            writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
            safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());

            // Hide it and kill collision so the player never sees or bumps it.
            if (auto* hideFn = chest->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
            { struct { bool b{true}; } p{}; try { safeProcessEvent(chest, hideFn, &p); } catch (...) {} }
            if (auto* collFn = chest->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
            { struct { bool b{false}; } p{}; try { safeProcessEvent(chest, collFn, &p); } catch (...) {} }

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
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            UObject* chestInv = nullptr;
            if (invCls)
            {
                if (auto* getComp = m_hiddenGoatChest->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> b(getComp->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getComp, b.data(), STR("ComponentClass"), invCls);
                    if (safeProcessEvent(m_hiddenGoatChest, getComp, b.data()))
                        chestInv = readGoatParm<UObject*>(getComp, b.data(), STR("ReturnValue"), nullptr);
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
                try { safeProcessEvent(chestInv, gcFn, gb.data()); } catch (...) {}
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

        // [rc.13 2026-05-24] NpcMgr probe — Desktop Claude's Path D validation.
        // Hypothesis: the cross-actor inventory mutation gate is at the
        // UMorNPCManager level, not the component level. RegisterWithNPCManager
        // (rc.12h) assigns the goat a guid + adds to manager state, but
        // somehow the manager still rejects player-side Take/Add ops.
        //
        // Three things this probe does on first bell-summon:
        //   1. Acquire UMorNPCManager singleton (via MoriaUtils::GetNpcManager
        //      static UFunction, fall back to findAllOfSafe scan).
        //   2. Enumerate ALL properties + UFunctions on the manager's UClass
        //      and log them — gives Desktop Claude the real names/types
        //      to plan rc.14 against if Methods below don't crack it.
        //   3. Method 1: append BP_NpcGoat_C path to ValidNpcClasses
        //      TArray<FSoftClassPath>. Headroom-checked; skipped if Max==Num.
        //   4. Method 2: try a candidate list of registration UFunctions
        //      (RegisterNpc / AddNpc / OnNpcSpawned / etc) and call any
        //      that exist with the goat actor as the first parm.
        //
        // One-shot per session via static flag. After this fires, user
        // can E-click Saddlebags to test if any of the methods unlocked
        // the UI's container mutations.
        // [rc.24 PATH X helper] Read AMorNPCManager.NpcInfo.Items.Num() once.
        // Resolves the manager via the same MoriaUtils::GetNpcManager + findAllOfSafe
        // fallback chain that runNpcMgrProbe uses. Returns -1 if manager not findable
        // or NpcInfo.Items header is unreadable.
        //
        // Per saved memory (npc-recovery-architecture.md): NpcInfo struct is at
        // mgr+0x03a0, with TArray<FMorNPCInfo> Items at offset 0x0108 inside the
        // struct. TArray header is Data(8B) + Num(4B) + Max(4B) = 16B at the array
        // base. Num lives at +8.
        ULONGLONG m_pathXDelayedProbeMs{0};
        int32_t readNpcInfoItemsNum()
        {
            UObject* mgr = nullptr;
            // Path 1: MoriaUtils::GetNpcManager static UFunction
            UClass* utilsCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MoriaUtils"));
            if (utilsCls && isObjectAlive(utilsCls))
            {
                UObject* utilsCDO = nullptr;
                try { utilsCDO = utilsCls->GetClassDefaultObject(); } catch (...) {}
                if (utilsCDO && isObjectAlive(utilsCDO))
                {
                    auto* getMgrFn = utilsCls->GetFunctionByNameInChain(STR("GetNpcManager"));
                    if (getMgrFn)
                    {
                        std::vector<uint8_t> buf(getMgrFn->GetParmsSize(), 0);
                        auto* pWC = findParam(getMgrFn, STR("WorldContextObject"));
                        if (pWC && m_localPC)
                            *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = m_localPC;
                        try { safeProcessEvent(utilsCDO, getMgrFn, buf.data()); } catch (...) {}
                        auto* pRet = findParam(getMgrFn, STR("ReturnValue"));
                        if (pRet)
                            mgr = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
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
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
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

        // Called from gameThreadTick to fire the deferred +1s NpcInfo snapshot.
        void tickPathXDelayedProbe()
        {
            if (m_pathXDelayedProbeMs == 0) return;
            ULONGLONG now = GetTickCount64();
            if (now < m_pathXDelayedProbeMs) return;
            int32_t n = readNpcInfoItemsNum();
            VLOG(STR("[MoriaCppMod] [PathX-E] +1000ms-DEFERRED NpcInfo.Items.Num={}\n"), n);
            m_pathXDelayedProbeMs = 0;  // one-shot
        }

        void runNpcMgrProbe(UObject* goat)
        {
            static bool s_probed = false;
            if (s_probed) return;
            s_probed = true;

            if (!goat || !isObjectAlive(goat))
            {
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] goat null/dead — skip\n"));
                return;
            }

            // --- Step 1a: try MoriaUtils::GetNpcManager static UFunction ---
            UObject* mgr = nullptr;
            UClass* utilsCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MoriaUtils"));
            if (utilsCls && isObjectAlive(utilsCls))
            {
                UObject* utilsCDO = nullptr;
                try { utilsCDO = utilsCls->GetClassDefaultObject(); } catch (...) {}
                if (utilsCDO && isObjectAlive(utilsCDO))
                {
                    auto* getMgrFn = utilsCls->GetFunctionByNameInChain(STR("GetNpcManager"));
                    if (getMgrFn)
                    {
                        VLOG(STR("[MoriaCppMod] [NpcMgrProbe] calling MoriaUtils::GetNpcManager\n"));
                        std::vector<uint8_t> buf(getMgrFn->GetParmsSize(), 0);
                        auto* pWC = findParam(getMgrFn, STR("WorldContextObject"));
                        if (pWC && m_localPC)
                            *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = m_localPC;
                        try { safeProcessEvent(utilsCDO, getMgrFn, buf.data()); } catch (...) {}
                        auto* pRet = findParam(getMgrFn, STR("ReturnValue"));
                        if (pRet)
                            mgr = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [NpcMgrProbe] MoriaUtils::GetNpcManager UFunction not found\n"));
                    }
                }
            }
            // --- Step 1b: fallback via findAllOfSafe ---
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] MoriaUtils route failed — falling back to findAllOfSafe\n"));
                std::vector<UObject*> candidates;
                if (findAllOfSafe(STR("MorNPCManager"), candidates))
                {
                    for (UObject* o : candidates)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring cn = safeClassName(o);
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                        mgr = o;
                        break;
                    }
                }
            }
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] manager not findable via either path — bail\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] manager={:p} class={}\n"),
                 (void*)mgr, safeClassName(mgr).c_str());

            UClass* mgrCls = nullptr;
            try { mgrCls = mgr->GetClassPrivate(); } catch (...) {}
            if (!mgrCls)
            {
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] manager class null — bail\n"));
                return;
            }

            // --- Step 2: Enumerate ALL properties on manager class ---
            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] === ALL Properties on manager ===\n"));
            int propCount = 0;
            FProperty* validProp = nullptr;
            try {
                for (auto* p : mgrCls->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    if (propCount >= 400) break;
                    std::wstring pn;
                    try { pn = p->GetName(); } catch (...) { continue; }
                    std::wstring pcn;
                    try { pcn = p->GetClass().GetName(); } catch (...) {}
                    int32 off = -1;
                    try { off = p->GetOffset_Internal(); } catch (...) {}
                    int32 sz = -1;
                    try { sz = p->GetSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [NpcMgrProbe]   Prop: {} [type={}] off=0x{:04x} size={}\n"),
                         pn.c_str(), pcn.c_str(), (unsigned)off, sz);
                    if (pn == STR("ValidNpcClasses")) validProp = p;
                    ++propCount;
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] === end ({} properties) ===\n"), propCount);

            // --- Step 3: Enumerate UFunctions, log all and registration candidates ---
            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] === ALL UFunctions on manager ===\n"));
            int fnCount = 0;
            try {
                for (auto* fn : mgrCls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    if (fnCount >= 400) break;
                    std::wstring fname;
                    try { fname = fn->GetName(); } catch (...) { continue; }
                    int parms = 0;
                    try { parms = fn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [NpcMgrProbe]   Func: {} parmSize={}\n"),
                         fname.c_str(), parms);
                    ++fnCount;
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] === end ({} functions) ===\n"), fnCount);

            // [rc.23 PATH X PROBES — Desktop Claude, 2026-05-25]
            // DC's Path X plan requires three probes BEFORE any mutation:
            //   Probe A: CPF_SaveGame flag on ValidNpcClasses + NpcInfo
            //            (decides if memory-only mutation is safe)
            //   Probe B: Register-shaped UFunction param details
            //            (so DC can pick the right call to make)
            //   Probe D: NpcInfo TArray count snapshot — if registration is
            //            adding our goat, this number should grow
            // CPF_SaveGame = 0x0000000001000000 (bit 24)
            VLOG(STR("[MoriaCppMod] [PathX-A] === Probe A: PropertyFlags + CPF_SaveGame ===\n"));
            try {
                for (auto* p : mgrCls->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    std::wstring pn; try { pn = p->GetName(); } catch (...) { continue; }
                    // Only care about ValidNpcClasses and NpcInfo for safety analysis.
                    if (pn != STR("ValidNpcClasses") && pn != STR("NpcInfo")) continue;
                    uint64_t flags = 0;
                    try { flags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                    bool savegame = (flags & 0x0000000001000000ULL) != 0;
                    VLOG(STR("[MoriaCppMod] [PathX-A]   {} flags=0x{:016x} CPF_SaveGame={}\n"),
                         pn.c_str(), flags, savegame ? STR("YES") : STR("no"));
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-A] === end Probe A ===\n"));

            VLOG(STR("[MoriaCppMod] [PathX-B] === Probe B: register-shaped UFunctions w/ parm detail ===\n"));
            try {
                for (auto* fn : mgrCls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                    bool match =
                        fname.find(STR("Register")) != std::wstring::npos ||
                        fname.find(STR("Add"))      != std::wstring::npos ||
                        fname.find(STR("Npc"))      != std::wstring::npos ||
                        fname.find(STR("NPC"))      != std::wstring::npos ||
                        fname.find(STR("Spawn"))    != std::wstring::npos;
                    if (!match) continue;
                    int parmsSize = 0;
                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                    int parmCount = 0;
                    VLOG(STR("[MoriaCppMod] [PathX-B]   Fn: {} parmsSize={}\n"),
                         fname.c_str(), parmsSize);
                    try {
                        for (auto* pit : fn->ForEachProperty())
                        {
                            if (!pit) continue;
                            uint64_t pflags = 0;
                            try { pflags = static_cast<uint64_t>(pit->GetPropertyFlags()); } catch (...) {}
                            bool isParm = (pflags & 0x0000000000000080ULL) != 0;  // CPF_Parm
                            if (!isParm) continue;
                            std::wstring pname; try { pname = pit->GetName(); } catch (...) {}
                            std::wstring ptype; try { ptype = pit->GetClass().GetName(); } catch (...) {}
                            int poff = -1; try { poff = pit->GetOffset_Internal(); } catch (...) {}
                            int psz = -1; try { psz = pit->GetSize(); } catch (...) {}
                            bool isReturn = (pflags & 0x0000000000000400ULL) != 0; // CPF_ReturnParm
                            bool isOut    = (pflags & 0x0000000000000100ULL) != 0; // CPF_OutParm
                            VLOG(STR("[MoriaCppMod] [PathX-B]     parm[{}] '{}' type={} off=0x{:04x} size={} ret={} out={} flags=0x{:016x}\n"),
                                 parmCount, pname.c_str(), ptype.c_str(),
                                 (unsigned)poff, psz,
                                 isReturn ? STR("Y") : STR("n"),
                                 isOut    ? STR("Y") : STR("n"),
                                 pflags);
                            ++parmCount;
                        }
                    } catch (...) {}
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-B] === end Probe B ===\n"));

            // Probe D: snapshot NpcInfo.Items.Num() to see current registration count.
            // NpcInfo is StructProperty at +0x03a0; struct holds TArray<FMorNPCInfo>
            // at offset 0x0108 (per [NpcRecovery] log "NpcInfo offsets resolved").
            VLOG(STR("[MoriaCppMod] [PathX-D] === Probe D: NpcInfo.Items count snapshot ===\n"));
            try {
                FProperty* npcInfoProp = nullptr;
                for (auto* p : mgrCls->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    std::wstring pn; try { pn = p->GetName(); } catch (...) { continue; }
                    if (pn == STR("NpcInfo")) { npcInfoProp = p; break; }
                }
                if (npcInfoProp)
                {
                    int32 niOff = npcInfoProp->GetOffset_Internal();
                    uint8_t* niBase = reinterpret_cast<uint8_t*>(mgr) + niOff;
                    // Items TArray at +0x0108 inside the struct (per saved memory).
                    uint8_t* itemsHdr = niBase + 0x0108;
                    if (isReadableMemory(itemsHdr, 16))
                    {
                        int32_t itemsNum = *reinterpret_cast<int32_t*>(itemsHdr + 8);
                        int32_t itemsMax = *reinterpret_cast<int32_t*>(itemsHdr + 12);
                        VLOG(STR("[MoriaCppMod] [PathX-D]   NpcInfo.Items: Num={} Max={} (at base+0x{:04x}+0x0108)\n"),
                             itemsNum, itemsMax, (unsigned)niOff);
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [PathX-D]   NpcInfo.Items header unreadable at base+0x{:04x}+0x0108\n"), (unsigned)niOff);
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PathX-D]   NpcInfo property not found\n"));
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-D] === end Probe D ===\n"));

            // [rc.23 PATH X — STOP HERE per DC's Phase-1 directive 2026-05-25]
            // The original Step 4 (direct-memcpy ValidNpcClasses mutation) and
            // Step 5 (RegisterNpc-shaped UFunction firing) are DISABLED while
            // DC reviews Probe A/B/D output. Re-enable by removing the #if 0
            // wrappers after DC confirms save-safety.
#if 0
            // --- Step 4: Method 1 — append BP_NpcGoat_C to ValidNpcClasses ---
            if (validProp)
            {
                auto* arrProp = static_cast<RC::Unreal::FArrayProperty*>(validProp);
                auto* inner = arrProp->GetInner();
                int32 innerSize = 0;
                std::wstring innerType;
                if (inner)
                {
                    try { innerSize = inner->GetSize(); } catch (...) {}
                    try { innerType = inner->GetClass().GetName(); } catch (...) {}
                }
                int32 arrOff = arrProp->GetOffset_Internal();
                uint8_t* arrField = reinterpret_cast<uint8_t*>(mgr) + arrOff;
                if (isReadableMemory(arrField, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(arrField);
                    int32_t arrNum = *reinterpret_cast<int32_t*>(arrField + 8);
                    int32_t arrMax = *reinterpret_cast<int32_t*>(arrField + 12);
                    VLOG(STR("[MoriaCppMod] [NpcMgrProbe] ValidNpcClasses BEFORE: num={} max={} elemSize={} innerType='{}'\n"),
                         arrNum, arrMax, innerSize, innerType.c_str());
                    if (innerSize > 0 && arrData && arrNum < arrMax)
                    {
                        // Headroom available. Construct new entry.
                        uint8_t* slot = arrData + arrNum * innerSize;
                        if (isReadableMemory(slot, innerSize))
                        {
                            // Clone an existing entry as template (preserves
                            // proper FString/etc initialization), then write
                            // our FName at offset 0.
                            if (arrNum > 0)
                            {
                                std::memcpy(slot, arrData, innerSize);  // clone entry[0]
                            }
                            else
                            {
                                std::memset(slot, 0, innerSize);  // no template; zero-init
                            }
                            // Write our FName at offset 0 (FSoftObjectPath.AssetPathName).
                            RC::Unreal::FName goatPath(
                                STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"),
                                RC::Unreal::FNAME_Add);
                            std::memcpy(slot, &goatPath, sizeof(RC::Unreal::FName));
                            // Bump count.
                            int32_t newNum = arrNum + 1;
                            std::memcpy(arrField + 8, &newNum, 4);
                            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] ValidNpcClasses AFTER: num={} (appended BP_NpcGoat_C path at slot {})\n"),
                                 newNum, arrNum);
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] new slot unreadable — append skipped\n"));
                        }
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [NpcMgrProbe] no headroom or zero size — append skipped\n"));
                    }
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [NpcMgrProbe] ValidNpcClasses TArray header unreadable\n"));
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] ValidNpcClasses property not found via enumeration\n"));
            }

            // --- Step 5: Method 2 — try registration UFunction candidates ---
            const wchar_t* regCandidates[] = {
                STR("RegisterNpc"), STR("AddNpc"), STR("OnNpcSpawned"),
                STR("RegisterNPC"), STR("AddNPC"), STR("OnNPCSpawned"),
                STR("AddNpcToInfo"), STR("RegisterNpcInfo"),
                STR("AddNpcEntry"), STR("InsertNpc"), STR("TrackNpc"),
                STR("RegisterManagedNpc"), STR("AddManagedNpc"),
                nullptr
            };
            for (const wchar_t** p = regCandidates; *p; ++p)
            {
                auto* fn = mgr->GetFunctionByNameInChain(*p);
                if (!fn) continue;
                int parmsSize = 0;
                try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] candidate UFunction '{}' PRESENT (parmSize={}), invoking with goat as 1st param\n"),
                     *p, parmsSize);
                std::vector<uint8_t> buf(parmsSize, 0);
                // Write goat actor at offset 0 (most common 1st-parm pattern)
                if (parmsSize >= sizeof(UObject*))
                {
                    *reinterpret_cast<UObject**>(buf.data()) = goat;
                }
                try { safeProcessEvent(mgr, fn, buf.data()); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [NpcMgrProbe] '{}' dispatched\n"), *p);
            }
#endif // PATH X — review Probe A/B/D output before re-enabling Steps 4-5

            VLOG(STR("[MoriaCppMod] [NpcMgrProbe] === probe complete — Path X probes A/B/D logged above ===\n"));
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
            setBoolByName(STR("bDetailsInteractionEnabled"),    true,  STR("details"));   // Follow/Stay row — KEEP
            // [rc.94 2026-07-10] RE-ENABLE Manage. Tobi's v1.11.0/v1.12.0 ships
            // the full craft+equip flow: craft PorterGoatSaddlebags (DT_ItemRecipes
            // -> EpicPack.Goat_Saddlebags) then drop the pack into the goat's
            // epic-pack slot (Goat.Slot.EpicPack) via the goat's native Manage
            // interaction -> 8x8 Goat_Saddlebags. rc.84 had disabled Manage; that
            // suppressed exactly the interaction the player needs. Turn it back on
            // and let the native path own the goat storage UI.
            setBoolByName(STR("bManageInteractionEnabled"),     true,  STR("manage-ON"));
            // Register companions (per desktop: probably bind to input system).
            setBoolByName(STR("bTalkInteractionRegister"),      true,  STR("talk-reg"));
            setBoolByName(STR("bDetailsInteractionRegister"),   true,  STR("details-reg"));
            setBoolByName(STR("bManagerInteractionRegister"),   true,  STR("manage-reg-ON"));
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
                if (seh_findAnyGoatActor(&hit) && !hit.empty())
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
            // [rc.16.1 2026-05-25] Tobi's Secrets of Khazad-dum ships the bell
            // as EQ_GoatBell_C (pickaxe-class tool) at the same path family.
            // Legacy BP_PorterGoatBell_C (from PorterGoatBell_v1.1.1 pak) is
            // no longer present in v1.6.0 installs. Try Tobi's class first;
            // fall back to legacy for users on older pak versions.
            static constexpr const wchar_t* BELL_PATHS[] = {
                STR("/Game/Mods/PorterGoat/Items/EQ_GoatBell.EQ_GoatBell_C"),
                STR("/Game/Mods/PorterGoat/Items/BP_PorterGoatBell.BP_PorterGoatBell_C"),
            };
            for (auto* p : BELL_PATHS)
            {
                if (grantPorterItemToPlayer(p, STR("Bell")))
                {
                    showOnScreen(L"Bell of the Goat granted!", 3.0f, 0.4f, 0.9f, 0.4f);
                    return;
                }
            }
            showOnScreen(L"Bell grant failed — see log", 2.5f, 0.9f, 0.4f, 0.4f);
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

        // [rc.64 ROLE HELPER 2026-06-28] Slim SetRoleFuzzy invoker — no
        // Register, no SetIsInteractive, no equipment refresh. Just changes
        // the role on the goat's MorNPCComponent. Used by onGoatFollow /
        // onGoatStay to toggle Porter <-> Wanderer per approved plan.
        bool setRoleFuzzyOnGoat(UObject* goat, const wchar_t* roleStr)
        {
            if (!goat || !isObjectAlive(goat) || !roleStr) return false;
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
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
            *reinterpret_cast<void**>   (fstr + 0)  = strBuf;
            *reinterpret_cast<int32_t*> (fstr + 8)  = strLen;
            *reinterpret_cast<int32_t*> (fstr + 12) = strLen;
            try { safeProcessEvent(npcComp, setRoleFn, buf2.data()); } catch (...) {}
            VLOG(STR("[MoriaCppMod] [GoatMenu] setRoleFuzzy('{}') fired on goat={:p} npcComp={:p}\n"),
                 roleStr, (void*)goat, (void*)npcComp);
            return true;
        }

        // Stay is retired (see memory: goat-final-architecture). Clears the
        // Talk-slot flag behind the Follow/Stay row; the Manage slot
        // (Saddlebags) is untouched. NOTE: Tobi's v1.12 row ignores this
        // flag — the dispatch hook's click-neutralizer is the real removal.
        void removeGoatFollowStayRow(UObject* goat)
        {
            if (!goat || !isObjectAlive(goat)) return;
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!npcCompCls) return;
            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (!getCompFn) return;
            std::vector<uint8_t> gbuf(getCompFn->GetParmsSize(), 0);
            writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), npcCompCls);
            if (!safeProcessEvent(goat, getCompFn, gbuf.data())) return;
            UObject* npcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
            if (!npcComp || !isObjectAlive(npcComp)) return;
            if (auto* flag = npcComp->GetValuePtrByPropertyNameInChain<bool>(STR("bTalkInteractionEnabled")))
            {
                *flag = false;
                VLOG(STR("[MoriaCppMod] [GoatMenu] Follow/Stay row REMOVED (bTalkInteractionEnabled=false on npcComp={:p})\n"),
                     (void*)npcComp);
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
            if (!fn) { VLOG(STR("[MoriaCppMod] [GoatGait] SetGait UFunction missing\n")); return; }
            std::vector<uint8_t> b(fn->GetParmsSize(), 0);
            b[0] = 1;  // EFGKGait::Running
            try { safeProcessEvent(goat, fn, b.data()); } catch (...) {}
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
                try { aCls = owner->GetClassPrivate(); } catch (...) {}
                if (!aCls) return 0;
                for (auto* strct = static_cast<UStruct*>(aCls); strct;
                     strct = strct->GetSuperStruct())
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
                                if (safeProcessEvent(comp, isActFn, ab.data()))
                                    active = ab[0] != 0;
                            }
                            if (!active) continue;
                            VLOG(STR("[MoriaCppMod] [GoatBrain] FSM re-activated by game — re-disabling (cls={})\n"), cls.c_str());
                        }
                        if (auto* deactFn = comp->GetFunctionByNameInChain(STR("Deactivate")))
                        { try { safeProcessEvent(comp, deactFn, nullptr); } catch (...) {} }
                        if (auto* tickFn = comp->GetFunctionByNameInChain(STR("SetComponentTickEnabled")))
                        {
                            std::vector<uint8_t> tb(tickFn->GetParmsSize(), 0);
                            tb[0] = 0;  // bEnabled = false
                            try { safeProcessEvent(comp, tickFn, tb.data()); } catch (...) {}
                        }
                        ++disabled;
                        VLOG(STR("[MoriaCppMod] [GoatBrain] FSM comp DISABLED on {}: cls={} ptr={:p} (reason='{}')\n"),
                             ownerLabel, cls.c_str(), (void*)comp, reason);
                    }
                }
                return disabled;
            };

            int total = disableFsmOn(goat, STR("pawn"));
            auto* ctrlPtr = goat->GetValuePtrByPropertyNameInChain<UObject*>(STR("Controller"));
            UObject* ctrl = (ctrlPtr && *ctrlPtr) ? *ctrlPtr : nullptr;
            if (ctrl && isObjectAlive(ctrl)) total += disableFsmOn(ctrl, STR("controller"));

            if (total == 0 && !onlyIfActive)
                VLOG(STR("[MoriaCppMod] [GoatBrain] no FSM components found on goat={:p} (reason='{}')\n"),
                     (void*)goat, reason);
            return total > 0;
        }

        void onGoatFollow()
        {
            // [rc.64 ROLE TOGGLE 2026-06-28] Per approved plan: FOLLOW
            // = SetRoleFuzzy("Porter") + stayMode=false so tick re-asserts
            // LeashActor. The role toggle is defense-in-depth per user
            // directive; LeashActor refresh in tick is what actually drives
            // the vanilla Bst_NPCGoatWorkPorter follow.
            for (auto& rec : m_followGoats) rec.stayMode = false;
            for (auto& rec : m_followGoats)
            {
                UObject* goat = rec.pawn.Get();
                if (goat && isObjectAlive(goat)) setRoleFuzzyOnGoat(goat, STR("Porter"));
                if (goat && isObjectAlive(goat)) stopGoatBrainLogic(goat, STR("MoriaCppMod Follow"));
                if (goat && isObjectAlive(goat)) setGoatGaitRunning(goat);
                // Movement mode can be stuck on MOVE_None from a prior bell
                // dismiss — force Walking (idempotent).
                if (goat && isObjectAlive(goat))
                {
                    UClass* mvCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                        STR("/Script/Engine.CharacterMovementComponent"));
                    if (mvCls)
                    {
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
                                        mb[0] = 1;  // MOVE_Walking
                                        try { safeProcessEvent(mv, sm, mb.data()); } catch (...) {}
                                        VLOG(STR("[MoriaCppMod] [rc.137] Follow: movement mode forced to Walking\n"));
                                    }
                            }
                        }
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatMenu] FOLLOW — role=Porter, stayMode=false (tick re-asserts LeashActor)\n"));
            showOnScreen(L"Porter Goat: following", 1.5f, 0.7f, 0.9f, 0.7f);
            clearGoatInjectedRows();
        }

        void onGoatStay()
        {
            // [rc.64 ROLE TOGGLE 2026-06-28] Per approved plan: STAY =
            // SetRoleFuzzy("Wanderer") + clearGoatLeashActor + StopMovement
            // + stayMode=true so tick STOPS re-asserting LeashActor. Result:
            // vanilla Bst_NPCGoatWorkPorter has no LeashActor target -> idles.
            for (auto& rec : m_followGoats) rec.stayMode = true;
            for (auto& rec : m_followGoats)
            {
                UObject* goat = rec.pawn.Get();
                if (!goat || !isObjectAlive(goat)) continue;
                setRoleFuzzyOnGoat(goat, STR("Wanderer"));
                // FSM must be down BEFORE StopMovement or it re-issues moves.
                stopGoatBrainLogic(goat, STR("MoriaCppMod Stay"));
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
                    clearGoatLeashActor(ctrl);
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatMenu] STAY — role=Wanderer + StopMovement + clearLeash + stayMode=true\n"));
            showOnScreen(L"Porter Goat: staying", 1.5f, 0.7f, 0.9f, 0.7f);
            clearGoatInjectedRows();
        }

        void onGoatWander()
        {
            // [rc.42 2026-06-10] No role change. Same logic as onGoatFollow
            // — clears stayMode so manual MoveToActor tick re-engages.
            // Wander as a distinct user-facing action is being deprecated
            // along with Stay per the "always follow" decision.
            for (auto& rec : m_followGoats) rec.stayMode = false;
            VLOG(STR("[MoriaCppMod] [GoatMenu] WANDER — stayMode=false (role unchanged, permanent Porter)\n"));
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
        // [rc.64 B3 HELPER 2026-06-28] Walk player's MorInventoryComponent
        // Items.List and return the FItemInstance ID whose Item UClass name
        // matches the given class-name substring. Returns 0 if not found.
        int32_t findPlayerItemIdByClassName(UObject* playerInv, const wchar_t* classNameSubstr)
        {
            if (!playerInv || !isObjectAlive(playerInv) || !classNameSubstr) return 0;
            FProperty* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items"));
            if (!itemsProp) return 0;
            uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16)) return 0;
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
            if (!arrData || arrNum <= 0) return 0;
            int stride  = iiSize();
            int itemOff = iiItemOff();
            int idOff   = iiIDOff();
            for (int i = 0; i < arrNum && i < 256; ++i)
            {
                uint8_t* entry = arrData + i * stride;
                if (!isReadableMemory(entry, stride)) continue;
                UClass* itemCls = *reinterpret_cast<UClass**>(entry + itemOff);
                int32_t itemID  = *reinterpret_cast<int32_t*>(entry + idOff);
                if (itemID == 0 || !itemCls || !isObjectAlive(itemCls)) continue;
                std::wstring cls;
                try { cls = itemCls->GetName(); } catch (...) { continue; }
                if (cls.find(classNameSubstr) != std::wstring::npos)
                {
                    VLOG(STR("[MoriaCppMod] [B3] player Items[{}] id={} class='{}' matches '{}'\n"),
                         i, itemID, cls.c_str(), classNameSubstr);
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
                    try { safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr); } catch (...) {}
                }
                m_test3SaddlebagWidget = nullptr;
            }

            UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
            if (!pawn) { showOnScreen(L"No player pawn", 2.0f, 0.9f, 0.4f, 0.4f); return; }

            // 1. Resolve player MorInventoryComponent
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            UObject* playerInv = nullptr;
            if (invCls)
            {
                if (auto* getCompFn = pawn->GetFunctionByNameInChain(STR("GetComponentByClass")))
                {
                    std::vector<uint8_t> gb(getCompFn->GetParmsSize(), 0);
                    writeGoatParm<UClass*>(getCompFn, gb.data(), STR("ComponentClass"), invCls);
                    try { safeProcessEvent(pawn, getCompFn, gb.data()); } catch (...) {}
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
                showOnScreen(L"No saddlebag equipped - pick one up near the bell",
                             3.0f, 1.0f, 0.5f, 0.5f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [B3] using player saddlebag handle id={} owner=playerInv={:p}\n"),
                 bagID, (void*)playerInv);

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
                auto* pIn  = findParam(getByTagFn, STR("Tag"));
                if (!pIn) pIn = findParam(getByTagFn, STR("ContainerTag"));
                auto* pRet = findParam(getByTagFn, STR("ReturnValue"));
                int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
                int rOff   = pRet ? pRet->GetOffset_Internal() : 8;
                std::memcpy(tb.data() + tagOff, &bodyTagName, sizeof(RC::Unreal::FName));
                try { safeProcessEvent(playerInv, getByTagFn, tb.data()); } catch (...) {}
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
                if (auto* p = w->GetValuePtrByPropertyNameInChain<uint8_t>(propName))
                    std::memcpy(p, src, size);
            };
            auto writeObj = [&](const wchar_t* propName, UObject* val) {
                if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(propName))
                    *p = val;
            };
            auto writeBool = [&](const wchar_t* propName, bool val) {
                if (auto* p = w->GetValuePtrByPropertyNameInChain<bool>(propName))
                    *p = val;
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

            VLOG(STR("[MoriaCppMod] [B3] widget bound: bagID={} playerInv={:p} (NPC mode OFF, storage view ON)\n"),
                 bagID, (void*)playerInv);

            // 9. Fire HandleStorageView(false) — non-NPC mode
            if (auto* hsvFn = w->GetFunctionByNameInChain(STR("HandleStorageView")))
            {
                int hsvSz = hsvFn->GetParmsSize();
                std::vector<uint8_t> hsvBuf(hsvSz, 0);
                hsvBuf[0] = 0;  // NPCMode = false
                if (auto* pn = findParam(hsvFn, STR("NPCMode"))) hsvBuf[pn->GetOffset_Internal()] = 0;
                try { safeProcessEvent(w, hsvFn, hsvBuf.data()); } catch (...) {}
            }

            // 10. AddToViewport + cache for ESC dismiss
            m_test3SaddlebagWidget = w;  // reuse Test 3's ESC dismiss cache
            if (auto* addFn = w->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                int addSz = addFn->GetParmsSize();
                std::vector<uint8_t> addBuf(addSz, 0);
                if (addSz >= 4) *reinterpret_cast<int32_t*>(addBuf.data()) = 100;
                try { safeProcessEvent(w, addFn, addBuf.data()); } catch (...) {}
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

        std::string saddlebagHandlePath(UObject* goat)
        {
            (void)goat;  // (sidecar removed; B2 dead code, self-contained path)
            std::string p = modPath("Mods/MoriaCppMod/goat-saddlebag.txt");
            return p.substr(0, p.size() - 4) + ".handle";  // .txt -> .handle
        }

        // Hide + no-collision + attach to the goat (snap location/rotation).
        void tameSaddlebagActor(UObject* bag, UObject* goat)
        {
            if (!bag || !isObjectAlive(bag)) return;
            if (auto* hideFn = bag->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
            { struct { bool b{true}; } p{}; try { safeProcessEvent(bag, hideFn, &p); } catch (...) {} }
            if (auto* collFn = bag->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
            { struct { bool b{false}; } p{}; try { safeProcessEvent(bag, collFn, &p); } catch (...) {} }
            if (goat && isObjectAlive(goat))
            {
                if (auto* att = bag->GetFunctionByNameInChain(STR("K2_AttachToActor")))
                {
                    std::vector<uint8_t> ab(att->GetParmsSize(), 0);
                    if (auto* pP = findParam(att, STR("ParentActor")))
                        *reinterpret_cast<UObject**>(ab.data() + pP->GetOffset_Internal()) = goat;
                    if (auto* pL = findParam(att, STR("LocationRule")))  ab[pL->GetOffset_Internal()] = 2;  // SnapToTarget
                    if (auto* pR = findParam(att, STR("RotationRule")))  ab[pR->GetOffset_Internal()] = 2;
                    if (auto* pS = findParam(att, STR("ScaleRule")))     ab[pS->GetOffset_Internal()] = 1;  // KeepWorld
                    try { safeProcessEvent(bag, att, ab.data()); } catch (...) {}
                }
            }
        }

        // Register the bag actor with the save system + persist the record
        // handle bytes (hex) for GetRuntimeActorFromHandle after reload.
        void registerSaddlebagActor(UObject* bag, UObject* goat)
        {
            UObject* ws = nullptr;
            {
                std::vector<UObject*> cands;
                if (findAllOfSafe(STR("MorSaveSystemWorldState"), cands))
                    for (UObject* o : cands)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring cn = safeClassName(o);
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                        ws = o; break;
                    }
            }
            if (!ws) { VLOG(STR("[MoriaCppMod] [BagActor rc.123] WorldState not found — register skipped\n")); return; }
            auto* storeFn = ws->GetFunctionByNameInChain(STR("StoreRuntimeActor"));
            if (!storeFn) { VLOG(STR("[MoriaCppMod] [BagActor rc.123] StoreRuntimeActor missing\n")); return; }
            std::vector<uint8_t> parms(storeFn->GetParmsSize(), 0);
            auto* pActor  = findParam(storeFn, STR("Actor"));
            auto* pHandle = findParam(storeFn, STR("InOutRuntimeActorHandle"));
            auto* pStab   = findParam(storeFn, STR("bStoreStability"));
            auto* pRet    = findParam(storeFn, STR("ReturnValue"));
            if (!pActor || !pHandle || !pRet) return;
            *reinterpret_cast<UObject**>(parms.data() + pActor->GetOffset_Internal()) = bag;
            if (pStab) *reinterpret_cast<bool*>(parms.data() + pStab->GetOffset_Internal()) = false;
            try { safeProcessEvent(ws, storeFn, parms.data()); } catch (...) { return; }
            bool ok = *reinterpret_cast<bool*>(parms.data() + pRet->GetOffset_Internal());
            int hs = (int)pHandle->GetSize();
            uint8_t* h = parms.data() + pHandle->GetOffset_Internal();
            // persist handle hex
            std::string hex;
            char b2[4];
            for (int i = 0; i < hs; i++) { snprintf(b2, 4, "%02X", h[i]); hex += b2; }
            std::ofstream f = openOutputFile(saddlebagHandlePath(goat), std::ios::trunc | std::ios::binary);
            if (f.is_open()) { f << hex << "\n"; f.close(); }
            VLOG(STR("[MoriaCppMod] [BagActor rc.123] StoreRuntimeActor ret={} handleBytes={} persisted\n"), ok, hs);
        }

        // Try to fetch the save-system-restored bag actor via the persisted handle.
        UObject* fetchRestoredSaddlebagActor(UObject* goat)
        {
            std::ifstream f(utf8PathToWide(saddlebagHandlePath(goat)));
            if (!f.is_open()) return nullptr;
            std::string hex;
            std::getline(f, hex);
            while (!hex.empty() && (hex.back()=='\r' || hex.back()=='\n')) hex.pop_back();
            if (hex.size() < 2 || (hex.size() & 1)) return nullptr;
            std::vector<uint8_t> hb(hex.size() / 2, 0);
            for (size_t i = 0; i < hb.size(); i++)
                hb[i] = (uint8_t)strtoul(hex.substr(i*2, 2).c_str(), nullptr, 16);
            UObject* ws = nullptr;
            {
                std::vector<UObject*> cands;
                if (findAllOfSafe(STR("MorSaveSystemWorldState"), cands))
                    for (UObject* o : cands)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring cn = safeClassName(o);
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                        ws = o; break;
                    }
            }
            if (!ws) return nullptr;
            auto* getFn = ws->GetFunctionByNameInChain(STR("GetRuntimeActorFromHandle"));
            if (!getFn) return nullptr;
            std::vector<uint8_t> parms(getFn->GetParmsSize(), 0);
            auto* pH   = findParam(getFn, STR("ActorHandle"));
            auto* pOk  = findParam(getFn, STR("bActorIsValid"));
            auto* pRet = findParam(getFn, STR("ReturnValue"));
            if (!pH || !pRet) return nullptr;
            int copyN = (int)pH->GetSize() < (int)hb.size() ? (int)pH->GetSize() : (int)hb.size();
            std::memcpy(parms.data() + pH->GetOffset_Internal(), hb.data(), copyN);
            try { safeProcessEvent(ws, getFn, parms.data()); } catch (...) { return nullptr; }
            bool valid = pOk ? *reinterpret_cast<bool*>(parms.data() + pOk->GetOffset_Internal()) : false;
            UObject* actor = *reinterpret_cast<UObject**>(parms.data() + pRet->GetOffset_Internal());
            VLOG(STR("[MoriaCppMod] [BagActor rc.123] GetRuntimeActorFromHandle valid={} actor={:p}\n"),
                 valid, (void*)actor);
            return (valid && actor && isObjectAlive(actor)) ? actor : nullptr;
        }

        // Find-or-restore-or-spawn the saddlebag ACTOR. Returns null only if
        // spawn is impossible. `allowSpawn` gates fresh creation on the craft check.
        UObject* ensureSaddlebagActor(UObject* goat, bool allowSpawn)
        {
            if (m_saddlebagActor && isObjectAlive(m_saddlebagActor)
                && !safeObjectName(m_saddlebagActor).empty())
                return m_saddlebagActor;
            m_saddlebagActor = nullptr;

            // 1. Any live bag actor in the world (save-system restore or earlier spawn)?
            {
                std::vector<UObject*> bags;
                findAllOfSafe(STR("BP_SaddleBags_Goat_C"), bags);
                for (auto* b : bags)
                {
                    if (!b || !isObjectAlive(b)) continue;
                    std::wstring nm = safeObjectName(b);
                    if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                    m_saddlebagActor = b;
                    VLOG(STR("[MoriaCppMod] [BagActor rc.123] found live bag actor {:p} '{}' in world — adopting\n"),
                         (void*)b, nm.c_str());
                    break;
                }
            }
            // 2. Ask the save system by persisted handle.
            if (!m_saddlebagActor)
                m_saddlebagActor = fetchRestoredSaddlebagActor(goat);

            // 3. Spawn fresh (craft-gated by caller).
            if (!m_saddlebagActor && allowSpawn)
            {
                if (!ensureGoatSpawnBindings()) return nullptr;
                const wchar_t* kBagPath = STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
                UClass* bagCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kBagPath);
                if (!bagCls) bagCls = goat_loadClassAssetBlocking(kBagPath);
                if (!bagCls || safeObjectName(bagCls).empty()) return nullptr;

                FVec3f loc{};
                if (auto* gloc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
                { struct { FVec3f Ret{}; } lp{}; if (safeProcessEvent(goat, gloc, &lp)) loc = lp.Ret; }
                FTransformRaw xform{};
                xform.Rotation = {0,0,0,1};
                xform.Translation = { loc.X, loc.Y, loc.Z + 60.0f };
                xform.Scale3D = {1,1,1};

                std::vector<uint8_t> buf(m_goatBeginSpawnFn->GetParmsSize(), 0);
                writeGoatParm<UObject*>     (m_goatBeginSpawnFn, buf.data(), STR("WorldContextObject"), goat);
                writeGoatParm<UClass*>      (m_goatBeginSpawnFn, buf.data(), STR("ActorClass"),         bagCls);
                writeGoatParm<FTransformRaw>(m_goatBeginSpawnFn, buf.data(), STR("SpawnTransform"),     xform);
                writeGoatParm<uint8_t>      (m_goatBeginSpawnFn, buf.data(), STR("CollisionHandlingOverride"), 1);
                if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) return nullptr;
                UObject* bag = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
                if (!bag) { VLOG(STR("[MoriaCppMod] [BagActor rc.123] BeginDeferred null\n")); return nullptr; }
                std::vector<uint8_t> buf2(m_goatFinishSpawnFn->GetParmsSize(), 0);
                writeGoatParm<UObject*>     (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          bag);
                writeGoatParm<FTransformRaw>(m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
                safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatFinishSpawnFn, buf2.data());
                m_saddlebagActor = bag;
                VLOG(STR("[MoriaCppMod] [BagActor rc.123] SPAWNED fresh bag actor {:p}\n"), (void*)bag);
                registerSaddlebagActor(bag, goat);
            }

            if (m_saddlebagActor)
            {
                tameSaddlebagActor(m_saddlebagActor, goat);
                // DISCOVERY: what does this actor carry?
                UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Script/Moria.MorInventoryComponent"));
                UObject* bagInv = nullptr;
                if (invCls)
                {
                    if (auto* gc = m_saddlebagActor->GetFunctionByNameInChain(STR("GetComponentByClass")))
                    {
                        std::vector<uint8_t> b(gc->GetParmsSize(), 0);
                        writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), invCls);
                        if (safeProcessEvent(m_saddlebagActor, gc, b.data()))
                            bagInv = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
                    }
                }
                int32_t cCnt = -1;
                if (bagInv)
                {
                    if (auto* gcf = bagInv->GetFunctionByNameInChain(STR("GetContainers")))
                    {
                        std::vector<uint8_t> gb(gcf->GetParmsSize(), 0);
                        try { safeProcessEvent(bagInv, gcf, gb.data()); } catch (...) {}
                        if (auto* pr = findParam(gcf, STR("ReturnValue")))
                            cCnt = *reinterpret_cast<int32_t*>(gb.data() + pr->GetOffset_Internal() + 8);
                    }
                }
                VLOG(STR("[MoriaCppMod] [BagActor rc.123] bag actor {:p}: MorInventoryComponent={:p} containers={}\n"),
                     (void*)m_saddlebagActor, (void*)bagInv, cCnt);
            }
            return m_saddlebagActor;
        }

        // [rc.123 B2] Open the storage UI bound to the bag ACTOR's own
        // inventory container. Returns false if the actor has no container
        // (caller falls back to the legacy AddItem fit + sidecar).
        bool openViaBagActor(UObject* goat, UObject* bagActor)
        {
            if (!bagActor || !isObjectAlive(bagActor)) return false;
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            if (!invCls) return false;
            UObject* bagInv = nullptr;
            if (auto* gc = bagActor->GetFunctionByNameInChain(STR("GetComponentByClass")))
            {
                std::vector<uint8_t> b(gc->GetParmsSize(), 0);
                writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), invCls);
                if (safeProcessEvent(bagActor, gc, b.data()))
                    bagInv = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
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
                try { safeProcessEvent(bagInv, gcf, gb.data()); } catch (...) {}
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
            m_storageSuppressAtMs = GetTickCount64() + 1500;  // [rc.130] suppression WINDOW (sweeps every 150ms)
            m_storageHarvestAtMs  = GetTickCount64() + 1200;
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
            UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));
            for (const wchar_t* dropCls : { STR("BP_DropItem_C"), STR("MorDroppedItem") })
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
                            VLOG(STR("[MoriaCppMod] [Carrier rc.126] found saddlebag carrier {:p} '{}'\n"),
                                 (void*)d, nm.c_str());
                            return d;
                        }
                    }
                }
            }
            return nullptr;
        }

        // Drop the player's CRAFTED pack via the game's native drop flow —
        // spawns a self-registered MorDroppedItem containing THAT item.
        bool dropCraftedPackFromPlayer(UObject* playerInv, UClass* saddleCls)
        {
            if (!playerInv || !saddleCls) return false;
            // locate the crafted pack's item ID in the player's Items array
            int32_t itemId = 0;
            if (auto* itemsProp = playerInv->GetPropertyByNameInChain(STR("Items")))
            {
                uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv)
                                  + itemsProp->GetOffset_Internal() + iiaListOff();
                if (isReadableMemory(listBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                    int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
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
                VLOG(STR("[MoriaCppMod] [Carrier rc.126] crafted pack ID not found in player inv\n"));
                return false;
            }
            // build the 20B FItemHandle {ID, Payload, OwnerWP}
            uint8_t handle[20] = {0};
            *reinterpret_cast<int32_t*>(handle) = itemId;
            RC::Unreal::FWeakObjectPtr wp(playerInv);
            std::memcpy(handle + 8, &wp, sizeof(wp));

            for (const wchar_t* fnName : { STR("ServerDropItem"), STR("DropItem") })
            {
                auto* df = playerInv->GetFunctionByNameInChain(fnName);
                if (!df) continue;
                std::vector<uint8_t> b(df->GetParmsSize(), 0);
                // log the real param list once, then fill by name
                for (auto* p : df->ForEachProperty())
                {
                    if (!p) continue;
                    std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                    std::wstring pt; try { pt = p->GetClass().GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [Carrier rc.126] {} param '{}' type={} off={} size={}\n"),
                         fnName, pn.c_str(), pt.c_str(), p->GetOffset_Internal(), p->GetSize());
                }
                auto* pItem  = findParam(df, STR("Item"));
                auto* pCount = findParam(df, STR("Count"));
                if (!pItem || (int)pItem->GetSize() < 20) continue;  // want the handle-taking overload
                std::memcpy(b.data() + pItem->GetOffset_Internal(), handle, 20);
                if (pCount) *reinterpret_cast<int32_t*>(b.data() + pCount->GetOffset_Internal()) = 1;
                try { safeProcessEvent(playerInv, df, b.data()); } catch (...) { continue; }
                VLOG(STR("[MoriaCppMod] [Carrier rc.126] {} fired for crafted pack id={}\n"), fnName, itemId);
                return true;
            }
            VLOG(STR("[MoriaCppMod] [Carrier rc.126] no usable drop fn on player inv\n"));
            return false;
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
                uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv)
                                  + itemsProp->GetOffset_Internal() + iiaListOff();
                if (isReadableMemory(listBase, 16))
                {
                    uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                    int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                    int stride = iiSize(), itemOff = iiItemOff(), idOff = iiIDOff();
                    for (int32_t i = 0; arrData && i < arrNum && i < 2000; i++)
                    {
                        uint8_t* entry = arrData + i * stride;
                        if (!isReadableMemory(entry, stride)) continue;
                        if (*reinterpret_cast<UClass**>(entry + itemOff) == saddleCls)
                        { itemId = *reinterpret_cast<int32_t*>(entry + idOff); break; }
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

            for (const wchar_t* fnName : { STR("ServerEjectActorDirection"), STR("EjectActorDirection"),
                                           STR("ServerEjectActor") })
            {
                auto* ef = playerInv->GetFunctionByNameInChain(fnName);
                if (!ef) continue;
                std::vector<uint8_t> b(ef->GetParmsSize(), 0);
                FProperty* pItem = nullptr;
                for (auto* p : ef->ForEachProperty())
                {
                    if (!p) continue;
                    std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                    std::wstring pt; try { pt = p->GetClass().GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} param '{}' type={} off={} size={}\n"),
                         fnName, pn.c_str(), pt.c_str(), p->GetOffset_Internal(), p->GetSize());
                    if (!pItem && p->GetSize() == 20 && pt == STR("StructProperty")) pItem = p;
                }
                if (!pItem) { VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} has no 20B handle param — skip\n"), fnName); continue; }
                std::memcpy(b.data() + pItem->GetOffset_Internal(), handle, 20);
                try { safeProcessEvent(playerInv, ef, b.data()); } catch (...) { continue; }
                VLOG(STR("[MoriaCppMod] [B5v2 rc.131] {} fired for crafted pack id={}\n"), fnName, itemId);
                return true;
            }
            VLOG(STR("[MoriaCppMod] [B5v2 rc.131] no eject fn available\n"));
            return false;
        }

        // [rc.132 B5 PROBE 2026-07-12] NUM8: drop the crafted pack via
        // ServerDropItem (the call the REAL equip-drop flow uses — capture-
        // proven) and scan deferred (+0.6s, +1.5s) for what spawned: the
        // BP_DropItem wrapper (B5 viable) vs a bare item actor (contents
        // question) vs both. Pack lands at the player's feet — recoverable.
        ULONGLONG m_b5ProbeScanAtMs{0};
        int m_b5ProbeScanPass{0};
        void probeB5DropCraftedPack()
        {
            const wchar_t* kSaddlePath =
                STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
            UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kSaddlePath);
            if (!saddleCls) saddleCls = goat_loadClassAssetBlocking(kSaddlePath);
            UObject* pawn = (m_localPawn && isObjectAlive(m_localPawn)) ? m_localPawn : getPawn();
            UObject* playerInv = pawn ? findPlayerInventoryComponent(pawn) : nullptr;
            if (!playerInv || !saddleCls)
            {
                showOnScreen(L"B5 probe: player inv / class missing", 2.0f, 0.9f, 0.4f, 0.4f);
                return;
            }
            if (dropCraftedPackFromPlayer(playerInv, saddleCls))
            {
                m_b5ProbeScanAtMs = GetTickCount64() + 600;
                m_b5ProbeScanPass = 0;
                showOnScreen(L"B5 probe: pack dropped - scanning...", 2.0f, 0.4f, 0.9f, 0.9f);
            }
            else showOnScreen(L"B5 probe: no crafted pack in inventory", 2.5f, 1.0f, 0.7f, 0.3f);
        }
        void tickB5ProbeScan()
        {
            if (m_b5ProbeScanAtMs == 0 || GetTickCount64() < m_b5ProbeScanAtMs) return;
            m_b5ProbeScanPass++;
            UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));
            int wrappers = 0, raws = 0;
            {
                std::vector<UObject*> drops;
                findAllOfSafe(STR("BP_DropItem_C"), drops);
                for (auto* d : drops)
                {
                    if (!d || !isObjectAlive(d)) continue;
                    std::wstring nm = safeObjectName(d);
                    if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                    UClass** p = d->GetValuePtrByPropertyNameInChain<UClass*>(STR("DroppedItem"));
                    if (p && saddleCls && *p == saddleCls)
                    {
                        wrappers++;
                        VLOG(STR("[MoriaCppMod] [B5probe rc.132] pass{} WRAPPER {:p} '{}' carries the pack\n"),
                             m_b5ProbeScanPass, (void*)d, nm.c_str());
                    }
                }
            }
            {
                std::vector<UObject*> bags;
                findAllOfSafe(STR("BP_SaddleBags_Goat_C"), bags);
                for (auto* b : bags)
                {
                    if (!b || !isObjectAlive(b)) continue;
                    std::wstring nm = safeObjectName(b);
                    if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                    raws++;
                    VLOG(STR("[MoriaCppMod] [B5probe rc.132] pass{} RAW actor {:p} '{}'\n"),
                         m_b5ProbeScanPass, (void*)b, nm.c_str());
                }
            }
            VLOG(STR("[MoriaCppMod] [B5probe rc.132] pass{} RESULT: wrappers={} rawActors={}\n"),
                 m_b5ProbeScanPass, wrappers, raws);
            if (m_b5ProbeScanPass == 1)
                m_b5ProbeScanAtMs = GetTickCount64() + 900;  // second pass at ~1.5s total
            else
            {
                m_b5ProbeScanAtMs = 0;
                showOnScreen(wrappers > 0 ? L"B5 probe: WRAPPER found - B5 viable!"
                                          : L"B5 probe: no wrapper (raw only) - see log",
                             3.0f, 0.4f, 0.9f, 0.4f);
            }
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
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            if (!invCls) return nullptr;
            if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
            {
                std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                if (auto* pCls = findParam(gf, STR("ComponentClass")))
                    *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                try { safeProcessEvent(goat, gf, b.data()); } catch (...) {}
                if (auto* pRet = findParam(gf, STR("ReturnValue")))
                {
                    uint8_t* arr = b.data() + pRet->GetOffset_Internal();
                    UObject** data = *reinterpret_cast<UObject***>(arr);
                    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                    for (int32_t i = 0; data && i < num && i < 16; i++)
                    {
                        UObject* c = data[i];
                        if (!c || !isObjectAlive(c)) continue;
                        std::wstring nm; try { nm = c->GetName(); } catch (...) {}
                        if (nm == STR("Inventory Comp")) return c;  // SCS cargo comp
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
            uint8_t* listBase = reinterpret_cast<uint8_t*>(inv)
                              + itemsProp->GetOffset_Internal() + iiaListOff();
            if (!isReadableMemory(listBase, 16)) return 0;
            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
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
                if (cs > 0) return id;     // the loaded/containered pack wins
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
            UClass* saddleCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));
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
                try { safeProcessEvent(toInv, gcf, gb.data()); } catch (...) {}
                if (auto* pr = findParam(gcf, STR("ReturnValue")))
                {
                    uint8_t* arr = gb.data() + pr->GetOffset_Internal();
                    uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
                    int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                    if (data && num > 0)
                        *reinterpret_cast<int32_t*>(destH) = *reinterpret_cast<int32_t*>(data);
                }
            }
            for (const wchar_t* fnName : { STR("ServerMoveItem"), STR("MoveItem") })
            {
                auto* mf = fromInv->GetFunctionByNameInChain(fnName);
                if (!mf) continue;
                FProperty* pItem = nullptr; FProperty* pDest = nullptr; FProperty* pAdd = nullptr;
                for (auto* p : mf->ForEachProperty())
                {
                    if (!p) continue;
                    std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                    if (pn == STR("Item"))        pItem = p;
                    if (pn == STR("Destination")) pDest = p;
                    if (pn == STR("AddType"))     pAdd  = p;
                }
                if (!pItem || !pDest) { VLOG(STR("[MoriaCppMod] [B7 rc.133] {}: Item/Destination params missing — skip\n"), fnName); continue; }
                // [rc.134] AddType enum decides placement behavior — zero didn't
                // create a container on a bare inventory. Try each value until
                // the pack actually ARRIVES (rescan-verified).
                for (uint8_t addType = 0; addType <= 3; addType++)
                {
                    std::vector<uint8_t> b(mf->GetParmsSize(), 0);
                    std::memcpy(b.data() + pItem->GetOffset_Internal(), itemH, 20);
                    std::memcpy(b.data() + pDest->GetOffset_Internal(), destH, 20);
                    if (pAdd) b[pAdd->GetOffset_Internal()] = addType;
                    try { safeProcessEvent(fromInv, mf, b.data()); } catch (...) { break; }
                    bool arrived = findPackId(toInv, saddleCls) != 0;
                    bool left    = findPackId(fromInv, saddleCls) == 0;
                    VLOG(STR("[MoriaCppMod] [B7 rc.134] {} via {} AddType={}: arrived={} leftSource={}\n"),
                         tag, fnName, (int)addType, arrived, left);
                    if (arrived) return true;
                    if (!pAdd) break;  // no enum to vary
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
                try { safeProcessEvent(goatInv, gc, gb.data()); } catch (...) {}
                auto* pr = findParam(gc, STR("ReturnValue"));
                if (!pr) return -1;
                return *reinterpret_cast<int32_t*>(gb.data() + pr->GetOffset_Internal() + 8);
            };
            if (contCountL() > 0) return true;
            const wchar_t* kSlotPath =
                STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C");
            UClass* slotCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, kSlotPath);
            if (!slotCls) slotCls = goat_loadClassAssetBlocking(kSlotPath);
            if (!slotCls) { VLOG(STR("[MoriaCppMod] [B7 rc.135] slot-container class missing\n")); return false; }
            auto* af = goatInv->GetFunctionByNameInChain(STR("AddItem"));
            if (!af) return false;
            auto* pItem  = findParam(af, STR("Item")); if (!pItem) pItem = findParam(af, STR("Class"));
            auto* pCount = findParam(af, STR("Count"));
            auto* pMeth  = findParam(af, STR("Method"));
            for (uint8_t m = 0; m <= 3; m++)
            {
                std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                if (pItem)  *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = slotCls;
                if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                if (pMeth)  ab[pMeth->GetOffset_Internal()] = m;
                try { safeProcessEvent(goatInv, af, ab.data()); } catch (...) { break; }
                int32_t c = contCountL();
                VLOG(STR("[MoriaCppMod] [B7 rc.135] AddItem(SLOT container) Method={} -> containers={}\n"), (int)m, c);
                if (c > 0) return true;
                if (!pMeth) break;
            }
            return contCountL() > 0;
        }

        ULONGLONG m_b7MoveBackAtMs{0};
        void b7SaveMomentStash(UObject* goat)
        {
            UObject* gInv = goatInvOf(goat);
            UObject* pInv = playerInvOf();
            if (!gInv || !pInv) return;
            if (movePackBetween(gInv, pInv, STR("stash goat->player (save)")))
                m_b7MoveBackAtMs = GetTickCount64() + 4000;  // return after save writes
        }
        void tickB7MoveBack()
        {
            if (m_b7MoveBackAtMs == 0 || GetTickCount64() < m_b7MoveBackAtMs) return;
            m_b7MoveBackAtMs = 0;
            UObject* goat = nullptr;
            for (auto& g : m_followGoats)
            {
                UObject* p = g.pawn.Get();
                if (p && isObjectAlive(p)) { goat = p; break; }
            }
            if (!goat) { VLOG(STR("[MoriaCppMod] [B7 rc.133] move-back: no live goat — pack stays with player\n")); return; }
            UObject* gInv = goatInvOf(goat);
            UObject* pInv = playerInvOf();
            if (gInv && pInv)
                movePackBetween(pInv, gInv, STR("return player->goat (post-save)"));
        }

        // [rc.126] Duplicate-goat cleanup: park-not-destroy + per-load orphan
        // spawns multiplied goats (census: 3 live!). Keep ONE (prefer non-zero
        // NpcGuid), destroy the rest. Their saddlebag carriers persist as
        // dropped items and re-attach to the kept goat.
        void dedupeGoats()
        {
            std::vector<UObject*> goats;
            findAllOfSafe(STR("BP_NpcGoat_C"), goats);
            std::vector<UObject*> live;
            for (auto* g : goats)
            {
                if (!g || !isObjectAlive(g)) continue;
                std::wstring nm = safeObjectName(g);
                if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                live.push_back(g);
            }
            if (live.size() <= 1) return;
            // prefer the goat with a non-zero NpcGuid (the identified one)
            auto guidNonZero = [&](UObject* g) -> bool {
                UClass* npcCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Script/Moria.MorNPCComponent"));
                if (!npcCls) return false;
                auto* gc = g->GetFunctionByNameInChain(STR("GetComponentByClass"));
                if (!gc) return false;
                std::vector<uint8_t> b(gc->GetParmsSize(), 0);
                writeGoatParm<UClass*>(gc, b.data(), STR("ComponentClass"), npcCls);
                if (!safeProcessEvent(g, gc, b.data())) return false;
                UObject* comp = readGoatParm<UObject*>(gc, b.data(), STR("ReturnValue"), nullptr);
                if (!comp) return false;
                uint8_t* gp = comp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                if (!gp) return false;
                for (int i = 0; i < 16; i++) if (gp[i]) return true;
                return false;
            };
            UObject* keep = nullptr;
            for (auto* g : live) if (guidNonZero(g)) { keep = g; break; }
            if (!keep) keep = live[0];
            int destroyed = 0;
            for (auto* g : live)
            {
                if (g == keep) continue;
                if (destroyGoat(g)) destroyed++;
            }
            // retrack
            m_followGoats.clear();
            adoptExistingGoat(keep);
            VLOG(STR("[MoriaCppMod] [Dedupe rc.126] kept goat {:p}, destroyed {} duplicate(s)\n"),
                 (void*)keep, destroyed);
        }

        // [rc.124 2026-07-11] Destroy stray raw BP_SaddleBags_Goat_C world
        // actors (rc.123 litter — inert props the save system faithfully
        // restores as visible floaters), unregister their save record, and
        // remove the persisted handle file. Legit saddlebag items live in
        // inventories, never as raw world actors, so destroying is safe.
        void cleanupStraySaddlebagActors(UObject* goat)
        {
            m_saddlebagActor = nullptr;
            int destroyed = 0;
            std::vector<UObject*> bags;
            findAllOfSafe(STR("BP_SaddleBags_Goat_C"), bags);
            for (auto* b : bags)
            {
                if (!b || !isObjectAlive(b)) continue;
                std::wstring nm = safeObjectName(b);
                if (nm.empty() || nm.rfind(STR("Default__"), 0) == 0) continue;
                if (auto* d = b->GetFunctionByNameInChain(STR("K2_DestroyActor")))
                {
                    std::vector<uint8_t> db(d->GetParmsSize() > 0 ? d->GetParmsSize() : 1, 0);
                    try { safeProcessEvent(b, d, db.data()); destroyed++; } catch (...) {}
                }
            }
            // Unregister the persisted record so the save stops restoring it.
            std::string hp = saddlebagHandlePath(goat);
            std::ifstream hf(utf8PathToWide(hp));
            if (hf.is_open())
            {
                std::string hex;
                std::getline(hf, hex);
                hf.close();
                while (!hex.empty() && (hex.back()=='\r' || hex.back()=='\n')) hex.pop_back();
                if (hex.size() >= 2 && !(hex.size() & 1))
                {
                    std::vector<uint8_t> hb(hex.size() / 2, 0);
                    for (size_t i = 0; i < hb.size(); i++)
                        hb[i] = (uint8_t)strtoul(hex.substr(i*2, 2).c_str(), nullptr, 16);
                    UObject* ws = nullptr;
                    std::vector<UObject*> cands;
                    if (findAllOfSafe(STR("MorSaveSystemWorldState"), cands))
                        for (UObject* o : cands)
                        {
                            if (!o || !isObjectAlive(o)) continue;
                            std::wstring cn = safeClassName(o);
                            if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                            ws = o; break;
                        }
                    if (ws)
                    {
                        if (auto* uf = ws->GetFunctionByNameInChain(STR("UnregisterRuntimeActorFromHandle")))
                        {
                            std::vector<uint8_t> ub(uf->GetParmsSize(), 0);
                            if (auto* pH = findParam(uf, STR("ActorHandle")))
                            {
                                int n = (int)pH->GetSize() < (int)hb.size() ? (int)pH->GetSize() : (int)hb.size();
                                std::memcpy(ub.data() + pH->GetOffset_Internal(), hb.data(), n);
                            }
                            if (auto* pW = findParam(uf, STR("bWasDestroyed")))
                                ub[pW->GetOffset_Internal()] = 1;
                            if (auto* pL = findParam(uf, STR("bUnregisterFromLevelRecord")))
                                ub[pL->GetOffset_Internal()] = 1;
                            try { safeProcessEvent(ws, uf, ub.data()); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [BagActor rc.124] UnregisterRuntimeActorFromHandle fired for stray record\n"));
                        }
                    }
                }
                _wremove(utf8PathToWide(hp).c_str());
            }
            if (destroyed > 0)
                VLOG(STR("[MoriaCppMod] [BagActor rc.124] cleanup: destroyed {} stray saddlebag actor(s)\n"), destroyed);
        }

        ULONGLONG m_sbWidgetOpenMs{0};       // [rc.130] widget-open time (Esc/Tab grace)
        // Sidecar (file-based contents backup) fully REMOVED 2026-07-12 per
        // user - persistence is native (pack in player inventory + NPC
        // registry). History: memory goat-final-architecture / tobi log.

        // [rc.109 2026-07-11] Cache for the one-shot delayed container re-drive
        // (protects the saddlebag grid against later NPC-path rebuilds).
        uint8_t   m_sbHandleCache[20]{};
        UObject*  m_sbGoatInvCache{nullptr};
        UObject*  m_sbPlayerInvCache{nullptr};
        ULONGLONG m_contReSetupAtMs{0};

        // [rc.109] Drive the screen's Storage_Container child down the chest
        // path: write its members (storageHandle + components + screen ref),
        // then call its own 'Set Up Storage Container' (which ClearChildren's
        // the pane and builds the grid widget from the handle).
        void driveSaddlebagStorageContainer(UObject* screen, UObject* goatInv, UObject* playerInv,
                                            const uint8_t bagHandle[20], const wchar_t* tag)
        {
            UObject* cont = jw_findChildInTree(screen, STR("WBP_UI_Inventory_Storage_Container"));
            if (!cont || !isObjectAlive(cont))
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] Storage_Container child NOT FOUND\n"), tag);
                return;
            }
            if (auto* p = cont->GetValuePtrByPropertyNameInChain<uint8_t>(STR("storageHandle")))
                std::memcpy(p, bagHandle, 20);
            if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("storageInventoryComponent")))
                *p = goatInv;
            if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent")))
                *p = playerInv;
            if (auto* p = cont->GetValuePtrByPropertyNameInChain<UObject*>(STR("StorageScreenRef")))
                *p = screen;
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
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.110] InteractableRef was {:p} -> NULLED (kills NPC-type classification)\n"),
                     (void*)prev);
            }
            else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.110] InteractableRef property NOT FOUND\n"));
            if (auto* f = cont->GetFunctionByNameInChain(STR("Set Up Storage Container")))
            {
                int psz = (int)f->GetParmsSize(); if (psz < 1) psz = 1;
                std::vector<uint8_t> b((size_t)psz, 0);
                try { safeProcessEvent(cont, f, b.data()); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] Storage_Container driven: handle+comps written, 'Set Up Storage Container' called (cont={:p})\n"),
                     tag, (void*)cont);
            }
            else VLOG(STR("[MoriaCppMod] [GoatSaddle] [{}] 'Set Up Storage Container' fn NOT FOUND\n"), tag);
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
                    try { safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr); } catch (...) {}
                }
                m_test3SaddlebagWidget = nullptr;
            }

            const wchar_t* widgetPath = STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C");
            UClass* widgetCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widgetPath);
            if (!widgetCls) widgetCls = goat_loadClassAssetBlocking(widgetPath);
            if (!widgetCls) { showOnScreen(L"StorageMode widget class missing", 2.5f, 0.9f, 0.4f, 0.4f); return; }

            UObject* w = jw_createGameWidget(widgetCls);
            if (!w || !isObjectAlive(w)) { showOnScreen(L"Widget create failed", 2.5f, 0.9f, 0.4f, 0.4f); return; }

            auto writeBytes = [&](const wchar_t* propName, const uint8_t* src, int size) {
                if (auto* p = w->GetValuePtrByPropertyNameInChain<uint8_t>(propName))
                    std::memcpy(p, src, size);
            };
            auto writeObj = [&](const wchar_t* propName, UObject* val) {
                if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(propName)) *p = val;
            };
            auto writeBool = [&](const wchar_t* propName, bool val) {
                if (auto* p = w->GetValuePtrByPropertyNameInChain<bool>(propName)) *p = val;
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
                    auto* pIn  = findParam(gbtFn, STR("Tag"));
                    if (!pIn) pIn = findParam(gbtFn, STR("ContainerTag"));
                    auto* pRet = findParam(gbtFn, STR("ReturnValue"));
                    int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
                    int rOff   = pRet ? pRet->GetOffset_Internal() : 8;
                    std::memcpy(tb.data() + tagOff, &bodyTag, sizeof(RC::Unreal::FName));
                    try { safeProcessEvent(playerInv, gbtFn, tb.data()); } catch (...) {}
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
            (void)goat;  // goat not bound in chest mode (kept in signature for logging/callers)

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
                    std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                    std::wstring pt; try { pt = p->GetClass().GetName(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106] HandleStorageView param '{}' type={} off={} size={}\n"),
                         pn.c_str(), pt.c_str(), p->GetOffset_Internal(), p->GetSize());
                    if (pt == STR("BoolProperty"))
                    {
                        bool val = false;
                        // NPC-ish → false; storage/view/show-ish → true
                        if (pn.find(STR("NPC")) != std::wstring::npos || pn.find(STR("Npc")) != std::wstring::npos)
                            val = false;
                        else if (pn.find(STR("torage")) != std::wstring::npos ||
                                 pn.find(STR("View"))   != std::wstring::npos ||
                                 pn.find(STR("Show"))   != std::wstring::npos ||
                                 pn.find(STR("Enable")) != std::wstring::npos)
                            val = true;
                        b[p->GetOffset_Internal()] = val ? 1 : 0;
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.106]   -> set '{}' = {}\n"), pn.c_str(), val);
                    }
                }
                try { safeProcessEvent(w, hsvFn, b.data()); } catch (...) {}
            }

            m_test3SaddlebagWidget = w;
            if (auto* addFn = w->GetFunctionByNameInChain(STR("AddToViewport")))
            {
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> b(sz, 0);
                if (sz >= 4) *reinterpret_cast<int32_t*>(b.data()) = 100;
                try { safeProcessEvent(w, addFn, b.data()); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.104] OUR chest-mode widget={:p} added to viewport (Z=100)\n"),
                     (void*)w);
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
                        try { safeProcessEvent(c, f, vb.data()); } catch (...) {}
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
            for (const wchar_t* evName : { STR("OnBeforeShow"), STR("OnAfterShow") })
            {
                if (auto* f = w->GetFunctionByNameInChain(evName))
                {
                    int psz = (int)f->GetParmsSize(); if (psz < 1) psz = 1;
                    std::vector<uint8_t> b((size_t)psz, 0);
                    try { safeProcessEvent(w, f, b.data()); } catch (...) {}
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
            m_sbGoatInvCache   = goatInv;
            m_sbPlayerInvCache = playerInv;
            m_contReSetupAtMs  = GetTickCount64() + 600;

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
                        vb[0] = 1;  // Collapsed
                        try { safeProcessEvent(c, f, vb.data()); } catch (...) {}
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
            m_sbWidgetOpenMs = GetTickCount64();  // [rc.130] Esc/Tab grace anchor
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
                        bool inViewport = false;
                        if (auto* ivFn = w->GetFunctionByNameInChain(STR("IsInViewport")))
                        {
                            std::vector<uint8_t> b(ivFn->GetParmsSize(), 0);
                            try { safeProcessEvent(w, ivFn, b.data()); } catch (...) {}
                            if (auto* pr = findParam(ivFn, STR("ReturnValue")))
                                inViewport = *reinterpret_cast<bool*>(b.data() + pr->GetOffset_Internal());
                        }
                        if (inViewport)
                        {
                            // [rc.137] COLLAPSE, don't RemoveFromParent — ripping the
                            // game's SINGLETON screen from the viewport broke every
                            // later chest open (Show() can't re-add it). Collapse is
                            // reversible: the native Show restores visibility.
                            if (auto* svFn = w->GetFunctionByNameInChain(STR("SetVisibility")))
                            {
                                std::vector<uint8_t> vb(svFn->GetParmsSize(), 0);
                                vb[0] = 1;  // Collapsed
                                try { safeProcessEvent(w, svFn, vb.data()); } catch (...) {}
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
                    bool ours = (w == m_test3SaddlebagWidget);
                    bool inViewport = false;
                    if (auto* ivFn = w->GetFunctionByNameInChain(STR("IsInViewport")))
                    {
                        std::vector<uint8_t> b(ivFn->GetParmsSize(), 0);
                        try { safeProcessEvent(w, ivFn, b.data()); } catch (...) {}
                        if (auto* pr = findParam(ivFn, STR("ReturnValue")))
                            inViewport = *reinterpret_cast<bool*>(b.data() + pr->GetOffset_Internal());
                    }
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.104 SUPPRESS] instance={:p} ours={} inViewport={}\n"),
                         (void*)w, ours, inViewport);
                    if (!ours && inViewport)
                    {
                        // [rc.137] collapse, don't remove (see sweep note — removal
                        // permanently broke chest opens this session)
                        if (auto* svFn = w->GetFunctionByNameInChain(STR("SetVisibility")))
                        {
                            std::vector<uint8_t> vb(svFn->GetParmsSize(), 0);
                            vb[0] = 1;  // Collapsed
                            try { safeProcessEvent(w, svFn, vb.data()); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.137 SUPPRESS] native StorageMode {:p} collapsed\n"),
                                 (void*)w);
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
                UObject* np = nullptr; bool fromNpc = false; UObject* ic = nullptr;
                if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC"))) np = *p;
                if (auto* p = w->GetValuePtrByPropertyNameInChain<bool>(STR("isOpenedFromNPC"))) fromNpc = *p;
                if (auto* p = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent"))) ic = *p;
                VLOG(STR("[MoriaCppMod] [UIDump rc.101] instance={:p} ours={} AssociatedNPC={:p} isOpenedFromNPC={} InventoryComponent={:p}\n"),
                     (void*)w, ours, (void*)np, fromNpc, (void*)ic);
                if (!screen) screen = w;
            }
            // Prefer OUR widget explicitly if alive.
            if (m_test3SaddlebagWidget && isObjectAlive(m_test3SaddlebagWidget))
                screen = m_test3SaddlebagWidget;
            VLOG(STR("[MoriaCppMod] [UIDump rc.101] live StorageMode instances={} using={:p} (ours={})\n"),
                 found.size(), (void*)screen, screen == m_test3SaddlebagWidget);
            if (!screen) return;

            // 1. Full-detail JSON for future recreation.
            harvestLiveWidgetToFile(screen, STR("StorageMode_GoatLive"));

            // 2. Compact tree in the log: class 'name' vis=<0..4> [+key bindings].
            //    Visibility enum: 0=Visible 1=Collapsed 2=Hidden 3=HitTestInvisible 4=SelfHitTestInvisible
            int lines = 0;
            std::function<void(UObject*, int)> walk = [&](UObject* w, int depth) {
                if (!w || !isObjectAlive(w) || lines > 2000 || depth > 16) return;
                std::wstring cls = safeClassName(w);
                std::wstring nm  = safeObjectName(w);
                // [rc.102] Skip the hidden NPC debug overlay subtree — its huge
                // tree ate the line budget before the walk reached the elements
                // we actually need (storageOverlay + epicPack+DetailsHBox).
                if (nm == STR("DebugNPCOverlay") || cls == STR("WBP_NPC_Debug_C"))
                {
                    VLOG(STR("[MoriaCppMod] [UIDump rc.101] {}{} '{}' (subtree SKIPPED)\n"),
                         std::wstring(depth * 2, L' ').c_str(), cls.c_str(), nm.c_str());
                    lines++;
                    return;
                }
                uint8_t vis = 255;
                if (auto* v = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"))) vis = *v;
                VLOG(STR("[MoriaCppMod] [UIDump rc.101] {}{} '{}' vis={}\n"),
                     std::wstring(depth * 2, L' ').c_str(), cls.c_str(), nm.c_str(), (int)vis);
                lines++;
                // Panel children
                if (auto* gcc = w->GetFunctionByNameInChain(STR("GetChildrenCount")))
                {
                    std::vector<uint8_t> b(gcc->GetParmsSize(), 0);
                    try { safeProcessEvent(w, gcc, b.data()); } catch (...) {}
                    int32_t n = 0;
                    if (auto* pr = findParam(gcc, STR("ReturnValue")))
                        n = *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal());
                    if (auto* gca = w->GetFunctionByNameInChain(STR("GetChildAt")))
                    {
                        for (int32_t i = 0; i < n && i < 64; i++)
                        {
                            std::vector<uint8_t> cb(gca->GetParmsSize(), 0);
                            if (auto* pi = findParam(gca, STR("Index")))
                                *reinterpret_cast<int32_t*>(cb.data() + pi->GetOffset_Internal()) = i;
                            try { safeProcessEvent(w, gca, cb.data()); } catch (...) {}
                            UObject* child = nullptr;
                            if (auto* pr2 = findParam(gca, STR("ReturnValue")))
                                child = *reinterpret_cast<UObject**>(cb.data() + pr2->GetOffset_Internal());
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
                VLOG(STR("[MoriaCppMod] [UIDump rc.101] screen.AssociatedNPC={:p} ({})\n"),
                     (void*)*np, *np ? safeClassName(*np).c_str() : STR("null"));
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
                for (auto& g : m_followGoats) {
                    UObject* p = g.pawn.Get();
                    if (p && isObjectAlive(p)) { goat = p; break; }
                }
                if (!goat) {
                    showOnScreen(L"No goat present", 2.0f, 0.9f, 0.4f, 0.4f);
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
                    UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                        STR("/Script/Moria.MorInventoryComponent"));
                    UObject* goatInv = nullptr;
                    auto readSH = [&](UObject* c) -> std::wstring {
                        if (!c) return STR("");
                        auto* p = c->GetPropertyByNameInChain(STR("StorageHandle"));
                        if (!p) return STR("");
                        auto* rn = reinterpret_cast<RC::Unreal::FName*>(reinterpret_cast<uint8_t*>(c) + p->GetOffset_Internal() + 8);
                        try { return rn->ToString(); } catch (...) { return STR(""); }
                    };
                    auto contCount = [&](UObject* c) -> int32_t {
                        auto* f = c ? c->GetFunctionByNameInChain(STR("GetContainers")) : nullptr;
                        if (!f) return -1;
                        std::vector<uint8_t> b(f->GetParmsSize(), 0);
                        try { safeProcessEvent(c, f, b.data()); } catch (...) {}
                        auto* pr = findParam(f, STR("ReturnValue"));
                        if (!pr) return -1;
                        return *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal() + 8);
                    };
                    if (invCls)
                    {
                        if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
                        {
                            std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                            if (auto* pCls = findParam(gf, STR("ComponentClass")))
                                *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                            try { safeProcessEvent(goat, gf, b.data()); } catch (...) {}
                            if (auto* pRet = findParam(gf, STR("ReturnValue"))) {
                                uint8_t* arr = b.data() + pRet->GetOffset_Internal();
                                UObject** data = *reinterpret_cast<UObject***>(arr);
                                int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                                UObject* firstAny = nullptr;
                                for (int32_t i = 0; data && i < num && i < 16; i++) {
                                    UObject* c = data[i];
                                    if (!c || !isObjectAlive(c)) continue;
                                    std::wstring nm; try { nm = c->GetName(); } catch (...) {}
                                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.99] comp[{}] name='{}' SH='{}' containers={}\n"),
                                         i, nm.c_str(), readSH(c).c_str(), contCount(c));
                                    if (!firstAny) firstAny = c;
                                    if (nm == STR("Inventory Comp")) goatInv = c;   // prefer the SCS cargo comp
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
                    if (!goatInv) {
                        showOnScreen(L"Goat has no inventory component", 2.5f, 0.9f, 0.4f, 0.4f);
                        return;
                    }
                    // [rc.128 2026-07-12] cleanupStraySaddlebagActors DISABLED —
                    // it was destroying LEGIT dropped packs: the game drops
                    // equippables as raw BP_SaddleBags_Goat_C actors, exactly
                    // what the cleanup deleted (it ate the user's crafted pack
                    // right after the B5 drop). rc.123's litter is long gone.
                    // cleanupStraySaddlebagActors(goat);

                    // [rc.126 B5] carrier-first find kept (wrapper carriers only).
                    if (UObject* carrier = findSaddlebagCarrier())
                    {
                        tameSaddlebagActor(carrier, goat);   // hide + attach
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
                        const wchar_t* kSaddlePathF =
                            STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
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
                                auto* pIn = findParam(gbt, STR("Tag")); if (!pIn) pIn = findParam(gbt, STR("ContainerTag"));
                                auto* pRet = findParam(gbt, STR("ReturnValue"));
                                RC::Unreal::FName t(STR("Goat_Saddlebags"), RC::Unreal::FNAME_Add);
                                if (pIn) std::memcpy(tb.data() + pIn->GetOffset_Internal(), &t, sizeof(t));
                                try { safeProcessEvent(pInvF, gbt, tb.data()); } catch (...) {}
                                if (pRet) cid = *reinterpret_cast<int32_t*>(tb.data() + pRet->GetOffset_Internal());
                            }
                            VLOG(STR("[MoriaCppMod] [rc.136] player pack id={} GetContainerByTag('Goat_Saddlebags') -> cid={}\n"),
                                 packIdF, cid);
                            if (cid == 0)
                            {
                                // diagnosis: list ALL player containers
                                if (auto* gcf = pInvF->GetFunctionByNameInChain(STR("GetContainers")))
                                {
                                    std::vector<uint8_t> gb(gcf->GetParmsSize(), 0);
                                    try { safeProcessEvent(pInvF, gcf, gb.data()); } catch (...) {}
                                    if (auto* pr = findParam(gcf, STR("ReturnValue")))
                                    {
                                        uint8_t* arr = gb.data() + pr->GetOffset_Internal();
                                        uint8_t* data = *reinterpret_cast<uint8_t**>(arr);
                                        int32_t num = *reinterpret_cast<int32_t*>(arr + 8);
                                        VLOG(STR("[MoriaCppMod] [rc.136] player inv has {} container(s):\n"), num);
                                        for (int32_t i = 0; data && i < num && i < 24; i++)
                                            VLOG(STR("[MoriaCppMod] [rc.136]   container[{}] id={}\n"),
                                                 i, *reinterpret_cast<int32_t*>(data + i * 20));
                                        // heuristic: the pack's container is the LAST one
                                        if (data && num > 0)
                                            cid = *reinterpret_cast<int32_t*>(data + (num - 1) * 20);
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
                                equipPorterSaddlebag(goat);  // visual saddle on the goat (rc.87, cosmetic)
                                VLOG(STR("[MoriaCppMod] [rc.136] opening PLAYER-side pack container id={} via goat menu\n"), cid);
                                openStorageWidgetForHandle(goat, pInvF, ph);
                                m_storageSuppressAtMs = GetTickCount64() + 1500;
                                m_storageHarvestAtMs  = GetTickCount64() + 1200;
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
                                    try { safeProcessEvent(b, f, ob.data()); } catch (...) {}
                                    if (auto* pr = findParam(f, STR("ReturnValue")))
                                        owner = *reinterpret_cast<UObject**>(ob.data() + pr->GetOffset_Internal());
                                }
                                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116 SCAN] live saddlebag actor '{}' {:p} owner={} \n"),
                                     nm.c_str(), (void*)b, owner ? safeClassName(owner).c_str() : STR("(none)"));
                                liveBags++;
                            }
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116 SCAN] {} live saddlebag actor(s) in world before fit\n"), liveBags);
                        }
                        const wchar_t* kSaddlePath =
                            STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C");
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
                                uint8_t* listBase = reinterpret_cast<uint8_t*>(playerInv)
                                                  + itemsProp->GetOffset_Internal() + iiaListOff();
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
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.103] saddleCls={:p} playerInv={:p} playerHas={}\n"),
                             (void*)saddleCls, (void*)playerInv, playerHas);
                        if (playerHas <= 0)
                        {
                            showOnScreen(L"Craft Goat Saddlebags first, then use Saddlebags again",
                                         3.5f, 1.0f, 0.7f, 0.3f);
                            // [rc.128] still suppress the vanilla NPC screen the
                            // E-release opens — otherwise the dwarf 4×3 appears
                            // and reads as "the saddlebags broke".
                            m_storageSuppressAtMs = GetTickCount64() + 1500;  // [rc.130] suppression WINDOW (sweeps every 150ms)
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
                                else VLOG(STR("[MoriaCppMod] [B5v2 rc.131] eject fired but NO wrapper carrier found — legacy fallback\n"));
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
                        for (const wchar_t* fnName : { STR("AddItem"), STR("RequestAddItem") })
                        {
                            auto* af = goatInv->GetFunctionByNameInChain(fnName);
                            if (!af) continue;
                            std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                            auto* pItem = findParam(af, STR("Item")); if (!pItem) pItem = findParam(af, STR("Class"));
                            auto* pCount = findParam(af, STR("Count"));
                            if (pItem)  *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = saddleCls;
                            if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                            try { safeProcessEvent(goatInv, af, ab.data()); } catch (...) {}
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
                                newest = b;  // FindAllOf order: last = most recently created
                            }
                            if (newest)
                            {
                                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116] registering fitted saddlebag actor {:p} '{}' with save system...\n"),
                                     (void*)newest, safeObjectName(newest).c_str());
                                storeGoatInWorldState(newest);
                            }
                            else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.116] NO saddlebag actor found after fit — container may be item-entry only (no actor); Channel-C not applicable, need alternate contents persistence\n"));
                        }

                        // DO NOT consume the crafted saddlebag — the pack item
                        // stays in the PLAYER inventory permanently; that IS
                        // the persistence (final architecture).
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.105] crafted saddlebag NOT consumed (persistence pending)\n"));
                        showOnScreen(L"Saddlebags fitted to the goat", 2.0f, 0.4f, 0.9f, 0.4f);
                        }  // [rc.133] end AddItem fallback (B7 move failed)
                    }
                    // Bind the first container's 20-byte handle and open CHEST mode.
                    uint8_t bagH[20] = {0};
                    int32_t bagId2 = 0;
                    if (auto* gcFn = goatInv->GetFunctionByNameInChain(STR("GetContainers")))
                    {
                        std::vector<uint8_t> gb(gcFn->GetParmsSize(), 0);
                        try { safeProcessEvent(goatInv, gcFn, gb.data()); } catch (...) {}
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
                    m_storageSuppressAtMs = GetTickCount64() + 1500;  // [rc.130] suppression WINDOW (sweeps every 150ms)
                    m_storageHarvestAtMs  = GetTickCount64() + 1200;
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
                    UClass* equipCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                        STR("/Script/Moria.MorEquipComponent"));
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] saddleCls={:p} equipCls={:p}\n"),
                         (void*)saddleCls, (void*)equipCls);
                    if (saddleCls && equipCls)
                    {
                        if (auto* getComp = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                        {
                            std::vector<uint8_t> gb(getComp->GetParmsSize(), 0);
                            writeGoatParm<UClass*>(getComp, gb.data(), STR("ComponentClass"), equipCls);
                            UObject* equipComp = nullptr;
                            if (safeProcessEvent(goat, getComp, gb.data()))
                                equipComp = readGoatParm<UObject*>(getComp, gb.data(), STR("ReturnValue"), nullptr);
                            if (equipComp && isObjectAlive(equipComp))
                            {
                                if (auto* sedFn = equipComp->GetFunctionByNameInChain(STR("ServerEquipDummyItem")))
                                {
                                    std::vector<uint8_t> eb(sedFn->GetParmsSize(), 0);
                                    auto* pIt = findParam(sedFn, STR("ItemToEquip"));
                                    if (!pIt) pIt = findParam(sedFn, STR("Item"));
                                    if (pIt) *reinterpret_cast<UClass**>(eb.data() + pIt->GetOffset_Internal()) = saddleCls;
                                    try { safeProcessEvent(equipComp, sedFn, eb.data()); } catch (...) {}
                                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] ServerEquipDummyItem(BP_SaddleBags_Goat) fired on EquipComp={:p}\n"),
                                         (void*)equipComp);
                                }
                                else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] ServerEquipDummyItem missing on EquipComp\n"));
                            }
                            else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.87] no MorEquipComponent on goat\n"));
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
                    UClass* gc = nullptr; try { gc = goat->GetClassPrivate(); } catch (...) {}
                    UFunction* mgFn = nullptr;
                    if (gc)
                    {
                        try {
                            for (auto* fn : gc->ForEachFunctionInChain())
                            {
                                if (!fn) continue;
                                std::wstring fn2; try { fn2 = fn->GetName(); } catch (...) { continue; }
                                if (fn2.find(STR("MorNpcOnManageLocalInteraction")) != std::wstring::npos)
                                { mgFn = fn; break; }
                            }
                        } catch (...) {}
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
                        std::wstring fn2; try { fn2 = mgFn->GetName(); } catch (...) {}
                        int psz = mgFn->GetParmsSize();
                        std::vector<uint8_t> pbuf(psz, 0);
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] firing native '{}' (parmSize={}) on goat={:p}\n"),
                             fn2.c_str(), psz, (void*)goat);
                        try { safeProcessEvent(goat, mgFn, pbuf.data()); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] native manage handler dispatched — watch for the storage screen\n"));
                        showOnScreen(L"Opening goat storage (native)", 1.5f, 0.4f, 0.9f, 0.5f);
                        return;  // native path owns the UI; do not run the container hunt
                    }
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.90] native manage SKIPPED (shows base 4x3); running container hunt for Goat.Slot.EpicPack\n"));
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.86] MorNpcOnManageLocalInteraction NOT found on goat — falling back to container path\n"));
                }

                UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Script/Moria.MorInventoryComponent"));
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
                    RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(
                        reinterpret_cast<uint8_t*>(comp) + shP->GetOffset_Internal() + 8);
                    try { return rn->ToString(); } catch (...) { return STR(""); }
                };
                if (invCls)
                {
                    // Try the array enumerator first (K2_GetComponentsByClass),
                    // else GetComponentsByClass, else fall back to single.
                    UObject* firstAny = nullptr;
                    for (const wchar_t* enumFn : { STR("K2_GetComponentsByClass"), STR("GetComponentsByClass") })
                    {
                        auto* gf = goat->GetFunctionByNameInChain(enumFn);
                        if (!gf) continue;
                        std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                        if (auto* pCls = findParam(gf, STR("ComponentClass")))
                            *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                        try { safeProcessEvent(goat, gf, b.data()); } catch (...) {}
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
                            std::wstring nm; try { nm = comp->GetName(); } catch (...) {}
                            std::wstring sh = readStorageRow(comp);
                            // [rc.90] Ground-truth per component: does it actually
                            // have a container now (Tobi v1.11.0 DefaultContainers)?
                            bool compHas = false; int compDefNum = -1;
                            if (auto* hcF = comp->GetFunctionByNameInChain(STR("HasContainers"))) {
                                std::vector<uint8_t> hb(hcF->GetParmsSize(), 0);
                                try { safeProcessEvent(comp, hcF, hb.data()); } catch (...) {}
                                if (auto* pr = findParam(hcF, STR("ReturnValue")))
                                    compHas = *reinterpret_cast<bool*>(hb.data() + pr->GetOffset_Internal());
                            }
                            if (auto* dcP = comp->GetPropertyByNameInChain(STR("DefaultContainers"))) {
                                uint8_t* a = reinterpret_cast<uint8_t*>(comp) + dcP->GetOffset_Internal();
                                if (isReadableMemory(a, 16)) compDefNum = *reinterpret_cast<int32_t*>(a + 8);
                            }
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.90]   comp[{}] name='{}' StorageHandle='{}' HasContainers={} DefaultContainers.Num={}\n"),
                                 i, nm.c_str(), sh.c_str(), compHas, compDefNum);
                            if (!firstAny) firstAny = comp;
                            // Prefer a component that ACTUALLY has containers and
                            // whose StorageHandle is the goat cargo/epic-pack slot.
                            if (compHas && (sh == STR("Goat.Slot.EpicPack") || sh == STR("Goat_Saddlebags"))) { goatInv = comp; }
                            else if (!goatInv && compHas) { goatInv = comp; }
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
                            if (safeProcessEvent(goat, getCompFn, b.data()))
                                goatInv = readGoatParm<UObject*>(getCompFn, b.data(), STR("ReturnValue"), nullptr);
                        }
                    }
                }
                if (!goatInv || !isObjectAlive(goatInv)) {
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.67] goat has no MorInventoryComponent\n"));
                    showOnScreen(L"Goat has no inventory component", 2.5f, 0.9f, 0.4f, 0.4f);
                    return;
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.76] chosen goatInv={:p} StorageHandle='{}'\n"),
                     (void*)goatInv, readStorageRow(goatInv).c_str());

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
                    try { safeProcessEvent(goatInv, hc, hb.data()); } catch (...) {}
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
                    try { safeProcessEvent(goatInv, gc, gb.data()); } catch (...) {}
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
                    for (const wchar_t* fnName : { STR("AddItem"), STR("RequestAddItem") })
                    {
                        auto* af = goatInv->GetFunctionByNameInChain(fnName);
                        if (!af) continue;
                        std::vector<uint8_t> ab(af->GetParmsSize(), 0);
                        auto* pItem = findParam(af, STR("Item"));
                        if (!pItem) pItem = findParam(af, STR("Class"));
                        auto* pCount = findParam(af, STR("Count"));
                        if (pItem)  *reinterpret_cast<UClass**>(ab.data() + pItem->GetOffset_Internal()) = cc;
                        if (pCount) *reinterpret_cast<int32_t*>(ab.data() + pCount->GetOffset_Internal()) = 1;
                        try { safeProcessEvent(goatInv, af, ab.data()); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] {}('{}') -> HasContainers={} count={}\n"),
                             fnName, clsPath, goatHasContainers(), goatContainerCount());
                        break;  // one add fn is enough
                    }
                };

                if (!hc0)
                {
                    // Step 1 — ResetToStarting (zero-arg; re-processes DefaultContainers)
                    if (auto* rs = goatInv->GetFunctionByNameInChain(STR("ResetToStarting")))
                    {
                        std::vector<uint8_t> rb(rs->GetParmsSize(), 0);
                        try { safeProcessEvent(goatInv, rs, rb.data()); } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] ResetToStarting() -> HasContainers={} count={}\n"),
                             goatHasContainers(), goatContainerCount());
                    }
                    else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] ResetToStarting NOT FOUND\n"));

                    // Step 2 — AddItem the epic-slot container class if still empty
                    if (!goatHasContainers())
                        goatAddItem(STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C"));

                    // Step 3 — AddItem the saddlebag pack (fills the epic slot → 8x8 cargo)
                    goatAddItem(STR("/Game/Mods/PorterGoat/Items/BP_SaddleBags_Goat.BP_SaddleBags_Goat_C"));

                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.92] AFTER injection: HasContainers={} count={}\n"),
                         goatHasContainers(), goatContainerCount());
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
                    UClass* gc2 = nullptr; try { gc2 = goat->GetClassPrivate(); } catch (...) {}
                    UFunction* mg2 = nullptr;
                    if (gc2)
                    {
                        try {
                            for (auto* fn : gc2->ForEachFunctionInChain())
                            {
                                if (!fn) continue;
                                std::wstring n; try { n = fn->GetName(); } catch (...) { continue; }
                                if (n.find(STR("MorNpcOnManageLocalInteraction")) != std::wstring::npos) { mg2 = fn; break; }
                            }
                        } catch (...) {}
                    }
                    if (mg2)
                    {
                        std::vector<uint8_t> pb(mg2->GetParmsSize(), 0);
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.93] container exists -> firing native manage '{}' (parm={})\n"),
                             mg2->GetName().c_str(), mg2->GetParmsSize());
                        try { safeProcessEvent(goat, mg2, pb.data()); } catch (...) {}
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
                    STR("Goat_Saddlebags"),        // 8x8 cargo (present once a pack is in the epic slot)
                    STR("Goat.Slot.EpicPack"),     // 1x1 epic-pack slot (Tobi v1.11.0 DefaultContainers)
                    STR("Inventory.BodyInventory"),
                    STR("BodyInventory"),
                    STR("Goat.BodyInventory"),
                    STR("Dwarf.BodyInventoryGOAT"), // our legacy name (last resort)
                };
                if (auto* getByTagFn = goatInv->GetFunctionByNameInChain(STR("GetContainerByTag")))
                {
                    auto* pIn  = findParam(getByTagFn, STR("Tag"));
                    if (!pIn) pIn = findParam(getByTagFn, STR("ContainerTag"));
                    auto* pRet = findParam(getByTagFn, STR("ReturnValue"));
                    int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
                    int rOff   = pRet ? pRet->GetOffset_Internal() : 8;
                    int sz = getByTagFn->GetParmsSize();
                    for (const wchar_t* cand : kCandidateTags)
                    {
                        std::vector<uint8_t> tb(sz, 0);
                        RC::Unreal::FName bagTag(cand, RC::Unreal::FNAME_Add);
                        std::memcpy(tb.data() + tagOff, &bagTag, sizeof(RC::Unreal::FName));
                        try { safeProcessEvent(goatInv, getByTagFn, tb.data()); } catch (...) {}
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
                        try { safeProcessEvent(goatInv, gcFn, gb.data()); } catch (...) {}
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
                                     id0, e0[0],e0[1],e0[2],e0[3], e0[8],e0[9],e0[10],e0[11]);
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
                    else VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.79] GetContainers missing on goat InvComp\n"));
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
                        UClass* icls = nullptr; try { icls = goatInv->GetClassPrivate(); } catch (...) {}
                        int total = 0, interesting = 0;
                        if (icls)
                        {
                            try {
                                for (auto* fn : icls->ForEachFunctionInChain())
                                {
                                    if (!fn) continue;
                                    std::wstring fnn; try { fnn = fn->GetName(); } catch (...) { continue; }
                                    total++;
                                    // Log ALL names compactly; expand params only for
                                    // names hinting at container creation/mutation.
                                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM] {} (parm={})\n"),
                                         fnn.c_str(), fn->GetParmsSize());
                                    bool hot =
                                        fnn.find(STR("Container")) != std::wstring::npos ||
                                        fnn.find(STR("Init"))      != std::wstring::npos ||
                                        fnn.find(STR("Create"))    != std::wstring::npos ||
                                        fnn.find(STR("Add"))       != std::wstring::npos ||
                                        fnn.find(STR("Give"))      != std::wstring::npos ||
                                        fnn.find(STR("Setup"))     != std::wstring::npos ||
                                        fnn.find(STR("Default"))   != std::wstring::npos ||
                                        fnn.find(STR("Spawn"))     != std::wstring::npos ||
                                        fnn.find(STR("Loadout"))   != std::wstring::npos ||
                                        fnn.find(STR("Register"))  != std::wstring::npos ||
                                        fnn.find(STR("Populate"))  != std::wstring::npos;
                                    if (hot)
                                    {
                                        interesting++;
                                        for (auto* p : fn->ForEachProperty())
                                        {
                                            if (!p) continue;
                                            std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                                            std::wstring pt; try { pt = p->GetClass().GetName(); } catch (...) {}
                                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM]      * param '{}' type={} off={} size={}\n"),
                                                 pn.c_str(), pt.c_str(), p->GetOffset_Internal(), p->GetSize());
                                        }
                                    }
                                }
                            } catch (...) {}
                        }
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.91 FNENUM] component class='{}' total functions={} hot={}\n"),
                             icls ? icls->GetName().c_str() : STR("(null)"), total, interesting);
                    }
                    if (auto* shProp = goatInv->GetPropertyByNameInChain(STR("StorageHandle")))
                    {
                        uint8_t* sh = reinterpret_cast<uint8_t*>(goatInv) + shProp->GetOffset_Internal();
                        // MorStorageRowHandle = { UDataTable* (8) ; FName RowName (@+8) }
                        RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(sh + 8);
                        try {
                            std::wstring rnStr = rn->ToString();
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [DISCOVERY] StorageHandle.RowName='{}'\n"), rnStr.c_str());
                        } catch (...) {}
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
                                    int32_t id  = *reinterpret_cast<int32_t*>(entry + idOff);
                                    int32_t slot= *reinterpret_cast<int32_t*>(entry + iiSlotOff());
                                    int32_t cs  = *reinterpret_cast<int32_t*>(entry + iiContainerStartOff());
                                    std::wstring nm = ic ? ic->GetName() : STR("(null)");
                                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [DISCOVERY]   [{}] id={} slot={} containerStart={} class={}\n"),
                                         i, id, slot, cs, nm.c_str());
                                }
                            }
                        }
                    }
                    showOnScreen(
                        L"Goat storage not found - see log DISCOVERY dump",
                        3.5f, 1.0f, 0.7f, 0.3f);
                    return;
                }

                // Container exists! Open StorageMode widget bound to it
                openStorageWidgetForHandle(goat, goatInv, bagHandle);
                return;
            }

            // ───── LEGACY rc.66 placeholder + earlier paths preserved below ─────

            // ───── LEGACY paths below are unreachable. Kept for reference
            //       until rc.67+ builds the sidecar persistence layer.
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
            // [rc.12e 2026-05-23] Extended UFunction dump using
            // ForEachFunctionInChain — enumerates EVERY UFunction in the
            // class chain (not just the hardcoded candidate list rc.3
            // probed). Filters to names containing inventory/storage/open/
            // show/access/container/use/server/local/broadcast/UI
            // keywords. One-shot per session, gated by
            // m_goatSaddlebagDiagDumped. Goal: surface any cross-actor-
            // aware UFunction we missed (OpenWith / OpenContainerFor /
            // BroadcastInventoryOpened / Server_OpenFor / etc.).
            if (!m_goatSaddlebagDiagDumped) {
                m_goatSaddlebagDiagDumped = true;
                auto dumpAllUFuncs = [&](UObject* o, const wchar_t* label) {
                    if (!o || !isObjectAlive(o)) return;
                    RC::Unreal::UClass* c = nullptr;
                    try { c = o->GetClassPrivate(); } catch (...) { return; }
                    if (!c) return;
                    VLOG(STR("[MoriaCppMod] [GoatSaddleDiag] === ALL UFuncs (keyword-filtered) on {} ===\n"), label);
                    int totalCount = 0;
                    int hitCount = 0;
                    try {
                        for (auto* fn : c->ForEachFunctionInChain())
                        {
                            if (totalCount >= 800) break;
                            ++totalCount;
                            std::wstring fnName;
                            try { fnName = fn->GetName(); } catch (...) { continue; }
                            std::wstring lo = fnName;
                            for (auto& ch : lo) ch = (wchar_t)towlower(ch);
                            bool isInteresting =
                                lo.find(L"open")       != std::wstring::npos
                             || lo.find(L"show")       != std::wstring::npos
                             || lo.find(L"access")     != std::wstring::npos
                             || lo.find(L"inventory")  != std::wstring::npos
                             || lo.find(L"storage")    != std::wstring::npos
                             || lo.find(L"container")  != std::wstring::npos
                             || lo.find(L"interact")   != std::wstring::npos
                             || lo.find(L"use")        != std::wstring::npos
                             || lo.find(L"broadcast")  != std::wstring::npos
                             || lo.find(L"hud")        != std::wstring::npos
                             || lo.find(L"widget")     != std::wstring::npos;
                            if (!isInteresting) continue;
                            int parms = 0;
                            try { parms = fn->GetParmsSize(); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [GoatSaddleDiag]   {} (parmSize={}) PRESENT\n"),
                                 fnName.c_str(), parms);
                            ++hitCount;
                        }
                    } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [GoatSaddleDiag] === end ({} hits / {} scanned) ===\n"),
                         hitCount, totalCount);
                };
                dumpAllUFuncs(goatEquip, STR("goat.EquipComp"));
                dumpAllUFuncs(goatInv,   STR("goat.InvComp"));
                dumpAllUFuncs(goat,      STR("goat (actor)"));
                if (m_localPC && isObjectAlive(m_localPC)) {
                    dumpAllUFuncs(m_localPC, STR("player.PC"));
                }
                // Also dump BP_SaddleBags_Goat_C's class — bag class might
                // expose its own opener.
                UClass* saddleCls = ensureSaddlebagItemClass();
                if (saddleCls && isObjectAlive(saddleCls))
                {
                    UObject* saddleCDO = nullptr;
                    try { saddleCDO = saddleCls->GetClassDefaultObject(); } catch (...) {}
                    if (saddleCDO && isObjectAlive(saddleCDO))
                        dumpAllUFuncs(saddleCDO, STR("BP_SaddleBags_Goat_C (CDO)"));
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

            // [rc.12d 2026-05-23] Priority match for psi.2 pak:
            //   1. Prefer BP_SaddleBags_Goat_C (the actual bag — what
            //      psi.2's loadout chain should spawn into goat inventory)
            //   2. Fall back to wrapper / SaddleBag / EpicPack / Goat_Slot
            //   3. Fall back to first non-zero item
            int stride  = iiSize();
            int itemOff = iiItemOff();
            int idOff   = iiIDOff();
            int32_t bagID = 0;       std::wstring bagCls;       // priority 1
            int32_t wrapperID = 0;   std::wstring wrapperCls;   // priority 2
            int32_t firstID = 0;     std::wstring firstCls;     // priority 3
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
                if (itemID == 0) continue;
                if (firstID == 0) { firstID = itemID; firstCls = cls; }
                // Priority 1: the actual bag instance (psi.2 loadout chain output).
                if (bagID == 0 && (cls == STR("BP_SaddleBags_Goat_C")
                                || cls == STR("BP_PorterGoatSaddlebags_C")))
                {
                    bagID = itemID;
                    bagCls = cls;
                }
                // Priority 2: wrapper or pack-shaped fallback.
                else if (wrapperID == 0
                       && (cls.find(STR("Goat_Slot")) != std::wstring::npos
                        || cls.find(STR("SaddleBag")) != std::wstring::npos
                        || cls.find(STR("Saddlebag")) != std::wstring::npos
                        || cls.find(STR("EpicPack")) != std::wstring::npos))
                {
                    wrapperID = itemID;
                    wrapperCls = cls;
                }
            }
            int32_t targetID = 0;
            std::wstring targetCls;
            if (bagID != 0)         { targetID = bagID;     targetCls = bagCls;     }
            else if (wrapperID != 0){ targetID = wrapperID; targetCls = wrapperCls; }
            else if (firstID != 0)  { targetID = firstID;   targetCls = firstCls;   }
            if (targetID == 0)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] no usable item handle in goat inventory\n"));
                showOnScreen(L"No usable items on goat", 2.5f, 0.9f, 0.7f, 0.4f);
                return;
            }
            const wchar_t* tier = (bagID != 0)         ? STR("priority1=BAG (psi.2 loadout)")
                                : (wrapperID != 0)     ? STR("priority2=WRAPPER (pre-psi)")
                                :                        STR("priority3=FIRST (fallback)");
            VLOG(STR("[MoriaCppMod] [GoatSaddle] targeting handle id={} class='{}' tier={}\n"),
                 targetID, targetCls.empty() ? STR("?") : targetCls.c_str(), tier);

            // [rc.18 TEST 1 — Desktop Claude plan, 2026-05-25]
            // The BndEvt__..._MorNpcOnManageLocalInteraction__DelegateSignature
            // handler IS the compiled BP-graph body. Firing it (rc.12f-rc.17)
            // dispatched cleanly via ProcessEvent but produced NO UI — the
            // graph likely branches internally on NpcInfo / role registration
            // which our bell-summoned goat lacks. DC's Test 1: skip the NPC
            // manage chain entirely. Treat the bag at goat.InvComp[id=bagID]
            // as a container item the player is "using" — same code path the
            // engine uses to open a placed chest. No NPC-registration gate.
            //
            // FItemHandle layout (parmSize=20):
            //   int32 ID at +0, int32 Payload at +4, FWeakObjectPtr (8 B) at +8, 4 B pad
            // We write only ID; engine resolves the rest via container lookup.
            //
            // Priority order if ServerUse no-ops: UseFromItemHandle (parmSize=20),
            // UseOrEquipItem (parmSize=20). All three are container-open shaped
            // per the GoatSaddleDiag dump in this morning's log.
            // [rc.20 TEST 3 — Desktop Claude, 2026-05-25]
            // Tests 1, 1b, 1c (rc.17-19) all silent-fail. DC's bytecode
            // disasm confirmed BP_SaddleBags_Goat_C is passive data with
            // zero opener UFunctions — the 4-call C++ engine chain
            // (BPGetManager → screen getter → SetInteracting → Show) is
            // the only path AND it bails silently inside one of those C++
            // calls for our non-registered goat.
            //
            // Test 3: bypass the whole BP entry chain. Spawn the StorageMode
            // screen widget directly, set its instance vars to point at the
            // goat's epic-pack sub-container, fire HandleStorageView(true),
            // AddToViewport. Widget doesn't care about NpcInfo — it just
            // renders whatever container its inst vars reference.
            openGoatSaddlebagInventory_Test3(goat, goatInv, targetID);
        }

        // Test 3 — direct widget spawn + bind path. Per DC spec:
        //   1. Resolve WBP_UI_Inventory_Screen_StorageMode_C
        //   2. Call goatInv->GetContainerByTag(Tag="Inventory.Slot.EpicPack")
        //      → returns FInventoryItemHandle (20 B) for the pack container
        //   3. CreateWidget via WidgetBlueprintLibrary::Create
        //   4. Reflect-write inst vars on the spawned widget:
        //        storageInventoryHandle   (FStructProperty, 20 B FInventoryItemHandle)
        //        epicPackTag              (FStructProperty, 8 B FGameplayTag)
        //        InventoryContainerEpicPackTag (FStructProperty, 8 B FGameplayTag)
        //        isStorageView            (FBoolProperty, true)
        //   5. Fire HandleStorageView(bool NPCMode=true)
        //   6. AddToViewport
        void openGoatSaddlebagInventory_Test3(UObject* goat, UObject* goatInv, int32_t targetID)
        {
            // [rc.34 STACKED-WIDGET FIX 2026-06-02] If a Test 3 widget is
            // already alive (user re-clicked Saddlebags without ESC), dismiss
            // it first. Otherwise we stack widgets and ESC only removes the
            // top one — underlying widgets continue to swallow input.
            if (m_test3SaddlebagWidget && isObjectAlive(m_test3SaddlebagWidget))
            {
                if (auto* rmFn = m_test3SaddlebagWidget->GetFunctionByNameInChain(STR("RemoveFromParent")))
                {
                    try { safeProcessEvent(m_test3SaddlebagWidget, rmFn, nullptr); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] dismissed stale Test 3 widget 0x{:p} before re-spawn\n"),
                         (void*)m_test3SaddlebagWidget);
                }
                m_test3SaddlebagWidget = nullptr;
            }

            // [rc.34 PATH C — NATIVE BNDEVT 2026-06-02]
            // Now that Path A adopted a valid NpcGuid (3720658F-...) and the
            // goat IS in NpcInfo, retry the BndEvt MorNpcOnManageLocalInteraction
            // handler. rc.17 tried this with NpcGuid=0 and the engine's screen-
            // getter returned null. With a valid GUID + NpcInfo entry, the
            // 4-call C++ chain (BPGetManager → screen-getter → SetInteracting →
            // Show) should now resolve. If the engine shows native UI, we skip
            // Test 3 entirely (engine handles its own ESC + state).
            UClass* goatCls = nullptr;
            try { goatCls = goat->GetClassPrivate(); } catch (...) {}
            UFunction* localMgmtFn = nullptr;
            if (goatCls)
            {
                try {
                    for (auto* fn : goatCls->ForEachFunctionInChain())
                    {
                        if (!fn) continue;
                        std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                        if (fname.find(STR("MorNpcOnManageLocalInteraction")) != std::wstring::npos)
                        {
                            localMgmtFn = fn;
                            break;
                        }
                    }
                } catch (...) {}
            }
            if (localMgmtFn)
            {
                std::wstring fname; try { fname = localMgmtFn->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [PathC] firing BndEvt '{}' on goat (NpcGuid is now valid — engine state should resolve)\n"),
                     fname.c_str());
                try { safeProcessEvent(goat, localMgmtFn, nullptr); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [PathC] BndEvt dispatch returned — observe if engine native UI appears (Test 3 will NOT fire)\n"));
                showOnScreen(L"PathC: Native BndEvt fired (look for engine UI)", 2.0f, 0.4f, 0.9f, 0.4f);
                return;  // Don't fall through to Test 3 — give the native path a clean test
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [PathC] MorNpcOnManageLocalInteraction not found — falling back to Test 3\n"));

            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] === direct widget spawn + bind ===\n"));

            // 1) Resolve widget class. May not be resident; LoadClassAsset_Blocking
            //    is the canonical lazy-load. Same pattern used for BP_PorterGoat.
            const wchar_t* widgetPath = STR("/Game/UI/Inventory/WBP_UI_Inventory_Screen_StorageMode.WBP_UI_Inventory_Screen_StorageMode_C");
            UClass* widgetCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widgetPath);
            if (!widgetCls)
            {
                widgetCls = goat_loadClassAssetBlocking(widgetPath);
                if (!widgetCls)
                {
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] widget class missing (StorageMode_C): {}\n"), widgetPath);
                    showOnScreen(L"StorageMode widget class not found", 2.5f, 0.9f, 0.4f, 0.4f);
                    return;
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] widget class resolved: {:p}\n"), (void*)widgetCls);

            // 2) Build FGameplayTag for "Inventory.Slot.EpicPack" (NM[748]).
            //    FGameplayTag = { FName TagName; } — 8 bytes, just the name.
            RC::Unreal::FName epicPackName(STR("Inventory.Slot.EpicPack"), RC::Unreal::FNAME_Add);
            uint8_t epicPackTag[8] = {0};
            std::memcpy(epicPackTag, &epicPackName, sizeof(RC::Unreal::FName));

            // 3) Call goatInv->GetContainerByTag(epicPackTag) → FInventoryItemHandle (20 B).
            //    parmSize=28 per the GoatSaddleDiag dump (8 in + 20 out).
            auto* getByTagFn = goatInv->GetFunctionByNameInChain(STR("GetContainerByTag"));
            if (!getByTagFn)
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] GetContainerByTag missing on goat.InvComp — bail\n"));
                showOnScreen(L"goat.InvComp has no GetContainerByTag", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            int tagBufSz = getByTagFn->GetParmsSize();
            std::vector<uint8_t> tagBuf(tagBufSz, 0);
            auto* pTagIn = findParam(getByTagFn, STR("Tag"));
            if (!pTagIn) pTagIn = findParam(getByTagFn, STR("ContainerTag"));
            if (!pTagIn) pTagIn = findParam(getByTagFn, STR("InTag"));
            auto* pTagRet = findParam(getByTagFn, STR("ReturnValue"));
            if (pTagIn)
            {
                std::memcpy(tagBuf.data() + pTagIn->GetOffset_Internal(), epicPackTag, 8);
            }
            else
            {
                // Best-effort: write tag at offset 0
                std::memcpy(tagBuf.data(), epicPackTag, 8);
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] firing GetContainerByTag(Tag='Inventory.Slot.EpicPack') on goatInv={:p} (parmSize={}, tagOff={}, retOff={})\n"),
                 (void*)goatInv, tagBufSz,
                 pTagIn ? pTagIn->GetOffset_Internal() : -1,
                 pTagRet ? pTagRet->GetOffset_Internal() : -1);
            try { safeProcessEvent(goatInv, getByTagFn, tagBuf.data()); } catch (...) {}

            // 4) Read back the 20-byte FInventoryItemHandle from GetContainerByTag,
            //    but [rc.21 2026-05-25] OVERRIDE it with the BAG's handle (targetID,
            //    typically =4 = BP_SaddleBags_Goat_C). Yesterday's rc.20 run showed
            //    GetContainerByTag(Inventory.Slot.EpicPack) returns ID=3 — the slot
            //    WRAPPER (BP_ContainerItem_Goat_Slot_EpicPack_C), a 1-slot container
            //    that just holds the bag. Binding the widget to the wrapper renders
            //    a 1-slot panel showing the bag icon — not the 8×8 bag contents.
            //    The bag at id=4 IS the storage we want; use it directly.
            int retOff = pTagRet ? pTagRet->GetOffset_Internal() : 8;
            uint8_t slotHandle[20] = {0};
            std::memcpy(slotHandle, tagBuf.data() + retOff, 20);
            int32_t slotHandleId = *reinterpret_cast<int32_t*>(slotHandle + 0);
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] GetContainerByTag returned SLOT handle ID={} (informational only; binding to BAG instead)\n"),
                 slotHandleId);

            // [rc.22 CRITICAL FIX 2026-05-25] FInventoryItemHandle layout per
            // moria_goat.inl:4260 is: int32 ID(+0) + int32 Payload(+4) +
            // FWeakObjectPtr OwnerComp(+8, 8B) + 4B pad. Writing only ID
            // (rc.20/21) left OwnerComp=null → engine can't dereference the
            // handle, both panes render empty/wrong. Wire OwnerComp = goatInv
            // so id=4 resolves against the goat's MorInventoryComponent.
            uint8_t packHandle[20] = {0};
            *reinterpret_cast<int32_t*>(packHandle + 0) = targetID;
            {
                RC::Unreal::FWeakObjectPtr ownerWP(goatInv);
                std::memcpy(packHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] binding storageInventoryHandle to BAG ID={} with OwnerComp=goatInv={:p}\n"), targetID, (void*)goatInv);

            // 5) Spawn the widget.
            UObject* w = jw_createGameWidget(widgetCls);
            if (!w || !isObjectAlive(w))
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] CreateWidget returned null/dead\n"));
                showOnScreen(L"StorageMode widget create failed", 2.5f, 0.9f, 0.4f, 0.4f);
                return;
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] widget spawned: {:p}\n"), (void*)w);

            // [rc.21 DIAG 2026-05-25] Dump every FProperty on the widget class
            // so we can see what other inst vars exist beyond the 4 DC specced.
            // User reports player-inventory pane also broken — likely there's a
            // playerInventoryComponent / playerHandle inst var that needs binding.
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] === ALL widget instance vars (FProperty) ===\n"));
            UClass* wCls = nullptr;
            try { wCls = w->GetClassPrivate(); } catch (...) {}
            int propCount = 0;
            if (wCls)
            {
                try {
                    for (auto* prop : wCls->ForEachPropertyInChain())
                    {
                        if (!prop) continue;
                        std::wstring pn; try { pn = prop->GetName(); } catch (...) { continue; }
                        std::wstring tn; try { tn = prop->GetClass().GetName(); } catch (...) {}
                        int off = -1; try { off = prop->GetOffset_Internal(); } catch (...) {}
                        int sz  = -1; try { sz  = prop->GetSize(); } catch (...) {}
                        // Filter to widget-class-defined props (skip massive UUserWidget base churn);
                        // keep anything whose name suggests inventory/handle/storage/NPC/player/tag.
                        bool keep =
                            pn.find(STR("storage")) != std::wstring::npos ||
                            pn.find(STR("Storage")) != std::wstring::npos ||
                            pn.find(STR("npc"))     != std::wstring::npos ||
                            pn.find(STR("NPC"))     != std::wstring::npos ||
                            pn.find(STR("Npc"))     != std::wstring::npos ||
                            pn.find(STR("inventory")) != std::wstring::npos ||
                            pn.find(STR("Inventory")) != std::wstring::npos ||
                            pn.find(STR("Handle")) != std::wstring::npos ||
                            pn.find(STR("handle")) != std::wstring::npos ||
                            pn.find(STR("Container")) != std::wstring::npos ||
                            pn.find(STR("Tag")) != std::wstring::npos ||
                            pn.find(STR("player")) != std::wstring::npos ||
                            pn.find(STR("Player")) != std::wstring::npos ||
                            pn.find(STR("epic")) != std::wstring::npos ||
                            pn.find(STR("Epic")) != std::wstring::npos ||
                            pn.find(STR("Pack")) != std::wstring::npos ||
                            pn.find(STR("Owner")) != std::wstring::npos ||
                            pn.find(STR("Target")) != std::wstring::npos ||
                            pn.find(STR("Root")) != std::wstring::npos ||
                            pn.find(STR("isOpened")) != std::wstring::npos ||
                            pn.find(STR("IsOpened")) != std::wstring::npos;
                        if (keep)
                        {
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3]   prop '{}' type={} off=0x{:04x} size={}\n"),
                                 pn.c_str(), tn.c_str(), off, sz);
                            ++propCount;
                        }
                    }
                } catch (...) {}
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] === end ({} keyword-matched props) ===\n"), propCount);

            // 6) Write instance vars via reflection. memcpy raw bytes into the
            //    struct slots; UE doesn't care as long as the layout matches.
            auto writeStruct = [&](const wchar_t* propName, const uint8_t* src, int size) {
                auto* propPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(propName);
                if (!propPtr)
                {
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] inst var '{}' not found on widget\n"), propName);
                    return false;
                }
                std::memcpy(propPtr, src, size);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote '{}' ({} B) at {:p}\n"),
                     propName, size, (void*)propPtr);
                return true;
            };

            writeStruct(STR("storageInventoryHandle"), packHandle, 20);
            writeStruct(STR("epicPackTag"), epicPackTag, 8);
            writeStruct(STR("InventoryContainerEpicPackTag"), epicPackTag, 8);

            auto* boolPtr = w->GetValuePtrByPropertyNameInChain<bool>(STR("isStorageView"));
            if (boolPtr)
            {
                *boolPtr = true;
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote isStorageView=true\n"));
            }

            // [rc.22 PLAYER PANE 2026-05-25] Bind right-side pane to the player's
            // body inventory. From the FProperty dump:
            //   InventoryComponent (ObjectProperty @ 0x0538) — main InvComp ref
            //   bodyInventoryHandle (StructProperty 20B @ 0x0548) — body container handle
            //   AssociatedNPC (ObjectProperty @ 0x0748) — NPC actor for storage view
            //   isOpenedFromNPC (BoolProperty @ 0x0740) — distinct from isStorageView
            //
            // Without these, the widget renders with null component refs and the
            // panes show garbage/empty.
            UObject* pc = m_localPC && isObjectAlive(m_localPC) ? m_localPC : findPlayerController();
            UObject* pawn = m_localPawn && isObjectAlive(m_localPawn) ? m_localPawn : nullptr;
            UObject* playerInv = nullptr;
            if (pawn)
            {
                UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Script/Moria.MorInventoryComponent"));
                if (invCls)
                {
                    auto* getCompFn = pawn->GetFunctionByNameInChain(STR("GetComponentByClass"));
                    if (getCompFn)
                    {
                        std::vector<uint8_t> gb(getCompFn->GetParmsSize(), 0);
                        writeGoatParm<UClass*>(getCompFn, gb.data(), STR("ComponentClass"), invCls);
                        try { safeProcessEvent(pawn, getCompFn, gb.data()); } catch (...) {}
                        playerInv = readGoatParm<UObject*>(getCompFn, gb.data(), STR("ReturnValue"), nullptr);
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] resolved player InvComp={:p}\n"), (void*)playerInv);

            // Write InventoryComponent = playerInv (right-pane component).
            if (auto* invCompPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComponent")))
            {
                *invCompPtr = playerInv;
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote InventoryComponent={:p}\n"), (void*)playerInv);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] InventoryComponent prop not found\n"));
            }

            // Build the player's body inventory handle.
            // Query playerInv->GetContainerByTag("Inventory.BodyInventory") → 20B handle.
            uint8_t playerBodyHandle[20] = {0};
            if (playerInv)
            {
                RC::Unreal::FName bodyTagName(STR("Inventory.BodyInventory"), RC::Unreal::FNAME_Add);
                auto* getByTagFn2 = playerInv->GetFunctionByNameInChain(STR("GetContainerByTag"));
                if (getByTagFn2)
                {
                    int sz = getByTagFn2->GetParmsSize();
                    std::vector<uint8_t> tb(sz, 0);
                    auto* pIn = findParam(getByTagFn2, STR("Tag"));
                    if (!pIn) pIn = findParam(getByTagFn2, STR("ContainerTag"));
                    if (!pIn) pIn = findParam(getByTagFn2, STR("InTag"));
                    auto* pRet = findParam(getByTagFn2, STR("ReturnValue"));
                    int tagOff = pIn ? pIn->GetOffset_Internal() : 0;
                    int rOff   = pRet ? pRet->GetOffset_Internal() : 8;
                    std::memcpy(tb.data() + tagOff, &bodyTagName, sizeof(RC::Unreal::FName));
                    try { safeProcessEvent(playerInv, getByTagFn2, tb.data()); } catch (...) {}
                    std::memcpy(playerBodyHandle, tb.data() + rOff, 20);
                    int32_t bodyId = *reinterpret_cast<int32_t*>(playerBodyHandle + 0);
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] player body handle: id={} (from GetContainerByTag Inventory.BodyInventory)\n"), bodyId);
                    // Ensure owner = playerInv (GetContainerByTag may already populate it, but
                    // overwrite to be safe).
                    RC::Unreal::FWeakObjectPtr ownerWP(playerInv);
                    std::memcpy(playerBodyHandle + 8, &ownerWP, sizeof(RC::Unreal::FWeakObjectPtr));
                }
            }
            if (auto* bodyPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("bodyInventoryHandle")))
            {
                std::memcpy(bodyPtr, playerBodyHandle, 20);
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote bodyInventoryHandle (20 B) at {:p}\n"), (void*)bodyPtr);
            }

            // AssociatedNPC = goat actor.
            if (auto* npcPtr = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("AssociatedNPC")))
            {
                *npcPtr = goat;
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote AssociatedNPC=goat={:p}\n"), (void*)goat);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] AssociatedNPC prop not found\n"));
            }

            // isOpenedFromNPC = true (distinct from isStorageView).
            if (auto* nbPtr = w->GetValuePtrByPropertyNameInChain<bool>(STR("isOpenedFromNPC")))
            {
                *nbPtr = true;
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] wrote isOpenedFromNPC=true\n"));
            }

            // 7) Fire HandleStorageView(bool NPCMode=true).
            auto* hsvFn = w->GetFunctionByNameInChain(STR("HandleStorageView"));
            if (hsvFn)
            {
                int hsvSz = hsvFn->GetParmsSize();
                std::vector<uint8_t> hsvBuf(hsvSz, 0);
                // First (and only) param is bool NPCMode. Write at offset 0.
                hsvBuf[0] = 1;  // true
                auto* pNpcMode = findParam(hsvFn, STR("NPCMode"));
                if (!pNpcMode) pNpcMode = findParam(hsvFn, STR("NPC Mode?"));
                if (!pNpcMode) pNpcMode = findParam(hsvFn, STR("bNPCMode"));
                if (pNpcMode)
                {
                    hsvBuf[pNpcMode->GetOffset_Internal()] = 1;
                    VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] HandleStorageView NPCMode param off={}\n"),
                         pNpcMode->GetOffset_Internal());
                }
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] firing HandleStorageView(NPCMode=true) parmSize={}\n"), hsvSz);
                try { safeProcessEvent(w, hsvFn, hsvBuf.data()); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] HandleStorageView returned\n"));
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] HandleStorageView UFunction not found on widget\n"));
            }

            // 8) AddToViewport.
            // [rc.22] Cache widget for ESC dismissal (see tickTest3EscDismiss in dllmain.cpp).
            m_test3SaddlebagWidget = w;
            auto* addFn = w->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn)
            {
                int addSz = addFn->GetParmsSize();
                std::vector<uint8_t> addBuf(addSz, 0);
                // ZOrder int32 at offset 0 (first param). Use 100 to sit above HUD.
                if (addSz >= 4) *reinterpret_cast<int32_t*>(addBuf.data()) = 100;
                try { safeProcessEvent(w, addFn, addBuf.data()); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] AddToViewport fired (ZOrder=100)\n"));
                showOnScreen(L"Test 3: StorageMode widget added to viewport", 1.5f, 0.4f, 0.9f, 0.4f);
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [GoatSaddle] [Test 3] AddToViewport UFunction missing\n"));
                showOnScreen(L"Test 3: widget spawned but cannot show", 2.5f, 0.9f, 0.4f, 0.4f);
            }
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
                    if ((s_inputAssertLogCounter++ % 50) == 0)
                        VLOG(STR("[MoriaCppMod] [GoatSaddle] [v8.2.x] input-mode UI enforcement active (10 Hz while open)\n"));
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
                        if (auto* f = c->GetFunctionByNameInChain(STR("SetVisibility")))
                        {
                            std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                            vb[0] = v;
                            try { safeProcessEvent(c, f, vb.data()); } catch (...) {}
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
                                if (auto* f = ww->GetFunctionByNameInChain(STR("SetVisibility")))
                                {
                                    std::vector<uint8_t> vb(f->GetParmsSize(), 0);
                                    vb[0] = 1;  // Collapsed
                                    try { safeProcessEvent(ww, f, vb.data()); } catch (...) {}
                                }
                                found++;
                                return;
                            }
                            if (auto* gcc = ww->GetFunctionByNameInChain(STR("GetChildrenCount")))
                            {
                                std::vector<uint8_t> b(gcc->GetParmsSize(), 0);
                                try { safeProcessEvent(ww, gcc, b.data()); } catch (...) {}
                                int32_t n = 0;
                                if (auto* pr = findParam(gcc, STR("ReturnValue")))
                                    n = *reinterpret_cast<int32_t*>(b.data() + pr->GetOffset_Internal());
                                if (auto* gca = ww->GetFunctionByNameInChain(STR("GetChildAt")))
                                {
                                    for (int32_t i = 0; i < n && i < 32; i++)
                                    {
                                        std::vector<uint8_t> cb(gca->GetParmsSize(), 0);
                                        if (auto* pi = findParam(gca, STR("Index")))
                                            *reinterpret_cast<int32_t*>(cb.data() + pi->GetOffset_Internal()) = i;
                                        try { safeProcessEvent(ww, gca, cb.data()); } catch (...) {}
                                        UObject* child = nullptr;
                                        if (auto* pr2 = findParam(gca, STR("ReturnValue")))
                                            child = *reinterpret_cast<UObject**>(cb.data() + pr2->GetOffset_Internal());
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
                                    if (*v == 1 || *v == 2)  // Collapsed / Hidden
                                        paneBroken = true;
                            }
                        }
                        if (paneBroken && m_sbGoatInvCache && isObjectAlive(m_sbGoatInvCache))
                        {
                            VLOG(STR("[MoriaCppMod] [GoatSaddle] [rc.111] pane broken (npcWidget={} ) — re-driving saddlebag grid\n"), found);
                            driveSaddlebagStorageContainer(m_goatSaddlebagWidget, m_sbGoatInvCache,
                                                           m_sbPlayerInvCache, m_sbHandleCache, STR("rc.111 heal"));
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
                    driveSaddlebagStorageContainer(m_goatSaddlebagWidget, m_sbGoatInvCache,
                                                   m_sbPlayerInvCache, m_sbHandleCache, STR("rc.109 re-drive"));
            }

            // Edge-trigger Esc OR Tab.
            static bool s_lastEsc = false;
            static bool s_lastTab = false;
            bool eDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            bool tDown = (GetAsyncKeyState(VK_TAB)    & 0x8000) != 0;
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
                if (seh_findAnyGoatActor(&hit))
                {
                    for (UObject* g : hit)
                        if (g && isObjectAlive(g)) { goat = g; break; }
                }
            }
            if (!goat)
            {
                // [rc.68 2026-06-29] No live goat -> SPAWN one via the same
                // path GA_Bell would use. This makes NUM+ a universal summon
                // key that works even without the bell BP (which Tobi removed
                // from 4.3.0). spawnBellGoat handles BeginDeferredActorSpawn,
                // FinishSpawningActor, SpawnDefaultController, and pushes
                // to m_followGoats.
                VLOG(STR("[MoriaCppMod] [Summon] no live goat -> spawning fresh via spawnBellGoat()\n"));
                showOnScreen(L"Summoning goat (no bell needed)", 2.0f, 0.4f, 0.9f, 0.4f);
                spawnBellGoat();
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

            // Destination: 250 units in FRONT of the player, +50 up.
            // Teleporting onto the player's exact spot fails encroachment;
            // that silent failure (plus the natively-restored goat sitting
            // kilometers away) is why the bell "stopped summoning".
            const float yawRad = pRot[1] * 3.14159265f / 180.0f;
            float dest[3] = { pLoc[0] + cosf(yawRad) * 250.0f,
                              pLoc[1] + sinf(yawRad) * 250.0f,
                              pLoc[2] + 50.0f };

            // 3. PRIMARY: K2_TeleportTo — the NPC-recovery-proven primitive
            //    (commits through the movement component) and it RETURNS
            //    success, so failure is visible instead of silent.
            bool arrived = false;
            if (auto* k2t = goat->GetFunctionByNameInChain(STR("K2_TeleportTo")))
            {
                std::vector<uint8_t> tb(k2t->GetParmsSize(), 0);
                if (auto* pD = findParam(k2t, STR("DestLocation")))
                    std::memcpy(tb.data() + pD->GetOffset_Internal(), dest, sizeof(dest));
                if (auto* pR = findParam(k2t, STR("DestRotation")))
                    std::memcpy(tb.data() + pR->GetOffset_Internal(), pRot, sizeof(pRot));
                if (safeProcessEvent(goat, k2t, tb.data()))
                    if (auto* pRet = findParam(k2t, STR("ReturnValue")))
                        arrived = *reinterpret_cast<bool*>(tb.data() + pRet->GetOffset_Internal());
                VLOG(STR("[MoriaCppMod] [Summon] K2_TeleportTo -> {}\n"),
                     arrived ? STR("OK") : STR("FAILED"));
            }
            if (!arrived)
            {
                // Fallback: force the location outright (bTeleport=true).
                if (auto* setLoc = goat->GetFunctionByNameInChain(STR("K2_SetActorLocation")))
                {
                    std::vector<uint8_t> sb(setLoc->GetParmsSize(), 0);
                    if (auto* pD = findParam(setLoc, STR("NewLocation")))
                        std::memcpy(sb.data() + pD->GetOffset_Internal(), dest, sizeof(dest));
                    if (auto* pT = findParam(setLoc, STR("bTeleport")))
                        *reinterpret_cast<bool*>(sb.data() + pT->GetOffset_Internal()) = true;
                    try { safeProcessEvent(goat, setLoc, sb.data()); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [Summon] fallback K2_SetActorLocation(teleport) fired\n"));
                }
            }

            // 4. ServerTeleportTo (replication path) — kept as a follow-up,
            //    no longer the only mechanism.
            if (auto* teleFn = goat->GetFunctionByNameInChain(STR("ServerTeleportTo")))
            {
                int psz = teleFn->GetParmsSize();
                std::vector<uint8_t> pbuf(psz, 0);
                auto* destLocProp = findParam(teleFn, STR("DestLocation"));
                auto* destRotProp = findParam(teleFn, STR("DestRotation"));
                if (destLocProp && destRotProp)
                {
                    std::memcpy(pbuf.data() + destLocProp->GetOffset_Internal(), dest, sizeof(dest));
                    std::memcpy(pbuf.data() + destRotProp->GetOffset_Internal(), pRot, sizeof(pRot));
                    try { safeProcessEvent(goat, teleFn, pbuf.data()); } catch (...) {}
                }
            }
            VLOG(STR("[MoriaCppMod] [Summon] teleport sequence complete (arrived={})\n"), arrived);
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

        // DEAD (ephemeral design): world-goat adoption is retired — the only
        // companion is the one the bell spawned (tracked at spawn). Kept as
        // an inert stub because the tick is still wired in dllmain.
        ULONGLONG m_lastNativeGoatScanMs{0};
        void tickAdoptNativeGoat()
        {
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
            auto* isValidFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:IsValid"));
            auto* kslCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary"));
            UObject* kslCDO = kslCls ? kslCls->GetClassDefaultObject() : nullptr;
            if (isValidFn && kslCDO)
            {
                std::vector<uint8_t> b(isValidFn->GetParmsSize(), 0);
                if (auto* pObj = findParam(isValidFn, STR("Object")))
                    *reinterpret_cast<UObject**>(b.data() + pObj->GetOffset_Internal()) = g;
                bool valid = false;
                if (safeProcessEvent(kslCDO, isValidFn, b.data()))
                    if (auto* pRet = findParam(isValidFn, STR("ReturnValue")))
                        valid = *reinterpret_cast<bool*>(b.data() + pRet->GetOffset_Internal());
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

            // EPHEMERAL GOAT (user decision 2026-07-12): the only persistent
            // state is the saddlebag pack in the PLAYER's inventory. The goat
            // itself carries nothing, so the bell is a plain toggle:
            //   goat present → DESTROY it
            //   no goat      → SPAWN a fresh one (unregistered passive fauna)
            // No hide/park/recall, no identity, no registry. History of the
            // old design: memory goat-final-architecture.
            UObject* live = nullptr;
            for (auto& g : m_followGoats)
            {
                UObject* p = g.pawn.Get();
                if (isGoatActorUsable(p)) { live = p; break; }
            }
            if (!live) live = findAnyGoatInWorld();  // untracked stray counts too

            if (live)
            {
                VLOG(STR("[MoriaCppMod] [BellToggle] DISMISS — destroying goat {:p} (ephemeral; pack lives with player)\n"),
                     (void*)live);
                if (auto* dFn = live->GetFunctionByNameInChain(STR("K2_DestroyActor")))
                { try { safeProcessEvent(live, dFn, nullptr); } catch (...) {} }
                m_followGoats.clear();
                showOnScreen(L"Rûdh wanders off (saddlebags safe with you)", 2.0f, 0.7f, 0.9f, 0.7f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [BellToggle] SUMMON — spawning fresh goat\n"));
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
            UClass* invCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                STR("/Script/Moria.MorInventoryComponent"));
            if (!invCls) return false;

            // Find the configured "Inventory Comp" (StorageHandle=Goat_Saddlebags),
            // not the empty inherited base component.
            UObject* inv = nullptr;
            UObject* firstAny = nullptr;
            if (auto* gf = goat->GetFunctionByNameInChain(STR("K2_GetComponentsByClass")))
            {
                std::vector<uint8_t> b(gf->GetParmsSize(), 0);
                if (auto* pCls = findParam(gf, STR("ComponentClass")))
                    *reinterpret_cast<UClass**>(b.data() + pCls->GetOffset_Internal()) = invCls;
                try { safeProcessEvent(goat, gf, b.data()); } catch (...) {}
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
                            RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(
                                reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
                            std::wstring sh; try { sh = rn->ToString(); } catch (...) {}
                            if (sh == STR("Goat_Saddlebags")) { inv = c; break; }
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
            *reinterpret_cast<int32_t*>(arrPtr + 8) = 1;   // Num
            *reinterpret_cast<int32_t*>(arrPtr + 12) = 1;  // Max
            VLOG(STR("[MoriaCppMod] [DefCont] wrote DefaultContainers=[{:p}] on Inventory Comp={:p} (was Num={})\n"),
                 (void*)containerCls, (void*)inv, oldNum);
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
            const wchar_t* contPath =
                STR("/Game/Mods/PorterGoat/Items/BP_ContainerItem_Goat_Slot_EpicPack.BP_ContainerItem_Goat_Slot_EpicPack_C");
            UClass* cc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, contPath);
            if (!cc) cc = goat_loadClassAssetBlocking(contPath);
            if (!cc) { VLOG(STR("[MoriaCppMod] [DefCont] archetype: container class not loadable\n")); return; }

            std::vector<UObject*> comps;
            if (!seh_findAllOf(STR("MorInventoryComponent"), &comps)) return;
            int patched = 0, goatComps = 0;
            for (auto* c : comps)
            {
                if (!c || !isObjectAlive(c)) continue;
                auto* shP = c->GetPropertyByNameInChain(STR("StorageHandle"));
                if (!shP) continue;
                RC::Unreal::FName* rn = reinterpret_cast<RC::Unreal::FName*>(
                    reinterpret_cast<uint8_t*>(c) + shP->GetOffset_Internal() + 8);
                std::wstring sh; try { sh = rn->ToString(); } catch (...) {}
                if (sh != STR("Goat_Saddlebags")) continue;
                goatComps++;
                std::wstring nm; try { nm = c->GetName(); } catch (...) {}
                auto* dcProp = c->GetPropertyByNameInChain(STR("DefaultContainers"));
                if (!dcProp) continue;
                uint8_t* arrPtr = reinterpret_cast<uint8_t*>(c) + dcProp->GetOffset_Internal();
                int32_t oldNum = *reinterpret_cast<int32_t*>(arrPtr + 8);
                VLOG(STR("[MoriaCppMod] [DefCont] Goat_Saddlebags comp '{}' {:p} DefaultContainers Num={}\n"),
                     nm.c_str(), (void*)c, oldNum);
                if (oldNum > 0) { patched++; continue; }
                void* elemMem = FMemory::Malloc(sizeof(UClass*), alignof(UClass*));
                if (!elemMem) continue;
                *reinterpret_cast<UClass**>(elemMem) = cc;
                *reinterpret_cast<void**>(arrPtr + 0) = elemMem;
                *reinterpret_cast<int32_t*>(arrPtr + 8) = 1;
                *reinterpret_cast<int32_t*>(arrPtr + 12) = 1;
                VLOG(STR("[MoriaCppMod] [DefCont] PATCHED '{}' {:p} DefaultContainers=[{:p}]\n"),
                     nm.c_str(), (void*)c, (void*)cc);
                patched++;
            }
            VLOG(STR("[MoriaCppMod] [DefCont] archetype patch: {} Goat_Saddlebags comps, {} patched\n"),
                 goatComps, patched);
            if (patched > 0) m_goatArchetypePatched = true;
        }

        //
        // Spawn path mirrors spawnFollowGoat (proven, used by NUM- earlier)
        // minus the Porter role assignment + with bellSpawned=true so the
        // tick loop drives MoveToActor follow.
        void spawnBellGoat()
        {
            VLOG(STR("[MoriaCppMod] [BellSpawn] entry — checking bindings\n"));
            m_lastSpawnGuidAdopted = false;  // [rc.45] reset per spawn; set inside adoption block
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
            bool clsAlive = (m_goatBPClass && isObjectAlive(m_goatBPClass)
                             && !safeObjectName(m_goatBPClass).empty());  // [rc.121] SEH probe — isObjectAlive lies on reused memory
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
                VLOG(STR("[MoriaCppMod] [BellSpawn] null goat — actorClass(cached)={:p} cdo={:p} pawn={:p} spawnLoc=({},{},{})\n"),
                     (void*)m_goatBPClass, (void*)m_kismetGameplayStaticsCDO, (void*)pawn,
                     spawnLoc.X, spawnLoc.Y, spawnLoc.Z);
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
                    VLOG(STR("[MoriaCppMod] [BellSpawn] fallback retry with {} (cls={:p})\n"),
                         altPath, (void*)altCls);
                    writeGoatParm<UClass*>(m_goatBeginSpawnFn, buf.data(), STR("ActorClass"), altCls);
                    if (!safeProcessEvent(m_kismetGameplayStaticsCDO, m_goatBeginSpawnFn, buf.data())) continue;
                    goat = readGoatParm<UObject*>(m_goatBeginSpawnFn, buf.data(), STR("ReturnValue"), nullptr);
                    if (goat)
                    {
                        VLOG(STR("[MoriaCppMod] [BellSpawn] fallback SUCCESS with {} → goat={:p} (caching as new primary)\n"),
                             altPath, (void*)goat);
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
            writeGoatParm<UObject*>      (m_goatFinishSpawnFn, buf2.data(), STR("Actor"),          goat);
            writeGoatParm<FTransformRaw> (m_goatFinishSpawnFn, buf2.data(), STR("SpawnTransform"), xform);
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
                try { gcls = goat->GetClassPrivate(); } catch (...) {}
                std::wstring spawnedCls = gcls ? safeObjectName(gcls) : STR("(null)");
                bool isTobiGoat = (spawnedCls == STR("BP_NpcGoat_C"));
                VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.98 VERIFY] spawned actor class='{}' isTobiGoat={}\n"),
                     spawnedCls.c_str(), isTobiGoat);
                if (!isTobiGoat)
                    showOnScreen(L"WARNING: wrong goat class spawned!", 3.5f, 0.9f, 0.4f, 0.4f);
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
                //
                // Removed: B5 DLL item-grant to player. The saddlebag is no
                // longer an item in player inventory.
                //
                // Saddlebag UI temporarily routed to a placeholder until the
                // sidecar persistence layer is approved + built (see
                // openCharacterSaddlebagPlaceholder).
                FollowGoatRecord rec{};
                rec.pawn               = RC::Unreal::FWeakObjectPtr(goat);
                rec.controller         = RC::Unreal::FWeakObjectPtr();
                rec.bellSpawned        = true;
                rec.interactiveRefired = true;
                m_followGoats.push_back(rec);
                VLOG(STR("[MoriaCppMod] [BellSpawn] [rc.66 Rollback] goat={:p} at ({:.1f},{:.1f},{:.1f}); herd size={}; NO player saddlebag grant (per user correction 2026-06-28)\n"),
                     (void*)goat, spawnLoc.X, spawnLoc.Y, spawnLoc.Z, m_followGoats.size());
                showOnScreen(L"Goat summoned", 2.0f, 0.4f, 0.9f, 0.4f);
                return;
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

                                // [rc.60 TAME GOAT 2026-06-28] Two changes to make
                                // the goat behave like a goat, not a dwarf:
                                //   1. Destroy dwarven inventory child actors
                                //      (BP_ContainerItem_Dwarf_*) — DwarfMirror_v1
                                //      BP brings these in at construction; they're
                                //      what make the goat get "Standing By/Working"
                                //      dwarf status, fight our MoveToActor, etc.
                                //   2. Stop the goat's behavior tree — keeps the
                                //      AI controller (so our MoveTo calls still
                                //      route) but prevents autonomous BT from
                                //      driving the goat against our intent.
                                // Per user's feedback when DC's modified pak (which
                                // removed the dwarven imports) had working follow/stay.
                                tameSpawnedGoat(goat);


                                // [rc.12h 2026-05-24] Re-enable RegisterWithNPCManager.
                                // rc.12g's component-side NpcGuid synthesis confirmed not
                                // sufficient — Take and Add still rejected in the Manage UI.
                                // The goat needs an actual entry in AMorNPCManager.NpcInfo.Items
                                // so the manager-side validation passes when the UI commits
                                // container mutations.
                                //
                                // RegisterWithNPCManager is a UFunction on MorNPCComponent that
                                // walks back up to AMorNPCManager and inserts a real NpcInfo
                                // entry for this NPC. We DROPPED this call in rc.49 era because
                                // it triggered "dwarf-treatment" — goat showed dwarf UIs etc. —
                                // and we wanted a wild-fauna feel. The trade is now inverted:
                                // we EXPLICITLY want dwarf-treatment because rc.12f's Manage
                                // handler depends on it.
                                //
                                // The engine will assign its own NpcGuid as part of registration,
                                // which overwrites whatever rc.12g synthesized below — that's
                                // fine. The synthesis remains as fallback IF registration fails.
                                // [rc.27 RESTORED 2026-05-27] RegisterWithNPCManager was NOT
                                // the cause of "all NPCs missing" — that was Peace Mode
                                // (PeaceMode=true in INI) zeroing AMorAISpawnManager.MaxSpawnLimit.
                                // Restored to operational state.
                                if (auto* regFn = npcComp->GetFunctionByNameInChain(STR("RegisterWithNPCManager")))
                                {
                                    // [rc.24 PATH X PROBE 2026-05-25] Pre/Post NpcInfo.Items.Num()
                                    // snapshot around the RegisterWithNPCManager call. If the count
                                    // grows from N → N+1 after the call, registration is doing the
                                    // job and the goat IS in NpcInfo. If it stays flat, we need a
                                    // different path (manual NpcInfo append).
                                    int32_t preNum = readNpcInfoItemsNum();
                                    VLOG(STR("[MoriaCppMod] [PathX-E] PRE-Register NpcInfo.Items.Num={}\n"), preNum);

                                    // [rc.31 PATH X PROBE F 2026-06-01] Dump MorNPCComponent
                                    // properties that might gate RegisterWithNPCManager. If a
                                    // bIsRegistered/bRegistered/RegistrationStatus flag is set,
                                    // the call will silently no-op. Earlier sessions had delta=+1;
                                    // current session has delta=0 — something on this component
                                    // changed.
                                    auto dumpNpcCompState = [&](const wchar_t* phase) {
                                        VLOG(STR("[MoriaCppMod] [PathX-F] === {} MorNPCComponent state ===\n"), phase);
                                        UClass* compCls = nullptr;
                                        try { compCls = npcComp->GetClassPrivate(); } catch (...) {}
                                        if (!compCls) return;
                                        try {
                                            for (auto* p : compCls->ForEachPropertyInChain())
                                            {
                                                if (!p) continue;
                                                std::wstring pn; try { pn = p->GetName(); } catch (...) { continue; }
                                                // Filter to gating-relevant names.
                                                bool relevant =
                                                    pn.find(STR("Regist"))     != std::wstring::npos ||
                                                    pn.find(STR("Valid"))      != std::wstring::npos ||
                                                    pn.find(STR("Manage"))     != std::wstring::npos ||
                                                    pn.find(STR("Active"))     != std::wstring::npos ||
                                                    pn.find(STR("Settlement")) != std::wstring::npos ||
                                                    pn.find(STR("Role"))       != std::wstring::npos ||
                                                    pn.find(STR("NpcGuid"))    != std::wstring::npos ||
                                                    pn.find(STR("Interacting")) != std::wstring::npos ||
                                                    (pn.size() > 1 && pn[0] == 'b' && (pn[1] >= 'A' && pn[1] <= 'Z'));  // bool flags bXxx
                                                if (!relevant) continue;
                                                std::wstring pcn; try { pcn = p->GetClass().GetName(); } catch (...) {}
                                                int32 off = -1; try { off = p->GetOffset_Internal(); } catch (...) {}
                                                uint8_t* base = reinterpret_cast<uint8_t*>(npcComp) + off;
                                                // Best-effort: log value for common types.
                                                if (pcn == STR("BoolProperty") && isReadableMemory(base, 1))
                                                {
                                                    VLOG(STR("[MoriaCppMod] [PathX-F]   {}={} (bool @ +0x{:04x})\n"),
                                                         pn.c_str(), *base ? STR("true") : STR("false"), (unsigned)off);
                                                }
                                                else if (pcn == STR("IntProperty") && isReadableMemory(base, 4))
                                                {
                                                    VLOG(STR("[MoriaCppMod] [PathX-F]   {}={} (int @ +0x{:04x})\n"),
                                                         pn.c_str(), *reinterpret_cast<int32_t*>(base), (unsigned)off);
                                                }
                                                else if (pcn == STR("UInt32Property") && isReadableMemory(base, 4))
                                                {
                                                    VLOG(STR("[MoriaCppMod] [PathX-F]   {}=0x{:08x} (uint32 @ +0x{:04x})\n"),
                                                         pn.c_str(), *reinterpret_cast<uint32_t*>(base), (unsigned)off);
                                                }
                                                else if (pcn == STR("ObjectProperty") && isReadableMemory(base, 8))
                                                {
                                                    UObject* o = *reinterpret_cast<UObject**>(base);
                                                    VLOG(STR("[MoriaCppMod] [PathX-F]   {}={:p} (object @ +0x{:04x} cls={})\n"),
                                                         pn.c_str(), (void*)o, (unsigned)off,
                                                         (o && isObjectAlive(o)) ? safeClassName(o).c_str() : STR("null/dead"));
                                                }
                                                else
                                                {
                                                    VLOG(STR("[MoriaCppMod] [PathX-F]   {} [type={} @ +0x{:04x} — not printable]\n"),
                                                         pn.c_str(), pcn.c_str(), (unsigned)off);
                                                }
                                            }
                                        } catch (...) {}
                                        VLOG(STR("[MoriaCppMod] [PathX-F] === end {} dump ===\n"), phase);
                                    };
                                    dumpNpcCompState(STR("PRE-Register"));

                                    // [rc.42 GUID ADOPTION v2 2026-06-10] Walk NpcInfo for an
                                    // existing goat-shaped entry. Filter broadened to include
                                    // 'Porter' (set by assignPorterRole) and 'Wanderer' (legacy
                                    // from setGoatRole on Stay clicks) — rc.41's 'Default'-only
                                    // filter missed entries our own code had relabeled. Iterate
                                    // BACKWARDS so multi-entry saves adopt the most recent goat
                                    // (the one user actually used last).
                                    //
                                    // If adoption succeeds, SKIP the RegisterWithNPCManager call
                                    // entirely. This is THE fix for "never save more than 1 goat":
                                    // Register always appends, so the only way to avoid duplicates
                                    // is to not call it when the entry already exists.
                                    bool guidAdopted = false;
                                    {
                                        UObject* mgrAdopt = nullptr;
                                        std::vector<UObject*> mgrsAdopt;
                                        if (findAllOfSafe(STR("MorNPCManager"), mgrsAdopt))
                                        {
                                            for (UObject* o : mgrsAdopt)
                                            {
                                                if (!o || !isObjectAlive(o)) continue;
                                                std::wstring cn = safeClassName(o);
                                                if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                                                mgrAdopt = o; break;
                                            }
                                        }
                                        if (mgrAdopt && isObjectAlive(mgrAdopt))
                                        {
                                            uint8_t* niBase = reinterpret_cast<uint8_t*>(mgrAdopt) + 0x03a0;
                                            uint8_t* itemsHdr = niBase + 0x0108;
                                            if (isReadableMemory(itemsHdr, 16))
                                            {
                                                uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
                                                int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
                                                constexpr int kStride         = 0x260;
                                                constexpr int kGuidOff        = 0x001c;
                                                constexpr int kRoleRowNameOff = 0x0050;
                                                constexpr int kIsRescuedOff   = 0x01A0;
                                                if (itemsData && itemsNum > 0 && itemsNum < 500
                                                    && isReadableMemory(itemsData, itemsNum * kStride))
                                                {
                                                    // [rc.50 FILTER NAME 2026-06-14] Pivoted from
                                                    // Role=='Porter' to Name=='Rûdh' (m_goatName).
                                                    // Role drifted back to 'Default' across save/
                                                    // reload despite our SetRoleFuzzy call —
                                                    // empirically proven rc.48 T3.3. Name is more
                                                    // inert: no engine logic actively resets it.
                                                    // We write m_goatName via writeGoatNameToEntry
                                                    // at bell-spawn (rc.50). Filter reads the
                                                    // entry's Name FText (abs offset +0x030) and
                                                    // matches against m_goatName.
                                                    constexpr int kNameOffAdopt = 0x0030;
                                                    for (int i = itemsNum - 1; i >= 0; --i)
                                                    {
                                                        uint8_t* entry = itemsData + i * kStride;
                                                        wchar_t tmpName[256];
                                                        seh_ftextToStringToBuf(entry + kNameOffAdopt, tmpName, 256);
                                                        std::wstring nameStr = tmpName;
                                                        if (isGoatNameMatch(nameStr))  // [v8.2.x] tolerates mojibake variant
                                                        {
                                                            uint8_t adoptGuid[16];
                                                            std::memcpy(adoptGuid, entry + kGuidOff, 16);
                                                            uint32_t* g = reinterpret_cast<uint32_t*>(adoptGuid);
                                                            VLOG(STR("[MoriaCppMod] [GuidAdopt] FOUND existing goat-shape at NpcInfo[{}] (Name='{}'): GUID={:08X}-{:08X}-{:08X}-{:08X}\n"),
                                                                 i, nameStr.c_str(), g[0], g[1], g[2], g[3]);
                                                            uint8_t* myGuidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                                                            if (myGuidPtr)
                                                            {
                                                                std::memcpy(myGuidPtr, adoptGuid, 16);
                                                                guidAdopted = true;
                                                                m_lastSpawnGuidAdopted = true;  // [rc.45] for assignPorterRole gate at spawn tail
                                                                VLOG(STR("[MoriaCppMod] [GuidAdopt] Wrote 16 bytes into npcComp.NpcGuid; Register call will be SKIPPED to prevent duplicate entry\n"));
                                                            }
                                                            else
                                                            {
                                                                VLOG(STR("[MoriaCppMod] [GuidAdopt] NpcGuid property not found on npcComp — adoption SKIPPED, Register will run\n"));
                                                            }
                                                            break;  // most-recent match wins
                                                        }
                                                    }
                                                }
                                                if (!guidAdopted)
                                                    VLOG(STR("[MoriaCppMod] [GuidAdopt] No existing goat-shape (Name=='{}') found in {} NpcInfo entries; Register will mint fresh GUID + writeGoatNameToEntry will mark the new entry\n"), m_goatName.c_str(), itemsNum);
                                            }
                                        }
                                    }

                                    // [rc.61 PATH A 2026-06-28] Path A — skip
                                    // RegisterWithNPCManager entirely. The Register
                                    // call engages the dwarven NPC interaction UI
                                    // (helmet slots, "Standing By/Working" labels)
                                    // as a side effect — surfacing on the goat as
                                    // dwarf inventory. Per-character persistence
                                    // moves to a sidecar JSON keyed by save slot.
                                    if (guidAdopted)
                                    {
                                        VLOG(STR("[MoriaCppMod] [GoatRegister] SKIPPING RegisterWithNPCManager on npcComp={:p} (GUID adopted; existing entry will bind)\n"),
                                             (void*)npcComp);
                                    }
                                    else
                                    {
                                        VLOG(STR("[MoriaCppMod] [GoatRegister] SKIPPING RegisterWithNPCManager on npcComp={:p} — rc.61 Path A: no NpcInfo registration, sidecar JSON handles persistence\n"),
                                             (void*)npcComp);
                                    }
                                    (void)regFn;  // unused under Path A; retained for diagnostic dumps

                                    dumpNpcCompState(STR("POST-Register"));

                                    int32_t postNum = readNpcInfoItemsNum();
                                    VLOG(STR("[MoriaCppMod] [PathX-E] POST-Register NpcInfo.Items.Num={} (delta={})\n"),
                                         postNum, postNum - preNum);

                                    // [rc.32 PATH X PROBE G 2026-06-01] Walk all NpcInfo.Items
                                    // entries and log: GUID + curRow FName + intRow FName.
                                    // Per saved memory: stride=0x260, guid@+0x001c, curRow@+0x0218,
                                    // intRow@+0x0228. Goal: find any entry whose row name matches
                                    // goat/porter/fauna patterns — that's an existing NPC slot we
                                    // could potentially adopt instead of spawning a fresh goat.
                                    {
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
                                        if (mgr && isObjectAlive(mgr))
                                        {
                                            uint8_t* niBase = reinterpret_cast<uint8_t*>(mgr) + 0x03a0;
                                            uint8_t* itemsHdr = niBase + 0x0108;
                                            if (isReadableMemory(itemsHdr, 16))
                                            {
                                                uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
                                                int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
                                                constexpr int kStride = 0x260;
                                                constexpr int kGuidOff = 0x001c;
                                                constexpr int kCurRowOff = 0x0218;
                                                constexpr int kIntRowOff = 0x0228;
                                                VLOG(STR("[MoriaCppMod] [PathX-G] === walking {} NpcInfo entries (stride=0x{:x}) ===\n"), itemsNum, kStride);
                                                // [rc.35 PROBE J 2026-06-02] Per FMorNPCInfo struct (from
                                                // CXXHeaderDump/Moria.hpp:3868):
                                                //   PersistentData @ +0x0010 (FMorNpcPersistentData, 0x200 B)
                                                //     NpcGuid       @ +0x000C (within Persistent → abs +0x001C)
                                                //     Name          @ +0x0020 (FText, abs +0x0030)
                                                //     CurrentRole   @ +0x0038 (FMorNPCRoleRowHandle, abs +0x0048)
                                                //       DataTable*  @ +0x00 → abs +0x0048
                                                //       RowName     @ +0x08 → abs +0x0050
                                                //     bIsRescued    @ +0x0190 (abs +0x01A0)
                                                //     StaticNpcData @ +0x01B0 (FDataTableRowHandle, abs +0x01C0)
                                                //       DataTable*  @ +0x00 → abs +0x01C0
                                                //       RowName     @ +0x08 → abs +0x01C8
                                                constexpr int kRoleRowNameOff   = 0x0050;
                                                constexpr int kStaticRowNameOff = 0x01C8;
                                                constexpr int kIsRescuedOff     = 0x01A0;
                                                int goatLike = 0;
                                                if (itemsData && itemsNum > 0 && itemsNum < 200 && isReadableMemory(itemsData, itemsNum * kStride))
                                                {
                                                    for (int i = 0; i < itemsNum; ++i)
                                                    {
                                                        uint8_t* entry = itemsData + i * kStride;
                                                        uint32_t* g = reinterpret_cast<uint32_t*>(entry + kGuidOff);
                                                        wchar_t tmpRole[256], tmpStatic[256];
                                                        seh_fnameToStringToBuf(entry + kRoleRowNameOff,   tmpRole,   256);
                                                        seh_fnameToStringToBuf(entry + kStaticRowNameOff, tmpStatic, 256);
                                                        std::wstring roleStr  = tmpRole;
                                                        std::wstring staticStr = tmpStatic;
                                                        bool isRescued = false;
                                                        if (isReadableMemory(entry + kIsRescuedOff, 1))
                                                            isRescued = *(entry + kIsRescuedOff) != 0;
                                                        bool matchGoat =
                                                            roleStr.find(STR("Goat"))   != std::wstring::npos ||
                                                            roleStr.find(STR("Porter")) != std::wstring::npos ||
                                                            roleStr.find(STR("Fauna"))  != std::wstring::npos ||
                                                            staticStr.find(STR("Goat"))   != std::wstring::npos ||
                                                            staticStr.find(STR("Porter")) != std::wstring::npos ||
                                                            staticStr.find(STR("Fauna"))  != std::wstring::npos;
                                                        // Always log last 10 + any goat-like + first 3.
                                                        bool dump = matchGoat || i < 3 || i >= itemsNum - 10;
                                                        if (dump)
                                                        {
                                                            VLOG(STR("[MoriaCppMod] [PathX-J]   [{}] GUID={:08X}-{:08X}-{:08X}-{:08X} Role='{}' StaticData='{}' Rescued={}{}\n"),
                                                                 i, g[0], g[1], g[2], g[3], roleStr.c_str(), staticStr.c_str(),
                                                                 isRescued ? STR("Y") : STR("n"),
                                                                 matchGoat ? STR(" *** GOAT-LIKE ***") : STR(""));
                                                            if (matchGoat) ++goatLike;
                                                        }
                                                    }
                                                }
                                                VLOG(STR("[MoriaCppMod] [PathX-J] === end — {} goat-like entries found across all {} entries ===\n"), goatLike, itemsNum);
                                            }
                                            else
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-G] NpcInfo.Items header unreadable\n"));
                                            }
                                        }
                                        else
                                        {
                                            VLOG(STR("[MoriaCppMod] [PathX-G] MorNPCManager singleton not findable\n"));
                                        }
                                    }

                                    // [rc.32 PATH X PROBE H 2026-06-01] Enumerate UFunctions on
                                    // MorAIPopulationManager and friends. Saved memory says the
                                    // C++ SpawnNpc path lives there. If any BP-callable variant
                                    // exists (e.g., RequestSpawn, SpawnAtLocation), we can route
                                    // the goat spawn through it for proper registration.
                                    {
                                        const wchar_t* spawnClsCands[] = {
                                            STR("MorAIPopulationManager"),
                                            STR("MorAILair"),
                                            STR("MorAILairBase"),
                                            STR("MorAIPatrolManager"),
                                            STR("MorAISpawnManager"),
                                            STR("MorNPCManager"),
                                            nullptr
                                        };
                                        for (const wchar_t** cn = spawnClsCands; *cn; ++cn)
                                        {
                                            std::vector<UObject*> cands;
                                            if (!findAllOfSafe(*cn, cands)) continue;
                                            UObject* inst = nullptr;
                                            for (UObject* o : cands)
                                            {
                                                if (!o || !isObjectAlive(o)) continue;
                                                std::wstring oc = safeClassName(o);
                                                if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                                                inst = o; break;
                                            }
                                            if (!inst) continue;
                                            UClass* iCls = nullptr;
                                            try { iCls = inst->GetClassPrivate(); } catch (...) {}
                                            if (!iCls) continue;
                                            VLOG(STR("[MoriaCppMod] [PathX-H] === UFunctions on {} (inst={:p}) ===\n"), *cn, (void*)inst);
                                            int hits = 0;
                                            try {
                                                for (auto* fn : iCls->ForEachFunctionInChain())
                                                {
                                                    if (!fn) continue;
                                                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                                    bool m =
                                                        fname.find(STR("Spawn"))   != std::wstring::npos ||
                                                        fname.find(STR("Create"))  != std::wstring::npos ||
                                                        fname.find(STR("Request")) != std::wstring::npos ||
                                                        fname.find(STR("Add"))     != std::wstring::npos ||
                                                        fname.find(STR("Npc"))     != std::wstring::npos ||
                                                        fname.find(STR("NPC"))     != std::wstring::npos;
                                                    if (!m) continue;
                                                    int parmsSize = 0;
                                                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                                    VLOG(STR("[MoriaCppMod] [PathX-H]   {} parmsSize={}\n"), fname.c_str(), parmsSize);
                                                    ++hits;
                                                }
                                            } catch (...) {}
                                            VLOG(STR("[MoriaCppMod] [PathX-H] === end {} ({} hits) ===\n"), *cn, hits);
                                        }
                                    }

                                    // [rc.33 PATH X PROBE I 2026-06-02] Dump BP_RequestSpawn
                                    // parm list with name/type/offset/size. parmsSize=105 — we
                                    // need the parm schema to craft a proper call (path B).
                                    {
                                        std::vector<UObject*> spawnMgrs;
                                        UObject* spawnMgr = nullptr;
                                        if (findAllOfSafe(STR("MorAISpawnManager"), spawnMgrs))
                                        {
                                            for (UObject* o : spawnMgrs)
                                            {
                                                if (!o || !isObjectAlive(o)) continue;
                                                std::wstring oc = safeClassName(o);
                                                if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                                                spawnMgr = o; break;
                                            }
                                        }
                                        if (spawnMgr)
                                        {
                                            auto* reqSpawnFn = spawnMgr->GetFunctionByNameInChain(STR("BP_RequestSpawn"));
                                            if (reqSpawnFn)
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-I] === BP_RequestSpawn parm schema ===\n"));
                                                int pi = 0;
                                                try {
                                                    for (auto* p : reqSpawnFn->ForEachProperty())
                                                    {
                                                        if (!p) continue;
                                                        uint64_t pflags = 0;
                                                        try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                                                        bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                                                        if (!isParm) continue;
                                                        std::wstring pn; try { pn = p->GetName(); } catch (...) {}
                                                        std::wstring pcn; try { pcn = p->GetClass().GetName(); } catch (...) {}
                                                        int off = -1; try { off = p->GetOffset_Internal(); } catch (...) {}
                                                        int sz = -1; try { sz = p->GetSize(); } catch (...) {}
                                                        bool isRet = (pflags & 0x0000000000000400ULL) != 0;
                                                        bool isOut = (pflags & 0x0000000000000100ULL) != 0;
                                                        VLOG(STR("[MoriaCppMod] [PathX-I]   parm[{}] '{}' type={} off=0x{:04x} size={} ret={} out={} flags=0x{:016x}\n"),
                                                             pi, pn.c_str(), pcn.c_str(), (unsigned)off, sz,
                                                             isRet ? STR("Y") : STR("n"),
                                                             isOut ? STR("Y") : STR("n"),
                                                             pflags);
                                                        ++pi;
                                                    }
                                                } catch (...) {}
                                                VLOG(STR("[MoriaCppMod] [PathX-I] === end ({} parms) ===\n"), pi);
                                            }
                                            else
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-I] BP_RequestSpawn UFunction not found on MorAISpawnManager\n"));
                                            }
                                        }
                                        else
                                        {
                                            VLOG(STR("[MoriaCppMod] [PathX-I] MorAISpawnManager singleton not findable\n"));
                                        }
                                    }

                                    // [rc.37 PROBE K — Desktop Claude Hypothesis B 2026-06-06]
                                    // Now that registration works (delta=+1 + real NpcGuid),
                                    // search MorNPCManager for UFunctions that might do "open
                                    // the manage screen for this NPC properly". DC's hypothesis:
                                    // there may be a manager-side function (Open*/Manage*/Show*/
                                    // Begin*/Recruit*) that takes an NpcGuid or NPC component
                                    // and routes UI binding through the manager — vs the BndEvt
                                    // path which defaults to player body inventory.
                                    {
                                        UObject* npcMgr = nullptr;
                                        std::vector<UObject*> mgrs2;
                                        if (findAllOfSafe(STR("MorNPCManager"), mgrs2))
                                        {
                                            for (UObject* o : mgrs2)
                                            {
                                                if (!o || !isObjectAlive(o)) continue;
                                                std::wstring oc = safeClassName(o);
                                                if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                                                npcMgr = o; break;
                                            }
                                        }
                                        if (npcMgr)
                                        {
                                            UClass* mc = nullptr;
                                            try { mc = npcMgr->GetClassPrivate(); } catch (...) {}
                                            if (mc)
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-K] === MorNPCManager UFunctions matching Open/Manage/Show/Begin/Interact/Screen ===\n"));
                                                int hits = 0;
                                                try {
                                                    for (auto* fn : mc->ForEachFunctionInChain())
                                                    {
                                                        if (!fn) continue;
                                                        std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                                        bool m =
                                                            fname.find(STR("Open"))     != std::wstring::npos ||
                                                            fname.find(STR("Manage"))   != std::wstring::npos ||
                                                            fname.find(STR("Show"))     != std::wstring::npos ||
                                                            fname.find(STR("Begin"))    != std::wstring::npos ||
                                                            fname.find(STR("Interact")) != std::wstring::npos ||
                                                            fname.find(STR("Screen"))   != std::wstring::npos ||
                                                            fname.find(STR("Display"))  != std::wstring::npos;
                                                        if (!m) continue;
                                                        int parmsSize = 0;
                                                        try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                                        VLOG(STR("[MoriaCppMod] [PathX-K]   {} parmsSize={}\n"), fname.c_str(), parmsSize);
                                                        // For each match, dump parm details so we know the signature.
                                                        try {
                                                            int pi = 0;
                                                            for (auto* p : fn->ForEachProperty())
                                                            {
                                                                if (!p) continue;
                                                                uint64_t pflags = 0;
                                                                try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                                                                bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                                                                if (!isParm) continue;
                                                                std::wstring pname; try { pname = p->GetName(); } catch (...) {}
                                                                std::wstring ptype; try { ptype = p->GetClass().GetName(); } catch (...) {}
                                                                int psz = -1; try { psz = p->GetSize(); } catch (...) {}
                                                                VLOG(STR("[MoriaCppMod] [PathX-K]     parm[{}] '{}' type={} size={}\n"),
                                                                     pi, pname.c_str(), ptype.c_str(), psz);
                                                                ++pi;
                                                            }
                                                        } catch (...) {}
                                                        ++hits;
                                                    }
                                                } catch (...) {}
                                                VLOG(STR("[MoriaCppMod] [PathX-K] === end ({} hits) ===\n"), hits);
                                            }
                                        }
                                        else
                                        {
                                            VLOG(STR("[MoriaCppMod] [PathX-K] MorNPCManager singleton not found\n"));
                                        }
                                        // Also probe MorPlayerController for similar shapes (DC's
                                        // earlier disasm showed the manage flow may originate from PC).
                                        if (m_localPC && isObjectAlive(m_localPC))
                                        {
                                            UClass* pcCls = nullptr;
                                            try { pcCls = m_localPC->GetClassPrivate(); } catch (...) {}
                                            if (pcCls)
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-K] === MorPlayerController UFunctions matching Open/Manage/Show/Begin/Interact/Screen ===\n"));
                                                int hits = 0;
                                                try {
                                                    for (auto* fn : pcCls->ForEachFunctionInChain())
                                                    {
                                                        if (!fn) continue;
                                                        std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                                        bool m =
                                                            fname.find(STR("Open"))     != std::wstring::npos ||
                                                            fname.find(STR("Manage"))   != std::wstring::npos ||
                                                            fname.find(STR("Show"))     != std::wstring::npos ||
                                                            fname.find(STR("Begin"))    != std::wstring::npos ||
                                                            fname.find(STR("Interact")) != std::wstring::npos ||
                                                            fname.find(STR("Screen"))   != std::wstring::npos ||
                                                            fname.find(STR("Display"))  != std::wstring::npos;
                                                        if (!m) continue;
                                                        // Skip noise — only manage-shaped functions
                                                        if (fname.find(STR("CinematicMode")) != std::wstring::npos ||
                                                            fname.find(STR("MovieMode"))     != std::wstring::npos ||
                                                            fname.find(STR("ConsoleCommand")) != std::wstring::npos)
                                                            continue;
                                                        int parmsSize = 0;
                                                        try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                                        VLOG(STR("[MoriaCppMod] [PathX-K]   PC.{} parmsSize={}\n"), fname.c_str(), parmsSize);
                                                        ++hits;
                                                        if (hits >= 40) break;  // cap output
                                                    }
                                                } catch (...) {}
                                                VLOG(STR("[MoriaCppMod] [PathX-K] === end PC ({} hits) ===\n"), hits);
                                            }
                                        }
                                    }

                                    // [rc.38 PROBE L — empirical UIManager discovery 2026-06-08]
                                    // Probe K found CloseUIScreen() on PC but no Open* counterpart on
                                    // either PC or MorNPCManager. The Open* function must live on
                                    // whatever object PC delegates UI management to. Two strategies:
                                    //   1) Walk PC's property chain for ObjectProperties whose name
                                    //      contains UI/Manager/Screen/HUD/Viewport. Dereference each
                                    //      and enumerate UFunctions on the target's class.
                                    //   2) Class-name shortlist fallback: try a handful of likely
                                    //      class names directly via findAllOfSafe.
                                    {
                                        if (m_localPC && isObjectAlive(m_localPC))
                                        {
                                            UClass* pcCls2 = nullptr;
                                            try { pcCls2 = m_localPC->GetClassPrivate(); } catch (...) {}
                                            if (pcCls2)
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-L] === PC property chain — UI-related ObjectProperties ===\n"));
                                                int candidates = 0;
                                                try {
                                                    for (auto* p : pcCls2->ForEachProperty())
                                                    {
                                                        if (!p) continue;
                                                        std::wstring pname; try { pname = p->GetName(); } catch (...) { continue; }
                                                        std::wstring pcls; try { pcls = p->GetClass().GetName(); } catch (...) { continue; }
                                                        bool isObj =
                                                            pcls == STR("ObjectProperty") ||
                                                            pcls == STR("WeakObjectProperty") ||
                                                            pcls == STR("SoftObjectProperty");
                                                        if (!isObj) continue;
                                                        bool nameHit =
                                                            pname.find(STR("UI"))       != std::wstring::npos ||
                                                            pname.find(STR("Manager"))  != std::wstring::npos ||
                                                            pname.find(STR("Screen"))   != std::wstring::npos ||
                                                            pname.find(STR("HUD"))      != std::wstring::npos ||
                                                            pname.find(STR("Viewport")) != std::wstring::npos;
                                                        if (!nameHit) continue;

                                                        int32_t off = -1;
                                                        try { off = p->GetOffset_Internal(); } catch (...) {}
                                                        UObject* target = nullptr;
                                                        try {
                                                            uint8_t* base = reinterpret_cast<uint8_t*>(m_localPC);
                                                            if (off >= 0)
                                                                target = *reinterpret_cast<UObject**>(base + off);
                                                        } catch (...) {}

                                                        std::wstring tcls = target ? safeClassName(target) : STR("(null)");
                                                        VLOG(STR("[MoriaCppMod] [PathX-L]   PC.{} type={} off=0x{:X} target={:p} cls={}\n"),
                                                             pname.c_str(), pcls.c_str(), (uint32_t)off, (void*)target, tcls.c_str());
                                                        ++candidates;

                                                        if (!target || !isObjectAlive(target)) continue;
                                                        UClass* tCls = nullptr;
                                                        try { tCls = target->GetClassPrivate(); } catch (...) {}
                                                        if (!tCls) continue;

                                                        VLOG(STR("[MoriaCppMod] [PathX-L]     -- UFunctions on {} matching filter --\n"), tcls.c_str());
                                                        int fh = 0;
                                                        try {
                                                            for (auto* fn : tCls->ForEachFunctionInChain())
                                                            {
                                                                if (!fn) continue;
                                                                std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                                                bool m =
                                                                    fname.find(STR("Open"))     != std::wstring::npos ||
                                                                    fname.find(STR("Manage"))   != std::wstring::npos ||
                                                                    fname.find(STR("Show"))     != std::wstring::npos ||
                                                                    fname.find(STR("Begin"))    != std::wstring::npos ||
                                                                    fname.find(STR("Interact")) != std::wstring::npos ||
                                                                    fname.find(STR("Screen"))   != std::wstring::npos ||
                                                                    fname.find(STR("Display"))  != std::wstring::npos ||
                                                                    fname.find(STR("Push"))     != std::wstring::npos ||
                                                                    fname.find(STR("Bind"))     != std::wstring::npos;
                                                                if (!m) continue;
                                                                int parmsSize = 0;
                                                                try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                                                VLOG(STR("[MoriaCppMod] [PathX-L]       {} parmsSize={}\n"), fname.c_str(), parmsSize);
                                                                try {
                                                                    int pi = 0;
                                                                    for (auto* fp : fn->ForEachProperty())
                                                                    {
                                                                        if (!fp) continue;
                                                                        uint64_t pflags = 0;
                                                                        try { pflags = static_cast<uint64_t>(fp->GetPropertyFlags()); } catch (...) {}
                                                                        bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                                                                        if (!isParm) continue;
                                                                        std::wstring fpname; try { fpname = fp->GetName(); } catch (...) {}
                                                                        std::wstring fptype; try { fptype = fp->GetClass().GetName(); } catch (...) {}
                                                                        int psz = -1; try { psz = fp->GetSize(); } catch (...) {}
                                                                        VLOG(STR("[MoriaCppMod] [PathX-L]         parm[{}] '{}' type={} size={}\n"),
                                                                             pi, fpname.c_str(), fptype.c_str(), psz);
                                                                        ++pi;
                                                                    }
                                                                } catch (...) {}
                                                                ++fh;
                                                                if (fh >= 30) break;
                                                            }
                                                        } catch (...) {}
                                                        VLOG(STR("[MoriaCppMod] [PathX-L]     -- end ({} hits) --\n"), fh);
                                                    }
                                                } catch (...) {}
                                                VLOG(STR("[MoriaCppMod] [PathX-L] === end PC chain ({} UI-candidates) ===\n"), candidates);
                                            }
                                        }

                                        // Class-name shortlist fallback
                                        static const wchar_t* candidateClasses[] = {
                                            STR("MorUIManager"),
                                            STR("MorHUD"),
                                            STR("FGKUIScreenManager"),
                                            STR("FGKUIManager"),
                                            STR("MorGameViewportClient"),
                                            STR("MorViewportClient"),
                                            STR("FGKHUDComponent"),
                                        };
                                        VLOG(STR("[MoriaCppMod] [PathX-L] === Class-name shortlist probe ===\n"));
                                        for (const wchar_t* cname : candidateClasses)
                                        {
                                            std::vector<UObject*> hits2;
                                            if (!findAllOfSafe(cname, hits2))
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-L]   {} -> findAllOf failed\n"), cname);
                                                continue;
                                            }
                                            UObject* live = nullptr;
                                            for (UObject* o : hits2)
                                            {
                                                if (!o || !isObjectAlive(o)) continue;
                                                std::wstring oc = safeClassName(o);
                                                if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                                                live = o; break;
                                            }
                                            if (!live)
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathX-L]   {} -> not found (or only CDOs)\n"), cname);
                                                continue;
                                            }
                                            std::wstring lc = safeClassName(live);
                                            VLOG(STR("[MoriaCppMod] [PathX-L]   {} -> live instance {:p} cls={}\n"),
                                                 cname, (void*)live, lc.c_str());
                                            UClass* lcCls = nullptr;
                                            try { lcCls = live->GetClassPrivate(); } catch (...) {}
                                            if (!lcCls) continue;
                                            int fh = 0;
                                            try {
                                                for (auto* fn : lcCls->ForEachFunctionInChain())
                                                {
                                                    if (!fn) continue;
                                                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                                    bool m =
                                                        fname.find(STR("Open"))     != std::wstring::npos ||
                                                        fname.find(STR("Manage"))   != std::wstring::npos ||
                                                        fname.find(STR("Show"))     != std::wstring::npos ||
                                                        fname.find(STR("Begin"))    != std::wstring::npos ||
                                                        fname.find(STR("Interact")) != std::wstring::npos ||
                                                        fname.find(STR("Screen"))   != std::wstring::npos ||
                                                        fname.find(STR("Display"))  != std::wstring::npos ||
                                                        fname.find(STR("Push"))     != std::wstring::npos ||
                                                        fname.find(STR("Bind"))     != std::wstring::npos;
                                                    if (!m) continue;
                                                    int parmsSize = 0;
                                                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                                    VLOG(STR("[MoriaCppMod] [PathX-L]     {}.{} parmsSize={}\n"),
                                                         cname, fname.c_str(), parmsSize);
                                                    ++fh;
                                                    if (fh >= 30) break;
                                                }
                                            } catch (...) {}
                                            VLOG(STR("[MoriaCppMod] [PathX-L]   -- {} end ({} hits) --\n"), cname, fh);
                                        }
                                        VLOG(STR("[MoriaCppMod] [PathX-L] === end class shortlist ===\n"));
                                    }

                                    // [rc.33 PATH A — GHOST GOAT ADOPT 2026-06-02] If our register
                                    // failed (NpcGuid stayed 0), adopt the GUID of a known prior-
                                    // session goat that's still in NpcInfo. Entry [79]'s GUID
                                    // 3720658F-4C7E9C03-B14A938E-1B09119E was our goat from a
                                    // prior session and the entry survived in NpcInfo. By
                                    // writing that GUID to our newly-spawned goat, the engine's
                                    // FMorNPCInfoArray::Find lookup will resolve.
                                    //
                                    // Fallback: if 3720658F isn't found, adopt the LAST entry's
                                    // GUID (likely the most recent goat from prior sessions).
                                    {
                                        // Re-read our goat's NpcGuid; only adopt if still zero.
                                        uint8_t* myGuidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                                        if (myGuidPtr)
                                        {
                                            uint32_t* mg = reinterpret_cast<uint32_t*>(myGuidPtr);
                                            bool stillZero = (mg[0] | mg[1] | mg[2] | mg[3]) == 0;
                                            if (stillZero)
                                            {
                                                // Find NpcInfo manager again (cheap).
                                                UObject* mgrAdopt = nullptr;
                                                std::vector<UObject*> mgrsAdopt;
                                                if (findAllOfSafe(STR("MorNPCManager"), mgrsAdopt))
                                                {
                                                    for (UObject* o : mgrsAdopt)
                                                    {
                                                        if (!o || !isObjectAlive(o)) continue;
                                                        std::wstring oc = safeClassName(o);
                                                        if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                                                        mgrAdopt = o; break;
                                                    }
                                                }
                                                if (mgrAdopt && isObjectAlive(mgrAdopt))
                                                {
                                                    uint8_t* itemsHdr = reinterpret_cast<uint8_t*>(mgrAdopt) + 0x03a0 + 0x0108;
                                                    if (isReadableMemory(itemsHdr, 16))
                                                    {
                                                        uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
                                                        int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
                                                        constexpr int kStride = 0x260;
                                                        constexpr int kGuidOff = 0x001c;
                                                        // Target: GUID 3720658F-4C7E9C03-B14A938E-1B09119E
                                                        constexpr uint32_t kTargetG0 = 0x3720658Fu;
                                                        constexpr uint32_t kTargetG1 = 0x4C7E9C03u;
                                                        constexpr uint32_t kTargetG2 = 0xB14A938Eu;
                                                        constexpr uint32_t kTargetG3 = 0x1B09119Eu;
                                                        int adoptIdx = -1;
                                                        uint32_t adoptG[4] = {0,0,0,0};
                                                        if (itemsData && itemsNum > 0 && itemsNum < 200 && isReadableMemory(itemsData, itemsNum * kStride))
                                                        {
                                                            // Prefer matching target GUID; fallback to last entry.
                                                            for (int i = 0; i < itemsNum; ++i)
                                                            {
                                                                uint32_t* g = reinterpret_cast<uint32_t*>(itemsData + i * kStride + kGuidOff);
                                                                if (g[0] == kTargetG0 && g[1] == kTargetG1 && g[2] == kTargetG2 && g[3] == kTargetG3)
                                                                {
                                                                    adoptIdx = i;
                                                                    adoptG[0] = g[0]; adoptG[1] = g[1]; adoptG[2] = g[2]; adoptG[3] = g[3];
                                                                    break;
                                                                }
                                                            }
                                                            if (adoptIdx < 0 && itemsNum > 0)
                                                            {
                                                                // Fallback: last entry.
                                                                int i = itemsNum - 1;
                                                                uint32_t* g = reinterpret_cast<uint32_t*>(itemsData + i * kStride + kGuidOff);
                                                                adoptIdx = i;
                                                                adoptG[0] = g[0]; adoptG[1] = g[1]; adoptG[2] = g[2]; adoptG[3] = g[3];
                                                            }
                                                        }
                                                        if (adoptIdx >= 0)
                                                        {
                                                            // [rc.35 SUSPENDED 2026-06-02] Adoption disabled
                                                            // while we identify whether those tail entries
                                                            // are actually goats or recruited dwarves
                                                            // (Probe J dumps Role + StaticNpcData FNames).
                                                            // The "helmets in bag" symptom suggests we
                                                            // adopted a dwarf's GUID and the engine bound
                                                            // UI to the dwarf's body inventory.
                                                            VLOG(STR("[MoriaCppMod] [PathA-Adopt] WOULD adopt from NpcInfo[{}] GUID={:08X}-{:08X}-{:08X}-{:08X} — SUSPENDED pending Probe J row identification\n"),
                                                                 adoptIdx, adoptG[0], adoptG[1], adoptG[2], adoptG[3]);
                                                        }
                                                        else
                                                        {
                                                            VLOG(STR("[MoriaCppMod] [PathA-Adopt] no adoptable entry found in NpcInfo (itemsNum={})\n"), itemsNum);
                                                        }
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                VLOG(STR("[MoriaCppMod] [PathA-Adopt] NpcGuid already non-zero — skip adoption\n"));
                                            }
                                        }
                                    }

                                    // Arm a +1s deferred snapshot in case OnRep_NpcInfo replication
                                    // is async and the count grows on the next replication tick.
                                    m_pathXDelayedProbeMs = GetTickCount64() + 1000;
                                    VLOG(STR("[MoriaCppMod] [PathX-E] armed deferred snapshot at +1000ms\n"));

                                    // Re-read NpcGuid AFTER registration — engine should have
                                    // assigned a manager-issued guid.
                                    uint8_t* postPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                                    if (postPtr)
                                    {
                                        uint32_t* gp = reinterpret_cast<uint32_t*>(postPtr);
                                        bool postZero = (gp[0] | gp[1] | gp[2] | gp[3]) == 0;
                                        VLOG(STR("[MoriaCppMod] [GoatRegister] POST-REG NpcGuid={:08X}-{:08X}-{:08X}-{:08X} (zero={})\n"),
                                             gp[0], gp[1], gp[2], gp[3], postZero);
                                    }
                                }
                                else
                                {
                                    VLOG(STR("[MoriaCppMod] [GoatRegister] RegisterWithNPCManager UFunction missing on npcComp — falling back to synthesis\n"));
                                }

                                // [rc.12g 2026-05-24] NpcGuid synthesis.
                                // The goat spawns with NpcGuid=0 because we use SpawnActorFromClass
                                // instead of AMorNPCManager.SpawnNpc (which is the path that
                                // assigns guids for ValidNpcClasses-registered NPCs). Containers
                                // in Moria refuse modifications when their owner-key is zero,
                                // which matches the "uninitialized / red X / can't add or take"
                                // symptom in rc.12f Manage UI tests 2/3/5.
                                //
                                // Fix: write a synthesized non-zero 16-byte FGuid into
                                // NpcGuid via reflection. Engine doesn't care if the value came
                                // from the manager or from us — it just needs a non-zero stable
                                // key to bind container handles to a persistent identity.
                                //
                                // Synthesis: byte 0-7 from goat actor pointer (stable per
                                // instance for the session, unique per spawned goat); byte 8-15
                                // = "GOATCPP*" magic + session counter (ensures non-zero +
                                // distinguishes our synthesized guids from manager-issued ones).
                                {
                                    uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                                    if (guidPtr)
                                    {
                                        uint32_t* g = reinterpret_cast<uint32_t*>(guidPtr);
                                        bool isZero = (g[0] | g[1] | g[2] | g[3]) == 0;
                                        // [rc.30 2026-05-29] GUID synthesis DISABLED.
                                        // User correctly identified that the synthesized
                                        // 'GOATCPP' magic GUID causes downstream
                                        //   LogMoriaNPC: Error: [FMorNPCInfoArray::Find] ID ... not found!
                                        // errors because the engine looks up our fake GUID
                                        // in NpcInfo and finds nothing. If RegisterWithNPCManager
                                        // didn't assign a GUID, the goat genuinely isn't
                                        // registered — pretending it is just causes errors
                                        // downstream. Leave the GUID at 0; let the engine
                                        // know this goat is non-registered.
                                        VLOG(STR("[MoriaCppMod] [GoatGuid] NpcGuid={:08X}-{:08X}-{:08X}-{:08X} (zero={}; synthesis DISABLED — would cause FMorNPCInfoArray::Find errors)\n"),
                                             g[0], g[1], g[2], g[3], isZero);
                                    }
                                    else
                                    {
                                        VLOG(STR("[MoriaCppMod] [GoatGuid] NpcGuid property not found on npcComp\n"));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // [rc.13 2026-05-24] NpcMgr probe — Desktop Claude's Path D
            // hypothesis test. Enumerates UMorNPCManager properties +
            // UFunctions, appends BP_NpcGoat_C to ValidNpcClasses, calls
            // candidate registration UFunctions. One-shot per session.
            runNpcMgrProbe(goat);

            // [rc.14.1 2026-05-24 ROLLBACK] Settlement assignment removed.
            // The runtime probe (rc.13) revealed:
            //   - ValidNpcClasses is full (12/12) — no append room
            //   - No callable RegisterNpc UFunction on BP_NPCManager_C
            //   - rc.12h's RegisterWithNPCManager likely early-exits on the
            //     internal ValidNpcClasses check, never adding to NpcInfo
            // Settlement-membership assignment would also gate on
            // ValidNpcClasses, so it's a no-op AND potentially teleports
            // the goat to settlement waypoint as a side effect. The real
            // fix is a pak edit that adds BP_NpcGoat_C to ValidNpcClasses
            // at the BP_NPCManager CDO level — pending Desktop Claude.
            // Keeping the assignment code in #if 0 for revival when the
            // pak edit lands.
#if 0
            {
                UClass* settlNpcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
                UObject* settlNpcComp = nullptr;
                if (settlNpcCompCls)
                {
                    if (auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass")))
                    {
                        std::vector<uint8_t> gbuf(getCompFn->GetParmsSize(), 0);
                        writeGoatParm<UClass*>(getCompFn, gbuf.data(), STR("ComponentClass"), settlNpcCompCls);
                        if (safeProcessEvent(goat, getCompFn, gbuf.data()))
                        {
                            settlNpcComp = readGoatParm<UObject*>(getCompFn, gbuf.data(), STR("ReturnValue"), nullptr);
                        }
                    }
                }
                if (settlNpcComp && isObjectAlive(settlNpcComp))
                {
                    VLOG(STR("[MoriaCppMod] [GoatSettlement] dispatching assignGoatToSettlement (waypoint=0) on npcComp={:p}\n"),
                         (void*)settlNpcComp);
                    assignGoatToSettlement(settlNpcComp, /*waypointId=*/0);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [GoatSettlement] npcComp re-resolve failed — skipped settlement assignment\n"));
                }
            }
#endif

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

            // [rc.48 ROLE + WORLDSTATE 2026-06-14]
            //
            // CRITICAL ARCHITECTURAL CHANGE per DC's Q-answers:
            //   - SetRoleFuzzy("Porter") is now ALWAYS called (fresh +
            //     adopt). This makes Role='Porter' the unique goat
            //     marker for next session's adoption filter, fixing
            //     the dwarf-hijack bug discovered 2026-06-14.
            //   - StoreRuntimeActor(goat) is queued for next tick (per
            //     DC Q3: post-FinishSpawning + frame delay for
            //     ConstructionScript+BeginPlay completion). This is the
            //     missing piece for actor + container + items
            //     persistence across save/reload.
            //   - For FRESH spawn only: assignPorterRole still fires
            //     (Register + SetIsInteractive + SetRoleFuzzy bundle).
            //     For ADOPT: SKIP assignPorterRole (preserves rc.45
            //     "no duplicate entry" invariant — empirically proven
            //     across rc.42-rc.46).

            if (m_lastSpawnGuidAdopted)
            {
                VLOG(STR("[MoriaCppMod] [BellSpawn] SKIP assignPorterRole — goat was adopted (rc.45 invariant: empirical evidence shows Register would duplicate the NpcInfo entry)\n"));
            }
            else if (!assignPorterRole(goat))
            {
                VLOG(STR("[MoriaCppMod] [BellSpawn] assignPorterRole returned false — porter BT pipeline NOT active for this goat\n"));
            }

            // [rc.48] Role='Porter' kept as cosmetic. The Role marker is
            // proven to drift back to 'Default' across save/reload — it's
            // unreliable as a filter. We keep the call for vanilla menu
            // header display ("Porter Goat") only.
            setGoatRoleFuzzy_Porter_Standalone(goat);

            // [rc.51 NAME MARKER — DIRECT WRITE 2026-06-14] Probe T (rc.50)
            // proved no name-setter UFunction exists on BP_NpcGoat_C or
            // its inheritance chain. Pivoting to direct memory write of
            // FText into NpcInfo entry +0x030. Need the goat's NpcGuid
            // first — get it via reflection from the npcComp.
            //
            // Note: we attempt this in BOTH fresh-spawn and adopt paths.
            // For adopt, the Name should already be present from a prior
            // session, but rewriting is idempotent. For fresh, this is
            // the only chance to install the marker.
            {
                UObject* npcCompForName = nullptr;
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
                            npcCompForName = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                        }
                    }
                }
                if (!npcCompForName || !isObjectAlive(npcCompForName))
                {
                    VLOG(STR("[MoriaCppMod] [BellSpawn] [NameMarker] npcComp not findable — name marker write SKIPPED\n"));
                }
                else
                {
                    uint8_t* myGuidPtr = npcCompForName->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                    if (!myGuidPtr)
                    {
                        VLOG(STR("[MoriaCppMod] [BellSpawn] [NameMarker] NpcGuid property not findable — name marker write SKIPPED\n"));
                    }
                    else
                    {
                        if (writeGoatNameDirectToNpcInfoEntry(myGuidPtr, m_goatName))
                        {
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [NameMarker] Name='{}' written to NpcInfo entry — adoption filter will find this goat on next bell-ring\n"),
                                 m_goatName.c_str());
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [NameMarker] writeGoatNameDirectToNpcInfoEntry returned false — name marker NOT installed\n"));
                        }

                        // [rc.85 UNIQUENPC ROWNAME FIX 2026-07-06] Write
                        // UniqueNpc.RowName='NPCGoat' — Tobi's v1.10.0
                        // DT_NPCUniqueCharacters row is 'NPCGoat' (CharacterName
                        // 'Rûdh', CharacterClass=BP_NpcGoat_C). The old 'Goat'
                        // value never matched his table, so the manager's reload
                        // lookup (§7a: UniqueNpc.RowName -> DT_NPCUniqueCharacters
                        // -> CharacterClass -> SpawnActor) silently failed and the
                        // goat never restored. This makes the restore path resolve.
                        if (writeUniqueNpcRowNameToEntry(myGuidPtr, STR("NPCGoat")))
                        {
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [UniqueNpcWrite] UniqueNpc.RowName='NPCGoat' written — restore lookup will hit DT_NPCUniqueCharacters['NPCGoat'] on reload\n"));
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [UniqueNpcWrite] writeUniqueNpcRowNameToEntry returned false — restore lookup will fail\n"));
                        }
                    }
                }
            }

            // [rc.53 STAGE 1 ID-SET 2026-06-15] Per save-system-architecture.md:
            // canonical persistence is via IMorSaveGameObject, keyed by
            // FMorSaveGameObjectId. If our goat's Id is zero/empty (which it
            // probably is, since UE4SS spawn bypasses the building system's
            // native registration), the save system can't track it. Call
            // UMorSaveSystemBlueprintLibrary::CreatePersistentSaveGameObjectId
            // to mint a fresh Id, then SaveGameObjectSetId on the goat.
            // Probe U will compare resulting state against a chest.
            try {
                UClass* bpLibCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorSaveSystemBlueprintLibrary"));
                UFunction* fnCreate = nullptr;
                UFunction* fnSetId  = nullptr;
                UFunction* fnGetId  = nullptr;
                if (bpLibCls)
                {
                    for (auto* fn : bpLibCls->ForEachFunctionInChain())
                    {
                        if (!fn) continue;
                        std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                        if (fname == STR("CreatePersistentSaveGameObjectId")) fnCreate = fn;
                    }
                }
                UClass* goatCls = nullptr;
                try { goatCls = goat->GetClassPrivate(); } catch (...) {}
                if (goatCls)
                {
                    for (auto* fn : goatCls->ForEachFunctionInChain())
                    {
                        if (!fn) continue;
                        std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                        if (fname == STR("SaveGameObjectSetId")) fnSetId = fn;
                        else if (fname == STR("SaveGameObjectGetId")) fnGetId = fn;
                    }
                }
                VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] resolved Create={:p} GetId={:p} SetId={:p}\n"),
                     (void*)fnCreate, (void*)fnGetId, (void*)fnSetId);

                bool needsId = true;
                int  getPsz  = 0;
                if (fnGetId)
                {
                    try { getPsz = fnGetId->GetParmsSize(); } catch (...) {}
                    std::vector<uint8_t> gbuf(getPsz > 0 ? getPsz : 64, 0);
                    if (safeProcessEvent(goat, fnGetId, gbuf.data()))
                    {
                        bool allZero = true;
                        for (int i = 0; i < getPsz; ++i) if (gbuf[i] != 0) { allZero = false; break; }
                        needsId = allZero;
                        VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] pre-set GetId psz={} {}\n"),
                             getPsz,
                             allZero ? STR("ALL ZERO (needs Id)") : STR("ALREADY SET"));
                    }
                }

                if (needsId && fnCreate && fnSetId && bpLibCls)
                {
                    // Get CDO for the static BP library call
                    UObject* bpLibCDO = bpLibCls->GetClassDefaultObject();
                    if (bpLibCDO)
                    {
                        int createPsz = 0;
                        try { createPsz = fnCreate->GetParmsSize(); } catch (...) {}
                        std::vector<uint8_t> cbuf(createPsz > 0 ? createPsz : 64, 0);
                        if (safeProcessEvent(bpLibCDO, fnCreate, cbuf.data()))
                        {
                            // The return value (FMorSaveGameObjectId) occupies the
                            // tail of the parms buffer. We don't know its exact size
                            // but we can pass the whole cbuf as the SetId arg —
                            // SetId's parms layout will read what it needs.
                            int setPsz = 0;
                            try { setPsz = fnSetId->GetParmsSize(); } catch (...) {}
                            std::vector<uint8_t> sbuf(setPsz > 0 ? setPsz : 64, 0);
                            // Copy the freshly-created Id bytes from cbuf into sbuf
                            // (Id is the only parm to SetId, return-only on Create)
                            int copyN = std::min((int)cbuf.size(), (int)sbuf.size());
                            std::memcpy(sbuf.data(), cbuf.data(), copyN);
                            if (safeProcessEvent(goat, fnSetId, sbuf.data()))
                            {
                                std::wstring hex;
                                for (int i = 0; i < copyN && i < 32; ++i)
                                {
                                    wchar_t tmp[8]; std::swprintf(tmp, 8, L"%02X ", sbuf[i]);
                                    hex += tmp;
                                }
                                VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] *** Created+Set Id on goat — first {} bytes: {} ***\n"),
                                     copyN < 32 ? copyN : 32, hex.c_str());
                            }
                            else
                            {
                                VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] SaveGameObjectSetId FAILED\n"));
                            }
                        }
                        else
                        {
                            VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] CreatePersistentSaveGameObjectId FAILED\n"));
                        }
                    }
                }
                else if (!needsId)
                {
                    VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] Skipped — goat already has non-zero Id\n"));
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] Skipped — missing UFunctions (Create={:p} SetId={:p})\n"),
                         (void*)fnCreate, (void*)fnSetId);
                }
            } catch (...) {
                VLOG(STR("[MoriaCppMod] [BellSpawn] [SaveId] Exception during Id mint/set\n"));
            }

            // [rc.48] Queue StoreRuntimeActor for ~100ms later. Per DC Q3:
            // post-FinishSpawning + frame delay ensures ConstructionScript
            // + BeginPlay are complete so we capture full actor state, not
            // CDO defaults.
            {
                PendingStoreRecord rec;
                rec.goat = RC::Unreal::FWeakObjectPtr(goat);
                rec.readyMs = GetTickCount64() + 100;
                m_pendingStores.push_back(rec);
                VLOG(STR("[MoriaCppMod] [BellSpawn] queued StoreRuntimeActor for goat={:p} (fires in ~100ms after ConstructionScript+BeginPlay complete)\n"),
                     (void*)goat);
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
                if (seh_findAnyGoatActor(&hit))
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
                if (seh_findAnyGoatActor(&hit))
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
            if (seh_findAnyGoatActor(&hit) && !hit.empty())
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
                if (seh_findAnyGoatActor(&hit))
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

            if (!ctrl || !isObjectAlive(ctrl)) {
                if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: ctrl null/dead\n"));
                return;
            }
            if (!playerPawn || !isObjectAlive(playerPawn)) {
                if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: playerPawn null/dead\n"));
                return;
            }
            std::wstring ctrlCls;
            try { ctrlCls = ctrl->GetClassPrivate()->GetName(); } catch (...) {}
            // AAIController has a "Blackboard" UPROPERTY (UE4 standard) —
            // alternative name "BlackboardComp" on some custom controllers.
            const wchar_t* bbProp = STR("Blackboard");
            UObject** bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Blackboard"));
            if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr)) {
                bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComp"));
                bbProp = STR("BlackboardComp");
            }
            if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr)) {
                if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: no Blackboard on ctrl cls='{}' (tried Blackboard + BlackboardComp)\n"),
                               ctrlCls.c_str());
                return;
            }
            UObject* bb = *bbPtr;
            auto* setObjFn = bb->GetFunctionByNameInChain(STR("SetValueAsObject"));
            if (!setObjFn) {
                if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: SetValueAsObject missing on bb\n"));
                return;
            }
            auto* pKey = findParam(setObjFn, STR("KeyName"));
            auto* pVal = findParam(setObjFn, STR("ObjectValue"));
            if (!pKey || !pVal) {
                if (diag) VLOG(STR("[MoriaCppMod] [Leash] ABORT: SetValueAsObject params missing (key={:p} val={:p})\n"),
                               (void*)pKey, (void*)pVal);
                return;
            }
            int sz = setObjFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            // FName "LeashActor"
            RC::Unreal::FName keyName(STR("LeashActor"), RC::Unreal::FNAME_Add);
            std::memcpy(buf.data() + pKey->GetOffset_Internal(), &keyName, sizeof(RC::Unreal::FName));
            *reinterpret_cast<UObject**>(buf.data() + pVal->GetOffset_Internal()) = playerPawn;
            bool ok = safeProcessEvent(bb, setObjFn, buf.data());
            if (diag) VLOG(STR("[MoriaCppMod] [Leash] SET LeashActor=player on ctrl='{}' bbProp='{}' PE_ok={}\n"),
                           ctrlCls.c_str(), bbProp, ok);
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
            if (!bbPtr || !*bbPtr || !isObjectAlive(*bbPtr))
                bbPtr = ctrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BlackboardComp"));
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
                    try { safeProcessEvent(bb, clearFn, buf.data()); } catch (...) {}
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
            try { safeProcessEvent(bb, setObjFn, buf.data()); } catch (...) {}
        }

        // [rc.41 PROBE N 2026-06-10] Walk NpcInfo at character-load BEFORE
        // any bell-ring. Dumps ALL entries (not just goat-shaped) so we can
        // empirically tell whether goat entries persisted across save/reload.
        // Callsite: dllmain.cpp tick at ~3s post-character-load (one-shot
        // gated by m_probeNFired).
        void runProbeN()
        {
            VLOG(STR("[MoriaCppMod] [PathX-N] === Probe N: NpcInfo dump at character-load ===\n"));
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
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [PathX-N] MorNPCManager singleton not findable — bail\n"));
                return;
            }
            uint8_t* niBase = reinterpret_cast<uint8_t*>(mgr) + 0x03a0;
            uint8_t* itemsHdr = niBase + 0x0108;
            if (!isReadableMemory(itemsHdr, 16))
            {
                VLOG(STR("[MoriaCppMod] [PathX-N] NpcInfo.Items header unreadable\n"));
                return;
            }
            uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
            int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
            constexpr int kStride          = 0x260;
            constexpr int kGuidOff         = 0x001c;
            constexpr int kRoleRowNameOff  = 0x0050;
            constexpr int kStaticRowNameOff = 0x01C8;
            constexpr int kIsRescuedOff    = 0x01A0;
            VLOG(STR("[MoriaCppMod] [PathX-N] NpcInfo.Items.Num={} (stride=0x{:x}, data={:p})\n"),
                 itemsNum, kStride, (void*)itemsData);
            if (!itemsData || itemsNum <= 0 || itemsNum >= 500
                || !isReadableMemory(itemsData, itemsNum * kStride))
            {
                VLOG(STR("[MoriaCppMod] [PathX-N] entries unreadable / out-of-range — bail\n"));
                return;
            }
            // [rc.45 NAME 2026-06-10] Also dump the Name FText so we can
            // verify what the engine writes there for our goats vs
            // recruited dwarves. User's hypothesis: filter by configured
            // goat name (m_goatName, default "Rûdh") as the unique
            // fingerprint. FMorNpcPersistentData.Name is FText @ +0x020
            // within persistent → abs +0x030 within FMorNPCInfo.
            constexpr int kNameOff = 0x0030;
            int goatLike = 0;
            for (int i = 0; i < itemsNum; ++i)
            {
                uint8_t* entry = itemsData + i * kStride;
                uint32_t* g = reinterpret_cast<uint32_t*>(entry + kGuidOff);
                wchar_t tmpRole[256], tmpStatic[256], tmpName[256];
                seh_fnameToStringToBuf(entry + kRoleRowNameOff,   tmpRole,   256);
                seh_fnameToStringToBuf(entry + kStaticRowNameOff, tmpStatic, 256);
                seh_ftextToStringToBuf(entry + kNameOff, tmpName, 256);
                std::wstring roleStr  = tmpRole;
                std::wstring staticStr = tmpStatic;
                std::wstring nameStr  = tmpName;
                bool isRescued = false;
                if (isReadableMemory(entry + kIsRescuedOff, 1))
                    isRescued = *(entry + kIsRescuedOff) != 0;
                // [rc.45] Goat-shape filter: Role IN ('Default', 'Porter').
                // Per user: Porter is goat-exclusive (DT_NPCRoles.Porter
                // row added by goat pak). Rescued constraint dropped.
                bool maybeGoat = (roleStr == STR("Default") || roleStr == STR("Porter"));
                VLOG(STR("[MoriaCppMod] [PathX-N]   [{}] GUID={:08X}-{:08X}-{:08X}-{:08X} Role='{}' Name='{}' StaticData='{}' Rescued={}{}\n"),
                     i, g[0], g[1], g[2], g[3], roleStr.c_str(), nameStr.c_str(), staticStr.c_str(),
                     isRescued ? STR("Y") : STR("n"),
                     maybeGoat ? STR(" *** GOAT-SHAPE (adoptable) ***") : STR(""));
                if (maybeGoat) ++goatLike;
            }
            VLOG(STR("[MoriaCppMod] [PathX-N] === end — {} goat-shape candidates across {} entries (m_goatName='{}') ===\n"),
                 goatLike, itemsNum, m_goatName.c_str());
        }

        // [rc.44 PROBE P 2026-06-10] Post-reload world scan. Empirical
        // diagnostic to determine what ACTUALLY persists in memory after
        // save/reload. Three scans, each read-only, no behavior change:
        //   1. BP_NpcGoat_C actors  — do goat actors survive world unload?
        //   2. BP_ContainerItem_Dwarf_BodyInventoryNPC_C actors — do body
        //      containers persist independently?
        //   3. BP_Wood_C actors — are wood items floating in the world
        //      after reload (orphaned from destroyed containers)?
        // Plus: for each live goat, dump its MorInventoryComponent.Items
        // array so we can see if the persisted entry has wood inside.
        void runProbeP()
        {
            VLOG(STR("[MoriaCppMod] [PathX-P] === Probe P: Post-reload world scan ===\n"));

            // Scan 1 — BP_NpcGoat_C actors
            {
                std::vector<UObject*> goats;
                findAllOfSafe(STR("BP_NpcGoat_C"), goats);
                VLOG(STR("[MoriaCppMod] [PathX-P] === Scan 1: BP_NpcGoat_C ({} total via FindAllOf) ===\n"), goats.size());
                UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
                UClass* invCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Moria.MorInventoryComponent"));
                int liveGoats = 0;
                for (UObject* g : goats)
                {
                    if (!g || !isObjectAlive(g)) continue;
                    std::wstring cn = safeClassName(g);
                    if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                    ++liveGoats;

                    // Read NpcGuid via MorNPCComponent
                    std::wstring guidStr = STR("?");
                    if (npcCompCls)
                    {
                        if (auto* getCompFn = g->GetFunctionByNameInChain(STR("GetComponentByClass")))
                        {
                            int sz = getCompFn->GetParmsSize();
                            std::vector<uint8_t> buf(sz, 0);
                            writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
                            if (safeProcessEvent(g, getCompFn, buf.data()))
                            {
                                UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                                if (npcComp && isObjectAlive(npcComp))
                                {
                                    uint8_t* guidPtr = npcComp->GetValuePtrByPropertyNameInChain<uint8_t>(STR("NpcGuid"));
                                    if (guidPtr)
                                    {
                                        uint32_t* g32 = reinterpret_cast<uint32_t*>(guidPtr);
                                        guidStr = std::format(STR("{:08X}-{:08X}-{:08X}-{:08X}"),
                                                              g32[0], g32[1], g32[2], g32[3]);
                                    }
                                }
                            }
                        }
                    }
                    VLOG(STR("[MoriaCppMod] [PathX-P]   goat={:p} cls='{}' GUID={}\n"),
                         (void*)g, cn.c_str(), guidStr.c_str());

                    // Walk InvComp.Items to look for inventory
                    if (invCompCls)
                    {
                        if (auto* getCompFn = g->GetFunctionByNameInChain(STR("GetComponentByClass")))
                        {
                            int sz = getCompFn->GetParmsSize();
                            std::vector<uint8_t> buf(sz, 0);
                            writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), invCompCls);
                            if (safeProcessEvent(g, getCompFn, buf.data()))
                            {
                                UObject* invComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
                                if (invComp && isObjectAlive(invComp))
                                {
                                    FProperty* itemsProp = invComp->GetPropertyByNameInChain(STR("Items"));
                                    if (itemsProp)
                                    {
                                        int itemsOff = itemsProp->GetOffset_Internal();
                                        uint8_t* listBase = reinterpret_cast<uint8_t*>(invComp) + itemsOff + iiaListOff();
                                        if (isReadableMemory(listBase, 16))
                                        {
                                            uint8_t* arrData = *reinterpret_cast<uint8_t**>(listBase);
                                            int32_t  arrNum  = *reinterpret_cast<int32_t*>(listBase + 8);
                                            int stride = iiSize();
                                            int itemOff = iiItemOff();
                                            int countOff = 0x18;
                                            int slotOff  = 0x1C;
                                            int idOff    = iiIDOff();
                                            VLOG(STR("[MoriaCppMod] [PathX-P]     InvComp={:p} Items.Num={}\n"),
                                                 (void*)invComp, arrNum);
                                            if (arrData && arrNum > 0 && arrNum < 200
                                                && isReadableMemory(arrData, arrNum * stride))
                                            {
                                                int woodCount = 0;
                                                for (int32_t i = 0; i < arrNum; ++i)
                                                {
                                                    uint8_t* entry = arrData + i * stride;
                                                    if (!isReadableMemory(entry, stride)) continue;
                                                    UClass* ic = *reinterpret_cast<UClass**>(entry + itemOff);
                                                    int32_t cnt = *reinterpret_cast<int32_t*>(entry + countOff);
                                                    int32_t sl  = *reinterpret_cast<int32_t*>(entry + slotOff);
                                                    int32_t id  = *reinterpret_cast<int32_t*>(entry + idOff);
                                                    std::wstring nm = ic ? safeObjectName(ic) : STR("(null)");
                                                    bool wood = nm.find(STR("Wood")) != std::wstring::npos;
                                                    if (wood) ++woodCount;
                                                    VLOG(STR("[MoriaCppMod] [PathX-P]       item[{}] id={} slot={} count={} class='{}'{}\n"),
                                                         i, id, sl, cnt, nm.c_str(),
                                                         wood ? STR(" *** WOOD ***") : STR(""));
                                                }
                                                if (woodCount > 0)
                                                {
                                                    VLOG(STR("[MoriaCppMod] [PathX-P]     *** GOAT {:p} HAS {} WOOD ITEM(S) ***\n"),
                                                         (void*)g, woodCount);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-P] === end Scan 1 ({} live goats) ===\n"), liveGoats);
            }

            // Scan 2 — BP_ContainerItem_Dwarf_BodyInventoryNPC_C actors
            {
                std::vector<UObject*> containers;
                findAllOfSafe(STR("BP_ContainerItem_Dwarf_BodyInventoryNPC_C"), containers);
                VLOG(STR("[MoriaCppMod] [PathX-P] === Scan 2: BP_ContainerItem_Dwarf_BodyInventoryNPC_C ({} total) ===\n"), containers.size());
                int live = 0;
                for (UObject* c : containers)
                {
                    if (!c || !isObjectAlive(c)) continue;
                    std::wstring cn = safeClassName(c);
                    if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                    ++live;
                    UObject* owner = nullptr;
                    if (auto* getOwnerFn = c->GetFunctionByNameInChain(STR("GetOwner")))
                    {
                        struct { UObject* Ret{nullptr}; } op{};
                        if (safeProcessEvent(c, getOwnerFn, &op)) owner = op.Ret;
                    }
                    std::wstring ownerCls = (owner && isObjectAlive(owner)) ? safeClassName(owner) : STR("(null)");
                    VLOG(STR("[MoriaCppMod] [PathX-P]   container={:p} cls='{}' owner={:p} ownerCls='{}'\n"),
                         (void*)c, cn.c_str(), (void*)owner, ownerCls.c_str());
                }
                VLOG(STR("[MoriaCppMod] [PathX-P] === end Scan 2 ({} live body containers) ===\n"), live);
            }

            // Scan 3 — BP_Wood_C actors (floating items)
            {
                std::vector<UObject*> woods;
                findAllOfSafe(STR("BP_Wood_C"), woods);
                VLOG(STR("[MoriaCppMod] [PathX-P] === Scan 3: BP_Wood_C ({} total) ===\n"), woods.size());
                int live = 0;
                for (UObject* w : woods)
                {
                    if (!w || !isObjectAlive(w)) continue;
                    std::wstring cn = safeClassName(w);
                    if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                    ++live;
                    UObject* owner = nullptr;
                    if (auto* getOwnerFn = w->GetFunctionByNameInChain(STR("GetOwner")))
                    {
                        struct { UObject* Ret{nullptr}; } op{};
                        if (safeProcessEvent(w, getOwnerFn, &op)) owner = op.Ret;
                    }
                    std::wstring ownerCls = (owner && isObjectAlive(owner)) ? safeClassName(owner) : STR("(null)");
                    if (live <= 20)
                    {
                        VLOG(STR("[MoriaCppMod] [PathX-P]   wood={:p} cls='{}' owner={:p} ownerCls='{}'\n"),
                             (void*)w, cn.c_str(), (void*)owner, ownerCls.c_str());
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-P] === end Scan 3 ({} live BP_Wood_C actors, first 20 logged) ===\n"), live);
            }

            VLOG(STR("[MoriaCppMod] [PathX-P] === Probe P END ===\n"));
        }

        // [rc.47 PROBE Q 2026-06-12] Hunts for the manager-side UFunction
        // chain the engine uses to respawn dwarves from saved NpcInfo
        // entries with their items intact. If we find a usable
        // RestoreNpc(FGuid)/SpawnFromInfo/etc, our bell-spawn DLL calls it
        // instead of spawning fresh and inherits dwarf persistence
        // wholesale — no pak surgery required.
        //
        // Three parts (all read-only, no behavior change):
        //   Q.1 — All MorNPCManager UFunctions, UNFILTERED. Probe K's
        //         keyword filter was too narrow; this dumps everything
        //         with full parm signatures.
        //   Q.2 — All MorSaveSystemWorldState UFunctions (the WorldState
        //         class is the save graph's central registry — receives
        //         the WorldState parameter for all 6 save-iface methods).
        //   Q.3 — Hex dump of FMorNpcPersistentData unknown region
        //         (+0x048..+0x190, 328 bytes) for entries with dwarf-shape
        //         (Role != 'Default' AND Role != 'Porter'). Looking for
        //         TArray header (8B ptr + 4B Num + 4B Max), FGuid bytes,
        //         or item-count integers.
        void runProbeQ()
        {
            VLOG(STR("[MoriaCppMod] [PathX-Q] === Probe Q: Manager + WorldState UFunction hunt + dwarf PersistentData hex ===\n"));

            // ─────────────────────────────────────────────────────────
            // Q.1 — MorNPCManager: ALL UFunctions, unfiltered
            // ─────────────────────────────────────────────────────────
            {
                VLOG(STR("[MoriaCppMod] [PathX-Q.1] === All MorNPCManager UFunctions (unfiltered) ===\n"));
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
                    VLOG(STR("[MoriaCppMod] [PathX-Q.1] MorNPCManager singleton not findable — skip\n"));
                }
                else
                {
                    UClass* mc = nullptr;
                    try { mc = mgr->GetClassPrivate(); } catch (...) {}
                    if (mc)
                    {
                        VLOG(STR("[MoriaCppMod] [PathX-Q.1] mgr={:p} cls={}\n"), (void*)mgr, cn_safe(mc));
                        int idx = 0;
                        try {
                            for (auto* fn : mc->ForEachFunctionInChain())
                            {
                                if (!fn) continue;
                                std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                int parmsSize = 0;
                                try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [PathX-Q.1] [{:03d}] {} ParmsSize={}\n"),
                                     idx, fname.c_str(), parmsSize);
                                // Dump parm details
                                try {
                                    int pi = 0;
                                    for (auto* p : fn->ForEachProperty())
                                    {
                                        if (!p) continue;
                                        uint64_t pflags = 0;
                                        try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                                        bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                                        if (!isParm) continue;
                                        bool isReturn = (pflags & 0x0000000000000400ULL) != 0;
                                        std::wstring pname; try { pname = p->GetName(); } catch (...) {}
                                        std::wstring ptype; try { ptype = p->GetClass().GetName(); } catch (...) {}
                                        int psz = -1; try { psz = p->GetSize(); } catch (...) {}
                                        VLOG(STR("[MoriaCppMod] [PathX-Q.1]        parm[{}] {} '{}' type={} size={}\n"),
                                             pi, isReturn ? STR("[ret]") : STR("[in ]"),
                                             pname.c_str(), ptype.c_str(), psz);
                                        ++pi;
                                    }
                                } catch (...) {}
                                ++idx;
                            }
                        } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [PathX-Q.1] === end MorNPCManager ({} total UFunctions) ===\n"), idx);
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // Q.2 — MorSaveSystemWorldState: ALL UFunctions
            // ─────────────────────────────────────────────────────────
            {
                VLOG(STR("[MoriaCppMod] [PathX-Q.2] === All MorSaveSystemWorldState UFunctions (unfiltered) ===\n"));
                // Try a list of candidate class names
                static const wchar_t* worldStateCands[] = {
                    STR("MorSaveSystemWorldState"),
                    STR("MorWorldState"),
                    STR("MorSaveWorldState"),
                    STR("SaveSystemWorldState"),
                    STR("MorSaveSystem"),
                    STR("MorSaveGameWorldState"),
                    nullptr
                };
                UObject* ws = nullptr;
                std::wstring foundCls;
                for (const wchar_t** cn = worldStateCands; *cn; ++cn)
                {
                    std::vector<UObject*> cands;
                    if (!findAllOfSafe(*cn, cands)) continue;
                    for (UObject* o : cands)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring oc = safeClassName(o);
                        if (oc.size() >= 9 && oc.substr(0,9) == STR("Default__")) continue;
                        ws = o; foundCls = *cn; break;
                    }
                    if (ws) break;
                }
                if (!ws)
                {
                    VLOG(STR("[MoriaCppMod] [PathX-Q.2] No WorldState class found across candidates — listing what we tried:\n"));
                    for (const wchar_t** cn = worldStateCands; *cn; ++cn)
                        VLOG(STR("[MoriaCppMod] [PathX-Q.2]   tried '{}': not found\n"), *cn);
                }
                else
                {
                    UClass* wc = nullptr;
                    try { wc = ws->GetClassPrivate(); } catch (...) {}
                    if (wc)
                    {
                        VLOG(STR("[MoriaCppMod] [PathX-Q.2] ws={:p} class={} (found via candidate '{}')\n"),
                             (void*)ws, cn_safe(wc), foundCls.c_str());
                        int idx = 0;
                        try {
                            for (auto* fn : wc->ForEachFunctionInChain())
                            {
                                if (!fn) continue;
                                std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                                int parmsSize = 0;
                                try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [PathX-Q.2] [{:03d}] {} ParmsSize={}\n"),
                                     idx, fname.c_str(), parmsSize);
                                try {
                                    int pi = 0;
                                    for (auto* p : fn->ForEachProperty())
                                    {
                                        if (!p) continue;
                                        uint64_t pflags = 0;
                                        try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                                        bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                                        if (!isParm) continue;
                                        bool isReturn = (pflags & 0x0000000000000400ULL) != 0;
                                        std::wstring pname; try { pname = p->GetName(); } catch (...) {}
                                        std::wstring ptype; try { ptype = p->GetClass().GetName(); } catch (...) {}
                                        int psz = -1; try { psz = p->GetSize(); } catch (...) {}
                                        VLOG(STR("[MoriaCppMod] [PathX-Q.2]        parm[{}] {} '{}' type={} size={}\n"),
                                             pi, isReturn ? STR("[ret]") : STR("[in ]"),
                                             pname.c_str(), ptype.c_str(), psz);
                                        ++pi;
                                    }
                                } catch (...) {}
                                ++idx;
                            }
                        } catch (...) {}
                        VLOG(STR("[MoriaCppMod] [PathX-Q.2] === end WorldState ({} total UFunctions) ===\n"), idx);
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // Q.3 — FMorNpcPersistentData hex dump for dwarf-shape entries
            //       (+0x048..+0x190 = 0x148 bytes = 328 bytes of unknown)
            // ─────────────────────────────────────────────────────────
            {
                VLOG(STR("[MoriaCppMod] [PathX-Q.3] === FMorNpcPersistentData +0x048..+0x190 hex dump (dwarf entries) ===\n"));
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
                    VLOG(STR("[MoriaCppMod] [PathX-Q.3] MorNPCManager singleton not findable — skip\n"));
                }
                else
                {
                    uint8_t* niBase = reinterpret_cast<uint8_t*>(mgr) + 0x03a0;
                    uint8_t* itemsHdr = niBase + 0x0108;
                    if (!isReadableMemory(itemsHdr, 16))
                    {
                        VLOG(STR("[MoriaCppMod] [PathX-Q.3] NpcInfo.Items header unreadable\n"));
                    }
                    else
                    {
                        uint8_t* itemsData = *reinterpret_cast<uint8_t**>(itemsHdr);
                        int32_t  itemsNum  = *reinterpret_cast<int32_t*>(itemsHdr + 8);
                        constexpr int kStride          = 0x260;
                        constexpr int kGuidOff         = 0x001c;
                        constexpr int kRoleRowNameOff  = 0x0050;
                        constexpr int kStaticRowNameOff = 0x01C8;
                        constexpr int kIsRescuedOff    = 0x01A0;
                        constexpr int kDumpStart       = 0x0048;  // PersistentData +0x038 = abs +0x048
                        constexpr int kDumpLen         = 0x0148;  // 328 bytes
                        VLOG(STR("[MoriaCppMod] [PathX-Q.3] NpcInfo has {} entries; scanning for dwarf-shape (Role != Default/Porter)\n"),
                             itemsNum);
                        if (itemsData && itemsNum > 0 && itemsNum < 500
                            && isReadableMemory(itemsData, itemsNum * kStride))
                        {
                            int dwarvesFound = 0;
                            int maxToDump = 5;  // cap dump to first 5 dwarves to keep log size sane
                            for (int i = 0; i < itemsNum && dwarvesFound < maxToDump; ++i)
                            {
                                uint8_t* entry = itemsData + i * kStride;
                                wchar_t tmpRole[256], tmpStatic[256];
                                seh_fnameToStringToBuf(entry + kRoleRowNameOff,   tmpRole,   256);
                                seh_fnameToStringToBuf(entry + kStaticRowNameOff, tmpStatic, 256);
                                std::wstring roleStr  = tmpRole;
                                std::wstring staticStr = tmpStatic;
                                bool isRescued = false;
                                if (isReadableMemory(entry + kIsRescuedOff, 1))
                                    isRescued = *(entry + kIsRescuedOff) != 0;
                                bool isDwarf = (roleStr != STR("Default") && roleStr != STR("Porter"));
                                if (!isDwarf) continue;
                                ++dwarvesFound;

                                uint32_t* g = reinterpret_cast<uint32_t*>(entry + kGuidOff);
                                VLOG(STR("[MoriaCppMod] [PathX-Q.3] --- Entry [{}] GUID={:08X}-{:08X}-{:08X}-{:08X} Role='{}' StaticData='{}' Rescued={} ---\n"),
                                     i, g[0], g[1], g[2], g[3],
                                     roleStr.c_str(), staticStr.c_str(),
                                     isRescued ? STR("Y") : STR("n"));

                                // Hex dump 0x148 bytes from entry + 0x048
                                uint8_t* dump = entry + kDumpStart;
                                if (!isReadableMemory(dump, kDumpLen))
                                {
                                    VLOG(STR("[MoriaCppMod] [PathX-Q.3]     [region unreadable]\n"));
                                    continue;
                                }
                                for (int off = 0; off < kDumpLen; off += 16)
                                {
                                    uint8_t* row = dump + off;
                                    // ASCII column
                                    wchar_t ascii[17] = {0};
                                    for (int j = 0; j < 16; ++j)
                                    {
                                        uint8_t b = row[j];
                                        ascii[j] = (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
                                    }
                                    VLOG(STR("[MoriaCppMod] [PathX-Q.3]   +0x{:04X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  |{}|\n"),
                                         (unsigned)(kDumpStart + off),
                                         row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
                                         row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15],
                                         ascii);
                                }
                                VLOG(STR("[MoriaCppMod] [PathX-Q.3] --- end entry [{}] ---\n"), i);
                            }
                            VLOG(STR("[MoriaCppMod] [PathX-Q.3] === Found {} dwarf-shape entries (dumped first {}); total entries={} ===\n"),
                                 dwarvesFound, dwarvesFound < maxToDump ? dwarvesFound : maxToDump, itemsNum);

                            // Fallback: if no dwarves, dump the FIRST entry regardless
                            // (might be a goat — still useful for layout reference)
                            if (dwarvesFound == 0 && itemsNum > 0)
                            {
                                VLOG(STR("[MoriaCppMod] [PathX-Q.3] No dwarves found — dumping entry [0] as layout reference\n"));
                                uint8_t* entry = itemsData;
                                wchar_t tmpRole[256], tmpStatic[256];
                                seh_fnameToStringToBuf(entry + kRoleRowNameOff,   tmpRole,   256);
                                seh_fnameToStringToBuf(entry + kStaticRowNameOff, tmpStatic, 256);
                                uint32_t* g = reinterpret_cast<uint32_t*>(entry + kGuidOff);
                                VLOG(STR("[MoriaCppMod] [PathX-Q.3] --- Entry [0] GUID={:08X}-{:08X}-{:08X}-{:08X} Role='{}' StaticData='{}' ---\n"),
                                     g[0], g[1], g[2], g[3], tmpRole, tmpStatic);
                                uint8_t* dump = entry + kDumpStart;
                                if (isReadableMemory(dump, kDumpLen))
                                {
                                    for (int off = 0; off < kDumpLen; off += 16)
                                    {
                                        uint8_t* row = dump + off;
                                        wchar_t ascii[17] = {0};
                                        for (int j = 0; j < 16; ++j)
                                        {
                                            uint8_t b = row[j];
                                            ascii[j] = (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
                                        }
                                        VLOG(STR("[MoriaCppMod] [PathX-Q.3]   +0x{:04X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  |{}|\n"),
                                             (unsigned)(kDumpStart + off),
                                             row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
                                             row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15],
                                             ascii);
                                    }
                                }
                            }
                        }
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-Q.3] === end Q.3 ===\n"));
            }

            VLOG(STR("[MoriaCppMod] [PathX-Q] === Probe Q END ===\n"));
        }

        // Small helper for class-name string in Probe Q logs.
        std::wstring cn_safe(UClass* c)
        {
            if (!c || !isObjectAlive(c)) return STR("(null)");
            try { return c->GetName(); } catch (...) { return STR("(throw)"); }
        }

        // [rc.48 PROBE R 2026-06-14] Walk BP_MorSaveSystemWorldState_C
        // class properties hunting for the runtime-actor registry.
        // Q.2 showed StoreRuntimeActor exists but didn't reveal where
        // it writes to. Probe R dumps every ArrayProperty + MapProperty
        // + ObjectProperty with name + offset + size + current count
        // (for arrays). Highlights properties whose name contains
        // 'Runtime', 'Actor', 'Registered', 'Saved', 'Persistent',
        // 'Handle', 'Record', 'Map', 'Set'.
        void runProbeR()
        {
            VLOG(STR("[MoriaCppMod] [PathX-R] === Probe R: WorldState property walk ===\n"));
            UObject* ws = nullptr;
            std::vector<UObject*> wsCands;
            if (findAllOfSafe(STR("MorSaveSystemWorldState"), wsCands))
            {
                for (UObject* o : wsCands)
                {
                    if (!o || !isObjectAlive(o)) continue;
                    std::wstring cn = safeClassName(o);
                    if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                    ws = o; break;
                }
            }
            if (!ws)
            {
                VLOG(STR("[MoriaCppMod] [PathX-R] MorSaveSystemWorldState singleton not findable — skip\n"));
                return;
            }
            UClass* wc = nullptr;
            try { wc = ws->GetClassPrivate(); } catch (...) {}
            if (!wc)
            {
                VLOG(STR("[MoriaCppMod] [PathX-R] WorldState class unreadable\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-R] ws={:p} class={}\n"), (void*)ws, cn_safe(wc));

            int total = 0, interesting = 0;
            try {
                for (auto* p : wc->ForEachPropertyInChain())
                {
                    if (!p) continue;
                    ++total;
                    std::wstring pname; try { pname = p->GetName(); } catch (...) { continue; }
                    std::wstring pcls; try { pcls = p->GetClass().GetName(); } catch (...) {}
                    int32 off = -1; try { off = p->GetOffset_Internal(); } catch (...) {}
                    int psz = -1; try { psz = p->GetSize(); } catch (...) {}

                    bool isContainer = (pcls == STR("ArrayProperty") ||
                                        pcls == STR("MapProperty") ||
                                        pcls == STR("SetProperty"));
                    bool keywordHit =
                        pname.find(STR("Runtime"))     != std::wstring::npos ||
                        pname.find(STR("Actor"))       != std::wstring::npos ||
                        pname.find(STR("Registered"))  != std::wstring::npos ||
                        pname.find(STR("Saved"))       != std::wstring::npos ||
                        pname.find(STR("Persistent"))  != std::wstring::npos ||
                        pname.find(STR("Handle"))      != std::wstring::npos ||
                        pname.find(STR("Record"))      != std::wstring::npos ||
                        pname.find(STR("Stability"))   != std::wstring::npos ||
                        pname.find(STR("Level"))       != std::wstring::npos;

                    if (!isContainer && !keywordHit) continue;
                    ++interesting;

                    VLOG(STR("[MoriaCppMod] [PathX-R]   {} '{}' type={} off=0x{:04X} size={}\n"),
                         keywordHit ? STR("***") : STR("   "),
                         pname.c_str(), pcls.c_str(), (unsigned)off, psz);

                    // For ArrayProperty, dump the TArray header (data ptr + Num + Max)
                    if (pcls == STR("ArrayProperty") && off >= 0)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(ws) + off;
                        if (isReadableMemory(base, 16))
                        {
                            uint8_t* data = *reinterpret_cast<uint8_t**>(base);
                            int32_t num   = *reinterpret_cast<int32_t*>(base + 8);
                            int32_t cap   = *reinterpret_cast<int32_t*>(base + 12);
                            VLOG(STR("[MoriaCppMod] [PathX-R]       TArray data={:p} Num={} Max={}\n"),
                                 (void*)data, num, cap);
                        }
                    }
                    // For MapProperty / SetProperty: TSparseArray internals are
                    // complex; just log the offset for now. DC can decode.
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-R] === end ({} interesting / {} total properties) ===\n"),
                 interesting, total);
        }

        // [rc.49 PROBE S 2026-06-14] Per user correction: pivot from
        // Role marker (proven to drift across save/reload) to Name
        // marker. Probe S enumerates ALL UFunctions on MorNPCComponent
        // with full parm signatures so we can find the right
        // name-write UFunction (SetName / SetCustomName / Rename /
        // SetDisplayName / etc.). Once found, rc.50 wires the call.
        void runProbeS()
        {
            VLOG(STR("[MoriaCppMod] [PathX-S] === Probe S: All MorNPCComponent UFunctions (unfiltered) ===\n"));
            UClass* compCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!compCls)
            {
                VLOG(STR("[MoriaCppMod] [PathX-S] MorNPCComponent UClass not findable\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-S] cls={:p} {}\n"), (void*)compCls, cn_safe(compCls));
            int idx = 0, hits = 0;
            // Pre-pass: count and emit highlighted name/rename hits first
            VLOG(STR("[MoriaCppMod] [PathX-S] --- highlighted: name/rename/display candidates ---\n"));
            try {
                for (auto* fn : compCls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                    bool isCandidate =
                        fname.find(STR("Name"))    != std::wstring::npos ||
                        fname.find(STR("Rename"))  != std::wstring::npos ||
                        fname.find(STR("Display")) != std::wstring::npos ||
                        fname.find(STR("Custom"))  != std::wstring::npos ||
                        fname.find(STR("Text"))    != std::wstring::npos ||
                        fname.find(STR("Title"))   != std::wstring::npos ||
                        fname.find(STR("Label"))   != std::wstring::npos;
                    if (!isCandidate) continue;
                    int parmsSize = 0;
                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [PathX-S] *** [{}] {} ParmsSize={}\n"),
                         idx, fname.c_str(), parmsSize);
                    try {
                        int pi = 0;
                        for (auto* p : fn->ForEachProperty())
                        {
                            if (!p) continue;
                            uint64_t pflags = 0;
                            try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                            bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                            if (!isParm) continue;
                            bool isReturn = (pflags & 0x0000000000000400ULL) != 0;
                            std::wstring pname; try { pname = p->GetName(); } catch (...) {}
                            std::wstring ptype; try { ptype = p->GetClass().GetName(); } catch (...) {}
                            int psz = -1; try { psz = p->GetSize(); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [PathX-S] ***        parm[{}] {} '{}' type={} size={}\n"),
                                 pi, isReturn ? STR("[ret]") : STR("[in ]"),
                                 pname.c_str(), ptype.c_str(), psz);
                            ++pi;
                        }
                    } catch (...) {}
                    ++hits;
                    ++idx;
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-S] --- end candidates ({} hits) ---\n"), hits);
            // Full list for completeness
            VLOG(STR("[MoriaCppMod] [PathX-S] --- full UFunction list ---\n"));
            int total = 0;
            try {
                for (auto* fn : compCls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                    int parmsSize = 0;
                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [PathX-S] [{:03d}] {} ParmsSize={}\n"),
                         total, fname.c_str(), parmsSize);
                    ++total;
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-S] === end ({} total UFunctions on MorNPCComponent) ===\n"),
                 total);
        }

        // [rc.50 PROBE T 2026-06-14] Enumerate BP_NpcGoat_C name-keyword
        // UFunctions. Per Probe S findings: CanSetCustomDisplayName on
        // MorNPCComponent implies SetCustomDisplayName exists on the
        // actor or a parent. Probe T finds it.
        void runProbeT()
        {
            VLOG(STR("[MoriaCppMod] [PathX-T] === Probe T: BP_NpcGoat_C name candidates ===\n"));
            UClass* goatCls = m_goatBPClass && isObjectAlive(m_goatBPClass) ? m_goatBPClass : nullptr;
            if (!goatCls)
            {
                goatCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
            }
            if (!goatCls)
            {
                goatCls = goat_loadClassAssetBlocking(STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
            }
            if (!goatCls)
            {
                VLOG(STR("[MoriaCppMod] [PathX-T] BP_NpcGoat_C UClass not findable — skip\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-T] cls={:p} {}\n"), (void*)goatCls, cn_safe(goatCls));
            int total = 0, hits = 0;
            try {
                for (auto* fn : goatCls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    ++total;
                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                    bool isCandidate =
                        fname.find(STR("Name"))    != std::wstring::npos ||
                        fname.find(STR("Rename"))  != std::wstring::npos ||
                        fname.find(STR("Display")) != std::wstring::npos ||
                        fname.find(STR("Custom"))  != std::wstring::npos ||
                        fname.find(STR("Title"))   != std::wstring::npos ||
                        fname.find(STR("Label"))   != std::wstring::npos;
                    if (!isCandidate) continue;
                    int parmsSize = 0;
                    try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                    VLOG(STR("[MoriaCppMod] [PathX-T] *** [{}] {} ParmsSize={}\n"),
                         hits, fname.c_str(), parmsSize);
                    try {
                        int pi = 0;
                        for (auto* p : fn->ForEachProperty())
                        {
                            if (!p) continue;
                            uint64_t pflags = 0;
                            try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                            bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                            if (!isParm) continue;
                            bool isReturn = (pflags & 0x0000000000000400ULL) != 0;
                            std::wstring pname; try { pname = p->GetName(); } catch (...) {}
                            std::wstring ptype; try { ptype = p->GetClass().GetName(); } catch (...) {}
                            int psz = -1; try { psz = p->GetSize(); } catch (...) {}
                            VLOG(STR("[MoriaCppMod] [PathX-T] ***        parm[{}] {} '{}' type={} size={}\n"),
                                 pi, isReturn ? STR("[ret]") : STR("[in ]"),
                                 pname.c_str(), ptype.c_str(), psz);
                            ++pi;
                        }
                    } catch (...) {}
                    ++hits;
                }
            } catch (...) {}
            VLOG(STR("[MoriaCppMod] [PathX-T] === end ({} hits / {} total functions in chain) ===\n"),
                 hits, total);
        }

        // [rc.53 PROBE U 2026-06-15] IMorSaveGameObject diagnostic. Per
        // header-dump analysis (MorSaveGameObject.h + Moria.hpp lines
        // 11600-11625), the canonical save path is the IMorSaveGameObject
        // interface, NOT StoreRuntimeActor. Save filter = SaveGameObjectIgnore.
        // Persistence key = SaveGameObjectGetId. Dormancy state = GetDormancy.
        // We probe both the live goat AND a nearby crafted chest. Chest is
        // the reference: it ALWAYS persists in vanilla, so whatever its
        // interface returns is what the goat MUST also return.
        void probeSaveIfaceOn(UObject* target, const wchar_t* label)
        {
            if (!target || !isObjectAlive(target))
            {
                VLOG(STR("[MoriaCppMod] [PathX-U] {} target null/stale — skip\n"), label);
                return;
            }
            UClass* cls = nullptr;
            try { cls = target->GetClassPrivate(); } catch (...) {}
            std::wstring clsName = cls ? cn_safe(cls) : std::wstring(STR("<no-class>"));
            VLOG(STR("[MoriaCppMod] [PathX-U] {} target={:p} class='{}'\n"),
                 label, (void*)target, clsName.c_str());
            if (!cls)
            {
                VLOG(STR("[MoriaCppMod] [PathX-U] {} no class — skip\n"), label);
                return;
            }

            // Resolve the three UFunctions in the chain
            UFunction* fnIgnore   = nullptr;
            UFunction* fnGetId    = nullptr;
            UFunction* fnGetDorm  = nullptr;
            try {
                for (auto* fn : cls->ForEachFunctionInChain())
                {
                    if (!fn) continue;
                    std::wstring fname; try { fname = fn->GetName(); } catch (...) { continue; }
                    if (fname == STR("SaveGameObjectIgnore"))      fnIgnore  = fn;
                    else if (fname == STR("SaveGameObjectGetId"))  fnGetId   = fn;
                    else if (fname == STR("SaveGameObjectGetDormancy")) fnGetDorm = fn;
                }
            } catch (...) {}

            VLOG(STR("[MoriaCppMod] [PathX-U] {}   resolved: Ignore={:p} GetId={:p} GetDorm={:p}\n"),
                 label, (void*)fnIgnore, (void*)fnGetId, (void*)fnGetDorm);

            // Call SaveGameObjectIgnore — returns bool
            if (fnIgnore)
            {
                int psz = 0; try { psz = fnIgnore->GetParmsSize(); } catch (...) {}
                std::vector<uint8_t> buf(psz > 0 ? psz : 1, 0);
                if (safeProcessEvent(target, fnIgnore, buf.data()) && psz >= 1)
                {
                    bool ignore = (buf[psz - 1] != 0);
                    VLOG(STR("[MoriaCppMod] [PathX-U] {}   SaveGameObjectIgnore() = {} ({})\n"),
                         label,
                         ignore ? STR("TRUE") : STR("FALSE"),
                         ignore ? STR("*** SAVE SYSTEM SKIPS THIS ACTOR ***") : STR("(saveable)"));
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PathX-U] {}   SaveGameObjectIgnore() FAILED to invoke (psz={})\n"),
                         label, psz);
                }
            }

            // Call SaveGameObjectGetId — returns FMorSaveGameObjectId (opaque struct).
            // Allocate a generous parms buffer; hex-dump the return region.
            if (fnGetId)
            {
                int psz = 0; try { psz = fnGetId->GetParmsSize(); } catch (...) {}
                std::vector<uint8_t> buf(psz > 0 ? psz : 64, 0);
                if (safeProcessEvent(target, fnGetId, buf.data()))
                {
                    // Hex-dump full parms buffer (return value is in there)
                    std::wstring hex;
                    bool allZero = true;
                    for (int i = 0; i < psz; ++i)
                    {
                        if (buf[i] != 0) allZero = false;
                        wchar_t tmp[8]; std::swprintf(tmp, 8, L"%02X ", buf[i]);
                        hex += tmp;
                        if ((i + 1) % 16 == 0) hex += L"\n[MoriaCppMod] [PathX-U]                ";
                    }
                    VLOG(STR("[MoriaCppMod] [PathX-U] {}   SaveGameObjectGetId() psz={} {} bytes:\n[MoriaCppMod] [PathX-U]                {}\n"),
                         label, psz,
                         allZero ? STR("*** ALL ZERO — NO PERSISTENT ID ***") : STR("(non-zero)"),
                         hex.c_str());
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PathX-U] {}   SaveGameObjectGetId() FAILED to invoke (psz={})\n"),
                         label, psz);
                }
            }

            // Call SaveGameObjectGetDormancy — returns EMorSaveGameObjectDormancyState (byte enum)
            if (fnGetDorm)
            {
                int psz = 0; try { psz = fnGetDorm->GetParmsSize(); } catch (...) {}
                std::vector<uint8_t> buf(psz > 0 ? psz : 1, 0);
                if (safeProcessEvent(target, fnGetDorm, buf.data()) && psz >= 1)
                {
                    uint8_t dorm = buf[psz - 1];
                    VLOG(STR("[MoriaCppMod] [PathX-U] {}   SaveGameObjectGetDormancy() = {} (raw byte)\n"),
                         label, (int)dorm);
                }
            }
        }

        void runProbeU()
        {
            VLOG(STR("[MoriaCppMod] [PathX-U] === Probe U: IMorSaveGameObject diagnostic ===\n"));

            // Goat — find via FindAllOf on BP_NpcGoat_C
            UClass* goatCls = m_goatBPClass && isObjectAlive(m_goatBPClass) ? m_goatBPClass : nullptr;
            if (!goatCls)
            {
                goatCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Game/Character/NpcGoat/BP_NpcGoat.BP_NpcGoat_C"));
            }
            UObject* goatTarget = nullptr;
            {
                std::vector<UObject*> found;
                if (seh_findAllOf(STR("BP_NpcGoat_C"), &found))
                {
                    for (auto* o : found)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        // skip class objects — we want instances
                        if (o == reinterpret_cast<UObject*>(goatCls)) continue;
                        // also skip CDO
                        if (goatCls && o == goatCls->GetClassDefaultObject()) continue;
                        goatTarget = o; break;
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-U] BP_NpcGoat_C find count={} → using={:p}\n"),
                     (int)found.size(), (void*)goatTarget);
            }
            if (goatTarget) probeSaveIfaceOn(goatTarget, STR("[GOAT ]"));

            // Chest — find via FindAllOf on BP_StorageChest_Construction_C
            UClass* chestCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Game/Construction/Storage/BP_StorageChest_Construction.BP_StorageChest_Construction_C"));
            // Fallback paths if the canonical path didn't match
            if (!chestCls)
            {
                chestCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Game/Buildables/Storage/BP_StorageChest_Construction.BP_StorageChest_Construction_C"));
            }
            UObject* chestTarget = nullptr;
            {
                std::vector<UObject*> found;
                if (seh_findAllOf(STR("BP_StorageChest_Construction_C"), &found))
                {
                    for (auto* o : found)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        if (chestCls && o == reinterpret_cast<UObject*>(chestCls)) continue;
                        if (chestCls && o == chestCls->GetClassDefaultObject()) continue;
                        chestTarget = o; break;
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-U] BP_StorageChest_Construction_C find count={} → using={:p}\n"),
                     (int)found.size(), (void*)chestTarget);
            }
            if (chestTarget) probeSaveIfaceOn(chestTarget, STR("[CHEST]"));

            // If chest not found, try BP_Hearth_Base_C as a fallback known-persister
            if (!chestTarget)
            {
                std::vector<UObject*> found;
                if (seh_findAllOf(STR("BP_Hearth_Base_C"), &found))
                {
                    for (auto* o : found)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        chestTarget = o; break;
                    }
                }
                VLOG(STR("[MoriaCppMod] [PathX-U] Fallback BP_Hearth_Base_C find count={} → using={:p}\n"),
                     (int)found.size(), (void*)chestTarget);
                if (chestTarget) probeSaveIfaceOn(chestTarget, STR("[HEARTH]"));
            }

            VLOG(STR("[MoriaCppMod] [PathX-U] === Probe U END ===\n"));
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
        struct PVTArrayHdr {
            void* Data;
            int32_t Num;
            int32_t Max;
        };
        struct PVStringHdr {
            wchar_t* Data;
            int32_t Num;
            int32_t Max;
        };

        // SEH-safe FString → wchar_t* copy. Returns bytes copied (0 on AV).
        // Can't return std::wstring inside __try (object unwinding rule).
        static int pv_readFStringRaw(const void* fstrBytes, wchar_t* out, int outCap) noexcept
        {
            if (!fstrBytes || !out || outCap <= 0) return 0;
            const PVStringHdr* h = reinterpret_cast<const PVStringHdr*>(fstrBytes);
            __try {
                if (!h->Data || h->Num <= 0) { out[0] = 0; return 0; }
                int len = h->Num - 1; // FString Num includes trailing null
                if (len < 0) { out[0] = 0; return 0; }
                if (len > outCap - 1) len = outCap - 1;
                for (int i = 0; i < len; ++i) out[i] = h->Data[i];
                out[len] = 0;
                return len;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                if (outCap > 0) out[0] = 0;
                return -1;
            }
        }

        std::wstring pv_readFString(const uint8_t* fstrBytes)
        {
            wchar_t buf[2048];
            int r = pv_readFStringRaw(fstrBytes, buf, 2048);
            if (r < 0) return L"<av>";
            return std::wstring(buf);
        }

        // Dump one TArray<FSoftObjectPath>-shaped TArray. Returns whether
        // we found a goat-shaped entry (AssetPathName or SubPath contains "Goat").
        bool pv_dumpSoftPathArray(UObject* mgr, int32_t offset, const wchar_t* label)
        {
            if (!mgr) return false;
            const PVTArrayHdr* arr = reinterpret_cast<const PVTArrayHdr*>(
                reinterpret_cast<const uint8_t*>(mgr) + offset);
            VLOG(STR("[MoriaCppMod] [PathX-V] {} TArray @ +0x{:04X}: Data={:p} Num={} Max={}\n"),
                 label, offset, arr->Data, arr->Num, arr->Max);
            if (!arr->Data || arr->Num <= 0 || arr->Num > 4096)
            {
                VLOG(STR("[MoriaCppMod] [PathX-V] {}   (empty or bogus Num — skip iteration)\n"), label);
                return false;
            }
            bool foundGoat = false;
            constexpr int kStride = 24; // FSoftObjectPath / FSoftClassPath
            for (int i = 0; i < arr->Num; ++i)
            {
                const uint8_t* entry = static_cast<const uint8_t*>(arr->Data) + i * kStride;
                std::wstring assetName = seh_fnameToString(const_cast<uint8_t*>(entry));
                std::wstring subPath   = pv_readFString(entry + 8);
                if (assetName.find(STR("Goat")) != std::wstring::npos
                 || subPath.find(STR("Goat")) != std::wstring::npos)
                {
                    foundGoat = true;
                    VLOG(STR("[MoriaCppMod] [PathX-V] {}   [{}] AssetPathName='{}' SubPath='{}' *** GOAT MATCH ***\n"),
                         label, i, assetName.c_str(), subPath.c_str());
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PathX-V] {}   [{}] AssetPathName='{}' SubPath='{}'\n"),
                         label, i, assetName.c_str(), subPath.c_str());
                }
            }
            return foundGoat;
        }

        void runProbeV()
        {
            VLOG(STR("[MoriaCppMod] [PathX-V] === Probe V: BP_NPCManager_C ValidNpc* dump ===\n"));

            // Step 1: find the manager (same pattern as runNpcMgrProbe)
            UObject* mgr = nullptr;
            UClass* utilsCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MoriaUtils"));
            if (utilsCls && isObjectAlive(utilsCls))
            {
                UObject* utilsCDO = nullptr;
                try { utilsCDO = utilsCls->GetClassDefaultObject(); } catch (...) {}
                if (utilsCDO && isObjectAlive(utilsCDO))
                {
                    auto* getMgrFn = utilsCls->GetFunctionByNameInChain(STR("GetNpcManager"));
                    if (getMgrFn)
                    {
                        std::vector<uint8_t> buf(getMgrFn->GetParmsSize(), 0);
                        auto* pWC = findParam(getMgrFn, STR("WorldContextObject"));
                        if (pWC && m_localPC)
                            *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = m_localPC;
                        try { safeProcessEvent(utilsCDO, getMgrFn, buf.data()); } catch (...) {}
                        auto* pRet = findParam(getMgrFn, STR("ReturnValue"));
                        if (pRet)
                            mgr = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                    }
                }
            }
            if (!mgr || !isObjectAlive(mgr))
            {
                std::vector<UObject*> candidates;
                if (findAllOfSafe(STR("MorNPCManager"), candidates))
                {
                    for (UObject* o : candidates)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring cn = safeClassName(o);
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                        mgr = o; break;
                    }
                }
            }
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [PathX-V] manager not findable — bail\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-V] manager={:p} class={}\n"),
                 (void*)mgr, safeClassName(mgr).c_str());

            // Step 2: TuningData reference (off 0x02b8, 8 bytes, UObject*)
            UObject* tuning = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(mgr) + 0x02b8);
            if (tuning && isObjectAlive(tuning))
            {
                std::wstring tname; try { tname = tuning->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [PathX-V] TuningData @ +0x02b8 = {:p} class='{}' name='{}'\n"),
                     (void*)tuning, safeClassName(tuning).c_str(), tname.c_str());
            }
            else
            {
                VLOG(STR("[MoriaCppMod] [PathX-V] TuningData @ +0x02b8 = null (manager has no tuning data ref)\n"));
            }

            // Step 3: dump the three gating arrays
            bool goatInClasses  = pv_dumpSoftPathArray(mgr, 0x0808, STR("ValidNpcClasses "));
            bool goatInRestores = pv_dumpSoftPathArray(mgr, 0x0818, STR("ValidNpcRestores"));
            // ValidNpcRoles likely a different struct (FName/role enum, not class path) — try stride 16
            {
                const PVTArrayHdr* arr = reinterpret_cast<const PVTArrayHdr*>(
                    reinterpret_cast<const uint8_t*>(mgr) + 0x0828);
                VLOG(STR("[MoriaCppMod] [PathX-V] ValidNpcRoles    TArray @ +0x0828: Data={:p} Num={} Max={}\n"),
                     arr->Data, arr->Num, arr->Max);
                if (arr->Data && arr->Num > 0 && arr->Num <= 256)
                {
                    // Try stride 16 first (FName + something small)
                    for (int i = 0; i < arr->Num && i < 32; ++i)
                    {
                        const uint8_t* entry = static_cast<const uint8_t*>(arr->Data) + i * 16;
                        std::wstring nm = seh_fnameToString(const_cast<uint8_t*>(entry));
                        VLOG(STR("[MoriaCppMod] [PathX-V] ValidNpcRoles     [{}] FName(@+0)='{}'\n"), i, nm.c_str());
                    }
                }
            }

            // Step 4: verdict
            VLOG(STR("[MoriaCppMod] [PathX-V] === VERDICT: goat in ValidNpcClasses={} in ValidNpcRestores={} ===\n"),
                 goatInClasses  ? STR("YES") : STR("NO (*** missing — actor will not restore ***)"),
                 goatInRestores ? STR("YES") : STR("NO"));
            VLOG(STR("[MoriaCppMod] [PathX-V] === Probe V END ===\n"));
        }

        // [rc.55 PROBE W 2026-06-16] Per-entry FMorNpcPersistentData dump.
        // Reflection-driven walk through the inner struct.
        // For each entry in BP_NPCManager_C.NpcInfo.Items[]:
        //   1. Decode PersistentData fields via reflection
        //   2. For RowHandle-shaped 16-byte struct fields, decode
        //      (DataTable* + FName RowName) — the gating fields
        //      UniqueNpc + StaticNpcData + CurrentRole live here.
        //   3. For FGuid / FText / bool / int / float, decode value
        //   4. For everything else, hex-preview first 16 bytes
        //
        // Hypothesis under test: goat[0]'s UniqueNpc or StaticNpcData
        // is empty/unresolvable, while vanilla dwarves have valid
        // row handles. That's the per-entry restore gate.
        void runProbeW_dumpField(const uint8_t* base, int32_t off, FProperty* prop, int depth)
        {
            if (!prop) return;
            std::wstring pn; try { pn = prop->GetName(); } catch (...) {}
            std::wstring ptype; try { ptype = prop->GetClass().GetName(); } catch (...) {}
            int32_t psize = -1; try { psize = prop->GetSize(); } catch (...) {}
            const uint8_t* field = base + off;
            const wchar_t* indent = depth >= 2 ? STR("                ") :
                                   depth == 1 ? STR("        ")           :
                                                STR("    ");

            if (ptype == STR("BoolProperty"))
            {
                bool b = (*field != 0);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Bool   = {}\n"),
                     indent, off, pn.c_str(), b ? STR("true") : STR("false"));
            }
            else if (ptype == STR("IntProperty"))
            {
                int32_t v = *reinterpret_cast<const int32_t*>(field);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Int    = {}\n"),
                     indent, off, pn.c_str(), v);
            }
            else if (ptype == STR("FloatProperty"))
            {
                float f = *reinterpret_cast<const float*>(field);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Float  = {:.3f}\n"),
                     indent, off, pn.c_str(), f);
            }
            else if (ptype == STR("ObjectProperty") || ptype == STR("ClassProperty"))
            {
                UObject* o = *reinterpret_cast<UObject* const*>(field);
                std::wstring oc = (o && isObjectAlive(o)) ? safeClassName(o) : STR("null");
                std::wstring on; try { if (o && isObjectAlive(o)) on = o->GetName(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Obj    = {:p} class='{}' name='{}'\n"),
                     indent, off, pn.c_str(), (void*)o, oc.c_str(), on.c_str());
            }
            else if (ptype == STR("NameProperty"))
            {
                std::wstring nm = seh_fnameToString(const_cast<uint8_t*>(field));
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : FName  = '{}'\n"),
                     indent, off, pn.c_str(), nm.c_str());
            }
            else if (ptype == STR("StrProperty"))
            {
                std::wstring s = pv_readFString(field);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : FStr   = '{}'\n"),
                     indent, off, pn.c_str(), s.c_str());
            }
            else if (ptype == STR("TextProperty"))
            {
                wchar_t tbuf[256]; seh_ftextToStringToBuf(const_cast<uint8_t*>(field), tbuf, 256);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : FText  = '{}'\n"),
                     indent, off, pn.c_str(), tbuf);
            }
            else if (ptype == STR("StructProperty"))
            {
                auto* sp = CastField<FStructProperty>(prop);
                UStruct* inner = sp ? sp->GetStruct() : nullptr;
                std::wstring sname; try { if (inner) sname = inner->GetName(); } catch (...) {}
                // RowHandle: (UObject* DataTable @+0, FName RowName @+8) → 16 bytes
                bool isRowHandle =
                    (sname == STR("DataTableRowHandle") ||
                     sname == STR("MorUniqueNpcRowHandle") ||
                     sname == STR("MorNPCRoleRowHandle") ||
                     sname == STR("MorNPCTraitRowHandle") ||
                     sname == STR("MorNPCActivityRowHandle"));
                if (isRowHandle && psize >= 16)
                {
                    UObject* dt = *reinterpret_cast<UObject* const*>(field);
                    std::wstring dtName = (dt && isObjectAlive(dt)) ? std::wstring(dt->GetName()) : std::wstring(STR("null"));
                    std::wstring rn = seh_fnameToString(const_cast<uint8_t*>(field + 8));
                    bool empty = (!dt && rn.empty());
                    VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : {} DT={} RowName='{}'{}\n"),
                         indent, off, pn.c_str(), sname.c_str(),
                         dtName.c_str(), rn.empty() ? STR("(none)") : rn.c_str(),
                         empty ? STR("  *** EMPTY ***") : STR(""));
                }
                else if (sname == STR("Guid") && psize >= 16)
                {
                    const uint32_t* g = reinterpret_cast<const uint32_t*>(field);
                    VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : FGuid  = {:08X}-{:08X}-{:08X}-{:08X}\n"),
                         indent, off, pn.c_str(), g[0], g[1], g[2], g[3]);
                }
                else if (sname == STR("Vector") && psize >= 12)
                {
                    const float* v = reinterpret_cast<const float*>(field);
                    VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : FVec   = ({:.1f},{:.1f},{:.1f})\n"),
                         indent, off, pn.c_str(), v[0], v[1], v[2]);
                }
                else if (depth < 2 && inner)
                {
                    // Recurse one level into the struct
                    VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Struct '{}' size={}  (recurse)\n"),
                         indent, off, pn.c_str(), sname.c_str(), psize);
                    try {
                        for (auto* sub : inner->ForEachPropertyInChain())
                        {
                            if (!sub) continue;
                            int32_t soff = -1; try { soff = sub->GetOffset_Internal(); } catch (...) {}
                            runProbeW_dumpField(field, soff, sub, depth + 1);
                        }
                    } catch (...) {}
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : Struct '{}' size={}\n"),
                         indent, off, pn.c_str(), sname.c_str(), psize);
                }
            }
            else if (ptype == STR("ArrayProperty"))
            {
                const PVTArrayHdr* arr = reinterpret_cast<const PVTArrayHdr*>(field);
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : TArray Num={} Max={} Data={:p}\n"),
                     indent, off, pn.c_str(), arr->Num, arr->Max, arr->Data);
            }
            else
            {
                // Fall back to hex preview
                int dump = psize > 0 && psize < 24 ? psize : 16;
                std::wstring hex;
                for (int i = 0; i < dump; ++i)
                {
                    wchar_t tmp[8]; std::swprintf(tmp, 8, L"%02X ", field[i]);
                    hex += tmp;
                }
                VLOG(STR("[MoriaCppMod] [PathX-W] {}+0x{:04X} {:32} : {} size={} hex={}\n"),
                     indent, off, pn.c_str(), ptype.c_str(), psize, hex.c_str());
            }
        }

        void runProbeW()
        {
            VLOG(STR("[MoriaCppMod] [PathX-W] === Probe W: per-entry FMorNpcPersistentData dump ===\n"));

            // Step 1: find the manager (same pattern as Probe V)
            UObject* mgr = nullptr;
            UClass* utilsCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Moria.MoriaUtils"));
            if (utilsCls && isObjectAlive(utilsCls))
            {
                UObject* utilsCDO = nullptr;
                try { utilsCDO = utilsCls->GetClassDefaultObject(); } catch (...) {}
                if (utilsCDO && isObjectAlive(utilsCDO))
                {
                    auto* getMgrFn = utilsCls->GetFunctionByNameInChain(STR("GetNpcManager"));
                    if (getMgrFn)
                    {
                        std::vector<uint8_t> buf(getMgrFn->GetParmsSize(), 0);
                        auto* pWC = findParam(getMgrFn, STR("WorldContextObject"));
                        if (pWC && m_localPC)
                            *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = m_localPC;
                        try { safeProcessEvent(utilsCDO, getMgrFn, buf.data()); } catch (...) {}
                        auto* pRet = findParam(getMgrFn, STR("ReturnValue"));
                        if (pRet)
                            mgr = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                    }
                }
            }
            if (!mgr || !isObjectAlive(mgr))
            {
                std::vector<UObject*> candidates;
                if (findAllOfSafe(STR("MorNPCManager"), candidates))
                {
                    for (UObject* o : candidates)
                    {
                        if (!o || !isObjectAlive(o)) continue;
                        std::wstring cn = safeClassName(o);
                        if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                        mgr = o; break;
                    }
                }
            }
            if (!mgr || !isObjectAlive(mgr))
            {
                VLOG(STR("[MoriaCppMod] [PathX-W] manager not findable — bail\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-W] manager={:p} class={}\n"),
                 (void*)mgr, safeClassName(mgr).c_str());

            // Step 2: reflection — find PersistentData offset within FMorNPCInfo + its struct
            UClass* mgrCls = nullptr;
            try { mgrCls = mgr->GetClassPrivate(); } catch (...) {}
            if (!mgrCls)
            {
                VLOG(STR("[MoriaCppMod] [PathX-W] manager class null — bail\n"));
                return;
            }
            int32_t offPersistentData = -1;
            UStruct* persistentDataStruct = nullptr;
            int32_t itemStride = 0x0260;
            try {
                auto* niProp = mgrCls->GetPropertyByNameInChain(STR("NpcInfo"));
                auto* niStructProp = CastField<FStructProperty>(niProp);
                if (niStructProp)
                {
                    UStruct* niStruct = niStructProp->GetStruct();
                    if (niStruct)
                    {
                        auto* itemsProp = niStruct->GetPropertyByNameInChain(STR("Items"));
                        auto* itemsArrProp = CastField<FArrayProperty>(itemsProp);
                        if (itemsArrProp)
                        {
                            auto* innerStructProp = CastField<FStructProperty>(itemsArrProp->GetInner());
                            if (innerStructProp)
                            {
                                UStruct* fMorNpcInfo = innerStructProp->GetStruct();
                                if (auto* ss = static_cast<UScriptStruct*>(fMorNpcInfo))
                                    itemStride = static_cast<int32_t>(ss->GetStructureSize());
                                if (fMorNpcInfo)
                                {
                                    auto* pdProp = fMorNpcInfo->GetPropertyByNameInChain(STR("PersistentData"));
                                    auto* pdStructProp = CastField<FStructProperty>(pdProp);
                                    if (pdStructProp)
                                    {
                                        offPersistentData = pdProp->GetOffset_Internal();
                                        persistentDataStruct = pdStructProp->GetStruct();
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {}

            VLOG(STR("[MoriaCppMod] [PathX-W] reflection: itemStride=0x{:04X}, PersistentData offset=0x{:04X}, struct={:p}\n"),
                 itemStride, offPersistentData, (void*)persistentDataStruct);
            if (offPersistentData < 0 || !persistentDataStruct)
            {
                VLOG(STR("[MoriaCppMod] [PathX-W] could not resolve PersistentData via reflection — bail\n"));
                return;
            }

            // Step 3: walk NpcInfo.Items, dump every entry
            uint8_t* mgrBase = reinterpret_cast<uint8_t*>(mgr);
            uint8_t* arrayHeader = mgrBase + 0x03a0 + 0x0108;
            uint8_t* itemsData = *reinterpret_cast<uint8_t**>(arrayHeader);
            int32_t itemsNum   = *reinterpret_cast<int32_t*>(arrayHeader + 8);
            VLOG(STR("[MoriaCppMod] [PathX-W] NpcInfo.Items: Data={:p} Num={}\n"),
                 (void*)itemsData, itemsNum);
            if (!itemsData || itemsNum <= 0 || itemsNum > 256)
            {
                VLOG(STR("[MoriaCppMod] [PathX-W] (empty or bogus Num — skip)\n"));
                return;
            }

            for (int i = 0; i < itemsNum; ++i)
            {
                uint8_t* entry = itemsData + (int64_t)i * itemStride;
                uint8_t* persistentData = entry + offPersistentData;
                VLOG(STR("[MoriaCppMod] [PathX-W] ━━━ Entry [{}] item@{:p} PersistentData@{:p} ━━━\n"),
                     i, (void*)entry, (void*)persistentData);
                try {
                    for (auto* p : persistentDataStruct->ForEachPropertyInChain())
                    {
                        if (!p) continue;
                        int32_t off = -1;
                        try { off = p->GetOffset_Internal(); } catch (...) {}
                        if (off < 0) continue;
                        runProbeW_dumpField(persistentData, off, p, 0);
                    }
                } catch (...) {
                    VLOG(STR("[MoriaCppMod] [PathX-W]   (exception during entry [{}] walk)\n"), i);
                }
            }
            VLOG(STR("[MoriaCppMod] [PathX-W] === Probe W END ===\n"));
        }

        // [rc.56 PROBE X 2026-06-16] NPCUnique row enumeration.
        // Scans every loaded UDataTable, finds the one whose RowStruct
        // is MorUniqueNPCDefinition. Iterates rows, decodes per-row:
        //   - CharacterName (FText)
        //   - CharacterClass (TSoftClassPtr — first 24 bytes are
        //     FSoftObjectPath: FName AssetPathName + FString SubPath)
        // Logs the row whose CharacterClass softpath contains "Goat"
        // as the *** GOAT ROW *** — that row name is the exact string
        // rc.57 writes into UniqueNpc.RowName at bell-spawn.
        void runProbeX()
        {
            VLOG(STR("[MoriaCppMod] [PathX-X] === Probe X: NPCUnique row enumeration ===\n"));

            // Step 1: find every UDataTable whose RowStruct is MorUniqueNPCDefinition
            std::vector<UObject*> tables;
            findAllOfSafe(STR("DataTable"), tables);
            VLOG(STR("[MoriaCppMod] [PathX-X] found {} live DataTables\n"), (int)tables.size());

            UObject* targetDT = nullptr;
            std::wstring targetName;
            for (auto* dt : tables)
            {
                if (!dt || !isObjectAlive(dt)) continue;
                std::wstring dtName; try { dtName = dt->GetName(); } catch (...) { continue; }
                // Skip CDOs / archetypes
                if (dtName.size() >= 9 && dtName.substr(0,9) == STR("Default__")) continue;

                int rsOff = -2;
                resolveOffset(dt, L"RowStruct", rsOff);
                if (rsOff < 0) continue;
                auto* base = reinterpret_cast<uint8_t*>(dt);
                if (!isReadableMemory(base + rsOff, 8)) continue;
                UStruct* rs = *reinterpret_cast<UStruct**>(base + rsOff);
                if (!rs || !isObjectAlive(rs)) continue;
                std::wstring rsName; try { rsName = rs->GetName(); } catch (...) { continue; }
                if (rsName == STR("MorUniqueNPCDefinition"))
                {
                    targetDT = dt;
                    targetName = dtName;
                    break;
                }
            }

            if (!targetDT)
            {
                VLOG(STR("[MoriaCppMod] [PathX-X] *** No DataTable with RowStruct=MorUniqueNPCDefinition found — table may not be loaded yet ***\n"));
                return;
            }
            VLOG(STR("[MoriaCppMod] [PathX-X] *** Target DT='{}' (ptr={:p}) ***\n"),
                 targetName.c_str(), (void*)targetDT);

            // Step 2: bind via DataTableUtil + resolve field offsets
            DataTableUtil dt;
            dt.bindFromObject(targetDT, targetName.c_str());
            if (!dt.isBound() || !dt.rowStruct)
            {
                VLOG(STR("[MoriaCppMod] [PathX-X] bindFromObject failed or rowStruct null\n"));
                return;
            }

            int offCharName  = dt.resolvePropertyOffset(STR("CharacterName"));
            int offCharClass = dt.resolvePropertyOffset(STR("CharacterClass"));
            int offAppearance= dt.resolvePropertyOffset(STR("AppearancePreset"));
            VLOG(STR("[MoriaCppMod] [PathX-X] field offsets: CharacterName=0x{:04X} CharacterClass=0x{:04X} AppearancePreset=0x{:04X}\n"),
                 offCharName, offCharClass, offAppearance);

            if (offCharClass < 0)
            {
                VLOG(STR("[MoriaCppMod] [PathX-X] CharacterClass field not resolvable — bail\n"));
                return;
            }

            // Step 3: iterate rows
            std::vector<std::wstring> rowNames = dt.getRowNames();
            VLOG(STR("[MoriaCppMod] [PathX-X] {} rows\n"), (int)rowNames.size());

            int goatHits = 0;
            for (auto& rn : rowNames)
            {
                uint8_t* row = dt.findRowData(rn.c_str());
                if (!row) continue;

                // CharacterName (FText, 24 bytes — read via existing helper)
                wchar_t nameBuf[256] = L"";
                if (offCharName >= 0)
                    seh_ftextToStringToBuf(row + offCharName, nameBuf, 256);

                // CharacterClass — TSoftClassPtr<AMorCharacter>
                // Layout starts with FSoftObjectPath (24 bytes):
                //   FName AssetPathName (8 bytes)
                //   FString SubPathString (16 bytes)
                std::wstring assetPath = seh_fnameToString(row + offCharClass);
                std::wstring subPath   = pv_readFString(row + offCharClass + 8);

                bool isGoatRow =
                    (assetPath.find(STR("Goat")) != std::wstring::npos) ||
                    (assetPath.find(STR("goat")) != std::wstring::npos) ||
                    (subPath.find(STR("Goat"))   != std::wstring::npos);

                std::wstring marker = isGoatRow ? STR("  *** GOAT ROW — write this RowName='") + rn + STR("' ***") : STR("");
                VLOG(STR("[MoriaCppMod] [PathX-X] Row='{}' CharacterName='{}' CharacterClass='{}{}{}'{}\n"),
                     rn.c_str(), nameBuf,
                     assetPath.c_str(),
                     subPath.empty() ? STR("") : STR(":"),
                     subPath.c_str(),
                     marker.c_str());

                if (isGoatRow) ++goatHits;
            }

            VLOG(STR("[MoriaCppMod] [PathX-X] === VERDICT: {} goat row(s) found ===\n"), goatHits);
            VLOG(STR("[MoriaCppMod] [PathX-X] === Probe X END ===\n"));
        }

        // [rc.57 PROBE Y 2026-06-23] Wider net. Three passes:
        //   Pass 1: re-decode DT_NPCUniqueCharacters with FIXED
        //           CharacterClass offset (+16 within TSoftClassPtr)
        //   Pass 2: iterate ALL DataTables with RowStruct=MorUniqueNPCDefinition
        //   Pass 3: brute-force scan EVERY DataTable's RowMap for any
        //           FName containing "NpcGoat" — catches the goat row
        //           in any table regardless of row struct
        void probeY_decodeOneDT(UObject* dt, const wchar_t* dtName)
        {
            DataTableUtil util;
            util.bindFromObject(dt, dtName);
            if (!util.isBound() || !util.rowStruct) return;

            int offCharName  = util.resolvePropertyOffset(STR("CharacterName"));
            int offCharClass = util.resolvePropertyOffset(STR("CharacterClass"));

            std::vector<std::wstring> rowNames = util.getRowNames();
            VLOG(STR("[MoriaCppMod] [PathX-Y]   table '{}' has {} rows (CharacterName=+0x{:04X} CharacterClass=+0x{:04X})\n"),
                 dtName, (int)rowNames.size(), offCharName, offCharClass);

            int goatHits = 0;
            for (auto& rn : rowNames)
            {
                uint8_t* row = util.findRowData(rn.c_str());
                if (!row) continue;

                wchar_t nameBuf[256] = L"";
                if (offCharName >= 0)
                    seh_ftextToStringToBuf(row + offCharName, nameBuf, 256);

                // FIXED: TSoftClassPtr layout per UE4.27:
                //   +0  WeakPtr (8)
                //   +8  TagAtLastTest (4) + pad (4)
                //   +16 ObjectID.AssetPathName (FName)    ← READ HERE
                //   +24 ObjectID.SubPathString (FString)
                std::wstring assetPath, subPath;
                if (offCharClass >= 0)
                {
                    assetPath = seh_fnameToString(row + offCharClass + 16);
                    subPath   = pv_readFString(row + offCharClass + 24);
                }

                bool isGoatRow =
                    (assetPath.find(STR("Goat")) != std::wstring::npos) ||
                    (assetPath.find(STR("goat")) != std::wstring::npos) ||
                    (subPath.find(STR("Goat"))   != std::wstring::npos);

                std::wstring marker = isGoatRow
                    ? (STR("  *** GOAT ROW — write this RowName='") + rn + STR("' ***"))
                    : STR("");
                VLOG(STR("[MoriaCppMod] [PathX-Y]     Row='{}' CharacterName='{}' CharacterClass='{}{}{}'{}\n"),
                     rn.c_str(), nameBuf,
                     assetPath.c_str(),
                     subPath.empty() ? STR("") : STR(":"),
                     subPath.c_str(),
                     marker.c_str());
                if (isGoatRow) ++goatHits;
            }
            VLOG(STR("[MoriaCppMod] [PathX-Y]   table '{}' goat row count = {}\n"), dtName, goatHits);
        }

        void runProbeY()
        {
            VLOG(STR("[MoriaCppMod] [PathX-Y] === Probe Y: wider net for goat row ===\n"));

            std::vector<UObject*> tables;
            findAllOfSafe(STR("DataTable"), tables);
            VLOG(STR("[MoriaCppMod] [PathX-Y] {} live DataTables\n"), (int)tables.size());

            // Pass 1+2: find ALL tables with MorUniqueNPCDefinition row struct
            VLOG(STR("[MoriaCppMod] [PathX-Y] --- Pass 1+2: all MorUniqueNPCDefinition tables (FIXED CharacterClass offset) ---\n"));
            int npcUniqueTables = 0;
            for (auto* dt : tables)
            {
                if (!dt || !isObjectAlive(dt)) continue;
                std::wstring dtName; try { dtName = dt->GetName(); } catch (...) { continue; }
                if (dtName.size() >= 9 && dtName.substr(0,9) == STR("Default__")) continue;

                int rsOff = -2;
                resolveOffset(dt, L"RowStruct", rsOff);
                if (rsOff < 0) continue;
                auto* base = reinterpret_cast<uint8_t*>(dt);
                if (!isReadableMemory(base + rsOff, 8)) continue;
                UStruct* rs = *reinterpret_cast<UStruct**>(base + rsOff);
                if (!rs || !isObjectAlive(rs)) continue;
                std::wstring rsName; try { rsName = rs->GetName(); } catch (...) { continue; }
                if (rsName != STR("MorUniqueNPCDefinition")) continue;

                ++npcUniqueTables;
                VLOG(STR("[MoriaCppMod] [PathX-Y] table #{} '{}' (ptr={:p})\n"),
                     npcUniqueTables, dtName.c_str(), (void*)dt);
                probeY_decodeOneDT(dt, dtName.c_str());
            }
            VLOG(STR("[MoriaCppMod] [PathX-Y] --- Pass 1+2 end: {} MorUniqueNPCDefinition tables ---\n"), npcUniqueTables);

            // Pass 3: brute-force scan EVERY DataTable's row names + row
            // bytes for any FName containing "NpcGoat" or "BP_NpcGoat".
            // Catches the goat row regardless of row struct type.
            VLOG(STR("[MoriaCppMod] [PathX-Y] --- Pass 3: brute-force scan for 'NpcGoat' in any DataTable ---\n"));
            int totalTablesScanned = 0;
            int totalHits = 0;
            for (auto* dt : tables)
            {
                if (!dt || !isObjectAlive(dt)) continue;
                std::wstring dtName; try { dtName = dt->GetName(); } catch (...) { continue; }
                if (dtName.size() >= 9 && dtName.substr(0,9) == STR("Default__")) continue;

                DataTableUtil util;
                util.bindFromObject(dt, dtName.c_str());
                if (!util.isBound()) continue;
                ++totalTablesScanned;

                // Get row map header to walk row keys + row data pointers
                DataTableUtil::RowMapHeader hdr{};
                if (!util.getRowMapHeader(hdr)) continue;
                if (hdr.Num <= 0 || hdr.Num > 100000) continue;

                int rowSize = util.rowSize > 0 ? util.rowSize : 0;
                int hitRows = 0;
                for (int i = 0; i < hdr.Num; ++i)
                {
                    uint8_t* elem = hdr.Data + i * DataTableUtil::SET_ELEMENT_SIZE;
                    if (!isReadableMemory(elem, DataTableUtil::SET_ELEMENT_SIZE)) continue;
                    uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + DataTableUtil::FNAME_SIZE);
                    if (!rowData || rowSize <= 0 || !isReadableMemory(rowData, rowSize)) continue;

                    // Sweep every 8-byte aligned position in the row,
                    // try interpreting as FName, check if it decodes
                    // to something containing "NpcGoat" or "BP_NpcGoat".
                    bool sawGoatRefInRow = false;
                    for (int j = 0; j + 8 <= rowSize; j += 8)
                    {
                        std::wstring fname = seh_fnameToString(rowData + j);
                        if (fname.empty()) continue;
                        if (fname.find(STR("NpcGoat")) != std::wstring::npos
                         || fname.find(STR("BP_NpcGoat")) != std::wstring::npos)
                        {
                            // Decode row name
                            FName rowNameFn;
                            std::memcpy(&rowNameFn, elem, DataTableUtil::FNAME_SIZE);
                            std::wstring rowName;
                            try { rowName = rowNameFn.ToString(); } catch (...) {}

                            VLOG(STR("[MoriaCppMod] [PathX-Y]   *** HIT: table='{}' rowName='{}' off=+0x{:04X} FName='{}' ***\n"),
                                 dtName.c_str(), rowName.c_str(), j, fname.c_str());
                            sawGoatRefInRow = true;
                            ++totalHits;
                            break; // one hit per row is enough
                        }
                    }
                }
                if (hitRows > 0)
                {
                    VLOG(STR("[MoriaCppMod] [PathX-Y]   table='{}' had {} goat-referencing rows\n"),
                         dtName.c_str(), hitRows);
                }
            }
            VLOG(STR("[MoriaCppMod] [PathX-Y] --- Pass 3 end: scanned {} tables, {} total goat-referencing rows ---\n"),
                 totalTablesScanned, totalHits);

            VLOG(STR("[MoriaCppMod] [PathX-Y] === Probe Y END ===\n"));
        }

        // EPHEMERAL GOAT: no load-time goat handling. (The AutoRestore /
        // stray-sweep that lived here served only the dev test worlds with
        // leftover NpcInfo markers; removed 2026-07-13 per user.)
        void autoRestoreGoatsFromMarker()
        {
        }

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
                        struct { UObject* Ret; } op{};
                        if (safeProcessEvent(item, getOwnerFn, &op)) owner = op.Ret;
                    }
                    if (owner != goat) continue;

                    if (auto* destFn = item->GetFunctionByNameInChain(STR("K2_DestroyActor")))
                    {
                        safeProcessEvent(item, destFn, nullptr);
                        ++destroyedCount;
                        VLOG(STR("[MoriaCppMod] [TameGoat] destroyed dwarven inventory child '{}' @{:p}\n"),
                             clsName, (void*)item);
                    }
                }
            }
            VLOG(STR("[MoriaCppMod] [TameGoat] destroyed {} dwarven inventory child actors\n"), destroyedCount);

            // === Step 2: Stop behavior tree on the goat's AI controller ===
            UObject* ctrl = nullptr;
            if (auto* getCtrlFn = goat->GetFunctionByNameInChain(STR("GetController")))
            {
                struct { UObject* Ret; } cp{};
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
                UClass* brainCls = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/AIModule.BrainComponent"));
                UObject* brain = nullptr;
                if (brainCls)
                {
                    if (auto* getCompFn = ctrl->GetFunctionByNameInChain(STR("GetComponentByClass")))
                    {
                        struct { UClass* C; UObject* Ret; } bp{};
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
                                *reinterpret_cast<void**>(p + 0)  = rbuf;
                                *reinterpret_cast<int32_t*>(p + 8)  = rlen;
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

        // [rc.50 WRITE GOAT NAME 2026-06-14] Speculative name-write at
        // bell-spawn. Tries candidate UFunctions in priority order on
        // the goat actor. Auto-detects parm type (FText / FString /
        // FName) and packs accordingly. Logs success/failure for each.
        bool writeGoatNameToEntry(UObject* goat, const std::wstring& name)
        {
            if (!goat || !isObjectAlive(goat)) return false;
            UClass* cls = nullptr;
            try { cls = goat->GetClassPrivate(); } catch (...) {}
            if (!cls) return false;

            // Candidate UFunction names in priority order
            static const wchar_t* candidates[] = {
                STR("SetCustomDisplayName"),
                STR("SetCustomName"),
                STR("SetDisplayName"),
                STR("SetNpcName"),
                STR("RenameNpc"),
                STR("Rename"),
                STR("SetName"),
                nullptr
            };

            for (const wchar_t** c = candidates; *c; ++c)
            {
                UFunction* fn = nullptr;
                try { fn = goat->GetFunctionByNameInChain(*c); } catch (...) { continue; }
                if (!fn)
                {
                    VLOG(STR("[MoriaCppMod] [NameWrite] '{}' not found, trying next\n"), *c);
                    continue;
                }
                int parmsSize = 0;
                try { parmsSize = fn->GetParmsSize(); } catch (...) {}
                VLOG(STR("[MoriaCppMod] [NameWrite] '{}' found (ParmsSize={}) — attempting call\n"),
                     *c, parmsSize);

                // Find first non-return param
                FProperty* pFirst = nullptr;
                try {
                    for (auto* p : fn->ForEachProperty())
                    {
                        if (!p) continue;
                        uint64_t pflags = 0;
                        try { pflags = static_cast<uint64_t>(p->GetPropertyFlags()); } catch (...) {}
                        bool isParm = (pflags & 0x0000000000000080ULL) != 0;
                        bool isReturn = (pflags & 0x0000000000000400ULL) != 0;
                        if (isParm && !isReturn) { pFirst = p; break; }
                    }
                } catch (...) {}
                if (!pFirst)
                {
                    VLOG(STR("[MoriaCppMod] [NameWrite] '{}' has no input parm — trying next\n"), *c);
                    continue;
                }

                std::wstring ptype; try { ptype = pFirst->GetClass().GetName(); } catch (...) {}
                std::vector<uint8_t> parms(parmsSize, 0);
                uint8_t* parmBytes = parms.data() + pFirst->GetOffset_Internal();
                bool packed = false;

                if (ptype == STR("StrProperty"))
                {
                    // FString = { wchar_t* Data; int32 Num; int32 Max; } = 16 bytes
                    int32_t strLen = static_cast<int32_t>(name.size()) + 1;
                    void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
                    if (strBuf)
                    {
                        wmemcpy(static_cast<wchar_t*>(strBuf), name.c_str(), strLen);
                        *reinterpret_cast<void**>   (parmBytes + 0)  = strBuf;
                        *reinterpret_cast<int32_t*> (parmBytes + 8)  = strLen;
                        *reinterpret_cast<int32_t*> (parmBytes + 12) = strLen;
                        packed = true;
                    }
                }
                else if (ptype == STR("NameProperty"))
                {
                    // FName = { uint32 ComparisonIndex; uint32 Number; } = 8 bytes
                    RC::Unreal::FName nameId(name.c_str(), RC::Unreal::FNAME_Add);
                    std::memcpy(parmBytes, &nameId, sizeof(RC::Unreal::FName));
                    packed = true;
                }
                else if (ptype == STR("TextProperty"))
                {
                    // FText is 24 bytes via UE4SS: TSharedPtr to FTextData + flags.
                    // Construct via KismetTextLibrary.Conv_StringToText, then memcpy.
                    UObject* ktl = UObjectGlobals::StaticFindObject<UObject*>(
                        nullptr, nullptr, STR("/Script/Engine.Default__KismetTextLibrary"));
                    if (!ktl)
                    {
                        VLOG(STR("[MoriaCppMod] [NameWrite] KismetTextLibrary CDO not found for FText path — trying next\n"));
                        continue;
                    }
                    UFunction* convFn = nullptr;
                    try { convFn = ktl->GetFunctionByNameInChain(STR("Conv_StringToText")); } catch (...) {}
                    if (!convFn)
                    {
                        VLOG(STR("[MoriaCppMod] [NameWrite] Conv_StringToText not found — trying next\n"));
                        continue;
                    }
                    int csz = convFn->GetParmsSize();
                    std::vector<uint8_t> cparms(csz, 0);
                    auto* pStr = findParam(convFn, STR("InString"));
                    auto* pRet = findParam(convFn, STR("ReturnValue"));
                    if (!pStr || !pRet)
                    {
                        VLOG(STR("[MoriaCppMod] [NameWrite] Conv_StringToText parms not resolvable — trying next\n"));
                        continue;
                    }
                    int32_t strLen = static_cast<int32_t>(name.size()) + 1;
                    void* strBuf = FMemory::Malloc(strLen * sizeof(wchar_t), 8);
                    if (!strBuf)
                    {
                        VLOG(STR("[MoriaCppMod] [NameWrite] FString alloc failed — trying next\n"));
                        continue;
                    }
                    wmemcpy(static_cast<wchar_t*>(strBuf), name.c_str(), strLen);
                    uint8_t* sP = cparms.data() + pStr->GetOffset_Internal();
                    *reinterpret_cast<void**>   (sP + 0)  = strBuf;
                    *reinterpret_cast<int32_t*> (sP + 8)  = strLen;
                    *reinterpret_cast<int32_t*> (sP + 12) = strLen;
                    try { safeProcessEvent(ktl, convFn, cparms.data()); }
                    catch (...) { continue; }
                    // FText returned; memcpy from cparms[+pRet] into parmBytes
                    int retSz = pRet->GetSize();
                    std::memcpy(parmBytes, cparms.data() + pRet->GetOffset_Internal(), retSz);
                    packed = true;
                    VLOG(STR("[MoriaCppMod] [NameWrite] Constructed FText via Conv_StringToText (size={} bytes)\n"), retSz);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [NameWrite] '{}' parm type '{}' unsupported — trying next\n"),
                         *c, ptype.c_str());
                    continue;
                }

                if (!packed) continue;

                int32_t preNum = readNpcInfoItemsNum();
                try { safeProcessEvent(goat, fn, parms.data()); }
                catch (...)
                {
                    VLOG(STR("[MoriaCppMod] [NameWrite] PE threw on '{}' — trying next\n"), *c);
                    continue;
                }
                int32_t postNum = readNpcInfoItemsNum();
                VLOG(STR("[MoriaCppMod] [NameWrite] SUCCESS via '{}' (parm type={}); NpcInfo.Num: {} → {} (delta={})\n"),
                     *c, ptype.c_str(), preNum, postNum, postNum - preNum);
                return true;
            }

            VLOG(STR("[MoriaCppMod] [NameWrite] No candidate UFunction worked — name marker NOT set\n"));
            return false;
        }

        // [rc.51 DIRECT NAME WRITE 2026-06-14] Probe T (rc.50) confirmed no
        // name-write UFunction exists on BP_NpcGoat_C or MorNPCComponent.
        // Pivoting to direct memory write of FText into the NpcInfo entry's
        // Name field (abs offset +0x030 within FMorNPCInfo, FText is 24 bytes
        // per Probe T parm size).
        //
        // Approach:
        //   1. Find the NpcInfo entry matching the goat's NpcGuid
        //   2. Construct FText('Rûdh') via Kismet's Conv_StringToText (proven
        //      via writeGoatNameToEntry FText-path code)
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
                    if (cn.size() >= 9 && cn.substr(0,9) == STR("Default__")) continue;
                    ws = o; break;
                }
            }
            if (!ws)
            {
                VLOG(STR("[MoriaCppMod] [WorldStore] WorldState singleton not findable\n"));
                return false;
            }
            UFunction* storeFn = nullptr;
            try { storeFn = ws->GetFunctionByNameInChain(STR("StoreRuntimeActor")); } catch (...) {}
            if (!storeFn)
            {
                VLOG(STR("[MoriaCppMod] [WorldStore] StoreRuntimeActor UFunction not found\n"));
                return false;
            }
            int parmsSize = 0;
            try { parmsSize = storeFn->GetParmsSize(); } catch (...) {}
            if (parmsSize != 42)
            {
                VLOG(STR("[MoriaCppMod] [WorldStore] WARN: ParmsSize={} (expected 42 per Q.2 enumeration)\n"),
                     parmsSize);
            }
            std::vector<uint8_t> parms(parmsSize > 0 ? parmsSize : 42, 0);

            // Resolve parm offsets by name to avoid hard-coded layout assumptions
            auto* pActor   = findParam(storeFn, STR("Actor"));
            auto* pHandle  = findParam(storeFn, STR("InOutRuntimeActorHandle"));
            auto* pStab    = findParam(storeFn, STR("bStoreStability"));
            auto* pRet     = findParam(storeFn, STR("ReturnValue"));
            if (!pActor || !pHandle || !pStab || !pRet)
            {
                VLOG(STR("[MoriaCppMod] [WorldStore] Missing parm reflection: Actor={} Handle={} Stab={} Ret={}\n"),
                     (void*)pActor, (void*)pHandle, (void*)pStab, (void*)pRet);
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
                 (void*)ws, (void*)goat, parmsSize);
            try { safeProcessEvent(ws, storeFn, parms.data()); }
            catch (...)
            {
                VLOG(STR("[MoriaCppMod] [WorldStore] PE threw — failed\n"));
                return false;
            }

            bool success = *reinterpret_cast<bool*>(parms.data() + pRet->GetOffset_Internal());

            // Dump the 32-byte handle for struct-layout decode
            uint8_t* h = parms.data() + pHandle->GetOffset_Internal();
            VLOG(STR("[MoriaCppMod] [WorldStore] StoreRuntimeActor returned {} — handle bytes:\n"),
                 success ? STR("SUCCESS") : STR("FAILURE"));
            VLOG(STR("[MoriaCppMod] [WorldStore]   +0x00: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}\n"),
                 h[0],  h[1],  h[2],  h[3],  h[4],  h[5],  h[6],  h[7],
                 h[8],  h[9],  h[10], h[11], h[12], h[13], h[14], h[15]);
            VLOG(STR("[MoriaCppMod] [WorldStore]   +0x10: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}  {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}\n"),
                 h[16], h[17], h[18], h[19], h[20], h[21], h[22], h[23],
                 h[24], h[25], h[26], h[27], h[28], h[29], h[30], h[31]);
            // Try interpreting first 16 bytes as FGuid
            uint32_t* g32 = reinterpret_cast<uint32_t*>(h);
            VLOG(STR("[MoriaCppMod] [WorldStore]   (first 16B as FGuid: {:08X}-{:08X}-{:08X}-{:08X})\n"),
                 g32[0], g32[1], g32[2], g32[3]);

            return success;
        }

        // [rc.48 SET ROLE FUZZY STANDALONE 2026-06-14] Per DC's Q2 answer:
        // SetRoleFuzzy("Porter") is safe to call in isolation (single
        // property mutation, no internal Register fire). We call this on
        // every bell-spawn (fresh + adopted) so the NpcInfo entry's
        // CurrentRole.RowName becomes "Porter", which is our unique
        // goat-marker for the next session's adoption filter.
        bool setGoatRoleFuzzy_Porter_Standalone(UObject* goat)
        {
            if (!goat || !isObjectAlive(goat)) return false;
            UClass* npcCompCls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/Moria.MorNPCComponent"));
            if (!npcCompCls) return false;
            auto* getCompFn = goat->GetFunctionByNameInChain(STR("GetComponentByClass"));
            if (!getCompFn) return false;
            int sz = getCompFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            writeGoatParm<UClass*>(getCompFn, buf.data(), STR("ComponentClass"), npcCompCls);
            if (!safeProcessEvent(goat, getCompFn, buf.data())) return false;
            UObject* npcComp = readGoatParm<UObject*>(getCompFn, buf.data(), STR("ReturnValue"), nullptr);
            if (!npcComp || !isObjectAlive(npcComp)) return false;

            auto* setRoleFn = npcComp->GetFunctionByNameInChain(STR("SetRoleFuzzy"));
            if (!setRoleFn) return false;
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

            // Log pre/post NpcInfo.Num to detect any side-effect entries
            int32_t preNum = readNpcInfoItemsNum();
            try { safeProcessEvent(npcComp, setRoleFn, buf2.data()); }
            catch (...) { return false; }
            int32_t postNum = readNpcInfoItemsNum();
            VLOG(STR("[MoriaCppMod] [RoleMark] SetRoleFuzzy('Porter') on npcComp={:p}; NpcInfo.Num: {} → {} (delta={})\n"),
                 (void*)npcComp, preNum, postNum, postNum - preNum);
            return true;
        }

        // [rc.48 TICK PENDING STORES 2026-06-14] Process queued
        // StoreRuntimeActor calls. Called from tickFollowGoats. Each
        // entry has a readyMs deadline (set to GetTickCount64()+100 at
        // queue time, giving ~6 frames at 60fps for ConstructionScript
        // and BeginPlay to fully complete). Once deadline elapsed, fire
        // storeGoatInWorldState and remove from queue.
        void tickPendingStores()
        {
            if (m_pendingStores.empty()) return;
            ULONGLONG now = GetTickCount64();
            for (auto it = m_pendingStores.begin(); it != m_pendingStores.end(); )
            {
                if (now < it->readyMs) { ++it; continue; }
                UObject* g = it->goat.Get();
                if (g && isObjectAlive(g))
                {
                    VLOG(STR("[MoriaCppMod] [WorldStore] pending store fired for goat={:p}\n"), (void*)g);
                    storeGoatInWorldState(g);
                }
                else
                {
                    VLOG(STR("[MoriaCppMod] [WorldStore] pending store skipped — goat died before deadline\n"));
                }
                it = m_pendingStores.erase(it);
            }
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
                // Stay is retired (native escort catch-up makes it
                // unenforceable; bell = dismiss/recall). Tobi's v1.12 row
                // ignores bTalkInteractionEnabled, so neutralize here:
                // force Follow and collapse the row widget.
                onGoatFollow();
                if (auto* visFn = widgetCtx->GetFunctionByNameInChain(STR("SetVisibility")))
                {
                    std::vector<uint8_t> vb(visFn->GetParmsSize(), 0);
                    vb[0] = 1;  // Collapsed
                    try { safeProcessEvent(widgetCtx, visFn, vb.data()); } catch (...) {}
                }
                VLOG(STR("[MoriaCppMod] [GoatMenu] Follow/Stay row clicked — forced FOLLOW + row collapsed\n"));
                showOnScreen(L"Rûdh always follows — ring the bell to dismiss", 2.5f, 0.7f, 0.9f, 0.7f);
            }
            else if (rowLabel.find(STR("Saddlebag")) != std::wstring::npos ||
                     rowLabel.find(STR("saddlebag")) != std::wstring::npos ||
                     rowLabel.find(STR("Equip"))     != std::wstring::npos ||
                     rowLabel.find(STR("equip"))     != std::wstring::npos)
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
            // [rc.129] a restored goat may come back HIDDEN (dismissed state can
            // ride the save) while the fresh track record says "following" —
            // an invisible companion. Always unhide on adoption.
            if (auto* hideFn = goat->GetFunctionByNameInChain(STR("SetActorHiddenInGame")))
            { struct { bool b{false}; } p{}; try { safeProcessEvent(goat, hideFn, &p); } catch (...) {} }
            if (auto* collFn = goat->GetFunctionByNameInChain(STR("SetActorEnableCollision")))
            { struct { bool b{true}; } p{}; try { safeProcessEvent(goat, collFn, &p); } catch (...) {} }
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
            // [rc.48] Drain deferred StoreRuntimeActor calls. Must run
            // before the empty-herd early-return so we fire even if
            // herd somehow empties between bell-ring and store deadline.
            tickPendingStores();

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
                        // Self-heal: a NATIVELY-RESTORED goat can come back
                        // UNPOSSESSED (no AIController) — without one the
                        // follow drive never runs and the goat can't walk at
                        // all. Our own spawn path calls SpawnDefaultController;
                        // do the same here once resolution has clearly failed.
                        if (g.ctrlAttempts >= 3)
                        {
                            if (auto* sdc = goat->GetFunctionByNameInChain(STR("SpawnDefaultController")))
                            {
                                try { safeProcessEvent(goat, sdc, nullptr); } catch (...) {}
                                VLOG(STR("[MoriaCppMod] [Goat] SpawnDefaultController fired on unpossessed goat {:p}\n"),
                                     (void*)goat);
                            }
                        }
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
                        UClass* npcCls2 = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                            STR("/Script/Moria.MorNPCComponent"));
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
                                            try { safeProcessEvent(nc, tf, tb2.data()); } catch (...) {}
                                            VLOG(STR("[MoriaCppMod] [GoatBrain] MorNPCComponent tick disabled (NpcId-Invalid spam mitigation)\n"));
                                        }
                                }
                            }
                    }
                    // Movement mode can be stuck on MOVE_None from a prior
                    // bell dismiss (DisableMovement) — force Walking.
                    UClass* mvCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                        STR("/Script/Engine.CharacterMovementComponent"));
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
                                        mb[0] = 1;  // MOVE_Walking
                                        try { safeProcessEvent(mv, sm, mb.data()); } catch (...) {}
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
                    if (tnow - s_lastTickLeashDiag > 5000) {
                        s_lastTickLeashDiag = tnow;
                        VLOG(STR("[MoriaCppMod] [Leash] tick reached assert: goat={:p} ctrl={:p} stayMode={} bellSpawned={}\n"),
                             (void*)goat, (void*)ctrl, g.stayMode, g.bellSpawned);
                    }
                }
                if (!g.stayMode)
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
                    float gLoc[3] = {0,0,0}, pLoc[3] = {0,0,0};
                    bool haveG = false, haveP = false;
                    if (auto* getLoc = goat->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
                    {
                        std::vector<uint8_t> gbuf(getLoc->GetParmsSize(), 0);
                        if (safeProcessEvent(goat, getLoc, gbuf.data()))
                            if (auto* pRet = findParam(getLoc, STR("ReturnValue")))
                            {
                                float* fv = reinterpret_cast<float*>(gbuf.data() + pRet->GetOffset_Internal());
                                gLoc[0]=fv[0]; gLoc[1]=fv[1]; gLoc[2]=fv[2]; haveG = true;
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
                                    pLoc[0]=fv[0]; pLoc[1]=fv[1]; pLoc[2]=fv[2]; haveP = true;
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
                                float dx = gLoc[0]-g.lastDiagPos[0], dy = gLoc[1]-g.lastDiagPos[1], dz = gLoc[2]-g.lastDiagPos[2];
                                float moved = std::sqrt(dx*dx + dy*dy + dz*dz);
                                float speed = moved / dt;
                                float dist = -1.0f;
                                if (haveP)
                                {
                                    float ex = gLoc[0]-pLoc[0], ey = gLoc[1]-pLoc[1], ez = gLoc[2]-pLoc[2];
                                    dist = std::sqrt(ex*ex + ey*ey + ez*ez);
                                }
                                static ULONGLONG s_lastMotionLog = 0;
                                if (speed > 1200.0f || (dnow - s_lastMotionLog) > 3000)
                                {
                                    s_lastMotionLog = dnow;
                                    VLOG(STR("[MoriaCppMod] [GoatDiag] distToPlayer={:.0f} speed={:.0f}/s (moved {:.0f} in {:.2f}s) stay={}\n"),
                                         dist, speed, moved, dt, g.stayMode);
                                }
                            }
                        }
                        g.lastDiagPos[0]=gLoc[0]; g.lastDiagPos[1]=gLoc[1]; g.lastDiagPos[2]=gLoc[2];
                        g.lastDiagMs = dnow;
                    }
                    if (!g.maxSpeedLogged)
                    {
                        g.maxSpeedLogged = true;
                        UClass* mvCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                            STR("/Script/Engine.CharacterMovementComponent"));
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
                        if (auto* pGoal = findParam(moveFn, STR("Goal")))
                            *reinterpret_cast<UObject**>(mbuf.data() + pGoal->GetOffset_Internal()) = pawn;
                        if (auto* pRad = findParam(moveFn, STR("AcceptanceRadius")))
                            *reinterpret_cast<float*>(mbuf.data() + pRad->GetOffset_Internal()) = 250.0f;
                        if (auto* pPath = findParam(moveFn, STR("bUsePathfinding")))
                            *reinterpret_cast<bool*>(mbuf.data() + pPath->GetOffset_Internal()) = true;
                        try { safeProcessEvent(ctrl, moveFn, mbuf.data()); } catch (...) {}
                        if (g.moveToActorLogsRemaining > 0)
                        {
                            g.moveToActorLogsRemaining--;
                            VLOG(STR("[MoriaCppMod] [GoatDrive] MoveToActor(player,r=250) issued on ctrl={:p} goat={:p}\n"),
                                 (void*)ctrl, (void*)goat);
                        }
                    }
                }
                else
                {
                    // Stay: actively halt each second so the goat's own idle/
                    // wander AI can't drift it away from where the player left it.
                    if (auto* stopFn = ctrl->GetFunctionByNameInChain(STR("StopMovement")))
                    {
                        try { safeProcessEvent(ctrl, stopFn, nullptr); } catch (...) {}
                    }
                }
            }
        }
