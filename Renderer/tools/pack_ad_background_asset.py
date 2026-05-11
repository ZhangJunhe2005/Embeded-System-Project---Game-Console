#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import time
import zlib
from pathlib import Path

from PIL import Image


MAGIC = b"APXBG001"
VERSION_RGB565 = 1
VERSION_INDEXED8 = 2
VERSION_INDEXED8_PER_FRAME_LUT = 3
HEADER_SIZE = 64
VIEW_W = 240
VIEW_H = 135
LUT_SIZE = 256 * 2


def parse_coord(path: Path) -> tuple[int, int]:
    x_s, y_s = path.stem.split("_", 1)
    return int(x_s), int(y_s)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_full_view(path: Path) -> Image.Image:
    img = Image.open(path).convert("RGB")
    if img.width < VIEW_W or img.height < VIEW_H:
        raise SystemExit(f"image too small for {VIEW_W}x{VIEW_H}: {img.size}")
    return img


def image_to_lcd_rgb565_bytes(path: Path, color_order: str, byte_order: str) -> bytes:
    img = load_full_view(path)
    out = bytearray()
    pixels = img.tobytes()
    for i in range(0, len(pixels), 3):
        r = pixels[i]
        g = pixels[i + 1]
        b = pixels[i + 2]
        if color_order == "bgr":
            r, b = b, r
        color = rgb888_to_rgb565(r, g, b)
        if byte_order == "lcd":
            # Store LCD-ready byte order. STM32 reads these bytes into uint16_t line
            # buffers on little-endian memory, which becomes swap16(rgb565).
            out += struct.pack(">H", color)
        else:
            out += struct.pack("<H", color)
    return bytes(out)


def ordered_background_paths(bg_dir: Path) -> tuple[list[Path], int, int, int, int]:
    paths = sorted(bg_dir.glob("*.png"), key=lambda p: (parse_coord(p)[1], parse_coord(p)[0]))
    if not paths:
        raise SystemExit(f"no png backgrounds found in {bg_dir}")

    coords = [parse_coord(p) for p in paths]
    xs = [c[0] for c in coords]
    ys = [c[1] for c in coords]
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)
    expected = {(x, y) for y in range(y_min, y_max + 1) for x in range(x_min, x_max + 1)}
    missing = sorted(expected - set(coords))
    if missing:
        raise SystemExit(f"missing rectangular background coords: {missing}")

    ordered_paths = []
    by_coord = {parse_coord(p): p for p in paths}
    for y in range(y_min, y_max + 1):
        for x in range(x_min, x_max + 1):
            ordered_paths.append(by_coord[(x, y)])
    return ordered_paths, x_min, x_max, y_min, y_max


def build_rgb565_asset(bg_dir: Path, out_path: Path, color_order: str, byte_order: str) -> bytes:
    ordered_paths, x_min, x_max, y_min, y_max = ordered_background_paths(bg_dir)
    first = load_full_view(ordered_paths[0])
    asset_w, asset_h = first.size
    frame_stride = asset_w * asset_h * 2
    frames = bytearray()
    for path in ordered_paths:
        img = load_full_view(path)
        if img.size != (asset_w, asset_h):
            raise SystemExit(f"all backgrounds must share one size; {path} is {img.size}, expected {(asset_w, asset_h)}")
        frame = image_to_lcd_rgb565_bytes(path, color_order, byte_order)
        if len(frame) != frame_stride:
            raise SystemExit(f"bad frame length for {path}: {len(frame)}")
        frames += frame

    data_offset = HEADER_SIZE
    total_size = data_offset + len(frames)
    body_crc = zlib.crc32(frames) & 0xFFFFFFFF

    header = bytearray(HEADER_SIZE)
    header[0:8] = MAGIC
    struct.pack_into(
        "<IHHhhhhHHIIII",
        header,
        8,
        VERSION_RGB565,
        asset_w,
        asset_h,
        x_min,
        x_max,
        y_min,
        y_max,
        len(ordered_paths),
        0,
        frame_stride,
        data_offset,
        total_size,
        body_crc,
    )

    asset = bytes(header) + bytes(frames)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(asset)
    print(f"wrote {out_path} ({len(asset)} bytes)")
    print(f"backgrounds={len(ordered_paths)} range x={x_min}..{x_max} y={y_min}..{y_max} "
          f"size={asset_w}x{asset_h} rgb565 color={color_order} bytes={byte_order} crc=0x{body_crc:08X}")
    return asset


