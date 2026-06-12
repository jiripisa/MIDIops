#pragma once

#include "core/SequenceGenerator.h"

namespace core {

// Root-heavy bass anchor per the Berlin spec (§2.4c, §9): the root is the
// skeleton on beats 1 and 3 (steps 0 and length/2), other active steps lean
// on root/fifth/octave with a rare excursion to degree 4 or 6 — the
// "heartbeat". The Bass voice always uses it; the Algorithm knob never
// applies to Bass.
class BassAnchorGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
