#!/usr/bin/env python3
"""Reshape cars in the ALREADY GENERATED cars.h / textures.h (the generator of the current
cars.h is not in the repo, so the data is patched directly).  Run it ONCE on the ORIGINAL files.

  python3 tune_cars.py <dir with original cars.h, textures.h, main.c, carshapes.py> <out dir>

  all cars except INSETTA : a bit wider
  BULLET GT, MALICE       : shorter, taller cabin, front bumper removed, thin grey rear bumper
  BULLET GT               : round front lights (2 per side), Dodge-style rear lights, tiny blue flames on the hood nose
  MALICE                  : rear light recesses, no front lights
  ROAMER / NEVADA / CANYON: shorter hood, cabin moved forward, slightly shorter bed overhang
  ROAMER                  : headache rack (2 posts + crossbar) behind the cab
  CANYON                  : rust removed
  main.c                  : firewall polygon closes the engine bay when the hood is gone
"""
import re, sys, os
import numpy as np
sys.path.insert(0, sys.argv[1])
import carshapes as CS

SRC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
MU, U = 1.55, 4096.0
KW = 1.05                      # width factor (all cars but INSETTA)
SKIP_WIDEN = {'INSETTA'}


def m2u(m): return int(round(m / MU * U))
def u2m(u): return u / U * MU


def ext_interp(x, xp, fp):
    x = np.asarray(x, float)
    y = np.interp(x, xp, fp)
    y = np.where(x < xp[0], fp[0] + (x - xp[0]), y)
    return np.where(x > xp[-1], fp[-1] + (x - xp[-1]), y)


# ================================================================ cars.h parsing
txt = open(os.path.join(SRC, 'cars.h')).read()
i0 = txt.index('static const CarDef carDefs')
head, body = txt[:i0], txt[i0:]
m = re.match(r'(static const CarDef carDefs\[NCARDEF\] = \{\n)(.*)(\n\};\s*)$', body, re.S)
pre, ents, post = m.group(1), m.group(2), m.group(3)
raw = re.split(r',\n(?=  \{ ")', ents)


def parse(e):
    s = re.sub(r'TX_CAR(\d)', r'"TX_CAR\1"', e.strip())
    s = re.sub(r'(\d(?:\.\d+)?)f\b', r'\1', s)
    return eval(s.replace('{', '[').replace('}', ']'))


def fmt(c):
    def l(v): return '{' + ','.join(str(x) for x in v) + '}'
    def l2(v): return '{' + ','.join(l2(x) if isinstance(x, list) else str(x) for x in v) + '}'
    st = ',\n      '.join('{%d,%s,%s,%d,%d}' % (s[0], l(s[1]), l(s[2]), s[3], s[4]) for s in c['st'])
    ax = ','.join(l(a) for a in c['ax'])
    xb = ','.join(l(x) for x in c['xb'])
    h = c['hdr']
    return ('  { "%s", %d, "%s", %.1ff,%.1ff,%.1ff,%.1ff,%.1ff,%.1ff, %d,%d,%d,\n'
            '    %d, {%s},\n    %d, {%s}, %d,\n    %d, {%s},\n    %d, {%s},\n    %s,\n    {%s},\n'
            '    %.2ff, %.2ff,%.2ff,%.2ff, %s }') % (
        h[0], h[1], h[2], *h[3:9], *h[9:12], len(c['st']), st, len(c['ax']), ax, c['ww'], len(c['xb']), xb,
        len(c['eq']), ','.join(l2(q) for q in c['eq']), ','.join(str(v) for v in c['ranges']),
        ','.join(l(u) for u in c['uv']), *c['tail'], c['tex'])


cars = []
for e in raw:
    p = parse(e)
    assert p[12] == len(p[13]) and p[14] == len(p[15]) and p[17] == len(p[18]) and p[19] == len(p[20]), p[0]
    cars.append(dict(hdr=p[:12], st=p[13], ax=p[15], ww=p[16], xb=p[18], eq=p[20], ranges=p[21:27], uv=p[27],
                     tail=p[28:32], tex=p[32], name=p[0], idx=len(cars)))
