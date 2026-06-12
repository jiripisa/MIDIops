#pragma once

#include <cstdint>

#include "core/Display.h"

namespace core {

// 16x16 1-bit mode icons: 16 rows, bit 15 = leftmost pixel. Drawn through
// fillRect runs so no platform support is needed; `scale` multiplies each
// icon pixel into a scale x scale square, matching drawText's sizing, so an
// icon scales in step with the text it sits above.
inline void drawIcon16(Display& d, int x, int y, const uint16_t* rows,
                       int scale, uint16_t color) {
    if (!rows || scale <= 0) return;
    for (int r = 0; r < 16; ++r) {
        const uint16_t bits = rows[r];
        if (!bits) continue;
        for (int c = 0; c < 16;) {
            if (bits & (0x8000u >> c)) {
                int run = 1;
                while (c + run < 16 && (bits & (0x8000u >> (c + run)))) ++run;
                d.fillRect(x + c * scale, y + r * scale, run * scale, scale, color);
                c += run;
            } else {
                ++c;
            }
        }
    }
}

namespace icons {

// Oscilloscope: bordered screen with one wave period, small stand below.
constexpr uint16_t kMonitoring[16] = {
    0xFFFF, 0x8001, 0x8001, 0x8C01, 0x9205, 0x9105, 0xA109, 0x8089,
    0x8091, 0x8061, 0x8001, 0x8001, 0xFFFF, 0x03C0, 0x03C0, 0x0FF0,
};

// Three notes ascending left-to-right (an arpeggio).
constexpr uint16_t kArp[16] = {
    0x0002, 0x0002, 0x0002, 0x0002, 0x000E, 0x008E, 0x0080, 0x0080,
    0x0080, 0x0380, 0x2380, 0x2000, 0x2000, 0x2000, 0xE000, 0xE000,
};

// Step-sequencer bars of varying height on a baseline.
constexpr uint16_t kBerlin[16] = {
    0x0000, 0x0000, 0x0000, 0x000E, 0x000E, 0x0E0E, 0x0E0E, 0x0E0E,
    0x0E0E, 0xEE0E, 0xEE0E, 0xEEEE, 0xEEEE, 0xEEEE, 0xEEEE, 0xFFFF,
};

// Metronome: tapering body, pendulum swung left with a weight near the top.
constexpr uint16_t kBpm[16] = {
    0x0180, 0x0240, 0x1A40, 0x1C20, 0x0C20, 0x0C10, 0x0C10, 0x1408,
    0x1208, 0x2204, 0x2204, 0x4102, 0x4102, 0xFFFF, 0xFFFF, 0x0000,
};

// Three mixer sliders with knobs at different heights.
constexpr uint16_t kSettings[16] = {
    0x0000, 0x2084, 0x2084, 0x208E, 0x708E, 0x708E, 0x7084, 0x2084,
    0x2084, 0x2384, 0x2384, 0x2384, 0x2084, 0x2084, 0x2084, 0x0000,
};

// Bug: antennae, head, split wing case, three leg pairs.
constexpr uint16_t kDebug[16] = {
    0x0810, 0x0420, 0x03C0, 0x03C0, 0x07E0, 0x3E7C, 0x0660, 0x0660,
    0x3E7C, 0x0660, 0x0660, 0x3E7C, 0x0660, 0x07E0, 0x03C0, 0x0000,
};

} // namespace icons

} // namespace core
