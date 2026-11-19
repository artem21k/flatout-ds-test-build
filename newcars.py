"""Rebuilds INSETTA (Lancer-Evolution style sedan) and TRUCK (long-nose Peterbilt tractor):
new body profiles (metres, same format as carshapes.py), cars.h entries and painted 128x64 atlases.
Uses carbody.build() so the geometry is generated exactly like the existing models."""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import gaussian_filter
import carbody as CB

MU = CB.MU                                   # model units per metre (1/1.55)
FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def m2u(m): return int(round(m * MU * 4096))


# ============================================================================ shapes (metres)
INSETTA = dict(
    name="INSETTA", front=2.05, rear=-2.28, axle_y=.34,
    axles=[(1.31, .34, .76, 1), (-1.31, .34, .76, 0)], wheel_w=.12, arch=1.16,
    # proportions measured from the in-game side view: short nose, raked windshield, LONG roof, steep short rear window, short deck
    top=[(2.05, .70), (1.98, .80), (1.7, .85), (1.2, .92), (.82, 1.00), (.12, 1.40), (-.40, 1.42), (-1.36, 1.385),
         (-1.70, 1.06), (-2.0, 1.06), (-2.28, .98)],
    belt=[(2.05, .58), (1.8, .74), (1.2, .86), (.82, .95), (0, .97), (-.9, .98), (-1.7, 1.00), (-2.28, .96)],
    sill=[(2.05, .30), (1.8, .24), (1.0, .20), (0, .20), (-1.0, .21), (-1.9, .26), (-2.28, .34)],
    width=[(2.05, .72), (1.9, .82), (1.5, .90), (1.31, .93), (.7, .89), (-.2, .88), (-1.0, .89), (-1.31, .93),
           (-1.8, .92), (-2.1, .88), (-2.28, .80)],
    wtop=[(2.05, .62), (1.8, .76), (1.2, .80), (.82, .78), (.12, .58), (-.4, .57), (-1.36, .60), (-1.7, .80),
          (-2.2, .78)],
    crown=[(-9, .04), (-1.5, .04), (-1.2, .06), (.3, .07), (.9, .05), (9, .05)],
    gh=(.82, .12, -1.36, -1.70), door=(.75, -1.25),
)
INSETTA_BOXES = [   # (a0, a1, y0, y1, b0, b1, colour) in metres
    (-.70, .70, .22, .30, 2.00, 2.12, (236, 236, 240)),                     # white front splitter
    (-.84, .84, 1.22, 1.27, -2.16, -1.86, (222, 224, 230)),                 # wing plank
    (-.40, -.34, 1.06, 1.22, -2.10, -2.02, (40, 40, 46)), (.34, .40, 1.06, 1.22, -2.10, -2.02, (40, 40, 46)),
    (-.88, -.84, 1.12, 1.29, -2.16, -1.84, (222, 224, 230)), (.84, .88, 1.12, 1.29, -2.16, -1.84, (222, 224, 230)),
]

