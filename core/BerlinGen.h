#pragma once

#include <cstdint>

#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class Scale;

uint8_t berlinBaseRoot(const Scale& scale, const BerlinParams& p);
void    berlinRegister(const BerlinParams& p, int& lo, int& hi);
int     berlinGateTicks(const BerlinParams& p);
int     berlinFoldIntoRegister(int note, int lo, int hi);
uint8_t berlinFinalizeVelocity(const BerlinParams& p, bool accent, BerlinRng& rng);
uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng);

} // namespace core
