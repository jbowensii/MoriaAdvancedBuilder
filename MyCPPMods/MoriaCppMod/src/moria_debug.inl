// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_debug.inl — Display, debug, and cheat command methods               ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // â"€â"€ PrintString support â"€â"€

        // â"€â"€ 6C: Display & UI Helpers â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
        // PrintString, on-screen text, chat widget, system messages

        // Discovers KismetSystemLibrary::PrintString param offsets at runtime.
        // Uses ForEachProperty() to locate params by name â€" safe across game updates.
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

        // Displays text on screen via KismetSystemLibrary::PrintString.
        // Requires probePrintString() to have discovered param offsets first.
        // Color is RGB (0.0-1.0), duration in seconds.
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

            // WorldContextObject
            std::memcpy(buf.data() + m_ps.worldContext, &pc, 8);

            // FString in param buffer: Data ptr (8) + ArrayNum (4) + ArrayMax (4)
            const wchar_t* textPtr = text.c_str();
            int32_t len = static_cast<int32_t>(text.size() + 1);
            uintptr_t ptrVal = reinterpret_cast<uintptr_t>(textPtr);
            std::memcpy(buf.data() + m_ps.inString, &ptrVal, 8);
            std::memcpy(buf.data() + m_ps.inString + 8, &len, 4);
            std::memcpy(buf.data() + m_ps.inString + 12, &len, 4);

            // bPrintToScreen = true
            buf[m_ps.printToScreen] = 1;

            // bPrintToLog = false (don't spam log)
            if (m_ps.printToLog >= 0) buf[m_ps.printToLog] = 0;

            // TextColor (FLinearColor: R, G, B, A)
            if (m_ps.textColor >= 0)
            {
                float color[4] = {r, g, b, 1.0f};
                std::memcpy(buf.data() + m_ps.textColor, color, 16);
            }

            // Duration
            std::memcpy(buf.data() + m_ps.duration, &duration, 4);

            cdo->ProcessEvent(fn, buf.data());
            VLOG(STR("[MoriaCppMod] showOnScreen: '{}' dur={:.1f}\n"), text, duration);
        }

        // â"€â"€ Chat/Widget display â"€â"€

        /* BELIEVED DEAD CODE -- chat widget system superseded by showOnScreen()
        void findWidgets()
        {
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
            m_chatWidget = nullptr;
            m_sysMessages = nullptr;
            for (auto* w : widgets)
            {
                if (!w) continue;
                std::wstring clsName = safeClassName(w);
                if (clsName.empty()) continue;
                if (clsName == STR("WBP_UI_ChatWidget_C") && !m_chatWidget) m_chatWidget = w;
                if (clsName == STR("WBP_UI_Console_SystemMessages_C") && !m_sysMessages) m_sysMessages = w;
            }
        }

        void showGameMessage(const std::wstring& text)
        {
            if (!m_chatWidget) findWidgets();
            if (!m_chatWidget) return;

            // Guard against stale widget pointer (GC slab reuse)
            if (!isWidgetAlive(m_chatWidget))
            {
                m_chatWidget = nullptr;
                findWidgets();
                if (!m_chatWidget) return;
            }

            // AddToShortChat(FText) â€" the floating chat overlay
            auto* func = m_chatWidget->GetFunctionByNameInChain(STR("AddToShortChat"));
            if (!func)
            {
                VLOG(STR("[MoriaCppMod] AddToShortChat not found\n"));
                return;
            }

            FText ftext(text.c_str());
            std::vector<uint8_t> buf(func->GetParmsSize(), 0);
            std::memcpy(buf.data(), &ftext, sizeof(FText));
            m_chatWidget->ProcessEvent(func, buf.data());
        }
        END BELIEVED DEAD CODE */



        // â"€â"€ Debug Cheat Functions â"€â"€
        // Calls a named 0-param function on a debug menu actor.
        // Also tries direct class search (faster than scanning all Actor instances).
        bool callDebugFunc(const wchar_t* actorClass, const wchar_t* funcName)
        {
            // Try direct class search first (faster and more reliable)
            {
                std::vector<UObject*> directObjs;
                UObjectGlobals::FindAllOf(actorClass, directObjs);
                if (!directObjs.empty())
                {
                    for (auto* a : directObjs)
                    {
                        if (!a) continue;
                        auto* fn = a->GetFunctionByNameInChain(funcName);
                        if (fn)
                        {
                            a->ProcessEvent(fn, nullptr);
                            VLOG(STR("[MoriaCppMod] Called {}::{} (direct find)\n"),
                                                            std::wstring(actorClass), std::wstring(funcName));
                            return true;
                        }
                    }
                }
            }

            // Fallback: scan all actors by class name
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);
            for (auto* a : actors)
            {
                if (!a) continue;
                std::wstring cls = safeClassName(a);
                if (cls == actorClass)
                {
                    auto* fn = a->GetFunctionByNameInChain(funcName);
                    if (fn)
                    {
                        a->ProcessEvent(fn, nullptr);
                        VLOG(STR("[MoriaCppMod] Called {}::{} (actor scan)\n"), cls, std::wstring(funcName));
                        return true;
                    }
                    VLOG(STR("[MoriaCppMod] Function {} not found on {}\n"), std::wstring(funcName), cls);
                    return false;
                }
            }
            // Only log on first retry (not every frame)
            static int s_debugNotFoundCount = 0;
            if (++s_debugNotFoundCount <= 3)
                VLOG(STR("[MoriaCppMod] Actor {} not found (attempt {})\n"), std::wstring(actorClass), s_debugNotFoundCount);
            return false;
        }


        // Read debug menu bool properties to show current state
        // â"€â"€ 6F: Debug & Cheat Commands â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
        // Debug menu toggles (Free Build), rotation control, aimed building rotation

        // Reads bool properties from BP_DebugMenu_CraftingAndConstruction_C
        // using runtime property discovery (GetValuePtrByPropertyNameInChain).
        void showDebugMenuState()
        {
            std::vector<UObject*> actors;
            UObjectGlobals::FindAllOf(STR("Actor"), actors);
            for (auto* a : actors)
            {
                if (!a) continue;
                std::wstring cls = safeClassName(a);
                if (cls == STR("BP_DebugMenu_CraftingAndConstruction_C"))
                {
                    bool freeCon = getBoolProp(a, L"free_construction");
                    bool freeCraft = getBoolProp(a, L"free_crafting");
                    bool prereqs = getBoolProp(a, L"construction_prereqs");
                    bool stability = getBoolProp(a, L"construction_stability");
                    bool instant = getBoolProp(a, L"instant_crafting");
                    std::wstring msg = L"[Cheats] ";
                    msg += freeCon ? L"FreeBuild:ON " : L"FreeBuild:OFF ";
                    msg += freeCraft ? L"FreeCraft:ON " : L"FreeCraft:OFF ";
                    msg += instant ? L"InstantCraft:ON " : L"InstantCraft:OFF ";
                    msg += prereqs ? L"Prereqs:OFF " : L"Prereqs:ON ";
                    msg += stability ? L"Stability:OFF" : L"Stability:ON";
                    showOnScreen(msg, 5.0f, 0.0f, 1.0f, 1.0f);
                    VLOG(STR("[MoriaCppMod] {}\n"), msg);
                    return;
                }
            }
            showOnScreen(Loc::get("msg.debug_actor_not_found").c_str(), 3.0f, 1.0f, 0.3f, 0.3f);
        }

        // Reads the actual debug menu toggle state and syncs s_config flags.
        // Called on character load so the UI toggles match the game's real state.
        bool syncDebugToggleState()
        {
            // Try direct class search first
            std::vector<UObject*> objs;
            UObjectGlobals::FindAllOf(STR("BP_DebugMenu_CraftingAndConstruction_C"), objs);
            if (objs.empty())
            {
                // Fallback: scan all actors
                std::vector<UObject*> actors;
                UObjectGlobals::FindAllOf(STR("Actor"), actors);
                for (auto* a : actors)
                {
                    if (a && safeClassName(a) == STR("BP_DebugMenu_CraftingAndConstruction_C"))
                    {
                        objs.push_back(a);
                        break;
                    }
                }
            }
            if (objs.empty())
            {
                VLOG(STR("[MoriaCppMod] syncDebugToggleState: debug actor not found\n"));
                return false;
            }

            UObject* debugActor = objs[0];
            s_config.freeBuild = getBoolProp(debugActor, L"free_construction");

            VLOG(
                STR("[MoriaCppMod] syncDebugToggleState: freeBuild={}\n"),
                s_config.freeBuild ? 1 : 0);
            return true;
        }


        // findBuildHUD, resolveGATA, setGATARotation, toggleSnap, restoreSnap
        // -> moved to moria_placement.inl

        // Dispatch action for MC toolbar slot (used by both keyboard and click handlers)
        void dispatchMcSlot(int slot)
        {
            switch (slot)
            {
            case 0: // Rotation
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
                showOnScreen(msg.c_str(), 2.0f, 0.0f, 1.0f, 0.0f);
                updateMcRotationLabel();
                break;
            }
            case 1: // Target
                if (isModifierDown())
                    quickBuildFromTarget();
                else if (m_tiShowTick > 0)
                    hideTargetInfo();
                else
                    dumpAimedActor();
                break;
            case 2: // Stability Check
                runStabilityAudit();
                break;
            case 3: // Super Dwarf
                if (isModifierDown())
                    toggleFlyMode();
                else
                    toggleHideCharacter();
                break;
            case 4: // Toolbar Swap
                swapToolbar();
                break;
            case 5: // Snap Toggle
                toggleSnap();
                break;
            case 8: // Remove Target
                removeAimed();
                break;
            case 9: // Undo Last
                undoLast();
                break;
            case 10: // Remove All
                removeAllOfType();
                break;
            case 11: // Configuration — handled separately
                break;
            default:
                break;
            }
        }
