#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import time
import zlib
from pathlib import Path

from PIL import Image


MAGIC = b"APXWPN01"
VERSION = 4
HEADER_SIZE = 64
ALPHA_THRESHOLD = 255
BBOX_MARGIN = 5
BOTTOM_NOISE_ROWS = 6
BOTTOM_MIN_RUN = 4
HOLE_FILL_NEIGHBORS = 6
BRIGHT_HOLE_FILL_NEIGHBORS = 10
BRIGHT_HOLE_MIN_LUMA = 150


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def sorted_qiedao_paths(seq_dir: Path) -> list[Path]:
    return sorted(seq_dir.glob("qiedao-*.png"), key=lambda p: int(p.stem.split("-")[-1]))


def sorted_qieqiang_paths(seq_dir: Path) -> list[Path]:
    return sorted(seq_dir.glob("qieqiang*.png"), key=lambda p: int("".join(c for c in p.stem if c.isdigit()) or "0"))


def sorted_numbered_paths(seq_dir: Path, prefix: str) -> list[Path]:
    return sorted(seq_dir.glob(f"{prefix}*.png"), key=path_number)


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
            if "-" in part:
                a, b = part.split("-", 1)
                if a.strip().isdigit() and b.strip().isdigit():
                    start = int(a)
                    end = int(b)
                    nums.update(range(min(start, end), max(start, end) + 1))
            elif part.strip().isdigit():
                nums.add(int(part))
    return nums


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
            continue
        if n in by_num:
            prev = global_indices[by_num[n]]
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
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
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


