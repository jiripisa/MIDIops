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

// Vertical piano keyboard width on the left of the roll.
constexpr int kBerlinKbW = 26;

inline bool berlinIsBlackKey(int note) {
    switch (((note % 12) + 12) % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

// Octave-snapped pitch range for the roll/keyboard: whole octaves (C..B),
// minimum 2 octaves, expanded to contain all active notes. Inclusive [lo, hi].
inline void berlinRollRange(const BerlinSequence& seq, int& lo, int& hi) {
    int mn = 127;
    int mx = 0;
    bool any = false;
    for (int i = 0; i < seq.length(); ++i) {
        if (!seq.step(i).active) continue;
        const int n = seq.step(i).note;
        if (n < mn) mn = n;
        if (n > mx) mx = n;
        any = true;
    }
    if (!any) { mn = 48; mx = 72; }                 // default C3..C5
    lo = mn - (mn % 12);                            // snap down to C
    hi = mx + (11 - (mx % 12));                     // snap up to B
    while (hi - lo < 23) {                          // ensure >= 2 octaves
        hi += 12;
        if (hi - lo < 23) lo -= 12;
    }
    if (lo < 0)   lo = 0;
    if (hi > 127) hi = 127;
}

// Piano-roll with a left keyboard. X = step, Y = pitch. The keyboard shows the
// pitch axis; used keys are tinted, the sounding note's key is highlighted;
// faint row lines give every semitone its own lane.
inline void drawBerlinPianoRoll(Display& d, const BerlinSequence& seq, int playhead,
                                int soundingNote, uint16_t noteColor) {
    d.fillRect(0, kBerlinRollTop, 320, kBerlinRollH, color::Black);

    int lo = 0;
    int hi = 0;
    berlinRollRange(seq, lo, hi);
    const int nSemis = (hi - lo + 1) < 1 ? 1 : (hi - lo + 1);
    const int rollTop = kBerlinRollTop;
    const int rollH   = kBerlinRollH;

    // Top y of note n's lane (higher note = smaller y); lane n spans [yTop(n), yTop(n-1)).
    auto yTop = [&](int n) { return rollTop + (hi - n) * rollH / nSemis; };
    auto noteUsed = [&](int n) {
        for (int i = 0; i < seq.length(); ++i)
            if (seq.step(i).active && seq.step(i).note == n) return true;
        return false;
    };

    const int kbW   = kBerlinKbW;
    const int rollX = kbW;
    const int rollW = 320 - kbW;
    const int n     = seq.length() < 1 ? 1 : seq.length();
    const int colW  = rollW / n;

    const uint16_t cWhite = color::LightGray;
    const uint16_t cBlack = rgb565(40, 40, 40);
    const uint16_t cUsedW = rgb565(40, 110, 40);
    const uint16_t cUsedB = rgb565(20, 70, 20);
    const uint16_t cPlay  = rgb565(120, 255, 120);
    const uint16_t cLine  = rgb565(30, 30, 30);

    // 1) Keyboard + faint per-semitone lane lines.
    for (int nt = lo; nt <= hi; ++nt) {
        const int y0 = yTop(nt);
        const int y1 = yTop(nt - 1);
        int h = y1 - y0;
        if (h < 1) h = 1;
        const bool black = berlinIsBlackKey(nt);
        if (soundingNote == nt) {
            d.fillRect(0, y0, kbW, h, cPlay);                     // sounding key, full width
        } else if (black) {
            d.fillRect(0, y0, kbW, h, cWhite);                   // white base
            d.fillRect(0, y0, kbW * 6 / 10, h, noteUsed(nt) ? cUsedB : cBlack);
        } else {
            d.fillRect(0, y0, kbW, h, noteUsed(nt) ? cUsedW : cWhite);
        }
        d.fillRect(rollX, y0, rollW, 1, cLine);                  // lane line across the roll
    }
    d.fillRect(kbW - 1, rollTop, 1, rollH, color::DarkGray);     // keyboard divider

    // 2) Playhead column (behind blocks).
    if (playhead >= 0 && playhead < n)
        d.fillRect(rollX + playhead * colW, rollTop, colW, rollH, color::DarkGray);

    // 3) Note blocks — aligned to keyboard lanes.
    for (int i = 0; i < n; ++i) {
        const BerlinStep& s = seq.step(i);
        if (!s.active) continue;
        if (s.note < lo || s.note > hi) continue;
        const int y0 = yTop(s.note);
        const int y1 = yTop(s.note - 1);
        int bh = y1 - y0 - 1;
        if (bh < 2) bh = 2;
        int w = colW * s.gateTicks / 12;
        if (w < 3) w = 3;
        if (w > colW - 1) w = colW - 1;
        const uint16_t c = s.accent ? color::White : noteColor;
        d.fillRect(rollX + i * colW + 1, y0, w, bh, c);
    }
}

} // namespace core
