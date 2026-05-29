#pragma once

#include "core/MidiOutput.h"

// USB-MIDI output for Teensy 4.1. Sends real-time messages via the global
// `usbMIDI` object provided by the Teensy core. Requires the project to
// be built with the "MIDI + Serial" USB type (set via -D USB_MIDI_SERIAL).
class TeensyMidiOutput : public core::MidiOutput {
public:
    void sendClock()    override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;
};
