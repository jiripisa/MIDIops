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
// Consonance weight (1..100) of `note`'s pitch class relative to the scale
// root, flattened toward uniform by tension% (0 = full root/fifth bias,
// 100 = flat). Shared by the Degree-Weighted picker and the Walk bias.
int     berlinDegreeWeight(const Scale& scale, int note, int tensionPercent);
uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng);

} // namespace core
