// moria_placement.inl — Build placement: cached BuildComp/HUD/Tab handles,
// ghost cancel, pitch/roll quaternion injection.

        UObject* getCachedBuildComp()
        {
            UObject* comp = m_cachedBuildComp.Get();
            if (comp) return comp;
            std::vector<UObject*> comps;
            findAllOfSafe(STR("MorBuildingComponent"), comps);
            if (!comps.empty() && comps[0])
            {
                m_cachedBuildComp = RC::Unreal::FWeakObjectPtr(comps[0]);
                QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildComp: found MorBuildingComponent {:p}\n"),
                      static_cast<void*>(comps[0]));
                return comps[0];
            }
            return nullptr;
        }


        UObject* getCachedBuildHUD()
        {
            UObject* hud = m_cachedBuildHUD.Get();
            if (hud && safeClassName(hud).find(L"BuildHUD") != std::wstring::npos)
                return hud;

            // Try via BuildComp API first
            UObject* comp = getCachedBuildComp();
            if (comp)
            {
                auto* fn = comp->GetFunctionByNameInChain(STR("GetActiveBuildingWidget"));
                if (fn)
                {
                    struct { UObject* Ret{nullptr}; } params{};
                    safeProcessEvent(comp, fn, &params);
                    if (params.Ret)
                    {
                        m_cachedBuildHUD = RC::Unreal::FWeakObjectPtr(params.Ret);
                        QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildHUD: via GetActiveBuildingWidget -> {:p}\n"),
                              static_cast<void*>(params.Ret));
                        return params.Ret;
                    }
                }
            }

            // Fallback — search by class
            UObject* found = findWidgetByClass(L"UI_WBP_BuildHUDv2_C", false);
            if (found)
            {
                m_cachedBuildHUD = RC::Unreal::FWeakObjectPtr(found);
                return found;
            }
            return nullptr;
        }


        bool isPlacementActive()
        {
            UObject* hud = getCachedBuildHUD();
            if (!hud) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (no HUD)\n")); return false; }
            auto* fn = hud->GetFunctionByNameInChain(STR("IsShowing"));
            if (!fn) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (no IsShowing fn)\n")); return false; }
            struct { bool Ret{false}; } params{};
            safeProcessEvent(hud, fn, &params);
            if (!params.Ret) { QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> false (HUD not showing)\n")); return false; }

            // Re-resolve each call — cheap name-walk, safe across world transitions
            FBoolProperty* bp_recipeSelectMode = resolveBoolProperty(hud, L"recipeSelectMode");
            if (!bp_recipeSelectMode) return false;
            bool recipeSelectMode = bp_recipeSelectMode->GetPropertyValueInContainer(hud);
            bool result = !recipeSelectMode;
            QBLOG(STR("[MoriaCppMod] [QB] isPlacementActive -> {} (recipeSelectMode={})\n"),
                  result ? STR("true") : STR("false"), recipeSelectMode ? STR("true") : STR("false"));
            return result;
        }


        bool isBuildTabShowing()
        {
            static bool s_lastState = false;
            UObject* tab = getCachedBuildTab();
            if (!tab) { s_lastState = false; return false; }
            auto* visFunc = tab->GetFunctionByNameInChain(STR("IsVisible"));
            if (!visFunc) { s_lastState = false; return false; }
            struct { bool Ret{false}; } params{};
            safeProcessEvent(tab, visFunc, &params);
            if (params.Ret != s_lastState)
            {
                QBLOG(STR("[MoriaCppMod] [QB] isBuildTabShowing -> {}\n"), params.Ret ? STR("true") : STR("false"));
                s_lastState = params.Ret;
            }
            return params.Ret;
        }


        void showBuildTab()
        {
            ULONGLONG now = GetTickCount64();
            if (now - m_lastShowHideTime < 500)
            {
                QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: SKIPPED ({}ms since last Show/Hide)\n"),
                      now - m_lastShowHideTime);
                return;
            }
            UObject* tab = getCachedBuildTab();
            if (!tab) { QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: no tab found\n")); return; }
            auto* fn = tab->GetFunctionByNameInChain(STR("Show"));
            QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: calling Show() fn={}\n"), fn ? STR("YES") : STR("NO"));
            if (!safeProcessEvent(tab, fn, nullptr)) { QBLOG(STR("[MoriaCppMod] [QB] showBuildTab: safeProcessEvent FAILED\n")); return; }
            m_lastShowHideTime = now;
        }


        void hideBuildTab()
        {
            ULONGLONG now = GetTickCount64();
            if (now - m_lastShowHideTime < 500)
            {
                QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: SKIPPED ({}ms since last Show/Hide)\n"),
                      now - m_lastShowHideTime);
                return;
            }
            UObject* tab = getCachedBuildTab();
            if (!tab) { QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: no tab found\n")); return; }
            auto* fn = tab->GetFunctionByNameInChain(STR("Hide"));
            QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: calling Hide() fn={}\n"), fn ? STR("YES") : STR("NO"));
            if (!safeProcessEvent(tab, fn, nullptr)) { QBLOG(STR("[MoriaCppMod] [QB] hideBuildTab: safeProcessEvent FAILED\n")); return; }
            m_lastShowHideTime = now;
        }


        bool activateBuildMode()
        {

            UObject* tab = getCachedBuildTab();
            if (tab)
            {
                QBLOG(STR("[MoriaCppMod] [QB] activateBuildMode: using showBuildTab()\n"));
                showBuildTab();
                return true;
            }

            UObject* pawn = getPawn();
            if (!pawn)
            {
                QBLOG(STR("[MoriaCppMod] [QB] activateBuildMode: no pawn found\n"));
                return false;
            }
            auto* fn = pawn->GetFunctionByNameInChain(STR("ConstructMode"));
            if (!fn)
            {
                QBLOG(STR("[MoriaCppMod] [QB] activateBuildMode: ConstructMode not found on pawn\n"));
                return false;
            }
            QBLOG(STR("[MoriaCppMod] [QB] activateBuildMode: calling ConstructMode() on character\n"));
            safeProcessEvent(pawn, fn, nullptr);
            return true;
        }


        // Synthesizing ESC defers cancellation to the game's own input handler.
        // Calling CancelTargeting() via ProcessEvent destroys GATA mid-tick.
        bool cancelPlacementViaAPI()
        {
            QBLOG(STR("[MoriaCppMod] [QB] Cancelling ghost via keybd_event(VK_ESCAPE)\n"));
            keybd_event(VK_ESCAPE, 0, 0, 0);
            keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
            return true;
        }

        UObject* resolveGATA()
        {
            auto* hud = getCachedBuildHUD();
            if (!hud) return nullptr;
            auto* weakPtr = hud->GetValuePtrByPropertyNameInChain<RC::Unreal::FWeakObjectPtr>(STR("TargetActor"));
            if (!weakPtr) return nullptr;
            return weakPtr->Get();
        }


        bool setGATARotation(UObject* gata, float step)
        {
            if (!gata) return false;
            float* snap = gata->GetValuePtrByPropertyNameInChain<float>(STR("SnapRotateIncrement"));
            float* free = gata->GetValuePtrByPropertyNameInChain<float>(STR("FreePlaceRotateIncrement"));
            if (!snap || !free) return false;
            *snap = step;
            *free = step;
            return true;
        }


        void toggleSnap()
        {
            VLOG(STR("[MoriaCppMod] [Snap] toggleSnap() called, current state: {} savedDist={}\n"),
                 m_snapEnabled ? STR("ON") : STR("OFF"), m_savedMaxSnapDistance);

            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Snap] resolveGATA returned nullptr — no piece being placed\n"));
                showErrorBox(L"Snap: place a piece first");
                return;
            }

            float* pSnap = gata->GetValuePtrByPropertyNameInChain<float>(STR("MaxSnapDistance"));
            if (!pSnap)
            {
                VLOG(STR("[MoriaCppMod] [Snap] MaxSnapDistance not found on GATA\n"));
                return;
            }


            if (m_savedMaxSnapDistance < 0.0f)
            {
                m_savedMaxSnapDistance = *pSnap;
                VLOG(STR("[MoriaCppMod] [Snap] Captured original MaxSnapDistance={}\n"), m_savedMaxSnapDistance);
            }

            m_snapEnabled = !m_snapEnabled;
            float newVal = m_snapEnabled ? m_savedMaxSnapDistance : 0.0f;
            *pSnap = newVal;

            VLOG(STR("[MoriaCppMod] [Snap] -> {} MaxSnapDistance={}\n"),
                 m_snapEnabled ? STR("ON") : STR("OFF"), newVal);

            showOnScreen(m_snapEnabled ? L"Snap: ON" : L"Snap: OFF",
                         2.0f, 0.4f, 0.6f, 1.0f);
        }


        void restoreSnap()
        {
            if (m_snapEnabled) return;
            m_snapEnabled = true;
            VLOG(STR("[MoriaCppMod] [Snap] Snap auto-restored to ON after placement (GATA will spawn fresh)\n"));
        }


        void onGhostAppeared()
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Placement] onGhostAppeared: resolveGATA returned nullptr (ghost not yet spawned)\n"));

                return;
            }


            if (!m_snapEnabled && m_savedMaxSnapDistance >= 0.0f)
            {
                float* pSnap = gata->GetValuePtrByPropertyNameInChain<float>(STR("MaxSnapDistance"));
                if (pSnap)
                {
                    *pSnap = 0.0f;
                    VLOG(STR("[MoriaCppMod] [Placement] Re-applied snap OFF to new GATA\n"));
                }
            }
            setGATARotation(gata, static_cast<float>(s_overlay.rotationStep.load()));
        }


        // ---- Pitch/Roll Building Placement ----
        // Offsets resolved at runtime via reflection (safe across game updates)
        int m_offTraceResults{-1};       // GATA → FMorPerformTraceResults TraceResults
        int m_offLastTraceResults{-1};   // GATA → FMorPerformTraceResults LastTraceResults
        int m_offTargetRotation{-1};     // FMorPerformTraceResults → FRotator TargetRotation
        int m_offCopiedComponents{-1};   // Reticle → TArray<UPrimitiveComponent*>
        int m_offRelativeRotation{-1};   // USceneComponent → FRotator RelativeRotation
        int m_offRelativeLocation{-1};   // USceneComponent → FVector RelativeLocation

        // Cache original RelativeLocations AND RelativeRotations so we always compute from originals
        struct OriginalRelLoc { float x, y, z; };
        struct OriginalRelRot { float pitch, yaw, roll; };
        std::vector<OriginalRelLoc> m_origRelLocs;
        std::vector<OriginalRelRot> m_origRelRots;
        OriginalRelLoc m_pivotPoint{0, 0, 0};  // snap component = center of assembly
        bool m_origRelLocsCached{false};
        static constexpr int GATA_YAW_ACCUMULATOR = 0x0658;  // NOT reflected — must stay hardcoded

        bool resolveGATAOffsets(UObject* gata)
        {
            if (m_offTraceResults >= 0) return true;  // already resolved
            auto* p1 = gata->GetPropertyByNameInChain(STR("TraceResults"));
            auto* p2 = gata->GetPropertyByNameInChain(STR("LastTraceResults"));
            if (!p1 || !p2) { VLOG(STR("[MoriaCppMod] [PitchRoll] Failed to resolve TraceResults offsets\n")); return false; }
            m_offTraceResults = p1->GetOffset_Internal();
            m_offLastTraceResults = p2->GetOffset_Internal();

            // Resolve TargetRotation within the TraceResults struct
            // p1 is known to be FStructProperty because TraceResults is a struct field
            auto* structProp = static_cast<RC::Unreal::FStructProperty*>(p1);
            if (!structProp) { VLOG(STR("[MoriaCppMod] [PitchRoll] TraceResults is not FStructProperty\n")); return false; }
            UScriptStruct* innerStruct = structProp->GetStruct();
            if (innerStruct)
            {
                for (FProperty* sp : innerStruct->ForEachProperty())
                {
                    if (sp->GetName() == STR("TargetRotation")) { m_offTargetRotation = sp->GetOffset_Internal(); break; }
                }
            }
            if (m_offTargetRotation < 0) { VLOG(STR("[MoriaCppMod] [PitchRoll] Failed to resolve TargetRotation offset\n")); return false; }

            VLOG(STR("[MoriaCppMod] [PitchRoll] Resolved: TraceResults=+0x{:04X} LastTrace=+0x{:04X} TargetRot=+0x{:04X}\n"),
                 m_offTraceResults, m_offLastTraceResults, m_offTargetRotation);
            return true;
        }

        bool resolveReticleOffsets(UObject* reticle)
        {
            if (m_offCopiedComponents >= 0 && m_offRelativeRotation >= 0 && m_offRelativeLocation >= 0) return true;
            if (m_offCopiedComponents < 0)
            {
                auto* p = reticle->GetPropertyByNameInChain(STR("CopiedComponents"));
                if (p) m_offCopiedComponents = p->GetOffset_Internal();
            }
            // RelativeRotation + RelativeLocation — resolve from USceneComponent class
            if (m_offRelativeRotation < 0 || m_offRelativeLocation < 0)
            {
                auto* sceneClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneComponent"));
                if (sceneClass)
                {
                    for (auto* sp : sceneClass->ForEachProperty())
                    {
                        if (sp->GetName() == STR("RelativeRotation")) m_offRelativeRotation = sp->GetOffset_Internal();
                        if (sp->GetName() == STR("RelativeLocation")) m_offRelativeLocation = sp->GetOffset_Internal();
                    }
                }
            }
            if (m_offCopiedComponents < 0 || m_offRelativeRotation < 0 || m_offRelativeLocation < 0)
            {
                VLOG(STR("[MoriaCppMod] [PitchRoll] Failed to resolve reticle offsets (CC={} RR={} RL={})\n"),
                     m_offCopiedComponents, m_offRelativeRotation, m_offRelativeLocation);
                return false;
            }
            VLOG(STR("[MoriaCppMod] [PitchRoll] Resolved: CopiedComponents=+0x{:04X} RelativeRotation=+0x{:04X} RelativeLocation=+0x{:04X}\n"),
                 m_offCopiedComponents, m_offRelativeRotation, m_offRelativeLocation);
            return true;
        }

        float m_experimentPitch{0.0f};
        float m_experimentRoll{0.0f};
        bool  m_pitchRollActive{false};

        // Read FRotator (Pitch, Yaw, Roll) from raw memory
        struct PRot { float pitch; float yaw; float roll; };

        PRot readGATARotation(UObject* gata, int traceOffset)
        {
            if (traceOffset < 0 || m_offTargetRotation < 0) return {0, 0, 0};
            auto* base = reinterpret_cast<uint8_t*>(gata);
            auto* rot = reinterpret_cast<float*>(base + traceOffset + m_offTargetRotation);
            return { rot[0], rot[1], rot[2] };
        }

        void writeGATARotation(UObject* gata, int traceOffset, float pitch, float yaw, float roll)
        {
            if (traceOffset < 0 || m_offTargetRotation < 0) return;
            auto* base = reinterpret_cast<uint8_t*>(gata);
            auto* rot = reinterpret_cast<float*>(base + traceOffset + m_offTargetRotation);
            rot[0] = pitch;
            rot[1] = yaw;
            rot[2] = roll;
        }

        void injectPitchRoll(float pitch, float roll)
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [PitchRoll] No GATA — place a piece first\n"));
                showErrorBox(L"Pitch/Roll: place a piece first");
                return;
            }

            if (!resolveGATAOffsets(gata)) return;

            // Read current yaw from TraceResults
            auto tr = readGATARotation(gata, m_offTraceResults);

            // Write pitch/roll to both TraceResults and LastTraceResults, preserve yaw
            writeGATARotation(gata, m_offTraceResults, pitch, tr.yaw, roll);
            writeGATARotation(gata, m_offLastTraceResults, pitch, tr.yaw, roll);

            m_experimentPitch = pitch;
            m_experimentRoll = roll;
            m_pitchRollActive = true;

            VLOG(STR("[MoriaCppMod] [PitchRoll] INJECTED: Pitch={:.1f} Roll={:.1f} (Yaw={:.1f} preserved)\n"),
                 pitch, roll, tr.yaw);
            showOnScreen(std::format(L"Pitch={:.0f}\u00B0 Roll={:.0f}\u00B0", pitch, roll),
                         3.0f, 1.0f, 0.8f, 0.0f);
        }

        // Rotate the multipart ghost preview to match pitch/roll.
        // Uses K2_SetRelativeLocationAndRotation (UFUNCTION) on each MorBreakableMeshPiece.
        // This properly updates ComponentToWorld AND marks the render transform dirty.
        // Key insight: work in RELATIVE space — the parent actor's yaw is applied automatically.
        // Original RelativeLocations are cached on first use to avoid accumulation drift.
        // Non-mesh components (MorConstructionSnapComponent) are skipped.
        //
        // Rotation math: UE4 FRotationTranslationMatrix (row-vector convention V_out = V * M)
        //   Pitch = Y-axis rotation, Roll = X-axis rotation
        // ============================================================================
        // Quaternion helpers for multipart ghost rotation
        //
        // CRITICAL: All quaternion functions MUST match UE4's conventions exactly.
        // UE4 uses a NEGATED X,Y convention compared to standard math quaternions:
        //   UE4 pure pitch P: (0, -sin(P/2), 0, cos(P/2))   [standard: (0, +sin, 0, cos)]
        //   UE4 pure roll  R: (-sin(R/2), 0, 0, cos(R/2))   [standard: (+sin, 0, 0, cos)]
        //   UE4 pure yaw   Y: (0, 0, sin(Y/2), cos(Y/2))    [same as standard]
        //
        // Getting this wrong causes quaternion compositions (quatMul) to produce
        // INCONSISTENT position vs rotation transforms, making multipart ghost pieces
        // separate even though the math looks correct in isolation.
        //
        // Reference: UE4 FRotator::Quaternion() and FQuat::Rotator() in RotationMatrix.h
        // ============================================================================
        struct Quat4 { float x, y, z, w; };

        // Construct a quaternion for combined pitch (Y-axis) + roll (X-axis) rotation.
        // Matches UE4: FRotator(pitchDeg, 0, rollDeg).Quaternion()
        // Result = Qpitch * Qroll with UE4's negated X,Y convention.
        static Quat4 pitchRollToQuat(float pitchDeg, float rollDeg)
        {
            constexpr float D2R_HALF = 3.14159265f / 360.0f;
            float hp = pitchDeg * D2R_HALF;
            float hr = rollDeg * D2R_HALF;
            float sp = sinf(hp), cp = cosf(hp);
            float sr = sinf(hr), cr = cosf(hr);
            // UE4 convention: Qpitch(Y) = (0, -sp, 0, cp), Qroll(X) = (-sr, 0, 0, cr)
            // Qpitch * Qroll via Hamilton product:
            return { -cp*sr, -sp*cr, -sp*sr, cp*cr };
        }

        // Hamilton product: composes two rotations (standard formula, works with any convention)
        static Quat4 quatMul(const Quat4& a, const Quat4& b)
        {
            return {
                a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
            };
        }

        // Rotate vector v by quaternion q: v' = q * v * q_conjugate
        // Uses the optimized cross-product form (avoids full quat multiply)
        static void quatRotVec(const Quat4& q, float vx, float vy, float vz,
                                float& ox, float& oy, float& oz)
        {
            float tx = 2.0f * (q.y*vz - q.z*vy);
            float ty = 2.0f * (q.z*vx - q.x*vz);
            float tz = 2.0f * (q.x*vy - q.y*vx);
            ox = vx + q.w*tx + (q.y*tz - q.z*ty);
            oy = vy + q.w*ty + (q.z*tx - q.x*tz);
            oz = vz + q.w*tz + (q.x*ty - q.y*tx);
        }

        struct EulerAngles { float pitch, yaw, roll; };
        static EulerAngles quatToEuler(const Quat4& q)
        {
            // Matches UE4 FQuat::Rotator() exactly
            constexpr float R2D = 180.0f / 3.14159265f;
            float singularityTest = q.z * q.x - q.w * q.y;
            float pitch = (fabsf(singularityTest) >= 0.4999995f)
                ? copysignf(90.0f, singularityTest)
                : asinf(2.0f * singularityTest) * R2D;
            float yaw  = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y*q.y + q.z*q.z)) * R2D;
            float roll = atan2f(-2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x*q.x + q.y*q.y)) * R2D;
            return {pitch, yaw, roll};
        }

        // Convert UE4 FRotator (Pitch,Yaw,Roll) → FQuat.
        // Matches UE4 FRotator::Quaternion() exactly.
        // Decomposition order: Q = Qyaw(Z) * Qpitch(Y) * Qroll(X) with UE4's sign convention.
        static Quat4 rotatorToQuat(float pitchDeg, float yawDeg, float rollDeg)
        {
            constexpr float D2R_HALF = 3.14159265f / 360.0f;
            float hp = pitchDeg * D2R_HALF;
            float hy = yawDeg * D2R_HALF;
            float hr = rollDeg * D2R_HALF;
            float sp = sinf(hp), cp = cosf(hp);
            float sy = sinf(hy), cy = cosf(hy);
            float sr = sinf(hr), cr = cosf(hr);
            // UE4 FRotator::Quaternion() exact formula
            return {
                 cr*sp*sy - sr*cp*cy,   // X
                -cr*sp*cy - sr*cp*sy,   // Y
                 cr*cp*sy - sr*sp*cy,   // Z
                 cr*cp*cy + sr*sp*sy    // W
            };
        }

        void rotateReticleVisual(UObject* gata, float pitchDeg, float rollDeg)
        {
            auto* getReticleFn = gata->GetFunctionByNameInChain(STR("GetReticleActor"));
            if (!getReticleFn) return;

            struct { UObject* ReturnValue{nullptr}; } retParams{};
            safeProcessEvent(gata, getReticleFn, &retParams);
            UObject* reticle = retParams.ReturnValue;
            if (!reticle) return;
            if (!resolveReticleOffsets(reticle)) return;

            // Root has world position + yaw set by GATA; children's RelLoc/RelRot are in root space.
            auto* rootProp = reticle->GetPropertyByNameInChain(STR("RootComponent"));
            if (!rootProp) return;
            UObject* rootComp = *reinterpret_cast<UObject**>(
                reinterpret_cast<uint8_t*>(reticle) + rootProp->GetOffset_Internal());
            if (!rootComp || !isObjectAlive(rootComp)) return;

            auto* reticleBase = reinterpret_cast<uint8_t*>(reticle);
            struct TArrayRaw { void** data; int32_t count; int32_t max; };
            auto* arr = reinterpret_cast<TArrayRaw*>(reticleBase + m_offCopiedComponents);
            if (!arr->data || arr->count <= 0) return;

            // Cache original RelLocs and RelRots on first press
            if (!m_origRelLocsCached)
            {
                m_origRelLocs.resize(arr->count);
                m_origRelRots.resize(arr->count);
                m_pivotPoint = {0, 0, 0};
                bool foundSnap = false;

                // Log root info
                auto* rootBase = reinterpret_cast<uint8_t*>(rootComp);
                float* rootRR = reinterpret_cast<float*>(rootBase + m_offRelativeRotation);
                VLOG(STR("[MoriaCppMod] [PitchRoll] Root {} RelRot=({:.1f},{:.1f},{:.1f})\n"),
                     safeClassName(rootComp), rootRR[0], rootRR[1], rootRR[2]);

                VLOG(STR("[MoriaCppMod] [PitchRoll] Caching {} components:\n"), arr->count);
                for (int ci = 0; ci < arr->count; ci++)
                {
                    auto* c = reinterpret_cast<uint8_t*>(arr->data[ci]);
                    if (!c || !isObjectAlive(reinterpret_cast<UObject*>(c)))
                    { m_origRelLocs[ci] = {0,0,0}; m_origRelRots[ci] = {0,0,0}; continue; }
                    float* rl = reinterpret_cast<float*>(c + m_offRelativeLocation);
                    float* rr = reinterpret_cast<float*>(c + m_offRelativeRotation);
                    m_origRelLocs[ci] = {rl[0], rl[1], rl[2]};
                    m_origRelRots[ci] = {rr[0], rr[1], rr[2]};

                    auto* cObj = reinterpret_cast<UObject*>(c);
                    std::wstring cls = safeClassName(cObj);

                    if (!foundSnap && cls.find(L"SnapComponent") != std::wstring::npos)
                    {
                        m_pivotPoint = {rl[0], rl[1], rl[2]};
                        foundSnap = true;
                        VLOG(STR("[MoriaCppMod] [PitchRoll]   [{}] {} RelLoc=({:.1f},{:.1f},{:.1f}) ** PIVOT **\n"),
                             ci, cls, rl[0], rl[1], rl[2]);
                        continue;
                    }

                    VLOG(STR("[MoriaCppMod] [PitchRoll]   [{}] {} RelLoc=({:.1f},{:.1f},{:.1f}) RelRot=({:.1f},{:.1f},{:.1f})\n"),
                         ci, cls, rl[0], rl[1], rl[2], rr[0], rr[1], rr[2]);
                }
                if (foundSnap)
                    VLOG(STR("[MoriaCppMod] [PitchRoll] Pivot: ({:.1f},{:.1f},{:.1f})\n"),
                         m_pivotPoint.x, m_pivotPoint.y, m_pivotPoint.z);
                m_origRelLocsCached = true;
            }

            // Build pitch/roll quaternion directly in ROOT SPACE.
            // Children's RelLoc/RelRot are defined in root space. Root already has yaw from GATA.
            // Applying pitch/roll in root space = piece-local pitch/roll (follows facing direction).
            //
            // IMPORTANT: No world-to-root conjugation needed because pitchRollToQuat uses
            // UE4-correct conventions. With wrong conventions (X,Y negated), the position
            // rotation and rotation composition would be INCONSISTENT — positions move one way
            // while mesh orientations rotate another — causing multipart pieces to separate.
            // The fix was matching UE4's FRotator::Quaternion() / FQuat::Rotator() exactly.
            Quat4 localPR = pitchRollToQuat(pitchDeg, rollDeg);

            for (int i = 0; i < arr->count; i++)
            {
                auto* comp = reinterpret_cast<uint8_t*>(arr->data[i]);
                if (!comp) continue;
                if (i >= static_cast<int>(m_origRelLocs.size())) break;

                auto* compObj = reinterpret_cast<UObject*>(comp);
                if (!isObjectAlive(compObj)) continue;
                if (!compObj->GetFunctionByNameInChain(STR("GetLocalBounds")))
                    continue;

                // POSITION: rigid-body rotation around the snap/pivot point.
                // Offset from pivot → rotate by localPR → add pivot back.
                // This keeps the assembly together because ALL pieces use the SAME
                // quaternion for both position and rotation (rigid-body invariant).
                float dx = m_origRelLocs[i].x - m_pivotPoint.x;
                float dy = m_origRelLocs[i].y - m_pivotPoint.y;
                float dz = m_origRelLocs[i].z - m_pivotPoint.z;

                float rx, ry, rz;
                quatRotVec(localPR, dx, dy, dz, rx, ry, rz);

                float newRelX = m_pivotPoint.x + rx;
                float newRelY = m_pivotPoint.y + ry;
                float newRelZ = m_pivotPoint.z + rz;

                // ROTATION: compose localPR * originalRelRot.
                // Each mesh piece has its own original RelRot (e.g., yaw ±90°) that orients
                // the mesh within the assembly. We COMPOSE (not replace) with our rotation
                // to preserve the original orientation while adding pitch/roll on top.
                Quat4 origQuat = rotatorToQuat(m_origRelRots[i].pitch, m_origRelRots[i].yaw, m_origRelRots[i].roll);
                Quat4 finalQuat = quatMul(localPR, origQuat);
                EulerAngles finalRot = quatToEuler(finalQuat);

                auto* setFn = compObj->GetFunctionByNameInChain(STR("K2_SetRelativeLocationAndRotation"));
                if (setFn)
                {
                    int sz = setFn->GetParmsSize();
                    std::vector<uint8_t> params(sz, 0);

                    int offLoc = -1, offRot = -1, offTeleport = -1;
                    for (auto* prop : setFn->ForEachProperty())
                    {
                        auto pname = prop->GetName();
                        if (pname == STR("NewLocation")) offLoc = prop->GetOffset_Internal();
                        else if (pname == STR("NewRotation")) offRot = prop->GetOffset_Internal();
                        else if (pname == STR("bTeleport")) offTeleport = prop->GetOffset_Internal();
                    }

                    if (offLoc >= 0 && offRot >= 0)
                    {
                        auto* loc = reinterpret_cast<float*>(params.data() + offLoc);
                        loc[0] = newRelX; loc[1] = newRelY; loc[2] = newRelZ;

                        auto* rot = reinterpret_cast<float*>(params.data() + offRot);
                        rot[0] = finalRot.pitch; rot[1] = finalRot.yaw; rot[2] = finalRot.roll;

                        if (offTeleport >= 0)
                            *reinterpret_cast<bool*>(params.data() + offTeleport) = true;

                        safeProcessEvent(compObj, setFn, params.data());
                    }
                }
            }
        }

        void tickPitchRoll()
        {
            if (!m_pitchRollActive) return;
            if (!m_pitchRotateEnabled && !m_rollRotateEnabled) { m_pitchRollActive = false; return; }
            UObject* gata = resolveGATA();
            if (!gata) { m_pitchRollActive = false; return; }

            if (!resolveGATAOffsets(gata)) { m_pitchRollActive = false; return; }

            // Re-inject every frame because GATA::Tick overwrites TraceResults
            auto tr = readGATARotation(gata, m_offTraceResults);
            writeGATARotation(gata, m_offTraceResults, m_experimentPitch, tr.yaw, m_experimentRoll);
            writeGATARotation(gata, m_offLastTraceResults, m_experimentPitch, tr.yaw, m_experimentRoll);

            // Rotate the preview ghost visually (per-component — GATA doesn't cascade to multipart children)
            rotateReticleVisual(gata, m_experimentPitch, m_experimentRoll);
        }

        void clearPitchRoll()
        {
            m_pitchRollActive = false;
            m_experimentPitch = 0.0f;
            m_experimentRoll = 0.0f;
            m_origRelLocsCached = false;
            m_origRelLocs.clear();
            m_origRelRots.clear();
            m_pivotPoint = {0, 0, 0};
            VLOG(STR("[MoriaCppMod] [PitchRoll] Cleared\n"));
        }

        // Hook: intercept BuildNewConstruction to inject pitch/roll into the FTransform
        // Approach: build a pitch/roll quaternion and multiply it onto the existing quat.
        // The existing quat already has yaw — we compose our rotation on top.
        void onBuildNewConstruction(UObject* context, UFunction* func, void* parms)
        {
            if (!m_pitchRollActive) return;
            if (!parms) return;

            // MP guard: only patch builds from the local player's build component
            // On a listen server, this prevents applying the host's pitch/roll to remote players' builds
            UObject* localComp = m_cachedBuildComp.Get();
            if (localComp && context != localComp)
            {
                VLOG(STR("[MoriaCppMod] [PitchRoll] BuildNewConstruction SKIPPED — context {} != localComp {} (remote player's build)\n"),
                     (void*)context, (void*)localComp);
                return;
            }

            for (auto* prop : func->ForEachProperty())
            {
                if (prop->GetName() != STR("Transform")) continue;

                int offset = prop->GetOffset_Internal();
                float* quat = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(parms) + offset);

                VLOG(STR("[MoriaCppMod] [PitchRoll] BuildNewConstruction INTERCEPTED!\n"));
                VLOG(STR("[MoriaCppMod] [PitchRoll]   BEFORE: Quat({:.4f}, {:.4f}, {:.4f}, {:.4f})\n"),
                     quat[0], quat[1], quat[2], quat[3]);

                // Build pitch/roll quaternion matching UE4 FRotator::Quaternion() exactly.
                // CRITICAL: X and Y components are NEGATED vs standard math convention.
                // Getting this wrong produces roll instead of pitch in the placed result.
                constexpr float DEG2RAD = 3.14159265f / 180.0f;
                float hp = m_experimentPitch * DEG2RAD * 0.5f;
                float hr = m_experimentRoll * DEG2RAD * 0.5f;
                float sp = sinf(hp), cp = cosf(hp);
                float sr = sinf(hr), cr = cosf(hr);

                // UE4 convention: Qpitch(Y) = (0,-sp,0,cp), Qroll(X) = (-sr,0,0,cr)
                // Qpitch * Qroll via Hamilton product:
                float prX = -cp * sr;
                float prY = -sp * cr;
                float prZ = -sp * sr;
                float prW = cp * cr;

                // Multiply: existingQuat * pitchRollQuat  (order matters!)
                // existingQ has yaw from GATA. Multiplying on the RIGHT applies pitch/roll
                // in the PIECE-LOCAL frame (after yaw). This produces correct euler:
                //   yaw270 * pitch45 → euler(pitch=45, yaw=270, roll=0)
                // The WRONG order (pitchRollQ * existingQ) applies in WORLD frame and
                // produces euler(pitch=0, yaw=270, roll=45) — pitch becomes roll!
                float ax = quat[0], ay = quat[1], az = quat[2], aw = quat[3];
                float bx = prX, by = prY, bz = prZ, bw = prW;

                quat[0] = aw*bx + ax*bw + ay*bz - az*by;
                quat[1] = aw*by - ax*bz + ay*bw + az*bx;
                quat[2] = aw*bz + ax*by - ay*bx + az*bw;
                quat[3] = aw*bw - ax*bx - ay*by - az*bz;

                VLOG(STR("[MoriaCppMod] [PitchRoll]   AFTER:  Quat({:.4f}, {:.4f}, {:.4f}, {:.4f})\n"),
                     quat[0], quat[1], quat[2], quat[3]);

                // Adjust translation so the snap/pivot point stays at the same world
                // position after rotation. Without this the placed piece drifts because
                // the rotation pivot (FTransform origin) doesn't coincide with the snap
                // point — the ghost preview handles this via rotateReticleVisual but the
                // FTransform only has one origin.
                //
                // Math: if pivot is at local offset P, rotating the piece changes pivot's
                // world position from T + Q_orig*P to T + Q_new*P.  Compensate:
                //   T_new = T + Q_orig*P - Q_new*P
                if (m_pivotPoint.x != 0.0f || m_pivotPoint.y != 0.0f || m_pivotPoint.z != 0.0f)
                {
                    // FTransform layout: Quat(16 bytes) then Translation(12 bytes)
                    float* trans = quat + 4;

                    // Pivot rotated by original yaw-only quaternion
                    Quat4 qOrig = {ax, ay, az, aw};
                    float owx, owy, owz;
                    quatRotVec(qOrig, m_pivotPoint.x, m_pivotPoint.y, m_pivotPoint.z, owx, owy, owz);

                    // Pivot rotated by new (yaw + pitch/roll) quaternion
                    Quat4 qNew = {quat[0], quat[1], quat[2], quat[3]};
                    float nwx, nwy, nwz;
                    quatRotVec(qNew, m_pivotPoint.x, m_pivotPoint.y, m_pivotPoint.z, nwx, nwy, nwz);

                    trans[0] += owx - nwx;
                    trans[1] += owy - nwy;
                    trans[2] += owz - nwz;

                    VLOG(STR("[MoriaCppMod] [PitchRoll]   Pivot=({:.1f},{:.1f},{:.1f}) TransAdj=({:.2f},{:.2f},{:.2f})\n"),
                         m_pivotPoint.x, m_pivotPoint.y, m_pivotPoint.z,
                         owx - nwx, owy - nwy, owz - nwz);
                }

                VLOG(STR("[MoriaCppMod] [PitchRoll]   Applied Pitch={:.1f} Roll={:.1f}\n"),
                     m_experimentPitch, m_experimentRoll);
                break;
            }
        }


        void onGhostDisappeared()
        {
            clearPitchRoll();

            if (m_qbPhase != PlacePhase::Idle)
                return;

            ULONGLONG now = GetTickCount64();
            if (now - m_lastQBSelectTime < 500)
                return;
            VLOG(STR("[MoriaCppMod] [Placement] onGhostDisappeared: greying out snap slot\n"));
        }


        SelectResult selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;
            const std::wstring& slotTexture = m_recipeSlots[slot].textureName;

            QBLOG(STR("[MoriaCppMod] [QuickBuild] SELECT: '{}' tex='{}' (F{}) hasHandle={}\n"),
                  targetName, slotTexture, slot + 1, m_recipeSlots[slot].hasHandle);

            UObject* buildHUD = getCachedBuildHUD();


            std::vector<UObject*> widgets;
            findAllOfSafe(STR("UI_WBP_Build_Item_Medium_C"), widgets);

            UObject* matchedWidget = nullptr;
            int visibleCount = 0;
            for (auto* w : widgets)
            {
                if (!w || !isObjectAlive(w)) continue;


                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    safeProcessEvent(w, visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                if (!name.empty() && name == targetName)
                {

                    if (!slotTexture.empty())
                    {
                        std::wstring widgetTex = extractIconTextureName(w);
                        if (widgetTex != slotTexture) continue;
                    }
                    matchedWidget = w;
                    break;
                }
            }

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   checked {} visible widgets, match: {}\n"), visibleCount, matchedWidget ? L"YES" : L"NO");

            if (!matchedWidget)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] No match among {} visible widgets\n"), visibleCount);
                return (visibleCount == 0) ? SelectResult::Loading : SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            resolveBSEOffsets(func);
            if (!bseOffsetsValid())
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] blockSelectedEvent param resolution failed\n"));
                return SelectResult::NotFound;
            }

            std::vector<uint8_t> params(s_bse.parmsSize, 0);
            bool gotFreshBLock = false;


            if (s_off_bLock == -2)
            {
                resolveOffset(matchedWidget, L"bLock", s_off_bLock);
                probeRecipeBlockStruct(matchedWidget);
            }


            if (s_off_bLock >= 0)
            {
                // Defense: s_off_bLock is process-global and cached on first
                // resolve. If a future patch reorders URecipeBlockWidget the
                // resolver could land on a stale offset. isReadableMemory
                // catches the resulting bad-pointer read before memcpy faults.
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                if (isReadableMemory(widgetBase + s_off_bLock, BLOCK_DATA_SIZE))
                {
                    std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                    gotFreshBLock = true;
                    QBLOG(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (@0x{:X})\n"), s_off_bLock);
                }
                else
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild]   bLock@0x{:X} unreadable, falling back to saved\n"), s_off_bLock);
                }
            }


            if (!gotFreshBLock && m_recipeSlots[slot].hasBLockData)
            {
                std::memcpy(params.data() + s_bse.bLock, m_recipeSlots[slot].bLockData, BLOCK_DATA_SIZE);
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using SAVED bLock (may be stale)\n"));
            }
            else if (!gotFreshBLock)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   WARNING: no bLock data at all, using zeros\n"));
            }

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;

            QBLOG(STR("[MoriaCppMod] [QuickBuild]   calling blockSelectedEvent with selfRef={:p}\n"), static_cast<void*>(matchedWidget));

            m_isAutoSelecting = true;
            struct AutoSelectGuardFB
            {
                bool& flag;
                ~AutoSelectGuardFB() { flag = false; }
            } guardFB{m_isAutoSelecting};
            if (!safeProcessEvent(buildTab, func, params.data())) { QBLOG(STR("[MoriaCppMod] [QB] blockSelectedEvent safeProcessEvent FAILED\n")); return SelectResult::NotFound; }
            m_isAutoSelecting = false;


            if (buildHUD)
                cacheRecipeHandleForSlot(buildHUD, slot);

            logBLockDiagnostics(L"BUILD", targetName, params.data());

            showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;
            refreshActionBar();

            m_activeBuilderSlot = slot;
            updateBuildersBar();

            onGhostAppeared();

            return SelectResult::Found;
        }


        // build a 16-byte FMor*RecipeRowHandle from a row name + DT.
        // Layout: [DT* 8 bytes][FName.CI 4][FName.Number 4]. Returns true on success.
        // Used by both F-key and target-build DIRECT paths so they share one
        // robust path: rebuilds the handle from the still-valid row name on
        // every press, instead of trusting a cached DT* that may have aged out
        // across world transitions / GC.
        bool buildSyntheticRecipeHandle(const std::wstring& rowName, uint8_t* outHandle16)
        {
            if (rowName.empty() || !outHandle16) return false;
            if (!m_dtConstructions.isBound()) m_dtConstructions.bind(L"DT_Constructions");
            UObject* dt = m_dtConstructions.table;
            if (!dt || !isObjectAlive(dt)) return false;
            std::memset(outHandle16, 0, RECIPE_HANDLE_SIZE);
            *reinterpret_cast<UObject**>(outHandle16 + 0) = dt;
            try {
                FName rowFName(rowName.c_str(), FNAME_Add);
                std::memcpy(outHandle16 + 8, &rowFName, sizeof(FName));
            } catch (...) {
                return false;
            }
            return true;
        }

        bool trySelectRecipeByHandle(UObject* buildHUD, const uint8_t* handleData)
        {
            if (!buildHUD || !handleData || !isObjectAlive(buildHUD)) return false;
            auto* selectFn = buildHUD->GetFunctionByNameInChain(STR("SelectRecipe"));
            if (!selectFn)
            {
                QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe not found on build HUD\n"));
                return false;
            }
            int pSz = selectFn->GetParmsSize();
            if (pSz < RECIPE_HANDLE_SIZE)
            {
                QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe parmsSize={} too small (need {})\n"), pSz, RECIPE_HANDLE_SIZE);
                return false;
            }
            std::vector<uint8_t> params(pSz, 0);
            std::memcpy(params.data(), handleData, RECIPE_HANDLE_SIZE);
            if (!safeProcessEvent(buildHUD, selectFn, params.data())) return false;
            uint32_t ci = *reinterpret_cast<const uint32_t*>(handleData + 8);
            QBLOG(STR("[MoriaCppMod] [QB] SelectRecipe called with handle CI={}\n"), ci);
            return true;
        }


        void cacheRecipeHandleForSlot(UObject* buildHUD, int slot)
        {
            if (!buildHUD || slot < 0 || slot >= QUICK_BUILD_SLOTS) return;
            auto* getHandleFn = buildHUD->GetFunctionByNameInChain(STR("GetSelectedRecipeHandle"));
            if (!getHandleFn) return;
            int hSz = getHandleFn->GetParmsSize();
            if (hSz < RECIPE_HANDLE_SIZE) return;
            std::vector<uint8_t> hParams(hSz, 0);
            safeProcessEvent(buildHUD, getHandleFn, hParams.data());
            std::memcpy(m_recipeSlots[slot].recipeHandle, hParams.data(), RECIPE_HANDLE_SIZE);
            m_recipeSlots[slot].hasHandle = true;
            uint32_t ci = *reinterpret_cast<uint32_t*>(hParams.data() + 8);
            QBLOG(STR("[MoriaCppMod] [QB] Cached recipe handle for F{}: CI={}\n"), slot + 1, ci);
        }


        static std::wstring normalizeForMatch(const std::wstring& s)
        {
            std::wstring out;
            out.reserve(s.size());
            for (wchar_t c : s)
            {
                if (std::iswalnum(c)) out += std::towlower(c);
            }
            return out;
        }

        SelectResult selectRecipeByTargetName(UObject* buildTab)
        {
            QBLOG(STR("[MoriaCppMod] [TargetBuild] Searching: name='{}' recipeRef='{}' bLockOffset=0x{:X}\n"),
                                            m_targetBuildName,
                                            m_targetBuildRecipeRef,
                                            s_off_bLock);


            std::vector<std::pair<std::wstring, uint32_t>> targetFNames;
            {

                FName fn1(m_targetBuildRecipeRef.c_str());
                uint32_t ci1 = fn1.GetComparisonIndex();
                targetFNames.push_back({m_targetBuildRecipeRef, ci1});


                std::wstring noBP = m_targetBuildRecipeRef;
                if (noBP.size() > 3 && noBP.substr(0, 3) == L"BP_") noBP.erase(0, 3);
                FName fn2(noBP.c_str());
                uint32_t ci2 = fn2.GetComparisonIndex();
                targetFNames.push_back({noBP, ci2});

                QBLOG(STR("[MoriaCppMod] [TargetBuild] FName CIs: full='{}' CI={}, short='{}' CI={}\n"),
                                                m_targetBuildRecipeRef,
                                                ci1,
                                                noBP,
                                                ci2);
            }

            std::vector<UObject*> widgets;
            findAllOfSafe(STR("UserWidget"), widgets);

            UObject* matchedWidget = nullptr;
            std::wstring matchedName;
            int visibleCount = 0;
            int bLockNullCount = 0, bLockMemFailCount = 0, variantsEmptyCount = 0;

            for (auto* w : widgets)
            {
                if (!w || !isObjectAlive(w)) continue;
                std::wstring cls = safeClassName(w);
                if (cls != L"UI_WBP_Build_Item_Medium_C") continue;

                auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                if (visFunc)
                {
                    struct
                    {
                        bool Ret{false};
                    } vp{};
                    safeProcessEvent(w, visFunc, &vp);
                    if (!vp.Ret) continue;
                }

                visibleCount++;
                std::wstring name = readWidgetDisplayName(w);
                bool isFirstFew = (visibleCount <= 5);


                if (s_off_bLock == -2)
                {
                    resolveOffset(w, L"bLock", s_off_bLock);
                    probeRecipeBlockStruct(w);
                }


                if (isFirstFew)
                {
                    std::wstring objName(w->GetName());
                    QBLOG(STR("[MoriaCppMod] [TargetBuild]   W[{}] obj='{}' display='{}'\n"), visibleCount, objName, name);
                }


                if (!matchedWidget && s_off_bLock >= 0 && !m_targetBuildRecipeRef.empty())
                {
                    uint8_t* widgetBase = reinterpret_cast<uint8_t*>(w);
                    uint8_t* bLock = widgetBase + s_off_bLock;

                    if (!isReadableMemory(bLock, BLOCK_DATA_SIZE))
                    {
                        bLockNullCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock=NULL\n"));
                    }
                    else if (!isReadableMemory(bLock + rbVariantsOff(), 16))
                    {
                        bLockMemFailCount++;
                        if (isFirstFew) QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock+104 not readable\n"));
                    }
                    else
                    {
                        uint8_t* variantsPtr = *reinterpret_cast<uint8_t**>(bLock + rbVariantsOff());
                        int32_t variantsCount = *reinterpret_cast<int32_t*>(bLock + rbVariantsNumOff());

                        if (isFirstFew)
                        {

                            uint32_t tagCI = *reinterpret_cast<uint32_t*>(bLock);
                            int32_t tagNum = *reinterpret_cast<int32_t*>(bLock + 4);
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     bLock tag CI={} Num={} | variants={} ptr={:p}\n"),
                                                            tagCI,
                                                            tagNum,
                                                            variantsCount,
                                                            (void*)variantsPtr);
                        }

                        if (variantsCount <= 0 || !variantsPtr)
                        {
                            variantsEmptyCount++;
                        }
                        else if (isReadableMemory(variantsPtr, variantEntrySize()))
                        {

                            uint32_t rowCI = *reinterpret_cast<uint32_t*>(variantsPtr + variantRowCIOff());
                            int32_t rowNum = *reinterpret_cast<int32_t*>(variantsPtr + variantRowNumOff());

                            if (isFirstFew)
                            {
                                QBLOG(STR("[MoriaCppMod] [TargetBuild]     RowName CI={} Num={}\n"), rowCI, rowNum);
                            }


                            for (auto& [tName, tCI] : targetFNames)
                            {
                                if (tCI == rowCI)
                                {
                                    matchedWidget = w;
                                    matchedName = name.empty() ? tName : name;
                                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (bLock RowName CI={}) on '{}' target='{}'\n"),
                                                                    rowCI,
                                                                    matchedName,
                                                                    tName);
                                    break;
                                }
                            }
                        }
                        else if (isFirstFew)
                        {
                            QBLOG(STR("[MoriaCppMod] [TargetBuild]     variantsPtr not readable (variantEntrySize bytes)\n"));
                        }
                    }
                }


                if (!matchedWidget && !name.empty() && name == m_targetBuildName)
                {
                    matchedWidget = w;
                    matchedName = name;
                    QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (exact display name) on '{}'\n"), name);
                }


                if (!matchedWidget && !name.empty())
                {
                    std::wstring nameNorm = normalizeForMatch(name);
                    std::wstring refNoBP = normalizeForMatch(m_targetBuildRecipeRef);
                    if (refNoBP.size() > 2 && refNoBP.substr(0, 2) == L"bp") refNoBP = refNoBP.substr(2);
                    std::wstring targetNorm = normalizeForMatch(m_targetBuildName);

                    if (nameNorm == refNoBP || nameNorm == targetNorm)
                    {
                        matchedWidget = w;
                        matchedName = name;
                        QBLOG(STR("[MoriaCppMod] [TargetBuild] MATCH (normalized exact) on '{}'\n"), name);
                    }
                }

                if (matchedWidget) break;
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Result: {} visible, bLockNull={} memFail={} varEmpty={} match={}\n"),
                                            visibleCount,
                                            bLockNullCount,
                                            bLockMemFailCount,
                                            variantsEmptyCount,
                                            matchedWidget ? matchedName.c_str() : L"NO");

            if (!matchedWidget)
            {
                if (visibleCount == 0) return SelectResult::Loading;
                showErrorBox(L"Recipe '" + m_targetBuildName + L"' not found in build menu");
                return SelectResult::NotFound;
            }

            auto* func = buildTab->GetFunctionByNameInChain(STR("blockSelectedEvent"));
            if (!func) return SelectResult::NotFound;

            resolveBSEOffsets(func);
            if (!bseOffsetsValid()) return SelectResult::NotFound;

            std::vector<uint8_t> params(s_bse.parmsSize, 0);


            bool gotFreshBLock = false;
            if (s_off_bLock >= 0)
            {
                // Same defense as the F-key path at startOrSwitchBuild:
                // isReadableMemory catches a stale s_off_bLock cache that
                // would otherwise AV the memcpy.
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                if (isReadableMemory(widgetBase + s_off_bLock, BLOCK_DATA_SIZE))
                {
                    std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                    gotFreshBLock = true;
                }
            }

            QBLOG(STR("[MoriaCppMod] [TargetBuild] Calling blockSelectedEvent: freshBLock={} selfRef={:p}\n"),
                                            gotFreshBLock,
                                            static_cast<void*>(matchedWidget));

            *reinterpret_cast<UObject**>(params.data() + s_bse.selfRef) = matchedWidget;
            *reinterpret_cast<int32_t*>(params.data() + s_bse.Index) = 0;


            m_isAutoSelecting = true;
            struct AutoSelectGuardTB
            {
                bool& flag;
                ~AutoSelectGuardTB() { flag = false; }
            } guardTB{m_isAutoSelecting};
            safeProcessEvent(buildTab, func, params.data());
            m_isAutoSelecting = false;

            logBLockDiagnostics(L"TARGET-BUILD", matchedName, params.data());

            showOnScreen((L"Build: " + matchedName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
            m_buildMenuWasOpen = true;
            refreshActionBar();
            onGhostAppeared();

            return SelectResult::Found;
        }


        void startOrSwitchBuild(int slot)
        {
            m_isTargetBuild = false;
            QBLOG(STR("[MoriaCppMod] [QuickBuild] ACTIVATE F{} -> '{}' (charLoaded={} frameCounter={} hasHandle={})\n"),
                                            slot + 1,
                                            m_recipeSlots[slot].displayName,
                                            m_characterLoaded,
                                            m_frameCounter,
                                            m_recipeSlots[slot].hasHandle);


            // DIRECT path: always rebuild handle from rowName.
            // Was: trusted cached recipeHandle (with raw DT pointer that ages out
            // across world transitions / GC). User reported "occasionally F#
            // does nothing" — consistent with stale DT pointer in cached handle:
            // SelectRecipe returns success but ghost doesn't spawn because the
            // game can't find the row in the freed/changed DT pointer.
            // New path: rebuild a synthetic handle every press from the still-
            // valid rowName + freshly-bound DT. Same code path target-build uses.
            if (!m_recipeSlots[slot].rowName.empty())
            {
                // 400ms cooldown gives the previous SelectRecipe RPC time to
                // replicate fully (server flip of recipeSelectMode + ghost
                // realize) before another press fires. Was 150ms — too tight
                // for the round-trip; produced "occasionally ghost doesn't
                // appear" when two presses landed in the same RPC window.
                ULONGLONG now = GetTickCount64();
                if (now - m_lastDirectSelectTime < 400)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} DIRECT path cooldown ({}ms since last)\n"),
                          slot + 1, now - m_lastDirectSelectTime);
                    return;
                }

                UObject* buildHUD = getCachedBuildHUD();
                uint8_t handle[RECIPE_HANDLE_SIZE]{};
                bool haveHandle = buildSyntheticRecipeHandle(m_recipeSlots[slot].rowName, handle);

                if (buildHUD && haveHandle)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT SelectRecipe F{} (synthetic handle, row='{}')\n"),
                          slot + 1, m_recipeSlots[slot].rowName);
                    {
                        m_isAutoSelecting = true;
                        struct AutoSelectGuardDirect { bool& f; ~AutoSelectGuardDirect() { f = false; } } guardDirect{m_isAutoSelecting};

                        if (trySelectRecipeByHandle(buildHUD, handle))
                        {
                            m_isAutoSelecting = false;
                            if (isPlacementActive())
                            {
                                m_lastDirectSelectTime = now;
                                m_lastQBSelectTime = now;
                                m_deferHideAndRefresh = true;
                                const std::wstring& targetName = m_recipeSlots[slot].displayName;
                                showOnScreen((L"Build: " + targetName).c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
                                m_buildMenuWasOpen = true;
                                m_activeBuilderSlot = slot;
                                updateBuildersBar();
                                onGhostAppeared();
                                return;
                            }
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path: SelectRecipe returned but no ghost — falling through\n"));
                        }
                        else
                        {
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path: trySelectRecipeByHandle failed — falling through\n"));
                        }
                    }
                }
                else
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path: buildHUD={:p} haveHandle={} — falling through\n"),
                          (void*)buildHUD, haveHandle ? STR("Y") : STR("N"));
                }
            }


            m_pendingQuickBuildSlot = slot;
            m_qbStartTime = GetTickCount64();

            if (isBuildTabShowing())
            {

                QBLOG(STR("[MoriaCppMod] [QuickBuild] Build tab open, selecting recipe\n"));
                m_qbPhase = PlacePhase::SelectRecipeWalk;
            }
            else if (isPlacementActive())
            {


                QBLOG(STR("[MoriaCppMod] [QuickBuild] Placement active, cancelling ghost via API\n"));
                cancelPlacementViaAPI();
                m_qbPhase = PlacePhase::CancelGhost;
            }
            else
            {


                QBLOG(STR("[MoriaCppMod] [QuickBuild] Activating build mode via API\n"));
                m_buildTabAfterShowFired = false;
                if (activateBuildMode())
                    m_qbPhase = PlacePhase::WaitingForShow;
                else
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] activateBuildMode failed\n"));
                    showErrorBox(L"Build: failed to open menu");
                    m_qbPhase = PlacePhase::Idle;
                }
            }
        }


        void startBuildFromTarget()
        {
            VLOG(STR("[MoriaCppMod] [TargetBuild] startBuildFromTarget: name='{}' recipeRef='{}'\n"),
                 m_targetBuildName, m_targetBuildRecipeRef);

            m_isTargetBuild = true;
            m_pendingQuickBuildSlot = -1;
            m_qbStartTime = GetTickCount64();

            // DIRECT path REMOVED from SHIFT+] (target-build).
            // v6.7.0 had no DIRECT path here; it always routed through the
            // state machine and was reliable. v6.20.44 added DIRECT to mirror
            // the F-key path, but reports of "ghost sometimes doesn't appear
            // on SHIFT+]" trace back to that change (see
            // plans/v670-vs-current-build-engine-diff.md). Suspected
            // mechanisms: (a) m_buildMenuWasOpen=true / m_deferHideAndRefresh
            // =true on a path where the menu was never open queues spurious
            // UI cleanup, (b) synthetic-handle layout gambling for a row that
            // was never confirmed by the game's own GetSelectedRecipeHandle.
            // F1-F8 keep their DIRECT path - user explicitly designed for
            // saved-config slot launch with no menu open. SHIFT+] always has
            // the menu close path to traverse anyway, so the state-machine
            // cost is acceptable.

            if (isBuildTabShowing())
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Build tab open, selecting recipe\n"));
                m_qbPhase = PlacePhase::SelectRecipeWalk;
            }
            else if (isPlacementActive())
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Placement active, cancelling ghost via API\n"));
                cancelPlacementViaAPI();
                m_qbPhase = PlacePhase::CancelGhost;
            }
            else
            {
                QBLOG(STR("[MoriaCppMod] [TargetBuild] Activating build mode via API\n"));
                m_buildTabAfterShowFired = false;
                if (activateBuildMode())
                    m_qbPhase = PlacePhase::WaitingForShow;
                else
                {
                    QBLOG(STR("[MoriaCppMod] [TargetBuild] activateBuildMode failed\n"));
                    showErrorBox(L"Build: failed to open menu");
                    m_qbPhase = PlacePhase::Idle;
                }
            }
        }


        void placementTick()
        {
            if (m_qbPhase == PlacePhase::Idle) return;

            ULONGLONG now = GetTickCount64();
            ULONGLONG elapsed = now - m_qbStartTime;


            if (elapsed > 5000)
            {
                QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: TIMEOUT at {}ms phase {}\n"),
                      elapsed, static_cast<int>(m_qbPhase));
                showErrorBox(Loc::get("msg.build_menu_timeout"));
                hideBuildTab();
                m_pendingQuickBuildSlot = -1;
                m_isTargetBuild = false;
                m_qbPhase = PlacePhase::Idle;
                return;
            }


            if (m_qbPhase == PlacePhase::CancelGhost)
            {
                if (!isPlacementActive())
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: ghost cancelled ({}ms), opening build menu\n"), elapsed);
                    if (isBuildTabShowing())
                        m_qbPhase = PlacePhase::SelectRecipeWalk;
                    else if (activateBuildMode())
                        m_qbPhase = PlacePhase::WaitingForShow;
                }
                return;
            }


            if (m_qbPhase == PlacePhase::WaitingForShow)
            {
                if (m_showSettleTime > 0)
                {
                    ULONGLONG settleElapsed = now - m_showSettleTime;
                    if (settleElapsed >= 500)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: animation settled ({}ms), transitioning to SelectRecipeWalk\n"), settleElapsed);
                        m_showSettleTime = 0;
                        m_qbPhase = PlacePhase::SelectRecipeWalk;
                    }
                }
                else if (isBuildTabShowing())
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: tab showing (fallback, {}ms)\n"), elapsed);
                    m_buildMenuPrimed = true;
                    m_showSettleTime = now; // start settle from now
                }
                return;
            }


            if (m_qbPhase == PlacePhase::SelectRecipeWalk)
            {
                UObject* buildTab = getCachedBuildTab();
                if (!buildTab) return;

                SelectResult result = m_isTargetBuild
                    ? selectRecipeByTargetName(buildTab)
                    : selectRecipeOnBuildTab(buildTab, m_pendingQuickBuildSlot);

                if (result == SelectResult::Found)
                {
                    s_overlay.totalRotation = 0;
                    s_overlay.needsUpdate = true;
                    m_pendingQuickBuildSlot = -1;
                    m_isTargetBuild = false;
                    m_qbPhase = PlacePhase::Idle;
                    m_lastQBSelectTime = now;
                }
                else if (result == SelectResult::NotFound)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: recipe not found ({}ms)\n"), elapsed);
                    if (!m_isTargetBuild && m_pendingQuickBuildSlot >= 0)
                    {
                        const std::wstring& targetName = m_recipeSlots[m_pendingQuickBuildSlot].displayName;
                        showErrorBox(L"Recipe '" + targetName + L"' not found in menu!");
                    }
                    m_pendingQuickBuildSlot = -1;
                    m_isTargetBuild = false;
                    m_qbPhase = PlacePhase::Idle;
                }

            }
        }
