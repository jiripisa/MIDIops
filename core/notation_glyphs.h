#pragma once

#include <cstdint>

// Bitmap glyphs for the notation view: treble + bass clefs (positioned
// against the staff via their "anchor row"), filled oval note head and
// a sharp accidental. All glyphs are encoded row-major with the leftmost
// pixel in the most-significant bit. Width is ≤ 8, so one byte per row.

namespace core::notation {

// ---- Treble clef (8 wide × 28 tall) ---------------------------------------
//   The middle bell wraps around the G4 line. Row `kTrebleClefG4Row` should
//   land exactly on the G4 staff line when blitting.
constexpr int kTrebleClefW      = 8;
constexpr int kTrebleClefH      = 28;
constexpr int kTrebleClefG4Row  = 13;

inline constexpr uint8_t kTrebleClef[kTrebleClefH] = {
    0x3C,  // ..####..   top head curl
    0x7E,  // .######.
    0xC3,  // ##....##
    0xC3,  // ##....##
    0xC3,  // ##....##
    0x7E,  // .######.
    0x60,  // .##.....
    0x60,  // .##.....
    0x60,  // .##.....
    0x60,  // .##.....
    0x60,  // .##.....
    0x7E,  // .######.   bell top
    0xC3,  // ##....##
    0xDB,  // ##.##.##   ← G4 line (dot marks the G)
    0xC3,  // ##....##
    0x7E,  // .######.   bell bottom
    0x60,  // .##.....
    0x60,  // .##.....
    0x60,  // .##.....
    0x60,  // .##.....
    0x70,  // .###....
    0x38,  // ..###...
    0x1C,  // ...###..
    0x1E,  // ...####.
    0x0F,  // ....####
    0x07,  // .....###
    0x06,  // .....##.
    0x06,  // .....##.
};

// ---- Bass clef (8 wide × 14 tall) ----------------------------------------
//   The curl sits on top of the F3 line; row `kBassClefF3Row` lands on F3.
constexpr int kBassClefW      = 8;
constexpr int kBassClefH      = 14;
constexpr int kBassClefF3Row  = 4;

inline constexpr uint8_t kBassClef[kBassClefH] = {
    0x3C,  // ..####..
    0x7E,  // .######.
    0xC3,  // ##....##
    0xC2,  // ##....#.
    0xC9,  // ##.##..#   ← F3 line (with top dot to the right)
    0xC2,  // ##....#.
    0xC9,  // ##.##..#   ← bottom dot
    0xC2,  // ##....#.
    0xC3,  // ##....##
    0xC6,  // ##...##.
    0x7C,  // .#####..
    0x18,  // ...##...
    0x0C,  // ....##..
    0x06,  // .....##.
};

// ---- Filled oval note head (8 wide × 5 tall) ----------------------------
//   Cheap symmetric oval — good enough at this resolution. Real music
//   notation tilts the oval slightly, but at 8 px the slant disappears.
constexpr int kNoteHeadW = 8;
constexpr int kNoteHeadH = 5;

inline constexpr uint8_t kNoteHead[kNoteHeadH] = {
    0x3C,  // ..####..
    0x7E,  // .######.
    0xFF,  // ########
    0x7E,  // .######.
    0x3C,  // ..####..
};

// ---- Sharp accidental (5 wide × 9 tall) ---------------------------------
constexpr int kSharpW = 5;
constexpr int kSharpH = 9;

inline constexpr uint8_t kSharp[kSharpH] = {
    0x0A,  // .#.#.
    0x0A,  // .#.#.
    0x1F,  // #####
    0x0A,  // .#.#.
    0x0A,  // .#.#.
    0x1F,  // #####
    0x0A,  // .#.#.
    0x0A,  // .#.#.
    0x0A,  // .#.#.
};

} // namespace core::notation
