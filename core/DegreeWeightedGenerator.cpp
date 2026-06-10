#include "core/DegreeWeightedGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void DegreeWeightedGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                       const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);

    for (int i = 0; i < length; ++i) {
        BerlinStep s;
        const bool active = (i == 0) || rng.chance(p.density);
        if (!active) { out.step(i) = s; continue; }

        const int note = (i == 0) ? baseRoot
                                  : berlinDegreeWeightedNote(scale, baseRoot, p, rng);
        const bool accent = (i == 0) || (note % 12 == scale.root());

        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.velocity  = berlinFinalizeVelocity(p, accent, rng);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        out.step(i) = s;
    }
}

} // namespace core
