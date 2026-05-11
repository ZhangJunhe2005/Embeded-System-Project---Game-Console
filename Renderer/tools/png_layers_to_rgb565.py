#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
from PIL import Image

LAYER_NAMES = ("leftwall", "center", "rightwall")
WINDOW_WIDTHS = {"leftwall": 72, "center": 96, "rightwall": 72}
SOURCE_HEIGHT = 360


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def swap16(v: int) -> int:
    return ((v & 0xFF) << 8) | (v >> 8)


def load_and_validate(path: Path, min_width: int, expected_height: int) -> Image.Image:
    img = Image.open(path).convert("RGB")
    w, h = img.size
    if h != expected_height:
        raise ValueError(f"{path.name} height mismatch: got {h}, expected {expected_height}")
    if w < min_width:
        raise ValueError(f"{path.name} width too small: got {w}, needs >= {min_width}")
    return img


def image_to_rgb565(img: Image.Image) -> list[int]:
    px = img.load()
    w, h = img.size
    data: list[int] = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            # ST7789 driver sends bytes in memory order via 8-bit DMA.
            # Store byte-swapped RGB565 so bus order becomes high-byte first.
            data.append(swap16(rgb888_to_rgb565(r, g, b)))
    return data


def write_array(f, name: str, data: list[int], width: int):
    f.write(f"const uint16_t {name}[] = {{\n")
    per_line = 12
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        vals = ", ".join(f"0x{v:04X}" for v in chunk)
        f.write(f"    {vals},\n")
    f.write("};\n\n")
    f.write(f"const uint16_t {name}_width = {width};\n")
    f.write(f"const uint16_t {name}_height = {len(data)//width};\n\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-dir", required=True)
    ap.add_argument("--out-h", required=True)
    ap.add_argument("--out-c", required=True)
    args = ap.parse_args()

    input_dir = Path(args.input_dir)
    out_h = Path(args.out_h)
    out_c = Path(args.out_c)

    images = {}
    for key in LAYER_NAMES:
        images[key] = load_and_validate(input_dir / f"{key}.png", WINDOW_WIDTHS[key], SOURCE_HEIGHT)

    rgb = {k: image_to_rgb565(v) for k, v in images.items()}

    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_c.parent.mkdir(parents=True, exist_ok=True)

    with out_h.open("w", encoding="ascii", newline="\n") as h:
        h.write("#pragma once\n#include <stdint.h>\n\n")
        h.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        for key in LAYER_NAMES:
            h.write(f"extern const uint16_t {key}_rgb565[];\n")
            h.write(f"extern const uint16_t {key}_rgb565_width;\n")
            h.write(f"extern const uint16_t {key}_rgb565_height;\n\n")
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
        h.write("#define LAYER_SOURCE_H 360\n")
        h.write("#define LAYER_Y_SHIFT_MAX (LAYER_SOURCE_H - LAYER_SCREEN_H)\n\n")
        h.write("#ifdef __cplusplus\n}\n#endif\n")

    with out_c.open("w", encoding="ascii", newline="\n") as c:
        c.write('#include "LayerBackground.h"\n\n')
        write_array(c, "leftwall_rgb565", rgb["leftwall"], images["leftwall"].size[0])
        write_array(c, "center_rgb565", rgb["center"], images["center"].size[0])
        write_array(c, "rightwall_rgb565", rgb["rightwall"], images["rightwall"].size[0])

    print(f"Generated:\n  {out_h}\n  {out_c}")


if __name__ == "__main__":
    main()
