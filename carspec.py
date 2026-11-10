"""Low-poly car models for RUMBLE FOREST DS: station tables + painted 128x64 atlases.
Each car is a lofted body: stations (b, yb, yz, yr, wh, wr) from front to back
  b  = position along the car (+ = front), yb = underside, yz = belt line (hull top),
  yr = roof/top (== yz where there is no cabin), wh = hull half-width, wr = roof half-width.
Atlas 128x64:  side 0..95 x 0..31 | top 96..127 x 0..55 | front 0..47 x 32..63 | back 48..95 x 32..63
              swatches: white (96..111, 56..63), black (112..127, 56..63)"""
import math, numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import gaussian_filter

FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SCALE = 1.35

def S(b, yb, yz, yr, wh, wr=None): return (b, yb, yz, yr, wh, wh if wr is None else wr)
def pair(a0, a1, y0, y1, b0, b1, col):          # mirrored box pair
    return [(-a1, -a0, y0, y1, b0, b1, col), (a0, a1, y0, y1, b0, b1, col)]
def centre(a0, a1, y0, y1, b0, b1, col): return [(a0, a1, y0, y1, b0, b1, col)]
CHROME = (176, 176, 182); DARK = (28, 28, 32)

CARS = [
 dict(name="ROAMER", cls=0, drive="RWD", stats=(3.0, 4.8, 4.0, 8.0, 8.0, 2.0), ui=(214, 120, 50),
      st=[S(1.45,.30,.78,.78,.72), S(1.12,.30,.84,.84,.74), S(.52,.30,.88,.88,.74), S(.14,.30,.90,1.40,.74,.60),
          S(-.30,.30,.90,1.40,.74,.60), S(-.42,.30,.90,.90,.74), S(-1.45,.30,.88,.88,.74)],
      ax=[(.92,.33,1), (-.92,.33,0)], ww=.12,
      boxes=centre(-.75,.75,.26,.48,1.45,1.58,CHROME)+centre(-.75,.75,.26,.48,-1.58,-1.45,CHROME),
      sty=dict(base=(190,100,44), rust=1.0, grime=.5, front='pickup', back='pickup', bed=(-.42,-1.45), doors=[.55,-.2],
               side_stripes=[], number=None, stripes=[], flames=False, hood=None)),
 dict(name="MALICE", cls=0, drive="RWD", stats=(3.6, 6.2, 5.6, 7.0, 6.6, 2.2), ui=(120, 120, 128),
      st=[S(1.50,.26,.62,.62,.72), S(1.20,.24,.70,.70,.76), S(.15,.24,.78,.78,.76), S(-.30,.24,.78,1.16,.76,.56),
          S(-.75,.24,.78,1.12,.76,.52), S(-1.20,.24,.82,.82,.76), S(-1.50,.26,.78,.78,.72)],
      ax=[(.95,.32,1), (-.95,.32,0)], ww=.12,
      boxes=centre(-.20,.20,.70,.88,.35,1.05,DARK)+centre(-.72,.72,.26,.36,1.50,1.58,CHROME)+centre(-.72,.72,.26,.36,-1.58,-1.50,CHROME),
      sty=dict(base=(34,34,38), rust=.7, grime=.9, front='coupe', back='coupe', bed=None, doors=[-.1,-.8],
               side_stripes=[], number=("17",(-.35,.5),((236,236,236),(20,20,20))), stripes=[((226,226,226),[(-.16,-.06),(.06,.16)])],
               flames=False, hood=None)),
 dict(name="NEVADA", cls=1, drive="4WD", stats=(6.2, 6.4, 5.6, 5.6, 6.0, 3.2), ui=(60, 100, 190),
      st=[S(1.55,.28,.76,.76,.76), S(1.25,.28,.86,.86,.78), S(.55,.28,.92,.92,.78), S(.20,.28,.94,1.40,.78,.64),
          S(-.62,.28,.94,1.40,.78,.64), S(-.78,.28,.94,.94,.78), S(-1.55,.28,.90,.90,.78)],
      ax=[(.98,.36,1), (-.98,.36,0)], ww=.13,
      boxes=centre(-.80,.80,.30,.52,1.55,1.68,CHROME)+centre(-.80,.80,.30,.52,-1.68,-1.55,CHROME)+centre(-.40,.40,1.40,1.50,-.36,-.24,DARK),
      sty=dict(base=(48,84,160), rust=.3, grime=.6, front='pickup', back='pickup', bed=(-.78,-1.55), doors=[.45,-.25],
               side_stripes=[((236,236,236),.62,.72),((200,40,40),.72,.78)], number=("23",(.1,.62),((240,240,240),(20,30,80))),
               stripes=[((236,236,236),[(-.2,-.06),(.06,.2)])], flames=False, hood=None)),
 dict(name="INSETTA", cls=1, drive="4WD", stats=(6.8, 7.2, 8.2, 3.8, 3.6, 3.6), ui=(240, 240, 244),
      st=[S(1.40,.24,.56,.56,.72), S(1.12,.24,.66,.66,.74), S(.40,.24,.72,.72,.74), S(-.05,.24,.72,1.18,.74,.56),
          S(-.70,.24,.72,1.16,.74,.54), S(-1.02,.24,.80,.80,.72), S(-1.42,.26,.78,.78,.70)],
      ax=[(.86,.31,1), (-.86,.31,0)], ww=.12,
      boxes=pair(.38,.46,.78,1.04,-1.34,-1.26,(236,236,238))+centre(-.66,.66,1.04,1.10,-1.46,-1.16,DARK),
      sty=dict(base=(238,238,240), rust=0, grime=.3, front='evo', back='evo', bed=None, doors=[-.05,-.7],
               side_stripes=[((196,40,40),.40,.50)], number=("5",(-.3,.5),((20,20,22),(240,240,240))), stripes=[],
               flames=False, hood=((22,22,26),.4,1.12))),
 dict(name="BULLET GT", cls=2, drive="RWD", stats=(8.2, 7.2, 6.4, 3.4, 4.4, 4.8), ui=(60, 100, 220),
      st=[S(1.52,.24,.60,.60,.74), S(1.22,.24,.68,.68,.78), S(.30,.24,.76,.76,.78), S(-.12,.24,.76,1.12,.78,.58),
          S(-.62,.24,.76,1.08,.78,.56), S(-1.15,.24,.80,.80,.78), S(-1.52,.24,.76,.76,.74)],
      ax=[(.90,.31,1), (-.90,.31,0)], ww=.12,
      boxes=centre(-.22,.22,.64,.82,.50,1.05,DARK)+centre(-.74,.74,.24,.34,1.52,1.60,CHROME)+centre(-.74,.74,.24,.34,-1.60,-1.52,CHROME),
      sty=dict(base=(24,24,28), rust=0, grime=.4, front='coupe', back='coupe', bed=None, doors=[-.05,-.7],
               side_stripes=[], number=None, stripes=[], flames=True, hood=None)),
 dict(name="CANYON", cls=2, drive="RWD", stats=(7.4, 6.8, 5.6, 4.8, 6.6, 4.2), ui=(70, 90, 160),
      st=[S(1.58,.34,.90,.90,.76), S(1.30,.34,.98,.98,.78), S(.58,.34,1.00,1.00,.78), S(.22,.34,1.00,1.52,.78,.66),
          S(-.58,.34,1.00,1.52,.78,.66), S(-.74,.34,1.00,1.00,.78), S(-1.58,.34,.96,.96,.78)],
      ax=[(1.0,.38,1), (-1.0,.38,0)], ww=.14,
      boxes=centre(-.80,.80,.34,.56,1.58,1.72,CHROME)+centre(-.80,.80,.34,.56,-1.72,-1.58,CHROME)+pair(.78,.90,.30,.36,-.5,.55,DARK),
      sty=dict(base=(34,50,110), rust=.15, grime=.5, front='pickup', back='pickup', bed=(-.74,-1.58), doors=[.5,-.2],
               side_stripes=[], number=("12",(.12,.66),((240,240,240),(20,30,90))),
               stripes=[((236,236,240),[(-.26,-.14),(-.06,.06),(.14,.26)])], flames=False, hood=None)),
 dict(name="TRUCK", cls=3, drive="RWD", stats=(4.0, 4.0, 3.0, 9.5, 9.8, 1.5), ui=(110, 110, 118),
      st=[S(2.05,.50,1.28,1.28,.86), S(1.50,.50,1.42,1.42,.86), S(.72,.50,1.44,1.44,.86), S(.62,.50,1.44,2.38,.86,.80),
          S(-.56,.50,1.44,2.46,.86,.80), S(-.62,.50,1.44,1.44,.86), S(-.66,.46,.80,.80,.56), S(-2.00,.46,.80,.80,.56)],
      ax=[(1.30,.50,1), (-1.05,.50,0), (-1.85,.50,0)], ww=.17,
      boxes=centre(-.90,.90,.46,.68,2.05,2.20,CHROME)+pair(.58,.66,.90,2.70,-.72,-.64,(200,200,205))+
            pair(.60,.84,.46,1.02,-1.50,-.80,(176,178,184))+centre(-.42,.42,.80,.90,-1.90,-1.10,DARK),
      sty=dict(base=(26,26,28), rust=.5, grime=.8, front='truck', back='truck', bed=None, doors=[.1],
               side_stripes=[((220,150,20),.95,1.0)], number=None, stripes=[], flames=False, hood=None)),
 dict(name="FLATMOBILE", cls=3, drive="RWD", stats=(9.4, 8.8, 7.4, 3.0, 3.8, 7.0), ui=(170, 172, 182),
      st=[S(1.60,.20,.34,.34,.66), S(1.00,.20,.50,.50,.80), S(.25,.20,.60,.60,.82), S(-.30,.20,.60,.98,.82,.52),
          S(-.85,.20,.60,.94,.82,.46), S(-1.25,.20,.72,.72,.84), S(-1.65,.22,.66,.66,.76)],
      ax=[(1.05,.33,1), (-1.05,.33,0)], ww=.12,
      boxes=centre(-.62,.62,.70,.76,-1.66,-1.50,DARK),
      sty=dict(base=(168,170,178), rust=.4, grime=.5, front='wedge', back='wedge', bed=None, doors=[-.1],
               side_stripes=[], number=None, stripes=[], flames=False, hood=None)),
]