TRUCK = dict(
    name="TRUCK", front=2.82, rear=-3.00, axle_y=.52,
    axles=[(2.32, .52, .98, 1), (-2.32, .52, .98, 0)], wheel_w=.19, arch=1.14,
    # measured from the in-game side view: long low hood, upright short cab, sleeper box, bare chassis (4x2 tractor)
    top=[(2.82, 1.50), (2.74, 1.56), (2.3, 1.52), (1.6, 1.50), (1.15, 1.52), (1.07, 1.56), (.98, 2.04), (.5, 2.07),
         (.42, 2.07), (.38, 2.32), (.30, 2.36), (-.85, 2.37), (-.93, 2.30), (-.97, 1.10), (-1.1, 1.02), (-3.0, .96)],
    belt=[(2.82, 1.38), (2.5, 1.44), (1.1, 1.46), (1.04, 1.62), (.4, 1.66), (.37, 1.70), (-.9, 1.72), (-.96, 1.12),
          (-1.1, .96), (-3.0, .90)],
    sill=[(2.82, .64), (2.4, .58), (1.5, .60), (1.1, .70), (.4, .78), (-.9, .74), (-1.0, .66), (-3.0, .62)],
    width=[(2.82, .62), (2.6, .80), (2.32, 1.06), (1.8, 1.06), (1.3, .90), (1.1, .84), (1.0, 1.12), (.4, 1.14),
           (-.9, 1.14), (-.98, .96), (-1.05, .72), (-2.0, .72), (-3.0, .78)],
    wtop=[(2.82, .52), (2.6, .62), (1.6, .70), (1.15, .72), (1.07, .74), (1.0, 1.06), (.5, 1.06), (-.85, 1.08),
          (-.93, 1.0), (-.97, .56), (-3.0, .56)],
    crown=[(-9, .04), (9, .04)], gh=(1.07, .98, .45, .40), door=(1.05, .40), rear_glass=False, sg_range=(1.07, .38),
)
TRUCK_BOXES = [
    (-1.00, 1.00, .22, .66, 2.82, 3.02, (176, 178, 184)),                                                      # chrome bumper
    (-1.16, -.85, 1.08, 1.14, 1.62, 2.88, (24, 24, 28)), (.85, 1.16, 1.08, 1.14, 1.62, 2.88, (24, 24, 28)),    # front fenders
    (-1.16, -.85, 1.08, 1.14, -2.92, -1.72, (24, 24, 28)), (.85, 1.16, 1.08, 1.14, -2.92, -1.72, (24, 24, 28)),  # rear fenders
    (-1.48, -1.02, .27, .74, -1.37, -.24, (176, 178, 184)), (1.02, 1.48, .27, .74, -1.37, -.24, (176, 178, 184)),  # fuel tanks
    (-1.34, -1.18, .45, 2.98, .15, .39, (186, 188, 194)), (1.18, 1.34, .45, 2.98, .15, .39, (186, 188, 194)),   # exhaust stacks
    (-1.00, -.74, 1.14, 1.57, 1.16, 1.50, (170, 172, 178)), (.74, 1.00, 1.14, 1.57, 1.16, 1.50, (170, 172, 178)),  # air cleaners
    (-.55, .55, .98, 1.04, -1.87, -1.28, (66, 66, 72)),                                                        # fifth wheel plate
    (-1.00, 1.00, .25, .45, -3.20, -3.00, (176, 178, 184)),                                                    # rear bumper
    (-1.12, -.86, .96, 1.08, -2.98, -2.90, (204, 26, 26)), (.86, 1.12, .96, 1.08, -2.98, -2.90, (204, 26, 26)),   # tail lamps
]


# ============================================================================ emit
def build_entry(shape, boxes, hdr, tex, idx):
    st = CB.build(shape)
    rng = CB.ranges(shape, st)
    sts = []
    for s in st:
        fl = (1 if s['sg'] else 0) | (2 if s['tg'] else 0) | (4 if s['bed'] else 0)
        sts.append([m2u(s['b']), [m2u(v) for v in s['x']], [m2u(v) for v in s['y']], s['lod'], fl])
    ax = [[m2u(ab), m2u(R), m2u(tr), stv] for (ab, R, tr, stv) in shape['axles']]
    xb = [[m2u(a0), m2u(a1), m2u(y0), m2u(y1), m2u(b0), m2u(b1), *col] for (a0, a1, y0, y1, b0, b1, col) in boxes]
    L = max(abs(s[0]) for s in sts) / 4096.0
    roof = max(max(s[2]) for s in sts) / 4096.0
    tail = [max(1.35, L * 1.35 * .72), L * 1.35 + 3.2 + max(0.0, roof - 1.5) * 1.2,
            1.8 + max(0.0, roof - 1.5) * 1.6 + roof * .4, roof]
    # atlas projections (identical to the generator's own)
    Bm = max(abs(s[0]) for s in sts) / 4096.0 + .15
    Ym = roof + .10
    Am = max(max(s[1]) for s in sts) / 4096.0 + .12
    uv = [[768, round(47.75 / Bm * 16), 508, -round(31.5 / Ym * 16)],
          [1792, round(15.75 / Am * 16), 448, -round(27.75 / Bm * 16)],
          [384, -round(23.75 / Am * 16), 1020, -round(31.5 / Ym * 16)],
          [1152, round(23.75 / Am * 16), 1020, -round(31.5 / Ym * 16)]]
    return dict(hdr=hdr, st=sts, ax=ax, ww=m2u(shape['wheel_w']), xb=xb, eq=[],
                ranges=[rng['hoodA'], rng['hoodB'], rng['trunkA'], rng['trunkB'], rng['doorA'], rng['doorB']],
                uv=uv, tail=tail, tex=tex, name=hdr[0], idx=idx), (st, Bm, Am, Ym)


