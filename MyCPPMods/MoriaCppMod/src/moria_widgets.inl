// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_widgets.inl — UMG widget creation, config UI, repositioning        ║
// ║  #include inside MoriaCppMod class body                                    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

        // Ã¢â€â‚¬Ã¢â€â‚¬ 6I: UMG Widget System Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        // Runtime UMG widget creation via StaticConstructObject + ProcessEvent:
        //   - Action Bar (12-slot hotbar frame images in UHorizontalBox)
        //   - Advanced Builder Toolbar (lower-right toggle button)
        //   - Target Info Popup (right-side actor details panel)
        //   - Info Box Popup (removal confirmation messages)
        //   - Config Menu (3-tab modal: Optional Mods, Key Mapping, Hide Environment)
        //   - Mod Controller Toolbar (3x3 grid of action buttons)

        // Helper: set a UImage's brush to a texture via ProcessEvent
        void umgSetBrush(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            ensureBrushOffset(img); // resolve Brush property offset on first call
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = true;
            img->ProcessEvent(setBrushFn, bp.data());
        }

        // Helper: set opacity on a UImage
        void umgSetOpacity(UObject* img, float opacity)
        {
            auto* fn = img->GetFunctionByNameInChain(STR("SetOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal()) = opacity;
            img->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetSize(FSlateChildSize) on a slot
        void umgSetSlotSize(UObject* slot, float value, uint8_t sizeRule)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* base = buf.data() + p->GetOffset_Internal();
            *reinterpret_cast<float*>(base + 0) = value;
            *reinterpret_cast<uint8_t*>(base + 4) = sizeRule;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetPadding on a slot (FMargin: Left, Top, Right, Bottom)
        void umgSetSlotPadding(UObject* slot, float left, float top, float right, float bottom)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetPadding"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InPadding"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* m = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            m[0] = left; m[1] = top; m[2] = right; m[3] = bottom;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetHorizontalAlignment on a slot
        void umgSetHAlign(UObject* slot, uint8_t align)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InHorizontalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetVerticalAlignment on a slot
        void umgSetVAlign(UObject* slot, uint8_t align)
        {
            auto* fn = slot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InVerticalAlignment"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            *reinterpret_cast<uint8_t*>(buf.data() + p->GetOffset_Internal()) = align;
            slot->ProcessEvent(fn, buf.data());
        }

        // Helper: call SetRenderScale on a UWidget (FVector2D: ScaleX, ScaleY)
        void umgSetRenderScale(UObject* widget, float sx, float sy)
        {
            auto* fn = widget->GetFunctionByNameInChain(STR("SetRenderScale"));
            if (!fn) return;
            auto* p = findParam(fn, STR("Scale"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = sx; v[1] = sy;
            widget->ProcessEvent(fn, buf.data());
        }

        // Helper: get mouse position in slate units via PlayerController::GetMousePositionScaledByDPI
        // This is the correct UE4 API Ã¢â‚¬â€ returns cursor in DPI-adjusted viewport coordinates
        bool getMousePositionSlate(float& outX, float& outY)
        {
            auto* pc = findPlayerController();
            if (!pc) return false;
            // Try GetMousePositionScaledByDPI first (returns DPI-corrected slate coords)
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
                    // Player param Ã¢â‚¬â€ pass the PC itself as the APlayerController
                    if (pPlayer) *reinterpret_cast<UObject**>(buf.data() + pPlayer->GetOffset_Internal()) = pc;
                    pc->ProcessEvent(fn, buf.data());  // called on WLL CDO or directly?
                    float x = *reinterpret_cast<float*>(buf.data() + pLocX->GetOffset_Internal());
                    float y = *reinterpret_cast<float*>(buf.data() + pLocY->GetOffset_Internal());
                    bool ok = pRV ? *reinterpret_cast<bool*>(buf.data() + pRV->GetOffset_Internal()) : true;
                    if (ok && (x > 0.0f || y > 0.0f)) { outX = x; outY = y; return true; }
                }
            }
            // Fallback: WLL::GetMousePositionOnViewport
            if (m_wllClass)
            {
                auto* cdo = m_wllClass->GetClassDefaultObject();
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
                        cdo->ProcessEvent(fn2, buf.data());
                        auto* rv = reinterpret_cast<float*>(buf.data() + pRV->GetOffset_Internal());
                        if (rv[0] > 0.0f || rv[1] > 0.0f) { outX = rv[0]; outY = rv[1]; return true; }
                    }
                }
            }
            return false;
        }

        // Helper: set position of a UUserWidget via SetPositionInViewport(FVector2D, bRemoveDPIScale)
        // bRemoveDPIScale=false: Position is already in slate units (don't let engine divide by DPI again)
        // bRemoveDPIScale=true:  Position is in raw pixels (engine will divide by DPI scale)
        void setWidgetPosition(UObject* widget, float x, float y, bool bRemoveDPIScale = false)
        {
            if (!widget) return;
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
            widget->ProcessEvent(fn, buf.data());
        }

        // Helper: set color multiplier on a UImage via SetColorAndOpacity(FLinearColor)
        void umgSetImageColor(UObject* img, float r, float g, float b, float a)
        {
            if (!img) return;
            auto* fn = img->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InColorAndOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* c = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            c[0] = r; c[1] = g; c[2] = b; c[3] = a;
            img->ProcessEvent(fn, buf.data());
        }

        // Get the state image UObject for a given toolbar + slot (for hover highlight)
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

        // Hit-test cursor (fractional viewport coords) against all visible toolbars
        // Returns true if a slot was hit, sets outTB (0=BB,1=AB,2=MC) and outSlot
        // Canvas slot sizes (m_toolbarSizeW/H) include umgScale once, but visual content
        // is rendered at an additional 0.81x via SetRenderScale, centered by pivot (0.5, 0.5).
        // Multiply by kRenderScale so hit zone matches the visible icons, not the larger canvas slot.
        bool hitTestToolbarSlot(float curFracX, float curFracY, int& outTB, int& outSlot)
        {
            outTB = -1; outSlot = -1;
            constexpr float kRenderScale = 0.81f;  // matches umgScale/mcScale/abScale
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
                // Visual content is kRenderScale of canvas slot, centered at pivot
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

        // Helper: set text on a UTextBlock via SetText(FText)
        void umgSetText(UObject* textBlock, const std::wstring& text)
        {
            if (!textBlock) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetText"));
            if (!fn) return;
            auto* pInText = findParam(fn, STR("InText"));
            if (!pInText) return;
            FText ftext(text.c_str());
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pInText->GetOffset_Internal(), &ftext, sizeof(FText));
            textBlock->ProcessEvent(fn, buf.data());
        }

        // Helper: set text color on a UTextBlock via SetColorAndOpacity(FSlateColor)
        void umgSetTextColor(UObject* textBlock, float r, float g, float b, float a)
        {
            if (!textBlock) return;
            auto* fn = textBlock->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
            if (!fn) return;
            auto* p = findParam(fn, STR("InColorAndOpacity"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* color = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            color[0] = r; color[1] = g; color[2] = b; color[3] = a;
            // ColorUseRule at offset 0x10 stays 0 (UseColor_Specified) from zero-init
            textBlock->ProcessEvent(fn, buf.data());
        }

        // Helper: set bold typeface on a UTextBlock by patching FSlateFontInfo.TypefaceFontName
        // FSlateFontInfo layout (SlateCore.hpp:309): FontObject@0x00, FontMaterial@0x08,
        //   OutlineSettings@0x10(0x20), TypefaceFontName@0x40(FName), Size@0x48, LetterSpacing@0x4C
        void umgSetBold(UObject* textBlock)
        {
            if (!textBlock) return;
            auto* setFontFn = textBlock->GetFunctionByNameInChain(STR("SetFont"));
            if (!setFontFn) return;
            int fontOff = resolveOffset(textBlock, L"Font", s_off_font);
            if (fontOff < 0) return;
            probeFontStruct(textBlock);
            auto* pFontInfo = findParam(setFontFn, STR("InFontInfo"));
            if (!pFontInfo) return;

            // Read current FSlateFontInfo from the TextBlock
            uint8_t* tbRaw = reinterpret_cast<uint8_t*>(textBlock);
            uint8_t fontBuf[FONT_STRUCT_SIZE];
            std::memcpy(fontBuf, tbRaw + fontOff, FONT_STRUCT_SIZE);

            // Patch TypefaceFontName to "Bold" (use probed offset, fallback to constant)
            RC::Unreal::FName boldName(STR("Bold"), RC::Unreal::FNAME_Add);
            std::memcpy(fontBuf + fontTypefaceName(), &boldName, sizeof(RC::Unreal::FName));

            // Call SetFont with the patched FSlateFontInfo
            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        void umgSetFontSize(UObject* textBlock, int32_t fontSize)
        {
            if (!textBlock) return;
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
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        // Patch font object + size on a TextBlock in one call
        void umgSetFontAndSize(UObject* textBlock, UObject* fontObj, int32_t fontSize)
        {
            if (!textBlock) return;
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
            textBlock->ProcessEvent(setFontFn, buf.data());
        }

        // Create a styled UTextBlock (class-level helper, usable outside toggleFontTestPanel)
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

        // Refresh key labels on all UMG toolbars from current s_bindings
        void refreshKeyLabels()
        {
            for (int i = 0; i < 8; i++)
                if (m_umgKeyLabels[i])
                    umgSetText(m_umgKeyLabels[i], keyName(s_bindings[i].key));
            for (int i = 0; i < MC_SLOTS; i++)
                if (m_mcKeyLabels[i])
                    umgSetText(m_mcKeyLabels[i], keyName(s_bindings[MC_BIND_BASE + i].key));
            // Advanced Builder toolbar key label
            if (m_abKeyLabel)
                umgSetText(m_abKeyLabel, keyName(s_bindings[BIND_AB_OPEN].key));
        }

        // Update MC slot 0 rotation label text from current rotation atomics
        void updateMcRotationLabel()
        {
            if (!m_mcRotationLabel) return;
            int step = s_overlay.rotationStep;
            int total = s_overlay.totalRotation;
            std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
            umgSetText(m_mcRotationLabel, txt);
        }

        // Set a toolbar slot's state icon (Empty/Inactive/Active)
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

        // Helper: set brush WITHOUT matching size (for icon images that need explicit sizing)
        void umgSetBrushNoMatch(UObject* img, UObject* texture, UFunction* setBrushFn)
        {
            ensureBrushOffset(img);
            auto* pTex = findParam(setBrushFn, STR("Texture"));
            auto* pMatch = findParam(setBrushFn, STR("bMatchSize"));
            int sz = setBrushFn->GetParmsSize();
            std::vector<uint8_t> bp(sz, 0);
            if (pTex) *reinterpret_cast<UObject**>(bp.data() + pTex->GetOffset_Internal()) = texture;
            if (pMatch) *reinterpret_cast<bool*>(bp.data() + pMatch->GetOffset_Internal()) = false;
            img->ProcessEvent(setBrushFn, bp.data());
        }

        // Helper: call SetBrushSize on a UImage (FVector2D: Width, Height)
        void umgSetBrushSize(UObject* img, float w, float h)
        {
            auto* fn = img->GetFunctionByNameInChain(STR("SetBrushSize"));
            if (!fn) return;
            auto* p = findParam(fn, STR("DesiredSize"));
            if (!p) return;
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            auto* v = reinterpret_cast<float*>(buf.data() + p->GetOffset_Internal());
            v[0] = w; v[1] = h;
            img->ProcessEvent(fn, buf.data());
        }

        // Set a UMG icon image's recipe texture (or hide if nullptr)
        // Sizes the icon to fit within the state icon bounds, preserving aspect ratio, shrunk 5%
        void setUmgSlotIcon(int slot, UObject* texture)
        {
            if (slot < 0 || slot >= 8 || !m_umgIconImages[slot] || !m_umgSetBrushFn) return;
            m_umgIconTextures[slot] = texture;
            if (texture)
            {
                // Set texture WITH bMatchSize=true so ImageSize gets the native tex dimensions
                umgSetBrush(m_umgIconImages[slot], texture, m_umgSetBrushFn);

                // Read the icon texture's native size from the brush (FSlateBrush.ImageSize at UImage+0x108+0x08)
                uint8_t* iBase = reinterpret_cast<uint8_t*>(m_umgIconImages[slot]);
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeX());
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeY());

                // Get state icon size as the container bounds
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

                // Scale icon to fit within state icon, preserving aspect ratio, then shrink 5%
                float iconW = containerW;
                float iconH = containerH;
                if (texW > 0.0f && texH > 0.0f)
                {
                    float scaleX = containerW / texW;
                    float scaleY = containerH / texH;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY; // fit inside
                    scale *= 0.76f; // shrink to fit (95% * 80% = 76%)
                    iconW = texW * scale;
                    iconH = texH * scale;
                }

                umgSetBrushSize(m_umgIconImages[slot], iconW, iconH);
                umgSetOpacity(m_umgIconImages[slot], 1.0f); // fully visible
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} icon sized: {}x{} (container: {}x{}, tex: {}x{})\n"),
                                                slot, iconW, iconH, containerW, containerH, texW, texH);
            }
            else
            {
                umgSetOpacity(m_umgIconImages[slot], 0.0f); // hidden
            }
        }

        // Find a UTexture2D by name from all loaded textures
        UObject* findTexture2DByName(const std::wstring& name)
        {
            if (name.empty()) return nullptr;
            std::vector<UObject*> textures;
            UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
            for (auto* t : textures)
            {
                if (!t) continue;
                if (std::wstring(t->GetName()) == name) return t;
            }
            return nullptr;
        }

        // Sync quick-build recipe slot states to UMG builders bar
        void updateBuildersBar()
        {
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

                    // Check if texture name changed (new recipe assigned to this slot)
                    bool nameChanged = (m_umgIconNames[i] != m_recipeSlots[i].textureName);
                    if (nameChanged)
                    {
                        // Invalidate cached texture so we re-lookup
                        m_umgIconTextures[i] = nullptr;
                        m_umgIconNames[i] = m_recipeSlots[i].textureName;
                    }

                    // Find and set icon texture if not yet cached
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
            auto* removeFn = m_umgBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_umgBarWidget->ProcessEvent(removeFn, nullptr);
            m_umgBarWidget = nullptr;
            m_umgSetBrushFn = nullptr;
            for (int i = 0; i < 8; i++)
            {
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

            // --- Phase A: Find all 4 textures ---
            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
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

            // --- Phase B: Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"UMG: missing widget classes!");
                return;
            }

            // --- Phase C1: Create UserWidget ---
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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"UMG: CreateWidget null!"); return; }

            // --- Phase C2: Get WidgetTree ---
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Phase C3: Build widget tree ---
            // Two nested Borders: outer = solid white line (2px padding), inner = fully transparent
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"UMG: SetBrushFromTexture missing!"); return; }
            m_umgSetBrushFn = setBrushFn; // cache for runtime state updates

            // Outer border: visible white outline
            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) { showErrorBox(L"UMG: outer border failed!"); return; }

            // Set as WidgetTree root
            if (widgetTree)
                setRootWidget(widgetTree, outerBorder);

            // Outer border: fully transparent (invisible frame)
            auto* setBrushColorFn = outerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // fully transparent
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            // Outer border padding = 0 (no border line)
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
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // Inner border: fully transparent (hides outer's white fill behind content)
            FStaticConstructObjectParameters innerBorderP(borderClass, outer);
            UObject* innerBorder = UObjectGlobals::StaticConstructObject(innerBorderP);
            if (!innerBorder) { showErrorBox(L"UMG: inner border failed!"); return; }

            // Inner border: transparent black
            auto* setBrushColorFn2 = innerBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn2)
            {
                auto* pColor = findParam(setBrushColorFn2, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn2->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // fully transparent
                    innerBorder->ProcessEvent(setBrushColorFn2, cb.data());
                }
            }

            // Set inner border as outer border's child
            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = innerBorder;
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Create HBox inside inner border
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
                innerBorder->ProcessEvent(setContentFn2, sc.data());
            }

            // --- Phase C4: Create 8 columns ---
            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            VLOG(STR("[MoriaCppMod] [UMG] Creating 8 slot columns...\n"));
            for (int i = 0; i < 8; i++)
            {
                // Create VBox column
                FStaticConstructObjectParameters vboxP(vboxClass, outer);
                UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                if (!vbox) continue;

                // Create state image (bottom layer), icon image (top layer), and frame image
                FStaticConstructObjectParameters siP(imageClass, outer);
                UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                if (!stateImg) continue;
                FStaticConstructObjectParameters iiP(imageClass, outer);
                UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                if (!iconImg) continue;
                FStaticConstructObjectParameters fiP(imageClass, outer);
                UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                if (!frameImg) continue;

                // Create UOverlay to stack state + icon images
                FStaticConstructObjectParameters olP(overlayClass, outer);
                UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                if (!overlay) continue;

                // Set textures (bMatchSize=true to preserve aspect ratio)
                umgSetBrush(stateImg, texEmpty, setBrushFn);
                umgSetBrush(frameImg, texFrame, setBrushFn);
                // Icon image starts with no brush (transparent/invisible until recipe assigned)
                umgSetOpacity(iconImg, 0.0f); // hidden until recipe set

                // Read native sizes from first slot (FSlateBrush.ImageSize at UImage+0x108+0x08)
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

                // State icon: fully opaque
                umgSetOpacity(stateImg, 1.0f);
                // Frame icon: 75% transparent
                umgSetOpacity(frameImg, 0.25f);

                // Add state + icon images to Overlay (state first = bottom layer, icon on top)
                auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    auto* pC = findParam(addToOverlayFn, STR("Content"));
                    auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                    // State image (bottom layer of overlay) Ã¢â‚¬â€ centered to preserve aspect ratio
                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                        overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* stateOlSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateOlSlot)
                        {
                            auto* setHAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2; // HAlign_Center
                                stateOlSlot->ProcessEvent(setHAFn, hb.data());
                            }
                            auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2; // VAlign_Center
                                stateOlSlot->ProcessEvent(setVAFn, vb.data());
                            }
                        }
                    }
                    // Icon image (top layer of overlay Ã¢â‚¬â€ transparent PNG on top of state)
                    {
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                        overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* iconSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (iconSlot)
                        {
                            // Center the icon within the overlay
                            auto* setHAFn = iconSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHAFn)
                            {
                                int sz2 = setHAFn->GetParmsSize();
                                std::vector<uint8_t> hb(sz2, 0);
                                auto* pHA = findParam(setHAFn, STR("InHorizontalAlignment"));
                                if (pHA) *reinterpret_cast<uint8_t*>(hb.data() + pHA->GetOffset_Internal()) = 2; // HAlign_Center
                                iconSlot->ProcessEvent(setHAFn, hb.data());
                            }
                            auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVAFn)
                            {
                                int sz2 = setVAFn->GetParmsSize();
                                std::vector<uint8_t> vb(sz2, 0);
                                auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                                if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2; // VAlign_Center
                                iconSlot->ProcessEvent(setVAFn, vb.data());
                            }
                        }
                    }
                }

                // Add to VBox: Overlay (top), Frame image (bottom)
                auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (addToVBoxFn)
                {
                    auto* pC = findParam(addToVBoxFn, STR("Content"));
                    auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));

                    // Overlay (top) Ã¢â‚¬â€ Auto size, centered, contains state + icon stacked
                    {
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                        vbox->ProcessEvent(addToVBoxFn, ap.data());
                        UObject* stateSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (stateSlot)
                        {
                            umgSetSlotSize(stateSlot, 1.0f, 0); // Auto Ã¢â‚¬â€ natural size, no stretch
                            umgSetHAlign(stateSlot, 2);          // HAlign_Center
                        }
                    }

                    // Frame overlay (bottom) Ã¢â‚¬â€ wraps frameImg + keyBgImg + keyLabel
                    {
                        // Create frame overlay to stack frame + keycap bg + key text
                        FStaticConstructObjectParameters foP(overlayClass, outer);
                        UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                        if (frameOverlay)
                        {
                            auto* addToFoFn = frameOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                            if (addToFoFn)
                            {
                                auto* foC = findParam(addToFoFn, STR("Content"));
                                auto* foR = findParam(addToFoFn, STR("ReturnValue"));

                                // Layer 1: frameImg (bottom Ã¢â‚¬â€ fills overlay)
                                {
                                    int sz2 = addToFoFn->GetParmsSize();
                                    std::vector<uint8_t> ap2(sz2, 0);
                                    if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                }

                                // Layer 2: keyBgImg (keycap background, centered)
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
                                        frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                        UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (kbSlot)
                                        {
                                            auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                        m_umgKeyBgImages[i] = keyBgImg;
                                    }
                                }

                                // Layer 3: keyLabel (UTextBlock, centered)
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
                                        frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                        UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                        if (tlSlot)
                                        {
                                            auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                        m_umgKeyLabels[i] = keyLabel;
                                    }
                                }
                            }
                        }

                        // Add frameOverlay (or fall back to frameImg) to VBox
                        UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                        int sz = addToVBoxFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                        vbox->ProcessEvent(addToVBoxFn, ap.data());
                        UObject* frameSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (frameSlot)
                        {
                            umgSetSlotSize(frameSlot, 1.0f, 0); // Auto
                            umgSetHAlign(frameSlot, 2);          // HAlign_Center
                            // Negative top padding pulls frame up into state icon (15% overlap)
                            float overlapPx = stateH * 0.15f;
                            umgSetSlotPadding(frameSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                        }
                    }
                }

                // Add VBox to HBox Ã¢â‚¬â€ Fill for even column distribution
                auto* addToHBoxFn = hbox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                if (addToHBoxFn)
                {
                    auto* pC = findParam(addToHBoxFn, STR("Content"));
                    auto* pR = findParam(addToHBoxFn, STR("ReturnValue"));
                    int sz = addToHBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = vbox;
                    hbox->ProcessEvent(addToHBoxFn, ap.data());
                    UObject* hSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (hSlot)
                    {
                        umgSetSlotSize(hSlot, 1.0f, 0); // Auto Ã¢â‚¬â€ let column take natural icon width
                        umgSetVAlign(hSlot, 2);          // VAlign_Center
                        // No negative padding: icons render at natural size, no horizontal squish
                    }
                }

                m_umgStateImages[i] = stateImg;
                m_umgIconImages[i] = iconImg;
                m_umgSlotStates[i] = UmgSlotState::Empty;
                VLOG(STR("[MoriaCppMod] [UMG] Slot #{} created\n"), i);
            }

            // --- Phase D: Size frame from icon dimensions and center on screen ---
            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;

            // Cache WLL class for mouse queries in drag hit-test
            if (!m_wllClass)
            {
                auto* wllClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                    STR("/Script/UMG.WidgetLayoutLibrary"));
                if (wllClass) m_wllClass = wllClass;
            }
            VLOG(STR("[MoriaCppMod] [UMG] Viewport: {}x{} viewportScale={:.3f} slate={:.0f}x{:.0f} aspect={:.3f}\n"),
                viewW, viewH, m_screen.viewportScale, m_screen.slateW, m_screen.slateH, m_screen.aspectRatio);

            // SetRenderScale: constant uniform scale Ã¢â‚¬â€ engine DPI handles resolution differences
            // Do NOT multiply by uiScale here Ã¢â‚¬â€ the engine already scales slate units to pixels
            constexpr float umgScale = 0.81f;
            umgSetRenderScale(outerBorder, umgScale, umgScale);

            // Use the larger of frame/state width for column width
            float iconW = (frameW > stateW) ? frameW : stateW;
            if (iconW < 1.0f) iconW = 64.0f; // fallback
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float vOverlap = stateH * 0.15f;
            // totalW: 8 columns at natural icon width, no overlap Ã¢â‚¬â€ icons stay square
            float totalW = 8.0f * iconW * umgScale;
            float totalH = (frameH + stateH - vOverlap) * umgScale;
            VLOG(STR("[MoriaCppMod] [UMG] Frame size: {}x{} (iconW={} frameH={} stateH={})\n"),
                                            totalW, totalH, iconW, frameH, stateH);

            // Set explicit pixel size
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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport FIRST (creates the slot for anchor/position/alignment to work on)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Alignment: center-center pivot (widget center placed at position)
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn)
            {
                auto* pAlign = findParam(setAlignFn, STR("Alignment"));
                if (pAlign)
                {
                    int sz = setAlignFn->GetParmsSize();
                    std::vector<uint8_t> al(sz, 0);
                    auto* v = reinterpret_cast<float*>(al.data() + pAlign->GetOffset_Internal());
                    v[0] = 0.5f; v[1] = 0.5f; // center-center pivot
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[0] >= 0) ? m_toolbarPosX[0] : TB_DEF_X[0];
                float fracY = (m_toolbarPosY[0] >= 0) ? m_toolbarPosY[0] : TB_DEF_Y[0];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }

            // Cache size as FRACTION of viewport for resolution-independent hit-testing
            m_toolbarSizeW[0] = m_screen.slateToFracX(totalW);
            m_toolbarSizeH[0] = m_screen.slateToFracY(totalH);
            m_umgBarWidget = userWidget;
            showOnScreen(Loc::get("msg.builders_bar_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [UMG] === Builders bar creation complete ===\n"));

            // Sync quick-build slot states to the builders bar
            updateBuildersBar();
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ Advanced Builder Toolbar (single toggle button, lower-right) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

        void destroyAdvancedBuilderBar()
        {
            if (!m_abBarWidget) return;
            auto* removeFn = m_abBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_abBarWidget->ProcessEvent(removeFn, nullptr);
            m_abBarWidget = nullptr;
            m_abKeyLabel = nullptr;
            m_abStateImage = nullptr;
            VLOG(STR("[MoriaCppMod] [AB] Advanced Builder toolbar removed\n"));
        }

        void createAdvancedBuilderBar()
        {
            if (m_abBarWidget) return; // already exists Ã¢â‚¬â€ persists until world unload

            VLOG(STR("[MoriaCppMod] [AB] === Creating Advanced Builder Toolbar ===\n"));

            // --- Phase A: Find textures ---
            UObject* texFrame = nullptr;     // T_UI_Frame_HUD_AB_Active_BothHands
            UObject* texActive = nullptr;    // T_UI_Btn_HUD_EpicAB_Focused
            UObject* texBlankRect = nullptr; // T_UI_Icon_Input_Blank_Rect
            UObject* texToolsIcon = nullptr; // Tools_Icon
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
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
            // StaticFindObject fallback for Tools_Icon
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

            // --- Phase B: Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"AB: missing widget classes!");
                return;
            }

            // --- Phase C: Create UserWidget ---
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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"AB: CreateWidget null!"); return; }

            // --- Get WidgetTree ---
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"AB: SetBrushFromTexture missing!"); return; }
            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;

            // --- Outer border (transparent) ---
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
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
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
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // --- Build single-slot VBox ---
            FStaticConstructObjectParameters vboxP(vboxClass, outer);
            UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
            if (!vbox) return;

            // Set VBox as border content
            auto* setContentFn = outerBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = vbox;
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Create images: stateImg, iconImg, frameImg
            FStaticConstructObjectParameters siP(imageClass, outer);
            UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
            FStaticConstructObjectParameters iiP(imageClass, outer);
            UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
            FStaticConstructObjectParameters fiP(imageClass, outer);
            UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
            if (!stateImg || !iconImg || !frameImg) return;

            // Cache state image for hover highlight
            m_abStateImage = stateImg;

            // Set textures
            umgSetBrush(stateImg, texActive, setBrushFn);  // active state (always active)
            umgSetBrush(frameImg, texFrame, setBrushFn);
            umgSetOpacity(stateImg, 1.0f);
            umgSetOpacity(frameImg, 0.25f);

            // Set Tools_Icon on iconImg at 75%
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

            // Read native sizes
            uint8_t* fBase = reinterpret_cast<uint8_t*>(frameImg);
            float frameW = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeX());
            float frameH = *reinterpret_cast<float*>(fBase + s_off_brush + brushImageSizeY());
            uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
            float stateW = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeX());
            float stateH = *reinterpret_cast<float*>(sBase + s_off_brush + brushImageSizeY());

            // Create Overlay for state + icon
            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
            if (!overlay) return;

            auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
            if (addToOverlayFn)
            {
                auto* pC = findParam(addToOverlayFn, STR("Content"));
                auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                // State image (bottom layer) Ã¢â‚¬â€ centered
                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = stateImg;
                    overlay->ProcessEvent(addToOverlayFn, ap.data());
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
                            stateOlSlot->ProcessEvent(setHAFn, hb.data());
                        }
                        auto* setVAFn = stateOlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            stateOlSlot->ProcessEvent(setVAFn, vb.data());
                        }
                    }
                }
                // Icon image (top layer) Ã¢â‚¬â€ centered
                {
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = iconImg;
                    overlay->ProcessEvent(addToOverlayFn, ap.data());
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
                            iconSlot->ProcessEvent(setHAFn, hb.data());
                        }
                        auto* setVAFn = iconSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                        if (setVAFn)
                        {
                            int sz2 = setVAFn->GetParmsSize();
                            std::vector<uint8_t> vb(sz2, 0);
                            auto* pVA = findParam(setVAFn, STR("InVerticalAlignment"));
                            if (pVA) *reinterpret_cast<uint8_t*>(vb.data() + pVA->GetOffset_Internal()) = 2;
                            iconSlot->ProcessEvent(setVAFn, vb.data());
                        }
                    }
                }
            }

            // Add Overlay + frame to VBox
            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (addToVBoxFn)
            {
                auto* pC = findParam(addToVBoxFn, STR("Content"));
                auto* pR = findParam(addToVBoxFn, STR("ReturnValue"));

                // Overlay (top)
                {
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = overlay;
                    vbox->ProcessEvent(addToVBoxFn, ap.data());
                    UObject* olSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (olSlot)
                    {
                        umgSetSlotSize(olSlot, 1.0f, 0); // Auto
                        umgSetHAlign(olSlot, 2);          // HAlign_Center
                    }
                }

                // Frame overlay (bottom) Ã¢â‚¬â€ frameImg + keyBgImg + keyLabel
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

                            // Layer 1: frameImg
                            {
                                int sz2 = addToFoFn->GetParmsSize();
                                std::vector<uint8_t> ap2(sz2, 0);
                                if (foC) *reinterpret_cast<UObject**>(ap2.data() + foC->GetOffset_Internal()) = frameImg;
                                frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                            }

                            // Layer 2: keyBgImg (keycap background, centered)
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
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                    UObject* kbSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (kbSlot)
                                    {
                                        auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                }
                            }

                            // Layer 3: keyLabel (UTextBlock, centered)
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
                                    frameOverlay->ProcessEvent(addToFoFn, ap2.data());
                                    UObject* tlSlot = foR ? *reinterpret_cast<UObject**>(ap2.data() + foR->GetOffset_Internal()) : nullptr;
                                    if (tlSlot)
                                    {
                                        auto* setHA = tlSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s3 = setHA->GetParmsSize(); std::vector<uint8_t> h(s3, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = tlSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s3 = setVA->GetParmsSize(); std::vector<uint8_t> v(s3, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; tlSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                    m_abKeyLabel = keyLabel;
                                }
                            }
                        }
                    }

                    // Add frameOverlay to VBox
                    UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                    int sz = addToVBoxFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = frameChild;
                    vbox->ProcessEvent(addToVBoxFn, ap.data());
                    UObject* fSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (fSlot)
                    {
                        umgSetSlotSize(fSlot, 1.0f, 0); // Auto
                        umgSetHAlign(fSlot, 2);
                        float overlapPx = stateH * 0.15f;
                        umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                    }
                }
            }

            // --- Phase D: Size and position (lower-right, 25px from edges) ---
            // Get viewport size for uiScale
            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
            float uiScale = m_screen.uiScale; // minimum scale for readability at sub-1080p

            // Constant render scale Ã¢â‚¬â€ engine DPI system handles resolution differences
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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Alignment: center pivot (0.5, 0.5) Ã¢â‚¬â€ consistent for drag repositioning
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[1] >= 0) ? m_toolbarPosX[1] : TB_DEF_X[1];
                float fracY = (m_toolbarPosY[1] >= 0) ? m_toolbarPosY[1] : TB_DEF_Y[1];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[1] = m_screen.slateToFracX(abTotalW);
            m_toolbarSizeH[1] = m_screen.slateToFracY(abTotalH);
            m_abBarWidget = userWidget;
            showOnScreen(L"Advanced Builder toolbar created!", 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [AB] === Advanced Builder toolbar created ({}x{}) ===\n"),
                                            abTotalW, abTotalH);
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ UMG Target Info Popup Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

        void destroyTargetInfoWidget()
        {
            if (!m_targetInfoWidget) return;
            auto* removeFn = m_targetInfoWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_targetInfoWidget->ProcessEvent(removeFn, nullptr);
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

            // Find UClasses
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

            // Create UserWidget
            int csz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(csz, 0);
            auto* pWC = findParam(createFn, STR("WorldContextObject"));
            auto* pWT = findParam(createFn, STR("WidgetType"));
            auto* pOP = findParam(createFn, STR("OwningPlayer"));
            auto* pRV = findParam(createFn, STR("ReturnValue"));
            if (pWC) *reinterpret_cast<UObject**>(cp.data() + pWC->GetOffset_Internal()) = pc;
            if (pWT) *reinterpret_cast<UObject**>(cp.data() + pWT->GetOffset_Internal()) = userWidgetClass;
            if (pOP) *reinterpret_cast<UObject**>(cp.data() + pOP->GetOffset_Internal()) = pc;
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root SizeBox Ã¢â‚¬â€ enforces fixed width so TextBlocks can wrap text
            UObject* rootSizeBox = nullptr;
            if (sizeBoxClass)
            {
                FStaticConstructObjectParameters sbP(sizeBoxClass, outer);
                rootSizeBox = UObjectGlobals::StaticConstructObject(sbP);
                if (rootSizeBox)
                {
                    if (widgetTree)
                        setRootWidget(widgetTree, rootSizeBox);
                    // SetWidthOverride(550) Ã¢â‚¬â€ hard width constraint for text wrapping
                    auto* setWFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                    if (setWFn) { int sz = setWFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 550.0f; rootSizeBox->ProcessEvent(setWFn, wp.data()); }
                    // Clip overflow to SizeBox bounds
                    auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
                    if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; /* ClipToBounds */ rootSizeBox->ProcessEvent(setClipFn, cp.data()); }
                }
            }

            // Border (dark blue background) Ã¢â‚¬â€ child of SizeBox
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            // If SizeBox exists, add border as its content; otherwise border is root widget
            if (rootSizeBox)
            {
                auto* setContentFn2 = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                if (setContentFn2)
                {
                    auto* pC = findParam(setContentFn2, STR("Content"));
                    int sz = setContentFn2->GetParmsSize();
                    std::vector<uint8_t> sc(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootBorder;
                    rootSizeBox->ProcessEvent(setContentFn2, sc.data());
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
                    c[0] = 0.0f; c[1] = 0.0f; c[2] = 0.0f; c[3] = 0.0f; // transparent (no bg)
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
                }
            }
            // Border padding
            auto* setBorderPadFn = rootBorder->GetFunctionByNameInChain(STR("SetPadding"));
            if (setBorderPadFn)
            {
                auto* pPad = findParam(setBorderPadFn, STR("InPadding"));
                if (pPad)
                {
                    int sz = setBorderPadFn->GetParmsSize();
                    std::vector<uint8_t> pp(sz, 0);
                    auto* m = reinterpret_cast<float*>(pp.data() + pPad->GetOffset_Internal());
                    m[0] = 12.0f; m[1] = 8.0f; m[2] = 12.0f; m[3] = 8.0f; // L, T, R, B
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox as border content
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
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Helper lambda: create TextBlock and add to VBox
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
                // Enable text wrapping at fixed pixel width (doesn't depend on parent layout)
                auto* wrapAtFn = tb->GetFunctionByNameInChain(STR("SetWrapTextAt"));
                if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 1040.0f; tb->ProcessEvent(wrapAtFn, wp.data()); }
                auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
                if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; tb->ProcessEvent(wrapFn, wp.data()); }
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
                return tb;
            };

            // Title
            m_tiTitleLabel = makeTextBlock(Loc::get("ui.target_info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
            // Separator (thin text line)
            makeTextBlock(L"--------------------------------", 0.31f, 0.51f, 0.78f, 0.5f);
            // Data rows
            m_tiClassLabel   = makeTextBlock(Loc::get("ui.label_class"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiNameLabel    = makeTextBlock(Loc::get("ui.label_name"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiDisplayLabel = makeTextBlock(Loc::get("ui.label_display"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiPathLabel    = makeTextBlock(Loc::get("ui.label_path"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiBuildLabel   = makeTextBlock(Loc::get("ui.label_build"), 0.86f, 0.90f, 0.96f, 0.9f);
            m_tiRecipeLabel  = makeTextBlock(Loc::get("ui.label_recipe"), 0.86f, 0.90f, 0.96f, 0.9f);

            // Add to viewport (hidden)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 101;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size for uiScale
            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
            float uiScale = m_screen.uiScale; // minimum scale for readability at sub-1080p

            // Render scale 1.0 -- engine DPI handles resolution scaling via Slate
            if (rootSizeBox) umgSetRenderScale(rootSizeBox, 1.0f, 1.0f);

            // Set desired size in Slate units (engine DPI scales to physical pixels)
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1100.0f; v[1] = 320.0f;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Alignment: center pivot (matches InfoBox)
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_targetInfoWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [TI] Target Info UMG widget created\n"));
        }

        void hideTargetInfo()
        {
            if (!m_targetInfoWidget) return;
            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; m_targetInfoWidget->ProcessEvent(fn, p); }
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

            // Update text labels (wrapText inserts newlines for lines > 70 chars)
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

            // Reposition to current saved position (may have changed via drag)
            {
                m_screen.refresh(findPlayerController());
                int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_targetInfoWidget, m_screen.fracToPixelX(fracX),
                                                      m_screen.fracToPixelY(fracY), true);
            }

            // Show widget
            auto* fn = m_targetInfoWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_targetInfoWidget->ProcessEvent(fn, p); }

            // Auto-copy to clipboard
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
                    memcpy(GlobalLock(hMem), copyText.c_str(), sz);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
                VLOG(STR("[MoriaCppMod] Target info copied to clipboard\n"));
            }

            m_tiShowTick = GetTickCount64();
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ UMG Info Box Popup (removal messages) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

        /* REMOVED: Old InfoBox popup system replaced by Error Box below
        void destroyInfoBox()
        {
            if (!m_infoBoxWidget) return;
            auto* removeFn = m_infoBoxWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_infoBoxWidget->ProcessEvent(removeFn, nullptr);
            m_infoBoxWidget = nullptr;
            m_ibTitleLabel = nullptr;
            m_ibMessageLabel = nullptr;
            m_ibShowTick = 0;
        }

        void createInfoBox()
        {
            if (m_infoBoxWidget) return;
            VLOG(STR("[MoriaCppMod] [IB] === Creating Info Box UMG widget ===\n"));

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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root border (dark blue bg Ã¢â‚¬â€ same as Target Info)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootBorder);

            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.098f; c[1] = 0.118f; c[2] = 0.176f; c[3] = 0.86f;
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
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
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox
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
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));

            auto makeTextBlock = [&](const std::wstring& text, float r, float g, float b, float a) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                int sz = addToVBoxFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
                vbox->ProcessEvent(addToVBoxFn, ap.data());
                return tb;
            };

            m_ibTitleLabel   = makeTextBlock(Loc::get("ui.info_title"), 0.78f, 0.86f, 1.0f, 1.0f);
            m_ibMessageLabel = makeTextBlock(L"", 0.86f, 0.90f, 0.96f, 0.9f);

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 102;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size for uiScale
            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
            float uiScale = m_screen.uiScale; // minimum scale for readability at sub-1080p

            // Render scale 1.0 Ã¢â‚¬â€ engine DPI handles resolution scaling
            if (vbox) umgSetRenderScale(vbox, 1.0f, 1.0f);

            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 400.0f * uiScale; v[1] = 80.0f * uiScale;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[3] = m_screen.slateToFracX(400.0f * m_screen.uiScale);
            m_toolbarSizeH[3] = m_screen.slateToFracY(80.0f * m_screen.uiScale);

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_infoBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [IB] Info Box UMG widget created\n"));
        }

        void hideInfoBox()
        {
            if (!m_infoBoxWidget) return;
            auto* fn = m_infoBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; m_infoBoxWidget->ProcessEvent(fn, p); }
            m_ibShowTick = 0;
        }

        void showInfoBox(const std::wstring& title, const std::wstring& message,
                         float r = 0.0f, float g = 1.0f, float b = 0.5f)
        {
            if (!m_infoBoxWidget) createInfoBox();
            if (!m_infoBoxWidget) return;

            umgSetText(m_ibTitleLabel, title);
            umgSetTextColor(m_ibTitleLabel, r, g, b, 1.0f);
            umgSetText(m_ibMessageLabel, message);

            // Reposition to current saved position (may have changed via drag)
            {
                m_screen.refresh(findPlayerController());
                int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_infoBoxWidget, m_screen.fracToPixelX(fracX),
                                                   m_screen.fracToPixelY(fracY), true);
            }

            auto* fn = m_infoBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_infoBoxWidget->ProcessEvent(fn, p); }

            m_ibShowTick = GetTickCount64();
        }
        END REMOVED OLD INFOBOX */

        // ── Error Box ── UMG popup for error/status messages (auto-fades after 5s) ──

        void destroyErrorBox()
        {
            if (!m_errorBoxWidget) return;
            auto* removeFn = m_errorBoxWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_errorBoxWidget->ProcessEvent(removeFn, nullptr);
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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) return;

            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Root border (dark red-tinted background)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootBorder);

            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.22f; c[1] = 0.06f; c[2] = 0.06f; c[3] = 0.88f; // dark red bg
                    rootBorder->ProcessEvent(setBrushColorFn, cb.data());
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
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // VBox
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
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            auto* addToVBoxFn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
            if (!addToVBoxFn) return;
            auto* vbC = findParam(addToVBoxFn, STR("Content"));

            // Message text (warm amber on dark red)
            FStaticConstructObjectParameters tbP(textBlockClass, outer);
            UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
            if (!tb) return;
            umgSetText(tb, L"");
            umgSetTextColor(tb, 1.0f, 0.85f, 0.6f, 1.0f);
            auto* wrapFn = tb->GetFunctionByNameInChain(STR("SetAutoWrapText"));
            if (wrapFn) { int ws = wrapFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapFn, STR("InAutoWrapText")); if (pw) *reinterpret_cast<bool*>(wp.data() + pw->GetOffset_Internal()) = true; tb->ProcessEvent(wrapFn, wp.data()); }
            auto* wrapAtFn = tb->GetFunctionByNameInChain(STR("SetWrapTextAt"));
            if (wrapAtFn) { int ws = wrapAtFn->GetParmsSize(); std::vector<uint8_t> wp(ws, 0); auto* pw = findParam(wrapAtFn, STR("InWrapTextAt")); if (pw) *reinterpret_cast<float*>(wp.data() + pw->GetOffset_Internal()) = 380.0f; tb->ProcessEvent(wrapAtFn, wp.data()); }
            int sz = addToVBoxFn->GetParmsSize();
            std::vector<uint8_t> ap(sz, 0);
            if (vbC) *reinterpret_cast<UObject**>(ap.data() + vbC->GetOffset_Internal()) = tb;
            vbox->ProcessEvent(addToVBoxFn, ap.data());
            m_ebMessageLabel = tb;

            // Add to viewport (high Z-order)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int vsz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(vsz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 110;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: same location as Target Info (slot 3)
            {
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(userWidget, m_screen.fracToPixelX(fracX),
                                              m_screen.fracToPixelY(fracY), true);
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_errorBoxWidget = userWidget;
            VLOG(STR("[MoriaCppMod] [EB] Error Box UMG widget created\n"));
        }

        void hideErrorBox()
        {
            if (!m_errorBoxWidget) return;
            auto* fn = m_errorBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 1; m_errorBoxWidget->ProcessEvent(fn, p); }
            m_ebShowTick = 0;
        }

        void showErrorBox(const std::wstring& message)
        {
            if (!s_verbose) return;
            if (!m_errorBoxWidget) createErrorBox();
            if (!m_errorBoxWidget) { showOnScreen(message, 5.0f, 1.0f, 0.3f, 0.3f); return; }

            umgSetText(m_ebMessageLabel, message);

            // Reposition
            {
                m_screen.refresh(findPlayerController());
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_errorBoxWidget, m_screen.fracToPixelX(fracX),
                                                    m_screen.fracToPixelY(fracY), true);
            }

            auto* fn = m_errorBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_errorBoxWidget->ProcessEvent(fn, p); }

            m_ebShowTick = GetTickCount64();
            VLOG(STR("[MoriaCppMod] [EB] showErrorBox: '{}'\n"), message);
        }


        // Ã¢â€â‚¬Ã¢â€â‚¬ Mod Controller Toolbar (4x3, lower-right) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

        void destroyModControllerBar()
        {
            if (!m_mcBarWidget) return;
            auto* removeFn = m_mcBarWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn)
                m_mcBarWidget->ProcessEvent(removeFn, nullptr);
            m_mcBarWidget = nullptr;
            for (int i = 0; i < MC_SLOTS; i++)
            {
                m_mcStateImages[i] = nullptr;
                m_mcIconImages[i] = nullptr;
                m_mcSlotStates[i] = UmgSlotState::Empty;
                m_mcKeyLabels[i] = nullptr;
                m_mcKeyBgImages[i] = nullptr;
            }
            m_mcRotationLabel = nullptr;
            m_mcSlot0Overlay = nullptr;
            m_mcSlot8Overlay = nullptr;
            m_mcSlot10Overlay = nullptr;
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

            VLOG(STR("[MoriaCppMod] [MC] === Creating 4x3 Mod Controller toolbar ===\n"));

            // --- Find textures (reuse same state/frame textures + MC slot icons) ---
            UObject* texFrame = nullptr;
            UObject* texEmpty = nullptr;
            UObject* texInactive = nullptr;
            UObject* texActive = nullptr;
            UObject* texBlankRect = nullptr;
            UObject* texRotation = nullptr;     // T_UI_Refresh Ã¢â‚¬â€ MC slot 0 (Rotation)
            UObject* texTarget = nullptr;       // T_UI_Search Ã¢â‚¬â€ MC slot 1 (Target)
            UObject* texRemoveTarget = nullptr; // T_UI_Icon_GoodPlace2 Ã¢â‚¬â€ MC slot 8 (Remove Target)
            UObject* texUndoLast = nullptr;     // T_UI_Alert_BakedIcon Ã¢â‚¬â€ MC slot 9 (Undo Last)
            UObject* texRemoveAll = nullptr;    // T_UI_Icon_Filled_GoodPlace2 Ã¢â‚¬â€ MC slot 10 (Remove All)
            UObject* texSettings = nullptr;     // T_UI_Icon_Settings Ã¢â‚¬â€ MC slot 11 (Configuration)
            UObject* texSnapToggle = nullptr;   // T_UI_Icon_Build — MC slot 5 (Snap Toggle)
            UObject* texStability = nullptr;     // T_UI_Icon_Craft — MC slot 2 (Stability Check)
            UObject* texHideChar = nullptr;     // T_UI_Eye_Open Ã¢â‚¬â€ MC slot 3 (Hide Character)
            {
                std::vector<UObject*> textures;
                UObjectGlobals::FindAllOf(STR("Texture2D"), textures);
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
            if (!m_umgTexBlankRect && texBlankRect) m_umgTexBlankRect = texBlankRect; // cache if not yet cached
            if (!m_umgTexInactive && texInactive) m_umgTexInactive = texInactive;
            if (!m_umgTexActive && texActive) m_umgTexActive = texActive;

            // Fallback: try StaticFindObject for textures not found in FindAllOf scan
            // (some textures may not be loaded yet; StaticFindObject with full path can locate them)
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

            // --- Find UClasses ---
            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            if (!userWidgetClass || !imageClass || !hboxClass || !vboxClass || !borderClass || !overlayClass)
            {
                showErrorBox(L"MC: missing widget classes!");
                return;
            }

            // --- Create UserWidget ---
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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"MC: CreateWidget null!"); return; }

            // --- Get WidgetTree ---
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"MC: SetBrushFromTexture missing!"); return; }
            // Reuse m_umgSetBrushFn if not already set
            if (!m_umgSetBrushFn) m_umgSetBrushFn = setBrushFn;

            // --- Outer border (transparent, invisible frame) ---
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
                    outerBorder->ProcessEvent(setBrushColorFn, cb.data());
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
                    outerBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }

            // --- Root VBox (3 rows) inside border ---
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
                outerBorder->ProcessEvent(setContentFn, sc.data());
            }

            // --- Create 3 rows x 4 columns = 12 slots ---
            float frameW = 0, frameH = 0, stateW = 0, stateH = 0;
            int slotIdx = 0;
            for (int row = 0; row < 3; row++)
            {
                // Create HBox for this row
                FStaticConstructObjectParameters hboxP(hboxClass, outer);
                UObject* hbox = UObjectGlobals::StaticConstructObject(hboxP);
                if (!hbox) continue;

                // Add HBox to root VBox
                UObject* rowSlot = addToVBox(rootVBox, hbox);
                if (rowSlot)
                {
                    umgSetSlotSize(rowSlot, 1.0f, 0); // Auto
                }

                for (int col = 0; col < 4; col++)
                {
                    int i = slotIdx++;

                    // Create VBox column
                    FStaticConstructObjectParameters vboxP(vboxClass, outer);
                    UObject* vbox = UObjectGlobals::StaticConstructObject(vboxP);
                    if (!vbox) continue;

                    // Create images
                    FStaticConstructObjectParameters siP(imageClass, outer);
                    UObject* stateImg = UObjectGlobals::StaticConstructObject(siP);
                    if (!stateImg) continue;
                    FStaticConstructObjectParameters iiP(imageClass, outer);
                    UObject* iconImg = UObjectGlobals::StaticConstructObject(iiP);
                    if (!iconImg) continue;
                    FStaticConstructObjectParameters fiP(imageClass, outer);
                    UObject* frameImg = UObjectGlobals::StaticConstructObject(fiP);
                    if (!frameImg) continue;

                    // Create UOverlay
                    FStaticConstructObjectParameters olP(overlayClass, outer);
                    UObject* overlay = UObjectGlobals::StaticConstructObject(olP);
                    if (!overlay) continue;

                    // Set textures
                    umgSetBrush(stateImg, texEmpty, setBrushFn);
                    umgSetBrush(frameImg, texFrame, setBrushFn);
                    umgSetOpacity(iconImg, 0.0f);

                    // Read native sizes from first slot; save overlay ref for slot 0
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
                        m_mcSlot0Overlay = overlay; // save for rotation label (added after loop)
                    }
                    if (i == 8) m_mcSlot8Overlay = overlay; // save for "Single" label
                    if (i == 10) m_mcSlot10Overlay = overlay; // save for "All" label

                    umgSetOpacity(stateImg, 1.0f);
                    umgSetOpacity(frameImg, 0.25f);

                    // State image (bottom layer) -- centered to preserve aspect
                    UObject* stateOlSlot = addToOverlay(overlay, stateImg);
                    if (stateOlSlot)
                    {
                        umgSetHAlign(stateOlSlot, 2); // HAlign_Center
                        umgSetVAlign(stateOlSlot, 2); // VAlign_Center
                    }
                    // Icon image (top layer) -- centered
                    UObject* iconSlot = addToOverlay(overlay, iconImg);
                    if (iconSlot)
                    {
                        umgSetHAlign(iconSlot, 2); // HAlign_Center
                        umgSetVAlign(iconSlot, 2); // VAlign_Center
                    }

                    // Overlay (top)
                    UObject* olSlot = addToVBox(vbox, overlay);
                    if (olSlot)
                    {
                        umgSetSlotSize(olSlot, 1.0f, 0); // Auto
                        umgSetHAlign(olSlot, 2);          // HAlign_Center
                    }
                    // Frame overlay (bottom) -- wraps frameImg + keyBgImg + keyLabel
                    {
                        FStaticConstructObjectParameters foP(overlayClass, outer);
                        UObject* frameOverlay = UObjectGlobals::StaticConstructObject(foP);

                        if (frameOverlay)
                        {
                            // Layer 1: frameImg (bottom -- fills overlay)
                            addToOverlay(frameOverlay, frameImg);

                            // Layer 2: keyBgImg (keycap background, centered)
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
                                        umgSetHAlign(kbSlot, 2); // HAlign_Center
                                        umgSetVAlign(kbSlot, 2); // VAlign_Center
                                    }
                                    m_mcKeyBgImages[i] = keyBgImg;
                                }
                            }

                            // Layer 3: keyLabel (UTextBlock, centered)
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
                                        umgSetHAlign(tlSlot, 2); // HAlign_Center
                                        umgSetVAlign(tlSlot, 2); // VAlign_Center
                                    }
                                    m_mcKeyLabels[i] = keyLabel;
                                }
                            }
                        }

                        // Add frameOverlay (or fall back to frameImg) to VBox
                        UObject* frameChild = frameOverlay ? frameOverlay : frameImg;
                        UObject* fSlot = addToVBox(vbox, frameChild);
                        if (fSlot)
                        {
                            umgSetSlotSize(fSlot, 1.0f, 0); // Auto
                            umgSetHAlign(fSlot, 2);
                            float overlapPx = stateH * 0.25f; // 25% vertical overlap (reduced 5%)
                            umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                        }
                    }

                    // Add VBox to HBox
                    UObject* hSlot = addToHBox(hbox, vbox);
                    if (hSlot)
                    {
                        umgSetSlotSize(hSlot, 1.0f, 1); // Fill
                        umgSetVAlign(hSlot, 0);          // VAlign_Fill
                        float colW2 = (frameW > stateW) ? frameW : stateW;
                        float hOverlap = colW2 * 0.10f; // 10% each side = 20% overlap (reduced from 40% for more spacing)
                        umgSetSlotPadding(hSlot, -hOverlap, 0.0f, -hOverlap, 0.0f);
                    }

                    m_mcStateImages[i] = stateImg;
                    m_mcIconImages[i] = iconImg;
                    m_mcSlotStates[i] = UmgSlotState::Empty;
                }
            }
            VLOG(STR("[MoriaCppMod] [MC] All 12 slots created (4x3)\n"));

            // --- Set custom icons for all 12 MC slots ---
            {
                UObject* mcSlotTextures[MC_SLOTS] = {
                    texRotation, texTarget, texStability, texHideChar,         // row 0: Rotation, Target, StabilityCheck, SuperDwarf
                    nullptr, texSnapToggle, nullptr, nullptr,               // row 1: Empty, SnapToggle, Empty6, Empty7
                    texRemoveTarget, texUndoLast, texRemoveAll, texSettings  // row 2: RemoveTarget, UndoLast, RemoveAll, Config
                };
                const wchar_t* mcSlotNames[MC_SLOTS] = {
                    L"T_UI_Refresh", L"T_UI_Search", L"T_UI_Icon_Craft", L"T_UI_Eye_Open",
                    L"Empty4", L"T_UI_Icon_Build", L"Empty6", L"Empty7",
                    L"T_UI_Icon_GoodPlace2", L"T_UI_Alert_BakedIcon", L"T_UI_Icon_Filled_GoodPlace2", L"T_UI_Icon_Settings"
                };
                for (int i = 0; i < MC_SLOTS; i++)
                {
                    if (!mcSlotTextures[i]) continue; // skip slots with no icon
                    if (m_mcIconImages[i] && setBrushFn)
                    {
                        umgSetBrush(m_mcIconImages[i], mcSlotTextures[i], setBrushFn);
                        umgSetOpacity(m_mcIconImages[i], 1.0f);
                        // Switch state image from empty to active frame for slots with icons
                        if (m_mcStateImages[i] && texActive)
                            umgSetBrush(m_mcStateImages[i], texActive, setBrushFn);
                        // Scale icon to 70% with aspect ratio preservation
                        uint8_t* iBase = reinterpret_cast<uint8_t*>(m_mcIconImages[i]);
                        float texW = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeX());
                        float texH = *reinterpret_cast<float*>(iBase + s_off_brush + brushImageSizeY());
                        if (texW > 0.0f && texH > 0.0f)
                        {
                            // Get state icon size as container bounds
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

            // --- Add rotation text overlay on MC slot 0 ---
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
                    umgSetTextColor(rotLabel, 0.4f, 0.6f, 1.0f, 1.0f); // medium blue

                    UObject* rotSlot = addToOverlay(m_mcSlot0Overlay, rotLabel);
                    if (rotSlot)
                    {
                        umgSetHAlign(rotSlot, 2); // HAlign_Center
                        umgSetVAlign(rotSlot, 2); // VAlign_Center
                    }
                    m_mcRotationLabel = rotLabel;
                    VLOG(STR("[MoriaCppMod] [MC] Rotation label created on slot 0\n"));
                }
            }

            // --- Add "Single" text overlay on MC slot 8 (Remove Target) ---
            if (m_mcSlot8Overlay && textBlockClass)
            {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* singleLabel = UObjectGlobals::StaticConstructObject(tbP);
                if (singleLabel)
                {
                    umgSetText(singleLabel, L"Single");
                    umgSetTextColor(singleLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                    umgSetFontSize(singleLabel, 31); // 10% larger

                    UObject* labelSlot = addToOverlay(m_mcSlot8Overlay, singleLabel);
                    if (labelSlot)
                    {
                        umgSetHAlign(labelSlot, 2); // HAlign_Center
                        umgSetVAlign(labelSlot, 2); // VAlign_Center
                    }
                    VLOG(STR("[MoriaCppMod] [MC] 'Single' label created on slot 8\n"));
                }
            }

            // --- Add "All" text overlay on MC slot 10 (Remove All) ---
            if (m_mcSlot10Overlay && textBlockClass)
            {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* allLabel = UObjectGlobals::StaticConstructObject(tbP);
                if (allLabel)
                {
                    umgSetText(allLabel, L"All");
                    umgSetTextColor(allLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                    umgSetFontSize(allLabel, 31); // 10% larger

                    UObject* labelSlot = addToOverlay(m_mcSlot10Overlay, allLabel);
                    if (labelSlot)
                    {
                        umgSetHAlign(labelSlot, 2); // HAlign_Center
                        umgSetVAlign(labelSlot, 2); // VAlign_Center
                    }
                    VLOG(STR("[MoriaCppMod] [MC] 'All' label created on slot 10\n"));
                }
            }

            // --- Size and position: lower-right of screen ---
            // Get viewport size for uiScale
            m_screen.refresh(findPlayerController());
            int32_t viewW = m_screen.viewW, viewH = m_screen.viewH;
            float uiScale = m_screen.uiScale; // minimum scale for readability at sub-1080p

            // SetRenderScale 0.81 * uiScale
            // Constant render scale Ã¢â‚¬â€ engine DPI system handles resolution differences
            float mcScale = 0.81f;
            umgSetRenderScale(outerBorder, mcScale, mcScale);

            float mcIconW = (frameW > stateW) ? frameW : stateW;
            if (mcIconW < 1.0f) mcIconW = 64.0f;
            if (frameH < 1.0f) frameH = 64.0f;
            if (stateH < 1.0f) stateH = 64.0f;

            float mcVOverlap = stateH * 0.25f;                     // 25% vertical overlap (matches slot padding)
            float mcHOverlapPerSlot = mcIconW * 0.20f;             // 20% horizontal overlap (10% each side, reduced from 40%)
            float mcTotalW = (4.0f * mcIconW - 3.0f * mcHOverlapPerSlot) * mcScale * 1.2f;  // 4 cols, 3 gaps, +20% wider for spacing
            float mcSlotH = (frameH + stateH - mcVOverlap);
            float mcTotalH = (3.0f * mcSlotH) * mcScale;           // 3 rows (4x3 grid)

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
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 100;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Alignment: center pivot (0.5, 0.5) Ã¢â‚¬â€ consistent for drag repositioning
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: fraction-based (user-customizable, resolution-independent)
            {
                float fracX = (m_toolbarPosX[2] >= 0) ? m_toolbarPosX[2] : TB_DEF_X[2];
                float fracY = (m_toolbarPosY[2] >= 0) ? m_toolbarPosY[2] : TB_DEF_Y[2];
                float setPosX = m_screen.fracToPixelX(fracX);
                float setPosY = m_screen.fracToPixelY(fracY);
                setWidgetPosition(userWidget, setPosX, setPosY, true);
                // DEBUG: read back actual position to verify coordinate space
                auto* getPosF = userWidget->GetFunctionByNameInChain(STR("GetPositionInViewport"));
                if (getPosF)
                {
                    auto* pRV = findParam(getPosF, STR("ReturnValue"));
                    if (pRV)
                    {
                        int sz = getPosF->GetParmsSize();
                        std::vector<uint8_t> buf(sz, 0);
                        userWidget->ProcessEvent(getPosF, buf.data());
                        auto* rv = reinterpret_cast<float*>(buf.data() + pRV->GetOffset_Internal());
                        VLOG(STR("[MoriaCppMod] [MC] SetPos=({:.1f},{:.1f}) GetPos=({:.1f},{:.1f}) rawVP={}x{} frac=({:.4f},{:.4f})\n"),
                            setPosX, setPosY, rv[0], rv[1], viewW, viewH, fracX, fracY);
                        showOnScreen(std::format(L"MC set=({:.0f},{:.0f}) got=({:.0f},{:.0f}) vp={}x{}",
                            setPosX, setPosY, rv[0], rv[1], viewW, viewH).c_str(), 10.0f, 1.0f, 1.0f, 0.0f);
                    }
                }
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[2] = m_screen.slateToFracX(mcTotalW);
            m_toolbarSizeH[2] = m_screen.slateToFracY(mcTotalH);
            m_mcBarWidget = userWidget;

            // Snap Toggle (slot 5) starts Disabled — no ghost piece at creation time
            setMcSlotState(5, UmgSlotState::Disabled);

            showOnScreen(Loc::get("msg.mod_controller_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [MC] === Mod Controller bar creation complete ({}x{}) ===\n"),
                                            mcTotalW, mcTotalH);
        }

        // ── Settings Panel (F12) ──────────────────────────────────────────────
        // Modal settings panel: tabbed left column (Key Bindings, Game Options,
        // Environment), scrollable keybinding rows on right with checkbox + label
        // + key capture button.  Enters UI input mode (mouse cursor).

        void toggleFontTestPanel()
        {
            // Toggle off: remove existing panel and restore game input
            if (m_ftVisible && m_fontTestWidget && isWidgetAlive(m_fontTestWidget))
            {
                auto* fn = m_fontTestWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (fn) safeProcessEvent(m_fontTestWidget, fn, nullptr);
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
                m_ftFreeBuildCheckImg = nullptr;
                m_ftFreeBuildLabel = nullptr;
                m_ftFreeBuildKeyLabel = nullptr;
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
                showOnScreen(L"Settings panel closed", 2.0f, 0.8f, 0.8f, 0.8f);
                return;
            }

            VLOG(STR("[MoriaCppMod] [Settings] Creating settings panel...\n"));

            // --- Phase A: Find textures ---
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

            // --- Phase B: Find UClasses ---
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

            // --- Phase C: Create UserWidget ---
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
            wblCDO->ProcessEvent(createFn, cp.data());
            UObject* userWidget = pRV ? *reinterpret_cast<UObject**>(cp.data() + pRV->GetOffset_Internal()) : nullptr;
            if (!userWidget) { showErrorBox(L"FontTest: CreateWidget failed!"); return; }

            // WidgetTree + outer
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = wtSlot ? *wtSlot : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // SetBrushFromTexture function
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showErrorBox(L"FontTest: SetBrushFromTexture missing!"); return; }

            // --- Phase D: Build widget tree ---
            // Root: Overlay (layers BG image behind content)
            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOverlay = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOverlay) { showErrorBox(L"FontTest: overlay failed!"); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOverlay);

            // Clip overflow to panel bounds
            {
                auto* setClipFn = rootOverlay->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; rootOverlay->ProcessEvent(setClipFn, cp.data()); }
            }

            // Layer 0: Blue border frame (0.5px dark blue around the panel)
            {
                FStaticConstructObjectParameters borderFrameP(imageClass, outer);
                UObject* borderFrame = UObjectGlobals::StaticConstructObject(borderFrameP);
                if (borderFrame)
                {
                    umgSetBrushSize(borderFrame, 1440.0f, 880.0f);
                    umgSetImageColor(borderFrame, 0.08f, 0.14f, 0.32f, 0.55f); // darker blue, semi-transparent
                    addToOverlay(rootOverlay, borderFrame);
                }
            }

            // Layer 1: Background image (inset 0.5px for border, 50% opacity)
            {
                FStaticConstructObjectParameters imgP(imageClass, outer);
                UObject* bgImg = UObjectGlobals::StaticConstructObject(imgP);
                if (bgImg)
                {
                    umgSetBrush(bgImg, texBG, setBrushFn);
                    umgSetBrushSize(bgImg, 1439.0f, 879.0f); // 1440-1, 880-1 (0.5px border each side)
                    umgSetOpacity(bgImg, 0.5f);
                    UObject* bgSlot = addToOverlay(rootOverlay, bgImg);
                    if (bgSlot) { umgSetHAlign(bgSlot, 2); umgSetVAlign(bgSlot, 2); } // center inside border
                }
            }

            // Layer 2: Content HBox (tabs left, font list right)
            FStaticConstructObjectParameters hbP(hboxClass, outer);
            UObject* contentHBox = UObjectGlobals::StaticConstructObject(hbP);
            if (!contentHBox) { showErrorBox(L"FontTest: hbox failed!"); return; }
            {
                UObject* slot = addToOverlay(rootOverlay, contentHBox);
                if (slot)
                {
                    umgSetSlotPadding(slot, 30.0f, 30.0f, 30.0f, 30.0f);
                    umgSetHAlign(slot, 0); // HAlign_Fill
                    umgSetVAlign(slot, 0); // VAlign_Fill — constrain height to overlay
                }
            }

            // Find DefaultRegularFont for all text
            UObject* defaultFont = nullptr;
            {
                std::vector<UObject*> fonts;
                UObjectGlobals::FindAllOf(STR("Font"), fonts);
                for (auto* f : fonts)
                {
                    if (f && std::wstring(f->GetName()) == L"DefaultRegularFont")
                    { defaultFont = f; break; }
                }
            }

            // Helper: create styled TextBlock
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

            // Left column: VBox with CONFIG_TAB_COUNT tabs (512x128 each)
            {
                FStaticConstructObjectParameters vbP(vboxClass, outer);
                UObject* tabVBox = UObjectGlobals::StaticConstructObject(vbP);
                if (tabVBox)
                {
                    UObject* slot = addToHBox(contentHBox, tabVBox);
                    if (slot) umgSetSlotPadding(slot, 2.5f, 10.0f, 2.5f, 0.0f);

                    const wchar_t* tabNames[] = { L"Key Bindings", L"Game Options", L"Environment", L"Game Mods" };
                    for (int i = 0; i < CONFIG_TAB_COUNT; i++)
                    {
                        FStaticConstructObjectParameters olTabP(overlayClass, outer);
                        UObject* tabOl = UObjectGlobals::StaticConstructObject(olTabP);
                        if (!tabOl) continue;

                        // Tab background image (512x128, tab 0 = focused)
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

                        // Centered text label
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
                                umgSetHAlign(txtSlot, 2); // HAlign_Center
                                umgSetVAlign(txtSlot, 2); // VAlign_Center
                            }
                        }

                        UObject* tabSlot = addToVBox(tabVBox, tabOl);
                        if (tabSlot) umgSetSlotPadding(tabSlot, 0.0f, 2.0f, 0.0f, 2.0f);
                    }
                }
            }

            // Separator line between tabs and right pane
            {
                // Try known separator texture names from _Shared folder
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
                        umgSetRenderScale(sepImg, 1.0f, -1.0f); // mirror vertically
                    }
                    else
                    {
                        // Fallback: thin line matching border color
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
                    UObjectGlobals::FindAllOf(STR("Texture2D"), allTex);
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

            // Right column: Overlay[Border(bg) + SizeBox→ScrollBox] — SizeBox directly constrains scroll height
            {
                // Overlay to layer border behind scrollbox
                FStaticConstructObjectParameters rightOlP(overlayClass, outer);
                UObject* rightOverlay = UObjectGlobals::StaticConstructObject(rightOlP);
                if (rightOverlay)
                {
                    // Layer 0: Border (visual background only)
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
                                frameBorder->ProcessEvent(setBrushColorFn, cb.data());
                            }
                        }
                        addToOverlay(rightOverlay, frameBorder);
                    }

                    // Layer 1: SizeBox → ScrollBox (SizeBox directly constrains scroll height)
                    FStaticConstructObjectParameters sbxP(sizeBoxClass, outer);
                    UObject* rightSizeBox = UObjectGlobals::StaticConstructObject(sbxP);
                    FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                    UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                    if (rightSizeBox && scrollBox)
                    {
                        m_ftScrollBox = scrollBox;
                        // Height = panel 880 - 30 top pad - 30 bottom pad - 5 top nudge = 815
                        auto* setHOvFn = rightSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
                        if (setHOvFn) { int sz = setHOvFn->GetParmsSize(); std::vector<uint8_t> hp(sz, 0); auto* p = findParam(setHOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 815.0f; rightSizeBox->ProcessEvent(setHOvFn, hp.data()); }
                        // Put ScrollBox directly inside SizeBox
                        auto* setChildFn = rightSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                        if (!setChildFn) setChildFn = rightSizeBox->GetFunctionByNameInChain(STR("AddChild"));
                        if (setChildFn)
                        {
                            auto* pChild = findParam(setChildFn, STR("Content"));
                            if (!pChild) pChild = findParam(setChildFn, STR("InContent"));
                            if (pChild) { int sz = setChildFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); *reinterpret_cast<UObject**>(cp.data() + pChild->GetOffset_Internal()) = scrollBox; rightSizeBox->ProcessEvent(setChildFn, cp.data()); }
                        }
                        UObject* sbSlot = addToOverlay(rightOverlay, rightSizeBox);
                        if (sbSlot)
                        {
                            umgSetHAlign(sbSlot, 0); // HAlign_Fill — scrollbar reaches right edge
                            umgSetVAlign(sbSlot, 0); // VAlign_Fill
                            umgSetSlotPadding(sbSlot, 10.0f, 15.0f, 10.0f, 10.0f); // 15 top = 10 pad + 5 nudge down
                        }

                        auto* alwaysShowFn = scrollBox->GetFunctionByNameInChain(STR("SetAlwaysShowScrollbar"));
                        if (alwaysShowFn)
                        {
                            auto* p = findParam(alwaysShowFn, STR("NewAlwaysShowScrollbar"));
                            int sz = alwaysShowFn->GetParmsSize();
                            std::vector<uint8_t> buf(sz, 0);
                            if (p) *reinterpret_cast<bool*>(buf.data() + p->GetOffset_Internal()) = true;
                            scrollBox->ProcessEvent(alwaysShowFn, buf.data());
                        }
                    }
                    UObject* borderSlot = addToHBox(contentHBox, rightOverlay);
                    if (borderSlot)
                    {
                        umgSetSlotSize(borderSlot, 1.0f, 1); // Fill remaining width
                        umgSetVAlign(borderSlot, 0); // VAlign_Fill — fill parent height
                    }

                    // Populate scroll content (guarded by scrollBox creation)
                    if (scrollBox)
                    {
                        // Create CONFIG_TAB_COUNT tab content VBoxes (one per tab, only tab 0 added to ScrollBox initially)
                        for (int t = 0; t < CONFIG_TAB_COUNT; t++)
                        {
                            FStaticConstructObjectParameters tcP(vboxClass, outer);
                            UObject* tcVBox = UObjectGlobals::StaticConstructObject(tcP);
                            if (tcVBox)
                            {
                                m_ftTabContent[t] = tcVBox;
                                if (t == 0) addChildToPanel(scrollBox, STR("AddChild"), tcVBox);
                            }
                        }

                        // ── Tab 1: Game Options ──────────────────────────
                        if (m_ftTabContent[1])
                        {
                            UObject* t1 = m_ftTabContent[1];

                            // Section header: "Cheat Toggles"
                            if (texSectionBg)
                            {
                                FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                if (secOl)
                                {
                                    FStaticConstructObjectParameters secImgP(imageClass, outer);
                                    UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                    if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                    UObject* secLabel = makeTB(Loc::get("ui.cheat_toggles"), 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                    if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                    addToVBox(t1, secOl);
                                }
                            }

                            // Free Build row: HBox { Checkbox | Label (fill) | greyed-out KeyBox }
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* fbRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (fbRow)
                                {
                                    // Checkbox
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
                                                    m_ftFreeBuildCheckImg = chkImg;
                                                    // Start hidden — updateFtFreeBuild will sync
                                                    auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = 1; chkImg->ProcessEvent(visFn, vp); }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(fbRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }
                                    // Label
                                    UObject* fbLabel = makeTB(Loc::get("ui.free_build"), 0.55f, 0.55f, 0.55f, 1.0f, 24);
                                    m_ftFreeBuildLabel = fbLabel;
                                    if (fbLabel)
                                    {
                                        UObject* ls = addToHBox(fbRow, fbLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }
                                    // Key box showing OFF/ON state
                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(L"OFF", 0.7f, 0.3f, 0.3f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); m_ftFreeBuildKeyLabel = kbLabel; UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(fbRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, fbRow);
                                }
                            }

                            // No Collision (Flying) row: HBox { Checkbox | Label (fill) | greyed-out KeyBox }
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* ncRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (ncRow)
                                {
                                    // Checkbox
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
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = 1; chkImg->ProcessEvent(visFn, vp); }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(ncRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }
                                    // Label
                                    UObject* ncLabel = makeTB(Loc::get("ui.no_collision"), 0.55f, 0.55f, 0.55f, 1.0f, 24);
                                    m_ftNoCollisionLabel = ncLabel;
                                    if (ncLabel)
                                    {
                                        UObject* ls = addToHBox(ncRow, ncLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }
                                    // Key box showing OFF/ON state
                                    if (texKeyBox)
                                    {
                                        FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                                        UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                                        if (kbOl)
                                        {
                                            FStaticConstructObjectParameters kbImgP(imageClass, outer);
                                            UObject* kbImg = UObjectGlobals::StaticConstructObject(kbImgP);
                                            if (kbImg) { umgSetBrushNoMatch(kbImg, texKeyBox, setBrushFn); umgSetBrushSize(kbImg, 400.0f, 128.0f); addToOverlay(kbOl, kbImg); }
                                            UObject* kbLabel = makeTB(L"OFF", 0.7f, 0.3f, 0.3f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); m_ftNoCollisionKeyLabel = kbLabel; UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(ncRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, ncRow);
                                }
                            }

                            // Unlock All Recipes row: HBox { no checkbox (92px pad) | Label (fill) | UNLOCK button }
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* ulRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (ulRow)
                                {
                                    UObject* ulLabel = makeTB(Loc::get("ui.unlock_all_recipes"), 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                    if (ulLabel)
                                    {
                                        UObject* ls = addToHBox(ulRow, ulLabel);
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
                                            UObject* kbLabel = makeTB(L"UNLOCK", 0.9f, 0.75f, 0.2f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(ulRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, ulRow);
                                }
                            }

                            // Rename Character row: HBox { no checkbox (92px pad) | Label (fill) | RENAME button }
                            {
                                FStaticConstructObjectParameters rowP(hboxClass, outer);
                                UObject* rcRow = UObjectGlobals::StaticConstructObject(rowP);
                                if (rcRow)
                                {
                                    UObject* rcLabel = makeTB(L"Rename Character", 0.86f, 0.90f, 0.96f, 0.85f, 24);
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
                                            UObject* kbLabel = makeTB(L"RENAME", 0.9f, 0.75f, 0.2f, 1.0f, 24);
                                            if (kbLabel) { umgSetBold(kbLabel); UObject* ks = addToOverlay(kbOl, kbLabel); if (ks) { umgSetHAlign(ks, 2); umgSetVAlign(ks, 2); } }
                                            UObject* kbSlot = addToHBox(rcRow, kbOl);
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, rcRow);
                                }
                            }

                            // Checkbox + keybind rows: Trash Item, Replenish Item, Remove Attributes
                            {
                                struct GameOptBind { int bindIdx; UObject** checkImgPtr; bool* enabledPtr; };
                                GameOptBind gameOptBinds[] = {
                                    { BIND_TRASH_ITEM,     &m_ftTrashCheckImg,      &m_trashItemEnabled },
                                    { BIND_REPLENISH_ITEM, &m_ftReplenishCheckImg,  &m_replenishItemEnabled },
                                    { BIND_REMOVE_ATTRS,   &m_ftRemoveAttrsCheckImg,&m_removeAttrsEnabled },
                                };
                                for (auto& gob : gameOptBinds)
                                {
                                    int bi = gob.bindIdx;
                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* goRow = UObjectGlobals::StaticConstructObject(rowP);
                                    if (!goRow) continue;
                                    // Checkbox
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
                                                    // Start hidden — update function will sync
                                                    auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                    if (visFn) { uint8_t vp[8]{}; vp[0] = 1; chkImg->ProcessEvent(visFn, vp); }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(goRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }
                                    // Label
                                    UObject* goLabel = makeTB(s_bindings[bi].label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                    if (goLabel)
                                    {
                                        UObject* ls = addToHBox(goRow, goLabel);
                                        if (ls) { umgSetSlotSize(ls, 1.0f, 1); umgSetSlotPadding(ls, 0.0f, 24.0f, 0.0f, 24.0f); umgSetVAlign(ls, 2); }
                                    }
                                    // Rebindable key box (same as Tab 0 pattern)
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
                                            if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                        }
                                    }
                                    addToVBox(t1, goRow);
                                }
                            }
                        }

                        // ── Tab 2: Environment ──────────────────────────
                        if (m_ftTabContent[2])
                        {
                            UObject* t2 = m_ftTabContent[2];

                            // Header: "Saved Removals: N"
                            int remCount = s_config.removalCount.load();
                            UObject* hdr = makeTB(Loc::get("ui.saved_removals_prefix") + std::to_wstring(remCount) + Loc::get("ui.saved_removals_suffix"),
                                                  0.78f, 0.86f, 1.0f, 1.0f, 24);
                            if (hdr) { umgSetBold(hdr); addToVBox(t2, hdr); }
                            m_ftRemovalHeader = hdr;

                            // Removal entries VBox (rebuilt dynamically)
                            FStaticConstructObjectParameters rvP(vboxClass, outer);
                            UObject* remVBox = UObjectGlobals::StaticConstructObject(rvP);
                            if (remVBox) { m_ftRemovalVBox = remVBox; addToVBox(t2, remVBox); }

                            // Populate removal entries
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

                                    // Danger icon (clickable)
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

                                    // Info VBox: name (bold) + coords (smaller)
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

                        // ── Tab 3: Game Mods ──────────────────────────
                        if (m_ftTabContent[3])
                        {
                            UObject* t3 = m_ftTabContent[3];

                            // Discover available game mods
                            m_ftGameModEntries = discoverGameMods();

                            if (m_ftGameModEntries.empty())
                            {
                                UObject* noMods = makeTB(L"No definition packs found in Mods/MoriaCppMod/definitions/",
                                    0.7f, 0.7f, 0.7f, 1.0f, 22);
                                if (noMods)
                                {
                                    UObject* s = addToVBox(t3, noMods);
                                    if (s) umgSetSlotPadding(s, 10.0f, 20.0f, 10.0f, 10.0f);
                                }
                            }
                            else
                            {
                                // Section header
                                if (texSectionBg)
                                {
                                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                                    if (secOl)
                                    {
                                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, 900.0f, 80.0f); addToOverlay(secOl, secImg); }
                                        UObject* secLabel = makeTB(L"Definition Packs", 0.78f, 0.86f, 1.0f, 1.0f, 28);
                                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                                        addToVBox(t3, secOl);
                                    }
                                }

                                // One row per game mod: Checkbox | Title + Description
                                for (size_t gi = 0; gi < m_ftGameModEntries.size() && gi < MAX_GAME_MODS; gi++)
                                {
                                    auto& gm = m_ftGameModEntries[gi];

                                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                                    UObject* gmRow = UObjectGlobals::StaticConstructObject(rowP);
                                    if (!gmRow) continue;

                                    // Checkbox
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
                                                    // Hide check if not enabled
                                                    if (!gm.enabled)
                                                    {
                                                        auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                        if (visFn) { uint8_t vp[8]{}; vp[0] = 2; chkImg->ProcessEvent(visFn, vp); }
                                                    }
                                                }
                                            }
                                            UObject* cbSlot = addToHBox(gmRow, cbOl);
                                            if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                        }
                                    }

                                    // Info VBox: Title (bold) + Description (smaller, grey)
                                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                                    if (infoVBox)
                                    {
                                        std::wstring wTitle(gm.title.begin(), gm.title.end());
                                        UObject* titleTB = makeTB(wTitle, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                                        if (titleTB) { umgSetBold(titleTB); addToVBox(infoVBox, titleTB); }

                                        // Show description (strip HTML tags for display)
                                        if (!gm.description.empty())
                                        {
                                            // Simple HTML strip — remove tags
                                            std::string plain;
                                            bool inTag = false;
                                            for (char c : gm.description)
                                            {
                                                if (c == '<') { inTag = true; continue; }
                                                if (c == '>') { inTag = false; plain += ' '; continue; }
                                                if (!inTag) plain += c;
                                            }
                                            // Collapse whitespace
                                            std::string desc;
                                            bool lastSpace = true;
                                            for (char c : plain)
                                            {
                                                if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                                                { if (!lastSpace) { desc += ' '; lastSpace = true; } }
                                                else { desc += c; lastSpace = false; }
                                            }
                                            // Trim
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
                                            umgSetSlotSize(infoSlot, 1.0f, 1); // Fill
                                            umgSetVAlign(infoSlot, 2); // VAlign_Center
                                        }
                                    }

                                    UObject* rowSlot = addToVBox(t3, gmRow);
                                    if (rowSlot) umgSetSlotPadding(rowSlot, 0.0f, 2.0f, 0.0f, 2.0f);
                                }

                                // Footer note
                                UObject* footerTB = makeTB(L"Changes take effect on next game launch",
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

                        // Populate keybinding rows into tab 0 content
                        UObject* tab0Content = m_ftTabContent[0];
                        const wchar_t* lastSection = nullptr;
                        for (int b = 0; b < BIND_COUNT; b++)
                        {
                            if (wcscmp(s_bindings[b].label, L"Reserved") == 0) continue;
                            // Skip binds that belong on the Game Options tab
                            if (wcscmp(s_bindings[b].section, L"Game Options") == 0) continue;

                            // Section header when section changes
                            if (!lastSection || wcscmp(lastSection, s_bindings[b].section) != 0)
                            {
                                lastSection = s_bindings[b].section;
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

                            // Keybinding row: HBox { Checkbox | Label (fill) | KeyBox (right) }
                            FStaticConstructObjectParameters rowHbP(hboxClass, outer);
                            UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowHbP);
                            if (!rowHBox) continue;

                            // Checkbox: Overlay with DiamondBG(80x80) + Check
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
                                            // Hide check if binding is disabled
                                            if (!s_bindings[b].enabled)
                                            {
                                                auto* visFn = chkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                                if (visFn) { uint8_t vp[8]{}; vp[0] = 2; chkImg->ProcessEvent(visFn, vp); }
                                            }
                                        }
                                    }
                                    UObject* cbSlot = addToHBox(rowHBox, cbOl);
                                    if (cbSlot) umgSetSlotPadding(cbSlot, 4.0f, 24.0f, 8.0f, 24.0f);
                                }
                            }

                            // Binding label (fills middle)
                            UObject* bindLabel = makeTB(s_bindings[b].label, 0.86f, 0.90f, 0.96f, 0.85f, 24);
                            if (bindLabel)
                            {
                                UObject* lblSlot = addToHBox(rowHBox, bindLabel);
                                if (lblSlot)
                                {
                                    umgSetSlotSize(lblSlot, 1.0f, 1); // Fill
                                    umgSetSlotPadding(lblSlot, 0.0f, 24.0f, 0.0f, 24.0f);
                                    umgSetVAlign(lblSlot, 2); // VAlign_Center
                                }
                            }

                            // Key box: Overlay(400x128) with image + centered key name
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
                                    if (kbSlot) umgSetSlotPadding(kbSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                }
                            }

                            addChildToPanel(tab0Content ? tab0Content : scrollBox, STR("AddChild"), rowHBox);
                        }

                        // Modifier key row at the end
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
                                        if (mkSlot) umgSetSlotPadding(mkSlot, 0.0f, 0.0f, 4.0f, 0.0f);
                                    }
                                }
                                addChildToPanel(tab0Content ? tab0Content : scrollBox, STR("AddChild"), modRow);
                            }
                        }

                        VLOG(STR("[MoriaCppMod] [Settings] Keybinding rows populated\n"));
                    }
                }
            }

            // --- Phase E: Add to viewport, center on screen ---
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 200;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Set desired size in Slate units (1440x880 panel)
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize)
                {
                    int sz = setDesiredSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb(sz, 0);
                    auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal());
                    v[0] = 1440.0f; v[1] = 880.0f;
                    userWidget->ProcessEvent(setDesiredSizeFn, sb.data());
                }
            }

            // Center alignment
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
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Center position (50% of viewport)
            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f),
                                          m_screen.fracToPixelY(0.5f), true);

            m_fontTestWidget = userWidget;
            m_ftSelectedTab = 0;
            m_ftVisible = true;
            setInputModeUI(userWidget);
            // Sync Game Options checkboxes to current state
            updateFtFreeBuild();
            updateFtNoCollision();
            updateFtGameOptCheckboxes();
            showOnScreen(L"Settings panel opened (ALT+INS to close)", 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [Settings] Panel created and displayed\n"));
        }

        // Switch settings panel tab (highlight + content visibility)
        void selectFontTestTab(int tab)
        {
            if (tab < 0 || tab >= CONFIG_TAB_COUNT) return;
            if (tab == m_ftSelectedTab) return;
            m_ftSelectedTab = tab;

            auto* sBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));

            for (int i = 0; i < CONFIG_TAB_COUNT; i++)
            {
                // Swap tab background texture
                if (m_ftTabImages[i] && sBrushFn)
                {
                    UObject* tex = (i == tab) ? m_ftTabActiveTexture : m_ftTabInactiveTexture;
                    if (tex) umgSetBrushNoMatch(m_ftTabImages[i], tex, sBrushFn);
                }
                // Swap tab label color
                if (m_ftTabLabels[i])
                {
                    if (i == tab)
                        umgSetTextColor(m_ftTabLabels[i], 0.9f, 0.88f, 0.78f, 1.0f);
                    else
                        umgSetTextColor(m_ftTabLabels[i], 0.55f, 0.55f, 0.65f, 0.7f);
                }
            }
            // Swap ScrollBox child: remove old tab VBox, add new one
            if (m_ftScrollBox)
            {
                auto* clearFn = m_ftScrollBox->GetFunctionByNameInChain(STR("ClearChildren"));
                if (clearFn) m_ftScrollBox->ProcessEvent(clearFn, nullptr);
                if (m_ftTabContent[tab])
                    addChildToPanel(m_ftScrollBox, STR("AddChild"), m_ftTabContent[tab]);
                // Reset scroll to top
                auto* setScrollFn = m_ftScrollBox->GetFunctionByNameInChain(STR("SetScrollOffset"));
                if (setScrollFn)
                {
                    auto* pOff = findParam(setScrollFn, STR("NewScrollOffset"));
                    if (pOff) { int sz = setScrollFn->GetParmsSize(); std::vector<uint8_t> sp(sz, 0); *reinterpret_cast<float*>(sp.data() + pOff->GetOffset_Internal()) = 0.0f; m_ftScrollBox->ProcessEvent(setScrollFn, sp.data()); }
                }
            }
            // Cancel key capture when switching tabs
            if (s_capturingBind >= 0)
            {
                s_capturingBind = -1;
                updateFontTestKeyLabels();
            }
        }

        // Update key box labels on the settings panel
        void updateFontTestKeyLabels()
        {
            int capturing = s_capturingBind.load();
            for (int i = 0; i < BIND_COUNT; i++)
            {
                if (!m_ftKeyBoxLabels[i]) continue;
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

        // Update Game Options tab checkboxes to match current state
        void updateFtFreeBuild()
        {
            bool on = s_config.freeBuild;
            if (m_ftFreeBuildCheckImg)
            {
                auto* visFn = m_ftFreeBuildCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; m_ftFreeBuildCheckImg->ProcessEvent(visFn, p); }
            }
            if (m_ftFreeBuildKeyLabel)
            {
                umgSetText(m_ftFreeBuildKeyLabel, on ? L"ON" : L"OFF");
                umgSetTextColor(m_ftFreeBuildKeyLabel, on ? 0.31f : 0.7f, on ? 0.86f : 0.3f, on ? 0.47f : 0.3f, 1.0f);
            }
        }

        void updateFtNoCollision()
        {
            bool on = m_noCollisionWhileFlying;
            if (m_ftNoCollisionCheckImg)
            {
                auto* visFn = m_ftNoCollisionCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; m_ftNoCollisionCheckImg->ProcessEvent(visFn, p); }
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
            };
            for (auto& item : items)
            {
                if (!item.img) continue;
                auto* visFn = item.img->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = item.on ? 0 : 1; item.img->ProcessEvent(visFn, p); }
            }
        }

        // ── Rename Character Dialog ──────────────────────────

        void showRenameDialog()
        {
            if (m_ftRenameVisible) { VLOG(STR("[MoriaCppMod] [Rename] BLOCKED: already visible\n")); return; }
            if (!m_characterLoaded) { showErrorBox(L"Character not loaded"); return; }
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

            int sz = createFn->GetParmsSize();
            std::vector<uint8_t> cp(sz, 0);
            auto* pOwner = findParam(createFn, STR("WorldContextObject"));
            auto* pClass = findParam(createFn, STR("WidgetType"));
            auto* pRet   = findParam(createFn, STR("ReturnValue"));
            if (!pOwner || !pClass || !pRet) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: pOwner={} pClass={} pRet={}\n"), (void*)pOwner, (void*)pClass, (void*)pRet); return; }
            *reinterpret_cast<UObject**>(cp.data() + pOwner->GetOffset_Internal()) = pc;
            *reinterpret_cast<UObject**>(cp.data() + pClass->GetOffset_Internal()) = userWidgetClass;
            wblClass->ProcessEvent(createFn, cp.data());
            UObject* userWidget = *reinterpret_cast<UObject**>(cp.data() + pRet->GetOffset_Internal());
            if (!userWidget) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: userWidget is null\n")); return; }
            VLOG(STR("[MoriaCppMod] [Rename] CP1: userWidget created\n"));

            UObject* outer = userWidget;
            auto* wtSlot = userWidget->GetValuePtrByPropertyNameInChain<UObject*>(STR("WidgetTree"));
            UObject* widgetTree = (wtSlot && *wtSlot) ? *wtSlot : nullptr;

            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));

            // Find textures
            UObject* texBG = findTexture2DByName(L"T_UI_Pnl_Background_Base");
            UObject* texSectionBg = findTexture2DByName(L"T_UI_Pnl_TabSelected");

            VLOG(STR("[MoriaCppMod] [Rename] CP2: textures done\n"));
            // Root overlay
            FStaticConstructObjectParameters olP(overlayClass, outer);
            UObject* rootOl = UObjectGlobals::StaticConstructObject(olP);
            if (!rootOl) { VLOG(STR("[MoriaCppMod] [Rename] FAIL: rootOl is null\n")); return; }
            if (widgetTree) setRootWidget(widgetTree, rootOl);

            // Clip overflow
            {
                auto* setClipFn = rootOl->GetFunctionByNameInChain(STR("SetClipping"));
                if (setClipFn) { int sz2 = setClipFn->GetParmsSize(); std::vector<uint8_t> cp2(sz2, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp2.data() + p->GetOffset_Internal()) = 1; rootOl->ProcessEvent(setClipFn, cp2.data()); }
            }

            VLOG(STR("[MoriaCppMod] [Rename] CP3: rootOl + clipping done\n"));
            const float dlgW = 700.0f, dlgH = 220.0f;

            // Layer 0: Border frame (dark blue)
            {
                FStaticConstructObjectParameters bfP(imageClass, outer);
                UObject* bf = UObjectGlobals::StaticConstructObject(bfP);
                if (bf) { umgSetBrushSize(bf, dlgW, dlgH); umgSetImageColor(bf, 0.08f, 0.14f, 0.32f, 1.0f); addToOverlay(rootOl, bf); }
            }

            // Layer 1: BG image inset 1px (fully opaque)
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

            // Layer 2: Content VBox
            FStaticConstructObjectParameters cvP(vboxClass, outer);
            UObject* contentVBox = UObjectGlobals::StaticConstructObject(cvP);
            if (contentVBox)
            {
                UObject* cvSlot = addToOverlay(rootOl, contentVBox);
                if (cvSlot) umgSetSlotPadding(cvSlot, 20.0f, 10.0f, 20.0f, 10.0f);

                // Section header: "Rename Character"
                if (texSectionBg && setBrushFn)
                {
                    FStaticConstructObjectParameters secOlP(overlayClass, outer);
                    UObject* secOl = UObjectGlobals::StaticConstructObject(secOlP);
                    if (secOl)
                    {
                        FStaticConstructObjectParameters secImgP(imageClass, outer);
                        UObject* secImg = UObjectGlobals::StaticConstructObject(secImgP);
                        if (secImg) { umgSetBrushNoMatch(secImg, texSectionBg, setBrushFn); umgSetBrushSize(secImg, dlgW - 40.0f, 50.0f); addToOverlay(secOl, secImg); }
                        UObject* secLabel = createTextBlock(L"Rename Character", 0.78f, 0.86f, 1.0f, 1.0f, 24);
                        if (secLabel) { umgSetBold(secLabel); UObject* ts = addToOverlay(secOl, secLabel); if (ts) { umgSetHAlign(ts, 2); umgSetVAlign(ts, 2); } }
                        addToVBox(contentVBox, secOl);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP4: header section done\n"));
                // Current name label
                {
                    std::wstring currentName = L"(unknown)";
                    // Try to get current character name
                    std::vector<UObject*> mgrs;
                    UObjectGlobals::FindAllOf(STR("CustomizationManager"), mgrs);
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
                            // GetCharacterName returns FString
                            int gsz = getFn->GetParmsSize();
                            std::vector<uint8_t> gbuf(gsz, 0);
                            target->ProcessEvent(getFn, gbuf.data());
                            auto* retProp = findParam(getFn, STR("ReturnValue"));
                            if (retProp)
                            {
                                // FString: {wchar_t* Data, int32 Num, int32 Max}
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
                        UObject* curLbl = createTextBlock(L"Current name:  ", 0.55f, 0.55f, 0.6f, 0.8f, 22);
                        if (curLbl) { UObject* ls = addToHBox(nameRow, curLbl); if (ls) { umgSetSlotPadding(ls, 0.0f, 10.0f, 0.0f, 5.0f); umgSetVAlign(ls, 2); } }
                        UObject* nameLbl = createTextBlock(currentName, 0.9f, 0.75f, 0.2f, 1.0f, 22);
                        if (nameLbl) { umgSetBold(nameLbl); UObject* ls = addToHBox(nameRow, nameLbl); if (ls) { umgSetSlotPadding(ls, 0.0f, 10.0f, 0.0f, 5.0f); umgSetVAlign(ls, 2); } }
                        addToVBox(contentVBox, nameRow);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP5: current name label done\n"));
                // EditableTextBox in a SizeBox (wide enough for 22+ chars)
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
                        // Set font on EditableTextBox: write to both WidgetStyle.Font and top-level Font
                        {
                            UObject* defaultFont = nullptr;
                            std::vector<UObject*> fonts;
                            UObjectGlobals::FindAllOf(STR("Font"), fonts);
                            for (auto* f : fonts) { if (f && std::wstring(f->GetName()) == L"DefaultRegularFont") { defaultFont = f; break; } }
                            if (defaultFont)
                            {
                                uint8_t* raw = reinterpret_cast<uint8_t*>(editBox);
                                int32_t fontSize = 24;
                                // WidgetStyle.Font: WidgetStyle@0x0130 + Font@0x0238 = 0x0368 (Slate rendering font)
                                constexpr int STYLE_FONT_OFF = 0x0368;
                                *reinterpret_cast<UObject**>(raw + STYLE_FONT_OFF) = defaultFont;
                                std::memcpy(raw + STYLE_FONT_OFF + fontSizeOff(), &fontSize, sizeof(int32_t));
                                // Top-level Font property at 0x0958
                                constexpr int EDITBOX_FONT_OFF = 0x0958;
                                *reinterpret_cast<UObject**>(raw + EDITBOX_FONT_OFF) = defaultFont;
                                std::memcpy(raw + EDITBOX_FONT_OFF + fontSizeOff(), &fontSize, sizeof(int32_t));
                            }
                        }
                        // Set width override so text box is wide
                        auto* setWOvFn = editSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
                        if (setWOvFn) { int wsz = setWOvFn->GetParmsSize(); std::vector<uint8_t> wp(wsz, 0); auto* p = findParam(setWOvFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = dlgW - 40.0f; editSizeBox->ProcessEvent(setWOvFn, wp.data()); }
                        auto* setHOvFn = editSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
                        if (setHOvFn) { int hsz = setHOvFn->GetParmsSize(); std::vector<uint8_t> hp(hsz, 0); auto* p = findParam(setHOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 50.0f; editSizeBox->ProcessEvent(setHOvFn, hp.data()); }
                        // Put editBox inside SizeBox
                        auto* setChildFn = editSizeBox->GetFunctionByNameInChain(STR("SetContent"));
                        if (!setChildFn) setChildFn = editSizeBox->GetFunctionByNameInChain(STR("AddChild"));
                        if (setChildFn) { auto* pChild = findParam(setChildFn, STR("Content")); if (!pChild) pChild = findParam(setChildFn, STR("InContent")); if (pChild) { int csz = setChildFn->GetParmsSize(); std::vector<uint8_t> cbuf(csz, 0); *reinterpret_cast<UObject**>(cbuf.data() + pChild->GetOffset_Internal()) = editBox; editSizeBox->ProcessEvent(setChildFn, cbuf.data()); } }
                        VLOG(STR("[MoriaCppMod] [Rename] CP5c: SizeBox configured, about to SetHintText...\n"));
                        // Set hint text using proper FText constructor (same pattern as setTextBlockText)
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
                                editBox->ProcessEvent(setHintFn, hbuf.data());
                            }
                        }
                        UObject* editSlot = addToVBox(contentVBox, editSizeBox);
                        if (editSlot) umgSetSlotPadding(editSlot, 0.0f, 8.0f, 0.0f, 10.0f);
                    }
                }

                VLOG(STR("[MoriaCppMod] [Rename] CP6: EditableTextBox section done\n"));
                // Button row: HBox { CANCEL (red) | CONFIRM (green) }
                {
                    FStaticConstructObjectParameters btnHbP(hboxClass, outer);
                    UObject* btnRow = UObjectGlobals::StaticConstructObject(btnHbP);
                    if (btnRow)
                    {
                        // CANCEL button — solid red bg, white text
                        {
                            FStaticConstructObjectParameters cOlP(overlayClass, outer);
                            UObject* cancelOl = UObjectGlobals::StaticConstructObject(cOlP);
                            if (cancelOl)
                            {
                                FStaticConstructObjectParameters cImgP(imageClass, outer);
                                UObject* cImg = UObjectGlobals::StaticConstructObject(cImgP);
                                if (cImg) { umgSetBrushSize(cImg, 250.0f, 55.0f); umgSetImageColor(cImg, 0.6f, 0.12f, 0.12f, 1.0f); addToOverlay(cancelOl, cImg); }
                                UObject* cLbl = createTextBlock(L"CANCEL", 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cLbl) { umgSetBold(cLbl); UObject* cs = addToOverlay(cancelOl, cLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cSlot = addToHBox(btnRow, cancelOl);
                                if (cSlot) umgSetSlotPadding(cSlot, 20.0f, 0.0f, 30.0f, 0.0f);
                            }
                        }
                        // CONFIRM button — solid green bg, white text
                        {
                            FStaticConstructObjectParameters cfOlP(overlayClass, outer);
                            UObject* confirmOl = UObjectGlobals::StaticConstructObject(cfOlP);
                            if (confirmOl)
                            {
                                FStaticConstructObjectParameters cfImgP(imageClass, outer);
                                UObject* cfImg = UObjectGlobals::StaticConstructObject(cfImgP);
                                if (cfImg) { umgSetBrushSize(cfImg, 250.0f, 55.0f); umgSetImageColor(cfImg, 0.12f, 0.5f, 0.15f, 1.0f); addToOverlay(confirmOl, cfImg); }
                                UObject* cfLbl = createTextBlock(L"CONFIRM", 1.0f, 1.0f, 1.0f, 1.0f, 22);
                                if (cfLbl) { umgSetBold(cfLbl); m_ftRenameConfirmLabel = cfLbl; UObject* cs = addToOverlay(confirmOl, cfLbl); if (cs) { umgSetHAlign(cs, 2); umgSetVAlign(cs, 2); } }
                                UObject* cfSlot = addToHBox(btnRow, confirmOl);
                                if (cfSlot) umgSetSlotPadding(cfSlot, 0.0f, 0.0f, 20.0f, 0.0f);
                            }
                        }
                        UObject* btnSlot = addToVBox(contentVBox, btnRow);
                        if (btnSlot) { umgSetHAlign(btnSlot, 2); } // center the button row
                    }
                }
            }

            VLOG(STR("[MoriaCppMod] [Rename] CP7: all widgets built, about to AddToViewport...\n"));
            // Add to viewport at ZOrder 300 (above settings panel)
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int avSz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(avSz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 300;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Set size
            auto* setDesiredSizeFn = userWidget->GetFunctionByNameInChain(STR("SetDesiredSizeInViewport"));
            if (setDesiredSizeFn)
            {
                auto* pSize = findParam(setDesiredSizeFn, STR("Size"));
                if (pSize) { int ssz = setDesiredSizeFn->GetParmsSize(); std::vector<uint8_t> sb(ssz, 0); auto* v = reinterpret_cast<float*>(sb.data() + pSize->GetOffset_Internal()); v[0] = dlgW; v[1] = dlgH; userWidget->ProcessEvent(setDesiredSizeFn, sb.data()); }
            }

            // Center alignment
            auto* setAlignFn = userWidget->GetFunctionByNameInChain(STR("SetAlignmentInViewport"));
            if (setAlignFn) { auto* pAlign = findParam(setAlignFn, STR("Alignment")); if (pAlign) { int asz = setAlignFn->GetParmsSize(); std::vector<uint8_t> ab(asz, 0); auto* a = reinterpret_cast<float*>(ab.data() + pAlign->GetOffset_Internal()); a[0] = 0.5f; a[1] = 0.5f; userWidget->ProcessEvent(setAlignFn, ab.data()); } }

            // Center position
            setWidgetPosition(userWidget, m_screen.fracToPixelX(0.5f), m_screen.fracToPixelY(0.5f), true);

            VLOG(STR("[MoriaCppMod] [Rename] CP8: AddToViewport + sizing done\n"));
            m_ftRenameWidget = userWidget;
            m_ftRenameVisible = true;
            setInputModeUI(userWidget);
            VLOG(STR("[MoriaCppMod] [Rename] Dialog opened\n"));
        }

        void hideRenameDialog()
        {
            if (!m_ftRenameVisible) return;
            if (m_ftRenameWidget)
            {
                auto* removeFn = m_ftRenameWidget->GetFunctionByNameInChain(STR("RemoveFromParent"));
                if (removeFn) m_ftRenameWidget->ProcessEvent(removeFn, nullptr);
                m_ftRenameWidget = nullptr;
            }
            m_ftRenameInput = nullptr;
            m_ftRenameConfirmLabel = nullptr;
            m_ftRenameVisible = false;
            // Restore input to settings panel if still open
            if (m_ftVisible && m_fontTestWidget)
                setInputModeUI(m_fontTestWidget);
            else
                setInputModeGame();
            VLOG(STR("[MoriaCppMod] [Rename] Dialog closed\n"));
        }

        void confirmRenameDialog()
        {
            if (!m_ftRenameVisible || !m_ftRenameInput) return;

            // Get text from EditableTextBox via GetText()
            auto* getFn = m_ftRenameInput->GetFunctionByNameInChain(STR("GetText"));
            if (!getFn) { showErrorBox(L"GetText not found on EditableTextBox"); hideRenameDialog(); return; }

            int gsz = getFn->GetParmsSize();
            std::vector<uint8_t> gbuf(gsz, 0);
            m_ftRenameInput->ProcessEvent(getFn, gbuf.data());

            auto* retProp = findParam(getFn, STR("ReturnValue"));
            if (!retProp) { hideRenameDialog(); return; }

            // GetText returns FText — use FText::ToString pattern
            std::wstring newName;
            auto* ftext = reinterpret_cast<FText*>(gbuf.data() + retProp->GetOffset_Internal());
            if (ftext->Data)
                newName = ftext->ToString();

            if (newName.empty()) { showErrorBox(L"Name cannot be empty"); return; }

            // Apply directly (same logic as applyPendingCharacterName but inline)
            {
                std::scoped_lock lock(m_charNameMutex);
                m_pendingCharName = newName;
            }
            m_pendingCharNameReady.store(true, std::memory_order_release);

            hideRenameDialog();
        }

        // Rebuild the Environment tab removal list
        void rebuildFtRemovalList()
        {
            if (!m_ftRemovalVBox) return;

            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!imageClass || !hboxClass || !vboxClass || !textBlockClass) return;

            UObject* outer = m_ftRemovalVBox->GetOuterPrivate();
            if (!outer) outer = m_ftRemovalVBox;

            auto* clearFn = m_ftRemovalVBox->GetFunctionByNameInChain(STR("ClearChildren"));
            if (clearFn) m_ftRemovalVBox->ProcessEvent(clearFn, nullptr);

            int count = s_config.removalCount.load();
            if (m_ftRemovalHeader)
            {
                umgSetText(m_ftRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
            }

            UObject* texDanger = findTexture2DByName(L"T_UI_Icon_Danger");
            UObject* defaultFont = nullptr;
            {
                std::vector<UObject*> fonts;
                UObjectGlobals::FindAllOf(STR("Font"), fonts);
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
                        UObject* nameTB = makeTB2(entry.friendlyName, 0.3f, 0.85f, 0.3f, 1.0f, 22);
                        if (nameTB) { umgSetBold(nameTB); addToVBox(infoVBox, nameTB); }
                        std::wstring coordText = entry.isTypeRule ? Loc::get("ui.type_rule") : entry.coordsW;
                        UObject* coordsTB = makeTB2(coordText, 0.85f, 0.25f, 0.25f, 1.0f, 16);
                        if (coordsTB) addToVBox(infoVBox, coordsTB);
                        UObject* infoSlot = addToHBox(rowHBox, infoVBox);
                        if (infoSlot) umgSetVAlign(infoSlot, 2);
                    }

                    addToVBox(m_ftRemovalVBox, rowHBox);
                }
            }
            m_ftLastRemovalCount = count;
        }

