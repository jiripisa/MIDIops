#include "core/ClockFollower.h"

namespace core {

void ClockFollower::onPulse(uint32_t nowMs) {
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
