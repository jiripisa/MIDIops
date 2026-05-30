#!/usr/bin/env python3
"""Convert a splash artwork PNG into core/splash_image.h.

The header exposes ``core::splash::kImage[320*240]`` packed as RGB565.
Run from the repo root after dropping a new splash file into photos/:

    ./scripts/build_splash_image.py photos/splashscreen_01.png

Requires Pillow (``pip install pillow``).
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("error: pillow not installed.  pip install pillow")

W, H = 320, 240
HEADER_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "core", "splash_image.h",
)


def rgb_to_565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("png", help="source PNG (any size, will be resampled to 320x240)")
    parser.add_argument("--out", default=HEADER_PATH,
                        help=f"output header path (default {HEADER_PATH!r})")
    args = parser.parse_args()

    src = Image.open(args.png).convert("RGB")
    print(f"loaded {args.png}: {src.size}")
    img = src.resize((W, H), Image.LANCZOS)

    pixels = [rgb_to_565(img.getpixel((x, y))) for y in range(H) for x in range(W)]

    with open(args.out, "w") as f:
        f.write("#pragma once\n\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"// {W}x{H} RGB565 splash image.\n")
        f.write(f"// Generated from a source PNG by scripts/build_splash_image.py\n")
        f.write("// (Pillow LANCZOS resample + (R5<<11)|(G6<<5)|B5 packing).\n")
        f.write("// 153,600 bytes; lives in flash on Teensy via constexpr.\n\n")
        f.write("namespace core::splash {\n\n")
        f.write(f"constexpr int kW = {W};\n")
        f.write(f"constexpr int kH = {H};\n\n")
        f.write("inline constexpr uint16_t kImage[kW * kH] = {\n")
        for i in range(0, len(pixels), 16):
            line = ", ".join(f"0x{v:04X}" for v in pixels[i:i+16])
            f.write(f"    {line},\n")
        f.write("};\n\n")
        f.write("} // namespace core::splash\n")

    print(f"wrote {args.out} ({os.path.getsize(args.out)/1024:.1f} KB)")


if __name__ == "__main__":
    main()
