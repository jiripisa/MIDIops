#include "TeensyButton.h"

#include <Arduino.h>

namespace {
constexpr uint32_t kDebounceMs = 20;
}

TeensyButton::TeensyButton(uint8_t pin) : pin_(pin) {}

void TeensyButton::begin() {
    // DFRobot Gravity LED switch (DFR0789) and similar latching panel
    // buttons report their state on a single signal line whose polarity
    // is the opposite of what a naïve momentary-button reading would
    // assume:
    //
    //   * Latched closed (LED lit): the switch breaks the path to GND.
    //     Teensy's INPUT_PULLUP pulls the line HIGH.
    //   * Latched open (LED dark):  the switch shorts the line to GND,
    //     so it reads LOW.
    //
    // Therefore "switch is on" == "signal is HIGH". Sample once at boot
    // so the initial debounced state matches reality immediately.
    pinMode(pin_, INPUT_PULLUP);
    const bool initial = (digitalRead(pin_) == HIGH);
    lastRaw_       = initial;
    lastStable_    = initial;
    lastChangeMs_  = 0;
}

bool TeensyButton::pollOn() {
    const bool     raw = (digitalRead(pin_) == HIGH);
    const uint32_t now = millis();

    if (raw != lastRaw_) {
        lastRaw_      = raw;
        lastChangeMs_ = now;
    } else if (now - lastChangeMs_ >= kDebounceMs) {
        lastStable_ = raw;
    }
    return lastStable_;
}