class Atlas:
    def __init__(s, car):
        st = car['st']; s.car = car; s.st = st
        s.Bm = max(abs(t[0]) for t in st) + 0.15
        s.Ym = max(t[3] for t in st) + 0.10
        s.Am = max(max(t[4], t[5]) for t in st) + 0.12
        s.img = Image.new("RGBA", (128, 64), (0, 0, 0, 255)); s.d = ImageDraw.Draw(s.img)
    def side(s, b, y): return (0.25 + (b + s.Bm) / (2 * s.Bm) * 95.5, 0.25 + (1 - y / s.Ym) * 31.5)
    def top(s, a, b): return (96.25 + (a + s.Am) / (2 * s.Am) * 31.5, 0.25 + (s.Bm - b) / (2 * s.Bm) * 55.5)
    def front(s, a, y): return (0.25 + (s.Am - a) / (2 * s.Am) * 47.5, 32.25 + (1 - y / s.Ym) * 31.5)
    def back(s, a, y): return (48.25 + (a + s.Am) / (2 * s.Am) * 47.5, 32.25 + (1 - y / s.Ym) * 31.5)
    def poly(s, pts, fill): s.d.polygon([tuple(p) for p in pts], fill=tuple(fill))
    def line(s, p, q, fill, w=1): s.d.line([tuple(p), tuple(q)], fill=tuple(fill), width=w)

