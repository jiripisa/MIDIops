#include "core/Scale.h"

namespace core {

// ---------------------------------------------------------------------------
// Interval tables — semitone offsets from root, ascending within one octave
// ---------------------------------------------------------------------------

static const uint8_t kMajor[]      = {0, 2, 4, 5, 7, 9, 11};   // 7 notes
static const uint8_t kMinor[]      = {0, 2, 3, 5, 7, 8, 10};   // 7 notes
static const uint8_t kAug[]        = {0, 2, 4, 6, 8, 10};      // 6 notes (whole-tone)
static const uint8_t kDim[]        = {0, 1, 3, 4, 6, 7, 9, 10};// 8 notes (octatonic half-whole)
static const uint8_t kPentaMajor[] = {0, 2, 4, 7, 9};          // 5 notes
static const uint8_t kPentaMinor[] = {0, 3, 5, 7, 10};         // 5 notes

int Scale::intervals(const uint8_t** out) const {
    switch (type_) {
        case Type::Major:      *out = kMajor;      return 7;
        case Type::Minor:      *out = kMinor;      return 7;
        case Type::Aug:        *out = kAug;        return 6;
        case Type::Dim:        *out = kDim;        return 8;
        case Type::PentaMajor: *out = kPentaMajor; return 5;
        case Type::PentaMinor: *out = kPentaMinor; return 5;
        default:               *out = kMajor;      return 7;
    }
}

// ---------------------------------------------------------------------------
// contains — is note's pitch-class in the scale?
// ---------------------------------------------------------------------------

bool Scale::contains(uint8_t note) const {
    const uint8_t* ivs = nullptr;
    int len = intervals(&ivs);
    // pitch class relative to root, in 0..11
    uint8_t pc = static_cast<uint8_t>((note - root_ + 120) % 12);
    for (int i = 0; i < len; ++i) {
        if (ivs[i] == pc) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// quantize — nearest in-scale note; ties prefer the LOWER note
// ---------------------------------------------------------------------------

uint8_t Scale::quantize(uint8_t note) const {
    if (contains(note)) return note;
    for (int d = 1; d <= 6; ++d) {
        int lo = static_cast<int>(note) - d;
        int hi = static_cast<int>(note) + d;
        if (lo >= 0 && contains(static_cast<uint8_t>(lo))) return static_cast<uint8_t>(lo);
        if (hi <= 127 && contains(static_cast<uint8_t>(hi))) return static_cast<uint8_t>(hi);
    }
    // Fallback: return the note itself (shouldn't happen for any sensible scale)
    return note;
}

// ---------------------------------------------------------------------------
// degreeNote — MIDI note `degrees` scale-steps above fromNote
// ---------------------------------------------------------------------------

uint8_t Scale::degreeNote(uint8_t fromNote, int degrees) const {
    const uint8_t* ivs = nullptr;
    int len = intervals(&ivs);

    // Quantize the starting note to the scale
    uint8_t qnote = quantize(fromNote);

    // Find the pitch class of qnote relative to root
    uint8_t pc = static_cast<uint8_t>((static_cast<int>(qnote) - root_ + 120) % 12);

    // Find its index in the interval table
    int idx = 0;
    for (int i = 0; i < len; ++i) {
        if (ivs[i] == pc) { idx = i; break; }
    }

    // The "base note" is the root note of the same-octave span that qnote belongs to.
    // qnote = base_note + ivs[idx], so:
    int base_note = static_cast<int>(qnote) - static_cast<int>(ivs[idx]);

    // Target index in the unbounded ascending scale sequence
    int target_idx = idx + degrees;

    // Handle negative target_idx (degrees < 0) with floor division
    int extra_octaves;
    int deg_in_oct;
    if (target_idx >= 0) {
        extra_octaves = target_idx / len;
        deg_in_oct    = target_idx % len;
    } else {
        // Floor division for negatives: round toward -infinity
        extra_octaves = (target_idx - len + 1) / len;
        deg_in_oct    = target_idx - extra_octaves * len;
    }

    int result = base_note + static_cast<int>(ivs[deg_in_oct]) + 12 * extra_octaves;

    // Clamp to valid MIDI range
    if (result < 0)   result = 0;
    if (result > 127) result = 127;

    return static_cast<uint8_t>(result);
}

int Scale::degreeCount() const {
    const uint8_t* ivs = nullptr;
    return intervals(&ivs);
}

} // namespace core
