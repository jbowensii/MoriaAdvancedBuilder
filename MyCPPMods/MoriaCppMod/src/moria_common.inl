// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_common.inl — Centralized coordinate conversion (ScreenCoords)      ║
// ║  Included inside the mod class body, before other .inl files.             ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// ════════════════════════════════════════════════════════════════════════════════
// ScreenCoords — single source of truth for viewport, cursor, and scale state
// ════════════════════════════════════════════════════════════════════════════════
//
// Coordinate spaces used in this mod:
//   Viewport Fraction (0.0–1.0) — resolution-independent, used for toolbar positions/sizes
//   Raw Pixels                  — Win32 client rect and UE4 GetViewportSize()
//   Slate Units                 — UMG logical coordinates (raw pixels / viewportScale)
//
// Usage:
//   m_screen.refresh(findPlayerController());  // caches viewW/viewH/viewportScale/slateW/slateH
//   float fx, fy;
//   if (m_screen.getCursorFraction(fx, fy)) { ... }
//   setWidgetPosition(w, m_screen.fracToPixelX(fx), m_screen.fracToPixelY(fy), true);

struct ScreenCoords
{
    // ── Cached State ──
    int32_t viewW{1920};            // raw pixel viewport width  (from GetViewportSize)
    int32_t viewH{1080};            // raw pixel viewport height (from GetViewportSize)
    float   viewportScale{1.0f};    // raw pixels per slate unit (from GetViewportScale)
    float   slateW{1920.0f};        // viewW / viewportScale — viewport width in slate units
    float   slateH{1080.0f};        // viewH / viewportScale — viewport height in slate units
    float   aspectRatio{16.0f/9.0f};// viewW / viewH
    float   uiScale{1.0f};          // viewH / 2160.0f, min 0.5 (base 4K design scale)

    // ── Query UE4 viewport scale via reflection ──
    static float queryViewportScale(UObject* worldContext)
    {
        static UFunction* s_fn  = nullptr;
        static UObject*   s_cdo = nullptr;
        if (!s_fn)
        {
            s_fn  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                      STR("/Script/UMG.WidgetLayoutLibrary:GetViewportScale"));
            s_cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                      STR("/Script/UMG.Default__WidgetLayoutLibrary"));
        }
        if (!s_fn || !s_cdo || !worldContext) return 1.0f;
        struct { UObject* WCO{nullptr}; float RV{1.0f}; } p{worldContext};
        s_cdo->ProcessEvent(s_fn, &p);
        return (p.RV > 0.1f) ? p.RV : 1.0f;
    }

    // ── Refresh ──
    // Call once per widget creation or per-frame in tick.
    // Queries GetViewportSize + GetViewportScale, derives all cached fields.
    // Returns false if PlayerController is null or GetViewportSize fails.
    bool refresh(UObject* playerController)
    {
        if (!playerController) return false;
        auto* fn = playerController->GetFunctionByNameInChain(STR("GetViewportSize"));
        if (!fn) return false;
        struct { int32_t SizeX{0}, SizeY{0}; } params{};
        playerController->ProcessEvent(fn, &params);
        if (params.SizeX > 0) viewW = params.SizeX;
        if (params.SizeY > 0) viewH = params.SizeY;

        viewportScale = queryViewportScale(playerController);
        slateW = static_cast<float>(viewW) / viewportScale;
        slateH = static_cast<float>(viewH) / viewportScale;
        aspectRatio = (viewH > 0) ? static_cast<float>(viewW) / static_cast<float>(viewH)
                                  : 16.0f / 9.0f;
        uiScale = static_cast<float>(viewH) / 2160.0f;
        if (uiScale < 0.5f) uiScale = 0.5f;
        return true;
    }

    // ── Cursor: Win32 → Viewport Fraction ──
    bool getCursorFraction(float& outFracX, float& outFracY) const
    {
        HWND gw = findGameWindow();
        if (!gw) return false;
        POINT cur; GetCursorPos(&cur); ScreenToClient(gw, &cur);
        RECT cr; GetClientRect(gw, &cr);
        if (cr.right <= 0 || cr.bottom <= 0) return false;
        outFracX = static_cast<float>(cur.x) / static_cast<float>(cr.right);
        outFracY = static_cast<float>(cur.y) / static_cast<float>(cr.bottom);
        return true;
    }

    // ── Cursor: Win32 → Raw Client Pixels ──
    bool getCursorClientPixels(int& outX, int& outY,
                               int& outClientW, int& outClientH) const
    {
        HWND gw = findGameWindow();
        if (!gw) return false;
        POINT cur; GetCursorPos(&cur); ScreenToClient(gw, &cur);
        RECT cr; GetClientRect(gw, &cr);
        if (cr.right <= 0 || cr.bottom <= 0) return false;
        outX = cur.x; outY = cur.y;
        outClientW = cr.right; outClientH = cr.bottom;
        return true;
    }

    // ── Cursor: Win32 → Slate Units ──
    bool getCursorSlate(float& outX, float& outY) const
    {
        int cx, cy, cw, ch;
        if (!getCursorClientPixels(cx, cy, cw, ch)) return false;
        outX = static_cast<float>(cx) / viewportScale;
        outY = static_cast<float>(cy) / viewportScale;
        return true;
    }

    // ── Conversions: Fraction ↔ Pixel ──
    float fracToPixelX(float frac) const { return frac * static_cast<float>(viewW); }
    float fracToPixelY(float frac) const { return frac * static_cast<float>(viewH); }
    float pixelToFracX(float px)   const { return (viewW > 0) ? px / static_cast<float>(viewW) : 0.0f; }
    float pixelToFracY(float py)   const { return (viewH > 0) ? py / static_cast<float>(viewH) : 0.0f; }

    // ── Conversions: Pixel ↔ Slate ──
    float pixelToSlateX(float px)  const { return px / viewportScale; }
    float pixelToSlateY(float py)  const { return py / viewportScale; }
    float slateToPixelX(float sx)  const { return sx * viewportScale; }
    float slateToPixelY(float sy)  const { return sy * viewportScale; }

    // ── Conversions: Fraction ↔ Slate ──
    float fracToSlateX(float frac) const { return frac * slateW; }
    float fracToSlateY(float frac) const { return frac * slateH; }
    float slateToFracX(float sx)   const { return (slateW > 0.0f) ? sx / slateW : 0.0f; }
    float slateToFracY(float sy)   const { return (slateH > 0.0f) ? sy / slateH : 0.0f; }
};

