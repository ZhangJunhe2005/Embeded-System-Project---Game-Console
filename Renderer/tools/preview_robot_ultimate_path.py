#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw


SCREEN_W = 240
SCREEN_H = 240
VIEW_W = 240
VIEW_H = 135
VIEW_Y = 52
ROBOT_GLOBAL_SCALE = 0.90


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def smoothstep(t: float) -> float:
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def perspective_step(x: float, y: float, dx: float, dy: float, ref_ground_y: float) -> tuple[float, float]:
    depth = clamp((y - (ref_ground_y - 96.0)) / 96.0, 0.45, 1.0)
    return x + dx * depth, y + dy * depth


def robot_scale_for_foot_y(foot_y: float, ground_y: float) -> float:
    t = clamp((foot_y - (ground_y - 44.0)) / 44.0, 0.0, 1.0)
    t = t * t
    return 0.62 + 0.38 * t


def load_robot_frames(robot_dir: Path) -> dict[str, Image.Image]:
    frames: dict[str, Image.Image] = {}
    for path in robot_dir.glob("*.png"):
        frames[path.stem] = Image.open(path).convert("RGBA")
    return frames


def load_timeline_frames(robot_dir: Path, prefix: str) -> dict[int, Image.Image]:
    frames: dict[int, Image.Image] = {}
    for path in robot_dir.glob(f"{prefix}*.png"):
        digits = "".join(c for c in path.stem if c.isdigit())
        if digits:
            frames[int(digits)] = Image.open(path).convert("RGBA")
    return frames


def expand_hold_timeline(real_frames: dict[int, Image.Image], start: int, end: int) -> list[Image.Image]:
    out: list[Image.Image] = []
    current: Image.Image | None = None
    for idx in range(start, end + 1):
        if idx in real_frames:
            current = real_frames[idx]
        if current is not None:
            out.append(current)
    return out


