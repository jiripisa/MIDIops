#include "TeensyButton.h"

#include <Arduino.h>

namespace {
constexpr uint32_t kDebounceMs = 20;
}

TeensyButton::TeensyButton(uint8_t pin, bool activeHigh)
    : pin_(pin), activeHigh_(activeHigh) {}

void TeensyButton::begin() {
    // Always enable the internal pull-up. The activeHigh_ flag then picks
    // which raw level we treat as "on" — see the header for the two
    // wiring conventions we support. Sample once at boot so the initial
    // debounced state matches reality without a 20 ms warm-up period.
    pinMode(pin_, INPUT_PULLUP);
    const int activeLevel = activeHigh_ ? HIGH : LOW;
    const bool initial = (digitalRead(pin_) == activeLevel);
    lastRaw_       = initial;
    lastStable_    = initial;
    lastChangeMs_  = 0;
}

bool TeensyButton::pollOn() {
    const int activeLevel = activeHigh_ ? HIGH : LOW;
    const bool     raw = (digitalRead(pin_) == activeLevel);
    const uint32_t now = millis();

    if (raw != lastRaw_) {
        lastRaw_      = raw;
        lastChangeMs_ = now;
    } else if (now - lastChangeMs_ >= kDebounceMs) {
        lastStable_ = raw;
    }
    return lastStable_;
}
