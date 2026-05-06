


// isGameWindowFocused / focusedAsyncKeyState / the
// `#define GetAsyncKeyState focusedAsyncKeyState` macro all moved to
// moria_common.h (free-function namespace scope) so every TU that
// includes the header sees the override - not just dllmain.cpp via
// the .inl-at-class-scope inclusion path. See the comment block in
// moria_common.h for the bug history.

struct ScreenCoords
{

    int32_t viewW{1920};
    int32_t viewH{1080};
    float   viewportScale{1.0f};
    float   slateW{1920.0f};
    float   slateH{1080.0f};
    float   aspectRatio{16.0f/9.0f};
    float   uiScale{1.0f};


    static float queryViewportScale(UObject* worldContext)
    {
        if (!worldContext) return 1.0f;
        auto* fn  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                      STR("/Script/UMG.WidgetLayoutLibrary:GetViewportScale"));
        auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                      STR("/Script/UMG.Default__WidgetLayoutLibrary"));
        if (!fn || !cdo) return 1.0f;
        struct { UObject* WCO{nullptr}; float RV{1.0f}; } p{worldContext};
        safeProcessEvent(cdo, fn, &p);
        return (p.RV > 0.1f) ? p.RV : 1.0f;
    }


    bool refresh(UObject* playerController)
    {
        if (!playerController) return false;
        auto* fn = playerController->GetFunctionByNameInChain(STR("GetViewportSize"));
        if (!fn) return false;
        struct { int32_t SizeX{0}, SizeY{0}; } params{};
        safeProcessEvent(playerController, fn, &params);
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

    float fracToPixelX(float frac) const { return frac * static_cast<float>(viewW); }
    float fracToPixelY(float frac) const { return frac * static_cast<float>(viewH); }

    float pixelToSlateX(float px)  const { return px / viewportScale; }
    float pixelToSlateY(float py)  const { return py / viewportScale; }

    float slateToFracX(float sx)   const { return (slateW > 0.0f) ? sx / slateW : 0.0f; }
    float slateToFracY(float sy)   const { return (slateH > 0.0f) ? sy / slateH : 0.0f; }
};


// SEH-wrapped helper. `safeClassName` is called from EVERY
// ProcessEvent pre-hook in the mod, which fires once per UFunction call
// in the entire game. Some of those calls happen on objects mid-
// destruction whose vtables are garbage; the virtual dispatches through
// `obj->GetClassPrivate()` and `cls->GetName()` access-violate without
// SEH protection. Cherry-picked from v6.11.0 (single highest-leverage
// stability fix per the 2026-05-03 audit).
//
// Implementation note: SEH (`__try`) is incompatible with object
// unwinding (`std::wstring` destructors), so we split the work:
//   1. `seh_classNameToBuf` — `noexcept`, no C++ locals, just a fixed
//      wchar_t buffer. Calls the dispatch helper inside __try.
//   2. `dispatchClassNameToBuf` — does the virtual dispatch and copies
//      into the buffer. Has a local `std::wstring` from `GetName()`
//      which needs unwinding — that's fine here, the SEH is in caller.
//   3. `safeClassName` — turns the buffer into a `std::wstring` only
//      AFTER SEH protection is no longer needed.
static void dispatchClassNameToBuf(UObject* obj, wchar_t* buf, size_t bufLen)
{
    if (!obj || bufLen == 0) { if (buf && bufLen) buf[0] = L'\0'; return; }
    UClass* cls = obj->GetClassPrivate();
    if (!cls) { buf[0] = L'\0'; return; }
    auto name = cls->GetName();
    wcsncpy_s(buf, bufLen, name.c_str(), _TRUNCATE);
}

static bool seh_classNameToBuf(UObject* obj, wchar_t* buf, size_t bufLen) noexcept
{
    __try {
        dispatchClassNameToBuf(obj, buf, bufLen);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (buf && bufLen) buf[0] = L'\0';
        return false;
    }
}

static std::wstring safeClassName(UObject* obj)
{
    if (!obj) return L"";
    wchar_t buf[256] = {};
    if (!seh_classNameToBuf(obj, buf, sizeof(buf) / sizeof(buf[0]))) return L"";
    return std::wstring(buf);
}


