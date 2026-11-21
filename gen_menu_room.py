#!/usr/bin/env python3
"""Builds menuroom.h: the garage the cars stand in on every menu screen (main menu, track, car select).

Original art made of ~100 coloured polygons + ONE 32x16 concrete floor tile (1 KB of texture VRAM; the
texture memory is almost full, so no big background picture is used).  The reference was a bright
hangar with a dark concrete floor, blue tool cabinets on the left, a red wall on the right and a hazy
open door at the back.

   python3 gen_menu_room.py            -> menuroom.h  (+ menuroom_preview.png)

Edit the room() function to move / recolour things, run again, commit menuroom.h.
World: x right, y up, z towards the camera; the car stands at the origin.
"""
import math, sys
import numpy as np
from PIL import Image

# ------------------------------------------------------------------ concrete floor tile (seamless)
def vnoise(w, h, cell, seed):
    r = np.random.RandomState(seed)
    gw, gh = max(w // cell, 1), max(h // cell, 1)
    g = r.rand(gh, gw)
    out = np.zeros((h, w))
    for y in range(h):
        for x in range(w):
            fx, fy = x / cell, y / cell
            x0, y0 = int(fx) % gw, int(fy) % gh
            x1, y1 = (x0 + 1) % gw, (y0 + 1) % gh
            tx, ty = fx - int(fx), fy - int(fy)
            tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
            out[y, x] = (g[y0, x0] * (1 - tx) + g[y0, x1] * tx) * (1 - ty) + (g[y1, x0] * (1 - tx) + g[y1, x1] * tx) * ty
    return out

def concrete_tile():                       # 32 wide x 16 high (1 KB): one tile = 6 m x 3 m of floor
    n1 = vnoise(32, 16, 16, 1); n2 = vnoise(32, 16, 8, 2); n3 = vnoise(32, 16, 4, 3)
    rnd = np.random.RandomState(5).rand(16, 32)
    img = np.zeros((16, 32, 3))
    for y in range(16):
        for x in range(32):
            v = 216 + 24 * (n1[y, x] - 0.5) + 18 * (n2[y, x] - 0.5) + 12 * (n3[y, x] - 0.5) + 10 * (rnd[y, x] - 0.5)
            if n2[y, x] < 0.10 and n3[y, x] < 0.3: v -= 12          # a few dark stains / cracks
            img[y, x] = (v, v * 0.995, v * 0.985)
    return np.clip(img, 0, 255).astype(np.uint8)

# ------------------------------------------------------------------ the room
Q = []     # each: dict(p=[4 xyz], c=[4 rgb], tex=0 flat / 1 concrete, uv=[4 (u,v) in texels])

def quad(p, c, tex=0, uv=None):
    if isinstance(c[0], (int, float)): c = [c] * 4
    Q.append(dict(p=[tuple(v) for v in p], c=[tuple(v) for v in c], tex=tex, uv=uv or [(0, 0)] * 4))

def shade(c, k): return tuple(int(max(0, min(255, v * k))) for v in c)

def box(x0, x1, y0, y1, z0, z1, col, faces="ftlr"):
    """axis-aligned box; faces: f=front(+z) b=back t=top l=left(-x) r=right(+x)"""
    if "f" in faces: quad([(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)], shade(col,1.0))
    if "b" in faces: quad([(x1,y0,z0),(x0,y0,z0),(x0,y1,z0),(x1,y1,z0)], shade(col,0.7))
    if "t" in faces: quad([(x0,y1,z1),(x1,y1,z1),(x1,y1,z0),(x0,y1,z0)], shade(col,1.12))
    if "l" in faces: quad([(x0,y0,z0),(x0,y0,z1),(x0,y1,z1),(x0,y1,z0)], shade(col,0.82))
    if "r" in faces: quad([(x1,y0,z1),(x1,y0,z0),(x1,y1,z0),(x1,y1,z1)], shade(col,0.88))

def room():
    XL, XR, ZB, ZF, H = -7.4, 7.8, -30.0, 14.0, 14.0
    # ---- floor: dark near the camera, brighter and brighter towards the bright opening
    zs = [ZF, 3.0, -6.0, -15.0, -23.0, ZB]
    fc = [(82,84,90), (92,94,100), (120,122,128), (176,179,186), (236,238,244), (255,255,255)]
    for i in range(len(zs) - 1):
        z0, z1 = zs[i], zs[i + 1]
        u0, u1 = XL / 6.0 * 512, XR / 6.0 * 512
        quad([(XL,0,z0),(XR,0,z0),(XR,0,z1),(XL,0,z1)], [fc[i], fc[i], fc[i+1], fc[i+1]], 1,
             [(u0, z0/3.0*256), (u1, z0/3.0*256), (u1, z1/3.0*256), (u0, z1/3.0*256)])
    # ---- back: pale haze + distant stands / barrier / low buildings
    quad([(XL,0,ZB),(XR,0,ZB),(XR,H,ZB),(XL,H,ZB)], [(240,242,246),(240,242,246),(196,214,238),(196,214,238)])
    box(-7.4, -2.0, 0, 2.2, ZB+0.3, ZB+0.4, (206,210,216), "f")                 # low buildings
    box(-1.0, 3.6, 0, 3.8, ZB+0.3, ZB+0.4, (112,116,124), "f")                  # steel stands
    box(-1.0, 3.6, 3.0, 3.8, ZB+0.3, ZB+0.4, (168,172,180), "f")
    for k in range(5): box(-0.8+k*0.95, -0.7+k*0.95, 0, 3.0, ZB+0.3, ZB+0.4, (70,74,82), "f")
    box(3.8, 7.8, 0, 1.3, ZB+0.3, ZB+0.4, (228,229,232), "f")                    # long white barrier
    box(3.8, 7.8, 1.0, 1.3, ZB+0.3, ZB+0.4, (186,48,44), "f")                    # red top line
    # ---- left wall: concrete above, blue tool cabinets in front
    quad([(XL,0,ZF),(XL,0,ZB),(XL,H,ZB),(XL,H,ZF)], [(120,122,128),(214,216,220),(226,228,232),(150,152,158)])
    cab=(66,92,142)
    quad([(XL+0.05,0,6.5),(XL+0.05,0,-4.5),(XL+0.05,3.3,-4.5),(XL+0.05,3.3,6.5)], [shade(cab,0.62),shade(cab,0.95),shade(cab,1.05),shade(cab,0.72)])
    for y in (0.85, 1.7, 2.55):                                              # drawer lines
        quad([(XL+0.07,y,6.5),(XL+0.07,y,-4.5),(XL+0.07,y+0.07,-4.5),(XL+0.07,y+0.07,6.5)], [(24,34,58)]*2 + [(30,42,70)]*2)
    for z in (4.0, 1.0, -1.6, -3.6):                                         # drawer separators
        quad([(XL+0.07,0,z),(XL+0.07,0,z-0.06),(XL+0.07,3.3,z-0.06),(XL+0.07,3.3,z)], (26,36,62))
    for z in (5.2, 2.5, -0.3, -2.6):                                         # handles
        for y in (0.45, 1.3, 2.15, 2.95):
            quad([(XL+0.1,y,z),(XL+0.1,y,z-0.7),(XL+0.1,y+0.07,z-0.7),(XL+0.1,y+0.07,z)], (190,196,206))
    box(XL, XL+1.2, 3.3, 3.45, -4.5, 6.5, (126,140,168), "ft")                # counter top
    quad([(XL,3.45,6.5),(XL+1.2,3.45,6.5),(XL+1.2,3.45,-4.5),(XL,3.45,-4.5)], (150,160,184))
    # tyre stack
    for k in range(3): box(XL+0.7, XL+2.1, 0.7*k, 0.7*k+0.7, -3.4, -2.0, (24,24,26), "ftr"); \
                       box(XL+0.7, XL+2.1, 0.7*k+0.25, 0.7*k+0.45, -3.4, -2.0, (50,50,54), "")
    # concrete pillar with a red panel
    box(XL, XL+2.0, 0, H, -21.0, -18.8, (186,187,192), "frl")
    quad([(XL+0.1,4.6,-18.78),(XL+1.9,4.6,-18.78),(XL+1.9,9.2,-18.78),(XL+0.1,9.2,-18.78)], [(132,26,30),(132,26,30),(168,40,44),(168,40,44)])
    # ---- right wall: big red wall with black pipes, concrete beyond
    quad([(XR,0,ZF),(XR,0,ZB),(XR,H,ZB),(XR,H,ZF)], [(130,132,138),(214,216,220),(226,228,232),(160,162,168)])
    red=(150,28,32)
    quad([(XR-0.05,0,8.0),(XR-0.05,0,-15.0),(XR-0.05,9.4,-15.0),(XR-0.05,9.4,8.0)], [shade(red,0.52),shade(red,0.98),shade(red,1.12),shade(red,0.62)])
    quad([(XR-0.07,0,8.0),(XR-0.07,0,-15.0),(XR-0.07,0.5,-15.0),(XR-0.07,0.5,8.0)], (56,16,18))
    for (zz, ww) in ((1.6, 0.18), (0.4, 0.18)):                              # two black pipes sweeping down the wall
        pts = [(zz,9.4),(zz-0.6,8.2),(zz-1.6,6.4),(zz-2.0,4.6)]
        for a in range(len(pts)-1):
            (za,ya),(zb,yb) = pts[a], pts[a+1]
            quad([(XR-0.09,ya,za),(XR-0.09,ya,za-ww),(XR-0.09,yb,zb-ww),(XR-0.09,yb,zb)], (14,14,16))
    box(XR-2.2, XR, 0, H, -24.0, -21.8, (180,182,188), "fl")                  # concrete pillar far right

def project(p, cam, look, W, H):
    ex = np.array(cam); lk = np.array(look)
    f = lk - ex; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    d = np.array(p) - ex
    z = d @ f
    fl = 1.0 / math.tan(math.radians(31))
    return (W / 2 + (d @ r) / z * fl / (W / H) * (H / 2), H / 2 - (d @ u) / z * fl * (H / 2), z)

def render(cam, look, name, W=512, Hh=384, tile=None):
    img = np.zeros((Hh, W, 3), np.float32); img[:] = (206, 216, 232)
    zb = np.full((Hh, W), 1e9, np.float32)
    ex = np.array(cam); lk = np.array(look)
    f = lk - ex; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    fl = 1.0 / math.tan(math.radians(31))
    def cs(p):
        d = np.array(p, float) - ex; return np.array([d @ r, d @ u, d @ f])
    def scr(c): return (W / 2 + c[0] / c[2] * fl / (W / Hh) * (Hh / 2), Hh / 2 - c[1] / c[2] * fl * (Hh / 2), c[2])
    def tri(P, C, UV, tex):
        xs = [p[0] for p in P]; ys = [p[1] for p in P]
        x0, x1 = max(int(min(xs)), 0), min(int(max(xs)) + 1, W); y0, y1 = max(int(min(ys)), 0), min(int(max(ys)) + 1, Hh)
        if x1 <= x0 or y1 <= y0: return
        X, Y = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = P
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9: return
        w0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / den; w1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / den; w2 = 1 - w0 - w1
        m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        iz = w0 / az + w1 / bz + w2 / cz; z = 1.0 / np.maximum(iz, 1e-9)
        col = sum(np.stack([(w / zz) * z for _ in range(3)], -1) * np.array(c) for w, zz, c in ((w0, az, C[0]), (w1, bz, C[1]), (w2, cz, C[2])))
        if tex and tile is not None:
            uu = (w0 * UV[0][0] / az + w1 * UV[1][0] / bz + w2 * UV[2][0] / cz) * z / 16.0
            vv = (w0 * UV[0][1] / az + w1 * UV[1][1] / bz + w2 * UV[2][1] / cz) * z / 16.0
            t = tile[np.floor(vv).astype(int) % 16, np.floor(uu).astype(int) % 32].astype(np.float32) / 255.0
            col = col * t
        ok = m & (z < zb[y0:y1, x0:x1])
        zz_ = zb[y0:y1, x0:x1]; zz_[ok] = z[ok]
        sub = img[y0:y1, x0:x1]; sub[ok] = col[ok]
    def poly(pts, cols, uvs, tex):
        V = [cs(p) for p in pts]
        for ia in ((0, 1, 2), (0, 2, 3)):
            vv = [V[i] for i in ia]; cc = [cols[i] for i in ia]; uu = [uvs[i] for i in ia]
            res = []
            for k in range(3):
                p, q = vv[k], vv[(k + 1) % 3]; pc, qc = np.array(cc[k], float), np.array(cc[(k + 1) % 3], float); pu, qu = uu[k], uu[(k + 1) % 3]
                pin, qin = p[2] >= 0.4, q[2] >= 0.4
                if pin: res.append((p, pc, pu))
                if pin != qin:
                    t = (0.4 - p[2]) / (q[2] - p[2]); res.append((p + (q - p) * t, pc + (qc - pc) * t, (pu[0] + (qu[0] - pu[0]) * t, pu[1] + (qu[1] - pu[1]) * t)))
            for k in range(1, len(res) - 1):
                tri([scr(res[0][0]), scr(res[k][0]), scr(res[k + 1][0])], [res[0][1], res[k][1], res[k + 1][1]], [res[0][2], res[k][2], res[k + 1][2]], tex)
    for q in Q: poly(q['p'], q['c'], q['uv'], q['tex'])
    return img

def car_box(img_fn):
    pass

def main():
    room()
    tile = concrete_tile()
    Image.fromarray(tile).resize((256, 128), Image.NEAREST).save('menuroom_tile.png')
    print('quads:', len(Q))
    # preview with a grey box standing in for the car
    save = list(Q)
    box(-2.2, 2.2, 0.15, 1.35, -0.95, 0.95, (150, 30, 30), "fbtlr")
    a = render((4.4, 1.7, 5.8), (0.3, 0.9, 0.0), 'car', tile=tile)
    b = render((2.8, 2.1, 6.6), (0.0, 0.8, 0.0), 'menu', tile=tile)
    sheet = np.concatenate([a, b], axis=1)
    Image.fromarray(np.clip(sheet, 0, 255).astype(np.uint8)).save('menuroom_preview.png')
    Q[:] = save
    # ------------------------------------------------------------------ header
    def s16(v): return int(round(v * 512))
    with open('menuroom.h', 'w') as f:
        f.write('/* generated by gen_menu_room.py - the menu garage: %d polygons + a 32x16 concrete floor tile (1 KB) */\n#pragma once\n' % len(Q))
        f.write('#define MR_NQ %d\n' % len(Q))
        r5 = (tile[:, :, 0] >> 3).astype(np.uint16); g5 = (tile[:, :, 1] >> 3).astype(np.uint16); b5 = (tile[:, :, 2] >> 3).astype(np.uint16)
        v = (r5 | (g5 << 5) | (b5 << 10) | 0x8000).flatten()
        f.write('static const unsigned short tex_concrete[512] __attribute__((aligned(4))) = {\n')
        for i in range(0, 512, 16): f.write('  ' + ','.join('0x%04x' % x for x in v[i:i + 16]) + ',\n')
        f.write('};\n')
        f.write('/* vertex positions: metres * 512 (the room is drawn with a x8 scale) */\nstatic const short mrV[MR_NQ][12] = {\n')
        for q in Q: f.write('  {' + ','.join(str(s16(c)) for p in q['p'] for c in p) + '},\n')
        f.write('};\nstatic const unsigned char mrC[MR_NQ][12] = {\n')
        for q in Q: f.write('  {' + ','.join(str(int(c)) for col in q['c'] for c in col) + '},\n')
        f.write('};\nstatic const short mrU[MR_NQ][8] = {\n')
        for q in Q: f.write('  {' + ','.join(str(int(round(c))) for uv in q['uv'] for c in uv) + '},\n')
        f.write('};\nstatic const unsigned char mrT[MR_NQ] = {' + ','.join(str(q['tex']) for q in Q) + '};\n')
    print('ok')

if __name__ == '__main__':
    main()
