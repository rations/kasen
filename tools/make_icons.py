#!/usr/bin/env python3
"""Generate the transport and track-strip button icons as HVIF.

HVIF is Haiku's vector icon format (what Icon-O-Matic exports and what
BIconUtils::GetVectorIcon renders). This script holds a small HVIF encoder
and the icon designs, and writes:

  src/ui/icons/<name>.hvif   one file per icon; open in Icon-O-Matic to edit
  src/ui/icons/IconData.h    the same bytes as C arrays, built into the app

Run from the repository root:  python3 tools/make_icons.py

The encoding follows Haiku's reader, src/libs/icon/flat_icon/ in the Haiku
tree (FlatIconImporter.cpp, FlatIconFormat.cpp): magic 'ficn', then styles,
paths and shapes, coordinates in a 64x64 canvas.

The look follows Haiku's icon style: a soft shadow, a dark outline, a body
lit from the top left, and a gloss over the upper half.
"""

import math
import os
import struct
import sys

# ---- format constants (FlatIconFormat.h) -----------------------------------

STYLE_TYPE_SOLID_COLOR = 1
STYLE_TYPE_GRADIENT = 2
SHAPE_TYPE_PATH_SOURCE = 10
TRANSFORMER_TYPE_STROKE = 23

GRADIENT_LINEAR = 0
GRADIENT_CIRCULAR = 1
GRADIENT_FLAG_TRANSFORM = 1 << 1

PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_NO_CURVES = 1 << 3

SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
SHAPE_FLAG_TRANSLATION = 1 << 5

# agg line joins / caps, as the stroke transformer stores them
JOIN_MITER, JOIN_ROUND = 0, 2
CAP_BUTT, CAP_ROUND = 0, 2

MAGIC = 0x6669636E  # 'ficn', written little-endian


def write_coord(out, c):
    """FlatIconFormat.cpp write_coord: 7 bits for small whole numbers,
    else 15 bits of (c + 128) * 102 with the high bit set."""
    c = max(-128.0, min(192.0, c))
    if int(c * 100.0) == int(c) * 100 and -32.0 <= c <= 95.0:
        out.append(int(c + 32.0))
    else:
        v = int((c + 128.0) * 102.0) | 0x8000
        out.append(v >> 8)
        out.append(v & 0xFF)


def write_float24(out, value):
    """FlatIconFormat.cpp write_float_24: 1 sign, 6 exponent, 17 mantissa."""
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = (bits & 0x80000000) >> 31
    exponent = ((bits & 0x7F800000) >> 23) - 127
    mantissa = bits & 0x007FFFFF
    if exponent >= 32 or exponent < -32:
        out += b"\0\0\0"
        return
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    out += bytes(((short >> 16) & 0xFF, (short >> 8) & 0xFF, short & 0xFF))


