#include "core/ClockFollower.h"

namespace core {

void ClockFollower::onPulse(uint32_t nowMs) {
    // Gap detection: if a window is in progress and the spacing since the last
    // pulse exceeds kGapMs (far beyond any 30-BPM ~83 ms spacing), the upstream
    // clock paused and resumed. Re-anchor on this pulse so the long idle span is
    // never averaged into the window (which would report a bogus ~30 BPM), while
    // keeping the last known bpm_ for display.
    if (haveFirst_ && (nowMs - lastPulseMs_) > kGapMs) {
        haveFirst_ = false;
    }
    lastPulseMs_ = nowMs;
    if (!haveFirst_) {
        firstMs_ = nowMs;
        haveFirst_ = true;
        count_ = 0;
        return;   // the anchor pulse opens the window; intervals count after it
    }
    ++count_;
    if (count_ == kWindow) {
        uint32_t dt = nowMs - firstMs_;
        if (dt > 0) {
            // 24 pulses span one beat, so dt ms is one beat → BPM = 60000 / dt.
            uint32_t bpm = 60000u / dt;
            if (bpm < 30) bpm = 30;
            if (bpm > 300) bpm = 300;
            bpm_ = static_cast<uint16_t>(bpm);
        }
        firstMs_ = nowMs;
        count_ = 0;
    }
}

} // namespace core