def wh_at(st, b):
    for i in range(len(st) - 1):
        b0, b1 = st[i][0], st[i + 1][0]
        if b0 >= b >= b1:
            t = 0 if b0 == b1 else (b0 - b) / (b0 - b1)
            return st[i][4] + (st[i + 1][4] - st[i][4]) * t
    return st[-1][4]

def shrink(pts, k):
    cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
    return [(cx + (p[0] - cx) * k, cy + (p[1] - cy) * k) for p in pts]

GLASS = (26, 38, 56); GLASS2 = (70, 98, 132)

def paint(A, car):
    st = A.st; sty = car['sty']; base = sty['base']
    rng = np.random.default_rng(abs(hash(car['name'])) % (2**32))
    d = A.d
    # ---- base paint with a light vertical gradient on the sides
    for y in range(32):
        k = 1.10 - 0.30 * y / 31.0
        d.line([(0, y), (95, y)], fill=tuple(int(min(255, c * k)) for c in base))
    d.rectangle([96, 0, 127, 55], fill=tuple(base)); d.rectangle([0, 32, 95, 63], fill=tuple(base))
    # ---- rust / grime (before decals so decals stay readable)
    arr = np.array(A.img).astype(np.float64)
    if sty['rust'] > 0:
        n = gaussian_filter(rng.standard_normal((64, 128)), 1.6); n /= n.std() + 1e-9
        m = n > (1.9 - 0.9 * sty["rust"])
        rustc = np.array([142, 78, 38]) if sum(base) > 200 else np.array([92, 52, 30])
        arr[..., :3][m] = rustc * (0.75 + 0.5 * rng.random((m.sum(), 1)))
    gr = sty['grime']
    n2 = gaussian_filter(rng.standard_normal((64, 128)), 2.2); n2 /= n2.std() + 1e-9
    arr[..., :3] *= (1 - 0.10 * gr * np.clip(n2, -1, 1))[..., None]
    low = np.zeros((64, 128)); low[22:32, :96] = np.linspace(0, 1, 10)[:, None] * 0.28 * gr    # dirty sills
    arr[..., :3] *= (1 - low)[..., None]
    A.img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA"); A.d = d = ImageDraw.Draw(A.img)

    # ---- dark hood (Evo): top region + front elevation untouched
    if sty['hood']:
        col, b0, b1 = sty['hood']
        A.poly([A.top(-A.Am * .9, b1), A.top(A.Am * .9, b1), A.top(A.Am * .9, b0), A.top(-A.Am * .9, b0)], col)
    # ---- stripes along the top
    for col, spans in sty['stripes']:
        for a0, a1 in spans:
            A.poly([A.top(a0, A.Bm - .05), A.top(a1, A.Bm - .05), A.top(a1, -A.Bm + .05), A.top(a0, -A.Bm + .05)], col)
    # ---- side stripes
    for col, y0, y1 in sty['side_stripes']:
        A.poly([A.side(-A.Bm + .05, y0), A.side(A.Bm - .05, y0), A.side(A.Bm - .05, y1), A.side(-A.Bm + .05, y1)], col)
    # ---- flames (Bullet GT): blue tongues sweeping back from the front wheel arch / hood
    if sty['flames']:
        for k in range(5):
            b0 = 1.35 - k * 0.04; yy = .52 - k * .07
            tip = (b0 - 1.25 + k * .07, yy + .03 - k * .01)
            A.poly([A.side(b0, yy + .10), A.side(tip[0], tip[1]), A.side(b0 - .35, yy - .02), A.side(b0, yy - .06)], (50, 120, 255))
        for k in range(4):
            x0 = -.55 + k * .30
            A.poly([A.top(x0, 1.45), A.top(x0 + .12, .95), A.top(x0 + .24, 1.45)], (50, 120, 255))
            A.poly([A.top(-x0, 1.45), A.top(-x0 - .12, .95), A.top(-x0 - .24, 1.45)], (50, 120, 255))
            A.poly([A.top(x0 + .05, 1.45), A.top(x0 + .12, 1.15), A.top(x0 + .19, 1.45)], (190, 220, 255))
    # ---- bed interior (pickups)
    if sty['bed']:
        bf, br = sty['bed']; w = max(t[4] for t in st) - .12
        A.poly([A.top(-w, bf - .06), A.top(w, bf - .06), A.top(w, br + .06), A.top(-w, br + .06)], (46, 44, 42))
        for k in range(1, 7):
            bb = bf + (br - bf) * k / 7.0
            A.line(A.top(-w, bb), A.top(w, bb), (70, 68, 64))
    # ---- glass: greenhouse side windows + windshield / rear window on top region
    for i in range(len(st) - 1):
        p, q = st[i], st[i + 1]
        if p[3] > p[2] + .02 or q[3] > q[2] + .02:
            quad = [A.side(p[0], p[2]), A.side(q[0], q[2]), A.side(q[0], q[3]), A.side(p[0], p[3])]
            A.poly(shrink(quad, .78), GLASS)
            hq = shrink(quad, .78)
            A.line(hq[3], hq[2], GLASS2)
            db = abs(q[3] - p[3]) / max(1e-6, abs(q[0] - p[0]))
            if db > .9:           # steep: windshield / rear window seen from above
                wtop = min(p[5] if p[3] > p[2] + .02 else p[4], q[5] if q[3] > q[2] + .02 else q[4])
                gq = [A.top(-wtop * .92, p[0]), A.top(wtop * .92, p[0]), A.top(wtop * .92, q[0]), A.top(-wtop * .92, q[0])]
                A.poly(gq, GLASS)
                A.line(gq[0], gq[1], GLASS2)
    # ---- doors, handles, wheel arches
    top_z = max(t[2] for t in st)
    for bd in sty['doors']:
        A.line(A.side(bd, .30), A.side(bd, top_z), (14, 14, 16))
        h = A.side(bd - .08, top_z - .12); A.d.rectangle([h[0], h[1], h[0] + 1.5, h[1] + .6], fill=(190, 190, 195))
    for (b, rad, steer) in car['ax']:
        x0, y0 = A.side(b - rad - .06, 2 * rad + .06); x1, y1 = A.side(b + rad + .06, -.02)
        A.d.ellipse([x0, y0, x1, y1], fill=(16, 14, 12))
    # ---- race number
    if sty['number']:
        txt, (nb, ny), (pc, tc) = sty['number']
        c = A.side(nb, ny); r = 4.6
        A.d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=tuple(pc))
        f = ImageFont.truetype(FB, 7); A.d.text((c[0], c[1] + .3), txt, font=f, fill=tuple(tc), anchor="mm")
    # ---- front elevation
    wt = A.st[0][4]; y0f, yzf = A.st[0][1], A.st[0][2]
    def fr(a0, a1, y0, y1, col): A.poly([A.front(a0, y0), A.front(a1, y0), A.front(a1, y1), A.front(a0, y1)], col)
    def bk(a0, a1, y0, y1, col): A.poly([A.back(a0, y0), A.back(a1, y0), A.back(a1, y1), A.back(a0, y1)], col)
    hgt = yzf - y0f
    ft = sty['front']
    if ft == 'pickup':
        fr(-wt, wt, y0f, y0f + hgt * .30, CHROME)                       # chrome bumper
        fr(-wt * .55, wt * .55, y0f + hgt * .38, yzf - hgt * .08, (24, 24, 26))   # grille
        for k in range(-3, 4): fr(k * wt * .15 - .02, k * wt * .15 + .02, y0f + hgt * .40, yzf - hgt * .10, CHROME)
        fr(-wt * .95, -wt * .62, y0f + hgt * .45, yzf - hgt * .12, (250, 240, 190)); fr(wt * .62, wt * .95, y0f + hgt * .45, yzf - hgt * .12, (250, 240, 190))
    elif ft == 'coupe':
        fr(-wt, wt, y0f, y0f + hgt * .22, CHROME)
        fr(-wt * .5, wt * .5, y0f + hgt * .35, y0f + hgt * .72, (20, 20, 22))
        for sgn in (-1, 1):
            c = A.front(sgn * wt * .72, y0f + hgt * .62); r = 3.4
            A.d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=(250, 238, 180), outline=(30, 30, 30))
    elif ft == 'evo':
        fr(-wt * .8, wt * .8, y0f + hgt * .08, y0f + hgt * .55, (18, 18, 20))
        fr(-wt * .35, wt * .35, y0f + hgt * .55, y0f + hgt * .85, (24, 24, 26))
        for sgn in (-1, 1): fr(min(sgn * wt * .98, sgn * wt * .6), max(sgn * wt * .98, sgn * wt * .6), y0f + hgt * .58, y0f + hgt * .92, (240, 244, 255))
    elif ft == 'truck':
        fr(-wt * .8, wt * .8, y0f + hgt * .12, yzf - hgt * .05, CHROME)
        for k in range(-5, 6): fr(k * wt * .14 - .015, k * wt * .14 + .015, y0f + hgt * .14, yzf - hgt * .07, (40, 40, 44))
        for sgn in (-1, 1):
            fr(min(sgn * wt * .98, sgn * wt * .83), max(sgn * wt * .98, sgn * wt * .83), y0f + hgt * .30, y0f + hgt * .70, (250, 240, 190))
    else:   # wedge
        fr(-wt * .9, wt * .9, y0f, y0f + hgt * .55, (22, 22, 26))
        for sgn in (-1, 1): fr(min(sgn * wt * .95, sgn * wt * .55), max(sgn * wt * .95, sgn * wt * .55), y0f + hgt * .6, yzf - 0.01, (230, 240, 255))
    # ---- back elevation
    bt = A.st[-1]; wb_, y0b, yzb = bt[4], bt[1], bt[2]; hb = yzb - y0b
    kind = sty['back']
    bk(-wb_, wb_, y0b, y0b + hb * .25, CHROME if kind in ('pickup', 'truck', 'coupe') else (30, 30, 34))
    if kind == 'truck':
        bk(-wb_ * .9, wb_ * .9, y0b + hb * .3, yzb, (30, 30, 32))
    for sgn in (-1, 1):
        bk(min(sgn * wb_ * .9, sgn * wb_ * .5), max(sgn * wb_ * .9, sgn * wb_ * .5), y0b + hb * .45, y0b + hb * .85, (214, 30, 28))
    bk(-wb_ * .22, wb_ * .22, y0b + hb * .32, y0b + hb * .52, (236, 236, 214))
    # ---- swatches + subtle noise
    d.rectangle([96, 56, 111, 63], fill=(255, 255, 255)); d.rectangle([112, 56, 127, 63], fill=(0, 0, 0))
    arr = np.array(A.img).astype(np.float64)
    arr[:56, :, :3] *= (1 + 0.03 * rng.standard_normal((56, 128)))[..., None]
    arr[56:, :96, :3] *= (1 + 0.03 * rng.standard_normal((8, 96)))[..., None]
    A.img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")

