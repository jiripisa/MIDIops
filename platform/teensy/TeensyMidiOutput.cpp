#include "TeensyMidiOutput.h"

#include <Arduino.h>
#include <IntervalTimer.h>
#include <usb_midi.h>

namespace {

// IntervalTimer + companion state. Static-global so the ISR (a free
// function) can reach the timer without bouncing through `this`.
IntervalTimer g_clockTimer;
uint16_t      g_currentBpm = 0;

void clockIsr() {
    // The Teensy 4 USB MIDI ring buffer is interrupt-safe for short
    // status bytes, so we can call sendRealTime() directly from the
    // timer ISR — the clock pulse is queued and picked up by the USB
    // ISR on the next 1 ms USB micro-frame.
    usbMIDI.sendRealTime(usbMIDI.Clock);
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

void TeensyMidiOutput::sendStart()    { usbMIDI.sendRealTime(usbMIDI.Start);    }
void TeensyMidiOutput::sendContinue() { usbMIDI.sendRealTime(usbMIDI.Continue); }
void TeensyMidiOutput::sendStop()     { usbMIDI.sendRealTime(usbMIDI.Stop);     }
