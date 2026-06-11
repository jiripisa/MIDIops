#include "TeensyMidiInput.h"

#include <Arduino.h>
#include <usb_midi.h>

void TeensyMidiInput::begin() {
    // The global `usbMIDI` object is initialized by the Teensy core; nothing
    // to do here. We keep this method so callers can mirror the host setup.
}

bool TeensyMidiInput::poll(core::MidiMessage& out) {
    // Loop internally so an unrecognized message (Active Sensing 0xFE, SysEx,
    // …) is consumed and skipped rather than ending the caller's while(poll)
    // drain loop early with recognized notes/clock still queued behind it.
    while (usbMIDI.read()) {
        const uint8_t  rawType = usbMIDI.getType();
        const uint8_t  channel = usbMIDI.getChannel();   // 1..16
        const uint8_t  d1      = static_cast<uint8_t>(usbMIDI.getData1() & 0x7F);
        const uint8_t  d2      = static_cast<uint8_t>(usbMIDI.getData2() & 0x7F);

        core::MidiType type = core::MidiType::Unknown;
        switch (rawType) {
            case usbMIDI.NoteOff:           type = core::MidiType::NoteOff;           break;
            case usbMIDI.NoteOn:            type = core::MidiType::NoteOn;            break;
            case usbMIDI.AfterTouchPoly:    type = core::MidiType::PolyAftertouch;    break;
            case usbMIDI.ControlChange:     type = core::MidiType::ControlChange;     break;
            case usbMIDI.ProgramChange:     type = core::MidiType::ProgramChange;     break;
            case usbMIDI.AfterTouchChannel: type = core::MidiType::ChannelAftertouch; break;
            case usbMIDI.PitchBend:         type = core::MidiType::PitchBend;         break;
            case usbMIDI.Clock:             type = core::MidiType::Clock;             break;
            case usbMIDI.Start:             type = core::MidiType::Start;             break;
            case usbMIDI.Continue:          type = core::MidiType::Continue;          break;
            case usbMIDI.Stop:              type = core::MidiType::Stop;              break;
            default: continue;   // unrecognized: skip and keep draining
        }

        out.type    = type;
        out.channel = channel;
        out.data1   = d1;
        out.data2   = d2;
        return true;
    }
    return false;   // queue empty
}
