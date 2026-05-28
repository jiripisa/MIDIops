#pragma once

#include <cstdint>

#include "core/Button.h"

// Reads the debounced state of a panel-mount LED toggle switch wired to a
// Teensy GPIO pin. Tested with DFRobot Gravity DFR0789 — see the .cpp for
// the polarity rationale.
//
// 20 ms debounce. Call pollOn() once per loop iteration.
class TeensyButton : public core::ButtonInput {
public:
    explicit TeensyButton(uint8_t pin);

    void begin();
    bool pollOn() override;

private:
    uint8_t  pin_;
    bool     lastRaw_       = false;   // last sampled raw level
    bool     lastStable_    = false;   // last debounced level
    uint32_t lastChangeMs_  = 0;
};
