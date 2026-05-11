#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SCREEN_W = 240
SCREEN_H = 240
VIEW_W = 240
VIEW_H = 135
VIEW_Y = 52


def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))


def parse_coord(path: Path) -> tuple[int, int]:
    x_s, y_s = path.stem.split("_", 1)
    return int(x_s), int(y_s)


def load_backgrounds(bg_dir: Path) -> dict[tuple[int, int], Image.Image]:
    out: dict[tuple[int, int], Image.Image] = {}
    for path in bg_dir.glob("*.png"):
        out[parse_coord(path)] = Image.open(path).convert("RGBA")
    return out


def compose_frame(
    bg: Image.Image,
    robot: Image.Image,
    robot_bg_x: int,
    robot_bg_y: int,
    crop_x: int,
    crop_y: int,
    label: str,
    scale: float = 1.0,
) -> Image.Image:
    max_x = bg.width - VIEW_W
    max_y = bg.height - VIEW_H
    crop_x = clamp(crop_x, 0, max_x)
    crop_y = clamp(crop_y, 0, max_y)

    view = bg.crop((crop_x, crop_y, crop_x + VIEW_W, crop_y + VIEW_H)).convert("RGBA")

    if scale <= 0:
        scale = 0.1
    if abs(scale - 1.0) > 0.001:
        rw = max(1, round(robot.width * scale))
        rh = max(1, round(robot.height * scale))
        robot_draw = robot.resize((rw, rh), Image.Resampling.NEAREST)
    else:
        robot_draw = robot

    robot_x = robot_bg_x - crop_x - robot_draw.width // 2
    robot_y = robot_bg_y - crop_y - robot_draw.height
    if robot_x < VIEW_W and robot_y < VIEW_H and robot_x + robot_draw.width > 0 and robot_y + robot_draw.height > 0:
        view.alpha_composite(robot_draw, (robot_x, robot_y))
        draw_robot_meter(view, robot_x + robot_draw.width // 2, robot_y)

    frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (4, 5, 6))
    frame.paste(view.convert("RGB"), (0, VIEW_Y))
    draw = ImageDraw.Draw(frame)

    sx = robot_bg_x - crop_x
    sy = VIEW_Y + robot_bg_y - crop_y
    if 0 <= sx < VIEW_W and VIEW_Y <= sy < VIEW_Y + VIEW_H:
        draw.line((sx - 5, sy, sx + 5, sy), fill=(255, 60, 60))
        draw.line((sx, sy - 5, sx, sy + 5), fill=(255, 60, 60))

    cx = SCREEN_W // 2
    cy = VIEW_Y + VIEW_H // 2
    draw.line((cx - 5, cy, cx + 5, cy), fill=(230, 230, 230))
    draw.line((cx, cy - 5, cx, cy + 5), fill=(230, 230, 230))
    draw.rectangle((0, VIEW_Y, VIEW_W - 1, VIEW_Y + VIEW_H - 1), outline=(60, 60, 60))
    draw.text((4, 4), label, fill=(230, 230, 230))
    return frame


