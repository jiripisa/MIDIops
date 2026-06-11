#include "TeensyMidiOutput.h"

#include <Arduino.h>
#include <IntervalTimer.h>
#include <usb_midi.h>

namespace {

IntervalTimer        g_clockTimer;
uint16_t             g_currentBpm = 0;
volatile uint32_t    g_clockTicks = 0;

void clockIsr() {
    // Send the clock pulse straight from the ISR, then force-flush the USB
    // TX buffer with send_now(). Without the flush the byte would sit in
    // the buffer until the next 1 ms USB micro-frame tick — which at 120
    // BPM (≈20.833 ms period) puts each pulse into a different phase of
    // the 1 ms grid, producing a sawtooth jitter that downstream DAWs
    // read as ±0.2 BPM wobble around the nominal tempo. send_now() kicks
    // the USB DMA immediately; it's non-blocking on Teensy 4.x so calling
    // it from an ISR is safe.
    usbMIDI.sendRealTime(usbMIDI.Clock);
    usbMIDI.send_now();
    ++g_clockTicks;
}

} // namespace

void TeensyMidiOutput::setClockBpm(uint16_t bpm) {
    if (bpm == g_currentBpm) return;

    if (bpm == 0) {
        g_clockTimer.end();
        g_currentBpm = 0;
        // Drop any ticks the ISR queued but the main loop hasn't drained yet,
        // so switching back to Internal later doesn't replay them as a phantom
        // burst. Guarded because the ISR writes g_clockTicks.
        noInterrupts();
        g_clockTicks = 0;
        interrupts();
        return;
    }

    // 24 PPQN: period = 60_000_000 / (BPM * 24) microseconds.
    // Use float to keep sub-microsecond precision — at 120 BPM the exact
    // period is 20833.333… µs, and the older integer truncation to 20833
    // µs leaked +0.002 BPM upward. Teensy 4.x IntervalTimer accepts a
    // float overload and resolves it through the 24 MHz GPT/PIT timer at
    // far below 1 µs granularity, so we keep the math honest at no cost.
    const float periodUs =
        60000000.0f / (static_cast<float>(bpm) * 24.0f);

    if (g_currentBpm == 0) {
        g_clockTimer.begin(clockIsr, periodUs);
    } else {
        // update() changes the period without resetting the phase, so a
        // BPM tweak doesn't visibly hiccup downstream gear.
        g_clockTimer.update(periodUs);
    }
    g_currentBpm = bpm;
}

// --------------------------------------------------------------------------
// usbMIDI TX race (N2)
//
// clockIsr() (above) sends a Clock byte from an IntervalTimer ISR while the
// main loop concurrently sends Start/Continue/Stop/NoteOn/NoteOff. The Teensy
// core's usb_midi_write_packed() mutates tx_head/tx_available with NO IRQ
// guard, so an ISR firing in the middle of a main-loop send can interleave and
// corrupt the buffer — dropping a NoteOff or garbling the stream.
//
// On this single-core MCU the ISR already runs atomically with respect to the
// main loop; the race is one-sided (only the ISR can preempt the main loop, not
// vice-versa). So we close it by making every main-loop send atomic w.r.t. the
// ISR: wrap the full send (including any implicit flush) in noInterrupts()/
// interrupts(). The clock keeps its low-jitter ISR-driven timing untouched.
// --------------------------------------------------------------------------

void TeensyMidiOutput::sendStart() {
    noInterrupts();
    usbMIDI.sendRealTime(usbMIDI.Start);
    interrupts();
}

void TeensyMidiOutput::sendContinue() {
    noInterrupts();
    usbMIDI.sendRealTime(usbMIDI.Continue);
    interrupts();
}

void TeensyMidiOutput::sendStop() {
    noInterrupts();
    usbMIDI.sendRealTime(usbMIDI.Stop);
    interrupts();
}

void TeensyMidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16) return;
    noInterrupts();
    usbMIDI.sendNoteOn(note, velocity, channel);
    interrupts();
}

void TeensyMidiOutput::sendNoteOff(uint8_t channel, uint8_t note) {
    if (channel < 1 || channel > 16) return;
    noInterrupts();
    usbMIDI.sendNoteOff(note, 0, channel);
    interrupts();
}

uint32_t TeensyMidiOutput::consumeClockTicks() {
    noInterrupts();
    uint32_t n = g_clockTicks;
    g_clockTicks = 0;
    interrupts();
    return n;
}
