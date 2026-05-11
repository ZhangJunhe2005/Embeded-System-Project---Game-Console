#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw

WIN_W = {"left": 72, "center": 96, "right": 72}
# Wall sampling anchors are fixed to inner sides.
# Legacy-correct motion: center follows yaw by draw position.
# Keep center sampling fixed by default to preserve old feel when source gets wider.
CENTER_SAMPLE_GAIN = 1.0
CENTER_DRAW_GAIN = 1.0

SCREEN_W = 240
SCREEN_H = 240

# Max wall-width swing in pixels for side deformation.
# yaw > 0 means looking left: left wall wider, right wall narrower.
MAX_LAYOUT_DELTA = 24
SEAM_OVERLAP = 1


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb565_to_rgb888(v: int) -> tuple[int, int, int]:
    r5 = (v >> 11) & 0x1F
    g6 = (v >> 5) & 0x3F
    b5 = v & 0x1F
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    return r8, g8, b8


def simulate_rgb565(img: Image.Image) -> Image.Image:
    src = img.convert("RGB")
    w, h = src.size
    out = Image.new("RGB", (w, h))
    spx = src.load()
    opx = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = spx[x, y]
            opx[x, y] = rgb565_to_rgb888(rgb888_to_rgb565(r, g, b))
    return out


def build_shared_palette(images: dict[str, Image.Image], colors: int) -> list[int]:
    src_h = next(iter(images.values())).height
    total_w = sum(img.width for img in images.values())
    atlas = Image.new("RGB", (total_w, src_h))
    x = 0
    for key in ("left", "center", "right"):
        img = images[key]
        atlas.paste(img, (x, 0))
        x += img.width

    q = atlas.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    return q.getpalette()[: colors * 3]


def apply_shared_palette(img: Image.Image, palette: list[int]) -> Image.Image:
    pal = Image.new("P", (1, 1))
    full_palette = palette + [0] * (768 - len(palette))
    pal.putpalette(full_palette)
    idx = img.quantize(palette=pal, dither=Image.Dither.NONE)
    return idx.convert("RGB")


def build_image_palette(img: Image.Image, colors: int) -> list[int]:
    q = img.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    return q.getpalette()[: colors * 3]


def load_layers(input_dir: Path, do_rgb565: bool, simulate_8bit: bool, palette_colors: int, palette_scope: str):
    files = {
        "left": input_dir / "leftwall.png",
        "center": input_dir / "center.png",
        "right": input_dir / "rightwall.png",
    }
    layers = {}
    src_h = None
    for k, p in files.items():
        img = Image.open(p).convert("RGB")
        if src_h is None:
            src_h = img.height
        if img.height != src_h:
            raise ValueError(f"{p.name} height must match {src_h}, got {img.height}")
        if img.width < WIN_W[k]:
            raise ValueError(f"{p.name} width {img.width} < window width {WIN_W[k]}")
        layers[k] = img

    if src_h is None:
        raise ValueError("No layers loaded")

    info = {
        "mode": "rgb888",
        "palette_colors": 0,
        "palette_scope": palette_scope,
    }

    if simulate_8bit:
        colors = int(clamp(palette_colors, 2, 256))
        if palette_scope == "per-layer":
            for k in layers:
                layer_palette = build_image_palette(layers[k], colors)
                layers[k] = apply_shared_palette(layers[k], layer_palette)
            info["mode"] = "8bit-lut(per-layer)"
        else:
            shared_palette = build_shared_palette(layers, colors)
            for k in layers:
                layers[k] = apply_shared_palette(layers[k], shared_palette)
            info["mode"] = "8bit-lut(shared)"
        info["palette_colors"] = colors

    if do_rgb565:
        for k in layers:
            layers[k] = simulate_rgb565(layers[k])
        info["mode"] += "+rgb565"

    info["src_h"] = src_h
    return layers, info


def compute_layout(yaw: float, max_layout_delta: float, center_draw_gain: float):
    # Center is always full image width and fixed at screen center.
    center_w = 126
    x_center_base = (SCREEN_W - center_w) // 2
    x_center = int(round(clamp(x_center_base + center_draw_gain * yaw, 0, SCREEN_W - center_w)))

    # Side walls fill the two side gaps; only side width split changes with yaw.
    side_total = SCREEN_W - center_w
    half = side_total // 2
    d = int(round(clamp(yaw, -max_layout_delta, max_layout_delta)))
    left_w = int(clamp(half + d, 20, side_total - 20))
    right_w = side_total - left_w

    # Draw side layers pinned to screen outer edges to avoid border gaps.
    # Center stays fixed and may be partially covered near its side edges.
    x_left = 0
    x_right = SCREEN_W - right_w

    return {
        "left": left_w,
        "center": center_w,
        "right": right_w,
        "x_left": x_left,
        "x_center": x_center,
        "x_right": x_right,
    }


def compute_layout_sync(yaw: float, yaw_range: float, center_draw_px: float, side_delta_px: float, gamma: float):
    center_w = 126
    x_center_base = (SCREEN_W - center_w) // 2

    side_total = SCREEN_W - center_w
    half = side_total // 2
    max_side_delta = max(0, half - 20)

    denom = max(1e-6, yaw_range)
    t = clamp(yaw / denom, -1.0, 1.0)
    t_e = (abs(t) ** max(0.01, gamma)) * (1.0 if t >= 0 else -1.0)

    x_center = int(round(clamp(x_center_base + center_draw_px * t_e, 0, SCREEN_W - center_w)))
    d = int(round(clamp(side_delta_px * t_e, -max_side_delta, max_side_delta)))
    left_w = int(clamp(half + d, 20, side_total - 20))
    right_w = side_total - left_w

    return {
        "left": left_w,
        "center": center_w,
        "right": right_w,
        "x_left": 0,
        "x_center": x_center,
        "x_right": SCREEN_W - right_w,
    }


