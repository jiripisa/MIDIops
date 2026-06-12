#pragma once

#include <cstdint>

#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class Scale;
class BerlinSequence;

uint8_t berlinBaseRoot(const Scale& scale, const BerlinParams& p);
void    berlinRegister(const BerlinParams& p, int& lo, int& hi);
int     berlinGateTicks(const BerlinParams& p);
int     berlinFoldIntoRegister(int note, int lo, int hi);
// Draw a stable per-step humanize unit in -100..100. Stored in BerlinStep::velJitter
// at generation; the actual velocity is later re-derived from it via berlinVelocityFor,
// so turning the Humanize knob re-scales the SAME jitter instead of re-rolling.
int8_t  berlinDrawJitter(BerlinRng& rng);
// Final velocity from the stable per-step jitter and the CURRENT params:
// base + jitter*humanize/100 + (accent ? accent : 0), clamped 1..126.
uint8_t berlinVelocityFor(const BerlinParams& p, bool accent, int8_t velJitter);
// Re-derive every active step's velocity from its stored jitter + accent flag
// and the current params — the live re-stamp used by the Dynamics knobs.
void    berlinStampVelocities(BerlinSequence& seq, const BerlinParams& p);
// Consonance weight (1..100) of `note`'s pitch class relative to the scale
// root, flattened toward uniform by tension% (0 = full root/fifth bias,
// 100 = flat). Shared by the Degree-Weighted picker and the Walk bias.
int     berlinDegreeWeight(const Scale& scale, int note, int tensionPercent);
uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng);

// Spec §2.4 step 3 — vertical consonance across the phasing stack. Walks the
// first kMaxSteps time columns; wherever two voices' steps coincide
// (column % length) at an interval class of 1, 6 or 11 semitones, the HIGHER
// note is moved to the nearest in-scale tone (searching outward 1..6
// semitones) that clears the clash in that column. One pass, pragmatic per
// spec; skipped entirely when tension > 60 ("or leave it if Tension is high").
void berlinEnforceConsonance(BerlinSequence** seqs, int n, const Scale& scale,
                             int tensionPercent);

} // namespace core
