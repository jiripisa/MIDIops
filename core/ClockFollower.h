#pragma once

#include <cstdint>

namespace core {

// Averages the interval over a one-beat (24-pulse) window to derive BPM
// from an incoming 24-PPQN clock. Clamped to [30..300]. Returns 0 until
// enough pulses have been seen.
class ClockFollower {
public:
    void reset() { count_ = 0; haveFirst_ = false; bpm_ = 0; firstMs_ = 0; }
    // Call on each incoming Clock pulse with a monotonic ms timestamp.
    void onPulse(uint32_t nowMs);
    uint16_t bpm() const { return bpm_; }   // 0 until known
private:
    static constexpr int kWindow = 24;      // one beat
    bool     haveFirst_ = false;
    uint32_t firstMs_ = 0;
    int      count_ = 0;
    uint16_t bpm_ = 0;
};

} // namespace core