class Icon:
    def __init__(self):
        self.styles = []
        self.paths = []
        self.shapes = []

    # -- styles --
    def solid(self, rgba):
        b = bytearray([STYLE_TYPE_SOLID_COLOR])
        b += bytes(rgba)
        self.styles.append(bytes(b))
        return len(self.styles) - 1

    def _gradient(self, gtype, matrix, stops):
        b = bytearray([STYLE_TYPE_GRADIENT, gtype, GRADIENT_FLAG_TRANSFORM, len(stops)])
        for v in matrix:
            write_float24(b, v)
        for offset, rgba in stops:
            b.append(int(round(offset * 255)))
            b += bytes(rgba)
        self.styles.append(bytes(b))
        return len(self.styles) - 1

    def linear(self, p0, p1, stops):
        """Gradient along p0 -> p1. The renderer runs a linear gradient over
        x = -64..64 in gradient space (IconRenderer.cpp); the matrix maps
        that span onto p0..p1. Matrix order is agg's: sx shy shx sy tx ty."""
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        cx, cy = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
        m = (dx / 128.0, dy / 128.0, -dy / 128.0, dx / 128.0, cx, cy)
        return self._gradient(GRADIENT_LINEAR, m, stops)

    def radial(self, center, radius, stops):
        """Circular gradient: radius 0..64 in gradient space."""
        s = radius / 64.0
        return self._gradient(GRADIENT_CIRCULAR, (s, 0.0, 0.0, s, center[0], center[1]), stops)

    # -- paths --
    def polygon(self, pts):
        b = bytearray([PATH_FLAG_CLOSED | PATH_FLAG_NO_CURVES, len(pts)])
        for x, y in pts:
            write_coord(b, x)
            write_coord(b, y)
        self.paths.append(bytes(b))
        return len(self.paths) - 1

    def curve_path(self, pts, closed):
        """pts: (point, control_in, control_out) triples."""
        b = bytearray([PATH_FLAG_CLOSED if closed else 0, len(pts)])
        for p, cin, cout in pts:
            for x, y in (p, cin, cout):
                write_coord(b, x)
                write_coord(b, y)
        self.paths.append(bytes(b))
        return len(self.paths) - 1

    def circle(self, c, r):
        k = 0.5523 * r
        x, y = c
        return self.curve_path(
            [
                ((x, y - r), (x - k, y - r), (x + k, y - r)),
                ((x + r, y), (x + r, y - k), (x + r, y + k)),
                ((x, y + r), (x + k, y + r), (x - k, y + r)),
                ((x - r, y), (x - r, y + k), (x - r, y - k)),
            ],
            True,
        )

    def open_path(self, pts):
        """An open path from (point, in, out) triples (for strokes)."""
        return self.curve_path(pts, False)

    # -- shapes --
    def shape(self, style, paths, stroke=None, translate=None):
        flags = 0
        if stroke:
            flags |= SHAPE_FLAG_HAS_TRANSFORMERS
        if translate:
            flags |= SHAPE_FLAG_TRANSLATION
        b = bytearray([SHAPE_TYPE_PATH_SOURCE, style, len(paths)])
        b += bytes(paths)
        b.append(flags)
        if translate:
            write_coord(b, translate[0])
            write_coord(b, translate[1])
        if stroke:
            width, join, cap = stroke
            b.append(1)  # one transformer
            b += bytes([TRANSFORMER_TYPE_STROKE, int(width + 128), join | (cap << 4), 4])
        self.shapes.append(bytes(b))

    def encode(self):
        for name, items in (("styles", self.styles), ("paths", self.paths), ("shapes", self.shapes)):
            if len(items) > 255:
                raise ValueError("too many " + name)
        out = bytearray(struct.pack("<I", MAGIC))
        for items in (self.styles, self.paths, self.shapes):
            out.append(len(items))
            for it in items:
                out += it
        return bytes(out)


# ---- the look ----------------------------------------------------------------

SHADOW = (0, 0, 0, 70)
GLOSS_TOP = (255, 255, 255, 165)
GLOSS_END = (255, 255, 255, 0)

# (light, dark, outline) per colour family
PALETTES = {
    "green": ((196, 248, 160, 255), (34, 132, 40, 255), (8, 40, 10, 255)),
    "red": ((255, 150, 128, 255), (172, 12, 10, 255), (60, 0, 0, 255)),
    "blue": ((200, 218, 246, 255), (46, 78, 152, 255), (8, 18, 46, 255)),
    "amber": ((255, 226, 140, 255), (214, 118, 8, 255), (64, 30, 0, 255)),
    "steel": ((240, 240, 244, 255), (104, 106, 118, 255), (36, 36, 42, 255)),
}


def solid_glyph(icon, paths, family, bounds):
    """Filled glyph: shadow, outline, lit body, gloss on the upper half."""
    light, dark, outline = PALETTES[family]
    x0, y0, x1, y1 = bounds
    icon.shape(icon.solid(SHADOW), paths, translate=(2, 3))
    icon.shape(icon.solid(outline), paths, stroke=(4, JOIN_ROUND, CAP_ROUND))
    body = icon.linear((x0, y0), (x1, y1), [(0.0, light), (1.0, dark)])
    icon.shape(body, paths)
    mid = y0 + (y1 - y0) * 0.55
    gloss = icon.linear((x0, y0), (x0, mid), [(0.0, GLOSS_TOP), (1.0, GLOSS_END)])
    icon.shape(gloss, paths)


