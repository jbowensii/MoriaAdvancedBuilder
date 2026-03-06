// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_hism.inl — HISM removal, line tracing, replay, dumpAimedActor      ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // Ã¢â€â‚¬Ã¢â€â‚¬ Camera & Trace Ã¢â€â‚¬Ã¢â€â‚¬

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6D: HISM Removal System Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // CRITICAL: Max 3 hides/frame to avoid render thread crash

        // Construct camera ray from viewport center, offset past pawn to avoid self-hits.
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

            // Start past character to avoid hitting objects between camera and pawn
            FVec3f pawnLoc = getPawnLocation();
            float dx = pawnLoc.X - cameraLoc.X;
            float dy = pawnLoc.Y - cameraLoc.Y;
            float dz = pawnLoc.Z - cameraLoc.Z;
            float camToChar = std::sqrt(dx * dx + dy * dy + dz * dz);
            float startOffset = camToChar + 50.0f; // 50 units past the character

            outStart = {cameraLoc.X + worldDir.X * startOffset, cameraLoc.Y + worldDir.Y * startOffset, cameraLoc.Z + worldDir.Z * startOffset};
            outEnd = {cameraLoc.X + worldDir.X * TRACE_DIST, cameraLoc.Y + worldDir.Y * TRACE_DIST, cameraLoc.Z + worldDir.Z * TRACE_DIST};
            return true;
        }

        // Extract hit UObject* from FHitResult Component FWeakObjectPtr.
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

        // Hide instance by moving underground + tiny scale (safe, no crash unlike RemoveInstance).
        bool hideInstance(UObject* comp, int32_t instanceIndex)
        {
            auto* updateFunc = comp->GetFunctionByNameInChain(STR("UpdateInstanceTransform"));
            if (!updateFunc) return false;

            resolveUITOffsets(updateFunc);
            if (!uitOffsetsValid()) return false;

            auto* transFunc = comp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
            if (!transFunc) return false;

            // One-time: validate GetInstanceTransform_Params layout against reflection
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

        // Restore instance to original transform (undo a hide)
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

        // LineTraceSingle via ProcessEvent. Returns true + fills hitBuf (136B) on hit.
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
            params[s_lt.TraceChannel] = 0;  // Visibility
            params[s_lt.bTraceComplex] = 1; // Per-triangle for accuracy
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
                params[s_lt.DrawDebugType] = 2; // ForDuration
                float greenColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                float redColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float drawTime = 5.0f;
                std::memcpy(params.data() + s_lt.TraceColor, greenColor, 16);
                std::memcpy(params.data() + s_lt.TraceHitColor, redColor, 16);
                std::memcpy(params.data() + s_lt.DrawTime, &drawTime, 4);
            }
            else
            {
                params[s_lt.DrawDebugType] = 0; // None
            }

            kslCDO->ProcessEvent(ltFunc, params.data());

            bool bHit = params[s_lt.ReturnValue] != 0;
            if (bHit)
            {
                std::memcpy(hitBuf, params.data() + s_lt.OutHit, 136);
            }
            return bHit;
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ Throttled Replay Ã¢â€â‚¬Ã¢â€â‚¬

        void startReplay()
        {
            if (m_replay.active) return; // don't interrupt active replay
            m_replay = {};
            if (m_savedRemovals.empty() && m_typeRemovals.empty()) return;

            std::vector<UObject*> rawComps;
            UObjectGlobals::FindAllOf(STR("GlobalHierarchicalInstancedStaticMeshComponent"), rawComps);
            if (rawComps.empty()) UObjectGlobals::FindAllOf(STR("HierarchicalInstancedStaticMeshComponent"), rawComps);

            // Store as weak pointers — GC can run between replay frames
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

        // Process up to MAX_HIDES_PER_FRAME instances per frame. Returns true if more work remains.
        bool processReplayBatch()
        {
            if (!m_replay.active) return false;

            int hidesThisBatch = 0;

            while (m_replay.compIdx < m_replay.compQueue.size())
            {
                UObject* comp = m_replay.compQueue[m_replay.compIdx].Get();

                auto* countFunc = comp ? comp->GetFunctionByNameInChain(STR("GetInstanceCount")) : nullptr;
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
                        return true; // Budget exhausted, continue next frame
                    }

                    int i = m_replay.instanceIdx++;

                    if (isTypeRule)
                    {
                        // For type rules, skip already-hidden instances
                        if (transFunc)
                        {
                            GetInstanceTransform_Params tp{};
                            tp.InstanceIndex = i;
                            tp.bWorldSpace = 1;
                            comp->ProcessEvent(transFunc, &tp);
                            if (tp.ReturnValue && tp.OutTransform.Translation.Z < -40000.0f) continue; // already hidden
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
                        if (pz < -40000.0f) continue; // already hidden

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
            if (m_replay.active) return; // don't interfere with active replay

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

        // Ã¢â€â‚¬Ã¢â€â‚¬ Actions Ã¢â€â‚¬Ã¢â€â‚¬

        void inspectAimed()
        {
            VLOG(STR("[MoriaCppMod] --- Inspect ---\n"));

            FVec3f start{}, end{};
            if (!getCameraRay(start, end))
            {
                VLOG(STR("[MoriaCppMod] getCameraRay failed\n"));
                return;
            }

            VLOG(STR("[MoriaCppMod] Ray: ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f})\n"), start.X, start.Y, start.Z, end.X, end.Y, end.Z);

            uint8_t hitBuf[136]{};
            if (!doLineTrace(start, end, hitBuf, true))
            { // debugDraw=true
                VLOG(STR("[MoriaCppMod] No hit\n"));
                showOnScreen(Loc::get("msg.no_hit"), 3.0f, 1.0f, 0.3f, 0.3f);
                return;
            }

            auto* hit = reinterpret_cast<const FHitResultLocal*>(hitBuf);
            FVec3f impactPoint = hit->ImpactPoint;
            int32_t item = hit->Item;

            UObject* hitComp = resolveHitComponent(hitBuf);

            std::wstring compName = hitComp ? std::wstring(hitComp->GetName()) : L"(null)";
            std::wstring fullName = hitComp ? std::wstring(hitComp->GetFullName()) : L"(null)";
            std::wstring className = L"(unknown)";
            if (hitComp)
            {
                auto* cls = hitComp->GetClassPrivate();
                if (cls) className = std::wstring(cls->GetName());
            }
            bool isHISM = isHISMComponent(hitComp);
            std::string meshId = hitComp ? componentNameToMeshId(compName) : "(null)";
            std::wstring meshIdW(meshId.begin(), meshId.end());

            VLOG(STR("[MoriaCppMod] Component: {} | Class: {} | Item: {} | HISM: {}\n"), compName, className, item, isHISM);
            VLOG(STR("[MoriaCppMod] FullPath: {}\n"), fullName);
            VLOG(STR("[MoriaCppMod] MeshID: {} | Impact: ({:.1f},{:.1f},{:.1f})\n"),
                                            meshIdW,
                                            impactPoint.X,
                                            impactPoint.Y,
                                            impactPoint.Z);

            if (isHISM && item >= 0 && hitComp)
            {
                auto* transFunc = hitComp->GetFunctionByNameInChain(STR("GetInstanceTransform"));
                if (transFunc)
                {
                    GetInstanceTransform_Params tp{};
                    tp.InstanceIndex = item;
                    tp.bWorldSpace = 1;
                    hitComp->ProcessEvent(transFunc, &tp);
                    if (tp.ReturnValue)
                    {
                        VLOG(STR("[MoriaCppMod] Instance #{} pos: ({:.1f},{:.1f},{:.1f})\n"),
                                                        item,
                                                        tp.OutTransform.Translation.X,
                                                        tp.OutTransform.Translation.Y,
                                                        tp.OutTransform.Translation.Z);
                    }
                }
            }

            std::wstring screenText = fullName + L"\nClass: " + className;
            if (isHISM)
            {
                screenText += L"\nItem: " + std::to_wstring(item) + L" | MeshID: " + meshIdW;
            }
            float screenR = isHISM ? 0.0f : 1.0f;
            float screenG = isHISM ? 1.0f : 0.5f;
            showOnScreen(screenText, 8.0f, screenR, screenG, 0.5f);
        }

        // LINT NOTE (#18): removeAimed is keypress-only (~20 Hz), not per-frame. Throttle would
        // risk partial stack removal and corrupt undo. Replay path has MAX_HIDES_PER_FRAME.
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

            // Find ALL instances at the same position (stacked instances)
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
                    // Save for undo (weak ptr guards against GC)
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

            // Save as type rule -- removes ALL of this mesh on every world
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

            // Ã¢â€â‚¬Ã¢â€â‚¬ Fly toggle Ã¢â€â‚¬Ã¢â€â‚¬
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

                // Order: Disable clears bCheatFlying first; Enable sets movement mode first,
                // then bCheatFlying, to satisfy engine transition requirements.
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

                // Toggle actor collision AFTER fly is set
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

            // Simple trace (bTraceComplex=0) to hit actors, not just HISM instances
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
            params[s_lt.TraceChannel] = 0;  // Visibility
            params[s_lt.bTraceComplex] = 0; // Simple trace to hit actor bounds
            params[s_lt.bIgnoreSelf] = 1;
            params[s_lt.DrawDebugType] = 2; // ForDuration
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
                showOnScreen(Loc::get("msg.actor_dump_no_hit"), 3.0f, 1.0f, 0.3f, 0.3f);
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

            // NOTE: AMorBreakable lacks GetDisplayName -- needs DataTable lookup
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
                // Fallback: strip BP_ prefix and _C suffix, replace underscores with spaces
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

            // Detect if player-buildable via construction components/base classes
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

            // Ã¢â€â‚¬Ã¢â€â‚¬ DT_Constructions lookup: find real display name for this actor Ã¢â€â‚¬Ã¢â€â‚¬
            std::wstring dtDisplayName;
            std::wstring dtRowName;
            {
                std::wofstream dumpFile("Mods/MoriaCppMod/actor_dump.txt", std::ios::trunc);
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

                    std::vector<UObject*> dataTables;
                    UObjectGlobals::FindAllOf(STR("DataTable"), dataTables);
                    UObject* dtConst = nullptr;
                    for (auto* dt : dataTables)
                    {
                        if (!dt) continue;
                        try
                        {
                            std::wstring name(dt->GetName());
                            if (name == STR("DT_Constructions"))
                            {
                                dtConst = dt;
                                break;
                            }
                        }
                        catch (...)
                        {
                        }
                    }

                    if (!dtConst)
                    {
                        dumpFile << L"DT_Constructions NOT FOUND\n";
                    }
                    else
                    {
                        dumpFile << L"Found DT_Constructions at " << dtConst << L"\n\n";

                        // Read RowMap: DT_ROWMAP_OFFSET, TSet<TPair<FName, uint8*>>
                        uint8_t* dtBase = reinterpret_cast<uint8_t*>(dtConst);
                        constexpr int SET_ELEMENT_SIZE = 24;
                        constexpr int FNAME_SIZE = 8;

                        if (s_off_dtRowActor == -2)
                        {
                            resolveOffset(dtConst, L"RowStruct", s_off_dtRowStruct);
                            if (s_off_dtRowStruct >= 0)
                            {
                                auto* rowStruct = *reinterpret_cast<UStruct**>(dtBase + s_off_dtRowStruct);
                                if (rowStruct)
                                {
                                    resolveStructFieldOffset(rowStruct, L"Actor", s_off_dtRowActor);
                                    resolveStructFieldOffset(rowStruct, L"DisplayName", s_off_dtRowDisplayName);
                                }
                            }
                        }

                        constexpr int SOFTCLASSPTR_ASSETPATH_FNAME = 0x10;
                        int actorFNameOff = (s_off_dtRowActor >= 0)
                            ? s_off_dtRowActor + SOFTCLASSPTR_ASSETPATH_FNAME
                            : DT_ROW_ACTOR_FNAME;
                        int displayNameOff = (s_off_dtRowDisplayName >= 0)
                            ? s_off_dtRowDisplayName
                            : CONSTRUCTION_DISPLAY_NAME;

                        struct
                        {
                            uint8_t* Data;
                            int32_t Num;
                            int32_t Max;
                        } elemArray{};
                        std::memcpy(&elemArray, dtBase + DT_ROWMAP_OFFSET, 16);

                        dumpFile << L"RowMap: " << elemArray.Num << L" rows\n\n";
                        dumpFile.flush();

                        // TSoftClassPtr layout: +0x00 TWeakObjectPtr(8B), +0x08 TagAtLastTest(4B)+pad,
                        //   +0x10 FName AssetPathName(8B), +0x18 FString SubPathString(16B)

                        int matchCount = 0;

                        for (int i = 0; i < elemArray.Num; i++)
                        {
                            uint8_t* elem = elemArray.Data + i * SET_ELEMENT_SIZE;
                            if (!isReadableMemory(elem, SET_ELEMENT_SIZE)) continue;

                            uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + FNAME_SIZE);
                            if (!rowData || !isReadableMemory(rowData, 0x78)) continue;

                            // Actor AssetPathName FName (dynamic resolve, fallback 0x60)
                            FName assetFName;
                            std::memcpy(&assetFName, rowData + actorFNameOff, FNAME_SIZE);
                            std::wstring rowAssetPath;
                            try
                            {
                                rowAssetPath = assetFName.ToString();
                            }
                            catch (...)
                            {
                                continue;
                            }

                            bool isMatch = false;
                            if (!rowAssetPath.empty())
                            {
                                if (!classPath.empty() && classPath.find(rowAssetPath) != std::wstring::npos)
                                {
                                    isMatch = true;
                                }
                                if (!isMatch && !recipeRef.empty() && rowAssetPath.find(recipeRef) != std::wstring::npos)
                                {
                                    isMatch = true;
                                }
                            }

                            if (i < 3 || isMatch)
                            {
                                FName rowName;
                                std::memcpy(&rowName, elem, FNAME_SIZE);
                                std::wstring rowNameStr;
                                try
                                {
                                    rowNameStr = rowName.ToString();
                                }
                                catch (...)
                                {
                                    rowNameStr = L"(err)";
                                }

                                std::wstring dispName;
                                try
                                {
                                    FText* txt = reinterpret_cast<FText*>(rowData + displayNameOff);
                                    if (txt && txt->Data && isReadableMemory(txt->Data, 8)) dispName = txt->ToString();
                                }
                                catch (...)
                                {
                                    dispName = L"(err)";
                                }

                                dumpFile << (isMatch ? L">>> MATCH" : L"   ") << L" [" << i << L"] " << rowNameStr << L"  Display=\"" << dispName
                                         << L"\"  ActorPath=\"" << rowAssetPath << L"\"\n";
                                dumpFile.flush();

                                if (isMatch && dtDisplayName.empty())
                                {
                                    dtDisplayName = dispName;
                                    dtRowName = rowNameStr;
                                }
                            }

                            if (isMatch) matchCount++;
                        }

                        dumpFile << L"\n=== RESULTS: " << elemArray.Num << L" rows scanned, " << matchCount << L" matches ===\n";
                        if (!dtDisplayName.empty())
                        {
                            dumpFile << L"MATCHED: row='" << dtRowName << L"' display='" << dtDisplayName << L"'\n";
                        }
                        else
                        {
                            dumpFile << L"NO MATCH FOUND for classPath='" << classPath << L"'\n";
                        }
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

            // Store for Shift+F10 build-from-target
            m_lastTargetBuildable = isBuildable;
            m_targetBuildRecipeRef = recipeRef;
            m_targetBuildRowName = dtRowName; // DT_Constructions row name (also key for DT_ConstructionRecipes)
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

            // Non-buildable: second HISM-aware trace for component/mesh inspect
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

                        showTargetInfo(inspCompName, friendlyName, inspMeshIdW, inspClassName, false, posInfo, L"");
                        VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
                        return;
                    }
                }
            }

            showTargetInfo(actorName, displayName, assetPath, actorClassName, isBuildable, recipeRef, dtRowName);

            VLOG(STR("[MoriaCppMod] === END AIMED ACTOR DUMP ===\n"));
        }
