#include "core/BerlinGen.h"

#include "core/Scale.h"

namespace core {

uint8_t berlinBaseRoot(const Scale& scale, const BerlinParams& p) {
    return scale.quantize(static_cast<uint8_t>(p.octaveBase + scale.root()));
}

void berlinRegister(const BerlinParams& p, int& lo, int& hi) {
    lo = p.octaveBase;
    hi = p.octaveBase + 12 * (p.octaveRange < 1 ? 1 : p.octaveRange);
    if (hi > 127) hi = 127;
    if (lo > hi)  lo = hi;
}

int berlinGateTicks(const BerlinParams& p) {
    int g = berlinResolutionTicks(p.resolution) * p.gatePercent / 100;
    return g < 1 ? 1 : g;
}

int berlinFoldIntoRegister(int note, int lo, int hi) {
    while (note > hi) note -= 12;
    while (note < lo) note += 12;
    return note;
}

uint8_t berlinFinalizeVelocity(const BerlinParams& p, bool accent, BerlinRng& rng) {
    int vel = p.velocityBase
              + rng.range(-static_cast<int>(p.velocityHumanize), p.velocityHumanize);
    if (accent) vel += p.accent;
    if (vel < 1)   vel = 1;
    if (vel > 126) vel = 126;     // leave 127 as headroom (per the Berlin doc)
    return static_cast<uint8_t>(vel);
}

// Interval consonance weight (0..100) by semitone (mod 12), per spec §2.1.
static int intervalWeight(int semitone) {
    switch (((semitone % 12) + 12) % 12) {
        case 0:  return 100;   case 7:  return 95;   case 5:  return 85;
        case 4:  return 55;    case 3:  return 55;   case 9:  return 45;
        case 8:  return 45;    case 2:  return 35;   case 10: return 30;
        case 11: return 30;    case 1:  return 15;   case 6:  return 5;
        default: return 30;
    }
}

uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng) {
    int lo, hi; berlinRegister(p, lo, hi);
    const int degs = scale.degreeCount();
    const int tension = p.tension > 100 ? 100 : p.tension;

    int weights[8];                 // degreeCount is 5..8
    int total = 0;
    for (int i = 0; i < degs; ++i) {
        const int semis = (static_cast<int>(scale.degreeNote(baseRoot, i)) - baseRoot + 120) % 12;
        int w = intervalWeight(semis);
        w = w + (100 - w) * tension / 100;        // higher tension → flatter
        weights[i] = w;
        total += w;
    }

    int pick = rng.range(0, total - 1);
    int idx = 0;
    for (int i = 0; i < degs; ++i) { pick -= weights[i]; if (pick < 0) { idx = i; break; } }

    const int oct = rng.range(0, (p.octaveRange < 1 ? 1 : p.octaveRange) - 1);
    int note = scale.degreeNote(baseRoot, idx + degs * oct);
    note = berlinFoldIntoRegister(note, lo, hi);
    return static_cast<uint8_t>(note);
}

} // namespace core