def draw_robot_meter(img: Image.Image, foot_x: int, robot_top_y: int) -> None:
    d = ImageDraw.Draw(img)
    bar_w = 44
    bar_h = 5
    cut = 3
    bar_x = int(clamp(foot_x - bar_w // 2, 2, VIEW_W - bar_w - 2))
    bar_y = int(clamp(robot_top_y - 10, 2, VIEW_H - bar_h - 2))
    armor_fill = int(round((bar_w - 2) * 1.0))
    health_fill = int(round((bar_w - 2) * 1.0))

    d.polygon(
        [
            (bar_x, bar_y),
            (bar_x + bar_w - 1 - cut, bar_y),
            (bar_x + bar_w - 1, bar_y + bar_h - 1),
            (bar_x, bar_y + bar_h - 1),
        ],
        fill=(8, 8, 10),
    )
    d.line((bar_x, bar_y, bar_x + bar_w - 1 - cut, bar_y), fill=(42, 42, 46))
    d.line((bar_x + bar_w - 1 - cut, bar_y, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y + bar_h - 1, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y, bar_x, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.rectangle((bar_x + 1, bar_y + 1, bar_x + health_fill, bar_y + bar_h - 2), fill=(238, 238, 232))
    d.rectangle((bar_x + 1, bar_y + 1, bar_x + armor_fill, bar_y + bar_h - 2), fill=(255, 94, 38))


@dataclass
class Segment:
    name: str
    duration: int
    x0: float
    y0: float
    x1: float
    y1: float
    lift0: float
    lift1: float
    arc: float
    mode: str


def segment_pose(seg: Segment, local_ms: int, run_frames: list[Image.Image], jump_frames: list[Image.Image], frames: dict[str, Image.Image]) -> Image.Image:
    if seg.mode == "run":
        return run_frames[(local_ms // 110) % len(run_frames)]
    if seg.mode == "slide_enter":
        return jump_frames[min(local_ms // 90, 1)]
    if seg.mode == "slide_hold":
        return jump_frames[1]
    if seg.mode == "jump":
        idx = min(local_ms // 70, len(jump_frames) - 1)
        return jump_frames[idx]
    if seg.mode == "wall":
        return frames["dengqiang25"]
    if seg.mode == "kick":
        idx = min(local_ms // 70, len(jump_frames) - 1)
        return jump_frames[idx]
    if seg.mode == "land":
        return frames["luodi"]
    if seg.mode == "lurch":
        return frames["lurch"]
    return run_frames[0]


def compose_frame(bg: Image.Image, robot: Image.Image, foot_x: float, foot_y: float, crop_x: int, crop_y: int, label: str, scale: float) -> Image.Image:
    max_x = bg.width - VIEW_W
    max_y = bg.height - VIEW_H
    crop_x = int(clamp(crop_x, 0, max_x))
    crop_y = int(clamp(crop_y, 0, max_y))
    view = bg.crop((crop_x, crop_y, crop_x + VIEW_W, crop_y + VIEW_H)).convert("RGBA")

    if abs(scale - 1.0) > 0.001:
        rw = max(1, round(robot.width * scale))
        rh = max(1, round(robot.height * scale))
        robot = robot.resize((rw, rh), Image.Resampling.NEAREST)
    robot_x = round(foot_x - crop_x - robot.width / 2.0)
    robot_y = round(foot_y - crop_y - robot.height)
    if robot_x < VIEW_W and robot_y < VIEW_H and robot_x + robot.width > 0 and robot_y + robot.height > 0:
        view.alpha_composite(robot, (robot_x, robot_y))
        draw_robot_meter(view, robot_x + robot.width // 2, robot_y)

    frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (4, 5, 6))
    frame.paste(view.convert("RGB"), (0, VIEW_Y))
    draw = ImageDraw.Draw(frame)
    cx = SCREEN_W // 2
    cy = VIEW_Y + VIEW_H // 2
    draw.line((cx - 5, cy, cx + 5, cy), fill=(230, 230, 230))
    draw.line((cx, cy - 5, cx, cy + 5), fill=(230, 230, 230))
    draw.rectangle((0, VIEW_Y, VIEW_W - 1, VIEW_Y + VIEW_H - 1), outline=(60, 60, 60))
    draw.text((4, 4), label, fill=(230, 230, 230))
    return frame


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--background", default="ad_background/0_0.png")
    ap.add_argument("--robot-dir", default="robot")
    ap.add_argument("--out", default="Renderer/gun_preview/robot_ultimate_path_preview.gif")
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--crop-x", type=int, default=120)
    ap.add_argument("--crop-y", type=int, default=72)
    args = ap.parse_args()

    bg = Image.open(args.background).convert("RGBA")
    root = Path(args.robot_dir)
    normal = load_robot_frames(root)
    ultimate_dir = root / "ultimate"
    ultimate = load_robot_frames(ultimate_dir)
    deng = load_timeline_frames(ultimate_dir, "dengqiang")

    run_frames = [deng[0], deng[0], deng[0], deng[3], deng[3], deng[3]]
    jump_frames = expand_hold_timeline(deng, 18, 37)
    if not run_frames or not jump_frames:
        raise SystemExit("missing dengqiang frames")

    ground_y = 204.0
    run_x0 = 218.0
    run_y0 = ground_y - 18.0
    run_dx = 12.0
    run_dy = 14.0
    run_x1, run_y1 = perspective_step(run_x0, run_y0, run_dx, run_dy, ground_y)
    slide_x1, slide_y1 = perspective_step(run_x1, run_y1, run_dx * 0.65, run_dy * 0.65, ground_y)
    slide_y1 -= 6.0
    wall_x = 300.0
    wall_y = ground_y - 12.0
    segments = [
        Segment("run", 520, run_x0, run_y0, run_x1, run_y1, 0, 0, 0, "run"),
        Segment("crouch-slide", 80, run_x1, run_y1, run_x1 + (slide_x1 - run_x1) * 0.25, run_y1 + (slide_y1 - run_y1) * 0.25, 0, 0, 0, "slide_enter"),
        Segment("slide-hold", 230, run_x1 + (slide_x1 - run_x1) * 0.25, run_y1 + (slide_y1 - run_y1) * 0.25, slide_x1, slide_y1, 0, 0, 0, "slide_hold"),
        Segment("low-right-jump", 390, slide_x1, slide_y1, wall_x, wall_y, 0, 12, 10, "jump"),
        Segment("wall-contact", 55, wall_x, wall_y, wall_x, wall_y, 12, 12, 0, "wall"),
        Segment("wall-kick-left-up", 610, wall_x, wall_y, 178, ground_y - 8, 12, 0, 30, "kick"),
        Segment("land-left", 110, 178, ground_y - 8, 178, ground_y, 0, 0, 0, "land"),
        Segment("lurch-right", 260, 178, ground_y, 214, ground_y - 2, 0, 2, 2, "lurch"),
        Segment("lurch-left", 310, 214, ground_y - 2, 168, ground_y + 1, 2, 0, 2, "lurch"),
        Segment("lurch-recover", 260, 168, ground_y + 1, 194, ground_y, 0, 0, 1, "lurch"),
    ]

    dt = int(round(1000 / max(1, args.fps)))
    total = sum(s.duration for s in segments)
    frames: list[Image.Image] = []
    t = 0
    seg_start = 0
    seg_i = 0
    while t < total:
        while seg_i + 1 < len(segments) and t >= seg_start + segments[seg_i].duration:
            seg_start += segments[seg_i].duration
            seg_i += 1
        seg = segments[seg_i]
        local = t - seg_start
        p = clamp(local / max(1, seg.duration), 0.0, 1.0)
        e = smoothstep(p)
        x = lerp(seg.x0, seg.x1, e)
        y = lerp(seg.y0, seg.y1, e)
        lift = lerp(seg.lift0, seg.lift1, e) + seg.arc * 4.0 * p * (1.0 - p)
        robot = segment_pose(seg, local, run_frames, jump_frames, {**ultimate, **{f"dengqiang{k}": v for k, v in deng.items()}})
        foot_y = y - lift
        if seg.mode in ("run", "slide_enter", "slide_hold", "lurch"):
            scale = robot_scale_for_foot_y(foot_y, ground_y)
        else:
            scale = 1.0
        scale *= ROBOT_GLOBAL_SCALE
        frames.append(compose_frame(bg, robot, x, foot_y, args.crop_x, args.crop_y, f"{seg.name} t={t}ms foot=({x:.0f},{foot_y:.0f}) s={scale:.2f}", scale))
        t += dt

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=dt,
        loop=0,
        optimize=False,
    )
    print(f"Saved {out} frames={len(frames)} duration={total}ms")


if __name__ == "__main__":
    main()
