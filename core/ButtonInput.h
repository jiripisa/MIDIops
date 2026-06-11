#pragma once

namespace core {

// Abstract front-panel button / switch. The implementation samples the
// hardware and applies debouncing; consumers just call pollOn() from their
// main loop and treat the return value as ground truth.
//
// For a latching (toggle) switch the returned bool tracks the mechanical
// position — true while latched closed (LED lit), false while latched open.
// For a momentary push button it tracks whether the button is currently
// held down.
//
// Implementations:
//  - platform/teensy/TeensyButton.* (GPIO + debounce)
//  - host: SDL keyboard binding handled inline in main.cpp
class ButtonInput {
public:
    virtual ~ButtonInput() = default;
    virtual bool pollOn() = 0;
};

} // namespace core
