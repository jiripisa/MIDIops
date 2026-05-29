#pragma once

#include <cstdint>

#include "core/MidiOutput.h"

// USB-MIDI output for Teensy 4.1. Owns a hardware IntervalTimer that
// fires the MIDI Clock master pulses from an ISR (independent of the
// main loop's rendering / polling cadence). Requires the project to be
// built with the "MIDI + Serial" USB type (-D USB_MIDI_SERIAL).
class TeensyMidiOutput : public core::MidiOutput {
public:
    void setClockBpm(uint16_t bpm) override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;
};