# ============================================================================ atlas painting
class Atlas:
    """atlas 128x64: side 0..95 x 0..31 | top 96..127 x 0..55 | front 0..47 x 32..63 | back 48..95 x 32..63"""
    def __init__(s, Bm, Am, Ym):
        s.Bm, s.Am, s.Ym = Bm, Am, Ym
        s.img = Image.new("RGBA", (128, 64), (0, 0, 0, 255)); s.d = ImageDraw.Draw(s.img)

    # metres -> pixel coordinates
    def side(s, b, y): b *= MU; y *= MU; return (0.25 + (b + s.Bm) / (2 * s.Bm) * 95.5, 0.25 + (1 - y / s.Ym) * 31.5)
    def top(s, a, b): a *= MU; b *= MU; return (96.25 + (a + s.Am) / (2 * s.Am) * 31.5, 0.25 + (s.Bm - b) / (2 * s.Bm) * 55.5)
    def front(s, a, y): a *= MU; y *= MU; return (0.25 + (s.Am - a) / (2 * s.Am) * 47.5, 32.25 + (1 - y / s.Ym) * 31.5)
    def back(s, a, y): a *= MU; y *= MU; return (48.25 + (a + s.Am) / (2 * s.Am) * 47.5, 32.25 + (1 - y / s.Ym) * 31.5)

    def poly(s, pts, col): s.d.polygon([tuple(p) for p in pts], fill=tuple(col))
    def line(s, p, q, col, w=1): s.d.line([tuple(p), tuple(q)], fill=tuple(col), width=w)

    def srect(s, b0, b1, y0, y1, col): s.poly([s.side(b0, y0), s.side(b1, y0), s.side(b1, y1), s.side(b0, y1)], col)
    def sbox(s, b0, b1, y0, y1, col):
        (xa, ya), (xb, yb) = s.side(b0, y1), s.side(b1, y0)
        s.d.rectangle([min(xa, xb), min(ya, yb), max(xa, xb), max(ya, yb)], outline=tuple(col))
    def trect(s, a0, a1, b0, b1, col): s.poly([s.top(a0, b0), s.top(a1, b0), s.top(a1, b1), s.top(a0, b1)], col)
    def frect(s, a0, a1, y0, y1, col): s.poly([s.front(a0, y0), s.front(a1, y0), s.front(a1, y1), s.front(a0, y1)], col)
    def brect(s, a0, a1, y0, y1, col): s.poly([s.back(a0, y0), s.back(a1, y0), s.back(a1, y1), s.back(a0, y1)], col)

    def sdisc(s, b, y, r, col): c = s.side(b, y); s.d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=tuple(col))
    def fdisc(s, a, y, r, col, out=None):
        c = s.front(a, y); s.d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=tuple(col), outline=out)


def shrink(pts, k):
    cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
    return [(cx + (p[0] - cx) * k, cy + (p[1] - cy) * k) for p in pts]


