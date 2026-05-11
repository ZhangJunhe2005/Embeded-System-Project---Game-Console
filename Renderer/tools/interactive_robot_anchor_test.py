#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import tkinter as tk
from PIL import Image, ImageTk


SCREEN_W = 240
SCREEN_H = 240
VIEW_W = 240
VIEW_H = 135
VIEW_Y = 52
SCALE = 3

ROBOT_BASE_X = 240.0
ROBOT_BASE_Y = 220.0
ROBOT_X_STEP = 12.0
ROBOT_Y_STEP = -3.0
ROBOT_Y_TO_X = 2.0
ROBOT_SCALE_STEP_Y = 0.07


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def parse_coord(path: Path) -> tuple[int, int]:
    x_s, y_s = path.stem.split("_", 1)
    return int(x_s), int(y_s)


def load_backgrounds(bg_dir: Path) -> dict[tuple[int, int], Image.Image]:
    bgs: dict[tuple[int, int], Image.Image] = {}
    for path in bg_dir.glob("*.png"):
        bgs[parse_coord(path)] = Image.open(path).convert("RGBA")
    return bgs


class RobotAnchorApp:
    def __init__(self) -> None:
        self.bgs = load_backgrounds(Path("ad_background"))
        if not self.bgs:
            raise SystemExit("No backgrounds found in ad_background/")
        self.robot = Image.open("robot1.png").convert("RGBA")

        xs = [coord[0] for coord in self.bgs]
        ys = [coord[1] for coord in self.bgs]
        self.min_x = min(xs)
        self.max_x = max(xs)
        self.min_y = min(ys)
        self.max_y = max(ys)

        self.player_x = 0.0
        self.player_y = 0.0
        self.look_x = 0.0
        self.look_y = 0.0
        self.keys: set[str] = set()
        self.last_mouse: tuple[int, int] | None = None

        self.root = tk.Tk()
        self.root.title("Robot Ground Anchor Test - WASD move, mouse look")
        self.canvas = tk.Canvas(self.root, width=SCREEN_W * SCALE, height=SCREEN_H * SCALE, highlightthickness=0)
        self.canvas.pack()
        self.image_item = self.canvas.create_image(0, 0, anchor=tk.NW)

        self.root.bind("<KeyPress>", self.on_key_down)
        self.root.bind("<KeyRelease>", self.on_key_up)
        self.root.bind("<Motion>", self.on_mouse_move)
        self.root.bind("<Leave>", self.on_mouse_leave)
        self.root.bind("<Button-1>", self.on_mouse_leave)
        self.root.bind("<Escape>", lambda _e: self.root.destroy())

        self.tk_image: ImageTk.PhotoImage | None = None
        self.tick()

    def on_key_down(self, event: tk.Event) -> None:
        self.keys.add(event.keysym.lower())

    def on_key_up(self, event: tk.Event) -> None:
        self.keys.discard(event.keysym.lower())

    def on_mouse_leave(self, _event: tk.Event) -> None:
        self.last_mouse = None

    def on_mouse_move(self, event: tk.Event) -> None:
        if self.last_mouse is None:
            self.last_mouse = (event.x, event.y)
            return
        dx = event.x - self.last_mouse[0]
        dy = event.y - self.last_mouse[1]
        self.last_mouse = (event.x, event.y)
        self.look_x += dx / SCALE
        self.look_y += dy / SCALE

    def update_controls(self) -> None:
        speed = 0.12
        if "shift_l" in self.keys or "shift_r" in self.keys:
            speed = 0.24
        if "a" in self.keys:
            self.player_x -= speed
        if "d" in self.keys:
            self.player_x += speed
        if "w" in self.keys:
            self.player_y += speed
        if "s" in self.keys:
            self.player_y -= speed
        self.player_x = clamp(self.player_x, self.min_x, self.max_x)
        self.player_y = clamp(self.player_y, self.min_y, self.max_y)

    def nearest_background(self) -> tuple[tuple[int, int], Image.Image]:
        gx = round(self.player_x)
        gy = round(self.player_y)
        gx = int(clamp(gx, self.min_x, self.max_x))
        gy = int(clamp(gy, self.min_y, self.max_y))
        if (gx, gy) in self.bgs:
            return (gx, gy), self.bgs[(gx, gy)]
        best = min(self.bgs, key=lambda c: abs(c[0] - gx) + abs(c[1] - gy))
        return best, self.bgs[best]

    def render(self) -> Image.Image:
        coord, bg = self.nearest_background()
        max_crop_x = bg.width - VIEW_W
        max_crop_y = bg.height - VIEW_H
        center_crop_x = max_crop_x / 2.0
        center_crop_y = max_crop_y / 2.0
        crop_x = round(clamp(center_crop_x + self.look_x, 0, max_crop_x))
        crop_y = round(clamp(center_crop_y + self.look_y, 0, max_crop_y))

        view = bg.crop((crop_x, crop_y, crop_x + VIEW_W, crop_y + VIEW_H)).convert("RGBA")

        robot_bg_x = ROBOT_BASE_X - self.player_x * ROBOT_X_STEP + self.player_y * ROBOT_Y_TO_X
        robot_bg_y = ROBOT_BASE_Y - self.player_y * ROBOT_Y_STEP
        robot_scale = max(0.25, 1.0 + self.player_y * ROBOT_SCALE_STEP_Y)
        rw = max(1, round(self.robot.width * robot_scale))
        rh = max(1, round(self.robot.height * robot_scale))
        robot_draw = self.robot.resize((rw, rh), Image.Resampling.NEAREST)
        robot_x = round(robot_bg_x - crop_x - robot_draw.width / 2.0)
        robot_y = round(robot_bg_y - crop_y - robot_draw.height)
        if robot_x < VIEW_W and robot_y < VIEW_H and robot_x + rw > 0 and robot_y + rh > 0:
            view.alpha_composite(robot_draw, (robot_x, robot_y))

        frame = Image.new("RGB", (SCREEN_W, SCREEN_H), (4, 5, 6))
        frame.paste(view.convert("RGB"), (0, VIEW_Y))
        return frame.resize((SCREEN_W * SCALE, SCREEN_H * SCALE), Image.Resampling.NEAREST)

    def tick(self) -> None:
        self.update_controls()
        frame = self.render()
        self.tk_image = ImageTk.PhotoImage(frame)
        self.canvas.itemconfig(self.image_item, image=self.tk_image)
        self.root.after(33, self.tick)

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    RobotAnchorApp().run()