def draw_robot_meter(img: Image.Image, foot_x: int, robot_top_y: int) -> None:
    d = ImageDraw.Draw(img)
    bar_w = 44
    bar_h = 5
    cut = 3
    bar_x = int(clamp(foot_x - bar_w // 2, 2, VIEW_W - bar_w - 2))
    bar_y = int(clamp(robot_top_y - 10, 2, VIEW_H - bar_h - 2))

    armor_ratio = 0.72
    health_ratio = 1.00
    armor_fill = int(round((bar_w - 2) * armor_ratio))
    health_fill = int(round((bar_w - 2) * health_ratio))

    def slant_poly(x: int, y: int, w: int, h: int, cut_px: int) -> list[tuple[int, int]]:
        cut_px = int(clamp(cut_px, 0, max(0, w - 1)))
        return [
            (x, y),
            (x + w - 1 - cut_px, y),
            (x + w - 1, y + h - 1),
            (x, y + h - 1),
        ]

    d.polygon(slant_poly(bar_x, bar_y, bar_w, bar_h, cut), fill=(8, 8, 10))
    d.line((bar_x, bar_y, bar_x + bar_w - 1 - cut, bar_y), fill=(42, 42, 46))
    d.line((bar_x + bar_w - 1 - cut, bar_y, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y + bar_h - 1, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y, bar_x, bar_y + bar_h - 1), fill=(42, 42, 46))

    def draw_meter_fill(fill_w: int, color: tuple[int, int, int]) -> None:
        fill_w = int(clamp(fill_w, 0, bar_w - 2))
        if fill_w <= 0:
            return
        cap_w = 3
        body_w = min(fill_w, bar_w - 2 - cap_w)
        if body_w > 0:
            d.rectangle((bar_x + 1, bar_y + 1, bar_x + body_w, bar_y + bar_h - 2), fill=color)
        if fill_w >= (bar_w - 3):
            cap_x = bar_x + bar_w - 1 - cap_w
            d.polygon([
                (cap_x, bar_y + 1),
                (bar_x + bar_w - 1 - cut, bar_y + 1),
                (bar_x + bar_w - 2, bar_y + bar_h - 2),
                (cap_x, bar_y + bar_h - 2),
            ], fill=color)

    draw_meter_fill(health_fill, (238, 238, 232))
    draw_meter_fill(armor_fill, (255, 94, 38))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--background", default="ad_background/0_0.png")
    ap.add_argument("--bg-dir", default="ad_background")
    ap.add_argument("--robot", default="robot1.png")
    ap.add_argument("--robot-x", type=int, default=240)
    ap.add_argument("--robot-y", type=int, default=100)
    ap.add_argument("--out", default="Renderer/gun_preview/robot_ground_anchor_x240_y100.gif")
    ap.add_argument("--duration", type=int, default=120)
    ap.add_argument("--movement", action="store_true")
    ap.add_argument("--full-space", action="store_true")
    ap.add_argument("--grid-step-x", type=float, default=0.0)
    ap.add_argument("--grid-step-y", type=float, default=0.0)
    ap.add_argument("--scale-step-y", type=float, default=0.0)
    ap.add_argument("--y-to-x", type=float, default=0.0)
    args = ap.parse_args()

    robot = Image.open(args.robot).convert("RGBA")

    if args.movement:
        bgs = load_backgrounds(Path(args.bg_dir))
        if args.full_space:
            xs = sorted({coord[0] for coord in bgs})
            ys = sorted({coord[1] for coord in bgs})
            path = []
            for row, y in enumerate(ys):
                row_xs = xs if (row % 2) == 0 else list(reversed(xs))
                for x in row_xs:
                    if (x, y) in bgs:
                        path.append((x, y))
            if (0, 0) in bgs:
                path.insert(0, (0, 0))
                path.append((0, 0))
        else:
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
                (-4, 0),
                (-3, 0),
                (-2, 0),
                (-1, 0),
                (0, 0),
                (0, 1),
                (0, 2),
                (0, 3),
                (0, 2),
                (0, 1),
                (0, 0),
                (0, -1),
                (0, -2),
                (0, -1),
                (0, 0),
            ]
        frames = []
        for coord in path:
            bg = bgs.get(coord)
            if bg is None:
                continue
            center_x = (bg.width - VIEW_W) // 2
            center_y = (bg.height - VIEW_H) // 2
            robot_x = round(args.robot_x - coord[0] * args.grid_step_x + coord[1] * args.y_to_x)
            robot_y = round(args.robot_y - coord[1] * args.grid_step_y)
            scale = max(0.25, 1.0 + coord[1] * args.scale_step_y)
            frames.append(
                compose_frame(
                    bg,
                    robot,
                    robot_x,
                    robot_y,
                    center_x,
                    center_y,
                    f"pos={coord} step=({args.grid_step_x},{args.grid_step_y}) y2x={args.y_to_x} scale={scale:.2f}",
                    scale=scale,
                )
            )
        if not frames:
            raise SystemExit("No movement frames generated")
    else:
        bg = Image.open(args.background).convert("RGBA")
        center_x = (bg.width - VIEW_W) // 2
        center_y = (bg.height - VIEW_H) // 2
        samples = []

        for dx in (0, -80, -60, -40, -20, 0, 20, 40, 60, 80, 60, 40, 20, 0):
            samples.append((center_x + dx, center_y, f"look x={dx:+d}"))
        for dy in (-50, -30, -15, 0, 15, 30, 50, 30, 15, 0):
            samples.append((center_x, center_y + dy, f"look y={dy:+d}"))
        for dx, dy in ((-70, -35), (-35, -20), (0, 0), (35, 20), (70, 35), (35, 20), (0, 0)):
            samples.append((center_x + dx, center_y + dy, f"look x={dx:+d} y={dy:+d}"))

        frames = [
            compose_frame(
                bg,
                robot,
                args.robot_x,
                args.robot_y,
                crop_x,
                crop_y,
                f"{label} crop=({crop_x},{crop_y}) robot=({args.robot_x},{args.robot_y})",
            )
            for crop_x, crop_y, label in samples
        ]

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=args.duration,
        loop=0,
        optimize=False,
    )
    print(f"Saved {out}")


if __name__ == "__main__":
    main()
