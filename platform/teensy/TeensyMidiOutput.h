#pragma once

#include <cstdint>

#include "core/MidiOutput.h"

// USB-MIDI output for Teensy 4.1.
//
// A hardware IntervalTimer fires at the MIDI Clock period and atomically
// increments a pending-pulse counter from its ISR. The main loop then
// calls drainClockQueue() to actually push the queued pulses through
// usbMIDI — this keeps the USB driver touching its TX/RX buffers from a
// single execution context (the ISR never calls into usbMIDI directly),
// which avoids contention with the USB controller's incoming MIDI path.
//
// Requires the project to be built with the "MIDI + Serial" USB type
// (-D USB_MIDI_SERIAL in platformio.ini).
class TeensyMidiOutput : public core::MidiOutput {
public:
    void setClockBpm(uint16_t bpm) override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;

    // Sends any clock pulses queued by the ISR since the last call. Cheap
    // when nothing is pending — call from the top of the main loop on
    // every iteration.
    void drainClockQueue();
};
