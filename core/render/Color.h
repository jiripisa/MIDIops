#pragma once

#include <cstdint>

#include "core/Display.h"   // color:: constants + rgb565()

namespace core {

// Multiplies an RGB565 colour by `factor256/256`. Used for vertical worm
// gradients (256 = full color, 160 ≈ 63 %).
inline uint16_t scaleRgb565(uint16_t c, uint16_t factor256) {
    uint32_t r = (c >> 11) & 0x1F;
    uint32_t g = (c >>  5) & 0x3F;
    uint32_t b =  c        & 0x1F;
    r = (r * factor256) >> 8;
    g = (g * factor256) >> 8;
    b = (b * factor256) >> 8;
    if (r > 0x1F) r = 0x1F;
    if (g > 0x3F) g = 0x3F;
    if (b > 0x1F) b = 0x1F;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Returns the per-channel palette colour for channels 1..16.
// Channel 0 or >16 → color::White (OMNI / out-of-range sentinel).
inline uint16_t channelColor(uint8_t channel) {
    static constexpr uint16_t kChannelPalette[16] = {
        rgb565( 80, 255,  80),   // 1  green
        rgb565(255, 200,  60),   // 2  yellow
        rgb565( 80, 200, 255),   // 3  sky
        rgb565(255, 100, 200),   // 4  magenta
        rgb565(255, 140,  60),   // 5  orange
        rgb565(180, 100, 255),   // 6  violet
        rgb565( 80, 255, 200),   // 7  teal
        rgb565(255,  80,  80),   // 8  red
        rgb565(200, 255,  80),   // 9  lime
        rgb565( 80, 140, 255),   // 10 blue
        rgb565(255, 180, 220),   // 11 pink
        rgb565(180, 255, 180),   // 12 pale green
        rgb565(220, 220,  80),   // 13 olive
        rgb565(180, 180, 255),   // 14 lavender
        rgb565(255, 220, 140),   // 15 peach
        rgb565(140, 220, 200),   // 16 aqua
    };
    if (channel < 1 || channel > 16) return color::White;
    return kChannelPalette[channel - 1];
}

} // namespace core
