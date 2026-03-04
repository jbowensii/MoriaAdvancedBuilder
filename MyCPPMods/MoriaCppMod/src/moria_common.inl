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
//   Slate Units                 — UMG logical coordinates (raw pixels / dpiScale)
//
// Usage:
//   m_screen.refresh(findPlayerController());  // call once, caches viewW/viewH/uiScale
//   float fx, fy;
//   if (m_screen.getCursorFraction(fx, fy)) { ... }
//   setWidgetPosition(w, m_screen.fracToPixelX(fx), m_screen.fracToPixelY(fy), true);

struct ScreenCoords
{
    // ── Cached State ──
    int32_t viewW{1920};    // raw pixel viewport width  (from GetViewportSize)
    int32_t viewH{1080};    // raw pixel viewport height (from GetViewportSize)
    float   dpiScale{1.0f}; // rawPixels / slateUnits (computed during DPI probe)
    float   uiScale{1.0f};  // viewH / 2160.0f, min 0.5 (base 4K design scale)

    // ── Refresh ──
    // Call once per widget creation or per-frame in tick.
    // Queries PlayerController::GetViewportSize(), updates viewW/viewH/uiScale.
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
        uiScale = static_cast<float>(viewH) / 2160.0f;
        if (uiScale < 0.5f) uiScale = 0.5f;
        return true;
    }

    // ── Cursor: Win32 → Viewport Fraction ──
    // Returns cursor position as fraction of game window (0.0–1.0).
    // Returns false if game window not found or client rect is degenerate.
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
    // Returns cursor position and client dimensions in raw pixels.
    // Used by config menu hit-test (needs pixel-level precision).
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

    // ── Conversions ──
    float fracToPixelX(float frac) const { return frac * static_cast<float>(viewW); }
    float fracToPixelY(float frac) const { return frac * static_cast<float>(viewH); }
    float pixelToFracX(float px)   const { return (viewW > 0) ? px / static_cast<float>(viewW) : 0.0f; }
    float pixelToFracY(float py)   const { return (viewH > 0) ? py / static_cast<float>(viewH) : 0.0f; }
    float pixelToSlateX(float px)  const { return px / dpiScale; }
    float pixelToSlateY(float py)  const { return py / dpiScale; }
};
