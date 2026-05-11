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

WORLD_X_STEP = 12
WORLD_Y_STEP = -3
WORLD_Y_TO_X = 2


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def parse_coord(path: Path) -> tuple[int, int]:
    x_s, y_s = path.stem.split("_", 1)
    return int(x_s), int(y_s)


def load_backgrounds(bg_dir: Path) -> dict[tuple[int, int], Image.Image]:
    out: dict[tuple[int, int], Image.Image] = {}
    for path in bg_dir.glob("*.png"):
        try:
            out[parse_coord(path)] = Image.open(path).convert("RGBA")
        except ValueError:
            continue
    return out


def draw_dialog_line(frame: Image.Image, line: str, chars: int) -> None:
    draw = ImageDraw.Draw(frame)
    draw.rectangle((0, 0, SCREEN_W - 1, VIEW_Y - 1), fill=(4, 5, 6))
    shown = line[:chars]
    draw.text((8, 12), shown, fill=(220, 226, 232))
    if chars >= len(line):
        draw.text((8, 30), "Press reload to continue", fill=(112, 185, 255))


def compose_frame(
    bg: Image.Image,
    npc: Image.Image,
    npc_world_x: int,
    npc_world_y: int,
    grid_x: int,
    grid_y: int,
    crop_x: int,
    crop_y: int,
    dialog_chars: int,
    label: str,
) -> Image.Image:
    max_x = bg.width - VIEW_W
    max_y = bg.height - VIEW_H
    crop_x = int(clamp(crop_x, 0, max_x))
    crop_y = int(clamp(crop_y, 0, max_y))
    view = bg.crop((crop_x, crop_y, crop_x + VIEW_W, crop_y + VIEW_H)).convert("RGBA")

    npc_bg_x = npc_world_x - grid_x * WORLD_X_STEP + grid_y * WORLD_Y_TO_X
    npc_bg_y = npc_world_y - grid_y * WORLD_Y_STEP
    npc_x = npc_bg_x - crop_x - npc.width // 2
    npc_y = npc_bg_y - crop_y - npc.height
    if npc_x < VIEW_W and npc_y < VIEW_H and npc_x + npc.width > 0 and npc_y + npc.height > 0:
        view.alpha_composite(npc, (npc_x, npc_y))

    frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (4, 5, 6))
    frame.paste(view.convert("RGB"), (0, VIEW_Y))
    draw = ImageDraw.Draw(frame)
    draw.rectangle((0, VIEW_Y, VIEW_W - 1, VIEW_Y + VIEW_H - 1), outline=(60, 60, 60))
    draw.line((SCREEN_W // 2 - 5, VIEW_Y + VIEW_H // 2, SCREEN_W // 2 + 5, VIEW_Y + VIEW_H // 2), fill=(230, 230, 230))
    draw.line((SCREEN_W // 2, VIEW_Y + VIEW_H // 2 - 5, SCREEN_W // 2, VIEW_Y + VIEW_H // 2 + 5), fill=(230, 230, 230))
    draw.text((6, SCREEN_H - 17), label, fill=(170, 176, 184))
    draw_dialog_line(frame, "Bloodhound: I am Bloth Hoondr... you can call me Bloodhound.", dialog_chars)
    return frame


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bg-dir", default="ad_background")
    ap.add_argument("--npc", default="beginning/Bloodhound.png")
    ap.add_argument("--out", default="Renderer/gun_preview/beginning_bloodhound_preview.gif")
    ap.add_argument("--world-x", type=int, default=350)
    ap.add_argument("--world-y", type=int, default=213)
    ap.add_argument("--bg-x", type=int, default=0)
    ap.add_argument("--bg-y", type=int, default=0)
    ap.add_argument("--fps", type=int, default=12)
    args = ap.parse_args()

    bgs = load_backgrounds(Path(args.bg_dir))
    if not bgs:
        raise SystemExit("no backgrounds found")
    npc = Image.open(args.npc).convert("RGBA")

    coord = (args.bg_x, args.bg_y)
    bg = bgs.get(coord) or bgs[min(bgs.keys(), key=lambda c: abs(c[0] - coord[0]) + abs(c[1] - coord[1]))]
    max_crop_x = bg.width - VIEW_W
    max_crop_y = bg.height - VIEW_H
    center_x = max_crop_x // 2
    center_y = max_crop_y // 2
    samples = [
        (0, 0, center_x, center_y, "center"),
        (0, 0, center_x + 55, center_y, "look right"),
        (0, 0, center_x - 55, center_y, "look left"),
        (0, 0, center_x, center_y + 38, "look down"),
        (1, 0, center_x, center_y, "player strafe right"),
        (-1, 0, center_x, center_y, "player strafe left"),
        (0, 1, center_x, center_y, "player forward"),
        (0, -1, center_x, center_y, "player back"),
        (0, 0, center_x, center_y, "center again"),
    ]

    frames: list[Image.Image] = []
    line_len = len("Bloodhound: I am Bloth Hoondr... you can call me Bloodhound.")
    for i, (gx, gy, crop_x, crop_y, label) in enumerate(samples):
        for hold in range(8):
            chars = min(line_len, (i * 8 + hold + 1) * 2)
            frames.append(compose_frame(bg, npc, args.world_x, args.world_y, gx, gy, crop_x, crop_y, chars, label))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    duration = int(round(1000 / max(1, args.fps)))
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=duration, loop=0, optimize=False)
    still = out.with_suffix(".png")
    frames[0].save(still)
    print(f"saved {out}")
    print(f"saved {still}")


if __name__ == "__main__":
    main()
