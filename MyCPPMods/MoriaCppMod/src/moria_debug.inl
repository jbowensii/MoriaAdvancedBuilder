// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_debug.inl — Display, debug, and cheat command methods               ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // â"€â"€ 6C: Display & UI Helpers â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
        // Discover PrintString param offsets via ForEachProperty
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

        // Display text on screen via PrintString (requires probePrintString first)
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

                // â"€â"€ Debug Cheat Functions â"€â"€
        // Call a 0-param function on a debug actor (direct class search + actor scan fallback)
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


        // â"€â"€ 6F: Debug & Cheat Commands â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€

        // Read and display debug menu toggle states
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
            showOnScreen(Loc::get("msg.debug_actor_not_found"), 3.0f, 1.0f, 0.3f, 0.3f);
        }

        // Sync s_config flags from actual debug menu state (called on character load)
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


        // Character rename — Win32 dialog on background thread, result picked up in on_update
        static DWORD WINAPI charNameDialogProc(LPVOID param)
        {
            auto* self = static_cast<MoriaCppMod*>(param);
            HWND owner = findGameWindow();

            // Build a simple modal dialog from a DLGTEMPLATE in memory
            // Layout: label + edit + OK/Cancel
            alignas(4) BYTE dlgBuf[512]{};
            auto* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
            dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
            dlg->cdit = 3; // 3 controls: static label, edit, OK button
            dlg->cx = 200;
            dlg->cy = 60;

            // Use DialogBoxIndirectParam with a dlgproc instead — simpler with InputBox approach
            // Actually, simplest: use a message-loop dialog via CreateWindowEx

            // Simplest reliable approach: TaskDialog is Vista+ but no edit control.
            // Use a plain Win32 window with an edit control.

            const wchar_t* className = L"MoriaCppModCharNameDlg";
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
                switch (msg) {
                case WM_CREATE: {
                    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                    HFONT font = CreateFontW(-28, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                    CreateWindowExW(0, L"STATIC", L"Character Name (max 22 chars):",
                        WS_CHILD | WS_VISIBLE, 20, 20, 560, 40, hwnd, nullptr, nullptr, nullptr);
                    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 68, 560, 48,
                        hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
                    CreateWindowExW(0, L"BUTTON", L"OK",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 220, 132, 160, 56,
                        hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
                    SendMessageW(edit, EM_SETLIMITTEXT, 22, 0);
                    // Apply font to all children
                    EnumChildWindows(hwnd, [](HWND child, LPARAM f) -> BOOL {
                        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
                        return TRUE;
                    }, reinterpret_cast<LPARAM>(font));
                    SetFocus(edit);
                    return 0;
                }
                case WM_COMMAND:
                    if (LOWORD(wp) == IDOK) {
                        wchar_t buf[24]{};
                        HWND edit = GetDlgItem(hwnd, 101);
                        GetWindowTextW(edit, buf, 23);
                        auto* self = reinterpret_cast<MoriaCppMod*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                        if (self && buf[0] != L'\0') {
                            self->m_pendingCharName = buf;
                            self->m_pendingCharNameReady.store(true);
                        }
                        DestroyWindow(hwnd);
                    }
                    return 0;
                case WM_KEYDOWN:
                    if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
                    break;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
                }
                return DefWindowProcW(hwnd, msg, wp, lp);
            };
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = className;
            RegisterClassExW(&wc);

            HWND dlgWnd = CreateWindowExW(WS_EX_TOPMOST, className, L"Rename Character",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                CW_USEDEFAULT, CW_USEDEFAULT, 632, 268,
                owner, nullptr, wc.hInstance, self);

            if (dlgWnd) {
                MSG m;
                while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                    if (!IsDialogMessageW(dlgWnd, &m)) {
                        TranslateMessage(&m);
                        DispatchMessageW(&m);
                    }
                }
            }
            UnregisterClassW(className, wc.hInstance);
            return 0;
        }

        void promptCharacterRename()
        {
            if (!m_characterLoaded) {
                showOnScreen(L"Character not loaded", 2.0f, 1.0f, 0.3f, 0.3f);
                return;
            }
            CreateThread(nullptr, 0, charNameDialogProc, this, 0, nullptr);
        }

        void applyPendingCharacterName()
        {
            std::wstring newName = m_pendingCharName;
            m_pendingCharName.clear();
            if (newName.empty()) return;

            // Find CustomizationManager component
            std::vector<UObject*> mgrs;
            UObjectGlobals::FindAllOf(STR("CustomizationManager"), mgrs);

            // Filter to our player's component
            UObject* pawn = getPawn();
            UObject* target = nullptr;
            for (auto* mgr : mgrs) {
                if (!mgr) continue;
                auto* outer = mgr->GetOuterPrivate();
                if (outer == pawn) { target = mgr; break; }
            }
            if (!target && !mgrs.empty()) target = mgrs[0]; // fallback: first found

            if (!target) {
                showOnScreen(L"CustomizationManager not found", 3.0f, 1.0f, 0.3f, 0.3f);
                VLOG(STR("[MoriaCppMod] applyPendingCharacterName: no CustomizationManager\n"));
                return;
            }

            auto* fn = target->GetFunctionByNameInChain(STR("SetCharacterName"));
            if (!fn) {
                showOnScreen(L"SetCharacterName not found", 3.0f, 1.0f, 0.3f, 0.3f);
                VLOG(STR("[MoriaCppMod] applyPendingCharacterName: SetCharacterName not found\n"));
                return;
            }

            // Build FString param: { wchar_t* Data, int32 Num, int32 Max }
            int32_t parmsSize = fn->GetParmsSize();
            std::vector<uint8_t> buf(parmsSize, 0);

            // Find the NewCharacterName param offset
            int nameOffset = -1;
            for (auto* prop : fn->ForEachProperty()) {
                if (prop->GetName() == STR("NewCharacterName")) {
                    nameOffset = prop->GetOffset_Internal();
                    break;
                }
            }
            if (nameOffset < 0) {
                showOnScreen(L"SetCharacterName param not found", 3.0f, 1.0f, 0.3f, 0.3f);
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

        // Dispatch MC toolbar slot action (keyboard and click handlers)
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
                showOnScreen(msg, 2.0f, 0.0f, 1.0f, 0.0f);
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