def stroked_glyph(icon, paths, family, bounds, width, heads=None):
    """A thick line glyph (rings, loop arrows): the outline is a wider stroke
    under the coloured stroke; optional filled arrowheads ride on top."""
    light, dark, outline = PALETTES[family]
    x0, y0, x1, y1 = bounds
    shadow = icon.solid(SHADOW)
    icon.shape(shadow, paths, stroke=(width, JOIN_ROUND, CAP_ROUND), translate=(2, 3))
    if heads:
        icon.shape(shadow, heads, translate=(2, 3))
    out = icon.solid(outline)
    icon.shape(out, paths, stroke=(width + 4, JOIN_ROUND, CAP_ROUND))
    if heads:
        icon.shape(out, heads, stroke=(4, JOIN_ROUND, CAP_ROUND))
    body = icon.linear((x0, y0), (x1, y1), [(0.0, light), (1.0, dark)])
    icon.shape(body, paths, stroke=(width, JOIN_ROUND, CAP_ROUND))
    if heads:
        icon.shape(body, heads)
    mid = y0 + (y1 - y0) * 0.5
    gloss = icon.linear((x0, y0), (x0, mid), [(0.0, GLOSS_TOP), (1.0, GLOSS_END)])
    icon.shape(gloss, paths, stroke=(width, JOIN_ROUND, CAP_ROUND))


# ---- the icons (64x64 canvas) ---------------------------------------------------

def icon_play():
    i = Icon()
    solid_glyph(i, [i.polygon([(15, 8), (55, 32), (15, 56)])], "green", (15, 8, 55, 56))
    return i


def icon_pause():
    i = Icon()
    p = [i.polygon([(12, 9), (27, 9), (27, 55), (12, 55)]),
         i.polygon([(37, 9), (52, 9), (52, 55), (37, 55)])]
    solid_glyph(i, p, "blue", (12, 9, 52, 55))
    return i


def icon_stop():
    i = Icon()
    solid_glyph(i, [i.polygon([(11, 11), (53, 11), (53, 53), (11, 53)])], "blue", (11, 11, 53, 53))
    return i


def icon_rtz():
    i = Icon()
    p = [i.polygon([(8, 9), (17, 9), (17, 55), (8, 55)]),
         i.polygon([(18, 32), (56, 9), (56, 55)])]
    solid_glyph(i, p, "blue", (8, 9, 56, 55))
    return i


def icon_next():
    i = Icon()
    p = [i.polygon([(8, 9), (46, 32), (8, 55)]),
         i.polygon([(47, 9), (56, 9), (56, 55), (47, 55)])]
    solid_glyph(i, p, "blue", (8, 9, 56, 55))
    return i


def icon_step_back():
    i = Icon()
    p = [i.polygon([(5, 32), (32, 12), (32, 52)]),
         i.polygon([(32, 32), (59, 12), (59, 52)])]
    solid_glyph(i, p, "blue", (5, 12, 59, 52))
    return i


def icon_step_fwd():
    i = Icon()
    p = [i.polygon([(5, 12), (32, 32), (5, 52)]),
         i.polygon([(32, 12), (59, 32), (32, 52)])]
    solid_glyph(i, p, "blue", (5, 12, 59, 52))
    return i


def icon_record():
    i = Icon()
    solid_glyph(i, [i.circle((32, 32), 23)], "red", (9, 9, 55, 55))
    return i


def icon_record_punch():
    """Punch in/out: a smaller dot inside a ring."""
    i = Icon()
    stroked_glyph(i, [i.circle((32, 32), 22)], "red", (8, 8, 56, 56), 6)
    solid_glyph(i, [i.circle((32, 32), 11)], "red", (21, 21, 43, 43))
    return i


def icon_mono():
    i = Icon()
    stroked_glyph(i, [i.circle((32, 32), 17)], "steel", (13, 13, 51, 51), 7)
    return i


