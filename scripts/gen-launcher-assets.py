#!/usr/bin/env python3
"""Generates the Playdate launcher assets (pdxinfo imagePath and launchSoundPath) from the
game's own data, so they look and sound like Quake.

    scripts/gen-launcher-assets.py [<pak0.pak>] [<out dir>]

<pak0.pak> defaults to port/boards/playdate/Source/id1/pak0_demo.pak (the shareware, which is
what a release ships), else pak0.pak next to it; <out dir> defaults to
port/boards/playdate/Source/launcher. It writes:

    card.png            350x155  the card in the launcher's "cards" view
    card-pressed.png    350x155  the card while A is held (the card inverted)
    icon.png             32x32   the icon in the "list" view
    launchImage.png     400x240  shown while the game loads
    wrapping-pattern.png 400x240  wrapping paper of a newly downloaded copy
    launch.wav                   played during the launch animation (the ammo pickup)

The Q logo is cut out of gfx/conback.lmp (the console background: a dark embossed logo on a
stone texture) by thresholding its blurred brightness; the text uses the console font
(CONCHARS in gfx.wad); the stone behind is BACKTILE from gfx.wad, ordered-dithered dark.
Everything is 1-bit, so pdc has nothing left to guess. Pure Python, no libraries needed.
"""
import os
import struct
import sys
import wave
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SOURCE = os.path.join(ROOT, "port", "boards", "playdate", "Source")
SOUND = "sound/weapons/lock4.wav"  # picking up ammo

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


# --- Quake data ---------------------------------------------------------------------------

