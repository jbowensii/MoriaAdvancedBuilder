


        void probePrintString()
        {
            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:PrintString"));
            if (!fn)
            {
                VLOG(STR("[MoriaCppMod] PrintString NOT FOUND\n"));
                return;
            }
            m_ps.parmsSize = fn->GetParmsSize();
            VLOG(STR("[MoriaCppMod] PrintString ParmsSize={}\n"), m_ps.parmsSize);

            for (auto* prop : fn->ForEachProperty())
            {
                auto name = prop->GetName();
                int offset = prop->GetOffset_Internal();
                int size = prop->GetSize();
                VLOG(STR("[MoriaCppMod]   PS: {} @{} size={}\n"), name, offset, size);

                if (name == STR("WorldContextObject"))
                    m_ps.worldContext = offset;
                else if (name == STR("inString"))
                    m_ps.inString = offset;
                else if (name == STR("bPrintToScreen"))
                    m_ps.printToScreen = offset;
                else if (name == STR("bPrintToLog"))
                    m_ps.printToLog = offset;
                else if (name == STR("TextColor"))
                    m_ps.textColor = offset;
                else if (name == STR("Duration"))
                    m_ps.duration = offset;
            }

            m_ps.valid = (m_ps.worldContext >= 0 && m_ps.inString >= 0 && m_ps.printToScreen >= 0 && m_ps.duration >= 0);
            VLOG(STR("[MoriaCppMod] PrintString valid={}\n"), m_ps.valid);
        }


        void showOnScreen(const std::wstring& text, float duration = 5.0f, float r = 0.0f, float g = 1.0f, float b = 0.5f)
        {
            if (!m_ps.valid) return;

            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:PrintString"));
            auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            auto* pc = findPlayerController();
            if (!fn || !cdo || !pc)
            {
                VLOG(STR("[MoriaCppMod] showOnScreen FAILED: fn={} cdo={} pc={}\n"),
                                                (void*)fn, (void*)cdo, (void*)pc);
                return;
            }

            std::vector<uint8_t> buf(m_ps.parmsSize, 0);


            std::memcpy(buf.data() + m_ps.worldContext, &pc, 8);


            const wchar_t* textPtr = text.c_str();
            int32_t len = static_cast<int32_t>(text.size() + 1);
            uintptr_t ptrVal = reinterpret_cast<uintptr_t>(textPtr);
            std::memcpy(buf.data() + m_ps.inString, &ptrVal, 8);
            std::memcpy(buf.data() + m_ps.inString + 8, &len, 4);
            std::memcpy(buf.data() + m_ps.inString + 12, &len, 4);


            buf[m_ps.printToScreen] = 1;


            if (m_ps.printToLog >= 0) buf[m_ps.printToLog] = 0;


            if (m_ps.textColor >= 0)
            {
                float color[4] = {r, g, b, 1.0f};
                std::memcpy(buf.data() + m_ps.textColor, color, 16);
            }


            std::memcpy(buf.data() + m_ps.duration, &duration, 4);

            cdo->ProcessEvent(fn, buf.data());
            VLOG(STR("[MoriaCppMod] showOnScreen: '{}' dur={:.1f}\n"), text, duration);
        }


        void logGameState(const wchar_t* label)
        {

            static UObject* s_utilsCDO = nullptr;
            static UFunction* s_fnGetGameState = nullptr;
            static UFunction* s_fnGetManager = nullptr;
            static int s_gsWorldCtx = -1, s_gsRet = -1;
            static int s_gmWorldCtx = -1, s_gmClass = -1, s_gmRet = -1;
            static int s_gsParmsSize = 0, s_gmParmsSize = 0;
            static bool s_resolved = false;

            if (!s_resolved)
            {
                s_resolved = true;
                s_utilsCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                    STR("/Script/Moria.Default__MoriaUtils"));
                s_fnGetGameState = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                    STR("/Script/Moria.MoriaUtils:GetMoriaGameState"));
                s_fnGetManager = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                    STR("/Script/Moria.MoriaUtils:GetManager"));

                if (s_fnGetGameState)
                {
                    s_gsParmsSize = s_fnGetGameState->GetParmsSize();
                    for (auto* prop : s_fnGetGameState->ForEachProperty())
                    {
                        auto name = prop->GetName();
                        if (name == STR("WorldContextObject")) s_gsWorldCtx = prop->GetOffset_Internal();
                        else if (name == STR("ReturnValue"))   s_gsRet = prop->GetOffset_Internal();
                    }
                }

                if (s_fnGetManager)
                {
                    s_gmParmsSize = s_fnGetManager->GetParmsSize();
                    for (auto* prop : s_fnGetManager->ForEachProperty())
                    {
                        auto name = prop->GetName();
                        if (name == STR("WorldContextObject")) s_gmWorldCtx = prop->GetOffset_Internal();
                        else if (name == STR("ManagerClass"))  s_gmClass = prop->GetOffset_Internal();
                        else if (name == STR("ReturnValue"))   s_gmRet = prop->GetOffset_Internal();
                    }
                }

                VLOG(STR("[MoriaCppMod] [GS] Resolved: utilsCDO={} fnGetGS={} fnGetMgr={}\n"),
                    (void*)s_utilsCDO, (void*)s_fnGetGameState, (void*)s_fnGetManager);
                VLOG(STR("[MoriaCppMod] [GS]   GetGameState: parmsSize={} worldCtx={} ret={}\n"),
                    s_gsParmsSize, s_gsWorldCtx, s_gsRet);
                VLOG(STR("[MoriaCppMod] [GS]   GetManager: parmsSize={} worldCtx={} class={} ret={}\n"),
                    s_gmParmsSize, s_gmWorldCtx, s_gmClass, s_gmRet);
            }

            auto* pc = findPlayerController();
            if (!s_utilsCDO || !pc)
            {
                VLOG(STR("[MoriaCppMod] [GS] [{}] SKIP — utilsCDO={} pc={}\n"),
                    label, (void*)s_utilsCDO, (void*)pc);
                return;
            }


            UObject* gameState = nullptr;
            if (s_fnGetGameState && s_gsWorldCtx >= 0 && s_gsRet >= 0)
            {
                std::vector<uint8_t> buf(s_gsParmsSize, 0);
                std::memcpy(buf.data() + s_gsWorldCtx, &pc, sizeof(UObject*));
                s_utilsCDO->ProcessEvent(s_fnGetGameState, buf.data());
                gameState = *reinterpret_cast<UObject**>(buf.data() + s_gsRet);
            }

            bool worldReady = false;
            bool shadersCompiled = false;
            if (gameState)
            {
                auto* fnReady = gameState->GetFunctionByNameInChain(STR("IsWorldReady"));
                if (fnReady)
                {
                    struct { bool Ret{false}; } p{};
                    gameState->ProcessEvent(fnReady, &p);
                    worldReady = p.Ret;
                }
                auto* fnShaders = gameState->GetFunctionByNameInChain(STR("GetShadersFinishedCompiling"));
                if (fnShaders)
                {
                    struct { bool Ret{false}; } p{};
                    gameState->ProcessEvent(fnShaders, &p);
                    shadersCompiled = p.Ret;
                }
            }

            VLOG(STR("[MoriaCppMod] [GS] [{}] GameState={} WorldReady={} ShadersCompiled={}\n"),
                label, (void*)gameState, worldReady, shadersCompiled);


            if (s_fnGetManager && s_gmWorldCtx >= 0 && s_gmClass >= 0 && s_gmRet >= 0)
            {
                auto* cmClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    STR("/Script/Moria.MorConstructionManager"));
                if (cmClass)
                {
                    std::vector<uint8_t> buf(s_gmParmsSize, 0);
                    std::memcpy(buf.data() + s_gmWorldCtx, &pc, sizeof(UObject*));
                    std::memcpy(buf.data() + s_gmClass, &cmClass, sizeof(UClass*));
                    s_utilsCDO->ProcessEvent(s_fnGetManager, buf.data());
                    UObject* mgr = *reinterpret_cast<UObject**>(buf.data() + s_gmRet);
                    VLOG(STR("[MoriaCppMod] [GS] [{}] ConstructionManager via GetManager={}\n"),
                        label, (void*)mgr);
                }
            }
        }


        void applyPendingCharacterName()
        {
            std::wstring newName;
            { std::scoped_lock lock(m_charNameMutex); newName = m_pendingCharName; m_pendingCharName.clear(); }
            if (newName.empty()) return;


            std::vector<UObject*> mgrs;
            UObjectGlobals::FindAllOf(STR("CustomizationManager"), mgrs);


            UObject* pawn = getPawn();
            UObject* target = nullptr;
            for (auto* mgr : mgrs) {
                if (!mgr) continue;
                auto* outer = mgr->GetOuterPrivate();
                if (outer == pawn) { target = mgr; break; }
            }
            if (!target && !mgrs.empty()) target = mgrs[0];

            if (!target) {
                showErrorBox(L"CustomizationManager not found");
                VLOG(STR("[MoriaCppMod] applyPendingCharacterName: no CustomizationManager\n"));
                return;
            }

            auto* fn = target->GetFunctionByNameInChain(STR("SetCharacterName"));
            if (!fn) {
                showErrorBox(L"SetCharacterName not found");
                VLOG(STR("[MoriaCppMod] applyPendingCharacterName: SetCharacterName not found\n"));
                return;
            }


            int32_t parmsSize = fn->GetParmsSize();
            std::vector<uint8_t> buf(parmsSize, 0);


            int nameOffset = -1;
            for (auto* prop : fn->ForEachProperty()) {
                if (prop->GetName() == STR("NewCharacterName")) {
                    nameOffset = prop->GetOffset_Internal();
                    break;
                }
            }
            if (nameOffset < 0) {
                showErrorBox(L"SetCharacterName param not found");
                return;
            }

            const wchar_t* ptr = newName.c_str();
            int32_t len = static_cast<int32_t>(newName.size() + 1);
            uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
            std::memcpy(buf.data() + nameOffset, &ptrVal, 8);
            std::memcpy(buf.data() + nameOffset + 8, &len, 4);
            std::memcpy(buf.data() + nameOffset + 12, &len, 4);

            target->ProcessEvent(fn, buf.data());

            std::wstring msg = L"Character renamed to: " + newName;
            showOnScreen(msg, 5.0f, 0.0f, 1.0f, 0.5f);
            VLOG(STR("[MoriaCppMod] SetCharacterName('{}') called\n"), newName);
        }


        void dispatchMcSlot(int slot)
        {
            VLOG(STR("[MoriaCppMod] [MC] dispatchMcSlot({}) BEGIN\n"), slot);
            logGameState(STR("MC-pre"));
            switch (slot)
            {
            case 0:
            {
                bool modDown = isModifierDown();
                int cur = s_overlay.rotationStep;
                int next;
                if (modDown)
                    next = (cur <= 5) ? 90 : cur - 5;
                else
                    next = (cur >= 90) ? 5 : cur + 5;
                s_overlay.rotationStep = next;
                s_overlay.needsUpdate = true;
                saveConfig();
                UObject* gata = resolveGATA();
                if (gata) setGATARotation(gata, static_cast<float>(next));
                std::wstring msg = L"Rotation step: " + std::to_wstring(next) + L"\xB0";
                showOnScreen(msg, 2.0f, 0.0f, 1.0f, 0.0f);
                updateMcRotationLabel();
                break;
            }
            case 1:
                if (resolveGATA())
                    toggleSnap();
                break;
            case 2:
                runStabilityAudit();
                break;
            case 3:
                if (isModifierDown())
                    toggleFlyMode();
                else
                    toggleHideCharacter();
                break;
            case 4:
                if (isModifierDown())
                    quickBuildFromTarget();
                else if (m_tiShowTick > 0)
                    hideTargetInfo();
                else
                    dumpAimedActor();
                break;
            case 5:
                break;
            case 6:
                removeAimed();
                break;
            case 7:
                undoLast();
                break;
            case 8:
                removeAllOfType();
                break;
            default:
                break;
            }
            logGameState(STR("MC-post"));
        }
