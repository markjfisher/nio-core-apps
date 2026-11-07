#!/usr/bin/env python3
"""Build-time graphics generator for the FujiNet demo.

Reads demo/art/{logo,letters,icons}.txt and writes a ca65 include with:
  - the striped FUJINET logo as custom characters + its 40x5 screen map
  - 15 sprite shapes (7 letters, 8 icons), 64 bytes each
  - motion, colour and filter tables

    python3 demo/tools/gen_gfx.py OUT.inc
"""
import math
import os
import sys

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "art")

LOGO_ROWS = 5            # character rows (40 pixels)
LOGO_FIRST_CHAR = 64     # screen codes 64.. hold the logo tiles
LOGO_LAST_CHAR = 223     # 224..255 are the starfield and line characters


def read_art(name):
    """Return {label: [[0/1, ...], ...]} or a single bitmap for unlabelled files."""
    with open(os.path.join(ART, name)) as f:
        lines = [l.rstrip("\n") for l in f]
    if not lines[0].startswith("="):
        return [[1 if c == "#" else 0 for c in l] for l in lines if l]
    out, label = {}, None
    for l in lines:
        if l.startswith("="):
            label = l[1:].strip()
            out[label] = []
        elif l:
            out[label].append([1 if c == "#" else 0 for c in l])
    return out


def byte_lines(data, per_line=16):
    rows = []
    for i in range(0, len(data), per_line):
        rows.append("        .byte " + ",".join(f"${b:02x}" for b in data[i:i + per_line]))
    return "\n".join(rows)


def logo():
    bits = read_art("logo.txt")
    h, w = len(bits), len(bits[0])
    assert h == LOGO_ROWS * 8, h
    assert w <= 320, w
    x0 = (320 - w) // 2
    canvas = [[0] * 320 for _ in range(h)]
    for y in range(h):
        if y % 8 == 7:          # the stripe: every character row's last line is empty
            continue
        for x in range(w):
            canvas[y][x0 + x] = bits[y][x]
    tiles, codes, screen = [], {}, []
    for row in range(LOGO_ROWS):
        for col in range(40):
            tile = tuple(
                sum(canvas[row * 8 + y][col * 8 + x] << (7 - x) for x in range(8))
                for y in range(8))
            if not any(tile):
                screen.append(32)
                continue
            if tile not in codes:
                codes[tile] = LOGO_FIRST_CHAR + len(tiles)
                tiles.append(tile)
            screen.append(codes[tile])
    assert LOGO_FIRST_CHAR + len(tiles) - 1 <= LOGO_LAST_CHAR, len(tiles)
    return tiles, screen


def sprite_bytes(bits):
    assert len(bits) <= 21 and all(len(r) == 24 for r in bits), "sprites are 24x21"
    rows = bits + [[0] * 24] * (21 - len(bits))
    out = []
    for r in rows:
        for b in range(3):
            out.append(sum(r[b * 8 + x] << (7 - x) for x in range(8)))
    return out + [0]


# C64 colours: 0 black 1 white 2 red 3 cyan 4 purple 5 green 6 blue 7 yellow
# 8 orange 9 brown 10 light red 11 dark grey 12 grey 13 light green
# 14 light blue 15 light grey
RAINBOW = [2, 2, 10, 10, 8, 8, 8, 7, 7, 7, 1, 7, 13, 13, 5, 5,
           5, 3, 3, 3, 14, 14, 6, 6, 6, 4, 4, 4, 10, 10, 2, 2]
SPRITE_RAINBOW = [1, 7, 13, 3, 14, 10, 8, 7, 1, 15, 3, 13, 7, 8, 10, 14]
BARS = [[9, 2, 8, 10, 7, 10, 8, 2, 9],          # red/orange
        [11, 5, 13, 7, 1, 7, 13, 5, 11],        # green
        [6, 4, 14, 3, 1, 3, 14, 4, 6]]          # blue