// ════════════════════════════════════════════════════════════════════════════════
// Shared Utility Functions
// ════════════════════════════════════════════════════════════════════════════════

// ── Object Introspection ────────────────────────────────────────────────────

// Safe class name extraction — returns L"" if obj or class is null
static std::wstring safeClassName(UObject* obj)
{
    if (!obj) return L"";
    auto* cls = obj->GetClassPrivate();
    if (!cls) return L"";
    return std::wstring(cls->GetName());
}

// GC-safe liveness check — not null, not pending destruction, not unreachable
static bool isWidgetAlive(UObject* obj)
{
    if (!obj) return false;
    if (obj->HasAnyFlags(static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed))) return false;
    if (obj->IsUnreachable()) return false;
    return true;
}

// ── Player & World Lookup ───────────────────────────────────────────────────

UObject* findPlayerController()
{
    std::vector<UObject*> pcs;
    UObjectGlobals::FindAllOf(STR("PlayerController"), pcs);
    return pcs.empty() ? nullptr : pcs[0];
}

UObject* getPawn()
{
    auto* pc = findPlayerController();
    if (!pc) return nullptr;
    auto* fn = pc->GetFunctionByNameInChain(STR("K2_GetPawn"));
    if (!fn) return nullptr;
    struct
    {
        UObject* Ret{nullptr};
    } p{};
    pc->ProcessEvent(fn, &p);
    return p.Ret;
}

FVec3f getPawnLocation()
{
    FVec3f loc{0, 0, 0};
    auto* pawn = getPawn();
    if (!pawn) return loc;
    auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
    if (!fn) return loc;
    pawn->ProcessEvent(fn, &loc);
    return loc;
}

// ── Widget Visibility ───────────────────────────────────────────────────────

// Set UMG widget visibility via ProcessEvent. vis: 0=Visible, 1=Collapsed, 2=Hidden
void setWidgetVisibility(UObject* widget, uint8_t vis)
{
    if (!widget) return;
    auto* fn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
    if (!fn) return;
    uint8_t parms[8]{};
    parms[0] = vis;
    widget->ProcessEvent(fn, parms);
}

// ── Panel Child Management ──────────────────────────────────────────────────

// Generic AddChild via named UFunction (AddChildToVerticalBox, etc.)
// Returns the slot UObject* for further configuration, or nullptr on failure.
UObject* addChildToPanel(UObject* parent, const wchar_t* fnName, UObject* child)
{
    if (!parent || !child) return nullptr;
    auto* fn = parent->GetFunctionByNameInChain(fnName);
    if (!fn) return nullptr;
    auto* pC = findParam(fn, STR("Content"));
    auto* pR = findParam(fn, STR("ReturnValue"));
    int sz = fn->GetParmsSize();
    std::vector<uint8_t> ap(sz, 0);
    if (pC) *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = child;
    parent->ProcessEvent(fn, ap.data());
    return pR ? *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal()) : nullptr;
}

UObject* addToVBox(UObject* parent, UObject* child)
{
    return addChildToPanel(parent, STR("AddChildToVerticalBox"), child);
}

UObject* addToHBox(UObject* parent, UObject* child)
{
    return addChildToPanel(parent, STR("AddChildToHorizontalBox"), child);
}

UObject* addToOverlay(UObject* parent, UObject* child)
{
    return addChildToPanel(parent, STR("AddChildToOverlay"), child);
}

// ── Widget Discovery ────────────────────────────────────────────────────────

// Find first UserWidget matching className. If requireVisible, skip hidden ones.
UObject* findWidgetByClass(const wchar_t* className, bool requireVisible = false)
{
    std::vector<UObject*> widgets;
    UObjectGlobals::FindAllOf(STR("UserWidget"), widgets);
    QBLOG(STR("[MoriaCppMod] [QB] findWidgetByClass('{}') requireVisible={} total={}\n"),
          className, requireVisible, widgets.size());
    for (auto* w : widgets)
    {
        if (!w || !isWidgetAlive(w)) continue;
        std::wstring cls = safeClassName(w);
        if (cls.empty()) continue;
        if (cls == className)
        {
            if (!requireVisible) return w;
            auto* visFunc = w->GetFunctionByNameInChain(STR("IsVisible"));
            if (visFunc)
            {
                struct
                {
                    bool Ret{false};
                } vp{};
                w->ProcessEvent(visFunc, &vp);
                if (vp.Ret) return w;
            }
        }
    }
    QBLOG(STR("[MoriaCppMod] [QB] findWidgetByClass('{}') -> NOT FOUND\n"), className);
    return nullptr;
}