def compose_frame(
    layers,
    yaw: float,
    y_offset: int,
    y_max: int,
    move_x: float,
    move_fb: float,
    retreat_only: bool,
    fb_center_scale: float,
    fb_width_gamma: float,
    center_resize: bool,
    fb_side_min: int,
    fb_depth_px: int,
    fb_center_depth_ratio: float,
    fb_wall_depth_ratio: float,
    center_sample_with_yaw: bool,
    max_layout_delta: float,
    center_draw_gain: float,
    seam_overlap: int,
    outer_guard: int,
    sync_motion: bool,
    sync_yaw_range: float,
    sync_center_draw_px: float,
    sync_side_delta_px: float,
    sync_gamma: float,
    center_phase_y: float | None = None,
    wall_phase_y: float | None = None,
    strafe_phase_x: float | None = None,
    strafe_side_scale: float = 0.8,
    strafe_left_parallax: float = 1.0,
    strafe_center_parallax: float = 0.8,
    strafe_right_parallax: float = 1.0,
    strafe_width_delta: float = 4.0,
    strafe_mode: bool = False,
):
    frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (0, 0, 0))
    y_base = int(clamp(y_offset, 0, y_max))
    sx_mode = move_x if strafe_phase_x is None else strafe_phase_x
    if strafe_mode and abs(float(sx_mode)) < 0.5:
        # Midpoint snap: force exact baseline at center crossing.
        sx_mode = 0.0
    strafe_active = strafe_mode
    strafe_midpoint = strafe_mode and abs(float(sx_mode)) < 1e-6
    effective_seam_overlap = 0 if strafe_mode else seam_overlap
    center_resize_effective = center_resize and (not strafe_mode)
    if sync_motion:
        layout = compute_layout_sync(yaw, sync_yaw_range, sync_center_draw_px, sync_side_delta_px, sync_gamma)
    else:
        layout = compute_layout(yaw, max_layout_delta, center_draw_gain)

    # Forward/back movement should feel different from pitch:
    # emulate dolly by changing center panel size and side-wall occupancy.
    if retreat_only:
        retreat = float(clamp(move_fb, 0.0, 1.0))
        # Near->far retreat should narrow center and expand side walls.
        fb = -retreat
    else:
        fb = float(clamp(move_fb, -1.0, 1.0))
    fb_w = (abs(fb) ** float(clamp(fb_width_gamma, 0.2, 2.0))) * (1.0 if fb >= 0 else -1.0)
    base_center_w = float(layout["center"])
    min_side = float(clamp(fb_side_min, 4, 40))
    center_scale = float(clamp(fb_center_scale, 0.0, 1.2))
    center_src_w = float(layers["center"].width)
    center_max_w = float(min(center_src_w, SCREEN_W - int(2 * min_side)))
    center_w_f = float(clamp(base_center_w * (1.0 + fb_w * center_scale), 96.0, center_max_w))
    side_total_f = float(SCREEN_W) - center_w_f
    base_side_total = float(max(1, layout["left"] + layout["right"]))
    if retreat_only:
        # Step 1: synchronized opening/closing for both walls.
        # Step 3: keep yaw contribution small during retreat/forward.
        yaw_bias_gain = 0.22
        half_f = side_total_f * 0.5
        d_full = (float(layout["left"]) - float(layout["right"])) * 0.5
        d_f = d_full * yaw_bias_gain
        left_w_f = float(clamp(half_f + d_f, min_side, side_total_f - min_side))
        right_w_f = side_total_f - left_w_f
    else:
        left_ratio = float(layout["left"]) / base_side_total
        left_w_f = float(clamp(side_total_f * left_ratio, min_side, side_total_f - min_side))
        right_w_f = side_total_f - left_w_f

    sx_for_width = move_x if strafe_phase_x is None else strafe_phase_x
    side_shift_f = float(sx_for_width) * float(clamp(strafe_side_scale, 0.0, 2.0))
    left_w_f = float(clamp(left_w_f - side_shift_f, min_side, side_total_f - min_side))
    right_w_f = side_total_f - left_w_f

    center_w = int(round(center_w_f))
    left_w = int(round(left_w_f))
    right_w = max(1, SCREEN_W - center_w - left_w)
    # Keep total width exact after rounding.
    left_w = int(clamp(left_w, int(min_side), SCREEN_W - center_w - int(min_side)))
    right_w = SCREEN_W - center_w - left_w

    if strafe_active:
        # Strafe principles:
        # 1) center stays complete and near-fixed on screen
        # 2) no width change; only src_x parallax is used as main cue
        center_w = int(round(base_center_w))
        side_total = SCREEN_W - center_w
        left_w = side_total // 2
        right_w = side_total - left_w

    old_center_base_f = (float(SCREEN_W) - base_center_w) * 0.5
    center_shift_f = float(layout["x_center"]) - old_center_base_f
    new_center_base_f = (float(SCREEN_W) - center_w_f) * 0.5
    # Keep center layer visually centered for forward/back; only yaw contributes lateral shift.
    x_center_f = float(clamp(new_center_base_f + center_shift_f, 0.0, float(SCREEN_W - center_w)))
    x_center = int(round(x_center_f))

    layout["center"] = center_w
    layout["left"] = left_w
    layout["right"] = right_w
    layout["x_left"] = 0
    if strafe_active:
        # Hard seam lock in strafe mode: walls meet center exactly at boundaries.
        layout["x_center"] = left_w
    else:
        x_center = int(clamp(x_center, left_w - effective_seam_overlap, left_w + effective_seam_overlap))
        layout["x_center"] = x_center
    layout["x_right"] = SCREEN_W - right_w

    # True forward/back cue: depth sampling moves in source Y (different per layer).
    depth_px = float(clamp(fb_depth_px, 0, 120))
    center_ratio = float(clamp(fb_center_depth_ratio, 0.0, 1.0))
    wall_ratio = float(clamp(fb_wall_depth_ratio, 0.0, 1.0))
    center_y0_f = float(clamp(float(y_base) + (fb * depth_px * center_ratio), 0.0, float(y_max)))
    wall_y0_f = float(clamp(float(y_base) + (fb * depth_px * wall_ratio), 0.0, float(y_max)))
    if center_phase_y is not None:
        center_y0_f = float(clamp(center_phase_y, 0.0, float(y_max)))
    if wall_phase_y is not None:
        wall_y0_f = float(clamp(wall_phase_y, 0.0, float(y_max)))
    center_y0 = int(round(center_y0_f))
    wall_y0 = int(round(wall_y0_f))

    # Draw walls first, then center overdraw to hide seams while keeping center visually stable.
    for k in ("left", "right", "center"):
        src = layers[k]
        win_w = layout[k]
        shift_max = src.width - win_w
        if shift_max < 0:
            # Fall back to fixed width if dynamic window exceeds source width.
            win_w = min(src.width, WIN_W[k])
            shift_max = src.width - win_w

        # Anchor rule requested by user:
        # - left wall always pinned to source right edge (inner side fixed)
        # - right wall always pinned to source left edge (inner side fixed)
        # - only outer-side reveal changes via dynamic window widths
        if k == "left":
            x0 = shift_max
        elif k == "right":
            x0 = 0
        else:
            if center_resize_effective:
                # Center layer: stable sampling + draw-time resize (heavier, closer to true zoom).
                win_w = min(src.width, base_center_w)
            else:
                # Board-friendly mode: no runtime resize, only change sampled/drawn width.
                win_w = min(src.width, layout["center"])
            shift_max = src.width - win_w
            base_x = shift_max / 2.0
            if center_sample_with_yaw:
                x0 = int(round(clamp(base_x + CENTER_SAMPLE_GAIN * yaw, 0, shift_max)))
            else:
                x0 = int(round(base_x))

        # Strafe is driven mainly by source-window X movement with mild parallax.
        sx = sx_mode
        if k == "center":
            x0 = int(round(clamp(base_x + sx * strafe_center_parallax, 0, shift_max)))
        elif k == "left":
            x0 = int(round(clamp(x0 + sx * strafe_left_parallax, 0, shift_max)))
        else:
            x0 = int(round(clamp(x0 + sx * strafe_right_parallax, 0, shift_max)))

        if strafe_midpoint:
            # Hard rule at strafe midpoint: exact baseline source crop positions.
            if k == "left":
                x0 = shift_max
            elif k == "right":
                x0 = 0
            else:
                x0 = int(round(clamp(base_x, 0, shift_max)))

        if k == "left" and outer_guard > 0:
            x0 = max(x0, outer_guard)
            win_w = min(win_w, src.width - x0)
        elif k == "right" and outer_guard > 0:
            win_w = min(win_w, max(1, src.width - outer_guard))

        center_draw_shift = 0
        if k == "center" and effective_seam_overlap > 0:
            # Expand center layer to cover both side seams symmetrically.
            left_extra = min(effective_seam_overlap, x0)
            right_extra = min(effective_seam_overlap, src.width - (x0 + win_w))
            x0 -= left_extra
            win_w += left_extra + right_extra
            center_draw_shift = left_extra

        sample_y0 = center_y0 if k == "center" else wall_y0
        crop = src.crop((x0, sample_y0, x0 + win_w, sample_y0 + SCREEN_H))

        if k == "center" and center_resize_effective:
            draw_w_target = int(layout["center"])
            if effective_seam_overlap > 0:
                draw_w_target += 2 * effective_seam_overlap
            if draw_w_target > 0 and draw_w_target != crop.width:
                crop = crop.resize((draw_w_target, SCREEN_H), Image.Resampling.BILINEAR)

        if k == "left":
            draw_x = layout["x_left"]
        elif k == "center":
            draw_x = max(0, layout["x_center"] - center_draw_shift - effective_seam_overlap)
        else:
            draw_x = layout["x_right"]

        frame.paste(crop, (draw_x, 0))
    return frame, layout


