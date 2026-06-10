#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Meandering contour: each active note steps ±scatter semitones from the
// previous one, quantized into the scale; step 0 anchors on the root.
// Density sets how many steps are active (rests otherwise). The doc's
// flagship generative recipe ("Drunkard's Walk MIDI Generation").
class DrunkardWalkGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
