#pragma once

#include "core/MidiInput.h"

// USB-MIDI input for Teensy 4.1. Requires the project to be built with the
// "MIDI + Serial" USB type (set via -D USB_MIDI_SERIAL in platformio.ini).
class TeensyMidiInput : public core::MidiInput {
public:
    void begin();
    bool poll(core::MidiMessage& out) override;
};