def draw_overlay(img: Image.Image, yaw: float, y_offset: int, rgb565_on: bool, layout, mode_text: str, center_motion_text: str):
    out = img.copy()
    d = ImageDraw.Draw(out)
    d.rectangle((0, 0, SCREEN_W, 14), fill=(0, 0, 0))
    d.text(
        (2, 2),
        f"yaw={yaw:+.1f} y={y_offset} w={layout['left']}/{layout['center']}/{layout['right']} rgb565={'on' if rgb565_on else 'off'} mode={mode_text} center={center_motion_text}",
        fill=(255, 255, 0),
    )
    return out


def draw_fb_linked_overlay(img: Image.Image, move_fb: float, link_enemy: bool, link_crosshair: bool, link_weapon: bool):
        out = img.copy()
        d = ImageDraw.Draw(out)
        fb01 = float(clamp((move_fb + 1.0) * 0.5, 0.0, 1.0))

        cx = SCREEN_W // 2
        cy = SCREEN_H // 2

        if link_crosshair:
            # Slightly scale/offset crosshair with forward motion.
            half = int(round(4 + fb01 * 2))
            y_shift = int(round(fb01 * 2))
            y = cy + y_shift
            d.line((cx - half, y, cx + half, y), fill=(255, 255, 255), width=1)
            d.line((cx, y - half, cx, y + half), fill=(255, 255, 255), width=1)

        if link_enemy:
            # Placeholder enemy proxy: larger and slightly lower when approaching.
            sz = int(round(10 + fb01 * 16))
            ey = int(round(92 + fb01 * 18))
            ex0 = cx - sz // 2
            ey0 = ey - sz // 2
            d.rectangle((ex0, ey0, ex0 + sz, ey0 + sz), outline=(255, 220, 120), width=2)

        if link_weapon:
            # Placeholder weapon proxy: occupies more lower screen area when moving forward.
            gun_h = int(round(16 + fb01 * 24))
            gun_w = int(round(72 + fb01 * 32))
            gx0 = cx - gun_w // 2
            gy0 = SCREEN_H - gun_h - 6
            d.rectangle((gx0, gy0, gx0 + gun_w, gy0 + gun_h), fill=(60, 60, 70), outline=(180, 180, 190), width=1)

        return out