GLASS, GLASS2 = (26, 38, 56), (80, 108, 142)


def paint_glass(A, st, k=.86, trim=None, side_only_to=None, side=True):
    """greenhouse side windows and windshield / rear window on the top projection (from the generated stations)"""
    for i in range(len(st) - 1):
        p, q = st[i], st[i + 1]
        if side and p['sg'] and q['sg']:
            quad = [A.side(p['b'], p['y'][3]), A.side(q['b'], q['y'][3]), A.side(q['b'], q['y'][4]), A.side(p['b'], p['y'][4])]
            A.poly(shrink(quad, k), GLASS)
            hq = shrink(quad, k); A.line(hq[3], hq[2], GLASS2)
            if trim: A.d.polygon([tuple(v) for v in shrink(quad, k + .06)], outline=trim)
        if p['tg']:
            wt = min(p['x'][4], q['x'][4]) * .90
            gq = [A.top(-wt, p['b']), A.top(wt, p['b']), A.top(wt, q['b']), A.top(-wt, q['b'])]
            A.poly(gq, GLASS); A.line(gq[0], gq[1], GLASS2)


def finish(A, rng, noise=.03):
    A.d.rectangle([96, 56, 111, 63], fill=(255, 255, 255)); A.d.rectangle([112, 56, 127, 63], fill=(0, 0, 0))
    arr = np.array(A.img).astype(np.float64)
    arr[:56, :, :3] *= (1 + noise * rng.standard_normal((56, 128)))[..., None]
    arr[56:, :96, :3] *= (1 + noise * rng.standard_normal((8, 96)))[..., None]
    return np.clip(arr, 0, 255).astype(np.uint8)


def to555(arr):
    r, g, b = [arr[..., i].astype(np.uint32) for i in range(3)]
    return ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000)


NAVY = (24, 34, 112); WHITE = (236, 238, 243); RED = (200, 36, 40); CHR = (176, 178, 184)


