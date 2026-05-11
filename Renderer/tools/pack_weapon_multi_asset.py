#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

MAGIC = b"APXWPN01"
VERSION = 5
HEADER_SIZE = 64
LUT_SIZE = 512
ALPHA_THRESHOLD = 255

ANIMS = {
    "KUWU_DRAW": 0,
    "KUWU_STOW": 1,
    "R99_DRAW": 2,
    "R99_STOW": 3,
    "R99_RELOAD": 4,
    "R99_ADS": 5,
    "FUZHU_DRAW": 6,
    "FUZHU_STOW": 7,
    "FUZHU_RELOAD": 8,
    "FUZHU_ADS": 9,
    "FUZHU_FIRE": 10,
    "HEPING_DRAW": 11,
    "HEPING_STOW": 12,
    "HEPING_RELOAD": 13,
    "HEPING_PUMP": 14,
}


@dataclass
class AnimSpec:
    name: str
    directory: Path
    glob_pat: str
    frame_ms: int = 40


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def path_number(path: Path) -> int:
    return int("".join(c for c in path.stem if c.isdigit()) or "0")


def blank_numbers(seq_dir: Path) -> set[int]:
    nums: set[int] = set()
    for path in seq_dir.glob("blank*.txt"):
        stem_digits = "".join(c for c in path.stem if c.isdigit())
        if stem_digits:
            nums.add(int(stem_digits))
        text = path.read_text(encoding="utf-8", errors="ignore").replace(",", "\n")
        for part in text.split():
            part = part.strip()
            if "-" in part:
                a, b = part.split("-", 1)
                if a.strip().isdigit() and b.strip().isdigit():
                    start, end = int(a), int(b)
                    nums.update(range(min(start, end), max(start, end) + 1))
            elif part.isdigit():
                nums.add(int(part))
    return nums


def sorted_paths(directory: Path, glob_pat: str) -> list[Path]:
    return sorted(directory.glob(glob_pat), key=path_number)


def build_timeline(paths: list[Path], global_indices: dict[Path, int], blanks: set[int]) -> list[int]:
    if not paths and not blanks:
        return []
    by_num = {path_number(p): p for p in paths}
    first = min(set(by_num) | blanks)
    last = max(set(by_num) | blanks)
    out: list[int] = []
    prev = -1
    for n in range(first, last + 1):
        if n in blanks:
            out.append(-1)
        elif n in by_num:
            prev = global_indices[by_num[n]]
            out.append(prev)
        else:
            out.append(prev)
    return out


