#!/usr/bin/env python3
"""Generates forestatlas.h: one 64x64 RGBA5551 texture holding every roadside
prop of the RALLY FOREST map (rock face, concrete wall, fences, signs, cone, logs).
All art is drawn here procedurally and is original (no brand logos).

Tile layout (x, y, w, h) in texels - keep in sync with forestmap.h:
  rock face      0, 0,32,32   seamless left/right
  concrete wall 32, 0,32,16   seamless left/right
  metal fence   32,16,32,16   alpha cut-out
  wood fence     0,32,32,16   alpha cut-out
  LED chevrons  32,32,32,16
  chevron sign   0,48,16,16
  traffic cone  16,48,16,16   alpha cut-out
  log / planks  32,48,32,16
"""
import numpy as np
from PIL import Image, ImageDraw

rng = np.random.RandomState(7)
A = np.zeros((64, 64, 4), np.uint8)          # r,g,b,a


def put(x0, y0, img):
    h, w = img.shape[:2]
    A[y0:y0 + h, x0:x0 + w] = img


def vnoise(w, h, cell, seed):
    """value noise that wraps horizontally (seamless tiles)"""
    r = np.random.RandomState(seed)
    gw, gh = max(w // cell, 1), h // cell + 2
    g = r.rand(gh, gw)
    out = np.zeros((h, w))
    for y in range(h):
        for x in range(w):
            fx, fy = x / cell, y / cell
            x0, y0 = int(fx) % gw, int(fy)
            x1 = (x0 + 1) % gw
            tx, ty = fx - int(fx), fy - int(fy)
            tx = tx * tx * (3 - 2 * tx)
            ty = ty * ty * (3 - 2 * ty)
            a = g[y0, x0] * (1 - tx) + g[y0, x1] * tx
            b = g[y0 + 1, x0] * (1 - tx) + g[y0 + 1, x1] * tx
            out[y, x] = a * (1 - ty) + b * ty
    return out


def clip(a):
    return np.clip(a, 0, 255).astype(np.uint8)


# ---- rock face: pale granite, thin dark cracks, moss at the foot -----------------
def rock():
    w = h = 32
    n1 = vnoise(w, h, 8, 1); n2 = vnoise(w, h, 4, 2); n3 = vnoise(w, h, 2, 3); n4 = vnoise(w, h, 8, 11)
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            tone = 132 + 70 * (0.55 * n1[y, x] + 0.3 * n2[y, x] + 0.15 * n3[y, x] - 0.5) + 36 * (n4[y, x] - 0.5)
            r, g, b = tone * 1.02, tone * 0.99, tone * 0.93
            if abs(n4[y, x] - 0.5) < 0.045 or abs(n1[y, x] - 0.52) < 0.03:      # thin cracks
                r, g, b = r * 0.5, g * 0.5, b * 0.5
            if y > 24 and n3[y, x] > 0.42:                                        # moss near the foot
                r, g, b = r * 0.6, g * 0.9, b * 0.5
            img[y, x] = (r, g, b, 255)
    return img


# ---- concrete road barrier -----------------------------------------------------
def concrete():
    w, h = 32, 16
    n = vnoise(w, h, 4, 5); n2 = vnoise(w, h, 2, 6)
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            g = 150 + n[y, x] * 50 + n2[y, x] * 14
            r, gg, b = g, g, g * 0.95
            if x in (0, 16): r, gg, b = r * 0.62, gg * 0.62, b * 0.62      # panel seams
            if y < 2: r, gg, b = r * 0.8, gg * 0.8, b * 0.8                # shaded cap
            if y > 12: r, gg, b = r * 0.78, gg * 0.78, b * 0.7              # dusty foot
            if x % 16 in (6, 7, 8) and 3 < y < 12 and n[y, x] > 0.55:       # rust streak
                r, gg, b = r * 1.0, gg * 0.78, b * 0.62
            img[y, x] = (r, gg, b, 255)
    return img


def metal_fence():
    w, h = 32, 16
    img = np.zeros((h, w, 4), np.uint8)
    rail = (70, 74, 78, 255); bar = (112, 116, 120, 255); post = (52, 55, 58, 255)
    img[0:2, :] = rail; img[14:16, :] = rail
    for x in range(1, w, 4): img[2:14, x] = bar
    for px in (0, 16): img[0:16, px:px + 2] = post
    return img


def wood_fence():
    w, h = 32, 16
    img = np.zeros((h, w, 4), np.uint8)
    post = (92, 62, 38, 255); post2 = (122, 86, 54, 255); rail = (140, 100, 62, 255); rail2 = (104, 72, 44, 255)
    for y in (3, 4, 5): img[y, :] = rail if y != 5 else rail2
    for y in (9, 10, 11): img[y, :] = rail if y != 11 else rail2
    for px in (1, 17):
        img[1:16, px:px + 3] = post
        img[1:16, px] = post2
    return img


def led():
    w, h = 32, 16
    img = np.zeros((h, w, 4), np.uint8)
    img[:] = (20, 22, 24, 255)
    img[0, :] = img[h - 1, :] = (84, 86, 90, 255)
    img[:, 0] = img[:, w - 1] = (84, 86, 90, 255)
    big = Image.new('L', (w * 4, h * 4), 0)
    d = ImageDraw.Draw(big)
    for ox in (30, 74):                              # two chevrons
        d.polygon([(ox, 12), (ox + 20, 12), (ox + 42, 32), (ox + 20, 52), (ox, 52), (ox + 22, 32)], fill=255)
    m = np.array(big.resize((w, h), Image.BOX)) > 110
    for y in range(h):
        for x in range(w):
            if m[y, x]:
                k = 1.0 if (x + y) % 2 == 0 else 0.82         # LED dot pattern
                img[y, x] = (255 * k, 186 * k, 18 * k, 255)
    return img


def chevron():
    w = h = 16
    img = np.zeros((h, w, 4), np.uint8)
    img[:] = (246, 202, 18, 255)
    img[0, :] = img[h - 1, :] = (20, 20, 20, 255)
    img[:, 0] = img[:, w - 1] = (20, 20, 20, 255)
    big = Image.new('L', (w * 4, h * 4), 0)
    d = ImageDraw.Draw(big)
    d.polygon([(16, 10), (30, 10), (50, 32), (30, 54), (16, 54), (36, 32)], fill=255)
    m = np.array(big.resize((w, h), Image.BOX)) > 110
    img[m] = (18, 18, 18, 255)
    return img


def cone():
    w = h = 16
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(1, 13):
        half = 1 + (y - 1) * 4 // 11 + (1 if y > 3 else 0)
        for x in range(8 - half, 8 + half):
            c = (250, 112, 22, 255)
            if 5 <= y <= 7: c = (236, 236, 232, 255)
            if x >= 8 + half - 1: c = (c[0] * 0.78, c[1] * 0.78, c[2] * 0.78, 255)
            img[y, x] = c
    img[13:16, 1:15] = (28, 28, 30, 255)
    return img


def logs():
    w, h = 32, 16
    n = vnoise(w, h, 4, 9)
    img = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            sh = 1.0 - 0.35 * abs((y % 8) - 3.5) / 3.5            # two round logs stacked
            g = (84 + n[y, x] * 40) * sh
            img[y, x] = (g * 1.15, g * 0.78, g * 0.5, 255)
            if y % 8 == 0: img[y, x] = (40, 28, 18, 255)
    return img


put(0, 0, rock()); put(32, 0, concrete()); put(32, 16, metal_fence()); put(0, 32, wood_fence())
put(32, 32, led()); put(0, 48, chevron()); put(16, 48, cone()); put(32, 48, logs())

# fill the unused 32x16 corner (32,16 area is used; 0,32.. used) - nothing else free
Image.fromarray(A, 'RGBA').resize((256, 256), Image.NEAREST).save('forestatlas_preview.png')

r5 = (A[:, :, 0] >> 3).astype(np.uint16); g5 = (A[:, :, 1] >> 3).astype(np.uint16); b5 = (A[:, :, 2] >> 3).astype(np.uint16)
v = r5 | (g5 << 5) | (b5 << 10)
v = np.where(A[:, :, 3] > 127, v | 0x8000, 0).astype(np.uint16).flatten()

with open('forestatlas.h', 'w') as f:
    f.write('/* generated by gen_forest_atlas.py - RALLY FOREST props, 64x64 RGBA5551 (8 KB of texture VRAM) */\n')
    f.write('#pragma once\n')
    f.write('static const unsigned short tex_forest[4096] __attribute__((aligned(4))) = {\n')
    for i in range(0, 4096, 16):
        f.write('  ' + ','.join('0x%04x' % x for x in v[i:i + 16]) + ',\n')
    f.write('};\n')
print('ok, opaque px:', int((A[:, :, 3] > 127).sum()))
