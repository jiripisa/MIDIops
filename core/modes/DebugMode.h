#pragma once

#include <cstdint>

#include "core/app/Mode.h"
#include "core/render/ModeIcons.h"

namespace core {

// Hardware bring-up telemetry: per-control activity for all 5 encoders
// and 3 latches, fed via the shell's raw-input tap. One screen.
class DebugMode : public Mode {
public:
    DebugMode();
    const char* name() const override { return "Debug"; }
    const uint16_t* icon() const override { return icons::kDebug; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return screen_; }
    void onRawInput(const RawInput& in) override;
    void update(uint32_t nowMs) override { nowMs_ = nowMs; }

private:
    struct Knob   { long total = 0; int lastDelta = 0; uint32_t lastMs = 0; };
    struct Button { unsigned count = 0; uint32_t lastMs = 0; };

    Knob     encKnob_[6];   // 1..5
    Button   encSw_[6];     // 1..5
    Button   latch_[4];     // 1..3
    // The shell delivers the latch LEVEL every main-loop frame; count only state
    // CHANGES (flips) so the telemetry does not spin at main-loop rate.
    bool     lastLatchOn_[4] = {false, false, false, false};  // 1..3
    uint32_t nowMs_ = 0;

    class DebugScreen : public Screen {
    public:
        explicit DebugScreen(DebugMode& m) : m_(m) {}
        const char* name() const override { return "io"; }
        void render(Display& d) const override;
    private:
        DebugMode& m_;
    };
    DebugScreen screen_{*this};
    friend class DebugScreen;
};

} // namespace core
