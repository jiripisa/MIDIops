#pragma once

#include <cstdint>

#include "core/MidiOutput.h"

// USB-MIDI output for Teensy 4.1. A hardware IntervalTimer fires at the
// MIDI Clock period and calls usbMIDI.sendRealTime() + usbMIDI.send_now()
// directly from its ISR — the timer gives hardware-precise pulse spacing
// (the float period overload keeps it sub-µs accurate) and send_now()
// force-flushes the USB TX buffer immediately, bypassing the 0–1 ms
// micro-frame delay that would otherwise smear pulses into a sawtooth
// jitter pattern. send_now() is non-blocking on T4.x so calling it from
// the ISR is the standard PJRC pattern for a low-jitter Clock master.
//
// The Teensy core's USB MIDI TX buffer is NOT interrupt-safe: its
// tx_head/tx_available are mutated without an IRQ guard. Because the clock
// pulse is sent from this ISR while the main loop sends notes/transport,
// every main-loop send is wrapped in noInterrupts()/interrupts() (see
// TeensyMidiOutput.cpp) to close the race — the ISR side already runs
// atomically w.r.t. the main loop on this single-core MCU.
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
    uint32_t consumeClockTicks() override;
};