def normalize_frame(im: Image.Image, width: int, height: int) -> Image.Image:
    if im.size == (width, height):
        return im
    if im.width > width or im.height > height:
        raise SystemExit(f"image {im.size} is larger than common frame {width}x{height}")
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    out.alpha_composite(im, ((width - im.width) // 2, height - im.height))
    return out


def mask_bbox(mask: list[bool], width: int, height: int, margin: int) -> tuple[int, int, int, int]:
    min_x, min_y = width, height
    max_x, max_y = -1, -1
    for y in range(height):
        row = y * width
        for x in range(width):
            if mask[row + x]:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    if max_x < min_x or max_y < min_y:
        return (0, 0, 0, 0)
    return (
        max(0, min_x - margin),
        max(0, min_y - margin),
        min(width, max_x + margin + 1),
        min(height, max_y + margin + 1),
    )


def default_specs() -> list[AnimSpec]:
    return [
        AnimSpec("KUWU_DRAW", Path("kuwu_animation/qiedao"), "qiedao-*.png", 45),
        AnimSpec("KUWU_STOW", Path("kuwu_animation/shoudao"), "qieqiang*.png", 40),
        AnimSpec("R99_DRAW", Path("gun_animation/r99qieqiang"), "qieqiang*.png", 40),
        AnimSpec("R99_STOW", Path("gun_animation/r99qiedao"), "shouqiang*.png", 40),
        AnimSpec("R99_RELOAD", Path("gun_animation/r99huandan"), "r99huandan*.png", 40),
        AnimSpec("R99_ADS", Path("gun_animation/r99kaijing"), "r99kaijing*.png", 35),
        AnimSpec("FUZHU_DRAW", Path("gun_animation/fuzhuqieqiang"), "fuzhuqieqiang*.png", 40),
        AnimSpec("FUZHU_STOW", Path("gun_animation/fuzhuqiedao"), "fuzhuqiedao*.png", 40),
        AnimSpec("FUZHU_RELOAD", Path("gun_animation/fuzhuhuandan"), "fuzhuhuandan*.png", 40),
        AnimSpec("FUZHU_ADS", Path("gun_animation/fuzhukaijing"), "fuzhukaijing*.png", 35),
        AnimSpec("FUZHU_FIRE", Path("gun_animation/fuzhukaiqiang"), "fuzhukaiqiang*.png", 40),
        AnimSpec("HEPING_DRAW", Path("gun_animation/hepingqieqiang"), "hepingqieqiang*.png", 40),
        AnimSpec("HEPING_STOW", Path("gun_animation/hepingqiedao"), "hepingqiedao*.png", 40),
        AnimSpec("HEPING_RELOAD", Path("gun_animation/hepinghuandan"), "hepinghuandan*.png", 40),
        AnimSpec("HEPING_PUMP", Path("gun_animation/hepinglashuan"), "hepinglashuan*.png", 40),
    ]


def build_asset(out_path: Path, kuwu_path: Path, margin: int) -> bytes:
    specs = default_specs()
    anim_paths: dict[str, list[Path]] = {}
    paths: list[Path] = []
    for spec in specs:
        ps = sorted_paths(spec.directory, spec.glob_pat)
        if not ps:
            raise SystemExit(f"no frames for {spec.name}: {spec.directory}/{spec.glob_pat}")
        anim_paths[spec.name] = ps
        paths.extend(ps)
    paths.append(kuwu_path)
    kuwu_frame = len(paths) - 1
    global_indices = {p: i for i, p in enumerate(paths)}

    timelines: dict[str, list[int]] = {}
    for spec in specs:
        timelines[spec.name] = build_timeline(anim_paths[spec.name], global_indices, blank_numbers(spec.directory))

    raw_images = [Image.open(p).convert("RGBA") for p in paths]
    width = max(im.width for im in raw_images)
    height = max(im.height for im in raw_images)
    images = [normalize_frame(im, width, height) for im in raw_images]

    atlas = Image.new("RGB", (width, height * len(images)), (0, 0, 0))
    masks: list[list[bool]] = []
    for frame_i, im in enumerate(images):
        mask: list[bool] = []
        px = im.load()
        for y in range(height):
            for x in range(width):
                r, g, b, a = px[x, y]
                opaque = a >= ALPHA_THRESHOLD
                mask.append(opaque)
                if opaque:
                    atlas.putpixel((x, frame_i * height + y), (r, g, b))
        masks.append(mask)

    pal_img = atlas.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal_idx = list(pal_img.tobytes())
    raw_pal = pal_img.getpalette()
    if raw_pal is None:
        raise SystemExit("quantize returned no palette")

    lut = bytearray(struct.pack("<H", 0))
    for i in range(255):
        lut += struct.pack("<H", rgb888_to_rgb565(raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2]))

    lut_offset = HEADER_SIZE
    anim_table_offset = lut_offset + len(lut)
    anim_table_size = len(specs) * 16
    timeline_offset = anim_table_offset + anim_table_size
    timeline_data = bytearray()
    anim_entries = bytearray()
    for spec in specs:
        tl = timelines[spec.name]
        off = timeline_offset + len(timeline_data)
        timeline_data += b"".join(struct.pack("<h", i) for i in tl)
        real = anim_paths[spec.name]
        anim_entries += struct.pack(
            "<HHIHHHH",
            ANIMS[spec.name],
            len(tl),
            off,
            spec.frame_ms,
            0,
            global_indices[real[0]],
            len(real),
        )

    frame_meta_offset = timeline_offset + len(timeline_data)
    data_offset = frame_meta_offset + len(images) * 12
    frame_meta = bytearray()
    frames = bytearray()
    full_frame_stride = width * height
    max_frame_pixels = 0
    for frame_i, mask in enumerate(masks):
        x0, y0, x1, y1 = mask_bbox(mask, width, height, margin)
        w = x1 - x0
        h = y1 - y0
        pixel_offset = len(frames)
        if w > 0 and h > 0:
            base = frame_i * full_frame_stride
            for y in range(y0, y1):
                row = y * width
                for x in range(x0, x1):
                    i = row + x
                    frames.append((pal_idx[base + i] + 1) if mask[i] else 0)
        max_frame_pixels = max(max_frame_pixels, w * h)
        frame_meta += struct.pack("<hhHHI", x0, y0, w, h, data_offset + pixel_offset)

    body = bytes(lut) + bytes(anim_entries) + bytes(timeline_data) + bytes(frame_meta) + bytes(frames)
    body_crc = zlib.crc32(body) & 0xFFFFFFFF
    total_size = HEADER_SIZE + len(body)

    header = bytearray(HEADER_SIZE)
    header[0:8] = MAGIC
    struct.pack_into(
        "<IHHHHHHIIIII",
        header,
        8,
        VERSION,
        width,
        height,
        len(timelines["KUWU_DRAW"]),
        len(images),
        kuwu_frame,
        0xFFFF,
        max_frame_pixels,
        lut_offset,
        data_offset,
        total_size,
        body_crc,
    )
    struct.pack_into("<HHIIHH", header, 44, len(specs), 0, anim_table_offset, frame_meta_offset, len(timelines["R99_DRAW"]), len(timelines["R99_STOW"]))

    asset = bytes(header) + body
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(asset)
    print(f"wrote {out_path} ({len(asset)} bytes)")
    print(f"version=5 frames={len(images)} anims={len(specs)} size={width}x{height} max_bbox={max_frame_pixels} margin={margin} crc=0x{body_crc:08X}")
    for spec in specs:
        print(f"  {spec.name}: real={len(anim_paths[spec.name])} timeline={len(timelines[spec.name])} frame_ms={spec.frame_ms}")
    return asset


def wait_for_text(ser, needle: bytes, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(1)
        if chunk:
            buf += chunk
            print(chunk.decode("utf-8", errors="ignore"), end="", flush=True)
            if needle in buf:
                return
            if len(buf) > 4096:
                del buf[:2048]
    raise SystemExit(f"timed out waiting for board text: {needle.decode(errors='ignore')}")


def wait_for_ack(ser, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        b = ser.read(1)
        if b == b"\x06":
            return
        if b:
            print(b.decode("utf-8", errors="ignore"), end="", flush=True)
    raise SystemExit("upload stopped: timed out waiting for ACK 0x06")


def upload_asset(port: str, baud: int, asset: bytes) -> None:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for upload: pip install pyserial") from exc
    with serial.Serial(port, baudrate=baud, timeout=1, write_timeout=2) as ser:
        time.sleep(0.8)
        ser.reset_input_buffer()
        print(f"waiting for board on {port} at {baud} baud")
        wait_for_text(ser, b"Waiting for 64-byte header at 115200 8N1...", 20.0)
        time.sleep(0.2)
        print("\nsending 64-byte header")
        ser.write(asset[:HEADER_SIZE])
        ser.flush()
        print("waiting for flash erase to finish")
        wait_for_text(ser, b"[ASSET] ready for body", 120.0)
        print(f"\nuploading body {len(asset) - HEADER_SIZE} bytes")
        sent = HEADER_SIZE
        while sent < len(asset):
            page_room = 256 - (sent % 256)
            chunk_size = min(page_room, len(asset) - sent)
            ser.write(asset[sent:sent + chunk_size])
            ser.flush()
            wait_for_ack(ser, 5.0)
            sent += chunk_size
            if ((sent - HEADER_SIZE) % 65536 == 0) or sent == len(asset):
                print(f"sent {sent}/{len(asset)}")
        ser.flush()
        print("upload complete; waiting for board verification")
        wait_for_text(ser, b"[ASSET] upload/program OK", 45.0)
        print("\nboard verified upload OK")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="Renderer/qiedao_asset.bin")
    ap.add_argument("--kuwu", default="kuwu.png")
    ap.add_argument("--bbox-margin", type=int, default=5)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()
    asset = build_asset(Path(args.out), Path(args.kuwu), args.bbox_margin)
    if args.port:
        upload_asset(args.port, args.baud, asset)


if __name__ == "__main__":
    main()
