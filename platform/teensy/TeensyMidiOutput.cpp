#include "TeensyMidiOutput.h"

#include <Arduino.h>
#include <IntervalTimer.h>
#include <usb_midi.h>

namespace {

IntervalTimer g_clockTimer;
uint16_t      g_currentBpm = 0;

void clockIsr() {
    // Send the clock pulse straight from the ISR — usbMIDI's TX path
    // queues the single-byte real-time message in an interrupt-safe way
    // and the USB controller picks it up on the next 1 ms micro-frame.
    // Hardware-timer-driven pulses ⇒ sub-microsecond jitter, which is
    // exactly what an external DAW slave needs to lock cleanly.
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

void TeensyMidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16) return;
    usbMIDI.sendNoteOn(note, velocity, channel);
}

void TeensyMidiOutput::sendNoteOff(uint8_t channel, uint8_t note) {
    if (channel < 1 || channel > 16) return;
    usbMIDI.sendNoteOff(note, 0, channel);
}
