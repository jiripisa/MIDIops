#include "core/DrunkardWalkGenerator.h"

#include "core/Scale.h"

namespace core {

void DrunkardWalkGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                     const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    int lo = p.octaveBase;
    int hi = p.octaveBase + 12 * (p.octaveRange < 1 ? 1 : p.octaveRange);
    // Defense-in-depth: keep the register within valid MIDI so the octave-fold
    // below can never push a note past 127 (which the uint8_t cast would wrap).
    if (hi > 127) hi = 127;
    if (lo > hi)  lo = hi;
    const int stepTicks = berlinResolutionTicks(p.resolution);
    int gate = stepTicks * p.gatePercent / 100; if (gate < 1) gate = 1;

    const uint8_t baseRoot = scale.quantize(static_cast<uint8_t>(p.octaveBase + scale.root()));
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
            // Fold into the register by octaves — preserves the pitch class (still in
            // scale) while bringing the note inside [lo, hi].
            while (note > hi) note -= 12;
            while (note < lo) note += 12;
        }
        last = note;

        int vel = p.velocityBase
                  + rng.range(-static_cast<int>(p.velocityHumanize), p.velocityHumanize);
        const bool accent = (i == 0) || (note % 12 == scale.root()); // accent on beat 1 and on root pitch-class notes
        if (accent) vel += p.accent;
        if (vel < 1)   vel = 1;
        if (vel > 126) vel = 126; // cap at 126: leave 127 as headroom so top layers don't screech (per the Berlin doc)

        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.velocity  = static_cast<uint8_t>(vel);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        out.step(i) = s;
    }
}

} // namespace core
