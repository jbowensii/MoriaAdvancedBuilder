


        UObject* getCachedBuildComp()
        {
            UObject* comp = m_cachedBuildComp.Get();
            if (comp) return comp;
            std::vector<UObject*> comps;
            UObjectGlobals::FindAllOf(STR("MorBuildingComponent"), comps);
            if (!comps.empty() && comps[0])
            {
                m_cachedBuildComp = RC::Unreal::FWeakObjectPtr(comps[0]);
                QBLOG(STR("[MoriaCppMod] [QB] getCachedBuildComp: found MorBuildingComponent {:p}\n"),
                      static_cast<void*>(comps[0]));
                return comps[0];
            }
            return nullptr;
        }


        UObject* findBuildHUD()
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos)
                {
                    auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
                    if (visFunc)
                    {
                        struct
                        {
                            bool Ret{false};
                        } vp{};
                        safeProcessEvent(w, visFunc, &vp);
                        if (vp.Ret) return w;
                    }
                }
            }

            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring cls = safeClassName(w);
                if (cls.empty()) continue;
                if (cls.find(STR("BuildHUD")) != std::wstring::npos) return w;
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
            if (now - m_lastShowHideTime < 350)
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
            if (now - m_lastShowHideTime < 350)
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


        // Cancel the active placement ghost via CancelTargeting() on the GATA
        // Returns true if cancel was issued, false if no GATA found
        bool cancelPlacementViaAPI()
        {
            UObject* gata = resolveGATA();
            if (!gata) return false;
            auto* cancelFn = gata->GetFunctionByNameInChain(STR("CancelTargeting"));
            if (!cancelFn)
            {
                QBLOG(STR("[MoriaCppMod] [QB] CancelTargeting not found on GATA, falling back to keybd_event\n"));
                keybd_event(VK_ESCAPE, 0, 0, 0);
                keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                return true;
            }
            if (!safeProcessEvent(gata, cancelFn, nullptr)) { QBLOG(STR("[MoriaCppMod] [QB] CancelTargeting safeProcessEvent FAILED\n")); return false; }
            QBLOG(STR("[MoriaCppMod] [QB] CancelTargeting() called on GATA\n"));
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
            setMcSlotState(1, m_snapEnabled ? UmgSlotState::Active : UmgSlotState::Inactive);
        }


        void restoreSnap()
        {
            if (m_snapEnabled) return;
            m_snapEnabled = true;
            VLOG(STR("[MoriaCppMod] [Snap] Snap auto-restored to ON after placement (GATA will spawn fresh)\n"));
        }


        void setMcSlotState(int slot, UmgSlotState state)
        {
            if (slot < 0 || slot >= MC_SLOTS) return;
            if (!m_mcBarWidget) return;
            if (m_mcSlotStates[slot] == state) return;
            m_mcSlotStates[slot] = state;

            UObject* stateImg = m_mcStateImages[slot];
            UObject* iconImg = m_mcIconImages[slot];

            if (state == UmgSlotState::Disabled)
            {
                if (stateImg && m_umgTexInactive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexInactive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 0.3f, 0.3f, 0.3f, 0.4f);
            }
            else if (state == UmgSlotState::Active)
            {
                if (stateImg && m_umgTexActive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexActive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            else if (state == UmgSlotState::Inactive)
            {
                if (stateImg && m_umgTexInactive && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexInactive, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            else
            {
                if (stateImg && m_umgTexEmpty && m_umgSetBrushFn)
                    umgSetBrush(stateImg, m_umgTexEmpty, m_umgSetBrushFn);
                if (iconImg)
                    umgSetImageColor(iconImg, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }


        void onGhostAppeared()
        {
            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Placement] onGhostAppeared: resolveGATA returned nullptr (ghost not yet spawned)\n"));

                setMcSlotState(1, m_snapEnabled ? UmgSlotState::Active : UmgSlotState::Inactive);
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
            setMcSlotState(1, m_snapEnabled ? UmgSlotState::Active : UmgSlotState::Inactive);
        }


        // ---- Pitch/Roll Building Placement ----
        // GATA offsets from CXX dump — validated at runtime on first use
        // WARNING [C7]: Hardcoded offsets — will break if game updates GATA class layout
        static constexpr int GATA_TRACE_RESULTS     = 0x0418;  // FMorPerformTraceResults
        static constexpr int GATA_LAST_TRACE_RESULTS = 0x04F0;
        static constexpr int TRACE_TARGET_ROTATION   = 0x00AC;  // FRotator within TraceResults
        static constexpr int GATA_YAW_ACCUMULATOR    = 0x0658;  // hidden float
        bool m_gataOffsetsValidated{false};

        float m_experimentPitch{0.0f};
        float m_experimentRoll{0.0f};
        bool  m_pitchRollActive{false};

        // Read FRotator (Pitch, Yaw, Roll) from raw memory
        struct PRot { float pitch; float yaw; float roll; };

        PRot readGATARotation(UObject* gata, int traceOffset)
        {
            auto* base = reinterpret_cast<uint8_t*>(gata);
            auto* rot = reinterpret_cast<float*>(base + traceOffset + TRACE_TARGET_ROTATION);
            return { rot[0], rot[1], rot[2] };
        }

        void writeGATARotation(UObject* gata, int traceOffset, float pitch, float yaw, float roll)
        {
            auto* base = reinterpret_cast<uint8_t*>(gata);
            auto* rot = reinterpret_cast<float*>(base + traceOffset + TRACE_TARGET_ROTATION);
            rot[0] = pitch;
            rot[1] = yaw;
            rot[2] = roll;
        }

        void logPlacementRotation()
        {
            if (!s_verbose) return;
            UObject* gata = resolveGATA();
            if (!gata) return;

            auto tr = readGATARotation(gata, GATA_TRACE_RESULTS);
            auto lr = readGATARotation(gata, GATA_LAST_TRACE_RESULTS);
            auto* base = reinterpret_cast<uint8_t*>(gata);
            float yawAcc = *reinterpret_cast<float*>(base + GATA_YAW_ACCUMULATOR);

            VLOG(STR("[MoriaCppMod] [PitchRoll] TraceResults.Rot=(P:{:.1f} Y:{:.1f} R:{:.1f})  ")
                 STR("LastTrace.Rot=(P:{:.1f} Y:{:.1f} R:{:.1f})  YawAcc={:.1f}  Snap={}\n"),
                 tr.pitch, tr.yaw, tr.roll,
                 lr.pitch, lr.yaw, lr.roll,
                 yawAcc, m_snapEnabled ? STR("ON") : STR("OFF"));
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

            // Read current yaw from TraceResults
            auto tr = readGATARotation(gata, GATA_TRACE_RESULTS);

            // Write pitch/roll to both TraceResults and LastTraceResults, preserve yaw
            writeGATARotation(gata, GATA_TRACE_RESULTS, pitch, tr.yaw, roll);
            writeGATARotation(gata, GATA_LAST_TRACE_RESULTS, pitch, tr.yaw, roll);

            m_experimentPitch = pitch;
            m_experimentRoll = roll;
            m_pitchRollActive = true;

            VLOG(STR("[MoriaCppMod] [PitchRoll] INJECTED: Pitch={:.1f} Roll={:.1f} (Yaw={:.1f} preserved)\n"),
                 pitch, roll, tr.yaw);
            showOnScreen(std::format(L"Pitch={:.0f}\u00B0 Roll={:.0f}\u00B0", pitch, roll),
                         3.0f, 1.0f, 0.8f, 0.0f);
        }

        // WARNING [C8]: Hardcoded reticle offsets — will break if game updates class layout
        // CopiedComponents is Instanced UPROPERTY but not resolvable via GetPropertyByNameInChain
        // RelativeRotation is on USceneComponent base (engine-level, stable)
        static constexpr int RETICLE_COPIED_COMPONENTS = 0x0260;  // TArray<UPrimitiveComponent*>
        static constexpr int COMPONENT_RELATIVE_ROTATION = 0x0128; // FRotator on USceneComponent

        void rotateReticleVisual(UObject* gata, float pitch, float roll)
        {
            // Get reticle actor via UFUNCTION
            auto* getReticleFn = gata->GetFunctionByNameInChain(STR("GetReticleActor"));
            if (!getReticleFn) return;

            struct { UObject* ReturnValue{nullptr}; } retParams{};
            safeProcessEvent(gata, getReticleFn, &retParams);
            UObject* reticle = retParams.ReturnValue;
            if (!reticle) return;

            // Access CopiedComponents TArray at reticle+0x0260
            auto* base = reinterpret_cast<uint8_t*>(reticle);
            struct TArrayRaw { void** data; int32_t count; int32_t max; };
            auto* arr = reinterpret_cast<TArrayRaw*>(base + RETICLE_COPIED_COMPONENTS);
            if (!arr->data || arr->count <= 0) return;

            for (int i = 0; i < arr->count; i++)
            {
                auto* comp = reinterpret_cast<uint8_t*>(arr->data[i]);
                if (!comp) continue;

                // Write RelativeRotation (FRotator: Pitch, Yaw, Roll) at +0x0128
                auto* rot = reinterpret_cast<float*>(comp + COMPONENT_RELATIVE_ROTATION);
                rot[0] = pitch;  // Pitch
                // rot[1] = yaw;  // leave yaw alone — game handles it
                rot[2] = roll;   // Roll
            }
        }

        void tickPitchRoll()
        {
            if (!m_pitchRollActive) return;
            if (!m_pitchRotateEnabled && !m_rollRotateEnabled) { m_pitchRollActive = false; return; }
            UObject* gata = resolveGATA();
            if (!gata) { m_pitchRollActive = false; return; }

            // Validate hardcoded offsets on first use [C7]
            if (!m_gataOffsetsValidated)
            {
                m_gataOffsetsValidated = true;
                auto* trProp = gata->GetPropertyByNameInChain(STR("TraceResults"));
                auto* ltrProp = gata->GetPropertyByNameInChain(STR("LastTraceResults"));
                if (trProp && trProp->GetOffset_Internal() != GATA_TRACE_RESULTS)
                {
                    VLOG(STR("[MoriaCppMod] [PitchRoll] WARNING: TraceResults offset mismatch! Expected 0x{:X}, got 0x{:X} — disabling pitch/roll\n"),
                         GATA_TRACE_RESULTS, trProp->GetOffset_Internal());
                    m_pitchRollActive = false;
                    return;
                }
                if (ltrProp && ltrProp->GetOffset_Internal() != GATA_LAST_TRACE_RESULTS)
                {
                    VLOG(STR("[MoriaCppMod] [PitchRoll] WARNING: LastTraceResults offset mismatch! Expected 0x{:X}, got 0x{:X} — disabling pitch/roll\n"),
                         GATA_LAST_TRACE_RESULTS, ltrProp->GetOffset_Internal());
                    m_pitchRollActive = false;
                    return;
                }
            }

            // Re-inject every frame because GATA::Tick overwrites TraceResults
            auto tr = readGATARotation(gata, GATA_TRACE_RESULTS);
            writeGATARotation(gata, GATA_TRACE_RESULTS, m_experimentPitch, tr.yaw, m_experimentRoll);
            writeGATARotation(gata, GATA_LAST_TRACE_RESULTS, m_experimentPitch, tr.yaw, m_experimentRoll);

            // Rotate the preview ghost visually
            rotateReticleVisual(gata, m_experimentPitch, m_experimentRoll);
        }

        void clearPitchRoll()
        {
            m_pitchRollActive = false;
            m_experimentPitch = 0.0f;
            m_experimentRoll = 0.0f;
            VLOG(STR("[MoriaCppMod] [PitchRoll] Cleared\n"));
        }

        // Deep property dump — walks all reflected properties on an object
        void dumpObjectProperties(UObject* obj, const std::wstring& label, int depth = 0)
        {
            if (!s_verbose || !obj || depth > 3) return;
            auto* cls = obj->GetClassPrivate();
            if (!cls) return;

            std::wstring indent(depth * 2, L' ');
            VLOG(STR("[MoriaCppMod] [Dump] {}{} (class={})\n"),
                 indent, label, cls->GetName());

            for (auto* strct = static_cast<UStruct*>(cls); strct; strct = strct->GetSuperStruct())
            {
                for (auto* prop : strct->ForEachProperty())
                {
                    auto name = prop->GetName();
                    int offset = prop->GetOffset_Internal();
                    int size = prop->GetSize();
                    auto* base = reinterpret_cast<uint8_t*>(obj);

                    // Determine property type
                    std::wstring typeName = L"unknown";
                    std::wstring valueStr = L"";

                    auto className = prop->GetClass().GetName();
                    typeName = className;

                    // Read common types
                    if (className.find(STR("FloatProperty")) != std::wstring::npos && size == 4)
                    {
                        float val = *reinterpret_cast<float*>(base + offset);
                        valueStr = std::format(L" = {:.4f}", val);
                    }
                    else if (className.find(STR("DoubleProperty")) != std::wstring::npos && size == 8)
                    {
                        double val = *reinterpret_cast<double*>(base + offset);
                        valueStr = std::format(L" = {:.4f}", val);
                    }
                    else if (className.find(STR("IntProperty")) != std::wstring::npos && size == 4)
                    {
                        int32_t val = *reinterpret_cast<int32_t*>(base + offset);
                        valueStr = std::format(L" = {}", val);
                    }
                    else if (className.find(STR("BoolProperty")) != std::wstring::npos)
                    {
                        // BoolProperty uses bit fields, try simple read
                        bool val = *reinterpret_cast<uint8_t*>(base + offset) != 0;
                        valueStr = val ? L" = true" : L" = false";
                    }
                    else if (className.find(STR("ByteProperty")) != std::wstring::npos && size == 1)
                    {
                        uint8_t val = *reinterpret_cast<uint8_t*>(base + offset);
                        valueStr = std::format(L" = {}", val);
                    }
                    else if (className.find(STR("EnumProperty")) != std::wstring::npos)
                    {
                        uint8_t val = *reinterpret_cast<uint8_t*>(base + offset);
                        valueStr = std::format(L" = (enum {})", val);
                    }
                    else if (className.find(STR("StructProperty")) != std::wstring::npos)
                    {
                        // For FVector (12 bytes) and FRotator (12 bytes)
                        if (size == 12)
                        {
                            float* v = reinterpret_cast<float*>(base + offset);
                            valueStr = std::format(L" = ({:.2f}, {:.2f}, {:.2f})", v[0], v[1], v[2]);
                        }
                        // FTransform (48+ bytes) — dump Rotation(quat) + Translation + Scale
                        else if (size >= 48)
                        {
                            float* v = reinterpret_cast<float*>(base + offset);
                            valueStr = std::format(L" = [Quat({:.3f},{:.3f},{:.3f},{:.3f}) Pos({:.1f},{:.1f},{:.1f}) Scale({:.2f},{:.2f},{:.2f})]",
                                v[0], v[1], v[2], v[3],   // rotation quat
                                v[4], v[5], v[6],          // translation
                                v[8], v[9], v[10]);        // scale (v[7] is padding)
                        }
                        else
                        {
                            valueStr = std::format(L" (struct, {} bytes)", size);
                        }
                    }
                    else if (className.find(STR("NameProperty")) != std::wstring::npos)
                    {
                        // FName — try to read safely
                        try {
                            auto* fn = reinterpret_cast<FName*>(base + offset);
                            auto nameStr = fn->ToString();
                            valueStr = L" = \"" + nameStr + L"\"";
                        } catch (...) {
                            valueStr = L" = <FName read failed>";
                        }
                    }
                    else if (className.find(STR("ObjectProperty")) != std::wstring::npos ||
                             className.find(STR("WeakObjectProperty")) != std::wstring::npos)
                    {
                        void* ptr = *reinterpret_cast<void**>(base + offset);
                        if (ptr)
                            valueStr = std::format(L" = {:p}", ptr);
                        else
                            valueStr = L" = nullptr";
                    }
                    else if (className.find(STR("ArrayProperty")) != std::wstring::npos)
                    {
                        int32_t count = *reinterpret_cast<int32_t*>(base + offset + sizeof(void*));
                        valueStr = std::format(L" (array, count={})", count);
                    }

                    VLOG(STR("[MoriaCppMod] [Dump] {}  +0x{:04X} [{}] {} ({} bytes){}\n"),
                         indent, offset, typeName, name, size, valueStr);
                }
            }
        }

        void dumpGATADeep()
        {
            if (!s_verbose) return;
            UObject* gata = resolveGATA();
            if (!gata)
            {
                VLOG(STR("[MoriaCppMod] [Dump] No GATA active\n"));
                return;
            }

            VLOG(STR("[MoriaCppMod] [Dump] ========== GATA DEEP PROPERTY DUMP ==========\n"));
            dumpObjectProperties(gata, L"GATA");

            // Also dump the reticle
            auto* getReticleFn = gata->GetFunctionByNameInChain(STR("GetReticleActor"));
            if (getReticleFn)
            {
                struct { UObject* ReturnValue{nullptr}; } rp{};
                safeProcessEvent(gata, getReticleFn, &rp);
                if (rp.ReturnValue)
                    dumpObjectProperties(rp.ReturnValue, L"Reticle", 1);
            }

            // Dump GetBuildTargetTransform result
            auto* buildComp = getCachedBuildComp();
            if (buildComp)
            {
                auto* transformFn = buildComp->GetFunctionByNameInChain(STR("GetBuildTargetTransform"));
                if (transformFn)
                {
                    // FTransform is 48 bytes (Quat4 + Vec3 + pad + Vec3)
                    uint8_t buf[64]{};
                    if (!safeProcessEvent(buildComp, transformFn, buf)) return;
                    float* v = reinterpret_cast<float*>(buf);
                    VLOG(STR("[MoriaCppMod] [Dump] GetBuildTargetTransform:\n"));
                    VLOG(STR("[MoriaCppMod] [Dump]   Rotation(Quat): X={:.4f} Y={:.4f} Z={:.4f} W={:.4f}\n"),
                         v[0], v[1], v[2], v[3]);
                    VLOG(STR("[MoriaCppMod] [Dump]   Translation:    X={:.1f} Y={:.1f} Z={:.1f}\n"),
                         v[4], v[5], v[6]);
                    VLOG(STR("[MoriaCppMod] [Dump]   Scale:          X={:.2f} Y={:.2f} Z={:.2f}\n"),
                         v[8], v[9], v[10]);
                }
            }

            VLOG(STR("[MoriaCppMod] [Dump] ========== END GATA DUMP ==========\n"));
        }

        // Hook: intercept BuildNewConstruction to inject pitch/roll into the FTransform
        void onBuildNewConstruction(UObject* context, UFunction* func, void* parms)
        {
            if (!m_pitchRollActive) return;
            if (!parms) return;

            // BuildNewConstruction params layout (from UHT):
            // FMorConstructionRecipeRowHandle RecipeHandle
            // AMorCharacter* Player
            // FTransform Transform       <-- we want to modify this
            // EBuildProcess ChosenProcess
            // bool bBuildAsFoundation
            // AActor* ConnectedTo
            // float StabilityEstimate

            // Walk the function's properties to find Transform offset
            for (auto* prop : func->ForEachProperty())
            {
                auto name = prop->GetName();
                if (name == STR("Transform"))
                {
                    int offset = prop->GetOffset_Internal();
                    auto* base = reinterpret_cast<uint8_t*>(parms);
                    float* quat = reinterpret_cast<float*>(base + offset);

                    VLOG(STR("[MoriaCppMod] [PitchRoll] BuildNewConstruction INTERCEPTED!\n"));
                    VLOG(STR("[MoriaCppMod] [PitchRoll]   BEFORE: Quat({:.4f}, {:.4f}, {:.4f}, {:.4f})\n"),
                         quat[0], quat[1], quat[2], quat[3]);

                    // Convert our pitch/roll/yaw (degrees) to quaternion
                    // FTransform stores rotation as FQuat (X, Y, Z, W)
                    // We need to construct a quaternion from Euler angles
                    constexpr float DEG2RAD = 3.14159265f / 180.0f;
                    float p = m_experimentPitch * DEG2RAD * 0.5f;
                    float y = 0.0f; // keep existing yaw from the quaternion
                    float r = m_experimentRoll * DEG2RAD * 0.5f;

                    // Extract existing yaw from current quaternion
                    // Current quat is yaw-only: (0, 0, sin(yaw/2), cos(yaw/2))
                    float existingYawSin = quat[2];
                    float existingYawCos = quat[3];

                    // Build pitch quaternion: (sin(p/2), 0, 0, cos(p/2)) around Y axis for UE4
                    // UE4 uses: Pitch=Y, Yaw=Z, Roll=X
                    float sp = sinf(p), cp = cosf(p);
                    float sr = sinf(r), cr = cosf(r);

                    // Compose: Qyaw * Qpitch * Qroll
                    // Qyaw   = (0, 0, existingYawSin, existingYawCos)
                    // Qpitch = (0, sp, 0, cp)  — rotation around Y
                    // Qroll  = (sr, 0, 0, cr)  — rotation around X

                    // First: Qpitch * Qroll
                    float prX = cp * sr;
                    float prY = sp * cr;
                    float prZ = -sp * sr;
                    float prW = cp * cr;

                    // Then: Qyaw * (Qpitch * Qroll)
                    float ys = existingYawSin;
                    float yc = existingYawCos;
                    float finalX = yc * prX + ys * prZ;  // actually: - ys * prY
                    float finalY = yc * prY - ys * prZ;
                    float finalZ = yc * prZ + ys * prW;
                    float finalW = yc * prW - ys * prZ;

                    // Simplified: use direct Euler-to-quat for UE4 convention
                    // UE4 FRotator order: Pitch(Y), Yaw(Z), Roll(X)
                    // Reconstruct yaw from existing quat
                    float existingYaw = atan2f(2.0f * existingYawSin * existingYawCos,
                                               1.0f - 2.0f * existingYawSin * existingYawSin);
                    float hy = existingYaw * 0.5f;
                    float hp = m_experimentPitch * DEG2RAD * 0.5f;
                    float hr = m_experimentRoll * DEG2RAD * 0.5f;

                    float cy2 = cosf(hy), sy2 = sinf(hy);
                    float cp2 = cosf(hp), sp2 = sinf(hp);
                    float cr2 = cosf(hr), sr2 = sinf(hr);

                    // UE4 quaternion from Euler (ZYX convention):
                    quat[0] = cr2 * sp2 * sy2 - sr2 * cp2 * cy2;  // X
                    quat[1] = -cr2 * sp2 * cy2 - sr2 * cp2 * sy2; // Y
                    quat[2] = cr2 * cp2 * sy2 - sr2 * sp2 * cy2;  // Z
                    quat[3] = cr2 * cp2 * cy2 + sr2 * sp2 * sy2;  // W

                    VLOG(STR("[MoriaCppMod] [PitchRoll]   AFTER:  Quat({:.4f}, {:.4f}, {:.4f}, {:.4f})\n"),
                         quat[0], quat[1], quat[2], quat[3]);
                    VLOG(STR("[MoriaCppMod] [PitchRoll]   Applied Pitch={:.1f} Roll={:.1f}\n"),
                         m_experimentPitch, m_experimentRoll);

                    break;
                }
            }
        }
        // ---- End Pitch/Roll Experiment ----


        void onGhostDisappeared()
        {
            clearPitchRoll();

            if (m_qbPhase != PlacePhase::Idle)
                return;

            ULONGLONG now = GetTickCount64();
            if (now - m_lastQBSelectTime < 500)
                return;
            VLOG(STR("[MoriaCppMod] [Placement] onGhostDisappeared: greying out snap slot\n"));
            setMcSlotState(1, UmgSlotState::Disabled);
        }


        SelectResult selectRecipeOnBuildTab(UObject* buildTab, int slot)
        {
            const std::wstring& targetName = m_recipeSlots[slot].displayName;
            const std::wstring& slotTexture = m_recipeSlots[slot].textureName;

            QBLOG(STR("[MoriaCppMod] [QuickBuild] SELECT: '{}' tex='{}' (F{}) hasHandle={}\n"),
                  targetName, slotTexture, slot + 1, m_recipeSlots[slot].hasHandle);

            UObject* buildHUD = getCachedBuildHUD();


            // FindAllOf the specific build item class — much smaller set than all UserWidgets
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UI_WBP_Build_Item_Medium_C"), widgets);

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
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
                QBLOG(STR("[MoriaCppMod] [QuickBuild]   using FRESH bLock from widget (@0x{:X})\n"), s_off_bLock);
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
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);

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
                uint8_t* widgetBase = reinterpret_cast<uint8_t*>(matchedWidget);
                std::memcpy(params.data() + s_bse.bLock, widgetBase + s_off_bLock, BLOCK_DATA_SIZE);
                gotFreshBLock = true;
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


            if (m_recipeSlots[slot].hasHandle)
            {
                ULONGLONG now = GetTickCount64();
                if (now - m_lastDirectSelectTime < 150)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] F{} DIRECT path cooldown ({}ms since last)\n"),
                          slot + 1, now - m_lastDirectSelectTime);
                    return;
                }

                UObject* buildHUD = nullptr;
                UObject* comp = getCachedBuildComp();
                if (comp)
                {
                    auto* fn = comp->GetFunctionByNameInChain(STR("GetActiveBuildingWidget"));
                    if (fn)
                    {
                        struct { UObject* Ret{nullptr}; } params{};
                        safeProcessEvent(comp, fn, &params);
                        if (params.Ret && isObjectAlive(params.Ret))
                            buildHUD = params.Ret;
                    }
                }
                if (buildHUD)
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT SelectRecipe for F{} (skipping state machine)\n"), slot + 1);
                    {
                        // RAII guard — ensures m_isAutoSelecting is always cleared
                        m_isAutoSelecting = true;
                        struct AutoSelectGuardDirect { bool& f; ~AutoSelectGuardDirect() { f = false; } } guardDirect{m_isAutoSelecting};

                        if (trySelectRecipeByHandle(buildHUD, m_recipeSlots[slot].recipeHandle))
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
                            QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path: SelectRecipe succeeded but no ghost — stale handle, invalidating\n"));
                        }
                    } // guardDirect clears m_isAutoSelecting
                    m_recipeSlots[slot].hasHandle = false;
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path failed, falling through to state machine\n"));
                }
                else
                {
                    QBLOG(STR("[MoriaCppMod] [QuickBuild] DIRECT path: no active overlay, need build menu\n"));
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
                    // OnAfterShow fired — wait 350ms for animations to settle
                    ULONGLONG settleElapsed = now - m_showSettleTime;
                    if (settleElapsed >= 350)
                    {
                        QBLOG(STR("[MoriaCppMod] [QuickBuild] SM: animation settled ({}ms), transitioning to SelectRecipeWalk\n"), settleElapsed);
                        m_showSettleTime = 0;
                        m_qbPhase = PlacePhase::SelectRecipeWalk;
                    }
                }
                else if (isBuildTabShowing())
                {
                    // Fallback — OnAfterShow didn't fire but tab is visible
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
                    updateMcRotationLabel();
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
