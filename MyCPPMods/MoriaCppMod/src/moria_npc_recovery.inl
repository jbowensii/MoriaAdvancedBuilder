// moria_npc_recovery.inl — NPC stuck-pathing recovery (teleport-as-fallback).
//
// PHASE 1 (v7.1.0-rc.1): READ-ONLY DIAGNOSTIC PROBE.
// No state mutation, no teleport, no Settings toggle yet. The probe walks
// the first AMorAIController-subclass NPC it sees, dumps its property
// surface to UE4SS.log, then sets m_npcProbeDone=true so it never fires
// again. Confirms three open implementation TODOs from
// plans/deep-percolating-parnas.md before Phase 2 commits to a design:
//
//   #1 — exact UPROPERTY name for the FSM's "current state" pointer
//        on UFGKActorFSMComponent (likely CurrentState or ActiveState)
//   #2 — whether AAIController.Pawn is reflectively accessible, or we
//        need to dispatch K2_GetPawn() via PE
//   #3 — whether FGK exposes a built-in bIsStuck / LastValidPathTime on
//        UMorPathFollowingComponent we should prefer over a location-
//        delta polling heuristic
//
// Probe gating: behind `s_verbose` AND a one-shot `m_npcProbeDone` flag.
// Production builds with verbose off see no overhead.

        bool m_npcProbeDone{false};

        // Walk a UStruct's full property chain and log each property's
        // name + type + offset. Used at probe time to discover the
        // unknown UPROPERTY names on UFGKActorFSMComponent.
        void npcProbeLogPropertyChain(UObject* obj, const wchar_t* label)
        {
            if (!obj || !isObjectAlive(obj)) return;
            UClass* cls = nullptr;
            try { cls = obj->GetClassPrivate(); } catch (...) {}
            if (!cls) return;
            std::wstring clsName;
            try { clsName = cls->GetName(); } catch (...) { clsName = STR("?"); }
            VLOG(STR("[NpcProbe] {} obj={:p} class={}\n"), label, (void*)obj, clsName.c_str());

            int propCount = 0;
            try {
                for (auto* prop : cls->ForEachPropertyInChain())
                {
                    if (!prop) continue;
                    std::wstring propName;
                    try { propName = std::wstring(prop->GetName()); } catch (...) {}
                    int32_t off = 0;
                    try { off = prop->GetOffset_Internal(); } catch (...) {}
                    VLOG(STR("[NpcProbe]   prop[{}] off={:#06x} name={}\n"),
                         propCount, (unsigned)off, propName.c_str());
                    if (++propCount >= 80) {
                        VLOG(STR("[NpcProbe]   ... (truncated at 80 properties)\n"));
                        break;
                    }
                }
            } catch (...) {}
            VLOG(STR("[NpcProbe] {} total properties logged: {}\n"), label, propCount);
        }

        // PHASE 1 — read-only one-shot probe. Walks the first NPC dwarf
        // controller it can find, dumps everything we need to confirm
        // open implementation TODOs #1 / #2 / #3.
        //
        // Throttled: only fires when s_verbose is true AND the probe
        // hasn't completed yet. Authority-only — runs on host or dedi,
        // skipped on remote clients (controllers there are proxies,
        // their FSM state is not authoritative).
        void tickNpcRecoveryProbe()
        {
            if (m_npcProbeDone) return;
            if (!s_verbose) return;
            if (!m_characterLoaded && !m_isDedicatedServer) return;

            // Throttle: only attempt once every 5 seconds while waiting
            // for an NPC to surface. Bubble streaming may not have
            // populated the controller list at character-load time.
            static ULONGLONG s_lastProbeAttemptMs = 0;
            ULONGLONG now = GetTickCount64();
            if (now - s_lastProbeAttemptMs < 5000) return;
            s_lastProbeAttemptMs = now;

            std::vector<UObject*> controllers;
            findAllOfSafe(STR("MorAIController"), controllers);

            // Filter to friendly NPC dwarves only. Cheap class-name
            // prefix match: dwarf controllers start with
            // "BP_AiController_Npc" (lowercase 'i'); orcs / goblins /
            // grendel / watcher / goat use uppercase 'I' or different
            // prefixes entirely.
            UObject* probeCtrl = nullptr;
            std::wstring probeCls;
            for (UObject* ctrl : controllers)
            {
                if (!ctrl || !isObjectAlive(ctrl)) continue;
                std::wstring cls = safeClassName(ctrl);
                if (cls.size() < 19) continue;
                if (cls.substr(0, 19) != STR("BP_AiController_Npc")) continue;
                probeCtrl = ctrl;
                probeCls = cls;
                break;
            }

            if (!probeCtrl)
            {
                VLOG(STR("[NpcProbe] no friendly NPC dwarf controller found yet "
                         "(scanned {} MorAIController subclasses)\n"),
                     (int)controllers.size());
                return;
            }

            VLOG(STR("[NpcProbe] ====== NPC RECOVERY DIAGNOSTIC PROBE ======\n"));
            VLOG(STR("[NpcProbe] target controller: {} at {:p}\n"),
                 probeCls.c_str(), (void*)probeCtrl);

            // ---- TODO #2: is AAIController.Pawn reflectively accessible? ----
            UObject* pawn = nullptr;
            {
                auto* pawnPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
                if (pawnPtr && *pawnPtr) pawn = *pawnPtr;
                VLOG(STR("[NpcProbe] [#2] AAIController.Pawn reflective read: ptr={:p} pawn={:p}\n"),
                     (void*)pawnPtr, (void*)pawn);
            }
            if (!pawn)
            {
                VLOG(STR("[NpcProbe] [#2] FALLBACK: trying K2_GetPawn() UFunction dispatch\n"));
                if (auto* fn = probeCtrl->GetFunctionByNameInChain(STR("K2_GetPawn")))
                {
                    auto* pRet = findParam(fn, STR("ReturnValue"));
                    if (pRet)
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                        try { safeProcessEvent(probeCtrl, fn, buf.data()); } catch (...) {}
                        pawn = *reinterpret_cast<UObject**>(buf.data() + pRet->GetOffset_Internal());
                        VLOG(STR("[NpcProbe] [#2] K2_GetPawn returned pawn={:p}\n"),
                             (void*)pawn);
                    }
                }
            }

            if (pawn && isObjectAlive(pawn))
            {
                std::wstring pawnCls = safeClassName(pawn);
                VLOG(STR("[NpcProbe] pawn class: {}\n"), pawnCls.c_str());

                // ---- pawn->RootComponent->RelativeLocation direct read ----
                auto* rcPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
                UObject* rootComp = (rcPtr && *rcPtr) ? *rcPtr : nullptr;
                VLOG(STR("[NpcProbe] pawn->RootComponent: ptr={:p} comp={:p}\n"),
                     (void*)rcPtr, (void*)rootComp);
                if (rootComp && isObjectAlive(rootComp))
                {
                    auto* relLocPtr = rootComp->GetValuePtrByPropertyNameInChain<float>(STR("RelativeLocation"));
                    if (relLocPtr)
                    {
                        VLOG(STR("[NpcProbe] pawn loc (direct mem): ({:.1f}, {:.1f}, {:.1f})\n"),
                             relLocPtr[0], relLocPtr[1], relLocPtr[2]);
                    }
                }

                // PE-fallback location read for cross-check.
                if (auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")))
                {
                    auto* pRet = findParam(fn, STR("ReturnValue"));
                    if (pRet)
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize(), 0);
                        try { safeProcessEvent(pawn, fn, buf.data()); } catch (...) {}
                        auto* peLoc = reinterpret_cast<float*>(buf.data() + pRet->GetOffset_Internal());
                        VLOG(STR("[NpcProbe] pawn loc (PE K2_GetActorLocation): ({:.1f}, {:.1f}, {:.1f})\n"),
                             peLoc[0], peLoc[1], peLoc[2]);
                    }
                }
            }

            // ---- BehaviorFSMComp ----
            auto* fsmPtr = probeCtrl->GetValuePtrByPropertyNameInChain<UObject*>(STR("BehaviorFSMComp"));
            UObject* fsmComp = (fsmPtr && *fsmPtr) ? *fsmPtr : nullptr;
            VLOG(STR("[NpcProbe] controller->BehaviorFSMComp: ptr={:p} comp={:p}\n"),
                 (void*)fsmPtr, (void*)fsmComp);

            if (fsmComp && isObjectAlive(fsmComp))
            {
                // ---- TODO #1: full property dump of UFGKActorFSMComponent ----
                npcProbeLogPropertyChain(fsmComp, STR("[#1] BehaviorFSMComp"));

                // Probe candidate field names for "current state" pointer.
                const wchar_t* candidates[] = {
                    STR("CurrentState"),
                    STR("ActiveState"),
                    STR("State"),
                    STR("CurrentChildState"),
                    STR("ActiveChildState"),
                };
                for (const wchar_t* name : candidates)
                {
                    auto* p = fsmComp->GetValuePtrByPropertyNameInChain<UObject*>(name);
                    if (p)
                    {
                        UObject* state = *p;
                        std::wstring stateCls = state ? safeClassName(state) : STR("(null)");
                        VLOG(STR("[NpcProbe] [#1] field '{}' EXISTS, value={:p} class={}\n"),
                             name, (void*)state, stateCls.c_str());
                        if (state && isObjectAlive(state))
                        {
                            // If this is a MoveTo state, dump its destination.
                            if (stateCls.find(STR("MoveTo")) != std::wstring::npos)
                            {
                                auto* destPtr = state->GetValuePtrByPropertyNameInChain<float>(STR("Destination"));
                                if (destPtr)
                                {
                                    VLOG(STR("[NpcProbe] [#1+] MoveTo.Destination: ({:.1f}, {:.1f}, {:.1f})\n"),
                                         destPtr[0], destPtr[1], destPtr[2]);
                                }
                                npcProbeLogPropertyChain(state, STR("[#1+] active MoveTo state"));
                            }
                        }
                    }
                }
            }

            // ---- TODO #3: check UMorPathFollowingComponent for built-in stuck flags ----
            if (pawn && isObjectAlive(pawn))
            {
                auto* pfcPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("PathFollowingComponent"));
                UObject* pfc = (pfcPtr && *pfcPtr) ? *pfcPtr : nullptr;
                VLOG(STR("[NpcProbe] [#3] pawn->PathFollowingComponent: comp={:p}\n"), (void*)pfc);
                if (pfc && isObjectAlive(pfc))
                {
                    npcProbeLogPropertyChain(pfc, STR("[#3] PathFollowingComponent"));
                }

                // Also check if MorPathFollowingComponent lives on a different access path.
                auto* mpfPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("MorPathFollowingComponent"));
                if (mpfPtr && *mpfPtr)
                {
                    npcProbeLogPropertyChain(*mpfPtr, STR("[#3 alt] MorPathFollowingComponent"));
                }
            }

            VLOG(STR("[NpcProbe] ====== probe complete; m_npcProbeDone=true ======\n"));
            m_npcProbeDone = true;
        }