def build_indexed8_asset(bg_dir: Path, out_path: Path) -> bytes:
    ordered_paths, x_min, x_max, y_min, y_max = ordered_background_paths(bg_dir)
    views = [load_full_view(p) for p in ordered_paths]
    asset_w, asset_h = views[0].size
    for path, img in zip(ordered_paths, views):
        if img.size != (asset_w, asset_h):
            raise SystemExit(f"all backgrounds must share one size; {path} is {img.size}, expected {(asset_w, asset_h)}")

    atlas = Image.new("RGB", (asset_w, asset_h * len(views)))
    for i, img in enumerate(views):
        atlas.paste(img, (0, i * asset_h))

    pal_img = atlas.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = pal_img.getpalette()[:256 * 3]
    while len(palette) < 256 * 3:
        palette.extend([0, 0, 0])

    lut = bytearray()
    for i in range(256):
        r, g, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
        lut += struct.pack("<H", rgb888_to_rgb565(r, g, b))

    frame_stride = asset_w * asset_h
    frames = bytearray()
    for img in views:
        q = img.quantize(palette=pal_img, dither=Image.Dither.NONE)
        frame = q.tobytes()
        if len(frame) != frame_stride:
            raise SystemExit(f"bad indexed frame length: {len(frame)}")
        frames += frame

    data_offset = HEADER_SIZE + LUT_SIZE
    total_size = data_offset + len(frames)
    body = bytes(lut) + bytes(frames)
    body_crc = zlib.crc32(body) & 0xFFFFFFFF

    header = bytearray(HEADER_SIZE)
    header[0:8] = MAGIC
    struct.pack_into(
        "<IHHhhhhHHIIII",
        header,
        8,
        VERSION_INDEXED8,
        asset_w,
        asset_h,
        x_min,
        x_max,
        y_min,
        y_max,
        len(ordered_paths),
        1,  # format: indexed8 + RGB565 LUT
        frame_stride,
        data_offset,
        total_size,
        body_crc,
    )

    asset = bytes(header) + body
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(asset)
    print(f"wrote {out_path} ({len(asset)} bytes)")
    print(f"backgrounds={len(ordered_paths)} range x={x_min}..{x_max} y={y_min}..{y_max} "
          f"size={asset_w}x{asset_h} indexed8+lut crc=0x{body_crc:08X}")
    return asset


