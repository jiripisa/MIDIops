#pragma once

#include <cstdint>

#include "core/Button.h"

// Reads the debounced state of a button wired to a Teensy GPIO pin.
// Always uses INPUT_PULLUP internally; the `activeHigh` constructor flag
// picks which raw level counts as "on":
//   - activeHigh = true  → DFRobot Gravity DFR0789 latching panel switch
//                          (signal floats HIGH via pull-up when latched
//                          closed; goes LOW when latched open)
//   - activeHigh = false → conventional momentary push-button that shorts
//                          the signal to GND when pressed (e.g. KY-040 SW)
//
// 20 ms debounce. Call pollOn() once per loop iteration.
class TeensyButton : public core::ButtonInput {
public:
    explicit TeensyButton(uint8_t pin, bool activeHigh = true);

    void begin();
    bool pollOn() override;

private:
    uint8_t  pin_;
    bool     activeHigh_;
    bool     lastRaw_       = false;   // last sampled raw level
    bool     lastStable_    = false;   // last debounced level
    uint32_t lastChangeMs_  = 0;
};
