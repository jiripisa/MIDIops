#include "TeensyMidiOutput.h"

#include <Arduino.h>
#include <IntervalTimer.h>
#include <usb_midi.h>

namespace {

// Hardware timer + companion state for the autonomous clock master.
// Static-global so the ISR (a free function) can reach the counter
// without bouncing through `this`.
IntervalTimer g_clockTimer;
uint16_t      g_currentBpm = 0;

// Pulses queued by the ISR, drained by the main loop. Declared volatile
// because the ISR writes to it concurrently with main-loop reads.
volatile uint32_t g_pendingPulses = 0;

void clockIsr() {
    // Keep the ISR tiny — incrementing a counter is ~µs. The main loop
    // will pick this up on the next drainClockQueue() and push the
    // pulses through usbMIDI from a safe context.
    g_pendingPulses++;
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
    const uint32_t periodUs =
        60000000ul / (static_cast<uint32_t>(bpm) * 24ul);

    if (g_currentBpm == 0) {
        g_clockTimer.begin(clockIsr, periodUs);
    } else {
        // update() changes the period without resetting the phase, so a
        // BPM tweak doesn't visibly hiccup downstream gear.
        g_clockTimer.update(periodUs);
    }
    g_currentBpm = bpm;
}

void TeensyMidiOutput::drainClockQueue() {
    // Snapshot-and-clear the counter with interrupts disabled so the
    // ISR can't increment it between the read and the reset.
    noInterrupts();
    uint32_t n = g_pendingPulses;
    g_pendingPulses = 0;
    interrupts();
    while (n--) {
        usbMIDI.sendRealTime(usbMIDI.Clock);
    }
}

void TeensyMidiOutput::sendStart()    { usbMIDI.sendRealTime(usbMIDI.Start);    }
void TeensyMidiOutput::sendContinue() { usbMIDI.sendRealTime(usbMIDI.Continue); }
void TeensyMidiOutput::sendStop()     { usbMIDI.sendRealTime(usbMIDI.Stop);     }
