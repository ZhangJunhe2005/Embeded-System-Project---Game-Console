#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


ALPHA_THRESHOLD = 255


def path_number(path: Path) -> int:
    digits = "".join(c for c in path.stem if c.isdigit())
    return int(digits or "0")


def sorted_qiedao_paths(seq_dir: Path) -> list[Path]:
    return sorted(seq_dir.glob("qiedao-*.png"), key=lambda p: int(p.stem.split("-")[-1]))


def sorted_numbered_paths(seq_dir: Path, prefix: str) -> list[Path]:
    return sorted(seq_dir.glob(f"{prefix}*.png"), key=path_number)


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


def normalize_frame(im: Image.Image, width: int, height: int) -> Image.Image:
    im = im.convert("RGBA")
    if im.size == (width, height):
        return im
    if im.width > width or im.height > height:
        raise SystemExit(f"image {im.size} is larger than preview canvas {width}x{height}")
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    out.alpha_composite(im, ((width - im.width) // 2, height - im.height))
    return out


def expand_timeline(paths: list[Path], blanks: set[int]) -> list[Path | None]:
    if not paths and not blanks:
        return []
    by_num = {path_number(p): p for p in paths}
    first = min(set(by_num) | blanks)
    last = max(set(by_num) | blanks)
    out: list[Path | None] = []
    prev: Path | None = None
    for n in range(first, last + 1):
        if n in blanks:
            out.append(None)
            continue
        if n in by_num:
            prev = by_num[n]
        out.append(prev)
    return out


def alpha_bbox(im: Image.Image, margin: int) -> tuple[int, int, int, int] | None:
    alpha = im.getchannel("A")
    raw = alpha.load()
    min_x = im.width
    min_y = im.height
    max_x = -1
    max_y = -1
    for y in range(im.height):
        for x in range(im.width):
            if raw[x, y] >= ALPHA_THRESHOLD:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if max_x < min_x or max_y < min_y:
        return None
    return (
        max(0, min_x - margin),
        max(0, min_y - margin),
        min(im.width, max_x + margin + 1),
        min(im.height, max_y + margin + 1),
    )


def checker_bg(width: int, height: int) -> Image.Image:
    bg = Image.new("RGB", (width, height), (24, 24, 24))
    d = ImageDraw.Draw(bg)
    for y in range(0, height, 8):
        for x in range(0, width, 8):
            if ((x // 8) + (y // 8)) & 1:
                d.rectangle((x, y, x + 7, y + 7), fill=(38, 38, 38))
    return bg


def composite_on_bg(im: Image.Image, bg: Image.Image) -> Image.Image:
    out = bg.copy().convert("RGBA")
    out.alpha_composite(im)
    return out.convert("RGB")


def preview_sequence(
    name: str,
    timeline: list[Path | None],
    canvas_w: int,
    canvas_h: int,
    margin: int,
    out_dir: Path,
    duration_ms: int,
) -> None:
    bg = checker_bg(canvas_w, canvas_h)
    frames: list[Image.Image] = []
    bbox_area = 0
    opaque_frames = 0

    for item in timeline:
        original = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
        restored = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
        bbox = None
        if item is not None:
            original = normalize_frame(Image.open(item), canvas_w, canvas_h)
            bbox = alpha_bbox(original, margin)
            if bbox is not None:
                crop = original.crop(bbox)
                restored.alpha_composite(crop, (bbox[0], bbox[1]))
                bbox_area += (bbox[2] - bbox[0]) * (bbox[3] - bbox[1])
                opaque_frames += 1

        left = composite_on_bg(original, bg)
        right = composite_on_bg(restored, bg)
        if bbox is not None:
            draw = ImageDraw.Draw(right)
            draw.rectangle((bbox[0], bbox[1], bbox[2] - 1, bbox[3] - 1), outline=(255, 32, 32), width=1)

        canvas = Image.new("RGB", (canvas_w * 2 + 8, canvas_h), (8, 8, 8))
        canvas.paste(left, (0, 0))
        canvas.paste(right, (canvas_w + 8, 0))
        frames.append(canvas)

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{name}_bbox_margin{margin}_compare.gif"
    frames[0].save(out_path, save_all=True, append_images=frames[1:], duration=duration_ms, loop=0)

    full_area = canvas_w * canvas_h
    avg_area = (bbox_area / opaque_frames) if opaque_frames else 0.0
    print(f"{name}: wrote {out_path}")
    print(f"{name}: frames={len(timeline)} opaque={opaque_frames} avg_bbox={avg_area:.0f}px ({avg_area / full_area * 100:.1f}% of full)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--qiedao-dir", default="qiedao")
    ap.add_argument("--kuwu", default="kuwu.png")
    ap.add_argument("--qieqiang-dir", default="qieqiang")
    ap.add_argument("--shouqiang-dir", default="shouqiang")
    ap.add_argument("--out-dir", default="Renderer/gun_preview")
    ap.add_argument("--margin", type=int, default=5)
    ap.add_argument("--width", type=int, default=240)
    ap.add_argument("--height", type=int, default=135)
    ap.add_argument("--duration", type=int, default=60)
    args = ap.parse_args()

    qiedao = sorted_qiedao_paths(Path(args.qiedao_dir))
    qieqiang = expand_timeline(sorted_numbered_paths(Path(args.qieqiang_dir), "qieqiang"), blank_numbers(Path(args.qieqiang_dir)))
    shouqiang = expand_timeline(sorted_numbered_paths(Path(args.shouqiang_dir), "shouqiang"), blank_numbers(Path(args.shouqiang_dir)))
    kuwu = [Path(args.kuwu)] * 8

    out_dir = Path(args.out_dir)
    preview_sequence("qiedao", qiedao, args.width, args.height, args.margin, out_dir, args.duration)
    preview_sequence("kuwu_idle", kuwu, args.width, args.height, args.margin, out_dir, args.duration)
    preview_sequence("qieqiang_timeline", qieqiang, args.width, args.height, args.margin, out_dir, args.duration)
    preview_sequence("shouqiang_timeline", shouqiang, args.width, args.height, args.margin, out_dir, args.duration)
    preview_sequence("cycle_kuwu_to_r99_to_kuwu", kuwu[:4] + qieqiang + shouqiang + qiedao + kuwu[:4],
                     args.width, args.height, args.margin, out_dir, args.duration)


if __name__ == "__main__":
    main()
