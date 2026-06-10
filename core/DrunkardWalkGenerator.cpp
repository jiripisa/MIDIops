#include "core/DrunkardWalkGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void DrunkardWalkGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                     const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    int lo, hi; berlinRegister(p, lo, hi);
    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    int last = baseRoot;

    for (int i = 0; i < length; ++i) {
        BerlinStep s;
        const bool active = (i == 0) || rng.chance(p.density);
        if (!active) { out.step(i) = s; continue; }

        int note;
        if (i == 0) {
            note = baseRoot;
        } else {
            const int delta = rng.range(-static_cast<int>(p.scatter), p.scatter);
            int cand = last + delta;
            if (cand < lo) cand = lo;
            if (cand > hi) cand = hi;
            note = scale.quantize(static_cast<uint8_t>(cand < 0 ? 0 : (cand > 127 ? 127 : cand)));
            note = berlinFoldIntoRegister(note, lo, hi);
        }
        last = note;

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
