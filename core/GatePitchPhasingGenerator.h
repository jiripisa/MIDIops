#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Note-phasing within one voice: a pitch list (length = params.length) and a
// gate list (length = params.gateLen) of different lengths run against each
// other; realized step i = pitch[i % P] gated by gate[i % G]. The realized
// pattern length is lcm(P, G), capped at BerlinSequence::kMaxSteps.
class GatePitchPhasingGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
