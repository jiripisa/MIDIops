#pragma once

#include <cstdint>

#include "core/Display.h"

namespace core {

// 2x2 parameter-grid geometry (below the shell's 10px top bar).
constexpr int kParamGridTop = 12;
constexpr int kParamCellW   = 160;
constexpr int kParamCellH   = (240 - kParamGridTop) / 2;   // 114

// Cycle an enum value by `delta` steps (wraps around E::kCount).
template <typename E>
inline E cycleEnum(E current, int delta) {
    int n = static_cast<int>(E::kCount);
    int v = (static_cast<int>(current) + delta % n + n) % n;
    return static_cast<E>(v);
}

// Draws one grid cell: parameter name (small) above its value (large).
// col/row are 0..1 (col 0 = left, row 0 = top).
inline void drawParamCell(Display& d, int col, int row,
                          const char* name, const char* value) {
    const int x = col * kParamCellW;
    const int y = kParamGridTop + row * kParamCellH;
    constexpr int pad = 8;
    d.drawText(x + pad, y + pad,      name,  color::Gray,  color::Black, 1);
    d.drawText(x + pad, y + pad + 18, value, color::White, color::Black, 3);
}

inline void drawParamGridDividers(Display& d) {
    d.fillRect(kParamCellW, kParamGridTop, 1, 240 - kParamGridTop, color::DarkGray);
    d.fillRect(0, kParamGridTop + kParamCellH, 320, 1, color::DarkGray);
}

} // namespace core
