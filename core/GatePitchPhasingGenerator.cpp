#include "core/GatePitchPhasingGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

static int gcdInt(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

void GatePitchPhasingGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                         const Scale& scale, BerlinRng& rng) {
    auto clampLen = [](int v) { return v < 1 ? 1 : (v > 16 ? 16 : v); };
    const int P = clampLen(p.length);
    const int G = clampLen(p.gateLen);

    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    const int gate = berlinGateTicks(p);

    // PITCH list (root-heavy). pitch[0] = root.
    uint8_t pitch[16];
    for (int k = 0; k < P; ++k)
        pitch[k] = (k == 0) ? baseRoot : berlinDegreeWeightedNote(scale, baseRoot, p, rng);

    // GATE list (density-gated). gate[0] = on.
    bool gateOn[16];
    for (int k = 0; k < G; ++k)
        gateOn[k] = (k == 0) ? true : rng.chance(p.density);

    // Realized length = lcm(P, G), capped at kMaxSteps.
    int lcm = P / gcdInt(P, G) * G;
    if (lcm > BerlinSequence::kMaxSteps) lcm = BerlinSequence::kMaxSteps;

    out.clear();
    out.setLength(lcm);
    for (int i = 0; i < lcm; ++i) {
        BerlinStep s;
        if (gateOn[i % G]) {
            const uint8_t note = pitch[i % P];
            const bool accent = (note % 12 == scale.root());
            s.active    = true;
            s.note      = note;
            s.velocity  = berlinFinalizeVelocity(p, accent, rng);
            s.accent    = accent;
            s.gateTicks = static_cast<uint16_t>(gate);
        }
        out.step(i) = s;
    }
}

} // namespace core
