#!/usr/bin/env python3
"""Keep SquareLine's image assets in a format this firmware can actually draw.

Indexed images (I1/I2/I4/I8) are unusable here the moment they are scaled or
rotated. The sw renderer has no indexed support at all, so with
``LV_BIN_DECODER_RAM_LOAD`` off ``decode_indexed()`` returns without decoding and
LVGL falls back to a line-by-line ``lv_bin_decoder_get_area()`` reader that cannot
serve a transform:

  scaling up    the requested area reaches outside the source, wrapping a
                uint32_t byte offset -> reads far out of bounds -> load fault
  scaling down  the area stays in bounds, but the reader hands the renderer
                1px-tall strips that transform to sub-pixel and vanish
  rotation      same as scaling down (hence arrow_left.png existing instead of
                rotating arrow.png)

There is no way to stop this at source. SquareLine infers the format from the source
PNG's colour count -- 256 or fewer distinct colours exports as indexed (measured:
Lock 247 -> I8, arrow 196 -> I8, seat 772 -> RGB565A8, turbo 1105 -> RGB565A8) -- and
a single-ink alpha mask is one RGB times at most 256 alpha levels, so it is always
under that bound. SquareLine cannot emit A8 at all. Since ``import_ui.ps1`` mirrors
with ``robocopy /MIR``, every import brings the indexed originals back, so converting
on each import is the permanent answer rather than a workaround.

Usage:
  python ui_assets.py check              # report; exit 1 if anything is indexed
  python ui_assets.py check --fix        # rewrite those files to A8 in place

Stdlib only, so import_ui.ps1 can always run it. Everything prints to stdout, because
the caller runs under ``$ErrorActionPreference = "Stop"`` where stderr would look
like a failure.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import shutil
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
IMAGE_DIR = REPO_ROOT / "main" / "ui" / "images"
UI_SOURCE_DIRS = ("screens", "components")

# LV_COLOR_INDEXED_PALETTE_SIZE, src/misc/lv_color.h
PALETTE_ENTRIES = {"I1": 2, "I2": 4, "I4": 16, "I8": 256}
INDEXED_BPP = {"I1": 1, "I2": 2, "I4": 4, "I8": 8}

MAP_OPEN = "_map[] = {"


class ConversionError(Exception):
    """An indexed image that cannot become A8 without losing information."""


@dataclasses.dataclass
class ImageFile:
    path: pathlib.Path
    prefix: str  # everything before the map body
    body: str  # the hex bytes themselves
    suffix: str  # the descriptor and anything after it
    cf: str  # colour format, without the LV_COLOR_FORMAT_ prefix
    w: int
    h: int

    @property
    def indexed(self) -> bool:
        return self.cf in INDEXED_BPP


def parse_image(path: pathlib.Path) -> ImageFile | None:
    """Split a SquareLine image .c into its map body and descriptor.

    Returns None for files this tool has no opinion on -- notably the SVG-derived
    ones, which declare no explicit .header.cf and size themselves with sizeof().
    """
    text = path.read_text(encoding="utf-8")
    if MAP_OPEN not in text:
        return None

    prefix, rest = text.split(MAP_OPEN, 1)
    if "};" not in rest:
        return None
    body, suffix = rest.split("};", 1)

    cf = re.search(r"\.header\.cf\s*=\s*LV_COLOR_FORMAT_(\w+)", suffix)
    w = re.search(r"\.header\.w\s*=\s*(\d+)", suffix)
    h = re.search(r"\.header\.h\s*=\s*(\d+)", suffix)
    if not (cf and w and h):
        return None

    return ImageFile(path, prefix, body, suffix, cf.group(1),
                     int(w.group(1)), int(h.group(1)))


def map_bytes(body: str) -> bytes:
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", body))


def decode_indexed(img: ImageFile, data: bytes) -> tuple[list[tuple[int, ...]], list[int]]:
    """Split an indexed map into its palette and one palette index per pixel.

    The palette is BGRA, 4 bytes per entry, at the front of the map. Rows are
    packed at (w * bpp + 7) >> 3 bytes -- LV_DRAW_BUF_STRIDE_ALIGN is 1 here and
    img_width_to_stride() uses the same formula -- so a sub-byte format can leave
    unused bits at the end of a row. Walk row by row rather than treating the
    pixels as one flat bit stream.
    """
    entries = PALETTE_ENTRIES[img.cf]
    bpp = INDEXED_BPP[img.cf]
    raw, pixels = data[: entries * 4], data[entries * 4:]

    stride = (img.w * bpp + 7) >> 3
    if len(pixels) != stride * img.h:
        raise ConversionError(
            "pixel data is %d bytes, expected %d for %dx%d %s"
            % (len(pixels), stride * img.h, img.w, img.h, img.cf))

    palette = [tuple(raw[4 * i: 4 * i + 4]) for i in range(entries)]
    mask = (1 << bpp) - 1

    idx = []
    for y in range(img.h):
        row = pixels[y * stride: (y + 1) * stride]
        for x in range(img.w):
            bit = x * bpp
            shift = 8 - bpp - (bit & 7)
            idx.append((row[bit >> 3] >> shift) & mask)
    return palette, idx


def to_a8(palette: list[tuple[int, ...]], idx: list[int]) -> bytes:
    """Flatten to one alpha byte per pixel. Only lossless for a single-ink palette."""
    alpha_of = bytes(entry[3] for entry in palette)
    return bytes(alpha_of[i] for i in idx)


def to_rgb565a8(palette: list[tuple[int, ...]], idx: list[int]) -> bytes:
    """Build an RGB565 plane followed by an A8 plane, the layout LVGL 9 expects.

    lv_draw_sw reads RGB565A8 as w*h*2 bytes of little-endian RGB565 followed by
    w*h bytes of alpha (hence data_size = w*h*3), both unpremultiplied. Checked
    against SquareLine's own RGB565A8 exports, which also park 0xffff in the
    colour plane wherever alpha is 0.
    """
    packed = []
    for b, g, r, a in palette:
        v = 0xffff if not a else ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)
        packed.append((v & 0xff, v >> 8))

    colour = bytearray(len(idx) * 2)
    alpha = bytearray(len(idx))
    for n, i in enumerate(idx):
        colour[2 * n], colour[2 * n + 1] = packed[i]
        alpha[n] = palette[i][3]
    return bytes(colour) + bytes(alpha)


def convert(img: ImageFile, data: bytes) -> tuple[str, bytes]:
    """Rewrite an indexed map into the cheapest format the renderer can transform.

    One ink -- an icon, a logo, any alpha mask -- is A8 at 1 byte per pixel; the
    colour comes back from the widget's image_recolor. A palette with real colour
    in it (a photo or a shaded render that happened to quantise under 256 colours)
    would lose that as A8, so it becomes RGB565A8 at 3 bytes per pixel instead:
    the format SquareLine itself picks above 256 colours, and the only other
    alpha-carrying format the sw renderer can transform. Rounding 8-bit channels
    to 5/6/5 costs nothing on screen -- LV_COLOR_DEPTH is 16.
    """
    palette, idx = decode_indexed(img, data)
    inks = {entry[:3] for entry in palette if entry[3]}
    if len(inks) <= 1:
        return "A8", to_a8(palette, idx)
    return "RGB565A8", to_rgb565a8(palette, idx)


def render(img: ImageFile, cf: str, data: bytes) -> str:
    """Rebuild the .c in `cf`, touching only the map body and the changed fields.

    Symbol names, include guards and LV_ATTRIBUTE_* boilerplate are preserved
    verbatim -- ui.h and the screen sources refer to them by name.
    """
    suffix = re.sub(r"\.header\.cf\s*=\s*LV_COLOR_FORMAT_\w+",
                    ".header.cf = LV_COLOR_FORMAT_" + cf, img.suffix, count=1)
    suffix = re.sub(r"\.data_size\s*=\s*[^,\n]+,",
                    ".data_size = %d," % len(data), suffix, count=1)

    rows = ["  " + ", ".join("0x%02x" % b for b in data[o: o + 16]) + ","
            for o in range(0, len(data), 16)]
    return img.prefix + MAP_OPEN + "\n" + "\n".join(rows) + "\n" + "};" + suffix


def suspicious_scales() -> list[str]:
    """Flag lv_image_set_scale() values that look like percentages.

    LVGL's unity is LV_SCALE_NONE = 256, but SquareLine writes the field raw, so
    an intended 80% arrives as 80 and renders at 31%.
    """
    notes = []
    for sub in UI_SOURCE_DIRS:
        for path in sorted((REPO_ROOT / "main" / "ui" / sub).glob("*.c")):
            for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                m = re.search(r"lv_image_set_scale\(\s*(\w+)\s*,\s*(\d+)\s*\)", line)
                if m and int(m.group(2)) < 100:
                    value = int(m.group(2))
                    notes.append(
                        "  %s:%d  %s scale=%d -> %d%% (unity is 256; %d%% would be %d)"
                        % (path.relative_to(REPO_ROOT).as_posix(), n, m.group(1),
                           value, round(value * 100 / 256), value,
                           round(value * 256 / 100)))
    return notes


def cmd_check(fix: bool) -> int:
    if not IMAGE_DIR.is_dir():
        print("ui_assets: no such directory: %s" % IMAGE_DIR)
        return 0

    indexed, failed, fixed = [], [], []
    for path in sorted(IMAGE_DIR.glob("*.c")):
        img = parse_image(path)
        if img is None or not img.indexed:
            continue
        indexed.append(img)

        if not fix:
            continue
        try:
            cf, data = convert(img, map_bytes(img.body))
        except ConversionError as exc:
            failed.append((img, exc))
            continue
        # Keep the untouched export beside the file. robocopy /MIR deletes it on
        # the next import (it is not in the export), which is exactly the window
        # it is useful for, and .orig is never picked up by SRC_DIRS.
        shutil.copy2(path, path.with_name(path.name + ".orig"))
        path.write_text(render(img, cf, data), encoding="utf-8", newline="")
        fixed.append((img, cf, len(data)))

    if indexed and not fix:
        # Report mode. In fix mode the per-file "converted" lines below already
        # name every one of these, so repeating the list just doubles the noise.
        print("WARNING: %d image(s) use an indexed colour format, which faults or"
              % len(indexed))
        print("draws nothing as soon as the image is scaled or rotated:")
        for img in indexed:
            print("  %-28s %s  %dx%d"
                  % (img.path.name, img.cf, img.w, img.h))
    elif indexed:
        print("Converting %d indexed image(s):" % len(indexed))

    for img, cf, size in fixed:
        print("  converted %s -> %s (%d bytes, original kept as %s.orig)"
              % (img.path.name, cf, size, img.path.name))
    for img, exc in failed:
        print("  CANNOT convert %s: %s" % (img.path.name, exc))

    notes = suspicious_scales()
    if notes:
        print("")
        print("NOTE: lv_image_set_scale() values below 100 are almost certainly"
              " meant as percentages:")
        print("\n".join(notes))

    unresolved = bool(failed) or (bool(indexed) and not fix)
    return 1 if unresolved else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n", maxsplit=1)[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    check = sub.add_parser("check", help="audit main/ui/images/*.c")
    check.add_argument("--fix", action="store_true",
                       help="rewrite indexed images in place as A8, or RGB565A8 "
                            "when the palette carries colour")

    args = parser.parse_args()
    return cmd_check(args.fix)


if __name__ == "__main__":
    sys.exit(main())