def paint_insetta(st, Bm, Am, Ym):
    A = Atlas(Bm, Am, Ym); d = A.d
    rng = np.random.default_rng(3)
    d.rectangle([0, 0, 127, 63], fill=WHITE)
    # ---- side: white upper body over a navy lower body, red swoosh between them
    line = [(2.35, 1.00), (1.0, .92), (.5, .86), (-.1, .62), (-.6, .55), (-1.2, .60), (-1.7, .78), (-2.35, .95)]
    navy = [A.side(b, y) for b, y in line] + [A.side(-2.35, -.2), A.side(2.35, -.2)]
    A.poly(navy, NAVY)
    redl = [(b, y + .07) for b, y in line]
    A.poly([A.side(b, y) for b, y in line] + [A.side(b, y) for b, y in reversed(redl)], RED)
    A.srect(-1.2, 1.1, .20, .27, (190, 194, 202))                      # silver rocker
    # ---- hood (top): navy with white arc stripes + black vents
    A.trect(-Am / MU, Am / MU, .85, 2.30, NAVY)
    A.trect(-.30, -.22, .90, 2.20, WHITE); A.trect(.22, .30, .90, 2.20, WHITE)
    A.trect(-.36, -.31, .90, 2.20, RED); A.trect(.31, .36, .90, 2.20, RED)
    A.trect(-.19, -.05, 1.05, 1.65, (14, 14, 18)); A.trect(.05, .19, 1.05, 1.65, (14, 14, 18))      # twin vents
    for k in range(5):
        bb = 1.10 + k * .12
        A.trect(-.19, -.05, bb, bb + .03, (50, 54, 62)); A.trect(.05, .19, bb, bb + .03, (50, 54, 62))
    # roof: white with navy stripes at the edges, trunk: white with a navy band
    for a0, a1 in ((-.62, -.50), (.50, .62)): A.trect(a0, a1, .25, -1.30, NAVY)
    A.trect(-.9, .9, -1.45, -2.00, WHITE)
    A.trect(-.9, .9, -2.10, -2.40, NAVY)
    paint_glass(A, st, trim=(210, 214, 222))
    # ---- pillars, doors, handles, arches
    for bd in (-.30, -1.25):
        A.line(A.side(bd, .30), A.side(bd, 1.0), (14, 14, 18))
    for bh in (.05, -.55):
        h = A.side(bh, .86); d.rectangle([h[0], h[1], h[0] + 1.6, h[1] + .7], fill=(60, 62, 70))
    for (ab, R, tr, stv) in INSETTA['axles']:
        x0, y0 = A.side(ab - R * 1.2, 2 * R * 1.05); x1, y1 = A.side(ab + R * 1.2, -.02)
        d.ellipse([x0, y0, x1, y1], fill=(14, 12, 12))
    # ---- sponsors / number (tiny, as in the original livery)
    A.srect(.23, .66, .40, .72, (244, 244, 246))                         # number plate
    f = ImageFont.truetype(FB, 7); c = A.side(.445, .56); d.text((c[0], c[1] + .2), "23", font=f, fill=(16, 16, 20), anchor="mm")
    A.srect(.23, .66, .72, .81, (238, 200, 30))                          # yellow ARTEC banner
    A.srect(1.15, 1.65, .62, .72, (240, 200, 30)); A.srect(1.0, 1.4, .30, .38, (240, 240, 244))   # POLLUX / NEPHI on the fender
    A.srect(-.05, -.30, .45, .72, RED); A.srect(-.05, -.30, .56, .59, (244, 244, 246))               # PEAK block
    A.srect(-.45, -.20, .45, .60, (244, 244, 246))                                                    # G-LITE
    A.srect(-1.85, -1.45, .90, .97, (240, 200, 30)); A.srect(-1.7, -1.2, .96, .99, NAVY)
    # ---- front elevation (nose face)
    wt = st[0]['x'][2] / MU * MU                                          # not used, kept for clarity
    A.frect(-.8, .8, .20, .50, NAVY)
    A.frect(-.8, .8, .20, .28, WHITE)                                      # white lower lip
    A.frect(-.52, .52, .24, .40, (14, 14, 18))                             # lower intake
    A.frect(-.26, .26, .40, .56, (20, 20, 24))                             # grille
    for sg in (-1, 1):
        A.frect(min(sg * .80, sg * .46), max(sg * .80, sg * .46), .40, .56, (16, 16, 20))     # headlamp housings
        A.fdisc(sg * .60, .48, 1.5, (210, 214, 224))
        A.frect(min(sg * .80, sg * .64), max(sg * .80, sg * .64), .22, .30, (240, 220, 120))     # fog lamps
    # ---- back elevation
    A.brect(-.8, .8, .32, .66, NAVY)
    A.brect(-.8, .8, .66, 1.10, WHITE)
    for sg in (-1, 1):
        A.brect(min(sg * .80, sg * .42), max(sg * .80, sg * .42), .70, .88, (150, 24, 30))       # tail lamps
        A.brect(min(sg * .80, sg * .42), max(sg * .80, sg * .42), .70, .74, (236, 236, 240))
        A.brect(min(sg * .50, sg * .38), max(sg * .50, sg * .38), .34, .40, (90, 92, 100))        # exhaust
    A.brect(-.22, .22, .56, .68, (240, 200, 30))                           # plate
    return to555(finish(A, rng, .025))


