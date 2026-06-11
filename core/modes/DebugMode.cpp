#include "core/modes/DebugMode.h"

#include <cstdio>

#include "core/Display.h"

namespace core {

DebugMode::DebugMode() = default;

void DebugMode::onRawInput(const RawInput& in) {
    switch (in.kind) {
        case RawInput::Kind::EncoderKnob:
            if (in.index >= 1 && in.index <= 5 && in.delta != 0) {
                encKnob_[in.index].total += in.delta;
                encKnob_[in.index].lastDelta = in.delta > 0 ? 1 : -1;
                encKnob_[in.index].lastMs = nowMs_;
            }
            break;
        case RawInput::Kind::EncoderSw:
            if (in.index >= 1 && in.index <= 5) {
                ++encSw_[in.index].count;
                encSw_[in.index].lastMs = nowMs_;
            }
            break;
        case RawInput::Kind::Latch:
            // Count flips, not frames: the shell delivers the level every loop.
            if (in.index >= 1 && in.index <= 3 && in.on != lastLatchOn_[in.index]) {
                lastLatchOn_[in.index] = in.on;
                ++latch_[in.index].count;
                latch_[in.index].lastMs = nowMs_;
            }
            break;
    }
}

void DebugMode::DebugScreen::render(Display& d) const {
    constexpr uint32_t kRecentMs = 500;
    int y = 14;
    char buf[40];
    for (int i = 1; i <= 5; ++i) {
        const auto& k = m_.encKnob_[i];
        const bool recent = k.lastMs != 0 && (m_.nowMs_ - k.lastMs) < kRecentMs;
        const uint16_t col = recent ? color::Yellow : color::White;
        std::snprintf(buf, sizeof(buf), "ENC%d  tot %ld  d %+d", i, k.total, k.lastDelta);
        d.drawText(4, y, buf, col, color::Black, 1);
        y += 12;
    }
    for (int i = 1; i <= 5; ++i) {
        std::snprintf(buf, sizeof(buf), "ENC%dsw  #%u", i, m_.encSw_[i].count);
        d.drawText(4, y, buf, color::White, color::Black, 1);
        y += 10;
    }
    for (int i = 1; i <= 3; ++i) {
        std::snprintf(buf, sizeof(buf), "LATCH%d  #%u", i, m_.latch_[i].count);
        d.drawText(170, 14 + (i - 1) * 12, buf, color::White, color::Black, 1);
    }
}

} // namespace core