def icon_stereo():
    i = Icon()
    stroked_glyph(i, [i.circle((22, 32), 15), i.circle((42, 32), 15)], "steel", (5, 15, 59, 49), 6)
    return i


def icon_loop():
    """Repeat: top run going right, bottom run going left, rounded corners,
    each ending in an arrowhead."""
    i = Icon()
    k = 0.5523
    r = 12
    top, bot, left, right = 18, 46, 8, 56
    upper = i.open_path([
        ((left, 34), (left, 34), (left, 30)),
        ((left, top + r), (left, top + r), (left, top + r - k * r)),
        ((left + r, top), (left + r - k * r, top), (left + r, top)),
        ((38, top), (38, top), (38, top)),
    ])
    lower = i.open_path([
        ((right, 30), (right, 30), (right, 34)),
        ((right, bot - r), (right, bot - r), (right, bot - r + k * r)),
        ((right - r, bot), (right - r + k * r, bot), (right - r, bot)),
        ((26, bot), (26, bot), (26, bot)),
    ])
    heads = [i.polygon([(36, top - 11), (50, top), (36, top + 11)]),
             i.polygon([(28, bot - 11), (14, bot), (28, bot + 11)])]
    stroked_glyph(i, [upper, lower], "amber", (left, top - 8, right, bot + 8), 7, heads)
    return i


def icon_split():
    """Split: two clips with a red cut line between them."""
    i = Icon()
    left = i.polygon([(3, 12), (26, 12), (26, 52), (3, 52)])
    right = i.polygon([(38, 12), (61, 12), (61, 52), (38, 52)])
    solid_glyph(i, [left, right], "green", (3, 12, 61, 52))
    cut = i.open_path([((32, 4), (32, 4), (32, 4)), ((32, 60), (32, 60), (32, 60))])
    stroked_glyph(i, [cut], "red", (28, 4, 36, 60), 6)
    return i


def icon_metronome():
    """Metronome: a wooden pyramid with a steel pendulum and its weight."""
    i = Icon()
    body = i.polygon([(7, 60), (57, 60), (42, 4), (22, 4)])
    solid_glyph(i, [body], "amber", (7, 4, 57, 60))
    arm = i.open_path([((32, 52), (32, 52), (32, 52)), ((50, 8), (50, 8), (50, 8))])
    stroked_glyph(i, [arm], "steel", (30, 6, 52, 54), 5)
    weight = i.polygon([(36, 22), (49, 27), (45, 37), (32, 32)])
    solid_glyph(i, [weight], "steel", (32, 22, 49, 37))
    return i


ICONS = {
    "play": icon_play,
    "pause": icon_pause,
    "stop": icon_stop,
    "rtz": icon_rtz,
    "next": icon_next,
    "step_back": icon_step_back,
    "step_fwd": icon_step_fwd,
    "record": icon_record,
    "record_punch": icon_record_punch,
    "loop": icon_loop,
    "mono": icon_mono,
    "stereo": icon_stereo,
    "split": icon_split,
    "metronome": icon_metronome,
}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(root, "src", "ui", "icons")
    os.makedirs(outdir, exist_ok=True)
    lines = [
        "// Generated by tools/make_icons.py - do not edit; edit the script (or",
        "// the .hvif files in Icon-O-Matic and convert them back) instead.",
        "// HVIF vector icons for the transport and track-strip buttons.",
        "#pragma once",
        "",
        "#include <SupportDefs.h>",
        "",
        "// clang-format off",
        "",
    ]
    for name, make in ICONS.items():
        data = make().encode()
        with open(os.path.join(outdir, name + ".hvif"), "wb") as f:
            f.write(data)
        lines.append("static const uint8 kIcon_%s[] = {" % name)
        for k in range(0, len(data), 16):
            lines.append("    " + ", ".join("0x%02x" % b for b in data[k:k + 16]) + ",")
        lines.append("};")
        lines.append("")
        print("%-14s %4d bytes" % (name, len(data)))
    lines.append("// clang-format on")
    lines.append("")
    with open(os.path.join(outdir, "IconData.h"), "w") as f:
        f.write("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
