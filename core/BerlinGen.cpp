#include "core/BerlinGen.h"

#include "core/BerlinSequence.h"
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

int8_t berlinDrawJitter(BerlinRng& rng) {
    return static_cast<int8_t>(rng.range(-100, 100));
}

uint8_t berlinVelocityFor(const BerlinParams& p, bool accent, int8_t velJitter) {
    int vel = p.velocityBase + (static_cast<int>(velJitter) * p.velocityHumanize) / 100;
    if (accent) vel += p.accent;
    if (vel < 1)   vel = 1;
    if (vel > 126) vel = 126;     // leave 127 as headroom (per the Berlin doc)
    return static_cast<uint8_t>(vel);
}

void berlinStampVelocities(BerlinSequence& seq, const BerlinParams& p) {
    for (int i = 0; i < seq.length(); ++i) {
        BerlinStep& s = seq.step(i);
        if (!s.active) continue;
        s.velocity = berlinVelocityFor(p, s.accent, s.velJitter);
    }
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

int berlinDegreeWeight(const Scale& scale, int note, int tensionPercent) {
    const int t = tensionPercent < 0 ? 0 : (tensionPercent > 100 ? 100 : tensionPercent);
    const int semis = ((note % 12) - scale.root() + 24) % 12;
    const int w = intervalWeight(semis);
    return w + (100 - w) * t / 100;               // higher tension → flatter
}

uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng) {
    int lo, hi; berlinRegister(p, lo, hi);
    const int degs = scale.degreeCount();

    int weights[8];                 // degreeCount is 5..8
    int total = 0;
    for (int i = 0; i < degs; ++i) {
        const int w = berlinDegreeWeight(scale, scale.degreeNote(baseRoot, i), p.tension);
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
