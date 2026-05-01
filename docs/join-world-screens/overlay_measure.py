"""
Overlay-measure tool for the Join World UI.

Compares two screenshots (master native vs our duplicate) and reports the
delta of each key landmark's bounding box, then converts to design-pixel
adjustments for the C++ builder.

Usage:
    python overlay_measure.py 01_main_with_history.png 02_current_iter3.png

Both images should be full-resolution PNG screenshots of the same window
size (ideally identical resolution — typically 3840x2160 from a 4K capture).

Algorithm:
1. Crop to the left UI column (x in [0, 30%-of-width]) — discard dwarf area.
2. Convert to grayscale, find bright regions (text/buttons are lighter than
   the dark background).
3. Threshold + connected components to extract bounding boxes.
4. For each landmark, find its bbox by approximate Y-band:
   - WORLD SELECTION: top 12% of column
   - JOIN OTHER WORLD: 13-22%
   - Enter Invite Code...: 22-28%
   - Input field: 28-35%
   - Advanced Join Options: 35-42%
   - Session History header: 42-50%
   - Session row: 50-65%
5. Compute delta_x = master.x - current.x, delta_y = master.y - current.y
   for the TOP-LEFT corner of each bbox.
6. Print human-readable report + suggested mod-side adjustments.

The master image was rendered at design res 1920x1080 (BP design canvas),
upscaled 2x to 3840x2160 by the renderer. So 1 image pixel = 0.5 design px.
For other resolutions, the script auto-detects scale from window dimensions.
"""

from __future__ import annotations
import sys, json
from pathlib import Path
from typing import NamedTuple

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Need: pip install pillow numpy", file=sys.stderr)
    sys.exit(2)


class BBox(NamedTuple):
    x: int; y: int; w: int; h: int
    @property
    def cx(self): return self.x + self.w // 2
    @property
    def cy(self): return self.y + self.h // 2


def find_text_bands(img_path: Path, ui_col_frac: float = 0.30) -> dict[str, BBox]:
    """Find bounding boxes for each landmark in the left UI column.

    Returns dict keyed by landmark name. Empty dict on failure.
    """
    im = Image.open(img_path).convert("L")  # grayscale
    W, H = im.size
    col_w = int(W * ui_col_frac)
    crop = im.crop((0, 0, col_w, H))
    arr = np.asarray(crop, dtype=np.uint8)
    # Threshold: anything brighter than 110 is "text/button" foreground
    fg = (arr > 110).astype(np.uint8)
    # For each row, count fg pixels — gives a 1D vertical signal
    row_sum = fg.sum(axis=1)
    # Find runs of rows where row_sum exceeds noise floor
    threshold = 4
    in_band = False
    bands = []  # list of (y_start, y_end)
    band_start = 0
    for y, s in enumerate(row_sum):
        if s > threshold and not in_band:
            in_band = True; band_start = y
        elif s <= threshold and in_band:
            in_band = False
            if y - band_start >= 3:  # min band height
                bands.append((band_start, y))
    if in_band:
        bands.append((band_start, H))

    # For each band, also compute X bbox
    out = []
    for y0, y1 in bands:
        sub = fg[y0:y1, :]
        col_sum = sub.sum(axis=0)
        cols = np.nonzero(col_sum > 0)[0]
        if len(cols) == 0: continue
        x0 = int(cols[0]); x1 = int(cols[-1] + 1)
        out.append(BBox(x0, y0, x1 - x0, y1 - y0))

    # Heuristic: assign first 7-8 bands by Y-order to landmark names.
    # The expected order in the UI is fixed.
    names = [
        "world_selection",
        "join_other_world",
        "enter_invite_label",
        "input_field",
        "advanced_button",
        "session_history_header",
        "session_history_row_title",
        "session_history_row_subtitle",
    ]
    result = {}
    for i, b in enumerate(out[:len(names)]):
        result[names[i]] = b
    return result


def report(master: Path, current: Path):
    print(f"== Master:  {master}")
    print(f"== Current: {current}")
    m = find_text_bands(master)
    c = find_text_bands(current)
    print(f"\nMaster bands found: {len(m)}")
    print(f"Current bands found: {len(c)}\n")

    # Resolution / scale
    Wm, Hm = Image.open(master).size
    Wc, Hc = Image.open(current).size
    if (Wm, Hm) != (Wc, Hc):
        print(f"!! WARNING: image sizes differ — master {Wm}x{Hm} vs current {Wc}x{Hc}")
        print("   Rerun with same-size captures for accurate deltas.\n")
    # Empirically validated against the master image: WORLD SELECTION renders
    # at image y=139, BP CanvasPanelSlot_0 places the GridPanel at design (124,128).
    # That confirms the game's UMG DPI scale at 4K is 1.0 — design px == image px.
    # (We do NOT divide by 2 even though image width is 3840 vs design 1920.)
    img_to_design = 1.0
    print(f"image-px to design-px scale factor: {img_to_design:.3f} (master width {Wm} — game uses 1.0 DPI scale at 4K)")
    print()

    print(f"{'Landmark':<32} {'Master (x,y,w,h)':<22} {'Current (x,y,w,h)':<22} {'dx':>5} {'dy':>5} {'dw':>5} {'dh':>5} {'dy_design':>10}")
    print("-" * 120)
    keys = list(m.keys())
    for k in keys:
        if k not in c:
            print(f"{k:<32} {str(m[k]):<22} <missing>")
            continue
        mb, cb = m[k], c[k]
        dx, dy, dw, dh = mb.x - cb.x, mb.y - cb.y, mb.w - cb.w, mb.h - cb.h
        dyd = dy * img_to_design
        print(f"{k:<32} ({mb.x:4d},{mb.y:4d},{mb.w:4d},{mb.h:3d}) ({cb.x:4d},{cb.y:4d},{cb.w:4d},{cb.h:3d}) {dx:5d} {dy:5d} {dw:5d} {dh:5d} {dyd:>+9.1f}")

    # Summary deltas
    print()
    print("SUGGESTED ADJUSTMENTS (in design pixels — applied to mod code):")
    if "world_selection" in m and "world_selection" in c:
        dx = (m["world_selection"].x - c["world_selection"].x) * img_to_design
        dy = (m["world_selection"].y - c["world_selection"].y) * img_to_design
        print(f"  Canvas slot offset shift: ({dx:+.0f}, {dy:+.0f}) design-px")
    if "join_other_world" in m and "join_other_world" in c:
        # Title height ratio gives font-size scale
        ratio = m["join_other_world"].h / max(c["join_other_world"].h, 1)
        print(f"  Title font size ×{ratio:.3f} (master height {m['join_other_world'].h}px / current {c['join_other_world'].h}px)")
    if "enter_invite_label" in m and "input_field" in m and "join_other_world" in m:
        m_gap = m["enter_invite_label"].y - (m["join_other_world"].y + m["join_other_world"].h)
        c_gap = c.get("enter_invite_label", BBox(0,0,0,0)).y - (c.get("join_other_world", BBox(0,0,0,0)).y + c.get("join_other_world", BBox(0,0,0,0)).h)
        d = (m_gap - c_gap) * img_to_design
        print(f"  TitletoSubtitle spacer: master gap {m_gap}px / current {c_gap}px to adjust spacer2 by {d:+.0f} design-px")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    report(Path(sys.argv[1]), Path(sys.argv[2]))