by = {c['name']: c for c in cars}
for c in cars:
    assert [u[0] for u in c['uv']] == [768, 1792, 384, 1152] and [u[2] for u in c['uv']] == [508, 448, 1020, 1020], c['name']

# ================================================================ length / height maps (metres)
def b_map_coupe(shape):
    """GT / MALICE: shorter overhangs (x0.8) and wheelbase (x0.75); wheel-arch zones stay unscaled"""
    (fa, R, _, _), (ra, _, _, _) = shape['axles']
    z = 1.3 * R
    front, rear = shape['front'], shape['rear']
    old = [rear, ra - z, ra + z, fa - z, fa + z, front]
    mid = (fa - z + ra + z) / 2; half = 0.75 * (fa - z - ra - z) / 2
    fi, ri = mid + half, mid - half
    new = [ri - 2 * z - 0.8 * ((ra - z) - rear) * 1.0, ri - 2 * z, ri, fi, fi + 2 * z, fi + 2 * z + 0.8 * (front - (fa + z))]
    new[0] = ri - 2 * z - 0.8 * ((ra - z) - rear)
    return old, new


def b_map_pickup(shape):
    """hood and front overhang shorter, cab moved forward, bed overhang a bit shorter"""
    (fa, R, _, _), (ra, _, _, _) = shape['axles']
    z = 1.2 * R
    wsb, _, _, rwb = shape['gh']
    front, rear = shape['front'], shape['rear']
    s = min(0.12, 0.55 * (fa - z - wsb))
    old = [rear, ra - z, ra + z, rwb, wsb, fa - z, fa + z, front]
    new = [ra - z - 0.80 * ((ra - z) - rear), ra - z, ra + z, rwb + s, wsb + s, fa - z, fa + z,
           fa + z + 0.55 * (front - (fa + z))]
    return old, new


def y_map_raise(peak, up=0.18):
    return [0, .75, 1.00, 1.10, peak, peak + 2.7], [0, .75, 1.04, 1.14, peak + up, peak + up + 2.7]


PLAN = {
    'BULLET GT': (b_map_coupe(CS.GT), y_map_raise(1.33)),
    'MALICE': (b_map_coupe(CS.MALICE), y_map_raise(1.37)),
    'ROAMER': (b_map_pickup(CS.ROAMER), None),
    'NEVADA': (b_map_pickup(CS.NEVADA), None),
    'CANYON': (b_map_pickup(CS.CANYON), None),
}

