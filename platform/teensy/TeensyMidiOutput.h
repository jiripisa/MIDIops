#pragma once

#include <cstdint>

#include "core/MidiOutput.h"

// USB-MIDI output for Teensy 4.1. A hardware IntervalTimer fires at the
// MIDI Clock period and calls usbMIDI.sendRealTime() directly from its
// ISR — that gives hardware-timer-precise pulse spacing (sub-microsecond
// jitter) independent of whatever the main loop is doing. The Teensy 4
// USB MIDI buffer is interrupt-safe for short status bytes, so this is
// the standard PJRC pattern for a MIDI Clock master.
//
// Requires the project to be built with the "MIDI + Serial" USB type
// (-D USB_MIDI_SERIAL in platformio.ini).
class TeensyMidiOutput : public core::MidiOutput {
public:
    void setClockBpm(uint16_t bpm) override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;
    void sendNoteOn (uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendNoteOff(uint8_t channel, uint8_t note) override;
};
