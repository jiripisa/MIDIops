#include "TeensyMidiOutput.h"

#include <Arduino.h>
#include <usb_midi.h>

void TeensyMidiOutput::sendClock() {
    usbMIDI.sendRealTime(usbMIDI.Clock);
}

void TeensyMidiOutput::sendStart() {
    usbMIDI.sendRealTime(usbMIDI.Start);
}

void TeensyMidiOutput::sendContinue() {
    usbMIDI.sendRealTime(usbMIDI.Continue);
}

void TeensyMidiOutput::sendStop() {
    usbMIDI.sendRealTime(usbMIDI.Stop);
}