def paint_truck(st, Bm, Am, Ym):
    A = Atlas(Bm, Am, Ym); d = A.d
    rng = np.random.default_rng(6)
    BLK = (22, 22, 26)
    d.rectangle([0, 0, 127, 63], fill=BLK)
    for y in range(32):                                                    # soft gloss gradient on the sides
        k = 1.35 - .70 * y / 31.0
        d.line([(0, y), (95, y)], fill=tuple(int(min(255, c * k)) for c in BLK))
    # ---- side: hood with chrome trim and the red engine showing, cab door, sleeper window panels
    A.srect(1.50, 2.72, 1.28, 1.34, CHR); A.srect(1.55, 2.45, 1.38, 1.46, (150, 36, 36)); A.srect(1.50, 2.72, 1.20, 1.22, (110, 110, 118))
    A.srect(1.04, 1.16, .70, 1.46, (34, 34, 40))                            # cowl / fender split
    paint_glass(A, st, k=.88, trim=(150, 154, 162))
    for b0, b1, y0, y1 in ((-.20, -.65, 1.35, 1.87), (-.20, -.65, .95, 1.22)):
        A.srect(b0, b1, y0, y1, (8, 8, 12)); A.sbox(b0, b1, y0, y1, (70, 72, 80))
    A.line(A.side(.40, .80), A.side(.40, 1.66), (6, 6, 8)); A.line(A.side(1.05, .80), A.side(1.05, 1.66), (6, 6, 8))
    h = A.side(.52, 1.20); d.rectangle([h[0], h[1], h[0] + 1.6, h[1] + .6], fill=CHR)
    A.srect(-.90, .38, 1.68, 1.71, (60, 60, 68))                            # cab / sleeper trim line
    for (ab, R, tr, stv) in TRUCK['axles']:
        x0, y0 = A.side(ab - R * 1.12, 2 * R * 1.05); x1, y1 = A.side(ab + R * 1.12, -.02)
        d.ellipse([x0, y0, x1, y1], fill=(10, 8, 8))
    # ---- top: hood with a lighter crown line, roof
    A.trect(-.04, .04, 1.07, 2.78, (44, 44, 50))
    A.trect(-.62, .62, .98, .40, (30, 30, 34))
    # ---- front elevation: tall chrome grille with black slats, round lamps
    A.frect(-.52, .52, .72, 2.06, CHR)
    A.frect(-.44, .44, .80, 1.98, (18, 18, 20))
    for k in range(-4, 5): A.frect(k * .10 - .012, k * .10 + .012, .82, 1.96, (110, 112, 120))
    for sg in (-1, 1):
        A.fdisc(sg * .60, 1.40, 2.0, (250, 240, 195), out=(150, 150, 156))
    A.frect(-.30, .30, .74, .82, (60, 60, 66))
    # ---- back elevation: sleeper wall is the visible rear (black with a plate), tail lights on the fenders
    A.brect(-1.0, 1.0, .60, 1.28, (30, 30, 34))
    A.brect(-.60, .60, 1.30, 3.20, (16, 16, 20))
    A.brect(-.20, .20, 1.50, 1.62, (236, 236, 240))
    for sg in (-1, 1):
        A.brect(min(sg * 1.08, sg * .80), max(sg * 1.08, sg * .80), .90, 1.10, (214, 30, 28))
        A.brect(min(sg * 1.08, sg * .80), max(sg * 1.08, sg * .80), .76, .86, (236, 236, 240))
    return to555(finish(A, rng, .02))


# ============================================================================ GT / MALICE: cabin, doors, side flames
def _bez(p0, p1, p2, n=24):
    ts = np.linspace(0, 1, n)[:, None]
    p0, p1, p2 = map(np.array, (p0, p1, p2))
    return (1 - ts) ** 2 * p0 + 2 * (1 - ts) * ts * p1 + ts ** 2 * p2