VU_RAMPS = [[0, 9, 2, 8, 10, 15, 7, 1],         # voice 1: bass + drums
            [0, 11, 5, 13, 15, 7, 1, 1],        # voice 2: chords
            [0, 6, 4, 14, 3, 15, 1, 1]]         # voice 3: melody
VU_DIST = [3, 2, 1, 0, 0, 1, 2, 3]
PULSE = [11, 11, 12, 12, 15, 15, 1, 1]
SCROLL_COLORS = [11, 11, 12, 12, 15, 15] + [1] * 28 + [15, 15, 12, 12, 11, 11]
ICON_COLORS = [3, 14, 13, 1, 10, 7, 8, 15]      # wifi floppy globe cloud heart note joystick phone


def table(name, values):
    return f"{name}:\n" + byte_lines([v & 0xFF for v in values])


def main():
    out = sys.argv[1]
    tiles, screen = logo()
    letters = read_art("letters.txt")
    icons = read_art("icons.txt")
    shapes = [letters[c] for c in "FUJINET"] + [icons[n] for n in
              ("wifi", "floppy", "globe", "cloud", "heart", "note", "joystick", "phone")]
    sprites = []
    for s in shapes:
        sprites += sprite_bytes(s)

    n = 256
    sin = [math.sin(2 * math.pi * i / n) for i in range(n)]
    cos = [math.cos(2 * math.pi * i / n) for i in range(n)]

    # Sprite coordinates: x 24..320 is on screen, y 50..229 (top line of a
    # 21-line sprite). Sprites stay in the starfield band, raster lines
    # 116..200: below the subtitle (row 7), above the FujiNet status (row 19),
    # and clear of the border bars, whose timing sprite DMA would upset.
    orbit_x = [round(172 + 132 * c) for c in cos]
    orbit_y = [round(148 + 32 * s) for s in sin]
    wave_y = [round(148 + 32 * s) for s in sin]
    sway_x = [round(74 + 46 * s) for s in sin]      # letter snake: base x 28..120
    bar_y = [round(12 + 12 * s) for s in sin]       # top bars: 0..24 inside 33 lines
    filt = [round(0x30 + 0x38 * (1 + s)) for s in sin]

    with open(out, "w") as f:
        f.write("; generated by demo/tools/gen_gfx.py -- do not edit\n\n")
        f.write(f"LOGO_FIRST_CHAR = {LOGO_FIRST_CHAR}\n")
        f.write(f"LOGO_NUM_CHARS = {len(tiles)}\n")
        f.write(f"NUM_SHAPES = {len(shapes)}\n\n")
        f.write("logo_chars:\n" + byte_lines([b for t in tiles for b in t]) + "\n")
        f.write("logo_map:\n" + byte_lines(screen, 40) + "\n")
        f.write(table("orbit_x_lo", orbit_x) + "\n")
        f.write(table("orbit_x_hi", [v >> 8 for v in orbit_x]) + "\n")
        f.write(table("orbit_y", orbit_y) + "\n")
        f.write(table("wave_y", wave_y) + "\n")
        f.write(table("sway_x", sway_x) + "\n")
        f.write(table("bar_y", bar_y) + "\n")
        f.write(table("filter_sweep", filt) + "\n")
        f.write(table("rainbow_ext", (RAINBOW * 3)[:72]) + "\n")
        f.write(table("sprite_rainbow", SPRITE_RAINBOW) + "\n")
        f.write(table("bar_colors", sum(BARS, [])) + "\n")
        f.write(table("vu_ramps", sum(VU_RAMPS, [])) + "\n")
        f.write(table("vu_dist", VU_DIST) + "\n")
        f.write(table("pulse_ramp", PULSE) + "\n")
        f.write(table("scroll_colors", SCROLL_COLORS) + "\n")
        f.write(table("icon_colors", ICON_COLORS) + "\n")
        f.write("\n.segment \"SPRITES\"\n")
        f.write("sprite_data:\n" + byte_lines(sprites) + "\n")
    print(f"gen_gfx: {len(tiles)} logo chars, {len(shapes)} sprites -> {out}")


if __name__ == "__main__":
    main()