def u16(p): return int(round(p * 16))

def emit(idx, car, A):
    st = A.st; n = len(st)
    rows = []
    for (b, yb, yz, yr, wh, wr) in st:
        hasg = yr > yz + 0.01
        wt = wr if hasg else wh
        su, svb = A.side(b, yb); _, svz = A.side(b, yz); _, svr = A.side(b, yr)
        tuL, tvv = A.top(-wt, b); tuR, _ = A.top(wt, b)
        rows.append("{%d,%d,%d,%d,%d,%d, %d,%d,%d,%d,%d,%d,%d}" % (
            round(b * 4096), round(yb * 4096), round(yz * 4096), round(yr * 4096), round(wh * 4096), round(wr * 4096),
            u16(su), u16(svb), u16(svz), u16(svr), u16(tuL), u16(tuR), u16(tvv)))
    axs = []
    for (b, rad, steer) in car['ax']:
        axs.append("{%d,%d,%d,%d}" % (round(b * 4096), round(rad * 4096), round((wh_at(st, b) - .09) * 4096), steer))
    xbs = ["{%d,%d,%d,%d,%d,%d,%d,%d,%d}" % (round(a0 * 4096), round(a1 * 4096), round(y0 * 4096), round(y1 * 4096),
           round(b0 * 4096), round(b1 * 4096), c[0], c[1], c[2]) for (a0, a1, y0, y1, b0, b1, c) in car['boxes']]
    s0, sl = st[0], st[-1]
    fuL = A.front(-s0[4], s0[1])[0]; fuR = A.front(s0[4], s0[1])[0]; fvb = A.front(0, s0[1])[1]; fvz = A.front(0, s0[2])[1]
    buL = A.back(-sl[4], sl[1])[0]; buR = A.back(sl[4], sl[1])[0]; bvb = A.back(0, sl[1])[1]; bvz = A.back(0, sl[2])[1]
    L = max(abs(t[0]) for t in st); roof = max(t[3] for t in st)
    cr = max(1.35, L * SCALE * 0.72)
    camD = L * SCALE + 3.2 + max(0.0, roof - 1.5) * 1.2; camY = 1.8 + max(0.0, roof - 1.5) * 1.6 + roof * 0.4
    t = car['stats']
    return ('  { "%s", %d, "%s", %.1ff,%.1ff,%.1ff,%.1ff,%.1ff,%.1ff, %d,%d,%d,\n    %d, {%s},\n    %d, {%s}, %d,\n    %d, {%s},\n'
            '    %d,%d,%d,%d, %d,%d,%d,%d, %.2ff, %.2ff,%.2ff,%.2ff, TX_CAR%d }'
            % (car['name'], car['cls'], car['drive'], t[0], t[1], t[2], t[3], t[4], t[5], car['ui'][0], car['ui'][1], car['ui'][2],
               n, ",".join(rows), len(axs), ",".join(axs), round(car['ww'] * 4096), len(xbs), ",".join(xbs),
               u16(fuL), u16(fuR), u16(fvb), u16(fvz), u16(buL), u16(buR), u16(bvb), u16(bvz), cr, camD, camY, roof, idx))

