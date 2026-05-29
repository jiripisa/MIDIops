#pragma once

namespace core {

// Abstract MIDI sink. Implementations push system real-time messages
// (clock, transport) to the host.
//
// Implementations:
//  - platform/teensy/TeensyMidiOutput.* (usbMIDI.sendRealTime)
//  - platform/host/RtMidiOutput.*       (RtMidi virtual output port)
class MidiOutput {
public:
    virtual ~MidiOutput() = default;

    // System real-time MIDI messages — single status byte, no data.
    virtual void sendClock()    = 0;   // 0xF8
    virtual void sendStart()    = 0;   // 0xFA
    virtual void sendContinue() = 0;   // 0xFB
    virtual void sendStop()     = 0;   // 0xFC
};

} // namespace core
