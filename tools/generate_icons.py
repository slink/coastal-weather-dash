#!/usr/bin/env python3
"""Generate 16 weather icon PNGs for a 3-color e-paper display.

Produces 8 icon types in two sizes (120x120 and 40x40).
Colors: white (255,255,255) background, black (0,0,0) outlines, red (255,0,0) accents.
Designed for sharp rendering on e-paper — minimal anti-aliasing.
"""

import math
import os
from pathlib import Path

from PIL import Image, ImageDraw

# ── Palette ──────────────────────────────────────────────────────────────────
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)

ICON_DIR = Path(__file__).parent / "icons"


# ── Helper: scale-aware drawing ──────────────────────────────────────────────
def _lw(size: int, base: int = 3) -> int:
    """Line width that scales with icon size."""
    return max(1, round(base * size / 120))


def _margin(size: int) -> int:
    """Pixel margin from edges."""
    return max(2, round(5 * size / 120))


# ── Cloud shape (reused by many icons) ───────────────────────────────────────
def draw_cloud(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    w: float,
    h: float,
    outline=BLACK,
    fill=WHITE,
    lw: int = 3,
):
    """Draw a stylised cloud centred at (cx, cy) with bounding size w x h.

    The cloud is built from overlapping ellipses with a flat bottom rectangle.
    """
    # Flat-bottom rectangle
    rect_h = h * 0.35
    rect_top = cy + h * 0.05
    rect_bot = cy + h * 0.40
    rect_left = cx - w * 0.45
    rect_right = cx + w * 0.45

    # Draw bumps (three overlapping ellipses along the top)
    bumps = [
        # (centre_x_offset, centre_y_offset, rx, ry)
        (-0.20, -0.10, 0.28, 0.30),  # left bump
        (0.05, -0.25, 0.32, 0.35),  # centre bump (tallest)
        (0.28, -0.05, 0.24, 0.25),  # right bump
    ]

    # First pass: fill all bumps in white to create solid interior
    for bx, by, brx, bry in bumps:
        ex = cx + w * bx
        ey = cy + h * by
        erx = w * brx
        ery = h * bry
        draw.ellipse(
            [ex - erx, ey - ery, ex + erx, ey + ery],
            fill=fill,
            outline=fill,
        )

    # Fill the rectangle body
    draw.rectangle([rect_left, rect_top, rect_right, rect_bot], fill=fill, outline=fill)

    # Second pass: draw outlines
    for bx, by, brx, bry in bumps:
        ex = cx + w * bx
        ey = cy + h * by
        erx = w * brx
        ery = h * bry
        draw.ellipse(
            [ex - erx, ey - ery, ex + erx, ey + ery],
            fill=None,
            outline=outline,
            width=lw,
        )

    # Redraw rectangle body fill to cover interior ellipse outlines, then bottom line
    inner_pad = lw
    draw.rectangle(
        [rect_left + inner_pad, rect_top, rect_right - inner_pad, rect_bot - lw],
        fill=fill,
        outline=fill,
    )
    # Bottom edge line
    draw.line(
        [rect_left, rect_bot, rect_right, rect_bot],
        fill=outline,
        width=lw,
    )
    # Left edge line (short)
    draw.line(
        [rect_left, rect_top + h * 0.05, rect_left, rect_bot],
        fill=outline,
        width=lw,
    )
    # Right edge line (short)
    draw.line(
        [rect_right, rect_top + h * 0.05, rect_right, rect_bot],
        fill=outline,
        width=lw,
    )


# ── Sun shape ────────────────────────────────────────────────────────────────
def draw_sun(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    radius: float,
    color=RED,
    lw: int = 3,
    num_rays: int = 8,
    ray_inner: float = 1.35,
    ray_outer: float = 1.75,
):
    """Draw a sun: circle outline with radiating rays."""
    # Circle
    draw.ellipse(
        [cx - radius, cy - radius, cx + radius, cy + radius],
        fill=None,
        outline=color,
        width=lw,
    )
    # Rays
    for i in range(num_rays):
        angle = 2 * math.pi * i / num_rays
        x1 = cx + math.cos(angle) * radius * ray_inner
        y1 = cy + math.sin(angle) * radius * ray_inner
        x2 = cx + math.cos(angle) * radius * ray_outer
        y2 = cy + math.sin(angle) * radius * ray_outer
        draw.line([x1, y1, x2, y2], fill=color, width=lw)


# ── Lightning bolt ───────────────────────────────────────────────────────────
def draw_lightning(
    draw: ImageDraw.ImageDraw,
    cx: float,
    top_y: float,
    bot_y: float,
    width: float,
    color=RED,
    lw: int = 2,
):
    """Draw a zigzag lightning bolt."""
    h = bot_y - top_y
    points = [
        (cx + width * 0.1, top_y),
        (cx - width * 0.15, top_y + h * 0.45),
        (cx + width * 0.10, top_y + h * 0.40),
        (cx - width * 0.05, bot_y),
        (cx + width * 0.15, top_y + h * 0.55),
        (cx - width * 0.10, top_y + h * 0.60),
    ]
    draw.polygon(points, fill=color, outline=color)


# ── Icon generators ─────────────────────────────────────────────────────────
def make_clear(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)
    m = _margin(size)
    cx, cy = size / 2, size / 2
    radius = (size / 2 - m) * 0.38
    draw_sun(draw, cx, cy, radius, color=RED, lw=lw)
    return img