def draw_center_crosshair(img: Image.Image, half: int) -> Image.Image:
    out = img.copy()
    d = ImageDraw.Draw(out)
    cx = SCREEN_W // 2
    cy = SCREEN_H // 2
    h = int(clamp(half, 2, 20))
    d.line((cx - h, cy, cx + h, cy), fill=(255, 255, 255), width=1)
    d.line((cx, cy - h, cx, cy + h), fill=(255, 255, 255), width=1)
    return out


def paste_gun_overlay(
    img: Image.Image,
    gun_img: Image.Image | None,
    gun_scale: float,
    gun_offset_x: int,
    gun_offset_y: int,
) -> Image.Image:
    if gun_img is None:
        return img

    scale = float(clamp(gun_scale, 0.2, 3.0))
    if abs(scale - 1.0) > 1e-6:
        w = max(1, int(round(gun_img.width * scale)))
        h = max(1, int(round(gun_img.height * scale)))
        gun = gun_img.resize((w, h), Image.Resampling.BILINEAR)
    else:
        gun = gun_img

    gx = (SCREEN_W - gun.width) // 2 + int(gun_offset_x)
    gy = SCREEN_H - gun.height + int(gun_offset_y)

    out = img.copy()
    out.paste(gun, (gx, gy), gun)
    return out


def gun_motion_offset(mode: str, frame_idx: int, frame_count: int, fps: int) -> tuple[int, int]:
    if mode == "none" or frame_count <= 1:
        return 0, 0

    t = frame_idx / max(1, frame_count - 1)
    idle_x = int(round(math.sin(t * math.tau * 0.75) * 1.0))
    idle_y = int(round(math.sin(t * math.tau) * 2.0))

    if mode == "idle":
        return idle_x, idle_y

    if mode == "fire":
        # One shot at the start of the loop: a quick upward kick followed by damped recovery.
        seconds = frame_idx / max(1, fps)
        recoil_y = -10.0 * math.exp(-4.8 * seconds)
        recoil_x = 2.0 * math.exp(-5.5 * seconds) * math.sin(32.0 * seconds)
        return int(round(idle_x + recoil_x)), int(round(idle_y + recoil_y))

    return 0, 0


def smoothstep(t: float) -> float:
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def weapon_swap_offsets(frame_idx: int, frame_count: int) -> tuple[bool, int, int, bool, int, int]:
    t = frame_idx / max(1, frame_count - 1)
    if t < 0.40:
        u = smoothstep(t / 0.40)
        return True, int(round(-8 * u)), int(round(76 * u)), False, 0, 0
    if t < 0.52:
        return False, 0, 0, False, 0, 0
    u = smoothstep((t - 0.52) / 0.48)
    settle = int(round((1.0 - u) * 76))
    return False, 0, 0, True, 0, settle


def natural_key(path: Path) -> list[int | str]:
    parts = re.split(r"(\d+)", path.name)
    return [int(p) if p.isdigit() else p.lower() for p in parts]


def load_weapon_sequence(seq_dir: str | None, blank_after: int) -> list[Image.Image | None]:
    if not seq_dir:
        return []
    files = [p for p in Path(seq_dir).iterdir() if p.suffix.lower() in {".png", ".jpg", ".jpeg"}]
    frames: list[Image.Image | None] = []
    for i, path in enumerate(sorted(files, key=natural_key), start=1):
        frames.append(Image.open(path).convert("RGBA"))
        if blank_after > 0 and i == blank_after:
            frames.append(None)
    return frames


