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
            // Spec §4.1: bias the wander toward consonant degrees. Enumerate
            // the in-scale candidates within ±scatter semitones of the
            // previous note (inside the register) and pick one weighted by
            // the consonance of its pitch class relative to the scale root;
            // Tension flattens the weights toward uniform. The window always
            // contains `last` itself, so it is effectively never empty.
            const int scat = p.scatter < 1 ? 1 : (p.scatter > 7 ? 7 : p.scatter);
            int cands[15];                  // scatter <= 7 → window <= 15 semitones
            int wts[15];
            int cnt = 0;
            int total = 0;
            for (int c = last - scat; c <= last + scat; ++c) {
                if (c < lo || c > hi) continue;
                if (!scale.contains(static_cast<uint8_t>(c))) continue;
                const int w = berlinDegreeWeight(scale, c, p.tension);
                cands[cnt] = c;
                wts[cnt] = w;
                ++cnt;
                total += w;
            }
            if (cnt == 0) {
                note = last;                // defensive; unreachable in practice
            } else {
                int pick = rng.range(0, total - 1);
                int idx = 0;
                for (int k = 0; k < cnt; ++k) {
                    pick -= wts[k];
                    if (pick < 0) { idx = k; break; }
                }
                note = cands[idx];
            }
        }
        last = note;

        const bool accent = (i == 0) || (note % 12 == scale.root());
        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        s.velJitter = berlinDrawJitter(rng);
        s.velocity  = berlinVelocityFor(p, accent, s.velJitter);
        out.step(i) = s;
    }
}

} // namespace core
