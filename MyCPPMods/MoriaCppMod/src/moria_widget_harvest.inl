// moria_widget_harvest.inl — v6.6.0+ debug-only WidgetTree harvester.
// Constructs UMG widgets off-viewport, walks their WidgetTree, dumps every child's
// class/slot/anchors/padding/brush/font/text to JSON files under
// `<game>/ue4ss/Mods/MoriaCppMod/widget-harvest/<ClassName>.json`. Used to plan
// pixel-faithful C++ duplicates of the game's UI screens.
//
// Triggered by Shift+Num0 (modifier + bubble-info key) so it doesn't conflict.
// All construction is best-effort; classes that aren't loaded yet are skipped silently.

        // ---------- JSON escape helper ----------
        inline std::string jsonEscape(const std::wstring& w)
        {
            std::string out;
            out.reserve(w.size() + 2);
            // First convert to UTF-8 via WideCharToMultiByte
            int bytes = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string utf8;
            if (bytes > 0)
            {
                utf8.resize(bytes - 1);
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
            }
            for (char c : utf8)
            {
                switch (c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                            out += buf;
                        }
                        else out += c;
                        break;
                }
            }
            return out;
        }

        inline std::string jsonEscape(const char* s)
        {
            return jsonEscape(std::wstring(s, s + strlen(s)));
        }

        // ---------- Safe path-name read ----------
        inline std::string safeGetPathName(UObject* obj)
        {
            if (!obj) return "";
            try
            {
                return jsonEscape(obj->GetFullName());
            }
            catch (...) { return ""; }
        }

        inline std::string safeGetClassName(UObject* obj)
        {
            if (!obj) return "";
            try
            {
                auto* c = obj->GetClassPrivate();
                return c ? jsonEscape(c->GetName()) : "";
            }
            catch (...) { return ""; }
        }

        inline std::string safeGetWidgetMemberName(UObject* widget)
        {
            if (!widget) return "";
            try { return jsonEscape(widget->GetName()); }
            catch (...) { return ""; }
        }

        // ---------- Slot property readers ----------
        // Returns a JSON-formatted string describing the slot type and its key fields.
        // Handles CanvasPanelSlot, OverlaySlot, VerticalBoxSlot, HorizontalBoxSlot,
        // SizeBoxSlot, BorderSlot, ScrollBoxSlot, WidgetSwitcherSlot.
        inline std::string harvestSlot(UObject* widget)
        {
            if (!widget || !isObjectAlive(widget)) return "null";

            auto* slotPtr = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Slot"));
            UObject* slot = slotPtr ? *slotPtr : nullptr;
            if (!slot || !isObjectAlive(slot)) return "null";

            std::string slotClass = safeGetClassName(slot);
            std::string out = "{\"slotClass\":\"" + slotClass + "\"";

            // CanvasPanelSlot — anchors, alignment, offsets (LayoutData/Anchors/Offsets/Alignment)
            // The runtime layout is a struct FAnchorData containing Anchors, Offsets, Alignment.
            // UE4 stores it as LayoutData.Anchors.{Minimum,Maximum} (FVector2D),
            // LayoutData.Offsets.{Left,Top,Right,Bottom} (FMargin), LayoutData.Alignment (FVector2D).
            // We probe each known field by name.
            auto readF2 = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<float>(name);
                if (p) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), ",\"%s\":[%g,%g]", label.c_str(), p[0], p[1]);
                    out += buf;
                }
            };
            auto readF4 = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<float>(name);
                if (p) {
                    char buf[160];
                    snprintf(buf, sizeof(buf), ",\"%s\":[%g,%g,%g,%g]",
                             label.c_str(), p[0], p[1], p[2], p[3]);
                    out += buf;
                }
            };
            auto readF1 = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<float>(name);
                if (p) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), ",\"%s\":%g", label.c_str(), *p);
                    out += buf;
                }
            };
            auto readU8 = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<uint8_t>(name);
                if (p) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), ",\"%s\":%u", label.c_str(), static_cast<unsigned>(*p));
                    out += buf;
                }
            };
            auto readBool = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<bool>(name);
                if (p) {
                    out += ",\"" + label + "\":";
                    out += (*p ? "true" : "false");
                }
            };
            auto readI32 = [&](const wchar_t* name, std::string label)
            {
                auto* p = slot->GetValuePtrByPropertyNameInChain<int32_t>(name);
                if (p) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), ",\"%s\":%d", label.c_str(), *p);
                    out += buf;
                }
            };

            // Try common slot fields. Property-by-name reflection is robust to slot type.
            readBool(STR("bAutoSize"), "bAutoSize");
            readI32(STR("ZOrder"), "ZOrder");
            readU8(STR("HorizontalAlignment"), "HAlign");
            readU8(STR("VerticalAlignment"), "VAlign");
            readF1(STR("Size.SizeRule"), "SizeRule");
            readF1(STR("Size.Value"), "SizeValue");
            // FMargin Padding has Left/Top/Right/Bottom — reflection might surface these flat
            readF1(STR("Padding.Left"), "PadL");
            readF1(STR("Padding.Top"), "PadT");
            readF1(STR("Padding.Right"), "PadR");
            readF1(STR("Padding.Bottom"), "PadB");
            // CanvasPanelSlot uses LayoutData. Try walking its fields:
            readF1(STR("LayoutData.Anchors.Minimum.X"), "AnchorMinX");
            readF1(STR("LayoutData.Anchors.Minimum.Y"), "AnchorMinY");
            readF1(STR("LayoutData.Anchors.Maximum.X"), "AnchorMaxX");
            readF1(STR("LayoutData.Anchors.Maximum.Y"), "AnchorMaxY");
            readF1(STR("LayoutData.Offsets.Left"), "OffsetL");
            readF1(STR("LayoutData.Offsets.Top"), "OffsetT");
            readF1(STR("LayoutData.Offsets.Right"), "OffsetR");
            readF1(STR("LayoutData.Offsets.Bottom"), "OffsetB");
            readF1(STR("LayoutData.Alignment.X"), "AlignX");
            readF1(STR("LayoutData.Alignment.Y"), "AlignY");
            readBool(STR("bAutoSize"), "bAutoSize2");
            // SizeBoxSlot: WidthOverride / HeightOverride live on the SizeBox itself, not slot

            out += "}";
            return out;
        }

        // ---------- Widget-specific extra fields ----------
        inline std::string harvestWidgetExtras(UObject* widget)
        {
            if (!widget || !isObjectAlive(widget)) return "";
            std::string out;
            std::string cls = safeGetClassName(widget);

            // Text
            auto* textProp = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Text"));
            (void)textProp;
            // FText reading is fragile via reflection — call ToString UFunction if present
            auto* getTextFn = widget->GetFunctionByNameInChain(STR("GetText"));
            if (getTextFn)
            {
                int sz = getTextFn->GetParmsSize();
                std::vector<uint8_t> p(sz, 0);
                try { safeProcessEvent(widget, getTextFn, p.data()); } catch (...) {}
                auto* rv = findParam(getTextFn, STR("ReturnValue"));
                if (rv)
                {
                    // FText is a complex struct; try ToString chain
                    auto* toStrFn = widget->GetFunctionByNameInChain(STR("ToString"));
                    if (!toStrFn)
                    {
                        // We don't have direct FText->FString in reflection; skip to avoid crash.
                    }
                }
            }

            // Image / Border brush — read Brush.ResourceObject path
            auto* brushPtrA = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Brush.ResourceObject"));
            if (brushPtrA && *brushPtrA)
                out += ",\"brush\":\"" + safeGetPathName(*brushPtrA) + "\"";

            // SizeBox WidthOverride/HeightOverride
            auto* w = widget->GetValuePtrByPropertyNameInChain<float>(STR("WidthOverride"));
            if (w)
            {
                char buf[64]; snprintf(buf, sizeof(buf), ",\"WidthOverride\":%g", *w);
                out += buf;
            }
            auto* h = widget->GetValuePtrByPropertyNameInChain<float>(STR("HeightOverride"));
            if (h)
            {
                char buf[64]; snprintf(buf, sizeof(buf), ",\"HeightOverride\":%g", *h);
                out += buf;
            }
            auto* bw = widget->GetValuePtrByPropertyNameInChain<bool>(STR("bOverride_WidthOverride"));
            if (bw)
            {
                out += ",\"bOverride_WidthOverride\":";
                out += (*bw ? "true" : "false");
            }
            auto* bh = widget->GetValuePtrByPropertyNameInChain<bool>(STR("bOverride_HeightOverride"));
            if (bh)
            {
                out += ",\"bOverride_HeightOverride\":";
                out += (*bh ? "true" : "false");
            }

            // Visibility
            auto* vis = widget->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
            if (vis)
            {
                char buf[64]; snprintf(buf, sizeof(buf), ",\"Visibility\":%u", static_cast<unsigned>(*vis));
                out += buf;
            }

            // Color (TextBlock, Image)
            auto* cR = widget->GetValuePtrByPropertyNameInChain<float>(STR("ColorAndOpacity.SpecifiedColor.R"));
            auto* cG = widget->GetValuePtrByPropertyNameInChain<float>(STR("ColorAndOpacity.SpecifiedColor.G"));
            auto* cB = widget->GetValuePtrByPropertyNameInChain<float>(STR("ColorAndOpacity.SpecifiedColor.B"));
            auto* cA = widget->GetValuePtrByPropertyNameInChain<float>(STR("ColorAndOpacity.SpecifiedColor.A"));
            if (cR && cG && cB && cA)
            {
                char buf[160];
                snprintf(buf, sizeof(buf), ",\"Color\":[%g,%g,%g,%g]", *cR, *cG, *cB, *cA);
                out += buf;
            }

            // Font (TextBlock)
            auto* fontPtr = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Font.FontObject"));
            if (fontPtr && *fontPtr)
                out += ",\"font\":\"" + safeGetPathName(*fontPtr) + "\"";
            auto* fontSize = widget->GetValuePtrByPropertyNameInChain<int32_t>(STR("Font.Size"));
            if (fontSize)
            {
                char buf[64]; snprintf(buf, sizeof(buf), ",\"FontSize\":%d", *fontSize);
                out += buf;
            }
            auto* fontTypeface = widget->GetValuePtrByPropertyNameInChain<wchar_t>(STR("Font.TypefaceFontName"));
            (void)fontTypeface; // FName — skip for now, avoid crash

            return out;
        }

        // ---------- Recursive walk ----------
        inline void harvestWidgetTree(UObject* widget, int depth, std::ofstream& out, bool& firstChild);

        inline void emitWidgetJson(UObject* widget, int depth, std::ofstream& out)
        {
            if (!widget || !isObjectAlive(widget)) return;

            std::string indent(depth * 2, ' ');
            std::string memberName = safeGetWidgetMemberName(widget);
            std::string className = safeGetClassName(widget);

            out << indent << "{\n";
            out << indent << "  \"name\":\"" << memberName << "\",\n";
            out << indent << "  \"class\":\"" << className << "\",\n";
            out << indent << "  \"slot\":" << harvestSlot(widget);
            std::string extras = harvestWidgetExtras(widget);
            if (!extras.empty())
                out << ",\n" << indent << "  \"extras\":{" << (extras.empty() ? "" : extras.c_str() + 1) << "}";

            // Children: a UWidget that is also a UPanelWidget exposes Slots TArray
            // Each slot has a Content UWidget. We probe Slots property and walk it.
            // Some classes expose direct child references too (e.g. ContentSlot on UContentWidget).
            std::vector<UObject*> children;

            // UPanelWidget::Slots is TArray<UPanelSlot*>; each slot's Content is the UWidget.
            // Reflection: find "Slots" property as TArray<UObject*>
            auto* slotsAddr = widget->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
            if (slotsAddr)
            {
                int n = slotsAddr->Num();
                for (int i = 0; i < n; ++i)
                {
                    UObject* panelSlot = (*slotsAddr)[i];
                    if (!panelSlot || !isObjectAlive(panelSlot)) continue;
                    auto* contentPtr = panelSlot->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                    if (contentPtr && *contentPtr && isObjectAlive(*contentPtr))
                        children.push_back(*contentPtr);
                }
            }

            // UContentWidget single Content
            if (children.empty())
            {
                auto* singleContent = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (singleContent && *singleContent && isObjectAlive(*singleContent))
                    children.push_back(*singleContent);
            }

            // Special cases: WidgetSwitcher children come through Slots; SizeBox single child
            // also through Slots (size box has 1 slot). Border has Content. Already handled.

            if (!children.empty())
            {
                out << ",\n" << indent << "  \"children\":[\n";
                bool first = true;
                for (auto* c : children)
                {
                    if (!first) out << ",\n";
                    emitWidgetJson(c, depth + 2, out);
                    first = false;
                }
                out << "\n" << indent << "  ]";
            }

            out << "\n" << indent << "}";
        }

        // ---------- Driver ----------
        inline UClass* findWidgetClass(const wchar_t* shortName)
        {
            // Try FindAllOf first — class must be loaded already. If not loaded,
            // we skip this widget rather than risk loading a cooked uasset cold.
            std::vector<UObject*> insts;
            findAllOfSafe(shortName, insts);
            // FindAllOf returns instances; we want their class. Walk back to UClass.
            for (auto* o : insts)
            {
                if (!o) continue;
                auto* c = o->GetClassPrivate();
                if (c) return c;
            }
            // Fallback: try StaticFindObject by short name (no path)
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, shortName);
            return cls;
        }

        // Construct a widget off-viewport (no AddToViewport), harvest its WidgetTree
        // to a JSON file, then mark the widget pending-kill.
        inline void harvestOneWidget(const wchar_t* widgetClassShortName)
        {
            std::wstring shortName = widgetClassShortName;
            std::string utf8Name = jsonEscape(shortName);

            VLOG(STR("[WidgetHarvest] === harvesting {} ===\n"), shortName.c_str());

            UClass* uwClass = findWidgetClass(widgetClassShortName);
            if (!uwClass)
            {
                VLOG(STR("[WidgetHarvest] class not loaded, skipping: {}\n"), shortName.c_str());
                showOnScreen((L"Harvest skip (class not loaded): " + shortName).c_str(), 2.5f, 1.0f, 0.7f, 0.3f);
                return;
            }

            auto* pc = findPlayerController();
            if (!pc) { VLOG(STR("[WidgetHarvest] no PC, abort\n")); return; }

            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { VLOG(STR("[WidgetHarvest] WBL missing\n")); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = uwClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;

            UObject* userWidget = nullptr;
            try
            {
                safeProcessEvent(wblCDO, createFn, cp.data());
                userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            }
            catch (...)
            {
                VLOG(STR("[WidgetHarvest] Create threw, abort {}\n"), shortName.c_str());
                return;
            }

            if (!userWidget || !isObjectAlive(userWidget))
            {
                VLOG(STR("[WidgetHarvest] Create returned null for {}\n"), shortName.c_str());
                return;
            }

            // Get the WidgetTree, walk RootWidget
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* root = nullptr;
            if (widgetTree)
            {
                auto* rootSlot = widgetTree->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                root = rootSlot ? *rootSlot : nullptr;
            }

            std::string outDir = modPath("Mods/MoriaCppMod/widget-harvest/");
            // Best-effort mkdir
            CreateDirectoryA(outDir.c_str(), nullptr);

            // UTF-8 short name -> simple ascii-safe filename
            std::string fname = outDir + utf8Name + ".json";
            std::ofstream f = openOutputFile(fname, std::ios::trunc);
            if (!f.is_open())
            {
                VLOG(STR("[WidgetHarvest] failed to open {}\n"), shortName.c_str());
                return;
            }

            f << "{\n";
            f << "  \"widgetClass\":\"" << utf8Name << "\",\n";
            f << "  \"harvestedAt\":\"" << __DATE__ << " " << __TIME__ << "\",\n";
            f << "  \"modVersion\":\"6.6.0\",\n";

            if (root)
            {
                f << "  \"root\":\n";
                emitWidgetJson(root, 1, f);
                f << "\n";
            }
            else
            {
                f << "  \"root\": null,\n";
                f << "  \"note\": \"WidgetTree or RootWidget not found — class may need different construction path\"\n";
            }
            f << "}\n";
            f.close();

            VLOG(STR("[WidgetHarvest] wrote {}.json\n"), shortName.c_str());

            // Tear down — RemoveFromParent (safe even if never added) + drop reference
            try
            {
                auto* removeFn = userWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn)
                {
                    int rsz = removeFn->GetParmsSize();
                    std::vector<uint8_t> rp(rsz, 0);
                    safeProcessEvent(userWidget, removeFn, rp.data());
                }
            }
            catch (...) {}
        }

        // Harvest the 12 widget classes relevant to the Join World UI plan.
        // Run from the main menu where these classes are most likely loaded.
        inline void harvestJoinWorldWidgets()
        {
            VLOG(STR("[WidgetHarvest] BEGIN harvest run\n"));
            showOnScreen(L"Harvesting Join World widgets... (see Mods/MoriaCppMod/widget-harvest/)",
                         3.0f, 0.4f, 0.9f, 0.4f);

            const wchar_t* targets[] = {
                STR("WBP_UI_JoinWorldScreen_C"),
                STR("WBP_UI_ChooseWorldScreen_C"),
                STR("WBP_JoinWorldScreen_GameDataPanel_C"),
                STR("WBP_UI_AdvancedJoinOptions_C"),
                STR("WBP_UI_SessionHistoryList_C"),
                STR("WBP_UI_SessionHistory_Item_C"),
                STR("WBP_UI_NetworkAlert_C"),
                STR("WBP_UI_PopUp_DedicatedServerDetails_C"),
                STR("WBP_FrontEndButton_C"),
                STR("UUI_WBP_Craft_BigButton_C"),
                STR("UUI_WBP_HUD_ControlPrompt_C"),
                STR("UI_WBP_LowerThird_C"),
            };
            for (auto* t : targets)
                harvestOneWidget(t);

            VLOG(STR("[WidgetHarvest] END harvest run\n"));
            showOnScreen(L"Widget harvest complete", 4.0f, 0.4f, 1.0f, 0.4f);
        }
