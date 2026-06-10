#pragma once

#include "core/BerlinSequence.h"
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class Scale;

// Fills a BerlinSequence from params + the global scale, using `rng`. A pure
// function of (params, scale, rng-state) → fully unit-testable with a fixed seed.
class SequenceGenerator {
public:
    virtual ~SequenceGenerator() = default;
    virtual void generate(BerlinSequence& out, const BerlinParams& p,
                          const Scale& scale, BerlinRng& rng) = 0;
};

} // namespace core
