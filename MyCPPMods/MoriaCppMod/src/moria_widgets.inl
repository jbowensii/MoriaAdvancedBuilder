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

        // Helper: returns the engine DPI scale so we can convert physical viewport pixels to
        // UMG logical pixels.  UE4 maps logical height Ã¢â€°Ë† 1080 at all resolutions via this scale.
        // WidgetLayoutLibrary::GetViewportScale(WorldContextObject) -> float
        float getViewportDpiScale(UObject* worldContext)
        {
            auto* fn  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                            STR("/Script/UMG.WidgetLayoutLibrary:GetViewportScale"));
            auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                            STR("/Script/UMG.Default__WidgetLayoutLibrary"));
            if (!fn || !cdo || !worldContext) return 1.0f;
            auto* pWC = findParam(fn, STR("WorldContextObject"));
            auto* pRV = findParam(fn, STR("ReturnValue"));
            int sz = fn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            if (pWC) *reinterpret_cast<UObject**>(buf.data() + pWC->GetOffset_Internal()) = worldContext;
            cdo->ProcessEvent(fn, buf.data());
            float scale = pRV ? *reinterpret_cast<float*>(buf.data() + pRV->GetOffset_Internal()) : 1.0f;
            return (scale > 0.1f) ? scale : 1.0f; // guard against 0 / garbage
        }

        // Compute the UI scale factor that maps design-space sizes (authored at 1080p logical)
        // to the current viewport.  physicalH comes from GetViewportSize(); dpiScale from
        // getViewportDpiScale().  Result Ã¢â€°Ë† 1.0 at all standard 16:9 resolutions.
        float computeUiScale(int32_t physicalH, float dpiScale)
        {
            float logicalH = static_cast<float>(physicalH) / dpiScale;
            float s = logicalH / 1080.0f;
            return (s < 0.5f) ? 0.5f : s; // minimum 0.5 for readability
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

            // Patch TypefaceFontName to "Bold"
            RC::Unreal::FName boldName(STR("Bold"), RC::Unreal::FNAME_Add);
            std::memcpy(fontBuf + FONT_TYPEFACE_NAME, &boldName, sizeof(RC::Unreal::FName));

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
            std::memcpy(fontBuf + FONT_SIZE, &fontSize, sizeof(int32_t));

            int sz = setFontFn->GetParmsSize();
            std::vector<uint8_t> buf(sz, 0);
            std::memcpy(buf.data() + pFontInfo->GetOffset_Internal(), fontBuf, FONT_STRUCT_SIZE);
            textBlock->ProcessEvent(setFontFn, buf.data());
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
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);

                // Get state icon size as the container bounds
                float containerW = 64.0f;
                float containerH = 64.0f;
                if (m_umgStateImages[slot])
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(m_umgStateImages[slot]);
                    containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
                showOnScreen(L"UMG: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
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
                showOnScreen(L"UMG: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Phase C1: Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"UMG: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"UMG: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            UObject* wblCDO = wblClass->GetClassDefaultObject();
            if (!wblCDO) { showOnScreen(L"UMG: WBL CDO null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

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
            if (!userWidget) { showOnScreen(L"UMG: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Phase C2: Get WidgetTree ---
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Phase C3: Build widget tree ---
            // Two nested Borders: outer = solid white line (2px padding), inner = fully transparent
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"UMG: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            m_umgSetBrushFn = setBrushFn; // cache for runtime state updates

            // Outer border: visible white outline
            FStaticConstructObjectParameters outerBorderP(borderClass, outer);
            UObject* outerBorder = UObjectGlobals::StaticConstructObject(outerBorderP);
            if (!outerBorder) { showOnScreen(L"UMG: outer border failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

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
            if (!innerBorder) { showOnScreen(L"UMG: inner border failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

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
            if (!hbox) { showOnScreen(L"UMG: HBox failed!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

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
                    frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
            // Get viewport size early for uiScale computation
            int32_t viewW = 1920, viewH = 1080; // fallback
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            m_cachedVW = viewW; m_cachedVH = viewH; // cache for drag loop
            VLOG(STR("[MoriaCppMod] [UMG] Viewport: {}x{}\n"), viewW, viewH);

            // --- DEBUG: Query engine DPI scale via GetViewportScale and WidgetLayoutLibrary ---
            // UUserWidget::GetViewportScale()
            float engineDpiScale = -1.0f;
            if (userWidget)
            {
                auto* gvsFn = userWidget->GetFunctionByNameInChain(STR("GetViewportScale"));
                if (gvsFn)
                {
                    struct { float ReturnValue{0.0f}; } gvsParams{};
                    userWidget->ProcessEvent(gvsFn, &gvsParams);
                    engineDpiScale = gvsParams.ReturnValue;
                }
            }
            // Also try WidgetLayoutLibrary::GetViewportScale(WorldContextObject)
            float wllDpiScale = -1.0f;
            auto* wllClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetLayoutLibrary"));
            if (wllClass) m_wllClass = wllClass; // cache for drag hit-test mouse queries
            if (wllClass)
            {
                auto* wllCDO = wllClass->GetClassDefaultObject();
                auto* wllFn = wllClass->GetFunctionByNameInChain(STR("GetViewportScale"));
                if (wllCDO && wllFn)
                {
                    auto* pWC2 = findParam(wllFn, STR("WorldContextObject"));
                    auto* pRV2 = findParam(wllFn, STR("ReturnValue"));
                    int sz2 = wllFn->GetParmsSize();
                    std::vector<uint8_t> wb(sz2, 0);
                    auto* pcForScale = findPlayerController();
                    if (pWC2 && pcForScale) *reinterpret_cast<UObject**>(wb.data() + pWC2->GetOffset_Internal()) = pcForScale;
                    wllCDO->ProcessEvent(wllFn, wb.data());
                    if (pRV2) wllDpiScale = *reinterpret_cast<float*>(wb.data() + pRV2->GetOffset_Internal());
                }
            }
            VLOG(STR("[MoriaCppMod] [UMG] DPI scale: UserWidget::GetViewportScale={:.4f}  WLL::GetViewportScale={:.4f}\n"),
                engineDpiScale, wllDpiScale);
            // --- END DEBUG ---

            // --- Compute true engine DPI scale: compare WLL slate viewport vs raw client pixels ---
            // WLL::GetViewportSize returns FVector2D in SLATE units; GetClientRect gives raw pixels.
            // dpiScale = rawPixels / slateUnits (e.g. 1.0 at 100% DPI, 1.5 at 150% DPI)
            float wllSlateW = -1.0f, wllSlateH = -1.0f;
            if (wllClass)
            {
                auto* wllCDO2 = wllClass->GetClassDefaultObject();
                auto* wllSizeFn = wllClass->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (wllCDO2 && wllSizeFn)
                {
                    auto* pWC3 = findParam(wllSizeFn, STR("WorldContextObject"));
                    auto* pSZ  = findParam(wllSizeFn, STR("Size"));
                    int sz3 = wllSizeFn->GetParmsSize();
                    std::vector<uint8_t> sb3(sz3, 0);
                    auto* pcForSize = findPlayerController();
                    if (pWC3 && pcForSize) *reinterpret_cast<UObject**>(sb3.data() + pWC3->GetOffset_Internal()) = pcForSize;
                    wllCDO2->ProcessEvent(wllSizeFn, sb3.data());
                    if (pSZ)
                    {
                        auto* sv = reinterpret_cast<float*>(sb3.data() + pSZ->GetOffset_Internal());
                        wllSlateW = sv[0]; wllSlateH = sv[1];
                    }
                }
            }
            // Compute dpiScale from slate vs raw pixels; fall back to GetViewportScale results
            if (wllSlateW > 0.0f && viewW > 0)
                m_dpiScale = static_cast<float>(viewW) / wllSlateW;
            else if (engineDpiScale > 0.0f)
                m_dpiScale = engineDpiScale;
            else if (wllDpiScale > 0.0f)
                m_dpiScale = wllDpiScale;
            else
                m_dpiScale = 1.0f; // fallback

            VLOG(STR("[MoriaCppMod] [UMG] DPI: slateVP={:.1f}x{:.1f} rawVP={}x{} dpiScale={:.4f}\n"),
                wllSlateW, wllSlateH, viewW, viewH, m_dpiScale);
            showOnScreen(std::format(L"DPI scale={:.3f}  slate={:.0f}x{:.0f}  raw={}x{}",
                m_dpiScale, wllSlateW, wllSlateH, viewW, viewH).c_str(), 8.0f, 1.0f, 1.0f, 0.0f);

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
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH), true);
            }

            // Cache size as FRACTION of viewport for resolution-independent hit-testing
            m_toolbarSizeW[0] = totalW / static_cast<float>(viewW);
            m_toolbarSizeH[0] = totalH / static_cast<float>(viewH);
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
                showOnScreen(L"AB: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
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
                showOnScreen(L"AB: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Phase C: Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"AB: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"AB: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
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
            if (!userWidget) { showOnScreen(L"AB: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Get WidgetTree ---
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"AB: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
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
                float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                if (texW > 0.0f && texH > 0.0f)
                {
                    uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                    float containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                    float containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
            float frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
            float frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
            uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
            float stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
            float stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);

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
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

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
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH), true);
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[1] = abTotalW / static_cast<float>(viewW);
            m_toolbarSizeH[1] = abTotalH / static_cast<float>(viewH);
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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

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
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH), true);
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
                int32_t viewW = 1920, viewH = 1080;
                auto* pcVp = findPlayerController();
                if (pcVp)
                {
                    auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                    if (vpFunc)
                    {
                        struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                        pcVp->ProcessEvent(vpFunc, &vpParams);
                        if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                        if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                    }
                }
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_targetInfoWidget, fracX * static_cast<float>(viewW),
                                                      fracY * static_cast<float>(viewH), true);
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

        /* BELIEVED DEAD CODE -- InfoBox popup system: widget created but never shown (showInfoBox never called)
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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
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
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

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
                setWidgetPosition(userWidget, fracX * static_cast<float>(viewW),
                                              fracY * static_cast<float>(viewH), true);
            }

            // Cache size for repositioning hit-test
            m_toolbarSizeW[3] = (400.0f * uiScale) / static_cast<float>(viewW);
            m_toolbarSizeH[3] = (80.0f * uiScale) / static_cast<float>(viewH);

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
                int32_t viewW = 1920, viewH = 1080;
                auto* pcVp = findPlayerController();
                if (pcVp)
                {
                    auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                    if (vpFunc)
                    {
                        struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                        pcVp->ProcessEvent(vpFunc, &vpParams);
                        if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                        if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                    }
                }
                float fracX = (m_toolbarPosX[3] >= 0) ? m_toolbarPosX[3] : TB_DEF_X[3];
                float fracY = (m_toolbarPosY[3] >= 0) ? m_toolbarPosY[3] : TB_DEF_Y[3];
                setWidgetPosition(m_infoBoxWidget, fracX * static_cast<float>(viewW),
                                                   fracY * static_cast<float>(viewH), true);
            }

            auto* fn = m_infoBoxWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (fn) { uint8_t p[8]{}; p[0] = 0; m_infoBoxWidget->ProcessEvent(fn, p); }

            m_ibShowTick = GetTickCount64();
        }
        END BELIEVED DEAD CODE */

        // Ã¢â€â‚¬Ã¢â€â‚¬ UMG Config Menu (first pass) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

        void destroyConfigWidget()
        {
            if (!m_configWidget) return;
            auto* removeFn = m_configWidget->GetFunctionByNameInChain(STR("RemoveFromViewport"));
            if (removeFn) m_configWidget->ProcessEvent(removeFn, nullptr);
            m_configWidget = nullptr;
            for (auto& t : m_cfgTabLabels) t = nullptr;
            for (auto& t : m_cfgTabContent) t = nullptr;
            for (auto& t : m_cfgTabImages) t = nullptr;
            m_cfgTabActiveTexture = nullptr;
            m_cfgTabInactiveTexture = nullptr;
            m_cfgVignetteImage = nullptr;
            for (auto& s : m_cfgScrollBoxes) s = nullptr;
            m_cfgFreeBuildLabel = nullptr;
            m_cfgFreeBuildCheckImg = nullptr;
            m_cfgNoCollisionLabel = nullptr;
            m_cfgNoCollisionCheckImg = nullptr;
            m_cfgUnlockBtnImg = nullptr;
            for (auto& k : m_cfgKeyValueLabels) k = nullptr;
            for (auto& k : m_cfgKeyBoxLabels) k = nullptr;
            m_cfgModifierLabel = nullptr;
            m_cfgModBoxLabel = nullptr;
            m_cfgRemovalHeader = nullptr;
            m_cfgRemovalVBox = nullptr;
            m_cfgLastRemovalCount = -1;
            m_cfgVisible = false;
            m_cfgActiveTab = 0;
        }

        void switchConfigTab(int tab)
        {
            if (tab < 0 || tab >= 3 || tab == m_cfgActiveTab) return;
            m_cfgActiveTab = tab;
            auto* sBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            for (int i = 0; i < 3; i++)
            {
                // Show/hide ScrollBoxes (which wrap the tab VBoxes)
                UObject* target = m_cfgScrollBoxes[i] ? m_cfgScrollBoxes[i] : m_cfgTabContent[i];
                if (target)
                {
                    auto* fn = target->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (fn) { uint8_t p[8]{}; p[0] = (i == tab) ? 0 : 1; target->ProcessEvent(fn, p); }
                }
                // Update tab label colors
                if (m_cfgTabLabels[i])
                {
                    if (i == tab)
                        umgSetTextColor(m_cfgTabLabels[i], 0.78f, 0.86f, 1.0f, 1.0f); // bright
                    else
                        umgSetTextColor(m_cfgTabLabels[i], 0.47f, 0.55f, 0.71f, 0.7f); // dim
                }
                // Swap tab background textures
                if (m_cfgTabImages[i] && sBrushFn)
                {
                    UObject* tex = (i == tab) ? m_cfgTabActiveTexture : m_cfgTabInactiveTexture;
                    if (tex) umgSetBrushNoMatch(m_cfgTabImages[i], tex, sBrushFn);
                }
            }
            // Cancel any active key capture when switching away from Tab 1
            if (tab != 1 && s_capturingBind >= 0)
            {
                s_capturingBind = -1;
                updateConfigKeyLabels();
            }
            // Refresh removal list when switching to Tab 2
            if (tab == 2)
            {
                int curCount = s_config.removalCount.load();
                if (curCount != m_cfgLastRemovalCount)
                    rebuildRemovalList();
            }
        }

        void updateConfigKeyLabels()
        {
            int capturing = s_capturingBind.load();
            for (int i = 0; i < BIND_COUNT; i++)
            {
                // Update old-style labels (kept for compat)
                if (m_cfgKeyValueLabels[i])
                {
                    std::wstring row = std::wstring(s_bindings[i].label) + Loc::get("ui.key_separator") + keyName(s_bindings[i].key);
                    umgSetText(m_cfgKeyValueLabels[i], row);
                }
                // Update new key box labels
                if (m_cfgKeyBoxLabels[i])
                {
                    if (capturing == i)
                    {
                        umgSetText(m_cfgKeyBoxLabels[i], Loc::get("ui.press_key"));
                        umgSetTextColor(m_cfgKeyBoxLabels[i], 1.0f, 0.9f, 0.0f, 1.0f); // yellow
                    }
                    else
                    {
                        umgSetText(m_cfgKeyBoxLabels[i], keyName(s_bindings[i].key));
                        umgSetTextColor(m_cfgKeyBoxLabels[i], 1.0f, 1.0f, 1.0f, 1.0f); // white
                    }
                }
            }
            if (m_cfgModifierLabel)
            {
                std::wstring modText = Loc::get("ui.set_modifier_key") + std::wstring(modifierName(s_modifierVK));
                umgSetText(m_cfgModifierLabel, modText);
            }
            // Update modifier key box label
            if (m_cfgModBoxLabel)
            {
                umgSetText(m_cfgModBoxLabel, std::wstring(modifierName(s_modifierVK)));
            }
        }

        void updateConfigFreeBuild()
        {
            bool on = s_config.freeBuild;
            if (m_cfgFreeBuildLabel)
            {
                umgSetText(m_cfgFreeBuildLabel, on ? Loc::get("ui.free_build_on") : Loc::get("ui.free_build"));
                umgSetTextColor(m_cfgFreeBuildLabel, on ? 0.31f : 0.55f, on ? 0.86f : 0.55f, on ? 0.47f : 0.55f, 1.0f);
            }
            // Show/hide check mark image
            if (m_cfgFreeBuildCheckImg)
            {
                auto* visFn = m_cfgFreeBuildCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; m_cfgFreeBuildCheckImg->ProcessEvent(visFn, p); }
            }
        }

        void updateConfigNoCollision()
        {
            bool on = m_noCollisionWhileFlying;
            if (m_cfgNoCollisionLabel)
            {
                umgSetText(m_cfgNoCollisionLabel, on ? Loc::get("ui.no_collision_on") : Loc::get("ui.no_collision"));
                umgSetTextColor(m_cfgNoCollisionLabel, on ? 0.31f : 0.55f, on ? 0.86f : 0.55f, on ? 0.47f : 0.55f, 1.0f);
            }
            if (m_cfgNoCollisionCheckImg)
            {
                auto* visFn = m_cfgNoCollisionCheckImg->GetFunctionByNameInChain(STR("SetVisibility"));
                if (visFn) { uint8_t p[8]{}; p[0] = on ? 0 : 1; m_cfgNoCollisionCheckImg->ProcessEvent(visFn, p); }
            }
        }

        void updateConfigRemovalCount()
        {
            if (m_cfgRemovalHeader)
            {
                int count = s_config.removalCount.load();
                umgSetText(m_cfgRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
            }
        }

        void rebuildRemovalList()
        {
            if (!m_cfgRemovalVBox) return;

            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!imageClass || !hboxClass || !vboxClass || !textBlockClass) return;

            // Get the outer from the VBox
            UObject* outer = m_cfgRemovalVBox->GetOuterPrivate();
            if (!outer) outer = m_cfgRemovalVBox;

            // Helper: add widget to VBox
            auto addToVBox = [&](UObject* vbox, UObject* child) {
                auto* fn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (!fn) return;
                auto* pC2 = findParam(fn, STR("Content"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                vbox->ProcessEvent(fn, ap.data());
            };

            // Clear existing entry rows (all children after the header)
            // Use ClearChildren then re-add header
            auto* clearFn = m_cfgRemovalVBox->GetFunctionByNameInChain(STR("ClearChildren"));
            if (clearFn) m_cfgRemovalVBox->ProcessEvent(clearFn, nullptr);

            // Re-add header
            int count = s_config.removalCount.load();
            if (m_cfgRemovalHeader)
            {
                umgSetText(m_cfgRemovalHeader, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"));
                addToVBox(m_cfgRemovalVBox, m_cfgRemovalHeader);
            }

            UObject* texDanger = findTexture2DByName(L"T_UI_Icon_Danger");

            if (s_config.removalCSInit)
            {
                CriticalSectionLock removalLock(s_config.removalCS);
                for (size_t i = 0; i < s_config.removalEntries.size(); i++)
                {
                    const auto& entry = s_config.removalEntries[i];

                    // Each row: HBox { Image(danger, 40x40) + VBox { TextBlock(name, bold 24pt), TextBlock(coords, 18pt gray) } }
                    FStaticConstructObjectParameters rowP(hboxClass, outer);
                    UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowP);
                    if (!rowHBox) continue;

                    auto* addToHFn = rowHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                    if (!addToHFn) continue;
                    auto* hbC = findParam(addToHFn, STR("Content"));

                    // Danger icon
                    if (texDanger && setBrushFn)
                    {
                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* dangerImg = UObjectGlobals::StaticConstructObject(imgP);
                        if (dangerImg)
                        {
                            umgSetBrushNoMatch(dangerImg, texDanger, setBrushFn);
                            umgSetBrushSize(dangerImg, 56.0f, 56.0f);
                            int sz = addToHFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = dangerImg;
                            rowHBox->ProcessEvent(addToHFn, ap.data());
                        }
                    }

                    // Info VBox: name + coords
                    FStaticConstructObjectParameters infoP(vboxClass, outer);
                    UObject* infoVBox = UObjectGlobals::StaticConstructObject(infoP);
                    if (infoVBox)
                    {
                        // Name (bold)
                        FStaticConstructObjectParameters tbP1(textBlockClass, outer);
                        UObject* nameTB = UObjectGlobals::StaticConstructObject(tbP1);
                        if (nameTB)
                        {
                            umgSetText(nameTB, entry.friendlyName);
                            umgSetTextColor(nameTB, 0.3f, 0.85f, 0.3f, 1.0f); // medium green
                            umgSetFontSize(nameTB, 24);
                            umgSetBold(nameTB);
                            addToVBox(infoVBox, nameTB);
                        }
                        // Coords or "TYPE RULE"
                        FStaticConstructObjectParameters tbP2(textBlockClass, outer);
                        UObject* coordsTB = UObjectGlobals::StaticConstructObject(tbP2);
                        if (coordsTB)
                        {
                            std::wstring coordText = entry.isTypeRule ? Loc::get("ui.type_rule") : entry.coordsW;
                            umgSetText(coordsTB, coordText);
                            umgSetTextColor(coordsTB, 0.85f, 0.25f, 0.25f, 1.0f); // medium red
                            umgSetFontSize(coordsTB, 18);
                            addToVBox(infoVBox, coordsTB);
                        }

                        int sz = addToHFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = infoVBox;
                        rowHBox->ProcessEvent(addToHFn, ap.data());
                    }

                    addToVBox(m_cfgRemovalVBox, rowHBox);
                }
            }
            m_cfgLastRemovalCount = count;
        }

        void createConfigWidget()
        {
            if (m_configWidget) return;
            VLOG(STR("[MoriaCppMod] [CFG] === Creating Config UMG widget ===\n"));

            auto* userWidgetClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
            auto* vboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.VerticalBox"));
            auto* hboxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.HorizontalBox"));
            auto* borderClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Border"));
            auto* textBlockClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TextBlock"));
            auto* overlayClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Overlay"));
            auto* imageClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            auto* scrollBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ScrollBox"));
            auto* sizeBoxClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.SizeBox"));
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!userWidgetClass || !vboxClass || !hboxClass || !borderClass || !textBlockClass) return;
            if (!overlayClass || !imageClass || !scrollBoxClass || !sizeBoxClass || !setBrushFn) return;

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

            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // Make widget focusable for modal input mode (via FBoolProperty API)
            setBoolProp(userWidget, L"bIsFocusable", true);

            // Root SizeBox to enforce fixed 1400x450 size
            FStaticConstructObjectParameters rootSbP(sizeBoxClass, outer);
            UObject* rootSizeBox = UObjectGlobals::StaticConstructObject(rootSbP);
            if (!rootSizeBox) return;
            if (widgetTree)
                setRootWidget(widgetTree, rootSizeBox);
            // SetWidthOverride(1400) and SetHeightOverride(900)
            auto* setWidthOvFn = rootSizeBox->GetFunctionByNameInChain(STR("SetWidthOverride"));
            if (setWidthOvFn) { int sz = setWidthOvFn->GetParmsSize(); std::vector<uint8_t> wp(sz, 0); auto* p = findParam(setWidthOvFn, STR("InWidthOverride")); if (p) *reinterpret_cast<float*>(wp.data() + p->GetOffset_Internal()) = 1400.0f; rootSizeBox->ProcessEvent(setWidthOvFn, wp.data()); }
            auto* setHeightOvFn = rootSizeBox->GetFunctionByNameInChain(STR("SetHeightOverride"));
            if (setHeightOvFn) { int sz = setHeightOvFn->GetParmsSize(); std::vector<uint8_t> hp(sz, 0); auto* p = findParam(setHeightOvFn, STR("InHeightOverride")); if (p) *reinterpret_cast<float*>(hp.data() + p->GetOffset_Internal()) = 900.0f; rootSizeBox->ProcessEvent(setHeightOvFn, hp.data()); }
            // SetClipping(ClipToBounds) Ã¢â‚¬â€ clip overflow content to SizeBox bounds
            auto* setClipFn = rootSizeBox->GetFunctionByNameInChain(STR("SetClipping"));
            if (setClipFn) { int sz = setClipFn->GetParmsSize(); std::vector<uint8_t> cp(sz, 0); auto* p = findParam(setClipFn, STR("InClipping")); if (p) *reinterpret_cast<uint8_t*>(cp.data() + p->GetOffset_Internal()) = 1; /* ClipToBounds */ rootSizeBox->ProcessEvent(setClipFn, cp.data()); }

            // Root Overlay (stacks vignette image behind content) Ã¢â‚¬â€ child of SizeBox
            FStaticConstructObjectParameters rootOlP(overlayClass, outer);
            UObject* rootOverlay = UObjectGlobals::StaticConstructObject(rootOlP);
            if (!rootOverlay) return;
            // Add overlay as SizeBox content
            auto* setSbContentFn = rootSizeBox->GetFunctionByNameInChain(STR("SetContent"));
            if (setSbContentFn)
            {
                auto* pC = findParam(setSbContentFn, STR("Content"));
                int sz = setSbContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pC) *reinterpret_cast<UObject**>(sc.data() + pC->GetOffset_Internal()) = rootOverlay;
                rootSizeBox->ProcessEvent(setSbContentFn, sc.data());
            }

            auto* addToOverlayFn = rootOverlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
            if (!addToOverlayFn) return;
            auto* olC = findParam(addToOverlayFn, STR("Content"));

            // Layer 0: Vignette border image (tinted dark blue)
            UObject* texVignette = findTexture2DByName(L"T_UI_Waypoint_Vignette_White_Optimized");
            if (texVignette)
            {
                FStaticConstructObjectParameters vigP(imageClass, outer);
                UObject* vigImg = UObjectGlobals::StaticConstructObject(vigP);
                if (vigImg)
                {
                    umgSetBrush(vigImg, texVignette, setBrushFn);
                    // Tint to dark blue
                    auto* setColorFn = vigImg->GetFunctionByNameInChain(STR("SetColorAndOpacity"));
                    if (setColorFn)
                    {
                        auto* pColor = findParam(setColorFn, STR("InColorAndOpacity"));
                        if (pColor)
                        {
                            int sz = setColorFn->GetParmsSize();
                            std::vector<uint8_t> cb(sz, 0);
                            auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                            c[0] = 0.059f; c[1] = 0.071f; c[2] = 0.110f; c[3] = 0.92f;
                            vigImg->ProcessEvent(setColorFn, cb.data());
                        }
                    }
                    int sz = addToOverlayFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (olC) *reinterpret_cast<UObject**>(ap.data() + olC->GetOffset_Internal()) = vigImg;
                    rootOverlay->ProcessEvent(addToOverlayFn, ap.data());
                    m_cfgVignetteImage = vigImg;
                }
            }
            else
                VLOG(STR("[MoriaCppMod] [CFG] Vignette texture not found, skipping border\n"));

            // Layer 1: Transparent Border with padding (content container)
            FStaticConstructObjectParameters borderP(borderClass, outer);
            UObject* rootBorder = UObjectGlobals::StaticConstructObject(borderP);
            if (!rootBorder) return;
            auto* setBrushColorFn = rootBorder->GetFunctionByNameInChain(STR("SetBrushColor"));
            if (setBrushColorFn)
            {
                auto* pColor = findParam(setBrushColorFn, STR("InBrushColor"));
                if (pColor)
                {
                    int sz = setBrushColorFn->GetParmsSize();
                    std::vector<uint8_t> cb(sz, 0);
                    // Semi-transparent dark blue background (50% opacity)
                    auto* c = reinterpret_cast<float*>(cb.data() + pColor->GetOffset_Internal());
                    c[0] = 0.059f; c[1] = 0.071f; c[2] = 0.110f; c[3] = 0.50f;
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
                    m[0] = 40.0f; m[1] = 28.0f; m[2] = 40.0f; m[3] = 28.0f;
                    rootBorder->ProcessEvent(setBorderPadFn, pp.data());
                }
            }
            // Add border to overlay layer 1
            {
                int sz = addToOverlayFn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (olC) *reinterpret_cast<UObject**>(ap.data() + olC->GetOffset_Internal()) = rootBorder;
                rootOverlay->ProcessEvent(addToOverlayFn, ap.data());
            }

            // Main VBox inside the border
            FStaticConstructObjectParameters mainVP(vboxClass, outer);
            UObject* mainVBox = UObjectGlobals::StaticConstructObject(mainVP);
            if (!mainVBox) return;
            auto* setContentFn = rootBorder->GetFunctionByNameInChain(STR("SetContent"));
            if (setContentFn)
            {
                auto* pContent = findParam(setContentFn, STR("Content"));
                int sz = setContentFn->GetParmsSize();
                std::vector<uint8_t> sc(sz, 0);
                if (pContent) *reinterpret_cast<UObject**>(sc.data() + pContent->GetOffset_Internal()) = mainVBox;
                rootBorder->ProcessEvent(setContentFn, sc.data());
            }

            // Helper: add widget to a VBox, returns slot for further configuration
            auto addToVBox = [&](UObject* vbox, UObject* child) -> UObject* {
                auto* fn = vbox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (!fn) return nullptr;
                auto* pC2 = findParam(fn, STR("Content"));
                auto* pRV = findParam(fn, STR("ReturnValue"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                vbox->ProcessEvent(fn, ap.data());
                return pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
            };

            // Helper: create TextBlock with optional font size (0 = default)
            auto makeTB = [&](const std::wstring& text, float r, float g, float b, float a, int32_t fontSize = 0) -> UObject* {
                FStaticConstructObjectParameters tbP(textBlockClass, outer);
                UObject* tb = UObjectGlobals::StaticConstructObject(tbP);
                if (!tb) return nullptr;
                umgSetText(tb, text);
                umgSetTextColor(tb, r, g, b, a);
                if (fontSize > 0) umgSetFontSize(tb, fontSize);
                return tb;
            };

            // Helper: add TextBlock to a VBox and return it
            auto addTB = [&](UObject* vbox, const std::wstring& text, float r, float g, float b, float a, int32_t fontSize = 0) -> UObject* {
                UObject* tb = makeTB(text, r, g, b, a, fontSize);
                if (tb) addToVBox(vbox, tb);
                return tb;
            };

            // Helper: add child to a ScrollBox (uses UPanelWidget::AddChild)
            auto addToScrollBox = [&](UObject* scrollBox, UObject* child) {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("AddChild"));
                if (!fn) return;
                auto* pC2 = findParam(fn, STR("Content"));
                int sz = fn->GetParmsSize();
                std::vector<uint8_t> ap(sz, 0);
                if (pC2) *reinterpret_cast<UObject**>(ap.data() + pC2->GetOffset_Internal()) = child;
                scrollBox->ProcessEvent(fn, ap.data());
            };

            // Title
            addTB(mainVBox, Loc::get("ui.config_title"), 0.78f, 0.86f, 1.0f, 1.0f, 36);
            addTB(mainVBox, L"--------------------------------------------", 0.31f, 0.51f, 0.78f, 0.4f, 20);

            // Tab bar: HBox with texture-backed tabs
            UObject* texP1 = findTexture2DByName(L"T_UI_Btn_P1_Up");
            UObject* texP2 = findTexture2DByName(L"T_UI_Btn_P2_Up");
            if (!texP1) texP1 = findTexture2DByName(L"T_UI_Btn_HUD_EpicAB_Focused");   // fallback
            if (!texP2) texP2 = findTexture2DByName(L"T_UI_Btn_HUD_EpicAB_Disabled");   // fallback
            m_cfgTabActiveTexture = texP1;
            m_cfgTabInactiveTexture = texP2;

            {
                FStaticConstructObjectParameters hbP(hboxClass, outer);
                UObject* tabHBox = UObjectGlobals::StaticConstructObject(hbP);
                if (tabHBox)
                {
                    addToVBox(mainVBox, tabHBox);
                    auto* addToHBoxFn = tabHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                    const wchar_t* tabNames[3] = {CONFIG_TAB_NAMES[0], CONFIG_TAB_NAMES[1], CONFIG_TAB_NAMES[2]};

                    for (int t = 0; t < 3; t++)
                    {
                        // Each tab = Overlay { UImage(texture) + TextBlock(label) }
                        FStaticConstructObjectParameters tolP(overlayClass, outer);
                        UObject* tabOl = UObjectGlobals::StaticConstructObject(tolP);
                        if (!tabOl) continue;

                        auto* addToTabOlFn = tabOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                        if (!addToTabOlFn) continue;
                        auto* tolC = findParam(addToTabOlFn, STR("Content"));
                        auto* tolR = findParam(addToTabOlFn, STR("ReturnValue"));

                        // Layer 0: tab background image (sized to cover text)
                        FStaticConstructObjectParameters imgP(imageClass, outer);
                        UObject* tabImg = UObjectGlobals::StaticConstructObject(imgP);
                        m_cfgTabImages[t] = tabImg;
                        if (tabImg)
                        {
                            UObject* tex = (t == 0) ? texP1 : texP2; // tab 0 active by default
                            if (tex) umgSetBrushNoMatch(tabImg, tex, setBrushFn);
                            umgSetBrushSize(tabImg, 420.0f, 66.0f);
                            int sz = addToTabOlFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (tolC) *reinterpret_cast<UObject**>(ap.data() + tolC->GetOffset_Internal()) = tabImg;
                            tabOl->ProcessEvent(addToTabOlFn, ap.data());
                        }

                        // Layer 1: tab label text
                        UObject* tabLabel = makeTB(tabNames[t],
                                                   (t == 0) ? 0.78f : 0.47f,
                                                   (t == 0) ? 0.86f : 0.55f,
                                                   (t == 0) ? 1.0f  : 0.71f,
                                                   (t == 0) ? 1.0f  : 0.7f,
                                                   28);
                        m_cfgTabLabels[t] = tabLabel;
                        if (tabLabel)
                        {
                            int sz = addToTabOlFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (tolC) *reinterpret_cast<UObject**>(ap.data() + tolC->GetOffset_Internal()) = tabLabel;
                            tabOl->ProcessEvent(addToTabOlFn, ap.data());
                            UObject* labelSlot = tolR ? *reinterpret_cast<UObject**>(ap.data() + tolR->GetOffset_Internal()) : nullptr;
                            if (labelSlot)
                            {
                                auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                                auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                            }
                        }

                        // Add tab overlay to HBox
                        if (addToHBoxFn)
                        {
                            auto* hbC = findParam(addToHBoxFn, STR("Content"));
                            int sz = addToHBoxFn->GetParmsSize();
                            std::vector<uint8_t> ap(sz, 0);
                            if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = tabOl;
                            tabHBox->ProcessEvent(addToHBoxFn, ap.data());
                        }
                    }
                }
            }

            addTB(mainVBox, L"-------------------------------------------------------------", 0.31f, 0.51f, 0.78f, 0.4f, 20);

            // Helper: configure ScrollBox to always show scrollbar
            auto configureScrollBox = [&](UObject* scrollBox) {
                auto* fn = scrollBox->GetFunctionByNameInChain(STR("SetAlwaysShowScrollbar"));
                if (fn) {
                    auto* p = findParam(fn, STR("NewAlwaysShowScrollbar"));
                    int sz = fn->GetParmsSize();
                    std::vector<uint8_t> buf(sz, 0);
                    if (p) *reinterpret_cast<bool*>(buf.data() + p->GetOffset_Internal()) = true;
                    scrollBox->ProcessEvent(fn, buf.data());
                }
            };

            // Tab 0: Optional Mods (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[0] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab0VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[0] = tab0VBox;

                if (scrollBox && tab0VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab0VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }

                    addTB(tab0VBox, Loc::get("ui.cheat_toggles"), 0.78f, 0.86f, 1.0f, 1.0f, 32);

                    // Free Build checkbox row: HBox { Overlay{checkbox+check} + TextBlock }
                    {
                        FStaticConstructObjectParameters hbP(hboxClass, outer);
                        UObject* cbRow = UObjectGlobals::StaticConstructObject(hbP);
                        if (cbRow)
                        {
                            addToVBox(tab0VBox, cbRow);
                            auto* addToHFn = cbRow->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));

                            // Checkbox overlay: background + check mark
                            FStaticConstructObjectParameters olP(overlayClass, outer);
                            UObject* cbOl = UObjectGlobals::StaticConstructObject(olP);
                            if (cbOl && addToHFn)
                            {
                                auto* addToOlFn = cbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                // Layer 0: checkbox background (always visible)
                                UObject* texCB = findTexture2DByName(L"T_UI_Icon_Checkbox_DiamondBG");
                                if (texCB && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* cbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (cbBgImg)
                                    {
                                        umgSetBrushNoMatch(cbBgImg, texCB, setBrushFn);
                                        umgSetBrushSize(cbBgImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = cbBgImg;
                                        cbOl->ProcessEvent(addToOlFn, ap.data());
                                    }
                                }
                                // Layer 1: check mark (shown/hidden based on state)
                                UObject* texCheck = findTexture2DByName(L"T_UI_Icon_Check");
                                if (texCheck && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* checkImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (checkImg)
                                    {
                                        umgSetBrushNoMatch(checkImg, texCheck, setBrushFn);
                                        umgSetBrushSize(checkImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = checkImg;
                                        cbOl->ProcessEvent(addToOlFn, ap.data());
                                        m_cfgFreeBuildCheckImg = checkImg;
                                        // Start hidden (will be shown by updateConfigFreeBuild)
                                        auto* visFn = checkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                        if (visFn) { uint8_t p[8]{}; p[0] = 1; checkImg->ProcessEvent(visFn, p); }
                                    }
                                }

                                // Add checkbox overlay to HBox
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = cbOl;
                                cbRow->ProcessEvent(addToHFn, ap.data());
                            }

                            // Label: "Free Build" text next to checkbox
                            UObject* fbLabel = makeTB(Loc::get("ui.free_build"), 0.55f, 0.55f, 0.55f, 1.0f, 26);
                            m_cfgFreeBuildLabel = fbLabel;
                            if (fbLabel && addToHFn)
                            {
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = fbLabel;
                                cbRow->ProcessEvent(addToHFn, ap.data());
                            }
                        }
                    }

                    addTB(tab0VBox, Loc::get("ui.free_build_desc"), 0.47f, 0.55f, 0.71f, 0.6f, 24);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 12);

                    // No Collision (Flying) checkbox row: HBox { Overlay{checkbox+check} + TextBlock }
                    {
                        FStaticConstructObjectParameters hbP(hboxClass, outer);
                        UObject* ncRow = UObjectGlobals::StaticConstructObject(hbP);
                        if (ncRow)
                        {
                            addToVBox(tab0VBox, ncRow);
                            auto* addToHFn = ncRow->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));

                            // Checkbox overlay: background + check mark
                            FStaticConstructObjectParameters olP(overlayClass, outer);
                            UObject* ncOl = UObjectGlobals::StaticConstructObject(olP);
                            if (ncOl && addToHFn)
                            {
                                auto* addToOlFn = ncOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                // Layer 0: checkbox background (always visible)
                                UObject* texCB = findTexture2DByName(L"T_UI_Icon_Checkbox_DiamondBG");
                                if (texCB && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* cbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (cbBgImg)
                                    {
                                        umgSetBrushNoMatch(cbBgImg, texCB, setBrushFn);
                                        umgSetBrushSize(cbBgImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = cbBgImg;
                                        ncOl->ProcessEvent(addToOlFn, ap.data());
                                    }
                                }
                                // Layer 1: check mark (shown/hidden based on state)
                                UObject* texCheck = findTexture2DByName(L"T_UI_Icon_Check");
                                if (texCheck && addToOlFn)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* checkImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (checkImg)
                                    {
                                        umgSetBrushNoMatch(checkImg, texCheck, setBrushFn);
                                        umgSetBrushSize(checkImg, 48.0f, 48.0f);
                                        auto* pCC = findParam(addToOlFn, STR("Content"));
                                        int sz = addToOlFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = checkImg;
                                        ncOl->ProcessEvent(addToOlFn, ap.data());
                                        m_cfgNoCollisionCheckImg = checkImg;
                                        // Start hidden (will be shown by updateConfigNoCollision)
                                        auto* visFn = checkImg->GetFunctionByNameInChain(STR("SetVisibility"));
                                        if (visFn) { uint8_t p[8]{}; p[0] = 1; checkImg->ProcessEvent(visFn, p); }
                                    }
                                }

                                // Add checkbox overlay to HBox
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = ncOl;
                                ncRow->ProcessEvent(addToHFn, ap.data());
                            }

                            // Label: "No Collision (Flying)" text next to checkbox
                            UObject* ncLabel = makeTB(Loc::get("ui.no_collision"), 0.55f, 0.55f, 0.55f, 1.0f, 26);
                            m_cfgNoCollisionLabel = ncLabel;
                            if (ncLabel && addToHFn)
                            {
                                auto* hbC = findParam(addToHFn, STR("Content"));
                                int sz = addToHFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (hbC) *reinterpret_cast<UObject**>(ap.data() + hbC->GetOffset_Internal()) = ncLabel;
                                ncRow->ProcessEvent(addToHFn, ap.data());
                            }
                        }
                    }

                    addTB(tab0VBox, Loc::get("ui.no_collision_desc"), 0.47f, 0.55f, 0.71f, 0.6f, 24);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 12);

                    // "Unlock All Recipes" button with T_UI_Btn_P2_Active texture
                    {
                        UObject* texBtn = findTexture2DByName(L"T_UI_Btn_P2_Active");
                        FStaticConstructObjectParameters olP(overlayClass, outer);
                        UObject* btnOl = UObjectGlobals::StaticConstructObject(olP);
                        if (btnOl)
                        {
                            auto* addToOlFn = btnOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                            if (addToOlFn && texBtn)
                            {
                                // Layer 0: button background image
                                FStaticConstructObjectParameters imgP2(imageClass, outer);
                                UObject* btnImg = UObjectGlobals::StaticConstructObject(imgP2);
                                if (btnImg)
                                {
                                    umgSetBrushNoMatch(btnImg, texBtn, setBrushFn);
                                    umgSetBrushSize(btnImg, 420.0f, 66.0f);
                                    auto* pCC = findParam(addToOlFn, STR("Content"));
                                    int sz = addToOlFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = btnImg;
                                    btnOl->ProcessEvent(addToOlFn, ap.data());
                                    m_cfgUnlockBtnImg = btnImg;
                                }
                            }
                            if (addToOlFn)
                            {
                                // Layer 1: "Unlock All Recipes" label
                                UObject* btnLabel = makeTB(Loc::get("ui.unlock_all_recipes"), 0.86f, 0.90f, 1.0f, 0.95f, 26);
                                if (btnLabel)
                                {
                                    auto* pCC = findParam(addToOlFn, STR("Content"));
                                    auto* pRV = findParam(addToOlFn, STR("ReturnValue"));
                                    int sz = addToOlFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = btnLabel;
                                    btnOl->ProcessEvent(addToOlFn, ap.data());
                                    // Center the label on the button
                                    UObject* labelSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                    if (labelSlot)
                                    {
                                        auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                        if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                                        auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                        if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                                    }
                                }
                            }
                            // Add button to VBox center-justified
                            auto* addBtnFn = tab0VBox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                            if (addBtnFn)
                            {
                                auto* pC = findParam(addBtnFn, STR("Content"));
                                auto* pR = findParam(addBtnFn, STR("ReturnValue"));
                                int sz = addBtnFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = btnOl;
                                tab0VBox->ProcessEvent(addBtnFn, ap.data());
                                UObject* btnSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                                if (btnSlot)
                                {
                                    // Center-justify the button
                                    auto* setHA = btnSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                    if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; /* HAlign_Center */ btnSlot->ProcessEvent(setHA, h.data()); }
                                }
                            }
                        }
                    }

                    // Pad tab 0 content Ã¢â‚¬â€ add spacer lines so it visually fills the scroll area like other tabs
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                    addTB(tab0VBox, L"", 0.0f, 0.0f, 0.0f, 0.0f, 32);
                }
            }

            // Tab 1: Key Mapping (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[1] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab1VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[1] = tab1VBox;

                if (scrollBox && tab1VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab1VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }

                    // Section heading background texture
                    UObject* texSectionBg = findTexture2DByName(L"T_UI_Map_LocationName_HUD");
                    // Key box background texture
                    UObject* texKeyBox = findTexture2DByName(L"T_UI_Btn_P1_Active");

                    const wchar_t* lastSection = nullptr;
                    for (int b = 0; b < BIND_COUNT; b++)
                    {
                        // Section header with background texture
                        if (!lastSection || wcscmp(lastSection, s_bindings[b].section) != 0)
                        {
                            lastSection = s_bindings[b].section;
                            if (texSectionBg)
                            {
                                FStaticConstructObjectParameters solP(overlayClass, outer);
                                UObject* secOl = UObjectGlobals::StaticConstructObject(solP);
                                if (secOl)
                                {
                                    auto* addToSecOlFn = secOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                    if (addToSecOlFn)
                                    {
                                        // Layer 0: background image
                                        FStaticConstructObjectParameters imgP2(imageClass, outer);
                                        UObject* secBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                        if (secBgImg)
                                        {
                                            umgSetBrushNoMatch(secBgImg, texSectionBg, setBrushFn);
                                            umgSetBrushSize(secBgImg, 1300.0f, 40.0f);
                                            auto* pCC = findParam(addToSecOlFn, STR("Content"));
                                            int sz = addToSecOlFn->GetParmsSize();
                                            std::vector<uint8_t> ap(sz, 0);
                                            if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = secBgImg;
                                            secOl->ProcessEvent(addToSecOlFn, ap.data());
                                        }
                                        // Layer 1: section name text
                                        UObject* secLabel = makeTB(std::wstring(lastSection), 0.78f, 0.86f, 1.0f, 1.0f, 32);
                                        if (secLabel)
                                        {
                                            umgSetBold(secLabel);
                                            auto* pCC = findParam(addToSecOlFn, STR("Content"));
                                            auto* pRV = findParam(addToSecOlFn, STR("ReturnValue"));
                                            int sz = addToSecOlFn->GetParmsSize();
                                            std::vector<uint8_t> ap(sz, 0);
                                            if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = secLabel;
                                            secOl->ProcessEvent(addToSecOlFn, ap.data());
                                            // Center vertically, left-align
                                            UObject* secSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                            if (secSlot)
                                            {
                                                auto* setVA = secSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                                if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; secSlot->ProcessEvent(setVA, v.data()); }
                                            }
                                        }
                                    }
                                    addToVBox(tab1VBox, secOl);
                                }
                            }
                            else
                            {
                                addTB(tab1VBox, std::wstring(lastSection), 0.78f, 0.86f, 1.0f, 1.0f, 32);
                            }
                        }

                        // Key binding row: HBox { Label(left) + Overlay{KeyBoxImg+KeyText}(right) }
                        FStaticConstructObjectParameters rowHbP(hboxClass, outer);
                        UObject* rowHBox = UObjectGlobals::StaticConstructObject(rowHbP);
                        if (rowHBox)
                        {
                            auto* addToRowFn = rowHBox->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                            if (!addToRowFn) continue;
                            auto* rowC = findParam(addToRowFn, STR("Content"));
                            auto* rowR = findParam(addToRowFn, STR("ReturnValue"));

                            // Left: binding label
                            UObject* bindLabel = makeTB(std::wstring(s_bindings[b].label), 0.86f, 0.90f, 0.96f, 0.85f, 26);
                            if (bindLabel)
                            {
                                int sz = addToRowFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = bindLabel;
                                rowHBox->ProcessEvent(addToRowFn, ap.data());
                                // Make label fill available space (push key box to right)
                                UObject* labelSlot = rowR ? *reinterpret_cast<UObject**>(ap.data() + rowR->GetOffset_Internal()) : nullptr;
                                if (labelSlot)
                                {
                                    auto* setFill = labelSlot->GetFunctionByNameInChain(STR("SetSize"));
                                    if (setFill) { int s2 = setFill->GetParmsSize(); std::vector<uint8_t> fp(s2, 0); auto* p = findParam(setFill, STR("InSize")); if (p) { *reinterpret_cast<float*>(fp.data() + p->GetOffset_Internal()) = 1.0f; fp[p->GetOffset_Internal() + 4] = 1; /* SizeRule=Fill */ } labelSlot->ProcessEvent(setFill, fp.data()); }
                                }
                            }

                            // Right: key box = Overlay { Image(key box bg) + TextBlock(key name) }
                            FStaticConstructObjectParameters kbOlP(overlayClass, outer);
                            UObject* kbOl = UObjectGlobals::StaticConstructObject(kbOlP);
                            if (kbOl)
                            {
                                auto* addToKbFn = kbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                if (addToKbFn && texKeyBox)
                                {
                                    // Layer 0: key box background
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* kbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (kbBgImg)
                                    {
                                        umgSetBrushNoMatch(kbBgImg, texKeyBox, setBrushFn);
                                        umgSetBrushSize(kbBgImg, 220.0f, 42.0f);
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbBgImg;
                                        kbOl->ProcessEvent(addToKbFn, ap.data());
                                    }
                                }
                                if (addToKbFn)
                                {
                                    // Layer 1: key name text
                                    UObject* kbLabel = makeTB(keyName(s_bindings[b].key), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                    m_cfgKeyBoxLabels[b] = kbLabel;
                                    if (kbLabel)
                                    {
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        auto* pRV = findParam(addToKbFn, STR("ReturnValue"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbLabel;
                                        kbOl->ProcessEvent(addToKbFn, ap.data());
                                        // Center text on key box
                                        UObject* kbSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                        if (kbSlot)
                                        {
                                            auto* setHA = kbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = kbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; kbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                    }
                                }
                                // Add key box overlay to row HBox
                                if (addToRowFn)
                                {
                                    int sz = addToRowFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = kbOl;
                                    rowHBox->ProcessEvent(addToRowFn, ap.data());
                                }
                            }

                            addToVBox(tab1VBox, rowHBox);
                        }
                    }

                    // Modifier key row (same layout as regular rows)
                    {
                        FStaticConstructObjectParameters rowHbP2(hboxClass, outer);
                        UObject* modRow = UObjectGlobals::StaticConstructObject(rowHbP2);
                        if (modRow)
                        {
                            auto* addToRowFn = modRow->GetFunctionByNameInChain(STR("AddChildToHorizontalBox"));
                            auto* rowC = findParam(addToRowFn, STR("Content"));
                            auto* rowR = findParam(addToRowFn, STR("ReturnValue"));

                            // Left: "Set Modifier Key" label
                            UObject* modLabel = makeTB(Loc::get("ui.set_modifier_key_short"), 0.86f, 0.90f, 0.96f, 0.85f, 26);
                            if (modLabel && addToRowFn)
                            {
                                int sz = addToRowFn->GetParmsSize();
                                std::vector<uint8_t> ap(sz, 0);
                                if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = modLabel;
                                modRow->ProcessEvent(addToRowFn, ap.data());
                                UObject* labelSlot = rowR ? *reinterpret_cast<UObject**>(ap.data() + rowR->GetOffset_Internal()) : nullptr;
                                if (labelSlot)
                                {
                                    auto* setFill = labelSlot->GetFunctionByNameInChain(STR("SetSize"));
                                    if (setFill) { int s2 = setFill->GetParmsSize(); std::vector<uint8_t> fp(s2, 0); auto* p = findParam(setFill, STR("InSize")); if (p) { *reinterpret_cast<float*>(fp.data() + p->GetOffset_Internal()) = 1.0f; fp[p->GetOffset_Internal() + 4] = 1; /* SizeRule=Fill */ } labelSlot->ProcessEvent(setFill, fp.data()); }
                                }
                            }

                            // Right: modifier key box
                            FStaticConstructObjectParameters kbOlP2(overlayClass, outer);
                            UObject* modKbOl = UObjectGlobals::StaticConstructObject(kbOlP2);
                            if (modKbOl)
                            {
                                auto* addToKbFn = modKbOl->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                                UObject* texKeyBox2 = findTexture2DByName(L"T_UI_Btn_P1_Active");
                                if (addToKbFn && texKeyBox2)
                                {
                                    FStaticConstructObjectParameters imgP2(imageClass, outer);
                                    UObject* kbBgImg = UObjectGlobals::StaticConstructObject(imgP2);
                                    if (kbBgImg)
                                    {
                                        umgSetBrushNoMatch(kbBgImg, texKeyBox2, setBrushFn);
                                        umgSetBrushSize(kbBgImg, 220.0f, 42.0f);
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = kbBgImg;
                                        modKbOl->ProcessEvent(addToKbFn, ap.data());
                                    }
                                }
                                if (addToKbFn)
                                {
                                    UObject* modKbLabel = makeTB(std::wstring(modifierName(s_modifierVK)), 1.0f, 1.0f, 1.0f, 1.0f, 24);
                                    m_cfgModBoxLabel = modKbLabel;
                                    if (modKbLabel)
                                    {
                                        auto* pCC = findParam(addToKbFn, STR("Content"));
                                        auto* pRV = findParam(addToKbFn, STR("ReturnValue"));
                                        int sz = addToKbFn->GetParmsSize();
                                        std::vector<uint8_t> ap(sz, 0);
                                        if (pCC) *reinterpret_cast<UObject**>(ap.data() + pCC->GetOffset_Internal()) = modKbLabel;
                                        modKbOl->ProcessEvent(addToKbFn, ap.data());
                                        UObject* modKbSlot = pRV ? *reinterpret_cast<UObject**>(ap.data() + pRV->GetOffset_Internal()) : nullptr;
                                        if (modKbSlot)
                                        {
                                            auto* setHA = modKbSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; modKbSlot->ProcessEvent(setHA, h.data()); }
                                            auto* setVA = modKbSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; modKbSlot->ProcessEvent(setVA, v.data()); }
                                        }
                                    }
                                }
                                if (addToRowFn)
                                {
                                    int sz = addToRowFn->GetParmsSize();
                                    std::vector<uint8_t> ap(sz, 0);
                                    if (rowC) *reinterpret_cast<UObject**>(ap.data() + rowC->GetOffset_Internal()) = modKbOl;
                                    modRow->ProcessEvent(addToRowFn, ap.data());
                                }
                            }

                            addToVBox(tab1VBox, modRow);
                        }
                    }

                    // Start hidden (ScrollBox hides, not inner VBox)
                    auto* visFn = scrollBox->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFn) { uint8_t p[8]{}; p[0] = 1; scrollBox->ProcessEvent(visFn, p); }
                }
            }

            // Tab 2: Hide Environment (in ScrollBox)
            {
                FStaticConstructObjectParameters sbP(scrollBoxClass, outer);
                UObject* scrollBox = UObjectGlobals::StaticConstructObject(sbP);
                m_cfgScrollBoxes[2] = scrollBox;

                FStaticConstructObjectParameters vP(vboxClass, outer);
                UObject* tab2VBox = UObjectGlobals::StaticConstructObject(vP);
                m_cfgTabContent[2] = tab2VBox;

                if (scrollBox && tab2VBox)
                {
                    configureScrollBox(scrollBox);
                    addToScrollBox(scrollBox, tab2VBox);
                    { UObject* slot = addToVBox(mainVBox, scrollBox); if (slot) umgSetSlotSize(slot, 1.0f, 1); /* Fill */ }
                    m_cfgRemovalVBox = tab2VBox;

                    int count = s_config.removalCount.load();
                    m_cfgRemovalHeader = addTB(tab2VBox, Loc::get("ui.saved_removals_prefix") + std::to_wstring(count) + Loc::get("ui.saved_removals_suffix"),
                                               0.78f, 0.86f, 1.0f, 1.0f, 32);
                    m_cfgLastRemovalCount = count;

                    // Populate removal entries with danger icons
                    rebuildRemovalList();

                    // Start hidden
                    auto* visFn = scrollBox->GetFunctionByNameInChain(STR("SetVisibility"));
                    if (visFn) { uint8_t p[8]{}; p[0] = 1; scrollBox->ProcessEvent(visFn, p); }
                }
            }

            // Add to viewport
            auto* addToViewportFn = userWidget->GetFunctionByNameInChain(STR("AddToViewport"));
            if (addToViewportFn)
            {
                auto* pZOrder = findParam(addToViewportFn, STR("ZOrder"));
                int sz = addToViewportFn->GetParmsSize();
                std::vector<uint8_t> vp(sz, 0);
                if (pZOrder) *reinterpret_cast<int32_t*>(vp.data() + pZOrder->GetOffset_Internal()) = 200;
                userWidget->ProcessEvent(addToViewportFn, vp.data());
            }

            // Get viewport size for uiScale
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

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
                    v[0] = 1400.0f; v[1] = 900.0f;
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
                    v[0] = 0.5f; v[1] = 0.5f; // centered
                    userWidget->ProcessEvent(setAlignFn, al.data());
                }
            }

            // Position: centered, slight Y offset
            {
                float posX = static_cast<float>(viewW) / 2.0f;
                float posY = static_cast<float>(viewH) / 2.0f - 100.0f;
                setWidgetPosition(userWidget, posX, posY, true);
            }

            // Start hidden
            auto* setVisFn = userWidget->GetFunctionByNameInChain(STR("SetVisibility"));
            if (setVisFn) { uint8_t p[8]{}; p[0] = 1; userWidget->ProcessEvent(setVisFn, p); }

            m_configWidget = userWidget;
            updateConfigFreeBuild();
            updateConfigNoCollision();
            VLOG(STR("[MoriaCppMod] [CFG] Config UMG widget created\n"));
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
            UObject* texToolbarSwap = nullptr;  // Swap-Bag_Icon Ã¢â‚¬â€ MC slot 4 (Toolbar Swap)
            UObject* texRemoveTarget = nullptr; // T_UI_Icon_GoodPlace2 Ã¢â‚¬â€ MC slot 8 (Remove Target)
            UObject* texUndoLast = nullptr;     // T_UI_Alert_BakedIcon Ã¢â‚¬â€ MC slot 9 (Undo Last)
            UObject* texRemoveAll = nullptr;    // T_UI_Icon_Filled_GoodPlace2 Ã¢â‚¬â€ MC slot 10 (Remove All)
            UObject* texSettings = nullptr;     // T_UI_Icon_Settings Ã¢â‚¬â€ MC slot 11 (Configuration)
            UObject* texStability = nullptr;     // HammerBreak_Icon — MC slot 2 (Stability Check)
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
                    else if (name == STR("Swap-Bag_Icon")) texToolbarSwap = t;
                    else if (name == STR("T_UI_Icon_GoodPlace2")) texRemoveTarget = t;
                    else if (name == STR("T_UI_Alert_BakedIcon")) texUndoLast = t;
                    else if (name == STR("T_UI_Icon_Filled_GoodPlace2")) texRemoveAll = t;
                    else if (name == STR("T_UI_Icon_Settings")) texSettings = t;
                    else if (name == STR("HammerBreak_Icon")) texStability = t;
                    else if (name == STR("T_UI_Eye_Open")) texHideChar = t;
                }
            }
            if (!texFrame || !texEmpty)
            {
                showOnScreen(L"MC: textures not found!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }
            if (!m_umgTexBlankRect && texBlankRect) m_umgTexBlankRect = texBlankRect; // cache if not yet cached

            // Fallback: try StaticFindObject for textures not found in FindAllOf scan
            // (some textures may not be loaded yet; StaticFindObject with full path can locate them)
            struct TexFallback { UObject*& ref; const TCHAR* path; const wchar_t* name; };
            TexFallback fallbacks[] = {
                {texToolbarSwap, STR("/Game/UI/textures/Interactables/Swap-Bag_Icon.Swap-Bag_Icon"), L"Swap-Bag_Icon"},
                {texRemoveTarget, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Icon_GoodPlace2.T_UI_Icon_GoodPlace2"), L"T_UI_Icon_GoodPlace2"},
                {texUndoLast, STR("/Game/UI/textures/_Shared/Icons/T_UI_Alert_BakedIcon.T_UI_Alert_BakedIcon"), L"T_UI_Alert_BakedIcon"},
                {texRemoveAll, STR("/Game/UI/textures/_Icons/Waypoints/FilledIcons/T_UI_Icon_Filled_GoodPlace2.T_UI_Icon_Filled_GoodPlace2"), L"T_UI_Icon_Filled_GoodPlace2"},
                {texSettings, STR("/Game/UI/textures/_Shared/Icons/T_UI_Icon_Settings.T_UI_Icon_Settings"), L"T_UI_Icon_Settings"},
                {texRotation, STR("/Game/UI/textures/_Shared/Icons/T_UI_Refresh.T_UI_Refresh"), L"T_UI_Refresh"},
                {texTarget, STR("/Game/UI/textures/_Icons/Menus/T_UI_Search.T_UI_Search"), L"T_UI_Search"},
                {texHideChar, STR("/Game/UI/textures/_Icons/Waypoints/T_UI_Eye_Open.T_UI_Eye_Open"), L"T_UI_Eye_Open"},
                {texStability, STR("/Game/UI/textures/CommunicationIcons/HammerBreak_Icon.HammerBreak_Icon"), L"HammerBreak_Icon"},
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
                showOnScreen(L"MC: missing widget classes!", 3.0f, 1.0f, 0.3f, 0.0f);
                return;
            }

            // --- Create UserWidget ---
            auto* pc = findPlayerController();
            if (!pc) { showOnScreen(L"MC: no PlayerController!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
            auto* createFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary:Create"));
            auto* wblClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.WidgetBlueprintLibrary"));
            if (!createFn || !wblClass) { showOnScreen(L"MC: WBL not found!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
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
            if (!userWidget) { showOnScreen(L"MC: CreateWidget null!", 3.0f, 1.0f, 0.3f, 0.0f); return; }

            // --- Get WidgetTree ---
            int wtOff = resolveOffset(userWidget, L"WidgetTree", s_off_widgetTree);
            UObject* widgetTree = (wtOff >= 0) ? *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(userWidget) + wtOff) : nullptr;
            UObject* outer = widgetTree ? widgetTree : userWidget;

            // --- Cache SetBrushFromTexture ---
            auto* setBrushFn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/UMG.Image:SetBrushFromTexture"));
            if (!setBrushFn) { showOnScreen(L"MC: SetBrushFromTexture missing!", 3.0f, 1.0f, 0.3f, 0.0f); return; }
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
                auto* addRowFn = rootVBox->GetFunctionByNameInChain(STR("AddChildToVerticalBox"));
                if (addRowFn)
                {
                    auto* pC = findParam(addRowFn, STR("Content"));
                    auto* pR = findParam(addRowFn, STR("ReturnValue"));
                    int sz = addRowFn->GetParmsSize();
                    std::vector<uint8_t> ap(sz, 0);
                    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = hbox;
                    rootVBox->ProcessEvent(addRowFn, ap.data());
                    UObject* rowSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                    if (rowSlot)
                    {
                        umgSetSlotSize(rowSlot, 1.0f, 0); // Auto
                    }
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
                        frameW = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        frameH = *reinterpret_cast<float*>(fBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        uint8_t* sBase = reinterpret_cast<uint8_t*>(stateImg);
                        stateW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        stateH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        VLOG(STR("[MoriaCppMod] [MC] Frame: {}x{}, State: {}x{}\n"),
                                                        frameW, frameH, stateW, stateH);
                        m_mcSlot0Overlay = overlay; // save for rotation label (added after loop)
                    }
                    if (i == 8) m_mcSlot8Overlay = overlay; // save for "Single" label
                    if (i == 10) m_mcSlot10Overlay = overlay; // save for "All" label

                    umgSetOpacity(stateImg, 1.0f);
                    umgSetOpacity(frameImg, 0.25f);

                    // Add state + icon to Overlay
                    auto* addToOverlayFn = overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                    if (addToOverlayFn)
                    {
                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));

                        // State image (bottom layer) Ã¢â‚¬â€ centered to preserve aspect
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
                        // Frame overlay (bottom) Ã¢â‚¬â€ wraps frameImg + keyBgImg + keyLabel
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
                                            m_mcKeyLabels[i] = keyLabel;
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
                            UObject* fSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                            if (fSlot)
                            {
                                umgSetSlotSize(fSlot, 1.0f, 0); // Auto
                                umgSetHAlign(fSlot, 2);
                                float overlapPx = stateH * 0.25f; // 25% vertical overlap (reduced 5%)
                                umgSetSlotPadding(fSlot, 0.0f, -overlapPx, 0.0f, 0.0f);
                            }
                        }
                    }

                    // Add VBox to HBox
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
                            umgSetSlotSize(hSlot, 1.0f, 1); // Fill
                            umgSetVAlign(hSlot, 0);          // VAlign_Fill
                            float colW2 = (frameW > stateW) ? frameW : stateW;
                            float hOverlap = colW2 * 0.10f; // 10% each side = 20% overlap (reduced from 40% for more spacing)
                            umgSetSlotPadding(hSlot, -hOverlap, 0.0f, -hOverlap, 0.0f);
                        }
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
                    texToolbarSwap, nullptr, nullptr, nullptr,               // row 1: ToolbarSwap, Empty5, Empty6, Empty7
                    texRemoveTarget, texUndoLast, texRemoveAll, texSettings  // row 2: RemoveTarget, UndoLast, RemoveAll, Config
                };
                const wchar_t* mcSlotNames[MC_SLOTS] = {
                    L"T_UI_Refresh", L"T_UI_Search", L"HammerBreak_Icon", L"T_UI_Eye_Open",
                    L"Swap-Bag_Icon", L"Empty5", L"Empty6", L"Empty7",
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
                        float texW = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                        float texH = *reinterpret_cast<float*>(iBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
                        if (texW > 0.0f && texH > 0.0f)
                        {
                            // Get state icon size as container bounds
                            float containerW = 64.0f, containerH = 64.0f;
                            if (m_mcStateImages[i])
                            {
                                uint8_t* sBase = reinterpret_cast<uint8_t*>(m_mcStateImages[i]);
                                containerW = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_X);
                                containerH = *reinterpret_cast<float*>(sBase + s_off_brush + BRUSH_IMAGE_SIZE_Y);
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
                auto* addToOverlayFn = m_mcSlot0Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* rotLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (rotLabel)
                    {
                        // Initial text: "5Ã‚Â°\nT0"
                        int step = s_overlay.rotationStep;
                        int total = s_overlay.totalRotation;
                        std::wstring txt = std::to_wstring(step) + L"\xB0\n" + L"T" + std::to_wstring(total);
                        umgSetText(rotLabel, txt);
                        umgSetTextColor(rotLabel, 0.4f, 0.6f, 1.0f, 1.0f); // medium blue

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = rotLabel;
                        m_mcSlot0Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* rotSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (rotSlot)
                        {
                            auto* setHA = rotSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; rotSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = rotSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; rotSlot->ProcessEvent(setVA, v.data()); }
                        }
                        m_mcRotationLabel = rotLabel;
                        VLOG(STR("[MoriaCppMod] [MC] Rotation label created on slot 0\n"));
                    }
                }
            }

            // --- Add "Single" text overlay on MC slot 8 (Remove Target) ---
            if (m_mcSlot8Overlay && textBlockClass)
            {
                auto* addToOverlayFn = m_mcSlot8Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* singleLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (singleLabel)
                    {
                        umgSetText(singleLabel, L"Single");
                        umgSetTextColor(singleLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                        umgSetFontSize(singleLabel, 31); // 10% larger

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = singleLabel;
                        m_mcSlot8Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* labelSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (labelSlot)
                        {
                            auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                        }
                        VLOG(STR("[MoriaCppMod] [MC] 'Single' label created on slot 8\n"));
                    }
                }
            }

            // --- Add "All" text overlay on MC slot 10 (Remove All) ---
            if (m_mcSlot10Overlay && textBlockClass)
            {
                auto* addToOverlayFn = m_mcSlot10Overlay->GetFunctionByNameInChain(STR("AddChildToOverlay"));
                if (addToOverlayFn)
                {
                    FStaticConstructObjectParameters tbP(textBlockClass, outer);
                    UObject* allLabel = UObjectGlobals::StaticConstructObject(tbP);
                    if (allLabel)
                    {
                        umgSetText(allLabel, L"All");
                        umgSetTextColor(allLabel, 0.85f, 0.05f, 0.05f, 1.0f); // bright deep red
                        umgSetFontSize(allLabel, 31); // 10% larger

                        auto* pC = findParam(addToOverlayFn, STR("Content"));
                        auto* pR = findParam(addToOverlayFn, STR("ReturnValue"));
                        int sz = addToOverlayFn->GetParmsSize();
                        std::vector<uint8_t> ap(sz, 0);
                        if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = allLabel;
                        m_mcSlot10Overlay->ProcessEvent(addToOverlayFn, ap.data());
                        UObject* labelSlot = pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
                        if (labelSlot)
                        {
                            auto* setHA = labelSlot->GetFunctionByNameInChain(STR("SetHorizontalAlignment"));
                            if (setHA) { int s2 = setHA->GetParmsSize(); std::vector<uint8_t> h(s2, 0); auto* p = findParam(setHA, STR("InHorizontalAlignment")); if (p) *reinterpret_cast<uint8_t*>(h.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setHA, h.data()); }
                            auto* setVA = labelSlot->GetFunctionByNameInChain(STR("SetVerticalAlignment"));
                            if (setVA) { int s2 = setVA->GetParmsSize(); std::vector<uint8_t> v(s2, 0); auto* p = findParam(setVA, STR("InVerticalAlignment")); if (p) *reinterpret_cast<uint8_t*>(v.data() + p->GetOffset_Internal()) = 2; labelSlot->ProcessEvent(setVA, v.data()); }
                        }
                        VLOG(STR("[MoriaCppMod] [MC] 'All' label created on slot 10\n"));
                    }
                }
            }

            // --- Size and position: lower-right of screen ---
            // Get viewport size for uiScale
            int32_t viewW = 1920, viewH = 1080;
            auto* pcVp = findPlayerController();
            if (pcVp)
            {
                auto* vpFunc = pcVp->GetFunctionByNameInChain(STR("GetViewportSize"));
                if (vpFunc)
                {
                    struct { int32_t SizeX{0}, SizeY{0}; } vpParams{};
                    pcVp->ProcessEvent(vpFunc, &vpParams);
                    if (vpParams.SizeX > 0) viewW = vpParams.SizeX;
                    if (vpParams.SizeY > 0) viewH = vpParams.SizeY;
                }
            }
            float uiScale = static_cast<float>(viewH) / 2160.0f;
            if (uiScale < 0.5f) uiScale = 0.5f; // minimum scale for readability at sub-1080p

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
            float mcTotalH = (2.0f * mcSlotH) * mcScale;           // 2 rows

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
                float setPosX = fracX * static_cast<float>(viewW);
                float setPosY = fracY * static_cast<float>(viewH);
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
            m_toolbarSizeW[2] = mcTotalW / static_cast<float>(viewW);
            m_toolbarSizeH[2] = mcTotalH / static_cast<float>(viewH);
            m_mcBarWidget = userWidget;
            showOnScreen(Loc::get("msg.mod_controller_created").c_str(), 3.0f, 0.0f, 1.0f, 0.0f);
            VLOG(STR("[MoriaCppMod] [MC] === Mod Controller bar creation complete ({}x{}) ===\n"),
                                            mcTotalW, mcTotalH);
        }

