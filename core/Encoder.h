#pragma once

namespace core {

// Abstract incremental (endless) rotary encoder. The implementation reads
// the hardware on demand and returns the net number of *detents* (clicks)
// that have happened since the last call. Positive means clockwise,
// negative counter-clockwise, zero means no movement.
//
// Implementations:
//  - platform/teensy/TeensyEncoder.* (KY-040 via PJRC Encoder library)
//  - host: arrow-key bindings in main.cpp (no class needed for SDL)
class EncoderInput {
public:
    virtual ~EncoderInput() = default;
    virtual int poll() = 0;
};

} // namespace core
