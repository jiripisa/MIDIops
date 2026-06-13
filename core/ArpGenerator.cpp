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

    // Number of degrees in the scale: the denominator for the octave wrap in
    // the cycling formula (triadDegs[i%3] + scaleLen*(i/3)). Scale exposes this
    // directly, which also avoids a top-of-MIDI-range edge case the old
    // empirical scan could mis-detect.
    const int scaleLen = scale.degreeCount();

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
