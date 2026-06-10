#pragma once

#include <cstdint>

namespace core {

struct BerlinStep {
    bool     active    = false;  // false = rest
    uint8_t  note      = 0;      // absolute MIDI note (scale-quantized)
    uint8_t  velocity  = 0;      // 1..127
    bool     accent    = false;  // visualization + already folded into velocity
    uint16_t gateTicks = 0;      // note-on duration in 24-PPQN ticks
};

// Fixed-capacity realized step pattern (no heap). Walk/Degree fill ≤ length
// steps; Phasing (Plan B) renders a bounded window here.
class BerlinSequence {
public:
    static constexpr int kMaxSteps = 32;

    int  length() const { return length_; }
    void setLength(int n) { length_ = n < 1 ? 1 : (n > kMaxSteps ? kMaxSteps : n); }

    const BerlinStep& step(int i) const { return steps_[clampIdx(i)]; }
    BerlinStep&       step(int i)       { return steps_[clampIdx(i)]; }

    void clear() { for (int i = 0; i < kMaxSteps; ++i) steps_[i] = BerlinStep{}; }

private:
    static int clampIdx(int i) { return i < 0 ? 0 : (i >= kMaxSteps ? kMaxSteps - 1 : i); }
    BerlinStep steps_[kMaxSteps] {};
    int        length_ = 16;
};

} // namespace core