# ================================================================ geometry
info = {}
for c in cars:
    name = c['name']
    old_uv = [list(u) for u in c['uv']]
    L_old = max(abs(s[0]) for s in c['st']) / U
    roof_old_m = max(max(s[2]) for s in c['st']) / U * MU
    plan = PLAN.get(name)
    Gb = Fy = None
    if plan:
        (bo, bn), yp = plan
        Gb = (lambda x, bo=bo, bn=bn: ext_interp(x, bo, bn), lambda x, bo=bo, bn=bn: ext_interp(x, bn, bo))
        if yp:
            yo, yn = yp
            Fy = (lambda x, yo=yo, yn=yn: ext_interp(x, yo, yn), lambda x, yo=yo, yn=yn: ext_interp(x, yn, yo))
        for s in c['st']:
            s[0] = m2u(float(Gb[0](u2m(s[0]))))
            if Fy: s[2] = [m2u(float(Fy[0](u2m(v)))) for v in s[2]]
        c['ax'] = [[m2u(float(Gb[0](u2m(a[0])))), *a[1:]] for a in c['ax']]
        for x in c['xb']:
            x[4] = m2u(float(Gb[0](u2m(x[4])))); x[5] = m2u(float(Gb[0](u2m(x[5]))))
            if Fy: x[2] = m2u(float(Fy[0](u2m(x[2])))); x[3] = m2u(float(Fy[0](u2m(x[3]))))
    if name not in SKIP_WIDEN:
        for s in c['st']: s[1] = [round(v * KW) for v in s[1]]
        c['ax'] = [[a[0], a[1], round(a[2] * KW), a[3]] for a in c['ax']]
        for x in c['xb']: x[0] = round(x[0] * KW); x[1] = round(x[1] * KW)
        for q in c['eq']:
            for pt in q[0]: pt[0] = round(pt[0] * KW)
        for r in (1, 2, 3): c['uv'][r][1] = round(c['uv'][r][1] / KW)
    L_new = max(abs(s[0]) for s in c['st']) / U
    roof_new = max(max(s[2]) for s in c['st']) / U
    if plan:
        c['tail'] = [max(1.35, L_new * 1.35 * 0.72), L_new * 1.35 + 3.2 + max(0.0, roof_new - 1.5) * 1.2,
                     1.8 + max(0.0, roof_new - 1.5) * 1.6 + roof_new * 0.4, roof_new]
        # atlas projections follow the new extents so the atlas keeps filling its regions
        Bo, Bn = L_old + 0.15, L_new + 0.15
        vk_side = -old_uv[0][3] / 16.0
        Ym_old = (old_uv[0][2] / 16.0 - 0.25) / vk_side
        Ym_new = float(Fy[0](Ym_old * MU)) / MU if Fy else Ym_old
        c['uv'][0][1] = round(c['uv'][0][1] * Bo / Bn)                 # side u(b)
        c['uv'][1][3] = -round(-old_uv[1][3] * Bo / Bn)                # top v(b)
        for r in (0, 2, 3): c['uv'][r][3] = -round(-old_uv[r][3] * Ym_old / Ym_new)
    info[name] = dict(old_uv=old_uv, Gb=Gb, Fy=Fy)


# ================================================================ bumpers, rack (boxes)
def thin_rear_bumper(c):
    s = c['st'][-1]
    B = min(st[0] for st in c['st'])
    ah = int(0.80 * max(s[1]))
    y0 = m2u(0.42); y1 = m2u(0.56)
    return [-ah, ah, y0, y1, B - m2u(0.035), B + m2u(0.07), 104, 104, 110]


for n in ('BULLET GT', 'MALICE'):
    c = by[n]
    c['xb'] = [thin_rear_bumper(c)]          # front chrome bumper removed, rear one thin, grey, sunk into the body

r = by['ROAMER']
Gb_r = info['ROAMER']['Gb'][0]
rb0, rb1 = m2u(float(Gb_r(-0.84))), m2u(float(Gb_r(-0.78)))
bed_y0, top_y = m2u(0.85), m2u(1.52)
ap, aq = round(m2u(0.68) * KW), round(m2u(0.74) * KW)
GREY = (60, 60, 64)
r['xb'] += [[-aq, -ap, bed_y0, top_y, rb0, rb1, *GREY], [ap, aq, bed_y0, top_y, rb0, rb1, *GREY],
            [-aq, aq, m2u(1.45), top_y, rb0, rb1, *GREY]]
assert len(r['xb']) <= 6
head = head.replace('#define MAXXB 4', '#define MAXXB 6')

# ================================================================ textures
tx = open(os.path.join(SRC, 'textures.h')).read()


