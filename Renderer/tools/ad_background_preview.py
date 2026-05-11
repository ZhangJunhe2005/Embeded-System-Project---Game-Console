#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SCREEN_W = 240
SCREEN_H = 240
VIEW_W = 240
VIEW_H = 135
VIEW_Y = (SCREEN_H - VIEW_H) // 2


def parse_coord(path: Path) -> tuple[int, int]:
    x_s, y_s = path.stem.split("_", 1)
    return int(x_s), int(y_s)


def load_backgrounds(bg_dir: Path) -> dict[tuple[int, int], Image.Image]:
    bgs: dict[tuple[int, int], Image.Image] = {}
    for path in bg_dir.glob("*.png"):
        bgs[parse_coord(path)] = Image.open(path).convert("RGB")
    return bgs


def coord_bounds(bgs: dict[tuple[int, int], Image.Image]) -> tuple[int, int, int, int]:
    xs = [coord[0] for coord in bgs]
    ys = [coord[1] for coord in bgs]
    return min(xs), max(xs), min(ys), max(ys)


def serpentine_path(bgs: dict[tuple[int, int], Image.Image]) -> list[tuple[int, int]]:
    min_x, max_x, min_y, max_y = coord_bounds(bgs)
    path: list[tuple[int, int]] = []
    for row_idx, y in enumerate(range(min_y, max_y + 1)):
        xs = range(min_x, max_x + 1)
        if row_idx % 2:
            xs = range(max_x, min_x - 1, -1)
        for x in xs:
            if (x, y) in bgs:
                path.append((x, y))
    return path


