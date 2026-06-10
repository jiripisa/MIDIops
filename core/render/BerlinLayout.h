#pragma once

#include "core/BerlinSequence.h"
#include "core/Display.h"

namespace core {

// Top parameter strip: one row of four cells (Enc1..4), below the 10px bar.
constexpr int kBerlinParamTop = 12;
constexpr int kBerlinParamH   = 78;
constexpr int kBerlinCellW    = 80;                                 // 320 / 4
constexpr int kBerlinRollTop  = kBerlinParamTop + kBerlinParamH;    // 90
constexpr int kBerlinRollH    = 240 - kBerlinRollTop;               // 150

inline void drawBerlinParamCell(Display& d, int col, const char* name, const char* value) {
    const int x = col * kBerlinCellW;
    d.drawText(x + 4, kBerlinParamTop + 6,  name,  color::Gray,  color::Black, 1);
    d.drawText(x + 4, kBerlinParamTop + 24, value, color::White, color::Black, 2);
}

inline void drawBerlinParamDividers(Display& d) {
    for (int c = 1; c < 4; ++c)
        d.fillRect(c * kBerlinCellW, kBerlinParamTop, 1, kBerlinParamH, color::DarkGray);
    d.fillRect(0, kBerlinRollTop - 1, 320, 1, color::DarkGray);
}

// Piano-roll of the current sequence with a playhead. X = step, Y = pitch.
inline void drawBerlinPianoRoll(Display& d, const BerlinSequence& seq, int playhead,
                                uint16_t noteColor) {
    d.fillRect(0, kBerlinRollTop, 320, kBerlinRollH, color::Black);
    const int n = seq.length() < 1 ? 1 : seq.length();
    const int colW = 320 / n;

    int lo = 127, hi = 0;
    for (int i = 0; i < n; ++i) {
        if (seq.step(i).active) {
            const int p = seq.step(i).note;
            if (p < lo) lo = p;
            if (p > hi) hi = p;
        }
    }
    if (lo > hi) { lo = 60; hi = 72; }
    if (hi - lo < 11) { hi = lo + 11; }                 // min 1-octave span
    const int rollBot = kBerlinRollTop + kBerlinRollH - 2;
    const int rollH   = kBerlinRollH - 4;

    if (playhead >= 0 && playhead < n)
        d.fillRect(playhead * colW, kBerlinRollTop, colW, kBerlinRollH, color::DarkGray);

    for (int i = 0; i < n; ++i) {
        const BerlinStep& s = seq.step(i);
        if (!s.active) continue;
        const int y = rollBot - (s.note - lo) * rollH / (hi - lo);
        int w = colW * s.gateTicks / 12;                // gate fraction of an 8th (12 ticks)
        if (w < 3) w = 3;
        if (w > colW - 1) w = colW - 1;
        const uint16_t c = s.accent ? color::White : noteColor;
        d.fillRect(i * colW + 1, y - 2, w, 5, c);
    }
}

} // namespace core