def build_indexed8_per_frame_lut_asset(bg_dir: Path, out_path: Path) -> bytes:
    ordered_paths, x_min, x_max, y_min, y_max = ordered_background_paths(bg_dir)
    views = [load_full_view(p) for p in ordered_paths]
    asset_w, asset_h = views[0].size
    for path, img in zip(ordered_paths, views):
        if img.size != (asset_w, asset_h):
            raise SystemExit(f"all backgrounds must share one size; {path} is {img.size}, expected {(asset_w, asset_h)}")

    frame_stride = asset_w * asset_h
    luts = bytearray()
    frames = bytearray()
    for img in views:
        q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        palette = q.getpalette()[:256 * 3]
        while len(palette) < 256 * 3:
            palette.extend([0, 0, 0])
        for i in range(256):
            r, g, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
            luts += struct.pack("<H", rgb888_to_rgb565(r, g, b))

        frame = q.tobytes()
        if len(frame) != frame_stride:
            raise SystemExit(f"bad indexed frame length: {len(frame)}")
        frames += frame

    lut_offset = HEADER_SIZE
    data_offset = lut_offset + len(luts)
    total_size = data_offset + len(frames)
    body = bytes(luts) + bytes(frames)
    body_crc = zlib.crc32(body) & 0xFFFFFFFF

    header = bytearray(HEADER_SIZE)
    header[0:8] = MAGIC
    struct.pack_into(
        "<IHHhhhhHHIIII",
        header,
        8,
        VERSION_INDEXED8_PER_FRAME_LUT,
        asset_w,
        asset_h,
        x_min,
        x_max,
        y_min,
        y_max,
        len(ordered_paths),
        2,  # format: indexed8 + one RGB565 LUT per frame
        frame_stride,
        data_offset,
        total_size,
        body_crc,
    )
    struct.pack_into("<II", header, 44, lut_offset, LUT_SIZE)

    asset = bytes(header) + body
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(asset)
    print(f"wrote {out_path} ({len(asset)} bytes)")
    print(f"backgrounds={len(ordered_paths)} range x={x_min}..{x_max} y={y_min}..{y_max} "
          f"size={asset_w}x{asset_h} indexed8+per-frame-lut crc=0x{body_crc:08X}")
    return asset


def wait_for_text(ser, needle: bytes, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(1)
        if chunk:
            buf += chunk
            try:
                print(chunk.decode("utf-8", errors="ignore"), end="", flush=True)
            except UnicodeError:
                pass
            if needle in buf:
                return
            if len(buf) > 4096:
                del buf[:2048]
    raise SystemExit(f"timed out waiting for board text: {needle.decode(errors='ignore')}")


def wait_for_ack(ser, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\x06":
            return
        try:
            print(b.decode("utf-8", errors="ignore"), end="", flush=True)
        except UnicodeError:
            pass
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
        wait_for_text(ser, b"[BG] Waiting for 64-byte header at 115200 8N1...", 25.0)
        time.sleep(0.2)

        print("\nsending 64-byte header")
        ser.write(asset[:HEADER_SIZE])
        ser.flush()

        print("waiting for flash erase to finish")
        wait_for_text(ser, b"[BG] ready for body", 120.0)

        print(f"\nuploading body {len(asset) - HEADER_SIZE} bytes")
        sent = HEADER_SIZE
        while sent < len(asset):
            page_room = 256 - (sent % 256)
            chunk_size = min(page_room, len(asset) - sent)
            chunk = asset[sent:sent + chunk_size]
            ser.write(chunk)
            ser.flush()
            wait_for_ack(ser, 5.0)
            sent += len(chunk)
            if ((sent - HEADER_SIZE) % 65536 == 0) or sent == len(asset):
                print(f"sent {sent}/{len(asset)}")
        ser.flush()
        print("upload complete; waiting for board verification")
        wait_for_text(ser, b"[BG] upload/program OK", 45.0)
        print("\nboard verified upload OK")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="ad_background")
    ap.add_argument("--out", default="Renderer/ad_background_asset.bin")
    ap.add_argument("--format", choices=("indexed8", "indexed8-per-frame-lut", "rgb565"), default="indexed8-per-frame-lut")
    ap.add_argument("--color-order", choices=("rgb", "bgr"), default="rgb")
    ap.add_argument("--byte-order", choices=("lcd", "little"), default="little")
    ap.add_argument("--port", help="optional serial port, e.g. COM6")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    if args.format == "rgb565":
        asset = build_rgb565_asset(Path(args.input), Path(args.out), args.color_order, args.byte_order)
    elif args.format == "indexed8-per-frame-lut":
        asset = build_indexed8_per_frame_lut_asset(Path(args.input), Path(args.out))
    else:
        asset = build_indexed8_asset(Path(args.input), Path(args.out))
    if args.port:
        upload_asset(args.port, args.baud, asset)


if __name__ == "__main__":
    main()