def build(add):
    parts = []
    for idx, car in enumerate(CARS):
        A = Atlas(car); paint(A, car)
        rgb = np.array(A.img.convert("RGB")); alpha = np.ones((64, 128), bool)
        add("car%d" % idx, (rgb, alpha))
        parts.append(emit(idx, car, A))
    hdr = ('/* AUTO-GENERATED by tools/carspec.py - do not edit by hand. */\n#pragma once\n'
           '#define MAXST 9\n#define MAXAX 3\n#define MAXXB 8\n#define NCARDEF %d\n'
           'typedef struct { short b,yb,yz,yr,wh,wr; short su,svb,svz,svr,tuL,tuR,tvv; } Station;\n'
           'typedef struct { short b,rad,wa,steer; } Axle;\n'
           'typedef struct { short a0,a1,y0,y1,b0,b1; unsigned char r,g,b; } XBox;\n'
           'typedef struct {\n  const char *name; int cls; const char *drive;\n  float top,acc,han,str,wgt,nit;\n  unsigned char r,g,b;\n'
           '  int nst; Station st[MAXST];\n  int nax; Axle ax[MAXAX]; short ww;\n  int nxb; XBox xb[MAXXB];\n'
           '  short fuL,fuR,fvb,fvz,buL,buR,bvb,bvz;\n  float cr, camD, camY, roof;\n  int tex;\n} CarDef;\n'
           'static const char *className[4] = { "DERBY", "RACE", "STREET", "BONUS" };\n'
           'static const CarDef carDefs[NCARDEF] = {\n' % len(CARS)) + ",\n".join(parts) + "\n};\n"
    return hdr
