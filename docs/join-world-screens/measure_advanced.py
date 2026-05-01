"""Thorough measurement of WBP_UI_AdvancedJoinOptions positions.
Master 4K (3840x2160). Design = 1920x1080 (= /2).
Panel content lives roughly at 4K x=40..620 (design x=20..310).
"""
from PIL import Image
im = Image.open('Advanced Join Original.png').convert('RGB')
W, H = im.size
px = im.load()

# Find rows with significant bright (text) content within the panel column.
# A "text row" = any row in the panel x-range that has >= 5 bright pixels (>180 brightness).
print("ROWS WITH BRIGHT CONTENT (>=5 px bright in x=40..620):")
print("(brightness >180 — captures text & input-field whites)\n")
in_band = False
band_start = 0
for y in range(0, H):
    bright_count = 0
    for x in range(40, 620, 2):  # every other px for speed
        r, g, b = px[x, y]
        if (r+g+b)/3 > 180:
            bright_count += 1
    if bright_count >= 5:
        if not in_band:
            band_start = y
            in_band = True
    else:
        if in_band:
            if y - band_start >= 4:
                # find horizontal extent at the brightest row in band
                mid_y = (band_start + y) // 2
                xs_bright = []
                for x in range(40, 620):
                    r, g, b = px[x, mid_y]
                    if (r+g+b)/3 > 180:
                        xs_bright.append(x)
                if xs_bright:
                    xL, xR = xs_bright[0], xs_bright[-1]
                    print(f"  4K y={band_start:4d}..{y-1:4d} (h={y-band_start:3d}) | design y={band_start//2:4d}..{(y-1)//2:4d} (h={(y-band_start)//2:3d}) | 4K x={xL}..{xR} (w={xR-xL+1}) | design x={xL//2}..{xR//2}")
            in_band = False

# Find teal-ish button bands (R<G<B, low saturation)
print("\n\nTEAL BUTTON BANDS (greenish/teal R<G<B):")
def is_teal(r, g, b):
    return r < g and r < b and 50 < r < 130 and 70 < g < 160 and 70 < b < 160

# scan y across the whole image — for each row, count teal pixels in panel x-range
teal_rows = []
for y in range(0, H):
    cnt = 0
    for x in range(40, 620, 2):
        r, g, b = px[x, y]
        if is_teal(r, g, b):
            cnt += 1
    teal_rows.append(cnt)

in_b = False
bs = 0
for y, c in enumerate(teal_rows):
    if c >= 10:
        if not in_b:
            bs = y
            in_b = True
    else:
        if in_b:
            if y - bs >= 6:
                # find horizontal extent
                mid = (bs + y) // 2
                xs = [x for x in range(40, 620) for r,g,b in [px[x, mid]] if is_teal(r,g,b)]
                if xs:
                    xL, xR = xs[0], xs[-1]
                    print(f"  4K y={bs:4d}..{y-1:4d} (h={y-bs:3d}) | design y={bs//2:4d}..{(y-1)//2:4d} (h={(y-bs)//2:3d}) | 4K x={xL}..{xR} (w={xR-xL+1}) | design x={xL//2}..{xR//2} (w={(xR-xL+1)//2})")
            in_b = False
