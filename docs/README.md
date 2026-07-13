# MoriaCppMod Documentation Index

Status legend: ✅ current (v8.5.x) · 🟡 older but structurally accurate · 📦 reference material

| Document | Status | What it covers |
|---|---|---|
| [architecture.md](architecture.md) | ✅ | Top-level architecture, source layout, stability rules, design patterns. **Start here.** |
| [goat-final-architecture.md](goat-final-architecture.md) | ✅ | Porter Goat companion — definitive design, movement stack, UI/input lessons, and the complete dead-end list. |
| [RELEASE_NOTES_v8.5.0.md](RELEASE_NOTES_v8.5.0.md) | ✅ | Latest shipped release notes (also v8.2.1, v8.2.0 alongside). |
| [joinworld-ui-takeover.md](joinworld-ui-takeover.md) | 🟡 | In-place native-screen modification methodology (Join World + Advanced Join). Still the reference pattern. |
| [dllmain.md](dllmain.md) | 🟡 | Main class walkthrough (v5-era; hooks/tick structure still broadly right, keybinds/goat/NPC sections superseded by architecture.md). |
| [hism.md](hism.md) | 🟡 | HISM removal/replay/bubble subsystem. |
| [inventory.md](inventory.md) | 🟡 | Inventory tools + container discovery. |
| [placement.md](placement.md) | 🟡 | Build menu, ghost detection, pitch/roll. |
| [quickbuild.md](quickbuild.md) | 🟡 | Quick-build state machine (now gated on the Advanced Builder toggle, see architecture.md). |
| [widgets.md](widgets.md) | 🟡 | UMG widget creation patterns (predates the NBB; the old MC/AB/builders toolbars it mentions are removed). |
| [stability.md](stability.md) | 🟡 | Stability audit feature. |
| [debug.md](debug.md) | 🟡 | Debug helpers (NOTE: all hardcoded NUM/N dev hotkeys were disabled in v8.2.x; diagnostics are ini-gated or keybind-driven now). |
| [overlay.md](overlay.md) | 🟡 | Legacy GDI+ overlay — **deprecated**, kept for archaeology. |
| blueprint-reference/ | 📦 | Decoded BP function inventories for the Join World screens. |
| game-assets/ | 📦 | Copies of referenced game .uassets (fonts, UI widgets, textures). |
| widget-harvest/ | 📦 | Runtime-captured widget-tree JSON dumps. |
| join-world-screens/ | 📦 | Reference screenshots + pixel-diff tooling for the UI clone work. |
| Khazad-dum ... Knowledge Base.docx/.txt | 📦 | User-facing knowledge base (versioned separately; edits require a Document Version bump + redeploy to both desktops). |

Removed in the v8.5.1 cleanup: `goat-follow-stay-architecture.md` (superseded by goat-final-architecture.md).
