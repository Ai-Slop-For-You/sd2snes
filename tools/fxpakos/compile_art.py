#!/usr/bin/env python3
"""Compile cover art into SNES-native assets for FXPAK OS.

Outputs:
  <prefix>.4bpp       SNES 4bpp planar tiles, row-major tile order
  <prefix>.pal        16 little-endian SNES BGR555 palette entries
  <prefix>.map        little-endian SNES tilemap words
  <prefix>.json       build metadata
  <prefix>.preview.png quantized preview

The first FXPAK OS library layout uses 80x112 covers (10x14 tiles), but any
positive width/height divisible by 8 is accepted.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Iterable, Sequence

try:
    from PIL import Image, ImageOps
except ImportError as exc:  # pragma: no cover - user-facing import guard
    raise SystemExit(
        "Pillow is required. Install it with: python3 -m pip install Pillow"
    ) from exc


DEFAULT_WIDTH = 80
DEFAULT_HEIGHT = 112
DEFAULT_COLORS = 15


def snes_color_word(rgb: Sequence[int]) -> int:
    """Convert 8-bit RGB to the SNES 15-bit color word (rrrrr low bits)."""
    r, g, b = (int(v) for v in rgb[:3])
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def fit_image(image: Image.Image, width: int, height: int, background: tuple[int, int, int]) -> Image.Image:
    """Contain source art within the requested canvas while preserving aspect."""
    rgb = image.convert("RGBA")
    bg = Image.new("RGBA", rgb.size, (*background, 255))
    rgb = Image.alpha_composite(bg, rgb).convert("RGB")
    return ImageOps.pad(
        rgb,
        (width, height),
        method=Image.Resampling.LANCZOS,
        color=background,
        centering=(0.5, 0.5),
    )


def quantize_image(image: Image.Image, colors: int, dither: bool) -> Image.Image:
    """Reserve transparent index 0; opaque SNES BG/OBJ art uses indices 1..15."""
    if not 1 <= colors <= 15:
        raise ValueError("Opaque SNES 4bpp art supports 1..15 colors (index 0 is transparent)")
    quantized = image.quantize(
        colors=colors,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE,
    )

    indexed = Image.new("P", image.size)
    indexed.putdata([value + 1 for value in quantized.tobytes()])
    palette = [0, 0, 0] + (quantized.getpalette() or [])[:45]
    indexed.putpalette(palette + [0] * (768 - len(palette)))
    return indexed


def palette_rgb(image: Image.Image, entries: int = 16) -> list[tuple[int, int, int]]:
    raw = image.getpalette() or []
    out: list[tuple[int, int, int]] = []
    for index in range(entries):
        base = index * 3
        if base + 2 < len(raw):
            out.append((raw[base], raw[base + 1], raw[base + 2]))
        else:
            out.append((0, 0, 0))
    return out


def encode_palette(image: Image.Image) -> bytes:
    payload = bytearray()
    for rgb in palette_rgb(image, 16):
        payload += struct.pack("<H", snes_color_word(rgb))
    return bytes(payload)


def encode_tile_4bpp(pixels: Sequence[int]) -> bytes:
    """Encode one 8x8 indexed tile in SNES 4bpp planar format.

    SNES tile order is two bytes per row for planes 0/1, followed by two bytes
    per row for planes 2/3. Within each byte, the left-most pixel is bit 7.
    """
    if len(pixels) != 64:
        raise ValueError("tile must contain exactly 64 pixels")

    low = bytearray()
    high = bytearray()

    for y in range(8):
        p0 = p1 = p2 = p3 = 0
        for x in range(8):
            value = int(pixels[y * 8 + x]) & 0x0F
            bit = 7 - x
            p0 |= ((value >> 0) & 1) << bit
            p1 |= ((value >> 1) & 1) << bit
            p2 |= ((value >> 2) & 1) << bit
            p3 |= ((value >> 3) & 1) << bit
        low.extend((p0, p1))
        high.extend((p2, p3))

    return bytes(low + high)


def iter_tiles(image: Image.Image) -> Iterable[list[int]]:
    width, height = image.size
    px = image.load()
    for ty in range(0, height, 8):
        for tx in range(0, width, 8):
            tile: list[int] = []
            for y in range(8):
                for x in range(8):
                    tile.append(int(px[tx + x, ty + y]))
            yield tile


def encode_tiles(image: Image.Image) -> bytes:
    return b"".join(encode_tile_4bpp(tile) for tile in iter_tiles(image))


def encode_tilemap(width: int, height: int, tile_base: int, palette_number: int, priority: bool) -> bytes:
    if not 0 <= tile_base <= 0x3FF:
        raise ValueError("tile_base must fit the SNES 10-bit tile index")
    if not 0 <= palette_number <= 7:
        raise ValueError("palette_number must be in 0..7")

    tile_count = (width // 8) * (height // 8)
    if tile_base + tile_count > 0x400:
        raise ValueError("tile range exceeds the SNES 10-bit tile index space")

    payload = bytearray()
    for offset in range(tile_count):
        word = (tile_base + offset) & 0x03FF
        word |= (palette_number & 0x07) << 10
        if priority:
            word |= 1 << 13
        payload += struct.pack("<H", word)
    return bytes(payload)


def parse_rgb(value: str) -> tuple[int, int, int]:
    text = value.strip().lstrip("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError("background must be RRGGBB or #RRGGBB")
    try:
        return tuple(int(text[i : i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("background must be hexadecimal RGB") from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="source PNG/JPEG/etc.")
    parser.add_argument("output_prefix", type=Path, help="output path without extension")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--colors", type=int, default=DEFAULT_COLORS)
    parser.add_argument("--tile-base", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--palette-number", type=int, default=0)
    parser.add_argument("--priority", action="store_true")
    parser.add_argument("--dither", action="store_true", help="enable Floyd-Steinberg dithering")
    parser.add_argument("--background", type=parse_rgb, default=(8, 8, 16), metavar="RRGGBB")
    return parser


def validate_dimensions(width: int, height: int) -> None:
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    if width % 8 or height % 8:
        raise ValueError("width and height must both be divisible by 8")


def main() -> int:
    args = build_parser().parse_args()
    validate_dimensions(args.width, args.height)

    if not args.input.is_file():
        raise SystemExit(f"input file does not exist: {args.input}")

    args.output_prefix.parent.mkdir(parents=True, exist_ok=True)

    with Image.open(args.input) as source:
        fitted = fit_image(source, args.width, args.height, args.background)
    indexed = quantize_image(fitted, args.colors, args.dither)

    tiles = encode_tiles(indexed)
    palette = encode_palette(indexed)
    tilemap = encode_tilemap(
        args.width,
        args.height,
        args.tile_base,
        args.palette_number,
        args.priority,
    )

    tile_count = (args.width // 8) * (args.height // 8)
    expected_tile_bytes = tile_count * 32
    if len(tiles) != expected_tile_bytes:
        raise RuntimeError(
            f"internal error: expected {expected_tile_bytes} tile bytes, got {len(tiles)}"
        )

    prefix = args.output_prefix
    prefix.with_suffix(".4bpp").write_bytes(tiles)
    prefix.with_suffix(".pal").write_bytes(palette)
    prefix.with_suffix(".map").write_bytes(tilemap)
    # Preview the actual BGR555 values, not the higher precision source palette.
    preview = indexed.copy()
    preview.putpalette([((v >> 3) * 255 // 31) for rgb in palette_rgb(indexed) for v in rgb] + [0] * 720)
    preview.convert("RGB").save(prefix.with_suffix(".preview.png"))

    metadata = {
        "format": "fxpakos-art-v1",
        "transparent_index": 0,
        "source": args.input.name,
        "width": args.width,
        "height": args.height,
        "tiles_x": args.width // 8,
        "tiles_y": args.height // 8,
        "tile_count": tile_count,
        "tile_base": args.tile_base,
        "palette_number": args.palette_number,
        "priority": bool(args.priority),
        "colors_requested": args.colors,
        "dither": bool(args.dither),
        "background_rgb": list(args.background),
        "bytes": {
            "tiles_4bpp": len(tiles),
            "palette": len(palette),
            "tilemap": len(tilemap),
        },
    }
    prefix.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    print(
        f"FXPAK OS art: {args.width}x{args.height}, {tile_count} tiles, "
        f"{len(tiles)} bytes graphics + {len(palette)} palette + {len(tilemap)} map"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
