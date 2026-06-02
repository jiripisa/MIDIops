#pragma once

#include <cstdint>

#include "core/Display.h"

namespace core {

// Blits a 1-bit-per-pixel bitmap: each rows[r] holds `width` bits, MSB =
// leftmost pixel. Set bits are drawn as horizontal runs via fillRect.
inline void drawGlyph(Display& d, int x, int y,
                      const uint16_t* rows, int width, int height,
                      uint16_t color) {
    for (int row = 0; row < height; ++row) {
        const uint16_t bits = rows[row];
        int col = 0;
        while (col < width) {
            const uint16_t mask =
                static_cast<uint16_t>(1u << (width - 1 - col));
            if (bits & mask) {
                int runEnd = col + 1;
                while (runEnd < width) {
                    const uint16_t m2 =
                        static_cast<uint16_t>(1u << (width - 1 - runEnd));
                    if (!(bits & m2)) break;
                    ++runEnd;
                }
                d.fillRect(x + col, y + row, runEnd - col, 1, color);
                col = runEnd;
            } else {
                ++col;
            }
        }
    }
}

} // namespace core
