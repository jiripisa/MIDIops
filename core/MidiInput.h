#pragma once

#include "MidiMessage.h"

namespace core {

// Abstract MIDI source. Implementations:
//  - platform/teensy/TeensyMidiInput.* (usbMIDI)
//  - platform/host/RtMidiInput.*       (RtMidi virtual port)
class MidiInput {
public:
    virtual ~MidiInput() = default;

    // Non-blocking. Returns true if `out` was populated with a fresh message.
    // Call repeatedly until it returns false to drain all pending messages.
    virtual bool poll(MidiMessage& out) = 0;
};

} // namespace core
