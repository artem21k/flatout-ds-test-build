#!/usr/bin/env python3
"""Procedural texture + font generator for FLATOUT DS.
Writes source/textures.h (RGBA5551 arrays, DS-ready) and preview PNGs.
Run:  python3 tools/gen_textures.py"""
import numpy as np, os, math
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import gaussian_filter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "source", "textures.h")
PREV = os.path.join(ROOT, "preview")
os.makedirs(PREV, exist_ok=True)
rng = np.random.default_rng(1234)

def tnoise(w, h, sigma):
    n = rng.standard_normal((h, w))
    n = gaussian_filter(n, sigma, mode="wrap")
    n -= n.mean(); n /= (n.std() + 1e-9)
    return n

def fbm(w, h, sigmas=(1, 2, 4), weights=(0.5, 0.3, 0.2)):
    t = sum(wt * tnoise(w, h, s) for s, wt in zip(sigmas, weights))
    return t / (np.abs(t).max() + 1e-9) * 1.6

def col(c): return np.array(c, dtype=np.float64)

def finish(rgb, alpha=None):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    h, w = rgb.shape[:2]
    if alpha is None: alpha = np.ones((h, w), bool)
    return rgb, alpha

def to_u16(rgb, alpha):
    r = (rgb[..., 0] >> 3).astype(np.uint16)
    g = (rgb[..., 1] >> 3).astype(np.uint16)
    b = (rgb[..., 2] >> 3).astype(np.uint16)
    v = r | (g << 5) | (b << 10)
    v = np.where(alpha, v | 0x8000, 0).astype(np.uint16)
    return v

def from_pil(img):
    a = np.array(img.convert("RGBA"))
    return a[..., :3], a[..., 3] > 127

