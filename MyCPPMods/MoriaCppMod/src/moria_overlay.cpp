// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  moria_overlay.cpp — Win32 GDI+ overlay rendering thread                  ║
// ║                                                                           ║
// ║  Runs entirely on the overlay thread. Must NOT call UE4SS APIs.           ║
// ║  Accesses only s_overlay (thread-safe via slotCS) and s_bindings          ║
// ║  (read-only from this thread).                                            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "moria_common.h"
#include "moria_keybinds.h"

namespace MoriaMods
{
    // ════════════════════════════════════════════════════════════════════════════
    // Overlay Rendering
    // ════════════════════════════════════════════════════════════════════════════

    // Renders the 12-slot hotbar overlay. Called from overlay thread on WM_TIMER.
    // Layout: [F1..F8] | [F9..F12] — build recipes with icons | utility slots
    // Scales relative to 1080p baseline. Uses UpdateLayeredWindow for per-pixel alpha.
    static void renderOverlay(HWND hwnd)
    {
        if (!s_overlay.gameHwnd || !IsWindow(s_overlay.gameHwnd))
        {
            s_overlay.gameHwnd = findGameWindow();
            if (!s_overlay.gameHwnd) return;
        }

        RECT clientRect;
        GetClientRect(s_overlay.gameHwnd, &clientRect);
        POINT origin = {0, 0};
        ClientToScreen(s_overlay.gameHwnd, &origin);
        int gameW = clientRect.right;
        int gameH = clientRect.bottom;
        if (gameW < 100 || gameH < 100) return;

        float scale = gameH / 1080.0f;
        if (scale < 0.5f) scale = 0.5f;

        // Each slot: icon box (slotSize x slotSize) with gap between them
        int slotSize = static_cast<int>(48 * scale);
        int gap = static_cast<int>(4 * scale);
        int padding = static_cast<int>(6 * scale);
        int labelH = static_cast<int>(14 * scale);    // height for F-key label below icon
        int separatorW = static_cast<int>(8 * scale); // white vertical bar between F8 and F9
        // Width = padding + 8 build slots + separator + 4 extra slots + padding
        int overlayW = padding * 2 + OVERLAY_SLOTS * slotSize + (OVERLAY_SLOTS - 1) * gap + separatorW;
        int overlayH = padding * 2 + slotSize + labelH;
        int overlayX = origin.x + (gameW - overlayW) / 2;
        int overlayY = origin.y + static_cast<int>(4 * scale);

        if (!s_overlay.visible)
        {
            ShowWindow(hwnd, SW_HIDE);
            return;
        }
        if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        // Create 32-bit ARGB bitmap
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = overlayW;
        bmi.bmiHeader.biHeight = -overlayH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp)
        {
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

        // Thread-safe slot copy
        OverlaySlot localSlots[OVERLAY_SLOTS]{};
        if (s_overlay.csInit)
        {
            CriticalSectionLock slotLock(s_overlay.slotCS);
            for (int i = 0; i < OVERLAY_SLOTS; i++)
                localSlots[i] = s_overlay.slots[i];
        }

        {
            Gdiplus::Graphics gfx(memDC);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // Overall background
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(100, 5, 8, 18));
            int radius = static_cast<int>(6 * scale);
            Gdiplus::GraphicsPath bgPath;
            bgPath.AddArc(0, 0, radius * 2, radius * 2, 180, 90);
            bgPath.AddArc(overlayW - radius * 2 - 1, 0, radius * 2, radius * 2, 270, 90);
            bgPath.AddArc(overlayW - radius * 2 - 1, overlayH - radius * 2 - 1, radius * 2, radius * 2, 0, 90);
            bgPath.AddArc(0, overlayH - radius * 2 - 1, radius * 2, radius * 2, 90, 90);
            bgPath.CloseFigure();
            gfx.FillPath(&bgBrush, &bgPath);

            // Brushes and fonts
            Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(51, 235, 235, 230)); // off-white 20% opacity
            Gdiplus::SolidBrush usedBrush(Gdiplus::Color(51, 245, 245, 240));  // off-white 20% opacity (used)
            Gdiplus::Pen slotBorder(Gdiplus::Color(180, 100, 160, 230), 3.0f); // light blue outline 2x thick
            Gdiplus::Pen usedBorder(Gdiplus::Color(220, 120, 180, 255), 3.0f); // light blue outline 2x thick (used)
            float labelFontSz = 10.0f * scale;
            if (labelFontSz < 9.0f) labelFontSz = 9.0f;
            Gdiplus::FontFamily fontFamily(L"Consolas");
            Gdiplus::Font labelFont(&fontFamily, labelFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            float nameFontSz = 9.0f * scale;
            if (nameFontSz < 8.0f) nameFontSz = 8.0f;
            Gdiplus::Font nameFont(&fontFamily, nameFontSz, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush labelBrush(Gdiplus::Color(200, 140, 170, 210));
            Gdiplus::SolidBrush nameBrush(Gdiplus::Color(180, 180, 200, 230));
            Gdiplus::SolidBrush letterBrush(Gdiplus::Color(140, 100, 140, 200));
            Gdiplus::Font letterFont(&fontFamily, slotSize * 0.45f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat centerFmt;
            centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            // Vertical separator pen (white, thin)
            Gdiplus::Pen separatorPen(Gdiplus::Color(180, 200, 210, 230), 1.0f);

            for (int i = 0; i < OVERLAY_SLOTS; i++)
            {
                // X offset: after F8 (index 8+), add separator width
                int sx = padding + i * (slotSize + gap) + (i >= OVERLAY_BUILD_SLOTS ? separatorW : 0);
                int sy = padding;

                // Draw vertical separator before F9
                if (i == OVERLAY_BUILD_SLOTS)
                {
                    int sepX = padding + OVERLAY_BUILD_SLOTS * (slotSize + gap) - gap / 2 + separatorW / 2;
                    gfx.DrawLine(&separatorPen, sepX, sy + 2, sepX, sy + slotSize - 2);
                }

                // Slot background — F9-F12 always draw as empty
                Gdiplus::Rect slotRect(sx, sy, slotSize, slotSize);
                if (i < OVERLAY_BUILD_SLOTS && localSlots[i].used)
                {
                    gfx.FillRectangle(&usedBrush, slotRect);
                    gfx.DrawRectangle(&usedBorder, slotRect);
                }
                else
                {
                    gfx.FillRectangle(&emptyBrush, slotRect);
                    gfx.DrawRectangle(&slotBorder, slotRect);
                }

                // Icon or letter placeholder (F1-F8 only)
                if (i < OVERLAY_BUILD_SLOTS)
                {
                    if (localSlots[i].used && localSlots[i].icon)
                    {
                        int iconPad = static_cast<int>(3 * scale);
                        gfx.DrawImage(localSlots[i].icon.get(), Gdiplus::Rect(sx + iconPad, sy + iconPad, slotSize - iconPad * 2, slotSize - iconPad * 2));
                    }
                    else if (localSlots[i].used && !localSlots[i].displayName.empty())
                    {
                        wchar_t letter[2] = {localSlots[i].displayName[0], 0};
                        Gdiplus::RectF letterRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                        gfx.DrawString(letter, 1, &letterFont, letterRect, &centerFmt, &letterBrush);
                    }
                }

                // Slot 8 (Target): archery target icon + TGT text
                if (i == 8)
                {
                    float bcx = (float)(sx + slotSize / 2);
                    float bcy = (float)(sy + slotSize / 2);
                    // Concentric rings: white, black, blue, red, gold (outside-in)
                    float r5 = slotSize * 0.42f; // outermost white
                    float r4 = slotSize * 0.35f; // black
                    float r3 = slotSize * 0.28f; // blue
                    float r2 = slotSize * 0.20f; // red
                    float r1 = slotSize * 0.12f; // gold center
                    Gdiplus::SolidBrush bWhite(Gdiplus::Color(160, 240, 240, 235));
                    Gdiplus::SolidBrush bBlack(Gdiplus::Color(160, 40, 40, 40));
                    Gdiplus::SolidBrush bBlue(Gdiplus::Color(160, 50, 120, 200));
                    Gdiplus::SolidBrush bRed(Gdiplus::Color(160, 210, 50, 40));
                    Gdiplus::SolidBrush bGold(Gdiplus::Color(160, 240, 200, 50));
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    gfx.FillEllipse(&bWhite, bcx - r5, bcy - r5, r5 * 2, r5 * 2);
                    gfx.FillEllipse(&bBlack, bcx - r4, bcy - r4, r4 * 2, r4 * 2);
                    gfx.FillEllipse(&bBlue, bcx - r3, bcy - r3, r3 * 2, r3 * 2);
                    gfx.FillEllipse(&bRed, bcx - r2, bcy - r2, r2 * 2, r2 * 2);
                    gfx.FillEllipse(&bGold, bcx - r1, bcy - r1, r1 * 2, r1 * 2);
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeDefault);

                    float tgtFontSz = slotSize * 0.28f;
                    Gdiplus::Font tgtFont(&fontFamily, tgtFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush tgtBrush(Gdiplus::Color(240, 10, 15, 70));
                    Gdiplus::RectF tgtRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(Loc::get("ovr.target").c_str(), -1, &tgtFont, tgtRect, &centerFmt, &tgtBrush);
                }

                // Slot 9 (Rotation): step degrees (top, bold) | separator line | T+total (bottom)
                if (i == 9)
                {
                    int stepVal = s_overlay.rotationStep;
                    int totalVal = s_overlay.totalRotation;

                    // Top line: step value with degree symbol (bold)
                    std::wstring stepStr = std::to_wstring(stepVal) + Loc::get("ovr.degree");
                    float stepFontSz = slotSize * 0.28f;
                    Gdiplus::Font stepFont(&fontFamily, stepFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush stepBrush(Gdiplus::Color(220, 180, 210, 255));
                    Gdiplus::StringFormat topFmt;
                    topFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    topFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF topRect((float)sx, (float)sy + slotSize * 0.02f, (float)slotSize, (float)slotSize * 0.45f);
                    gfx.DrawString(stepStr.c_str(), -1, &stepFont, topRect, &topFmt, &stepBrush);

                    // Horizontal separator line
                    float lineY = (float)sy + slotSize * 0.48f;
                    float lineMargin = slotSize * 0.15f;
                    Gdiplus::Pen linePen(Gdiplus::Color(120, 180, 180, 200), 1.0f);
                    gfx.DrawLine(&linePen, (float)sx + lineMargin, lineY, (float)sx + slotSize - lineMargin, lineY);

                    // Bottom line: T+total (no degree symbol)
                    std::wstring totalStr = L"T" + std::to_wstring(totalVal);
                    float totalFontSz = slotSize * 0.28f;
                    Gdiplus::Font totalFont(&fontFamily, totalFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush totalBrush(Gdiplus::Color(255, 200, 230, 255));
                    Gdiplus::StringFormat botFmt;
                    botFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    botFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF botRect((float)sx, (float)sy + slotSize * 0.50f, (float)slotSize, (float)slotSize * 0.48f);
                    gfx.DrawString(totalStr.c_str(), -1, &totalFont, botRect, &botFmt, &totalBrush);
                }

                // Slot 10 (Toolbar Swap): show active toolbar number
                if (i == 10)
                {
                    int tb = s_overlay.activeToolbar;
                    std::wstring tbStr = L"T" + std::to_wstring(tb + 1);
                    float tbFontSz = slotSize * 0.35f;
                    Gdiplus::Font tbFont(&fontFamily, tbFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush tbBrush(Gdiplus::Color(220, 180, 210, 255));
                    Gdiplus::RectF tbRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(tbStr.c_str(), -1, &tbFont, tbRect, &centerFmt, &tbBrush);
                }

                // Slot 11 (Configuration): gear/cog icon + CFG text
                if (i == 11)
                {
                    float gcx = (float)(sx + slotSize / 2);
                    float gcy = (float)(sy + slotSize / 2);
                    constexpr int nTeeth = 8;
                    constexpr float kPI = 3.14159265f;
                    float tipR = slotSize * 0.40f;  // outer radius (tooth tips)
                    float rootR = slotSize * 0.28f; // inner radius (between teeth)
                    float holeR = slotSize * 0.10f; // center hole

                    // Build gear profile: each tooth = flat top, each gap = flat valley
                    float segAngle = 2.0f * kPI / nTeeth;
                    float halfTip = segAngle * 0.22f;  // half angular width of tooth top
                    float halfRoot = segAngle * 0.28f; // half angular width of root valley

                    std::vector<Gdiplus::PointF> gearPts;
                    for (int t = 0; t < nTeeth; t++)
                    {
                        float ctrAngle = t * segAngle - kPI / 2.0f;
                        float gapCtr = ctrAngle + segAngle * 0.5f;
                        // Flat tooth top (outer)
                        gearPts.push_back({gcx + tipR * cosf(ctrAngle - halfTip), gcy + tipR * sinf(ctrAngle - halfTip)});
                        gearPts.push_back({gcx + tipR * cosf(ctrAngle + halfTip), gcy + tipR * sinf(ctrAngle + halfTip)});
                        // Flat root valley (inner)
                        gearPts.push_back({gcx + rootR * cosf(gapCtr - halfRoot), gcy + rootR * sinf(gapCtr - halfRoot)});
                        gearPts.push_back({gcx + rootR * cosf(gapCtr + halfRoot), gcy + rootR * sinf(gapCtr + halfRoot)});
                    }

                    Gdiplus::GraphicsPath gearPath;
                    gearPath.AddPolygon(gearPts.data(), (int)gearPts.size());

                    Gdiplus::SolidBrush gearBrush(Gdiplus::Color(150, 150, 165, 185));
                    Gdiplus::Pen gearOutline(Gdiplus::Color(140, 100, 115, 140), 1.2f * (slotSize / 48.0f));
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    gfx.FillPath(&gearBrush, &gearPath);
                    gfx.DrawPath(&gearOutline, &gearPath);
                    // Center hole
                    Gdiplus::SolidBrush holeBrush(Gdiplus::Color(150, 25, 30, 45));
                    gfx.FillEllipse(&holeBrush, gcx - holeR, gcy - holeR, holeR * 2, holeR * 2);
                    Gdiplus::Pen holeRing(Gdiplus::Color(140, 100, 115, 140), 1.2f * (slotSize / 48.0f));
                    gfx.DrawEllipse(&holeRing, gcx - holeR, gcy - holeR, holeR * 2, holeR * 2);
                    gfx.SetSmoothingMode(Gdiplus::SmoothingModeDefault);

                    float cfgFontSz = slotSize * 0.28f;
                    Gdiplus::Font cfgFont(&fontFamily, cfgFontSz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush cfgBrush(Gdiplus::Color(240, 10, 15, 70));
                    Gdiplus::RectF cfgRect((float)sx, (float)sy, (float)slotSize, (float)slotSize);
                    gfx.DrawString(Loc::get("ovr.config").c_str(), -1, &cfgFont, cfgRect, &centerFmt, &cfgBrush);
                }

                // Key label below slot — pulled from s_bindings via named constants
                // Slot mapping: 0-7=QB bindings, 8=Target, 9=Rotation, 10=Swap, 11=Config
                std::wstring fLabel;
                if (i <= 7)
                {
                    fLabel = keyName(s_bindings[i].key); // Quick Build 1-8
                }
                else if (i == 8)
                {
                    fLabel = keyName(s_bindings[BIND_TARGET].key); // Target
                }
                else if (i == 9)
                {
                    fLabel = keyName(s_bindings[BIND_ROTATION].key); // Rotation
                }
                else if (i == 10)
                {
                    fLabel = keyName(s_bindings[BIND_SWAP].key); // Toolbar Swap
                }
                else if (i == 11)
                {
                    fLabel = keyName(s_bindings[BIND_CONFIG].key); // Configuration
                }
                Gdiplus::RectF labelRect((float)sx, (float)(sy + slotSize + 1), (float)slotSize, (float)labelH);
                gfx.DrawString(fLabel.c_str(), -1, &labelFont, labelRect, &centerFmt, &labelBrush);
            }
        }

        POINT ptSrc = {0, 0};
        SIZE sz = {overlayW, overlayH};
        POINT ptDst = {overlayX, overlayY};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    // ════════════════════════════════════════════════════════════════════════════
    // Window Procedure & Thread Entry
    // ════════════════════════════════════════════════════════════════════════════

    static LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_TIMER:
            renderOverlay(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    DWORD WINAPI overlayThreadProc(LPVOID /*param*/)
    {
        // GDI+ already initialized in startOverlay() — ensure it's ready
        if (!s_overlay.gdipToken)
        {
            Gdiplus::GdiplusStartupInput gdipInput;
            Gdiplus::GdiplusStartup(&s_overlay.gdipToken, &gdipInput, nullptr);
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlayWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MoriaCppModOverlay";
        UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
        if (!RegisterClassExW(&wc))
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            s_overlay.gdipToken = 0;
            return 1;
        }

        for (int i = 0; i < 60 && s_overlay.running; i++)
        {
            s_overlay.gameHwnd = findGameWindow();
            if (s_overlay.gameHwnd) break;
            Sleep(500);
        }
        if (!s_overlay.running || !s_overlay.gameHwnd)
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            s_overlay.gdipToken = 0;
            UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
            return 0;
        }

        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                    L"MoriaCppModOverlay",
                                    L"",
                                    WS_POPUP,
                                    0,
                                    0,
                                    1,
                                    1,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandle(nullptr),
                                    nullptr);
        if (!hwnd)
        {
            Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
            s_overlay.gdipToken = 0;
            UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
            return 1;
        }

        s_overlay.overlayHwnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        renderOverlay(hwnd);

        SetTimer(hwnd, 1, 200, nullptr); // 5Hz refresh

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            if (!s_overlay.running) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        s_overlay.overlayHwnd = nullptr;
        Gdiplus::GdiplusShutdown(s_overlay.gdipToken);
        s_overlay.gdipToken = 0;
        UnregisterClassW(L"MoriaCppModOverlay", GetModuleHandle(nullptr));
        return 0;
    }

} // namespace MoriaMods
