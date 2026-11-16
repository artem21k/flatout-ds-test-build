"""Body mesh generator: smooth profile curves (metres) -> stations with 6 rings -> game data.
Axes (metres): b = forward (+front), a = sideways (+right), y = up.   1 model unit (mu) = 1.55 m.
Rings per station (right side, mirrored for the left):
   0 sill   1 lower flank   2 hip (widest)   3 shoulder / beltline   4 top edge (hood edge, roof edge, bed wall)   5 crown (a = 0)"""
import numpy as np
from scipy.interpolate import PchipInterpolator

MU = 1.0 / 1.55
NR = 6

def curve(pts):
    pts = sorted(pts)
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    if len(pts) == 1: return lambda b: np.full_like(np.asarray(b, float), ys[0])
    f = PchipInterpolator(xs, ys, extrapolate=True)
    lo, hi = xs[0], xs[-1]
    return lambda b: f(np.clip(b, lo, hi))

def build(car):
    """returns stations front->back: dict(b, x[6], y[6], sg, tg, bed, lod) in metres"""
    top = curve(car['top']); belt = curve(car['belt']); sill = curve(car['sill'])
    width = curve(car['width']); wtop = curve(car['wtop']); crown = curve(car.get('crown', [(-9, .04), (9, .04)]))
    wsb, rf, rr, rwb = car['gh']                     # windshield base, roof front, roof rear, rear-window base
    F, Rr = car['front'], car['rear']
    key = {F: 3, Rr: 3, wsb: 3, rf: 3, rr: 3, rwb: 3}
    for b in (F - .05, F - .14, F - .30, Rr + .05, Rr + .14, Rr + .30): key.setdefault(round(b, 3), 1)
    for (ab, R, tr, st) in car['axles']:
        for d, l in ((-1.2, 2), (-.7, 1), (0.0, 2), (.7, 1), (1.2, 2)): key.setdefault(round(ab + d * R, 3), l)
    for b in car.get('extra_b', []): key.setdefault(b, 1)
    bs = sorted(key, reverse=True)
    must = set([F, Rr, wsb, rf, rr, rwb] + list(car.get('must_b', [])))
    out = []                                         # (b, lod)
    for b in bs:
        if out and out[-1][0] - b < 0.06:
            if b in must and out[-1][0] not in must: out[-1] = (b, key[b])
            elif b in must and out[-1][0] in must: out.append((b, key[b]))
            continue
        out.append((b, key[b]))
    gap = car.get('maxgap', .5)
    res = []
    for k, (b, l) in enumerate(out):
        res.append((b, l))
        if k + 1 < len(out):
            g = b - out[k + 1][0]
            n = int(g / gap)
            for j in range(1, n + 1):
                res.append((b - g * j / (n + 1), 0))
    # lod: 2 = always, 1 = medium and up, 0 = only highest
    stations = []
    for b, l in res:
        b = float(b)
        yb = float(sill(b)); ys = float(belt(b)); yc = float(top(b)); w = float(width(b)); wt = float(wtop(b)); cr = float(crown(b))
        for (ab, R, tr, st) in car['axles']:
            d = abs(b - ab); ra = R * car.get('arch', 1.12)
            if d < ra:
                ya = car['axle_y'] + (ra * ra - d * d) ** .5 * car.get('arch_k', .98)
                yb = max(yb, min(ya, ys - .12))
        for e in (F, Rr):                                     # round the plan-view corners at the nose and the tail
            dd = abs(b - e)
            if dd < .32:
                f = .80 + .20 * (dd / .32) ** .7; w *= f; wt *= f
        if rf >= b >= rr: wt *= car.get('roof_k', .93)         # tumblehome of the greenhouse
        sg = (wsb >= b >= rwb)
        ye = yc - cr
        h = ys - yb
        x = [w * car.get('sill_in', .76), w * .955, w, w * car.get('sh_in', .93), wt, 0.0]
        y = [yb, yb + .30 * h, yb + .68 * h, ys, max(ye, ys + .004), yc]
        stations.append(dict(b=b, x=x, y=y, sg=sg, tg=False, bed=False, lod=l))
    if 'sg_range' in car:
        for s in stations: s['sg'] = bool(car['sg_range'][0] >= s['b'] >= car['sg_range'][1])
    for i in range(len(stations) - 1):
        mid = .5 * (stations[i]['b'] + stations[i + 1]['b'])
        stations[i]['tg'] = bool((rf <= mid <= wsb) or (car.get('rear_glass', True) and rwb <= mid <= rr))
    # open load bed: ring 3 = outer top of the wall, ring 4 = inner wall foot, ring 5 = floor centre
    bed = car.get('bed')
    if bed:
        b0, b1, floor, inner = bed
        for s in stations:
            if b1 - 1e-6 <= s['b'] <= b0 + 1e-6:
                wo = s['x'][3]
                s['x'][4] = min(wo - .05, inner); s['y'][4] = floor + .02; s['y'][5] = floor
                s['bed'] = True
    if 'tweak' in car: car['tweak'](stations)
    return stations

def ranges(car, st):
    """segment index ranges for the parts that can fall off (indices into the station list, [a,b))"""
    wsb, rf, rr, rwb = car['gh']
    n = len(st)
    def first(pred):
        for i in range(n):
            if pred(st[i]['b']): return i
        return n
    # hood: from the nose to the windshield base ; trunk: from the rear window base to the tail ; doors: windshield base .. rear of cabin
    hA = 1; hB = first(lambda b: b <= wsb + 1e-6)
    tA = first(lambda b: b <= rwb + 1e-6); tB = n - 1
    dA = first(lambda b: b <= car.get('door', (wsb - .05, rr + .15))[0] + 1e-6); dB = first(lambda b: b <= car.get('door', (wsb - .05, rr + .15))[1] + 1e-6)
    return dict(hoodA=hA, hoodB=max(hB, hA + 1), trunkA=tA, trunkB=tB, doorA=dA, doorB=max(dB, dA + 1))

def to_mu(stations):
    return [dict(b=s['b'] * MU, x=[v * MU for v in s['x']], y=[v * MU for v in s['y']], sg=s['sg'], tg=s['tg'], bed=s['bed'], lod=s['lod']) for s in stations]
