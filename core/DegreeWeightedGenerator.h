#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Each active step picks a scale degree weighted toward root/fifth (Tension
// flattens the weights). Step 0 anchors on the root. Density gates rests.
class DegreeWeightedGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