def get_atlas(k):
    mt = re.search(r'(static const unsigned short tex_car%d\[8192\][^{]*\{\n)(.*?)(\n\};)' % k, tx, re.S)
    v = np.array([int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', mt.group(2))], dtype=np.uint32)
    assert len(v) == 8192
    return mt, v.reshape(64, 128)


REG = {'side': (0, 96, 0, 32), 'top': (96, 128, 0, 56), 'front': (0, 48, 32, 64), 'back': (48, 96, 32, 64)}


def split(A):
    return [((A & 31)).astype(float), (((A >> 5) & 31)).astype(float), (((A >> 10) & 31)).astype(float)]


def bil(ch, x, y):
    x = np.clip(x - 0.5, 0, 127); y = np.clip(y - 0.5, 0, 63)
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    x1 = np.minimum(x0 + 1, 127); y1 = np.minimum(y0 + 1, 63)
    fx = x - x0; fy = y - y0
    return ch[y0, x0] * (1 - fx) * (1 - fy) + ch[y0, x1] * fx * (1 - fy) + ch[y1, x0] * (1 - fx) * fy + ch[y1, x1] * fx * fy


def dil(m, n=1):
    for _ in range(n):
        p = np.pad(m, 1)
        m = p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
    return m


def inpaint(NEW, reg, maskfn, grow=1, protect=None):
    """replace masked pixels by the median of the unmasked neighbourhood"""
    x0, x1, y0, y1 = REG[reg]
    for _ in range(4):
        rr = [ch[y0:y1, x0:x1] * 8 for ch in NEW]
        mask = maskfn(*rr)
        if not mask.any(): return
        if grow:
            pm = np.pad(mask, 1)
            mask = pm[1:-1, 1:-1] | pm[:-2, 1:-1] | pm[2:, 1:-1] | pm[1:-1, :-2] | pm[1:-1, 2:]
        bad = mask.copy()
        if protect is not None:
            pr_ = protect(*rr); mask = mask & ~pr_; bad = mask | pr_
        ys, xs = np.nonzero(mask)
        for y, x in zip(ys, xs):
            for rad in (3, 5, 8):
                ya, yb, xa, xb_ = max(0, y - rad), min(y1 - y0, y + rad + 1), max(0, x - rad), min(x1 - x0, x + rad + 1)
                ok = ~bad[ya:yb, xa:xb_]
                if ok.sum() >= 4:
                    for k in range(3):
                        NEW[k][y0 + y, x0 + x] = np.median(NEW[k][y0 + ya:y0 + yb, x0 + xa:x0 + xb_][ok])
                    break
        # the pixels filled in this pass count as good for the next one
        for k in range(3): pass


def fillrect(NEW, reg, u0, u1, v0, v1, col, minpx=0):
    x0, x1, y0, y1 = REG[reg]
    if v1 - v0 < minpx: c = (v0 + v1) / 2; v0, v1 = c - minpx / 2, c + minpx / 2
    for y in range(y0, y1):
        for x in range(x0, x1):
            if u0 <= x + .5 <= u1 and v0 <= y + .5 <= v1:
                for k in range(3): NEW[k][y, x] = col[k] / 8.0


def disc(NEW, reg, cu, cv, rad, col):
    x0, x1, y0, y1 = REG[reg]
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x + .5 - cu) ** 2 + (y + .5 - cv) ** 2 <= rad * rad:
                for k in range(3): NEW[k][y, x] = col[k] / 8.0


def proj(c, reg):
    """final atlas projection of a car: functions metres -> pixels"""
    r = {'side': 0, 'top': 1, 'front': 2, 'back': 3}[reg]
    u0, uk, v0, vk = [v / 16.0 for v in c['uv'][r]]
    return (lambda m: u0 + uk * m / MU), (lambda m: v0 + vk * m / MU)


names = [c['name'] for c in cars]
for c in cars:
    n, k = c['name'], c['idx']
    mt, A = get_atlas(k)
    plan = PLAN.get(n)
    # ---- GT: hood is solid blue in the original atlas -> black like the body
    if n == 'BULLET GT':
        body = int(A[0, 98])
        for rr in range(0, 19):
            for cc in range(96, 128):
                p = int(A[rr, cc]); pr, pb = p & 31, (p >> 10) & 31
                if pb > pr + 8: A[rr, cc] = body
    chan = split(A)
    NEW = [ch.copy() for ch in chan]
    if plan:
        iu = info[n]; ou = iu['old_uv']; nu = c['uv']
        Gi = iu['Gb'][1]; Fi = iu['Fy'][1] if iu['Fy'] else (lambda v: v)

        def rs(reg, fn):
            x0, x1, y0, y1 = REG[reg]
            ys, xs = np.mgrid[y0:y1, x0:x1]
            ox, oy = fn(xs + 0.5, ys + 0.5)
            ox = np.clip(ox, x0 + 0.5, x1 - 0.5); oy = np.clip(oy, y0 + 0.5, y1 - 0.5)
            for q in range(3): NEW[q][y0:y1, x0:x1] = bil(chan[q], ox, oy)

        def side(cx, cy):
            b_new = (cx - nu[0][0] / 16.0) / (nu[0][1] / 16.0) * MU
            y_new = (nu[0][2] / 16.0 - cy) / (-nu[0][3] / 16.0) * MU
            b_old = Gi(b_new) / MU; y_old = Fi(y_new) / MU
            return ou[0][0] / 16.0 + ou[0][1] / 16.0 * b_old, ou[0][2] / 16.0 - (-ou[0][3] / 16.0) * y_old

        def top(cx, cy):
            # a is only rescaled by the width factor: u(new) -> u(old) through the (uk_old/uk_new) ratio
            a_new = (cx - nu[1][0] / 16.0) / (nu[1][1] / 16.0) * MU
            b_new = (nu[1][2] / 16.0 - cy) / (-nu[1][3] / 16.0) * MU
            b_old = Gi(b_new) / MU
            ux = ou[1][0] / 16.0 + (ou[1][1] / 16.0) * (a_new / MU)
            return ux if False else cx, ou[1][2] / 16.0 - (-ou[1][3] / 16.0) * b_old

        def yfn(r):
            def f(cx, cy):
                y_new = (nu[r][2] / 16.0 - cy) / (-nu[r][3] / 16.0) * MU
                y_old = Fi(y_new) / MU
                return cx, ou[r][2] / 16.0 - (-ou[r][3] / 16.0) * y_old
            return f
        rs('side', side); rs('top', top)
        if iu['Fy']: rs('front', yfn(2)); rs('back', yfn(3))

    uF = proj(c, 'front'); uB = proj(c, 'back'); uT = proj(c, 'top')
    light = lambda r, g, b: (r > 120) & (g > 120) & (b > 120) & (np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b) < 16)
    cream = lambda r, g, b: (r > 195) & (g > 190) & (b > 150) & (b < r - 14)
    red = lambda r, g, b: (r > 90) & (g < 70) & (b < 70) & (r > g + 50)
    if n == 'BULLET GT':
        # front: remove painted bumper band + square lamps, paint 2 round lamps per side
        inpaint(NEW, 'front', lambda r, g, b: light(r, g, b) | cream(r, g, b))
        for a in (-0.66, -0.40, 0.40, 0.66):
            cu, cv = uF[0](a * KW), uF[1](0.70)
            disc(NEW, 'front', cu, cv, 2.7, (52, 52, 58)); disc(NEW, 'front', cu, cv, 1.7, (250, 240, 205))
        # rear: Dodge-style straight tail lamps, each side broken once, plate stays
        inpaint(NEW, 'back', lambda r, g, b: light(r, g, b) | red(r, g, b), protect=cream)
        for a0, a1 in ((0.42, 0.70), (0.15, 0.34)):
            for sg in (-1, 1):
                ua, ub = sorted((uB[0](sg * a0 * KW), uB[0](sg * a1 * KW)))
                fillrect(NEW, 'back', ua, ub, uB[1](0.77), uB[1](0.67), (236, 36, 36), minpx=2)
        # tiny blue flames on the very nose of the hood (top view, above the nose edge)
        nose_b = max(s[0] for s in c['st']) / U * MU
        row0 = uT[1](nose_b) + 0.1
        for dx, ln in ((-7.0, 3.0), (-3.5, 4.5), (0.0, 6.0), (3.5, 4.5), (7.0, 3.0)):
            cu = 112 + dx
            for yy in range(0, 56):
                rowc = yy + .5
                t = (rowc - row0) / ln
                if 0 <= t < 1:
                    half = 1.6 * (1 - t) + 0.4
                    for xx in range(96, 128):
                        if abs(xx + .5 - cu) <= half:
                            col = (60, 130, 250) if abs(xx + .5 - cu) <= half * 0.55 else (34, 96, 212)
                            for q in range(3): NEW[q][yy, xx] = col[q] / 8.0
    if n == 'MALICE':
        # no front lamps (wrecked derby car), rear: recesses where the lamps would be
        inpaint(NEW, 'front', lambda r, g, b: light(r, g, b) | cream(r, g, b))
        inpaint(NEW, 'back', lambda r, g, b: light(r, g, b) | red(r, g, b), protect=cream)
        for a0, a1 in ((0.42, 0.70), (0.15, 0.34)):
            for sg in (-1, 1):
                ua, ub = sorted((uB[0](sg * a0 * KW), uB[0](sg * a1 * KW)))
                fillrect(NEW, 'back', ua, ub, uB[1](0.77), uB[1](0.67), (10, 10, 12), minpx=2)
    if n == 'CANYON':
        # street car: no rust. rust = orange/brown, not the bright red lamps or cream plate
        rust = lambda r, g, b: (r > 50) & (r < 195) & (r > b - 6) & (r > g + 6)
        bright = lambda r, g, b: dil((r > 195) | ((r > 150) & (g > 150)), 2)
        for reg in REG: inpaint(NEW, reg, rust, grow=1, protect=bright)

    Rn = np.clip(np.rint(NEW[0]), 0, 31).astype(np.uint32)
    Gn = np.clip(np.rint(NEW[1]), 0, 31).astype(np.uint32)
    Bn = np.clip(np.rint(NEW[2]), 0, 31).astype(np.uint32)
    An = (Rn | (Gn << 5) | (Bn << 10) | 0x8000).astype(np.uint32)
    An[56:64, 96:128] = A[56:64, 96:128]          # the white / black swatches stay exactly as they were
    if n in PLAN or n in ('CANYON', 'MALICE'):
        flat = An.ravel()
        arr = '\n'.join('  ' + ','.join('0x%04X' % v for v in flat[i:i + 16]) + ',' for i in range(0, 8192, 16))
        # the offsets of tx are stale after the first replacement -> re-search each time
        mt2, _ = get_atlas(k)
        tx = tx[:mt2.start(2)] + arr + tx[mt2.end(2):]