def read_pak(path):
    f = open(path, "rb")
    ident, off, size = struct.unpack("<4sii", f.read(12))
    if ident != b"PACK":
        sys.exit(f"{path}: not a pak file")
    f.seek(off)
    entries = {}
    for _ in range(size // 64):
        name, pos, length = struct.unpack("<56sii", f.read(64))
        entries[name.split(b"\0")[0].decode()] = (pos, length)

    def get(name):
        pos, length = entries[name]
        f.seek(pos)
        return f.read(length)
    return get


def read_wad(data):
    _, count, off = struct.unpack("<4sii", data[:12])
    lumps = {}
    for i in range(count):
        pos, _, size, _, _, _, name = struct.unpack("<iiibbh16s", data[off + i * 32:off + i * 32 + 32])
        lumps[name.split(b"\0")[0].decode()] = data[pos:pos + size]
    return lumps


def brightness(pixels, w, h, palette):
    """Palette indices -> rows of 0..1 brightness."""
    lum = [(0.299 * palette[i * 3] + 0.587 * palette[i * 3 + 1] + 0.114 * palette[i * 3 + 2]) / 255
           for i in range(256)]
    return [[lum[pixels[y * w + x]] for x in range(w)] for y in range(h)]


# --- masks: rows of floats, 1 = ink ---------------------------------------------------------

def blur(img, r):
    h, w = len(img), len(img[0])
    out = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            s = n = 0
            for yy in range(max(0, y - r), min(h, y + r + 1)):
                row = img[yy]
                for xx in range(max(0, x - r), min(w, x + r + 1)):
                    s += row[xx]
                    n += 1
            out[y][x] = s / n
    return out


def largest_component(mask):
    h, w = len(mask), len(mask[0])
    seen = [[False] * w for _ in range(h)]
    best = []
    for sy in range(h):
        for sx in range(w):
            if not mask[sy][sx] or seen[sy][sx]:
                continue
            seen[sy][sx] = True
            stack, comp = [(sx, sy)], []
            while stack:
                x, y = stack.pop()
                comp.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(comp) > len(best):
                best = comp
    out = [[0.0] * w for _ in range(h)]
    for x, y in best:
        out[y][x] = 1.0
    return out


def crop(mask):
    ys = [y for y, row in enumerate(mask) if any(row)]
    xs = [x for x in range(len(mask[0])) if any(row[x] for row in mask)]
    return [row[xs[0]:xs[-1] + 1] for row in mask[ys[0]:ys[-1] + 1]]


def logo_mask(get, palette):
    """The Q logo from the console background, cropped to its bounding box."""
    data = get("gfx/conback.lmp")
    w, h = struct.unpack("<ii", data[:8])
    lum = blur(brightness(data[8:], w, h, palette), 2)
    # the logo is the darkest large shape; the frame and the id logo in the corner are dark too,
    # but kept apart from it by the margin and by taking only the largest connected shape
    dark = [[1.0 if 6 <= y < h - 6 and 40 <= x < w - 40 and lum[y][x] < 19 / 255 else 0.0
             for x in range(w)] for y in range(h)]
    mask = largest_component(dark)
    for r in (2, 2, 1):  # round off the stone texture's bite marks
        mask = [[1.0 if v >= 0.5 else 0.0 for v in row] for row in blur(mask, r)]
    return crop(mask)


def scale(mask, w, h):
    """Resample to w x h: 4x4 supersampled bilinear coverage, thresholded at one half."""
    sh, sw = len(mask), len(mask[0])

    def at(x, y):
        x = min(max(x, 0.0), sw - 1.0)
        y = min(max(y, 0.0), sh - 1.0)
        x0, y0 = int(x), int(y)
        x1, y1 = min(x0 + 1, sw - 1), min(y0 + 1, sh - 1)
        fx, fy = x - x0, y - y0
        top = mask[y0][x0] * (1 - fx) + mask[y0][x1] * fx
        bottom = mask[y1][x0] * (1 - fx) + mask[y1][x1] * fx
        return top * (1 - fy) + bottom * fy
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            s = 0.0
            for j in range(4):
                for i in range(4):
                    s += at((x + (i + 0.5) / 4) * sw / w - 0.5, (y + (j + 0.5) / 4) * sh / h - 0.5)
            row.append(1.0 if s / 16 >= 0.5 else 0.0)
        out.append(row)
    return out


def scale_to_height(mask, h):
    return scale(mask, max(1, round(len(mask[0]) * h / len(mask))), h)


def text_mask(conchars, palette, text, k):
    """Text in the console font (its lit face, not the shading), each font pixel k x k."""
    w = 8 * len(text) * k
    out = [[0.0] * w for _ in range(8 * k)]
    for n, ch in enumerate(text):
        c = ord(ch)
        for y in range(8):
            for x in range(8):
                p = conchars[(c // 16 * 8 + y) * 128 + c % 16 * 8 + x]
                if p and palette[p * 3] + palette[p * 3 + 1] + palette[p * 3 + 2] > 3 * 50:
                    for j in range(k):
                        for i in range(k):
                            out[y * k + j][(n * 8 + x) * k + i] = 1.0
    return out


# --- 1-bit images: rows of 0 (black) / 1 (white) -------------------------------------------

def solid(w, h, v):
    return [[v] * w for _ in range(h)]


def stone(tile, w, h):
    """BACKTILE repeated, its brightness stretched to 0..0.25 and ordered-dithered."""
    th, tw = len(tile), len(tile[0])
    vals = sorted(v for row in tile for v in row)
    lo, hi = vals[len(vals) // 20], vals[-len(vals) // 100]
    return [[1 if min(max((tile[y % th][x % tw] - lo) / (hi - lo), 0), 1) * 0.25 * 16 > BAYER4[y % 4][x % 4] + 0.5
             else 0 for x in range(w)] for y in range(h)]


def stamp(img, mask, x0, y0, colour, outline=0):
    """Draws mask at (x0, y0) in colour, first ringed by an outline of the other colour."""
    h, w = len(img), len(img[0])
    mh, mw = len(mask), len(mask[0])
    if outline:
        for y in range(-outline, mh + outline):
            for x in range(-outline, mw + outline):
                if not 0 <= y0 + y < h or not 0 <= x0 + x < w:
                    continue
                if any(mask[yy][xx]
                       for yy in range(max(0, y - outline), min(mh, y + outline + 1))
                       for xx in range(max(0, x - outline), min(mw, x + outline + 1))):
                    img[y0 + y][x0 + x] = 1 - colour
    for y in range(mh):
        for x in range(mw):
            if mask[y][x] and 0 <= y0 + y < h and 0 <= x0 + x < w:
                img[y0 + y][x0 + x] = colour


def write_png(path, img):
    h, w = len(img), len(img[0])
    raw = b"".join(b"\0" + bytes(255 if v else 0 for v in row) for row in img)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_sound(path, data):
    """Quake's 8-bit unsigned mono wav as 16-bit signed (pdc turns it into a .pda)."""
    tmp = path + ".src"
    with open(tmp, "wb") as f:
        f.write(data)
    with wave.open(tmp) as src:
        rate, width, frames = src.getframerate(), src.getsampwidth(), src.readframes(src.getnframes())
    os.remove(tmp)
    if width == 1:
        frames = b"".join(struct.pack("<h", (b - 128) << 8) for b in frames)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(frames)
    return len(frames) / 2 / rate


# --- the assets ----------------------------------------------------------------------------

def main():
    pak = sys.argv[1] if len(sys.argv) > 1 else None
    if not pak:
        for name in ("pak0_demo.pak", "pak0.pak"):
            pak = os.path.join(SOURCE, "id1", name)
            if os.path.exists(pak):
                break
        else:
            sys.exit("no pak0_demo.pak or pak0.pak in Source/id1: pass the path of one")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SOURCE, "launcher")
    os.makedirs(out, exist_ok=True)

    get = read_pak(pak)
    palette = get("gfx/palette.lmp")
    wad = read_wad(get("gfx.wad"))
    conchars = wad["CONCHARS"]
    tw, th = struct.unpack("<ii", wad["BACKTILE"][:8])
    tile = brightness(wad["BACKTILE"][8:], tw, th, palette)
    logo = logo_mask(get, palette)

    # card: logo on the left, QUAKE / PLAYDATE on the right
    card = stone(tile, 350, 155)
    big = scale_to_height(logo, 135)
    stamp(card, big, 22, 10, 1, outline=2)
    title, sub = text_mask(conchars, palette, "QUAKE", 4), text_mask(conchars, palette, "PLAYDATE", 2)
    right = 22 + len(big[0]) + (350 - 22 - len(big[0]) - len(title[0])) // 2
    stamp(card, title, right, 46, 1, outline=2)
    stamp(card, sub, right + (len(title[0]) - len(sub[0])) // 2, 92, 1, outline=2)
    write_png(os.path.join(out, "card.png"), card)
    write_png(os.path.join(out, "card-pressed.png"), [[1 - v for v in row] for row in card])

    icon = solid(32, 32, 0)
    small = scale_to_height(logo, 30)
    stamp(icon, small, (32 - len(small[0])) // 2, 1, 1)
    write_png(os.path.join(out, "icon.png"), icon)

    launch = stone(tile, 400, 240)
    big = scale_to_height(logo, 196)
    stamp(launch, big, (400 - len(big[0])) // 2, 8, 1, outline=3)
    loading = text_mask(conchars, palette, "LOADING", 2)
    stamp(launch, loading, (400 - len(loading[0])) // 2, 214, 1, outline=2)
    write_png(os.path.join(out, "launchImage.png"), launch)

    paper = solid(400, 240, 1)
    small = scale_to_height(logo, 28)
    for row, y in enumerate(range(-14, 240, 40)):
        for x in range(-25 + (row % 2) * 25, 400, 50):
            stamp(paper, small, x, y, 0)
    write_png(os.path.join(out, "wrapping-pattern.png"), paper)

    seconds = write_sound(os.path.join(out, "launch.wav"), get(SOUND))
    print(f"{out}: card, card-pressed, icon, launchImage, wrapping-pattern; "
          f"launch.wav = {SOUND} ({seconds:.2f} s) from {os.path.basename(pak)}")


if __name__ == "__main__":
    main()
