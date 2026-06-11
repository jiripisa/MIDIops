#pragma once

#include <cstdint>

namespace core {

enum class BerlinAlgorithm : uint8_t { DrunkardWalk = 0, GatePitchPhasing, DegreeWeighted, kCount };
enum class BerlinResolution : uint8_t { Eighth = 0, Sixteenth, kCount };
enum class BerlinBehavior : uint8_t { Locked = 0, Evolve, Live, kCount };

// 24-PPQN clock ticks per step for each resolution.
inline int berlinResolutionTicks(BerlinResolution r) {
    return r == BerlinResolution::Sixteenth ? 6 : 12;
}

// All Berlin generator + playback parameters. Scale + root are NOT here —
// they come from global Settings (AppServices::scale()). Tempo is global too.
struct BerlinParams {
    BerlinAlgorithm  algorithm        = BerlinAlgorithm::DrunkardWalk;
    uint8_t          length           = 16;   // 3..32 steps (BerlinSequence::kMaxSteps)
    BerlinResolution resolution       = BerlinResolution::Eighth;
    uint8_t          density          = 50;   // 0..100 % active steps
    uint8_t          gatePercent      = 55;   // 40..99
    uint8_t          tension          = 30;   // 0..100 % (degree-weight spread)
    uint8_t          octaveBase       = 48;   // MIDI note of C in the base octave (C1=24..C5=72), step 12
    uint8_t          octaveRange      = 2;    // 1..3 octaves
    uint8_t          velocityBase     = 100;  // 1..126
    uint8_t          velocityHumanize = 20;   // 0..30 (±)
    uint8_t          accent           = 20;   // 0..27 velocity boost
    uint8_t          scatter          = 3;    // 1..7 semitones (Drunkard's Walk)
    uint8_t          gateLen          = 6;    // 3..16 (Gate/Pitch Phasing — Plan B)
    BerlinBehavior   behavior         = BerlinBehavior::Live;
    uint8_t          morph            = 100;  // 0..100 % regeneration intensity
    uint8_t          evolveRate       = 4;    // 1..8 loops (Evolve — Plan C)
};

} // namespace core
