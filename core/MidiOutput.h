#pragma once

#include <cstdint>

namespace core {

// Abstract MIDI sink. Implementations push the MIDI Clock master stream
// and one-shot transport messages to the host.
//
// Clock generation is autonomous: the implementation owns its own timing
// source (hardware timer on Teensy, dedicated thread on the host) so that
// pulses stay on schedule even when the main loop is busy with display
// rendering or other work.
//
// Implementations:
//  - platform/teensy/TeensyMidiOutput.* (IntervalTimer + usbMIDI)
//  - platform/host/RtMidiOutput.*       (std::thread + RtMidi virtual port)
class MidiOutput {
public:
    virtual ~MidiOutput() = default;

    // Reconfigures the autonomous MIDI Clock master. With a non-zero BPM
    // the implementation emits 0xF8 status bytes at 24 PPQN (one every
    // 60 / BPM / 24 seconds). Passing 0 stops the clock entirely.
    virtual void setClockBpm(uint16_t bpm) = 0;

    // One-shot transport messages — sent synchronously from the caller.
    virtual void sendStart()    = 0;   // 0xFA
    virtual void sendContinue() = 0;   // 0xFB
    virtual void sendStop()     = 0;   // 0xFC
};

} // namespace core
