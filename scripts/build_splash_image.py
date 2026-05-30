#!/usr/bin/env python3
"""Convert a full-screen artwork PNG into a 320x240 RGB565 header.

The output header exposes ``core::<namespace>::kImage[320*240]`` packed
as RGB565. Run from the repo root after dropping a source PNG into
photos/:

    ./scripts/build_splash_image.py photos/splashscreen_01.png
    ./scripts/build_splash_image.py \\
        "photos/press a note to mapping.png" \\
        --out core/mapping_prompt_image.h --namespace mapping_prompt

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
DEFAULT_OUT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "core", "splash_image.h",
)


def rgb_to_565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("png", help="source PNG (any size, resampled to 320x240)")
    parser.add_argument("--out",       default=DEFAULT_OUT,
                        help=f"output header path (default {DEFAULT_OUT!r})")
    parser.add_argument("--namespace", default="splash",
                        help="inner namespace under core:: (default 'splash')")
    args = parser.parse_args()

    src = Image.open(args.png).convert("RGB")
    print(f"loaded {args.png}: {src.size}")
    img = src.resize((W, H), Image.LANCZOS)

    pixels = [rgb_to_565(img.getpixel((x, y))) for y in range(H) for x in range(W)]

    with open(args.out, "w") as f:
        f.write("#pragma once\n\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"// {W}x{H} RGB565 artwork.\n")
        f.write(f"// Generated from a source PNG by scripts/build_splash_image.py\n")
        f.write("// (Pillow LANCZOS resample + (R5<<11)|(G6<<5)|B5 packing).\n")
        f.write("// 153,600 bytes; lives in flash on Teensy via constexpr.\n\n")
        f.write(f"namespace core::{args.namespace} {{\n\n")
        f.write(f"constexpr int kW = {W};\n")
        f.write(f"constexpr int kH = {H};\n\n")
        f.write("inline constexpr uint16_t kImage[kW * kH] = {\n")
        for i in range(0, len(pixels), 16):
            line = ", ".join(f"0x{v:04X}" for v in pixels[i:i+16])
            f.write(f"    {line},\n")
        f.write("};\n\n")
        f.write(f"}} // namespace core::{args.namespace}\n")

    print(f"wrote {args.out} ({os.path.getsize(args.out)/1024:.1f} KB)")


if __name__ == "__main__":
    main()
