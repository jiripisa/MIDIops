#include "core/BassAnchorGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void BassAnchorGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                   const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    int lo = 0, hi = 0;
    berlinRegister(p, lo, hi);
    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    const int half = length / 2;          // "beat 3" of a 16-step bar

    for (int i = 0; i < length; ++i) {
        const bool anchor = (i == 0) || (length >= 8 && i == half);
        const bool active = anchor || rng.chance(p.density);
        if (!active) { out.step(i) = BerlinStep{}; continue; }

        int note = baseRoot;
        if (!anchor) {
            // Pitch palette per spec §9: mostly root, then fifth/octave,
            // rarely degree 4 or 6. Quantized to scale, folded into register.
            const int r = rng.range(0, 99);
            if      (r < 55) note = baseRoot;                              // root
            else if (r < 75) note = baseRoot + 7;                          // fifth
            else if (r < 90) note = baseRoot + 12;                         // octave
            else             note = baseRoot + (rng.chance(50) ? 5 : 9);   // deg 4/6
            if (note > 127) note = 127;
            note = scale.quantize(static_cast<uint8_t>(note));
            note = berlinFoldIntoRegister(note, lo, hi);
        }

        BerlinStep s;
        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.accent    = anchor;
        s.gateTicks = static_cast<uint16_t>(gate);
        s.velJitter = berlinDrawJitter(rng);
        s.velocity  = berlinVelocityFor(p, s.accent, s.velJitter);
        out.step(i) = s;
    }
}

} // namespace core