def build_asset(seq_dir: Path, kuwu_path: Path, out_path: Path, qieqiang_dir: Path, shouqiang_dir: Path, bbox_margin: int) -> bytes:
    qiedao_paths = sorted_qiedao_paths(seq_dir)
    if not qiedao_paths:
        raise SystemExit(f"no qiedao-*.png found in {seq_dir}")

    qieqiang_paths = sorted_qieqiang_paths(qieqiang_dir)
    if not qieqiang_paths:
        raise SystemExit(f"no qieqiang*.png found in {qieqiang_dir}")
    shouqiang_paths = sorted_numbered_paths(shouqiang_dir, "shouqiang")
    if not shouqiang_paths:
        raise SystemExit(f"no shouqiang*.png found in {shouqiang_dir}")

    paths = qiedao_paths + [kuwu_path] + qieqiang_paths + shouqiang_paths
    global_indices = {path: i for i, path in enumerate(paths)}
    qieqiang_timeline = build_timeline(qieqiang_paths, global_indices, blank_numbers(qieqiang_dir))
    shouqiang_timeline = build_timeline(shouqiang_paths, global_indices, blank_numbers(shouqiang_dir))
    raw_images = [Image.open(p).convert("RGBA") for p in paths]
    width = max(im.width for im in raw_images)
    height = max(im.height for im in raw_images)
    images = [normalize_frame(im, width, height) for im in raw_images]

    atlas = Image.new("RGB", (width, height * len(images)), (0, 0, 0))
    masks: list[list[bool]] = []
    for frame_i, im in enumerate(images):
        mask: list[bool] = []
        rgb: list[tuple[int, int, int]] = []
        px = im.load()
        for y in range(height):
            for x in range(width):
                r, g, b, a = px[x, y]
                opaque = a >= ALPHA_THRESHOLD
                mask.append(opaque)
                rgb.append((r, g, b))
                if opaque:
                    atlas.putpixel((x, frame_i * height + y), (r, g, b))
        for y in range(height - BOTTOM_NOISE_ROWS, height):
            row_start = y * width
            x = 0
            while x < width:
                if not mask[row_start + x]:
                    x += 1
                    continue
                run_start = x
                while x < width and mask[row_start + x]:
                    x += 1
                if (x - run_start) < BOTTOM_MIN_RUN:
                    for xx in range(run_start, x):
                        mask[row_start + xx] = False

        fill_pixels: list[tuple[int, int, tuple[int, int, int]]] = []
        for y in range(1, height - 1):
            for x in range(1, width - 1):
                i = y * width + x
                if mask[i]:
                    continue
                neighbors: list[tuple[int, int, int]] = []
                for yy in range(y - 1, y + 2):
                    for xx in range(x - 1, x + 2):
                        if xx == x and yy == y:
                            continue
                        ni = yy * width + xx
                        if mask[ni]:
                            neighbors.append(rgb[ni])
                if len(neighbors) >= HOLE_FILL_NEIGHBORS:
                    r = sum(c[0] for c in neighbors) // len(neighbors)
                    g = sum(c[1] for c in neighbors) // len(neighbors)
                    b = sum(c[2] for c in neighbors) // len(neighbors)
                    fill_pixels.append((x, y, (r, g, b)))

        for x, y, color in fill_pixels:
            mask[y * width + x] = True
            rgb[y * width + x] = color
            atlas.putpixel((x, frame_i * height + y), color)

        bright_fill_pixels: list[tuple[int, int, tuple[int, int, int]]] = []
        for y in range(2, height - 2):
            for x in range(2, width - 2):
                i = y * width + x
                if mask[i]:
                    continue
                neighbors: list[tuple[int, int, int]] = []
                for yy in range(y - 2, y + 3):
                    for xx in range(x - 2, x + 3):
                        if xx == x and yy == y:
                            continue
                        ni = yy * width + xx
                        if not mask[ni]:
                            continue
                        r, g, b = rgb[ni]
                        luma = (77 * r + 150 * g + 29 * b) >> 8
                        if luma >= BRIGHT_HOLE_MIN_LUMA:
                            neighbors.append((r, g, b))
                if len(neighbors) >= BRIGHT_HOLE_FILL_NEIGHBORS:
                    r = sum(c[0] for c in neighbors) // len(neighbors)
                    g = sum(c[1] for c in neighbors) // len(neighbors)
                    b = sum(c[2] for c in neighbors) // len(neighbors)
                    bright_fill_pixels.append((x, y, (r, g, b)))

        for x, y, color in bright_fill_pixels:
            mask[y * width + x] = True
            rgb[y * width + x] = color
            atlas.putpixel((x, frame_i * height + y), color)
        masks.append(mask)

    pal_img = atlas.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal_idx = list(pal_img.tobytes())
    raw_pal = pal_img.getpalette()
    if raw_pal is None:
        raise SystemExit("quantize returned no palette")

    lut = bytearray()
    lut += struct.pack("<H", 0)
    for i in range(255):
        lut += struct.pack("<H", rgb888_to_rgb565(raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2]))

    lut_offset = HEADER_SIZE
    qieqiang_timeline_offset = lut_offset + len(lut)
    qieqiang_timeline_data = b"".join(struct.pack("<h", i) for i in qieqiang_timeline)
    shouqiang_timeline_offset = qieqiang_timeline_offset + len(qieqiang_timeline_data)
    shouqiang_timeline_data = b"".join(struct.pack("<h", i) for i in shouqiang_timeline)

    frame_meta_offset = shouqiang_timeline_offset + len(shouqiang_timeline_data)
    data_offset = frame_meta_offset + len(images) * 12
    frame_meta = bytearray()
    frames = bytearray()
    full_frame_stride = width * height
    max_frame_pixels = 0
    for frame_i, mask in enumerate(masks):
        x0, y0, x1, y1 = mask_bbox(mask, width, height, bbox_margin)
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

    frame_stride = max_frame_pixels
    total_size = data_offset + len(frames)
    body = bytes(lut) + qieqiang_timeline_data + shouqiang_timeline_data + bytes(frame_meta) + bytes(frames)
    body_crc = zlib.crc32(body) & 0xFFFFFFFF

    header = bytearray(HEADER_SIZE)
    header[0:8] = MAGIC
    struct.pack_into(
        "<IHHHHHHIIIII",
        header,
        8,
        VERSION,
        width,
        height,
        len(qiedao_paths),
        len(images),
        len(qiedao_paths),
        15,
        frame_stride,
        lut_offset,
        data_offset,
        total_size,
        body_crc,
    )
    struct.pack_into(
        "<HHHHIIHH",
        header,
        44,
        len(qiedao_paths) + 1,
        len(qieqiang_paths),
        len(qiedao_paths) + 1 + len(qieqiang_paths),
        len(shouqiang_paths),
        qieqiang_timeline_offset,
        shouqiang_timeline_offset,
        len(qieqiang_timeline),
        len(shouqiang_timeline),
    )

    asset = bytes(header) + body
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(asset)
    print(f"wrote {out_path} ({len(asset)} bytes)")
    print(
        f"frames={len(images)} qiedao={len(qiedao_paths)} kuwu=1 "
        f"qieqiang={len(qieqiang_paths)}/{len(qieqiang_timeline)} "
        f"shouqiang={len(shouqiang_paths)}/{len(shouqiang_timeline)} "
        f"size={width}x{height} max_bbox={frame_stride} margin={bbox_margin} crc=0x{body_crc:08X}"
    )
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
        wait_for_text(ser, b"Waiting for 64-byte header at 115200 8N1...", 20.0)
        time.sleep(0.2)

        print("\nsending 64-byte header")
        ser.write(asset[:64])
        ser.flush()

        print("waiting for flash erase to finish")
        wait_for_text(ser, b"[ASSET] ready for body", 60.0)

        print(f"\nuploading body {len(asset) - 64} bytes")
        sent = 64
        while sent < len(asset):
            page_room = 256 - (sent % 256)
            chunk_size = min(page_room, len(asset) - sent)
            chunk = asset[sent:sent + chunk_size]
            ser.write(chunk)
            ser.flush()
            wait_for_ack(ser, 5.0)
            sent += len(chunk)
            if ((sent - 64) % 16384 == 0) or sent == len(asset):
                print(f"sent {sent}/{len(asset)}")
        ser.flush()
        print("upload complete; waiting for board verification")
        wait_for_text(ser, b"[ASSET] upload/program OK", 30.0)
        print("\nboard verified upload OK")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seq-dir", default="qiedao")
    ap.add_argument("--kuwu", default="kuwu.png")
    ap.add_argument("--qieqiang-dir", default="qieqiang")
    ap.add_argument("--shouqiang-dir", default="shouqiang")
    ap.add_argument("--out", default="Renderer/qiedao_asset.bin")
    ap.add_argument("--bbox-margin", type=int, default=BBOX_MARGIN)
    ap.add_argument("--port", help="optional serial port, e.g. COM6")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    asset = build_asset(Path(args.seq_dir), Path(args.kuwu), Path(args.out), Path(args.qieqiang_dir), Path(args.shouqiang_dir), args.bbox_margin)
    if args.port:
        upload_asset(args.port, args.baud, asset)


if __name__ == "__main__":
    main()
