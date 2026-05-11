#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def write_index_array(f, name: str, data: list[int], width: int) -> None:
    f.write(f"const uint8_t {name}[] = {{\n")
    per_line = 24
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        f.write("    " + ", ".join(f"0x{v:02X}" for v in chunk) + ",\n")
    f.write("};\n\n")
    f.write(f"const uint16_t {name}_width = {width};\n")
    f.write(f"const uint16_t {name}_height = {len(data) // width};\n\n")


def write_lut_array(f, name: str, data: list[int]) -> None:
    f.write(f"const uint16_t {name}[256] = {{\n")
    per_line = 12
    for i in range(0, 256, per_line):
        chunk = data[i:i + per_line]
        f.write("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
    f.write("};\n\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--out-h", required=True)
    ap.add_argument("--out-c", required=True)
    ap.add_argument("--symbol", default="r99")
    ap.add_argument("--alpha-threshold", type=int, default=128)
    args = ap.parse_args()

    src = Image.open(args.input).convert("RGBA")
    w, h = src.size
    alpha_threshold = max(0, min(255, args.alpha_threshold))

    rgb_for_quant = Image.new("RGB", (w, h), (0, 0, 0))
    src_px = src.load()
    rgb_px = rgb_for_quant.load()
    opaque_mask: list[bool] = []

    for y in range(h):
        for x in range(w):
            r, g, b, a = src_px[x, y]
            opaque = a >= alpha_threshold
            opaque_mask.append(opaque)
            if opaque:
                rgb_px[x, y] = (r, g, b)

    pal_img = rgb_for_quant.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal_idx = list(pal_img.tobytes())
    raw_pal = pal_img.getpalette()
    if raw_pal is None:
        raise ValueError("quantize returned no palette")

    idx: list[int] = [0] * (w * h)
    for i, opaque in enumerate(opaque_mask):
        if opaque:
            idx[i] = pal_idx[i] + 1

    lut: list[int] = [0] * 256
    for i in range(255):
        r = raw_pal[i * 3 + 0]
        g = raw_pal[i * 3 + 1]
        b = raw_pal[i * 3 + 2]
        lut[i + 1] = rgb888_to_rgb565(r, g, b)

    out_h = Path(args.out_h)
    out_c = Path(args.out_c)
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_c.parent.mkdir(parents=True, exist_ok=True)

    guard = f"{args.symbol.upper()}_GUN_INDEXED_H"
    with out_h.open("w", encoding="ascii", newline="\n") as hfile:
        hfile.write(f"#ifndef {guard}\n#define {guard}\n\n")
        hfile.write("#include <stdint.h>\n\n")
        hfile.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        hfile.write(f"extern const uint8_t {args.symbol}_idx8[];\n")
        hfile.write(f"extern const uint16_t {args.symbol}_idx8_width;\n")
        hfile.write(f"extern const uint16_t {args.symbol}_idx8_height;\n")
        hfile.write(f"extern const uint16_t {args.symbol}_lut565[256];\n\n")
        hfile.write(f"#define {args.symbol.upper()}_TRANSPARENT_INDEX 0\n\n")
        hfile.write("#ifdef __cplusplus\n}\n#endif\n\n")
        hfile.write(f"#endif /* {guard} */\n")

    with out_c.open("w", encoding="ascii", newline="\n") as cfile:
        cfile.write(f'#include "{out_h.name}"\n\n')
        write_index_array(cfile, f"{args.symbol}_idx8", idx, w)
        write_lut_array(cfile, f"{args.symbol}_lut565", lut)

    opaque_count = sum(1 for v in opaque_mask if v)
    print(f"Generated {out_h} and {out_c}")
    print(f"{w}x{h}, opaque={opaque_count}, bytes={len(idx) + 512}")


if __name__ == "__main__":
    main()
