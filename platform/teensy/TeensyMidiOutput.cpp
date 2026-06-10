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

void TeensyMidiOutput::sendStart()    { usbMIDI.sendRealTime(usbMIDI.Start);    }
void TeensyMidiOutput::sendContinue() { usbMIDI.sendRealTime(usbMIDI.Continue); }
void TeensyMidiOutput::sendStop()     { usbMIDI.sendRealTime(usbMIDI.Stop);     }

void TeensyMidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16) return;
    usbMIDI.sendNoteOn(note, velocity, channel);
}

void TeensyMidiOutput::sendNoteOff(uint8_t channel, uint8_t note) {
    if (channel < 1 || channel > 16) return;
    usbMIDI.sendNoteOff(note, 0, channel);
}

uint32_t TeensyMidiOutput::consumeClockTicks() {
    noInterrupts();
    uint32_t n = g_clockTicks;
    g_clockTicks = 0;
    interrupts();
    return n;
}
