#pragma once

#include <cstdint>

// Bitmap glyphs for the notation view. Encoded row-major with the leftmost
// pixel in the most-significant bit of each row word. Widths up to 16 px
// are supported; the unused MSBs are ignored.

namespace core::notation {

// ---- Treble clef (12 wide × 31 tall) -------------------------------------
// Symmetric "G-clef" shape with the iconic bottom spiral as its dominant
// feature: small open curl at the top, vertical stem, oval bell wrapping
// the G4 staff line, then a multi-step spiral that curls inward from a
// long sweep to the left and closes back at the bottom right.
//
// Row `kTrebleClefG4Row` lands on G4 when blitting.
constexpr int kTrebleClefW     = 12;
constexpr int kTrebleClefH     = 31;
constexpr int kTrebleClefG4Row = 15;

inline constexpr uint16_t kTrebleClef[kTrebleClefH] = {
    0x030,  //  0:  ......##....   top tip
    0x078,  //  1:  .....####...
    0x0CC,  //  2:  ....##..##..   curl opens
    0x186,  //  3:  ...##....##.
    0x186,  //  4:  ...##....##.
    0x0CC,  //  5:  ....##..##..   curl closes
    0x078,  //  6:  .....####...
    0x030,  //  7:  ......##....   transitions to stem
    0x030,  //  8:  ......##....
    0x030,  //  9:  ......##....   stem (centred)
    0x030,  // 10:  ......##....
    0x030,  // 11:  ......##....
    0x0FC,  // 12:  ....######..   bell top
    0x186,  // 13:  ...##....##.
    0x303,  // 14:  ..##......##
    0x303,  // 15:  ..##......##  ← G4
    0x303,  // 16:  ..##......##
    0x186,  // 17:  ...##....##.
    0x0FC,  // 18:  ....######..   bell bottom
    0x030,  // 19:  ......##....
    0x060,  // 20:  .....##.....   tail curves LEFT
    0x0C0,  // 21:  ....##......
    0x180,  // 22:  ...##.......
    0x300,  // 23:  ..##........
    0x600,  // 24:  .##.........
    0xC00,  // 25:  ##..........
    0xC00,  // 26:  ##..........
    0xC03,  // 27:  ##........##   spiral closes
    0x606,  // 28:  .##......##.
    0x30C,  // 29:  ..##....##..
    0x1F8,  // 30:  ...######...   spiral inner end
};

// ---- Bass clef (12 wide × 16 tall) ---------------------------------------
// "9"-shaped curl with TWO clearly separated dots stacked vertically to
// the right of the body. The F3 staff line passes between the dots and
// through the open right side of the curl. Body uses cols 0–7; the two
// dots sit at cols 9–10 with col 8 empty as a separator.
//
// Row `kBassClefF3Row` lands on F3 when blitting.
constexpr int kBassClefW     = 12;
constexpr int kBassClefH     = 16;
constexpr int kBassClefF3Row = 5;

inline constexpr uint16_t kBassClef[kBassClefH] = {
    0x3F0,  //  0:  ..######....   top arc
    0x630,  //  1:  .##....##...
    0xC30,  //  2:  ##....##....   body sides
    0xC30,  //  3:  ##....##....
    0xC36,  //  4:  ##....##.##.   ← top dot (cols 9–10)
    0xC30,  //  5:  ##....##....   ← F3 line
    0xC36,  //  6:  ##....##.##.   ← bottom dot
    0xC30,  //  7:  ##....##....
    0x630,  //  8:  .##....##...
    0x3F0,  //  9:  ..######....   curl closes
    0x0C0,  // 10:  ....##......   tail
    0x060,  // 11:  .....##.....
    0x030,  // 12:  ......##....
    0x018,  // 13:  .......##...
    0x00C,  // 14:  ........##..
    0x006,  // 15:  .........##.
};

// ---- Filled oval note head (8 wide × 5 tall) -----------------------------
constexpr int kNoteHeadW = 8;
constexpr int kNoteHeadH = 5;

inline constexpr uint16_t kNoteHead[kNoteHeadH] = {
    0x03C,  // ..####..
    0x07E,  // .######.
    0x0FF,  // ########
    0x07E,  // .######.
    0x03C,  // ..####..
};

// ---- Sharp accidental (5 wide × 9 tall) ---------------------------------
constexpr int kSharpW = 5;
constexpr int kSharpH = 9;

inline constexpr uint16_t kSharp[kSharpH] = {
    0x00A,  // .#.#.
    0x00A,  // .#.#.
    0x01F,  // #####
    0x00A,  // .#.#.
    0x00A,  // .#.#.
    0x01F,  // #####
    0x00A,  // .#.#.
    0x00A,  // .#.#.
    0x00A,  // .#.#.
};

} // namespace core::notation