def paste_robot_overlay(
    img: Image.Image,
    robot_frames: list[Image.Image],
    crack_img: Image.Image | None,
    die_img: Image.Image | None,
    frame_idx: int,
    foot_x: int,
    foot_y: int,
    patrol_px: int,
    step_px: int,
    face_dir: int,
    armor_ratio: float,
    health_ratio: float,
    health_chip_ratio: float,
    health_chip_age: int,
    crack_active: bool,
    die_active: bool,
) -> tuple[Image.Image, int, int]:
    if not robot_frames:
        return img, foot_x, face_dir

    seq = [0, 1, 2, 1, 0, 1, 2, 1, 0]
    phase = frame_idx % len(seq)
    local_idx = seq[phase]
    turn_frame = local_idx == 2

    next_x = foot_x + face_dir * step_px
    left_limit = (SCREEN_W // 2) - patrol_px
    right_limit = (SCREEN_W // 2) + patrol_px
    if turn_frame and (next_x < left_limit or next_x > right_limit):
        face_dir = -face_dir
        next_x = foot_x + face_dir * step_px
    foot_x = int(clamp(next_x, left_limit, right_limit))

    robot = robot_frames[local_idx]
    if crack_active and crack_img is not None:
        robot = crack_img
    if die_active and die_img is not None:
        robot = die_img
    if face_dir < 0:
        robot = robot.transpose(Image.Transpose.FLIP_LEFT_RIGHT)

    draw_x = foot_x - robot.width // 2
    draw_y = foot_y - robot.height
    out = img.copy()
    out.paste(robot, (draw_x, draw_y), robot)

    bar_w = 54
    bar_h = 6
    bar_x = int(clamp(foot_x - bar_w // 2, 2, SCREEN_W - bar_w - 2))
    bar_y = int(clamp(draw_y - 12, 2, SCREEN_H - 14))
    d = ImageDraw.Draw(out)

    armor_ratio = clamp(armor_ratio, 0.0, 1.0)
    health_ratio = clamp(health_ratio, 0.0, 1.0)
    armor_fill = int(round((bar_w - 2) * armor_ratio))
    health_fill = int(round((bar_w - 2) * health_ratio))

    def slant_poly(x: int, y: int, w: int, h: int, cut: int) -> list[tuple[int, int]]:
        cut = int(clamp(cut, 0, max(0, w - 1)))
        return [
            (x, y),
            (x + w - 1 - cut, y),
            (x + w - 1, y + h - 1),
            (x, y + h - 1),
        ]

    d.polygon(slant_poly(bar_x, bar_y, bar_w, bar_h, 4), fill=(8, 8, 10))
    d.line((bar_x, bar_y, bar_x + bar_w - 5, bar_y), fill=(42, 42, 46))
    d.line((bar_x + bar_w - 5, bar_y, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y + bar_h - 1, bar_x + bar_w - 1, bar_y + bar_h - 1), fill=(42, 42, 46))
    d.line((bar_x, bar_y, bar_x, bar_y + bar_h - 1), fill=(42, 42, 46))

    def draw_meter_fill(fill_w: int, color: tuple[int, int, int]) -> None:
        fill_w = int(clamp(fill_w, 0, bar_w - 2))
        if fill_w <= 0:
            return
        cap_w = 4
        body_w = min(fill_w, bar_w - 2 - cap_w)
        if body_w > 0:
            d.rectangle((bar_x + 1, bar_y + 1, bar_x + body_w, bar_y + bar_h - 2), fill=color)
        if fill_w >= (bar_w - 3):
            cap_x = bar_x + bar_w - 1 - cap_w
            d.polygon([
                (cap_x, bar_y + 1),
                (bar_x + bar_w - 5, bar_y + 1),
                (bar_x + bar_w - 2, bar_y + bar_h - 2),
                (cap_x, bar_y + bar_h - 2),
            ], fill=color)

    draw_meter_fill(health_fill, (238, 238, 232))
    draw_meter_fill(armor_fill, (255, 94, 38))

    if health_chip_ratio > 0.0 and health_chip_age >= 0:
        chip_age = int(clamp(health_chip_age, 0, 8))
        base_chip_w = max(8, int(round((bar_w - 2) * clamp(health_chip_ratio, 0.0, 1.0))))
        chip_colors = [
            (246, 246, 240),
            (218, 218, 220),
            (174, 174, 182),
            (118, 118, 130),
        ]
        base_h = bar_h - 2
        widths = [2, 3, 4, 5]
        heights = [base_h, base_h + 2, base_h + 4, base_h + 6]
        if base_chip_w >= 12:
            widths = [2, 3, 4, 5]
        total_w = sum(widths)
        start_x = int(clamp(bar_x + 1 + health_fill,
                            bar_x + 1,
                            bar_x + bar_w - 1 - total_w))
        cy = bar_y + bar_h // 2
        for part, color in enumerate(chip_colors):
            grow = 1 if chip_age >= 5 and part >= 2 else 0
            chip_w = widths[part] + grow
            chip_h = heights[part] + grow
            chip_x0 = start_x + sum(widths[:part])
            chip_y0 = cy - chip_h // 2
            d.rectangle((chip_x0, chip_y0, chip_x0 + chip_w - 1, chip_y0 + chip_h - 1), fill=color)
    return out, foot_x, face_dir


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-dir", required=True)
    ap.add_argument("--out")
    ap.add_argument("--gif")
    ap.add_argument("--anim", choices=["yaw", "pitch", "both", "move", "strafe"], default="yaw")
    ap.add_argument("--preset", choices=["none", "mature-sync"], default="none")
    ap.add_argument("--yaw", type=float, default=0.0)
    ap.add_argument("--y-offset", type=int, default=40)
    ap.add_argument("--yaw-range", type=float, default=28.0)
    ap.add_argument("--pitch-range", type=int, default=36)
    ap.add_argument("--move-x", type=float, default=0.0)
    ap.add_argument("--move-fb", type=float, default=0.0)
    ap.add_argument("--move-range-x", type=float, default=18.0)
    ap.add_argument("--move-range-fb", type=float, default=1.0)
    ap.add_argument("--move-fb-start", type=float, default=999.0)
    ap.add_argument("--move-fb-end", type=float, default=999.0)
    ap.add_argument("--retreat-only", action="store_true")
    ap.add_argument("--retreat-start", type=float, default=0.0)
    ap.add_argument("--retreat-end", type=float, default=1.0)
    ap.add_argument("--fb-center-scale", type=float, default=0.60)
    ap.add_argument("--fb-width-gamma", type=float, default=1.0)
    ap.add_argument("--center-resize", action="store_true")
    ap.add_argument("--fb-side-min", type=int, default=10)
    ap.add_argument("--fb-depth-px", type=int, default=54)
    ap.add_argument("--fb-center-depth-ratio", type=float, default=0.0)
    ap.add_argument("--fb-wall-depth-ratio", type=float, default=0.65)
    ap.add_argument("--strafe-side-scale", type=float, default=0.80)
    ap.add_argument("--strafe-left-parallax", type=float, default=1.10)
    ap.add_argument("--strafe-center-parallax", type=float, default=0.55)
    ap.add_argument("--strafe-right-parallax", type=float, default=0.95)
    ap.add_argument("--strafe-width-delta", type=float, default=4.0)
    ap.add_argument("--link-enemy", action="store_true")
    ap.add_argument("--link-crosshair", action="store_true")
    ap.add_argument("--link-weapon", action="store_true")
    ap.add_argument("--frames", type=int, default=61)
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--no-rgb565", action="store_true")
    ap.add_argument("--simulate-8bit", action="store_true")
    ap.add_argument("--palette-colors", type=int, default=256)
    ap.add_argument("--palette-scope", choices=["shared", "per-layer"], default="shared")
    ap.add_argument("--center-sample-with-yaw", action="store_true")
    ap.add_argument("--layout-delta", type=float, default=MAX_LAYOUT_DELTA)
    ap.add_argument("--center-draw-gain", type=float, default=CENTER_DRAW_GAIN)
    ap.add_argument("--seam-overlap", type=int, default=SEAM_OVERLAP)
    ap.add_argument("--outer-guard", type=int, default=0)
    ap.add_argument("--sync-motion", action="store_true")
    ap.add_argument("--sync-gamma", type=float, default=1.2)
    ap.add_argument("--sync-yaw-range", type=float, default=-1.0)
    ap.add_argument("--sync-center-draw", type=float, default=-1.0)
    ap.add_argument("--sync-side-delta", type=float, default=-1.0)
    ap.add_argument("--gun-image")
    ap.add_argument("--gun-scale", type=float, default=1.0)
    ap.add_argument("--gun-offset-x", type=int, default=0)
    ap.add_argument("--gun-offset-y", type=int, default=0)
    ap.add_argument("--gun-motion", choices=["none", "idle", "fire"], default="none")
    ap.add_argument("--knife-image")
    ap.add_argument("--knife-offset-x", type=int, default=0)
    ap.add_argument("--knife-offset-y", type=int, default=12)
    ap.add_argument("--weapon-swap", action="store_true")
    ap.add_argument("--weapon-sequence-dir")
    ap.add_argument("--weapon-sequence-blank-after", type=int, default=0)
    ap.add_argument("--weapon-sequence-offset-x", type=int, default=0)
    ap.add_argument("--weapon-sequence-offset-y", type=int, default=12)
    ap.add_argument("--crosshair", action="store_true")
    ap.add_argument("--crosshair-half", type=int, default=6)
    ap.add_argument("--robot-images", nargs=3)
    ap.add_argument("--robot-foot-x", type=int, default=120)
    ap.add_argument("--robot-foot-y", type=int, default=133)
    ap.add_argument("--robot-patrol-px", type=int, default=28)
    ap.add_argument("--robot-step-px", type=int, default=7)
    ap.add_argument("--robot-armor", type=float, default=1.0)
    ap.add_argument("--robot-health", type=float, default=1.0)
    ap.add_argument("--robot-damage-preview", action="store_true")
    ap.add_argument("--robot-crack-image")
    ap.add_argument("--robot-die-image")
    args = ap.parse_args()

    if args.preset == "mature-sync":
        args.sync_motion = True
        args.sync_gamma = 1.2
        args.sync_yaw_range = 28.0
        args.sync_center_draw = 28.0
        args.sync_side_delta = 28.0
        args.seam_overlap = 1
        args.palette_scope = "per-layer"
        args.simulate_8bit = True
        args.retreat_only = True
        args.retreat_start = 0.0
        args.retreat_end = 0.72
        args.fb_center_scale = 0.06
        args.fb_width_gamma = 1.20
        args.center_resize = False
        args.fb_side_min = 10
        args.fb_depth_px = 52
        args.fb_center_depth_ratio = 0.45
        args.fb_wall_depth_ratio = 0.45
        args.strafe_side_scale = 0.90
        args.strafe_left_parallax = 1.10
        args.strafe_center_parallax = 0.55
        args.strafe_right_parallax = 0.95
        args.strafe_width_delta = 4.0

    if not args.out and not args.gif:
        raise ValueError("Please provide --out and/or --gif")

    layers, info = load_layers(
        Path(args.input_dir),
        not args.no_rgb565,
        args.simulate_8bit,
        args.palette_colors,
        args.palette_scope,
    )

    print(f"Preview mode: {info['mode']}")
    if args.simulate_8bit:
        total_px = sum(v.width * v.height for v in layers.values())
        lut_count = 3 if args.palette_scope == "per-layer" else 1
        bytes_8bit = total_px + (info["palette_colors"] * 2 * lut_count)
        bytes_16bit = total_px * 2
        print(f"8-bit+LUT estimate: {bytes_8bit} bytes (vs RGB565 {bytes_16bit} bytes)")

    sync_yaw_range = args.sync_yaw_range if args.sync_yaw_range > 0 else args.yaw_range
    sync_center_draw = args.sync_center_draw if args.sync_center_draw >= 0 else args.yaw_range
    sync_side_delta = args.sync_side_delta if args.sync_side_delta >= 0 else args.yaw_range

    gun_img = None
    if args.gun_image:
        gun_img = Image.open(args.gun_image).convert("RGBA")
    knife_img = None
    if args.knife_image:
        knife_img = Image.open(args.knife_image).convert("RGBA")
    robot_frames: list[Image.Image] = []
    if args.robot_images:
        robot_frames = [Image.open(p).convert("RGBA") for p in args.robot_images]
    robot_crack_img = Image.open(args.robot_crack_image).convert("RGBA") if args.robot_crack_image else None
    robot_die_img = Image.open(args.robot_die_image).convert("RGBA") if args.robot_die_image else None
    weapon_sequence = load_weapon_sequence(args.weapon_sequence_dir, args.weapon_sequence_blank_after)

    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        y_max = info["src_h"] - SCREEN_H
        frame, layout = compose_frame(
            layers,
            args.yaw,
            args.y_offset,
            y_max,
            args.move_x,
            args.move_fb,
            args.retreat_only,
            args.fb_center_scale,
            args.fb_width_gamma,
            args.center_resize,
            args.fb_side_min,
            args.fb_depth_px,
            args.fb_center_depth_ratio,
            args.fb_wall_depth_ratio,
            args.center_sample_with_yaw,
            args.layout_delta,
            args.center_draw_gain,
            args.seam_overlap,
            args.outer_guard,
            args.sync_motion,
            sync_yaw_range,
            sync_center_draw,
            sync_side_delta,
            args.sync_gamma,
            None,
            None,
            None,
            args.strafe_side_scale,
            args.strafe_left_parallax,
            args.strafe_center_parallax,
            args.strafe_right_parallax,
            args.strafe_width_delta,
            False,
        )
        center_motion = "draw+sample" if args.center_sample_with_yaw else "draw-only"
        frame, _, _ = paste_robot_overlay(
            frame,
            robot_frames,
            robot_crack_img,
            robot_die_img,
            0,
            args.robot_foot_x,
            args.robot_foot_y,
            args.robot_patrol_px,
            0,
            1,
            args.robot_armor,
            args.robot_health,
            0.0,
            -1,
            False,
            False,
        )
        if weapon_sequence:
            seq_frame = weapon_sequence[0]
            if seq_frame is not None:
                frame = paste_gun_overlay(
                    frame,
                    seq_frame,
                    1.0,
                    args.weapon_sequence_offset_x,
                    args.weapon_sequence_offset_y,
                )
        elif args.weapon_swap:
            show_knife, kdx, kdy, show_gun, gdx, gdy = weapon_swap_offsets(0, 1)
            if show_knife:
                frame = paste_gun_overlay(frame, knife_img, 1.0, args.knife_offset_x + kdx, args.knife_offset_y + kdy)
            if show_gun:
                frame = paste_gun_overlay(frame, gun_img, args.gun_scale, args.gun_offset_x + gdx, args.gun_offset_y + gdy)
        else:
            gun_dx, gun_dy = gun_motion_offset(args.gun_motion, 0, 1, args.fps)
            frame = paste_gun_overlay(frame, gun_img, args.gun_scale, args.gun_offset_x + gun_dx, args.gun_offset_y + gun_dy)
        if args.crosshair:
            frame = draw_center_crosshair(frame, args.crosshair_half)
        frame = draw_overlay(frame, args.yaw, int(clamp(args.y_offset, 0, y_max)), not args.no_rgb565, layout, info["mode"], center_motion)
        frame.save(out)
        print(f"Saved preview PNG: {out}")

    if args.gif:
        gif = Path(args.gif)
        gif.parent.mkdir(parents=True, exist_ok=True)
        n = max(3, args.frames)
        if n % 2 == 0:
            n += 1
        imgs = []
        prev_move_fb = None
        center_phase = None
        wall_phase = None
        prev_move_x = None
        strafe_phase = None
        robot_foot_x = args.robot_foot_x
        robot_face_dir = 1
        for i in range(n):
            t = i / (n - 1)
            move_x = args.move_x
            move_fb = args.move_fb
            if args.anim == "pitch":
                yaw = args.yaw
                y_offset = args.y_offset - args.pitch_range + int(round(2.0 * args.pitch_range * t))
            elif args.anim == "both":
                yaw = -args.yaw_range + 2.0 * args.yaw_range * t
                y_offset = args.y_offset - args.pitch_range + int(round(2.0 * args.pitch_range * t))
            elif args.anim == "move":
                yaw = args.yaw
                y_offset = args.y_offset
                move_x = -args.move_range_x + 2.0 * args.move_range_x * t
                if args.retreat_only:
                    r0 = clamp(args.retreat_start, 0.0, 1.0)
                    r1 = clamp(args.retreat_end, 0.0, 1.0)
                    u = t * t * (3.0 - 2.0 * t)
                    move_fb = r0 + (r1 - r0) * u
                else:
                    fb_start = args.move_fb_start
                    fb_end = args.move_fb_end
                    if fb_start > 1.5 or fb_start < -1.5:
                        # Default: original image state is the far endpoint.
                        fb_start = 0.0
                    if fb_end > 1.5 or fb_end < -1.5:
                        # Move forward from far to near by default.
                        fb_end = args.move_range_fb
                    fb_start = clamp(fb_start, -1.0, 1.0)
                    fb_end = clamp(fb_end, -1.0, 1.0)
                    move_fb = fb_start + (fb_end - fb_start) * t
            elif args.anim == "strafe":
                yaw = args.yaw
                y_offset = args.y_offset
                u = t * t * (3.0 - 2.0 * t)
                move_x = -args.move_range_x + 2.0 * args.move_range_x * u
                move_fb = args.move_fb
            else:
                yaw = -args.yaw_range + 2.0 * args.yaw_range * t
                y_offset = args.y_offset

            y_max = info["src_h"] - SCREEN_H
            y_clamped = int(clamp(y_offset, 0, y_max))
            center_phase_arg = None
            wall_phase_arg = None
            strafe_phase_arg = None
            if args.anim == "move":
                depth_px = float(clamp(args.fb_depth_px, 0, 120))
                center_ratio = float(clamp(args.fb_center_depth_ratio, 0.0, 1.0))
                wall_ratio = float(clamp(args.fb_wall_depth_ratio, 0.0, 1.0))
                if prev_move_fb is None:
                    center_phase = float(clamp(y_clamped + move_fb * depth_px * center_ratio, 0.0, float(y_max)))
                    wall_phase = float(clamp(y_clamped + move_fb * depth_px * wall_ratio, 0.0, float(y_max)))
                else:
                    delta_fb = move_fb - prev_move_fb
                    v_common = delta_fb * depth_px
                    center_phase = float(clamp(center_phase + v_common * center_ratio, 0.0, float(y_max)))
                    wall_phase = float(clamp(wall_phase + v_common * wall_ratio, 0.0, float(y_max)))
                prev_move_fb = move_fb
                center_phase_arg = center_phase
                wall_phase_arg = wall_phase
            if args.anim == "strafe":
                if prev_move_x is None:
                    strafe_phase = move_x
                else:
                    strafe_phase = strafe_phase + (move_x - prev_move_x)
                prev_move_x = move_x
                strafe_phase_arg = strafe_phase
            frame, layout = compose_frame(
                layers,
                yaw,
                y_clamped,
                y_max,
                move_x,
                move_fb,
                args.retreat_only,
                args.fb_center_scale,
                args.fb_width_gamma,
                args.center_resize,
                args.fb_side_min,
                args.fb_depth_px,
                args.fb_center_depth_ratio,
                args.fb_wall_depth_ratio,
                args.center_sample_with_yaw,
                args.layout_delta,
                args.center_draw_gain,
                args.seam_overlap,
                args.outer_guard,
                args.sync_motion,
                sync_yaw_range,
                sync_center_draw,
                sync_side_delta,
                args.sync_gamma,
                center_phase_arg,
                wall_phase_arg,
                strafe_phase_arg,
                args.strafe_side_scale,
                args.strafe_left_parallax,
                args.strafe_center_parallax,
                args.strafe_right_parallax,
                args.strafe_width_delta,
                args.anim == "strafe",
            )
            center_motion = "draw+sample" if args.center_sample_with_yaw else "draw-only"
            if args.robot_damage_preview:
                if t < 0.45:
                    armor_ratio = max(0.0, 1.0 - t / 0.45)
                    health_ratio = 1.0
                    chip_ratio = 0.0
                    chip_age = -1
                    crack_active = t > 0.39
                    die_active = False
                else:
                    armor_ratio = 0.0
                    health_progress = (t - 0.45) / 0.55
                    hit_index = int(health_progress * 8.0)
                    hit_phase = health_progress * 8.0 - hit_index
                    health_ratio = max(0.0, 1.0 - (hit_index + hit_phase) * 0.09)
                    chip_ratio = 0.09 if health_ratio > 0.0 else 0.0
                    chip_age = int(clamp(hit_phase * 8.0, 0.0, 8.0))
                    crack_active = health_progress < 0.10
                    die_active = health_ratio <= 0.02
            else:
                armor_ratio = args.robot_armor
                health_ratio = args.robot_health
                chip_ratio = 0.0
                chip_age = -1
                crack_active = False
                die_active = health_ratio <= 0.0

            frame, robot_foot_x, robot_face_dir = paste_robot_overlay(
                frame,
                robot_frames,
                robot_crack_img,
                robot_die_img,
                i,
                robot_foot_x,
                args.robot_foot_y,
                args.robot_patrol_px,
                args.robot_step_px,
                robot_face_dir,
                armor_ratio,
                health_ratio,
                chip_ratio,
                chip_age,
                crack_active,
                die_active,
            )
            if weapon_sequence:
                seq_done = i >= len(weapon_sequence)
                seq_frame = weapon_sequence[min(i, len(weapon_sequence) - 1)] if not seq_done else knife_img
                if seq_frame is not None:
                    seq_idle_y = 0
                    if seq_done:
                        idle_i = i - len(weapon_sequence)
                        seq_idle_y = int(round(math.sin(idle_i * math.tau / 24.0) * 2.0))
                    frame = paste_gun_overlay(
                        frame,
                        seq_frame,
                        1.0,
                        args.weapon_sequence_offset_x,
                        args.weapon_sequence_offset_y + seq_idle_y,
                    )
            elif args.weapon_swap:
                show_knife, kdx, kdy, show_gun, gdx, gdy = weapon_swap_offsets(i, n)
                if show_knife:
                    frame = paste_gun_overlay(frame, knife_img, 1.0, args.knife_offset_x + kdx, args.knife_offset_y + kdy)
                if show_gun:
                    frame = paste_gun_overlay(frame, gun_img, args.gun_scale, args.gun_offset_x + gdx, args.gun_offset_y + gdy)
            else:
                gun_dx, gun_dy = gun_motion_offset(args.gun_motion, i, n, args.fps)
                frame = paste_gun_overlay(frame, gun_img, args.gun_scale, args.gun_offset_x + gun_dx, args.gun_offset_y + gun_dy)
            if args.crosshair:
                frame = draw_center_crosshair(frame, args.crosshair_half)
            frame = draw_overlay(frame, yaw, y_clamped, not args.no_rgb565, layout, info["mode"], center_motion)
            imgs.append(frame)
        imgs[0].save(gif, save_all=True, append_images=imgs[1:], optimize=False, duration=int(1000/max(1,args.fps)), loop=0)
        print(f"Saved preview GIF: {gif}")


if __name__ == "__main__":
    main()