def paint_gt_side(A, st, shape):
    """GT side: black body with clean blue flame tongues, dark wheel arches, thin silver rocker (supersampled)"""
    S = 4
    BODY = (16, 16, 24)
    sup = Image.new("RGB", (96 * S, 32 * S), BODY); d = ImageDraw.Draw(sup)
    nose, rear = st[0]['b'], st[-1]['b']
    tongues = [((nose - .05, .70), (nose - .8, .96), (nose - 2.0, .97), .20),
               ((nose - .05, .55), (nose - .9, .78), (nose - 2.5, .86), .18),
               ((nose - .10, .45), (nose - 1.0, .55), (nose - 2.8, .66), .14),
               ((nose - .60, .80), (nose - 1.3, .62), (nose - 2.2, .60), .10),
               ((nose - 1.4, .70), (nose - 2.1, .88), (nose - 3.2, .84), .09),
               ((rear + 1.0, .72), (rear + .6, .95), (rear + .15, .86), .08),
               ((nose - 2.4, .55), (nose - 3.1, .70), (nose - 4.1, .62), .06)]
    for (r, c, t, w0) in tongues:
        P = _bez(r, c, t)
        dP = np.gradient(P, axis=0); nrm = np.stack([-dP[:, 1], dP[:, 0]], 1)
        nrm /= (np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9)
        w = (w0 * (1 - np.linspace(0, 1, len(P))) ** .85)[:, None]
        pts = list(P + nrm * w / 2) + list((P - nrm * w / 2)[::-1])
        d.polygon([(A.side(b, y)[0] * S, A.side(b, y)[1] * S) for b, y in pts], fill=(44, 104, 236))
    for (ab, R, tr, stv) in shape['axles']:                               # wheel arches
        x0, y0 = A.side(ab - R * 1.2, 2 * R * 1.05); x1, y1 = A.side(ab + R * 1.2, -.02)
        d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=(14, 12, 12))
    (xa, ya), (xb, yb) = A.side(nose - 1.0, .30), A.side(rear + 1.0, .26)
    d.rectangle([min(xa, xb) * S, ya * S, max(xa, xb) * S, yb * S], fill=(58, 60, 70))               # rocker
    small = sup.resize((96, 32), Image.BOX)
    A.img.paste(small, (0, 0))


def paint_cabin(A, st, shape, body, trim=None, seam=(60, 62, 72), handle=(150, 152, 160)):
    """repaint the greenhouse: clean window glass (side + windshield + rear window), door seams for ONE door, handle"""
    d = A.d
    Ym_m = A.Ym / MU
    for i in range(len(st) - 1):                                           # clear everything above the belt line (side)
        p, q = st[i], st[i + 1]
        A.poly([A.side(p['b'], p['y'][3] - .012), A.side(q['b'], q['y'][3] - .012), A.side(q['b'], Ym_m + .3),
                A.side(p['b'], Ym_m + .3)], body)
    wsb, rf, rr, rwb = shape['gh']
    wt = max(s['x'][4] for s in st) + .03
    A.trect(-wt, wt, wsb + .02, rwb - .04, body)                            # clear the roof / glass band (top view)
    sg = [s_ for s_ in st if s_['sg']]
    if sg:                                                                     # one continuous side window (door glass + quarter glass)
        pts = [A.side(s_['b'] - .02, s_['y'][4] - .035) for s_ in sg] + [A.side(s_['b'] - .02, s_['y'][3] + .03) for s_ in reversed(sg)]
        A.poly(pts, GLASS)
        A.d.polygon([tuple(v) for v in pts], outline=trim if trim else GLASS2)
    paint_glass(A, st, k=.88, trim=trim, side=False)
    d0, d1 = shape['door']
    belt = lambda b: float(np.interp(-b, [-s['b'] for s in st], [s['y'][3] for s in st]))
    top4 = lambda b: float(np.interp(-b, [-s['b'] for s in st], [s['y'][4] for s in st]))
    for b0 in (d0, d1):
        A.line(A.side(b0, .30), A.side(b0, belt(b0) + .01), seam)
    A.line(A.side(d1, belt(d1) + .02), A.side(d1, top4(d1) - .02), (10, 10, 14))        # pillar behind the door
    h = A.side(d1 + .25, .84); d.rectangle([h[0], h[1], h[0] + 1.8, h[1] + .7], fill=handle)
