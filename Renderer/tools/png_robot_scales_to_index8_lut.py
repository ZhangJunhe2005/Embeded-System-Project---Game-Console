#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


SCALES = {
    -2: 0.86,
    -1: 0.93,
    0: 1.00,
    1: 1.07,
    2: 1.14,
    3: 1.21,
}

ULTIMATE_SCALES = [0.56, 0.64, 0.72, 0.80, 0.86, 0.90]
ULTIMATE_SCALE_Y = [160, 172, 184, 194, 200, 204]


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inputs", nargs="+", default=["robot1.png"])
    ap.add_argument("--input", help="legacy alias for a single input image")
    ap.add_argument("--out-h", default="Renderer/Robot1Indexed.h")
    ap.add_argument("--out-c", default="Renderer/Robot1Indexed.c")
    ap.add_argument("--symbol", default="robot1")
    ap.add_argument("--alpha-threshold", type=int, default=128)
    args = ap.parse_args()

    input_paths = [args.input] if args.input else args.inputs
    sources = [Image.open(p).convert("RGBA") for p in input_paths]
    alpha_threshold = max(0, min(255, args.alpha_threshold))

    frame_names: list[str] = []
    frames: list[tuple[str, int, int, Image.Image]] = []
    normal_offset = 0
    for pose_i, src in enumerate(sources):
        for grid_y, scale in SCALES.items():
            w = max(1, round(src.width * scale))
            h = max(1, round(src.height * scale))
            frame_names.append(f"normal{pose_i}_y{grid_y}")
            frames.append(("normal", pose_i, grid_y, src.resize((w, h), Image.Resampling.NEAREST)))

    die_offset = len(frames)
    die_paths = sorted(Path("robot").glob("robotdie*.png"), key=lambda p: int("".join(c for c in p.stem if c.isdigit()) or "0"))
    for die_i, path in enumerate(die_paths):
        src = Image.open(path).convert("RGBA")
        for scale_i, scale in enumerate(ULTIMATE_SCALES):
            w = max(1, round(src.width * scale))
            h = max(1, round(src.height * scale))
            frame_names.append(f"die{die_i}_s{scale_i}")
            frames.append(("die", die_i, scale_i, src.resize((w, h), Image.Resampling.NEAREST)))

    ultimate_offset = len(frames)
    ultimate_paths: list[Path] = []
    ultimate_dir = Path("robot/ultimate")
    for path in sorted(ultimate_dir.glob("dengqiang*.png"), key=lambda p: int("".join(c for c in p.stem if c.isdigit()) or "0")):
        ultimate_paths.append(path)
    for name in ("luodi.png", "lurch.png"):
        path = ultimate_dir / name
        if path.exists():
            ultimate_paths.append(path)
    for ult_i, path in enumerate(ultimate_paths):
        src = Image.open(path).convert("RGBA")
        for scale_i, scale in enumerate(ULTIMATE_SCALES):
            w = max(1, round(src.width * scale))
            h = max(1, round(src.height * scale))
            frame_names.append(f"{path.stem}_s{scale_i}")
            frames.append(("ultimate", ult_i, scale_i, src.resize((w, h), Image.Resampling.NEAREST)))

    atlas_w = sum(im.width for _kind, _pose_i, _grid_y, im in frames)
    atlas_h = max(im.height for _kind, _pose_i, _grid_y, im in frames)
    atlas_rgb = Image.new("RGB", (atlas_w, atlas_h), (0, 0, 0))
    opaque: list[bool] = [False] * (atlas_w * atlas_h)
    offsets: list[int] = []

    ox = 0
    for _kind, _pose_i, _grid_y, im in frames:
        offsets.append(ox)
        px = im.load()
        out_px = atlas_rgb.load()
        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = px[x, y]
                if a >= alpha_threshold:
                    out_px[ox + x, y] = (r, g, b)
                    opaque[y * atlas_w + ox + x] = True
        ox += im.width

    pal_img = atlas_rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal_idx = list(pal_img.tobytes())
    raw_pal = pal_img.getpalette()
    if raw_pal is None:
        raise SystemExit("quantize returned no palette")

    lut = [0] * 256
    for i in range(255):
        lut[i + 1] = rgb888_to_rgb565(raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2])

    frame_data: list[list[int]] = []
    for frame_i, (_kind, _pose_i, _grid_y, im) in enumerate(frames):
        ox = offsets[frame_i]
        data: list[int] = []
        for y in range(im.height):
            for x in range(im.width):
                atlas_i = y * atlas_w + ox + x
                data.append(pal_idx[atlas_i] + 1 if opaque[atlas_i] else 0)
        frame_data.append(data)

    out_h = Path(args.out_h)
    out_c = Path(args.out_c)
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_c.parent.mkdir(parents=True, exist_ok=True)
    sym = args.symbol
    guard = f"{sym.upper()}_INDEXED_H"

    with out_h.open("w", encoding="ascii", newline="\n") as h:
        h.write(f"#ifndef {guard}\n#define {guard}\n\n")
        h.write("#include <stdint.h>\n\n")
        h.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        h.write(f"#define {sym.upper()}_FRAME_COUNT {len(frames)}\n")
        h.write(f"#define {sym.upper()}_POSE_COUNT {len(sources)}\n")
        h.write(f"#define {sym.upper()}_SCALE_COUNT {len(SCALES)}\n")
        h.write(f"#define {sym.upper()}_NORMAL_OFFSET {normal_offset}\n")
        h.write(f"#define {sym.upper()}_DIE_OFFSET {die_offset}\n")
        h.write(f"#define {sym.upper()}_DIE_FRAME_COUNT {len(die_paths)}\n")
        h.write(f"#define {sym.upper()}_ULTIMATE_OFFSET {ultimate_offset}\n")
        h.write(f"#define {sym.upper()}_ULTIMATE_FRAME_COUNT {len(ultimate_paths)}\n")
        h.write(f"#define {sym.upper()}_ULTIMATE_SCALE_COUNT {len(ULTIMATE_SCALES)}\n")
        h.write(f"#define {sym.upper()}_TRANSPARENT_INDEX 0\n\n")
        h.write(f"extern const uint8_t * const {sym}_frames[{sym.upper()}_FRAME_COUNT];\n")
        h.write(f"extern const int8_t {sym}_grid_y[{sym.upper()}_SCALE_COUNT];\n")
        h.write(f"extern const int16_t {sym}_ultimate_scale_y[{sym.upper()}_ULTIMATE_SCALE_COUNT];\n")
        h.write(f"extern const uint16_t {sym}_widths[{sym.upper()}_FRAME_COUNT];\n")
        h.write(f"extern const uint16_t {sym}_heights[{sym.upper()}_FRAME_COUNT];\n")
        h.write(f"extern const uint16_t {sym}_lut565[256];\n\n")
        h.write("#ifdef __cplusplus\n}\n#endif\n\n")
        h.write(f"#endif /* {guard} */\n")

    with out_c.open("w", encoding="ascii", newline="\n") as c:
        c.write(f'#include "{out_h.name}"\n\n')
        for i, data in enumerate(frame_data):
            c.write(f"static const uint8_t {sym}_frame_{i}[] = {{\n")
            for j in range(0, len(data), 24):
                c.write("    " + ", ".join(f"0x{v:02X}" for v in data[j:j + 24]) + ",\n")
            c.write("};\n\n")
        c.write(f"const uint8_t * const {sym}_frames[{sym.upper()}_FRAME_COUNT] = {{\n")
        for i in range(len(frame_data)):
            c.write(f"    {sym}_frame_{i},\n")
        c.write("};\n\n")
        c.write(f"const int8_t {sym}_grid_y[{sym.upper()}_SCALE_COUNT] = {{ ")
        c.write(", ".join(str(grid_y) for grid_y in SCALES.keys()))
        c.write(" };\n")
        c.write(f"const int16_t {sym}_ultimate_scale_y[{sym.upper()}_ULTIMATE_SCALE_COUNT] = {{ ")
        c.write(", ".join(str(v) for v in ULTIMATE_SCALE_Y))
        c.write(" };\n")
        c.write(f"const uint16_t {sym}_widths[{sym.upper()}_FRAME_COUNT] = {{ ")
        c.write(", ".join(str(im.width) for _kind, _pose_i, _grid_y, im in frames))
        c.write(" };\n")
        c.write(f"const uint16_t {sym}_heights[{sym.upper()}_FRAME_COUNT] = {{ ")
        c.write(", ".join(str(im.height) for _kind, _pose_i, _grid_y, im in frames))
        c.write(" };\n\n")
        c.write(f"const uint16_t {sym}_lut565[256] = {{\n")
        for i in range(0, 256, 12):
            c.write("    " + ", ".join(f"0x{v:04X}" for v in lut[i:i + 12]) + ",\n")
        c.write("};\n")

    total_bytes = sum(len(data) for data in frame_data) + 512
    print(f"Generated {out_h} and {out_c}")
    print("inputs=" + ", ".join(str(p) for p in input_paths))
    print(f"normal_offset={normal_offset} die_offset={die_offset} ultimate_offset={ultimate_offset}")
    print("frames=" + ", ".join(f"{name}:{im.width}x{im.height}" for name, (_kind, _pose, _gy, im) in zip(frame_names, frames)))
    print(f"bytes={total_bytes}")


if __name__ == "__main__":
    main()