def center_crop_view(src: Image.Image, look_x: int = 0, look_y: int = 0) -> Image.Image:
    max_x = src.width - VIEW_W
    max_y = src.height - VIEW_H
    x0 = max(0, min(max_x, (src.width - VIEW_W) // 2 + look_x))
    y0 = max(0, min(max_y, (src.height - VIEW_H) // 2 + look_y))
    return src.crop((x0, y0, x0 + VIEW_W, y0 + VIEW_H))


def compose_screen(view: Image.Image, label: str, show_guides: bool = True) -> Image.Image:
    frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (4, 5, 6))
    frame.paste(view, (0, VIEW_Y))
    draw = ImageDraw.Draw(frame)

    if show_guides:
        cx = SCREEN_W // 2
        cy = VIEW_Y + VIEW_H // 2
        draw.line((cx - 6, cy, cx + 6, cy), fill=(230, 230, 230))
        draw.line((cx, cy - 6, cx, cy + 6), fill=(230, 230, 230))
        draw.rectangle((0, VIEW_Y, SCREEN_W - 1, VIEW_Y + VIEW_H - 1), outline=(52, 52, 52))

    draw.text((4, 4), label, fill=(220, 220, 220))
    return frame


def make_grid_contact(bgs: dict[tuple[int, int], Image.Image], out: Path) -> None:
    thumb_w, thumb_h = 160, 90
    pad = 18
    min_x, max_x, min_y, max_y = coord_bounds(bgs)
    cols = max_x - min_x + 1
    rows = max_y - min_y + 1
    sheet = Image.new("RGB", (cols * thumb_w, rows * (thumb_h + pad)), (10, 10, 12))
    draw = ImageDraw.Draw(sheet)

    for gy, y in enumerate(range(max_y, min_y - 1, -1)):
        for gx, x in enumerate(range(min_x, max_x + 1)):
            bg = bgs.get((x, y))
            if bg is None:
                continue
            view = center_crop_view(bg).resize((thumb_w, thumb_h), Image.Resampling.BILINEAR)
            px = gx * thumb_w
            py = gy * (thumb_h + pad)
            sheet.paste(view, (px, py + pad))
            draw.text((px + 4, py + 2), f"{x}_{y}", fill=(255, 255, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"Saved contact sheet: {out}")


def make_move_gif(bgs: dict[tuple[int, int], Image.Image], out: Path, fps: int) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (2, 1),
        (2, 2),
        (1, 2),
        (0, 2),
        (-1, 1),
        (-2, 0),
        (-1, -1),
        (0, -2),
        (1, -1),
        (0, 0),
    ]

    frames: list[Image.Image] = []
    for coord in path:
        bg = bgs[coord]
        # Small look sweep inside each position, just to preview crop headroom.
        for look_x in (-24, 0, 24, 0):
            view = center_crop_view(bg, look_x=look_x)
            frames.append(compose_screen(view, f"pos {coord[0]}_{coord[1]}  look_x={look_x:+d}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=int(1000 / max(1, fps)),
        loop=0,
        optimize=False,
    )
    print(f"Saved movement GIF: {out}")


def make_position_step_gif(bgs: dict[tuple[int, int], Image.Image], out: Path, duration_ms: int) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (3, 0),
        (2, 0),
        (1, 0),
        (0, 0),
        (-1, 0),
        (-2, 0),
        (-3, 0),
        (-2, 0),
        (-1, 0),
        (0, 0),
        (0, 1),
        (0, 2),
        (0, 1),
        (0, 0),
        (0, -1),
        (0, -2),
        (0, -1),
        (0, 0),
        (1, 1),
        (2, 2),
        (1, 1),
        (0, 0),
        (-1, -1),
        (-2, -2),
        (-1, -1),
        (0, 0),
    ]
    frames = [compose_screen(center_crop_view(bgs[coord]), f"pos {coord[0]}_{coord[1]}") for coord in path]
    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved slow position GIF: {out}")


def make_look_crop_gif(bgs: dict[tuple[int, int], Image.Image], out: Path, duration_ms: int) -> None:
    bg = bgs[(0, 0)]
    samples = [
        (-80, 0),
        (-48, 0),
        (-24, 0),
        (0, 0),
        (24, 0),
        (48, 0),
        (80, 0),
        (48, 0),
        (24, 0),
        (0, 0),
        (0, -48),
        (0, -24),
        (0, 0),
        (0, 24),
        (0, 48),
        (0, 24),
        (0, 0),
    ]
    frames = [
        compose_screen(center_crop_view(bg, look_x=lx, look_y=ly), f"0_0 crop x={lx:+d} y={ly:+d}")
        for lx, ly in samples
    ]
    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved look crop GIF: {out}")


def block_transition_view(
    a: Image.Image,
    b: Image.Image,
    t: float,
    move_dx: int,
    move_dy: int,
    block: int | tuple[int, int] = 16,
    random_weight: float = 0.3,
) -> Image.Image:
    if isinstance(block, tuple):
        block_w, block_h = block
    else:
        block_w = block_h = block

    out = a.copy()
    draw_order: list[tuple[float, int, int]] = []
    for y in range(0, VIEW_H, block_h):
        for x in range(0, VIEW_W, block_w):
            # Directional order: blocks in the movement direction arrive slightly earlier.
            nx = x / max(1, VIEW_W - block_w)
            ny = y / max(1, VIEW_H - block_h)
            directional = 0.0
            if move_dx > 0:
                directional += nx
            elif move_dx < 0:
                directional += 1.0 - nx
            if move_dy > 0:
                directional += 1.0 - ny
            elif move_dy < 0:
                directional += ny
            if move_dx != 0 and move_dy != 0:
                directional *= 0.5

            # Small deterministic variation so the transition feels like changed blocks, not a wipe.
            noise = (((x // block_w) * 37 + (y // block_h) * 19) % 23) / 23.0
            score = directional * (1.0 - random_weight) + noise * random_weight
            draw_order.append((score, x, y))

    scores = [s for s, _, _ in draw_order]
    lo, hi = min(scores), max(scores)
    threshold = lo + (hi - lo) * t
    soft = 0.18
    for score, x, y in draw_order:
        box = (x, y, min(x + block_w, VIEW_W), min(y + block_h, VIEW_H))
        if score <= threshold - soft:
            out.paste(b.crop(box), box)
        elif score <= threshold + soft:
            alpha = int(max(0.0, min(1.0, (threshold + soft - score) / (soft * 2.0))) * 255)
            patch = Image.blend(a.crop(box), b.crop(box), alpha / 255.0)
            out.paste(patch, box)
    return out


def make_smooth_plan_gif(bgs: dict[tuple[int, int], Image.Image], out: Path, duration_ms: int) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (2, 1),
        (2, 2),
        (1, 2),
        (0, 2),
        (-1, 1),
        (-2, 0),
        (-1, -1),
        (0, -2),
        (1, -1),
        (0, 0),
    ]
    frames: list[Image.Image] = []
    for a_coord, b_coord in zip(path, path[1:]):
        ax, ay = a_coord
        bx, by = b_coord
        move_dx = bx - ax
        move_dy = by - ay
        a_view = center_crop_view(bgs[a_coord])
        b_view = center_crop_view(bgs[b_coord])
        frames.append(compose_screen(a_view, f"pos {ax}_{ay}"))
        for step in range(1, 6):
            t = step / 6.0
            view = block_transition_view(a_view, b_view, t, move_dx, move_dy)
            frames.append(compose_screen(view, f"{ax}_{ay} -> {bx}_{by}  t={t:.2f}"))
    frames.append(compose_screen(center_crop_view(bgs[path[-1]]), f"pos {path[-1][0]}_{path[-1][1]}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved smooth plan GIF: {out}")


def make_smooth_plan_fast_gif(
    bgs: dict[tuple[int, int], Image.Image],
    out: Path,
    steps: tuple[float, ...] = (0.0, 0.33, 0.66),
    duration_ms: int = 50,
    block: int | tuple[int, int] = 16,
    random_weight: float = 0.3,
) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (2, 1),
        (2, 2),
        (1, 2),
        (0, 2),
        (-1, 1),
        (-2, 0),
        (-1, -1),
        (0, -2),
        (1, -1),
        (0, 0),
    ]
    frames: list[Image.Image] = []
    for a_coord, b_coord in zip(path, path[1:]):
        ax, ay = a_coord
        bx, by = b_coord
        a_view = center_crop_view(bgs[a_coord])
        b_view = center_crop_view(bgs[b_coord])
        for t in steps:
            view = block_transition_view(a_view, b_view, t, bx - ax, by - ay, block=block, random_weight=random_weight)
            frames.append(compose_screen(view, f"{ax}_{ay}->{bx}_{by}"))
    frames.append(compose_screen(center_crop_view(bgs[path[-1]]), f"pos {path[-1][0]}_{path[-1][1]}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved fast smooth GIF: {out}")


def make_smooth_plan_adaptive_block_gif(
    bgs: dict[tuple[int, int], Image.Image],
    out: Path,
    steps: tuple[float, ...] = (0.0, 0.33, 0.66),
    duration_ms: int = 50,
    horizontal_block: tuple[int, int] = (8, 16),
    vertical_block: tuple[int, int] = (16, 8),
    random_weight: float = 0.0,
) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (2, 1),
        (2, 2),
        (1, 2),
        (0, 2),
        (-1, 1),
        (-2, 0),
        (-1, -1),
        (0, -2),
        (1, -1),
        (0, 0),
    ]
    frames: list[Image.Image] = []
    for a_coord, b_coord in zip(path, path[1:]):
        ax, ay = a_coord
        bx, by = b_coord
        move_dx = bx - ax
        move_dy = by - ay
        block = horizontal_block if abs(move_dx) >= abs(move_dy) else vertical_block
        a_view = center_crop_view(bgs[a_coord])
        b_view = center_crop_view(bgs[b_coord])
        for t in steps:
            view = block_transition_view(a_view, b_view, t, move_dx, move_dy, block=block, random_weight=random_weight)
            frames.append(compose_screen(view, f"{ax}_{ay}->{bx}_{by} {block[0]}x{block[1]}"))
    frames.append(compose_screen(center_crop_view(bgs[path[-1]]), f"pos {path[-1][0]}_{path[-1][1]}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved adaptive block GIF: {out}")


def make_all_position_scan_gif(
    bgs: dict[tuple[int, int], Image.Image],
    out: Path,
    duration_ms: int = 120,
) -> None:
    path = serpentine_path(bgs)
    frames = [compose_screen(center_crop_view(bgs[coord]), f"pos {coord[0]}_{coord[1]}") for coord in path]

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved all-position scan GIF: {out}")


def make_all_smooth_scan_gif(
    bgs: dict[tuple[int, int], Image.Image],
    out: Path,
    steps: tuple[float, ...] = (0.0, 0.25, 0.50, 0.75),
    duration_ms: int = 50,
) -> None:
    path = serpentine_path(bgs)
    frames: list[Image.Image] = []
    for a_coord, b_coord in zip(path, path[1:]):
        ax, ay = a_coord
        bx, by = b_coord
        move_dx = bx - ax
        move_dy = by - ay
        block = (8, 16) if abs(move_dx) >= abs(move_dy) else (16, 8)
        a_view = center_crop_view(bgs[a_coord])
        b_view = center_crop_view(bgs[b_coord])
        for t in steps:
            view = block_transition_view(a_view, b_view, t, move_dx, move_dy, block=block, random_weight=0.0)
            frames.append(compose_screen(view, f"{ax}_{ay}->{bx}_{by} {block[0]}x{block[1]}"))
    frames.append(compose_screen(center_crop_view(bgs[path[-1]]), f"pos {path[-1][0]}_{path[-1][1]}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
    )
    print(f"Saved all smooth scan GIF: {out}")


def make_crop_assisted_gif(bgs: dict[tuple[int, int], Image.Image], out: Path) -> None:
    path = [
        (0, 0),
        (1, 0),
        (2, 0),
        (2, 1),
        (2, 2),
        (1, 2),
        (0, 2),
        (-1, 1),
        (-2, 0),
        (-1, -1),
        (0, -2),
        (1, -1),
        (0, 0),
    ]
    frames: list[Image.Image] = []
    shift_px = 5
    for a_coord, b_coord in zip(path, path[1:]):
        ax, ay = a_coord
        bx, by = b_coord
        mdx = bx - ax
        mdy = by - ay
        sx = shift_px * (1 if mdx > 0 else -1 if mdx < 0 else 0)
        sy = shift_px * (-1 if mdy > 0 else 1 if mdy < 0 else 0)

        a_bg = bgs[a_coord]
        b_bg = bgs[b_coord]

        # 1) Move crop inside current background toward the edge.
        for t in (0.0, 0.50, 1.0):
            view = center_crop_view(a_bg, int(sx * t), int(sy * t))
            frames.append(compose_screen(view, f"{ax}_{ay} crop-out"))

        # 2) Switch background, start from opposite crop offset, with one block-assisted bridge.
        a_edge = center_crop_view(a_bg, sx, sy)
        b_edge = center_crop_view(b_bg, -sx, -sy)
        bridge = block_transition_view(a_edge, b_edge, 0.55, mdx, mdy)
        frames.append(compose_screen(bridge, f"{ax}_{ay}->{bx}_{by} bridge"))

        # 3) Recentre crop inside new background.
        for t in (1.0, 0.50, 0.0):
            view = center_crop_view(b_bg, int(-sx * t), int(-sy * t))
            frames.append(compose_screen(view, f"{bx}_{by} crop-in"))

    frames.append(compose_screen(center_crop_view(bgs[path[-1]]), f"pos {path[-1][0]}_{path[-1][1]}"))

    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=50,
        loop=0,
        optimize=False,
    )
    print(f"Saved crop-assisted GIF: {out}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="ad_background")
    ap.add_argument("--out-dir", default="Renderer/ad_background_preview")
    ap.add_argument("--fps", type=int, default=8)
    ap.add_argument("--slow-duration", type=int, default=700)
    args = ap.parse_args()

    bgs = load_backgrounds(Path(args.input))
    min_x, max_x, min_y, max_y = coord_bounds(bgs)
    expected = {(x, y) for x in range(min_x, max_x + 1) for y in range(min_y, max_y + 1)}
    missing = sorted(expected - set(bgs))
    if missing:
        raise SystemExit(f"missing backgrounds: {missing}")

    sizes = {im.size for im in bgs.values()}
    if len(sizes) != 1:
        raise SystemExit(f"mixed image sizes: {sorted(sizes)}")

    out_dir = Path(args.out_dir)
    make_grid_contact(bgs, out_dir / "ad_grid_contact.png")
    make_all_position_scan_gif(bgs, out_dir / "ad_66_position_scan.gif")
    make_all_smooth_scan_gif(bgs, out_dir / "ad_66_smooth_scan_20fps.gif")
    make_move_gif(bgs, out_dir / "ad_move_preview.gif", args.fps)
    make_position_step_gif(bgs, out_dir / "ad_position_step_slow.gif", args.slow_duration)
    make_look_crop_gif(bgs, out_dir / "ad_look_crop_slow.gif", args.slow_duration)
    make_smooth_plan_gif(bgs, out_dir / "ad_smooth_plan_preview.gif", max(90, args.slow_duration // 3))
    make_smooth_plan_fast_gif(bgs, out_dir / "ad_smooth_plan_20fps.gif")
    make_smooth_plan_fast_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_5state.gif",
        steps=(0.0, 0.20, 0.40, 0.60, 0.80),
        duration_ms=50,
    )
    make_smooth_plan_fast_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_16px_random.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        block=16,
        random_weight=0.7,
    )
    make_smooth_plan_fast_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_8px_random.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        block=8,
        random_weight=0.7,
    )
    make_smooth_plan_fast_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_8px_directional.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        block=8,
        random_weight=0.0,
    )
    make_smooth_plan_fast_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_8x16_horizontal.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        block=(8, 16),
        random_weight=0.0,
    )
    make_smooth_plan_adaptive_block_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_adaptive_8x16_16x8.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        horizontal_block=(8, 16),
        vertical_block=(16, 8),
        random_weight=0.0,
    )
    make_smooth_plan_adaptive_block_gif(
        bgs,
        out_dir / "ad_smooth_plan_20fps_adaptive_8x16_16x16.gif",
        steps=(0.0, 0.25, 0.50, 0.75),
        duration_ms=50,
        horizontal_block=(8, 16),
        vertical_block=(16, 16),
        random_weight=0.0,
    )
    make_crop_assisted_gif(bgs, out_dir / "ad_crop_assisted_20fps.gif")


if __name__ == "__main__":
    main()
