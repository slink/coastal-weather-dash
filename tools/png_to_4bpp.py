#!/usr/bin/env python3
"""
png_to_4bpp.py - Convert PNG images to 4bpp PROGMEM C arrays for ESP32 e-paper.

Reads all PNG files from an input directory and generates a single C header file
containing packed 4-bit-per-pixel image data suitable for a 7-color e-ink display.

Color mapping (7-color e-ink palette):
    BLACK = 0    RED = 4    WHITE = 1

Pixel classification thresholds:
    - Red channel > 150  AND  green < 100  AND  blue < 100  -->  RED  (4)
    - Average brightness < 128                                -->  BLACK (0)
    - Everything else                                         -->  WHITE (1)
    - Fully transparent pixels (alpha == 0) are treated as WHITE.

Nibble packing:
    Two pixels per byte, high nibble first.
    For a row of pixels [p0, p1, p2, p3, ...], the byte stream is:
        byte0 = (p0 << 4) | p1
        byte1 = (p2 << 4) | p3
        ...

Filename conventions:
    clear_120x120.png  -->  icon_clear_large   (120x120)
    clear_40x40.png    -->  icon_clear_small    (40x40)

Usage:
    python png_to_4bpp.py <input_dir> <output_header>

Example:
    python png_to_4bpp.py tools/icons/ firmware/weather_icons.h
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

from PIL import Image

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# 4bpp palette indices matching the 7-color e-ink controller
COLOR_BLACK: int = 0
COLOR_WHITE: int = 1
COLOR_RED: int = 4

# How many hex bytes to emit per line in the output header (matches ibis_logos.h)
BYTES_PER_LINE: int = 16

# Size-to-suffix mapping for human-friendly array names
SIZE_SUFFIX: dict[tuple[int, int], str] = {
    (120, 120): "large",
    (40, 40): "small",
}

# Regex to pull the base name and dimensions from a filename like "clear_120x120.png"
FILENAME_RE = re.compile(r"^(.+?)_(\d+)x(\d+)\.png$", re.IGNORECASE)


# ---------------------------------------------------------------------------
# Pixel classification
# ---------------------------------------------------------------------------

def classify_pixel(r: int, g: int, b: int, a: int) -> int:
    """Map an RGBA pixel to a 4bpp palette index.

    Order of tests matters -- red detection must come before the brightness
    check because dark reds would otherwise be classified as black.
    """
    # Fully transparent pixels are white (background)
    if a == 0:
        return COLOR_WHITE

    # Red detection: strong red channel, weak green and blue
    if r > 150 and g < 100 and b < 100:
        return COLOR_RED

    # Brightness gate: dark pixels are black
    brightness = (r + g + b) / 3.0
    if brightness < 128:
        return COLOR_BLACK

    # Default: white
    return COLOR_WHITE


# ---------------------------------------------------------------------------
# Image conversion
# ---------------------------------------------------------------------------

def image_to_4bpp(img: Image.Image) -> list[int]:
    """Convert a PIL Image to a list of nibble-packed bytes.

    Returns a flat list of uint8 values where each byte encodes two pixels
    (high nibble = left pixel, low nibble = right pixel).

    If the image width is odd the last pixel in each row gets its own byte
    with the low nibble set to WHITE (padding).
    """
    # Ensure we have RGBA so we can inspect alpha
    img = img.convert("RGBA")
    width, height = img.size
    pixels = list(img.getdata())

    packed: list[int] = []

    for y in range(height):
        row_start = y * width
        x = 0
        while x < width:
            hi = classify_pixel(*pixels[row_start + x])

            if x + 1 < width:
                lo = classify_pixel(*pixels[row_start + x + 1])
            else:
                # Odd-width image: pad the trailing nibble with white
                lo = COLOR_WHITE

            packed.append((hi << 4) | lo)
            x += 2

    return packed


# ---------------------------------------------------------------------------
# Name derivation
# ---------------------------------------------------------------------------

def derive_names(filename: str) -> tuple[str, int, int] | None:
    """Parse a filename and return (array_name, width, height) or None.

    Examples:
        "clear_120x120.png"   --> ("icon_clear_large", 120, 120)
        "rain_40x40.png"      --> ("icon_rain_small",  40,  40)
        "partly_cloudy_120x120.png" --> ("icon_partly_cloudy_large", 120, 120)
    """
    match = FILENAME_RE.match(filename)
    if match is None:
        return None

    base_name = match.group(1)       # e.g. "clear" or "partly_cloudy"
    w = int(match.group(2))
    h = int(match.group(3))

    suffix = SIZE_SUFFIX.get((w, h))
    if suffix is None:
        # Unknown size -- fall back to WxH so we never silently drop icons
        suffix = f"{w}x{h}"

    array_name = f"icon_{base_name}_{suffix}"
    return array_name, w, h


# ---------------------------------------------------------------------------
# C header generation
# ---------------------------------------------------------------------------

def format_byte_array(data: list[int]) -> str:
    """Format a list of uint8 values as indented, comma-separated hex literals.

    Produces 16 values per line with 2-space indent, matching ibis_logos.h.
    The last value has no trailing comma.
    """
    lines: list[str] = []
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i : i + BYTES_PER_LINE]
        hex_vals = [f"0x{b:02x}" for b in chunk]

        # Omit trailing comma on the very last value of the entire array
        if i + BYTES_PER_LINE >= len(data):
            lines.append("  " + ", ".join(hex_vals))
        else:
            lines.append("  " + ", ".join(hex_vals) + ",")

    return "\n".join(lines)


def generate_header(icons: list[tuple[str, int, int, list[int]]]) -> str:
    """Build the complete C header string.

    Parameters
    ----------
    icons : list of (array_name, width, height, packed_bytes)
    """
    parts: list[str] = []

    parts.append("// Auto-generated weather icon data for e-ink display")
    parts.append(
        "// 7-color e-ink palette: BLACK=0, WHITE=1, GREEN=2, "
        "BLUE=3, RED=4, YELLOW=5, ORANGE=6"
    )
    parts.append("")
    parts.append("#ifndef WEATHER_ICONS_H")
    parts.append("#define WEATHER_ICONS_H")
    parts.append("")
    parts.append("#include <Arduino.h>")

    for array_name, width, height, packed in icons:
        upper_name = array_name.upper()
        byte_count = len(packed)

        parts.append("")
        parts.append(
            f"// {array_name} - {width}x{height} pixels, 4bpp (2 pixels per byte)"
        )
        parts.append(f"const uint16_t {upper_name}_WIDTH = {width};")
        parts.append(f"const uint16_t {upper_name}_HEIGHT = {height};")
        parts.append(
            f"const uint8_t {array_name}[{byte_count}] PROGMEM = {{"
        )
        parts.append(format_byte_array(packed))
        parts.append("};")

    parts.append("")
    parts.append("#endif // WEATHER_ICONS_H")
    parts.append("")  # trailing newline

    return "\n".join(parts)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PNG icons to a 4bpp PROGMEM C header for e-ink displays."
    )
    parser.add_argument(
        "input_dir",
        type=Path,
        help="Directory containing PNG files (e.g. tools/icons/)",
    )
    parser.add_argument(
        "output_header",
        type=Path,
        help="Path for the generated C header (e.g. firmware/weather_icons.h)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)

    input_dir: Path = args.input_dir
    output_path: Path = args.output_header

    if not input_dir.is_dir():
        print(f"Error: '{input_dir}' is not a directory.", file=sys.stderr)
        sys.exit(1)

    # Discover and sort PNGs so output order is deterministic
    png_files = sorted(input_dir.glob("*.png"))
    if not png_files:
        print(f"Error: no PNG files found in '{input_dir}'.", file=sys.stderr)
        sys.exit(1)

    icons: list[tuple[str, int, int, list[int]]] = []

    for png_path in png_files:
        result = derive_names(png_path.name)
        if result is None:
            print(f"Warning: skipping '{png_path.name}' (unexpected filename format).", file=sys.stderr)
            continue

        array_name, expected_w, expected_h = result

        img = Image.open(png_path)
        actual_w, actual_h = img.size

        if (actual_w, actual_h) != (expected_w, expected_h):
            print(
                f"Warning: '{png_path.name}' is {actual_w}x{actual_h} but "
                f"filename says {expected_w}x{expected_h}. Using actual dimensions.",
                file=sys.stderr,
            )

        packed = image_to_4bpp(img)
        icons.append((array_name, actual_w, actual_h, packed))

        print(
            f"  {array_name}: {actual_w}x{actual_h} -> {len(packed)} bytes",
            file=sys.stderr,
        )

    if not icons:
        print("Error: no valid icons were processed.", file=sys.stderr)
        sys.exit(1)

    # Ensure the output directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)

    header = generate_header(icons)
    output_path.write_text(header)

    print(
        f"Wrote {len(icons)} icon(s) to '{output_path}' "
        f"({os.path.getsize(output_path)} bytes).",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
