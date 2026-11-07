#!/usr/bin/env python3
"""Generate Icon.png (57x57), placeholder.png (256x256) and Default.png (320x480) for
NowPlaying.app: a dark gradient tile with rounded corners and a drawn eighth-note glyph.
Pure Python (zlib + struct), no PIL, deterministic output."""
import struct
import sys
import zlib


def png(path, w, h, rgba_rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgba_rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def in_round_rect(x, y, size, r):
    # distance test against the four corner circles
    cx = r if x < r else (size - 1 - r if x > size - 1 - r else x)
    cy = r if y < r else (size - 1 - r if y > size - 1 - r else y)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def in_poly(x, y, pts):
    inside = False
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < xi:
                inside = not inside
    return inside


def note_hit(x, y, size):
    s = size / 100.0
    # head: ellipse centred (40,69), radii 12 x 9, slightly rotated look via two ellipses
    for (cx, cy, rx, ry) in ((40, 69, 12.5, 9.5), (36, 71, 9, 7)):
        if ((x - cx * s) / (rx * s)) ** 2 + ((y - cy * s) / (ry * s)) ** 2 <= 1:
            return True
    if 47 * s <= x <= 52 * s and 22 * s <= y <= 69 * s:  # stem
        return True
    flag = [(47 * s, 22 * s), (73 * s, 34 * s), (73 * s, 52 * s), (64 * s, 44 * s), (52 * s, 40 * s), (52 * s, 32 * s)]
    return in_poly(x, y, flag)


def tile(path, size, radius, accent):
    rows = []
    for y in range(size):
        g = int(24 + 20 * (1 - y / size))
        base = (g, g + 2, g + 7)
        row = []
        for x in range(size):
            if not in_round_rect(x, y, size, radius):
                row += [0, 0, 0, 0]
            elif note_hit(x + 0.5, y + 0.5, size):
                row += list(accent) + [255]
            else:
                row += list(base) + [255]
        rows.append(row)
    png(path, size, size, rows)


out = sys.argv[1] if len(sys.argv) > 1 else "."
tile(f"{out}/Icon.png", 57, 10, (72, 204, 142))
tile(f"{out}/placeholder.png", 256, 22, (112, 118, 128))
png(f"{out}/Default.png", 320, 480, [[0, 0, 0, 255] * 320 for _ in range(480)])
print("images ok")
