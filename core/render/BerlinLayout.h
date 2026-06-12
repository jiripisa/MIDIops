#pragma once

#include "core/BerlinSequence.h"
#include "core/Display.h"
#include "core/render/KeyLayout.h"
#include "core/render/ParamGrid.h"

namespace core {

// Top parameter strip: one row of four cells (Enc1..4), below the 10px bar.
constexpr int kBerlinParamTop = 12;
constexpr int kBerlinParamH   = 78;
constexpr int kBerlinCellW    = 80;                                 // 320 / 4
constexpr int kBerlinRollTop  = kBerlinParamTop + kBerlinParamH;    // 90
constexpr int kBerlinRollH    = 240 - kBerlinRollTop;               // 150

// `dim` renders the cell greyed-out — used for parameters the currently
// selected algorithm/behavior ignores, so the user sees at a glance that the
// knob has no effect right now.
inline void drawBerlinParamCell(Display& d, int col, const char* name, const char* value,
                                bool dim = false) {
    const int x = col * kBerlinCellW;
    drawParamCellAt(d, x, kBerlinParamTop, name, value, /*pad=*/4, /*nameDy=*/6,
                    /*valueDy=*/24, /*valueSize=*/2,
                    dim ? color::DarkGray : color::Gray,
                    dim ? color::DarkGray : color::White);
}

inline void drawBerlinParamDividers(Display& d) {
    for (int c = 1; c < 4; ++c)
        d.fillRect(c * kBerlinCellW, kBerlinParamTop, 1, kBerlinParamH, color::DarkGray);
    d.fillRect(0, kBerlinRollTop - 1, 320, 1, color::DarkGray);
}

// Scale an RGB565 colour's brightness by t/255 (component-wise).
inline uint16_t scaleRgb565(uint16_t c, int t) {
    const int r = ((c >> 11) & 0x1F) * t / 255;
    const int g = ((c >> 5)  & 0x3F) * t / 255;
    const int b = (c & 0x1F) * t / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Vertical piano keyboard width on the left of the roll.
constexpr int kBerlinKbW = 26;

inline bool berlinIsBlackKey(int note) {
    return isBlackPc(((note % 12) + 12) % 12);
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
// pitch axis; keys used by the sequence are marked with a small gray dot, and
// the currently-played note's key greys out; faint row lines give every
// semitone its own lane.
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
    const uint16_t cPlay  = color::Gray;             // played note: the key greys out
    const uint16_t cDot   = rgb565(110, 110, 110);   // "used in sequence" marker dot
    const uint16_t cLine  = rgb565(30, 30, 30);

    // 1) Keyboard + faint per-semitone lane lines.
    for (int nt = lo; nt <= hi; ++nt) {
        const int y0 = yTop(nt);
        const int y1 = yTop(nt - 1);
        int h = y1 - y0;
        if (h < 1) h = 1;
        const bool black = berlinIsBlackKey(nt);
        if (soundingNote == nt) {
            d.fillRect(0, y0, kbW, h, cPlay);                    // played note: key greys out
        } else if (black) {
            d.fillRect(0, y0, kbW, h, cWhite);                   // white base
            d.fillRect(0, y0, kbW * 6 / 10, h, cBlack);          // black key bar
        } else {
            d.fillRect(0, y0, kbW, h, cWhite);
        }
        // "used in sequence" marker: a small gray dot near the key's right edge
        // (skipped on the played key, which is already fully grey).
        if (soundingNote != nt && noteUsed(nt)) {
            int ds = h - 2;
            if (ds > 4) ds = 4;
            if (ds < 2) ds = 2;
            const int dx = kbW - ds - 3;
            const int dy = y0 + (h - ds) / 2;
            d.fillRect(dx, dy, ds, ds, cDot);
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
        // Block brightness reflects the step's velocity: quiet notes dim, loud
        // notes bright. QUADRATIC mapping: real velocities cluster in ~70..126
        // (base 100 ± humanize, + accent), and a linear map over 1..126 wasted
        // most of the brightness range — squaring spreads that musical band
        // across ~30%..100% so per-note differences are clearly visible, while
        // the ~10% floor keeps even the quietest note legible on black.
        // Accents stay white (scaled), normal notes keep the green hue (scaled).
        const int v = s.velocity;
        const int t = 25 + (v * v * 230) / (126 * 126);
        const uint16_t c = scaleRgb565(s.accent ? color::White : noteColor, t);
        d.fillRect(rollX + i * colW + 1, y0, w, bh, c);
    }
}

// Voice identity for the multi-voice roll: index 0 Bass, 1 Mid, 2 High.
constexpr uint16_t kBerlinVoiceColors[3] = {
    rgb565(90, 140, 255),    // Bass — blue
    color::Green,            // Mid
    rgb565(255, 150, 40),    // High — orange
};
constexpr const char* kBerlinVoiceNames[3] = {"BASS", "MID", "HIGH"};

struct BerlinRollVoice {
    const BerlinSequence* seq = nullptr;
    int         playhead     = 0;
    int         soundingNote = -1;
    uint16_t    color        = 0;
    bool        muted        = false;
    bool        edited       = false;
    const char* name         = "";
};

// One roll over the union register of all voices. Each voice spans the full
// width at its own column width (length-normalized), so phasing shows as
// playheads drifting apart; a voice's playhead line is drawn only across its
// own pitch band. Brightness still encodes velocity; the edited voice is
// fully saturated, others dimmed, muted voices darkest. The edited voice's
// name is labelled top-right in its color.
inline void drawBerlinMultiRoll(Display& d, const BerlinRollVoice* vs, int n) {
    d.fillRect(0, kBerlinRollTop, 320, kBerlinRollH, color::Black);
    if (n < 1) return;

    // Union pitch range over all voices' active steps (C3..C5 fallback),
    // octave-snapped, at least 2 octaves — same rules as the single roll.
    int mn = 127, mx = 0;
    bool any = false;
    for (int v = 0; v < n; ++v) {
        const BerlinSequence& s = *vs[v].seq;
        for (int i = 0; i < s.length(); ++i) {
            if (!s.step(i).active) continue;
            if (s.step(i).note < mn) mn = s.step(i).note;
            if (s.step(i).note > mx) mx = s.step(i).note;
            any = true;
        }
    }
    if (!any) { mn = 48; mx = 72; }
    int lo = mn - (mn % 12);
    int hi = mx + (11 - (mx % 12));
    while (hi - lo < 23) { hi += 12; if (hi - lo < 23) lo -= 12; }
    if (lo < 0)   lo = 0;
    if (hi > 127) hi = 127;

    const int nSemis = hi - lo + 1;
    auto yTop = [&](int note) {
        return kBerlinRollTop + (hi - note) * kBerlinRollH / nSemis;
    };
    const int kbW   = kBerlinKbW;
    const int rollX = kbW;
    const int rollW = 320 - kbW;

    // Keyboard + lane lines (a key is marked used/sounding if ANY voice does).
    const uint16_t cWhite = color::LightGray;
    const uint16_t cBlack = rgb565(40, 40, 40);
    const uint16_t cDot   = rgb565(110, 110, 110);
    const uint16_t cLine  = rgb565(30, 30, 30);
    auto usedByAny = [&](int note) {
        for (int v = 0; v < n; ++v) {
            const BerlinSequence& s = *vs[v].seq;
            for (int i = 0; i < s.length(); ++i)
                if (s.step(i).active && s.step(i).note == note) return true;
        }
        return false;
    };
    auto soundingByAny = [&](int note) {
        for (int v = 0; v < n; ++v)
            if (vs[v].soundingNote == note) return true;
        return false;
    };
    for (int nt = lo; nt <= hi; ++nt) {
        const int y0 = yTop(nt);
        const int y1 = yTop(nt - 1);
        int h = y1 - y0;
        if (h < 1) h = 1;
        if (soundingByAny(nt)) {
            d.fillRect(0, y0, kbW, h, color::Gray);
        } else if (berlinIsBlackKey(nt)) {
            d.fillRect(0, y0, kbW, h, cWhite);
            d.fillRect(0, y0, kbW * 6 / 10, h, cBlack);
        } else {
            d.fillRect(0, y0, kbW, h, cWhite);
        }
        if (!soundingByAny(nt) && usedByAny(nt)) {
            int ds = h - 2; if (ds > 4) ds = 4; if (ds < 2) ds = 2;
            d.fillRect(kbW - ds - 3, y0 + (h - ds) / 2, ds, ds, cDot);
        }
        d.fillRect(rollX, y0, rollW, 1, cLine);
    }
    d.fillRect(kbW - 1, kBerlinRollTop, 1, kBerlinRollH, color::DarkGray);

    // Per-voice playhead lines (restricted to the voice's own pitch band) and
    // note blocks. Saturation: edited 255, others 140, muted 70 (of t).
    for (int v = 0; v < n; ++v) {
        const BerlinRollVoice& rv = vs[v];
        const BerlinSequence& s = *rv.seq;
        const int len  = s.length() < 1 ? 1 : s.length();
        const int colW = rollW / len;
        const int sat  = rv.muted ? 70 : (rv.edited ? 255 : 140);

        int bandLo = 127, bandHi = 0;
        for (int i = 0; i < len; ++i) {
            if (!s.step(i).active) continue;
            if (s.step(i).note < bandLo) bandLo = s.step(i).note;
            if (s.step(i).note > bandHi) bandHi = s.step(i).note;
        }
        if (bandLo > bandHi) { bandLo = lo; bandHi = hi; }
        const int bandY0 = yTop(bandHi);
        const int bandY1 = yTop(bandLo - 1);
        if (rv.playhead >= 0 && rv.playhead < len) {
            d.fillRect(rollX + rv.playhead * colW, bandY0, colW,
                       bandY1 - bandY0, scaleRgb565(rv.color, 60));
        }

        for (int i = 0; i < len; ++i) {
            const BerlinStep& st = s.step(i);
            if (!st.active) continue;
            if (st.note < lo || st.note > hi) continue;
            const int y0 = yTop(st.note);
            const int y1 = yTop(st.note - 1);
            int bh = y1 - y0 - 1; if (bh < 2) bh = 2;
            int w = colW * st.gateTicks / 12;
            if (w < 3) w = 3;
            if (w > colW - 1) w = colW - 1;
            const int vt = st.velocity;
            int t = 25 + (vt * vt * 230) / (126 * 126);    // quadratic velocity map
            t = t * sat / 255;
            d.fillRect(rollX + i * colW + 1, y0, w, bh, scaleRgb565(rv.color, t));
        }
    }

    // Edited voice label, top-right of the roll, in the voice's color.
    for (int v = 0; v < n; ++v) {
        if (!vs[v].edited) continue;
        int len = 0; while (vs[v].name[len] != '\0') ++len;
        d.drawText(320 - len * 6 - 4, kBerlinRollTop + 3, vs[v].name,
                   vs[v].color, color::Black, 1);
    }
}

} // namespace core