static bool isObjectAlive(UObject* obj)
{
    if (!obj) return false;
    // SEH guard: pointer may be fully freed (not just GC-flagged), so reading
    // object flags can access-violate. This happens when UMG widgets are destroyed
    // during world transitions or menu rebuilds and our cached raw pointer dangles.
    __try
    {
        if (obj->HasAnyFlags(static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed))) return false;
        if (obj->IsUnreachable()) return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


// true if a widget is currently displayed (parent slot non-null OR
// IsInViewport returns true). Used to filter stale ghost UMG widgets returned
// by FindAllOf that linger between GC passes after the screen was closed.
// Calling BP MarkAllAsRead on a stale widget is a silent no-op, which is what
// caused crafting recipes to revert on reload in v6.20.16.
static bool isWidgetInViewport(UObject* widget)
{
    if (!widget || !isObjectAlive(widget)) return false;
    __try
    {
        // Try IsInViewport UFunction first (UUserWidget). Returns bool.
        auto* isInVp = widget->GetFunctionByNameInChain(STR("IsInViewport"));
        if (isInVp)
        {
            struct { bool ret; } parm{false};
            safeProcessEvent(widget, isInVp, &parm);
            if (parm.ret) return true;
        }
        // Fallback: Slot pointer non-null means widget is parented in a panel.
        auto** slotPtr = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("Slot"));
        if (slotPtr && *slotPtr) return true;
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


// SEH-wrapped FindAllOf. C++ functions with destructors can't host __try,
// so we put the call in a plain helper. Returns false on AV (caller
// should treat as "no results"). The output vector is filled on success.
static bool seh_findAllOf(const wchar_t* className, std::vector<UObject*>* out) noexcept
{
    __try {
        UObjectGlobals::FindAllOf(className, *out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Convenience wrapper around seh_findAllOf. Cherry-picked from
// v6.11.0. Use this in place of raw `UObjectGlobals::FindAllOf` so we
// get SEH protection against vtable-AV during world-unload (when
// GUObjectArray entries are mid-destruction with garbage vtables).
//
// IMPORTANT: even with this wrapper, ALWAYS run `isObjectAlive()` on
// each returned pointer before dereferencing — pointers can be valid
// but flagged for GC.
static bool findAllOfSafe(const wchar_t* className, std::vector<UObject*>& out) noexcept
{
    return seh_findAllOf(className, &out);
}

UObject* findPlayerController()
{
    std::vector<UObject*> pcs;
    // FindAllOf iterates GUObjectArray and reads each entry's class via
    // a virtual call. During world unload some entries are in the
    // process of being destroyed — their vtable becomes garbage
    // (0xFFF... or 0x000... pattern) and the dispatch AVs. Wrap in SEH.
    if (!seh_findAllOf(STR("PlayerController"), &pcs)) return nullptr;
    if (pcs.empty()) return nullptr;

    // In multiplayer the engine holds replicated proxies of every remote PC
    // alongside the local one. Filter for the actual local PC by reading the
    // UPROPERTY bIsLocalPlayerController directly (no warn-logging helper).
    for (auto* pc : pcs)
    {
        if (!pc || !isObjectAlive(pc)) continue;
        for (auto* strct = static_cast<UStruct*>(pc->GetClassPrivate());
             strct; strct = strct->GetSuperStruct())
        {
            FBoolProperty* found = nullptr;
            for (auto* prop : strct->ForEachProperty())
            {
                if (prop->GetName() == std::wstring_view(STR("bIsLocalPlayerController")))
                {
                    found = CastField<FBoolProperty>(prop);
                    break;
                }
            }
            if (found)
            {
                if (found->GetPropertyValueInContainer(pc)) return pc;
                break;
            }
        }
    }

    // Fallback (dedicated server with only remote proxies, or property missing)
    return nullptr;
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
    safeProcessEvent(pc, fn, &p);
    return p.Ret;
}

FVec3f getPawnLocation()
{
    FVec3f loc{0, 0, 0};
    auto* pawn = getPawn();
    if (!pawn) return loc;
    auto* fn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
    if (!fn) return loc;
    safeProcessEvent(pawn, fn, &loc);
    return loc;
}


void setWidgetVisibility(UObject* widget, uint8_t vis)
{
    if (!widget || !isObjectAlive(widget)) return;
    auto* fn = widget->GetFunctionByNameInChain(STR("SetVisibility"));
    if (!fn) return;
    uint8_t parms[8]{};
    parms[0] = vis;
    safeProcessEvent(widget, fn, parms);
}


UObject* addChildToPanel(UObject* parent, const wchar_t* fnName, UObject* child)
{
    if (!parent || !child) return nullptr;
    auto* fn = parent->GetFunctionByNameInChain(fnName);
    if (!fn) return nullptr;
    auto* pC = findParam(fn, STR("Content"));
    auto* pR = findParam(fn, STR("ReturnValue"));
    int sz = fn->GetParmsSize();
    std::vector<uint8_t> ap(sz, 0);
    if (pC && pC->GetOffset_Internal() >= 0 && pC->GetOffset_Internal() + (int)sizeof(UObject*) <= sz)
        *reinterpret_cast<UObject**>(ap.data() + pC->GetOffset_Internal()) = child;
    safeProcessEvent(parent, fn, ap.data());
    if (pR && pR->GetOffset_Internal() >= 0 && pR->GetOffset_Internal() + (int)sizeof(UObject*) <= sz)
        return *reinterpret_cast<UObject**>(ap.data() + pR->GetOffset_Internal());
    return nullptr;
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


UObject* findWidgetByClass(const wchar_t* className, bool requireVisible = false)
{
    std::vector<UObject*> widgets;
    findAllOfSafe(STR("UserWidget"), widgets);
    QBLOG(STR("[MoriaCppMod] [QB] findWidgetByClass('{}') requireVisible={} total={}\n"),
          className, requireVisible, widgets.size());
    for (auto* w : widgets)
    {
        if (!w || !isObjectAlive(w)) continue;
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
                safeProcessEvent(w, visFunc, &vp);
                if (vp.Ret) return w;
            }
        }
    }
    QBLOG(STR("[MoriaCppMod] [QB] findWidgetByClass('{}') -> NOT FOUND\n"), className);
    return nullptr;
}
