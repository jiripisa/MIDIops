#include "core/ArpGenerator.h"

#include "core/Scale.h"

namespace core {
namespace ArpGenerator {

int build(uint8_t rootNote, const Scale& scale, const ArpParams& p,
          uint8_t* out, int outCap) {
    // Clamp step count to valid range and available capacity
    int count = static_cast<int>(p.steps);
    if (count < 1)  count = 1;
    if (count > kMaxSteps) count = kMaxSteps;
    if (count > outCap) count = outCap;

    // Quantize the root note into the scale
    uint8_t root = scale.quantize(rootNote);

    // Triad scale-degree offsets within one scale span: root(0), 3rd(2), 5th(4)
    static constexpr int kTriadDegs[3] = {0, 2, 4};

    // Number of degrees in the scale (needed for octave-wrap calculation).
    // We derive it by counting up from the root until we reach the next
    // octave — that is, until degreeNote wraps.  For the formula below we
    // need the scale length so we can compute how many full scale-spans
    // fit into one octave.  A simpler approach: call degreeNote directly
    // using the cycling formula (triadDegs[i%3] + scaleLen*(i/3)).
    //
    // To avoid needing scaleLen explicitly we can use:
    //   octaveSpan = 7 for 7-note scales, 5 for pentatonic, etc.
    // But Scale doesn't expose its length publicly.  Instead, we walk the
    // triad cycle directly: for the i-th step, the scale degree is
    //   triadDegs[i%3]  +  (scaleLen * (i/3))
    // and scaleLen is the denominator for the octave wrap.
    //
    // The simplest portable approach: find scaleLen by checking when
    // degreeNote(root, n) crosses into the next octave (pitch class wraps).
    // We search over n = 1..12.
    int scaleLen = 7; // safe default; overridden below
    for (int n = 1; n <= 12; ++n) {
        uint8_t candidate = scale.degreeNote(root, n);
        // When pitch class returns to root's pitch class we've completed one span
        if ((candidate - root) % 12 == 0 && candidate != root) {
            scaleLen = n;
            break;
        }
    }

    for (int i = 0; i < count; ++i) {
        int degree = kTriadDegs[i % 3] + scaleLen * (i / 3);
        int note = static_cast<int>(scale.degreeNote(root, degree))
                   + 12 * static_cast<int>(p.octave);
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        out[i] = static_cast<uint8_t>(note);
    }

    return count;
}

} // namespace ArpGenerator
} // namespace core
