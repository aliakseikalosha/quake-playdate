#!/usr/bin/env python3
"""Compare the pictures two hostcheck builds wrote in HSHOTS mode (see hostcheck.c).

    tools/hostcheck/compare-shots.py <old dir> <new dir> [<out dir>]

For every dD_NNNN.ppm (the half-resolution 3D view) present in both directories it prints the
share of pixels whose brightness differs noticeably, and with <out dir> writes old | new
side by side (2x) as dD_NNNN.png, plus the LCD images (dD_NNNN-lcd.png). Pure Python, no
libraries needed."""
import os
import struct
import sys
import zlib


def read_pnm(path):
    data = open(path, "rb").read()
    parts, pos = [], 0
    while len(parts) < (4 if data[:2] == b"P6" else 3):
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        parts.append(data[start:pos])
    pos += 1
    w, h = int(parts[1]), int(parts[2])
    if parts[0] == b"P6":
        px = data[pos:pos + w * h * 3]
        return w, h, [tuple(px[i * 3:i * 3 + 3]) for i in range(w * h)]
    # P4: 1 = black
    rowbytes = (w + 7) // 8
    out = []
    for y in range(h):
        row = data[pos + y * rowbytes:pos + (y + 1) * rowbytes]
        for x in range(w):
            bit = (row[x >> 3] >> (7 - (x & 7))) & 1
            out.append((0, 0, 0) if bit else (255, 255, 255))
    return w, h, out


def write_png(path, w, h, pixels):
    raw = b"".join(b"\0" + bytes(c for p in pixels[y * w:(y + 1) * w] for c in p) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def side_by_side(a, b, scale, gap=4):
    (wa, ha, pa), (wb, hb, pb) = a, b
    w, h = (wa + wb) * scale + gap, max(ha, hb) * scale
    out = [(255, 0, 255)] * (w * h)
    for (img, x0) in ((a, 0), (b, wa * scale + gap)):
        iw, ih, ip = img
        for y in range(ih * scale):
            for x in range(iw * scale):
                out[y * w + x0 + x] = ip[(y // scale) * iw + x // scale]
    return w, h, out


def lum(p):
    return (77 * p[0] + 151 * p[1] + 28 * p[2]) >> 8


def main():
    old, new = sys.argv[1], sys.argv[2]
    outdir = sys.argv[3] if len(sys.argv) > 3 else None
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    names = sorted(n for n in os.listdir(old) if n.endswith(".ppm") and os.path.exists(os.path.join(new, n)))
    tot_diff = tot_px = 0
    for n in names:
        a, b = read_pnm(os.path.join(old, n)), read_pnm(os.path.join(new, n))
        if a[:2] != b[:2]:
            print(f"{n}: sizes differ {a[:2]} {b[:2]}")
            continue
        diff = sum(1 for p, q in zip(a[2], b[2]) if abs(lum(p) - lum(q)) > 24)
        tot_diff += diff
        tot_px += len(a[2])
        print(f"{n}: {100.0 * diff / len(a[2]):5.1f}% of pixels differ by > 24 levels")
        if outdir:
            base = n[:-4]
            write_png(os.path.join(outdir, base + ".png"), *side_by_side(a, b, 2))
            lo, ln = os.path.join(old, base + ".pbm"), os.path.join(new, base + ".pbm")
            if os.path.exists(lo) and os.path.exists(ln):
                write_png(os.path.join(outdir, base + "-lcd.png"), *side_by_side(read_pnm(lo), read_pnm(ln), 1))
    if tot_px:
        print(f"all: {100.0 * tot_diff / tot_px:.2f}% of pixels differ by > 24 levels ({len(names)} pictures)")


if __name__ == "__main__":
    main()
