


        bool getCameraRay(FVec3f& outStart, FVec3f& outEnd)
        {
            auto* pc = findPlayerController();
            if (!pc) return false;

            m_screen.refresh(pc);
            float centerX = m_screen.fracToPixelX(0.5f);
            float centerY = m_screen.fracToPixelY(0.5f);

            auto* deprojFunc = pc->GetFunctionByNameInChain(STR("DeprojectScreenPositionToWorld"));
            if (!deprojFunc) return false;
            resolveDSPOffsets(deprojFunc);
            if (s_dsp.ScreenX < 0 || s_dsp.ScreenY < 0 || s_dsp.WorldLocation < 0 || s_dsp.WorldDirection < 0) return false;
            std::vector<uint8_t> buf(s_dsp.parmsSize, 0);
            std::memcpy(buf.data() + s_dsp.ScreenX, &centerX, 4);
            std::memcpy(buf.data() + s_dsp.ScreenY, &centerY, 4);
            pc->ProcessEvent(deprojFunc, buf.data());

            FVec3f cameraLoc{}, worldDir{};
            std::memcpy(&cameraLoc, buf.data() + s_dsp.WorldLocation, 12);
            std::memcpy(&worldDir, buf.data() + s_dsp.WorldDirection, 12);


            FVec3f pawnLoc = getPawnLocation();
            float dx = pawnLoc.X - cameraLoc.X;
            float dy = pawnLoc.Y - cameraLoc.Y;
            float dz = pawnLoc.Z - cameraLoc.Z;
            float camToChar = std::sqrt(dx * dx + dy * dy + dz * dz);
            float startOffset = camToChar + 50.0f;

            outStart = {cameraLoc.X + worldDir.X * startOffset, cameraLoc.Y + worldDir.Y * startOffset, cameraLoc.Z + worldDir.Z * startOffset};
            outEnd = {cameraLoc.X + worldDir.X * TRACE_DIST, cameraLoc.Y + worldDir.Y * TRACE_DIST, cameraLoc.Z + worldDir.Z * TRACE_DIST};
            return true;
        }


        UObject* resolveHitComponent(const uint8_t* hitBuf)
        {
            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            return hit->Component.Get();
        }

        bool isHISMComponent(UObject* comp)
        {
            if (!comp) return false;
            auto* cls = comp->GetClassPrivate();
            if (!cls) return false;
            std::wstring clsName(cls->GetName());
            return clsName.find(STR("InstancedStaticMeshComponent")) != std::wstring::npos;
        }


        bool hideInstance(UObject* comp, int32_t instanceIndex)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            resolveUITOffsets(updateFunc);
            if (!uitOffsetsValid()) return false;

            auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!transFunc) return false;


            static bool s_gtpValidated = false;
            if (!s_gtpValidated)
            {
                s_gtpValidated = true;
                for (auto* prop : transFunc->ForEachProperty())
                {
                    std::wstring n(prop->GetName());
                    int off = prop->GetOffset_Internal();
                    if (n == L"InstanceIndex" && off != 0)
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform InstanceIndex expected @0, got @{}\n"), off);
                    else if (n == L"bWorldSpace" && off != offsetof(GetInstanceTransform_Params, bWorldSpace))
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform bWorldSpace expected @{}, got @{}\n"),
                             (int)offsetof(GetInstanceTransform_Params, bWorldSpace), off);
                    else if (n == L"ReturnValue" && off != offsetof(GetInstanceTransform_Params, ReturnValue))
                        VLOG(STR("[MoriaCppMod] WARN: GetInstanceTransform ReturnValue expected @{}, got @{}\n"),
                             (int)offsetof(GetInstanceTransform_Params, ReturnValue), off);
                }
                VLOG(STR("[MoriaCppMod] Validated GetInstanceTransform_Params ({}B struct vs {}B UFunction)\n"),
                     (int)sizeof(GetInstanceTransform_Params), transFunc->GetParmsSize());
            }

            GetInstanceTransform_Params gtp{};
            gtp.InstanceIndex = instanceIndex;
            gtp.bWorldSpace = 1;
            comp->ProcessEvent(transFunc, &gtp);
            if (!gtp.ReturnValue) return false;

            FTransformRaw hidden = gtp.OutTransform;
            hidden.Translation.Z -= 50000.0f;
            hidden.Scale3D = {0.001f, 0.001f, 0.001f};

            std::vector<uint8_t> params(s_uit.parmsSize, 0);
            std::memcpy(params.data() + s_uit.InstanceIndex, &instanceIndex, 4);
            std::memcpy(params.data() + s_uit.NewInstanceTransform, &hidden, 48);
            params[s_uit.bWorldSpace] = 1;
            params[s_uit.bMarkRenderStateDirty] = 1;
            params[s_uit.bTeleport] = 1;
            comp->ProcessEvent(updateFunc, params.data());
            return params[s_uit.ReturnValue] != 0;
        }


        bool restoreInstance(UObject* comp, int32_t instanceIndex, const FTransformRaw& original)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            resolveUITOffsets(updateFunc);
            if (!uitOffsetsValid()) return false;

            std::vector<uint8_t> params(s_uit.parmsSize, 0);
            std::memcpy(params.data() + s_uit.InstanceIndex, &instanceIndex, 4);
            std::memcpy(params.data() + s_uit.NewInstanceTransform, &original, 48);
            params[s_uit.bWorldSpace] = 1;
            params[s_uit.bMarkRenderStateDirty] = 1;
            params[s_uit.bTeleport] = 1;
            comp->ProcessEvent(updateFunc, params.data());
            return params[s_uit.ReturnValue] != 0;
        }


        bool doLineTrace(const FVec3f& start, const FVec3f& end, uint8_t* hitBuf, bool debugDraw = false)
        {
            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return false;

            resolveLTOffsets(ltFunc);
            if (!ltOffsetsValid()) return false;

            std::vector<uint8_t> params(s_lt.parmsSize, 0);
            std::memcpy(params.data() + s_lt.WorldContextObject, &pc, 8);
            std::memcpy(params.data() + s_lt.Start, &start, 12);
            std::memcpy(params.data() + s_lt.End, &end, 12);
            params[s_lt.TraceChannel] = 0;
            params[s_lt.bTraceComplex] = 1;
            params[s_lt.bIgnoreSelf] = 1;

            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params.data() + s_lt.ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 8, &one, 4);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 12, &one, 4);
            }

            if (debugDraw)
            {
                params[s_lt.DrawDebugType] = 2;
                float greenColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                float redColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float drawTime = 5.0f;
                std::memcpy(params.data() + s_lt.TraceColor, greenColor, 16);
                std::memcpy(params.data() + s_lt.TraceHitColor, redColor, 16);
                std::memcpy(params.data() + s_lt.DrawTime, &drawTime, 4);
            }
            else
            {
                params[s_lt.DrawDebugType] = 0;
            }

            kslCDO->ProcessEvent(ltFunc, params.data());

            bool bHit = params[s_lt.ReturnValue] != 0;
            if (bHit)
            {
                std::memcpy(hitBuf, params.data() + s_lt.OutHit, 136);
            }
            return bHit;
        }


        void startReplay()
        {
            if (m_replay.active) return;
            m_replay = {};
            if (m_savedRemovals.empty() && m_typeRemovals.empty()) return;

            std::vector<UObject*> rawComps;
            UObjectGlobals::FindAllOf(STR("GlobalHierarchicalInstancedStaticMeshComponent"), rawComps);
            if (rawComps.empty()) UObjectGlobals::FindAllOf(STR("HierarchicalInstancedStaticMeshComponent"), rawComps);


            m_replay.compQueue.reserve(rawComps.size());
            for (auto* c : rawComps) m_replay.compQueue.emplace_back(c);

            m_replay.active = !m_replay.compQueue.empty();
            if (m_replay.active)
            {
                VLOG(STR("[MoriaCppMod] Starting throttled replay ({} comps, max {} hides/frame)\n"),
                                                m_replay.compQueue.size(),
                                                MAX_HIDES_PER_FRAME);
            }
        }


        bool processReplayBatch()
        {
            if (!m_replay.active) return false;

            int hidesThisBatch = 0;

            while (m_replay.compIdx < m_replay.compQueue.size())
            {
                UObject* comp = m_replay.compQueue[m_replay.compIdx].Get();
                if (!comp)
                {
                    m_replay.compIdx++;
                    m_replay.instanceIdx = 0;
                    continue;
                }

                auto* countFunc = comp->GetFunctionByNameInChain(STR("GetInstanceCount"));
                if (!countFunc)
                {
                    m_replay.compIdx++;
                    m_replay.instanceIdx = 0;
                    continue;
                }

                std::string meshId = componentNameToMeshId(std::wstring(comp->GetName()));
                bool isTypeRule = m_typeRemovals.count(meshId) > 0;

                if (!isTypeRule)
                {
                    bool hasPending = false;
                    for (size_t si = 0; si < m_savedRemovals.size(); si++)
                    {
                        if (!m_appliedRemovals[si] && m_savedRemovals[si].meshName == meshId)
                        {
                            hasPending = true;
                            break;
                        }
                    }
                    if (!hasPending)
                    {
                        m_processedComps.insert(comp);
                        m_replay.compIdx++;
                        m_replay.instanceIdx = 0;
                        continue;
                    }
                }

                auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));

                GetInstanceCount_Params cp{};
                comp->ProcessEvent(countFunc, &cp);
                int count = cp.ReturnValue;

                if (count == 0 || m_replay.instanceIdx >= count)
                {
                    m_processedComps.insert(comp);
                    m_replay.compIdx++;
                    m_replay.instanceIdx = 0;
                    continue;
                }

                while (m_replay.instanceIdx < count)
                {
                    if (hidesThisBatch >= MAX_HIDES_PER_FRAME)
                    {
                        return true;
                    }

                    int i = m_replay.instanceIdx++;

                    if (isTypeRule)
                    {

                        if (transFunc)
                        {
                            GetInstanceTransform_Params tp{};
                            tp.InstanceIndex = i;
                            tp.bWorldSpace = 1;
                            comp->ProcessEvent(transFunc, &tp);
                            if (tp.ReturnValue && tp.OutTransform.Translation.Z < -40000.0f) continue;
                        }
                        if (hideInstance(comp, i))
                        {
                            hidesThisBatch++;
                            m_replay.totalHidden++;
                        }
                    }
                    else if (transFunc)
                    {
                        GetInstanceTransform_Params tp{};
                        tp.InstanceIndex = i;
                        tp.bWorldSpace = 1;
                        comp->ProcessEvent(transFunc, &tp);
                        if (!tp.ReturnValue) continue;

                        float px = tp.OutTransform.Translation.X;
                        float py = tp.OutTransform.Translation.Y;
                        float pz = tp.OutTransform.Translation.Z;
                        if (pz < -40000.0f) continue;

                        for (size_t si = 0; si < m_savedRemovals.size(); si++)
                        {
                            if (m_appliedRemovals[si]) continue;
                            if (m_savedRemovals[si].meshName != meshId) continue;
                            float ddx = px - m_savedRemovals[si].posX;
                            float ddy = py - m_savedRemovals[si].posY;
                            float ddz = pz - m_savedRemovals[si].posZ;
                            if (ddx * ddx + ddy * ddy + ddz * ddz < POS_TOLERANCE * POS_TOLERANCE)
                            {
                                hideInstance(comp, i);
                                m_appliedRemovals[si] = true;
                                hidesThisBatch++;
                                m_replay.totalHidden++;
                                break;
                            }
                        }
                    }
                }

                m_processedComps.insert(comp);
                m_replay.compIdx++;
                m_replay.instanceIdx = 0;
            }

            m_replay.active = false;
            int pending = pendingCount();
            VLOG(STR("[MoriaCppMod] Replay done: {} hidden, {} pending\n"), m_replay.totalHidden, pending);
            return false;
        }

        void checkForNewComponents()
        {
            if (m_savedRemovals.empty() && m_typeRemovals.empty()) return;
            if (m_replay.active) return;

            std::vector<UObject*> comps;
            UObjectGlobals::FindAllOf(STR("GlobalHierarchicalInstancedStaticMeshComponent"), comps);

            std::vector<RC::Unreal::FWeakObjectPtr> newComps;
            for (auto* comp : comps)
            {
                if (!m_processedComps.count(comp)) newComps.emplace_back(comp);
            }
            if (newComps.empty()) return;

            m_replay = {};
            m_replay.compQueue = std::move(newComps);
            m_replay.active = true;
            VLOG(STR("[MoriaCppMod] Streaming: {} new components queued for replay\n"), m_replay.compQueue.size());
        }


        void removeAimed()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                VLOG(STR("[MoriaCppMod] No hit\n"));
                return;
            }

            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            FVec3f impactPoint = hit->ImpactPoint;
            int32_t item = hit->Item;

            UObject* hitComp = resolveHitComponent(hitBuf);

            if (!hitComp || !isHISMComponent(hitComp))
            {
                std::wstring name = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
                std::wstring cls = L"";
                if (hitComp)
                {
                    auto* c = hitComp->GetClassPrivate();
                    if (c) cls = std::wstring(c->GetName());
                }
                VLOG(STR("[MoriaCppMod] Not HISM: {} ({})\n"), name, cls);
                return;
            }

            if (item < 0)
            {
                VLOG(STR("[MoriaCppMod] No instance index (Item=-1)\n"));
                return;
            }

            auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            auto* countFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceCount"));
            if (!transFunc || !countFunc) return;
            GetInstanceTransform_Params tp{};
            tp.InstanceIndex = item;
            tp.bWorldSpace = 1;
            hitComp->ProcessEvent(transFunc, &tp);
            if (!tp.ReturnValue) return;

            float targetX = tp.OutTransform.Translation.X;
            float targetY = tp.OutTransform.Translation.Y;
            float targetZ = tp.OutTransform.Translation.Z;
            std::wstring compName(hitComp->GetName());
            std::string meshId = componentNameToMeshId(compName);


            GetInstanceCount_Params cp{};
            hitComp->ProcessEvent(countFunc, &cp);
            int count = cp.ReturnValue;

            int hiddenCount = 0;
            for (int i = 0; i < count; i++)
            {
                GetInstanceTransform_Params itp{};
                itp.InstanceIndex = i;
                itp.bWorldSpace = 1;
                hitComp->ProcessEvent(transFunc, &itp);
                if (!itp.ReturnValue) continue;

                float px = itp.OutTransform.Translation.X;
                float py = itp.OutTransform.Translation.Y;
                float pz = itp.OutTransform.Translation.Z;

                if (pz < -40000.0f) continue;

                float ddx = px - targetX;
                float ddy = py - targetY;
                float ddz = pz - targetZ;
                if (ddx * ddx + ddy * ddy + ddz * ddz < POS_TOLERANCE * POS_TOLERANCE)
                {

                    m_undoStack.push_back({RC::Unreal::FWeakObjectPtr(hitComp), i, itp.OutTransform, compName});

                    SavedRemoval sr;
                    sr.meshName = meshId;
                    sr.posX = px;
                    sr.posY = py;
                    sr.posZ = pz;
                    m_savedRemovals.push_back(sr);
                    m_appliedRemovals.push_back(true);
                    appendToSaveFile(sr);
                    buildRemovalEntries();

                    hideInstance(hitComp, i);
                    hiddenCount++;
                }
            }

            VLOG(STR("[MoriaCppMod] REMOVED {} stacked at ({:.0f},{:.0f},{:.0f}) from {} | Total: {}\n"),
                                            hiddenCount,
                                            targetX,
                                            targetY,
                                            targetZ,
                                            compName,
                                            m_savedRemovals.size());
        }

        void removeAllOfType()
        {
            FVec3f start{}, end{};
            if (!getCameraRay(start, end)) return;

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf))
            {
                VLOG(STR("[MoriaCppMod] No hit\n"));
                return;
            }

            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp || !isHISMComponent(hitComp))
            {
                std::wstring name = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
                VLOG(STR("[MoriaCppMod] Not HISM: {}\n"), name);
                return;
            }

            auto* countFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceCount"));
            auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!countFunc) return;

            std::wstring compName(hitComp->GetName());
            std::string meshId = componentNameToMeshId(compName);


            if (!m_typeRemovals.count(meshId))
            {
                m_typeRemovals.insert(meshId);
                std::ofstream file(m_saveFilePath, std::ios::app);
                if (file.is_open()) file << "@" << meshId << "\n";
                buildRemovalEntries();
            }

            GetInstanceCount_Params cp{};
            hitComp->ProcessEvent(countFunc, &cp);
            int count = cp.ReturnValue;

            int hidden = 0;
            for (int i = 0; i < count; i++)
            {
                if (transFunc)
                {
                    GetInstanceTransform_Params tp{};
                    tp.InstanceIndex = i;
                    tp.bWorldSpace = 1;
                    hitComp->ProcessEvent(transFunc, &tp);
                    if (tp.ReturnValue)
                    {

                        if (tp.OutTransform.Translation.Z < -40000.0f) continue;
                        m_undoStack.push_back({RC::Unreal::FWeakObjectPtr(hitComp), i, tp.OutTransform, compName, true, meshId});
                    }
                }
                if (hideInstance(hitComp, i)) hidden++;
            }

            std::wstring meshIdW(meshId.begin(), meshId.end());
            VLOG(STR("[MoriaCppMod] TYPE RULE: @{} Ã¢â‚¬â€ hidden {} instances (persists across all worlds)\n"), meshIdW, hidden);
        }

        void toggleHideCharacter()
        {
            std::vector<UObject*> dwarves;
            UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
            if (dwarves.empty()) return;

            m_characterHidden = !m_characterHidden;
            for (auto* dwarf : dwarves)
            {
                auto* fn = dwarf->GetFunctionByNameInChain(STR("SetActorHiddenInGame"));
                if (fn)
                {
                    std::vector<uint8_t> params(fn->GetParmsSize(), 0);
                    params[0] = m_characterHidden ? 1 : 0;
                    dwarf->ProcessEvent(fn, params.data());
                }
            }
            if (m_characterHidden)
                showOnScreen(Loc::get("msg.char_hidden"), 2.0f, 0.3f, 0.8f, 1.0f);
            else
                showOnScreen(Loc::get("msg.char_visible"), 2.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] Character hidden = {}\n"), m_characterHidden ? 1 : 0);
        }

        void toggleFlyMode()
        {
            std::vector<UObject*> dwarves;
            UObjectGlobals::FindAllOf(STR("BP_FGKDwarf_C"), dwarves);
            if (dwarves.empty()) return;

            m_flyMode = !m_flyMode;
            constexpr uint8_t MOVE_Falling = 3;
            constexpr uint8_t MOVE_Flying  = 5;


            for (auto* dwarf : dwarves)
            {
                auto** movCompPtr = dwarf->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement"));
                if (!movCompPtr) continue;
                auto* movComp = *movCompPtr;
                if (!movComp)
                {
                    VLOG(STR("[MoriaCppMod] CharacterMovement is null!\n"));
                    continue;
                }


                if (!m_flyMode)
                    setBoolProp(movComp, L"bCheatFlying", false);

                auto* fn = movComp->GetFunctionByNameInChain(STR("SetMovementMode"));
                if (fn)
                {
                    std::vector<uint8_t> params(fn->GetParmsSize(), 0);
                    params[0] = m_flyMode ? MOVE_Flying : MOVE_Falling;
                    movComp->ProcessEvent(fn, params.data());
                    VLOG(STR("[MoriaCppMod] SetMovementMode({}) called\n"),
                                                    m_flyMode ? 5 : 3);
                }

                if (m_flyMode)
                    setBoolProp(movComp, L"bCheatFlying", true);

                VLOG(STR("[MoriaCppMod] bCheatFlying = {}\n"), m_flyMode ? 1 : 0);


                bool shouldDisableCollision = m_flyMode && m_noCollisionWhileFlying;
                bool shouldRestoreCollision = !m_flyMode;
                if (shouldDisableCollision || shouldRestoreCollision)
                {
                    auto* colFn = dwarf->GetFunctionByNameInChain(STR("SetActorEnableCollision"));
                    if (colFn)
                    {
                        std::vector<uint8_t> colParams(colFn->GetParmsSize(), 0);
                        colParams[0] = shouldDisableCollision ? 0 : 1;
                        dwarf->ProcessEvent(colFn, colParams.data());
                        VLOG(STR("[MoriaCppMod] SetActorEnableCollision({}) Ã¢â‚¬â€ noclip {}\n"),
                                                        shouldDisableCollision ? 0 : 1, shouldDisableCollision ? STR("ON") : STR("OFF"));
                    }
                }
            }

            if (m_flyMode)
                showOnScreen(m_noCollisionWhileFlying ? L"Fly + Noclip ON" : L"Fly ON", 2.0f, 0.3f, 0.8f, 1.0f);
            else
                showOnScreen(Loc::get("msg.fly_off"), 2.0f, 0.3f, 1.0f, 0.3f);
            VLOG(STR("[MoriaCppMod] Fly mode = {}, noCollision = {}\n"), m_flyMode ? 1 : 0, m_noCollisionWhileFlying ? 1 : 0);
        }

        void dumpAimedActor()
        {
            VLOG(STR("[MoriaCppMod] === AIMED ACTOR DUMP ===\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                VLOG(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }


            auto* ltFunc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            auto* kslCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!ltFunc || !kslCDO || !pc) return;

            resolveLTOffsets(ltFunc);
            if (s_lt.ReturnValue < 0) return;

            std::vector<uint8_t> params(s_lt.parmsSize, 0);
            std::memcpy(params.data() + s_lt.WorldContextObject, &pc, 8);
            std::memcpy(params.data() + s_lt.Start, &start, 12);
            std::memcpy(params.data() + s_lt.End, &end, 12);
            params[s_lt.TraceChannel] = 0;
            params[s_lt.bTraceComplex] = 0;
            params[s_lt.bIgnoreSelf] = 1;
            params[s_lt.DrawDebugType] = 2;
            float greenColor[4] = {0.0f, 1.0f, 1.0f, 1.0f};
            float redColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
            float drawTime = 5.0f;
            std::memcpy(params.data() + s_lt.TraceColor, greenColor, 16);
            std::memcpy(params.data() + s_lt.TraceHitColor, redColor, 16);
            std::memcpy(params.data() + s_lt.DrawTime, &drawTime, 4);

            auto* pawn = getPawn();
            if (pawn)
            {
                uintptr_t arrPtr = reinterpret_cast<uintptr_t>(&pawn);
                int32_t one = 1;
                std::memcpy(params.data() + s_lt.ActorsToIgnore, &arrPtr, 8);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 8, &one, 4);
                std::memcpy(params.data() + s_lt.ActorsToIgnore + 12, &one, 4);
            }

            kslCDO->ProcessEvent(ltFunc, params.data());

            bool bHit = params[s_lt.ReturnValue] != 0;
            if (!bHit)
            {
                VLOG(STR("[MoriaCppMod] No hit\n"));
                showErrorBox(Loc::get("msg.actor_dump_no_hit"));
                return;
            }

            uint8_t hitBuf[136]{};
            std::memcpy(hitBuf, params.data() + s_lt.OutHit, 136);

            UObject* hitComp = resolveHitComponent(hitBuf);
            if (!hitComp)
            {
                VLOG(STR("[MoriaCppMod] Hit but null component\n"));
                return;
            }

            std::wstring compName(hitComp->GetName());
            std::wstring compClass = safeClassName(hitComp);
            VLOG(STR("[MoriaCppMod] Hit component: {} ({})\n"), compName, compClass);

            auto* ownerFunc = hitComp->GetFunctionByNameInChain(STR("GetOwner"));
            UObject* actor = nullptr;
            if (ownerFunc)
            {
                struct
                {
                    UObject* Ret{nullptr};
                } op{};
                hitComp->ProcessEvent(ownerFunc, &op);
                actor = op.Ret;
            }

            if (!actor)
            {
                VLOG(STR("[MoriaCppMod] No owning actor found\n"));
                showOnScreen(L"[ActorDump] Component: " + compName + L" (" + compClass + L")\nNo owning actor", 5.0f);
                return;
            }

            std::wstring actorName(actor->GetName());
            std::wstring actorClassName = safeClassName(actor);

            std::wstring assetPath(actor->GetPathName());


            std::wstring displayName;
            auto* getDispFn = actor->GetFunctionByNameInChain(STR("GetDisplayName"));
            if (getDispFn)
            {
                VLOG(STR("[MoriaCppMod] GetDisplayName found, parmsSize={}\n"), getDispFn->GetParmsSize());
                if (getDispFn->GetParmsSize() == sizeof(FText))
                {
                    FText txt{};
                    actor->ProcessEvent(getDispFn, &txt);
                    if (txt.Data) displayName = txt.ToString();
                }
            }
            else
            {
                VLOG(STR("[MoriaCppMod] GetDisplayName NOT found on this actor\n"));
            }
            if (displayName.empty())
            {

                std::wstring cleaned = actorClassName;
                if (cleaned.size() > 3 && cleaned.substr(0, 3) == L"BP_") cleaned.erase(0, 3);
                if (cleaned.size() > 2 && cleaned.substr(cleaned.size() - 2) == L"_C") cleaned.resize(cleaned.size() - 2);
                for (auto& c : cleaned)
                {
                    if (c == L'_') c = L' ';
                }
                displayName = cleaned;
                VLOG(STR("[MoriaCppMod] Display name fallback: '{}'\n"), displayName);
            }

            VLOG(STR("[MoriaCppMod] Actor: {} | Class: {}\n"), actorName, actorClassName);
            VLOG(STR("[MoriaCppMod] Display: {}\n"), displayName);
            VLOG(STR("[MoriaCppMod] Path: {}\n"), assetPath);


            bool isBuildable = false;
            std::wstring recipeRef;
            auto* actorCls = actor->GetClassPrivate();
            if (actorCls)
            {
                auto* actorStruct = static_cast<UStruct*>(actorCls);

                for (auto* prop : actorStruct->ForEachPropertyInChain())
                {
                    if (!prop) continue;
                    std::wstring propName(prop->GetName());
                    if (propName.find(L"MorConstructionSnap") != std::wstring::npos || propName.find(L"MorConstructionStability") != std::wstring::npos ||
                        propName.find(L"MorConstructionPermit") != std::wstring::npos)
                    {
                        isBuildable = true;
                        break;
                    }
                }

                if (!isBuildable)
                {
                    for (auto* super = actorCls->GetSuperStruct(); super; super = super->GetSuperStruct())
                    {
                        std::wstring superName(super->GetName());
                        if (superName.find(L"BaseArchitectureBreakable") != std::wstring::npos || superName.find(L"CraftingStation") != std::wstring::npos ||
                            superName.find(L"FueledCraftingStation") != std::wstring::npos)
                        {
                            isBuildable = true;
                            break;
                        }
                    }
                }

                if (isBuildable)
                {

                    recipeRef = actorClassName;
                    if (recipeRef.size() > 2 && recipeRef.substr(recipeRef.size() - 2) == L"_C") recipeRef.resize(recipeRef.size() - 2);
                }
            }

            VLOG(STR("[MoriaCppMod] Buildable: {} Recipe: {}\n"), isBuildable ? STR("Yes") : STR("No"), recipeRef);


            std::wstring dtDisplayName;
            std::wstring dtRowName;
            {
                std::wofstream dumpFile(modPath("Mods/MoriaCppMod/actor_dump.txt"), std::ios::trunc);
                if (dumpFile.is_open())
                {
                    dumpFile << L"=== DT_Constructions SCAN for: " << actorClassName << L" ===\n";
                    dumpFile << L"Actor path: " << assetPath << L"\n";

                    std::wstring classPath;
                    if (actorCls)
                    {
                        classPath = std::wstring(actorCls->GetPathName());
                        dumpFile << L"Class path: " << classPath << L"\n";
                    }
                    dumpFile << L"\n";
                    dumpFile.flush();


                    if (!m_dtConstructions.isBound()) m_dtConstructions.bind(L"DT_Constructions");

                    if (!m_dtConstructions.isBound())
                    {
                        dumpFile << L"DT_Constructions NOT FOUND\n";
                    }
                    else
                    {
                        dumpFile << L"Found DT_Constructions at " << m_dtConstructions.table << L"\n\n";

                        auto names = m_dtConstructions.getRowNames();
                        int actorOff = m_dtConstructions.resolvePropertyOffset(L"Actor");

                        dumpFile << L"DT_Constructions: " << names.size() << L" rows, Actor@0x"
                                 << std::hex << actorOff << std::dec << L"\n\n";
                        dumpFile.flush();

                        int matchCount = 0;

                        for (size_t i = 0; i < names.size(); i++)
                        {
                            const auto& rowNameStr = names[i];
                            uint8_t* rowData = m_dtConstructions.findRowData(rowNameStr.c_str());
                            if (!rowData) continue;


                            std::wstring rowAssetPath;
                            if (actorOff >= 0 && isReadableMemory(rowData + actorOff + SOFTCLASSPTR_ASSETPATH_FNAME, 8))
                            {
                                FName assetFName;
                                std::memcpy(&assetFName, rowData + actorOff + SOFTCLASSPTR_ASSETPATH_FNAME, 8);
                                try { rowAssetPath = assetFName.ToString(); } catch (...) { continue; }
                            }
                            else continue;

                            bool isMatch = false;
                            if (!rowAssetPath.empty())
                            {
                                if (!classPath.empty() && classPath.find(rowAssetPath) != std::wstring::npos)
                                    isMatch = true;
                                if (!isMatch && !recipeRef.empty() && rowAssetPath.find(recipeRef) != std::wstring::npos)
                                    isMatch = true;
                            }

                            if (i < 3 || isMatch)
                            {
                                std::wstring dispName = m_dtConstructions.readFText(rowNameStr.c_str(), L"DisplayName");

                                dumpFile << (isMatch ? L">>> MATCH" : L"   ") << L" [" << i << L"] " << rowNameStr
                                         << L"  Display=\"" << dispName << L"\"  ActorPath=\"" << rowAssetPath << L"\"\n";
                                dumpFile.flush();

                                if (isMatch && dtDisplayName.empty())
                                {
                                    dtDisplayName = dispName;
                                    dtRowName = rowNameStr;
                                }
                            }

                            if (isMatch) matchCount++;
                        }

                        dumpFile << L"\n=== RESULTS: " << names.size() << L" rows scanned, " << matchCount << L" matches ===\n";
                        if (!dtDisplayName.empty())
                            dumpFile << L"MATCHED: row='" << dtRowName << L"' display='" << dtDisplayName << L"'\n";
                        else
                            dumpFile << L"NO MATCH FOUND for classPath='" << classPath << L"'\n";
                    }
                    dumpFile.close();

                    VLOG(STR("[MoriaCppMod] DT_Constructions scan -> actor_dump.txt\n"));
                }
            }

            if (!dtDisplayName.empty())
            {
                displayName = dtDisplayName;
                VLOG(STR("[MoriaCppMod] DT display name: '{}' (row '{}')\n"), displayName, dtRowName);
            }


            m_lastTargetBuildable = isBuildable;
            m_targetBuildRecipeRef = recipeRef;
            m_targetBuildRowName = dtRowName;
            if (isBuildable && !displayName.empty())
            {
                m_targetBuildName = displayName;
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Stored target: name='{}' recipeRef='{}' row='{}'\n"),
                                                m_targetBuildName,
                                                m_targetBuildRecipeRef,
                                                m_targetBuildRowName);
            }
            else
            {
                m_targetBuildName.clear();
            }


            if (!isBuildable)
            {
                uint8_t inspectHitBuf[136]{};
                if (doLineTrace(start, end, inspectHitBuf, false))
                {
                    auto* inspHit = reinterpret_cast<const FHitResultLocal*>(inspectHitBuf);
                    FVec3f impactPt = inspHit->ImpactPoint;
                    int32_t instItem = inspHit->Item;
                    UObject* inspComp = resolveHitComponent(inspectHitBuf);
                    if (inspComp)
                    {
                        std::wstring inspCompName(inspComp->GetName());
                        std::wstring inspClassName = safeClassName(inspComp);
                        bool inspIsHISM = isHISMComponent(inspComp);
                        std::string inspMeshId = componentNameToMeshId(inspCompName);
                        std::wstring inspMeshIdW(inspMeshId.begin(), inspMeshId.end());

                        std::wstring friendlyName = extractFriendlyName(inspMeshId);

                        std::wstring posInfo;
                        if (inspIsHISM && instItem >= 0)
                        {
                            auto* transFunc = inspComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
                            if (transFunc)
                            {
                                GetInstanceTransform_Params tp{};
                                tp.InstanceIndex = instItem;
                                tp.bWorldSpace = 1;
                                inspComp->ProcessEvent(transFunc, &tp);
                                if (tp.ReturnValue)
                                {
                                    posInfo = std::format(L"Pos: ({:.0f}, {:.0f}, {:.0f}) Item #{}",
                                                          tp.OutTransform.Translation.X,
                                                          tp.OutTransform.Translation.Y,
                                                          tp.OutTransform.Translation.Z,
                                                          instItem);
                                }
                            }
                        }
                        if (posInfo.empty())
                        {
                            posInfo = std::format(L"Impact: ({:.0f}, {:.0f}, {:.0f})", impactPt.X, impactPt.Y, impactPt.Z);
                        }

                        VLOG(STR("[MoriaCppMod] [F10] Non-buildable inspect: {} | HISM={} | MeshID={}\n"),
                                                        inspCompName, inspIsHISM, inspMeshIdW);

                        showTargetInfoUMG(inspCompName, friendlyName, inspMeshIdW, inspClassName, false, posInfo, L"");
                        VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
                        return;
                    }
                }
            }

            showTargetInfoUMG(actorName, displayName, assetPath, actorClassName, isBuildable, recipeRef, dtRowName);

            VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
        }
