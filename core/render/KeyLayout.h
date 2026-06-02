#pragma once

#include <cstdint>

namespace core {

// ---- Layout constants (320x240 logical pixels) --------------------------

static constexpr int     kScreenW          = 320;
static constexpr int     kRollTop          = 22;   // inclusive upper edge
// Roll/keyboard boundary. Lowered from 180 to 187 so the keyboard (anchored
// at the bottom edge) is a bit shorter: white keys 240-187=53px against the
// unchanged 32px black keys (~0.60 ratio, closer to a real piano).
static constexpr int     kRollBottom       = 187;  // exclusive lower edge
static constexpr int     kKeyboardTop      = 187;
static constexpr int     kKeyboardBot      = 240;
static constexpr int     kBlackKeyH        = 32;

// Keyboard window — starts on C2, ends on B5.
static constexpr uint8_t kLowestNote       = 36;   // C2
static constexpr uint8_t kHighestNote      = 83;   // B5
static constexpr int     kWhiteKeysVisible = 28;   // 4 octaves
static constexpr int     kWhiteKeyW        = 11;   // 28 * 11 = 308
static constexpr int     kBlackKeyW        = 7;
static constexpr int     kKeyboardX0       =
    (kScreenW - kWhiteKeysVisible * kWhiteKeyW) / 2;

// ---- Types --------------------------------------------------------------

struct KeyRect { int x; int w; bool isBlack; };

// ---- Internal lookup tables (static constexpr, header-local) ------------

namespace detail {

// Pitch-class (0..11) -> index of the white key inside its octave, or -1 for
// black keys. C, D, E, F, G, A, B.
static constexpr int8_t kWhiteIdxInOctave[12] = {
     0, -1,  1, -1,  2,
     3, -1,  4, -1,  5,
    -1,  6
};

// Pitch-class of each white key within an octave, indexed 0..6.
static constexpr uint8_t kWhitePc[7] = {0, 2, 4, 5, 7, 9, 11};

} // namespace detail

// ---- Helpers ------------------------------------------------------------

inline bool noteVisible(uint8_t note) {
    return note >= kLowestNote && note <= kHighestNote;
}

inline bool isBlackPc(int pc) {
    return detail::kWhiteIdxInOctave[pc] < 0;
}

inline int whiteKeyIdx(uint8_t note) {
    const int octaveDiff = (note / 12) - (kLowestNote / 12);
    const int wi         = detail::kWhiteIdxInOctave[note % 12];
    return wi + octaveDiff * 7;
}

inline uint8_t whiteKeyAt(int idx) {
    const int oct = idx / 7;
    const int pc  = detail::kWhitePc[idx % 7];
    return static_cast<uint8_t>(kLowestNote + oct * 12 + pc);
}

inline KeyRect keyRectFor(uint8_t note) {
    if (!noteVisible(note)) return {-1, 0, false};
    const int pc = note % 12;
    if (isBlackPc(pc)) {
        const int leftWhiteIdx = whiteKeyIdx(static_cast<uint8_t>(note - 1));
        const int whiteRightEdge =
            kKeyboardX0 + (leftWhiteIdx + 1) * kWhiteKeyW;
        return { whiteRightEdge - kBlackKeyW / 2, kBlackKeyW, true };
    }
    const int wi = whiteKeyIdx(note);
    return { kKeyboardX0 + wi * kWhiteKeyW, kWhiteKeyW, false };
}

} // namespace core