open(os.path.join(OUT, 'textures.h'), 'w').write(tx)
open(os.path.join(OUT, 'cars.h'), 'w').write(head + pre + ',\n'.join(fmt(c) for c in cars) + post)

# ================================================================ main.c : firewall behind the engine bay
mc = open(os.path.join(SRC, 'main.c')).read()
anchor = "        VX(q[0],0,0); VX(q[1],512,0); VX(q[2],512,512); VX(q[3],0,512);\n"
assert mc.count(anchor) == 1
fw = anchor + (
    "        {                                                      /* firewall: closes the bay towards the cabin so you cannot look into the hollow body */\n"
    "            int yt=P[i1][5][0][1], w2=(P[i1][4][1][0]*9)/10, b2=P[i1][4][0][2]; int g0=34*f100/100, g1=120*f100/100;\n"
    "            if(yt>y+80){\n"
    "                q[0][0]=-w2; q[0][1]=y;  q[0][2]=b2;  q[1][0]=w2; q[1][1]=y;  q[1][2]=b2;\n"
    "                q[2][0]=w2;  q[2][1]=yt; q[2][2]=b2;  q[3][0]=-w2; q[3][1]=yt; q[3][2]=b2;\n"
    "                glColor3b(g0,g0,g0+4); VX(q[0],0,512); VX(q[1],512,512);\n"
    "                glColor3b(g1,g1,g1+6); VX(q[2],512,256); VX(q[3],0,256);\n"
    "            }\n"
    "        }\n")
open(os.path.join(OUT, 'main.c'), 'w').write(mc.replace(anchor, fw))
for c in cars:
    print('%-10s L %.2f m  roof %.2f m  boxes %d' % (c['name'], max(abs(s[0]) for s in c['st']) / U * MU * 2,
                                                    max(max(s[2]) for s in c['st']) / U * MU, len(c['xb'])))