def make_partly_cloudy(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)
    m = _margin(size)

    # Sun behind and to the upper-right
    sun_cx = size * 0.62
    sun_cy = size * 0.30
    sun_r = size * 0.16
    draw_sun(draw, sun_cx, sun_cy, sun_r, color=RED, lw=lw, ray_outer=1.8)

    # Cloud in front, lower-left
    cloud_cx = size * 0.45
    cloud_cy = size * 0.58
    cloud_w = size * 0.72
    cloud_h = size * 0.48
    draw_cloud(draw, cloud_cx, cloud_cy, cloud_w, cloud_h, outline=BLACK, fill=WHITE, lw=lw)
    return img


def make_cloudy(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)

    # Back cloud (slightly up and right)
    draw_cloud(draw, size * 0.55, size * 0.38, size * 0.60, size * 0.42, lw=lw)
    # Front cloud (slightly down and left)
    draw_cloud(draw, size * 0.42, size * 0.55, size * 0.70, size * 0.48, lw=lw)
    return img


def make_fog(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size, base=3)
    m = _margin(size)

    # Three horizontal lines with slight waviness
    x_left = m + size * 0.10
    x_right = size - m - size * 0.10
    gap = size * 0.18

    for i, y_base in enumerate([
        size * 0.32,
        size * 0.50,
        size * 0.68,
    ]):
        # Alternate slight offsets for visual interest
        offset = size * 0.03 if i % 2 == 0 else -size * 0.03
        # Draw as a series of short segments to create slight wave
        segments = 12
        seg_w = (x_right - x_left) / segments
        pts = []
        for s in range(segments + 1):
            x = x_left + s * seg_w
            y = y_base + math.sin(s * math.pi / (segments / 2)) * size * 0.02
            pts.append((x, y))
        for s in range(len(pts) - 1):
            draw.line([pts[s], pts[s + 1]], fill=BLACK, width=lw)

    return img


def make_drizzle(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)

    # Cloud in upper portion
    cloud_cy = size * 0.38
    draw_cloud(draw, size * 0.50, cloud_cy, size * 0.70, size * 0.42, lw=lw)

    # Small dots below (drizzle)
    dot_r = max(1, round(size * 0.025))
    dot_y_start = size * 0.66
    dot_spacing_x = size * 0.15
    dot_spacing_y = size * 0.10

    positions = [
        (size * 0.32, dot_y_start),
        (size * 0.50, dot_y_start),
        (size * 0.68, dot_y_start),
        (size * 0.40, dot_y_start + dot_spacing_y),
        (size * 0.58, dot_y_start + dot_spacing_y),
    ]
    for px, py in positions:
        draw.ellipse([px - dot_r, py - dot_r, px + dot_r, py + dot_r], fill=BLACK)

    return img


def make_rain(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)

    # Cloud
    cloud_cy = size * 0.35
    draw_cloud(draw, size * 0.50, cloud_cy, size * 0.70, size * 0.40, lw=lw)

    # Rain lines (angled slightly)
    rain_lw = max(1, _lw(size, base=2))
    line_len = size * 0.12
    y_start = size * 0.62
    y_gap = size * 0.12
    x_positions = [size * 0.30, size * 0.45, size * 0.60, size * 0.75]

    for row in range(2):
        for j, x in enumerate(x_positions):
            if row == 1 and j == 3:
                continue  # skip last dot in second row
            y = y_start + row * y_gap
            # Slight diagonal
            draw.line(
                [x, y, x - size * 0.03, y + line_len],
                fill=BLACK,
                width=rain_lw,
            )

    return img


def make_snow(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)

    # Cloud
    cloud_cy = size * 0.35
    draw_cloud(draw, size * 0.50, cloud_cy, size * 0.70, size * 0.40, lw=lw)

    # Snowflakes as small asterisks / star shapes
    flake_r = max(2, round(size * 0.04))
    flake_lw = max(1, _lw(size, base=1.5))

    positions = [
        (size * 0.30, size * 0.66),
        (size * 0.50, size * 0.64),
        (size * 0.70, size * 0.66),
        (size * 0.38, size * 0.78),
        (size * 0.58, size * 0.80),
    ]

    for fx, fy in positions:
        # Draw 3 crossing lines (6 arms)
        for angle_deg in [0, 60, 120]:
            a = math.radians(angle_deg)
            dx = math.cos(a) * flake_r
            dy = math.sin(a) * flake_r
            draw.line([fx - dx, fy - dy, fx + dx, fy + dy], fill=BLACK, width=flake_lw)

    return img


def make_thunderstorm(size: int) -> Image.Image:
    img = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(img)
    lw = _lw(size)

    # Cloud
    cloud_cy = size * 0.32
    draw_cloud(draw, size * 0.50, cloud_cy, size * 0.74, size * 0.42, lw=lw)

    # Lightning bolt in RED
    bolt_w = size * 0.30
    bolt_top = size * 0.55
    bolt_bot = size * 0.90
    draw_lightning(draw, size * 0.48, bolt_top, bolt_bot, bolt_w, color=RED, lw=lw)

    return img


# ── Main ─────────────────────────────────────────────────────────────────────
ICONS = {
    "clear": make_clear,
    "partly_cloudy": make_partly_cloudy,
    "cloudy": make_cloudy,
    "fog": make_fog,
    "drizzle": make_drizzle,
    "rain": make_rain,
    "snow": make_snow,
    "thunderstorm": make_thunderstorm,
}

SIZES = [120, 40]


def main():
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    generated = []

    for name, gen_fn in ICONS.items():
        for sz in SIZES:
            img = gen_fn(sz)
            fname = f"{name}_{sz}x{sz}.png"
            path = ICON_DIR / fname
            img.save(path)
            generated.append(str(path))
            print(f"  created {fname}")

    print(f"\n  {len(generated)} icons written to {ICON_DIR}/")


if __name__ == "__main__":
    main()
