#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Tuple

from PIL import Image

LAYER_NAMES = ("leftwall", "center", "rightwall")
WINDOW_WIDTHS = {"leftwall": 72, "center": 96, "rightwall": 72}


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_and_validate(path: Path, min_width: int, expected_height: int | None) -> Image.Image:
    img = Image.open(path).convert("RGB")
    w, h = img.size
    if expected_height is not None and h != expected_height:
        raise ValueError(f"{path.name} height mismatch: got {h}, expected {expected_height}")
    if w < min_width:
        raise ValueError(f"{path.name} width too small: got {w}, needs >= {min_width}")
    return img


def quantize_layer(img: Image.Image, colors: int) -> Tuple[List[int], List[int], int, int]:
    pal_img = img.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = list(pal_img.tobytes())

    raw_pal = pal_img.getpalette()
    if raw_pal is None:
        raise ValueError("quantize returned no palette")

    lut: List[int] = [0] * 256
    for i in range(256):
        r = raw_pal[i * 3 + 0]
        g = raw_pal[i * 3 + 1]
        b = raw_pal[i * 3 + 2]
        lut[i] = rgb888_to_rgb565(r, g, b)

    return idx, lut, pal_img.width, pal_img.height


def write_index_array(f, name: str, data: List[int], width: int) -> None:
    f.write(f"const uint8_t {name}[] = {{\n")
    per_line = 24
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        vals = ", ".join(f"0x{v:02X}" for v in chunk)
        f.write(f"    {vals},\n")
    f.write("};\n\n")
    f.write(f"const uint16_t {name}_width = {width};\n")
    f.write(f"const uint16_t {name}_height = {len(data) // width};\n\n")


def write_lut_array(f, name: str, data: List[int]) -> None:
    f.write(f"const uint16_t {name}[256] = {{\n")
    per_line = 12
    for i in range(0, 256, per_line):
        chunk = data[i:i + per_line]
        vals = ", ".join(f"0x{v:04X}" for v in chunk)
        f.write(f"    {vals},\n")
    f.write("};\n\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-dir", required=True)
    ap.add_argument("--out-h", required=True)
    ap.add_argument("--out-c", required=True)
    ap.add_argument("--colors", type=int, default=256)
    ap.add_argument("--source-height", type=int, default=0,
                    help="Expected source height. 0 means auto-detect from first layer.")
    args = ap.parse_args()

    if args.colors < 2 or args.colors > 256:
        raise ValueError("--colors must be in [2, 256]")

    input_dir = Path(args.input_dir)
    out_h = Path(args.out_h)
    out_c = Path(args.out_c)

    images = {}
    expected_height = args.source_height if args.source_height > 0 else None
    for key in LAYER_NAMES:
        img = load_and_validate(input_dir / f"{key}.png", WINDOW_WIDTHS[key], expected_height)
        images[key] = img
        if expected_height is None:
            expected_height = img.size[1]

    if expected_height is None:
        raise ValueError("No input layers found")

    for key in LAYER_NAMES:
        if images[key].size[1] != expected_height:
            raise ValueError("All layers must have the same height")

    idx_data = {}
    lut_data = {}
    for key in LAYER_NAMES:
        idx, lut, _, _ = quantize_layer(images[key], args.colors)
        idx_data[key] = idx
        lut_data[key] = lut

    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_c.parent.mkdir(parents=True, exist_ok=True)

    with out_h.open("w", encoding="ascii", newline="\n") as h:
        h.write("#pragma once\n#include <stdint.h>\n\n")
        h.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        for key in LAYER_NAMES:
            h.write(f"extern const uint8_t {key}_idx8[];\n")
            h.write(f"extern const uint16_t {key}_idx8_width;\n")
            h.write(f"extern const uint16_t {key}_idx8_height;\n")
            h.write(f"extern const uint16_t {key}_lut565[256];\n\n")
        h.write("#define LAYER_WIN_LEFT   72\n")
        h.write("#define LAYER_WIN_CENTER 96\n")
        h.write("#define LAYER_WIN_RIGHT  72\n\n")
        h.write("#define LAYER_DRAW_X_LEFT   0\n")
        h.write("#define LAYER_DRAW_X_CENTER 72\n")
        h.write("#define LAYER_DRAW_X_RIGHT  168\n\n")
        h.write(f"#define LAYER_SRC_W_LEFT   {images['leftwall'].size[0]}\n")
        h.write(f"#define LAYER_SRC_W_CENTER {images['center'].size[0]}\n")
        h.write(f"#define LAYER_SRC_W_RIGHT  {images['rightwall'].size[0]}\n")
        h.write("#define LAYER_SHIFT_MAX_LEFT   (LAYER_SRC_W_LEFT - LAYER_WIN_LEFT)\n")
        h.write("#define LAYER_SHIFT_MAX_CENTER (LAYER_SRC_W_CENTER - LAYER_WIN_CENTER)\n")
        h.write("#define LAYER_SHIFT_MAX_RIGHT  (LAYER_SRC_W_RIGHT - LAYER_WIN_RIGHT)\n\n")
        h.write("#define LAYER_SCREEN_H 240\n")
        h.write(f"#define LAYER_SOURCE_H {expected_height}\n")
        h.write("#define LAYER_Y_SHIFT_MAX (LAYER_SOURCE_H - LAYER_SCREEN_H)\n\n")
        h.write("#ifdef __cplusplus\n}\n#endif\n")

    with out_c.open("w", encoding="ascii", newline="\n") as c:
        c.write('#include "LayerBackgroundIndexed.h"\n\n')
        for key in LAYER_NAMES:
            write_index_array(c, f"{key}_idx8", idx_data[key], images[key].size[0])
            write_lut_array(c, f"{key}_lut565", lut_data[key])

    total_idx = sum(len(idx_data[k]) for k in LAYER_NAMES)
    total_lut = 256 * 2 * len(LAYER_NAMES)
    print(f"Generated:\n  {out_h}\n  {out_c}")
    print(f"INDEX8+LUT bytes: {total_idx + total_lut} (index={total_idx}, lut={total_lut})")


if __name__ == "__main__":
    main()
