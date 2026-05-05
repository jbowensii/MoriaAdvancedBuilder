


        static constexpr uint8_t STAB_UNINITIALIZED = 0;
        static constexpr uint8_t STAB_INITIALIZING  = 1;
        static constexpr uint8_t STAB_UNSTABLE      = 3;
        static constexpr uint8_t STAB_PROVISIONAL   = 4;
        static constexpr uint8_t STAB_DECONSTRUCTED = 5;

        static constexpr float THRESHOLD_CRITICAL = 20.0f;
        static constexpr float THRESHOLD_MARGINAL = 50.0f;


        static constexpr float AUDIT_LIGHT_CRITICAL_INTENSITY = 25000.0f;
        static constexpr float AUDIT_LIGHT_MARGINAL_INTENSITY = 15000.0f;
        static constexpr float AUDIT_LIGHT_RADIUS              = 10.0f;


        void spawnVfxAtLocation(UObject* worldContext, UObject* niagaraSystem,
                                float x, float y, float z)
        {
            static UFunction* s_fn = nullptr;
            static UObject* s_cdo = nullptr;
            if (!s_fn)
            {
                s_fn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/Niagara.NiagaraFunctionLibrary:SpawnSystemAtLocation"));
                s_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/Niagara.Default__NiagaraFunctionLibrary"));
            }
            if (!s_fn || !s_cdo) return;

            static int s_parmsSize = -1;
            static int s_offWCO = -1, s_offSystem = -1, s_offLocation = -1;
            static int s_offRotation = -1, s_offScale = -1;
            static int s_offAutoDestroy = -1, s_offAutoActivate = -1;
            static int s_offPooling = -1, s_offPreCull = -1, s_offRet = -1;
            if (s_parmsSize < 0)
            {
                s_parmsSize = s_fn->GetPropertiesSize();
                for (auto* prop : s_fn->ForEachProperty())
                {
                    if (!prop) continue;
                    std::wstring name(prop->GetName());
                    int off = prop->GetOffset_Internal();
                    if (name == L"WorldContextObject") s_offWCO = off;
                    else if (name == L"SystemTemplate") s_offSystem = off;
                    else if (name == L"Location") s_offLocation = off;
                    else if (name == L"Rotation") s_offRotation = off;
                    else if (name == L"Scale") s_offScale = off;
                    else if (name == L"bAutoDestroy") s_offAutoDestroy = off;
                    else if (name == L"bAutoActivate") s_offAutoActivate = off;
                    else if (name == L"PoolingMethod") s_offPooling = off;
                    else if (name == L"bPreCullCheck") s_offPreCull = off;
                    else if (name == L"ReturnValue") s_offRet = off;
                }
            }
            if (s_offSystem < 0 || s_offLocation < 0) return;

            std::vector<uint8_t> buf(s_parmsSize, 0);
            if (s_offWCO >= 0) std::memcpy(buf.data() + s_offWCO, &worldContext, 8);
            std::memcpy(buf.data() + s_offSystem, &niagaraSystem, 8);
            float loc[3] = {x, y, z};
            std::memcpy(buf.data() + s_offLocation, loc, 12);
            if (s_offScale >= 0)
            {
                float scale[3] = {1.0f, 1.0f, 1.0f};
                std::memcpy(buf.data() + s_offScale, scale, 12);
            }
            if (s_offAutoDestroy >= 0) buf[s_offAutoDestroy] = 1;
            if (s_offAutoActivate >= 0) buf[s_offAutoActivate] = 1;
            safeProcessEvent(s_cdo, s_fn, buf.data());
        }


        void spawnPointLightAtLocation(UObject* worldContext,
                                       float x, float y, float z, bool critical)
        {

            static UFunction* s_spawnFn = nullptr;
            static UObject* s_gsCDO = nullptr;
            static UClass* s_lightClass = nullptr;
            static UFunction* s_finishFn = nullptr;
            static bool s_resolved = false;
            static int s_spawnSize = -1;
            static int s_sWCO = -1, s_sClass = -1, s_sXform = -1, s_sRet = -1;
            static int s_finSize = -1, s_fActor = -1, s_fXform = -1, s_fRet = -1;

            if (!s_resolved)
            {
                s_resolved = true;
                s_spawnFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
                s_gsCDO = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
                s_lightClass = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, STR("/Script/Engine.PointLight"));
                s_finishFn = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));

                if (!s_spawnFn || !s_gsCDO)
                {
                    VLOG(STR("[MoriaCppMod] [STAB] BeginDeferredActorSpawnFromClass NOT FOUND\n"));
                    return;
                }
                if (!s_lightClass)
                {
                    VLOG(STR("[MoriaCppMod] [STAB] PointLight class NOT FOUND\n"));
                    return;
                }

                s_spawnSize = s_spawnFn->GetPropertiesSize();
                for (auto* prop : s_spawnFn->ForEachProperty())
                {
                    if (!prop) continue;
                    std::wstring name(prop->GetName());
                    int off = prop->GetOffset_Internal();
                    if (name == L"WorldContextObject") s_sWCO = off;
                    else if (name == L"ActorClass") s_sClass = off;
                    else if (name == L"SpawnTransform") s_sXform = off;
                    else if (name == L"ReturnValue") s_sRet = off;
                }
                if (s_finishFn)
                {
                    s_finSize = s_finishFn->GetPropertiesSize();
                    for (auto* prop : s_finishFn->ForEachProperty())
                    {
                        if (!prop) continue;
                        std::wstring name(prop->GetName());
                        int off = prop->GetOffset_Internal();
                        if (name == L"Actor") s_fActor = off;
                        else if (name == L"SpawnTransform") s_fXform = off;
                        else if (name == L"ReturnValue") s_fRet = off;
                    }
                }
                VLOG(STR("[MoriaCppMod] [STAB] SpawnFn: size={} wco={} cls={} xform={} ret={}\n"),
                     s_spawnSize, s_sWCO, s_sClass, s_sXform, s_sRet);
            }
            if (!s_spawnFn || !s_gsCDO || !s_lightClass) return;
            if (s_sClass < 0 || s_sXform < 0) return;


            FTransformRaw xform{};
            xform.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            xform.Translation = {x, y, z};
            xform.Scale3D = {1.0f, 1.0f, 1.0f};

            std::vector<uint8_t> buf(s_spawnSize, 0);
            if (s_sWCO >= 0) std::memcpy(buf.data() + s_sWCO, &worldContext, 8);
            std::memcpy(buf.data() + s_sClass, &s_lightClass, 8);
            std::memcpy(buf.data() + s_sXform, &xform, 48);
            safeProcessEvent(s_gsCDO, s_spawnFn, buf.data());

            UObject* spawned = nullptr;
            if (s_sRet >= 0)
                spawned = *reinterpret_cast<UObject**>(buf.data() + s_sRet);

            if (!spawned)
            {
                VLOG(STR("[MoriaCppMod] [STAB] PointLight spawn returned null\n"));
                return;
            }

            if (s_finishFn && s_fActor >= 0)
            {
                std::vector<uint8_t> finBuf(s_finSize, 0);
                std::memcpy(finBuf.data() + s_fActor, &spawned, 8);
                if (s_fXform >= 0)
                    std::memcpy(finBuf.data() + s_fXform, &xform, 48);
                safeProcessEvent(s_gsCDO, s_finishFn, finBuf.data());
            }


            {
                static UFunction* s_setIntFn = nullptr;
                static UFunction* s_setColorFn = nullptr;
                static UFunction* s_setRadFn = nullptr;
                static bool s_lightResolved = false;
                static int s_intSize = -1, s_intOff = -1;
                static int s_colSize = -1, s_colOff = -1;
                static int s_radSize = -1, s_radOff = -1;

                auto* getRootFn = spawned->GetFunctionByNameInChain(STR("K2_GetRootComponent"));
                UObject* lightComp = nullptr;
                if (getRootFn)
                {
                    struct { UObject* Ret{nullptr}; } rp{};
                    safeProcessEvent(spawned, getRootFn, &rp);
                    lightComp = rp.Ret;
                }
                if (!lightComp) lightComp = spawned;

                if (!s_lightResolved)
                {
                    s_lightResolved = true;
                    s_setIntFn = lightComp->GetFunctionByNameInChain(STR("SetIntensity"));
                    s_setColorFn = lightComp->GetFunctionByNameInChain(STR("SetLightColor"));
                    s_setRadFn = lightComp->GetFunctionByNameInChain(STR("SetAttenuationRadius"));

                    if (s_setIntFn)
                    {
                        s_intSize = s_setIntFn->GetPropertiesSize();
                        for (auto* p : s_setIntFn->ForEachProperty())
                        {
                            if (!p) continue;
                            std::wstring n(p->GetName());
                            if (n == L"NewIntensity" || n == L"NewLightBrightness")
                                s_intOff = p->GetOffset_Internal();
                        }
                    }
                    if (s_setColorFn)
                    {
                        s_colSize = s_setColorFn->GetPropertiesSize();
                        for (auto* p : s_setColorFn->ForEachProperty())
                        {
                            if (!p) continue;
                            std::wstring n(p->GetName());
                            if (n == L"NewLightColor")
                                s_colOff = p->GetOffset_Internal();
                        }
                    }
                    if (s_setRadFn)
                    {
                        s_radSize = s_setRadFn->GetPropertiesSize();
                        for (auto* p : s_setRadFn->ForEachProperty())
                        {
                            if (!p) continue;
                            std::wstring n(p->GetName());
                            if (n == L"NewRadius")
                                s_radOff = p->GetOffset_Internal();
                        }
                    }
                    VLOG(STR("[MoriaCppMod] [STAB] Light funcs: int={} col={} rad={}\n"),
                         s_setIntFn ? 1 : 0, s_setColorFn ? 1 : 0, s_setRadFn ? 1 : 0);
                }

                if (s_setIntFn && s_intOff >= 0)
                {
                    std::vector<uint8_t> b(s_intSize, 0);
                    float intensity = critical ? AUDIT_LIGHT_CRITICAL_INTENSITY : AUDIT_LIGHT_MARGINAL_INTENSITY;
                    std::memcpy(b.data() + s_intOff, &intensity, 4);
                    safeProcessEvent(lightComp, s_setIntFn, b.data());
                }


                if (s_setColorFn && s_colOff >= 0)
                {
                    std::vector<uint8_t> b(s_colSize, 0);
                    float color[4];
                    if (critical)
                        { color[0] = 1.0f; color[1] = 0.0f; color[2] = 0.0f; color[3] = 1.0f; }
                    else
                        { color[0] = 1.0f; color[1] = 0.8f; color[2] = 0.0f; color[3] = 1.0f; }
                    std::memcpy(b.data() + s_colOff, color, 16);
                    safeProcessEvent(lightComp, s_setColorFn, b.data());
                }


                if (s_setRadFn && s_radOff >= 0)
                {
                    std::vector<uint8_t> b(s_radSize, 0);
                    float radius = AUDIT_LIGHT_RADIUS;
                    std::memcpy(b.data() + s_radOff, &radius, 4);
                    safeProcessEvent(lightComp, s_setRadFn, b.data());
                }
            }

            m_auditSpawnedActors.push_back(RC::Unreal::FWeakObjectPtr(spawned));
            VLOG(STR("[MoriaCppMod] [STAB] PointLight spawned at ({:.0f},{:.0f},{:.0f}) {}\n"),
                 x, y, z, critical ? STR("CRITICAL/red") : STR("MARGINAL/yellow"));
        }


        void destroyAuditActors()
        {
            for (auto& weakActor : m_auditSpawnedActors)
            {
                UObject* actor = weakActor.Get();
                if (!actor) continue;
                auto* destroyFn = actor->GetFunctionByNameInChain(STR("K2_DestroyActor"));
                if (destroyFn)
                    safeProcessEvent(actor, destroyFn, nullptr);
            }
            if (!m_auditSpawnedActors.empty())
                VLOG(STR("[MoriaCppMod] [STAB] Destroyed {} spawned actor(s)\n"),
                     m_auditSpawnedActors.size());
            m_auditSpawnedActors.clear();
        }


        void clearStabilityHighlights()
        {
            destroyAuditActors();
            if (!m_auditLocations.empty())
                VLOG(STR("[MoriaCppMod] [STAB] Audit cleared ({} locations)\n"),
                     m_auditLocations.size());
            m_auditLocations.clear();
            m_auditClearTime = 0;
        }


        void runStabilityAudit()
        {
            clearStabilityHighlights();

            VLOG(STR("[MoriaCppMod] [STAB] === Stability Audit ===\n"));


            UObject* mgr = nullptr;
            {
                std::vector<UObject*> mgrs;
                findAllOfSafe(STR("MorConstructionManager"), mgrs);
                for (auto* m : mgrs)
                {
                    if (!m) continue;
                    std::wstring name(m->GetName());
                    if (name.find(L"Default__") == std::wstring::npos)
                    {
                        mgr = m;
                        break;
                    }
                }
            }
            if (!mgr)
            {
                VLOG(STR("[MoriaCppMod] [STAB] ERROR: no construction manager found\n"));
                return;
            }


            void* vfxPtr = mgr->GetValuePtrByPropertyNameInChain(STR("StabilityLossVFX"));
            UObject* stabilityVfx = vfxPtr ? *static_cast<UObject**>(vfxPtr) : nullptr;


            void* arrPtr = mgr->GetValuePtrByPropertyNameInChain(STR("AllStabilityComponents"));
            if (!arrPtr || !isReadableMemory(arrPtr, 16)) return;

            UObject** arrData = *reinterpret_cast<UObject***>(arrPtr);
            int32_t arrNum = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(arrPtr) + 8);
            VLOG(STR("[MoriaCppMod] [STAB] Scanning {} stability components\n"), arrNum);
            if (arrNum <= 0 || !arrData) return;

            auto* pc = findPlayerController();
            FVec3f playerLoc = getPawnLocation();

            int countStable = 0, countMarginal = 0, countCritical = 0;

            struct ProblemInfo { UObject* actor; float stability; uint8_t state; float x, y, z; };
            std::vector<ProblemInfo> problems;


            for (int32_t i = 0; i < arrNum; i++)
            {
                UObject* comp = arrData[i];
                if (!comp) continue;

                void* statePtr = comp->GetValuePtrByPropertyNameInChain(STR("State"));
                void* stabPtr = comp->GetValuePtrByPropertyNameInChain(STR("Stability"));
                if (!stabPtr) continue;

                uint8_t state = statePtr ? *static_cast<uint8_t*>(statePtr) : 0;
                float stability = *static_cast<float*>(stabPtr);

                if (state == STAB_DECONSTRUCTED || state == STAB_UNINITIALIZED || state == STAB_INITIALIZING)
                    continue;

                bool isProblem = false;
                if (state == STAB_UNSTABLE || stability <= THRESHOLD_CRITICAL)
                    { countCritical++; isProblem = true; }
                else if (state == STAB_PROVISIONAL || stability <= THRESHOLD_MARGINAL)
                    { countMarginal++; isProblem = true; }
                else
                    countStable++;

                if (isProblem)
                {
                    auto* ownerFunc = comp->GetFunctionByNameInChain(STR("GetOwner"));
                    if (!ownerFunc) continue;
                    struct { UObject* Ret{nullptr}; } op{};
                    safeProcessEvent(comp, ownerFunc, &op);
                    if (!op.Ret) continue;

                    FVec3f loc{0, 0, 0};
                    auto* locFunc = op.Ret->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
                    if (locFunc) safeProcessEvent(op.Ret, locFunc, &loc);

                    problems.push_back({op.Ret, stability, state, loc.X, loc.Y, loc.Z});
                }
            }

            int totalChecked = countStable + countMarginal + countCritical;
            VLOG(STR("[MoriaCppMod] [STAB] {} checked ({} stable, {} marginal, {} critical)\n"),
                 totalChecked, countStable, countMarginal, countCritical);

            if (problems.empty())
            {
                VLOG(STR("[MoriaCppMod] [STAB] No problems found\n"));
                return;
            }


            for (auto& p : problems)
            {
                float dx = p.x - playerLoc.X, dy = p.y - playerLoc.Y, dz = p.z - playerLoc.Z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz) / 100.0f;
                bool isCritical = (p.state == STAB_UNSTABLE || p.stability <= THRESHOLD_CRITICAL);
                const wchar_t* label = isCritical ? L"CRITICAL" : L"MARGINAL";
                VLOG(STR("[MoriaCppMod] [STAB]   {} stab={:.1f} dist={:.0f}m at ({:.0f},{:.0f},{:.0f})\n"),
                     label, p.stability, dist, p.x, p.y, p.z);

                if (stabilityVfx && pc)
                    spawnVfxAtLocation(pc, stabilityVfx, p.x, p.y, p.z);

                m_auditLocations.push_back({p.x, p.y, p.z, isCritical});

                if (pc)
                    spawnPointLightAtLocation(pc, p.x, p.y, p.z, isCritical);
            }

            VLOG(STR("[MoriaCppMod] [STAB] {} PointLight(s) + VFX spawned\n"), problems.size());


            m_auditClearTime = GetTickCount64() + 10000;
        }
