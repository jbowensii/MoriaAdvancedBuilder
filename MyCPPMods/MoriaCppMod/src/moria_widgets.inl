
// moria_widgets.inl — UMG widget management, F12 config panel, crosshair reticle (v5.5.0)
// deferRemoveWidget: hide immediately + remove next frame (prevents Slate PaintFastPath crash)
// Crosshair reticle: T_UI_Bow_Reticle centered on screen, 40s auto-hide, resolution-scaled via uiScale
// F12 panel: 1540px width, wLeft corrected for alignment, Environment tab grouped by bubble with icon click narrowed to 64px

        // ---- Deferred Widget Removal (prevents Slate PaintFastPath crash) ----
        // Widgets are hidden immediately but removed from viewport on next frame tick.
        std::vector<UObject*> m_pendingWidgetRemovals;

        void deferRemoveWidget(UObject* widget)
        {
            if (!widget) return;
            // Hide immediately (prevents interaction and visual this frame)
            auto* visFn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (visFn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(widget, visFn, p); }
            m_pendingWidgetRemovals.push_back(widget);
        }

        void tickDeferredWidgetRemovals()
        {
            if (m_pendingWidgetRemovals.empty()) return;
            for (auto* w : m_pendingWidgetRemovals)
            {
                // v6.20.23 — alive-check before deref. Between deferRemoveWidget
                // (this frame) and tick (next frame) a world transition can GC
                // the widget. Without this, GetFunctionByNameInChain AVs.
                if (!w || !isObjectAlive(w)) continue;
                auto* removeFn = w->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (!removeFn) removeFn = w->GetFunctionByNameInChain(STR("RemoveFromViewport"));
                if (removeFn) safeProcessEvent(w, removeFn, nullptr);
            }
            m_pendingWidgetRemovals.clear();
        }
        // ---- End Deferred Widget Removal ----

        void umgSetBrush(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            if (!img || !isObjectAlive(img) || !setBrushFn) return;
            ensureBrushOffset(img);
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = true;
            safeProcessEvent(img, setBrushFn, bp.data());
        }


        void umgSetOpacity(UObject* img, float opacity)
        {
            if (!img || !isObjectAlive(img)) return;
            auto* fn = img->GetFunctionByNameInChain(STR("SetOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal()) = opacity;
            safeProcessEvent(img, fn, buf.data());
        }


        void umgSetSlotSize(UObject* slot, float value, uint8_t sizeRule)
        {
            if (!slot || !isObjectAlive(slot)) return;
            auto* fn = slot->GetFunctionByNameInChain(STR("SetSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* base = buf.data() + p->GetOffset_Internal();
            *reinterpret_cast<float*>(base + 0) = value;
            *reinterpret_cast<uint8_t*>(base + 4) = sizeRule;
            safeProcessEvent(slot, fn, buf.data());
        }


        void umgSetSlotPadding(UObject* slot, float left, float top, float right, float bottom)
        {
            if (!slot || !isObjectAlive(slot)) return;
            auto* fn = slot->GetFunctionByNameInChain(STR("SetPadding"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InPadding"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* m = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            m[0] = left; m[1] = top; m[2] = right; m[3] = bottom;
            safeProcessEvent(slot, fn, buf.data());
        }


        void umgSetHAlign(UObject* slot, uint8_t align)
        {
            if (!slot || !isObjectAlive(slot)) return;
            auto* fn = slot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InHorizontalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            safeProcessEvent(slot, fn, buf.data());
        }


        void umgSetVAlign(UObject* slot, uint8_t align)
        {
            if (!slot || !isObjectAlive(slot)) return;
            auto* fn = slot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InVerticalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            safeProcessEvent(slot, fn, buf.data());
        }


        void umgSetRenderScale(UObject* widget, float sx, float sy)
        {
            if (!widget || !isObjectAlive(widget)) return;
            auto* fn = widget->GetFunctionByNameInChain(STR("SetRenderScale"));
            if (!fn) return;
            auto* p = findParam(fn, STR("Scale"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = sx; v[1] = sy;
            safeProcessEvent(widget, fn, buf.data());
        }


        bool getMousePositionSlate(float& outX, float& outY)
        {
            auto* pc = findPlayerController();
            if (!pc) return false;

            auto* fn = pc->GetFunctionByNameInChain(STR("GetMousePositionScaledByDPI"));
            if (fn)
            {
                auto* pPlayer = findParam(fn, STR("Player"));
                auto* pLocX   = findParam(fn, STR("LocationX"));
                auto* pLocY   = findParam(fn, STR("LocationY"));
                auto* pRV     = findParam(fn, STR("ReturnValue"));
                if (pLocX && pLocY)
                {
                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);

                    if (pPlayer) *reinterpret_cast<UObject**>(buf.data() + pPlayer->GetOffset_Internal()) = pc;
                    safeProcessEvent(pc, fn, buf.data());
                    float x = *reinterpret_cast<float*>(buf.data() + pLocX->GetOffset_Internal());
                    float y = *reinterpret_cast<float*>(buf.data() + pLocY->GetOffset_Internal());
                    bool ok = pRV ? *reinterpret_cast<bool*>(buf.data() + pRV->GetOffset_Internal()) : true;
                    if (ok && (x > 0.0f || y > 0.0f)) { outX = x; outY = y; return true; }
                }
            }

            if (m_wllClass)
            {
                UObject* cdo = m_wllClass->GetClassDefaultObject();
                auto* fn2 = m_wllClass->GetFunctionByNameInChain(STR("GetMousePositionOnViewport"));
                if (cdo && fn2)
                {
                    auto* pWC = findParam(fn2, STR("WorldContextObject"));
                    auto* pRV = findParam(fn2, STR("ReturnValue"));
                    if (pRV)
                    {
                        int sz = fn2->GetParmsSize();
                        std::vector<uint8_t> buf(sz, 0);
                        if (pWC && pc) *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = pc;
                        safeProcessEvent(cdo, fn2, buf.data());
                        auto* rv = reinterpret_cast<float*>(buf.data() + pRV->GetOffset_Internal());
                        if (rv[0] > 0.0f || rv[1] > 0.0f) { outX = rv[0]; outY = rv[1]; return true; }
                    }
                }
            }
            return false;
        }


        void setWidgetPosition(UObject* widget, float x, float y, bool bRemoveDPIScale = false)
        {
            if (!widget || !isObjectAlive(widget)) return;
            auto* fn = widget->GetFunctionByNameInChain(STR("SetPositionInViewport"));
            if (!fn) return;
            auto* pPos = findParam(fn, STR("Position"));
            auto* pDPI = findParam(fn, STR("bRemoveDPIScale"));
            if (!pPos) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + pPos->GetOffset_Internal());
            v[0] = x; v[1] = y;
            if (pDPI) *reinterpret_cast<bool*>(buf.data() + pDPI->GetOffset_Internal()) = bRemoveDPIScale;
            safeProcessEvent(widget, fn, buf.data());
        }


        void umgSetImageColor(UObject* img, float r, float g, float b, float a)
        {
            if (!img || !isObjectAlive(img)) return;
            auto* fn = img->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InColorAndOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* c = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            c[0] = r; c[1] = g; c[2] = b; c[3] = a;
            safeProcessEvent(img, fn, buf.data());
        }


        UObject* getSlotStateImage(int tb, int slot)
        {
            switch (tb)
            {
            case 0: return (slot >= 0 && slot < 8) ? m_umgStateImages[slot] : nullptr;
            case 1: return (slot == 0) ? m_abStateImage : nullptr;
            case 2: return (slot >= 0 && slot < MC_SLOTS) ? m_mcStateImages[slot] : nullptr;
            default: return nullptr;
            }
        }


        bool hitTestToolbarSlot(float curFracX, float curFracY, int& outTB, int& outSlot)
        {
            outTB = -1; outSlot = -1;
            constexpr float kRenderScale = 0.81f;
            struct TBInfo { int idx; int cols; int rows; UObject* widget; };
            TBInfo bars[] = {
                {2, 4, 3, m_mcBarWidget},
                {0, 8, 1, m_umgBarWidget},
                {1, 1, 1, m_abBarWidget},
            };
            for (auto& b : bars)
            {
                if (!b.widget) continue;
                float posX = (m_toolbarPosX[b.idx] >= 0) ? m_toolbarPosX[b.idx] : TB_DEF_X[b.idx];
                float posY = (m_toolbarPosY[b.idx] >= 0) ? m_toolbarPosY[b.idx] : TB_DEF_Y[b.idx];

                float halfW = m_toolbarSizeW[b.idx] * kRenderScale * 0.5f;
                float halfH = m_toolbarSizeH[b.idx] * kRenderScale * 0.5f;
                if (halfW <= 0 || halfH <= 0) continue;
                float relX = curFracX - (posX - halfW);
                float relY = curFracY - (posY - halfH);
                float fullW = halfW * 2.0f, fullH = halfH * 2.0f;
                if (relX < 0 || relX >= fullW || relY < 0 || relY >= fullH) continue;
                int col = static_cast<int>(relX / (fullW / b.cols));
                int row = static_cast<int>(relY / (fullH / b.rows));
                col = std::clamp(col, 0, b.cols - 1);
                row = std::clamp(row, 0, b.rows - 1);
                outTB = b.idx;
                outSlot = row * b.cols + col;
                return true;
            }
            return false;
        }


        void umgSetText(UObject* textBlock, const std::wstring& text)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetText"));
            if (!fn) return;
            auto* pInText = findParam(fn, STR("InText"));
            if (!pInText) return;
            FText ftext(text.c_str());
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pInText->GetOffset_Internal(), &ftext, sizeof(FText));
            safeProcessEvent(textBlock, fn, buf.data());
        }


        void umgSetTextColor(UObject* textBlock, float r, float g, float b, float a)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InColorAndOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* color = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            color[0] = r; color[1] = g; color[2] = b; color[3] = a;

            safeProcessEvent(textBlock, fn, buf.data());
        }


        void umgSetBold(UObject* textBlock)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            probeFontStruct(textBlock);
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;


            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, tbRaw + fontOff, FONT_STRUCT_SIZE);


            RC::Unreal::FName boldName(STR("Bold"), RC::Unreal::FNAME_Add);
            std::memcpy(fontBuf + fontTypefaceName(), &boldName, sizeof(RC::Unreal::FName));


            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            safeProcessEvent(textBlock, setFontFn, buf.data());
        }

        void umgSetFontSize(UObject* textBlock, int32_t fontSize)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, tbRaw + fontOff, FONT_STRUCT_SIZE);
            std::memcpy(fontBuf + fontSizeOff(), &fontSize, sizeof(int32_t));

            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            safeProcessEvent(textBlock, setFontFn, buf.data());
        }


        void umgSetFontAndSize(UObject* textBlock, UObject* fontObj, int32_t fontSize)
        {
            if (!textBlock || !isObjectAlive(textBlock)) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;
            probeFontStruct(textBlock);
            uint8_t* raw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, raw + fontOff, FONT_STRUCT_SIZE);
            if (fontObj) *reinterpret_cast<UObject**>(fontBuf + 0x00) = fontObj;
            std::memcpy(fontBuf + fontSizeOff(), &fontSize, sizeof(int32_t));
            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            safeProcessEvent(textBlock, setFontFn, buf.data());
        }


        UObject* createTextBlock(const std::wstring& text, float r, float g, float b, float a, int32_t fontSize)
        {
            auto* tbClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!tbClass) return nullptr;
            FStaticConstructObjectParameters tbP(tbClass, nullptr);
            UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
            if (!tb) return nullptr;
            umgSetText(tb, text);
            umgSetTextColor(tb, r, g, b, a);
            umgSetFontSize(tb, fontSize);
            return tb;
        }


        void refreshKeyLabels()
        {
            for (int i = 0; i < 8; i++)
                if (m_umgKeyLabels[i])
                    umgSetText(m_umgKeyLabels[i], keyName(s_bindings[i].key));
            for (int i = 0; i < MC_SLOTS; i++)
                if (m_mcKeyLabels[i])
                    umgSetText(m_mcKeyLabels[i], keyName(s_bindings[MC_BIND_BASE + i].key));

            if (m_abKeyLabel)
                umgSetText(m_abKeyLabel, keyName(s_bindings[BIND_AB_OPEN].key));
        }


        void updateMcRotationLabel()
        {
            if (!m_mcRotationLabel) return;
            int step = s_overlay.rotationStep;
            int total = s_overlay.totalRotation;
            std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
            umgSetText(m_mcRotationLabel, txt);
        }


        void setUmgSlotState(int slot, UmgSlotState state)
        {
            if (slot < 0 || slot >= 8 || !m_umgStateImages[slot]) return;
            m_umgSlotStates[slot] = state;
            UObject* tex = nullptr;
            switch (state)
            {
            case UmgSlotState::Empty:    tex = m_umgTexEmpty; break;
            case UmgSlotState::Inactive: tex = m_umgTexInactive; break;
            case UmgSlotState::Active:   tex = m_umgTexActive; break;
            }
            if (!tex || !m_umgSetBrushFn) return;
            umgSetBrush(m_umgStateImages[slot], tex, m_umgSetBrushFn);
        }


        void umgSetBrushNoMatch(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            if (!img || !isObjectAlive(img) || !setBrushFn) return;
            ensureBrushOffset(img);
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = false;
            safeProcessEvent(img, setBrushFn, bp.data());
        }


        void umgSetBrushSize(UObject* img, float w, float h)
        {
            if (!img || !isObjectAlive(img)) return;
            auto* fn = img->GetFunctionByNameInChain(STR("SetBrushSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("DesiredSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = w; v[1] = h;
            safeProcessEvent(img, fn, buf.data());
        }


        void setUmgSlotIcon(int slot, UObject* texture)
        {
            if (slot < 0 || slot >= 8 || !m_umgIconImages[slot] || !m_umgSetBrushFn) return;
            m_umgIconTextures[slot] = texture;
            if (texture)
            {

                umgSetBrush(m_umgIconImages[slot], texture, m_umgSetBrushFn);


                uint8_t* iBase = reinterpret_cast<uint8_t*>(m_umgIconImages[slot]);
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeX());
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeY());


                float containerW = 64.0f;
                float containerH = 64.0f;
                if (m_umgStateImages[slot])
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(m_umgStateImages[slot]);
                    containerW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
                    containerH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());
                }
                if (containerW < 1.0f) containerW = 64.0f;
                if (containerH < 1.0f) containerH = 64.0f;


                float iconW = containerW;
                float iconH = containerH;
                if (texW > 0.0f && texH > 0.0f)
                {
                    float scaleX = containerW / texW;
                    float scaleY = containerH / texH;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    scale *= 0.76f;
                    iconW = texW * scale;
                    iconH = texH * scale;
                }

                umgSetBrushSize(m_umgIconImages[slot], iconW, iconH);
                umgSetOpacity(m_umgIconImages[slot], 1.0f);
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} icon sized: {}x{} (container: {}x{}, tex: {}x{})\n"),
                                                slot, iconW, iconH, containerW, containerH, texW, texH);
            }
            else
            {
                umgSetOpacity(m_umgIconImages[slot], 0.0f);
            }
        }


        UObject* findTexture2DByName(const std::wstring& name)
        {
            if (name.empty()) return nullptr;
            std::vector<UObject*> textures;
            findAllOfSafe(STR("Texture2D"), textures);
            for (auto* t : textures)
            {
                if (!t) continue;
                if (std::wstring(t->GetName()) == name) return t;
            }
            return nullptr;
        }


        void updateBuildersBar()
        {
            // v6.20.22 — also refresh the NEW Building Bar icons (top-of-screen).
            // Without this, SHIFT+F# updates m_recipeSlots[i].textureName + the
            // OLD UMG bar but the NBB icons stay at whatever was rendered when
            // the bar was first created. User reported: "icon does not change"
            // (they were looking at the top bar).
            if (m_newBuildingBar && isObjectAlive(m_newBuildingBar))
            {
                populateNewBuildingBarIcons();
            }
            if (!m_umgBarWidget) return;
            for (int i = 0; i < 8; i++)
            {
                if (!m_recipeSlots[i].used)
                {
                    setUmgSlotState(i, UmgSlotState::Empty);
                    if (m_umgIconTextures[i] || !m_umgIconNames[i].empty())
                    {
                        setUmgSlotIcon(i, nullptr);
                        m_umgIconNames[i].clear();
                    }
                }
                else
                {
                    if (i == m_activeBuilderSlot)
                        setUmgSlotState(i, UmgSlotState::Active);
                    else
                        setUmgSlotState(i, UmgSlotState::Inactive);


                    bool nameChanged = (m_umgIconNames[i] != m_recipeSlots[i].textureName);
                    if (nameChanged)
                    {

                        m_umgIconTextures[i] = nullptr;
                        m_umgIconNames[i] = m_recipeSlots[i].textureName;
                    }


                    if (!m_umgIconTextures[i] && !m_recipeSlots[i].textureName.empty())
                    {
                        UObject* tex = findTexture2DByName(m_recipeSlots[i].textureName);
                        if (tex)
                        {
                            setUmgSlotIcon(i, tex);
                            VLOG(STR("[MoriaCppMod] [UMG] Slot #{} icon set: {}\n"),
                                                            i, m_recipeSlots[i].textureName);
                        }
                    }
                }
            }
        }

        void destroyExperimentalBar()
        {
            if (!m_umgBarWidget) return;
            deferRemoveWidget(m_umgBarWidget);
            m_umgBarWidget = nullptr;
            m_umgSetBrushFn = nullptr;
            for (int i = 0; i < 8; i++)
            {
                m_umgSlotButtons[i] = nullptr;
                m_umgStateImages[i] = nullptr;
                m_umgIconImages[i] = nullptr;
                m_umgIconTextures[i] = nullptr;
                m_umgIconNames[i].clear();
                m_umgSlotStates[i] = UmgSlotState::Empty;
                m_umgKeyLabels[i] = nullptr;
                m_umgKeyBgImages[i] = nullptr;
            }
            m_umgTexBlankRect = nullptr;
            VLOG(STR("[MoriaCppMod] [UMG] Bar removed from viewport\n"));
        }

        void createExperimentalBar()
        {
            if (m_umgBarWidget)
            {
                destroyExperimentalBar();
                showOnScreen(Loc::get("msg.umg_bar_removed").c_str(), 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [UMG] === Creating 8-slot toolbar ===\n"));


            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            {
                std::vector<UObject*> textures;
                findAllOfSafe(STR("Texture2D"), textures);
                VLOG(STR("[MoriaCppMod] [UMG] Found {} Texture2D objects\n"), textures.size());
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Empty")) texEmpty = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Disabled")) texInactive = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                }
            }
            VLOG(STR("[MoriaCppMod] [UMG] Textures: frame={} empty={} inactive={} active={} blankRect={}\n"),
                                            texFrame ? STR("OK") : STR("NO"), texEmpty ? STR("OK") : STR("NO"),
                                            texInactive ? STR("OK") : STR("NO"), texActive ? STR("OK") : STR("NO"),
                                            texBlankRect ? STR("OK") : STR("NO"));
            if (!texFrame || !texEmpty)
            {
                showErrorBox(L"UMG: textures not found!");
                return;
            }
            m_umgTexEmpty = texEmpty;
            m_umgTexInactive = texInactive;
            m_umgTexActive = texActive;
            m_umgTexBlankRect = texBlankRect;


            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* buttonClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"UMG: missing widget classes!");
                return;
            }


            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"UMG: no PlayerController!"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showErrorBox(L"UMG: WBL not found!"); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) { showErrorBox(L"UMG: WBL CDO null!"); return; }

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"UMG: CreateWidget null!"); return; }


            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"UMG: SetBrushFromTexture missing!"); return; }
            m_umgSetBrushFn = setBrushFn;


            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) { showErrorBox(L"UMG: outer border failed!"); return; }


            if (widgetTree)
                setRootWidget(widgetTree, outerBorder);


            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(outerBorder, setBrushColorFn, cb.data());
                }
            }

            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 0.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 0.0f;
                    safeProcessEvent(outerBorder, setBorderPadFn, pp.data());
                }
            }


            FStaticConstructObjectParameters innerBorderP(borderClass, outer);
            UObject* innerBorder = UObjectGlobals::StaticConstructObject(innerBorderP);
            if (!innerBorder) { showErrorBox(L"UMG: inner border failed!"); return; }


            auto* setBrushColorFn2 = innerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn2)
            {
                auto* pColor = findParam(setBrushColorFn2, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn2->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(innerBorder, setBrushColorFn2, cb.data());
                }
            }


            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = innerBorder;
                safeProcessEvent(outerBorder, setContentFn, sc.data());
            }


            FStaticConstructObjectParameters hboxP(hboxClass, outer);
            UObject* hbox = UObjectGlobals::StaticConstructObject(hboxP);
            if (!hbox) { showErrorBox(L"UMG: HBox failed!"); return; }

            auto* setContentFn2 = innerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn2)
            {
                auto* pContent = findParam(setContentFn2, STR("Content"));
                int sz = setContentFn2->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = hbox;
                safeProcessEvent(innerBorder, setContentFn2, sc.data());
            }


            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            VLOG(STR("[MoriaCppMod] [UMG] Creating 8 slot columns...\n"));
            for (int i = 0; i < 8; i++)
            {

                FStaticConstructObjectParameters vboxP(vboxClass, outer);
                UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                if (!vbox) continue;


                FStaticConstructObjectParameters siP(imageClass, outer);
                UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                if (!stateImg) continue;
                FStaticConstructObjectParameters iiP(imageClass, outer);
                UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                if (!iconImg) continue;
                FStaticConstructObjectParameters fiP(imageClass, outer);
                UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                if (!frameImg) continue;


                FStaticConstructObjectParameters olP(overlayClass, outer);
                UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                if (!overlay) continue;


                umgSetBrush(stateImg, texEmpty, setBrushFn);
                umgSetBrush(frameImg, texFrame, setBrushFn);

                umgSetOpacity(iconImg, 0.0f);


                if (i == 0)
                {
                    uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
                    frameW = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeX());
                    frameH = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeY());
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    stateW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
                    stateH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());
                    VLOG(STR("[MoriaCppMod] [UMG] Frame icon: {}x{}, State icon: {}x{}\n"),
                                                    frameW, frameH, stateW, stateH);
                }


                umgSetOpacity(stateImg, 1.0f);

                umgSetOpacity(frameImg, 0.25f);


                auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    auto* pC = findParam(addToOverlayFn, STR("Content"));
                    auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));


                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                        safeProcessEvent(overlay, addToOverlayFn, ap.data());
                        UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateOlSlot)
                        {
                            auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                                safeProcessEvent(stateOlSlot, setHAFn, hb.data());
                            }
                            auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                                safeProcessEvent(stateOlSlot, setVAFn, vb.data());
                            }
                        }
                    }

                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                        safeProcessEvent(overlay, addToOverlayFn, ap.data());
                        UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (iconSlot)
                        {

                            auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                                safeProcessEvent(iconSlot, setHAFn, hb.data());
                            }
                            auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                                safeProcessEvent(iconSlot, setVAFn, vb.data());
                            }
                        }
                    }
                }


                auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (addToVBoxFn)
                {
                    auto* pC = findParam(addToVBoxFn, STR("Content"));
                    auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));


                    {
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                        safeProcessEvent(vbox, addToVBoxFn, ap.data());
                        UObject* stateSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateSlot)
                        {
                            umgSetSlotSize(stateSlot, 1.0f, 0);
                            umgSetHAlign(stateSlot, 2);
                        }
                    }


                    {

                        FStaticConstructObjectParameters foP(overlayClass, outer);
                        UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                        if (frameOverlay)
                        {
                            auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                            if (addToFoFn)
                            {
                                auto* foC = findParam(addToFoFn, STR("Content"));
                                auto* foR = findParam(addToFoFn, STR("ReturnValue"));


                                {
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                    safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                                }


                                if (texBlankRect)
                                {
                                    FStaticConstructObjectParameters kbP(imageClass, outer);
                                    UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                    if (keyBgImg && setBrushFn)
                                    {
                                        umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                        umgSetOpacity(keyBgImg, 0.8f);

                                        int sz2 = addToFoFn->GetParmsSize();
                                        std::vector<uint8_t> ap2(sz2, 0);
                                        if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyBgImg;
                                        safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                                        UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (kbSlot)
                                        {
                                            auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(kbSlot, setHA, h.data()); }
                                            auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(kbSlot, setVA, v.data()); }
                                        }
                                        m_umgKeyBgImages[i] = keyBgImg;
                                    }
                                }


                                if (textBlockClass)
                                {
                                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                    UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                    if (keyLabel)
                                    {
                                        std::wstring kn = keyName(s_bindings[i].key);
                                        umgSetText(keyLabel, kn);
                                        umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);

                                        int sz2 = addToFoFn->GetParmsSize();
                                        std::vector<uint8_t> ap2(sz2, 0);
                                        if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyLabel;
                                        safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                                        UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (tlSlot)
                                        {
                                            auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(tlSlot, setHA, h.data()); }
                                            auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(tlSlot, setVA, v.data()); }
                                        }
                                        m_umgKeyLabels[i] = keyLabel;
                                    }
                                }
                            }
                        }


                        UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                        safeProcessEvent(vbox, addToVBoxFn, ap.data());
                        UObject* frameSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (frameSlot)
                        {
                            umgSetSlotSize(frameSlot, 1.0f, 0);
                            umgSetHAlign(frameSlot, 2);

                            float overlapPx = stateH * 0.15f;
                            umgSetSlotPadding(frameSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                        }
                    }
                }


                // Wrap slot VBox in UButton for gamepad navigation + native click
                UObject* slotWidget = vbox;
                if (buttonClass)
                {
                    FStaticConstructObjectParameters btnP(buttonClass, outer);
                    UObject* btn = UObjectGlobals::StaticConstructObject(btnP);
                    if (btn)
                    {
                        float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        std::memcpy(reinterpret_cast<uint8_t*>(btn) + 0x03B0, transparent, 16);
                        *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(btn) + 0x03C3) = true;
                        auto* setContentFn = btn->GetFunctionByNameInChain(STR("SetContent"));
                        if (setContentFn)
                        {
                            struct { UObject* Content{nullptr}; UObject* Ret{nullptr}; } scp{};
                            scp.Content = vbox;
                            safeProcessEvent(btn, setContentFn, &scp);
                        }
                        else
                            addChildToPanel(btn, STR("AddChild"), vbox);
                        m_umgSlotButtons[i] = btn;
                        slotWidget = btn;
                    }
                }

                auto* addToHBoxFn = hbox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                if (addToHBoxFn)
                {
                    auto* pC = findParam(addToHBoxFn, STR("Content"));
                    auto* pR = findParam(addToHBoxFn, STR("ReturnValue"));
                    int sz = addToHBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = slotWidget;
                    safeProcessEvent(hbox, addToHBoxFn, ap.data());
                    UObject* hSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (hSlot)
                    {
                        umgSetSlotSize(hSlot, 1.0f, 0);
                        umgSetVAlign(hSlot, 2);
                    }
                }

                m_umgStateImages[i] = stateImg;
                m_umgIconImages[i] = iconImg;
                m_umgSlotStates[i] = UmgSlotState::Empty;
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} created\n"), i);
            }


            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;


            if (!m_wllClass)
            {
                auto* wllClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                    STR("/Script/UMG.WidgetLayoutLibrary"));
                if (wllClass) m_wllClass = wllClass;
            }
            VLOG(STR("[MoriaCppMod] [UMG] Viewport: {}x{} viewportScale={:.3f} slate={:.0f}x{:.0f} aspect={:.3f}\n"),
                viewW, viewH, m_screen.viewportScale, m_screen.slateW, m_screen.slateH, m_screen.aspectRatio);


            constexpr float umgScale = 0.81f;
            umgSetRenderScale(outerBorder, umgScale, umgScale);


            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;

            float totalW = 8.0f * iconW * umgScale;
            float totalH = (frameH + stateH - vOverlap) * umgScale;
            VLOG(STR("[MoriaCppMod] [UMG] Frame size: {}x{} (iconW={} frameH={} stateH={})\n"),
                                            totalW, totalH, iconW, frameH, stateH);


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = totalW; v[1] = totalH;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            {
                float fracX = (m_toolbarPosX[0] >= 0) ? m_toolbarPosX[0] : TB_DEF_X[0];
                float fracY = (m_toolbarPosY[0] >= 0) ? m_toolbarPosY[0] : TB_DEF_Y[0];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }


            m_toolbarSizeW[0] = m_screen.slateToFracX(totalW);
            m_toolbarSizeH[0] = m_screen.slateToFracY(totalH);
            m_umgBarWidget = userWidget;
            showOnScreen(Loc::get("msg.builders_bar_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [UMG] === Builders bar creation complete ===\n"));


            updateBuildersBar();
        }


        void destroyAdvancedBuilderBar()
        {
            if (!m_abBarWidget) return;
            deferRemoveWidget(m_abBarWidget);
            m_abBarWidget = nullptr;
            m_abSlotButton = nullptr;
            m_abKeyLabel = nullptr;
            m_abStateImage = nullptr;
            VLOG(STR("[MoriaCppMod] [AB] Advanced Builder toolbar removed\n"));
        }

        void createAdvancedBuilderBar()
        {
            if (m_abBarWidget) return;

            VLOG(STR("[MoriaCppMod] [AB] === Creating Advanced Builder Toolbar ===\n"));


            UObject* texFrame = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            UObject* texToolsIcon = nullptr;
            {
                std::vector<UObject*> textures;
                findAllOfSafe(STR("Texture2D"), textures);
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                    else if (name == STR("Tools_Icon")) texToolsIcon = t;
                }
            }

            if (!texToolsIcon)
            {
                texToolsIcon = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Game/UI/textures/ClothingIcons/Tools_Icon.Tools_Icon"));
                if (texToolsIcon)
                    VLOG(STR("[MoriaCppMod] [AB] Tools_Icon found via StaticFindObject\n"));
                else
                    VLOG(STR("[MoriaCppMod] [AB] WARNING: Tools_Icon NOT found\n"));
            }
            if (!texFrame || !texActive)
            {
                showErrorBox(L"AB: textures not found!");
                return;
            }


            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* buttonClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            if (!userWidgetClass || !imageClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"AB: missing widget classes!");
                return;
            }


            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"AB: no PlayerController!"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showErrorBox(L"AB: WBL not found!"); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"AB: CreateWidget null!"); return; }


            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"AB: SetBrushFromTexture missing!"); return; }
            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;


            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) return;

            if (widgetTree)
                setRootWidget(widgetTree, outerBorder);

            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(outerBorder, setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    safeProcessEvent(outerBorder, setBorderPadFn, pp.data());
                }
            }


            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;


            // Wrap vbox in UButton for gamepad navigation, then set as border content
            UObject* borderContent = vbox;
            if (buttonClass)
            {
                FStaticConstructObjectParameters btnP(buttonClass, outer);
                UObject* btn = UObjectGlobals::StaticConstructObject(btnP);
                if (btn)
                {
                    float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    std::memcpy(reinterpret_cast<uint8_t*>(btn) + 0x03B0, transparent, 16);
                    *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(btn) + 0x03C3) = true;
                    auto* btnSetContentFn = btn->GetFunctionByNameInChain(STR("SetContent"));
                    if (btnSetContentFn)
                    {
                        struct { UObject* Content{nullptr}; UObject* Ret{nullptr}; } scp{};
                        scp.Content = vbox;
                        safeProcessEvent(btn, btnSetContentFn, &scp);
                    }
                    else
                        addChildToPanel(btn, STR("AddChild"), vbox);
                    m_abSlotButton = btn;
                    borderContent = btn;
                    VLOG(STR("[MoriaCppMod] [AB] Slot wrapped in UButton {:p}\n"), (void*)btn);
                }
            }

            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = borderContent;
                safeProcessEvent(outerBorder, setContentFn, sc.data());
            }


            FStaticConstructObjectParameters siP(imageClass, outer);
            UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
            FStaticConstructObjectParameters iiP(imageClass, outer);
            UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
            FStaticConstructObjectParameters fiP(imageClass, outer);
            UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
            if (!stateImg || !iconImg || !frameImg) return;


            m_abStateImage = stateImg;


            umgSetBrush(stateImg, texActive, setBrushFn);
            umgSetBrush(frameImg, texFrame, setBrushFn);
            umgSetOpacity(stateImg, 1.0f);
            umgSetOpacity(frameImg, 0.25f);


            if (texToolsIcon)
            {
                umgSetBrush(iconImg, texToolsIcon, setBrushFn);
                umgSetOpacity(iconImg, 1.0f);
                uint8_t* iBase = reinterpret_cast<uint8_t*>(iconImg);
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeX());
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeY());
                if (texW > 0.0f && texH > 0.0f)
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    float containerW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
                    float containerH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());
                    if (containerW < 1.0f) containerW = 64.0f;
                    if (containerH < 1.0f) containerH = 64.0f;
                    float scaleX = containerW / texW;
                    float scaleY = containerH / texH;
                    float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.75f;
                    umgSetBrushSize(iconImg, texW * scale, texH * scale);
                }
            }
            else
            {
                umgSetOpacity(iconImg, 0.0f);
            }


            uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
            float frameW = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeX());
            float frameH = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeY());
            uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
            float stateW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
            float stateH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());


            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
            if (!overlay) return;

            auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
            if (addToOverlayFn)
            {
                auto* pC = findParam(addToOverlayFn, STR("Content"));
                auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));


                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                    safeProcessEvent(overlay, addToOverlayFn, ap.data());
                    UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (stateOlSlot)
                    {
                        auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                        if (setHAFn)
                        {
                            int sz2 = setHAFn->GetParmsSize();
                            std::vector<uint8_t> hb(sz2, 0);
                            auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                            if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                            safeProcessEvent(stateOlSlot, setHAFn, hb.data());
                        }
                        auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            safeProcessEvent(stateOlSlot, setVAFn, vb.data());
                        }
                    }
                }

                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                    safeProcessEvent(overlay, addToOverlayFn, ap.data());
                    UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (iconSlot)
                    {
                        auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                        if (setHAFn)
                        {
                            int sz2 = setHAFn->GetParmsSize();
                            std::vector<uint8_t> hb(sz2, 0);
                            auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                            if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2;
                            safeProcessEvent(iconSlot, setHAFn, hb.data());
                        }
                        auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            safeProcessEvent(iconSlot, setVAFn, vb.data());
                        }
                    }
                }
            }


            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (addToVBoxFn)
            {
                auto* pC = findParam(addToVBoxFn, STR("Content"));
                auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));


                {
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                    safeProcessEvent(vbox, addToVBoxFn, ap.data());
                    UObject* olSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (olSlot)
                    {
                        umgSetSlotSize(olSlot, 1.0f, 0);
                        umgSetHAlign(olSlot, 2);
                    }
                }


                {
                    FStaticConstructObjectParameters foP(overlayClass, outer);
                    UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                    if (frameOverlay)
                    {
                        auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                        if (addToFoFn)
                        {
                            auto* foC = findParam(addToFoFn, STR("Content"));
                            auto* foR = findParam(addToFoFn, STR("ReturnValue"));


                            {
                                int sz2 = addToFoFn->GetParmsSize();
                                std::vector<uint8_t> ap2(sz2, 0);
                                if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                            }


                            if (texBlankRect)
                            {
                                FStaticConstructObjectParameters kbP(imageClass, outer);
                                UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                if (keyBgImg && setBrushFn)
                                {
                                    umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                    umgSetOpacity(keyBgImg, 0.8f);
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyBgImg;
                                    safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                                    UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (kbSlot)
                                    {
                                        auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(kbSlot, setHA, h.data()); }
                                        auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(kbSlot, setVA, v.data()); }
                                    }
                                }
                            }


                            if (textBlockClass)
                            {
                                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                if (keyLabel)
                                {
                                    std::wstring kn = keyName(s_bindings[BIND_AB_OPEN].key);
                                    umgSetText(keyLabel, kn);
                                    umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = keyLabel;
                                    safeProcessEvent(frameOverlay, addToFoFn, ap2.data());
                                    UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (tlSlot)
                                    {
                                        auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(tlSlot, setHA, h.data()); }
                                        auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; safeProcessEvent(tlSlot, setVA, v.data()); }
                                    }
                                    m_abKeyLabel = keyLabel;
                                }
                            }
                        }
                    }


                    UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                    safeProcessEvent(vbox, addToVBoxFn, ap.data());
                    UObject* fSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (fSlot)
                    {
                        umgSetSlotSize(fSlot, 1.0f, 0);
                        umgSetHAlign(fSlot, 2);
                        float overlapPx = stateH * 0.15f;
                        umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                    }
                }
            }


            m_screen.refresh(findPlayerController());


            float abScale = 0.81f;
            umgSetRenderScale(outerBorder, abScale, abScale);

            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;
            float abTotalW = iconW * abScale;
            float abTotalH = (frameH + stateH - vOverlap) * abScale;

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = abTotalW; v[1] = abTotalH;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            {
                float fracX = (m_toolbarPosX[1] >= 0) ? m_toolbarPosX[1] : TB_DEF_X[1];
                float fracY = (m_toolbarPosY[1] >= 0) ? m_toolbarPosY[1] : TB_DEF_Y[1];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }


            m_toolbarSizeW[1] = m_screen.slateToFracX(abTotalW);
            m_toolbarSizeH[1] = m_screen.slateToFracY(abTotalH);
            m_abBarWidget = userWidget;

            // Gamepad focus is NOT set at creation — it would steal focus from game UI
            // (e.g., player selection screen). Focus will be set on-demand when gamepad input is detected.

            showOnScreen(Loc::get("msg.ab_toolbar_created"), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [AB] === Advanced Builder toolbar created ({}x{}) ===\n"),
                                            abTotalW, abTotalH);
        }


        void destroyTargetInfoWidget()
        {
            if (!m_targetInfoWidget) return;
            deferRemoveWidget(m_targetInfoWidget);
            m_targetInfoWidget = nullptr;
            m_tiTitleLabel = nullptr;
            m_tiClassLabel = nullptr;
            m_tiNameLabel = nullptr;
            m_tiDisplayLabel = nullptr;
            m_tiPathLabel = nullptr;
            m_tiBuildLabel = nullptr;
            m_tiRecipeLabel = nullptr;
            m_tiShowTick = 0;
        }

        void createTargetInfoWidget()
        {
            if (m_targetInfoWidget) return;
            VLOG(STR("[MoriaCppMod] [TI] === Creating Target Info UMG widget ===\n"));


            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            if (!userWidgetClass || !vboxClass || !borderClass || !textBlockClass) return;

            auto* pc = findPlayerController();
            if (!pc) return;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;


            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            UObject* rootSizeBox = nullptr;
            if (sizeBoxClass)
            {
                FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
                rootSizeBox = UObjectGlobals::StaticConstructObject(sbP);
                if (rootSizeBox)
                {
                    if (widgetTree)
                        setRootWidget(widgetTree, rootSizeBox);

                    auto* setWFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                    if (setWFn) { int sz = setWFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 820.0f; safeProcessEvent(rootSizeBox, setWFn, wp.data()); }

                }
            }


            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;

            if (rootSizeBox)
            {
                auto* setContentFn2 = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                if (setContentFn2)
                {
                    auto* pC = findParam(setContentFn2, STR("Content"));
                    int sz = setContentFn2->GetParmsSize();
                    std::vector<uint8_t> sc(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootBorder;
                    safeProcessEvent(rootSizeBox, setContentFn2, sc.data());
                }
            }
            else if (widgetTree)
            {
                setRootWidget(widgetTree, rootBorder);
            }

            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(rootBorder, setBrushColorFn, cb.data());
                }
            }

            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 12.0f; m[1] = 8.0f; m[2] = 12.0f; m[3] = 8.0f;
                    safeProcessEvent(rootBorder, setBorderPadFn, pp.data());
                }
            }


            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                safeProcessEvent(rootBorder, setContentFn, sc.data());
            }


            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));
            auto* vbR = findParam(addToVBoxFn, STR("ReturnValue"));

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);

                auto* wrapAtFn = tb->GetFunctionByNameInChain(STR("SetWrapTextAt"));
                if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 790.0f; safeProcessEvent(tb, wrapAtFn, wp.data()); }
                auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
                if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; safeProcessEvent(tb, wrapFn, wp.data()); }
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                safeProcessEvent(vbox, addToVBoxFn, ap.data());
                return tb;
            };


            m_tiTitleLabel = makeTextBlock(Loc::get("ui.target_info_title"), 0.78f, 0.86f, 1.0f, 1.0f);

            makeTextBlock(L"--------------------------------", 0.31f, 0.51f, 0.78f, 0.5f);

            m_tiClassLabel   = makeTextBlock(Loc::get("ui.label_class"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiNameLabel    = makeTextBlock(Loc::get("ui.label_name"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiDisplayLabel = makeTextBlock(Loc::get("ui.label_display"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiPathLabel    = makeTextBlock(Loc::get("ui.label_path"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiBuildLabel   = makeTextBlock(Loc::get("ui.label_build"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiRecipeLabel  = makeTextBlock(Loc::get("ui.label_recipe"), 0.86f, 0.90f, 0.96f, 0.9f);


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 101;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }

            m_screen.refresh(findPlayerController());
            if (rootSizeBox) umgSetRenderScale(rootSizeBox, 1.0f, 1.0f);


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 840.0f; v[1] = 480.0f;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }


            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(userWidget, setVisFn, p); }

            m_targetInfoWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [TI] Target Info UMG widget created\n"));
        }

        void hideTargetInfo()
        {
            if (!m_targetInfoWidget) return;
            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(m_targetInfoWidget, fn, p); }
            m_tiShowTick = 0;
        }

        void showTargetInfoUMG(const std::wstring& name,
                               const std::wstring& display,
                               const std::wstring& path,
                               const std::wstring& cls,
                               bool buildable,
                               const std::wstring& recipe,
                               const std::wstring& rowName)
        {
            if (!m_targetInfoWidget) createTargetInfoWidget();
            if (!m_targetInfoWidget) return;


            umgSetText(m_tiClassLabel, wrapText(Loc::get("ui.value_class_prefix"), cls));
            umgSetText(m_tiNameLabel, wrapText(Loc::get("ui.value_name_prefix"), name));
            umgSetText(m_tiDisplayLabel, wrapText(Loc::get("ui.value_display_prefix"), display));
            umgSetText(m_tiPathLabel, wrapText(Loc::get("ui.value_path_prefix"), path));
            std::wstring buildStr = buildable ? Loc::get("ui.yes") : Loc::get("ui.no");
            umgSetText(m_tiBuildLabel, Loc::get("ui.value_build_prefix") + buildStr);
            if (buildable)
                umgSetTextColor(m_tiBuildLabel, 0.31f, 0.86f, 0.31f, 1.0f);
            else
                umgSetTextColor(m_tiBuildLabel, 0.7f, 0.55f, 0.39f, 0.8f);
            std::wstring recipeDisplay = !rowName.empty() ? rowName : recipe;
            umgSetText(m_tiRecipeLabel, recipeDisplay.empty() ? L"" : wrapText(Loc::get("ui.value_recipe_prefix"), recipeDisplay));


            {
                m_screen.refresh(findPlayerController());
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_targetInfoWidget, m_screen.fracToPixelX(fracX),
                                                      m_screen.fracToPixelY(fracY), true);
            }


            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; safeProcessEvent(m_targetInfoWidget, fn, p); }


            std::wstring copyText = L"Class: " + cls + L"\r\n" + L"Name: " + name + L"\r\n" +
                                    L"Display: " + display + L"\r\n" + L"Path: " + path + L"\r\n" +
                                    L"Buildable: " + buildStr;
            if (!recipeDisplay.empty()) copyText += L"\r\nRecipe: " + recipeDisplay;
            HWND hwnd = findGameWindow();
            if (OpenClipboard(hwnd))
            {
                EmptyClipboard();
                size_t sz = (copyText.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem)
                {
                    if (auto* ptr = GlobalLock(hMem))
                    {
                        memcpy(ptr, copyText.c_str(), sz);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                }
                CloseClipboard();
                VLOG(STR("[MoriaCppMod] Target info copied to clipboard\n"));
            }

            // v6.20.6 — Reverted inspect-notification add. The home-rolled
            // panel already shows the data the user wants; doubling up
            // with a transient notification was redundant and the
            // fallback (red box) appeared on top of the panel.

            m_tiShowTick = GetTickCount64();
        }


        // ── Crosshair reticle (centered, shown during inspect) ──────────

        void createCrosshair()
        {
            if (m_crosshairWidget) return;
            Output::send<LogLevel::Normal>(STR("[MoriaCppMod] [CH] createCrosshair() START\n"));

            // Exact same class/function lookup pattern as createErrorBox
            auto* imageClass      = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* borderClass     = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* createFn        = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass        = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!imageClass || !userWidgetClass || !borderClass || !createFn || !wblClass)
            { Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CH] Missing UMG classes\n")); return; }

            auto* pc = findPlayerController();
            if (!pc) { Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CH] No PC\n")); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            // Find texture
            UObject* texReticle = findTexture2DByName(L"T_UI_Bow_Reticle");
            if (!texReticle) texReticle = findTexture2DByName(L"T_UI_Btn_P1_Active");
            if (!texReticle) { Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CH] No texture\n")); return; }

            // Create UserWidget — SAME pattern as createErrorBox (with WorldContextObject)
            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { Output::send<LogLevel::Warning>(STR("[MoriaCppMod] [CH] CreateWidget null\n")); return; }
            m_crosshairWidget = userWidget;

            // Get WidgetTree — CRITICAL, same as createErrorBox
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Create transparent border as root (no background)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree) setRootWidget(widgetTree, rootBorder);

            // Transparent background
            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn) {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor) {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // fully transparent
                    safeProcessEvent(rootBorder, setBrushColorFn, cb.data());
                }
            }

            // Create image with reticle texture
            FStaticConstructObjectParameters imgP(imageClass, outer);
            UObject* img = UObjectGlobals::StaticConstructObject(imgP);
            if (!img) return;

            auto* setBrushFn = img->GetFunctionByNameInChain(STR("SetBrushFromTexture"));
            if (setBrushFn) umgSetBrushNoMatch(img, texReticle, setBrushFn);
            // Scale icon based on resolution — 128px at 1080p, scales up with uiScale
            float iconSize = 128.0f * m_screen.uiScale;
            umgSetBrushSize(img, iconSize, iconSize);
            // Bright saturated red so it's visible on any background
            umgSetImageColor(img, 1.0f, 0.0f, 0.0f, 1.0f);

            // Add image as content of the border
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn) {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = img;
                safeProcessEvent(rootBorder, setContentFn, sc.data());
            }

            // Add to viewport
            auto* addFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addFn) {
                int sz = addFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                auto* pZ = addFn->GetPropertyByNameInChain(STR("ZOrder"));
                if (pZ) *reinterpret_cast<int32_t*>(ap.data() + pZ->GetOffset_Internal()) = 100;
                safeProcessEvent(userWidget, addFn, ap.data());
            }

            // Position at exact center — same as showInfoMessage
            m_screen.refresh(pc);
            float halfIcon = iconSize / 2.0f;
            float cx = m_screen.fracToPixelX(0.5f) - halfIcon;
            float cy = m_screen.fracToPixelY(0.5f) - halfIcon;
            setWidgetPosition(m_crosshairWidget, cx, cy, true);

            // Start hidden
            auto* visFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (visFn) { uint8_t vp[8]{}; vp[0] = 1; safeProcessEvent(userWidget, visFn, vp); }

            Output::send<LogLevel::Normal>(STR("[MoriaCppMod] [CH] Crosshair OK at ({},{}) scale={}\n"), cx, cy, m_screen.viewportScale);
        }

        void showCrosshair()
        {
            if (!m_crosshairWidget) createCrosshair();
            if (!m_crosshairWidget) return;

            // Reposition to exact center, scaled for resolution
            UObject* pc = findPlayerController();
            if (pc) {
                m_screen.refresh(pc);
                float halfIcon = (64.0f * m_screen.uiScale) / 2.0f;
                float cx = m_screen.fracToPixelX(0.5f) - halfIcon;
                float cy = m_screen.fracToPixelY(0.5f) - halfIcon;
                setWidgetPosition(m_crosshairWidget, cx, cy, true);
            }

            auto* fn = m_crosshairWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; safeProcessEvent(m_crosshairWidget, fn, p); }
            m_crosshairShowTick = GetTickCount64();
        }

        void hideCrosshair()
        {
            if (!m_crosshairWidget) return;
            auto* fn = m_crosshairWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(m_crosshairWidget, fn, p); }
            m_crosshairShowTick = 0;
        }

        // ── Error Box ────────────────────────────────────────────────────

        void destroyErrorBox()
        {
            if (!m_errorBoxWidget) return;
            deferRemoveWidget(m_errorBoxWidget);
            m_errorBoxWidget = nullptr;
            m_ebMessageLabel = nullptr;
            m_ebShowTick = 0;
        }

        void createErrorBox()
        {
            if (m_errorBoxWidget) return;
            VLOG(STR("[MoriaCppMod] [EB] === Creating Error Box UMG widget ===\n"));

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !vboxClass || !borderClass || !textBlockClass) return;

            auto* pc = findPlayerController();
            if (!pc) return;
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) return;
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootBorder);

            // v6.20.13 — Restyled from white-on-red error look to game-
            // native dark-transparent panel with gold text. The
            // UI_WBP_NotificationFeed approach didn't work — the BP
            // renders item/recipe/lore-specific layouts, not arbitrary
            // text — so we abandon that entirely and just make this
            // home-rolled box look like the game's notifications.
            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    // Dark transparent background ~ near-black, 80% opaque.
                    c[0] = 0.05f; c[1] = 0.05f; c[2] = 0.07f; c[3] = 0.82f;
                    safeProcessEvent(rootBorder, setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    // More generous padding to feel like a notification panel.
                    m[0] = 20.0f; m[1] = 12.0f; m[2] = 20.0f; m[3] = 12.0f;
                    safeProcessEvent(rootBorder, setBorderPadFn, pp.data());
                }
            }


            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                safeProcessEvent(rootBorder, setContentFn, sc.data());
            }

            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));


            FStaticConstructObjectParameters tbP(textBlockClass, outer);
            UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
            if (!tb) return;
            umgSetText(tb, L"");
            // v6.20.13 — game-gold text (matches the warm cream color used
            // in the game's UI for highlights / important values).
            umgSetTextColor(tb, 1.0f, 0.82f, 0.45f, 1.0f);
            auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
            if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; safeProcessEvent(tb, wrapFn, wp.data()); }
            auto* wrapAtFn = tb->GetFunctionByNameInChain(STR("SetWrapTextAt"));
            if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 380.0f; safeProcessEvent(tb, wrapAtFn, wp.data()); }
            int sz = addToVBoxFn->GetParmsSize();
            std::vector<uint8_t> ap(sz, 0);
            if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
            safeProcessEvent(vbox, addToVBoxFn, ap.data());
            m_ebMessageLabel = tb;


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int vsz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(vsz, 0);
                // v6.20.6 — raised from 110 to 800 so the error-box (used
                // as the fallback when UI_WBP_NotificationFeed isn't found)
                // appears ABOVE the pause menu blur (~ZOrder 100-200).
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 800;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }

            m_screen.refresh(findPlayerController());
            float uiScale = m_screen.uiScale;

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int ssz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(ssz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 420.0f * uiScale; v[1] = 60.0f * uiScale;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }

            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int asz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(asz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.0f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }


            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(userWidget, setVisFn, p); }

            m_errorBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [EB] Error Box UMG widget created\n"));
        }

        void hideErrorBox()
        {
            if (!m_errorBoxWidget || !isObjectAlive(m_errorBoxWidget)) { m_errorBoxWidget = nullptr; return; }
            auto* fn = m_errorBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; safeProcessEvent(m_errorBoxWidget, fn, p); }
            m_ebShowTick = 0;
        }

        // Show a message in the info box widget (always visible, auto-hides)
        void showInfoMessage(const std::wstring& message)
        {
            if (!m_errorBoxWidget || !isObjectAlive(m_errorBoxWidget))
            {
                m_errorBoxWidget = nullptr;
                createErrorBox();
            }
            if (!m_errorBoxWidget) return;

            umgSetText(m_ebMessageLabel, message);

            {
                m_screen.refresh(findPlayerController());
                // Re-check widget liveness after findPlayerController (which
                // can trigger GC during world unload, leaving m_errorBoxWidget
                // alive but with a corrupted class pointer).
                if (!m_errorBoxWidget || !isObjectAlive(m_errorBoxWidget)) {
                    m_errorBoxWidget = nullptr;
                    return;
                }
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                try {
                    setWidgetPosition(m_errorBoxWidget, m_screen.fracToPixelX(fracX),
                                                        m_screen.fracToPixelY(fracY), true);
                } catch (...) {
                    m_errorBoxWidget = nullptr;
                    return;
                }
            }

            if (!m_errorBoxWidget || !isObjectAlive(m_errorBoxWidget)) {
                m_errorBoxWidget = nullptr;
                return;
            }
            UFunction* fn = nullptr;
            try { fn = m_errorBoxWidget->GetFunctionByNameInChain(STR("SetVisibility")); } catch (...) {}
            if (fn) { uint8_t p[8]{}; p[0] = 0; safeProcessEvent(m_errorBoxWidget, fn, p); }

            m_ebShowTick = GetTickCount64();
            VLOG(STR("[MoriaCppMod] [EB] showInfoMessage: '{}'\n"), message);
        }

        void showErrorBox(const std::wstring& message)
        {
            if (!s_verbose) return;
            showInfoMessage(message);
        }


        void destroyModControllerBar()
        {
            if (!m_mcBarWidget) return;
            deferRemoveWidget(m_mcBarWidget);
            m_mcBarWidget = nullptr;
            for (int i = 0; i < MC_SLOTS; i++)
            {
                m_mcSlotButtons[i] = nullptr;
                m_mcStateImages[i] = nullptr;
                m_mcIconImages[i] = nullptr;
                m_mcSlotStates[i] = UmgSlotState::Empty;
                m_mcKeyLabels[i] = nullptr;
                m_mcKeyBgImages[i] = nullptr;
            }
            m_mcFocusedSlot = -1;
            m_mcRotationLabel = nullptr;
            m_mcSlot0Overlay = nullptr;
            m_mcSlot6Overlay = nullptr;
            m_mcSlot8Overlay = nullptr;
            VLOG(STR("[MoriaCppMod] [MC] Mod Controller bar removed\n"));
        }

        void createModControllerBar()
        {
            if (m_mcBarWidget)
            {
                destroyModControllerBar();
                showOnScreen(Loc::get("msg.mc_removed").c_str(), 2.0f, 1.0f, 1.0f, 0.0f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [MC] === Creating 3x3 Mod Controller toolbar ===\n"));


            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            UObject* texRotation = nullptr;
            UObject* texTarget = nullptr;
            UObject* texRemoveTarget = nullptr;
            UObject* texUndoLast = nullptr;
            UObject* texRemoveAll = nullptr;
            UObject* texSettings = nullptr;
            UObject* texSnapToggle = nullptr;
            UObject* texStability = nullptr;
            UObject* texHideChar = nullptr;
            {
                std::vector<UObject*> textures;
                findAllOfSafe(STR("Texture2D"), textures);
                for (auto* t : textures)
                {
                    if (!t) continue;
                    auto name = t->GetName();
                    if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) texFrame = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Empty")) texEmpty = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Disabled")) texInactive = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) texActive = t;
                    else if (name == STR("T_UI_Icon_Input_Blank_Rect")) texBlankRect = t;
                    else if (name == STR("T_UI_Refresh")) texRotation = t;
                    else if (name == STR("T_UI_Search")) texTarget = t;
                    else if (name == STR("T_UI_Icon_GoodPlace2")) texRemoveTarget = t;
                    else if (name == STR("T_UI_Alert_BakedIcon")) texUndoLast = t;
                    else if (name == STR("T_UI_Icon_Filled_GoodPlace2")) texRemoveAll = t;
                    else if (name == STR("T_UI_Icon_Settings")) texSettings = t;
                    else if (name == STR("T_UI_Icon_Craft")) texStability = t;
                    else if (name == STR("T_UI_Eye_Open")) texHideChar = t;
                    else if (name == STR("T_UI_Icon_Build")) texSnapToggle = t;
                }
            }
            if (!texFrame || !texEmpty)
            {
                showErrorBox(L"MC: textures not found!");
                return;
            }
            if (!m_umgTexBlankRect && texBlankRect) m_umgTexBlankRect = texBlankRect;
            if (!m_umgTexInactive && texInactive) m_umgTexInactive = texInactive;
            if (!m_umgTexActive && texActive) m_umgTexActive = texActive;


            struct TexFallback { UObject*& ref; const TCHAR* path; const wchar_t* name; };
            TexFallback fallbacks[] = {
                {texRemoveTarget, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Icon_GoodPlace2.T_UI_Icon_GoodPlace2"), L"T_UI_Icon_GoodPlace2"},
                {texUndoLast, STR("/Game/UI/textures/_Shared/Icons/T_UI_Alert_BakedIcon.T_UI_Alert_BakedIcon"), L"T_UI_Alert_BakedIcon"},
                {texRemoveAll, STR("/Game/UI/textures/_Icons/Waypoints/FilledIcons/T_UI_Icon_Filled_GoodPlace2.T_UI_Icon_Filled_GoodPlace2"), L"T_UI_Icon_Filled_GoodPlace2"},
                {texSettings, STR("/Game/UI/textures/_Shared/Icons/T_UI_Icon_Settings.T_UI_Icon_Settings"), L"T_UI_Icon_Settings"},
                {texRotation, STR("/Game/UI/textures/_Shared/Icons/T_UI_Refresh.T_UI_Refresh"), L"T_UI_Refresh"},
                {texTarget, STR("/Game/UI/textures/_Icons/Menus/T_UI_Search.T_UI_Search"), L"T_UI_Search"},
                {texHideChar, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Eye_Open.T_UI_Eye_Open"), L"T_UI_Eye_Open"},
                {texStability, STR("/Game/UI/textures/_Shared/Icons/T_UI_Icon_Craft.T_UI_Icon_Craft"), L"T_UI_Icon_Craft"},
                {texSnapToggle, STR("/Game/UI/textures/_Shared/Icons/T_UI_Icon_Build.T_UI_Icon_Build"), L"T_UI_Icon_Build"},
            };
            for (auto& fb : fallbacks)
            {
                if (!fb.ref)
                {
                    fb.ref = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, fb.path);
                    if (fb.ref)
                        VLOG(STR("[MoriaCppMod] [MC] {} found via StaticFindObject fallback\n"), fb.name);
                    else
                        VLOG(STR("[MoriaCppMod] [MC] WARNING: {} NOT found via FindAllOf or StaticFindObject\n"), fb.name);
                }
            }


            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* buttonClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"MC: missing widget classes!");
                return;
            }


            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"MC: no PlayerController!"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showErrorBox(L"MC: WBL not found!"); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) return;

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"MC: CreateWidget null!"); return; }


            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"MC: SetBrushFromTexture missing!"); return; }

            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;


            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) return;

            if (widgetTree)
                setRootWidget(widgetTree, outerBorder);

            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f;
                    safeProcessEvent(outerBorder, setBrushColorFn, cb.data());
                }
            }
            auto* setBorderPadFn = outerBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    safeProcessEvent(outerBorder, setBorderPadFn, pp.data());
                }
            }


            FStaticConstructObjectParameters rootVBoxP(vboxClass, outer);
            UObject* rootVBox = UObjectGlobals::StaticConstructObject(rootVBoxP);
            if (!rootVBox) return;

            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = rootVBox;
                safeProcessEvent(outerBorder, setContentFn, sc.data());
            }


            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            int slotIdx = 0;
            for (int row = 0; row < 3; row++)
            {

                FStaticConstructObjectParameters hboxP(hboxClass, outer);
                UObject* hbox = UObjectGlobals::StaticConstructObject(hboxP);
                if (!hbox) continue;


                UObject* rowSlot = addToVBox(rootVBox, hbox);
                if (rowSlot)
                {
                    umgSetSlotSize(rowSlot, 1.0f, 0);
                }

                for (int col = 0; col < 3; col++)
                {
                    int i = slotIdx++;


                    FStaticConstructObjectParameters vboxP(vboxClass, outer);
                    UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                    if (!vbox) continue;


                    FStaticConstructObjectParameters siP(imageClass, outer);
                    UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                    if (!stateImg) continue;
                    FStaticConstructObjectParameters iiP(imageClass, outer);
                    UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                    if (!iconImg) continue;
                    FStaticConstructObjectParameters fiP(imageClass, outer);
                    UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                    if (!frameImg) continue;


                    FStaticConstructObjectParameters olP(overlayClass, outer);
                    UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                    if (!overlay) continue;


                    umgSetBrush(stateImg, texEmpty, setBrushFn);
                    umgSetBrush(frameImg, texFrame, setBrushFn);
                    umgSetOpacity(iconImg, 0.0f);


                    if (i == 0)
                    {
                        uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
                        frameW = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeX());
                        frameH = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeY());
                        uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                        stateW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
                        stateH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());
                        VLOG(STR("[MoriaCppMod] [MC] Frame: {}x{}, State: {}x{}\n"),
                                                        frameW, frameH, stateW, stateH);
                        m_mcSlot0Overlay = overlay;
                    }
                    if (i == 6) m_mcSlot6Overlay = overlay;
                    if (i == 8) m_mcSlot8Overlay = overlay;

                    umgSetOpacity(stateImg, 1.0f);
                    umgSetOpacity(frameImg, 0.25f);


                    UObject* stateOlSlot = addToOverlay(overlay, stateImg);
                    if (stateOlSlot)
                    {
                        umgSetHAlign(stateOlSlot, 2);
                        umgSetVAlign(stateOlSlot, 2);
                    }

                    UObject* iconSlot = addToOverlay(overlay, iconImg);
                    if (iconSlot)
                    {
                        umgSetHAlign(iconSlot, 2);
                        umgSetVAlign(iconSlot, 2);
                    }


                    UObject* olSlot = addToVBox(vbox, overlay);
                    if (olSlot)
                    {
                        umgSetSlotSize(olSlot, 1.0f, 0);
                        umgSetHAlign(olSlot, 2);
                    }

                    {
                        FStaticConstructObjectParameters foP(overlayClass, outer);
                        UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                        if (frameOverlay)
                        {

                            addToOverlay(frameOverlay, frameImg);


                            if (texBlankRect)
                            {
                                FStaticConstructObjectParameters kbP(imageClass, outer);
                                UObject* keyBgImg = UObjectGlobals::StaticConstructObject(kbP);
                                if (keyBgImg && setBrushFn)
                                {
                                    umgSetBrush(keyBgImg, texBlankRect, setBrushFn);
                                    umgSetOpacity(keyBgImg, 0.8f);

                                    UObject* kbSlot = addToOverlay(frameOverlay, keyBgImg);
                                    if (kbSlot)
                                    {
                                        umgSetHAlign(kbSlot, 2);
                                        umgSetVAlign(kbSlot, 2);
                                    }
                                    m_mcKeyBgImages[i] = keyBgImg;
                                }
                            }


                            if (textBlockClass)
                            {
                                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                                UObject* keyLabel = UObjectGlobals::StaticConstructObject(tbP);
                                if (keyLabel)
                                {
                                    std::wstring kn = keyName(s_bindings[MC_BIND_BASE + i].key);
                                    umgSetText(keyLabel, kn);
                                    umgSetTextColor(keyLabel, 1.0f, 1.0f, 1.0f, 1.0f);

                                    UObject* tlSlot = addToOverlay(frameOverlay, keyLabel);
                                    if (tlSlot)
                                    {
                                        umgSetHAlign(tlSlot, 2);
                                        umgSetVAlign(tlSlot, 2);
                                    }
                                    m_mcKeyLabels[i] = keyLabel;
                                }
                            }
                        }


                        UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                        UObject* fSlot = addToVBox(vbox, frameChild);
                        if (fSlot)
                        {
                            umgSetSlotSize(fSlot, 1.0f, 0);
                            umgSetHAlign(fSlot, 2);
                            float overlapPx = stateH * 0.25f;
                            umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                        }
                    }


                    // Wrap slot VBox in UButton for gamepad navigation + native click
                    UObject* slotWidget = vbox;  // fallback if no button class
                    if (buttonClass)
                    {
                        FStaticConstructObjectParameters btnP(buttonClass, outer);
                        UObject* btn = UObjectGlobals::StaticConstructObject(btnP);
                        if (btn)
                        {
                            // Make button transparent — only the slot content shows
                            // BackgroundColor at offset 0x03B0 (FLinearColor, 16 bytes)
                            float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                            std::memcpy(reinterpret_cast<uint8_t*>(btn) + 0x03B0, transparent, 16);
                            // IsFocusable at offset 0x03C3 — default true, but ensure it
                            *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(btn) + 0x03C3) = true;

                            // Add vbox as button content via UContentWidget::SetContent
                            auto* setContentFn = btn->GetFunctionByNameInChain(STR("SetContent"));
                            if (setContentFn)
                            {
                                struct { UObject* Content{nullptr}; UObject* Ret{nullptr}; } scp{};
                                scp.Content = vbox;
                                safeProcessEvent(btn, setContentFn, &scp);
                            }
                            else
                            {
                                // Fallback: AddChild from UPanelWidget
                                addChildToPanel(btn, STR("AddChild"), vbox);
                            }

                            m_mcSlotButtons[i] = btn;
                            slotWidget = btn;
                            VLOG(STR("[MoriaCppMod] [MC] Slot {} wrapped in UButton {:p}\n"), i, (void*)btn);
                        }
                    }

                    UObject* hSlot = addToHBox(hbox, slotWidget);
                    if (hSlot)
                    {
                        umgSetSlotSize(hSlot, 1.0f, 1);
                        umgSetVAlign(hSlot, 0);
                        float colW2 = (frameW > stateW) ? frameW : stateW;
                        float hOverlap = colW2 * 0.10f;
                        umgSetSlotPadding(hSlot, -hOverlap, 0.0f, -hOverlap, 0.0f);
                    }

                    m_mcStateImages[i] = stateImg;
                    m_mcIconImages[i] = iconImg;
                    m_mcSlotStates[i] = UmgSlotState::Empty;
                }
            }
            VLOG(STR("[MoriaCppMod] [MC] All 9 slots created (3x3)\n"));


            {
                UObject* mcSlotTextures[MC_SLOTS] = {
                    texRotation, texSnapToggle, texStability,
                    texHideChar, texTarget, texSettings,
                    texRemoveTarget, texUndoLast, texRemoveAll
                };
                const wchar_t* mcSlotNames[MC_SLOTS] = {
                    L"T_UI_Refresh", L"T_UI_Icon_Build", L"T_UI_Icon_Craft",
                    L"T_UI_Eye_Open", L"T_UI_Search", L"T_UI_Icon_Settings",
                    L"T_UI_Icon_GoodPlace2", L"T_UI_Alert_BakedIcon", L"T_UI_Icon_Filled_GoodPlace2"
                };
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (!mcSlotTextures[i]) continue;
                    if (m_mcIconImages[i] && setBrushFn)
                    {
                        umgSetBrush(m_mcIconImages[i], mcSlotTextures[i], setBrushFn);
                        umgSetOpacity(m_mcIconImages[i], 1.0f);

                        if (m_mcStateImages[i] && texActive)
                            umgSetBrush(m_mcStateImages[i], texActive, setBrushFn);

                        uint8_t* iBase = reinterpret_cast<uint8_t*>(m_mcIconImages[i]);
                        float texW = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeX());
                        float texH = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeY());
                        if (texW > 0.0f && texH > 0.0f)
                        {

                            float containerW = 64.0f, containerH = 64.0f;
                            if (m_mcStateImages[i])
                            {
                                uint8_t* sBase = reinterpret_cast<uint8_t*>(m_mcStateImages[i]);
                                containerW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
                                containerH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());
                            }
                            if (containerW < 1.0f) containerW = 64.0f;
                            if (containerH < 1.0f) containerH = 64.0f;
                            float scaleX = containerW / texW;
                            float scaleY = containerH / texH;
                            float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.70f;
                            umgSetBrushSize(m_mcIconImages[i], texW * scale, texH * scale);
                        }
                        VLOG(STR("[MoriaCppMod] [MC] Slot {} icon set to {}\n"), i, mcSlotNames[i]);
                    }
                    else
                    {
                        VLOG(STR("[MoriaCppMod] [MC] WARNING: {} texture not found for slot {}\n"), mcSlotNames[i], i);
                    }
                }
            }


            if (m_mcSlot0Overlay && textBlockClass)
            {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* rotLabel = UObjectGlobals::StaticConstructObject(tbP);
                if (rotLabel)
                {
                    int step = s_overlay.rotationStep;
                    int total = s_overlay.totalRotation;
                    std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
                    umgSetText(rotLabel, txt);
                    umgSetTextColor(rotLabel, 0.4f, 0.6f, 1.0f, 1.0f);

                    UObject* rotSlot = addToOverlay(m_mcSlot0Overlay, rotLabel);
                    if (rotSlot)
                    {
                        umgSetHAlign(rotSlot, 2);
                        umgSetVAlign(rotSlot, 2);
                    }
                    m_mcRotationLabel = rotLabel;
                    VLOG(STR("[MoriaCppMod] [MC] Rotation label created on slot 0\n"));
                }
            }


            if (m_mcSlot6Overlay && textBlockClass)
            {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* singleLabel = UObjectGlobals::StaticConstructObject(tbP);
                if (singleLabel)
                {
                    umgSetText(singleLabel, Loc::get("ui.label_single"));
                    umgSetTextColor(singleLabel, 0.85f, 0.05f, 0.05f, 1.0f);
                    umgSetFontSize(singleLabel, 31);

                    UObject* labelSlot = addToOverlay(m_mcSlot6Overlay, singleLabel);
                    if (labelSlot)
                    {
                        umgSetHAlign(labelSlot, 2);
                        umgSetVAlign(labelSlot, 2);
                    }
                    VLOG(STR("[MoriaCppMod] [MC] 'Single' label created on slot 6\n"));
                }
            }


            if (m_mcSlot8Overlay && textBlockClass)
            {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* allLabel = UObjectGlobals::StaticConstructObject(tbP);
                if (allLabel)
                {
                    umgSetText(allLabel, Loc::get("ui.label_all"));
                    umgSetTextColor(allLabel, 0.85f, 0.05f, 0.05f, 1.0f);
                    umgSetFontSize(allLabel, 31);

                    UObject* labelSlot = addToOverlay(m_mcSlot8Overlay, allLabel);
                    if (labelSlot)
                    {
                        umgSetHAlign(labelSlot, 2);
                        umgSetVAlign(labelSlot, 2);
                    }
                    VLOG(STR("[MoriaCppMod] [MC] 'All' label created on slot 8\n"));
                }
            }

            m_screen.refresh(findPlayerController());
            float mcScale = 0.81f;
            umgSetRenderScale(outerBorder, mcScale, mcScale);

            float mcIconW = (frameW > stateW) ? frameW : stateW;
            if (mcIconW < 1.0f) mcIconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float mcVOverlap = stateH * 0.25f;
            float mcHOverlapPerSlot = mcIconW * 0.20f;
            float mcTotalW = (3.0f * mcIconW - 2.0f * mcHOverlapPerSlot) * mcScale * 1.2f;
            float mcSlotH = (frameH + stateH - mcVOverlap);
            float mcTotalH = (3.0f * mcSlotH) * mcScale;

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = mcTotalW; v[1] = mcTotalH;
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            {
                float fracX = (m_toolbarPosX[2] >= 0) ? m_toolbarPosX[2] : TB_DEF_X[2];
                float fracY = (m_toolbarPosY[2] >= 0) ? m_toolbarPosY[2] : TB_DEF_Y[2];
                float setPosX = m_screen.fracToPixelX(fracX);
                float setPosY = m_screen.fracToPixelY(fracY);
                setWidgetPosition(userWidget, setPosX, setPosY, true);
            }


            m_toolbarSizeW[2] = m_screen.slateToFracX(mcTotalW);
            m_toolbarSizeH[2] = m_screen.slateToFracY(mcTotalH);
            m_mcBarWidget = userWidget;


            setMcSlotState(1, UmgSlotState::Disabled);

            // Gamepad focus is set on the AB toolbar button (always visible, toggles other toolbars)

            showOnScreen(Loc::get("msg.mod_controller_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [MC] === Mod Controller bar creation complete ({}x{}) ===\n"),
                                            mcTotalW, mcTotalH);
        }

        // ─────────────────────────────────────────────────────────────────
        // v6.10.0 — "New Building Bar"
        //
        // Spawns one extra UWBP_UI_ActionBar_C instance, anchored to the
        // top-center of the HUD, and tames it so that:
        //   - all 4 special slots (Epic / HeavyCarry / MainHand / Offhand)
        //     and their slot markers + decorative frames are Collapsed,
        //   - all inventory wiring is neutralised by NULLing out
        //     InventoryComponent / equipmentComponent / MorCharacter on
        //     OUR instance only, so the BP's Tick / OnInvChanged paths
        //     short-circuit on null instead of trying to read inventory.
        //
        // This is Phase 1 — chrome only, no function. Phase 2 will:
        //   - layer our 8 builder-slot icons over slotMarker1..8
        //   - layer our F# key labels on top
        //   - route clicks/keypresses to our existing dispatch
        //
        // The native HUD ActionBar is untouched — our pointer is a
        // separate UObject of the same class.
        // ─────────────────────────────────────────────────────────────────
        void destroyNewBuildingBar()
        {
            if (!m_newBuildingBar) return;
            deferRemoveWidget(m_newBuildingBar);
            m_newBuildingBar = nullptr;
            for (int i = 0; i < 8; ++i)
            {
                m_nbbSlotEmpty[i]  = nullptr;
                m_nbbSlotFocus[i]  = nullptr;
                m_nbbSlotIcon[i]   = nullptr;
                m_nbbSlotKeyLbl[i] = nullptr;
                m_nbbSlotMarker[i] = nullptr;
                m_nbbSlotButton[i] = nullptr;
            }
            VLOG(STR("[MoriaCppMod] [NewBuildingBar] removed\n"));
        }

        // v6.10.0 — Highlight/un-highlight a slot by toggling its
        // "focused" overlay's Visibility. Phase 2 wires this to clicks +
        // F# key presses. Slot range 0..7.
        void newBuildingBarHighlight(int slot, bool on)
        {
            if (slot < 0 || slot >= 8) return;
            UObject* fx = m_nbbSlotFocus[slot];
            if (!fx || !isObjectAlive(fx)) return;
            if (auto* visPtr = fx->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                *visPtr = on ? 0 : 1; // 0=Visible, 1=Collapsed
            setWidgetVisibility(fx, on ? 0 : 1);
        }

        // v0.10 — Populate each slot icon from m_recipeSlots[i].textureName
        // — the QuickBuild assignments loaded from the INI. Walks the
        // global Texture2D set once, then SetBrushFromTexture on each
        // slot icon image. Slots without an assignment stay invisible.
        void populateNewBuildingBarIcons()
        {
            if (!m_newBuildingBar || !isObjectAlive(m_newBuildingBar)) return;
            UFunction* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) return;

            // Resolve all needed textures by name in one pass.
            std::vector<UObject*> textures;
            try { findAllOfSafe(STR("Texture2D"), textures); } catch (...) {}
            UObject* slotTex[QUICK_BUILD_SLOTS]{};
            int filled = 0;
            for (int i = 0; i < QUICK_BUILD_SLOTS; ++i)
            {
                if (!m_recipeSlots[i].used || m_recipeSlots[i].textureName.empty()) continue;
                const std::wstring& want = m_recipeSlots[i].textureName;
                for (auto* t : textures) {
                    if (!t) continue;
                    if (t->GetName() == want) { slotTex[i] = t; break; }
                }
            }

            // Apply to each slot icon image.
            for (int i = 0; i < 8; ++i)
            {
                UObject* iImg = m_nbbSlotIcon[i];
                if (!iImg || !isObjectAlive(iImg)) continue;
                if (!slotTex[i])
                {
                    // No texture assigned — keep the slot empty.
                    if (auto* visPtr = iImg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                        *visPtr = 1;
                    setWidgetVisibility(iImg, 1);
                    continue;
                }
                umgSetBrush(iImg, slotTex[i], setBrushFn);
                if (s_off_brush >= 0) {
                    uint8_t* base = reinterpret_cast<uint8_t*>(iImg);
                    *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX()) = 128.0f;
                    *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY()) = 128.0f;
                }
                if (auto* visPtr = iImg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                    *visPtr = 0; // Visible
                setWidgetVisibility(iImg, 0);
                ++filled;
            }
            VLOG(STR("[NewBuildingBar] populated {} slot icons from m_recipeSlots\n"), filled);
        }

        // v6.10.0 — Re-read s_bindings[0..7].key and update each slot's
        // F# label. Call this after the user rebinds a QuickBuild key
        // in Settings → Key Mapping so the bar reflects the new chord.
        void refreshNewBuildingBarKeyLabels()
        {
            if (!m_newBuildingBar || !isObjectAlive(m_newBuildingBar)) return;
            for (int i = 0; i < 8; ++i)
            {
                UObject* tb = m_nbbSlotKeyLbl[i];
                if (!tb || !isObjectAlive(tb)) continue;
                std::wstring kn = keyName(s_bindings[i].key);
                umgSetText(tb, kn);
            }
        }

        // ─────────────────────────────────────────────────────────────────
        // Discover-and-replicate (v0.2 of the New Building Bar).
        //
        // The "spawn an ActionBar and tame it" approach failed because the
        // BP keeps re-asserting its Tick / Construct logic over our
        // overrides — the Epic slot reappears, the placeholder "99" comes
        // back, etc. Instead we now:
        //
        //   1. Spawn a hidden ActionBar instance ONLY to read its widget
        //      tree at runtime. Discover the texture pointers and brush
        //      ImageSize from each named UImage member.
        //   2. Destroy the discovery spawn (deferRemoveWidget).
        //   3. Build a brand-new UMG widget tree from scratch using those
        //      captured textures, with our own layout. The BP never gets
        //      to touch this — it's a plain UUserWidget with primitives.
        //
        // This is the same capture-and-replay pattern documented in the
        // ue4-ui-duplication skill (used for the JoinWorld UI clone).
        // ─────────────────────────────────────────────────────────────────
        struct NbbDiscoveredAssets {
            UObject* texTopFrameLeft{nullptr};
            UObject* texTopFrameRight{nullptr};
            UObject* texMiddleFrame{nullptr};
            UObject* texBottomLeft{nullptr};
            UObject* texBottomRight{nullptr};
            UObject* texSlotMarker[8]{};
            float sizeTopL[2]{0,0}, sizeTopR[2]{0,0}, sizeMid[2]{0,0};
            float sizeBotL[2]{0,0}, sizeBotR[2]{0,0};
            float sizeMarker[2]{32, 16};
            // Slot frame textures — pulled from the well-known names that
            // the existing MC bar already locates by FindAllOf.
            UObject* texSlotEmpty{nullptr};
            UObject* texSlotFocus{nullptr};
            UObject* texSlotDisabled{nullptr};
            UObject* texSlotFrame{nullptr};
            float sizeSlot[2]{96, 96};
            // v0.4 — proper slot textures captured from the slot widget
            // (UI_WBP_Inventory_ActionBar_Item_1 → nestedInventoryItem,
            // a UUI_WBP_Inventory_Item_AB_C with emptyFullSlot /
            // buttonFocused / FocusedCorners UImages). Way better visual
            // match than the EpicAB textures we used as a stopgap.
            UObject* texEmptyFullSlot{nullptr};
            UObject* texButtonFocused{nullptr};
            UObject* texFocusedCorners{nullptr};
            float sizeEmptyFullSlot[2]{0,0};
            float sizeButtonFocused[2]{0,0};
            float sizeFocusedCorners[2]{0,0};
        };

        // v0.8 — Cached fast-path: if we already discovered textures
        // earlier in this session, just hand back the cache. No re-scan,
        // no re-spawn. Bar construction skips straight to layout.
        bool nbbDiscoverAssetsCached(NbbDiscoveredAssets& out)
        {
            if (!m_nbbAssetsCached) return false;
            out.texEmptyFullSlot   = m_nbbCachedSlotEmpty;
            out.texButtonFocused   = m_nbbCachedSlotFocus;
            out.texFocusedCorners  = m_nbbCachedSlotCorners;
            out.texSlotFrame       = m_nbbCachedBarFrame;
            out.texSlotEmpty       = m_nbbCachedSlotEmpty; // alias
            out.texSlotFocus       = m_nbbCachedSlotFocus; // alias
            VLOG(STR("[NewBuildingBar] discover: using CACHED textures (no scan)\n"));
            return true;
        }

        bool nbbDiscoverAssets(NbbDiscoveredAssets& out)
        {
            if (nbbDiscoverAssetsCached(out)) return true;

            // v0.7 — Read from a LIVE native HUD ActionBar instance
            // (preferred), falling back to a fresh spawn if no live
            // instance exists yet. The live HUD instance has its
            // textures wired; the fresh spawn does not — but we still
            // get layout + texture-by-name fallbacks from the global
            // FindAllOf<Texture2D> pass below.
            UObject* tmplt = nullptr;
            {
                std::vector<UObject*> instances;
                try { findAllOfSafe(STR("WBP_UI_ActionBar_C"), instances); } catch (...) {}
                VLOG(STR("[NewBuildingBar] discover: found {} ActionBar instances\n"),
                     (int)instances.size());
                // Prefer one that ISN'T the CDO and has middleFrame with
                // a wired texture; otherwise just pick the first live one.
                UObject* anyLive = nullptr;
                for (auto* w : instances) {
                    if (!w || !isObjectAlive(w)) continue;
                    // Skip CDO objects (their name starts with "Default__")
                    std::wstring nm; try { nm = w->GetName(); } catch (...) {}
                    if (nm.find(STR("Default__")) == 0) continue;
                    if (!anyLive) anyLive = w;
                    // Probe middleFrame for a wired brush texture.
                    auto* mp = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("middleFrame"));
                    UObject* mid = mp ? *mp : nullptr;
                    if (mid && isObjectAlive(mid) && s_off_brush >= 0) {
                        UObject* tex = *reinterpret_cast<UObject**>(
                            reinterpret_cast<uint8_t*>(mid) + s_off_brush + brushResourceObj());
                        if (tex) {
                            tmplt = w;
                            VLOG(STR("[NewBuildingBar] discover: using LIVE wired ActionBar {:p} '{}'\n"),
                                 (void*)w, nm.c_str());
                            break;
                        }
                    }
                }
                if (!tmplt && anyLive) {
                    tmplt = anyLive;
                    VLOG(STR("[NewBuildingBar] discover: no wired-texture instance — using {:p} (textures may be empty)\n"),
                         (void*)tmplt);
                }
            }
            // Last-resort fallback: spawn a fresh template if we couldn't
            // find any live instance (e.g. discover ran before the HUD
            // ActionBar was created).
            if (!tmplt) {
                UClass* abCls = nullptr;
                for (const wchar_t* path : {
                    STR("/Game/UI/HUD/ActionBar/WBP_UI_ActionBar.WBP_UI_ActionBar_C"),
                    STR("WBP_UI_ActionBar_C"),
                }) {
                    try { abCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path); }
                    catch (...) {}
                    if (abCls) break;
                }
                if (abCls) tmplt = jw_createGameWidget(abCls);
                if (tmplt)
                    VLOG(STR("[NewBuildingBar] discover: fallback fresh-spawn template {:p}\n"), (void*)tmplt);
                else {
                    VLOG(STR("[NewBuildingBar] discover: no live instance and fresh-spawn failed — aborting\n"));
                    return false;
                }
            }

            // Helper: read brush ResourceObject + ImageSize from a named
            // UImage member of `parent`.
            auto readImg = [&](UObject* parent, const wchar_t* name,
                               UObject*& outTex, float* outSize)
            {
                if (!parent) return;
                auto* pp = parent->GetValuePtrByPropertyNameInChain<UObject*>(name);
                UObject* img = pp ? *pp : nullptr;
                if (!img || !isObjectAlive(img)) return;
                if (s_off_brush < 0) return;
                uint8_t* base = reinterpret_cast<uint8_t*>(img);
                outTex = *reinterpret_cast<UObject**>(base + s_off_brush + brushResourceObj());
                outSize[0] = *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX());
                outSize[1] = *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY());
            };

            readImg(tmplt, STR("topFrameLeft"),       out.texTopFrameLeft,  out.sizeTopL);
            readImg(tmplt, STR("topFrameRight"),      out.texTopFrameRight, out.sizeTopR);
            readImg(tmplt, STR("middleFrame"),        out.texMiddleFrame,   out.sizeMid);
            readImg(tmplt, STR("bottomFrameRight"),   out.texBottomLeft,    out.sizeBotL);
            readImg(tmplt, STR("bottomFrameRight_1"), out.texBottomRight,   out.sizeBotR);

            for (int i = 0; i < 8; ++i)
            {
                wchar_t nm[32];
                swprintf_s(nm, L"slotMarker%d", i + 1);
                float sz[2]{0,0};
                readImg(tmplt, nm, out.texSlotMarker[i], sz);
                if (i == 0 && sz[0] > 0) { out.sizeMarker[0] = sz[0]; out.sizeMarker[1] = sz[1]; }
            }

            // Slot frame textures — find the well-known HUD textures by
            // name (these are the same textures the native ActionBar
            // uses internally). Also try to read a slot frame from one
            // of the spawned slot's internal images for ImageSize.
            {
                std::vector<UObject*> textures;
                try { findAllOfSafe(STR("Texture2D"), textures); } catch (...) {}
                for (auto* t : textures) {
                    if (!t) continue;
                    auto name = t->GetName();
                    if      (name == STR("T_UI_Btn_HUD_EpicAB_Empty"))    out.texSlotEmpty   = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Focused")) out.texSlotFocus    = t;
                    else if (name == STR("T_UI_Btn_HUD_EpicAB_Disabled")) out.texSlotDisabled = t;
                    else if (name == STR("T_UI_Frame_HUD_AB_Active_BothHands")) out.texSlotFrame = t;
                }
            }

            // v0.4 — Walk into one of the spawned numbered slot widgets
            // (UI_WBP_Inventory_ActionBar_Item_1 → nestedInventoryItem,
            // a UUI_WBP_Inventory_Item_AB_C). Capture the proper slot
            // textures from there (emptyFullSlot / buttonFocused /
            // FocusedCorners) — these are what the native ActionBar
            // actually renders, not the EpicAB textures.
            {
                auto* slot1Ptr = tmplt->GetValuePtrByPropertyNameInChain<UObject*>(STR("UI_WBP_Inventory_ActionBar_Item_1"));
                UObject* slot1 = slot1Ptr ? *slot1Ptr : nullptr;
                UObject* nested = nullptr;
                if (slot1 && isObjectAlive(slot1))
                {
                    auto* nPtr = slot1->GetValuePtrByPropertyNameInChain<UObject*>(STR("nestedInventoryItem"));
                    nested = nPtr ? *nPtr : nullptr;
                }
                if (nested && isObjectAlive(nested))
                {
                    readImg(nested, STR("emptyFullSlot"), out.texEmptyFullSlot, out.sizeEmptyFullSlot);
                    readImg(nested, STR("buttonFocused"), out.texButtonFocused, out.sizeButtonFocused);
                    readImg(nested, STR("FocusedCorners"), out.texFocusedCorners, out.sizeFocusedCorners);
                    VLOG(STR("[NewBuildingBar] discover: slot textures emptyFullSlot={:p}({},{}) buttonFocused={:p} FocusedCorners={:p}\n"),
                         (void*)out.texEmptyFullSlot, out.sizeEmptyFullSlot[0], out.sizeEmptyFullSlot[1],
                         (void*)out.texButtonFocused, (void*)out.texFocusedCorners);
                }
                else
                {
                    VLOG(STR("[NewBuildingBar] discover: nestedInventoryItem on slot 1 is null — slot textures will fall back\n"));
                }
            }

            // Destroy the discovery spawn — never enters viewport.
            // (jw_createGameWidget creates a UUserWidget that's not yet
            // added to viewport. Just clearing references should be
            // enough for GC. RemoveFromParent is a no-op when not added.)
            VLOG(STR("[NewBuildingBar] discover: captured TopL={:p} TopR={:p} Mid={:p} BotL={:p} BotR={:p} SlotEmpty={:p} SlotFocus={:p}\n"),
                 (void*)out.texTopFrameLeft, (void*)out.texTopFrameRight,
                 (void*)out.texMiddleFrame, (void*)out.texBottomLeft,
                 (void*)out.texBottomRight, (void*)out.texSlotEmpty,
                 (void*)out.texSlotFocus);

            // v0.5 — DEEP DIVE: walk the spawned ActionBar's WidgetTree
            // and log every UImage we see, with its texture name + brush
            // size + parent slot type + canvas-slot Position/Size if it
            // lives in a CanvasPanel. Lets us understand exactly how the
            // native chrome is composed without guessing.
            auto* wtPtr = tmplt->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* wt = wtPtr ? *wtPtr : nullptr;
            UObject* tRoot = nullptr;
            if (wt) {
                auto* rPtr = wt->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootWidget"));
                if (rPtr) tRoot = *rPtr;
            }
            std::function<void(UObject*, int)> dump = [&](UObject* w, int depth) {
                if (!w || !isObjectAlive(w)) return;
                std::wstring nm; try { nm = w->GetName(); } catch (...) {}
                std::wstring cls = safeClassName(w);
                std::wstring indent(depth * 2, L' ');

                // For UImage, log brush texture + size.
                if (cls == STR("Image"))
                {
                    UObject* tex = nullptr;
                    float sx = 0, sy = 0;
                    if (s_off_brush >= 0)
                    {
                        uint8_t* base = reinterpret_cast<uint8_t*>(w);
                        tex = *reinterpret_cast<UObject**>(base + s_off_brush + brushResourceObj());
                        sx  = *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX());
                        sy  = *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY());
                    }
                    std::wstring tn; if (tex) try { tn = tex->GetName(); } catch (...) {}
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    int vis = visPtr ? *visPtr : -1;
                    VLOG(STR("[NBB-DUMP] {}Image '{}' tex='{}' size=({},{}) vis={}\n"),
                         indent.c_str(), nm.c_str(), tn.c_str(), sx, sy, vis);
                }
                else
                {
                    auto* visPtr = w->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility"));
                    int vis = visPtr ? *visPtr : -1;
                    VLOG(STR("[NBB-DUMP] {}{} '{}' vis={}\n"),
                         indent.c_str(), cls.c_str(), nm.c_str(), vis);
                }

                // For CanvasPanelSlot children, log slot Position/Size.
                auto* slots = w->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Slots"));
                if (slots) {
                    for (int i = 0; i < slots->Num(); ++i) {
                        UObject* s = (*slots)[i];
                        if (!s) continue;
                        std::wstring scls = safeClassName(s);
                        if (scls == STR("CanvasPanelSlot"))
                        {
                            // CanvasPanelSlot has LayoutData (FAnchorData) — Offsets, Anchors, Alignment, Size
                            auto* lPtr = s->GetValuePtrByPropertyNameInChain<float>(STR("LayoutData"));
                            if (lPtr) {
                                VLOG(STR("[NBB-DUMP] {}  CPSlot Offsets=({},{},{},{}) Anchors=({},{}, {},{}) Align=({},{})\n"),
                                     indent.c_str(),
                                     lPtr[0], lPtr[1], lPtr[2], lPtr[3],
                                     lPtr[4], lPtr[5], lPtr[6], lPtr[7],
                                     lPtr[8], lPtr[9]);
                            }
                        }
                        auto* c = s->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                        if (c && *c) dump(*c, depth + 1);
                    }
                }
                auto* sc = w->GetValuePtrByPropertyNameInChain<UObject*>(STR("Content"));
                if (sc && *sc) dump(*sc, depth + 1);
            };
            VLOG(STR("[NBB-DUMP] === BEGIN dump of WBP_UI_ActionBar_C tree ===\n"));
            if (tRoot) dump(tRoot, 0);
            VLOG(STR("[NBB-DUMP] === END dump ===\n"));

            // v0.8 — Populate the persistent texture cache so subsequent
            // createNewBuildingBar calls (e.g. after destroyNewBuildingBar)
            // skip the entire discovery + spawn + tree-walk path. This is
            // the user's "remember and display them ourselves" — discover
            // ONCE, then reuse forever within the session.
            m_nbbCachedSlotEmpty   = out.texEmptyFullSlot ? out.texEmptyFullSlot : out.texSlotEmpty;
            m_nbbCachedSlotFocus   = out.texButtonFocused ? out.texButtonFocused : out.texSlotFocus;
            m_nbbCachedSlotCorners = out.texFocusedCorners;
            m_nbbCachedBarFrame    = out.texSlotFrame;
            // KeyBg texture lookup happens later in createNewBuildingBar
            // (via FindAllOf for T_UI_Icon_Input_Blank_Rect); cache it
            // there once we resolve it.
            m_nbbAssetsCached = (m_nbbCachedSlotEmpty != nullptr);
            VLOG(STR("[NewBuildingBar] discover: cached={} (slotEmpty={:p} slotFocus={:p} corners={:p} barFrame={:p})\n"),
                 m_nbbAssetsCached ? L"YES" : L"no",
                 (void*)m_nbbCachedSlotEmpty, (void*)m_nbbCachedSlotFocus,
                 (void*)m_nbbCachedSlotCorners, (void*)m_nbbCachedBarFrame);
            return true;
        }

        void createNewBuildingBar()
        {
            if (m_newBuildingBar && isObjectAlive(m_newBuildingBar))
            {
                VLOG(STR("[NewBuildingBar] already exists, skipping\n"));
                return;
            }

            // ── Step 1: discover assets (textures + sizes). v0.8 caches
            // results; if cache is populated we skip everything else.
            NbbDiscoveredAssets a;
            (void)nbbDiscoverAssets(a); // OK to fail — we'll fall through to FindAllOf below

            // v0.10 — Use the PROPER numbered-slot textures we found via
            // the [NBB-TEX] inventory dump. T_UI_Btn_HUD_AB_* (no Epic
            // prefix) is what the native ActionBar uses for slots 1-8.
            // Plus discovered the actual chrome assets:
            //   T_UI_Frame_HUD_AB_Top       (top decorative)
            //   T_UI_Frame_HUD_AB_Middle_0  (middle frame)
            //   T_UI_Frame_HUD_AB_Bottom    (bottom decorative)
            UObject* texChromeTop = nullptr;
            UObject* texChromeMiddle = nullptr;
            UObject* texChromeBottom = nullptr;
            if (!a.texSlotEmpty || !a.texSlotFocus || !a.texSlotFrame ||
                !texChromeTop || !texChromeMiddle || !texChromeBottom)
            {
                std::vector<UObject*> textures;
                try { findAllOfSafe(STR("Texture2D"), textures); } catch (...) {}
                for (auto* t : textures) {
                    if (!t) continue;
                    auto name = t->GetName();
                    // Slot textures — prefer non-Epic variants.
                    if (!a.texSlotEmpty    && name == STR("T_UI_Btn_HUD_AB_Empty"))    a.texSlotEmpty = t;
                    if (!a.texSlotFocus    && name == STR("T_UI_Btn_HUD_AB_Focused"))  a.texSlotFocus = t;
                    if (!a.texSlotDisabled && name == STR("T_UI_Btn_HUD_AB_Disabled")) a.texSlotDisabled = t;
                    // Chrome assets.
                    if (!texChromeTop      && name == STR("T_UI_Frame_HUD_AB_Top"))    texChromeTop = t;
                    if (!texChromeMiddle   && name == STR("T_UI_Frame_HUD_AB_Middle_0")) texChromeMiddle = t;
                    if (!texChromeBottom   && name == STR("T_UI_Frame_HUD_AB_Bottom")) texChromeBottom = t;
                    // Last-resort fallback for slot empty/focus.
                    if (!a.texSlotEmpty    && name == STR("T_UI_Btn_HUD_EpicAB_Empty"))   a.texSlotEmpty = t;
                    if (!a.texSlotFocus    && name == STR("T_UI_Btn_HUD_EpicAB_Focused")) a.texSlotFocus = t;
                }
                if (!m_nbbCachedSlotEmpty && a.texSlotEmpty) m_nbbCachedSlotEmpty = a.texSlotEmpty;
                if (!m_nbbCachedSlotFocus && a.texSlotFocus) m_nbbCachedSlotFocus = a.texSlotFocus;
                if (!m_nbbCachedTexChromeTop    && texChromeTop)    m_nbbCachedTexChromeTop    = texChromeTop;
                if (!m_nbbCachedTexChromeMiddle && texChromeMiddle) m_nbbCachedTexChromeMiddle = texChromeMiddle;
                if (!m_nbbCachedTexChromeBottom && texChromeBottom) m_nbbCachedTexChromeBottom = texChromeBottom;
                if (m_nbbCachedSlotEmpty) m_nbbAssetsCached = true;
                VLOG(STR("[NewBuildingBar] textures: slotEmpty={:p} slotFocus={:p} chromeTop={:p} chromeMid={:p} chromeBot={:p}\n"),
                     (void*)m_nbbCachedSlotEmpty, (void*)m_nbbCachedSlotFocus,
                     (void*)m_nbbCachedTexChromeTop, (void*)m_nbbCachedTexChromeMiddle,
                     (void*)m_nbbCachedTexChromeBottom);
            }
            // Pull cached chrome regardless of how this call resolved.
            if (!texChromeTop)    texChromeTop    = m_nbbCachedTexChromeTop;
            if (!texChromeMiddle) texChromeMiddle = m_nbbCachedTexChromeMiddle;
            if (!texChromeBottom) texChromeBottom = m_nbbCachedTexChromeBottom;
            if (!a.texSlotEmpty)
            {
                VLOG(STR("[NewBuildingBar] aborting — no slot empty texture available even after fallback\n"));
                return;
            }

            // ── Step 2: build a fresh UUserWidget with our own layout.
            UClass* userWidgetCls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            UClass* canvasCls     = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.CanvasPanel"));
            UClass* hboxCls       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            UClass* vboxCls       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            UClass* overlayCls    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            UClass* imageCls      = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            UClass* textCls       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            UClass* buttonCls     = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Button"));
            if (!userWidgetCls || !canvasCls || !hboxCls || !vboxCls || !overlayCls || !imageCls || !textCls)
            {
                VLOG(STR("[NewBuildingBar] missing UMG class — aborting\n"));
                return;
            }

            UObject* userWidget = jw_createGameWidget(userWidgetCls);
            if (!userWidget) return;
            auto* wtPtr = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtPtr ? *wtPtr : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            UFunction* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn)
            {
                VLOG(STR("[NewBuildingBar] SetBrushFromTexture missing — aborting\n"));
                return;
            }

            // Root: HorizontalBox (the slot row). Decorative top/bottom
            // frames are stacked vertically via a VBox containing
            // [topFrame HBox, slot row HBox, bottomFrame HBox].
            FStaticConstructObjectParameters rootP(vboxCls, outer);
            UObject* root = UObjectGlobals::StaticConstructObject(rootP);
            if (!root) return;
            if (widgetTree) setRootWidget(widgetTree, root);

            auto mkImg = [&](UObject* texture, float w, float h, float opacity = 1.0f) -> UObject*
            {
                if (!texture) return nullptr;
                FStaticConstructObjectParameters p(imageCls, outer);
                UObject* img = UObjectGlobals::StaticConstructObject(p);
                if (!img) return nullptr;
                umgSetBrush(img, texture, setBrushFn);
                if (s_off_brush >= 0 && w > 0 && h > 0)
                {
                    uint8_t* base = reinterpret_cast<uint8_t*>(img);
                    *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX()) = w;
                    *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY()) = h;
                }
                umgSetOpacity(img, opacity);
                return img;
            };
            auto mkOverlay = [&]() -> UObject* {
                FStaticConstructObjectParameters p(overlayCls, outer);
                return UObjectGlobals::StaticConstructObject(p);
            };
            auto mkVBox = [&]() -> UObject* {
                FStaticConstructObjectParameters p(vboxCls, outer);
                return UObjectGlobals::StaticConstructObject(p);
            };
            auto mkHBox = [&]() -> UObject* {
                FStaticConstructObjectParameters p(hboxCls, outer);
                return UObjectGlobals::StaticConstructObject(p);
            };
            auto mkText = [&](const std::wstring& s) -> UObject* {
                FStaticConstructObjectParameters p(textCls, outer);
                UObject* t = UObjectGlobals::StaticConstructObject(p);
                if (!t) return nullptr;
                umgSetText(t, s);
                umgSetTextColor(t, 1.0f, 1.0f, 1.0f, 1.0f);
                return t;
            };

            // v0.4 — bumped to 192×192 per user feedback (140 still too
            // small). Marker scaled accordingly.
            const float slotW   = 192.0f;
            const float slotH   = 192.0f;
            const float markerW = 128.0f;
            const float markerH = 40.0f;

            // v0.4 — prefer the proper slot textures captured from
            // emptyFullSlot / buttonFocused / FocusedCorners over the
            // EpicAB fallback textures.
            UObject* useEmptyTex = a.texEmptyFullSlot ? a.texEmptyFullSlot : a.texSlotEmpty;
            UObject* useFocusTex = a.texButtonFocused ? a.texButtonFocused : a.texSlotFocus;
            UObject* useFocusCornersTex = a.texFocusedCorners; // optional extra layer

            // ── Find the small grey "key rect" texture for F# labels —
            // same texture the bottom MC bar uses.
            UObject* texKeyBg = nullptr;
            {
                std::vector<UObject*> textures;
                try { findAllOfSafe(STR("Texture2D"), textures); } catch (...) {}
                for (auto* t : textures) {
                    if (!t) continue;
                    if (t->GetName() == STR("T_UI_Icon_Input_Blank_Rect")) { texKeyBg = t; break; }
                }
            }

            // v0.11 — Chrome backdrop REMOVED entirely per user request.
            // The Top/Middle/Bottom textures we found ARE the right
            // assets but their layout/scaling looked wrong without the
            // exact native CanvasPanel positions, and the user prefers
            // a plain bar to a wrong one. Just the slot row.
            (void)texChromeTop; (void)texChromeMiddle; (void)texChromeBottom;
            UObject* slotRow = mkHBox();
            if (!slotRow) return;
            UObject* slotRowSlot = addToVBox(root, slotRow);
            if (slotRowSlot) umgSetHAlign(slotRowSlot, 2); // Center

            // v0.9 — One-shot diagnostic: enumerate every Texture2D whose
            // name contains "HUD" or "ActionBar" and log it. This gives
            // us the inventory of HUD-related textures so we can
            // identify the actual chrome assets (middleFrame, topFrame,
            // bottomFrame textures) by name. Logged once per session.
            if (!m_nbbHudTexturesDumped)
            {
                m_nbbHudTexturesDumped = true;
                std::vector<UObject*> textures;
                try { findAllOfSafe(STR("Texture2D"), textures); } catch (...) {}
                int count = 0;
                VLOG(STR("[NBB-TEX] === HUD/ActionBar texture inventory ===\n"));
                for (auto* t : textures) {
                    if (!t) continue;
                    std::wstring n; try { n = t->GetName(); } catch (...) { continue; }
                    if (n.find(STR("HUD"))       != std::wstring::npos ||
                        n.find(STR("ActionBar")) != std::wstring::npos ||
                        n.find(STR("AB_"))       != std::wstring::npos ||
                        n.find(STR("Frame"))     != std::wstring::npos)
                    {
                        VLOG(STR("[NBB-TEX]   {}\n"), n.c_str());
                        ++count;
                    }
                }
                VLOG(STR("[NBB-TEX] === total {} matching textures ===\n"), count);
            }

            for (int i = 0; i < 8; ++i)
            {
                UObject* slotVBox = mkVBox();
                if (!slotVBox) continue;

                // (1) Numbered marker on top.
                if (a.texSlotMarker[i] || a.texSlotMarker[0])
                {
                    UObject* mTex = a.texSlotMarker[i] ? a.texSlotMarker[i] : a.texSlotMarker[0];
                    UObject* mImg = mkImg(mTex, markerW, markerH);
                    if (mImg)
                    {
                        UObject* ms = addToVBox(slotVBox, mImg);
                        if (ms) umgSetHAlign(ms, 2);
                        m_nbbSlotMarker[i] = mImg;
                    }
                }

                // (2) Slot frame Overlay: empty + focus + icon + bottom-center key label.
                UObject* slotOv = mkOverlay();
                if (slotOv)
                {
                    // Empty texture (always visible) — proper slot tex
                    // captured from the live ActionBar's slot widget.
                    UObject* eImg = mkImg(useEmptyTex, slotW, slotH);
                    if (eImg)
                    {
                        UObject* es = addToOverlay(slotOv, eImg);
                        if (es) { umgSetHAlign(es, 2); umgSetVAlign(es, 2); }
                        m_nbbSlotEmpty[i] = eImg;
                    }
                    // Focused texture (collapsed initially; toggled on highlight).
                    if (useFocusTex)
                    {
                        UObject* fImg = mkImg(useFocusTex, slotW, slotH);
                        if (fImg)
                        {
                            UObject* fs = addToOverlay(slotOv, fImg);
                            if (fs) { umgSetHAlign(fs, 2); umgSetVAlign(fs, 2); }
                            if (auto* visPtr = fImg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                                *visPtr = 1; // Collapsed
                            setWidgetVisibility(fImg, 1);
                            m_nbbSlotFocus[i] = fImg;
                        }
                    }
                    // Focused corner glow — extra subtle highlight layer
                    // that appears on top of buttonFocused. Only added
                    // if we successfully captured it.
                    if (useFocusCornersTex)
                    {
                        UObject* cImg = mkImg(useFocusCornersTex, slotW, slotH);
                        if (cImg)
                        {
                            UObject* cs = addToOverlay(slotOv, cImg);
                            if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); }
                            if (auto* visPtr = cImg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                                *visPtr = 1; // Collapsed by default
                            setWidgetVisibility(cImg, 1);
                            // Tracked alongside the focus image so
                            // highlight toggles both together.
                        }
                    }
                    // Icon placeholder (Phase 2 will populate with the
                    // actual builder-piece texture).
                    {
                        FStaticConstructObjectParameters ip(imageCls, outer);
                        UObject* iImg = UObjectGlobals::StaticConstructObject(ip);
                        if (iImg)
                        {
                            UObject* is = addToOverlay(slotOv, iImg);
                            if (is) { umgSetHAlign(is, 2); umgSetVAlign(is, 2); }
                            if (auto* visPtr = iImg->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Visibility")))
                                *visPtr = 1;
                            setWidgetVisibility(iImg, 1);
                            m_nbbSlotIcon[i] = iImg;
                        }
                    }
                    // (3) F# key label — bottom-center, with a small grey
                    // rect background (matches the existing builder bar
                    // style). Text is read from s_bindings[i].key so it
                    // tracks the user's QuickBuild keymap.
                    if (texKeyBg)
                    {
                        FStaticConstructObjectParameters kbP(imageCls, outer);
                        UObject* kbImg = UObjectGlobals::StaticConstructObject(kbP);
                        if (kbImg)
                        {
                            umgSetBrush(kbImg, texKeyBg, setBrushFn);
                            if (s_off_brush >= 0) {
                                uint8_t* base = reinterpret_cast<uint8_t*>(kbImg);
                                *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeX()) = 36.0f;
                                *reinterpret_cast<float*>(base + s_off_brush + brushImageSizeY()) = 22.0f;
                            }
                            umgSetOpacity(kbImg, 0.8f);
                            UObject* ks = addToOverlay(slotOv, kbImg);
                            if (ks) {
                                umgSetHAlign(ks, 2); // Center
                                umgSetVAlign(ks, 3); // Bottom
                                umgSetSlotPadding(ks, 0, 0, 0, 6);
                            }
                            m_nbbSlotKeyBg[i] = kbImg;
                        }
                    }
                    {
                        std::wstring kn = keyName(s_bindings[i].key);
                        UObject* tb = mkText(kn);
                        if (tb)
                        {
                            UObject* ts = addToOverlay(slotOv, tb);
                            if (ts) {
                                umgSetHAlign(ts, 2); // Center
                                umgSetVAlign(ts, 3); // Bottom
                                umgSetSlotPadding(ts, 0, 0, 0, 6);
                            }
                            m_nbbSlotKeyLbl[i] = tb;
                        }
                    }
                }

                if (slotOv)
                {
                    UObject* ovSlot = addToVBox(slotVBox, slotOv);
                    if (ovSlot) umgSetHAlign(ovSlot, 2);
                }

                UObject* slotInRow = addToHBox(slotRow, slotVBox);
                if (slotInRow) { umgSetVAlign(slotInRow, 0); umgSetSlotPadding(slotInRow, 4, 0, 4, 0); }
            }

            // v0.11 — bottom decorative strip removed per user request.

            // ── Add to viewport at top-center.
            auto* fnAdd = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (fnAdd)
            {
                std::vector<uint8_t> b(fnAdd->GetParmsSize(), 0);
                if (auto* p = findParam(fnAdd, STR("ZOrder")))
                    *reinterpret_cast<int32_t*>(b.data() + p->GetOffset_Internal()) = 50;
                safeProcessEvent(userWidget, fnAdd, b.data());
            }
            if (auto* fnAlign = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport")))
            {
                std::vector<uint8_t> b(fnAlign->GetParmsSize(), 0);
                if (auto* p = findParam(fnAlign, STR("Alignment")))
                {
                    float* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = 0.5f; xy[1] = 0.0f;
                }
                safeProcessEvent(userWidget, fnAlign, b.data());
            }
            if (auto* fnPos = userWidget->GetFunctionByNameInChain(STR("SetPositionInViewport")))
            {
                m_screen.refresh(findPlayerController());
                float centerX = static_cast<float>(m_screen.viewW) * 0.5f;
                float topY    = 10.0f; // v0.12 — 10 px from top edge
                std::vector<uint8_t> b(fnPos->GetParmsSize(), 0);
                if (auto* p = findParam(fnPos, STR("Position")))
                {
                    float* xy = reinterpret_cast<float*>(b.data() + p->GetOffset_Internal());
                    xy[0] = centerX; xy[1] = topY;
                }
                if (auto* p = findParam(fnPos, STR("bRemoveDPIScale")))
                    *reinterpret_cast<bool*>(b.data() + p->GetOffset_Internal()) = true;
                safeProcessEvent(userWidget, fnPos, b.data());
                VLOG(STR("[NewBuildingBar] anchored top-center at ({}, {})\n"), centerX, topY);
            }

            m_newBuildingBar = userWidget;
            VLOG(STR("[NewBuildingBar] === built from-scratch with discovered assets ===\n"));

            // v0.10 — Pull QuickBuild icons from m_recipeSlots and apply
            // to each slot icon image. Slots without an assignment stay
            // empty.
            populateNewBuildingBarIcons();

            // v0.4 — visible proof that highlight toggling works: leave
            // slot 0 highlighted ON at create time.
            newBuildingBarHighlight(0, true);

            showOnScreen(L"New Building Bar created (slot 1 highlighted as test)",
                         3.0f, 0.4f, 0.9f, 1.0f);
        }


        void toggleFontTestPanel()
        {

            if (m_ftVisible && m_fontTestWidget && isObjectAlive(m_fontTestWidget))
            {
                deferRemoveWidget(m_fontTestWidget);
                m_fontTestWidget = nullptr;
                m_ftVisible = false;
                for (auto& t : m_ftTabImages) t = nullptr;
                for (auto& t : m_ftTabLabels) t = nullptr;
                m_ftTabActiveTexture = nullptr;
                m_ftTabInactiveTexture = nullptr;
                m_ftSelectedTab = 0;
                m_ftScrollBox = nullptr;
                for (auto& c : m_ftTabContent) c = nullptr;
                for (auto& l : m_ftKeyBoxLabels) l = nullptr;
                for (auto& c : m_ftCheckImages) c = nullptr;
                m_ftModBoxLabel = nullptr;
                m_ftNoCollisionCheckImg = nullptr;
                m_ftNoCollisionLabel = nullptr;
                m_ftNoCollisionKeyLabel = nullptr;
                m_ftRemovalVBox = nullptr;
                m_ftRemovalHeader = nullptr;
                m_ftLastRemovalCount = -1;
                for (auto& c : m_ftGameModCheckImages) c = nullptr;
                m_ftGameModEntries.clear();
                if (s_capturingBind >= 0) { s_capturingBind = -1; }
                setInputModeGame();
                showOnScreen(Loc::get("msg.settings_closed"), 2.0f, 0.8f, 0.8f, 0.8f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [Settings] Creating settings panel...\n"));


            UObject* texBG = findTexture2DByName(L"T_UI_Pnl_Craft_BG");
            UObject* texTab = findTexture2DByName(L"T_UI_Btn_P2_Up");
            UObject* texTabFocused = findTexture2DByName(L"T_UI_Btn_P1_Focused");
            UObject* texKeyBox = findTexture2DByName(L"T_UI_Btn_P1_Active");
            UObject* texSectionBg = findTexture2DByName(L"T_UI_Map_LocationName_HUD");
            UObject* texCB = findTexture2DByName(L"T_UI_Icon_Checkbox_DiamondBG");
            UObject* texCheck = findTexture2DByName(L"T_UI_Icon_Check");
            if (!texBG) { showErrorBox(L"Settings: BG not found!"); return; }
            if (!texTab) { showErrorBox(L"Settings: Tab texture not found!"); return; }
            if (!texTabFocused) texTabFocused = texTab;
            m_ftTabActiveTexture = texTabFocused;
            m_ftTabInactiveTexture = texTab;


            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass      = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* overlayClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* borderClass     = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* sizeBoxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* scrollBoxClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ScrollBox"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !overlayClass ||
                !textBlockClass || !borderClass || !sizeBoxClass || !scrollBoxClass)
            {
                showErrorBox(L"FontTest: missing UMG classes!");
                return;
            }


            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"FontTest: no PlayerController!"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showErrorBox(L"FontTest: WBL not found!"); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) { showErrorBox(L"FontTest: WBL CDO null!"); return; }

            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"FontTest: CreateWidget failed!"); return; }


            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;


            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"FontTest: SetBrushFromTexture missing!"); return; }


            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOverlay = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOverlay) { showErrorBox(L"FontTest: overlay failed!"); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOverlay);


            {
                auto* setClipFn = rootOverlay->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; safeProcessEvent(rootOverlay, setClipFn, cp.data()); }
            }


            {
                FStaticConstructObjectParameters borderFrameP(imageClass, outer);
                UObject* borderFrame = UObjectGlobals::StaticConstructObject(borderFrameP);
                if (borderFrame)
                {
                    umgSetBrushSize(borderFrame, 1540.0f, 880.0f);
                    umgSetImageColor(borderFrame, 0.08f, 0.14f, 0.32f, 0.55f);
                    addToOverlay(rootOverlay, borderFrame);
                }
            }


            {
                FStaticConstructObjectParameters imgP(imageClass, outer);
                UObject* bgImg = UObjectGlobals::StaticConstructObject(imgP);
                if (bgImg)
                {
                    umgSetBrush(bgImg, texBG, setBrushFn);
                    umgSetBrushSize(bgImg, 1539.0f, 879.0f);
                    umgSetOpacity(bgImg, 0.5f);
                    UObject* bgSlot = addToOverlay(rootOverlay, bgImg);
                    if (bgSlot) { umgSetHAlign(bgSlot, 2); umgSetVAlign(bgSlot, 2); }
                }
            }


            FStaticConstructObjectParameters hbP(hboxClass, outer);
            UObject* contentHBox = UObjectGlobals::StaticConstructObject(hbP);
            if (!contentHBox) { showErrorBox(L"FontTest: hbox failed!"); return; }
            {
                UObject* slot = addToOverlay(rootOverlay, contentHBox);
                if (slot)
                {
                    umgSetSlotPadding(slot, 30.0f, 30.0f, 30.0f, 30.0f);
                    umgSetHAlign(slot, 0);
                    umgSetVAlign(slot, 0);
                }
            }


            UObject* defaultFont = nullptr;
            {
                std::vector<UObject*> fonts;
                findAllOfSafe(STR("Font"), fonts);
                for (auto* f : fonts)
                {
                    if (f && std::wstring(f->GetName()) == L"DefaultRegularFont")
                    { defaultFont = f; break; }
                }
            }


            auto makeTB = [&](const std::wstring& text, float r, float g, float b, float a, int32_t size) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                if (defaultFont) umgSetFontAndSize(tb, defaultFont, size);
                else umgSetFontSize(tb, size);
                return tb;
            };


            {
                FStaticConstructObjectParameters vbP(vboxClass, outer);
                UObject* tabVBox = UObjectGlobals::StaticConstructObject(vbP);
                if (tabVBox)
                {
                    UObject* slot = addToHBox(contentHBox, tabVBox);
                    if (slot) umgSetSlotPadding(slot, 2.5f, 10.0f, 2.5f, 0.0f);

                    const wchar_t* tabNames[] = { L"Key Bindings", L"Game Options", L"Environment", L"Game Mods", L"Cheats", L"Tweaks" };
                    for (int i = 0; i < CONFIG_TAB_COUNT; i++)
                    {
                        FStaticConstructObjectParameters olTabP(overlayClass, outer);
                        UObject* tabOl = UObjectGlobals::StaticConstructObject(olTabP);
                        if (!tabOl) continue;


                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* tabImg = UObjectGlobals::StaticConstructObject(imgP);
                        if (tabImg)
                        {
                            UObject* tex = (i == 0) ? texTabFocused : texTab;
                            umgSetBrushNoMatch(tabImg, tex, setBrushFn);
                            umgSetBrushSize(tabImg, 512.0f, 128.0f);
                            addToOverlay(tabOl, tabImg);
                            m_ftTabImages[i] = tabImg;
                        }


                        UObject* tabLabel = makeTB(tabNames[i],
                            (i == 0) ? 0.9f : 0.55f,
                            (i == 0) ? 0.88f : 0.55f,
                            (i == 0) ? 0.78f : 0.65f,
                            (i == 0) ? 1.0f : 0.7f, 24);
                        if (tabLabel)
                        {
                            m_ftTabLabels[i] = tabLabel;
                            UObject* txtSlot = addToOverlay(tabOl, tabLabel);
                            if (txtSlot)
                            {
                                umgSetHAlign(txtSlot, 2);
                                umgSetVAlign(txtSlot, 2);
                            }
                        }

                        UObject* tabSlot = addToVBox(tabVBox, tabOl);
                        if (tabSlot) umgSetSlotPadding(tabSlot, 0.0f, 2.0f, 0.0f, 2.0f);
                    }
                }
            }


            {

                UObject* texSep = findTexture2DByName(L"T_UI_Shared_Line");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Pnl_Separator");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Line");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Separator");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Shared_Separator");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Pnl_Line");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Shared_LineSeparator");
                if (!texSep) texSep = findTexture2DByName(L"T_UI_Shared_VerticalLine");

                FStaticConstructObjectParameters sepImgP(imageClass, outer);
                UObject* sepImg = UObjectGlobals::StaticConstructObject(sepImgP);
                if (sepImg)
                {
                    if (texSep)
                    {
                        umgSetBrush(sepImg, texSep, setBrushFn);
                        umgSetBrushSize(sepImg, 32.0f, 820.0f);
                        umgSetRenderScale(sepImg, 1.0f, -1.0f);
                    }
                    else
                    {

                        umgSetBrushSize(sepImg, 6.0f, 820.0f);
                        umgSetImageColor(sepImg, 0.08f, 0.14f, 0.32f, 0.55f);
                    }
                    UObject* sepSlot = addToHBox(contentHBox, sepImg);
                    if (sepSlot) umgSetSlotPadding(sepSlot, 2.0f, 0.0f, 2.0f, 0.0f);
                }

                if (!texSep)
                {
                    VLOG(STR("[MoriaCppMod] [Settings] Separator texture not found. Textures with 'Shared'/'Line'/'Sep':\n"));
                    std::vector<UObject*> allTex;
                    findAllOfSafe(STR("Texture2D"), allTex);
                    for (auto* t : allTex)
                    {
                        if (!t) continue;
                        std::wstring n(t->GetName());
                        if (n.find(L"Shared") != std::wstring::npos || n.find(L"shared") != std::wstring::npos ||
                            n.find(L"Line") != std::wstring::npos || n.find(L"Sep") != std::wstring::npos)
                            VLOG(STR("[MoriaCppMod] [Settings]   TEX: {}\n"), n);
                    }
                }
            }


            {

                FStaticConstructObjectParameters rightOlP(overlayClass, outer);
                UObject* rightOverlay = UObjectGlobals::StaticConstructObject(rightOlP);
                if (rightOverlay)
                {

                    FStaticConstructObjectParameters borderP(borderClass, outer);
                    UObject* frameBorder = UObjectGlobals::StaticConstructObject(borderP);
                    if (frameBorder)
                    {
                        auto* setBrushColorFn = frameBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
                        if (setBrushColorFn)
                        {
                            auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                            if (pColor)
                            {
                                int sz = setBrushColorFn->GetParmsSize();
                                std::vector<uint8_t> cb(sz, 0);
                                auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                                c[0] = 0.05f; c[1] = 0.05f; c[2] = 0.08f; c[3] = 0.4f;
                                safeProcessEvent(frameBorder, setBrushColorFn, cb.data());
                            }
                        }
                        addToOverlay(rightOverlay, frameBorder);
                    }


                    FStaticConstructObjectParameters sbxP(sizeBoxClass, outer);
                    UObject* rightSizeBox = UObjectGlobals::StaticConstructObject(sbxP);
                    FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                    UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                    if (rightSizeBox && scrollBox)
                    {
                        m_ftScrollBox = scrollBox;

                        auto* setHOvFn = rightSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
                        if (setHOvFn) { int sz = setHOvFn->GetParmsSize(); std::vector<uint8_t> hp(sz, 0); auto* p = findParam(setHOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 815.0f; safeProcessEvent(rightSizeBox, setHOvFn, hp.data()); }

                        // Set width to fill available space (fixes scrollbar not expanding)
                        auto* setWOvFn = rightSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                        if (setWOvFn) { int sz = setWOvFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWOvFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 960.0f; safeProcessEvent(rightSizeBox, setWOvFn, wp.data()); }

                        auto* setChildFn = rightSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                        if (!setChildFn) setChildFn = rightSizeBox->GetFunctionByNameInChain(STR("AddChild"));
                        if (setChildFn)
                        {
                            auto* pChild = findParam(setChildFn, STR("Content"));
                            if (!pChild) pChild = findParam(setChildFn, STR("InContent"));
                            if (pChild) { int sz = setChildFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); *reinterpret_cast<UObject**>(cp.data() + pChild->GetOffset_Internal()) = scrollBox; safeProcessEvent(rightSizeBox, setChildFn, cp.data()); }
                        }
                        UObject* sbSlot = addToOverlay(rightOverlay, rightSizeBox);
                        if (sbSlot)
                        {
                            umgSetHAlign(sbSlot, 0);
                            umgSetVAlign(sbSlot, 0);
                            umgSetSlotPadding(sbSlot, 10.0f, 15.0f, 0.0f, 10.0f);
                        }

                        auto* alwaysShowFn = scrollBox->GetFunctionByNameInChain(STR("SetAlwaysShowScrollbar"));
                        if (alwaysShowFn)
                        {
                            auto* p = findParam(alwaysShowFn, STR("NewAlwaysShowScrollbar"));
                            int sz = alwaysShowFn->GetParmsSize();
                            std::vector<uint8_t> buf(sz, 0);
                            if (p) *reinterpret_cast<bool*>(buf.data() + p->GetOffset_Internal()) = true;
                            safeProcessEvent(scrollBox, alwaysShowFn, buf.data());
                        }

                        // Add padding between content and scrollbar
                        auto* setPaddingFn = scrollBox->GetFunctionByNameInChain(STR("SetScrollbarPadding"));
                        if (setPaddingFn)
                        {
                            auto* pPad = findParam(setPaddingFn, STR("InScrollbarPadding"));
                            if (pPad)
                            {
                                int sz = setPaddingFn->GetParmsSize();
                                std::vector<uint8_t> buf(sz, 0);
                                // FMargin: Left, Top, Right, Bottom
                                auto* m = reinterpret_cast<float*>(buf.data() + pPad->GetOffset_Internal());
                                m[0] = 60.0f; // left padding (space between content and scrollbar)
                                m[1] = 0.0f;
                                m[2] = 0.0f;
                                m[3] = 0.0f;
                                safeProcessEvent(scrollBox, setPaddingFn, buf.data());
                            }
                        }
                    }
                    UObject* borderSlot = addToHBox(contentHBox, rightOverlay);
                    if (borderSlot)
                    {
                        umgSetSlotSize(borderSlot, 1.0f, 1);
                        umgSetVAlign(borderSlot, 0);
                    }


                    if (scrollBox)
                    {

                        for (int t = 0; t < CONFIG_TAB_COUNT; t++)
                        {
                            FStaticConstructObjectParameters tcP(vboxClass, outer);
                            UObject* tcVBox = UObjectGlobals::StaticConstructObject(tcP);
                            if (tcVBox)
                            {
                                m_ftTabContent[t] = tcVBox;
                                addChildToPanel(scrollBox, STR("AddChild"), tcVBox);
                                if (t != 0) setWidgetVisibility(tcVBox, 1); // Collapse non-active tabs
                            }
                        }


                        if (m_ftTabContent[1])
                        {
                            UObject* t1 = m_ftTabContent[1];


                            if (texSectionBg)
                            {
                                FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                if (secOl)
                                {
                                    FStaticConstructObjectParameters secImgP(imageClass, outer);
                                    UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                    if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                    UObject* secLabel = makeTB(Loc::get("ui.options_title"), 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                    if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                    addToVBox(t1, secOl);
                                }
                            }


                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* ncRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (ncRow)
                                {

                                    if (texCB)
                                    {
                                        FStaticConstructObjectParameters olP(overlayClass, outer);
                                        UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                                        if (cbOl)
                                        {
                                            FStaticConstructObjectParameters imgP(imageClass, outer);
                                            UObject* cbBg = UObjectGlobals::StaticConstructObject(imgP);
                                            if (cbBg) { umgSetBrushNoMatch(cbBg, texCB, setBrushFn); umgSetBrushSize(cbBg, 80.0f, 80.0f); addToOverlay(cbOl, cbBg); }
                                            if (texCheck)
                                            {
                                                FStaticConstructObjectParameters chkP(imageClass, outer);
                                                UObject* chkImg = UObjectGlobals::StaticConstructObject(chkP);
                                                if (chkImg)
                                                {
                                                    umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                                    umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                                    addToOverlay(cbOl, chkImg);
                                                    m_ftNoCollisionCheckImg = chkImg;
                                                    auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = 1; safeProcessEvent(chkImg, visFn, vp); }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(ncRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }

                                    UObject* ncLabel = makeTB(Loc::get("ui.no_collision"), 0.86f, 0.90f, 0.96f, 1.0f, 24);
                                    m_ftNoCollisionLabel = ncLabel;
                                    if (ncLabel)
                                    {
                                        UObject* ls = addToHBox(ncRow, ncLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }

                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(Loc::get("ui.status_off"), 0.7f, 0.3f, 0.3f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); m_ftNoCollisionKeyLabel = kbLabel; UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(ncRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, ncRow);
                                }
                            }


                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* rcRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (rcRow)
                                {
                                    UObject* rcLabel = makeTB(Loc::get("ui.rename_character"), 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                    if (rcLabel)
                                    {
                                        UObject* ls = addToHBox(rcRow, rcLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }
                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(Loc::get("ui.button_rename"), 0.9f, 0.75f, 0.2f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(rcRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, rcRow);
                                }
                            }

                            // Save Game button row (same pattern as Rename Character)
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* sgRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (sgRow)
                                {
                                    UObject* sgLabel = makeTB(L"Save Game", 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                    if (sgLabel)
                                    {
                                        UObject* ls = addToHBox(sgRow, sgLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }
                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(L"SAVE NOW", 0.31f, 0.78f, 0.47f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(sgRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, sgRow);
                                }
                            }

                            {
                                struct GameOptBind { int bindIdx; UObject** checkImgPtr; bool* enabledPtr; };
                                GameOptBind gameOptBinds[] = {
                                    { BIND_TRASH_ITEM,     &m_ftTrashCheckImg,      &m_trashItemEnabled },
                                    { BIND_REPLENISH_ITEM, &m_ftReplenishCheckImg,  &m_replenishItemEnabled },
                                    { BIND_REMOVE_ATTRS,   &m_ftRemoveAttrsCheckImg,&m_removeAttrsEnabled },
                                    { BIND_PITCH_ROTATE,   &m_ftPitchCheckImg,      &m_pitchRotateEnabled },
                                    { BIND_ROLL_ROTATE,    &m_ftRollCheckImg,       &m_rollRotateEnabled },
                                };
                                for (auto& gob : gameOptBinds)
                                {
                                    int bi = gob.bindIdx;
                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* goRow = UObjectGlobals::StaticConstructObject(rowP);
                                    if (!goRow) continue;

                                    if (texCB)
                                    {
                                        FStaticConstructObjectParameters olP(overlayClass, outer);
                                        UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                                        if (cbOl)
                                        {
                                            FStaticConstructObjectParameters imgP(imageClass, outer);
                                            UObject* cbBg = UObjectGlobals::StaticConstructObject(imgP);
                                            if (cbBg) { umgSetBrushNoMatch(cbBg, texCB, setBrushFn); umgSetBrushSize(cbBg, 80.0f, 80.0f); addToOverlay(cbOl, cbBg); }
                                            if (texCheck)
                                            {
                                                FStaticConstructObjectParameters chkP(imageClass, outer);
                                                UObject* chkImg = UObjectGlobals::StaticConstructObject(chkP);
                                                if (chkImg)
                                                {
                                                    umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                                    umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                                    addToOverlay(cbOl, chkImg);
                                                    *gob.checkImgPtr = chkImg;

                                                    auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = 1; safeProcessEvent(chkImg, visFn, vp); }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(goRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }

                                    UObject* goLabel = makeTB(s_bindings[bi].label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                    if (goLabel)
                                    {
                                        UObject* ls = addToHBox(goRow, goLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }

                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(keyName(s_bindings[bi].key), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                            if (kbLabel)
                                            {
                                                m_ftKeyBoxLabels[bi] = kbLabel;
                                                UObject* ks = addToOverlay(kbOl, kbLabel);
                                                if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                            }
                                            UObject* kbSlot = addToHBox(goRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, goRow);
                                }
                            }
                        }


                        if (m_ftTabContent[2])
                        {
                            UObject* t2 = m_ftTabContent[2];


                            int remCount = s_config.removalCount.load();
                            UObject* hdr = makeTB(Loc::get("ui.saved_removals_prefix") + std::to_wstring(remCount) + Loc::get("ui.saved_removals_suffix"),
                                                  0.78f, 0.86f, 1.0f, 1.0f, 24);
                            if (hdr) { umgSetBold(hdr); addToVBox(t2, hdr); }
                            m_ftRemovalHeader = hdr;


                            FStaticConstructObjectParameters rvP(vboxClass, outer);
                            UObject* remVBox = UObjectGlobals::StaticConstructObject(rvP);
                            if (remVBox) { m_ftRemovalVBox = remVBox; addToVBox(t2, remVBox); }


                            UObject* texDanger = findTexture2DByName(L"T_UI_Icon_Danger");
                            if (remVBox && s_config.removalCSInit)
                            {
                                CriticalSectionLock removalLock(s_config.removalCS);
                                for (size_t i = 0; i < s_config.removalEntries.size(); i++)
                                {
                                    const auto& entry = s_config.removalEntries[i];
                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowP);
                                    if (!rowHBox) continue;


                                    if (texDanger && setBrushFn)
                                    {
                                        FStaticConstructObjectParameters imgP(imageClass, outer);
                                        UObject* dangerImg = UObjectGlobals::StaticConstructObject(imgP);
                                        if (dangerImg)
                                        {
                                            umgSetBrushNoMatch(dangerImg, texDanger, setBrushFn);
                                            umgSetBrushSize(dangerImg, 56.0f, 56.0f);
                                            UObject* imgSlot = addToHBox(rowHBox, dangerImg);
                                            if (imgSlot) umgSetSlotPadding(imgSlot, 4.0f, 8.0f, 8.0f, 8.0f);
                                        }
                                    }


                                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                                    if (infoVBox)
                                    {
                                        UObject* nameTB = makeTB(entry.friendlyName, 0.3f, 0.85f, 0.3f, 1.0f, 22);
                                        if (nameTB) { umgSetBold(nameTB); addToVBox(infoVBox, nameTB); }
                                        std::wstring coordText = entry.isTypeRule ? Loc::get("ui.type_rule") : entry.coordsW;
                                        UObject* coordsTB = makeTB(coordText, 0.85f, 0.25f, 0.25f, 1.0f, 16);
                                        if (coordsTB) addToVBox(infoVBox, coordsTB);
                                        UObject* infoSlot = addToHBox(rowHBox, infoVBox);
                                        if (infoSlot) umgSetVAlign(infoSlot, 2);
                                    }

                                    addToVBox(remVBox, rowHBox);
                                }
                            }
                            m_ftLastRemovalCount = remCount;
                        }


                        if (m_ftTabContent[3])
                        {
                            UObject* t3 = m_ftTabContent[3];


                            m_ftGameModEntries = discoverGameMods();

                            if (m_ftGameModEntries.empty())
                            {
                                UObject* noMods = makeTB(Loc::get("msg.no_definition_packs"),
                                    0.7f, 0.7f, 0.7f, 1.0f, 22);
                                if (noMods)
                                {
                                    UObject* s = addToVBox(t3, noMods);
                                    if (s) umgSetSlotPadding(s, 10.0f, 20.0f, 10.0f, 10.0f);
                                }
                            }
                            else
                            {

                                if (texSectionBg)
                                {
                                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                    if (secOl)
                                    {
                                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                        UObject* secLabel = makeTB(Loc::get("ui.definition_packs_title"), 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                        addToVBox(t3, secOl);
                                    }
                                }


                                for (size_t gi = 0; gi < m_ftGameModEntries.size() && gi < MAX_GAME_MODS; gi++)
                                {
                                    auto& gm = m_ftGameModEntries[gi];

                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* gmRow = UObjectGlobals::StaticConstructObject(rowP);
                                    if (!gmRow) continue;


                                    if (texCB)
                                    {
                                        FStaticConstructObjectParameters olP(overlayClass, outer);
                                        UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                                        if (cbOl)
                                        {
                                            FStaticConstructObjectParameters imgP(imageClass, outer);
                                            UObject* cbBg = UObjectGlobals::StaticConstructObject(imgP);
                                            if (cbBg) { umgSetBrushNoMatch(cbBg, texCB, setBrushFn); umgSetBrushSize(cbBg, 80.0f, 80.0f); addToOverlay(cbOl, cbBg); }
                                            if (texCheck)
                                            {
                                                FStaticConstructObjectParameters chkImgP(imageClass, outer);
                                                UObject* chkImg = UObjectGlobals::StaticConstructObject(chkImgP);
                                                if (chkImg)
                                                {
                                                    umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                                    umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                                    addToOverlay(cbOl, chkImg);
                                                    m_ftGameModCheckImages[gi] = chkImg;

                                                    if (!gm.enabled)
                                                    {
                                                        auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                        if (visFn) { uint8_t vp[8]{}; vp[0] = 2; safeProcessEvent(chkImg, visFn, vp); }
                                                    }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(gmRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }


                                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                                    if (infoVBox)
                                    {
                                        std::wstring wTitle(gm.title.begin(), gm.title.end());
                                        UObject* titleTB = makeTB(wTitle, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                        if (titleTB) { umgSetBold(titleTB); addToVBox(infoVBox, titleTB); }


                                        if (!gm.description.empty())
                                        {

                                            std::string plain;
                                            bool inTag = false;
                                            for (char c : gm.description)
                                            {
                                                if (c == '<') { inTag = true; continue; }
                                                if (c == '>') { inTag = false; plain += ' '; continue; }
                                                if (!inTag) plain += c;
                                            }

                                            std::string desc;
                                            bool lastSpace = true;
                                            for (char c : plain)
                                            {
                                                if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                                                { if (!lastSpace) { desc += ' '; lastSpace = true; } }
                                                else { desc += c; lastSpace = false; }
                                            }

                                            while (!desc.empty() && desc.back() == ' ') desc.pop_back();
                                            while (!desc.empty() && desc.front() == ' ') desc.erase(desc.begin());

                                            if (!desc.empty())
                                            {
                                                std::wstring wDesc(desc.begin(), desc.end());
                                                UObject* descTB = makeTB(wDesc, 0.6f, 0.6f, 0.65f, 0.8f, 18);
                                                if (descTB) addToVBox(infoVBox, descTB);
                                            }
                                        }

                                        UObject* infoSlot = addToHBox(gmRow, infoVBox);
                                        if (infoSlot)
                                        {
                                            umgSetSlotSize(infoSlot, 1.0f, 1);
                                            umgSetVAlign(infoSlot, 2);
                                        }
                                    }

                                    UObject* rowSlot = addToVBox(t3, gmRow);
                                    if (rowSlot) umgSetSlotPadding(rowSlot, 0.0f, 2.0f, 0.0f, 2.0f);
                                }


                                UObject* footerTB = makeTB(Loc::get("msg.game_mods_restart_notice"),
                                    1.0f, 0.2f, 0.2f, 1.0f, 20);
                                if (footerTB)
                                {
                                    umgSetBold(footerTB);
                                    UObject* fs = addToVBox(t3, footerTB);
                                    if (fs) umgSetSlotPadding(fs, 10.0f, 20.0f, 10.0f, 10.0f);
                                }
                            }

                            VLOG(STR("[MoriaCppMod] [Settings] Game Mods tab populated ({} entries)\n"),
                                m_ftGameModEntries.size());
                        }


                        // v6.4.1 — Cheats tab (index 4). Two action buttons styled exactly like
                        // the Rename Character / Save Game buttons in the Game Options tab:
                        //   HBox: [Label stretched, pad L=92 T=24 B=24 VAlign=Center]
                        //         [ texKeyBox overlay 400x128 with centered bold text, pad R=50 ]
                        if (m_ftTabContent[4])
                        {
                            UObject* t4 = m_ftTabContent[4];

                            // Helper lambda: build one action row matching SAVE NOW / RENAME pattern.
                            auto makeCheatRow = [&](const wchar_t* rowLabel, const wchar_t* btnText,
                                                    float btnR, float btnG, float btnB, float btnA,
                                                    UObject*& outBtnImg) {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* row = UObjectGlobals::StaticConstructObject(rowP);
                                if (!row) return;

                                UObject* label = makeTB(rowLabel, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                if (label)
                                {
                                    UObject* ls = addToHBox(row, label);
                                    if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                }

                                if (texKeyBox)
                                {
                                    FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                    UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                    if (kbOl)
                                    {
                                        FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                        UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                        if (kbImg)
                                        {
                                            umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn);
                                            umgSetBrushSize(kbImg, 400.0f, 128.0f);
                                            addToOverlay(kbOl, kbImg);
                                            outBtnImg = kbImg;
                                        }
                                        UObject* kbLabel = makeTB(btnText, btnR, btnG, btnB, btnA, 24);
                                        if (kbLabel)
                                        {
                                            umgSetBold(kbLabel);
                                            UObject* ks = addToOverlay(kbOl, kbLabel);
                                            if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                        }
                                        UObject* kbSlot = addToHBox(row, kbOl);
                                        if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                    }
                                }

                                addToVBox(t4, row);
                            };

                            // UNLOCK button colored like RENAME (gold)
                            makeCheatRow(STR("Unlock Recipes"), STR("UNLOCK"),
                                         0.9f, 0.75f, 0.2f, 1.0f, m_ftCheatsUnlockBtnImg);

                            // READ button colored like SAVE NOW (green)
                            makeCheatRow(STR("Read All"), STR("READ"),
                                         0.31f, 0.78f, 0.47f, 1.0f, m_ftCheatsReadBtnImg);

                            // Peace Mode toggle row — checkbox on left, label, dynamic PEACE/FIGHT button
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* pmRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (pmRow)
                                {
                                    // Checkbox overlay (diamond background + optional check mark)
                                    if (texCB)
                                    {
                                        FStaticConstructObjectParameters olP(overlayClass, outer);
                                        UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                                        if (cbOl)
                                        {
                                            FStaticConstructObjectParameters imgP(imageClass, outer);
                                            UObject* cbBg = UObjectGlobals::StaticConstructObject(imgP);
                                            if (cbBg) { umgSetBrushNoMatch(cbBg, texCB, setBrushFn); umgSetBrushSize(cbBg, 80.0f, 80.0f); addToOverlay(cbOl, cbBg); }
                                            if (texCheck)
                                            {
                                                FStaticConstructObjectParameters chkP(imageClass, outer);
                                                UObject* chkImg = UObjectGlobals::StaticConstructObject(chkP);
                                                if (chkImg)
                                                {
                                                    umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                                    umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                                    addToOverlay(cbOl, chkImg);
                                                    m_ftPeaceCheckImg = chkImg;
                                                    // Start hidden if peace mode is off
                                                    auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = m_peaceModeEnabled ? 0 : 1; safeProcessEvent(chkImg, visFn, vp); }
                                                }
                                            }
                                            m_ftPeaceCheckBoxOl = cbOl;
                                            UObject* cbSlot = addToHBox(pmRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }

                                    // Label
                                    UObject* pmLabel = makeTB(STR("Peace Mode"), 0.86f, 0.90f, 0.96f, 1.0f, 24);
                                    if (pmLabel)
                                    {
                                        UObject* ls = addToHBox(pmRow, pmLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }

                                    // Button: PEACE (green) when enabled, FIGHT (red/orange) when disabled
                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg)
                                            {
                                                umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn);
                                                umgSetBrushSize(kbImg, 400.0f, 128.0f);
                                                addToOverlay(kbOl, kbImg);
                                                m_ftPeaceBtnImg = kbImg;
                                            }
                                            bool on = m_peaceModeEnabled;
                                            UObject* kbLabel = makeTB(on ? L"PEACE" : L"FIGHT",
                                                                       on ? 0.31f : 0.9f,
                                                                       on ? 0.86f : 0.45f,
                                                                       on ? 0.47f : 0.25f,
                                                                       1.0f, 24);
                                            if (kbLabel)
                                            {
                                                umgSetBold(kbLabel);
                                                m_ftPeaceBtnLabel = kbLabel;
                                                UObject* ks = addToOverlay(kbOl, kbLabel);
                                                if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                            }
                                            UObject* kbSlot = addToHBox(pmRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                        }
                                    }

                                    addToVBox(t4, pmRow);
                                }
                            }

                            // v6.4.1 — populate the cheat entries table (Clear All + categories + toggles)
                            // IMPORTANT: m_buffStates persists across F12 open/close cycles so toggles
                            // stay "on" when the player reopens the panel. Only widget-ref arrays
                            // (which point to freshly-built widgets) are reset each rebuild.
                            int nEntries = 0;
                            const CheatEntry* entries = cheatEntries(nEntries);
                            if ((int)m_buffStates.size() != nEntries)
                                m_buffStates.assign(nEntries, false);
                            m_ftBuffCheckImgs.assign(nEntries, nullptr);
                            m_ftBuffBtnLabels.assign(nEntries, nullptr);
                            m_buffRowTopYs.assign(nEntries, 0);
                            m_buffRowHeights.assign(nEntries, 0);

                            // Running Y offset within the tab content (relative to top of cheat entries block,
                            // which itself lives at Y=384 past the 3 existing rows).
                            int yCursor = 384;  // Unlock(128) + Read(128) + Peace(128) = 384 px before this block

                            for (int i = 0; i < nEntries; ++i)
                            {
                                const CheatEntry& e = entries[i];
                                m_buffRowTopYs[i] = yCursor;

                                if (e.kind == CheatRowKind::SectionHeader)
                                {
                                    int h = 80;
                                    m_buffRowHeights[i] = h;

                                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                    if (secOl)
                                    {
                                        if (texSectionBg)
                                        {
                                            FStaticConstructObjectParameters secImgP(imageClass, outer);
                                            UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                            if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                        }
                                        UObject* secLabel = makeTB(e.label, 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                        addToVBox(t4, secOl);
                                    }
                                    yCursor += h;
                                }
                                else if (e.kind == CheatRowKind::ClearAllBtn)
                                {
                                    int h = 128;
                                    m_buffRowHeights[i] = h;

                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* row = UObjectGlobals::StaticConstructObject(rowP);
                                    if (row)
                                    {
                                        UObject* lbl = makeTB(e.label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                        if (lbl)
                                        {
                                            UObject* ls = addToHBox(row, lbl);
                                            if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                        }
                                        if (texKeyBox)
                                        {
                                            FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                            UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                            if (kbOl)
                                            {
                                                FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                                UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                                if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                                UObject* kbLabel = makeTB(STR("CLEAR"), 0.95f, 0.4f, 0.4f, 1.0f, 24);
                                                if (kbLabel) { umgSetBold(kbLabel); UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                                UObject* kbSlot = addToHBox(row, kbOl);
                                                if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                            }
                                        }
                                        addToVBox(t4, row);
                                    }
                                    yCursor += h;
                                }
                                else  // BuffToggle — checkbox + label + ON/OFF button
                                {
                                    int h = 128;
                                    m_buffRowHeights[i] = h;

                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* row = UObjectGlobals::StaticConstructObject(rowP);
                                    if (row)
                                    {
                                        // Checkbox overlay (diamond + optional check mark)
                                        if (texCB)
                                        {
                                            FStaticConstructObjectParameters olP(overlayClass, outer);
                                            UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                                            if (cbOl)
                                            {
                                                FStaticConstructObjectParameters imgP(imageClass, outer);
                                                UObject* cbBg = UObjectGlobals::StaticConstructObject(imgP);
                                                if (cbBg) { umgSetBrushNoMatch(cbBg, texCB, setBrushFn); umgSetBrushSize(cbBg, 80.0f, 80.0f); addToOverlay(cbOl, cbBg); }
                                                if (texCheck)
                                                {
                                                    FStaticConstructObjectParameters chkP(imageClass, outer);
                                                    UObject* chkImg = UObjectGlobals::StaticConstructObject(chkP);
                                                    if (chkImg)
                                                    {
                                                        umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                                        umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                                        addToOverlay(cbOl, chkImg);
                                                        m_ftBuffCheckImgs[i] = chkImg;
                                                        // Reflect current state (persisted across F12 rebuilds)
                                                        auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                        if (visFn) { uint8_t vp[8]{}; vp[0] = m_buffStates[i] ? 0 : 1; safeProcessEvent(chkImg, visFn, vp); }
                                                    }
                                                }
                                                UObject* cbSlot = addToHBox(row, cbOl);
                                                if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                            }
                                        }

                                        // Label
                                        UObject* lbl = makeTB(e.label, 0.86f, 0.90f, 0.96f, 1.0f, 24);
                                        if (lbl)
                                        {
                                            UObject* ls = addToHBox(row, lbl);
                                            if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                        }

                                        // ON/OFF button
                                        if (texKeyBox)
                                        {
                                            FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                            UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                            if (kbOl)
                                            {
                                                FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                                UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                                if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                                bool bOn = m_buffStates[i];
                                                UObject* kbLabel = makeTB(bOn ? STR("ON") : STR("OFF"),
                                                                          bOn ? 0.31f : 0.7f,
                                                                          bOn ? 0.86f : 0.3f,
                                                                          bOn ? 0.47f : 0.3f, 1.0f, 24);
                                                if (kbLabel)
                                                {
                                                    umgSetBold(kbLabel);
                                                    m_ftBuffBtnLabels[i] = kbLabel;
                                                    UObject* ks = addToOverlay(kbOl, kbLabel);
                                                    if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                                }
                                                UObject* kbSlot = addToHBox(row, kbOl);
                                                if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                            }
                                        }

                                        addToVBox(t4, row);
                                    }
                                    yCursor += h;
                                }
                            }
                            m_cheatsContentTotalHeight = yCursor;

                            VLOG(STR("[MoriaCppMod] [Settings] Cheats tab populated ({} entries, {} px tall)\n"),
                                 nEntries + 3, m_cheatsContentTotalHeight);
                        }


                        // v6.4.1 — Tweaks tab (index 5). Same row layout as Cheats: label + cycling value button.
                        // Each row is 128 tall (button). Category headers are 80 tall.
                        if (m_ftTabContent[5])
                        {
                            UObject* t5 = m_ftTabContent[5];

                            int nTweaks = 0;
                            const TweakEntry* tweaks = tweakEntries(nTweaks);

                            // Persist cycle indices across F12 rebuilds; widget refs reset each rebuild.
                            if ((int)m_tweakCurrentIdx.size() != nTweaks)
                                m_tweakCurrentIdx.assign(nTweaks, 0);
                            m_ftTweakBtnLabels.assign(nTweaks, nullptr);
                            m_tweakRowTopYs.assign(nTweaks, 0);
                            m_tweakRowHeights.assign(nTweaks, 0);

                            int yCursor = 0;
                            for (int i = 0; i < nTweaks; ++i)
                            {
                                const TweakEntry& e = tweaks[i];
                                m_tweakRowTopYs[i] = yCursor;

                                if (e.kind == TweakKind::SectionHeader)
                                {
                                    int h = 80;
                                    m_tweakRowHeights[i] = h;
                                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                    if (secOl)
                                    {
                                        if (texSectionBg)
                                        {
                                            FStaticConstructObjectParameters secImgP(imageClass, outer);
                                            UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                            if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                        }
                                        UObject* secLabel = makeTB(e.label, 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                        addToVBox(t5, secOl);
                                    }
                                    yCursor += h;
                                }
                                else  // TweakRow / SpecialNoCost / SpecialInstantCraft — label + cycling value button
                                {
                                    int h = 128;
                                    m_tweakRowHeights[i] = h;
                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* row = UObjectGlobals::StaticConstructObject(rowP);
                                    if (row)
                                    {
                                        UObject* lbl = makeTB(e.label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                        if (lbl)
                                        {
                                            UObject* ls = addToHBox(row, lbl);
                                            if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                        }

                                        if (texKeyBox)
                                        {
                                            FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                            UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                            if (kbOl)
                                            {
                                                FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                                UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                                if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }

                                                // Initial button text based on current cycle index.
                                                // Index 0 = DEFAULT (always). Otherwise format depends on kind.
                                                int ci = m_tweakCurrentIdx[i];
                                                if (ci < 0 || ci >= (int)e.cycleValues.size()) ci = 0;
                                                bool isDef = (ci == 0);
                                                int initVal = e.cycleValues.empty() ? 0 : e.cycleValues[ci];
                                                wchar_t textBuf[32];
                                                if (isDef)
                                                    swprintf(textBuf, 32, L"DEFAULT");
                                                else if (e.kind == TweakKind::SpecialNoCost ||
                                                         e.kind == TweakKind::SpecialInstantCraft)
                                                    swprintf(textBuf, 32, L"ON");
                                                else if (e.isMultiplier)
                                                    swprintf(textBuf, 32, L"%dx", initVal);
                                                else
                                                    swprintf(textBuf, 32, L"%d", initVal);

                                                float r = isDef ? 0.7f  : 0.31f;
                                                float g = isDef ? 0.7f  : 0.86f;
                                                float b = isDef ? 0.55f : 0.47f;
                                                UObject* kbLabel = makeTB(textBuf, r, g, b, 1.0f, 24);
                                                if (kbLabel)
                                                {
                                                    umgSetBold(kbLabel);
                                                    m_ftTweakBtnLabels[i] = kbLabel;
                                                    UObject* ks = addToOverlay(kbOl, kbLabel);
                                                    if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                                }
                                                UObject* kbSlot = addToHBox(row, kbOl);
                                                if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                            }
                                        }
                                        addToVBox(t5, row);
                                    }
                                    yCursor += h;
                                }
                            }

                            VLOG(STR("[MoriaCppMod] [Settings] Tweaks tab populated ({} entries, {} px tall)\n"),
                                 nTweaks, yCursor);
                        }


                        UObject* tab0Content = m_ftTabContent[0];
                        const wchar_t* lastSection = nullptr;
                        for (int b = 0; b < BIND_COUNT; b++)
                        {
                            if (s_bindings[b].label == L"Reserved") continue;

                            if (s_bindings[b].section == L"Game Options") continue;


                            if (!lastSection || s_bindings[b].section != lastSection)
                            {
                                lastSection = s_bindings[b].section.c_str();
                                if (texSectionBg)
                                {
                                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                    if (secOl)
                                    {
                                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                        if (secImg)
                                        {
                                            umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn);
                                            umgSetBrushSize(secImg, 900.0f, 80.0f);
                                            addToOverlay(secOl, secImg);
                                        }
                                        UObject* secLabel = makeTB(lastSection, 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                        if (secLabel)
                                        {
                                            umgSetBold(secLabel);
                                            UObject* ts = addToOverlay(secOl, secLabel);
                                            if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); }
                                        }
                                        addChildToPanel(tab0Content ? tab0Content : scrollBox, STR("AddChild"), secOl);
                                    }
                                }
                            }


                            FStaticConstructObjectParameters rowHbP(hboxClass, outer);
                            UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowHbP);
                            if (!rowHBox) continue;


                            if (texCB)
                            {
                                FStaticConstructObjectParameters cbOlP(overlayClass, outer);
                                UObject* cbOl = UObjectGlobals::StaticConstructObject(cbOlP);
                                if (cbOl)
                                {
                                    FStaticConstructObjectParameters cbImgP(imageClass, outer);
                                    UObject* cbBg = UObjectGlobals::StaticConstructObject(cbImgP);
                                    if (cbBg)
                                    {
                                        umgSetBrushNoMatch(cbBg, texCB, setBrushFn);
                                        umgSetBrushSize(cbBg, 80.0f, 80.0f);
                                        addToOverlay(cbOl, cbBg);
                                    }
                                    if (texCheck)
                                    {
                                        FStaticConstructObjectParameters chkImgP(imageClass, outer);
                                        UObject* chkImg = UObjectGlobals::StaticConstructObject(chkImgP);
                                        if (chkImg)
                                        {
                                            umgSetBrushNoMatch(chkImg, texCheck, setBrushFn);
                                            umgSetBrushSize(chkImg, 80.0f, 80.0f);
                                            addToOverlay(cbOl, chkImg);
                                            m_ftCheckImages[b] = chkImg;

                                            if (!s_bindings[b].enabled)
                                            {
                                                auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                if (visFn) { uint8_t vp[8]{}; vp[0] = 2; safeProcessEvent(chkImg, visFn, vp); }
                                            }
                                        }
                                    }
                                    UObject* cbSlot = addToHBox(rowHBox, cbOl);
                                    if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                }
                            }


                            UObject* bindLabel = makeTB(s_bindings[b].label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                            if (bindLabel)
                            {
                                UObject* lblSlot = addToHBox(rowHBox, bindLabel);
                                if (lblSlot)
                                {
                                    umgSetSlotSize(lblSlot, 1.0f, 1);
                                    umgSetSlotPadding(lblSlot, 0.0f, 24.0f, 0.0f, 24.0f);
                                    umgSetVAlign(lblSlot, 2);
                                }
                            }


                            if (texKeyBox)
                            {
                                FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                if (kbOl)
                                {
                                    FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                    UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                    if (kbImg)
                                    {
                                        umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn);
                                        umgSetBrushSize(kbImg, 400.0f, 128.0f);
                                        addToOverlay(kbOl, kbImg);
                                    }
                                    UObject* kbLabel = makeTB(keyName(s_bindings[b].key), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                    if (kbLabel)
                                    {
                                        m_ftKeyBoxLabels[b] = kbLabel;
                                        UObject* ks = addToOverlay(kbOl, kbLabel);
                                        if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); }
                                    }
                                    UObject* kbSlot = addToHBox(rowHBox, kbOl);
                                    if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                }
                            }

                            addChildToPanel(tab0Content ? tab0Content : scrollBox, STR("AddChild"), rowHBox);
                        }


                        {
                            FStaticConstructObjectParameters modHbP(hboxClass, outer);
                            UObject* modRow = UObjectGlobals::StaticConstructObject(modHbP);
                            if (modRow)
                            {
                                UObject* modLabel = makeTB(Loc::get("ui.set_modifier_key_short"), 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                if (modLabel)
                                {
                                    UObject* ls = addToHBox(modRow, modLabel);
                                    if (ls)
                                    {
                                        umgSetSlotSize(ls, 1.0f, 1);
                                        umgSetSlotPadding(ls, 92.0f, 24.0f, 0.0f, 24.0f);
                                        umgSetVAlign(ls, 2);
                                    }
                                }
                                if (texKeyBox)
                                {
                                    FStaticConstructObjectParameters mkOlP(overlayClass, outer);
                                    UObject* mkOl = UObjectGlobals::StaticConstructObject(mkOlP);
                                    if (mkOl)
                                    {
                                        FStaticConstructObjectParameters mkImgP(imageClass, outer);
                                        UObject* mkImg = UObjectGlobals::StaticConstructObject(mkImgP);
                                        if (mkImg)
                                        {
                                            umgSetBrushNoMatch(mkImg, texKeyBox, setBrushFn);
                                            umgSetBrushSize(mkImg, 400.0f, 128.0f);
                                            addToOverlay(mkOl, mkImg);
                                        }
                                        UObject* mkLabel = makeTB(std::wstring(modifierName(s_modifierVK)), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                        if (mkLabel)
                                        {
                                            m_ftModBoxLabel = mkLabel;
                                            UObject* ms = addToOverlay(mkOl, mkLabel);
                                            if (ms) { umgSetHAlign(ms, 2); umgSetVAlign(ms, 2); }
                                        }
                                        UObject* mkSlot = addToHBox(modRow, mkOl);
                                        if (mkSlot) umgSetSlotPadding(mkSlot, 0.0f, 0.0f, 50.0f, 0.0f);
                                    }
                                }
                                addChildToPanel(tab0Content ? tab0Content : scrollBox, STR("AddChild"), modRow);
                            }
                        }

                        VLOG(STR("[MoriaCppMod] [Settings] Keybinding rows populated\n"));
                    }
                }
            }


            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 200;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1540.0f; v[1] = 880.0f;  // wider than original 1440 for scrollbar spacing
                    safeProcessEvent(userWidget, setDesiredSizeFn, sb.data());
                }
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f;
                    safeProcessEvent(userWidget, setAlignFn, al.data());
                }
            }


            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f),
                                          m_screen.fracToPixelY(0.5f), true);

            m_fontTestWidget = userWidget;
            m_ftSelectedTab = 0;
            m_ftVisible = true;
            setInputModeUI(userWidget);

            updateFtNoCollision();
            updateFtGameOptCheckboxes();
            showOnScreen(Loc::get("msg.settings_opened"), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [Settings] Panel created and displayed\n"));
        }


        void selectFontTestTab(int tab)
        {
            if (tab < 0 || tab >= CONFIG_TAB_COUNT) return;
            if (tab == m_ftSelectedTab) return;
            m_ftSelectedTab = tab;

            auto* sBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));

            for (int i = 0; i < CONFIG_TAB_COUNT; i++)
            {

                if (m_ftTabImages[i] && sBrushFn)
                {
                    UObject* tex = (i == tab) ? m_ftTabActiveTexture : m_ftTabInactiveTexture;
                    if (tex) umgSetBrushNoMatch(m_ftTabImages[i], tex, sBrushFn);
                }

                if (m_ftTabLabels[i])
                {
                    if (i == tab)
                        umgSetTextColor(m_ftTabLabels[i], 0.9f, 0.88f, 0.78f, 1.0f);
                    else
                        umgSetTextColor(m_ftTabLabels[i], 0.55f, 0.55f, 0.65f, 0.7f);
                }
            }

            // Switch tabs by toggling visibility — NOT ClearChildren/AddChild.
            // ClearChildren destroys child widgets while MovieScene animation sequences
            // are still registered with UUMGSequenceTickManager, causing a crash in
            // TickWidgetAnimations when it tries to process stale sequences.
            for (int i = 0; i < CONFIG_TAB_COUNT; i++)
            {
                if (m_ftTabContent[i] && isObjectAlive(m_ftTabContent[i]))
                    setWidgetVisibility(m_ftTabContent[i], (i == tab) ? 0 : 1); // 0=Visible, 1=Collapsed
            }

            if (m_ftScrollBox)
            {
                auto* setScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("SetScrollOffset"));
                if (setScrollFn)
                {
                    auto* pOff = findParam(setScrollFn, STR("NewScrollOffset"));
                    if (pOff) { int sz = setScrollFn->GetParmsSize(); std::vector<uint8_t> sp(sz, 0); *reinterpret_cast<float*>(sp.data() + pOff->GetOffset_Internal()) = 0.0f; safeProcessEvent(m_ftScrollBox, setScrollFn, sp.data()); }
                }
            }

            if (s_capturingBind >= 0)
            {
                s_capturingBind = -1;
                updateFontTestKeyLabels();
            }
        }


        void updateFontTestKeyLabels()
        {
            int capturing = s_capturingBind.load();
            for (int i = 0; i < BIND_COUNT; i++)
            {
                if (!m_ftKeyBoxLabels[i] || !isObjectAlive(m_ftKeyBoxLabels[i])) { m_ftKeyBoxLabels[i] = nullptr; continue; }
                if (capturing == i)
                {
                    umgSetText(m_ftKeyBoxLabels[i], Loc::get("ui.press_key"));
                    umgSetTextColor(m_ftKeyBoxLabels[i], 1.0f, 0.9f, 0.0f, 1.0f);
                }
                else
                {
                    umgSetText(m_ftKeyBoxLabels[i], keyName(s_bindings[i].key));
                    umgSetTextColor(m_ftKeyBoxLabels[i], 1.0f, 1.0f, 1.0f, 1.0f);
                }
            }
            if (m_ftModBoxLabel)
                umgSetText(m_ftModBoxLabel, std::wstring(modifierName(s_modifierVK)));
        }


        // v6.4.1 Tweaks tab: format the button text for a tweak row based on its current cycle index.
        // Index 0 is always "DEFAULT". For other indices, format depends on kind.
        void updateTweakRowUI(int idx)
        {
            if (idx < 0 || idx >= (int)m_tweakCurrentIdx.size()) return;
            if (idx >= (int)m_ftTweakBtnLabels.size() || !m_ftTweakBtnLabels[idx]) return;

            int count = 0;
            const TweakEntry* all = tweakEntries(count);
            if (idx >= count) return;
            const TweakEntry& e = all[idx];
            if (e.cycleValues.empty()) return;

            int ci = m_tweakCurrentIdx[idx];
            if (ci < 0 || ci >= (int)e.cycleValues.size()) ci = 0;
            bool isDefault = (ci == 0);
            int val = e.cycleValues[ci];

            wchar_t text[32];
            if (isDefault)
            {
                swprintf(text, 32, L"DEFAULT");
            }
            else if (e.kind == TweakKind::SpecialNoCost ||
                     e.kind == TweakKind::SpecialInstantCraft)
            {
                swprintf(text, 32, L"ON");
            }
            else if (e.isMultiplier)
            {
                swprintf(text, 32, L"%dx", val);
            }
            else
            {
                swprintf(text, 32, L"%d", val);
            }

            umgSetText(m_ftTweakBtnLabels[idx], text);
            if (isDefault)
                umgSetTextColor(m_ftTweakBtnLabels[idx], 0.7f, 0.7f, 0.55f, 1.0f);
            else
                umgSetTextColor(m_ftTweakBtnLabels[idx], 0.31f, 0.86f, 0.47f, 1.0f);
        }

        // v6.4.1 Cheats tab: sync one buff row's checkbox icon + button text/color with m_buffStates[idx].
        void updateBuffRowUI(int idx)
        {
            if (idx < 0 || idx >= (int)m_buffStates.size()) return;
            bool on = m_buffStates[idx];
            if (idx < (int)m_ftBuffCheckImgs.size() && m_ftBuffCheckImgs[idx])
            {
                auto* visFn = m_ftBuffCheckImgs[idx]->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; safeProcessEvent(m_ftBuffCheckImgs[idx], visFn, p); }
            }
            if (idx < (int)m_ftBuffBtnLabels.size() && m_ftBuffBtnLabels[idx])
            {
                umgSetText(m_ftBuffBtnLabels[idx], on ? L"ON" : L"OFF");
                umgSetTextColor(m_ftBuffBtnLabels[idx],
                                on ? 0.31f : 0.7f,
                                on ? 0.86f : 0.3f,
                                on ? 0.47f : 0.3f, 1.0f);
            }
        }

        // v6.4.1 Peace Mode: sync checkbox visibility + button text/color with m_peaceModeEnabled.
        void updateFtPeaceMode()
        {
            bool on = m_peaceModeEnabled;
            if (m_ftPeaceCheckImg)
            {
                auto* visFn = m_ftPeaceCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; safeProcessEvent(m_ftPeaceCheckImg, visFn, p); }
            }
            if (m_ftPeaceBtnLabel)
            {
                umgSetText(m_ftPeaceBtnLabel, on ? L"PEACE" : L"FIGHT");
                // Green when PEACE on, red/orange when FIGHT (off)
                umgSetTextColor(m_ftPeaceBtnLabel,
                                on ? 0.31f : 0.9f,
                                on ? 0.86f : 0.45f,
                                on ? 0.47f : 0.25f,
                                1.0f);
            }
        }

        void updateFtNoCollision()
        {
            bool on = m_noCollisionWhileFlying;
            if (m_ftNoCollisionCheckImg)
            {
                auto* visFn = m_ftNoCollisionCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; safeProcessEvent(m_ftNoCollisionCheckImg, visFn, p); }
            }
            if (m_ftNoCollisionKeyLabel)
            {
                umgSetText(m_ftNoCollisionKeyLabel, on ? L"ON" : L"OFF");
                umgSetTextColor(m_ftNoCollisionKeyLabel, on ? 0.31f : 0.7f, on ? 0.86f : 0.3f, on ? 0.47f : 0.3f, 1.0f);
            }
        }

        void updateFtGameOptCheckboxes()
        {
            struct { UObject* img; bool on; } items[] = {
                { m_ftTrashCheckImg,      m_trashItemEnabled },
                { m_ftReplenishCheckImg,  m_replenishItemEnabled },
                { m_ftRemoveAttrsCheckImg,m_removeAttrsEnabled },
                { m_ftPitchCheckImg,      m_pitchRotateEnabled },
                { m_ftRollCheckImg,       m_rollRotateEnabled },
            };
            for (auto& item : items)
            {
                if (!item.img) continue;
                auto* visFn = item.img->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = item.on ? 0 : 1; safeProcessEvent(item.img, visFn, p); }
            }
        }


        ULONGLONG m_lastSaveTime{0};

        // v6.20.13 — Reverted UI_WBP_NotificationFeed approach. The feed's
        // UI_WBP_Notification_Generic_C BP renders item/recipe/lore-specific
        // layouts and won't display arbitrary text via SetData. v6.20.4-12
        // confirmed the technical pieces fired (feed found, class resolved,
        // notification parented to grid, ShowNotification UFunction called)
        // but nothing visible — the BP-side render path for text-only is
        // empty. Now showGameNotification is a thin wrapper around the
        // restyled showInfoMessage (game-gold on dark transparent), which
        // works reliably in pause AND in-world.
        // v6.20.13 — Thin wrapper around restyled showInfoMessage.
        // Body parameter is appended to title with a newline if present.
        // Duration is ignored (showInfoMessage has its own auto-hide).
        // The UI_WBP_NotificationFeed approach was abandoned in v6.20.13;
        // see v6.20.13 commit message for details (the feed's notification
        // BP renders item/recipe/lore-specific layouts only, not arbitrary
        // text). The home-rolled error-box was restyled (gold-on-dark) to
        // match the game's notification visual language.
        void showGameNotification(const std::wstring& title,
                                  const std::wstring& body = L"",
                                  float /*duration*/ = 3.0f)
        {
            std::wstring msg = body.empty() ? title : (title + L"\n" + body);
            showInfoMessage(msg);
        }
        void triggerSaveGame()
        {
            // Cooldown: prevent double-trigger (10s minimum between saves)
            ULONGLONG now = GetTickCount64();
            if (now - m_lastSaveTime < 10000)
            {
                showGameNotification(L"Save: please wait...", L"", 2.0f);
                return;
            }

            // MP fix: use local pawn, not first dwarf in the world
            UObject* pawn = getPawn();
            if (!pawn)
            {
                showErrorBox(L"Save: no player character");
                return;
            }

            // Check if save system is valid via blueprint library
            auto* validFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                STR("/Script/Moria.MorSaveSystemBlueprintLibrary:IsSaveSystemWorldStateValid"));
            auto* libCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                STR("/Script/Moria.Default__MorSaveSystemBlueprintLibrary"));
            if (validFn && libCDO)
            {
                struct { bool ReturnValue{false}; } vp{};
                safeProcessEvent(libCDO, validFn, &vp);
                if (!vp.ReturnValue)
                {
                    showErrorBox(L"Save: system not ready");
                    VLOG(STR("[MoriaCppMod] [Save] IsSaveSystemWorldStateValid returned false\n"));
                    return;
                }
            }

            // Use ServerAutoSave on MorCheatsComponent (Server RPC — safest path)
            UObject* cheatsComp = findActorComponentByClass(pawn, STR("MorCheatsComponent"));
            if (!cheatsComp)
            {
                VLOG(STR("[MoriaCppMod] [Save] MorCheatsComponent not found, trying CheatManager\n"));
                // Fallback: try CheatManager on PlayerController
                auto* pc = findPlayerController();
                if (pc)
                {
                    auto* cmFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                        STR("/Script/Moria.MorCheatManager:SaveSystemAutoSave"));
                    auto* cm = pc->GetValuePtrByPropertyNameInChain<UObject*>(STR("CheatManager"));
                    if (cmFn && cm && *cm)
                    {
                        safeProcessEvent((*cm), cmFn, nullptr);
                        m_lastSaveTime = now;
                        showGameNotification(L"Game Saved", L"", 3.0f);
                        VLOG(STR("[MoriaCppMod] [Save] Triggered via CheatManager::SaveSystemAutoSave\n"));
                        return;
                    }
                }
                showErrorBox(L"Save: no save component found");
                return;
            }

            auto* saveFn = cheatsComp->GetFunctionByNameInChain(STR("ServerAutoSave"));
            if (!saveFn)
            {
                showErrorBox(L"Save: ServerAutoSave not found");
                return;
            }

            safeProcessEvent(cheatsComp, saveFn, nullptr);
            m_lastSaveTime = now;
            showGameNotification(L"Game Saved", L"", 3.0f);
            VLOG(STR("[MoriaCppMod] [Save] Triggered via MorCheatsComponent::ServerAutoSave\n"));
        }


        void showRenameDialog()
        {
            if (m_ftRenameVisible) { VLOG(STR("[MoriaCppMod] [Rename] BLOCKED: already visible\n")); return; }
            if (!m_characterLoaded) { showErrorBox(Loc::get("err.character_not_loaded")); return; }
            VLOG(STR("[MoriaCppMod] [Rename] Starting dialog creation...\n"));

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass      = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* overlayClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* hboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass       = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass  = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* sizeBoxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* editBoxClass    = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.EditableTextBox"));
            if (!userWidgetClass || !imageClass || !overlayClass || !hboxClass || !vboxClass ||
                !textBlockClass || !sizeBoxClass || !editBoxClass) { showErrorBox(L"Rename: missing UMG classes"); return; }

            auto* pc = findPlayerController();
            if (!pc) { showErrorBox(L"Rename: no PlayerController"); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: createFn={} wblClass={}\n"), (void*)createFn, (void*)wblClass); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: wblCDO is null\n")); return; }

            int sz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(sz, 0);
            auto* pOwner = findParam(createFn, STR("WorldContextObject"));
            auto* pClass = findParam(createFn, STR("WidgetType"));
            auto* pOP    = findParam(createFn, STR("OwningPlayer"));
            auto* pRet   = findParam(createFn, STR("ReturnValue"));
            if (!pOwner || !pClass || !pRet) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: pOwner={} pClass={} pRet={}\n"), (void*)pOwner, (void*)pClass, (void*)pRet); return; }
            *reinterpret_cast<UObject**>(cp.data() + pOwner->GetOffset_Internal()) = pc;
            *reinterpret_cast<UObject**>(cp.data() + pClass->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            safeProcessEvent(wblCDO, createFn, cp.data());
            UObject* userWidget = pRet ? *reinterpret_cast<UObject**>(cp.data() + pRet->GetOffset_Internal()) : nullptr;
            if (!userWidget) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: userWidget is null\n")); return; }
            VLOG(STR("[MoriaCppMod] [Rename] CP1: userWidget created\n"));

            UObject* outer = userWidget;
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = (wtSlot && *wtSlot) ? *wtSlot : nullptr;

            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));


            UObject* texBG = findTexture2DByName(L"T_UI_Pnl_Background_Base");
            UObject* texSectionBg = findTexture2DByName(L"T_UI_Pnl_TabSelected");

            VLOG(STR("[MoriaCppMod] [Rename] CP2: textures done\n"));

            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOl = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOl) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: rootOl is null\n")); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOl);


            {
                auto* setClipFn = rootOl->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz2 = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz2, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; safeProcessEvent(rootOl, setClipFn, cp2.data()); }
            }

            VLOG(STR("[MoriaCppMod] [Rename] CP3: rootOl + clipping done\n"));
            const float dlgW = 700.0f, dlgH = 220.0f;


            {
                FStaticConstructObjectParameters bfP(imageClass, outer);
                UObject* bf = UObjectGlobals::StaticConstructObject(bfP);
                if (bf) { umgSetBrushSize(bf, dlgW, dlgH); umgSetImageColor(bf, 0.08f, 0.14f, 0.32f, 1.0f); addToOverlay(rootOl, bf); }
            }


            if (texBG && setBrushFn)
            {
                FStaticConstructObjectParameters bgP(imageClass, outer);
                UObject* bg = UObjectGlobals::StaticConstructObject(bgP);
                if (bg)
                {
                    umgSetBrush(bg, texBG, setBrushFn);
                    umgSetBrushSize(bg, dlgW - 2.0f, dlgH - 2.0f);
                    umgSetOpacity(bg, 1.0f);
                    UObject* s = addToOverlay(rootOl, bg);
                    if (s) { umgSetHAlign(s, 2); umgSetVAlign(s, 2); }
                }
            }


            FStaticConstructObjectParameters cvP(vboxClass, outer);
            UObject* contentVBox = UObjectGlobals::StaticConstructObject(cvP);
            if (contentVBox)
            {
                UObject* cvSlot = addToOverlay(rootOl, contentVBox);
                if (cvSlot) umgSetSlotPadding(cvSlot, 20.0f, 10.0f, 20.0f, 10.0f);


                if (texSectionBg && setBrushFn)
                {
                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                    if (secOl)
                    {
                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, dlgW - 40.0f, 50.0f); addToOverlay(secOl, secImg); }
                        UObject* secLabel = createTextBlock(Loc::get("ui.rename_character"), 0.78f, 0.86f, 1.0f, 1.0f, 24);
                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                        addToVBox(contentVBox, secOl);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP4: header section done\n"));

                {
                    std::wstring currentName = L"(unknown)";

                    std::vector<UObject*> mgrs;
                    findAllOfSafe(STR("CustomizationManager"), mgrs);
                    UObject* pawn = getPawn();
                    UObject* target = nullptr;
                    for (auto* mgr : mgrs) { if (!mgr) continue; if (mgr->GetOuterPrivate() == pawn) { target = mgr; break; } }
                    if (!target && !mgrs.empty()) target = mgrs[0];
                    VLOG(STR("[MoriaCppMod] [Rename] CP4a: FindAllOf done, target={}\n"), (void*)target);
                    if (target)
                    {
                        auto* getFn = target->GetFunctionByNameInChain(STR("GetCharacterName"));
                        if (getFn)
                        {

                            int gsz = getFn->GetParmsSize();
                            std::vector<uint8_t> gbuf(gsz, 0);
                            safeProcessEvent(target, getFn, gbuf.data());
                            auto* retProp = findParam(getFn, STR("ReturnValue"));
                            if (retProp)
                            {

                                uintptr_t strPtr = *reinterpret_cast<uintptr_t*>(gbuf.data() + retProp->GetOffset_Internal());
                                int32_t strLen = *reinterpret_cast<int32_t*>(gbuf.data() + retProp->GetOffset_Internal() + 8);
                                if (strPtr && strLen > 0)
                                    currentName = reinterpret_cast<const wchar_t*>(strPtr);
                            }
                        }
                    }

                    FStaticConstructObjectParameters nameHbP(hboxClass, outer);
                    UObject* nameRow = UObjectGlobals::StaticConstructObject(nameHbP);
                    if (nameRow)
                    {
                        UObject* curLbl = createTextBlock(Loc::get("ui.current_name_prefix"), 0.55f, 0.55f, 0.6f, 0.8f, 22);
                        if (curLbl) { UObject* ls = addToHBox(nameRow, curLbl); if (ls) { umgSetSlotPadding(ls, 0.0f, 10.0f, 0.0f, 5.0f); umgSetVAlign(ls, 2); } }
                        UObject* nameLbl = createTextBlock(currentName, 0.9f, 0.75f, 0.2f, 1.0f, 22);
                        if (nameLbl) { umgSetBold(nameLbl); UObject* ls = addToHBox(nameRow, nameLbl); if (ls) { umgSetSlotPadding(ls, 0.0f, 10.0f, 0.0f, 5.0f); umgSetVAlign(ls, 2); } }
                        addToVBox(contentVBox, nameRow);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP5: current name label done\n"));

                {
                    VLOG(STR("[MoriaCppMod] [Rename] CP5a: about to create EditableTextBox...\n"));
                    FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
                    UObject* editSizeBox = UObjectGlobals::StaticConstructObject(sbP);
                    FStaticConstructObjectParameters etP(editBoxClass, outer);
                    UObject* editBox = UObjectGlobals::StaticConstructObject(etP);
                    VLOG(STR("[MoriaCppMod] [Rename] CP5b: EditableTextBox created sb={} eb={}\n"), (void*)editSizeBox, (void*)editBox);
                    if (editSizeBox && editBox)
                    {
                        m_ftRenameInput = editBox;

                        {
                            UObject* defaultFont = nullptr;
                            std::vector<UObject*> fonts;
                            findAllOfSafe(STR("Font"), fonts);
                            for (auto* f : fonts) { if (f && std::wstring(f->GetName()) == L"DefaultRegularFont") { defaultFont = f; break; } }
                            if (defaultFont)
                            {
                                uint8_t* raw = reinterpret_cast<uint8_t*>(editBox);
                                int32_t fontSize = 24;

                                constexpr int STYLE_FONT_OFF = 0x0368;
                                *reinterpret_cast<UObject**>(raw + STYLE_FONT_OFF) = defaultFont;
                                std::memcpy(raw + STYLE_FONT_OFF + fontSizeOff(), &fontSize, sizeof(int32_t));

                                constexpr int EDITBOX_FONT_OFF = 0x0958;
                                *reinterpret_cast<UObject**>(raw + EDITBOX_FONT_OFF) = defaultFont;
                                std::memcpy(raw + EDITBOX_FONT_OFF + fontSizeOff(), &fontSize, sizeof(int32_t));
                            }
                        }

                        auto* setWOvFn = editSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                        if (setWOvFn) { int wsz = setWOvFn->GetParmsSize(); std::vector<uint8_t> wp(wsz, 0); auto* p = findParam(setWOvFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = dlgW - 40.0f; safeProcessEvent(editSizeBox, setWOvFn, wp.data()); }
                        auto* setHOvFn = editSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
                        if (setHOvFn) { int hsz = setHOvFn->GetParmsSize(); std::vector<uint8_t> hp(hsz, 0); auto* p = findParam(setHOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 50.0f; safeProcessEvent(editSizeBox, setHOvFn, hp.data()); }

                        auto* setChildFn = editSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                        if (!setChildFn) setChildFn = editSizeBox->GetFunctionByNameInChain(STR("AddChild"));
                        if (setChildFn) { auto* pChild = findParam(setChildFn, STR("Content")); if (!pChild) pChild = findParam(setChildFn, STR("InContent")); if (pChild) { int csz = setChildFn->GetParmsSize(); std::vector<uint8_t> cbuf(csz, 0); *reinterpret_cast<UObject**>(cbuf.data() + pChild->GetOffset_Internal()) = editBox; safeProcessEvent(editSizeBox, setChildFn, cbuf.data()); } }
                        VLOG(STR("[MoriaCppMod] [Rename] CP5c: SizeBox configured, about to SetHintText...\n"));

                        auto* setHintFn = editBox->GetFunctionByNameInChain(STR("SetHintText"));
                        if (setHintFn)
                        {
                            auto* pHint = findParam(setHintFn, STR("InText"));
                            if (!pHint) pHint = findParam(setHintFn, STR("InHintText"));
                            if (pHint)
                            {
                                int hsz = setHintFn->GetParmsSize();
                                std::vector<uint8_t> hbuf(hsz, 0);
                                FText hintText(STR("Enter new name..."));
                                std::memcpy(hbuf.data() + pHint->GetOffset_Internal(), &hintText, sizeof(FText));
                                safeProcessEvent(editBox, setHintFn, hbuf.data());
                            }
                        }
                        UObject* editSlot = addToVBox(contentVBox, editSizeBox);
                        if (editSlot) umgSetSlotPadding(editSlot, 0.0f, 8.0f, 0.0f, 10.0f);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP6: EditableTextBox section done\n"));

                {
                    FStaticConstructObjectParameters btnHbP(hboxClass, outer);
                    UObject* btnRow = UObjectGlobals::StaticConstructObject(btnHbP);
                    if (btnRow)
                    {

                        {
                            FStaticConstructObjectParameters cOlP(overlayClass, outer);
                            UObject* cancelOl = UObjectGlobals::StaticConstructObject(cOlP);
                            if (cancelOl)
                            {
                                FStaticConstructObjectParameters cImgP(imageClass, outer);
                                UObject* cImg = UObjectGlobals::StaticConstructObject(cImgP);
                                if (cImg) { umgSetBrushSize(cImg, 250.0f, 55.0f); umgSetImageColor(cImg, 0.6f, 0.12f, 0.12f, 1.0f); addToOverlay(cancelOl, cImg); }
                                UObject* cLbl = createTextBlock(Loc::get("ui.button_cancel"), 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cLbl) { umgSetBold(cLbl); UObject* cs = addToOverlay(cancelOl, cLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cSlot = addToHBox(btnRow, cancelOl);
                                if (cSlot) umgSetSlotPadding(cSlot, 20.0f, 0.0f, 30.0f, 0.0f);
                            }
                        }

                        {
                            FStaticConstructObjectParameters cfOlP(overlayClass, outer);
                            UObject* confirmOl = UObjectGlobals::StaticConstructObject(cfOlP);
                            if (confirmOl)
                            {
                                FStaticConstructObjectParameters cfImgP(imageClass, outer);
                                UObject* cfImg = UObjectGlobals::StaticConstructObject(cfImgP);
                                if (cfImg) { umgSetBrushSize(cfImg, 250.0f, 55.0f); umgSetImageColor(cfImg, 0.12f, 0.5f, 0.15f, 1.0f); addToOverlay(confirmOl, cfImg); }
                                UObject* cfLbl = createTextBlock(Loc::get("ui.button_confirm"), 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cfLbl) { umgSetBold(cfLbl); m_ftRenameConfirmLabel = cfLbl; UObject* cs = addToOverlay(confirmOl, cfLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cfSlot = addToHBox(btnRow, confirmOl);
                                if (cfSlot) umgSetSlotPadding(cfSlot, 0.0f, 0.0f, 20.0f, 0.0f);
                            }
                        }
                        UObject* btnSlot = addToVBox(contentVBox, btnRow);
                        if (btnSlot) { umgSetHAlign(btnSlot, 2); }
                    }
                }
            }

            VLOG(STR("[MoriaCppMod] [Rename] CP7: all widgets built, about to AddToViewport...\n"));

            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int avSz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(avSz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 300;
                safeProcessEvent(userWidget, addToViewportFn, vp.data());
            }


            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize) { int ssz = setDesiredSizeFn->GetParmsSize(); std::vector<uint8_t> sb(ssz, 0); auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal()); v[0] = dlgW; v[1] = dlgH; safeProcessEvent(userWidget, setDesiredSizeFn, sb.data()); }
            }


            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn) { auto* pAlign = findParam(setAlignFn, STR("Alignment")); if (pAlign) { int asz = setAlignFn->GetParmsSize(); std::vector<uint8_t> ab(asz, 0); auto* a = reinterpret_cast<float*>(ab.data() + pAlign->GetOffset_Internal()); a[0] = 0.5f; a[1] = 0.5f; safeProcessEvent(userWidget, setAlignFn, ab.data()); } }


            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f), m_screen.fracToPixelY(0.5f), true);

            VLOG(STR("[MoriaCppMod] [Rename] CP8: AddToViewport + sizing done\n"));
            m_ftRenameWidget = userWidget;
            m_ftRenameVisible = true;
            setInputModeUI(userWidget);

            // v6.20.24 — clear pending bit-0 latch on RETURN/ESCAPE/LBUTTON
            // before entering poll loop. Without this, ESCAPE-to-open-pause-menu
            // OR ENTER-to-click-RENAME-button leaves a pending press latched in
            // GetAsyncKeyState; the next poll inside the dialog sees it and
            // immediately closes/confirms the dialog (28ms after open).
            (void)GetAsyncKeyState(VK_RETURN);
            (void)GetAsyncKeyState(VK_ESCAPE);
            (void)GetAsyncKeyState(VK_LBUTTON);

            VLOG(STR("[MoriaCppMod] [Rename] Dialog opened\n"));
        }

        void hideRenameDialog()
        {
            if (!m_ftRenameVisible) return;
            if (m_ftRenameWidget)
            {
                deferRemoveWidget(m_ftRenameWidget);
                m_ftRenameWidget = nullptr;
            }
            m_ftRenameInput = nullptr;
            m_ftRenameConfirmLabel = nullptr;
            m_ftRenameVisible = false;

            // v6.20.25 — if the rename dialog was opened from the pause menu
            // (typical), the pause menu is still on screen and needs UI input.
            // Falling through to setInputModeGame leaves the user locked out
            // (game paused, pause menu visible but non-interactive — only ESC
            // dismisses it). Detect any live pause-menu instance and restore
            // UI mode focused on it.
            if (m_ftVisible && m_fontTestWidget)
            {
                setInputModeUI(m_fontTestWidget);
            }
            else
            {
                std::vector<UObject*> menus;
                findAllOfSafe(STR("UI_WBP_EscapeMenu2_C"), menus);
                UObject* pauseMenu = nullptr;
                for (UObject* m : menus)
                {
                    if (!m || !isObjectAlive(m)) continue;
                    if (isWidgetInViewport(m)) { pauseMenu = m; break; }
                }
                if (pauseMenu)
                    setInputModeUI(pauseMenu);
                else
                    setInputModeGame();
            }
            VLOG(STR("[MoriaCppMod] [Rename] Dialog closed (deferred removal)\n"));
        }

        void confirmRenameDialog()
        {
            if (!m_ftRenameVisible || !m_ftRenameInput) return;


            auto* getFn = m_ftRenameInput->GetFunctionByNameInChain(STR("GetText"));
            if (!getFn) { showErrorBox(L"GetText not found on EditableTextBox"); hideRenameDialog(); return; }

            int gsz = getFn->GetParmsSize();
            std::vector<uint8_t> gbuf(gsz, 0);
            safeProcessEvent(m_ftRenameInput, getFn, gbuf.data());

            auto* retProp = findParam(getFn, STR("ReturnValue"));
            if (!retProp) { hideRenameDialog(); return; }


            std::wstring newName;
            auto* ftext = reinterpret_cast<FText*>(gbuf.data() + retProp->GetOffset_Internal());
            if (ftext->Data)
                newName = ftext->ToString();

            if (newName.empty()) { showErrorBox(Loc::get("err.rename_name_empty")); return; }


            {
                std::scoped_lock lock(m_charNameMutex);
                m_pendingCharName = newName;
            }
            m_pendingCharNameReady.store(true, std::memory_order_release);

            hideRenameDialog();
        }


        void rebuildFtRemovalList()
        {
            if (!m_ftRemovalVBox || !isObjectAlive(m_ftRemovalVBox)) { m_ftRemovalVBox = nullptr; return; }

            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!imageClass || !hboxClass || !vboxClass || !textBlockClass) return;

            UObject* outer = m_ftRemovalVBox->GetOuterPrivate();
            if (!outer) outer = m_ftRemovalVBox;

            // Hide before ClearChildren to prevent Slate PaintFastPath crash
            setWidgetVisibility(m_ftRemovalVBox, 1); // Collapsed
            auto* clearFn = m_ftRemovalVBox->GetFunctionByNameInChain(STR("ClearChildren"));
            if (clearFn) safeProcessEvent(m_ftRemovalVBox, clearFn, nullptr);

            int count = s_config.removalCount.load();
            if (m_ftRemovalHeader)
            {
                umgSetText(m_ftRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
            }

            UObject* texDanger = findTexture2DByName(L"T_UI_Icon_Danger");
            UObject* defaultFont = nullptr;
            {
                std::vector<UObject*> fonts;
                findAllOfSafe(STR("Font"), fonts);
                for (auto* f : fonts) { if (f && std::wstring(f->GetName()) == L"DefaultRegularFont") { defaultFont = f; break; } }
            }

            auto makeTB2 = [&](const std::wstring& text, float r, float g, float b, float a, int32_t size) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                if (defaultFont) umgSetFontAndSize(tb, defaultFont, size);
                else umgSetFontSize(tb, size);
                return tb;
            };

            if (s_config.removalCSInit)
            {
                CriticalSectionLock removalLock(s_config.removalCS);

                // Group entries by bubble, current bubble first
                std::map<std::string, std::vector<size_t>> grouped;
                std::vector<size_t> typeRuleIndices;
                for (size_t i = 0; i < s_config.removalEntries.size(); i++)
                {
                    const auto& entry = s_config.removalEntries[i];
                    if (entry.isTypeRule)
                        typeRuleIndices.push_back(i);
                    else
                    {
                        std::string bId = entry.bubbleId.empty() ? "Unknown" : entry.bubbleId;
                        grouped[bId].push_back(i);
                    }
                }

                // Build ordered list: current bubble first, then rest alphabetically
                std::vector<std::string> bubbleOrder;
                if (!m_currentBubbleId.empty() && grouped.count(m_currentBubbleId))
                    bubbleOrder.push_back(m_currentBubbleId);
                for (auto& [bId, _] : grouped)
                {
                    if (bId != m_currentBubbleId)
                        bubbleOrder.push_back(bId);
                }

                // Helper to add one entry row
                auto addEntryRow = [&](size_t i) {
                    const auto& entry = s_config.removalEntries[i];
                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                    UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowP);
                    if (!rowHBox) return;

                    if (texDanger && setBrushFn)
                    {
                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* dangerImg = UObjectGlobals::StaticConstructObject(imgP);
                        if (dangerImg)
                        {
                            umgSetBrushNoMatch(dangerImg, texDanger, setBrushFn);
                            umgSetBrushSize(dangerImg, 56.0f, 56.0f);
                            UObject* imgSlot = addToHBox(rowHBox, dangerImg);
                            if (imgSlot) umgSetSlotPadding(imgSlot, 4.0f, 8.0f, 8.0f, 8.0f);
                        }
                    }

                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                    if (infoVBox)
                    {
                        UObject* nameTB = makeTB2(entry.friendlyName, 0.3f, 0.85f, 0.3f, 1.0f, 22);
                        if (nameTB) { umgSetBold(nameTB); addToVBox(infoVBox, nameTB); }
                        std::wstring coordText = entry.isTypeRule ? Loc::get("ui.type_rule") : entry.coordsW;
                        UObject* coordsTB = makeTB2(coordText, 0.85f, 0.25f, 0.25f, 1.0f, 16);
                        if (coordsTB) addToVBox(infoVBox, coordsTB);
                        UObject* infoSlot = addToHBox(rowHBox, infoVBox);
                        if (infoSlot) umgSetVAlign(infoSlot, 2);
                    }

                    addToVBox(m_ftRemovalVBox, rowHBox);
                };

                // Type rules first
                if (!typeRuleIndices.empty())
                {
                    UObject* trHeader = makeTB2(L"— Type Rules —", 1.0f, 0.8f, 0.2f, 1.0f, 24);
                    if (trHeader) { umgSetBold(trHeader); UObject* hs = addToVBox(m_ftRemovalVBox, trHeader); if (hs) umgSetSlotPadding(hs, 10.0f, 8.0f, 0.0f, 4.0f); }
                    for (size_t i : typeRuleIndices)
                        addEntryRow(i);
                }

                // Then each bubble group
                for (const auto& bId : bubbleOrder)
                {
                    auto& indices = grouped[bId];
                    // Make display name from bubble ID (replace _ with space)
                    std::wstring displayName;
                    for (char c : bId) displayName += (c == '_') ? L' ' : static_cast<wchar_t>(c);

                    bool isCurrent = (bId == m_currentBubbleId);
                    float hr = isCurrent ? 0.2f : 0.7f;
                    float hg = isCurrent ? 0.9f : 0.7f;
                    float hb = isCurrent ? 1.0f : 0.7f;
                    std::wstring prefix = isCurrent ? L"★ " : L"— ";
                    std::wstring suffix = L" (" + std::to_wstring(indices.size()) + L") —";

                    UObject* bubbleHeader = makeTB2(prefix + displayName + suffix, hr, hg, hb, 1.0f, 24);
                    if (bubbleHeader) { umgSetBold(bubbleHeader); UObject* hs = addToVBox(m_ftRemovalVBox, bubbleHeader); if (hs) umgSetSlotPadding(hs, 10.0f, 12.0f, 0.0f, 4.0f); }

                    for (size_t i : indices)
                        addEntryRow(i);
                }
            }
            setWidgetVisibility(m_ftRemovalVBox, 0); // Visible — children rebuilt
            m_ftLastRemovalCount = count;
        }