textures = []  # (name, w, h, u16 array)
def add(name, rgb_alpha):
    rgb, alpha = rgb_alpha
    h, w = rgb.shape[:2]
    textures.append((name, w, h, to_u16(rgb, alpha)))
    img = Image.fromarray(np.dstack([rgb, (alpha * 255).astype(np.uint8)]), "RGBA")
    bg = Image.new("RGBA", img.size, (255, 0, 255, 255))
    bg.alpha_composite(img)
    k = max(1, 256 // max(w, h)) if max(w, h) < 256 else 1
    bg.resize((w * k, h * k), Image.NEAREST).convert("RGB").save(os.path.join(PREV, f"tex_{name}.png"))

# ---------------------------------------------------------------- ground
def ground(base, contrast, speck=(0.03, 25), extra=None):
    n = fbm(64, 64) * contrast
    c = col(base)[None, None, :] * (1 + n[..., None])
    sp = rng.random((64, 64))
    c += np.where(sp < speck[0] / 2, speck[1], 0)[..., None]
    c -= np.where(sp > 1 - speck[0] / 2, speck[1], 0)[..., None]
    if extra: c = extra(c)
    return finish(c)

def grass_extra(c):
    d = rng.random((64, 64))
    c[d < 0.004] = (200, 190, 80)
    c[(d > 0.5) & (d < 0.51)] = (28, 80, 30)
    return c
add("grass", ground((52, 118, 44), 0.16, extra=grass_extra))

def dirt_extra(c):
    for _ in range(70):
        x, y = rng.integers(0, 64, 2)
        cc = (96, 74, 52) if rng.random() < 0.55 else (186, 156, 118)
        c[y, x] = cc
        if rng.random() < 0.4: c[(y + 1) % 64, x] = cc
    return c
add("dirt", ground((146, 110, 74), 0.14, extra=dirt_extra))

def sand_extra(c):
    yy = np.arange(64)[:, None] * np.ones((1, 64))
    n = tnoise(64, 64, 3) * 0.35
    rip = np.sin(2 * np.pi * (yy / 16.0 + n)) * 0.05
    return c * (1 + rip[..., None])
add("sand", ground((218, 186, 126), 0.08, speck=(0.02, 14), extra=sand_extra))

# ---------------------------------------------------------------- roads (u across, v along, tileable in v)
def road(base, verge, rut_dark=0.20):
    W = H = 64
    u = (np.arange(W) + 0.5) / W
    n = fbm(W, H, (1, 2, 3), (0.5, 0.3, 0.2))
    c = col(base)[None, None, :] * (1 + 0.12 * n[..., None])
    ruts = (np.exp(-((u - 0.30) / 0.06) ** 2) + np.exp(-((u - 0.70) / 0.06) ** 2)) * rut_dark
    c *= (1 - ruts[None, :, None])
    c *= (1 + 0.06 * np.exp(-((u - 0.5) / 0.08) ** 2))[None, :, None]
    # ragged verge
    en = tnoise(W, H, 1.5)
    t = 0.085 + 0.035 * en
    uu = np.broadcast_to(u[None, :], (H, W))
    isv = (uu < t) | (uu > 1 - t)
    vc = col(verge)[None, None, :] * (1 + 0.15 * n[..., None])
    c = np.where(isv[..., None], vc, c)
    # stones
    st = rng.random((H, W))
    c[(st < 0.012)] = (170, 160, 150)
    c[(st > 0.99)] = (70, 60, 50)
    return finish(c)
add("road_dirt", road((124, 104, 82), (70, 92, 48)))
add("road_sand", road((198, 134, 86), (222, 190, 132), 0.17))

def start_tex():
    rgb, a = road((124, 104, 82), (70, 92, 48))
    rgb = rgb[:32].copy(); a = a[:32]
    for y in range(12, 20):
        for x in range(6, 58):
            sq = ((x - 6) // 6 + (y - 12) // 4) & 1
            rgb[y, x] = (240, 240, 240) if sq == 0 else (24, 24, 24)
    return rgb, a
add("start", start_tex())

# ---------------------------------------------------------------- billboards (alpha cutout)
def pine():
    W, H = 64, 128
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([28, 108, 35, 127], fill=(92, 60, 32, 255))
    d.rectangle([28, 108, 30, 127], fill=(120, 80, 44, 255))
    tiers = [(2, 40, 15), (22, 64, 20), (42, 90, 25), (62, 114, 30)]
    for top, bot, hw in tiers:
        pts = [(32, top)]
        k = 6
        for i in range(k + 1):
            x = 32 - hw + i * (2 * hw / k)
            y = bot + (3 if i % 2 else 0)
            pts.append((x, y))
        d.polygon(pts, fill=(28, 92, 40, 255))
        left = [(32, top)] + [(32 - hw + i * (2 * hw / k), bot + (3 if i % 2 else 0)) for i in range(0, k // 2 + 1)]
        d.polygon(left, fill=(44, 122, 56, 255))
        d.line([(32, top), (32 - hw + 3, bot - 2)], fill=(70, 150, 82, 255))
        d.line([(32 + 2, top + 6), (32 + hw - 4, bot - 3)], fill=(20, 68, 30, 255))
    rgb, alpha = from_pil(img)
    rgb = rgb.astype(np.float64)
    n = rng.standard_normal((H, W)) * 8
    rgb[alpha] += n[alpha][:, None]
    return finish(rgb, alpha)
add("pine", pine())

def cactus():
    W, H = 32, 64
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    g = (52, 132, 74, 255); g2 = (36, 100, 56, 255); g3 = (92, 170, 108, 255)
    d.rounded_rectangle([12, 6, 20, 63], radius=4, fill=g)
    d.rounded_rectangle([3, 22, 8, 40], radius=2, fill=g)
    d.rectangle([3, 36, 13, 41], fill=g)
    d.rounded_rectangle([23, 14, 28, 34], radius=2, fill=g)
    d.rectangle([19, 30, 28, 35], fill=g)
    for x in (14, 17):
        d.line([(x, 8), (x, 62)], fill=g2)
    d.line([(13, 8), (13, 62)], fill=g3)
    for (x, y) in [(5, 26), (5, 33), (25, 20), (25, 28), (14, 15), (18, 30), (14, 44), (18, 52)]:
        d.point((x, y), fill=(230, 230, 190, 255))
    return finish(*from_pil(img))
add("cactus", cactus())

def rock():
    W, H = 64, 32
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.polygon([(2, 31), (6, 16), (16, 6), (30, 2), (44, 5), (57, 14), (62, 31)], fill=(150, 132, 116, 255))
    d.polygon([(6, 16), (16, 6), (30, 2), (26, 14), (14, 22)], fill=(196, 178, 158, 255))
    d.polygon([(30, 2), (44, 5), (57, 14), (44, 18), (26, 14)], fill=(172, 154, 136, 255))
    d.polygon([(2, 31), (14, 22), (26, 14), (44, 18), (40, 31)], fill=(124, 108, 94, 255))
    d.polygon([(44, 18), (57, 14), (62, 31), (40, 31)], fill=(98, 86, 76, 255))
    d.line([(26, 14), (30, 31)], fill=(84, 72, 64, 255))
    rgb, alpha = from_pil(img)
    rgb = rgb.astype(np.float64) + (rng.standard_normal((H, W)) * 7)[..., None]
    return finish(rgb, alpha)
add("rock", rock())

# ---------------------------------------------------------------- arena props
def wall():
    W, H = 64, 32
    n = fbm(W, H) * 0.10
    c = col((172, 172, 176))[None, None, :] * (1 + n[..., None])
    for y in range(0, 9):          # hazard stripe band, diagonal red/white
        for x in range(W):
            c[y, x] = (196, 36, 30) if ((x + y) // 6) & 1 else (238, 238, 238)
    c[9, :] = (60, 60, 64)
    for y in range(10, H):         # panel seams + bolts
        c[y, 0] = (110, 110, 116); c[y, 32] = (110, 110, 116)
    for x in (4, 28, 36, 60):
        for y in (14, 28):
            c[y, x] = (80, 80, 86)
    grime = np.linspace(0, 1, H)[:, None] ** 2 * 0.25
    c[10:] *= (1 - grime[10:, :, None])
    c[H - 2:] *= 0.7
    return finish(c)
add("wall", wall())

def pillar():
    W = H = 32
    n = fbm(W, H) * 0.10
    c = col((164, 164, 168))[None, None, :] * (1 + n[..., None])
    for y in range(18, 30):
        for x in range(W):
            c[y, x] = (240, 200, 30) if ((x + y) // 5) & 1 else (24, 24, 24)
    c[17, :] = (60, 60, 60); c[30, :] = (60, 60, 60); c[31, :] = (90, 90, 90)
    c[8, :] = (130, 130, 136)
    return finish(c)
add("pillar", pillar())

def barrel():
    W = H = 32
    u = (np.arange(W) + 0.5) / W
    shade = 0.62 + 0.55 * np.sin(np.pi * u) ** 0.7
    n = fbm(W, H) * 0.10
    c = col((196, 44, 30))[None, None, :] * (shade[None, :, None]) * (1 + n[..., None])
    for y in (4, 5, 15, 16, 26, 27):
        c[y, :] = col((150, 150, 156))[None, :] * shade[:, None]
    for x in range(8, 24):
        for y in range(8, 13):
            c[y, x] = col((240, 210, 40)) * shade[x]  # label
    return finish(c)
add("barrel", barrel())

def block():
    W = H = 64
    u = (np.arange(W) + 0.5) / W
    rib = 0.80 + 0.20 * np.sin(2 * np.pi * u * 10)
    n = fbm(W, H) * 0.14
    c = col((178, 74, 40))[None, None, :] * rib[None, :, None] * (1 + n[..., None])
    c[0:5, :] = (110, 44, 26); c[H - 5:H, :] = (96, 38, 22)
    c[:, 31:33] = (70, 30, 20)
    rust = tnoise(W, H, 2.5)
    c[rust > 1.2] = (130, 70, 40)
    for y in (22, 30):
        c[y, 20:28] = (210, 210, 200); c[y, 40:48] = (210, 210, 200)
    return finish(c)
add("block", block())

def tires():
    W = H = 32
    c = np.zeros((H, W, 3)) + col((30, 30, 33))
    for y in range(H):
        t = y % 8
        if t == 0: c[y] = (74, 74, 80)
        elif t == 1: c[y] = (50, 50, 54)
        elif t == 7: c[y] = (14, 14, 16)
    n = fbm(W, H) * 0.25
    c *= (1 + n[..., None])
    for x in range(2, 30, 6):
        for y in (3, 11, 19, 27):
            c[y, x:x + 3] = (190, 190, 190)
    return finish(c)
add("tires", tires())

# ---------------------------------------------------------------- vehicles
def wheel():
    W = H = 16
    img = Image.new("RGBA", (W, H), (14, 14, 16, 255))
    d = ImageDraw.Draw(img)
    d.ellipse([0, 0, 15, 15], fill=(36, 36, 40, 255))
    d.ellipse([3, 3, 12, 12], fill=(170, 172, 180, 255))
    d.ellipse([5, 5, 10, 10], fill=(110, 112, 120, 255))
    for (x, y) in [(7, 4), (11, 7), (9, 11), (5, 10), (4, 6)]:
        d.point((x, y), fill=(230, 230, 235, 255))
    d.point((7, 7), fill=(40, 40, 44, 255)); d.point((8, 8), fill=(40, 40, 44, 255))
    return finish(*from_pil(img))
add("wheel", wheel())

add("black", finish(np.full((8, 8, 3), 18.0)))
add("white", finish(np.full((8, 8, 3), 255.0)))

# ---------------------------------------------------------------- sky bands (128x64, tileable horizontally)
def sky(top, mid, haze, far_c, near_c, kind):
    W, H = 128, 32
    v = np.linspace(0, 1, H)[:, None]                     # 0 top, 1 bottom
    t = np.clip(v / 0.72, 0, 1)
    base = col(top)[None, None, :] * (1 - t[..., None]) + col(haze)[None, None, :] * t[..., None]
    base = np.broadcast_to(base, (H, W, 3)).copy()
    # clouds
    cn = tnoise(W, H, 5) * 0.8 + tnoise(W, H, 2.5) * 0.35
    mask = np.clip((cn - 0.55) * 1.4, 0, 1) * np.clip(1.2 - v * 1.4, 0, 1)
    base = base * (1 - mask[..., None] * 0.9) + col((250, 250, 255))[None, None, :] * (mask[..., None] * 0.9)
    # ridge silhouettes
    x = np.arange(W)
    def ridge(seed_s, amp, off, sharp):
        r = gaussian_filter(rng.standard_normal(W), seed_s, mode="wrap")
        r = (r - r.min()) / (r.max() - r.min() + 1e-9)
        if kind == "mesa":
            r = np.round(r * 3.0) / 3.0 if sharp else r
        return off - amp * r
    far = ridge(3, 7, 26, kind == "mesa")
    near = ridge(2, 6, 29, False)
    out = base.copy()
    rows = np.arange(H)[:, None]
    out = np.where((rows >= far[None, :])[..., None], col(far_c)[None, None, :], out)
    out = np.where((rows >= near[None, :])[..., None], col(near_c)[None, None, :], out)
    if kind != "mesa":                                   # spiky pine tops on near ridge
        for xx in range(W):
            if rng.random() < 0.5:
                top_y = int(near[xx]) - int(rng.integers(1, 4))
                if 0 <= top_y < H: out[top_y:int(near[xx]) + 1, xx] = col(near_c)
    return finish(out)
add("sky_forest", sky((70, 120, 200), (130, 170, 225), (206, 222, 236), (92, 128, 142), (34, 78, 44), "pine"))
add("sky_desert", sky((86, 130, 196), (150, 180, 220), (240, 214, 170), (186, 120, 84), (150, 84, 54), "mesa"))

# ---------------------------------------------------------------- particles + banner
def smoke():
    W = H = 32
    yy, xx = np.mgrid[0:H, 0:W]
    r = np.hypot(xx - 15.5, yy - 15.5) / 15.0
    n = tnoise(W, H, 2) * 0.12
    alpha = r + n < 0.92
    shade = 215 - 110 * np.clip(r, 0, 1)
    rgb = np.dstack([shade, shade, shade])
    return finish(rgb, alpha)
add("smoke", smoke())

def spark():
    W = H = 16
    yy, xx = np.mgrid[0:H, 0:W]
    dx, dy = np.abs(xx - 7.5), np.abs(yy - 7.5)
    alpha = (dx * dy < 3.2) & (np.hypot(dx, dy) < 7.8)
    r = np.hypot(dx, dy) / 8
    rgb = np.dstack([255 - 0 * r, 255 - 90 * r, 220 - 200 * r])
    return finish(rgb, alpha)
add("spark", spark())

def banner():
    W, H = 64, 16
    img = Image.new("RGBA", (W, H), (190, 28, 28, 255))
    d = ImageDraw.Draw(img)
    for i in range(0, W, 8):
        d.rectangle([i, 0, i + 3, 1], fill=(240, 240, 240, 255)); d.rectangle([i + 4, H - 2, i + 7, H - 1], fill=(240, 240, 240, 255))
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 11)
    d.text((32, 8), "START", font=f, fill=(255, 255, 255, 255), anchor="mm")
    return finish(*from_pil(img))
add("banner", banner())

# ---------------------------------------------------------------- 8x8 font (ASCII 32..127)
# Rendered big from DejaVu Sans Mono Bold (caps only), box-filtered down to a 6x7 glyph
# inside an 8x8 cell, so S/5, B/8, O/0 stay distinguishable. Lowercase maps to caps.
def make_font():
    BIG = 80
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", BIG)
    cap = f.getbbox("H", anchor="ls")[1] * -1          # cap height in big px
    unit = cap / 7.0
    cw, chh = int(round(6 * unit)), int(round(8 * unit))
    data = []
    sheet = Image.new("L", (96 * 8, 8), 0)
    for i in range(96):
        ch = chr(32 + i)
        if "a" <= ch <= "z": ch = ch.upper()
        big = Image.new("L", (cw, chh), 0)
        ImageDraw.Draw(big).text((cw / 2, 7 * unit), ch, font=f, fill=255, anchor="ms")
        small = np.array(big.resize((6, 8), Image.BOX), dtype=np.float64) / 255.0
        a = small > 0.40
        cell = np.zeros((8, 8), bool)
        cell[:, 1:7] = a
        if ch == "0":                                   # slashed zero so 0 never reads as 8
            cell[:] = False
            for yy, row in enumerate([".XXXX.", "X....X", "X...XX", "X..X.X", "XX...X", "X....X", ".XXXX."]):
                for xx, cc in enumerate(row):
                    if cc == "X": cell[yy, 1 + xx] = True
        for y in range(8):
            b = 0
            for x in range(8):
                if cell[y, x]: b |= 1 << (7 - x)
            data.append(b)
        sheet.paste(Image.fromarray((cell * 255).astype(np.uint8)), (i * 8, 0))
    sheet.resize((96 * 8 * 2, 16), Image.NEAREST).save(os.path.join(PREV, "font.png"))
    big_sheet = Image.new("L", (32 * 9, 3 * 9), 0)
    for i in range(96):
        tile = sheet.crop((i * 8, 0, i * 8 + 8, 8))
        big_sheet.paste(tile, ((i % 32) * 9, (i // 32) * 9))
    big_sheet.resize((32 * 9 * 4, 3 * 9 * 4), Image.NEAREST).save(os.path.join(PREV, "font_grid.png"))
    return data
font = make_font()

# ---------------------------------------------------------------- cars (stations + painted atlases)
import carspec
cars_header = carspec.build(add)
import dmg_tex                                   # damage system: flat engine-bay picture + two dent/scratch overlay tiles (added after the cars, older TX_ ids stay)
add("engine", dmg_tex.engine_tex())
add("dmg1", dmg_tex.dmg_light())
add("dmg2", dmg_tex.dmg_heavy())
open(os.path.join(ROOT, "source", "cars.h"), "w").write(cars_header)

# ---------------------------------------------------------------- write header
with open(OUT_H, "w") as fh:
    fh.write("/* AUTO-GENERATED by tools/gen_textures.py - do not edit by hand.\n"
             "   All textures are RGBA5551 (bit15 = opaque) for the DS 3D engine. */\n#pragma once\n\n")
    fh.write("typedef struct { const unsigned short *data; int w, h; } TexDef;\n\n")
    for (name, w, h, arr) in textures:
        fh.write(f"static const unsigned short tex_{name}[{w*h}] __attribute__((aligned(4))) = {{\n")
        flat = arr.flatten()
        for i in range(0, len(flat), 16):
            fh.write("  " + ",".join(f"0x{int(v):04X}" for v in flat[i:i + 16]) + ",\n")
        fh.write("};\n\n")
    fh.write("enum {\n")
    for i, (name, w, h, arr) in enumerate(textures):
        fh.write(f"  TX_{name.upper()} = {i},\n")
    fh.write(f"  TX_COUNT = {len(textures)}\n}};\n\n")
    fh.write("static const TexDef texDefs[TX_COUNT] = {\n")
    for (name, w, h, arr) in textures:
        fh.write(f"  {{ tex_{name}, {w}, {h} }},\n")
    fh.write("};\n\n")
    fh.write("static const unsigned char font8x8[96*8] = {\n")
    for i in range(0, len(font), 8):
        fh.write("  " + ",".join(f"0x{b:02X}" for b in font[i:i + 8]) + ",\n")
    fh.write("};\n")
tot = sum(w * h * 2 for (_, w, h, _) in textures)
print("textures:", len(textures), "total texture bytes:", tot)
