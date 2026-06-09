#pragma once

#include <cstdint>

#include "core/ArpTypes.h"

namespace core {

class Scale;

// Pure note generation: the ascending list of MIDI notes for a held root.
// Direction ordering is applied later by the engine, NOT here.
namespace ArpGenerator {
    static constexpr int kMaxSteps = 16;
    // Quantizes root into `scale`, builds the diatonic triad (scale degrees
    // 0,2,4,...) extended to `p.steps` notes wrapping octaves, applies
    // `p.octave` shift, writes ascending notes into `out` (capacity outCap),
    // returns the count (== min(p.steps, outCap), p.steps clamped to 1..16).
    int build(uint8_t rootNote, const Scale& scale, const ArpParams& p,
              uint8_t* out, int outCap);
}

} // namespace core
